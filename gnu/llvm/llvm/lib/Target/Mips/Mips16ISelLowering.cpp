//===-- Mips16ISelLowering.h - Mips16 DAG Lowering Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Subclass of MipsTargetLowering specialized for mips16.
//
//===----------------------------------------------------------------------===//
#include "Mips16ISelLowering.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "Mips16HardFloatInfo.h"
#include "MipsMachineFunction.h"
#include "MipsRegisterInfo.h"
#include "MipsTargetMachine.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "mips-lower"

static cl::opt<bool> DontExpandCondPseudos16(
  "mips16-dont-expand-cond-pseudo",
  cl::init(false),
  cl::desc("Don't expand conditional move related "
           "pseudos for Mips 16"),
  cl::Hidden);

namespace {
struct Mips16Libcall {
  RTLIB::Libcall Libcall;
  const char *Name;

  bool operator<(const Mips16Libcall &RHS) const {
    return std::strcmp(Name, RHS.Name) < 0;
  }
};

struct Mips16IntrinsicHelperType{
  const char* Name;
  const char* Helper;

  bool operator<(const Mips16IntrinsicHelperType &RHS) const {
    return std::strcmp(Name, RHS.Name) < 0;
  }
  bool operator==(const Mips16IntrinsicHelperType &RHS) const {
    return std::strcmp(Name, RHS.Name) == 0;
  }
};
}

// Libcalls for which no helper is generated. Sorted by name for binary search.
static const Mips16Libcall HardFloatLibCalls[] = {
  { RTLIB::ADD_F64, "__mips16_adddf3" },
  { RTLIB::ADD_F32, "__mips16_addsf3" },
  { RTLIB::DIV_F64, "__mips16_divdf3" },
  { RTLIB::DIV_F32, "__mips16_divsf3" },
  { RTLIB::OEQ_F64, "__mips16_eqdf2" },
  { RTLIB::OEQ_F32, "__mips16_eqsf2" },
  { RTLIB::FPEXT_F32_F64, "__mips16_extendsfdf2" },
  { RTLIB::FPTOSINT_F64_I32, "__mips16_fix_truncdfsi" },
  { RTLIB::FPTOSINT_F32_I32, "__mips16_fix_truncsfsi" },
  { RTLIB::SINTTOFP_I32_F64, "__mips16_floatsidf" },
  { RTLIB::SINTTOFP_I32_F32, "__mips16_floatsisf" },
  { RTLIB::UINTTOFP_I32_F64, "__mips16_floatunsidf" },
  { RTLIB::UINTTOFP_I32_F32, "__mips16_floatunsisf" },
  { RTLIB::OGE_F64, "__mips16_gedf2" },
  { RTLIB::OGE_F32, "__mips16_gesf2" },
  { RTLIB::OGT_F64, "__mips16_gtdf2" },
  { RTLIB::OGT_F32, "__mips16_gtsf2" },
  { RTLIB::OLE_F64, "__mips16_ledf2" },
  { RTLIB::OLE_F32, "__mips16_lesf2" },
  { RTLIB::OLT_F64, "__mips16_ltdf2" },
  { RTLIB::OLT_F32, "__mips16_ltsf2" },
  { RTLIB::MUL_F64, "__mips16_muldf3" },
  { RTLIB::MUL_F32, "__mips16_mulsf3" },
  { RTLIB::UNE_F64, "__mips16_nedf2" },
  { RTLIB::UNE_F32, "__mips16_nesf2" },
  { RTLIB::UNKNOWN_LIBCALL, "__mips16_ret_dc" }, // No associated libcall.
  { RTLIB::UNKNOWN_LIBCALL, "__mips16_ret_df" }, // No associated libcall.
  { RTLIB::UNKNOWN_LIBCALL, "__mips16_ret_sc" }, // No associated libcall.
  { RTLIB::UNKNOWN_LIBCALL, "__mips16_ret_sf" }, // No associated libcall.
  { RTLIB::SUB_F64, "__mips16_subdf3" },
  { RTLIB::SUB_F32, "__mips16_subsf3" },
  { RTLIB::FPROUND_F64_F32, "__mips16_truncdfsf2" },
  { RTLIB::UO_F64, "__mips16_unorddf2" },
  { RTLIB::UO_F32, "__mips16_unordsf2" }
};

