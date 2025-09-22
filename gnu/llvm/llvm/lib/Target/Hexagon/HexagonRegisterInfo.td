//===-- HexagonRegisterInfo.td - Hexagon Register defs -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the Hexagon register file.
//===----------------------------------------------------------------------===//

let Namespace = "Hexagon" in {

  class HexagonReg<bits<5> num, string n, list<string> alt = [],
                   list<Register> alias = []> : Register<n, alt> {
    let Aliases = alias;
    let HWEncoding{4-0} = num;
  }

  // These registers are used to preserve a distinction between
  // vector register pairs of differing order.
  class HexagonFakeReg<string n> : Register<n> {
    let isArtificial = 1;
  }

  class HexagonDoubleReg<bits<5> num, string n, list<Register> subregs,
                         list<string> alt = []> :
        RegisterWithSubRegs<n, subregs> {
    let AltNames = alt;
    let HWEncoding{4-0} = num;
  }

  class HexagonSys<bits<7> num, string n, list<string> alt = [],
                   list<Register> alias = []> : Register<n, alt> {
    let Aliases = alias;
    let HWEncoding{6-0} = num;
  }

  class HexagonDoubleSys<bits<7> num, string n, list<Register> subregs,
                         list<string> alt = []> :
        RegisterWithSubRegs<n, subregs> {
    let AltNames = alt;
    let HWEncoding{6-0} = num;
  }

  // Registers are identified with 5-bit ID numbers.
  // Ri - 32-bit integer registers.
  class Ri<bits<5> num, string n, list<string> alt = []> :
        HexagonReg<num, n, alt>;

  // Rp - false/pseudo registers.  These registers are used
  // to provide a distinct set of aliases for both styles of vector
  // register pairs without encountering subregister indexing constraints.
  class R_fake<string n> :
        HexagonFakeReg<n>;


  // Rf - 32-bit floating-point registers.
  class Rf<bits<5> num, string n> : HexagonReg<num, n>;

  // Rd - 64-bit registers.
  class Rd<bits<5> num, string n, list<Register> subregs,
           list<string> alt = []> :
        HexagonDoubleReg<num, n, subregs, alt> {
    let SubRegs = subregs;
  }

  // Rp - predicate registers
  class Rp<bits<5> num, string n> : HexagonReg<num, n>;


  // Rq - vector predicate registers
  class Rq<bits<3> num, string n> : Register<n, []> {
    let HWEncoding{2-0} = num;
  }

  // Rc - control registers
  class Rc<bits<5> num, string n,
           list<string> alt = [], list<Register> alias = []> :
        HexagonReg<num, n, alt, alias>;

  // Rcc - 64-bit control registers.
  class Rcc<bits<5> num, string n, list<Register> subregs,
            list<string> alt = []> :
        HexagonDoubleReg<num, n, subregs, alt> {
    let SubRegs = subregs;
  }

  // Rs - system registers
  class Rs<bits<7> num, string n,
           list<string> alt = [], list<Register> alias = []> :
        HexagonSys<num, n, alt, alias>;

  // Rss - 64-bit system registers.
  class Rss<bits<7> num, string n, list<Register> subregs,
            list<string> alt = []> :
        HexagonDoubleSys<num, n, subregs, alt> {
    let SubRegs = subregs;
  }

  // Mx - address modifier registers
  class Mx<bits<1> num, string n> : Register<n, []> {
    let HWEncoding{0} = num;
  }

  // Rg - Guest/Hypervisor registers
  class Rg<bits<5> num, string n,
           list<string> alt = [], list<Register> alias = []> :
        HexagonReg<num, n, alt, alias>;

  // Rgg - 64-bit Guest/Hypervisor registers
  class Rgg<bits<5> num, string n, list<Register> subregs> :
        HexagonDoubleReg<num, n, subregs> {
    let SubRegs = subregs;
  }

  def isub_lo  : SubRegIndex<32>;
  def isub_hi  : SubRegIndex<32, 32>;
  def vsub_lo  : SubRegIndex<-1, -1>;
  def vsub_hi  : SubRegIndex<-1, -1>;
  def vsub_fake: SubRegIndex<-1, -1>;
  def wsub_lo  : SubRegIndex<-1, -1>;
  def wsub_hi  : SubRegIndex<-1, -1>;
  def subreg_overflow : SubRegIndex<1, 0>;

  // Integer registers.
  foreach i = 0-28 in {
    def R#i  : Ri<i, "r"#i>,  DwarfRegNum<[i]>;
  }
  def R29 : Ri<29, "r29", ["sp"]>, DwarfRegNum<[29]>;
  def R30 : Ri<30, "r30", ["fp"]>, DwarfRegNum<[30]>;
  def R31 : Ri<31, "r31", ["lr"]>, DwarfRegNum<[31]>;

  // Aliases of the R* registers used to hold 64-bit int values (doubles).
  let SubRegIndices = [isub_lo, isub_hi], CoveredBySubRegs = 1 in {
  def D0  : Rd< 0,  "r1:0",  [R0,  R1]>,  DwarfRegNum<[32]>;
  def D1  : Rd< 2,  "r3:2",  [R2,  R3]>,  DwarfRegNum<[34]>;
  def D2  : Rd< 4,  "r5:4",  [R4,  R5]>,  DwarfRegNum<[36]>;
  def D3  : Rd< 6,  "r7:6",  [R6,  R7]>,  DwarfRegNum<[38]>;
  def D4  : Rd< 8,  "r9:8",  [R8,  R9]>,  DwarfRegNum<[40]>;
  def D5  : Rd<10, "r11:10", [R10, R11]>, DwarfRegNum<[42]>;
  def D6  : Rd<12, "r13:12", [R12, R13]>, DwarfRegNum<[44]>;
  def D7  : Rd<14, "r15:14", [R14, R15]>, DwarfRegNum<[46]>;
  def D8  : Rd<16, "r17:16", [R16, R17]>, DwarfRegNum<[48]>;
  def D9  : Rd<18, "r19:18", [R18, R19]>, DwarfRegNum<[50]>;
  def D10 : Rd<20, "r21:20", [R20, R21]>, DwarfRegNum<[52]>;
  def D11 : Rd<22, "r23:22", [R22, R23]>, DwarfRegNum<[54]>;
  def D12 : Rd<24, "r25:24", [R24, R25]>, DwarfRegNum<[56]>;
  def D13 : Rd<26, "r27:26", [R26, R27]>, DwarfRegNum<[58]>;
  def D14 : Rd<28, "r29:28", [R28, R29]>, DwarfRegNum<[60]>;
  def D15 : Rd<30, "r31:30", [R30, R31], ["lr:fp"]>, DwarfRegNum<[62]>;
  }

  // Predicate registers.
  def P0 : Rp<0, "p0">, DwarfRegNum<[63]>;
  def P1 : Rp<1, "p1">, DwarfRegNum<[64]>;
  def P2 : Rp<2, "p2">, DwarfRegNum<[65]>;
  def P3 : Rp<3, "p3">, DwarfRegNum<[66]>;

  // Fake register to represent USR.OVF bit. Arithmetic/saturating instruc-
  // tions modify this bit, and multiple such instructions are allowed in the
  // same packet. We need to ignore output dependencies on this bit, but not
  // on the entire USR.
  def USR_OVF : Rc<?, "usr.ovf">;

  def USR  : Rc<8,  "usr",       ["c8"]>,   DwarfRegNum<[75]> {
    let SubRegIndices = [subreg_overflow];
    let SubRegs = [USR_OVF];
  }

  // Control registers.
  def SA0:        Rc<0,  "sa0",        ["c0"]>,    DwarfRegNum<[67]>;
  def LC0:        Rc<1,  "lc0",        ["c1"]>,    DwarfRegNum<[68]>;
  def SA1:        Rc<2,  "sa1",        ["c2"]>,    DwarfRegNum<[69]>;
  def LC1:        Rc<3,  "lc1",        ["c3"]>,    DwarfRegNum<[70]>;
  def P3_0:       Rc<4,  "p3:0",       ["c4"], [P0, P1, P2, P3]>,
                                                   DwarfRegNum<[71]>;
  // When defining more Cn registers, make sure to explicitly mark them
  // as reserved in HexagonRegisterInfo.cpp.
  def C5:         Rc<5,  "c5",         ["c5"]>,    DwarfRegNum<[72]>;
  def M0:         Rc<6,  "m0",         ["c6"]>,    DwarfRegNum<[73]>;
  def M1:         Rc<7,  "m1",         ["c7"]>,    DwarfRegNum<[74]>;
  // Define C8 separately and make it aliased with USR.
  // The problem is that USR has subregisters (e.g. overflow). If USR was
  // specified as a subregister of C9_8, it would imply that subreg_overflow
  // and isub_lo can be composed, which leads to all kinds of issues
  // with lane masks.
  def C8:         Rc<8,  "c8",         [], [USR]>, DwarfRegNum<[75]>;
  def PC:         Rc<9,  "pc",         ["c9"]>,    DwarfRegNum<[76]>;
  def UGP:        Rc<10, "ugp",        ["c10"]>,   DwarfRegNum<[77]>;
  def GP:         Rc<11, "gp",         ["c11"]>,   DwarfRegNum<[78]>;
  def CS0:        Rc<12, "cs0",        ["c12"]>,   DwarfRegNum<[79]>;
  def CS1:        Rc<13, "cs1",        ["c13"]>,   DwarfRegNum<[80]>;
  def UPCYCLELO:  Rc<14, "upcyclelo",  ["c14"]>,   DwarfRegNum<[81]>;
  def UPCYCLEHI:  Rc<15, "upcyclehi",  ["c15"]>,   DwarfRegNum<[82]>;
  def FRAMELIMIT: Rc<16, "framelimit", ["c16"]>,   DwarfRegNum<[83]>;
  def FRAMEKEY:   Rc<17, "framekey",   ["c17"]>,   DwarfRegNum<[84]>;
  def PKTCOUNTLO: Rc<18, "pktcountlo", ["c18"]>,   DwarfRegNum<[85]>;
  def PKTCOUNTHI: Rc<19, "pktcounthi", ["c19"]>,   DwarfRegNum<[86]>;
  def UTIMERLO:   Rc<30, "utimerlo",   ["c30"]>,   DwarfRegNum<[97]>;
  def UTIMERHI:   Rc<31, "utimerhi",   ["c31"]>,   DwarfRegNum<[98]>;

  // Control registers pairs.
  let SubRegIndices = [isub_lo, isub_hi], CoveredBySubRegs = 1 in {
    def C1_0   : Rcc<0,   "c1:0",  [SA0, LC0], ["lc0:sa0"]>, DwarfRegNum<[67]>;
    def C3_2   : Rcc<2,   "c3:2",  [SA1, LC1], ["lc1:sa1"]>, DwarfRegNum<[69]>;
    def C5_4   : Rcc<4,   "c5:4",  [P3_0, C5]>,              DwarfRegNum<[71]>;
    def C7_6   : Rcc<6,   "c7:6",  [M0, M1],   ["m1:0"]>,    DwarfRegNum<[72]>;
    // Use C8 instead of USR as a subregister of C9_8.
    def C9_8   : Rcc<8,   "c9:8",  [C8, PC]>,                DwarfRegNum<[74]>;
    def C11_10 : Rcc<10, "c11:10", [UGP, GP]>,               DwarfRegNum<[76]>;
    def CS     : Rcc<12, "c13:12", [CS0, CS1], ["cs1:0"]>,   DwarfRegNum<[78]>;
    def UPCYCLE: Rcc<14, "c15:14", [UPCYCLELO, UPCYCLEHI], ["upcycle"]>,
                                                              DwarfRegNum<[80]>;
    def C17_16 : Rcc<16, "c17:16", [FRAMELIMIT, FRAMEKEY]>,  DwarfRegNum<[83]>;
    def PKTCOUNT : Rcc<18, "c19:18", [PKTCOUNTLO, PKTCOUNTHI], ["pktcount"]>,
                                                              DwarfRegNum<[85]>;
    def UTIMER :  Rcc<30, "c31:30", [UTIMERLO, UTIMERHI], ["utimer"]>,
                                                              DwarfRegNum<[97]>;
  }

  foreach i = 0-31 in {
    def V#i  : Ri<i, "v"#i>,  DwarfRegNum<[!add(i, 99)]>;
    def VF#i : R_fake<"__"#!add(i,999999)>,  DwarfRegNum<[!add(i, 999999)]>;
    def VFR#i : R_fake<"__"#!add(i,9999999)>,  DwarfRegNum<[!add(i, 9999999)]>;
  }
  def VTMP : Ri<0, "vtmp">, DwarfRegNum<[131]>;

  // Aliases of the V* registers used to hold double vec values.
  let SubRegIndices = [vsub_lo, vsub_hi, vsub_fake], CoveredBySubRegs = 1 in {
  def W0  : Rd< 0,  "v1:0",  [V0,  V1, VF0]>,  DwarfRegNum<[99]>;
  def W1  : Rd< 2,  "v3:2",  [V2,  V3, VF1]>,  DwarfRegNum<[101]>;
  def W2  : Rd< 4,  "v5:4",  [V4,  V5, VF2]>,  DwarfRegNum<[103]>;
  def W3  : Rd< 6,  "v7:6",  [V6,  V7, VF3]>,  DwarfRegNum<[105]>;
  def W4  : Rd< 8,  "v9:8",  [V8,  V9, VF4]>,  DwarfRegNum<[107]>;
  def W5  : Rd<10, "v11:10", [V10, V11, VF5]>, DwarfRegNum<[109]>;
  def W6  : Rd<12, "v13:12", [V12, V13, VF6]>, DwarfRegNum<[111]>;
  def W7  : Rd<14, "v15:14", [V14, V15, VF7]>, DwarfRegNum<[113]>;
  def W8  : Rd<16, "v17:16", [V16, V17, VF8]>, DwarfRegNum<[115]>;
  def W9  : Rd<18, "v19:18", [V18, V19, VF9]>, DwarfRegNum<[117]>;
  def W10 : Rd<20, "v21:20", [V20, V21, VF10]>, DwarfRegNum<[119]>;
  def W11 : Rd<22, "v23:22", [V22, V23, VF11]>, DwarfRegNum<[121]>;
  def W12 : Rd<24, "v25:24", [V24, V25, VF12]>, DwarfRegNum<[123]>;
  def W13 : Rd<26, "v27:26", [V26, V27, VF13]>, DwarfRegNum<[125]>;
  def W14 : Rd<28, "v29:28", [V28, V29, VF14]>, DwarfRegNum<[127]>;
  def W15 : Rd<30, "v31:30", [V30, V31, VF15]>, DwarfRegNum<[129]>;
  }

  // Reverse Aliases of the V* registers used to hold double vec values.
  let SubRegIndices = [vsub_lo, vsub_hi, vsub_fake], CoveredBySubRegs = 1 in {
  def WR0 : Rd< 1,  "v0:1",  [V0, V1, VFR0]>,  DwarfRegNum<[161]>;
  def WR1 : Rd< 3,  "v2:3",  [V2, V3, VFR1]>,  DwarfRegNum<[162]>;
  def WR2 : Rd< 5,  "v4:5",  [V4, V5, VFR2]>,  DwarfRegNum<[163]>;
  def WR3 : Rd< 7,  "v6:7",  [V6, V7, VFR3]>,  DwarfRegNum<[164]>;
  def WR4 : Rd< 9,  "v8:9",  [V8, V9, VFR4]>,  DwarfRegNum<[165]>;
  def WR5 : Rd<11, "v10:11", [V10, V11, VFR5]>,  DwarfRegNum<[166]>;
  def WR6 : Rd<13, "v12:13", [V12, V13, VFR6]>,  DwarfRegNum<[167]>;
  def WR7 : Rd<15, "v14:15", [V14, V15, VFR7]>,  DwarfRegNum<[168]>;
  def WR8 : Rd<17, "v16:17", [V16, V17, VFR8]>,  DwarfRegNum<[169]>;
  def WR9 : Rd<19, "v18:19", [V18, V19, VFR9]>,  DwarfRegNum<[170]>;
  def WR10: Rd<21, "v20:21", [V20, V21, VFR10]>,  DwarfRegNum<[171]>;
  def WR11: Rd<23, "v22:23", [V22, V23, VFR11]>,  DwarfRegNum<[172]>;
  def WR12: Rd<25, "v24:25", [V24, V25, VFR12]>,  DwarfRegNum<[173]>;
  def WR13: Rd<27, "v26:27", [V26, V27, VFR13]>,  DwarfRegNum<[174]>;
  def WR14: Rd<29, "v28:29", [V28, V29, VFR14]>,  DwarfRegNum<[175]>;
  def WR15: Rd<31, "v30:31", [V30, V31, VFR15]>,  DwarfRegNum<[176]>;
  }

  // Aliases of the V* registers used to hold quad vec values.
  let SubRegIndices = [wsub_lo, wsub_hi], CoveredBySubRegs = 1 in {
  def VQ0  : Rd< 0, "v3:0",   [W0,  W1]>,  DwarfRegNum<[252]>;
  def VQ1  : Rd< 4, "v7:4",   [W2,  W3]>,  DwarfRegNum<[253]>;
  def VQ2  : Rd< 8, "v11:8",  [W4,  W5]>,  DwarfRegNum<[254]>;
  def VQ3  : Rd<12, "v15:12", [W6,  W7]>,  DwarfRegNum<[255]>;
  def VQ4  : Rd<16, "v19:16", [W8,  W9]>,  DwarfRegNum<[256]>;
  def VQ5  : Rd<20, "v23:20", [W10, W11]>, DwarfRegNum<[257]>;
  def VQ6  : Rd<24, "v27:24", [W12, W13]>, DwarfRegNum<[258]>;
  def VQ7  : Rd<28, "v31:28", [W14, W15]>, DwarfRegNum<[259]>;
  }

  // Vector Predicate registers.
  def Q0 : Rq<0, "q0">, DwarfRegNum<[131]>;
  def Q1 : Rq<1, "q1">, DwarfRegNum<[132]>;
  def Q2 : Rq<2, "q2">, DwarfRegNum<[133]>;
  def Q3 : Rq<3, "q3">, DwarfRegNum<[134]>;

  // System registers.
  def SGP0     :  Rs<0,  "sgp0",       ["s0"]>,  DwarfRegNum<[144]>;
  def SGP1     :  Rs<1,  "sgp1",       ["s1"]>,  DwarfRegNum<[145]>;
  def STID     :  Rs<2,  "stid",       ["s2"]>,  DwarfRegNum<[146]>;
  def ELR      :  Rs<3,  "elr",        ["s3"]>,  DwarfRegNum<[147]>;
  def BADVA0   :  Rs<4,  "badva0",     ["s4"]>,  DwarfRegNum<[148]>;
  def BADVA1   :  Rs<5,  "badva1",     ["s5"]>,  DwarfRegNum<[149]>;
  def SSR      :  Rs<6,  "ssr",        ["s6"]>,  DwarfRegNum<[150]>;
  def CCR      :  Rs<7,  "ccr",        ["s7"]>,  DwarfRegNum<[151]>;
  def HTID     :  Rs<8,  "htid",       ["s8"]>,  DwarfRegNum<[152]>;
  def BADVA    :  Rs<9,  "badva",      ["s9"]>,  DwarfRegNum<[153]>;
  def IMASK    :  Rs<10, "imask",      ["s10"]>, DwarfRegNum<[154]>;
  def S11      :  Rs<11, "s11">,                 DwarfRegNum<[155]>;
  def S12      :  Rs<12, "s12">,                 DwarfRegNum<[156]>;
  def S13      :  Rs<13, "s13">,                 DwarfRegNum<[157]>;
  def S14      :  Rs<14, "s14">,                 DwarfRegNum<[158]>;
  def S15      :  Rs<15, "s15">,                 DwarfRegNum<[159]>;
  def EVB      :  Rs<16, "evb",        ["s16"]>, DwarfRegNum<[160]>;
  def MODECTL  :  Rs<17, "modectl",    ["s17"]>, DwarfRegNum<[161]>;
  def SYSCFG   :  Rs<18, "syscfg",     ["s18"]>, DwarfRegNum<[162]>;
  def S19      :  Rs<19, "s19",        ["s19"]>, DwarfRegNum<[163]>;
  def S20      :  Rs<20, "s20",        ["s20"]>, DwarfRegNum<[164]>;
  def VID      :  Rs<21, "vid",        ["s21"]>, DwarfRegNum<[165]>;
  def S22      :  Rs<22, "s22",        ["s22"]>, DwarfRegNum<[166]>;
  def S23      :  Rs<23, "s23">,                 DwarfRegNum<[167]>;
  def S24      :  Rs<24, "s24">,                 DwarfRegNum<[168]>;
  def S25      :  Rs<25, "s25">,                 DwarfRegNum<[169]>;
  def S26      :  Rs<26, "s26">,                 DwarfRegNum<[170]>;
  def CFGBASE  :  Rs<27, "cfgbase",    ["s27"]>, DwarfRegNum<[171]>;
  def DIAG     :  Rs<28, "diag",       ["s28"]>, DwarfRegNum<[172]>;
  def REV      :  Rs<29, "rev",        ["s29"]>, DwarfRegNum<[173]>;
  def PCYCLELO :  Rs<30, "pcyclelo",   ["s30"]>, DwarfRegNum<[174]>;
  def PCYCLEHI :  Rs<31, "pcyclehi",   ["s31"]>, DwarfRegNum<[175]>;
  def ISDBST   :  Rs<32, "isdbst",     ["s32"]>, DwarfRegNum<[176]>;
  def ISDBCFG0 :  Rs<33, "isdbcfg0",   ["s33"]>, DwarfRegNum<[177]>;
  def ISDBCFG1 :  Rs<34, "isdbcfg1",   ["s34"]>, DwarfRegNum<[178]>;
  def S35      :  Rs<35, "s35">,                 DwarfRegNum<[179]>;
  def BRKPTPC0 :  Rs<36, "brkptpc0",   ["s36"]>, DwarfRegNum<[180]>;
  def BRKPTCFG0:  Rs<37, "brkptcfg0",  ["s37"]>, DwarfRegNum<[181]>;
  def BRKPTPC1 :  Rs<38, "brkptpc1",   ["s38"]>, DwarfRegNum<[182]>;
  def BRKPTCFG1:  Rs<39, "brkptcfg1",  ["s39"]>, DwarfRegNum<[183]>;
  def ISDBMBXIN:  Rs<40, "isdbmbxin",  ["s40"]>, DwarfRegNum<[184]>;
  def ISDBMBXOUT: Rs<41, "isdbmbxout", ["s41"]>, DwarfRegNum<[185]>;
  def ISDBEN:     Rs<42, "isdben",     ["s42"]>, DwarfRegNum<[186]>;
  def ISDBGPR:    Rs<43, "isdbgpr",    ["s43"]>, DwarfRegNum<[187]>;
  def S44:        Rs<44, "s44">,                 DwarfRegNum<[188]>;
  def S45:        Rs<45, "s45">,                 DwarfRegNum<[189]>;
  def S46:        Rs<46, "s46">,                 DwarfRegNum<[190]>;
  def S47:        Rs<47, "s47">,                 DwarfRegNum<[191]>;
  def PMUCNT0:    Rs<48, "pmucnt0",    ["s48"]>, DwarfRegNum<[192]>;
  def PMUCNT1:    Rs<49, "pmucnt1",    ["s49"]>, DwarfRegNum<[193]>;
  def PMUCNT2:    Rs<50, "pmucnt2",    ["s50"]>, DwarfRegNum<[194]>;
  def PMUCNT3:    Rs<51, "pmucnt3",    ["s51"]>, DwarfRegNum<[195]>;
  def PMUEVTCFG:  Rs<52, "pmuevtcfg",  ["s52"]>, DwarfRegNum<[196]>;
  def PMUCFG:     Rs<53, "pmucfg",     ["s53"]>, DwarfRegNum<[197]>;
  def S54:        Rs<54, "s54">,                 DwarfRegNum<[198]>;
  def S55:        Rs<55, "s55">,                 DwarfRegNum<[199]>;
  def S56:        Rs<56, "s56">,                 DwarfRegNum<[200]>;
  def S57:        Rs<57, "s57">,                 DwarfRegNum<[201]>;
  def S58:        Rs<58, "s58">,                 DwarfRegNum<[202]>;
  def S59:        Rs<59, "s59">,                 DwarfRegNum<[203]>;
  def S60:        Rs<60, "s60">,                 DwarfRegNum<[204]>;
  def S61:        Rs<61, "s61">,                 DwarfRegNum<[205]>;
  def S62:        Rs<62, "s62">,                 DwarfRegNum<[206]>;
  def S63:        Rs<63, "s63">,                 DwarfRegNum<[207]>;
  def S64:        Rs<64, "s64">,                 DwarfRegNum<[208]>;
  def S65:        Rs<65, "s65">,                 DwarfRegNum<[209]>;
  def S66:        Rs<66, "s66">,                 DwarfRegNum<[210]>;
  def S67:        Rs<67, "s67">,                 DwarfRegNum<[211]>;
  def S68:        Rs<68, "s68">,                 DwarfRegNum<[212]>;
  def S69:        Rs<69, "s69">,                 DwarfRegNum<[213]>;
  def S70:        Rs<70, "s70">,                 DwarfRegNum<[214]>;
  def S71:        Rs<71, "s71">,                 DwarfRegNum<[215]>;
  def S72:        Rs<72, "s72">,                 DwarfRegNum<[216]>;
  def S73:        Rs<73, "s73">,                 DwarfRegNum<[217]>;
  def S74:        Rs<74, "s74">,                 DwarfRegNum<[218]>;
  def S75:        Rs<75, "s75">,                 DwarfRegNum<[219]>;
  def S76:        Rs<76, "s76">,                 DwarfRegNum<[220]>;
  def S77:        Rs<77, "s77">,                 DwarfRegNum<[221]>;
  def S78:        Rs<78, "s78">,                 DwarfRegNum<[222]>;
  def S79:        Rs<79, "s79">,                 DwarfRegNum<[223]>;
  def S80:        Rs<80, "s80">,                 DwarfRegNum<[224]>;

  // System Register Pair
  let SubRegIndices = [isub_lo, isub_hi], CoveredBySubRegs = 1 in {
    def SGP1_0 : Rss<0,  "s1:0",  [SGP0, SGP1], ["sgp1:0"]>, DwarfRegNum<[144]>;
    def S3_2   : Rss<2,  "s3:2",  [STID, ELR]>,              DwarfRegNum<[146]>;
    def S5_4   : Rss<4,  "s5:4",  [BADVA0, BADVA1], ["badva1:0"]>,
                                                             DwarfRegNum<[148]>;
    def S7_6   : Rss<6,  "s7:6",  [SSR, CCR], ["ccr:ssr"]>,  DwarfRegNum<[150]>;
    def S9_8   : Rss<8,  "s9:8",  [HTID, BADVA]>,            DwarfRegNum<[152]>;
    def S11_10 : Rss<10, "s11:10", [IMASK, S11]>,            DwarfRegNum<[154]>;
    def S13_12 : Rss<12, "s13:12", [S12, S13]>,              DwarfRegNum<[156]>;
    def S15_14 : Rss<14, "s15:14", [S14, S15]>,              DwarfRegNum<[158]>;
    def S17_16 : Rss<16, "s17:16", [EVB, MODECTL]>,          DwarfRegNum<[160]>;
    def S19_18 : Rss<18, "s19:18", [SYSCFG, S19]>,           DwarfRegNum<[162]>;
    def S21_20 : Rss<20, "s21:20", [S20, VID]>,              DwarfRegNum<[164]>;
    def S23_22 : Rss<22, "s23:22", [S22, S23]>,              DwarfRegNum<[166]>;
    def S25_24 : Rss<24, "s25:24", [S24, S25]>,              DwarfRegNum<[168]>;
    def S27_26 : Rss<26, "s27:26", [S26, CFGBASE]>,          DwarfRegNum<[170]>;
    def S29_28 : Rss<28, "s29:28", [DIAG, REV]>,             DwarfRegNum<[172]>;
    def S31_30 : Rss<30, "s31:30", [PCYCLELO, PCYCLEHI], ["pcycle"]>,    DwarfRegNum<[174]>;
    def S33_32 : Rss<32, "s33:32", [ISDBST, ISDBCFG0]>,      DwarfRegNum<[176]>;
    def S35_34 : Rss<34, "s35:34", [ISDBCFG1, S35]>,    DwarfRegNum<[178]>;
    def S37_36 : Rss<36, "s37:36", [BRKPTPC0, BRKPTCFG0]>,   DwarfRegNum<[180]>;
    def S39_38 : Rss<38, "s39:38", [BRKPTPC1, BRKPTCFG1]>,   DwarfRegNum<[182]>;
    def S41_40 : Rss<40, "s41:40", [ISDBMBXIN, ISDBMBXOUT]>, DwarfRegNum<[184]>;
    def S43_42 : Rss<42, "s43:42", [ISDBEN, ISDBGPR]>,       DwarfRegNum<[186]>;
    def S45_44 : Rss<44, "s45:44", [S44, S45]>,              DwarfRegNum<[188]>;
    def S47_46 : Rss<46, "s47:46", [S46, S47]>,              DwarfRegNum<[190]>;
    def S49_48 : Rss<48, "s49:48", [PMUCNT0, PMUCNT1]>,      DwarfRegNum<[192]>;
    def S51_50 : Rss<50, "s51:50", [PMUCNT2, PMUCNT3]>,      DwarfRegNum<[194]>;
    def S53_52 : Rss<52, "s53:52", [PMUEVTCFG, PMUCFG]>,     DwarfRegNum<[196]>;
    def S55_54 : Rss<54, "s55:54", [S54, S55]>,              DwarfRegNum<[198]>;
    def S57_56 : Rss<56, "s57:56", [S56, S57]>,              DwarfRegNum<[200]>;
    def S59_58 : Rss<58, "s59:58", [S58, S59]>,              DwarfRegNum<[202]>;
    def S61_60 : Rss<60, "s61:60", [S60, S61]>,              DwarfRegNum<[204]>;
    def S63_62 : Rss<62, "s63:62", [S62, S63]>,              DwarfRegNum<[206]>;
    def S65_64 : Rss<64, "s65:64", [S64, S65]>,              DwarfRegNum<[208]>;
    def S67_66 : Rss<66, "s67:66", [S66, S67]>,              DwarfRegNum<[210]>;
    def S69_68 : Rss<68, "s69:68", [S68, S69]>,              DwarfRegNum<[212]>;
    def S71_70 : Rss<70, "s71:70", [S70, S71]>,              DwarfRegNum<[214]>;
    def S73_72 : Rss<72, "s73:72", [S72, S73]>,              DwarfRegNum<[216]>;
    def S75_74 : Rss<74, "s75:74", [S74, S75]>,              DwarfRegNum<[218]>;
    def S77_76 : Rss<76, "s77:76", [S77, S76]>,              DwarfRegNum<[219]>;
    def S79_78 : Rss<78, "s79:78", [S79, S78]>,              DwarfRegNum<[220]>;
  }

  // Guest Registers
  def GELR:      Rg<0,  "gelr", ["g0"]>,       DwarfRegNum<[220]>;
  def GSR:       Rg<1,  "gsr", ["g1"]>,        DwarfRegNum<[221]>;
  def GOSP:      Rg<2,  "gosp", ["g2"]>,       DwarfRegNum<[222]>;
  def G3:        Rg<3,  "gbadva", ["g3"]>,     DwarfRegNum<[223]>;
  def G4:        Rg<4,  "g4">,                 DwarfRegNum<[224]>;
  def G5:        Rg<5,  "g5">,                 DwarfRegNum<[225]>;
  def G6:        Rg<6,  "g6">,                 DwarfRegNum<[226]>;
  def G7:        Rg<7,  "g7">,                 DwarfRegNum<[227]>;
  def G8:        Rg<8,  "g8">,                 DwarfRegNum<[228]>;
  def G9:        Rg<9,  "g9">,                 DwarfRegNum<[229]>;
  def G10:       Rg<10, "g10">,                DwarfRegNum<[230]>;
  def G11:       Rg<11, "g11">,                DwarfRegNum<[231]>;
  def G12:       Rg<12, "g12">,                DwarfRegNum<[232]>;
  def G13:       Rg<13, "g13">,                DwarfRegNum<[233]>;
  def G14:       Rg<14, "g14">,                DwarfRegNum<[234]>;
  def G15:       Rg<15, "g15">,                DwarfRegNum<[235]>;
  def GPMUCNT4:  Rg<16, "gpmucnt4", ["g16"]>,  DwarfRegNum<[236]>;
  def GPMUCNT5:  Rg<17, "gpmucnt5", ["g17"]>,  DwarfRegNum<[237]>;
  def GPMUCNT6:  Rg<18, "gpmucnt6", ["g18"]>,  DwarfRegNum<[238]>;
  def GPMUCNT7:  Rg<19, "gpmucnt7", ["g19"]>,  DwarfRegNum<[239]>;
  def G20:       Rg<20, "g20">,                DwarfRegNum<[240]>;
  def G21:       Rg<21, "g21">,                DwarfRegNum<[241]>;
  def G22:       Rg<22, "g22">,                DwarfRegNum<[242]>;
  def G23:       Rg<23, "g23">,                DwarfRegNum<[243]>;
  def GPCYCLELO: Rg<24, "gpcyclelo", ["g24"]>, DwarfRegNum<[244]>;
  def GPCYCLEHI: Rg<25, "gpcyclehi", ["g25"]>, DwarfRegNum<[245]>;
  def GPMUCNT0:  Rg<26, "gpmucnt0",  ["g26"]>, DwarfRegNum<[246]>;
  def GPMUCNT1:  Rg<27, "gpmucnt1",  ["g27"]>, DwarfRegNum<[247]>;
  def GPMUCNT2:  Rg<28, "gpmucnt2",  ["g28"]>, DwarfRegNum<[248]>;
  def GPMUCNT3:  Rg<29, "gpmucnt3",  ["g29"]>, DwarfRegNum<[249]>;
  def G30:       Rg<30, "g30">,                DwarfRegNum<[250]>;
  def G31:       Rg<31, "g31">,                DwarfRegNum<[251]>;

  // Guest Register Pairs
  let SubRegIndices = [isub_lo, isub_hi], CoveredBySubRegs = 1 in {
    def G1_0   : Rgg<0,  "g1:0",   [GELR, GSR]>,            DwarfRegNum<[220]>;
    def G3_2   : Rgg<2,  "g3:2",   [GOSP, G3]>,             DwarfRegNum<[222]>;
    def G5_4   : Rgg<4,  "g5:4",   [G4, G5]>,               DwarfRegNum<[224]>;
    def G7_6   : Rgg<6,  "g7:6",   [G6, G7]>,               DwarfRegNum<[226]>;
    def G9_8   : Rgg<8,  "g9:8",   [G8, G9]>,               DwarfRegNum<[228]>;
    def G11_10 : Rgg<10, "g11:10", [G10, G11]>,             DwarfRegNum<[230]>;
    def G13_12 : Rgg<12, "g13:12", [G12, G13]>,             DwarfRegNum<[232]>;
    def G15_14 : Rgg<14, "g15:14", [G14, G15]>,             DwarfRegNum<[234]>;
    def G17_16 : Rgg<16, "g17:16", [GPMUCNT4, GPMUCNT5]>,   DwarfRegNum<[236]>;
    def G19_18 : Rgg<18, "g19:18", [GPMUCNT6, GPMUCNT7]>,   DwarfRegNum<[238]>;
    def G21_20 : Rgg<20, "g21:20", [G20, G21]>,             DwarfRegNum<[240]>;
    def G23_22 : Rgg<22, "g23:22", [G22, G23]>,             DwarfRegNum<[242]>;
    def G25_24 : Rgg<24, "g25:24", [GPCYCLELO, GPCYCLEHI]>, DwarfRegNum<[244]>;
    def G27_26 : Rgg<26, "g27:26", [GPMUCNT0, GPMUCNT1]>,   DwarfRegNum<[246]>;
    def G29_28 : Rgg<28, "g29:28", [GPMUCNT2, GPMUCNT3]>,   DwarfRegNum<[248]>;
    def G31_30 : Rgg<30, "g31:30", [G30, G31]>,             DwarfRegNum<[250]>;
  }

}

