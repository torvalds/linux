//===-- HexagonISelDAGToDAG.cpp - A dag to dag inst selector for Hexagon --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the Hexagon target.
//
//===----------------------------------------------------------------------===//

#include "HexagonISelDAGToDAG.h"
#include "Hexagon.h"
#include "HexagonISelLowering.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonTargetMachine.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsHexagon.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "hexagon-isel"
#define PASS_NAME "Hexagon DAG->DAG Pattern Instruction Selection"

static
cl::opt<bool>
EnableAddressRebalancing("isel-rebalance-addr", cl::Hidden, cl::init(true),
  cl::desc("Rebalance address calculation trees to improve "
          "instruction selection"));

// Rebalance only if this allows e.g. combining a GA with an offset or
// factoring out a shift.
static
cl::opt<bool>
RebalanceOnlyForOptimizations("rebalance-only-opt", cl::Hidden, cl::init(false),
  cl::desc("Rebalance address tree only if this allows optimizations"));

static
cl::opt<bool>
RebalanceOnlyImbalancedTrees("rebalance-only-imbal", cl::Hidden,
  cl::init(false), cl::desc("Rebalance address tree only if it is imbalanced"));

static cl::opt<bool> CheckSingleUse("hexagon-isel-su", cl::Hidden,
  cl::init(true), cl::desc("Enable checking of SDNode's single-use status"));

//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

#define GET_DAGISEL_BODY HexagonDAGToDAGISel
#include "HexagonGenDAGISel.inc"

namespace llvm {
/// createHexagonISelDag - This pass converts a legalized DAG into a
/// Hexagon-specific DAG, ready for instruction scheduling.
FunctionPass *createHexagonISelDag(HexagonTargetMachine &TM,
                                   CodeGenOptLevel OptLevel) {
  return new HexagonDAGToDAGISelLegacy(TM, OptLevel);
}
}

HexagonDAGToDAGISelLegacy::HexagonDAGToDAGISelLegacy(HexagonTargetMachine &tm,
                                                     CodeGenOptLevel OptLevel)
    : SelectionDAGISelLegacy(
          ID, std::make_unique<HexagonDAGToDAGISel>(tm, OptLevel)) {}

char HexagonDAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(HexagonDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

void HexagonDAGToDAGISel::SelectIndexedLoad(LoadSDNode *LD, const SDLoc &dl) {
  SDValue Chain = LD->getChain();
  SDValue Base = LD->getBasePtr();
  SDValue Offset = LD->getOffset();
  int32_t Inc = cast<ConstantSDNode>(Offset.getNode())->getSExtValue();
  EVT LoadedVT = LD->getMemoryVT();
  unsigned Opcode = 0;

  // Check for zero extended loads. Treat any-extend loads as zero extended
  // loads.
  ISD::LoadExtType ExtType = LD->getExtensionType();
  bool IsZeroExt = (ExtType == ISD::ZEXTLOAD || ExtType == ISD::EXTLOAD);
  bool IsValidInc = HII->isValidAutoIncImm(LoadedVT, Inc);

  assert(LoadedVT.isSimple());
  switch (LoadedVT.getSimpleVT().SimpleTy) {
  case MVT::i8:
    if (IsZeroExt)
      Opcode = IsValidInc ? Hexagon::L2_loadrub_pi : Hexagon::L2_loadrub_io;
    else
      Opcode = IsValidInc ? Hexagon::L2_loadrb_pi : Hexagon::L2_loadrb_io;
    break;
  case MVT::i16:
    if (IsZeroExt)
      Opcode = IsValidInc ? Hexagon::L2_loadruh_pi : Hexagon::L2_loadruh_io;
    else
      Opcode = IsValidInc ? Hexagon::L2_loadrh_pi : Hexagon::L2_loadrh_io;
    break;
  case MVT::i32:
  case MVT::f32:
  case MVT::v2i16:
  case MVT::v4i8:
    Opcode = IsValidInc ? Hexagon::L2_loadri_pi : Hexagon::L2_loadri_io;
    break;
  case MVT::i64:
  case MVT::f64:
  case MVT::v2i32:
  case MVT::v4i16:
  case MVT::v8i8:
    Opcode = IsValidInc ? Hexagon::L2_loadrd_pi : Hexagon::L2_loadrd_io;
    break;
  case MVT::v64i8:
  case MVT::v32i16:
  case MVT::v16i32:
  case MVT::v8i64:
  case MVT::v128i8:
  case MVT::v64i16:
  case MVT::v32i32:
  case MVT::v16i64:
    if (isAlignedMemNode(LD)) {
      if (LD->isNonTemporal())
        Opcode = IsValidInc ? Hexagon::V6_vL32b_nt_pi : Hexagon::V6_vL32b_nt_ai;
      else
        Opcode = IsValidInc ? Hexagon::V6_vL32b_pi : Hexagon::V6_vL32b_ai;
    } else {
      Opcode = IsValidInc ? Hexagon::V6_vL32Ub_pi : Hexagon::V6_vL32Ub_ai;
    }
    break;
  default:
    llvm_unreachable("Unexpected memory type in indexed load");
  }

  SDValue IncV = CurDAG->getTargetConstant(Inc, dl, MVT::i32);
  MachineMemOperand *MemOp = LD->getMemOperand();

  auto getExt64 = [this,ExtType] (MachineSDNode *N, const SDLoc &dl)
        -> MachineSDNode* {
    if (ExtType == ISD::ZEXTLOAD || ExtType == ISD::EXTLOAD) {
      SDValue Zero = CurDAG->getTargetConstant(0, dl, MVT::i32);
      return CurDAG->getMachineNode(Hexagon::A4_combineir, dl, MVT::i64,
                                    Zero, SDValue(N, 0));
    }
    if (ExtType == ISD::SEXTLOAD)
      return CurDAG->getMachineNode(Hexagon::A2_sxtw, dl, MVT::i64,
                                    SDValue(N, 0));
    return N;
  };

  //                  Loaded value   Next address   Chain
  SDValue From[3] = { SDValue(LD,0), SDValue(LD,1), SDValue(LD,2) };
  SDValue To[3];

  EVT ValueVT = LD->getValueType(0);
  if (ValueVT == MVT::i64 && ExtType != ISD::NON_EXTLOAD) {
    // A load extending to i64 will actually produce i32, which will then
    // need to be extended to i64.
    assert(LoadedVT.getSizeInBits() <= 32);
    ValueVT = MVT::i32;
  }

  if (IsValidInc) {
    MachineSDNode *L = CurDAG->getMachineNode(Opcode, dl, ValueVT,
                                              MVT::i32, MVT::Other, Base,
                                              IncV, Chain);
    CurDAG->setNodeMemRefs(L, {MemOp});
    To[1] = SDValue(L, 1); // Next address.
    To[2] = SDValue(L, 2); // Chain.
    // Handle special case for extension to i64.
    if (LD->getValueType(0) == MVT::i64)
      L = getExt64(L, dl);
    To[0] = SDValue(L, 0); // Loaded (extended) value.
  } else {
    SDValue Zero = CurDAG->getTargetConstant(0, dl, MVT::i32);
    MachineSDNode *L = CurDAG->getMachineNode(Opcode, dl, ValueVT, MVT::Other,
                                              Base, Zero, Chain);
    CurDAG->setNodeMemRefs(L, {MemOp});
    To[2] = SDValue(L, 1); // Chain.
    MachineSDNode *A = CurDAG->getMachineNode(Hexagon::A2_addi, dl, MVT::i32,
                                              Base, IncV);
    To[1] = SDValue(A, 0); // Next address.
    // Handle special case for extension to i64.
    if (LD->getValueType(0) == MVT::i64)
      L = getExt64(L, dl);
    To[0] = SDValue(L, 0); // Loaded (extended) value.
  }
  ReplaceUses(From, To, 3);
  CurDAG->RemoveDeadNode(LD);
}

MachineSDNode *HexagonDAGToDAGISel::LoadInstrForLoadIntrinsic(SDNode *IntN) {
  if (IntN->getOpcode() != ISD::INTRINSIC_W_CHAIN)
    return nullptr;

  SDLoc dl(IntN);
  unsigned IntNo = IntN->getConstantOperandVal(1);

  static std::map<unsigned,unsigned> LoadPciMap = {
    { Intrinsic::hexagon_circ_ldb,  Hexagon::L2_loadrb_pci  },
    { Intrinsic::hexagon_circ_ldub, Hexagon::L2_loadrub_pci },
    { Intrinsic::hexagon_circ_ldh,  Hexagon::L2_loadrh_pci  },
    { Intrinsic::hexagon_circ_lduh, Hexagon::L2_loadruh_pci },
    { Intrinsic::hexagon_circ_ldw,  Hexagon::L2_loadri_pci  },
    { Intrinsic::hexagon_circ_ldd,  Hexagon::L2_loadrd_pci  },
  };
  auto FLC = LoadPciMap.find(IntNo);
  if (FLC != LoadPciMap.end()) {
    EVT ValTy = (IntNo == Intrinsic::hexagon_circ_ldd) ? MVT::i64 : MVT::i32;
    EVT RTys[] = { ValTy, MVT::i32, MVT::Other };
    // Operands: { Base, Increment, Modifier, Chain }
    auto Inc = cast<ConstantSDNode>(IntN->getOperand(5));
    SDValue I = CurDAG->getTargetConstant(Inc->getSExtValue(), dl, MVT::i32);
    MachineSDNode *Res = CurDAG->getMachineNode(FLC->second, dl, RTys,
          { IntN->getOperand(2), I, IntN->getOperand(4),
            IntN->getOperand(0) });
    return Res;
  }

  return nullptr;
}

SDNode *HexagonDAGToDAGISel::StoreInstrForLoadIntrinsic(MachineSDNode *LoadN,
      SDNode *IntN) {
  // The "LoadN" is just a machine load instruction. The intrinsic also
  // involves storing it. Generate an appropriate store to the location
  // given in the intrinsic's operand(3).
  uint64_t F = HII->get(LoadN->getMachineOpcode()).TSFlags;
  unsigned SizeBits = (F >> HexagonII::MemAccessSizePos) &
                      HexagonII::MemAccesSizeMask;
  unsigned Size = 1U << (SizeBits-1);

  SDLoc dl(IntN);
  MachinePointerInfo PI;
  SDValue TS;
  SDValue Loc = IntN->getOperand(3);

  if (Size >= 4)
    TS = CurDAG->getStore(SDValue(LoadN, 2), dl, SDValue(LoadN, 0), Loc, PI,
                          Align(Size));
  else
    TS = CurDAG->getTruncStore(SDValue(LoadN, 2), dl, SDValue(LoadN, 0), Loc,
                               PI, MVT::getIntegerVT(Size * 8), Align(Size));

  SDNode *StoreN;
  {
    HandleSDNode Handle(TS);
    SelectStore(TS.getNode());
    StoreN = Handle.getValue().getNode();
  }

  // Load's results are { Loaded value, Updated pointer, Chain }
  ReplaceUses(SDValue(IntN, 0), SDValue(LoadN, 1));
  ReplaceUses(SDValue(IntN, 1), SDValue(StoreN, 0));
  return StoreN;
}

bool HexagonDAGToDAGISel::tryLoadOfLoadIntrinsic(LoadSDNode *N) {
  // The intrinsics for load circ/brev perform two operations:
  // 1. Load a value V from the specified location, using the addressing
  //    mode corresponding to the intrinsic.
  // 2. Store V into a specified location. This location is typically a
  //    local, temporary object.
  // In many cases, the program using these intrinsics will immediately
  // load V again from the local object. In those cases, when certain
  // conditions are met, the last load can be removed.
  // This function identifies and optimizes this pattern. If the pattern
  // cannot be optimized, it returns nullptr, which will cause the load
  // to be selected separately from the intrinsic (which will be handled
  // in SelectIntrinsicWChain).

  SDValue Ch = N->getOperand(0);
  SDValue Loc = N->getOperand(1);

  // Assume that the load and the intrinsic are connected directly with a
  // chain:
  //   t1: i32,ch = int.load ..., ..., ..., Loc, ...    // <-- C
  //   t2: i32,ch = load t1:1, Loc, ...
  SDNode *C = Ch.getNode();

  if (C->getOpcode() != ISD::INTRINSIC_W_CHAIN)
    return false;

  // The second load can only be eliminated if its extension type matches
  // that of the load instruction corresponding to the intrinsic. The user
  // can provide an address of an unsigned variable to store the result of
  // a sign-extending intrinsic into (or the other way around).
  ISD::LoadExtType IntExt;
  switch (C->getConstantOperandVal(1)) {
  case Intrinsic::hexagon_circ_ldub:
  case Intrinsic::hexagon_circ_lduh:
    IntExt = ISD::ZEXTLOAD;
    break;
  case Intrinsic::hexagon_circ_ldw:
  case Intrinsic::hexagon_circ_ldd:
    IntExt = ISD::NON_EXTLOAD;
    break;
  default:
    IntExt = ISD::SEXTLOAD;
    break;
  }
  if (N->getExtensionType() != IntExt)
    return false;

  // Make sure the target location for the loaded value in the load intrinsic
  // is the location from which LD (or N) is loading.
  if (C->getNumOperands() < 4 || Loc.getNode() != C->getOperand(3).getNode())
    return false;

  if (MachineSDNode *L = LoadInstrForLoadIntrinsic(C)) {
    SDNode *S = StoreInstrForLoadIntrinsic(L, C);
    SDValue F[] = { SDValue(N,0), SDValue(N,1), SDValue(C,0), SDValue(C,1) };
    SDValue T[] = { SDValue(L,0), SDValue(S,0), SDValue(L,1), SDValue(S,0) };
    ReplaceUses(F, T, std::size(T));
    // This transformation will leave the intrinsic dead. If it remains in
    // the DAG, the selection code will see it again, but without the load,
    // and it will generate a store that is normally required for it.
    CurDAG->RemoveDeadNode(C);
    return true;
  }
  return false;
}

// Convert the bit-reverse load intrinsic to appropriate target instruction.
bool HexagonDAGToDAGISel::SelectBrevLdIntrinsic(SDNode *IntN) {
  if (IntN->getOpcode() != ISD::INTRINSIC_W_CHAIN)
    return false;

  const SDLoc &dl(IntN);
  unsigned IntNo = IntN->getConstantOperandVal(1);

  static const std::map<unsigned, unsigned> LoadBrevMap = {
    { Intrinsic::hexagon_L2_loadrb_pbr, Hexagon::L2_loadrb_pbr },
    { Intrinsic::hexagon_L2_loadrub_pbr, Hexagon::L2_loadrub_pbr },
    { Intrinsic::hexagon_L2_loadrh_pbr, Hexagon::L2_loadrh_pbr },
    { Intrinsic::hexagon_L2_loadruh_pbr, Hexagon::L2_loadruh_pbr },
    { Intrinsic::hexagon_L2_loadri_pbr, Hexagon::L2_loadri_pbr },
    { Intrinsic::hexagon_L2_loadrd_pbr, Hexagon::L2_loadrd_pbr }
  };
  auto FLI = LoadBrevMap.find(IntNo);
  if (FLI != LoadBrevMap.end()) {
    EVT ValTy =
        (IntNo == Intrinsic::hexagon_L2_loadrd_pbr) ? MVT::i64 : MVT::i32;
    EVT RTys[] = { ValTy, MVT::i32, MVT::Other };
    // Operands of Intrinsic: {chain, enum ID of intrinsic, baseptr,
    // modifier}.
    // Operands of target instruction: { Base, Modifier, Chain }.
    MachineSDNode *Res = CurDAG->getMachineNode(
        FLI->second, dl, RTys,
        {IntN->getOperand(2), IntN->getOperand(3), IntN->getOperand(0)});

    MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(IntN)->getMemOperand();
    CurDAG->setNodeMemRefs(Res, {MemOp});

    ReplaceUses(SDValue(IntN, 0), SDValue(Res, 0));
    ReplaceUses(SDValue(IntN, 1), SDValue(Res, 1));
    ReplaceUses(SDValue(IntN, 2), SDValue(Res, 2));
    CurDAG->RemoveDeadNode(IntN);
    return true;
  }
  return false;
}

/// Generate a machine instruction node for the new circular buffer intrinsics.
/// The new versions use a CSx register instead of the K field.
bool HexagonDAGToDAGISel::SelectNewCircIntrinsic(SDNode *IntN) {
  if (IntN->getOpcode() != ISD::INTRINSIC_W_CHAIN)
    return false;

  SDLoc DL(IntN);
  unsigned IntNo = IntN->getConstantOperandVal(1);
  SmallVector<SDValue, 7> Ops;

  static std::map<unsigned,unsigned> LoadNPcMap = {
    { Intrinsic::hexagon_L2_loadrub_pci, Hexagon::PS_loadrub_pci },
    { Intrinsic::hexagon_L2_loadrb_pci, Hexagon::PS_loadrb_pci },
    { Intrinsic::hexagon_L2_loadruh_pci, Hexagon::PS_loadruh_pci },
    { Intrinsic::hexagon_L2_loadrh_pci, Hexagon::PS_loadrh_pci },
    { Intrinsic::hexagon_L2_loadri_pci, Hexagon::PS_loadri_pci },
    { Intrinsic::hexagon_L2_loadrd_pci, Hexagon::PS_loadrd_pci },
    { Intrinsic::hexagon_L2_loadrub_pcr, Hexagon::PS_loadrub_pcr },
    { Intrinsic::hexagon_L2_loadrb_pcr, Hexagon::PS_loadrb_pcr },
    { Intrinsic::hexagon_L2_loadruh_pcr, Hexagon::PS_loadruh_pcr },
    { Intrinsic::hexagon_L2_loadrh_pcr, Hexagon::PS_loadrh_pcr },
    { Intrinsic::hexagon_L2_loadri_pcr, Hexagon::PS_loadri_pcr },
    { Intrinsic::hexagon_L2_loadrd_pcr, Hexagon::PS_loadrd_pcr }
  };
  auto FLI = LoadNPcMap.find (IntNo);
  if (FLI != LoadNPcMap.end()) {
    EVT ValTy = MVT::i32;
    if (IntNo == Intrinsic::hexagon_L2_loadrd_pci ||
        IntNo == Intrinsic::hexagon_L2_loadrd_pcr)
      ValTy = MVT::i64;
    EVT RTys[] = { ValTy, MVT::i32, MVT::Other };
    // Handle load.*_pci case which has 6 operands.
    if (IntN->getNumOperands() == 6) {
      auto Inc = cast<ConstantSDNode>(IntN->getOperand(3));
      SDValue I = CurDAG->getTargetConstant(Inc->getSExtValue(), DL, MVT::i32);
      // Operands: { Base, Increment, Modifier, Start, Chain }.
      Ops = { IntN->getOperand(2), I, IntN->getOperand(4), IntN->getOperand(5),
              IntN->getOperand(0) };
    } else
      // Handle load.*_pcr case which has 5 operands.
      // Operands: { Base, Modifier, Start, Chain }.
      Ops = { IntN->getOperand(2), IntN->getOperand(3), IntN->getOperand(4),
              IntN->getOperand(0) };
    MachineSDNode *Res = CurDAG->getMachineNode(FLI->second, DL, RTys, Ops);
    ReplaceUses(SDValue(IntN, 0), SDValue(Res, 0));
    ReplaceUses(SDValue(IntN, 1), SDValue(Res, 1));
    ReplaceUses(SDValue(IntN, 2), SDValue(Res, 2));
    CurDAG->RemoveDeadNode(IntN);
    return true;
  }

  static std::map<unsigned,unsigned> StoreNPcMap = {
    { Intrinsic::hexagon_S2_storerb_pci, Hexagon::PS_storerb_pci },
    { Intrinsic::hexagon_S2_storerh_pci, Hexagon::PS_storerh_pci },
    { Intrinsic::hexagon_S2_storerf_pci, Hexagon::PS_storerf_pci },
    { Intrinsic::hexagon_S2_storeri_pci, Hexagon::PS_storeri_pci },
    { Intrinsic::hexagon_S2_storerd_pci, Hexagon::PS_storerd_pci },
    { Intrinsic::hexagon_S2_storerb_pcr, Hexagon::PS_storerb_pcr },
    { Intrinsic::hexagon_S2_storerh_pcr, Hexagon::PS_storerh_pcr },
    { Intrinsic::hexagon_S2_storerf_pcr, Hexagon::PS_storerf_pcr },
    { Intrinsic::hexagon_S2_storeri_pcr, Hexagon::PS_storeri_pcr },
    { Intrinsic::hexagon_S2_storerd_pcr, Hexagon::PS_storerd_pcr }
  };
  auto FSI = StoreNPcMap.find (IntNo);
  if (FSI != StoreNPcMap.end()) {
    EVT RTys[] = { MVT::i32, MVT::Other };
    // Handle store.*_pci case which has 7 operands.
    if (IntN->getNumOperands() == 7) {
      auto Inc = cast<ConstantSDNode>(IntN->getOperand(3));
      SDValue I = CurDAG->getTargetConstant(Inc->getSExtValue(), DL, MVT::i32);
      // Operands: { Base, Increment, Modifier, Value, Start, Chain }.
      Ops = { IntN->getOperand(2), I, IntN->getOperand(4), IntN->getOperand(5),
              IntN->getOperand(6), IntN->getOperand(0) };
    } else
      // Handle store.*_pcr case which has 6 operands.
      // Operands: { Base, Modifier, Value, Start, Chain }.
      Ops = { IntN->getOperand(2), IntN->getOperand(3), IntN->getOperand(4),
              IntN->getOperand(5), IntN->getOperand(0) };
    MachineSDNode *Res = CurDAG->getMachineNode(FSI->second, DL, RTys, Ops);
    ReplaceUses(SDValue(IntN, 0), SDValue(Res, 0));
    ReplaceUses(SDValue(IntN, 1), SDValue(Res, 1));
    CurDAG->RemoveDeadNode(IntN);
    return true;
  }

  return false;
}

void HexagonDAGToDAGISel::SelectLoad(SDNode *N) {
  SDLoc dl(N);
  LoadSDNode *LD = cast<LoadSDNode>(N);

  // Handle indexed loads.
  ISD::MemIndexedMode AM = LD->getAddressingMode();
  if (AM != ISD::UNINDEXED) {
    SelectIndexedLoad(LD, dl);
    return;
  }

  // Handle patterns using circ/brev load intrinsics.
  if (tryLoadOfLoadIntrinsic(LD))
    return;

  SelectCode(LD);
}

void HexagonDAGToDAGISel::SelectIndexedStore(StoreSDNode *ST, const SDLoc &dl) {
  SDValue Chain = ST->getChain();
  SDValue Base = ST->getBasePtr();
  SDValue Offset = ST->getOffset();
  SDValue Value = ST->getValue();
  // Get the constant value.
  int32_t Inc = cast<ConstantSDNode>(Offset.getNode())->getSExtValue();
  EVT StoredVT = ST->getMemoryVT();
  EVT ValueVT = Value.getValueType();

  bool IsValidInc = HII->isValidAutoIncImm(StoredVT, Inc);
  unsigned Opcode = 0;

  assert(StoredVT.isSimple());
  switch (StoredVT.getSimpleVT().SimpleTy) {
  case MVT::i8:
    Opcode = IsValidInc ? Hexagon::S2_storerb_pi : Hexagon::S2_storerb_io;
    break;
  case MVT::i16:
    Opcode = IsValidInc ? Hexagon::S2_storerh_pi : Hexagon::S2_storerh_io;
    break;
  case MVT::i32:
  case MVT::f32:
  case MVT::v2i16:
  case MVT::v4i8:
    Opcode = IsValidInc ? Hexagon::S2_storeri_pi : Hexagon::S2_storeri_io;
    break;
  case MVT::i64:
  case MVT::f64:
  case MVT::v2i32:
  case MVT::v4i16:
  case MVT::v8i8:
    Opcode = IsValidInc ? Hexagon::S2_storerd_pi : Hexagon::S2_storerd_io;
    break;
  case MVT::v64i8:
  case MVT::v32i16:
  case MVT::v16i32:
  case MVT::v8i64:
  case MVT::v128i8:
  case MVT::v64i16:
  case MVT::v32i32:
  case MVT::v16i64:
    if (isAlignedMemNode(ST)) {
      if (ST->isNonTemporal())
        Opcode = IsValidInc ? Hexagon::V6_vS32b_nt_pi : Hexagon::V6_vS32b_nt_ai;
      else
        Opcode = IsValidInc ? Hexagon::V6_vS32b_pi : Hexagon::V6_vS32b_ai;
    } else {
      Opcode = IsValidInc ? Hexagon::V6_vS32Ub_pi : Hexagon::V6_vS32Ub_ai;
    }
    break;
  default:
    llvm_unreachable("Unexpected memory type in indexed store");
  }

  if (ST->isTruncatingStore() && ValueVT.getSizeInBits() == 64) {
    assert(StoredVT.getSizeInBits() < 64 && "Not a truncating store");
    Value = CurDAG->getTargetExtractSubreg(Hexagon::isub_lo,
                                           dl, MVT::i32, Value);
  }

  SDValue IncV = CurDAG->getTargetConstant(Inc, dl, MVT::i32);
  MachineMemOperand *MemOp = ST->getMemOperand();

  //                  Next address   Chain
  SDValue From[2] = { SDValue(ST,0), SDValue(ST,1) };
  SDValue To[2];

  if (IsValidInc) {
    // Build post increment store.
    SDValue Ops[] = { Base, IncV, Value, Chain };
    MachineSDNode *S = CurDAG->getMachineNode(Opcode, dl, MVT::i32, MVT::Other,
                                              Ops);
    CurDAG->setNodeMemRefs(S, {MemOp});
    To[0] = SDValue(S, 0);
    To[1] = SDValue(S, 1);
  } else {
    SDValue Zero = CurDAG->getTargetConstant(0, dl, MVT::i32);
    SDValue Ops[] = { Base, Zero, Value, Chain };
    MachineSDNode *S = CurDAG->getMachineNode(Opcode, dl, MVT::Other, Ops);
    CurDAG->setNodeMemRefs(S, {MemOp});
    To[1] = SDValue(S, 0);
    MachineSDNode *A = CurDAG->getMachineNode(Hexagon::A2_addi, dl, MVT::i32,
                                              Base, IncV);
    To[0] = SDValue(A, 0);
  }

  ReplaceUses(From, To, 2);
  CurDAG->RemoveDeadNode(ST);
}

void HexagonDAGToDAGISel::SelectStore(SDNode *N) {
  SDLoc dl(N);
  StoreSDNode *ST = cast<StoreSDNode>(N);

  // Handle indexed stores.
  ISD::MemIndexedMode AM = ST->getAddressingMode();
  if (AM != ISD::UNINDEXED) {
    SelectIndexedStore(ST, dl);
    return;
  }

  SelectCode(ST);
}

void HexagonDAGToDAGISel::SelectSHL(SDNode *N) {
  SDLoc dl(N);
  SDValue Shl_0 = N->getOperand(0);
  SDValue Shl_1 = N->getOperand(1);

  auto Default = [this,N] () -> void { SelectCode(N); };

  if (N->getValueType(0) != MVT::i32 || Shl_1.getOpcode() != ISD::Constant)
    return Default();

  // RHS is const.
  int32_t ShlConst = cast<ConstantSDNode>(Shl_1)->getSExtValue();

  if (Shl_0.getOpcode() == ISD::MUL) {
    SDValue Mul_0 = Shl_0.getOperand(0); // Val
    SDValue Mul_1 = Shl_0.getOperand(1); // Const
    // RHS of mul is const.
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Mul_1)) {
      int32_t ValConst = C->getSExtValue() << ShlConst;
      if (isInt<9>(ValConst)) {
        SDValue Val = CurDAG->getTargetConstant(ValConst, dl, MVT::i32);
        SDNode *Result = CurDAG->getMachineNode(Hexagon::M2_mpysmi, dl,
                                                MVT::i32, Mul_0, Val);
        ReplaceNode(N, Result);
        return;
      }
    }
    return Default();
  }

  if (Shl_0.getOpcode() == ISD::SUB) {
    SDValue Sub_0 = Shl_0.getOperand(0); // Const 0
    SDValue Sub_1 = Shl_0.getOperand(1); // Val
    if (ConstantSDNode *C1 = dyn_cast<ConstantSDNode>(Sub_0)) {
      if (C1->getSExtValue() != 0 || Sub_1.getOpcode() != ISD::SHL)
        return Default();
      SDValue Shl2_0 = Sub_1.getOperand(0); // Val
      SDValue Shl2_1 = Sub_1.getOperand(1); // Const
      if (ConstantSDNode *C2 = dyn_cast<ConstantSDNode>(Shl2_1)) {
        int32_t ValConst = 1 << (ShlConst + C2->getSExtValue());
        if (isInt<9>(-ValConst)) {
          SDValue Val = CurDAG->getTargetConstant(-ValConst, dl, MVT::i32);
          SDNode *Result = CurDAG->getMachineNode(Hexagon::M2_mpysmi, dl,
                                                  MVT::i32, Shl2_0, Val);
          ReplaceNode(N, Result);
          return;
        }
      }
    }
  }

  return Default();
}

