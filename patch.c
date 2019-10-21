/*
 Copyright (c) 2019 Carlos Montiers Aguilera
 Copyright (c) 2019 Jason Hood

 This software is provided 'as-is', without any express or implied
 warranty. In no event will the authors be held liable for any damages
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

#include "khash.h"
#ifdef _WIN64
KHASH_MAP_INIT_INT64(ptrdw, DWORD)
#else
KHASH_MAP_INIT_INT(ptrdw, DWORD)
#endif

khash_t(ptrdw) *sfwork_map;


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
BYTE SFWork_saved;
#else
BYTE oldCtrlCAborts[5];
DWORD SFWork_esp, ParseForF_ret;
BYTE SFWork_stdcall;
int SFWork_saved, SFWork_passed, SFWork_first;
DWORD ForFbegin_org, ParseFor_org, ParseForF_org;
#endif

int oldPutMsg, oldParseFor, oldParseForF;
BYTE oldLexText[5], oldEchoOnOff[5], oldSFWorkmkstr[6], oldSFWorkresize[6];
BYTE oldForFbegin[6], oldForFend[6];
DWORD_PTR SFWork_mkstr_org, FreeStack, ForFend_org;
BYTE SFWork_mkstr_reg;

int ForF_stack[FORF_STACKSIZE], ForF_stacktop;


int SFWork_hook(LPWSTR saved, LPWSTR *passed, int first)
{
	khint_t k;
	int absent;

	if (saved == NULL) {
		k = kh_put(ptrdw, sfwork_map, (DWORD_PTR) passed, &absent);
		kh_val(sfwork_map, k) = 0;
		return 0;
	}
	k = kh_get(ptrdw, sfwork_map, (DWORD_PTR) passed);
	if (kh_val(sfwork_map, k) == 0 || first) {
		kh_val(sfwork_map, k) = *pDCount;
		return -1;
	}
	return kh_val(sfwork_map, k);
}


void ForFbegin_hook(void)
{
	if (++ForF_stacktop == lenof(ForF_stack)) {
		ForF_stacktop = 0;
	}
	ForF_stack[ForF_stacktop] = 1;
}

void __attribute((fastcall)) ForFend_hook(BYTE end)
{
	if (end || *pGotoFlag) {
		if (--ForF_stacktop == -1) {
			ForF_stacktop = lenof(ForF_stack) - 1;
		}
	} else {
		++ForF_stack[ForF_stacktop];
	}
}


void ParseFor(void)
{
	DWORD len;

	LPWSTR p = *pLexBufPtr;
	while (iswspace(*p)) {
		++p;
	}
	if ((p[0] | 0x20) == L'd' && (p[1] | 0x20) == L'o' && iswspace(p[2])) {
		// Translate "for do ..." to "for %_ in (:*) do ...".
		len = wcslen(*pLexBufPtr);
		if (*pLexBufPtr + 10 + len >= pLexBufferend) {
			return;
		}
		memmove(*pLexBufPtr + 10, *pLexBufPtr, (len + 1) * sizeof(wchar_t));
		memcpy(*pLexBufPtr, L"%_ in (:*)", 10 * sizeof(wchar_t));
	} else if (p[0] == L'%' && p[1] != L'\0' && iswspace(p[2]) &&
			   ((p[3] | 0x20) == L'd' && (p[4] | 0x20) == L'o' && iswspace(p[5]))) {
		// Translate "for %? do ..." to "for %? in (:range*) do ...".
		p += 3;
		len = wcslen(p);
		if (p + 13 + len >= pLexBufferend) {
			return;
		}
		memmove(p + 13, p, (len + 1) * sizeof(wchar_t));
		memcpy(p, L"in (:range*) ", 13 * sizeof(wchar_t));
	} else if (p[0] == L'%' && p[1] != L'\0' && iswspace(p[2])
			   && (iswdigit(p[3]) || (p[3] == L'-' && iswdigit(p[4]))
				   || (*pfDelayedExpansion && p[3] == L'!'))) {
		// Translate "for %? N ..." to "for %? in (:range*N) ...".
		int digits = 1;
		p += 4;
		if (p[-1] == L'!') {
			if (*p == L'!') {
				return;
			}
			for (;;) {
				if (*p == L'\0') {
					return;
				}
				++digits;
				++p;
				if (p[-1] == L'!') {
					break;
				}
			}
		} else {
			while (iswdigit(*p) || *p == L':' || *p == L'-') {
				++digits;
				++p;
			}
		}
		len = wcslen(p);
		if (p + 12 + len >= pLexBufferend) {
			return;
		}
		memmove(p + 12, p, (len + 1) * sizeof(wchar_t));
		p[11] = L')';
		memmove(p + 11 - digits, p - digits, digits * sizeof(wchar_t));
		memcpy(p - digits, L"in (:range*", 11 * sizeof(wchar_t));
	}
}

void ParseForF(void)
{
	LPWSTR p = pTmpBuf;
	if (*p == L'%' || *p == L'/') {
		return;
	}
	if (*p == L'"' || *p == L'\'') {
		++p;
	}
	for (;;) {
		while (*p <= L' ') {
			if (*p == L'\0') {
				return;
			}
			++p;
		}
		if (WCSIBEG(p, L"line")) {
			// Translate "... line..." to "...     ... delims= eol=".
			p[0] = p[1] = p[2] = p[3] = L' ';
			p += wcslen(p);
			if (p[-1] == L'"' || p[-1] == L'\'') {
				--p;
			}
			wcscpy(p, L" delims= eol=");
			*pTokLen = wcslen(pTmpBuf) + 1; 	// includes NUL
			return;
		}
		while (*p > L' ') {
			++p;
		}
	}
}


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
	asm(
		"push %ecx\n"               // preserve possible register argument
		"mov _SFWork_first,%eax\n"
		"pushl (%eax,%ebp)\n"
		"mov _SFWork_passed,%eax\n"
		"pushl (%eax,%ebp)\n"
		"mov _SFWork_saved,%eax\n"
		"test %eax,%eax\n"
		"mov (%eax,%ebp),%eax\n"
		"js 1f\n"
		"mov -28(%ebp),%ecx\n"      // debug version doesn't store it
		"lea 4(%ecx,%eax,4),%eax\n"
		"1:\n"
		"pushl (%eax)\n"
		"call _SFWork_hook\n"
		"add $12,%esp\n"
		"pop %ecx\n"
		"test %eax,%eax\n"
		"js 1f\n"
		"jz org\n"
		"mov %eax,%ecx\n"           // 6.2.9200.16384 passes in eax, 6.3+ in ecx
		"mov %esp,_SFWork_esp\n"    // 6.2.8102.0 is stdcall, but mkstr is fast
		"push %eax\n"
		"call *_FreeStack\n"        // free everything allocated between loops
		"mov _SFWork_esp,%esp\n"
		"1:\n"
		"mov _SFWork_passed,%eax\n"
		"mov (%eax,%ebp),%eax\n"
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
		"addl $0x34,(%esp)\n"       // skip over all the inline code
		"cmp $0xF6,%cl\n"           // select the register it uses
		"cmove %eax,%esi\n"
		"cmovb %eax,%ebx\n"         // 0xDB
		"cmova %eax,%edi\n"         // 0xFF
		"ret $8\n"
		"org:\n"
		"jmp *_SFWork_mkstr_org\n"
		"exit:"
	);
}

void ForFend(void)
{
	asm(
		"pushf\n"
		"setnc %cl\n"
		"call _ForFend_hook\n"
		"popf\n"
		"jnc 1f\n"
		"mov _ForFend_org,%eax\n"
		"mov %eax,(%esp)\n"
		"1:"
	);
}

void ForFend_opp(void)
{
	asm(
		"pushf\n"
		"setnc %cl\n"
		"call _ForFend_hook\n"
		"popf\n"
		"jc 1f\n"
		"mov _ForFend_org,%eax\n"
		"mov %eax,(%esp)\n"
		"1:"
	);
}

void ForFbegin_jmp(void)
{
	asm(
		"call _ForFbegin_hook\n"
		"jmp *_ForFbegin_org\n"
	);
}

void ParseFor_hook(void)
{
	asm(
		"push %eax\n"
		"push %ecx\n"
		"call _ParseFor\n"
		"pop %ecx\n"
		"pop %eax\n"
		"jmp *_ParseFor_org\n"
	);
}

void ParseForF_hook(void)
{
	asm(
		"pop _ParseForF_ret\n"
		"call *_ParseForF_org\n"
		"push _ParseForF_ret\n"
		"jmp _ParseForF\n"
	);
}

#else

void SFWork_mkstr(void)
{
	asm(
		"cmpb $0,cmdDebug(%rip)\n"      // debug version (only one, so far)
		"cmovnz 0xB0(%rsp),%r9d\n"      // retrieve the values
		"cmovnz %r12,%rdx\n"
		"mov %r9d,%r8d\n"
		"push %rcx\n"
		"push %rdx\n"
		"movzxb SFWork_saved(%rip),%eax\n"
		"test $0x80,%al\n"
		"jns 2f\n"
		"cmp $0xA0,%al\n"
		"je 1f\n"
		"mov 0x18(%rsp,%rax),%rax\n"    // 0x90, 0x80
		"mov 8(%rax,%rdi,8),%rcx\n"
		"jmp 3f\n"
		"org:\n"
		"jmp *SFWork_mkstr_org(%rip)\n"
		"1:\n"
		"mov 8(%rsp,%rax),%rax\n"       // 0xA0
		"mov 8(%rax,%r13,8),%rcx\n"
		"jmp 3f\n"
		"2:\n"
		"cmp $0x34,%al\n"
		"jne 1f\n"
		"mov (%r14),%rcx\n"             // 0x34
		"jmp 3f\n"
		"1:\n"
		"cmova %r15,%rax\n"             // 0x3C
		"cmovb %r13,%rax\n"             // 0x2C
		"mov 8(%rax),%rcx\n"
		"3:\n"
		"sub $32,%rsp\n"                // shadow space
		"call SFWork_hook\n"
		"add $32,%rsp\n"
		"pop %rdx\n"
		"pop %rcx\n"
		"test %eax,%eax\n"
		"js 1f\n"
		"jz org\n"
		"push %rdx\n"
		"sub $32,%rsp\n"                // shadow space
		"mov %eax,%ecx\n"
		"call *FreeStack(%rip)\n"       // free everything allocated between loops
		"add $32,%rsp\n"
		"pop %rdx\n"
		"1:\n"
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
		"addq $0x48,(%rsp)\n"           // skip over all the inline code
		"cmp $0xFF,%cl\n"               // select the register it uses
		"cmove %rax,%rdi\n"
		"cmovne %rax,%rbx\n"
		"exit:");
}

// C can't access labels created in asm, so store the offsets in the function
// and read them from there.
#define redirect_data		((LPDWORD)redirect_code+1)
#define redirect_code_start ((LPBYTE)redirect_code + redirect_data[0])
#define redirect_code_size	redirect_data[1]
#define rMyPutStdErrMsg 	(redirect + redirect_data[2])
#define rMyLexText			(redirect + redirect_data[3])
#define rPromptUser 		(redirect + redirect_data[4])
#define rAbortFlag			(redirect + redirect_data[5])
#define rPromptUserOrg		(redirect + redirect_data[6])
#define rSFWork_mkstr		(redirect + redirect_data[7])
#define rForFbegin			(redirect + redirect_data[8])
#define rForFend			(redirect + redirect_data[9])
#define rForFendj			(redirect + redirect_data[10])
#define rParseFor			(redirect + redirect_data[11])
#define rParseForF			(redirect + redirect_data[12])
#define rParseFor_org		(DWORD_PTR*)(redirect + redirect_data[13])
#define rParseForF_org		(DWORD_PTR*)(redirect + redirect_data[13]+8)

// This code gets relocated to a region of memory closer to CMD, to stay within
// the 32-bit relative address range.
void redirect_code(void)
{
	asm(
		"ret\n"                 // never called, but it avoids dereferencing
		".align 4\n"            // redirect_code and the strict alias warning
		".long redirect_code_start - redirect_code\n"
		".macro rel label\n"
		".long \\label - redirect_code_start\n"
		".endm\n"
		"rel redirect_code_end\n"
		"rel rMyPutStdErrMsg\n"
		"rel rMyLexText\n"
		"rel rPromptUser\n"
		"rel rAbortFlag+1\n"
		"rel rPromptUserOrg\n"
		"rel rSFWork_mkstr\n"
		"rel rForFbegin\n"
		"rel rForFend\n"
		"rel rForFendj\n"
		"rel rParseFor\n"
		"rel rParseForF\n"
		"rel rParseFor_org\n"

		"redirect_code_start:\n"

		"rMyPutStdErrMsg:\n"
		"jmp *aMyPutStdErrMsg(%rip)\n"

		"mov $0x4000,%ebx\n"            // used by the debug version
		"rMyLexText:\n"
		"jmp *aMyLexText(%rip)\n"

		"rPromptUser:\n"
		"cmp $0x237b,%edx\n"            // Terminate batch job?
		"jne 1f\n"
		"rAbortFlag:\n"
		"mov $0,%eax\n"                 // value patched in
		"ret\n"
		"1:\n"
		"pop %rax\n"                    // return address
		"add $2,%rax\n"                 // skip RET/NOP
		"rPromptUserOrg:\n"
		".fill 7,1,0x90\n"              // original code patched in
		"push %rax\n"
		"ret\n"

		"rSFWork_mkstr:\n"
		"jmp *aSFWork_mkstr(%rip)\n"

		"rForFbegin:\n"
		"push %rdx\n"                   // one of these registers may contain LF
		//"push %r8\n"                  // these registers aren't used by the hook
		//"push %r9\n"
		//"push %r10\n"
		//"sub $32,%rsp\n"              // shadow space not needed
		"call *aForFbegin_hook(%rip)\n"
		//"add $32,%rsp\n"
		//"pop %r10\n"
		//"pop %r9\n"
		//"pop %r8\n"
		"pop %rdx\n"
		"ret\n"

		"rForFend:\n"
		"pushf\n"
		"push %rdx\n"
		//"push %r8\n"
		//"push %r9\n"
		//"push %r10\n"
		"setnc %cl\n"
		//"sub $32,%rsp\n"
		"call *aForFend_hook(%rip)\n"
		//"add $32,%rsp\n"
		//"pop %r10\n"
		//"pop %r9\n"
		//"pop %r8\n"
		"pop %rdx\n"
		"popf\n"
		"rForFendj:\n"
		"jnc 1f\n"                      // possibly patched to jc
		"movabs ForFend_org,%rax\n"
		"mov %rax,(%rsp)\n"
		"1:\n"
		"ret\n"

		"rParseFor:\n"
		"push %rcx\n"
		"push %rdx\n"
		"call *aParseFor(%rip)\n"
		"pop %rdx\n"
		"pop %rcx\n"
		"jmp *aParseFor_org(%rip)\n"

		"rParseForF:\n"
		"sub $32,%rsp\n"
		"call *aParseForF_org(%rip)\n"
		"add $32,%rsp\n"
		"push %rax\n"
		"call *aParseForF(%rip)\n"
		"pop %rax\n"
		"ret\n"

		".align 8\n"
		".macro abs label\n"
		"a\\label: .quad \\label\n"
		".endm\n"
		"abs MyPutStdErrMsg\n"
		"abs MyLexText\n"
		"abs SFWork_mkstr\n"
		"abs ForFbegin_hook\n"
		"abs ForFend_hook\n"
		"abs ParseFor\n"
		"abs ParseForF\n"
		"rParseFor_org:\n"
		"aParseFor_org: .quad 0\n"
		"aParseForF_org: .quad 0\n"

		"redirect_code_end:\n"
	);
}

#endif

#define WriteCode(addr, code) \
	do { \
		static const char insns[sizeof(code)-1] = { code }; \
		WriteMemory(addr, insns, sizeof(insns)); \
	} while (0)

// Create a displacement to TO from FROM (the end of the instruction).
#define MKDISP(to, from) ((DWORD_PTR)(to) - (DWORD_PTR)(from))

// Given the address of a displacement get its absolute destination.
#define MKABS(disp) ((DWORD_PTR)(disp)+4 + *(int *)(disp))

void hookCmd(void)
{
	PIMAGE_DOS_HEADER pDosHeader;
	PIMAGE_NT_HEADERS pNTHeader;
	LPBYTE cmd;
	LPDWORD data, end;
	struct sCmdEntry *cmdentry;
	fnCmdFunc pMyEcho, pMyCall;
	const struct sCMD *ver;
	int i;
	struct __attribute__((gcc_struct,packed)) {
		char nop;
		char op;
		int  disp;
	} call;

	cmd = (LPBYTE) GetModuleHandle(NULL);
	data = (LPDWORD) cmd;
	pDosHeader = (PIMAGE_DOS_HEADER) data;
	pNTHeader = MakeVA(PIMAGE_NT_HEADERS, pDosHeader->e_lfanew);
	end = MakeVA(LPDWORD, pNTHeader->OptionalHeader.SizeOfImage);
	cmd_end = end;
	pMyEcho = MyEcho;
	pMyCall = MyCall;

	sfwork_map = kh_init(ptrdw);

	// Search the image for the ECHO & CALL help identifiers (to locate eEcho &
	// eCall), L"%s\r\n" (for ECHO's output) and the binary file version.
	while (data < end) {
		if (!eEcho) {
			cmdentry = (struct sCmdEntry *) data;
			if (cmdentry->helpid == 0x2392 &&		// ECHO
				(cmdentry+14)->helpid == 0x238F) {	// CALL
				peEcho = &cmdentry->func;
				eEcho = cmdentry->func;
				WriteMemory(peEcho, &pMyEcho, sizeof(pMyEcho));
				peCall = &cmdentry[14].func;
				eCall = cmdentry[14].func;
				WriteMemory(peCall, &pMyCall, sizeof(pMyCall));
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

	// Use eCall to get the address of LastRetCode.
	LPBYTE p = (LPBYTE) eCall;
#ifdef _WIN64
	p += cmdDebug ? 25 : 15;
	pLastRetCode = (int *)MKABS(p);
#else
	while (*p++ != 0xA3) ;
	pLastRetCode = *(int **)p;
#endif

	// This will be found because the loader finds it first.
	ver = cmd_versions;
	while (ver->verMS != cmdFileVersionMS ||
		   ver->verLS != cmdFileVersionLS ||
		   *(LPCWSTR)(cmd + *ver->offsets) != L';') {
		++ver;
	}
	for (i = 0; i < OFFSETS; ++i) {
		cmd_addrs[i] = cmd + ver->offsets[i];
	}

#ifdef _WIN64
	// CMD and the DLL could be more than 2GiB apart, so allocate some memory
	// before CMD to keep within the 32-bit relative address range.
	for (redirect = cmd - 0x1000;;) {
		LPVOID mem = VirtualAlloc(redirect, redirect_code_size,
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
	memcpy(redirect, redirect_code_start, redirect_code_size);
	memcpy(rPromptUserOrg, pCtrlCAborts, 7);
	if (pForFend[1] == 0x83) {	// 6.2.8102.0 uses jnc, not jc.
		--*rForFendj;			// jnc -> jc
	}
#endif

	oldPutMsg = *pPutStdErrMsg;
	oldParseFor = *pParseFortoken;
	oldParseForF = *pForFoptions;
	memcpy(oldCtrlCAborts, pCtrlCAborts, sizeof(oldCtrlCAborts));
	memcpy(oldLexText, pLexText, 5);
	memcpy(oldEchoOnOff, pEchoOnOff, 5);
	memcpy(oldSFWorkmkstr, pSFWorkmkstr, 6);
	memcpy(oldSFWorkresize, pSFWorkresize, 6);
	memcpy(oldForFbegin, pForFbegin, 6);
	memcpy(oldForFend, pForFend, 6);

	// Only check the first argument for help (pEchoHelp points to the ECHO
	// command byte, which is followed by je).
	if (*++pEchoHelp == 0x0F) {
		WriteMemory(pEchoHelp, "\x90\xE9", 2);
	} else {
		WriteByte(pEchoHelp, 0xEB);
	}

#ifdef _WIN64
	call.nop = 0x40;	// dummy REX prefix
#else
	call.nop = 0x90;	// nop
#endif
	call.op = 0xE8; 	// call

	// Patch FOR to fix a substitute inefficiency: each loop has its own
	// memory.	Allocate once and reuse it.  Also free other memory allocated
	// during each loop.
	FreeStack = (DWORD_PTR) pFreeStack;
	SFWork_saved = *pSFWorksaved;
#ifdef _WIN64
	if (*pSFWorkmkstr == 0xFF) {
		SFWork_mkstr_reg = pSFWorkmkstr[0x47];
		SFWork_mkstr_org = *(DWORD_PTR*)MKABS(pSFWorkmkstr+2);
		call.disp = MKDISP(rSFWork_mkstr, pSFWorkmkstr+6);
		WriteMemory(pSFWorkmkstr, &call, 6);
	} else {
		SFWork_mkstr_org = MKABS(pSFWorkmkstr+1);
		i = MKDISP(rSFWork_mkstr, pSFWorkmkstr+5);
		WriteMemory(pSFWorkmkstr+1, &i, 4);
	}
	if (*pSFWorkresize == 0xFF) {
		WriteCode(pSFWorkresize, "\x4C\x89\xC0"		// mov rax,r8
								 "\x0F\x1F\x00");	// nop
	} else {
		WriteCode(pSFWorkresize, "\x48\x89\xC8"		// mov rax,rcx
								 "\x66\x90");		// nop
	}
#else
	SFWork_passed = *pSFWorkpassed;
	SFWork_first = (cmdFileVersionMS >= 0x60002) ? 12 : 20;
	if (*pSFWorkmkstr == 0xE8) {
		SFWork_mkstr_org = MKABS(pSFWorkmkstr+1);
		if (pSFWorkmkstr[-5] == 0x68) {
			SFWork_stdcall = 1;
		}
		i = MKDISP(SFWork_mkstr, pSFWorkmkstr+5);
		WriteMemory(pSFWorkmkstr+1, &i, 4);
	} else {
		SFWork_mkstr_reg = pSFWorkmkstr[0x33];
		SFWork_mkstr_org = **(LPDWORD*)(pSFWorkmkstr+2);
		call.disp = MKDISP(SFWork_mkstr, pSFWorkmkstr+6);
		WriteMemory(pSFWorkmkstr, &call, 6);
	}
	if (*pSFWorkresize == 0xE8) {
		if (SFWork_stdcall) {
			WriteCode(pSFWorkresize, "\x58"				// pop eax
									 "\x59"				// pop ecx
									 "\x66\x66\x90");	// nop
		} else {
			WriteCode(pSFWorkresize, "\x89\xC8" 		// mov eax,ecx
									 "\x66\x66\x90");	// nop
		}
	} else {
		WriteCode(pSFWorkresize, "\x59"			// pop ecx
								 "\x59"			// pop ecx
								 "\x58"			// pop eax
								 "\x59"			// pop ecx
								 "\x66\x90");	// nop
	}
#endif

	// Patch FOR to fix a bug with wildcard expansion: each name accumulates,
	// resizing bigger and bigger (this patch is not undone on unload).
	// I've made the initial size big enough, no need to resize.
	WriteByte(pForResize, 0xEB);				// jmp
#ifdef _WIN64
	if (cmdFileVersionMS == 0x50002) {
		// 5.2.*.*
		WriteCode(pForResize, "\x40\xE9");		// jmp with dummy REX prefix
		WriteCode(pForMkstr,
				  "\x0F\x1F\x00"						// nop
				  "\x44\x8D\xA1\x00\x01\x00\x00");		// lea r12d,rcx+256
	} else if (cmdFileVersionMS == 0x60000 ||
			   cmdFileVersionMS == 0x60001) {
		// 6.0.*.*
		// 6.1.*.*
		WriteCode(pForMkstr,
				  "\x0F\x1F\x00"						// nop
				  "\x44\x8D\xA9\x00\x01\x00\x00");		// lea r13d,rcx+256
	} else if (cmdFileVersionMS == 0x60002) {
		if (cmdFileVersionLS == 0x1FA60000) {
			// 6.2.8102.0
			WriteCode(pForMkstr,
					  "\x90" 							// nop
					  "\x49\xFF\xC7"					// inc r15
					  "\x66\x42\x83\x3C\x79\x00"		// cmp word[rcx+r15*2],0
					  "\x75\xF5"						// jnz inc
					  "\x41\x81\xC7\x00\x01\x00\x00");	// add r15d,256
		} else {
			// 6.2.9200.16384
			WriteCode(pForMkstr,
					  "\x90" 							// nop
					  "\x48\xFF\xC5"					// inc rbp
					  "\x66\x83\x3C\x69\x00" 			// cmp word[rcx+rbp*2],0
					  "\x75\xF6"						// jnz inc
					  "\x81\xC5\x00\x01\x00\x00");		// add ebp,256
		}
	} else if (/*cmdFileVersionMS == 0x60003 &&
			   cmdFileVersionLS == 0x24D70000 &&*/
			   cmdDebug) {
		// 6.3.9431.0u
		WriteCode(pForMkstr, "\x90"						// nop
							 "\x48\xFF\xC2"				// inc rdx
							 "\x66\x44\x39\x24\x51"		// cmp [rcx+rdx*2],r12w
							 "\x75\xF6"					// jnz inc
							 "\xFE\xC6"					// inc dh
							 "\x89\xD7");				// mov edi,edx
	} else if (cmdFileVersionMS == 0xA0000 &&
			   HIWORD(cmdFileVersionLS) >= 17763) {
		// 10.0.17763.1
		// 10.0.18362.1
		WriteCode(pForMkstr, "\x90" 					// nop
							 "\x48\xFF\xC2" 			// inc rdx
							 "\x66\x44\x39\x34\x51"		// cmp [rcx+rdx*2],r14w
							 "\x75\xF6" 				// jnz inc
							 "\xFE\xC6" 				// inc dh
							 "\x89\xD7");				// mov edi,edx
	} else {
		// 6.3.*.*
		// 10.0.*.*
		WriteCode(pForMkstr, "\x90"						// nop
							 "\x48\xFF\xC2"				// inc rdx
							 "\x66\x39\x2C\x51"			// cmp [rcx+rdx*2],bp
							 "\x75\xF7" 				// jnz inc
							 "\xFE\xC6" 				// inc dh
							 "\x89\xD7");				// mov edi,edx
	}
