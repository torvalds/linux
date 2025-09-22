//===- Target.td - Target Independent TableGen interface ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent interfaces which should be
// implemented by each target which is using a TableGen based code generator.
//
//===----------------------------------------------------------------------===//

// Include all information about LLVM intrinsics.
include "llvm/IR/Intrinsics.td"

class Predicate; // Forward def

//===----------------------------------------------------------------------===//
// Register file description - These classes are used to fill in the target
// description classes.

class HwMode<string FS, list<Predicate> Ps> {
  // A string representing subtarget features that turn on this HW mode.
  // For example, "+feat1,-feat2" will indicate that the mode is active
  // when "feat1" is enabled and "feat2" is disabled at the same time.
  // Any other features are not checked.
  // When multiple modes are used, they should be mutually exclusive,
  // otherwise the results are unpredictable.
  string Features = FS;

  // A list of predicates that turn on this HW mode.
  list<Predicate> Predicates = Ps;
}

// A special mode recognized by tablegen. This mode is considered active
// when no other mode is active. For targets that do not use specific hw
// modes, this is the only mode.
def DefaultMode : HwMode<"", []>;

// A class used to associate objects with HW modes. It is only intended to
// be used as a base class, where the derived class should contain a member
// "Objects", which is a list of the same length as the list of modes.
// The n-th element on the Objects list will be associated with the n-th
// element on the Modes list.
class HwModeSelect<list<HwMode> Ms> {
  list<HwMode> Modes = Ms;
}

// A common class that implements a counterpart of ValueType, which is
// dependent on a HW mode. This class inherits from ValueType itself,
// which makes it possible to use objects of this class where ValueType
// objects could be used. This is specifically applicable to selection
// patterns.
class ValueTypeByHwMode<list<HwMode> Ms, list<ValueType> Ts>
    : HwModeSelect<Ms>, ValueType<0, 0> {
  // The length of this list must be the same as the length of Ms.
  list<ValueType> Objects = Ts;
}

// A class that implements a counterpart of PtrValueType, which is
// dependent on a HW mode. This class inherits from PtrValueType itself,
// which makes it possible to use objects of this class where PtrValueType
// or ValueType could be used. This is specifically applicable to selection
// patterns.
class PtrValueTypeByHwMode<ValueTypeByHwMode scalar, int addrspace>
    : HwModeSelect<scalar.Modes>, PtrValueType<ValueType<0, 0>, addrspace> {
  list<ValueType> Objects = scalar.Objects;
}

// A class representing the register size, spill size and spill alignment
// in bits of a register.
class RegInfo<int RS, int SS, int SA> {
  int RegSize = RS;         // Register size in bits.
  int SpillSize = SS;       // Spill slot size in bits.
  int SpillAlignment = SA;  // Spill slot alignment in bits.
}

// The register size/alignment information, parameterized by a HW mode.
class RegInfoByHwMode<list<HwMode> Ms = [], list<RegInfo> Ts = []>
    : HwModeSelect<Ms> {
  // The length of this list must be the same as the length of Ms.
  list<RegInfo> Objects = Ts;
}

class SubRegRange<int size, int offset = 0> {
  int Size = size;      // Sub register size in bits.
  int Offset = offset;  // Offset of the first bit of the sub-reg index.
}

class SubRegRangeByHwMode<list<HwMode> Ms = [], list<SubRegRange> Ts = []>
    : HwModeSelect<Ms> {
  // The length of this list must be the same as the length of Ms.
  list<SubRegRange> Objects = Ts;
}

// SubRegIndex - Use instances of SubRegIndex to identify subregisters.
class SubRegIndex<int size, int offset = 0> {
  string Namespace = "";

  // The size/offset information, parameterized by a HW mode.
  // If the HwModes provided for SubRegRanges does not include the DefaultMode,
  // the/ Size and Offset fields below will be used for the default. Otherwise,
  // the Size and Offset fields are ignored.
  SubRegRangeByHwMode SubRegRanges;

  // Size - Size (in bits) of the sub-registers represented by this index.
  int Size = size;

  // Offset - Offset of the first bit that is part of this sub-register index.
  // Set it to -1 if the same index is used to represent sub-registers that can
  // be at different offsets (for example when using an index to access an
  // element in a register tuple).
  int Offset = offset;

  // ComposedOf - A list of two SubRegIndex instances, [A, B].
  // This indicates that this SubRegIndex is the result of composing A and B.
  // See ComposedSubRegIndex.
  list<SubRegIndex> ComposedOf = [];

  // CoveringSubRegIndices - A list of two or more sub-register indexes that
  // cover this sub-register.
  //
  // This field should normally be left blank as TableGen can infer it.
  //
  // TableGen automatically detects sub-registers that straddle the registers
  // in the SubRegs field of a Register definition. For example:
  //
  //   Q0    = dsub_0 -> D0, dsub_1 -> D1
  //   Q1    = dsub_0 -> D2, dsub_1 -> D3
  //   D1_D2 = dsub_0 -> D1, dsub_1 -> D2
  //   QQ0   = qsub_0 -> Q0, qsub_1 -> Q1
  //
  // TableGen will infer that D1_D2 is a sub-register of QQ0. It will be given
  // the synthetic index dsub_1_dsub_2 unless some SubRegIndex is defined with
  // CoveringSubRegIndices = [dsub_1, dsub_2].
  list<SubRegIndex> CoveringSubRegIndices = [];
}

// ComposedSubRegIndex - A sub-register that is the result of composing A and B.
// Offset is set to the sum of A and B's Offsets. Size is set to B's Size.
class ComposedSubRegIndex<SubRegIndex A, SubRegIndex B>
  : SubRegIndex<B.Size, !cond(!eq(A.Offset, -1): -1,
                              !eq(B.Offset, -1): -1,
                              true:              !add(A.Offset, B.Offset))> {
  // See SubRegIndex.
  let ComposedOf = [A, B];
}

// RegAltNameIndex - The alternate name set to use for register operands of
// this register class when printing.
class RegAltNameIndex {
  string Namespace = "";

  // A set to be used if the name for a register is not defined in this set.
  // This allows creating name sets with only a few alternative names.
  RegAltNameIndex FallbackRegAltNameIndex = ?;
}
def NoRegAltName : RegAltNameIndex;

// Register - You should define one instance of this class for each register
// in the target machine.  String n will become the "name" of the register.
class Register<string n, list<string> altNames = []> {
  string Namespace = "";
  string AsmName = n;
  list<string> AltNames = altNames;

  // Aliases - A list of registers that this register overlaps with.  A read or
  // modification of this register can potentially read or modify the aliased
  // registers.
  list<Register> Aliases = [];

  // SubRegs - A list of registers that are parts of this register. Note these
  // are "immediate" sub-registers and the registers within the list do not
  // themselves overlap. e.g. For X86, EAX's SubRegs list contains only [AX],
  // not [AX, AH, AL].
  list<Register> SubRegs = [];

  // SubRegIndices - For each register in SubRegs, specify the SubRegIndex used
  // to address it. Sub-sub-register indices are automatically inherited from
  // SubRegs.
  list<SubRegIndex> SubRegIndices = [];

  // RegAltNameIndices - The alternate name indices which are valid for this
  // register.
  list<RegAltNameIndex> RegAltNameIndices = [];

  // DwarfNumbers - Numbers used internally by gcc/gdb to identify the register.
  // These values can be determined by locating the <target>.h file in the
  // directory llvmgcc/gcc/config/<target>/ and looking for REGISTER_NAMES.  The
  // order of these names correspond to the enumeration used by gcc.  A value of
  // -1 indicates that the gcc number is undefined and -2 that register number
  // is invalid for this mode/flavour.
  list<int> DwarfNumbers = [];

  // CostPerUse - Additional cost of instructions using this register compared
  // to other registers in its class. The register allocator will try to
  // minimize the number of instructions using a register with a CostPerUse.
  // This is used by the ARC target, by the ARM Thumb and x86-64 targets, where
  // some registers require larger instruction encodings, by the RISC-V target,
  // where some registers preclude using some C instructions. By making it a
  // list, targets can have multiple cost models associated with each register
  // and can choose one specific cost model per Machine Function by overriding
  // TargetRegisterInfo::getRegisterCostTableIndex. Every target register will
  // finally have an equal number of cost values which is the max of costPerUse
  // values specified. Any mismatch in the cost values for a register will be
  // filled with zeros. Restricted the cost type to uint8_t in the
  // generated table. It will considerably reduce the table size.
  list<int> CostPerUse = [0];

  // CoveredBySubRegs - When this bit is set, the value of this register is
  // completely determined by the value of its sub-registers.  For example, the
  // x86 register AX is covered by its sub-registers AL and AH, but EAX is not
  // covered by its sub-register AX.
  bit CoveredBySubRegs = false;