//
// Handling intrinsics for circular load and bitreverse load.
//
void HexagonDAGToDAGISel::SelectIntrinsicWChain(SDNode *N) {
  if (MachineSDNode *L = LoadInstrForLoadIntrinsic(N)) {
    StoreInstrForLoadIntrinsic(L, N);
    CurDAG->RemoveDeadNode(N);
    return;
  }

  // Handle bit-reverse load intrinsics.
  if (SelectBrevLdIntrinsic(N))
    return;

  if (SelectNewCircIntrinsic(N))
    return;

  unsigned IntNo = N->getConstantOperandVal(1);
  if (IntNo == Intrinsic::hexagon_V6_vgathermw ||
      IntNo == Intrinsic::hexagon_V6_vgathermw_128B ||
      IntNo == Intrinsic::hexagon_V6_vgathermh ||
      IntNo == Intrinsic::hexagon_V6_vgathermh_128B ||
      IntNo == Intrinsic::hexagon_V6_vgathermhw ||
      IntNo == Intrinsic::hexagon_V6_vgathermhw_128B) {
    SelectV65Gather(N);
    return;
  }
  if (IntNo == Intrinsic::hexagon_V6_vgathermwq ||
      IntNo == Intrinsic::hexagon_V6_vgathermwq_128B ||
      IntNo == Intrinsic::hexagon_V6_vgathermhq ||
      IntNo == Intrinsic::hexagon_V6_vgathermhq_128B ||
      IntNo == Intrinsic::hexagon_V6_vgathermhwq ||
      IntNo == Intrinsic::hexagon_V6_vgathermhwq_128B) {
    SelectV65GatherPred(N);
    return;
  }

  SelectCode(N);
}

void HexagonDAGToDAGISel::SelectIntrinsicWOChain(SDNode *N) {
  unsigned IID = N->getConstantOperandVal(0);
  unsigned Bits;
  switch (IID) {
  case Intrinsic::hexagon_S2_vsplatrb:
    Bits = 8;
    break;
  case Intrinsic::hexagon_S2_vsplatrh:
    Bits = 16;
    break;
  case Intrinsic::hexagon_V6_vaddcarry:
  case Intrinsic::hexagon_V6_vaddcarry_128B:
  case Intrinsic::hexagon_V6_vsubcarry:
  case Intrinsic::hexagon_V6_vsubcarry_128B:
    SelectHVXDualOutput(N);
    return;
  default:
    SelectCode(N);
    return;
  }

  SDValue V = N->getOperand(1);
  SDValue U;
  // Splat intrinsics.
  if (keepsLowBits(V, Bits, U)) {
    SDValue R = CurDAG->getNode(N->getOpcode(), SDLoc(N), N->getValueType(0),
                                N->getOperand(0), U);
    ReplaceNode(N, R.getNode());
    SelectCode(R.getNode());
    return;
  }
  SelectCode(N);
}

void HexagonDAGToDAGISel::SelectExtractSubvector(SDNode *N) {
  SDValue Inp = N->getOperand(0);
  MVT ResTy = N->getValueType(0).getSimpleVT();
  unsigned Idx = N->getConstantOperandVal(1);

  [[maybe_unused]] MVT InpTy = Inp.getValueType().getSimpleVT();
  [[maybe_unused]] unsigned ResLen = ResTy.getVectorNumElements();
  assert(InpTy.getVectorElementType() == ResTy.getVectorElementType());
  assert(2 * ResLen == InpTy.getVectorNumElements());
  assert(ResTy.getSizeInBits() == 32);
  assert(Idx == 0 || Idx == ResLen);

  unsigned SubReg = Idx == 0 ? Hexagon::isub_lo : Hexagon::isub_hi;
  SDValue Ext = CurDAG->getTargetExtractSubreg(SubReg, SDLoc(N), ResTy, Inp);

  ReplaceNode(N, Ext.getNode());
}

//
// Map floating point constant values.
//
void HexagonDAGToDAGISel::SelectConstantFP(SDNode *N) {
  SDLoc dl(N);
  auto *CN = cast<ConstantFPSDNode>(N);
  APInt A = CN->getValueAPF().bitcastToAPInt();
  if (N->getValueType(0) == MVT::f32) {
    SDValue V = CurDAG->getTargetConstant(A.getZExtValue(), dl, MVT::i32);
    ReplaceNode(N, CurDAG->getMachineNode(Hexagon::A2_tfrsi, dl, MVT::f32, V));
    return;
  }
  if (N->getValueType(0) == MVT::f64) {
    SDValue V = CurDAG->getTargetConstant(A.getZExtValue(), dl, MVT::i64);
    ReplaceNode(N, CurDAG->getMachineNode(Hexagon::CONST64, dl, MVT::f64, V));
    return;
  }

  SelectCode(N);
}

//
// Map boolean values.
//
void HexagonDAGToDAGISel::SelectConstant(SDNode *N) {
  if (N->getValueType(0) == MVT::i1) {
    assert(!(N->getAsZExtVal() >> 1));
    unsigned Opc = (cast<ConstantSDNode>(N)->getSExtValue() != 0)
                      ? Hexagon::PS_true
                      : Hexagon::PS_false;
    ReplaceNode(N, CurDAG->getMachineNode(Opc, SDLoc(N), MVT::i1));
    return;
  }

  SelectCode(N);
}

void HexagonDAGToDAGISel::SelectFrameIndex(SDNode *N) {
  MachineFrameInfo &MFI = MF->getFrameInfo();
  const HexagonFrameLowering *HFI = HST->getFrameLowering();
  int FX = cast<FrameIndexSDNode>(N)->getIndex();
  Align StkA = HFI->getStackAlign();
  Align MaxA = MFI.getMaxAlign();
  SDValue FI = CurDAG->getTargetFrameIndex(FX, MVT::i32);
  SDLoc DL(N);
  SDValue Zero = CurDAG->getTargetConstant(0, DL, MVT::i32);
  SDNode *R = nullptr;

  // Use PS_fi when:
  // - the object is fixed, or
  // - there are no objects with higher-than-default alignment, or
  // - there are no dynamically allocated objects.
  // Otherwise, use PS_fia.
  if (FX < 0 || MaxA <= StkA || !MFI.hasVarSizedObjects()) {
    R = CurDAG->getMachineNode(Hexagon::PS_fi, DL, MVT::i32, FI, Zero);
  } else {
    auto &HMFI = *MF->getInfo<HexagonMachineFunctionInfo>();
    Register AR = HMFI.getStackAlignBaseReg();
    SDValue CH = CurDAG->getEntryNode();
    SDValue Ops[] = { CurDAG->getCopyFromReg(CH, DL, AR, MVT::i32), FI, Zero };
    R = CurDAG->getMachineNode(Hexagon::PS_fia, DL, MVT::i32, Ops);
  }

  ReplaceNode(N, R);
}

void HexagonDAGToDAGISel::SelectAddSubCarry(SDNode *N) {
  unsigned OpcCarry = N->getOpcode() == HexagonISD::ADDC ? Hexagon::A4_addp_c
                                                         : Hexagon::A4_subp_c;
  SDNode *C = CurDAG->getMachineNode(OpcCarry, SDLoc(N), N->getVTList(),
                                     { N->getOperand(0), N->getOperand(1),
                                       N->getOperand(2) });
  ReplaceNode(N, C);
}

void HexagonDAGToDAGISel::SelectVAlign(SDNode *N) {
  MVT ResTy = N->getValueType(0).getSimpleVT();
  if (HST->isHVXVectorType(ResTy, true))
    return SelectHvxVAlign(N);

  const SDLoc &dl(N);
  unsigned VecLen = ResTy.getSizeInBits();
  if (VecLen == 32) {
    SDValue Ops[] = {
      CurDAG->getTargetConstant(Hexagon::DoubleRegsRegClassID, dl, MVT::i32),
      N->getOperand(0),
      CurDAG->getTargetConstant(Hexagon::isub_hi, dl, MVT::i32),
      N->getOperand(1),
      CurDAG->getTargetConstant(Hexagon::isub_lo, dl, MVT::i32)
    };
    SDNode *R = CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl,
                                       MVT::i64, Ops);

    // Shift right by "(Addr & 0x3) * 8" bytes.
    SDNode *C;
    SDValue M0 = CurDAG->getTargetConstant(0x18, dl, MVT::i32);
    SDValue M1 = CurDAG->getTargetConstant(0x03, dl, MVT::i32);
    if (HST->useCompound()) {
      C = CurDAG->getMachineNode(Hexagon::S4_andi_asl_ri, dl, MVT::i32,
                                 M0, N->getOperand(2), M1);
    } else {
      SDNode *T = CurDAG->getMachineNode(Hexagon::S2_asl_i_r, dl, MVT::i32,
                                         N->getOperand(2), M1);
      C = CurDAG->getMachineNode(Hexagon::A2_andir, dl, MVT::i32,
                                 SDValue(T, 0), M0);
    }
    SDNode *S = CurDAG->getMachineNode(Hexagon::S2_lsr_r_p, dl, MVT::i64,
                                       SDValue(R, 0), SDValue(C, 0));
    SDValue E = CurDAG->getTargetExtractSubreg(Hexagon::isub_lo, dl, ResTy,
                                               SDValue(S, 0));
    ReplaceNode(N, E.getNode());
  } else {
    assert(VecLen == 64);
    SDNode *Pu = CurDAG->getMachineNode(Hexagon::C2_tfrrp, dl, MVT::v8i1,
                                        N->getOperand(2));
    SDNode *VA = CurDAG->getMachineNode(Hexagon::S2_valignrb, dl, ResTy,
                                        N->getOperand(0), N->getOperand(1),
                                        SDValue(Pu,0));
    ReplaceNode(N, VA);
  }
}

void HexagonDAGToDAGISel::SelectVAlignAddr(SDNode *N) {
  const SDLoc &dl(N);
  SDValue A = N->getOperand(1);
  int Mask = -cast<ConstantSDNode>(A.getNode())->getSExtValue();
  assert(isPowerOf2_32(-Mask));

  SDValue M = CurDAG->getTargetConstant(Mask, dl, MVT::i32);
  SDNode *AA = CurDAG->getMachineNode(Hexagon::A2_andir, dl, MVT::i32,
                                      N->getOperand(0), M);
  ReplaceNode(N, AA);
}

// Handle these nodes here to avoid having to write patterns for all
// combinations of input/output types. In all cases, the resulting
// instruction is the same.
void HexagonDAGToDAGISel::SelectTypecast(SDNode *N) {
  SDValue Op = N->getOperand(0);
  MVT OpTy = Op.getValueType().getSimpleVT();
  SDNode *T = CurDAG->MorphNodeTo(N, N->getOpcode(),
                                  CurDAG->getVTList(OpTy), {Op});
  ReplaceNode(T, Op.getNode());
}