#else
	if (cmdFileVersionMS == 0x50000) {
		// 5.0.*.*
		WriteCode(pForMkstr, "\xEB\x00" 				// jmp inc
							 "\xFE\xC4");				// inc ah
	} else if (HIWORD(cmdFileVersionMS) == 5) {
		if (LOWORD(cmdFileVersionLS) == 0) {
			// 5.1.2600.0
			// 5.2.3790.0
			WriteMemory(pForMkstr, pForMkstr+2, 8);
			WriteCode(pForMkstr+8, "\xFE\xC4");			// inc ah
		} else {
			// 5.1.*.*
			// 5.2.*.*
			WriteMemory(pForMkstr, pForMkstr+6, 8);
			WriteCode(pForMkstr+8, "\x81\xC0\x00\x01\x00\x00");  // add eax,256
		}
	} else if (cmdFileVersionMS == 0x60000) {
		// 6.0.*.*
		WriteCode(pForMkstr, "\xFE\xC4" 				// inc ah
							 "\x93"); 					// xchg ebx,eax
	} else if (cmdFileVersionMS == 0x60001) {
		// 6.1.*.*
		WriteCode(pForMkstr, "\xFE\xC4" 				// inc ah
							 "\x97"); 					// xchg edi,eax
	} else if (cmdFileVersionMS == 0x60002) {
		// 6.2.*.*
		WriteCode(pForMkstr, "\x85\xC0" 				// test eax,eax
							 "\x75\xF6" 				// jnz $-8
							 "\x2B\xD1" 				// sub edx,ecx
							 "\xD1\xFA" 				// sar edx,1
							 "\xFE\xC6" 				// inc dh
							 "\x89\xD3");				// mov ebx,edx
	} else {
		// 6.3.*.*
		// 10.0.*.*
		WriteCode(pForMkstr, "\xFE\xC6" 				// inc dh
							 "\x92"); 					// xchg edx,eax
	}