  // HWEncoding - The target specific hardware encoding for this register.
  bits<16> HWEncoding = 0;

  bit isArtificial = false;

  // isConstant - This register always holds a constant value (e.g. the zero
  // register in architectures such as MIPS)
  bit isConstant = false;

  /// PositionOrder - Indicate tablegen to place the newly added register at a later
  /// position to avoid iterations on them on unsupported target.
  int PositionOrder = 0;
}

// RegisterWithSubRegs - This can be used to define instances of Register which
// need to specify sub-registers.
// List "subregs" specifies which registers are sub-registers to this one. This
// is used to populate the SubRegs and AliasSet fields of TargetRegisterDesc.
// This allows the code generator to be careful not to put two values with
// overlapping live ranges into registers which alias.
class RegisterWithSubRegs<string n, list<Register> subregs> : Register<n> {
  let SubRegs = subregs;
}

// DAGOperand - An empty base class that unifies RegisterClass's and other forms
// of Operand's that are legal as type qualifiers in DAG patterns.  This should
// only ever be used for defining multiclasses that are polymorphic over both
// RegisterClass's and other Operand's.
class DAGOperand {
  string OperandNamespace = "MCOI";
  string DecoderMethod = "";
}

// RegisterClass - Now that all of the registers are defined, and aliases
// between registers are defined, specify which registers belong to which
// register classes.  This also defines the default allocation order of
// registers by register allocators.
//
class RegisterClass<string namespace, list<ValueType> regTypes, int alignment,
                    dag regList, RegAltNameIndex idx = NoRegAltName>
  : DAGOperand {
  string Namespace = namespace;

  // The register size/alignment information, parameterized by a HW mode.
  RegInfoByHwMode RegInfos;

  // RegType - Specify the list ValueType of the registers in this register
  // class.  Note that all registers in a register class must have the same
  // ValueTypes.  This is a list because some targets permit storing different
  // types in same register, for example vector values with 128-bit total size,
  // but different count/size of items, like SSE on x86.
  //
  list<ValueType> RegTypes = regTypes;

  // Size - Specify the spill size in bits of the registers.  A default value of
  // zero lets tablegen pick an appropriate size.
  int Size = 0;

  // Alignment - Specify the alignment required of the registers when they are
  // stored or loaded to memory.
  //
  int Alignment = alignment;

  // CopyCost - This value is used to specify the cost of copying a value
  // between two registers in this register class. The default value is one
  // meaning it takes a single instruction to perform the copying. A negative
  // value means copying is extremely expensive or impossible.
  int CopyCost = 1;

  // MemberList - Specify which registers are in this class.  If the
  // allocation_order_* method are not specified, this also defines the order of
  // allocation used by the register allocator.
  //
  dag MemberList = regList;

  // AltNameIndex - The alternate register name to use when printing operands
  // of this register class. Every register in the register class must have
  // a valid alternate name for the given index.
  RegAltNameIndex altNameIndex = idx;

  // isAllocatable - Specify that the register class can be used for virtual
  // registers and register allocation.  Some register classes are only used to
  // model instruction operand constraints, and should have isAllocatable = 0.
  bit isAllocatable = true;

  // AltOrders - List of alternative allocation orders. The default order is
  // MemberList itself, and that is good enough for most targets since the
  // register allocators automatically remove reserved registers and move
  // callee-saved registers to the end.
  list<dag> AltOrders = [];

  // AltOrderSelect - The body of a function that selects the allocation order
  // to use in a given machine function. The code will be inserted in a
  // function like this:
  //
  //   static inline unsigned f(const MachineFunction &MF) { ... }
  //
  // The function should return 0 to select the default order defined by
  // MemberList, 1 to select the first AltOrders entry and so on.
  code AltOrderSelect = [{}];

  // Specify allocation priority for register allocators using a greedy
  // heuristic. Classes with higher priority values are assigned first. This is
  // useful as it is sometimes beneficial to assign registers to highly
  // constrained classes first. The value has to be in the range [0,31].
  int AllocationPriority = 0;

  // Force register class to use greedy's global heuristic for all
  // registers in this class. This should more aggressively try to
  // avoid spilling in pathological cases.
  bit GlobalPriority = false;

  // Generate register pressure set for this register class and any class
  // synthesized from it. Set to 0 to inhibit unneeded pressure sets.
  bit GeneratePressureSet = true;

  // Weight override for register pressure calculation. This is the value
  // TargetRegisterClass::getRegClassWeight() will return. The weight is in
  // units of pressure for this register class. If unset tablegen will
  // calculate a weight based on a number of register units in this register
  // class registers. The weight is per register.
  int Weight = ?;

  // The diagnostic type to present when referencing this operand in a match
  // failure error message. If this is empty, the default Match_InvalidOperand
  // diagnostic type will be used. If this is "<name>", a Match_<name> enum
  // value will be generated and used for this operand type. The target
  // assembly parser is responsible for converting this into a user-facing
  // diagnostic message.
  string DiagnosticType = "";

  // A diagnostic message to emit when an invalid value is provided for this
  // register class when it is being used as an assembly operand. If this is
  // non-empty, an anonymous diagnostic type enum value will be generated, and
  // the assembly matcher will provide a function to map from diagnostic types
  // to message strings.
  string DiagnosticString = "";

  // Target-specific flags. This becomes the TSFlags field in TargetRegisterClass.
  bits<8> TSFlags = 0;

  // If set then consider this register class to be the base class for registers in
  // its MemberList.  The base class for registers present in multiple base register
  // classes will be resolved in the order defined by this value, with lower values
  // taking precedence over higher ones.  Ties are resolved by enumeration order.
  int BaseClassOrder = ?;
}

// The memberList in a RegisterClass is a dag of set operations. TableGen
// evaluates these set operations and expand them into register lists. These
// are the most common operation, see test/TableGen/SetTheory.td for more
// examples of what is possible:
//
// (add R0, R1, R2) - Set Union. Each argument can be an individual register, a
// register class, or a sub-expression. This is also the way to simply list
// registers.
//
// (sub GPR, SP) - Set difference. Subtract the last arguments from the first.
//
// (and GPR, CSR) - Set intersection. All registers from the first set that are
// also in the second set.
//
// (sequence "R%u", 0, 15) -> [R0, R1, ..., R15]. Generate a sequence of
// numbered registers.  Takes an optional 4th operand which is a stride to use
// when generating the sequence.
//
// (shl GPR, 4) - Remove the first N elements.
//
// (trunc GPR, 4) - Truncate after the first N elements.
//
// (rotl GPR, 1) - Rotate N places to the left.
//
// (rotr GPR, 1) - Rotate N places to the right.
//
// (decimate GPR, 2) - Pick every N'th element, starting with the first.
//
// (interleave A, B, ...) - Interleave the elements from each argument list.
//
// All of these operators work on ordered sets, not lists. That means
// duplicates are removed from sub-expressions.

// Set operators. The rest is defined in TargetSelectionDAG.td.
def sequence;
def decimate;
def interleave;

// RegisterTuples - Automatically generate super-registers by forming tuples of
// sub-registers. This is useful for modeling register sequence constraints
// with pseudo-registers that are larger than the architectural registers.
//
// The sub-register lists are zipped together:
//
//   def EvenOdd : RegisterTuples<[sube, subo], [(add R0, R2), (add R1, R3)]>;
//
// Generates the same registers as:
//
//   let SubRegIndices = [sube, subo] in {
//     def R0_R1 : RegisterWithSubRegs<"", [R0, R1]>;
//     def R2_R3 : RegisterWithSubRegs<"", [R2, R3]>;
//   }
//
// The generated pseudo-registers inherit super-classes and fields from their
// first sub-register. Most fields from the Register class are inferred, and
// the AsmName and Dwarf numbers are cleared.
//
// RegisterTuples instances can be used in other set operations to form
// register classes and so on. This is the only way of using the generated
// registers.
//
// RegNames may be specified to supply asm names for the generated tuples.
// If used must have the same size as the list of produced registers.
class RegisterTuples<list<SubRegIndex> Indices, list<dag> Regs,
                     list<string> RegNames = []> {
  // SubRegs - N lists of registers to be zipped up. Super-registers are
  // synthesized from the first element of each SubRegs list, the second
  // element and so on.
  list<dag> SubRegs = Regs;

  // SubRegIndices - N SubRegIndex instances. This provides the names of the
  // sub-registers in the synthesized super-registers.
  list<SubRegIndex> SubRegIndices = Indices;

  // List of asm names for the generated tuple registers.
  list<string> RegAsmNames = RegNames;

  // PositionOrder - Indicate tablegen to place the newly added register at a later
  // position to avoid iterations on them on unsupported target.
  int PositionOrder = 0;
}

