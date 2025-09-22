//===- AsmMatcherEmitter.cpp - Generate an assembly matcher ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits a target specifier matcher for converting parsed
// assembly operands in the MCInst structures. It also emits a matcher for
// custom operand parsing.
//
// Converting assembly operands into MCInst structures
// ---------------------------------------------------
//
// The input to the target specific matcher is a list of literal tokens and
// operands. The target specific parser should generally eliminate any syntax
// which is not relevant for matching; for example, comma tokens should have
// already been consumed and eliminated by the parser. Most instructions will
// end up with a single literal token (the instruction name) and some number of
// operands.
//
// Some example inputs, for X86:
//   'addl' (immediate ...) (register ...)
//   'add' (immediate ...) (memory ...)
//   'call' '*' %epc
//
// The assembly matcher is responsible for converting this input into a precise
// machine instruction (i.e., an instruction with a well defined encoding). This
// mapping has several properties which complicate matching:
//
//  - It may be ambiguous; many architectures can legally encode particular
//    variants of an instruction in different ways (for example, using a smaller
//    encoding for small immediates). Such ambiguities should never be
//    arbitrarily resolved by the assembler, the assembler is always responsible
//    for choosing the "best" available instruction.
//
//  - It may depend on the subtarget or the assembler context. Instructions
//    which are invalid for the current mode, but otherwise unambiguous (e.g.,
//    an SSE instruction in a file being assembled for i486) should be accepted
//    and rejected by the assembler front end. However, if the proper encoding
//    for an instruction is dependent on the assembler context then the matcher
//    is responsible for selecting the correct machine instruction for the
//    current mode.
//
// The core matching algorithm attempts to exploit the regularity in most
// instruction sets to quickly determine the set of possibly matching
// instructions, and the simplify the generated code. Additionally, this helps
// to ensure that the ambiguities are intentionally resolved by the user.
//
// The matching is divided into two distinct phases:
//
//   1. Classification: Each operand is mapped to the unique set which (a)
//      contains it, and (b) is the largest such subset for which a single
//      instruction could match all members.
//
//      For register classes, we can generate these subgroups automatically. For
//      arbitrary operands, we expect the user to define the classes and their
//      relations to one another (for example, 8-bit signed immediates as a
//      subset of 32-bit immediates).
//
//      By partitioning the operands in this way, we guarantee that for any
//      tuple of classes, any single instruction must match either all or none
//      of the sets of operands which could classify to that tuple.
//
//      In addition, the subset relation amongst classes induces a partial order
//      on such tuples, which we use to resolve ambiguities.
//
//   2. The input can now be treated as a tuple of classes (static tokens are
//      simple singleton sets). Each such tuple should generally map to a single
//      instruction (we currently ignore cases where this isn't true, whee!!!),
//      which we can emit a simple matcher for.
//
// Custom Operand Parsing
// ----------------------
//
//  Some targets need a custom way to parse operands, some specific instructions
//  can contain arguments that can represent processor flags and other kinds of
//  identifiers that need to be mapped to specific values in the final encoded
//  instructions. The target specific custom operand parsing works in the
//  following way:
//
//   1. A operand match table is built, each entry contains a mnemonic, an
//      operand class, a mask for all operand positions for that same
//      class/mnemonic and target features to be checked while trying to match.
//
//   2. The operand matcher will try every possible entry with the same
//      mnemonic and will check if the target feature for this mnemonic also
//      matches. After that, if the operand to be matched has its index
//      present in the mask, a successful match occurs. Otherwise, fallback
//      to the regular operand parsing.
//
//   3. For a match success, each operand class that has a 'ParserMethod'
//      becomes part of a switch from where the custom method is called.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenInstAlias.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenRegisters.h"
#include "Common/CodeGenTarget.h"
#include "Common/SubtargetFeatureInfo.h"
#include "Common/Types.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/StringToOffsetTable.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cassert>
#include <cctype>
#include <forward_list>
#include <map>
#include <set>

using namespace llvm;

#define DEBUG_TYPE "asm-matcher-emitter"

cl::OptionCategory AsmMatcherEmitterCat("Options for -gen-asm-matcher");

static cl::opt<std::string>
    MatchPrefix("match-prefix", cl::init(""),
                cl::desc("Only match instructions with the given prefix"),
                cl::cat(AsmMatcherEmitterCat));

namespace {
class AsmMatcherInfo;

// Register sets are used as keys in some second-order sets TableGen creates
// when generating its data structures. This means that the order of two
// RegisterSets can be seen in the outputted AsmMatcher tables occasionally, and
// can even affect compiler output (at least seen in diagnostics produced when
// all matches fail). So we use a type that sorts them consistently.
typedef std::set<Record *, LessRecordByID> RegisterSet;

class AsmMatcherEmitter {
  RecordKeeper &Records;

public:
  AsmMatcherEmitter(RecordKeeper &R) : Records(R) {}

  void run(raw_ostream &o);
};

/// ClassInfo - Helper class for storing the information about a particular
/// class of operands which can be matched.
struct ClassInfo {
  enum ClassInfoKind {
    /// Invalid kind, for use as a sentinel value.
    Invalid = 0,

    /// The class for a particular token.
    Token,

    /// The (first) register class, subsequent register classes are
    /// RegisterClass0+1, and so on.
    RegisterClass0,

    /// The (first) user defined class, subsequent user defined classes are
    /// UserClass0+1, and so on.
    UserClass0 = 1 << 16
  };

  /// Kind - The class kind, which is either a predefined kind, or (UserClass0 +
  /// N) for the Nth user defined class.
  unsigned Kind;

  /// SuperClasses - The super classes of this class. Note that for simplicities
  /// sake user operands only record their immediate super class, while register
  /// operands include all superclasses.
  std::vector<ClassInfo *> SuperClasses;

  /// Name - The full class name, suitable for use in an enum.
  std::string Name;

  /// ClassName - The unadorned generic name for this class (e.g., Token).
  std::string ClassName;

  /// ValueName - The name of the value this class represents; for a token this
  /// is the literal token string, for an operand it is the TableGen class (or
  /// empty if this is a derived class).
  std::string ValueName;

  /// PredicateMethod - The name of the operand method to test whether the
  /// operand matches this class; this is not valid for Token or register kinds.
  std::string PredicateMethod;

  /// RenderMethod - The name of the operand method to add this operand to an
  /// MCInst; this is not valid for Token or register kinds.
  std::string RenderMethod;

  /// ParserMethod - The name of the operand method to do a target specific
  /// parsing on the operand.
  std::string ParserMethod;

  /// For register classes: the records for all the registers in this class.
  RegisterSet Registers;

  /// For custom match classes: the diagnostic kind for when the predicate
  /// fails.
  std::string DiagnosticType;

  /// For custom match classes: the diagnostic string for when the predicate
  /// fails.
  std::string DiagnosticString;

  /// Is this operand optional and not always required.
  bool IsOptional;

  /// DefaultMethod - The name of the method that returns the default operand
  /// for optional operand
  std::string DefaultMethod;

public:
  /// isRegisterClass() - Check if this is a register class.
  bool isRegisterClass() const {
    return Kind >= RegisterClass0 && Kind < UserClass0;
  }

  /// isUserClass() - Check if this is a user defined class.
  bool isUserClass() const { return Kind >= UserClass0; }

  /// isRelatedTo - Check whether this class is "related" to \p RHS. Classes
  /// are related if they are in the same class hierarchy.
  bool isRelatedTo(const ClassInfo &RHS) const {
    // Tokens are only related to tokens.
    if (Kind == Token || RHS.Kind == Token)
      return Kind == Token && RHS.Kind == Token;

    // Registers classes are only related to registers classes, and only if
    // their intersection is non-empty.
    if (isRegisterClass() || RHS.isRegisterClass()) {
      if (!isRegisterClass() || !RHS.isRegisterClass())
        return false;

      std::vector<Record *> Tmp;
      std::set_intersection(Registers.begin(), Registers.end(),
                            RHS.Registers.begin(), RHS.Registers.end(),
                            std::back_inserter(Tmp), LessRecordByID());

      return !Tmp.empty();
    }

    // Otherwise we have two users operands; they are related if they are in the
    // same class hierarchy.
    //
    // FIXME: This is an oversimplification, they should only be related if they
    // intersect, however we don't have that information.
    assert(isUserClass() && RHS.isUserClass() && "Unexpected class!");
    const ClassInfo *Root = this;
    while (!Root->SuperClasses.empty())
      Root = Root->SuperClasses.front();

    const ClassInfo *RHSRoot = &RHS;
    while (!RHSRoot->SuperClasses.empty())
      RHSRoot = RHSRoot->SuperClasses.front();

    return Root == RHSRoot;
  }

  /// isSubsetOf - Test whether this class is a subset of \p RHS.
  bool isSubsetOf(const ClassInfo &RHS) const {
    // This is a subset of RHS if it is the same class...
    if (this == &RHS)
      return true;

    // ... or if any of its super classes are a subset of RHS.
    SmallVector<const ClassInfo *, 16> Worklist(SuperClasses.begin(),
                                                SuperClasses.end());
    SmallPtrSet<const ClassInfo *, 16> Visited;
    while (!Worklist.empty()) {
      auto *CI = Worklist.pop_back_val();
      if (CI == &RHS)
        return true;
      for (auto *Super : CI->SuperClasses)
        if (Visited.insert(Super).second)
          Worklist.push_back(Super);
    }

    return false;
  }

  int getTreeDepth() const {
    int Depth = 0;
    const ClassInfo *Root = this;
    while (!Root->SuperClasses.empty()) {
      Depth++;
      Root = Root->SuperClasses.front();
    }
    return Depth;
  }

  const ClassInfo *findRoot() const {
    const ClassInfo *Root = this;
    while (!Root->SuperClasses.empty())
      Root = Root->SuperClasses.front();
    return Root;
  }

  /// Compare two classes. This does not produce a total ordering, but does
  /// guarantee that subclasses are sorted before their parents, and that the
  /// ordering is transitive.
  bool operator<(const ClassInfo &RHS) const {
    if (this == &RHS)
      return false;

    // First, enforce the ordering between the three different types of class.
    // Tokens sort before registers, which sort before user classes.
    if (Kind == Token) {
      if (RHS.Kind != Token)
        return true;
      assert(RHS.Kind == Token);
    } else if (isRegisterClass()) {
      if (RHS.Kind == Token)
        return false;
      else if (RHS.isUserClass())
        return true;
      assert(RHS.isRegisterClass());
    } else if (isUserClass()) {
      if (!RHS.isUserClass())
        return false;
      assert(RHS.isUserClass());
    } else {
      llvm_unreachable("Unknown ClassInfoKind");
    }

    if (Kind == Token || isUserClass()) {
      // Related tokens and user classes get sorted by depth in the inheritence
      // tree (so that subclasses are before their parents).
      if (isRelatedTo(RHS)) {
        if (getTreeDepth() > RHS.getTreeDepth())
          return true;
        if (getTreeDepth() < RHS.getTreeDepth())
          return false;
      } else {
        // Unrelated tokens and user classes are ordered by the name of their
        // root nodes, so that there is a consistent ordering between
        // unconnected trees.
        return findRoot()->ValueName < RHS.findRoot()->ValueName;
      }
    } else if (isRegisterClass()) {
      // For register sets, sort by number of registers. This guarantees that
      // a set will always sort before all of it's strict supersets.
      if (Registers.size() != RHS.Registers.size())
        return Registers.size() < RHS.Registers.size();
    } else {
      llvm_unreachable("Unknown ClassInfoKind");
    }

    // FIXME: We should be able to just return false here, as we only need a
    // partial order (we use stable sorts, so this is deterministic) and the
    // name of a class shouldn't be significant. However, some of the backends
    // accidentally rely on this behaviour, so it will have to stay like this
    // until they are fixed.
    return ValueName < RHS.ValueName;
  }
};

class AsmVariantInfo {
public:
  StringRef RegisterPrefix;
  StringRef TokenizingCharacters;
  StringRef SeparatorCharacters;
  StringRef BreakCharacters;
  StringRef Name;
  int AsmVariantNo;
};

bool getPreferSmallerInstructions(CodeGenTarget const &Target) {
  return Target.getAsmParser()->getValueAsBit("PreferSmallerInstructions");
}

/// MatchableInfo - Helper class for storing the necessary information for an
/// instruction or alias which is capable of being matched.
struct MatchableInfo {
  struct AsmOperand {
    /// Token - This is the token that the operand came from.
    StringRef Token;

    /// The unique class instance this operand should match.
    ClassInfo *Class;

    /// The operand name this is, if anything.
    StringRef SrcOpName;

    /// The operand name this is, before renaming for tied operands.
    StringRef OrigSrcOpName;

    /// The suboperand index within SrcOpName, or -1 for the entire operand.
    int SubOpIdx;

    /// Whether the token is "isolated", i.e., it is preceded and followed
    /// by separators.
    bool IsIsolatedToken;

    /// Register record if this token is singleton register.
    Record *SingletonReg;

    explicit AsmOperand(bool IsIsolatedToken, StringRef T)
        : Token(T), Class(nullptr), SubOpIdx(-1),
          IsIsolatedToken(IsIsolatedToken), SingletonReg(nullptr) {}
  };

  /// ResOperand - This represents a single operand in the result instruction
  /// generated by the match.  In cases (like addressing modes) where a single
  /// assembler operand expands to multiple MCOperands, this represents the
  /// single assembler operand, not the MCOperand.
  struct ResOperand {
    enum {
      /// RenderAsmOperand - This represents an operand result that is
      /// generated by calling the render method on the assembly operand.  The
      /// corresponding AsmOperand is specified by AsmOperandNum.
      RenderAsmOperand,

      /// TiedOperand - This represents a result operand that is a duplicate of
      /// a previous result operand.
      TiedOperand,

      /// ImmOperand - This represents an immediate value that is dumped into
      /// the operand.
      ImmOperand,

      /// RegOperand - This represents a fixed register that is dumped in.
      RegOperand
    } Kind;

    /// Tuple containing the index of the (earlier) result operand that should
    /// be copied from, as well as the indices of the corresponding (parsed)
    /// operands in the asm string.
    struct TiedOperandsTuple {
      unsigned ResOpnd;
      unsigned SrcOpnd1Idx;
      unsigned SrcOpnd2Idx;
    };

    union {
      /// This is the operand # in the AsmOperands list that this should be
      /// copied from.
      unsigned AsmOperandNum;

      /// Description of tied operands.
      TiedOperandsTuple TiedOperands;

      /// ImmVal - This is the immediate value added to the instruction.
      int64_t ImmVal;

      /// Register - This is the register record.
      Record *Register;
    };

    /// MINumOperands - The number of MCInst operands populated by this
    /// operand.
    unsigned MINumOperands;

    static ResOperand getRenderedOp(unsigned AsmOpNum, unsigned NumOperands) {
      ResOperand X;
      X.Kind = RenderAsmOperand;
      X.AsmOperandNum = AsmOpNum;
      X.MINumOperands = NumOperands;
      return X;
    }

    static ResOperand getTiedOp(unsigned TiedOperandNum, unsigned SrcOperand1,
                                unsigned SrcOperand2) {
      ResOperand X;
      X.Kind = TiedOperand;
      X.TiedOperands = {TiedOperandNum, SrcOperand1, SrcOperand2};
      X.MINumOperands = 1;
      return X;
    }

    static ResOperand getImmOp(int64_t Val) {
      ResOperand X;
      X.Kind = ImmOperand;
      X.ImmVal = Val;
      X.MINumOperands = 1;
      return X;
    }

    static ResOperand getRegOp(Record *Reg) {
      ResOperand X;
      X.Kind = RegOperand;
      X.Register = Reg;
      X.MINumOperands = 1;
      return X;
    }
  };

  /// AsmVariantID - Target's assembly syntax variant no.
  int AsmVariantID;

  /// AsmString - The assembly string for this instruction (with variants
  /// removed), e.g. "movsx $src, $dst".
  std::string AsmString;

  /// TheDef - This is the definition of the instruction or InstAlias that this
  /// matchable came from.
  Record *const TheDef;

  // ResInstSize - The size of the resulting instruction for this matchable.
  unsigned ResInstSize;

  /// DefRec - This is the definition that it came from.
  PointerUnion<const CodeGenInstruction *, const CodeGenInstAlias *> DefRec;

  const CodeGenInstruction *getResultInst() const {
    if (isa<const CodeGenInstruction *>(DefRec))
      return cast<const CodeGenInstruction *>(DefRec);
    return cast<const CodeGenInstAlias *>(DefRec)->ResultInst;
  }

  /// ResOperands - This is the operand list that should be built for the result
  /// MCInst.
  SmallVector<ResOperand, 8> ResOperands;

  /// Mnemonic - This is the first token of the matched instruction, its
  /// mnemonic.
  StringRef Mnemonic;

  /// AsmOperands - The textual operands that this instruction matches,
  /// annotated with a class and where in the OperandList they were defined.
  /// This directly corresponds to the tokenized AsmString after the mnemonic is
  /// removed.
  SmallVector<AsmOperand, 8> AsmOperands;

  /// Predicates - The required subtarget features to match this instruction.
  SmallVector<const SubtargetFeatureInfo *, 4> RequiredFeatures;

  /// ConversionFnKind - The enum value which is passed to the generated
  /// convertToMCInst to convert parsed operands into an MCInst for this
  /// function.
  std::string ConversionFnKind;

  /// If this instruction is deprecated in some form.
  bool HasDeprecation = false;

  /// If this is an alias, this is use to determine whether or not to using
  /// the conversion function defined by the instruction's AsmMatchConverter
  /// or to use the function generated by the alias.
  bool UseInstAsmMatchConverter;

  MatchableInfo(const CodeGenInstruction &CGI)
      : AsmVariantID(0), AsmString(CGI.AsmString), TheDef(CGI.TheDef),
        ResInstSize(TheDef->getValueAsInt("Size")), DefRec(&CGI),
        UseInstAsmMatchConverter(true) {}

  MatchableInfo(std::unique_ptr<const CodeGenInstAlias> Alias)
      : AsmVariantID(0), AsmString(Alias->AsmString), TheDef(Alias->TheDef),
        ResInstSize(Alias->ResultInst->TheDef->getValueAsInt("Size")),
        DefRec(Alias.release()), UseInstAsmMatchConverter(TheDef->getValueAsBit(
                                     "UseInstAsmMatchConverter")) {}

  // Could remove this and the dtor if PointerUnion supported unique_ptr
  // elements with a dynamic failure/assertion (like the one below) in the case
  // where it was copied while being in an owning state.
  MatchableInfo(const MatchableInfo &RHS)
      : AsmVariantID(RHS.AsmVariantID), AsmString(RHS.AsmString),
        TheDef(RHS.TheDef), ResInstSize(RHS.ResInstSize), DefRec(RHS.DefRec),
        ResOperands(RHS.ResOperands), Mnemonic(RHS.Mnemonic),
        AsmOperands(RHS.AsmOperands), RequiredFeatures(RHS.RequiredFeatures),
        ConversionFnKind(RHS.ConversionFnKind),
        HasDeprecation(RHS.HasDeprecation),
        UseInstAsmMatchConverter(RHS.UseInstAsmMatchConverter) {
    assert(!isa<const CodeGenInstAlias *>(DefRec));
  }

  ~MatchableInfo() {
    delete dyn_cast_if_present<const CodeGenInstAlias *>(DefRec);
  }

  // Two-operand aliases clone from the main matchable, but mark the second
  // operand as a tied operand of the first for purposes of the assembler.
  void formTwoOperandAlias(StringRef Constraint);

  void initialize(const AsmMatcherInfo &Info,
                  SmallPtrSetImpl<Record *> &SingletonRegisters,
                  AsmVariantInfo const &Variant, bool HasMnemonicFirst);

  /// validate - Return true if this matchable is a valid thing to match against
  /// and perform a bunch of validity checking.
  bool validate(StringRef CommentDelimiter, bool IsAlias) const;

  /// findAsmOperand - Find the AsmOperand with the specified name and
  /// suboperand index.
  int findAsmOperand(StringRef N, int SubOpIdx) const {
    auto I = find_if(AsmOperands, [&](const AsmOperand &Op) {
      return Op.SrcOpName == N && Op.SubOpIdx == SubOpIdx;
    });
    return (I != AsmOperands.end()) ? I - AsmOperands.begin() : -1;
  }

  /// findAsmOperandNamed - Find the first AsmOperand with the specified name.
  /// This does not check the suboperand index.
  int findAsmOperandNamed(StringRef N, int LastIdx = -1) const {
    auto I =
        llvm::find_if(llvm::drop_begin(AsmOperands, LastIdx + 1),
                      [&](const AsmOperand &Op) { return Op.SrcOpName == N; });
    return (I != AsmOperands.end()) ? I - AsmOperands.begin() : -1;
  }

  int findAsmOperandOriginallyNamed(StringRef N) const {
    auto I = find_if(AsmOperands, [&](const AsmOperand &Op) {
      return Op.OrigSrcOpName == N;
    });
    return (I != AsmOperands.end()) ? I - AsmOperands.begin() : -1;
  }

  void buildInstructionResultOperands();
  void buildAliasResultOperands(bool AliasConstraintsAreChecked);

