
//===------------ MIRVRegNamerUtils.h - MIR VReg Renaming Utilities -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The purpose of these utilities is to abstract out parts of the MIRCanon pass
// that are responsible for renaming virtual registers with the purpose of
// sharing code with a MIRVRegNamer pass that could be the analog of the
// opt -instnamer pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_MIRVREGNAMERUTILS_H
#define LLVM_LIB_CODEGEN_MIRVREGNAMERUTILS_H

#include "llvm/CodeGen/Register.h"
#include <map>
#include <vector>
#include <string>

namespace llvm {

class MachineBasicBlock;
class MachineInstr;
class MachineRegisterInfo;
class StringRef;

/// VRegRenamer - This class is used for renaming vregs in a machine basic
/// block according to semantics of the instruction.
class VRegRenamer {
  class NamedVReg {
    Register Reg;
    std::string Name;

  public:
    NamedVReg(Register Reg, std::string Name = "") : Reg(Reg), Name(Name) {}
    NamedVReg(std::string Name = "") : Reg(~0U), Name(Name) {}

    const std::string &getName() const { return Name; }

    Register getReg() const { return Reg; }
  };

  MachineRegisterInfo &MRI;

  unsigned CurrentBBNumber = 0;

  /// Given an Instruction, construct a hash of the operands
  /// of the instructions along with the opcode.
  /// When dealing with virtual registers, just hash the opcode of
  /// the instruction defining that vreg.
  /// Handle immediates, registers (physical and virtual) explicitly,
  /// and return a common value for the other cases.
  /// Instruction will be named in the following scheme
  /// bb<block_no>_hash_<collission_count>.
  std::string getInstructionOpcodeHash(MachineInstr &MI);

  /// For all the VRegs that are candidates for renaming,
  /// return a mapping from old vregs to new vregs with names.
  std::map<unsigned, unsigned>
  getVRegRenameMap(const std::vector<NamedVReg> &VRegs);

  /// Perform replacing of registers based on the <old,new> vreg map.
  bool doVRegRenaming(const std::map<unsigned, unsigned> &VRegRenameMap);

  /// createVirtualRegister - Given an existing vreg, create a named vreg to
  /// take its place. The name is determined by calling
  /// getInstructionOpcodeHash.
  unsigned createVirtualRegister(unsigned VReg);

  /// Create a vreg with name and return it.
  unsigned createVirtualRegisterWithLowerName(unsigned VReg, StringRef Name);

  /// Linearly traverse the MachineBasicBlock and rename each instruction's
  /// vreg definition based on the semantics of the instruction.
  /// Names are as follows bb<BBNum>_hash_[0-9]+
  bool renameInstsInMBB(MachineBasicBlock *MBB);

public:
  VRegRenamer() = delete;
  VRegRenamer(MachineRegisterInfo &MRI) : MRI(MRI) {}

  /// Same as the above, but sets a BBNum depending on BB traversal that
  /// will be used as prefix for the vreg names.
  bool renameVRegs(MachineBasicBlock *MBB, unsigned BBNum) {
    CurrentBBNumber = BBNum;
    return renameInstsInMBB(MBB);
  }
};

} // namespace llvm

#endif
