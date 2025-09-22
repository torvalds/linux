//===-- PerfectShuffle.cpp - Perfect Shuffle Generator --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file computes an optimal sequence of instructions for doing all shuffles
// of two 4-element vectors.  With a release build and when configured to emit
// an altivec instruction table, this takes about 30s to run on a 2.7Ghz
// PowerPC G5.
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

#define GENERATE_NEON
#define GENERATE_NEON_INS

struct Operator;

// Masks are 4-nibble hex numbers.  Values 0-7 in any nibble means that it takes
// an element from that value of the input vectors.  A value of 8 means the
// entry is undefined.

// Mask manipulation functions.
static inline unsigned short MakeMask(unsigned V0, unsigned V1,
                                      unsigned V2, unsigned V3) {
  return (V0 << (3*4)) | (V1 << (2*4)) | (V2 << (1*4)) | (V3 << (0*4));
}

/// getMaskElt - Return element N of the specified mask.
static unsigned getMaskElt(unsigned Mask, unsigned Elt) {
  return (Mask >> ((3-Elt)*4)) & 0xF;
}

static unsigned setMaskElt(unsigned Mask, unsigned Elt, unsigned NewVal) {
  unsigned FieldShift = ((3-Elt)*4);
  return (Mask & ~(0xF << FieldShift)) | (NewVal << FieldShift);
}

// Reject elements where the values are 9-15.
static bool isValidMask(unsigned short Mask) {
  unsigned short UndefBits = Mask & 0x8888;
  return (Mask & ((UndefBits >> 1)|(UndefBits>>2)|(UndefBits>>3))) == 0;
}

/// hasUndefElements - Return true if any of the elements in the mask are undefs
///
static bool hasUndefElements(unsigned short Mask) {
  return (Mask & 0x8888) != 0;
}

/// isOnlyLHSMask - Return true if this mask only refers to its LHS, not
/// including undef values..
static bool isOnlyLHSMask(unsigned short Mask) {
  return (Mask & 0x4444) == 0;
}

/// getLHSOnlyMask - Given a mask that refers to its LHS and RHS, modify it to
/// refer to the LHS only (for when one argument value is passed into the same
/// function twice).
#if 0
static unsigned short getLHSOnlyMask(unsigned short Mask) {
  return Mask & 0xBBBB;  // Keep only LHS and Undefs.
}
#endif

/// getCompressedMask - Turn a 16-bit uncompressed mask (where each elt uses 4
/// bits) into a compressed 13-bit mask, where each elt is multiplied by 9.
static unsigned getCompressedMask(unsigned short Mask) {
  return getMaskElt(Mask, 0)*9*9*9 + getMaskElt(Mask, 1)*9*9 +
         getMaskElt(Mask, 2)*9     + getMaskElt(Mask, 3);
}

static void PrintMask(unsigned i, std::ostream &OS) {
  OS << "<" << (char)(getMaskElt(i, 0) == 8 ? 'u' : ('0'+getMaskElt(i, 0)))
     << "," << (char)(getMaskElt(i, 1) == 8 ? 'u' : ('0'+getMaskElt(i, 1)))
     << "," << (char)(getMaskElt(i, 2) == 8 ? 'u' : ('0'+getMaskElt(i, 2)))
     << "," << (char)(getMaskElt(i, 3) == 8 ? 'u' : ('0'+getMaskElt(i, 3)))
     << ">";
}

/// ShuffleVal - This represents a shufflevector operation.
struct ShuffleVal {
  Operator *Op;   // The Operation used to generate this value.
  unsigned Cost;  // Number of instrs used to generate this value.
  unsigned short Arg0, Arg1;  // Input operands for this value.

  ShuffleVal() : Cost(1000000) {}
};


/// ShufTab - This is the actual shuffle table that we are trying to generate.
///
static ShuffleVal ShufTab[65536];

/// TheOperators - All of the operators that this target supports.
static std::vector<Operator*> TheOperators;

/// Operator - This is a vector operation that is available for use.
struct Operator {
  const char *Name;
  unsigned short ShuffleMask;
  unsigned short OpNum;
  unsigned Cost;