  /// shouldBeMatchedBefore - Compare two matchables for ordering.
  bool shouldBeMatchedBefore(const MatchableInfo &RHS,
                             bool PreferSmallerInstructions) const {
    // The primary comparator is the instruction mnemonic.
    if (int Cmp = Mnemonic.compare_insensitive(RHS.Mnemonic))
      return Cmp == -1;

    // (Optionally) Order by the resultant instuctions size.
    // eg. for ARM thumb instructions smaller encodings should be preferred.
    if (PreferSmallerInstructions && ResInstSize != RHS.ResInstSize)
      return ResInstSize < RHS.ResInstSize;

    if (AsmOperands.size() != RHS.AsmOperands.size())
      return AsmOperands.size() < RHS.AsmOperands.size();

    // Compare lexicographically by operand. The matcher validates that other
    // orderings wouldn't be ambiguous using \see couldMatchAmbiguouslyWith().
    for (unsigned i = 0, e = AsmOperands.size(); i != e; ++i) {
      if (*AsmOperands[i].Class < *RHS.AsmOperands[i].Class)
        return true;
      if (*RHS.AsmOperands[i].Class < *AsmOperands[i].Class)
        return false;
    }

    // For X86 AVX/AVX512 instructions, we prefer vex encoding because the
    // vex encoding size is smaller. Since X86InstrSSE.td is included ahead
    // of X86InstrAVX512.td, the AVX instruction ID is less than AVX512 ID.
    // We use the ID to sort AVX instruction before AVX512 instruction in
    // matching table. As well as InstAlias.
    if (getResultInst()->TheDef->isSubClassOf("Instruction") &&
        getResultInst()->TheDef->getValueAsBit("HasPositionOrder") &&
        RHS.getResultInst()->TheDef->isSubClassOf("Instruction") &&
        RHS.getResultInst()->TheDef->getValueAsBit("HasPositionOrder"))
      return getResultInst()->TheDef->getID() <
             RHS.getResultInst()->TheDef->getID();

    // Give matches that require more features higher precedence. This is useful
    // because we cannot define AssemblerPredicates with the negation of
    // processor features. For example, ARM v6 "nop" may be either a HINT or
    // MOV. With v6, we want to match HINT. The assembler has no way to
    // predicate MOV under "NoV6", but HINT will always match first because it
    // requires V6 while MOV does not.
    if (RequiredFeatures.size() != RHS.RequiredFeatures.size())
      return RequiredFeatures.size() > RHS.RequiredFeatures.size();

    return false;
  }

  /// couldMatchAmbiguouslyWith - Check whether this matchable could
  /// ambiguously match the same set of operands as \p RHS (without being a
  /// strictly superior match).
  bool couldMatchAmbiguouslyWith(const MatchableInfo &RHS,
                                 bool PreferSmallerInstructions) const {
    // The primary comparator is the instruction mnemonic.
    if (Mnemonic != RHS.Mnemonic)
      return false;

    // Different variants can't conflict.
    if (AsmVariantID != RHS.AsmVariantID)
      return false;

    // The size of instruction is unambiguous.
    if (PreferSmallerInstructions && ResInstSize != RHS.ResInstSize)
      return false;

    // The number of operands is unambiguous.
    if (AsmOperands.size() != RHS.AsmOperands.size())
      return false;

    // Otherwise, make sure the ordering of the two instructions is unambiguous
    // by checking that either (a) a token or operand kind discriminates them,
    // or (b) the ordering among equivalent kinds is consistent.

    // Tokens and operand kinds are unambiguous (assuming a correct target
    // specific parser).
    for (unsigned i = 0, e = AsmOperands.size(); i != e; ++i)
      if (AsmOperands[i].Class->Kind != RHS.AsmOperands[i].Class->Kind ||
          AsmOperands[i].Class->Kind == ClassInfo::Token)
        if (*AsmOperands[i].Class < *RHS.AsmOperands[i].Class ||
            *RHS.AsmOperands[i].Class < *AsmOperands[i].Class)
          return false;

    // Otherwise, this operand could commute if all operands are equivalent, or
    // there is a pair of operands that compare less than and a pair that
    // compare greater than.
    bool HasLT = false, HasGT = false;
    for (unsigned i = 0, e = AsmOperands.size(); i != e; ++i) {
      if (*AsmOperands[i].Class < *RHS.AsmOperands[i].Class)
        HasLT = true;
      if (*RHS.AsmOperands[i].Class < *AsmOperands[i].Class)
        HasGT = true;
    }

    return HasLT == HasGT;
  }

  void dump() const;

private:
  void tokenizeAsmString(AsmMatcherInfo const &Info,
                         AsmVariantInfo const &Variant);
  void addAsmOperand(StringRef Token, bool IsIsolatedToken = false);
};

struct OperandMatchEntry {
  unsigned OperandMask;
  const MatchableInfo *MI;
  ClassInfo *CI;

  static OperandMatchEntry create(const MatchableInfo *mi, ClassInfo *ci,
                                  unsigned opMask) {
    OperandMatchEntry X;
    X.OperandMask = opMask;
    X.CI = ci;
    X.MI = mi;
    return X;
  }
};

class AsmMatcherInfo {
public:
  /// Tracked Records
  RecordKeeper &Records;

  /// The tablegen AsmParser record.
  Record *AsmParser;

  /// Target - The target information.
  CodeGenTarget &Target;

  /// The classes which are needed for matching.
  std::forward_list<ClassInfo> Classes;

  /// The information on the matchables to match.
  std::vector<std::unique_ptr<MatchableInfo>> Matchables;

  /// Info for custom matching operands by user defined methods.
  std::vector<OperandMatchEntry> OperandMatchInfo;

  /// Map of Register records to their class information.
  typedef std::map<Record *, ClassInfo *, LessRecordByID> RegisterClassesTy;
  RegisterClassesTy RegisterClasses;

  /// Map of Predicate records to their subtarget information.
  std::map<Record *, SubtargetFeatureInfo, LessRecordByID> SubtargetFeatures;

  /// Map of AsmOperandClass records to their class information.
  std::map<Record *, ClassInfo *> AsmOperandClasses;

  /// Map of RegisterClass records to their class information.
  std::map<Record *, ClassInfo *> RegisterClassClasses;

private:
  /// Map of token to class information which has already been constructed.
  std::map<std::string, ClassInfo *> TokenClasses;

private:
  /// getTokenClass - Lookup or create the class for the given token.
  ClassInfo *getTokenClass(StringRef Token);

  /// getOperandClass - Lookup or create the class for the given operand.
  ClassInfo *getOperandClass(const CGIOperandList::OperandInfo &OI,
                             int SubOpIdx);
  ClassInfo *getOperandClass(Record *Rec, int SubOpIdx);

  /// buildRegisterClasses - Build the ClassInfo* instances for register
  /// classes.
  void buildRegisterClasses(SmallPtrSetImpl<Record *> &SingletonRegisters);

  /// buildOperandClasses - Build the ClassInfo* instances for user defined
  /// operand classes.
  void buildOperandClasses();

  void buildInstructionOperandReference(MatchableInfo *II, StringRef OpName,
                                        unsigned AsmOpIdx);
  void buildAliasOperandReference(MatchableInfo *II, StringRef OpName,
                                  MatchableInfo::AsmOperand &Op);

public:
  AsmMatcherInfo(Record *AsmParser, CodeGenTarget &Target,
                 RecordKeeper &Records);

  /// Construct the various tables used during matching.
  void buildInfo();

  /// buildOperandMatchInfo - Build the necessary information to handle user
  /// defined operand parsing methods.
  void buildOperandMatchInfo();

  /// getSubtargetFeature - Lookup or create the subtarget feature info for the
  /// given operand.
  const SubtargetFeatureInfo *getSubtargetFeature(Record *Def) const {
    assert(Def->isSubClassOf("Predicate") && "Invalid predicate type!");
    const auto &I = SubtargetFeatures.find(Def);
    return I == SubtargetFeatures.end() ? nullptr : &I->second;
  }

  RecordKeeper &getRecords() const { return Records; }

  bool hasOptionalOperands() const {
    return any_of(Classes,
                  [](const ClassInfo &Class) { return Class.IsOptional; });
  }
};

} // end anonymous namespace

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MatchableInfo::dump() const {
  errs() << TheDef->getName() << " -- "
         << "flattened:\"" << AsmString << "\"\n";

  errs() << "  variant: " << AsmVariantID << "\n";

  for (unsigned i = 0, e = AsmOperands.size(); i != e; ++i) {
    const AsmOperand &Op = AsmOperands[i];
    errs() << "  op[" << i << "] = " << Op.Class->ClassName << " - ";
    errs() << '\"' << Op.Token << "\"\n";
  }
}
#endif

static std::pair<StringRef, StringRef>
parseTwoOperandConstraint(StringRef S, ArrayRef<SMLoc> Loc) {
  // Split via the '='.
  std::pair<StringRef, StringRef> Ops = S.split('=');
  if (Ops.second == "")
    PrintFatalError(Loc, "missing '=' in two-operand alias constraint");
  // Trim whitespace and the leading '$' on the operand names.
  size_t start = Ops.first.find_first_of('$');
  if (start == std::string::npos)
    PrintFatalError(Loc, "expected '$' prefix on asm operand name");
  Ops.first = Ops.first.slice(start + 1, std::string::npos);
  size_t end = Ops.first.find_last_of(" \t");
  Ops.first = Ops.first.slice(0, end);
  // Now the second operand.
  start = Ops.second.find_first_of('$');
  if (start == std::string::npos)
    PrintFatalError(Loc, "expected '$' prefix on asm operand name");
  Ops.second = Ops.second.slice(start + 1, std::string::npos);
  end = Ops.second.find_last_of(" \t");
  Ops.first = Ops.first.slice(0, end);
  return Ops;
}

void MatchableInfo::formTwoOperandAlias(StringRef Constraint) {
  // Figure out which operands are aliased and mark them as tied.
  std::pair<StringRef, StringRef> Ops =
      parseTwoOperandConstraint(Constraint, TheDef->getLoc());

  // Find the AsmOperands that refer to the operands we're aliasing.
  int SrcAsmOperand = findAsmOperandNamed(Ops.first);
  int DstAsmOperand = findAsmOperandNamed(Ops.second);
  if (SrcAsmOperand == -1)
    PrintFatalError(TheDef->getLoc(),
                    "unknown source two-operand alias operand '" + Ops.first +
                        "'.");
  if (DstAsmOperand == -1)
    PrintFatalError(TheDef->getLoc(),
                    "unknown destination two-operand alias operand '" +
                        Ops.second + "'.");

  // Find the ResOperand that refers to the operand we're aliasing away
  // and update it to refer to the combined operand instead.
  for (ResOperand &Op : ResOperands) {
    if (Op.Kind == ResOperand::RenderAsmOperand &&
        Op.AsmOperandNum == (unsigned)SrcAsmOperand) {
      Op.AsmOperandNum = DstAsmOperand;
      break;
    }
  }
  // Remove the AsmOperand for the alias operand.
  AsmOperands.erase(AsmOperands.begin() + SrcAsmOperand);
  // Adjust the ResOperand references to any AsmOperands that followed
  // the one we just deleted.
  for (ResOperand &Op : ResOperands) {
    switch (Op.Kind) {
    default:
      // Nothing to do for operands that don't reference AsmOperands.
      break;
    case ResOperand::RenderAsmOperand:
      if (Op.AsmOperandNum > (unsigned)SrcAsmOperand)
        --Op.AsmOperandNum;
      break;
    }
  }
}

/// extractSingletonRegisterForAsmOperand - Extract singleton register,
/// if present, from specified token.
static void extractSingletonRegisterForAsmOperand(MatchableInfo::AsmOperand &Op,
                                                  const AsmMatcherInfo &Info,
                                                  StringRef RegisterPrefix) {
  StringRef Tok = Op.Token;

  // If this token is not an isolated token, i.e., it isn't separated from
  // other tokens (e.g. with whitespace), don't interpret it as a register name.
  if (!Op.IsIsolatedToken)
    return;

  if (RegisterPrefix.empty()) {
    std::string LoweredTok = Tok.lower();
    if (const CodeGenRegister *Reg = Info.Target.getRegisterByName(LoweredTok))
      Op.SingletonReg = Reg->TheDef;
    return;
  }

  if (!Tok.starts_with(RegisterPrefix))
    return;

  StringRef RegName = Tok.substr(RegisterPrefix.size());
  if (const CodeGenRegister *Reg = Info.Target.getRegisterByName(RegName))
    Op.SingletonReg = Reg->TheDef;

  // If there is no register prefix (i.e. "%" in "%eax"), then this may
  // be some random non-register token, just ignore it.
}

void MatchableInfo::initialize(const AsmMatcherInfo &Info,
                               SmallPtrSetImpl<Record *> &SingletonRegisters,
                               AsmVariantInfo const &Variant,
                               bool HasMnemonicFirst) {
  AsmVariantID = Variant.AsmVariantNo;
  AsmString = CodeGenInstruction::FlattenAsmStringVariants(
      AsmString, Variant.AsmVariantNo);

  tokenizeAsmString(Info, Variant);

  // The first token of the instruction is the mnemonic, which must be a
  // simple string, not a $foo variable or a singleton register.
  if (AsmOperands.empty())
    PrintFatalError(TheDef->getLoc(),
                    "Instruction '" + TheDef->getName() + "' has no tokens");

  assert(!AsmOperands[0].Token.empty());
  if (HasMnemonicFirst) {
    Mnemonic = AsmOperands[0].Token;
    if (Mnemonic[0] == '$')
      PrintFatalError(TheDef->getLoc(),
                      "Invalid instruction mnemonic '" + Mnemonic + "'!");

    // Remove the first operand, it is tracked in the mnemonic field.
    AsmOperands.erase(AsmOperands.begin());
  } else if (AsmOperands[0].Token[0] != '$')
    Mnemonic = AsmOperands[0].Token;

  // Compute the require features.
  for (Record *Predicate : TheDef->getValueAsListOfDefs("Predicates"))
    if (const SubtargetFeatureInfo *Feature =
            Info.getSubtargetFeature(Predicate))
      RequiredFeatures.push_back(Feature);

  // Collect singleton registers, if used.
  for (MatchableInfo::AsmOperand &Op : AsmOperands) {
    extractSingletonRegisterForAsmOperand(Op, Info, Variant.RegisterPrefix);
    if (Record *Reg = Op.SingletonReg)
      SingletonRegisters.insert(Reg);
  }

  const RecordVal *DepMask = TheDef->getValue("DeprecatedFeatureMask");
  if (!DepMask)
    DepMask = TheDef->getValue("ComplexDeprecationPredicate");

  HasDeprecation =
      DepMask ? !DepMask->getValue()->getAsUnquotedString().empty() : false;
}

/// Append an AsmOperand for the given substring of AsmString.
void MatchableInfo::addAsmOperand(StringRef Token, bool IsIsolatedToken) {
  AsmOperands.push_back(AsmOperand(IsIsolatedToken, Token));
}

/// tokenizeAsmString - Tokenize a simplified assembly string.
void MatchableInfo::tokenizeAsmString(const AsmMatcherInfo &Info,
                                      AsmVariantInfo const &Variant) {
  StringRef String = AsmString;
  size_t Prev = 0;
  bool InTok = false;
  bool IsIsolatedToken = true;
  for (size_t i = 0, e = String.size(); i != e; ++i) {
    char Char = String[i];
    if (Variant.BreakCharacters.contains(Char)) {
      if (InTok) {
        addAsmOperand(String.slice(Prev, i), false);
        Prev = i;
        IsIsolatedToken = false;
      }
      InTok = true;
      continue;
    }
    if (Variant.TokenizingCharacters.contains(Char)) {
      if (InTok) {
        addAsmOperand(String.slice(Prev, i), IsIsolatedToken);
        InTok = false;
        IsIsolatedToken = false;
      }
      addAsmOperand(String.slice(i, i + 1), IsIsolatedToken);
      Prev = i + 1;
      IsIsolatedToken = true;
      continue;
    }
    if (Variant.SeparatorCharacters.contains(Char)) {
      if (InTok) {
        addAsmOperand(String.slice(Prev, i), IsIsolatedToken);
        InTok = false;
      }
      Prev = i + 1;
      IsIsolatedToken = true;
      continue;
    }

    switch (Char) {
    case '\\':
      if (InTok) {
        addAsmOperand(String.slice(Prev, i), false);
        InTok = false;
        IsIsolatedToken = false;
      }
      ++i;
      assert(i != String.size() && "Invalid quoted character");
      addAsmOperand(String.slice(i, i + 1), IsIsolatedToken);
      Prev = i + 1;
      IsIsolatedToken = false;
      break;

    case '$': {
      if (InTok) {
        addAsmOperand(String.slice(Prev, i), IsIsolatedToken);
        InTok = false;
        IsIsolatedToken = false;
      }

      // If this isn't "${", start new identifier looking like "$xxx"
      if (i + 1 == String.size() || String[i + 1] != '{') {
        Prev = i;
        break;
      }

      size_t EndPos = String.find('}', i);
      assert(EndPos != StringRef::npos &&
             "Missing brace in operand reference!");
      addAsmOperand(String.slice(i, EndPos + 1), IsIsolatedToken);
      Prev = EndPos + 1;
      i = EndPos;
      IsIsolatedToken = false;
      break;
    }

    default:
      InTok = true;
      break;
    }
  }
  if (InTok && Prev != String.size())
    addAsmOperand(String.substr(Prev), IsIsolatedToken);
}

bool MatchableInfo::validate(StringRef CommentDelimiter, bool IsAlias) const {
  // Reject matchables with no .s string.
  if (AsmString.empty())
    PrintFatalError(TheDef->getLoc(), "instruction with empty asm string");

  // Reject any matchables with a newline in them, they should be marked
  // isCodeGenOnly if they are pseudo instructions.
  if (AsmString.find('\n') != std::string::npos)
    PrintFatalError(TheDef->getLoc(),
                    "multiline instruction is not valid for the asmparser, "
                    "mark it isCodeGenOnly");

  // Remove comments from the asm string.  We know that the asmstring only
  // has one line.
  if (!CommentDelimiter.empty() &&
      StringRef(AsmString).contains(CommentDelimiter))
    PrintFatalError(TheDef->getLoc(),
                    "asmstring for instruction has comment character in it, "
                    "mark it isCodeGenOnly");

  // Reject matchables with operand modifiers, these aren't something we can
  // handle, the target should be refactored to use operands instead of
  // modifiers.
  //
  // Also, check for instructions which reference the operand multiple times,
  // if they don't define a custom AsmMatcher: this implies a constraint that
  // the built-in matching code would not honor.
  std::set<std::string> OperandNames;
  for (const AsmOperand &Op : AsmOperands) {
    StringRef Tok = Op.Token;
    if (Tok[0] == '$' && Tok.contains(':'))
      PrintFatalError(
          TheDef->getLoc(),
          "matchable with operand modifier '" + Tok +
              "' not supported by asm matcher.  Mark isCodeGenOnly!");
    // Verify that any operand is only mentioned once.
    // We reject aliases and ignore instructions for now.
    if (!IsAlias && TheDef->getValueAsString("AsmMatchConverter").empty() &&
        Tok[0] == '$' && !OperandNames.insert(std::string(Tok)).second) {
      LLVM_DEBUG({
        errs() << "warning: '" << TheDef->getName() << "': "
               << "ignoring instruction with tied operand '" << Tok << "'\n";
      });
      return false;
    }
  }

  return true;
}

static std::string getEnumNameForToken(StringRef Str) {
  std::string Res;

  for (char C : Str) {
    switch (C) {
    case '*':
      Res += "_STAR_";
      break;
    case '%':
      Res += "_PCT_";
      break;
    case ':':
      Res += "_COLON_";
      break;
    case '!':
      Res += "_EXCLAIM_";
      break;
    case '.':
      Res += "_DOT_";
      break;
    case '<':
      Res += "_LT_";
      break;
    case '>':
      Res += "_GT_";
      break;
    case '-':
      Res += "_MINUS_";
      break;
    case '#':
      Res += "_HASH_";
      break;
    default:
      if (isAlnum(C))
        Res += C;
      else
        Res += "_" + utostr((unsigned)C) + "_";
    }
  }

  return Res;
}

ClassInfo *AsmMatcherInfo::getTokenClass(StringRef Token) {
  ClassInfo *&Entry = TokenClasses[std::string(Token)];

  if (!Entry) {
    Classes.emplace_front();
    Entry = &Classes.front();
    Entry->Kind = ClassInfo::Token;
    Entry->ClassName = "Token";
    Entry->Name = "MCK_" + getEnumNameForToken(Token);
    Entry->ValueName = std::string(Token);
    Entry->PredicateMethod = "<invalid>";
    Entry->RenderMethod = "<invalid>";
    Entry->ParserMethod = "";
    Entry->DiagnosticType = "";
    Entry->IsOptional = false;
    Entry->DefaultMethod = "<invalid>";
  }

  return Entry;
}

ClassInfo *
AsmMatcherInfo::getOperandClass(const CGIOperandList::OperandInfo &OI,
                                int SubOpIdx) {
  Record *Rec = OI.Rec;
  if (SubOpIdx != -1)
    Rec = cast<DefInit>(OI.MIOperandInfo->getArg(SubOpIdx))->getDef();
  return getOperandClass(Rec, SubOpIdx);
}

