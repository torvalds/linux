//-- SystemZScheduleZ13.td - SystemZ Scheduling Definitions ----*- tblgen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Z13 to support instruction
// scheduling and other instruction cost heuristics.
//
// Pseudos expanded right after isel do not need to be modelled here.
//
//===----------------------------------------------------------------------===//

def Z13Model : SchedMachineModel {

    let UnsupportedFeatures = Arch11UnsupportedFeatures.List;

    let IssueWidth = 6;             // Number of instructions decoded per cycle.
    let MicroOpBufferSize = 60;     // Issue queues
    let LoadLatency = 1;            // Optimistic load latency.

    let PostRAScheduler = 1;

    // Extra cycles for a mispredicted branch.
    let MispredictPenalty = 20;
}

let SchedModel = Z13Model in  {
// These definitions need the SchedModel value. They could be put in a
// subtarget common include file, but it seems the include system in Tablegen
// currently (2016) rejects multiple includes of same file.

// Decoder grouping rules
let NumMicroOps = 1 in {
  def : WriteRes<NormalGr, []>;
  def : WriteRes<BeginGroup, []> { let BeginGroup  = 1; }
  def : WriteRes<EndGroup, []>   { let EndGroup    = 1; }
}
def : WriteRes<Cracked, []> {
  let NumMicroOps = 2;
  let BeginGroup  = 1;
}
def : WriteRes<GroupAlone, []> {
  let NumMicroOps = 3;
  let BeginGroup  = 1;
  let EndGroup    = 1;
}
def : WriteRes<GroupAlone2, []> {
  let NumMicroOps = 6;
  let BeginGroup  = 1;
  let EndGroup    = 1;
}
def : WriteRes<GroupAlone3, []> {
  let NumMicroOps = 9;
  let BeginGroup  = 1;
  let EndGroup    = 1;
}

// Incoming latency removed from the register operand which is used together
// with a memory operand by the instruction.
def : ReadAdvance<RegReadAdv, 4>;

// LoadLatency (above) is not used for instructions in this file. This is
// instead the role of LSULatency, which is the latency value added to the
// result of loads and instructions with folded memory operands.
def : WriteRes<LSULatency, []> { let Latency = 4; let NumMicroOps = 0; }

let NumMicroOps = 0 in {
  foreach L = 1-30 in
    def : WriteRes<!cast<SchedWrite>("WLat"#L), []> { let Latency = L; }
}

// Execution units.
def Z13_FXaUnit     : ProcResource<2>;
def Z13_FXbUnit     : ProcResource<2>;
def Z13_LSUnit      : ProcResource<2>;
def Z13_VecUnit     : ProcResource<2>;
def Z13_VecFPdUnit  : ProcResource<2> { let BufferSize = 1; /* blocking */ }
def Z13_VBUnit      : ProcResource<2>;
def Z13_MCD         : ProcResource<1>;

// Subtarget specific definitions of scheduling resources.
let NumMicroOps = 0 in {
  def : WriteRes<FXa, [Z13_FXaUnit]>;
  def : WriteRes<FXb, [Z13_FXbUnit]>;
  def : WriteRes<LSU, [Z13_LSUnit]>;
  def : WriteRes<VecBF,  [Z13_VecUnit]>;
  def : WriteRes<VecDF,  [Z13_VecUnit]>;
  def : WriteRes<VecDFX, [Z13_VecUnit]>;
  def : WriteRes<VecMul,  [Z13_VecUnit]>;
  def : WriteRes<VecStr,  [Z13_VecUnit]>;
  def : WriteRes<VecXsPm, [Z13_VecUnit]>;
  foreach Num = 2-5 in { let ReleaseAtCycles = [Num] in {
    def : WriteRes<!cast<SchedWrite>("FXa"#Num), [Z13_FXaUnit]>;
    def : WriteRes<!cast<SchedWrite>("FXb"#Num), [Z13_FXbUnit]>;
    def : WriteRes<!cast<SchedWrite>("LSU"#Num), [Z13_LSUnit]>;
    def : WriteRes<!cast<SchedWrite>("VecBF"#Num), [Z13_VecUnit]>;
    def : WriteRes<!cast<SchedWrite>("VecDF"#Num), [Z13_VecUnit]>;
    def : WriteRes<!cast<SchedWrite>("VecDFX"#Num), [Z13_VecUnit]>;
    def : WriteRes<!cast<SchedWrite>("VecMul"#Num), [Z13_VecUnit]>;
    def : WriteRes<!cast<SchedWrite>("VecStr"#Num), [Z13_VecUnit]>;
    def : WriteRes<!cast<SchedWrite>("VecXsPm"#Num), [Z13_VecUnit]>;
  }}

  def : WriteRes<VecFPd,  [Z13_VecFPdUnit]> { let ReleaseAtCycles = [30]; }

  def : WriteRes<VBU,     [Z13_VBUnit]>; // Virtual Branching Unit
}

def : WriteRes<MCD, [Z13_MCD]> { let NumMicroOps = 3;
                                 let BeginGroup  = 1;
                                 let EndGroup    = 1; }

// -------------------------- INSTRUCTIONS ---------------------------------- //

// InstRW constructs have been used in order to preserve the
// readability of the InstrInfo files.

// For each instruction, as matched by a regexp, provide a list of
// resources that it needs. These will be combined into a SchedClass.

//===----------------------------------------------------------------------===//
// Stack allocation
//===----------------------------------------------------------------------===//

// Pseudo -> LA / LAY
def : InstRW<[WLat1, FXa, NormalGr], (instregex "ADJDYNALLOC$")>;

//===----------------------------------------------------------------------===//
// Branch instructions
//===----------------------------------------------------------------------===//

// Branch
def : InstRW<[WLat1, VBU, NormalGr], (instregex "(Call)?BRC(L)?(Asm.*)?$")>;
def : InstRW<[WLat1, VBU, NormalGr], (instregex "(Call)?J(G)?(Asm.*)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "(Call)?BC(R)?(Asm.*)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "(Call)?B(R)?(Asm.*)?$")>;
def : InstRW<[WLat1, FXa, EndGroup], (instregex "BRCT(G)?$")>;
def : InstRW<[WLat1, FXa, FXb, GroupAlone], (instregex "BRCTH$")>;
def : InstRW<[WLat1, FXa, FXb, GroupAlone], (instregex "BCT(G)?(R)?$")>;
def : InstRW<[WLat1, FXa2, FXb2, GroupAlone2],
             (instregex "B(R)?X(H|L).*$")>;

// Compare and branch
def : InstRW<[WLat1, FXb, NormalGr], (instregex "C(L)?(G)?(I|R)J(Asm.*)?$")>;
def : InstRW<[WLat1, FXb2, GroupAlone],
             (instregex "C(L)?(G)?(I|R)B(Call|Return|Asm.*)?$")>;

//===----------------------------------------------------------------------===//
// Trap instructions
//===----------------------------------------------------------------------===//

// Trap
def : InstRW<[WLat1, VBU, NormalGr], (instregex "(Cond)?Trap$")>;

// Compare and trap
def : InstRW<[WLat1, FXb, NormalGr], (instregex "C(G)?(I|R)T(Asm.*)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CL(G)?RT(Asm.*)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CL(F|G)IT(Asm.*)?$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "CL(G)?T(Asm.*)?$")>;

//===----------------------------------------------------------------------===//
// Call and return instructions
//===----------------------------------------------------------------------===//

// Call
def : InstRW<[WLat1, VBU, FXa2, GroupAlone], (instregex "(Call)?BRAS$")>;
def : InstRW<[WLat1, FXa2, FXb, GroupAlone], (instregex "(Call)?BRASL(_XPLINK64)?$")>;
def : InstRW<[WLat1, FXa2, FXb, GroupAlone], (instregex "(Call)?BAS(R)?(_XPLINK64|_STACKEXT)?$")>;
def : InstRW<[WLat1, FXa2, FXb, GroupAlone], (instregex "TLS_(G|L)DCALL$")>;

// Return
def : InstRW<[WLat1, FXb, EndGroup], (instregex "Return(_XPLINK)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CondReturn(_XPLINK)?$")>;

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

// Moves
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "MV(G|H)?HI$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "MVI(Y)?$")>;

// Move character
def : InstRW<[WLat1, FXb, LSU3, GroupAlone], (instregex "MVC$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "MVCL(E|U)?$")>;

// Pseudo -> reg move
def : InstRW<[WLat1, FXa, NormalGr], (instregex "COPY(_TO_REGCLASS)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "EXTRACT_SUBREG$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "INSERT_SUBREG$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "REG_SEQUENCE$")>;

// Loads
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "L(Y|FH|RL|Mux)?$")>;
def : InstRW<[LSULatency, LSULatency, LSU, NormalGr], (instregex "LCBB$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LG(RL)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "L128$")>;

def : InstRW<[WLat1, FXa, NormalGr], (instregex "LLIH(F|H|L)$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LLIL(F|H|L)$")>;

def : InstRW<[WLat1, FXa, NormalGr], (instregex "LG(F|H)I$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LHI(Mux)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LR$")>;

// Load and zero rightmost byte
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LZR(F|G)$")>;

// Load and trap
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "L(FH|G)?AT$")>;

// Load and test
def : InstRW<[WLat1LSU, WLat1LSU, LSU, FXa, NormalGr], (instregex "LT(G)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LT(G)?R$")>;

// Stores
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "STG(RL)?$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "ST128$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "ST(Y|FH|RL|Mux)?$")>;

// String moves.
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "MVST$")>;

//===----------------------------------------------------------------------===//
// Conditional move instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, FXa, NormalGr], (instregex "LOCRMux$")>;
def : InstRW<[WLat2, FXa, NormalGr], (instregex "LOC(G|FH)?R(Asm.*)?$")>;
def : InstRW<[WLat2, FXa, NormalGr], (instregex "LOC(G|H)?HI(Mux|(Asm.*))?$")>;
def : InstRW<[WLat2LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "LOC(G|FH|Mux)?(Asm.*)?$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr],
             (instregex "STOC(G|FH|Mux)?(Asm.*)?$")>;

//===----------------------------------------------------------------------===//
// Sign extensions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXa, NormalGr], (instregex "L(B|H|G)R$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LG(B|H|F)R$")>;

def : InstRW<[WLat1LSU, WLat1LSU, FXa, LSU, NormalGr], (instregex "LTGF$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LTGFR$")>;

def : InstRW<[WLat1LSU, FXa, LSU, NormalGr], (instregex "LB(H|Mux)?$")>;
def : InstRW<[WLat1LSU, FXa, LSU, NormalGr], (instregex "LH(Y)?$")>;
def : InstRW<[WLat1LSU, FXa, LSU, NormalGr], (instregex "LH(H|Mux|RL)$")>;
def : InstRW<[WLat1LSU, FXa, LSU, NormalGr], (instregex "LG(B|H|F)$")>;
def : InstRW<[WLat1LSU, FXa, LSU, NormalGr], (instregex "LG(H|F)RL$")>;

//===----------------------------------------------------------------------===//
// Zero extensions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXa, NormalGr], (instregex "LLCR(Mux)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LLHR(Mux)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LLG(C|H|F|T)R$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLC(Mux)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLH(Mux)?$")>;
def : InstRW<[WLat1LSU, FXa, LSU, NormalGr], (instregex "LL(C|H)H$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLHRL$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLG(C|H|F|T|HRL|FRL)$")>;

// Load and zero rightmost byte
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LLZRGF$")>;

// Load and trap
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "LLG(F|T)?AT$")>;

//===----------------------------------------------------------------------===//
// Truncations
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "STC(H|Y|Mux)?$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "STH(H|Y|RL|Mux)?$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "STCM(H|Y)?$")>;

//===----------------------------------------------------------------------===//
// Multi-register moves
//===----------------------------------------------------------------------===//

// Load multiple (estimated average of 5 ops)
def : InstRW<[WLat10, WLat10, LSU5, GroupAlone], (instregex "LM(H|Y|G)?$")>;

// Load multiple disjoint
def : InstRW<[WLat30, WLat30, MCD], (instregex "LMD$")>;

// Store multiple
def : InstRW<[WLat1, LSU2, FXb3, GroupAlone], (instregex "STM(G|H|Y)?$")>;

//===----------------------------------------------------------------------===//
// Byte swaps
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXa, NormalGr], (instregex "LRV(G)?R$")>;
def : InstRW<[WLat1LSU, FXa, LSU, NormalGr], (instregex "LRV(G|H)?$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "STRV(G|H)?$")>;
def : InstRW<[WLat30, MCD], (instregex "MVCIN$")>;

//===----------------------------------------------------------------------===//
// Load address instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXa, NormalGr], (instregex "LA(Y|RL)?$")>;

// Load the Global Offset Table address ( -> larl )
def : InstRW<[WLat1, FXa, NormalGr], (instregex "GOT$")>;

//===----------------------------------------------------------------------===//
// Absolute and Negation
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, WLat2, FXa, NormalGr], (instregex "LP(G)?R$")>;
def : InstRW<[WLat3, WLat3, FXa2, Cracked], (instregex "L(N|P)GFR$")>;
def : InstRW<[WLat2, WLat2, FXa, NormalGr], (instregex "LN(R|GR)$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "LC(R|GR)$")>;
def : InstRW<[WLat2, WLat2, FXa2, Cracked], (instregex "LCGFR$")>;

//===----------------------------------------------------------------------===//
// Insertion
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, RegReadAdv, FXa, LSU, NormalGr], (instregex "IC(Y)?$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "IC32(Y)?$")>;
def : InstRW<[WLat1LSU, RegReadAdv, WLat1LSU, FXa, LSU, NormalGr],
             (instregex "ICM(H|Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "II(F|H|L)Mux$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "IIHF(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "IIHH(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "IIHL(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "IILF(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "IILH(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "IILL(64)?$")>;

//===----------------------------------------------------------------------===//
// Addition
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "A(Y)?$")>;
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "AH(Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AIH$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AFI(Mux)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "AG$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AGFI$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AGHI(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AGR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AHI(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AHIMux(K)?$")>;
def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "AL(Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AL(FI|HSIK)$")>;
def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "ALG(F)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "ALGHSIK$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "ALGF(I|R)$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "ALGR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "ALR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "AR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "A(L)?HHHR$")>;
def : InstRW<[WLat2, WLat2, FXa, NormalGr], (instregex "A(L)?HHLR$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "ALSIH(N)?$")>;
def : InstRW<[WLat2LSU, FXb, LSU, NormalGr], (instregex "A(L)?(G)?SI$")>;

// Logical addition with carry
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXa, LSU, GroupAlone],
             (instregex "ALC(G)?$")>;
def : InstRW<[WLat2, WLat2, FXa, GroupAlone], (instregex "ALC(G)?R$")>;

// Add with sign extension (32 -> 64)
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "AGF$")>;
def : InstRW<[WLat2, WLat2, FXa, NormalGr], (instregex "AGFR$")>;

//===----------------------------------------------------------------------===//
// Subtraction
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "S(G|Y)?$")>;
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "SH(Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SGR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SLFI$")>;
def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "SL(G|GF|Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SLGF(I|R)$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SLGR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SLR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "S(L)?HHHR$")>;
def : InstRW<[WLat2, WLat2, FXa, NormalGr], (instregex "S(L)?HHLR$")>;

// Subtraction with borrow
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXa, LSU, GroupAlone],
             (instregex "SLB(G)?$")>;
def : InstRW<[WLat2, WLat2, FXa, GroupAlone], (instregex "SLB(G)?R$")>;

// Subtraction with sign extension (32 -> 64)
def : InstRW<[WLat2LSU, WLat2LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "SGF$")>;
def : InstRW<[WLat2, WLat2, FXa, NormalGr], (instregex "SGFR$")>;

//===----------------------------------------------------------------------===//
// AND
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "N(G|Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NGR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NI(FMux|HMux|LMux)$")>;
def : InstRW<[WLat2LSU, FXb, LSU, NormalGr], (instregex "NI(Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NIHF(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NIHH(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NIHL(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NILF(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NILH(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NILL(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "NR(K)?$")>;
def : InstRW<[WLat3LSU, LSU2, FXb, Cracked], (instregex "NC$")>;

//===----------------------------------------------------------------------===//
// OR
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "O(G|Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OGR(K)?$")>;
def : InstRW<[WLat2LSU, FXb, LSU, NormalGr], (instregex "OI(Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OI(FMux|HMux|LMux)$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OIHF(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OIHH(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OIHL(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OILF(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OILH(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OILL(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "OR(K)?$")>;
def : InstRW<[WLat3LSU, LSU2, FXb, Cracked], (instregex "OC$")>;

//===----------------------------------------------------------------------===//
// XOR
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, WLat1LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "X(G|Y)?$")>;
def : InstRW<[WLat2LSU, FXb, LSU, NormalGr], (instregex "XI(Y)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "XIFMux$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "XGR(K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "XIHF(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "XILF(64)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "XR(K)?$")>;
def : InstRW<[WLat3LSU, LSU2, FXb, Cracked], (instregex "XC$")>;

//===----------------------------------------------------------------------===//
// Multiplication
//===----------------------------------------------------------------------===//

def : InstRW<[WLat6LSU, RegReadAdv, FXa, LSU, NormalGr],
             (instregex "MS(GF|Y)?$")>;
def : InstRW<[WLat6, FXa, NormalGr], (instregex "MS(R|FI)$")>;
def : InstRW<[WLat8LSU, RegReadAdv, FXa, LSU, NormalGr], (instregex "MSG$")>;
def : InstRW<[WLat8, FXa, NormalGr], (instregex "MSGR$")>;
def : InstRW<[WLat6, FXa, NormalGr], (instregex "MSGF(I|R)$")>;
def : InstRW<[WLat11LSU, RegReadAdv, FXa2, LSU, GroupAlone],
             (instregex "MLG$")>;
def : InstRW<[WLat9, FXa2, GroupAlone], (instregex "MLGR$")>;
def : InstRW<[WLat5, FXa, NormalGr], (instregex "MGHI$")>;
def : InstRW<[WLat5, FXa, NormalGr], (instregex "MHI$")>;
def : InstRW<[WLat5LSU, RegReadAdv, FXa, LSU, NormalGr], (instregex "MH(Y)?$")>;
def : InstRW<[WLat7, FXa2, GroupAlone], (instregex "M(L)?R$")>;
def : InstRW<[WLat7LSU, RegReadAdv, FXa2, LSU, GroupAlone],
             (instregex "M(FY|L)?$")>;

//===----------------------------------------------------------------------===//
// Division and remainder
//===----------------------------------------------------------------------===//

def : InstRW<[WLat20, FXa4, GroupAlone], (instregex "DR$")>;
def : InstRW<[WLat30, RegReadAdv, FXa4, LSU, GroupAlone2], (instregex "D$")>;
def : InstRW<[WLat30, FXa2, GroupAlone], (instregex "DSG(F)?R$")>;
def : InstRW<[WLat30, RegReadAdv, FXa2, LSU, GroupAlone2],
             (instregex "DSG(F)?$")>;
def : InstRW<[WLat20, FXa4, GroupAlone], (instregex "DLR$")>;
def : InstRW<[WLat30, FXa4, GroupAlone], (instregex "DLGR$")>;
def : InstRW<[WLat30, RegReadAdv, FXa4, LSU, GroupAlone2], (instregex "DL(G)?$")>;

//===----------------------------------------------------------------------===//
// Shifts
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXa, NormalGr], (instregex "SLL(G|K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SRL(G|K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SRA(G|K)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "SLA(G|K)?$")>;
def : InstRW<[WLat5LSU, WLat5LSU, FXa4, LSU, GroupAlone2],
             (instregex "S(L|R)D(A|L)$")>;

// Rotate
def : InstRW<[WLat2LSU, FXa, LSU, NormalGr], (instregex "RLL(G)?$")>;

// Rotate and insert
def : InstRW<[WLat1, FXa, NormalGr], (instregex "RISBH(G|H|L)$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "RISBL(G|H|L)$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "RISBG(N|32)?(Z)?$")>;
def : InstRW<[WLat1, FXa, NormalGr], (instregex "RISBMux$")>;

// Rotate and Select
def : InstRW<[WLat3, WLat3, FXa2, Cracked], (instregex "R(N|O|X)SBG$")>;

//===----------------------------------------------------------------------===//
// Comparison
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1LSU, RegReadAdv, FXb, LSU, NormalGr],
             (instregex "C(G|Y|Mux)?$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CRL$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "C(F|H)I(Mux)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CG(F|H)I$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CG(HSI|RL)$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "C(G)?R$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CIH$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXb, LSU, NormalGr], (instregex "CHF$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CHSI$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXb, LSU, NormalGr],
             (instregex "CL(Y|Mux)?$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CLFHSI$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CLFI(Mux)?$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXb, LSU, NormalGr], (instregex "CLG$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CLG(HRL|HSI)$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXb, LSU, NormalGr], (instregex "CLGF$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CLGFRL$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CLGF(I|R)$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CLGR$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CLGRL$")>;
def : InstRW<[WLat1LSU, RegReadAdv, FXb, LSU, NormalGr], (instregex "CLHF$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CLH(RL|HSI)$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CLIH$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CLI(Y)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "CLR$")>;
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "CLRL$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "C(L)?HHR$")>;
def : InstRW<[WLat2, FXb, NormalGr], (instregex "C(L)?HLR$")>;

// Compare halfword
def : InstRW<[WLat2LSU, RegReadAdv, FXb, LSU, NormalGr], (instregex "CH(Y)?$")>;
def : InstRW<[WLat2LSU, FXb, LSU, NormalGr], (instregex "CHRL$")>;
def : InstRW<[WLat2LSU, RegReadAdv, FXb, LSU, NormalGr], (instregex "CGH$")>;
def : InstRW<[WLat2LSU, FXb, LSU, NormalGr], (instregex "CGHRL$")>;
def : InstRW<[WLat2LSU, FXa, FXb, LSU, Cracked], (instregex "CHHSI$")>;

// Compare with sign extension (32 -> 64)
def : InstRW<[WLat2LSU, RegReadAdv, FXb, LSU, NormalGr], (instregex "CGF$")>;
def : InstRW<[WLat2LSU, FXb, LSU, NormalGr], (instregex "CGFRL$")>;
def : InstRW<[WLat2, FXb, NormalGr], (instregex "CGFR$")>;

// Compare logical character
def : InstRW<[WLat6, FXb, LSU2, Cracked], (instregex "CLC$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "CLCL(E|U)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "CLST$")>;

// Test under mask
def : InstRW<[WLat1LSU, FXb, LSU, NormalGr], (instregex "TM(Y)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "TM(H|L)Mux$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "TMHH(64)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "TMHL(64)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "TMLH(64)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "TMLL(64)?$")>;

// Compare logical characters under mask
def : InstRW<[WLat2LSU, RegReadAdv, FXb, LSU, NormalGr],
             (instregex "CLM(H|Y)?$")>;

//===----------------------------------------------------------------------===//
// Prefetch and execution hint
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, LSU, NormalGr], (instregex "PFD(RL)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "BPP$")>;
def : InstRW<[FXb, EndGroup], (instregex "BPRP$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "NIAI$")>;

//===----------------------------------------------------------------------===//
// Atomic operations
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXb, EndGroup], (instregex "Serialize$")>;

def : InstRW<[WLat2LSU, WLat2LSU, FXb, LSU, NormalGr], (instregex "LAA(G)?$")>;
def : InstRW<[WLat2LSU, WLat2LSU, FXb, LSU, NormalGr], (instregex "LAAL(G)?$")>;
def : InstRW<[WLat2LSU, WLat2LSU, FXb, LSU, NormalGr], (instregex "LAN(G)?$")>;
def : InstRW<[WLat2LSU, WLat2LSU, FXb, LSU, NormalGr], (instregex "LAO(G)?$")>;
def : InstRW<[WLat2LSU, WLat2LSU, FXb, LSU, NormalGr], (instregex "LAX(G)?$")>;

// Test and set
def : InstRW<[WLat2LSU, FXb, LSU, EndGroup], (instregex "TS$")>;

// Compare and swap
def : InstRW<[WLat3LSU, WLat3LSU, FXa, FXb, LSU, GroupAlone],
             (instregex "CS(G|Y)?$")>;

// Compare double and swap
def : InstRW<[WLat6LSU, WLat6LSU, FXa3, FXb2, LSU, GroupAlone2],
             (instregex "CDS(Y)?$")>;
def : InstRW<[WLat15, WLat15, FXa2, FXb4, LSU3, GroupAlone3],
             (instregex "CDSG$")>;

// Compare and swap and store
def : InstRW<[WLat30, MCD], (instregex "CSST$")>;

// Perform locked operation
def : InstRW<[WLat30, MCD], (instregex "PLO$")>;

// Load/store pair from/to quadword
def : InstRW<[WLat4LSU, LSU2, GroupAlone], (instregex "LPQ$")>;
def : InstRW<[WLat1, FXb2, LSU, GroupAlone], (instregex "STPQ$")>;

// Load pair disjoint
def : InstRW<[WLat1LSU, WLat1LSU, LSU2, GroupAlone], (instregex "LPD(G)?$")>;

//===----------------------------------------------------------------------===//
// Translate and convert
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, LSU5, GroupAlone], (instregex "TR$")>;
def : InstRW<[WLat30, WLat30, WLat30, FXa3, LSU2, GroupAlone2],
             (instregex "TRT$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "TRTR$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "TRE$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "TRT(R)?E(Opt)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "TR(T|O)(T|O)(Opt)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD],
             (instregex "CU(12|14|21|24|41|42)(Opt)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "(CUUTF|CUTFU)(Opt)?$")>;

//===----------------------------------------------------------------------===//
// Message-security assist
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, WLat30, WLat30, WLat30, MCD],
             (instregex "KM(C|F|O|CTR)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD],
             (instregex "(KIMD|KLMD|KMAC|PCC|PPNO)$")>;

//===----------------------------------------------------------------------===//
// Decimal arithmetic
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, RegReadAdv, FXb, VecDF2, LSU2, GroupAlone2],
             (instregex "CVBG$")>;
def : InstRW<[WLat30, RegReadAdv, FXb, VecDF, LSU, GroupAlone2],
             (instregex "CVB(Y)?$")>;
def : InstRW<[WLat1, FXb3, VecDF4, LSU, GroupAlone3], (instregex "CVDG$")>;
def : InstRW<[WLat1, FXb2, VecDF, LSU, GroupAlone2], (instregex "CVD(Y)?$")>;
def : InstRW<[WLat1, LSU5, GroupAlone], (instregex "MV(N|O|Z)$")>;
def : InstRW<[WLat1, LSU5, GroupAlone], (instregex "(PACK|PKA|PKU)$")>;
def : InstRW<[WLat12, LSU5, GroupAlone], (instregex "UNPK(A|U)$")>;
def : InstRW<[WLat1, FXb, LSU2, Cracked], (instregex "UNPK$")>;

def : InstRW<[WLat5LSU, FXb, VecDFX, LSU3, GroupAlone2],
             (instregex "(A|S|ZA)P$")>;
def : InstRW<[WLat1, FXb, VecDFX4, LSU3, GroupAlone2], (instregex "(M|D)P$")>;
def : InstRW<[WLat15, FXb, VecDFX2, LSU2, GroupAlone3], (instregex "SRP$")>;
def : InstRW<[WLat8, VecDFX, LSU, LSU, GroupAlone], (instregex "CP$")>;
def : InstRW<[WLat3LSU, VecDFX, LSU, Cracked], (instregex "TP$")>;
def : InstRW<[WLat30, MCD], (instregex "ED(MK)?$")>;

//===----------------------------------------------------------------------===//
// Access registers
//===----------------------------------------------------------------------===//

// Extract/set/copy access register
def : InstRW<[WLat3, LSU, NormalGr], (instregex "(EAR|SAR|CPYA)$")>;

// Load address extended
def : InstRW<[WLat5, LSU, FXa, Cracked], (instregex "LAE(Y)?$")>;

// Load/store access multiple (not modeled precisely)
def : InstRW<[WLat20, WLat20, LSU5, GroupAlone], (instregex "LAM(Y)?$")>;
def : InstRW<[WLat1, LSU5, FXb, GroupAlone2], (instregex "STAM(Y)?$")>;

//===----------------------------------------------------------------------===//
// Program mask and addressing mode
//===----------------------------------------------------------------------===//

// Insert Program Mask
def : InstRW<[WLat3, FXa, EndGroup], (instregex "IPM$")>;

// Set Program Mask
def : InstRW<[WLat3, LSU, EndGroup], (instregex "SPM$")>;

// Branch and link
def : InstRW<[WLat1, FXa2, FXb, GroupAlone], (instregex "BAL(R)?$")>;

// Test addressing mode
def : InstRW<[WLat1, FXb, NormalGr], (instregex "TAM$")>;

// Set addressing mode
def : InstRW<[WLat1, FXb, EndGroup], (instregex "SAM(24|31|64)$")>;

// Branch (and save) and set mode.
def : InstRW<[WLat1, FXa, FXb, GroupAlone], (instregex "BSM$")>;
def : InstRW<[WLat1, FXa2, FXb, GroupAlone], (instregex "BASSM$")>;

//===----------------------------------------------------------------------===//
// Transactional execution
//===----------------------------------------------------------------------===//

// Transaction begin
def : InstRW<[WLat9, LSU2, FXb5, GroupAlone2], (instregex "TBEGIN(C)?$")>;

// Transaction end
def : InstRW<[WLat1, FXb, GroupAlone], (instregex "TEND$")>;

// Transaction abort
def : InstRW<[WLat30, MCD], (instregex "TABORT$")>;

// Extract Transaction Nesting Depth
def : InstRW<[WLat1, FXa, NormalGr], (instregex "ETND$")>;

// Nontransactional store
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "NTSTG$")>;

//===----------------------------------------------------------------------===//
// Processor assist
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "PPA$")>;

//===----------------------------------------------------------------------===//
// Miscellaneous Instructions.
//===----------------------------------------------------------------------===//

// Find leftmost one
def : InstRW<[WLat7, WLat7, FXa2, GroupAlone], (instregex "FLOGR$")>;

// Population count
def : InstRW<[WLat3, WLat3, FXa, NormalGr], (instregex "POPCNT$")>;

// String instructions
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "SRST(U)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "CUSE$")>;

// Various complex instructions
def : InstRW<[WLat30, WLat30, WLat30, WLat30, MCD], (instregex "CFC$")>;
def : InstRW<[WLat30, WLat30, WLat30, WLat30, WLat30, WLat30, MCD],
             (instregex "UPT$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "CKSM$")>;
def : InstRW<[WLat30, WLat30, WLat30, WLat30, MCD], (instregex "CMPSC$")>;

// Execute
def : InstRW<[WLat1, FXb, GroupAlone], (instregex "EX(RL)?$")>;

//===----------------------------------------------------------------------===//
// .insn directive instructions
//===----------------------------------------------------------------------===//

// An "empty" sched-class will be assigned instead of the "invalid sched-class".
// getNumDecoderSlots() will then return 1 instead of 0.
def : InstRW<[], (instregex "Insn.*")>;


// ----------------------------- Floating point ----------------------------- //

//===----------------------------------------------------------------------===//
// FP: Move instructions
//===----------------------------------------------------------------------===//

// Load zero
def : InstRW<[WLat1, FXb, NormalGr], (instregex "LZ(DR|ER)$")>;
def : InstRW<[WLat2, FXb2, Cracked], (instregex "LZXR$")>;

// Load
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "LER$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "LD(R|R32|GR)$")>;
def : InstRW<[WLat3, FXb, NormalGr], (instregex "LGDR$")>;
def : InstRW<[WLat2, FXb2, GroupAlone], (instregex "LXR$")>;

// Load and Test
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "LT(E|D)BR$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "LTXBR$")>;

// Copy sign
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "CPSDR(d|s)(d|s)$")>;

//===----------------------------------------------------------------------===//
// FP: Load instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2LSU, VecXsPm, LSU, NormalGr], (instregex "LE(Y)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LD(Y|E32)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LX$")>;

//===----------------------------------------------------------------------===//
// FP: Store instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "ST(E|D)(Y)?$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "STX$")>;

//===----------------------------------------------------------------------===//
// FP: Conversion instructions
//===----------------------------------------------------------------------===//

// Load rounded
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "LEDBR(A)?$")>;
def : InstRW<[WLat9, VecDF2, NormalGr], (instregex "L(E|D)XBR(A)?$")>;

// Load lengthened
def : InstRW<[WLat7LSU, VecBF, LSU, NormalGr], (instregex "LDEB$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "LDEBR$")>;
def : InstRW<[WLat8LSU, VecBF4, LSU, GroupAlone], (instregex "LX(E|D)B$")>;
def : InstRW<[WLat8, VecBF4, GroupAlone], (instregex "LX(E|D)BR$")>;

// Convert from fixed / logical
def : InstRW<[WLat8, FXb, VecBF, Cracked], (instregex "C(E|D)(F|G)BR(A)?$")>;
def : InstRW<[WLat11, FXb, VecDF4, GroupAlone2], (instregex "CX(F|G)BR(A)?$")>;
def : InstRW<[WLat8, FXb, VecBF, Cracked], (instregex "C(E|D)L(F|G)BR$")>;
def : InstRW<[WLat11, FXb, VecDF4, GroupAlone2], (instregex "CXL(F|G)BR$")>;

// Convert to fixed / logical
def : InstRW<[WLat10, WLat10, FXb, VecBF, Cracked],
             (instregex "C(F|G)(E|D)BR(A)?$")>;
def : InstRW<[WLat12, WLat12, FXb, VecDF2, Cracked],
             (instregex "C(F|G)XBR(A)?$")>;
def : InstRW<[WLat10, WLat10, FXb, VecBF, GroupAlone], (instregex "CLFEBR$")>;
def : InstRW<[WLat10, WLat10, FXb, VecBF, Cracked], (instregex "CLFDBR$")>;
def : InstRW<[WLat10, WLat10, FXb, VecBF, Cracked], (instregex "CLG(E|D)BR$")>;
def : InstRW<[WLat12, WLat12, FXb, VecDF2, Cracked], (instregex "CL(F|G)XBR$")>;

//===----------------------------------------------------------------------===//
// FP: Unary arithmetic
//===----------------------------------------------------------------------===//

// Load Complement / Negative / Positive
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "L(C|N|P)(E|D)BR$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "L(C|N|P)DFR(_32)?$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "L(C|N|P)XBR$")>;

// Square root
def : InstRW<[WLat30, VecFPd, LSU, NormalGr], (instregex "SQ(E|D)B$")>;
def : InstRW<[WLat30, VecFPd, NormalGr], (instregex "SQ(E|D)BR$")>;
def : InstRW<[WLat30, VecFPd, GroupAlone], (instregex "SQXBR$")>;

// Load FP integer
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "FI(E|D)BR(A)?$")>;
def : InstRW<[WLat10, VecDF4, GroupAlone], (instregex "FIXBR(A)?$")>;

//===----------------------------------------------------------------------===//
// FP: Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition
def : InstRW<[WLat7LSU, WLat7LSU, RegReadAdv, VecBF, LSU, NormalGr],
             (instregex "A(E|D)B$")>;
def : InstRW<[WLat7, WLat7, VecBF, NormalGr], (instregex "A(E|D)BR$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "AXBR$")>;

// Subtraction
def : InstRW<[WLat7LSU, WLat7LSU, RegReadAdv, VecBF, LSU, NormalGr],
             (instregex "S(E|D)B$")>;
def : InstRW<[WLat7, WLat7, VecBF, NormalGr], (instregex "S(E|D)BR$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "SXBR$")>;

// Multiply
def : InstRW<[WLat7LSU, RegReadAdv, VecBF, LSU, NormalGr],
             (instregex "M(D|DE|EE)B$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "M(D|DE|EE)BR$")>;
def : InstRW<[WLat8LSU, RegReadAdv, VecBF4, LSU, GroupAlone],
             (instregex "MXDB$")>;
def : InstRW<[WLat8, VecBF4, GroupAlone], (instregex "MXDBR$")>;
def : InstRW<[WLat20, VecDF4, GroupAlone], (instregex "MXBR$")>;

// Multiply and add / subtract
def : InstRW<[WLat7LSU, RegReadAdv, RegReadAdv, VecBF2, LSU, GroupAlone],
             (instregex "M(A|S)EB$")>;
def : InstRW<[WLat7, VecBF, GroupAlone], (instregex "M(A|S)EBR$")>;
def : InstRW<[WLat7LSU, RegReadAdv, RegReadAdv, VecBF2, LSU, GroupAlone],
             (instregex "M(A|S)DB$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "M(A|S)DBR$")>;

// Division
def : InstRW<[WLat30, RegReadAdv, VecFPd, LSU, NormalGr],
             (instregex "D(E|D)B$")>;
def : InstRW<[WLat30, VecFPd, NormalGr], (instregex "D(E|D)BR$")>;
def : InstRW<[WLat30, VecFPd, GroupAlone], (instregex "DXBR$")>;

// Divide to integer
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "DI(E|D)BR$")>;

//===----------------------------------------------------------------------===//
// FP: Comparisons
//===----------------------------------------------------------------------===//

// Compare
def : InstRW<[WLat3LSU, RegReadAdv, VecXsPm, LSU, NormalGr],
             (instregex "(K|C)(E|D)B$")>;
def : InstRW<[WLat3, VecXsPm, NormalGr], (instregex "(K|C)(E|D)BR$")>;
def : InstRW<[WLat9, VecDF2, GroupAlone], (instregex "(K|C)XBR$")>;

// Test Data Class
def : InstRW<[WLat5, LSU, VecXsPm, NormalGr], (instregex "TC(E|D)B$")>;
def : InstRW<[WLat10, LSU, VecDF4, GroupAlone], (instregex "TCXB$")>;

//===----------------------------------------------------------------------===//
// FP: Floating-point control register instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat4, FXa, LSU, GroupAlone], (instregex "EFPC$")>;
def : InstRW<[WLat1, FXb, LSU, GroupAlone], (instregex "STFPC$")>;
def : InstRW<[WLat3, LSU, GroupAlone], (instregex "SFPC$")>;
def : InstRW<[WLat3LSU, LSU2, GroupAlone], (instregex "LFPC$")>;
def : InstRW<[WLat30, MCD], (instregex "SFASR$")>;
def : InstRW<[WLat30, MCD], (instregex "LFAS$")>;
def : InstRW<[WLat3, FXb, GroupAlone], (instregex "SRNM(B|T)?$")>;


// --------------------- Hexadecimal floating point ------------------------- //

//===----------------------------------------------------------------------===//
// HFP: Move instructions
//===----------------------------------------------------------------------===//

// Load and Test
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "LT(E|D)R$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "LTXR$")>;

//===----------------------------------------------------------------------===//
// HFP: Conversion instructions
//===----------------------------------------------------------------------===//

// Load rounded
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "(LEDR|LRER)$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "LEXR$")>;
def : InstRW<[WLat9, VecDF2, NormalGr], (instregex "(LDXR|LRDR)$")>;

// Load lengthened
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "LDE$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "LDER$")>;
def : InstRW<[WLat8LSU, VecBF4, LSU, GroupAlone], (instregex "LX(E|D)$")>;
def : InstRW<[WLat8, VecBF4, GroupAlone], (instregex "LX(E|D)R$")>;

// Convert from fixed
def : InstRW<[WLat8, FXb, VecBF, Cracked], (instregex "C(E|D)(F|G)R$")>;
def : InstRW<[WLat11, FXb, VecDF4, GroupAlone2], (instregex "CX(F|G)R$")>;

// Convert to fixed
def : InstRW<[WLat10, WLat10, FXb, VecBF, Cracked], (instregex "C(F|G)(E|D)R$")>;
def : InstRW<[WLat12, WLat12, FXb, VecDF2, Cracked], (instregex "C(F|G)XR$")>;

// Convert BFP to HFP / HFP to BFP.
def : InstRW<[WLat7, WLat7, VecBF, NormalGr], (instregex "THD(E)?R$")>;
def : InstRW<[WLat7, WLat7, VecBF, NormalGr], (instregex "TB(E)?DR$")>;

//===----------------------------------------------------------------------===//
// HFP: Unary arithmetic
//===----------------------------------------------------------------------===//

// Load Complement / Negative / Positive
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "L(C|N|P)(E|D)R$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "L(C|N|P)XR$")>;

// Halve
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "H(E|D)R$")>;

// Square root
def : InstRW<[WLat30, VecFPd, LSU, NormalGr], (instregex "SQ(E|D)$")>;
def : InstRW<[WLat30, VecFPd, NormalGr], (instregex "SQ(E|D)R$")>;
def : InstRW<[WLat30, VecFPd, GroupAlone], (instregex "SQXR$")>;

// Load FP integer
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "FI(E|D)R$")>;
def : InstRW<[WLat10, VecDF4, GroupAlone], (instregex "FIXR$")>;

//===----------------------------------------------------------------------===//
// HFP: Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition
def : InstRW<[WLat7LSU, WLat7LSU, RegReadAdv, VecBF, LSU, NormalGr],
             (instregex "A(E|D|U|W)$")>;
def : InstRW<[WLat7, WLat7, VecBF, NormalGr], (instregex "A(E|D|U|W)R$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "AXR$")>;

// Subtraction
def : InstRW<[WLat7LSU, WLat7LSU, RegReadAdv, VecBF, LSU, NormalGr],
             (instregex "S(E|D|U|W)$")>;
def : InstRW<[WLat7, WLat7, VecBF, NormalGr], (instregex "S(E|D|U|W)R$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "SXR$")>;

// Multiply
def : InstRW<[WLat7LSU, RegReadAdv, VecBF, LSU, NormalGr],
             (instregex "M(D|DE|E|EE)$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "M(D|DE|E|EE)R$")>;
def : InstRW<[WLat8LSU, RegReadAdv, VecBF4, LSU, GroupAlone],
             (instregex "MXD$")>;
def : InstRW<[WLat8, VecBF4, GroupAlone], (instregex "MXDR$")>;
def : InstRW<[WLat30, VecDF4, GroupAlone], (instregex "MXR$")>;
def : InstRW<[WLat8LSU, RegReadAdv, VecBF4, LSU, GroupAlone],
             (instregex "MY$")>;
def : InstRW<[WLat7LSU, RegReadAdv, VecBF2, LSU, GroupAlone],
             (instregex "MY(H|L)$")>;
def : InstRW<[WLat8, VecBF4, GroupAlone], (instregex "MYR$")>;
def : InstRW<[WLat7, VecBF, GroupAlone], (instregex "MY(H|L)R$")>;

// Multiply and add / subtract
def : InstRW<[WLat7LSU, RegReadAdv, RegReadAdv, VecBF2, LSU, GroupAlone],
             (instregex "M(A|S)(E|D)$")>;
def : InstRW<[WLat7, VecBF, GroupAlone], (instregex "M(A|S)(E|D)R$")>;
def : InstRW<[WLat8LSU, RegReadAdv, RegReadAdv, VecBF4, LSU, GroupAlone],
             (instregex "MAY$")>;
def : InstRW<[WLat7LSU, RegReadAdv, RegReadAdv, VecBF2, LSU, GroupAlone],
             (instregex "MAY(H|L)$")>;
def : InstRW<[WLat8, VecBF4, GroupAlone], (instregex "MAYR$")>;
def : InstRW<[WLat7, VecBF, GroupAlone], (instregex "MAY(H|L)R$")>;

// Division
def : InstRW<[WLat30, RegReadAdv, VecFPd, LSU, NormalGr],
             (instregex "D(E|D)$")>;
def : InstRW<[WLat30, VecFPd, NormalGr], (instregex "D(E|D)R$")>;
def : InstRW<[WLat30, VecFPd, GroupAlone], (instregex "DXR$")>;

//===----------------------------------------------------------------------===//
// HFP: Comparisons
//===----------------------------------------------------------------------===//

// Compare
def : InstRW<[WLat7LSU, RegReadAdv, VecBF, LSU, NormalGr],
             (instregex "C(E|D)$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "C(E|D)R$")>;
def : InstRW<[WLat10, VecDF2, GroupAlone], (instregex "CXR$")>;


// ------------------------ Decimal floating point -------------------------- //

//===----------------------------------------------------------------------===//
// DFP: Move instructions
//===----------------------------------------------------------------------===//

// Load and Test
def : InstRW<[WLat8, WLat8, VecDF, NormalGr], (instregex "LTDTR$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "LTXTR$")>;

//===----------------------------------------------------------------------===//
// DFP: Conversion instructions
//===----------------------------------------------------------------------===//

// Load rounded
def : InstRW<[WLat15, VecDF, NormalGr], (instregex "LEDTR$")>;
def : InstRW<[WLat15, VecDF2, NormalGr], (instregex "LDXTR$")>;

// Load lengthened
def : InstRW<[WLat8, VecDF, NormalGr], (instregex "LDETR$")>;
def : InstRW<[WLat10, VecDF4, GroupAlone], (instregex "LXDTR$")>;

// Convert from fixed / logical
def : InstRW<[WLat30, FXb, VecDF, Cracked], (instregex "CD(F|G)TR(A)?$")>;
def : InstRW<[WLat30, FXb, VecDF4, GroupAlone2], (instregex "CX(F|G)TR(A)?$")>;
def : InstRW<[WLat30, FXb, VecDF, Cracked], (instregex "CDL(F|G)TR$")>;
def : InstRW<[WLat30, FXb, VecDF4, GroupAlone2], (instregex "CXL(F|G)TR$")>;

// Convert to fixed / logical
def : InstRW<[WLat30, WLat30, FXb, VecDF, Cracked],
             (instregex "C(F|G)DTR(A)?$")>;
def : InstRW<[WLat30, WLat30, FXb, VecDF2, Cracked],
             (instregex "C(F|G)XTR(A)?$")>;
def : InstRW<[WLat30, WLat30, FXb, VecDF, Cracked], (instregex "CL(F|G)DTR$")>;
def : InstRW<[WLat30, WLat30, FXb, VecDF2, Cracked], (instregex "CL(F|G)XTR$")>;

// Convert from / to signed / unsigned packed
def : InstRW<[WLat9, FXb, VecDF, Cracked], (instregex "CD(S|U)TR$")>;
def : InstRW<[WLat12, FXb2, VecDF4, GroupAlone2], (instregex "CX(S|U)TR$")>;
def : InstRW<[WLat11, FXb, VecDF, Cracked], (instregex "C(S|U)DTR$")>;
def : InstRW<[WLat15, FXb2, VecDF4, GroupAlone2], (instregex "C(S|U)XTR$")>;

// Convert from / to zoned
def : InstRW<[WLat8LSU, LSU, VecDF, Cracked], (instregex "CDZT$")>;
def : InstRW<[WLat16LSU, LSU2, VecDF4, GroupAlone3], (instregex "CXZT$")>;
def : InstRW<[WLat1, FXb, LSU, VecDF, Cracked], (instregex "CZDT$")>;
def : InstRW<[WLat1, FXb, LSU, VecDF2, GroupAlone], (instregex "CZXT$")>;

// Convert from / to packed
def : InstRW<[WLat8LSU, LSU, VecDF, Cracked], (instregex "CDPT$")>;
def : InstRW<[WLat16LSU, LSU2, VecDF4, GroupAlone3], (instregex "CXPT$")>;
def : InstRW<[WLat1, FXb, LSU, VecDF, Cracked], (instregex "CPDT$")>;
def : InstRW<[WLat1, FXb, LSU, VecDF2, GroupAlone], (instregex "CPXT$")>;

// Perform floating-point operation
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "PFPO$")>;

//===----------------------------------------------------------------------===//
// DFP: Unary arithmetic
//===----------------------------------------------------------------------===//

// Load FP integer
def : InstRW<[WLat8, VecDF, NormalGr], (instregex "FIDTR$")>;
def : InstRW<[WLat10, VecDF4, GroupAlone], (instregex "FIXTR$")>;

// Extract biased exponent
def : InstRW<[WLat11, FXb, VecDF, Cracked], (instregex "EEDTR$")>;
def : InstRW<[WLat11, FXb, VecDF, Cracked], (instregex "EEXTR$")>;

// Extract significance
def : InstRW<[WLat11, FXb, VecDF, Cracked], (instregex "ESDTR$")>;
def : InstRW<[WLat12, FXb, VecDF2, Cracked], (instregex "ESXTR$")>;

//===----------------------------------------------------------------------===//
// DFP: Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition
def : InstRW<[WLat8, WLat8, VecDF, NormalGr], (instregex "ADTR(A)?$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "AXTR(A)?$")>;

// Subtraction
def : InstRW<[WLat8, WLat8, VecDF, NormalGr], (instregex "SDTR(A)?$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "SXTR(A)?$")>;

// Multiply
def : InstRW<[WLat30, VecDF, NormalGr], (instregex "MDTR(A)?$")>;
def : InstRW<[WLat30, VecDF4, GroupAlone], (instregex "MXTR(A)?$")>;

// Division
def : InstRW<[WLat30, VecDF, NormalGr], (instregex "DDTR(A)?$")>;
def : InstRW<[WLat30, VecDF4, GroupAlone], (instregex "DXTR(A)?$")>;

// Quantize
def : InstRW<[WLat8, WLat8, VecDF, NormalGr], (instregex "QADTR$")>;
def : InstRW<[WLat10, WLat10, VecDF4, GroupAlone], (instregex "QAXTR$")>;

// Reround
def : InstRW<[WLat9, WLat9, FXb, VecDF, Cracked], (instregex "RRDTR$")>;
def : InstRW<[WLat11, WLat11, FXb, VecDF4, GroupAlone2], (instregex "RRXTR$")>;

// Shift significand left/right
def : InstRW<[WLat11LSU, LSU, VecDF, GroupAlone], (instregex "S(L|R)DT$")>;
def : InstRW<[WLat11LSU, LSU, VecDF4, GroupAlone], (instregex "S(L|R)XT$")>;

// Insert biased exponent
def : InstRW<[WLat9, FXb, VecDF, Cracked], (instregex "IEDTR$")>;
def : InstRW<[WLat11, FXb, VecDF4, GroupAlone2], (instregex "IEXTR$")>;

//===----------------------------------------------------------------------===//
// DFP: Comparisons
//===----------------------------------------------------------------------===//

// Compare
def : InstRW<[WLat8, VecDF, NormalGr], (instregex "(K|C)DTR$")>;
def : InstRW<[WLat9, VecDF2, GroupAlone], (instregex "(K|C)XTR$")>;

// Compare biased exponent
def : InstRW<[WLat8, VecDF, NormalGr], (instregex "CEDTR$")>;
def : InstRW<[WLat8, VecDF, NormalGr], (instregex "CEXTR$")>;

// Test Data Class/Group
def : InstRW<[WLat15, LSU, VecDF, NormalGr], (instregex "TD(C|G)(E|D)T$")>;
def : InstRW<[WLat15, LSU, VecDF2, GroupAlone], (instregex "TD(C|G)XT$")>;


// --------------------------------- Vector --------------------------------- //

//===----------------------------------------------------------------------===//
// Vector: Move instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXb, NormalGr], (instregex "VLR(32|64)?$")>;
def : InstRW<[WLat4, FXb, NormalGr], (instregex "VLGV(B|F|G|H)?$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "VLVG(B|F|G|H)?$")>;
def : InstRW<[WLat3, FXb, NormalGr], (instregex "VLVGP(32)?$")>;

//===----------------------------------------------------------------------===//
// Vector: Immediate instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VZERO$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VONE$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VGBM$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VGM(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VREPI(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VLEI(B|F|G|H)$")>;

//===----------------------------------------------------------------------===//
// Vector: Loads
//===----------------------------------------------------------------------===//

def : InstRW<[LSULatency, LSU, NormalGr], (instregex "VL(Align)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "VL(L|BB)$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "VL(32|64)$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "VLLEZ(B|F|G|H)?$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "VLREP(B|F|G|H)?$")>;
def : InstRW<[WLat2LSU, RegReadAdv, VecXsPm, LSU, NormalGr],
             (instregex "VLE(B|F|G|H)$")>;
def : InstRW<[WLat6LSU, RegReadAdv, FXb, LSU, VecXsPm, Cracked],
             (instregex "VGE(F|G)$")>;
def : InstRW<[WLat4LSU, WLat4LSU, LSU5, GroupAlone],
             (instregex "VLM(Align)?$")>;

//===----------------------------------------------------------------------===//
// Vector: Stores
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "VST(Align|L|32|64)?$")>;
def : InstRW<[WLat1, FXb, LSU, NormalGr], (instregex "VSTE(F|G)$")>;
def : InstRW<[WLat1, FXb, LSU, VecXsPm, Cracked], (instregex "VSTE(B|H)$")>;
def : InstRW<[WLat1, LSU2, FXb3, GroupAlone2], (instregex "VSTM(Align)?$")>;
def : InstRW<[WLat1, FXb2, LSU, Cracked], (instregex "VSCE(F|G)$")>;

//===----------------------------------------------------------------------===//
// Vector: Selects and permutes
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VMRH(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VMRL(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VPERM$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VPDI$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VREP(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VSEL$")>;

//===----------------------------------------------------------------------===//
// Vector: Widening and narrowing
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VPK(F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VPKS(F|G|H)?$")>;
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "VPKS(F|G|H)S$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VPKLS(F|G|H)?$")>;
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "VPKLS(F|G|H)S$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VSEG(B|F|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VUPH(B|F|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VUPL(B|F)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VUPLH(B|F|H|W)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VUPLL(B|F|H)?$")>;

//===----------------------------------------------------------------------===//
// Vector: Integer arithmetic
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VA(B|F|G|H|Q|C|CQ)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VACC(B|F|G|H|Q|C|CQ)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VAVG(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VAVGL(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VN(C|O)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VO$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VCKSM$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VCLZ(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VCTZ(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VX$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VGFM?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VGFMA(B|F|G|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VGFM(B|F|G|H)$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VLC(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VLP(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VMX(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VMXL(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VMN(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VMNL(B|F|G|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMAL(B|F)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMALE(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMALH(B|F|H|W)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMALO(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMAO(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMAE(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMAH(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VME(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMH(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VML(B|F)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMLE(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMLH(B|F|H|W)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMLO(B|F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VMO(B|F|H)?$")>;

def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VPOPCT$")>;

def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VERLL(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VERLLV(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VERIM(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VESL(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VESLV(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VESRA(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VESRAV(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VESRL(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VESRLV(B|F|G|H)?$")>;

def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VSL(DB)?$")>;
def : InstRW<[WLat3, VecXsPm2, NormalGr], (instregex "VSLB$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VSR(A|L)$")>;
def : InstRW<[WLat3, VecXsPm2, NormalGr], (instregex "VSR(A|L)B$")>;

def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VSB(I|IQ|CBI|CBIQ)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VSCBI(B|F|G|H|Q)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VS(F|G|H|Q)?$")>;

def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VSUM(B|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VSUMG(F|H)?$")>;
def : InstRW<[WLat4, VecMul, NormalGr], (instregex "VSUMQ(F|G)?$")>;

//===----------------------------------------------------------------------===//
// Vector: Integer comparison
//===----------------------------------------------------------------------===//

def : InstRW<[WLat3, VecXsPm, NormalGr], (instregex "VEC(B|F|G|H)?$")>;
def : InstRW<[WLat3, VecXsPm, NormalGr], (instregex "VECL(B|F|G|H)?$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VCEQ(B|F|G|H)?$")>;
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "VCEQ(B|F|G|H)S$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VCH(B|F|G|H)?$")>;
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "VCH(B|F|G|H)S$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VCHL(B|F|G|H)?$")>;
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "VCHL(B|F|G|H)S$")>;
def : InstRW<[WLat4, VecStr, NormalGr], (instregex "VTM$")>;

//===----------------------------------------------------------------------===//
// Vector: Floating-point arithmetic
//===----------------------------------------------------------------------===//

// Conversion and rounding
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VCD(L)?G$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VCD(L)?GB$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "WCD(L)?GB$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VC(L)?GD$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VC(L)?GDB$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "WC(L)?GDB$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VL(DE|ED)$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VL(DE|ED)B$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "WL(DE|ED)B$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VFI$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VFIDB$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "WFIDB$")>;

// Sign operations
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "VFPSO$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "(V|W)FPSODB$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "(V|W)FL(C|N|P)DB$")>;

// Test data class
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "VFTCI$")>;
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "(V|W)FTCIDB$")>;

// Add / subtract
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VF(A|S)$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VF(A|S)DB$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "WF(A|S)DB$")>;

// Multiply / multiply-and-add/subtract
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VFM$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VFMDB$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "WFMDB$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VFM(A|S)$")>;
def : InstRW<[WLat8, VecBF2, NormalGr], (instregex "VFM(A|S)DB$")>;
def : InstRW<[WLat7, VecBF, NormalGr], (instregex "WFM(A|S)DB$")>;

// Divide / square root
def : InstRW<[WLat30, VecFPd, NormalGr], (instregex "VFD$")>;
def : InstRW<[WLat30, VecFPd, NormalGr], (instregex "(V|W)FDDB$")>;
def : InstRW<[WLat30, VecFPd, NormalGr], (instregex "VFSQ$")>;
def : InstRW<[WLat30, VecFPd, NormalGr], (instregex "(V|W)FSQDB$")>;

//===----------------------------------------------------------------------===//
// Vector: Floating-point comparison
//===----------------------------------------------------------------------===//

def : InstRW<[WLat2, WLat2, VecXsPm, NormalGr], (instregex "VFC(E|H|HE)$")>;
def : InstRW<[WLat2, WLat2, VecXsPm, NormalGr], (instregex "VFC(E|H|HE)DB$")>;
def : InstRW<[WLat2, VecXsPm, NormalGr], (instregex "WFC(E|H|HE)DB$")>;
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "VFC(E|H|HE)DBS$")>;
def : InstRW<[WLat3, WLat3, VecXsPm, NormalGr], (instregex "WFC(E|H|HE)DBS$")>;
def : InstRW<[WLat3, VecXsPm, NormalGr], (instregex "WF(C|K)$")>;
def : InstRW<[WLat3, VecXsPm, NormalGr], (instregex "WF(C|K)DB$")>;

//===----------------------------------------------------------------------===//
// Vector: Floating-point insertion and extraction
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXb, NormalGr], (instregex "LEFR$")>;
def : InstRW<[WLat4, FXb, NormalGr], (instregex "LFER$")>;

//===----------------------------------------------------------------------===//
// Vector: String instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat3, VecStr, NormalGr], (instregex "VFAE(B)?$")>;
def : InstRW<[WLat3, VecStr, NormalGr], (instregex "VFAE(F|H)$")>;
def : InstRW<[WLat4, WLat4, VecStr, NormalGr], (instregex "VFAE(B|F|H)S$")>;
def : InstRW<[WLat3, VecStr, NormalGr], (instregex "VFAEZ(B|F|H)$")>;
def : InstRW<[WLat4, WLat4, VecStr, NormalGr], (instregex "VFAEZ(B|F|H)S$")>;
def : InstRW<[WLat3, VecStr, NormalGr], (instregex "VFEE(B|F|H|ZB|ZF|ZH)?$")>;
def : InstRW<[WLat4, WLat4, VecStr, NormalGr],
             (instregex "VFEE(B|F|H|ZB|ZF|ZH)S$")>;
def : InstRW<[WLat3, VecStr, NormalGr], (instregex "VFENE(B|F|H|ZB|ZF|ZH)?$")>;
def : InstRW<[WLat4, WLat4, VecStr, NormalGr],
             (instregex "VFENE(B|F|H|ZB|ZF|ZH)S$")>;
def : InstRW<[WLat3, VecStr, NormalGr], (instregex "VISTR(B|F|H)?$")>;
def : InstRW<[WLat4, WLat4, VecStr, NormalGr], (instregex "VISTR(B|F|H)S$")>;
def : InstRW<[WLat3, VecStr, NormalGr], (instregex "VSTRC(B|F|H)?$")>;
def : InstRW<[WLat4, WLat4, VecStr, NormalGr], (instregex "VSTRC(B|F|H)S$")>;
def : InstRW<[WLat3, VecStr, NormalGr], (instregex "VSTRCZ(B|F|H)$")>;
def : InstRW<[WLat4, WLat4, VecStr, NormalGr], (instregex "VSTRCZ(B|F|H)S$")>;


// -------------------------------- System ---------------------------------- //

//===----------------------------------------------------------------------===//
// System: Program-Status Word Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, WLat30, MCD], (instregex "EPSW$")>;
def : InstRW<[WLat30, MCD], (instregex "LPSW(E)?$")>;
def : InstRW<[WLat3, FXa, GroupAlone], (instregex "IPK$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "SPKA$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "SSM$")>;
def : InstRW<[WLat1, FXb, LSU, GroupAlone], (instregex "ST(N|O)SM$")>;
def : InstRW<[WLat3, FXa, NormalGr], (instregex "IAC$")>;
def : InstRW<[WLat1, LSU, EndGroup], (instregex "SAC(F)?$")>;

//===----------------------------------------------------------------------===//
// System: Control Register Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat4LSU, WLat4LSU, LSU2, GroupAlone], (instregex "LCTL(G)?$")>;
def : InstRW<[WLat1, LSU5, FXb, GroupAlone2], (instregex "STCT(L|G)$")>;
def : InstRW<[LSULatency, LSU, NormalGr], (instregex "E(P|S)A(I)?R$")>;
def : InstRW<[WLat30, MCD], (instregex "SSA(I)?R$")>;
def : InstRW<[WLat30, MCD], (instregex "ESEA$")>;

//===----------------------------------------------------------------------===//
// System: Prefix-Register Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "S(T)?PX$")>;

//===----------------------------------------------------------------------===//
// System: Storage-Key and Real Memory Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "ISKE$")>;
def : InstRW<[WLat30, MCD], (instregex "IVSK$")>;
def : InstRW<[WLat30, MCD], (instregex "SSKE(Opt)?$")>;
def : InstRW<[WLat30, MCD], (instregex "RRB(E|M)$")>;
def : InstRW<[WLat30, MCD], (instregex "PFMF$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "TB$")>;
def : InstRW<[WLat30, MCD], (instregex "PGIN$")>;
def : InstRW<[WLat30, MCD], (instregex "PGOUT$")>;

//===----------------------------------------------------------------------===//
// System: Dynamic-Address-Translation Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "IPTE(Opt)?(Opt)?$")>;
def : InstRW<[WLat30, MCD], (instregex "IDTE(Opt)?$")>;
def : InstRW<[WLat30, MCD], (instregex "CRDTE(Opt)?$")>;
def : InstRW<[WLat30, MCD], (instregex "PTLB$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "CSP(G)?$")>;
def : InstRW<[WLat30, WLat30, WLat30, MCD], (instregex "LPTEA$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "LRA(Y|G)?$")>;
def : InstRW<[WLat30, MCD], (instregex "STRAG$")>;
def : InstRW<[WLat30, MCD], (instregex "LURA(G)?$")>;
def : InstRW<[WLat30, MCD], (instregex "STUR(A|G)$")>;
def : InstRW<[WLat30, MCD], (instregex "TPROT$")>;

//===----------------------------------------------------------------------===//
// System: Memory-move Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat4LSU, FXa2, FXb, LSU5, GroupAlone2], (instregex "MVC(K|P|S)$")>;
def : InstRW<[WLat1, FXa, LSU5, GroupAlone2], (instregex "MVC(S|D)K$")>;
def : InstRW<[WLat30, MCD], (instregex "MVCOS$")>;
def : InstRW<[WLat30, MCD], (instregex "MVPG$")>;

//===----------------------------------------------------------------------===//
// System: Address-Space Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "LASP$")>;
def : InstRW<[WLat1, LSU, GroupAlone], (instregex "PALB$")>;
def : InstRW<[WLat30, MCD], (instregex "PC$")>;
def : InstRW<[WLat30, MCD], (instregex "PR$")>;
def : InstRW<[WLat30, MCD], (instregex "PT(I)?$")>;
def : InstRW<[WLat30, MCD], (instregex "RP$")>;
def : InstRW<[WLat30, MCD], (instregex "BS(G|A)$")>;
def : InstRW<[WLat30, MCD], (instregex "TAR$")>;

//===----------------------------------------------------------------------===//
// System: Linkage-Stack Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "BAKR$")>;
def : InstRW<[WLat30, MCD], (instregex "EREG(G)?$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "(E|M)STA$")>;

//===----------------------------------------------------------------------===//
// System: Time-Related Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "PTFF$")>;
def : InstRW<[WLat30, MCD], (instregex "SCK(PF|C)?$")>;
def : InstRW<[WLat1, LSU2, GroupAlone], (instregex "SPT$")>;
def : InstRW<[WLat15, LSU3, FXa2, FXb, GroupAlone2], (instregex "STCK(F)?$")>;
def : InstRW<[WLat20, LSU4, FXa2, FXb2, GroupAlone3], (instregex "STCKE$")>;
def : InstRW<[WLat30, MCD], (instregex "STCKC$")>;
def : InstRW<[WLat1, LSU2, FXb, Cracked], (instregex "STPT$")>;

//===----------------------------------------------------------------------===//
// System: CPU-Related Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "STAP$")>;
def : InstRW<[WLat30, MCD], (instregex "STIDP$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "STSI$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "STFL(E)?$")>;
def : InstRW<[WLat30, MCD], (instregex "ECAG$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "ECTG$")>;
def : InstRW<[WLat30, MCD], (instregex "PTF$")>;
def : InstRW<[WLat30, MCD], (instregex "PCKMO$")>;

//===----------------------------------------------------------------------===//
// System: Miscellaneous Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "SVC$")>;
def : InstRW<[WLat1, FXb, GroupAlone], (instregex "MC$")>;
def : InstRW<[WLat30, MCD], (instregex "DIAG$")>;
def : InstRW<[WLat1, FXb, NormalGr], (instregex "TRAC(E|G)$")>;
def : InstRW<[WLat30, MCD], (instregex "TRAP(2|4)$")>;
def : InstRW<[WLat30, MCD], (instregex "SIG(P|A)$")>;
def : InstRW<[WLat30, MCD], (instregex "SIE$")>;

//===----------------------------------------------------------------------===//
// System: CPU-Measurement Facility Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXb, NormalGr], (instregex "LPP$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "ECPGA$")>;
def : InstRW<[WLat30, WLat30, MCD], (instregex "E(C|P)CTR$")>;
def : InstRW<[WLat30, MCD], (instregex "LCCTL$")>;
def : InstRW<[WLat30, MCD], (instregex "L(P|S)CTL$")>;
def : InstRW<[WLat30, MCD], (instregex "Q(S|CTR)I$")>;
def : InstRW<[WLat30, MCD], (instregex "S(C|P)CTR$")>;

//===----------------------------------------------------------------------===//
// System: I/O Instructions
//===----------------------------------------------------------------------===//

def : InstRW<[WLat30, MCD], (instregex "(C|H|R|X)SCH$")>;
def : InstRW<[WLat30, MCD], (instregex "(M|S|ST|T)SCH$")>;
def : InstRW<[WLat30, MCD], (instregex "RCHP$")>;
def : InstRW<[WLat30, MCD], (instregex "SCHM$")>;
def : InstRW<[WLat30, MCD], (instregex "STC(PS|RW)$")>;
def : InstRW<[WLat30, MCD], (instregex "TPI$")>;
def : InstRW<[WLat30, MCD], (instregex "SAL$")>;

//===----------------------------------------------------------------------===//
// NOPs
//===----------------------------------------------------------------------===//

def : InstRW<[WLat1, FXb, NormalGr], (instregex "NOP(R)?$")>;

}

