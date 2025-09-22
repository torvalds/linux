//===-- interception_win_test.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
// Tests for interception_win.h.
//
//===----------------------------------------------------------------------===//
#include "interception/interception.h"

#include "gtest/gtest.h"

// Too slow for debug build
// Disabling for ARM64 since testcases are x86/x64 assembly.
#if !SANITIZER_DEBUG
#if SANITIZER_WINDOWS
#    if !SANITIZER_WINDOWS_ARM64

#      include <stdarg.h>

#      define WIN32_LEAN_AND_MEAN
#      include <windows.h>

namespace __interception {
namespace {

enum FunctionPrefixKind {
  FunctionPrefixNone,
  FunctionPrefixPadding,
  FunctionPrefixHotPatch,
  FunctionPrefixDetour,
};

typedef bool (*TestOverrideFunction)(uptr, uptr, uptr*);
typedef int (*IdentityFunction)(int);

#if SANITIZER_WINDOWS64

const u8 kIdentityCodeWithPrologue[] = {
    0x55,                   // push        rbp
    0x48, 0x89, 0xE5,       // mov         rbp,rsp
    0x8B, 0xC1,             // mov         eax,ecx
    0x5D,                   // pop         rbp
    0xC3,                   // ret
};

const u8 kIdentityCodeWithPushPop[] = {
    0x55,                   // push        rbp
    0x48, 0x89, 0xE5,       // mov         rbp,rsp
    0x53,                   // push        rbx
    0x50,                   // push        rax
    0x58,                   // pop         rax
    0x8B, 0xC1,             // mov         rax,rcx
    0x5B,                   // pop         rbx
    0x5D,                   // pop         rbp
    0xC3,                   // ret
};

const u8 kIdentityTwiceOffset = 16;
const u8 kIdentityTwice[] = {
    0x55,                   // push        rbp
    0x48, 0x89, 0xE5,       // mov         rbp,rsp
    0x8B, 0xC1,             // mov         eax,ecx
    0x5D,                   // pop         rbp
    0xC3,                   // ret
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x55,                   // push        rbp
    0x48, 0x89, 0xE5,       // mov         rbp,rsp
    0x8B, 0xC1,             // mov         eax,ecx
    0x5D,                   // pop         rbp
    0xC3,                   // ret
};

const u8 kIdentityCodeWithMov[] = {
    0x89, 0xC8,             // mov         eax, ecx
    0xC3,                   // ret
};

const u8 kIdentityCodeWithJump[] = {
    0xE9, 0x04, 0x00, 0x00,
    0x00,                   // jmp + 4
    0xCC, 0xCC, 0xCC, 0xCC,
    0x89, 0xC8,             // mov         eax, ecx
    0xC3,                   // ret
};

const u8 kIdentityCodeWithJumpBackwards[] = {
    0x89, 0xC8,  // mov         eax, ecx
    0xC3,        // ret
    0xE9, 0xF8, 0xFF, 0xFF,
    0xFF,  // jmp - 8
    0xCC, 0xCC, 0xCC, 0xCC,
};
const u8 kIdentityCodeWithJumpBackwardsOffset = 3;

#    else

const u8 kIdentityCodeWithPrologue[] = {
    0x55,                   // push        ebp
    0x8B, 0xEC,             // mov         ebp,esp
    0x8B, 0x45, 0x08,       // mov         eax,dword ptr [ebp + 8]
    0x5D,                   // pop         ebp
    0xC3,                   // ret
};

const u8 kIdentityCodeWithPushPop[] = {
    0x55,                   // push        ebp
    0x8B, 0xEC,             // mov         ebp,esp
    0x53,                   // push        ebx
    0x50,                   // push        eax
    0x58,                   // pop         eax
    0x8B, 0x45, 0x08,       // mov         eax,dword ptr [ebp + 8]
    0x5B,                   // pop         ebx
    0x5D,                   // pop         ebp
    0xC3,                   // ret
};

const u8 kIdentityTwiceOffset = 8;
const u8 kIdentityTwice[] = {
    0x55,                   // push        ebp
    0x8B, 0xEC,             // mov         ebp,esp
    0x8B, 0x45, 0x08,       // mov         eax,dword ptr [ebp + 8]
    0x5D,                   // pop         ebp
    0xC3,                   // ret
    0x55,                   // push        ebp
    0x8B, 0xEC,             // mov         ebp,esp
    0x8B, 0x45, 0x08,       // mov         eax,dword ptr [ebp + 8]
    0x5D,                   // pop         ebp
    0xC3,                   // ret
};

const u8 kIdentityCodeWithMov[] = {
    0x8B, 0x44, 0x24, 0x04, // mov         eax,dword ptr [esp + 4]
    0xC3,                   // ret
};

const u8 kIdentityCodeWithJump[] = {
    0xE9, 0x04, 0x00, 0x00,
    0x00,                   // jmp + 4
    0xCC, 0xCC, 0xCC, 0xCC,
    0x8B, 0x44, 0x24, 0x04, // mov         eax,dword ptr [esp + 4]
    0xC3,                   // ret
};

const u8 kIdentityCodeWithJumpBackwards[] = {
    0x8B, 0x44, 0x24, 0x04,  // mov         eax,dword ptr [esp + 4]
    0xC3,                    // ret
    0xE9, 0xF6, 0xFF, 0xFF,
    0xFF,  // jmp - 10
    0xCC, 0xCC, 0xCC, 0xCC,
};
const u8 kIdentityCodeWithJumpBackwardsOffset = 5;

#    endif

const u8 kPatchableCode1[] = {
    0xB8, 0x4B, 0x00, 0x00, 0x00,   // mov eax,4B
    0x33, 0xC9,                     // xor ecx,ecx
    0xC3,                           // ret
};

const u8 kPatchableCode2[] = {
    0x55,                           // push ebp
    0x8B, 0xEC,                     // mov ebp,esp
    0x33, 0xC0,                     // xor eax,eax
    0x5D,                           // pop ebp
    0xC3,                           // ret
};

const u8 kPatchableCode3[] = {
    0x55,                           // push ebp
    0x8B, 0xEC,                     // mov ebp,esp
    0x6A, 0x00,                     // push 0
    0xE8, 0x3D, 0xFF, 0xFF, 0xFF,   // call <func>
};

const u8 kPatchableCode4[] = {
    0xE9, 0xCC, 0xCC, 0xCC, 0xCC,   // jmp <label>
    0x90, 0x90, 0x90, 0x90,
};

const u8 kPatchableCode5[] = {
    0x55,                                      // push    ebp
    0x8b, 0xec,                                // mov     ebp,esp
    0x8d, 0xa4, 0x24, 0x30, 0xfd, 0xff, 0xff,  // lea     esp,[esp-2D0h]
    0x54,                                      // push    esp
};

#if SANITIZER_WINDOWS64
u8 kLoadGlobalCode[] = {
  0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, // mov    eax [rip + global]
  0xC3,                               // ret
};
#endif

const u8 kUnpatchableCode1[] = {
    0xC3,                           // ret
};

const u8 kUnpatchableCode2[] = {
    0x33, 0xC9,                     // xor ecx,ecx
    0xC3,                           // ret
};

const u8 kUnpatchableCode3[] = {
    0x75, 0xCC,                     // jne <label>
    0x33, 0xC9,                     // xor ecx,ecx
    0xC3,                           // ret
};

const u8 kUnpatchableCode4[] = {
    0x74, 0xCC,                     // jne <label>
    0x33, 0xC9,                     // xor ecx,ecx
    0xC3,                           // ret
};

const u8 kUnpatchableCode5[] = {
    0xEB, 0x02,                     // jmp <label>
    0x33, 0xC9,                     // xor ecx,ecx
    0xC3,                           // ret
};

const u8 kUnpatchableCode6[] = {
    0xE8, 0xCC, 0xCC, 0xCC, 0xCC,   // call <func>
    0x90, 0x90, 0x90, 0x90,
};

const u8 kUnpatchableCode7[] = {
    0x33, 0xc0,                     // xor     eax,eax
    0x48, 0x85, 0xd2,               // test    rdx,rdx
    0x74, 0x10,                     // je      +16  (unpatchable)
};

const u8 kUnpatchableCode8[] = {
    0x48, 0x8b, 0xc1,               // mov     rax,rcx
    0x0f, 0xb7, 0x10,               // movzx   edx,word ptr [rax]
    0x48, 0x83, 0xc0, 0x02,         // add     rax,2
    0x66, 0x85, 0xd2,               // test    dx,dx
    0x75, 0xf4,                     // jne     -12  (unpatchable)
};

const u8 kUnpatchableCode9[] = {
    0x4c, 0x8b, 0xc1,               // mov     r8,rcx
    0x8a, 0x01,                     // mov     al,byte ptr [rcx]
    0x48, 0xff, 0xc1,               // inc     rcx
    0x84, 0xc0,                     // test    al,al
    0x75, 0xf7,                     // jne     -9  (unpatchable)
};

const u8 kPatchableCode6[] = {
    0x48, 0x89, 0x54, 0x24, 0xBB, // mov QWORD PTR [rsp + 0xBB], rdx
    0x33, 0xC9,                   // xor ecx,ecx
    0xC3,                         // ret
};

const u8 kPatchableCode7[] = {
    0x4c, 0x89, 0x4c, 0x24, 0xBB,  // mov QWORD PTR [rsp + 0xBB], r9
    0x33, 0xC9,                   // xor ecx,ecx
    0xC3,                         // ret
};

const u8 kPatchableCode8[] = {
    0x4c, 0x89, 0x44, 0x24, 0xBB, // mov QWORD PTR [rsp + 0xBB], r8
    0x33, 0xC9,                   // xor ecx,ecx
    0xC3,                         // ret
};

const u8 kPatchableCode9[] = {
    0x8a, 0x01,                     // al,byte ptr [rcx]
    0x45, 0x33, 0xc0,               // xor     r8d,r8d
    0x84, 0xc0,                     // test    al,al
};

const u8 kPatchableCode10[] = {
    0x45, 0x33, 0xc0,               // xor     r8d,r8d
    0x41, 0x8b, 0xc0,               // mov     eax,r8d
    0x48, 0x85, 0xd2,               // test    rdx,rdx
};

const u8 kPatchableCode11[] = {
    0x48, 0x83, 0xec, 0x38,         // sub     rsp,38h
    0x83, 0x64, 0x24, 0x28, 0x00,   // and     dword ptr [rsp+28h],0
};

const u8 kPatchableCode12[] = {
    0x55,                           // push    ebp
    0x53,                           // push    ebx
    0x57,                           // push    edi
    0x56,                           // push    esi
    0x8b, 0x6c, 0x24, 0x18,         // mov     ebp,dword ptr[esp+18h]
};

const u8 kPatchableCode13[] = {
    0x55,                           // push    ebp
    0x53,                           // push    ebx
    0x57,                           // push    edi
    0x56,                           // push    esi
    0x8b, 0x5c, 0x24, 0x14,         // mov     ebx,dword ptr[esp+14h]
};

const u8 kPatchableCode14[] = {
    0x55,                           // push    ebp
    0x89, 0xe5,                     // mov     ebp,esp
    0x53,                           // push    ebx
    0x57,                           // push    edi
    0x56,                           // push    esi
};

const u8 kUnsupportedCode1[] = {
    0x0f, 0x0b,                     // ud2
    0x0f, 0x0b,                     // ud2
    0x0f, 0x0b,                     // ud2
    0x0f, 0x0b,                     // ud2
};

// A buffer holding the dynamically generated code under test.
u8* ActiveCode;
const size_t ActiveCodeLength = 4096;

int InterceptorFunction(int x);

/// Allocate code memory more than 2GB away from Base.
u8 *AllocateCode2GBAway(u8 *Base) {
  // Find a 64K aligned location after Base plus 2GB.
  size_t TwoGB = 0x80000000;
  size_t AllocGranularity = 0x10000;
  Base = (u8 *)((((uptr)Base + TwoGB + AllocGranularity)) & ~(AllocGranularity - 1));

  // Check if that location is free, and if not, loop over regions until we find
  // one that is.
  MEMORY_BASIC_INFORMATION mbi = {};
  while (sizeof(mbi) == VirtualQuery(Base, &mbi, sizeof(mbi))) {
    if (mbi.State & MEM_FREE) break;
    Base += mbi.RegionSize;
  }

  // Allocate one RWX page at the free location.
  return (u8 *)::VirtualAlloc(Base, ActiveCodeLength, MEM_COMMIT | MEM_RESERVE,
                              PAGE_EXECUTE_READWRITE);
}

template<class T>
static void LoadActiveCode(
    const T &code,
    uptr *entry_point,
    FunctionPrefixKind prefix_kind = FunctionPrefixNone) {
  if (ActiveCode == nullptr) {
    ActiveCode = AllocateCode2GBAway((u8*)&InterceptorFunction);
    ASSERT_NE(ActiveCode, nullptr) << "failed to allocate RWX memory 2GB away";
  }

  size_t position = 0;

  // Add padding to avoid memory violation when scanning the prefix.
  for (int i = 0; i < 16; ++i)
    ActiveCode[position++] = 0xC3;  // Instruction 'ret'.

  // Add function padding.
  size_t padding = 0;
  if (prefix_kind == FunctionPrefixPadding)
    padding = 16;
  else if (prefix_kind == FunctionPrefixDetour ||
           prefix_kind == FunctionPrefixHotPatch)
    padding = FIRST_32_SECOND_64(5, 6);
  // Insert |padding| instructions 'nop'.
  for (size_t i = 0; i < padding; ++i)
    ActiveCode[position++] = 0x90;

  // Keep track of the entry point.
  *entry_point = (uptr)&ActiveCode[position];

  // Add the detour instruction (i.e. mov edi, edi)
  if (prefix_kind == FunctionPrefixDetour) {
#if SANITIZER_WINDOWS64
    // Note that "mov edi,edi" is NOP in 32-bit only, in 64-bit it clears
    // higher bits of RDI.
    // Use 66,90H as NOP for Windows64.
    ActiveCode[position++] = 0x66;
    ActiveCode[position++] = 0x90;
#else
    // mov edi,edi.
    ActiveCode[position++] = 0x8B;
    ActiveCode[position++] = 0xFF;
#endif

  }

  // Copy the function body.
  for (size_t i = 0; i < sizeof(T); ++i)
    ActiveCode[position++] = code[i];
}

int InterceptorFunctionCalled;
IdentityFunction InterceptedRealFunction;

int InterceptorFunction(int x) {
  ++InterceptorFunctionCalled;
  return InterceptedRealFunction(x);
}

}  // namespace

// Tests for interception_win.h
TEST(Interception, InternalGetProcAddress) {
  HMODULE ntdll_handle = ::GetModuleHandle("ntdll");
  ASSERT_NE(nullptr, ntdll_handle);
  uptr DbgPrint_expected = (uptr)::GetProcAddress(ntdll_handle, "DbgPrint");
  uptr isdigit_expected = (uptr)::GetProcAddress(ntdll_handle, "isdigit");
  uptr DbgPrint_adddress = InternalGetProcAddress(ntdll_handle, "DbgPrint");
  uptr isdigit_address = InternalGetProcAddress(ntdll_handle, "isdigit");

  EXPECT_EQ(DbgPrint_expected, DbgPrint_adddress);
  EXPECT_EQ(isdigit_expected, isdigit_address);
  EXPECT_NE(DbgPrint_adddress, isdigit_address);
}

template <class T>
static void TestIdentityFunctionPatching(
    const T &code, TestOverrideFunction override,
    FunctionPrefixKind prefix_kind = FunctionPrefixNone,
    int function_start_offset = 0) {
  uptr identity_address;
  LoadActiveCode(code, &identity_address, prefix_kind);
  identity_address += function_start_offset;
  IdentityFunction identity = (IdentityFunction)identity_address;

  // Validate behavior before dynamic patching.
  InterceptorFunctionCalled = 0;
  EXPECT_EQ(0, identity(0));
  EXPECT_EQ(42, identity(42));
  EXPECT_EQ(0, InterceptorFunctionCalled);

  // Patch the function.
  uptr real_identity_address = 0;
  bool success = override(identity_address,
                         (uptr)&InterceptorFunction,
                         &real_identity_address);
  EXPECT_TRUE(success);
  EXPECT_NE(0U, real_identity_address);
  IdentityFunction real_identity = (IdentityFunction)real_identity_address;
  InterceptedRealFunction = real_identity;

  // Don't run tests if hooking failed or the real function is not valid.
  if (!success || !real_identity_address)
    return;

  // Calling the redirected function.
  InterceptorFunctionCalled = 0;
  EXPECT_EQ(0, identity(0));
  EXPECT_EQ(42, identity(42));
  EXPECT_EQ(2, InterceptorFunctionCalled);

  // Calling the real function.
  InterceptorFunctionCalled = 0;
  EXPECT_EQ(0, real_identity(0));
  EXPECT_EQ(42, real_identity(42));
  EXPECT_EQ(0, InterceptorFunctionCalled);

  TestOnlyReleaseTrampolineRegions();
}

#    if !SANITIZER_WINDOWS64
TEST(Interception, OverrideFunctionWithDetour) {
  TestOverrideFunction override = OverrideFunctionWithDetour;
  FunctionPrefixKind prefix = FunctionPrefixDetour;
  TestIdentityFunctionPatching(kIdentityCodeWithPrologue, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithPushPop, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithMov, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithJump, override, prefix);
}
#endif  // !SANITIZER_WINDOWS64

TEST(Interception, OverrideFunctionWithRedirectJump) {
  TestOverrideFunction override = OverrideFunctionWithRedirectJump;
  TestIdentityFunctionPatching(kIdentityCodeWithJump, override);
  TestIdentityFunctionPatching(kIdentityCodeWithJumpBackwards, override,
                               FunctionPrefixNone,
                               kIdentityCodeWithJumpBackwardsOffset);
}

TEST(Interception, OverrideFunctionWithHotPatch) {
  TestOverrideFunction override = OverrideFunctionWithHotPatch;
  FunctionPrefixKind prefix = FunctionPrefixHotPatch;
  TestIdentityFunctionPatching(kIdentityCodeWithMov, override, prefix);
}

TEST(Interception, OverrideFunctionWithTrampoline) {
  TestOverrideFunction override = OverrideFunctionWithTrampoline;
  FunctionPrefixKind prefix = FunctionPrefixNone;
  TestIdentityFunctionPatching(kIdentityCodeWithPrologue, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithPushPop, override, prefix);

  prefix = FunctionPrefixPadding;
  TestIdentityFunctionPatching(kIdentityCodeWithPrologue, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithPushPop, override, prefix);
}

TEST(Interception, OverrideFunction) {
  TestOverrideFunction override = OverrideFunction;
  FunctionPrefixKind prefix = FunctionPrefixNone;
  TestIdentityFunctionPatching(kIdentityCodeWithPrologue, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithPushPop, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithJump, override, prefix);

  prefix = FunctionPrefixPadding;
  TestIdentityFunctionPatching(kIdentityCodeWithPrologue, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithPushPop, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithMov, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithJump, override, prefix);

  prefix = FunctionPrefixHotPatch;
  TestIdentityFunctionPatching(kIdentityCodeWithPrologue, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithPushPop, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithMov, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithJump, override, prefix);

  prefix = FunctionPrefixDetour;
  TestIdentityFunctionPatching(kIdentityCodeWithPrologue, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithPushPop, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithMov, override, prefix);
  TestIdentityFunctionPatching(kIdentityCodeWithJump, override, prefix);
}

template<class T>
static void TestIdentityFunctionMultiplePatching(
    const T &code,
    TestOverrideFunction override,
    FunctionPrefixKind prefix_kind = FunctionPrefixNone) {
  uptr identity_address;
  LoadActiveCode(code, &identity_address, prefix_kind);

  // Patch the function.
  uptr real_identity_address = 0;
  bool success = override(identity_address,
                          (uptr)&InterceptorFunction,
                          &real_identity_address);
  EXPECT_TRUE(success);
  EXPECT_NE(0U, real_identity_address);

  // Re-patching the function should not work.
  success = override(identity_address,
                     (uptr)&InterceptorFunction,
                     &real_identity_address);
  EXPECT_FALSE(success);

  TestOnlyReleaseTrampolineRegions();
}

TEST(Interception, OverrideFunctionMultiplePatchingIsFailing) {
#if !SANITIZER_WINDOWS64
  TestIdentityFunctionMultiplePatching(kIdentityCodeWithPrologue,
                                       OverrideFunctionWithDetour,
                                       FunctionPrefixDetour);
#endif

  TestIdentityFunctionMultiplePatching(kIdentityCodeWithMov,
                                       OverrideFunctionWithHotPatch,
                                       FunctionPrefixHotPatch);

  TestIdentityFunctionMultiplePatching(kIdentityCodeWithPushPop,
                                       OverrideFunctionWithTrampoline,
                                       FunctionPrefixPadding);
}

TEST(Interception, OverrideFunctionTwice) {
  uptr identity_address1;
  LoadActiveCode(kIdentityTwice, &identity_address1);
  uptr identity_address2 = identity_address1 + kIdentityTwiceOffset;
  IdentityFunction identity1 = (IdentityFunction)identity_address1;
  IdentityFunction identity2 = (IdentityFunction)identity_address2;

  // Patch the two functions.
  uptr real_identity_address = 0;
  EXPECT_TRUE(OverrideFunction(identity_address1,
                               (uptr)&InterceptorFunction,
                               &real_identity_address));
  EXPECT_TRUE(OverrideFunction(identity_address2,
                               (uptr)&InterceptorFunction,
                               &real_identity_address));
  IdentityFunction real_identity = (IdentityFunction)real_identity_address;
  InterceptedRealFunction = real_identity;

  // Calling the redirected function.
  InterceptorFunctionCalled = 0;
  EXPECT_EQ(42, identity1(42));
  EXPECT_EQ(42, identity2(42));
  EXPECT_EQ(2, InterceptorFunctionCalled);

  TestOnlyReleaseTrampolineRegions();
}

template<class T>
static bool TestFunctionPatching(
    const T &code,
    TestOverrideFunction override,
    FunctionPrefixKind prefix_kind = FunctionPrefixNone) {
  uptr address;
  LoadActiveCode(code, &address, prefix_kind);
  uptr unused_real_address = 0;
  bool result = override(
      address, (uptr)&InterceptorFunction, &unused_real_address);

  TestOnlyReleaseTrampolineRegions();
  return result;
}

TEST(Interception, PatchableFunction) {
  TestOverrideFunction override = OverrideFunction;
  // Test without function padding.
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode1, override));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode2, override));
#if SANITIZER_WINDOWS64
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode3, override));
#else
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode3, override));
#endif
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode4, override));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode5, override));
#if SANITIZER_WINDOWS64
  EXPECT_TRUE(TestFunctionPatching(kLoadGlobalCode, override));
