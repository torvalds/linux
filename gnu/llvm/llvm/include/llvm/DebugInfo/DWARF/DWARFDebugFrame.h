//===- DWARFDebugFrame.h - Parsing of .debug_frame --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"
#include <map>
#include <memory>
#include <vector>

namespace llvm {

class raw_ostream;
class DWARFDataExtractor;
class MCRegisterInfo;
struct DIDumpOptions;

namespace dwarf {

constexpr uint32_t InvalidRegisterNumber = UINT32_MAX;

/// A class that represents a location for the Call Frame Address (CFA) or a
/// register. This is decoded from the DWARF Call Frame Information
/// instructions and put into an UnwindRow.
class UnwindLocation {
public:
  enum Location {
    /// Not specified.
    Unspecified,
    /// Register is not available and can't be recovered.
    Undefined,
    /// Register value is in the register, nothing needs to be done to unwind
    /// it:
    ///   reg = reg
    Same,
    /// Register is in or at the CFA plus an offset:
    ///   reg = CFA + offset
    ///   reg = defef(CFA + offset)
    CFAPlusOffset,
    /// Register or CFA is in or at a register plus offset, optionally in
    /// an address space:
    ///   reg = reg + offset [in addrspace]
    ///   reg = deref(reg + offset [in addrspace])
    RegPlusOffset,
    /// Register or CFA value is in or at a value found by evaluating a DWARF
    /// expression:
    ///   reg = eval(dwarf_expr)
    ///   reg = deref(eval(dwarf_expr))
    DWARFExpr,
    /// Value is a constant value contained in "Offset":
    ///   reg = Offset
    Constant,
  };

private:
  Location Kind;   /// The type of the location that describes how to unwind it.
  uint32_t RegNum; /// The register number for Kind == RegPlusOffset.
  int32_t Offset;  /// The offset for Kind == CFAPlusOffset or RegPlusOffset.
  std::optional<uint32_t> AddrSpace;   /// The address space for Kind ==
                                       /// RegPlusOffset for CFA.
  std::optional<DWARFExpression> Expr; /// The DWARF expression for Kind ==
                                       /// DWARFExpression.
  bool Dereference; /// If true, the resulting location must be dereferenced
                    /// after the location value is computed.

  // Constructors are private to force people to use the create static
  // functions.
  UnwindLocation(Location K)
      : Kind(K), RegNum(InvalidRegisterNumber), Offset(0),
        AddrSpace(std::nullopt), Dereference(false) {}

  UnwindLocation(Location K, uint32_t Reg, int32_t Off,
                 std::optional<uint32_t> AS, bool Deref)
      : Kind(K), RegNum(Reg), Offset(Off), AddrSpace(AS), Dereference(Deref) {}

  UnwindLocation(DWARFExpression E, bool Deref)
      : Kind(DWARFExpr), RegNum(InvalidRegisterNumber), Offset(0), Expr(E),
        Dereference(Deref) {}

public:
  /// Create a location whose rule is set to Unspecified. This means the
  /// register value might be in the same register but it wasn't specified in
  /// the unwind opcodes.
  static UnwindLocation createUnspecified();
  /// Create a location where the value is undefined and not available. This can
  /// happen when a register is volatile and can't be recovered.
  static UnwindLocation createUndefined();
  /// Create a location where the value is known to be in the register itself.
  static UnwindLocation createSame();
  /// Create a location that is in (Deref == false) or at (Deref == true) the
  /// CFA plus an offset. Most registers that are spilled onto the stack use
  /// this rule. The rule for the register will use this rule and specify a
  /// unique offset from the CFA with \a Deref set to true. This value will be
  /// relative to a CFA value which is typically defined using the register
  /// plus offset location. \see createRegisterPlusOffset(...) for more
  /// information.
  static UnwindLocation createIsCFAPlusOffset(int32_t Off);
  static UnwindLocation createAtCFAPlusOffset(int32_t Off);
  /// Create a location where the saved value is in (Deref == false) or at
  /// (Deref == true) a regiser plus an offset and, optionally, in the specified
  /// address space (used mostly for the CFA).
  ///
  /// The CFA is usually defined using this rule by using the stack pointer or
  /// frame pointer as the register, with an offset that accounts for all
  /// spilled registers and all local variables in a function, and Deref ==
  /// false.
  static UnwindLocation
  createIsRegisterPlusOffset(uint32_t Reg, int32_t Off,
                             std::optional<uint32_t> AddrSpace = std::nullopt);
  static UnwindLocation
  createAtRegisterPlusOffset(uint32_t Reg, int32_t Off,
                             std::optional<uint32_t> AddrSpace = std::nullopt);
  /// Create a location whose value is the result of evaluating a DWARF
  /// expression. This allows complex expressions to be evaluated in order to
  /// unwind a register or CFA value.
  static UnwindLocation createIsDWARFExpression(DWARFExpression Expr);
  static UnwindLocation createAtDWARFExpression(DWARFExpression Expr);
  static UnwindLocation createIsConstant(int32_t Value);

