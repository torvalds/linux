//===-- ObjectFileMachO.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"

#include "Plugins/Process/Utility/RegisterContextDarwin_arm.h"
#include "Plugins/Process/Utility/RegisterContextDarwin_arm64.h"
#include "Plugins/Process/Utility/RegisterContextDarwin_i386.h"
#include "Plugins/Process/Utility/RegisterContextDarwin_x86_64.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Progress.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/DWARFCallFrameInfo.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include "lldb/Utility/UUID.h"

#include "lldb/Host/SafeMachO.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"

#include "ObjectFileMachO.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
// GetLLDBSharedCacheUUID() needs to call dlsym()
#include <dlfcn.h>
#include <mach/mach_init.h>
#include <mach/vm_map.h>
#include <lldb/Host/SafeMachO.h>
#endif

#ifndef __APPLE__
#include "lldb/Utility/AppleUuidCompatibility.h"
#else
#include <uuid/uuid.h>
#endif

#include <bitset>
#include <memory>
#include <optional>

// Unfortunately the signpost header pulls in the system MachO header, too.
#ifdef CPU_TYPE_ARM
#undef CPU_TYPE_ARM
#endif
#ifdef CPU_TYPE_ARM64
#undef CPU_TYPE_ARM64
#endif
#ifdef CPU_TYPE_ARM64_32
#undef CPU_TYPE_ARM64_32
#endif
#ifdef CPU_TYPE_I386
#undef CPU_TYPE_I386
#endif
#ifdef CPU_TYPE_X86_64
#undef CPU_TYPE_X86_64
#endif
#ifdef MH_DYLINKER
#undef MH_DYLINKER
#endif
#ifdef MH_OBJECT
#undef MH_OBJECT
#endif
#ifdef LC_VERSION_MIN_MACOSX
#undef LC_VERSION_MIN_MACOSX
#endif
#ifdef LC_VERSION_MIN_IPHONEOS
#undef LC_VERSION_MIN_IPHONEOS
#endif
#ifdef LC_VERSION_MIN_TVOS
#undef LC_VERSION_MIN_TVOS
#endif
#ifdef LC_VERSION_MIN_WATCHOS
#undef LC_VERSION_MIN_WATCHOS
#endif
#ifdef LC_BUILD_VERSION
#undef LC_BUILD_VERSION
#endif
#ifdef PLATFORM_MACOS
#undef PLATFORM_MACOS
#endif
#ifdef PLATFORM_MACCATALYST
#undef PLATFORM_MACCATALYST
#endif
#ifdef PLATFORM_IOS
#undef PLATFORM_IOS
#endif
#ifdef PLATFORM_IOSSIMULATOR
#undef PLATFORM_IOSSIMULATOR
#endif
#ifdef PLATFORM_TVOS
#undef PLATFORM_TVOS
#endif
#ifdef PLATFORM_TVOSSIMULATOR
#undef PLATFORM_TVOSSIMULATOR
#endif
#ifdef PLATFORM_WATCHOS
#undef PLATFORM_WATCHOS
#endif
#ifdef PLATFORM_WATCHOSSIMULATOR
#undef PLATFORM_WATCHOSSIMULATOR
#endif

#define THUMB_ADDRESS_BIT_MASK 0xfffffffffffffffeull
using namespace lldb;
using namespace lldb_private;
using namespace llvm::MachO;

LLDB_PLUGIN_DEFINE(ObjectFileMachO)

static void PrintRegisterValue(RegisterContext *reg_ctx, const char *name,
                               const char *alt_name, size_t reg_byte_size,
                               Stream &data) {
  const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(name);
  if (reg_info == nullptr)
    reg_info = reg_ctx->GetRegisterInfoByName(alt_name);
  if (reg_info) {
    lldb_private::RegisterValue reg_value;
    if (reg_ctx->ReadRegister(reg_info, reg_value)) {
      if (reg_info->byte_size >= reg_byte_size)
        data.Write(reg_value.GetBytes(), reg_byte_size);
      else {
        data.Write(reg_value.GetBytes(), reg_info->byte_size);
        for (size_t i = 0, n = reg_byte_size - reg_info->byte_size; i < n; ++i)
          data.PutChar(0);
      }
      return;
    }
  }
  // Just write zeros if all else fails
  for (size_t i = 0; i < reg_byte_size; ++i)
    data.PutChar(0);
}

class RegisterContextDarwin_x86_64_Mach : public RegisterContextDarwin_x86_64 {
public:
  RegisterContextDarwin_x86_64_Mach(lldb_private::Thread &thread,
                                    const DataExtractor &data)
      : RegisterContextDarwin_x86_64(thread, 0) {
    SetRegisterDataFrom_LC_THREAD(data);
  }

  void InvalidateAllRegisters() override {
    // Do nothing... registers are always valid...
  }

  void SetRegisterDataFrom_LC_THREAD(const DataExtractor &data) {
    lldb::offset_t offset = 0;
    SetError(GPRRegSet, Read, -1);
    SetError(FPURegSet, Read, -1);
    SetError(EXCRegSet, Read, -1);
    bool done = false;

    while (!done) {
      int flavor = data.GetU32(&offset);
      if (flavor == 0)
        done = true;
      else {
        uint32_t i;
        uint32_t count = data.GetU32(&offset);
        switch (flavor) {
        case GPRRegSet:
          for (i = 0; i < count; ++i)
            (&gpr.rax)[i] = data.GetU64(&offset);
          SetError(GPRRegSet, Read, 0);
          done = true;

          break;
        case FPURegSet:
          // TODO: fill in FPU regs....
          // SetError (FPURegSet, Read, -1);
          done = true;

          break;
        case EXCRegSet:
          exc.trapno = data.GetU32(&offset);
          exc.err = data.GetU32(&offset);
          exc.faultvaddr = data.GetU64(&offset);
          SetError(EXCRegSet, Read, 0);
          done = true;
          break;
        case 7:
        case 8:
        case 9:
          // fancy flavors that encapsulate of the above flavors...
          break;

        default:
          done = true;
          break;
        }
      }
    }
  }

  static bool Create_LC_THREAD(Thread *thread, Stream &data) {
    RegisterContextSP reg_ctx_sp(thread->GetRegisterContext());
    if (reg_ctx_sp) {
      RegisterContext *reg_ctx = reg_ctx_sp.get();

      data.PutHex32(GPRRegSet); // Flavor
      data.PutHex32(GPRWordCount);
      PrintRegisterValue(reg_ctx, "rax", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rbx", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rcx", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rdx", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rdi", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rsi", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rbp", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rsp", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "r8", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "r9", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "r10", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "r11", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "r12", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "r13", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "r14", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "r15", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rip", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "rflags", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "cs", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "fs", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "gs", nullptr, 8, data);

      //            // Write out the FPU registers
      //            const size_t fpu_byte_size = sizeof(FPU);
      //            size_t bytes_written = 0;
      //            data.PutHex32 (FPURegSet);
      //            data.PutHex32 (fpu_byte_size/sizeof(uint64_t));
      //            bytes_written += data.PutHex32(0); // uint32_t pad[0]
      //            bytes_written += data.PutHex32(0); // uint32_t pad[1]
      //            bytes_written += WriteRegister (reg_ctx, "fcw", "fctrl", 2,
      //            data);   // uint16_t    fcw;    // "fctrl"
      //            bytes_written += WriteRegister (reg_ctx, "fsw" , "fstat", 2,
      //            data);  // uint16_t    fsw;    // "fstat"
      //            bytes_written += WriteRegister (reg_ctx, "ftw" , "ftag", 1,
      //            data);   // uint8_t     ftw;    // "ftag"
      //            bytes_written += data.PutHex8  (0); // uint8_t pad1;
      //            bytes_written += WriteRegister (reg_ctx, "fop" , NULL, 2,
      //            data);     // uint16_t    fop;    // "fop"
      //            bytes_written += WriteRegister (reg_ctx, "fioff", "ip", 4,
      //            data);    // uint32_t    ip;     // "fioff"
      //            bytes_written += WriteRegister (reg_ctx, "fiseg", NULL, 2,
      //            data);    // uint16_t    cs;     // "fiseg"
      //            bytes_written += data.PutHex16 (0); // uint16_t    pad2;
      //            bytes_written += WriteRegister (reg_ctx, "dp", "fooff" , 4,
      //            data);   // uint32_t    dp;     // "fooff"
      //            bytes_written += WriteRegister (reg_ctx, "foseg", NULL, 2,
      //            data);    // uint16_t    ds;     // "foseg"
      //            bytes_written += data.PutHex16 (0); // uint16_t    pad3;
      //            bytes_written += WriteRegister (reg_ctx, "mxcsr", NULL, 4,
      //            data);    // uint32_t    mxcsr;
      //            bytes_written += WriteRegister (reg_ctx, "mxcsrmask", NULL,
      //            4, data);// uint32_t    mxcsrmask;
      //            bytes_written += WriteRegister (reg_ctx, "stmm0", NULL,
      //            sizeof(MMSReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "stmm1", NULL,
      //            sizeof(MMSReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "stmm2", NULL,
      //            sizeof(MMSReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "stmm3", NULL,
      //            sizeof(MMSReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "stmm4", NULL,
      //            sizeof(MMSReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "stmm5", NULL,
      //            sizeof(MMSReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "stmm6", NULL,
      //            sizeof(MMSReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "stmm7", NULL,
      //            sizeof(MMSReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm0" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm1" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm2" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm3" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm4" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm5" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm6" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm7" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm8" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm9" , NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm10", NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm11", NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm12", NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm13", NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm14", NULL,
      //            sizeof(XMMReg), data);
      //            bytes_written += WriteRegister (reg_ctx, "xmm15", NULL,
      //            sizeof(XMMReg), data);
      //
      //            // Fill rest with zeros
      //            for (size_t i=0, n = fpu_byte_size - bytes_written; i<n; ++
      //            i)
      //                data.PutChar(0);

      // Write out the EXC registers
      data.PutHex32(EXCRegSet);
      data.PutHex32(EXCWordCount);
      PrintRegisterValue(reg_ctx, "trapno", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "err", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "faultvaddr", nullptr, 8, data);
      return true;
    }
    return false;
  }

protected:
  int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) override { return 0; }

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) override { return 0; }

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) override { return 0; }

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) override {
    return 0;
  }

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) override {
    return 0;
  }

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) override {
    return 0;
  }
};

class RegisterContextDarwin_i386_Mach : public RegisterContextDarwin_i386 {
public:
  RegisterContextDarwin_i386_Mach(lldb_private::Thread &thread,
                                  const DataExtractor &data)
      : RegisterContextDarwin_i386(thread, 0) {
    SetRegisterDataFrom_LC_THREAD(data);
  }

  void InvalidateAllRegisters() override {
    // Do nothing... registers are always valid...
  }

  void SetRegisterDataFrom_LC_THREAD(const DataExtractor &data) {
    lldb::offset_t offset = 0;
    SetError(GPRRegSet, Read, -1);
    SetError(FPURegSet, Read, -1);
    SetError(EXCRegSet, Read, -1);
    bool done = false;

    while (!done) {
      int flavor = data.GetU32(&offset);
      if (flavor == 0)
        done = true;
      else {
        uint32_t i;
        uint32_t count = data.GetU32(&offset);
        switch (flavor) {
        case GPRRegSet:
          for (i = 0; i < count; ++i)
            (&gpr.eax)[i] = data.GetU32(&offset);
          SetError(GPRRegSet, Read, 0);
          done = true;

          break;
        case FPURegSet:
          // TODO: fill in FPU regs....
          // SetError (FPURegSet, Read, -1);
          done = true;

          break;
        case EXCRegSet:
          exc.trapno = data.GetU32(&offset);
          exc.err = data.GetU32(&offset);
          exc.faultvaddr = data.GetU32(&offset);
          SetError(EXCRegSet, Read, 0);
          done = true;
          break;
        case 7:
        case 8:
        case 9:
          // fancy flavors that encapsulate of the above flavors...
          break;

        default:
          done = true;
          break;
        }
      }
    }
  }

  static bool Create_LC_THREAD(Thread *thread, Stream &data) {
    RegisterContextSP reg_ctx_sp(thread->GetRegisterContext());
    if (reg_ctx_sp) {
      RegisterContext *reg_ctx = reg_ctx_sp.get();

      data.PutHex32(GPRRegSet); // Flavor
      data.PutHex32(GPRWordCount);
      PrintRegisterValue(reg_ctx, "eax", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "ebx", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "ecx", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "edx", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "edi", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "esi", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "ebp", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "esp", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "ss", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "eflags", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "eip", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "cs", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "ds", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "es", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "fs", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "gs", nullptr, 4, data);

      // Write out the EXC registers
      data.PutHex32(EXCRegSet);
      data.PutHex32(EXCWordCount);
      PrintRegisterValue(reg_ctx, "trapno", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "err", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "faultvaddr", nullptr, 4, data);
      return true;
    }
    return false;
  }

protected:
  int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) override { return 0; }

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) override { return 0; }

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) override { return 0; }

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) override {
    return 0;
  }

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) override {
    return 0;
  }

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) override {
    return 0;
  }
};

class RegisterContextDarwin_arm_Mach : public RegisterContextDarwin_arm {
public:
  RegisterContextDarwin_arm_Mach(lldb_private::Thread &thread,
                                 const DataExtractor &data)
      : RegisterContextDarwin_arm(thread, 0) {
    SetRegisterDataFrom_LC_THREAD(data);
  }

  void InvalidateAllRegisters() override {
    // Do nothing... registers are always valid...
  }

  void SetRegisterDataFrom_LC_THREAD(const DataExtractor &data) {
    lldb::offset_t offset = 0;
    SetError(GPRRegSet, Read, -1);
    SetError(FPURegSet, Read, -1);
    SetError(EXCRegSet, Read, -1);
    bool done = false;

    while (!done) {
      int flavor = data.GetU32(&offset);
      uint32_t count = data.GetU32(&offset);
      lldb::offset_t next_thread_state = offset + (count * 4);
      switch (flavor) {
      case GPRAltRegSet:
      case GPRRegSet: {
        // r0-r15, plus CPSR
        uint32_t gpr_buf_count = (sizeof(gpr.r) / sizeof(gpr.r[0])) + 1;
        if (count == gpr_buf_count) {
          for (uint32_t i = 0; i < (count - 1); ++i) {
            gpr.r[i] = data.GetU32(&offset);
          }
          gpr.cpsr = data.GetU32(&offset);

          SetError(GPRRegSet, Read, 0);
        }
      }
        offset = next_thread_state;
        break;

      case FPURegSet: {
        uint8_t *fpu_reg_buf = (uint8_t *)&fpu.floats;
        const int fpu_reg_buf_size = sizeof(fpu.floats);
        if (data.ExtractBytes(offset, fpu_reg_buf_size, eByteOrderLittle,
                              fpu_reg_buf) == fpu_reg_buf_size) {
          offset += fpu_reg_buf_size;
          fpu.fpscr = data.GetU32(&offset);
          SetError(FPURegSet, Read, 0);
        } else {
          done = true;
        }
      }
        offset = next_thread_state;
        break;

      case EXCRegSet:
        if (count == 3) {
          exc.exception = data.GetU32(&offset);
          exc.fsr = data.GetU32(&offset);
          exc.far = data.GetU32(&offset);
          SetError(EXCRegSet, Read, 0);
        }
        done = true;
        offset = next_thread_state;
        break;

      // Unknown register set flavor, stop trying to parse.
      default:
        done = true;
      }
    }
  }

  static bool Create_LC_THREAD(Thread *thread, Stream &data) {
    RegisterContextSP reg_ctx_sp(thread->GetRegisterContext());
    if (reg_ctx_sp) {
      RegisterContext *reg_ctx = reg_ctx_sp.get();

      data.PutHex32(GPRRegSet); // Flavor
      data.PutHex32(GPRWordCount);
      PrintRegisterValue(reg_ctx, "r0", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r1", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r2", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r3", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r4", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r5", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r6", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r7", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r8", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r9", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r10", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r11", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "r12", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "sp", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "lr", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "pc", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "cpsr", nullptr, 4, data);

      // Write out the EXC registers
      //            data.PutHex32 (EXCRegSet);
      //            data.PutHex32 (EXCWordCount);
      //            WriteRegister (reg_ctx, "exception", NULL, 4, data);
      //            WriteRegister (reg_ctx, "fsr", NULL, 4, data);
      //            WriteRegister (reg_ctx, "far", NULL, 4, data);
      return true;
    }
    return false;
  }

protected:
  int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) override { return -1; }

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) override { return -1; }

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) override { return -1; }

  int DoReadDBG(lldb::tid_t tid, int flavor, DBG &dbg) override { return -1; }

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) override {
    return 0;
  }

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) override {
    return 0;
  }

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) override {
    return 0;
  }

  int DoWriteDBG(lldb::tid_t tid, int flavor, const DBG &dbg) override {
    return -1;
  }
};

class RegisterContextDarwin_arm64_Mach : public RegisterContextDarwin_arm64 {
public:
  RegisterContextDarwin_arm64_Mach(lldb_private::Thread &thread,
                                   const DataExtractor &data)
      : RegisterContextDarwin_arm64(thread, 0) {
    SetRegisterDataFrom_LC_THREAD(data);
  }

  void InvalidateAllRegisters() override {
    // Do nothing... registers are always valid...
  }

  void SetRegisterDataFrom_LC_THREAD(const DataExtractor &data) {
    lldb::offset_t offset = 0;
    SetError(GPRRegSet, Read, -1);
    SetError(FPURegSet, Read, -1);
    SetError(EXCRegSet, Read, -1);
    bool done = false;
    while (!done) {
      int flavor = data.GetU32(&offset);
      uint32_t count = data.GetU32(&offset);
      lldb::offset_t next_thread_state = offset + (count * 4);
      switch (flavor) {
      case GPRRegSet:
        // x0-x29 + fp + lr + sp + pc (== 33 64-bit registers) plus cpsr (1
        // 32-bit register)
        if (count >= (33 * 2) + 1) {
          for (uint32_t i = 0; i < 29; ++i)
            gpr.x[i] = data.GetU64(&offset);
          gpr.fp = data.GetU64(&offset);
          gpr.lr = data.GetU64(&offset);
          gpr.sp = data.GetU64(&offset);
          gpr.pc = data.GetU64(&offset);
          gpr.cpsr = data.GetU32(&offset);
          SetError(GPRRegSet, Read, 0);
        }
        offset = next_thread_state;
        break;
      case FPURegSet: {
        uint8_t *fpu_reg_buf = (uint8_t *)&fpu.v[0];
        const int fpu_reg_buf_size = sizeof(fpu);
        if (fpu_reg_buf_size == count * sizeof(uint32_t) &&
            data.ExtractBytes(offset, fpu_reg_buf_size, eByteOrderLittle,
                              fpu_reg_buf) == fpu_reg_buf_size) {
          SetError(FPURegSet, Read, 0);
        } else {
          done = true;
        }
      }
        offset = next_thread_state;
        break;
      case EXCRegSet:
        if (count == 4) {
          exc.far = data.GetU64(&offset);
          exc.esr = data.GetU32(&offset);
          exc.exception = data.GetU32(&offset);
          SetError(EXCRegSet, Read, 0);
        }
        offset = next_thread_state;
        break;
      default:
        done = true;
        break;
      }
    }
  }

  static bool Create_LC_THREAD(Thread *thread, Stream &data) {
    RegisterContextSP reg_ctx_sp(thread->GetRegisterContext());
    if (reg_ctx_sp) {
      RegisterContext *reg_ctx = reg_ctx_sp.get();

      data.PutHex32(GPRRegSet); // Flavor
      data.PutHex32(GPRWordCount);
      PrintRegisterValue(reg_ctx, "x0", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x1", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x2", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x3", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x4", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x5", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x6", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x7", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x8", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x9", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x10", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x11", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x12", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x13", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x14", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x15", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x16", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x17", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x18", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x19", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x20", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x21", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x22", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x23", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x24", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x25", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x26", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x27", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "x28", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "fp", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "lr", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "sp", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "pc", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "cpsr", nullptr, 4, data);
      data.PutHex32(0); // uint32_t pad at the end

      // Write out the EXC registers
      data.PutHex32(EXCRegSet);
      data.PutHex32(EXCWordCount);
      PrintRegisterValue(reg_ctx, "far", nullptr, 8, data);
      PrintRegisterValue(reg_ctx, "esr", nullptr, 4, data);
      PrintRegisterValue(reg_ctx, "exception", nullptr, 4, data);
      return true;
    }
    return false;
  }

protected:
  int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) override { return -1; }

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) override { return -1; }

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) override { return -1; }

  int DoReadDBG(lldb::tid_t tid, int flavor, DBG &dbg) override { return -1; }

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) override {
    return 0;
  }

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) override {
    return 0;
  }

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) override {
    return 0;
  }

  int DoWriteDBG(lldb::tid_t tid, int flavor, const DBG &dbg) override {
    return -1;
  }
};

static uint32_t MachHeaderSizeFromMagic(uint32_t magic) {
  switch (magic) {
  case MH_MAGIC:
  case MH_CIGAM:
    return sizeof(struct llvm::MachO::mach_header);

  case MH_MAGIC_64:
  case MH_CIGAM_64:
    return sizeof(struct llvm::MachO::mach_header_64);
    break;

  default:
    break;
  }
  return 0;
}

#define MACHO_NLIST_ARM_SYMBOL_IS_THUMB 0x0008

char ObjectFileMachO::ID;

void ObjectFileMachO::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), GetPluginDescriptionStatic(), CreateInstance,
      CreateMemoryInstance, GetModuleSpecifications, SaveCore);
}

void ObjectFileMachO::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ObjectFile *ObjectFileMachO::CreateInstance(const lldb::ModuleSP &module_sp,
                                            DataBufferSP data_sp,
                                            lldb::offset_t data_offset,
                                            const FileSpec *file,
                                            lldb::offset_t file_offset,
                                            lldb::offset_t length) {
  if (!data_sp) {
    data_sp = MapFileData(*file, length, file_offset);
    if (!data_sp)
      return nullptr;
    data_offset = 0;
  }

  if (!ObjectFileMachO::MagicBytesMatch(data_sp, data_offset, length))
    return nullptr;

  // Update the data to contain the entire file if it doesn't already
  if (data_sp->GetByteSize() < length) {
    data_sp = MapFileData(*file, length, file_offset);
    if (!data_sp)
      return nullptr;
    data_offset = 0;
  }
  auto objfile_up = std::make_unique<ObjectFileMachO>(
      module_sp, data_sp, data_offset, file, file_offset, length);
  if (!objfile_up || !objfile_up->ParseHeader())
    return nullptr;

  return objfile_up.release();
}

ObjectFile *ObjectFileMachO::CreateMemoryInstance(
    const lldb::ModuleSP &module_sp, WritableDataBufferSP data_sp,
    const ProcessSP &process_sp, lldb::addr_t header_addr) {
  if (ObjectFileMachO::MagicBytesMatch(data_sp, 0, data_sp->GetByteSize())) {
    std::unique_ptr<ObjectFile> objfile_up(
        new ObjectFileMachO(module_sp, data_sp, process_sp, header_addr));
    if (objfile_up.get() && objfile_up->ParseHeader())
      return objfile_up.release();
  }
  return nullptr;
}

size_t ObjectFileMachO::GetModuleSpecifications(
    const lldb_private::FileSpec &file, lldb::DataBufferSP &data_sp,
    lldb::offset_t data_offset, lldb::offset_t file_offset,
    lldb::offset_t length, lldb_private::ModuleSpecList &specs) {
  const size_t initial_count = specs.GetSize();

  if (ObjectFileMachO::MagicBytesMatch(data_sp, 0, data_sp->GetByteSize())) {
    DataExtractor data;
    data.SetData(data_sp);
    llvm::MachO::mach_header header;
    if (ParseHeader(data, &data_offset, header)) {
      size_t header_and_load_cmds =
          header.sizeofcmds + MachHeaderSizeFromMagic(header.magic);
      if (header_and_load_cmds >= data_sp->GetByteSize()) {
        data_sp = MapFileData(file, header_and_load_cmds, file_offset);
        data.SetData(data_sp);
        data_offset = MachHeaderSizeFromMagic(header.magic);
      }
      if (data_sp) {
        ModuleSpec base_spec;
        base_spec.GetFileSpec() = file;
        base_spec.SetObjectOffset(file_offset);
        base_spec.SetObjectSize(length);
        GetAllArchSpecs(header, data, data_offset, base_spec, specs);
      }
    }
  }
  return specs.GetSize() - initial_count;
}

ConstString ObjectFileMachO::GetSegmentNameTEXT() {
  static ConstString g_segment_name_TEXT("__TEXT");
  return g_segment_name_TEXT;
}

ConstString ObjectFileMachO::GetSegmentNameDATA() {
  static ConstString g_segment_name_DATA("__DATA");
  return g_segment_name_DATA;
}

ConstString ObjectFileMachO::GetSegmentNameDATA_DIRTY() {
  static ConstString g_segment_name("__DATA_DIRTY");
  return g_segment_name;
}

ConstString ObjectFileMachO::GetSegmentNameDATA_CONST() {
  static ConstString g_segment_name("__DATA_CONST");
  return g_segment_name;
}

ConstString ObjectFileMachO::GetSegmentNameOBJC() {
  static ConstString g_segment_name_OBJC("__OBJC");
  return g_segment_name_OBJC;
}

ConstString ObjectFileMachO::GetSegmentNameLINKEDIT() {
  static ConstString g_section_name_LINKEDIT("__LINKEDIT");
  return g_section_name_LINKEDIT;
}

ConstString ObjectFileMachO::GetSegmentNameDWARF() {
  static ConstString g_section_name("__DWARF");
  return g_section_name;
}

ConstString ObjectFileMachO::GetSegmentNameLLVM_COV() {
  static ConstString g_section_name("__LLVM_COV");
  return g_section_name;
}

ConstString ObjectFileMachO::GetSectionNameEHFrame() {
  static ConstString g_section_name_eh_frame("__eh_frame");
  return g_section_name_eh_frame;
}

bool ObjectFileMachO::MagicBytesMatch(DataBufferSP data_sp,
                                      lldb::addr_t data_offset,
                                      lldb::addr_t data_length) {
  DataExtractor data;
  data.SetData(data_sp, data_offset, data_length);
  lldb::offset_t offset = 0;
  uint32_t magic = data.GetU32(&offset);

  offset += 4; // cputype
  offset += 4; // cpusubtype
  uint32_t filetype = data.GetU32(&offset);

  // A fileset has a Mach-O header but is not an
  // individual file and must be handled via an
  // ObjectContainer plugin.
  if (filetype == llvm::MachO::MH_FILESET)
    return false;

  return MachHeaderSizeFromMagic(magic) != 0;
}

ObjectFileMachO::ObjectFileMachO(const lldb::ModuleSP &module_sp,
                                 DataBufferSP data_sp,
                                 lldb::offset_t data_offset,
                                 const FileSpec *file,
                                 lldb::offset_t file_offset,
                                 lldb::offset_t length)
    : ObjectFile(module_sp, file, file_offset, length, data_sp, data_offset),
      m_mach_sections(), m_entry_point_address(), m_thread_context_offsets(),
      m_thread_context_offsets_valid(false), m_reexported_dylibs(),
      m_allow_assembly_emulation_unwind_plans(true) {
  ::memset(&m_header, 0, sizeof(m_header));
  ::memset(&m_dysymtab, 0, sizeof(m_dysymtab));
}

ObjectFileMachO::ObjectFileMachO(const lldb::ModuleSP &module_sp,
                                 lldb::WritableDataBufferSP header_data_sp,
                                 const lldb::ProcessSP &process_sp,
                                 lldb::addr_t header_addr)
    : ObjectFile(module_sp, process_sp, header_addr, header_data_sp),
      m_mach_sections(), m_entry_point_address(), m_thread_context_offsets(),
      m_thread_context_offsets_valid(false), m_reexported_dylibs(),
      m_allow_assembly_emulation_unwind_plans(true) {
  ::memset(&m_header, 0, sizeof(m_header));
  ::memset(&m_dysymtab, 0, sizeof(m_dysymtab));
}

bool ObjectFileMachO::ParseHeader(DataExtractor &data,
                                  lldb::offset_t *data_offset_ptr,
                                  llvm::MachO::mach_header &header) {
  data.SetByteOrder(endian::InlHostByteOrder());
  // Leave magic in the original byte order
  header.magic = data.GetU32(data_offset_ptr);
  bool can_parse = false;
  bool is_64_bit = false;
  switch (header.magic) {
  case MH_MAGIC:
    data.SetByteOrder(endian::InlHostByteOrder());
    data.SetAddressByteSize(4);
    can_parse = true;
    break;

  case MH_MAGIC_64:
    data.SetByteOrder(endian::InlHostByteOrder());
    data.SetAddressByteSize(8);
    can_parse = true;
    is_64_bit = true;
    break;

  case MH_CIGAM:
    data.SetByteOrder(endian::InlHostByteOrder() == eByteOrderBig
                          ? eByteOrderLittle
                          : eByteOrderBig);
    data.SetAddressByteSize(4);
    can_parse = true;
    break;

  case MH_CIGAM_64:
    data.SetByteOrder(endian::InlHostByteOrder() == eByteOrderBig
                          ? eByteOrderLittle
                          : eByteOrderBig);
    data.SetAddressByteSize(8);
    is_64_bit = true;
    can_parse = true;
    break;

  default:
    break;
  }

  if (can_parse) {
    data.GetU32(data_offset_ptr, &header.cputype, 6);
    if (is_64_bit)
      *data_offset_ptr += 4;
    return true;
  } else {
    memset(&header, 0, sizeof(header));
  }
  return false;
}

bool ObjectFileMachO::ParseHeader() {
  ModuleSP module_sp(GetModule());
  if (!module_sp)
    return false;

  std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
  bool can_parse = false;
  lldb::offset_t offset = 0;
  m_data.SetByteOrder(endian::InlHostByteOrder());
  // Leave magic in the original byte order
  m_header.magic = m_data.GetU32(&offset);
  switch (m_header.magic) {
  case MH_MAGIC:
    m_data.SetByteOrder(endian::InlHostByteOrder());
    m_data.SetAddressByteSize(4);
    can_parse = true;
    break;

  case MH_MAGIC_64:
    m_data.SetByteOrder(endian::InlHostByteOrder());
    m_data.SetAddressByteSize(8);
    can_parse = true;
    break;

  case MH_CIGAM:
    m_data.SetByteOrder(endian::InlHostByteOrder() == eByteOrderBig
                            ? eByteOrderLittle
                            : eByteOrderBig);
    m_data.SetAddressByteSize(4);
    can_parse = true;
    break;

  case MH_CIGAM_64:
    m_data.SetByteOrder(endian::InlHostByteOrder() == eByteOrderBig
                            ? eByteOrderLittle
                            : eByteOrderBig);
    m_data.SetAddressByteSize(8);
    can_parse = true;
    break;

  default:
    break;
  }

  if (can_parse) {
    m_data.GetU32(&offset, &m_header.cputype, 6);

    ModuleSpecList all_specs;
    ModuleSpec base_spec;
    GetAllArchSpecs(m_header, m_data, MachHeaderSizeFromMagic(m_header.magic),
                    base_spec, all_specs);

    for (unsigned i = 0, e = all_specs.GetSize(); i != e; ++i) {
      ArchSpec mach_arch =
          all_specs.GetModuleSpecRefAtIndex(i).GetArchitecture();

      // Check if the module has a required architecture
      const ArchSpec &module_arch = module_sp->GetArchitecture();
      if (module_arch.IsValid() && !module_arch.IsCompatibleMatch(mach_arch))
        continue;

      if (SetModulesArchitecture(mach_arch)) {
        const size_t header_and_lc_size =
            m_header.sizeofcmds + MachHeaderSizeFromMagic(m_header.magic);
        if (m_data.GetByteSize() < header_and_lc_size) {
          DataBufferSP data_sp;
          ProcessSP process_sp(m_process_wp.lock());
          if (process_sp) {
            data_sp = ReadMemory(process_sp, m_memory_addr, header_and_lc_size);
          } else {
            // Read in all only the load command data from the file on disk
            data_sp = MapFileData(m_file, header_and_lc_size, m_file_offset);
            if (data_sp->GetByteSize() != header_and_lc_size)
              continue;
          }
          if (data_sp)
            m_data.SetData(data_sp);
        }
      }
      return true;
    }
    // None found.
    return false;
  } else {
    memset(&m_header, 0, sizeof(struct llvm::MachO::mach_header));
  }
  return false;
}

ByteOrder ObjectFileMachO::GetByteOrder() const {
  return m_data.GetByteOrder();
}

bool ObjectFileMachO::IsExecutable() const {
  return m_header.filetype == MH_EXECUTE;
}

bool ObjectFileMachO::IsDynamicLoader() const {
  return m_header.filetype == MH_DYLINKER;
}

bool ObjectFileMachO::IsSharedCacheBinary() const {
  return m_header.flags & MH_DYLIB_IN_CACHE;
}

bool ObjectFileMachO::IsKext() const {
  return m_header.filetype == MH_KEXT_BUNDLE;
}

uint32_t ObjectFileMachO::GetAddressByteSize() const {
  return m_data.GetAddressByteSize();
}