// RegisterCategory - This class is a list of RegisterClasses that belong to a
// general cateogry --- e.g. "general purpose" or "fixed" registers. This is
// useful for identifying registers in a generic way instead of having
// information about a specific target's registers.
class RegisterCategory<list<RegisterClass> classes> {
  // Classes - A list of register classes that fall within the category.
  list<RegisterClass> Classes = classes;
}

//===----------------------------------------------------------------------===//
// DwarfRegNum - This class provides a mapping of the llvm register enumeration
// to the register numbering used by gcc and gdb.  These values are used by a
// debug information writer to describe where values may be located during
// execution.
class DwarfRegNum<list<int> Numbers> {
  // DwarfNumbers - Numbers used internally by gcc/gdb to identify the register.
  // These values can be determined by locating the <target>.h file in the
  // directory llvmgcc/gcc/config/<target>/ and looking for REGISTER_NAMES.  The
  // order of these names correspond to the enumeration used by gcc.  A value of
  // -1 indicates that the gcc number is undefined and -2 that register number
  // is invalid for this mode/flavour.
  list<int> DwarfNumbers = Numbers;
}

// DwarfRegAlias - This class declares that a given register uses the same dwarf
// numbers as another one. This is useful for making it clear that the two
// registers do have the same number. It also lets us build a mapping
// from dwarf register number to llvm register.
class DwarfRegAlias<Register reg> {
      Register DwarfAlias = reg;
}

//===----------------------------------------------------------------------===//
// SubtargetFeature - A characteristic of the chip set.
//
class SubtargetFeature<string n, string f, string v, string d,
                       list<SubtargetFeature> i = []> {
  // Name - Feature name.  Used by command line (-mattr=) to determine the
  // appropriate target chip.
  //
  string Name = n;

  // FieldName - Field in XXXSubtarget to be set by feature.
  //
  string FieldName = f;

  // Value - Value the XXXSubtarget field to be set to by feature.
  //
  // A value of "true" or "false" implies the field is a bool. Otherwise,
  // it is assumed to be an integer. the integer value may be the name of an
  // enum constant. If multiple features use the same integer field, the
  // field will be set to the maximum value of all enabled features that
  // share the field.
  //
  string Value = v;

  // Desc - Feature description.  Used by command line (-mattr=) to display help
  // information.
  //
  string Desc = d;

  // Implies - Features that this feature implies are present. If one of those
  // features isn't set, then this one shouldn't be set either.
  //
  list<SubtargetFeature> Implies = i;
}

/// Specifies a Subtarget feature that this instruction is deprecated on.
class Deprecated<SubtargetFeature dep> {
  SubtargetFeature DeprecatedFeatureMask = dep;
}

/// A custom predicate used to determine if an instruction is
/// deprecated or not.
class ComplexDeprecationPredicate<string dep> {
  string ComplexDeprecationPredicate = dep;
}

//===----------------------------------------------------------------------===//
// Pull in the common support for MCPredicate (portable scheduling predicates).
//
include "llvm/Target/TargetInstrPredicate.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for scheduling
//
include "llvm/Target/TargetSchedule.td"

class InstructionEncoding {
  // Size of encoded instruction.
  int Size;

  // The "namespace" in which this instruction exists, on targets like ARM
  // which multiple ISA namespaces exist.
  string DecoderNamespace = "";

  // List of predicates which will be turned into isel matching code.
  list<Predicate> Predicates = [];

  string DecoderMethod = "";

  // Is the instruction decoder method able to completely determine if the
  // given instruction is valid or not. If the TableGen definition of the
  // instruction specifies bitpattern A??B where A and B are static bits, the
  // hasCompleteDecoder flag says whether the decoder method fully handles the
  // ?? space, i.e. if it is a final arbiter for the instruction validity.
  // If not then the decoder attempts to continue decoding when the decoder
  // method fails.
  //
  // This allows to handle situations where the encoding is not fully
  // orthogonal. Example:
  // * InstA with bitpattern 0b0000????,
  // * InstB with bitpattern 0b000000?? but the associated decoder method
  //   DecodeInstB() returns Fail when ?? is 0b00 or 0b11.
  //
  // The decoder tries to decode a bitpattern that matches both InstA and
  // InstB bitpatterns first as InstB (because it is the most specific
  // encoding). In the default case (hasCompleteDecoder = 1), when
  // DecodeInstB() returns Fail the bitpattern gets rejected. By setting
  // hasCompleteDecoder = 0 in InstB, the decoder is informed that
  // DecodeInstB() is not able to determine if all possible values of ?? are
  // valid or not. If DecodeInstB() returns Fail the decoder will attempt to
  // decode the bitpattern as InstA too.
  bit hasCompleteDecoder = true;
}

// Allows specifying an InstructionEncoding by HwMode. If an Instruction specifies
// an EncodingByHwMode, its Inst and Size members are ignored and Ts are used
// to encode and decode based on HwMode.
class EncodingByHwMode<list<HwMode> Ms = [], list<InstructionEncoding> Ts = []>
    : HwModeSelect<Ms> {
  // The length of this list must be the same as the length of Ms.
  list<InstructionEncoding> Objects = Ts;
}

//===----------------------------------------------------------------------===//
// Instruction set description - These classes correspond to the C++ classes in
// the Target/TargetInstrInfo.h file.
//
class Instruction : InstructionEncoding {
  string Namespace = "";

  dag OutOperandList;       // An dag containing the MI def operand list.
  dag InOperandList;        // An dag containing the MI use operand list.
  string AsmString = "";    // The .s format to print the instruction with.

  // Allows specifying a canonical InstructionEncoding by HwMode. If non-empty,
  // the Inst member of this Instruction is ignored.
  EncodingByHwMode EncodingInfos;

  // Pattern - Set to the DAG pattern for this instruction, if we know of one,
  // otherwise, uninitialized.
  list<dag> Pattern;

  // The follow state will eventually be inferred automatically from the
  // instruction pattern.

  list<Register> Uses = []; // Default to using no non-operand registers
  list<Register> Defs = []; // Default to modifying no non-operand registers

  // Predicates - List of predicates which will be turned into isel matching
  // code.
  list<Predicate> Predicates = [];

  // Size - Size of encoded instruction, or zero if the size cannot be determined
  // from the opcode.
  int Size = 0;

  // Code size, for instruction selection.
  // FIXME: What does this actually mean?
  int CodeSize = 0;

  // Added complexity passed onto matching pattern.
  int AddedComplexity  = 0;

  // Indicates if this is a pre-isel opcode that should be
  // legalized/regbankselected/selected.
  bit isPreISelOpcode = false;

  // These bits capture information about the high-level semantics of the
  // instruction.
  bit isReturn     = false;     // Is this instruction a return instruction?
  bit isBranch     = false;     // Is this instruction a branch instruction?
  bit isEHScopeReturn = false;  // Does this instruction end an EH scope?
  bit isIndirectBranch = false; // Is this instruction an indirect branch?
  bit isCompare    = false;     // Is this instruction a comparison instruction?
  bit isMoveImm    = false;     // Is this instruction a move immediate instruction?
  bit isMoveReg    = false;     // Is this instruction a move register instruction?
  bit isBitcast    = false;     // Is this instruction a bitcast instruction?
  bit isSelect     = false;     // Is this instruction a select instruction?
  bit isBarrier    = false;     // Can control flow fall through this instruction?
  bit isCall       = false;     // Is this instruction a call instruction?
  bit isAdd        = false;     // Is this instruction an add instruction?
  bit isTrap       = false;     // Is this instruction a trap instruction?
  bit canFoldAsLoad = false;    // Can this be folded as a simple memory operand?
  bit mayLoad      = ?;         // Is it possible for this inst to read memory?
  bit mayStore     = ?;         // Is it possible for this inst to write memory?
  bit mayRaiseFPException = false; // Can this raise a floating-point exception?
  bit isConvertibleToThreeAddress = false;  // Can this 2-addr instruction promote?
  bit isCommutable = false;     // Is this 3 operand instruction commutable?
  bit isTerminator = false;     // Is this part of the terminator for a basic block?
  bit isReMaterializable = false; // Is this instruction re-materializable?
  bit isPredicable = false;     // 1 means this instruction is predicable
                                // even if it does not have any operand
                                // tablegen can identify as a predicate
  bit isUnpredicable = false;   // 1 means this instruction is not predicable
                                // even if it _does_ have a predicate operand
  bit hasDelaySlot = false;     // Does this instruction have an delay slot?
  bit usesCustomInserter = false; // Pseudo instr needing special help.
  bit hasPostISelHook = false;  // To be *adjusted* after isel by target hook.
  bit hasCtrlDep   = false;     // Does this instruction r/w ctrl-flow chains?
  bit isNotDuplicable = false;  // Is it unsafe to duplicate this instruction?
  bit isConvergent = false;     // Is this instruction convergent?
  bit isAuthenticated = false;  // Does this instruction authenticate a pointer?
  bit isAsCheapAsAMove = false; // As cheap (or cheaper) than a move instruction.
  bit hasExtraSrcRegAllocReq = false; // Sources have special regalloc requirement?
  bit hasExtraDefRegAllocReq = false; // Defs have special regalloc requirement?
  bit isRegSequence = false;    // Is this instruction a kind of reg sequence?
                                // If so, make sure to override
                                // TargetInstrInfo::getRegSequenceLikeInputs.
  bit isPseudo     = false;     // Is this instruction a pseudo-instruction?
                                // If so, won't have encoding information for
                                // the [MC]CodeEmitter stuff.
  bit isMeta = false;           // Is this instruction a meta-instruction?
                                // If so, won't produce any output in the form of
                                // executable instructions
  bit isExtractSubreg = false;  // Is this instruction a kind of extract subreg?
                                // If so, make sure to override
                                // TargetInstrInfo::getExtractSubregLikeInputs.
  bit isInsertSubreg = false;   // Is this instruction a kind of insert subreg?
                                // If so, make sure to override
                                // TargetInstrInfo::getInsertSubregLikeInputs.
  bit variadicOpsAreDefs = false; // Are variadic operands definitions?

