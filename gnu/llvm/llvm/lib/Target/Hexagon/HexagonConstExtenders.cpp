//===- HexagonConstExtenders.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <set>
#include <utility>
#include <vector>

#define DEBUG_TYPE "hexagon-cext-opt"

using namespace llvm;

static cl::opt<unsigned> CountThreshold(
    "hexagon-cext-threshold", cl::init(3), cl::Hidden,
    cl::desc("Minimum number of extenders to trigger replacement"));

static cl::opt<unsigned>
    ReplaceLimit("hexagon-cext-limit", cl::init(0), cl::Hidden,
                 cl::desc("Maximum number of replacements"));

namespace llvm {
  void initializeHexagonConstExtendersPass(PassRegistry&);
  FunctionPass *createHexagonConstExtenders();
}

static int32_t adjustUp(int32_t V, uint8_t A, uint8_t O) {
  assert(isPowerOf2_32(A));
  int32_t U = (V & -A) + O;
  return U >= V ? U : U+A;
}

static int32_t adjustDown(int32_t V, uint8_t A, uint8_t O) {
  assert(isPowerOf2_32(A));
  int32_t U = (V & -A) + O;
  return U <= V ? U : U-A;
}

namespace {
  struct OffsetRange {
    // The range of values between Min and Max that are of form Align*N+Offset,
    // for some integer N. Min and Max are required to be of that form as well,
    // except in the case of an empty range.
    int32_t Min = INT_MIN, Max = INT_MAX;
    uint8_t Align = 1;
    uint8_t Offset = 0;

    OffsetRange() = default;
    OffsetRange(int32_t L, int32_t H, uint8_t A, uint8_t O = 0)
      : Min(L), Max(H), Align(A), Offset(O) {}
    OffsetRange &intersect(OffsetRange A) {
      if (Align < A.Align)
        std::swap(*this, A);

      // Align >= A.Align.
      if (Offset >= A.Offset && (Offset - A.Offset) % A.Align == 0) {
        Min = adjustUp(std::max(Min, A.Min), Align, Offset);
        Max = adjustDown(std::min(Max, A.Max), Align, Offset);
      } else {
        // Make an empty range.
        Min = 0;
        Max = -1;
      }
      // Canonicalize empty ranges.
      if (Min > Max)
        std::tie(Min, Max, Align) = std::make_tuple(0, -1, 1);
      return *this;
    }
    OffsetRange &shift(int32_t S) {
      Min += S;
      Max += S;
      Offset = (Offset+S) % Align;
      return *this;
    }
    OffsetRange &extendBy(int32_t D) {
      // If D < 0, extend Min, otherwise extend Max.
      assert(D % Align == 0);
      if (D < 0)
        Min = (INT_MIN-D < Min) ? Min+D : INT_MIN;
      else
        Max = (INT_MAX-D > Max) ? Max+D : INT_MAX;
      return *this;
    }
    bool empty() const {
      return Min > Max;
    }
    bool contains(int32_t V) const {
      return Min <= V && V <= Max && (V-Offset) % Align == 0;
    }
    bool operator==(const OffsetRange &R) const {
      return Min == R.Min && Max == R.Max && Align == R.Align;
    }
    bool operator!=(const OffsetRange &R) const {
      return !operator==(R);
    }
    bool operator<(const OffsetRange &R) const {
      if (Min != R.Min)
        return Min < R.Min;
      if (Max != R.Max)
        return Max < R.Max;
      return Align < R.Align;
    }
    static OffsetRange zero() { return {0, 0, 1}; }
  };

  struct RangeTree {
    struct Node {
      Node(const OffsetRange &R) : MaxEnd(R.Max), Range(R) {}
      unsigned Height = 1;
      unsigned Count = 1;
      int32_t MaxEnd;
      const OffsetRange &Range;
      Node *Left = nullptr, *Right = nullptr;
    };

    Node *Root = nullptr;

    void add(const OffsetRange &R) {
      Root = add(Root, R);
    }
    void erase(const Node *N) {
      Root = remove(Root, N);
      delete N;
    }
    void order(SmallVectorImpl<Node*> &Seq) const {
      order(Root, Seq);
    }
    SmallVector<Node*,8> nodesWith(int32_t P, bool CheckAlign = true) {
      SmallVector<Node*,8> Nodes;
      nodesWith(Root, P, CheckAlign, Nodes);
      return Nodes;
    }
    void dump() const;
    ~RangeTree() {
      SmallVector<Node*,8> Nodes;
      order(Nodes);
      for (Node *N : Nodes)
        delete N;
    }

  private:
    void dump(const Node *N) const;
    void order(Node *N, SmallVectorImpl<Node*> &Seq) const;
    void nodesWith(Node *N, int32_t P, bool CheckA,
                   SmallVectorImpl<Node*> &Seq) const;

    Node *add(Node *N, const OffsetRange &R);
    Node *remove(Node *N, const Node *D);
    Node *rotateLeft(Node *Lower, Node *Higher);
    Node *rotateRight(Node *Lower, Node *Higher);
    unsigned height(Node *N) {
      return N != nullptr ? N->Height : 0;
    }
    Node *update(Node *N) {
      assert(N != nullptr);
      N->Height = 1 + std::max(height(N->Left), height(N->Right));
      if (N->Left)
        N->MaxEnd = std::max(N->MaxEnd, N->Left->MaxEnd);
      if (N->Right)
        N->MaxEnd = std::max(N->MaxEnd, N->Right->MaxEnd);
      return N;
    }
    Node *rebalance(Node *N) {
      assert(N != nullptr);
      int32_t Balance = height(N->Right) - height(N->Left);
      if (Balance < -1)
        return rotateRight(N->Left, N);
      if (Balance > 1)
        return rotateLeft(N->Right, N);
      return N;
    }
  };

  struct Loc {
    MachineBasicBlock *Block = nullptr;
    MachineBasicBlock::iterator At;

    Loc(MachineBasicBlock *B, MachineBasicBlock::iterator It)
      : Block(B), At(It) {
      if (B->end() == It) {
        Pos = -1;
      } else {
        assert(It->getParent() == B);
        Pos = std::distance(B->begin(), It);
      }
    }
    bool operator<(Loc A) const {
      if (Block != A.Block)
        return Block->getNumber() < A.Block->getNumber();
      if (A.Pos == -1)
        return Pos != A.Pos;
      return Pos != -1 && Pos < A.Pos;
    }
  private:
    int Pos = 0;
  };

  struct HexagonConstExtenders : public MachineFunctionPass {
    static char ID;
    HexagonConstExtenders() : MachineFunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addPreserved<MachineDominatorTreeWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    StringRef getPassName() const override {
      return "Hexagon constant-extender optimization";
    }
    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    struct Register {
      Register() = default;
      Register(llvm::Register R, unsigned S) : Reg(R), Sub(S) {}
      Register(const MachineOperand &Op)
        : Reg(Op.getReg()), Sub(Op.getSubReg()) {}
      Register &operator=(const MachineOperand &Op) {
        if (Op.isReg()) {
          Reg = Op.getReg();
          Sub = Op.getSubReg();
        } else if (Op.isFI()) {
          Reg = llvm::Register::index2StackSlot(Op.getIndex());
        }
        return *this;
      }
      bool isVReg() const {
        return Reg != 0 && !Reg.isStack() && Reg.isVirtual();
      }
      bool isSlot() const { return Reg != 0 && Reg.isStack(); }
      operator MachineOperand() const {
        if (isVReg())
          return MachineOperand::CreateReg(Reg, /*Def*/false, /*Imp*/false,
                          /*Kill*/false, /*Dead*/false, /*Undef*/false,
                          /*EarlyClobber*/false, Sub);
        if (Reg.isStack()) {
          int FI = llvm::Register::stackSlot2Index(Reg);
          return MachineOperand::CreateFI(FI);
        }
        llvm_unreachable("Cannot create MachineOperand");
      }
      bool operator==(Register R) const { return Reg == R.Reg && Sub == R.Sub; }
      bool operator!=(Register R) const { return !operator==(R); }
      bool operator<(Register R) const {
        // For std::map.
        return Reg < R.Reg || (Reg == R.Reg && Sub < R.Sub);
      }
      llvm::Register Reg;
      unsigned Sub = 0;
    };

    struct ExtExpr {
      // A subexpression in which the extender is used. In general, this
      // represents an expression where adding D to the extender will be
      // equivalent to adding D to the expression as a whole. In other
      // words, expr(add(##V,D) = add(expr(##V),D).

      // The original motivation for this are the io/ur addressing modes,
      // where the offset is extended. Consider the io example:
      // In memw(Rs+##V), the ##V could be replaced by a register Rt to
      // form the rr mode: memw(Rt+Rs<<0). In such case, however, the
      // register Rt must have exactly the value of ##V. If there was
      // another instruction memw(Rs+##V+4), it would need a different Rt.
      // Now, if Rt was initialized as "##V+Rs<<0", both of these
      // instructions could use the same Rt, just with different offsets.
      // Here it's clear that "initializer+4" should be the same as if
      // the offset 4 was added to the ##V in the initializer.

      // The only kinds of expressions that support the requirement of
      // commuting with addition are addition and subtraction from ##V.
      // Include shifting the Rs to account for the ur addressing mode:
      //   ##Val + Rs << S
      //   ##Val - Rs
      Register Rs;
      unsigned S = 0;
      bool Neg = false;

      ExtExpr() = default;
      ExtExpr(Register RS, bool NG, unsigned SH) : Rs(RS), S(SH), Neg(NG) {}
      // Expression is trivial if it does not modify the extender.
      bool trivial() const {
        return Rs.Reg == 0;
      }
      bool operator==(const ExtExpr &Ex) const {
        return Rs == Ex.Rs && S == Ex.S && Neg == Ex.Neg;
      }
      bool operator!=(const ExtExpr &Ex) const {
        return !operator==(Ex);
      }
      bool operator<(const ExtExpr &Ex) const {
        if (Rs != Ex.Rs)
          return Rs < Ex.Rs;
        if (S != Ex.S)
          return S < Ex.S;
        return !Neg && Ex.Neg;
      }
    };

    struct ExtDesc {
      MachineInstr *UseMI = nullptr;
      unsigned OpNum = -1u;
      // The subexpression in which the extender is used (e.g. address
      // computation).
      ExtExpr Expr;
      // Optional register that is assigned the value of Expr.
      Register Rd;
      // Def means that the output of the instruction may differ from the
      // original by a constant c, and that the difference can be corrected
      // by adding/subtracting c in all users of the defined register.
      bool IsDef = false;

