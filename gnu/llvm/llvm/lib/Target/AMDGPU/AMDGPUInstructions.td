//===-- AMDGPUInstructions.td - Common instruction defs ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains instruction defs that are common to all hw codegen
// targets.
//
//===----------------------------------------------------------------------===//

class AddressSpacesImpl {
  int Flat = 0;
  int Global = 1;
  int Region = 2;
  int Local = 3;
  int Constant = 4;
  int Private = 5;
  int Constant32Bit = 6;
}

def AddrSpaces : AddressSpacesImpl;


class AMDGPUInst <dag outs, dag ins, string asm = "",
  list<dag> pattern = []> : Instruction {
  field bit isRegisterLoad = 0;
  field bit isRegisterStore = 0;

  let Namespace = "AMDGPU";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asm;
  let Pattern = pattern;
  let Itinerary = NullALU;

  // SoftFail is a field the disassembler can use to provide a way for
  // instructions to not match without killing the whole decode process. It is
  // mainly used for ARM, but Tablegen expects this field to exist or it fails
  // to build the decode table.
  field bits<96> SoftFail = 0;

  let DecoderNamespace = Namespace;

  let TSFlags{63} = isRegisterLoad;
  let TSFlags{62} = isRegisterStore;
}

class AMDGPUShaderInst <dag outs, dag ins, string asm = "",
  list<dag> pattern = []> : AMDGPUInst<outs, ins, asm, pattern> {

  field bits<32> Inst = 0xffffffff;
}

//===---------------------------------------------------------------------===//
// Return instruction
//===---------------------------------------------------------------------===//

class ILFormat<dag outs, dag ins, string asmstr, list<dag> pattern>
: Instruction {

     let Namespace = "AMDGPU";
     dag OutOperandList = outs;
     dag InOperandList = ins;
     let Pattern = pattern;
     let AsmString = !strconcat(asmstr, "\n");
     let isPseudo = 1;
     let Itinerary = NullALU;
     bit hasIEEEFlag = 0;
     bit hasZeroOpFlag = 0;
     let mayLoad = 0;
     let mayStore = 0;
     let hasSideEffects = 0;
     let isCodeGenOnly = 1;
}

// Get the union of two Register lists
class RegListUnion<list<Register> lstA, list<Register> lstB> {
  list<Register> ret = !listconcat(lstA, !listremove(lstB, lstA));
}

class AMDGPUPat<dag pattern, dag result> : Pat<pattern, result>,
      PredicateControl, GISelFlags;

let GIIgnoreCopies = 1 in
class AMDGPUPatIgnoreCopies<dag pattern, dag result> : AMDGPUPat<pattern, result>;

let RecomputePerFunction = 1 in {
def FP16Denormals : Predicate<"MF->getInfo<SIMachineFunctionInfo>()->getMode().FP64FP16Denormals != DenormalMode::getPreserveSign()">;
def FP32Denormals : Predicate<"MF->getInfo<SIMachineFunctionInfo>()->getMode().FP32Denormals != DenormalMode::getPreserveSign()">;
def FP64Denormals : Predicate<"MF->getInfo<SIMachineFunctionInfo>()->getMode().FP64FP16Denormals != DenormalMode::getPreserveSign()">;
def NoFP16Denormals : Predicate<"MF->getInfo<SIMachineFunctionInfo>()->getMode().FP64FP16Denormals == DenormalMode::getPreserveSign()">;
def NoFP32Denormals : Predicate<"MF->getInfo<SIMachineFunctionInfo>()->getMode().FP32Denormals == DenormalMode::getPreserveSign()">;
def NoFP64Denormals : Predicate<"MF->getInfo<SIMachineFunctionInfo>()->getMode().FP64FP16Denormals == DenormalMode::getPreserveSign()">;
def UnsafeFPMath : Predicate<"TM.Options.UnsafeFPMath">;
}

def FMA : Predicate<"Subtarget->hasFMA()">;

def InstFlag : OperandWithDefaultOps <i32, (ops (i32 0))>;

def i1imm_0 : OperandWithDefaultOps<i1, (ops (i1 0))>;

class CustomOperandClass<string name, bit optional, string predicateMethod,
                         string parserMethod, string defaultMethod>
    : AsmOperandClass {
  let Name = name;
  let PredicateMethod = predicateMethod;
  let ParserMethod = parserMethod;
  let RenderMethod = "addImmOperands";
  let IsOptional = optional;
  let DefaultMethod = defaultMethod;
}

class CustomOperandProps<bit optional = 0, string name = NAME> {
  string ImmTy = "ImmTy"#name;
  string PredicateMethod = "is"#name;
  string ParserMethod = "parse"#name;
  string DefaultValue = "0";
  string DefaultMethod = "[this]() { return "#
    "AMDGPUOperand::CreateImm(this, "#DefaultValue#", SMLoc(), "#
    "AMDGPUOperand::"#ImmTy#"); }";
  string PrintMethod = "print"#name;
  AsmOperandClass ParserMatchClass =
    CustomOperandClass<name, optional, PredicateMethod, ParserMethod,
                       DefaultMethod>;
  string OperandType = "OPERAND_IMMEDIATE";
}

