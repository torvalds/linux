//===-- RISCVInstrInfoV.td - RISC-V 'V' instructions -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file describes the RISC-V instructions from the standard 'V' Vector
/// extension, version 1.0.
///
//===----------------------------------------------------------------------===//

include "RISCVInstrFormatsV.td"

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

class VTypeIAsmOperand<int VTypeINum> : AsmOperandClass {
  let Name = "VTypeI" # VTypeINum;
  let ParserMethod = "parseVTypeI";
  let DiagnosticType = "InvalidVTypeI";
  let RenderMethod = "addVTypeIOperands";
}

class VTypeIOp<int VTypeINum> : RISCVOp {
  let ParserMatchClass = VTypeIAsmOperand<VTypeINum>;
  let PrintMethod = "printVTypeI";
  let DecoderMethod = "decodeUImmOperand<"#VTypeINum#">";
  let OperandType = "OPERAND_VTYPEI" # VTypeINum;
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isUInt<VTypeINum>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

def VTypeIOp10 : VTypeIOp<10>;
def VTypeIOp11 : VTypeIOp<11>;

def VMaskAsmOperand : AsmOperandClass {
  let Name = "RVVMaskRegOpOperand";
  let RenderMethod = "addRegOperands";
  let PredicateMethod = "isV0Reg";
  let ParserMethod = "parseMaskReg";
  let IsOptional = 1;
  let DefaultMethod = "defaultMaskRegOp";
  let DiagnosticType = "InvalidVMaskRegister";
}

def VMaskOp : RegisterOperand<VMV0> {
  let ParserMatchClass = VMaskAsmOperand;
  let PrintMethod = "printVMaskReg";
  let EncoderMethod = "getVMaskReg";
  let DecoderMethod = "decodeVMaskReg";
}

def simm5 : RISCVSImmLeafOp<5> {
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isInt<5>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

def SImm5Plus1AsmOperand : AsmOperandClass {
  let Name = "SImm5Plus1";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidSImm5Plus1";
}

def simm5_plus1 : RISCVOp, ImmLeaf<XLenVT,
  [{return (isInt<5>(Imm) && Imm != -16) || Imm == 16;}]> {
  let ParserMatchClass = SImm5Plus1AsmOperand;
  let OperandType = "OPERAND_SIMM5_PLUS1";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return (isInt<5>(Imm) && Imm != -16) || Imm == 16;
    return MCOp.isBareSymbolRef();
  }];
}

def simm5_plus1_nonzero : ImmLeaf<XLenVT,
  [{return Imm != 0 && ((isInt<5>(Imm) && Imm != -16) || Imm == 16);}]>;

//===----------------------------------------------------------------------===//
// Scheduling definitions.
//===----------------------------------------------------------------------===//