      MachineOperand &getOp() {
        return UseMI->getOperand(OpNum);
      }
      const MachineOperand &getOp() const {
        return UseMI->getOperand(OpNum);
      }
    };

    struct ExtRoot {
      union {
        const ConstantFP *CFP;  // MO_FPImmediate
        const char *SymbolName; // MO_ExternalSymbol
        const GlobalValue *GV;  // MO_GlobalAddress
        const BlockAddress *BA; // MO_BlockAddress
        int64_t ImmVal;         // MO_Immediate, MO_TargetIndex,
                                // and MO_ConstantPoolIndex
      } V;
      unsigned Kind;            // Same as in MachineOperand.
      unsigned char TF;         // TargetFlags.

      ExtRoot(const MachineOperand &Op);
      bool operator==(const ExtRoot &ER) const {
        return Kind == ER.Kind && V.ImmVal == ER.V.ImmVal;
      }
      bool operator!=(const ExtRoot &ER) const {
        return !operator==(ER);
      }
      bool operator<(const ExtRoot &ER) const;
    };

    struct ExtValue : public ExtRoot {
      int32_t Offset;

      ExtValue(const MachineOperand &Op);
      ExtValue(const ExtDesc &ED) : ExtValue(ED.getOp()) {}
      ExtValue(const ExtRoot &ER, int32_t Off) : ExtRoot(ER), Offset(Off) {}
      bool operator<(const ExtValue &EV) const;
      bool operator==(const ExtValue &EV) const {
        return ExtRoot(*this) == ExtRoot(EV) && Offset == EV.Offset;
      }
      bool operator!=(const ExtValue &EV) const {
        return !operator==(EV);
      }
      explicit operator MachineOperand() const;
    };

    using IndexList = SetVector<unsigned>;
    using ExtenderInit = std::pair<ExtValue, ExtExpr>;
    using AssignmentMap = std::map<ExtenderInit, IndexList>;
    using LocDefList = std::vector<std::pair<Loc, IndexList>>;

    const HexagonSubtarget *HST = nullptr;
    const HexagonInstrInfo *HII = nullptr;
    const HexagonRegisterInfo *HRI = nullptr;
    MachineDominatorTree *MDT = nullptr;
    MachineRegisterInfo *MRI = nullptr;
    std::vector<ExtDesc> Extenders;
    std::vector<unsigned> NewRegs;

    bool isStoreImmediate(unsigned Opc) const;
    bool isRegOffOpcode(unsigned ExtOpc) const ;
    unsigned getRegOffOpcode(unsigned ExtOpc) const;
    unsigned getDirectRegReplacement(unsigned ExtOpc) const;
    OffsetRange getOffsetRange(Register R, const MachineInstr &MI) const;
    OffsetRange getOffsetRange(const ExtDesc &ED) const;
    OffsetRange getOffsetRange(Register Rd) const;

    void recordExtender(MachineInstr &MI, unsigned OpNum);
    void collectInstr(MachineInstr &MI);
    void collect(MachineFunction &MF);
    void assignInits(const ExtRoot &ER, unsigned Begin, unsigned End,
                     AssignmentMap &IMap);
    void calculatePlacement(const ExtenderInit &ExtI, const IndexList &Refs,
                            LocDefList &Defs);
    Register insertInitializer(Loc DefL, const ExtenderInit &ExtI);
    bool replaceInstrExact(const ExtDesc &ED, Register ExtR);
    bool replaceInstrExpr(const ExtDesc &ED, const ExtenderInit &ExtI,
                          Register ExtR, int32_t &Diff);
    bool replaceInstr(unsigned Idx, Register ExtR, const ExtenderInit &ExtI);
    bool replaceExtenders(const AssignmentMap &IMap);

    unsigned getOperandIndex(const MachineInstr &MI,
                             const MachineOperand &Op) const;
    const MachineOperand &getPredicateOp(const MachineInstr &MI) const;
    const MachineOperand &getLoadResultOp(const MachineInstr &MI) const;
    const MachineOperand &getStoredValueOp(const MachineInstr &MI) const;

    friend struct PrintRegister;
    friend struct PrintExpr;
    friend struct PrintInit;
    friend struct PrintIMap;
    friend raw_ostream &operator<< (raw_ostream &OS,
                                    const struct PrintRegister &P);
    friend raw_ostream &operator<< (raw_ostream &OS, const struct PrintExpr &P);
    friend raw_ostream &operator<< (raw_ostream &OS, const struct PrintInit &P);
    friend raw_ostream &operator<< (raw_ostream &OS, const ExtDesc &ED);
    friend raw_ostream &operator<< (raw_ostream &OS, const ExtRoot &ER);
    friend raw_ostream &operator<< (raw_ostream &OS, const ExtValue &EV);
    friend raw_ostream &operator<< (raw_ostream &OS, const OffsetRange &OR);
    friend raw_ostream &operator<< (raw_ostream &OS, const struct PrintIMap &P);
  };

  using HCE = HexagonConstExtenders;

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<< (raw_ostream &OS, const OffsetRange &OR) {
    if (OR.Min > OR.Max)
      OS << '!';
    OS << '[' << OR.Min << ',' << OR.Max << "]a" << unsigned(OR.Align)
       << '+' << unsigned(OR.Offset);
    return OS;
  }

  struct PrintRegister {
    PrintRegister(HCE::Register R, const HexagonRegisterInfo &I)
      : Rs(R), HRI(I) {}
    HCE::Register Rs;
    const HexagonRegisterInfo &HRI;
  };

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<< (raw_ostream &OS, const PrintRegister &P) {
    if (P.Rs.Reg != 0)
      OS << printReg(P.Rs.Reg, &P.HRI, P.Rs.Sub);
    else
      OS << "noreg";
    return OS;
  }

  struct PrintExpr {
    PrintExpr(const HCE::ExtExpr &E, const HexagonRegisterInfo &I)
      : Ex(E), HRI(I) {}
    const HCE::ExtExpr &Ex;
    const HexagonRegisterInfo &HRI;
  };

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<< (raw_ostream &OS, const PrintExpr &P) {
    OS << "## " << (P.Ex.Neg ? "- " : "+ ");
    if (P.Ex.Rs.Reg != 0)
      OS << printReg(P.Ex.Rs.Reg, &P.HRI, P.Ex.Rs.Sub);
    else
      OS << "__";
    OS << " << " << P.Ex.S;
    return OS;
  }

  struct PrintInit {
    PrintInit(const HCE::ExtenderInit &EI, const HexagonRegisterInfo &I)
      : ExtI(EI), HRI(I) {}
    const HCE::ExtenderInit &ExtI;
    const HexagonRegisterInfo &HRI;
  };

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<< (raw_ostream &OS, const PrintInit &P) {
    OS << '[' << P.ExtI.first << ", "
       << PrintExpr(P.ExtI.second, P.HRI) << ']';
    return OS;
  }

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<< (raw_ostream &OS, const HCE::ExtDesc &ED) {
    assert(ED.OpNum != -1u);
    const MachineBasicBlock &MBB = *ED.getOp().getParent()->getParent();
    const MachineFunction &MF = *MBB.getParent();
    const auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
    OS << "bb#" << MBB.getNumber() << ": ";
    if (ED.Rd.Reg != 0)
      OS << printReg(ED.Rd.Reg, &HRI, ED.Rd.Sub);
    else
      OS << "__";
    OS << " = " << PrintExpr(ED.Expr, HRI);
    if (ED.IsDef)
      OS << ", def";
    return OS;
  }

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<< (raw_ostream &OS, const HCE::ExtRoot &ER) {
    switch (ER.Kind) {
      case MachineOperand::MO_Immediate:
        OS << "imm:" << ER.V.ImmVal;
        break;
      case MachineOperand::MO_FPImmediate:
        OS << "fpi:" << *ER.V.CFP;
        break;
      case MachineOperand::MO_ExternalSymbol:
        OS << "sym:" << *ER.V.SymbolName;
        break;
      case MachineOperand::MO_GlobalAddress:
        OS << "gad:" << ER.V.GV->getName();
        break;
      case MachineOperand::MO_BlockAddress:
        OS << "blk:" << *ER.V.BA;
        break;
      case MachineOperand::MO_TargetIndex:
        OS << "tgi:" << ER.V.ImmVal;
        break;
      case MachineOperand::MO_ConstantPoolIndex:
        OS << "cpi:" << ER.V.ImmVal;
        break;
      case MachineOperand::MO_JumpTableIndex:
        OS << "jti:" << ER.V.ImmVal;
        break;
      default:
        OS << "???:" << ER.V.ImmVal;
        break;
    }
    return OS;
  }

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<< (raw_ostream &OS, const HCE::ExtValue &EV) {
    OS << HCE::ExtRoot(EV) << "  off:" << EV.Offset;
    return OS;
  }

  struct PrintIMap {
    PrintIMap(const HCE::AssignmentMap &M, const HexagonRegisterInfo &I)
      : IMap(M), HRI(I) {}
    const HCE::AssignmentMap &IMap;
    const HexagonRegisterInfo &HRI;
  };

  LLVM_ATTRIBUTE_UNUSED
  raw_ostream &operator<< (raw_ostream &OS, const PrintIMap &P) {
    OS << "{\n";
    for (const std::pair<const HCE::ExtenderInit, HCE::IndexList> &Q : P.IMap) {
      OS << "  " << PrintInit(Q.first, P.HRI) << " -> {";
      for (unsigned I : Q.second)
        OS << ' ' << I;
      OS << " }\n";
    }
    OS << "}\n";
    return OS;
  }
}

