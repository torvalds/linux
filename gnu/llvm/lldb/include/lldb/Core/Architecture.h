//===-- Architecture.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_ARCHITECTURE_H
#define LLDB_CORE_ARCHITECTURE_H

#include "lldb/Core/PluginInterface.h"
#include "lldb/Target/DynamicRegisterInfo.h"
#include "lldb/Target/MemoryTagManager.h"

namespace lldb_private {

class Architecture : public PluginInterface {
public:
  /// This is currently intended to handle cases where a
  /// program stops at an instruction that won't get executed and it
  /// allows the stop reason, like "breakpoint hit", to be replaced
  /// with a different stop reason like "no stop reason".
  ///
  /// This is specifically used for ARM in Thumb code when we stop in
  /// an IT instruction (if/then/else) where the instruction won't get
  /// executed and therefore it wouldn't be correct to show the program
  /// stopped at the current PC. The code is generic and applies to all
  /// ARM CPUs.
  virtual void OverrideStopInfo(Thread &thread) const = 0;

  /// This method is used to get the number of bytes that should be
  /// skipped, from function start address, to reach the first
  /// instruction after the prologue. If overrode, it must return
  /// non-zero only if the current address matches one of the known
  /// function entry points.
  ///
  /// This method is called only if the standard platform-independent
  /// code fails to get the number of bytes to skip, giving the plugin
  /// a chance to try to find the missing info.
  ///
  /// This is specifically used for PPC64, where functions may have
  /// more than one entry point, global and local, so both should
  /// be compared with current address, in order to find out the
  /// number of bytes that should be skipped, in case we are stopped
  /// at either function entry point.
  virtual size_t GetBytesToSkip(Symbol &func, const Address &curr_addr) const {
    return 0;
  }

  /// Adjust function breakpoint address, if needed. In some cases,
  /// the function start address is not the right place to set the
  /// breakpoint, specially in functions with multiple entry points.
  ///
  /// This is specifically used for PPC64, for functions that have
  /// both a global and a local entry point. In this case, the
  /// breakpoint is adjusted to the first function address reached
  /// by both entry points.
  virtual void AdjustBreakpointAddress(const Symbol &func,
                                       Address &addr) const {}


  /// Get \a load_addr as a callable code load address for this target
  ///
  /// Take \a load_addr and potentially add any address bits that are
  /// needed to make the address callable. For ARM this can set bit
  /// zero (if it already isn't) if \a load_addr is a thumb function.
  /// If \a addr_class is set to AddressClass::eInvalid, then the address
  /// adjustment will always happen. If it is set to an address class
  /// that doesn't have code in it, LLDB_INVALID_ADDRESS will be
  /// returned.
  virtual lldb::addr_t GetCallableLoadAddress(
      lldb::addr_t addr, AddressClass addr_class = AddressClass::eInvalid) const {
    return addr;
  }

  /// Get \a load_addr as an opcode for this target.
  ///
  /// Take \a load_addr and potentially strip any address bits that are
  /// needed to make the address point to an opcode. For ARM this can
  /// clear bit zero (if it already isn't) if \a load_addr is a
  /// thumb function and load_addr is in code.
  /// If \a addr_class is set to AddressClass::eInvalid, then the address
  /// adjustment will always happen. If it is set to an address class
  /// that doesn't have code in it, LLDB_INVALID_ADDRESS will be
  /// returned.

  virtual lldb::addr_t GetOpcodeLoadAddress(
      lldb::addr_t addr, AddressClass addr_class = AddressClass::eInvalid) const {
    return addr;
  }

  // Get load_addr as breakable load address for this target. Take a addr and
  // check if for any reason there is a better address than this to put a
  // breakpoint on. If there is then return that address. For MIPS, if
  // instruction at addr is a delay slot instruction then this method will find
  // the address of its previous instruction and return that address.
  virtual lldb::addr_t GetBreakableLoadAddress(lldb::addr_t addr,
                                               Target &target) const {
    return addr;
  }

  // Returns a pointer to an object that can manage memory tags for this
  // Architecture E.g. masking out tags, unpacking tag streams etc. Returns
  // nullptr if the architecture does not have a memory tagging extension.
  //
  // The return pointer being valid does not mean that the current process has
  // memory tagging enabled, just that a tagging technology exists for this
  // architecture.
  virtual const MemoryTagManager *GetMemoryTagManager() const {
    return nullptr;
  }

  // This returns true if a write to the named register should cause lldb to
  // reconfigure its register information. For example on AArch64 writing to vg
  // to change the vector length means lldb has to change the size of registers.
  virtual bool
  RegisterWriteCausesReconfigure(const llvm::StringRef name) const {
    return false;
  }

  // Call this after writing a register for which RegisterWriteCausesReconfigure
  // returns true. This method will update the layout of registers according to
  // the new state e.g. the new length of scalable vector registers.
  // Returns true if anything changed, which means existing register values must
  // be invalidated.
  virtual bool ReconfigureRegisterInfo(DynamicRegisterInfo &reg_info,
                                       DataExtractor &reg_data,
                                       RegisterContext &reg_context) const {
    return false;
  }
};

} // namespace lldb_private

#endif // LLDB_CORE_ARCHITECTURE_H
