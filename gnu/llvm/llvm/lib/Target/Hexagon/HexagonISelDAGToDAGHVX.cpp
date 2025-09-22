//===-- HexagonISelDAGToDAGHVX.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonISelDAGToDAG.h"
#include "HexagonISelLowering.h"
#include "HexagonTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsHexagon.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#define DEBUG_TYPE "hexagon-isel"
using namespace llvm;

namespace {

// --------------------------------------------------------------------
// Implementation of permutation networks.

// Implementation of the node routing through butterfly networks:
// - Forward delta.
// - Reverse delta.
// - Benes.
//
//
// Forward delta network consists of log(N) steps, where N is the number
// of inputs. In each step, an input can stay in place, or it can get
// routed to another position[1]. The step after that consists of two
// networks, each half in size in terms of the number of nodes. In those
// terms, in the given step, an input can go to either the upper or the
// lower network in the next step.
//
// [1] Hexagon's vdelta/vrdelta allow an element to be routed to both
// positions as long as there is no conflict.

// Here's a delta network for 8 inputs, only the switching routes are
// shown:
//
//         Steps:
//         |- 1 ---------------|- 2 -----|- 3 -|
//
// Inp[0] ***                 ***       ***   *** Out[0]
//           \               /   \     /   \ /
//            \             /     \   /     X
//             \           /       \ /     / \
// Inp[1] ***   \         /   ***   X   ***   *** Out[1]
//           \   \       /   /   \ / \ /
//            \   \     /   /     X   X
//             \   \   /   /     / \ / \
// Inp[2] ***   \   \ /   /   ***   X   ***   *** Out[2]
//           \   \   X   /   /     / \     \ /
//            \   \ / \ /   /     /   \     X
//             \   X   X   /     /     \   / \
// Inp[3] ***   \ / \ / \ /   ***       ***   *** Out[3]
//           \   X   X   X   /
//            \ / \ / \ / \ /
//             X   X   X   X
//            / \ / \ / \ / \
//           /   X   X   X   \
// Inp[4] ***   / \ / \ / \   ***       ***   *** Out[4]
//             /   X   X   \     \     /   \ /
//            /   / \ / \   \     \   /     X
//           /   /   X   \   \     \ /     / \
// Inp[5] ***   /   / \   \   ***   X   ***   *** Out[5]
//             /   /   \   \     \ / \ /
//            /   /     \   \     X   X
//           /   /       \   \   / \ / \
// Inp[6] ***   /         \   ***   X   ***   *** Out[6]
//             /           \       / \     \ /
//            /             \     /   \     X
//           /               \   /     \   / \
// Inp[7] ***                 ***       ***   *** Out[7]
//
//
// Reverse delta network is same as delta network, with the steps in
// the opposite order.
//
//
// Benes network is a forward delta network immediately followed by
// a reverse delta network.

enum class ColorKind { None, Red, Black };

// Graph coloring utility used to partition nodes into two groups:
// they will correspond to nodes routed to the upper and lower networks.
struct Coloring {
  using Node = int;
  using MapType = std::map<Node, ColorKind>;
  static constexpr Node Ignore = Node(-1);

  Coloring(ArrayRef<Node> Ord) : Order(Ord) {
    build();
    if (!color())
      Colors.clear();
  }

  const MapType &colors() const {
    return Colors;
  }

  ColorKind other(ColorKind Color) {
    if (Color == ColorKind::None)
      return ColorKind::Red;
    return Color == ColorKind::Red ? ColorKind::Black : ColorKind::Red;
  }

  LLVM_DUMP_METHOD void dump() const;

private:
  ArrayRef<Node> Order;
  MapType Colors;
  std::set<Node> Needed;

  using NodeSet = std::set<Node>;
  std::map<Node,NodeSet> Edges;

  Node conj(Node Pos) {
    Node Num = Order.size();
    return (Pos < Num/2) ? Pos + Num/2 : Pos - Num/2;
  }

  ColorKind getColor(Node N) {
    auto F = Colors.find(N);
    return F != Colors.end() ? F->second : ColorKind::None;
  }

  std::pair<bool, ColorKind> getUniqueColor(const NodeSet &Nodes);

  void build();
  bool color();
};
} // namespace

std::pair<bool, ColorKind> Coloring::getUniqueColor(const NodeSet &Nodes) {
  auto Color = ColorKind::None;
  for (Node N : Nodes) {
    ColorKind ColorN = getColor(N);
    if (ColorN == ColorKind::None)
      continue;
    if (Color == ColorKind::None)
      Color = ColorN;
    else if (Color != ColorKind::None && Color != ColorN)
      return { false, ColorKind::None };
  }
  return { true, Color };
}

void Coloring::build() {
  // Add Order[P] and Order[conj(P)] to Edges.
  for (unsigned P = 0; P != Order.size(); ++P) {
    Node I = Order[P];
    if (I != Ignore) {
      Needed.insert(I);
      Node PC = Order[conj(P)];
      if (PC != Ignore && PC != I)
        Edges[I].insert(PC);
    }
  }
  // Add I and conj(I) to Edges.
  for (unsigned I = 0; I != Order.size(); ++I) {
    if (!Needed.count(I))
      continue;
    Node C = conj(I);
    // This will create an entry in the edge table, even if I is not
    // connected to any other node. This is necessary, because it still
    // needs to be colored.
    NodeSet &Is = Edges[I];
    if (Needed.count(C))
      Is.insert(C);
  }
}

