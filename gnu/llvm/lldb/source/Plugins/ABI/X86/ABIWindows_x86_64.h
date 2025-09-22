//===-- ABIWindows_x86_64.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_X86_ABIWINDOWS_X86_64_H
#define LLDB_SOURCE_PLUGINS_ABI_X86_ABIWINDOWS_X86_64_H

#include "Plugins/ABI/X86/ABIX86_64.h"

class ABIWindows_x86_64 : public ABIX86_64 {
public:
  ~ABIWindows_x86_64() override = default;

  size_t GetRedZoneSize() const override;

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override;

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override;

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  bool
  CreateFunctionEntryUnwindPlan(lldb_private::UnwindPlan &unwind_plan) override;

  bool CreateDefaultUnwindPlan(lldb_private::UnwindPlan &unwind_plan) override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  // In Windows_x86_64 ABI requires that the stack will be maintained 16-byte
  // aligned.
  // When ntdll invokes callbacks such as KiUserExceptionDispatcher or
  // KiUserCallbackDispatcher, those functions won't have a properly 16-byte
  // aligned stack - but tolerate unwinding through them by relaxing the
  // requirement to 8 bytes.
  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    if (cfa & (8ull - 1ull))
      return false; // Not 8 byte aligned
    if (cfa == 0)
      return false; // Zero is not a valid stack address
    return true;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override {
    // We have a 64 bit address space, so anything is valid as opcodes
    // aren't fixed width...
    return true;
  }

  bool GetPointerReturnRegister(const char *&name) override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------

  static void Initialize();

  static void Terminate();

  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp, const lldb_private::ArchSpec &arch);

  static llvm::StringRef GetPluginNameStatic() { return "windows-x86_64"; }

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  void CreateRegisterMapIfNeeded();

  lldb::ValueObjectSP
  GetReturnValueObjectSimple(lldb_private::Thread &thread,
                             lldb_private::CompilerType &ast_type) const;

  bool RegisterIsCalleeSaved(const lldb_private::RegisterInfo *reg_info);
  uint32_t GetGenericNum(llvm::StringRef reg) override;

private:
  using ABIX86_64::ABIX86_64; // Call CreateInstance instead.
};

#endif // LLDB_SOURCE_PLUGINS_ABI_X86_ABIWINDOWS_X86_64_H