  Location getLocation() const { return Kind; }
  uint32_t getRegister() const { return RegNum; }
  int32_t getOffset() const { return Offset; }
  uint32_t getAddressSpace() const {
    assert(Kind == RegPlusOffset && AddrSpace);
    return *AddrSpace;
  }
  int32_t getConstant() const { return Offset; }
  /// Some opcodes will modify the CFA location's register only, so we need
  /// to be able to modify the CFA register when evaluating DWARF Call Frame
  /// Information opcodes.
  void setRegister(uint32_t NewRegNum) { RegNum = NewRegNum; }
  /// Some opcodes will modify the CFA location's offset only, so we need
  /// to be able to modify the CFA offset when evaluating DWARF Call Frame
  /// Information opcodes.
  void setOffset(int32_t NewOffset) { Offset = NewOffset; }
  /// Some opcodes modify a constant value and we need to be able to update
  /// the constant value (DW_CFA_GNU_window_save which is also known as
  // DW_CFA_AARCH64_negate_ra_state).
  void setConstant(int32_t Value) { Offset = Value; }

  std::optional<DWARFExpression> getDWARFExpressionBytes() const {
    return Expr;
  }
  /// Dump a location expression as text and use the register information if
  /// some is provided.
  ///
  /// \param OS the stream to use for output.
  ///
  /// \param MRI register information that helps emit register names insteead
  /// of raw register numbers.
  ///
  /// \param IsEH true if the DWARF Call Frame Information is from .eh_frame
  /// instead of from .debug_frame. This is needed for register number
  /// conversion because some register numbers differ between the two sections
  /// for certain architectures like x86.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) const;

  bool operator==(const UnwindLocation &RHS) const;
};

raw_ostream &operator<<(raw_ostream &OS, const UnwindLocation &R);

/// A class that can track all registers with locations in a UnwindRow object.
///
/// Register locations use a map where the key is the register number and the
/// the value is a UnwindLocation.
///
/// The register maps are put into a class so that all register locations can
/// be copied when parsing the unwind opcodes DW_CFA_remember_state and
/// DW_CFA_restore_state.
class RegisterLocations {
  std::map<uint32_t, UnwindLocation> Locations;

public:
  /// Return the location for the register in \a RegNum if there is a location.
  ///
  /// \param RegNum the register number to find a location for.
  ///
  /// \returns A location if one is available for \a RegNum, or std::nullopt
  /// otherwise.
  std::optional<UnwindLocation> getRegisterLocation(uint32_t RegNum) const {
    auto Pos = Locations.find(RegNum);
    if (Pos == Locations.end())
      return std::nullopt;
    return Pos->second;
  }

  /// Set the location for the register in \a RegNum to \a Location.
  ///
  /// \param RegNum the register number to set the location for.
  ///
  /// \param Location the UnwindLocation that describes how to unwind the value.
  void setRegisterLocation(uint32_t RegNum, const UnwindLocation &Location) {
    Locations.erase(RegNum);
    Locations.insert(std::make_pair(RegNum, Location));
  }

  /// Removes any rule for the register in \a RegNum.
  ///
  /// \param RegNum the register number to remove the location for.
  void removeRegisterLocation(uint32_t RegNum) { Locations.erase(RegNum); }