  // Does the instruction have side effects that are not captured by any
  // operands of the instruction or other flags?
  bit hasSideEffects = ?;

  // Is this instruction a "real" instruction (with a distinct machine
  // encoding), or is it a pseudo instruction used for codegen modeling
  // purposes.
  // FIXME: For now this is distinct from isPseudo, above, as code-gen-only
  // instructions can (and often do) still have encoding information
  // associated with them. Once we've migrated all of them over to true
  // pseudo-instructions that are lowered to real instructions prior to
  // the printer/emitter, we can remove this attribute and just use isPseudo.
  //
  // The intended use is:
  // isPseudo: Does not have encoding information and should be expanded,
  //   at the latest, during lowering to MCInst.
  //
  // isCodeGenOnly: Does have encoding information and can go through to the
  //   CodeEmitter unchanged, but duplicates a canonical instruction
  //   definition's encoding and should be ignored when constructing the
  //   assembler match tables.
  bit isCodeGenOnly = false;

  // Is this instruction a pseudo instruction for use by the assembler parser.
  bit isAsmParserOnly = false;

  // This instruction is not expected to be queried for scheduling latencies
  // and therefore needs no scheduling information even for a complete
  // scheduling model.
  bit hasNoSchedulingInfo = false;

  InstrItinClass Itinerary = NoItinerary;// Execution steps used for scheduling.

  // Scheduling information from TargetSchedule.td.
  list<SchedReadWrite> SchedRW;

  /// Support for operand constraints. There are currently two kinds:
  /// "$src = $dst"
  ///   Ensures that the operands are allocated to the same register.
  ///
  /// "@earlyclobber $rd"
  ///   Ensures that LLVM will not use the same register for any inputs (other
  ///   than an input tied to this output).
  ///
  /// See also:
  /// - MC/MCInstrDesc.h:OperandConstraint::{TIED_TO, EARLY_CLOBBER}.
  /// - CodeGen/MachineOperand.h:MachineOperand::{TiedTo, IsEarlyClobber}.
  /// - The LLVM IR specification: Section `Output constraints` in the
  ///   discussion of inline assembly constraint strings.
  string Constraints = "";

  /// DisableEncoding - List of operand names (e.g. "$op1,$op2") that should not
  /// be encoded into the output machineinstr.
  string DisableEncoding = "";

  string PostEncoderMethod = "";

  /// Target-specific flags. This becomes the TSFlags field in TargetInstrDesc.
  bits<64> TSFlags = 0;

  ///@name Assembler Parser Support
  ///@{

  string AsmMatchConverter = "";

  /// TwoOperandAliasConstraint - Enable TableGen to auto-generate a
  /// two-operand matcher inst-alias for a three operand instruction.
  /// For example, the arm instruction "add r3, r3, r5" can be written
  /// as "add r3, r5". The constraint is of the same form as a tied-operand
  /// constraint. For example, "$Rn = $Rd".
  string TwoOperandAliasConstraint = "";

  /// Assembler variant name to use for this instruction. If specified then
  /// instruction will be presented only in MatchTable for this variant. If
  /// not specified then assembler variants will be determined based on
  /// AsmString
  string AsmVariantName = "";

  ///@}

  /// UseNamedOperandTable - If set, the operand indices of this instruction
  /// can be queried via the getNamedOperandIdx() function which is generated
  /// by TableGen.
  bit UseNamedOperandTable = false;

  /// Should generate helper functions that help you to map a logical operand's
  /// index to the underlying MIOperand's index.
  /// In most architectures logical operand indices are equal to
  /// MIOperand indices, but for some CISC architectures, a logical operand
  /// might be consist of multiple MIOperand (e.g. a logical operand that
  /// uses complex address mode).
  bit UseLogicalOperandMappings = false;

  /// Should FastISel ignore this instruction. For certain ISAs, they have
  /// instructions which map to the same ISD Opcode, value type operands and
  /// instruction selection predicates. FastISel cannot handle such cases, but
  /// SelectionDAG can.
  bit FastISelShouldIgnore = false;

  /// HasPositionOrder: Indicate tablegen to sort the instructions by record
  /// ID, so that instruction that is defined earlier can be sorted earlier
  /// in the assembly matching table.
  bit HasPositionOrder = false;
}

/// Defines a Pat match between compressed and uncompressed instruction.
/// The relationship and helper function generation are handled by
/// CompressInstEmitter backend.
class CompressPat<dag input, dag output, list<Predicate> predicates = []> {
  /// Uncompressed instruction description.
  dag Input = input;
  /// Compressed instruction description.
  dag Output = output;
  /// Predicates that must be true for this to match.
  list<Predicate> Predicates = predicates;
  /// Duplicate match when tied operand is just different.
  bit isCompressOnly = false;
}

/// Defines an additional encoding that disassembles to the given instruction
/// Like Instruction, the Inst and SoftFail fields are omitted to allow targets
// to specify their size.
class AdditionalEncoding<Instruction I> : InstructionEncoding {
  Instruction AliasOf = I;
}

/// PseudoInstExpansion - Expansion information for a pseudo-instruction.
/// Which instruction it expands to and how the operands map from the
/// pseudo.
class PseudoInstExpansion<dag Result> {
  dag ResultInst = Result;     // The instruction to generate.
  bit isPseudo = true;
}

/// Predicates - These are extra conditionals which are turned into instruction
/// selector matching code. Currently each predicate is just a string.
class Predicate<string cond> {
  string CondString = cond;

  /// AssemblerMatcherPredicate - If this feature can be used by the assembler
  /// matcher, this is true.  Targets should set this by inheriting their
  /// feature from the AssemblerPredicate class in addition to Predicate.
  bit AssemblerMatcherPredicate = false;

  /// AssemblerCondDag - Set of subtarget features being tested used
  /// as alternative condition string used for assembler matcher. Must be used
  /// with (all_of) to indicate that all features must be present, or (any_of)
  /// to indicate that at least one must be. The required lack of presence of
  /// a feature can be tested using a (not) node including the feature.
  /// e.g. "(all_of ModeThumb)" is translated to "(Bits & ModeThumb) != 0".
  ///      "(all_of (not ModeThumb))" is translated to
  ///      "(Bits & ModeThumb) == 0".
  ///      "(all_of ModeThumb, FeatureThumb2)" is translated to
  ///      "(Bits & ModeThumb) != 0 && (Bits & FeatureThumb2) != 0".
  ///      "(any_of ModeTumb, FeatureThumb2)" is translated to
  ///      "(Bits & ModeThumb) != 0 || (Bits & FeatureThumb2) != 0".
  /// all_of and any_of cannot be combined in a single dag, instead multiple
  /// predicates can be placed onto Instruction definitions.
  dag AssemblerCondDag;

  /// PredicateName - User-level name to use for the predicate. Mainly for use
  /// in diagnostics such as missing feature errors in the asm matcher.
  string PredicateName = "";

  /// Setting this to '1' indicates that the predicate must be recomputed on
  /// every function change. Most predicates can leave this at '0'.
  ///
  /// Ignored by SelectionDAG, it always recomputes the predicate on every use.
  bit RecomputePerFunction = false;
}

/// NoHonorSignDependentRounding - This predicate is true if support for
/// sign-dependent-rounding is not enabled.
def NoHonorSignDependentRounding
 : Predicate<"!TM.Options.HonorSignDependentRoundingFPMath()">;

class Requires<list<Predicate> preds> {
  list<Predicate> Predicates = preds;
}

/// ops definition - This is just a simple marker used to identify the operand
/// list for an instruction. outs and ins are identical both syntactically and
/// semantically; they are used to define def operands and use operands to
/// improve readability. This should be used like this:
///     (outs R32:$dst), (ins R32:$src1, R32:$src2) or something similar.
def ops;
def outs;
def ins;

