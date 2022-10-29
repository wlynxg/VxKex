///////////////////////////////////////////////////////////////////////////////
//
// Module Name:
//
//     kexhk.c
//
// Abstract:
//
//     Routines for hooking code.
//
//     Intended for usage only during early process initialization, e.g. to
//     hook Nt* functions permanently, or temporarily hook functions while
//     running in a strictly single-threaded environment.
//
//     The only situation where you can safely keep Basic hooks active when
//     multiple threads are running is when you never unhook or hook anything
//     else. In other terms, only if you have a complete re-implementation of
//     the function you are hooking (which is easy for Nt* syscall stubs).
//
//     In the future, a simple disassembler should be added so that these
//     hooks can remain active throughout the entire life of the process, once
//     initialized.
//
// Author:
//
//     vxiiduu (23-Oct-2022)
//
// Environment:
//
//   Early process creation ONLY. For reasons of simplicity these functions
//   are not thread safe at all.
//
// Revision History:
//
//     vxiiduu              23-Oct-2022  Initial creation.
//
///////////////////////////////////////////////////////////////////////////////

#include "buildcfg.h"
#include "kexdllp.h"

STATIC BYTE BasicHookTemplate[BASIC_HOOK_LENGTH] = {
#ifdef KEX_ARCH_X64
	0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,				// JMP [FuncPtr]
	0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC	// FuncPtr: DQ 0xCCCCCCCCCCCCCCCC
#else
	0x68, 0xCC, 0xCC, 0xCC, 0xCC,					// PUSH 0xCCCCCCCC
	0xC3											// RET
#endif
};

//
// Install a hook on a given API routine.
//
//   ApiAddress
//     Address of a function that you want to hook.
//
//   RedirectedAddress
//     Address of your function with an identical call signature that you
//     want to get called instead of the hooked function.
//
//   HookContext
//     Optional parameter that allows you to undo the hook later, e.g. if
//     you want to uninstall the hook or if you want to call the original API.
//     If this parameter is NULL, the hook is permanent.
//
NTSTATUS NTAPI KexHkInstallBasicHook(
	IN		PVOID					ApiAddress,
	IN		PVOID					RedirectedAddress,
	OUT		PKEX_BASIC_HOOK_CONTEXT	HookContext OPTIONAL)
{
	NTSTATUS Status;
	PVOID ApiPageAddress;
	SIZE_T HookLength;
	ULONG OldProtect;

	if (!ApiAddress) {
		return STATUS_INVALID_PARAMETER_1;
	}
	
	if (!RedirectedAddress) {
		return STATUS_INVALID_PARAMETER_2;
	}

	if (!HookContext) {
		return STATUS_INVALID_PARAMETER_3;
	}

	ApiPageAddress = ApiAddress;
	HookLength = sizeof(BasicHookTemplate);
	
	//
	// Fill out hook context fields, copy the original bytes (so
	// we can undo the hook later), and prepare the hook template for
	// writing.
	//

	if (HookContext) {
		HookContext->OriginalApiAddress = ApiAddress;
		
		RtlCopyMemory(
			HookContext->OriginalInstructions,
			ApiAddress,
			sizeof(BasicHookTemplate));
	}

	*(PPVOID) &BasicHookTemplate[BASIC_HOOK_DESTINATION_OFFSET] = RedirectedAddress;
	
	//
	// Let us write to the address of the hooked API.
	//

	Status = NtProtectVirtualMemory(
		NtCurrentProcess(),
		&ApiPageAddress,
		&HookLength,
		PAGE_READWRITE,
		&OldProtect);

	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	//
	// Hook the function and restore old page protections.
	//

	RtlCopyMemory(ApiAddress, BasicHookTemplate, sizeof(BasicHookTemplate));

	NtProtectVirtualMemory(
		NtCurrentProcess(),
		&ApiPageAddress,
		&HookLength,
		OldProtect,
		&OldProtect);

	return STATUS_SUCCESS;
}

NTSTATUS NTAPI KexHkRemoveBasicHook(
	IN		PKEX_BASIC_HOOK_CONTEXT	HookContext)
{
	NTSTATUS Status;
	PVOID ApiPageAddress;
	SIZE_T HookLength;
	ULONG OldProtect;

	if (!HookContext) {
		return STATUS_INVALID_PARAMETER_1;
	}

	ApiPageAddress = HookContext->OriginalApiAddress;
	HookLength = sizeof(BasicHookTemplate);

	Status = NtProtectVirtualMemory(
		NtCurrentProcess(),
		&ApiPageAddress,
		&HookLength,
		PAGE_READWRITE,
		&OldProtect);

	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	RtlCopyMemory(
		HookContext->OriginalApiAddress,
		HookContext->OriginalInstructions,
		sizeof(BasicHookTemplate));

	NtProtectVirtualMemory(
		NtCurrentProcess(),
		&ApiPageAddress,
		&HookLength,
		OldProtect,
		&OldProtect);

	return STATUS_SUCCESS;
}