  /// Dump all registers + locations that are currently defined in this object.
  ///
  /// \param OS the stream to use for output.
  ///
  /// \param MRI register information that helps emit register names insteead
  /// of raw register numbers.
  ///
  /// \param IsEH true if the DWARF Call Frame Information is from .eh_frame
  /// instead of from .debug_frame. This is needed for register number
  /// conversion because some register numbers differ between the two sections
  /// for certain architectures like x86.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) const;

  /// Returns true if we have any register locations in this object.
  bool hasLocations() const { return !Locations.empty(); }

  size_t size() const { return Locations.size(); }

  bool operator==(const RegisterLocations &RHS) const {
    return Locations == RHS.Locations;
  }
};

raw_ostream &operator<<(raw_ostream &OS, const RegisterLocations &RL);

/// A class that represents a single row in the unwind table that is decoded by
/// parsing the DWARF Call Frame Information opcodes.
///
/// The row consists of an optional address, the rule to unwind the CFA and all
/// rules to unwind any registers. If the address doesn't have a value, this
/// row represents the initial instructions for a CIE. If the address has a
/// value the UnwindRow represents a row in the UnwindTable for a FDE. The
/// address is the first address for which the CFA location and register rules
/// are valid within a function.
///
/// UnwindRow objects are created by parsing opcodes in the DWARF Call Frame
/// Information and UnwindRow objects are lazily populated and pushed onto a
/// stack in the UnwindTable when evaluating this state machine. Accessors are
/// needed for the address, CFA value, and register locations as the opcodes
/// encode a state machine that produces a sorted array of UnwindRow objects
/// \see UnwindTable.
class UnwindRow {
  /// The address will be valid when parsing the instructions in a FDE. If
  /// invalid, this object represents the initial instructions of a CIE.
  std::optional<uint64_t> Address; ///< Address for row in FDE, invalid for CIE.
  UnwindLocation CFAValue;    ///< How to unwind the Call Frame Address (CFA).
  RegisterLocations RegLocs;  ///< How to unwind all registers in this list.

public:
  UnwindRow() : CFAValue(UnwindLocation::createUnspecified()) {}

  /// Returns true if the address is valid in this object.
  bool hasAddress() const { return Address.has_value(); }

  /// Get the address for this row.
  ///
  /// Clients should only call this function after verifying it has a valid
  /// address with a call to \see hasAddress().
  uint64_t getAddress() const { return *Address; }

  /// Set the address for this UnwindRow.
  ///
  /// The address represents the first address for which the CFAValue and
  /// RegLocs are valid within a function.
  void setAddress(uint64_t Addr) { Address = Addr; }

  /// Offset the address for this UnwindRow.
  ///
  /// The address represents the first address for which the CFAValue and
  /// RegLocs are valid within a function. Clients must ensure that this object
  /// already has an address (\see hasAddress()) prior to calling this
  /// function.
  void slideAddress(uint64_t Offset) { *Address += Offset; }
  UnwindLocation &getCFAValue() { return CFAValue; }
  const UnwindLocation &getCFAValue() const { return CFAValue; }
  RegisterLocations &getRegisterLocations() { return RegLocs; }
  const RegisterLocations &getRegisterLocations() const { return RegLocs; }

  /// Dump the UnwindRow to the stream.
  ///
  /// \param OS the stream to use for output.
  ///
  /// \param MRI register information that helps emit register names insteead
  /// of raw register numbers.
  ///
  /// \param IsEH true if the DWARF Call Frame Information is from .eh_frame
  /// instead of from .debug_frame. This is needed for register number
  /// conversion because some register numbers differ between the two sections
  /// for certain architectures like x86.
  ///
  /// \param IndentLevel specify the indent level as an integer. The UnwindRow
  /// will be output to the stream preceded by 2 * IndentLevel number of spaces.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts,
            unsigned IndentLevel = 0) const;
};

raw_ostream &operator<<(raw_ostream &OS, const UnwindRow &Row);

class CFIProgram;
class CIE;
class FDE;

/// A class that contains all UnwindRow objects for an FDE or a single unwind
/// row for a CIE. To unwind an address the rows, which are sorted by start
/// address, can be searched to find the UnwindRow with the lowest starting
/// address that is greater than or equal to the address that is being looked
/// up.
class UnwindTable {
public:
  using RowContainer = std::vector<UnwindRow>;
  using iterator = RowContainer::iterator;
  using const_iterator = RowContainer::const_iterator;