/// variable_ops definition - Mark this instruction as taking a variable number
/// of operands.
def variable_ops;

/// variable-length instruction encoding utilities.
/// The `ascend` operator should be used like this:
///     (ascend 0b0010, 0b1101)
/// Which represent a seqence of encoding fragments placing from LSB to MSB.
/// Thus, in this case the final encoding will be 0b1101_0010.
/// The arguments for `ascend` can either be `bits` or another DAG.
def ascend;
/// In addition, we can use `descend` to describe an encoding that places
/// its arguments (i.e. encoding fragments) from MSB to LSB. For instance:
///     (descend 0b0010, 0b1101)
/// This results in an encoding of 0b0010_1101.
def descend;
/// The `operand` operator should be used like this:
///     (operand "$src", 4)
/// Which represents a 4-bit encoding for an instruction operand named `$src`.
def operand;
/// Similar to `operand`, we can reference only part of the operand's encoding:
///     (slice "$src", 6, 8)
///     (slice "$src", 8, 6)
/// Both DAG represent bit 6 to 8 (total of 3 bits) in the encoding of operand
/// `$src`.
def slice;
/// You can use `encoder` or `decoder` to specify a custom encoder or decoder
/// function for a specific `operand` or `slice` directive. For example:
///     (operand "$src", 4, (encoder "encodeMyImm"))
///     (slice "$src", 8, 6, (encoder "encodeMyReg"))
///     (operand "$src", 4, (encoder "encodeMyImm"), (decoder "decodeMyImm"))
/// The ordering of `encoder` and `decoder` in the same `operand` or `slice`
/// doesn't matter.
/// Note that currently we cannot assign different decoders in the same
/// (instruction) operand.
def encoder;
def decoder;

/// PointerLikeRegClass - Values that are designed to have pointer width are
/// derived from this.  TableGen treats the register class as having a symbolic
/// type that it doesn't know, and resolves the actual regclass to use by using
/// the TargetRegisterInfo::getPointerRegClass() hook at codegen time.
class PointerLikeRegClass<int Kind> {
  int RegClassKind = Kind;
}


/// ptr_rc definition - Mark this operand as being a pointer value whose
/// register class is resolved dynamically via a callback to TargetInstrInfo.
/// FIXME: We should probably change this to a class which contain a list of
/// flags. But currently we have but one flag.
def ptr_rc : PointerLikeRegClass<0>;

/// unknown definition - Mark this operand as being of unknown type, causing
/// it to be resolved by inference in the context it is used.
class unknown_class;
def unknown : unknown_class;

/// AsmOperandClass - Representation for the kinds of operands which the target
/// specific parser can create and the assembly matcher may need to distinguish.
///
/// Operand classes are used to define the order in which instructions are
/// matched, to ensure that the instruction which gets matched for any
/// particular list of operands is deterministic.
///
/// The target specific parser must be able to classify a parsed operand into a
/// unique class which does not partially overlap with any other classes. It can
/// match a subset of some other class, in which case the super class field
/// should be defined.
class AsmOperandClass {
  /// The name to use for this class, which should be usable as an enum value.
  string Name = ?;

  /// The super classes of this operand.
  list<AsmOperandClass> SuperClasses = [];

  /// The name of the method on the target specific operand to call to test
  /// whether the operand is an instance of this class. If not set, this will
  /// default to "isFoo", where Foo is the AsmOperandClass name. The method
  /// signature should be:
  ///   bool isFoo() const;
  string PredicateMethod = ?;

  /// The name of the method on the target specific operand to call to add the
  /// target specific operand to an MCInst. If not set, this will default to
  /// "addFooOperands", where Foo is the AsmOperandClass name. The method
  /// signature should be:
  ///   void addFooOperands(MCInst &Inst, unsigned N) const;
  string RenderMethod = ?;

  /// The name of the method on the target specific operand to call to custom
  /// handle the operand parsing. This is useful when the operands do not relate
  /// to immediates or registers and are very instruction specific (as flags to
  /// set in a processor register, coprocessor number, ...).
  string ParserMethod = ?;

  // The diagnostic type to present when referencing this operand in a
  // match failure error message. By default, use a generic "invalid operand"
  // diagnostic. The target AsmParser maps these codes to text.
  string DiagnosticType = "";

  /// A diagnostic message to emit when an invalid value is provided for this
  /// operand.
  string DiagnosticString = "";

  /// Set to 1 if this operand is optional and not always required. Typically,
  /// the AsmParser will emit an error when it finishes parsing an
  /// instruction if it hasn't matched all the operands yet.  However, this
  /// error will be suppressed if all of the remaining unmatched operands are
  /// marked as IsOptional.
  bit IsOptional = false;

  /// The name of the method on the target specific asm parser that returns the
  /// default operand for this optional operand. This method is only used if
  /// IsOptional == 1. If not set, this will default to "defaultFooOperands",
  /// where Foo is the AsmOperandClass name. The method signature should be:
  ///   std::unique_ptr<MCParsedAsmOperand> defaultFooOperands() const;
  string DefaultMethod = ?;
}

def ImmAsmOperand : AsmOperandClass {
  let Name = "Imm";
}

/// Operand Types - These provide the built-in operand types that may be used
/// by a target.  Targets can optionally provide their own operand types as
/// needed, though this should not be needed for RISC targets.
class Operand<ValueType ty> : DAGOperand {
  ValueType Type = ty;
  string PrintMethod = "printOperand";
  string EncoderMethod = "";
  bit hasCompleteDecoder = true;
  string OperandType = "OPERAND_UNKNOWN";
  dag MIOperandInfo = (ops);

  // MCOperandPredicate - Optionally, a code fragment operating on
  // const MCOperand &MCOp, and returning a bool, to indicate if
  // the value of MCOp is valid for the specific subclass of Operand
  code MCOperandPredicate;

  // ParserMatchClass - The "match class" that operands of this type fit
  // in. Match classes are used to define the order in which instructions are
  // match, to ensure that which instructions gets matched is deterministic.
  //
  // The target specific parser must be able to classify an parsed operand into
  // a unique class, which does not partially overlap with any other classes. It
  // can match a subset of some other class, in which case the AsmOperandClass
  // should declare the other operand as one of its super classes.
  AsmOperandClass ParserMatchClass = ImmAsmOperand;
}

class RegisterOperand<RegisterClass regclass, string pm = "printOperand">
  : DAGOperand {
  // RegClass - The register class of the operand.
  RegisterClass RegClass = regclass;
  // PrintMethod - The target method to call to print register operands of
  // this type. The method normally will just use an alt-name index to look
  // up the name to print. Default to the generic printOperand().
  string PrintMethod = pm;

  // EncoderMethod - The target method name to call to encode this register
  // operand.
  string EncoderMethod = "";

  // ParserMatchClass - The "match class" that operands of this type fit
  // in. Match classes are used to define the order in which instructions are
  // match, to ensure that which instructions gets matched is deterministic.
  //
  // The target specific parser must be able to classify an parsed operand into
  // a unique class, which does not partially overlap with any other classes. It
  // can match a subset of some other class, in which case the AsmOperandClass
  // should declare the other operand as one of its super classes.
  AsmOperandClass ParserMatchClass;

  string OperandType = "OPERAND_REGISTER";

  // When referenced in the result of a CodeGen pattern, GlobalISel will
  // normally copy the matched operand to the result. When this is set, it will
  // emit a special copy that will replace zero-immediates with the specified
  // zero-register.
  Register GIZeroRegister = ?;
}

let OperandType = "OPERAND_IMMEDIATE" in {
def i1imm  : Operand<i1>;
def i8imm  : Operand<i8>;
def i16imm : Operand<i16>;
def i32imm : Operand<i32>;
def i64imm : Operand<i64>;

def f32imm : Operand<f32>;
def f64imm : Operand<f64>;
}

// Register operands for generic instructions don't have an MVT, but do have
// constraints linking the operands (e.g. all operands of a G_ADD must
// have the same LLT).
class TypedOperand<string Ty> : Operand<untyped> {
  let OperandType = Ty;
  bit IsPointer = false;
  bit IsImmediate = false;
}

def type0 : TypedOperand<"OPERAND_GENERIC_0">;
def type1 : TypedOperand<"OPERAND_GENERIC_1">;
def type2 : TypedOperand<"OPERAND_GENERIC_2">;
def type3 : TypedOperand<"OPERAND_GENERIC_3">;
def type4 : TypedOperand<"OPERAND_GENERIC_4">;
def type5 : TypedOperand<"OPERAND_GENERIC_5">;

let IsPointer = true in {
  def ptype0 : TypedOperand<"OPERAND_GENERIC_0">;
  def ptype1 : TypedOperand<"OPERAND_GENERIC_1">;
  def ptype2 : TypedOperand<"OPERAND_GENERIC_2">;
  def ptype3 : TypedOperand<"OPERAND_GENERIC_3">;
  def ptype4 : TypedOperand<"OPERAND_GENERIC_4">;
  def ptype5 : TypedOperand<"OPERAND_GENERIC_5">;
}

