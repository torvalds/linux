//===- Patterns.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Contains the Pattern hierarchy alongside helper classes such as
/// PatFrag, MIFlagsInfo, PatternType, etc.
///
/// These classes are used by the GlobalISel Combiner backend to help parse,
/// process and emit MIR patterns.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_GLOBALISEL_PATTERNS_H
#define LLVM_UTILS_GLOBALISEL_PATTERNS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include <memory>
#include <optional>
#include <string>

namespace llvm {

class Record;
class SMLoc;
class StringInit;
class CodeExpansions;
class CodeGenInstruction;
struct CodeGenIntrinsic;

namespace gi {

class CXXPredicateCode;
class LLTCodeGen;
class LLTCodeGenOrTempType;
class RuleMatcher;

//===- PatternType --------------------------------------------------------===//

/// Represent the type of a Pattern Operand.
///
/// Types have two form:
///   - LLTs, which are straightforward.
///   - Special types, e.g. GITypeOf
class PatternType {
public:
  static constexpr StringLiteral SpecialTyClassName = "GISpecialType";
  static constexpr StringLiteral TypeOfClassName = "GITypeOf";

  enum PTKind : uint8_t {
    PT_None,

    PT_ValueType,
    PT_TypeOf,
  };

  PatternType() : Kind(PT_None), Data() {}

  static std::optional<PatternType> get(ArrayRef<SMLoc> DiagLoc,
                                        const Record *R, Twine DiagCtx);
  static PatternType getTypeOf(StringRef OpName);

  bool isNone() const { return Kind == PT_None; }
  bool isLLT() const { return Kind == PT_ValueType; }
  bool isSpecial() const { return isTypeOf(); }
  bool isTypeOf() const { return Kind == PT_TypeOf; }

  StringRef getTypeOfOpName() const;
  const Record *getLLTRecord() const;

  explicit operator bool() const { return !isNone(); }

  bool operator==(const PatternType &Other) const;
  bool operator!=(const PatternType &Other) const { return !operator==(Other); }

  std::string str() const;

private:
  PatternType(PTKind Kind) : Kind(Kind), Data() {}

  PTKind Kind;
  union DataT {
    DataT() : Str() {}

    /// PT_ValueType -> ValueType Def.
    const Record *Def;

    /// PT_TypeOf -> Operand name (without the '$')
    StringRef Str;
  } Data;
};

//===- Pattern Base Class -------------------------------------------------===//

/// Base class for all patterns that can be written in an `apply`, `match` or
/// `pattern` DAG operator.
///
/// For example:
///
///     (apply (G_ZEXT $x, $y), (G_ZEXT $y, $z), "return isFoo(${z})")
///
/// Creates 3 Pattern objects:
///   - Two CodeGenInstruction Patterns
///   - A CXXPattern
class Pattern {
public:
  enum {
    K_AnyOpcode,
    K_CXX,

    K_CodeGenInstruction,
    K_PatFrag,
    K_Builtin,
  };

  virtual ~Pattern() = default;

  unsigned getKind() const { return Kind; }
  const char *getKindName() const;

  bool hasName() const { return !Name.empty(); }
  StringRef getName() const { return Name; }

  virtual void print(raw_ostream &OS, bool PrintName = true) const = 0;
  void dump() const;

protected:
  Pattern(unsigned Kind, StringRef Name) : Kind(Kind), Name(Name) {
    assert(!Name.empty() && "unnamed pattern!");
  }

  void printImpl(raw_ostream &OS, bool PrintName,
                 function_ref<void()> ContentPrinter) const;

private:
  unsigned Kind;
  StringRef Name;
};

//===- AnyOpcodePattern ---------------------------------------------------===//

/// `wip_match_opcode` patterns.
/// This matches one or more opcodes, and does not check any operands
/// whatsoever.
///
/// TODO: Long-term, this needs to be removed. It's a hack around MIR
///       pattern matching limitations.
class AnyOpcodePattern : public Pattern {
public:
  AnyOpcodePattern(StringRef Name) : Pattern(K_AnyOpcode, Name) {}