#endif

	// Hook PutStdErr to write the batch file name and line number.
	pPutMsg = (LPVOID)MKABS(pPutStdErrMsg);
#ifdef _WIN64
	i = MKDISP(rMyPutStdErrMsg, pPutStdErrMsg+1);
#else
	if (cmdFileVersionMS > 0x60002) {
		i = (DWORD)fastPutStdErrMsg;
	} else if (cmdFileVersionMS == 0x60002) {
		i = (DWORD)fastPutStdErrMsg62;
	} else {
		i = (DWORD)stdPutStdErrMsg;
	}
	i -= (DWORD)pPutStdErrMsg+4;
#endif
	WriteMemory(pPutStdErrMsg, &i, 4);

	// Hook Lex text type to process Unicode characters.
#ifdef _WIN64
	call.disp = MKDISP(rMyLexText, pLexText+5);
	if (cmdDebug) {
		// Currently only the one debug version.
		/*if (cmdFileVersionMS = 0x60003 &&
			cmdFileVersionLS == 0x24d70000) */
		call.disp -= 5;
	}
#else
	if (cmdDebug) {
		// Currently only the one debug version.
		/*if (cmdFileVersionMS = 0x60003 &&
			cmdFileVersionLS == 0x24d70000) */
		call.disp = (DWORD)MyLexTextESI;
	} else {
		call.disp = (DWORD)MyLexText;
	}
	call.disp -= (DWORD)pLexText+5;