// untyped_imm is for operands where isImm() will be true. It currently has no
// special behaviour and is only used for clarity.
def untyped_imm_0 : TypedOperand<"OPERAND_GENERIC_IMM_0"> {
  let IsImmediate = true;
}

/// zero_reg definition - Special node to stand for the zero register.
///
def zero_reg;

/// undef_tied_input - Special node to indicate an input register tied
/// to an output which defaults to IMPLICIT_DEF.
def undef_tied_input;

/// All operands which the MC layer classifies as predicates should inherit from
/// this class in some manner. This is already handled for the most commonly
/// used PredicateOperand, but may be useful in other circumstances.
class PredicateOp;

/// OperandWithDefaultOps - This Operand class can be used as the parent class
/// for an Operand that needs to be initialized with a default value if
/// no value is supplied in a pattern.  This class can be used to simplify the
/// pattern definitions for instructions that have target specific flags
/// encoded as immediate operands.
class OperandWithDefaultOps<ValueType ty, dag defaultops>
  : Operand<ty> {
  dag DefaultOps = defaultops;
}

/// PredicateOperand - This can be used to define a predicate operand for an
/// instruction.  OpTypes specifies the MIOperandInfo for the operand, and
/// AlwaysVal specifies the value of this predicate when set to "always
/// execute".
class PredicateOperand<ValueType ty, dag OpTypes, dag AlwaysVal>
  : OperandWithDefaultOps<ty, AlwaysVal>, PredicateOp {
  let MIOperandInfo = OpTypes;
}

/// OptionalDefOperand - This is used to define a optional definition operand
/// for an instruction. DefaultOps is the register the operand represents if
/// none is supplied, e.g. zero_reg.
class OptionalDefOperand<ValueType ty, dag OpTypes, dag defaultops>
  : OperandWithDefaultOps<ty, defaultops> {
  let MIOperandInfo = OpTypes;
}


// InstrInfo - This class should only be instantiated once to provide parameters
// which are global to the target machine.
//
class InstrInfo {
  // Target can specify its instructions in either big or little-endian formats.
  // For instance, while both Sparc and PowerPC are big-endian platforms, the
  // Sparc manual specifies its instructions in the format [31..0] (big), while
  // PowerPC specifies them using the format [0..31] (little).
  bit isLittleEndianEncoding = false;

  // The instruction properties mayLoad, mayStore, and hasSideEffects are unset
  // by default, and TableGen will infer their value from the instruction
  // pattern when possible.
  //
  // Normally, TableGen will issue an error if it can't infer the value of a
  // property that hasn't been set explicitly. When guessInstructionProperties
  // is set, it will guess a safe value instead.
  //
  // This option is a temporary migration help. It will go away.
  bit guessInstructionProperties = true;
}

// Standard Pseudo Instructions.
// This list must match TargetOpcodes.def.
// Only these instructions are allowed in the TargetOpcode namespace.
// Ensure mayLoad and mayStore have a default value, so as not to break
// targets that set guessInstructionProperties=0. Any local definition of
// mayLoad/mayStore takes precedence over these default values.
class StandardPseudoInstruction : Instruction {
  let mayLoad = false;
  let mayStore = false;
  let isCodeGenOnly = true;
  let isPseudo = true;
  let hasNoSchedulingInfo = true;
  let Namespace = "TargetOpcode";
}
def PHI : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins variable_ops);
  let AsmString = "PHINODE";
  let hasSideEffects = false;
}
def INLINEASM : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "";
  let hasSideEffects = false;  // Note side effect is encoded in an operand.
}
def INLINEASM_BR : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "";
  // Unlike INLINEASM, this is always treated as having side-effects.
  let hasSideEffects = true;
  // Despite potentially branching, this instruction is intentionally _not_
  // marked as a terminator or a branch.
}
def CFI_INSTRUCTION : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "";
  let hasCtrlDep = true;
  let hasSideEffects = false;
  let isNotDuplicable = true;
  let isMeta = true;
}
def EH_LABEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "";
  let hasCtrlDep = true;
  let hasSideEffects = false;
  let isNotDuplicable = true;
  let isMeta = true;
}
def GC_LABEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "";
  let hasCtrlDep = true;
  let hasSideEffects = false;
  let isNotDuplicable = true;
  let isMeta = true;
}
def ANNOTATION_LABEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "";
  let hasCtrlDep = true;
  let hasSideEffects = false;
  let isNotDuplicable = true;
}
def KILL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "";
  let hasSideEffects = false;
  let isMeta = true;
}
def EXTRACT_SUBREG : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$supersrc, i32imm:$subidx);
  let AsmString = "";
  let hasSideEffects = false;
}
def INSERT_SUBREG : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$supersrc, unknown:$subsrc, i32imm:$subidx);
  let AsmString = "";
  let hasSideEffects = false;
  let Constraints = "$supersrc = $dst";
}
def IMPLICIT_DEF : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins);
  let AsmString = "";
  let hasSideEffects = false;
  let isReMaterializable = true;
  let isAsCheapAsAMove = true;
  let isMeta = true;
}
def SUBREG_TO_REG : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$implsrc, unknown:$subsrc, i32imm:$subidx);
  let AsmString = "";
  let hasSideEffects = false;
}
def COPY_TO_REGCLASS : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$src, i32imm:$regclass);
  let AsmString = "";
  let hasSideEffects = false;
  let isAsCheapAsAMove = true;
}
def DBG_VALUE : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "DBG_VALUE";
  let hasSideEffects = false;
  let isMeta = true;
}
def DBG_VALUE_LIST : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "DBG_VALUE_LIST";
  let hasSideEffects = 0;
  let isMeta = true;
}
def DBG_INSTR_REF : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "DBG_INSTR_REF";
  let hasSideEffects = false;
  let isMeta = true;
}
def DBG_PHI : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "DBG_PHI";
  let hasSideEffects = 0;
  let isMeta = true;
}
def DBG_LABEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$label);
  let AsmString = "DBG_LABEL";
  let hasSideEffects = false;
  let isMeta = true;
}
def REG_SEQUENCE : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$supersrc, variable_ops);
  let AsmString = "";
  let hasSideEffects = false;
  let isAsCheapAsAMove = true;
}
def COPY : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$src);
  let AsmString = "";
  let hasSideEffects = false;
  let isAsCheapAsAMove = true;
  let hasNoSchedulingInfo = false;
}
def BUNDLE : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "BUNDLE";
  let hasSideEffects = false;
}
def LIFETIME_START : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "LIFETIME_START";
  let hasSideEffects = false;
  let isMeta = true;
}
def LIFETIME_END : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$id);
  let AsmString = "LIFETIME_END";
  let hasSideEffects = false;
  let isMeta = true;
}
def PSEUDO_PROBE : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i64imm:$guid, i64imm:$index, i8imm:$type, i32imm:$attr);
  let AsmString = "PSEUDO_PROBE";
  let hasSideEffects = 1;
  let isMeta = true;
}
def ARITH_FENCE : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$src);
  let AsmString = "";
  let hasSideEffects = false;
  let Constraints = "$src = $dst";
  let isMeta = true;
}