// Common class of scheduling definitions.
// `ReadVMergeOp` will be prepended to reads if instruction is masked.
// `ReadVMask` will be appended to reads if instruction is masked.
// Operands:
//   `writes`       SchedWrites that are listed for each explicit def operand
//                  in order.
//   `reads`        SchedReads that are listed for each explicit use operand.
//   `forceMasked`  Forced to be masked (e.g. Add-with-Carry Instructions).
//   `forceMergeOpRead` Force to have read for merge operand.
class SchedCommon<list<SchedWrite> writes, list<SchedRead> reads,
                  string mx = "WorstCase", int sew = 0, bit forceMasked = 0,
                  bit forceMergeOpRead = 0> : Sched<[]> {
  defvar isMasked = !ne(!find(NAME, "_MASK"), -1);
  defvar isMaskedOrForceMasked = !or(forceMasked, isMasked);
  defvar mergeRead = !if(!or(!eq(mx, "WorstCase"), !eq(sew, 0)),
                            !cast<SchedRead>("ReadVMergeOp_" # mx),
                            !cast<SchedRead>("ReadVMergeOp_" # mx # "_E" #sew));
  defvar needsMergeRead = !or(isMaskedOrForceMasked, forceMergeOpRead);
  defvar readsWithMask =
      !if(isMaskedOrForceMasked, !listconcat(reads, [ReadVMask]), reads);
  defvar allReads =
      !if(needsMergeRead, !listconcat([mergeRead], readsWithMask), reads);
  let SchedRW = !listconcat(writes, allReads);
}

// Common class of scheduling definitions for n-ary instructions.
// The scheudling resources are relevant to LMUL and may be relevant to SEW.
class SchedNary<string write, list<string> reads, string mx, int sew = 0,
                bit forceMasked = 0, bit forceMergeOpRead = 0>
    : SchedCommon<[!cast<SchedWrite>(
                      !if(sew,
                          write # "_" # mx # "_E" # sew,
                          write # "_" # mx))],
                  !foreach(read, reads,
                           !cast<SchedRead>(!if(sew, read #"_" #mx #"_E" #sew,
                                                 read #"_" #mx))),
                  mx, sew, forceMasked, forceMergeOpRead>;

// Classes with postfix "MC" are only used in MC layer.
// For these classes, we assume that they are with the worst case costs and
// `ReadVMask` is always needed (with some exceptions).

// For instructions with no operand.
class SchedNullary<string write, string mx, int sew = 0, bit forceMasked = 0,
                   bit forceMergeOpRead = 0>:
  SchedNary<write, [], mx, sew, forceMasked, forceMergeOpRead>;
class SchedNullaryMC<string write, bit forceMasked = 1>:
  SchedNullary<write, "WorstCase", forceMasked=forceMasked>;

// For instructions with one operand.
class SchedUnary<string write, string read0, string mx, int sew = 0,
                 bit forceMasked = 0, bit forceMergeOpRead = 0>:
  SchedNary<write, [read0], mx, sew, forceMasked, forceMergeOpRead>;
class SchedUnaryMC<string write, string read0, bit forceMasked = 1>:
  SchedUnary<write, read0, "WorstCase", forceMasked=forceMasked>;

// For instructions with two operands.
class SchedBinary<string write, string read0, string read1, string mx,
                  int sew = 0, bit forceMasked = 0, bit forceMergeOpRead = 0>
    : SchedNary<write, [read0, read1], mx, sew, forceMasked, forceMergeOpRead>;
class SchedBinaryMC<string write, string read0, string read1,
                    bit forceMasked = 1>:
  SchedBinary<write, read0, read1, "WorstCase", forceMasked=forceMasked>;

// For instructions with three operands.
class SchedTernary<string write, string read0, string read1, string read2,
                   string mx, int sew = 0, bit forceMasked = 0>
    : SchedNary<write, [read0, read1, read2], mx, sew, forceMasked>;
class SchedTernaryMC<string write, string read0, string read1, string read2,
                     int sew = 0, bit forceMasked = 1>:
  SchedNary<write, [read0, read1, read2], "WorstCase", sew, forceMasked>;

// For reduction instructions.
class SchedReduction<string write, string read, string mx, int sew,
                     bit forceMergeOpRead = 0>
    : SchedCommon<[!cast<SchedWrite>(write #"_" #mx #"_E" #sew)],
                  !listsplat(!cast<SchedRead>(read), 3), mx, sew, forceMergeOpRead>;
class SchedReductionMC<string write, string readV, string readV0>:
  SchedCommon<[!cast<SchedWrite>(write # "_WorstCase")],
              [!cast<SchedRead>(readV), !cast<SchedRead>(readV0)],
              forceMasked=1>;

// Whole Vector Register Move
class VMVRSched<int n> : SchedCommon<
  [!cast<SchedWrite>("WriteVMov" # n # "V")],
  [!cast<SchedRead>("ReadVMov" # n # "V")]
>;

// Vector Unit-Stride Loads and Stores
class VLESched<string lmul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVLDE_" # lmul)],
  [ReadVLDX], mx=lmul, forceMasked=forceMasked
>;
class VLESchedMC : VLESched<"WorstCase", forceMasked=1>;

class VSESched<string lmul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVSTE_" # lmul)],
  [!cast<SchedRead>("ReadVSTEV_" # lmul), ReadVSTX], mx=lmul,
  forceMasked=forceMasked
>;
class VSESchedMC : VSESched<"WorstCase", forceMasked=1>;

// Vector Strided Loads and Stores
class VLSSched<int eew, string emul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVLDS" # eew # "_" # emul)],
  [ReadVLDX, ReadVLDSX], emul, eew, forceMasked
>;
class VLSSchedMC<int eew> : VLSSched<eew, "WorstCase", forceMasked=1>;

class VSSSched<int eew, string emul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVSTS" # eew # "_" # emul)],
  [!cast<SchedRead>("ReadVSTS" # eew # "V_" # emul), ReadVSTX, ReadVSTSX],
  emul, eew, forceMasked
>;
class VSSSchedMC<int eew> : VSSSched<eew, "WorstCase", forceMasked=1>;

// Vector Indexed Loads and Stores
class VLXSched<int dataEEW, bit isOrdered, string dataEMUL, string idxEMUL,
               bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVLD" # !if(isOrdered, "O", "U") # "X" # dataEEW # "_" # dataEMUL)],
  [ReadVLDX, !cast<SchedRead>("ReadVLD" # !if(isOrdered, "O", "U") # "XV_" # idxEMUL)],
  dataEMUL, dataEEW, forceMasked
>;
class VLXSchedMC<int dataEEW, bit isOrdered>:
  VLXSched<dataEEW, isOrdered, "WorstCase", "WorstCase", forceMasked=1>;

class VSXSched<int dataEEW, bit isOrdered, string dataEMUL, string idxEMUL,
               bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVST" # !if(isOrdered, "O", "U") # "X" # dataEEW # "_" # dataEMUL)],
  [!cast<SchedRead>("ReadVST" # !if(isOrdered, "O", "U") #"X" # dataEEW # "_" # dataEMUL),
   ReadVSTX, !cast<SchedRead>("ReadVST" # !if(isOrdered, "O", "U") # "XV_" # idxEMUL)],
  dataEMUL, dataEEW, forceMasked
>;
class VSXSchedMC<int dataEEW, bit isOrdered>:
  VSXSched<dataEEW, isOrdered, "WorstCase", "WorstCase", forceMasked=1>;

// Unit-stride Fault-Only-First Loads
class VLFSched<string lmul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVLDFF_" # lmul)],
  [ReadVLDX], mx=lmul, forceMasked=forceMasked
>;
class VLFSchedMC: VLFSched<"WorstCase", forceMasked=1>;

// Unit-Stride Segment Loads and Stores
class VLSEGSched<int nf, int eew, string emul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVLSEG" #nf #"e" #eew #"_" #emul)],
  [ReadVLDX], emul, eew, forceMasked
>;
class VLSEGSchedMC<int nf, int eew> : VLSEGSched<nf, eew, "WorstCase",
                                                 forceMasked=1>;

class VSSEGSched<int nf, int eew, string emul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVSSEG" # nf # "e" # eew # "_" # emul)],
  [!cast<SchedRead>("ReadVSTEV_" #emul), ReadVSTX], emul, eew, forceMasked
>;
class VSSEGSchedMC<int nf, int eew> : VSSEGSched<nf, eew, "WorstCase",
                                                 forceMasked=1>;

class VLSEGFFSched<int nf, int eew, string emul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVLSEGFF" # nf # "e" # eew # "_" # emul)],
  [ReadVLDX], emul, eew, forceMasked
>;
class VLSEGFFSchedMC<int nf, int eew> : VLSEGFFSched<nf, eew, "WorstCase",
                                                     forceMasked=1>;

// Strided Segment Loads and Stores
class VLSSEGSched<int nf, int eew, string emul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVLSSEG" #nf #"e" #eew #"_" #emul)],
  [ReadVLDX, ReadVLDSX], emul, eew, forceMasked
>;
class VLSSEGSchedMC<int nf, int eew> : VLSSEGSched<nf, eew, "WorstCase",
                                                   forceMasked=1>;

class VSSSEGSched<int nf, int eew, string emul, bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVSSSEG" #nf #"e" #eew #"_" #emul)],
  [!cast<SchedRead>("ReadVSTS" #eew #"V_" #emul),
   ReadVSTX, ReadVSTSX], emul, eew, forceMasked
>;
class VSSSEGSchedMC<int nf, int eew> : VSSSEGSched<nf, eew, "WorstCase",
                                                   forceMasked=1>;

// Indexed Segment Loads and Stores
class VLXSEGSched<int nf, int eew, bit isOrdered, string emul,
                  bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVL" #!if(isOrdered, "O", "U") #"XSEG" #nf #"e" #eew #"_" #emul)],
  [ReadVLDX, !cast<SchedRead>("ReadVLD" #!if(isOrdered, "O", "U") #"XV_" #emul)],
  emul, eew, forceMasked
>;
class VLXSEGSchedMC<int nf, int eew, bit isOrdered>:
  VLXSEGSched<nf, eew, isOrdered, "WorstCase", forceMasked=1>;

// Passes sew=0 instead of eew=0 since this pseudo does not follow MX_E form.
class VSXSEGSched<int nf, int eew, bit isOrdered, string emul,
                  bit forceMasked = 0> : SchedCommon<
  [!cast<SchedWrite>("WriteVS" #!if(isOrdered, "O", "U") #"XSEG" #nf #"e" #eew #"_" #emul)],
  [!cast<SchedRead>("ReadVST" #!if(isOrdered, "O", "U") #"X" #eew #"_" #emul),
   ReadVSTX, !cast<SchedRead>("ReadVST" #!if(isOrdered, "O", "U") #"XV_" #emul)],
  emul, sew=0, forceMasked=forceMasked
>;
class VSXSEGSchedMC<int nf, int eew, bit isOrdered>:
  VSXSEGSched<nf, eew, isOrdered, "WorstCase", forceMasked=1>;

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
// unit-stride load vd, (rs1), vm
class VUnitStrideLoad<RISCVWidth width, string opcodestr>
    : RVInstVLU<0b000, width.Value{3}, LUMOPUnitStride, width.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1, VMaskOp:$vm), opcodestr, "$vd, ${rs1}$vm">;

let vm = 1, RVVConstraint = NoConstraint in {
// unit-stride whole register load vl<nf>r.v vd, (rs1)
class VWholeLoad<bits<3> nf, RISCVWidth width, string opcodestr, RegisterClass VRC>
    : RVInstVLU<nf, width.Value{3}, LUMOPUnitStrideWholeReg,
                width.Value{2-0}, (outs VRC:$vd), (ins GPRMemZeroOffset:$rs1),
                opcodestr, "$vd, $rs1"> {
  let Uses = [];
}

// unit-stride mask load vd, (rs1)
class VUnitStrideLoadMask<string opcodestr>
    : RVInstVLU<0b000, LSWidth8.Value{3}, LUMOPUnitStrideMask, LSWidth8.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1), opcodestr, "$vd, $rs1">;
} // vm = 1, RVVConstraint = NoConstraint

// unit-stride fault-only-first load vd, (rs1), vm
class VUnitStrideLoadFF<RISCVWidth width, string opcodestr>
    : RVInstVLU<0b000, width.Value{3}, LUMOPUnitStrideFF, width.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1, VMaskOp:$vm), opcodestr, "$vd, ${rs1}$vm">;

// strided load vd, (rs1), rs2, vm
class VStridedLoad<RISCVWidth width, string opcodestr>
    : RVInstVLS<0b000, width.Value{3}, width.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1, GPR:$rs2, VMaskOp:$vm), opcodestr,
                "$vd, $rs1, $rs2$vm">;

// indexed load vd, (rs1), vs2, vm
class VIndexedLoad<RISCVMOP mop, RISCVWidth width, string opcodestr>
    : RVInstVLX<0b000, width.Value{3}, mop, width.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1, VR:$vs2, VMaskOp:$vm), opcodestr,
                "$vd, $rs1, $vs2$vm">;

// unit-stride segment load vd, (rs1), vm
class VUnitStrideSegmentLoad<bits<3> nf, RISCVWidth width, string opcodestr>
    : RVInstVLU<nf, width.Value{3}, LUMOPUnitStride, width.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1, VMaskOp:$vm), opcodestr, "$vd, ${rs1}$vm">;

// segment fault-only-first load vd, (rs1), vm
class VUnitStrideSegmentLoadFF<bits<3> nf, RISCVWidth width, string opcodestr>
    : RVInstVLU<nf, width.Value{3}, LUMOPUnitStrideFF, width.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1, VMaskOp:$vm), opcodestr, "$vd, ${rs1}$vm">;

// strided segment load vd, (rs1), rs2, vm
class VStridedSegmentLoad<bits<3> nf, RISCVWidth width, string opcodestr>
    : RVInstVLS<nf, width.Value{3}, width.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1, GPR:$rs2, VMaskOp:$vm), opcodestr,
                "$vd, $rs1, $rs2$vm">;

// indexed segment load vd, (rs1), vs2, vm
class VIndexedSegmentLoad<bits<3> nf, RISCVMOP mop, RISCVWidth width,
                          string opcodestr>
    : RVInstVLX<nf, width.Value{3}, mop, width.Value{2-0},
                (outs VR:$vd),
                (ins GPRMemZeroOffset:$rs1, VR:$vs2, VMaskOp:$vm), opcodestr,
                "$vd, $rs1, $vs2$vm">;
} // hasSideEffects = 0, mayLoad = 1, mayStore = 0

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
// unit-stride store vd, vs3, (rs1), vm
class VUnitStrideStore<RISCVWidth width, string opcodestr>
    : RVInstVSU<0b000, width.Value{3}, SUMOPUnitStride, width.Value{2-0},
                (outs), (ins VR:$vs3, GPRMemZeroOffset:$rs1, VMaskOp:$vm), opcodestr,
                "$vs3, ${rs1}$vm">;

let vm = 1 in {
// vs<nf>r.v vd, (rs1)
class VWholeStore<bits<3> nf, string opcodestr, RegisterClass VRC>
    : RVInstVSU<nf, 0, SUMOPUnitStrideWholeReg,
                0b000, (outs), (ins VRC:$vs3, GPRMemZeroOffset:$rs1),
                opcodestr, "$vs3, $rs1"> {
  let Uses = [];
}

// unit-stride mask store vd, vs3, (rs1)
class VUnitStrideStoreMask<string opcodestr>
    : RVInstVSU<0b000, LSWidth8.Value{3}, SUMOPUnitStrideMask, LSWidth8.Value{2-0},
                (outs), (ins VR:$vs3, GPRMemZeroOffset:$rs1), opcodestr,
                "$vs3, $rs1">;
} // vm = 1

// strided store vd, vs3, (rs1), rs2, vm
class VStridedStore<RISCVWidth width, string opcodestr>
    : RVInstVSS<0b000, width.Value{3}, width.Value{2-0}, (outs),
                (ins VR:$vs3, GPRMemZeroOffset:$rs1, GPR:$rs2, VMaskOp:$vm),
                opcodestr, "$vs3, $rs1, $rs2$vm">;

// indexed store vd, vs3, (rs1), vs2, vm
class VIndexedStore<RISCVMOP mop, RISCVWidth width, string opcodestr>
    : RVInstVSX<0b000, width.Value{3}, mop, width.Value{2-0}, (outs),
                (ins VR:$vs3, GPRMemZeroOffset:$rs1, VR:$vs2, VMaskOp:$vm),
                opcodestr, "$vs3, $rs1, $vs2$vm">;

// segment store vd, vs3, (rs1), vm
class VUnitStrideSegmentStore<bits<3> nf, RISCVWidth width, string opcodestr>
    : RVInstVSU<nf, width.Value{3}, SUMOPUnitStride, width.Value{2-0},
                (outs), (ins VR:$vs3, GPRMemZeroOffset:$rs1, VMaskOp:$vm), opcodestr,
                "$vs3, ${rs1}$vm">;

// segment store vd, vs3, (rs1), rs2, vm
class VStridedSegmentStore<bits<3> nf, RISCVWidth width, string opcodestr>
    : RVInstVSS<nf, width.Value{3}, width.Value{2-0}, (outs),
                (ins VR:$vs3, GPRMemZeroOffset:$rs1, GPR:$rs2, VMaskOp:$vm),
                opcodestr, "$vs3, $rs1, $rs2$vm">;

// segment store vd, vs3, (rs1), vs2, vm
class VIndexedSegmentStore<bits<3> nf, RISCVMOP mop, RISCVWidth width,
                           string opcodestr>
    : RVInstVSX<nf, width.Value{3}, mop, width.Value{2-0}, (outs),
                (ins VR:$vs3, GPRMemZeroOffset:$rs1, VR:$vs2, VMaskOp:$vm),
                opcodestr, "$vs3, $rs1, $vs2$vm">;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 1

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
// op vd, vs2, vs1, vm
class VALUVV<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : RVInstVV<funct6, opv, (outs VR:$vd),
                (ins VR:$vs2, VR:$vs1, VMaskOp:$vm),
                opcodestr, "$vd, $vs2, $vs1$vm">;

// op vd, vs2, vs1, v0 (without mask, use v0 as carry input)
class VALUmVV<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : RVInstVV<funct6, opv, (outs VR:$vd),
                (ins VR:$vs2, VR:$vs1, VMV0:$v0),
                opcodestr, "$vd, $vs2, $vs1, v0"> {
  let vm = 0;
}

// op vd, vs1, vs2, vm (reverse the order of vs1 and vs2)
class VALUrVV<bits<6> funct6, RISCVVFormat opv, string opcodestr,
              bit EarlyClobber = 0>
    : RVInstVV<funct6, opv, (outs VR:$vd_wb),
                (ins VR:$vd, VR:$vs1, VR:$vs2, VMaskOp:$vm),
                opcodestr, "$vd, $vs1, $vs2$vm"> {
  let Constraints = !if(EarlyClobber, "@earlyclobber $vd_wb, $vd = $vd_wb",
                                      "$vd = $vd_wb");
}

// op vd, vs2, vs1
class VALUVVNoVm<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : RVInstVV<funct6, opv, (outs VR:$vd),
               (ins VR:$vs2, VR:$vs1),
               opcodestr, "$vd, $vs2, $vs1"> {
  let vm = 1;
}

// op vd, vs2, rs1, vm
class VALUVX<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : RVInstVX<funct6, opv, (outs VR:$vd),
                (ins VR:$vs2, GPR:$rs1, VMaskOp:$vm),
                opcodestr, "$vd, $vs2, $rs1$vm">;

// op vd, vs2, rs1, v0 (without mask, use v0 as carry input)
class VALUmVX<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : RVInstVX<funct6, opv, (outs VR:$vd),
                (ins VR:$vs2, GPR:$rs1, VMV0:$v0),
                opcodestr, "$vd, $vs2, $rs1, v0"> {
  let vm = 0;
}

// op vd, rs1, vs2, vm (reverse the order of rs1 and vs2)
class VALUrVX<bits<6> funct6, RISCVVFormat opv, string opcodestr,
              bit EarlyClobber = 0>
    : RVInstVX<funct6, opv, (outs VR:$vd_wb),
                (ins VR:$vd, GPR:$rs1, VR:$vs2, VMaskOp:$vm),
                opcodestr, "$vd, $rs1, $vs2$vm"> {
  let Constraints = !if(EarlyClobber, "@earlyclobber $vd_wb, $vd = $vd_wb",
                                      "$vd = $vd_wb");
}

// op vd, vs1, vs2
class VALUVXNoVm<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : RVInstVX<funct6, opv, (outs VR:$vd),
               (ins VR:$vs2, GPR:$rs1),
               opcodestr, "$vd, $vs2, $rs1"> {
  let vm = 1;
}

// op vd, vs2, imm, vm
class VALUVI<bits<6> funct6, string opcodestr, Operand optype = simm5>
    : RVInstIVI<funct6, (outs VR:$vd),
                (ins VR:$vs2, optype:$imm, VMaskOp:$vm),
                opcodestr, "$vd, $vs2, $imm$vm">;

// op vd, vs2, imm, v0 (without mask, use v0 as carry input)
class VALUmVI<bits<6> funct6, string opcodestr, Operand optype = simm5>
    : RVInstIVI<funct6, (outs VR:$vd),
                (ins VR:$vs2, optype:$imm, VMV0:$v0),
                opcodestr, "$vd, $vs2, $imm, v0"> {
  let vm = 0;
}

// op vd, vs2, imm, vm
class VALUVINoVm<bits<6> funct6, string opcodestr, Operand optype = simm5>
    : RVInstIVI<funct6, (outs VR:$vd),
                (ins VR:$vs2, optype:$imm),
                opcodestr, "$vd, $vs2, $imm"> {
  let vm = 1;
}

// op vd, vs2, rs1, vm (Float)
class VALUVF<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : RVInstVX<funct6, opv, (outs VR:$vd),
                (ins VR:$vs2, FPR32:$rs1, VMaskOp:$vm),
                opcodestr, "$vd, $vs2, $rs1$vm">;

// op vd, rs1, vs2, vm (Float) (with mask, reverse the order of rs1 and vs2)
class VALUrVF<bits<6> funct6, RISCVVFormat opv, string opcodestr,
              bit EarlyClobber = 0>
    : RVInstVX<funct6, opv, (outs VR:$vd_wb),
                (ins VR:$vd, FPR32:$rs1, VR:$vs2, VMaskOp:$vm),
                opcodestr, "$vd, $rs1, $vs2$vm"> {
  let Constraints = !if(EarlyClobber, "@earlyclobber $vd_wb, $vd = $vd_wb",
                                      "$vd = $vd_wb");
}

// op vd, vs2, vm (use vs1 as instruction encoding)
class VALUVs2<bits<6> funct6, bits<5> vs1, RISCVVFormat opv, string opcodestr>
    : RVInstV<funct6, vs1, opv, (outs VR:$vd),
               (ins VR:$vs2, VMaskOp:$vm),
               opcodestr, "$vd, $vs2$vm">;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

//===----------------------------------------------------------------------===//
// Combination of instruction classes.
// Use these multiclasses to define instructions more easily.
//===----------------------------------------------------------------------===//

multiclass VIndexLoadStore<int eew> {
  defvar w = !cast<RISCVWidth>("LSWidth" # eew);

  def VLUXEI # eew # _V :
    VIndexedLoad<MOPLDIndexedUnord, w, "vluxei" # eew # ".v">,
    VLXSchedMC<eew, isOrdered=0>;
  def VLOXEI # eew # _V :
    VIndexedLoad<MOPLDIndexedOrder, w, "vloxei" # eew # ".v">,
    VLXSchedMC<eew, isOrdered=1>;

  def VSUXEI # eew # _V :
    VIndexedStore<MOPSTIndexedUnord, w, "vsuxei" # eew # ".v">,
    VSXSchedMC<eew, isOrdered=0>;
  def VSOXEI # eew # _V :
    VIndexedStore<MOPSTIndexedOrder, w, "vsoxei" # eew # ".v">,
    VSXSchedMC<eew, isOrdered=1>;
}

multiclass VALU_IV_V<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVIALUV", "ReadVIALUV", "ReadVIALUV">;
}

multiclass VALU_IV_X<string opcodestr, bits<6> funct6> {
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVIALUX", "ReadVIALUV", "ReadVIALUX">;
}

multiclass VALU_IV_I<string opcodestr, bits<6> funct6> {
  def I  : VALUVI<funct6, opcodestr # ".vi">,
           SchedUnaryMC<"WriteVIALUI", "ReadVIALUV">;
}

multiclass VALU_IV_V_X_I<string opcodestr, bits<6> funct6>
    : VALU_IV_V<opcodestr, funct6>,
      VALU_IV_X<opcodestr, funct6>,
      VALU_IV_I<opcodestr, funct6>;

multiclass VALU_IV_V_X<string opcodestr, bits<6> funct6>
    : VALU_IV_V<opcodestr, funct6>,
      VALU_IV_X<opcodestr, funct6>;

multiclass VALU_IV_X_I<string opcodestr, bits<6> funct6>
    : VALU_IV_X<opcodestr, funct6>,
      VALU_IV_I<opcodestr, funct6>;

multiclass VALU_MV_V_X<string opcodestr, bits<6> funct6, string vw> {
  def V  : VALUVV<funct6, OPMVV, opcodestr # "." # vw # "v">,
           SchedBinaryMC<"WriteVIWALUV", "ReadVIWALUV", "ReadVIWALUV">;
  def X  : VALUVX<funct6, OPMVX, opcodestr # "." # vw # "x">,
           SchedBinaryMC<"WriteVIWALUX", "ReadVIWALUV", "ReadVIWALUX">;
}

multiclass VMAC_MV_V_X<string opcodestr, bits<6> funct6> {
  def V : VALUrVV<funct6, OPMVV, opcodestr # ".vv">,
          SchedTernaryMC<"WriteVIMulAddV", "ReadVIMulAddV", "ReadVIMulAddV",
                         "ReadVIMulAddV">;
  def X : VALUrVX<funct6, OPMVX, opcodestr # ".vx">,
          SchedTernaryMC<"WriteVIMulAddX", "ReadVIMulAddV", "ReadVIMulAddX",
                         "ReadVIMulAddV">;
}

multiclass VWMAC_MV_X<string opcodestr, bits<6> funct6> {
  let RVVConstraint = WidenV in
  def X : VALUrVX<funct6, OPMVX, opcodestr # ".vx">,
          SchedTernaryMC<"WriteVIWMulAddX", "ReadVIWMulAddV", "ReadVIWMulAddX",
                         "ReadVIWMulAddV">;
}

multiclass VWMAC_MV_V_X<string opcodestr, bits<6> funct6>
   : VWMAC_MV_X<opcodestr, funct6> {
  let RVVConstraint = WidenV in
  def V : VALUrVV<funct6, OPMVV, opcodestr # ".vv", EarlyClobber=1>,
          SchedTernaryMC<"WriteVIWMulAddV", "ReadVIWMulAddV", "ReadVIWMulAddV",
                         "ReadVIWMulAddV">;
}

multiclass VALU_MV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPMVV, opcodestr>,
           SchedUnaryMC<"WriteVExtV", "ReadVExtV">;
}

multiclass VMRG_IV_V_X_I<string opcodestr, bits<6> funct6> {
  def VM : VALUmVV<funct6, OPIVV, opcodestr # ".vvm">,
           SchedBinaryMC<"WriteVIMergeV", "ReadVIMergeV", "ReadVIMergeV">;
  def XM : VALUmVX<funct6, OPIVX, opcodestr # ".vxm">,
           SchedBinaryMC<"WriteVIMergeX", "ReadVIMergeV", "ReadVIMergeX">;
  def IM : VALUmVI<funct6, opcodestr # ".vim">,
           SchedUnaryMC<"WriteVIMergeI", "ReadVIMergeV">;
}

multiclass VALUm_IV_V_X<string opcodestr, bits<6> funct6> {
  def VM : VALUmVV<funct6, OPIVV, opcodestr # ".vvm">,
           SchedBinaryMC<"WriteVICALUV", "ReadVICALUV", "ReadVICALUV">;
  def XM : VALUmVX<funct6, OPIVX, opcodestr # ".vxm">,
           SchedBinaryMC<"WriteVICALUX", "ReadVICALUV", "ReadVICALUX">;
}

multiclass VALUm_IV_V_X_I<string opcodestr, bits<6> funct6>
    : VALUm_IV_V_X<opcodestr, funct6> {
  def IM : VALUmVI<funct6, opcodestr # ".vim">,
           SchedUnaryMC<"WriteVICALUI", "ReadVICALUV">;
}

multiclass VALUNoVm_IV_V_X<string opcodestr, bits<6> funct6> {
  def V : VALUVVNoVm<funct6, OPIVV, opcodestr # ".vv">,
          SchedBinaryMC<"WriteVICALUV", "ReadVICALUV", "ReadVICALUV",
                        forceMasked=0>;
  def X : VALUVXNoVm<funct6, OPIVX, opcodestr # ".vx">,
          SchedBinaryMC<"WriteVICALUX", "ReadVICALUV", "ReadVICALUX",
                        forceMasked=0>;
}

multiclass VALUNoVm_IV_V_X_I<string opcodestr, bits<6> funct6>
   : VALUNoVm_IV_V_X<opcodestr, funct6> {
  def I : VALUVINoVm<funct6, opcodestr # ".vi">,
          SchedUnaryMC<"WriteVICALUI", "ReadVICALUV", forceMasked=0>;
}

multiclass VALU_FV_F<string opcodestr, bits<6> funct6> {
  def F : VALUVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedBinaryMC<"WriteVFALUF", "ReadVFALUV", "ReadVFALUF">;
}

multiclass VALU_FV_V_F<string opcodestr, bits<6> funct6>
    : VALU_FV_F<opcodestr, funct6> {
  def V : VALUVV<funct6, OPFVV, opcodestr # ".vv">,
          SchedBinaryMC<"WriteVFALUV", "ReadVFALUV", "ReadVFALUV">;
}

multiclass VWALU_FV_V_F<string opcodestr, bits<6> funct6, string vw> {
  def V : VALUVV<funct6, OPFVV, opcodestr # "." # vw # "v">,
          SchedBinaryMC<"WriteVFWALUV", "ReadVFWALUV", "ReadVFWALUV">;
  def F : VALUVF<funct6, OPFVF, opcodestr # "." # vw # "f">,
          SchedBinaryMC<"WriteVFWALUF", "ReadVFWALUV", "ReadVFWALUF">;
}

multiclass VMUL_FV_V_F<string opcodestr, bits<6> funct6> {
  def V : VALUVV<funct6, OPFVV, opcodestr # ".vv">,
          SchedBinaryMC<"WriteVFMulV", "ReadVFMulV", "ReadVFMulV">;
  def F : VALUVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedBinaryMC<"WriteVFMulF", "ReadVFMulV", "ReadVFMulF">;
}

multiclass VDIV_FV_F<string opcodestr, bits<6> funct6> {
  def F : VALUVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedBinaryMC<"WriteVFDivF", "ReadVFDivV", "ReadVFDivF">;
}

multiclass VDIV_FV_V_F<string opcodestr, bits<6> funct6>
    : VDIV_FV_F<opcodestr, funct6> {
  def V : VALUVV<funct6, OPFVV, opcodestr # ".vv">,
          SchedBinaryMC<"WriteVFDivV", "ReadVFDivV", "ReadVFDivV">;
}

multiclass VWMUL_FV_V_F<string opcodestr, bits<6> funct6> {
  def V : VALUVV<funct6, OPFVV, opcodestr # ".vv">,
          SchedBinaryMC<"WriteVFWMulV", "ReadVFWMulV", "ReadVFWMulV">;
  def F : VALUVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedBinaryMC<"WriteVFWMulF", "ReadVFWMulV", "ReadVFWMulF">;
}

multiclass VMAC_FV_V_F<string opcodestr, bits<6> funct6> {
  def V : VALUrVV<funct6, OPFVV, opcodestr # ".vv">,
          SchedTernaryMC<"WriteVFMulAddV", "ReadVFMulAddV", "ReadVFMulAddV",
                         "ReadVFMulAddV">;
  def F : VALUrVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedTernaryMC<"WriteVFMulAddF", "ReadVFMulAddV", "ReadVFMulAddF",
                         "ReadVFMulAddV">;
}

multiclass VWMAC_FV_V_F<string opcodestr, bits<6> funct6> {
  let RVVConstraint = WidenV in {
  def V : VALUrVV<funct6, OPFVV, opcodestr # ".vv", EarlyClobber=1>,
          SchedTernaryMC<"WriteVFWMulAddV", "ReadVFWMulAddV", "ReadVFWMulAddV",
                         "ReadVFWMulAddV">;
  def F : VALUrVF<funct6, OPFVF, opcodestr # ".vf", EarlyClobber=1>,
          SchedTernaryMC<"WriteVFWMulAddF", "ReadVFWMulAddV", "ReadVFWMulAddF",
                         "ReadVFWMulAddV">;
  }
}

multiclass VSQR_FV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFSqrtV", "ReadVFSqrtV">;
}

multiclass VRCP_FV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFRecpV", "ReadVFRecpV">;
}

multiclass VMINMAX_FV_V_F<string opcodestr, bits<6> funct6> {
  def V : VALUVV<funct6, OPFVV, opcodestr # ".vv">,
          SchedBinaryMC<"WriteVFMinMaxV", "ReadVFMinMaxV", "ReadVFMinMaxV">;
  def F : VALUVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedBinaryMC<"WriteVFMinMaxF", "ReadVFMinMaxV", "ReadVFMinMaxF">;
}

multiclass VCMP_FV_F<string opcodestr, bits<6> funct6> {
  def F : VALUVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedBinaryMC<"WriteVFCmpF", "ReadVFCmpV", "ReadVFCmpF">;
}

multiclass VCMP_FV_V_F<string opcodestr, bits<6> funct6>
    : VCMP_FV_F<opcodestr, funct6> {
  def V : VALUVV<funct6, OPFVV, opcodestr # ".vv">,
          SchedBinaryMC<"WriteVFCmpV", "ReadVFCmpV", "ReadVFCmpV">;
}

multiclass VSGNJ_FV_V_F<string opcodestr, bits<6> funct6> {
  def V : VALUVV<funct6, OPFVV, opcodestr # ".vv">,
          SchedBinaryMC<"WriteVFSgnjV", "ReadVFSgnjV", "ReadVFSgnjV">;
  def F : VALUVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedBinaryMC<"WriteVFSgnjF", "ReadVFSgnjV", "ReadVFSgnjF">;
}

multiclass VCLS_FV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFClassV", "ReadVFClassV">;
}

multiclass VCVTF_IV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFCvtIToFV", "ReadVFCvtIToFV">;
}

multiclass VCVTI_FV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFCvtFToIV", "ReadVFCvtFToIV">;
}

multiclass VWCVTF_IV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFWCvtIToFV", "ReadVFWCvtIToFV">;
}

multiclass VWCVTI_FV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFWCvtFToIV", "ReadVFWCvtFToIV">;
}

multiclass VWCVTF_FV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFWCvtFToFV", "ReadVFWCvtFToFV">;
}

multiclass VNCVTF_IV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFNCvtIToFV", "ReadVFNCvtIToFV">;
}

multiclass VNCVTI_FV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFNCvtFToIV", "ReadVFNCvtFToIV">;
}

multiclass VNCVTF_FV_VS2<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPFVV, opcodestr>,
           SchedUnaryMC<"WriteVFNCvtFToFV", "ReadVFNCvtFToFV">;
}

multiclass VRED_MV_V<string opcodestr, bits<6> funct6> {
  def _VS : VALUVV<funct6, OPMVV, opcodestr # ".vs">,
            SchedReductionMC<"WriteVIRedV_From", "ReadVIRedV", "ReadVIRedV0">;
}

multiclass VREDMINMAX_MV_V<string opcodestr, bits<6> funct6> {
  def _VS : VALUVV<funct6, OPMVV, opcodestr # ".vs">,
            SchedReductionMC<"WriteVIRedMinMaxV_From", "ReadVIRedV", "ReadVIRedV0">;
}

multiclass VWRED_IV_V<string opcodestr, bits<6> funct6> {
  def _VS : VALUVV<funct6, OPIVV, opcodestr # ".vs">,
            SchedReductionMC<"WriteVIWRedV_From", "ReadVIWRedV", "ReadVIWRedV0">;
}

multiclass VRED_FV_V<string opcodestr, bits<6> funct6> {
  def _VS : VALUVV<funct6, OPFVV, opcodestr # ".vs">,
            SchedReductionMC<"WriteVFRedV_From", "ReadVFRedV", "ReadVFRedV0">;
}

multiclass VREDMINMAX_FV_V<string opcodestr, bits<6> funct6> {
  def _VS : VALUVV<funct6, OPFVV, opcodestr # ".vs">,
            SchedReductionMC<"WriteVFRedMinMaxV_From", "ReadVFRedV", "ReadVFRedV0">;
}

multiclass VREDO_FV_V<string opcodestr, bits<6> funct6> {
  def _VS : VALUVV<funct6, OPFVV, opcodestr # ".vs">,
            SchedReductionMC<"WriteVFRedOV_From", "ReadVFRedOV", "ReadVFRedOV0">;
}

multiclass VWRED_FV_V<string opcodestr, bits<6> funct6> {
  def _VS : VALUVV<funct6, OPFVV, opcodestr # ".vs">,
            SchedReductionMC<"WriteVFWRedV_From", "ReadVFWRedV", "ReadVFWRedV0">;
}

multiclass VWREDO_FV_V<string opcodestr, bits<6> funct6> {
  def _VS : VALUVV<funct6, OPFVV, opcodestr # ".vs">,
            SchedReductionMC<"WriteVFWRedOV_From", "ReadVFWRedOV", "ReadVFWRedOV0">;
}

multiclass VMALU_MV_Mask<string opcodestr, bits<6> funct6, string vm = "v"> {
  def M : VALUVVNoVm<funct6, OPMVV, opcodestr #"." #vm #"m">,
          SchedBinaryMC<"WriteVMALUV", "ReadVMALUV", "ReadVMALUV",
                        forceMasked=0>;
}

multiclass VMSFS_MV_V<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPMVV, opcodestr>,
           SchedUnaryMC<"WriteVMSFSV", "ReadVMSFSV">;
}

multiclass VIOTA_MV_V<string opcodestr, bits<6> funct6, bits<5> vs1> {
  def "" : VALUVs2<funct6, vs1, OPMVV, opcodestr>,
           SchedUnaryMC<"WriteVIotaV", "ReadVIotaV">;
}

multiclass VSHT_IV_V_X_I<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVShiftV", "ReadVShiftV", "ReadVShiftV">;
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVShiftX", "ReadVShiftV", "ReadVShiftX">;
  def I  : VALUVI<funct6, opcodestr # ".vi", uimm5>,
           SchedUnaryMC<"WriteVShiftI", "ReadVShiftV">;
}

multiclass VNSHT_IV_V_X_I<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".wv">,
           SchedBinaryMC<"WriteVNShiftV", "ReadVNShiftV", "ReadVNShiftV">;
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".wx">,
           SchedBinaryMC<"WriteVNShiftX", "ReadVNShiftV", "ReadVNShiftX">;
  def I  : VALUVI<funct6, opcodestr # ".wi", uimm5>,
           SchedUnaryMC<"WriteVNShiftI", "ReadVNShiftV">;
}

multiclass VMINMAX_IV_V_X<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVIMinMaxV", "ReadVIMinMaxV", "ReadVIMinMaxV">;
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVIMinMaxX", "ReadVIMinMaxV", "ReadVIMinMaxX">;
}

multiclass VCMP_IV_V<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVICmpV", "ReadVICmpV", "ReadVICmpV">;
}

multiclass VCMP_IV_X<string opcodestr, bits<6> funct6> {
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVICmpX", "ReadVICmpV", "ReadVICmpX">;
}

multiclass VCMP_IV_I<string opcodestr, bits<6> funct6> {
  def I  : VALUVI<funct6, opcodestr # ".vi">,
           SchedUnaryMC<"WriteVICmpI", "ReadVICmpV">;
}

multiclass VCMP_IV_V_X_I<string opcodestr, bits<6> funct6>
    : VCMP_IV_V<opcodestr, funct6>,
      VCMP_IV_X<opcodestr, funct6>,
      VCMP_IV_I<opcodestr, funct6>;

multiclass VCMP_IV_X_I<string opcodestr, bits<6> funct6>
    : VCMP_IV_X<opcodestr, funct6>,
      VCMP_IV_I<opcodestr, funct6>;

multiclass VCMP_IV_V_X<string opcodestr, bits<6> funct6>
    : VCMP_IV_V<opcodestr, funct6>,
      VCMP_IV_X<opcodestr, funct6>;

multiclass VMUL_MV_V_X<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPMVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVIMulV", "ReadVIMulV", "ReadVIMulV">;
  def X  : VALUVX<funct6, OPMVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVIMulX", "ReadVIMulV", "ReadVIMulX">;
}

multiclass VWMUL_MV_V_X<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPMVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVIWMulV", "ReadVIWMulV", "ReadVIWMulV">;
  def X  : VALUVX<funct6, OPMVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVIWMulX", "ReadVIWMulV", "ReadVIWMulX">;
}

multiclass VDIV_MV_V_X<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPMVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVIDivV", "ReadVIDivV", "ReadVIDivV">;
  def X  : VALUVX<funct6, OPMVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVIDivX", "ReadVIDivV", "ReadVIDivX">;
}

multiclass VSALU_IV_V_X<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVSALUV", "ReadVSALUV", "ReadVSALUV">;
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVSALUX", "ReadVSALUV", "ReadVSALUX">;
}

multiclass VSALU_IV_V_X_I<string opcodestr, bits<6> funct6>
    : VSALU_IV_V_X<opcodestr, funct6> {
  def I  : VALUVI<funct6, opcodestr # ".vi">,
           SchedUnaryMC<"WriteVSALUI", "ReadVSALUV">;
}

multiclass VAALU_MV_V_X<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPMVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVAALUV", "ReadVAALUV", "ReadVAALUV">;
  def X  : VALUVX<funct6, OPMVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVAALUX", "ReadVAALUV", "ReadVAALUX">;
}

multiclass VSMUL_IV_V_X<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVSMulV", "ReadVSMulV", "ReadVSMulV">;
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVSMulX", "ReadVSMulV", "ReadVSMulX">;
}

multiclass VSSHF_IV_V_X_I<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVSShiftV", "ReadVSShiftV", "ReadVSShiftV">;
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVSShiftX", "ReadVSShiftV", "ReadVSShiftX">;
  def I  : VALUVI<funct6, opcodestr # ".vi", uimm5>,
           SchedUnaryMC<"WriteVSShiftI", "ReadVSShiftV">;
}

multiclass VNCLP_IV_V_X_I<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".wv">,
           SchedBinaryMC<"WriteVNClipV", "ReadVNClipV", "ReadVNClipV">;
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".wx">,
           SchedBinaryMC<"WriteVNClipX", "ReadVNClipV", "ReadVNClipX">;
  def I  : VALUVI<funct6, opcodestr # ".wi", uimm5>,
           SchedUnaryMC<"WriteVNClipI", "ReadVNClipV">;
}

multiclass VSLD_IV_X_I<string opcodestr, bits<6> funct6, bit slidesUp> {
  // Note: In the future, if VISlideI is also split into VSlideUpI and
  // VSlideDownI, it'll probably better to use two separate multiclasses.
  defvar WriteSlideX = !if(slidesUp, "WriteVSlideUpX", "WriteVSlideDownX");
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<WriteSlideX, "ReadVISlideV", "ReadVISlideX">;
  def I  : VALUVI<funct6, opcodestr # ".vi", uimm5>,
           SchedUnaryMC<"WriteVSlideI", "ReadVISlideV">;
}

multiclass VSLD1_MV_X<string opcodestr, bits<6> funct6> {
  def X  : VALUVX<funct6, OPMVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVISlide1X", "ReadVISlideV", "ReadVISlideX">;
}

multiclass VSLD1_FV_F<string opcodestr, bits<6> funct6> {
  def F : VALUVF<funct6, OPFVF, opcodestr # ".vf">,
          SchedBinaryMC<"WriteVFSlide1F", "ReadVFSlideV", "ReadVFSlideF">;
}

multiclass VGTR_IV_V_X_I<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPIVV, opcodestr # ".vv">,
           SchedBinaryMC<"WriteVRGatherVV", "ReadVRGatherVV_data",
                         "ReadVRGatherVV_index">;
  def X  : VALUVX<funct6, OPIVX, opcodestr # ".vx">,
           SchedBinaryMC<"WriteVRGatherVX", "ReadVRGatherVX_data",
                         "ReadVRGatherVX_index">;
  def I  : VALUVI<funct6, opcodestr # ".vi", uimm5>,
           SchedUnaryMC<"WriteVRGatherVI", "ReadVRGatherVI_data">;
}

multiclass VCPR_MV_Mask<string opcodestr, bits<6> funct6, string vm = "v"> {
  def M  : VALUVVNoVm<funct6, OPMVV, opcodestr # "." # vm # "m">,
           SchedBinaryMC<"WriteVCompressV", "ReadVCompressV", "ReadVCompressV">;
}

multiclass VWholeLoadN<int l, bits<3> nf, string opcodestr, RegisterClass VRC> {
  defvar w = !cast<RISCVWidth>("LSWidth" # l);
  defvar s = !cast<SchedWrite>("WriteVLD" # !add(nf, 1) # "R");

  def E # l # _V : VWholeLoad<nf, w, opcodestr # "e" # l # ".v", VRC>,
                   Sched<[s, ReadVLDX]>;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasVInstructions] in {
let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in {
def VSETVLI : RVInstSetVLi<(outs GPR:$rd), (ins GPR:$rs1, VTypeIOp11:$vtypei),
                           "vsetvli", "$rd, $rs1, $vtypei">,
                           Sched<[WriteVSETVLI, ReadVSETVLI]>;
def VSETIVLI : RVInstSetiVLi<(outs GPR:$rd), (ins uimm5:$uimm, VTypeIOp10:$vtypei),
                             "vsetivli", "$rd, $uimm, $vtypei">,
                             Sched<[WriteVSETIVLI]>;

def VSETVL : RVInstSetVL<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                         "vsetvl", "$rd, $rs1, $rs2">,
                          Sched<[WriteVSETVL, ReadVSETVL, ReadVSETVL]>;
} // hasSideEffects = 1, mayLoad = 0, mayStore = 0
} // Predicates = [HasVInstructions]

foreach eew = [8, 16, 32, 64] in {
  defvar w = !cast<RISCVWidth>("LSWidth" # eew);

  let Predicates = !if(!eq(eew, 64), [HasVInstructionsI64],
                                     [HasVInstructions]) in {
    // Vector Unit-Stride Instructions
    def VLE#eew#_V : VUnitStrideLoad<w, "vle"#eew#".v">, VLESchedMC;
    def VSE#eew#_V  : VUnitStrideStore<w,  "vse"#eew#".v">, VSESchedMC;

    // Vector Unit-Stride Fault-only-First Loads
    def VLE#eew#FF_V : VUnitStrideLoadFF<w,  "vle"#eew#"ff.v">, VLFSchedMC;

    // Vector Strided Instructions
    def VLSE#eew#_V  : VStridedLoad<w,  "vlse"#eew#".v">, VLSSchedMC<eew>;
    def VSSE#eew#_V  : VStridedStore<w,  "vsse"#eew#".v">, VSSSchedMC<eew>;

    defm VL1R : VWholeLoadN<eew, 0, "vl1r", VR>;
    defm VL2R : VWholeLoadN<eew, 1, "vl2r", VRM2>;
    defm VL4R : VWholeLoadN<eew, 3, "vl4r", VRM4>;
    defm VL8R : VWholeLoadN<eew, 7, "vl8r", VRM8>;
  }

  let Predicates = !if(!eq(eew, 64), [IsRV64, HasVInstructionsI64],
                                     [HasVInstructions]) in
  defm "" : VIndexLoadStore<eew>;
}

let Predicates = [HasVInstructions] in {
def VLM_V : VUnitStrideLoadMask<"vlm.v">,
             Sched<[WriteVLDM_WorstCase, ReadVLDX]>;
def VSM_V : VUnitStrideStoreMask<"vsm.v">,
             Sched<[WriteVSTM_WorstCase, ReadVSTM_WorstCase, ReadVSTX]>;
def : InstAlias<"vle1.v $vd, (${rs1})",
                (VLM_V VR:$vd, GPR:$rs1), 0>;
def : InstAlias<"vse1.v $vs3, (${rs1})",
                (VSM_V VR:$vs3, GPR:$rs1), 0>;

def VS1R_V : VWholeStore<0, "vs1r.v", VR>,
             Sched<[WriteVST1R, ReadVST1R, ReadVSTX]>;
def VS2R_V : VWholeStore<1, "vs2r.v", VRM2>,
             Sched<[WriteVST2R, ReadVST2R, ReadVSTX]>;
def VS4R_V : VWholeStore<3, "vs4r.v", VRM4>,
             Sched<[WriteVST4R, ReadVST4R, ReadVSTX]>;
def VS8R_V : VWholeStore<7, "vs8r.v", VRM8>,
             Sched<[WriteVST8R, ReadVST8R, ReadVSTX]>;

def : InstAlias<"vl1r.v $vd, (${rs1})", (VL1RE8_V VR:$vd, GPR:$rs1)>;
def : InstAlias<"vl2r.v $vd, (${rs1})", (VL2RE8_V VRM2:$vd, GPR:$rs1)>;
def : InstAlias<"vl4r.v $vd, (${rs1})", (VL4RE8_V VRM4:$vd, GPR:$rs1)>;
def : InstAlias<"vl8r.v $vd, (${rs1})", (VL8RE8_V VRM8:$vd, GPR:$rs1)>;
} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructions] in {
// Vector Single-Width Integer Add and Subtract
defm VADD_V : VALU_IV_V_X_I<"vadd", 0b000000>;
defm VSUB_V : VALU_IV_V_X<"vsub", 0b000010>;
defm VRSUB_V : VALU_IV_X_I<"vrsub", 0b000011>;

def : InstAlias<"vneg.v $vd, $vs$vm", (VRSUB_VX VR:$vd, VR:$vs, X0, VMaskOp:$vm)>;
def : InstAlias<"vneg.v $vd, $vs", (VRSUB_VX VR:$vd, VR:$vs, X0, zero_reg)>;

// Vector Widening Integer Add/Subtract
// Refer to 11.2 Widening Vector Arithmetic Instructions
// The destination vector register group cannot overlap a source vector
// register group of a different element width (including the mask register
// if masked), otherwise an illegal instruction exception is raised.
let Constraints = "@earlyclobber $vd" in {
let RVVConstraint = WidenV in {
defm VWADDU_V : VALU_MV_V_X<"vwaddu", 0b110000, "v">;
defm VWSUBU_V : VALU_MV_V_X<"vwsubu", 0b110010, "v">;
defm VWADD_V : VALU_MV_V_X<"vwadd", 0b110001, "v">;
defm VWSUB_V : VALU_MV_V_X<"vwsub", 0b110011, "v">;
} // RVVConstraint = WidenV
// Set earlyclobber for following instructions for second and mask operands.
// This has the downside that the earlyclobber constraint is too coarse and
// will impose unnecessary restrictions by not allowing the destination to
// overlap with the first (wide) operand.
let RVVConstraint = WidenW in {
defm VWADDU_W : VALU_MV_V_X<"vwaddu", 0b110100, "w">;
defm VWSUBU_W : VALU_MV_V_X<"vwsubu", 0b110110, "w">;
defm VWADD_W : VALU_MV_V_X<"vwadd", 0b110101, "w">;
defm VWSUB_W : VALU_MV_V_X<"vwsub", 0b110111, "w">;
} // RVVConstraint = WidenW
} // Constraints = "@earlyclobber $vd"

def : InstAlias<"vwcvt.x.x.v $vd, $vs$vm",
                (VWADD_VX VR:$vd, VR:$vs, X0, VMaskOp:$vm)>;
def : InstAlias<"vwcvt.x.x.v $vd, $vs",
                (VWADD_VX VR:$vd, VR:$vs, X0, zero_reg)>;
def : InstAlias<"vwcvtu.x.x.v $vd, $vs$vm",
                (VWADDU_VX VR:$vd, VR:$vs, X0, VMaskOp:$vm)>;
def : InstAlias<"vwcvtu.x.x.v $vd, $vs",
                (VWADDU_VX VR:$vd, VR:$vs, X0, zero_reg)>;

// Vector Integer Extension
defm VZEXT_VF8 : VALU_MV_VS2<"vzext.vf8", 0b010010, 0b00010>;
defm VSEXT_VF8 : VALU_MV_VS2<"vsext.vf8", 0b010010, 0b00011>;
defm VZEXT_VF4 : VALU_MV_VS2<"vzext.vf4", 0b010010, 0b00100>;
defm VSEXT_VF4 : VALU_MV_VS2<"vsext.vf4", 0b010010, 0b00101>;
defm VZEXT_VF2 : VALU_MV_VS2<"vzext.vf2", 0b010010, 0b00110>;
defm VSEXT_VF2 : VALU_MV_VS2<"vsext.vf2", 0b010010, 0b00111>;

// Vector Integer Add-with-Carry / Subtract-with-Borrow Instructions
defm VADC_V : VALUm_IV_V_X_I<"vadc", 0b010000>;
let Constraints = "@earlyclobber $vd", RVVConstraint = NoConstraint in {
defm VMADC_V : VALUm_IV_V_X_I<"vmadc", 0b010001>;
defm VMADC_V : VALUNoVm_IV_V_X_I<"vmadc", 0b010001>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = NoConstraint
defm VSBC_V : VALUm_IV_V_X<"vsbc", 0b010010>;
let Constraints = "@earlyclobber $vd", RVVConstraint = NoConstraint in {
defm VMSBC_V : VALUm_IV_V_X<"vmsbc", 0b010011>;
defm VMSBC_V : VALUNoVm_IV_V_X<"vmsbc", 0b010011>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = NoConstraint

// Vector Bitwise Logical Instructions
defm VAND_V : VALU_IV_V_X_I<"vand", 0b001001>;
defm VOR_V : VALU_IV_V_X_I<"vor", 0b001010>;
defm VXOR_V : VALU_IV_V_X_I<"vxor", 0b001011>;

def : InstAlias<"vnot.v $vd, $vs$vm",
                (VXOR_VI VR:$vd, VR:$vs, -1, VMaskOp:$vm)>;
def : InstAlias<"vnot.v $vd, $vs",
                (VXOR_VI VR:$vd, VR:$vs, -1, zero_reg)>;

// Vector Single-Width Bit Shift Instructions
defm VSLL_V : VSHT_IV_V_X_I<"vsll", 0b100101>;
defm VSRL_V : VSHT_IV_V_X_I<"vsrl", 0b101000>;
defm VSRA_V : VSHT_IV_V_X_I<"vsra", 0b101001>;

// Vector Narrowing Integer Right Shift Instructions
// Refer to 11.3. Narrowing Vector Arithmetic Instructions
// The destination vector register group cannot overlap the first source
// vector register group (specified by vs2). The destination vector register
// group cannot overlap the mask register if used, unless LMUL=1.
let Constraints = "@earlyclobber $vd" in {
defm VNSRL_W : VNSHT_IV_V_X_I<"vnsrl", 0b101100>;
defm VNSRA_W : VNSHT_IV_V_X_I<"vnsra", 0b101101>;
} // Constraints = "@earlyclobber $vd"

def : InstAlias<"vncvt.x.x.w $vd, $vs$vm",
                (VNSRL_WX VR:$vd, VR:$vs, X0, VMaskOp:$vm)>;
def : InstAlias<"vncvt.x.x.w $vd, $vs",
                (VNSRL_WX VR:$vd, VR:$vs, X0, zero_reg)>;

// Vector Integer Comparison Instructions
let RVVConstraint = NoConstraint in {
defm VMSEQ_V : VCMP_IV_V_X_I<"vmseq", 0b011000>;
defm VMSNE_V : VCMP_IV_V_X_I<"vmsne", 0b011001>;
defm VMSLTU_V : VCMP_IV_V_X<"vmsltu", 0b011010>;
defm VMSLT_V : VCMP_IV_V_X<"vmslt", 0b011011>;
defm VMSLEU_V : VCMP_IV_V_X_I<"vmsleu", 0b011100>;
defm VMSLE_V : VCMP_IV_V_X_I<"vmsle", 0b011101>;
defm VMSGTU_V : VCMP_IV_X_I<"vmsgtu", 0b011110>;
defm VMSGT_V : VCMP_IV_X_I<"vmsgt", 0b011111>;
} // RVVConstraint = NoConstraint

def : InstAlias<"vmsgtu.vv $vd, $va, $vb$vm",
                (VMSLTU_VV VR:$vd, VR:$vb, VR:$va, VMaskOp:$vm), 0>;
def : InstAlias<"vmsgt.vv $vd, $va, $vb$vm",
                (VMSLT_VV VR:$vd, VR:$vb, VR:$va, VMaskOp:$vm), 0>;
def : InstAlias<"vmsgeu.vv $vd, $va, $vb$vm",
                (VMSLEU_VV VR:$vd, VR:$vb, VR:$va, VMaskOp:$vm), 0>;
def : InstAlias<"vmsge.vv $vd, $va, $vb$vm",
                (VMSLE_VV VR:$vd, VR:$vb, VR:$va, VMaskOp:$vm), 0>;

let isCodeGenOnly = 0, isAsmParserOnly = 1, hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in {
// For unsigned comparisons we need to special case 0 immediate to maintain
// the always true/false semantics we would invert if we just decremented the
// immediate like we do for signed. To match the GNU assembler we will use
// vmseq/vmsne.vv with the same register for both operands which we can't do
// from an InstAlias.
def PseudoVMSGEU_VI : Pseudo<(outs VR:$vd),
                             (ins VR:$vs2, simm5_plus1:$imm, VMaskOp:$vm),
                             [], "vmsgeu.vi", "$vd, $vs2, $imm$vm">;
def PseudoVMSLTU_VI : Pseudo<(outs VR:$vd),
                             (ins VR:$vs2, simm5_plus1:$imm, VMaskOp:$vm),
                             [], "vmsltu.vi", "$vd, $vs2, $imm$vm">;
// Handle signed with pseudos as well for more consistency in the
// implementation.
def PseudoVMSGE_VI : Pseudo<(outs VR:$vd),
                            (ins VR:$vs2, simm5_plus1:$imm, VMaskOp:$vm),
                            [], "vmsge.vi", "$vd, $vs2, $imm$vm">;
def PseudoVMSLT_VI : Pseudo<(outs VR:$vd),
                            (ins VR:$vs2, simm5_plus1:$imm, VMaskOp:$vm),
                            [], "vmslt.vi", "$vd, $vs2, $imm$vm">;
}

let isCodeGenOnly = 0, isAsmParserOnly = 1, hasSideEffects = 0, mayLoad = 0,
    mayStore = 0 in {
def PseudoVMSGEU_VX : Pseudo<(outs VR:$vd),
                             (ins VR:$vs2, GPR:$rs1),
                             [], "vmsgeu.vx", "$vd, $vs2, $rs1">;
def PseudoVMSGE_VX : Pseudo<(outs VR:$vd),
                            (ins VR:$vs2, GPR:$rs1),
                            [], "vmsge.vx", "$vd, $vs2, $rs1">;
def PseudoVMSGEU_VX_M : Pseudo<(outs VRNoV0:$vd),
                               (ins VR:$vs2, GPR:$rs1, VMaskOp:$vm),
                               [], "vmsgeu.vx", "$vd, $vs2, $rs1$vm">;
def PseudoVMSGE_VX_M : Pseudo<(outs VRNoV0:$vd),
                              (ins VR:$vs2, GPR:$rs1, VMaskOp:$vm),
                              [], "vmsge.vx", "$vd, $vs2, $rs1$vm">;
def PseudoVMSGEU_VX_M_T : Pseudo<(outs VR:$vd, VRNoV0:$scratch),
                                 (ins VR:$vs2, GPR:$rs1, VMaskOp:$vm),
                                 [], "vmsgeu.vx", "$vd, $vs2, $rs1$vm, $scratch">;
def PseudoVMSGE_VX_M_T : Pseudo<(outs VR:$vd, VRNoV0:$scratch),
                                (ins VR:$vs2, GPR:$rs1, VMaskOp:$vm),
                                [], "vmsge.vx", "$vd, $vs2, $rs1$vm, $scratch">;
}

// Vector Integer Min/Max Instructions
defm VMINU_V : VMINMAX_IV_V_X<"vminu", 0b000100>;
defm VMIN_V : VMINMAX_IV_V_X<"vmin", 0b000101>;
defm VMAXU_V : VMINMAX_IV_V_X<"vmaxu", 0b000110>;
defm VMAX_V : VMINMAX_IV_V_X<"vmax", 0b000111>;

// Vector Single-Width Integer Multiply Instructions
defm VMUL_V : VMUL_MV_V_X<"vmul", 0b100101>;
defm VMULH_V : VMUL_MV_V_X<"vmulh", 0b100111>;
defm VMULHU_V : VMUL_MV_V_X<"vmulhu", 0b100100>;
defm VMULHSU_V : VMUL_MV_V_X<"vmulhsu", 0b100110>;

// Vector Integer Divide Instructions
defm VDIVU_V : VDIV_MV_V_X<"vdivu", 0b100000>;
defm VDIV_V : VDIV_MV_V_X<"vdiv", 0b100001>;
defm VREMU_V : VDIV_MV_V_X<"vremu", 0b100010>;
defm VREM_V : VDIV_MV_V_X<"vrem", 0b100011>;

// Vector Widening Integer Multiply Instructions
let Constraints = "@earlyclobber $vd", RVVConstraint = WidenV in {
defm VWMUL_V : VWMUL_MV_V_X<"vwmul", 0b111011>;
defm VWMULU_V : VWMUL_MV_V_X<"vwmulu", 0b111000>;
defm VWMULSU_V : VWMUL_MV_V_X<"vwmulsu", 0b111010>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = WidenV

// Vector Single-Width Integer Multiply-Add Instructions
defm VMACC_V : VMAC_MV_V_X<"vmacc", 0b101101>;
defm VNMSAC_V : VMAC_MV_V_X<"vnmsac", 0b101111>;
defm VMADD_V : VMAC_MV_V_X<"vmadd", 0b101001>;
defm VNMSUB_V : VMAC_MV_V_X<"vnmsub", 0b101011>;

// Vector Widening Integer Multiply-Add Instructions
defm VWMACCU_V : VWMAC_MV_V_X<"vwmaccu", 0b111100>;
defm VWMACC_V : VWMAC_MV_V_X<"vwmacc", 0b111101>;
defm VWMACCSU_V : VWMAC_MV_V_X<"vwmaccsu", 0b111111>;
defm VWMACCUS_V : VWMAC_MV_X<"vwmaccus", 0b111110>;

// Vector Integer Merge Instructions
defm VMERGE_V : VMRG_IV_V_X_I<"vmerge", 0b010111>;

// Vector Integer Move Instructions
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, vs2 = 0, vm = 1,
    RVVConstraint = NoConstraint  in {
// op vd, vs1
def VMV_V_V : RVInstVV<0b010111, OPIVV, (outs VR:$vd),
                       (ins VR:$vs1), "vmv.v.v", "$vd, $vs1">,
              SchedUnaryMC<"WriteVIMovV", "ReadVIMovV", forceMasked=0>;
// op vd, rs1
def VMV_V_X : RVInstVX<0b010111, OPIVX, (outs VR:$vd),
                       (ins GPR:$rs1), "vmv.v.x", "$vd, $rs1">,
              SchedUnaryMC<"WriteVIMovX", "ReadVIMovX", forceMasked=0>;
// op vd, imm
def VMV_V_I : RVInstIVI<0b010111, (outs VR:$vd),
                       (ins simm5:$imm), "vmv.v.i", "$vd, $imm">,
              SchedNullaryMC<"WriteVIMovI", forceMasked=0>;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

// Vector Fixed-Point Arithmetic Instructions
defm VSADDU_V : VSALU_IV_V_X_I<"vsaddu", 0b100000>;
defm VSADD_V : VSALU_IV_V_X_I<"vsadd", 0b100001>;
defm VSSUBU_V : VSALU_IV_V_X<"vssubu", 0b100010>;
defm VSSUB_V : VSALU_IV_V_X<"vssub", 0b100011>;

// Vector Single-Width Averaging Add and Subtract
defm VAADDU_V : VAALU_MV_V_X<"vaaddu", 0b001000>;
defm VAADD_V : VAALU_MV_V_X<"vaadd", 0b001001>;
defm VASUBU_V : VAALU_MV_V_X<"vasubu", 0b001010>;
defm VASUB_V : VAALU_MV_V_X<"vasub", 0b001011>;

// Vector Single-Width Fractional Multiply with Rounding and Saturation
defm VSMUL_V : VSMUL_IV_V_X<"vsmul", 0b100111>;

// Vector Single-Width Scaling Shift Instructions
defm VSSRL_V : VSSHF_IV_V_X_I<"vssrl", 0b101010>;
defm VSSRA_V : VSSHF_IV_V_X_I<"vssra", 0b101011>;

// Vector Narrowing Fixed-Point Clip Instructions
let Constraints = "@earlyclobber $vd" in {
defm VNCLIPU_W : VNCLP_IV_V_X_I<"vnclipu", 0b101110>;
defm VNCLIP_W : VNCLP_IV_V_X_I<"vnclip", 0b101111>;
} // Constraints = "@earlyclobber $vd"
} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructionsAnyF] in {
// Vector Single-Width Floating-Point Add/Subtract Instructions
let Uses = [FRM], mayRaiseFPException = true in {
defm VFADD_V : VALU_FV_V_F<"vfadd", 0b000000>;
defm VFSUB_V : VALU_FV_V_F<"vfsub", 0b000010>;
defm VFRSUB_V : VALU_FV_F<"vfrsub", 0b100111>;
}

// Vector Widening Floating-Point Add/Subtract Instructions
let Constraints = "@earlyclobber $vd",
    Uses = [FRM],
    mayRaiseFPException = true in {
let RVVConstraint = WidenV in {
defm VFWADD_V : VWALU_FV_V_F<"vfwadd", 0b110000, "v">;
defm VFWSUB_V : VWALU_FV_V_F<"vfwsub", 0b110010, "v">;
} // RVVConstraint = WidenV
// Set earlyclobber for following instructions for second and mask operands.
// This has the downside that the earlyclobber constraint is too coarse and
// will impose unnecessary restrictions by not allowing the destination to
// overlap with the first (wide) operand.
let RVVConstraint = WidenW in {
defm VFWADD_W : VWALU_FV_V_F<"vfwadd", 0b110100, "w">;
defm VFWSUB_W : VWALU_FV_V_F<"vfwsub", 0b110110, "w">;
} // RVVConstraint = WidenW
} // Constraints = "@earlyclobber $vd", Uses = [FRM], mayRaiseFPException = true

// Vector Single-Width Floating-Point Multiply/Divide Instructions
let Uses = [FRM], mayRaiseFPException = true in {
defm VFMUL_V : VMUL_FV_V_F<"vfmul", 0b100100>;
defm VFDIV_V : VDIV_FV_V_F<"vfdiv", 0b100000>;
defm VFRDIV_V : VDIV_FV_F<"vfrdiv", 0b100001>;
}

// Vector Widening Floating-Point Multiply
let Constraints = "@earlyclobber $vd", RVVConstraint = WidenV,
    Uses = [FRM], mayRaiseFPException = true in {
defm VFWMUL_V : VWMUL_FV_V_F<"vfwmul", 0b111000>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = WidenV, Uses = [FRM], mayRaiseFPException = true

// Vector Single-Width Floating-Point Fused Multiply-Add Instructions
let Uses = [FRM], mayRaiseFPException = true in {
defm VFMACC_V : VMAC_FV_V_F<"vfmacc", 0b101100>;
defm VFNMACC_V : VMAC_FV_V_F<"vfnmacc", 0b101101>;
defm VFMSAC_V : VMAC_FV_V_F<"vfmsac", 0b101110>;
defm VFNMSAC_V : VMAC_FV_V_F<"vfnmsac", 0b101111>;
defm VFMADD_V : VMAC_FV_V_F<"vfmadd", 0b101000>;
defm VFNMADD_V : VMAC_FV_V_F<"vfnmadd", 0b101001>;
defm VFMSUB_V : VMAC_FV_V_F<"vfmsub", 0b101010>;
defm VFNMSUB_V : VMAC_FV_V_F<"vfnmsub", 0b101011>;
}

// Vector Widening Floating-Point Fused Multiply-Add Instructions
let Uses = [FRM], mayRaiseFPException = true in {
defm VFWMACC_V : VWMAC_FV_V_F<"vfwmacc", 0b111100>;
defm VFWNMACC_V : VWMAC_FV_V_F<"vfwnmacc", 0b111101>;
defm VFWMSAC_V : VWMAC_FV_V_F<"vfwmsac", 0b111110>;
defm VFWNMSAC_V : VWMAC_FV_V_F<"vfwnmsac", 0b111111>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = WidenV, Uses = [FRM], mayRaiseFPException = true

// Vector Floating-Point Square-Root Instruction
let Uses = [FRM], mayRaiseFPException = true in {
defm VFSQRT_V : VSQR_FV_VS2<"vfsqrt.v", 0b010011, 0b00000>;
defm VFREC7_V : VRCP_FV_VS2<"vfrec7.v", 0b010011, 0b00101>;
}

let mayRaiseFPException = true in
defm VFRSQRT7_V : VRCP_FV_VS2<"vfrsqrt7.v", 0b010011, 0b00100>;

// Vector Floating-Point MIN/MAX Instructions
let mayRaiseFPException = true in {
defm VFMIN_V : VMINMAX_FV_V_F<"vfmin", 0b000100>;
defm VFMAX_V : VMINMAX_FV_V_F<"vfmax", 0b000110>;
}

// Vector Floating-Point Sign-Injection Instructions
defm VFSGNJ_V : VSGNJ_FV_V_F<"vfsgnj", 0b001000>;
defm VFSGNJN_V : VSGNJ_FV_V_F<"vfsgnjn", 0b001001>;
defm VFSGNJX_V : VSGNJ_FV_V_F<"vfsgnjx", 0b001010>;

def : InstAlias<"vfneg.v $vd, $vs$vm",
                (VFSGNJN_VV VR:$vd, VR:$vs, VR:$vs, VMaskOp:$vm)>;
def : InstAlias<"vfneg.v $vd, $vs",
                (VFSGNJN_VV VR:$vd, VR:$vs, VR:$vs, zero_reg)>;
def : InstAlias<"vfabs.v $vd, $vs$vm",
                (VFSGNJX_VV VR:$vd, VR:$vs, VR:$vs, VMaskOp:$vm)>;
def : InstAlias<"vfabs.v $vd, $vs",
                (VFSGNJX_VV VR:$vd, VR:$vs, VR:$vs, zero_reg)>;

// Vector Floating-Point Compare Instructions
let RVVConstraint = NoConstraint, mayRaiseFPException = true in {
defm VMFEQ_V : VCMP_FV_V_F<"vmfeq", 0b011000>;
defm VMFNE_V : VCMP_FV_V_F<"vmfne", 0b011100>;
defm VMFLT_V : VCMP_FV_V_F<"vmflt", 0b011011>;
defm VMFLE_V : VCMP_FV_V_F<"vmfle", 0b011001>;
defm VMFGT_V : VCMP_FV_F<"vmfgt", 0b011101>;
defm VMFGE_V : VCMP_FV_F<"vmfge", 0b011111>;
} // RVVConstraint = NoConstraint, mayRaiseFPException = true

def : InstAlias<"vmfgt.vv $vd, $va, $vb$vm",
                (VMFLT_VV VR:$vd, VR:$vb, VR:$va, VMaskOp:$vm), 0>;
def : InstAlias<"vmfge.vv $vd, $va, $vb$vm",
                (VMFLE_VV VR:$vd, VR:$vb, VR:$va, VMaskOp:$vm), 0>;

// Vector Floating-Point Classify Instruction
defm VFCLASS_V : VCLS_FV_VS2<"vfclass.v", 0b010011, 0b10000>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {

// Vector Floating-Point Merge Instruction
let vm = 0 in
def VFMERGE_VFM : RVInstVX<0b010111, OPFVF, (outs VR:$vd),
                           (ins VR:$vs2, FPR32:$rs1, VMV0:$v0),
                           "vfmerge.vfm", "$vd, $vs2, $rs1, v0">,
                  SchedBinaryMC<"WriteVFMergeV", "ReadVFMergeV", "ReadVFMergeF">;

// Vector Floating-Point Move Instruction
let RVVConstraint = NoConstraint in
let vm = 1, vs2 = 0 in
def VFMV_V_F : RVInstVX<0b010111, OPFVF, (outs VR:$vd),
                       (ins FPR32:$rs1), "vfmv.v.f", "$vd, $rs1">,
               SchedUnaryMC<"WriteVFMovV", "ReadVFMovF", forceMasked=0>;

} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

// Single-Width Floating-Point/Integer Type-Convert Instructions
let mayRaiseFPException = true in {
let Uses = [FRM] in {
defm VFCVT_XU_F_V : VCVTI_FV_VS2<"vfcvt.xu.f.v", 0b010010, 0b00000>;
defm VFCVT_X_F_V : VCVTI_FV_VS2<"vfcvt.x.f.v", 0b010010, 0b00001>;
}
defm VFCVT_RTZ_XU_F_V : VCVTI_FV_VS2<"vfcvt.rtz.xu.f.v", 0b010010, 0b00110>;
defm VFCVT_RTZ_X_F_V : VCVTI_FV_VS2<"vfcvt.rtz.x.f.v", 0b010010, 0b00111>;
let Uses = [FRM] in {
defm VFCVT_F_XU_V : VCVTF_IV_VS2<"vfcvt.f.xu.v", 0b010010, 0b00010>;
defm VFCVT_F_X_V : VCVTF_IV_VS2<"vfcvt.f.x.v", 0b010010, 0b00011>;
}
} // mayRaiseFPException = true

// Widening Floating-Point/Integer Type-Convert Instructions
let Constraints = "@earlyclobber $vd", RVVConstraint = WidenCvt,
    mayRaiseFPException = true in {
let Uses = [FRM] in {
defm VFWCVT_XU_F_V : VWCVTI_FV_VS2<"vfwcvt.xu.f.v", 0b010010, 0b01000>;
defm VFWCVT_X_F_V : VWCVTI_FV_VS2<"vfwcvt.x.f.v", 0b010010, 0b01001>;
}
defm VFWCVT_RTZ_XU_F_V : VWCVTI_FV_VS2<"vfwcvt.rtz.xu.f.v", 0b010010, 0b01110>;
defm VFWCVT_RTZ_X_F_V : VWCVTI_FV_VS2<"vfwcvt.rtz.x.f.v", 0b010010, 0b01111>;
defm VFWCVT_F_XU_V : VWCVTF_IV_VS2<"vfwcvt.f.xu.v", 0b010010, 0b01010>;
defm VFWCVT_F_X_V : VWCVTF_IV_VS2<"vfwcvt.f.x.v", 0b010010, 0b01011>;
defm VFWCVT_F_F_V : VWCVTF_FV_VS2<"vfwcvt.f.f.v", 0b010010, 0b01100>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = WidenCvt

// Narrowing Floating-Point/Integer Type-Convert Instructions
let Constraints = "@earlyclobber $vd", mayRaiseFPException = true in {
let Uses = [FRM] in {
defm VFNCVT_XU_F_W : VNCVTI_FV_VS2<"vfncvt.xu.f.w", 0b010010, 0b10000>;
defm VFNCVT_X_F_W : VNCVTI_FV_VS2<"vfncvt.x.f.w", 0b010010, 0b10001>;
}
defm VFNCVT_RTZ_XU_F_W : VNCVTI_FV_VS2<"vfncvt.rtz.xu.f.w", 0b010010, 0b10110>;
defm VFNCVT_RTZ_X_F_W : VNCVTI_FV_VS2<"vfncvt.rtz.x.f.w", 0b010010, 0b10111>;
let Uses = [FRM] in {
defm VFNCVT_F_XU_W : VNCVTF_IV_VS2<"vfncvt.f.xu.w", 0b010010, 0b10010>;
defm VFNCVT_F_X_W : VNCVTF_IV_VS2<"vfncvt.f.x.w", 0b010010, 0b10011>;
defm VFNCVT_F_F_W : VNCVTF_FV_VS2<"vfncvt.f.f.w", 0b010010, 0b10100>;
}
defm VFNCVT_ROD_F_F_W : VNCVTF_FV_VS2<"vfncvt.rod.f.f.w", 0b010010, 0b10101>;
} // Constraints = "@earlyclobber $vd", mayRaiseFPException = true
} // Predicates = HasVInstructionsAnyF]

let Predicates = [HasVInstructions] in {

// Vector Single-Width Integer Reduction Instructions
let RVVConstraint = NoConstraint in {
defm VREDSUM  : VRED_MV_V<"vredsum", 0b000000>;
defm VREDMAXU : VREDMINMAX_MV_V<"vredmaxu", 0b000110>;
defm VREDMAX  : VREDMINMAX_MV_V<"vredmax", 0b000111>;
defm VREDMINU : VREDMINMAX_MV_V<"vredminu", 0b000100>;
defm VREDMIN  : VREDMINMAX_MV_V<"vredmin", 0b000101>;
defm VREDAND  : VRED_MV_V<"vredand", 0b000001>;
defm VREDOR   : VRED_MV_V<"vredor", 0b000010>;
defm VREDXOR  : VRED_MV_V<"vredxor", 0b000011>;
} // RVVConstraint = NoConstraint

// Vector Widening Integer Reduction Instructions
let Constraints = "@earlyclobber $vd", RVVConstraint = NoConstraint in {
// Set earlyclobber for following instructions for second and mask operands.
// This has the downside that the earlyclobber constraint is too coarse and
// will impose unnecessary restrictions by not allowing the destination to
// overlap with the first (wide) operand.
defm VWREDSUMU : VWRED_IV_V<"vwredsumu", 0b110000>;
defm VWREDSUM : VWRED_IV_V<"vwredsum", 0b110001>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = NoConstraint

} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructionsAnyF] in {
// Vector Single-Width Floating-Point Reduction Instructions
let RVVConstraint = NoConstraint in {
let Uses = [FRM], mayRaiseFPException = true in {
defm VFREDOSUM : VREDO_FV_V<"vfredosum", 0b000011>;
defm VFREDUSUM : VRED_FV_V<"vfredusum", 0b000001>;
}
let mayRaiseFPException = true in {
defm VFREDMAX : VREDMINMAX_FV_V<"vfredmax", 0b000111>;
defm VFREDMIN : VREDMINMAX_FV_V<"vfredmin", 0b000101>;
}
} // RVVConstraint = NoConstraint

def : InstAlias<"vfredsum.vs $vd, $vs2, $vs1$vm",
                (VFREDUSUM_VS VR:$vd, VR:$vs2, VR:$vs1, VMaskOp:$vm), 0>;

// Vector Widening Floating-Point Reduction Instructions
let Constraints = "@earlyclobber $vd", RVVConstraint = NoConstraint in {
// Set earlyclobber for following instructions for second and mask operands.
// This has the downside that the earlyclobber constraint is too coarse and
// will impose unnecessary restrictions by not allowing the destination to
// overlap with the first (wide) operand.
let Uses = [FRM], mayRaiseFPException = true in {
defm VFWREDOSUM : VWREDO_FV_V<"vfwredosum", 0b110011>;
defm VFWREDUSUM : VWRED_FV_V<"vfwredusum", 0b110001>;
}
} // Constraints = "@earlyclobber $vd", RVVConstraint = NoConstraint

def : InstAlias<"vfwredsum.vs $vd, $vs2, $vs1$vm",
                (VFWREDUSUM_VS VR:$vd, VR:$vs2, VR:$vs1, VMaskOp:$vm), 0>;
} // Predicates = [HasVInstructionsAnyF]

let Predicates = [HasVInstructions] in {
// Vector Mask-Register Logical Instructions
let RVVConstraint = NoConstraint in {
defm VMAND_M : VMALU_MV_Mask<"vmand", 0b011001, "m">;
defm VMNAND_M : VMALU_MV_Mask<"vmnand", 0b011101, "m">;
defm VMANDN_M : VMALU_MV_Mask<"vmandn", 0b011000, "m">;
defm VMXOR_M : VMALU_MV_Mask<"vmxor", 0b011011, "m">;
defm VMOR_M : VMALU_MV_Mask<"vmor", 0b011010, "m">;
defm VMNOR_M : VMALU_MV_Mask<"vmnor", 0b011110, "m">;
defm VMORN_M : VMALU_MV_Mask<"vmorn", 0b011100, "m">;
defm VMXNOR_M : VMALU_MV_Mask<"vmxnor", 0b011111, "m">;
}

def : InstAlias<"vmmv.m $vd, $vs",
                (VMAND_MM VR:$vd, VR:$vs, VR:$vs)>;
def : InstAlias<"vmclr.m $vd",
                (VMXOR_MM VR:$vd, VR:$vd, VR:$vd)>;
def : InstAlias<"vmset.m $vd",
                (VMXNOR_MM VR:$vd, VR:$vd, VR:$vd)>;
def : InstAlias<"vmnot.m $vd, $vs",
                (VMNAND_MM VR:$vd, VR:$vs, VR:$vs)>;

def : InstAlias<"vmandnot.mm $vd, $vs2, $vs1",
                (VMANDN_MM VR:$vd, VR:$vs2, VR:$vs1), 0>;
def : InstAlias<"vmornot.mm $vd, $vs2, $vs1",
                (VMORN_MM VR:$vd, VR:$vs2, VR:$vs1), 0>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0,
    RVVConstraint = NoConstraint  in {

// Vector mask population count vcpop
def VCPOP_M : RVInstV<0b010000, 0b10000, OPMVV, (outs GPR:$vd),
                      (ins VR:$vs2, VMaskOp:$vm),
                      "vcpop.m", "$vd, $vs2$vm">,
              SchedUnaryMC<"WriteVMPopV", "ReadVMPopV">;

// vfirst find-first-set mask bit
def VFIRST_M : RVInstV<0b010000, 0b10001, OPMVV, (outs GPR:$vd),
                       (ins VR:$vs2, VMaskOp:$vm),
                       "vfirst.m", "$vd, $vs2$vm">,
              SchedUnaryMC<"WriteVMFFSV", "ReadVMFFSV">;

} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

def : InstAlias<"vpopc.m $vd, $vs2$vm",
                (VCPOP_M GPR:$vd, VR:$vs2, VMaskOp:$vm), 0>;

let Constraints = "@earlyclobber $vd", RVVConstraint = Iota in {

// vmsbf.m set-before-first mask bit
defm VMSBF_M : VMSFS_MV_V<"vmsbf.m", 0b010100, 0b00001>;
// vmsif.m set-including-first mask bit
defm VMSIF_M : VMSFS_MV_V<"vmsif.m", 0b010100, 0b00011>;
// vmsof.m set-only-first mask bit
defm VMSOF_M : VMSFS_MV_V<"vmsof.m", 0b010100, 0b00010>;
// Vector Iota Instruction
defm VIOTA_M : VIOTA_MV_V<"viota.m", 0b010100, 0b10000>;

} // Constraints = "@earlyclobber $vd", RVVConstraint = Iota

// Vector Element Index Instruction
let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {

let vs2 = 0 in
def VID_V : RVInstV<0b010100, 0b10001, OPMVV, (outs VR:$vd),
                    (ins VMaskOp:$vm), "vid.v", "$vd$vm">,
            SchedNullaryMC<"WriteVIdxV">;

// Integer Scalar Move Instructions
let vm = 1, RVVConstraint = NoConstraint in {
def VMV_X_S : RVInstV<0b010000, 0b00000, OPMVV, (outs GPR:$vd),
                      (ins VR:$vs2), "vmv.x.s", "$vd, $vs2">,
              Sched<[WriteVMovXS, ReadVMovXS]>;
let Constraints = "$vd = $vd_wb" in
def VMV_S_X : RVInstV2<0b010000, 0b00000, OPMVX, (outs VR:$vd_wb),
                      (ins VR:$vd, GPR:$rs1), "vmv.s.x", "$vd, $rs1">,
              Sched<[WriteVMovSX, ReadVMovSX_V, ReadVMovSX_X]>;
}

} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructionsAnyF] in {

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, vm = 1,
    RVVConstraint = NoConstraint  in {
// Floating-Point Scalar Move Instructions
def VFMV_F_S : RVInstV<0b010000, 0b00000, OPFVV, (outs FPR32:$vd),
                      (ins VR:$vs2), "vfmv.f.s", "$vd, $vs2">,
               Sched<[WriteVMovFS, ReadVMovFS]>;
let Constraints = "$vd = $vd_wb" in
def VFMV_S_F : RVInstV2<0b010000, 0b00000, OPFVF, (outs VR:$vd_wb),
                       (ins VR:$vd, FPR32:$rs1), "vfmv.s.f", "$vd, $rs1">,
               Sched<[WriteVMovSF, ReadVMovSF_V, ReadVMovSF_F]>;

} // hasSideEffects = 0, mayLoad = 0, mayStore = 0, vm = 1

} // Predicates = [HasVInstructionsAnyF]

let Predicates = [HasVInstructions] in {
// Vector Slide Instructions
let Constraints = "@earlyclobber $vd", RVVConstraint = SlideUp in {
defm VSLIDEUP_V : VSLD_IV_X_I<"vslideup", 0b001110, /*slidesUp=*/true>;
defm VSLIDE1UP_V : VSLD1_MV_X<"vslide1up", 0b001110>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = SlideUp
defm VSLIDEDOWN_V : VSLD_IV_X_I<"vslidedown", 0b001111, /*slidesUp=*/false>;
defm VSLIDE1DOWN_V : VSLD1_MV_X<"vslide1down", 0b001111>;
} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructionsAnyF] in {
let Constraints = "@earlyclobber $vd", RVVConstraint = SlideUp in {
defm VFSLIDE1UP_V : VSLD1_FV_F<"vfslide1up", 0b001110>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = SlideUp
defm VFSLIDE1DOWN_V : VSLD1_FV_F<"vfslide1down", 0b001111>;
} // Predicates = [HasVInstructionsAnyF]

let Predicates = [HasVInstructions] in {
// Vector Register Gather Instruction
let Constraints = "@earlyclobber $vd", RVVConstraint = Vrgather in {
defm VRGATHER_V : VGTR_IV_V_X_I<"vrgather", 0b001100>;
def VRGATHEREI16_VV : VALUVV<0b001110, OPIVV, "vrgatherei16.vv">,
                      SchedBinaryMC<"WriteVRGatherEI16VV",
                                    "ReadVRGatherEI16VV_data",
                                    "ReadVRGatherEI16VV_index">;
} // Constraints = "@earlyclobber $vd", RVVConstraint = Vrgather

// Vector Compress Instruction
let Constraints = "@earlyclobber $vd", RVVConstraint = Vcompress in {
defm VCOMPRESS_V : VCPR_MV_Mask<"vcompress", 0b010111>;
} // Constraints = "@earlyclobber $vd", RVVConstraint = Vcompress

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isMoveReg = 1,
    RVVConstraint = NoConstraint in {
// A future extension may relax the vector register alignment restrictions.
foreach n = [1, 2, 4, 8] in {
  defvar vrc = !cast<VReg>(!if(!eq(n, 1), "VR", "VRM"#n));
  def VMV#n#R_V  : RVInstV<0b100111, !add(n, -1), OPIVI, (outs vrc:$vd),
                           (ins vrc:$vs2), "vmv" # n # "r.v", "$vd, $vs2">,
                   VMVRSched<n> {
    let Uses = [];
    let vm = 1;
  }
}
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0
} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructions] in {
  foreach nf=2-8 in {
    foreach eew = [8, 16, 32] in {
      defvar w = !cast<RISCVWidth>("LSWidth"#eew);

      def VLSEG#nf#E#eew#_V :
        VUnitStrideSegmentLoad<!add(nf, -1), w, "vlseg"#nf#"e"#eew#".v">,
        VLSEGSchedMC<nf, eew>;
      def VLSEG#nf#E#eew#FF_V :
        VUnitStrideSegmentLoadFF<!add(nf, -1), w, "vlseg"#nf#"e"#eew#"ff.v">,
        VLSEGFFSchedMC<nf, eew>;
      def VSSEG#nf#E#eew#_V :
        VUnitStrideSegmentStore<!add(nf, -1), w, "vsseg"#nf#"e"#eew#".v">,
        VSSEGSchedMC<nf, eew>;
      // Vector Strided Instructions
      def VLSSEG#nf#E#eew#_V :
        VStridedSegmentLoad<!add(nf, -1), w, "vlsseg"#nf#"e"#eew#".v">,
        VLSSEGSchedMC<nf, eew>;
      def VSSSEG#nf#E#eew#_V :
        VStridedSegmentStore<!add(nf, -1), w, "vssseg"#nf#"e"#eew#".v">,
        VSSSEGSchedMC<nf, eew>;

      // Vector Indexed Instructions
      def VLUXSEG#nf#EI#eew#_V :
        VIndexedSegmentLoad<!add(nf, -1), MOPLDIndexedUnord, w,
                            "vluxseg"#nf#"ei"#eew#".v">,
        VLXSEGSchedMC<nf, eew, isOrdered=0>;
      def VLOXSEG#nf#EI#eew#_V :
        VIndexedSegmentLoad<!add(nf, -1), MOPLDIndexedOrder, w,
                            "vloxseg"#nf#"ei"#eew#".v">,
        VLXSEGSchedMC<nf, eew, isOrdered=1>;
      def VSUXSEG#nf#EI#eew#_V :
        VIndexedSegmentStore<!add(nf, -1), MOPSTIndexedUnord, w,
                             "vsuxseg"#nf#"ei"#eew#".v">,
        VSXSEGSchedMC<nf, eew, isOrdered=0>;
      def VSOXSEG#nf#EI#eew#_V :
        VIndexedSegmentStore<!add(nf, -1), MOPSTIndexedOrder, w,
                             "vsoxseg"#nf#"ei"#eew#".v">,
        VSXSEGSchedMC<nf, eew, isOrdered=1>;
    }
  }
} // Predicates = [HasVInstructions]

let Predicates = [HasVInstructionsI64] in {
  foreach nf=2-8 in {
    // Vector Unit-strided Segment Instructions
    def VLSEG#nf#E64_V :
      VUnitStrideSegmentLoad<!add(nf, -1), LSWidth64, "vlseg"#nf#"e64.v">,
      VLSEGSchedMC<nf, 64>;
    def VLSEG#nf#E64FF_V :
      VUnitStrideSegmentLoadFF<!add(nf, -1), LSWidth64, "vlseg"#nf#"e64ff.v">,
      VLSEGFFSchedMC<nf, 64>;
    def VSSEG#nf#E64_V :
      VUnitStrideSegmentStore<!add(nf, -1), LSWidth64, "vsseg"#nf#"e64.v">,
      VSSEGSchedMC<nf, 64>;

    // Vector Strided Segment Instructions
    def VLSSEG#nf#E64_V :
      VStridedSegmentLoad<!add(nf, -1), LSWidth64, "vlsseg"#nf#"e64.v">,
      VLSSEGSchedMC<nf, 64>;
    def VSSSEG#nf#E64_V :
      VStridedSegmentStore<!add(nf, -1), LSWidth64, "vssseg"#nf#"e64.v">,
      VSSSEGSchedMC<nf, 64>;
  }
} // Predicates = [HasVInstructionsI64]
let Predicates = [HasVInstructionsI64, IsRV64] in {
  foreach nf = 2 - 8 in {
    // Vector Indexed Segment Instructions
    def VLUXSEG #nf #EI64_V
        : VIndexedSegmentLoad<!add(nf, -1), MOPLDIndexedUnord, LSWidth64,
                              "vluxseg" #nf #"ei64.v">,
          VLXSEGSchedMC<nf, 64, isOrdered=0>;
    def VLOXSEG #nf #EI64_V
        : VIndexedSegmentLoad<!add(nf, -1), MOPLDIndexedOrder, LSWidth64,
                              "vloxseg" #nf #"ei64.v">,
          VLXSEGSchedMC<nf, 64, isOrdered=1>;
    def VSUXSEG #nf #EI64_V
        : VIndexedSegmentStore<!add(nf, -1), MOPSTIndexedUnord, LSWidth64,
                               "vsuxseg" #nf #"ei64.v">,
          VSXSEGSchedMC<nf, 64, isOrdered=0>;
    def VSOXSEG #nf #EI64_V
        : VIndexedSegmentStore<!add(nf, -1), MOPSTIndexedOrder, LSWidth64,
                               "vsoxseg" #nf #"ei64.v">,
          VSXSEGSchedMC<nf, 64, isOrdered=1>;
  }
} // Predicates = [HasVInstructionsI64, IsRV64]

include "RISCVInstrInfoZvfbf.td"
include "RISCVInstrInfoVPseudos.td"