bool Coloring::color() {
  SetVector<Node> FirstQ;
  auto Enqueue = [this,&FirstQ] (Node N) {
    SetVector<Node> Q;
    Q.insert(N);
    for (unsigned I = 0; I != Q.size(); ++I) {
      NodeSet &Ns = Edges[Q[I]];
      Q.insert(Ns.begin(), Ns.end());
    }
    FirstQ.insert(Q.begin(), Q.end());
  };
  for (Node N : Needed)
    Enqueue(N);

  for (Node N : FirstQ) {
    if (Colors.count(N))
      continue;
    NodeSet &Ns = Edges[N];
    auto P = getUniqueColor(Ns);
    if (!P.first)
      return false;
    Colors[N] = other(P.second);
  }

  // First, color nodes that don't have any dups.
  for (auto E : Edges) {
    Node N = E.first;
    if (!Needed.count(conj(N)) || Colors.count(N))
      continue;
    auto P = getUniqueColor(E.second);
    if (!P.first)
      return false;
    Colors[N] = other(P.second);
  }

  // Now, nodes that are still uncolored. Since the graph can be modified
  // in this step, create a work queue.
  std::vector<Node> WorkQ;
  for (auto E : Edges) {
    Node N = E.first;
    if (!Colors.count(N))
      WorkQ.push_back(N);
  }

  for (Node N : WorkQ) {
    NodeSet &Ns = Edges[N];
    auto P = getUniqueColor(Ns);
    if (P.first) {
      Colors[N] = other(P.second);
      continue;
    }

    // Coloring failed. Split this node.
    Node C = conj(N);
    ColorKind ColorN = other(ColorKind::None);
    ColorKind ColorC = other(ColorN);
    NodeSet &Cs = Edges[C];
    NodeSet CopyNs = Ns;
    for (Node M : CopyNs) {
      ColorKind ColorM = getColor(M);
      if (ColorM == ColorC) {
        // Connect M with C, disconnect M from N.
        Cs.insert(M);
        Edges[M].insert(C);
        Ns.erase(M);
        Edges[M].erase(N);
      }
    }
    Colors[N] = ColorN;
    Colors[C] = ColorC;
  }

  // Explicitly assign "None" to all uncolored nodes.
  for (unsigned I = 0; I != Order.size(); ++I)
    if (Colors.count(I) == 0)
      Colors[I] = ColorKind::None;

  return true;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void Coloring::dump() const {
  dbgs() << "{ Order:   {";
  for (Node P : Order) {
    if (P != Ignore)
      dbgs() << ' ' << P;
    else
      dbgs() << " -";
  }
  dbgs() << " }\n";
  dbgs() << "  Needed: {";
  for (Node N : Needed)
    dbgs() << ' ' << N;
  dbgs() << " }\n";

  dbgs() << "  Edges: {\n";
  for (auto E : Edges) {
    dbgs() << "    " << E.first << " -> {";
    for (auto N : E.second)
      dbgs() << ' ' << N;
    dbgs() << " }\n";
  }
  dbgs() << "  }\n";

  auto ColorKindToName = [](ColorKind C) {
    switch (C) {
    case ColorKind::None:
      return "None";
    case ColorKind::Red:
      return "Red";
    case ColorKind::Black:
      return "Black";
    }
    llvm_unreachable("all ColorKinds should be handled by the switch above");
  };

  dbgs() << "  Colors: {\n";
  for (auto C : Colors)
    dbgs() << "    " << C.first << " -> " << ColorKindToName(C.second) << "\n";
  dbgs() << "  }\n}\n";
}
#endif

namespace {
// Base class of for reordering networks. They don't strictly need to be
// permutations, as outputs with repeated occurrences of an input element
// are allowed.
struct PermNetwork {
  using Controls = std::vector<uint8_t>;
  using ElemType = int;
  static constexpr ElemType Ignore = ElemType(-1);

  enum : uint8_t {
    None,
    Pass,
    Switch
  };
  enum : uint8_t {
    Forward,
    Reverse
  };

  PermNetwork(ArrayRef<ElemType> Ord, unsigned Mult = 1) {
    Order.assign(Ord.data(), Ord.data()+Ord.size());
    Log = 0;

    unsigned S = Order.size();
    while (S >>= 1)
      ++Log;

    Table.resize(Order.size());
    for (RowType &Row : Table)
      Row.resize(Mult*Log, None);
  }

  void getControls(Controls &V, unsigned StartAt, uint8_t Dir) const {
    unsigned Size = Order.size();
    V.resize(Size);
    for (unsigned I = 0; I != Size; ++I) {
      unsigned W = 0;
      for (unsigned L = 0; L != Log; ++L) {
        unsigned C = ctl(I, StartAt+L) == Switch;
        if (Dir == Forward)
          W |= C << (Log-1-L);
        else
          W |= C << L;
      }
      assert(isUInt<8>(W));
      V[I] = uint8_t(W);
    }
  }

  uint8_t ctl(ElemType Pos, unsigned Step) const {
    return Table[Pos][Step];
  }
  unsigned size() const {
    return Order.size();
  }
  unsigned steps() const {
    return Log;
  }

protected:
  unsigned Log;
  std::vector<ElemType> Order;
  using RowType = std::vector<uint8_t>;
  std::vector<RowType> Table;
};

struct ForwardDeltaNetwork : public PermNetwork {
  ForwardDeltaNetwork(ArrayRef<ElemType> Ord) : PermNetwork(Ord) {}

  bool run(Controls &V) {
    if (!route(Order.data(), Table.data(), size(), 0))
      return false;
    getControls(V, 0, Forward);
    return true;
  }

private:
  bool route(ElemType *P, RowType *T, unsigned Size, unsigned Step);
};

struct ReverseDeltaNetwork : public PermNetwork {
  ReverseDeltaNetwork(ArrayRef<ElemType> Ord) : PermNetwork(Ord) {}

  bool run(Controls &V) {
    if (!route(Order.data(), Table.data(), size(), 0))
      return false;
    getControls(V, 0, Reverse);
    return true;
  }

private:
  bool route(ElemType *P, RowType *T, unsigned Size, unsigned Step);
};

struct BenesNetwork : public PermNetwork {
  BenesNetwork(ArrayRef<ElemType> Ord) : PermNetwork(Ord, 2) {}

  bool run(Controls &F, Controls &R) {
    if (!route(Order.data(), Table.data(), size(), 0))
      return false;

    getControls(F, 0, Forward);
    getControls(R, Log, Reverse);
    return true;
  }

private:
  bool route(ElemType *P, RowType *T, unsigned Size, unsigned Step);
};
} // namespace

bool ForwardDeltaNetwork::route(ElemType *P, RowType *T, unsigned Size,
                                unsigned Step) {
  bool UseUp = false, UseDown = false;
  ElemType Num = Size;

  // Cannot use coloring here, because coloring is used to determine
  // the "big" switch, i.e. the one that changes halves, and in a forward
  // network, a color can be simultaneously routed to both halves in the
  // step we're working on.
  for (ElemType J = 0; J != Num; ++J) {
    ElemType I = P[J];
    // I is the position in the input,
    // J is the position in the output.
    if (I == Ignore)
      continue;
    uint8_t S;
    if (I < Num/2)
      S = (J < Num/2) ? Pass : Switch;
    else
      S = (J < Num/2) ? Switch : Pass;

    // U is the element in the table that needs to be updated.
    ElemType U = (S == Pass) ? I : (I < Num/2 ? I+Num/2 : I-Num/2);
    if (U < Num/2)
      UseUp = true;
    else
      UseDown = true;
    if (T[U][Step] != S && T[U][Step] != None)
      return false;
    T[U][Step] = S;
  }

  for (ElemType J = 0; J != Num; ++J)
    if (P[J] != Ignore && P[J] >= Num/2)
      P[J] -= Num/2;

  if (Step+1 < Log) {
    if (UseUp   && !route(P,        T,        Size/2, Step+1))
      return false;
    if (UseDown && !route(P+Size/2, T+Size/2, Size/2, Step+1))
      return false;
  }
  return true;
}

bool ReverseDeltaNetwork::route(ElemType *P, RowType *T, unsigned Size,
                                unsigned Step) {
  unsigned Pets = Log-1 - Step;
  bool UseUp = false, UseDown = false;
  ElemType Num = Size;

  // In this step half-switching occurs, so coloring can be used.
  Coloring G({P,Size});
  const Coloring::MapType &M = G.colors();
  if (M.empty())
    return false;

  ColorKind ColorUp = ColorKind::None;
  for (ElemType J = 0; J != Num; ++J) {
    ElemType I = P[J];
    // I is the position in the input,
    // J is the position in the output.
    if (I == Ignore)
      continue;
    ColorKind C = M.at(I);
    if (C == ColorKind::None)
      continue;
    // During "Step", inputs cannot switch halves, so if the "up" color
    // is still unknown, make sure that it is selected in such a way that
    // "I" will stay in the same half.
    bool InpUp = I < Num/2;
    if (ColorUp == ColorKind::None)
      ColorUp = InpUp ? C : G.other(C);
    if ((C == ColorUp) != InpUp) {
      // If I should go to a different half than where is it now, give up.
      return false;
    }

    uint8_t S;
    if (InpUp) {
      S = (J < Num/2) ? Pass : Switch;
      UseUp = true;
    } else {
      S = (J < Num/2) ? Switch : Pass;
      UseDown = true;
    }
    T[J][Pets] = S;
  }

  // Reorder the working permutation according to the computed switch table
  // for the last step (i.e. Pets).
  for (ElemType J = 0, E = Size / 2; J != E; ++J) {
    ElemType PJ = P[J];         // Current values of P[J]
    ElemType PC = P[J+Size/2];  // and P[conj(J)]
    ElemType QJ = PJ;           // New values of P[J]
    ElemType QC = PC;           // and P[conj(J)]
    if (T[J][Pets] == Switch)
      QC = PJ;
    if (T[J+Size/2][Pets] == Switch)
      QJ = PC;
    P[J] = QJ;
    P[J+Size/2] = QC;
  }

  for (ElemType J = 0; J != Num; ++J)
    if (P[J] != Ignore && P[J] >= Num/2)
      P[J] -= Num/2;

  if (Step+1 < Log) {
    if (UseUp && !route(P, T, Size/2, Step+1))
      return false;
    if (UseDown && !route(P+Size/2, T+Size/2, Size/2, Step+1))
      return false;
  }
  return true;
}

bool BenesNetwork::route(ElemType *P, RowType *T, unsigned Size,
                         unsigned Step) {
  Coloring G({P,Size});
  const Coloring::MapType &M = G.colors();
  if (M.empty())
    return false;
  ElemType Num = Size;

  unsigned Pets = 2*Log-1 - Step;
  bool UseUp = false, UseDown = false;

  // Both assignments, i.e. Red->Up and Red->Down are valid, but they will
  // result in different controls. Let's pick the one where the first
  // control will be "Pass".
  ColorKind ColorUp = ColorKind::None;
  for (ElemType J = 0; J != Num; ++J) {
    ElemType I = P[J];
    if (I == Ignore)
      continue;
    ColorKind C = M.at(I);
    if (C == ColorKind::None)
      continue;
    if (ColorUp == ColorKind::None) {
      ColorUp = (I < Num / 2) ? ColorKind::Red : ColorKind::Black;
    }
    unsigned CI = (I < Num/2) ? I+Num/2 : I-Num/2;
    if (C == ColorUp) {
      if (I < Num/2)
        T[I][Step] = Pass;
      else
        T[CI][Step] = Switch;
      T[J][Pets] = (J < Num/2) ? Pass : Switch;
      UseUp = true;
    } else { // Down
      if (I < Num/2)
        T[CI][Step] = Switch;
      else
        T[I][Step] = Pass;
      T[J][Pets] = (J < Num/2) ? Switch : Pass;
      UseDown = true;
    }
  }

  // Reorder the working permutation according to the computed switch table
  // for the last step (i.e. Pets).
  for (ElemType J = 0; J != Num/2; ++J) {
    ElemType PJ = P[J];         // Current values of P[J]
    ElemType PC = P[J+Num/2];   // and P[conj(J)]
    ElemType QJ = PJ;           // New values of P[J]
    ElemType QC = PC;           // and P[conj(J)]
    if (T[J][Pets] == Switch)
      QC = PJ;
    if (T[J+Num/2][Pets] == Switch)
      QJ = PC;
    P[J] = QJ;
    P[J+Num/2] = QC;
  }

  for (ElemType J = 0; J != Num; ++J)
    if (P[J] != Ignore && P[J] >= Num/2)
      P[J] -= Num/2;

  if (Step+1 < Log) {
    if (UseUp && !route(P, T, Size/2, Step+1))
      return false;
    if (UseDown && !route(P+Size/2, T+Size/2, Size/2, Step+1))
      return false;
  }
  return true;
}

// --------------------------------------------------------------------
// Support for building selection results (output instructions that are
// parts of the final selection).

namespace {
struct OpRef {
  OpRef(SDValue V) : OpV(V) {}
  bool isValue() const { return OpV.getNode() != nullptr; }
  bool isValid() const { return isValue() || !(OpN & Invalid); }
  bool isUndef() const { return OpN & Undef; }
  static OpRef res(int N) { return OpRef(Whole | (N & Index)); }
  static OpRef fail() { return OpRef(Invalid); }

  static OpRef lo(const OpRef &R) {
    assert(!R.isValue());
    return OpRef(R.OpN & (Undef | Index | LoHalf));
  }
  static OpRef hi(const OpRef &R) {
    assert(!R.isValue());
    return OpRef(R.OpN & (Undef | Index | HiHalf));
  }
  static OpRef undef(MVT Ty) { return OpRef(Undef | Ty.SimpleTy); }

  // Direct value.
  SDValue OpV = SDValue();

  // Reference to the operand of the input node:
  // If the 31st bit is 1, it's undef, otherwise, bits 28..0 are the
  // operand index:
  // If bit 30 is set, it's the high half of the operand.
  // If bit 29 is set, it's the low half of the operand.
  unsigned OpN = 0;

  enum : unsigned {
    Invalid = 0x10000000,
    LoHalf  = 0x20000000,
    HiHalf  = 0x40000000,
    Whole   = LoHalf | HiHalf,
    Undef   = 0x80000000,
    Index   = 0x0FFFFFFF,  // Mask of the index value.
    IndexBits = 28,
  };

  LLVM_DUMP_METHOD
  void print(raw_ostream &OS, const SelectionDAG &G) const;

private:
  OpRef(unsigned N) : OpN(N) {}
};

struct NodeTemplate {
  NodeTemplate() = default;
  unsigned Opc = 0;
  MVT Ty = MVT::Other;
  std::vector<OpRef> Ops;

  LLVM_DUMP_METHOD void print(raw_ostream &OS, const SelectionDAG &G) const;
};

struct ResultStack {
  ResultStack(SDNode *Inp)
    : InpNode(Inp), InpTy(Inp->getValueType(0).getSimpleVT()) {}
  SDNode *InpNode;
  MVT InpTy;
  unsigned push(const NodeTemplate &Res) {
    List.push_back(Res);
    return List.size()-1;
  }
  unsigned push(unsigned Opc, MVT Ty, std::vector<OpRef> &&Ops) {
    NodeTemplate Res;
    Res.Opc = Opc;
    Res.Ty = Ty;
    Res.Ops = Ops;
    return push(Res);
  }
  bool empty() const { return List.empty(); }
  unsigned size() const { return List.size(); }
  unsigned top() const { return size()-1; }
  const NodeTemplate &operator[](unsigned I) const { return List[I]; }
  unsigned reset(unsigned NewTop) {
    List.resize(NewTop+1);
    return NewTop;
  }

  using BaseType = std::vector<NodeTemplate>;
  BaseType::iterator begin() { return List.begin(); }
  BaseType::iterator end()   { return List.end(); }
  BaseType::const_iterator begin() const { return List.begin(); }
  BaseType::const_iterator end() const   { return List.end(); }

  BaseType List;

  LLVM_DUMP_METHOD
  void print(raw_ostream &OS, const SelectionDAG &G) const;
};
} // namespace

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void OpRef::print(raw_ostream &OS, const SelectionDAG &G) const {
  if (isValue()) {
    OpV.getNode()->print(OS, &G);
    return;
  }
  if (OpN & Invalid) {
    OS << "invalid";
    return;
  }
  if (OpN & Undef) {
    OS << "undef";
    return;
  }
  if ((OpN & Whole) != Whole) {
    assert((OpN & Whole) == LoHalf || (OpN & Whole) == HiHalf);
    if (OpN & LoHalf)
      OS << "lo ";
    else
      OS << "hi ";
  }
  OS << '#' << SignExtend32(OpN & Index, IndexBits);
}

void NodeTemplate::print(raw_ostream &OS, const SelectionDAG &G) const {
  const TargetInstrInfo &TII = *G.getSubtarget().getInstrInfo();
  OS << format("%8s", EVT(Ty).getEVTString().c_str()) << "  "
     << TII.getName(Opc);
  bool Comma = false;
  for (const auto &R : Ops) {
    if (Comma)
      OS << ',';
    Comma = true;
    OS << ' ';
    R.print(OS, G);
  }
}

void ResultStack::print(raw_ostream &OS, const SelectionDAG &G) const {
  OS << "Input node:\n";
#ifndef NDEBUG
  InpNode->dumpr(&G);
#endif
  OS << "Result templates:\n";
  for (unsigned I = 0, E = List.size(); I != E; ++I) {
    OS << '[' << I << "] ";
    List[I].print(OS, G);
    OS << '\n';
  }
}
#endif

namespace {
struct ShuffleMask {
  ShuffleMask(ArrayRef<int> M) : Mask(M) {
    for (int M : Mask) {
      if (M == -1)
        continue;
      MinSrc = (MinSrc == -1) ? M : std::min(MinSrc, M);
      MaxSrc = (MaxSrc == -1) ? M : std::max(MaxSrc, M);
    }
  }

  ArrayRef<int> Mask;
  int MinSrc = -1, MaxSrc = -1;

  ShuffleMask lo() const {
    size_t H = Mask.size()/2;
    return ShuffleMask(Mask.take_front(H));
  }
  ShuffleMask hi() const {
    size_t H = Mask.size()/2;
    return ShuffleMask(Mask.take_back(H));
  }

  void print(raw_ostream &OS) const {
    OS << "MinSrc:" << MinSrc << ", MaxSrc:" << MaxSrc << " {";
    for (int M : Mask)
      OS << ' ' << M;
    OS << " }";
  }
};

LLVM_ATTRIBUTE_UNUSED
raw_ostream &operator<<(raw_ostream &OS, const ShuffleMask &SM) {
  SM.print(OS);
  return OS;
}
} // namespace

namespace shuffles {
using MaskT = SmallVector<int, 128>;
// Vdd = vshuffvdd(Vu, Vv, Rt)
// Vdd = vdealvdd(Vu, Vv, Rt)
// Vd  = vpack(Vu, Vv, Size, TakeOdd)
// Vd  = vshuff(Vu, Vv, Size, TakeOdd)
// Vd  = vdeal(Vu, Vv, Size, TakeOdd)
// Vd  = vdealb4w(Vu, Vv)

ArrayRef<int> lo(ArrayRef<int> Vuu) { return Vuu.take_front(Vuu.size() / 2); }
ArrayRef<int> hi(ArrayRef<int> Vuu) { return Vuu.take_back(Vuu.size() / 2); }

MaskT vshuffvdd(ArrayRef<int> Vu, ArrayRef<int> Vv, unsigned Rt) {
  int Len = Vu.size();
  MaskT Vdd(2 * Len);
  std::copy(Vv.begin(), Vv.end(), Vdd.begin());
  std::copy(Vu.begin(), Vu.end(), Vdd.begin() + Len);

  auto Vd0 = MutableArrayRef<int>(Vdd).take_front(Len);
  auto Vd1 = MutableArrayRef<int>(Vdd).take_back(Len);

  for (int Offset = 1; Offset < Len; Offset *= 2) {
    if ((Rt & Offset) == 0)
      continue;
    for (int i = 0; i != Len; ++i) {
      if ((i & Offset) == 0)
        std::swap(Vd1[i], Vd0[i + Offset]);
    }
  }
  return Vdd;
}

MaskT vdealvdd(ArrayRef<int> Vu, ArrayRef<int> Vv, unsigned Rt) {
  int Len = Vu.size();
  MaskT Vdd(2 * Len);
  std::copy(Vv.begin(), Vv.end(), Vdd.begin());
  std::copy(Vu.begin(), Vu.end(), Vdd.begin() + Len);

  auto Vd0 = MutableArrayRef<int>(Vdd).take_front(Len);
  auto Vd1 = MutableArrayRef<int>(Vdd).take_back(Len);

  for (int Offset = Len / 2; Offset > 0; Offset /= 2) {
    if ((Rt & Offset) == 0)
      continue;
    for (int i = 0; i != Len; ++i) {
      if ((i & Offset) == 0)
        std::swap(Vd1[i], Vd0[i + Offset]);
    }
  }
  return Vdd;
}

MaskT vpack(ArrayRef<int> Vu, ArrayRef<int> Vv, unsigned Size, bool TakeOdd) {
  int Len = Vu.size();
  MaskT Vd(Len);
  auto Odd = static_cast<int>(TakeOdd);
  for (int i = 0, e = Len / (2 * Size); i != e; ++i) {
    for (int b = 0; b != static_cast<int>(Size); ++b) {
      // clang-format off
      Vd[i * Size + b]           = Vv[(2 * i + Odd) * Size + b];
      Vd[i * Size + b + Len / 2] = Vu[(2 * i + Odd) * Size + b];
      // clang-format on
    }
  }
  return Vd;
}

MaskT vshuff(ArrayRef<int> Vu, ArrayRef<int> Vv, unsigned Size, bool TakeOdd) {
  int Len = Vu.size();
  MaskT Vd(Len);
  auto Odd = static_cast<int>(TakeOdd);
  for (int i = 0, e = Len / (2 * Size); i != e; ++i) {
    for (int b = 0; b != static_cast<int>(Size); ++b) {
      Vd[(2 * i + 0) * Size + b] = Vv[(2 * i + Odd) * Size + b];
      Vd[(2 * i + 1) * Size + b] = Vu[(2 * i + Odd) * Size + b];
    }
  }
  return Vd;
}

MaskT vdeal(ArrayRef<int> Vu, ArrayRef<int> Vv, unsigned Size, bool TakeOdd) {
  int Len = Vu.size();
  MaskT T = vdealvdd(Vu, Vv, Len - 2 * Size);
  return vpack(hi(T), lo(T), Size, TakeOdd);
}

MaskT vdealb4w(ArrayRef<int> Vu, ArrayRef<int> Vv) {
  int Len = Vu.size();
  MaskT Vd(Len);
  for (int i = 0, e = Len / 4; i != e; ++i) {
    Vd[0 * (Len / 4) + i] = Vv[4 * i + 0];
    Vd[1 * (Len / 4) + i] = Vv[4 * i + 2];
    Vd[2 * (Len / 4) + i] = Vu[4 * i + 0];
    Vd[3 * (Len / 4) + i] = Vu[4 * i + 2];
  }
  return Vd;
}

template <typename ShuffFunc, typename... OptArgs>
auto mask(ShuffFunc S, unsigned Length, OptArgs... args) -> MaskT {
  MaskT Vu(Length), Vv(Length);
  std::iota(Vu.begin(), Vu.end(), Length); // High
  std::iota(Vv.begin(), Vv.end(), 0);      // Low
  return S(Vu, Vv, args...);
}

} // namespace shuffles

// --------------------------------------------------------------------
// The HvxSelector class.

static const HexagonTargetLowering &getHexagonLowering(SelectionDAG &G) {
  return static_cast<const HexagonTargetLowering&>(G.getTargetLoweringInfo());
}
static const HexagonSubtarget &getHexagonSubtarget(SelectionDAG &G) {
  return G.getSubtarget<HexagonSubtarget>();
}

namespace llvm {
  struct HvxSelector {
    const HexagonTargetLowering &Lower;
    HexagonDAGToDAGISel &ISel;
    SelectionDAG &DAG;
    const HexagonSubtarget &HST;
    const unsigned HwLen;

    HvxSelector(HexagonDAGToDAGISel &HS, SelectionDAG &G)
        : Lower(getHexagonLowering(G)), ISel(HS), DAG(G),
          HST(getHexagonSubtarget(G)), HwLen(HST.getVectorLength()) {}

    MVT getSingleVT(MVT ElemTy) const {
      assert(ElemTy != MVT::i1 && "Use getBoolVT for predicates");
      unsigned NumElems = HwLen / (ElemTy.getSizeInBits() / 8);
      return MVT::getVectorVT(ElemTy, NumElems);
    }

    MVT getPairVT(MVT ElemTy) const {
      assert(ElemTy != MVT::i1); // Suspicious: there are no predicate pairs.
      unsigned NumElems = (2 * HwLen) / (ElemTy.getSizeInBits() / 8);
      return MVT::getVectorVT(ElemTy, NumElems);
    }

    MVT getBoolVT() const {
      // Return HwLen x i1.
      return MVT::getVectorVT(MVT::i1, HwLen);
    }

    void selectExtractSubvector(SDNode *N);
    void selectShuffle(SDNode *N);
    void selectRor(SDNode *N);
    void selectVAlign(SDNode *N);

    static SmallVector<uint32_t, 8> getPerfectCompletions(ShuffleMask SM,
                                                          unsigned Width);
    static SmallVector<uint32_t, 8> completeToPerfect(
        ArrayRef<uint32_t> Completions, unsigned Width);
    static std::optional<int> rotationDistance(ShuffleMask SM, unsigned WrapAt);

  private:
    void select(SDNode *ISelN);
    void materialize(const ResultStack &Results);

    SDValue getConst32(int Val, const SDLoc &dl);
    SDValue getVectorConstant(ArrayRef<uint8_t> Data, const SDLoc &dl);

    enum : unsigned {
      None,
      PackMux,
    };
    OpRef concats(OpRef Va, OpRef Vb, ResultStack &Results);
    OpRef funnels(OpRef Va, OpRef Vb, int Amount, ResultStack &Results);

    OpRef packs(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results,
                MutableArrayRef<int> NewMask, unsigned Options = None);
    OpRef packp(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results,
                MutableArrayRef<int> NewMask);
    OpRef vmuxs(ArrayRef<uint8_t> Bytes, OpRef Va, OpRef Vb,
                ResultStack &Results);
    OpRef vmuxp(ArrayRef<uint8_t> Bytes, OpRef Va, OpRef Vb,
                ResultStack &Results);

    OpRef shuffs1(ShuffleMask SM, OpRef Va, ResultStack &Results);
    OpRef shuffs2(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results);
    OpRef shuffp1(ShuffleMask SM, OpRef Va, ResultStack &Results);
    OpRef shuffp2(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results);

    OpRef butterfly(ShuffleMask SM, OpRef Va, ResultStack &Results);
    OpRef contracting(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results);
    OpRef expanding(ShuffleMask SM, OpRef Va, ResultStack &Results);
    OpRef perfect(ShuffleMask SM, OpRef Va, ResultStack &Results);

    bool selectVectorConstants(SDNode *N);
    bool scalarizeShuffle(ArrayRef<int> Mask, const SDLoc &dl, MVT ResTy,
                          SDValue Va, SDValue Vb, SDNode *N);
  };
} // namespace llvm

static void splitMask(ArrayRef<int> Mask, MutableArrayRef<int> MaskL,
                      MutableArrayRef<int> MaskR) {
  unsigned VecLen = Mask.size();
  assert(MaskL.size() == VecLen && MaskR.size() == VecLen);
  for (unsigned I = 0; I != VecLen; ++I) {
    int M = Mask[I];
    if (M < 0) {
      MaskL[I] = MaskR[I] = -1;
    } else if (unsigned(M) < VecLen) {
      MaskL[I] = M;
      MaskR[I] = -1;
    } else {
      MaskL[I] = -1;
      MaskR[I] = M-VecLen;
    }
  }
}

static std::pair<int,unsigned> findStrip(ArrayRef<int> A, int Inc,
                                         unsigned MaxLen) {
  assert(A.size() > 0 && A.size() >= MaxLen);
  int F = A[0];
  int E = F;
  for (unsigned I = 1; I != MaxLen; ++I) {
    if (A[I] - E != Inc)
      return { F, I };
    E = A[I];
  }
  return { F, MaxLen };
}

static bool isUndef(ArrayRef<int> Mask) {
  for (int Idx : Mask)
    if (Idx != -1)
      return false;
  return true;
}

static bool isIdentity(ArrayRef<int> Mask) {
  for (int I = 0, E = Mask.size(); I != E; ++I) {
    int M = Mask[I];
    if (M >= 0 && M != I)
      return false;
  }
  return true;
}

static bool isLowHalfOnly(ArrayRef<int> Mask) {
  int L = Mask.size();
  assert(L % 2 == 0);
  // Check if the second half of the mask is all-undef.
  return llvm::all_of(Mask.drop_front(L / 2), [](int M) { return M < 0; });
}

static SmallVector<unsigned, 4> getInputSegmentList(ShuffleMask SM,
                                                    unsigned SegLen) {
  assert(isPowerOf2_32(SegLen));
  SmallVector<unsigned, 4> SegList;
  if (SM.MaxSrc == -1)
    return SegList;

  unsigned Shift = Log2_32(SegLen);
  BitVector Segs(alignTo(SM.MaxSrc + 1, SegLen) >> Shift);

  for (int M : SM.Mask) {
    if (M >= 0)
      Segs.set(M >> Shift);
  }

  for (unsigned B : Segs.set_bits())
    SegList.push_back(B);
  return SegList;
}

static SmallVector<unsigned, 4> getOutputSegmentMap(ShuffleMask SM,
                                                    unsigned SegLen) {
  // Calculate the layout of the output segments in terms of the input
  // segments.
  // For example [1,3,1,0] means that the output consists of 4 output
  // segments, where the first output segment has only elements of the
  // input segment at index 1. The next output segment only has elements
  // of the input segment 3, etc.
  // If an output segment only has undef elements, the value will be ~0u.
  // If an output segment has elements from more than one input segment,
  // the corresponding value will be ~1u.
  unsigned MaskLen = SM.Mask.size();
  assert(MaskLen % SegLen == 0);
  SmallVector<unsigned, 4> Map(MaskLen / SegLen);

  for (int S = 0, E = Map.size(); S != E; ++S) {
    unsigned Idx = ~0u;
    for (int I = 0; I != static_cast<int>(SegLen); ++I) {
      int M = SM.Mask[S*SegLen + I];
      if (M < 0)
        continue;
      unsigned G = M / SegLen; // Input segment of this element.
      if (Idx == ~0u) {
        Idx = G;
      } else if (Idx != G) {
        Idx = ~1u;
        break;
      }
    }
    Map[S] = Idx;
  }

  return Map;
}

static void packSegmentMask(ArrayRef<int> Mask, ArrayRef<unsigned> OutSegMap,
                            unsigned SegLen, MutableArrayRef<int> PackedMask) {
  SmallVector<unsigned, 4> InvMap;
  for (int I = OutSegMap.size() - 1; I >= 0; --I) {
    unsigned S = OutSegMap[I];
    assert(S != ~0u && "Unexpected undef");
    assert(S != ~1u && "Unexpected multi");
    if (InvMap.size() <= S)
      InvMap.resize(S+1);
    InvMap[S] = I;
  }

  unsigned Shift = Log2_32(SegLen);
  for (int I = 0, E = Mask.size(); I != E; ++I) {
    int M = Mask[I];
    if (M >= 0) {
      int OutIdx = InvMap[M >> Shift];
      M = (M & (SegLen-1)) + SegLen*OutIdx;
    }
    PackedMask[I] = M;
  }
}

bool HvxSelector::selectVectorConstants(SDNode *N) {
  // Constant vectors are generated as loads from constant pools or as
  // splats of a constant value. Since they are generated during the
  // selection process, the main selection algorithm is not aware of them.
  // Select them directly here.
  SmallVector<SDNode*,4> Nodes;
  SetVector<SDNode*> WorkQ;

  // The DAG can change (due to CSE) during selection, so cache all the
  // unselected nodes first to avoid traversing a mutating DAG.
  WorkQ.insert(N);
  for (unsigned i = 0; i != WorkQ.size(); ++i) {
    SDNode *W = WorkQ[i];
    if (!W->isMachineOpcode() && W->getOpcode() == HexagonISD::ISEL)
      Nodes.push_back(W);
    for (unsigned j = 0, f = W->getNumOperands(); j != f; ++j)
      WorkQ.insert(W->getOperand(j).getNode());
  }

  for (SDNode *L : Nodes)
    select(L);

  return !Nodes.empty();
}

void HvxSelector::materialize(const ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {
    dbgs() << "Materializing\n";
    Results.print(dbgs(), DAG);
  });
  if (Results.empty())
    return;
  const SDLoc &dl(Results.InpNode);
  std::vector<SDValue> Output;

  for (unsigned I = 0, E = Results.size(); I != E; ++I) {
    const NodeTemplate &Node = Results[I];
    std::vector<SDValue> Ops;
    for (const OpRef &R : Node.Ops) {
      assert(R.isValid());
      if (R.isValue()) {
        Ops.push_back(R.OpV);
        continue;
      }
      if (R.OpN & OpRef::Undef) {
        MVT::SimpleValueType SVT = MVT::SimpleValueType(R.OpN & OpRef::Index);
        Ops.push_back(ISel.selectUndef(dl, MVT(SVT)));
        continue;
      }
      // R is an index of a result.
      unsigned Part = R.OpN & OpRef::Whole;
      int Idx = SignExtend32(R.OpN & OpRef::Index, OpRef::IndexBits);
      if (Idx < 0)
        Idx += I;
      assert(Idx >= 0 && unsigned(Idx) < Output.size());
      SDValue Op = Output[Idx];
      MVT OpTy = Op.getValueType().getSimpleVT();
      if (Part != OpRef::Whole) {
        assert(Part == OpRef::LoHalf || Part == OpRef::HiHalf);
        MVT HalfTy = MVT::getVectorVT(OpTy.getVectorElementType(),
                                      OpTy.getVectorNumElements()/2);
        unsigned Sub = (Part == OpRef::LoHalf) ? Hexagon::vsub_lo
                                               : Hexagon::vsub_hi;
        Op = DAG.getTargetExtractSubreg(Sub, dl, HalfTy, Op);
      }
      Ops.push_back(Op);
    } // for (Node : Results)

    assert(Node.Ty != MVT::Other);
    SDNode *ResN = (Node.Opc == TargetOpcode::COPY)
                      ? Ops.front().getNode()
                      : DAG.getMachineNode(Node.Opc, dl, Node.Ty, Ops);
    Output.push_back(SDValue(ResN, 0));
  }

  SDNode *OutN = Output.back().getNode();
  SDNode *InpN = Results.InpNode;
  DEBUG_WITH_TYPE("isel", {
    dbgs() << "Generated node:\n";
    OutN->dumpr(&DAG);
  });

  ISel.ReplaceNode(InpN, OutN);
  selectVectorConstants(OutN);
  DAG.RemoveDeadNodes();
}