AddressClass ObjectFileMachO::GetAddressClass(lldb::addr_t file_addr) {
  Symtab *symtab = GetSymtab();
  if (!symtab)
    return AddressClass::eUnknown;

  Symbol *symbol = symtab->FindSymbolContainingFileAddress(file_addr);
  if (symbol) {
    if (symbol->ValueIsAddress()) {
      SectionSP section_sp(symbol->GetAddressRef().GetSection());
      if (section_sp) {
        const lldb::SectionType section_type = section_sp->GetType();
        switch (section_type) {
        case eSectionTypeInvalid:
          return AddressClass::eUnknown;

        case eSectionTypeCode:
          if (m_header.cputype == llvm::MachO::CPU_TYPE_ARM) {
            // For ARM we have a bit in the n_desc field of the symbol that
            // tells us ARM/Thumb which is bit 0x0008.
            if (symbol->GetFlags() & MACHO_NLIST_ARM_SYMBOL_IS_THUMB)
              return AddressClass::eCodeAlternateISA;
          }
          return AddressClass::eCode;

        case eSectionTypeContainer:
          return AddressClass::eUnknown;

        case eSectionTypeData:
        case eSectionTypeDataCString:
        case eSectionTypeDataCStringPointers:
        case eSectionTypeDataSymbolAddress:
        case eSectionTypeData4:
        case eSectionTypeData8:
        case eSectionTypeData16:
        case eSectionTypeDataPointers:
        case eSectionTypeZeroFill:
        case eSectionTypeDataObjCMessageRefs:
        case eSectionTypeDataObjCCFStrings:
        case eSectionTypeGoSymtab:
          return AddressClass::eData;

        case eSectionTypeDebug:
        case eSectionTypeDWARFDebugAbbrev:
        case eSectionTypeDWARFDebugAbbrevDwo:
        case eSectionTypeDWARFDebugAddr:
        case eSectionTypeDWARFDebugAranges:
        case eSectionTypeDWARFDebugCuIndex:
        case eSectionTypeDWARFDebugFrame:
        case eSectionTypeDWARFDebugInfo:
        case eSectionTypeDWARFDebugInfoDwo:
        case eSectionTypeDWARFDebugLine:
        case eSectionTypeDWARFDebugLineStr:
        case eSectionTypeDWARFDebugLoc:
        case eSectionTypeDWARFDebugLocDwo:
        case eSectionTypeDWARFDebugLocLists:
        case eSectionTypeDWARFDebugLocListsDwo:
        case eSectionTypeDWARFDebugMacInfo:
        case eSectionTypeDWARFDebugMacro:
        case eSectionTypeDWARFDebugNames:
        case eSectionTypeDWARFDebugPubNames:
        case eSectionTypeDWARFDebugPubTypes:
        case eSectionTypeDWARFDebugRanges:
        case eSectionTypeDWARFDebugRngLists:
        case eSectionTypeDWARFDebugRngListsDwo:
        case eSectionTypeDWARFDebugStr:
        case eSectionTypeDWARFDebugStrDwo:
        case eSectionTypeDWARFDebugStrOffsets:
        case eSectionTypeDWARFDebugStrOffsetsDwo:
        case eSectionTypeDWARFDebugTuIndex:
        case eSectionTypeDWARFDebugTypes:
        case eSectionTypeDWARFDebugTypesDwo:
        case eSectionTypeDWARFAppleNames:
        case eSectionTypeDWARFAppleTypes:
        case eSectionTypeDWARFAppleNamespaces:
        case eSectionTypeDWARFAppleObjC:
        case eSectionTypeDWARFGNUDebugAltLink:
        case eSectionTypeCTF:
        case eSectionTypeSwiftModules:
          return AddressClass::eDebug;

        case eSectionTypeEHFrame:
        case eSectionTypeARMexidx:
        case eSectionTypeARMextab:
        case eSectionTypeCompactUnwind:
          return AddressClass::eRuntime;

        case eSectionTypeAbsoluteAddress:
        case eSectionTypeELFSymbolTable:
        case eSectionTypeELFDynamicSymbols:
        case eSectionTypeELFRelocationEntries:
        case eSectionTypeELFDynamicLinkInfo:
        case eSectionTypeOther:
          return AddressClass::eUnknown;
        }
      }
    }

    const SymbolType symbol_type = symbol->GetType();
    switch (symbol_type) {
    case eSymbolTypeAny:
      return AddressClass::eUnknown;
    case eSymbolTypeAbsolute:
      return AddressClass::eUnknown;

    case eSymbolTypeCode:
    case eSymbolTypeTrampoline:
    case eSymbolTypeResolver:
      if (m_header.cputype == llvm::MachO::CPU_TYPE_ARM) {
        // For ARM we have a bit in the n_desc field of the symbol that tells
        // us ARM/Thumb which is bit 0x0008.
        if (symbol->GetFlags() & MACHO_NLIST_ARM_SYMBOL_IS_THUMB)
          return AddressClass::eCodeAlternateISA;
      }
      return AddressClass::eCode;

    case eSymbolTypeData:
      return AddressClass::eData;
    case eSymbolTypeRuntime:
      return AddressClass::eRuntime;
    case eSymbolTypeException:
      return AddressClass::eRuntime;
    case eSymbolTypeSourceFile:
      return AddressClass::eDebug;
    case eSymbolTypeHeaderFile:
      return AddressClass::eDebug;
    case eSymbolTypeObjectFile:
      return AddressClass::eDebug;
    case eSymbolTypeCommonBlock:
      return AddressClass::eDebug;
    case eSymbolTypeBlock:
      return AddressClass::eDebug;
    case eSymbolTypeLocal:
      return AddressClass::eData;
    case eSymbolTypeParam:
      return AddressClass::eData;
    case eSymbolTypeVariable:
      return AddressClass::eData;
    case eSymbolTypeVariableType:
      return AddressClass::eDebug;
    case eSymbolTypeLineEntry:
      return AddressClass::eDebug;
    case eSymbolTypeLineHeader:
      return AddressClass::eDebug;
    case eSymbolTypeScopeBegin:
      return AddressClass::eDebug;
    case eSymbolTypeScopeEnd:
      return AddressClass::eDebug;
    case eSymbolTypeAdditional:
      return AddressClass::eUnknown;
    case eSymbolTypeCompiler:
      return AddressClass::eDebug;
    case eSymbolTypeInstrumentation:
      return AddressClass::eDebug;
    case eSymbolTypeUndefined:
      return AddressClass::eUnknown;
    case eSymbolTypeObjCClass:
      return AddressClass::eRuntime;
    case eSymbolTypeObjCMetaClass:
      return AddressClass::eRuntime;
    case eSymbolTypeObjCIVar:
      return AddressClass::eRuntime;
    case eSymbolTypeReExported:
      return AddressClass::eRuntime;
    }
  }
  return AddressClass::eUnknown;
}

bool ObjectFileMachO::IsStripped() {
  if (m_dysymtab.cmd == 0) {
    ModuleSP module_sp(GetModule());
    if (module_sp) {
      lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
      for (uint32_t i = 0; i < m_header.ncmds; ++i) {
        const lldb::offset_t load_cmd_offset = offset;

        llvm::MachO::load_command lc = {};
        if (m_data.GetU32(&offset, &lc.cmd, 2) == nullptr)
          break;
        if (lc.cmd == LC_DYSYMTAB) {
          m_dysymtab.cmd = lc.cmd;
          m_dysymtab.cmdsize = lc.cmdsize;
          if (m_data.GetU32(&offset, &m_dysymtab.ilocalsym,
                            (sizeof(m_dysymtab) / sizeof(uint32_t)) - 2) ==
              nullptr) {
            // Clear m_dysymtab if we were unable to read all items from the
            // load command
            ::memset(&m_dysymtab, 0, sizeof(m_dysymtab));
          }
        }
        offset = load_cmd_offset + lc.cmdsize;
      }
    }
  }
  if (m_dysymtab.cmd)
    return m_dysymtab.nlocalsym <= 1;
  return false;
}

ObjectFileMachO::EncryptedFileRanges ObjectFileMachO::GetEncryptedFileRanges() {
  EncryptedFileRanges result;
  lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);

  llvm::MachO::encryption_info_command encryption_cmd;
  for (uint32_t i = 0; i < m_header.ncmds; ++i) {
    const lldb::offset_t load_cmd_offset = offset;
    if (m_data.GetU32(&offset, &encryption_cmd, 2) == nullptr)
      break;

    // LC_ENCRYPTION_INFO and LC_ENCRYPTION_INFO_64 have the same sizes for the
    // 3 fields we care about, so treat them the same.
    if (encryption_cmd.cmd == LC_ENCRYPTION_INFO ||
        encryption_cmd.cmd == LC_ENCRYPTION_INFO_64) {
      if (m_data.GetU32(&offset, &encryption_cmd.cryptoff, 3)) {
        if (encryption_cmd.cryptid != 0) {
          EncryptedFileRanges::Entry entry;
          entry.SetRangeBase(encryption_cmd.cryptoff);
          entry.SetByteSize(encryption_cmd.cryptsize);
          result.Append(entry);
        }
      }
    }
    offset = load_cmd_offset + encryption_cmd.cmdsize;
  }

  return result;
}

void ObjectFileMachO::SanitizeSegmentCommand(
    llvm::MachO::segment_command_64 &seg_cmd, uint32_t cmd_idx) {
  if (m_length == 0 || seg_cmd.filesize == 0)
    return;

  if (IsSharedCacheBinary() && !IsInMemory()) {
    // In shared cache images, the load commands are relative to the
    // shared cache file, and not the specific image we are
    // examining. Let's fix this up so that it looks like a normal
    // image.
    if (strncmp(seg_cmd.segname, GetSegmentNameTEXT().GetCString(),
                sizeof(seg_cmd.segname)) == 0)
      m_text_address = seg_cmd.vmaddr;
    if (strncmp(seg_cmd.segname, GetSegmentNameLINKEDIT().GetCString(),
                sizeof(seg_cmd.segname)) == 0)
      m_linkedit_original_offset = seg_cmd.fileoff;

    seg_cmd.fileoff = seg_cmd.vmaddr - m_text_address;
  }

  if (seg_cmd.fileoff > m_length) {
    // We have a load command that says it extends past the end of the file.
    // This is likely a corrupt file.  We don't have any way to return an error
    // condition here (this method was likely invoked from something like
    // ObjectFile::GetSectionList()), so we just null out the section contents,
    // and dump a message to stdout.  The most common case here is core file
    // debugging with a truncated file.
    const char *lc_segment_name =
        seg_cmd.cmd == LC_SEGMENT_64 ? "LC_SEGMENT_64" : "LC_SEGMENT";
    GetModule()->ReportWarning(
        "load command {0} {1} has a fileoff ({2:x16}) that extends beyond "
        "the end of the file ({3:x16}), ignoring this section",
        cmd_idx, lc_segment_name, seg_cmd.fileoff, m_length);

    seg_cmd.fileoff = 0;
    seg_cmd.filesize = 0;
  }

  if (seg_cmd.fileoff + seg_cmd.filesize > m_length) {
    // We have a load command that says it extends past the end of the file.
    // This is likely a corrupt file.  We don't have any way to return an error
    // condition here (this method was likely invoked from something like
    // ObjectFile::GetSectionList()), so we just null out the section contents,
    // and dump a message to stdout.  The most common case here is core file
    // debugging with a truncated file.
    const char *lc_segment_name =
        seg_cmd.cmd == LC_SEGMENT_64 ? "LC_SEGMENT_64" : "LC_SEGMENT";
    GetModule()->ReportWarning(
        "load command {0} {1} has a fileoff + filesize ({2:x16}) that "
        "extends beyond the end of the file ({4:x16}), the segment will be "
        "truncated to match",
        cmd_idx, lc_segment_name, seg_cmd.fileoff + seg_cmd.filesize, m_length);

    // Truncate the length
    seg_cmd.filesize = m_length - seg_cmd.fileoff;
  }
}

static uint32_t
GetSegmentPermissions(const llvm::MachO::segment_command_64 &seg_cmd) {
  uint32_t result = 0;
  if (seg_cmd.initprot & VM_PROT_READ)
    result |= ePermissionsReadable;
  if (seg_cmd.initprot & VM_PROT_WRITE)
    result |= ePermissionsWritable;
  if (seg_cmd.initprot & VM_PROT_EXECUTE)
    result |= ePermissionsExecutable;
  return result;
}

static lldb::SectionType GetSectionType(uint32_t flags,
                                        ConstString section_name) {

  if (flags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS))
    return eSectionTypeCode;

  uint32_t mach_sect_type = flags & SECTION_TYPE;
  static ConstString g_sect_name_objc_data("__objc_data");
  static ConstString g_sect_name_objc_msgrefs("__objc_msgrefs");
  static ConstString g_sect_name_objc_selrefs("__objc_selrefs");
  static ConstString g_sect_name_objc_classrefs("__objc_classrefs");
  static ConstString g_sect_name_objc_superrefs("__objc_superrefs");
  static ConstString g_sect_name_objc_const("__objc_const");
  static ConstString g_sect_name_objc_classlist("__objc_classlist");
  static ConstString g_sect_name_cfstring("__cfstring");

  static ConstString g_sect_name_dwarf_debug_abbrev("__debug_abbrev");
  static ConstString g_sect_name_dwarf_debug_abbrev_dwo("__debug_abbrev.dwo");
  static ConstString g_sect_name_dwarf_debug_addr("__debug_addr");
  static ConstString g_sect_name_dwarf_debug_aranges("__debug_aranges");
  static ConstString g_sect_name_dwarf_debug_cu_index("__debug_cu_index");
  static ConstString g_sect_name_dwarf_debug_frame("__debug_frame");
  static ConstString g_sect_name_dwarf_debug_info("__debug_info");
  static ConstString g_sect_name_dwarf_debug_info_dwo("__debug_info.dwo");
  static ConstString g_sect_name_dwarf_debug_line("__debug_line");
  static ConstString g_sect_name_dwarf_debug_line_dwo("__debug_line.dwo");
  static ConstString g_sect_name_dwarf_debug_line_str("__debug_line_str");
  static ConstString g_sect_name_dwarf_debug_loc("__debug_loc");
  static ConstString g_sect_name_dwarf_debug_loclists("__debug_loclists");
  static ConstString g_sect_name_dwarf_debug_loclists_dwo("__debug_loclists.dwo");
  static ConstString g_sect_name_dwarf_debug_macinfo("__debug_macinfo");
  static ConstString g_sect_name_dwarf_debug_macro("__debug_macro");
  static ConstString g_sect_name_dwarf_debug_macro_dwo("__debug_macro.dwo");
  static ConstString g_sect_name_dwarf_debug_names("__debug_names");
  static ConstString g_sect_name_dwarf_debug_pubnames("__debug_pubnames");
  static ConstString g_sect_name_dwarf_debug_pubtypes("__debug_pubtypes");
  static ConstString g_sect_name_dwarf_debug_ranges("__debug_ranges");
  static ConstString g_sect_name_dwarf_debug_rnglists("__debug_rnglists");
  static ConstString g_sect_name_dwarf_debug_str("__debug_str");
  static ConstString g_sect_name_dwarf_debug_str_dwo("__debug_str.dwo");
  static ConstString g_sect_name_dwarf_debug_str_offs("__debug_str_offs");
  static ConstString g_sect_name_dwarf_debug_str_offs_dwo("__debug_str_offs.dwo");
  static ConstString g_sect_name_dwarf_debug_tu_index("__debug_tu_index");
  static ConstString g_sect_name_dwarf_debug_types("__debug_types");
  static ConstString g_sect_name_dwarf_apple_names("__apple_names");
  static ConstString g_sect_name_dwarf_apple_types("__apple_types");
  static ConstString g_sect_name_dwarf_apple_namespaces("__apple_namespac");
  static ConstString g_sect_name_dwarf_apple_objc("__apple_objc");
  static ConstString g_sect_name_eh_frame("__eh_frame");
  static ConstString g_sect_name_compact_unwind("__unwind_info");
  static ConstString g_sect_name_text("__text");
  static ConstString g_sect_name_data("__data");
  static ConstString g_sect_name_go_symtab("__gosymtab");
  static ConstString g_sect_name_ctf("__ctf");
  static ConstString g_sect_name_swift_ast("__swift_ast");

  if (section_name == g_sect_name_dwarf_debug_abbrev)
    return eSectionTypeDWARFDebugAbbrev;
  if (section_name == g_sect_name_dwarf_debug_abbrev_dwo)
    return eSectionTypeDWARFDebugAbbrevDwo;
  if (section_name == g_sect_name_dwarf_debug_addr)
    return eSectionTypeDWARFDebugAddr;
  if (section_name == g_sect_name_dwarf_debug_aranges)
    return eSectionTypeDWARFDebugAranges;
  if (section_name == g_sect_name_dwarf_debug_cu_index)
    return eSectionTypeDWARFDebugCuIndex;
  if (section_name == g_sect_name_dwarf_debug_frame)
    return eSectionTypeDWARFDebugFrame;
  if (section_name == g_sect_name_dwarf_debug_info)
    return eSectionTypeDWARFDebugInfo;
  if (section_name == g_sect_name_dwarf_debug_info_dwo)
    return eSectionTypeDWARFDebugInfoDwo;
  if (section_name == g_sect_name_dwarf_debug_line)
    return eSectionTypeDWARFDebugLine;
  if (section_name == g_sect_name_dwarf_debug_line_dwo)
    return eSectionTypeDWARFDebugLine; // Same as debug_line.
  if (section_name == g_sect_name_dwarf_debug_line_str)
    return eSectionTypeDWARFDebugLineStr;
  if (section_name == g_sect_name_dwarf_debug_loc)
    return eSectionTypeDWARFDebugLoc;
  if (section_name == g_sect_name_dwarf_debug_loclists)
    return eSectionTypeDWARFDebugLocLists;
  if (section_name == g_sect_name_dwarf_debug_loclists_dwo)
    return eSectionTypeDWARFDebugLocListsDwo;
  if (section_name == g_sect_name_dwarf_debug_macinfo)
    return eSectionTypeDWARFDebugMacInfo;
  if (section_name == g_sect_name_dwarf_debug_macro)
    return eSectionTypeDWARFDebugMacro;
  if (section_name == g_sect_name_dwarf_debug_macro_dwo)
    return eSectionTypeDWARFDebugMacInfo; // Same as debug_macro.
  if (section_name == g_sect_name_dwarf_debug_names)
    return eSectionTypeDWARFDebugNames;
  if (section_name == g_sect_name_dwarf_debug_pubnames)
    return eSectionTypeDWARFDebugPubNames;
  if (section_name == g_sect_name_dwarf_debug_pubtypes)
    return eSectionTypeDWARFDebugPubTypes;
  if (section_name == g_sect_name_dwarf_debug_ranges)
    return eSectionTypeDWARFDebugRanges;
  if (section_name == g_sect_name_dwarf_debug_rnglists)
    return eSectionTypeDWARFDebugRngLists;
  if (section_name == g_sect_name_dwarf_debug_str)
    return eSectionTypeDWARFDebugStr;
  if (section_name == g_sect_name_dwarf_debug_str_dwo)
    return eSectionTypeDWARFDebugStrDwo;
  if (section_name == g_sect_name_dwarf_debug_str_offs)
    return eSectionTypeDWARFDebugStrOffsets;
  if (section_name == g_sect_name_dwarf_debug_str_offs_dwo)
    return eSectionTypeDWARFDebugStrOffsetsDwo;
  if (section_name == g_sect_name_dwarf_debug_tu_index)
    return eSectionTypeDWARFDebugTuIndex;
  if (section_name == g_sect_name_dwarf_debug_types)
    return eSectionTypeDWARFDebugTypes;
  if (section_name == g_sect_name_dwarf_apple_names)
    return eSectionTypeDWARFAppleNames;
  if (section_name == g_sect_name_dwarf_apple_types)
    return eSectionTypeDWARFAppleTypes;
  if (section_name == g_sect_name_dwarf_apple_namespaces)
    return eSectionTypeDWARFAppleNamespaces;
  if (section_name == g_sect_name_dwarf_apple_objc)
    return eSectionTypeDWARFAppleObjC;
  if (section_name == g_sect_name_objc_selrefs)
    return eSectionTypeDataCStringPointers;
  if (section_name == g_sect_name_objc_msgrefs)
    return eSectionTypeDataObjCMessageRefs;
  if (section_name == g_sect_name_eh_frame)
    return eSectionTypeEHFrame;
  if (section_name == g_sect_name_compact_unwind)
    return eSectionTypeCompactUnwind;
  if (section_name == g_sect_name_cfstring)
    return eSectionTypeDataObjCCFStrings;
  if (section_name == g_sect_name_go_symtab)
    return eSectionTypeGoSymtab;
  if (section_name == g_sect_name_ctf)
    return eSectionTypeCTF;
  if (section_name == g_sect_name_swift_ast)
    return eSectionTypeSwiftModules;
  if (section_name == g_sect_name_objc_data ||
      section_name == g_sect_name_objc_classrefs ||
      section_name == g_sect_name_objc_superrefs ||
      section_name == g_sect_name_objc_const ||
      section_name == g_sect_name_objc_classlist) {
    return eSectionTypeDataPointers;
  }

  switch (mach_sect_type) {
  // TODO: categorize sections by other flags for regular sections
  case S_REGULAR:
    if (section_name == g_sect_name_text)
      return eSectionTypeCode;
    if (section_name == g_sect_name_data)
      return eSectionTypeData;
    return eSectionTypeOther;
  case S_ZEROFILL:
    return eSectionTypeZeroFill;
  case S_CSTRING_LITERALS: // section with only literal C strings
    return eSectionTypeDataCString;
  case S_4BYTE_LITERALS: // section with only 4 byte literals
    return eSectionTypeData4;
  case S_8BYTE_LITERALS: // section with only 8 byte literals
    return eSectionTypeData8;
  case S_LITERAL_POINTERS: // section with only pointers to literals
    return eSectionTypeDataPointers;
  case S_NON_LAZY_SYMBOL_POINTERS: // section with only non-lazy symbol pointers
    return eSectionTypeDataPointers;
  case S_LAZY_SYMBOL_POINTERS: // section with only lazy symbol pointers
    return eSectionTypeDataPointers;
  case S_SYMBOL_STUBS: // section with only symbol stubs, byte size of stub in
                       // the reserved2 field
    return eSectionTypeCode;
  case S_MOD_INIT_FUNC_POINTERS: // section with only function pointers for
                                 // initialization
    return eSectionTypeDataPointers;
  case S_MOD_TERM_FUNC_POINTERS: // section with only function pointers for
                                 // termination
    return eSectionTypeDataPointers;
  case S_COALESCED:
    return eSectionTypeOther;
  case S_GB_ZEROFILL:
    return eSectionTypeZeroFill;
  case S_INTERPOSING: // section with only pairs of function pointers for
                      // interposing
    return eSectionTypeCode;
  case S_16BYTE_LITERALS: // section with only 16 byte literals
    return eSectionTypeData16;
  case S_DTRACE_DOF:
    return eSectionTypeDebug;
  case S_LAZY_DYLIB_SYMBOL_POINTERS:
    return eSectionTypeDataPointers;
  default:
    return eSectionTypeOther;
  }
}

struct ObjectFileMachO::SegmentParsingContext {
  const EncryptedFileRanges EncryptedRanges;
  lldb_private::SectionList &UnifiedList;
  uint32_t NextSegmentIdx = 0;
  uint32_t NextSectionIdx = 0;
  bool FileAddressesChanged = false;

  SegmentParsingContext(EncryptedFileRanges EncryptedRanges,
                        lldb_private::SectionList &UnifiedList)
      : EncryptedRanges(std::move(EncryptedRanges)), UnifiedList(UnifiedList) {}
};

void ObjectFileMachO::ProcessSegmentCommand(
    const llvm::MachO::load_command &load_cmd_, lldb::offset_t offset,
    uint32_t cmd_idx, SegmentParsingContext &context) {
  llvm::MachO::segment_command_64 load_cmd;
  memcpy(&load_cmd, &load_cmd_, sizeof(load_cmd_));

  if (!m_data.GetU8(&offset, (uint8_t *)load_cmd.segname, 16))
    return;

  ModuleSP module_sp = GetModule();
  const bool is_core = GetType() == eTypeCoreFile;
  const bool is_dsym = (m_header.filetype == MH_DSYM);
  bool add_section = true;
  bool add_to_unified = true;
  ConstString const_segname(
      load_cmd.segname, strnlen(load_cmd.segname, sizeof(load_cmd.segname)));

  SectionSP unified_section_sp(
      context.UnifiedList.FindSectionByName(const_segname));
  if (is_dsym && unified_section_sp) {
    if (const_segname == GetSegmentNameLINKEDIT()) {
      // We need to keep the __LINKEDIT segment private to this object file
      // only
      add_to_unified = false;
    } else {
      // This is the dSYM file and this section has already been created by the
      // object file, no need to create it.
      add_section = false;
    }
  }
  load_cmd.vmaddr = m_data.GetAddress(&offset);
  load_cmd.vmsize = m_data.GetAddress(&offset);
  load_cmd.fileoff = m_data.GetAddress(&offset);
  load_cmd.filesize = m_data.GetAddress(&offset);
  if (!m_data.GetU32(&offset, &load_cmd.maxprot, 4))
    return;

  SanitizeSegmentCommand(load_cmd, cmd_idx);

  const uint32_t segment_permissions = GetSegmentPermissions(load_cmd);
  const bool segment_is_encrypted =
      (load_cmd.flags & SG_PROTECTED_VERSION_1) != 0;

  // Use a segment ID of the segment index shifted left by 8 so they never
  // conflict with any of the sections.
  SectionSP segment_sp;
  if (add_section && (const_segname || is_core)) {
    segment_sp = std::make_shared<Section>(
        module_sp, // Module to which this section belongs
        this,      // Object file to which this sections belongs
        ++context.NextSegmentIdx
            << 8, // Section ID is the 1 based segment index
        // shifted right by 8 bits as not to collide with any of the 256
        // section IDs that are possible
        const_segname,         // Name of this section
        eSectionTypeContainer, // This section is a container of other
        // sections.
        load_cmd.vmaddr, // File VM address == addresses as they are
        // found in the object file
        load_cmd.vmsize,  // VM size in bytes of this section
        load_cmd.fileoff, // Offset to the data for this section in
        // the file
        load_cmd.filesize, // Size in bytes of this section as found
        // in the file
        0,               // Segments have no alignment information
        load_cmd.flags); // Flags for this section

    segment_sp->SetIsEncrypted(segment_is_encrypted);
    m_sections_up->AddSection(segment_sp);
    segment_sp->SetPermissions(segment_permissions);
    if (add_to_unified)
      context.UnifiedList.AddSection(segment_sp);
  } else if (unified_section_sp) {
    // If this is a dSYM and the file addresses in the dSYM differ from the
    // file addresses in the ObjectFile, we must use the file base address for
    // the Section from the dSYM for the DWARF to resolve correctly.
    // This only happens with binaries in the shared cache in practice;
    // normally a mismatch like this would give a binary & dSYM that do not
    // match UUIDs. When a binary is included in the shared cache, its
    // segments are rearranged to optimize the shared cache, so its file
    // addresses will differ from what the ObjectFile had originally,
    // and what the dSYM has.
    if (is_dsym && unified_section_sp->GetFileAddress() != load_cmd.vmaddr) {
      Log *log = GetLog(LLDBLog::Symbols);
      if (log) {
        log->Printf(
            "Installing dSYM's %s segment file address over ObjectFile's "
            "so symbol table/debug info resolves correctly for %s",
            const_segname.AsCString(),
            module_sp->GetFileSpec().GetFilename().AsCString());
      }

      // Make sure we've parsed the symbol table from the ObjectFile before
      // we go around changing its Sections.
      module_sp->GetObjectFile()->GetSymtab();
      // eh_frame would present the same problems but we parse that on a per-
      // function basis as-needed so it's more difficult to remove its use of
      // the Sections.  Realistically, the environments where this code path
      // will be taken will not have eh_frame sections.

      unified_section_sp->SetFileAddress(load_cmd.vmaddr);

      // Notify the module that the section addresses have been changed once
      // we're done so any file-address caches can be updated.
      context.FileAddressesChanged = true;
    }
    m_sections_up->AddSection(unified_section_sp);
  }

  llvm::MachO::section_64 sect64;
  ::memset(&sect64, 0, sizeof(sect64));
  // Push a section into our mach sections for the section at index zero
  // (NO_SECT) if we don't have any mach sections yet...
  if (m_mach_sections.empty())
    m_mach_sections.push_back(sect64);
  uint32_t segment_sect_idx;
  const lldb::user_id_t first_segment_sectID = context.NextSectionIdx + 1;

  const uint32_t num_u32s = load_cmd.cmd == LC_SEGMENT ? 7 : 8;
  for (segment_sect_idx = 0; segment_sect_idx < load_cmd.nsects;
       ++segment_sect_idx) {
    if (m_data.GetU8(&offset, (uint8_t *)sect64.sectname,
                     sizeof(sect64.sectname)) == nullptr)
      break;
    if (m_data.GetU8(&offset, (uint8_t *)sect64.segname,
                     sizeof(sect64.segname)) == nullptr)
      break;
    sect64.addr = m_data.GetAddress(&offset);
    sect64.size = m_data.GetAddress(&offset);

    if (m_data.GetU32(&offset, &sect64.offset, num_u32s) == nullptr)
      break;

    if (IsSharedCacheBinary() && !IsInMemory()) {
      sect64.offset = sect64.addr - m_text_address;
    }

    // Keep a list of mach sections around in case we need to get at data that
    // isn't stored in the abstracted Sections.
    m_mach_sections.push_back(sect64);

    if (add_section) {
      ConstString section_name(
          sect64.sectname, strnlen(sect64.sectname, sizeof(sect64.sectname)));
      if (!const_segname) {
        // We have a segment with no name so we need to conjure up segments
        // that correspond to the section's segname if there isn't already such
        // a section. If there is such a section, we resize the section so that
        // it spans all sections.  We also mark these sections as fake so
        // address matches don't hit if they land in the gaps between the child
        // sections.
        const_segname.SetTrimmedCStringWithLength(sect64.segname,
                                                  sizeof(sect64.segname));
        segment_sp = context.UnifiedList.FindSectionByName(const_segname);
        if (segment_sp.get()) {
          Section *segment = segment_sp.get();
          // Grow the section size as needed.
          const lldb::addr_t sect64_min_addr = sect64.addr;
          const lldb::addr_t sect64_max_addr = sect64_min_addr + sect64.size;
          const lldb::addr_t curr_seg_byte_size = segment->GetByteSize();
          const lldb::addr_t curr_seg_min_addr = segment->GetFileAddress();
          const lldb::addr_t curr_seg_max_addr =
              curr_seg_min_addr + curr_seg_byte_size;
          if (sect64_min_addr >= curr_seg_min_addr) {
            const lldb::addr_t new_seg_byte_size =
                sect64_max_addr - curr_seg_min_addr;
            // Only grow the section size if needed
            if (new_seg_byte_size > curr_seg_byte_size)
              segment->SetByteSize(new_seg_byte_size);
          } else {
            // We need to change the base address of the segment and adjust the
            // child section offsets for all existing children.
            const lldb::addr_t slide_amount =
                sect64_min_addr - curr_seg_min_addr;
            segment->Slide(slide_amount, false);
            segment->GetChildren().Slide(-slide_amount, false);
            segment->SetByteSize(curr_seg_max_addr - sect64_min_addr);
          }

          // Grow the section size as needed.
          if (sect64.offset) {
            const lldb::addr_t segment_min_file_offset =
                segment->GetFileOffset();
            const lldb::addr_t segment_max_file_offset =
                segment_min_file_offset + segment->GetFileSize();

            const lldb::addr_t section_min_file_offset = sect64.offset;
            const lldb::addr_t section_max_file_offset =
                section_min_file_offset + sect64.size;
            const lldb::addr_t new_file_offset =
                std::min(section_min_file_offset, segment_min_file_offset);
            const lldb::addr_t new_file_size =
                std::max(section_max_file_offset, segment_max_file_offset) -
                new_file_offset;
            segment->SetFileOffset(new_file_offset);
            segment->SetFileSize(new_file_size);
          }
        } else {
          // Create a fake section for the section's named segment
          segment_sp = std::make_shared<Section>(
              segment_sp, // Parent section
              module_sp,  // Module to which this section belongs
              this,       // Object file to which this section belongs
              ++context.NextSegmentIdx
                  << 8, // Section ID is the 1 based segment index
              // shifted right by 8 bits as not to
              // collide with any of the 256 section IDs
              // that are possible
              const_segname,         // Name of this section
              eSectionTypeContainer, // This section is a container of
              // other sections.
              sect64.addr, // File VM address == addresses as they are
              // found in the object file
              sect64.size,   // VM size in bytes of this section
              sect64.offset, // Offset to the data for this section in
              // the file
              sect64.offset ? sect64.size : 0, // Size in bytes of
              // this section as
              // found in the file
              sect64.align,
              load_cmd.flags); // Flags for this section
          segment_sp->SetIsFake(true);
          segment_sp->SetPermissions(segment_permissions);
          m_sections_up->AddSection(segment_sp);
          if (add_to_unified)
            context.UnifiedList.AddSection(segment_sp);
          segment_sp->SetIsEncrypted(segment_is_encrypted);
        }
      }
      assert(segment_sp.get());

      lldb::SectionType sect_type = GetSectionType(sect64.flags, section_name);

      SectionSP section_sp(new Section(
          segment_sp, module_sp, this, ++context.NextSectionIdx, section_name,
          sect_type, sect64.addr - segment_sp->GetFileAddress(), sect64.size,
          sect64.offset, sect64.offset == 0 ? 0 : sect64.size, sect64.align,
          sect64.flags));
      // Set the section to be encrypted to match the segment

      bool section_is_encrypted = false;
      if (!segment_is_encrypted && load_cmd.filesize != 0)
        section_is_encrypted = context.EncryptedRanges.FindEntryThatContains(
                                   sect64.offset) != nullptr;

      section_sp->SetIsEncrypted(segment_is_encrypted || section_is_encrypted);
      section_sp->SetPermissions(segment_permissions);
      segment_sp->GetChildren().AddSection(section_sp);

      if (segment_sp->IsFake()) {
        segment_sp.reset();
        const_segname.Clear();
      }
    }
  }
  if (segment_sp && is_dsym) {
    if (first_segment_sectID <= context.NextSectionIdx) {
      lldb::user_id_t sect_uid;
      for (sect_uid = first_segment_sectID; sect_uid <= context.NextSectionIdx;
           ++sect_uid) {
        SectionSP curr_section_sp(
            segment_sp->GetChildren().FindSectionByID(sect_uid));
        SectionSP next_section_sp;
        if (sect_uid + 1 <= context.NextSectionIdx)
          next_section_sp =
              segment_sp->GetChildren().FindSectionByID(sect_uid + 1);

        if (curr_section_sp.get()) {
          if (curr_section_sp->GetByteSize() == 0) {
            if (next_section_sp.get() != nullptr)
              curr_section_sp->SetByteSize(next_section_sp->GetFileAddress() -
                                           curr_section_sp->GetFileAddress());
            else
              curr_section_sp->SetByteSize(load_cmd.vmsize);
          }
        }
      }
    }
  }
}