#endif
	WriteMemory(pLexText, &call.op, 5);

	// Hook FOR /F to maintain a line number.
	ForFend_org = MKABS(pForFend+2);
#ifdef _WIN64
	// No need for original begin, it can never be true.
	call.disp = MKDISP(rForFbegin, pForFbegin+6);
	WriteMemory(pForFbegin, &call, 6);
	call.disp = MKDISP(rForFend, pForFend+6);
	WriteMemory(pForFend, &call, 6);
#else
	if (pForFbegin[1] == 0x83) {
		// No need for original begin, it can never be true.
		call.disp = MKDISP(ForFbegin_hook, pForFbegin+6);
		WriteMemory(pForFbegin, &call, 6);
		call.disp = (DWORD)ForFend;
	} else {
		// No need to return, it can never be false.
		ForFbegin_org = MKABS(pForFbegin+2);
		i = MKDISP(ForFbegin_jmp, pForFbegin+6);
		WriteMemory(pForFbegin+2, &i, 4);
		call.disp = (DWORD)ForFend_opp;
	}
	call.disp -= (DWORD)pForFend+6;
	WriteMemory(pForFend, &call, 6);
#endif

	// Hook FOR to allow shorthand for infinite & range loops.
#ifdef _WIN64
	*rParseFor_org = MKABS(pParseFortoken);
	i = MKDISP(rParseFor, pParseFortoken+1);