  size_t size() const { return Rows.size(); }
  iterator begin() { return Rows.begin(); }
  const_iterator begin() const { return Rows.begin(); }
  iterator end() { return Rows.end(); }
  const_iterator end() const { return Rows.end(); }
  const UnwindRow &operator[](size_t Index) const {
    assert(Index < size());
    return Rows[Index];
  }

  /// Dump the UnwindTable to the stream.
  ///
  /// \param OS the stream to use for output.
  ///
  /// \param MRI register information that helps emit register names insteead
  /// of raw register numbers.
  ///
  /// \param IsEH true if the DWARF Call Frame Information is from .eh_frame
  /// instead of from .debug_frame. This is needed for register number
  /// conversion because some register numbers differ between the two sections
  /// for certain architectures like x86.
  ///
  /// \param IndentLevel specify the indent level as an integer. The UnwindRow
  /// will be output to the stream preceded by 2 * IndentLevel number of spaces.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts,
            unsigned IndentLevel = 0) const;

  /// Create an UnwindTable from a Common Information Entry (CIE).
  ///
  /// \param Cie The Common Information Entry to extract the table from. The
  /// CFIProgram is retrieved from the \a Cie object and used to create the
  /// UnwindTable.
  ///
  /// \returns An error if the DWARF Call Frame Information opcodes have state
  /// machine errors, or a valid UnwindTable otherwise.
  static Expected<UnwindTable> create(const CIE *Cie);

  /// Create an UnwindTable from a Frame Descriptor Entry (FDE).
  ///
  /// \param Fde The Frame Descriptor Entry to extract the table from. The
  /// CFIProgram is retrieved from the \a Fde object and used to create the
  /// UnwindTable.
  ///
  /// \returns An error if the DWARF Call Frame Information opcodes have state
  /// machine errors, or a valid UnwindTable otherwise.
  static Expected<UnwindTable> create(const FDE *Fde);

private:
  RowContainer Rows;
  /// The end address when data is extracted from a FDE. This value will be
  /// invalid when a UnwindTable is extracted from a CIE.
  std::optional<uint64_t> EndAddress;

  /// Parse the information in the CFIProgram and update the CurrRow object
  /// that the state machine describes.
  ///
  /// This is an internal implementation that emulates the state machine
  /// described in the DWARF Call Frame Information opcodes and will push
  /// CurrRow onto the Rows container when needed.
  ///
  /// \param CFIP the CFI program that contains the opcodes from a CIE or FDE.
  ///
  /// \param CurrRow the current row to modify while parsing the state machine.
  ///
  /// \param InitialLocs If non-NULL, we are parsing a FDE and this contains
  /// the initial register locations from the CIE. If NULL, then a CIE's
  /// opcodes are being parsed and this is not needed. This is used for the
  /// DW_CFA_restore and DW_CFA_restore_extended opcodes.
  Error parseRows(const CFIProgram &CFIP, UnwindRow &CurrRow,
                  const RegisterLocations *InitialLocs);
};

raw_ostream &operator<<(raw_ostream &OS, const UnwindTable &Rows);

/// Represent a sequence of Call Frame Information instructions that, when read
/// in order, construct a table mapping PC to frame state. This can also be
/// referred to as "CFI rules" in DWARF literature to avoid confusion with
/// computer programs in the broader sense, and in this context each instruction
/// would be a rule to establish the mapping. Refer to pg. 172 in the DWARF5
/// manual, "6.4.1 Structure of Call Frame Information".
class CFIProgram {
public:
  static constexpr size_t MaxOperands = 3;
  typedef SmallVector<uint64_t, MaxOperands> Operands;

  /// An instruction consists of a DWARF CFI opcode and an optional sequence of
  /// operands. If it refers to an expression, then this expression has its own
  /// sequence of operations and operands handled separately by DWARFExpression.
  struct Instruction {
    Instruction(uint8_t Opcode) : Opcode(Opcode) {}

