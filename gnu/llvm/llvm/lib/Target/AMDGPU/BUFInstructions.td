//===-- BUFInstructions.td - Buffer Instruction Definitions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def MUBUFAddr64 : ComplexPattern<iPTR, 4, "SelectMUBUFAddr64">;
def MUBUFOffset : ComplexPattern<iPTR, 3, "SelectMUBUFOffset">;

def MUBUFScratchOffen : ComplexPattern<iPTR, 4, "SelectMUBUFScratchOffen", [], [SDNPWantParent]>;
def MUBUFScratchOffset : ComplexPattern<iPTR, 3, "SelectMUBUFScratchOffset", [], [SDNPWantParent], 20>;

def BUFSOffset   : ComplexPattern<iPTR, 1, "SelectBUFSOffset">;

def BUFAddrKind {
  int Offset = 0;
  int OffEn  = 1;
  int IdxEn  = 2;
  int BothEn = 3;
  int Addr64 = 4;
}

class getAddrName<int addrKind> {
  string ret =
    !if(!eq(addrKind, BUFAddrKind.Offset), "offset",
    !if(!eq(addrKind, BUFAddrKind.OffEn),  "offen",
    !if(!eq(addrKind, BUFAddrKind.IdxEn),  "idxen",
    !if(!eq(addrKind, BUFAddrKind.BothEn), "bothen",
    !if(!eq(addrKind, BUFAddrKind.Addr64), "addr64",
    "")))));
}

class MUBUFAddr64Table <bit is_addr64, string Name> {
  bit IsAddr64 = is_addr64;
  string OpName = Name;
}

class MTBUFAddr64Table <bit is_addr64, string Name> {
  bit IsAddr64 = is_addr64;
  string OpName = Name;
}


//===----------------------------------------------------------------------===//
// BUF class (base class for MTBUF and MUBUF pseudos)
//===----------------------------------------------------------------------===//

class BUF_Pseudo <string opName, dag outs, dag ins,
                    string asmOps, list<dag> pattern=[]> :
  InstSI<outs, ins, "", pattern>,
  SIMCInstr<opName, SIEncodingFamily.NONE> {

  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let Size = 8;
  let UseNamedOperandTable = 1;

  string Mnemonic = opName;
  string AsmOperands = asmOps;

  Instruction Opcode = !cast<Instruction>(NAME);


  let VM_CNT = 1;
  let EXP_CNT = 1;

  let Uses = [EXEC];
  let hasSideEffects = 0;
  let SchedRW = [WriteVMEM];



  bits<1> offen       = 0;
  bits<1> idxen       = 0;
  bits<1> addr64      = 0;
  bits<1> lds         = 0;
  bits<1> has_vdata   = !not(lds);
  bits<1> has_vaddr   = 1;
  bits<1> has_glc     = 1;
  bits<1> has_dlc     = 1;
  bits<1> glc_value   = 0; // the value for glc if no such operand
  bits<1> dlc_value   = 0; // the value for dlc if no such operand
  bits<1> has_srsrc   = 1;
  bits<1> has_soffset = 1;
  bits<1> has_offset  = 1;
  bits<1> has_slc     = 1;
  bits<1> tfe         = 0;
  bits<4> elements    = 0;
  bits<1> has_sccb    = 1;
  bits<1> sccb_value  = 0;
  bits<1> IsBufferInv = 0;
}



//===----------------------------------------------------------------------===//
// MTBUF classes
//===----------------------------------------------------------------------===//

class MTBUFGetBaseOpcode<string Op> {
  string ret = !subst("FORMAT_XY", "FORMAT_X",
    !subst("FORMAT_XYZ", "FORMAT_X",
    !subst("FORMAT_XYZW", "FORMAT_X", Op)));
}


class MTBUF_Pseudo <string opName, dag outs, dag ins,
                    string asmOps, list<dag> pattern=[]> :
  BUF_Pseudo <opName, outs, ins, asmOps, pattern> {

  Instruction BaseOpcode = !cast<Instruction>(MTBUFGetBaseOpcode<NAME>.ret);
  let MTBUF = 1;
}

class MTBUF_Real <MTBUF_Pseudo ps, string real_name = ps.Mnemonic> :
  InstSI <ps.OutOperandList, ps.InOperandList, real_name # ps.AsmOperands, []> {

  let isPseudo = 0;
  let isCodeGenOnly = 0;

  let VM_CNT = 1;
  let EXP_CNT = 1;
  let MTBUF = 1;

  // copy relevant pseudo op flags
  let UseNamedOperandTable = ps.UseNamedOperandTable;
  let SubtargetPredicate = ps.SubtargetPredicate;
  let OtherPredicates    = ps.OtherPredicates;
  let AsmMatchConverter  = ps.AsmMatchConverter;
  let Constraints        = ps.Constraints;
  let DisableEncoding    = ps.DisableEncoding;
  let TSFlags            = ps.TSFlags;
  let SchedRW            = ps.SchedRW;
  let mayLoad            = ps.mayLoad;
  let mayStore           = ps.mayStore;
  let IsAtomicRet        = ps.IsAtomicRet;
  let IsAtomicNoRet      = ps.IsAtomicNoRet;
  let Uses               = ps.Uses;
  let Defs               = ps.Defs;

  bits<12> offset;
  bits<5>  cpol;
  bits<7>  format;
  bits<8>  vaddr;
  bits<10> vdata;
  bits<7>  srsrc;
  bits<8>  soffset;

  bits<4> dfmt = format{3-0};
  bits<3> nfmt = format{6-4};

  // GFX90A+ only: instruction uses AccVGPR for data
  // Bit supersedes tfe.
  bits<1> acc = !if(ps.has_vdata, vdata{9}, 0);
}

class getMTBUFInsDA<list<RegisterClass> vdataList,
                    list<RegisterClass> vaddrList=[], bit hasRestrictedSOffset> {
  RegisterClass vdataClass = !if(!empty(vdataList), ?, !head(vdataList));
  RegisterClass vaddrClass = !if(!empty(vaddrList), ?, !head(vaddrList));
  RegisterOperand vdata_op = getLdStRegisterOperand<vdataClass>.ret;

  dag SOffset = !if(hasRestrictedSOffset, (ins SReg_32:$soffset),
                                 (ins SCSrc_b32:$soffset));

  dag NonVaddrInputs = !con((ins SReg_128:$srsrc), SOffset,
                            (ins Offset:$offset, FORMAT:$format, CPol_0:$cpol, i1imm_0:$swz));

  dag Inputs = !if(!empty(vaddrList),
                   NonVaddrInputs,
                   !con((ins vaddrClass:$vaddr), NonVaddrInputs));
  dag ret = !if(!empty(vdataList),
                Inputs,
                !con((ins vdata_op:$vdata), Inputs));
}

class getMTBUFIns<int addrKind, list<RegisterClass> vdataList=[], bit hasRestrictedSOffset> {
  dag ret =
    !if(!eq(addrKind, BUFAddrKind.Offset), getMTBUFInsDA<vdataList, [], hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.OffEn),  getMTBUFInsDA<vdataList, [VGPR_32], hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.IdxEn),  getMTBUFInsDA<vdataList, [VGPR_32], hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.BothEn), getMTBUFInsDA<vdataList, [VReg_64], hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.Addr64), getMTBUFInsDA<vdataList, [VReg_64], hasRestrictedSOffset>.ret,
    (ins))))));
}

class getMTBUFAsmOps<int addrKind> {
  string Pfx =
    !if(!eq(addrKind, BUFAddrKind.Offset), "off, $srsrc,$format $soffset",
    !if(!eq(addrKind, BUFAddrKind.OffEn),
            "$vaddr, $srsrc,$format $soffset offen",
    !if(!eq(addrKind, BUFAddrKind.IdxEn),
            "$vaddr, $srsrc,$format $soffset idxen",
    !if(!eq(addrKind, BUFAddrKind.BothEn),
            "$vaddr, $srsrc,$format $soffset idxen offen",
    !if(!eq(addrKind, BUFAddrKind.Addr64),
            "$vaddr, $srsrc,$format $soffset addr64",
    "")))));
  string ret = " $vdata, " # Pfx # "$offset$cpol";
}

class MTBUF_SetupAddr<int addrKind> {
  bits<1> offen  = !or(!eq(addrKind, BUFAddrKind.OffEn),
                       !eq(addrKind, BUFAddrKind.BothEn));

  bits<1> idxen  = !or(!eq(addrKind, BUFAddrKind.IdxEn),
                       !eq(addrKind, BUFAddrKind.BothEn));

  bits<1> addr64 = !eq(addrKind, BUFAddrKind.Addr64);

  bits<1> has_vaddr = !ne(addrKind, BUFAddrKind.Offset);
}

class MTBUF_Load_Pseudo <string opName,
                         int addrKind,
                         RegisterClass vdataClass,
                         int elems,
                         bit hasRestrictedSOffset = 0,
                         list<dag> pattern=[],
                         // Workaround bug bz30254
                         int addrKindCopy = addrKind>
  : MTBUF_Pseudo<opName,
                 (outs getLdStRegisterOperand<vdataClass>.ret:$vdata),
                 getMTBUFIns<addrKindCopy, [], hasRestrictedSOffset>.ret,
                 getMTBUFAsmOps<addrKindCopy>.ret,
                 pattern>,
    MTBUF_SetupAddr<addrKindCopy> {
  let PseudoInstr = opName # "_" # getAddrName<addrKindCopy>.ret;
  let mayLoad = 1;
  let mayStore = 0;
  let elements = elems;
}

multiclass MTBUF_Pseudo_Loads_Helper<string opName, RegisterClass vdataClass,
                              int elems, bit hasRestrictedSOffset> {

  def _OFFSET : MTBUF_Load_Pseudo <opName, BUFAddrKind.Offset, vdataClass, elems, hasRestrictedSOffset>,
                MTBUFAddr64Table<0, NAME>;

  def _ADDR64 : MTBUF_Load_Pseudo <opName, BUFAddrKind.Addr64, vdataClass, elems, hasRestrictedSOffset>,
                MTBUFAddr64Table<1, NAME>;

  def _OFFEN  : MTBUF_Load_Pseudo <opName, BUFAddrKind.OffEn, vdataClass, elems, hasRestrictedSOffset>;
  def _IDXEN  : MTBUF_Load_Pseudo <opName, BUFAddrKind.IdxEn, vdataClass, elems, hasRestrictedSOffset>;
  def _BOTHEN : MTBUF_Load_Pseudo <opName, BUFAddrKind.BothEn, vdataClass, elems, hasRestrictedSOffset>;

  let DisableWQM = 1 in {
    def _OFFSET_exact : MTBUF_Load_Pseudo <opName, BUFAddrKind.Offset, vdataClass, elems, hasRestrictedSOffset>;
    def _OFFEN_exact  : MTBUF_Load_Pseudo <opName, BUFAddrKind.OffEn, vdataClass, elems, hasRestrictedSOffset>;
    def _IDXEN_exact  : MTBUF_Load_Pseudo <opName, BUFAddrKind.IdxEn, vdataClass, elems, hasRestrictedSOffset>;
    def _BOTHEN_exact : MTBUF_Load_Pseudo <opName, BUFAddrKind.BothEn, vdataClass, elems, hasRestrictedSOffset>;
  }
}

multiclass MTBUF_Pseudo_Loads<string opName, RegisterClass vdataClass,
                              int elems> {
  defm NAME : MTBUF_Pseudo_Loads_Helper<opName, vdataClass, elems, 0>;
  defm _VBUFFER : MTBUF_Pseudo_Loads_Helper<opName, vdataClass, elems, 1>;
}

class MTBUF_Store_Pseudo <string opName,
                          int addrKind,
                          RegisterClass vdataClass,
                          int elems,
                          bit hasRestrictedSOffset = 0,
                          list<dag> pattern=[],
                          // Workaround bug bz30254
                          int addrKindCopy = addrKind,
                          RegisterClass vdataClassCopy = vdataClass>
  : MTBUF_Pseudo<opName,
                 (outs),
                 getMTBUFIns<addrKindCopy, [vdataClassCopy], hasRestrictedSOffset>.ret,
                 getMTBUFAsmOps<addrKindCopy>.ret,
                 pattern>,
    MTBUF_SetupAddr<addrKindCopy> {
  let PseudoInstr = opName # "_" # getAddrName<addrKindCopy>.ret;
  let mayLoad = 0;
  let mayStore = 1;
  let elements = elems;
}

multiclass MTBUF_Pseudo_Stores_Helper<string opName, RegisterClass vdataClass,
                               int elems, bit hasRestrictedSOffset> {

  def _OFFSET : MTBUF_Store_Pseudo <opName, BUFAddrKind.Offset, vdataClass, elems, hasRestrictedSOffset>,
    MTBUFAddr64Table<0, NAME>;

  def _ADDR64 : MTBUF_Store_Pseudo <opName, BUFAddrKind.Addr64, vdataClass, elems, hasRestrictedSOffset>,
    MTBUFAddr64Table<1, NAME>;

  def _OFFEN  : MTBUF_Store_Pseudo <opName, BUFAddrKind.OffEn, vdataClass, elems, hasRestrictedSOffset>;
  def _IDXEN  : MTBUF_Store_Pseudo <opName, BUFAddrKind.IdxEn, vdataClass, elems, hasRestrictedSOffset>;
  def _BOTHEN : MTBUF_Store_Pseudo <opName, BUFAddrKind.BothEn, vdataClass, elems, hasRestrictedSOffset>;

  let DisableWQM = 1 in {
    def _OFFSET_exact : MTBUF_Store_Pseudo <opName, BUFAddrKind.Offset, vdataClass, elems, hasRestrictedSOffset>;
    def _OFFEN_exact  : MTBUF_Store_Pseudo <opName, BUFAddrKind.OffEn, vdataClass, elems, hasRestrictedSOffset>;
    def _IDXEN_exact  : MTBUF_Store_Pseudo <opName, BUFAddrKind.IdxEn, vdataClass, elems, hasRestrictedSOffset>;
    def _BOTHEN_exact : MTBUF_Store_Pseudo <opName, BUFAddrKind.BothEn, vdataClass, elems, hasRestrictedSOffset>;
  }
}

multiclass MTBUF_Pseudo_Stores<string opName, RegisterClass vdataClass,
                               int elems> {
  defm NAME : MTBUF_Pseudo_Stores_Helper<opName, vdataClass, elems, 0>;
  defm _VBUFFER : MTBUF_Pseudo_Stores_Helper<opName, vdataClass, elems, 1>;
}

//===----------------------------------------------------------------------===//
// MUBUF classes
//===----------------------------------------------------------------------===//

class MUBUFGetBaseOpcode<string Op> {
  string ret = !subst("DWORDX2", "DWORD",
    !subst("DWORDX3", "DWORD",
    !subst("DWORDX4", "DWORD", Op)));
}

class MUBUF_Pseudo <string opName, dag outs, dag ins,
                    string asmOps, list<dag> pattern=[]> :
  BUF_Pseudo <opName, outs, ins, asmOps, pattern> {

  Instruction BaseOpcode = !cast<Instruction>(MUBUFGetBaseOpcode<NAME>.ret);
  let MUBUF = 1;
  let AsmMatchConverter = "cvtMubuf";
  let usesCustomInserter = 1;
}

class MUBUF_Real <MUBUF_Pseudo ps, string real_name = ps.Mnemonic> :
  InstSI <ps.OutOperandList, ps.InOperandList, real_name # ps.AsmOperands, []> {

  let isPseudo = 0;
  let isCodeGenOnly = 0;

  let VM_CNT = 1;
  let EXP_CNT = 1;
  let MUBUF = 1;

  // copy relevant pseudo op flags
  let SubtargetPredicate   = ps.SubtargetPredicate;
  let AsmMatchConverter    = ps.AsmMatchConverter;
  let OtherPredicates      = ps.OtherPredicates;
  let Constraints          = ps.Constraints;
  let DisableEncoding      = ps.DisableEncoding;
  let TSFlags              = ps.TSFlags;
  let UseNamedOperandTable = ps.UseNamedOperandTable;
  let SchedRW              = ps.SchedRW;
  let mayLoad              = ps.mayLoad;
  let mayStore             = ps.mayStore;
  let IsAtomicRet          = ps.IsAtomicRet;
  let IsAtomicNoRet        = ps.IsAtomicNoRet;
  let VALU                 = ps.VALU;
  let LGKM_CNT             = ps.LGKM_CNT;
  let Uses                 = ps.Uses;
  let Defs                 = ps.Defs;

  bits<12> offset;
  bits<5>  cpol;
  bits<8>  vaddr;
  bits<10> vdata;
  bits<7>  srsrc;
  bits<8>  soffset;

  // GFX90A+ only: instruction uses AccVGPR for data
  // Bit supersedes tfe.
  bits<1> acc = !if(ps.has_vdata, vdata{9}, 0);
}

// For cache invalidation instructions.
class MUBUF_Invalidate <string opName, SDPatternOperator node = null_frag> :
  MUBUF_Pseudo<opName, (outs), (ins), "", [(node)]> {

  let AsmMatchConverter = "";

  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;

  let IsBufferInv = 1;
  // Set everything else to 0.
  let offen       = 0;
  let idxen       = 0;
  let addr64      = 0;
  let has_vdata   = 0;
  let has_vaddr   = 0;
  let has_glc     = 0;
  let has_dlc     = 0;
  let glc_value   = 0;
  let dlc_value   = 0;
  let has_srsrc   = 0;
  let has_soffset = 0;
  let has_offset  = 0;
  let has_slc     = 0;
  let has_sccb    = 0;
  let sccb_value  = 0;
}

class getLdStVDataRegisterOperand<RegisterClass RC, bit isTFE> {
  RegisterOperand tfeVDataOp =
    !cond(!eq(RC.Size, 32)  : AVLdSt_64,
          !eq(RC.Size, 64)  : AVLdSt_96,
          !eq(RC.Size, 96)  : AVLdSt_128,
          !eq(RC.Size, 128) : AVLdSt_160);

  RegisterOperand ret = !if(isTFE, tfeVDataOp, getLdStRegisterOperand<RC>.ret);
}

class getMUBUFInsDA<list<RegisterClass> vdataList,
                    list<RegisterClass> vaddrList, bit isTFE, bit hasRestrictedSOffset> {
  RegisterClass vdataClass = !if(!empty(vdataList), ?, !head(vdataList));
  RegisterClass vaddrClass = !if(!empty(vaddrList), ?, !head(vaddrList));
  RegisterOperand vdata_op = getLdStVDataRegisterOperand<vdataClass, isTFE>.ret;

  dag SOffset = !if(hasRestrictedSOffset, (ins SReg_32:$soffset), (ins SCSrc_b32:$soffset));
  dag NonVaddrInputs = !con((ins SReg_128:$srsrc), SOffset, (ins Offset:$offset, CPol_0:$cpol, i1imm_0:$swz));

  dag Inputs = !if(!empty(vaddrList), NonVaddrInputs, !con((ins vaddrClass:$vaddr), NonVaddrInputs));
  dag ret = !if(!empty(vdataList), Inputs, !con((ins vdata_op:$vdata), Inputs));
}

class getMUBUFElements<ValueType vt> {
  int ret =
    !if(!eq(vt, f16), 1,
      !if(!eq(vt, v2f16), 2,
        !if(!eq(vt, v3f16), 3,
          !if(!eq(vt, v4f16), 4,
            !if(!eq(vt.Size, 32), 1,
              !if(!eq(vt.Size, 64), 2,
                !if(!eq(vt.Size, 96), 3,
                  !if(!eq(vt.Size, 128), 4, 0)
                )
              )
            )
          )
        )
      )
    );
}

class getMUBUFIns<int addrKind, list<RegisterClass> vdataList, bit isTFE, bit hasRestrictedSOffset> {
  dag ret =
    !if(!eq(addrKind, BUFAddrKind.Offset), getMUBUFInsDA<vdataList, [], isTFE, hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.OffEn),  getMUBUFInsDA<vdataList, [VGPR_32], isTFE, hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.IdxEn),  getMUBUFInsDA<vdataList, [VGPR_32], isTFE, hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.BothEn), getMUBUFInsDA<vdataList, [VReg_64], isTFE, hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.Addr64), getMUBUFInsDA<vdataList, [VReg_64], isTFE, hasRestrictedSOffset>.ret,
    (ins))))));
}

class getMUBUFAsmOps<int addrKind, bit noVdata = 0, bit isLds = 0, bit isTFE = 0> {
  string Vdata = !if(noVdata, " ", " $vdata, ");
  string Lds = !if(isLds, " lds", "");
  string TFE = !if(isTFE, " tfe", "");
  string MainArgs =
    !if(!eq(addrKind, BUFAddrKind.Offset), "off, $srsrc, $soffset",
    !if(!eq(addrKind, BUFAddrKind.OffEn),  "$vaddr, $srsrc, $soffset offen",
    !if(!eq(addrKind, BUFAddrKind.IdxEn),  "$vaddr, $srsrc, $soffset idxen",
    !if(!eq(addrKind, BUFAddrKind.BothEn), "$vaddr, $srsrc, $soffset idxen offen",
    !if(!eq(addrKind, BUFAddrKind.Addr64), "$vaddr, $srsrc, $soffset addr64",
    "")))));
  string Offset = "$offset";
  string OtherArgs = "$cpol";

  string ret = Vdata # MainArgs # Offset # OtherArgs # Lds # TFE;
}

class MUBUF_SetupAddr<int addrKind> {
  bits<1> offen  = !or(!eq(addrKind, BUFAddrKind.OffEn),
                       !eq(addrKind, BUFAddrKind.BothEn));

  bits<1> idxen  = !or(!eq(addrKind, BUFAddrKind.IdxEn),
                       !eq(addrKind, BUFAddrKind.BothEn));

  bits<1> addr64 = !eq(addrKind, BUFAddrKind.Addr64);

  bits<1> has_vaddr = !ne(addrKind, BUFAddrKind.Offset);
}

class MUBUF_Load_Pseudo <string opName,
                         int addrKind,
                         ValueType vdata_vt,
                         bit HasTiedDest = 0,
                         bit isLds = 0,
                         bit isLdsOpc = 0,
                         bit isTFE = 0,
                         bit hasRestrictedSOffset = 0,
                         list<dag> pattern=[],
                         // Workaround bug bz30254
                         int addrKindCopy = addrKind,
                         RegisterClass vdata_rc = getVregSrcForVT<vdata_vt>.ret.RegClass,
                         RegisterOperand vdata_op = getLdStVDataRegisterOperand<vdata_rc, isTFE>.ret>
  : MUBUF_Pseudo<opName,
                 !if(!or(isLds, isLdsOpc), (outs), (outs vdata_op:$vdata)),
                 !con(getMUBUFIns<addrKindCopy, [], isTFE, hasRestrictedSOffset>.ret,
                      !if(HasTiedDest, (ins vdata_op:$vdata_in), (ins))),
                 getMUBUFAsmOps<addrKindCopy, !or(isLds, isLdsOpc), isLds, isTFE>.ret,
                 pattern>,
    MUBUF_SetupAddr<addrKindCopy> {
  let PseudoInstr = opName # !if(isLds, "_lds", "") # !if(isTFE, "_tfe", "") #
                    "_" # getAddrName<addrKindCopy>.ret;
  let AsmMatchConverter = "cvtMubuf";

  let Constraints = !if(HasTiedDest, "$vdata = $vdata_in", "");
  let LGKM_CNT = isLds;
  let has_vdata = !not(!or(isLds, isLdsOpc));
  let mayLoad = 1;
  let mayStore = isLds;
  let Uses = !if(!or(isLds, isLdsOpc) , [EXEC, M0], [EXEC]);
  let tfe = isTFE;
  let lds = isLds;
  let elements = getMUBUFElements<vdata_vt>.ret;
  let VALU = isLds;
}

class MUBUF_Offset_Load_Pat <Instruction inst, ValueType load_vt = i32, SDPatternOperator ld = null_frag> : GCNPat <
  (load_vt (ld (MUBUFOffset v4i32:$srsrc, i32:$soffset, i32:$offset))),
  (load_vt (inst v4i32:$srsrc, i32:$soffset, i32:$offset))
>;

class MUBUF_Addr64_Load_Pat <Instruction inst,
                            ValueType load_vt = i32,
                            SDPatternOperator ld = null_frag> : GCNPat <
  (load_vt (ld (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset, i32:$offset))),
  (load_vt (inst i64:$vaddr, v4i32:$srsrc, i32:$soffset, i32:$offset))
>;