void ObjectFileMachO::ProcessDysymtabCommand(
    const llvm::MachO::load_command &load_cmd, lldb::offset_t offset) {
  m_dysymtab.cmd = load_cmd.cmd;
  m_dysymtab.cmdsize = load_cmd.cmdsize;
  m_data.GetU32(&offset, &m_dysymtab.ilocalsym,
                (sizeof(m_dysymtab) / sizeof(uint32_t)) - 2);
}

void ObjectFileMachO::CreateSections(SectionList &unified_section_list) {
  if (m_sections_up)
    return;

  m_sections_up = std::make_unique<SectionList>();

  lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
  // bool dump_sections = false;
  ModuleSP module_sp(GetModule());

  offset = MachHeaderSizeFromMagic(m_header.magic);

  SegmentParsingContext context(GetEncryptedFileRanges(), unified_section_list);
  llvm::MachO::load_command load_cmd;
  for (uint32_t i = 0; i < m_header.ncmds; ++i) {
    const lldb::offset_t load_cmd_offset = offset;
    if (m_data.GetU32(&offset, &load_cmd, 2) == nullptr)
      break;

    if (load_cmd.cmd == LC_SEGMENT || load_cmd.cmd == LC_SEGMENT_64)
      ProcessSegmentCommand(load_cmd, offset, i, context);
    else if (load_cmd.cmd == LC_DYSYMTAB)
      ProcessDysymtabCommand(load_cmd, offset);

    offset = load_cmd_offset + load_cmd.cmdsize;
  }

  if (context.FileAddressesChanged && module_sp)
    module_sp->SectionFileAddressesChanged();
}

class MachSymtabSectionInfo {
public:
  MachSymtabSectionInfo(SectionList *section_list)
      : m_section_list(section_list), m_section_infos() {
    // Get the number of sections down to a depth of 1 to include all segments
    // and their sections, but no other sections that may be added for debug
    // map or
    m_section_infos.resize(section_list->GetNumSections(1));
  }

  SectionSP GetSection(uint8_t n_sect, addr_t file_addr) {
    if (n_sect == 0)
      return SectionSP();
    if (n_sect < m_section_infos.size()) {
      if (!m_section_infos[n_sect].section_sp) {
        SectionSP section_sp(m_section_list->FindSectionByID(n_sect));
        m_section_infos[n_sect].section_sp = section_sp;
        if (section_sp) {
          m_section_infos[n_sect].vm_range.SetBaseAddress(
              section_sp->GetFileAddress());
          m_section_infos[n_sect].vm_range.SetByteSize(
              section_sp->GetByteSize());
        } else {
          std::string filename = "<unknown>";
          SectionSP first_section_sp(m_section_list->GetSectionAtIndex(0));
          if (first_section_sp)
            filename = first_section_sp->GetObjectFile()->GetFileSpec().GetPath();

          Debugger::ReportError(
              llvm::formatv("unable to find section {0} for a symbol in "
                            "{1}, corrupt file?",
                            n_sect, filename));
        }
      }
      if (m_section_infos[n_sect].vm_range.Contains(file_addr)) {
        // Symbol is in section.
        return m_section_infos[n_sect].section_sp;
      } else if (m_section_infos[n_sect].vm_range.GetByteSize() == 0 &&
                 m_section_infos[n_sect].vm_range.GetBaseAddress() ==
                     file_addr) {
        // Symbol is in section with zero size, but has the same start address
        // as the section. This can happen with linker symbols (symbols that
        // start with the letter 'l' or 'L'.
        return m_section_infos[n_sect].section_sp;
      }
    }
    return m_section_list->FindSectionContainingFileAddress(file_addr);
  }

protected:
  struct SectionInfo {
    SectionInfo() : vm_range(), section_sp() {}

    VMRange vm_range;
    SectionSP section_sp;
  };
  SectionList *m_section_list;
  std::vector<SectionInfo> m_section_infos;
};

#define TRIE_SYMBOL_IS_THUMB (1ULL << 63)
struct TrieEntry {
  void Dump() const {
    printf("0x%16.16llx 0x%16.16llx 0x%16.16llx \"%s\"",
           static_cast<unsigned long long>(address),
           static_cast<unsigned long long>(flags),
           static_cast<unsigned long long>(other), name.GetCString());
    if (import_name)
      printf(" -> \"%s\"\n", import_name.GetCString());
    else
      printf("\n");
  }
  ConstString name;
  uint64_t address = LLDB_INVALID_ADDRESS;
  uint64_t flags =
      0; // EXPORT_SYMBOL_FLAGS_REEXPORT, EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER,
         // TRIE_SYMBOL_IS_THUMB
  uint64_t other = 0;
  ConstString import_name;
};

struct TrieEntryWithOffset {
  lldb::offset_t nodeOffset;
  TrieEntry entry;

  TrieEntryWithOffset(lldb::offset_t offset) : nodeOffset(offset), entry() {}

  void Dump(uint32_t idx) const {
    printf("[%3u] 0x%16.16llx: ", idx,
           static_cast<unsigned long long>(nodeOffset));
    entry.Dump();
  }

  bool operator<(const TrieEntryWithOffset &other) const {
    return (nodeOffset < other.nodeOffset);
  }
};

static bool ParseTrieEntries(DataExtractor &data, lldb::offset_t offset,
                             const bool is_arm, addr_t text_seg_base_addr,
                             std::vector<llvm::StringRef> &nameSlices,
                             std::set<lldb::addr_t> &resolver_addresses,
                             std::vector<TrieEntryWithOffset> &reexports,
                             std::vector<TrieEntryWithOffset> &ext_symbols) {
  if (!data.ValidOffset(offset))
    return true;

  // Terminal node -- end of a branch, possibly add this to
  // the symbol table or resolver table.
  const uint64_t terminalSize = data.GetULEB128(&offset);
  lldb::offset_t children_offset = offset + terminalSize;
  if (terminalSize != 0) {
    TrieEntryWithOffset e(offset);
    e.entry.flags = data.GetULEB128(&offset);
    const char *import_name = nullptr;
    if (e.entry.flags & EXPORT_SYMBOL_FLAGS_REEXPORT) {
      e.entry.address = 0;
      e.entry.other = data.GetULEB128(&offset); // dylib ordinal
      import_name = data.GetCStr(&offset);
    } else {
      e.entry.address = data.GetULEB128(&offset);
      if (text_seg_base_addr != LLDB_INVALID_ADDRESS)
        e.entry.address += text_seg_base_addr;
      if (e.entry.flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) {
        e.entry.other = data.GetULEB128(&offset);
        uint64_t resolver_addr = e.entry.other;
        if (text_seg_base_addr != LLDB_INVALID_ADDRESS)
          resolver_addr += text_seg_base_addr;
        if (is_arm)
          resolver_addr &= THUMB_ADDRESS_BIT_MASK;
        resolver_addresses.insert(resolver_addr);
      } else
        e.entry.other = 0;
    }
    bool add_this_entry = false;
    if (Flags(e.entry.flags).Test(EXPORT_SYMBOL_FLAGS_REEXPORT) &&
        import_name && import_name[0]) {
      // add symbols that are reexport symbols with a valid import name.
      add_this_entry = true;
    } else if (e.entry.flags == 0 &&
               (import_name == nullptr || import_name[0] == '\0')) {
      // add externally visible symbols, in case the nlist record has
      // been stripped/omitted.
      add_this_entry = true;
    }
    if (add_this_entry) {
      std::string name;
      if (!nameSlices.empty()) {
        for (auto name_slice : nameSlices)
          name.append(name_slice.data(), name_slice.size());
      }
      if (name.size() > 1) {
        // Skip the leading '_'
        e.entry.name.SetCStringWithLength(name.c_str() + 1, name.size() - 1);
      }
      if (import_name) {
        // Skip the leading '_'
        e.entry.import_name.SetCString(import_name + 1);
      }
      if (Flags(e.entry.flags).Test(EXPORT_SYMBOL_FLAGS_REEXPORT)) {
        reexports.push_back(e);
      } else {
        if (is_arm && (e.entry.address & 1)) {
          e.entry.flags |= TRIE_SYMBOL_IS_THUMB;
          e.entry.address &= THUMB_ADDRESS_BIT_MASK;
        }
        ext_symbols.push_back(e);
      }
    }
  }

  const uint8_t childrenCount = data.GetU8(&children_offset);
  for (uint8_t i = 0; i < childrenCount; ++i) {
    const char *cstr = data.GetCStr(&children_offset);
    if (cstr)
      nameSlices.push_back(llvm::StringRef(cstr));
    else
      return false; // Corrupt data
    lldb::offset_t childNodeOffset = data.GetULEB128(&children_offset);
    if (childNodeOffset) {
      if (!ParseTrieEntries(data, childNodeOffset, is_arm, text_seg_base_addr,
                            nameSlices, resolver_addresses, reexports,
                            ext_symbols)) {
        return false;
      }
    }
    nameSlices.pop_back();
  }
  return true;
}

static SymbolType GetSymbolType(const char *&symbol_name,
                                bool &demangled_is_synthesized,
                                const SectionSP &text_section_sp,
                                const SectionSP &data_section_sp,
                                const SectionSP &data_dirty_section_sp,
                                const SectionSP &data_const_section_sp,
                                const SectionSP &symbol_section) {
  SymbolType type = eSymbolTypeInvalid;

  const char *symbol_sect_name = symbol_section->GetName().AsCString();
  if (symbol_section->IsDescendant(text_section_sp.get())) {
    if (symbol_section->IsClear(S_ATTR_PURE_INSTRUCTIONS |
                                S_ATTR_SELF_MODIFYING_CODE |
                                S_ATTR_SOME_INSTRUCTIONS))
      type = eSymbolTypeData;
    else
      type = eSymbolTypeCode;
  } else if (symbol_section->IsDescendant(data_section_sp.get()) ||
             symbol_section->IsDescendant(data_dirty_section_sp.get()) ||
             symbol_section->IsDescendant(data_const_section_sp.get())) {
    if (symbol_sect_name &&
        ::strstr(symbol_sect_name, "__objc") == symbol_sect_name) {
      type = eSymbolTypeRuntime;

      if (symbol_name) {
        llvm::StringRef symbol_name_ref(symbol_name);
        if (symbol_name_ref.starts_with("OBJC_")) {
          static const llvm::StringRef g_objc_v2_prefix_class("OBJC_CLASS_$_");
          static const llvm::StringRef g_objc_v2_prefix_metaclass(
              "OBJC_METACLASS_$_");
          static const llvm::StringRef g_objc_v2_prefix_ivar("OBJC_IVAR_$_");
          if (symbol_name_ref.starts_with(g_objc_v2_prefix_class)) {
            symbol_name = symbol_name + g_objc_v2_prefix_class.size();
            type = eSymbolTypeObjCClass;
            demangled_is_synthesized = true;
          } else if (symbol_name_ref.starts_with(g_objc_v2_prefix_metaclass)) {
            symbol_name = symbol_name + g_objc_v2_prefix_metaclass.size();
            type = eSymbolTypeObjCMetaClass;
            demangled_is_synthesized = true;
          } else if (symbol_name_ref.starts_with(g_objc_v2_prefix_ivar)) {
            symbol_name = symbol_name + g_objc_v2_prefix_ivar.size();
            type = eSymbolTypeObjCIVar;
            demangled_is_synthesized = true;
          }
        }
      }
    } else if (symbol_sect_name &&
               ::strstr(symbol_sect_name, "__gcc_except_tab") ==
                   symbol_sect_name) {
      type = eSymbolTypeException;
    } else {
      type = eSymbolTypeData;
    }
  } else if (symbol_sect_name &&
             ::strstr(symbol_sect_name, "__IMPORT") == symbol_sect_name) {
    type = eSymbolTypeTrampoline;
  }
  return type;
}

static std::optional<struct nlist_64>
ParseNList(DataExtractor &nlist_data, lldb::offset_t &nlist_data_offset,
           size_t nlist_byte_size) {
  struct nlist_64 nlist;
  if (!nlist_data.ValidOffsetForDataOfSize(nlist_data_offset, nlist_byte_size))
    return {};
  nlist.n_strx = nlist_data.GetU32_unchecked(&nlist_data_offset);
  nlist.n_type = nlist_data.GetU8_unchecked(&nlist_data_offset);
  nlist.n_sect = nlist_data.GetU8_unchecked(&nlist_data_offset);
  nlist.n_desc = nlist_data.GetU16_unchecked(&nlist_data_offset);
  nlist.n_value = nlist_data.GetAddress_unchecked(&nlist_data_offset);
  return nlist;
}

enum { DebugSymbols = true, NonDebugSymbols = false };