class CustomOperand<ValueType type, bit optional = 0, string name = NAME>
  : Operand<type>, CustomOperandProps<optional, name>;

class ImmOperand<ValueType type, string name = NAME, bit optional = 0,
                 string printer = "print"#name>
    : CustomOperand<type, optional, name> {
  let ImmTy = "ImmTyNone";
  let ParserMethod = "";
  let PrintMethod = printer;
}

class S16ImmOperand : ImmOperand<i16, "S16Imm", 0, "printU16ImmOperand">;

def s16imm : S16ImmOperand;
def u16imm : ImmOperand<i16, "U16Imm", 0, "printU16ImmOperand">;

class ValuePredicatedOperand<CustomOperand op, string valuePredicate,
                             bit optional = 0>
    : CustomOperand<op.Type, optional> {
  let ImmTy = op.ImmTy;
  defvar OpPredicate = op.ParserMatchClass.PredicateMethod;
  let PredicateMethod =
    "getPredicate([](const AMDGPUOperand &Op) -> bool { "#
    "return Op."#OpPredicate#"() && "#valuePredicate#"; })";
  let ParserMethod = op.ParserMatchClass.ParserMethod;
  let DefaultValue = op.DefaultValue;
  let DefaultMethod = op.DefaultMethod;
  let PrintMethod = op.PrintMethod;
}

//===--------------------------------------------------------------------===//
// Custom Operands
//===--------------------------------------------------------------------===//
def brtarget   : Operand<OtherVT>;

//===----------------------------------------------------------------------===//
// Misc. PatFrags
//===----------------------------------------------------------------------===//

class HasOneUseUnaryOp<SDPatternOperator op> : PatFrag<
  (ops node:$src0),
  (op $src0)> {
  let HasOneUse = 1;
}

class HasOneUseBinOp<SDPatternOperator op> : PatFrag<
  (ops node:$src0, node:$src1),
  (op $src0, $src1)> {
  let HasOneUse = 1;
}

class HasOneUseTernaryOp<SDPatternOperator op> : PatFrag<
  (ops node:$src0, node:$src1, node:$src2),
  (op $src0, $src1, $src2)> {
  let HasOneUse = 1;
}

class is_canonicalized_1<SDPatternOperator op> : PatFrag<
  (ops node:$src0),
  (op $src0),
  [{
    const SITargetLowering &Lowering =
              *static_cast<const SITargetLowering *>(getTargetLowering());

    return Lowering.isCanonicalized(*CurDAG, N->getOperand(0));
   }]> {

  let GISelPredicateCode = [{
    const SITargetLowering *TLI = static_cast<const SITargetLowering *>(
      MF.getSubtarget().getTargetLowering());

    return TLI->isCanonicalized(MI.getOperand(1).getReg(), MF);
  }];
}

class is_canonicalized_2<SDPatternOperator op> : PatFrag<
  (ops node:$src0, node:$src1),
  (op $src0, $src1),
  [{
    const SITargetLowering &Lowering =
              *static_cast<const SITargetLowering *>(getTargetLowering());

    return Lowering.isCanonicalized(*CurDAG, N->getOperand(0)) &&
      Lowering.isCanonicalized(*CurDAG, N->getOperand(1));
   }]> {

  // TODO: Improve the Legalizer for g_build_vector in Global Isel to match this class
  let GISelPredicateCode = [{
    const SITargetLowering *TLI = static_cast<const SITargetLowering *>(
      MF.getSubtarget().getTargetLowering());

    return TLI->isCanonicalized(MI.getOperand(1).getReg(), MF) &&
      TLI->isCanonicalized(MI.getOperand(2).getReg(), MF);
  }];
}

class FoldTernaryOpPat<SDPatternOperator op1, SDPatternOperator op2> : PatFrag<
  (ops node:$src0, node:$src1, node:$src2),
  (op2 (op1 node:$src0, node:$src1), node:$src2)
>;

def imad : FoldTernaryOpPat<mul, add>;

let Properties = [SDNPCommutative, SDNPAssociative] in {
def smax_oneuse : HasOneUseBinOp<smax>;
def smin_oneuse : HasOneUseBinOp<smin>;
def umax_oneuse : HasOneUseBinOp<umax>;
def umin_oneuse : HasOneUseBinOp<umin>;

def fminnum_oneuse : HasOneUseBinOp<fminnum>;
def fmaxnum_oneuse : HasOneUseBinOp<fmaxnum>;
def fminimum_oneuse : HasOneUseBinOp<fminimum>;
def fmaximum_oneuse : HasOneUseBinOp<fmaximum>;

def fminnum_ieee_oneuse : HasOneUseBinOp<fminnum_ieee>;
def fmaxnum_ieee_oneuse : HasOneUseBinOp<fmaxnum_ieee>;


def and_oneuse : HasOneUseBinOp<and>;
def or_oneuse : HasOneUseBinOp<or>;
def xor_oneuse : HasOneUseBinOp<xor>;
} // Properties = [SDNPCommutative, SDNPAssociative]