static const Mips16IntrinsicHelperType Mips16IntrinsicHelper[] = {
  {"__fixunsdfsi", "__mips16_call_stub_2" },
  {"ceil",  "__mips16_call_stub_df_2"},
  {"ceilf", "__mips16_call_stub_sf_1"},
  {"copysign",  "__mips16_call_stub_df_10"},
  {"copysignf", "__mips16_call_stub_sf_5"},
  {"cos",  "__mips16_call_stub_df_2"},
  {"cosf", "__mips16_call_stub_sf_1"},
  {"exp2",  "__mips16_call_stub_df_2"},
  {"exp2f", "__mips16_call_stub_sf_1"},
  {"floor",  "__mips16_call_stub_df_2"},
  {"floorf", "__mips16_call_stub_sf_1"},
  {"log2",  "__mips16_call_stub_df_2"},
  {"log2f", "__mips16_call_stub_sf_1"},
  {"nearbyint",  "__mips16_call_stub_df_2"},
  {"nearbyintf", "__mips16_call_stub_sf_1"},
  {"rint",  "__mips16_call_stub_df_2"},
  {"rintf", "__mips16_call_stub_sf_1"},
  {"sin",  "__mips16_call_stub_df_2"},
  {"sinf", "__mips16_call_stub_sf_1"},
  {"sqrt",  "__mips16_call_stub_df_2"},
  {"sqrtf", "__mips16_call_stub_sf_1"},
  {"trunc",  "__mips16_call_stub_df_2"},
  {"truncf", "__mips16_call_stub_sf_1"},
};

Mips16TargetLowering::Mips16TargetLowering(const MipsTargetMachine &TM,
                                           const MipsSubtarget &STI)
    : MipsTargetLowering(TM, STI) {

  // Set up the register classes
  addRegisterClass(MVT::i32, &Mips::CPU16RegsRegClass);

  if (!Subtarget.useSoftFloat())
    setMips16HardFloatLibCalls();

  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, LibCall);
  setOperationAction(ISD::ATOMIC_CMP_SWAP, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_SWAP, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_ADD, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_SUB, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_AND, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_OR, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_XOR, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_NAND, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_MIN, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_MAX, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_UMIN, MVT::i32, LibCall);
  setOperationAction(ISD::ATOMIC_LOAD_UMAX, MVT::i32, LibCall);

  setOperationAction(ISD::ROTR, MVT::i32,  Expand);
  setOperationAction(ISD::ROTR, MVT::i64,  Expand);
  setOperationAction(ISD::BSWAP, MVT::i32, Expand);
  setOperationAction(ISD::BSWAP, MVT::i64, Expand);

  computeRegisterProperties(STI.getRegisterInfo());
}

const MipsTargetLowering *
llvm::createMips16TargetLowering(const MipsTargetMachine &TM,
                                 const MipsSubtarget &STI) {
  return new Mips16TargetLowering(TM, STI);
}

bool Mips16TargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned, Align, MachineMemOperand::Flags, unsigned *Fast) const {
  return false;
}