  Operator(unsigned short shufflemask, const char *name, unsigned opnum,
           unsigned cost = 1)
    :  Name(name), ShuffleMask(shufflemask), OpNum(opnum),Cost(cost) {
    TheOperators.push_back(this);
  }
  ~Operator() {
    assert(TheOperators.back() == this);
    TheOperators.pop_back();
  }

  bool isOnlyLHSOperator() const {
    return isOnlyLHSMask(ShuffleMask);
  }

  const char *getName() const { return Name; }
  unsigned getCost() const { return Cost; }

  unsigned short getTransformedMask(unsigned short LHSMask, unsigned RHSMask) {
    // Extract the elements from LHSMask and RHSMask, as appropriate.
    unsigned Result = 0;
    for (unsigned i = 0; i != 4; ++i) {
      unsigned SrcElt = (ShuffleMask >> (4*i)) & 0xF;
      unsigned ResElt;
      if (SrcElt < 4)
        ResElt = getMaskElt(LHSMask, SrcElt);
      else if (SrcElt < 8)
        ResElt = getMaskElt(RHSMask, SrcElt-4);
      else {
        assert(SrcElt == 8 && "Bad src elt!");
        ResElt = 8;
      }
      Result |= ResElt << (4*i);
    }
    return Result;
  }
};

#ifdef GENERATE_NEON_INS
// Special case "insert" op identifier used below
static Operator InsOp(0, "ins", 15, 1);
#endif

static const char *getZeroCostOpName(unsigned short Op) {
  if (ShufTab[Op].Arg0 == 0x0123)
    return "LHS";
  else if (ShufTab[Op].Arg0 == 0x4567)
    return "RHS";
  else {
    assert(0 && "bad zero cost operation");
    abort();
  }
}

static void PrintOperation(unsigned ValNo, unsigned short Vals[]) {
  unsigned short ThisOp = Vals[ValNo];
  std::cerr << "t" << ValNo;
  PrintMask(ThisOp, std::cerr);
  std::cerr << " = " << ShufTab[ThisOp].Op->getName() << "(";

  if (ShufTab[ShufTab[ThisOp].Arg0].Cost == 0) {
    std::cerr << getZeroCostOpName(ShufTab[ThisOp].Arg0);
    PrintMask(ShufTab[ThisOp].Arg0, std::cerr);
  } else {
    // Figure out what tmp # it is.
    for (unsigned i = 0; ; ++i)
      if (Vals[i] == ShufTab[ThisOp].Arg0) {
        std::cerr << "t" << i;
        break;
      }
  }

#ifdef GENERATE_NEON_INS
  if (ShufTab[ThisOp].Op == &InsOp) {
    std::cerr << ", lane " << ShufTab[ThisOp].Arg1;
  } else
#endif
  if (!ShufTab[Vals[ValNo]].Op->isOnlyLHSOperator()) {
    std::cerr << ", ";
    if (ShufTab[ShufTab[ThisOp].Arg1].Cost == 0) {
      std::cerr << getZeroCostOpName(ShufTab[ThisOp].Arg1);
      PrintMask(ShufTab[ThisOp].Arg1, std::cerr);
    } else {
      // Figure out what tmp # it is.
      for (unsigned i = 0; ; ++i)
        if (Vals[i] == ShufTab[ThisOp].Arg1) {
          std::cerr << "t" << i;
          break;
        }
    }
  }
  std::cerr << ")  ";
}

static unsigned getNumEntered() {
  unsigned Count = 0;
  for (unsigned i = 0; i != 65536; ++i)
    Count += ShufTab[i].Cost < 100;
  return Count;
}

static void EvaluateOps(unsigned short Elt, unsigned short Vals[],
                        unsigned &NumVals) {
  if (ShufTab[Elt].Cost == 0) return;
#ifdef GENERATE_NEON_INS
  if (ShufTab[Elt].Op == &InsOp) {
    EvaluateOps(ShufTab[Elt].Arg0, Vals, NumVals);
    Vals[NumVals++] = Elt;
    return;
  }
#endif

  // If this value has already been evaluated, it is free.  FIXME: match undefs.
  for (unsigned i = 0, e = NumVals; i != e; ++i)
    if (Vals[i] == Elt) return;

  // Otherwise, get the operands of the value, then add it.
  unsigned Arg0 = ShufTab[Elt].Arg0, Arg1 = ShufTab[Elt].Arg1;
  if (ShufTab[Arg0].Cost)
    EvaluateOps(Arg0, Vals, NumVals);
  if (Arg0 != Arg1 && ShufTab[Arg1].Cost)
    EvaluateOps(Arg1, Vals, NumVals);

  Vals[NumVals++] = Elt;
}