void ObjectFileMachO::ParseSymtab(Symtab &symtab) {
  ModuleSP module_sp(GetModule());
  if (!module_sp)
    return;

  Log *log = GetLog(LLDBLog::Symbols);

  const FileSpec &file = m_file ? m_file : module_sp->GetFileSpec();
  const char *file_name = file.GetFilename().AsCString("<Unknown>");
  LLDB_SCOPED_TIMERF("ObjectFileMachO::ParseSymtab () module = %s", file_name);
  LLDB_LOG(log, "Parsing symbol table for {0}", file_name);
  Progress progress("Parsing symbol table", file_name);

  llvm::MachO::symtab_command symtab_load_command = {0, 0, 0, 0, 0, 0};
  llvm::MachO::linkedit_data_command function_starts_load_command = {0, 0, 0, 0};
  llvm::MachO::linkedit_data_command exports_trie_load_command = {0, 0, 0, 0};
  llvm::MachO::dyld_info_command dyld_info = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  llvm::MachO::dysymtab_command dysymtab = m_dysymtab;
  // The data element of type bool indicates that this entry is thumb
  // code.
  typedef AddressDataArray<lldb::addr_t, bool, 100> FunctionStarts;

  // Record the address of every function/data that we add to the symtab.
  // We add symbols to the table in the order of most information (nlist
  // records) to least (function starts), and avoid duplicating symbols
  // via this set.
  llvm::DenseSet<addr_t> symbols_added;

  // We are using a llvm::DenseSet for "symbols_added" so we must be sure we
  // do not add the tombstone or empty keys to the set.
  auto add_symbol_addr = [&symbols_added](lldb::addr_t file_addr) {
    // Don't add the tombstone or empty keys.
    if (file_addr == UINT64_MAX || file_addr == UINT64_MAX - 1)
      return;
    symbols_added.insert(file_addr);
  };
  FunctionStarts function_starts;
  lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
  uint32_t i;
  FileSpecList dylib_files;
  llvm::StringRef g_objc_v2_prefix_class("_OBJC_CLASS_$_");
  llvm::StringRef g_objc_v2_prefix_metaclass("_OBJC_METACLASS_$_");
  llvm::StringRef g_objc_v2_prefix_ivar("_OBJC_IVAR_$_");
  UUID image_uuid;

  for (i = 0; i < m_header.ncmds; ++i) {
    const lldb::offset_t cmd_offset = offset;
    // Read in the load command and load command size
    llvm::MachO::load_command lc;
    if (m_data.GetU32(&offset, &lc, 2) == nullptr)
      break;
    // Watch for the symbol table load command
    switch (lc.cmd) {
    case LC_SYMTAB:
      symtab_load_command.cmd = lc.cmd;
      symtab_load_command.cmdsize = lc.cmdsize;
      // Read in the rest of the symtab load command
      if (m_data.GetU32(&offset, &symtab_load_command.symoff, 4) ==
          nullptr) // fill in symoff, nsyms, stroff, strsize fields
        return;
      break;

    case LC_DYLD_INFO:
    case LC_DYLD_INFO_ONLY:
      if (m_data.GetU32(&offset, &dyld_info.rebase_off, 10)) {
        dyld_info.cmd = lc.cmd;
        dyld_info.cmdsize = lc.cmdsize;
      } else {
        memset(&dyld_info, 0, sizeof(dyld_info));
      }
      break;

    case LC_LOAD_DYLIB:
    case LC_LOAD_WEAK_DYLIB:
    case LC_REEXPORT_DYLIB:
    case LC_LOADFVMLIB:
    case LC_LOAD_UPWARD_DYLIB: {
      uint32_t name_offset = cmd_offset + m_data.GetU32(&offset);
      const char *path = m_data.PeekCStr(name_offset);
      if (path) {
        FileSpec file_spec(path);
        // Strip the path if there is @rpath, @executable, etc so we just use
        // the basename
        if (path[0] == '@')
          file_spec.ClearDirectory();

        if (lc.cmd == LC_REEXPORT_DYLIB) {
          m_reexported_dylibs.AppendIfUnique(file_spec);
        }

        dylib_files.Append(file_spec);
      }
    } break;

    case LC_DYLD_EXPORTS_TRIE:
      exports_trie_load_command.cmd = lc.cmd;
      exports_trie_load_command.cmdsize = lc.cmdsize;
      if (m_data.GetU32(&offset, &exports_trie_load_command.dataoff, 2) ==
          nullptr) // fill in offset and size fields
        memset(&exports_trie_load_command, 0,
               sizeof(exports_trie_load_command));
      break;
    case LC_FUNCTION_STARTS:
      function_starts_load_command.cmd = lc.cmd;
      function_starts_load_command.cmdsize = lc.cmdsize;
      if (m_data.GetU32(&offset, &function_starts_load_command.dataoff, 2) ==
          nullptr) // fill in data offset and size fields
        memset(&function_starts_load_command, 0,
               sizeof(function_starts_load_command));
      break;

    case LC_UUID: {
      const uint8_t *uuid_bytes = m_data.PeekData(offset, 16);

      if (uuid_bytes)
        image_uuid = UUID(uuid_bytes, 16);
      break;
    }

    default:
      break;
    }
    offset = cmd_offset + lc.cmdsize;
  }

  if (!symtab_load_command.cmd)
    return;

  SectionList *section_list = GetSectionList();
  if (section_list == nullptr)
    return;

  const uint32_t addr_byte_size = m_data.GetAddressByteSize();
  const ByteOrder byte_order = m_data.GetByteOrder();
  bool bit_width_32 = addr_byte_size == 4;
  const size_t nlist_byte_size =
      bit_width_32 ? sizeof(struct nlist) : sizeof(struct nlist_64);

  DataExtractor nlist_data(nullptr, 0, byte_order, addr_byte_size);
  DataExtractor strtab_data(nullptr, 0, byte_order, addr_byte_size);
  DataExtractor function_starts_data(nullptr, 0, byte_order, addr_byte_size);
  DataExtractor indirect_symbol_index_data(nullptr, 0, byte_order,
                                           addr_byte_size);
  DataExtractor dyld_trie_data(nullptr, 0, byte_order, addr_byte_size);

  const addr_t nlist_data_byte_size =
      symtab_load_command.nsyms * nlist_byte_size;
  const addr_t strtab_data_byte_size = symtab_load_command.strsize;
  addr_t strtab_addr = LLDB_INVALID_ADDRESS;

  ProcessSP process_sp(m_process_wp.lock());
  Process *process = process_sp.get();

  uint32_t memory_module_load_level = eMemoryModuleLoadLevelComplete;
  bool is_shared_cache_image = IsSharedCacheBinary();
  bool is_local_shared_cache_image = is_shared_cache_image && !IsInMemory();
  SectionSP linkedit_section_sp(
      section_list->FindSectionByName(GetSegmentNameLINKEDIT()));

  if (process && m_header.filetype != llvm::MachO::MH_OBJECT &&
      !is_local_shared_cache_image) {
    Target &target = process->GetTarget();

    memory_module_load_level = target.GetMemoryModuleLoadLevel();

    // Reading mach file from memory in a process or core file...

    if (linkedit_section_sp) {
      addr_t linkedit_load_addr =
          linkedit_section_sp->GetLoadBaseAddress(&target);
      if (linkedit_load_addr == LLDB_INVALID_ADDRESS) {
        // We might be trying to access the symbol table before the
        // __LINKEDIT's load address has been set in the target. We can't
        // fail to read the symbol table, so calculate the right address
        // manually
        linkedit_load_addr = CalculateSectionLoadAddressForMemoryImage(
            m_memory_addr, GetMachHeaderSection(), linkedit_section_sp.get());
      }

      const addr_t linkedit_file_offset = linkedit_section_sp->GetFileOffset();
      const addr_t symoff_addr = linkedit_load_addr +
                                 symtab_load_command.symoff -
                                 linkedit_file_offset;
      strtab_addr = linkedit_load_addr + symtab_load_command.stroff -
                    linkedit_file_offset;

        // Always load dyld - the dynamic linker - from memory if we didn't
        // find a binary anywhere else. lldb will not register
        // dylib/framework/bundle loads/unloads if we don't have the dyld
        // symbols, we force dyld to load from memory despite the user's
        // target.memory-module-load-level setting.
        if (memory_module_load_level == eMemoryModuleLoadLevelComplete ||
            m_header.filetype == llvm::MachO::MH_DYLINKER) {
          DataBufferSP nlist_data_sp(
              ReadMemory(process_sp, symoff_addr, nlist_data_byte_size));
          if (nlist_data_sp)
            nlist_data.SetData(nlist_data_sp, 0, nlist_data_sp->GetByteSize());
          if (dysymtab.nindirectsyms != 0) {
            const addr_t indirect_syms_addr = linkedit_load_addr +
                                              dysymtab.indirectsymoff -
                                              linkedit_file_offset;
            DataBufferSP indirect_syms_data_sp(ReadMemory(
                process_sp, indirect_syms_addr, dysymtab.nindirectsyms * 4));
            if (indirect_syms_data_sp)
              indirect_symbol_index_data.SetData(
                  indirect_syms_data_sp, 0,
                  indirect_syms_data_sp->GetByteSize());
            // If this binary is outside the shared cache,
            // cache the string table.
            // Binaries in the shared cache all share a giant string table,
            // and we can't share the string tables across multiple
            // ObjectFileMachO's, so we'd end up re-reading this mega-strtab
            // for every binary in the shared cache - it would be a big perf
            // problem. For binaries outside the shared cache, it's faster to
            // read the entire strtab at once instead of piece-by-piece as we
            // process the nlist records.
            if (!is_shared_cache_image) {
              DataBufferSP strtab_data_sp(
                  ReadMemory(process_sp, strtab_addr, strtab_data_byte_size));
              if (strtab_data_sp) {
                strtab_data.SetData(strtab_data_sp, 0,
                                    strtab_data_sp->GetByteSize());
              }
            }
          }
        if (memory_module_load_level >= eMemoryModuleLoadLevelPartial) {
          if (function_starts_load_command.cmd) {
            const addr_t func_start_addr =
                linkedit_load_addr + function_starts_load_command.dataoff -
                linkedit_file_offset;
            DataBufferSP func_start_data_sp(
                ReadMemory(process_sp, func_start_addr,
                           function_starts_load_command.datasize));
            if (func_start_data_sp)
              function_starts_data.SetData(func_start_data_sp, 0,
                                           func_start_data_sp->GetByteSize());
          }
        }
      }
    }
  } else {
    if (is_local_shared_cache_image) {
      // The load commands in shared cache images are relative to the
      // beginning of the shared cache, not the library image. The
      // data we get handed when creating the ObjectFileMachO starts
      // at the beginning of a specific library and spans to the end
      // of the cache to be able to reach the shared LINKEDIT
      // segments. We need to convert the load command offsets to be
      // relative to the beginning of our specific image.
      lldb::addr_t linkedit_offset = linkedit_section_sp->GetFileOffset();
      lldb::offset_t linkedit_slide =
          linkedit_offset - m_linkedit_original_offset;
      symtab_load_command.symoff += linkedit_slide;
      symtab_load_command.stroff += linkedit_slide;
      dyld_info.export_off += linkedit_slide;
      dysymtab.indirectsymoff += linkedit_slide;
      function_starts_load_command.dataoff += linkedit_slide;
      exports_trie_load_command.dataoff += linkedit_slide;
    }

    nlist_data.SetData(m_data, symtab_load_command.symoff,
                       nlist_data_byte_size);
    strtab_data.SetData(m_data, symtab_load_command.stroff,
                        strtab_data_byte_size);

    // We shouldn't have exports data from both the LC_DYLD_INFO command
    // AND the LC_DYLD_EXPORTS_TRIE command in the same binary:
    lldbassert(!((dyld_info.export_size > 0)
                 && (exports_trie_load_command.datasize > 0)));
    if (dyld_info.export_size > 0) {
      dyld_trie_data.SetData(m_data, dyld_info.export_off,
                             dyld_info.export_size);
    } else if (exports_trie_load_command.datasize > 0) {
      dyld_trie_data.SetData(m_data, exports_trie_load_command.dataoff,
                             exports_trie_load_command.datasize);
    }

    if (dysymtab.nindirectsyms != 0) {
      indirect_symbol_index_data.SetData(m_data, dysymtab.indirectsymoff,
                                         dysymtab.nindirectsyms * 4);
    }
    if (function_starts_load_command.cmd) {
      function_starts_data.SetData(m_data, function_starts_load_command.dataoff,
                                   function_starts_load_command.datasize);
    }
  }

  const bool have_strtab_data = strtab_data.GetByteSize() > 0;

  ConstString g_segment_name_TEXT = GetSegmentNameTEXT();
  ConstString g_segment_name_DATA = GetSegmentNameDATA();
  ConstString g_segment_name_DATA_DIRTY = GetSegmentNameDATA_DIRTY();
  ConstString g_segment_name_DATA_CONST = GetSegmentNameDATA_CONST();
  ConstString g_segment_name_OBJC = GetSegmentNameOBJC();
  ConstString g_section_name_eh_frame = GetSectionNameEHFrame();
  SectionSP text_section_sp(
      section_list->FindSectionByName(g_segment_name_TEXT));
  SectionSP data_section_sp(
      section_list->FindSectionByName(g_segment_name_DATA));
  SectionSP data_dirty_section_sp(
      section_list->FindSectionByName(g_segment_name_DATA_DIRTY));
  SectionSP data_const_section_sp(
      section_list->FindSectionByName(g_segment_name_DATA_CONST));
  SectionSP objc_section_sp(
      section_list->FindSectionByName(g_segment_name_OBJC));
  SectionSP eh_frame_section_sp;
  if (text_section_sp.get())
    eh_frame_section_sp = text_section_sp->GetChildren().FindSectionByName(
        g_section_name_eh_frame);
  else
    eh_frame_section_sp =
        section_list->FindSectionByName(g_section_name_eh_frame);

  const bool is_arm = (m_header.cputype == llvm::MachO::CPU_TYPE_ARM);
  const bool always_thumb = GetArchitecture().IsAlwaysThumbInstructions();

  // lldb works best if it knows the start address of all functions in a
  // module. Linker symbols or debug info are normally the best source of
  // information for start addr / size but they may be stripped in a released
  // binary. Two additional sources of information exist in Mach-O binaries:
  //    LC_FUNCTION_STARTS - a list of ULEB128 encoded offsets of each
  //    function's start address in the
  //                         binary, relative to the text section.
  //    eh_frame           - the eh_frame FDEs have the start addr & size of
  //    each function
  //  LC_FUNCTION_STARTS is the fastest source to read in, and is present on
  //  all modern binaries.
  //  Binaries built to run on older releases may need to use eh_frame
  //  information.

  if (text_section_sp && function_starts_data.GetByteSize()) {
    FunctionStarts::Entry function_start_entry;
    function_start_entry.data = false;
    lldb::offset_t function_start_offset = 0;
    function_start_entry.addr = text_section_sp->GetFileAddress();
    uint64_t delta;
    while ((delta = function_starts_data.GetULEB128(&function_start_offset)) >
           0) {
      // Now append the current entry
      function_start_entry.addr += delta;
      if (is_arm) {
        if (function_start_entry.addr & 1) {
          function_start_entry.addr &= THUMB_ADDRESS_BIT_MASK;
          function_start_entry.data = true;
        } else if (always_thumb) {
          function_start_entry.data = true;
        }
      }
      function_starts.Append(function_start_entry);
    }
  } else {
    // If m_type is eTypeDebugInfo, then this is a dSYM - it will have the
    // load command claiming an eh_frame but it doesn't actually have the
    // eh_frame content.  And if we have a dSYM, we don't need to do any of
    // this fill-in-the-missing-symbols works anyway - the debug info should
    // give us all the functions in the module.
    if (text_section_sp.get() && eh_frame_section_sp.get() &&
        m_type != eTypeDebugInfo) {
      DWARFCallFrameInfo eh_frame(*this, eh_frame_section_sp,
                                  DWARFCallFrameInfo::EH);
      DWARFCallFrameInfo::FunctionAddressAndSizeVector functions;
      eh_frame.GetFunctionAddressAndSizeVector(functions);
      addr_t text_base_addr = text_section_sp->GetFileAddress();
      size_t count = functions.GetSize();
      for (size_t i = 0; i < count; ++i) {
        const DWARFCallFrameInfo::FunctionAddressAndSizeVector::Entry *func =
            functions.GetEntryAtIndex(i);
        if (func) {
          FunctionStarts::Entry function_start_entry;
          function_start_entry.addr = func->base - text_base_addr;
          if (is_arm) {
            if (function_start_entry.addr & 1) {
              function_start_entry.addr &= THUMB_ADDRESS_BIT_MASK;
              function_start_entry.data = true;
            } else if (always_thumb) {
              function_start_entry.data = true;
            }
          }
          function_starts.Append(function_start_entry);
        }
      }
    }
  }

  const size_t function_starts_count = function_starts.GetSize();

  // For user process binaries (executables, dylibs, frameworks, bundles), if
  // we don't have LC_FUNCTION_STARTS/eh_frame section in this binary, we're
  // going to assume the binary has been stripped.  Don't allow assembly
  // language instruction emulation because we don't know proper function
  // start boundaries.
  //
  // For all other types of binaries (kernels, stand-alone bare board
  // binaries, kexts), they may not have LC_FUNCTION_STARTS / eh_frame
  // sections - we should not make any assumptions about them based on that.
  if (function_starts_count == 0 && CalculateStrata() == eStrataUser) {
    m_allow_assembly_emulation_unwind_plans = false;
    Log *unwind_or_symbol_log(GetLog(LLDBLog::Symbols | LLDBLog::Unwind));

    if (unwind_or_symbol_log)
      module_sp->LogMessage(
          unwind_or_symbol_log,
          "no LC_FUNCTION_STARTS, will not allow assembly profiled unwinds");
  }

  const user_id_t TEXT_eh_frame_sectID = eh_frame_section_sp.get()
                                             ? eh_frame_section_sp->GetID()
                                             : static_cast<user_id_t>(NO_SECT);

  uint32_t N_SO_index = UINT32_MAX;

  MachSymtabSectionInfo section_info(section_list);
  std::vector<uint32_t> N_FUN_indexes;
  std::vector<uint32_t> N_NSYM_indexes;
  std::vector<uint32_t> N_INCL_indexes;
  std::vector<uint32_t> N_BRAC_indexes;
  std::vector<uint32_t> N_COMM_indexes;
  typedef std::multimap<uint64_t, uint32_t> ValueToSymbolIndexMap;
  typedef llvm::DenseMap<uint32_t, uint32_t> NListIndexToSymbolIndexMap;
  typedef llvm::DenseMap<const char *, uint32_t> ConstNameToSymbolIndexMap;
  ValueToSymbolIndexMap N_FUN_addr_to_sym_idx;
  ValueToSymbolIndexMap N_STSYM_addr_to_sym_idx;
  ConstNameToSymbolIndexMap N_GSYM_name_to_sym_idx;
  // Any symbols that get merged into another will get an entry in this map
  // so we know
  NListIndexToSymbolIndexMap m_nlist_idx_to_sym_idx;
  uint32_t nlist_idx = 0;
  Symbol *symbol_ptr = nullptr;

  uint32_t sym_idx = 0;
  Symbol *sym = nullptr;
  size_t num_syms = 0;
  std::string memory_symbol_name;
  uint32_t unmapped_local_symbols_found = 0;

  std::vector<TrieEntryWithOffset> reexport_trie_entries;
  std::vector<TrieEntryWithOffset> external_sym_trie_entries;
  std::set<lldb::addr_t> resolver_addresses;

  const size_t dyld_trie_data_size = dyld_trie_data.GetByteSize();
  if (dyld_trie_data_size > 0) {
    LLDB_LOG(log, "Parsing {0} bytes of dyld trie data", dyld_trie_data_size);
    SectionSP text_segment_sp =
        GetSectionList()->FindSectionByName(GetSegmentNameTEXT());
    lldb::addr_t text_segment_file_addr = LLDB_INVALID_ADDRESS;
    if (text_segment_sp)
      text_segment_file_addr = text_segment_sp->GetFileAddress();
    std::vector<llvm::StringRef> nameSlices;
    ParseTrieEntries(dyld_trie_data, 0, is_arm, text_segment_file_addr,
                     nameSlices, resolver_addresses, reexport_trie_entries,
                     external_sym_trie_entries);
  }

  typedef std::set<ConstString> IndirectSymbols;
  IndirectSymbols indirect_symbol_names;

#if TARGET_OS_IPHONE

  // Some recent builds of the dyld_shared_cache (hereafter: DSC) have been
  // optimized by moving LOCAL symbols out of the memory mapped portion of
  // the DSC. The symbol information has all been retained, but it isn't
  // available in the normal nlist data. However, there *are* duplicate
  // entries of *some*
  // LOCAL symbols in the normal nlist data. To handle this situation
  // correctly, we must first attempt
  // to parse any DSC unmapped symbol information. If we find any, we set a
  // flag that tells the normal nlist parser to ignore all LOCAL symbols.

  if (IsSharedCacheBinary()) {
    // Before we can start mapping the DSC, we need to make certain the
    // target process is actually using the cache we can find.

    // Next we need to determine the correct path for the dyld shared cache.

    ArchSpec header_arch = GetArchitecture();

    UUID dsc_uuid;
    UUID process_shared_cache_uuid;
    addr_t process_shared_cache_base_addr;

    if (process) {
      GetProcessSharedCacheUUID(process, process_shared_cache_base_addr,
                                process_shared_cache_uuid);
    }

    __block bool found_image = false;
    __block void *nlist_buffer = nullptr;
    __block unsigned nlist_count = 0;
    __block char *string_table = nullptr;
    __block vm_offset_t vm_nlist_memory = 0;
    __block mach_msg_type_number_t vm_nlist_bytes_read = 0;
    __block vm_offset_t vm_string_memory = 0;
    __block mach_msg_type_number_t vm_string_bytes_read = 0;

    auto _ = llvm::make_scope_exit(^{
      if (vm_nlist_memory)
        vm_deallocate(mach_task_self(), vm_nlist_memory, vm_nlist_bytes_read);
      if (vm_string_memory)
        vm_deallocate(mach_task_self(), vm_string_memory, vm_string_bytes_read);
    });

    typedef llvm::DenseMap<ConstString, uint16_t> UndefinedNameToDescMap;
    typedef llvm::DenseMap<uint32_t, ConstString> SymbolIndexToName;
    UndefinedNameToDescMap undefined_name_to_desc;
    SymbolIndexToName reexport_shlib_needs_fixup;

    dyld_for_each_installed_shared_cache(^(dyld_shared_cache_t shared_cache) {
      uuid_t cache_uuid;
      dyld_shared_cache_copy_uuid(shared_cache, &cache_uuid);
      if (found_image)
        return;

        if (process_shared_cache_uuid.IsValid() &&
          process_shared_cache_uuid != UUID::fromData(&cache_uuid, 16))
        return;

      dyld_shared_cache_for_each_image(shared_cache, ^(dyld_image_t image) {
        uuid_t dsc_image_uuid;
        if (found_image)
          return;

        dyld_image_copy_uuid(image, &dsc_image_uuid);
        if (image_uuid != UUID::fromData(dsc_image_uuid, 16))
          return;

        found_image = true;

        // Compute the size of the string table. We need to ask dyld for a
        // new SPI to avoid this step.
        dyld_image_local_nlist_content_4Symbolication(
            image, ^(const void *nlistStart, uint64_t nlistCount,
                     const char *stringTable) {
              if (!nlistStart || !nlistCount)
                return;

              // The buffers passed here are valid only inside the block.
              // Use vm_read to make a cheap copy of them available for our
              // processing later.
              kern_return_t ret =
                  vm_read(mach_task_self(), (vm_address_t)nlistStart,
                          nlist_byte_size * nlistCount, &vm_nlist_memory,
                          &vm_nlist_bytes_read);
              if (ret != KERN_SUCCESS)
                return;
              assert(vm_nlist_bytes_read == nlist_byte_size * nlistCount);

              // We don't know the size of the string table. It's cheaper
              // to map the whole VM region than to determine the size by
              // parsing all the nlist entries.
              vm_address_t string_address = (vm_address_t)stringTable;
              vm_size_t region_size;
              mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
              vm_region_basic_info_data_t info;
              memory_object_name_t object;
              ret = vm_region_64(mach_task_self(), &string_address,
                                 &region_size, VM_REGION_BASIC_INFO_64,
                                 (vm_region_info_t)&info, &info_count, &object);
              if (ret != KERN_SUCCESS)
                return;

              ret = vm_read(mach_task_self(), (vm_address_t)stringTable,
                            region_size -
                                ((vm_address_t)stringTable - string_address),
                            &vm_string_memory, &vm_string_bytes_read);
              if (ret != KERN_SUCCESS)
                return;

              nlist_buffer = (void *)vm_nlist_memory;
              string_table = (char *)vm_string_memory;
              nlist_count = nlistCount;
            });
      });
    });
    if (nlist_buffer) {
      DataExtractor dsc_local_symbols_data(nlist_buffer,
                                           nlist_count * nlist_byte_size,
                                           byte_order, addr_byte_size);
      unmapped_local_symbols_found = nlist_count;

                // The normal nlist code cannot correctly size the Symbols
                // array, we need to allocate it here.
                sym = symtab.Resize(
                    symtab_load_command.nsyms + m_dysymtab.nindirectsyms +
                    unmapped_local_symbols_found - m_dysymtab.nlocalsym);
                num_syms = symtab.GetNumSymbols();

      lldb::offset_t nlist_data_offset = 0;

                for (uint32_t nlist_index = 0;
                     nlist_index < nlist_count;
                     nlist_index++) {
                  /////////////////////////////
                  {
                    std::optional<struct nlist_64> nlist_maybe =
                        ParseNList(dsc_local_symbols_data, nlist_data_offset,
                                   nlist_byte_size);
                    if (!nlist_maybe)
                      break;
                    struct nlist_64 nlist = *nlist_maybe;

                    SymbolType type = eSymbolTypeInvalid;
          const char *symbol_name = string_table + nlist.n_strx;

                    if (symbol_name == NULL) {
                      // No symbol should be NULL, even the symbols with no
                      // string values should have an offset zero which
                      // points to an empty C-string
                      Debugger::ReportError(llvm::formatv(
                          "DSC unmapped local symbol[{0}] has invalid "
                          "string table offset {1:x} in {2}, ignoring symbol",
                          nlist_index, nlist.n_strx,
                          module_sp->GetFileSpec().GetPath());
                      continue;
                    }
                    if (symbol_name[0] == '\0')
                      symbol_name = NULL;

                    const char *symbol_name_non_abi_mangled = NULL;

                    SectionSP symbol_section;
                    uint32_t symbol_byte_size = 0;
                    bool add_nlist = true;
                    bool is_debug = ((nlist.n_type & N_STAB) != 0);
                    bool demangled_is_synthesized = false;
                    bool is_gsym = false;
                    bool set_value = true;

                    assert(sym_idx < num_syms);

                    sym[sym_idx].SetDebug(is_debug);

                    if (is_debug) {
                      switch (nlist.n_type) {
                      case N_GSYM:
                        // global symbol: name,,NO_SECT,type,0
                        // Sometimes the N_GSYM value contains the address.

                        // FIXME: In the .o files, we have a GSYM and a debug
                        // symbol for all the ObjC data.  They
                        // have the same address, but we want to ensure that
                        // we always find only the real symbol, 'cause we
                        // don't currently correctly attribute the
                        // GSYM one to the ObjCClass/Ivar/MetaClass
                        // symbol type.  This is a temporary hack to make
                        // sure the ObjectiveC symbols get treated correctly.
                        // To do this right, we should coalesce all the GSYM
                        // & global symbols that have the same address.

                        is_gsym = true;
                        sym[sym_idx].SetExternal(true);

                        if (symbol_name && symbol_name[0] == '_' &&
                            symbol_name[1] == 'O') {
                          llvm::StringRef symbol_name_ref(symbol_name);
                          if (symbol_name_ref.starts_with(
                                  g_objc_v2_prefix_class)) {
                            symbol_name_non_abi_mangled = symbol_name + 1;
                            symbol_name =
                                symbol_name + g_objc_v2_prefix_class.size();
                            type = eSymbolTypeObjCClass;
                            demangled_is_synthesized = true;

                          } else if (symbol_name_ref.starts_with(
                                         g_objc_v2_prefix_metaclass)) {
                            symbol_name_non_abi_mangled = symbol_name + 1;
                            symbol_name =
                                symbol_name + g_objc_v2_prefix_metaclass.size();
                            type = eSymbolTypeObjCMetaClass;
                            demangled_is_synthesized = true;
                          } else if (symbol_name_ref.starts_with(
                                         g_objc_v2_prefix_ivar)) {
                            symbol_name_non_abi_mangled = symbol_name + 1;
                            symbol_name =
                                symbol_name + g_objc_v2_prefix_ivar.size();
                            type = eSymbolTypeObjCIVar;
                            demangled_is_synthesized = true;
                          }
                        } else {
                          if (nlist.n_value != 0)
                            symbol_section = section_info.GetSection(
                                nlist.n_sect, nlist.n_value);
                          type = eSymbolTypeData;
                        }
                        break;

                      case N_FNAME:
                        // procedure name (f77 kludge): name,,NO_SECT,0,0
                        type = eSymbolTypeCompiler;
                        break;

                      case N_FUN:
                        // procedure: name,,n_sect,linenumber,address
                        if (symbol_name) {
                          type = eSymbolTypeCode;
                          symbol_section = section_info.GetSection(
                              nlist.n_sect, nlist.n_value);

                          N_FUN_addr_to_sym_idx.insert(
                              std::make_pair(nlist.n_value, sym_idx));
                          // We use the current number of symbols in the
                          // symbol table in lieu of using nlist_idx in case
                          // we ever start trimming entries out
                          N_FUN_indexes.push_back(sym_idx);
                        } else {
                          type = eSymbolTypeCompiler;

                          if (!N_FUN_indexes.empty()) {
                            // Copy the size of the function into the
                            // original
                            // STAB entry so we don't have
                            // to hunt for it later
                            symtab.SymbolAtIndex(N_FUN_indexes.back())
                                ->SetByteSize(nlist.n_value);
                            N_FUN_indexes.pop_back();
                            // We don't really need the end function STAB as
                            // it contains the size which we already placed
                            // with the original symbol, so don't add it if
                            // we want a minimal symbol table
                            add_nlist = false;
                          }
                        }
                        break;

                      case N_STSYM:
                        // static symbol: name,,n_sect,type,address
                        N_STSYM_addr_to_sym_idx.insert(
                            std::make_pair(nlist.n_value, sym_idx));
                        symbol_section = section_info.GetSection(nlist.n_sect,
                                                                 nlist.n_value);
                        if (symbol_name && symbol_name[0]) {
                          type = ObjectFile::GetSymbolTypeFromName(
                              symbol_name + 1, eSymbolTypeData);
                        }
                        break;

                      case N_LCSYM:
                        // .lcomm symbol: name,,n_sect,type,address
                        symbol_section = section_info.GetSection(nlist.n_sect,
                                                                 nlist.n_value);
                        type = eSymbolTypeCommonBlock;
                        break;

                      case N_BNSYM:
                        // We use the current number of symbols in the symbol
                        // table in lieu of using nlist_idx in case we ever
                        // start trimming entries out Skip these if we want
                        // minimal symbol tables
                        add_nlist = false;
                        break;

                      case N_ENSYM:
                        // Set the size of the N_BNSYM to the terminating
                        // index of this N_ENSYM so that we can always skip
                        // the entire symbol if we need to navigate more
                        // quickly at the source level when parsing STABS
                        // Skip these if we want minimal symbol tables
                        add_nlist = false;
                        break;

                      case N_OPT:
                        // emitted with gcc2_compiled and in gcc source
                        type = eSymbolTypeCompiler;
                        break;

                      case N_RSYM:
                        // register sym: name,,NO_SECT,type,register
                        type = eSymbolTypeVariable;
                        break;

                      case N_SLINE:
                        // src line: 0,,n_sect,linenumber,address
                        symbol_section = section_info.GetSection(nlist.n_sect,
                                                                 nlist.n_value);
                        type = eSymbolTypeLineEntry;
                        break;

                      case N_SSYM:
                        // structure elt: name,,NO_SECT,type,struct_offset
                        type = eSymbolTypeVariableType;
                        break;

                      case N_SO:
                        // source file name
                        type = eSymbolTypeSourceFile;
                        if (symbol_name == NULL) {
                          add_nlist = false;
                          if (N_SO_index != UINT32_MAX) {
                            // Set the size of the N_SO to the terminating
                            // index of this N_SO so that we can always skip
                            // the entire N_SO if we need to navigate more
                            // quickly at the source level when parsing STABS
                            symbol_ptr = symtab.SymbolAtIndex(N_SO_index);
                            symbol_ptr->SetByteSize(sym_idx);
                            symbol_ptr->SetSizeIsSibling(true);
                          }
                          N_NSYM_indexes.clear();
                          N_INCL_indexes.clear();
                          N_BRAC_indexes.clear();
                          N_COMM_indexes.clear();
                          N_FUN_indexes.clear();
                          N_SO_index = UINT32_MAX;
                        } else {
                          // We use the current number of symbols in the
                          // symbol table in lieu of using nlist_idx in case
                          // we ever start trimming entries out
                          const bool N_SO_has_full_path = symbol_name[0] == '/';
                          if (N_SO_has_full_path) {
                            if ((N_SO_index == sym_idx - 1) &&
                                ((sym_idx - 1) < num_syms)) {
                              // We have two consecutive N_SO entries where
                              // the first contains a directory and the
                              // second contains a full path.
                              sym[sym_idx - 1].GetMangled().SetValue(
                                  ConstString(symbol_name));
                              m_nlist_idx_to_sym_idx[nlist_idx] = sym_idx - 1;
                              add_nlist = false;
                            } else {
                              // This is the first entry in a N_SO that
                              // contains a directory or
                              // a full path to the source file
                              N_SO_index = sym_idx;
                            }
                          } else if ((N_SO_index == sym_idx - 1) &&
                                     ((sym_idx - 1) < num_syms)) {
                            // This is usually the second N_SO entry that
                            // contains just the filename, so here we combine
                            // it with the first one if we are minimizing the
                            // symbol table
                            const char *so_path = sym[sym_idx - 1]
                                                      .GetMangled()
                                                      .GetDemangledName()
                                                      .AsCString();
                            if (so_path && so_path[0]) {
                              std::string full_so_path(so_path);
                              const size_t double_slash_pos =
                                  full_so_path.find("//");
                              if (double_slash_pos != std::string::npos) {
                                // The linker has been generating bad N_SO
                                // entries with doubled up paths
                                // in the format "%s%s" where the first
                                // string in the DW_AT_comp_dir, and the
                                // second is the directory for the source
                                // file so you end up with a path that looks
                                // like "/tmp/src//tmp/src/"
                                FileSpec so_dir(so_path);
                                if (!FileSystem::Instance().Exists(so_dir)) {
                                  so_dir.SetFile(
                                      &full_so_path[double_slash_pos + 1],
                                      FileSpec::Style::native);
                                  if (FileSystem::Instance().Exists(so_dir)) {
                                    // Trim off the incorrect path
                                    full_so_path.erase(0, double_slash_pos + 1);
                                  }
                                }
                              }
                              if (*full_so_path.rbegin() != '/')
                                full_so_path += '/';
                              full_so_path += symbol_name;
                              sym[sym_idx - 1].GetMangled().SetValue(
                                  ConstString(full_so_path.c_str()));
                              add_nlist = false;
                              m_nlist_idx_to_sym_idx[nlist_idx] = sym_idx - 1;
                            }
                          } else {
                            // This could be a relative path to a N_SO
                            N_SO_index = sym_idx;
                          }
                        }
                        break;

                      case N_OSO:
                        // object file name: name,,0,0,st_mtime
                        type = eSymbolTypeObjectFile;
                        break;

                      case N_LSYM:
                        // local sym: name,,NO_SECT,type,offset
                        type = eSymbolTypeLocal;
                        break;

                      // INCL scopes
                      case N_BINCL:
                        // include file beginning: name,,NO_SECT,0,sum We use
                        // the current number of symbols in the symbol table
                        // in lieu of using nlist_idx in case we ever start
                        // trimming entries out
                        N_INCL_indexes.push_back(sym_idx);
                        type = eSymbolTypeScopeBegin;
                        break;

                      case N_EINCL:
                        // include file end: name,,NO_SECT,0,0
                        // Set the size of the N_BINCL to the terminating
                        // index of this N_EINCL so that we can always skip
                        // the entire symbol if we need to navigate more
                        // quickly at the source level when parsing STABS
                        if (!N_INCL_indexes.empty()) {
                          symbol_ptr =
                              symtab.SymbolAtIndex(N_INCL_indexes.back());
                          symbol_ptr->SetByteSize(sym_idx + 1);
                          symbol_ptr->SetSizeIsSibling(true);
                          N_INCL_indexes.pop_back();
                        }
                        type = eSymbolTypeScopeEnd;
                        break;

                      case N_SOL:
                        // #included file name: name,,n_sect,0,address
                        type = eSymbolTypeHeaderFile;

                        // We currently don't use the header files on darwin
                        add_nlist = false;
                        break;

                      case N_PARAMS:
                        // compiler parameters: name,,NO_SECT,0,0
                        type = eSymbolTypeCompiler;
                        break;

                      case N_VERSION:
                        // compiler version: name,,NO_SECT,0,0
                        type = eSymbolTypeCompiler;
                        break;

                      case N_OLEVEL:
                        // compiler -O level: name,,NO_SECT,0,0
                        type = eSymbolTypeCompiler;
                        break;

                      case N_PSYM:
                        // parameter: name,,NO_SECT,type,offset
                        type = eSymbolTypeVariable;
                        break;

                      case N_ENTRY:
                        // alternate entry: name,,n_sect,linenumber,address
                        symbol_section = section_info.GetSection(nlist.n_sect,
                                                                 nlist.n_value);
                        type = eSymbolTypeLineEntry;
                        break;

                      // Left and Right Braces
                      case N_LBRAC:
                        // left bracket: 0,,NO_SECT,nesting level,address We
                        // use the current number of symbols in the symbol
                        // table in lieu of using nlist_idx in case we ever
                        // start trimming entries out
                        symbol_section = section_info.GetSection(nlist.n_sect,
                                                                 nlist.n_value);
                        N_BRAC_indexes.push_back(sym_idx);
                        type = eSymbolTypeScopeBegin;
                        break;

                      case N_RBRAC:
                        // right bracket: 0,,NO_SECT,nesting level,address
                        // Set the size of the N_LBRAC to the terminating
                        // index of this N_RBRAC so that we can always skip
                        // the entire symbol if we need to navigate more
                        // quickly at the source level when parsing STABS
                        symbol_section = section_info.GetSection(nlist.n_sect,
                                                                 nlist.n_value);
                        if (!N_BRAC_indexes.empty()) {
                          symbol_ptr =
                              symtab.SymbolAtIndex(N_BRAC_indexes.back());
                          symbol_ptr->SetByteSize(sym_idx + 1);
                          symbol_ptr->SetSizeIsSibling(true);
                          N_BRAC_indexes.pop_back();
                        }
                        type = eSymbolTypeScopeEnd;
                        break;

                      case N_EXCL:
                        // deleted include file: name,,NO_SECT,0,sum
                        type = eSymbolTypeHeaderFile;
                        break;

                      // COMM scopes
                      case N_BCOMM:
                        // begin common: name,,NO_SECT,0,0
                        // We use the current number of symbols in the symbol
                        // table in lieu of using nlist_idx in case we ever
                        // start trimming entries out
                        type = eSymbolTypeScopeBegin;
                        N_COMM_indexes.push_back(sym_idx);
                        break;

                      case N_ECOML:
                        // end common (local name): 0,,n_sect,0,address
                        symbol_section = section_info.GetSection(nlist.n_sect,
                                                                 nlist.n_value);
                        // Fall through

                      case N_ECOMM:
                        // end common: name,,n_sect,0,0
                        // Set the size of the N_BCOMM to the terminating
                        // index of this N_ECOMM/N_ECOML so that we can
                        // always skip the entire symbol if we need to
                        // navigate more quickly at the source level when
                        // parsing STABS
                        if (!N_COMM_indexes.empty()) {
                          symbol_ptr =
                              symtab.SymbolAtIndex(N_COMM_indexes.back());
                          symbol_ptr->SetByteSize(sym_idx + 1);
                          symbol_ptr->SetSizeIsSibling(true);
                          N_COMM_indexes.pop_back();
                        }
                        type = eSymbolTypeScopeEnd;
                        break;

                      case N_LENG:
                        // second stab entry with length information
                        type = eSymbolTypeAdditional;
                        break;

                      default:
                        break;
                      }
                    } else {
                      // uint8_t n_pext    = N_PEXT & nlist.n_type;
                      uint8_t n_type = N_TYPE & nlist.n_type;
                      sym[sym_idx].SetExternal((N_EXT & nlist.n_type) != 0);

                      switch (n_type) {
                      case N_INDR: {
                        const char *reexport_name_cstr =
                            strtab_data.PeekCStr(nlist.n_value);
                        if (reexport_name_cstr && reexport_name_cstr[0]) {
                          type = eSymbolTypeReExported;
                          ConstString reexport_name(
                              reexport_name_cstr +
                              ((reexport_name_cstr[0] == '_') ? 1 : 0));
                          sym[sym_idx].SetReExportedSymbolName(reexport_name);
                          set_value = false;
                          reexport_shlib_needs_fixup[sym_idx] = reexport_name;
                          indirect_symbol_names.insert(ConstString(
                              symbol_name + ((symbol_name[0] == '_') ? 1 : 0)));
                        } else
                          type = eSymbolTypeUndefined;
                      } break;

                      case N_UNDF:
                        if (symbol_name && symbol_name[0]) {
                          ConstString undefined_name(
                              symbol_name + ((symbol_name[0] == '_') ? 1 : 0));
                          undefined_name_to_desc[undefined_name] = nlist.n_desc;
                        }
                      // Fall through
                      case N_PBUD:
                        type = eSymbolTypeUndefined;
                        break;

                      case N_ABS:
                        type = eSymbolTypeAbsolute;
                        break;

                      case N_SECT: {
                        symbol_section = section_info.GetSection(nlist.n_sect,
                                                                 nlist.n_value);

                        if (symbol_section == NULL) {
                          // TODO: warn about this?
                          add_nlist = false;
                          break;
                        }

                        if (TEXT_eh_frame_sectID == nlist.n_sect) {
                          type = eSymbolTypeException;
                        } else {
                          uint32_t section_type =
                              symbol_section->Get() & SECTION_TYPE;

                          switch (section_type) {
                          case S_CSTRING_LITERALS:
                            type = eSymbolTypeData;
                            break; // section with only literal C strings
                          case S_4BYTE_LITERALS:
                            type = eSymbolTypeData;
                            break; // section with only 4 byte literals
                          case S_8BYTE_LITERALS:
                            type = eSymbolTypeData;
                            break; // section with only 8 byte literals
                          case S_LITERAL_POINTERS:
                            type = eSymbolTypeTrampoline;
                            break; // section with only pointers to literals
                          case S_NON_LAZY_SYMBOL_POINTERS:
                            type = eSymbolTypeTrampoline;
                            break; // section with only non-lazy symbol
                                   // pointers
                          case S_LAZY_SYMBOL_POINTERS:
                            type = eSymbolTypeTrampoline;
                            break; // section with only lazy symbol pointers
                          case S_SYMBOL_STUBS:
                            type = eSymbolTypeTrampoline;
                            break; // section with only symbol stubs, byte
                                   // size of stub in the reserved2 field
                          case S_MOD_INIT_FUNC_POINTERS:
                            type = eSymbolTypeCode;
                            break; // section with only function pointers for
                                   // initialization
                          case S_MOD_TERM_FUNC_POINTERS:
                            type = eSymbolTypeCode;
                            break; // section with only function pointers for
                                   // termination
                          case S_INTERPOSING:
                            type = eSymbolTypeTrampoline;
                            break; // section with only pairs of function
                                   // pointers for interposing
                          case S_16BYTE_LITERALS:
                            type = eSymbolTypeData;
                            break; // section with only 16 byte literals
                          case S_DTRACE_DOF:
                            type = eSymbolTypeInstrumentation;
                            break;
                          case S_LAZY_DYLIB_SYMBOL_POINTERS:
                            type = eSymbolTypeTrampoline;
                            break;
                          default:
                            switch (symbol_section->GetType()) {
                            case lldb::eSectionTypeCode:
                              type = eSymbolTypeCode;
                              break;
                            case eSectionTypeData:
                            case eSectionTypeDataCString: // Inlined C string
                                                          // data
                            case eSectionTypeDataCStringPointers: // Pointers
                                                                  // to C
                                                                  // string
                                                                  // data
                            case eSectionTypeDataSymbolAddress:   // Address of
                                                                  // a symbol in
                                                                  // the symbol
                                                                  // table
                            case eSectionTypeData4:
                            case eSectionTypeData8:
                            case eSectionTypeData16:
                              type = eSymbolTypeData;
                              break;
                            default:
                              break;
                            }
                            break;
                          }

                          if (type == eSymbolTypeInvalid) {
                            const char *symbol_sect_name =
                                symbol_section->GetName().AsCString();
                            if (symbol_section->IsDescendant(
                                    text_section_sp.get())) {
                              if (symbol_section->IsClear(
                                      S_ATTR_PURE_INSTRUCTIONS |
                                      S_ATTR_SELF_MODIFYING_CODE |
                                      S_ATTR_SOME_INSTRUCTIONS))
                                type = eSymbolTypeData;
                              else
                                type = eSymbolTypeCode;
                            } else if (symbol_section->IsDescendant(
                                           data_section_sp.get()) ||
                                       symbol_section->IsDescendant(
                                           data_dirty_section_sp.get()) ||
                                       symbol_section->IsDescendant(
                                           data_const_section_sp.get())) {
                              if (symbol_sect_name &&
                                  ::strstr(symbol_sect_name, "__objc") ==
                                      symbol_sect_name) {
                                type = eSymbolTypeRuntime;

                                if (symbol_name) {
                                  llvm::StringRef symbol_name_ref(symbol_name);
                                  if (symbol_name_ref.starts_with("_OBJC_")) {
                                    llvm::StringRef
                                        g_objc_v2_prefix_class(
                                            "_OBJC_CLASS_$_");
                                    llvm::StringRef
                                        g_objc_v2_prefix_metaclass(
                                            "_OBJC_METACLASS_$_");
                                    llvm::StringRef
                                        g_objc_v2_prefix_ivar("_OBJC_IVAR_$_");
                                    if (symbol_name_ref.starts_with(
                                            g_objc_v2_prefix_class)) {
                                      symbol_name_non_abi_mangled =
                                          symbol_name + 1;
                                      symbol_name =
                                          symbol_name +
                                          g_objc_v2_prefix_class.size();
                                      type = eSymbolTypeObjCClass;
                                      demangled_is_synthesized = true;
                                    } else if (
                                        symbol_name_ref.starts_with(
                                            g_objc_v2_prefix_metaclass)) {
                                      symbol_name_non_abi_mangled =
                                          symbol_name + 1;
                                      symbol_name =
                                          symbol_name +
                                          g_objc_v2_prefix_metaclass.size();
                                      type = eSymbolTypeObjCMetaClass;
                                      demangled_is_synthesized = true;
                                    } else if (symbol_name_ref.starts_with(
                                                   g_objc_v2_prefix_ivar)) {
                                      symbol_name_non_abi_mangled =
                                          symbol_name + 1;
                                      symbol_name =
                                          symbol_name +
                                          g_objc_v2_prefix_ivar.size();
                                      type = eSymbolTypeObjCIVar;
                                      demangled_is_synthesized = true;
                                    }
                                  }
                                }
                              } else if (symbol_sect_name &&
                                         ::strstr(symbol_sect_name,
                                                  "__gcc_except_tab") ==
                                             symbol_sect_name) {
                                type = eSymbolTypeException;
                              } else {
                                type = eSymbolTypeData;
                              }
                            } else if (symbol_sect_name &&
                                       ::strstr(symbol_sect_name, "__IMPORT") ==
                                           symbol_sect_name) {
                              type = eSymbolTypeTrampoline;
                            } else if (symbol_section->IsDescendant(
                                           objc_section_sp.get())) {
                              type = eSymbolTypeRuntime;
                              if (symbol_name && symbol_name[0] == '.') {
                                llvm::StringRef symbol_name_ref(symbol_name);
                                llvm::StringRef
                                    g_objc_v1_prefix_class(".objc_class_name_");
                                if (symbol_name_ref.starts_with(
                                        g_objc_v1_prefix_class)) {
                                  symbol_name_non_abi_mangled = symbol_name;
                                  symbol_name = symbol_name +
                                                g_objc_v1_prefix_class.size();
                                  type = eSymbolTypeObjCClass;
                                  demangled_is_synthesized = true;
                                }
                              }
                            }
                          }
                        }
                      } break;
                      }
                    }

                    if (add_nlist) {
                      uint64_t symbol_value = nlist.n_value;
                      if (symbol_name_non_abi_mangled) {
                        sym[sym_idx].GetMangled().SetMangledName(
                            ConstString(symbol_name_non_abi_mangled));
                        sym[sym_idx].GetMangled().SetDemangledName(
                            ConstString(symbol_name));
                      } else {
                        if (symbol_name && symbol_name[0] == '_') {
                          symbol_name++; // Skip the leading underscore
                        }

                        if (symbol_name) {
                          ConstString const_symbol_name(symbol_name);
                          sym[sym_idx].GetMangled().SetValue(const_symbol_name);
                          if (is_gsym && is_debug) {
                            const char *gsym_name =
                                sym[sym_idx]
                                    .GetMangled()
                                    .GetName(Mangled::ePreferMangled)
                                    .GetCString();
                            if (gsym_name)
                              N_GSYM_name_to_sym_idx[gsym_name] = sym_idx;
                          }
                        }
                      }
                      if (symbol_section) {
                        const addr_t section_file_addr =
                            symbol_section->GetFileAddress();
                        if (symbol_byte_size == 0 &&
                            function_starts_count > 0) {
                          addr_t symbol_lookup_file_addr = nlist.n_value;
                          // Do an exact address match for non-ARM addresses,
                          // else get the closest since the symbol might be a
                          // thumb symbol which has an address with bit zero
                          // set
                          FunctionStarts::Entry *func_start_entry =
                              function_starts.FindEntry(symbol_lookup_file_addr,
                                                        !is_arm);
                          if (is_arm && func_start_entry) {
                            // Verify that the function start address is the
                            // symbol address (ARM) or the symbol address + 1
                            // (thumb)
                            if (func_start_entry->addr !=
                                    symbol_lookup_file_addr &&
                                func_start_entry->addr !=
                                    (symbol_lookup_file_addr + 1)) {
                              // Not the right entry, NULL it out...
                              func_start_entry = NULL;
                            }
                          }
                          if (func_start_entry) {
                            func_start_entry->data = true;

                            addr_t symbol_file_addr = func_start_entry->addr;
                            uint32_t symbol_flags = 0;
                            if (is_arm) {
                              if (symbol_file_addr & 1)
                                symbol_flags = MACHO_NLIST_ARM_SYMBOL_IS_THUMB;
                              symbol_file_addr &= THUMB_ADDRESS_BIT_MASK;
                            }

                            const FunctionStarts::Entry *next_func_start_entry =
                                function_starts.FindNextEntry(func_start_entry);
                            const addr_t section_end_file_addr =
                                section_file_addr +
                                symbol_section->GetByteSize();
                            if (next_func_start_entry) {
                              addr_t next_symbol_file_addr =
                                  next_func_start_entry->addr;
                              // Be sure the clear the Thumb address bit when
                              // we calculate the size from the current and
                              // next address
                              if (is_arm)
                                next_symbol_file_addr &= THUMB_ADDRESS_BIT_MASK;
                              symbol_byte_size = std::min<lldb::addr_t>(
                                  next_symbol_file_addr - symbol_file_addr,
                                  section_end_file_addr - symbol_file_addr);
                            } else {
                              symbol_byte_size =
                                  section_end_file_addr - symbol_file_addr;
                            }
                          }
                        }
                        symbol_value -= section_file_addr;
                      }

                      if (is_debug == false) {
                        if (type == eSymbolTypeCode) {
                          // See if we can find a N_FUN entry for any code
                          // symbols. If we do find a match, and the name
                          // matches, then we can merge the two into just the
                          // function symbol to avoid duplicate entries in
                          // the symbol table
                          auto range =
                              N_FUN_addr_to_sym_idx.equal_range(nlist.n_value);
                          if (range.first != range.second) {
                            bool found_it = false;
                            for (auto pos = range.first; pos != range.second;
                                 ++pos) {
                              if (sym[sym_idx].GetMangled().GetName(
                                      Mangled::ePreferMangled) ==
                                  sym[pos->second].GetMangled().GetName(
                                      Mangled::ePreferMangled)) {
                                m_nlist_idx_to_sym_idx[nlist_idx] = pos->second;
                                // We just need the flags from the linker
                                // symbol, so put these flags
                                // into the N_FUN flags to avoid duplicate
                                // symbols in the symbol table
                                sym[pos->second].SetExternal(
                                    sym[sym_idx].IsExternal());
                                sym[pos->second].SetFlags(nlist.n_type << 16 |
                                                          nlist.n_desc);
                                if (resolver_addresses.find(nlist.n_value) !=
                                    resolver_addresses.end())
                                  sym[pos->second].SetType(eSymbolTypeResolver);
                                sym[sym_idx].Clear();
                                found_it = true;
                                break;
                              }
                            }
                            if (found_it)
                              continue;
                          } else {
                            if (resolver_addresses.find(nlist.n_value) !=
                                resolver_addresses.end())
                              type = eSymbolTypeResolver;
                          }
                        } else if (type == eSymbolTypeData ||
                                   type == eSymbolTypeObjCClass ||
                                   type == eSymbolTypeObjCMetaClass ||
                                   type == eSymbolTypeObjCIVar) {
                          // See if we can find a N_STSYM entry for any data
                          // symbols. If we do find a match, and the name
                          // matches, then we can merge the two into just the
                          // Static symbol to avoid duplicate entries in the
                          // symbol table
                          auto range = N_STSYM_addr_to_sym_idx.equal_range(
                              nlist.n_value);
                          if (range.first != range.second) {
                            bool found_it = false;
                            for (auto pos = range.first; pos != range.second;
                                 ++pos) {
                              if (sym[sym_idx].GetMangled().GetName(
                                      Mangled::ePreferMangled) ==
                                  sym[pos->second].GetMangled().GetName(
                                      Mangled::ePreferMangled)) {
                                m_nlist_idx_to_sym_idx[nlist_idx] = pos->second;
                                // We just need the flags from the linker
                                // symbol, so put these flags
                                // into the N_STSYM flags to avoid duplicate
                                // symbols in the symbol table
                                sym[pos->second].SetExternal(
                                    sym[sym_idx].IsExternal());
                                sym[pos->second].SetFlags(nlist.n_type << 16 |
                                                          nlist.n_desc);
                                sym[sym_idx].Clear();
                                found_it = true;
                                break;
                              }
                            }
                            if (found_it)
                              continue;
                          } else {
                            const char *gsym_name =
                                sym[sym_idx]
                                    .GetMangled()
                                    .GetName(Mangled::ePreferMangled)
                                    .GetCString();
                            if (gsym_name) {
                              // Combine N_GSYM stab entries with the non
                              // stab symbol
                              ConstNameToSymbolIndexMap::const_iterator pos =
                                  N_GSYM_name_to_sym_idx.find(gsym_name);
                              if (pos != N_GSYM_name_to_sym_idx.end()) {
                                const uint32_t GSYM_sym_idx = pos->second;
                                m_nlist_idx_to_sym_idx[nlist_idx] =
                                    GSYM_sym_idx;
                                // Copy the address, because often the N_GSYM
                                // address has an invalid address of zero
                                // when the global is a common symbol
                                sym[GSYM_sym_idx].GetAddressRef().SetSection(
                                    symbol_section);
                                sym[GSYM_sym_idx].GetAddressRef().SetOffset(
                                    symbol_value);
                                add_symbol_addr(sym[GSYM_sym_idx]
                                                    .GetAddress()
                                                    .GetFileAddress());
                                // We just need the flags from the linker
                                // symbol, so put these flags
                                // into the N_GSYM flags to avoid duplicate
                                // symbols in the symbol table
                                sym[GSYM_sym_idx].SetFlags(nlist.n_type << 16 |
                                                           nlist.n_desc);
                                sym[sym_idx].Clear();
                                continue;
                              }
                            }
                          }
                        }
                      }

                      sym[sym_idx].SetID(nlist_idx);
                      sym[sym_idx].SetType(type);
                      if (set_value) {
                        sym[sym_idx].GetAddressRef().SetSection(symbol_section);
                        sym[sym_idx].GetAddressRef().SetOffset(symbol_value);
                        add_symbol_addr(
                            sym[sym_idx].GetAddress().GetFileAddress());
                      }
                      sym[sym_idx].SetFlags(nlist.n_type << 16 | nlist.n_desc);

                      if (symbol_byte_size > 0)
                        sym[sym_idx].SetByteSize(symbol_byte_size);

                      if (demangled_is_synthesized)
                        sym[sym_idx].SetDemangledNameIsSynthesized(true);
                      ++sym_idx;
                    } else {
                      sym[sym_idx].Clear();
                    }
                  }
                  /////////////////////////////
                }
            }

            for (const auto &pos : reexport_shlib_needs_fixup) {
              const auto undef_pos = undefined_name_to_desc.find(pos.second);
              if (undef_pos != undefined_name_to_desc.end()) {
                const uint8_t dylib_ordinal =
                    llvm::MachO::GET_LIBRARY_ORDINAL(undef_pos->second);
                if (dylib_ordinal > 0 && dylib_ordinal < dylib_files.GetSize())
                  sym[pos.first].SetReExportedSymbolSharedLibrary(
                      dylib_files.GetFileSpecAtIndex(dylib_ordinal - 1));
              }
            }
          }

#endif
  lldb::offset_t nlist_data_offset = 0;

  if (nlist_data.GetByteSize() > 0) {

    // If the sym array was not created while parsing the DSC unmapped
    // symbols, create it now.
    if (sym == nullptr) {
      sym =
          symtab.Resize(symtab_load_command.nsyms + m_dysymtab.nindirectsyms);
      num_syms = symtab.GetNumSymbols();
    }

    if (unmapped_local_symbols_found) {
      assert(m_dysymtab.ilocalsym == 0);
      nlist_data_offset += (m_dysymtab.nlocalsym * nlist_byte_size);
      nlist_idx = m_dysymtab.nlocalsym;
    } else {
      nlist_idx = 0;
    }

    typedef llvm::DenseMap<ConstString, uint16_t> UndefinedNameToDescMap;
    typedef llvm::DenseMap<uint32_t, ConstString> SymbolIndexToName;
    UndefinedNameToDescMap undefined_name_to_desc;
    SymbolIndexToName reexport_shlib_needs_fixup;

    // Symtab parsing is a huge mess. Everything is entangled and the code
    // requires access to a ridiculous amount of variables. LLDB depends
    // heavily on the proper merging of symbols and to get that right we need
    // to make sure we have parsed all the debug symbols first. Therefore we
    // invoke the lambda twice, once to parse only the debug symbols and then
    // once more to parse the remaining symbols.
    auto ParseSymbolLambda = [&](struct nlist_64 &nlist, uint32_t nlist_idx,
                                 bool debug_only) {
      const bool is_debug = ((nlist.n_type & N_STAB) != 0);
      if (is_debug != debug_only)
        return true;

      const char *symbol_name_non_abi_mangled = nullptr;
      const char *symbol_name = nullptr;

      if (have_strtab_data) {
        symbol_name = strtab_data.PeekCStr(nlist.n_strx);

        if (symbol_name == nullptr) {
          // No symbol should be NULL, even the symbols with no string values
          // should have an offset zero which points to an empty C-string
          Debugger::ReportError(llvm::formatv(
              "symbol[{0}] has invalid string table offset {1:x} in {2}, "
              "ignoring symbol",
              nlist_idx, nlist.n_strx, module_sp->GetFileSpec().GetPath()));
          return true;
        }
        if (symbol_name[0] == '\0')
          symbol_name = nullptr;
      } else {
        const addr_t str_addr = strtab_addr + nlist.n_strx;
        Status str_error;
        if (process->ReadCStringFromMemory(str_addr, memory_symbol_name,
                                           str_error))
          symbol_name = memory_symbol_name.c_str();
      }

      SymbolType type = eSymbolTypeInvalid;
      SectionSP symbol_section;
      lldb::addr_t symbol_byte_size = 0;
      bool add_nlist = true;
      bool is_gsym = false;
      bool demangled_is_synthesized = false;
      bool set_value = true;

      assert(sym_idx < num_syms);
      sym[sym_idx].SetDebug(is_debug);

      if (is_debug) {
        switch (nlist.n_type) {
        case N_GSYM:
          // global symbol: name,,NO_SECT,type,0
          // Sometimes the N_GSYM value contains the address.

          // FIXME: In the .o files, we have a GSYM and a debug symbol for all
          // the ObjC data.  They
          // have the same address, but we want to ensure that we always find
          // only the real symbol, 'cause we don't currently correctly
          // attribute the GSYM one to the ObjCClass/Ivar/MetaClass symbol
          // type.  This is a temporary hack to make sure the ObjectiveC
          // symbols get treated correctly.  To do this right, we should
          // coalesce all the GSYM & global symbols that have the same
          // address.
          is_gsym = true;
          sym[sym_idx].SetExternal(true);

          if (symbol_name && symbol_name[0] == '_' && symbol_name[1] == 'O') {
            llvm::StringRef symbol_name_ref(symbol_name);
            if (symbol_name_ref.starts_with(g_objc_v2_prefix_class)) {
              symbol_name_non_abi_mangled = symbol_name + 1;
              symbol_name = symbol_name + g_objc_v2_prefix_class.size();
              type = eSymbolTypeObjCClass;
              demangled_is_synthesized = true;

            } else if (symbol_name_ref.starts_with(
                           g_objc_v2_prefix_metaclass)) {
              symbol_name_non_abi_mangled = symbol_name + 1;
              symbol_name = symbol_name + g_objc_v2_prefix_metaclass.size();
              type = eSymbolTypeObjCMetaClass;
              demangled_is_synthesized = true;
            } else if (symbol_name_ref.starts_with(g_objc_v2_prefix_ivar)) {
              symbol_name_non_abi_mangled = symbol_name + 1;
              symbol_name = symbol_name + g_objc_v2_prefix_ivar.size();
              type = eSymbolTypeObjCIVar;
              demangled_is_synthesized = true;
            }
          } else {
            if (nlist.n_value != 0)
              symbol_section =
                  section_info.GetSection(nlist.n_sect, nlist.n_value);
            type = eSymbolTypeData;
          }
          break;

        case N_FNAME:
          // procedure name (f77 kludge): name,,NO_SECT,0,0
          type = eSymbolTypeCompiler;
          break;

        case N_FUN:
          // procedure: name,,n_sect,linenumber,address
          if (symbol_name) {
            type = eSymbolTypeCode;
            symbol_section =
                section_info.GetSection(nlist.n_sect, nlist.n_value);

            N_FUN_addr_to_sym_idx.insert(
                std::make_pair(nlist.n_value, sym_idx));
            // We use the current number of symbols in the symbol table in
            // lieu of using nlist_idx in case we ever start trimming entries
            // out
            N_FUN_indexes.push_back(sym_idx);
          } else {
            type = eSymbolTypeCompiler;

            if (!N_FUN_indexes.empty()) {
              // Copy the size of the function into the original STAB entry
              // so we don't have to hunt for it later
              symtab.SymbolAtIndex(N_FUN_indexes.back())
                  ->SetByteSize(nlist.n_value);
              N_FUN_indexes.pop_back();
              // We don't really need the end function STAB as it contains
              // the size which we already placed with the original symbol,
              // so don't add it if we want a minimal symbol table
              add_nlist = false;
            }
          }
          break;

        case N_STSYM:
          // static symbol: name,,n_sect,type,address
          N_STSYM_addr_to_sym_idx.insert(
              std::make_pair(nlist.n_value, sym_idx));
          symbol_section = section_info.GetSection(nlist.n_sect, nlist.n_value);
          if (symbol_name && symbol_name[0]) {
            type = ObjectFile::GetSymbolTypeFromName(symbol_name + 1,
                                                     eSymbolTypeData);
          }
          break;

        case N_LCSYM:
          // .lcomm symbol: name,,n_sect,type,address
          symbol_section = section_info.GetSection(nlist.n_sect, nlist.n_value);
          type = eSymbolTypeCommonBlock;
          break;

        case N_BNSYM:
          // We use the current number of symbols in the symbol table in lieu
          // of using nlist_idx in case we ever start trimming entries out
          // Skip these if we want minimal symbol tables
          add_nlist = false;
          break;

        case N_ENSYM:
          // Set the size of the N_BNSYM to the terminating index of this
          // N_ENSYM so that we can always skip the entire symbol if we need
          // to navigate more quickly at the source level when parsing STABS
          // Skip these if we want minimal symbol tables
          add_nlist = false;
          break;

        case N_OPT:
          // emitted with gcc2_compiled and in gcc source
          type = eSymbolTypeCompiler;
          break;

        case N_RSYM:
          // register sym: name,,NO_SECT,type,register
          type = eSymbolTypeVariable;
          break;

        case N_SLINE:
          // src line: 0,,n_sect,linenumber,address
          symbol_section = section_info.GetSection(nlist.n_sect, nlist.n_value);
          type = eSymbolTypeLineEntry;
          break;

        case N_SSYM:
          // structure elt: name,,NO_SECT,type,struct_offset
          type = eSymbolTypeVariableType;
          break;

        case N_SO:
          // source file name
          type = eSymbolTypeSourceFile;
          if (symbol_name == nullptr) {
            add_nlist = false;
            if (N_SO_index != UINT32_MAX) {
              // Set the size of the N_SO to the terminating index of this
              // N_SO so that we can always skip the entire N_SO if we need
              // to navigate more quickly at the source level when parsing
              // STABS
              symbol_ptr = symtab.SymbolAtIndex(N_SO_index);
              symbol_ptr->SetByteSize(sym_idx);
              symbol_ptr->SetSizeIsSibling(true);
            }
            N_NSYM_indexes.clear();
            N_INCL_indexes.clear();
            N_BRAC_indexes.clear();
            N_COMM_indexes.clear();
            N_FUN_indexes.clear();
            N_SO_index = UINT32_MAX;
          } else {
            // We use the current number of symbols in the symbol table in
            // lieu of using nlist_idx in case we ever start trimming entries
            // out
            const bool N_SO_has_full_path = symbol_name[0] == '/';
            if (N_SO_has_full_path) {
              if ((N_SO_index == sym_idx - 1) && ((sym_idx - 1) < num_syms)) {
                // We have two consecutive N_SO entries where the first
                // contains a directory and the second contains a full path.
                sym[sym_idx - 1].GetMangled().SetValue(
                    ConstString(symbol_name));
                m_nlist_idx_to_sym_idx[nlist_idx] = sym_idx - 1;
                add_nlist = false;
              } else {
                // This is the first entry in a N_SO that contains a
                // directory or a full path to the source file
                N_SO_index = sym_idx;
              }
            } else if ((N_SO_index == sym_idx - 1) &&
                       ((sym_idx - 1) < num_syms)) {
              // This is usually the second N_SO entry that contains just the
              // filename, so here we combine it with the first one if we are
              // minimizing the symbol table
              const char *so_path =
                  sym[sym_idx - 1].GetMangled().GetDemangledName().AsCString();
              if (so_path && so_path[0]) {
                std::string full_so_path(so_path);
                const size_t double_slash_pos = full_so_path.find("//");
                if (double_slash_pos != std::string::npos) {
                  // The linker has been generating bad N_SO entries with
                  // doubled up paths in the format "%s%s" where the first
                  // string in the DW_AT_comp_dir, and the second is the
                  // directory for the source file so you end up with a path
                  // that looks like "/tmp/src//tmp/src/"
                  FileSpec so_dir(so_path);
                  if (!FileSystem::Instance().Exists(so_dir)) {
                    so_dir.SetFile(&full_so_path[double_slash_pos + 1],
                                   FileSpec::Style::native);
                    if (FileSystem::Instance().Exists(so_dir)) {
                      // Trim off the incorrect path
                      full_so_path.erase(0, double_slash_pos + 1);
                    }
                  }
                }
                if (*full_so_path.rbegin() != '/')
                  full_so_path += '/';
                full_so_path += symbol_name;
                sym[sym_idx - 1].GetMangled().SetValue(
                    ConstString(full_so_path.c_str()));
                add_nlist = false;
                m_nlist_idx_to_sym_idx[nlist_idx] = sym_idx - 1;
              }
            } else {
              // This could be a relative path to a N_SO
              N_SO_index = sym_idx;
            }
          }
          break;

        case N_OSO:
          // object file name: name,,0,0,st_mtime
          type = eSymbolTypeObjectFile;
          break;

        case N_LSYM:
          // local sym: name,,NO_SECT,type,offset
          type = eSymbolTypeLocal;
          break;

        // INCL scopes
        case N_BINCL:
          // include file beginning: name,,NO_SECT,0,sum We use the current
          // number of symbols in the symbol table in lieu of using nlist_idx
          // in case we ever start trimming entries out
          N_INCL_indexes.push_back(sym_idx);
          type = eSymbolTypeScopeBegin;
          break;

        case N_EINCL:
          // include file end: name,,NO_SECT,0,0
          // Set the size of the N_BINCL to the terminating index of this
          // N_EINCL so that we can always skip the entire symbol if we need
          // to navigate more quickly at the source level when parsing STABS
          if (!N_INCL_indexes.empty()) {
            symbol_ptr = symtab.SymbolAtIndex(N_INCL_indexes.back());
            symbol_ptr->SetByteSize(sym_idx + 1);
            symbol_ptr->SetSizeIsSibling(true);
            N_INCL_indexes.pop_back();
          }
          type = eSymbolTypeScopeEnd;
          break;

        case N_SOL:
          // #included file name: name,,n_sect,0,address
          type = eSymbolTypeHeaderFile;

          // We currently don't use the header files on darwin
          add_nlist = false;
          break;

        case N_PARAMS:
          // compiler parameters: name,,NO_SECT,0,0
          type = eSymbolTypeCompiler;
          break;

        case N_VERSION:
          // compiler version: name,,NO_SECT,0,0
          type = eSymbolTypeCompiler;
          break;

        case N_OLEVEL:
          // compiler -O level: name,,NO_SECT,0,0
          type = eSymbolTypeCompiler;
          break;

        case N_PSYM:
          // parameter: name,,NO_SECT,type,offset
          type = eSymbolTypeVariable;
          break;

        case N_ENTRY:
          // alternate entry: name,,n_sect,linenumber,address
          symbol_section = section_info.GetSection(nlist.n_sect, nlist.n_value);
          type = eSymbolTypeLineEntry;
          break;

        // Left and Right Braces
        case N_LBRAC:
          // left bracket: 0,,NO_SECT,nesting level,address We use the
          // current number of symbols in the symbol table in lieu of using
          // nlist_idx in case we ever start trimming entries out
          symbol_section = section_info.GetSection(nlist.n_sect, nlist.n_value);
          N_BRAC_indexes.push_back(sym_idx);
          type = eSymbolTypeScopeBegin;
          break;

        case N_RBRAC:
          // right bracket: 0,,NO_SECT,nesting level,address Set the size of
          // the N_LBRAC to the terminating index of this N_RBRAC so that we
          // can always skip the entire symbol if we need to navigate more
          // quickly at the source level when parsing STABS
          symbol_section = section_info.GetSection(nlist.n_sect, nlist.n_value);
          if (!N_BRAC_indexes.empty()) {
            symbol_ptr = symtab.SymbolAtIndex(N_BRAC_indexes.back());
            symbol_ptr->SetByteSize(sym_idx + 1);
            symbol_ptr->SetSizeIsSibling(true);
            N_BRAC_indexes.pop_back();
          }
          type = eSymbolTypeScopeEnd;
          break;

        case N_EXCL:
          // deleted include file: name,,NO_SECT,0,sum
          type = eSymbolTypeHeaderFile;
          break;

        // COMM scopes
        case N_BCOMM:
          // begin common: name,,NO_SECT,0,0
          // We use the current number of symbols in the symbol table in lieu
          // of using nlist_idx in case we ever start trimming entries out
          type = eSymbolTypeScopeBegin;
          N_COMM_indexes.push_back(sym_idx);
          break;

        case N_ECOML:
          // end common (local name): 0,,n_sect,0,address
          symbol_section = section_info.GetSection(nlist.n_sect, nlist.n_value);
          [[fallthrough]];

        case N_ECOMM:
          // end common: name,,n_sect,0,0
          // Set the size of the N_BCOMM to the terminating index of this
          // N_ECOMM/N_ECOML so that we can always skip the entire symbol if
          // we need to navigate more quickly at the source level when
          // parsing STABS
          if (!N_COMM_indexes.empty()) {
            symbol_ptr = symtab.SymbolAtIndex(N_COMM_indexes.back());
            symbol_ptr->SetByteSize(sym_idx + 1);
            symbol_ptr->SetSizeIsSibling(true);
            N_COMM_indexes.pop_back();
          }
          type = eSymbolTypeScopeEnd;
          break;

        case N_LENG:
          // second stab entry with length information
          type = eSymbolTypeAdditional;
          break;

        default:
          break;
        }
      } else {
        uint8_t n_type = N_TYPE & nlist.n_type;
        sym[sym_idx].SetExternal((N_EXT & nlist.n_type) != 0);

        switch (n_type) {
        case N_INDR: {
          const char *reexport_name_cstr = strtab_data.PeekCStr(nlist.n_value);
          if (reexport_name_cstr && reexport_name_cstr[0] && symbol_name) {
            type = eSymbolTypeReExported;
            ConstString reexport_name(reexport_name_cstr +
                                      ((reexport_name_cstr[0] == '_') ? 1 : 0));
            sym[sym_idx].SetReExportedSymbolName(reexport_name);
            set_value = false;
            reexport_shlib_needs_fixup[sym_idx] = reexport_name;
            indirect_symbol_names.insert(
                ConstString(symbol_name + ((symbol_name[0] == '_') ? 1 : 0)));
          } else
            type = eSymbolTypeUndefined;
        } break;

        case N_UNDF:
          if (symbol_name && symbol_name[0]) {
            ConstString undefined_name(symbol_name +
                                       ((symbol_name[0] == '_') ? 1 : 0));
            undefined_name_to_desc[undefined_name] = nlist.n_desc;
          }
          [[fallthrough]];

        case N_PBUD:
          type = eSymbolTypeUndefined;
          break;

        case N_ABS:
          type = eSymbolTypeAbsolute;
          break;

        case N_SECT: {
          symbol_section = section_info.GetSection(nlist.n_sect, nlist.n_value);

          if (!symbol_section) {
            // TODO: warn about this?
            add_nlist = false;
            break;
          }

          if (TEXT_eh_frame_sectID == nlist.n_sect) {
            type = eSymbolTypeException;
          } else {
            uint32_t section_type = symbol_section->Get() & SECTION_TYPE;

            switch (section_type) {
            case S_CSTRING_LITERALS:
              type = eSymbolTypeData;
              break; // section with only literal C strings
            case S_4BYTE_LITERALS:
              type = eSymbolTypeData;
              break; // section with only 4 byte literals
            case S_8BYTE_LITERALS:
              type = eSymbolTypeData;
              break; // section with only 8 byte literals
            case S_LITERAL_POINTERS:
              type = eSymbolTypeTrampoline;
              break; // section with only pointers to literals
            case S_NON_LAZY_SYMBOL_POINTERS:
              type = eSymbolTypeTrampoline;
              break; // section with only non-lazy symbol pointers
            case S_LAZY_SYMBOL_POINTERS:
              type = eSymbolTypeTrampoline;
              break; // section with only lazy symbol pointers
            case S_SYMBOL_STUBS:
              type = eSymbolTypeTrampoline;
              break; // section with only symbol stubs, byte size of stub in
                     // the reserved2 field
            case S_MOD_INIT_FUNC_POINTERS:
              type = eSymbolTypeCode;
              break; // section with only function pointers for initialization
            case S_MOD_TERM_FUNC_POINTERS:
              type = eSymbolTypeCode;
              break; // section with only function pointers for termination
            case S_INTERPOSING:
              type = eSymbolTypeTrampoline;
              break; // section with only pairs of function pointers for
                     // interposing
            case S_16BYTE_LITERALS:
              type = eSymbolTypeData;
              break; // section with only 16 byte literals
            case S_DTRACE_DOF:
              type = eSymbolTypeInstrumentation;
              break;
            case S_LAZY_DYLIB_SYMBOL_POINTERS:
              type = eSymbolTypeTrampoline;
              break;
            default:
              switch (symbol_section->GetType()) {
              case lldb::eSectionTypeCode:
                type = eSymbolTypeCode;
                break;
              case eSectionTypeData:
              case eSectionTypeDataCString:         // Inlined C string data
              case eSectionTypeDataCStringPointers: // Pointers to C string
                                                    // data
              case eSectionTypeDataSymbolAddress:   // Address of a symbol in
                                                    // the symbol table
              case eSectionTypeData4:
              case eSectionTypeData8:
              case eSectionTypeData16:
                type = eSymbolTypeData;
                break;
              default:
                break;
              }
              break;
            }

            if (type == eSymbolTypeInvalid) {
              const char *symbol_sect_name =
                  symbol_section->GetName().AsCString();
              if (symbol_section->IsDescendant(text_section_sp.get())) {
                if (symbol_section->IsClear(S_ATTR_PURE_INSTRUCTIONS |
                                            S_ATTR_SELF_MODIFYING_CODE |
                                            S_ATTR_SOME_INSTRUCTIONS))
                  type = eSymbolTypeData;
                else
                  type = eSymbolTypeCode;
              } else if (symbol_section->IsDescendant(data_section_sp.get()) ||
                         symbol_section->IsDescendant(
                             data_dirty_section_sp.get()) ||
                         symbol_section->IsDescendant(
                             data_const_section_sp.get())) {
                if (symbol_sect_name &&
                    ::strstr(symbol_sect_name, "__objc") == symbol_sect_name) {
                  type = eSymbolTypeRuntime;

                  if (symbol_name) {
                    llvm::StringRef symbol_name_ref(symbol_name);
                    if (symbol_name_ref.starts_with("_OBJC_")) {
                      llvm::StringRef g_objc_v2_prefix_class(
                          "_OBJC_CLASS_$_");
                      llvm::StringRef g_objc_v2_prefix_metaclass(
                          "_OBJC_METACLASS_$_");
                      llvm::StringRef g_objc_v2_prefix_ivar(
                          "_OBJC_IVAR_$_");
                      if (symbol_name_ref.starts_with(g_objc_v2_prefix_class)) {
                        symbol_name_non_abi_mangled = symbol_name + 1;
                        symbol_name =
                            symbol_name + g_objc_v2_prefix_class.size();
                        type = eSymbolTypeObjCClass;
                        demangled_is_synthesized = true;
                      } else if (symbol_name_ref.starts_with(
                                     g_objc_v2_prefix_metaclass)) {
                        symbol_name_non_abi_mangled = symbol_name + 1;
                        symbol_name =
                            symbol_name + g_objc_v2_prefix_metaclass.size();
                        type = eSymbolTypeObjCMetaClass;
                        demangled_is_synthesized = true;
                      } else if (symbol_name_ref.starts_with(
                                     g_objc_v2_prefix_ivar)) {
                        symbol_name_non_abi_mangled = symbol_name + 1;
                        symbol_name =
                            symbol_name + g_objc_v2_prefix_ivar.size();
                        type = eSymbolTypeObjCIVar;
                        demangled_is_synthesized = true;
                      }
                    }
                  }
                } else if (symbol_sect_name &&
                           ::strstr(symbol_sect_name, "__gcc_except_tab") ==
                               symbol_sect_name) {
                  type = eSymbolTypeException;
                } else {
                  type = eSymbolTypeData;
                }
              } else if (symbol_sect_name &&
                         ::strstr(symbol_sect_name, "__IMPORT") ==
                             symbol_sect_name) {
                type = eSymbolTypeTrampoline;
              } else if (symbol_section->IsDescendant(objc_section_sp.get())) {
                type = eSymbolTypeRuntime;
                if (symbol_name && symbol_name[0] == '.') {
                  llvm::StringRef symbol_name_ref(symbol_name);
                  llvm::StringRef g_objc_v1_prefix_class(
                      ".objc_class_name_");
                  if (symbol_name_ref.starts_with(g_objc_v1_prefix_class)) {
                    symbol_name_non_abi_mangled = symbol_name;
                    symbol_name = symbol_name + g_objc_v1_prefix_class.size();
                    type = eSymbolTypeObjCClass;
                    demangled_is_synthesized = true;
                  }
                }
              }
            }
          }
        } break;
        }
      }

      if (!add_nlist) {
        sym[sym_idx].Clear();
        return true;
      }

      uint64_t symbol_value = nlist.n_value;

      if (symbol_name_non_abi_mangled) {
        sym[sym_idx].GetMangled().SetMangledName(
            ConstString(symbol_name_non_abi_mangled));
        sym[sym_idx].GetMangled().SetDemangledName(ConstString(symbol_name));
      } else {

        if (symbol_name && symbol_name[0] == '_') {
          symbol_name++; // Skip the leading underscore
        }

        if (symbol_name) {
          ConstString const_symbol_name(symbol_name);
          sym[sym_idx].GetMangled().SetValue(const_symbol_name);
        }
      }

      if (is_gsym) {
        const char *gsym_name = sym[sym_idx]
                                    .GetMangled()
                                    .GetName(Mangled::ePreferMangled)
                                    .GetCString();
        if (gsym_name)
          N_GSYM_name_to_sym_idx[gsym_name] = sym_idx;
      }

      if (symbol_section) {
        const addr_t section_file_addr = symbol_section->GetFileAddress();
        if (symbol_byte_size == 0 && function_starts_count > 0) {
          addr_t symbol_lookup_file_addr = nlist.n_value;
          // Do an exact address match for non-ARM addresses, else get the
          // closest since the symbol might be a thumb symbol which has an
          // address with bit zero set.
          FunctionStarts::Entry *func_start_entry =
              function_starts.FindEntry(symbol_lookup_file_addr, !is_arm);
          if (is_arm && func_start_entry) {
            // Verify that the function start address is the symbol address
            // (ARM) or the symbol address + 1 (thumb).
            if (func_start_entry->addr != symbol_lookup_file_addr &&
                func_start_entry->addr != (symbol_lookup_file_addr + 1)) {
              // Not the right entry, NULL it out...
              func_start_entry = nullptr;
            }
          }
          if (func_start_entry) {
            func_start_entry->data = true;

            addr_t symbol_file_addr = func_start_entry->addr;
            if (is_arm)
              symbol_file_addr &= THUMB_ADDRESS_BIT_MASK;

            const FunctionStarts::Entry *next_func_start_entry =
                function_starts.FindNextEntry(func_start_entry);
            const addr_t section_end_file_addr =
                section_file_addr + symbol_section->GetByteSize();
            if (next_func_start_entry) {
              addr_t next_symbol_file_addr = next_func_start_entry->addr;
              // Be sure the clear the Thumb address bit when we calculate the
              // size from the current and next address
              if (is_arm)
                next_symbol_file_addr &= THUMB_ADDRESS_BIT_MASK;
              symbol_byte_size = std::min<lldb::addr_t>(
                  next_symbol_file_addr - symbol_file_addr,
                  section_end_file_addr - symbol_file_addr);
            } else {
              symbol_byte_size = section_end_file_addr - symbol_file_addr;
            }
          }
        }
        symbol_value -= section_file_addr;
      }

      if (!is_debug) {
        if (type == eSymbolTypeCode) {
          // See if we can find a N_FUN entry for any code symbols. If we do
          // find a match, and the name matches, then we can merge the two into
          // just the function symbol to avoid duplicate entries in the symbol
          // table.
          std::pair<ValueToSymbolIndexMap::const_iterator,
                    ValueToSymbolIndexMap::const_iterator>
              range;
          range = N_FUN_addr_to_sym_idx.equal_range(nlist.n_value);
          if (range.first != range.second) {
            for (ValueToSymbolIndexMap::const_iterator pos = range.first;
                 pos != range.second; ++pos) {
              if (sym[sym_idx].GetMangled().GetName(Mangled::ePreferMangled) ==
                  sym[pos->second].GetMangled().GetName(
                      Mangled::ePreferMangled)) {
                m_nlist_idx_to_sym_idx[nlist_idx] = pos->second;
                // We just need the flags from the linker symbol, so put these
                // flags into the N_FUN flags to avoid duplicate symbols in the
                // symbol table.
                sym[pos->second].SetExternal(sym[sym_idx].IsExternal());
                sym[pos->second].SetFlags(nlist.n_type << 16 | nlist.n_desc);
                if (resolver_addresses.find(nlist.n_value) !=
                    resolver_addresses.end())
                  sym[pos->second].SetType(eSymbolTypeResolver);
                sym[sym_idx].Clear();
                return true;
              }
            }
          } else {
            if (resolver_addresses.find(nlist.n_value) !=
                resolver_addresses.end())
              type = eSymbolTypeResolver;
          }
        } else if (type == eSymbolTypeData || type == eSymbolTypeObjCClass ||
                   type == eSymbolTypeObjCMetaClass ||
                   type == eSymbolTypeObjCIVar) {
          // See if we can find a N_STSYM entry for any data symbols. If we do
          // find a match, and the name matches, then we can merge the two into
          // just the Static symbol to avoid duplicate entries in the symbol
          // table.
          std::pair<ValueToSymbolIndexMap::const_iterator,
                    ValueToSymbolIndexMap::const_iterator>
              range;
          range = N_STSYM_addr_to_sym_idx.equal_range(nlist.n_value);
          if (range.first != range.second) {
            for (ValueToSymbolIndexMap::const_iterator pos = range.first;
                 pos != range.second; ++pos) {
              if (sym[sym_idx].GetMangled().GetName(Mangled::ePreferMangled) ==
                  sym[pos->second].GetMangled().GetName(
                      Mangled::ePreferMangled)) {
                m_nlist_idx_to_sym_idx[nlist_idx] = pos->second;
                // We just need the flags from the linker symbol, so put these
                // flags into the N_STSYM flags to avoid duplicate symbols in
                // the symbol table.
                sym[pos->second].SetExternal(sym[sym_idx].IsExternal());
                sym[pos->second].SetFlags(nlist.n_type << 16 | nlist.n_desc);
                sym[sym_idx].Clear();
                return true;
              }
            }
          } else {
            // Combine N_GSYM stab entries with the non stab symbol.
            const char *gsym_name = sym[sym_idx]
                                        .GetMangled()
                                        .GetName(Mangled::ePreferMangled)
                                        .GetCString();
            if (gsym_name) {
              ConstNameToSymbolIndexMap::const_iterator pos =
                  N_GSYM_name_to_sym_idx.find(gsym_name);
              if (pos != N_GSYM_name_to_sym_idx.end()) {
                const uint32_t GSYM_sym_idx = pos->second;
                m_nlist_idx_to_sym_idx[nlist_idx] = GSYM_sym_idx;
                // Copy the address, because often the N_GSYM address has an
                // invalid address of zero when the global is a common symbol.
                sym[GSYM_sym_idx].GetAddressRef().SetSection(symbol_section);
                sym[GSYM_sym_idx].GetAddressRef().SetOffset(symbol_value);
                add_symbol_addr(
                    sym[GSYM_sym_idx].GetAddress().GetFileAddress());
                // We just need the flags from the linker symbol, so put these
                // flags into the N_GSYM flags to avoid duplicate symbols in
                // the symbol table.
                sym[GSYM_sym_idx].SetFlags(nlist.n_type << 16 | nlist.n_desc);
                sym[sym_idx].Clear();
                return true;
              }
            }
          }
        }
      }

      sym[sym_idx].SetID(nlist_idx);
      sym[sym_idx].SetType(type);
      if (set_value) {
        sym[sym_idx].GetAddressRef().SetSection(symbol_section);
        sym[sym_idx].GetAddressRef().SetOffset(symbol_value);
        if (symbol_section)
          add_symbol_addr(sym[sym_idx].GetAddress().GetFileAddress());
      }
      sym[sym_idx].SetFlags(nlist.n_type << 16 | nlist.n_desc);
      if (nlist.n_desc & N_WEAK_REF)
        sym[sym_idx].SetIsWeak(true);

      if (symbol_byte_size > 0)
        sym[sym_idx].SetByteSize(symbol_byte_size);

      if (demangled_is_synthesized)
        sym[sym_idx].SetDemangledNameIsSynthesized(true);

      ++sym_idx;
      return true;
    };

    // First parse all the nlists but don't process them yet. See the next
    // comment for an explanation why.
    std::vector<struct nlist_64> nlists;
    nlists.reserve(symtab_load_command.nsyms);
    for (; nlist_idx < symtab_load_command.nsyms; ++nlist_idx) {
      if (auto nlist =
              ParseNList(nlist_data, nlist_data_offset, nlist_byte_size))
        nlists.push_back(*nlist);
      else
        break;
    }

    // Now parse all the debug symbols. This is needed to merge non-debug
    // symbols in the next step. Non-debug symbols are always coalesced into
    // the debug symbol. Doing this in one step would mean that some symbols
    // won't be merged.
    nlist_idx = 0;
    for (auto &nlist : nlists) {
      if (!ParseSymbolLambda(nlist, nlist_idx++, DebugSymbols))
        break;
    }

    // Finally parse all the non debug symbols.
    nlist_idx = 0;
    for (auto &nlist : nlists) {
      if (!ParseSymbolLambda(nlist, nlist_idx++, NonDebugSymbols))
        break;
    }

    for (const auto &pos : reexport_shlib_needs_fixup) {
      const auto undef_pos = undefined_name_to_desc.find(pos.second);
      if (undef_pos != undefined_name_to_desc.end()) {
        const uint8_t dylib_ordinal =
            llvm::MachO::GET_LIBRARY_ORDINAL(undef_pos->second);
        if (dylib_ordinal > 0 && dylib_ordinal < dylib_files.GetSize())
          sym[pos.first].SetReExportedSymbolSharedLibrary(
              dylib_files.GetFileSpecAtIndex(dylib_ordinal - 1));
      }
    }
  }

  // Count how many trie symbols we'll add to the symbol table
  int trie_symbol_table_augment_count = 0;
  for (auto &e : external_sym_trie_entries) {
    if (!symbols_added.contains(e.entry.address))
      trie_symbol_table_augment_count++;
  }

  if (num_syms < sym_idx + trie_symbol_table_augment_count) {
    num_syms = sym_idx + trie_symbol_table_augment_count;
    sym = symtab.Resize(num_syms);
  }
  uint32_t synthetic_sym_id = symtab_load_command.nsyms;

  // Add symbols from the trie to the symbol table.
  for (auto &e : external_sym_trie_entries) {
    if (symbols_added.contains(e.entry.address))
      continue;

    // Find the section that this trie address is in, use that to annotate
    // symbol type as we add the trie address and name to the symbol table.
    Address symbol_addr;
    if (module_sp->ResolveFileAddress(e.entry.address, symbol_addr)) {
      SectionSP symbol_section(symbol_addr.GetSection());
      const char *symbol_name = e.entry.name.GetCString();
      bool demangled_is_synthesized = false;
      SymbolType type =
          GetSymbolType(symbol_name, demangled_is_synthesized, text_section_sp,
                        data_section_sp, data_dirty_section_sp,
                        data_const_section_sp, symbol_section);

      sym[sym_idx].SetType(type);
      if (symbol_section) {
        sym[sym_idx].SetID(synthetic_sym_id++);
        sym[sym_idx].GetMangled().SetMangledName(ConstString(symbol_name));
        if (demangled_is_synthesized)
          sym[sym_idx].SetDemangledNameIsSynthesized(true);
        sym[sym_idx].SetIsSynthetic(true);
        sym[sym_idx].SetExternal(true);
        sym[sym_idx].GetAddressRef() = symbol_addr;
        add_symbol_addr(symbol_addr.GetFileAddress());
        if (e.entry.flags & TRIE_SYMBOL_IS_THUMB)
          sym[sym_idx].SetFlags(MACHO_NLIST_ARM_SYMBOL_IS_THUMB);
        ++sym_idx;
      }
    }
  }

  if (function_starts_count > 0) {
    uint32_t num_synthetic_function_symbols = 0;
    for (i = 0; i < function_starts_count; ++i) {
      if (!symbols_added.contains(function_starts.GetEntryRef(i).addr))
        ++num_synthetic_function_symbols;
    }

    if (num_synthetic_function_symbols > 0) {
      if (num_syms < sym_idx + num_synthetic_function_symbols) {
        num_syms = sym_idx + num_synthetic_function_symbols;
        sym = symtab.Resize(num_syms);
      }
      for (i = 0; i < function_starts_count; ++i) {
        const FunctionStarts::Entry *func_start_entry =
            function_starts.GetEntryAtIndex(i);
        if (!symbols_added.contains(func_start_entry->addr)) {
          addr_t symbol_file_addr = func_start_entry->addr;
          uint32_t symbol_flags = 0;
          if (func_start_entry->data)
            symbol_flags = MACHO_NLIST_ARM_SYMBOL_IS_THUMB;
          Address symbol_addr;
          if (module_sp->ResolveFileAddress(symbol_file_addr, symbol_addr)) {
            SectionSP symbol_section(symbol_addr.GetSection());
            uint32_t symbol_byte_size = 0;
            if (symbol_section) {
              const addr_t section_file_addr = symbol_section->GetFileAddress();
              const FunctionStarts::Entry *next_func_start_entry =
                  function_starts.FindNextEntry(func_start_entry);
              const addr_t section_end_file_addr =
                  section_file_addr + symbol_section->GetByteSize();
              if (next_func_start_entry) {
                addr_t next_symbol_file_addr = next_func_start_entry->addr;
                if (is_arm)
                  next_symbol_file_addr &= THUMB_ADDRESS_BIT_MASK;
                symbol_byte_size = std::min<lldb::addr_t>(
                    next_symbol_file_addr - symbol_file_addr,
                    section_end_file_addr - symbol_file_addr);
              } else {
                symbol_byte_size = section_end_file_addr - symbol_file_addr;
              }
              sym[sym_idx].SetID(synthetic_sym_id++);
              // Don't set the name for any synthetic symbols, the Symbol
              // object will generate one if needed when the name is accessed
              // via accessors.
              sym[sym_idx].GetMangled().SetDemangledName(ConstString());
              sym[sym_idx].SetType(eSymbolTypeCode);
              sym[sym_idx].SetIsSynthetic(true);
              sym[sym_idx].GetAddressRef() = symbol_addr;
              add_symbol_addr(symbol_addr.GetFileAddress());
              if (symbol_flags)
                sym[sym_idx].SetFlags(symbol_flags);
              if (symbol_byte_size)
                sym[sym_idx].SetByteSize(symbol_byte_size);
              ++sym_idx;
            }
          }
        }
      }
    }
  }

  // Trim our symbols down to just what we ended up with after removing any
  // symbols.
  if (sym_idx < num_syms) {
    num_syms = sym_idx;
    sym = symtab.Resize(num_syms);
  }

  // Now synthesize indirect symbols
  if (m_dysymtab.nindirectsyms != 0) {
    if (indirect_symbol_index_data.GetByteSize()) {
      NListIndexToSymbolIndexMap::const_iterator end_index_pos =
          m_nlist_idx_to_sym_idx.end();

      for (uint32_t sect_idx = 1; sect_idx < m_mach_sections.size();
           ++sect_idx) {
        if ((m_mach_sections[sect_idx].flags & SECTION_TYPE) ==
            S_SYMBOL_STUBS) {
          uint32_t symbol_stub_byte_size = m_mach_sections[sect_idx].reserved2;
          if (symbol_stub_byte_size == 0)
            continue;

          const uint32_t num_symbol_stubs =
              m_mach_sections[sect_idx].size / symbol_stub_byte_size;

          if (num_symbol_stubs == 0)
            continue;

          const uint32_t symbol_stub_index_offset =
              m_mach_sections[sect_idx].reserved1;
          for (uint32_t stub_idx = 0; stub_idx < num_symbol_stubs; ++stub_idx) {
            const uint32_t symbol_stub_index =
                symbol_stub_index_offset + stub_idx;
            const lldb::addr_t symbol_stub_addr =
                m_mach_sections[sect_idx].addr +
                (stub_idx * symbol_stub_byte_size);
            lldb::offset_t symbol_stub_offset = symbol_stub_index * 4;
            if (indirect_symbol_index_data.ValidOffsetForDataOfSize(
                    symbol_stub_offset, 4)) {
              const uint32_t stub_sym_id =
                  indirect_symbol_index_data.GetU32(&symbol_stub_offset);
              if (stub_sym_id & (INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL))
                continue;

              NListIndexToSymbolIndexMap::const_iterator index_pos =
                  m_nlist_idx_to_sym_idx.find(stub_sym_id);
              Symbol *stub_symbol = nullptr;
              if (index_pos != end_index_pos) {
                // We have a remapping from the original nlist index to a
                // current symbol index, so just look this up by index
                stub_symbol = symtab.SymbolAtIndex(index_pos->second);
              } else {
                // We need to lookup a symbol using the original nlist symbol
                // index since this index is coming from the S_SYMBOL_STUBS
                stub_symbol = symtab.FindSymbolByID(stub_sym_id);
              }

              if (stub_symbol) {
                Address so_addr(symbol_stub_addr, section_list);

                if (stub_symbol->GetType() == eSymbolTypeUndefined) {
                  // Change the external symbol into a trampoline that makes
                  // sense These symbols were N_UNDF N_EXT, and are useless
                  // to us, so we can re-use them so we don't have to make up
                  // a synthetic symbol for no good reason.
                  if (resolver_addresses.find(symbol_stub_addr) ==
                      resolver_addresses.end())
                    stub_symbol->SetType(eSymbolTypeTrampoline);
                  else
                    stub_symbol->SetType(eSymbolTypeResolver);
                  stub_symbol->SetExternal(false);
                  stub_symbol->GetAddressRef() = so_addr;
                  stub_symbol->SetByteSize(symbol_stub_byte_size);
                } else {
                  // Make a synthetic symbol to describe the trampoline stub
                  Mangled stub_symbol_mangled_name(stub_symbol->GetMangled());
                  if (sym_idx >= num_syms) {
                    sym = symtab.Resize(++num_syms);
                    stub_symbol = nullptr; // this pointer no longer valid
                  }
                  sym[sym_idx].SetID(synthetic_sym_id++);
                  sym[sym_idx].GetMangled() = stub_symbol_mangled_name;
                  if (resolver_addresses.find(symbol_stub_addr) ==
                      resolver_addresses.end())
                    sym[sym_idx].SetType(eSymbolTypeTrampoline);
                  else
                    sym[sym_idx].SetType(eSymbolTypeResolver);
                  sym[sym_idx].SetIsSynthetic(true);
                  sym[sym_idx].GetAddressRef() = so_addr;
                  add_symbol_addr(so_addr.GetFileAddress());
                  sym[sym_idx].SetByteSize(symbol_stub_byte_size);
                  ++sym_idx;
                }
              } else {
                if (log)
                  log->Warning("symbol stub referencing symbol table symbol "
                               "%u that isn't in our minimal symbol table, "
                               "fix this!!!",
                               stub_sym_id);
              }
            }
          }
        }
      }
    }
  }

  if (!reexport_trie_entries.empty()) {
    for (const auto &e : reexport_trie_entries) {
      if (e.entry.import_name) {
        // Only add indirect symbols from the Trie entries if we didn't have
        // a N_INDR nlist entry for this already
        if (indirect_symbol_names.find(e.entry.name) ==
            indirect_symbol_names.end()) {
          // Make a synthetic symbol to describe re-exported symbol.
          if (sym_idx >= num_syms)
            sym = symtab.Resize(++num_syms);
          sym[sym_idx].SetID(synthetic_sym_id++);
          sym[sym_idx].GetMangled() = Mangled(e.entry.name);
          sym[sym_idx].SetType(eSymbolTypeReExported);
          sym[sym_idx].SetIsSynthetic(true);
          sym[sym_idx].SetReExportedSymbolName(e.entry.import_name);
          if (e.entry.other > 0 && e.entry.other <= dylib_files.GetSize()) {
            sym[sym_idx].SetReExportedSymbolSharedLibrary(
                dylib_files.GetFileSpecAtIndex(e.entry.other - 1));
          }
          ++sym_idx;
        }
      }
    }
  }
}

void ObjectFileMachO::Dump(Stream *s) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    s->Printf("%p: ", static_cast<void *>(this));
    s->Indent();
    if (m_header.magic == MH_MAGIC_64 || m_header.magic == MH_CIGAM_64)
      s->PutCString("ObjectFileMachO64");
    else
      s->PutCString("ObjectFileMachO32");

    *s << ", file = '" << m_file;
    ModuleSpecList all_specs;
    ModuleSpec base_spec;
    GetAllArchSpecs(m_header, m_data, MachHeaderSizeFromMagic(m_header.magic),
                    base_spec, all_specs);
    for (unsigned i = 0, e = all_specs.GetSize(); i != e; ++i) {
      *s << "', triple";
      if (e)
        s->Printf("[%d]", i);
      *s << " = ";
      *s << all_specs.GetModuleSpecRefAtIndex(i)
                .GetArchitecture()
                .GetTriple()
                .getTriple();
    }
    *s << "\n";
    SectionList *sections = GetSectionList();
    if (sections)
      sections->Dump(s->AsRawOstream(), s->GetIndentLevel(), nullptr, true,
                     UINT32_MAX);

    if (m_symtab_up)
      m_symtab_up->Dump(s, nullptr, eSortOrderNone);
  }
}