  static bool classof(const Pattern *P) { return P->getKind() == K_AnyOpcode; }

  void addOpcode(const CodeGenInstruction *I) { Insts.push_back(I); }
  const auto &insts() const { return Insts; }

  void print(raw_ostream &OS, bool PrintName = true) const override;

private:
  SmallVector<const CodeGenInstruction *, 4> Insts;
};

//===- CXXPattern ---------------------------------------------------------===//

/// Represents raw C++ code which may need some expansions.
///
///   e.g. [{ return isFooBux(${src}.getReg()); }]
///
/// For the expanded code, \see CXXPredicateCode. CXXPredicateCode objects are
/// created through `expandCode`.
///
/// \see CodeExpander and \see CodeExpansions for more information on code
/// expansions.
///
/// This object has two purposes:
///   - Represent C++ code as a pattern entry.
///   - Be a factory for expanded C++ code.
///     - It's immutable and only holds the raw code so we can expand the same
///       CXX pattern multiple times if we need to.
///
/// Note that the code is always trimmed in the constructor, so leading and
/// trailing whitespaces are removed. This removes bloat in the output, avoids
/// formatting issues, but also allows us to check things like
/// `.startswith("return")` trivially without worrying about spaces.
class CXXPattern : public Pattern {
public:
  CXXPattern(const StringInit &Code, StringRef Name);

  CXXPattern(StringRef Code, StringRef Name)
      : Pattern(K_CXX, Name), RawCode(Code.trim().str()) {}

  static bool classof(const Pattern *P) { return P->getKind() == K_CXX; }

  void setIsApply(bool Value = true) { IsApply = Value; }
  StringRef getRawCode() const { return RawCode; }

  /// Expands raw code, replacing things such as `${foo}` with their
  /// substitution in \p CE.
  ///
  /// Can only be used on 'match' CXX Patterns. 'apply' CXX pattern emission
  /// is handled differently as we emit both the 'match' and 'apply' part
  /// together in a single Custom CXX Action.
  ///
  /// \param CE     Map of Code Expansions
  /// \param Locs   SMLocs for the Code Expander, in case it needs to emit
  ///               diagnostics.
  /// \param AddComment Optionally called to emit a comment before the expanded
  ///                   code.
  ///
  /// \return A CXXPredicateCode object that contains the expanded code. Note
  /// that this may or may not insert a new object. All CXXPredicateCode objects
  /// are held in a set to avoid emitting duplicate C++ code.
  const CXXPredicateCode &
  expandCode(const CodeExpansions &CE, ArrayRef<SMLoc> Locs,
             function_ref<void(raw_ostream &)> AddComment = {}) const;

  void print(raw_ostream &OS, bool PrintName = true) const override;

private:
  bool IsApply = false;
  std::string RawCode;
};

//===- InstructionPattern ---------------------------------------------===//

/// An operand for an InstructionPattern.
///
/// Operands are composed of three elements:
///   - (Optional) Value
///   - (Optional) Name
///   - (Optional) Type
///
/// Some examples:
///   (i32 0):$x -> V=int(0), Name='x', Type=i32
///   0:$x -> V=int(0), Name='x'
///   $x -> Name='x'
///   i32:$x -> Name='x', Type = i32
class InstructionOperand {
public:
  using IntImmTy = int64_t;

  InstructionOperand(IntImmTy Imm, StringRef Name, PatternType Type)
      : Value(Imm), Name(Name), Type(Type) {}

  InstructionOperand(StringRef Name, PatternType Type)
      : Name(Name), Type(Type) {}

  bool isNamedImmediate() const { return hasImmValue() && isNamedOperand(); }

  bool hasImmValue() const { return Value.has_value(); }
  IntImmTy getImmValue() const { return *Value; }

  bool isNamedOperand() const { return !Name.empty(); }
  StringRef getOperandName() const {
    assert(isNamedOperand() && "Operand is unnamed");
    return Name;
  }