void HexagonDAGToDAGISel::SelectP2D(SDNode *N) {
  MVT ResTy = N->getValueType(0).getSimpleVT();
  SDNode *T = CurDAG->getMachineNode(Hexagon::C2_mask, SDLoc(N), ResTy,
                                     N->getOperand(0));
  ReplaceNode(N, T);
}

void HexagonDAGToDAGISel::SelectD2P(SDNode *N) {
  const SDLoc &dl(N);
  MVT ResTy = N->getValueType(0).getSimpleVT();
  SDValue Zero = CurDAG->getTargetConstant(0, dl, MVT::i32);
  SDNode *T = CurDAG->getMachineNode(Hexagon::A4_vcmpbgtui, dl, ResTy,
                                     N->getOperand(0), Zero);
  ReplaceNode(N, T);
}

void HexagonDAGToDAGISel::SelectV2Q(SDNode *N) {
  const SDLoc &dl(N);
  MVT ResTy = N->getValueType(0).getSimpleVT();
  // The argument to V2Q should be a single vector.
  MVT OpTy = N->getOperand(0).getValueType().getSimpleVT(); (void)OpTy;
  assert(HST->getVectorLength() * 8 == OpTy.getSizeInBits());

  SDValue C = CurDAG->getTargetConstant(-1, dl, MVT::i32);
  SDNode *R = CurDAG->getMachineNode(Hexagon::A2_tfrsi, dl, MVT::i32, C);
  SDNode *T = CurDAG->getMachineNode(Hexagon::V6_vandvrt, dl, ResTy,
                                     N->getOperand(0), SDValue(R,0));
  ReplaceNode(N, T);
}

void HexagonDAGToDAGISel::SelectQ2V(SDNode *N) {
  const SDLoc &dl(N);
  MVT ResTy = N->getValueType(0).getSimpleVT();
  // The result of V2Q should be a single vector.
  assert(HST->getVectorLength() * 8 == ResTy.getSizeInBits());

  SDValue C = CurDAG->getTargetConstant(-1, dl, MVT::i32);
  SDNode *R = CurDAG->getMachineNode(Hexagon::A2_tfrsi, dl, MVT::i32, C);
  SDNode *T = CurDAG->getMachineNode(Hexagon::V6_vandqrt, dl, ResTy,
                                     N->getOperand(0), SDValue(R,0));
  ReplaceNode(N, T);
}

void HexagonDAGToDAGISel::FDiv(SDNode *N) {
  const SDLoc &dl(N);
  ArrayRef<EVT> ResultType(N->value_begin(), N->value_end());
  SmallVector<SDValue, 2> Ops;
  Ops = {N->getOperand(0), N->getOperand(1)};
  SDVTList VTs;
  VTs = CurDAG->getVTList(MVT::f32, MVT::f32);
  SDNode *ResScale = CurDAG->getMachineNode(Hexagon::F2_sfrecipa, dl, VTs, Ops);
  SDNode *D = CurDAG->getMachineNode(Hexagon::F2_sffixupd, dl, MVT::f32, Ops);

  SDValue C = CurDAG->getTargetConstant(0x3f800000, dl, MVT::i32);
  SDNode *constNode =
      CurDAG->getMachineNode(Hexagon::A2_tfrsi, dl, MVT::f32, C);

  SDNode *n = CurDAG->getMachineNode(Hexagon::F2_sffixupn, dl, MVT::f32, Ops);
  SDNode *Err = CurDAG->getMachineNode(Hexagon::F2_sffms_lib, dl, MVT::f32,
                                       SDValue(constNode, 0), SDValue(D, 0),
                                       SDValue(ResScale, 0));
  SDNode *NewRec = CurDAG->getMachineNode(Hexagon::F2_sffma_lib, dl, MVT::f32,
                                          SDValue(ResScale, 0), SDValue(Err, 0),
                                          SDValue(ResScale, 0));
  SDNode *newErr = CurDAG->getMachineNode(Hexagon::F2_sffms_lib, dl, MVT::f32,
                                          SDValue(constNode, 0), SDValue(D, 0),
                                          SDValue(NewRec, 0));
  SDNode *q = CurDAG->getMachineNode(
      Hexagon::A2_andir, dl, MVT::f32, SDValue(n, 0),
      CurDAG->getTargetConstant(0x80000000, dl, MVT::i32));
  SDNode *NewQ =
      CurDAG->getMachineNode(Hexagon::F2_sffma_lib, dl, MVT::f32, SDValue(q, 0),
                             SDValue(n, 0), SDValue(NewRec, 0));
  SDNode *NNewRec = CurDAG->getMachineNode(
      Hexagon::F2_sffma_lib, dl, MVT::f32, SDValue(NewRec, 0),
      SDValue(newErr, 0), SDValue(NewRec, 0));
  SDNode *qErr =
      CurDAG->getMachineNode(Hexagon::F2_sffms_lib, dl, MVT::f32, SDValue(n, 0),
                             SDValue(D, 0), SDValue(NewQ, 0));
  SDNode *NNewQ = CurDAG->getMachineNode(Hexagon::F2_sffma_lib, dl, MVT::f32,
                                         SDValue(NewQ, 0), SDValue(qErr, 0),
                                         SDValue(NNewRec, 0));

  SDNode *NqErr =
      CurDAG->getMachineNode(Hexagon::F2_sffms_lib, dl, MVT::f32, SDValue(n, 0),
                             SDValue(NNewQ, 0), SDValue(D, 0));
  std::array<SDValue, 4> temp1 = {SDValue(NNewQ, 0), SDValue(NqErr, 0),
                                  SDValue(NNewRec, 0), SDValue(ResScale, 1)};
  ArrayRef<SDValue> OpValue1(temp1);
  SDNode *FinalNewQ =
      CurDAG->getMachineNode(Hexagon::F2_sffma_sc, dl, MVT::f32, OpValue1);
  ReplaceNode(N, FinalNewQ);
}

void HexagonDAGToDAGISel::FastFDiv(SDNode *N) {
  const SDLoc &dl(N);
  ArrayRef<EVT> ResultType(N->value_begin(), N->value_end());
  SmallVector<SDValue, 2> Ops;
  Ops = {N->getOperand(0), N->getOperand(1)};
  SDVTList VTs;
  VTs = CurDAG->getVTList(MVT::f32, MVT::f32);
  SDNode *ResScale = CurDAG->getMachineNode(Hexagon::F2_sfrecipa, dl, VTs, Ops);
  SDNode *D = CurDAG->getMachineNode(Hexagon::F2_sffixupd, dl, MVT::f32, Ops);

  SDValue C = CurDAG->getTargetConstant(0x3f800000, dl, MVT::i32);
  SDNode *constNode =
      CurDAG->getMachineNode(Hexagon::A2_tfrsi, dl, MVT::f32, C);

  SDNode *n = CurDAG->getMachineNode(Hexagon::F2_sffixupn, dl, MVT::f32, Ops);
  SDNode *Err = CurDAG->getMachineNode(Hexagon::F2_sffms_lib, dl, MVT::f32,
                                       SDValue(constNode, 0), SDValue(D, 0),
                                       SDValue(ResScale, 0));
  SDNode *NewRec = CurDAG->getMachineNode(Hexagon::F2_sffma_lib, dl, MVT::f32,
                                          SDValue(ResScale, 0), SDValue(Err, 0),
                                          SDValue(ResScale, 0));
  SDNode *newErr = CurDAG->getMachineNode(Hexagon::F2_sffms_lib, dl, MVT::f32,
                                          SDValue(constNode, 0), SDValue(D, 0),
                                          SDValue(NewRec, 0));

  SDNode *NNewRec = CurDAG->getMachineNode(
      Hexagon::F2_sffma_lib, dl, MVT::f32, SDValue(NewRec, 0),
      SDValue(newErr, 0), SDValue(NewRec, 0));
  SDNode *FinalNewQ = CurDAG->getMachineNode(
      Hexagon::F2_sfmpy, dl, MVT::f32, SDValue(NNewRec, 0), SDValue(n, 0));
  ReplaceNode(N, FinalNewQ);
}

void HexagonDAGToDAGISel::SelectFDiv(SDNode *N) {
  if (N->getFlags().hasAllowReassociation())
    FastFDiv(N);
  else
    FDiv(N);
  return;
}

void HexagonDAGToDAGISel::Select(SDNode *N) {
  if (N->isMachineOpcode())
    return N->setNodeId(-1);  // Already selected.

  auto isHvxOp = [this](SDNode *N) {
    for (unsigned i = 0, e = N->getNumValues(); i != e; ++i) {
      if (HST->isHVXVectorType(N->getValueType(i), true))
        return true;
    }
    for (SDValue I : N->ops()) {
      if (HST->isHVXVectorType(I.getValueType(), true))
        return true;
    }
    return false;
  };

  if (HST->useHVXOps() && isHvxOp(N)) {
    switch (N->getOpcode()) {
    case ISD::EXTRACT_SUBVECTOR:  return SelectHvxExtractSubvector(N);
    case ISD::VECTOR_SHUFFLE:     return SelectHvxShuffle(N);

    case HexagonISD::VROR:        return SelectHvxRor(N);
    }
  }

  switch (N->getOpcode()) {
  case ISD::Constant:             return SelectConstant(N);
  case ISD::ConstantFP:           return SelectConstantFP(N);
  case ISD::FrameIndex:           return SelectFrameIndex(N);
  case ISD::SHL:                  return SelectSHL(N);
  case ISD::LOAD:                 return SelectLoad(N);
  case ISD::STORE:                return SelectStore(N);
  case ISD::INTRINSIC_W_CHAIN:    return SelectIntrinsicWChain(N);
  case ISD::INTRINSIC_WO_CHAIN:   return SelectIntrinsicWOChain(N);
  case ISD::EXTRACT_SUBVECTOR:    return SelectExtractSubvector(N);

  case HexagonISD::ADDC:
  case HexagonISD::SUBC:          return SelectAddSubCarry(N);
  case HexagonISD::VALIGN:        return SelectVAlign(N);
  case HexagonISD::VALIGNADDR:    return SelectVAlignAddr(N);
  case HexagonISD::TYPECAST:      return SelectTypecast(N);
  case HexagonISD::P2D:           return SelectP2D(N);
  case HexagonISD::D2P:           return SelectD2P(N);
  case HexagonISD::Q2V:           return SelectQ2V(N);
  case HexagonISD::V2Q:           return SelectV2Q(N);
  case ISD::FDIV:
    return SelectFDiv(N);
  }

  SelectCode(N);
}

bool HexagonDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  SDValue Inp = Op, Res;

  switch (ConstraintID) {
  default:
    return true;
  case InlineAsm::ConstraintCode::o: // Offsetable.
  case InlineAsm::ConstraintCode::v: // Not offsetable.
  case InlineAsm::ConstraintCode::m: // Memory.
    if (SelectAddrFI(Inp, Res))
      OutOps.push_back(Res);
    else
      OutOps.push_back(Inp);
    break;
  }

  OutOps.push_back(CurDAG->getTargetConstant(0, SDLoc(Op), MVT::i32));
  return false;
}

static bool isMemOPCandidate(SDNode *I, SDNode *U) {
  // I is an operand of U. Check if U is an arithmetic (binary) operation
  // usable in a memop, where the other operand is a loaded value, and the
  // result of U is stored in the same location.

  if (!U->hasOneUse())
    return false;
  unsigned Opc = U->getOpcode();
  switch (Opc) {
    case ISD::ADD:
    case ISD::SUB:
    case ISD::AND:
    case ISD::OR:
      break;
    default:
      return false;
  }

  SDValue S0 = U->getOperand(0);
  SDValue S1 = U->getOperand(1);
  SDValue SY = (S0.getNode() == I) ? S1 : S0;

  SDNode *UUse = *U->use_begin();
  if (UUse->getNumValues() != 1)
    return false;

  // Check if one of the inputs to U is a load instruction and the output
  // is used by a store instruction. If so and they also have the same
  // base pointer, then don't preoprocess this node sequence as it
  // can be matched to a memop.
  SDNode *SYNode = SY.getNode();
  if (UUse->getOpcode() == ISD::STORE && SYNode->getOpcode() == ISD::LOAD) {
    SDValue LDBasePtr = cast<MemSDNode>(SYNode)->getBasePtr();
    SDValue STBasePtr = cast<MemSDNode>(UUse)->getBasePtr();
    if (LDBasePtr == STBasePtr)
      return true;
  }
  return false;
}