ClassInfo *AsmMatcherInfo::getOperandClass(Record *Rec, int SubOpIdx) {
  if (Rec->isSubClassOf("RegisterOperand")) {
    // RegisterOperand may have an associated ParserMatchClass. If it does,
    // use it, else just fall back to the underlying register class.
    const RecordVal *R = Rec->getValue("ParserMatchClass");
    if (!R || !R->getValue())
      PrintFatalError(Rec->getLoc(),
                      "Record `" + Rec->getName() +
                          "' does not have a ParserMatchClass!\n");

    if (DefInit *DI = dyn_cast<DefInit>(R->getValue())) {
      Record *MatchClass = DI->getDef();
      if (ClassInfo *CI = AsmOperandClasses[MatchClass])
        return CI;
    }

    // No custom match class. Just use the register class.
    Record *ClassRec = Rec->getValueAsDef("RegClass");
    if (!ClassRec)
      PrintFatalError(Rec->getLoc(),
                      "RegisterOperand `" + Rec->getName() +
                          "' has no associated register class!\n");
    if (ClassInfo *CI = RegisterClassClasses[ClassRec])
      return CI;
    PrintFatalError(Rec->getLoc(), "register class has no class info!");
  }

  if (Rec->isSubClassOf("RegisterClass")) {
    if (ClassInfo *CI = RegisterClassClasses[Rec])
      return CI;
    PrintFatalError(Rec->getLoc(), "register class has no class info!");
  }

  if (!Rec->isSubClassOf("Operand"))
    PrintFatalError(Rec->getLoc(),
                    "Operand `" + Rec->getName() +
                        "' does not derive from class Operand!\n");
  Record *MatchClass = Rec->getValueAsDef("ParserMatchClass");
  if (ClassInfo *CI = AsmOperandClasses[MatchClass])
    return CI;

  PrintFatalError(Rec->getLoc(), "operand has no match class!");
}

struct LessRegisterSet {
  bool operator()(const RegisterSet &LHS, const RegisterSet &RHS) const {
    // std::set<T> defines its own compariso "operator<", but it
    // performs a lexicographical comparison by T's innate comparison
    // for some reason. We don't want non-deterministic pointer
    // comparisons so use this instead.
    return std::lexicographical_compare(LHS.begin(), LHS.end(), RHS.begin(),
                                        RHS.end(), LessRecordByID());
  }
};

void AsmMatcherInfo::buildRegisterClasses(
    SmallPtrSetImpl<Record *> &SingletonRegisters) {
  const auto &Registers = Target.getRegBank().getRegisters();
  auto &RegClassList = Target.getRegBank().getRegClasses();

  typedef std::set<RegisterSet, LessRegisterSet> RegisterSetSet;

  // The register sets used for matching.
  RegisterSetSet RegisterSets;

  // Gather the defined sets.
  for (const CodeGenRegisterClass &RC : RegClassList)
    RegisterSets.insert(
        RegisterSet(RC.getOrder().begin(), RC.getOrder().end()));

  // Add any required singleton sets.
  for (Record *Rec : SingletonRegisters) {
    RegisterSets.insert(RegisterSet(&Rec, &Rec + 1));
  }

  // Introduce derived sets where necessary (when a register does not determine
  // a unique register set class), and build the mapping of registers to the set
  // they should classify to.
  std::map<Record *, RegisterSet> RegisterMap;
  for (const CodeGenRegister &CGR : Registers) {
    // Compute the intersection of all sets containing this register.
    RegisterSet ContainingSet;

    for (const RegisterSet &RS : RegisterSets) {
      if (!RS.count(CGR.TheDef))
        continue;

      if (ContainingSet.empty()) {
        ContainingSet = RS;
        continue;
      }

      RegisterSet Tmp;
      std::set_intersection(ContainingSet.begin(), ContainingSet.end(),
                            RS.begin(), RS.end(),
                            std::inserter(Tmp, Tmp.begin()), LessRecordByID());
      ContainingSet = std::move(Tmp);
    }

    if (!ContainingSet.empty()) {
      RegisterSets.insert(ContainingSet);
      RegisterMap.insert(std::pair(CGR.TheDef, ContainingSet));
    }
  }

  // Construct the register classes.
  std::map<RegisterSet, ClassInfo *, LessRegisterSet> RegisterSetClasses;
  unsigned Index = 0;
  for (const RegisterSet &RS : RegisterSets) {
    Classes.emplace_front();
    ClassInfo *CI = &Classes.front();
    CI->Kind = ClassInfo::RegisterClass0 + Index;
    CI->ClassName = "Reg" + utostr(Index);
    CI->Name = "MCK_Reg" + utostr(Index);
    CI->ValueName = "";
    CI->PredicateMethod = ""; // unused
    CI->RenderMethod = "addRegOperands";
    CI->Registers = RS;
    // FIXME: diagnostic type.
    CI->DiagnosticType = "";
    CI->IsOptional = false;
    CI->DefaultMethod = ""; // unused
    RegisterSetClasses.insert(std::pair(RS, CI));
    ++Index;
  }

  // Find the superclasses; we could compute only the subgroup lattice edges,
  // but there isn't really a point.
  for (const RegisterSet &RS : RegisterSets) {
    ClassInfo *CI = RegisterSetClasses[RS];
    for (const RegisterSet &RS2 : RegisterSets)
      if (RS != RS2 && std::includes(RS2.begin(), RS2.end(), RS.begin(),
                                     RS.end(), LessRecordByID()))
        CI->SuperClasses.push_back(RegisterSetClasses[RS2]);
  }

  // Name the register classes which correspond to a user defined RegisterClass.
  for (const CodeGenRegisterClass &RC : RegClassList) {
    // Def will be NULL for non-user defined register classes.
    Record *Def = RC.getDef();
    if (!Def)
      continue;
    ClassInfo *CI = RegisterSetClasses[RegisterSet(RC.getOrder().begin(),
                                                   RC.getOrder().end())];
    if (CI->ValueName.empty()) {
      CI->ClassName = RC.getName();
      CI->Name = "MCK_" + RC.getName();
      CI->ValueName = RC.getName();
    } else
      CI->ValueName = CI->ValueName + "," + RC.getName();

    Init *DiagnosticType = Def->getValueInit("DiagnosticType");
    if (StringInit *SI = dyn_cast<StringInit>(DiagnosticType))
      CI->DiagnosticType = std::string(SI->getValue());

    Init *DiagnosticString = Def->getValueInit("DiagnosticString");
    if (StringInit *SI = dyn_cast<StringInit>(DiagnosticString))
      CI->DiagnosticString = std::string(SI->getValue());

    // If we have a diagnostic string but the diagnostic type is not specified
    // explicitly, create an anonymous diagnostic type.
    if (!CI->DiagnosticString.empty() && CI->DiagnosticType.empty())
      CI->DiagnosticType = RC.getName();

    RegisterClassClasses.insert(std::pair(Def, CI));
  }

  // Populate the map for individual registers.
  for (auto &It : RegisterMap)
    RegisterClasses[It.first] = RegisterSetClasses[It.second];

  // Name the register classes which correspond to singleton registers.
  for (Record *Rec : SingletonRegisters) {
    ClassInfo *CI = RegisterClasses[Rec];
    assert(CI && "Missing singleton register class info!");

    if (CI->ValueName.empty()) {
      CI->ClassName = std::string(Rec->getName());
      CI->Name = "MCK_" + Rec->getName().str();
      CI->ValueName = std::string(Rec->getName());
    } else
      CI->ValueName = CI->ValueName + "," + Rec->getName().str();
  }
}

void AsmMatcherInfo::buildOperandClasses() {
  std::vector<Record *> AsmOperands =
      Records.getAllDerivedDefinitions("AsmOperandClass");

  // Pre-populate AsmOperandClasses map.
  for (Record *Rec : AsmOperands) {
    Classes.emplace_front();
    AsmOperandClasses[Rec] = &Classes.front();
  }

  unsigned Index = 0;
  for (Record *Rec : AsmOperands) {
    ClassInfo *CI = AsmOperandClasses[Rec];
    CI->Kind = ClassInfo::UserClass0 + Index;

    ListInit *Supers = Rec->getValueAsListInit("SuperClasses");
    for (Init *I : Supers->getValues()) {
      DefInit *DI = dyn_cast<DefInit>(I);
      if (!DI) {
        PrintError(Rec->getLoc(), "Invalid super class reference!");
        continue;
      }

      ClassInfo *SC = AsmOperandClasses[DI->getDef()];
      if (!SC)
        PrintError(Rec->getLoc(), "Invalid super class reference!");
      else
        CI->SuperClasses.push_back(SC);
    }
    CI->ClassName = std::string(Rec->getValueAsString("Name"));
    CI->Name = "MCK_" + CI->ClassName;
    CI->ValueName = std::string(Rec->getName());

    // Get or construct the predicate method name.
    Init *PMName = Rec->getValueInit("PredicateMethod");
    if (StringInit *SI = dyn_cast<StringInit>(PMName)) {
      CI->PredicateMethod = std::string(SI->getValue());
    } else {
      assert(isa<UnsetInit>(PMName) && "Unexpected PredicateMethod field!");
      CI->PredicateMethod = "is" + CI->ClassName;
    }

    // Get or construct the render method name.
    Init *RMName = Rec->getValueInit("RenderMethod");
    if (StringInit *SI = dyn_cast<StringInit>(RMName)) {
      CI->RenderMethod = std::string(SI->getValue());
    } else {
      assert(isa<UnsetInit>(RMName) && "Unexpected RenderMethod field!");
      CI->RenderMethod = "add" + CI->ClassName + "Operands";
    }

    // Get the parse method name or leave it as empty.
    Init *PRMName = Rec->getValueInit("ParserMethod");
    if (StringInit *SI = dyn_cast<StringInit>(PRMName))
      CI->ParserMethod = std::string(SI->getValue());

    // Get the diagnostic type and string or leave them as empty.
    Init *DiagnosticType = Rec->getValueInit("DiagnosticType");
    if (StringInit *SI = dyn_cast<StringInit>(DiagnosticType))
      CI->DiagnosticType = std::string(SI->getValue());
    Init *DiagnosticString = Rec->getValueInit("DiagnosticString");
    if (StringInit *SI = dyn_cast<StringInit>(DiagnosticString))
      CI->DiagnosticString = std::string(SI->getValue());
    // If we have a DiagnosticString, we need a DiagnosticType for use within
    // the matcher.
    if (!CI->DiagnosticString.empty() && CI->DiagnosticType.empty())
      CI->DiagnosticType = CI->ClassName;

    Init *IsOptional = Rec->getValueInit("IsOptional");
    if (BitInit *BI = dyn_cast<BitInit>(IsOptional))
      CI->IsOptional = BI->getValue();

    // Get or construct the default method name.
    Init *DMName = Rec->getValueInit("DefaultMethod");
    if (StringInit *SI = dyn_cast<StringInit>(DMName)) {
      CI->DefaultMethod = std::string(SI->getValue());
    } else {
      assert(isa<UnsetInit>(DMName) && "Unexpected DefaultMethod field!");
      CI->DefaultMethod = "default" + CI->ClassName + "Operands";
    }

    ++Index;
  }
}

AsmMatcherInfo::AsmMatcherInfo(Record *asmParser, CodeGenTarget &target,
                               RecordKeeper &records)
    : Records(records), AsmParser(asmParser), Target(target) {}

/// buildOperandMatchInfo - Build the necessary information to handle user
/// defined operand parsing methods.
void AsmMatcherInfo::buildOperandMatchInfo() {

  /// Map containing a mask with all operands indices that can be found for
  /// that class inside a instruction.
  typedef std::map<ClassInfo *, unsigned, deref<std::less<>>> OpClassMaskTy;
  OpClassMaskTy OpClassMask;

  bool CallCustomParserForAllOperands =
      AsmParser->getValueAsBit("CallCustomParserForAllOperands");
  for (const auto &MI : Matchables) {
    OpClassMask.clear();

    // Keep track of all operands of this instructions which belong to the
    // same class.
    unsigned NumOptionalOps = 0;
    for (unsigned i = 0, e = MI->AsmOperands.size(); i != e; ++i) {
      const MatchableInfo::AsmOperand &Op = MI->AsmOperands[i];
      if (CallCustomParserForAllOperands || !Op.Class->ParserMethod.empty()) {
        unsigned &OperandMask = OpClassMask[Op.Class];
        OperandMask |= maskTrailingOnes<unsigned>(NumOptionalOps + 1)
                       << (i - NumOptionalOps);
      }
      if (Op.Class->IsOptional)
        ++NumOptionalOps;
    }

    // Generate operand match info for each mnemonic/operand class pair.
    for (const auto &OCM : OpClassMask) {
      unsigned OpMask = OCM.second;
      ClassInfo *CI = OCM.first;
      OperandMatchInfo.push_back(
          OperandMatchEntry::create(MI.get(), CI, OpMask));
    }
  }
}

void AsmMatcherInfo::buildInfo() {
  // Build information about all of the AssemblerPredicates.
  const std::vector<std::pair<Record *, SubtargetFeatureInfo>>
      &SubtargetFeaturePairs = SubtargetFeatureInfo::getAll(Records);
  SubtargetFeatures.insert(SubtargetFeaturePairs.begin(),
                           SubtargetFeaturePairs.end());
#ifndef NDEBUG
  for (const auto &Pair : SubtargetFeatures)
    LLVM_DEBUG(Pair.second.dump());
#endif // NDEBUG

  bool HasMnemonicFirst = AsmParser->getValueAsBit("HasMnemonicFirst");
  bool ReportMultipleNearMisses =
      AsmParser->getValueAsBit("ReportMultipleNearMisses");

  // Parse the instructions; we need to do this first so that we can gather the
  // singleton register classes.
  SmallPtrSet<Record *, 16> SingletonRegisters;
  unsigned VariantCount = Target.getAsmParserVariantCount();
  for (unsigned VC = 0; VC != VariantCount; ++VC) {
    Record *AsmVariant = Target.getAsmParserVariant(VC);
    StringRef CommentDelimiter =
        AsmVariant->getValueAsString("CommentDelimiter");
    AsmVariantInfo Variant;
    Variant.RegisterPrefix = AsmVariant->getValueAsString("RegisterPrefix");
    Variant.TokenizingCharacters =
        AsmVariant->getValueAsString("TokenizingCharacters");
    Variant.SeparatorCharacters =
        AsmVariant->getValueAsString("SeparatorCharacters");
    Variant.BreakCharacters = AsmVariant->getValueAsString("BreakCharacters");
    Variant.Name = AsmVariant->getValueAsString("Name");
    Variant.AsmVariantNo = AsmVariant->getValueAsInt("Variant");

    for (const CodeGenInstruction *CGI : Target.getInstructionsByEnumValue()) {

      // If the tblgen -match-prefix option is specified (for tblgen hackers),
      // filter the set of instructions we consider.
      if (!StringRef(CGI->TheDef->getName()).starts_with(MatchPrefix))
        continue;

      // Ignore "codegen only" instructions.
      if (CGI->TheDef->getValueAsBit("isCodeGenOnly"))
        continue;

      // Ignore instructions for different instructions
      StringRef V = CGI->TheDef->getValueAsString("AsmVariantName");
      if (!V.empty() && V != Variant.Name)
        continue;

      auto II = std::make_unique<MatchableInfo>(*CGI);

      II->initialize(*this, SingletonRegisters, Variant, HasMnemonicFirst);

      // Ignore instructions which shouldn't be matched and diagnose invalid
      // instruction definitions with an error.
      if (!II->validate(CommentDelimiter, false))
        continue;

      Matchables.push_back(std::move(II));
    }

    // Parse all of the InstAlias definitions and stick them in the list of
    // matchables.
    std::vector<Record *> AllInstAliases =
        Records.getAllDerivedDefinitions("InstAlias");
    for (Record *InstAlias : AllInstAliases) {
      auto Alias = std::make_unique<CodeGenInstAlias>(InstAlias, Target);

      // If the tblgen -match-prefix option is specified (for tblgen hackers),
      // filter the set of instruction aliases we consider, based on the target
      // instruction.
      if (!StringRef(Alias->ResultInst->TheDef->getName())
               .starts_with(MatchPrefix))
        continue;

      StringRef V = Alias->TheDef->getValueAsString("AsmVariantName");
      if (!V.empty() && V != Variant.Name)
        continue;

      auto II = std::make_unique<MatchableInfo>(std::move(Alias));

      II->initialize(*this, SingletonRegisters, Variant, HasMnemonicFirst);

      // Validate the alias definitions.
      II->validate(CommentDelimiter, true);

      Matchables.push_back(std::move(II));
    }
  }

  // Build info for the register classes.
  buildRegisterClasses(SingletonRegisters);

  // Build info for the user defined assembly operand classes.
  buildOperandClasses();

  // Build the information about matchables, now that we have fully formed
  // classes.
  std::vector<std::unique_ptr<MatchableInfo>> NewMatchables;
  for (auto &II : Matchables) {
    // Parse the tokens after the mnemonic.
    // Note: buildInstructionOperandReference may insert new AsmOperands, so
    // don't precompute the loop bound.
    for (unsigned i = 0; i != II->AsmOperands.size(); ++i) {
      MatchableInfo::AsmOperand &Op = II->AsmOperands[i];
      StringRef Token = Op.Token;

      // Check for singleton registers.
      if (Record *RegRecord = Op.SingletonReg) {
        Op.Class = RegisterClasses[RegRecord];
        assert(Op.Class && Op.Class->Registers.size() == 1 &&
               "Unexpected class for singleton register");
        continue;
      }

      // Check for simple tokens.
      if (Token[0] != '$') {
        Op.Class = getTokenClass(Token);
        continue;
      }

      if (Token.size() > 1 && isdigit(Token[1])) {
        Op.Class = getTokenClass(Token);
        continue;
      }

      // Otherwise this is an operand reference.
      StringRef OperandName;
      if (Token[1] == '{')
        OperandName = Token.substr(2, Token.size() - 3);
      else
        OperandName = Token.substr(1);

      if (isa<const CodeGenInstruction *>(II->DefRec))
        buildInstructionOperandReference(II.get(), OperandName, i);
      else
        buildAliasOperandReference(II.get(), OperandName, Op);
    }

    if (isa<const CodeGenInstruction *>(II->DefRec)) {
      II->buildInstructionResultOperands();
      // If the instruction has a two-operand alias, build up the
      // matchable here. We'll add them in bulk at the end to avoid
      // confusing this loop.
      StringRef Constraint =
          II->TheDef->getValueAsString("TwoOperandAliasConstraint");
      if (Constraint != "") {
        // Start by making a copy of the original matchable.
        auto AliasII = std::make_unique<MatchableInfo>(*II);

        // Adjust it to be a two-operand alias.
        AliasII->formTwoOperandAlias(Constraint);

        // Add the alias to the matchables list.
        NewMatchables.push_back(std::move(AliasII));
      }
    } else
      // FIXME: The tied operands checking is not yet integrated with the
      // framework for reporting multiple near misses. To prevent invalid
      // formats from being matched with an alias if a tied-operands check
      // would otherwise have disallowed it, we just disallow such constructs
      // in TableGen completely.
      II->buildAliasResultOperands(!ReportMultipleNearMisses);
  }
  if (!NewMatchables.empty())
    Matchables.insert(Matchables.end(),
                      std::make_move_iterator(NewMatchables.begin()),
                      std::make_move_iterator(NewMatchables.end()));

  // Process token alias definitions and set up the associated superclass
  // information.
  std::vector<Record *> AllTokenAliases =
      Records.getAllDerivedDefinitions("TokenAlias");
  for (Record *Rec : AllTokenAliases) {
    ClassInfo *FromClass = getTokenClass(Rec->getValueAsString("FromToken"));
    ClassInfo *ToClass = getTokenClass(Rec->getValueAsString("ToToken"));
    if (FromClass == ToClass)
      PrintFatalError(Rec->getLoc(),
                      "error: Destination value identical to source value.");
    FromClass->SuperClasses.push_back(ToClass);
  }

  // Reorder classes so that classes precede super classes.
  Classes.sort();

#ifdef EXPENSIVE_CHECKS
  // Verify that the table is sorted and operator < works transitively.
  for (auto I = Classes.begin(), E = Classes.end(); I != E; ++I) {
    for (auto J = I; J != E; ++J) {
      assert(!(*J < *I));
      assert(I == J || !J->isSubsetOf(*I));
    }
  }
#endif
}