int main() {
  // Seed the table with accesses to the LHS and RHS.
  ShufTab[0x0123].Cost = 0;
  ShufTab[0x0123].Op = nullptr;
  ShufTab[0x0123].Arg0 = 0x0123;
  ShufTab[0x4567].Cost = 0;
  ShufTab[0x4567].Op = nullptr;
  ShufTab[0x4567].Arg0 = 0x4567;

  // Seed the first-level of shuffles, shuffles whose inputs are the input to
  // the vectorshuffle operation.
  bool MadeChange = true;
  unsigned OpCount = 0;
  while (MadeChange) {
    MadeChange = false;
    ++OpCount;
    std::cerr << "Starting iteration #" << OpCount << " with "
              << getNumEntered() << " entries established.\n";

    // Scan the table for two reasons: First, compute the maximum cost of any
    // operation left in the table.  Second, make sure that values with undefs
    // have the cheapest alternative that they match.
    unsigned MaxCost = ShufTab[0].Cost;
    for (unsigned i = 1; i != 0x8889; ++i) {
      if (!isValidMask(i)) continue;
      if (ShufTab[i].Cost > MaxCost)
        MaxCost = ShufTab[i].Cost;

      // If this value has an undef, make it be computed the cheapest possible
      // way of any of the things that it matches.
      if (hasUndefElements(i)) {
        // This code is a little bit tricky, so here's the idea: consider some
        // permutation, like 7u4u.  To compute the lowest cost for 7u4u, we
        // need to take the minimum cost of all of 7[0-8]4[0-8], 81 entries.  If
        // there are 3 undefs, the number rises to 729 entries we have to scan,
        // and for the 4 undef case, we have to scan the whole table.
        //
        // Instead of doing this huge amount of scanning, we process the table
        // entries *in order*, and use the fact that 'u' is 8, larger than any
        // valid index.  Given an entry like 7u4u then, we only need to scan
        // 7[0-7]4u - 8 entries.  We can get away with this, because we already
        // know that each of 704u, 714u, 724u, etc contain the minimum value of
        // all of the 704[0-8], 714[0-8] and 724[0-8] entries respectively.
        unsigned UndefIdx;
        if (i & 0x8000)
          UndefIdx = 0;
        else if (i & 0x0800)
          UndefIdx = 1;
        else if (i & 0x0080)
          UndefIdx = 2;
        else if (i & 0x0008)
          UndefIdx = 3;
        else
          abort();

        unsigned MinVal  = i;
        unsigned MinCost = ShufTab[i].Cost;

        // Scan the 8 entries.
        for (unsigned j = 0; j != 8; ++j) {
          unsigned NewElt = setMaskElt(i, UndefIdx, j);
          if (ShufTab[NewElt].Cost < MinCost) {
            MinCost = ShufTab[NewElt].Cost;
            MinVal = NewElt;
          }
        }

        // If we found something cheaper than what was here before, use it.
        if (i != MinVal) {
          MadeChange = true;
          ShufTab[i] = ShufTab[MinVal];
        }
      }
#ifdef GENERATE_NEON_INS
      else {
        // Similarly, if we take the mask (eg 3,6,1,0) and take the cost with
        // undef for each lane (eg u,6,1,0 or 3,u,1,0 etc), we can use a single
        // lane insert to fixup the result.
        for (unsigned LaneIdx = 0; LaneIdx < 4; LaneIdx++) {
          if (getMaskElt(i, LaneIdx) == 8)
            continue;
          unsigned NewElt = setMaskElt(i, LaneIdx, 8);
          if (ShufTab[NewElt].Cost + 1 < ShufTab[i].Cost) {
            MadeChange = true;
            ShufTab[i].Cost = ShufTab[NewElt].Cost + 1;
            ShufTab[i].Op = &InsOp;
            ShufTab[i].Arg0 = NewElt;
            ShufTab[i].Arg1 = LaneIdx;
          }
        }

        // Similar idea for using a D register mov, masking out 2 lanes to undef
        for (unsigned LaneIdx = 0; LaneIdx < 4; LaneIdx += 2) {
          unsigned Ln0 = getMaskElt(i, LaneIdx);
          unsigned Ln1 = getMaskElt(i, LaneIdx + 1);
          if ((Ln0 == 0 && Ln1 == 1) || (Ln0 == 2 && Ln1 == 3) ||
              (Ln0 == 4 && Ln1 == 5) || (Ln0 == 6 && Ln1 == 7)) {
            unsigned NewElt = setMaskElt(i, LaneIdx, 8);
            NewElt = setMaskElt(NewElt, LaneIdx + 1, 8);
            if (ShufTab[NewElt].Cost + 1 < ShufTab[i].Cost) {
              MadeChange = true;
              ShufTab[i].Cost = ShufTab[NewElt].Cost + 1;
              ShufTab[i].Op = &InsOp;
              ShufTab[i].Arg0 = NewElt;
              ShufTab[i].Arg1 = (LaneIdx >> 1) | 0x4;
            }
          }
        }
      }
#endif
    }

    for (unsigned LHS = 0; LHS != 0x8889; ++LHS) {
      if (!isValidMask(LHS)) continue;
      if (ShufTab[LHS].Cost > 1000) continue;

      // If nothing involving this operand could possibly be cheaper than what
      // we already have, don't consider it.
      if (ShufTab[LHS].Cost + 1 >= MaxCost)
        continue;

      for (unsigned opnum = 0, e = TheOperators.size(); opnum != e; ++opnum) {
        Operator *Op = TheOperators[opnum];
#ifdef GENERATE_NEON_INS
        if (Op == &InsOp)
          continue;
#endif

        // Evaluate op(LHS,LHS)
        unsigned ResultMask = Op->getTransformedMask(LHS, LHS);

        unsigned Cost = ShufTab[LHS].Cost + Op->getCost();
        if (Cost < ShufTab[ResultMask].Cost) {
          ShufTab[ResultMask].Cost = Cost;
          ShufTab[ResultMask].Op = Op;
          ShufTab[ResultMask].Arg0 = LHS;
          ShufTab[ResultMask].Arg1 = LHS;
          MadeChange = true;
        }

        // If this is a two input instruction, include the op(x,y) cases.  If
        // this is a one input instruction, skip this.
        if (Op->isOnlyLHSOperator()) continue;

        for (unsigned RHS = 0; RHS != 0x8889; ++RHS) {
          if (!isValidMask(RHS)) continue;
          if (ShufTab[RHS].Cost > 1000) continue;

          // If nothing involving this operand could possibly be cheaper than
          // what we already have, don't consider it.
          if (ShufTab[RHS].Cost + 1 >= MaxCost)
            continue;


          // Evaluate op(LHS,RHS)
          unsigned ResultMask = Op->getTransformedMask(LHS, RHS);

          if (ShufTab[ResultMask].Cost <= OpCount ||
              ShufTab[ResultMask].Cost <= ShufTab[LHS].Cost ||
              ShufTab[ResultMask].Cost <= ShufTab[RHS].Cost)
            continue;

          // Figure out the cost to evaluate this, knowing that CSE's only need
          // to be evaluated once.
          unsigned short Vals[30];
          unsigned NumVals = 0;
          EvaluateOps(LHS, Vals, NumVals);
          EvaluateOps(RHS, Vals, NumVals);

          unsigned Cost = NumVals + Op->getCost();
          if (Cost < ShufTab[ResultMask].Cost) {
            ShufTab[ResultMask].Cost = Cost;
            ShufTab[ResultMask].Op = Op;
            ShufTab[ResultMask].Arg0 = LHS;
            ShufTab[ResultMask].Arg1 = RHS;
            MadeChange = true;
          }
        }
      }
    }
  }

  std::cerr << "Finished Table has " << getNumEntered()
            << " entries established.\n";

  unsigned CostArray[10] = { 0 };

  // Compute a cost histogram.
  for (unsigned i = 0; i != 65536; ++i) {
    if (!isValidMask(i)) continue;
    if (ShufTab[i].Cost > 9)
      ++CostArray[9];
    else
      ++CostArray[ShufTab[i].Cost];
  }

  for (unsigned i = 0; i != 9; ++i)
    if (CostArray[i])
      std::cout << "// " << CostArray[i] << " entries have cost " << i << "\n";
  if (CostArray[9])
    std::cout << "// " << CostArray[9] << " entries have higher cost!\n";


  // Build up the table to emit.
  std::cout << "\n// This table is 6561*4 = 26244 bytes in size.\n";
  std::cout << "static const unsigned PerfectShuffleTable[6561+1] = {\n";

  for (unsigned i = 0; i != 0x8889; ++i) {
    if (!isValidMask(i)) continue;

    // CostSat - The cost of this operation saturated to two bits.
    unsigned CostSat = ShufTab[i].Cost;
    if (CostSat > 4) CostSat = 4;
    if (CostSat == 0) CostSat = 1;
    --CostSat;  // Cost is now between 0-3.

    unsigned OpNum = ShufTab[i].Op ? ShufTab[i].Op->OpNum : 0;
    assert(OpNum < 16 && "Too few bits to encode operation!");

    unsigned LHS = getCompressedMask(ShufTab[i].Arg0);
    unsigned RHS = getCompressedMask(ShufTab[i].Arg1);

    // Encode this as 2 bits of saturated cost, 4 bits of opcodes, 13 bits of
    // LHS, and 13 bits of RHS = 32 bits.
    unsigned Val = (CostSat << 30) | (OpNum << 26) | (LHS << 13) | RHS;

    std::cout << "  " << std::setw(10) << Val << "U, // ";
    PrintMask(i, std::cout);
    std::cout << ": Cost " << ShufTab[i].Cost;
    std::cout << " " << (ShufTab[i].Op ? ShufTab[i].Op->getName() : "copy");
    std::cout << " ";
    if (ShufTab[ShufTab[i].Arg0].Cost == 0) {
      std::cout << getZeroCostOpName(ShufTab[i].Arg0);
    } else {
      PrintMask(ShufTab[i].Arg0, std::cout);
    }

    if (ShufTab[i].Op && !ShufTab[i].Op->isOnlyLHSOperator()) {
      std::cout << ", ";
      if (ShufTab[ShufTab[i].Arg1].Cost == 0) {
        std::cout << getZeroCostOpName(ShufTab[i].Arg1);
      } else {
        PrintMask(ShufTab[i].Arg1, std::cout);
      }
    }
#ifdef GENERATE_NEON_INS
    else if (ShufTab[i].Op == &InsOp) {
      std::cout << ", lane " << ShufTab[i].Arg1;
    }
#endif

    std::cout << "\n";
  }
  std::cout << "  0\n};\n";

  if (false) {
    // Print out the table.
    for (unsigned i = 0; i != 0x8889; ++i) {
      if (!isValidMask(i)) continue;
      if (ShufTab[i].Cost < 1000) {
        PrintMask(i, std::cerr);
        std::cerr << " - Cost " << ShufTab[i].Cost << " - ";

        unsigned short Vals[30];
        unsigned NumVals = 0;
        EvaluateOps(i, Vals, NumVals);

        for (unsigned j = 0, e = NumVals; j != e; ++j)
          PrintOperation(j, Vals);
        std::cerr << "\n";
      }
    }
  }
}