// HVX types

def VecI1:   ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v64i1,  v128i1,  v64i1]>;
def VecI8:   ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v64i8,  v128i8,  v64i8]>;
def VecI16:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v32i16, v64i16,  v32i16]>;
def VecI32:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v16i32, v32i32,  v16i32]>;
def VecF16:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v32f16, v64f16,  v32f16]>;
def VecF32:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v16f32, v32f32,  v16f32]>;

def VecPI8:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v128i8, v256i8,  v128i8]>;
def VecPI16: ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v64i16, v128i16, v64i16]>;
def VecPI32: ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v32i32, v64i32,  v32i32]>;
def VecPF16: ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v64f16, v128f16, v64f16]>;
def VecPF32: ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v32f32, v64f32,  v32f32]>;

def VecQ8:   ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v64i1,  v128i1,  v64i1]>;
def VecQ16:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v32i1,  v64i1,   v32i1]>;
def VecQ32:  ValueTypeByHwMode<[Hvx64,  Hvx128,  DefaultMode],
                               [v16i1,  v32i1,   v16i1]>;

// HVX register classes

def HvxVR : RegisterClass<"Hexagon", [VecI8, VecI16, VecI32, VecF16, VecF32], 512,
  (add (sequence "V%u", 0, 31), VTMP)> {
  let RegInfos = RegInfoByHwMode<[Hvx64, Hvx128, DefaultMode],
    [RegInfo<512,512,512>, RegInfo<1024,1024,1024>, RegInfo<512,512,512>]>;
}