/// buildInstructionOperandReference - The specified operand is a reference to a
/// named operand such as $src.  Resolve the Class and OperandInfo pointers.
void AsmMatcherInfo::buildInstructionOperandReference(MatchableInfo *II,
                                                      StringRef OperandName,
                                                      unsigned AsmOpIdx) {
  const CodeGenInstruction &CGI = *cast<const CodeGenInstruction *>(II->DefRec);
  const CGIOperandList &Operands = CGI.Operands;
  MatchableInfo::AsmOperand *Op = &II->AsmOperands[AsmOpIdx];

  // Map this token to an operand.
  unsigned Idx;
  if (!Operands.hasOperandNamed(OperandName, Idx))
    PrintFatalError(II->TheDef->getLoc(),
                    "error: unable to find operand: '" + OperandName + "'");

  // If the instruction operand has multiple suboperands, but the parser
  // match class for the asm operand is still the default "ImmAsmOperand",
  // then handle each suboperand separately.
  if (Op->SubOpIdx == -1 && Operands[Idx].MINumOperands > 1) {
    Record *Rec = Operands[Idx].Rec;
    assert(Rec->isSubClassOf("Operand") && "Unexpected operand!");
    Record *MatchClass = Rec->getValueAsDef("ParserMatchClass");
    if (MatchClass && MatchClass->getValueAsString("Name") == "Imm") {
      // Insert remaining suboperands after AsmOpIdx in II->AsmOperands.
      StringRef Token = Op->Token; // save this in case Op gets moved
      for (unsigned SI = 1, SE = Operands[Idx].MINumOperands; SI != SE; ++SI) {
        MatchableInfo::AsmOperand NewAsmOp(/*IsIsolatedToken=*/true, Token);
        NewAsmOp.SubOpIdx = SI;
        II->AsmOperands.insert(II->AsmOperands.begin() + AsmOpIdx + SI,
                               NewAsmOp);
      }
      // Replace Op with first suboperand.
      Op = &II->AsmOperands[AsmOpIdx]; // update the pointer in case it moved
      Op->SubOpIdx = 0;
    }
  }

  // Set up the operand class.
  Op->Class = getOperandClass(Operands[Idx], Op->SubOpIdx);
  Op->OrigSrcOpName = OperandName;

  // If the named operand is tied, canonicalize it to the untied operand.
  // For example, something like:
  //   (outs GPR:$dst), (ins GPR:$src)
  // with an asmstring of
  //   "inc $src"
  // we want to canonicalize to:
  //   "inc $dst"
  // so that we know how to provide the $dst operand when filling in the result.
  int OITied = -1;
  if (Operands[Idx].MINumOperands == 1)
    OITied = Operands[Idx].getTiedRegister();
  if (OITied != -1) {
    // The tied operand index is an MIOperand index, find the operand that
    // contains it.
    std::pair<unsigned, unsigned> Idx = Operands.getSubOperandNumber(OITied);
    OperandName = Operands[Idx.first].Name;
    Op->SubOpIdx = Idx.second;
  }

  Op->SrcOpName = OperandName;
}

/// buildAliasOperandReference - When parsing an operand reference out of the
/// matching string (e.g. "movsx $src, $dst"), determine what the class of the
/// operand reference is by looking it up in the result pattern definition.
void AsmMatcherInfo::buildAliasOperandReference(MatchableInfo *II,
                                                StringRef OperandName,
                                                MatchableInfo::AsmOperand &Op) {
  const CodeGenInstAlias &CGA = *cast<const CodeGenInstAlias *>(II->DefRec);

  // Set up the operand class.
  for (unsigned i = 0, e = CGA.ResultOperands.size(); i != e; ++i)
    if (CGA.ResultOperands[i].isRecord() &&
        CGA.ResultOperands[i].getName() == OperandName) {
      // It's safe to go with the first one we find, because CodeGenInstAlias
      // validates that all operands with the same name have the same record.
      Op.SubOpIdx = CGA.ResultInstOperandIndex[i].second;
      // Use the match class from the Alias definition, not the
      // destination instruction, as we may have an immediate that's
      // being munged by the match class.
      Op.Class =
          getOperandClass(CGA.ResultOperands[i].getRecord(), Op.SubOpIdx);
      Op.SrcOpName = OperandName;
      Op.OrigSrcOpName = OperandName;
      return;
    }

  PrintFatalError(II->TheDef->getLoc(),
                  "error: unable to find operand: '" + OperandName + "'");
}

void MatchableInfo::buildInstructionResultOperands() {
  const CodeGenInstruction *ResultInst = getResultInst();

  // Loop over all operands of the result instruction, determining how to
  // populate them.
  for (const CGIOperandList::OperandInfo &OpInfo : ResultInst->Operands) {
    // If this is a tied operand, just copy from the previously handled operand.
    int TiedOp = -1;
    if (OpInfo.MINumOperands == 1)
      TiedOp = OpInfo.getTiedRegister();
    if (TiedOp != -1) {
      int TiedSrcOperand = findAsmOperandOriginallyNamed(OpInfo.Name);
      if (TiedSrcOperand != -1 &&
          ResOperands[TiedOp].Kind == ResOperand::RenderAsmOperand)
        ResOperands.push_back(ResOperand::getTiedOp(
            TiedOp, ResOperands[TiedOp].AsmOperandNum, TiedSrcOperand));
      else
        ResOperands.push_back(ResOperand::getTiedOp(TiedOp, 0, 0));
      continue;
    }

    int SrcOperand = findAsmOperandNamed(OpInfo.Name);
    if (OpInfo.Name.empty() || SrcOperand == -1) {
      // This may happen for operands that are tied to a suboperand of a
      // complex operand.  Simply use a dummy value here; nobody should
      // use this operand slot.
      // FIXME: The long term goal is for the MCOperand list to not contain
      // tied operands at all.
      ResOperands.push_back(ResOperand::getImmOp(0));
      continue;
    }

    // Check if the one AsmOperand populates the entire operand.
    unsigned NumOperands = OpInfo.MINumOperands;
    if (AsmOperands[SrcOperand].SubOpIdx == -1) {
      ResOperands.push_back(ResOperand::getRenderedOp(SrcOperand, NumOperands));
      continue;
    }

    // Add a separate ResOperand for each suboperand.
    for (unsigned AI = 0; AI < NumOperands; ++AI) {
      assert(AsmOperands[SrcOperand + AI].SubOpIdx == (int)AI &&
             AsmOperands[SrcOperand + AI].SrcOpName == OpInfo.Name &&
             "unexpected AsmOperands for suboperands");
      ResOperands.push_back(ResOperand::getRenderedOp(SrcOperand + AI, 1));
    }
  }
}

void MatchableInfo::buildAliasResultOperands(bool AliasConstraintsAreChecked) {
  const CodeGenInstAlias &CGA = *cast<const CodeGenInstAlias *>(DefRec);
  const CodeGenInstruction *ResultInst = getResultInst();

  // Map of:  $reg -> #lastref
  //   where $reg is the name of the operand in the asm string
  //   where #lastref is the last processed index where $reg was referenced in
  //   the asm string.
  SmallDenseMap<StringRef, int> OperandRefs;

  // Loop over all operands of the result instruction, determining how to
  // populate them.
  unsigned AliasOpNo = 0;
  unsigned LastOpNo = CGA.ResultInstOperandIndex.size();
  for (unsigned i = 0, e = ResultInst->Operands.size(); i != e; ++i) {
    const CGIOperandList::OperandInfo *OpInfo = &ResultInst->Operands[i];

    // If this is a tied operand, just copy from the previously handled operand.
    int TiedOp = -1;
    if (OpInfo->MINumOperands == 1)
      TiedOp = OpInfo->getTiedRegister();
    if (TiedOp != -1) {
      unsigned SrcOp1 = 0;
      unsigned SrcOp2 = 0;

      // If an operand has been specified twice in the asm string,
      // add the two source operand's indices to the TiedOp so that
      // at runtime the 'tied' constraint is checked.
      if (ResOperands[TiedOp].Kind == ResOperand::RenderAsmOperand) {
        SrcOp1 = ResOperands[TiedOp].AsmOperandNum;

        // Find the next operand (similarly named operand) in the string.
        StringRef Name = AsmOperands[SrcOp1].SrcOpName;
        auto Insert = OperandRefs.try_emplace(Name, SrcOp1);
        SrcOp2 = findAsmOperandNamed(Name, Insert.first->second);

        // Not updating the record in OperandRefs will cause TableGen
        // to fail with an error at the end of this function.
        if (AliasConstraintsAreChecked)
          Insert.first->second = SrcOp2;

        // In case it only has one reference in the asm string,
        // it doesn't need to be checked for tied constraints.
        SrcOp2 = (SrcOp2 == (unsigned)-1) ? SrcOp1 : SrcOp2;
      }

      // If the alias operand is of a different operand class, we only want
      // to benefit from the tied-operands check and just match the operand
      // as a normal, but not copy the original (TiedOp) to the result
      // instruction. We do this by passing -1 as the tied operand to copy.
      if (ResultInst->Operands[i].Rec->getName() !=
          ResultInst->Operands[TiedOp].Rec->getName()) {
        SrcOp1 = ResOperands[TiedOp].AsmOperandNum;
        int SubIdx = CGA.ResultInstOperandIndex[AliasOpNo].second;
        StringRef Name = CGA.ResultOperands[AliasOpNo].getName();
        SrcOp2 = findAsmOperand(Name, SubIdx);
        ResOperands.push_back(
            ResOperand::getTiedOp((unsigned)-1, SrcOp1, SrcOp2));
      } else {
        ResOperands.push_back(ResOperand::getTiedOp(TiedOp, SrcOp1, SrcOp2));
        continue;
      }
    }

    // Handle all the suboperands for this operand.
    const std::string &OpName = OpInfo->Name;
    for (; AliasOpNo < LastOpNo &&
           CGA.ResultInstOperandIndex[AliasOpNo].first == i;
         ++AliasOpNo) {
      int SubIdx = CGA.ResultInstOperandIndex[AliasOpNo].second;

      // Find out what operand from the asmparser that this MCInst operand
      // comes from.
      switch (CGA.ResultOperands[AliasOpNo].Kind) {
      case CodeGenInstAlias::ResultOperand::K_Record: {
        StringRef Name = CGA.ResultOperands[AliasOpNo].getName();
        int SrcOperand = findAsmOperand(Name, SubIdx);
        if (SrcOperand == -1)
          PrintFatalError(TheDef->getLoc(),
                          "Instruction '" + TheDef->getName() +
                              "' has operand '" + OpName +
                              "' that doesn't appear in asm string!");

        // Add it to the operand references. If it is added a second time, the
        // record won't be updated and it will fail later on.
        OperandRefs.try_emplace(Name, SrcOperand);

        unsigned NumOperands = (SubIdx == -1 ? OpInfo->MINumOperands : 1);
        ResOperands.push_back(
            ResOperand::getRenderedOp(SrcOperand, NumOperands));
        break;
      }
      case CodeGenInstAlias::ResultOperand::K_Imm: {
        int64_t ImmVal = CGA.ResultOperands[AliasOpNo].getImm();
        ResOperands.push_back(ResOperand::getImmOp(ImmVal));
        break;
      }
      case CodeGenInstAlias::ResultOperand::K_Reg: {
        Record *Reg = CGA.ResultOperands[AliasOpNo].getRegister();
        ResOperands.push_back(ResOperand::getRegOp(Reg));
        break;
      }
      }
    }
  }

  // Check that operands are not repeated more times than is supported.
  for (auto &T : OperandRefs) {
    if (T.second != -1 && findAsmOperandNamed(T.first, T.second) != -1)
      PrintFatalError(TheDef->getLoc(),
                      "Operand '" + T.first + "' can never be matched");
  }
}

static unsigned
getConverterOperandID(const std::string &Name,
                      SmallSetVector<CachedHashString, 16> &Table,
                      bool &IsNew) {
  IsNew = Table.insert(CachedHashString(Name));

  unsigned ID = IsNew ? Table.size() - 1 : find(Table, Name) - Table.begin();

  assert(ID < Table.size());

  return ID;
}

static unsigned
emitConvertFuncs(CodeGenTarget &Target, StringRef ClassName,
                 std::vector<std::unique_ptr<MatchableInfo>> &Infos,
                 bool HasMnemonicFirst, bool HasOptionalOperands,
                 raw_ostream &OS) {
  SmallSetVector<CachedHashString, 16> OperandConversionKinds;
  SmallSetVector<CachedHashString, 16> InstructionConversionKinds;
  std::vector<std::vector<uint8_t>> ConversionTable;
  size_t MaxRowLength = 2; // minimum is custom converter plus terminator.

  // TargetOperandClass - This is the target's operand class, like X86Operand.
  std::string TargetOperandClass = Target.getName().str() + "Operand";

  // Write the convert function to a separate stream, so we can drop it after
  // the enum. We'll build up the conversion handlers for the individual
  // operand types opportunistically as we encounter them.
  std::string ConvertFnBody;
  raw_string_ostream CvtOS(ConvertFnBody);
  // Start the unified conversion function.
  if (HasOptionalOperands) {
    CvtOS << "void " << Target.getName() << ClassName << "::\n"
          << "convertToMCInst(unsigned Kind, MCInst &Inst, "
          << "unsigned Opcode,\n"
          << "                const OperandVector &Operands,\n"
          << "                const SmallBitVector &OptionalOperandsMask,\n"
          << "                ArrayRef<unsigned> DefaultsOffset) {\n";
  } else {
    CvtOS << "void " << Target.getName() << ClassName << "::\n"
          << "convertToMCInst(unsigned Kind, MCInst &Inst, "
          << "unsigned Opcode,\n"
          << "                const OperandVector &Operands) {\n";
  }
  CvtOS << "  assert(Kind < CVT_NUM_SIGNATURES && \"Invalid signature!\");\n";
  CvtOS << "  const uint8_t *Converter = ConversionTable[Kind];\n";
  CvtOS << "  Inst.setOpcode(Opcode);\n";
  CvtOS << "  for (const uint8_t *p = Converter; *p; p += 2) {\n";
  if (HasOptionalOperands) {
    // When optional operands are involved, formal and actual operand indices
    // may differ. Map the former to the latter by subtracting the number of
    // absent optional operands.
    // FIXME: This is not an operand index in the CVT_Tied case
    CvtOS << "    unsigned OpIdx = *(p + 1) - DefaultsOffset[*(p + 1)];\n";
  } else {
    CvtOS << "    unsigned OpIdx = *(p + 1);\n";
  }
  CvtOS << "    switch (*p) {\n";
  CvtOS << "    default: llvm_unreachable(\"invalid conversion entry!\");\n";
  CvtOS << "    case CVT_Reg:\n";
  CvtOS << "      static_cast<" << TargetOperandClass
        << " &>(*Operands[OpIdx]).addRegOperands(Inst, 1);\n";
  CvtOS << "      break;\n";
  CvtOS << "    case CVT_Tied: {\n";
  CvtOS << "      assert(*(p + 1) < (size_t)(std::end(TiedAsmOperandTable) -\n";
  CvtOS
      << "                              std::begin(TiedAsmOperandTable)) &&\n";
  CvtOS << "             \"Tied operand not found\");\n";
  CvtOS << "      unsigned TiedResOpnd = TiedAsmOperandTable[*(p + 1)][0];\n";
  CvtOS << "      if (TiedResOpnd != (uint8_t)-1)\n";
  CvtOS << "        Inst.addOperand(Inst.getOperand(TiedResOpnd));\n";
  CvtOS << "      break;\n";
  CvtOS << "    }\n";

  std::string OperandFnBody;
  raw_string_ostream OpOS(OperandFnBody);
  // Start the operand number lookup function.
  OpOS << "void " << Target.getName() << ClassName << "::\n"
       << "convertToMapAndConstraints(unsigned Kind,\n";
  OpOS.indent(27);
  OpOS << "const OperandVector &Operands) {\n"
       << "  assert(Kind < CVT_NUM_SIGNATURES && \"Invalid signature!\");\n"
       << "  unsigned NumMCOperands = 0;\n"
       << "  const uint8_t *Converter = ConversionTable[Kind];\n"
       << "  for (const uint8_t *p = Converter; *p; p += 2) {\n"
       << "    switch (*p) {\n"
       << "    default: llvm_unreachable(\"invalid conversion entry!\");\n"
       << "    case CVT_Reg:\n"
       << "      Operands[*(p + 1)]->setMCOperandNum(NumMCOperands);\n"
       << "      Operands[*(p + 1)]->setConstraint(\"r\");\n"
       << "      ++NumMCOperands;\n"
       << "      break;\n"
       << "    case CVT_Tied:\n"
       << "      ++NumMCOperands;\n"
       << "      break;\n";

  // Pre-populate the operand conversion kinds with the standard always
  // available entries.
  OperandConversionKinds.insert(CachedHashString("CVT_Done"));
  OperandConversionKinds.insert(CachedHashString("CVT_Reg"));
  OperandConversionKinds.insert(CachedHashString("CVT_Tied"));
  enum { CVT_Done, CVT_Reg, CVT_Tied };

  // Map of e.g. <0, 2, 3> -> "Tie_0_2_3" enum label.
  std::map<std::tuple<uint8_t, uint8_t, uint8_t>, std::string>
      TiedOperandsEnumMap;

  for (auto &II : Infos) {
    // Check if we have a custom match function.
    StringRef AsmMatchConverter =
        II->getResultInst()->TheDef->getValueAsString("AsmMatchConverter");
    if (!AsmMatchConverter.empty() && II->UseInstAsmMatchConverter) {
      std::string Signature = ("ConvertCustom_" + AsmMatchConverter).str();
      II->ConversionFnKind = Signature;

      // Check if we have already generated this signature.
      if (!InstructionConversionKinds.insert(CachedHashString(Signature)))
        continue;

      // Remember this converter for the kind enum.
      unsigned KindID = OperandConversionKinds.size();
      OperandConversionKinds.insert(
          CachedHashString("CVT_" + getEnumNameForToken(AsmMatchConverter)));

      // Add the converter row for this instruction.
      ConversionTable.emplace_back();
      ConversionTable.back().push_back(KindID);
      ConversionTable.back().push_back(CVT_Done);

      // Add the handler to the conversion driver function.
      CvtOS << "    case CVT_" << getEnumNameForToken(AsmMatchConverter)
            << ":\n"
            << "      " << AsmMatchConverter << "(Inst, Operands);\n"
            << "      break;\n";

      // FIXME: Handle the operand number lookup for custom match functions.
      continue;
    }

    // Build the conversion function signature.
    std::string Signature = "Convert";

    std::vector<uint8_t> ConversionRow;

    // Compute the convert enum and the case body.
    MaxRowLength = std::max(MaxRowLength, II->ResOperands.size() * 2 + 1);

    for (unsigned i = 0, e = II->ResOperands.size(); i != e; ++i) {
      const MatchableInfo::ResOperand &OpInfo = II->ResOperands[i];

      // Generate code to populate each result operand.
      switch (OpInfo.Kind) {
      case MatchableInfo::ResOperand::RenderAsmOperand: {
        // This comes from something we parsed.
        const MatchableInfo::AsmOperand &Op =
            II->AsmOperands[OpInfo.AsmOperandNum];

        // Registers are always converted the same, don't duplicate the
        // conversion function based on them.
        Signature += "__";
        std::string Class;
        Class = Op.Class->isRegisterClass() ? "Reg" : Op.Class->ClassName;
        Signature += Class;
        Signature += utostr(OpInfo.MINumOperands);
        Signature += "_" + itostr(OpInfo.AsmOperandNum);

        // Add the conversion kind, if necessary, and get the associated ID
        // the index of its entry in the vector).
        std::string Name =
            "CVT_" +
            (Op.Class->isRegisterClass() ? "Reg" : Op.Class->RenderMethod);
        if (Op.Class->IsOptional) {
          // For optional operands we must also care about DefaultMethod
          assert(HasOptionalOperands);
          Name += "_" + Op.Class->DefaultMethod;
        }
        Name = getEnumNameForToken(Name);

        bool IsNewConverter = false;
        unsigned ID =
            getConverterOperandID(Name, OperandConversionKinds, IsNewConverter);

        // Add the operand entry to the instruction kind conversion row.
        ConversionRow.push_back(ID);
        ConversionRow.push_back(OpInfo.AsmOperandNum + HasMnemonicFirst);

        if (!IsNewConverter)
          break;

        // This is a new operand kind. Add a handler for it to the
        // converter driver.
        CvtOS << "    case " << Name << ":\n";
        if (Op.Class->IsOptional) {
          // If optional operand is not present in actual instruction then we
          // should call its DefaultMethod before RenderMethod
          assert(HasOptionalOperands);
          CvtOS << "      if (OptionalOperandsMask[*(p + 1) - 1]) {\n"
                << "        " << Op.Class->DefaultMethod << "()"
                << "->" << Op.Class->RenderMethod << "(Inst, "
                << OpInfo.MINumOperands << ");\n"
                << "      } else {\n"
                << "        static_cast<" << TargetOperandClass
                << " &>(*Operands[OpIdx])." << Op.Class->RenderMethod
                << "(Inst, " << OpInfo.MINumOperands << ");\n"
                << "      }\n";
        } else {
          CvtOS << "      static_cast<" << TargetOperandClass
                << " &>(*Operands[OpIdx])." << Op.Class->RenderMethod
                << "(Inst, " << OpInfo.MINumOperands << ");\n";
        }
        CvtOS << "      break;\n";

        // Add a handler for the operand number lookup.
        OpOS << "    case " << Name << ":\n"
             << "      Operands[*(p + 1)]->setMCOperandNum(NumMCOperands);\n";

        if (Op.Class->isRegisterClass())
          OpOS << "      Operands[*(p + 1)]->setConstraint(\"r\");\n";
        else
          OpOS << "      Operands[*(p + 1)]->setConstraint(\"m\");\n";
        OpOS << "      NumMCOperands += " << OpInfo.MINumOperands << ";\n"
             << "      break;\n";
        break;
      }
      case MatchableInfo::ResOperand::TiedOperand: {
        // If this operand is tied to a previous one, just copy the MCInst
        // operand from the earlier one.We can only tie single MCOperand values.
        assert(OpInfo.MINumOperands == 1 && "Not a singular MCOperand");
        uint8_t TiedOp = OpInfo.TiedOperands.ResOpnd;
        uint8_t SrcOp1 = OpInfo.TiedOperands.SrcOpnd1Idx + HasMnemonicFirst;
        uint8_t SrcOp2 = OpInfo.TiedOperands.SrcOpnd2Idx + HasMnemonicFirst;
        assert((i > TiedOp || TiedOp == (uint8_t)-1) &&
               "Tied operand precedes its target!");
        auto TiedTupleName = std::string("Tie") + utostr(TiedOp) + '_' +
                             utostr(SrcOp1) + '_' + utostr(SrcOp2);
        Signature += "__" + TiedTupleName;
        ConversionRow.push_back(CVT_Tied);
        ConversionRow.push_back(TiedOp);
        ConversionRow.push_back(SrcOp1);
        ConversionRow.push_back(SrcOp2);

        // Also create an 'enum' for this combination of tied operands.
        auto Key = std::tuple(TiedOp, SrcOp1, SrcOp2);
        TiedOperandsEnumMap.emplace(Key, TiedTupleName);
        break;
      }
      case MatchableInfo::ResOperand::ImmOperand: {
        int64_t Val = OpInfo.ImmVal;
        std::string Ty = "imm_" + itostr(Val);
        Ty = getEnumNameForToken(Ty);
        Signature += "__" + Ty;

        std::string Name = "CVT_" + Ty;
        bool IsNewConverter = false;
        unsigned ID =
            getConverterOperandID(Name, OperandConversionKinds, IsNewConverter);
        // Add the operand entry to the instruction kind conversion row.
        ConversionRow.push_back(ID);
        ConversionRow.push_back(0);

        if (!IsNewConverter)
          break;

        CvtOS << "    case " << Name << ":\n"
              << "      Inst.addOperand(MCOperand::createImm(" << Val << "));\n"
              << "      break;\n";

        OpOS << "    case " << Name << ":\n"
             << "      Operands[*(p + 1)]->setMCOperandNum(NumMCOperands);\n"
             << "      Operands[*(p + 1)]->setConstraint(\"\");\n"
             << "      ++NumMCOperands;\n"
             << "      break;\n";
        break;
      }
      case MatchableInfo::ResOperand::RegOperand: {
        std::string Reg, Name;
        if (!OpInfo.Register) {
          Name = "reg0";
          Reg = "0";
        } else {
          Reg = getQualifiedName(OpInfo.Register);
          Name = "reg" + OpInfo.Register->getName().str();
        }
        Signature += "__" + Name;
        Name = "CVT_" + Name;
        bool IsNewConverter = false;
        unsigned ID =
            getConverterOperandID(Name, OperandConversionKinds, IsNewConverter);
        // Add the operand entry to the instruction kind conversion row.
        ConversionRow.push_back(ID);
        ConversionRow.push_back(0);

        if (!IsNewConverter)
          break;
        CvtOS << "    case " << Name << ":\n"
              << "      Inst.addOperand(MCOperand::createReg(" << Reg << "));\n"
              << "      break;\n";

        OpOS << "    case " << Name << ":\n"
             << "      Operands[*(p + 1)]->setMCOperandNum(NumMCOperands);\n"
             << "      Operands[*(p + 1)]->setConstraint(\"m\");\n"
             << "      ++NumMCOperands;\n"
             << "      break;\n";
      }
      }
    }

    // If there were no operands, add to the signature to that effect
    if (Signature == "Convert")
      Signature += "_NoOperands";

    II->ConversionFnKind = Signature;

    // Save the signature. If we already have it, don't add a new row
    // to the table.
    if (!InstructionConversionKinds.insert(CachedHashString(Signature)))
      continue;

    // Add the row to the table.
    ConversionTable.push_back(std::move(ConversionRow));
  }

  // Finish up the converter driver function.
  CvtOS << "    }\n  }\n}\n\n";

  // Finish up the operand number lookup function.
  OpOS << "    }\n  }\n}\n\n";

  // Output a static table for tied operands.
  if (TiedOperandsEnumMap.size()) {
    // The number of tied operand combinations will be small in practice,
    // but just add the assert to be sure.
    assert(TiedOperandsEnumMap.size() <= 254 &&
           "Too many tied-operand combinations to reference with "
           "an 8bit offset from the conversion table, where index "
           "'255' is reserved as operand not to be copied.");

    OS << "enum {\n";
    for (auto &KV : TiedOperandsEnumMap) {
      OS << "  " << KV.second << ",\n";
    }
    OS << "};\n\n";

    OS << "static const uint8_t TiedAsmOperandTable[][3] = {\n";
    for (auto &KV : TiedOperandsEnumMap) {
      OS << "  /* " << KV.second << " */ { " << utostr(std::get<0>(KV.first))
         << ", " << utostr(std::get<1>(KV.first)) << ", "
         << utostr(std::get<2>(KV.first)) << " },\n";
    }
    OS << "};\n\n";
  } else
    OS << "static const uint8_t TiedAsmOperandTable[][3] = "
          "{ /* empty  */ {0, 0, 0} };\n\n";

  OS << "namespace {\n";

  // Output the operand conversion kind enum.
  OS << "enum OperatorConversionKind {\n";
  for (const auto &Converter : OperandConversionKinds)
    OS << "  " << Converter << ",\n";
  OS << "  CVT_NUM_CONVERTERS\n";
  OS << "};\n\n";

  // Output the instruction conversion kind enum.
  OS << "enum InstructionConversionKind {\n";
  for (const auto &Signature : InstructionConversionKinds)
    OS << "  " << Signature << ",\n";
  OS << "  CVT_NUM_SIGNATURES\n";
  OS << "};\n\n";

  OS << "} // end anonymous namespace\n\n";

  // Output the conversion table.
  OS << "static const uint8_t ConversionTable[CVT_NUM_SIGNATURES]["
     << MaxRowLength << "] = {\n";

  for (unsigned Row = 0, ERow = ConversionTable.size(); Row != ERow; ++Row) {
    assert(ConversionTable[Row].size() % 2 == 0 && "bad conversion row!");
    OS << "  // " << InstructionConversionKinds[Row] << "\n";
    OS << "  { ";
    for (unsigned i = 0, e = ConversionTable[Row].size(); i != e; i += 2) {
      OS << OperandConversionKinds[ConversionTable[Row][i]] << ", ";
      if (OperandConversionKinds[ConversionTable[Row][i]] !=
          CachedHashString("CVT_Tied")) {
        OS << (unsigned)(ConversionTable[Row][i + 1]) << ", ";
        continue;
      }

      // For a tied operand, emit a reference to the TiedAsmOperandTable
      // that contains the operand to copy, and the parsed operands to
      // check for their tied constraints.
      auto Key = std::tuple((uint8_t)ConversionTable[Row][i + 1],
                            (uint8_t)ConversionTable[Row][i + 2],
                            (uint8_t)ConversionTable[Row][i + 3]);
      auto TiedOpndEnum = TiedOperandsEnumMap.find(Key);
      assert(TiedOpndEnum != TiedOperandsEnumMap.end() &&
             "No record for tied operand pair");
      OS << TiedOpndEnum->second << ", ";
      i += 2;
    }
    OS << "CVT_Done },\n";
  }

  OS << "};\n\n";

  // Spit out the conversion driver function.
  OS << ConvertFnBody;

  // Spit out the operand number lookup function.
  OS << OperandFnBody;

  return ConversionTable.size();
}