MachineBasicBlock *
Mips16TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                  MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
    return MipsTargetLowering::EmitInstrWithCustomInserter(MI, BB);
  case Mips::SelBeqZ:
    return emitSel16(Mips::BeqzRxImm16, MI, BB);
  case Mips::SelBneZ:
    return emitSel16(Mips::BnezRxImm16, MI, BB);
  case Mips::SelTBteqZCmpi:
    return emitSeliT16(Mips::Bteqz16, Mips::CmpiRxImmX16, MI, BB);
  case Mips::SelTBteqZSlti:
    return emitSeliT16(Mips::Bteqz16, Mips::SltiRxImmX16, MI, BB);
  case Mips::SelTBteqZSltiu:
    return emitSeliT16(Mips::Bteqz16, Mips::SltiuRxImmX16, MI, BB);
  case Mips::SelTBtneZCmpi:
    return emitSeliT16(Mips::Btnez16, Mips::CmpiRxImmX16, MI, BB);
  case Mips::SelTBtneZSlti:
    return emitSeliT16(Mips::Btnez16, Mips::SltiRxImmX16, MI, BB);
  case Mips::SelTBtneZSltiu:
    return emitSeliT16(Mips::Btnez16, Mips::SltiuRxImmX16, MI, BB);
  case Mips::SelTBteqZCmp:
    return emitSelT16(Mips::Bteqz16, Mips::CmpRxRy16, MI, BB);
  case Mips::SelTBteqZSlt:
    return emitSelT16(Mips::Bteqz16, Mips::SltRxRy16, MI, BB);
  case Mips::SelTBteqZSltu:
    return emitSelT16(Mips::Bteqz16, Mips::SltuRxRy16, MI, BB);
  case Mips::SelTBtneZCmp:
    return emitSelT16(Mips::Btnez16, Mips::CmpRxRy16, MI, BB);
  case Mips::SelTBtneZSlt:
    return emitSelT16(Mips::Btnez16, Mips::SltRxRy16, MI, BB);
  case Mips::SelTBtneZSltu:
    return emitSelT16(Mips::Btnez16, Mips::SltuRxRy16, MI, BB);
  case Mips::BteqzT8CmpX16:
    return emitFEXT_T8I816_ins(Mips::Bteqz16, Mips::CmpRxRy16, MI, BB);
  case Mips::BteqzT8SltX16:
    return emitFEXT_T8I816_ins(Mips::Bteqz16, Mips::SltRxRy16, MI, BB);
  case Mips::BteqzT8SltuX16:
    // TBD: figure out a way to get this or remove the instruction
    // altogether.
    return emitFEXT_T8I816_ins(Mips::Bteqz16, Mips::SltuRxRy16, MI, BB);
  case Mips::BtnezT8CmpX16:
    return emitFEXT_T8I816_ins(Mips::Btnez16, Mips::CmpRxRy16, MI, BB);
  case Mips::BtnezT8SltX16:
    return emitFEXT_T8I816_ins(Mips::Btnez16, Mips::SltRxRy16, MI, BB);
  case Mips::BtnezT8SltuX16:
    // TBD: figure out a way to get this or remove the instruction
    // altogether.
    return emitFEXT_T8I816_ins(Mips::Btnez16, Mips::SltuRxRy16, MI, BB);
  case Mips::BteqzT8CmpiX16: return emitFEXT_T8I8I16_ins(
    Mips::Bteqz16, Mips::CmpiRxImm16, Mips::CmpiRxImmX16, false, MI, BB);
  case Mips::BteqzT8SltiX16: return emitFEXT_T8I8I16_ins(
    Mips::Bteqz16, Mips::SltiRxImm16, Mips::SltiRxImmX16, true, MI, BB);
  case Mips::BteqzT8SltiuX16: return emitFEXT_T8I8I16_ins(
    Mips::Bteqz16, Mips::SltiuRxImm16, Mips::SltiuRxImmX16, false, MI, BB);
  case Mips::BtnezT8CmpiX16: return emitFEXT_T8I8I16_ins(
    Mips::Btnez16, Mips::CmpiRxImm16, Mips::CmpiRxImmX16, false, MI, BB);
  case Mips::BtnezT8SltiX16: return emitFEXT_T8I8I16_ins(
    Mips::Btnez16, Mips::SltiRxImm16, Mips::SltiRxImmX16, true, MI, BB);
  case Mips::BtnezT8SltiuX16: return emitFEXT_T8I8I16_ins(
    Mips::Btnez16, Mips::SltiuRxImm16, Mips::SltiuRxImmX16, false, MI, BB);
    break;
  case Mips::SltCCRxRy16:
    return emitFEXT_CCRX16_ins(Mips::SltRxRy16, MI, BB);
    break;
  case Mips::SltiCCRxImmX16:
    return emitFEXT_CCRXI16_ins
      (Mips::SltiRxImm16, Mips::SltiRxImmX16, MI, BB);
  case Mips::SltiuCCRxImmX16:
    return emitFEXT_CCRXI16_ins
      (Mips::SltiuRxImm16, Mips::SltiuRxImmX16, MI, BB);
  case Mips::SltuCCRxRy16:
    return emitFEXT_CCRX16_ins
      (Mips::SltuRxRy16, MI, BB);
  }
}

