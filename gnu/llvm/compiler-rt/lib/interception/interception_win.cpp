//===-- interception_win.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Windows-specific interception methods.
//
// This file is implementing several hooking techniques to intercept calls
// to functions. The hooks are dynamically installed by modifying the assembly
// code.
//
// The hooking techniques are making assumptions on the way the code is
// generated and are safe under these assumptions.
//
// On 64-bit architecture, there is no direct 64-bit jump instruction. To allow
// arbitrary branching on the whole memory space, the notion of trampoline
// region is used. A trampoline region is a memory space withing 2G boundary
// where it is safe to add custom assembly code to build 64-bit jumps.
//
// Hooking techniques
// ==================
//
// 1) Detour
//
//    The Detour hooking technique is assuming the presence of an header with
//    padding and an overridable 2-bytes nop instruction (mov edi, edi). The
//    nop instruction can safely be replaced by a 2-bytes jump without any need
//    to save the instruction. A jump to the target is encoded in the function
//    header and the nop instruction is replaced by a short jump to the header.
//
//        head:  5 x nop                 head:  jmp <hook>
//        func:  mov edi, edi    -->     func:  jmp short <head>
//               [...]                   real:  [...]
//
//    This technique is only implemented on 32-bit architecture.
//    Most of the time, Windows API are hookable with the detour technique.
//
// 2) Redirect Jump
//
//    The redirect jump is applicable when the first instruction is a direct
//    jump. The instruction is replaced by jump to the hook.
//
//        func:  jmp <label>     -->     func:  jmp <hook>
//
//    On an 64-bit architecture, a trampoline is inserted.
//
//        func:  jmp <label>     -->     func:  jmp <tramp>
//                                              [...]
//
//                                   [trampoline]
//                                      tramp:  jmp QWORD [addr]
//                                       addr:  .bytes <hook>
//
//    Note: <real> is equivalent to <label>.
//
// 3) HotPatch
//
//    The HotPatch hooking is assuming the presence of an header with padding
//    and a first instruction with at least 2-bytes.
//
//    The reason to enforce the 2-bytes limitation is to provide the minimal
//    space to encode a short jump. HotPatch technique is only rewriting one
//    instruction to avoid breaking a sequence of instructions containing a
//    branching target.
//
//    Assumptions are enforced by MSVC compiler by using the /HOTPATCH flag.
//      see: https://msdn.microsoft.com/en-us/library/ms173507.aspx
//    Default padding length is 5 bytes in 32-bits and 6 bytes in 64-bits.
//
//        head:   5 x nop                head:  jmp <hook>
//        func:   <instr>        -->     func:  jmp short <head>
//                [...]                  body:  [...]
//
//                                   [trampoline]
//                                       real:  <instr>
//                                              jmp <body>
//
//    On an 64-bit architecture:
//
//        head:   6 x nop                head:  jmp QWORD [addr1]
//        func:   <instr>        -->     func:  jmp short <head>
//                [...]                  body:  [...]
//
//                                   [trampoline]
//                                      addr1:  .bytes <hook>
//                                       real:  <instr>
//                                              jmp QWORD [addr2]
//                                      addr2:  .bytes <body>
//
// 4) Trampoline
//
//    The Trampoline hooking technique is the most aggressive one. It is
//    assuming that there is a sequence of instructions that can be safely
//    replaced by a jump (enough room and no incoming branches).
//
//    Unfortunately, these assumptions can't be safely presumed and code may
//    be broken after hooking.
//
//        func:   <instr>        -->     func:  jmp <hook>
//                <instr>
//                [...]                  body:  [...]
//
//                                   [trampoline]
//                                       real:  <instr>
//                                              <instr>
//                                              jmp <body>
//
//    On an 64-bit architecture:
//
//        func:   <instr>        -->     func:  jmp QWORD [addr1]
//                <instr>
//                [...]                  body:  [...]
//
//                                   [trampoline]
//                                      addr1:  .bytes <hook>
//                                       real:  <instr>
//                                              <instr>
//                                              jmp QWORD [addr2]
//                                      addr2:  .bytes <body>
//===----------------------------------------------------------------------===//

#include "interception.h"

#if SANITIZER_WINDOWS
#include "sanitizer_common/sanitizer_platform.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace __interception {

static const int kAddressLength = FIRST_32_SECOND_64(4, 8);
static const int kJumpInstructionLength = 5;
static const int kShortJumpInstructionLength = 2;
UNUSED static const int kIndirectJumpInstructionLength = 6;
static const int kBranchLength =
    FIRST_32_SECOND_64(kJumpInstructionLength, kIndirectJumpInstructionLength);
static const int kDirectBranchLength = kBranchLength + kAddressLength;

#  if defined(_MSC_VER)
#    define INTERCEPTION_FORMAT(f, a)
#  else
#    define INTERCEPTION_FORMAT(f, a) __attribute__((format(printf, f, a)))
#  endif

static void (*ErrorReportCallback)(const char *format, ...)
    INTERCEPTION_FORMAT(1, 2);

void SetErrorReportCallback(void (*callback)(const char *format, ...)) {
  ErrorReportCallback = callback;
}

#  define ReportError(...)                \
    do {                                  \
      if (ErrorReportCallback)            \
        ErrorReportCallback(__VA_ARGS__); \
    } while (0)

static void InterceptionFailed() {
  ReportError("interception_win: failed due to an unrecoverable error.\n");
  // This acts like an abort when no debugger is attached. According to an old
  // comment, calling abort() leads to an infinite recursion in CheckFailed.
  __debugbreak();
}