    uint8_t Opcode;
    Operands Ops;
    // Associated DWARF expression in case this instruction refers to one
    std::optional<DWARFExpression> Expression;

    Expected<uint64_t> getOperandAsUnsigned(const CFIProgram &CFIP,
                                            uint32_t OperandIdx) const;

    Expected<int64_t> getOperandAsSigned(const CFIProgram &CFIP,
                                         uint32_t OperandIdx) const;
  };

  using InstrList = std::vector<Instruction>;
  using iterator = InstrList::iterator;
  using const_iterator = InstrList::const_iterator;

  iterator begin() { return Instructions.begin(); }
  const_iterator begin() const { return Instructions.begin(); }
  iterator end() { return Instructions.end(); }
  const_iterator end() const { return Instructions.end(); }

  unsigned size() const { return (unsigned)Instructions.size(); }
  bool empty() const { return Instructions.empty(); }
  uint64_t codeAlign() const { return CodeAlignmentFactor; }
  int64_t dataAlign() const { return DataAlignmentFactor; }
  Triple::ArchType triple() const { return Arch; }

  CFIProgram(uint64_t CodeAlignmentFactor, int64_t DataAlignmentFactor,
             Triple::ArchType Arch)
      : CodeAlignmentFactor(CodeAlignmentFactor),
        DataAlignmentFactor(DataAlignmentFactor),
        Arch(Arch) {}

  /// Parse and store a sequence of CFI instructions from Data,
  /// starting at *Offset and ending at EndOffset. *Offset is updated
  /// to EndOffset upon successful parsing, or indicates the offset
  /// where a problem occurred in case an error is returned.
  Error parse(DWARFDataExtractor Data, uint64_t *Offset, uint64_t EndOffset);

  void dump(raw_ostream &OS, DIDumpOptions DumpOpts, unsigned IndentLevel,
            std::optional<uint64_t> InitialLocation) const;

  void addInstruction(const Instruction &I) { Instructions.push_back(I); }

  /// Get a DWARF CFI call frame string for the given DW_CFA opcode.
  StringRef callFrameString(unsigned Opcode) const;

private:
  std::vector<Instruction> Instructions;
  const uint64_t CodeAlignmentFactor;
  const int64_t DataAlignmentFactor;
  Triple::ArchType Arch;

  /// Convenience method to add a new instruction with the given opcode.
  void addInstruction(uint8_t Opcode) {
    Instructions.push_back(Instruction(Opcode));
  }

  /// Add a new single-operand instruction.
  void addInstruction(uint8_t Opcode, uint64_t Operand1) {
    Instructions.push_back(Instruction(Opcode));
    Instructions.back().Ops.push_back(Operand1);
  }

  /// Add a new instruction that has two operands.
  void addInstruction(uint8_t Opcode, uint64_t Operand1, uint64_t Operand2) {
    Instructions.push_back(Instruction(Opcode));
    Instructions.back().Ops.push_back(Operand1);
    Instructions.back().Ops.push_back(Operand2);
  }

  /// Add a new instruction that has three operands.
  void addInstruction(uint8_t Opcode, uint64_t Operand1, uint64_t Operand2,
                      uint64_t Operand3) {
    Instructions.push_back(Instruction(Opcode));
    Instructions.back().Ops.push_back(Operand1);
    Instructions.back().Ops.push_back(Operand2);
    Instructions.back().Ops.push_back(Operand3);
  }

  /// Types of operands to CFI instructions
  /// In DWARF, this type is implicitly tied to a CFI instruction opcode and
  /// thus this type doesn't need to be explicitly written to the file (this is
  /// not a DWARF encoding). The relationship of instrs to operand types can
  /// be obtained from getOperandTypes() and is only used to simplify
  /// instruction printing.
  enum OperandType {
    OT_Unset,
    OT_None,
    OT_Address,
    OT_Offset,
    OT_FactoredCodeOffset,
    OT_SignedFactDataOffset,
    OT_UnsignedFactDataOffset,
    OT_Register,
    OT_AddressSpace,
    OT_Expression
  };

  /// Get the OperandType as a "const char *".
  static const char *operandTypeString(OperandType OT);