bool Mips16TargetLowering::isEligibleForTailCallOptimization(
    const CCState &CCInfo, unsigned NextStackOffset,
    const MipsFunctionInfo &FI) const {
  // No tail call optimization for mips16.
  return false;
}

void Mips16TargetLowering::setMips16HardFloatLibCalls() {
  for (unsigned I = 0; I != std::size(HardFloatLibCalls); ++I) {
    assert((I == 0 || HardFloatLibCalls[I - 1] < HardFloatLibCalls[I]) &&
           "Array not sorted!");
    if (HardFloatLibCalls[I].Libcall != RTLIB::UNKNOWN_LIBCALL)
      setLibcallName(HardFloatLibCalls[I].Libcall, HardFloatLibCalls[I].Name);
  }
}

//
// The Mips16 hard float is a crazy quilt inherited from gcc. I have a much
// cleaner way to do all of this but it will have to wait until the traditional
// gcc mechanism is completed.
//
// For Pic, in order for Mips16 code to call Mips32 code which according the abi
// have either arguments or returned values placed in floating point registers,
// we use a set of helper functions. (This includes functions which return type
//  complex which on Mips are returned in a pair of floating point registers).
//
// This is an encoding that we inherited from gcc.
// In Mips traditional O32, N32 ABI, floating point numbers are passed in
// floating point argument registers 1,2 only when the first and optionally
// the second arguments are float (sf) or double (df).
// For Mips16 we are only concerned with the situations where floating point
// arguments are being passed in floating point registers by the ABI, because
// Mips16 mode code cannot execute floating point instructions to load those
// values and hence helper functions are needed.
// The possibilities are (), (sf), (sf, sf), (sf, df), (df), (df, sf), (df, df)
// the helper function suffixs for these are:
//                        0,  1,    5,        9,         2,   6,        10
// this suffix can then be calculated as follows:
// for a given argument Arg:
//     Arg1x, Arg2x = 1 :  Arg is sf
//                    2 :  Arg is df
//                    0:   Arg is neither sf or df
// So this stub is the string for number Arg1x + Arg2x*4.
// However not all numbers between 0 and 10 are possible, we check anyway and
// assert if the impossible exists.
//

unsigned int Mips16TargetLowering::getMips16HelperFunctionStubNumber
  (ArgListTy &Args) const {
  unsigned int resultNum = 0;
  if (Args.size() >= 1) {
    Type *t = Args[0].Ty;
    if (t->isFloatTy()) {
      resultNum = 1;
    }
    else if (t->isDoubleTy()) {
      resultNum = 2;
    }
  }
  if (resultNum) {
    if (Args.size() >=2) {
      Type *t = Args[1].Ty;
      if (t->isFloatTy()) {
        resultNum += 4;
      }
      else if (t->isDoubleTy()) {
        resultNum += 8;
      }
    }
  }
  return resultNum;
}

//
// Prefixes are attached to stub numbers depending on the return type.
// return type: float  sf_
//              double df_
//              single complex sc_
//              double complext dc_
//              others  NO PREFIX
//
//
// The full name of a helper function is__mips16_call_stub +
//    return type dependent prefix + stub number
//
// FIXME: This is something that probably should be in a different source file
// and perhaps done differently but my main purpose is to not waste runtime
// on something that we can enumerate in the source. Another possibility is
// to have a python script to generate these mapping tables. This will do
// for now. There are a whole series of helper function mapping arrays, one
// for each return type class as outlined above. There there are 11 possible
// entries. Ones with 0 are ones which should never be selected.
//
// All the arrays are similar except for ones which return neither
// sf, df, sc, dc, in which we only care about ones which have sf or df as a
// first parameter.
//
#define P_ "__mips16_call_stub_"
#define MAX_STUB_NUMBER 10
#define T1 P "1", P "2", 0, 0, P "5", P "6", 0, 0, P "9", P "10"
#define T P "0" , T1
#define P P_
static char const * vMips16Helper[MAX_STUB_NUMBER+1] =
  {nullptr, T1 };