#ifdef GENERATE_ALTIVEC

///===---------------------------------------------------------------------===//
/// The altivec instruction definitions.  This is the altivec-specific part of
/// this file.
///===---------------------------------------------------------------------===//

// Note that the opcode numbers here must match those in the PPC backend.
enum {
  OP_COPY = 0,   // Copy, used for things like <u,u,u,3> to say it is <0,1,2,3>
  OP_VMRGHW,
  OP_VMRGLW,
  OP_VSPLTISW0,
  OP_VSPLTISW1,
  OP_VSPLTISW2,
  OP_VSPLTISW3,
  OP_VSLDOI4,
  OP_VSLDOI8,
  OP_VSLDOI12
};

struct vmrghw : public Operator {
  vmrghw() : Operator(0x0415, "vmrghw", OP_VMRGHW) {}
} the_vmrghw;

struct vmrglw : public Operator {
  vmrglw() : Operator(0x2637, "vmrglw", OP_VMRGLW) {}
} the_vmrglw;

template<unsigned Elt>
struct vspltisw : public Operator {
  vspltisw(const char *N, unsigned Opc)
    : Operator(MakeMask(Elt, Elt, Elt, Elt), N, Opc) {}
};

vspltisw<0> the_vspltisw0("vspltisw0", OP_VSPLTISW0);
vspltisw<1> the_vspltisw1("vspltisw1", OP_VSPLTISW1);
vspltisw<2> the_vspltisw2("vspltisw2", OP_VSPLTISW2);
vspltisw<3> the_vspltisw3("vspltisw3", OP_VSPLTISW3);

