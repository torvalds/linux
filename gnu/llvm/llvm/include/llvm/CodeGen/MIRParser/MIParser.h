//===- MIParser.h - Machine Instructions Parser -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the function that parses the machine instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRPARSER_MIPARSER_H
#define LLVM_CODEGEN_MIRPARSER_MIPARSER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/SMLoc.h"
#include <map>
#include <utility>

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MDNode;
class RegisterBank;
struct SlotMapping;
class SMDiagnostic;
class SourceMgr;
class StringRef;
class TargetRegisterClass;
class TargetSubtargetInfo;

struct VRegInfo {
  enum uint8_t {
    UNKNOWN, NORMAL, GENERIC, REGBANK
  } Kind = UNKNOWN;
  bool Explicit = false; ///< VReg was explicitly specified in the .mir file.
  union {
    const TargetRegisterClass *RC;
    const RegisterBank *RegBank;
  } D;
  Register VReg;
  Register PreferredReg;
};

using Name2RegClassMap = StringMap<const TargetRegisterClass *>;
using Name2RegBankMap = StringMap<const RegisterBank *>;

struct PerTargetMIParsingState {
private:
  const TargetSubtargetInfo &Subtarget;

  /// Maps from instruction names to op codes.
  StringMap<unsigned> Names2InstrOpCodes;

  /// Maps from register names to registers.
  StringMap<Register> Names2Regs;

  /// Maps from register mask names to register masks.
  StringMap<const uint32_t *> Names2RegMasks;

  /// Maps from subregister names to subregister indices.
  StringMap<unsigned> Names2SubRegIndices;

  /// Maps from target index names to target indices.
  StringMap<int> Names2TargetIndices;

  /// Maps from direct target flag names to the direct target flag values.
  StringMap<unsigned> Names2DirectTargetFlags;

  /// Maps from direct target flag names to the bitmask target flag values.
  StringMap<unsigned> Names2BitmaskTargetFlags;

  /// Maps from MMO target flag names to MMO target flag values.
  StringMap<MachineMemOperand::Flags> Names2MMOTargetFlags;

  /// Maps from register class names to register classes.
  Name2RegClassMap Names2RegClasses;

  /// Maps from register bank names to register banks.
  Name2RegBankMap Names2RegBanks;

  void initNames2InstrOpCodes();
  void initNames2Regs();
  void initNames2RegMasks();
  void initNames2SubRegIndices();
  void initNames2TargetIndices();
  void initNames2DirectTargetFlags();
  void initNames2BitmaskTargetFlags();
  void initNames2MMOTargetFlags();

  void initNames2RegClasses();
  void initNames2RegBanks();

public:
  /// Try to convert an instruction name to an opcode. Return true if the
  /// instruction name is invalid.
  bool parseInstrName(StringRef InstrName, unsigned &OpCode);

  /// Try to convert a register name to a register number. Return true if the
  /// register name is invalid.
  bool getRegisterByName(StringRef RegName, Register &Reg);

  /// Check if the given identifier is a name of a register mask.
  ///
  /// Return null if the identifier isn't a register mask.
  const uint32_t *getRegMask(StringRef Identifier);

  /// Check if the given identifier is a name of a subregister index.
  ///
  /// Return 0 if the name isn't a subregister index class.
  unsigned getSubRegIndex(StringRef Name);

  /// Try to convert a name of target index to the corresponding target index.
  ///
  /// Return true if the name isn't a name of a target index.
  bool getTargetIndex(StringRef Name, int &Index);

  /// Try to convert a name of a direct target flag to the corresponding
  /// target flag.
  ///
  /// Return true if the name isn't a name of a direct flag.
  bool getDirectTargetFlag(StringRef Name, unsigned &Flag);

  /// Try to convert a name of a bitmask target flag to the corresponding
  /// target flag.
  ///
  /// Return true if the name isn't a name of a bitmask target flag.
  bool getBitmaskTargetFlag(StringRef Name, unsigned &Flag);

  /// Try to convert a name of a MachineMemOperand target flag to the
  /// corresponding target flag.
  ///
  /// Return true if the name isn't a name of a target MMO flag.
  bool getMMOTargetFlag(StringRef Name, MachineMemOperand::Flags &Flag);

