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

#define EBVERSION 0

#ifndef UNICODE
#define UNICODE
#endif

#define _WIN32_WINNT 0x0500
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define WSZ(len) ((len) * sizeof(WCHAR))	// byte size of a wide string
#define lenof(a) (sizeof(a) / sizeof(*a))	// elements in a static array

// Macro to convert a relative virtual address into a virtual address.
// Requires pDosHeader to point to the base.
#define MakeVA(cast, addValue) \
	(cast)((DWORD_PTR)pDosHeader + (DWORD_PTR)(addValue))

struct sCMD {
	DWORD verMS, verLS;
	const DWORD *offsets;
};

extern const struct sCMD cmd_versions[];

extern DWORD cmdFileVersionMS, cmdFileVersionLS, cmdDebug;
extern LPVOID cmd_end;

extern BOOL onWindowsTerminal;
extern HWND consoleHwnd;
extern HANDLE consoleOutput;

extern LPBYTE cmd_addrs[];

struct cmdnode {		// partial definition
	LPVOID stuff[14];
	LPWSTR cmd;
	LPWSTR arg;
};

typedef DWORD (WINAPI *fnCmdFunc)(struct cmdnode *node);

extern fnCmdFunc *peEcho, eEcho, *peCall, eCall;
extern LPWSTR Fmt17;
extern int *pLastRetCode;

DWORD WINAPI MyEcho(struct cmdnode *node);
DWORD WINAPI MyCall(struct cmdnode *node);
int MyPutStdErrMsg(UINT a, int b, UINT c, va_list *d);
UINT MyLexText(void);

#ifdef _WIN64
typedef int (*fnPutMsg)(UINT, int, UINT, va_list *);
extern fnPutMsg pPutMsg;
typedef LPWSTR (*fnMSCmdVar)(LPVOID, LPCWSTR, int *, LPCWSTR, LPWSTR *);
#else
typedef int (__stdcall *stdPutMsg)(UINT, int, UINT, va_list *);
typedef int (__fastcall *fastPutMsg)(UINT, int, UINT, va_list *);
typedef int (__fastcall *fastPutMsg62)(int, va_list *, UINT, UINT);
extern LPVOID pPutMsg;
typedef LPWSTR (__stdcall *stdMSCmdVar)(LPVOID, LPCWSTR, int *, LPCWSTR, LPWSTR *);
typedef LPWSTR (__fastcall *fastMSCmdVar)(LPVOID, LPCWSTR, int *, LPCWSTR, LPWSTR *);
typedef LPWSTR (__fastcall *fastMSCmdVar62)(LPCWSTR, LPVOID, LPCWSTR, int *, LPWSTR *);
#endif
extern int batchfile;

#ifdef _WIN64
#define ARCH L"amd64"
#else
#define ARCH L"x86"
#endif

#define STRINGBUFFERMAX 32768

#define FINDFOR_STACKSIZE 128
#define FORF_STACKSIZE 32		// should be way more than enough


void WriteMemory(LPVOID dst, LPVOID src, int size);
DWORD getBatchLine();
void hookCmd(void);
void unhookCmd(void);
void hookEchoOptions(BOOL on);
void hookCtrlCAborts(char aborts);

// The eol character is no longer modified, but still used to verify the version.
//#define peol				cmd_addrs[0]			// expected first!
#ifdef _WIN64
#define pMSCmdVar			((fnMSCmdVar) cmd_addrs[1])
#else
#define pMSCmdVar			cmd_addrs[1]
#endif
#define pLexText			cmd_addrs[2]
#define pTmpBuf 			((LPWSTR)cmd_addrs[3])
#define pPutStdErrMsg		((int*)cmd_addrs[4])
#define pCurrentBatchFile	((LPWSTR**)cmd_addrs[5])
#define pfOutputUnicode 	cmd_addrs[6]
#define pfDelayedExpansion	cmd_addrs[7]
#define pfEnableExtensions	cmd_addrs[8]
#define pfDumpTokens		cmd_addrs[9]
#define pfDumpParse 		cmd_addrs[10]
#define pForMkstr			cmd_addrs[11]	// storage for FOR wildcard matches
#define pForResize			cmd_addrs[12]	// prevent resizing it
#define pEchoOnOff			cmd_addrs[13]
#define pEchoFlag			cmd_addrs[14]
#define pStartHelp			cmd_addrs[15]
#define pEchoHelp			cmd_addrs[16]
#define pCtrlCAborts		cmd_addrs[17]
#define pFreeStack			cmd_addrs[18]
#define pDCount 			((LPDWORD)cmd_addrs[19])
#define pSFWorkmkstr		cmd_addrs[20]
#define pSFWorkresize		cmd_addrs[21]
#define pSFWorksaved		((char*)cmd_addrs[22])
#define pGotoFlag			cmd_addrs[23]
#define pForFbegin			cmd_addrs[24]
#define pForFend			cmd_addrs[25]

#ifdef _WIN64
#define OFFSETS 26
#else
#define pSFWorkpassed		((char*)cmd_addrs[26])
#define OFFSETS 27
#endif