def not_oneuse : HasOneUseUnaryOp<not>;

def add_oneuse : HasOneUseBinOp<add>;
def sub_oneuse : HasOneUseBinOp<sub>;

def srl_oneuse : HasOneUseBinOp<srl>;
def shl_oneuse : HasOneUseBinOp<shl>;

def select_oneuse : HasOneUseTernaryOp<select>;

def AMDGPUmul_u24_oneuse : HasOneUseBinOp<AMDGPUmul_u24>;
def AMDGPUmul_i24_oneuse : HasOneUseBinOp<AMDGPUmul_i24>;

//===----------------------------------------------------------------------===//
// PatFrags for shifts
//===----------------------------------------------------------------------===//

// Constrained shift PatFrags.

def csh_mask_16 : PatFrag<(ops node:$src0), (and node:$src0, imm),
  [{ return isUnneededShiftMask(N, 4); }]> {
    let GISelPredicateCode = [{ return isUnneededShiftMask(MI, 4); }];
  }

def csh_mask_32 : PatFrag<(ops node:$src0), (and node:$src0, imm),
  [{ return isUnneededShiftMask(N, 5); }]> {
    let GISelPredicateCode = [{ return isUnneededShiftMask(MI, 5); }];
  }

def csh_mask_64 : PatFrag<(ops node:$src0), (and node:$src0, imm),
  [{ return isUnneededShiftMask(N, 6); }]> {
    let GISelPredicateCode = [{ return isUnneededShiftMask(MI, 6); }];
  }