  /// Check if the given identifier is a name of a register class.
  ///
  /// Return null if the name isn't a register class.
  const TargetRegisterClass *getRegClass(StringRef Name);

  /// Check if the given identifier is a name of a register bank.
  ///
  /// Return null if the name isn't a register bank.
  const RegisterBank *getRegBank(StringRef Name);

  PerTargetMIParsingState(const TargetSubtargetInfo &STI)
    : Subtarget(STI) {
    initNames2RegClasses();
    initNames2RegBanks();
  }

  ~PerTargetMIParsingState() = default;

  void setTarget(const TargetSubtargetInfo &NewSubtarget);
};

struct PerFunctionMIParsingState {
  BumpPtrAllocator Allocator;
  MachineFunction &MF;
  SourceMgr *SM;
  const SlotMapping &IRSlots;
  PerTargetMIParsingState &Target;

  std::map<unsigned, TrackingMDNodeRef> MachineMetadataNodes;
  std::map<unsigned, std::pair<TempMDTuple, SMLoc>> MachineForwardRefMDNodes;

  DenseMap<unsigned, MachineBasicBlock *> MBBSlots;
  DenseMap<Register, VRegInfo *> VRegInfos;
  StringMap<VRegInfo *> VRegInfosNamed;
  DenseMap<unsigned, int> FixedStackObjectSlots;
  DenseMap<unsigned, int> StackObjectSlots;
  DenseMap<unsigned, unsigned> ConstantPoolSlots;
  DenseMap<unsigned, unsigned> JumpTableSlots;

  /// Maps from slot numbers to function's unnamed values.
  DenseMap<unsigned, const Value *> Slots2Values;

  PerFunctionMIParsingState(MachineFunction &MF, SourceMgr &SM,
                            const SlotMapping &IRSlots,
                            PerTargetMIParsingState &Target);

  VRegInfo &getVRegInfo(Register Num);
  VRegInfo &getVRegInfoNamed(StringRef RegName);
  const Value *getIRValue(unsigned Slot);
};

/// Parse the machine basic block definitions, and skip the machine
/// instructions.
///
/// This function runs the first parsing pass on the machine function's body.
/// It parses only the machine basic block definitions and creates the machine
/// basic blocks in the given machine function.
///
/// The machine instructions aren't parsed during the first pass because all
/// the machine basic blocks aren't defined yet - this makes it impossible to
/// resolve the machine basic block references.
///
/// Return true if an error occurred.
bool parseMachineBasicBlockDefinitions(PerFunctionMIParsingState &PFS,
                                       StringRef Src, SMDiagnostic &Error);

/// Parse the machine instructions.
///
/// This function runs the second parsing pass on the machine function's body.
/// It skips the machine basic block definitions and parses only the machine
/// instructions and basic block attributes like liveins and successors.
///
/// The second parsing pass assumes that the first parsing pass already ran
/// on the given source string.
///
/// Return true if an error occurred.
bool parseMachineInstructions(PerFunctionMIParsingState &PFS, StringRef Src,
                              SMDiagnostic &Error);

bool parseMBBReference(PerFunctionMIParsingState &PFS,
                       MachineBasicBlock *&MBB, StringRef Src,
                       SMDiagnostic &Error);

bool parseRegisterReference(PerFunctionMIParsingState &PFS,
                            Register &Reg, StringRef Src,
                            SMDiagnostic &Error);

bool parseNamedRegisterReference(PerFunctionMIParsingState &PFS, Register &Reg,
                                 StringRef Src, SMDiagnostic &Error);

bool parseVirtualRegisterReference(PerFunctionMIParsingState &PFS,
                                   VRegInfo *&Info, StringRef Src,
                                   SMDiagnostic &Error);

bool parseStackObjectReference(PerFunctionMIParsingState &PFS, int &FI,
                               StringRef Src, SMDiagnostic &Error);

bool parseMDNode(PerFunctionMIParsingState &PFS, MDNode *&Node, StringRef Src,
                 SMDiagnostic &Error);

bool parseMachineMetadata(PerFunctionMIParsingState &PFS, StringRef Src,
                          SMRange SourceRange, SMDiagnostic &Error);

} // end namespace llvm

#endif // LLVM_CODEGEN_MIRPARSER_MIPARSER_H