def HvxWR : RegisterClass<"Hexagon", [VecPI8, VecPI16, VecPI32, VecPF16, VecPF32], 1024,
  (add (sequence "W%u", 0, 15), (sequence "WR%u", 0, 15))> {
  let RegInfos = RegInfoByHwMode<[Hvx64, Hvx128, DefaultMode],
    [RegInfo<1024,1024,512>, RegInfo<2048,2048,1024>, RegInfo<1024,1024,512>]>;
}

def HvxQR : RegisterClass<"Hexagon", [VecI1, VecQ8, VecQ16, VecQ32], 128,
  (add Q0, Q1, Q2, Q3)> {
  let RegInfos = RegInfoByHwMode<[Hvx64, Hvx128, DefaultMode],
    [RegInfo<64,512,512>, RegInfo<128,1024,1024>, RegInfo<64,512,512>]>;
}

def HvxVQR : RegisterClass<"Hexagon", [untyped], 2048,
  (add (sequence "VQ%u", 0, 7))> {
  let RegInfos = RegInfoByHwMode<[Hvx64, Hvx128, DefaultMode],
    [RegInfo<2048,2048,512>, RegInfo<4096,4096,1024>, RegInfo<2048,2048,512>]>;
}

// Core register classes

def IntRegs : RegisterClass<"Hexagon", [i32, f32, v4i8, v2i16], 32,
  (add (sequence "R%u", 0, 9), (sequence "R%u", 12, 28),
       R10, R11, R29, R30, R31)>;