foreach width = [16, 32, 64] in {
defvar csh_mask = !cast<SDPatternOperator>("csh_mask_"#width);

def cshl_#width : PatFrags<(ops node:$src0, node:$src1),
  [(shl node:$src0, node:$src1), (shl node:$src0, (csh_mask node:$src1))]>;
defvar cshl = !cast<SDPatternOperator>("cshl_"#width);
def cshl_#width#_oneuse : HasOneUseBinOp<cshl>;
def clshl_rev_#width : PatFrag <(ops node:$src0, node:$src1),
  (cshl $src1, $src0)>;

def csrl_#width : PatFrags<(ops node:$src0, node:$src1),
  [(srl node:$src0, node:$src1), (srl node:$src0, (csh_mask node:$src1))]>;
defvar csrl = !cast<SDPatternOperator>("csrl_"#width);
def csrl_#width#_oneuse : HasOneUseBinOp<csrl>;
def clshr_rev_#width : PatFrag <(ops node:$src0, node:$src1),
  (csrl $src1, $src0)>;

def csra_#width : PatFrags<(ops node:$src0, node:$src1),
  [(sra node:$src0, node:$src1), (sra node:$src0, (csh_mask node:$src1))]>;
defvar csra = !cast<SDPatternOperator>("csra_"#width);
def csra_#width#_oneuse : HasOneUseBinOp<csra>;
def cashr_rev_#width : PatFrag <(ops node:$src0, node:$src1),
  (csra $src1, $src0)>;
} // end foreach width

def srl_16 : PatFrag<
  (ops node:$src0), (srl_oneuse node:$src0, (i32 16))
>;


def hi_i16_elt : PatFrag<
  (ops node:$src0), (i16 (trunc (i32 (srl_16 node:$src0))))
>;


def hi_f16_elt : PatLeaf<
  (vt), [{
  if (N->getOpcode() != ISD::BITCAST)
    return false;
  SDValue Tmp = N->getOperand(0);

  if (Tmp.getOpcode() != ISD::SRL)
    return false;
    if (const auto *RHS = dyn_cast<ConstantSDNode>(Tmp.getOperand(1))
      return RHS->getZExtValue() == 16;
    return false;
}]>;

//===----------------------------------------------------------------------===//
// PatLeafs for zero immediate
//===----------------------------------------------------------------------===//

def immzero : PatLeaf<(imm), [{ return N->isZero(); }]>;
def fpimmzero : PatLeaf<(fpimm), [{ return N->isZero(); }]>;

//===----------------------------------------------------------------------===//
// PatLeafs for floating-point comparisons
//===----------------------------------------------------------------------===//

def COND_OEQ : PatFrags<(ops), [(OtherVT SETOEQ), (OtherVT SETEQ)]>;
def COND_ONE : PatFrags<(ops), [(OtherVT SETONE), (OtherVT SETNE)]>;
def COND_OGT : PatFrags<(ops), [(OtherVT SETOGT), (OtherVT SETGT)]>;
def COND_OGE : PatFrags<(ops), [(OtherVT SETOGE), (OtherVT SETGE)]>;
def COND_OLT : PatFrags<(ops), [(OtherVT SETOLT), (OtherVT SETLT)]>;
def COND_OLE : PatFrags<(ops), [(OtherVT SETOLE), (OtherVT SETLE)]>;
def COND_O   : PatFrags<(ops), [(OtherVT SETO)]>;
def COND_UO  : PatFrags<(ops), [(OtherVT SETUO)]>;

//===----------------------------------------------------------------------===//
// PatLeafs for unsigned / unordered comparisons
//===----------------------------------------------------------------------===//

def COND_UEQ : PatFrag<(ops), (OtherVT SETUEQ)>;
def COND_UNE : PatFrag<(ops), (OtherVT SETUNE)>;
def COND_UGT : PatFrag<(ops), (OtherVT SETUGT)>;
def COND_UGE : PatFrag<(ops), (OtherVT SETUGE)>;
def COND_ULT : PatFrag<(ops), (OtherVT SETULT)>;
def COND_ULE : PatFrag<(ops), (OtherVT SETULE)>;

// XXX - For some reason R600 version is preferring to use unordered
// for setne?
def COND_UNE_NE  : PatFrags<(ops), [(OtherVT SETUNE), (OtherVT SETNE)]>;

//===----------------------------------------------------------------------===//
// PatLeafs for signed comparisons
//===----------------------------------------------------------------------===//

def COND_SGT : PatFrag<(ops), (OtherVT SETGT)>;
def COND_SGE : PatFrag<(ops), (OtherVT SETGE)>;
def COND_SLT : PatFrag<(ops), (OtherVT SETLT)>;
def COND_SLE : PatFrag<(ops), (OtherVT SETLE)>;

//===----------------------------------------------------------------------===//
// PatLeafs for integer equality
//===----------------------------------------------------------------------===//

def COND_EQ : PatFrags<(ops), [(OtherVT SETEQ), (OtherVT SETUEQ)]>;
def COND_NE : PatFrags<(ops), [(OtherVT SETNE), (OtherVT SETUNE)]>;

// FIXME: Should not need code predicate
//def COND_NULL : PatLeaf<(OtherVT null_frag)>;
def COND_NULL : PatLeaf <
  (cond),
  [{(void)N; return false;}]
>;

//===----------------------------------------------------------------------===//
// PatLeafs for Texture Constants
//===----------------------------------------------------------------------===//

def TEX_ARRAY : PatLeaf<
  (imm),
  [{uint32_t TType = (uint32_t)N->getZExtValue();
    return TType == 9 || TType == 10 || TType == 16;
  }]
>;

def TEX_RECT : PatLeaf<
  (imm),
  [{uint32_t TType = (uint32_t)N->getZExtValue();
    return TType == 5;
  }]
>;

def TEX_SHADOW : PatLeaf<
  (imm),
  [{uint32_t TType = (uint32_t)N->getZExtValue();
    return (TType >= 6 && TType <= 8) || TType == 13;
  }]
>;

def TEX_SHADOW_ARRAY : PatLeaf<
  (imm),
  [{uint32_t TType = (uint32_t)N->getZExtValue();
    return TType == 11 || TType == 12 || TType == 17;
  }]
>;

//===----------------------------------------------------------------------===//
// Load/Store Pattern Fragments
//===----------------------------------------------------------------------===//

def atomic_cmp_swap_glue : SDNode <"ISD::ATOMIC_CMP_SWAP", SDTAtomic3,
  [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand, SDNPInGlue]
>;

class AddressSpaceList<list<int> AS> {
  list<int> AddrSpaces = AS;
}

class Aligned<int Bytes> {
  int MinAlignment = Bytes;
}

class StoreHi16<SDPatternOperator op, ValueType vt> : PatFrag <
  (ops node:$value, node:$ptr), (op (srl node:$value, (i32 16)), node:$ptr)> {
  let IsStore = 1;
  let MemoryVT = vt;
}

def LoadAddress_constant : AddressSpaceList<[ AddrSpaces.Constant,
                                              AddrSpaces.Constant32Bit ]>;
def LoadAddress_global : AddressSpaceList<[ AddrSpaces.Global,
                                            AddrSpaces.Constant,
                                            AddrSpaces.Constant32Bit ]>;
def StoreAddress_global : AddressSpaceList<[ AddrSpaces.Global ]>;

def LoadAddress_flat : AddressSpaceList<[ AddrSpaces.Flat,
                                          AddrSpaces.Global,
                                          AddrSpaces.Constant,
                                          AddrSpaces.Constant32Bit ]>;
def StoreAddress_flat : AddressSpaceList<[ AddrSpaces.Flat, AddrSpaces.Global ]>;

def LoadAddress_private : AddressSpaceList<[ AddrSpaces.Private ]>;
def StoreAddress_private : AddressSpaceList<[ AddrSpaces.Private ]>;

def LoadAddress_local : AddressSpaceList<[ AddrSpaces.Local ]>;
def StoreAddress_local : AddressSpaceList<[ AddrSpaces.Local ]>;

def LoadAddress_region : AddressSpaceList<[ AddrSpaces.Region ]>;
def StoreAddress_region : AddressSpaceList<[ AddrSpaces.Region ]>;



foreach as = [ "global", "flat", "constant", "local", "private", "region" ] in {
let AddressSpaces = !cast<AddressSpaceList>("LoadAddress_"#as).AddrSpaces in {

def load_#as : PatFrag<(ops node:$ptr), (unindexedload node:$ptr)> {
  let IsLoad = 1;
  let IsNonExtLoad = 1;
}

def extloadi8_#as  : PatFrag<(ops node:$ptr), (extloadi8 node:$ptr)> {
  let IsLoad = 1;
}

def extloadi16_#as : PatFrag<(ops node:$ptr), (extloadi16 node:$ptr)> {
  let IsLoad = 1;
}

def sextloadi8_#as  : PatFrag<(ops node:$ptr), (sextloadi8 node:$ptr)> {
  let IsLoad = 1;
}

def sextloadi16_#as : PatFrag<(ops node:$ptr), (sextloadi16 node:$ptr)> {
  let IsLoad = 1;
}

def zextloadi8_#as  : PatFrag<(ops node:$ptr), (zextloadi8 node:$ptr)> {
  let IsLoad = 1;
}

def zextloadi16_#as : PatFrag<(ops node:$ptr), (zextloadi16 node:$ptr)> {
  let IsLoad = 1;
}