#undef P
#define P P_ "sf_"
static char const * sfMips16Helper[MAX_STUB_NUMBER+1] =
  { T };
#undef P
#define P P_ "df_"
static char const * dfMips16Helper[MAX_STUB_NUMBER+1] =
  { T };
#undef P
#define P P_ "sc_"
static char const * scMips16Helper[MAX_STUB_NUMBER+1] =
  { T };
#undef P
#define P P_ "dc_"
static char const * dcMips16Helper[MAX_STUB_NUMBER+1] =
  { T };
#undef P
#undef P_


const char* Mips16TargetLowering::
  getMips16HelperFunction
    (Type* RetTy, ArgListTy &Args, bool &needHelper) const {
  const unsigned int stubNum = getMips16HelperFunctionStubNumber(Args);
#ifndef NDEBUG
  const unsigned int maxStubNum = 10;
  assert(stubNum <= maxStubNum);
  const bool validStubNum[maxStubNum+1] =
    {true, true, true, false, false, true, true, false, false, true, true};
  assert(validStubNum[stubNum]);
#endif
  const char *result;
  if (RetTy->isFloatTy()) {
    result = sfMips16Helper[stubNum];
  }
  else if (RetTy ->isDoubleTy()) {
    result = dfMips16Helper[stubNum];
  } else if (StructType *SRetTy = dyn_cast<StructType>(RetTy)) {
    // check if it's complex
    if (SRetTy->getNumElements() == 2) {
      if ((SRetTy->getElementType(0)->isFloatTy()) &&
          (SRetTy->getElementType(1)->isFloatTy())) {
        result = scMips16Helper[stubNum];
      } else if ((SRetTy->getElementType(0)->isDoubleTy()) &&
                 (SRetTy->getElementType(1)->isDoubleTy())) {
        result = dcMips16Helper[stubNum];
      } else {
        llvm_unreachable("Uncovered condition");
      }
    } else {
      llvm_unreachable("Uncovered condition");
    }
  } else {
    if (stubNum == 0) {
      needHelper = false;
      return "";
    }
    result = vMips16Helper[stubNum];
  }
  needHelper = true;
  return result;
}