/// emitMatchClassEnumeration - Emit the enumeration for match class kinds.
static void emitMatchClassEnumeration(CodeGenTarget &Target,
                                      std::forward_list<ClassInfo> &Infos,
                                      raw_ostream &OS) {
  OS << "namespace {\n\n";

  OS << "/// MatchClassKind - The kinds of classes which participate in\n"
     << "/// instruction matching.\n";
  OS << "enum MatchClassKind {\n";
  OS << "  InvalidMatchClass = 0,\n";
  OS << "  OptionalMatchClass = 1,\n";
  ClassInfo::ClassInfoKind LastKind = ClassInfo::Token;
  StringRef LastName = "OptionalMatchClass";
  for (const auto &CI : Infos) {
    if (LastKind == ClassInfo::Token && CI.Kind != ClassInfo::Token) {
      OS << "  MCK_LAST_TOKEN = " << LastName << ",\n";
    } else if (LastKind < ClassInfo::UserClass0 &&
               CI.Kind >= ClassInfo::UserClass0) {
      OS << "  MCK_LAST_REGISTER = " << LastName << ",\n";
    }
    LastKind = (ClassInfo::ClassInfoKind)CI.Kind;
    LastName = CI.Name;

    OS << "  " << CI.Name << ", // ";
    if (CI.Kind == ClassInfo::Token) {
      OS << "'" << CI.ValueName << "'\n";
    } else if (CI.isRegisterClass()) {
      if (!CI.ValueName.empty())
        OS << "register class '" << CI.ValueName << "'\n";
      else
        OS << "derived register class\n";
    } else {
      OS << "user defined class '" << CI.ValueName << "'\n";
    }
  }
  OS << "  NumMatchClassKinds\n";
  OS << "};\n\n";

  OS << "} // end anonymous namespace\n\n";
}

/// emitMatchClassDiagStrings - Emit a function to get the diagnostic text to be
/// used when an assembly operand does not match the expected operand class.
static void emitOperandMatchErrorDiagStrings(AsmMatcherInfo &Info,
                                             raw_ostream &OS) {
  // If the target does not use DiagnosticString for any operands, don't emit
  // an unused function.
  if (llvm::all_of(Info.Classes, [](const ClassInfo &CI) {
        return CI.DiagnosticString.empty();
      }))
    return;

  OS << "static const char *getMatchKindDiag(" << Info.Target.getName()
     << "AsmParser::" << Info.Target.getName()
     << "MatchResultTy MatchResult) {\n";
  OS << "  switch (MatchResult) {\n";

  for (const auto &CI : Info.Classes) {
    if (!CI.DiagnosticString.empty()) {
      assert(!CI.DiagnosticType.empty() &&
             "DiagnosticString set without DiagnosticType");
      OS << "  case " << Info.Target.getName() << "AsmParser::Match_"
         << CI.DiagnosticType << ":\n";
      OS << "    return \"" << CI.DiagnosticString << "\";\n";
    }
  }

  OS << "  default:\n";
  OS << "    return nullptr;\n";

  OS << "  }\n";
  OS << "}\n\n";
}

static void emitRegisterMatchErrorFunc(AsmMatcherInfo &Info, raw_ostream &OS) {
  OS << "static unsigned getDiagKindFromRegisterClass(MatchClassKind "
        "RegisterClass) {\n";
  if (none_of(Info.Classes, [](const ClassInfo &CI) {
        return CI.isRegisterClass() && !CI.DiagnosticType.empty();
      })) {
    OS << "  return MCTargetAsmParser::Match_InvalidOperand;\n";
  } else {
    OS << "  switch (RegisterClass) {\n";
    for (const auto &CI : Info.Classes) {
      if (CI.isRegisterClass() && !CI.DiagnosticType.empty()) {
        OS << "  case " << CI.Name << ":\n";
        OS << "    return " << Info.Target.getName() << "AsmParser::Match_"
           << CI.DiagnosticType << ";\n";
      }
    }

    OS << "  default:\n";
    OS << "    return MCTargetAsmParser::Match_InvalidOperand;\n";

    OS << "  }\n";
  }
  OS << "}\n\n";
}

/// emitValidateOperandClass - Emit the function to validate an operand class.
static void emitValidateOperandClass(AsmMatcherInfo &Info, raw_ostream &OS) {
  OS << "static unsigned validateOperandClass(MCParsedAsmOperand &GOp, "
     << "MatchClassKind Kind) {\n";
  OS << "  " << Info.Target.getName() << "Operand &Operand = ("
     << Info.Target.getName() << "Operand &)GOp;\n";

  // The InvalidMatchClass is not to match any operand.
  OS << "  if (Kind == InvalidMatchClass)\n";
  OS << "    return MCTargetAsmParser::Match_InvalidOperand;\n\n";

  // Check for Token operands first.
  // FIXME: Use a more specific diagnostic type.
  OS << "  if (Operand.isToken() && Kind <= MCK_LAST_TOKEN)\n";
  OS << "    return isSubclass(matchTokenString(Operand.getToken()), Kind) ?\n"
     << "             MCTargetAsmParser::Match_Success :\n"
     << "             MCTargetAsmParser::Match_InvalidOperand;\n\n";

  // Check the user classes. We don't care what order since we're only
  // actually matching against one of them.
  OS << "  switch (Kind) {\n"
        "  default: break;\n";
  for (const auto &CI : Info.Classes) {
    if (!CI.isUserClass())
      continue;

    OS << "  // '" << CI.ClassName << "' class\n";
    OS << "  case " << CI.Name << ": {\n";
    OS << "    DiagnosticPredicate DP(Operand." << CI.PredicateMethod
       << "());\n";
    OS << "    if (DP.isMatch())\n";
    OS << "      return MCTargetAsmParser::Match_Success;\n";
    if (!CI.DiagnosticType.empty()) {
      OS << "    if (DP.isNearMatch())\n";
      OS << "      return " << Info.Target.getName() << "AsmParser::Match_"
         << CI.DiagnosticType << ";\n";
      OS << "    break;\n";
    } else
      OS << "    break;\n";
    OS << "    }\n";
  }
  OS << "  } // end switch (Kind)\n\n";

  // Check for register operands, including sub-classes.
  OS << "  if (Operand.isReg()) {\n";
  OS << "    MatchClassKind OpKind;\n";
  OS << "    switch (Operand.getReg().id()) {\n";
  OS << "    default: OpKind = InvalidMatchClass; break;\n";
  for (const auto &RC : Info.RegisterClasses)
    OS << "    case " << RC.first->getValueAsString("Namespace")
       << "::" << RC.first->getName() << ": OpKind = " << RC.second->Name
       << "; break;\n";
  OS << "    }\n";
  OS << "    return isSubclass(OpKind, Kind) ? "
     << "(unsigned)MCTargetAsmParser::Match_Success :\n                     "
     << "                 getDiagKindFromRegisterClass(Kind);\n  }\n\n";

  // Expected operand is a register, but actual is not.
  OS << "  if (Kind > MCK_LAST_TOKEN && Kind <= MCK_LAST_REGISTER)\n";
  OS << "    return getDiagKindFromRegisterClass(Kind);\n\n";

  // Generic fallthrough match failure case for operands that don't have
  // specialized diagnostic types.
  OS << "  return MCTargetAsmParser::Match_InvalidOperand;\n";
  OS << "}\n\n";
}

/// emitIsSubclass - Emit the subclass predicate function.
static void emitIsSubclass(CodeGenTarget &Target,
                           std::forward_list<ClassInfo> &Infos,
                           raw_ostream &OS) {
  OS << "/// isSubclass - Compute whether \\p A is a subclass of \\p B.\n";
  OS << "static bool isSubclass(MatchClassKind A, MatchClassKind B) {\n";
  OS << "  if (A == B)\n";
  OS << "    return true;\n\n";

  bool EmittedSwitch = false;
  for (const auto &A : Infos) {
    std::vector<StringRef> SuperClasses;
    if (A.IsOptional)
      SuperClasses.push_back("OptionalMatchClass");
    for (const auto &B : Infos) {
      if (&A != &B && A.isSubsetOf(B))
        SuperClasses.push_back(B.Name);
    }

    if (SuperClasses.empty())
      continue;

    // If this is the first SuperClass, emit the switch header.
    if (!EmittedSwitch) {
      OS << "  switch (A) {\n";
      OS << "  default:\n";
      OS << "    return false;\n";
      EmittedSwitch = true;
    }

    OS << "\n  case " << A.Name << ":\n";

    if (SuperClasses.size() == 1) {
      OS << "    return B == " << SuperClasses.back() << ";\n";
      continue;
    }

    if (!SuperClasses.empty()) {
      OS << "    switch (B) {\n";
      OS << "    default: return false;\n";
      for (StringRef SC : SuperClasses)
        OS << "    case " << SC << ": return true;\n";
      OS << "    }\n";
    } else {
      // No case statement to emit
      OS << "    return false;\n";
    }
  }

  // If there were case statements emitted into the string stream write the
  // default.
  if (EmittedSwitch)
    OS << "  }\n";
  else
    OS << "  return false;\n";

  OS << "}\n\n";
}

/// emitMatchTokenString - Emit the function to match a token string to the
/// appropriate match class value.
static void emitMatchTokenString(CodeGenTarget &Target,
                                 std::forward_list<ClassInfo> &Infos,
                                 raw_ostream &OS) {
  // Construct the match list.
  std::vector<StringMatcher::StringPair> Matches;
  for (const auto &CI : Infos) {
    if (CI.Kind == ClassInfo::Token)
      Matches.emplace_back(CI.ValueName, "return " + CI.Name + ";");
  }

  OS << "static MatchClassKind matchTokenString(StringRef Name) {\n";

  StringMatcher("Name", Matches, OS).Emit();

  OS << "  return InvalidMatchClass;\n";
  OS << "}\n\n";
}

/// emitMatchRegisterName - Emit the function to match a string to the target
/// specific register enum.
static void emitMatchRegisterName(CodeGenTarget &Target, Record *AsmParser,
                                  raw_ostream &OS) {
  // Construct the match list.
  std::vector<StringMatcher::StringPair> Matches;
  const auto &Regs = Target.getRegBank().getRegisters();
  std::string Namespace =
      Regs.front().TheDef->getValueAsString("Namespace").str();
  for (const CodeGenRegister &Reg : Regs) {
    StringRef AsmName = Reg.TheDef->getValueAsString("AsmName");
    if (AsmName.empty())
      continue;

    Matches.emplace_back(AsmName.str(), "return " + Namespace +
                                            "::" + Reg.getName().str() + ';');
  }

  OS << "static MCRegister MatchRegisterName(StringRef Name) {\n";

  bool IgnoreDuplicates =
      AsmParser->getValueAsBit("AllowDuplicateRegisterNames");
  StringMatcher("Name", Matches, OS).Emit(0, IgnoreDuplicates);

  OS << "  return " << Namespace << "::NoRegister;\n";
  OS << "}\n\n";
}

/// Emit the function to match a string to the target
/// specific register enum.
static void emitMatchRegisterAltName(CodeGenTarget &Target, Record *AsmParser,
                                     raw_ostream &OS) {
  // Construct the match list.
  std::vector<StringMatcher::StringPair> Matches;
  const auto &Regs = Target.getRegBank().getRegisters();
  std::string Namespace =
      Regs.front().TheDef->getValueAsString("Namespace").str();
  for (const CodeGenRegister &Reg : Regs) {

    auto AltNames = Reg.TheDef->getValueAsListOfStrings("AltNames");

    for (auto AltName : AltNames) {
      AltName = StringRef(AltName).trim();

      // don't handle empty alternative names
      if (AltName.empty())
        continue;

      Matches.emplace_back(AltName.str(), "return " + Namespace +
                                              "::" + Reg.getName().str() + ';');
    }
  }

  OS << "static MCRegister MatchRegisterAltName(StringRef Name) {\n";

  bool IgnoreDuplicates =
      AsmParser->getValueAsBit("AllowDuplicateRegisterNames");
  StringMatcher("Name", Matches, OS).Emit(0, IgnoreDuplicates);

  OS << "  return " << Namespace << "::NoRegister;\n";
  OS << "}\n\n";
}

/// emitOperandDiagnosticTypes - Emit the operand matching diagnostic types.
static void emitOperandDiagnosticTypes(AsmMatcherInfo &Info, raw_ostream &OS) {
  // Get the set of diagnostic types from all of the operand classes.
  std::set<StringRef> Types;
  for (const auto &OpClassEntry : Info.AsmOperandClasses) {
    if (!OpClassEntry.second->DiagnosticType.empty())
      Types.insert(OpClassEntry.second->DiagnosticType);
  }
  for (const auto &OpClassEntry : Info.RegisterClassClasses) {
    if (!OpClassEntry.second->DiagnosticType.empty())
      Types.insert(OpClassEntry.second->DiagnosticType);
  }

  if (Types.empty())
    return;

  // Now emit the enum entries.
  for (StringRef Type : Types)
    OS << "  Match_" << Type << ",\n";
  OS << "  END_OPERAND_DIAGNOSTIC_TYPES\n";
}

/// emitGetSubtargetFeatureName - Emit the helper function to get the
/// user-level name for a subtarget feature.
static void emitGetSubtargetFeatureName(AsmMatcherInfo &Info, raw_ostream &OS) {
  OS << "// User-level names for subtarget features that participate in\n"
     << "// instruction matching.\n"
     << "static const char *getSubtargetFeatureName(uint64_t Val) {\n";
  if (!Info.SubtargetFeatures.empty()) {
    OS << "  switch(Val) {\n";
    for (const auto &SF : Info.SubtargetFeatures) {
      const SubtargetFeatureInfo &SFI = SF.second;
      // FIXME: Totally just a placeholder name to get the algorithm working.
      OS << "  case " << SFI.getEnumBitName() << ": return \""
         << SFI.TheDef->getValueAsString("PredicateName") << "\";\n";
    }
    OS << "  default: return \"(unknown)\";\n";
    OS << "  }\n";
  } else {
    // Nothing to emit, so skip the switch
    OS << "  return \"(unknown)\";\n";
  }
  OS << "}\n\n";
}