template<unsigned N>
struct vsldoi : public Operator {
  vsldoi(const char *Name, unsigned Opc)
    : Operator(MakeMask(N&7, (N+1)&7, (N+2)&7, (N+3)&7), Name, Opc) {
  }
};

vsldoi<1> the_vsldoi1("vsldoi4" , OP_VSLDOI4);
vsldoi<2> the_vsldoi2("vsldoi8" , OP_VSLDOI8);
vsldoi<3> the_vsldoi3("vsldoi12", OP_VSLDOI12);

#endif

#ifdef GENERATE_NEON
enum {
  OP_COPY = 0,   // Copy, used for things like <u,u,u,3> to say it is <0,1,2,3>
  OP_VREV,
  OP_VDUP0,
  OP_VDUP1,
  OP_VDUP2,
  OP_VDUP3,
  OP_VEXT1,
  OP_VEXT2,
  OP_VEXT3,
  OP_VUZPL, // VUZP, left result
  OP_VUZPR, // VUZP, right result
  OP_VZIPL, // VZIP, left result
  OP_VZIPR, // VZIP, right result
  OP_VTRNL, // VTRN, left result
  OP_VTRNR  // VTRN, right result
};

struct vrev : public Operator {
  vrev() : Operator(0x1032, "vrev", OP_VREV) {}
} the_vrev;