void Mips16TargetLowering::
getOpndList(SmallVectorImpl<SDValue> &Ops,
            std::deque< std::pair<unsigned, SDValue> > &RegsToPass,
            bool IsPICCall, bool GlobalOrExternal, bool InternalLinkage,
            bool IsCallReloc, CallLoweringInfo &CLI, SDValue Callee,
            SDValue Chain) const {
  SelectionDAG &DAG = CLI.DAG;
  MachineFunction &MF = DAG.getMachineFunction();
  MipsFunctionInfo *FuncInfo = MF.getInfo<MipsFunctionInfo>();
  const char* Mips16HelperFunction = nullptr;
  bool NeedMips16Helper = false;

  if (Subtarget.inMips16HardFloat()) {
    //
    // currently we don't have symbols tagged with the mips16 or mips32
    // qualifier so we will assume that we don't know what kind it is.
    // and generate the helper
    //
    bool LookupHelper = true;
    if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(CLI.Callee)) {
      Mips16Libcall Find = { RTLIB::UNKNOWN_LIBCALL, S->getSymbol() };

      if (std::binary_search(std::begin(HardFloatLibCalls),
                             std::end(HardFloatLibCalls), Find))
        LookupHelper = false;
      else {
        const char *Symbol = S->getSymbol();
        Mips16IntrinsicHelperType IntrinsicFind = { Symbol, "" };
        const Mips16HardFloatInfo::FuncSignature *Signature =
            Mips16HardFloatInfo::findFuncSignature(Symbol);
        if (!IsPICCall && (Signature && (FuncInfo->StubsNeeded.find(Symbol) ==
                                         FuncInfo->StubsNeeded.end()))) {
          FuncInfo->StubsNeeded[Symbol] = Signature;
          //
          // S2 is normally saved if the stub is for a function which
          // returns a float or double value and is not otherwise. This is
          // because more work is required after the function the stub
          // is calling completes, and so the stub cannot directly return
          // and the stub has no stack space to store the return address so
          // S2 is used for that purpose.
          // In order to take advantage of not saving S2, we need to also
          // optimize the call in the stub and this requires some further
          // functionality in MipsAsmPrinter which we don't have yet.
          // So for now we always save S2. The optimization will be done
          // in a follow-on patch.
          //
          if (true || (Signature->RetSig != Mips16HardFloatInfo::NoFPRet))
            FuncInfo->setSaveS2();
        }
        // one more look at list of intrinsics
        const Mips16IntrinsicHelperType *Helper =
            llvm::lower_bound(Mips16IntrinsicHelper, IntrinsicFind);
        if (Helper != std::end(Mips16IntrinsicHelper) &&
            *Helper == IntrinsicFind) {
          Mips16HelperFunction = Helper->Helper;
          NeedMips16Helper = true;
          LookupHelper = false;
        }

      }
    } else if (GlobalAddressSDNode *G =
                   dyn_cast<GlobalAddressSDNode>(CLI.Callee)) {
      Mips16Libcall Find = { RTLIB::UNKNOWN_LIBCALL,
                             G->getGlobal()->getName().data() };

      if (std::binary_search(std::begin(HardFloatLibCalls),
                             std::end(HardFloatLibCalls), Find))
        LookupHelper = false;
    }
    if (LookupHelper)
      Mips16HelperFunction =
        getMips16HelperFunction(CLI.RetTy, CLI.getArgs(), NeedMips16Helper);
  }

  SDValue JumpTarget = Callee;

  // T9 should contain the address of the callee function if
  // -relocation-model=pic or it is an indirect call.
  if (IsPICCall || !GlobalOrExternal) {
    unsigned V0Reg = Mips::V0;
    if (NeedMips16Helper) {
      RegsToPass.push_front(std::make_pair(V0Reg, Callee));
      JumpTarget = DAG.getExternalSymbol(Mips16HelperFunction,
                                         getPointerTy(DAG.getDataLayout()));
      ExternalSymbolSDNode *S = cast<ExternalSymbolSDNode>(JumpTarget);
      JumpTarget = getAddrGlobal(S, CLI.DL, JumpTarget.getValueType(), DAG,
                                 MipsII::MO_GOT, Chain,
                                 FuncInfo->callPtrInfo(MF, S->getSymbol()));
    } else
      RegsToPass.push_front(std::make_pair((unsigned)Mips::T9, Callee));
  }

  Ops.push_back(JumpTarget);

  MipsTargetLowering::getOpndList(Ops, RegsToPass, IsPICCall, GlobalOrExternal,
                                  InternalLinkage, IsCallReloc, CLI, Callee,
                                  Chain);
}