static std::string GetAliasRequiredFeatures(Record *R,
                                            const AsmMatcherInfo &Info) {
  std::vector<Record *> ReqFeatures = R->getValueAsListOfDefs("Predicates");
  std::string Result;

  if (ReqFeatures.empty())
    return Result;

  for (unsigned i = 0, e = ReqFeatures.size(); i != e; ++i) {
    const SubtargetFeatureInfo *F = Info.getSubtargetFeature(ReqFeatures[i]);

    if (!F)
      PrintFatalError(R->getLoc(),
                      "Predicate '" + ReqFeatures[i]->getName() +
                          "' is not marked as an AssemblerPredicate!");

    if (i)
      Result += " && ";

    Result += "Features.test(" + F->getEnumBitName() + ')';
  }

  return Result;
}

static void
emitMnemonicAliasVariant(raw_ostream &OS, const AsmMatcherInfo &Info,
                         std::vector<Record *> &Aliases, unsigned Indent = 0,
                         StringRef AsmParserVariantName = StringRef()) {
  // Keep track of all the aliases from a mnemonic.  Use an std::map so that the
  // iteration order of the map is stable.
  std::map<std::string, std::vector<Record *>> AliasesFromMnemonic;

  for (Record *R : Aliases) {
    // FIXME: Allow AssemblerVariantName to be a comma separated list.
    StringRef AsmVariantName = R->getValueAsString("AsmVariantName");
    if (AsmVariantName != AsmParserVariantName)
      continue;
    AliasesFromMnemonic[R->getValueAsString("FromMnemonic").lower()].push_back(
        R);
  }
  if (AliasesFromMnemonic.empty())
    return;

  // Process each alias a "from" mnemonic at a time, building the code executed
  // by the string remapper.
  std::vector<StringMatcher::StringPair> Cases;
  for (const auto &AliasEntry : AliasesFromMnemonic) {
    const std::vector<Record *> &ToVec = AliasEntry.second;

    // Loop through each alias and emit code that handles each case.  If there
    // are two instructions without predicates, emit an error.  If there is one,
    // emit it last.
    std::string MatchCode;
    int AliasWithNoPredicate = -1;

    for (unsigned i = 0, e = ToVec.size(); i != e; ++i) {
      Record *R = ToVec[i];
      std::string FeatureMask = GetAliasRequiredFeatures(R, Info);

      // If this unconditionally matches, remember it for later and diagnose
      // duplicates.
      if (FeatureMask.empty()) {
        if (AliasWithNoPredicate != -1 &&
            R->getValueAsString("ToMnemonic") !=
                ToVec[AliasWithNoPredicate]->getValueAsString("ToMnemonic")) {
          // We can't have two different aliases from the same mnemonic with no
          // predicate.
          PrintError(
              ToVec[AliasWithNoPredicate]->getLoc(),
              "two different MnemonicAliases with the same 'from' mnemonic!");
          PrintFatalError(R->getLoc(), "this is the other MnemonicAlias.");
        }

        AliasWithNoPredicate = i;
        continue;
      }
      if (R->getValueAsString("ToMnemonic") == AliasEntry.first)
        PrintFatalError(R->getLoc(), "MnemonicAlias to the same string");

      if (!MatchCode.empty())
        MatchCode += "else ";
      MatchCode += "if (" + FeatureMask + ")\n";
      MatchCode += "  Mnemonic = \"";
      MatchCode += R->getValueAsString("ToMnemonic").lower();
      MatchCode += "\";\n";
    }

    if (AliasWithNoPredicate != -1) {
      Record *R = ToVec[AliasWithNoPredicate];
      if (!MatchCode.empty())
        MatchCode += "else\n  ";
      MatchCode += "Mnemonic = \"";
      MatchCode += R->getValueAsString("ToMnemonic").lower();
      MatchCode += "\";\n";
    }

    MatchCode += "return;";

    Cases.push_back(std::pair(AliasEntry.first, MatchCode));
  }
  StringMatcher("Mnemonic", Cases, OS).Emit(Indent);
}

/// emitMnemonicAliases - If the target has any MnemonicAlias<> definitions,
/// emit a function for them and return true, otherwise return false.
static bool emitMnemonicAliases(raw_ostream &OS, const AsmMatcherInfo &Info,
                                CodeGenTarget &Target) {
  // Ignore aliases when match-prefix is set.
  if (!MatchPrefix.empty())
    return false;

  std::vector<Record *> Aliases =
      Info.getRecords().getAllDerivedDefinitions("MnemonicAlias");
  if (Aliases.empty())
    return false;

  OS << "static void applyMnemonicAliases(StringRef &Mnemonic, "
        "const FeatureBitset &Features, unsigned VariantID) {\n";
  OS << "  switch (VariantID) {\n";
  unsigned VariantCount = Target.getAsmParserVariantCount();
  for (unsigned VC = 0; VC != VariantCount; ++VC) {
    Record *AsmVariant = Target.getAsmParserVariant(VC);
    int AsmParserVariantNo = AsmVariant->getValueAsInt("Variant");
    StringRef AsmParserVariantName = AsmVariant->getValueAsString("Name");
    OS << "  case " << AsmParserVariantNo << ":\n";
    emitMnemonicAliasVariant(OS, Info, Aliases, /*Indent=*/2,
                             AsmParserVariantName);
    OS << "    break;\n";
  }
  OS << "  }\n";

  // Emit aliases that apply to all variants.
  emitMnemonicAliasVariant(OS, Info, Aliases);

  OS << "}\n\n";

  return true;
}

static void
emitCustomOperandParsing(raw_ostream &OS, CodeGenTarget &Target,
                         const AsmMatcherInfo &Info, StringRef ClassName,
                         StringToOffsetTable &StringTable,
                         unsigned MaxMnemonicIndex, unsigned MaxFeaturesIndex,
                         bool HasMnemonicFirst, const Record &AsmParser) {
  unsigned MaxMask = 0;
  for (const OperandMatchEntry &OMI : Info.OperandMatchInfo) {
    MaxMask |= OMI.OperandMask;
  }

  // Emit the static custom operand parsing table;
  OS << "namespace {\n";
  OS << "  struct OperandMatchEntry {\n";
  OS << "    " << getMinimalTypeForRange(MaxMnemonicIndex) << " Mnemonic;\n";
  OS << "    " << getMinimalTypeForRange(MaxMask) << " OperandMask;\n";
  OS << "    "
     << getMinimalTypeForRange(
            std::distance(Info.Classes.begin(), Info.Classes.end()) +
            2 /* Include 'InvalidMatchClass' and 'OptionalMatchClass' */)
     << " Class;\n";
  OS << "    " << getMinimalTypeForRange(MaxFeaturesIndex)
     << " RequiredFeaturesIdx;\n\n";
  OS << "    StringRef getMnemonic() const {\n";
  OS << "      return StringRef(MnemonicTable + Mnemonic + 1,\n";
  OS << "                       MnemonicTable[Mnemonic]);\n";
  OS << "    }\n";
  OS << "  };\n\n";

  OS << "  // Predicate for searching for an opcode.\n";
  OS << "  struct LessOpcodeOperand {\n";
  OS << "    bool operator()(const OperandMatchEntry &LHS, StringRef RHS) {\n";
  OS << "      return LHS.getMnemonic()  < RHS;\n";
  OS << "    }\n";
  OS << "    bool operator()(StringRef LHS, const OperandMatchEntry &RHS) {\n";
  OS << "      return LHS < RHS.getMnemonic();\n";
  OS << "    }\n";
  OS << "    bool operator()(const OperandMatchEntry &LHS,";
  OS << " const OperandMatchEntry &RHS) {\n";
  OS << "      return LHS.getMnemonic() < RHS.getMnemonic();\n";
  OS << "    }\n";
  OS << "  };\n";

  OS << "} // end anonymous namespace\n\n";

  OS << "static const OperandMatchEntry OperandMatchTable["
     << Info.OperandMatchInfo.size() << "] = {\n";

  OS << "  /* Operand List Mnemonic, Mask, Operand Class, Features */\n";
  for (const OperandMatchEntry &OMI : Info.OperandMatchInfo) {
    const MatchableInfo &II = *OMI.MI;

    OS << "  { ";

    // Store a pascal-style length byte in the mnemonic.
    std::string LenMnemonic = char(II.Mnemonic.size()) + II.Mnemonic.lower();
    OS << StringTable.GetOrAddStringOffset(LenMnemonic, false) << " /* "
       << II.Mnemonic << " */, ";

    OS << OMI.OperandMask;
    OS << " /* ";
    ListSeparator LS;
    for (int i = 0, e = 31; i != e; ++i)
      if (OMI.OperandMask & (1 << i))
        OS << LS << i;
    OS << " */, ";

    OS << OMI.CI->Name;

    // Write the required features mask.
    OS << ", AMFBS";
    if (II.RequiredFeatures.empty())
      OS << "_None";
    else
      for (unsigned i = 0, e = II.RequiredFeatures.size(); i != e; ++i)
        OS << '_' << II.RequiredFeatures[i]->TheDef->getName();

    OS << " },\n";
  }
  OS << "};\n\n";

  // Emit the operand class switch to call the correct custom parser for
  // the found operand class.
  OS << "ParseStatus " << Target.getName() << ClassName << "::\n"
     << "tryCustomParseOperand(OperandVector"
     << " &Operands,\n                      unsigned MCK) {\n\n"
     << "  switch(MCK) {\n";

  for (const auto &CI : Info.Classes) {
    if (CI.ParserMethod.empty())
      continue;
    OS << "  case " << CI.Name << ":\n"
       << "    return " << CI.ParserMethod << "(Operands);\n";
  }

  OS << "  default:\n";
  OS << "    return ParseStatus::NoMatch;\n";
  OS << "  }\n";
  OS << "  return ParseStatus::NoMatch;\n";
  OS << "}\n\n";

  // Emit the static custom operand parser. This code is very similar with
  // the other matcher. Also use MatchResultTy here just in case we go for
  // a better error handling.
  OS << "ParseStatus " << Target.getName() << ClassName << "::\n"
     << "MatchOperandParserImpl(OperandVector"
     << " &Operands,\n                       StringRef Mnemonic,\n"
     << "                       bool ParseForAllFeatures) {\n";

  // Emit code to get the available features.
  OS << "  // Get the current feature set.\n";
  OS << "  const FeatureBitset &AvailableFeatures = "
        "getAvailableFeatures();\n\n";

  OS << "  // Get the next operand index.\n";
  OS << "  unsigned NextOpNum = Operands.size()"
     << (HasMnemonicFirst ? " - 1" : "") << ";\n";

  // Emit code to search the table.
  OS << "  // Search the table.\n";
  if (HasMnemonicFirst) {
    OS << "  auto MnemonicRange =\n";
    OS << "    std::equal_range(std::begin(OperandMatchTable), "
          "std::end(OperandMatchTable),\n";
    OS << "                     Mnemonic, LessOpcodeOperand());\n\n";
  } else {
    OS << "  auto MnemonicRange = std::pair(std::begin(OperandMatchTable),"
          " std::end(OperandMatchTable));\n";
    OS << "  if (!Mnemonic.empty())\n";
    OS << "    MnemonicRange =\n";
    OS << "      std::equal_range(std::begin(OperandMatchTable), "
          "std::end(OperandMatchTable),\n";
    OS << "                       Mnemonic, LessOpcodeOperand());\n\n";
  }

  OS << "  if (MnemonicRange.first == MnemonicRange.second)\n";
  OS << "    return ParseStatus::NoMatch;\n\n";

  OS << "  for (const OperandMatchEntry *it = MnemonicRange.first,\n"
     << "       *ie = MnemonicRange.second; it != ie; ++it) {\n";

  OS << "    // equal_range guarantees that instruction mnemonic matches.\n";
  OS << "    assert(Mnemonic == it->getMnemonic());\n\n";

  // Emit check that the required features are available.
  OS << "    // check if the available features match\n";
  OS << "    const FeatureBitset &RequiredFeatures = "
        "FeatureBitsets[it->RequiredFeaturesIdx];\n";
  OS << "    if (!ParseForAllFeatures && (AvailableFeatures & "
        "RequiredFeatures) != RequiredFeatures)\n";
  OS << "      continue;\n\n";

  // Emit check to ensure the operand number matches.
  OS << "    // check if the operand in question has a custom parser.\n";
  OS << "    if (!(it->OperandMask & (1 << NextOpNum)))\n";
  OS << "      continue;\n\n";

  // Emit call to the custom parser method
  StringRef ParserName = AsmParser.getValueAsString("OperandParserMethod");
  if (ParserName.empty())
    ParserName = "tryCustomParseOperand";
  OS << "    // call custom parse method to handle the operand\n";
  OS << "    ParseStatus Result = " << ParserName << "(Operands, it->Class);\n";
  OS << "    if (!Result.isNoMatch())\n";
  OS << "      return Result;\n";
  OS << "  }\n\n";

  OS << "  // Okay, we had no match.\n";
  OS << "  return ParseStatus::NoMatch;\n";
  OS << "}\n\n";
}

static void emitAsmTiedOperandConstraints(CodeGenTarget &Target,
                                          AsmMatcherInfo &Info, raw_ostream &OS,
                                          bool HasOptionalOperands) {
  std::string AsmParserName =
      std::string(Info.AsmParser->getValueAsString("AsmParserClassName"));
  OS << "static bool ";
  OS << "checkAsmTiedOperandConstraints(const " << Target.getName()
     << AsmParserName << "&AsmParser,\n";
  OS << "                               unsigned Kind, const OperandVector "
        "&Operands,\n";
  if (HasOptionalOperands)
    OS << "                               ArrayRef<unsigned> DefaultsOffset,\n";
  OS << "                               uint64_t &ErrorInfo) {\n";
  OS << "  assert(Kind < CVT_NUM_SIGNATURES && \"Invalid signature!\");\n";
  OS << "  const uint8_t *Converter = ConversionTable[Kind];\n";
  OS << "  for (const uint8_t *p = Converter; *p; p += 2) {\n";
  OS << "    switch (*p) {\n";
  OS << "    case CVT_Tied: {\n";
  OS << "      unsigned OpIdx = *(p + 1);\n";
  OS << "      assert(OpIdx < (size_t)(std::end(TiedAsmOperandTable) -\n";
  OS << "                              std::begin(TiedAsmOperandTable)) &&\n";
  OS << "             \"Tied operand not found\");\n";
  OS << "      unsigned OpndNum1 = TiedAsmOperandTable[OpIdx][1];\n";
  OS << "      unsigned OpndNum2 = TiedAsmOperandTable[OpIdx][2];\n";
  if (HasOptionalOperands) {
    // When optional operands are involved, formal and actual operand indices
    // may differ. Map the former to the latter by subtracting the number of
    // absent optional operands.
    OS << "      OpndNum1 = OpndNum1 - DefaultsOffset[OpndNum1];\n";
    OS << "      OpndNum2 = OpndNum2 - DefaultsOffset[OpndNum2];\n";
  }
  OS << "      if (OpndNum1 != OpndNum2) {\n";
  OS << "        auto &SrcOp1 = Operands[OpndNum1];\n";
  OS << "        auto &SrcOp2 = Operands[OpndNum2];\n";
  OS << "        if (!AsmParser.areEqualRegs(*SrcOp1, *SrcOp2)) {\n";
  OS << "          ErrorInfo = OpndNum2;\n";
  OS << "          return false;\n";
  OS << "        }\n";
  OS << "      }\n";
  OS << "      break;\n";
  OS << "    }\n";
  OS << "    default:\n";
  OS << "      break;\n";
  OS << "    }\n";
  OS << "  }\n";
  OS << "  return true;\n";
  OS << "}\n\n";
}

static void emitMnemonicSpellChecker(raw_ostream &OS, CodeGenTarget &Target,
                                     unsigned VariantCount) {
  OS << "static std::string " << Target.getName()
     << "MnemonicSpellCheck(StringRef S, const FeatureBitset &FBS,"
     << " unsigned VariantID) {\n";
  if (!VariantCount)
    OS << "  return \"\";";
  else {
    OS << "  const unsigned MaxEditDist = 2;\n";
    OS << "  std::vector<StringRef> Candidates;\n";
    OS << "  StringRef Prev = \"\";\n\n";

    OS << "  // Find the appropriate table for this asm variant.\n";
    OS << "  const MatchEntry *Start, *End;\n";
    OS << "  switch (VariantID) {\n";
    OS << "  default: llvm_unreachable(\"invalid variant!\");\n";
    for (unsigned VC = 0; VC != VariantCount; ++VC) {
      Record *AsmVariant = Target.getAsmParserVariant(VC);
      int AsmVariantNo = AsmVariant->getValueAsInt("Variant");
      OS << "  case " << AsmVariantNo << ": Start = std::begin(MatchTable" << VC
         << "); End = std::end(MatchTable" << VC << "); break;\n";
    }
    OS << "  }\n\n";
    OS << "  for (auto I = Start; I < End; I++) {\n";
    OS << "    // Ignore unsupported instructions.\n";
    OS << "    const FeatureBitset &RequiredFeatures = "
          "FeatureBitsets[I->RequiredFeaturesIdx];\n";
    OS << "    if ((FBS & RequiredFeatures) != RequiredFeatures)\n";
    OS << "      continue;\n";
    OS << "\n";
    OS << "    StringRef T = I->getMnemonic();\n";
    OS << "    // Avoid recomputing the edit distance for the same string.\n";
    OS << "    if (T == Prev)\n";
    OS << "      continue;\n";
    OS << "\n";
    OS << "    Prev = T;\n";
    OS << "    unsigned Dist = S.edit_distance(T, false, MaxEditDist);\n";
    OS << "    if (Dist <= MaxEditDist)\n";
    OS << "      Candidates.push_back(T);\n";
    OS << "  }\n";
    OS << "\n";
    OS << "  if (Candidates.empty())\n";
    OS << "    return \"\";\n";
    OS << "\n";
    OS << "  std::string Res = \", did you mean: \";\n";
    OS << "  unsigned i = 0;\n";
    OS << "  for (; i < Candidates.size() - 1; i++)\n";
    OS << "    Res += Candidates[i].str() + \", \";\n";
    OS << "  return Res + Candidates[i].str() + \"?\";\n";
  }
  OS << "}\n";
  OS << "\n";
}

static void emitMnemonicChecker(raw_ostream &OS, CodeGenTarget &Target,
                                unsigned VariantCount, bool HasMnemonicFirst,
                                bool HasMnemonicAliases) {
  OS << "static bool " << Target.getName()
     << "CheckMnemonic(StringRef Mnemonic,\n";
  OS << "                                "
     << "const FeatureBitset &AvailableFeatures,\n";
  OS << "                                "
     << "unsigned VariantID) {\n";

  if (!VariantCount) {
    OS << "  return false;\n";
  } else {
    if (HasMnemonicAliases) {
      OS << "  // Process all MnemonicAliases to remap the mnemonic.\n";
      OS << "  applyMnemonicAliases(Mnemonic, AvailableFeatures, VariantID);";
      OS << "\n\n";
    }
    OS << "  // Find the appropriate table for this asm variant.\n";
    OS << "  const MatchEntry *Start, *End;\n";
    OS << "  switch (VariantID) {\n";
    OS << "  default: llvm_unreachable(\"invalid variant!\");\n";
    for (unsigned VC = 0; VC != VariantCount; ++VC) {
      Record *AsmVariant = Target.getAsmParserVariant(VC);
      int AsmVariantNo = AsmVariant->getValueAsInt("Variant");
      OS << "  case " << AsmVariantNo << ": Start = std::begin(MatchTable" << VC
         << "); End = std::end(MatchTable" << VC << "); break;\n";
    }
    OS << "  }\n\n";

    OS << "  // Search the table.\n";
    if (HasMnemonicFirst) {
      OS << "  auto MnemonicRange = "
            "std::equal_range(Start, End, Mnemonic, LessOpcode());\n\n";
    } else {
      OS << "  auto MnemonicRange = std::pair(Start, End);\n";
      OS << "  unsigned SIndex = Mnemonic.empty() ? 0 : 1;\n";
      OS << "  if (!Mnemonic.empty())\n";
      OS << "    MnemonicRange = "
         << "std::equal_range(Start, End, Mnemonic.lower(), LessOpcode());\n\n";
    }

    OS << "  if (MnemonicRange.first == MnemonicRange.second)\n";
    OS << "    return false;\n\n";

    OS << "  for (const MatchEntry *it = MnemonicRange.first, "
       << "*ie = MnemonicRange.second;\n";
    OS << "       it != ie; ++it) {\n";
    OS << "    const FeatureBitset &RequiredFeatures =\n";
    OS << "      FeatureBitsets[it->RequiredFeaturesIdx];\n";
    OS << "    if ((AvailableFeatures & RequiredFeatures) == ";
    OS << "RequiredFeatures)\n";
    OS << "      return true;\n";
    OS << "  }\n";
    OS << "  return false;\n";
  }
  OS << "}\n";
  OS << "\n";
}