def STACKMAP : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i64imm:$id, i32imm:$nbytes, variable_ops);
  let hasSideEffects = true;
  let isCall = true;
  let mayLoad = true;
  let usesCustomInserter = true;
}
def PATCHPOINT : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins i64imm:$id, i32imm:$nbytes, unknown:$callee,
                       i32imm:$nargs, i32imm:$cc, variable_ops);
  let hasSideEffects = true;
  let isCall = true;
  let mayLoad = true;
  let usesCustomInserter = true;
}
def STATEPOINT : StandardPseudoInstruction {
  let OutOperandList = (outs variable_ops);
  let InOperandList = (ins variable_ops);
  let usesCustomInserter = true;
  let mayLoad = true;
  let mayStore = true;
  let hasSideEffects = true;
  let isCall = true;
}
def LOAD_STACK_GUARD : StandardPseudoInstruction {
  let OutOperandList = (outs ptr_rc:$dst);
  let InOperandList = (ins);
  let mayLoad = true;
  bit isReMaterializable = true;
  let hasSideEffects = false;
  bit isPseudo = true;
}
def PREALLOCATED_SETUP : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$a);
  let usesCustomInserter = true;
  let hasSideEffects = true;
}
def PREALLOCATED_ARG : StandardPseudoInstruction {
  let OutOperandList = (outs ptr_rc:$loc);
  let InOperandList = (ins i32imm:$a, i32imm:$b);
  let usesCustomInserter = true;
  let hasSideEffects = true;
}
def LOCAL_ESCAPE : StandardPseudoInstruction {
  // This instruction is really just a label. It has to be part of the chain so
  // that it doesn't get dropped from the DAG, but it produces nothing and has
  // no side effects.
  let OutOperandList = (outs);
  let InOperandList = (ins ptr_rc:$symbol, i32imm:$id);
  let hasSideEffects = false;
  let hasCtrlDep = true;
}
def FAULTING_OP : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins variable_ops);
  let usesCustomInserter = true;
  let hasSideEffects = true;
  let mayLoad = true;
  let mayStore = true;
  let isTerminator = true;
  let isBranch = true;
}
def PATCHABLE_OP : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let usesCustomInserter = true;
  let mayLoad = true;
  let mayStore = true;
  let hasSideEffects = true;
}
def PATCHABLE_FUNCTION_ENTER : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = "# XRay Function Enter.";
  let usesCustomInserter = true;
  let hasSideEffects = true;
}
def PATCHABLE_RET : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "# XRay Function Patchable RET.";
  let usesCustomInserter = true;
  let hasSideEffects = true;
  let isTerminator = true;
  let isReturn = true;
}
def PATCHABLE_FUNCTION_EXIT : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = "# XRay Function Exit.";
  let usesCustomInserter = true;
  let hasSideEffects = true;
  let isReturn = false; // Original return instruction will follow
}
def PATCHABLE_TAIL_CALL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "# XRay Tail Call Exit.";
  let usesCustomInserter = true;
  let hasSideEffects = true;
  let isReturn = true;
}
def PATCHABLE_EVENT_CALL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptr_rc:$event, unknown:$size);
  let AsmString = "# XRay Custom Event Log.";
  let usesCustomInserter = true;
  let isCall = true;
  let mayLoad = true;
  let mayStore = true;
  let hasSideEffects = true;
}
def PATCHABLE_TYPED_EVENT_CALL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$type, ptr_rc:$event, unknown:$size);
  let AsmString = "# XRay Typed Event Log.";
  let usesCustomInserter = true;
  let isCall = true;
  let mayLoad = true;
  let mayStore = true;
  let hasSideEffects = true;
}
def FENTRY_CALL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = "# FEntry call";
  let usesCustomInserter = true;
  let isCall = true;
  let mayLoad = true;
  let mayStore = true;
  let hasSideEffects = true;
}
def ICALL_BRANCH_FUNNEL : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins variable_ops);
  let AsmString = "";
  let hasSideEffects = true;
}
def MEMBARRIER : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let AsmString = "";
  let hasSideEffects = true;
  let Size = 0;
  let isMeta = true;
}
def JUMP_TABLE_DEBUG_INFO : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i64imm:$jti);
  let AsmString = "";
  let hasSideEffects = false;
  let Size = 0;
  let isMeta = true;
}

let hasSideEffects = false, isMeta = true, isConvergent = true in {
def CONVERGENCECTRL_ANCHOR : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins);
}
def CONVERGENCECTRL_ENTRY : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins);
}
def CONVERGENCECTRL_LOOP : StandardPseudoInstruction {
  let OutOperandList = (outs unknown:$dst);
  let InOperandList = (ins unknown:$src);
}
def CONVERGENCECTRL_GLUE : StandardPseudoInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$src);
}
}

// Generic opcodes used in GlobalISel.
include "llvm/Target/GenericOpcodes.td"

//===----------------------------------------------------------------------===//
// AsmParser - This class can be implemented by targets that wish to implement
// .s file parsing.
//
// Subtargets can have multiple different assembly parsers (e.g. AT&T vs Intel
// syntax on X86 for example).
//
class AsmParser {
  // AsmParserClassName - This specifies the suffix to use for the asmparser
  // class.  Generated AsmParser classes are always prefixed with the target
  // name.
  string AsmParserClassName  = "AsmParser";

  // AsmParserInstCleanup - If non-empty, this is the name of a custom member
  // function of the AsmParser class to call on every matched instruction.
  // This can be used to perform target specific instruction post-processing.
  string AsmParserInstCleanup  = "";

  // ShouldEmitMatchRegisterName - Set to false if the target needs a hand
  // written register name matcher
  bit ShouldEmitMatchRegisterName = true;

  // Set to true if the target needs a generated 'alternative register name'
  // matcher.
  //
  // This generates a function which can be used to lookup registers from
  // their aliases. This function will fail when called on targets where
  // several registers share the same alias (i.e. not a 1:1 mapping).
  bit ShouldEmitMatchRegisterAltName = false;

  // Set to true if MatchRegisterName and MatchRegisterAltName functions
  // should be generated even if there are duplicate register names. The
  // target is responsible for coercing aliased registers as necessary
  // (e.g. in validateTargetOperandClass), and there are no guarantees about
  // which numeric register identifier will be returned in the case of
  // multiple matches.
  bit AllowDuplicateRegisterNames = false;

  // HasMnemonicFirst - Set to false if target instructions don't always
  // start with a mnemonic as the first token.
  bit HasMnemonicFirst = true;

  // ReportMultipleNearMisses -
  // When 0, the assembly matcher reports an error for one encoding or operand
  // that did not match the parsed instruction.
  // When 1, the assembly matcher returns a list of encodings that were close
  // to matching the parsed instruction, so to allow more detailed error
  // messages.
  bit ReportMultipleNearMisses = false;

  // OperandParserMethod - If non-empty, this is the name of a custom
  // member function of the AsmParser class to call for every instruction
  // operand to be parsed.
  string OperandParserMethod = "";

  // CallCustomParserForAllOperands - Set to true if the custom parser
  // method shall be called for all operands as opposed to only those
  // that have their own specified custom parsers.
  bit CallCustomParserForAllOperands = false;

  // PreferSmallerInstructions - Should the assembly matcher prefer the smaller
  // instructions.
  // 
  // This is useful for the ARM instructions set where smaller encodings must
  // be preferentially selected.
  //
  // The preference order is: 
  //    Instrution size (if this option is enabled, smallest first)
  //    Number of Operands (least first), 
  //    Operand Classes (lexicographically by operand), 
  //    (Optional) Instruction id (see AsmMatcherEmitter.cpp for details), 
  //    Number of required features (least first)
  bit PreferSmallerInstructions = false;
}
def DefaultAsmParser : AsmParser;

//===----------------------------------------------------------------------===//
// AsmParserVariant - Subtargets can have multiple different assembly parsers
// (e.g. AT&T vs Intel syntax on X86 for example). This class can be
// implemented by targets to describe such variants.
//
class AsmParserVariant {
  // Variant - AsmParsers can be of multiple different variants.  Variants are
  // used to support targets that need to parse multiple formats for the
  // assembly language.
  int Variant = 0;

  // Name - The AsmParser variant name (e.g., AT&T vs Intel).
  string Name = "";

  // CommentDelimiter - If given, the delimiter string used to recognize
  // comments which are hard coded in the .td assembler strings for individual
  // instructions.
  string CommentDelimiter = "";

  // RegisterPrefix - If given, the token prefix which indicates a register
  // token. This is used by the matcher to automatically recognize hard coded
  // register tokens as constrained registers, instead of tokens, for the
  // purposes of matching.
  string RegisterPrefix = "";

  // TokenizingCharacters - Characters that are standalone tokens
  string TokenizingCharacters = "[]*!";

  // SeparatorCharacters - Characters that are not tokens
  string SeparatorCharacters = " \t,";

  // BreakCharacters - Characters that start new identifiers
  string BreakCharacters = "";
}
def DefaultAsmParserVariant : AsmParserVariant;

// Operators for combining SubtargetFeatures in AssemblerPredicates
def any_of;
def all_of;

/// AssemblerPredicate - This is a Predicate that can be used when the assembler
/// matches instructions and aliases.
class AssemblerPredicate<dag cond, string name = ""> {
  bit AssemblerMatcherPredicate = true;
  dag AssemblerCondDag = cond;
  string PredicateName = name;
}

/// TokenAlias - This class allows targets to define assembler token
/// operand aliases. That is, a token literal operand which is equivalent
/// to another, canonical, token literal. For example, ARM allows:
///   vmov.u32 s4, #0  -> vmov.i32, #0
/// 'u32' is a more specific designator for the 32-bit integer type specifier
/// and is legal for any instruction which accepts 'i32' as a datatype suffix.
///   def : TokenAlias<".u32", ".i32">;
///
/// This works by marking the match class of 'From' as a subclass of the
/// match class of 'To'.
class TokenAlias<string From, string To> {
  string FromToken = From;
  string ToToken = To;
}

