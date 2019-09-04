/*
 Copyright (c) 2019 Carlos Montiers Aguilera
 Copyright (c) 2019 Jason Hood

 This software is provided 'as-is', without any express or implied
 warranty.  In no event will the authors be held liable for any damages
 arising from the use of this software.

 Permission is granted to anyone to use this software for any purpose,
 including commercial applications, and to alter it and redistribute it
 freely, subject to the following restrictions:

 1. The origin of this software must not be misrepresented; you must not
 claim that you wrote the original software. If you use this software
 in a product, an acknowledgment in the product documentation would be
 appreciated but is not required.

 2. Altered source versions must be plainly marked as such, and must not be
 misrepresented as being the original software.

 3. This notice may not be removed or altered from any source distribution.

 Carlos Montiers Aguilera   cmontiers@gmail.com
 Jason Hood                 jadoxa@yahoo.com.au

*/

#include "dll_enhancedbatch.h"

struct sCmdEntry {
	LPCWSTR   name;
	fnCmdFunc func;
	DWORD	  flags;
	DWORD	  helpid;
	DWORD	  exthelpid;
	DWORD	  helpextra;
};

#ifdef _WIN64
LPBYTE redirect;
BYTE oldCtrlCAborts[7];
#else
BYTE oldCtrlCAborts[5];
DWORD SFWork_esp;
BYTE SFWork_stdcall;
int SFWork_passed, SFWork_first;
#endif

int iPutMsg;
BYTE oldLexText[5], oldEchoOnOff[5], oldSFWorkmkstr[6], oldSFWorkresize[6];

DWORD_PTR SFWork_mkstr_org;
DWORD_PTR FreeStack;
LPDWORD gpDCount;
DWORD loopDCount;
BYTE SFWork_mkstr_reg;


#ifndef _WIN64
void MyLexTextESI(void)
{
	asm("call _MyLexText");
	asm("movl %eax,%esi");
}

int __stdcall stdPutStdErrMsg(UINT a, int b, UINT c, va_list *d)
{
	return MyPutStdErrMsg(a, b, c, d);
}

int __fastcall fastPutStdErrMsg(UINT a, int b, UINT c, va_list *d)
{
	return MyPutStdErrMsg(a, b, c, d);
}

int __fastcall fastPutStdErrMsg62(int b, va_list *d, UINT a, UINT c)
{
	return MyPutStdErrMsg(a, b, c, d);
}

void SFWork_mkstr(void)
{
	asm("mov _SFWork_first,%eax\n"
		"cmpl $0,(%ebp,%eax)\n"
		"jnz org\n"                 // first call is the normal mkstr
		"mov _loopDCount,%eax\n"
		"test %eax,%eax\n"
		"jnz 1f\n"                  // third and later calls free allocations
		"mov _gpDCount,%eax\n"      // second call remembers current allocations
		"mov (%eax),%eax\n"
		"mov %eax,_loopDCount\n"
		"jmp 2f\n"
		"org:\n"
		"andl $0,_loopDCount\n"
		"jmp *_SFWork_mkstr_org\n"
		"1:\n"
		"mov %eax,%ecx\n"           // 6.2 passes in eax
		"mov %esp,_SFWork_esp\n"    // 6.2.8102.0 is stdcall, but mkstr is fast
		"push %eax\n"
		"call *_FreeStack\n"        // free everything allocated between loops
		"mov _SFWork_esp,%esp\n"
		"2:\n"
		"mov _SFWork_passed,%eax\n"
		"mov (%ebp,%eax),%eax\n"
		"mov (%eax),%eax\n"         // reuse the original mkstr
		"mov -8(%eax),%ecx\n"       // no terminator is added, reset to 0
		"sub $8,%ecx\n"             // length that was requested
		"push %eax\n"
		"push %edi\n"
		"mov %eax,%edi\n"
		"xor %eax,%eax\n"
		"rep stosb\n"
		"pop %edi\n"
		"pop %eax\n"
		"mov _SFWork_mkstr_reg,%cl\n"
		"test %cl,%cl\n"
		"jnz 1f\n"                  // inline
		"cmp %cl,_SFWork_stdcall\n"
		"jz exit\n"                 // fastcall, just return
		"ret $4\n"                  // stdcall, tidy up
		"1:\n"
		"addl $0x35,(%esp)\n"       // skip over all the inline code
		"cmp $0xF6,%cl\n"           // select the register it uses
		"jne 1f\n"
		"mov %eax,%esi\n"
		"2:\n"
		"ret $8\n"
		"1:\n"
		"cmp $0xDB,%cl\n"
		"jne 1f\n"
		"mov %eax,%ebx\n"
		"jmp 2b\n"
		"1:\n"
		"mov %eax,%edi\n"
		"jmp 2b\n"
		"exit:");
}