static bool DistanceIsWithin2Gig(uptr from, uptr target) {
#if SANITIZER_WINDOWS64
  if (from < target)
    return target - from <= (uptr)0x7FFFFFFFU;
  else
    return from - target <= (uptr)0x80000000U;
#else
  // In a 32-bit address space, the address calculation will wrap, so this check
  // is unnecessary.
  return true;
#endif
}

static uptr GetMmapGranularity() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwAllocationGranularity;
}

UNUSED static uptr RoundUpTo(uptr size, uptr boundary) {
  return (size + boundary - 1) & ~(boundary - 1);
}

// FIXME: internal_str* and internal_mem* functions should be moved from the
// ASan sources into interception/.

static size_t _strlen(const char *str) {
  const char* p = str;
  while (*p != '\0') ++p;
  return p - str;
}

static char* _strchr(char* str, char c) {
  while (*str) {
    if (*str == c)
      return str;
    ++str;
  }
  return nullptr;
}

static void _memset(void *p, int value, size_t sz) {
  for (size_t i = 0; i < sz; ++i)
    ((char*)p)[i] = (char)value;
}

static void _memcpy(void *dst, void *src, size_t sz) {
  char *dst_c = (char*)dst,
       *src_c = (char*)src;
  for (size_t i = 0; i < sz; ++i)
    dst_c[i] = src_c[i];
}

static bool ChangeMemoryProtection(
    uptr address, uptr size, DWORD *old_protection) {
  return ::VirtualProtect((void*)address, size,
                          PAGE_EXECUTE_READWRITE,
                          old_protection) != FALSE;
}

static bool RestoreMemoryProtection(
    uptr address, uptr size, DWORD old_protection) {
  DWORD unused;
  return ::VirtualProtect((void*)address, size,
                          old_protection,
                          &unused) != FALSE;
}

static bool IsMemoryPadding(uptr address, uptr size) {
  u8* function = (u8*)address;
  for (size_t i = 0; i < size; ++i)
    if (function[i] != 0x90 && function[i] != 0xCC)
      return false;
  return true;
}

static const u8 kHintNop8Bytes[] = {
  0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00
};

template<class T>
static bool FunctionHasPrefix(uptr address, const T &pattern) {
  u8* function = (u8*)address - sizeof(pattern);
  for (size_t i = 0; i < sizeof(pattern); ++i)
    if (function[i] != pattern[i])
      return false;
  return true;
}

static bool FunctionHasPadding(uptr address, uptr size) {
  if (IsMemoryPadding(address - size, size))
    return true;
  if (size <= sizeof(kHintNop8Bytes) &&
      FunctionHasPrefix(address, kHintNop8Bytes))
    return true;
  return false;
}

static void WritePadding(uptr from, uptr size) {
  _memset((void*)from, 0xCC, (size_t)size);
}

static void WriteJumpInstruction(uptr from, uptr target) {
  if (!DistanceIsWithin2Gig(from + kJumpInstructionLength, target)) {
    ReportError(
        "interception_win: cannot write jmp further than 2GB away, from %p to "
        "%p.\n",
        (void *)from, (void *)target);
    InterceptionFailed();
  }
  ptrdiff_t offset = target - from - kJumpInstructionLength;
  *(u8*)from = 0xE9;
  *(u32*)(from + 1) = offset;
}

static void WriteShortJumpInstruction(uptr from, uptr target) {
  sptr offset = target - from - kShortJumpInstructionLength;
  if (offset < -128 || offset > 127)
    InterceptionFailed();
  *(u8*)from = 0xEB;
  *(u8*)(from + 1) = (u8)offset;
}

#if SANITIZER_WINDOWS64
static void WriteIndirectJumpInstruction(uptr from, uptr indirect_target) {
  // jmp [rip + <offset>] = FF 25 <offset> where <offset> is a relative
  // offset.
  // The offset is the distance from then end of the jump instruction to the
  // memory location containing the targeted address. The displacement is still
  // 32-bit in x64, so indirect_target must be located within +/- 2GB range.
  int offset = indirect_target - from - kIndirectJumpInstructionLength;
  if (!DistanceIsWithin2Gig(from + kIndirectJumpInstructionLength,
                            indirect_target)) {
    ReportError(
        "interception_win: cannot write indirect jmp with target further than "
        "2GB away, from %p to %p.\n",
        (void *)from, (void *)indirect_target);
    InterceptionFailed();
  }
  *(u16*)from = 0x25FF;
  *(u32*)(from + 2) = offset;
}
#endif

static void WriteBranch(
    uptr from, uptr indirect_target, uptr target) {
#if SANITIZER_WINDOWS64
  WriteIndirectJumpInstruction(from, indirect_target);
  *(u64*)indirect_target = target;
#else
  (void)indirect_target;
  WriteJumpInstruction(from, target);
#endif
}

static void WriteDirectBranch(uptr from, uptr target) {
#if SANITIZER_WINDOWS64
  // Emit an indirect jump through immediately following bytes:
  //   jmp [rip + kBranchLength]
  //   .quad <target>
  WriteBranch(from, from + kBranchLength, target);
#else
  WriteJumpInstruction(from, target);
#endif
}

struct TrampolineMemoryRegion {
  uptr content;
  uptr allocated_size;
  uptr max_size;
};