INITIALIZE_PASS_BEGIN(HexagonConstExtenders, "hexagon-cext-opt",
      "Hexagon constant-extender optimization", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(HexagonConstExtenders, "hexagon-cext-opt",
      "Hexagon constant-extender optimization", false, false)

static unsigned ReplaceCounter = 0;

char HCE::ID = 0;

#ifndef NDEBUG
LLVM_DUMP_METHOD void RangeTree::dump() const {
  dbgs() << "Root: " << Root << '\n';
  if (Root)
    dump(Root);
}

LLVM_DUMP_METHOD void RangeTree::dump(const Node *N) const {
  dbgs() << "Node: " << N << '\n';
  dbgs() << "  Height: " << N->Height << '\n';
  dbgs() << "  Count: " << N->Count << '\n';
  dbgs() << "  MaxEnd: " << N->MaxEnd << '\n';
  dbgs() << "  Range: " << N->Range << '\n';
  dbgs() << "  Left: " << N->Left << '\n';
  dbgs() << "  Right: " << N->Right << "\n\n";

  if (N->Left)
    dump(N->Left);
  if (N->Right)
    dump(N->Right);
}
#endif

void RangeTree::order(Node *N, SmallVectorImpl<Node*> &Seq) const {
  if (N == nullptr)
    return;
  order(N->Left, Seq);
  Seq.push_back(N);
  order(N->Right, Seq);
}

void RangeTree::nodesWith(Node *N, int32_t P, bool CheckA,
      SmallVectorImpl<Node*> &Seq) const {
  if (N == nullptr || N->MaxEnd < P)
    return;
  nodesWith(N->Left, P, CheckA, Seq);
  if (N->Range.Min <= P) {
    if ((CheckA && N->Range.contains(P)) || (!CheckA && P <= N->Range.Max))
      Seq.push_back(N);
    nodesWith(N->Right, P, CheckA, Seq);
  }
}

RangeTree::Node *RangeTree::add(Node *N, const OffsetRange &R) {
  if (N == nullptr)
    return new Node(R);

  if (N->Range == R) {
    N->Count++;
    return N;
  }

  if (R < N->Range)
    N->Left = add(N->Left, R);
  else
    N->Right = add(N->Right, R);
  return rebalance(update(N));
}

RangeTree::Node *RangeTree::remove(Node *N, const Node *D) {
  assert(N != nullptr);

  if (N != D) {
    assert(N->Range != D->Range && "N and D should not be equal");
    if (D->Range < N->Range)
      N->Left = remove(N->Left, D);
    else
      N->Right = remove(N->Right, D);
    return rebalance(update(N));
  }

  // We got to the node we need to remove. If any of its children are
  // missing, simply replace it with the other child.
  if (N->Left == nullptr || N->Right == nullptr)
    return (N->Left == nullptr) ? N->Right : N->Left;

  // Find the rightmost child of N->Left, remove it and plug it in place
  // of N.
  Node *M = N->Left;
  while (M->Right)
    M = M->Right;
  M->Left = remove(N->Left, M);
  M->Right = N->Right;
  return rebalance(update(M));
}

RangeTree::Node *RangeTree::rotateLeft(Node *Lower, Node *Higher) {
  assert(Higher->Right == Lower);
  // The Lower node is on the right from Higher. Make sure that Lower's
  // balance is greater to the right. Otherwise the rotation will create
  // an unbalanced tree again.
  if (height(Lower->Left) > height(Lower->Right))
    Lower = rotateRight(Lower->Left, Lower);
  assert(height(Lower->Left) <= height(Lower->Right));
  Higher->Right = Lower->Left;
  update(Higher);
  Lower->Left = Higher;
  update(Lower);
  return Lower;
}

RangeTree::Node *RangeTree::rotateRight(Node *Lower, Node *Higher) {
  assert(Higher->Left == Lower);
  // The Lower node is on the left from Higher. Make sure that Lower's
  // balance is greater to the left. Otherwise the rotation will create
  // an unbalanced tree again.
  if (height(Lower->Left) < height(Lower->Right))
    Lower = rotateLeft(Lower->Right, Lower);
  assert(height(Lower->Left) >= height(Lower->Right));
  Higher->Left = Lower->Right;
  update(Higher);
  Lower->Right = Higher;
  update(Lower);
  return Lower;
}


HCE::ExtRoot::ExtRoot(const MachineOperand &Op) {
  // Always store ImmVal, since it's the field used for comparisons.
  V.ImmVal = 0;
  if (Op.isImm())
    ; // Keep 0. Do not use Op.getImm() for value here (treat 0 as the root).
  else if (Op.isFPImm())
    V.CFP = Op.getFPImm();
  else if (Op.isSymbol())
    V.SymbolName = Op.getSymbolName();
  else if (Op.isGlobal())
    V.GV = Op.getGlobal();
  else if (Op.isBlockAddress())
    V.BA = Op.getBlockAddress();
  else if (Op.isCPI() || Op.isTargetIndex() || Op.isJTI())
    V.ImmVal = Op.getIndex();
  else
    llvm_unreachable("Unexpected operand type");

  Kind = Op.getType();
  TF = Op.getTargetFlags();
}

bool HCE::ExtRoot::operator< (const HCE::ExtRoot &ER) const {
  if (Kind != ER.Kind)
    return Kind < ER.Kind;
  switch (Kind) {
    case MachineOperand::MO_Immediate:
    case MachineOperand::MO_TargetIndex:
    case MachineOperand::MO_ConstantPoolIndex:
    case MachineOperand::MO_JumpTableIndex:
      return V.ImmVal < ER.V.ImmVal;
    case MachineOperand::MO_FPImmediate: {
      const APFloat &ThisF = V.CFP->getValueAPF();
      const APFloat &OtherF = ER.V.CFP->getValueAPF();
      return ThisF.bitcastToAPInt().ult(OtherF.bitcastToAPInt());
    }
    case MachineOperand::MO_ExternalSymbol:
      return StringRef(V.SymbolName) < StringRef(ER.V.SymbolName);
    case MachineOperand::MO_GlobalAddress:
      // Do not use GUIDs, since they depend on the source path. Moving the
      // source file to a different directory could cause different GUID
      // values for a pair of given symbols. These symbols could then compare
      // "less" in one directory, but "greater" in another.
      assert(!V.GV->getName().empty() && !ER.V.GV->getName().empty());
      return V.GV->getName() < ER.V.GV->getName();
    case MachineOperand::MO_BlockAddress: {
      const BasicBlock *ThisB = V.BA->getBasicBlock();
      const BasicBlock *OtherB = ER.V.BA->getBasicBlock();
      assert(ThisB->getParent() == OtherB->getParent());
      const Function &F = *ThisB->getParent();
      return std::distance(F.begin(), ThisB->getIterator()) <
             std::distance(F.begin(), OtherB->getIterator());
    }
  }
  return V.ImmVal < ER.V.ImmVal;
}

HCE::ExtValue::ExtValue(const MachineOperand &Op) : ExtRoot(Op) {
  if (Op.isImm())
    Offset = Op.getImm();
  else if (Op.isFPImm() || Op.isJTI())
    Offset = 0;
  else if (Op.isSymbol() || Op.isGlobal() || Op.isBlockAddress() ||
           Op.isCPI() || Op.isTargetIndex())
    Offset = Op.getOffset();
  else
    llvm_unreachable("Unexpected operand type");
}

bool HCE::ExtValue::operator< (const HCE::ExtValue &EV) const {
  const ExtRoot &ER = *this;
  if (!(ER == ExtRoot(EV)))
    return ER < EV;
  return Offset < EV.Offset;
}

HCE::ExtValue::operator MachineOperand() const {
  switch (Kind) {
    case MachineOperand::MO_Immediate:
      return MachineOperand::CreateImm(V.ImmVal + Offset);
    case MachineOperand::MO_FPImmediate:
      assert(Offset == 0);
      return MachineOperand::CreateFPImm(V.CFP);
    case MachineOperand::MO_ExternalSymbol:
      assert(Offset == 0);
      return MachineOperand::CreateES(V.SymbolName, TF);
    case MachineOperand::MO_GlobalAddress:
      return MachineOperand::CreateGA(V.GV, Offset, TF);
    case MachineOperand::MO_BlockAddress:
      return MachineOperand::CreateBA(V.BA, Offset, TF);
    case MachineOperand::MO_TargetIndex:
      return MachineOperand::CreateTargetIndex(V.ImmVal, Offset, TF);
    case MachineOperand::MO_ConstantPoolIndex:
      return MachineOperand::CreateCPI(V.ImmVal, Offset, TF);
    case MachineOperand::MO_JumpTableIndex:
      assert(Offset == 0);
      return MachineOperand::CreateJTI(V.ImmVal, TF);
    default:
      llvm_unreachable("Unhandled kind");
 }
}

bool HCE::isStoreImmediate(unsigned Opc) const {
  switch (Opc) {
    case Hexagon::S4_storeirbt_io:
    case Hexagon::S4_storeirbf_io:
    case Hexagon::S4_storeirht_io:
    case Hexagon::S4_storeirhf_io:
    case Hexagon::S4_storeirit_io:
    case Hexagon::S4_storeirif_io:
    case Hexagon::S4_storeirb_io:
    case Hexagon::S4_storeirh_io:
    case Hexagon::S4_storeiri_io:
      return true;
    default:
      break;
  }
  return false;
}

bool HCE::isRegOffOpcode(unsigned Opc) const {
  switch (Opc) {
    case Hexagon::L2_loadrub_io:
    case Hexagon::L2_loadrb_io:
    case Hexagon::L2_loadruh_io:
    case Hexagon::L2_loadrh_io:
    case Hexagon::L2_loadri_io:
    case Hexagon::L2_loadrd_io:
    case Hexagon::L2_loadbzw2_io:
    case Hexagon::L2_loadbzw4_io:
    case Hexagon::L2_loadbsw2_io:
    case Hexagon::L2_loadbsw4_io:
    case Hexagon::L2_loadalignh_io:
    case Hexagon::L2_loadalignb_io:
    case Hexagon::L2_ploadrubt_io:
    case Hexagon::L2_ploadrubf_io:
    case Hexagon::L2_ploadrbt_io:
    case Hexagon::L2_ploadrbf_io:
    case Hexagon::L2_ploadruht_io:
    case Hexagon::L2_ploadruhf_io:
    case Hexagon::L2_ploadrht_io:
    case Hexagon::L2_ploadrhf_io:
    case Hexagon::L2_ploadrit_io:
    case Hexagon::L2_ploadrif_io:
    case Hexagon::L2_ploadrdt_io:
    case Hexagon::L2_ploadrdf_io:
    case Hexagon::S2_storerb_io:
    case Hexagon::S2_storerh_io:
    case Hexagon::S2_storerf_io:
    case Hexagon::S2_storeri_io:
    case Hexagon::S2_storerd_io:
    case Hexagon::S2_pstorerbt_io:
    case Hexagon::S2_pstorerbf_io:
    case Hexagon::S2_pstorerht_io:
    case Hexagon::S2_pstorerhf_io:
    case Hexagon::S2_pstorerft_io:
    case Hexagon::S2_pstorerff_io:
    case Hexagon::S2_pstorerit_io:
    case Hexagon::S2_pstorerif_io:
    case Hexagon::S2_pstorerdt_io:
    case Hexagon::S2_pstorerdf_io:
    case Hexagon::A2_addi:
      return true;
    default:
      break;
  }
  return false;
}

unsigned HCE::getRegOffOpcode(unsigned ExtOpc) const {
  // If there exists an instruction that takes a register and offset,
  // that corresponds to the ExtOpc, return it, otherwise return 0.
  using namespace Hexagon;
  switch (ExtOpc) {
    case A2_tfrsi:    return A2_addi;
    default:
      break;
  }
  const MCInstrDesc &D = HII->get(ExtOpc);
  if (D.mayLoad() || D.mayStore()) {
    uint64_t F = D.TSFlags;
    unsigned AM = (F >> HexagonII::AddrModePos) & HexagonII::AddrModeMask;
    switch (AM) {
      case HexagonII::Absolute:
      case HexagonII::AbsoluteSet:
      case HexagonII::BaseLongOffset:
        switch (ExtOpc) {
          case PS_loadrubabs:
          case L4_loadrub_ap:
          case L4_loadrub_ur:     return L2_loadrub_io;
          case PS_loadrbabs:
          case L4_loadrb_ap:
          case L4_loadrb_ur:      return L2_loadrb_io;
          case PS_loadruhabs:
          case L4_loadruh_ap:
          case L4_loadruh_ur:     return L2_loadruh_io;
          case PS_loadrhabs:
          case L4_loadrh_ap:
          case L4_loadrh_ur:      return L2_loadrh_io;
          case PS_loadriabs:
          case L4_loadri_ap:
          case L4_loadri_ur:      return L2_loadri_io;
          case PS_loadrdabs:
          case L4_loadrd_ap:
          case L4_loadrd_ur:      return L2_loadrd_io;
          case L4_loadbzw2_ap:
          case L4_loadbzw2_ur:    return L2_loadbzw2_io;
          case L4_loadbzw4_ap:
          case L4_loadbzw4_ur:    return L2_loadbzw4_io;
          case L4_loadbsw2_ap:
          case L4_loadbsw2_ur:    return L2_loadbsw2_io;
          case L4_loadbsw4_ap:
          case L4_loadbsw4_ur:    return L2_loadbsw4_io;
          case L4_loadalignh_ap:
          case L4_loadalignh_ur:  return L2_loadalignh_io;
          case L4_loadalignb_ap:
          case L4_loadalignb_ur:  return L2_loadalignb_io;
          case L4_ploadrubt_abs:  return L2_ploadrubt_io;
          case L4_ploadrubf_abs:  return L2_ploadrubf_io;
          case L4_ploadrbt_abs:   return L2_ploadrbt_io;
          case L4_ploadrbf_abs:   return L2_ploadrbf_io;
          case L4_ploadruht_abs:  return L2_ploadruht_io;
          case L4_ploadruhf_abs:  return L2_ploadruhf_io;
          case L4_ploadrht_abs:   return L2_ploadrht_io;
          case L4_ploadrhf_abs:   return L2_ploadrhf_io;
          case L4_ploadrit_abs:   return L2_ploadrit_io;
          case L4_ploadrif_abs:   return L2_ploadrif_io;
          case L4_ploadrdt_abs:   return L2_ploadrdt_io;
          case L4_ploadrdf_abs:   return L2_ploadrdf_io;
          case PS_storerbabs:
          case S4_storerb_ap:
          case S4_storerb_ur:     return S2_storerb_io;
          case PS_storerhabs:
          case S4_storerh_ap:
          case S4_storerh_ur:     return S2_storerh_io;
          case PS_storerfabs:
          case S4_storerf_ap:
          case S4_storerf_ur:     return S2_storerf_io;
          case PS_storeriabs:
          case S4_storeri_ap:
          case S4_storeri_ur:     return S2_storeri_io;
          case PS_storerdabs:
          case S4_storerd_ap:
          case S4_storerd_ur:     return S2_storerd_io;
          case S4_pstorerbt_abs:  return S2_pstorerbt_io;
          case S4_pstorerbf_abs:  return S2_pstorerbf_io;
          case S4_pstorerht_abs:  return S2_pstorerht_io;
          case S4_pstorerhf_abs:  return S2_pstorerhf_io;
          case S4_pstorerft_abs:  return S2_pstorerft_io;
          case S4_pstorerff_abs:  return S2_pstorerff_io;
          case S4_pstorerit_abs:  return S2_pstorerit_io;
          case S4_pstorerif_abs:  return S2_pstorerif_io;
          case S4_pstorerdt_abs:  return S2_pstorerdt_io;
          case S4_pstorerdf_abs:  return S2_pstorerdf_io;
          default:
            break;
        }
        break;
      case HexagonII::BaseImmOffset:
        if (!isStoreImmediate(ExtOpc))
          return ExtOpc;
        break;
      default:
        break;
    }
  }
  return 0;
}

unsigned HCE::getDirectRegReplacement(unsigned ExtOpc) const {
  switch (ExtOpc) {
    case Hexagon::A2_addi:          return Hexagon::A2_add;
    case Hexagon::A2_andir:         return Hexagon::A2_and;
    case Hexagon::A2_combineii:     return Hexagon::A4_combineri;
    case Hexagon::A2_orir:          return Hexagon::A2_or;
    case Hexagon::A2_paddif:        return Hexagon::A2_paddf;
    case Hexagon::A2_paddit:        return Hexagon::A2_paddt;
    case Hexagon::A2_subri:         return Hexagon::A2_sub;
    case Hexagon::A2_tfrsi:         return TargetOpcode::COPY;
    case Hexagon::A4_cmpbeqi:       return Hexagon::A4_cmpbeq;
    case Hexagon::A4_cmpbgti:       return Hexagon::A4_cmpbgt;
    case Hexagon::A4_cmpbgtui:      return Hexagon::A4_cmpbgtu;
    case Hexagon::A4_cmpheqi:       return Hexagon::A4_cmpheq;
    case Hexagon::A4_cmphgti:       return Hexagon::A4_cmphgt;
    case Hexagon::A4_cmphgtui:      return Hexagon::A4_cmphgtu;
    case Hexagon::A4_combineii:     return Hexagon::A4_combineir;
    case Hexagon::A4_combineir:     return TargetOpcode::REG_SEQUENCE;
    case Hexagon::A4_combineri:     return TargetOpcode::REG_SEQUENCE;
    case Hexagon::A4_rcmpeqi:       return Hexagon::A4_rcmpeq;
    case Hexagon::A4_rcmpneqi:      return Hexagon::A4_rcmpneq;
    case Hexagon::C2_cmoveif:       return Hexagon::A2_tfrpf;
    case Hexagon::C2_cmoveit:       return Hexagon::A2_tfrpt;
    case Hexagon::C2_cmpeqi:        return Hexagon::C2_cmpeq;
    case Hexagon::C2_cmpgti:        return Hexagon::C2_cmpgt;
    case Hexagon::C2_cmpgtui:       return Hexagon::C2_cmpgtu;
    case Hexagon::C2_muxii:         return Hexagon::C2_muxir;
    case Hexagon::C2_muxir:         return Hexagon::C2_mux;
    case Hexagon::C2_muxri:         return Hexagon::C2_mux;
    case Hexagon::C4_cmpltei:       return Hexagon::C4_cmplte;
    case Hexagon::C4_cmplteui:      return Hexagon::C4_cmplteu;
    case Hexagon::C4_cmpneqi:       return Hexagon::C4_cmpneq;
    case Hexagon::M2_accii:         return Hexagon::M2_acci;        // T -> T
    /* No M2_macsin */
    case Hexagon::M2_macsip:        return Hexagon::M2_maci;        // T -> T
    case Hexagon::M2_mpysin:        return Hexagon::M2_mpyi;
    case Hexagon::M2_mpysip:        return Hexagon::M2_mpyi;
    case Hexagon::M2_mpysmi:        return Hexagon::M2_mpyi;
    case Hexagon::M2_naccii:        return Hexagon::M2_nacci;       // T -> T
    case Hexagon::M4_mpyri_addi:    return Hexagon::M4_mpyri_addr;
    case Hexagon::M4_mpyri_addr:    return Hexagon::M4_mpyrr_addr;  // _ -> T
    case Hexagon::M4_mpyrr_addi:    return Hexagon::M4_mpyrr_addr;  // _ -> T
    case Hexagon::S4_addaddi:       return Hexagon::M2_acci;        // _ -> T
    case Hexagon::S4_addi_asl_ri:   return Hexagon::S2_asl_i_r_acc; // T -> T
    case Hexagon::S4_addi_lsr_ri:   return Hexagon::S2_lsr_i_r_acc; // T -> T
    case Hexagon::S4_andi_asl_ri:   return Hexagon::S2_asl_i_r_and; // T -> T
    case Hexagon::S4_andi_lsr_ri:   return Hexagon::S2_lsr_i_r_and; // T -> T
    case Hexagon::S4_ori_asl_ri:    return Hexagon::S2_asl_i_r_or;  // T -> T
    case Hexagon::S4_ori_lsr_ri:    return Hexagon::S2_lsr_i_r_or;  // T -> T
    case Hexagon::S4_subaddi:       return Hexagon::M2_subacc;      // _ -> T
    case Hexagon::S4_subi_asl_ri:   return Hexagon::S2_asl_i_r_nac; // T -> T
    case Hexagon::S4_subi_lsr_ri:   return Hexagon::S2_lsr_i_r_nac; // T -> T

    // Store-immediates:
    case Hexagon::S4_storeirbf_io:  return Hexagon::S2_pstorerbf_io;
    case Hexagon::S4_storeirb_io:   return Hexagon::S2_storerb_io;
    case Hexagon::S4_storeirbt_io:  return Hexagon::S2_pstorerbt_io;
    case Hexagon::S4_storeirhf_io:  return Hexagon::S2_pstorerhf_io;
    case Hexagon::S4_storeirh_io:   return Hexagon::S2_storerh_io;
    case Hexagon::S4_storeirht_io:  return Hexagon::S2_pstorerht_io;
    case Hexagon::S4_storeirif_io:  return Hexagon::S2_pstorerif_io;
    case Hexagon::S4_storeiri_io:   return Hexagon::S2_storeri_io;
    case Hexagon::S4_storeirit_io:  return Hexagon::S2_pstorerit_io;

    default:
      break;
  }
  return 0;
}

// Return the allowable deviation from the current value of Rb (i.e. the
// range of values that can be added to the current value) which the
// instruction MI can accommodate.
// The instruction MI is a user of register Rb, which is defined via an
// extender. It may be possible for MI to be tweaked to work for a register
// defined with a slightly different value. For example
//   ... = L2_loadrub_io Rb, 1
// can be modifed to be
//   ... = L2_loadrub_io Rb', 0
// if Rb' = Rb+1.
// The range for Rb would be [Min+1, Max+1], where [Min, Max] is a range
// for L2_loadrub with offset 0. That means that Rb could be replaced with
// Rc, where Rc-Rb belongs to [Min+1, Max+1].
OffsetRange HCE::getOffsetRange(Register Rb, const MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  // Instructions that are constant-extended may be replaced with something
  // else that no longer offers the same range as the original.
  if (!isRegOffOpcode(Opc) || HII->isConstExtended(MI))
    return OffsetRange::zero();

  if (Opc == Hexagon::A2_addi) {
    const MachineOperand &Op1 = MI.getOperand(1), &Op2 = MI.getOperand(2);
    if (Rb != Register(Op1) || !Op2.isImm())
      return OffsetRange::zero();
    OffsetRange R = { -(1<<15)+1, (1<<15)-1, 1 };
    return R.shift(Op2.getImm());
  }

  // HII::getBaseAndOffsetPosition returns the increment position as "offset".
  if (HII->isPostIncrement(MI))
    return OffsetRange::zero();

  const MCInstrDesc &D = HII->get(Opc);
  assert(D.mayLoad() || D.mayStore());

  unsigned BaseP, OffP;
  if (!HII->getBaseAndOffsetPosition(MI, BaseP, OffP) ||
      Rb != Register(MI.getOperand(BaseP)) ||
      !MI.getOperand(OffP).isImm())
    return OffsetRange::zero();

  uint64_t F = (D.TSFlags >> HexagonII::MemAccessSizePos) &
                  HexagonII::MemAccesSizeMask;
  uint8_t A = HexagonII::getMemAccessSizeInBytes(HexagonII::MemAccessSize(F));
  unsigned L = Log2_32(A);
  unsigned S = 10+L;  // sint11_L
  int32_t Min = -alignDown((1<<S)-1, A);

  // The range will be shifted by Off. To prefer non-negative offsets,
  // adjust Max accordingly.
  int32_t Off = MI.getOperand(OffP).getImm();
  int32_t Max = Off >= 0 ? 0 : -Off;

  OffsetRange R = { Min, Max, A };
  return R.shift(Off);
}

// Return the allowable deviation from the current value of the extender ED,
// for which the instruction corresponding to ED can be modified without
// using an extender.
// The instruction uses the extender directly. It will be replaced with
// another instruction, say MJ, where the extender will be replaced with a
// register. MJ can allow some variability with respect to the value of
// that register, as is the case with indexed memory instructions.
OffsetRange HCE::getOffsetRange(const ExtDesc &ED) const {
  // The only way that there can be a non-zero range available is if
  // the instruction using ED will be converted to an indexed memory
  // instruction.
  unsigned IdxOpc = getRegOffOpcode(ED.UseMI->getOpcode());
  switch (IdxOpc) {
    case 0:
      return OffsetRange::zero();
    case Hexagon::A2_addi:    // s16
      return { -32767, 32767, 1 };
    case Hexagon::A2_subri:   // s10
      return { -511, 511, 1 };
  }

  if (!ED.UseMI->mayLoad() && !ED.UseMI->mayStore())
    return OffsetRange::zero();
  const MCInstrDesc &D = HII->get(IdxOpc);
  uint64_t F = (D.TSFlags >> HexagonII::MemAccessSizePos) &
                  HexagonII::MemAccesSizeMask;
  uint8_t A = HexagonII::getMemAccessSizeInBytes(HexagonII::MemAccessSize(F));
  unsigned L = Log2_32(A);
  unsigned S = 10+L;  // sint11_L
  int32_t Min = -alignDown((1<<S)-1, A);
  int32_t Max = 0;  // Force non-negative offsets.
  return { Min, Max, A };
}

// Get the allowable deviation from the current value of Rd by checking
// all uses of Rd.
OffsetRange HCE::getOffsetRange(Register Rd) const {
  OffsetRange Range;
  for (const MachineOperand &Op : MRI->use_operands(Rd.Reg)) {
    // Make sure that the register being used by this operand is identical
    // to the register that was defined: using a different subregister
    // precludes any non-trivial range.
    if (Rd != Register(Op))
      return OffsetRange::zero();
    Range.intersect(getOffsetRange(Rd, *Op.getParent()));
  }
  return Range;
}

void HCE::recordExtender(MachineInstr &MI, unsigned OpNum) {
  unsigned Opc = MI.getOpcode();
  ExtDesc ED;
  ED.OpNum = OpNum;

  bool IsLoad = MI.mayLoad();
  bool IsStore = MI.mayStore();

  // Fixed stack slots have negative indexes, and they cannot be used
  // with TRI::stackSlot2Index and TRI::index2StackSlot. This is somewhat
  // unfortunate, but should not be a frequent thing.
  for (MachineOperand &Op : MI.operands())
    if (Op.isFI() && Op.getIndex() < 0)
      return;

  if (IsLoad || IsStore) {
    unsigned AM = HII->getAddrMode(MI);
    switch (AM) {
      // (Re: ##Off + Rb<<S) = Rd: ##Val
      case HexagonII::Absolute:       // (__: ## + __<<_)
        break;
      case HexagonII::AbsoluteSet:    // (Rd: ## + __<<_)
        ED.Rd = MI.getOperand(OpNum-1);
        ED.IsDef = true;
        break;
      case HexagonII::BaseImmOffset:  // (__: ## + Rs<<0)
        // Store-immediates are treated as non-memory operations, since
        // it's the value being stored that is extended (as opposed to
        // a part of the address).
        if (!isStoreImmediate(Opc))
          ED.Expr.Rs = MI.getOperand(OpNum-1);
        break;
      case HexagonII::BaseLongOffset: // (__: ## + Rs<<S)
        ED.Expr.Rs = MI.getOperand(OpNum-2);
        ED.Expr.S = MI.getOperand(OpNum-1).getImm();
        break;
      default:
        llvm_unreachable("Unhandled memory instruction");
    }
  } else {
    switch (Opc) {
      case Hexagon::A2_tfrsi:         // (Rd: ## + __<<_)
        ED.Rd = MI.getOperand(0);
        ED.IsDef = true;
        break;
      case Hexagon::A2_combineii:     // (Rd: ## + __<<_)
      case Hexagon::A4_combineir:
        ED.Rd = { MI.getOperand(0).getReg(), Hexagon::isub_hi };
        ED.IsDef = true;
        break;
      case Hexagon::A4_combineri:     // (Rd: ## + __<<_)
        ED.Rd = { MI.getOperand(0).getReg(), Hexagon::isub_lo };
        ED.IsDef = true;
        break;
      case Hexagon::A2_addi:          // (Rd: ## + Rs<<0)
        ED.Rd = MI.getOperand(0);
        ED.Expr.Rs = MI.getOperand(OpNum-1);
        break;
      case Hexagon::M2_accii:         // (__: ## + Rs<<0)
      case Hexagon::M2_naccii:
      case Hexagon::S4_addaddi:
        ED.Expr.Rs = MI.getOperand(OpNum-1);
        break;
      case Hexagon::A2_subri:         // (Rd: ## - Rs<<0)
        ED.Rd = MI.getOperand(0);
        ED.Expr.Rs = MI.getOperand(OpNum+1);
        ED.Expr.Neg = true;
        break;
      case Hexagon::S4_subaddi:       // (__: ## - Rs<<0)
        ED.Expr.Rs = MI.getOperand(OpNum+1);
        ED.Expr.Neg = true;
        break;
      default:                        // (__: ## + __<<_)
        break;
    }
  }

  ED.UseMI = &MI;

  // Ignore unnamed globals.
  ExtRoot ER(ED.getOp());
  if (ER.Kind == MachineOperand::MO_GlobalAddress)
    if (ER.V.GV->getName().empty())
      return;
  // Ignore block address that points to block in another function
  if (ER.Kind == MachineOperand::MO_BlockAddress)
    if (ER.V.BA->getFunction() != &(MI.getMF()->getFunction()))
      return;
  Extenders.push_back(ED);
}

void HCE::collectInstr(MachineInstr &MI) {
  if (!HII->isConstExtended(MI))
    return;

  // Skip some non-convertible instructions.
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::M2_macsin:  // There is no Rx -= mpyi(Rs,Rt).
    case Hexagon::C4_addipc:
    case Hexagon::S4_or_andi:
    case Hexagon::S4_or_andix:
    case Hexagon::S4_or_ori:
      return;
  }
  recordExtender(MI, HII->getCExtOpNum(MI));
}