#else

void SFWork_mkstr(void)
{
	asm("cmpb $0,cmdDebug(%rip)\n"      // debug version (only one, so far)
		"cmovnz 0xB0(%rsp),%r9d\n"      // retrieve the values
		"cmovnz %r12,%rdx\n"
		"test %r9d,%r9d\n"
		"jnz org\n"                     // first call is the normal mkstr
		"mov loopDCount(%rip),%ecx\n"
		"test %ecx,%ecx\n"
		"jnz 1f\n"                      // third and later free allocations
		"mov gpDCount(%rip),%rax\n"     // second remembers current allocations
		"mov (%rax),%ecx\n"
		"mov %ecx,loopDCount(%rip)\n"
		"jmp 2f\n"
		"org:\n"
		"andl $0,loopDCount(%rip)\n"
		"jmp *SFWork_mkstr_org(%rip)\n"
		"1:\n"
		"push %rdx\n"
		"sub $32,%rsp\n"                // shadow space
		"call *FreeStack(%rip)\n"       // free everything allocated between loops
		"add $32,%rsp\n"
		"pop %rdx\n"
		"2:\n"
		"mov (%rdx),%rax\n"             // reuse the original mkstr
		"mov -16(%rax),%rcx\n"          // no terminator is added, reset to 0
		"sub $16,%rcx\n"                // length that was requested
		"push %rax\n"
		"push %rdi\n"
		"mov %rax,%rdi\n"
		"xor %eax,%eax\n"
		"rep stosb\n"
		"pop %rdi\n"
		"pop %rax\n"
		"mov SFWork_mkstr_reg(%rip),%cl\n"
		"test %cl,%cl\n"
		"jz exit\n"
		"addq $0x49,(%rsp)\n"           // skip over all the inline code
		"cmp $0xFF,%cl\n"               // select the register it uses
		"cmove %rax,%rdi\n"
		"cmovne %rax,%rbx\n"
		"exit:");
}

#endif