def atomic_load_8_#as : PatFrag<(ops node:$ptr), (atomic_load_8 node:$ptr)> {
  let IsAtomic = 1;
  let MemoryVT = i8;
}

def atomic_load_16_#as : PatFrag<(ops node:$ptr), (atomic_load_16 node:$ptr)> {
  let IsAtomic = 1;
  let MemoryVT = i16;
}

def atomic_load_32_#as : PatFrag<(ops node:$ptr), (atomic_load_32 node:$ptr)> {
  let IsAtomic = 1;
  let MemoryVT = i32;
}

def atomic_load_64_#as : PatFrag<(ops node:$ptr), (atomic_load_64 node:$ptr)> {
  let IsAtomic = 1;
  let MemoryVT = i64;
}
} // End let AddressSpaces
} // End foreach as


foreach as = [ "global", "flat", "local", "private", "region" ] in {
let IsStore = 1, AddressSpaces = !cast<AddressSpaceList>("StoreAddress_"#as).AddrSpaces in {
def store_#as : PatFrag<(ops node:$val, node:$ptr),
                    (unindexedstore node:$val, node:$ptr)> {
  let IsTruncStore = 0;
}

// truncstore fragments.
def truncstore_#as : PatFrag<(ops node:$val, node:$ptr),
                             (unindexedstore node:$val, node:$ptr)> {
  let IsTruncStore = 1;
}

// TODO: We don't really need the truncstore here. We can use
// unindexedstore with MemoryVT directly, which will save an
// unnecessary check that the memory size is less than the value type
// in the generated matcher table.
def truncstorei8_#as : PatFrag<(ops node:$val, node:$ptr),
                               (truncstorei8 node:$val, node:$ptr)>;
def truncstorei16_#as : PatFrag<(ops node:$val, node:$ptr),
                                (truncstorei16 node:$val, node:$ptr)>;

def store_hi16_#as : StoreHi16 <truncstorei16, i16>;
def truncstorei8_hi16_#as : StoreHi16<truncstorei8, i8>;
def truncstorei16_hi16_#as : StoreHi16<truncstorei16, i16>;
} // End let IsStore = 1, AddressSpaces = ...

let IsAtomic = 1, AddressSpaces = !cast<AddressSpaceList>("StoreAddress_"#as).AddrSpaces in {
def atomic_store_8_#as : PatFrag<(ops node:$val, node:$ptr),
                                 (atomic_store_8 node:$val, node:$ptr)>;
def atomic_store_16_#as : PatFrag<(ops node:$val, node:$ptr),
                                  (atomic_store_16 node:$val, node:$ptr)>;
def atomic_store_32_#as : PatFrag<(ops node:$val, node:$ptr),
                                  (atomic_store_32 node:$val, node:$ptr)>;
def atomic_store_64_#as : PatFrag<(ops node:$val, node:$ptr),
                                  (atomic_store_64 node:$val, node:$ptr)>;
} // End let IsAtomic = 1, AddressSpaces = ...
} // End foreach as

multiclass noret_op {
  let HasNoUse = true in
  def "_noret" : PatFrag<(ops node:$ptr, node:$data),
    (!cast<SDPatternOperator>(NAME) node:$ptr, node:$data)>;
}

