//===-- ABISysV_riscv.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ABISysV_riscv_h_
#define liblldb_ABISysV_riscv_h_

// Other libraries and framework includes
#include "llvm/TargetParser/Triple.h"

// Project includes
#include "lldb/Target/ABI.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/Flags.h"
#include "lldb/lldb-private.h"

class ABISysV_riscv : public lldb_private::RegInfoBasedABI {
public:
  ~ABISysV_riscv() override = default;

  size_t GetRedZoneSize() const override { return 0; }

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override;

  // Special thread plan for GDB style non-jit function calls.
  bool
  PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                     lldb::addr_t functionAddress, lldb::addr_t returnAddress,
                     llvm::Type &prototype,
                     llvm::ArrayRef<ABI::CallArgument> args) const override;

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override;

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  // Specialized to work with llvm IR types.
  lldb::ValueObjectSP GetReturnValueObjectImpl(lldb_private::Thread &thread,
                                               llvm::Type &type) const override;

  bool
  CreateFunctionEntryUnwindPlan(lldb_private::UnwindPlan &unwind_plan) override;

  bool CreateDefaultUnwindPlan(lldb_private::UnwindPlan &unwind_plan) override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    // The CFA must be 128 bit aligned, unless the E ABI is used
    lldb_private::ArchSpec arch = GetProcessSP()->GetTarget().GetArchitecture();
    lldb_private::Flags arch_flags = arch.GetFlags();
    if (arch_flags.Test(lldb_private::ArchSpec::eRISCV_rve))
      return (cfa & 0x3ull) == 0;
    return (cfa & 0xfull) == 0;
  }

  void SetIsRV64(bool is_rv64) { m_is_rv64 = is_rv64; }

  bool CodeAddressIsValid(lldb::addr_t pc) override {
    // Calls can use the least significant bit to store auxiliary information,
    // so no strict check is done for alignment.

    lldb_private::ArchSpec arch = GetProcessSP()->GetTarget().GetArchitecture();

    // <addr> & 2 set is a fault if C extension is not used.
    lldb_private::Flags arch_flags(arch.GetFlags());
    if (!arch_flags.Test(lldb_private::ArchSpec::eRISCV_rvc) && (pc & 2))
      return false;

    // Make sure 64 bit addr_t only has lower 32 bits set on riscv32
    llvm::Triple::ArchType machine = arch.GetMachine();
    if (llvm::Triple::riscv32 == machine)
      return (pc <= UINT32_MAX);

    return true;
  }

  const lldb_private::RegisterInfo *
  GetRegisterInfoArray(uint32_t &count) override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------

  static void Initialize();

  static void Terminate();

  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);

  static llvm::StringRef GetPluginNameStatic() { return "sysv-riscv"; }

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  void AugmentRegisterInfo(
      std::vector<lldb_private::DynamicRegisterInfo::Register> &regs) override;

  bool RegisterIsCalleeSaved(const lldb_private::RegisterInfo *reg_info);

private:
  lldb::ValueObjectSP
  GetReturnValueObjectSimple(lldb_private::Thread &thread,
                             lldb_private::CompilerType &ast_type) const;

  using lldb_private::RegInfoBasedABI::RegInfoBasedABI; // Call CreateInstance
                                                        // instead.
  bool m_is_rv64; // true if target is riscv64; false if target is riscv32
};

#endif // liblldb_ABISysV_riscv_h_