UNUSED static const uptr kTrampolineScanLimitRange = 1ull << 31;  // 2 gig
static const int kMaxTrampolineRegion = 1024;
static TrampolineMemoryRegion TrampolineRegions[kMaxTrampolineRegion];

static void *AllocateTrampolineRegion(uptr image_address, size_t granularity) {
#if SANITIZER_WINDOWS64
  uptr address = image_address;
  uptr scanned = 0;
  while (scanned < kTrampolineScanLimitRange) {
    MEMORY_BASIC_INFORMATION info;
    if (!::VirtualQuery((void*)address, &info, sizeof(info)))
      return nullptr;

    // Check whether a region can be allocated at |address|.
    if (info.State == MEM_FREE && info.RegionSize >= granularity) {
      void *page = ::VirtualAlloc((void*)RoundUpTo(address, granularity),
                                  granularity,
                                  MEM_RESERVE | MEM_COMMIT,
                                  PAGE_EXECUTE_READWRITE);
      return page;
    }

    // Move to the next region.
    address = (uptr)info.BaseAddress + info.RegionSize;
    scanned += info.RegionSize;
  }
  return nullptr;
#else
  return ::VirtualAlloc(nullptr,
                        granularity,
                        MEM_RESERVE | MEM_COMMIT,
                        PAGE_EXECUTE_READWRITE);
#endif
}

// Used by unittests to release mapped memory space.
void TestOnlyReleaseTrampolineRegions() {
  for (size_t bucket = 0; bucket < kMaxTrampolineRegion; ++bucket) {
    TrampolineMemoryRegion *current = &TrampolineRegions[bucket];
    if (current->content == 0)
      return;
    ::VirtualFree((void*)current->content, 0, MEM_RELEASE);
    current->content = 0;
  }
}

static uptr AllocateMemoryForTrampoline(uptr image_address, size_t size) {
  // Find a region within 2G with enough space to allocate |size| bytes.
  TrampolineMemoryRegion *region = nullptr;
  for (size_t bucket = 0; bucket < kMaxTrampolineRegion; ++bucket) {
    TrampolineMemoryRegion* current = &TrampolineRegions[bucket];
    if (current->content == 0) {
      // No valid region found, allocate a new region.
      size_t bucket_size = GetMmapGranularity();
      void *content = AllocateTrampolineRegion(image_address, bucket_size);
      if (content == nullptr)
        return 0U;

      current->content = (uptr)content;
      current->allocated_size = 0;
      current->max_size = bucket_size;
      region = current;
      break;
    } else if (current->max_size - current->allocated_size > size) {
#if SANITIZER_WINDOWS64
        // In 64-bits, the memory space must be allocated within 2G boundary.
        uptr next_address = current->content + current->allocated_size;
        if (next_address < image_address ||
            next_address - image_address >= 0x7FFF0000)
          continue;
#endif
      // The space can be allocated in the current region.
      region = current;
      break;
    }
  }

  // Failed to find a region.
  if (region == nullptr)
    return 0U;

  // Allocate the space in the current region.
  uptr allocated_space = region->content + region->allocated_size;
  region->allocated_size += size;
  WritePadding(allocated_space, size);

  return allocated_space;
}

// The following prologues cannot be patched because of the short jump
// jumping to the patching region.

// Short jump patterns  below are only for x86_64.
#  if SANITIZER_WINDOWS_x64
// ntdll!wcslen in Win11
//   488bc1          mov     rax,rcx
//   0fb710          movzx   edx,word ptr [rax]
//   4883c002        add     rax,2
//   6685d2          test    dx,dx
//   75f4            jne     -12
static const u8 kPrologueWithShortJump1[] = {
    0x48, 0x8b, 0xc1, 0x0f, 0xb7, 0x10, 0x48, 0x83,
    0xc0, 0x02, 0x66, 0x85, 0xd2, 0x75, 0xf4,
};

// ntdll!strrchr in Win11
//   4c8bc1          mov     r8,rcx
//   8a01            mov     al,byte ptr [rcx]
//   48ffc1          inc     rcx
//   84c0            test    al,al
//   75f7            jne     -9
static const u8 kPrologueWithShortJump2[] = {
    0x4c, 0x8b, 0xc1, 0x8a, 0x01, 0x48, 0xff, 0xc1,
    0x84, 0xc0, 0x75, 0xf7,
};
#endif