multiclass MUBUF_Pseudo_Load_Pats_Common<string BaseInst, ValueType load_vt = i32, SDPatternOperator ld = null_frag> {
  def : MUBUF_Offset_Load_Pat<!cast<Instruction>(BaseInst#"_OFFSET"), load_vt, ld>;
  def : MUBUF_Addr64_Load_Pat<!cast<Instruction>(BaseInst#"_ADDR64"), load_vt, ld>;
}

multiclass MUBUF_Pseudo_Load_Pats<string BaseInst, ValueType load_vt = i32, SDPatternOperator ld = null_frag>{
  let OtherPredicates = [HasUnrestrictedSOffset] in {
    defm : MUBUF_Pseudo_Load_Pats_Common<BaseInst, load_vt, ld>;
  }
  defm : MUBUF_Pseudo_Load_Pats_Common<BaseInst # "_VBUFFER", load_vt, ld>;
}

multiclass MUBUF_Pseudo_Loads_Helper<string opName, ValueType load_vt,
                                     bit TiedDest, bit isLds, bit isTFE, bit hasRestrictedSOffset> {
  defvar legal_load_vt = !if(!eq(load_vt, v3f16), v4f16, load_vt);

  def _OFFSET : MUBUF_Load_Pseudo <opName, BUFAddrKind.Offset, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>,
    MUBUFAddr64Table<0, NAME # !if(isLds, "_LDS", "")>;

  def _ADDR64 : MUBUF_Load_Pseudo <opName, BUFAddrKind.Addr64, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>,
    MUBUFAddr64Table<1, NAME # !if(isLds, "_LDS", "")>;

  def _OFFEN  : MUBUF_Load_Pseudo <opName, BUFAddrKind.OffEn, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>;
  def _IDXEN  : MUBUF_Load_Pseudo <opName, BUFAddrKind.IdxEn, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>;
  def _BOTHEN : MUBUF_Load_Pseudo <opName, BUFAddrKind.BothEn, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>;

  let DisableWQM = 1 in {
    def _OFFSET_exact : MUBUF_Load_Pseudo <opName, BUFAddrKind.Offset, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>;
    def _OFFEN_exact  : MUBUF_Load_Pseudo <opName, BUFAddrKind.OffEn, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>;
    def _IDXEN_exact  : MUBUF_Load_Pseudo <opName, BUFAddrKind.IdxEn, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>;
    def _BOTHEN_exact : MUBUF_Load_Pseudo <opName, BUFAddrKind.BothEn, legal_load_vt, TiedDest, isLds, 0, isTFE, hasRestrictedSOffset>;
  }
}

multiclass MUBUF_Pseudo_Loads<string opName, ValueType load_vt = i32,
                              bit TiedDest = 0, bit isLds = 0> {
  defm NAME : MUBUF_Pseudo_Loads_Helper<opName, load_vt, TiedDest, isLds, 0, 0>;
  defm _VBUFFER : MUBUF_Pseudo_Loads_Helper<opName, load_vt, TiedDest, isLds, 0, 1>;

  if !not(isLds) then {
    defm _TFE : MUBUF_Pseudo_Loads_Helper<opName, load_vt, TiedDest, isLds, 1, 0>;
    defm _TFE_VBUFFER : MUBUF_Pseudo_Loads_Helper<opName, load_vt, TiedDest, isLds, 1, 1>;
  }
}

multiclass MUBUF_Pseudo_Loads_Lds<string opName, ValueType load_vt = i32> {
  defm NAME : MUBUF_Pseudo_Loads<opName, load_vt>;
  defm _LDS : MUBUF_Pseudo_Loads<opName, load_vt, 0, 1>;
}

multiclass MUBUF_Pseudo_Loads_LDSOpc<string opName,
                                     ValueType load_vt = i32,
                                     bit TiedDest = 0,
                                     bit isLds = 0,
                                     bit isLdsOpc = 1> {

  defvar legal_load_vt = !if(!eq(!cast<string>(load_vt), !cast<string>(v3f16)), v4f16, load_vt);

  def _OFFSET : MUBUF_Load_Pseudo <opName, BUFAddrKind.Offset, legal_load_vt, TiedDest, isLds, isLdsOpc>;
  def _OFFEN  : MUBUF_Load_Pseudo <opName, BUFAddrKind.OffEn, legal_load_vt, TiedDest, isLds, isLdsOpc>;
  def _IDXEN  : MUBUF_Load_Pseudo <opName, BUFAddrKind.IdxEn, legal_load_vt, TiedDest, isLds, isLdsOpc>;
  def _BOTHEN : MUBUF_Load_Pseudo <opName, BUFAddrKind.BothEn, legal_load_vt, TiedDest, isLds, isLdsOpc>;

  def _VBUFFER_OFFSET : MUBUF_Load_Pseudo <opName, BUFAddrKind.Offset, legal_load_vt, TiedDest, isLds, isLdsOpc, 0, 1>;
  def _VBUFFER_OFFEN  : MUBUF_Load_Pseudo <opName, BUFAddrKind.OffEn, legal_load_vt, TiedDest, isLds, isLdsOpc, 0, 1>;
  def _VBUFFER_IDXEN  : MUBUF_Load_Pseudo <opName, BUFAddrKind.IdxEn, legal_load_vt, TiedDest, isLds, isLdsOpc, 0, 1>;
  def _VBUFFER_BOTHEN : MUBUF_Load_Pseudo <opName, BUFAddrKind.BothEn, legal_load_vt, TiedDest, isLds, isLdsOpc, 0, 1>;
}

class MUBUF_Store_Pseudo <string opName,
                          int addrKind,
                          ValueType store_vt,
                          bit isTFE = 0,
                          bit hasRestrictedSOffset = 0,
                          list<dag> pattern=[],
                          // Workaround bug bz30254
                          int addrKindCopy = addrKind>
  : MUBUF_Pseudo<opName,
                 (outs),
                 getMUBUFIns<addrKindCopy, [getVregSrcForVT<store_vt>.ret.RegClass], isTFE, hasRestrictedSOffset>.ret,
                 getMUBUFAsmOps<addrKindCopy, 0, 0, isTFE>.ret,
                 pattern>,
    MUBUF_SetupAddr<addrKindCopy> {
  let PseudoInstr = opName # "_" # !if(isTFE, "_tfe", "") #
                    getAddrName<addrKindCopy>.ret;
  let mayLoad = 0;
  let mayStore = 1;
  let elements = getMUBUFElements<store_vt>.ret;
  let tfe = isTFE;
}

multiclass MUBUF_Pseudo_Store_Pats_Common<string BaseInst, ValueType store_vt = i32, SDPatternOperator st = null_frag> {

  def : GCNPat <
    (st store_vt:$vdata, (MUBUFOffset v4i32:$srsrc, i32:$soffset, i32:$offset)),
    (!cast<MUBUF_Pseudo>(BaseInst # _OFFSET) store_vt:$vdata, v4i32:$srsrc, i32:$soffset, i32:$offset)>;

  def : GCNPat <
    (st store_vt:$vdata, (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset, i32:$offset)),
    (!cast<MUBUF_Pseudo>(BaseInst # _ADDR64) store_vt:$vdata, i64:$vaddr, v4i32:$srsrc, i32:$soffset, i32:$offset)>;
}

multiclass MUBUF_Pseudo_Store_Pats<string BaseInst, ValueType store_vt = i32, SDPatternOperator st = null_frag> {
  let OtherPredicates = [HasUnrestrictedSOffset] in {
    defm : MUBUF_Pseudo_Store_Pats_Common<BaseInst, store_vt, st>;
  }
  defm : MUBUF_Pseudo_Store_Pats_Common<BaseInst # "_VBUFFER", store_vt, st>;
}

multiclass MUBUF_Pseudo_Stores_Helper<string opName, ValueType store_vt,
                                      bit isTFE, bit hasRestrictedSOffset> {
  defvar legal_store_vt = !if(!eq(store_vt, v3f16), v4f16, store_vt);

  def _OFFSET : MUBUF_Store_Pseudo <opName, BUFAddrKind.Offset, legal_store_vt, isTFE, hasRestrictedSOffset>,
    MUBUFAddr64Table<0, NAME>;

  def _ADDR64 : MUBUF_Store_Pseudo <opName, BUFAddrKind.Addr64, legal_store_vt, isTFE, hasRestrictedSOffset>,
    MUBUFAddr64Table<1, NAME>;

  def _OFFEN  : MUBUF_Store_Pseudo <opName, BUFAddrKind.OffEn, legal_store_vt, isTFE, hasRestrictedSOffset>;
  def _IDXEN  : MUBUF_Store_Pseudo <opName, BUFAddrKind.IdxEn, legal_store_vt, isTFE, hasRestrictedSOffset>;
  def _BOTHEN : MUBUF_Store_Pseudo <opName, BUFAddrKind.BothEn, legal_store_vt, isTFE, hasRestrictedSOffset>;

  let DisableWQM = 1 in {
    def _OFFSET_exact : MUBUF_Store_Pseudo <opName, BUFAddrKind.Offset, legal_store_vt, isTFE, hasRestrictedSOffset>;
    def _OFFEN_exact  : MUBUF_Store_Pseudo <opName, BUFAddrKind.OffEn, legal_store_vt, isTFE, hasRestrictedSOffset>;
    def _IDXEN_exact  : MUBUF_Store_Pseudo <opName, BUFAddrKind.IdxEn, legal_store_vt, isTFE, hasRestrictedSOffset>;
    def _BOTHEN_exact : MUBUF_Store_Pseudo <opName, BUFAddrKind.BothEn, legal_store_vt, isTFE, hasRestrictedSOffset>;
  }
}

multiclass MUBUF_Pseudo_Stores<string opName, ValueType store_vt = i32> {
  defm NAME : MUBUF_Pseudo_Stores_Helper<opName, store_vt, 0, 0>;
  defm _TFE : MUBUF_Pseudo_Stores_Helper<opName, store_vt, 1, 0>;

  defm _VBUFFER : MUBUF_Pseudo_Stores_Helper<opName, store_vt, 0, 1>;
  defm _TFE_VBUFFER : MUBUF_Pseudo_Stores_Helper<opName, store_vt, 1, 1>;
}

class MUBUF_Pseudo_Store_Lds<string opName>
  : MUBUF_Pseudo<opName,
                 (outs),
                 (ins SReg_128:$srsrc, SCSrc_b32:$soffset, Offset:$offset, CPol:$cpol, i1imm:$swz),
                 " $srsrc, $soffset$offset lds$cpol"> {
  let LGKM_CNT = 1;
  let mayLoad = 1;
  let mayStore = 1;

  let has_vdata = 0;
  let has_vaddr = 0;
  let lds = 1;
  let VALU = 1;

  let Uses = [EXEC, M0];
  let AsmMatchConverter = "cvtMubuf";
}

class getMUBUFAtomicInsDA<RegisterClass vdataClass, bit vdata_in, bit hasRestrictedSOffset,
                          list<RegisterClass> vaddrList=[]> {
  RegisterClass vaddrClass = !if(!empty(vaddrList), ?, !head(vaddrList));
  RegisterOperand vdata_op = getLdStRegisterOperand<vdataClass>.ret;

  dag VData = !if(vdata_in, (ins vdata_op:$vdata_in), (ins vdata_op:$vdata));
  dag Data = !if(!empty(vaddrList), VData, !con(VData, (ins vaddrClass:$vaddr)));
  dag SOffset = !if(hasRestrictedSOffset, (ins SReg_32:$soffset), (ins SCSrc_b32:$soffset));
  dag MainInputs = !con((ins SReg_128:$srsrc), SOffset, (ins Offset:$offset));
  dag CPol = !if(vdata_in, (ins CPol_GLC_WithDefault:$cpol),
                           (ins CPol_NonGLC_WithDefault:$cpol));

  dag ret = !con(Data, MainInputs, CPol);
}

class getMUBUFAtomicIns<int addrKind,
                        RegisterClass vdataClass,
                        bit vdata_in,
                        bit hasRestrictedSOffset,
                        // Workaround bug bz30254
                        RegisterClass vdataClassCopy=vdataClass> {
  dag ret =
    !if(!eq(addrKind, BUFAddrKind.Offset),
            getMUBUFAtomicInsDA<vdataClassCopy, vdata_in, hasRestrictedSOffset>.ret,
    !if(!eq(addrKind, BUFAddrKind.OffEn),
            getMUBUFAtomicInsDA<vdataClassCopy, vdata_in, hasRestrictedSOffset, [VGPR_32]>.ret,
    !if(!eq(addrKind, BUFAddrKind.IdxEn),
            getMUBUFAtomicInsDA<vdataClassCopy, vdata_in, hasRestrictedSOffset, [VGPR_32]>.ret,
    !if(!eq(addrKind, BUFAddrKind.BothEn),
            getMUBUFAtomicInsDA<vdataClassCopy, vdata_in, hasRestrictedSOffset, [VReg_64]>.ret,
    !if(!eq(addrKind, BUFAddrKind.Addr64),
            getMUBUFAtomicInsDA<vdataClassCopy, vdata_in, hasRestrictedSOffset, [VReg_64]>.ret,
    (ins))))));
}

class MUBUF_Atomic_Pseudo<string opName,
                          int addrKind,
                          dag outs,
                          dag ins,
                          string asmOps,
                          list<dag> pattern=[],
                          // Workaround bug bz30254
                          int addrKindCopy = addrKind>
  : MUBUF_Pseudo<opName, outs, ins, asmOps, pattern>,
    MUBUF_SetupAddr<addrKindCopy> {
  let mayStore = 1;
  let mayLoad = 1;
  let hasPostISelHook = 1;
  let hasSideEffects = 1;
  let DisableWQM = 1;
  let has_glc = 0;
  let has_dlc = 0;
  let has_sccb = 1;
  let AsmMatchConverter = "cvtMubufAtomic";
}

class MUBUF_AtomicNoRet_Pseudo<string opName, int addrKind,
                               RegisterClass vdataClass,
                               bit hasRestrictedSOffset = 0,
                               list<dag> pattern=[],
                               // Workaround bug bz30254
                               int addrKindCopy = addrKind,
                               RegisterClass vdataClassCopy = vdataClass>
  : MUBUF_Atomic_Pseudo<opName, addrKindCopy,
                        (outs),
                        getMUBUFAtomicIns<addrKindCopy, vdataClassCopy, 0, hasRestrictedSOffset>.ret,
                        getMUBUFAsmOps<addrKindCopy>.ret,
                        pattern> {
  let PseudoInstr = opName # "_" # getAddrName<addrKindCopy>.ret;
  let glc_value = 0;
  let dlc_value = 0;
  let sccb_value = 0;
  let IsAtomicNoRet = 1;
}

class MUBUF_AtomicRet_Pseudo<string opName, int addrKind,
                             RegisterClass vdataClass,
                             bit hasRestrictedSOffset = 0,
                             list<dag> pattern=[],
                             // Workaround bug bz30254
                             int addrKindCopy = addrKind,
                             RegisterClass vdataClassCopy = vdataClass,
                             RegisterOperand vdata_op = getLdStRegisterOperand<vdataClass>.ret>
  : MUBUF_Atomic_Pseudo<opName, addrKindCopy,
                        (outs vdata_op:$vdata),
                        getMUBUFAtomicIns<addrKindCopy, vdataClassCopy, 1, hasRestrictedSOffset>.ret,
                        getMUBUFAsmOps<addrKindCopy>.ret,
                        pattern> {
  let PseudoInstr = opName # "_rtn_" # getAddrName<addrKindCopy>.ret;
  let glc_value = 1;
  let dlc_value = 0;
  let sccb_value = 0;
  let IsAtomicRet = 1;
  let Constraints = "$vdata = $vdata_in";
  let DisableEncoding = "$vdata_in";
}

multiclass MUBUF_Pseudo_Atomics_NO_RTN <string opName,
                                        RegisterClass vdataClass,
                                        ValueType vdataType> {
  let FPAtomic = vdataType.isFP in {
    def _OFFSET : MUBUF_AtomicNoRet_Pseudo <opName, BUFAddrKind.Offset, vdataClass, 0>,
                  MUBUFAddr64Table <0, NAME>;
    def _ADDR64 : MUBUF_AtomicNoRet_Pseudo <opName, BUFAddrKind.Addr64, vdataClass, 0>,
                  MUBUFAddr64Table <1, NAME>;
    def _OFFEN  : MUBUF_AtomicNoRet_Pseudo <opName, BUFAddrKind.OffEn,  vdataClass, 0>;
    def _IDXEN  : MUBUF_AtomicNoRet_Pseudo <opName, BUFAddrKind.IdxEn,  vdataClass, 0>;
    def _BOTHEN : MUBUF_AtomicNoRet_Pseudo <opName, BUFAddrKind.BothEn, vdataClass, 0>;

    def _VBUFFER_OFFSET : MUBUF_AtomicNoRet_Pseudo <opName #_vbuffer, BUFAddrKind.Offset, vdataClass, 1>,
                  MUBUFAddr64Table <0, NAME # "_VBUFFER">;
    def _VBUFFER_ADDR64 : MUBUF_AtomicNoRet_Pseudo <opName #_vbuffer, BUFAddrKind.Addr64, vdataClass, 1>,
                  MUBUFAddr64Table <1, NAME # "_VBUFFER">;
    def _VBUFFER_OFFEN  : MUBUF_AtomicNoRet_Pseudo <opName #_vbuffer, BUFAddrKind.OffEn,  vdataClass, 1>;
    def _VBUFFER_IDXEN  : MUBUF_AtomicNoRet_Pseudo <opName #_vbuffer, BUFAddrKind.IdxEn,  vdataClass, 1>;
    def _VBUFFER_BOTHEN : MUBUF_AtomicNoRet_Pseudo <opName #_vbuffer, BUFAddrKind.BothEn, vdataClass, 1>;
  }
}

multiclass MUBUF_Pseudo_Atomics_RTN <string opName,
                                     RegisterClass vdataClass,
                                     ValueType vdataType,
                                     SDPatternOperator atomic> {
  let FPAtomic = vdataType.isFP in {
    def _OFFSET_RTN : MUBUF_AtomicRet_Pseudo <opName, BUFAddrKind.Offset, vdataClass, 0,
      [(set vdataType:$vdata,
       (atomic (MUBUFOffset v4i32:$srsrc, i32:$soffset, i32:$offset),
               vdataType:$vdata_in))]>,
      MUBUFAddr64Table <0, NAME # "_RTN">;

    def _ADDR64_RTN : MUBUF_AtomicRet_Pseudo <opName, BUFAddrKind.Addr64, vdataClass, 0,
      [(set vdataType:$vdata,
       (atomic (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset, i32:$offset),
                vdataType:$vdata_in))]>,
      MUBUFAddr64Table <1, NAME # "_RTN">;

    def _OFFEN_RTN  : MUBUF_AtomicRet_Pseudo <opName, BUFAddrKind.OffEn,  vdataClass, 0>;
    def _IDXEN_RTN  : MUBUF_AtomicRet_Pseudo <opName, BUFAddrKind.IdxEn,  vdataClass, 0>;
    def _BOTHEN_RTN : MUBUF_AtomicRet_Pseudo <opName, BUFAddrKind.BothEn, vdataClass, 0>;

    def _VBUFFER_OFFSET_RTN : MUBUF_AtomicRet_Pseudo <opName #_vbuffer, BUFAddrKind.Offset, vdataClass, 1,
      [(set vdataType:$vdata,
       (atomic (MUBUFOffset v4i32:$srsrc, i32:$soffset, i32:$offset),
               vdataType:$vdata_in))]>,
      MUBUFAddr64Table <0, NAME # "_VBUFFER_RTN">;

    def _VBUFFER_ADDR64_RTN : MUBUF_AtomicRet_Pseudo <opName #_vbuffer, BUFAddrKind.Addr64, vdataClass, 1,
      [(set vdataType:$vdata,
       (atomic (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset, i32:$offset),
                vdataType:$vdata_in))]>,
      MUBUFAddr64Table <1, NAME # "_VBUFFER_RTN">;

    def _VBUFFER_OFFEN_RTN  : MUBUF_AtomicRet_Pseudo <opName #_vbuffer, BUFAddrKind.OffEn,  vdataClass, 1>;
    def _VBUFFER_IDXEN_RTN  : MUBUF_AtomicRet_Pseudo <opName #_vbuffer, BUFAddrKind.IdxEn,  vdataClass, 1>;
    def _VBUFFER_BOTHEN_RTN : MUBUF_AtomicRet_Pseudo <opName #_vbuffer, BUFAddrKind.BothEn, vdataClass, 1>;
  }
}

multiclass MUBUF_Pseudo_Atomics <string opName,
                                 RegisterClass vdataClass,
                                 ValueType vdataType,
                                 SDPatternOperator atomic = null_frag> :
  MUBUF_Pseudo_Atomics_NO_RTN<opName, vdataClass, vdataType>,
  MUBUF_Pseudo_Atomics_RTN<opName, vdataClass, vdataType, atomic>;


//===----------------------------------------------------------------------===//
// MUBUF Instructions
//===----------------------------------------------------------------------===//

defm BUFFER_LOAD_FORMAT_X : MUBUF_Pseudo_Loads_Lds <
  "buffer_load_format_x", f32
>;
defm BUFFER_LOAD_FORMAT_XY : MUBUF_Pseudo_Loads <
  "buffer_load_format_xy", v2f32
>;
defm BUFFER_LOAD_FORMAT_XYZ : MUBUF_Pseudo_Loads <
  "buffer_load_format_xyz", v3f32
>;
defm BUFFER_LOAD_FORMAT_XYZW : MUBUF_Pseudo_Loads <
  "buffer_load_format_xyzw", v4f32
>;
defm BUFFER_STORE_FORMAT_X : MUBUF_Pseudo_Stores <
  "buffer_store_format_x", f32
>;
defm BUFFER_STORE_FORMAT_XY : MUBUF_Pseudo_Stores <
  "buffer_store_format_xy", v2f32
>;
defm BUFFER_STORE_FORMAT_XYZ : MUBUF_Pseudo_Stores <
  "buffer_store_format_xyz", v3f32
>;
defm BUFFER_STORE_FORMAT_XYZW : MUBUF_Pseudo_Stores <
  "buffer_store_format_xyzw", v4f32
>;

let OtherPredicates = [HasUnpackedD16VMem], D16Buf = 1 in {
let TiedSourceNotRead = 1 in {
  defm BUFFER_LOAD_FORMAT_D16_X_gfx80 : MUBUF_Pseudo_Loads <
    "buffer_load_format_d16_x", i32
  >;
  defm BUFFER_LOAD_FORMAT_D16_XY_gfx80 : MUBUF_Pseudo_Loads <
    "buffer_load_format_d16_xy", v2i32
  >;
  defm BUFFER_LOAD_FORMAT_D16_XYZ_gfx80 : MUBUF_Pseudo_Loads <
    "buffer_load_format_d16_xyz", v3i32
  >;
  defm BUFFER_LOAD_FORMAT_D16_XYZW_gfx80 : MUBUF_Pseudo_Loads <
   "buffer_load_format_d16_xyzw", v4i32
  >;
}
  defm BUFFER_STORE_FORMAT_D16_X_gfx80 : MUBUF_Pseudo_Stores <
    "buffer_store_format_d16_x", i32
  >;
  defm BUFFER_STORE_FORMAT_D16_XY_gfx80 : MUBUF_Pseudo_Stores <
    "buffer_store_format_d16_xy", v2i32
  >;
  defm BUFFER_STORE_FORMAT_D16_XYZ_gfx80 : MUBUF_Pseudo_Stores <
    "buffer_store_format_d16_xyz", v3i32
  >;
  defm BUFFER_STORE_FORMAT_D16_XYZW_gfx80 : MUBUF_Pseudo_Stores <
    "buffer_store_format_d16_xyzw", v4i32
  >;
} // End OtherPredicates = [HasUnpackedD16VMem], D16Buf = 1.

let OtherPredicates = [HasPackedD16VMem], D16Buf = 1 in {
let TiedSourceNotRead = 1 in {
  defm BUFFER_LOAD_FORMAT_D16_X : MUBUF_Pseudo_Loads <
    "buffer_load_format_d16_x", f16
  >;
  defm BUFFER_LOAD_FORMAT_D16_XY : MUBUF_Pseudo_Loads <
    "buffer_load_format_d16_xy", v2f16
  >;
  defm BUFFER_LOAD_FORMAT_D16_XYZ : MUBUF_Pseudo_Loads <
    "buffer_load_format_d16_xyz", v3f16
  >;
  defm BUFFER_LOAD_FORMAT_D16_XYZW : MUBUF_Pseudo_Loads <
    "buffer_load_format_d16_xyzw", v4f16
  >;
}
  defm BUFFER_STORE_FORMAT_D16_X : MUBUF_Pseudo_Stores <
    "buffer_store_format_d16_x", f16
  >;
  defm BUFFER_STORE_FORMAT_D16_XY : MUBUF_Pseudo_Stores <
    "buffer_store_format_d16_xy", v2f16
  >;
  defm BUFFER_STORE_FORMAT_D16_XYZ : MUBUF_Pseudo_Stores <
    "buffer_store_format_d16_xyz", v3f16
  >;
  defm BUFFER_STORE_FORMAT_D16_XYZW : MUBUF_Pseudo_Stores <
    "buffer_store_format_d16_xyzw", v4f16
  >;
} // End OtherPredicates = [HasPackedD16VMem], D16Buf = 1.

defm BUFFER_LOAD_UBYTE : MUBUF_Pseudo_Loads_Lds <
  "buffer_load_ubyte", i32
>;
defm BUFFER_LOAD_SBYTE : MUBUF_Pseudo_Loads_Lds <
  "buffer_load_sbyte", i32
>;
defm BUFFER_LOAD_USHORT : MUBUF_Pseudo_Loads_Lds <
  "buffer_load_ushort", i32
>;
defm BUFFER_LOAD_SSHORT : MUBUF_Pseudo_Loads_Lds <
  "buffer_load_sshort", i32
>;
defm BUFFER_LOAD_DWORD : MUBUF_Pseudo_Loads_Lds <
  "buffer_load_dword", i32
>;
defm BUFFER_LOAD_DWORDX2 : MUBUF_Pseudo_Loads <
  "buffer_load_dwordx2", v2i32
>;
defm BUFFER_LOAD_DWORDX3 : MUBUF_Pseudo_Loads <
  "buffer_load_dwordx3", v3i32
>;
defm BUFFER_LOAD_DWORDX4 : MUBUF_Pseudo_Loads <
  "buffer_load_dwordx4", v4i32
>;

defm BUFFER_LOAD_LDS_B32 : MUBUF_Pseudo_Loads_LDSOpc <
  "buffer_load_lds_b32", i32
>;
defm BUFFER_LOAD_LDS_FORMAT_X : MUBUF_Pseudo_Loads_LDSOpc <
  "buffer_load_lds_format_x", f32
>;
defm BUFFER_LOAD_LDS_I8 : MUBUF_Pseudo_Loads_LDSOpc <
  "buffer_load_lds_i8", i32
>;
defm BUFFER_LOAD_LDS_I16 : MUBUF_Pseudo_Loads_LDSOpc <
  "buffer_load_lds_i16", i32
>;
defm BUFFER_LOAD_LDS_U8 : MUBUF_Pseudo_Loads_LDSOpc <
  "buffer_load_lds_u8", i32
>;
defm BUFFER_LOAD_LDS_U16 : MUBUF_Pseudo_Loads_LDSOpc <
  "buffer_load_lds_u16", i32
>;

defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_UBYTE", i32, atomic_load_8_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_USHORT", i32, atomic_load_16_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_UBYTE", i16, atomic_load_8_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_USHORT", i16, atomic_load_16_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_UBYTE", i32, extloadi8_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_UBYTE", i32, zextloadi8_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_SBYTE", i32, sextloadi8_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_USHORT", i32, extloadi16_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_USHORT", i32, zextloadi16_global>;
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_SSHORT", i32, sextloadi16_global>;

foreach vt = Reg32Types.types in {
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_DWORD", vt, load_global>;
}

foreach vt = VReg_64.RegTypes in {
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_DWORDX2", vt, load_global>;
}

foreach vt = VReg_96.RegTypes in {
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_DWORDX3", vt, load_global>;
}

foreach vt = VReg_128.RegTypes in {
defm : MUBUF_Pseudo_Load_Pats<"BUFFER_LOAD_DWORDX4", vt, load_global>;
}

defm BUFFER_STORE_BYTE : MUBUF_Pseudo_Stores <
  "buffer_store_byte", i32
>;
defm BUFFER_STORE_SHORT : MUBUF_Pseudo_Stores <
  "buffer_store_short", i32
>;
defm BUFFER_STORE_DWORD : MUBUF_Pseudo_Stores <
  "buffer_store_dword", i32
>;
defm BUFFER_STORE_DWORDX2 : MUBUF_Pseudo_Stores <
  "buffer_store_dwordx2", v2i32
>;
defm BUFFER_STORE_DWORDX3 : MUBUF_Pseudo_Stores <
  "buffer_store_dwordx3", v3i32
>;
defm BUFFER_STORE_DWORDX4 : MUBUF_Pseudo_Stores <
  "buffer_store_dwordx4", v4i32
>;

defm : MUBUF_Pseudo_Store_Pats<"BUFFER_STORE_BYTE", i32, truncstorei8_global>;
defm : MUBUF_Pseudo_Store_Pats<"BUFFER_STORE_SHORT", i32, truncstorei16_global>;

foreach vt = Reg32Types.types in {
defm : MUBUF_Pseudo_Store_Pats<"BUFFER_STORE_DWORD", vt, store_global>;
}

foreach vt = VReg_64.RegTypes in {
defm : MUBUF_Pseudo_Store_Pats<"BUFFER_STORE_DWORDX2", vt, store_global>;
}

foreach vt = VReg_96.RegTypes in {
defm : MUBUF_Pseudo_Store_Pats<"BUFFER_STORE_DWORDX3", vt, store_global>;
}

foreach vt = VReg_128.RegTypes in {
defm : MUBUF_Pseudo_Store_Pats<"BUFFER_STORE_DWORDX4", vt, store_global>;
}

defm BUFFER_ATOMIC_SWAP : MUBUF_Pseudo_Atomics <
  "buffer_atomic_swap", VGPR_32, i32
>;
defm BUFFER_ATOMIC_CMPSWAP : MUBUF_Pseudo_Atomics <
  "buffer_atomic_cmpswap", VReg_64, v2i32
>;
defm BUFFER_ATOMIC_ADD : MUBUF_Pseudo_Atomics <
  "buffer_atomic_add", VGPR_32, i32
>;
defm BUFFER_ATOMIC_SUB : MUBUF_Pseudo_Atomics <
  "buffer_atomic_sub", VGPR_32, i32
>;
defm BUFFER_ATOMIC_SMIN : MUBUF_Pseudo_Atomics <
  "buffer_atomic_smin", VGPR_32, i32
>;
defm BUFFER_ATOMIC_UMIN : MUBUF_Pseudo_Atomics <
  "buffer_atomic_umin", VGPR_32, i32
>;
defm BUFFER_ATOMIC_SMAX : MUBUF_Pseudo_Atomics <
  "buffer_atomic_smax", VGPR_32, i32
>;
defm BUFFER_ATOMIC_UMAX : MUBUF_Pseudo_Atomics <
  "buffer_atomic_umax", VGPR_32, i32
>;
defm BUFFER_ATOMIC_AND : MUBUF_Pseudo_Atomics <
  "buffer_atomic_and", VGPR_32, i32
>;
defm BUFFER_ATOMIC_OR : MUBUF_Pseudo_Atomics <
  "buffer_atomic_or", VGPR_32, i32
>;
defm BUFFER_ATOMIC_XOR : MUBUF_Pseudo_Atomics <
  "buffer_atomic_xor", VGPR_32, i32
>;
defm BUFFER_ATOMIC_INC : MUBUF_Pseudo_Atomics <
  "buffer_atomic_inc", VGPR_32, i32
>;
defm BUFFER_ATOMIC_DEC : MUBUF_Pseudo_Atomics <
  "buffer_atomic_dec", VGPR_32, i32
>;
defm BUFFER_ATOMIC_SWAP_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_swap_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_CMPSWAP_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_cmpswap_x2", VReg_128, v2i64
>;
defm BUFFER_ATOMIC_ADD_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_add_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_SUB_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_sub_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_SMIN_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_smin_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_UMIN_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_umin_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_SMAX_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_smax_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_UMAX_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_umax_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_AND_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_and_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_OR_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_or_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_XOR_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_xor_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_INC_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_inc_x2", VReg_64, i64
>;
defm BUFFER_ATOMIC_DEC_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_dec_x2", VReg_64, i64
>;

let OtherPredicates = [HasGFX10_BEncoding] in {
  defm BUFFER_ATOMIC_CSUB : MUBUF_Pseudo_Atomics <
    "buffer_atomic_csub", VGPR_32, i32, int_amdgcn_global_atomic_csub
  >;
}

let SubtargetPredicate = isGFX8GFX9 in {
def BUFFER_STORE_LDS_DWORD : MUBUF_Pseudo_Store_Lds <"buffer_store_lds_dword">;
}

let SubtargetPredicate = isGFX6 in { // isn't on CI & VI
/*
defm BUFFER_ATOMIC_RSUB        : MUBUF_Pseudo_Atomics <"buffer_atomic_rsub">;
defm BUFFER_ATOMIC_RSUB_X2     : MUBUF_Pseudo_Atomics <"buffer_atomic_rsub_x2">;
*/

def BUFFER_WBINVL1_SC : MUBUF_Invalidate <"buffer_wbinvl1_sc",
                                          int_amdgcn_buffer_wbinvl1_sc>;
}

let SubtargetPredicate = isGFX6GFX7GFX10Plus in {

defm BUFFER_ATOMIC_FCMPSWAP : MUBUF_Pseudo_Atomics <
  "buffer_atomic_fcmpswap", VReg_64, v2f32, null_frag
>;
}

let SubtargetPredicate = HasAtomicFMinFMaxF32GlobalInsts in {
defm BUFFER_ATOMIC_FMIN : MUBUF_Pseudo_Atomics <
  "buffer_atomic_fmin", VGPR_32, f32, null_frag
>;
defm BUFFER_ATOMIC_FMAX : MUBUF_Pseudo_Atomics <
  "buffer_atomic_fmax", VGPR_32, f32, null_frag
>;
}

let SubtargetPredicate = isGFX6GFX7GFX10 in {
defm BUFFER_ATOMIC_FCMPSWAP_X2 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_fcmpswap_x2", VReg_128, v2f64, null_frag
>;
}

let SubtargetPredicate = HasD16LoadStore in {
let TiedSourceNotRead = 1 in {

defm BUFFER_LOAD_UBYTE_D16 : MUBUF_Pseudo_Loads <
  "buffer_load_ubyte_d16", i32, 1
>;

defm BUFFER_LOAD_UBYTE_D16_HI : MUBUF_Pseudo_Loads <
  "buffer_load_ubyte_d16_hi", i32, 1
>;

defm BUFFER_LOAD_SBYTE_D16 : MUBUF_Pseudo_Loads <
  "buffer_load_sbyte_d16", i32, 1
>;

defm BUFFER_LOAD_SBYTE_D16_HI : MUBUF_Pseudo_Loads <
  "buffer_load_sbyte_d16_hi", i32, 1
>;

defm BUFFER_LOAD_SHORT_D16 : MUBUF_Pseudo_Loads <
  "buffer_load_short_d16", i32, 1
>;

defm BUFFER_LOAD_SHORT_D16_HI : MUBUF_Pseudo_Loads <
  "buffer_load_short_d16_hi", i32, 1
>;

defm BUFFER_LOAD_FORMAT_D16_HI_X : MUBUF_Pseudo_Loads <
  "buffer_load_format_d16_hi_x", i32
>;
} // End TiedSourceNotRead

defm BUFFER_STORE_BYTE_D16_HI : MUBUF_Pseudo_Stores <
  "buffer_store_byte_d16_hi", i32
>;

defm BUFFER_STORE_SHORT_D16_HI : MUBUF_Pseudo_Stores <
  "buffer_store_short_d16_hi", i32
>;

defm BUFFER_STORE_FORMAT_D16_HI_X : MUBUF_Pseudo_Stores <
  "buffer_store_format_d16_hi_x", i32
>;

} // End HasD16LoadStore

let SubtargetPredicate = isNotGFX12Plus in
def BUFFER_WBINVL1 : MUBUF_Invalidate <
  "buffer_wbinvl1", int_amdgcn_buffer_wbinvl1
>;

let SubtargetPredicate = HasAtomicFaddNoRtnInsts in
defm BUFFER_ATOMIC_ADD_F32 : MUBUF_Pseudo_Atomics_NO_RTN<
  "buffer_atomic_add_f32", VGPR_32, f32
>;

let SubtargetPredicate = HasAtomicBufferGlobalPkAddF16NoRtnInsts in
defm BUFFER_ATOMIC_PK_ADD_F16 : MUBUF_Pseudo_Atomics_NO_RTN <
  "buffer_atomic_pk_add_f16", VGPR_32, v2f16
>;

let SubtargetPredicate = HasAtomicFaddRtnInsts in
defm BUFFER_ATOMIC_ADD_F32 : MUBUF_Pseudo_Atomics_RTN<
  "buffer_atomic_add_f32", VGPR_32, f32, null_frag
>;

let SubtargetPredicate = HasAtomicBufferGlobalPkAddF16Insts in
defm BUFFER_ATOMIC_PK_ADD_F16 : MUBUF_Pseudo_Atomics_RTN <
  "buffer_atomic_pk_add_f16", VGPR_32, v2f16, null_frag
>;

let SubtargetPredicate = isGFX12Plus in {
defm BUFFER_ATOMIC_COND_SUB_U32 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_cond_sub_u32", VGPR_32, i32
>;
}

let SubtargetPredicate = HasAtomicBufferPkAddBF16Inst in {
let FPAtomic = 1 in
defm BUFFER_ATOMIC_PK_ADD_BF16 : MUBUF_Pseudo_Atomics <
  "buffer_atomic_pk_add_bf16", VGPR_32, v2bf16
>;
}

//===----------------------------------------------------------------------===//
// MTBUF Instructions
//===----------------------------------------------------------------------===//

defm TBUFFER_LOAD_FORMAT_X     : MTBUF_Pseudo_Loads  <"tbuffer_load_format_x",     VGPR_32,  1>;
defm TBUFFER_LOAD_FORMAT_XY    : MTBUF_Pseudo_Loads  <"tbuffer_load_format_xy",    VReg_64,  2>;
defm TBUFFER_LOAD_FORMAT_XYZ   : MTBUF_Pseudo_Loads  <"tbuffer_load_format_xyz",   VReg_96,  3>;
defm TBUFFER_LOAD_FORMAT_XYZW  : MTBUF_Pseudo_Loads  <"tbuffer_load_format_xyzw",  VReg_128, 4>;
defm TBUFFER_STORE_FORMAT_X    : MTBUF_Pseudo_Stores <"tbuffer_store_format_x",    VGPR_32,  1>;
defm TBUFFER_STORE_FORMAT_XY   : MTBUF_Pseudo_Stores <"tbuffer_store_format_xy",   VReg_64,  2>;
defm TBUFFER_STORE_FORMAT_XYZ  : MTBUF_Pseudo_Stores <"tbuffer_store_format_xyz",  VReg_96,  3>;
defm TBUFFER_STORE_FORMAT_XYZW : MTBUF_Pseudo_Stores <"tbuffer_store_format_xyzw", VReg_128, 4>;

let SubtargetPredicate = HasUnpackedD16VMem, D16Buf = 1 in {
let TiedSourceNotRead = 1 in {
  defm TBUFFER_LOAD_FORMAT_D16_X_gfx80     : MTBUF_Pseudo_Loads  <"tbuffer_load_format_d16_x",     VGPR_32,  1>;
  defm TBUFFER_LOAD_FORMAT_D16_XY_gfx80    : MTBUF_Pseudo_Loads  <"tbuffer_load_format_d16_xy",    VReg_64,  2>;
  defm TBUFFER_LOAD_FORMAT_D16_XYZ_gfx80   : MTBUF_Pseudo_Loads  <"tbuffer_load_format_d16_xyz",   VReg_96,  3>;
  defm TBUFFER_LOAD_FORMAT_D16_XYZW_gfx80  : MTBUF_Pseudo_Loads  <"tbuffer_load_format_d16_xyzw",  VReg_128, 4>;
}
  defm TBUFFER_STORE_FORMAT_D16_X_gfx80    : MTBUF_Pseudo_Stores <"tbuffer_store_format_d16_x",    VGPR_32,  1>;
  defm TBUFFER_STORE_FORMAT_D16_XY_gfx80   : MTBUF_Pseudo_Stores <"tbuffer_store_format_d16_xy",   VReg_64,  2>;
  defm TBUFFER_STORE_FORMAT_D16_XYZ_gfx80  : MTBUF_Pseudo_Stores <"tbuffer_store_format_d16_xyz",  VReg_96,  3>;
  defm TBUFFER_STORE_FORMAT_D16_XYZW_gfx80 : MTBUF_Pseudo_Stores <"tbuffer_store_format_d16_xyzw", VReg_128, 4>;
} // End HasUnpackedD16VMem.

let SubtargetPredicate = HasPackedD16VMem, D16Buf = 1 in {
let TiedSourceNotRead = 1 in {
  defm TBUFFER_LOAD_FORMAT_D16_X     : MTBUF_Pseudo_Loads  <"tbuffer_load_format_d16_x",     VGPR_32, 1>;
  defm TBUFFER_LOAD_FORMAT_D16_XY    : MTBUF_Pseudo_Loads  <"tbuffer_load_format_d16_xy",    VGPR_32, 2>;
  defm TBUFFER_LOAD_FORMAT_D16_XYZ   : MTBUF_Pseudo_Loads  <"tbuffer_load_format_d16_xyz",   VReg_64, 3>;
  defm TBUFFER_LOAD_FORMAT_D16_XYZW  : MTBUF_Pseudo_Loads  <"tbuffer_load_format_d16_xyzw",  VReg_64, 4>;
}
  defm TBUFFER_STORE_FORMAT_D16_X    : MTBUF_Pseudo_Stores <"tbuffer_store_format_d16_x",    VGPR_32, 1>;
  defm TBUFFER_STORE_FORMAT_D16_XY   : MTBUF_Pseudo_Stores <"tbuffer_store_format_d16_xy",   VGPR_32, 2>;
  defm TBUFFER_STORE_FORMAT_D16_XYZ  : MTBUF_Pseudo_Stores <"tbuffer_store_format_d16_xyz",  VReg_64, 3>;
  defm TBUFFER_STORE_FORMAT_D16_XYZW : MTBUF_Pseudo_Stores <"tbuffer_store_format_d16_xyzw", VReg_64, 4>;
} // End HasPackedD16VMem.

let SubtargetPredicate = isGFX7Plus in {

//===----------------------------------------------------------------------===//
// Instruction definitions for CI and newer.
//===----------------------------------------------------------------------===//

def BUFFER_WBINVL1_VOL : MUBUF_Invalidate <"buffer_wbinvl1_vol",
                                           int_amdgcn_buffer_wbinvl1_vol>;

} // End let SubtargetPredicate = isGFX7Plus

let SubtargetPredicate = isGFX90APlus in {
  def BUFFER_WBL2  : MUBUF_Invalidate<"buffer_wbl2"> {
    let has_glc = 1;
    let has_sccb = 1;
    let InOperandList = (ins CPol_0:$cpol);
    let AsmOperands = "$cpol";
  }
  def BUFFER_INVL2 : MUBUF_Invalidate<"buffer_invl2"> {
    let SubtargetPredicate = isGFX90AOnly;
  }
} // End SubtargetPredicate = isGFX90APlus

let SubtargetPredicate = HasFlatBufferGlobalAtomicFaddF64Inst in {
  defm BUFFER_ATOMIC_ADD_F64 : MUBUF_Pseudo_Atomics<"buffer_atomic_add_f64", VReg_64, f64>;
} // End SubtargetPredicate = HasFlatBufferGlobalAtomicFaddF64Inst

let SubtargetPredicate = HasAtomicFMinFMaxF64GlobalInsts in {
  // Note the names can be buffer_atomic_fmin_x2/buffer_atomic_fmax_x2
  // depending on some subtargets.
  defm BUFFER_ATOMIC_MIN_F64 : MUBUF_Pseudo_Atomics<"buffer_atomic_min_f64", VReg_64, f64>;
  defm BUFFER_ATOMIC_MAX_F64 : MUBUF_Pseudo_Atomics<"buffer_atomic_max_f64", VReg_64, f64>;
}

def BUFFER_INV : MUBUF_Invalidate<"buffer_inv"> {
  let SubtargetPredicate = isGFX940Plus;
  let has_glc = 1;
  let has_sccb = 1;
  let InOperandList = (ins CPol_0:$cpol);
  let AsmOperands = "$cpol";
}

def BUFFER_GL0_INV : MUBUF_Invalidate<"buffer_gl0_inv">;
def BUFFER_GL1_INV : MUBUF_Invalidate<"buffer_gl1_inv">;

//===----------------------------------------------------------------------===//
// MUBUF Patterns
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// buffer_load/store_format patterns
//===----------------------------------------------------------------------===//

multiclass MUBUF_LoadIntrinsicPat_Common<SDPatternOperator name, ValueType vt,
                                  string opcode, ValueType memoryVt = vt> {
  defvar st = !if(!eq(memoryVt, vt), name, mubuf_intrinsic_load<name, memoryVt>);

  def : GCNPat<
    (vt (st v4i32:$rsrc, 0, 0, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$auxiliary, 0)),
    (!cast<MUBUF_Pseudo>(opcode # _OFFSET) SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (vt (st v4i32:$rsrc, 0, i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$auxiliary, 0)),
    (!cast<MUBUF_Pseudo>(opcode # _OFFEN) VGPR_32:$voffset, SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (vt (st v4i32:$rsrc, i32:$vindex, 0, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$auxiliary, timm)),
    (!cast<MUBUF_Pseudo>(opcode # _IDXEN) VGPR_32:$vindex, SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (vt (st v4i32:$rsrc, i32:$vindex, i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$auxiliary, timm)),
    (!cast<MUBUF_Pseudo>(opcode # _BOTHEN)
      (REG_SEQUENCE VReg_64, VGPR_32:$vindex, sub0, VGPR_32:$voffset, sub1),
      SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;
}

multiclass MUBUF_LoadIntrinsicPat<SDPatternOperator name, ValueType vt,
                                  string opcode, ValueType memoryVt = vt>{
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : MUBUF_LoadIntrinsicPat_Common<name, vt, opcode, memoryVt>;
  }
  defm : MUBUF_LoadIntrinsicPat_Common<name, vt, opcode # "_VBUFFER", memoryVt>;
}

defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format, f32, "BUFFER_LOAD_FORMAT_X">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format, i32, "BUFFER_LOAD_FORMAT_X">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format, v2f32, "BUFFER_LOAD_FORMAT_XY">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format, v2i32, "BUFFER_LOAD_FORMAT_XY">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format, v3f32, "BUFFER_LOAD_FORMAT_XYZ">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format, v3i32, "BUFFER_LOAD_FORMAT_XYZ">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format, v4f32, "BUFFER_LOAD_FORMAT_XYZW">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format, v4i32, "BUFFER_LOAD_FORMAT_XYZW">;

defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_tfe, v2i32, "BUFFER_LOAD_FORMAT_X_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_tfe, v3i32, "BUFFER_LOAD_FORMAT_XY_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_tfe, v4i32, "BUFFER_LOAD_FORMAT_XYZ_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_tfe, v5i32, "BUFFER_LOAD_FORMAT_XYZW_TFE">;

let OtherPredicates = [HasUnpackedD16VMem] in {
  defm : MUBUF_LoadIntrinsicPat_Common<SIbuffer_load_format_d16, f16, "BUFFER_LOAD_FORMAT_D16_X_gfx80">;
  defm : MUBUF_LoadIntrinsicPat_Common<SIbuffer_load_format_d16, i16, "BUFFER_LOAD_FORMAT_D16_X_gfx80">;
  defm : MUBUF_LoadIntrinsicPat_Common<SIbuffer_load_format_d16, i32, "BUFFER_LOAD_FORMAT_D16_X_gfx80">;
  defm : MUBUF_LoadIntrinsicPat_Common<SIbuffer_load_format_d16, v2i32, "BUFFER_LOAD_FORMAT_D16_XY_gfx80">;
  defm : MUBUF_LoadIntrinsicPat_Common<SIbuffer_load_format_d16, v3i32, "BUFFER_LOAD_FORMAT_D16_XYZ_gfx80">;
  defm : MUBUF_LoadIntrinsicPat_Common<SIbuffer_load_format_d16, v4i32, "BUFFER_LOAD_FORMAT_D16_XYZW_gfx80">;
} // End HasUnpackedD16VMem.

let OtherPredicates = [HasPackedD16VMem] in {
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, f16, "BUFFER_LOAD_FORMAT_D16_X">;
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, i16, "BUFFER_LOAD_FORMAT_D16_X">;
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, i32, "BUFFER_LOAD_FORMAT_D16_X">;
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, v2f16, "BUFFER_LOAD_FORMAT_D16_XY">;
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, v2i16, "BUFFER_LOAD_FORMAT_D16_XY">;
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, v4f16, "BUFFER_LOAD_FORMAT_D16_XYZ", v3f16>;
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, v4i16, "BUFFER_LOAD_FORMAT_D16_XYZ", v3i16>;
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, v4f16, "BUFFER_LOAD_FORMAT_D16_XYZW">;
  defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_format_d16, v4i16, "BUFFER_LOAD_FORMAT_D16_XYZW">;
} // End HasPackedD16VMem.

foreach vt = Reg32Types.types in {
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load, vt, "BUFFER_LOAD_DWORD">;
}

foreach vt = Reg64Types.types in {
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load, vt, "BUFFER_LOAD_DWORDX2">;
}

foreach vt = Reg96Types.types in {
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load, vt, "BUFFER_LOAD_DWORDX3">;
}

foreach vt = Reg128Types.types in {
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load, vt, "BUFFER_LOAD_DWORDX4">;
}

defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_byte, i32, "BUFFER_LOAD_SBYTE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_short, i32, "BUFFER_LOAD_SSHORT">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_ubyte, i32, "BUFFER_LOAD_UBYTE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_ushort,  i32, "BUFFER_LOAD_USHORT">;

defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_tfe, v2i32, "BUFFER_LOAD_DWORD_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_tfe, v3i32, "BUFFER_LOAD_DWORDX2_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_tfe, v4i32, "BUFFER_LOAD_DWORDX3_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_tfe, v5i32, "BUFFER_LOAD_DWORDX4_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_byte_tfe, v2i32, "BUFFER_LOAD_SBYTE_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_short_tfe, v2i32, "BUFFER_LOAD_SSHORT_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_ubyte_tfe, v2i32, "BUFFER_LOAD_UBYTE_TFE">;
defm : MUBUF_LoadIntrinsicPat<SIbuffer_load_ushort_tfe, v2i32, "BUFFER_LOAD_USHORT_TFE">;

multiclass MUBUF_StoreIntrinsicPat_Common<SDPatternOperator name, ValueType vt,
                                   string opcode, ValueType memoryVt = vt> {
  defvar st = !if(!eq(memoryVt, vt), name, mubuf_intrinsic_store<name, memoryVt>);

  def : GCNPat<
    (st vt:$vdata, v4i32:$rsrc, 0, 0, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$auxiliary, 0),
    (!cast<MUBUF_Pseudo>(opcode # _OFFSET_exact) getVregSrcForVT<vt>.ret:$vdata, SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (st vt:$vdata, v4i32:$rsrc, 0, i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$auxiliary, 0),
    (!cast<MUBUF_Pseudo>(opcode # _OFFEN_exact) getVregSrcForVT<vt>.ret:$vdata, VGPR_32:$voffset, SReg_128:$rsrc, SCSrc_b32:$soffset,
      timm:$offset, (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (st vt:$vdata, v4i32:$rsrc, i32:$vindex, 0, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$auxiliary, timm),
    (!cast<MUBUF_Pseudo>(opcode # _IDXEN_exact) getVregSrcForVT<vt>.ret:$vdata, VGPR_32:$vindex, SReg_128:$rsrc, SCSrc_b32:$soffset,
      timm:$offset, (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (st vt:$vdata, v4i32:$rsrc, i32:$vindex, i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$auxiliary, timm),
    (!cast<MUBUF_Pseudo>(opcode # _BOTHEN_exact)
      getVregSrcForVT<vt>.ret:$vdata,
      (REG_SEQUENCE VReg_64, VGPR_32:$vindex, sub0, VGPR_32:$voffset, sub1),
      SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset, (extract_cpol $auxiliary),
      (extract_swz $auxiliary))
  >;
}

multiclass MUBUF_StoreIntrinsicPat<SDPatternOperator name, ValueType vt,
                                   string opcode, ValueType memoryVt = vt> {
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : MUBUF_StoreIntrinsicPat_Common<name, vt, opcode, memoryVt>;
  }
  defm : MUBUF_StoreIntrinsicPat_Common<name, vt, opcode # "_VBUFFER", memoryVt>;
}

defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, f32, "BUFFER_STORE_FORMAT_X">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, i32, "BUFFER_STORE_FORMAT_X">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, v2f32, "BUFFER_STORE_FORMAT_XY">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, v2i32, "BUFFER_STORE_FORMAT_XY">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, v2i32, "BUFFER_STORE_FORMAT_XY">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, v3f32, "BUFFER_STORE_FORMAT_XYZ">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, v3i32, "BUFFER_STORE_FORMAT_XYZ">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, v4f32, "BUFFER_STORE_FORMAT_XYZW">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format, v4i32, "BUFFER_STORE_FORMAT_XYZW">;

let OtherPredicates = [HasUnpackedD16VMem] in {
  defm : MUBUF_StoreIntrinsicPat_Common<SIbuffer_store_format_d16, f16, "BUFFER_STORE_FORMAT_D16_X_gfx80">;
  defm : MUBUF_StoreIntrinsicPat_Common<SIbuffer_store_format_d16, i16, "BUFFER_STORE_FORMAT_D16_X_gfx80">;
  defm : MUBUF_StoreIntrinsicPat_Common<SIbuffer_store_format_d16, i32, "BUFFER_STORE_FORMAT_D16_X_gfx80">;
  defm : MUBUF_StoreIntrinsicPat_Common<SIbuffer_store_format_d16, v2i32, "BUFFER_STORE_FORMAT_D16_XY_gfx80">;
  defm : MUBUF_StoreIntrinsicPat_Common<SIbuffer_store_format_d16, v3i32, "BUFFER_STORE_FORMAT_D16_XYZ_gfx80">;
  defm : MUBUF_StoreIntrinsicPat_Common<SIbuffer_store_format_d16, v4i32, "BUFFER_STORE_FORMAT_D16_XYZW_gfx80">;
} // End HasUnpackedD16VMem.

let OtherPredicates = [HasPackedD16VMem] in {
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, f16, "BUFFER_STORE_FORMAT_D16_X">;
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, i16, "BUFFER_STORE_FORMAT_D16_X">;
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, i32, "BUFFER_STORE_FORMAT_D16_X">;
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, v2f16, "BUFFER_STORE_FORMAT_D16_XY">;
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, v2i16, "BUFFER_STORE_FORMAT_D16_XY">;
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, v4f16, "BUFFER_STORE_FORMAT_D16_XYZ", v3f16>;
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, v4i16, "BUFFER_STORE_FORMAT_D16_XYZ", v3i16>;
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, v4f16, "BUFFER_STORE_FORMAT_D16_XYZW">;
  defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_format_d16, v4i16, "BUFFER_STORE_FORMAT_D16_XYZW">;
} // End HasPackedD16VMem.

foreach vt = Reg32Types.types in {
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store, vt, "BUFFER_STORE_DWORD">;
}

foreach vt = Reg64Types.types in {
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store, vt, "BUFFER_STORE_DWORDX2">;
}

foreach vt = Reg96Types.types in {
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store, vt, "BUFFER_STORE_DWORDX3">;
}

foreach vt = Reg128Types.types in {
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store, vt, "BUFFER_STORE_DWORDX4">;
}

defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_byte, i32, "BUFFER_STORE_BYTE">;
defm : MUBUF_StoreIntrinsicPat<SIbuffer_store_short, i32, "BUFFER_STORE_SHORT">;

//===----------------------------------------------------------------------===//
// buffer_atomic patterns
//===----------------------------------------------------------------------===//

multiclass BufferAtomicPat_Common<string OpPrefix, ValueType vt, string Inst, bit isIntr = 0> {
  foreach RtnMode = ["ret", "noret"] in {

  defvar Op = !cast<SDPatternOperator>(OpPrefix
                                       # !if(!eq(RtnMode, "ret"), "", "_noret")
                                       # !if(isIntr, "", "_" # vt));
  defvar InstSuffix = !if(!eq(RtnMode, "ret"), "_RTN", "");

  let AddedComplexity = !if(!eq(RtnMode, "ret"), 0, 1) in {
  def : GCNPat<
    (vt (Op (MUBUFOffset v4i32:$srsrc, i32:$soffset, i32:$offset), vt:$vdata_in)),
    (!cast<MUBUF_Pseudo>(Inst # "_OFFSET" # InstSuffix) getVregSrcForVT<vt>.ret:$vdata_in,
      SReg_128:$srsrc, SCSrc_b32:$soffset, Offset:$offset)
  >;

  def : GCNPat<
    (vt (Op (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset, i32:$offset),
      vt:$vdata_in)),
    (!cast<MUBUF_Pseudo>(Inst # "_ADDR64" # InstSuffix) getVregSrcForVT<vt>.ret:$vdata_in,
      VReg_64:$vaddr, SReg_128:$srsrc, SCSrc_b32:$soffset, Offset:$offset)
  >;
  } // end let AddedComplexity

  } // end foreach RtnMode
}

multiclass BufferAtomicPat<string OpPrefix, ValueType vt, string Inst, bit isIntr = 0> {
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : BufferAtomicPat_Common<OpPrefix, vt, Inst, isIntr>;
  }
  defm : BufferAtomicPat_Common<OpPrefix, vt, Inst # "_VBUFFER", isIntr>;
}

multiclass BufferAtomicIntrPat<string OpPrefix, ValueType vt, string Inst> {
  defm : BufferAtomicPat<OpPrefix, vt, Inst, /* isIntr */ 1>;
}

multiclass BufferAtomicCmpSwapPat_Common<ValueType vt, ValueType data_vt, string Inst> {
  foreach RtnMode = ["ret", "noret"] in {

  defvar Op = !cast<SDPatternOperator>("AMDGPUatomic_cmp_swap_global"
                                       # !if(!eq(RtnMode, "ret"), "", "_noret")
                                       # "_" # vt);
  defvar InstSuffix = !if(!eq(RtnMode, "ret"), "_RTN", "");
  defvar data_vt_RC = getVregSrcForVT<data_vt>.ret.RegClass;

  let AddedComplexity = !if(!eq(RtnMode, "ret"), 0, 1) in {
  defvar OffsetResDag = (!cast<MUBUF_Pseudo>(Inst # "_OFFSET" # InstSuffix)
    data_vt_RC:$vdata_in, SReg_128:$srsrc, SCSrc_b32:$soffset,
    Offset:$offset);
  def : GCNPat<
    (vt (Op (MUBUFOffset v4i32:$srsrc, i32:$soffset, i32:$offset), data_vt:$vdata_in)),
    !if(!eq(RtnMode, "ret"),
      (EXTRACT_SUBREG (vt (COPY_TO_REGCLASS OffsetResDag, data_vt_RC)),
        !if(!eq(vt, i32), sub0, sub0_sub1)),
      OffsetResDag)
  >;

  defvar Addr64ResDag = (!cast<MUBUF_Pseudo>(Inst # "_ADDR64" # InstSuffix)
    data_vt_RC:$vdata_in, VReg_64:$vaddr, SReg_128:$srsrc,
    SCSrc_b32:$soffset, Offset:$offset);
  def : GCNPat<
    (vt (Op (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset, i32:$offset),
      data_vt:$vdata_in)),
    !if(!eq(RtnMode, "ret"),
      (EXTRACT_SUBREG (vt (COPY_TO_REGCLASS Addr64ResDag, data_vt_RC)),
        !if(!eq(vt, i32), sub0, sub0_sub1)),
      Addr64ResDag)
  >;
  } // end let AddedComplexity

  } // end foreach RtnMode
}

multiclass BufferAtomicCmpSwapPat<ValueType vt, ValueType data_vt, string Inst> {
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : BufferAtomicCmpSwapPat_Common<vt, data_vt, Inst>;
  }
  defm : BufferAtomicCmpSwapPat_Common<vt, data_vt, Inst # "_VBUFFER">;
}


foreach Ty = [i32, i64] in {

defvar Suffix = !if(!eq(Ty, i64), "_X2", "");

defm : BufferAtomicPat<"atomic_swap_global", Ty, "BUFFER_ATOMIC_SWAP" # Suffix>;
defm : BufferAtomicPat<"atomic_load_add_global", Ty, "BUFFER_ATOMIC_ADD" # Suffix>;
defm : BufferAtomicPat<"atomic_load_sub_global", Ty, "BUFFER_ATOMIC_SUB" # Suffix>;
defm : BufferAtomicPat<"atomic_load_min_global", Ty, "BUFFER_ATOMIC_SMIN" # Suffix>;
defm : BufferAtomicPat<"atomic_load_umin_global", Ty, "BUFFER_ATOMIC_UMIN" # Suffix>;
defm : BufferAtomicPat<"atomic_load_max_global", Ty, "BUFFER_ATOMIC_SMAX" # Suffix>;
defm : BufferAtomicPat<"atomic_load_umax_global", Ty, "BUFFER_ATOMIC_UMAX" # Suffix>;
defm : BufferAtomicPat<"atomic_load_and_global", Ty, "BUFFER_ATOMIC_AND" # Suffix>;
defm : BufferAtomicPat<"atomic_load_or_global", Ty, "BUFFER_ATOMIC_OR" # Suffix>;
defm : BufferAtomicPat<"atomic_load_xor_global", Ty, "BUFFER_ATOMIC_XOR" # Suffix>;
defm : BufferAtomicPat<"atomic_load_uinc_wrap_global", Ty, "BUFFER_ATOMIC_INC" # Suffix>;
defm : BufferAtomicPat<"atomic_load_udec_wrap_global", Ty, "BUFFER_ATOMIC_DEC" # Suffix>;

} // end foreach Ty

let SubtargetPredicate = HasAtomicFMinFMaxF32GlobalInsts in {
defm : BufferAtomicPat<"atomic_load_fmin_global", f32, "BUFFER_ATOMIC_FMIN">;
defm : BufferAtomicPat<"atomic_load_fmax_global", f32, "BUFFER_ATOMIC_FMAX">;
}

let SubtargetPredicate = HasAtomicFMinFMaxF64GlobalInsts in {
defm : BufferAtomicPat<"atomic_load_fmin_global", f64, "BUFFER_ATOMIC_MIN_F64">;
defm : BufferAtomicPat<"atomic_load_fmax_global", f64, "BUFFER_ATOMIC_MAX_F64">;
}

defm : BufferAtomicCmpSwapPat<i32, v2i32, "BUFFER_ATOMIC_CMPSWAP">;
defm : BufferAtomicCmpSwapPat<i64, v2i64, "BUFFER_ATOMIC_CMPSWAP_X2">;

multiclass SIBufferAtomicPat_Common<string OpPrefix, ValueType vt, string Inst,
                             list<string> RtnModes = ["ret", "noret"]> {
  foreach RtnMode = RtnModes in {

  defvar Op = !cast<SDPatternOperator>(OpPrefix
                                       # !if(!eq(RtnMode, "ret"), "", "_noret"));

  defvar InstSuffix = !if(!eq(RtnMode, "ret"), "_RTN", "");
  defvar CachePolicy = !if(!eq(RtnMode, "ret"),
    (extract_cpol_set_glc $auxiliary), (extract_cpol $auxiliary));

  let AddedComplexity = !if(!eq(RtnMode, "ret"), 0, 1) in {
  def : GCNPat<
    (vt (Op vt:$vdata_in, v4i32:$rsrc, 0, 0, (BUFSOffset i32:$soffset),
              timm:$offset, timm:$auxiliary, 0)),
    (!cast<MUBUF_Pseudo>(Inst # "_OFFSET" # InstSuffix)
      getVregSrcForVT<vt>.ret:$vdata_in, SReg_128:$rsrc, SCSrc_b32:$soffset,
      timm:$offset, CachePolicy)
  >;

  def : GCNPat<
    (vt (Op vt:$vdata_in, v4i32:$rsrc, i32:$vindex, 0, (BUFSOffset i32:$soffset),
              timm:$offset, timm:$auxiliary, timm)),
    (!cast<MUBUF_Pseudo>(Inst # "_IDXEN" # InstSuffix)
      getVregSrcForVT<vt>.ret:$vdata_in, VGPR_32:$vindex, SReg_128:$rsrc,
      SCSrc_b32:$soffset, timm:$offset, CachePolicy)
  >;

  def : GCNPat<
    (vt (Op vt:$vdata_in, v4i32:$rsrc, 0, i32:$voffset,
              (BUFSOffset i32:$soffset), timm:$offset, timm:$auxiliary, 0)),
    (!cast<MUBUF_Pseudo>(Inst # "_OFFEN" # InstSuffix)
      getVregSrcForVT<vt>.ret:$vdata_in, VGPR_32:$voffset, SReg_128:$rsrc,
      SCSrc_b32:$soffset, timm:$offset, CachePolicy)
  >;

  def : GCNPat<
    (vt (Op vt:$vdata_in, v4i32:$rsrc, i32:$vindex, i32:$voffset,
              (BUFSOffset i32:$soffset), timm:$offset, timm:$auxiliary, timm)),
    (!cast<MUBUF_Pseudo>(Inst # "_BOTHEN" # InstSuffix)
      getVregSrcForVT<vt>.ret:$vdata_in,
      (REG_SEQUENCE VReg_64, VGPR_32:$vindex, sub0, VGPR_32:$voffset, sub1),
        SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset, CachePolicy)
  >;
  } // end let AddedComplexity

  } // end foreach RtnMode
}

multiclass SIBufferAtomicPat<string OpPrefix, ValueType vt, string Inst,
                             list<string> RtnModes = ["ret", "noret"]> {
  let OtherPredicates = [HasUnrestrictedSOffset] in {
    defm : SIBufferAtomicPat_Common<OpPrefix, vt, Inst, RtnModes>;
  }

  // FIXME: This needs a !HasUnrestrictedSOffset predicate
  defm : SIBufferAtomicPat_Common<OpPrefix, vt, Inst # "_VBUFFER", RtnModes>;
}

defm : SIBufferAtomicPat<"SIbuffer_atomic_swap", i32, "BUFFER_ATOMIC_SWAP">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_swap", f32, "BUFFER_ATOMIC_SWAP">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_add", i32, "BUFFER_ATOMIC_ADD">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_sub", i32, "BUFFER_ATOMIC_SUB">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_smin", i32, "BUFFER_ATOMIC_SMIN">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_umin", i32, "BUFFER_ATOMIC_UMIN">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_smax", i32, "BUFFER_ATOMIC_SMAX">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_umax", i32, "BUFFER_ATOMIC_UMAX">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_and", i32, "BUFFER_ATOMIC_AND">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_or", i32, "BUFFER_ATOMIC_OR">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_xor", i32, "BUFFER_ATOMIC_XOR">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_inc", i32, "BUFFER_ATOMIC_INC">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_dec", i32, "BUFFER_ATOMIC_DEC">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_csub", i32, "BUFFER_ATOMIC_CSUB", ["ret"]>;
defm : SIBufferAtomicPat<"SIbuffer_atomic_swap", i64, "BUFFER_ATOMIC_SWAP_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_add", i64,  "BUFFER_ATOMIC_ADD_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_sub", i64, "BUFFER_ATOMIC_SUB_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_smin", i64, "BUFFER_ATOMIC_SMIN_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_umin", i64, "BUFFER_ATOMIC_UMIN_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_smax", i64, "BUFFER_ATOMIC_SMAX_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_umax", i64, "BUFFER_ATOMIC_UMAX_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_and", i64, "BUFFER_ATOMIC_AND_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_or", i64, "BUFFER_ATOMIC_OR_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_xor", i64, "BUFFER_ATOMIC_XOR_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_inc", i64, "BUFFER_ATOMIC_INC_X2">;
defm : SIBufferAtomicPat<"SIbuffer_atomic_dec", i64, "BUFFER_ATOMIC_DEC_X2">;

let SubtargetPredicate = HasAtomicCSubNoRtnInsts in
defm : SIBufferAtomicPat<"SIbuffer_atomic_csub", i32, "BUFFER_ATOMIC_CSUB", ["noret"]>;

let SubtargetPredicate = HasAtomicBufferPkAddBF16Inst in {
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fadd", v2bf16, "BUFFER_ATOMIC_PK_ADD_BF16">;
}

let SubtargetPredicate = isGFX12Plus in {
  defm : SIBufferAtomicPat_Common<"SIbuffer_atomic_cond_sub_u32", i32, "BUFFER_ATOMIC_COND_SUB_U32_VBUFFER", ["ret"]>;
}

let SubtargetPredicate = HasAtomicCSubNoRtnInsts in {
defm : SIBufferAtomicPat_Common<"SIbuffer_atomic_cond_sub_u32", i32, "BUFFER_ATOMIC_COND_SUB_U32_VBUFFER", ["noret"]>;
}

let SubtargetPredicate = HasAtomicFMinFMaxF32GlobalInsts in {
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fmin", f32, "BUFFER_ATOMIC_FMIN">;
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fmax", f32, "BUFFER_ATOMIC_FMAX">;
}

let SubtargetPredicate = HasAtomicFMinFMaxF64GlobalInsts in {
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fmin", f64, "BUFFER_ATOMIC_MIN_F64">;
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fmax", f64, "BUFFER_ATOMIC_MAX_F64">;
}

class NoUseBufferAtomic<SDPatternOperator Op, ValueType vt> : PatFrag <
  (ops node:$src0, node:$src1, node:$src2, node:$src3, node:$src4, node:$src5, node:$src6, node:$src7),
  (vt (Op $src0, $src1, $src2, $src3, $src4, $src5, $src6, $src7))> {
  let HasNoUse = true;
}

multiclass BufferAtomicPatterns_NO_RTN_Common<SDPatternOperator name, ValueType vt,
                                       string opcode> {
  def : GCNPat<
    (NoUseBufferAtomic<name, vt> vt:$vdata_in, v4i32:$rsrc, 0,
                                 0, (BUFSOffset i32:$soffset), timm:$offset,
                                 timm:$auxiliary, 0),
    (!cast<MUBUF_Pseudo>(opcode # _OFFSET) getVregSrcForVT<vt>.ret:$vdata_in, SReg_128:$rsrc, SCSrc_b32:$soffset,
                                          timm:$offset, (extract_cpol $auxiliary))
  >;

  def : GCNPat<
    (NoUseBufferAtomic<name, vt> vt:$vdata_in, v4i32:$rsrc, i32:$vindex,
                                 0, (BUFSOffset i32:$soffset), timm:$offset,
                                 timm:$auxiliary, timm),
    (!cast<MUBUF_Pseudo>(opcode # _IDXEN) getVregSrcForVT<vt>.ret:$vdata_in, VGPR_32:$vindex, SReg_128:$rsrc, SCSrc_b32:$soffset,
                                          timm:$offset, (extract_cpol $auxiliary))
  >;

  def : GCNPat<
    (NoUseBufferAtomic<name, vt> vt:$vdata_in, v4i32:$rsrc, 0,
                                 i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
                                 timm:$auxiliary, 0),
    (!cast<MUBUF_Pseudo>(opcode # _OFFEN) getVregSrcForVT<vt>.ret:$vdata_in, VGPR_32:$voffset, SReg_128:$rsrc, SCSrc_b32:$soffset,
                                          timm:$offset, (extract_cpol $auxiliary))
  >;

  def : GCNPat<
    (NoUseBufferAtomic<name, vt> vt:$vdata_in, v4i32:$rsrc, i32:$vindex,
                                 i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
                                 timm:$auxiliary, timm),
    (!cast<MUBUF_Pseudo>(opcode # _BOTHEN)
      getVregSrcForVT<vt>.ret:$vdata_in,
      (REG_SEQUENCE VReg_64, VGPR_32:$vindex, sub0, VGPR_32:$voffset, sub1),
      SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset, (extract_cpol $auxiliary))
  >;
}

multiclass BufferAtomicPatterns_NO_RTN<SDPatternOperator name, ValueType vt,
                                       string opcode> {
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : BufferAtomicPatterns_NO_RTN_Common<name, vt, opcode>;
  }
  defm : BufferAtomicPatterns_NO_RTN_Common<name, vt, opcode # "_VBUFFER">;
}

let SubtargetPredicate = HasAtomicFaddNoRtnInsts in
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fadd", f32, "BUFFER_ATOMIC_ADD_F32", ["noret"]>;

let SubtargetPredicate = HasAtomicBufferGlobalPkAddF16NoRtnInsts in {
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fadd", v2f16, "BUFFER_ATOMIC_PK_ADD_F16", ["noret"]>;
} // End SubtargetPredicate = HasAtomicBufferGlobalPkAddF16NoRtnInsts

let SubtargetPredicate = HasAtomicFaddRtnInsts in
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fadd", f32, "BUFFER_ATOMIC_ADD_F32", ["ret"]>;

let SubtargetPredicate = HasAtomicBufferGlobalPkAddF16Insts in {
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fadd", v2f16, "BUFFER_ATOMIC_PK_ADD_F16", ["ret"]>;
} // End SubtargetPredicate = HasAtomicBufferGlobalPkAddF16Insts

let SubtargetPredicate = HasFlatBufferGlobalAtomicFaddF64Inst in {
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fadd", f64, "BUFFER_ATOMIC_ADD_F64">;
} // End SubtargetPredicate = HasFlatBufferGlobalAtomicFaddF64Inst

let SubtargetPredicate = HasAtomicFMinFMaxF64GlobalInsts in {
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fmin", f64, "BUFFER_ATOMIC_MIN_F64">;
  defm : SIBufferAtomicPat<"SIbuffer_atomic_fmax", f64, "BUFFER_ATOMIC_MAX_F64">;
} //End let SubtargetPredicate = HasAtomicFMinFMaxF64GlobalInsts

multiclass SIBufferAtomicCmpSwapPat_Common<ValueType vt, ValueType data_vt, string Inst> {
  foreach RtnMode = ["ret", "noret"] in {
    defvar Op = !cast<SDPatternOperator>(SIbuffer_atomic_cmpswap
                                         # !if(!eq(RtnMode, "ret"), "", "_noret"));
    defvar InstSuffix = !if(!eq(RtnMode, "ret"), "_RTN", "");
    defvar CachePolicy = !if(!eq(RtnMode, "ret"),
      (extract_cpol_set_glc $auxiliary),
      (extract_cpol $auxiliary));
    defvar SrcRC = getVregSrcForVT<vt>.ret;
    defvar DataRC = getVregSrcForVT<data_vt>.ret.RegClass;
    defvar SubLo = !if(!eq(vt, i32), sub0, sub0_sub1);
    defvar SubHi = !if(!eq(vt, i32), sub1, sub2_sub3);

    defvar OffsetResDag = (!cast<MUBUF_Pseudo>(Inst # "_OFFSET" # InstSuffix)
      (REG_SEQUENCE DataRC, SrcRC:$data, SubLo, SrcRC:$cmp, SubHi),
      SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset, CachePolicy);
    def : GCNPat<
      (vt (Op
          vt:$data, vt:$cmp, v4i32:$rsrc, 0, 0, (BUFSOffset i32:$soffset),
          timm:$offset, timm:$auxiliary, 0)),
      !if(!eq(RtnMode, "ret"),
        (EXTRACT_SUBREG OffsetResDag, SubLo),
        OffsetResDag)
    >;

    defvar IdxenResDag = (!cast<MUBUF_Pseudo>(Inst # "_IDXEN" # InstSuffix)
      (REG_SEQUENCE DataRC, SrcRC:$data, SubLo, SrcRC:$cmp, SubHi),
      VGPR_32:$vindex, SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      CachePolicy);
    def : GCNPat<
      (vt (Op
          vt:$data, vt:$cmp, v4i32:$rsrc, i32:$vindex,
          0, (BUFSOffset i32:$soffset), timm:$offset,
          timm:$auxiliary, timm)),
      !if(!eq(RtnMode, "ret"),
        (EXTRACT_SUBREG IdxenResDag, SubLo),
        IdxenResDag)
    >;

    defvar OffenResDag = (!cast<MUBUF_Pseudo>(Inst # "_OFFEN" # InstSuffix)
      (REG_SEQUENCE DataRC, SrcRC:$data, SubLo, SrcRC:$cmp, SubHi),
      VGPR_32:$voffset, SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      CachePolicy);
    def : GCNPat<
      (vt (Op
          vt:$data, vt:$cmp, v4i32:$rsrc, 0,
          i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
          timm:$auxiliary, 0)),
      !if(!eq(RtnMode, "ret"),
        (EXTRACT_SUBREG OffenResDag, SubLo),
        OffenResDag)
    >;

    defvar BothenResDag = (!cast<MUBUF_Pseudo>(Inst # "_BOTHEN" # InstSuffix)
      (REG_SEQUENCE DataRC, SrcRC:$data, SubLo, SrcRC:$cmp, SubHi),
      (REG_SEQUENCE VReg_64, VGPR_32:$vindex, sub0, VGPR_32:$voffset, sub1),
      SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset, CachePolicy);
    def : GCNPat<
      (vt (Op
          vt:$data, vt:$cmp, v4i32:$rsrc, i32:$vindex,
          i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
          timm:$auxiliary, timm)),
      !if(!eq(RtnMode, "ret"),
        (EXTRACT_SUBREG BothenResDag, SubLo),
        BothenResDag)
    >;
  } // end foreach RtnMode
}

multiclass SIBufferAtomicCmpSwapPat<ValueType vt, ValueType data_vt, string Inst> {
  let OtherPredicates = [HasUnrestrictedSOffset] in {
    defm : SIBufferAtomicCmpSwapPat_Common<vt, data_vt, Inst>;
  }
  defm : SIBufferAtomicCmpSwapPat_Common<vt, data_vt, Inst # "_VBUFFER">;
}

defm : SIBufferAtomicCmpSwapPat<i32, v2i32, "BUFFER_ATOMIC_CMPSWAP">;
defm : SIBufferAtomicCmpSwapPat<i64, v2i64, "BUFFER_ATOMIC_CMPSWAP_X2">;

class MUBUFLoad_PatternADDR64 <MUBUF_Pseudo Instr_ADDR64, ValueType vt,
                              PatFrag constant_ld> : GCNPat <
     (vt (constant_ld (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset,
                                   i32:$offset))),
     (Instr_ADDR64 $vaddr, $srsrc, $soffset, $offset)
  >;

multiclass MUBUFLoad_Atomic_Pattern <MUBUF_Pseudo Instr_ADDR64, MUBUF_Pseudo Instr_OFFSET,
                                     ValueType vt, PatFrag atomic_ld> {
  def : GCNPat <
     (vt (atomic_ld (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset, i32:$offset))),
     (Instr_ADDR64 $vaddr, $srsrc, $soffset, $offset)
  >;

  def : GCNPat <
    (vt (atomic_ld (MUBUFOffset v4i32:$rsrc, i32:$soffset, i32:$offset))),
    (Instr_OFFSET $rsrc, $soffset, (as_i16imm $offset))
  >;
}

let SubtargetPredicate = isGFX6GFX7 in {
def : MUBUFLoad_PatternADDR64 <BUFFER_LOAD_SBYTE_ADDR64, i32, sextloadi8_constant>;
def : MUBUFLoad_PatternADDR64 <BUFFER_LOAD_UBYTE_ADDR64, i32, extloadi8_constant>;
def : MUBUFLoad_PatternADDR64 <BUFFER_LOAD_UBYTE_ADDR64, i32, zextloadi8_constant>;
def : MUBUFLoad_PatternADDR64 <BUFFER_LOAD_SSHORT_ADDR64, i32, sextloadi16_constant>;
def : MUBUFLoad_PatternADDR64 <BUFFER_LOAD_USHORT_ADDR64, i32, extloadi16_constant>;
def : MUBUFLoad_PatternADDR64 <BUFFER_LOAD_USHORT_ADDR64, i32, zextloadi16_constant>;

defm : MUBUFLoad_Atomic_Pattern <BUFFER_LOAD_DWORD_ADDR64, BUFFER_LOAD_DWORD_OFFSET, i32, atomic_load_32_global>;
defm : MUBUFLoad_Atomic_Pattern <BUFFER_LOAD_DWORDX2_ADDR64, BUFFER_LOAD_DWORDX2_OFFSET, i64, atomic_load_64_global>;
} // End SubtargetPredicate = isGFX6GFX7

multiclass MUBUFLoad_PatternOffset_Common <string Instr, ValueType vt,
                               PatFrag ld> {
  def : GCNPat <
    (vt (ld (MUBUFOffset v4i32:$srsrc, i32:$soffset, i32:$offset))),
    (!cast<MUBUF_Pseudo>(Instr # "_OFFSET") $srsrc, $soffset, $offset)
  >;
}

multiclass MUBUFLoad_PatternOffset <string Instr, ValueType vt,
                                    PatFrag ld> {
  let OtherPredicates = [HasUnrestrictedSOffset] in {
    defm : MUBUFLoad_PatternOffset_Common<Instr, vt, ld>;
  }
  defm : MUBUFLoad_PatternOffset_Common<Instr # "_VBUFFER", vt, ld>;
}

let OtherPredicates = [Has16BitInsts] in {

defm : MUBUFLoad_PatternOffset <"BUFFER_LOAD_SBYTE", i16, sextloadi8_constant>;
defm : MUBUFLoad_PatternOffset <"BUFFER_LOAD_UBYTE", i16, extloadi8_constant>;
defm : MUBUFLoad_PatternOffset <"BUFFER_LOAD_UBYTE", i16, zextloadi8_constant>;
defm : MUBUFLoad_PatternOffset <"BUFFER_LOAD_SBYTE", i16, sextloadi8_global>;
defm : MUBUFLoad_PatternOffset <"BUFFER_LOAD_UBYTE", i16, extloadi8_global>;
defm : MUBUFLoad_PatternOffset <"BUFFER_LOAD_UBYTE", i16, zextloadi8_global>;

defm : MUBUFLoad_PatternOffset <"BUFFER_LOAD_USHORT", i16, load_global>;

} // End OtherPredicates = [Has16BitInsts]

multiclass MUBUFScratchLoadPat_Common <string Instr,
                                ValueType vt, PatFrag ld> {
  def : GCNPat <
    (vt (ld (MUBUFScratchOffen v4i32:$srsrc, i32:$vaddr,
                               i32:$soffset, i32:$offset))),
    (!cast<MUBUF_Pseudo>(Instr # _OFFEN) $vaddr, $srsrc, $soffset, $offset, 0, 0)
  >;

  def : GCNPat <
    (vt (ld (MUBUFScratchOffset v4i32:$srsrc, i32:$soffset, i32:$offset))),
    (!cast<MUBUF_Pseudo>(Instr # _OFFSET) $srsrc, $soffset, $offset, 0, 0)
  >;
}

multiclass MUBUFScratchLoadPat <string Instr,
                                ValueType vt, PatFrag ld> {
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : MUBUFScratchLoadPat_Common<Instr, vt, ld>;
  }
  defm : MUBUFScratchLoadPat_Common<Instr # "_VBUFFER", vt, ld>;
}

// XXX - Is it possible to have a complex pattern in a PatFrag?
multiclass MUBUFScratchLoadPat_D16_Common <string Instr,
                                ValueType vt, PatFrag ld_frag> {
  def : GCNPat <
    (ld_frag (MUBUFScratchOffen v4i32:$srsrc, i32:$vaddr, i32:$soffset, i32:$offset), vt:$in),
    (!cast<MUBUF_Pseudo>(Instr # _OFFEN) $vaddr, $srsrc, $soffset, $offset, $in)
  >;

  def : GCNPat <
    (ld_frag (MUBUFScratchOffset v4i32:$srsrc, i32:$soffset, i32:$offset), vt:$in),
    (!cast<MUBUF_Pseudo>(Instr # _OFFSET) $srsrc, $soffset, $offset, $in)
  >;
}

multiclass MUBUFScratchLoadPat_D16 <string Instr,
                                ValueType vt, PatFrag ld_frag> {
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : MUBUFScratchLoadPat_D16_Common<Instr, vt, ld_frag>;
  }
  defm : MUBUFScratchLoadPat_D16_Common<Instr # "_VBUFFER", vt, ld_frag>;
}

let OtherPredicates = [DisableFlatScratch] in {
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_SBYTE", i32, sextloadi8_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_UBYTE", i32, extloadi8_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_UBYTE", i32, zextloadi8_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_SBYTE", i16, sextloadi8_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_UBYTE", i16, extloadi8_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_UBYTE", i16, zextloadi8_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_SSHORT", i32, sextloadi16_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_USHORT", i32, extloadi16_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_USHORT", i32, zextloadi16_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_USHORT", i16, load_private>;

foreach vt = Reg32Types.types in {
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_DWORD", vt, load_private>;
}
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_DWORDX2", v2i32, load_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_DWORDX3", v3i32, load_private>;
defm : MUBUFScratchLoadPat <"BUFFER_LOAD_DWORDX4", v4i32, load_private>;

let OtherPredicates = [D16PreservesUnusedBits, DisableFlatScratch] in {
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_SHORT_D16_HI", v2i16, load_d16_hi_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_UBYTE_D16_HI", v2i16, az_extloadi8_d16_hi_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_SBYTE_D16_HI", v2i16, sextloadi8_d16_hi_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_SHORT_D16_HI", v2f16, load_d16_hi_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_UBYTE_D16_HI", v2f16, az_extloadi8_d16_hi_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_SBYTE_D16_HI", v2f16, sextloadi8_d16_hi_private>;

defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_SHORT_D16", v2i16, load_d16_lo_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_UBYTE_D16", v2i16, az_extloadi8_d16_lo_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_SBYTE_D16", v2i16, sextloadi8_d16_lo_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_SHORT_D16", v2f16, load_d16_lo_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_UBYTE_D16", v2f16, az_extloadi8_d16_lo_private>;
defm : MUBUFScratchLoadPat_D16<"BUFFER_LOAD_SBYTE_D16", v2f16, sextloadi8_d16_lo_private>;
}

} // End OtherPredicates = [DisableFlatScratch]

multiclass MUBUFStore_Atomic_Pattern <MUBUF_Pseudo Instr_ADDR64, MUBUF_Pseudo Instr_OFFSET,
                                      ValueType vt, PatFrag atomic_st> {
  def : GCNPat <
     (atomic_st vt:$val, (MUBUFAddr64 v4i32:$srsrc, i64:$vaddr, i32:$soffset, i32:$offset)),
     (Instr_ADDR64 $val, $vaddr, $srsrc, $soffset, $offset)
  >;

  def : GCNPat <
    (atomic_st vt:$val, (MUBUFOffset v4i32:$rsrc, i32:$soffset, i32:$offset)),
    (Instr_OFFSET $val, $rsrc, $soffset, (as_i16imm $offset))
  >;
}
let SubtargetPredicate = isGFX6GFX7 in {
defm : MUBUFStore_Atomic_Pattern <BUFFER_STORE_BYTE_ADDR64, BUFFER_STORE_BYTE_OFFSET, i32, atomic_store_8_global>;
defm : MUBUFStore_Atomic_Pattern <BUFFER_STORE_BYTE_ADDR64, BUFFER_STORE_BYTE_OFFSET, i16, atomic_store_8_global>;
defm : MUBUFStore_Atomic_Pattern <BUFFER_STORE_SHORT_ADDR64, BUFFER_STORE_SHORT_OFFSET, i32, atomic_store_16_global>;
defm : MUBUFStore_Atomic_Pattern <BUFFER_STORE_SHORT_ADDR64, BUFFER_STORE_SHORT_OFFSET, i16, atomic_store_16_global>;
defm : MUBUFStore_Atomic_Pattern <BUFFER_STORE_DWORD_ADDR64, BUFFER_STORE_DWORD_OFFSET, i32, atomic_store_32_global>;
defm : MUBUFStore_Atomic_Pattern <BUFFER_STORE_DWORDX2_ADDR64, BUFFER_STORE_DWORDX2_OFFSET, i64, atomic_store_64_global>;
} // End Predicates = isGFX6GFX7


multiclass MUBUFStore_PatternOffset_Common <string Instr, ValueType vt,
                                     PatFrag st> {

  def : GCNPat <
    (st vt:$vdata, (MUBUFOffset v4i32:$srsrc, i32:$soffset, i32:$offset)),
    (!cast<MUBUF_Pseudo>(Instr # "_OFFSET") $vdata, $srsrc, $soffset, $offset)
  >;
}

multiclass MUBUFStore_PatternOffset <string Instr, ValueType vt,
                                     PatFrag st> {
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : MUBUFStore_PatternOffset_Common<Instr, vt, st>;
  }
  defm : MUBUFStore_PatternOffset_Common<Instr # "_VBUFFER", vt, st>;
}

defm : MUBUFStore_PatternOffset <"BUFFER_STORE_BYTE", i16, truncstorei8_global>;
defm : MUBUFStore_PatternOffset <"BUFFER_STORE_SHORT", i16, store_global>;

multiclass MUBUFScratchStorePat_Common <string Instr,
                                 ValueType vt, PatFrag st,
                                 RegisterClass rc = VGPR_32> {
  def : GCNPat <
    (st vt:$value, (MUBUFScratchOffen v4i32:$srsrc, i32:$vaddr,
                                      i32:$soffset, i32:$offset)),
    (!cast<MUBUF_Pseudo>(Instr # _OFFEN) rc:$value, $vaddr, $srsrc, $soffset, $offset, 0, 0)
  >;

  def : GCNPat <
    (st vt:$value, (MUBUFScratchOffset v4i32:$srsrc, i32:$soffset,
                                       i32:$offset)),
    (!cast<MUBUF_Pseudo>(Instr # _OFFSET) rc:$value, $srsrc, $soffset, $offset, 0, 0)
  >;
}

multiclass MUBUFScratchStorePat <string Instr,
                                 ValueType vt, PatFrag st,
                                 RegisterClass rc = VGPR_32> {
  let SubtargetPredicate = HasUnrestrictedSOffset in {
    defm : MUBUFScratchStorePat_Common<Instr, vt, st, rc>;
  }
  defm : MUBUFScratchStorePat_Common<Instr # "_VBUFFER", vt, st, rc>;
}

let OtherPredicates = [DisableFlatScratch] in {
defm : MUBUFScratchStorePat <"BUFFER_STORE_BYTE", i32, truncstorei8_private>;
defm : MUBUFScratchStorePat <"BUFFER_STORE_SHORT", i32, truncstorei16_private>;
defm : MUBUFScratchStorePat <"BUFFER_STORE_BYTE", i16, truncstorei8_private>;
defm : MUBUFScratchStorePat <"BUFFER_STORE_SHORT", i16, store_private>;

foreach vt = Reg32Types.types in {
defm : MUBUFScratchStorePat <"BUFFER_STORE_DWORD", vt, store_private>;
}

defm : MUBUFScratchStorePat <"BUFFER_STORE_DWORDX2", v2i32, store_private, VReg_64>;
defm : MUBUFScratchStorePat <"BUFFER_STORE_DWORDX3", v3i32, store_private, VReg_96>;
defm : MUBUFScratchStorePat <"BUFFER_STORE_DWORDX4", v4i32, store_private, VReg_128>;


let OtherPredicates = [HasD16LoadStore, DisableFlatScratch] in {
 // Hiding the extract high pattern in the PatFrag seems to not
 // automatically increase the complexity.
let AddedComplexity = 1 in {
defm : MUBUFScratchStorePat <"BUFFER_STORE_SHORT_D16_HI", i32, store_hi16_private>;
defm : MUBUFScratchStorePat <"BUFFER_STORE_BYTE_D16_HI", i32, truncstorei8_hi16_private>;
}
}
} // End OtherPredicates = [DisableFlatScratch]

//===----------------------------------------------------------------------===//
// MTBUF Patterns
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// tbuffer_load/store_format patterns
//===----------------------------------------------------------------------===//

multiclass MTBUF_LoadIntrinsicPat_Common<SDPatternOperator name, ValueType vt,
                                  string opcode, ValueType memoryVt = vt> {
  defvar st = !if(!eq(memoryVt, vt), name, mtbuf_intrinsic_load<name, memoryVt>);

  def : GCNPat<
    (vt (st v4i32:$rsrc, 0, 0, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$format, timm:$auxiliary, 0)),
    (!cast<MTBUF_Pseudo>(opcode # _OFFSET) SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (as_i8timm $format),
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (vt (st v4i32:$rsrc, i32:$vindex, 0, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$format, timm:$auxiliary, timm)),
    (!cast<MTBUF_Pseudo>(opcode # _IDXEN) VGPR_32:$vindex, SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (as_i8timm $format),
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (vt (st v4i32:$rsrc, 0, i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$format, timm:$auxiliary, 0)),
    (!cast<MTBUF_Pseudo>(opcode # _OFFEN) VGPR_32:$voffset, SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (as_i8timm $format),
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (vt (st v4i32:$rsrc, i32:$vindex, i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
              timm:$format, timm:$auxiliary, timm)),
    (!cast<MTBUF_Pseudo>(opcode # _BOTHEN)
      (REG_SEQUENCE VReg_64, VGPR_32:$vindex, sub0, VGPR_32:$voffset, sub1),
      SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset,
      (as_i8timm $format),
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;
}

multiclass MTBUF_LoadIntrinsicPat<SDPatternOperator name, ValueType vt,
                                  string opcode, ValueType memoryVt = vt> {
  let OtherPredicates = [HasUnrestrictedSOffset] in {
    defm : MTBUF_LoadIntrinsicPat_Common<name, vt, opcode, memoryVt>;
  }
  defm : MTBUF_LoadIntrinsicPat_Common<name, vt, opcode # "_VBUFFER", memoryVt>;
}

defm : MTBUF_LoadIntrinsicPat<SItbuffer_load, i32,   "TBUFFER_LOAD_FORMAT_X">;
defm : MTBUF_LoadIntrinsicPat<SItbuffer_load, v2i32, "TBUFFER_LOAD_FORMAT_XY">;
defm : MTBUF_LoadIntrinsicPat<SItbuffer_load, v3i32, "TBUFFER_LOAD_FORMAT_XYZ">;
defm : MTBUF_LoadIntrinsicPat<SItbuffer_load, v4i32, "TBUFFER_LOAD_FORMAT_XYZW">;
defm : MTBUF_LoadIntrinsicPat<SItbuffer_load, f32,   "TBUFFER_LOAD_FORMAT_X">;
defm : MTBUF_LoadIntrinsicPat<SItbuffer_load, v2f32, "TBUFFER_LOAD_FORMAT_XY">;
defm : MTBUF_LoadIntrinsicPat<SItbuffer_load, v3f32, "TBUFFER_LOAD_FORMAT_XYZ">;
defm : MTBUF_LoadIntrinsicPat<SItbuffer_load, v4f32, "TBUFFER_LOAD_FORMAT_XYZW">;

let SubtargetPredicate = HasUnpackedD16VMem in {
  defm : MTBUF_LoadIntrinsicPat_Common<SItbuffer_load_d16, f16,   "TBUFFER_LOAD_FORMAT_D16_X_gfx80">;
  defm : MTBUF_LoadIntrinsicPat_Common<SItbuffer_load_d16, i32,   "TBUFFER_LOAD_FORMAT_D16_X_gfx80">;
  defm : MTBUF_LoadIntrinsicPat_Common<SItbuffer_load_d16, v2i32, "TBUFFER_LOAD_FORMAT_D16_XY_gfx80">;
  defm : MTBUF_LoadIntrinsicPat_Common<SItbuffer_load_d16, v3i32, "TBUFFER_LOAD_FORMAT_D16_XYZ_gfx80">;
  defm : MTBUF_LoadIntrinsicPat_Common<SItbuffer_load_d16, v4i32, "TBUFFER_LOAD_FORMAT_D16_XYZW_gfx80">;
} // End HasUnpackedD16VMem.

let SubtargetPredicate = HasPackedD16VMem in {
  defm : MTBUF_LoadIntrinsicPat<SItbuffer_load_d16, f16,   "TBUFFER_LOAD_FORMAT_D16_X">;
  defm : MTBUF_LoadIntrinsicPat<SItbuffer_load_d16, i32,   "TBUFFER_LOAD_FORMAT_D16_X">;
  defm : MTBUF_LoadIntrinsicPat<SItbuffer_load_d16, v2f16, "TBUFFER_LOAD_FORMAT_D16_XY">;
  defm : MTBUF_LoadIntrinsicPat<SItbuffer_load_d16, v4f16, "TBUFFER_LOAD_FORMAT_D16_XYZ", v3f16>;
  defm : MTBUF_LoadIntrinsicPat<SItbuffer_load_d16, v4f16, "TBUFFER_LOAD_FORMAT_D16_XYZW">;
} // End HasPackedD16VMem.

multiclass MTBUF_StoreIntrinsicPat_Common<SDPatternOperator name, ValueType vt,
                                        string opcode, ValueType memoryVt = vt> {
  defvar st = !if(!eq(memoryVt, vt), name, mtbuf_intrinsic_store<name, memoryVt>);

  def : GCNPat<
    (st vt:$vdata, v4i32:$rsrc, 0, 0, (BUFSOffset i32:$soffset), timm:$offset,
          timm:$format, timm:$auxiliary, 0),
    (!cast<MTBUF_Pseudo>(opcode # _OFFSET_exact) getVregSrcForVT<vt>.ret:$vdata, SReg_128:$rsrc, SCSrc_b32:$soffset,
      timm:$offset, (as_i8timm $format),
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (st vt:$vdata, v4i32:$rsrc, i32:$vindex, 0, (BUFSOffset i32:$soffset), timm:$offset,
          timm:$format, timm:$auxiliary, timm),
    (!cast<MTBUF_Pseudo>(opcode # _IDXEN_exact) getVregSrcForVT<vt>.ret:$vdata, VGPR_32:$vindex, SReg_128:$rsrc, SCSrc_b32:$soffset,
      timm:$offset, (as_i8timm $format),
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (st vt:$vdata, v4i32:$rsrc, 0, i32:$voffset, (BUFSOffset i32:$soffset), timm:$offset,
          timm:$format, timm:$auxiliary, 0),
    (!cast<MTBUF_Pseudo>(opcode # _OFFEN_exact) getVregSrcForVT<vt>.ret:$vdata, VGPR_32:$voffset, SReg_128:$rsrc, SCSrc_b32:$soffset,
      timm:$offset, (as_i8timm $format),
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;

  def : GCNPat<
    (st vt:$vdata, v4i32:$rsrc, i32:$vindex, i32:$voffset, (BUFSOffset i32:$soffset),
          timm:$offset, timm:$format, timm:$auxiliary, timm),
    (!cast<MTBUF_Pseudo>(opcode # _BOTHEN_exact)
      getVregSrcForVT<vt>.ret:$vdata,
      (REG_SEQUENCE VReg_64, VGPR_32:$vindex, sub0, VGPR_32:$voffset, sub1),
      SReg_128:$rsrc, SCSrc_b32:$soffset, timm:$offset, (as_i8timm $format),
      (extract_cpol $auxiliary), (extract_swz $auxiliary))
  >;
}

multiclass MTBUF_StoreIntrinsicPat<SDPatternOperator name, ValueType vt,
                                  string opcode, ValueType memoryVt = vt> {
  let OtherPredicates = [HasUnrestrictedSOffset] in {
    defm : MTBUF_StoreIntrinsicPat_Common<name, vt, opcode, memoryVt>;
  }
  defm : MTBUF_StoreIntrinsicPat_Common<name, vt, opcode # "_VBUFFER", memoryVt>;
}

defm : MTBUF_StoreIntrinsicPat<SItbuffer_store, i32,   "TBUFFER_STORE_FORMAT_X">;
defm : MTBUF_StoreIntrinsicPat<SItbuffer_store, v2i32, "TBUFFER_STORE_FORMAT_XY">;
defm : MTBUF_StoreIntrinsicPat<SItbuffer_store, v3i32, "TBUFFER_STORE_FORMAT_XYZ">;
defm : MTBUF_StoreIntrinsicPat<SItbuffer_store, v4i32, "TBUFFER_STORE_FORMAT_XYZW">;
defm : MTBUF_StoreIntrinsicPat<SItbuffer_store, f32,   "TBUFFER_STORE_FORMAT_X">;
defm : MTBUF_StoreIntrinsicPat<SItbuffer_store, v2f32, "TBUFFER_STORE_FORMAT_XY">;
defm : MTBUF_StoreIntrinsicPat<SItbuffer_store, v3f32, "TBUFFER_STORE_FORMAT_XYZ">;
defm : MTBUF_StoreIntrinsicPat<SItbuffer_store, v4f32, "TBUFFER_STORE_FORMAT_XYZW">;

let SubtargetPredicate = HasUnpackedD16VMem in {
  defm : MTBUF_StoreIntrinsicPat_Common<SItbuffer_store_d16, f16,   "TBUFFER_STORE_FORMAT_D16_X_gfx80">;
  defm : MTBUF_StoreIntrinsicPat_Common<SItbuffer_store_d16, i32,   "TBUFFER_STORE_FORMAT_D16_X_gfx80">;
  defm : MTBUF_StoreIntrinsicPat_Common<SItbuffer_store_d16, v2i32, "TBUFFER_STORE_FORMAT_D16_XY_gfx80">;
  defm : MTBUF_StoreIntrinsicPat_Common<SItbuffer_store_d16, v3i32, "TBUFFER_STORE_FORMAT_D16_XYZ_gfx80">;
  defm : MTBUF_StoreIntrinsicPat_Common<SItbuffer_store_d16, v4i32, "TBUFFER_STORE_FORMAT_D16_XYZW_gfx80">;
} // End HasUnpackedD16VMem.

let SubtargetPredicate = HasPackedD16VMem in {
  defm : MTBUF_StoreIntrinsicPat<SItbuffer_store_d16, f16,   "TBUFFER_STORE_FORMAT_D16_X">;
  defm : MTBUF_StoreIntrinsicPat<SItbuffer_store_d16, i32,   "TBUFFER_STORE_FORMAT_D16_X">;
  defm : MTBUF_StoreIntrinsicPat<SItbuffer_store_d16, v2f16, "TBUFFER_STORE_FORMAT_D16_XY">;
  defm : MTBUF_StoreIntrinsicPat<SItbuffer_store_d16, v4f16, "TBUFFER_STORE_FORMAT_D16_XYZ", v3f16>;
  defm : MTBUF_StoreIntrinsicPat<SItbuffer_store_d16, v4f16, "TBUFFER_STORE_FORMAT_D16_XYZW">;
} // End HasPackedD16VMem.

//===----------------------------------------------------------------------===//
// Target-specific instruction encodings.
//===----------------------------------------------------------------------===//

// Shortcut to default Mnemonic from BUF_Pseudo. Hides the cast to the
// specific pseudo (bothen in this case) since any of them will work.
class get_BUF_ps<string name> {
  string Mnemonic = !cast<BUF_Pseudo>(name # "_OFFSET").Mnemonic;
}

//===----------------------------------------------------------------------===//
// Base ENC_MUBUF for GFX6, GFX7, GFX10, GFX11.
//===----------------------------------------------------------------------===//

class Base_MUBUF_Real_gfx6_gfx7_gfx10_gfx11 <MUBUF_Pseudo ps, int ef,
                                             string real_name = ps.Mnemonic> :
  MUBUF_Real<ps, real_name>, Enc64, SIMCInstr<ps.PseudoInstr, ef> {
  let Inst{11-0}  = !if(ps.has_offset, offset, ?);
  let Inst{31-26} = 0x38;
  let Inst{39-32} = !if(ps.has_vaddr, vaddr, ?);
  let Inst{47-40} = !if(ps.has_vdata, vdata{7-0}, ?);
  let Inst{52-48} = !if(ps.has_srsrc, srsrc{6-2}, ?);
  let Inst{63-56} = !if(ps.has_soffset, soffset, ?);
}

multiclass MUBUF_Real_gfx11<bits<8> op, string real_name = !cast<MUBUF_Pseudo>(NAME).Mnemonic> {
  defvar ps = !cast<MUBUF_Pseudo>(NAME);
  def _gfx11 : Base_MUBUF_Real_gfx6_gfx7_gfx10_gfx11<ps, SIEncodingFamily.GFX11, real_name> {
    let Inst{12}    = !if(ps.has_slc, cpol{CPolBit.SLC}, ?);
    // In GFX11 dlc is applicable to all loads/stores/atomics.
    let Inst{13}    = !if(!or(ps.mayLoad, ps.mayStore), cpol{CPolBit.DLC}, ps.dlc_value);
    let Inst{14}    = !if(ps.has_glc, cpol{CPolBit.GLC}, ps.glc_value);
    let Inst{25-18} = op;
    let Inst{53}    = ps.tfe;
    let Inst{54}    = ps.offen;
    let Inst{55}    = ps.idxen;
    let AssemblerPredicate = isGFX11Only;
    let DecoderNamespace = "GFX11";
  }
}

class Base_MUBUF_Real_gfx6_gfx7_gfx10<bits<7> op, MUBUF_Pseudo ps, int ef, string asmName> :
  Base_MUBUF_Real_gfx6_gfx7_gfx10_gfx11<ps, ef, asmName> {
  let Inst{12}    = ps.offen;
  let Inst{13}    = ps.idxen;
  let Inst{14}    = !if(ps.has_glc, cpol{CPolBit.GLC}, ps.glc_value);
  let Inst{16}    = ps.lds;
  let Inst{24-18} = op;
  let Inst{54}    = !if(ps.has_slc, cpol{CPolBit.SLC}, ?);
  let Inst{55}    = ps.tfe;
}

multiclass MUBUF_Real_gfx10<bits<8> op, string psName = NAME,
                            string asmName = !cast<MUBUF_Pseudo>(psName).Mnemonic> {
  defvar ps = !cast<MUBUF_Pseudo>(psName);
  def _gfx10 : Base_MUBUF_Real_gfx6_gfx7_gfx10<op{6-0}, ps, SIEncodingFamily.GFX10, asmName> {
    let Inst{15} = !if(ps.has_dlc, cpol{CPolBit.DLC}, ps.dlc_value);
    let Inst{25} = op{7};
    let AssemblerPredicate = isGFX10Only;
    let DecoderNamespace = "GFX10";
  }
}

multiclass MUBUF_Real_gfx6_gfx7<bits<8> op, string psName = NAME,
                                string asmName = !cast<MUBUF_Pseudo>(psName).Mnemonic> {
  defvar ps = !cast<MUBUF_Pseudo>(psName);
  def _gfx6_gfx7 : Base_MUBUF_Real_gfx6_gfx7_gfx10<op{6-0}, ps, SIEncodingFamily.SI, asmName> {
    let Inst{15} = ps.addr64;
    let AssemblerPredicate = isGFX6GFX7;
    let DecoderNamespace = "GFX6GFX7";
  }
}

multiclass MUBUF_Real_gfx6<bits<8> op> {
  defvar ps = !cast<MUBUF_Pseudo>(NAME);
  def _gfx6 : Base_MUBUF_Real_gfx6_gfx7_gfx10<op{6-0}, ps, SIEncodingFamily.SI, ps.Mnemonic> {
    let Inst{15} = ps.addr64;
    let AssemblerPredicate = isGFX6;
    let DecoderNamespace = "GFX6";
  }
}

multiclass MUBUF_Real_gfx7<bits<8> op> {
  defvar ps = !cast<MUBUF_Pseudo>(NAME);
  def _gfx7 : Base_MUBUF_Real_gfx6_gfx7_gfx10<op{6-0}, ps, SIEncodingFamily.SI, ps.Mnemonic> {
    let Inst{15} = ps.addr64;
    let AssemblerPredicate = isGFX7Only;
    let DecoderNamespace = "GFX7";
  }
}

//===----------------------------------------------------------------------===//
// Base ENC_VBUFFER for GFX12.
//===----------------------------------------------------------------------===//

class VBUFFER_Real <bits<8> op, BUF_Pseudo ps, string real_name> :
  InstSI <ps.OutOperandList, ps.InOperandList, real_name # ps.AsmOperands, []>, Enc96 {

  let isPseudo = 0;
  let isCodeGenOnly = 0;

  let VM_CNT = 1;
  let EXP_CNT = 1;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;
  let OtherPredicates    = ps.OtherPredicates;
  let Constraints        = ps.Constraints;
  let DisableEncoding    = ps.DisableEncoding;
  let TSFlags            = ps.TSFlags;
  let UseNamedOperandTable = ps.UseNamedOperandTable;
  let SchedRW            = ps.SchedRW;
  let mayLoad            = ps.mayLoad;
  let mayStore           = ps.mayStore;
  let IsAtomicRet        = ps.IsAtomicRet;
  let IsAtomicNoRet      = ps.IsAtomicNoRet;
  let VALU               = ps.VALU;
  let LGKM_CNT           = ps.LGKM_CNT;
  let MUBUF              = ps.MUBUF;
  let MTBUF              = ps.MTBUF;
  let Uses               = ps.Uses;
  let Defs               = ps.Defs;

  bits<24> offset;
  bits<8>  vaddr;
  bits<10> vdata;

  bits<7>  srsrc;
  bits<7>  soffset;
  bits<6>  cpol;

  let Inst{95-72} = !if(ps.has_offset, offset, ?);
  let Inst{71-64} = !if(ps.has_vaddr, vaddr, ?);
  let Inst{39-32} = !if(ps.has_vdata, vdata{7-0}, ?);

  let Inst{47-41} = !if(ps.has_srsrc, srsrc, ?);
  let Inst{49-48} = 0b00;
  let Inst{6-0}   = !if(ps.has_soffset, soffset, ?);
  let Inst{21-14} = op;
  let Inst{22}    = ps.tfe;
  let Inst{62}    = ps.offen;
  let Inst{63}    = ps.idxen;

  let Inst{54-53} = cpol{2-1}; // th{2-1}
  let Inst{52}    = !if(ps.IsAtomicRet, 1, cpol{0}); // th{0}
  let Inst{51-50} = cpol{4-3}; // scope

  let Inst{31-26} = 0b110001;
}

class VBUFFER_Real_gfx12<bits<8> op, BUF_Pseudo ps, string real_name> :
    VBUFFER_Real<op, ps, real_name>,
    SIMCInstr<ps.PseudoInstr, SIEncodingFamily.GFX12> {
  let AssemblerPredicate = isGFX12Only;
  let DecoderNamespace = "GFX12";
}

multiclass VBUFFER_MUBUF_Real_gfx12<bits<8> op, string real_name> {
  defvar ps = !cast<MUBUF_Pseudo>(NAME);
  def _gfx12 : VBUFFER_Real_gfx12<op, ps, real_name> {
    // Set the format field to be 1 to avoid round-trip issues, as some tools
    // print BUF_FMT_INVALID for format 0.
    let Inst{61-55} = 0b0000001;
  }
  // Have a version of the instruction to disassemble to for any other
  // format field values.
  def _gfx12_format : VBUFFER_Real<op, ps, real_name> {
    let AsmVariantName = "NonParsable";
    let DecoderNamespace = "GFX12";
  }
}

multiclass VBUFFER_MTBUF_Real_gfx12<bits<4> op, string real_name> {
  defvar ps = !cast<MTBUF_Pseudo>(NAME);
  def _gfx12 : VBUFFER_Real_gfx12<{0b1000, op}, ps, real_name> {
    bits<7> format;
    let Inst{61-55} = format;
  }
}

//===----------------------------------------------------------------------===//
// MUBUF - GFX11, GFX12.
//===----------------------------------------------------------------------===//

// gfx11 instruction that accept both old and new assembler name.
class Mnem_gfx11_gfx12 <string mnemonic, string real_name> :
    AMDGPUMnemonicAlias<mnemonic, real_name> {
  let AssemblerPredicate = isGFX11Plus;
}

class Mnem_gfx11 <string mnemonic, string real_name> :
    AMDGPUMnemonicAlias<mnemonic, real_name> {
  let AssemblerPredicate = isGFX11Only;
}

class Mnem_gfx12 <string mnemonic, string real_name> :
    AMDGPUMnemonicAlias<mnemonic, real_name> {
  let AssemblerPredicate = isGFX12Plus;
}

multiclass MUBUF_Real_AllAddr_gfx11_Impl2<bits<8> op, string real_name> {
  defm _BOTHEN : MUBUF_Real_gfx11<op, real_name>;
  defm _IDXEN  : MUBUF_Real_gfx11<op, real_name>;
  defm _OFFEN  : MUBUF_Real_gfx11<op, real_name>;
  defm _OFFSET : MUBUF_Real_gfx11<op, real_name>;
}

multiclass MUBUF_Real_AllAddr_gfx12_Impl2<bits<8> op, string real_name> {
  defm _VBUFFER_BOTHEN : VBUFFER_MUBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_IDXEN  : VBUFFER_MUBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_OFFEN  : VBUFFER_MUBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_OFFSET : VBUFFER_MUBUF_Real_gfx12<op, real_name>;
}

multiclass MUBUF_Real_AllAddr_gfx11_gfx12_Impl2<bits<8> op, string real_name> :
  MUBUF_Real_AllAddr_gfx11_Impl2<op, real_name>,
  MUBUF_Real_AllAddr_gfx12_Impl2<op, real_name>;

multiclass MUBUF_Real_AllAddr_gfx11_Impl<bits<8> op, bit hasTFE,
                                 string real_name = get_BUF_ps<NAME>.Mnemonic> {
  defm NAME : MUBUF_Real_AllAddr_gfx11_Impl2<op, real_name>;
  if hasTFE then
    defm _TFE : MUBUF_Real_AllAddr_gfx11_Impl2<op, real_name>;
}

multiclass MUBUF_Real_AllAddr_gfx11_gfx12_Impl<bits<8> op, bit hasTFE,
                                               string real_name> {
  defm NAME : MUBUF_Real_AllAddr_gfx11_gfx12_Impl2<op, real_name>;
  if hasTFE then
    defm _TFE : MUBUF_Real_AllAddr_gfx11_gfx12_Impl2<op, real_name>;
}

// Non-renamed, non-atomic gfx11/gfx12 mubuf instructions.
multiclass MUBUF_Real_AllAddr_gfx11<bits<8> op, bit hasTFE = 1> :
  MUBUF_Real_AllAddr_gfx11_Impl<op, hasTFE>;

multiclass MUBUF_Real_AllAddr_gfx11_gfx12<bits<8> op,
                                 string real_name = get_BUF_ps<NAME>.Mnemonic> :
  MUBUF_Real_AllAddr_gfx11_gfx12_Impl<op, /*hasTFE=*/1, real_name> {
  defvar ps = get_BUF_ps<NAME>;
  if !ne(ps.Mnemonic, real_name) then
    def : Mnem_gfx11_gfx12<ps.Mnemonic, real_name>;
}

multiclass MUBUF_Real_Atomic_gfx11_impl<bits<8> op, bit is_return,
                                                string real_name> {
  defvar Rtn = !if(is_return, "_RTN", "");
  defm _BOTHEN#Rtn : MUBUF_Real_gfx11<op, real_name>;
  defm _IDXEN#Rtn  : MUBUF_Real_gfx11<op, real_name>;
  defm _OFFEN#Rtn  : MUBUF_Real_gfx11<op, real_name>;
  defm _OFFSET#Rtn : MUBUF_Real_gfx11<op, real_name>;
}

multiclass MUBUF_Real_Atomic_gfx12_impl<bits<8> op, bit is_return,
                                 string real_name = get_BUF_ps<NAME>.Mnemonic> {
  defvar Rtn = !if(is_return, "_RTN", "");
  defm _VBUFFER_BOTHEN#Rtn : VBUFFER_MUBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_IDXEN#Rtn  : VBUFFER_MUBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_OFFEN#Rtn  : VBUFFER_MUBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_OFFSET#Rtn : VBUFFER_MUBUF_Real_gfx12<op, real_name>;
}

multiclass MUBUF_Real_Atomic_gfx11_gfx12_impl<bits<8> op, bit is_return,
                                                string real_name> :
  MUBUF_Real_Atomic_gfx11_impl<op, is_return, real_name>,
  MUBUF_Real_Atomic_gfx12_impl<op, is_return, real_name>;

multiclass MUBUF_Real_Atomic_gfx12<bits<8> op> :
  MUBUF_Real_Atomic_gfx12_impl<op, 0>,
  MUBUF_Real_Atomic_gfx12_impl<op, 1>;

multiclass MUBUF_Real_Atomic_gfx11<bits<8> op, string real_name> :
  MUBUF_Real_Atomic_gfx11_impl<op, 0, real_name>,
  MUBUF_Real_Atomic_gfx11_impl<op, 1, real_name> {
  defvar ps = get_BUF_ps<NAME>;
  def : Mnem_gfx11_gfx12<ps.Mnemonic, real_name>;
}

multiclass MUBUF_Real_Atomic_gfx11_gfx12<bits<8> op,
                                  string gfx12_name = get_BUF_ps<NAME>.Mnemonic,
                                  string gfx11_name = gfx12_name> :
  MUBUF_Real_Atomic_gfx11_impl<op, 0, gfx11_name>,
  MUBUF_Real_Atomic_gfx11_impl<op, 1, gfx11_name>,
  MUBUF_Real_Atomic_gfx12_impl<op, 0, gfx12_name>,
  MUBUF_Real_Atomic_gfx12_impl<op, 1, gfx12_name> {
  defvar ps = get_BUF_ps<NAME>;
  if !ne(ps.Mnemonic, gfx11_name) then
    def : Mnem_gfx11<ps.Mnemonic, gfx11_name>;
  if !ne(ps.Mnemonic, gfx12_name) then
    def : Mnem_gfx12<ps.Mnemonic, gfx12_name>;
  if !ne(gfx11_name, gfx12_name) then
    def : Mnem_gfx12<gfx11_name, gfx12_name>;
}

defm BUFFER_GL0_INV               : MUBUF_Real_gfx11<0x02B>;
defm BUFFER_GL1_INV               : MUBUF_Real_gfx11<0x02C>;

defm BUFFER_LOAD_DWORD            : MUBUF_Real_AllAddr_gfx11_gfx12<0x014, "buffer_load_b32">;
defm BUFFER_LOAD_DWORDX2          : MUBUF_Real_AllAddr_gfx11_gfx12<0x015, "buffer_load_b64">;
defm BUFFER_LOAD_DWORDX3          : MUBUF_Real_AllAddr_gfx11_gfx12<0x016, "buffer_load_b96">;
defm BUFFER_LOAD_DWORDX4          : MUBUF_Real_AllAddr_gfx11_gfx12<0x017, "buffer_load_b128">;
defm BUFFER_LOAD_SHORT_D16        : MUBUF_Real_AllAddr_gfx11_gfx12<0x020, "buffer_load_d16_b16">;
defm BUFFER_LOAD_FORMAT_D16_X     : MUBUF_Real_AllAddr_gfx11_gfx12<0x008, "buffer_load_d16_format_x">;
defm BUFFER_LOAD_FORMAT_D16_XY    : MUBUF_Real_AllAddr_gfx11_gfx12<0x009, "buffer_load_d16_format_xy">;
defm BUFFER_LOAD_FORMAT_D16_XYZ   : MUBUF_Real_AllAddr_gfx11_gfx12<0x00a, "buffer_load_d16_format_xyz">;
defm BUFFER_LOAD_FORMAT_D16_XYZW  : MUBUF_Real_AllAddr_gfx11_gfx12<0x00b, "buffer_load_d16_format_xyzw">;
defm BUFFER_LOAD_SHORT_D16_HI     : MUBUF_Real_AllAddr_gfx11_gfx12<0x023, "buffer_load_d16_hi_b16">;
defm BUFFER_LOAD_FORMAT_D16_HI_X  : MUBUF_Real_AllAddr_gfx11_gfx12<0x026, "buffer_load_d16_hi_format_x">;
defm BUFFER_LOAD_SBYTE_D16_HI     : MUBUF_Real_AllAddr_gfx11_gfx12<0x022, "buffer_load_d16_hi_i8">;
defm BUFFER_LOAD_UBYTE_D16_HI     : MUBUF_Real_AllAddr_gfx11_gfx12<0x021, "buffer_load_d16_hi_u8">;
defm BUFFER_LOAD_SBYTE_D16        : MUBUF_Real_AllAddr_gfx11_gfx12<0x01f, "buffer_load_d16_i8">;
defm BUFFER_LOAD_UBYTE_D16        : MUBUF_Real_AllAddr_gfx11_gfx12<0x01e, "buffer_load_d16_u8">;
defm BUFFER_LOAD_FORMAT_X         : MUBUF_Real_AllAddr_gfx11_gfx12<0x000>;
defm BUFFER_LOAD_FORMAT_XY        : MUBUF_Real_AllAddr_gfx11_gfx12<0x001>;
defm BUFFER_LOAD_FORMAT_XYZ       : MUBUF_Real_AllAddr_gfx11_gfx12<0x002>;
defm BUFFER_LOAD_FORMAT_XYZW      : MUBUF_Real_AllAddr_gfx11_gfx12<0x003>;
defm BUFFER_LOAD_SBYTE            : MUBUF_Real_AllAddr_gfx11_gfx12<0x011, "buffer_load_i8">;
defm BUFFER_LOAD_SSHORT           : MUBUF_Real_AllAddr_gfx11_gfx12<0x013, "buffer_load_i16">;
defm BUFFER_LOAD_UBYTE            : MUBUF_Real_AllAddr_gfx11_gfx12<0x010, "buffer_load_u8">;
defm BUFFER_LOAD_USHORT           : MUBUF_Real_AllAddr_gfx11_gfx12<0x012, "buffer_load_u16">;
defm BUFFER_LOAD_LDS_B32          : MUBUF_Real_AllAddr_gfx11<0x031, 0>;
defm BUFFER_LOAD_LDS_FORMAT_X     : MUBUF_Real_AllAddr_gfx11<0x032, 0>;
defm BUFFER_LOAD_LDS_I8           : MUBUF_Real_AllAddr_gfx11<0x02e, 0>;
defm BUFFER_LOAD_LDS_I16          : MUBUF_Real_AllAddr_gfx11<0x030, 0>;
defm BUFFER_LOAD_LDS_U8           : MUBUF_Real_AllAddr_gfx11<0x02d, 0>;
defm BUFFER_LOAD_LDS_U16          : MUBUF_Real_AllAddr_gfx11<0x02f, 0>;
defm BUFFER_STORE_BYTE            : MUBUF_Real_AllAddr_gfx11_gfx12<0x018, "buffer_store_b8">;
defm BUFFER_STORE_SHORT           : MUBUF_Real_AllAddr_gfx11_gfx12<0x019, "buffer_store_b16">;
defm BUFFER_STORE_DWORD           : MUBUF_Real_AllAddr_gfx11_gfx12<0x01A, "buffer_store_b32">;
defm BUFFER_STORE_DWORDX2         : MUBUF_Real_AllAddr_gfx11_gfx12<0x01B, "buffer_store_b64">;
defm BUFFER_STORE_DWORDX3         : MUBUF_Real_AllAddr_gfx11_gfx12<0x01C, "buffer_store_b96">;
defm BUFFER_STORE_DWORDX4         : MUBUF_Real_AllAddr_gfx11_gfx12<0x01D, "buffer_store_b128">;
defm BUFFER_STORE_FORMAT_D16_X    : MUBUF_Real_AllAddr_gfx11_gfx12<0x00C, "buffer_store_d16_format_x">;
defm BUFFER_STORE_FORMAT_D16_XY   : MUBUF_Real_AllAddr_gfx11_gfx12<0x00D, "buffer_store_d16_format_xy">;
defm BUFFER_STORE_FORMAT_D16_XYZ  : MUBUF_Real_AllAddr_gfx11_gfx12<0x00E, "buffer_store_d16_format_xyz">;
defm BUFFER_STORE_FORMAT_D16_XYZW : MUBUF_Real_AllAddr_gfx11_gfx12<0x00F, "buffer_store_d16_format_xyzw">;
defm BUFFER_STORE_BYTE_D16_HI     : MUBUF_Real_AllAddr_gfx11_gfx12<0x024, "buffer_store_d16_hi_b8">;
defm BUFFER_STORE_SHORT_D16_HI    : MUBUF_Real_AllAddr_gfx11_gfx12<0x025, "buffer_store_d16_hi_b16">;
defm BUFFER_STORE_FORMAT_D16_HI_X : MUBUF_Real_AllAddr_gfx11_gfx12<0x027, "buffer_store_d16_hi_format_x">;
defm BUFFER_STORE_FORMAT_X        : MUBUF_Real_AllAddr_gfx11_gfx12<0x004>;
defm BUFFER_STORE_FORMAT_XY       : MUBUF_Real_AllAddr_gfx11_gfx12<0x005>;
defm BUFFER_STORE_FORMAT_XYZ      : MUBUF_Real_AllAddr_gfx11_gfx12<0x006>;
defm BUFFER_STORE_FORMAT_XYZW     : MUBUF_Real_AllAddr_gfx11_gfx12<0x007>;
defm BUFFER_ATOMIC_ADD_F32        : MUBUF_Real_Atomic_gfx11_gfx12<0x056>;
defm BUFFER_ATOMIC_ADD            : MUBUF_Real_Atomic_gfx11_gfx12<0x035, "buffer_atomic_add_u32">;
defm BUFFER_ATOMIC_ADD_X2         : MUBUF_Real_Atomic_gfx11_gfx12<0x043, "buffer_atomic_add_u64">;
defm BUFFER_ATOMIC_AND            : MUBUF_Real_Atomic_gfx11_gfx12<0x03C, "buffer_atomic_and_b32">;
defm BUFFER_ATOMIC_AND_X2         : MUBUF_Real_Atomic_gfx11_gfx12<0x049, "buffer_atomic_and_b64">;
defm BUFFER_ATOMIC_CMPSWAP        : MUBUF_Real_Atomic_gfx11_gfx12<0x034, "buffer_atomic_cmpswap_b32">;
defm BUFFER_ATOMIC_CMPSWAP_X2     : MUBUF_Real_Atomic_gfx11_gfx12<0x042, "buffer_atomic_cmpswap_b64">;
defm BUFFER_ATOMIC_FCMPSWAP       : MUBUF_Real_Atomic_gfx11<0x050, "buffer_atomic_cmpswap_f32">;
defm BUFFER_ATOMIC_COND_SUB_U32   : MUBUF_Real_Atomic_gfx12<0x050>;
defm BUFFER_ATOMIC_CSUB           : MUBUF_Real_Atomic_gfx11_gfx12<0x037, "buffer_atomic_sub_clamp_u32", "buffer_atomic_csub_u32">;
defm BUFFER_ATOMIC_DEC            : MUBUF_Real_Atomic_gfx11_gfx12<0x040, "buffer_atomic_dec_u32">;
defm BUFFER_ATOMIC_DEC_X2         : MUBUF_Real_Atomic_gfx11_gfx12<0x04D, "buffer_atomic_dec_u64">;
defm BUFFER_ATOMIC_INC            : MUBUF_Real_Atomic_gfx11_gfx12<0x03F, "buffer_atomic_inc_u32">;
defm BUFFER_ATOMIC_INC_X2         : MUBUF_Real_Atomic_gfx11_gfx12<0x04C, "buffer_atomic_inc_u64">;
defm BUFFER_ATOMIC_FMAX           : MUBUF_Real_Atomic_gfx11_gfx12<0x052, "buffer_atomic_max_num_f32", "buffer_atomic_max_f32">;
defm BUFFER_ATOMIC_SMAX           : MUBUF_Real_Atomic_gfx11_gfx12<0x03A, "buffer_atomic_max_i32">;
defm BUFFER_ATOMIC_SMAX_X2        : MUBUF_Real_Atomic_gfx11_gfx12<0x047, "buffer_atomic_max_i64">;
defm BUFFER_ATOMIC_UMAX           : MUBUF_Real_Atomic_gfx11_gfx12<0x03B, "buffer_atomic_max_u32">;
defm BUFFER_ATOMIC_UMAX_X2        : MUBUF_Real_Atomic_gfx11_gfx12<0x048, "buffer_atomic_max_u64">;
defm BUFFER_ATOMIC_FMIN           : MUBUF_Real_Atomic_gfx11_gfx12<0x051, "buffer_atomic_min_num_f32", "buffer_atomic_min_f32">;
defm BUFFER_ATOMIC_SMIN           : MUBUF_Real_Atomic_gfx11_gfx12<0x038, "buffer_atomic_min_i32">;
defm BUFFER_ATOMIC_SMIN_X2        : MUBUF_Real_Atomic_gfx11_gfx12<0x045, "buffer_atomic_min_i64">;
defm BUFFER_ATOMIC_UMIN           : MUBUF_Real_Atomic_gfx11_gfx12<0x039, "buffer_atomic_min_u32">;
defm BUFFER_ATOMIC_UMIN_X2        : MUBUF_Real_Atomic_gfx11_gfx12<0x046, "buffer_atomic_min_u64">;
defm BUFFER_ATOMIC_OR             : MUBUF_Real_Atomic_gfx11_gfx12<0x03D, "buffer_atomic_or_b32">;
defm BUFFER_ATOMIC_OR_X2          : MUBUF_Real_Atomic_gfx11_gfx12<0x04A, "buffer_atomic_or_b64">;
defm BUFFER_ATOMIC_SUB            : MUBUF_Real_Atomic_gfx11_gfx12<0x036, "buffer_atomic_sub_u32">;
defm BUFFER_ATOMIC_SUB_X2         : MUBUF_Real_Atomic_gfx11_gfx12<0x044, "buffer_atomic_sub_u64">;
defm BUFFER_ATOMIC_SWAP           : MUBUF_Real_Atomic_gfx11_gfx12<0x033, "buffer_atomic_swap_b32">;
defm BUFFER_ATOMIC_SWAP_X2        : MUBUF_Real_Atomic_gfx11_gfx12<0x041, "buffer_atomic_swap_b64">;
defm BUFFER_ATOMIC_XOR            : MUBUF_Real_Atomic_gfx11_gfx12<0x03E, "buffer_atomic_xor_b32">;
defm BUFFER_ATOMIC_XOR_X2         : MUBUF_Real_Atomic_gfx11_gfx12<0x04B, "buffer_atomic_xor_b64">;
defm BUFFER_ATOMIC_PK_ADD_F16     : MUBUF_Real_Atomic_gfx12<0x059>;
defm BUFFER_ATOMIC_PK_ADD_BF16    : MUBUF_Real_Atomic_gfx12<0x05a>;

//===----------------------------------------------------------------------===//
// MUBUF - GFX10.
//===----------------------------------------------------------------------===//

multiclass MUBUF_Real_AllAddr_Helper_gfx10<bits<8> op> {
  defm _BOTHEN : MUBUF_Real_gfx10<op>;
  defm _IDXEN  : MUBUF_Real_gfx10<op>;
  defm _OFFEN  : MUBUF_Real_gfx10<op>;
  defm _OFFSET : MUBUF_Real_gfx10<op>;
}
multiclass MUBUF_Real_AllAddr_gfx10<bits<8> op> {
  defm NAME : MUBUF_Real_AllAddr_Helper_gfx10<op>;
  defm _TFE : MUBUF_Real_AllAddr_Helper_gfx10<op>;
}
multiclass MUBUF_Real_AllAddr_Lds_gfx10<bits<8> op, bit isTFE = 0> {
  defm _OFFSET : MUBUF_Real_gfx10<op>;
  defm _OFFEN  : MUBUF_Real_gfx10<op>;
  defm _IDXEN  : MUBUF_Real_gfx10<op>;
  defm _BOTHEN : MUBUF_Real_gfx10<op>;

  if !not(isTFE) then {
    defm _LDS_OFFSET : MUBUF_Real_gfx10<op>;
    defm _LDS_OFFEN  : MUBUF_Real_gfx10<op>;
    defm _LDS_IDXEN  : MUBUF_Real_gfx10<op>;
    defm _LDS_BOTHEN : MUBUF_Real_gfx10<op>;
  }
}
multiclass MUBUF_Real_Atomics_RTN_gfx10<bits<8> op, string psName = NAME,
                                        string asmName = !cast<MUBUF_Pseudo>(psName).Mnemonic> {
  defm _BOTHEN_RTN : MUBUF_Real_gfx10<op, psName#"_BOTHEN_RTN", asmName>;
  defm _IDXEN_RTN  : MUBUF_Real_gfx10<op, psName#"_IDXEN_RTN", asmName>;
  defm _OFFEN_RTN  : MUBUF_Real_gfx10<op, psName#"_OFFEN_RTN", asmName>;
  defm _OFFSET_RTN : MUBUF_Real_gfx10<op, psName#"_OFFSET_RTN", asmName>;
}
multiclass MUBUF_Real_Atomics_gfx10<bits<8> op, string psName = NAME,
                                    string asmName = get_BUF_ps<psName>.Mnemonic> :
    MUBUF_Real_Atomics_RTN_gfx10<op, psName, asmName> {
  defm _BOTHEN : MUBUF_Real_gfx10<op, psName#"_BOTHEN", asmName>;
  defm _IDXEN  : MUBUF_Real_gfx10<op, psName#"_IDXEN", asmName>;
  defm _OFFEN  : MUBUF_Real_gfx10<op, psName#"_OFFEN", asmName>;
  defm _OFFSET : MUBUF_Real_gfx10<op, psName#"_OFFSET", asmName>;
}

defm BUFFER_STORE_BYTE_D16_HI     : MUBUF_Real_AllAddr_gfx10<0x019>;
defm BUFFER_STORE_SHORT_D16_HI    : MUBUF_Real_AllAddr_gfx10<0x01b>;
defm BUFFER_LOAD_UBYTE_D16        : MUBUF_Real_AllAddr_gfx10<0x020>;
defm BUFFER_LOAD_UBYTE_D16_HI     : MUBUF_Real_AllAddr_gfx10<0x021>;
defm BUFFER_LOAD_SBYTE_D16        : MUBUF_Real_AllAddr_gfx10<0x022>;
defm BUFFER_LOAD_SBYTE_D16_HI     : MUBUF_Real_AllAddr_gfx10<0x023>;
defm BUFFER_LOAD_SHORT_D16        : MUBUF_Real_AllAddr_gfx10<0x024>;
defm BUFFER_LOAD_SHORT_D16_HI     : MUBUF_Real_AllAddr_gfx10<0x025>;
defm BUFFER_LOAD_FORMAT_D16_HI_X  : MUBUF_Real_AllAddr_gfx10<0x026>;
defm BUFFER_STORE_FORMAT_D16_HI_X : MUBUF_Real_AllAddr_gfx10<0x027>;
defm BUFFER_LOAD_FORMAT_D16_X     : MUBUF_Real_AllAddr_gfx10<0x080>;
defm BUFFER_LOAD_FORMAT_D16_XY    : MUBUF_Real_AllAddr_gfx10<0x081>;
defm BUFFER_LOAD_FORMAT_D16_XYZ   : MUBUF_Real_AllAddr_gfx10<0x082>;
defm BUFFER_LOAD_FORMAT_D16_XYZW  : MUBUF_Real_AllAddr_gfx10<0x083>;
defm BUFFER_STORE_FORMAT_D16_X    : MUBUF_Real_AllAddr_gfx10<0x084>;
defm BUFFER_STORE_FORMAT_D16_XY   : MUBUF_Real_AllAddr_gfx10<0x085>;
defm BUFFER_STORE_FORMAT_D16_XYZ  : MUBUF_Real_AllAddr_gfx10<0x086>;
defm BUFFER_STORE_FORMAT_D16_XYZW : MUBUF_Real_AllAddr_gfx10<0x087>;

defm BUFFER_GL0_INV : MUBUF_Real_gfx10<0x071>;
defm BUFFER_GL1_INV : MUBUF_Real_gfx10<0x072>;

//===----------------------------------------------------------------------===//
// MUBUF - GFX6, GFX7, GFX10.
//===----------------------------------------------------------------------===//

multiclass MUBUF_Real_AllAddr_Helper_gfx6_gfx7<bits<8> op> {
  defm _ADDR64 : MUBUF_Real_gfx6_gfx7<op>;
  defm _BOTHEN : MUBUF_Real_gfx6_gfx7<op>;
  defm _IDXEN  : MUBUF_Real_gfx6_gfx7<op>;
  defm _OFFEN  : MUBUF_Real_gfx6_gfx7<op>;
  defm _OFFSET : MUBUF_Real_gfx6_gfx7<op>;
}
multiclass MUBUF_Real_AllAddr_gfx6_gfx7<bits<8> op> {
  defm NAME : MUBUF_Real_AllAddr_Helper_gfx6_gfx7<op>;
  defm _TFE : MUBUF_Real_AllAddr_Helper_gfx6_gfx7<op>;
}
multiclass MUBUF_Real_AllAddr_Lds_gfx6_gfx7<bits<8> op, bit isTFE = 0> {
  defm _OFFSET : MUBUF_Real_gfx6_gfx7<op>;
  defm _ADDR64 : MUBUF_Real_gfx6_gfx7<op>;
  defm _OFFEN  : MUBUF_Real_gfx6_gfx7<op>;
  defm _IDXEN  : MUBUF_Real_gfx6_gfx7<op>;
  defm _BOTHEN : MUBUF_Real_gfx6_gfx7<op>;

  if !not(isTFE) then {
    defm _LDS_OFFSET : MUBUF_Real_gfx6_gfx7<op>;
    defm _LDS_ADDR64 : MUBUF_Real_gfx6_gfx7<op>;
    defm _LDS_OFFEN  : MUBUF_Real_gfx6_gfx7<op>;
    defm _LDS_IDXEN  : MUBUF_Real_gfx6_gfx7<op>;
    defm _LDS_BOTHEN : MUBUF_Real_gfx6_gfx7<op>;
  }
}
multiclass MUBUF_Real_Atomics_gfx6_gfx7<bits<8> op, string psName, string asmName> {
  defm _ADDR64 : MUBUF_Real_gfx6_gfx7<op, psName#"_ADDR64", asmName>;
  defm _BOTHEN : MUBUF_Real_gfx6_gfx7<op, psName#"_BOTHEN", asmName>;
  defm _IDXEN  : MUBUF_Real_gfx6_gfx7<op, psName#"_IDXEN", asmName>;
  defm _OFFEN  : MUBUF_Real_gfx6_gfx7<op, psName#"_OFFEN", asmName>;
  defm _OFFSET : MUBUF_Real_gfx6_gfx7<op, psName#"_OFFSET", asmName>;

  defm _ADDR64_RTN : MUBUF_Real_gfx6_gfx7<op, psName#"_ADDR64_RTN", asmName>;
  defm _BOTHEN_RTN : MUBUF_Real_gfx6_gfx7<op, psName#"_BOTHEN_RTN", asmName>;
  defm _IDXEN_RTN  : MUBUF_Real_gfx6_gfx7<op, psName#"_IDXEN_RTN", asmName>;
  defm _OFFEN_RTN  : MUBUF_Real_gfx6_gfx7<op, psName#"_OFFEN_RTN", asmName>;
  defm _OFFSET_RTN : MUBUF_Real_gfx6_gfx7<op, psName#"_OFFSET_RTN", asmName>;
}

multiclass MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<bits<8> op> :
  MUBUF_Real_AllAddr_gfx6_gfx7<op>, MUBUF_Real_AllAddr_gfx10<op>;

multiclass MUBUF_Real_AllAddr_Lds_Helper_gfx6_gfx7_gfx10<bits<8> op, bit isTFE = 0> :
  MUBUF_Real_AllAddr_Lds_gfx6_gfx7<op, isTFE>,
  MUBUF_Real_AllAddr_Lds_gfx10<op, isTFE>;

multiclass MUBUF_Real_AllAddr_Lds_gfx6_gfx7_gfx10<bits<8> op> {
  defm NAME : MUBUF_Real_AllAddr_Lds_Helper_gfx6_gfx7_gfx10<op>;
  defm _TFE : MUBUF_Real_AllAddr_Lds_Helper_gfx6_gfx7_gfx10<op, 1>;
}

multiclass MUBUF_Real_Atomics_gfx6_gfx7_gfx10<bits<8> op, string psName = NAME,
                                              string asmName = get_BUF_ps<psName>.Mnemonic> :
  MUBUF_Real_Atomics_gfx6_gfx7<op, psName, asmName>,
  MUBUF_Real_Atomics_gfx10<op, psName, asmName>;

// FIXME-GFX6: Following instructions are available only on GFX6.
//defm BUFFER_ATOMIC_RSUB         : MUBUF_Real_Atomics_gfx6 <0x034>;
//defm BUFFER_ATOMIC_RSUB_X2      : MUBUF_Real_Atomics_gfx6 <0x054>;

defm BUFFER_LOAD_FORMAT_X     : MUBUF_Real_AllAddr_Lds_gfx6_gfx7_gfx10<0x000>;
defm BUFFER_LOAD_FORMAT_XY    : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x001>;
defm BUFFER_LOAD_FORMAT_XYZ   : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x002>;
defm BUFFER_LOAD_FORMAT_XYZW  : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x003>;
defm BUFFER_STORE_FORMAT_X    : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x004>;
defm BUFFER_STORE_FORMAT_XY   : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x005>;
defm BUFFER_STORE_FORMAT_XYZ  : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x006>;
defm BUFFER_STORE_FORMAT_XYZW : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x007>;
defm BUFFER_LOAD_UBYTE        : MUBUF_Real_AllAddr_Lds_gfx6_gfx7_gfx10<0x008>;
defm BUFFER_LOAD_SBYTE        : MUBUF_Real_AllAddr_Lds_gfx6_gfx7_gfx10<0x009>;
defm BUFFER_LOAD_USHORT       : MUBUF_Real_AllAddr_Lds_gfx6_gfx7_gfx10<0x00a>;
defm BUFFER_LOAD_SSHORT       : MUBUF_Real_AllAddr_Lds_gfx6_gfx7_gfx10<0x00b>;
defm BUFFER_LOAD_DWORD        : MUBUF_Real_AllAddr_Lds_gfx6_gfx7_gfx10<0x00c>;
defm BUFFER_LOAD_DWORDX2      : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x00d>;
defm BUFFER_LOAD_DWORDX4      : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x00e>;
defm BUFFER_LOAD_DWORDX3      : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x00f>;
defm BUFFER_STORE_BYTE        : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x018>;
defm BUFFER_STORE_SHORT       : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x01a>;
defm BUFFER_STORE_DWORD       : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x01c>;
defm BUFFER_STORE_DWORDX2     : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x01d>;
defm BUFFER_STORE_DWORDX4     : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x01e>;
defm BUFFER_STORE_DWORDX3     : MUBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x01f>;

defm BUFFER_ATOMIC_SWAP        : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x030>;
defm BUFFER_ATOMIC_CMPSWAP     : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x031>;
defm BUFFER_ATOMIC_ADD         : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x032>;
defm BUFFER_ATOMIC_SUB         : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x033>;
defm BUFFER_ATOMIC_SMIN        : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x035>;
defm BUFFER_ATOMIC_UMIN        : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x036>;
defm BUFFER_ATOMIC_SMAX        : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x037>;
defm BUFFER_ATOMIC_UMAX        : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x038>;
defm BUFFER_ATOMIC_AND         : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x039>;
defm BUFFER_ATOMIC_OR          : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x03a>;
defm BUFFER_ATOMIC_XOR         : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x03b>;
defm BUFFER_ATOMIC_INC         : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x03c>;
defm BUFFER_ATOMIC_DEC         : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x03d>;
defm BUFFER_ATOMIC_FCMPSWAP    : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x03e>;
defm BUFFER_ATOMIC_FMIN        : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x03f>;
defm BUFFER_ATOMIC_FMAX        : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x040>;
defm BUFFER_ATOMIC_SWAP_X2     : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x050>;
defm BUFFER_ATOMIC_CMPSWAP_X2  : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x051>;
defm BUFFER_ATOMIC_ADD_X2      : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x052>;
defm BUFFER_ATOMIC_SUB_X2      : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x053>;
defm BUFFER_ATOMIC_SMIN_X2     : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x055>;
defm BUFFER_ATOMIC_UMIN_X2     : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x056>;
defm BUFFER_ATOMIC_SMAX_X2     : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x057>;
defm BUFFER_ATOMIC_UMAX_X2     : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x058>;
defm BUFFER_ATOMIC_AND_X2      : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x059>;
defm BUFFER_ATOMIC_OR_X2       : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x05a>;
defm BUFFER_ATOMIC_XOR_X2      : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x05b>;
defm BUFFER_ATOMIC_INC_X2      : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x05c>;
defm BUFFER_ATOMIC_DEC_X2      : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x05d>;
// FIXME-GFX7: Need to handle hazard for BUFFER_ATOMIC_FCMPSWAP_X2 on GFX7.
defm BUFFER_ATOMIC_FCMPSWAP_X2 : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x05e>;
defm BUFFER_ATOMIC_FMIN_X2     : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x05f, "BUFFER_ATOMIC_MIN_F64", "buffer_atomic_fmin_x2">;
defm BUFFER_ATOMIC_FMAX_X2     : MUBUF_Real_Atomics_gfx6_gfx7_gfx10<0x060, "BUFFER_ATOMIC_MAX_F64", "buffer_atomic_fmax_x2">;

defm BUFFER_ATOMIC_CSUB       : MUBUF_Real_Atomics_gfx10<0x034>;

defm BUFFER_WBINVL1_SC        : MUBUF_Real_gfx6<0x070>;
defm BUFFER_WBINVL1_VOL       : MUBUF_Real_gfx7<0x070>;
defm BUFFER_WBINVL1           : MUBUF_Real_gfx6_gfx7<0x071>;

//===----------------------------------------------------------------------===//
// Base ENC_MTBUF for GFX6, GFX7, GFX10, GFX11.
//===----------------------------------------------------------------------===//

class Base_MTBUF_Real_gfx6_gfx7_gfx10_gfx11<MTBUF_Pseudo ps, int ef,
                                            string real_name = ps.Mnemonic> :
  MTBUF_Real<ps, real_name>, Enc64, SIMCInstr<ps.PseudoInstr, ef> {
  let Inst{11-0}  = !if(ps.has_offset, offset, ?);
  let Inst{14}    = !if(ps.has_glc, cpol{CPolBit.GLC}, ps.glc_value);
  let Inst{31-26} = 0x3a; //encoding
  let Inst{39-32} = !if(ps.has_vaddr, vaddr, ?);
  let Inst{47-40} = !if(ps.has_vdata, vdata{7-0}, ?);
  let Inst{52-48} = !if(ps.has_srsrc, srsrc{6-2}, ?);
  let Inst{63-56} = !if(ps.has_soffset, soffset, ?);
}

multiclass MTBUF_Real_gfx11<bits<4> op, string real_name> {
  defvar ps = !cast<MTBUF_Pseudo>(NAME);
  def _gfx11 : Base_MTBUF_Real_gfx6_gfx7_gfx10_gfx11<ps, SIEncodingFamily.GFX11, real_name> {
    let Inst{12}    = !if(ps.has_slc, cpol{CPolBit.SLC}, ?);
    let Inst{13}    = !if(ps.has_dlc, cpol{CPolBit.DLC}, ps.dlc_value);
    let Inst{18-15} = op;
    let Inst{25-19} = format;
    let Inst{53}    = ps.tfe;
    let Inst{54}    = ps.offen;
    let Inst{55}    = ps.idxen;
    let AssemblerPredicate = isGFX11Only;
    let DecoderNamespace = "GFX11";
  }
}

class Base_MTBUF_Real_gfx6_gfx7_gfx10<bits<3> op, MTBUF_Pseudo ps, int ef> :
  Base_MTBUF_Real_gfx6_gfx7_gfx10_gfx11<ps, ef> {
  let Inst{12}    = ps.offen;
  let Inst{13}    = ps.idxen;
  let Inst{18-16} = op;
  let Inst{54}    = !if(ps.has_slc, cpol{CPolBit.SLC}, ?);
  let Inst{55}    = ps.tfe;
}

//===----------------------------------------------------------------------===//
// MTBUF - GFX11.
//===----------------------------------------------------------------------===//

multiclass MTBUF_Real_AllAddr_gfx11_gfx12_Impl<bits<4> op, string real_name> {
  defm _BOTHEN : MTBUF_Real_gfx11<op, real_name>;
  defm _IDXEN  : MTBUF_Real_gfx11<op, real_name>;
  defm _OFFEN  : MTBUF_Real_gfx11<op, real_name>;
  defm _OFFSET : MTBUF_Real_gfx11<op, real_name>;

  defm _VBUFFER_BOTHEN : VBUFFER_MTBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_IDXEN  : VBUFFER_MTBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_OFFEN  : VBUFFER_MTBUF_Real_gfx12<op, real_name>;
  defm _VBUFFER_OFFSET : VBUFFER_MTBUF_Real_gfx12<op, real_name>;
}

multiclass MTBUF_Real_AllAddr_gfx11_gfx12<bits<4> op,
                                   string real_name = get_BUF_ps<NAME>.Mnemonic>
  : MTBUF_Real_AllAddr_gfx11_gfx12_Impl<op, real_name> {
  defvar ps = get_BUF_ps<NAME>;
  if !ne(ps.Mnemonic, real_name) then
    def : Mnem_gfx11_gfx12<ps.Mnemonic, real_name>;
}

defm TBUFFER_LOAD_FORMAT_D16_X     : MTBUF_Real_AllAddr_gfx11_gfx12<0x008, "tbuffer_load_d16_format_x">;
defm TBUFFER_LOAD_FORMAT_D16_XY    : MTBUF_Real_AllAddr_gfx11_gfx12<0x009, "tbuffer_load_d16_format_xy">;
defm TBUFFER_LOAD_FORMAT_D16_XYZ   : MTBUF_Real_AllAddr_gfx11_gfx12<0x00a, "tbuffer_load_d16_format_xyz">;
defm TBUFFER_LOAD_FORMAT_D16_XYZW  : MTBUF_Real_AllAddr_gfx11_gfx12<0x00b, "tbuffer_load_d16_format_xyzw">;
defm TBUFFER_LOAD_FORMAT_X         : MTBUF_Real_AllAddr_gfx11_gfx12<0x000>;
defm TBUFFER_LOAD_FORMAT_XY        : MTBUF_Real_AllAddr_gfx11_gfx12<0x001>;
defm TBUFFER_LOAD_FORMAT_XYZ       : MTBUF_Real_AllAddr_gfx11_gfx12<0x002>;
defm TBUFFER_LOAD_FORMAT_XYZW      : MTBUF_Real_AllAddr_gfx11_gfx12<0x003>;
defm TBUFFER_STORE_FORMAT_D16_X    : MTBUF_Real_AllAddr_gfx11_gfx12<0x00c, "tbuffer_store_d16_format_x">;
defm TBUFFER_STORE_FORMAT_D16_XY   : MTBUF_Real_AllAddr_gfx11_gfx12<0x00d, "tbuffer_store_d16_format_xy">;
defm TBUFFER_STORE_FORMAT_D16_XYZ  : MTBUF_Real_AllAddr_gfx11_gfx12<0x00e, "tbuffer_store_d16_format_xyz">;
defm TBUFFER_STORE_FORMAT_D16_XYZW : MTBUF_Real_AllAddr_gfx11_gfx12<0x00f, "tbuffer_store_d16_format_xyzw">;
defm TBUFFER_STORE_FORMAT_X        : MTBUF_Real_AllAddr_gfx11_gfx12<0x004>;
defm TBUFFER_STORE_FORMAT_XY       : MTBUF_Real_AllAddr_gfx11_gfx12<0x005>;
defm TBUFFER_STORE_FORMAT_XYZ      : MTBUF_Real_AllAddr_gfx11_gfx12<0x006>;
defm TBUFFER_STORE_FORMAT_XYZW     : MTBUF_Real_AllAddr_gfx11_gfx12<0x007>;

//===----------------------------------------------------------------------===//
// MTBUF - GFX10.
//===----------------------------------------------------------------------===//

multiclass MTBUF_Real_gfx10<bits<4> op> {
  defvar ps = !cast<MTBUF_Pseudo>(NAME);
  def _gfx10 : Base_MTBUF_Real_gfx6_gfx7_gfx10<op{2-0}, ps, SIEncodingFamily.GFX10> {
    let Inst{15} = !if(ps.has_dlc, cpol{CPolBit.DLC}, ps.dlc_value);
    let Inst{25-19} = format;
    let Inst{53} = op{3};
    let AssemblerPredicate = isGFX10Only;
    let DecoderNamespace = "GFX10";
  }
}

multiclass MTBUF_Real_AllAddr_gfx10<bits<4> op> {
  defm _BOTHEN : MTBUF_Real_gfx10<op>;
  defm _IDXEN  : MTBUF_Real_gfx10<op>;
  defm _OFFEN  : MTBUF_Real_gfx10<op>;
  defm _OFFSET : MTBUF_Real_gfx10<op>;
}

defm TBUFFER_LOAD_FORMAT_D16_X     : MTBUF_Real_AllAddr_gfx10<0x008>;
defm TBUFFER_LOAD_FORMAT_D16_XY    : MTBUF_Real_AllAddr_gfx10<0x009>;
defm TBUFFER_LOAD_FORMAT_D16_XYZ   : MTBUF_Real_AllAddr_gfx10<0x00a>;
defm TBUFFER_LOAD_FORMAT_D16_XYZW  : MTBUF_Real_AllAddr_gfx10<0x00b>;
defm TBUFFER_STORE_FORMAT_D16_X    : MTBUF_Real_AllAddr_gfx10<0x00c>;
defm TBUFFER_STORE_FORMAT_D16_XY   : MTBUF_Real_AllAddr_gfx10<0x00d>;
defm TBUFFER_STORE_FORMAT_D16_XYZ  : MTBUF_Real_AllAddr_gfx10<0x00e>;
defm TBUFFER_STORE_FORMAT_D16_XYZW : MTBUF_Real_AllAddr_gfx10<0x00f>;

//===----------------------------------------------------------------------===//
// MTBUF - GFX6, GFX7, GFX10.
//===----------------------------------------------------------------------===//

multiclass MTBUF_Real_gfx6_gfx7<bits<4> op> {
  defvar ps = !cast<MTBUF_Pseudo>(NAME);
  def _gfx6_gfx7 : Base_MTBUF_Real_gfx6_gfx7_gfx10<op{2-0}, ps, SIEncodingFamily.SI> {
    let Inst{15} = ps.addr64;
    let Inst{22-19} = dfmt;
    let Inst{25-23} = nfmt;
    let AssemblerPredicate = isGFX6GFX7;
    let DecoderNamespace = "GFX6GFX7";
  }
}

multiclass MTBUF_Real_AllAddr_gfx6_gfx7<bits<4> op> {
  defm _ADDR64 : MTBUF_Real_gfx6_gfx7<op>;
  defm _BOTHEN : MTBUF_Real_gfx6_gfx7<op>;
  defm _IDXEN  : MTBUF_Real_gfx6_gfx7<op>;
  defm _OFFEN  : MTBUF_Real_gfx6_gfx7<op>;
  defm _OFFSET : MTBUF_Real_gfx6_gfx7<op>;
}

multiclass MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<bits<4> op> :
  MTBUF_Real_AllAddr_gfx6_gfx7<op>, MTBUF_Real_AllAddr_gfx10<op>;

defm TBUFFER_LOAD_FORMAT_X     : MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x000>;
defm TBUFFER_LOAD_FORMAT_XY    : MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x001>;
defm TBUFFER_LOAD_FORMAT_XYZ   : MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x002>;
defm TBUFFER_LOAD_FORMAT_XYZW  : MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x003>;
defm TBUFFER_STORE_FORMAT_X    : MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x004>;
defm TBUFFER_STORE_FORMAT_XY   : MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x005>;
defm TBUFFER_STORE_FORMAT_XYZ  : MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x006>;
defm TBUFFER_STORE_FORMAT_XYZW : MTBUF_Real_AllAddr_gfx6_gfx7_gfx10<0x007>;

//===----------------------------------------------------------------------===//
// GFX8, GFX9 (VI).
//===----------------------------------------------------------------------===//

class MUBUF_Real_Base_vi <bits<7> op, MUBUF_Pseudo ps, int Enc,
                          bit has_sccb = ps.has_sccb> :
  MUBUF_Real<ps>,
  Enc64,
  SIMCInstr<ps.PseudoInstr, Enc> {

  let Inst{11-0}  = !if(ps.has_offset, offset, ?);
  let Inst{12}    = ps.offen;
  let Inst{13}    = ps.idxen;
  let Inst{14}    = !if(ps.has_glc, cpol{CPolBit.GLC}, ps.glc_value);
  let Inst{15}    = !if(has_sccb, cpol{CPolBit.SCC}, ps.sccb_value);
  let Inst{16}    = ps.lds;
  let Inst{17}    = !if(ps.has_slc, cpol{CPolBit.SLC}, ?);
  let Inst{24-18} = op;
  let Inst{31-26} = 0x38; //encoding
  let Inst{39-32} = !if(ps.has_vaddr, vaddr, ?);
  let Inst{47-40} = !if(ps.has_vdata, vdata{7-0}, ?);
  let Inst{52-48} = !if(ps.has_srsrc, srsrc{6-2}, ?);
  let Inst{63-56} = !if(ps.has_soffset, soffset, ?);
}

multiclass MUBUF_Real_vi <bits<7> op,
                          bit has_sccb = !cast<MUBUF_Pseudo>(NAME).has_sccb> {
  defvar ps = !cast<MUBUF_Pseudo>(NAME);
  def _vi : MUBUF_Real_Base_vi<op, ps, SIEncodingFamily.VI, has_sccb> {
    let AssemblerPredicate = isGFX8GFX9NotGFX90A;
    let DecoderNamespace = "GFX8";

    let Inst{55}    = ps.tfe;
  }
}

multiclass MUBUF_Real_gfx90a <bits<7> op,
                              bit has_sccb = !cast<MUBUF_Pseudo>(NAME).has_sccb> {
  defvar ps = !cast<MUBUF_Pseudo>(NAME);
  def _gfx90a : MUBUF_Real_Base_vi<op, ps, SIEncodingFamily.GFX90A, has_sccb> {
    let AssemblerPredicate = isGFX90APlus;
    let DecoderNamespace = "GFX90A";
    let AsmString = ps.Mnemonic # !subst("$sccb", !if(has_sccb, "$sccb",""),
                                  ps.AsmOperands);

    let Inst{55}    = acc;
  }
}

class MUBUF_Real_gfx940 <bits<7> op, MUBUF_Pseudo ps> :
  MUBUF_Real_Base_vi<op, ps, SIEncodingFamily.GFX940> {
  let AssemblerPredicate = isGFX940Plus;
  let DecoderNamespace = "GFX9";
  let AsmString = ps.Mnemonic # ps.AsmOperands;

  let Inst{55} = acc;
}

multiclass MUBUF_Real_vi_gfx90a<bits<7> op, bit isTFE = 0> : MUBUF_Real_vi<op> {
  defvar ps = !cast<MUBUF_Pseudo>(NAME);

  if !not(isTFE) then {
    if !not(ps.FPAtomic) then
      defm NAME : MUBUF_Real_gfx90a<op>;
  }

  if ps.FPAtomic then {
    let AssemblerPredicate = isGFX90AOnly in
      defm NAME : MUBUF_Real_gfx90a<op, 0>;

    def _gfx940 : MUBUF_Real_gfx940<op, ps>;
  }
}

multiclass MUBUF_Real_AllAddr_Helper_vi<bits<7> op, bit isTFE = 0> {
  defm _OFFSET : MUBUF_Real_vi_gfx90a <op, isTFE>;
  defm _OFFEN  : MUBUF_Real_vi_gfx90a <op, isTFE>;
  defm _IDXEN  : MUBUF_Real_vi_gfx90a <op, isTFE>;
  defm _BOTHEN : MUBUF_Real_vi_gfx90a <op, isTFE>;
}

multiclass MUBUF_Real_AllAddr_vi<bits<7> op, bit hasTFE = 1> {
  defm NAME : MUBUF_Real_AllAddr_Helper_vi<op>;
  if hasTFE then
    defm _TFE : MUBUF_Real_AllAddr_Helper_vi<op, 1>;
}

multiclass MUBUF_Real_AllAddr_Lds_Helper_vi<bits<7> op, bit isTFE = 0> {
  defm _OFFSET : MUBUF_Real_vi <op>;
  defm _OFFEN  : MUBUF_Real_vi <op>;
  defm _IDXEN  : MUBUF_Real_vi <op>;
  defm _BOTHEN : MUBUF_Real_vi <op>;

  if !not(isTFE) then {
    defm _LDS_OFFSET : MUBUF_Real_vi <op>;
    defm _LDS_OFFEN  : MUBUF_Real_vi <op>;
    defm _LDS_IDXEN  : MUBUF_Real_vi <op>;
    defm _LDS_BOTHEN : MUBUF_Real_vi <op>;

    defm _OFFSET : MUBUF_Real_gfx90a <op>;
    defm _OFFEN  : MUBUF_Real_gfx90a <op>;
    defm _IDXEN  : MUBUF_Real_gfx90a <op>;
    defm _BOTHEN : MUBUF_Real_gfx90a <op>;

    defm _LDS_OFFSET : MUBUF_Real_gfx90a <op>;
    defm _LDS_OFFEN  : MUBUF_Real_gfx90a <op>;
    defm _LDS_IDXEN  : MUBUF_Real_gfx90a <op>;
    defm _LDS_BOTHEN : MUBUF_Real_gfx90a <op>;
  }
}

multiclass MUBUF_Real_AllAddr_Lds_vi<bits<7> op> {
  defm NAME : MUBUF_Real_AllAddr_Lds_Helper_vi<op>;
  defm _TFE : MUBUF_Real_AllAddr_Lds_Helper_vi<op, 1>;
}

multiclass MUBUF_Real_gfx80 <bits<7> op> {
  defvar ps = !cast<MUBUF_Pseudo>(NAME);
  def _gfx80 : MUBUF_Real<ps>,
               Enc64,
               SIMCInstr<ps.PseudoInstr, SIEncodingFamily.GFX80> {
    let AssemblerPredicate=HasUnpackedD16VMem;
    let DecoderNamespace="GFX80_UNPACKED";

    let Inst{11-0}  = !if(ps.has_offset, offset, ?);
    let Inst{12}    = ps.offen;
    let Inst{13}    = ps.idxen;
    let Inst{14}    = !if(ps.has_glc, cpol{CPolBit.GLC}, ps.glc_value);
    let Inst{16}    = ps.lds;
    let Inst{17}    = !if(ps.has_slc, cpol{CPolBit.SLC}, ?);
    let Inst{24-18} = op;
    let Inst{31-26} = 0x38; //encoding
    let Inst{39-32} = !if(ps.has_vaddr, vaddr, ?);
    let Inst{47-40} = !if(ps.has_vdata, vdata{7-0}, ?);
    let Inst{52-48} = !if(ps.has_srsrc, srsrc{6-2}, ?);
    let Inst{55}    = ps.tfe;
    let Inst{63-56} = !if(ps.has_soffset, soffset, ?);
  }
}

multiclass MUBUF_Real_AllAddr_Helper_gfx80<bits<7> op> {
  defm _OFFSET : MUBUF_Real_gfx80 <op>;
  defm _OFFEN  : MUBUF_Real_gfx80 <op>;
  defm _IDXEN  : MUBUF_Real_gfx80 <op>;
  defm _BOTHEN : MUBUF_Real_gfx80 <op>;
}

multiclass MUBUF_Real_AllAddr_gfx80<bits<7> op> {
  defm NAME : MUBUF_Real_AllAddr_Helper_gfx80<op>;
  defm _TFE : MUBUF_Real_AllAddr_Helper_gfx80<op>;
}

multiclass MUBUF_Real_Atomic_vi<bits<7> op> :
  MUBUF_Real_AllAddr_vi<op, 0> {
  defm _OFFSET_RTN : MUBUF_Real_vi_gfx90a <op>;
  defm _OFFEN_RTN  : MUBUF_Real_vi_gfx90a <op>;
  defm _IDXEN_RTN  : MUBUF_Real_vi_gfx90a <op>;
  defm _BOTHEN_RTN : MUBUF_Real_vi_gfx90a <op>;
}

defm BUFFER_LOAD_FORMAT_X       : MUBUF_Real_AllAddr_Lds_vi <0x00>;
defm BUFFER_LOAD_FORMAT_XY      : MUBUF_Real_AllAddr_vi <0x01>;
defm BUFFER_LOAD_FORMAT_XYZ     : MUBUF_Real_AllAddr_vi <0x02>;
defm BUFFER_LOAD_FORMAT_XYZW    : MUBUF_Real_AllAddr_vi <0x03>;
defm BUFFER_STORE_FORMAT_X      : MUBUF_Real_AllAddr_vi <0x04>;
defm BUFFER_STORE_FORMAT_XY     : MUBUF_Real_AllAddr_vi <0x05>;
defm BUFFER_STORE_FORMAT_XYZ    : MUBUF_Real_AllAddr_vi <0x06>;
defm BUFFER_STORE_FORMAT_XYZW   : MUBUF_Real_AllAddr_vi <0x07>;
let SubtargetPredicate = HasUnpackedD16VMem in {
  defm BUFFER_LOAD_FORMAT_D16_X_gfx80       : MUBUF_Real_AllAddr_gfx80 <0x08>;
  defm BUFFER_LOAD_FORMAT_D16_XY_gfx80      : MUBUF_Real_AllAddr_gfx80 <0x09>;
  defm BUFFER_LOAD_FORMAT_D16_XYZ_gfx80     : MUBUF_Real_AllAddr_gfx80 <0x0a>;
  defm BUFFER_LOAD_FORMAT_D16_XYZW_gfx80    : MUBUF_Real_AllAddr_gfx80 <0x0b>;
  defm BUFFER_STORE_FORMAT_D16_X_gfx80      : MUBUF_Real_AllAddr_gfx80 <0x0c>;
  defm BUFFER_STORE_FORMAT_D16_XY_gfx80     : MUBUF_Real_AllAddr_gfx80 <0x0d>;
  defm BUFFER_STORE_FORMAT_D16_XYZ_gfx80    : MUBUF_Real_AllAddr_gfx80 <0x0e>;
  defm BUFFER_STORE_FORMAT_D16_XYZW_gfx80   : MUBUF_Real_AllAddr_gfx80 <0x0f>;
} // End HasUnpackedD16VMem.
let SubtargetPredicate = HasPackedD16VMem in {
  defm BUFFER_LOAD_FORMAT_D16_X       : MUBUF_Real_AllAddr_vi <0x08>;
  defm BUFFER_LOAD_FORMAT_D16_XY      : MUBUF_Real_AllAddr_vi <0x09>;
  defm BUFFER_LOAD_FORMAT_D16_XYZ     : MUBUF_Real_AllAddr_vi <0x0a>;
  defm BUFFER_LOAD_FORMAT_D16_XYZW    : MUBUF_Real_AllAddr_vi <0x0b>;
  defm BUFFER_STORE_FORMAT_D16_X      : MUBUF_Real_AllAddr_vi <0x0c>;
  defm BUFFER_STORE_FORMAT_D16_XY     : MUBUF_Real_AllAddr_vi <0x0d>;
  defm BUFFER_STORE_FORMAT_D16_XYZ    : MUBUF_Real_AllAddr_vi <0x0e>;
  defm BUFFER_STORE_FORMAT_D16_XYZW   : MUBUF_Real_AllAddr_vi <0x0f>;
} // End HasPackedD16VMem.
defm BUFFER_LOAD_UBYTE          : MUBUF_Real_AllAddr_Lds_vi <0x10>;
defm BUFFER_LOAD_SBYTE          : MUBUF_Real_AllAddr_Lds_vi <0x11>;
defm BUFFER_LOAD_USHORT         : MUBUF_Real_AllAddr_Lds_vi <0x12>;
defm BUFFER_LOAD_SSHORT         : MUBUF_Real_AllAddr_Lds_vi <0x13>;
defm BUFFER_LOAD_DWORD          : MUBUF_Real_AllAddr_Lds_vi <0x14>;
defm BUFFER_LOAD_DWORDX2        : MUBUF_Real_AllAddr_vi <0x15>;
defm BUFFER_LOAD_DWORDX3        : MUBUF_Real_AllAddr_vi <0x16>;
defm BUFFER_LOAD_DWORDX4        : MUBUF_Real_AllAddr_vi <0x17>;
defm BUFFER_STORE_BYTE          : MUBUF_Real_AllAddr_vi <0x18>;
defm BUFFER_STORE_BYTE_D16_HI   : MUBUF_Real_AllAddr_vi <0x19>;
defm BUFFER_STORE_SHORT         : MUBUF_Real_AllAddr_vi <0x1a>;
defm BUFFER_STORE_SHORT_D16_HI  : MUBUF_Real_AllAddr_vi <0x1b>;
defm BUFFER_STORE_DWORD         : MUBUF_Real_AllAddr_vi <0x1c>;
defm BUFFER_STORE_DWORDX2       : MUBUF_Real_AllAddr_vi <0x1d>;
defm BUFFER_STORE_DWORDX3       : MUBUF_Real_AllAddr_vi <0x1e>;
defm BUFFER_STORE_DWORDX4       : MUBUF_Real_AllAddr_vi <0x1f>;

defm BUFFER_LOAD_UBYTE_D16      : MUBUF_Real_AllAddr_vi <0x20>;
defm BUFFER_LOAD_UBYTE_D16_HI   : MUBUF_Real_AllAddr_vi <0x21>;
defm BUFFER_LOAD_SBYTE_D16      : MUBUF_Real_AllAddr_vi <0x22>;
defm BUFFER_LOAD_SBYTE_D16_HI   : MUBUF_Real_AllAddr_vi <0x23>;
defm BUFFER_LOAD_SHORT_D16      : MUBUF_Real_AllAddr_vi <0x24>;
defm BUFFER_LOAD_SHORT_D16_HI   : MUBUF_Real_AllAddr_vi <0x25>;

defm BUFFER_LOAD_FORMAT_D16_HI_X  : MUBUF_Real_AllAddr_vi <0x26>;
defm BUFFER_STORE_FORMAT_D16_HI_X : MUBUF_Real_AllAddr_vi <0x27>;

defm BUFFER_ATOMIC_SWAP         : MUBUF_Real_Atomic_vi <0x40>;
defm BUFFER_ATOMIC_CMPSWAP      : MUBUF_Real_Atomic_vi <0x41>;
defm BUFFER_ATOMIC_ADD          : MUBUF_Real_Atomic_vi <0x42>;
defm BUFFER_ATOMIC_SUB          : MUBUF_Real_Atomic_vi <0x43>;
defm BUFFER_ATOMIC_SMIN         : MUBUF_Real_Atomic_vi <0x44>;
defm BUFFER_ATOMIC_UMIN         : MUBUF_Real_Atomic_vi <0x45>;
defm BUFFER_ATOMIC_SMAX         : MUBUF_Real_Atomic_vi <0x46>;
defm BUFFER_ATOMIC_UMAX         : MUBUF_Real_Atomic_vi <0x47>;
defm BUFFER_ATOMIC_AND          : MUBUF_Real_Atomic_vi <0x48>;
defm BUFFER_ATOMIC_OR           : MUBUF_Real_Atomic_vi <0x49>;
defm BUFFER_ATOMIC_XOR          : MUBUF_Real_Atomic_vi <0x4a>;
defm BUFFER_ATOMIC_INC          : MUBUF_Real_Atomic_vi <0x4b>;
defm BUFFER_ATOMIC_DEC          : MUBUF_Real_Atomic_vi <0x4c>;

defm BUFFER_ATOMIC_SWAP_X2      : MUBUF_Real_Atomic_vi <0x60>;
defm BUFFER_ATOMIC_CMPSWAP_X2   : MUBUF_Real_Atomic_vi <0x61>;
defm BUFFER_ATOMIC_ADD_X2       : MUBUF_Real_Atomic_vi <0x62>;
defm BUFFER_ATOMIC_SUB_X2       : MUBUF_Real_Atomic_vi <0x63>;
defm BUFFER_ATOMIC_SMIN_X2      : MUBUF_Real_Atomic_vi <0x64>;
defm BUFFER_ATOMIC_UMIN_X2      : MUBUF_Real_Atomic_vi <0x65>;
defm BUFFER_ATOMIC_SMAX_X2      : MUBUF_Real_Atomic_vi <0x66>;
defm BUFFER_ATOMIC_UMAX_X2      : MUBUF_Real_Atomic_vi <0x67>;
defm BUFFER_ATOMIC_AND_X2       : MUBUF_Real_Atomic_vi <0x68>;
defm BUFFER_ATOMIC_OR_X2        : MUBUF_Real_Atomic_vi <0x69>;
defm BUFFER_ATOMIC_XOR_X2       : MUBUF_Real_Atomic_vi <0x6a>;
defm BUFFER_ATOMIC_INC_X2       : MUBUF_Real_Atomic_vi <0x6b>;
defm BUFFER_ATOMIC_DEC_X2       : MUBUF_Real_Atomic_vi <0x6c>;

defm BUFFER_STORE_LDS_DWORD     : MUBUF_Real_vi_gfx90a <0x3d>;

let AssemblerPredicate = isGFX8GFX9 in {
defm BUFFER_WBINVL1             : MUBUF_Real_vi <0x3e>;
defm BUFFER_WBINVL1_VOL         : MUBUF_Real_vi <0x3f>;
} // End AssemblerPredicate = isGFX8GFX9


defm BUFFER_ATOMIC_PK_ADD_F16 : MUBUF_Real_Atomic_vi <0x4e>;
defm BUFFER_ATOMIC_ADD_F32    : MUBUF_Real_Atomic_vi <0x4d>;

let SubtargetPredicate = isGFX90APlus in {
  defm BUFFER_ATOMIC_ADD_F64 : MUBUF_Real_Atomic_vi<0x4f>;
  defm BUFFER_ATOMIC_MIN_F64 : MUBUF_Real_Atomic_vi<0x50>;
  defm BUFFER_ATOMIC_MAX_F64 : MUBUF_Real_Atomic_vi<0x51>;
} // End SubtargetPredicate = isGFX90APlus

let AsmString = BUFFER_WBL2.Mnemonic, // drop flags
    AssemblerPredicate = isGFX90AOnly,
    SubtargetPredicate = isGFX90AOnly in
defm BUFFER_WBL2  : MUBUF_Real_gfx90a<0x28>;
defm BUFFER_INVL2 : MUBUF_Real_gfx90a<0x29>;

let SubtargetPredicate = isGFX940Plus in {
def BUFFER_WBL2_gfx940  : MUBUF_Real_gfx940<0x28, BUFFER_WBL2>;
def BUFFER_INV_gfx940   : MUBUF_Real_gfx940<0x29, BUFFER_INV>;
}

class MTBUF_Real_Base_vi <bits<4> op, MTBUF_Pseudo ps, int Enc> :
  MTBUF_Real<ps>,
  Enc64,
  SIMCInstr<ps.PseudoInstr, Enc> {

  let Inst{11-0}  = !if(ps.has_offset, offset, ?);
  let Inst{12}    = ps.offen;
  let Inst{13}    = ps.idxen;
  let Inst{14}    = !if(ps.has_glc, cpol{CPolBit.GLC}, ps.glc_value);
  let Inst{18-15} = op;
  let Inst{22-19} = dfmt;
  let Inst{25-23} = nfmt;
  let Inst{31-26} = 0x3a; //encoding
  let Inst{39-32} = !if(ps.has_vaddr, vaddr, ?);
  let Inst{47-40} = !if(ps.has_vdata, vdata{7-0}, ?);
  let Inst{52-48} = !if(ps.has_srsrc, srsrc{6-2}, ?);
  let Inst{53}    = !if(ps.has_sccb, cpol{CPolBit.SCC}, ps.sccb_value);
  let Inst{54}    = !if(ps.has_slc, cpol{CPolBit.SLC}, ?);
  let Inst{55}    = ps.tfe;
  let Inst{63-56} = !if(ps.has_soffset, soffset, ?);
}

class MTBUF_Real_vi <bits<4> op, MTBUF_Pseudo ps> :
  MTBUF_Real_Base_vi <op, ps, SIEncodingFamily.VI> {
  let AssemblerPredicate = isGFX8GFX9NotGFX90A;
  let DecoderNamespace = "GFX8";

  let Inst{55}    = ps.tfe;
}

class MTBUF_Real_gfx90a <bits<4> op, MTBUF_Pseudo ps> :
  MTBUF_Real_Base_vi <op, ps, SIEncodingFamily.GFX90A> {
  let AssemblerPredicate = isGFX90APlus;
  let DecoderNamespace = "GFX90A";
  let AsmString = ps.Mnemonic # ps.AsmOperands;

  let Inst{55}    = acc;
}

multiclass MTBUF_Real_vi_gfx90a<bits<4> op> {
  defvar ps = !cast<MTBUF_Pseudo>(NAME);
  def _vi :     MTBUF_Real_vi<op, ps>;
  def _gfx90a : MTBUF_Real_gfx90a<op, ps>;
}

multiclass MTBUF_Real_AllAddr_vi<bits<4> op> {
  defm _OFFSET : MTBUF_Real_vi_gfx90a <op>;
  defm _OFFEN  : MTBUF_Real_vi_gfx90a <op>;
  defm _IDXEN  : MTBUF_Real_vi_gfx90a <op>;
  defm _BOTHEN : MTBUF_Real_vi_gfx90a <op>;
}

multiclass MTBUF_Real_gfx80 <bits<4> op> {
  defvar ps = !cast<MTBUF_Pseudo>(NAME);
  def _gfx80 : MTBUF_Real<ps>,
               Enc64,
               SIMCInstr<ps.PseudoInstr, SIEncodingFamily.GFX80> {
    let AssemblerPredicate=HasUnpackedD16VMem;
    let DecoderNamespace="GFX80_UNPACKED";

    let Inst{11-0}  = !if(ps.has_offset, offset, ?);
    let Inst{12}    = ps.offen;
    let Inst{13}    = ps.idxen;
    let Inst{14}    = !if(ps.has_glc, cpol{CPolBit.GLC}, ps.glc_value);
    let Inst{18-15} = op;
    let Inst{22-19} = dfmt;
    let Inst{25-23} = nfmt;
    let Inst{31-26} = 0x3a; //encoding
    let Inst{39-32} = !if(ps.has_vaddr, vaddr, ?);
    let Inst{47-40} = !if(ps.has_vdata, vdata{7-0}, ?);
    let Inst{52-48} = !if(ps.has_srsrc, srsrc{6-2}, ?);
    let Inst{54}    = !if(ps.has_slc, cpol{CPolBit.SLC}, ?);
    let Inst{55}    = ps.tfe;
    let Inst{63-56} = !if(ps.has_soffset, soffset, ?);
  }
}

multiclass MTBUF_Real_AllAddr_gfx80<bits<4> op> {
  defm _OFFSET : MTBUF_Real_gfx80 <op>;
  defm _OFFEN  : MTBUF_Real_gfx80 <op>;
  defm _IDXEN  : MTBUF_Real_gfx80 <op>;
  defm _BOTHEN : MTBUF_Real_gfx80 <op>;
}

defm TBUFFER_LOAD_FORMAT_X     : MTBUF_Real_AllAddr_vi <0x00>;
defm TBUFFER_LOAD_FORMAT_XY    : MTBUF_Real_AllAddr_vi <0x01>;
defm TBUFFER_LOAD_FORMAT_XYZ   : MTBUF_Real_AllAddr_vi <0x02>;
defm TBUFFER_LOAD_FORMAT_XYZW  : MTBUF_Real_AllAddr_vi <0x03>;
defm TBUFFER_STORE_FORMAT_X    : MTBUF_Real_AllAddr_vi <0x04>;
defm TBUFFER_STORE_FORMAT_XY   : MTBUF_Real_AllAddr_vi <0x05>;
defm TBUFFER_STORE_FORMAT_XYZ  : MTBUF_Real_AllAddr_vi <0x06>;
defm TBUFFER_STORE_FORMAT_XYZW : MTBUF_Real_AllAddr_vi <0x07>;
let SubtargetPredicate = HasUnpackedD16VMem in {
  defm TBUFFER_LOAD_FORMAT_D16_X_gfx80     : MTBUF_Real_AllAddr_gfx80 <0x08>;
  defm TBUFFER_LOAD_FORMAT_D16_XY_gfx80    : MTBUF_Real_AllAddr_gfx80 <0x09>;
  defm TBUFFER_LOAD_FORMAT_D16_XYZ_gfx80   : MTBUF_Real_AllAddr_gfx80 <0x0a>;
  defm TBUFFER_LOAD_FORMAT_D16_XYZW_gfx80  : MTBUF_Real_AllAddr_gfx80 <0x0b>;
  defm TBUFFER_STORE_FORMAT_D16_X_gfx80    : MTBUF_Real_AllAddr_gfx80 <0x0c>;
  defm TBUFFER_STORE_FORMAT_D16_XY_gfx80   : MTBUF_Real_AllAddr_gfx80 <0x0d>;
  defm TBUFFER_STORE_FORMAT_D16_XYZ_gfx80  : MTBUF_Real_AllAddr_gfx80 <0x0e>;
  defm TBUFFER_STORE_FORMAT_D16_XYZW_gfx80 : MTBUF_Real_AllAddr_gfx80 <0x0f>;
} // End HasUnpackedD16VMem.
let SubtargetPredicate = HasPackedD16VMem in {
  defm TBUFFER_LOAD_FORMAT_D16_X     : MTBUF_Real_AllAddr_vi <0x08>;
  defm TBUFFER_LOAD_FORMAT_D16_XY    : MTBUF_Real_AllAddr_vi <0x09>;
  defm TBUFFER_LOAD_FORMAT_D16_XYZ   : MTBUF_Real_AllAddr_vi <0x0a>;
  defm TBUFFER_LOAD_FORMAT_D16_XYZW  : MTBUF_Real_AllAddr_vi <0x0b>;
  defm TBUFFER_STORE_FORMAT_D16_X    : MTBUF_Real_AllAddr_vi <0x0c>;
  defm TBUFFER_STORE_FORMAT_D16_XY   : MTBUF_Real_AllAddr_vi <0x0d>;
  defm TBUFFER_STORE_FORMAT_D16_XYZ  : MTBUF_Real_AllAddr_vi <0x0e>;
  defm TBUFFER_STORE_FORMAT_D16_XYZW : MTBUF_Real_AllAddr_vi <0x0f>;
} // End HasUnpackedD16VMem.

def MUBUFInfoTable : GenericTable {
  let FilterClass = "MUBUF_Pseudo";
  let CppTypeName = "MUBUFInfo";
  let Fields = [
    "Opcode", "BaseOpcode", "elements", "has_vaddr", "has_srsrc", "has_soffset",
    "IsBufferInv", "tfe"
  ];

  let PrimaryKey = ["Opcode"];
  let PrimaryKeyName = "getMUBUFOpcodeHelper";
}

def getMUBUFInfoFromOpcode : SearchIndex {
  let Table = MUBUFInfoTable;
  let Key = ["Opcode"];
}

def getMUBUFInfoFromBaseOpcodeAndElements : SearchIndex {
  let Table = MUBUFInfoTable;
  let Key = ["BaseOpcode", "elements"];
}

def MTBUFInfoTable : GenericTable {
  let FilterClass = "MTBUF_Pseudo";
  let CppTypeName = "MTBUFInfo";
  let Fields = ["Opcode", "BaseOpcode", "elements", "has_vaddr", "has_srsrc", "has_soffset"];

  let PrimaryKey = ["Opcode"];
  let PrimaryKeyName = "getMTBUFOpcodeHelper";
}

def getMTBUFInfoFromOpcode : SearchIndex {
  let Table = MTBUFInfoTable;
  let Key = ["Opcode"];
}

def getMTBUFInfoFromBaseOpcodeAndElements : SearchIndex {
  let Table = MTBUFInfoTable;
  let Key = ["BaseOpcode", "elements"];
}