multiclass global_addr_space_atomic_op {
  def "_noret_global_addrspace" :
    PatFrag<(ops node:$ptr, node:$data),
            (!cast<SDPatternOperator>(NAME) node:$ptr, node:$data)>{
      let HasNoUse = true;
      let AddressSpaces = LoadAddress_global.AddrSpaces;
      let IsAtomic = 1;
    }
    def "_global_addrspace" :
    PatFrag<(ops node:$ptr, node:$data),
            (!cast<SDPatternOperator>(NAME) node:$ptr, node:$data)>{
      let AddressSpaces = LoadAddress_global.AddrSpaces;
      let IsAtomic = 1;
    }
}

multiclass flat_addr_space_atomic_op {
  def "_noret_flat_addrspace" :
    PatFrag<(ops node:$ptr, node:$data),
            (!cast<SDPatternOperator>(NAME) node:$ptr, node:$data)>{
      let HasNoUse = true;
      let AddressSpaces = LoadAddress_flat.AddrSpaces;
      let IsAtomic = 1;
    }
    def "_flat_addrspace" :
    PatFrag<(ops node:$ptr, node:$data),
            (!cast<SDPatternOperator>(NAME) node:$ptr, node:$data)>{
      let AddressSpaces = LoadAddress_flat.AddrSpaces;
      let IsAtomic = 1;
    }
}

multiclass local_addr_space_atomic_op {
  def "_noret_local_addrspace" :
    PatFrag<(ops node:$ptr, node:$data),
            (!cast<SDPatternOperator>(NAME) node:$ptr, node:$data)>{
      let HasNoUse = true;
      let AddressSpaces = LoadAddress_local.AddrSpaces;
      let IsAtomic = 1;
    }
    def "_local_addrspace" :
    PatFrag<(ops node:$ptr, node:$data),
            (!cast<SDPatternOperator>(NAME) node:$ptr, node:$data)>{
      let AddressSpaces = LoadAddress_local.AddrSpaces;
      let IsAtomic = 1;
    }
}

defm int_amdgcn_flat_atomic_fadd : noret_op;
defm int_amdgcn_flat_atomic_fadd : flat_addr_space_atomic_op;
defm int_amdgcn_flat_atomic_fadd_v2bf16 : noret_op;
defm int_amdgcn_flat_atomic_fmin : noret_op;
defm int_amdgcn_flat_atomic_fmax : noret_op;
defm int_amdgcn_global_atomic_fadd : global_addr_space_atomic_op;
defm int_amdgcn_flat_atomic_fadd : global_addr_space_atomic_op;
defm int_amdgcn_global_atomic_fadd_v2bf16 : noret_op;
defm int_amdgcn_global_atomic_fmin : noret_op;
defm int_amdgcn_global_atomic_fmax : noret_op;
defm int_amdgcn_global_atomic_csub : noret_op;
defm int_amdgcn_flat_atomic_fadd : local_addr_space_atomic_op;
defm int_amdgcn_global_atomic_ordered_add_b64 : noret_op;
defm int_amdgcn_flat_atomic_fmin_num : noret_op;
defm int_amdgcn_flat_atomic_fmax_num : noret_op;
defm int_amdgcn_global_atomic_fmin_num : noret_op;
defm int_amdgcn_global_atomic_fmax_num : noret_op;
defm int_amdgcn_atomic_cond_sub_u32 : local_addr_space_atomic_op;
defm int_amdgcn_atomic_cond_sub_u32 : flat_addr_space_atomic_op;
defm int_amdgcn_atomic_cond_sub_u32 : global_addr_space_atomic_op;

multiclass noret_binary_atomic_op<SDNode atomic_op> {
  let HasNoUse = true in
  defm "_noret" : binary_atomic_op<atomic_op>;
}

multiclass noret_binary_atomic_op_fp<SDNode atomic_op> {
  let HasNoUse = true in
  defm "_noret" : binary_atomic_op_fp<atomic_op>;
}

multiclass noret_ternary_atomic_op<SDNode atomic_op> {
  let HasNoUse = true in
  defm "_noret" : ternary_atomic_op<atomic_op>;
}

defvar atomic_addrspace_names = [ "global", "flat", "constant", "local", "private", "region" ];

multiclass binary_atomic_op_all_as<SDNode atomic_op> {
  foreach as = atomic_addrspace_names in {
    let AddressSpaces = !cast<AddressSpaceList>("LoadAddress_"#as).AddrSpaces in {
      defm "_"#as : binary_atomic_op<atomic_op>;
      defm "_"#as : noret_binary_atomic_op<atomic_op>;
    }
  }
}
multiclass binary_atomic_op_fp_all_as<SDNode atomic_op> {
  foreach as = atomic_addrspace_names in {
    let AddressSpaces = !cast<AddressSpaceList>("LoadAddress_"#as).AddrSpaces in {
      defm "_"#as : binary_atomic_op_fp<atomic_op>;
      defm "_"#as : noret_binary_atomic_op_fp<atomic_op>;
    }
  }
}