OpRef HvxSelector::concats(OpRef Lo, OpRef Hi, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  const SDLoc &dl(Results.InpNode);
  Results.push(TargetOpcode::REG_SEQUENCE, getPairVT(MVT::i8), {
    getConst32(Hexagon::HvxWRRegClassID, dl),
    Lo, getConst32(Hexagon::vsub_lo, dl),
    Hi, getConst32(Hexagon::vsub_hi, dl),
  });
  return OpRef::res(Results.top());
}

OpRef HvxSelector::funnels(OpRef Va, OpRef Vb, int Amount,
                           ResultStack &Results) {
  // Do a funnel shift towards the low end (shift right) by Amount bytes.
  // If Amount < 0, treat it as shift left, i.e. do a shift right by
  // Amount + HwLen.
  auto VecLen = static_cast<int>(HwLen);

  if (Amount == 0)
    return Va;
  if (Amount == VecLen)
    return Vb;

  MVT Ty = getSingleVT(MVT::i8);
  const SDLoc &dl(Results.InpNode);

  if (Amount < 0)
    Amount += VecLen;
  if (Amount > VecLen) {
    Amount -= VecLen;
    std::swap(Va, Vb);
  }

  if (isUInt<3>(Amount)) {
    SDValue A = getConst32(Amount, dl);
    Results.push(Hexagon::V6_valignbi, Ty, {Vb, Va, A});
  } else if (isUInt<3>(VecLen - Amount)) {
    SDValue A = getConst32(VecLen - Amount, dl);
    Results.push(Hexagon::V6_vlalignbi, Ty, {Vb, Va, A});
  } else {
    SDValue A = getConst32(Amount, dl);
    Results.push(Hexagon::A2_tfrsi, Ty, {A});
    Results.push(Hexagon::V6_valignb, Ty, {Vb, Va, OpRef::res(-1)});
  }
  return OpRef::res(Results.top());
}