// Transform: (or (select c x 0) z)  ->  (select c (or x z) z)
//            (or (select c 0 y) z)  ->  (select c z (or y z))
void HexagonDAGToDAGISel::ppSimplifyOrSelect0(std::vector<SDNode*> &&Nodes) {
  SelectionDAG &DAG = *CurDAG;

  for (auto *I : Nodes) {
    if (I->getOpcode() != ISD::OR)
      continue;

    auto IsSelect0 = [](const SDValue &Op) -> bool {
      if (Op.getOpcode() != ISD::SELECT)
        return false;
      return isNullConstant(Op.getOperand(1)) ||
             isNullConstant(Op.getOperand(2));
    };

    SDValue N0 = I->getOperand(0), N1 = I->getOperand(1);
    EVT VT = I->getValueType(0);
    bool SelN0 = IsSelect0(N0);
    SDValue SOp = SelN0 ? N0 : N1;
    SDValue VOp = SelN0 ? N1 : N0;

    if (SOp.getOpcode() == ISD::SELECT && SOp.getNode()->hasOneUse()) {
      SDValue SC = SOp.getOperand(0);
      SDValue SX = SOp.getOperand(1);
      SDValue SY = SOp.getOperand(2);
      SDLoc DLS = SOp;
      if (isNullConstant(SY)) {
        SDValue NewOr = DAG.getNode(ISD::OR, DLS, VT, SX, VOp);
        SDValue NewSel = DAG.getNode(ISD::SELECT, DLS, VT, SC, NewOr, VOp);
        DAG.ReplaceAllUsesWith(I, NewSel.getNode());
      } else if (isNullConstant(SX)) {
        SDValue NewOr = DAG.getNode(ISD::OR, DLS, VT, SY, VOp);
        SDValue NewSel = DAG.getNode(ISD::SELECT, DLS, VT, SC, VOp, NewOr);
        DAG.ReplaceAllUsesWith(I, NewSel.getNode());
      }
    }
  }
}

// Transform: (store ch val (add x (add (shl y c) e)))
//        to: (store ch val (add x (shl (add y d) c))),
// where e = (shl d c) for some integer d.
// The purpose of this is to enable generation of loads/stores with
// shifted addressing mode, i.e. mem(x+y<<#c). For that, the shift
// value c must be 0, 1 or 2.
void HexagonDAGToDAGISel::ppAddrReorderAddShl(std::vector<SDNode*> &&Nodes) {
  SelectionDAG &DAG = *CurDAG;

  for (auto *I : Nodes) {
    if (I->getOpcode() != ISD::STORE)
      continue;

    // I matched: (store ch val Off)
    SDValue Off = I->getOperand(2);
    // Off needs to match: (add x (add (shl y c) (shl d c))))
    if (Off.getOpcode() != ISD::ADD)
      continue;
    // Off matched: (add x T0)
    SDValue T0 = Off.getOperand(1);
    // T0 needs to match: (add T1 T2):
    if (T0.getOpcode() != ISD::ADD)
      continue;
    // T0 matched: (add T1 T2)
    SDValue T1 = T0.getOperand(0);
    SDValue T2 = T0.getOperand(1);
    // T1 needs to match: (shl y c)
    if (T1.getOpcode() != ISD::SHL)
      continue;
    SDValue C = T1.getOperand(1);
    ConstantSDNode *CN = dyn_cast<ConstantSDNode>(C.getNode());
    if (CN == nullptr)
      continue;
    unsigned CV = CN->getZExtValue();
    if (CV > 2)
      continue;
    // T2 needs to match e, where e = (shl d c) for some d.
    ConstantSDNode *EN = dyn_cast<ConstantSDNode>(T2.getNode());
    if (EN == nullptr)
      continue;
    unsigned EV = EN->getZExtValue();
    if (EV % (1 << CV) != 0)
      continue;
    unsigned DV = EV / (1 << CV);

    // Replace T0 with: (shl (add y d) c)
    SDLoc DL = SDLoc(I);
    EVT VT = T0.getValueType();
    SDValue D = DAG.getConstant(DV, DL, VT);
    // NewAdd = (add y d)
    SDValue NewAdd = DAG.getNode(ISD::ADD, DL, VT, T1.getOperand(0), D);
    // NewShl = (shl NewAdd c)
    SDValue NewShl = DAG.getNode(ISD::SHL, DL, VT, NewAdd, C);
    ReplaceNode(T0.getNode(), NewShl.getNode());
  }
}

// Transform: (load ch (add x (and (srl y c) Mask)))
//        to: (load ch (add x (shl (srl y d) d-c)))
// where
// Mask = 00..0 111..1 0.0
//          |     |     +-- d-c 0s, and d-c is 0, 1 or 2.
//          |     +-------- 1s
//          +-------------- at most c 0s
// Motivating example:
// DAG combiner optimizes (add x (shl (srl y 5) 2))
//                     to (add x (and (srl y 3) 1FFFFFFC))
// which results in a constant-extended and(##...,lsr). This transformation
// undoes this simplification for cases where the shl can be folded into
// an addressing mode.
void HexagonDAGToDAGISel::ppAddrRewriteAndSrl(std::vector<SDNode*> &&Nodes) {
  SelectionDAG &DAG = *CurDAG;

  for (SDNode *N : Nodes) {
    unsigned Opc = N->getOpcode();
    if (Opc != ISD::LOAD && Opc != ISD::STORE)
      continue;
    SDValue Addr = Opc == ISD::LOAD ? N->getOperand(1) : N->getOperand(2);
    // Addr must match: (add x T0)
    if (Addr.getOpcode() != ISD::ADD)
      continue;
    SDValue T0 = Addr.getOperand(1);
    // T0 must match: (and T1 Mask)
    if (T0.getOpcode() != ISD::AND)
      continue;

    // We have an AND.
    //
    // Check the first operand. It must be: (srl y c).
    SDValue S = T0.getOperand(0);
    if (S.getOpcode() != ISD::SRL)
      continue;
    ConstantSDNode *SN = dyn_cast<ConstantSDNode>(S.getOperand(1).getNode());
    if (SN == nullptr)
      continue;
    if (SN->getAPIntValue().getBitWidth() != 32)
      continue;
    uint32_t CV = SN->getZExtValue();

    // Check the second operand: the supposed mask.
    ConstantSDNode *MN = dyn_cast<ConstantSDNode>(T0.getOperand(1).getNode());
    if (MN == nullptr)
      continue;
    if (MN->getAPIntValue().getBitWidth() != 32)
      continue;
    uint32_t Mask = MN->getZExtValue();
    // Examine the mask.
    uint32_t TZ = llvm::countr_zero(Mask);
    uint32_t M1 = llvm::countr_one(Mask >> TZ);
    uint32_t LZ = llvm::countl_zero(Mask);
    // Trailing zeros + middle ones + leading zeros must equal the width.
    if (TZ + M1 + LZ != 32)
      continue;
    // The number of trailing zeros will be encoded in the addressing mode.
    if (TZ > 2)
      continue;
    // The number of leading zeros must be at most c.
    if (LZ > CV)
      continue;

    // All looks good.
    SDValue Y = S.getOperand(0);
    EVT VT = Addr.getValueType();
    SDLoc dl(S);
    // TZ = D-C, so D = TZ+C.
    SDValue D = DAG.getConstant(TZ+CV, dl, VT);
    SDValue DC = DAG.getConstant(TZ, dl, VT);
    SDValue NewSrl = DAG.getNode(ISD::SRL, dl, VT, Y, D);
    SDValue NewShl = DAG.getNode(ISD::SHL, dl, VT, NewSrl, DC);
    ReplaceNode(T0.getNode(), NewShl.getNode());
  }
}

// Transform: (op ... (zext i1 c) ...) -> (select c (op ... 0 ...)
//                                                  (op ... 1 ...))
void HexagonDAGToDAGISel::ppHoistZextI1(std::vector<SDNode*> &&Nodes) {
  SelectionDAG &DAG = *CurDAG;

  for (SDNode *N : Nodes) {
    unsigned Opc = N->getOpcode();
    if (Opc != ISD::ZERO_EXTEND)
      continue;
    SDValue OpI1 = N->getOperand(0);
    EVT OpVT = OpI1.getValueType();
    if (!OpVT.isSimple() || OpVT.getSimpleVT() != MVT::i1)
      continue;
    for (auto I = N->use_begin(), E = N->use_end(); I != E; ++I) {
      SDNode *U = *I;
      if (U->getNumValues() != 1)
        continue;
      EVT UVT = U->getValueType(0);
      if (!UVT.isSimple() || !UVT.isInteger() || UVT.getSimpleVT() == MVT::i1)
        continue;
      // Do not generate select for all i1 vector type.
      if (UVT.isVector() && UVT.getVectorElementType() == MVT::i1)
        continue;
      if (isMemOPCandidate(N, U))
        continue;

      // Potentially simplifiable operation.
      unsigned I1N = I.getOperandNo();
      SmallVector<SDValue,2> Ops(U->getNumOperands());
      for (unsigned i = 0, n = U->getNumOperands(); i != n; ++i)
        Ops[i] = U->getOperand(i);
      EVT BVT = Ops[I1N].getValueType();

      const SDLoc &dl(U);
      SDValue C0 = DAG.getConstant(0, dl, BVT);
      SDValue C1 = DAG.getConstant(1, dl, BVT);
      SDValue If0, If1;

      if (isa<MachineSDNode>(U)) {
        unsigned UseOpc = U->getMachineOpcode();
        Ops[I1N] = C0;
        If0 = SDValue(DAG.getMachineNode(UseOpc, dl, UVT, Ops), 0);
        Ops[I1N] = C1;
        If1 = SDValue(DAG.getMachineNode(UseOpc, dl, UVT, Ops), 0);
      } else {
        unsigned UseOpc = U->getOpcode();
        Ops[I1N] = C0;
        If0 = DAG.getNode(UseOpc, dl, UVT, Ops);
        Ops[I1N] = C1;
        If1 = DAG.getNode(UseOpc, dl, UVT, Ops);
      }
      // We're generating a SELECT way after legalization, so keep the types
      // simple.
      unsigned UW = UVT.getSizeInBits();
      EVT SVT = (UW == 32 || UW == 64) ? MVT::getIntegerVT(UW) : UVT;
      SDValue Sel = DAG.getNode(ISD::SELECT, dl, SVT, OpI1,
                                DAG.getBitcast(SVT, If1),
                                DAG.getBitcast(SVT, If0));
      SDValue Ret = DAG.getBitcast(UVT, Sel);
      DAG.ReplaceAllUsesWith(U, Ret.getNode());
    }
  }
}

void HexagonDAGToDAGISel::PreprocessISelDAG() {
  // Repack all nodes before calling each preprocessing function,
  // because each of them can modify the set of nodes.
  auto getNodes = [this]() -> std::vector<SDNode *> {
    std::vector<SDNode *> T;
    T.reserve(CurDAG->allnodes_size());
    for (SDNode &N : CurDAG->allnodes())
      T.push_back(&N);
    return T;
  };

  if (HST->useHVXOps())
    PreprocessHvxISelDAG();

  // Transform: (or (select c x 0) z)  ->  (select c (or x z) z)
  //            (or (select c 0 y) z)  ->  (select c z (or y z))
  ppSimplifyOrSelect0(getNodes());

  // Transform: (store ch val (add x (add (shl y c) e)))
  //        to: (store ch val (add x (shl (add y d) c))),
  // where e = (shl d c) for some integer d.
  // The purpose of this is to enable generation of loads/stores with
  // shifted addressing mode, i.e. mem(x+y<<#c). For that, the shift
  // value c must be 0, 1 or 2.
  ppAddrReorderAddShl(getNodes());

  // Transform: (load ch (add x (and (srl y c) Mask)))
  //        to: (load ch (add x (shl (srl y d) d-c)))
  // where
  // Mask = 00..0 111..1 0.0
  //          |     |     +-- d-c 0s, and d-c is 0, 1 or 2.
  //          |     +-------- 1s
  //          +-------------- at most c 0s
  // Motivating example:
  // DAG combiner optimizes (add x (shl (srl y 5) 2))
  //                     to (add x (and (srl y 3) 1FFFFFFC))
  // which results in a constant-extended and(##...,lsr). This transformation
  // undoes this simplification for cases where the shl can be folded into
  // an addressing mode.
  ppAddrRewriteAndSrl(getNodes());

  // Transform: (op ... (zext i1 c) ...) -> (select c (op ... 0 ...)
  //                                                  (op ... 1 ...))
  ppHoistZextI1(getNodes());

  DEBUG_WITH_TYPE("isel", {
    dbgs() << "Preprocessed (Hexagon) selection DAG:";
    CurDAG->dump();
  });

  if (EnableAddressRebalancing) {
    rebalanceAddressTrees();

    DEBUG_WITH_TYPE("isel", {
      dbgs() << "Address tree balanced selection DAG:";
      CurDAG->dump();
    });
  }
}

void HexagonDAGToDAGISel::emitFunctionEntryCode() {
  auto &HST = MF->getSubtarget<HexagonSubtarget>();
  auto &HFI = *HST.getFrameLowering();
  if (!HFI.needsAligna(*MF))
    return;

  MachineFrameInfo &MFI = MF->getFrameInfo();
  MachineBasicBlock *EntryBB = &MF->front();
  Align EntryMaxA = MFI.getMaxAlign();

  // Reserve the first non-volatile register.
  Register AP = 0;
  auto &HRI = *HST.getRegisterInfo();
  BitVector Reserved = HRI.getReservedRegs(*MF);
  for (const MCPhysReg *R = HRI.getCalleeSavedRegs(MF); *R; ++R) {
    if (Reserved[*R])
      continue;
    AP = *R;
    break;
  }
  assert(AP.isValid() && "Couldn't reserve stack align register");
  BuildMI(EntryBB, DebugLoc(), HII->get(Hexagon::PS_aligna), AP)
      .addImm(EntryMaxA.value());
  MF->getInfo<HexagonMachineFunctionInfo>()->setStackAlignBaseReg(AP);
}