  InstructionOperand withNewName(StringRef NewName) const {
    InstructionOperand Result = *this;
    Result.Name = NewName;
    return Result;
  }

  void setIsDef(bool Value = true) { Def = Value; }
  bool isDef() const { return Def; }

  void setType(PatternType NewType) {
    assert((!Type || (Type == NewType)) && "Overwriting type!");
    Type = NewType;
  }
  PatternType getType() const { return Type; }

  std::string describe() const;
  void print(raw_ostream &OS) const;

  void dump() const;

private:
  std::optional<int64_t> Value;
  StringRef Name;
  PatternType Type;
  bool Def = false;
};

/// Base class for CodeGenInstructionPattern & PatFragPattern, which handles all
/// the boilerplate for patterns that have a list of operands for some (pseudo)
/// instruction.
class InstructionPattern : public Pattern {
public:
  virtual ~InstructionPattern() = default;

  static bool classof(const Pattern *P) {
    return P->getKind() == K_CodeGenInstruction || P->getKind() == K_PatFrag ||
           P->getKind() == K_Builtin;
  }

  template <typename... Ty> void addOperand(Ty &&...Init) {
    Operands.emplace_back(std::forward<Ty>(Init)...);
  }

  auto &operands() { return Operands; }
  const auto &operands() const { return Operands; }
  unsigned operands_size() const { return Operands.size(); }
  InstructionOperand &getOperand(unsigned K) { return Operands[K]; }
  const InstructionOperand &getOperand(unsigned K) const { return Operands[K]; }

  /// When this InstructionPattern is used as the match root, returns the
  /// operands that must be redefined in the 'apply' pattern for the rule to be
  /// valid.
  ///
  /// For most patterns, this just returns the defs.
  /// For PatFrag this only returns the root of the PF.
  ///
  /// Returns an empty array on error.
  virtual ArrayRef<InstructionOperand> getApplyDefsNeeded() const {
    return {operands().begin(), getNumInstDefs()};
  }

  auto named_operands() {
    return make_filter_range(Operands,
                             [&](auto &O) { return O.isNamedOperand(); });
  }

  auto named_operands() const {
    return make_filter_range(Operands,
                             [&](auto &O) { return O.isNamedOperand(); });
  }

  virtual bool isVariadic() const { return false; }
  virtual unsigned getNumInstOperands() const = 0;
  virtual unsigned getNumInstDefs() const = 0;

  bool hasAllDefs() const { return operands_size() >= getNumInstDefs(); }

  virtual StringRef getInstName() const = 0;

  /// Diagnoses all uses of special types in this Pattern and returns true if at
  /// least one diagnostic was emitted.
  bool diagnoseAllSpecialTypes(ArrayRef<SMLoc> Loc, Twine Msg) const;

  void reportUnreachable(ArrayRef<SMLoc> Locs) const;
  virtual bool checkSemantics(ArrayRef<SMLoc> Loc);

  void print(raw_ostream &OS, bool PrintName = true) const override;

protected:
  InstructionPattern(unsigned K, StringRef Name) : Pattern(K, Name) {}

  virtual void printExtras(raw_ostream &OS) const {}

  SmallVector<InstructionOperand, 4> Operands;
};

//===- OperandTable -------------------------------------------------------===//

/// Maps InstructionPattern operands to their definitions. This allows us to tie
/// different patterns of a (apply), (match) or (patterns) set of patterns
/// together.
class OperandTable {
public:
  bool addPattern(InstructionPattern *P,
                  function_ref<void(StringRef)> DiagnoseRedef);

  struct LookupResult {
    LookupResult() = default;
    LookupResult(InstructionPattern *Def) : Found(true), Def(Def) {}

    bool Found = false;
    InstructionPattern *Def = nullptr;

    bool isLiveIn() const { return Found && !Def; }
  };