// Va, Vb are single vectors. If SM only uses two vector halves from Va/Vb,
// pack these halves into a single vector, and remap SM into NewMask to use
// the new vector instead.
OpRef HvxSelector::packs(ShuffleMask SM, OpRef Va, OpRef Vb,
                         ResultStack &Results, MutableArrayRef<int> NewMask,
                         unsigned Options) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  if (!Va.isValid() || !Vb.isValid())
    return OpRef::fail();

  if (Vb.isUndef()) {
    std::copy(SM.Mask.begin(), SM.Mask.end(), NewMask.begin());
    return Va;
  }
  if (Va.isUndef()) {
    std::copy(SM.Mask.begin(), SM.Mask.end(), NewMask.begin());
    ShuffleVectorSDNode::commuteMask(NewMask);
    return Vb;
  }

  MVT Ty = getSingleVT(MVT::i8);
  MVT PairTy = getPairVT(MVT::i8);
  OpRef Inp[2] = {Va, Vb};
  unsigned VecLen = SM.Mask.size();

  auto valign = [this](OpRef Lo, OpRef Hi, unsigned Amt, MVT Ty,
                       ResultStack &Results) {
    if (Amt == 0)
      return Lo;
    const SDLoc &dl(Results.InpNode);
    if (isUInt<3>(Amt) || isUInt<3>(HwLen - Amt)) {
      bool IsRight = isUInt<3>(Amt); // Right align.
      SDValue S = getConst32(IsRight ? Amt : HwLen - Amt, dl);
      unsigned Opc = IsRight ? Hexagon::V6_valignbi : Hexagon::V6_vlalignbi;
      Results.push(Opc, Ty, {Hi, Lo, S});
      return OpRef::res(Results.top());
    }
    Results.push(Hexagon::A2_tfrsi, MVT::i32, {getConst32(Amt, dl)});
    OpRef A = OpRef::res(Results.top());
    Results.push(Hexagon::V6_valignb, Ty, {Hi, Lo, A});
    return OpRef::res(Results.top());
  };

  // Segment is a vector half.
  unsigned SegLen = HwLen / 2;

  // Check if we can shuffle vector halves around to get the used elements
  // into a single vector.
  shuffles::MaskT MaskH(SM.Mask);
  SmallVector<unsigned, 4> SegList = getInputSegmentList(SM.Mask, SegLen);
  unsigned SegCount = SegList.size();
  SmallVector<unsigned, 4> SegMap = getOutputSegmentMap(SM.Mask, SegLen);

  if (SegList.empty())
    return OpRef::undef(Ty);

  // NOTE:
  // In the following part of the function, where the segments are rearranged,
  // the shuffle mask SM can be of any length that is a multiple of a vector
  // (i.e. a multiple of 2*SegLen), and non-zero.
  // The output segment map is computed, and it may have any even number of
  // entries, but the rearrangement of input segments will be done based only
  // on the first two (non-undef) entries in the segment map.
  // For example, if the output map is 3, 1, 1, 3 (it can have at most two
  // distinct entries!), the segments 1 and 3 of Va/Vb will be packaged into
  // a single vector V = 3:1. The output mask will then be updated to use
  // seg(0,V), seg(1,V), seg(1,V), seg(0,V).
  //
  // Picking the segments based on the output map is an optimization. For
  // correctness it is only necessary that Seg0 and Seg1 are the two input
  // segments that are used in the output.

  unsigned Seg0 = ~0u, Seg1 = ~0u;
  for (unsigned X : SegMap) {
    if (X == ~0u)
      continue;
    if (Seg0 == ~0u)
      Seg0 = X;
    else if (Seg1 != ~0u)
      break;
    if (X == ~1u || X != Seg0)
      Seg1 = X;
  }

  if (SegCount == 1) {
    unsigned SrcOp = SegList[0] / 2;
    for (int I = 0; I != static_cast<int>(VecLen); ++I) {
      int M = SM.Mask[I];
      if (M >= 0) {
        M -= SrcOp * HwLen;
        assert(M >= 0);
      }
      NewMask[I] = M;
    }
    return Inp[SrcOp];
  }

  if (SegCount == 2) {
    // Seg0 should not be undef here: this would imply a SegList
    // with <= 1 elements, which was checked earlier.
    assert(Seg0 != ~0u);

    // If Seg0 or Seg1 are "multi-defined", pick them from the input
    // segment list instead.
    if (Seg0 == ~1u || Seg1 == ~1u) {
      if (Seg0 == Seg1) {
        Seg0 = SegList[0];
        Seg1 = SegList[1];
      } else if (Seg0 == ~1u) {
        Seg0 = SegList[0] != Seg1 ? SegList[0] : SegList[1];
      } else {
        assert(Seg1 == ~1u);
        Seg1 = SegList[0] != Seg0 ? SegList[0] : SegList[1];
      }
    }
    assert(Seg0 != ~1u && Seg1 != ~1u);

    assert(Seg0 != Seg1 && "Expecting different segments");
    const SDLoc &dl(Results.InpNode);
    Results.push(Hexagon::A2_tfrsi, MVT::i32, {getConst32(SegLen, dl)});
    OpRef HL = OpRef::res(Results.top());

    // Va = AB, Vb = CD

    if (Seg0 / 2 == Seg1 / 2) {
      // Same input vector.
      Va = Inp[Seg0 / 2];
      if (Seg0 > Seg1) {
        // Swap halves.
        Results.push(Hexagon::V6_vror, Ty, {Inp[Seg0 / 2], HL});
        Va = OpRef::res(Results.top());
      }
      packSegmentMask(SM.Mask, {Seg0, Seg1}, SegLen, MaskH);
    } else if (Seg0 % 2 == Seg1 % 2) {
      // Picking AC, BD, CA, or DB.
      // vshuff(CD,AB,HL) -> BD:AC
      // vshuff(AB,CD,HL) -> DB:CA
      auto Vs = (Seg0 == 0 || Seg0 == 1) ? std::make_pair(Vb, Va)  // AC or BD
                                         : std::make_pair(Va, Vb); // CA or DB
      Results.push(Hexagon::V6_vshuffvdd, PairTy, {Vs.first, Vs.second, HL});
      OpRef P = OpRef::res(Results.top());
      Va = (Seg0 == 0 || Seg0 == 2) ? OpRef::lo(P) : OpRef::hi(P);
      packSegmentMask(SM.Mask, {Seg0, Seg1}, SegLen, MaskH);
    } else {
      // Picking AD, BC, CB, or DA.
      if ((Seg0 == 0 && Seg1 == 3) || (Seg0 == 2 && Seg1 == 1)) {
        // AD or BC: this can be done using vmux.
        // Q = V6_pred_scalar2 SegLen
        // V = V6_vmux Q, (Va, Vb) or (Vb, Va)
        Results.push(Hexagon::V6_pred_scalar2, getBoolVT(), {HL});
        OpRef Qt = OpRef::res(Results.top());
        auto Vs = (Seg0 == 0) ? std::make_pair(Va, Vb)  // AD
                              : std::make_pair(Vb, Va); // CB
        Results.push(Hexagon::V6_vmux, Ty, {Qt, Vs.first, Vs.second});
        Va = OpRef::res(Results.top());
        packSegmentMask(SM.Mask, {Seg0, Seg1}, SegLen, MaskH);
      } else {
        // BC or DA: this could be done via valign by SegLen.
        // Do nothing here, because valign (if possible) will be generated
        // later on (make sure the Seg0 values are as expected).
        assert(Seg0 == 1 || Seg0 == 3);
      }
    }
  }

  // Check if the arguments can be packed by valign(Va,Vb) or valign(Vb,Va).
  // FIXME: maybe remove this?
  ShuffleMask SMH(MaskH);
  assert(SMH.Mask.size() == VecLen);
  shuffles::MaskT MaskA(SMH.Mask);

  if (SMH.MaxSrc - SMH.MinSrc >= static_cast<int>(HwLen)) {
    // valign(Lo=Va,Hi=Vb) won't work. Try swapping Va/Vb.
    shuffles::MaskT Swapped(SMH.Mask);
    ShuffleVectorSDNode::commuteMask(Swapped);
    ShuffleMask SW(Swapped);
    if (SW.MaxSrc - SW.MinSrc < static_cast<int>(HwLen)) {
      MaskA.assign(SW.Mask.begin(), SW.Mask.end());
      std::swap(Va, Vb);
    }
  }
  ShuffleMask SMA(MaskA);
  assert(SMA.Mask.size() == VecLen);

  if (SMA.MaxSrc - SMA.MinSrc < static_cast<int>(HwLen)) {
    int ShiftR = SMA.MinSrc;
    if (ShiftR >= static_cast<int>(HwLen)) {
      Va = Vb;
      Vb = OpRef::undef(Ty);
      ShiftR -= HwLen;
    }
    OpRef RetVal = valign(Va, Vb, ShiftR, Ty, Results);

    for (int I = 0; I != static_cast<int>(VecLen); ++I) {
      int M = SMA.Mask[I];
      if (M != -1)
        M -= SMA.MinSrc;
      NewMask[I] = M;
    }
    return RetVal;
  }

  // By here, packing by segment (half-vector) shuffling, and vector alignment
  // failed. Try vmux.
  // Note: since this is using the original mask, Va and Vb must not have been
  // modified.

  if (Options & PackMux) {
    // If elements picked from Va and Vb have all different (source) indexes
    // (relative to the start of the argument), do a mux, and update the mask.
    BitVector Picked(HwLen);
    SmallVector<uint8_t,128> MuxBytes(HwLen);
    bool CanMux = true;
    for (int I = 0; I != static_cast<int>(VecLen); ++I) {
      int M = SM.Mask[I];
      if (M == -1)
        continue;
      if (M >= static_cast<int>(HwLen))
        M -= HwLen;
      else
        MuxBytes[M] = 0xFF;
      if (Picked[M]) {
        CanMux = false;
        break;
      }
      NewMask[I] = M;
    }
    if (CanMux)
      return vmuxs(MuxBytes, Va, Vb, Results);
  }
  return OpRef::fail();
}

// Va, Vb are vector pairs. If SM only uses two single vectors from Va/Vb,
// pack these vectors into a pair, and remap SM into NewMask to use the
// new pair instead.
OpRef HvxSelector::packp(ShuffleMask SM, OpRef Va, OpRef Vb,
                         ResultStack &Results, MutableArrayRef<int> NewMask) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  SmallVector<unsigned, 4> SegList = getInputSegmentList(SM.Mask, HwLen);
  if (SegList.empty())
    return OpRef::undef(getPairVT(MVT::i8));

  // If more than two halves are used, bail.
  // TODO: be more aggressive here?
  unsigned SegCount = SegList.size();
  if (SegCount > 2)
    return OpRef::fail();

  MVT HalfTy = getSingleVT(MVT::i8);

  OpRef Inp[2] = { Va, Vb };
  OpRef Out[2] = { OpRef::undef(HalfTy), OpRef::undef(HalfTy) };

  // Really make sure we have at most 2 vectors used in the mask.
  assert(SegCount <= 2);

  for (int I = 0, E = SegList.size(); I != E; ++I) {
    unsigned S = SegList[I];
    OpRef Op = Inp[S / 2];
    Out[I] = (S & 1) ? OpRef::hi(Op) : OpRef::lo(Op);
  }

  // NOTE: Using SegList as the packing map here (not SegMap). This works,
  // because we're not concerned here about the order of the segments (i.e.
  // single vectors) in the output pair. Changing the order of vectors is
  // free (as opposed to changing the order of vector halves as in packs),
  // and so there is no extra cost added in case the order needs to be
  // changed later.
  packSegmentMask(SM.Mask, SegList, HwLen, NewMask);
  return concats(Out[0], Out[1], Results);
}

OpRef HvxSelector::vmuxs(ArrayRef<uint8_t> Bytes, OpRef Va, OpRef Vb,
                         ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  MVT ByteTy = getSingleVT(MVT::i8);
  MVT BoolTy = MVT::getVectorVT(MVT::i1, HwLen);
  const SDLoc &dl(Results.InpNode);
  SDValue B = getVectorConstant(Bytes, dl);
  Results.push(Hexagon::V6_vd0, ByteTy, {});
  Results.push(Hexagon::V6_veqb, BoolTy, {OpRef(B), OpRef::res(-1)});
  Results.push(Hexagon::V6_vmux, ByteTy, {OpRef::res(-1), Vb, Va});
  return OpRef::res(Results.top());
}

OpRef HvxSelector::vmuxp(ArrayRef<uint8_t> Bytes, OpRef Va, OpRef Vb,
                         ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  size_t S = Bytes.size() / 2;
  OpRef L = vmuxs(Bytes.take_front(S), OpRef::lo(Va), OpRef::lo(Vb), Results);
  OpRef H = vmuxs(Bytes.drop_front(S), OpRef::hi(Va), OpRef::hi(Vb), Results);
  return concats(L, H, Results);
}

OpRef HvxSelector::shuffs1(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  unsigned VecLen = SM.Mask.size();
  assert(HwLen == VecLen);
  (void)VecLen;
  assert(all_of(SM.Mask, [this](int M) { return M == -1 || M < int(HwLen); }));

  if (isIdentity(SM.Mask))
    return Va;
  if (isUndef(SM.Mask))
    return OpRef::undef(getSingleVT(MVT::i8));

  // First, check for rotations.
  if (auto Dist = rotationDistance(SM, VecLen)) {
    OpRef Rotate = funnels(Va, Va, *Dist, Results);
    if (Rotate.isValid())
      return Rotate;
  }
  unsigned HalfLen = HwLen / 2;
  assert(isPowerOf2_32(HalfLen));

  // Handle special case where the output is the same half of the input
  // repeated twice, i.e. if Va = AB, then handle the output of AA or BB.
  std::pair<int, unsigned> Strip1 = findStrip(SM.Mask, 1, HalfLen);
  if ((Strip1.first & ~HalfLen) == 0 && Strip1.second == HalfLen) {
    std::pair<int, unsigned> Strip2 =
        findStrip(SM.Mask.drop_front(HalfLen), 1, HalfLen);
    if (Strip1 == Strip2) {
      const SDLoc &dl(Results.InpNode);
      Results.push(Hexagon::A2_tfrsi, MVT::i32, {getConst32(HalfLen, dl)});
      Results.push(Hexagon::V6_vshuffvdd, getPairVT(MVT::i8),
                   {Va, Va, OpRef::res(Results.top())});
      OpRef S = OpRef::res(Results.top());
      return (Strip1.first == 0) ? OpRef::lo(S) : OpRef::hi(S);
    }
  }

  OpRef P = perfect(SM, Va, Results);
  if (P.isValid())
    return P;
  return butterfly(SM, Va, Results);
}