void HexagonDAGToDAGISel::updateAligna() {
  auto &HFI = *MF->getSubtarget<HexagonSubtarget>().getFrameLowering();
  if (!HFI.needsAligna(*MF))
    return;
  auto *AlignaI = const_cast<MachineInstr*>(HFI.getAlignaInstr(*MF));
  assert(AlignaI != nullptr);
  unsigned MaxA = MF->getFrameInfo().getMaxAlign().value();
  if (AlignaI->getOperand(1).getImm() < MaxA)
    AlignaI->getOperand(1).setImm(MaxA);
}

// Match a frame index that can be used in an addressing mode.
bool HexagonDAGToDAGISel::SelectAddrFI(SDValue &N, SDValue &R) {
  if (N.getOpcode() != ISD::FrameIndex)
    return false;
  auto &HFI = *HST->getFrameLowering();
  MachineFrameInfo &MFI = MF->getFrameInfo();
  int FX = cast<FrameIndexSDNode>(N)->getIndex();
  if (!MFI.isFixedObjectIndex(FX) && HFI.needsAligna(*MF))
    return false;
  R = CurDAG->getTargetFrameIndex(FX, MVT::i32);
  return true;
}

inline bool HexagonDAGToDAGISel::SelectAddrGA(SDValue &N, SDValue &R) {
  return SelectGlobalAddress(N, R, false, Align(1));
}

inline bool HexagonDAGToDAGISel::SelectAddrGP(SDValue &N, SDValue &R) {
  return SelectGlobalAddress(N, R, true, Align(1));
}

inline bool HexagonDAGToDAGISel::SelectAnyImm(SDValue &N, SDValue &R) {
  return SelectAnyImmediate(N, R, Align(1));
}

inline bool HexagonDAGToDAGISel::SelectAnyImm0(SDValue &N, SDValue &R) {
  return SelectAnyImmediate(N, R, Align(1));
}
inline bool HexagonDAGToDAGISel::SelectAnyImm1(SDValue &N, SDValue &R) {
  return SelectAnyImmediate(N, R, Align(2));
}
inline bool HexagonDAGToDAGISel::SelectAnyImm2(SDValue &N, SDValue &R) {
  return SelectAnyImmediate(N, R, Align(4));
}
inline bool HexagonDAGToDAGISel::SelectAnyImm3(SDValue &N, SDValue &R) {
  return SelectAnyImmediate(N, R, Align(8));
}

inline bool HexagonDAGToDAGISel::SelectAnyInt(SDValue &N, SDValue &R) {
  EVT T = N.getValueType();
  if (!T.isInteger() || T.getSizeInBits() != 32 || !isa<ConstantSDNode>(N))
    return false;
  int32_t V = cast<const ConstantSDNode>(N)->getZExtValue();
  R = CurDAG->getTargetConstant(V, SDLoc(N), N.getValueType());
  return true;
}

bool HexagonDAGToDAGISel::SelectAnyImmediate(SDValue &N, SDValue &R,
                                             Align Alignment) {
  switch (N.getOpcode()) {
  case ISD::Constant: {
    if (N.getValueType() != MVT::i32)
      return false;
    int32_t V = cast<const ConstantSDNode>(N)->getZExtValue();
    if (!isAligned(Alignment, V))
      return false;
    R = CurDAG->getTargetConstant(V, SDLoc(N), N.getValueType());
    return true;
  }
  case HexagonISD::JT:
  case HexagonISD::CP:
    // These are assumed to always be aligned at least 8-byte boundary.
    if (Alignment > Align(8))
      return false;
    R = N.getOperand(0);
    return true;
  case ISD::ExternalSymbol:
    // Symbols may be aligned at any boundary.
    if (Alignment > Align(1))
      return false;
    R = N;
    return true;
  case ISD::BlockAddress:
    // Block address is always aligned at least 4-byte boundary.
    if (Alignment > Align(4) ||
        !isAligned(Alignment, cast<BlockAddressSDNode>(N)->getOffset()))
      return false;
    R = N;
    return true;
  }

  if (SelectGlobalAddress(N, R, false, Alignment) ||
      SelectGlobalAddress(N, R, true, Alignment))
    return true;

  return false;
}

bool HexagonDAGToDAGISel::SelectGlobalAddress(SDValue &N, SDValue &R,
                                              bool UseGP, Align Alignment) {
  switch (N.getOpcode()) {
  case ISD::ADD: {
    SDValue N0 = N.getOperand(0);
    SDValue N1 = N.getOperand(1);
    unsigned GAOpc = N0.getOpcode();
    if (UseGP && GAOpc != HexagonISD::CONST32_GP)
      return false;
    if (!UseGP && GAOpc != HexagonISD::CONST32)
      return false;
    if (ConstantSDNode *Const = dyn_cast<ConstantSDNode>(N1)) {
      if (!isAligned(Alignment, Const->getZExtValue()))
        return false;
      SDValue Addr = N0.getOperand(0);
      if (GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(Addr)) {
        if (GA->getOpcode() == ISD::TargetGlobalAddress) {
          uint64_t NewOff = GA->getOffset() + (uint64_t)Const->getSExtValue();
          R = CurDAG->getTargetGlobalAddress(GA->getGlobal(), SDLoc(Const),
                                             N.getValueType(), NewOff);
          return true;
        }
      }
    }
    break;
  }
  case HexagonISD::CP:
  case HexagonISD::JT:
  case HexagonISD::CONST32:
    // The operand(0) of CONST32 is TargetGlobalAddress, which is what we
    // want in the instruction.
    if (!UseGP)
      R = N.getOperand(0);
    return !UseGP;
  case HexagonISD::CONST32_GP:
    if (UseGP)
      R = N.getOperand(0);
    return UseGP;
  default:
    return false;
  }

  return false;
}

bool HexagonDAGToDAGISel::DetectUseSxtw(SDValue &N, SDValue &R) {
  // This (complex pattern) function is meant to detect a sign-extension
  // i32->i64 on a per-operand basis. This would allow writing single
  // patterns that would cover a number of combinations of different ways
  // a sign-extensions could be written. For example:
  //   (mul (DetectUseSxtw x) (DetectUseSxtw y)) -> (M2_dpmpyss_s0 x y)
  // could match either one of these:
  //   (mul (sext x) (sext_inreg y))
  //   (mul (sext-load *p) (sext_inreg y))
  //   (mul (sext_inreg x) (sext y))
  // etc.
  //
  // The returned value will have type i64 and its low word will
  // contain the value being extended. The high bits are not specified.
  // The returned type is i64 because the original type of N was i64,
  // but the users of this function should only use the low-word of the
  // result, e.g.
  //  (mul sxtw:x, sxtw:y) -> (M2_dpmpyss_s0 (LoReg sxtw:x), (LoReg sxtw:y))

  if (N.getValueType() != MVT::i64)
    return false;
  unsigned Opc = N.getOpcode();
  switch (Opc) {
    case ISD::SIGN_EXTEND:
    case ISD::SIGN_EXTEND_INREG: {
      // sext_inreg has the source type as a separate operand.
      EVT T = Opc == ISD::SIGN_EXTEND
                ? N.getOperand(0).getValueType()
                : cast<VTSDNode>(N.getOperand(1))->getVT();
      unsigned SW = T.getSizeInBits();
      if (SW == 32)
        R = N.getOperand(0);
      else if (SW < 32)
        R = N;
      else
        return false;
      break;
    }
    case ISD::LOAD: {
      LoadSDNode *L = cast<LoadSDNode>(N);
      if (L->getExtensionType() != ISD::SEXTLOAD)
        return false;
      // All extending loads extend to i32, so even if the value in
      // memory is shorter than 32 bits, it will be i32 after the load.
      if (L->getMemoryVT().getSizeInBits() > 32)
        return false;
      R = N;
      break;
    }
    case ISD::SRA: {
      auto *S = dyn_cast<ConstantSDNode>(N.getOperand(1));
      if (!S || S->getZExtValue() != 32)
        return false;
      R = N;
      break;
    }
    default:
      return false;
  }
  EVT RT = R.getValueType();
  if (RT == MVT::i64)
    return true;
  assert(RT == MVT::i32);
  // This is only to produce a value of type i64. Do not rely on the
  // high bits produced by this.
  const SDLoc &dl(N);
  SDValue Ops[] = {
    CurDAG->getTargetConstant(Hexagon::DoubleRegsRegClassID, dl, MVT::i32),
    R, CurDAG->getTargetConstant(Hexagon::isub_hi, dl, MVT::i32),
    R, CurDAG->getTargetConstant(Hexagon::isub_lo, dl, MVT::i32)
  };
  SDNode *T = CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, dl,
                                     MVT::i64, Ops);
  R = SDValue(T, 0);
  return true;
}

bool HexagonDAGToDAGISel::keepsLowBits(const SDValue &Val, unsigned NumBits,
      SDValue &Src) {
  unsigned Opc = Val.getOpcode();
  switch (Opc) {
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND: {
    const SDValue &Op0 = Val.getOperand(0);
    EVT T = Op0.getValueType();
    if (T.isInteger() && T.getSizeInBits() == NumBits) {
      Src = Op0;
      return true;
    }
    break;
  }
  case ISD::SIGN_EXTEND_INREG:
  case ISD::AssertSext:
  case ISD::AssertZext:
    if (Val.getOperand(0).getValueType().isInteger()) {
      VTSDNode *T = cast<VTSDNode>(Val.getOperand(1));
      if (T->getVT().getSizeInBits() == NumBits) {
        Src = Val.getOperand(0);
        return true;
      }
    }
    break;
  case ISD::AND: {
    // Check if this is an AND with NumBits of lower bits set to 1.
    uint64_t Mask = (1ULL << NumBits) - 1;
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Val.getOperand(0))) {
      if (C->getZExtValue() == Mask) {
        Src = Val.getOperand(1);
        return true;
      }
    }
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Val.getOperand(1))) {
      if (C->getZExtValue() == Mask) {
        Src = Val.getOperand(0);
        return true;
      }
    }
    break;
  }
  case ISD::OR:
  case ISD::XOR: {
    // OR/XOR with the lower NumBits bits set to 0.
    uint64_t Mask = (1ULL << NumBits) - 1;
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Val.getOperand(0))) {
      if ((C->getZExtValue() & Mask) == 0) {
        Src = Val.getOperand(1);
        return true;
      }
    }
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Val.getOperand(1))) {
      if ((C->getZExtValue() & Mask) == 0) {
        Src = Val.getOperand(0);
        return true;
      }
    }
    break;
  }
  default:
    break;
  }
  return false;
}

bool HexagonDAGToDAGISel::isAlignedMemNode(const MemSDNode *N) const {
  return N->getAlign().value() >= N->getMemoryVT().getStoreSize();
}

bool HexagonDAGToDAGISel::isSmallStackStore(const StoreSDNode *N) const {
  unsigned StackSize = MF->getFrameInfo().estimateStackSize(*MF);
  switch (N->getMemoryVT().getStoreSize()) {
    case 1:
      return StackSize <= 56;   // 1*2^6 - 8
    case 2:
      return StackSize <= 120;  // 2*2^6 - 8
    case 4:
      return StackSize <= 248;  // 4*2^6 - 8
    default:
      return false;
  }
}

// Return true when the given node fits in a positive half word.
bool HexagonDAGToDAGISel::isPositiveHalfWord(const SDNode *N) const {
  if (const ConstantSDNode *CN = dyn_cast<const ConstantSDNode>(N)) {
    int64_t V = CN->getSExtValue();
    return V > 0 && isInt<16>(V);
  }
  if (N->getOpcode() == ISD::SIGN_EXTEND_INREG) {
    const VTSDNode *VN = dyn_cast<const VTSDNode>(N->getOperand(1));
    return VN->getVT().getSizeInBits() <= 16;
  }
  return false;
}

bool HexagonDAGToDAGISel::hasOneUse(const SDNode *N) const {
  return !CheckSingleUse || N->hasOneUse();
}

////////////////////////////////////////////////////////////////////////////////
// Rebalancing of address calculation trees

static bool isOpcodeHandled(const SDNode *N) {
  switch (N->getOpcode()) {
    case ISD::ADD:
    case ISD::MUL:
      return true;
    case ISD::SHL:
      // We only handle constant shifts because these can be easily flattened
      // into multiplications by 2^Op1.
      return isa<ConstantSDNode>(N->getOperand(1).getNode());
    default:
      return false;
  }
}

/// Return the weight of an SDNode
int HexagonDAGToDAGISel::getWeight(SDNode *N) {
  if (!isOpcodeHandled(N))
    return 1;
  assert(RootWeights.count(N) && "Cannot get weight of unseen root!");
  assert(RootWeights[N] != -1 && "Cannot get weight of unvisited root!");
  assert(RootWeights[N] != -2 && "Cannot get weight of RAWU'd root!");
  return RootWeights[N];
}

int HexagonDAGToDAGISel::getHeight(SDNode *N) {
  if (!isOpcodeHandled(N))
    return 0;
  assert(RootWeights.count(N) && RootWeights[N] >= 0 &&
      "Cannot query height of unvisited/RAUW'd node!");
  return RootHeights[N];
}

namespace {
struct WeightedLeaf {
  SDValue Value;
  int Weight;
  int InsertionOrder;

  WeightedLeaf() {}