void HCE::collect(MachineFunction &MF) {
  Extenders.clear();
  for (MachineBasicBlock &MBB : MF) {
    // Skip unreachable blocks.
    if (MBB.getNumber() == -1)
      continue;
    for (MachineInstr &MI : MBB)
      collectInstr(MI);
  }
}

void HCE::assignInits(const ExtRoot &ER, unsigned Begin, unsigned End,
      AssignmentMap &IMap) {
  // Basic correctness: make sure that all extenders in the range [Begin..End)
  // share the same root ER.
  for (unsigned I = Begin; I != End; ++I)
    assert(ER == ExtRoot(Extenders[I].getOp()));

  // Construct the list of ranges, such that for each P in Ranges[I],
  // a register Reg = ER+P can be used in place of Extender[I]. If the
  // instruction allows, uses in the form of Reg+Off are considered
  // (here, Off = required_value - P).
  std::vector<OffsetRange> Ranges(End-Begin);

  // For each extender that is a def, visit all uses of the defined register,
  // and produce an offset range that works for all uses. The def doesn't
  // have to be checked, because it can become dead if all uses can be updated
  // to use a different reg/offset.
  for (unsigned I = Begin; I != End; ++I) {
    const ExtDesc &ED = Extenders[I];
    if (!ED.IsDef)
      continue;
    ExtValue EV(ED);
    LLVM_DEBUG(dbgs() << " =" << I << ". " << EV << "  " << ED << '\n');
    assert(ED.Rd.Reg != 0);
    Ranges[I-Begin] = getOffsetRange(ED.Rd).shift(EV.Offset);
    // A2_tfrsi is a special case: it will be replaced with A2_addi, which
    // has a 16-bit signed offset. This means that A2_tfrsi not only has a
    // range coming from its uses, but also from the fact that its replacement
    // has a range as well.
    if (ED.UseMI->getOpcode() == Hexagon::A2_tfrsi) {
      int32_t D = alignDown(32767, Ranges[I-Begin].Align); // XXX hardcoded
      Ranges[I-Begin].extendBy(-D).extendBy(D);
    }
  }

  // Visit all non-def extenders. For each one, determine the offset range
  // available for it.
  for (unsigned I = Begin; I != End; ++I) {
    const ExtDesc &ED = Extenders[I];
    if (ED.IsDef)
      continue;
    ExtValue EV(ED);
    LLVM_DEBUG(dbgs() << "  " << I << ". " << EV << "  " << ED << '\n');
    OffsetRange Dev = getOffsetRange(ED);
    Ranges[I-Begin].intersect(Dev.shift(EV.Offset));
  }

  // Here for each I there is a corresponding Range[I]. Construct the
  // inverse map, that to each range will assign the set of indexes in
  // [Begin..End) that this range corresponds to.
  std::map<OffsetRange, IndexList> RangeMap;
  for (unsigned I = Begin; I != End; ++I)
    RangeMap[Ranges[I-Begin]].insert(I);

  LLVM_DEBUG({
    dbgs() << "Ranges\n";
    for (unsigned I = Begin; I != End; ++I)
      dbgs() << "  " << I << ". " << Ranges[I-Begin] << '\n';
    dbgs() << "RangeMap\n";
    for (auto &P : RangeMap) {
      dbgs() << "  " << P.first << " ->";
      for (unsigned I : P.second)
        dbgs() << ' ' << I;
      dbgs() << '\n';
    }
  });

  // Select the definition points, and generate the assignment between
  // these points and the uses.

  // For each candidate offset, keep a pair CandData consisting of
  // the total number of ranges containing that candidate, and the
  // vector of corresponding RangeTree nodes.
  using CandData = std::pair<unsigned, SmallVector<RangeTree::Node*,8>>;
  std::map<int32_t, CandData> CandMap;

  RangeTree Tree;
  for (const OffsetRange &R : Ranges)
    Tree.add(R);
  SmallVector<RangeTree::Node*,8> Nodes;
  Tree.order(Nodes);

  auto MaxAlign = [](const SmallVectorImpl<RangeTree::Node*> &Nodes,
                     uint8_t Align, uint8_t Offset) {
    for (RangeTree::Node *N : Nodes) {
      if (N->Range.Align <= Align || N->Range.Offset < Offset)
        continue;
      if ((N->Range.Offset - Offset) % Align != 0)
        continue;
      Align = N->Range.Align;
      Offset = N->Range.Offset;
    }
    return std::make_pair(Align, Offset);
  };

  // Construct the set of all potential definition points from the endpoints
  // of the ranges. If a given endpoint also belongs to a different range,
  // but with a higher alignment, also consider the more-highly-aligned
  // value of this endpoint.
  std::set<int32_t> CandSet;
  for (RangeTree::Node *N : Nodes) {
    const OffsetRange &R = N->Range;
    auto P0 = MaxAlign(Tree.nodesWith(R.Min, false), R.Align, R.Offset);
    CandSet.insert(R.Min);
    if (R.Align < P0.first)
      CandSet.insert(adjustUp(R.Min, P0.first, P0.second));
    auto P1 = MaxAlign(Tree.nodesWith(R.Max, false), R.Align, R.Offset);
    CandSet.insert(R.Max);
    if (R.Align < P1.first)
      CandSet.insert(adjustDown(R.Max, P1.first, P1.second));
  }

  // Build the assignment map: candidate C -> { list of extender indexes }.
  // This has to be done iteratively:
  // - pick the candidate that covers the maximum number of extenders,
  // - add the candidate to the map,
  // - remove the extenders from the pool.
  while (true) {
    using CMap = std::map<int32_t,unsigned>;
    CMap Counts;
    for (auto It = CandSet.begin(), Et = CandSet.end(); It != Et; ) {
      auto &&V = Tree.nodesWith(*It);
      unsigned N = std::accumulate(V.begin(), V.end(), 0u,
                    [](unsigned Acc, const RangeTree::Node *N) {
                      return Acc + N->Count;
                    });
      if (N != 0)
        Counts.insert({*It, N});
      It = (N != 0) ? std::next(It) : CandSet.erase(It);
    }
    if (Counts.empty())
      break;

    // Find the best candidate with respect to the number of extenders covered.
    auto BestIt = llvm::max_element(
        Counts, [](const CMap::value_type &A, const CMap::value_type &B) {
          return A.second < B.second || (A.second == B.second && A < B);
        });
    int32_t Best = BestIt->first;
    ExtValue BestV(ER, Best);
    for (RangeTree::Node *N : Tree.nodesWith(Best)) {
      for (unsigned I : RangeMap[N->Range])
        IMap[{BestV,Extenders[I].Expr}].insert(I);
      Tree.erase(N);
    }
  }

  LLVM_DEBUG(dbgs() << "IMap (before fixup) = " << PrintIMap(IMap, *HRI));

  // There is some ambiguity in what initializer should be used, if the
  // descriptor's subexpression is non-trivial: it can be the entire
  // subexpression (which is what has been done so far), or it can be
  // the extender's value itself, if all corresponding extenders have the
  // exact value of the initializer (i.e. require offset of 0).

  // To reduce the number of initializers, merge such special cases.
  for (std::pair<const ExtenderInit,IndexList> &P : IMap) {
    // Skip trivial initializers.
    if (P.first.second.trivial())
      continue;
    // If the corresponding trivial initializer does not exist, skip this
    // entry.
    const ExtValue &EV = P.first.first;
    AssignmentMap::iterator F = IMap.find({EV, ExtExpr()});
    if (F == IMap.end())
      continue;

    // Finally, check if all extenders have the same value as the initializer.
    // Make sure that extenders that are a part of a stack address are not
    // merged with those that aren't. Stack addresses need an offset field
    // (to be used by frame index elimination), while non-stack expressions
    // can be replaced with forms (such as rr) that do not have such a field.
    // Example:
    //
    // Collected 3 extenders
    //  =2. imm:0  off:32968  bb#2: %7 = ## + __ << 0, def
    //   0. imm:0  off:267  bb#0: __ = ## + SS#1 << 0
    //   1. imm:0  off:267  bb#1: __ = ## + SS#1 << 0
    // Ranges
    //   0. [-756,267]a1+0
    //   1. [-756,267]a1+0
    //   2. [201,65735]a1+0
    // RangeMap
    //   [-756,267]a1+0 -> 0 1
    //   [201,65735]a1+0 -> 2
    // IMap (before fixup) = {
    //   [imm:0  off:267, ## + __ << 0] -> { 2 }
    //   [imm:0  off:267, ## + SS#1 << 0] -> { 0 1 }
    // }
    // IMap (after fixup) = {
    //   [imm:0  off:267, ## + __ << 0] -> { 2 0 1 }
    //   [imm:0  off:267, ## + SS#1 << 0] -> { }
    // }
    // Inserted def in bb#0 for initializer: [imm:0  off:267, ## + __ << 0]
    //   %12:intregs = A2_tfrsi 267
    //
    // The result was
    //   %12:intregs = A2_tfrsi 267
    //   S4_pstorerbt_rr %3, %12, %stack.1, 0, killed %4
    // Which became
    //   r0 = #267
    //   if (p0.new) memb(r0+r29<<#4) = r2

    bool IsStack = any_of(F->second, [this](unsigned I) {
                      return Extenders[I].Expr.Rs.isSlot();
                   });
    auto SameValue = [&EV,this,IsStack](unsigned I) {
      const ExtDesc &ED = Extenders[I];
      return ED.Expr.Rs.isSlot() == IsStack &&
             ExtValue(ED).Offset == EV.Offset;
    };
    if (all_of(P.second, SameValue)) {
      F->second.insert(P.second.begin(), P.second.end());
      P.second.clear();
    }
  }

  LLVM_DEBUG(dbgs() << "IMap (after fixup) = " << PrintIMap(IMap, *HRI));
}