OpRef HvxSelector::shuffs2(ShuffleMask SM, OpRef Va, OpRef Vb,
                           ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  if (isUndef(SM.Mask))
    return OpRef::undef(getSingleVT(MVT::i8));

  OpRef C = contracting(SM, Va, Vb, Results);
  if (C.isValid())
    return C;

  int VecLen = SM.Mask.size();
  shuffles::MaskT PackedMask(VecLen);
  OpRef P = packs(SM, Va, Vb, Results, PackedMask);
  if (P.isValid())
    return shuffs1(ShuffleMask(PackedMask), P, Results);

  // TODO: Before we split the mask, try perfect shuffle on concatenated
  // operands.

  shuffles::MaskT MaskL(VecLen), MaskR(VecLen);
  splitMask(SM.Mask, MaskL, MaskR);

  OpRef L = shuffs1(ShuffleMask(MaskL), Va, Results);
  OpRef R = shuffs1(ShuffleMask(MaskR), Vb, Results);
  if (!L.isValid() || !R.isValid())
    return OpRef::fail();

  SmallVector<uint8_t, 128> Bytes(VecLen);
  for (int I = 0; I != VecLen; ++I) {
    if (MaskL[I] != -1)
      Bytes[I] = 0xFF;
  }
  return vmuxs(Bytes, L, R, Results);
}

OpRef HvxSelector::shuffp1(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  int VecLen = SM.Mask.size();

  if (isIdentity(SM.Mask))
    return Va;
  if (isUndef(SM.Mask))
    return OpRef::undef(getPairVT(MVT::i8));

  shuffles::MaskT PackedMask(VecLen);
  OpRef P = packs(SM, OpRef::lo(Va), OpRef::hi(Va), Results, PackedMask);
  if (P.isValid()) {
    ShuffleMask PM(PackedMask);
    OpRef E = expanding(PM, P, Results);
    if (E.isValid())
      return E;

    OpRef L = shuffs1(PM.lo(), P, Results);
    OpRef H = shuffs1(PM.hi(), P, Results);
    if (L.isValid() && H.isValid())
      return concats(L, H, Results);
  }

  if (!isLowHalfOnly(SM.Mask)) {
    // Doing a perfect shuffle on a low-half mask (i.e. where the upper half
    // is all-undef) may produce a perfect shuffle that generates legitimate
    // upper half. This isn't wrong, but if the perfect shuffle was possible,
    // then there is a good chance that a shorter (contracting) code may be
    // used as well (e.g. V6_vshuffeb, etc).
    OpRef R = perfect(SM, Va, Results);
    if (R.isValid())
      return R;
    // TODO commute the mask and try the opposite order of the halves.
  }

  OpRef L = shuffs2(SM.lo(), OpRef::lo(Va), OpRef::hi(Va), Results);
  OpRef H = shuffs2(SM.hi(), OpRef::lo(Va), OpRef::hi(Va), Results);
  if (L.isValid() && H.isValid())
    return concats(L, H, Results);

  return OpRef::fail();
}

OpRef HvxSelector::shuffp2(ShuffleMask SM, OpRef Va, OpRef Vb,
                           ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  if (isUndef(SM.Mask))
    return OpRef::undef(getPairVT(MVT::i8));

  int VecLen = SM.Mask.size();
  SmallVector<int,256> PackedMask(VecLen);
  OpRef P = packp(SM, Va, Vb, Results, PackedMask);
  if (P.isValid())
    return shuffp1(ShuffleMask(PackedMask), P, Results);

  SmallVector<int,256> MaskL(VecLen), MaskR(VecLen);
  splitMask(SM.Mask, MaskL, MaskR);

  OpRef L = shuffp1(ShuffleMask(MaskL), Va, Results);
  OpRef R = shuffp1(ShuffleMask(MaskR), Vb, Results);
  if (!L.isValid() || !R.isValid())
    return OpRef::fail();

  // Mux the results.
  SmallVector<uint8_t,256> Bytes(VecLen);
  for (int I = 0; I != VecLen; ++I) {
    if (MaskL[I] != -1)
      Bytes[I] = 0xFF;
  }
  return vmuxp(Bytes, L, R, Results);
}

namespace {
  struct Deleter : public SelectionDAG::DAGNodeDeletedListener {
    template <typename T>
    Deleter(SelectionDAG &D, T &C)
      : SelectionDAG::DAGNodeDeletedListener(D, [&C] (SDNode *N, SDNode *E) {
                                                  C.erase(N);
                                                }) {}
  };

  template <typename T>
  struct NullifyingVector : public T {
    DenseMap<SDNode*, SDNode**> Refs;
    NullifyingVector(T &&V) : T(V) {
      for (unsigned i = 0, e = T::size(); i != e; ++i) {
        SDNode *&N = T::operator[](i);
        Refs[N] = &N;
      }
    }
    void erase(SDNode *N) {
      auto F = Refs.find(N);
      if (F != Refs.end())
        *F->second = nullptr;
    }
  };
}

void HvxSelector::select(SDNode *ISelN) {
  // What's important here is to select the right set of nodes. The main
  // selection algorithm loops over nodes in a topological order, i.e. users
  // are visited before their operands.
  //
  // It is an error to have an unselected node with a selected operand, and
  // there is an assertion in the main selector code to enforce that.
  //
  // Such a situation could occur if we selected a node, which is both a
  // subnode of ISelN, and a subnode of an unrelated (and yet unselected)
  // node in the DAG.
  assert(ISelN->getOpcode() == HexagonISD::ISEL);
  SDNode *N0 = ISelN->getOperand(0).getNode();

  // There could have been nodes created (i.e. inserted into the DAG)
  // that are now dead. Remove them, in case they use any of the nodes
  // to select (and make them look shared).
  DAG.RemoveDeadNodes();

  SetVector<SDNode *> SubNodes;

  if (!N0->isMachineOpcode()) {
    // Don't want to select N0 if it's shared with another node, except if
    // it's shared with other ISELs.
    auto IsISelN = [](SDNode *T) { return T->getOpcode() == HexagonISD::ISEL; };
    if (llvm::all_of(N0->uses(), IsISelN))
      SubNodes.insert(N0);
  }
  if (SubNodes.empty()) {
    ISel.ReplaceNode(ISelN, N0);
    return;
  }

  // Need to manually select the nodes that are dominated by the ISEL. Other
  // nodes are reachable from the rest of the DAG, and so will be selected
  // by the DAG selection routine.
  SetVector<SDNode*> Dom, NonDom;
  Dom.insert(N0);

  auto IsDomRec = [&Dom, &NonDom] (SDNode *T, auto Rec) -> bool {
    if (Dom.count(T))
      return true;
    if (T->use_empty() || NonDom.count(T))
      return false;
    for (SDNode *U : T->uses()) {
      // If T is reachable from a known non-dominated node, then T itself
      // is non-dominated.
      if (!Rec(U, Rec)) {
        NonDom.insert(T);
        return false;
      }
    }
    Dom.insert(T);
    return true;
  };

  auto IsDom = [&IsDomRec] (SDNode *T) { return IsDomRec(T, IsDomRec); };

  // Add the rest of nodes dominated by ISEL to SubNodes.
  for (unsigned I = 0; I != SubNodes.size(); ++I) {
    for (SDValue Op : SubNodes[I]->ops()) {
      SDNode *O = Op.getNode();
      if (IsDom(O))
        SubNodes.insert(O);
    }
  }

  // Do a topological sort of nodes from Dom.
  SetVector<SDNode*> TmpQ;

  std::map<SDNode *, unsigned> OpCount;
  for (SDNode *T : Dom) {
    unsigned NumDomOps = llvm::count_if(T->ops(), [&Dom](const SDUse &U) {
                             return Dom.count(U.getNode());
                           });

    OpCount.insert({T, NumDomOps});
    if (NumDomOps == 0)
      TmpQ.insert(T);
  }

  for (unsigned I = 0; I != TmpQ.size(); ++I) {
    SDNode *S = TmpQ[I];
    for (SDNode *U : S->uses()) {
      if (U == ISelN)
        continue;
      auto F = OpCount.find(U);
      assert(F != OpCount.end());
      if (F->second > 0 && !--F->second)
        TmpQ.insert(F->first);
    }
  }

  // Remove the marker.
  ISel.ReplaceNode(ISelN, N0);

  assert(SubNodes.size() == TmpQ.size());
  NullifyingVector<decltype(TmpQ)::vector_type> Queue(TmpQ.takeVector());

  Deleter DUQ(DAG, Queue);
  for (SDNode *S : reverse(Queue)) {
    if (S == nullptr)
      continue;
    DEBUG_WITH_TYPE("isel", {dbgs() << "HVX selecting: "; S->dump(&DAG);});
    ISel.Select(S);
  }
}

bool HvxSelector::scalarizeShuffle(ArrayRef<int> Mask, const SDLoc &dl,
                                   MVT ResTy, SDValue Va, SDValue Vb,
                                   SDNode *N) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  MVT ElemTy = ResTy.getVectorElementType();
  assert(ElemTy == MVT::i8);
  unsigned VecLen = Mask.size();
  bool HavePairs = (2*HwLen == VecLen);
  MVT SingleTy = getSingleVT(MVT::i8);

  // The prior attempts to handle this shuffle may have left a bunch of
  // dead nodes in the DAG (such as constants). These nodes will be added
  // at the end of DAG's node list, which at that point had already been
  // sorted topologically. In the main selection loop, the node list is
  // traversed backwards from the root node, which means that any new
  // nodes (from the end of the list) will not be visited.
  // Scalarization will replace the shuffle node with the scalarized
  // expression, and if that expression reused any if the leftoever (dead)
  // nodes, these nodes would not be selected (since the "local" selection
  // only visits nodes that are not in AllNodes).
  // To avoid this issue, remove all dead nodes from the DAG now.
//  DAG.RemoveDeadNodes();

  SmallVector<SDValue,128> Ops;
  LLVMContext &Ctx = *DAG.getContext();
  MVT LegalTy = Lower.getTypeToTransformTo(Ctx, ElemTy).getSimpleVT();
  for (int I : Mask) {
    if (I < 0) {
      Ops.push_back(ISel.selectUndef(dl, LegalTy));
      continue;
    }
    SDValue Vec;
    unsigned M = I;
    if (M < VecLen) {
      Vec = Va;
    } else {
      Vec = Vb;
      M -= VecLen;
    }
    if (HavePairs) {
      if (M < HwLen) {
        Vec = DAG.getTargetExtractSubreg(Hexagon::vsub_lo, dl, SingleTy, Vec);
      } else {
        Vec = DAG.getTargetExtractSubreg(Hexagon::vsub_hi, dl, SingleTy, Vec);
        M -= HwLen;
      }
    }
    SDValue Idx = DAG.getConstant(M, dl, MVT::i32);
    SDValue Ex = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, LegalTy, {Vec, Idx});
    SDValue L = Lower.LowerOperation(Ex, DAG);
    assert(L.getNode());
    Ops.push_back(L);
  }

  SDValue LV;
  if (2*HwLen == VecLen) {
    SDValue B0 = DAG.getBuildVector(SingleTy, dl, {Ops.data(), HwLen});
    SDValue L0 = Lower.LowerOperation(B0, DAG);
    SDValue B1 = DAG.getBuildVector(SingleTy, dl, {Ops.data()+HwLen, HwLen});
    SDValue L1 = Lower.LowerOperation(B1, DAG);
    // XXX CONCAT_VECTORS is legal for HVX vectors. Legalizing (lowering)
    // functions may expect to be called only for illegal operations, so
    // make sure that they are not called for legal ones. Develop a better
    // mechanism for dealing with this.
    LV = DAG.getNode(ISD::CONCAT_VECTORS, dl, ResTy, {L0, L1});
  } else {
    SDValue BV = DAG.getBuildVector(ResTy, dl, Ops);
    LV = Lower.LowerOperation(BV, DAG);
  }

  assert(!N->use_empty());
  SDValue IS = DAG.getNode(HexagonISD::ISEL, dl, ResTy, LV);
  ISel.ReplaceNode(N, IS.getNode());
  select(IS.getNode());
  DAG.RemoveDeadNodes();
  return true;
}

SmallVector<uint32_t, 8> HvxSelector::getPerfectCompletions(ShuffleMask SM,
                                                            unsigned Width) {
  auto possibilities = [](ArrayRef<uint8_t> Bs, unsigned Width) -> uint32_t {
    unsigned Impossible = ~(1u << Width) + 1;
    for (unsigned I = 0, E = Bs.size(); I != E; ++I) {
      uint8_t B = Bs[I];
      if (B == 0xff)
        continue;
      if (~Impossible == 0)
        break;
      for (unsigned Log = 0; Log != Width; ++Log) {
        if (Impossible & (1u << Log))
          continue;
        unsigned Expected = (I >> Log) % 2;
        if (B != Expected)
          Impossible |= (1u << Log);
      }
    }
    return ~Impossible;
  };

  SmallVector<uint32_t, 8> Worklist(Width);

  for (unsigned BitIdx = 0; BitIdx != Width; ++BitIdx) {
    SmallVector<uint8_t> BitValues(SM.Mask.size());
    for (int i = 0, e = SM.Mask.size(); i != e; ++i) {
      int M = SM.Mask[i];
      if (M < 0)
        BitValues[i] = 0xff;
      else
        BitValues[i] = (M & (1u << BitIdx)) != 0;
    }
    Worklist[BitIdx] = possibilities(BitValues, Width);
  }

  // If there is a word P in Worklist that matches multiple possibilities,
  // then if any other word Q matches any of the possibilities matched by P,
  // then Q matches all the possibilities matched by P. In fact, P == Q.
  // In other words, for each words P, Q, the sets of possibilities matched
  // by P and Q are either equal or disjoint (no partial overlap).
  //
  // Illustration: For 4-bit values there are 4 complete sequences:
  // a:  0 1 0 1  0 1 0 1  0 1 0 1  0 1 0 1
  // b:  0 0 1 1  0 0 1 1  0 0 1 1  0 0 1 1
  // c:  0 0 0 0  1 1 1 1  0 0 0 0  1 1 1 1
  // d:  0 0 0 0  0 0 0 0  1 1 1 1  1 1 1 1
  //
  // Words containing unknown bits that match two of the complete
  // sequences:
  // ab: 0 u u 1  0 u u 1  0 u u 1  0 u u 1
  // ac: 0 u 0 u  u 1 u 1  0 u 0 u  u 1 u 1
  // ad: 0 u 0 u  0 u 0 u  u 1 u 1  u 1 u 1
  // bc: 0 0 u u  u u 1 1  0 0 u u  u u 1 1
  // bd: 0 0 u u  0 0 u u  u u 1 1  u u 1 1
  // cd: 0 0 0 0  u u u u  u u u u  1 1 1 1
  //
  // Proof of the claim above:
  // Let P be a word that matches s0 and s1. For that to happen, all known
  // bits in P must match s0 and s1 exactly.
  // Assume there is Q that matches s1. Note that since P and Q came from
  // the same shuffle mask, the positions of unknown bits in P and Q match
  // exactly, which makes the indices of known bits be exactly the same
  // between P and Q. Since P matches s0 and s1, the known bits of P much
  // match both s0 and s1. Also, since Q matches s1, the known bits in Q
  // are exactly the same as in s1, which means that they are exactly the
  // same as in P. This implies that P == Q.

  // There can be a situation where there are more entries with the same
  // bits set than there are set bits (e.g. value 9 occuring more than 2
  // times). In such cases it will be impossible to complete this to a
  // perfect shuffle.
  SmallVector<uint32_t, 8> Sorted(Worklist);
  llvm::sort(Sorted.begin(), Sorted.end());

  for (unsigned I = 0, E = Sorted.size(); I != E;) {
    unsigned P = Sorted[I], Count = 1;
    while (++I != E && P == Sorted[I])
      ++Count;
    if ((unsigned)llvm::popcount(P) < Count) {
      // Reset all occurences of P, if there are more occurrences of P
      // than there are bits in P.
      for (unsigned &Q : Worklist) {
        if (Q == P)
          Q = 0;
      }
    }
  }

  return Worklist;
}