#endif

  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode1, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode2, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode3, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode4, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode5, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode6, override));
}

#if !SANITIZER_WINDOWS64
TEST(Interception, PatchableFunctionWithDetour) {
  TestOverrideFunction override = OverrideFunctionWithDetour;
  // Without the prefix, no function can be detoured.
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode1, override));
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode2, override));
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode3, override));
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode4, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode1, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode2, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode3, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode4, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode5, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode6, override));

  // With the prefix, all functions can be detoured.
  FunctionPrefixKind prefix = FunctionPrefixDetour;
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode1, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode2, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode3, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode4, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kUnpatchableCode1, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kUnpatchableCode2, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kUnpatchableCode3, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kUnpatchableCode4, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kUnpatchableCode5, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kUnpatchableCode6, override, prefix));
}
#endif  // !SANITIZER_WINDOWS64

TEST(Interception, PatchableFunctionWithRedirectJump) {
  TestOverrideFunction override = OverrideFunctionWithRedirectJump;
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode1, override));
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode2, override));
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode3, override));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode4, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode1, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode2, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode3, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode4, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode5, override));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode6, override));
}

TEST(Interception, PatchableFunctionWithHotPatch) {
  TestOverrideFunction override = OverrideFunctionWithHotPatch;
  FunctionPrefixKind prefix = FunctionPrefixHotPatch;

  EXPECT_TRUE(TestFunctionPatching(kPatchableCode1, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode2, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode3, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode4, override, prefix));
#if SANITIZER_WINDOWS64
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode6, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode7, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode8, override, prefix));
#endif
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode1, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kUnpatchableCode2, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode3, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode4, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode5, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode6, override, prefix));
}