  LookupResult lookup(StringRef OpName) const {
    if (auto It = Table.find(OpName); It != Table.end())
      return LookupResult(It->second);
    return LookupResult();
  }

  InstructionPattern *getDef(StringRef OpName) const {
    return lookup(OpName).Def;
  }

  void print(raw_ostream &OS, StringRef Name = "", StringRef Indent = "") const;

  auto begin() const { return Table.begin(); }
  auto end() const { return Table.end(); }

  void dump() const;

private:
  StringMap<InstructionPattern *> Table;
};

//===- MIFlagsInfo --------------------------------------------------------===//

/// Helper class to contain data associated with a MIFlags operand.
class MIFlagsInfo {
public:
  void addSetFlag(const Record *R);
  void addUnsetFlag(const Record *R);
  void addCopyFlag(StringRef InstName);

  const auto &set_flags() const { return SetF; }
  const auto &unset_flags() const { return UnsetF; }
  const auto &copy_flags() const { return CopyF; }

private:
  SetVector<StringRef> SetF, UnsetF, CopyF;
};

//===- CodeGenInstructionPattern ------------------------------------------===//

/// Matches an instruction or intrinsic:
///    e.g. `G_ADD $x, $y, $z` or `int_amdgcn_cos $a`
///
/// Intrinsics are just normal instructions with a special operand for intrinsic
/// ID. Despite G_INTRINSIC opcodes being variadic, we consider that the
/// Intrinsic's info takes priority. This means we return:
///   - false for isVariadic() and other variadic-related queries.
///   - getNumInstDefs and getNumInstOperands use the intrinsic's in/out
///   operands.
class CodeGenInstructionPattern : public InstructionPattern {
public:
  CodeGenInstructionPattern(const CodeGenInstruction &I, StringRef Name)
      : InstructionPattern(K_CodeGenInstruction, Name), I(I) {}

  static bool classof(const Pattern *P) {
    return P->getKind() == K_CodeGenInstruction;
  }

  bool is(StringRef OpcodeName) const;

  void setIntrinsic(const CodeGenIntrinsic *I) { IntrinInfo = I; }
  const CodeGenIntrinsic *getIntrinsic() const { return IntrinInfo; }
  bool isIntrinsic() const { return IntrinInfo; }

  bool hasVariadicDefs() const;
  bool isVariadic() const override;
  unsigned getNumInstDefs() const override;
  unsigned getNumInstOperands() const override;

  MIFlagsInfo &getOrCreateMIFlagsInfo();
  const MIFlagsInfo *getMIFlagsInfo() const { return FI.get(); }

  const CodeGenInstruction &getInst() const { return I; }
  StringRef getInstName() const override;

private:
  void printExtras(raw_ostream &OS) const override;

  const CodeGenInstruction &I;
  const CodeGenIntrinsic *IntrinInfo = nullptr;
  std::unique_ptr<MIFlagsInfo> FI;
};

//===- OperandTypeChecker -------------------------------------------------===//

/// This is a trivial type checker for all operands in a set of
/// InstructionPatterns.
///
/// It infers the type of each operand, check it's consistent with the known
/// type of the operand, and then sets all of the types in all operands in
/// propagateTypes.
///
/// It also handles verifying correctness of special types.
class OperandTypeChecker {
public:
  OperandTypeChecker(ArrayRef<SMLoc> DiagLoc) : DiagLoc(DiagLoc) {}

  /// Step 1: Check each pattern one by one. All patterns that pass through here
  /// are added to a common worklist so propagateTypes can access them.
  bool check(InstructionPattern &P,
             std::function<bool(const PatternType &)> VerifyTypeOfOperand);

  /// Step 2: Propagate all types. e.g. if one use of "$a" has type i32, make
  /// all uses of "$a" have type i32.
  void propagateTypes();

protected:
  ArrayRef<SMLoc> DiagLoc;

private:
  using InconsistentTypeDiagFn = std::function<void()>;

  void PrintSeenWithTypeIn(InstructionPattern &P, StringRef OpName,
                           PatternType Ty) const;

