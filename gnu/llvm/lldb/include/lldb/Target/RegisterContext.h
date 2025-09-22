//===-- RegisterContext.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_REGISTERCONTEXT_H
#define LLDB_TARGET_REGISTERCONTEXT_H

#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class RegisterContext : public std::enable_shared_from_this<RegisterContext>,
                        public ExecutionContextScope {
public:
  // Constructors and Destructors
  RegisterContext(Thread &thread, uint32_t concrete_frame_idx);

  ~RegisterContext() override;

  void InvalidateIfNeeded(bool force);

  // Subclasses must override these functions
  virtual void InvalidateAllRegisters() = 0;

  virtual size_t GetRegisterCount() = 0;

  virtual const RegisterInfo *GetRegisterInfoAtIndex(size_t reg) = 0;

  virtual size_t GetRegisterSetCount() = 0;

  virtual const RegisterSet *GetRegisterSet(size_t reg_set) = 0;

  virtual lldb::ByteOrder GetByteOrder();

  virtual bool ReadRegister(const RegisterInfo *reg_info,
                            RegisterValue &reg_value) = 0;

  virtual bool WriteRegister(const RegisterInfo *reg_info,
                             const RegisterValue &reg_value) = 0;

  virtual bool ReadAllRegisterValues(lldb::WritableDataBufferSP &data_sp) {
    return false;
  }

  virtual bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) {
    return false;
  }

  virtual bool RegisterWriteCausesReconfigure(const llvm::StringRef name) {
    return false;
  }

  virtual bool ReconfigureRegisterInfo() { return false; }

  // These two functions are used to implement "push" and "pop" of register
  // states.  They are used primarily for expression evaluation, where we need
  // to push a new state (storing the old one in data_sp) and then restoring
  // the original state by passing the data_sp we got from ReadAllRegisters to
  // WriteAllRegisterValues. ReadAllRegisters will do what is necessary to
  // return a coherent set of register values for this thread, which may mean
  // e.g. interrupting a thread that is sitting in a kernel trap.  That is a
  // somewhat disruptive operation, so these API's should only be used when
  // this behavior is needed.

  virtual bool
  ReadAllRegisterValues(lldb_private::RegisterCheckpoint &reg_checkpoint);

  virtual bool WriteAllRegisterValues(
      const lldb_private::RegisterCheckpoint &reg_checkpoint);

  bool CopyFromRegisterContext(lldb::RegisterContextSP context);

  /// Convert from a given register numbering scheme to the lldb register
  /// numbering scheme
  ///
  /// There may be multiple ways to enumerate the registers for a given
  /// architecture.  ABI references will specify one to be used with
  /// DWARF, the register numberings from process plugin, there may
  /// be a variation used for eh_frame unwind instructions (e.g. on Darwin),
  /// and so on.  Register 5 by itself is meaningless - RegisterKind
  /// enumeration tells you what context that number should be translated as.
  ///
  /// Inside lldb, register numbers are in the eRegisterKindLLDB scheme;
  /// arguments which take a register number should take one in that
  /// scheme.
  ///
  /// eRegisterKindGeneric is a special numbering scheme which gives us
  /// constant values for the pc, frame register, stack register, etc., for
  /// use within lldb.  They may not be defined for all architectures but
  /// it allows generic code to translate these common registers into the
  /// lldb numbering scheme.
  ///
  /// This method translates a given register kind + register number into
  /// the eRegisterKindLLDB register numbering.
  ///
  /// \param [in] kind
  ///     The register numbering scheme (RegisterKind) that the following
  ///     register number is in.
  ///
  /// \param [in] num
  ///     A register number in the 'kind' register numbering scheme.
  ///
  /// \return
  ///     The equivalent register number in the eRegisterKindLLDB
  ///     numbering scheme, if possible, else LLDB_INVALID_REGNUM.
  virtual uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                                       uint32_t num);

  // Subclasses can override these functions if desired
  virtual uint32_t NumSupportedHardwareBreakpoints();

  virtual uint32_t SetHardwareBreakpoint(lldb::addr_t addr, size_t size);

  virtual bool ClearHardwareBreakpoint(uint32_t hw_idx);

  virtual uint32_t NumSupportedHardwareWatchpoints();

  virtual uint32_t SetHardwareWatchpoint(lldb::addr_t addr, size_t size,
                                         bool read, bool write);

  virtual bool ClearHardwareWatchpoint(uint32_t hw_index);

  virtual bool HardwareSingleStep(bool enable);

  virtual Status
  ReadRegisterValueFromMemory(const lldb_private::RegisterInfo *reg_info,
                              lldb::addr_t src_addr, uint32_t src_len,
                              RegisterValue &reg_value);

  virtual Status
  WriteRegisterValueToMemory(const lldb_private::RegisterInfo *reg_info,
                             lldb::addr_t dst_addr, uint32_t dst_len,
                             const RegisterValue &reg_value);

  // Subclasses should not override these
  virtual lldb::tid_t GetThreadID() const;

  virtual Thread &GetThread() { return m_thread; }

  const RegisterInfo *GetRegisterInfoByName(llvm::StringRef reg_name,
                                            uint32_t start_idx = 0);

  const RegisterInfo *GetRegisterInfo(lldb::RegisterKind reg_kind,
                                      uint32_t reg_num);

  uint64_t GetPC(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  // Returns the register value containing thread specific data, like TLS data
  // and other thread specific stuff.
  uint64_t GetThreadPointer(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  /// Get an address suitable for symbolication.
  /// When symbolicating -- computing line, block, function --
  /// for a function in the middle of the stack, using the return
  /// address can lead to unexpected results for the user.
  /// A function that ends in a tail-call may have another function
  /// as the "return" address, but it will never actually return.
  /// Or a noreturn call in the middle of a function is the end of
  /// a block of instructions, and a DWARF location list entry for
  /// the return address may be a very different code path with
  /// incorrect results when printing variables for this frame.
  ///
  /// At a source line view, the user expects the current-line indictation
  /// to point to the function call they're under, not the next source line.
  ///
  /// The return address (GetPC()) should always be shown to the user,
  /// but when computing context, keeping within the bounds of the
  /// call instruction is what the user expects to see.
  ///
  /// \param [out] address
  ///     An Address object that will be filled in, if a PC can be retrieved.
  ///
  /// \return
  ///     Returns true if the Address param was filled in.
  bool GetPCForSymbolication(Address &address);

  bool SetPC(uint64_t pc);

  bool SetPC(Address addr);

  uint64_t GetSP(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  bool SetSP(uint64_t sp);

  uint64_t GetFP(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  bool SetFP(uint64_t fp);

  const char *GetRegisterName(uint32_t reg);

  uint64_t GetReturnAddress(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  uint64_t GetFlags(uint64_t fail_value = 0);

  uint64_t ReadRegisterAsUnsigned(uint32_t reg, uint64_t fail_value);

  uint64_t ReadRegisterAsUnsigned(const RegisterInfo *reg_info,
                                  uint64_t fail_value);

  bool WriteRegisterFromUnsigned(uint32_t reg, uint64_t uval);

  bool WriteRegisterFromUnsigned(const RegisterInfo *reg_info, uint64_t uval);

  bool ConvertBetweenRegisterKinds(lldb::RegisterKind source_rk,
                                   uint32_t source_regnum,
                                   lldb::RegisterKind target_rk,
                                   uint32_t &target_regnum);

  // lldb::ExecutionContextScope pure virtual functions
  lldb::TargetSP CalculateTarget() override;

  lldb::ProcessSP CalculateProcess() override;

  lldb::ThreadSP CalculateThread() override;

  lldb::StackFrameSP CalculateStackFrame() override;

  void CalculateExecutionContext(ExecutionContext &exe_ctx) override;

  uint32_t GetStopID() const { return m_stop_id; }

  void SetStopID(uint32_t stop_id) { m_stop_id = stop_id; }

protected:
  /// Indicates that this frame is currently executing code,
  /// that the PC value is not a return-pc but an actual executing
  /// instruction.  Some places in lldb will treat a return-pc
  /// value differently than the currently-executing-pc value,
  /// and this method can indicate if that should be done.
  /// The base class implementation only uses the frame index,
  /// but subclasses may have additional information that they
  /// can use to detect frames in this state, for instance a
  /// frame above a trap handler (sigtramp etc)..
  virtual bool BehavesLikeZerothFrame() const {
    return m_concrete_frame_idx == 0;
  }

  // Classes that inherit from RegisterContext can see and modify these
  Thread &m_thread; // The thread that this register context belongs to.
  uint32_t m_concrete_frame_idx; // The concrete frame index for this register
                                 // context
  uint32_t m_stop_id; // The stop ID that any data in this context is valid for
private:
  // For RegisterContext only
  RegisterContext(const RegisterContext &) = delete;
  const RegisterContext &operator=(const RegisterContext &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_REGISTERCONTEXT_H