/// MnemonicAlias - This class allows targets to define assembler mnemonic
/// aliases.  This should be used when all forms of one mnemonic are accepted
/// with a different mnemonic.  For example, X86 allows:
///   sal %al, 1    -> shl %al, 1
///   sal %ax, %cl  -> shl %ax, %cl
///   sal %eax, %cl -> shl %eax, %cl
/// etc.  Though "sal" is accepted with many forms, all of them are directly
/// translated to a shl, so it can be handled with (in the case of X86, it
/// actually has one for each suffix as well):
///   def : MnemonicAlias<"sal", "shl">;
///
/// Mnemonic aliases are mapped before any other translation in the match phase,
/// and do allow Requires predicates, e.g.:
///
///  def : MnemonicAlias<"pushf", "pushfq">, Requires<[In64BitMode]>;
///  def : MnemonicAlias<"pushf", "pushfl">, Requires<[In32BitMode]>;
///
/// Mnemonic aliases can also be constrained to specific variants, e.g.:
///
///  def : MnemonicAlias<"pushf", "pushfq", "att">, Requires<[In64BitMode]>;
///
/// If no variant (e.g., "att" or "intel") is specified then the alias is
/// applied unconditionally.
class MnemonicAlias<string From, string To, string VariantName = ""> {
  string FromMnemonic = From;
  string ToMnemonic = To;
  string AsmVariantName = VariantName;

  // Predicates - Predicates that must be true for this remapping to happen.
  list<Predicate> Predicates = [];
}

/// InstAlias - This defines an alternate assembly syntax that is allowed to
/// match an instruction that has a different (more canonical) assembly
/// representation.
class InstAlias<string Asm, dag Result, int Emit = 1, string VariantName = ""> {
  string AsmString = Asm;      // The .s format to match the instruction with.
  dag ResultInst = Result;     // The MCInst to generate.

  // This determines which order the InstPrinter detects aliases for
  // printing. A larger value makes the alias more likely to be
  // emitted. The Instruction's own definition is notionally 0.5, so 0
  // disables printing and 1 enables it if there are no conflicting aliases.
  int EmitPriority = Emit;

  // Predicates - Predicates that must be true for this to match.
  list<Predicate> Predicates = [];

  // If the instruction specified in Result has defined an AsmMatchConverter
  // then setting this to 1 will cause the alias to use the AsmMatchConverter
  // function when converting the OperandVector into an MCInst instead of the
  // function that is generated by the dag Result.
  // Setting this to 0 will cause the alias to ignore the Result instruction's
  // defined AsmMatchConverter and instead use the function generated by the
  // dag Result.
  bit UseInstAsmMatchConverter = true;

  // Assembler variant name to use for this alias. If not specified then
  // assembler variants will be determined based on AsmString
  string AsmVariantName = VariantName;
}

//===----------------------------------------------------------------------===//
// AsmWriter - This class can be implemented by targets that need to customize
// the format of the .s file writer.
//
// Subtargets can have multiple different asmwriters (e.g. AT&T vs Intel syntax
// on X86 for example).
//
class AsmWriter {
  // AsmWriterClassName - This specifies the suffix to use for the asmwriter
  // class.  Generated AsmWriter classes are always prefixed with the target
  // name.
  string AsmWriterClassName  = "InstPrinter";

  // PassSubtarget - Determines whether MCSubtargetInfo should be passed to
  // the various print methods.
  // FIXME: Remove after all ports are updated.
  int PassSubtarget = 0;

  // Variant - AsmWriters can be of multiple different variants.  Variants are
  // used to support targets that need to emit assembly code in ways that are
  // mostly the same for different targets, but have minor differences in
  // syntax.  If the asmstring contains {|} characters in them, this integer
  // will specify which alternative to use.  For example "{x|y|z}" with Variant
  // == 1, will expand to "y".
  int Variant = 0;
}
def DefaultAsmWriter : AsmWriter;


//===----------------------------------------------------------------------===//
// Target - This class contains the "global" target information
//
class Target {
  // InstructionSet - Instruction set description for this target.
  InstrInfo InstructionSet;

  // AssemblyParsers - The AsmParser instances available for this target.
  list<AsmParser> AssemblyParsers = [DefaultAsmParser];

  /// AssemblyParserVariants - The AsmParserVariant instances available for
  /// this target.
  list<AsmParserVariant> AssemblyParserVariants = [DefaultAsmParserVariant];

  // AssemblyWriters - The AsmWriter instances available for this target.
  list<AsmWriter> AssemblyWriters = [DefaultAsmWriter];

  // AllowRegisterRenaming - Controls whether this target allows
  // post-register-allocation renaming of registers.  This is done by
  // setting hasExtraDefRegAllocReq and hasExtraSrcRegAllocReq to 1
  // for all opcodes if this flag is set to 0.
  int AllowRegisterRenaming = 0;
}

//===----------------------------------------------------------------------===//
// Processor chip sets - These values represent each of the chip sets supported
// by the scheduler.  Each Processor definition requires corresponding
// instruction itineraries.
//
class Processor<string n, ProcessorItineraries pi, list<SubtargetFeature> f,
                list<SubtargetFeature> tunef = []> {
  // Name - Chip set name.  Used by command line (-mcpu=) to determine the
  // appropriate target chip.
  //
  string Name = n;

  // SchedModel - The machine model for scheduling and instruction cost.
  //
  SchedMachineModel SchedModel = NoSchedModel;

  // ProcItin - The scheduling information for the target processor.
  //
  ProcessorItineraries ProcItin = pi;

  // Features - list of
  list<SubtargetFeature> Features = f;

  // TuneFeatures - list of features for tuning for this CPU. If the target
  // supports -mtune, this should contain the list of features used to make
  // microarchitectural optimization decisions for a given processor.  While
  // Features should contain the architectural features for the processor.
  list<SubtargetFeature> TuneFeatures = tunef;
}

// ProcessorModel allows subtargets to specify the more general
// SchedMachineModel instead if a ProcessorItinerary. Subtargets will
// gradually move to this newer form.
//
// Although this class always passes NoItineraries to the Processor
// class, the SchedMachineModel may still define valid Itineraries.
class ProcessorModel<string n, SchedMachineModel m, list<SubtargetFeature> f,
                     list<SubtargetFeature> tunef = []>
  : Processor<n, NoItineraries, f, tunef> {
  let SchedModel = m;
}

//===----------------------------------------------------------------------===//
// InstrMapping - This class is used to create mapping tables to relate
// instructions with each other based on the values specified in RowFields,
// ColFields, KeyCol and ValueCols.
//
class InstrMapping {
  // FilterClass - Used to limit search space only to the instructions that
  // define the relationship modeled by this InstrMapping record.
  string FilterClass;

  // RowFields - List of fields/attributes that should be same for all the
  // instructions in a row of the relation table. Think of this as a set of
  // properties shared by all the instructions related by this relationship
  // model and is used to categorize instructions into subgroups. For instance,
  // if we want to define a relation that maps 'Add' instruction to its
  // predicated forms, we can define RowFields like this:
  //
  // let RowFields = BaseOp
  // All add instruction predicated/non-predicated will have to set their BaseOp
  // to the same value.
  //
  // def Add: { let BaseOp = 'ADD'; let predSense = 'nopred' }
  // def Add_predtrue: { let BaseOp = 'ADD'; let predSense = 'true' }
  // def Add_predfalse: { let BaseOp = 'ADD'; let predSense = 'false'  }
  list<string> RowFields = [];

  // List of fields/attributes that are same for all the instructions
  // in a column of the relation table.
  // Ex: let ColFields = 'predSense' -- It means that the columns are arranged
  // based on the 'predSense' values. All the instruction in a specific
  // column have the same value and it is fixed for the column according
  // to the values set in 'ValueCols'.
  list<string> ColFields = [];

  // Values for the fields/attributes listed in 'ColFields'.
  // Ex: let KeyCol = 'nopred' -- It means that the key instruction (instruction
  // that models this relation) should be non-predicated.
  // In the example above, 'Add' is the key instruction.
  list<string> KeyCol = [];

  // List of values for the fields/attributes listed in 'ColFields', one for
  // each column in the relation table.
  //
  // Ex: let ValueCols = [['true'],['false']] -- It adds two columns in the
  // table. First column requires all the instructions to have predSense
  // set to 'true' and second column requires it to be 'false'.
  list<list<string> > ValueCols = [];
}

//===----------------------------------------------------------------------===//
// Pull in the common support for calling conventions.
//
include "llvm/Target/TargetCallingConv.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for DAG isel generation.
//
include "llvm/Target/TargetSelectionDAG.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for Global ISel register bank info generation.
//
include "llvm/Target/GlobalISel/RegisterBank.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for DAG isel generation.
//
include "llvm/Target/GlobalISel/Target.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for the Global ISel DAG-based selector generation.
//
include "llvm/Target/GlobalISel/SelectionDAGCompat.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for Pfm Counters generation.
//
include "llvm/Target/TargetPfmCounters.td"

//===----------------------------------------------------------------------===//
// Pull in the common support for macro fusion.
//
include "llvm/Target/TargetMacroFusion.td"