  struct OpTypeInfo {
    PatternType Type;
    InconsistentTypeDiagFn PrintTypeSrcNote = []() {};
  };

  StringMap<OpTypeInfo> Types;

  SmallVector<InstructionPattern *, 16> Pats;
};

//===- PatFrag ------------------------------------------------------------===//

/// Represents a parsed GICombinePatFrag. This can be thought of as the
/// equivalent of a CodeGenInstruction, but for PatFragPatterns.
///
/// PatFrags are made of 3 things:
///   - Out parameters (defs)
///   - In parameters
///   - A set of pattern lists (alternatives).
///
/// If the PatFrag uses instruction patterns, the root must be one of the defs.
///
/// Note that this DOES NOT represent the use of the PatFrag, only its
/// definition. The use of the PatFrag in a Pattern is represented by
/// PatFragPattern.
///
/// PatFrags use the term "parameter" instead of operand because they're
/// essentially macros, and using that name avoids confusion. Other than that,
/// they're structured similarly to a MachineInstruction  - all parameters
/// (operands) are in the same list, with defs at the start. This helps mapping
/// parameters to values, because, param N of a PatFrag is always operand N of a
/// PatFragPattern.
class PatFrag {
public:
  static constexpr StringLiteral ClassName = "GICombinePatFrag";

  enum ParamKind {
    PK_Root,
    PK_MachineOperand,
    PK_Imm,
  };

  struct Param {
    StringRef Name;
    ParamKind Kind;
  };

  using ParamVec = SmallVector<Param, 4>;
  using ParamIt = ParamVec::const_iterator;

  /// Represents an alternative of the PatFrag. When parsing a GICombinePatFrag,
  /// this is created from its "Alternatives" list. Each alternative is a list
  /// of patterns written wrapped in a  `(pattern ...)` dag init.
  ///
  /// Each argument to the `pattern` DAG operator is parsed into a Pattern
  /// instance.
  struct Alternative {
    OperandTable OpTable;
    SmallVector<std::unique_ptr<Pattern>, 4> Pats;
  };

  explicit PatFrag(const Record &Def);

  static StringRef getParamKindStr(ParamKind OK);

  StringRef getName() const;

  const Record &getDef() const { return Def; }
  ArrayRef<SMLoc> getLoc() const;

  Alternative &addAlternative() { return Alts.emplace_back(); }
  const Alternative &getAlternative(unsigned K) const { return Alts[K]; }
  unsigned num_alternatives() const { return Alts.size(); }

  void addInParam(StringRef Name, ParamKind Kind);
  iterator_range<ParamIt> in_params() const;
  unsigned num_in_params() const { return Params.size() - NumOutParams; }

  void addOutParam(StringRef Name, ParamKind Kind);
  iterator_range<ParamIt> out_params() const;
  unsigned num_out_params() const { return NumOutParams; }

  unsigned num_roots() const;
  unsigned num_params() const { return num_in_params() + num_out_params(); }

  /// Finds the operand \p Name and returns its index or -1 if not found.
  /// Remember that all params are part of the same list, with out params at the
  /// start. This means that the index returned can be used to access operands
  /// of InstructionPatterns.
  unsigned getParamIdx(StringRef Name) const;
  const Param &getParam(unsigned K) const { return Params[K]; }

  bool canBeMatchRoot() const { return num_roots() == 1; }

  void print(raw_ostream &OS, StringRef Indent = "") const;
  void dump() const;

  /// Checks if the in-param \p ParamName can be unbound or not.
  /// \p ArgName is the name of the argument passed to the PatFrag.
  ///
  /// An argument can be unbound only if, for all alternatives:
  ///   - There is no CXX pattern, OR:
  ///   - There is an InstructionPattern that binds the parameter.
  ///
  /// e.g. in (MyPatFrag $foo), if $foo has never been seen before (= it's
  /// unbound), this checks if MyPatFrag supports it or not.
  bool handleUnboundInParam(StringRef ParamName, StringRef ArgName,
                            ArrayRef<SMLoc> DiagLoc) const;