void HCE::calculatePlacement(const ExtenderInit &ExtI, const IndexList &Refs,
      LocDefList &Defs) {
  if (Refs.empty())
    return;

  // The placement calculation is somewhat simple right now: it finds a
  // single location for the def that dominates all refs. Since this may
  // place the def far from the uses, producing several locations for
  // defs that collectively dominate all refs could be better.
  // For now only do the single one.
  DenseSet<MachineBasicBlock*> Blocks;
  DenseSet<MachineInstr*> RefMIs;
  const ExtDesc &ED0 = Extenders[Refs[0]];
  MachineBasicBlock *DomB = ED0.UseMI->getParent();
  RefMIs.insert(ED0.UseMI);
  Blocks.insert(DomB);
  for (unsigned i = 1, e = Refs.size(); i != e; ++i) {
    const ExtDesc &ED = Extenders[Refs[i]];
    MachineBasicBlock *MBB = ED.UseMI->getParent();
    RefMIs.insert(ED.UseMI);
    DomB = MDT->findNearestCommonDominator(DomB, MBB);
    Blocks.insert(MBB);
  }

#ifndef NDEBUG
  // The block DomB should be dominated by the def of each register used
  // in the initializer.
  Register Rs = ExtI.second.Rs;  // Only one reg allowed now.
  const MachineInstr *DefI = Rs.isVReg() ? MRI->getVRegDef(Rs.Reg) : nullptr;

  // This should be guaranteed given that the entire expression is used
  // at each instruction in Refs. Add an assertion just in case.
  assert(!DefI || MDT->dominates(DefI->getParent(), DomB));
#endif

  MachineBasicBlock::iterator It;
  if (Blocks.count(DomB)) {
    // Try to find the latest possible location for the def.
    MachineBasicBlock::iterator End = DomB->end();
    for (It = DomB->begin(); It != End; ++It)
      if (RefMIs.count(&*It))
        break;
    assert(It != End && "Should have found a ref in DomB");
  } else {
    // DomB does not contain any refs.
    It = DomB->getFirstTerminator();
  }
  Loc DefLoc(DomB, It);
  Defs.emplace_back(DefLoc, Refs);
}