UUID ObjectFileMachO::GetUUID(const llvm::MachO::mach_header &header,
                              const lldb_private::DataExtractor &data,
                              lldb::offset_t lc_offset) {
  uint32_t i;
  llvm::MachO::uuid_command load_cmd;

  lldb::offset_t offset = lc_offset;
  for (i = 0; i < header.ncmds; ++i) {
    const lldb::offset_t cmd_offset = offset;
    if (data.GetU32(&offset, &load_cmd, 2) == nullptr)
      break;

    if (load_cmd.cmd == LC_UUID) {
      const uint8_t *uuid_bytes = data.PeekData(offset, 16);

      if (uuid_bytes) {
        // OpenCL on Mac OS X uses the same UUID for each of its object files.
        // We pretend these object files have no UUID to prevent crashing.

        const uint8_t opencl_uuid[] = {0x8c, 0x8e, 0xb3, 0x9b, 0x3b, 0xa8,
                                       0x4b, 0x16, 0xb6, 0xa4, 0x27, 0x63,
                                       0xbb, 0x14, 0xf0, 0x0d};

        if (!memcmp(uuid_bytes, opencl_uuid, 16))
          return UUID();

        return UUID(uuid_bytes, 16);
      }
      return UUID();
    }
    offset = cmd_offset + load_cmd.cmdsize;
  }
  return UUID();
}