void hookCmd(void)
{
	PIMAGE_DOS_HEADER pDosHeader;
	PIMAGE_NT_HEADERS pNTHeader;
	LPBYTE cmd;
	LPDWORD data, end;
	struct sCmdEntry *cmdentry;
	fnCmdFunc pMyEcho;
	const struct sCMD *ver;

	cmd = (LPBYTE) GetModuleHandle(NULL);
	data = (LPDWORD) cmd;
	pDosHeader = (PIMAGE_DOS_HEADER) data;
	pNTHeader = MakeVA(PIMAGE_NT_HEADERS, pDosHeader->e_lfanew);
	end = MakeVA(LPDWORD, pNTHeader->OptionalHeader.SizeOfImage);
	cmd_end = end;
	pMyEcho = MyEcho;

	// Search the image for the ECHO & SET help identifiers (to locate eEcho),
	// L"%s\r\n" (for its output) and the binary file version.
	while (data < end) {
		if (!eEcho) {
			cmdentry = (struct sCmdEntry *) data;
			if (cmdentry->helpid == 0x2392 &&		// ECHO
				(cmdentry+1)->helpid == 0x2389) {	// SET
				peEcho = &cmdentry->func;
				eEcho = cmdentry->func;
				WriteMemory(peEcho, &pMyEcho, sizeof(pMyEcho));
			}
		}
		if (!Fmt17) {
			if (data[0] == 0x00730025 &&	// L"%s"
				data[1] == 0x000a000d) {	// L"\r\n"
				Fmt17 = (LPWSTR) data;
			}
		}
		if (data[0] == VS_FFI_SIGNATURE) {
			cmdFileVersionMS = data[2];
			cmdFileVersionLS = data[3];
			cmdDebug = data[7] & VS_FF_DEBUG;
			break;
		}
		++data;
	}

	for (ver = cmd_versions; ver->offsets; ++ver) {
		if (ver->verMS == cmdFileVersionMS &&
			ver->verLS == cmdFileVersionLS &&
			*(LPCWSTR)(cmd + *ver->offsets) == L';') {

			int i;
			for (i = 0; i < OFFSETS; ++i) {
				cmd_addrs[i] = cmd + ver->offsets[i];
			}

			memcpy(oldCtrlCAborts, pCtrlCAborts, sizeof(oldCtrlCAborts));

			// Swap START & ECHO's help tests, so ECHO has no help and START
			// only looks at its first argument.
			WriteMemory(pStartHelp, (LPVOID) 9, 1);
			WriteMemory(pEchoHelp, (LPVOID) 31, 1);
			// Patch ECHO to always echo, ignoring state.
			memcpy(oldEchoOnOff, pEchoOnOff, 5);
#ifdef _WIN64
			if (cmdFileVersionMS == 0x50002) {
				// 5.2.*.*
				WriteMemory(pEchoOnOff, "\x6A\x03"  // push 3
										"\x59"      // pop rcx
										, 3);
			} else if (cmdFileVersionMS == 0x60002) {
				// 6.2.*.*
				WriteMemory(pEchoOnOff, "\x31\xC9"      // xor ecx,ecx
										"\x83\xC9\x01"  // or ecx,1
										, 5);
			} else {
				// 6.0.*.*
				// 6.1.*.*
				// 6.3.*.*
				// 10.*.*.*
				WriteMemory(pEchoOnOff, "\xB8\x03\x00\x00", 5);     // mov eax,3
			}
#else
			if (cmdFileVersionMS < 0x60002)  {
				// 5.*.*.*
				// 6.0.*.*
				// 6.1.*.*
				WriteMemory(pEchoOnOff, "\x58"      // pop eax
										"\x58"      // pop eax
										"\x6A\x03"  // push 3
										"\x58"      // pop eax
										, 5);
			} else if (cmdFileVersionMS == 0x60002) {
				if (cmdFileVersionLS == 0x1FA60000) {
					// 6.2.8102.0
					WriteMemory(pEchoOnOff, "\x90"      // nop
											"\x58"      // pop eax
											"\x6A\x03"  // push 3
											"\x58"      // pop eax
											, 5);
				} else {
					// 6.2.9200.16384
					WriteMemory(pEchoOnOff, "\x33\xC0"      // xor eax,eax
											"\x83\xC8\x01"  // or eax,1
											, 5);
				}
			} else {
				// 6.3.*.*
				// 10.*.*.*
				WriteMemory(pEchoOnOff, "\xB8\x03\x00\x00", 5);     // mov eax,3
			}
#endif

			// Patch FOR to fix a substitute bug - each one has its own memory.
			// Allocate once and reuse it.	It also frees memory allocated
			// during each loop.
			memcpy(oldSFWorkmkstr, pSFWorkmkstr, 6);
			memcpy(oldSFWorkresize, pSFWorkresize, 6);
			FreeStack = (DWORD_PTR) pFreeStack;
			gpDCount = (LPDWORD) pDCount;
#ifdef _WIN64
			if (*pSFWorkmkstr == 0xFF) {
				SFWork_mkstr_reg = pSFWorkmkstr[0x47];
				SFWork_mkstr_org = *(DWORD_PTR*)((DWORD_PTR)pSFWorkmkstr+6 + *(int *)(pSFWorkmkstr+2));
				WriteMemory(pSFWorkmkstr, (LPVOID) 0xE8, 1);	// call
				WriteMemory(pSFWorkmkstr+5, (LPVOID) 0x90, 1);	// nop
			} else {
				SFWork_mkstr_org = (DWORD_PTR)pSFWorkmkstr+5 + *(int *)(pSFWorkmkstr+1);
			}
			if (*pSFWorkresize == 0xFF) {
				WriteMemory(pSFWorkresize, "\x4C\x89\xC0"   // mov rax,r8
										   "\x0F\x1F"       // nop
										   , 6);			// include NUL
			} else {
				WriteMemory(pSFWorkresize, "\x48\x89\xC8"   // mov rax,rcx
										   "\x66\x90"       // nop
										   , 5);
			}
#else
			SFWork_passed = *pSFWorkpassed;
			SFWork_first = (cmdFileVersionMS >= 0x60002) ? 12 : 20;
			if (*pSFWorkmkstr == 0xE8) {
				SFWork_mkstr_org = (DWORD)pSFWorkmkstr+5 + *(int *)(pSFWorkmkstr+1);
				if (pSFWorkmkstr[-5] == 0x68) {
					SFWork_stdcall = 1;
				}
			} else {
				SFWork_mkstr_org = **(LPDWORD*)(pSFWorkmkstr+2);
				SFWork_mkstr_reg = pSFWorkmkstr[0x33];
				WriteMemory(pSFWorkmkstr, (LPVOID) 0xE8, 1);	// call
				WriteMemory(pSFWorkmkstr+5, (LPVOID) 0x90, 1);	// nop
			}
			i = (DWORD)SFWork_mkstr - ((DWORD)pSFWorkmkstr+5);
			WriteMemory(pSFWorkmkstr+1, &i, 4);
			if (*pSFWorkresize == 0xE8) {
				if (SFWork_stdcall) {
					WriteMemory(pSFWorkresize, "\x58"           // pop eax
											   "\x59"           // pop ecx
											   "\x66\x66\x90"   // nop
											   , 5);
				} else {
					WriteMemory(pSFWorkresize, "\x89\xC8"       // mov eax,ecx
											   "\x66\x66\x90"   // nop
											   , 5);
				}
			} else {
				WriteMemory(pSFWorkresize, "\x59"       // pop ecx
										   "\x59"       // pop ecx
										   "\x58"       // pop eax
										   "\x59"       // pop ecx
										   "\x66\x90"   // nop
										   , 6);
			}
#endif

			// Patch FOR to fix a bug with wildcard expansion - each name
			// accumulates, resizing bigger and bigger (this patch is not
			// undone on unload).
			// I've made the initial size big enough, no need to resize.
			WriteMemory(pForResize, (LPVOID) 0xEB, 1);
#ifdef _WIN64
			if (cmdFileVersionMS == 0x50002) {
				// 5.2.*.*
				WriteMemory(pForResize, "\x90\xE9", 2);
				WriteMemory(pForMkstr, "\x90\x90\x90"               // nop
									   "\x44\x8D\xA1\x00\x01\x00"   // lea r12d,rcx+256
									   , 10);
			} else if (cmdFileVersionMS == 0x60000 ||
					   cmdFileVersionMS == 0x60001) {
				// 6.0.*.*
				// 6.1.*.*
				WriteMemory(pForMkstr, "\x90\x90\x90"               // nop
									   "\x44\x8D\xA9\x00\x01\x00"   // lea r13d,rcx+256
									   , 10);
			} else if (cmdFileVersionMS == 0x60002) {
				if (cmdFileVersionLS == 0x1FA60000) {
					// 6.2.8102.0
					WriteMemory(pForMkstr, "\x90"                       // nop
										   "\x49\xFF\xC7"               // inc r15
										   "\x66\x42\x83\x3C\x79\x00"	// jmp word[rcx+r15*2],0
										   "\x75\xF5"                   // jnz inc
										   "\x41\x81\xC7\x00\x01\x00"   // add r15d,100
										   , 19);
				} else {
					// 6.2.9200.16384
					WriteMemory(pForMkstr, "\x90"                       // nop
										   "\x48\xFF\xC5"               // inc rbp
										   "\x66\x83\x3C\x69\x00"       // cmp word[rcx+rbp*2],0
										   "\x75\xF6"                   // jnz inc
										   "\x81\xC5\x00\x01\x00"       // add ebp,100
										   , 17);
				}
			} else if (cmdFileVersionMS == 0x60003 &&
					   cmdFileVersionLS == 0x24D70000 &&
					   cmdDebug) {
				// 6.3.9431.0u
				WriteMemory(pForMkstr, "\x90"                   // nop
									   "\x48\xFF\xC2"           // inc rdx
									   "\x66\x44\x39\x24\x51"   // cmp [rcx+rdx*2],r12w
									   "\x75\xF6"               // jnz inc
									   "\xFE\xC6"               // inc dh
									   "\x89\xD7", 15);         // mov edi,edx
			} else if (cmdFileVersionMS == 0xA0000 &&
					   HIWORD(cmdFileVersionLS) >= 17763) {
				// 10.0.17763.1
				// 10.0.18362.1
				WriteMemory(pForMkstr, "\x48\xFF\xC2"           // inc rdx
									   "\x90"                   // nop
									   "\x66\x44\x39\x34\x51"   // cmp [rcx+rdx*2],r14w
									   "\x75\xF5"               // jnz inc
									   "\xFE\xC6"               // inc dh
									   "\x89\xD7", 15);         // mov edi,edx
			} else {
				// 6.3.*.*
				// 10.0.*.*
				WriteMemory(pForMkstr, "\x90"                   // nop
									   "\x48\xFF\xC2"           // inc rdx
									   "\x66\x39\x2C\x51"       // cmp [rcx+rdx*2],bp
									   "\x75\xF7"               // jnz inc
									   "\xFE\xC6"               // inc dh
									   "\x89\xD7", 14);         // mov edi,edx
			}
#else
			if (cmdFileVersionMS == 0x50000) {
				// 5.0.*.*
				WriteMemory(pForMkstr, "\xEB\x00\xFE\xC4", 4);  // jmp $+2; inc ah
			} else if (HIWORD(cmdFileVersionMS) == 5) {
				if (LOWORD(cmdFileVersionLS) == 0) {
					// 5.1.2600.0
					// 5.2.3790.0
					WriteMemory(pForMkstr, pForMkstr+2, 8);
					WriteMemory(pForMkstr+8, "\xFE\xC4", 2);    // inc ah
				} else {
					// 5.1.*.*
					// 5.2.*.*
					WriteMemory(pForMkstr, pForMkstr+6, 8);
					WriteMemory(pForMkstr+8, "\x81\xC0\x00\x01\x00", 6);    // add eax,256
				}
			} else if (cmdFileVersionMS == 0x60000) {
				// 6.0.*.*
				WriteMemory(pForMkstr, "\xFE\xC4"   // inc ah
									   "\x93", 3);	// xchg ebx,eax
			} else if (cmdFileVersionMS == 0x60001) {
				// 6.1.*.*
				WriteMemory(pForMkstr, "\xFE\xC4"   // inc ah
									   "\x97", 3);	// xchg edi,eax
			} else if (cmdFileVersionMS == 0x60002) {
				// 6.2.*.*
				WriteMemory(pForMkstr, "\x85\xC0"   // test eax,eax
									   "\x75\xF6"   // jnz $-8
									   "\x2B\xD1"   // sub edx,ecx
									   "\xD1\xFA"   // sar edx,1
									   "\xFE\xC6"   // inc dh
									   "\x89\xD3"   // mov ebx,edx
									   , 12);
			} else {
				// 6.3.*.*
				// 10.0.*.*
				WriteMemory(pForMkstr, "\xFE\xC6"   // inc dh
									   "\x92", 3);	// xchg edx,eax
			}
#endif

#ifdef _WIN64
			// CMD and the DLL could be more than 2GiB apart, so allocate some
			// memory before CMD to near jump to, which then does an absolute
			// jump to the DLL.
			static const char redirect_code[] =
			/*	0 */								// MyPutStdErrMsg
			/*	8 */								// MyLexText

			/* 16 */	"\xFF\x25\xEA\xFF\xFF\xFF"  // jmp [redirect]

			/* 22 */	"\xBB\00\x40\x00\x00"       // mov ebx,0x4000
			/* 27 */	"\xFF\x25\xE7\xFF\xFF\xFF"  // jmp [redirect+8]

			/* 33 */	"\x81\xFA\x7B\x23\x00\x00"  // cmp edx,0x237b ;Terminate batch job?
						"\x75\x05"                  // jne not_terminate
			/* 41 */	"\xB8\x00\x00\x00\x00"      // mov eax,0 ;or 1
						"\xC3"                      // ret
						"\x58"                      // pop rax ;return address
						"\x48\x83\xC0\x02"          // add rax,2 ;skip RET/NOP
			/* 52 */	"???????"                   // original code
						"\x50"                      // push rax
						"\xC3"                      // ret
						"???"                       // alignment
			/* 64 */	"????????"                  // SFWork_mkstr
			/* 72 */	"\xFF\x25\xF2\xFF\xFF\xFF"  // jmp [redirect+64]
			;

			for (redirect = cmd - 0x1000;;) {
				LPVOID mem = VirtualAlloc(redirect, 16 + sizeof(redirect_code),
										  MEM_COMMIT | MEM_RESERVE,
										  PAGE_EXECUTE_READWRITE);
				if (mem) {
					redirect = mem;
					break;
				} else {
					MEMORY_BASIC_INFORMATION mbi;
					VirtualQuery(redirect, &mbi, sizeof(mbi));
					redirect = (LPBYTE)mbi.AllocationBase - 0x1000;
				}
			}
			((LPVOID *)redirect)[0] = MyPutStdErrMsg;
			memcpy(redirect + 16, redirect_code, sizeof(redirect_code));
			memcpy(redirect + 52, pCtrlCAborts, 7);
			((LPVOID *)redirect)[8] = SFWork_mkstr;
			i = (DWORD_PTR)redirect + 72 - (DWORD_PTR)pSFWorkmkstr - 5;
			WriteMemory(pSFWorkmkstr+1, &i, 4);
#endif

			// Hook PutStdErr to write the batch file name and line number.
			iPutMsg = *pPutStdErrMsg;
			pPutMsg = (LPVOID)((DWORD_PTR)pPutStdErrMsg + 4 + iPutMsg);
#ifdef _WIN64
			i = (DWORD_PTR)redirect + 16 - (DWORD_PTR)pPutStdErrMsg - 4;
#else
			if (cmdFileVersionMS > 0x60002) {
				i = (DWORD_PTR)fastPutStdErrMsg;
			} else if (cmdFileVersionMS == 0x60002) {
				i = (DWORD_PTR)fastPutStdErrMsg62;
			} else {
				i = (DWORD_PTR)stdPutStdErrMsg;
			}
			i -= (DWORD_PTR)pPutStdErrMsg + 4;
#endif
			WriteMemory(pPutStdErrMsg, &i, 4);

			// Hook Lex text type to process Unicode characters.
			memcpy(oldLexText, pLexText, 5);
			WriteMemory(pLexText, (LPVOID) 0xE8, 1); // call
#ifdef _WIN64
			((LPVOID *)redirect)[1] = MyLexText;
			if (cmdDebug) {
				// Currently only the one debug version.
				/*if (cmdFileVersionMS = 0x60003 &&
					cmdFileVersionLS == 0x24d70000) */
				i = (DWORD_PTR)redirect + 22;
			} else {
				i = (DWORD_PTR)redirect + 27;
			}
#else
			if (cmdDebug) {
				// Currently only the one debug version.
				/*if (cmdFileVersionMS = 0x60003 &&
					cmdFileVersionLS == 0x24d70000) */
				i = (DWORD_PTR)MyLexTextESI;
			} else {
				i = (DWORD_PTR)MyLexText;
			}
#endif
			i -= (DWORD_PTR)pLexText + 5;
			WriteMemory(pLexText + 1, &i, 4);

			break;
		}
	}
}