HCE::Register HCE::insertInitializer(Loc DefL, const ExtenderInit &ExtI) {
  llvm::Register DefR = MRI->createVirtualRegister(&Hexagon::IntRegsRegClass);
  MachineBasicBlock &MBB = *DefL.Block;
  MachineBasicBlock::iterator At = DefL.At;
  DebugLoc dl = DefL.Block->findDebugLoc(DefL.At);
  const ExtValue &EV = ExtI.first;
  MachineOperand ExtOp(EV);

  const ExtExpr &Ex = ExtI.second;
  const MachineInstr *InitI = nullptr;

  if (Ex.Rs.isSlot()) {
    assert(Ex.S == 0 && "Cannot have a shift of a stack slot");
    assert(!Ex.Neg && "Cannot subtract a stack slot");
    // DefR = PS_fi Rb,##EV
    InitI = BuildMI(MBB, At, dl, HII->get(Hexagon::PS_fi), DefR)
              .add(MachineOperand(Ex.Rs))
              .add(ExtOp);
  } else {
    assert((Ex.Rs.Reg == 0 || Ex.Rs.isVReg()) && "Expecting virtual register");
    if (Ex.trivial()) {
      // DefR = ##EV
      InitI = BuildMI(MBB, At, dl, HII->get(Hexagon::A2_tfrsi), DefR)
                .add(ExtOp);
    } else if (Ex.S == 0) {
      if (Ex.Neg) {
        // DefR = sub(##EV,Rb)
        InitI = BuildMI(MBB, At, dl, HII->get(Hexagon::A2_subri), DefR)
                  .add(ExtOp)
                  .add(MachineOperand(Ex.Rs));
      } else {
        // DefR = add(Rb,##EV)
        InitI = BuildMI(MBB, At, dl, HII->get(Hexagon::A2_addi), DefR)
                  .add(MachineOperand(Ex.Rs))
                  .add(ExtOp);
      }
    } else {
      if (HST->useCompound()) {
        unsigned NewOpc = Ex.Neg ? Hexagon::S4_subi_asl_ri
                                 : Hexagon::S4_addi_asl_ri;
        // DefR = add(##EV,asl(Rb,S))
        InitI = BuildMI(MBB, At, dl, HII->get(NewOpc), DefR)
                  .add(ExtOp)
                  .add(MachineOperand(Ex.Rs))
                  .addImm(Ex.S);
      } else {
        // No compounds are available. It is not clear whether we should
        // even process such extenders where the initializer cannot be
        // a single instruction, but do it for now.
        llvm::Register TmpR = MRI->createVirtualRegister(&Hexagon::IntRegsRegClass);
        BuildMI(MBB, At, dl, HII->get(Hexagon::S2_asl_i_r), TmpR)
          .add(MachineOperand(Ex.Rs))
          .addImm(Ex.S);
        if (Ex.Neg)
          InitI = BuildMI(MBB, At, dl, HII->get(Hexagon::A2_subri), DefR)
                    .add(ExtOp)
                    .add(MachineOperand(Register(TmpR, 0)));
        else
          InitI = BuildMI(MBB, At, dl, HII->get(Hexagon::A2_addi), DefR)
                    .add(MachineOperand(Register(TmpR, 0)))
                    .add(ExtOp);
      }
    }
  }

  assert(InitI);
  (void)InitI;
  LLVM_DEBUG(dbgs() << "Inserted def in bb#" << MBB.getNumber()
                    << " for initializer: " << PrintInit(ExtI, *HRI) << "\n  "
                    << *InitI);
  return { DefR, 0 };
}