static llvm::StringRef GetOSName(uint32_t cmd) {
  switch (cmd) {
  case llvm::MachO::LC_VERSION_MIN_IPHONEOS:
    return llvm::Triple::getOSTypeName(llvm::Triple::IOS);
  case llvm::MachO::LC_VERSION_MIN_MACOSX:
    return llvm::Triple::getOSTypeName(llvm::Triple::MacOSX);
  case llvm::MachO::LC_VERSION_MIN_TVOS:
    return llvm::Triple::getOSTypeName(llvm::Triple::TvOS);
  case llvm::MachO::LC_VERSION_MIN_WATCHOS:
    return llvm::Triple::getOSTypeName(llvm::Triple::WatchOS);
  default:
    llvm_unreachable("unexpected LC_VERSION load command");
  }
}

namespace {
struct OSEnv {
  llvm::StringRef os_type;
  llvm::StringRef environment;
  OSEnv(uint32_t cmd) {
    switch (cmd) {
    case llvm::MachO::PLATFORM_MACOS:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::MacOSX);
      return;
    case llvm::MachO::PLATFORM_IOS:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::IOS);
      return;
    case llvm::MachO::PLATFORM_TVOS:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::TvOS);
      return;
    case llvm::MachO::PLATFORM_WATCHOS:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::WatchOS);
      return;
    case llvm::MachO::PLATFORM_BRIDGEOS:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::BridgeOS);
      return;
    case llvm::MachO::PLATFORM_DRIVERKIT:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::DriverKit);
      return;
    case llvm::MachO::PLATFORM_MACCATALYST:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::IOS);
      environment = llvm::Triple::getEnvironmentTypeName(llvm::Triple::MacABI);
      return;
    case llvm::MachO::PLATFORM_IOSSIMULATOR:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::IOS);
      environment =
          llvm::Triple::getEnvironmentTypeName(llvm::Triple::Simulator);
      return;
    case llvm::MachO::PLATFORM_TVOSSIMULATOR:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::TvOS);
      environment =
          llvm::Triple::getEnvironmentTypeName(llvm::Triple::Simulator);
      return;
    case llvm::MachO::PLATFORM_WATCHOSSIMULATOR:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::WatchOS);
      environment =
          llvm::Triple::getEnvironmentTypeName(llvm::Triple::Simulator);
      return;
    case llvm::MachO::PLATFORM_XROS:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::XROS);
      return;
    case llvm::MachO::PLATFORM_XROS_SIMULATOR:
      os_type = llvm::Triple::getOSTypeName(llvm::Triple::XROS);
      environment =
          llvm::Triple::getEnvironmentTypeName(llvm::Triple::Simulator);
      return;
    default: {
      Log *log(GetLog(LLDBLog::Symbols | LLDBLog::Process));
      LLDB_LOGF(log, "unsupported platform in LC_BUILD_VERSION");
    }
    }
  }
};

struct MinOS {
  uint32_t major_version, minor_version, patch_version;
  MinOS(uint32_t version)
      : major_version(version >> 16), minor_version((version >> 8) & 0xffu),
        patch_version(version & 0xffu) {}
};
} // namespace

void ObjectFileMachO::GetAllArchSpecs(const llvm::MachO::mach_header &header,
                                      const lldb_private::DataExtractor &data,
                                      lldb::offset_t lc_offset,
                                      ModuleSpec &base_spec,
                                      lldb_private::ModuleSpecList &all_specs) {
  auto &base_arch = base_spec.GetArchitecture();
  base_arch.SetArchitecture(eArchTypeMachO, header.cputype, header.cpusubtype);
  if (!base_arch.IsValid())
    return;

  bool found_any = false;
  auto add_triple = [&](const llvm::Triple &triple) {
    auto spec = base_spec;
    spec.GetArchitecture().GetTriple() = triple;
    if (spec.GetArchitecture().IsValid()) {
      spec.GetUUID() = ObjectFileMachO::GetUUID(header, data, lc_offset);
      all_specs.Append(spec);
      found_any = true;
    }
  };

  // Set OS to an unspecified unknown or a "*" so it can match any OS
  llvm::Triple base_triple = base_arch.GetTriple();
  base_triple.setOS(llvm::Triple::UnknownOS);
  base_triple.setOSName(llvm::StringRef());

  if (header.filetype == MH_PRELOAD) {
    if (header.cputype == CPU_TYPE_ARM) {
      // If this is a 32-bit arm binary, and it's a standalone binary, force
      // the Vendor to Apple so we don't accidentally pick up the generic
      // armv7 ABI at runtime.  Apple's armv7 ABI always uses r7 for the
      // frame pointer register; most other armv7 ABIs use a combination of
      // r7 and r11.
      base_triple.setVendor(llvm::Triple::Apple);
    } else {
      // Set vendor to an unspecified unknown or a "*" so it can match any
      // vendor This is required for correct behavior of EFI debugging on
      // x86_64
      base_triple.setVendor(llvm::Triple::UnknownVendor);
      base_triple.setVendorName(llvm::StringRef());
    }
    return add_triple(base_triple);
  }

  llvm::MachO::load_command load_cmd;

  // See if there is an LC_VERSION_MIN_* load command that can give
  // us the OS type.
  lldb::offset_t offset = lc_offset;
  for (uint32_t i = 0; i < header.ncmds; ++i) {
    const lldb::offset_t cmd_offset = offset;
    if (data.GetU32(&offset, &load_cmd, 2) == nullptr)
      break;

    llvm::MachO::version_min_command version_min;
    switch (load_cmd.cmd) {
    case llvm::MachO::LC_VERSION_MIN_MACOSX:
    case llvm::MachO::LC_VERSION_MIN_IPHONEOS:
    case llvm::MachO::LC_VERSION_MIN_TVOS:
    case llvm::MachO::LC_VERSION_MIN_WATCHOS: {
      if (load_cmd.cmdsize != sizeof(version_min))
        break;
      if (data.ExtractBytes(cmd_offset, sizeof(version_min),
                            data.GetByteOrder(), &version_min) == 0)
        break;
      MinOS min_os(version_min.version);
      llvm::SmallString<32> os_name;
      llvm::raw_svector_ostream os(os_name);
      os << GetOSName(load_cmd.cmd) << min_os.major_version << '.'
         << min_os.minor_version << '.' << min_os.patch_version;

      auto triple = base_triple;
      triple.setOSName(os.str());

      // Disambiguate legacy simulator platforms.
      if (load_cmd.cmd != llvm::MachO::LC_VERSION_MIN_MACOSX &&
          (base_triple.getArch() == llvm::Triple::x86_64 ||
           base_triple.getArch() == llvm::Triple::x86)) {
        // The combination of legacy LC_VERSION_MIN load command and
        // x86 architecture always indicates a simulator environment.
        // The combination of LC_VERSION_MIN and arm architecture only
        // appears for native binaries. Back-deploying simulator
        // binaries on Apple Silicon Macs use the modern unambigous
        // LC_BUILD_VERSION load commands; no special handling required.
        triple.setEnvironment(llvm::Triple::Simulator);
      }
      add_triple(triple);
      break;
    }
    default:
      break;
    }

    offset = cmd_offset + load_cmd.cmdsize;
  }

  // See if there are LC_BUILD_VERSION load commands that can give
  // us the OS type.
  offset = lc_offset;
  for (uint32_t i = 0; i < header.ncmds; ++i) {
    const lldb::offset_t cmd_offset = offset;
    if (data.GetU32(&offset, &load_cmd, 2) == nullptr)
      break;

    do {
      if (load_cmd.cmd == llvm::MachO::LC_BUILD_VERSION) {
        llvm::MachO::build_version_command build_version;
        if (load_cmd.cmdsize < sizeof(build_version)) {
          // Malformed load command.
          break;
        }
        if (data.ExtractBytes(cmd_offset, sizeof(build_version),
                              data.GetByteOrder(), &build_version) == 0)
          break;
        MinOS min_os(build_version.minos);
        OSEnv os_env(build_version.platform);
        llvm::SmallString<16> os_name;
        llvm::raw_svector_ostream os(os_name);
        os << os_env.os_type << min_os.major_version << '.'
           << min_os.minor_version << '.' << min_os.patch_version;
        auto triple = base_triple;
        triple.setOSName(os.str());
        os_name.clear();
        if (!os_env.environment.empty())
          triple.setEnvironmentName(os_env.environment);
        add_triple(triple);
      }
    } while (false);
    offset = cmd_offset + load_cmd.cmdsize;
  }

  if (!found_any) {
    add_triple(base_triple);
  }
}

ArchSpec ObjectFileMachO::GetArchitecture(
    ModuleSP module_sp, const llvm::MachO::mach_header &header,
    const lldb_private::DataExtractor &data, lldb::offset_t lc_offset) {
  ModuleSpecList all_specs;
  ModuleSpec base_spec;
  GetAllArchSpecs(header, data, MachHeaderSizeFromMagic(header.magic),
                  base_spec, all_specs);

  // If the object file offers multiple alternative load commands,
  // pick the one that matches the module.
  if (module_sp) {
    const ArchSpec &module_arch = module_sp->GetArchitecture();
    for (unsigned i = 0, e = all_specs.GetSize(); i != e; ++i) {
      ArchSpec mach_arch =
          all_specs.GetModuleSpecRefAtIndex(i).GetArchitecture();
      if (module_arch.IsCompatibleMatch(mach_arch))
        return mach_arch;
    }
  }

  // Return the first arch we found.
  if (all_specs.GetSize() == 0)
    return {};
  return all_specs.GetModuleSpecRefAtIndex(0).GetArchitecture();
}

UUID ObjectFileMachO::GetUUID() {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
    return GetUUID(m_header, m_data, offset);
  }
  return UUID();
}

uint32_t ObjectFileMachO::GetDependentModules(FileSpecList &files) {
  uint32_t count = 0;
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    llvm::MachO::load_command load_cmd;
    lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
    std::vector<std::string> rpath_paths;
    std::vector<std::string> rpath_relative_paths;
    std::vector<std::string> at_exec_relative_paths;
    uint32_t i;
    for (i = 0; i < m_header.ncmds; ++i) {
      const uint32_t cmd_offset = offset;
      if (m_data.GetU32(&offset, &load_cmd, 2) == nullptr)
        break;

      switch (load_cmd.cmd) {
      case LC_RPATH:
      case LC_LOAD_DYLIB:
      case LC_LOAD_WEAK_DYLIB:
      case LC_REEXPORT_DYLIB:
      case LC_LOAD_DYLINKER:
      case LC_LOADFVMLIB:
      case LC_LOAD_UPWARD_DYLIB: {
        uint32_t name_offset = cmd_offset + m_data.GetU32(&offset);
        // For LC_LOAD_DYLIB there is an alternate encoding
        // which adds a uint32_t `flags` field for `DYLD_USE_*`
        // flags.  This can be detected by a timestamp field with
        // the `DYLIB_USE_MARKER` constant value.
        bool is_delayed_init = false;
        uint32_t use_command_marker = m_data.GetU32(&offset);
        if (use_command_marker == 0x1a741800 /* DYLIB_USE_MARKER */) {
          offset += 4; /* uint32_t current_version */
          offset += 4; /* uint32_t compat_version */
          uint32_t flags = m_data.GetU32(&offset);
          // If this LC_LOAD_DYLIB is marked delay-init,
          // don't report it as a dependent library -- it
          // may be loaded in the process at some point,
          // but will most likely not be load at launch.
          if (flags & 0x08 /* DYLIB_USE_DELAYED_INIT */)
            is_delayed_init = true;
        }
        const char *path = m_data.PeekCStr(name_offset);
        if (path && !is_delayed_init) {
          if (load_cmd.cmd == LC_RPATH)
            rpath_paths.push_back(path);
          else {
            if (path[0] == '@') {
              if (strncmp(path, "@rpath", strlen("@rpath")) == 0)
                rpath_relative_paths.push_back(path + strlen("@rpath"));
              else if (strncmp(path, "@executable_path",
                               strlen("@executable_path")) == 0)
                at_exec_relative_paths.push_back(path +
                                                 strlen("@executable_path"));
            } else {
              FileSpec file_spec(path);
              if (files.AppendIfUnique(file_spec))
                count++;
            }
          }
        }
      } break;

      default:
        break;
      }
      offset = cmd_offset + load_cmd.cmdsize;
    }

    FileSpec this_file_spec(m_file);
    FileSystem::Instance().Resolve(this_file_spec);

    if (!rpath_paths.empty()) {
      // Fixup all LC_RPATH values to be absolute paths
      std::string loader_path("@loader_path");
      std::string executable_path("@executable_path");
      for (auto &rpath : rpath_paths) {
        if (llvm::StringRef(rpath).starts_with(loader_path)) {
          rpath.erase(0, loader_path.size());
          rpath.insert(0, this_file_spec.GetDirectory().GetCString());
        } else if (llvm::StringRef(rpath).starts_with(executable_path)) {
          rpath.erase(0, executable_path.size());
          rpath.insert(0, this_file_spec.GetDirectory().GetCString());
        }
      }

      for (const auto &rpath_relative_path : rpath_relative_paths) {
        for (const auto &rpath : rpath_paths) {
          std::string path = rpath;
          path += rpath_relative_path;
          // It is OK to resolve this path because we must find a file on disk
          // for us to accept it anyway if it is rpath relative.
          FileSpec file_spec(path);
          FileSystem::Instance().Resolve(file_spec);
          if (FileSystem::Instance().Exists(file_spec) &&
              files.AppendIfUnique(file_spec)) {
            count++;
            break;
          }
        }
      }
    }

    // We may have @executable_paths but no RPATHS.  Figure those out here.
    // Only do this if this object file is the executable.  We have no way to
    // get back to the actual executable otherwise, so we won't get the right
    // path.
    if (!at_exec_relative_paths.empty() && CalculateType() == eTypeExecutable) {
      FileSpec exec_dir = this_file_spec.CopyByRemovingLastPathComponent();
      for (const auto &at_exec_relative_path : at_exec_relative_paths) {
        FileSpec file_spec =
            exec_dir.CopyByAppendingPathComponent(at_exec_relative_path);
        if (FileSystem::Instance().Exists(file_spec) &&
            files.AppendIfUnique(file_spec))
          count++;
      }
    }
  }
  return count;
}

lldb_private::Address ObjectFileMachO::GetEntryPointAddress() {
  // If the object file is not an executable it can't hold the entry point.
  // m_entry_point_address is initialized to an invalid address, so we can just
  // return that. If m_entry_point_address is valid it means we've found it
  // already, so return the cached value.

  if ((!IsExecutable() && !IsDynamicLoader()) ||
      m_entry_point_address.IsValid()) {
    return m_entry_point_address;
  }

  // Otherwise, look for the UnixThread or Thread command.  The data for the
  // Thread command is given in /usr/include/mach-o.h, but it is basically:
  //
  //  uint32_t flavor  - this is the flavor argument you would pass to
  //  thread_get_state
  //  uint32_t count   - this is the count of longs in the thread state data
  //  struct XXX_thread_state state - this is the structure from
  //  <machine/thread_status.h> corresponding to the flavor.
  //  <repeat this trio>
  //
  // So we just keep reading the various register flavors till we find the GPR
  // one, then read the PC out of there.
  // FIXME: We will need to have a "RegisterContext data provider" class at some
  // point that can get all the registers
  // out of data in this form & attach them to a given thread.  That should
  // underlie the MacOS X User process plugin, and we'll also need it for the
  // MacOS X Core File process plugin.  When we have that we can also use it
  // here.
  //
  // For now we hard-code the offsets and flavors we need:
  //
  //

  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    llvm::MachO::load_command load_cmd;
    lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
    uint32_t i;
    lldb::addr_t start_address = LLDB_INVALID_ADDRESS;
    bool done = false;

    for (i = 0; i < m_header.ncmds; ++i) {
      const lldb::offset_t cmd_offset = offset;
      if (m_data.GetU32(&offset, &load_cmd, 2) == nullptr)
        break;

      switch (load_cmd.cmd) {
      case LC_UNIXTHREAD:
      case LC_THREAD: {
        while (offset < cmd_offset + load_cmd.cmdsize) {
          uint32_t flavor = m_data.GetU32(&offset);
          uint32_t count = m_data.GetU32(&offset);
          if (count == 0) {
            // We've gotten off somehow, log and exit;
            return m_entry_point_address;
          }

          switch (m_header.cputype) {
          case llvm::MachO::CPU_TYPE_ARM:
            if (flavor == 1 ||
                flavor == 9) // ARM_THREAD_STATE/ARM_THREAD_STATE32
                             // from mach/arm/thread_status.h
            {
              offset += 60; // This is the offset of pc in the GPR thread state
                            // data structure.
              start_address = m_data.GetU32(&offset);
              done = true;
            }
            break;
          case llvm::MachO::CPU_TYPE_ARM64:
          case llvm::MachO::CPU_TYPE_ARM64_32:
            if (flavor == 6) // ARM_THREAD_STATE64 from mach/arm/thread_status.h
            {
              offset += 256; // This is the offset of pc in the GPR thread state
                             // data structure.
              start_address = m_data.GetU64(&offset);
              done = true;
            }
            break;
          case llvm::MachO::CPU_TYPE_I386:
            if (flavor ==
                1) // x86_THREAD_STATE32 from mach/i386/thread_status.h
            {
              offset += 40; // This is the offset of eip in the GPR thread state
                            // data structure.
              start_address = m_data.GetU32(&offset);
              done = true;
            }
            break;
          case llvm::MachO::CPU_TYPE_X86_64:
            if (flavor ==
                4) // x86_THREAD_STATE64 from mach/i386/thread_status.h
            {
              offset += 16 * 8; // This is the offset of rip in the GPR thread
                                // state data structure.
              start_address = m_data.GetU64(&offset);
              done = true;
            }
            break;
          default:
            return m_entry_point_address;
          }
          // Haven't found the GPR flavor yet, skip over the data for this
          // flavor:
          if (done)
            break;
          offset += count * 4;
        }
      } break;
      case LC_MAIN: {
        uint64_t entryoffset = m_data.GetU64(&offset);
        SectionSP text_segment_sp =
            GetSectionList()->FindSectionByName(GetSegmentNameTEXT());
        if (text_segment_sp) {
          done = true;
          start_address = text_segment_sp->GetFileAddress() + entryoffset;
        }
      } break;

      default:
        break;
      }
      if (done)
        break;

      // Go to the next load command:
      offset = cmd_offset + load_cmd.cmdsize;
    }

    if (start_address == LLDB_INVALID_ADDRESS && IsDynamicLoader()) {
      if (GetSymtab()) {
        Symbol *dyld_start_sym = GetSymtab()->FindFirstSymbolWithNameAndType(
            ConstString("_dyld_start"), SymbolType::eSymbolTypeCode,
            Symtab::eDebugAny, Symtab::eVisibilityAny);
        if (dyld_start_sym && dyld_start_sym->GetAddress().IsValid()) {
          start_address = dyld_start_sym->GetAddress().GetFileAddress();
        }
      }
    }

    if (start_address != LLDB_INVALID_ADDRESS) {
      // We got the start address from the load commands, so now resolve that
      // address in the sections of this ObjectFile:
      if (!m_entry_point_address.ResolveAddressUsingFileSections(
              start_address, GetSectionList())) {
        m_entry_point_address.Clear();
      }
    } else {
      // We couldn't read the UnixThread load command - maybe it wasn't there.
      // As a fallback look for the "start" symbol in the main executable.

      ModuleSP module_sp(GetModule());

      if (module_sp) {
        SymbolContextList contexts;
        SymbolContext context;
        module_sp->FindSymbolsWithNameAndType(ConstString("start"),
                                              eSymbolTypeCode, contexts);
        if (contexts.GetSize()) {
          if (contexts.GetContextAtIndex(0, context))
            m_entry_point_address = context.symbol->GetAddress();
        }
      }
    }
  }

  return m_entry_point_address;
}