SmallVector<uint32_t, 8>
HvxSelector::completeToPerfect(ArrayRef<uint32_t> Completions, unsigned Width) {
  // Pick a completion if there are multiple possibilities. For now just
  // select any valid completion.
  SmallVector<uint32_t, 8> Comps(Completions);

  for (unsigned I = 0; I != Width; ++I) {
    uint32_t P = Comps[I];
    assert(P != 0);
    if (isPowerOf2_32(P))
      continue;
    // T = least significant bit of P.
    uint32_t T = P ^ ((P - 1) & P);
    // Clear T in all remaining words matching P.
    for (unsigned J = I + 1; J != Width; ++J) {
      if (Comps[J] == P)
        Comps[J] ^= T;
    }
    Comps[I] = T;
  }

#ifndef NDEBUG
  // Check that we have generated a valid completion.
  uint32_t OrAll = 0;
  for (uint32_t C : Comps) {
    assert(isPowerOf2_32(C));
    OrAll |= C;
  }
  assert(OrAll == (1u << Width) -1);
#endif

  return Comps;
}

std::optional<int> HvxSelector::rotationDistance(ShuffleMask SM,
                                                 unsigned WrapAt) {
  std::optional<int> Dist;
  for (int I = 0, E = SM.Mask.size(); I != E; ++I) {
    int M = SM.Mask[I];
    if (M < 0)
      continue;
    if (Dist) {
      if ((I + *Dist) % static_cast<int>(WrapAt) != M)
        return std::nullopt;
    } else {
      // Integer a%b operator assumes rounding towards zero by /, so it
      // "misbehaves" when a crosses 0 (the remainder also changes sign).
      // Add WrapAt in an attempt to keep I+Dist non-negative.
      Dist = M - I;
      if (Dist < 0)
        Dist = *Dist + WrapAt;
    }
  }
  return Dist;
}

OpRef HvxSelector::contracting(ShuffleMask SM, OpRef Va, OpRef Vb,
                               ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", { dbgs() << __func__ << '\n'; });
  if (!Va.isValid() || !Vb.isValid())
    return OpRef::fail();

  // Contracting shuffles, i.e. instructions that always discard some bytes
  // from the operand vectors.
  //
  // Funnel shifts
  // V6_vshuff{e,o}b
  // V6_vshuf{e,o}h
  // V6_vdealb4w
  // V6_vpack{e,o}{b,h}

  int VecLen = SM.Mask.size();

  // First, check for funnel shifts.
  if (auto Dist = rotationDistance(SM, 2 * VecLen)) {
    OpRef Funnel = funnels(Va, Vb, *Dist, Results);
    if (Funnel.isValid())
      return Funnel;
  }

  MVT SingleTy = getSingleVT(MVT::i8);
  MVT PairTy = getPairVT(MVT::i8);

  auto same = [](ArrayRef<int> Mask1, ArrayRef<int> Mask2) -> bool {
    return Mask1 == Mask2;
  };

  using PackConfig = std::pair<unsigned, bool>;
  PackConfig Packs[] = {
      {1, false}, // byte, even
      {1, true},  // byte, odd
      {2, false}, // half, even
      {2, true},  // half, odd
  };

  { // Check vpack
    unsigned Opcodes[] = {
        Hexagon::V6_vpackeb,
        Hexagon::V6_vpackob,
        Hexagon::V6_vpackeh,
        Hexagon::V6_vpackoh,
    };
    for (int i = 0, e = std::size(Opcodes); i != e; ++i) {
      auto [Size, Odd] = Packs[i];
      if (same(SM.Mask, shuffles::mask(shuffles::vpack, HwLen, Size, Odd))) {
        Results.push(Opcodes[i], SingleTy, {Vb, Va});
        return OpRef::res(Results.top());
      }
    }
  }

  { // Check vshuff
    unsigned Opcodes[] = {
        Hexagon::V6_vshuffeb,
        Hexagon::V6_vshuffob,
        Hexagon::V6_vshufeh,
        Hexagon::V6_vshufoh,
    };
    for (int i = 0, e = std::size(Opcodes); i != e; ++i) {
      auto [Size, Odd] = Packs[i];
      if (same(SM.Mask, shuffles::mask(shuffles::vshuff, HwLen, Size, Odd))) {
        Results.push(Opcodes[i], SingleTy, {Vb, Va});
        return OpRef::res(Results.top());
      }
    }
  }

  { // Check vdeal
    // There is no "V6_vdealeb", etc, but the supposed behavior of vdealeb
    // is equivalent to "(V6_vpackeb (V6_vdealvdd Vu, Vv, -2))". Other such
    // variants of "deal" can be done similarly.
    unsigned Opcodes[] = {
        Hexagon::V6_vpackeb,
        Hexagon::V6_vpackob,
        Hexagon::V6_vpackeh,
        Hexagon::V6_vpackoh,
    };
    const SDLoc &dl(Results.InpNode);

    for (int i = 0, e = std::size(Opcodes); i != e; ++i) {
      auto [Size, Odd] = Packs[i];
      if (same(SM.Mask, shuffles::mask(shuffles::vdeal, HwLen, Size, Odd))) {
        Results.push(Hexagon::A2_tfrsi, MVT::i32, {getConst32(-2 * Size, dl)});
        Results.push(Hexagon::V6_vdealvdd, PairTy, {Vb, Va, OpRef::res(-1)});
        auto vdeal = OpRef::res(Results.top());
        Results.push(Opcodes[i], SingleTy,
                     {OpRef::hi(vdeal), OpRef::lo(vdeal)});
        return OpRef::res(Results.top());
      }
    }
  }

  if (same(SM.Mask, shuffles::mask(shuffles::vdealb4w, HwLen))) {
    Results.push(Hexagon::V6_vdealb4w, SingleTy, {Vb, Va});
    return OpRef::res(Results.top());
  }

  return OpRef::fail();
}

OpRef HvxSelector::expanding(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  // Expanding shuffles (using all elements and inserting into larger vector):
  //
  // V6_vunpacku{b,h} [*]
  //
  // [*] Only if the upper elements (filled with 0s) are "don't care" in Mask.
  //
  // Note: V6_vunpacko{b,h} are or-ing the high byte/half in the result, so
  // they are not shuffles.
  //
  // The argument is a single vector.

  int VecLen = SM.Mask.size();
  assert(2*HwLen == unsigned(VecLen) && "Expecting vector-pair type");

  std::pair<int,unsigned> Strip = findStrip(SM.Mask, 1, VecLen);

  // The patterns for the unpacks, in terms of the starting offsets of the
  // consecutive strips (L = length of the strip, N = VecLen):
  //
  // vunpacku:  0, -1, L, -1, 2L, -1 ...

  if (Strip.first != 0)
    return OpRef::fail();

  // The vunpackus only handle byte and half-word.
  if (Strip.second != 1 && Strip.second != 2)
    return OpRef::fail();

  int N = VecLen;
  int L = Strip.second;

  // First, check the non-ignored strips.
  for (int I = 2*L; I < N; I += 2*L) {
    auto S = findStrip(SM.Mask.drop_front(I), 1, N-I);
    if (S.second != unsigned(L))
      return OpRef::fail();
    if (2*S.first != I)
      return OpRef::fail();
  }
  // Check the -1s.
  for (int I = L; I < N; I += 2*L) {
    auto S = findStrip(SM.Mask.drop_front(I), 0, N-I);
    if (S.first != -1 || S.second != unsigned(L))
      return OpRef::fail();
  }

  unsigned Opc = Strip.second == 1 ? Hexagon::V6_vunpackub
                                   : Hexagon::V6_vunpackuh;
  Results.push(Opc, getPairVT(MVT::i8), {Va});
  return OpRef::res(Results.top());
}