TEST(Interception, PatchableFunctionWithTrampoline) {
  TestOverrideFunction override = OverrideFunctionWithTrampoline;
  FunctionPrefixKind prefix = FunctionPrefixPadding;

  EXPECT_TRUE(TestFunctionPatching(kPatchableCode1, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode2, override, prefix));
#if SANITIZER_WINDOWS64
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode3, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode9, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode10, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode11, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode7, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode8, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode9, override, prefix));
#else
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode3, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode12, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode13, override, prefix));
#endif
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode4, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode14, override, prefix));

  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode1, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode2, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode3, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode4, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode5, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode6, override, prefix));
}

TEST(Interception, UnsupportedInstructionWithTrampoline) {
  TestOverrideFunction override = OverrideFunctionWithTrampoline;
  FunctionPrefixKind prefix = FunctionPrefixPadding;

  static bool reportCalled;
  reportCalled = false;

  struct Local {
    static void Report(const char *format, ...) {
      if (reportCalled)
        FAIL() << "Report called more times than expected";
      reportCalled = true;
      ASSERT_STREQ(
          "interception_win: unhandled instruction at %p: %02x %02x %02x %02x "
          "%02x %02x %02x %02x\n",
          format);
      va_list args;
      va_start(args, format);
      u8 *ptr = va_arg(args, u8 *);
      for (int i = 0; i < 8; i++) EXPECT_EQ(kUnsupportedCode1[i], ptr[i]);
      int bytes[8];
      for (int i = 0; i < 8; i++) {
        bytes[i] = va_arg(args, int);
        EXPECT_EQ(kUnsupportedCode1[i], bytes[i]);
      }
      va_end(args);
    }
  };

  SetErrorReportCallback(Local::Report);
  EXPECT_FALSE(TestFunctionPatching(kUnsupportedCode1, override, prefix));
  SetErrorReportCallback(nullptr);

  if (!reportCalled)
    ADD_FAILURE() << "Report not called";
}