defm atomic_swap : binary_atomic_op_all_as<atomic_swap>;
defm atomic_load_add : binary_atomic_op_all_as<atomic_load_add>;
defm atomic_load_and : binary_atomic_op_all_as<atomic_load_and>;
defm atomic_load_max : binary_atomic_op_all_as<atomic_load_max>;
defm atomic_load_min : binary_atomic_op_all_as<atomic_load_min>;
defm atomic_load_or : binary_atomic_op_all_as<atomic_load_or>;
defm atomic_load_sub : binary_atomic_op_all_as<atomic_load_sub>;
defm atomic_load_umax : binary_atomic_op_all_as<atomic_load_umax>;
defm atomic_load_umin : binary_atomic_op_all_as<atomic_load_umin>;
defm atomic_load_xor : binary_atomic_op_all_as<atomic_load_xor>;
defm atomic_load_fadd : binary_atomic_op_fp_all_as<atomic_load_fadd>;
defm atomic_load_fmin : binary_atomic_op_fp_all_as<atomic_load_fmin>;
defm atomic_load_fmax : binary_atomic_op_fp_all_as<atomic_load_fmax>;
defm atomic_load_uinc_wrap : binary_atomic_op_all_as<atomic_load_uinc_wrap>;
defm atomic_load_udec_wrap : binary_atomic_op_all_as<atomic_load_udec_wrap>;
defm AMDGPUatomic_cmp_swap : binary_atomic_op_all_as<AMDGPUatomic_cmp_swap>;

def load_align8_local : PatFrag<(ops node:$ptr), (load_local node:$ptr)>,
                       Aligned<8> {
  let IsLoad = 1;
}

def load_align16_local : PatFrag<(ops node:$ptr), (load_local node:$ptr)>,
                        Aligned<16> {
  let IsLoad = 1;
}

def store_align8_local: PatFrag<(ops node:$val, node:$ptr),
                                (store_local node:$val, node:$ptr)>, Aligned<8> {
  let IsStore = 1;
}

def store_align16_local: PatFrag<(ops node:$val, node:$ptr),
                                (store_local node:$val, node:$ptr)>, Aligned<16> {
  let IsStore = 1;
}

let AddressSpaces = StoreAddress_local.AddrSpaces in {
defm atomic_cmp_swap_local : ternary_atomic_op<atomic_cmp_swap>;
defm atomic_cmp_swap_local : noret_ternary_atomic_op<atomic_cmp_swap>;
defm atomic_cmp_swap_local_m0 : noret_ternary_atomic_op<atomic_cmp_swap_glue>;
defm atomic_cmp_swap_local_m0 : ternary_atomic_op<atomic_cmp_swap_glue>;
}

let AddressSpaces = StoreAddress_region.AddrSpaces in {
defm atomic_cmp_swap_region : noret_ternary_atomic_op<atomic_cmp_swap>;
defm atomic_cmp_swap_region_m0 : noret_ternary_atomic_op<atomic_cmp_swap_glue>;
defm atomic_cmp_swap_region_m0 : ternary_atomic_op<atomic_cmp_swap_glue>;
}

//===----------------------------------------------------------------------===//
// Misc Pattern Fragments
//===----------------------------------------------------------------------===//

class Constants {
int TWO_PI = 0x40c90fdb;
int PI = 0x40490fdb;
int TWO_PI_INV = 0x3e22f983;
int FP_4294966784 = 0x4f7ffffe; // 4294966784 = 4294967296 - 512 = 2^32 - 2^9
int FP16_ONE = 0x3C00;
int FP16_NEG_ONE = 0xBC00;
int FP32_ONE = 0x3f800000;
int FP32_NEG_ONE = 0xbf800000;
int FP64_ONE = 0x3ff0000000000000;
int FP64_NEG_ONE = 0xbff0000000000000;
}
def CONST : Constants;

def FP_ZERO : PatLeaf <
  (fpimm),
  [{return N->getValueAPF().isZero();}]
>;

def FP_ONE : PatLeaf <
  (fpimm),
  [{return N->isExactlyValue(1.0);}]
>;

def FP_HALF : PatLeaf <
  (fpimm),
  [{return N->isExactlyValue(0.5);}]
>;

/* Generic helper patterns for intrinsics */
/* -------------------------------------- */

class POW_Common <AMDGPUInst log_ieee, AMDGPUInst exp_ieee, AMDGPUInst mul>
  : AMDGPUPat <
  (fpow f32:$src0, f32:$src1),
  (exp_ieee (mul f32:$src1, (log_ieee f32:$src0)))
>;

/* Other helper patterns */
/* --------------------- */

/* Extract element pattern */
class Extract_Element <ValueType sub_type, ValueType vec_type, int sub_idx,
                       SubRegIndex sub_reg>
  : AMDGPUPat<
  (sub_type (extractelt vec_type:$src, sub_idx)),
  (EXTRACT_SUBREG $src, sub_reg)