lldb_private::Address ObjectFileMachO::GetBaseAddress() {
  lldb_private::Address header_addr;
  SectionList *section_list = GetSectionList();
  if (section_list) {
    SectionSP text_segment_sp(
        section_list->FindSectionByName(GetSegmentNameTEXT()));
    if (text_segment_sp) {
      header_addr.SetSection(text_segment_sp);
      header_addr.SetOffset(0);
    }
  }
  return header_addr;
}

uint32_t ObjectFileMachO::GetNumThreadContexts() {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (!m_thread_context_offsets_valid) {
      m_thread_context_offsets_valid = true;
      lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
      FileRangeArray::Entry file_range;
      llvm::MachO::thread_command thread_cmd;
      for (uint32_t i = 0; i < m_header.ncmds; ++i) {
        const uint32_t cmd_offset = offset;
        if (m_data.GetU32(&offset, &thread_cmd, 2) == nullptr)
          break;

        if (thread_cmd.cmd == LC_THREAD) {
          file_range.SetRangeBase(offset);
          file_range.SetByteSize(thread_cmd.cmdsize - 8);
          m_thread_context_offsets.Append(file_range);
        }
        offset = cmd_offset + thread_cmd.cmdsize;
      }
    }
  }
  return m_thread_context_offsets.GetSize();
}

std::vector<std::tuple<offset_t, offset_t>>
ObjectFileMachO::FindLC_NOTEByName(std::string name) {
  std::vector<std::tuple<offset_t, offset_t>> results;
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());

    offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
    for (uint32_t i = 0; i < m_header.ncmds; ++i) {
      const uint32_t cmd_offset = offset;
      llvm::MachO::load_command lc = {};
      if (m_data.GetU32(&offset, &lc.cmd, 2) == nullptr)
        break;
      if (lc.cmd == LC_NOTE) {
        char data_owner[17];
        m_data.CopyData(offset, 16, data_owner);
        data_owner[16] = '\0';
        offset += 16;

        if (name == data_owner) {
          offset_t payload_offset = m_data.GetU64_unchecked(&offset);
          offset_t payload_size = m_data.GetU64_unchecked(&offset);
          results.push_back({payload_offset, payload_size});
        }
      }
      offset = cmd_offset + lc.cmdsize;
    }
  }
  return results;
}

std::string ObjectFileMachO::GetIdentifierString() {
  Log *log(
      GetLog(LLDBLog::Symbols | LLDBLog::Process | LLDBLog::DynamicLoader));
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());

    auto lc_notes = FindLC_NOTEByName("kern ver str");
    for (auto lc_note : lc_notes) {
      offset_t payload_offset = std::get<0>(lc_note);
      offset_t payload_size = std::get<1>(lc_note);
      uint32_t version;
      if (m_data.GetU32(&payload_offset, &version, 1) != nullptr) {
        if (version == 1) {
          uint32_t strsize = payload_size - sizeof(uint32_t);
          std::string result(strsize, '\0');
          m_data.CopyData(payload_offset, strsize, result.data());
          LLDB_LOGF(log, "LC_NOTE 'kern ver str' found with text '%s'",
                    result.c_str());
          return result;
        }
      }
    }

    // Second, make a pass over the load commands looking for an obsolete
    // LC_IDENT load command.
    offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
    for (uint32_t i = 0; i < m_header.ncmds; ++i) {
      const uint32_t cmd_offset = offset;
      llvm::MachO::ident_command ident_command;
      if (m_data.GetU32(&offset, &ident_command, 2) == nullptr)
        break;
      if (ident_command.cmd == LC_IDENT && ident_command.cmdsize != 0) {
        std::string result(ident_command.cmdsize, '\0');
        if (m_data.CopyData(offset, ident_command.cmdsize, result.data()) ==
            ident_command.cmdsize) {
          LLDB_LOGF(log, "LC_IDENT found with text '%s'", result.c_str());
          return result;
        }
      }
      offset = cmd_offset + ident_command.cmdsize;
    }
  }
  return {};
}

AddressableBits ObjectFileMachO::GetAddressableBits() {
  AddressableBits addressable_bits;

  Log *log(GetLog(LLDBLog::Process));
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    auto lc_notes = FindLC_NOTEByName("addrable bits");
    for (auto lc_note : lc_notes) {
      offset_t payload_offset = std::get<0>(lc_note);
      uint32_t version;
      if (m_data.GetU32(&payload_offset, &version, 1) != nullptr) {
        if (version == 3) {
          uint32_t num_addr_bits = m_data.GetU32_unchecked(&payload_offset);
          addressable_bits.SetAddressableBits(num_addr_bits);
          LLDB_LOGF(log,
                    "LC_NOTE 'addrable bits' v3 found, value %d "
                    "bits",
                    num_addr_bits);
        }
        if (version == 4) {
          uint32_t lo_addr_bits = m_data.GetU32_unchecked(&payload_offset);
          uint32_t hi_addr_bits = m_data.GetU32_unchecked(&payload_offset);

          if (lo_addr_bits == hi_addr_bits)
            addressable_bits.SetAddressableBits(lo_addr_bits);
          else
            addressable_bits.SetAddressableBits(lo_addr_bits, hi_addr_bits);
          LLDB_LOGF(log, "LC_NOTE 'addrable bits' v4 found, value %d & %d bits",
                    lo_addr_bits, hi_addr_bits);
        }
      }
    }
  }
  return addressable_bits;
}

bool ObjectFileMachO::GetCorefileMainBinaryInfo(addr_t &value,
                                                bool &value_is_offset,
                                                UUID &uuid,
                                                ObjectFile::BinaryType &type) {
  Log *log(
      GetLog(LLDBLog::Symbols | LLDBLog::Process | LLDBLog::DynamicLoader));
  value = LLDB_INVALID_ADDRESS;
  value_is_offset = false;
  uuid.Clear();
  uint32_t log2_pagesize = 0; // not currently passed up to caller
  uint32_t platform = 0;      // not currently passed up to caller
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());

    auto lc_notes = FindLC_NOTEByName("main bin spec");
    for (auto lc_note : lc_notes) {
      offset_t payload_offset = std::get<0>(lc_note);

      // struct main_bin_spec
      // {
      //     uint32_t version;       // currently 2
      //     uint32_t type;          // 0 == unspecified, 1 == kernel,
      //                             // 2 == user process,
      //                             // 3 == standalone binary
      //     uint64_t address;       // UINT64_MAX if address not specified
      //     uint64_t slide;         // slide, UINT64_MAX if unspecified
      //                             // 0 if no slide needs to be applied to
      //                             // file address
      //     uuid_t   uuid;          // all zero's if uuid not specified
      //     uint32_t log2_pagesize; // process page size in log base 2,
      //                             // e.g. 4k pages are 12.
      //                             // 0 for unspecified
      //     uint32_t platform;      // The Mach-O platform for this corefile.
      //                             // 0 for unspecified.
      //                             // The values are defined in
      //                             // <mach-o/loader.h>, PLATFORM_*.
      // } __attribute((packed));

      // "main bin spec" (main binary specification) data payload is
      // formatted:
      //    uint32_t version       [currently 1]
      //    uint32_t type          [0 == unspecified, 1 == kernel,
      //                            2 == user process, 3 == firmware ]
      //    uint64_t address       [ UINT64_MAX if address not specified ]
      //    uuid_t   uuid          [ all zero's if uuid not specified ]
      //    uint32_t log2_pagesize [ process page size in log base
      //                             2, e.g. 4k pages are 12.
      //                             0 for unspecified ]
      //    uint32_t unused        [ for alignment ]

      uint32_t version;
      if (m_data.GetU32(&payload_offset, &version, 1) != nullptr &&
          version <= 2) {
        uint32_t binspec_type = 0;
        uuid_t raw_uuid;
        memset(raw_uuid, 0, sizeof(uuid_t));

        if (!m_data.GetU32(&payload_offset, &binspec_type, 1))
          return false;
        if (!m_data.GetU64(&payload_offset, &value, 1))
          return false;
        uint64_t slide = LLDB_INVALID_ADDRESS;
        if (version > 1 && !m_data.GetU64(&payload_offset, &slide, 1))
          return false;
        if (value == LLDB_INVALID_ADDRESS && slide != LLDB_INVALID_ADDRESS) {
          value = slide;
          value_is_offset = true;
        }

        if (m_data.CopyData(payload_offset, sizeof(uuid_t), raw_uuid) != 0) {
          uuid = UUID(raw_uuid, sizeof(uuid_t));
          // convert the "main bin spec" type into our
          // ObjectFile::BinaryType enum
          const char *typestr = "unrecognized type";
          switch (binspec_type) {
          case 0:
            type = eBinaryTypeUnknown;
            typestr = "uknown";
            break;
          case 1:
            type = eBinaryTypeKernel;
            typestr = "xnu kernel";
            break;
          case 2:
            type = eBinaryTypeUser;
            typestr = "userland dyld";
            break;
          case 3:
            type = eBinaryTypeStandalone;
            typestr = "standalone";
            break;
          }
          LLDB_LOGF(log,
                    "LC_NOTE 'main bin spec' found, version %d type %d "
                    "(%s), value 0x%" PRIx64 " value-is-slide==%s uuid %s",
                    version, type, typestr, value,
                    value_is_offset ? "true" : "false",
                    uuid.GetAsString().c_str());
          if (!m_data.GetU32(&payload_offset, &log2_pagesize, 1))
            return false;
          if (version > 1 && !m_data.GetU32(&payload_offset, &platform, 1))
            return false;
          return true;
        }
      }
    }
  }
  return false;
}

bool ObjectFileMachO::GetCorefileThreadExtraInfos(std::vector<tid_t> &tids) {
  tids.clear();
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());

    Log *log(GetLog(LLDBLog::Object | LLDBLog::Process | LLDBLog::Thread));
    auto lc_notes = FindLC_NOTEByName("process metadata");
    for (auto lc_note : lc_notes) {
      offset_t payload_offset = std::get<0>(lc_note);
      offset_t strsize = std::get<1>(lc_note);
      std::string buf(strsize, '\0');
      if (m_data.CopyData(payload_offset, strsize, buf.data()) != strsize) {
        LLDB_LOGF(log,
                  "Unable to read %" PRIu64
                  " bytes of 'process metadata' LC_NOTE JSON contents",
                  strsize);
        return false;
      }
      while (buf.back() == '\0')
        buf.resize(buf.size() - 1);
      StructuredData::ObjectSP object_sp = StructuredData::ParseJSON(buf);
      StructuredData::Dictionary *dict = object_sp->GetAsDictionary();
      if (!dict) {
        LLDB_LOGF(log, "Unable to read 'process metadata' LC_NOTE, did not "
                       "get a dictionary.");
        return false;
      }
      StructuredData::Array *threads;
      if (!dict->GetValueForKeyAsArray("threads", threads) || !threads) {
        LLDB_LOGF(log,
                  "'process metadata' LC_NOTE does not have a 'threads' key");
        return false;
      }
      if (threads->GetSize() != GetNumThreadContexts()) {
        LLDB_LOGF(log, "Unable to read 'process metadata' LC_NOTE, number of "
                       "threads does not match number of LC_THREADS.");
        return false;
      }
      const size_t num_threads = threads->GetSize();
      for (size_t i = 0; i < num_threads; i++) {
        std::optional<StructuredData::Dictionary *> maybe_thread =
            threads->GetItemAtIndexAsDictionary(i);
        if (!maybe_thread) {
          LLDB_LOGF(log,
                    "Unable to read 'process metadata' LC_NOTE, threads "
                    "array does not have a dictionary at index %zu.",
                    i);
          return false;
        }
        StructuredData::Dictionary *thread = *maybe_thread;
        tid_t tid = LLDB_INVALID_THREAD_ID;
        if (thread->GetValueForKeyAsInteger<tid_t>("thread_id", tid))
          if (tid == 0)
            tid = LLDB_INVALID_THREAD_ID;
        tids.push_back(tid);
      }

      if (log) {
        StreamString logmsg;
        logmsg.Printf("LC_NOTE 'process metadata' found: ");
        dict->Dump(logmsg, /* pretty_print */ false);
        LLDB_LOGF(log, "%s", logmsg.GetData());
      }
      return true;
    }
  }
  return false;
}

lldb::RegisterContextSP
ObjectFileMachO::GetThreadContextAtIndex(uint32_t idx,
                                         lldb_private::Thread &thread) {
  lldb::RegisterContextSP reg_ctx_sp;

  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (!m_thread_context_offsets_valid)
      GetNumThreadContexts();

    const FileRangeArray::Entry *thread_context_file_range =
        m_thread_context_offsets.GetEntryAtIndex(idx);
    if (thread_context_file_range) {

      DataExtractor data(m_data, thread_context_file_range->GetRangeBase(),
                         thread_context_file_range->GetByteSize());

      switch (m_header.cputype) {
      case llvm::MachO::CPU_TYPE_ARM64:
      case llvm::MachO::CPU_TYPE_ARM64_32:
        reg_ctx_sp =
            std::make_shared<RegisterContextDarwin_arm64_Mach>(thread, data);
        break;

      case llvm::MachO::CPU_TYPE_ARM:
        reg_ctx_sp =
            std::make_shared<RegisterContextDarwin_arm_Mach>(thread, data);
        break;

      case llvm::MachO::CPU_TYPE_I386:
        reg_ctx_sp =
            std::make_shared<RegisterContextDarwin_i386_Mach>(thread, data);
        break;

      case llvm::MachO::CPU_TYPE_X86_64:
        reg_ctx_sp =
            std::make_shared<RegisterContextDarwin_x86_64_Mach>(thread, data);
        break;
      }
    }
  }
  return reg_ctx_sp;
}

ObjectFile::Type ObjectFileMachO::CalculateType() {
  switch (m_header.filetype) {
  case MH_OBJECT: // 0x1u
    if (GetAddressByteSize() == 4) {
      // 32 bit kexts are just object files, but they do have a valid
      // UUID load command.
      if (GetUUID()) {
        // this checking for the UUID load command is not enough we could
        // eventually look for the symbol named "OSKextGetCurrentIdentifier" as
        // this is required of kexts
        if (m_strata == eStrataInvalid)
          m_strata = eStrataKernel;
        return eTypeSharedLibrary;
      }
    }
    return eTypeObjectFile;

  case MH_EXECUTE:
    return eTypeExecutable; // 0x2u
  case MH_FVMLIB:
    return eTypeSharedLibrary; // 0x3u
  case MH_CORE:
    return eTypeCoreFile; // 0x4u
  case MH_PRELOAD:
    return eTypeSharedLibrary; // 0x5u
  case MH_DYLIB:
    return eTypeSharedLibrary; // 0x6u
  case MH_DYLINKER:
    return eTypeDynamicLinker; // 0x7u
  case MH_BUNDLE:
    return eTypeSharedLibrary; // 0x8u
  case MH_DYLIB_STUB:
    return eTypeStubLibrary; // 0x9u
  case MH_DSYM:
    return eTypeDebugInfo; // 0xAu
  case MH_KEXT_BUNDLE:
    return eTypeSharedLibrary; // 0xBu
  default:
    break;
  }
  return eTypeUnknown;
}

ObjectFile::Strata ObjectFileMachO::CalculateStrata() {
  switch (m_header.filetype) {
  case MH_OBJECT: // 0x1u
  {
    // 32 bit kexts are just object files, but they do have a valid
    // UUID load command.
    if (GetUUID()) {
      // this checking for the UUID load command is not enough we could
      // eventually look for the symbol named "OSKextGetCurrentIdentifier" as
      // this is required of kexts
      if (m_type == eTypeInvalid)
        m_type = eTypeSharedLibrary;

      return eStrataKernel;
    }
  }
    return eStrataUnknown;

  case MH_EXECUTE: // 0x2u
    // Check for the MH_DYLDLINK bit in the flags
    if (m_header.flags & MH_DYLDLINK) {
      return eStrataUser;
    } else {
      SectionList *section_list = GetSectionList();
      if (section_list) {
        static ConstString g_kld_section_name("__KLD");
        if (section_list->FindSectionByName(g_kld_section_name))
          return eStrataKernel;
      }
    }
    return eStrataRawImage;

  case MH_FVMLIB:
    return eStrataUser; // 0x3u
  case MH_CORE:
    return eStrataUnknown; // 0x4u
  case MH_PRELOAD:
    return eStrataRawImage; // 0x5u
  case MH_DYLIB:
    return eStrataUser; // 0x6u
  case MH_DYLINKER:
    return eStrataUser; // 0x7u
  case MH_BUNDLE:
    return eStrataUser; // 0x8u
  case MH_DYLIB_STUB:
    return eStrataUser; // 0x9u
  case MH_DSYM:
    return eStrataUnknown; // 0xAu
  case MH_KEXT_BUNDLE:
    return eStrataKernel; // 0xBu
  default:
    break;
  }
  return eStrataUnknown;
}

llvm::VersionTuple ObjectFileMachO::GetVersion() {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    llvm::MachO::dylib_command load_cmd;
    lldb::offset_t offset = MachHeaderSizeFromMagic(m_header.magic);
    uint32_t version_cmd = 0;
    uint64_t version = 0;
    uint32_t i;
    for (i = 0; i < m_header.ncmds; ++i) {
      const lldb::offset_t cmd_offset = offset;
      if (m_data.GetU32(&offset, &load_cmd, 2) == nullptr)
        break;

      if (load_cmd.cmd == LC_ID_DYLIB) {
        if (version_cmd == 0) {
          version_cmd = load_cmd.cmd;
          if (m_data.GetU32(&offset, &load_cmd.dylib, 4) == nullptr)
            break;
          version = load_cmd.dylib.current_version;
        }
        break; // Break for now unless there is another more complete version
               // number load command in the future.
      }
      offset = cmd_offset + load_cmd.cmdsize;
    }

    if (version_cmd == LC_ID_DYLIB) {
      unsigned major = (version & 0xFFFF0000ull) >> 16;
      unsigned minor = (version & 0x0000FF00ull) >> 8;
      unsigned subminor = (version & 0x000000FFull);
      return llvm::VersionTuple(major, minor, subminor);
    }
  }
  return llvm::VersionTuple();
}

ArchSpec ObjectFileMachO::GetArchitecture() {
  ModuleSP module_sp(GetModule());
  ArchSpec arch;
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());

    return GetArchitecture(module_sp, m_header, m_data,
                           MachHeaderSizeFromMagic(m_header.magic));
  }
  return arch;
}

void ObjectFileMachO::GetProcessSharedCacheUUID(Process *process,
                                                addr_t &base_addr, UUID &uuid) {
  uuid.Clear();
  base_addr = LLDB_INVALID_ADDRESS;
  if (process && process->GetDynamicLoader()) {
    DynamicLoader *dl = process->GetDynamicLoader();
    LazyBool using_shared_cache;
    LazyBool private_shared_cache;
    dl->GetSharedCacheInformation(base_addr, uuid, using_shared_cache,
                                  private_shared_cache);
  }
  Log *log(GetLog(LLDBLog::Symbols | LLDBLog::Process));
  LLDB_LOGF(
      log,
      "inferior process shared cache has a UUID of %s, base address 0x%" PRIx64,
      uuid.GetAsString().c_str(), base_addr);
}

// From dyld SPI header dyld_process_info.h
typedef void *dyld_process_info;
struct lldb_copy__dyld_process_cache_info {
  uuid_t cacheUUID;          // UUID of cache used by process
  uint64_t cacheBaseAddress; // load address of dyld shared cache
  bool noCache;              // process is running without a dyld cache
  bool privateCache; // process is using a private copy of its dyld cache
};

// #including mach/mach.h pulls in machine.h & CPU_TYPE_ARM etc conflicts with
// llvm enum definitions llvm::MachO::CPU_TYPE_ARM turning them into compile
// errors. So we need to use the actual underlying types of task_t and
// kern_return_t below.
extern "C" unsigned int /*task_t*/ mach_task_self();

void ObjectFileMachO::GetLLDBSharedCacheUUID(addr_t &base_addr, UUID &uuid) {
  uuid.Clear();
  base_addr = LLDB_INVALID_ADDRESS;

#if defined(__APPLE__)
  uint8_t *(*dyld_get_all_image_infos)(void);
  dyld_get_all_image_infos =
      (uint8_t * (*)()) dlsym(RTLD_DEFAULT, "_dyld_get_all_image_infos");
  if (dyld_get_all_image_infos) {
    uint8_t *dyld_all_image_infos_address = dyld_get_all_image_infos();
    if (dyld_all_image_infos_address) {
      uint32_t *version = (uint32_t *)
          dyld_all_image_infos_address; // version <mach-o/dyld_images.h>
      if (*version >= 13) {
        uuid_t *sharedCacheUUID_address = 0;
        int wordsize = sizeof(uint8_t *);
        if (wordsize == 8) {
          sharedCacheUUID_address =
              (uuid_t *)((uint8_t *)dyld_all_image_infos_address +
                         160); // sharedCacheUUID <mach-o/dyld_images.h>
          if (*version >= 15)
            base_addr =
                *(uint64_t
                      *)((uint8_t *)dyld_all_image_infos_address +
                         176); // sharedCacheBaseAddress <mach-o/dyld_images.h>
        } else {
          sharedCacheUUID_address =
              (uuid_t *)((uint8_t *)dyld_all_image_infos_address +
                         84); // sharedCacheUUID <mach-o/dyld_images.h>
          if (*version >= 15) {
            base_addr = 0;
            base_addr =
                *(uint32_t
                      *)((uint8_t *)dyld_all_image_infos_address +
                         100); // sharedCacheBaseAddress <mach-o/dyld_images.h>
          }
        }
        uuid = UUID(sharedCacheUUID_address, sizeof(uuid_t));
      }
    }
  } else {
    // Exists in macOS 10.12 and later, iOS 10.0 and later - dyld SPI
    dyld_process_info (*dyld_process_info_create)(
        unsigned int /* task_t */ task, uint64_t timestamp,
        unsigned int /*kern_return_t*/ *kernelError);
    void (*dyld_process_info_get_cache)(void *info, void *cacheInfo);
    void (*dyld_process_info_release)(dyld_process_info info);

    dyld_process_info_create = (void *(*)(unsigned int /* task_t */, uint64_t,
                                          unsigned int /*kern_return_t*/ *))
        dlsym(RTLD_DEFAULT, "_dyld_process_info_create");
    dyld_process_info_get_cache = (void (*)(void *, void *))dlsym(
        RTLD_DEFAULT, "_dyld_process_info_get_cache");
    dyld_process_info_release =
        (void (*)(void *))dlsym(RTLD_DEFAULT, "_dyld_process_info_release");

    if (dyld_process_info_create && dyld_process_info_get_cache) {
      unsigned int /*kern_return_t */ kern_ret;
      dyld_process_info process_info =
          dyld_process_info_create(::mach_task_self(), 0, &kern_ret);
      if (process_info) {
        struct lldb_copy__dyld_process_cache_info sc_info;
        memset(&sc_info, 0, sizeof(struct lldb_copy__dyld_process_cache_info));
        dyld_process_info_get_cache(process_info, &sc_info);
        if (sc_info.cacheBaseAddress != 0) {
          base_addr = sc_info.cacheBaseAddress;
          uuid = UUID(sc_info.cacheUUID, sizeof(uuid_t));
        }
        dyld_process_info_release(process_info);
      }
    }
  }
  Log *log(GetLog(LLDBLog::Symbols | LLDBLog::Process));
  if (log && uuid.IsValid())
    LLDB_LOGF(log,
              "lldb's in-memory shared cache has a UUID of %s base address of "
              "0x%" PRIx64,
              uuid.GetAsString().c_str(), base_addr);
#endif
}

static llvm::VersionTuple FindMinimumVersionInfo(DataExtractor &data,
                                                 lldb::offset_t offset,
                                                 size_t ncmds) {
  for (size_t i = 0; i < ncmds; i++) {
    const lldb::offset_t load_cmd_offset = offset;
    llvm::MachO::load_command lc = {};
    if (data.GetU32(&offset, &lc.cmd, 2) == nullptr)
      break;

    uint32_t version = 0;
    if (lc.cmd == llvm::MachO::LC_VERSION_MIN_MACOSX ||
        lc.cmd == llvm::MachO::LC_VERSION_MIN_IPHONEOS ||
        lc.cmd == llvm::MachO::LC_VERSION_MIN_TVOS ||
        lc.cmd == llvm::MachO::LC_VERSION_MIN_WATCHOS) {
      // struct version_min_command {
      //   uint32_t cmd; // LC_VERSION_MIN_*
      //   uint32_t cmdsize;
      //   uint32_t version; // X.Y.Z encoded in nibbles xxxx.yy.zz
      //   uint32_t sdk;
      // };
      // We want to read version.
      version = data.GetU32(&offset);
    } else if (lc.cmd == llvm::MachO::LC_BUILD_VERSION) {
      // struct build_version_command {
      //   uint32_t cmd; // LC_BUILD_VERSION
      //   uint32_t cmdsize;
      //   uint32_t platform;
      //   uint32_t minos; // X.Y.Z encoded in nibbles xxxx.yy.zz
      //   uint32_t sdk;
      //   uint32_t ntools;
      // };
      // We want to read minos.
      offset += sizeof(uint32_t);     // Skip over platform
      version = data.GetU32(&offset); // Extract minos
    }

    if (version) {
      const uint32_t xxxx = version >> 16;
      const uint32_t yy = (version >> 8) & 0xffu;
      const uint32_t zz = version & 0xffu;
      if (xxxx)
        return llvm::VersionTuple(xxxx, yy, zz);
    }
    offset = load_cmd_offset + lc.cmdsize;
  }
  return llvm::VersionTuple();
}

llvm::VersionTuple ObjectFileMachO::GetMinimumOSVersion() {
  if (!m_min_os_version)
    m_min_os_version = FindMinimumVersionInfo(
        m_data, MachHeaderSizeFromMagic(m_header.magic), m_header.ncmds);
  return *m_min_os_version;
}

llvm::VersionTuple ObjectFileMachO::GetSDKVersion() {
  if (!m_sdk_versions)
    m_sdk_versions = FindMinimumVersionInfo(
        m_data, MachHeaderSizeFromMagic(m_header.magic), m_header.ncmds);
  return *m_sdk_versions;
}

bool ObjectFileMachO::GetIsDynamicLinkEditor() {
  return m_header.filetype == llvm::MachO::MH_DYLINKER;
}

bool ObjectFileMachO::CanTrustAddressRanges() {
  // Dsymutil guarantees that the .debug_aranges accelerator is complete and can
  // be trusted by LLDB.
  return m_header.filetype == llvm::MachO::MH_DSYM;
}

bool ObjectFileMachO::AllowAssemblyEmulationUnwindPlans() {
  return m_allow_assembly_emulation_unwind_plans;
}

Section *ObjectFileMachO::GetMachHeaderSection() {
  // Find the first address of the mach header which is the first non-zero file
  // sized section whose file offset is zero. This is the base file address of
  // the mach-o file which can be subtracted from the vmaddr of the other
  // segments found in memory and added to the load address
  ModuleSP module_sp = GetModule();
  if (!module_sp)
    return nullptr;
  SectionList *section_list = GetSectionList();
  if (!section_list)
    return nullptr;

  // Some binaries can have a TEXT segment with a non-zero file offset.
  // Binaries in the shared cache are one example.  Some hand-generated
  // binaries may not be laid out in the normal TEXT,DATA,LC_SYMTAB order
  // in the file, even though they're laid out correctly in vmaddr terms.
  SectionSP text_segment_sp =
      section_list->FindSectionByName(GetSegmentNameTEXT());
  if (text_segment_sp.get() && SectionIsLoadable(text_segment_sp.get()))
    return text_segment_sp.get();

  const size_t num_sections = section_list->GetSize();
  for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
    Section *section = section_list->GetSectionAtIndex(sect_idx).get();
    if (section->GetFileOffset() == 0 && SectionIsLoadable(section))
      return section;
  }

  return nullptr;
}

bool ObjectFileMachO::SectionIsLoadable(const Section *section) {
  if (!section)
    return false;
  if (section->IsThreadSpecific())
    return false;
  if (GetModule().get() != section->GetModule().get())
    return false;
  // firmware style binaries with llvm gcov segment do
  // not have that segment mapped into memory.
  if (section->GetName() == GetSegmentNameLLVM_COV()) {
    const Strata strata = GetStrata();
    if (strata == eStrataKernel || strata == eStrataRawImage)
      return false;
  }
  // Be careful with __LINKEDIT and __DWARF segments
  if (section->GetName() == GetSegmentNameLINKEDIT() ||
      section->GetName() == GetSegmentNameDWARF()) {
    // Only map __LINKEDIT and __DWARF if we have an in memory image and
    // this isn't a kernel binary like a kext or mach_kernel.
    const bool is_memory_image = (bool)m_process_wp.lock();
    const Strata strata = GetStrata();
    if (is_memory_image == false || strata == eStrataKernel)
      return false;
  }
  return true;
}

lldb::addr_t ObjectFileMachO::CalculateSectionLoadAddressForMemoryImage(
    lldb::addr_t header_load_address, const Section *header_section,
    const Section *section) {
  ModuleSP module_sp = GetModule();
  if (module_sp && header_section && section &&
      header_load_address != LLDB_INVALID_ADDRESS) {
    lldb::addr_t file_addr = header_section->GetFileAddress();
    if (file_addr != LLDB_INVALID_ADDRESS && SectionIsLoadable(section))
      return section->GetFileAddress() - file_addr + header_load_address;
  }
  return LLDB_INVALID_ADDRESS;
}

bool ObjectFileMachO::SetLoadAddress(Target &target, lldb::addr_t value,
                                     bool value_is_offset) {
  Log *log(GetLog(LLDBLog::DynamicLoader));
  ModuleSP module_sp = GetModule();
  if (!module_sp)
    return false;

  SectionList *section_list = GetSectionList();
  if (!section_list)
    return false;

  size_t num_loaded_sections = 0;
  const size_t num_sections = section_list->GetSize();

  // Warn if some top-level segments map to the same address. The binary may be
  // malformed.
  const bool warn_multiple = true;

  if (log) {
    StreamString logmsg;
    logmsg << "ObjectFileMachO::SetLoadAddress ";
    if (GetFileSpec())
      logmsg << "path='" << GetFileSpec().GetPath() << "' ";
    if (GetUUID()) {
      logmsg << "uuid=" << GetUUID().GetAsString();
    }
    LLDB_LOGF(log, "%s", logmsg.GetData());
  }
  if (value_is_offset) {
    // "value" is an offset to apply to each top level segment
    for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
      // Iterate through the object file sections to find all of the
      // sections that size on disk (to avoid __PAGEZERO) and load them
      SectionSP section_sp(section_list->GetSectionAtIndex(sect_idx));
      if (SectionIsLoadable(section_sp.get())) {
        LLDB_LOGF(log,
                  "ObjectFileMachO::SetLoadAddress segment '%s' load addr is "
                  "0x%" PRIx64,
                  section_sp->GetName().AsCString(),
                  section_sp->GetFileAddress() + value);
        if (target.GetSectionLoadList().SetSectionLoadAddress(
                section_sp, section_sp->GetFileAddress() + value,
                warn_multiple))
          ++num_loaded_sections;
      }
    }
  } else {
    // "value" is the new base address of the mach_header, adjust each
    // section accordingly

    Section *mach_header_section = GetMachHeaderSection();
    if (mach_header_section) {
      for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
        SectionSP section_sp(section_list->GetSectionAtIndex(sect_idx));

        lldb::addr_t section_load_addr =
            CalculateSectionLoadAddressForMemoryImage(
                value, mach_header_section, section_sp.get());
        if (section_load_addr != LLDB_INVALID_ADDRESS) {
          LLDB_LOGF(log,
                    "ObjectFileMachO::SetLoadAddress segment '%s' load addr is "
                    "0x%" PRIx64,
                    section_sp->GetName().AsCString(), section_load_addr);
          if (target.GetSectionLoadList().SetSectionLoadAddress(
                  section_sp, section_load_addr, warn_multiple))
            ++num_loaded_sections;
        }
      }
    }
  }
  return num_loaded_sections > 0;
}

struct all_image_infos_header {
  uint32_t version;         // currently 1
  uint32_t imgcount;        // number of binary images
  uint64_t entries_fileoff; // file offset in the corefile of where the array of
                            // struct entry's begin.
  uint32_t entries_size;    // size of 'struct entry'.
  uint32_t unused;
};

struct image_entry {
  uint64_t filepath_offset;  // offset in corefile to c-string of the file path,
                             // UINT64_MAX if unavailable.
  uuid_t uuid;               // uint8_t[16].  should be set to all zeroes if
                             // uuid is unknown.
  uint64_t load_address;     // UINT64_MAX if unknown.
  uint64_t seg_addrs_offset; // offset to the array of struct segment_vmaddr's.
  uint32_t segment_count;    // The number of segments for this binary.
  uint32_t unused;