#else
	ParseFor_org = MKABS(pParseFortoken);
	i = MKDISP(ParseFor_hook, pParseFortoken+1);
#endif
	WriteMemory(pParseFortoken, &i, 4);

	// Hook FOR /F to use "line" as shorthand for "delims= eol=".
#ifdef _WIN64
	*rParseForF_org = MKABS(pForFoptions);
	i = MKDISP(rParseForF, pForFoptions+1);
#else
	ParseForF_org = MKABS(pForFoptions);
	i = MKDISP(ParseForF_hook, pForFoptions+1);
#endif
	WriteMemory(pForFoptions, &i, 4);
}

void hookEchoOptions(BOOL options)
{
	if (!options) {
		// Swap START & ECHO's help tests, so ECHO has no help and START only
		// looks at its first argument (now done for all commands).
		WriteByte(pStartHelp, 9);
		//WriteByte(pEchoHelp, 31);
		// Patch ECHO to always echo, ignoring options.
#ifdef _WIN64
		if (cmdFileVersionMS == 0x50002) {
			// 5.2.*.*
			WriteCode(pEchoOnOff, "\x6A\x03"			// push 3
								  "\x59");				// pop rcx
		} else if (cmdFileVersionMS == 0x60002) {
			// 6.2.*.*
			WriteCode(pEchoOnOff, "\x31\xC9"			// xor ecx,ecx
								  "\x83\xC9\x01");		// or ecx,1
		} else {
			// 6.0.*.*
			// 6.1.*.*
			// 6.3.*.*
			// 10.*.*.*
			WriteCode(pEchoOnOff, "\xB8\x03\x00\x00\x00");	// mov eax,3
		}
#else
		if (cmdFileVersionMS < 0x60002)  {
			// 5.*.*.*
			// 6.0.*.*
			// 6.1.*.*
			WriteCode(pEchoOnOff, "\x58" 				// pop eax
								  "\x58" 				// pop eax
								  "\x6A\x03"			// push 3
								  "\x58");				// pop eax
		} else if (cmdFileVersionMS == 0x60002) {
			if (cmdFileVersionLS == 0x1FA60000) {
				// 6.2.8102.0
				WriteCode(pEchoOnOff, "\x90" 			// nop
									  "\x58" 			// pop eax
									  "\x6A\x03"		// push 3
									  "\x58");			// pop eax
			} else {
				// 6.2.9200.16384
				WriteCode(pEchoOnOff, "\x33\xC0"		// xor eax,eax
									  "\x83\xC8\x01");	// or eax,1
			}
		} else {
			// 6.3.*.*
			// 10.*.*.*
			WriteCode(pEchoOnOff, "\xB8\x03\x00\x00\x00");	// mov eax,3
		}
#endif
	} else {
		WriteByte(pStartHelp, 31);
		//WriteByte(pEchoHelp, 9);
		WriteMemory(pEchoOnOff, oldEchoOnOff, 5);
	}
}