  WeightedLeaf(SDValue Value, int Weight, int InsertionOrder) :
    Value(Value), Weight(Weight), InsertionOrder(InsertionOrder) {
    assert(Weight >= 0 && "Weight must be >= 0");
  }

  static bool Compare(const WeightedLeaf &A, const WeightedLeaf &B) {
    assert(A.Value.getNode() && B.Value.getNode());
    return A.Weight == B.Weight ?
            (A.InsertionOrder > B.InsertionOrder) :
            (A.Weight > B.Weight);
  }
};

/// A specialized priority queue for WeigthedLeaves. It automatically folds
/// constants and allows removal of non-top elements while maintaining the
/// priority order.
class LeafPrioQueue {
  SmallVector<WeightedLeaf, 8> Q;
  bool HaveConst;
  WeightedLeaf ConstElt;
  unsigned Opcode;

public:
  bool empty() {
    return (!HaveConst && Q.empty());
  }

  size_t size() {
    return Q.size() + HaveConst;
  }

  bool hasConst() {
    return HaveConst;
  }

  const WeightedLeaf &top() {
    if (HaveConst)
      return ConstElt;
    return Q.front();
  }

  WeightedLeaf pop() {
    if (HaveConst) {
      HaveConst = false;
      return ConstElt;
    }
    std::pop_heap(Q.begin(), Q.end(), WeightedLeaf::Compare);
    return Q.pop_back_val();
  }

  void push(WeightedLeaf L, bool SeparateConst=true) {
    if (!HaveConst && SeparateConst && isa<ConstantSDNode>(L.Value)) {
      if (Opcode == ISD::MUL &&
          cast<ConstantSDNode>(L.Value)->getSExtValue() == 1)
        return;
      if (Opcode == ISD::ADD &&
          cast<ConstantSDNode>(L.Value)->getSExtValue() == 0)
        return;

      HaveConst = true;
      ConstElt = L;
    } else {
      Q.push_back(L);
      std::push_heap(Q.begin(), Q.end(), WeightedLeaf::Compare);
    }
  }

  /// Push L to the bottom of the queue regardless of its weight. If L is
  /// constant, it will not be folded with other constants in the queue.
  void pushToBottom(WeightedLeaf L) {
    L.Weight = 1000;
    push(L, false);
  }

  /// Search for a SHL(x, [<=MaxAmount]) subtree in the queue, return the one of
  /// lowest weight and remove it from the queue.
  WeightedLeaf findSHL(uint64_t MaxAmount);

  WeightedLeaf findMULbyConst();

  LeafPrioQueue(unsigned Opcode) :
    HaveConst(false), Opcode(Opcode) { }
};
} // end anonymous namespace

WeightedLeaf LeafPrioQueue::findSHL(uint64_t MaxAmount) {
  int ResultPos;
  WeightedLeaf Result;

  for (int Pos = 0, End = Q.size(); Pos != End; ++Pos) {
    const WeightedLeaf &L = Q[Pos];
    const SDValue &Val = L.Value;
    if (Val.getOpcode() != ISD::SHL ||
        !isa<ConstantSDNode>(Val.getOperand(1)) ||
        Val.getConstantOperandVal(1) > MaxAmount)
      continue;
    if (!Result.Value.getNode() || Result.Weight > L.Weight ||
        (Result.Weight == L.Weight && Result.InsertionOrder > L.InsertionOrder))
    {
      Result = L;
      ResultPos = Pos;
    }
  }

  if (Result.Value.getNode()) {
    Q.erase(&Q[ResultPos]);
    std::make_heap(Q.begin(), Q.end(), WeightedLeaf::Compare);
  }

  return Result;
}

WeightedLeaf LeafPrioQueue::findMULbyConst() {
  int ResultPos;
  WeightedLeaf Result;

  for (int Pos = 0, End = Q.size(); Pos != End; ++Pos) {
    const WeightedLeaf &L = Q[Pos];
    const SDValue &Val = L.Value;
    if (Val.getOpcode() != ISD::MUL ||
        !isa<ConstantSDNode>(Val.getOperand(1)) ||
        Val.getConstantOperandVal(1) > 127)
      continue;
    if (!Result.Value.getNode() || Result.Weight > L.Weight ||
        (Result.Weight == L.Weight && Result.InsertionOrder > L.InsertionOrder))
    {
      Result = L;
      ResultPos = Pos;
    }
  }

  if (Result.Value.getNode()) {
    Q.erase(&Q[ResultPos]);
    std::make_heap(Q.begin(), Q.end(), WeightedLeaf::Compare);
  }

  return Result;
}

SDValue HexagonDAGToDAGISel::getMultiplierForSHL(SDNode *N) {
  uint64_t MulFactor = 1ull << N->getConstantOperandVal(1);
  return CurDAG->getConstant(MulFactor, SDLoc(N),
                             N->getOperand(1).getValueType());
}

/// @returns the value x for which 2^x is a factor of Val
static unsigned getPowerOf2Factor(SDValue Val) {
  if (Val.getOpcode() == ISD::MUL) {
    unsigned MaxFactor = 0;
    for (int i = 0; i < 2; ++i) {
      ConstantSDNode *C = dyn_cast<ConstantSDNode>(Val.getOperand(i));
      if (!C)
        continue;
      const APInt &CInt = C->getAPIntValue();
      if (CInt.getBoolValue())
        MaxFactor = CInt.countr_zero();
    }
    return MaxFactor;
  }
  if (Val.getOpcode() == ISD::SHL) {
    if (!isa<ConstantSDNode>(Val.getOperand(1).getNode()))
      return 0;
    return (unsigned) Val.getConstantOperandVal(1);
  }

  return 0;
}

/// @returns true if V>>Amount will eliminate V's operation on its child
static bool willShiftRightEliminate(SDValue V, unsigned Amount) {
  if (V.getOpcode() == ISD::MUL) {
    SDValue Ops[] = { V.getOperand(0), V.getOperand(1) };
    for (int i = 0; i < 2; ++i)
      if (isa<ConstantSDNode>(Ops[i].getNode()) &&
          V.getConstantOperandVal(i) % (1ULL << Amount) == 0) {
        uint64_t NewConst = V.getConstantOperandVal(i) >> Amount;
        return (NewConst == 1);
      }
  } else if (V.getOpcode() == ISD::SHL) {
    return (Amount == V.getConstantOperandVal(1));
  }

  return false;
}

SDValue HexagonDAGToDAGISel::factorOutPowerOf2(SDValue V, unsigned Power) {
  SDValue Ops[] = { V.getOperand(0), V.getOperand(1) };
  if (V.getOpcode() == ISD::MUL) {
    for (int i=0; i < 2; ++i) {
      if (isa<ConstantSDNode>(Ops[i].getNode()) &&
          V.getConstantOperandVal(i) % ((uint64_t)1 << Power) == 0) {
        uint64_t NewConst = V.getConstantOperandVal(i) >> Power;
        if (NewConst == 1)
          return Ops[!i];
        Ops[i] = CurDAG->getConstant(NewConst,
                                     SDLoc(V), V.getValueType());
        break;
      }
    }
  } else if (V.getOpcode() == ISD::SHL) {
    uint64_t ShiftAmount = V.getConstantOperandVal(1);
    if (ShiftAmount == Power)
      return Ops[0];
    Ops[1] = CurDAG->getConstant(ShiftAmount - Power,
                                 SDLoc(V), V.getValueType());
  }

  return CurDAG->getNode(V.getOpcode(), SDLoc(V), V.getValueType(), Ops);
}

static bool isTargetConstant(const SDValue &V) {
  return V.getOpcode() == HexagonISD::CONST32 ||
         V.getOpcode() == HexagonISD::CONST32_GP;
}

unsigned HexagonDAGToDAGISel::getUsesInFunction(const Value *V) {
  if (GAUsesInFunction.count(V))
    return GAUsesInFunction[V];

  unsigned Result = 0;
  const Function &CurF = CurDAG->getMachineFunction().getFunction();
  for (const User *U : V->users()) {
    if (isa<Instruction>(U) &&
        cast<Instruction>(U)->getParent()->getParent() == &CurF)
      ++Result;
  }

  GAUsesInFunction[V] = Result;

  return Result;
}