// Emit a function mapping match classes to strings, for debugging.
static void emitMatchClassKindNames(std::forward_list<ClassInfo> &Infos,
                                    raw_ostream &OS) {
  OS << "#ifndef NDEBUG\n";
  OS << "const char *getMatchClassName(MatchClassKind Kind) {\n";
  OS << "  switch (Kind) {\n";

  OS << "  case InvalidMatchClass: return \"InvalidMatchClass\";\n";
  OS << "  case OptionalMatchClass: return \"OptionalMatchClass\";\n";
  for (const auto &CI : Infos) {
    OS << "  case " << CI.Name << ": return \"" << CI.Name << "\";\n";
  }
  OS << "  case NumMatchClassKinds: return \"NumMatchClassKinds\";\n";

  OS << "  }\n";
  OS << "  llvm_unreachable(\"unhandled MatchClassKind!\");\n";
  OS << "}\n\n";
  OS << "#endif // NDEBUG\n";
}

static std::string
getNameForFeatureBitset(const std::vector<Record *> &FeatureBitset) {
  std::string Name = "AMFBS";
  for (const auto &Feature : FeatureBitset)
    Name += ("_" + Feature->getName()).str();
  return Name;
}

void AsmMatcherEmitter::run(raw_ostream &OS) {
  CodeGenTarget Target(Records);
  Record *AsmParser = Target.getAsmParser();
  StringRef ClassName = AsmParser->getValueAsString("AsmParserClassName");

  emitSourceFileHeader("Assembly Matcher Source Fragment", OS, Records);

  // Compute the information on the instructions to match.
  AsmMatcherInfo Info(AsmParser, Target, Records);
  Info.buildInfo();

  bool PreferSmallerInstructions = getPreferSmallerInstructions(Target);
  // Sort the instruction table using the partial order on classes. We use
  // stable_sort to ensure that ambiguous instructions are still
  // deterministically ordered.
  llvm::stable_sort(
      Info.Matchables,
      [PreferSmallerInstructions](const std::unique_ptr<MatchableInfo> &A,
                                  const std::unique_ptr<MatchableInfo> &B) {
        return A->shouldBeMatchedBefore(*B, PreferSmallerInstructions);
      });

#ifdef EXPENSIVE_CHECKS
  // Verify that the table is sorted and operator < works transitively.
  for (auto I = Info.Matchables.begin(), E = Info.Matchables.end(); I != E;
       ++I) {
    for (auto J = I; J != E; ++J) {
      assert(!(*J)->shouldBeMatchedBefore(**I, PreferSmallerInstructions));
    }
  }
#endif

  DEBUG_WITH_TYPE("instruction_info", {
    for (const auto &MI : Info.Matchables)
      MI->dump();
  });

  // Check for ambiguous matchables.
  DEBUG_WITH_TYPE("ambiguous_instrs", {
    unsigned NumAmbiguous = 0;
    for (auto I = Info.Matchables.begin(), E = Info.Matchables.end(); I != E;
         ++I) {
      for (auto J = std::next(I); J != E; ++J) {
        const MatchableInfo &A = **I;
        const MatchableInfo &B = **J;

        if (A.couldMatchAmbiguouslyWith(B, PreferSmallerInstructions)) {
          errs() << "warning: ambiguous matchables:\n";
          A.dump();
          errs() << "\nis incomparable with:\n";
          B.dump();
          errs() << "\n\n";
          ++NumAmbiguous;
        }
      }
    }
    if (NumAmbiguous)
      errs() << "warning: " << NumAmbiguous << " ambiguous matchables!\n";
  });

  // Compute the information on the custom operand parsing.
  Info.buildOperandMatchInfo();

  bool HasMnemonicFirst = AsmParser->getValueAsBit("HasMnemonicFirst");
  bool HasOptionalOperands = Info.hasOptionalOperands();
  bool ReportMultipleNearMisses =
      AsmParser->getValueAsBit("ReportMultipleNearMisses");

  // Write the output.

  // Information for the class declaration.
  OS << "\n#ifdef GET_ASSEMBLER_HEADER\n";
  OS << "#undef GET_ASSEMBLER_HEADER\n";
  OS << "  // This should be included into the middle of the declaration of\n";
  OS << "  // your subclasses implementation of MCTargetAsmParser.\n";
  OS << "  FeatureBitset ComputeAvailableFeatures(const FeatureBitset &FB) "
        "const;\n";
  if (HasOptionalOperands) {
    OS << "  void convertToMCInst(unsigned Kind, MCInst &Inst, "
       << "unsigned Opcode,\n"
       << "                       const OperandVector &Operands,\n"
       << "                       const SmallBitVector "
          "&OptionalOperandsMask,\n"
       << "                       ArrayRef<unsigned> DefaultsOffset);\n";
  } else {
    OS << "  void convertToMCInst(unsigned Kind, MCInst &Inst, "
       << "unsigned Opcode,\n"
       << "                       const OperandVector &Operands);\n";
  }
  OS << "  void convertToMapAndConstraints(unsigned Kind,\n                ";
  OS << "           const OperandVector &Operands) override;\n";
  OS << "  unsigned MatchInstructionImpl(const OperandVector &Operands,\n"
     << "                                MCInst &Inst,\n";
  if (ReportMultipleNearMisses)
    OS << "                                SmallVectorImpl<NearMissInfo> "
          "*NearMisses,\n";
  else
    OS << "                                uint64_t &ErrorInfo,\n"
       << "                                FeatureBitset &MissingFeatures,\n";
  OS << "                                bool matchingInlineAsm,\n"
     << "                                unsigned VariantID = 0);\n";
  if (!ReportMultipleNearMisses)
    OS << "  unsigned MatchInstructionImpl(const OperandVector &Operands,\n"
       << "                                MCInst &Inst,\n"
       << "                                uint64_t &ErrorInfo,\n"
       << "                                bool matchingInlineAsm,\n"
       << "                                unsigned VariantID = 0) {\n"
       << "    FeatureBitset MissingFeatures;\n"
       << "    return MatchInstructionImpl(Operands, Inst, ErrorInfo, "
          "MissingFeatures,\n"
       << "                                matchingInlineAsm, VariantID);\n"
       << "  }\n\n";

  if (!Info.OperandMatchInfo.empty()) {
    OS << "  ParseStatus MatchOperandParserImpl(\n";
    OS << "    OperandVector &Operands,\n";
    OS << "    StringRef Mnemonic,\n";
    OS << "    bool ParseForAllFeatures = false);\n";

    OS << "  ParseStatus tryCustomParseOperand(\n";
    OS << "    OperandVector &Operands,\n";
    OS << "    unsigned MCK);\n\n";
  }

  OS << "#endif // GET_ASSEMBLER_HEADER\n\n";

  // Emit the operand match diagnostic enum names.
  OS << "\n#ifdef GET_OPERAND_DIAGNOSTIC_TYPES\n";
  OS << "#undef GET_OPERAND_DIAGNOSTIC_TYPES\n\n";
  emitOperandDiagnosticTypes(Info, OS);
  OS << "#endif // GET_OPERAND_DIAGNOSTIC_TYPES\n\n";

  OS << "\n#ifdef GET_REGISTER_MATCHER\n";
  OS << "#undef GET_REGISTER_MATCHER\n\n";

  // Emit the subtarget feature enumeration.
  SubtargetFeatureInfo::emitSubtargetFeatureBitEnumeration(
      Info.SubtargetFeatures, OS);

  // Emit the function to match a register name to number.
  // This should be omitted for Mips target
  if (AsmParser->getValueAsBit("ShouldEmitMatchRegisterName"))
    emitMatchRegisterName(Target, AsmParser, OS);

  if (AsmParser->getValueAsBit("ShouldEmitMatchRegisterAltName"))
    emitMatchRegisterAltName(Target, AsmParser, OS);

  OS << "#endif // GET_REGISTER_MATCHER\n\n";

  OS << "\n#ifdef GET_SUBTARGET_FEATURE_NAME\n";
  OS << "#undef GET_SUBTARGET_FEATURE_NAME\n\n";

  // Generate the helper function to get the names for subtarget features.
  emitGetSubtargetFeatureName(Info, OS);

  OS << "#endif // GET_SUBTARGET_FEATURE_NAME\n\n";

  OS << "\n#ifdef GET_MATCHER_IMPLEMENTATION\n";
  OS << "#undef GET_MATCHER_IMPLEMENTATION\n\n";

  // Generate the function that remaps for mnemonic aliases.
  bool HasMnemonicAliases = emitMnemonicAliases(OS, Info, Target);

  // Generate the convertToMCInst function to convert operands into an MCInst.
  // Also, generate the convertToMapAndConstraints function for MS-style inline
  // assembly.  The latter doesn't actually generate a MCInst.
  unsigned NumConverters =
      emitConvertFuncs(Target, ClassName, Info.Matchables, HasMnemonicFirst,
                       HasOptionalOperands, OS);

  // Emit the enumeration for classes which participate in matching.
  emitMatchClassEnumeration(Target, Info.Classes, OS);

  // Emit a function to get the user-visible string to describe an operand
  // match failure in diagnostics.
  emitOperandMatchErrorDiagStrings(Info, OS);

  // Emit a function to map register classes to operand match failure codes.
  emitRegisterMatchErrorFunc(Info, OS);

  // Emit the routine to match token strings to their match class.
  emitMatchTokenString(Target, Info.Classes, OS);

  // Emit the subclass predicate routine.
  emitIsSubclass(Target, Info.Classes, OS);

  // Emit the routine to validate an operand against a match class.
  emitValidateOperandClass(Info, OS);

  emitMatchClassKindNames(Info.Classes, OS);

  // Emit the available features compute function.
  SubtargetFeatureInfo::emitComputeAssemblerAvailableFeatures(
      Info.Target.getName(), ClassName, "ComputeAvailableFeatures",
      Info.SubtargetFeatures, OS);

  if (!ReportMultipleNearMisses)
    emitAsmTiedOperandConstraints(Target, Info, OS, HasOptionalOperands);

  StringToOffsetTable StringTable;

  size_t MaxNumOperands = 0;
  unsigned MaxMnemonicIndex = 0;
  bool HasDeprecation = false;
  for (const auto &MI : Info.Matchables) {
    MaxNumOperands = std::max(MaxNumOperands, MI->AsmOperands.size());
    HasDeprecation |= MI->HasDeprecation;

    // Store a pascal-style length byte in the mnemonic.
    std::string LenMnemonic = char(MI->Mnemonic.size()) + MI->Mnemonic.lower();
    MaxMnemonicIndex = std::max(
        MaxMnemonicIndex, StringTable.GetOrAddStringOffset(LenMnemonic, false));
  }

  OS << "static const char MnemonicTable[] =\n";
  StringTable.EmitString(OS);
  OS << ";\n\n";

  std::vector<std::vector<Record *>> FeatureBitsets;
  for (const auto &MI : Info.Matchables) {
    if (MI->RequiredFeatures.empty())
      continue;
    FeatureBitsets.emplace_back();
    for (unsigned I = 0, E = MI->RequiredFeatures.size(); I != E; ++I)
      FeatureBitsets.back().push_back(MI->RequiredFeatures[I]->TheDef);
  }

  llvm::sort(FeatureBitsets, [&](const std::vector<Record *> &A,
                                 const std::vector<Record *> &B) {
    if (A.size() < B.size())
      return true;
    if (A.size() > B.size())
      return false;
    for (auto Pair : zip(A, B)) {
      if (std::get<0>(Pair)->getName() < std::get<1>(Pair)->getName())
        return true;
      if (std::get<0>(Pair)->getName() > std::get<1>(Pair)->getName())
        return false;
    }
    return false;
  });
  FeatureBitsets.erase(llvm::unique(FeatureBitsets), FeatureBitsets.end());
  OS << "// Feature bitsets.\n"
     << "enum : " << getMinimalTypeForRange(FeatureBitsets.size()) << " {\n"
     << "  AMFBS_None,\n";
  for (const auto &FeatureBitset : FeatureBitsets) {
    if (FeatureBitset.empty())
      continue;
    OS << "  " << getNameForFeatureBitset(FeatureBitset) << ",\n";
  }
  OS << "};\n\n"
     << "static constexpr FeatureBitset FeatureBitsets[] = {\n"
     << "  {}, // AMFBS_None\n";
  for (const auto &FeatureBitset : FeatureBitsets) {
    if (FeatureBitset.empty())
      continue;
    OS << "  {";
    for (const auto &Feature : FeatureBitset) {
      const auto &I = Info.SubtargetFeatures.find(Feature);
      assert(I != Info.SubtargetFeatures.end() && "Didn't import predicate?");
      OS << I->second.getEnumBitName() << ", ";
    }
    OS << "},\n";
  }
  OS << "};\n\n";

  // Emit the static match table; unused classes get initialized to 0 which is
  // guaranteed to be InvalidMatchClass.
  //
  // FIXME: We can reduce the size of this table very easily. First, we change
  // it so that store the kinds in separate bit-fields for each index, which
  // only needs to be the max width used for classes at that index (we also need
  // to reject based on this during classification). If we then make sure to
  // order the match kinds appropriately (putting mnemonics last), then we
  // should only end up using a few bits for each class, especially the ones
  // following the mnemonic.
  OS << "namespace {\n";
  OS << "  struct MatchEntry {\n";
  OS << "    " << getMinimalTypeForRange(MaxMnemonicIndex) << " Mnemonic;\n";
  OS << "    uint16_t Opcode;\n";
  OS << "    " << getMinimalTypeForRange(NumConverters) << " ConvertFn;\n";
  OS << "    " << getMinimalTypeForRange(FeatureBitsets.size())
     << " RequiredFeaturesIdx;\n";
  OS << "    "
     << getMinimalTypeForRange(
            std::distance(Info.Classes.begin(), Info.Classes.end()) +
            2 /* Include 'InvalidMatchClass' and 'OptionalMatchClass' */)
     << " Classes[" << MaxNumOperands << "];\n";
  OS << "    StringRef getMnemonic() const {\n";
  OS << "      return StringRef(MnemonicTable + Mnemonic + 1,\n";
  OS << "                       MnemonicTable[Mnemonic]);\n";
  OS << "    }\n";
  OS << "  };\n\n";

  OS << "  // Predicate for searching for an opcode.\n";
  OS << "  struct LessOpcode {\n";
  OS << "    bool operator()(const MatchEntry &LHS, StringRef RHS) {\n";
  OS << "      return LHS.getMnemonic() < RHS;\n";
  OS << "    }\n";
  OS << "    bool operator()(StringRef LHS, const MatchEntry &RHS) {\n";
  OS << "      return LHS < RHS.getMnemonic();\n";
  OS << "    }\n";
  OS << "    bool operator()(const MatchEntry &LHS, const MatchEntry &RHS) {\n";
  OS << "      return LHS.getMnemonic() < RHS.getMnemonic();\n";
  OS << "    }\n";
  OS << "  };\n";

  OS << "} // end anonymous namespace\n\n";

  unsigned VariantCount = Target.getAsmParserVariantCount();
  for (unsigned VC = 0; VC != VariantCount; ++VC) {
    Record *AsmVariant = Target.getAsmParserVariant(VC);
    int AsmVariantNo = AsmVariant->getValueAsInt("Variant");

    OS << "static const MatchEntry MatchTable" << VC << "[] = {\n";

    for (const auto &MI : Info.Matchables) {
      if (MI->AsmVariantID != AsmVariantNo)
        continue;

      // Store a pascal-style length byte in the mnemonic.
      std::string LenMnemonic =
          char(MI->Mnemonic.size()) + MI->Mnemonic.lower();
      OS << "  { " << StringTable.GetOrAddStringOffset(LenMnemonic, false)
         << " /* " << MI->Mnemonic << " */, " << Target.getInstNamespace()
         << "::" << MI->getResultInst()->TheDef->getName() << ", "
         << MI->ConversionFnKind << ", ";

      // Write the required features mask.
      OS << "AMFBS";
      if (MI->RequiredFeatures.empty())
        OS << "_None";
      else
        for (unsigned i = 0, e = MI->RequiredFeatures.size(); i != e; ++i)
          OS << '_' << MI->RequiredFeatures[i]->TheDef->getName();

      OS << ", { ";
      ListSeparator LS;
      for (const MatchableInfo::AsmOperand &Op : MI->AsmOperands)
        OS << LS << Op.Class->Name;
      OS << " }, },\n";
    }

    OS << "};\n\n";
  }

  OS << "#include \"llvm/Support/Debug.h\"\n";
  OS << "#include \"llvm/Support/Format.h\"\n\n";

  // Finally, build the match function.
  OS << "unsigned " << Target.getName() << ClassName << "::\n"
     << "MatchInstructionImpl(const OperandVector &Operands,\n";
  OS << "                     MCInst &Inst,\n";
  if (ReportMultipleNearMisses)
    OS << "                     SmallVectorImpl<NearMissInfo> *NearMisses,\n";
  else
    OS << "                     uint64_t &ErrorInfo,\n"
       << "                     FeatureBitset &MissingFeatures,\n";
  OS << "                     bool matchingInlineAsm, unsigned VariantID) {\n";

  if (!ReportMultipleNearMisses) {
    OS << "  // Eliminate obvious mismatches.\n";
    OS << "  if (Operands.size() > " << (MaxNumOperands + HasMnemonicFirst)
       << ") {\n";
    OS << "    ErrorInfo = " << (MaxNumOperands + HasMnemonicFirst) << ";\n";
    OS << "    return Match_InvalidOperand;\n";
    OS << "  }\n\n";
  }

  // Emit code to get the available features.
  OS << "  // Get the current feature set.\n";
  OS << "  const FeatureBitset &AvailableFeatures = "
        "getAvailableFeatures();\n\n";

  OS << "  // Get the instruction mnemonic, which is the first token.\n";
  if (HasMnemonicFirst) {
    OS << "  StringRef Mnemonic = ((" << Target.getName()
       << "Operand &)*Operands[0]).getToken();\n\n";
  } else {
    OS << "  StringRef Mnemonic;\n";
    OS << "  if (Operands[0]->isToken())\n";
    OS << "    Mnemonic = ((" << Target.getName()
       << "Operand &)*Operands[0]).getToken();\n\n";
  }

  if (HasMnemonicAliases) {
    OS << "  // Process all MnemonicAliases to remap the mnemonic.\n";
    OS << "  applyMnemonicAliases(Mnemonic, AvailableFeatures, VariantID);\n\n";
  }

  // Emit code to compute the class list for this operand vector.
  if (!ReportMultipleNearMisses) {
    OS << "  // Some state to try to produce better error messages.\n";
    OS << "  bool HadMatchOtherThanFeatures = false;\n";
    OS << "  bool HadMatchOtherThanPredicate = false;\n";
    OS << "  unsigned RetCode = Match_InvalidOperand;\n";
    OS << "  MissingFeatures.set();\n";
    OS << "  // Set ErrorInfo to the operand that mismatches if it is\n";
    OS << "  // wrong for all instances of the instruction.\n";
    OS << "  ErrorInfo = ~0ULL;\n";
  }

  if (HasOptionalOperands) {
    OS << "  SmallBitVector OptionalOperandsMask(" << MaxNumOperands << ");\n";
  }

  // Emit code to search the table.
  OS << "  // Find the appropriate table for this asm variant.\n";
  OS << "  const MatchEntry *Start, *End;\n";
  OS << "  switch (VariantID) {\n";
  OS << "  default: llvm_unreachable(\"invalid variant!\");\n";
  for (unsigned VC = 0; VC != VariantCount; ++VC) {
    Record *AsmVariant = Target.getAsmParserVariant(VC);
    int AsmVariantNo = AsmVariant->getValueAsInt("Variant");
    OS << "  case " << AsmVariantNo << ": Start = std::begin(MatchTable" << VC
       << "); End = std::end(MatchTable" << VC << "); break;\n";
  }
  OS << "  }\n";

  OS << "  // Search the table.\n";
  if (HasMnemonicFirst) {
    OS << "  auto MnemonicRange = "
          "std::equal_range(Start, End, Mnemonic, LessOpcode());\n\n";
  } else {
    OS << "  auto MnemonicRange = std::pair(Start, End);\n";
    OS << "  unsigned SIndex = Mnemonic.empty() ? 0 : 1;\n";
    OS << "  if (!Mnemonic.empty())\n";
    OS << "    MnemonicRange = "
          "std::equal_range(Start, End, Mnemonic.lower(), LessOpcode());\n\n";
  }

  OS << "  DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"AsmMatcher: found \" "
        "<<\n"
     << "  std::distance(MnemonicRange.first, MnemonicRange.second) <<\n"
     << "  \" encodings with mnemonic '\" << Mnemonic << \"'\\n\");\n\n";

  OS << "  // Return a more specific error code if no mnemonics match.\n";
  OS << "  if (MnemonicRange.first == MnemonicRange.second)\n";
  OS << "    return Match_MnemonicFail;\n\n";

  OS << "  for (const MatchEntry *it = MnemonicRange.first, "
     << "*ie = MnemonicRange.second;\n";
  OS << "       it != ie; ++it) {\n";
  OS << "    const FeatureBitset &RequiredFeatures = "
        "FeatureBitsets[it->RequiredFeaturesIdx];\n";
  OS << "    bool HasRequiredFeatures =\n";
  OS << "      (AvailableFeatures & RequiredFeatures) == RequiredFeatures;\n";
  OS << "    DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"Trying to match "
        "opcode \"\n";
  OS << "                                          << MII.getName(it->Opcode) "
        "<< \"\\n\");\n";

  if (ReportMultipleNearMisses) {
    OS << "    // Some state to record ways in which this instruction did not "
          "match.\n";
    OS << "    NearMissInfo OperandNearMiss = NearMissInfo::getSuccess();\n";
    OS << "    NearMissInfo FeaturesNearMiss = NearMissInfo::getSuccess();\n";
    OS << "    NearMissInfo EarlyPredicateNearMiss = "
          "NearMissInfo::getSuccess();\n";
    OS << "    NearMissInfo LatePredicateNearMiss = "
          "NearMissInfo::getSuccess();\n";
    OS << "    bool MultipleInvalidOperands = false;\n";
  }

  if (HasMnemonicFirst) {
    OS << "    // equal_range guarantees that instruction mnemonic matches.\n";
    OS << "    assert(Mnemonic == it->getMnemonic());\n";
  }

  // Emit check that the subclasses match.
  if (!ReportMultipleNearMisses)
    OS << "    bool OperandsValid = true;\n";
  if (HasOptionalOperands) {
    OS << "    OptionalOperandsMask.reset(0, " << MaxNumOperands << ");\n";
  }
  OS << "    for (unsigned FormalIdx = " << (HasMnemonicFirst ? "0" : "SIndex")
     << ", ActualIdx = " << (HasMnemonicFirst ? "1" : "SIndex")
     << "; FormalIdx != " << MaxNumOperands << "; ++FormalIdx) {\n";
  OS << "      auto Formal = "
     << "static_cast<MatchClassKind>(it->Classes[FormalIdx]);\n";
  OS << "      DEBUG_WITH_TYPE(\"asm-matcher\",\n";
  OS << "                      dbgs() << \"  Matching formal operand class \" "
        "<< getMatchClassName(Formal)\n";
  OS << "                             << \" against actual operand at index \" "
        "<< ActualIdx);\n";
  OS << "      if (ActualIdx < Operands.size())\n";
  OS << "        DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \" (\";\n";
  OS << "                        Operands[ActualIdx]->print(dbgs()); dbgs() << "
        "\"): \");\n";
  OS << "      else\n";
  OS << "        DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \": \");\n";
  OS << "      if (ActualIdx >= Operands.size()) {\n";
  OS << "        DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"actual operand "
        "index out of range\\n\");\n";
  if (ReportMultipleNearMisses) {
    OS << "        bool ThisOperandValid = (Formal == "
       << "InvalidMatchClass) || "
          "isSubclass(Formal, OptionalMatchClass);\n";
    OS << "        if (!ThisOperandValid) {\n";
    OS << "          if (!OperandNearMiss) {\n";
    OS << "            // Record info about match failure for later use.\n";
    OS << "            DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"recording "
          "too-few-operands near miss\\n\");\n";
    OS << "            OperandNearMiss =\n";
    OS << "                NearMissInfo::getTooFewOperands(Formal, "
          "it->Opcode);\n";
    OS << "          } else if (OperandNearMiss.getKind() != "
          "NearMissInfo::NearMissTooFewOperands) {\n";
    OS << "            // If more than one operand is invalid, give up on this "
          "match entry.\n";
    OS << "            DEBUG_WITH_TYPE(\n";
    OS << "                \"asm-matcher\",\n";
    OS << "                dbgs() << \"second invalid operand, giving up on "
          "this opcode\\n\");\n";
    OS << "            MultipleInvalidOperands = true;\n";
    OS << "            break;\n";
    OS << "          }\n";
    OS << "        } else {\n";
    OS << "          DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"but formal "
          "operand not required\\n\");\n";
    OS << "          if (isSubclass(Formal, OptionalMatchClass)) {\n";
    OS << "            OptionalOperandsMask.set(FormalIdx);\n";
    OS << "          }\n";
    OS << "        }\n";
    OS << "        continue;\n";
  } else {
    OS << "        if (Formal == InvalidMatchClass) {\n";
    if (HasOptionalOperands) {
      OS << "          OptionalOperandsMask.set(FormalIdx, " << MaxNumOperands
         << ");\n";
    }
    OS << "          break;\n";
    OS << "        }\n";
    OS << "        if (isSubclass(Formal, OptionalMatchClass)) {\n";
    if (HasOptionalOperands) {
      OS << "          OptionalOperandsMask.set(FormalIdx);\n";
    }
    OS << "          continue;\n";
    OS << "        }\n";
    OS << "        OperandsValid = false;\n";
    OS << "        ErrorInfo = ActualIdx;\n";
    OS << "        break;\n";
  }
  OS << "      }\n";
  OS << "      MCParsedAsmOperand &Actual = *Operands[ActualIdx];\n";
  OS << "      unsigned Diag = validateOperandClass(Actual, Formal);\n";
  OS << "      if (Diag == Match_Success) {\n";
  OS << "        DEBUG_WITH_TYPE(\"asm-matcher\",\n";
  OS << "                        dbgs() << \"match success using generic "
        "matcher\\n\");\n";
  OS << "        ++ActualIdx;\n";
  OS << "        continue;\n";
  OS << "      }\n";
  OS << "      // If the generic handler indicates an invalid operand\n";
  OS << "      // failure, check for a special case.\n";
  OS << "      if (Diag != Match_Success) {\n";
  OS << "        unsigned TargetDiag = validateTargetOperandClass(Actual, "
        "Formal);\n";
  OS << "        if (TargetDiag == Match_Success) {\n";
  OS << "          DEBUG_WITH_TYPE(\"asm-matcher\",\n";
  OS << "                          dbgs() << \"match success using target "
        "matcher\\n\");\n";
  OS << "          ++ActualIdx;\n";
  OS << "          continue;\n";
  OS << "        }\n";
  OS << "        // If the target matcher returned a specific error code use\n";
  OS << "        // that, else use the one from the generic matcher.\n";
  OS << "        if (TargetDiag != Match_InvalidOperand && "
        "HasRequiredFeatures)\n";
  OS << "          Diag = TargetDiag;\n";
  OS << "      }\n";
  OS << "      // If current formal operand wasn't matched and it is optional\n"
     << "      // then try to match next formal operand\n";
  OS << "      if (Diag == Match_InvalidOperand "
     << "&& isSubclass(Formal, OptionalMatchClass)) {\n";
  if (HasOptionalOperands) {
    OS << "        OptionalOperandsMask.set(FormalIdx);\n";
  }
  OS << "        DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"ignoring "
        "optional operand\\n\");\n";
  OS << "        continue;\n";
  OS << "      }\n";

  if (ReportMultipleNearMisses) {
    OS << "      if (!OperandNearMiss) {\n";
    OS << "        // If this is the first invalid operand we have seen, "
          "record some\n";
    OS << "        // information about it.\n";
    OS << "        DEBUG_WITH_TYPE(\n";
    OS << "            \"asm-matcher\",\n";
    OS << "            dbgs()\n";
    OS << "                << \"operand match failed, recording near-miss with "
          "diag code \"\n";
    OS << "                << Diag << \"\\n\");\n";
    OS << "        OperandNearMiss =\n";
    OS << "            NearMissInfo::getMissedOperand(Diag, Formal, "
          "it->Opcode, ActualIdx);\n";
    OS << "        ++ActualIdx;\n";
    OS << "      } else {\n";
    OS << "        // If more than one operand is invalid, give up on this "
          "match entry.\n";
    OS << "        DEBUG_WITH_TYPE(\n";
    OS << "            \"asm-matcher\",\n";
    OS << "            dbgs() << \"second operand mismatch, skipping this "
          "opcode\\n\");\n";
    OS << "        MultipleInvalidOperands = true;\n";
    OS << "        break;\n";
    OS << "      }\n";
    OS << "    }\n\n";
  } else {
    OS << "      // If this operand is broken for all of the instances of "
          "this\n";
    OS << "      // mnemonic, keep track of it so we can report loc info.\n";
    OS << "      // If we already had a match that only failed due to a\n";
    OS << "      // target predicate, that diagnostic is preferred.\n";
    OS << "      if (!HadMatchOtherThanPredicate &&\n";
    OS << "          (it == MnemonicRange.first || ErrorInfo <= ActualIdx)) "
          "{\n";
    OS << "        if (HasRequiredFeatures && (ErrorInfo != ActualIdx || Diag "
          "!= Match_InvalidOperand))\n";
    OS << "          RetCode = Diag;\n";
    OS << "        ErrorInfo = ActualIdx;\n";
    OS << "      }\n";
    OS << "      // Otherwise, just reject this instance of the mnemonic.\n";
    OS << "      OperandsValid = false;\n";
    OS << "      break;\n";
    OS << "    }\n\n";
  }

  if (ReportMultipleNearMisses)
    OS << "    if (MultipleInvalidOperands) {\n";
  else
    OS << "    if (!OperandsValid) {\n";
  OS << "      DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"Opcode result: "
        "multiple \"\n";
  OS << "                                               \"operand mismatches, "
        "ignoring \"\n";
  OS << "                                               \"this opcode\\n\");\n";
  OS << "      continue;\n";
  OS << "    }\n";

  // Emit check that the required features are available.
  OS << "    if (!HasRequiredFeatures) {\n";
  if (!ReportMultipleNearMisses)
    OS << "      HadMatchOtherThanFeatures = true;\n";
  OS << "      FeatureBitset NewMissingFeatures = RequiredFeatures & "
        "~AvailableFeatures;\n";
  OS << "      DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"Missing target "
        "features:\";\n";
  OS << "                      for (unsigned I = 0, E = "
        "NewMissingFeatures.size(); I != E; ++I)\n";
  OS << "                        if (NewMissingFeatures[I])\n";
  OS << "                          dbgs() << ' ' << I;\n";
  OS << "                      dbgs() << \"\\n\");\n";
  if (ReportMultipleNearMisses) {
    OS << "      FeaturesNearMiss = "
          "NearMissInfo::getMissedFeature(NewMissingFeatures);\n";
  } else {
    OS << "      if (NewMissingFeatures.count() <=\n"
          "          MissingFeatures.count())\n";
    OS << "        MissingFeatures = NewMissingFeatures;\n";
    OS << "      continue;\n";
  }
  OS << "    }\n";
  OS << "\n";
  OS << "    Inst.clear();\n\n";
  OS << "    Inst.setOpcode(it->Opcode);\n";
  // Verify the instruction with the target-specific match predicate function.
  OS << "    // We have a potential match but have not rendered the operands.\n"
     << "    // Check the target predicate to handle any context sensitive\n"
        "    // constraints.\n"
     << "    // For example, Ties that are referenced multiple times must be\n"
        "    // checked here to ensure the input is the same for each match\n"
        "    // constraints. If we leave it any later the ties will have been\n"
        "    // canonicalized\n"
     << "    unsigned MatchResult;\n"
     << "    if ((MatchResult = checkEarlyTargetMatchPredicate(Inst, "
        "Operands)) != Match_Success) {\n"
     << "      Inst.clear();\n";
  OS << "      DEBUG_WITH_TYPE(\n";
  OS << "          \"asm-matcher\",\n";
  OS << "          dbgs() << \"Early target match predicate failed with diag "
        "code \"\n";
  OS << "                 << MatchResult << \"\\n\");\n";
  if (ReportMultipleNearMisses) {
    OS << "      EarlyPredicateNearMiss = "
          "NearMissInfo::getMissedPredicate(MatchResult);\n";
  } else {
    OS << "      RetCode = MatchResult;\n"
       << "      HadMatchOtherThanPredicate = true;\n"
       << "      continue;\n";
  }
  OS << "    }\n\n";

  if (ReportMultipleNearMisses) {
    OS << "    // If we did not successfully match the operands, then we can't "
          "convert to\n";
    OS << "    // an MCInst, so bail out on this instruction variant now.\n";
    OS << "    if (OperandNearMiss) {\n";
    OS << "      // If the operand mismatch was the only problem, reprrt it as "
          "a near-miss.\n";
    OS << "      if (NearMisses && !FeaturesNearMiss && "
          "!EarlyPredicateNearMiss) {\n";
    OS << "        DEBUG_WITH_TYPE(\n";
    OS << "            \"asm-matcher\",\n";
    OS << "            dbgs()\n";
    OS << "                << \"Opcode result: one mismatched operand, adding "
          "near-miss\\n\");\n";
    OS << "        NearMisses->push_back(OperandNearMiss);\n";
    OS << "      } else {\n";
    OS << "        DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"Opcode result: "
          "multiple \"\n";
    OS << "                                                 \"types of "
          "mismatch, so not \"\n";
    OS << "                                                 \"reporting "
          "near-miss\\n\");\n";
    OS << "      }\n";
    OS << "      continue;\n";
    OS << "    }\n\n";
  }

  // When converting parsed operands to MCInst we need to know whether optional
  // operands were parsed or not so that we can choose the correct converter
  // function. We also need to know this when checking tied operand constraints.
  // DefaultsOffset is an array of deltas between the formal (MCInst) and the
  // actual (parsed operand array) operand indices. When all optional operands
  // are present, all elements of the array are zeros. If some of the optional
  // operands are absent, the array might look like '0, 0, 1, 1, 1, 2, 2, 3',
  // where each increment in value reflects the absence of an optional operand.
  if (HasOptionalOperands) {
    OS << "    unsigned DefaultsOffset[" << (MaxNumOperands + 1)
       << "] = { 0 };\n";
    OS << "    assert(OptionalOperandsMask.size() == " << (MaxNumOperands)
       << ");\n";
    OS << "    for (unsigned i = 0, NumDefaults = 0; i < " << (MaxNumOperands)
       << "; ++i) {\n";
    OS << "      DefaultsOffset[i + 1] = NumDefaults;\n";
    OS << "      NumDefaults += (OptionalOperandsMask[i] ? 1 : 0);\n";
    OS << "    }\n\n";
  }

  OS << "    if (matchingInlineAsm) {\n";
  OS << "      convertToMapAndConstraints(it->ConvertFn, Operands);\n";
  if (!ReportMultipleNearMisses) {
    if (HasOptionalOperands) {
      OS << "      if (!checkAsmTiedOperandConstraints(*this, it->ConvertFn, "
            "Operands,\n";
      OS << "                                          DefaultsOffset, "
            "ErrorInfo))\n";
    } else {
      OS << "      if (!checkAsmTiedOperandConstraints(*this, it->ConvertFn, "
            "Operands,\n";
      OS << "                                          ErrorInfo))\n";
    }
    OS << "        return Match_InvalidTiedOperand;\n";
    OS << "\n";
  }
  OS << "      return Match_Success;\n";
  OS << "    }\n\n";
  OS << "    // We have selected a definite instruction, convert the parsed\n"
     << "    // operands into the appropriate MCInst.\n";
  if (HasOptionalOperands) {
    OS << "    convertToMCInst(it->ConvertFn, Inst, it->Opcode, Operands,\n"
       << "                    OptionalOperandsMask, DefaultsOffset);\n";
  } else {
    OS << "    convertToMCInst(it->ConvertFn, Inst, it->Opcode, Operands);\n";
  }
  OS << "\n";

  // Verify the instruction with the target-specific match predicate function.
  OS << "    // We have a potential match. Check the target predicate to\n"
     << "    // handle any context sensitive constraints.\n"
     << "    if ((MatchResult = checkTargetMatchPredicate(Inst)) !="
     << " Match_Success) {\n"
     << "      DEBUG_WITH_TYPE(\"asm-matcher\",\n"
     << "                      dbgs() << \"Target match predicate failed with "
        "diag code \"\n"
     << "                             << MatchResult << \"\\n\");\n"
     << "      Inst.clear();\n";
  if (ReportMultipleNearMisses) {
    OS << "      LatePredicateNearMiss = "
          "NearMissInfo::getMissedPredicate(MatchResult);\n";
  } else {
    OS << "      RetCode = MatchResult;\n"
       << "      HadMatchOtherThanPredicate = true;\n"
       << "      continue;\n";
  }
  OS << "    }\n\n";

  if (ReportMultipleNearMisses) {
    OS << "    int NumNearMisses = ((int)(bool)OperandNearMiss +\n";
    OS << "                         (int)(bool)FeaturesNearMiss +\n";
    OS << "                         (int)(bool)EarlyPredicateNearMiss +\n";
    OS << "                         (int)(bool)LatePredicateNearMiss);\n";
    OS << "    if (NumNearMisses == 1) {\n";
    OS << "      // We had exactly one type of near-miss, so add that to the "
          "list.\n";
    OS << "      assert(!OperandNearMiss && \"OperandNearMiss was handled "
          "earlier\");\n";
    OS << "      DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"Opcode result: "
          "found one type of \"\n";
    OS << "                                            \"mismatch, so "
          "reporting a \"\n";
    OS << "                                            \"near-miss\\n\");\n";
    OS << "      if (NearMisses && FeaturesNearMiss)\n";
    OS << "        NearMisses->push_back(FeaturesNearMiss);\n";
    OS << "      else if (NearMisses && EarlyPredicateNearMiss)\n";
    OS << "        NearMisses->push_back(EarlyPredicateNearMiss);\n";
    OS << "      else if (NearMisses && LatePredicateNearMiss)\n";
    OS << "        NearMisses->push_back(LatePredicateNearMiss);\n";
    OS << "\n";
    OS << "      continue;\n";
    OS << "    } else if (NumNearMisses > 1) {\n";
    OS << "      // This instruction missed in more than one way, so ignore "
          "it.\n";
    OS << "      DEBUG_WITH_TYPE(\"asm-matcher\", dbgs() << \"Opcode result: "
          "multiple \"\n";
    OS << "                                               \"types of mismatch, "
          "so not \"\n";
    OS << "                                               \"reporting "
          "near-miss\\n\");\n";
    OS << "      continue;\n";
    OS << "    }\n";
  }

  // Call the post-processing function, if used.
  StringRef InsnCleanupFn = AsmParser->getValueAsString("AsmParserInstCleanup");
  if (!InsnCleanupFn.empty())
    OS << "    " << InsnCleanupFn << "(Inst);\n";

  if (HasDeprecation) {
    OS << "    std::string Info;\n";
    OS << "    if "
          "(!getParser().getTargetParser().getTargetOptions()."
          "MCNoDeprecatedWarn &&\n";
    OS << "        MII.getDeprecatedInfo(Inst, getSTI(), Info)) {\n";
    OS << "      SMLoc Loc = ((" << Target.getName()
       << "Operand &)*Operands[0]).getStartLoc();\n";
    OS << "      getParser().Warning(Loc, Info, std::nullopt);\n";
    OS << "    }\n";
  }

  if (!ReportMultipleNearMisses) {
    if (HasOptionalOperands) {
      OS << "    if (!checkAsmTiedOperandConstraints(*this, it->ConvertFn, "
            "Operands,\n";
      OS << "                                         DefaultsOffset, "
            "ErrorInfo))\n";
    } else {
      OS << "    if (!checkAsmTiedOperandConstraints(*this, it->ConvertFn, "
            "Operands,\n";
      OS << "                                         ErrorInfo))\n";
    }
    OS << "      return Match_InvalidTiedOperand;\n";
    OS << "\n";
  }

  OS << "    DEBUG_WITH_TYPE(\n";
  OS << "        \"asm-matcher\",\n";
  OS << "        dbgs() << \"Opcode result: complete match, selecting this "
        "opcode\\n\");\n";
  OS << "    return Match_Success;\n";
  OS << "  }\n\n";

  if (ReportMultipleNearMisses) {
    OS << "  // No instruction variants matched exactly.\n";
    OS << "  return Match_NearMisses;\n";
  } else {
    OS << "  // Okay, we had no match.  Try to return a useful error code.\n";
    OS << "  if (HadMatchOtherThanPredicate || !HadMatchOtherThanFeatures)\n";
    OS << "    return RetCode;\n\n";
    OS << "  ErrorInfo = 0;\n";
    OS << "  return Match_MissingFeature;\n";
  }
  OS << "}\n\n";

  if (!Info.OperandMatchInfo.empty())
    emitCustomOperandParsing(OS, Target, Info, ClassName, StringTable,
                             MaxMnemonicIndex, FeatureBitsets.size(),
                             HasMnemonicFirst, *AsmParser);

  OS << "#endif // GET_MATCHER_IMPLEMENTATION\n\n";

  OS << "\n#ifdef GET_MNEMONIC_SPELL_CHECKER\n";
  OS << "#undef GET_MNEMONIC_SPELL_CHECKER\n\n";

  emitMnemonicSpellChecker(OS, Target, VariantCount);

  OS << "#endif // GET_MNEMONIC_SPELL_CHECKER\n\n";

  OS << "\n#ifdef GET_MNEMONIC_CHECKER\n";
  OS << "#undef GET_MNEMONIC_CHECKER\n\n";

  emitMnemonicChecker(OS, Target, VariantCount, HasMnemonicFirst,
                      HasMnemonicAliases);

  OS << "#endif // GET_MNEMONIC_CHECKER\n\n";
}

static TableGen::Emitter::OptClass<AsmMatcherEmitter>
    X("gen-asm-matcher", "Generate assembly instruction matcher");