  image_entry() {
    filepath_offset = UINT64_MAX;
    memset(&uuid, 0, sizeof(uuid_t));
    segment_count = 0;
    load_address = UINT64_MAX;
    seg_addrs_offset = UINT64_MAX;
    unused = 0;
  }
  image_entry(const image_entry &rhs) {
    filepath_offset = rhs.filepath_offset;
    memcpy(&uuid, &rhs.uuid, sizeof(uuid_t));
    segment_count = rhs.segment_count;
    seg_addrs_offset = rhs.seg_addrs_offset;
    load_address = rhs.load_address;
    unused = rhs.unused;
  }
};

struct segment_vmaddr {
  char segname[16];
  uint64_t vmaddr;
  uint64_t unused;

  segment_vmaddr() {
    memset(&segname, 0, 16);
    vmaddr = UINT64_MAX;
    unused = 0;
  }
  segment_vmaddr(const segment_vmaddr &rhs) {
    memcpy(&segname, &rhs.segname, 16);
    vmaddr = rhs.vmaddr;
    unused = rhs.unused;
  }
};

// Write the payload for the "all image infos" LC_NOTE into
// the supplied all_image_infos_payload, assuming that this
// will be written into the corefile starting at
// initial_file_offset.
//
// The placement of this payload is a little tricky.  We're
// laying this out as
//
// 1. header (struct all_image_info_header)
// 2. Array of fixed-size (struct image_entry)'s, one
//    per binary image present in the process.
// 3. Arrays of (struct segment_vmaddr)'s, a varying number
//    for each binary image.
// 4. Variable length c-strings of binary image filepaths,
//    one per binary.
//
// To compute where everything will be laid out in the
// payload, we need to iterate over the images and calculate
// how many segment_vmaddr structures each image will need,
// and how long each image's filepath c-string is. There
// are some multiple passes over the image list while calculating
// everything.

static offset_t CreateAllImageInfosPayload(
    const lldb::ProcessSP &process_sp, offset_t initial_file_offset,
    StreamString &all_image_infos_payload, SaveCoreStyle core_style) {
  Target &target = process_sp->GetTarget();
  ModuleList modules = target.GetImages();

  // stack-only corefiles have no reason to include binaries that
  // are not executing; we're trying to make the smallest corefile
  // we can, so leave the rest out.
  if (core_style == SaveCoreStyle::eSaveCoreStackOnly)
    modules.Clear();

  std::set<std::string> executing_uuids;
  ThreadList &thread_list(process_sp->GetThreadList());
  for (uint32_t i = 0; i < thread_list.GetSize(); i++) {
    ThreadSP thread_sp = thread_list.GetThreadAtIndex(i);
    uint32_t stack_frame_count = thread_sp->GetStackFrameCount();
    for (uint32_t j = 0; j < stack_frame_count; j++) {
      StackFrameSP stack_frame_sp = thread_sp->GetStackFrameAtIndex(j);
      Address pc = stack_frame_sp->GetFrameCodeAddress();
      ModuleSP module_sp = pc.GetModule();
      if (module_sp) {
        UUID uuid = module_sp->GetUUID();
        if (uuid.IsValid()) {
          executing_uuids.insert(uuid.GetAsString());
          modules.AppendIfNeeded(module_sp);
        }
      }
    }
  }
  size_t modules_count = modules.GetSize();

  struct all_image_infos_header infos;
  infos.version = 1;
  infos.imgcount = modules_count;
  infos.entries_size = sizeof(image_entry);
  infos.entries_fileoff = initial_file_offset + sizeof(all_image_infos_header);
  infos.unused = 0;

  all_image_infos_payload.PutHex32(infos.version);
  all_image_infos_payload.PutHex32(infos.imgcount);
  all_image_infos_payload.PutHex64(infos.entries_fileoff);
  all_image_infos_payload.PutHex32(infos.entries_size);
  all_image_infos_payload.PutHex32(infos.unused);

  // First create the structures for all of the segment name+vmaddr vectors
  // for each module, so we will know the size of them as we add the
  // module entries.
  std::vector<std::vector<segment_vmaddr>> modules_segment_vmaddrs;
  for (size_t i = 0; i < modules_count; i++) {
    ModuleSP module = modules.GetModuleAtIndex(i);

    SectionList *sections = module->GetSectionList();
    size_t sections_count = sections->GetSize();
    std::vector<segment_vmaddr> segment_vmaddrs;
    for (size_t j = 0; j < sections_count; j++) {
      SectionSP section = sections->GetSectionAtIndex(j);
      if (!section->GetParent().get()) {
        addr_t vmaddr = section->GetLoadBaseAddress(&target);
        if (vmaddr == LLDB_INVALID_ADDRESS)
          continue;
        ConstString name = section->GetName();
        segment_vmaddr seg_vmaddr;
        // This is the uncommon case where strncpy is exactly
        // the right one, doesn't need to be nul terminated.
        // The segment name in a Mach-O LC_SEGMENT/LC_SEGMENT_64 is char[16] and
        // is not guaranteed to be nul-terminated if all 16 characters are
        // used.
        // coverity[buffer_size_warning]
        strncpy(seg_vmaddr.segname, name.AsCString(),
                sizeof(seg_vmaddr.segname));
        seg_vmaddr.vmaddr = vmaddr;
        seg_vmaddr.unused = 0;
        segment_vmaddrs.push_back(seg_vmaddr);
      }
    }
    modules_segment_vmaddrs.push_back(segment_vmaddrs);
  }

  offset_t size_of_vmaddr_structs = 0;
  for (size_t i = 0; i < modules_segment_vmaddrs.size(); i++) {
    size_of_vmaddr_structs +=
        modules_segment_vmaddrs[i].size() * sizeof(segment_vmaddr);
  }

  offset_t size_of_filepath_cstrings = 0;
  for (size_t i = 0; i < modules_count; i++) {
    ModuleSP module_sp = modules.GetModuleAtIndex(i);
    size_of_filepath_cstrings += module_sp->GetFileSpec().GetPath().size() + 1;
  }

  // Calculate the file offsets of our "all image infos" payload in the
  // corefile. initial_file_offset the original value passed in to this method.

  offset_t start_of_entries =
      initial_file_offset + sizeof(all_image_infos_header);
  offset_t start_of_seg_vmaddrs =
      start_of_entries + sizeof(image_entry) * modules_count;
  offset_t start_of_filenames = start_of_seg_vmaddrs + size_of_vmaddr_structs;

  offset_t final_file_offset = start_of_filenames + size_of_filepath_cstrings;

  // Now write the one-per-module 'struct image_entry' into the
  // StringStream; keep track of where the struct segment_vmaddr
  // entries for each module will end up in the corefile.

  offset_t current_string_offset = start_of_filenames;
  offset_t current_segaddrs_offset = start_of_seg_vmaddrs;
  std::vector<struct image_entry> image_entries;
  for (size_t i = 0; i < modules_count; i++) {
    ModuleSP module_sp = modules.GetModuleAtIndex(i);

    struct image_entry ent;
    memcpy(&ent.uuid, module_sp->GetUUID().GetBytes().data(), sizeof(ent.uuid));
    if (modules_segment_vmaddrs[i].size() > 0) {
      ent.segment_count = modules_segment_vmaddrs[i].size();
      ent.seg_addrs_offset = current_segaddrs_offset;
    }
    ent.filepath_offset = current_string_offset;
    ObjectFile *objfile = module_sp->GetObjectFile();
    if (objfile) {
      Address base_addr(objfile->GetBaseAddress());
      if (base_addr.IsValid()) {
        ent.load_address = base_addr.GetLoadAddress(&target);
      }
    }

    all_image_infos_payload.PutHex64(ent.filepath_offset);
    all_image_infos_payload.PutRawBytes(ent.uuid, sizeof(ent.uuid));
    all_image_infos_payload.PutHex64(ent.load_address);
    all_image_infos_payload.PutHex64(ent.seg_addrs_offset);
    all_image_infos_payload.PutHex32(ent.segment_count);

    if (executing_uuids.find(module_sp->GetUUID().GetAsString()) !=
        executing_uuids.end())
      all_image_infos_payload.PutHex32(1);
    else
      all_image_infos_payload.PutHex32(0);

    current_segaddrs_offset += ent.segment_count * sizeof(segment_vmaddr);
    current_string_offset += module_sp->GetFileSpec().GetPath().size() + 1;
  }

  // Now write the struct segment_vmaddr entries into the StringStream.

  for (size_t i = 0; i < modules_segment_vmaddrs.size(); i++) {
    if (modules_segment_vmaddrs[i].size() == 0)
      continue;
    for (struct segment_vmaddr segvm : modules_segment_vmaddrs[i]) {
      all_image_infos_payload.PutRawBytes(segvm.segname, sizeof(segvm.segname));
      all_image_infos_payload.PutHex64(segvm.vmaddr);
      all_image_infos_payload.PutHex64(segvm.unused);
    }
  }

  for (size_t i = 0; i < modules_count; i++) {
    ModuleSP module_sp = modules.GetModuleAtIndex(i);
    std::string filepath = module_sp->GetFileSpec().GetPath();
    all_image_infos_payload.PutRawBytes(filepath.data(), filepath.size() + 1);
  }

  return final_file_offset;
}

// Temp struct used to combine contiguous memory regions with
// identical permissions.
struct page_object {
  addr_t addr;
  addr_t size;
  uint32_t prot;
};

bool ObjectFileMachO::SaveCore(const lldb::ProcessSP &process_sp,
                               const lldb_private::SaveCoreOptions &options,
                               Status &error) {
  auto core_style = options.GetStyle();
  if (core_style == SaveCoreStyle::eSaveCoreUnspecified)
    core_style = SaveCoreStyle::eSaveCoreDirtyOnly;
  // The FileSpec and Process are already checked in PluginManager::SaveCore.
  assert(options.GetOutputFile().has_value());
  assert(process_sp);
  const FileSpec outfile = options.GetOutputFile().value();

  Target &target = process_sp->GetTarget();
  const ArchSpec target_arch = target.GetArchitecture();
  const llvm::Triple &target_triple = target_arch.GetTriple();
  if (target_triple.getVendor() == llvm::Triple::Apple &&
      (target_triple.getOS() == llvm::Triple::MacOSX ||
       target_triple.getOS() == llvm::Triple::IOS ||
       target_triple.getOS() == llvm::Triple::WatchOS ||
       target_triple.getOS() == llvm::Triple::TvOS ||
       target_triple.getOS() == llvm::Triple::XROS)) {
    // NEED_BRIDGEOS_TRIPLE target_triple.getOS() == llvm::Triple::BridgeOS))
    // {
    bool make_core = false;
    switch (target_arch.GetMachine()) {
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_32:
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      make_core = true;
      break;
    default:
      error.SetErrorStringWithFormat("unsupported core architecture: %s",
                                     target_triple.str().c_str());
      break;
    }

    if (make_core) {
      Process::CoreFileMemoryRanges core_ranges;
      error = process_sp->CalculateCoreFileSaveRanges(core_style, core_ranges);
      if (error.Success()) {
        const uint32_t addr_byte_size = target_arch.GetAddressByteSize();
        const ByteOrder byte_order = target_arch.GetByteOrder();
        std::vector<llvm::MachO::segment_command_64> segment_load_commands;
        for (const auto &core_range : core_ranges) {
          uint32_t cmd_type = LC_SEGMENT_64;
          uint32_t segment_size = sizeof(llvm::MachO::segment_command_64);
          if (addr_byte_size == 4) {
            cmd_type = LC_SEGMENT;
            segment_size = sizeof(llvm::MachO::segment_command);
          }
          // Skip any ranges with no read/write/execute permissions and empty
          // ranges.
          if (core_range.lldb_permissions == 0 || core_range.range.size() == 0)
            continue;
          uint32_t vm_prot = 0;
          if (core_range.lldb_permissions & ePermissionsReadable)
            vm_prot |= VM_PROT_READ;
          if (core_range.lldb_permissions & ePermissionsWritable)
            vm_prot |= VM_PROT_WRITE;
          if (core_range.lldb_permissions & ePermissionsExecutable)
            vm_prot |= VM_PROT_EXECUTE;
          const addr_t vm_addr = core_range.range.start();
          const addr_t vm_size = core_range.range.size();
          llvm::MachO::segment_command_64 segment = {
              cmd_type,     // uint32_t cmd;
              segment_size, // uint32_t cmdsize;
              {0},          // char segname[16];
              vm_addr,      // uint64_t vmaddr;   // uint32_t for 32-bit Mach-O
              vm_size,      // uint64_t vmsize;   // uint32_t for 32-bit Mach-O
              0,            // uint64_t fileoff;  // uint32_t for 32-bit Mach-O
              vm_size,      // uint64_t filesize; // uint32_t for 32-bit Mach-O
              vm_prot,      // uint32_t maxprot;
              vm_prot,      // uint32_t initprot;
              0,            // uint32_t nsects;
              0};           // uint32_t flags;
          segment_load_commands.push_back(segment);
        }

        StreamString buffer(Stream::eBinary, addr_byte_size, byte_order);

        llvm::MachO::mach_header_64 mach_header;
        mach_header.magic = addr_byte_size == 8 ? MH_MAGIC_64 : MH_MAGIC;
        mach_header.cputype = target_arch.GetMachOCPUType();
        mach_header.cpusubtype = target_arch.GetMachOCPUSubType();
        mach_header.filetype = MH_CORE;
        mach_header.ncmds = segment_load_commands.size();
        mach_header.flags = 0;
        mach_header.reserved = 0;
        ThreadList &thread_list = process_sp->GetThreadList();
        const uint32_t num_threads = thread_list.GetSize();

        // Make an array of LC_THREAD data items. Each one contains the
        // contents of the LC_THREAD load command. The data doesn't contain
        // the load command + load command size, we will add the load command
        // and load command size as we emit the data.
        std::vector<StreamString> LC_THREAD_datas(num_threads);
        for (auto &LC_THREAD_data : LC_THREAD_datas) {
          LC_THREAD_data.GetFlags().Set(Stream::eBinary);
          LC_THREAD_data.SetAddressByteSize(addr_byte_size);
          LC_THREAD_data.SetByteOrder(byte_order);
        }
        for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
          ThreadSP thread_sp(thread_list.GetThreadAtIndex(thread_idx));
          if (thread_sp) {
            switch (mach_header.cputype) {
            case llvm::MachO::CPU_TYPE_ARM64:
            case llvm::MachO::CPU_TYPE_ARM64_32:
              RegisterContextDarwin_arm64_Mach::Create_LC_THREAD(
                  thread_sp.get(), LC_THREAD_datas[thread_idx]);
              break;

            case llvm::MachO::CPU_TYPE_ARM:
              RegisterContextDarwin_arm_Mach::Create_LC_THREAD(
                  thread_sp.get(), LC_THREAD_datas[thread_idx]);
              break;

            case llvm::MachO::CPU_TYPE_I386:
              RegisterContextDarwin_i386_Mach::Create_LC_THREAD(
                  thread_sp.get(), LC_THREAD_datas[thread_idx]);
              break;

            case llvm::MachO::CPU_TYPE_X86_64:
              RegisterContextDarwin_x86_64_Mach::Create_LC_THREAD(
                  thread_sp.get(), LC_THREAD_datas[thread_idx]);
              break;
            }
          }
        }

        // The size of the load command is the size of the segments...
        if (addr_byte_size == 8) {
          mach_header.sizeofcmds = segment_load_commands.size() *
                                   sizeof(llvm::MachO::segment_command_64);
        } else {
          mach_header.sizeofcmds = segment_load_commands.size() *
                                   sizeof(llvm::MachO::segment_command);
        }

        // and the size of all LC_THREAD load command
        for (const auto &LC_THREAD_data : LC_THREAD_datas) {
          ++mach_header.ncmds;
          mach_header.sizeofcmds += 8 + LC_THREAD_data.GetSize();
        }

        // Bits will be set to indicate which bits are NOT used in
        // addressing in this process or 0 for unknown.
        uint64_t address_mask = process_sp->GetCodeAddressMask();
        if (address_mask != LLDB_INVALID_ADDRESS_MASK) {
          // LC_NOTE "addrable bits"
          mach_header.ncmds++;
          mach_header.sizeofcmds += sizeof(llvm::MachO::note_command);
        }

        // LC_NOTE "process metadata"
        mach_header.ncmds++;
        mach_header.sizeofcmds += sizeof(llvm::MachO::note_command);

        // LC_NOTE "all image infos"
        mach_header.ncmds++;
        mach_header.sizeofcmds += sizeof(llvm::MachO::note_command);

        // Write the mach header
        buffer.PutHex32(mach_header.magic);
        buffer.PutHex32(mach_header.cputype);
        buffer.PutHex32(mach_header.cpusubtype);
        buffer.PutHex32(mach_header.filetype);
        buffer.PutHex32(mach_header.ncmds);
        buffer.PutHex32(mach_header.sizeofcmds);
        buffer.PutHex32(mach_header.flags);
        if (addr_byte_size == 8) {
          buffer.PutHex32(mach_header.reserved);
        }

        // Skip the mach header and all load commands and align to the next
        // 0x1000 byte boundary
        addr_t file_offset = buffer.GetSize() + mach_header.sizeofcmds;

        file_offset = llvm::alignTo(file_offset, 16);
        std::vector<std::unique_ptr<LCNoteEntry>> lc_notes;

        // Add "addrable bits" LC_NOTE when an address mask is available
        if (address_mask != LLDB_INVALID_ADDRESS_MASK) {
          std::unique_ptr<LCNoteEntry> addrable_bits_lcnote_up(
              new LCNoteEntry(addr_byte_size, byte_order));
          addrable_bits_lcnote_up->name = "addrable bits";
          addrable_bits_lcnote_up->payload_file_offset = file_offset;
          int bits = std::bitset<64>(~address_mask).count();
          addrable_bits_lcnote_up->payload.PutHex32(4); // version
          addrable_bits_lcnote_up->payload.PutHex32(
              bits); // # of bits used for low addresses
          addrable_bits_lcnote_up->payload.PutHex32(
              bits); // # of bits used for high addresses
          addrable_bits_lcnote_up->payload.PutHex32(0); // reserved

          file_offset += addrable_bits_lcnote_up->payload.GetSize();

          lc_notes.push_back(std::move(addrable_bits_lcnote_up));
        }

        // Add "process metadata" LC_NOTE
        std::unique_ptr<LCNoteEntry> thread_extrainfo_lcnote_up(
            new LCNoteEntry(addr_byte_size, byte_order));
        thread_extrainfo_lcnote_up->name = "process metadata";
        thread_extrainfo_lcnote_up->payload_file_offset = file_offset;

        StructuredData::DictionarySP dict(
            std::make_shared<StructuredData::Dictionary>());
        StructuredData::ArraySP threads(
            std::make_shared<StructuredData::Array>());
        for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
          ThreadSP thread_sp(thread_list.GetThreadAtIndex(thread_idx));
          StructuredData::DictionarySP thread(
              std::make_shared<StructuredData::Dictionary>());
          thread->AddIntegerItem("thread_id", thread_sp->GetID());
          threads->AddItem(thread);
        }
        dict->AddItem("threads", threads);
        StreamString strm;
        dict->Dump(strm, /* pretty */ false);
        thread_extrainfo_lcnote_up->payload.PutRawBytes(strm.GetData(),
                                                        strm.GetSize());

        file_offset += thread_extrainfo_lcnote_up->payload.GetSize();
        file_offset = llvm::alignTo(file_offset, 16);
        lc_notes.push_back(std::move(thread_extrainfo_lcnote_up));

        // Add "all image infos" LC_NOTE
        std::unique_ptr<LCNoteEntry> all_image_infos_lcnote_up(
            new LCNoteEntry(addr_byte_size, byte_order));
        all_image_infos_lcnote_up->name = "all image infos";
        all_image_infos_lcnote_up->payload_file_offset = file_offset;
        file_offset = CreateAllImageInfosPayload(
            process_sp, file_offset, all_image_infos_lcnote_up->payload,
            core_style);
        lc_notes.push_back(std::move(all_image_infos_lcnote_up));

        // Add LC_NOTE load commands
        for (auto &lcnote : lc_notes) {
          // Add the LC_NOTE load command to the file.
          buffer.PutHex32(LC_NOTE);
          buffer.PutHex32(sizeof(llvm::MachO::note_command));
          char namebuf[16];
          memset(namebuf, 0, sizeof(namebuf));
          // This is the uncommon case where strncpy is exactly
          // the right one, doesn't need to be nul terminated.
          // LC_NOTE name field is char[16] and is not guaranteed to be
          // nul-terminated.
          // coverity[buffer_size_warning]
          strncpy(namebuf, lcnote->name.c_str(), sizeof(namebuf));
          buffer.PutRawBytes(namebuf, sizeof(namebuf));
          buffer.PutHex64(lcnote->payload_file_offset);
          buffer.PutHex64(lcnote->payload.GetSize());
        }

        // Align to 4096-byte page boundary for the LC_SEGMENTs.
        file_offset = llvm::alignTo(file_offset, 4096);

        for (auto &segment : segment_load_commands) {
          segment.fileoff = file_offset;
          file_offset += segment.filesize;
        }

        // Write out all of the LC_THREAD load commands
        for (const auto &LC_THREAD_data : LC_THREAD_datas) {
          const size_t LC_THREAD_data_size = LC_THREAD_data.GetSize();
          buffer.PutHex32(LC_THREAD);
          buffer.PutHex32(8 + LC_THREAD_data_size); // cmd + cmdsize + data
          buffer.Write(LC_THREAD_data.GetString().data(), LC_THREAD_data_size);
        }

        // Write out all of the segment load commands
        for (const auto &segment : segment_load_commands) {
          buffer.PutHex32(segment.cmd);
          buffer.PutHex32(segment.cmdsize);
          buffer.PutRawBytes(segment.segname, sizeof(segment.segname));
          if (addr_byte_size == 8) {
            buffer.PutHex64(segment.vmaddr);
            buffer.PutHex64(segment.vmsize);
            buffer.PutHex64(segment.fileoff);
            buffer.PutHex64(segment.filesize);
          } else {
            buffer.PutHex32(static_cast<uint32_t>(segment.vmaddr));
            buffer.PutHex32(static_cast<uint32_t>(segment.vmsize));
            buffer.PutHex32(static_cast<uint32_t>(segment.fileoff));
            buffer.PutHex32(static_cast<uint32_t>(segment.filesize));
          }
          buffer.PutHex32(segment.maxprot);
          buffer.PutHex32(segment.initprot);
          buffer.PutHex32(segment.nsects);
          buffer.PutHex32(segment.flags);
        }

        std::string core_file_path(outfile.GetPath());
        auto core_file = FileSystem::Instance().Open(
            outfile, File::eOpenOptionWriteOnly | File::eOpenOptionTruncate |
                         File::eOpenOptionCanCreate);
        if (!core_file) {
          error = core_file.takeError();
        } else {
          // Read 1 page at a time
          uint8_t bytes[0x1000];
          // Write the mach header and load commands out to the core file
          size_t bytes_written = buffer.GetString().size();
          error =
              core_file.get()->Write(buffer.GetString().data(), bytes_written);
          if (error.Success()) {

            for (auto &lcnote : lc_notes) {
              if (core_file.get()->SeekFromStart(lcnote->payload_file_offset) ==
                  -1) {
                error.SetErrorStringWithFormat("Unable to seek to corefile pos "
                                               "to write '%s' LC_NOTE payload",
                                               lcnote->name.c_str());
                return false;
              }
              bytes_written = lcnote->payload.GetSize();
              error = core_file.get()->Write(lcnote->payload.GetData(),
                                             bytes_written);
              if (!error.Success())
                return false;
            }

            // Now write the file data for all memory segments in the process
            for (const auto &segment : segment_load_commands) {
              if (core_file.get()->SeekFromStart(segment.fileoff) == -1) {
                error.SetErrorStringWithFormat(
                    "unable to seek to offset 0x%" PRIx64 " in '%s'",
                    segment.fileoff, core_file_path.c_str());
                break;
              }

              target.GetDebugger().GetAsyncOutputStream()->Printf(
                  "Saving %" PRId64
                  " bytes of data for memory region at 0x%" PRIx64 "\n",
                  segment.vmsize, segment.vmaddr);
              addr_t bytes_left = segment.vmsize;
              addr_t addr = segment.vmaddr;
              Status memory_read_error;
              while (bytes_left > 0 && error.Success()) {
                const size_t bytes_to_read =
                    bytes_left > sizeof(bytes) ? sizeof(bytes) : bytes_left;

                // In a savecore setting, we don't really care about caching,
                // as the data is dumped and very likely never read again,
                // so we call ReadMemoryFromInferior to bypass it.
                const size_t bytes_read = process_sp->ReadMemoryFromInferior(
                    addr, bytes, bytes_to_read, memory_read_error);

                if (bytes_read == bytes_to_read) {
                  size_t bytes_written = bytes_read;
                  error = core_file.get()->Write(bytes, bytes_written);
                  bytes_left -= bytes_read;
                  addr += bytes_read;
                } else {
                  // Some pages within regions are not readable, those should
                  // be zero filled
                  memset(bytes, 0, bytes_to_read);
                  size_t bytes_written = bytes_to_read;
                  error = core_file.get()->Write(bytes, bytes_written);
                  bytes_left -= bytes_to_read;
                  addr += bytes_to_read;
                }
              }
            }
          }
        }
      }
    }
    return true; // This is the right plug to handle saving core files for
                 // this process
  }
  return false;
}

ObjectFileMachO::MachOCorefileAllImageInfos
ObjectFileMachO::GetCorefileAllImageInfos() {
  MachOCorefileAllImageInfos image_infos;
  Log *log(GetLog(LLDBLog::Object | LLDBLog::Symbols | LLDBLog::Process |
                  LLDBLog::DynamicLoader));

  auto lc_notes = FindLC_NOTEByName("all image infos");
  for (auto lc_note : lc_notes) {
    offset_t payload_offset = std::get<0>(lc_note);
    // Read the struct all_image_infos_header.
    uint32_t version = m_data.GetU32(&payload_offset);
    if (version != 1) {
      return image_infos;
    }
    uint32_t imgcount = m_data.GetU32(&payload_offset);
    uint64_t entries_fileoff = m_data.GetU64(&payload_offset);
    // 'entries_size' is not used, nor is the 'unused' entry.
    //  offset += 4; // uint32_t entries_size;
    //  offset += 4; // uint32_t unused;

    LLDB_LOGF(log, "LC_NOTE 'all image infos' found version %d with %d images",
              version, imgcount);
    payload_offset = entries_fileoff;
    for (uint32_t i = 0; i < imgcount; i++) {
      // Read the struct image_entry.
      offset_t filepath_offset = m_data.GetU64(&payload_offset);
      uuid_t uuid;
      memcpy(&uuid, m_data.GetData(&payload_offset, sizeof(uuid_t)),
             sizeof(uuid_t));
      uint64_t load_address = m_data.GetU64(&payload_offset);
      offset_t seg_addrs_offset = m_data.GetU64(&payload_offset);
      uint32_t segment_count = m_data.GetU32(&payload_offset);
      uint32_t currently_executing = m_data.GetU32(&payload_offset);

      MachOCorefileImageEntry image_entry;
      image_entry.filename = (const char *)m_data.GetCStr(&filepath_offset);
      image_entry.uuid = UUID(uuid, sizeof(uuid_t));
      image_entry.load_address = load_address;
      image_entry.currently_executing = currently_executing;

      offset_t seg_vmaddrs_offset = seg_addrs_offset;
      for (uint32_t j = 0; j < segment_count; j++) {
        char segname[17];
        m_data.CopyData(seg_vmaddrs_offset, 16, segname);
        segname[16] = '\0';
        seg_vmaddrs_offset += 16;
        uint64_t vmaddr = m_data.GetU64(&seg_vmaddrs_offset);
        seg_vmaddrs_offset += 8; /* unused */

        std::tuple<ConstString, addr_t> new_seg{ConstString(segname), vmaddr};
        image_entry.segment_load_addresses.push_back(new_seg);
      }
      LLDB_LOGF(log, "  image entry: %s %s 0x%" PRIx64 " %s",
                image_entry.filename.c_str(),
                image_entry.uuid.GetAsString().c_str(),
                image_entry.load_address,
                image_entry.currently_executing ? "currently executing"
                                                : "not currently executing");
      image_infos.all_image_infos.push_back(image_entry);
    }
  }

  lc_notes = FindLC_NOTEByName("load binary");
  for (auto lc_note : lc_notes) {
    offset_t payload_offset = std::get<0>(lc_note);
    uint32_t version = m_data.GetU32(&payload_offset);
    if (version == 1) {
      uuid_t uuid;
      memcpy(&uuid, m_data.GetData(&payload_offset, sizeof(uuid_t)),
             sizeof(uuid_t));
      uint64_t load_address = m_data.GetU64(&payload_offset);
      uint64_t slide = m_data.GetU64(&payload_offset);
      std::string filename = m_data.GetCStr(&payload_offset);

      MachOCorefileImageEntry image_entry;
      image_entry.filename = filename;
      image_entry.uuid = UUID(uuid, sizeof(uuid_t));
      image_entry.load_address = load_address;
      image_entry.slide = slide;
      image_entry.currently_executing = true;
      image_infos.all_image_infos.push_back(image_entry);
      LLDB_LOGF(log,
                "LC_NOTE 'load binary' found, filename %s uuid %s load "
                "address 0x%" PRIx64 " slide 0x%" PRIx64,
                filename.c_str(),
                image_entry.uuid.IsValid()
                    ? image_entry.uuid.GetAsString().c_str()
                    : "00000000-0000-0000-0000-000000000000",
                load_address, slide);
    }
  }

  return image_infos;
}

bool ObjectFileMachO::LoadCoreFileImages(lldb_private::Process &process) {
  MachOCorefileAllImageInfos image_infos = GetCorefileAllImageInfos();
  Log *log = GetLog(LLDBLog::Object | LLDBLog::DynamicLoader);
  Status error;

  bool found_platform_binary = false;
  ModuleList added_modules;
  for (MachOCorefileImageEntry &image : image_infos.all_image_infos) {
    ModuleSP module_sp, local_filesystem_module_sp;

    // If this is a platform binary, it has been loaded (or registered with
    // the DynamicLoader to be loaded), we don't need to do any further
    // processing.  We're not going to call ModulesDidLoad on this in this
    // method, so notify==true.
    if (process.GetTarget()
            .GetDebugger()
            .GetPlatformList()
            .LoadPlatformBinaryAndSetup(&process, image.load_address,
                                        true /* notify */)) {
      LLDB_LOGF(log,
                "ObjectFileMachO::%s binary at 0x%" PRIx64
                " is a platform binary, has been handled by a Platform plugin.",
                __FUNCTION__, image.load_address);
      continue;
    }

    bool value_is_offset = image.load_address == LLDB_INVALID_ADDRESS;
    uint64_t value = value_is_offset ? image.slide : image.load_address;
    if (value_is_offset && value == LLDB_INVALID_ADDRESS) {
      // We have neither address nor slide; so we will find the binary
      // by UUID and load it at slide/offset 0.
      value = 0;
    }

    // We have either a UUID, or we have a load address which
    // and can try to read load commands and find a UUID.
    if (image.uuid.IsValid() ||
        (!value_is_offset && value != LLDB_INVALID_ADDRESS)) {
      const bool set_load_address = image.segment_load_addresses.size() == 0;
      const bool notify = false;
      // Userland Darwin binaries will have segment load addresses via
      // the `all image infos` LC_NOTE.
      const bool allow_memory_image_last_resort =
          image.segment_load_addresses.size();
      module_sp = DynamicLoader::LoadBinaryWithUUIDAndAddress(
          &process, image.filename, image.uuid, value, value_is_offset,
          image.currently_executing, notify, set_load_address,
          allow_memory_image_last_resort);
    }

    // We have a ModuleSP to load in the Target.  Load it at the
    // correct address/slide and notify/load scripting resources.
    if (module_sp) {
      added_modules.Append(module_sp, false /* notify */);

      // We have a list of segment load address
      if (image.segment_load_addresses.size() > 0) {
        if (log) {
          std::string uuidstr = image.uuid.GetAsString();
          log->Printf("ObjectFileMachO::LoadCoreFileImages adding binary '%s' "
                      "UUID %s with section load addresses",
                      module_sp->GetFileSpec().GetPath().c_str(),
                      uuidstr.c_str());
        }
        for (auto name_vmaddr_tuple : image.segment_load_addresses) {
          SectionList *sectlist = module_sp->GetObjectFile()->GetSectionList();
          if (sectlist) {
            SectionSP sect_sp =
                sectlist->FindSectionByName(std::get<0>(name_vmaddr_tuple));
            if (sect_sp) {
              process.GetTarget().SetSectionLoadAddress(
                  sect_sp, std::get<1>(name_vmaddr_tuple));
            }
          }
        }
      } else {
        if (log) {
          std::string uuidstr = image.uuid.GetAsString();
          log->Printf("ObjectFileMachO::LoadCoreFileImages adding binary '%s' "
                      "UUID %s with %s 0x%" PRIx64,
                      module_sp->GetFileSpec().GetPath().c_str(),
                      uuidstr.c_str(),
                      value_is_offset ? "slide" : "load address", value);
        }
        bool changed;
        module_sp->SetLoadAddress(process.GetTarget(), value, value_is_offset,
                                  changed);
      }
    }
  }
  if (added_modules.GetSize() > 0) {
    process.GetTarget().ModulesDidLoad(added_modules);
    process.Flush();
    return true;
  }
  // Return true if the only binary we found was the platform binary,
  // and it was loaded outside the scope of this method.
  if (found_platform_binary)
    return true;

  // No binaries.
  return false;
}