// Replace the extender at index Idx with the register ExtR.
bool HCE::replaceInstrExact(const ExtDesc &ED, Register ExtR) {
  MachineInstr &MI = *ED.UseMI;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineBasicBlock::iterator At = MI.getIterator();
  DebugLoc dl = MI.getDebugLoc();
  unsigned ExtOpc = MI.getOpcode();

  // With a few exceptions, direct replacement amounts to creating an
  // instruction with a corresponding register opcode, with all operands
  // the same, except for the register used in place of the extender.
  unsigned RegOpc = getDirectRegReplacement(ExtOpc);

  if (RegOpc == TargetOpcode::REG_SEQUENCE) {
    if (ExtOpc == Hexagon::A4_combineri)
      BuildMI(MBB, At, dl, HII->get(RegOpc))
        .add(MI.getOperand(0))
        .add(MI.getOperand(1))
        .addImm(Hexagon::isub_hi)
        .add(MachineOperand(ExtR))
        .addImm(Hexagon::isub_lo);
    else if (ExtOpc == Hexagon::A4_combineir)
      BuildMI(MBB, At, dl, HII->get(RegOpc))
        .add(MI.getOperand(0))
        .add(MachineOperand(ExtR))
        .addImm(Hexagon::isub_hi)
        .add(MI.getOperand(2))
        .addImm(Hexagon::isub_lo);
    else
      llvm_unreachable("Unexpected opcode became REG_SEQUENCE");
    MBB.erase(MI);
    return true;
  }
  if (ExtOpc == Hexagon::C2_cmpgei || ExtOpc == Hexagon::C2_cmpgeui) {
    unsigned NewOpc = ExtOpc == Hexagon::C2_cmpgei ? Hexagon::C2_cmplt
                                                   : Hexagon::C2_cmpltu;
    BuildMI(MBB, At, dl, HII->get(NewOpc))
      .add(MI.getOperand(0))
      .add(MachineOperand(ExtR))
      .add(MI.getOperand(1));
    MBB.erase(MI);
    return true;
  }

  if (RegOpc != 0) {
    MachineInstrBuilder MIB = BuildMI(MBB, At, dl, HII->get(RegOpc));
    unsigned RegN = ED.OpNum;
    // Copy all operands except the one that has the extender.
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      if (i != RegN)
        MIB.add(MI.getOperand(i));
      else
        MIB.add(MachineOperand(ExtR));
    }
    MIB.cloneMemRefs(MI);
    MBB.erase(MI);
    return true;
  }

  if (MI.mayLoadOrStore() && !isStoreImmediate(ExtOpc)) {
    // For memory instructions, there is an asymmetry in the addressing
    // modes. Addressing modes allowing extenders can be replaced with
    // addressing modes that use registers, but the order of operands
    // (or even their number) may be different.
    // Replacements:
    //   BaseImmOffset (io)  -> BaseRegOffset (rr)
    //   BaseLongOffset (ur) -> BaseRegOffset (rr)
    unsigned RegOpc, Shift;
    unsigned AM = HII->getAddrMode(MI);
    if (AM == HexagonII::BaseImmOffset) {
      RegOpc = HII->changeAddrMode_io_rr(ExtOpc);
      Shift = 0;
    } else if (AM == HexagonII::BaseLongOffset) {
      // Loads:  Rd = L4_loadri_ur Rs, S, ##
      // Stores: S4_storeri_ur Rs, S, ##, Rt
      RegOpc = HII->changeAddrMode_ur_rr(ExtOpc);
      Shift = MI.getOperand(MI.mayLoad() ? 2 : 1).getImm();
    } else {
      llvm_unreachable("Unexpected addressing mode");
    }
#ifndef NDEBUG
    if (RegOpc == -1u) {
      dbgs() << "\nExtOpc: " << HII->getName(ExtOpc) << " has no rr version\n";
      llvm_unreachable("No corresponding rr instruction");
    }
#endif

    unsigned BaseP, OffP;
    HII->getBaseAndOffsetPosition(MI, BaseP, OffP);

    // Build an rr instruction: (RegOff + RegBase<<0)
    MachineInstrBuilder MIB = BuildMI(MBB, At, dl, HII->get(RegOpc));
    // First, add the def for loads.
    if (MI.mayLoad())
      MIB.add(getLoadResultOp(MI));
    // Handle possible predication.
    if (HII->isPredicated(MI))
      MIB.add(getPredicateOp(MI));
    // Build the address.
    MIB.add(MachineOperand(ExtR));      // RegOff
    MIB.add(MI.getOperand(BaseP));      // RegBase
    MIB.addImm(Shift);                  // << Shift
    // Add the stored value for stores.
    if (MI.mayStore())
      MIB.add(getStoredValueOp(MI));
    MIB.cloneMemRefs(MI);
    MBB.erase(MI);
    return true;
  }

#ifndef NDEBUG
  dbgs() << '\n' << MI;
#endif
  llvm_unreachable("Unhandled exact replacement");
  return false;
}