  bool checkSemantics();
  bool buildOperandsTables();

private:
  static void printParamsList(raw_ostream &OS, iterator_range<ParamIt> Params);

  void PrintError(Twine Msg) const;

  const Record &Def;
  unsigned NumOutParams = 0;
  ParamVec Params;
  SmallVector<Alternative, 2> Alts;
};

//===- PatFragPattern -----------------------------------------------------===//

/// Represents a use of a GICombinePatFrag.
class PatFragPattern : public InstructionPattern {
public:
  PatFragPattern(const PatFrag &PF, StringRef Name)
      : InstructionPattern(K_PatFrag, Name), PF(PF) {}

  static bool classof(const Pattern *P) { return P->getKind() == K_PatFrag; }

  const PatFrag &getPatFrag() const { return PF; }
  StringRef getInstName() const override { return PF.getName(); }

  unsigned getNumInstDefs() const override { return PF.num_out_params(); }
  unsigned getNumInstOperands() const override { return PF.num_params(); }

  ArrayRef<InstructionOperand> getApplyDefsNeeded() const override;

  bool checkSemantics(ArrayRef<SMLoc> DiagLoc) override;

  /// Before emitting the patterns inside the PatFrag, add all necessary code
  /// expansions to \p PatFragCEs imported from \p ParentCEs.
  ///
  /// For a MachineOperand PatFrag parameter, this will fetch the expansion for
  /// that operand from \p ParentCEs and add it to \p PatFragCEs. Errors can be
  /// emitted if the MachineOperand reference is unbound.
  ///
  /// For an Immediate PatFrag parameter this simply adds the integer value to
  /// \p PatFragCEs as an expansion.
  ///
  /// \param ParentCEs Contains all of the code expansions declared by the other
  ///                  patterns emitted so far in the pattern list containing
  ///                  this PatFragPattern.
  /// \param PatFragCEs Output Code Expansions (usually empty)
  /// \param DiagLoc    Diagnostic loc in case an error occurs.
  /// \return `true` on success, `false` on failure.
  bool mapInputCodeExpansions(const CodeExpansions &ParentCEs,
                              CodeExpansions &PatFragCEs,
                              ArrayRef<SMLoc> DiagLoc) const;

private:
  const PatFrag &PF;
};

//===- BuiltinPattern -----------------------------------------------------===//

/// Represents builtin instructions such as "GIReplaceReg" and "GIEraseRoot".
enum BuiltinKind {
  BI_ReplaceReg,
  BI_EraseRoot,
};

class BuiltinPattern : public InstructionPattern {
  struct BuiltinInfo {
    StringLiteral DefName;
    BuiltinKind Kind;
    unsigned NumOps;
    unsigned NumDefs;
  };

  static constexpr std::array<BuiltinInfo, 2> KnownBuiltins = {{
      {"GIReplaceReg", BI_ReplaceReg, 2, 1},
      {"GIEraseRoot", BI_EraseRoot, 0, 0},
  }};

public:
  static constexpr StringLiteral ClassName = "GIBuiltinInst";

  BuiltinPattern(const Record &Def, StringRef Name)
      : InstructionPattern(K_Builtin, Name), I(getBuiltinInfo(Def)) {}

  static bool classof(const Pattern *P) { return P->getKind() == K_Builtin; }

  unsigned getNumInstOperands() const override { return I.NumOps; }
  unsigned getNumInstDefs() const override { return I.NumDefs; }
  StringRef getInstName() const override { return I.DefName; }
  BuiltinKind getBuiltinKind() const { return I.Kind; }

  bool checkSemantics(ArrayRef<SMLoc> Loc) override;

private:
  static BuiltinInfo getBuiltinInfo(const Record &Def);

  BuiltinInfo I;
};

} // namespace gi
} // end namespace llvm

#endif // ifndef LLVM_UTILS_GLOBALISEL_PATTERNS_H