OpRef HvxSelector::perfect(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", { dbgs() << __func__ << '\n'; });
  // V6_vdeal{b,h}
  // V6_vshuff{b,h}

  // V6_vshufoe{b,h}  those are quivalent to vshuffvdd(..,{1,2})
  // V6_vshuffvdd (V6_vshuff)
  // V6_dealvdd (V6_vdeal)

  int VecLen = SM.Mask.size();
  assert(isPowerOf2_32(VecLen) && Log2_32(VecLen) <= 8);
  unsigned LogLen = Log2_32(VecLen);
  unsigned HwLog = Log2_32(HwLen);
  // The result length must be the same as the length of a single vector,
  // or a vector pair.
  assert(LogLen == HwLog || LogLen == HwLog + 1);
  bool HavePairs = LogLen == HwLog + 1;

  SmallVector<unsigned, 8> Perm(LogLen);

  // Check if this could be a perfect shuffle, or a combination of perfect
  // shuffles.
  //
  // Consider this permutation (using hex digits to make the ASCII diagrams
  // easier to read):
  //   { 0, 8, 1, 9, 2, A, 3, B, 4, C, 5, D, 6, E, 7, F }.
  // This is a "deal" operation: divide the input into two halves, and
  // create the output by picking elements by alternating between these two
  // halves:
  //   0 1 2 3 4 5 6 7    -->    0 8 1 9 2 A 3 B 4 C 5 D 6 E 7 F  [*]
  //   8 9 A B C D E F
  //
  // Aside from a few special explicit cases (V6_vdealb, etc.), HVX provides
  // a somwehat different mechanism that could be used to perform shuffle/
  // deal operations: a 2x2 transpose.
  // Consider the halves of inputs again, they can be interpreted as a 2x8
  // matrix. A 2x8 matrix can be looked at four 2x2 matrices concatenated
  // together. Now, when considering 2 elements at a time, it will be a 2x4
  // matrix (with elements 01, 23, 45, etc.), or two 2x2 matrices:
  //   01 23  45 67
  //   89 AB  CD EF
  // With groups of 4, this will become a single 2x2 matrix, and so on.
  //
  // The 2x2 transpose instruction works by transposing each of the 2x2
  // matrices (or "sub-matrices"), given a specific group size. For example,
  // if the group size is 1 (i.e. each element is its own group), there
  // will be four transposes of the four 2x2 matrices that form the 2x8.
  // For example, with the inputs as above, the result will be:
  //   0 8  2 A  4 C  6 E
  //   1 9  3 B  5 D  7 F
  // Now, this result can be tranposed again, but with the group size of 2:
  //   08 19  4C 5D
  //   2A 3B  6E 7F
  // If we then transpose that result, but with the group size of 4, we get:
  //   0819 2A3B
  //   4C5D 6E7F
  // If we concatenate these two rows, it will be
  //   0 8 1 9 2 A 3 B 4 C 5 D 6 E 7 F
  // which is the same as the "deal" [*] above.
  //
  // In general, a "deal" of individual elements is a series of 2x2 transposes,
  // with changing group size. HVX has two instructions:
  //   Vdd = V6_vdealvdd Vu, Vv, Rt
  //   Vdd = V6_shufvdd  Vu, Vv, Rt
  // that perform exactly that. The register Rt controls which transposes are
  // going to happen: a bit at position n (counting from 0) indicates that a
  // transpose with a group size of 2^n will take place. If multiple bits are
  // set, multiple transposes will happen: vdealvdd will perform them starting
  // with the largest group size, vshuffvdd will do them in the reverse order.
  //
  // The main observation is that each 2x2 transpose corresponds to swapping
  // columns of bits in the binary representation of the values.
  //
  // The numbers {3,2,1,0} and the log2 of the number of contiguous 1 bits
  // in a given column. The * denote the columns that will be swapped.
  // The transpose with the group size 2^n corresponds to swapping columns
  // 3 (the highest log) and log2(n):
  //
  //     3 2 1 0         0 2 1 3         0 2 3 1
  //     *     *             * *           * *
  //  0  0 0 0 0      0  0 0 0 0      0  0 0 0 0      0  0 0 0 0
  //  1  0 0 0 1      8  1 0 0 0      8  1 0 0 0      8  1 0 0 0
  //  2  0 0 1 0      2  0 0 1 0      1  0 0 0 1      1  0 0 0 1
  //  3  0 0 1 1      A  1 0 1 0      9  1 0 0 1      9  1 0 0 1
  //  4  0 1 0 0      4  0 1 0 0      4  0 1 0 0      2  0 0 1 0
  //  5  0 1 0 1      C  1 1 0 0      C  1 1 0 0      A  1 0 1 0
  //  6  0 1 1 0      6  0 1 1 0      5  0 1 0 1      3  0 0 1 1
  //  7  0 1 1 1      E  1 1 1 0      D  1 1 0 1      B  1 0 1 1
  //  8  1 0 0 0      1  0 0 0 1      2  0 0 1 0      4  0 1 0 0
  //  9  1 0 0 1      9  1 0 0 1      A  1 0 1 0      C  1 1 0 0
  //  A  1 0 1 0      3  0 0 1 1      3  0 0 1 1      5  0 1 0 1
  //  B  1 0 1 1      B  1 0 1 1      B  1 0 1 1      D  1 1 0 1
  //  C  1 1 0 0      5  0 1 0 1      6  0 1 1 0      6  0 1 1 0
  //  D  1 1 0 1      D  1 1 0 1      E  1 1 1 0      E  1 1 1 0
  //  E  1 1 1 0      7  0 1 1 1      7  0 1 1 1      7  0 1 1 1
  //  F  1 1 1 1      F  1 1 1 1      F  1 1 1 1      F  1 1 1 1

  // There is one special case that is not a perfect shuffle, but can be
  // turned into one easily: when the shuffle operates on a vector pair,
  // but the two vectors in the pair are swapped. The code that identifies
  // perfect shuffles will reject it, unless the order is reversed.
  shuffles::MaskT MaskStorage(SM.Mask);
  bool InvertedPair = false;
  if (HavePairs && SM.Mask[0] >= int(HwLen)) {
    for (int i = 0, e = SM.Mask.size(); i != e; ++i) {
      int M = SM.Mask[i];
      MaskStorage[i] = M >= int(HwLen) ? M - HwLen : M + HwLen;
    }
    InvertedPair = true;
    SM = ShuffleMask(MaskStorage);
  }

  auto Comps = getPerfectCompletions(SM, LogLen);
  if (llvm::is_contained(Comps, 0))
    return OpRef::fail();

  auto Pick = completeToPerfect(Comps, LogLen);
  for (unsigned I = 0; I != LogLen; ++I)
    Perm[I] = Log2_32(Pick[I]);

  // Once we have Perm, represent it as cycles. Denote the maximum log2
  // (equal to log2(VecLen)-1) as M. The cycle containing M can then be
  // written as (M a1 a2 a3 ... an). That cycle can be broken up into
  // simple swaps as (M a1)(M a2)(M a3)...(M an), with the composition
  // order being from left to right. Any (contiguous) segment where the
  // values ai, ai+1...aj are either all increasing or all decreasing,
  // can be implemented via a single vshuffvdd/vdealvdd respectively.
  //
  // If there is a cycle (a1 a2 ... an) that does not involve M, it can
  // be written as (M an)(a1 a2 ... an)(M a1). The first two cycles can
  // then be folded to get (M a1 a2 ... an)(M a1), and the above procedure
  // can be used to generate a sequence of vshuffvdd/vdealvdd.
  //
  // Example:
  // Assume M = 4 and consider a permutation (0 1)(2 3). It can be written
  // as (4 0 1)(4 0) composed with (4 2 3)(4 2), or simply
  //   (4 0 1)(4 0)(4 2 3)(4 2).
  // It can then be expanded into swaps as
  //   (4 0)(4 1)(4 0)(4 2)(4 3)(4 2),
  // and broken up into "increasing" segments as
  //   [(4 0)(4 1)] [(4 0)(4 2)(4 3)] [(4 2)].
  // This is equivalent to
  //   (4 0 1)(4 0 2 3)(4 2),
  // which can be implemented as 3 vshufvdd instructions.

  using CycleType = SmallVector<unsigned, 8>;
  std::set<CycleType> Cycles;
  std::set<unsigned> All;

  for (unsigned I : Perm)
    All.insert(I);

  // If the cycle contains LogLen-1, move it to the front of the cycle.
  // Otherwise, return the cycle unchanged.
  auto canonicalize = [LogLen](const CycleType &C) -> CycleType {
    unsigned LogPos, N = C.size();
    for (LogPos = 0; LogPos != N; ++LogPos)
      if (C[LogPos] == LogLen - 1)
        break;
    if (LogPos == N)
      return C;

    CycleType NewC(C.begin() + LogPos, C.end());
    NewC.append(C.begin(), C.begin() + LogPos);
    return NewC;
  };

  auto pfs = [](const std::set<CycleType> &Cs, unsigned Len) {
    // Ordering: shuff: 5 0 1 2 3 4, deal: 5 4 3 2 1 0 (for Log=6),
    // for bytes zero is included, for halfwords is not.
    if (Cs.size() != 1)
      return 0u;
    const CycleType &C = *Cs.begin();
    if (C[0] != Len - 1)
      return 0u;
    int D = Len - C.size();
    if (D != 0 && D != 1)
      return 0u;

    bool IsDeal = true, IsShuff = true;
    for (unsigned I = 1; I != Len - D; ++I) {
      if (C[I] != Len - 1 - I)
        IsDeal = false;
      if (C[I] != I - (1 - D)) // I-1, I
        IsShuff = false;
    }
    // At most one, IsDeal or IsShuff, can be non-zero.
    assert(!(IsDeal || IsShuff) || IsDeal != IsShuff);
    static unsigned Deals[] = {Hexagon::V6_vdealb, Hexagon::V6_vdealh};
    static unsigned Shufs[] = {Hexagon::V6_vshuffb, Hexagon::V6_vshuffh};
    return IsDeal ? Deals[D] : (IsShuff ? Shufs[D] : 0);
  };

  while (!All.empty()) {
    unsigned A = *All.begin();
    All.erase(A);
    CycleType C;
    C.push_back(A);
    for (unsigned B = Perm[A]; B != A; B = Perm[B]) {
      C.push_back(B);
      All.erase(B);
    }
    if (C.size() <= 1)
      continue;
    Cycles.insert(canonicalize(C));
  }

  MVT SingleTy = getSingleVT(MVT::i8);
  MVT PairTy = getPairVT(MVT::i8);

  // Recognize patterns for V6_vdeal{b,h} and V6_vshuff{b,h}.
  if (unsigned(VecLen) == HwLen) {
    if (unsigned SingleOpc = pfs(Cycles, LogLen)) {
      Results.push(SingleOpc, SingleTy, {Va});
      return OpRef::res(Results.top());
    }
  }

  // From the cycles, construct the sequence of values that will
  // then form the control values for vdealvdd/vshuffvdd, i.e.
  // (M a1 a2)(M a3 a4 a5)... -> a1 a2 a3 a4 a5
  // This essentially strips the M value from the cycles where
  // it's present, and performs the insertion of M (then stripping)
  // for cycles without M (as described in an earlier comment).
  SmallVector<unsigned, 8> SwapElems;
  // When the input is extended (i.e. single vector becomes a pair),
  // this is done by using an "undef" vector as the second input.
  // However, then we get
  //   input 1: GOODBITS
  //   input 2: ........
  // but we need
  //   input 1: ....BITS
  //   input 2: ....GOOD
  // Then at the end, this needs to be undone. To accomplish this,
  // artificially add "LogLen-1" at both ends of the sequence.
  if (!HavePairs)
    SwapElems.push_back(LogLen - 1);
  for (const CycleType &C : Cycles) {
    // Do the transformation: (a1..an) -> (M a1..an)(M a1).
    unsigned First = (C[0] == LogLen - 1) ? 1 : 0;
    SwapElems.append(C.begin() + First, C.end());
    if (First == 0)
      SwapElems.push_back(C[0]);
  }
  if (!HavePairs)
    SwapElems.push_back(LogLen - 1);

  const SDLoc &dl(Results.InpNode);
  OpRef Arg = HavePairs ? Va : concats(Va, OpRef::undef(SingleTy), Results);
  if (InvertedPair)
    Arg = concats(OpRef::hi(Arg), OpRef::lo(Arg), Results);

  for (unsigned I = 0, E = SwapElems.size(); I != E;) {
    bool IsInc = I == E - 1 || SwapElems[I] < SwapElems[I + 1];
    unsigned S = (1u << SwapElems[I]);
    if (I < E - 1) {
      while (++I < E - 1 && IsInc == (SwapElems[I] < SwapElems[I + 1]))
        S |= 1u << SwapElems[I];
      // The above loop will not add a bit for the final SwapElems[I+1],
      // so add it here.
      S |= 1u << SwapElems[I];
    }
    ++I;

    NodeTemplate Res;
    Results.push(Hexagon::A2_tfrsi, MVT::i32, {getConst32(S, dl)});
    Res.Opc = IsInc ? Hexagon::V6_vshuffvdd : Hexagon::V6_vdealvdd;
    Res.Ty = PairTy;
    Res.Ops = {OpRef::hi(Arg), OpRef::lo(Arg), OpRef::res(-1)};
    Results.push(Res);
    Arg = OpRef::res(Results.top());
  }

  return HavePairs ? Arg : OpRef::lo(Arg);
}

OpRef HvxSelector::butterfly(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  // Butterfly shuffles.
  //
  // V6_vdelta
  // V6_vrdelta
  // V6_vror

  // The assumption here is that all elements picked by Mask are in the
  // first operand to the vector_shuffle. This assumption is enforced
  // by the caller.

  MVT ResTy = getSingleVT(MVT::i8);
  PermNetwork::Controls FC, RC;
  const SDLoc &dl(Results.InpNode);
  int VecLen = SM.Mask.size();

  for (int M : SM.Mask) {
    if (M != -1 && M >= VecLen)
      return OpRef::fail();
  }

  // Try the deltas/benes for both single vectors and vector pairs.
  ForwardDeltaNetwork FN(SM.Mask);
  if (FN.run(FC)) {
    SDValue Ctl = getVectorConstant(FC, dl);
    Results.push(Hexagon::V6_vdelta, ResTy, {Va, OpRef(Ctl)});
    return OpRef::res(Results.top());
  }

  // Try reverse delta.
  ReverseDeltaNetwork RN(SM.Mask);
  if (RN.run(RC)) {
    SDValue Ctl = getVectorConstant(RC, dl);
    Results.push(Hexagon::V6_vrdelta, ResTy, {Va, OpRef(Ctl)});
    return OpRef::res(Results.top());
  }

  // Do Benes.
  BenesNetwork BN(SM.Mask);
  if (BN.run(FC, RC)) {
    SDValue CtlF = getVectorConstant(FC, dl);
    SDValue CtlR = getVectorConstant(RC, dl);
    Results.push(Hexagon::V6_vdelta, ResTy, {Va, OpRef(CtlF)});
    Results.push(Hexagon::V6_vrdelta, ResTy,
                 {OpRef::res(-1), OpRef(CtlR)});
    return OpRef::res(Results.top());
  }

  return OpRef::fail();
}

SDValue HvxSelector::getConst32(int Val, const SDLoc &dl) {
  return DAG.getTargetConstant(Val, dl, MVT::i32);
}

SDValue HvxSelector::getVectorConstant(ArrayRef<uint8_t> Data,
                                       const SDLoc &dl) {
  SmallVector<SDValue, 128> Elems;
  for (uint8_t C : Data)
    Elems.push_back(DAG.getConstant(C, dl, MVT::i8));
  MVT VecTy = MVT::getVectorVT(MVT::i8, Data.size());
  SDValue BV = DAG.getBuildVector(VecTy, dl, Elems);
  SDValue LV = Lower.LowerOperation(BV, DAG);
  DAG.RemoveDeadNode(BV.getNode());
  return DAG.getNode(HexagonISD::ISEL, dl, VecTy, LV);
}

void HvxSelector::selectExtractSubvector(SDNode *N) {
  SDValue Inp = N->getOperand(0);
  MVT ResTy = N->getValueType(0).getSimpleVT();
  unsigned Idx = N->getConstantOperandVal(1);

  [[maybe_unused]] MVT InpTy = Inp.getValueType().getSimpleVT();
  [[maybe_unused]] unsigned ResLen = ResTy.getVectorNumElements();
  assert(InpTy.getVectorElementType() == ResTy.getVectorElementType());
  assert(2 * ResLen == InpTy.getVectorNumElements());
  assert(Idx == 0 || Idx == ResLen);

  unsigned SubReg = Idx == 0 ? Hexagon::vsub_lo : Hexagon::vsub_hi;
  SDValue Ext = DAG.getTargetExtractSubreg(SubReg, SDLoc(N), ResTy, Inp);

  ISel.ReplaceNode(N, Ext.getNode());
}

void HvxSelector::selectShuffle(SDNode *N) {
  DEBUG_WITH_TYPE("isel", {
    dbgs() << "Starting " << __func__ << " on node:\n";
    N->dump(&DAG);
  });
  MVT ResTy = N->getValueType(0).getSimpleVT();
  // Assume that vector shuffles operate on vectors of bytes.
  assert(ResTy.isVector() && ResTy.getVectorElementType() == MVT::i8);

  auto *SN = cast<ShuffleVectorSDNode>(N);
  std::vector<int> Mask(SN->getMask().begin(), SN->getMask().end());
  // This shouldn't really be necessary. Is it?
  for (int &Idx : Mask)
    if (Idx != -1 && Idx < 0)
      Idx = -1;

  unsigned VecLen = Mask.size();
  bool HavePairs = (2*HwLen == VecLen);
  assert(ResTy.getSizeInBits() / 8 == VecLen);

  // Vd = vector_shuffle Va, Vb, Mask
  //

  bool UseLeft = false, UseRight = false;
  for (unsigned I = 0; I != VecLen; ++I) {
    if (Mask[I] == -1)
      continue;
    unsigned Idx = Mask[I];
    assert(Idx < 2*VecLen);
    if (Idx < VecLen)
      UseLeft = true;
    else
      UseRight = true;
  }

  DEBUG_WITH_TYPE("isel", {
    dbgs() << "VecLen=" << VecLen << " HwLen=" << HwLen << " UseLeft="
           << UseLeft << " UseRight=" << UseRight << " HavePairs="
           << HavePairs << '\n';
  });
  // If the mask is all -1's, generate "undef".
  if (!UseLeft && !UseRight) {
    ISel.ReplaceNode(N, ISel.selectUndef(SDLoc(SN), ResTy).getNode());
    return;
  }

  SDValue Vec0 = N->getOperand(0);
  SDValue Vec1 = N->getOperand(1);
  assert(Vec0.getValueType() == ResTy && Vec1.getValueType() == ResTy);

  ResultStack Results(SN);
  OpRef Va = OpRef::undef(ResTy);
  OpRef Vb = OpRef::undef(ResTy);

  if (!Vec0.isUndef()) {
    Results.push(TargetOpcode::COPY, ResTy, {Vec0});
    Va = OpRef::OpRef::res(Results.top());
  }
  if (!Vec1.isUndef()) {
    Results.push(TargetOpcode::COPY, ResTy, {Vec1});
    Vb = OpRef::res(Results.top());
  }

  OpRef Res = !HavePairs ? shuffs2(ShuffleMask(Mask), Va, Vb, Results)
                         : shuffp2(ShuffleMask(Mask), Va, Vb, Results);

  bool Done = Res.isValid();
  if (Done) {
    // Make sure that Res is on the stack before materializing.
    Results.push(TargetOpcode::COPY, ResTy, {Res});
    materialize(Results);
  } else {
    Done = scalarizeShuffle(Mask, SDLoc(N), ResTy, Vec0, Vec1, N);
  }

  if (!Done) {
#ifndef NDEBUG
    dbgs() << "Unhandled shuffle:\n";
    SN->dumpr(&DAG);
#endif
    llvm_unreachable("Failed to select vector shuffle");
  }
}