// Returns 0 on error.
static size_t GetInstructionSize(uptr address, size_t* rel_offset = nullptr) {
#if SANITIZER_ARM64
  // An ARM64 instruction is 4 bytes long.
  return 4;
#endif

#  if SANITIZER_WINDOWS_x64
  if (memcmp((u8*)address, kPrologueWithShortJump1,
             sizeof(kPrologueWithShortJump1)) == 0 ||
      memcmp((u8*)address, kPrologueWithShortJump2,
             sizeof(kPrologueWithShortJump2)) == 0) {
    return 0;
  }
#endif

  switch (*(u64*)address) {
    case 0x90909090909006EB:  // stub: jmp over 6 x nop.
      return 8;
  }

  switch (*(u8*)address) {
    case 0x90:  // 90 : nop
    case 0xC3:  // C3 : ret   (for small/empty function interception
    case 0xCC:  // CC : int 3  i.e. registering weak functions)
      return 1;

    case 0x50:  // push eax / rax
    case 0x51:  // push ecx / rcx
    case 0x52:  // push edx / rdx
    case 0x53:  // push ebx / rbx
    case 0x54:  // push esp / rsp
    case 0x55:  // push ebp / rbp
    case 0x56:  // push esi / rsi
    case 0x57:  // push edi / rdi
    case 0x5D:  // pop ebp / rbp
      return 1;

    case 0x6A:  // 6A XX = push XX
      return 2;

    case 0xb8:  // b8 XX XX XX XX : mov eax, XX XX XX XX
    case 0xB9:  // b9 XX XX XX XX : mov ecx, XX XX XX XX
      return 5;

    // Cannot overwrite control-instruction. Return 0 to indicate failure.
    case 0xE9:  // E9 XX XX XX XX : jmp <label>
    case 0xE8:  // E8 XX XX XX XX : call <func>
    case 0xEB:  // EB XX : jmp XX (short jump)
    case 0x70:  // 7Y YY : jy XX (short conditional jump)
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7B:
    case 0x7C:
    case 0x7D:
    case 0x7E:
    case 0x7F:
      return 0;
  }

  switch (*(u16*)(address)) {
    case 0x018A:  // 8A 01 : mov al, byte ptr [ecx]
    case 0xFF8B:  // 8B FF : mov edi, edi
    case 0xEC8B:  // 8B EC : mov ebp, esp
    case 0xc889:  // 89 C8 : mov eax, ecx
    case 0xE589:  // 89 E5 : mov ebp, esp
    case 0xC18B:  // 8B C1 : mov eax, ecx
    case 0xC033:  // 33 C0 : xor eax, eax
    case 0xC933:  // 33 C9 : xor ecx, ecx
    case 0xD233:  // 33 D2 : xor edx, edx
      return 2;

    // Cannot overwrite control-instruction. Return 0 to indicate failure.
    case 0x25FF:  // FF 25 XX XX XX XX : jmp [XXXXXXXX]
      return 0;
  }

  switch (0x00FFFFFF & *(u32*)address) {
    case 0x24A48D:  // 8D A4 24 XX XX XX XX : lea esp, [esp + XX XX XX XX]
      return 7;
  }

  switch (0x000000FF & *(u32 *)address) {
    case 0xc2:  // C2 XX XX : ret XX (needed for registering weak functions)
      return 3;
  }

#  if SANITIZER_WINDOWS_x64
  switch (*(u8*)address) {
    case 0xA1:  // A1 XX XX XX XX XX XX XX XX :
                //   movabs eax, dword ptr ds:[XXXXXXXX]
      return 9;

    case 0x83:
      const u8 next_byte = *(u8*)(address + 1);
      const u8 mod = next_byte >> 6;
      const u8 rm = next_byte & 7;
      if (mod == 1 && rm == 4)
        return 5;  // 83 ModR/M SIB Disp8 Imm8
                   //   add|or|adc|sbb|and|sub|xor|cmp [r+disp8], imm8
  }

  switch (*(u16*)address) {
    case 0x5040:  // push rax
    case 0x5140:  // push rcx
    case 0x5240:  // push rdx
    case 0x5340:  // push rbx
    case 0x5440:  // push rsp
    case 0x5540:  // push rbp
    case 0x5640:  // push rsi
    case 0x5740:  // push rdi
    case 0x5441:  // push r12
    case 0x5541:  // push r13
    case 0x5641:  // push r14
    case 0x5741:  // push r15
    case 0x9066:  // Two-byte NOP
    case 0xc084:  // test al, al
    case 0x018a:  // mov al, byte ptr [rcx]
      return 2;

    case 0x058A:  // 8A 05 XX XX XX XX : mov al, byte ptr [XX XX XX XX]
    case 0x058B:  // 8B 05 XX XX XX XX : mov eax, dword ptr [XX XX XX XX]
      if (rel_offset)
        *rel_offset = 2;
      return 6;
  }

  switch (0x00FFFFFF & *(u32*)address) {
    case 0xe58948:    // 48 8b c4 : mov rbp, rsp
    case 0xc18b48:    // 48 8b c1 : mov rax, rcx
    case 0xc48b48:    // 48 8b c4 : mov rax, rsp
    case 0xd9f748:    // 48 f7 d9 : neg rcx
    case 0xd12b48:    // 48 2b d1 : sub rdx, rcx
    case 0x07c1f6:    // f6 c1 07 : test cl, 0x7
    case 0xc98548:    // 48 85 C9 : test rcx, rcx
    case 0xd28548:    // 48 85 d2 : test rdx, rdx
    case 0xc0854d:    // 4d 85 c0 : test r8, r8
    case 0xc2b60f:    // 0f b6 c2 : movzx eax, dl
    case 0xc03345:    // 45 33 c0 : xor r8d, r8d
    case 0xc93345:    // 45 33 c9 : xor r9d, r9d
    case 0xdb3345:    // 45 33 DB : xor r11d, r11d
    case 0xd98b4c:    // 4c 8b d9 : mov r11, rcx
    case 0xd28b4c:    // 4c 8b d2 : mov r10, rdx
    case 0xc98b4c:    // 4C 8B C9 : mov r9, rcx
    case 0xc18b4c:    // 4C 8B C1 : mov r8, rcx
    case 0xd2b60f:    // 0f b6 d2 : movzx edx, dl
    case 0xca2b48:    // 48 2b ca : sub rcx, rdx
    case 0xca3b48:    // 48 3b ca : cmp rcx, rdx
    case 0x10b70f:    // 0f b7 10 : movzx edx, WORD PTR [rax]
    case 0xc00b4d:    // 3d 0b c0 : or r8, r8
    case 0xc08b41:    // 41 8b c0 : mov eax, r8d
    case 0xd18b48:    // 48 8b d1 : mov rdx, rcx
    case 0xdc8b4c:    // 4c 8b dc : mov r11, rsp
    case 0xd18b4c:    // 4c 8b d1 : mov r10, rcx
    case 0xE0E483:    // 83 E4 E0 : and esp, 0xFFFFFFE0
      return 3;

    case 0xec8348:    // 48 83 ec XX : sub rsp, XX
    case 0xf88349:    // 49 83 f8 XX : cmp r8, XX
    case 0x588948:    // 48 89 58 XX : mov QWORD PTR[rax + XX], rbx
      return 4;

    case 0xec8148:    // 48 81 EC XX XX XX XX : sub rsp, XXXXXXXX
      return 7;

    case 0x058b48:    // 48 8b 05 XX XX XX XX :
                      //   mov rax, QWORD PTR [rip + XXXXXXXX]
    case 0x058d48:    // 48 8d 05 XX XX XX XX :
                      //   lea rax, QWORD PTR [rip + XXXXXXXX]
    case 0x25ff48:    // 48 ff 25 XX XX XX XX :
                      //   rex.W jmp QWORD PTR [rip + XXXXXXXX]
    case 0x158D4C:    // 4c 8d 15 XX XX XX XX : lea r10, [rip + XX]
      // Instructions having offset relative to 'rip' need offset adjustment.
      if (rel_offset)
        *rel_offset = 3;
      return 7;

    case 0x2444c7:    // C7 44 24 XX YY YY YY YY
                      //   mov dword ptr [rsp + XX], YYYYYYYY
      return 8;
  }

  switch (*(u32*)(address)) {
    case 0x24448b48:  // 48 8b 44 24 XX : mov rax, QWORD ptr [rsp + XX]
    case 0x246c8948:  // 48 89 6C 24 XX : mov QWORD ptr [rsp + XX], rbp
    case 0x245c8948:  // 48 89 5c 24 XX : mov QWORD PTR [rsp + XX], rbx
    case 0x24748948:  // 48 89 74 24 XX : mov QWORD PTR [rsp + XX], rsi
    case 0x247c8948:  // 48 89 7c 24 XX : mov QWORD PTR [rsp + XX], rdi
    case 0x244C8948:  // 48 89 4C 24 XX : mov QWORD PTR [rsp + XX], rcx
    case 0x24548948:  // 48 89 54 24 XX : mov QWORD PTR [rsp + XX], rdx
    case 0x244c894c:  // 4c 89 4c 24 XX : mov QWORD PTR [rsp + XX], r9
    case 0x2444894c:  // 4c 89 44 24 XX : mov QWORD PTR [rsp + XX], r8
      return 5;
    case 0x24648348:  // 48 83 64 24 XX : and QWORD PTR [rsp + XX], YY
      return 6;
  }

#else

  switch (*(u8*)address) {
    case 0xA1:  // A1 XX XX XX XX :  mov eax, dword ptr ds:[XXXXXXXX]
      return 5;
  }
  switch (*(u16*)address) {
    case 0x458B:  // 8B 45 XX : mov eax, dword ptr [ebp + XX]
    case 0x5D8B:  // 8B 5D XX : mov ebx, dword ptr [ebp + XX]
    case 0x7D8B:  // 8B 7D XX : mov edi, dword ptr [ebp + XX]
    case 0xEC83:  // 83 EC XX : sub esp, XX
    case 0x75FF:  // FF 75 XX : push dword ptr [ebp + XX]
      return 3;
    case 0xC1F7:  // F7 C1 XX YY ZZ WW : test ecx, WWZZYYXX
    case 0x25FF:  // FF 25 XX YY ZZ WW : jmp dword ptr ds:[WWZZYYXX]
      return 6;
    case 0x3D83:  // 83 3D XX YY ZZ WW TT : cmp TT, WWZZYYXX
      return 7;
    case 0x7D83:  // 83 7D XX YY : cmp dword ptr [ebp + XX], YY
      return 4;
  }

  switch (0x00FFFFFF & *(u32*)address) {
    case 0x24448A:  // 8A 44 24 XX : mov eal, dword ptr [esp + XX]
    case 0x24448B:  // 8B 44 24 XX : mov eax, dword ptr [esp + XX]
    case 0x244C8B:  // 8B 4C 24 XX : mov ecx, dword ptr [esp + XX]
    case 0x24548B:  // 8B 54 24 XX : mov edx, dword ptr [esp + XX]
    case 0x245C8B:  // 8B 5C 24 XX : mov ebx, dword ptr [esp + XX]
    case 0x246C8B:  // 8B 6C 24 XX : mov ebp, dword ptr [esp + XX]
    case 0x24748B:  // 8B 74 24 XX : mov esi, dword ptr [esp + XX]
    case 0x247C8B:  // 8B 7C 24 XX : mov edi, dword ptr [esp + XX]
      return 4;
  }

  switch (*(u32*)address) {
    case 0x2444B60F:  // 0F B6 44 24 XX : movzx eax, byte ptr [esp + XX]
      return 5;
  }
#endif

  // Unknown instruction! This might happen when we add a new interceptor, use
  // a new compiler version, or if Windows changed how some functions are
  // compiled. In either case, we print the address and 8 bytes of instructions
  // to notify the user about the error and to help identify the unknown
  // instruction. Don't treat this as a fatal error, though we can break the
  // debugger if one has been attached.
  u8 *bytes = (u8 *)address;
  ReportError(
      "interception_win: unhandled instruction at %p: %02x %02x %02x %02x %02x "
      "%02x %02x %02x\n",
      (void *)address, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
      bytes[5], bytes[6], bytes[7]);
  if (::IsDebuggerPresent())
    __debugbreak();
  return 0;
}