TEST(Interception, PatchableFunctionPadding) {
  TestOverrideFunction override = OverrideFunction;
  FunctionPrefixKind prefix = FunctionPrefixPadding;

  EXPECT_TRUE(TestFunctionPatching(kPatchableCode1, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode2, override, prefix));
#if SANITIZER_WINDOWS64
  EXPECT_FALSE(TestFunctionPatching(kPatchableCode3, override, prefix));
#else
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode3, override, prefix));
#endif
  EXPECT_TRUE(TestFunctionPatching(kPatchableCode4, override, prefix));

  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode1, override, prefix));
  EXPECT_TRUE(TestFunctionPatching(kUnpatchableCode2, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode3, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode4, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode5, override, prefix));
  EXPECT_FALSE(TestFunctionPatching(kUnpatchableCode6, override, prefix));
}

TEST(Interception, EmptyExportTable) {
  // We try to get a pointer to a function from an executable that doesn't
  // export any symbol (empty export table).
  uptr FunPtr = InternalGetProcAddress((void *)GetModuleHandleA(0), "example");
  EXPECT_EQ(0U, FunPtr);
}

}  // namespace __interception

#    endif  // !SANITIZER_WINDOWS_ARM64
#endif  // SANITIZER_WINDOWS
#endif  // #if !SANITIZER_DEBUG