void hookCtrlCAborts(char aborts)
{
	if (aborts == -1) {
		WriteMemory(pCtrlCAborts, oldCtrlCAborts, sizeof(oldCtrlCAborts));
	} else {
#ifdef _WIN64
		char code[7];
		code[0] = 0xE8; 		// call
		*(int *)(code+1) = MKDISP(rPromptUser, pCtrlCAborts+5);
		code[5] = 0xC3; 		// ret
		code[6] = 0x90; 		// nop
		*rAbortFlag = aborts;
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
	kh_destroy(ptrdw, sfwork_map);

	WriteMemory(peCall, &eCall, sizeof(eCall));
	WriteMemory(peEcho, &eEcho, sizeof(eEcho));
	WriteMemory(pPutStdErrMsg, &oldPutMsg, 4);
	WriteMemory(pLexText, oldLexText, 5);
	WriteMemory(pEchoOnOff, oldEchoOnOff, 5);
	WriteByte(pStartHelp, 31);
	//WriteByte(pEchoHelp, 9);
	if (*pEchoHelp == 0x90) {
		WriteMemory(pEchoHelp, "\x0F\x84", 2);
	} else {
		WriteByte(pEchoHelp, 0x74);
	}
	WriteMemory(pCtrlCAborts, oldCtrlCAborts, sizeof(oldCtrlCAborts));
	WriteMemory(pSFWorkmkstr, oldSFWorkmkstr, 6);
	WriteMemory(pSFWorkresize, oldSFWorkresize, 6);
	WriteMemory(pForFbegin, oldForFbegin, 6);
	WriteMemory(pForFend, oldForFend, 6);
	WriteMemory(pParseFortoken, &oldParseFor, 4);
	WriteMemory(pForFoptions, &oldParseForF, 4);

	*pfDumpTokens = 0;
	*pfDumpParse = 0;

#ifdef _WIN64
	VirtualFree(redirect, 0, MEM_RELEASE);
#endif
}