// Returns 0 on error.
static size_t RoundUpToInstrBoundary(size_t size, uptr address) {
  size_t cursor = 0;
  while (cursor < size) {
    size_t instruction_size = GetInstructionSize(address + cursor);
    if (!instruction_size)
      return 0;
    cursor += instruction_size;
  }
  return cursor;
}

static bool CopyInstructions(uptr to, uptr from, size_t size) {
  size_t cursor = 0;
  while (cursor != size) {
    size_t rel_offset = 0;
    size_t instruction_size = GetInstructionSize(from + cursor, &rel_offset);
    if (!instruction_size)
      return false;
    _memcpy((void *)(to + cursor), (void *)(from + cursor),
            (size_t)instruction_size);
    if (rel_offset) {
#  if SANITIZER_WINDOWS64
      // we want to make sure that the new relative offset still fits in 32-bits
      // this will be untrue if relocated_offset \notin [-2**31, 2**31)
      s64 delta = to - from;
      s64 relocated_offset = *(s32 *)(to + cursor + rel_offset) - delta;
      if (-0x8000'0000ll > relocated_offset || relocated_offset > 0x7FFF'FFFFll)
        return false;
#  else
      // on 32-bit, the relative offset will always be correct
      s32 delta = to - from;
      s32 relocated_offset = *(s32 *)(to + cursor + rel_offset) - delta;
#  endif
      *(s32 *)(to + cursor + rel_offset) = relocated_offset;
    }
    cursor += instruction_size;
  }
  return true;
}