template<unsigned Elt>
struct vdup : public Operator {
  vdup(const char *N, unsigned Opc)
    : Operator(MakeMask(Elt, Elt, Elt, Elt), N, Opc) {}
};

vdup<0> the_vdup0("vdup0", OP_VDUP0);
vdup<1> the_vdup1("vdup1", OP_VDUP1);
vdup<2> the_vdup2("vdup2", OP_VDUP2);
vdup<3> the_vdup3("vdup3", OP_VDUP3);

template<unsigned N>
struct vext : public Operator {
  vext(const char *Name, unsigned Opc)
    : Operator(MakeMask(N&7, (N+1)&7, (N+2)&7, (N+3)&7), Name, Opc) {
  }
};

vext<1> the_vext1("vext1", OP_VEXT1);
vext<2> the_vext2("vext2", OP_VEXT2);
vext<3> the_vext3("vext3", OP_VEXT3);

struct vuzpl : public Operator {
  vuzpl() : Operator(0x0246, "vuzpl", OP_VUZPL, 1) {}
} the_vuzpl;

struct vuzpr : public Operator {
  vuzpr() : Operator(0x1357, "vuzpr", OP_VUZPR, 1) {}
} the_vuzpr;

struct vzipl : public Operator {
  vzipl() : Operator(0x0415, "vzipl", OP_VZIPL, 1) {}
} the_vzipl;

struct vzipr : public Operator {
  vzipr() : Operator(0x2637, "vzipr", OP_VZIPR, 1) {}
} the_vzipr;

struct vtrnl : public Operator {
  vtrnl() : Operator(0x0426, "vtrnl", OP_VTRNL, 1) {}
} the_vtrnl;

struct vtrnr : public Operator {
  vtrnr() : Operator(0x1537, "vtrnr", OP_VTRNR, 1) {}
} the_vtrnr;

#endif