MachineBasicBlock *
Mips16TargetLowering::emitSel16(unsigned Opc, MachineInstr &MI,
                                MachineBasicBlock *BB) const {
  if (DontExpandCondPseudos16)
    return BB;
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  // To "insert" a SELECT_CC instruction, we actually have to insert the
  // diamond control-flow pattern.  The incoming instruction knows the
  // destination vreg to set, the condition code register to branch on, the
  // true/false values to select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  //  thisMBB:
  //  ...
  //   TrueVal = ...
  //   setcc r1, r2, r3
  //   bNE   r1, r0, copy1MBB
  //   fallthrough --> copy0MBB
  MachineBasicBlock *thisMBB  = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *sinkMBB  = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, copy0MBB);
  F->insert(It, sinkMBB);

  // Transfer the remainder of BB and its successor edges to sinkMBB.
  sinkMBB->splice(sinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(copy0MBB);
  BB->addSuccessor(sinkMBB);

  BuildMI(BB, DL, TII->get(Opc))
      .addReg(MI.getOperand(3).getReg())
      .addMBB(sinkMBB);

  //  copy0MBB:
  //   %FalseValue = ...
  //   # fallthrough to sinkMBB
  BB = copy0MBB;

  // Update machine-CFG edges
  BB->addSuccessor(sinkMBB);

  //  sinkMBB:
  //   %Result = phi [ %TrueValue, thisMBB ], [ %FalseValue, copy0MBB ]
  //  ...
  BB = sinkMBB;

  BuildMI(*BB, BB->begin(), DL, TII->get(Mips::PHI), MI.getOperand(0).getReg())
      .addReg(MI.getOperand(1).getReg())
      .addMBB(thisMBB)
      .addReg(MI.getOperand(2).getReg())
      .addMBB(copy0MBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

MachineBasicBlock *
Mips16TargetLowering::emitSelT16(unsigned Opc1, unsigned Opc2, MachineInstr &MI,
                                 MachineBasicBlock *BB) const {
  if (DontExpandCondPseudos16)
    return BB;
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  // To "insert" a SELECT_CC instruction, we actually have to insert the
  // diamond control-flow pattern.  The incoming instruction knows the
  // destination vreg to set, the condition code register to branch on, the
  // true/false values to select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  //  thisMBB:
  //  ...
  //   TrueVal = ...
  //   setcc r1, r2, r3
  //   bNE   r1, r0, copy1MBB
  //   fallthrough --> copy0MBB
  MachineBasicBlock *thisMBB  = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *sinkMBB  = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, copy0MBB);
  F->insert(It, sinkMBB);

  // Transfer the remainder of BB and its successor edges to sinkMBB.
  sinkMBB->splice(sinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(copy0MBB);
  BB->addSuccessor(sinkMBB);

  BuildMI(BB, DL, TII->get(Opc2))
      .addReg(MI.getOperand(3).getReg())
      .addReg(MI.getOperand(4).getReg());
  BuildMI(BB, DL, TII->get(Opc1)).addMBB(sinkMBB);

  //  copy0MBB:
  //   %FalseValue = ...
  //   # fallthrough to sinkMBB
  BB = copy0MBB;

  // Update machine-CFG edges
  BB->addSuccessor(sinkMBB);

  //  sinkMBB:
  //   %Result = phi [ %TrueValue, thisMBB ], [ %FalseValue, copy0MBB ]
  //  ...
  BB = sinkMBB;

  BuildMI(*BB, BB->begin(), DL, TII->get(Mips::PHI), MI.getOperand(0).getReg())
      .addReg(MI.getOperand(1).getReg())
      .addMBB(thisMBB)
      .addReg(MI.getOperand(2).getReg())
      .addMBB(copy0MBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;

}

MachineBasicBlock *
Mips16TargetLowering::emitSeliT16(unsigned Opc1, unsigned Opc2,
                                  MachineInstr &MI,
                                  MachineBasicBlock *BB) const {
  if (DontExpandCondPseudos16)
    return BB;
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  // To "insert" a SELECT_CC instruction, we actually have to insert the
  // diamond control-flow pattern.  The incoming instruction knows the
  // destination vreg to set, the condition code register to branch on, the
  // true/false values to select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  //  thisMBB:
  //  ...
  //   TrueVal = ...
  //   setcc r1, r2, r3
  //   bNE   r1, r0, copy1MBB
  //   fallthrough --> copy0MBB
  MachineBasicBlock *thisMBB  = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *sinkMBB  = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, copy0MBB);
  F->insert(It, sinkMBB);

  // Transfer the remainder of BB and its successor edges to sinkMBB.
  sinkMBB->splice(sinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(copy0MBB);
  BB->addSuccessor(sinkMBB);

  BuildMI(BB, DL, TII->get(Opc2))
      .addReg(MI.getOperand(3).getReg())
      .addImm(MI.getOperand(4).getImm());
  BuildMI(BB, DL, TII->get(Opc1)).addMBB(sinkMBB);

  //  copy0MBB:
  //   %FalseValue = ...
  //   # fallthrough to sinkMBB
  BB = copy0MBB;

  // Update machine-CFG edges
  BB->addSuccessor(sinkMBB);

  //  sinkMBB:
  //   %Result = phi [ %TrueValue, thisMBB ], [ %FalseValue, copy0MBB ]
  //  ...
  BB = sinkMBB;

  BuildMI(*BB, BB->begin(), DL, TII->get(Mips::PHI), MI.getOperand(0).getReg())
      .addReg(MI.getOperand(1).getReg())
      .addMBB(thisMBB)
      .addReg(MI.getOperand(2).getReg())
      .addMBB(copy0MBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;

}

MachineBasicBlock *
Mips16TargetLowering::emitFEXT_T8I816_ins(unsigned BtOpc, unsigned CmpOpc,
                                          MachineInstr &MI,
                                          MachineBasicBlock *BB) const {
  if (DontExpandCondPseudos16)
    return BB;
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  Register regX = MI.getOperand(0).getReg();
  Register regY = MI.getOperand(1).getReg();
  MachineBasicBlock *target = MI.getOperand(2).getMBB();
  BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(CmpOpc))
      .addReg(regX)
      .addReg(regY);
  BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(BtOpc)).addMBB(target);
  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

MachineBasicBlock *Mips16TargetLowering::emitFEXT_T8I8I16_ins(
    unsigned BtOpc, unsigned CmpiOpc, unsigned CmpiXOpc, bool ImmSigned,
    MachineInstr &MI, MachineBasicBlock *BB) const {
  if (DontExpandCondPseudos16)
    return BB;
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  Register regX = MI.getOperand(0).getReg();
  int64_t imm = MI.getOperand(1).getImm();
  MachineBasicBlock *target = MI.getOperand(2).getMBB();
  unsigned CmpOpc;
  if (isUInt<8>(imm))
    CmpOpc = CmpiOpc;
  else if ((!ImmSigned && isUInt<16>(imm)) ||
           (ImmSigned && isInt<16>(imm)))
    CmpOpc = CmpiXOpc;
  else
    llvm_unreachable("immediate field not usable");
  BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(CmpOpc)).addReg(regX).addImm(imm);
  BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(BtOpc)).addMBB(target);
  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

static unsigned Mips16WhichOp8uOr16simm
  (unsigned shortOp, unsigned longOp, int64_t Imm) {
  if (isUInt<8>(Imm))
    return shortOp;
  else if (isInt<16>(Imm))
    return longOp;
  else
    llvm_unreachable("immediate field not usable");
}

MachineBasicBlock *
Mips16TargetLowering::emitFEXT_CCRX16_ins(unsigned SltOpc, MachineInstr &MI,
                                          MachineBasicBlock *BB) const {
  if (DontExpandCondPseudos16)
    return BB;
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  Register CC = MI.getOperand(0).getReg();
  Register regX = MI.getOperand(1).getReg();
  Register regY = MI.getOperand(2).getReg();
  BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(SltOpc))
      .addReg(regX)
      .addReg(regY);
  BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(Mips::MoveR3216), CC)
      .addReg(Mips::T8);
  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

MachineBasicBlock *
Mips16TargetLowering::emitFEXT_CCRXI16_ins(unsigned SltiOpc, unsigned SltiXOpc,
                                           MachineInstr &MI,
                                           MachineBasicBlock *BB) const {
  if (DontExpandCondPseudos16)
    return BB;
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  Register CC = MI.getOperand(0).getReg();
  Register regX = MI.getOperand(1).getReg();
  int64_t Imm = MI.getOperand(2).getImm();
  unsigned SltOpc = Mips16WhichOp8uOr16simm(SltiOpc, SltiXOpc, Imm);
  BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(SltOpc)).addReg(regX).addImm(Imm);
  BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(Mips::MoveR3216), CC)
      .addReg(Mips::T8);
  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;

}