#if !SANITIZER_WINDOWS64
bool OverrideFunctionWithDetour(
    uptr old_func, uptr new_func, uptr *orig_old_func) {
  const int kDetourHeaderLen = 5;
  const u16 kDetourInstruction = 0xFF8B;

  uptr header = (uptr)old_func - kDetourHeaderLen;
  uptr patch_length = kDetourHeaderLen + kShortJumpInstructionLength;

  // Validate that the function is hookable.
  if (*(u16*)old_func != kDetourInstruction ||
      !IsMemoryPadding(header, kDetourHeaderLen))
    return false;

  // Change memory protection to writable.
  DWORD protection = 0;
  if (!ChangeMemoryProtection(header, patch_length, &protection))
    return false;

  // Write a relative jump to the redirected function.
  WriteJumpInstruction(header, new_func);

  // Write the short jump to the function prefix.
  WriteShortJumpInstruction(old_func, header);

  // Restore previous memory protection.
  if (!RestoreMemoryProtection(header, patch_length, protection))
    return false;

  if (orig_old_func)
    *orig_old_func = old_func + kShortJumpInstructionLength;

  return true;
}
#endif

bool OverrideFunctionWithRedirectJump(
    uptr old_func, uptr new_func, uptr *orig_old_func) {
  // Check whether the first instruction is a relative jump.
  if (*(u8*)old_func != 0xE9)
    return false;

  if (orig_old_func) {
    sptr relative_offset = *(s32 *)(old_func + 1);
    uptr absolute_target = old_func + relative_offset + kJumpInstructionLength;
    *orig_old_func = absolute_target;
  }

#if SANITIZER_WINDOWS64
  // If needed, get memory space for a trampoline jump.
  uptr trampoline = AllocateMemoryForTrampoline(old_func, kDirectBranchLength);
  if (!trampoline)
    return false;
  WriteDirectBranch(trampoline, new_func);
#endif

  // Change memory protection to writable.
  DWORD protection = 0;
  if (!ChangeMemoryProtection(old_func, kJumpInstructionLength, &protection))
    return false;

  // Write a relative jump to the redirected function.
  WriteJumpInstruction(old_func, FIRST_32_SECOND_64(new_func, trampoline));

  // Restore previous memory protection.
  if (!RestoreMemoryProtection(old_func, kJumpInstructionLength, protection))
    return false;

  return true;
}

bool OverrideFunctionWithHotPatch(
    uptr old_func, uptr new_func, uptr *orig_old_func) {
  const int kHotPatchHeaderLen = kBranchLength;

  uptr header = (uptr)old_func - kHotPatchHeaderLen;
  uptr patch_length = kHotPatchHeaderLen + kShortJumpInstructionLength;

  // Validate that the function is hot patchable.
  size_t instruction_size = GetInstructionSize(old_func);
  if (instruction_size < kShortJumpInstructionLength ||
      !FunctionHasPadding(old_func, kHotPatchHeaderLen))
    return false;

  if (orig_old_func) {
    // Put the needed instructions into the trampoline bytes.
    uptr trampoline_length = instruction_size + kDirectBranchLength;
    uptr trampoline = AllocateMemoryForTrampoline(old_func, trampoline_length);
    if (!trampoline)
      return false;
    if (!CopyInstructions(trampoline, old_func, instruction_size))
      return false;
    WriteDirectBranch(trampoline + instruction_size,
                      old_func + instruction_size);
    *orig_old_func = trampoline;
  }

  // If needed, get memory space for indirect address.
  uptr indirect_address = 0;
#if SANITIZER_WINDOWS64
  indirect_address = AllocateMemoryForTrampoline(old_func, kAddressLength);
  if (!indirect_address)
    return false;
#endif

  // Change memory protection to writable.
  DWORD protection = 0;
  if (!ChangeMemoryProtection(header, patch_length, &protection))
    return false;

  // Write jumps to the redirected function.
  WriteBranch(header, indirect_address, new_func);
  WriteShortJumpInstruction(old_func, header);

  // Restore previous memory protection.
  if (!RestoreMemoryProtection(header, patch_length, protection))
    return false;

  return true;
}