  /// Retrieve the array describing the types of operands according to the enum
  /// above. This is indexed by opcode.
  static ArrayRef<OperandType[MaxOperands]> getOperandTypes();

  /// Print \p Opcode's operand number \p OperandIdx which has value \p Operand.
  void printOperand(raw_ostream &OS, DIDumpOptions DumpOpts,
                    const Instruction &Instr, unsigned OperandIdx,
                    uint64_t Operand, std::optional<uint64_t> &Address) const;
};

/// An entry in either debug_frame or eh_frame. This entry can be a CIE or an
/// FDE.
class FrameEntry {
public:
  enum FrameKind { FK_CIE, FK_FDE };

  FrameEntry(FrameKind K, bool IsDWARF64, uint64_t Offset, uint64_t Length,
             uint64_t CodeAlign, int64_t DataAlign, Triple::ArchType Arch)
      : Kind(K), IsDWARF64(IsDWARF64), Offset(Offset), Length(Length),
        CFIs(CodeAlign, DataAlign, Arch) {}

  virtual ~FrameEntry() = default;

  FrameKind getKind() const { return Kind; }
  uint64_t getOffset() const { return Offset; }
  uint64_t getLength() const { return Length; }
  const CFIProgram &cfis() const { return CFIs; }
  CFIProgram &cfis() { return CFIs; }

  /// Dump the instructions in this CFI fragment
  virtual void dump(raw_ostream &OS, DIDumpOptions DumpOpts) const = 0;

protected:
  const FrameKind Kind;

  const bool IsDWARF64;

  /// Offset of this entry in the section.
  const uint64_t Offset;

  /// Entry length as specified in DWARF.
  const uint64_t Length;

  CFIProgram CFIs;
};

/// DWARF Common Information Entry (CIE)
class CIE : public FrameEntry {
public:
  // CIEs (and FDEs) are simply container classes, so the only sensible way to
  // create them is by providing the full parsed contents in the constructor.
  CIE(bool IsDWARF64, uint64_t Offset, uint64_t Length, uint8_t Version,
      SmallString<8> Augmentation, uint8_t AddressSize,
      uint8_t SegmentDescriptorSize, uint64_t CodeAlignmentFactor,
      int64_t DataAlignmentFactor, uint64_t ReturnAddressRegister,
      SmallString<8> AugmentationData, uint32_t FDEPointerEncoding,
      uint32_t LSDAPointerEncoding, std::optional<uint64_t> Personality,
      std::optional<uint32_t> PersonalityEnc, Triple::ArchType Arch)
      : FrameEntry(FK_CIE, IsDWARF64, Offset, Length, CodeAlignmentFactor,
                   DataAlignmentFactor, Arch),
        Version(Version), Augmentation(std::move(Augmentation)),
        AddressSize(AddressSize), SegmentDescriptorSize(SegmentDescriptorSize),
        CodeAlignmentFactor(CodeAlignmentFactor),
        DataAlignmentFactor(DataAlignmentFactor),
        ReturnAddressRegister(ReturnAddressRegister),
        AugmentationData(std::move(AugmentationData)),
        FDEPointerEncoding(FDEPointerEncoding),
        LSDAPointerEncoding(LSDAPointerEncoding), Personality(Personality),
        PersonalityEnc(PersonalityEnc) {}

  static bool classof(const FrameEntry *FE) { return FE->getKind() == FK_CIE; }

  StringRef getAugmentationString() const { return Augmentation; }
  uint64_t getCodeAlignmentFactor() const { return CodeAlignmentFactor; }
  int64_t getDataAlignmentFactor() const { return DataAlignmentFactor; }
  uint8_t getVersion() const { return Version; }
  uint64_t getReturnAddressRegister() const { return ReturnAddressRegister; }
  std::optional<uint64_t> getPersonalityAddress() const { return Personality; }
  std::optional<uint32_t> getPersonalityEncoding() const {
    return PersonalityEnc;
  }

  StringRef getAugmentationData() const { return AugmentationData; }

  uint32_t getFDEPointerEncoding() const { return FDEPointerEncoding; }

  uint32_t getLSDAPointerEncoding() const { return LSDAPointerEncoding; }

  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) const override;