void hookCtrlCAborts(char aborts)
{
	if (aborts == -1) {
		WriteMemory(pCtrlCAborts, oldCtrlCAborts, sizeof(oldCtrlCAborts));
	} else {
#ifdef _WIN64
		char code[7];
		code[0] = 0xE8; 		// call redirect+33
		*(int *)(code+1) = (DWORD_PTR)redirect+33 - ((DWORD_PTR)pCtrlCAborts+5);
		code[5] = 0xC3; 		// ret
		code[6] = 0x90; 		// nop
		redirect[42] = aborts;
#else
		char code[5];
		if (cmdFileVersionMS < 0x60003) {
			code[0] = 0x58; 	// pop eax ;0
			code[1] = 0x59; 	// pop ecx
		} else {
			code[0] = 0x33; 	// xor eax,eax
			code[1] = 0xC0;
		}
		code[2] = 0x59; 		// pop ecx
		code[3] = 0xB0; 		// mov al, aborts
		code[4] = aborts;
#endif
		WriteMemory(pCtrlCAborts, code, sizeof(code));
	}
}

void unhookCmd(void)
{
	WriteMemory(peEcho, &eEcho, sizeof(eEcho));
	WriteMemory(pPutStdErrMsg, &iPutMsg, 4);
	WriteMemory(pLexText, oldLexText, 5);
	WriteMemory(pEchoOnOff, oldEchoOnOff, 5);
	WriteMemory(pStartHelp, (LPVOID) 31, 1);
	WriteMemory(pEchoHelp, (LPVOID) 9, 1);
	WriteMemory(pCtrlCAborts, oldCtrlCAborts, sizeof(oldCtrlCAborts));
	WriteMemory(pSFWorkmkstr, oldSFWorkmkstr, 6);
	WriteMemory(pSFWorkresize, oldSFWorkresize, 6);

	*pfDumpTokens = 0;
	*pfDumpParse = 0;

#ifdef _WIN64
	VirtualFree(redirect, 0, MEM_RELEASE);
#endif
}