bool OverrideFunctionWithTrampoline(
    uptr old_func, uptr new_func, uptr *orig_old_func) {

  size_t instructions_length = kBranchLength;
  size_t padding_length = 0;
  uptr indirect_address = 0;

  if (orig_old_func) {
    // Find out the number of bytes of the instructions we need to copy
    // to the trampoline.
    instructions_length = RoundUpToInstrBoundary(kBranchLength, old_func);
    if (!instructions_length)
      return false;

    // Put the needed instructions into the trampoline bytes.
    uptr trampoline_length = instructions_length + kDirectBranchLength;
    uptr trampoline = AllocateMemoryForTrampoline(old_func, trampoline_length);
    if (!trampoline)
      return false;
    if (!CopyInstructions(trampoline, old_func, instructions_length))
      return false;
    WriteDirectBranch(trampoline + instructions_length,
                      old_func + instructions_length);
    *orig_old_func = trampoline;
  }

#if SANITIZER_WINDOWS64
  // Check if the targeted address can be encoded in the function padding.
  // Otherwise, allocate it in the trampoline region.
  if (IsMemoryPadding(old_func - kAddressLength, kAddressLength)) {
    indirect_address = old_func - kAddressLength;
    padding_length = kAddressLength;
  } else {
    indirect_address = AllocateMemoryForTrampoline(old_func, kAddressLength);
    if (!indirect_address)
      return false;
  }
#endif

  // Change memory protection to writable.
  uptr patch_address = old_func - padding_length;
  uptr patch_length = instructions_length + padding_length;
  DWORD protection = 0;
  if (!ChangeMemoryProtection(patch_address, patch_length, &protection))
    return false;

  // Patch the original function.
  WriteBranch(old_func, indirect_address, new_func);

  // Restore previous memory protection.
  if (!RestoreMemoryProtection(patch_address, patch_length, protection))
    return false;

  return true;
}

bool OverrideFunction(
    uptr old_func, uptr new_func, uptr *orig_old_func) {
#if !SANITIZER_WINDOWS64
  if (OverrideFunctionWithDetour(old_func, new_func, orig_old_func))
    return true;
#endif
  if (OverrideFunctionWithRedirectJump(old_func, new_func, orig_old_func))
    return true;
  if (OverrideFunctionWithHotPatch(old_func, new_func, orig_old_func))
    return true;
  if (OverrideFunctionWithTrampoline(old_func, new_func, orig_old_func))
    return true;
  return false;
}

static void **InterestingDLLsAvailable() {
  static const char *InterestingDLLs[] = {
    "kernel32.dll",
    "msvcr100d.dll",      // VS2010
    "msvcr110d.dll",      // VS2012
    "msvcr120d.dll",      // VS2013
    "vcruntime140d.dll",  // VS2015
    "ucrtbased.dll",      // Universal CRT
    "msvcr100.dll",       // VS2010
    "msvcr110.dll",       // VS2012
    "msvcr120.dll",       // VS2013
    "vcruntime140.dll",   // VS2015
    "ucrtbase.dll",       // Universal CRT
#  if (defined(__MINGW32__) && defined(__i386__))
    "libc++.dll",     // libc++
    "libunwind.dll",  // libunwind
#  endif
    // NTDLL should go last as it exports some functions that we should
    // override in the CRT [presumably only used internally].
    "ntdll.dll",
    NULL
  };
  static void *result[ARRAY_SIZE(InterestingDLLs)] = { 0 };
  if (!result[0]) {
    for (size_t i = 0, j = 0; InterestingDLLs[i]; ++i) {
      if (HMODULE h = GetModuleHandleA(InterestingDLLs[i]))
        result[j++] = (void *)h;
    }
  }
  return &result[0];
}

namespace {
// Utility for reading loaded PE images.
template <typename T> class RVAPtr {
 public:
  RVAPtr(void *module, uptr rva)
      : ptr_(reinterpret_cast<T *>(reinterpret_cast<char *>(module) + rva)) {}
  operator T *() { return ptr_; }
  T *operator->() { return ptr_; }
  T *operator++() { return ++ptr_; }

 private:
  T *ptr_;
};
} // namespace