// Registers are listed in reverse order for allocation preference reasons.
def GeneralSubRegs : RegisterClass<"Hexagon", [i32], 32,
  (add R23, R22, R21, R20, R19, R18, R17, R16,
       R7, R6, R5, R4, R3, R2, R1, R0)>;

def IntRegsLow8 : RegisterClass<"Hexagon", [i32], 32,
  (add R7, R6, R5, R4, R3, R2, R1, R0)> ;

def DoubleRegs : RegisterClass<"Hexagon", [i64, f64, v8i8, v4i16, v2i32], 64,
  (add (sequence "D%u", 0, 4), (sequence "D%u", 6, 13), D5, D14, D15)>;

def GeneralDoubleLow8Regs : RegisterClass<"Hexagon", [i64], 64,
  (add D11, D10, D9, D8, D3, D2, D1, D0)>;

let Size = 32 in
def PredRegs : RegisterClass<"Hexagon",
  [i1, v2i1, v4i1, v8i1, v4i8, v2i16, i32], 32, (add P0, P1, P2, P3)>;

let Size = 32 in
def ModRegs : RegisterClass<"Hexagon", [i32], 32, (add M0, M1)>;

let Size = 32, isAllocatable = 0 in
def CtrRegs : RegisterClass<"Hexagon", [i32], 32,
  (add LC0, SA0, LC1, SA1, P3_0, C5, C8, PC, UGP, GP, CS0, CS1,
       UPCYCLELO, UPCYCLEHI,
       FRAMELIMIT, FRAMEKEY, PKTCOUNTLO, PKTCOUNTHI, UTIMERLO, UTIMERHI,
       M0, M1, USR)>;