>;

/* Insert element pattern */
class Insert_Element <ValueType elem_type, ValueType vec_type,
                      int sub_idx, SubRegIndex sub_reg>
  : AMDGPUPat <
  (insertelt vec_type:$vec, elem_type:$elem, sub_idx),
  (INSERT_SUBREG $vec, $elem, sub_reg)
>;

// XXX: Convert to new syntax and use COPY_TO_REG, once the DFAPacketizer
// can handle COPY instructions.
// bitconvert pattern
class BitConvert <ValueType dt, ValueType st, RegisterClass rc> : AMDGPUPat <
  (dt (bitconvert (st rc:$src0))),
  (dt rc:$src0)
>;

// XXX: Convert to new syntax and use COPY_TO_REG, once the DFAPacketizer
// can handle COPY instructions.
class DwordAddrPat<ValueType vt, RegisterClass rc> : AMDGPUPat <
  (vt (AMDGPUdwordaddr (vt rc:$addr))),
  (vt rc:$addr)
>;

// rotr pattern
class ROTRPattern <Instruction BIT_ALIGN> : AMDGPUPat <
  (rotr i32:$src0, i32:$src1),
  (BIT_ALIGN $src0, $src0, $src1)
>;

// Special conversion patterns

def cvt_rpi_i32_f32 : PatFrag <
  (ops node:$src),
  (fp_to_sint (ffloor (fadd $src, FP_HALF))),
  [{ (void) N; return TM.Options.NoNaNsFPMath; }]
>;

def cvt_flr_i32_f32 : PatFrag <
  (ops node:$src),
  (fp_to_sint (ffloor $src)),
  [{ (void)N; return TM.Options.NoNaNsFPMath; }]
>;

let AddedComplexity = 2 in {
class IMad24Pat<Instruction Inst, bit HasClamp = 0> : AMDGPUPat <
  (add (AMDGPUmul_i24 i32:$src0, i32:$src1), i32:$src2),
  !if(HasClamp, (Inst $src0, $src1, $src2, (i1 0)),
                (Inst $src0, $src1, $src2))
>;

class UMad24Pat<Instruction Inst, bit HasClamp = 0> : AMDGPUPat <
  (add (AMDGPUmul_u24 i32:$src0, i32:$src1), i32:$src2),
  !if(HasClamp, (Inst $src0, $src1, $src2, (i1 0)),
                (Inst $src0, $src1, $src2))
>;
} // AddedComplexity.

class RcpPat<Instruction RcpInst, ValueType vt> : AMDGPUPat <
  (fdiv FP_ONE, vt:$src),
  (RcpInst $src)
>;

// Instructions which select to the same v_min_f*
def fminnum_like : PatFrags<(ops node:$src0, node:$src1),
  [(fminnum_ieee node:$src0, node:$src1),
   (fminnum node:$src0, node:$src1)]
>;

// Instructions which select to the same v_max_f*
def fmaxnum_like : PatFrags<(ops node:$src0, node:$src1),
  [(fmaxnum_ieee node:$src0, node:$src1),
   (fmaxnum node:$src0, node:$src1)]
>;

class NeverNaNPats<dag ops, list<dag> frags> : PatFrags<ops, frags> {
  let PredicateCode = [{
    return CurDAG->isKnownNeverNaN(SDValue(N,0));
  }];
  let GISelPredicateCode = [{
    return isKnownNeverNaN(MI.getOperand(0).getReg(), MRI);
  }];
}

def fminnum_like_nnan : NeverNaNPats<(ops node:$src0, node:$src1),
  [(fminnum_ieee node:$src0, node:$src1),
   (fminnum node:$src0, node:$src1)]
>;

def fmaxnum_like_nnan : NeverNaNPats<(ops node:$src0, node:$src1),
  [(fmaxnum_ieee node:$src0, node:$src1),
   (fmaxnum node:$src0, node:$src1)]
>;

def fminnum_like_oneuse : PatFrags<(ops node:$src0, node:$src1),
  [(fminnum_ieee_oneuse node:$src0, node:$src1),
   (fminnum_oneuse node:$src0, node:$src1)]
>;

def fmaxnum_like_oneuse : PatFrags<(ops node:$src0, node:$src1),
  [(fmaxnum_ieee_oneuse node:$src0, node:$src1),
   (fmaxnum_oneuse node:$src0, node:$src1)]
>;

def any_fmad : PatFrags<(ops node:$src0, node:$src1, node:$src2),
  [(fmad node:$src0, node:$src1, node:$src2),
   (AMDGPUfmad_ftz node:$src0, node:$src1, node:$src2)]
>;

// FIXME: fsqrt should not select directly
def any_amdgcn_sqrt : PatFrags<(ops node:$src0),
  [(fsqrt node:$src0), (int_amdgcn_sqrt node:$src0)]
>;