void HvxSelector::selectRor(SDNode *N) {
  // If this is a rotation by less than 8, use V6_valignbi.
  MVT Ty = N->getValueType(0).getSimpleVT();
  const SDLoc &dl(N);
  SDValue VecV = N->getOperand(0);
  SDValue RotV = N->getOperand(1);
  SDNode *NewN = nullptr;

  if (auto *CN = dyn_cast<ConstantSDNode>(RotV.getNode())) {
    unsigned S = CN->getZExtValue() % HST.getVectorLength();
    if (S == 0) {
      NewN = VecV.getNode();
    } else if (isUInt<3>(S)) {
      NewN = DAG.getMachineNode(Hexagon::V6_valignbi, dl, Ty,
                                {VecV, VecV, getConst32(S, dl)});
    }
  }

  if (!NewN)
    NewN = DAG.getMachineNode(Hexagon::V6_vror, dl, Ty, {VecV, RotV});

  ISel.ReplaceNode(N, NewN);
}

void HvxSelector::selectVAlign(SDNode *N) {
  SDValue Vv = N->getOperand(0);
  SDValue Vu = N->getOperand(1);
  SDValue Rt = N->getOperand(2);
  SDNode *NewN = DAG.getMachineNode(Hexagon::V6_valignb, SDLoc(N),
                                    N->getValueType(0), {Vv, Vu, Rt});
  ISel.ReplaceNode(N, NewN);
  DAG.RemoveDeadNode(N);
}

void HexagonDAGToDAGISel::PreprocessHvxISelDAG() {
  auto getNodes = [this]() -> std::vector<SDNode *> {
    std::vector<SDNode *> T;
    T.reserve(CurDAG->allnodes_size());
    for (SDNode &N : CurDAG->allnodes())
      T.push_back(&N);
    return T;
  };

  ppHvxShuffleOfShuffle(getNodes());
}

template <> struct std::hash<SDValue> {
  std::size_t operator()(SDValue V) const {
    return std::hash<const void *>()(V.getNode()) +
           std::hash<unsigned>()(V.getResNo());
  };
};

void HexagonDAGToDAGISel::ppHvxShuffleOfShuffle(std::vector<SDNode *> &&Nodes) {
  // Motivating case:
  //   t10: v64i32 = ...
  //         t46: v128i8 = vector_shuffle<...> t44, t45
  //         t48: v128i8 = vector_shuffle<...> t44, t45
  //       t42: v128i8 = vector_shuffle<...> t46, t48
  //     t12: v32i32 = extract_subvector t10, Constant:i32<0>
  //   t44: v128i8 = bitcast t12
  //     t15: v32i32 = extract_subvector t10, Constant:i32<32>
  //   t45: v128i8 = bitcast t15
  SelectionDAG &DAG = *CurDAG;
  unsigned HwLen = HST->getVectorLength();

  struct SubVectorInfo {
    SubVectorInfo(SDValue S, unsigned H) : Src(S), HalfIdx(H) {}
    SDValue Src;
    unsigned HalfIdx;
  };

  using MapType = std::unordered_map<SDValue, unsigned>;

  auto getMaskElt = [&](unsigned Idx, ShuffleVectorSDNode *Shuff0,
                        ShuffleVectorSDNode *Shuff1,
                        const MapType &OpMap) -> int {
    // Treat Shuff0 and Shuff1 as operands to another vector shuffle, and
    // Idx as a (non-undef) element of the top level shuffle's mask, that
    // is, index into concat(Shuff0, Shuff1).
    // Assuming that Shuff0 and Shuff1 both operate on subvectors of the
    // same source vector (as described by OpMap), return the index of
    // that source vector corresponding to Idx.
    ShuffleVectorSDNode *OpShuff = Idx < HwLen ? Shuff0 : Shuff1;
    if (Idx >= HwLen)
      Idx -= HwLen;

    // Get the mask index that M points at in the corresponding operand.
    int MaybeN = OpShuff->getMaskElt(Idx);
    if (MaybeN < 0)
      return -1;

    auto N = static_cast<unsigned>(MaybeN);
    unsigned SrcBase = N < HwLen ? OpMap.at(OpShuff->getOperand(0))
                                 : OpMap.at(OpShuff->getOperand(1));
    if (N >= HwLen)
      N -= HwLen;

    return N + SrcBase;
  };

  auto fold3 = [&](SDValue TopShuff, SDValue Inp, MapType &&OpMap) -> SDValue {
    // Fold all 3 shuffles into a single one.
    auto *This = cast<ShuffleVectorSDNode>(TopShuff);
    auto *S0 = cast<ShuffleVectorSDNode>(TopShuff.getOperand(0));
    auto *S1 = cast<ShuffleVectorSDNode>(TopShuff.getOperand(1));
    ArrayRef<int> TopMask = This->getMask();
    // This should be guaranteed by type checks in the caller, and the fact
    // that all shuffles should have been promoted to operate on MVT::i8.
    assert(TopMask.size() == S0->getMask().size() &&
           TopMask.size() == S1->getMask().size());
    assert(TopMask.size() == HwLen);

    SmallVector<int, 256> FoldedMask(2 * HwLen);
    for (unsigned I = 0; I != HwLen; ++I) {
      int MaybeM = TopMask[I];
      if (MaybeM >= 0) {
        FoldedMask[I] =
            getMaskElt(static_cast<unsigned>(MaybeM), S0, S1, OpMap);
      } else {
        FoldedMask[I] = -1;
      }
    }
    // The second half of the result will be all-undef.
    std::fill(FoldedMask.begin() + HwLen, FoldedMask.end(), -1);

    // Return
    //   FoldedShuffle = (Shuffle Inp, undef, FoldedMask)
    //   (LoHalf FoldedShuffle)
    const SDLoc &dl(TopShuff);
    MVT SingleTy = MVT::getVectorVT(MVT::i8, HwLen);
    MVT PairTy = MVT::getVectorVT(MVT::i8, 2 * HwLen);
    SDValue FoldedShuff =
        DAG.getVectorShuffle(PairTy, dl, DAG.getBitcast(PairTy, Inp),
                             DAG.getUNDEF(PairTy), FoldedMask);
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, SingleTy, FoldedShuff,
                       DAG.getConstant(0, dl, MVT::i32));
  };

  auto getSourceInfo = [](SDValue V) -> std::optional<SubVectorInfo> {
    while (V.getOpcode() == ISD::BITCAST)
      V = V.getOperand(0);
    if (V.getOpcode() != ISD::EXTRACT_SUBVECTOR)
      return std::nullopt;
    return SubVectorInfo(V.getOperand(0),
                         !cast<ConstantSDNode>(V.getOperand(1))->isZero());
  };

  for (SDNode *N : Nodes) {
    if (N->getOpcode() != ISD::VECTOR_SHUFFLE)
      continue;
    EVT ResTy = N->getValueType(0);
    if (ResTy.getVectorElementType() != MVT::i8)
      continue;
    if (ResTy.getVectorNumElements() != HwLen)
      continue;

    SDValue V0 = N->getOperand(0);
    SDValue V1 = N->getOperand(1);
    if (V0.getOpcode() != ISD::VECTOR_SHUFFLE)
      continue;
    if (V1.getOpcode() != ISD::VECTOR_SHUFFLE)
      continue;
    if (V0.getValueType() != ResTy || V1.getValueType() != ResTy)
      continue;

    // Check if all operands of the two operand shuffles are extract_subvectors
    // from the same vector pair.
    auto V0A = getSourceInfo(V0.getOperand(0));
    if (!V0A.has_value())
      continue;
    auto V0B = getSourceInfo(V0.getOperand(1));
    if (!V0B.has_value() || V0B->Src != V0A->Src)
      continue;
    auto V1A = getSourceInfo(V1.getOperand(0));
    if (!V1A.has_value() || V1A->Src != V0A->Src)
      continue;
    auto V1B = getSourceInfo(V1.getOperand(1));
    if (!V1B.has_value() || V1B->Src != V0A->Src)
      continue;

    // The source must be a pair. This should be guaranteed here,
    // but check just in case.
    assert(V0A->Src.getValueType().getSizeInBits() == 16 * HwLen);

    MapType OpMap = {
        {V0.getOperand(0), V0A->HalfIdx * HwLen},
        {V0.getOperand(1), V0B->HalfIdx * HwLen},
        {V1.getOperand(0), V1A->HalfIdx * HwLen},
        {V1.getOperand(1), V1B->HalfIdx * HwLen},
    };
    SDValue NewS = fold3(SDValue(N, 0), V0A->Src, std::move(OpMap));
    ReplaceNode(N, NewS.getNode());
  }
}

void HexagonDAGToDAGISel::SelectHvxExtractSubvector(SDNode *N) {
  HvxSelector(*this, *CurDAG).selectExtractSubvector(N);
}

void HexagonDAGToDAGISel::SelectHvxShuffle(SDNode *N) {
  HvxSelector(*this, *CurDAG).selectShuffle(N);
}

void HexagonDAGToDAGISel::SelectHvxRor(SDNode *N) {
  HvxSelector(*this, *CurDAG).selectRor(N);
}

void HexagonDAGToDAGISel::SelectHvxVAlign(SDNode *N) {
  HvxSelector(*this, *CurDAG).selectVAlign(N);
}

void HexagonDAGToDAGISel::SelectV65GatherPred(SDNode *N) {
  const SDLoc &dl(N);
  SDValue Chain = N->getOperand(0);
  SDValue Address = N->getOperand(2);
  SDValue Predicate = N->getOperand(3);
  SDValue Base = N->getOperand(4);
  SDValue Modifier = N->getOperand(5);
  SDValue Offset = N->getOperand(6);
  SDValue ImmOperand = CurDAG->getTargetConstant(0, dl, MVT::i32);

  unsigned Opcode;
  unsigned IntNo = N->getConstantOperandVal(1);
  switch (IntNo) {
  default:
    llvm_unreachable("Unexpected HVX gather intrinsic.");
  case Intrinsic::hexagon_V6_vgathermhq:
  case Intrinsic::hexagon_V6_vgathermhq_128B:
    Opcode = Hexagon::V6_vgathermhq_pseudo;
    break;
  case Intrinsic::hexagon_V6_vgathermwq:
  case Intrinsic::hexagon_V6_vgathermwq_128B:
    Opcode = Hexagon::V6_vgathermwq_pseudo;
    break;
  case Intrinsic::hexagon_V6_vgathermhwq:
  case Intrinsic::hexagon_V6_vgathermhwq_128B:
    Opcode = Hexagon::V6_vgathermhwq_pseudo;
    break;
  }

  SDVTList VTs = CurDAG->getVTList(MVT::Other);
  SDValue Ops[] = { Address, ImmOperand,
                    Predicate, Base, Modifier, Offset, Chain };
  SDNode *Result = CurDAG->getMachineNode(Opcode, dl, VTs, Ops);

  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Result), {MemOp});

  ReplaceNode(N, Result);
}

void HexagonDAGToDAGISel::SelectV65Gather(SDNode *N) {
  const SDLoc &dl(N);
  SDValue Chain = N->getOperand(0);
  SDValue Address = N->getOperand(2);
  SDValue Base = N->getOperand(3);
  SDValue Modifier = N->getOperand(4);
  SDValue Offset = N->getOperand(5);
  SDValue ImmOperand = CurDAG->getTargetConstant(0, dl, MVT::i32);

  unsigned Opcode;
  unsigned IntNo = N->getConstantOperandVal(1);
  switch (IntNo) {
  default:
    llvm_unreachable("Unexpected HVX gather intrinsic.");
  case Intrinsic::hexagon_V6_vgathermh:
  case Intrinsic::hexagon_V6_vgathermh_128B:
    Opcode = Hexagon::V6_vgathermh_pseudo;
    break;
  case Intrinsic::hexagon_V6_vgathermw:
  case Intrinsic::hexagon_V6_vgathermw_128B:
    Opcode = Hexagon::V6_vgathermw_pseudo;
    break;
  case Intrinsic::hexagon_V6_vgathermhw:
  case Intrinsic::hexagon_V6_vgathermhw_128B:
    Opcode = Hexagon::V6_vgathermhw_pseudo;
    break;
  }

  SDVTList VTs = CurDAG->getVTList(MVT::Other);
  SDValue Ops[] = { Address, ImmOperand, Base, Modifier, Offset, Chain };
  SDNode *Result = CurDAG->getMachineNode(Opcode, dl, VTs, Ops);

  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Result), {MemOp});

  ReplaceNode(N, Result);
}

void HexagonDAGToDAGISel::SelectHVXDualOutput(SDNode *N) {
  unsigned IID = N->getConstantOperandVal(0);
  SDNode *Result;
  switch (IID) {
  case Intrinsic::hexagon_V6_vaddcarry: {
    std::array<SDValue, 3> Ops = {
        {N->getOperand(1), N->getOperand(2), N->getOperand(3)}};
    SDVTList VTs = CurDAG->getVTList(MVT::v16i32, MVT::v64i1);
    Result = CurDAG->getMachineNode(Hexagon::V6_vaddcarry, SDLoc(N), VTs, Ops);
    break;
  }
  case Intrinsic::hexagon_V6_vaddcarry_128B: {
    std::array<SDValue, 3> Ops = {
        {N->getOperand(1), N->getOperand(2), N->getOperand(3)}};
    SDVTList VTs = CurDAG->getVTList(MVT::v32i32, MVT::v128i1);
    Result = CurDAG->getMachineNode(Hexagon::V6_vaddcarry, SDLoc(N), VTs, Ops);
    break;
  }
  case Intrinsic::hexagon_V6_vsubcarry: {
    std::array<SDValue, 3> Ops = {
        {N->getOperand(1), N->getOperand(2), N->getOperand(3)}};
    SDVTList VTs = CurDAG->getVTList(MVT::v16i32, MVT::v64i1);
    Result = CurDAG->getMachineNode(Hexagon::V6_vsubcarry, SDLoc(N), VTs, Ops);
    break;
  }
  case Intrinsic::hexagon_V6_vsubcarry_128B: {
    std::array<SDValue, 3> Ops = {
        {N->getOperand(1), N->getOperand(2), N->getOperand(3)}};
    SDVTList VTs = CurDAG->getVTList(MVT::v32i32, MVT::v128i1);
    Result = CurDAG->getMachineNode(Hexagon::V6_vsubcarry, SDLoc(N), VTs, Ops);
    break;
  }
  default:
    llvm_unreachable("Unexpected HVX dual output intrinsic.");
  }
  ReplaceUses(N, Result);
  ReplaceUses(SDValue(N, 0), SDValue(Result, 0));
  ReplaceUses(SDValue(N, 1), SDValue(Result, 1));
  CurDAG->RemoveDeadNode(N);
}