let Size = 64 in
def VectRegRev : RegisterClass<"Hexagon", [i64], 64,
  (add (sequence "WR%u", 0, 15))>;

let isAllocatable = 0 in
def UsrBits : RegisterClass<"Hexagon", [i1], 0, (add USR_OVF)>;

let Size = 64, isAllocatable = 0 in
def CtrRegs64 : RegisterClass<"Hexagon", [i64], 64,
  (add C1_0, C3_2, C5_4, C7_6, C9_8, C11_10, CS, UPCYCLE, C17_16,
       PKTCOUNT, UTIMER)>;

let Size = 32, isAllocatable = 0 in
def GuestRegs : RegisterClass<"Hexagon", [i32], 32,
  (add GELR, GSR, GOSP,
       (sequence "G%u", 3, 15),
       GPMUCNT4, GPMUCNT5, GPMUCNT6, GPMUCNT7,
       G20, G21, G22, G23,
       GPCYCLELO, GPCYCLEHI, GPMUCNT0, GPMUCNT1,
       GPMUCNT2,  GPMUCNT3,
       G30, G31)>;

let Size = 64, isAllocatable = 0 in
def GuestRegs64 : RegisterClass<"Hexagon", [i64], 64,
  (add G1_0, G3_2,
       G5_4, G7_6, G9_8, G11_10, G13_12, G15_14,
       G17_16, G19_18,
       G21_20, G23_22,
       G25_24, G27_26, G29_28,
       G31_30)>;