// Replace the extender ED with a form corresponding to the initializer ExtI.
bool HCE::replaceInstrExpr(const ExtDesc &ED, const ExtenderInit &ExtI,
      Register ExtR, int32_t &Diff) {
  MachineInstr &MI = *ED.UseMI;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineBasicBlock::iterator At = MI.getIterator();
  DebugLoc dl = MI.getDebugLoc();
  unsigned ExtOpc = MI.getOpcode();

  if (ExtOpc == Hexagon::A2_tfrsi) {
    // A2_tfrsi is a special case: it's replaced with A2_addi, which introduces
    // another range. One range is the one that's common to all tfrsi's uses,
    // this one is the range of immediates in A2_addi. When calculating ranges,
    // the addi's 16-bit argument was included, so now we need to make it such
    // that the produced value is in the range for the uses alone.
    // Most of the time, simply adding Diff will make the addi produce exact
    // result, but if Diff is outside of the 16-bit range, some adjustment
    // will be needed.
    unsigned IdxOpc = getRegOffOpcode(ExtOpc);
    assert(IdxOpc == Hexagon::A2_addi);

    // Clamp Diff to the 16 bit range.
    int32_t D = isInt<16>(Diff) ? Diff : (Diff > 0 ? 32767 : -32768);
    if (Diff > 32767) {
      // Split Diff into two values: one that is close to min/max int16,
      // and the other being the rest, and such that both have the same
      // "alignment" as Diff.
      uint32_t UD = Diff;
      OffsetRange R = getOffsetRange(MI.getOperand(0));
      uint32_t A = std::min<uint32_t>(R.Align, 1u << llvm::countr_zero(UD));
      D &= ~(A-1);
    }
    BuildMI(MBB, At, dl, HII->get(IdxOpc))
      .add(MI.getOperand(0))
      .add(MachineOperand(ExtR))
      .addImm(D);
    Diff -= D;
#ifndef NDEBUG
    // Make sure the output is within allowable range for uses.
    // "Diff" is a difference in the "opposite direction", i.e. Ext - DefV,
    // not DefV - Ext, as the getOffsetRange would calculate.
    OffsetRange Uses = getOffsetRange(MI.getOperand(0));
    if (!Uses.contains(-Diff))
      dbgs() << "Diff: " << -Diff << " out of range " << Uses
             << " for " << MI;
    assert(Uses.contains(-Diff));
#endif
    MBB.erase(MI);
    return true;
  }

  const ExtValue &EV = ExtI.first; (void)EV;
  const ExtExpr &Ex = ExtI.second; (void)Ex;

  if (ExtOpc == Hexagon::A2_addi || ExtOpc == Hexagon::A2_subri) {
    // If addi/subri are replaced with the exactly matching initializer,
    // they amount to COPY.
    // Check that the initializer is an exact match (for simplicity).
#ifndef NDEBUG
    bool IsAddi = ExtOpc == Hexagon::A2_addi;
    const MachineOperand &RegOp = MI.getOperand(IsAddi ? 1 : 2);
    const MachineOperand &ImmOp = MI.getOperand(IsAddi ? 2 : 1);
    assert(Ex.Rs == RegOp && EV == ImmOp && Ex.Neg != IsAddi &&
           "Initializer mismatch");
#endif
    BuildMI(MBB, At, dl, HII->get(TargetOpcode::COPY))
      .add(MI.getOperand(0))
      .add(MachineOperand(ExtR));
    Diff = 0;
    MBB.erase(MI);
    return true;
  }
  if (ExtOpc == Hexagon::M2_accii || ExtOpc == Hexagon::M2_naccii ||
      ExtOpc == Hexagon::S4_addaddi || ExtOpc == Hexagon::S4_subaddi) {
    // M2_accii:    add(Rt,add(Rs,V)) (tied)
    // M2_naccii:   sub(Rt,add(Rs,V))
    // S4_addaddi:  add(Rt,add(Rs,V))
    // S4_subaddi:  add(Rt,sub(V,Rs))
    // Check that Rs and V match the initializer expression. The Rs+V is the
    // combination that is considered "subexpression" for V, although Rx+V
    // would also be valid.
#ifndef NDEBUG
    bool IsSub = ExtOpc == Hexagon::S4_subaddi;
    Register Rs = MI.getOperand(IsSub ? 3 : 2);
    ExtValue V = MI.getOperand(IsSub ? 2 : 3);
    assert(EV == V && Rs == Ex.Rs && IsSub == Ex.Neg && "Initializer mismatch");
#endif
    unsigned NewOpc = ExtOpc == Hexagon::M2_naccii ? Hexagon::A2_sub
                                                   : Hexagon::A2_add;
    BuildMI(MBB, At, dl, HII->get(NewOpc))
      .add(MI.getOperand(0))
      .add(MI.getOperand(1))
      .add(MachineOperand(ExtR));
    MBB.erase(MI);
    return true;
  }

  if (MI.mayLoadOrStore()) {
    unsigned IdxOpc = getRegOffOpcode(ExtOpc);
    assert(IdxOpc && "Expecting indexed opcode");
    MachineInstrBuilder MIB = BuildMI(MBB, At, dl, HII->get(IdxOpc));
    // Construct the new indexed instruction.
    // First, add the def for loads.
    if (MI.mayLoad())
      MIB.add(getLoadResultOp(MI));
    // Handle possible predication.
    if (HII->isPredicated(MI))
      MIB.add(getPredicateOp(MI));
    // Build the address.
    MIB.add(MachineOperand(ExtR));
    MIB.addImm(Diff);
    // Add the stored value for stores.
    if (MI.mayStore())
      MIB.add(getStoredValueOp(MI));
    MIB.cloneMemRefs(MI);
    MBB.erase(MI);
    return true;
  }

#ifndef NDEBUG
  dbgs() << '\n' << PrintInit(ExtI, *HRI) << "  " << MI;
#endif
  llvm_unreachable("Unhandled expr replacement");
  return false;
}

bool HCE::replaceInstr(unsigned Idx, Register ExtR, const ExtenderInit &ExtI) {
  if (ReplaceLimit.getNumOccurrences()) {
    if (ReplaceLimit <= ReplaceCounter)
      return false;
    ++ReplaceCounter;
  }
  const ExtDesc &ED = Extenders[Idx];
  assert((!ED.IsDef || ED.Rd.Reg != 0) && "Missing Rd for def");
  const ExtValue &DefV = ExtI.first;
  assert(ExtRoot(ExtValue(ED)) == ExtRoot(DefV) && "Extender root mismatch");
  const ExtExpr &DefEx = ExtI.second;

  ExtValue EV(ED);
  int32_t Diff = EV.Offset - DefV.Offset;
  const MachineInstr &MI = *ED.UseMI;
  LLVM_DEBUG(dbgs() << __func__ << " Idx:" << Idx << " ExtR:"
                    << PrintRegister(ExtR, *HRI) << " Diff:" << Diff << '\n');

  // These two addressing modes must be converted into indexed forms
  // regardless of what the initializer looks like.
  bool IsAbs = false, IsAbsSet = false;
  if (MI.mayLoadOrStore()) {
    unsigned AM = HII->getAddrMode(MI);
    IsAbs = AM == HexagonII::Absolute;
    IsAbsSet = AM == HexagonII::AbsoluteSet;
  }

  // If it's a def, remember all operands that need to be updated.
  // If ED is a def, and Diff is not 0, then all uses of the register Rd
  // defined by ED must be in the form (Rd, imm), i.e. the immediate offset
  // must follow the Rd in the operand list.
  std::vector<std::pair<MachineInstr*,unsigned>> RegOps;
  if (ED.IsDef && Diff != 0) {
    for (MachineOperand &Op : MRI->use_operands(ED.Rd.Reg)) {
      MachineInstr &UI = *Op.getParent();
      RegOps.push_back({&UI, getOperandIndex(UI, Op)});
    }
  }

  // Replace the instruction.
  bool Replaced = false;
  if (Diff == 0 && DefEx.trivial() && !IsAbs && !IsAbsSet)
    Replaced = replaceInstrExact(ED, ExtR);
  else
    Replaced = replaceInstrExpr(ED, ExtI, ExtR, Diff);

  if (Diff != 0 && Replaced && ED.IsDef) {
    // Update offsets of the def's uses.
    for (std::pair<MachineInstr*,unsigned> P : RegOps) {
      unsigned J = P.second;
      assert(P.first->getNumOperands() > J+1 &&
             P.first->getOperand(J+1).isImm());
      MachineOperand &ImmOp = P.first->getOperand(J+1);
      ImmOp.setImm(ImmOp.getImm() + Diff);
    }
    // If it was an absolute-set instruction, the "set" part has been removed.
    // ExtR will now be the register with the extended value, and since all
    // users of Rd have been updated, all that needs to be done is to replace
    // Rd with ExtR.
    if (IsAbsSet) {
      assert(ED.Rd.Sub == 0 && ExtR.Sub == 0);
      MRI->replaceRegWith(ED.Rd.Reg, ExtR.Reg);
    }
  }

  return Replaced;
}

bool HCE::replaceExtenders(const AssignmentMap &IMap) {
  LocDefList Defs;
  bool Changed = false;

  for (const std::pair<const ExtenderInit, IndexList> &P : IMap) {
    const IndexList &Idxs = P.second;
    if (Idxs.size() < CountThreshold)
      continue;

    Defs.clear();
    calculatePlacement(P.first, Idxs, Defs);
    for (const std::pair<Loc,IndexList> &Q : Defs) {
      Register DefR = insertInitializer(Q.first, P.first);
      NewRegs.push_back(DefR.Reg);
      for (unsigned I : Q.second)
        Changed |= replaceInstr(I, DefR, P.first);
    }
  }
  return Changed;
}

unsigned HCE::getOperandIndex(const MachineInstr &MI,
      const MachineOperand &Op) const {
  for (unsigned i = 0, n = MI.getNumOperands(); i != n; ++i)
    if (&MI.getOperand(i) == &Op)
      return i;
  llvm_unreachable("Not an operand of MI");
}

const MachineOperand &HCE::getPredicateOp(const MachineInstr &MI) const {
  assert(HII->isPredicated(MI));
  for (const MachineOperand &Op : MI.operands()) {
    if (!Op.isReg() || !Op.isUse() ||
        MRI->getRegClass(Op.getReg()) != &Hexagon::PredRegsRegClass)
      continue;
    assert(Op.getSubReg() == 0 && "Predicate register with a subregister");
    return Op;
  }
  llvm_unreachable("Predicate operand not found");
}

const MachineOperand &HCE::getLoadResultOp(const MachineInstr &MI) const {
  assert(MI.mayLoad());
  return MI.getOperand(0);
}

const MachineOperand &HCE::getStoredValueOp(const MachineInstr &MI) const {
  assert(MI.mayStore());
  return MI.getOperand(MI.getNumExplicitOperands()-1);
}

bool HCE::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;
  if (MF.getFunction().hasPersonalityFn()) {
    LLVM_DEBUG(dbgs() << getPassName() << ": skipping " << MF.getName()
                      << " due to exception handling\n");
    return false;
  }
  LLVM_DEBUG(MF.print(dbgs() << "Before " << getPassName() << '\n', nullptr));

  HST = &MF.getSubtarget<HexagonSubtarget>();
  HII = HST->getInstrInfo();
  HRI = HST->getRegisterInfo();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MRI = &MF.getRegInfo();
  AssignmentMap IMap;

  collect(MF);
  llvm::sort(Extenders, [this](const ExtDesc &A, const ExtDesc &B) {
    ExtValue VA(A), VB(B);
    if (VA != VB)
      return VA < VB;
    const MachineInstr *MA = A.UseMI;
    const MachineInstr *MB = B.UseMI;
    if (MA == MB) {
      // If it's the same instruction, compare operand numbers.
      return A.OpNum < B.OpNum;
    }

    const MachineBasicBlock *BA = MA->getParent();
    const MachineBasicBlock *BB = MB->getParent();
    assert(BA->getNumber() != -1 && BB->getNumber() != -1);
    if (BA != BB)
      return BA->getNumber() < BB->getNumber();
    return MDT->dominates(MA, MB);
  });

  bool Changed = false;
  LLVM_DEBUG(dbgs() << "Collected " << Extenders.size() << " extenders\n");
  for (unsigned I = 0, E = Extenders.size(); I != E; ) {
    unsigned B = I;
    const ExtRoot &T = Extenders[B].getOp();
    while (I != E && ExtRoot(Extenders[I].getOp()) == T)
      ++I;

    IMap.clear();
    assignInits(T, B, I, IMap);
    Changed |= replaceExtenders(IMap);
  }

  LLVM_DEBUG({
    if (Changed)
      MF.print(dbgs() << "After " << getPassName() << '\n', nullptr);
    else
      dbgs() << "No changes\n";
  });
  return Changed;
}

FunctionPass *llvm::createHexagonConstExtenders() {
  return new HexagonConstExtenders();
}