/// Note - After calling this, N may be dead. It may have been replaced by a
/// new node, so always use the returned value in place of N.
///
/// @returns The SDValue taking the place of N (which could be N if it is
/// unchanged)
SDValue HexagonDAGToDAGISel::balanceSubTree(SDNode *N, bool TopLevel) {
  assert(RootWeights.count(N) && "Cannot balance non-root node.");
  assert(RootWeights[N] != -2 && "This node was RAUW'd!");
  assert(!TopLevel || N->getOpcode() == ISD::ADD);

  // Return early if this node was already visited
  if (RootWeights[N] != -1)
    return SDValue(N, 0);

  assert(isOpcodeHandled(N));

  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);

  // Return early if the operands will remain unchanged or are all roots
  if ((!isOpcodeHandled(Op0.getNode()) || RootWeights.count(Op0.getNode())) &&
      (!isOpcodeHandled(Op1.getNode()) || RootWeights.count(Op1.getNode()))) {
    SDNode *Op0N = Op0.getNode();
    int Weight;
    if (isOpcodeHandled(Op0N) && RootWeights[Op0N] == -1) {
      Weight = getWeight(balanceSubTree(Op0N).getNode());
      // Weight = calculateWeight(Op0N);
    } else
      Weight = getWeight(Op0N);

    SDNode *Op1N = N->getOperand(1).getNode(); // Op1 may have been RAUWd
    if (isOpcodeHandled(Op1N) && RootWeights[Op1N] == -1) {
      Weight += getWeight(balanceSubTree(Op1N).getNode());
      // Weight += calculateWeight(Op1N);
    } else
      Weight += getWeight(Op1N);

    RootWeights[N] = Weight;
    RootHeights[N] = std::max(getHeight(N->getOperand(0).getNode()),
                              getHeight(N->getOperand(1).getNode())) + 1;

    LLVM_DEBUG(dbgs() << "--> No need to balance root (Weight=" << Weight
                      << " Height=" << RootHeights[N] << "): ");
    LLVM_DEBUG(N->dump(CurDAG));

    return SDValue(N, 0);
  }

  LLVM_DEBUG(dbgs() << "** Balancing root node: ");
  LLVM_DEBUG(N->dump(CurDAG));

  unsigned NOpcode = N->getOpcode();

  LeafPrioQueue Leaves(NOpcode);
  SmallVector<SDValue, 4> Worklist;
  Worklist.push_back(SDValue(N, 0));

  // SHL nodes will be converted to MUL nodes
  if (NOpcode == ISD::SHL)
    NOpcode = ISD::MUL;

  bool CanFactorize = false;
  WeightedLeaf Mul1, Mul2;
  unsigned MaxPowerOf2 = 0;
  WeightedLeaf GA;

  // Do not try to factor out a shift if there is already a shift at the tip of
  // the tree.
  bool HaveTopLevelShift = false;
  if (TopLevel &&
      ((isOpcodeHandled(Op0.getNode()) && Op0.getOpcode() == ISD::SHL &&
                        Op0.getConstantOperandVal(1) < 4) ||
       (isOpcodeHandled(Op1.getNode()) && Op1.getOpcode() == ISD::SHL &&
                        Op1.getConstantOperandVal(1) < 4)))
    HaveTopLevelShift = true;

  // Flatten the subtree into an ordered list of leaves; at the same time
  // determine whether the tree is already balanced.
  int InsertionOrder = 0;
  SmallDenseMap<SDValue, int> NodeHeights;
  bool Imbalanced = false;
  int CurrentWeight = 0;
  while (!Worklist.empty()) {
    SDValue Child = Worklist.pop_back_val();

    if (Child.getNode() != N && RootWeights.count(Child.getNode())) {
      // CASE 1: Child is a root note

      int Weight = RootWeights[Child.getNode()];
      if (Weight == -1) {
        Child = balanceSubTree(Child.getNode());
        // calculateWeight(Child.getNode());
        Weight = getWeight(Child.getNode());
      } else if (Weight == -2) {
        // Whoops, this node was RAUWd by one of the balanceSubTree calls we
        // made. Our worklist isn't up to date anymore.
        // Restart the whole process.
        LLVM_DEBUG(dbgs() << "--> Subtree was RAUWd. Restarting...\n");
        return balanceSubTree(N, TopLevel);
      }

      NodeHeights[Child] = 1;
      CurrentWeight += Weight;

      unsigned PowerOf2;
      if (TopLevel && !CanFactorize && !HaveTopLevelShift &&
          (Child.getOpcode() == ISD::MUL || Child.getOpcode() == ISD::SHL) &&
          Child.hasOneUse() && (PowerOf2 = getPowerOf2Factor(Child))) {
        // Try to identify two factorizable MUL/SHL children greedily. Leave
        // them out of the priority queue for now so we can deal with them
        // after.
        if (!Mul1.Value.getNode()) {
          Mul1 = WeightedLeaf(Child, Weight, InsertionOrder++);
          MaxPowerOf2 = PowerOf2;
        } else {
          Mul2 = WeightedLeaf(Child, Weight, InsertionOrder++);
          MaxPowerOf2 = std::min(MaxPowerOf2, PowerOf2);

          // Our addressing modes can only shift by a maximum of 3
          if (MaxPowerOf2 > 3)
            MaxPowerOf2 = 3;

          CanFactorize = true;
        }
      } else
        Leaves.push(WeightedLeaf(Child, Weight, InsertionOrder++));
    } else if (!isOpcodeHandled(Child.getNode())) {
      // CASE 2: Child is an unhandled kind of node (e.g. constant)
      int Weight = getWeight(Child.getNode());

      NodeHeights[Child] = getHeight(Child.getNode());
      CurrentWeight += Weight;

      if (isTargetConstant(Child) && !GA.Value.getNode())
        GA = WeightedLeaf(Child, Weight, InsertionOrder++);
      else
        Leaves.push(WeightedLeaf(Child, Weight, InsertionOrder++));
    } else {
      // CASE 3: Child is a subtree of same opcode
      // Visit children first, then flatten.
      unsigned ChildOpcode = Child.getOpcode();
      assert(ChildOpcode == NOpcode ||
             (NOpcode == ISD::MUL && ChildOpcode == ISD::SHL));

      // Convert SHL to MUL
      SDValue Op1;
      if (ChildOpcode == ISD::SHL)
        Op1 = getMultiplierForSHL(Child.getNode());
      else
        Op1 = Child->getOperand(1);

      if (!NodeHeights.count(Op1) || !NodeHeights.count(Child->getOperand(0))) {
        assert(!NodeHeights.count(Child) && "Parent visited before children?");
        // Visit children first, then re-visit this node
        Worklist.push_back(Child);
        Worklist.push_back(Op1);
        Worklist.push_back(Child->getOperand(0));
      } else {
        // Back at this node after visiting the children
        if (std::abs(NodeHeights[Op1] - NodeHeights[Child->getOperand(0)]) > 1)
          Imbalanced = true;

        NodeHeights[Child] = std::max(NodeHeights[Op1],
                                      NodeHeights[Child->getOperand(0)]) + 1;
      }
    }
  }

  LLVM_DEBUG(dbgs() << "--> Current height=" << NodeHeights[SDValue(N, 0)]
                    << " weight=" << CurrentWeight
                    << " imbalanced=" << Imbalanced << "\n");

  // Transform MUL(x, C * 2^Y) + SHL(z, Y) -> SHL(ADD(MUL(x, C), z), Y)
  //  This factors out a shift in order to match memw(a<<Y+b).
  if (CanFactorize && (willShiftRightEliminate(Mul1.Value, MaxPowerOf2) ||
                       willShiftRightEliminate(Mul2.Value, MaxPowerOf2))) {
    LLVM_DEBUG(dbgs() << "--> Found common factor for two MUL children!\n");
    int Weight = Mul1.Weight + Mul2.Weight;
    int Height = std::max(NodeHeights[Mul1.Value], NodeHeights[Mul2.Value]) + 1;
    SDValue Mul1Factored = factorOutPowerOf2(Mul1.Value, MaxPowerOf2);
    SDValue Mul2Factored = factorOutPowerOf2(Mul2.Value, MaxPowerOf2);
    SDValue Sum = CurDAG->getNode(ISD::ADD, SDLoc(N), Mul1.Value.getValueType(),
                                  Mul1Factored, Mul2Factored);
    SDValue Const = CurDAG->getConstant(MaxPowerOf2, SDLoc(N),
                                        Mul1.Value.getValueType());
    SDValue New = CurDAG->getNode(ISD::SHL, SDLoc(N), Mul1.Value.getValueType(),
                                  Sum, Const);
    NodeHeights[New] = Height;
    Leaves.push(WeightedLeaf(New, Weight, Mul1.InsertionOrder));
  } else if (Mul1.Value.getNode()) {
    // We failed to factorize two MULs, so now the Muls are left outside the
    // queue... add them back.
    Leaves.push(Mul1);
    if (Mul2.Value.getNode())
      Leaves.push(Mul2);
    CanFactorize = false;
  }

  // Combine GA + Constant -> GA+Offset, but only if GA is not used elsewhere
  // and the root node itself is not used more than twice. This reduces the
  // amount of additional constant extenders introduced by this optimization.
  bool CombinedGA = false;
  if (NOpcode == ISD::ADD && GA.Value.getNode() && Leaves.hasConst() &&
      GA.Value.hasOneUse() && N->use_size() < 3) {
    GlobalAddressSDNode *GANode =
      cast<GlobalAddressSDNode>(GA.Value.getOperand(0));
    ConstantSDNode *Offset = cast<ConstantSDNode>(Leaves.top().Value);

    if (getUsesInFunction(GANode->getGlobal()) == 1 && Offset->hasOneUse() &&
        getTargetLowering()->isOffsetFoldingLegal(GANode)) {
      LLVM_DEBUG(dbgs() << "--> Combining GA and offset ("
                        << Offset->getSExtValue() << "): ");
      LLVM_DEBUG(GANode->dump(CurDAG));

      SDValue NewTGA =
        CurDAG->getTargetGlobalAddress(GANode->getGlobal(), SDLoc(GA.Value),
            GANode->getValueType(0),
            GANode->getOffset() + (uint64_t)Offset->getSExtValue());
      GA.Value = CurDAG->getNode(GA.Value.getOpcode(), SDLoc(GA.Value),
          GA.Value.getValueType(), NewTGA);
      GA.Weight += Leaves.top().Weight;

      NodeHeights[GA.Value] = getHeight(GA.Value.getNode());
      CombinedGA = true;

      Leaves.pop(); // Remove the offset constant from the queue
    }
  }

  if ((RebalanceOnlyForOptimizations && !CanFactorize && !CombinedGA) ||
      (RebalanceOnlyImbalancedTrees && !Imbalanced)) {
    RootWeights[N] = CurrentWeight;
    RootHeights[N] = NodeHeights[SDValue(N, 0)];

    return SDValue(N, 0);
  }

  // Combine GA + SHL(x, C<=31) so we will match Rx=add(#u8,asl(Rx,#U5))
  if (NOpcode == ISD::ADD && GA.Value.getNode()) {
    WeightedLeaf SHL = Leaves.findSHL(31);
    if (SHL.Value.getNode()) {
      int Height = std::max(NodeHeights[GA.Value], NodeHeights[SHL.Value]) + 1;
      GA.Value = CurDAG->getNode(ISD::ADD, SDLoc(GA.Value),
                                 GA.Value.getValueType(),
                                 GA.Value, SHL.Value);
      GA.Weight = SHL.Weight; // Specifically ignore the GA weight here
      NodeHeights[GA.Value] = Height;
    }
  }

  if (GA.Value.getNode())
    Leaves.push(GA);

  // If this is the top level and we haven't factored out a shift, we should try
  // to move a constant to the bottom to match addressing modes like memw(rX+C)
  if (TopLevel && !CanFactorize && Leaves.hasConst()) {
    LLVM_DEBUG(dbgs() << "--> Pushing constant to tip of tree.");
    Leaves.pushToBottom(Leaves.pop());
  }

  const DataLayout &DL = CurDAG->getDataLayout();
  const TargetLowering &TLI = *getTargetLowering();

  // Rebuild the tree using Huffman's algorithm
  while (Leaves.size() > 1) {
    WeightedLeaf L0 = Leaves.pop();

    // See whether we can grab a MUL to form an add(Rx,mpyi(Ry,#u6)),
    // otherwise just get the next leaf
    WeightedLeaf L1 = Leaves.findMULbyConst();
    if (!L1.Value.getNode())
      L1 = Leaves.pop();

    assert(L0.Weight <= L1.Weight && "Priority queue is broken!");

    SDValue V0 = L0.Value;
    int V0Weight = L0.Weight;
    SDValue V1 = L1.Value;
    int V1Weight = L1.Weight;

    // Make sure that none of these nodes have been RAUW'd
    if ((RootWeights.count(V0.getNode()) && RootWeights[V0.getNode()] == -2) ||
        (RootWeights.count(V1.getNode()) && RootWeights[V1.getNode()] == -2)) {
      LLVM_DEBUG(dbgs() << "--> Subtree was RAUWd. Restarting...\n");
      return balanceSubTree(N, TopLevel);
    }

    ConstantSDNode *V0C = dyn_cast<ConstantSDNode>(V0);
    ConstantSDNode *V1C = dyn_cast<ConstantSDNode>(V1);
    EVT VT = N->getValueType(0);
    SDValue NewNode;

    if (V0C && !V1C) {
      std::swap(V0, V1);
      std::swap(V0C, V1C);
    }

    // Calculate height of this node
    assert(NodeHeights.count(V0) && NodeHeights.count(V1) &&
           "Children must have been visited before re-combining them!");
    int Height = std::max(NodeHeights[V0], NodeHeights[V1]) + 1;

    // Rebuild this node (and restore SHL from MUL if needed)
    if (V1C && NOpcode == ISD::MUL && V1C->getAPIntValue().isPowerOf2())
      NewNode = CurDAG->getNode(
          ISD::SHL, SDLoc(V0), VT, V0,
          CurDAG->getConstant(
              V1C->getAPIntValue().logBase2(), SDLoc(N),
              TLI.getScalarShiftAmountTy(DL, V0.getValueType())));
    else
      NewNode = CurDAG->getNode(NOpcode, SDLoc(N), VT, V0, V1);

    NodeHeights[NewNode] = Height;

    int Weight = V0Weight + V1Weight;
    Leaves.push(WeightedLeaf(NewNode, Weight, L0.InsertionOrder));

    LLVM_DEBUG(dbgs() << "--> Built new node (Weight=" << Weight
                      << ",Height=" << Height << "):\n");
    LLVM_DEBUG(NewNode.dump());
  }

  assert(Leaves.size() == 1);
  SDValue NewRoot = Leaves.top().Value;

  assert(NodeHeights.count(NewRoot));
  int Height = NodeHeights[NewRoot];

  // Restore SHL if we earlier converted it to a MUL
  if (NewRoot.getOpcode() == ISD::MUL) {
    ConstantSDNode *V1C = dyn_cast<ConstantSDNode>(NewRoot.getOperand(1));
    if (V1C && V1C->getAPIntValue().isPowerOf2()) {
      EVT VT = NewRoot.getValueType();
      SDValue V0 = NewRoot.getOperand(0);
      NewRoot = CurDAG->getNode(
          ISD::SHL, SDLoc(NewRoot), VT, V0,
          CurDAG->getConstant(
              V1C->getAPIntValue().logBase2(), SDLoc(NewRoot),
              TLI.getScalarShiftAmountTy(DL, V0.getValueType())));
    }
  }

  if (N != NewRoot.getNode()) {
    LLVM_DEBUG(dbgs() << "--> Root is now: ");
    LLVM_DEBUG(NewRoot.dump());

    // Replace all uses of old root by new root
    CurDAG->ReplaceAllUsesWith(N, NewRoot.getNode());
    // Mark that we have RAUW'd N
    RootWeights[N] = -2;
  } else {
    LLVM_DEBUG(dbgs() << "--> Root unchanged.\n");
  }

  RootWeights[NewRoot.getNode()] = Leaves.top().Weight;
  RootHeights[NewRoot.getNode()] = Height;

  return NewRoot;
}

void HexagonDAGToDAGISel::rebalanceAddressTrees() {
  for (SDNode &Node : llvm::make_early_inc_range(CurDAG->allnodes())) {
    SDNode *N = &Node;
    if (N->getOpcode() != ISD::LOAD && N->getOpcode() != ISD::STORE)
      continue;

    SDValue BasePtr = cast<MemSDNode>(N)->getBasePtr();
    if (BasePtr.getOpcode() != ISD::ADD)
      continue;

    // We've already processed this node
    if (RootWeights.count(BasePtr.getNode()))
      continue;

    LLVM_DEBUG(dbgs() << "** Rebalancing address calculation in node: ");
    LLVM_DEBUG(N->dump(CurDAG));

    // FindRoots
    SmallVector<SDNode *, 4> Worklist;

    Worklist.push_back(BasePtr.getOperand(0).getNode());
    Worklist.push_back(BasePtr.getOperand(1).getNode());

    while (!Worklist.empty()) {
      SDNode *N = Worklist.pop_back_val();
      unsigned Opcode = N->getOpcode();

      if (!isOpcodeHandled(N))
        continue;

      Worklist.push_back(N->getOperand(0).getNode());
      Worklist.push_back(N->getOperand(1).getNode());

      // Not a root if it has only one use and same opcode as its parent
      if (N->hasOneUse() && Opcode == N->use_begin()->getOpcode())
        continue;

      // This root node has already been processed
      if (RootWeights.count(N))
        continue;

      RootWeights[N] = -1;
    }

    // Balance node itself
    RootWeights[BasePtr.getNode()] = -1;
    SDValue NewBasePtr = balanceSubTree(BasePtr.getNode(), /*TopLevel=*/ true);

    if (N->getOpcode() == ISD::LOAD)
      N = CurDAG->UpdateNodeOperands(N, N->getOperand(0),
            NewBasePtr, N->getOperand(2));
    else
      N = CurDAG->UpdateNodeOperands(N, N->getOperand(0), N->getOperand(1),
            NewBasePtr, N->getOperand(3));

    LLVM_DEBUG(dbgs() << "--> Final node: ");
    LLVM_DEBUG(N->dump(CurDAG));
  }

  CurDAG->RemoveDeadNodes();
  GAUsesInFunction.clear();
  RootHeights.clear();
  RootWeights.clear();
}