let Size = 32, isAllocatable = 0 in
def SysRegs : RegisterClass<"Hexagon", [i32], 32,
  (add SGP0, SGP1, STID, ELR, BADVA0, BADVA1,
       SSR, CCR, HTID, BADVA, IMASK,
       S11, S12, S13, S14, S15,
       S19, S23, S25,
       EVB, MODECTL, SYSCFG, S20, VID, S22, S24,
       S26, CFGBASE, DIAG, REV, PCYCLEHI,
       PCYCLELO, ISDBST, ISDBCFG0, ISDBCFG1, S35,
       BRKPTPC0, BRKPTCFG0, BRKPTPC1, BRKPTCFG1,
       ISDBMBXIN, ISDBMBXOUT, ISDBEN, ISDBGPR,
       S44, S45, S46, S47,
       PMUCNT0, PMUCNT1, PMUCNT2, PMUCNT3,
       PMUEVTCFG, PMUCFG, S54, S55, S56, S57,
       S58, S59, S60, S61, S62, S63, S64, S65, S66, S67,
       S68, S69, S70, S71, S72, S73, S74, S75, S76, S77,
       S78, S79, S80
       )>;

let Size = 64, isAllocatable = 0 in
def SysRegs64 : RegisterClass<"Hexagon", [i64], 64,
  (add SGP1_0,
       S3_2, S5_4, S7_6, S9_8,
       S11_10, S13_12, S15_14,
       S17_16, S19_18, S21_20,
       S23_22, S25_24,
       S27_26, S29_28, S31_30, S33_32, S35_34,
       S37_36, S39_38, S41_40, S43_42, S45_44,
       S47_46, S49_48, S51_50, S53_52,
       S55_54, S57_56, S59_58,
       S61_60, S63_62, S65_64, S67_66, S69_68,
       S71_70, S73_72, S75_74, S77_76, S79_78
       )>;

// These registers are new for v62 and onward.
// The function RegisterMatchesArch() uses this list for validation.
let isAllocatable = 0 in
def V62Regs : RegisterClass<"Hexagon", [i32], 32,
  (add FRAMELIMIT, FRAMEKEY,   C17_16, PKTCOUNTLO, PKTCOUNTHI, PKTCOUNT,
       UTIMERLO,   UTIMERHI,   UTIMER)>;

// These registers are new for v65 and onward.
let Size = 32, isAllocatable = 0 in
def V65Regs : RegisterClass<"Hexagon", [i32], 32, (add VTMP)>;


def HexagonCSR
  : CalleeSavedRegs<(add R16, R17, R18, R19, R20, R21, R22, R23,
                         R24, R25, R26, R27)>;