private:
  /// The following fields are defined in section 6.4.1 of the DWARF standard v4
  const uint8_t Version;
  const SmallString<8> Augmentation;
  const uint8_t AddressSize;
  const uint8_t SegmentDescriptorSize;
  const uint64_t CodeAlignmentFactor;
  const int64_t DataAlignmentFactor;
  const uint64_t ReturnAddressRegister;

  // The following are used when the CIE represents an EH frame entry.
  const SmallString<8> AugmentationData;
  const uint32_t FDEPointerEncoding;
  const uint32_t LSDAPointerEncoding;
  const std::optional<uint64_t> Personality;
  const std::optional<uint32_t> PersonalityEnc;
};

/// DWARF Frame Description Entry (FDE)
class FDE : public FrameEntry {
public:
  FDE(bool IsDWARF64, uint64_t Offset, uint64_t Length, uint64_t CIEPointer,
      uint64_t InitialLocation, uint64_t AddressRange, CIE *Cie,
      std::optional<uint64_t> LSDAAddress, Triple::ArchType Arch)
      : FrameEntry(FK_FDE, IsDWARF64, Offset, Length,
                   Cie ? Cie->getCodeAlignmentFactor() : 0,
                   Cie ? Cie->getDataAlignmentFactor() : 0, Arch),
        CIEPointer(CIEPointer), InitialLocation(InitialLocation),
        AddressRange(AddressRange), LinkedCIE(Cie), LSDAAddress(LSDAAddress) {}

  ~FDE() override = default;

  const CIE *getLinkedCIE() const { return LinkedCIE; }
  uint64_t getCIEPointer() const { return CIEPointer; }
  uint64_t getInitialLocation() const { return InitialLocation; }
  uint64_t getAddressRange() const { return AddressRange; }
  std::optional<uint64_t> getLSDAAddress() const { return LSDAAddress; }

  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) const override;

  static bool classof(const FrameEntry *FE) { return FE->getKind() == FK_FDE; }

private:
  /// The following fields are defined in section 6.4.1 of the DWARFv3 standard.
  /// Note that CIE pointers in EH FDEs, unlike DWARF FDEs, contain relative
  /// offsets to the linked CIEs. See the following link for more info:
  /// https://refspecs.linuxfoundation.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html
  const uint64_t CIEPointer;
  const uint64_t InitialLocation;
  const uint64_t AddressRange;
  const CIE *LinkedCIE;
  const std::optional<uint64_t> LSDAAddress;
};

} // end namespace dwarf

/// A parsed .debug_frame or .eh_frame section
class DWARFDebugFrame {
  const Triple::ArchType Arch;
  // True if this is parsing an eh_frame section.
  const bool IsEH;
  // Not zero for sane pointer values coming out of eh_frame
  const uint64_t EHFrameAddress;

  std::vector<std::unique_ptr<dwarf::FrameEntry>> Entries;
  using iterator = pointee_iterator<decltype(Entries)::const_iterator>;

  /// Return the entry at the given offset or nullptr.
  dwarf::FrameEntry *getEntryAtOffset(uint64_t Offset) const;

public:
  // If IsEH is true, assume it is a .eh_frame section. Otherwise,
  // it is a .debug_frame section. EHFrameAddress should be different
  // than zero for correct parsing of .eh_frame addresses when they
  // use a PC-relative encoding.
  DWARFDebugFrame(Triple::ArchType Arch,
                  bool IsEH = false, uint64_t EHFrameAddress = 0);
  ~DWARFDebugFrame();

  /// Dump the section data into the given stream.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts,
            std::optional<uint64_t> Offset) const;

  /// Parse the section from raw data. \p Data is assumed to contain the whole
  /// frame section contents to be parsed.
  Error parse(DWARFDataExtractor Data);

  /// Return whether the section has any entries.
  bool empty() const { return Entries.empty(); }

  /// DWARF Frame entries accessors
  iterator begin() const { return Entries.begin(); }
  iterator end() const { return Entries.end(); }
  iterator_range<iterator> entries() const {
    return iterator_range<iterator>(Entries.begin(), Entries.end());
  }

  uint64_t getEHFrameAddress() const { return EHFrameAddress; }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H