// Internal implementation of GetProcAddress. At least since Windows 8,
// GetProcAddress appears to initialize DLLs before returning function pointers
// into them. This is problematic for the sanitizers, because they typically
// want to intercept malloc *before* MSVCRT initializes. Our internal
// implementation walks the export list manually without doing initialization.
uptr InternalGetProcAddress(void *module, const char *func_name) {
  // Check that the module header is full and present.
  RVAPtr<IMAGE_DOS_HEADER> dos_stub(module, 0);
  RVAPtr<IMAGE_NT_HEADERS> headers(module, dos_stub->e_lfanew);
  if (!module || dos_stub->e_magic != IMAGE_DOS_SIGNATURE ||  // "MZ"
      headers->Signature != IMAGE_NT_SIGNATURE ||             // "PE\0\0"
      headers->FileHeader.SizeOfOptionalHeader <
          sizeof(IMAGE_OPTIONAL_HEADER)) {
    return 0;
  }

  IMAGE_DATA_DIRECTORY *export_directory =
      &headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (export_directory->Size == 0)
    return 0;
  RVAPtr<IMAGE_EXPORT_DIRECTORY> exports(module,
                                         export_directory->VirtualAddress);
  RVAPtr<DWORD> functions(module, exports->AddressOfFunctions);
  RVAPtr<DWORD> names(module, exports->AddressOfNames);
  RVAPtr<WORD> ordinals(module, exports->AddressOfNameOrdinals);

  for (DWORD i = 0; i < exports->NumberOfNames; i++) {
    RVAPtr<char> name(module, names[i]);
    if (!strcmp(func_name, name)) {
      DWORD index = ordinals[i];
      RVAPtr<char> func(module, functions[index]);

      // Handle forwarded functions.
      DWORD offset = functions[index];
      if (offset >= export_directory->VirtualAddress &&
          offset < export_directory->VirtualAddress + export_directory->Size) {
        // An entry for a forwarded function is a string with the following
        // format: "<module> . <function_name>" that is stored into the
        // exported directory.
        char function_name[256];
        size_t funtion_name_length = _strlen(func);
        if (funtion_name_length >= sizeof(function_name) - 1)
          InterceptionFailed();

        _memcpy(function_name, func, funtion_name_length);
        function_name[funtion_name_length] = '\0';
        char* separator = _strchr(function_name, '.');
        if (!separator)
          InterceptionFailed();
        *separator = '\0';

        void* redirected_module = GetModuleHandleA(function_name);
        if (!redirected_module)
          InterceptionFailed();
        return InternalGetProcAddress(redirected_module, separator + 1);
      }

      return (uptr)(char *)func;
    }
  }

  return 0;
}

bool OverrideFunction(
    const char *func_name, uptr new_func, uptr *orig_old_func) {
  bool hooked = false;
  void **DLLs = InterestingDLLsAvailable();
  for (size_t i = 0; DLLs[i]; ++i) {
    uptr func_addr = InternalGetProcAddress(DLLs[i], func_name);
    if (func_addr &&
        OverrideFunction(func_addr, new_func, orig_old_func)) {
      hooked = true;
    }
  }
  return hooked;
}

bool OverrideImportedFunction(const char *module_to_patch,
                              const char *imported_module,
                              const char *function_name, uptr new_function,
                              uptr *orig_old_func) {
  HMODULE module = GetModuleHandleA(module_to_patch);
  if (!module)
    return false;

  // Check that the module header is full and present.
  RVAPtr<IMAGE_DOS_HEADER> dos_stub(module, 0);
  RVAPtr<IMAGE_NT_HEADERS> headers(module, dos_stub->e_lfanew);
  if (!module || dos_stub->e_magic != IMAGE_DOS_SIGNATURE ||  // "MZ"
      headers->Signature != IMAGE_NT_SIGNATURE ||             // "PE\0\0"
      headers->FileHeader.SizeOfOptionalHeader <
          sizeof(IMAGE_OPTIONAL_HEADER)) {
    return false;
  }

  IMAGE_DATA_DIRECTORY *import_directory =
      &headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

  // Iterate the list of imported DLLs. FirstThunk will be null for the last
  // entry.
  RVAPtr<IMAGE_IMPORT_DESCRIPTOR> imports(module,
                                          import_directory->VirtualAddress);
  for (; imports->FirstThunk != 0; ++imports) {
    RVAPtr<const char> modname(module, imports->Name);
    if (_stricmp(&*modname, imported_module) == 0)
      break;
  }
  if (imports->FirstThunk == 0)
    return false;

  // We have two parallel arrays: the import address table (IAT) and the table
  // of names. They start out containing the same data, but the loader rewrites
  // the IAT to hold imported addresses and leaves the name table in
  // OriginalFirstThunk alone.
  RVAPtr<IMAGE_THUNK_DATA> name_table(module, imports->OriginalFirstThunk);
  RVAPtr<IMAGE_THUNK_DATA> iat(module, imports->FirstThunk);
  for (; name_table->u1.Ordinal != 0; ++name_table, ++iat) {
    if (!IMAGE_SNAP_BY_ORDINAL(name_table->u1.Ordinal)) {
      RVAPtr<IMAGE_IMPORT_BY_NAME> import_by_name(
          module, name_table->u1.ForwarderString);
      const char *funcname = &import_by_name->Name[0];
      if (strcmp(funcname, function_name) == 0)
        break;
    }
  }
  if (name_table->u1.Ordinal == 0)
    return false;

  // Now we have the correct IAT entry. Do the swap. We have to make the page
  // read/write first.
  if (orig_old_func)
    *orig_old_func = iat->u1.AddressOfData;
  DWORD old_prot, unused_prot;
  if (!VirtualProtect(&iat->u1.AddressOfData, 4, PAGE_EXECUTE_READWRITE,
                      &old_prot))
    return false;
  iat->u1.AddressOfData = new_function;
  if (!VirtualProtect(&iat->u1.AddressOfData, 4, old_prot, &unused_prot))
    return false;  // Not clear if this failure bothers us.
  return true;
}

}  // namespace __interception

#endif  // SANITIZER_APPLE
