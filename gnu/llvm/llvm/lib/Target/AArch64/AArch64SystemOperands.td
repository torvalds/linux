//===- AArch64SystemOperands.td ----------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the symbolic operands permitted for various kinds of
// AArch64 system instruction.
//
//===----------------------------------------------------------------------===//

include "llvm/TableGen/SearchableTable.td"

//===----------------------------------------------------------------------===//
// Features that, for the compiler, only enable system operands and PStates
//===----------------------------------------------------------------------===//

def HasCCPP    : Predicate<"Subtarget->hasCCPP()">,
                 AssemblerPredicateWithAll<(all_of FeatureCCPP), "ccpp">;

def HasPAN     : Predicate<"Subtarget->hasPAN()">,
                 AssemblerPredicateWithAll<(all_of FeaturePAN),
                 "ARM v8.1  Privileged Access-Never extension">;

def HasPsUAO   : Predicate<"Subtarget->hasPsUAO()">,
                 AssemblerPredicateWithAll<(all_of FeaturePsUAO),
                 "ARM v8.2 UAO PState extension (psuao)">;

def HasPAN_RWV : Predicate<"Subtarget->hasPAN_RWV()">,
                 AssemblerPredicateWithAll<(all_of FeaturePAN_RWV),
                 "ARM v8.2 PAN AT S1E1R and AT S1E1W Variation">;

def HasCONTEXTIDREL2
               : Predicate<"Subtarget->hasCONTEXTIDREL2()">,
                 AssemblerPredicateWithAll<(all_of FeatureCONTEXTIDREL2),
                 "Target contains CONTEXTIDR_EL2 RW operand">;

//===----------------------------------------------------------------------===//
// AT (address translate) instruction options.
//===----------------------------------------------------------------------===//

class AT<string name, bits<3> op1, bits<4> crn, bits<4> crm,
         bits<3> op2> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<14> Encoding;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  code Requires = [{ {} }];
}

def : AT<"S1E1R",  0b000, 0b0111, 0b1000, 0b000>;
def : AT<"S1E2R",  0b100, 0b0111, 0b1000, 0b000>;
def : AT<"S1E3R",  0b110, 0b0111, 0b1000, 0b000>;
def : AT<"S1E1W",  0b000, 0b0111, 0b1000, 0b001>;
def : AT<"S1E2W",  0b100, 0b0111, 0b1000, 0b001>;
def : AT<"S1E3W",  0b110, 0b0111, 0b1000, 0b001>;
def : AT<"S1E0R",  0b000, 0b0111, 0b1000, 0b010>;
def : AT<"S1E0W",  0b000, 0b0111, 0b1000, 0b011>;
def : AT<"S12E1R", 0b100, 0b0111, 0b1000, 0b100>;
def : AT<"S12E1W", 0b100, 0b0111, 0b1000, 0b101>;
def : AT<"S12E0R", 0b100, 0b0111, 0b1000, 0b110>;
def : AT<"S12E0W", 0b100, 0b0111, 0b1000, 0b111>;

let Requires = [{ {AArch64::FeaturePAN_RWV} }] in {
def : AT<"S1E1RP", 0b000, 0b0111, 0b1001, 0b000>;
def : AT<"S1E1WP", 0b000, 0b0111, 0b1001, 0b001>;
}

// v8.9a/v9.4a FEAT_ATS1A
def : AT<"S1E1A", 0b000, 0b0111, 0b1001, 0b010>;
def : AT<"S1E2A", 0b100, 0b0111, 0b1001, 0b010>;
def : AT<"S1E3A", 0b110, 0b0111, 0b1001, 0b010>;

//===----------------------------------------------------------------------===//
// DMB/DSB (data barrier) instruction options.
//===----------------------------------------------------------------------===//

class DB<string name, bits<4> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding = encoding;
}

def : DB<"oshld", 0x1>;
def : DB<"oshst", 0x2>;
def : DB<"osh",   0x3>;
def : DB<"nshld", 0x5>;
def : DB<"nshst", 0x6>;
def : DB<"nsh",   0x7>;
def : DB<"ishld", 0x9>;
def : DB<"ishst", 0xa>;
def : DB<"ish",   0xb>;
def : DB<"ld",    0xd>;
def : DB<"st",    0xe>;
def : DB<"sy",    0xf>;

class DBnXS<string name, bits<4> encoding, bits<5> immValue> : SearchableTable {
  let SearchableFields = ["Name", "Encoding", "ImmValue"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding = encoding;
  bits<5> ImmValue = immValue;
  code Requires = [{ {AArch64::FeatureXS} }];
}

def : DBnXS<"oshnxs", 0x3, 0x10>;
def : DBnXS<"nshnxs", 0x7, 0x14>;
def : DBnXS<"ishnxs", 0xb, 0x18>;
def : DBnXS<"synxs",  0xf, 0x1c>;

//===----------------------------------------------------------------------===//
// DC (data cache maintenance) instruction options.
//===----------------------------------------------------------------------===//

class DC<string name, bits<3> op1, bits<4> crn, bits<4> crm,
         bits<3> op2> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<14> Encoding;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  code Requires = [{ {} }];
}

def : DC<"ZVA",   0b011, 0b0111, 0b0100, 0b001>;
def : DC<"IVAC",  0b000, 0b0111, 0b0110, 0b001>;
def : DC<"ISW",   0b000, 0b0111, 0b0110, 0b010>;
def : DC<"CVAC",  0b011, 0b0111, 0b1010, 0b001>;
def : DC<"CSW",   0b000, 0b0111, 0b1010, 0b010>;
def : DC<"CVAU",  0b011, 0b0111, 0b1011, 0b001>;
def : DC<"CIVAC", 0b011, 0b0111, 0b1110, 0b001>;
def : DC<"CISW",  0b000, 0b0111, 0b1110, 0b010>;

let Requires = [{ {AArch64::FeatureCCPP} }] in
def : DC<"CVAP",  0b011, 0b0111, 0b1100, 0b001>;

let Requires = [{ {AArch64::FeatureCacheDeepPersist} }] in
def : DC<"CVADP",  0b011, 0b0111, 0b1101, 0b001>;

let Requires = [{ {AArch64::FeatureMTE} }] in {
def : DC<"IGVAC",   0b000, 0b0111, 0b0110, 0b011>;
def : DC<"IGSW",    0b000, 0b0111, 0b0110, 0b100>;
def : DC<"CGSW",    0b000, 0b0111, 0b1010, 0b100>;
def : DC<"CIGSW",   0b000, 0b0111, 0b1110, 0b100>;
def : DC<"CGVAC",   0b011, 0b0111, 0b1010, 0b011>;
def : DC<"CGVAP",   0b011, 0b0111, 0b1100, 0b011>;
def : DC<"CGVADP",  0b011, 0b0111, 0b1101, 0b011>;
def : DC<"CIGVAC",  0b011, 0b0111, 0b1110, 0b011>;
def : DC<"GVA",     0b011, 0b0111, 0b0100, 0b011>;
def : DC<"IGDVAC",  0b000, 0b0111, 0b0110, 0b101>;
def : DC<"IGDSW",   0b000, 0b0111, 0b0110, 0b110>;
def : DC<"CGDSW",   0b000, 0b0111, 0b1010, 0b110>;
def : DC<"CIGDSW",  0b000, 0b0111, 0b1110, 0b110>;
def : DC<"CGDVAC",  0b011, 0b0111, 0b1010, 0b101>;
def : DC<"CGDVAP",  0b011, 0b0111, 0b1100, 0b101>;
def : DC<"CGDVADP", 0b011, 0b0111, 0b1101, 0b101>;
def : DC<"CIGDVAC", 0b011, 0b0111, 0b1110, 0b101>;
def : DC<"GZVA",    0b011, 0b0111, 0b0100, 0b100>;
}

let Requires = [{ {AArch64::FeatureMEC} }] in {
def : DC<"CIPAE",   0b100, 0b0111, 0b1110, 0b000>;
def : DC<"CIGDPAE", 0b100, 0b0111, 0b1110, 0b111>;
}

//===----------------------------------------------------------------------===//
// IC (instruction cache maintenance) instruction options.
//===----------------------------------------------------------------------===//

class IC<string name, bits<3> op1, bits<4> crn, bits<4> crm, bits<3> op2,
         bit needsreg> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<14> Encoding;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  bit NeedsReg = needsreg;
}

def : IC<"IALLUIS", 0b000, 0b0111, 0b0001, 0b000, 0>;
def : IC<"IALLU",   0b000, 0b0111, 0b0101, 0b000, 0>;
def : IC<"IVAU",    0b011, 0b0111, 0b0101, 0b001, 1>;

//===----------------------------------------------------------------------===//
// ISB (instruction-fetch barrier) instruction options.
//===----------------------------------------------------------------------===//

class ISB<string name, bits<4> encoding> : SearchableTable{
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding;
  let Encoding = encoding;
}

def : ISB<"sy", 0xf>;

//===----------------------------------------------------------------------===//
// TSB (Trace synchronization barrier) instruction options.
//===----------------------------------------------------------------------===//

class TSB<string name, bits<4> encoding> : SearchableTable{
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding;
  let Encoding = encoding;

  code Requires = [{ {AArch64::FeatureTRACEV8_4} }];
}

def : TSB<"csync", 0>;

//===----------------------------------------------------------------------===//
// PRFM (prefetch) instruction options.
//===----------------------------------------------------------------------===//

class PRFM<string type,   bits<2> type_encoding,
           string target, bits<2> target_encoding,
           string policy, bits<1> policy_encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = type # target # policy;
  bits<5> Encoding;
  let Encoding{4-3} = type_encoding;
  let Encoding{2-1} = target_encoding;
  let Encoding{0} = policy_encoding;

  code Requires = [{ {} }];
}

def : PRFM<"pld", 0b00, "l1",  0b00, "keep", 0b0>;
def : PRFM<"pld", 0b00, "l1",  0b00, "strm", 0b1>;
def : PRFM<"pld", 0b00, "l2",  0b01, "keep", 0b0>;
def : PRFM<"pld", 0b00, "l2",  0b01, "strm", 0b1>;
def : PRFM<"pld", 0b00, "l3",  0b10, "keep", 0b0>;
def : PRFM<"pld", 0b00, "l3",  0b10, "strm", 0b1>;
let Requires = [{ {AArch64::FeaturePRFM_SLC} }] in {
def : PRFM<"pld", 0b00, "slc", 0b11, "keep", 0b0>;
def : PRFM<"pld", 0b00, "slc", 0b11, "strm", 0b1>;
}
def : PRFM<"pli", 0b01, "l1",  0b00, "keep", 0b0>;
def : PRFM<"pli", 0b01, "l1",  0b00, "strm", 0b1>;
def : PRFM<"pli", 0b01, "l2",  0b01, "keep", 0b0>;
def : PRFM<"pli", 0b01, "l2",  0b01, "strm", 0b1>;
def : PRFM<"pli", 0b01, "l3",  0b10, "keep", 0b0>;
def : PRFM<"pli", 0b01, "l3",  0b10, "strm", 0b1>;
let Requires = [{ {AArch64::FeaturePRFM_SLC} }] in {
def : PRFM<"pli", 0b01, "slc", 0b11, "keep", 0b0>;
def : PRFM<"pli", 0b01, "slc", 0b11, "strm", 0b1>;
}
def : PRFM<"pst", 0b10, "l1",  0b00, "keep", 0b0>;
def : PRFM<"pst", 0b10, "l1",  0b00, "strm", 0b1>;
def : PRFM<"pst", 0b10, "l2",  0b01, "keep", 0b0>;
def : PRFM<"pst", 0b10, "l2",  0b01, "strm", 0b1>;
def : PRFM<"pst", 0b10, "l3",  0b10, "keep", 0b0>;
def : PRFM<"pst", 0b10, "l3",  0b10, "strm", 0b1>;
let Requires = [{ {AArch64::FeaturePRFM_SLC} }] in {
def : PRFM<"pst", 0b10, "slc", 0b11, "keep", 0b0>;
def : PRFM<"pst", 0b10, "slc", 0b11, "strm", 0b1>;
}

//===----------------------------------------------------------------------===//
// SVE Prefetch instruction options.
//===----------------------------------------------------------------------===//

class SVEPRFM<string name, bits<4> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<4> Encoding;
  let Encoding = encoding;
  code Requires = [{ {} }];
}

let Requires = [{ {AArch64::FeatureSVE} }] in {
def : SVEPRFM<"pldl1keep", 0x00>;
def : SVEPRFM<"pldl1strm", 0x01>;
def : SVEPRFM<"pldl2keep", 0x02>;
def : SVEPRFM<"pldl2strm", 0x03>;
def : SVEPRFM<"pldl3keep", 0x04>;
def : SVEPRFM<"pldl3strm", 0x05>;
def : SVEPRFM<"pstl1keep", 0x08>;
def : SVEPRFM<"pstl1strm", 0x09>;
def : SVEPRFM<"pstl2keep", 0x0a>;
def : SVEPRFM<"pstl2strm", 0x0b>;
def : SVEPRFM<"pstl3keep", 0x0c>;
def : SVEPRFM<"pstl3strm", 0x0d>;
}

//===----------------------------------------------------------------------===//
// RPRFM (prefetch) instruction options.
//===----------------------------------------------------------------------===//

class RPRFM<string name, bits<1> type_encoding, bits<5> policy_encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<6> Encoding;
  let Encoding{0} = type_encoding;
  let Encoding{5-1} = policy_encoding;
  code Requires = [{ {} }];
}

def : RPRFM<"pldkeep", 0b0, 0b00000>;
def : RPRFM<"pstkeep", 0b1, 0b00000>;
def : RPRFM<"pldstrm", 0b0, 0b00010>;
def : RPRFM<"pststrm", 0b1, 0b00010>;

//===----------------------------------------------------------------------===//
// SVE Predicate patterns
//===----------------------------------------------------------------------===//

class SVEPREDPAT<string name, bits<5> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<5> Encoding;
  let Encoding = encoding;
}

def : SVEPREDPAT<"pow2",  0x00>;
def : SVEPREDPAT<"vl1",   0x01>;
def : SVEPREDPAT<"vl2",   0x02>;
def : SVEPREDPAT<"vl3",   0x03>;
def : SVEPREDPAT<"vl4",   0x04>;
def : SVEPREDPAT<"vl5",   0x05>;
def : SVEPREDPAT<"vl6",   0x06>;
def : SVEPREDPAT<"vl7",   0x07>;
def : SVEPREDPAT<"vl8",   0x08>;
def : SVEPREDPAT<"vl16",  0x09>;
def : SVEPREDPAT<"vl32",  0x0a>;
def : SVEPREDPAT<"vl64",  0x0b>;
def : SVEPREDPAT<"vl128", 0x0c>;
def : SVEPREDPAT<"vl256", 0x0d>;
def : SVEPREDPAT<"mul4",  0x1d>;
def : SVEPREDPAT<"mul3",  0x1e>;
def : SVEPREDPAT<"all",   0x1f>;

//===----------------------------------------------------------------------===//
// SVE Predicate-as-counter patterns
//===----------------------------------------------------------------------===//

class SVEVECLENSPECIFIER<string name, bits<1> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<1> Encoding;
  let Encoding = encoding;
}

def : SVEVECLENSPECIFIER<"vlx2", 0x0>;
def : SVEVECLENSPECIFIER<"vlx4", 0x1>;

//===----------------------------------------------------------------------===//
// Exact FP Immediates.
//
// These definitions are used to create a lookup table with FP Immediates that
// is used for a few instructions that only accept a limited set of exact FP
// immediates values.
//===----------------------------------------------------------------------===//
class ExactFPImm<string name, string repr, bits<4> enum > : SearchableTable {
  let SearchableFields = ["Enum", "Repr"];
  let EnumValueField = "Enum";

  string Name = name;
  bits<4> Enum = enum;
  string Repr = repr;
}

def : ExactFPImm<"zero", "0.0", 0x0>;
def : ExactFPImm<"half", "0.5", 0x1>;
def : ExactFPImm<"one",  "1.0", 0x2>;
def : ExactFPImm<"two",  "2.0", 0x3>;

//===----------------------------------------------------------------------===//
// PState instruction options.
//===----------------------------------------------------------------------===//

class PStateImm0_15<string name, bits<3> op1, bits<3> op2> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<6> Encoding;
  let Encoding{5-3} = op1;
  let Encoding{2-0} = op2;
  code Requires = [{ {} }];
}

class PStateImm0_1<string name, bits<3> op1, bits<3> op2, bits<3> crm_high> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<9> Encoding;
  let Encoding{8-6} = crm_high;
  let Encoding{5-3} = op1;
  let Encoding{2-0} = op2;
  code Requires = [{ {} }];
}

//                   Name,     Op1,   Op2
def : PStateImm0_15<"SPSel",   0b000, 0b101>;
def : PStateImm0_15<"DAIFSet", 0b011, 0b110>;
def : PStateImm0_15<"DAIFClr", 0b011, 0b111>;
// v8.1a "Privileged Access Never" extension-specific PStates
let Requires = [{ {AArch64::FeaturePAN} }] in
def : PStateImm0_15<"PAN",     0b000, 0b100>;

// v8.2a "User Access Override" extension-specific PStates
let Requires = [{ {AArch64::FeaturePsUAO} }] in
def : PStateImm0_15<"UAO",     0b000, 0b011>;
// v8.4a timing insensitivity of data processing instructions
let Requires = [{ {AArch64::FeatureDIT} }] in
def : PStateImm0_15<"DIT",     0b011, 0b010>;
// v8.5a Spectre Mitigation
let Requires = [{ {AArch64::FeatureSSBS} }] in
def : PStateImm0_15<"SSBS",    0b011, 0b001>;
// v8.5a Memory Tagging Extension
let Requires = [{ {AArch64::FeatureMTE} }] in
def : PStateImm0_15<"TCO",     0b011, 0b100>;
// v8.8a Non-Maskable Interrupts
let Requires = [{ {AArch64::FeatureNMI} }] in
def : PStateImm0_1<"ALLINT",   0b001, 0b000, 0b000>;
// v9.4a Exception-based event profiling
//                  Name,      Op1,   Op2,   Crm_high
def : PStateImm0_1<"PM",       0b001, 0b000, 0b001>;

//===----------------------------------------------------------------------===//
// SVCR instruction options.
//===----------------------------------------------------------------------===//

class SVCR<string name, bits<3> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<3> Encoding;
  let Encoding = encoding;
  code Requires = [{ {} }];
}

let Requires = [{ {AArch64::FeatureSME} }] in {
def : SVCR<"SVCRSM",   0b001>;
def : SVCR<"SVCRZA",   0b010>;
def : SVCR<"SVCRSMZA", 0b011>;
}

//===----------------------------------------------------------------------===//
// PSB instruction options.
//===----------------------------------------------------------------------===//

class PSB<string name, bits<5> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<5> Encoding;
  let Encoding = encoding;
}

def : PSB<"csync", 0x11>;

//===----------------------------------------------------------------------===//
// BTI instruction options.
//===----------------------------------------------------------------------===//

class BTI<string name, bits<3> encoding> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  bits<3> Encoding;
  let Encoding = encoding;
}

def : BTI<"c",  0b010>;
def : BTI<"j",  0b100>;
def : BTI<"jc", 0b110>;

//===----------------------------------------------------------------------===//
// TLBI (translation lookaside buffer invalidate) instruction options.
//===----------------------------------------------------------------------===//

class TLBIEntry<string name, bits<3> op1, bits<4> crn, bits<4> crm,
             bits<3> op2, bit needsreg> {
  string Name = name;
  bits<14> Encoding;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  bit NeedsReg = needsreg;
  list<string> Requires = [];
  list<string> ExtraRequires = [];
  code RequiresStr = [{ { }] # !interleave(Requires # ExtraRequires, [{, }]) # [{ } }];
}

def TLBITable : GenericTable {
  let FilterClass = "TLBIEntry";
  let CppTypeName = "TLBI";
  let Fields = ["Name", "Encoding", "NeedsReg", "RequiresStr"];
}

def lookupTLBIByName : SearchIndex {
  let Table = TLBITable;
  let Key = ["Name"];
}

def lookupTLBIByEncoding : SearchIndex {
  let Table = TLBITable;
  let Key = ["Encoding"];
}

multiclass TLBI<string name, bits<3> op1, bits<4> crn, bits<4> crm,
             bits<3> op2, bit needsreg = 1> {
  def : TLBIEntry<name, op1, crn, crm, op2, needsreg>;
  def : TLBIEntry<!strconcat(name, "nXS"), op1, crn, crm, op2, needsreg> {
    let Encoding{7} = 1;
    let ExtraRequires = ["AArch64::FeatureXS"];
  }
}

defm : TLBI<"IPAS2E1IS",    0b100, 0b1000, 0b0000, 0b001>;
defm : TLBI<"IPAS2LE1IS",   0b100, 0b1000, 0b0000, 0b101>;
defm : TLBI<"VMALLE1IS",    0b000, 0b1000, 0b0011, 0b000, 0>;
defm : TLBI<"ALLE2IS",      0b100, 0b1000, 0b0011, 0b000, 0>;
defm : TLBI<"ALLE3IS",      0b110, 0b1000, 0b0011, 0b000, 0>;
defm : TLBI<"VAE1IS",       0b000, 0b1000, 0b0011, 0b001>;
defm : TLBI<"VAE2IS",       0b100, 0b1000, 0b0011, 0b001>;
defm : TLBI<"VAE3IS",       0b110, 0b1000, 0b0011, 0b001>;
defm : TLBI<"ASIDE1IS",     0b000, 0b1000, 0b0011, 0b010>;
defm : TLBI<"VAAE1IS",      0b000, 0b1000, 0b0011, 0b011>;
defm : TLBI<"ALLE1IS",      0b100, 0b1000, 0b0011, 0b100, 0>;
defm : TLBI<"VALE1IS",      0b000, 0b1000, 0b0011, 0b101>;
defm : TLBI<"VALE2IS",      0b100, 0b1000, 0b0011, 0b101>;
defm : TLBI<"VALE3IS",      0b110, 0b1000, 0b0011, 0b101>;
defm : TLBI<"VMALLS12E1IS", 0b100, 0b1000, 0b0011, 0b110, 0>;
defm : TLBI<"VAALE1IS",     0b000, 0b1000, 0b0011, 0b111>;
defm : TLBI<"IPAS2E1",      0b100, 0b1000, 0b0100, 0b001>;
defm : TLBI<"IPAS2LE1",     0b100, 0b1000, 0b0100, 0b101>;
defm : TLBI<"VMALLE1",      0b000, 0b1000, 0b0111, 0b000, 0>;
defm : TLBI<"ALLE2",        0b100, 0b1000, 0b0111, 0b000, 0>;
defm : TLBI<"ALLE3",        0b110, 0b1000, 0b0111, 0b000, 0>;
defm : TLBI<"VAE1",         0b000, 0b1000, 0b0111, 0b001>;
defm : TLBI<"VAE2",         0b100, 0b1000, 0b0111, 0b001>;
defm : TLBI<"VAE3",         0b110, 0b1000, 0b0111, 0b001>;
defm : TLBI<"ASIDE1",       0b000, 0b1000, 0b0111, 0b010>;
defm : TLBI<"VAAE1",        0b000, 0b1000, 0b0111, 0b011>;
defm : TLBI<"ALLE1",        0b100, 0b1000, 0b0111, 0b100, 0>;
defm : TLBI<"VALE1",        0b000, 0b1000, 0b0111, 0b101>;
defm : TLBI<"VALE2",        0b100, 0b1000, 0b0111, 0b101>;
defm : TLBI<"VALE3",        0b110, 0b1000, 0b0111, 0b101>;
defm : TLBI<"VMALLS12E1",   0b100, 0b1000, 0b0111, 0b110, 0>;
defm : TLBI<"VAALE1",       0b000, 0b1000, 0b0111, 0b111>;

// Armv8.4-A Translation Lookaside Buffer Instructions (TLBI)
let Requires = ["AArch64::FeatureTLB_RMI"] in {
// Armv8.4-A Outer Sharable TLB Maintenance instructions:
//                         op1    CRn     CRm     op2
defm : TLBI<"VMALLE1OS",    0b000, 0b1000, 0b0001, 0b000, 0>;
defm : TLBI<"VAE1OS",       0b000, 0b1000, 0b0001, 0b001>;
defm : TLBI<"ASIDE1OS",     0b000, 0b1000, 0b0001, 0b010>;
defm : TLBI<"VAAE1OS",      0b000, 0b1000, 0b0001, 0b011>;
defm : TLBI<"VALE1OS",      0b000, 0b1000, 0b0001, 0b101>;
defm : TLBI<"VAALE1OS",     0b000, 0b1000, 0b0001, 0b111>;
defm : TLBI<"IPAS2E1OS",    0b100, 0b1000, 0b0100, 0b000>;
defm : TLBI<"IPAS2LE1OS",   0b100, 0b1000, 0b0100, 0b100>;
defm : TLBI<"VAE2OS",       0b100, 0b1000, 0b0001, 0b001>;
defm : TLBI<"VALE2OS",      0b100, 0b1000, 0b0001, 0b101>;
defm : TLBI<"VMALLS12E1OS", 0b100, 0b1000, 0b0001, 0b110, 0>;
defm : TLBI<"VAE3OS",       0b110, 0b1000, 0b0001, 0b001>;
defm : TLBI<"VALE3OS",      0b110, 0b1000, 0b0001, 0b101>;
defm : TLBI<"ALLE2OS",      0b100, 0b1000, 0b0001, 0b000, 0>;
defm : TLBI<"ALLE1OS",      0b100, 0b1000, 0b0001, 0b100, 0>;
defm : TLBI<"ALLE3OS",      0b110, 0b1000, 0b0001, 0b000, 0>;

// Armv8.4-A TLB Range Maintenance instructions:
//                         op1    CRn     CRm     op2
defm : TLBI<"RVAE1",        0b000, 0b1000, 0b0110, 0b001>;
defm : TLBI<"RVAAE1",       0b000, 0b1000, 0b0110, 0b011>;
defm : TLBI<"RVALE1",       0b000, 0b1000, 0b0110, 0b101>;
defm : TLBI<"RVAALE1",      0b000, 0b1000, 0b0110, 0b111>;
defm : TLBI<"RVAE1IS",      0b000, 0b1000, 0b0010, 0b001>;
defm : TLBI<"RVAAE1IS",     0b000, 0b1000, 0b0010, 0b011>;
defm : TLBI<"RVALE1IS",     0b000, 0b1000, 0b0010, 0b101>;
defm : TLBI<"RVAALE1IS",    0b000, 0b1000, 0b0010, 0b111>;
defm : TLBI<"RVAE1OS",      0b000, 0b1000, 0b0101, 0b001>;
defm : TLBI<"RVAAE1OS",     0b000, 0b1000, 0b0101, 0b011>;
defm : TLBI<"RVALE1OS",     0b000, 0b1000, 0b0101, 0b101>;
defm : TLBI<"RVAALE1OS",    0b000, 0b1000, 0b0101, 0b111>;
defm : TLBI<"RIPAS2E1IS",   0b100, 0b1000, 0b0000, 0b010>;
defm : TLBI<"RIPAS2LE1IS",  0b100, 0b1000, 0b0000, 0b110>;
defm : TLBI<"RIPAS2E1",     0b100, 0b1000, 0b0100, 0b010>;
defm : TLBI<"RIPAS2LE1",    0b100, 0b1000, 0b0100, 0b110>;
defm : TLBI<"RIPAS2E1OS",   0b100, 0b1000, 0b0100, 0b011>;
defm : TLBI<"RIPAS2LE1OS",  0b100, 0b1000, 0b0100, 0b111>;
defm : TLBI<"RVAE2",        0b100, 0b1000, 0b0110, 0b001>;
defm : TLBI<"RVALE2",       0b100, 0b1000, 0b0110, 0b101>;
defm : TLBI<"RVAE2IS",      0b100, 0b1000, 0b0010, 0b001>;
defm : TLBI<"RVALE2IS",     0b100, 0b1000, 0b0010, 0b101>;
defm : TLBI<"RVAE2OS",      0b100, 0b1000, 0b0101, 0b001>;
defm : TLBI<"RVALE2OS",     0b100, 0b1000, 0b0101, 0b101>;
defm : TLBI<"RVAE3",        0b110, 0b1000, 0b0110, 0b001>;
defm : TLBI<"RVALE3",       0b110, 0b1000, 0b0110, 0b101>;
defm : TLBI<"RVAE3IS",      0b110, 0b1000, 0b0010, 0b001>;
defm : TLBI<"RVALE3IS",     0b110, 0b1000, 0b0010, 0b101>;
defm : TLBI<"RVAE3OS",      0b110, 0b1000, 0b0101, 0b001>;
defm : TLBI<"RVALE3OS",     0b110, 0b1000, 0b0101, 0b101>;
} //FeatureTLB_RMI

// Armv9-A Realm Management Extention TLBI Instructions
let Requires = ["AArch64::FeatureRME"] in {
defm : TLBI<"RPAOS",        0b110, 0b1000, 0b0100, 0b011>;
defm : TLBI<"RPALOS",       0b110, 0b1000, 0b0100, 0b111>;
defm : TLBI<"PAALLOS",      0b110, 0b1000, 0b0001, 0b100, 0>;
defm : TLBI<"PAALL",        0b110, 0b1000, 0b0111, 0b100, 0>;
}

// Armv9.5-A TLBI VMALL for Dirty State
let Requires = ["AArch64::FeatureTLBIW"] in {
//                           op1,   CRn,    CRm,    op2,   needsreg
defm : TLBI<"VMALLWS2E1",    0b100, 0b1000, 0b0110, 0b010, 0>;
defm : TLBI<"VMALLWS2E1IS",  0b100, 0b1000, 0b0010, 0b010, 0>;
defm : TLBI<"VMALLWS2E1OS",  0b100, 0b1000, 0b0101, 0b010, 0>;
}

//===----------------------------------------------------------------------===//
// MRS/MSR (system register read/write) instruction options.
//===----------------------------------------------------------------------===//

class SysReg<string name, bits<2> op0, bits<3> op1, bits<4> crn, bits<4> crm,
             bits<3> op2> : SearchableTable {
  let SearchableFields = ["Name", "Encoding"];
  let EnumValueField = "Encoding";

  string Name = name;
  string AltName = name;
  bits<16> Encoding;
  let Encoding{15-14} = op0;
  let Encoding{13-11} = op1;
  let Encoding{10-7} = crn;
  let Encoding{6-3} = crm;
  let Encoding{2-0} = op2;
  bit Readable = ?;
  bit Writeable = ?;
  code Requires = [{ {} }];
}

class RWSysReg<string name, bits<2> op0, bits<3> op1, bits<4> crn, bits<4> crm,
               bits<3> op2>
    : SysReg<name, op0, op1, crn, crm, op2> {
  let Readable = 1;
  let Writeable = 1;
}

class ROSysReg<string name, bits<2> op0, bits<3> op1, bits<4> crn, bits<4> crm,
               bits<3> op2>
    : SysReg<name, op0, op1, crn, crm, op2> {
  let Readable = 1;
  let Writeable = 0;
}

class WOSysReg<string name, bits<2> op0, bits<3> op1, bits<4> crn, bits<4> crm,
               bits<3> op2>
    : SysReg<name, op0, op1, crn, crm, op2> {
  let Readable = 0;
  let Writeable = 1;
}

//===----------------------
// Read-only regs
//===----------------------

//                                    Op0    Op1     CRn     CRm    Op2
def : ROSysReg<"MDCCSR_EL0",         0b10, 0b011, 0b0000, 0b0001, 0b000>;
def : ROSysReg<"DBGDTRRX_EL0",       0b10, 0b011, 0b0000, 0b0101, 0b000>;
def : ROSysReg<"MDRAR_EL1",          0b10, 0b000, 0b0001, 0b0000, 0b000>;
def : ROSysReg<"OSLSR_EL1",          0b10, 0b000, 0b0001, 0b0001, 0b100>;
def : ROSysReg<"DBGAUTHSTATUS_EL1",  0b10, 0b000, 0b0111, 0b1110, 0b110>;
def : ROSysReg<"PMCEID0_EL0",        0b11, 0b011, 0b1001, 0b1100, 0b110>;
def : ROSysReg<"PMCEID1_EL0",        0b11, 0b011, 0b1001, 0b1100, 0b111>;
def : ROSysReg<"PMMIR_EL1",          0b11, 0b000, 0b1001, 0b1110, 0b110>;
def : ROSysReg<"MIDR_EL1",           0b11, 0b000, 0b0000, 0b0000, 0b000>;
def : ROSysReg<"CCSIDR_EL1",         0b11, 0b001, 0b0000, 0b0000, 0b000>;

//v8.3 CCIDX - extending the CCsIDr number of sets
def : ROSysReg<"CCSIDR2_EL1",        0b11, 0b001, 0b0000, 0b0000, 0b010> {
  let Requires = [{ {AArch64::FeatureCCIDX} }];
}
def : ROSysReg<"CLIDR_EL1",          0b11, 0b001, 0b0000, 0b0000, 0b001>;
def : ROSysReg<"CTR_EL0",            0b11, 0b011, 0b0000, 0b0000, 0b001>;
def : ROSysReg<"MPIDR_EL1",          0b11, 0b000, 0b0000, 0b0000, 0b101>;
def : ROSysReg<"REVIDR_EL1",         0b11, 0b000, 0b0000, 0b0000, 0b110>;
def : ROSysReg<"AIDR_EL1",           0b11, 0b001, 0b0000, 0b0000, 0b111>;
def : ROSysReg<"DCZID_EL0",          0b11, 0b011, 0b0000, 0b0000, 0b111>;
def : ROSysReg<"ID_PFR0_EL1",        0b11, 0b000, 0b0000, 0b0001, 0b000>;
def : ROSysReg<"ID_PFR1_EL1",        0b11, 0b000, 0b0000, 0b0001, 0b001>;
def : ROSysReg<"ID_PFR2_EL1",        0b11, 0b000, 0b0000, 0b0011, 0b100> {
    let Requires = [{ {AArch64::FeatureSpecRestrict} }];
}
def : ROSysReg<"ID_DFR0_EL1",        0b11, 0b000, 0b0000, 0b0001, 0b010>;
def : ROSysReg<"ID_DFR1_EL1",        0b11, 0b000, 0b0000, 0b0011, 0b101>;
def : ROSysReg<"ID_AFR0_EL1",        0b11, 0b000, 0b0000, 0b0001, 0b011>;
def : ROSysReg<"ID_MMFR0_EL1",       0b11, 0b000, 0b0000, 0b0001, 0b100>;
def : ROSysReg<"ID_MMFR1_EL1",       0b11, 0b000, 0b0000, 0b0001, 0b101>;
def : ROSysReg<"ID_MMFR2_EL1",       0b11, 0b000, 0b0000, 0b0001, 0b110>;
def : ROSysReg<"ID_MMFR3_EL1",       0b11, 0b000, 0b0000, 0b0001, 0b111>;
def : ROSysReg<"ID_ISAR0_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b000>;
def : ROSysReg<"ID_ISAR1_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b001>;
def : ROSysReg<"ID_ISAR2_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b010>;
def : ROSysReg<"ID_ISAR3_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b011>;
def : ROSysReg<"ID_ISAR4_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b100>;
def : ROSysReg<"ID_ISAR5_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b101>;
def : ROSysReg<"ID_ISAR6_EL1",       0b11, 0b000, 0b0000, 0b0010, 0b111> {
  let Requires = [{ {AArch64::HasV8_2aOps} }];
}
def : ROSysReg<"ID_AA64PFR0_EL1",     0b11, 0b000, 0b0000, 0b0100, 0b000>;
def : ROSysReg<"ID_AA64PFR1_EL1",     0b11, 0b000, 0b0000, 0b0100, 0b001>;
def : ROSysReg<"ID_AA64PFR2_EL1",     0b11, 0b000, 0b0000, 0b0100, 0b010>;
def : ROSysReg<"ID_AA64DFR0_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b000>;
def : ROSysReg<"ID_AA64DFR1_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b001>;
def : ROSysReg<"ID_AA64DFR2_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b010>;
def : ROSysReg<"ID_AA64AFR0_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b100>;
def : ROSysReg<"ID_AA64AFR1_EL1",     0b11, 0b000, 0b0000, 0b0101, 0b101>;
def : ROSysReg<"ID_AA64ISAR0_EL1",    0b11, 0b000, 0b0000, 0b0110, 0b000>;
def : ROSysReg<"ID_AA64ISAR1_EL1",    0b11, 0b000, 0b0000, 0b0110, 0b001>;
def : ROSysReg<"ID_AA64ISAR2_EL1",    0b11, 0b000, 0b0000, 0b0110, 0b010>;
def : ROSysReg<"ID_AA64ISAR3_EL1",    0b11, 0b000, 0b0000, 0b0110, 0b011>;
def : ROSysReg<"ID_AA64MMFR0_EL1",    0b11, 0b000, 0b0000, 0b0111, 0b000>;
def : ROSysReg<"ID_AA64MMFR1_EL1",    0b11, 0b000, 0b0000, 0b0111, 0b001>;
def : ROSysReg<"ID_AA64MMFR2_EL1",    0b11, 0b000, 0b0000, 0b0111, 0b010>;
def : ROSysReg<"ID_AA64MMFR3_EL1",    0b11, 0b000, 0b0000, 0b0111, 0b011>;
def : ROSysReg<"ID_AA64MMFR4_EL1",    0b11, 0b000, 0b0000, 0b0111, 0b100>;
def : ROSysReg<"MVFR0_EL1",           0b11, 0b000, 0b0000, 0b0011, 0b000>;
def : ROSysReg<"MVFR1_EL1",           0b11, 0b000, 0b0000, 0b0011, 0b001>;
def : ROSysReg<"MVFR2_EL1",           0b11, 0b000, 0b0000, 0b0011, 0b010>;
def : ROSysReg<"RVBAR_EL1",           0b11, 0b000, 0b1100, 0b0000, 0b001>;
def : ROSysReg<"RVBAR_EL2",           0b11, 0b100, 0b1100, 0b0000, 0b001>;
def : ROSysReg<"RVBAR_EL3",           0b11, 0b110, 0b1100, 0b0000, 0b001>;
def : ROSysReg<"ISR_EL1",             0b11, 0b000, 0b1100, 0b0001, 0b000>;
def : ROSysReg<"CNTPCT_EL0",          0b11, 0b011, 0b1110, 0b0000, 0b001>;
def : ROSysReg<"CNTVCT_EL0",          0b11, 0b011, 0b1110, 0b0000, 0b010>;
def : ROSysReg<"ID_MMFR4_EL1",        0b11, 0b000, 0b0000, 0b0010, 0b110>;
def : ROSysReg<"ID_MMFR5_EL1",        0b11, 0b000, 0b0000, 0b0011, 0b110>;

// Trace registers
//                                   Op0    Op1     CRn     CRm    Op2
def : ROSysReg<"TRCSTATR",           0b10, 0b001, 0b0000, 0b0011, 0b000>;
def : ROSysReg<"TRCIDR8",            0b10, 0b001, 0b0000, 0b0000, 0b110>;
def : ROSysReg<"TRCIDR9",            0b10, 0b001, 0b0000, 0b0001, 0b110>;
def : ROSysReg<"TRCIDR10",           0b10, 0b001, 0b0000, 0b0010, 0b110>;
def : ROSysReg<"TRCIDR11",           0b10, 0b001, 0b0000, 0b0011, 0b110>;
def : ROSysReg<"TRCIDR12",           0b10, 0b001, 0b0000, 0b0100, 0b110>;
def : ROSysReg<"TRCIDR13",           0b10, 0b001, 0b0000, 0b0101, 0b110>;
def : ROSysReg<"TRCIDR0",            0b10, 0b001, 0b0000, 0b1000, 0b111>;
def : ROSysReg<"TRCIDR1",            0b10, 0b001, 0b0000, 0b1001, 0b111>;
def : ROSysReg<"TRCIDR2",            0b10, 0b001, 0b0000, 0b1010, 0b111>;
def : ROSysReg<"TRCIDR3",            0b10, 0b001, 0b0000, 0b1011, 0b111>;
def : ROSysReg<"TRCIDR4",            0b10, 0b001, 0b0000, 0b1100, 0b111>;
def : ROSysReg<"TRCIDR5",            0b10, 0b001, 0b0000, 0b1101, 0b111>;
def : ROSysReg<"TRCIDR6",            0b10, 0b001, 0b0000, 0b1110, 0b111>;
def : ROSysReg<"TRCIDR7",            0b10, 0b001, 0b0000, 0b1111, 0b111>;
def : ROSysReg<"TRCOSLSR",           0b10, 0b001, 0b0001, 0b0001, 0b100>;
def : ROSysReg<"TRCPDSR",            0b10, 0b001, 0b0001, 0b0101, 0b100>;
def : ROSysReg<"TRCDEVAFF0",         0b10, 0b001, 0b0111, 0b1010, 0b110>;
def : ROSysReg<"TRCDEVAFF1",         0b10, 0b001, 0b0111, 0b1011, 0b110>;
def : ROSysReg<"TRCLSR",             0b10, 0b001, 0b0111, 0b1101, 0b110>;
def : ROSysReg<"TRCAUTHSTATUS",      0b10, 0b001, 0b0111, 0b1110, 0b110>;
def : ROSysReg<"TRCDEVARCH",         0b10, 0b001, 0b0111, 0b1111, 0b110>;
def : ROSysReg<"TRCDEVID",           0b10, 0b001, 0b0111, 0b0010, 0b111>;
def : ROSysReg<"TRCDEVTYPE",         0b10, 0b001, 0b0111, 0b0011, 0b111>;
def : ROSysReg<"TRCPIDR4",           0b10, 0b001, 0b0111, 0b0100, 0b111>;
def : ROSysReg<"TRCPIDR5",           0b10, 0b001, 0b0111, 0b0101, 0b111>;
def : ROSysReg<"TRCPIDR6",           0b10, 0b001, 0b0111, 0b0110, 0b111>;
def : ROSysReg<"TRCPIDR7",           0b10, 0b001, 0b0111, 0b0111, 0b111>;
def : ROSysReg<"TRCPIDR0",           0b10, 0b001, 0b0111, 0b1000, 0b111>;
def : ROSysReg<"TRCPIDR1",           0b10, 0b001, 0b0111, 0b1001, 0b111>;
def : ROSysReg<"TRCPIDR2",           0b10, 0b001, 0b0111, 0b1010, 0b111>;
def : ROSysReg<"TRCPIDR3",           0b10, 0b001, 0b0111, 0b1011, 0b111>;
def : ROSysReg<"TRCCIDR0",           0b10, 0b001, 0b0111, 0b1100, 0b111>;
def : ROSysReg<"TRCCIDR1",           0b10, 0b001, 0b0111, 0b1101, 0b111>;
def : ROSysReg<"TRCCIDR2",           0b10, 0b001, 0b0111, 0b1110, 0b111>;
def : ROSysReg<"TRCCIDR3",           0b10, 0b001, 0b0111, 0b1111, 0b111>;

// GICv3 registers
//                                 Op0    Op1     CRn     CRm    Op2
def : ROSysReg<"ICC_IAR1_EL1",       0b11, 0b000, 0b1100, 0b1100, 0b000>;
def : ROSysReg<"ICC_IAR0_EL1",       0b11, 0b000, 0b1100, 0b1000, 0b000>;
def : ROSysReg<"ICC_HPPIR1_EL1",     0b11, 0b000, 0b1100, 0b1100, 0b010>;
def : ROSysReg<"ICC_HPPIR0_EL1",     0b11, 0b000, 0b1100, 0b1000, 0b010>;
def : ROSysReg<"ICC_RPR_EL1",        0b11, 0b000, 0b1100, 0b1011, 0b011>;
def : ROSysReg<"ICH_VTR_EL2",        0b11, 0b100, 0b1100, 0b1011, 0b001>;
def : ROSysReg<"ICH_EISR_EL2",       0b11, 0b100, 0b1100, 0b1011, 0b011>;
def : ROSysReg<"ICH_ELRSR_EL2",      0b11, 0b100, 0b1100, 0b1011, 0b101>;

// SVE control registers
//                                   Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureSVE} }] in {
def : ROSysReg<"ID_AA64ZFR0_EL1",    0b11, 0b000, 0b0000, 0b0100, 0b100>;
}

// v8.1a "Limited Ordering Regions" extension-specific system register
//                         Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureLOR} }] in
def : ROSysReg<"LORID_EL1",  0b11, 0b000, 0b1010, 0b0100, 0b111>;

// v8.2a "RAS extension" registers
//                         Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureRAS} }] in {
def : ROSysReg<"ERRIDR_EL1", 0b11, 0b000, 0b0101, 0b0011, 0b000>;
def : ROSysReg<"ERXFR_EL1",  0b11, 0b000, 0b0101, 0b0100, 0b000>;
}

// v8.5a "random number" registers
//                       Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureRandGen} }] in {
def : ROSysReg<"RNDR",   0b11, 0b011, 0b0010, 0b0100, 0b000>;
def : ROSysReg<"RNDRRS", 0b11, 0b011, 0b0010, 0b0100, 0b001>;
}

// v8.5a Software Context Number registers
let Requires = [{ {AArch64::FeatureSpecRestrict} }] in {
def : RWSysReg<"SCXTNUM_EL0", 0b11, 0b011, 0b1101, 0b0000, 0b111>;
def : RWSysReg<"SCXTNUM_EL1", 0b11, 0b000, 0b1101, 0b0000, 0b111>;
def : RWSysReg<"SCXTNUM_EL2", 0b11, 0b100, 0b1101, 0b0000, 0b111>;
def : RWSysReg<"SCXTNUM_EL3", 0b11, 0b110, 0b1101, 0b0000, 0b111>;
def : RWSysReg<"SCXTNUM_EL12", 0b11, 0b101, 0b1101, 0b0000, 0b111>;
}

// v9a Realm Management Extension registers
let Requires = [{ {AArch64::FeatureRME} }] in {
def : RWSysReg<"GPCCR_EL3", 0b11, 0b110, 0b0010, 0b0001, 0b110>;
def : RWSysReg<"GPTBR_EL3", 0b11, 0b110, 0b0010, 0b0001, 0b100>;
}
// MFAR_EL3 is part of both FEAT_RME and FEAT_PFAR (further below). The latter
// is unconditional so this register has to be too.
def : RWSysReg<"MFAR_EL3",  0b11, 0b110, 0b0110, 0b0000, 0b101>;

// v9a Memory Encryption Contexts Extension registers
let Requires = [{ {AArch64::FeatureMEC} }] in {
def : ROSysReg<"MECIDR_EL2",     0b11, 0b100, 0b1010, 0b1000, 0b111>;
def : RWSysReg<"MECID_P0_EL2",   0b11, 0b100, 0b1010, 0b1000, 0b000>;
def : RWSysReg<"MECID_A0_EL2",   0b11, 0b100, 0b1010, 0b1000, 0b001>;
def : RWSysReg<"MECID_P1_EL2",   0b11, 0b100, 0b1010, 0b1000, 0b010>;
def : RWSysReg<"MECID_A1_EL2",   0b11, 0b100, 0b1010, 0b1000, 0b011>;
def : RWSysReg<"VMECID_P_EL2",   0b11, 0b100, 0b1010, 0b1001, 0b000>;
def : RWSysReg<"VMECID_A_EL2",   0b11, 0b100, 0b1010, 0b1001, 0b001>;
def : RWSysReg<"MECID_RL_A_EL3", 0b11, 0b110, 0b1010, 0b1010, 0b001>;
}

// v9-a Scalable Matrix Extension (SME) registers
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureSME} }] in {
def : ROSysReg<"ID_AA64SMFR0_EL1", 0b11, 0b000, 0b0000, 0b0100, 0b101>;
}

//===----------------------
// Write-only regs
//===----------------------

//                                 Op0    Op1     CRn     CRm    Op2
def : WOSysReg<"DBGDTRTX_EL0",       0b10, 0b011, 0b0000, 0b0101, 0b000>;
def : WOSysReg<"OSLAR_EL1",          0b10, 0b000, 0b0001, 0b0000, 0b100>;
def : WOSysReg<"PMSWINC_EL0",        0b11, 0b011, 0b1001, 0b1100, 0b100>;

// Trace Registers
//                                 Op0    Op1     CRn     CRm    Op2
def : WOSysReg<"TRCOSLAR",           0b10, 0b001, 0b0001, 0b0000, 0b100>;
def : WOSysReg<"TRCLAR",             0b10, 0b001, 0b0111, 0b1100, 0b110>;

// GICv3 registers
//                                 Op0    Op1     CRn     CRm    Op2
def : WOSysReg<"ICC_EOIR1_EL1",      0b11, 0b000, 0b1100, 0b1100, 0b001>;
def : WOSysReg<"ICC_EOIR0_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b001>;
def : WOSysReg<"ICC_DIR_EL1",        0b11, 0b000, 0b1100, 0b1011, 0b001>;
def : WOSysReg<"ICC_SGI1R_EL1",      0b11, 0b000, 0b1100, 0b1011, 0b101>;
def : WOSysReg<"ICC_ASGI1R_EL1",     0b11, 0b000, 0b1100, 0b1011, 0b110>;
def : WOSysReg<"ICC_SGI0R_EL1",      0b11, 0b000, 0b1100, 0b1011, 0b111>;

//===----------------------
// Read-write regs
//===----------------------

//                                   Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"OSDTRRX_EL1",        0b10, 0b000, 0b0000, 0b0000, 0b010>;
def : RWSysReg<"OSDTRTX_EL1",        0b10, 0b000, 0b0000, 0b0011, 0b010>;
def : RWSysReg<"TEECR32_EL1",        0b10, 0b010, 0b0000, 0b0000, 0b000>;
def : RWSysReg<"MDCCINT_EL1",        0b10, 0b000, 0b0000, 0b0010, 0b000>;
def : RWSysReg<"MDSCR_EL1",          0b10, 0b000, 0b0000, 0b0010, 0b010>;
def : RWSysReg<"DBGDTR_EL0",         0b10, 0b011, 0b0000, 0b0100, 0b000>;
def : RWSysReg<"OSECCR_EL1",         0b10, 0b000, 0b0000, 0b0110, 0b010>;
def : RWSysReg<"DBGVCR32_EL2",       0b10, 0b100, 0b0000, 0b0111, 0b000>;
foreach n = 0-15 in {
  defvar nb = !cast<bits<4>>(n);
  //                                 Op0   Op1    CRn     CRm Op2
  def : RWSysReg<"DBGBVR"#n#"_EL1",  0b10, 0b000, 0b0000, nb, 0b100>;
  def : RWSysReg<"DBGBCR"#n#"_EL1",  0b10, 0b000, 0b0000, nb, 0b101>;
  def : RWSysReg<"DBGWVR"#n#"_EL1",  0b10, 0b000, 0b0000, nb, 0b110>;
  def : RWSysReg<"DBGWCR"#n#"_EL1",  0b10, 0b000, 0b0000, nb, 0b111>;
}
//                                   Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"TEEHBR32_EL1",       0b10, 0b010, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"OSDLR_EL1",          0b10, 0b000, 0b0001, 0b0011, 0b100>;
def : RWSysReg<"DBGPRCR_EL1",        0b10, 0b000, 0b0001, 0b0100, 0b100>;
def : RWSysReg<"DBGCLAIMSET_EL1",    0b10, 0b000, 0b0111, 0b1000, 0b110>;
def : RWSysReg<"DBGCLAIMCLR_EL1",    0b10, 0b000, 0b0111, 0b1001, 0b110>;
def : RWSysReg<"CSSELR_EL1",         0b11, 0b010, 0b0000, 0b0000, 0b000>;
def : RWSysReg<"VPIDR_EL2",          0b11, 0b100, 0b0000, 0b0000, 0b000>;
def : RWSysReg<"VMPIDR_EL2",         0b11, 0b100, 0b0000, 0b0000, 0b101>;
def : RWSysReg<"CPACR_EL1",          0b11, 0b000, 0b0001, 0b0000, 0b010>;
def : RWSysReg<"SCTLR_EL1",          0b11, 0b000, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"SCTLR_EL2",          0b11, 0b100, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"SCTLR_EL3",          0b11, 0b110, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"ACTLR_EL1",          0b11, 0b000, 0b0001, 0b0000, 0b001>;
def : RWSysReg<"ACTLR_EL2",          0b11, 0b100, 0b0001, 0b0000, 0b001>;
def : RWSysReg<"ACTLR_EL3",          0b11, 0b110, 0b0001, 0b0000, 0b001>;
def : RWSysReg<"HCR_EL2",            0b11, 0b100, 0b0001, 0b0001, 0b000>;
def : RWSysReg<"HCRX_EL2",           0b11, 0b100, 0b0001, 0b0010, 0b010> {
  let Requires = [{ {AArch64::FeatureHCX} }];
}
def : RWSysReg<"SCR_EL3",            0b11, 0b110, 0b0001, 0b0001, 0b000>;
def : RWSysReg<"MDCR_EL2",           0b11, 0b100, 0b0001, 0b0001, 0b001>;
def : RWSysReg<"SDER32_EL3",         0b11, 0b110, 0b0001, 0b0001, 0b001>;
def : RWSysReg<"CPTR_EL2",           0b11, 0b100, 0b0001, 0b0001, 0b010>;
def : RWSysReg<"CPTR_EL3",           0b11, 0b110, 0b0001, 0b0001, 0b010>;
def : RWSysReg<"HSTR_EL2",           0b11, 0b100, 0b0001, 0b0001, 0b011>;
def : RWSysReg<"HACR_EL2",           0b11, 0b100, 0b0001, 0b0001, 0b111>;
def : RWSysReg<"MDCR_EL3",           0b11, 0b110, 0b0001, 0b0011, 0b001>;
def : RWSysReg<"TTBR0_EL1",          0b11, 0b000, 0b0010, 0b0000, 0b000>;
def : RWSysReg<"TTBR0_EL3",          0b11, 0b110, 0b0010, 0b0000, 0b000>;

let Requires = [{ {AArch64::FeatureEL2VMSA} }] in {
def : RWSysReg<"TTBR0_EL2",          0b11, 0b100, 0b0010, 0b0000, 0b000> {
  let AltName = "VSCTLR_EL2";
}
def : RWSysReg<"VTTBR_EL2",          0b11, 0b100, 0b0010, 0b0001, 0b000>;
}

def : RWSysReg<"TTBR1_EL1",          0b11, 0b000, 0b0010, 0b0000, 0b001>;
def : RWSysReg<"TCR_EL1",            0b11, 0b000, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"TCR_EL2",            0b11, 0b100, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"TCR_EL3",            0b11, 0b110, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"VTCR_EL2",           0b11, 0b100, 0b0010, 0b0001, 0b010>;
def : RWSysReg<"DACR32_EL2",         0b11, 0b100, 0b0011, 0b0000, 0b000>;
def : RWSysReg<"SPSR_EL1",           0b11, 0b000, 0b0100, 0b0000, 0b000>;
def : RWSysReg<"SPSR_EL2",           0b11, 0b100, 0b0100, 0b0000, 0b000>;
def : RWSysReg<"SPSR_EL3",           0b11, 0b110, 0b0100, 0b0000, 0b000>;
def : RWSysReg<"ELR_EL1",            0b11, 0b000, 0b0100, 0b0000, 0b001>;
def : RWSysReg<"ELR_EL2",            0b11, 0b100, 0b0100, 0b0000, 0b001>;
def : RWSysReg<"ELR_EL3",            0b11, 0b110, 0b0100, 0b0000, 0b001>;
def : RWSysReg<"SP_EL0",             0b11, 0b000, 0b0100, 0b0001, 0b000>;
def : RWSysReg<"SP_EL1",             0b11, 0b100, 0b0100, 0b0001, 0b000>;
def : RWSysReg<"SP_EL2",             0b11, 0b110, 0b0100, 0b0001, 0b000>;
def : RWSysReg<"SPSel",              0b11, 0b000, 0b0100, 0b0010, 0b000>;
def : RWSysReg<"NZCV",               0b11, 0b011, 0b0100, 0b0010, 0b000>;
def : RWSysReg<"DAIF",               0b11, 0b011, 0b0100, 0b0010, 0b001>;
def : ROSysReg<"CurrentEL",          0b11, 0b000, 0b0100, 0b0010, 0b010>;
def : RWSysReg<"SPSR_irq",           0b11, 0b100, 0b0100, 0b0011, 0b000>;
def : RWSysReg<"SPSR_abt",           0b11, 0b100, 0b0100, 0b0011, 0b001>;
def : RWSysReg<"SPSR_und",           0b11, 0b100, 0b0100, 0b0011, 0b010>;
def : RWSysReg<"SPSR_fiq",           0b11, 0b100, 0b0100, 0b0011, 0b011>;
let Requires = [{ {AArch64::FeatureFPARMv8} }] in {
def : RWSysReg<"FPCR",               0b11, 0b011, 0b0100, 0b0100, 0b000>;
def : RWSysReg<"FPSR",               0b11, 0b011, 0b0100, 0b0100, 0b001>;
}
def : RWSysReg<"DSPSR_EL0",          0b11, 0b011, 0b0100, 0b0101, 0b000>;
def : RWSysReg<"DLR_EL0",            0b11, 0b011, 0b0100, 0b0101, 0b001>;
def : RWSysReg<"IFSR32_EL2",         0b11, 0b100, 0b0101, 0b0000, 0b001>;
def : RWSysReg<"AFSR0_EL1",          0b11, 0b000, 0b0101, 0b0001, 0b000>;
def : RWSysReg<"AFSR0_EL2",          0b11, 0b100, 0b0101, 0b0001, 0b000>;
def : RWSysReg<"AFSR0_EL3",          0b11, 0b110, 0b0101, 0b0001, 0b000>;
def : RWSysReg<"AFSR1_EL1",          0b11, 0b000, 0b0101, 0b0001, 0b001>;
def : RWSysReg<"AFSR1_EL2",          0b11, 0b100, 0b0101, 0b0001, 0b001>;
def : RWSysReg<"AFSR1_EL3",          0b11, 0b110, 0b0101, 0b0001, 0b001>;
def : RWSysReg<"ESR_EL1",            0b11, 0b000, 0b0101, 0b0010, 0b000>;
def : RWSysReg<"ESR_EL2",            0b11, 0b100, 0b0101, 0b0010, 0b000>;
def : RWSysReg<"ESR_EL3",            0b11, 0b110, 0b0101, 0b0010, 0b000>;
def : RWSysReg<"FPEXC32_EL2",        0b11, 0b100, 0b0101, 0b0011, 0b000>;
def : RWSysReg<"FAR_EL1",            0b11, 0b000, 0b0110, 0b0000, 0b000>;
def : RWSysReg<"FAR_EL2",            0b11, 0b100, 0b0110, 0b0000, 0b000>;
def : RWSysReg<"FAR_EL3",            0b11, 0b110, 0b0110, 0b0000, 0b000>;
def : RWSysReg<"HPFAR_EL2",          0b11, 0b100, 0b0110, 0b0000, 0b100>;
def : RWSysReg<"PAR_EL1",            0b11, 0b000, 0b0111, 0b0100, 0b000>;
def : RWSysReg<"PMCR_EL0",           0b11, 0b011, 0b1001, 0b1100, 0b000>;
def : RWSysReg<"PMCNTENSET_EL0",     0b11, 0b011, 0b1001, 0b1100, 0b001>;
def : RWSysReg<"PMCNTENCLR_EL0",     0b11, 0b011, 0b1001, 0b1100, 0b010>;
def : RWSysReg<"PMOVSCLR_EL0",       0b11, 0b011, 0b1001, 0b1100, 0b011>;
def : RWSysReg<"PMSELR_EL0",         0b11, 0b011, 0b1001, 0b1100, 0b101>;
def : RWSysReg<"PMCCNTR_EL0",        0b11, 0b011, 0b1001, 0b1101, 0b000>;
def : RWSysReg<"PMXEVTYPER_EL0",     0b11, 0b011, 0b1001, 0b1101, 0b001>;
def : RWSysReg<"PMXEVCNTR_EL0",      0b11, 0b011, 0b1001, 0b1101, 0b010>;
def : RWSysReg<"PMUSERENR_EL0",      0b11, 0b011, 0b1001, 0b1110, 0b000>;
def : RWSysReg<"PMINTENSET_EL1",     0b11, 0b000, 0b1001, 0b1110, 0b001>;
def : RWSysReg<"PMINTENCLR_EL1",     0b11, 0b000, 0b1001, 0b1110, 0b010>;
def : RWSysReg<"PMOVSSET_EL0",       0b11, 0b011, 0b1001, 0b1110, 0b011>;
def : RWSysReg<"MAIR_EL1",           0b11, 0b000, 0b1010, 0b0010, 0b000>;
def : RWSysReg<"MAIR_EL2",           0b11, 0b100, 0b1010, 0b0010, 0b000>;
def : RWSysReg<"MAIR_EL3",           0b11, 0b110, 0b1010, 0b0010, 0b000>;
def : RWSysReg<"AMAIR_EL1",          0b11, 0b000, 0b1010, 0b0011, 0b000>;
def : RWSysReg<"AMAIR_EL2",          0b11, 0b100, 0b1010, 0b0011, 0b000>;
def : RWSysReg<"AMAIR_EL3",          0b11, 0b110, 0b1010, 0b0011, 0b000>;
def : RWSysReg<"VBAR_EL1",           0b11, 0b000, 0b1100, 0b0000, 0b000>;
def : RWSysReg<"VBAR_EL2",           0b11, 0b100, 0b1100, 0b0000, 0b000>;
def : RWSysReg<"VBAR_EL3",           0b11, 0b110, 0b1100, 0b0000, 0b000>;
def : RWSysReg<"RMR_EL1",            0b11, 0b000, 0b1100, 0b0000, 0b010>;
def : RWSysReg<"RMR_EL2",            0b11, 0b100, 0b1100, 0b0000, 0b010>;
def : RWSysReg<"RMR_EL3",            0b11, 0b110, 0b1100, 0b0000, 0b010>;
def : RWSysReg<"CONTEXTIDR_EL1",     0b11, 0b000, 0b1101, 0b0000, 0b001>;
def : RWSysReg<"TPIDR_EL0",          0b11, 0b011, 0b1101, 0b0000, 0b010>;
def : RWSysReg<"TPIDR_EL2",          0b11, 0b100, 0b1101, 0b0000, 0b010>;
def : RWSysReg<"TPIDR_EL3",          0b11, 0b110, 0b1101, 0b0000, 0b010>;
def : RWSysReg<"TPIDRRO_EL0",        0b11, 0b011, 0b1101, 0b0000, 0b011>;
def : RWSysReg<"TPIDR_EL1",          0b11, 0b000, 0b1101, 0b0000, 0b100>;
def : RWSysReg<"CNTFRQ_EL0",         0b11, 0b011, 0b1110, 0b0000, 0b000>;
def : RWSysReg<"CNTVOFF_EL2",        0b11, 0b100, 0b1110, 0b0000, 0b011>;
def : RWSysReg<"CNTKCTL_EL1",        0b11, 0b000, 0b1110, 0b0001, 0b000>;
def : RWSysReg<"CNTHCTL_EL2",        0b11, 0b100, 0b1110, 0b0001, 0b000>;
def : RWSysReg<"CNTP_TVAL_EL0",      0b11, 0b011, 0b1110, 0b0010, 0b000>;
def : RWSysReg<"CNTHP_TVAL_EL2",     0b11, 0b100, 0b1110, 0b0010, 0b000>;
def : RWSysReg<"CNTPS_TVAL_EL1",     0b11, 0b111, 0b1110, 0b0010, 0b000>;
def : RWSysReg<"CNTP_CTL_EL0",       0b11, 0b011, 0b1110, 0b0010, 0b001>;
def : RWSysReg<"CNTHP_CTL_EL2",      0b11, 0b100, 0b1110, 0b0010, 0b001>;
def : RWSysReg<"CNTPS_CTL_EL1",      0b11, 0b111, 0b1110, 0b0010, 0b001>;
def : RWSysReg<"CNTP_CVAL_EL0",      0b11, 0b011, 0b1110, 0b0010, 0b010>;
def : RWSysReg<"CNTHP_CVAL_EL2",     0b11, 0b100, 0b1110, 0b0010, 0b010>;
def : RWSysReg<"CNTPS_CVAL_EL1",     0b11, 0b111, 0b1110, 0b0010, 0b010>;
def : RWSysReg<"CNTV_TVAL_EL0",      0b11, 0b011, 0b1110, 0b0011, 0b000>;
def : RWSysReg<"CNTV_CTL_EL0",       0b11, 0b011, 0b1110, 0b0011, 0b001>;
def : RWSysReg<"CNTV_CVAL_EL0",      0b11, 0b011, 0b1110, 0b0011, 0b010>;
def : RWSysReg<"PMEVCNTR0_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b000>;
def : RWSysReg<"PMEVCNTR1_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b001>;
def : RWSysReg<"PMEVCNTR2_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b010>;
def : RWSysReg<"PMEVCNTR3_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b011>;
def : RWSysReg<"PMEVCNTR4_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b100>;
def : RWSysReg<"PMEVCNTR5_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b101>;
def : RWSysReg<"PMEVCNTR6_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b110>;
def : RWSysReg<"PMEVCNTR7_EL0",      0b11, 0b011, 0b1110, 0b1000, 0b111>;
def : RWSysReg<"PMEVCNTR8_EL0",      0b11, 0b011, 0b1110, 0b1001, 0b000>;
def : RWSysReg<"PMEVCNTR9_EL0",      0b11, 0b011, 0b1110, 0b1001, 0b001>;
def : RWSysReg<"PMEVCNTR10_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b010>;
def : RWSysReg<"PMEVCNTR11_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b011>;
def : RWSysReg<"PMEVCNTR12_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b100>;
def : RWSysReg<"PMEVCNTR13_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b101>;
def : RWSysReg<"PMEVCNTR14_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b110>;
def : RWSysReg<"PMEVCNTR15_EL0",     0b11, 0b011, 0b1110, 0b1001, 0b111>;
def : RWSysReg<"PMEVCNTR16_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b000>;
def : RWSysReg<"PMEVCNTR17_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b001>;
def : RWSysReg<"PMEVCNTR18_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b010>;
def : RWSysReg<"PMEVCNTR19_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b011>;
def : RWSysReg<"PMEVCNTR20_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b100>;
def : RWSysReg<"PMEVCNTR21_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b101>;
def : RWSysReg<"PMEVCNTR22_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b110>;
def : RWSysReg<"PMEVCNTR23_EL0",     0b11, 0b011, 0b1110, 0b1010, 0b111>;
def : RWSysReg<"PMEVCNTR24_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b000>;
def : RWSysReg<"PMEVCNTR25_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b001>;
def : RWSysReg<"PMEVCNTR26_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b010>;
def : RWSysReg<"PMEVCNTR27_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b011>;
def : RWSysReg<"PMEVCNTR28_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b100>;
def : RWSysReg<"PMEVCNTR29_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b101>;
def : RWSysReg<"PMEVCNTR30_EL0",     0b11, 0b011, 0b1110, 0b1011, 0b110>;
def : RWSysReg<"PMCCFILTR_EL0",      0b11, 0b011, 0b1110, 0b1111, 0b111>;
def : RWSysReg<"PMEVTYPER0_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b000>;
def : RWSysReg<"PMEVTYPER1_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b001>;
def : RWSysReg<"PMEVTYPER2_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b010>;
def : RWSysReg<"PMEVTYPER3_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b011>;
def : RWSysReg<"PMEVTYPER4_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b100>;
def : RWSysReg<"PMEVTYPER5_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b101>;
def : RWSysReg<"PMEVTYPER6_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b110>;
def : RWSysReg<"PMEVTYPER7_EL0",     0b11, 0b011, 0b1110, 0b1100, 0b111>;
def : RWSysReg<"PMEVTYPER8_EL0",     0b11, 0b011, 0b1110, 0b1101, 0b000>;
def : RWSysReg<"PMEVTYPER9_EL0",     0b11, 0b011, 0b1110, 0b1101, 0b001>;
def : RWSysReg<"PMEVTYPER10_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b010>;
def : RWSysReg<"PMEVTYPER11_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b011>;
def : RWSysReg<"PMEVTYPER12_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b100>;
def : RWSysReg<"PMEVTYPER13_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b101>;
def : RWSysReg<"PMEVTYPER14_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b110>;
def : RWSysReg<"PMEVTYPER15_EL0",    0b11, 0b011, 0b1110, 0b1101, 0b111>;
def : RWSysReg<"PMEVTYPER16_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b000>;
def : RWSysReg<"PMEVTYPER17_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b001>;
def : RWSysReg<"PMEVTYPER18_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b010>;
def : RWSysReg<"PMEVTYPER19_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b011>;
def : RWSysReg<"PMEVTYPER20_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b100>;
def : RWSysReg<"PMEVTYPER21_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b101>;
def : RWSysReg<"PMEVTYPER22_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b110>;
def : RWSysReg<"PMEVTYPER23_EL0",    0b11, 0b011, 0b1110, 0b1110, 0b111>;
def : RWSysReg<"PMEVTYPER24_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b000>;
def : RWSysReg<"PMEVTYPER25_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b001>;
def : RWSysReg<"PMEVTYPER26_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b010>;
def : RWSysReg<"PMEVTYPER27_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b011>;
def : RWSysReg<"PMEVTYPER28_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b100>;
def : RWSysReg<"PMEVTYPER29_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b101>;
def : RWSysReg<"PMEVTYPER30_EL0",    0b11, 0b011, 0b1110, 0b1111, 0b110>;

// Trace registers
//                                 Op0    Op1     CRn     CRm    Op2
def : RWSysReg<"TRCPRGCTLR",         0b10, 0b001, 0b0000, 0b0001, 0b000>;
def : RWSysReg<"TRCPROCSELR",        0b10, 0b001, 0b0000, 0b0010, 0b000>;
def : RWSysReg<"TRCCONFIGR",         0b10, 0b001, 0b0000, 0b0100, 0b000>;
def : RWSysReg<"TRCAUXCTLR",         0b10, 0b001, 0b0000, 0b0110, 0b000>;
def : RWSysReg<"TRCEVENTCTL0R",      0b10, 0b001, 0b0000, 0b1000, 0b000>;
def : RWSysReg<"TRCEVENTCTL1R",      0b10, 0b001, 0b0000, 0b1001, 0b000>;
def : RWSysReg<"TRCSTALLCTLR",       0b10, 0b001, 0b0000, 0b1011, 0b000>;
def : RWSysReg<"TRCTSCTLR",          0b10, 0b001, 0b0000, 0b1100, 0b000>;
def : RWSysReg<"TRCSYNCPR",          0b10, 0b001, 0b0000, 0b1101, 0b000>;
def : RWSysReg<"TRCCCCTLR",          0b10, 0b001, 0b0000, 0b1110, 0b000>;
def : RWSysReg<"TRCBBCTLR",          0b10, 0b001, 0b0000, 0b1111, 0b000>;
def : RWSysReg<"TRCTRACEIDR",        0b10, 0b001, 0b0000, 0b0000, 0b001>;
def : RWSysReg<"TRCQCTLR",           0b10, 0b001, 0b0000, 0b0001, 0b001>;
def : RWSysReg<"TRCVICTLR",          0b10, 0b001, 0b0000, 0b0000, 0b010>;
def : RWSysReg<"TRCVIIECTLR",        0b10, 0b001, 0b0000, 0b0001, 0b010>;
def : RWSysReg<"TRCVISSCTLR",        0b10, 0b001, 0b0000, 0b0010, 0b010>;
def : RWSysReg<"TRCVIPCSSCTLR",      0b10, 0b001, 0b0000, 0b0011, 0b010>;
def : RWSysReg<"TRCVDCTLR",          0b10, 0b001, 0b0000, 0b1000, 0b010>;
def : RWSysReg<"TRCVDSACCTLR",       0b10, 0b001, 0b0000, 0b1001, 0b010>;
def : RWSysReg<"TRCVDARCCTLR",       0b10, 0b001, 0b0000, 0b1010, 0b010>;
def : RWSysReg<"TRCSEQEVR0",         0b10, 0b001, 0b0000, 0b0000, 0b100>;
def : RWSysReg<"TRCSEQEVR1",         0b10, 0b001, 0b0000, 0b0001, 0b100>;
def : RWSysReg<"TRCSEQEVR2",         0b10, 0b001, 0b0000, 0b0010, 0b100>;
def : RWSysReg<"TRCSEQRSTEVR",       0b10, 0b001, 0b0000, 0b0110, 0b100>;
def : RWSysReg<"TRCSEQSTR",          0b10, 0b001, 0b0000, 0b0111, 0b100>;
def : RWSysReg<"TRCEXTINSELR",       0b10, 0b001, 0b0000, 0b1000, 0b100>;
def : RWSysReg<"TRCCNTRLDVR0",       0b10, 0b001, 0b0000, 0b0000, 0b101>;
def : RWSysReg<"TRCCNTRLDVR1",       0b10, 0b001, 0b0000, 0b0001, 0b101>;
def : RWSysReg<"TRCCNTRLDVR2",       0b10, 0b001, 0b0000, 0b0010, 0b101>;
def : RWSysReg<"TRCCNTRLDVR3",       0b10, 0b001, 0b0000, 0b0011, 0b101>;
def : RWSysReg<"TRCCNTCTLR0",        0b10, 0b001, 0b0000, 0b0100, 0b101>;
def : RWSysReg<"TRCCNTCTLR1",        0b10, 0b001, 0b0000, 0b0101, 0b101>;
def : RWSysReg<"TRCCNTCTLR2",        0b10, 0b001, 0b0000, 0b0110, 0b101>;
def : RWSysReg<"TRCCNTCTLR3",        0b10, 0b001, 0b0000, 0b0111, 0b101>;
def : RWSysReg<"TRCCNTVR0",          0b10, 0b001, 0b0000, 0b1000, 0b101>;
def : RWSysReg<"TRCCNTVR1",          0b10, 0b001, 0b0000, 0b1001, 0b101>;
def : RWSysReg<"TRCCNTVR2",          0b10, 0b001, 0b0000, 0b1010, 0b101>;
def : RWSysReg<"TRCCNTVR3",          0b10, 0b001, 0b0000, 0b1011, 0b101>;
def : RWSysReg<"TRCIMSPEC0",         0b10, 0b001, 0b0000, 0b0000, 0b111>;
def : RWSysReg<"TRCIMSPEC1",         0b10, 0b001, 0b0000, 0b0001, 0b111>;
def : RWSysReg<"TRCIMSPEC2",         0b10, 0b001, 0b0000, 0b0010, 0b111>;
def : RWSysReg<"TRCIMSPEC3",         0b10, 0b001, 0b0000, 0b0011, 0b111>;
def : RWSysReg<"TRCIMSPEC4",         0b10, 0b001, 0b0000, 0b0100, 0b111>;
def : RWSysReg<"TRCIMSPEC5",         0b10, 0b001, 0b0000, 0b0101, 0b111>;
def : RWSysReg<"TRCIMSPEC6",         0b10, 0b001, 0b0000, 0b0110, 0b111>;
def : RWSysReg<"TRCIMSPEC7",         0b10, 0b001, 0b0000, 0b0111, 0b111>;
def : RWSysReg<"TRCRSCTLR2",         0b10, 0b001, 0b0001, 0b0010, 0b000>;
def : RWSysReg<"TRCRSCTLR3",         0b10, 0b001, 0b0001, 0b0011, 0b000>;
def : RWSysReg<"TRCRSCTLR4",         0b10, 0b001, 0b0001, 0b0100, 0b000>;
def : RWSysReg<"TRCRSCTLR5",         0b10, 0b001, 0b0001, 0b0101, 0b000>;
def : RWSysReg<"TRCRSCTLR6",         0b10, 0b001, 0b0001, 0b0110, 0b000>;
def : RWSysReg<"TRCRSCTLR7",         0b10, 0b001, 0b0001, 0b0111, 0b000>;
def : RWSysReg<"TRCRSCTLR8",         0b10, 0b001, 0b0001, 0b1000, 0b000>;
def : RWSysReg<"TRCRSCTLR9",         0b10, 0b001, 0b0001, 0b1001, 0b000>;
def : RWSysReg<"TRCRSCTLR10",        0b10, 0b001, 0b0001, 0b1010, 0b000>;
def : RWSysReg<"TRCRSCTLR11",        0b10, 0b001, 0b0001, 0b1011, 0b000>;
def : RWSysReg<"TRCRSCTLR12",        0b10, 0b001, 0b0001, 0b1100, 0b000>;
def : RWSysReg<"TRCRSCTLR13",        0b10, 0b001, 0b0001, 0b1101, 0b000>;
def : RWSysReg<"TRCRSCTLR14",        0b10, 0b001, 0b0001, 0b1110, 0b000>;
def : RWSysReg<"TRCRSCTLR15",        0b10, 0b001, 0b0001, 0b1111, 0b000>;
def : RWSysReg<"TRCRSCTLR16",        0b10, 0b001, 0b0001, 0b0000, 0b001>;
def : RWSysReg<"TRCRSCTLR17",        0b10, 0b001, 0b0001, 0b0001, 0b001>;
def : RWSysReg<"TRCRSCTLR18",        0b10, 0b001, 0b0001, 0b0010, 0b001>;
def : RWSysReg<"TRCRSCTLR19",        0b10, 0b001, 0b0001, 0b0011, 0b001>;
def : RWSysReg<"TRCRSCTLR20",        0b10, 0b001, 0b0001, 0b0100, 0b001>;
def : RWSysReg<"TRCRSCTLR21",        0b10, 0b001, 0b0001, 0b0101, 0b001>;
def : RWSysReg<"TRCRSCTLR22",        0b10, 0b001, 0b0001, 0b0110, 0b001>;
def : RWSysReg<"TRCRSCTLR23",        0b10, 0b001, 0b0001, 0b0111, 0b001>;
def : RWSysReg<"TRCRSCTLR24",        0b10, 0b001, 0b0001, 0b1000, 0b001>;
def : RWSysReg<"TRCRSCTLR25",        0b10, 0b001, 0b0001, 0b1001, 0b001>;
def : RWSysReg<"TRCRSCTLR26",        0b10, 0b001, 0b0001, 0b1010, 0b001>;
def : RWSysReg<"TRCRSCTLR27",        0b10, 0b001, 0b0001, 0b1011, 0b001>;
def : RWSysReg<"TRCRSCTLR28",        0b10, 0b001, 0b0001, 0b1100, 0b001>;
def : RWSysReg<"TRCRSCTLR29",        0b10, 0b001, 0b0001, 0b1101, 0b001>;
def : RWSysReg<"TRCRSCTLR30",        0b10, 0b001, 0b0001, 0b1110, 0b001>;
def : RWSysReg<"TRCRSCTLR31",        0b10, 0b001, 0b0001, 0b1111, 0b001>;
def : RWSysReg<"TRCSSCCR0",          0b10, 0b001, 0b0001, 0b0000, 0b010>;
def : RWSysReg<"TRCSSCCR1",          0b10, 0b001, 0b0001, 0b0001, 0b010>;
def : RWSysReg<"TRCSSCCR2",          0b10, 0b001, 0b0001, 0b0010, 0b010>;
def : RWSysReg<"TRCSSCCR3",          0b10, 0b001, 0b0001, 0b0011, 0b010>;
def : RWSysReg<"TRCSSCCR4",          0b10, 0b001, 0b0001, 0b0100, 0b010>;
def : RWSysReg<"TRCSSCCR5",          0b10, 0b001, 0b0001, 0b0101, 0b010>;
def : RWSysReg<"TRCSSCCR6",          0b10, 0b001, 0b0001, 0b0110, 0b010>;
def : RWSysReg<"TRCSSCCR7",          0b10, 0b001, 0b0001, 0b0111, 0b010>;
def : RWSysReg<"TRCSSCSR0",          0b10, 0b001, 0b0001, 0b1000, 0b010>;
def : RWSysReg<"TRCSSCSR1",          0b10, 0b001, 0b0001, 0b1001, 0b010>;
def : RWSysReg<"TRCSSCSR2",          0b10, 0b001, 0b0001, 0b1010, 0b010>;
def : RWSysReg<"TRCSSCSR3",          0b10, 0b001, 0b0001, 0b1011, 0b010>;
def : RWSysReg<"TRCSSCSR4",          0b10, 0b001, 0b0001, 0b1100, 0b010>;
def : RWSysReg<"TRCSSCSR5",          0b10, 0b001, 0b0001, 0b1101, 0b010>;
def : RWSysReg<"TRCSSCSR6",          0b10, 0b001, 0b0001, 0b1110, 0b010>;
def : RWSysReg<"TRCSSCSR7",          0b10, 0b001, 0b0001, 0b1111, 0b010>;
def : RWSysReg<"TRCSSPCICR0",        0b10, 0b001, 0b0001, 0b0000, 0b011>;
def : RWSysReg<"TRCSSPCICR1",        0b10, 0b001, 0b0001, 0b0001, 0b011>;
def : RWSysReg<"TRCSSPCICR2",        0b10, 0b001, 0b0001, 0b0010, 0b011>;
def : RWSysReg<"TRCSSPCICR3",        0b10, 0b001, 0b0001, 0b0011, 0b011>;
def : RWSysReg<"TRCSSPCICR4",        0b10, 0b001, 0b0001, 0b0100, 0b011>;
def : RWSysReg<"TRCSSPCICR5",        0b10, 0b001, 0b0001, 0b0101, 0b011>;
def : RWSysReg<"TRCSSPCICR6",        0b10, 0b001, 0b0001, 0b0110, 0b011>;
def : RWSysReg<"TRCSSPCICR7",        0b10, 0b001, 0b0001, 0b0111, 0b011>;
def : RWSysReg<"TRCPDCR",            0b10, 0b001, 0b0001, 0b0100, 0b100>;
def : RWSysReg<"TRCACVR0",           0b10, 0b001, 0b0010, 0b0000, 0b000>;
def : RWSysReg<"TRCACVR1",           0b10, 0b001, 0b0010, 0b0010, 0b000>;
def : RWSysReg<"TRCACVR2",           0b10, 0b001, 0b0010, 0b0100, 0b000>;
def : RWSysReg<"TRCACVR3",           0b10, 0b001, 0b0010, 0b0110, 0b000>;
def : RWSysReg<"TRCACVR4",           0b10, 0b001, 0b0010, 0b1000, 0b000>;
def : RWSysReg<"TRCACVR5",           0b10, 0b001, 0b0010, 0b1010, 0b000>;
def : RWSysReg<"TRCACVR6",           0b10, 0b001, 0b0010, 0b1100, 0b000>;
def : RWSysReg<"TRCACVR7",           0b10, 0b001, 0b0010, 0b1110, 0b000>;
def : RWSysReg<"TRCACVR8",           0b10, 0b001, 0b0010, 0b0000, 0b001>;
def : RWSysReg<"TRCACVR9",           0b10, 0b001, 0b0010, 0b0010, 0b001>;
def : RWSysReg<"TRCACVR10",          0b10, 0b001, 0b0010, 0b0100, 0b001>;
def : RWSysReg<"TRCACVR11",          0b10, 0b001, 0b0010, 0b0110, 0b001>;
def : RWSysReg<"TRCACVR12",          0b10, 0b001, 0b0010, 0b1000, 0b001>;
def : RWSysReg<"TRCACVR13",          0b10, 0b001, 0b0010, 0b1010, 0b001>;
def : RWSysReg<"TRCACVR14",          0b10, 0b001, 0b0010, 0b1100, 0b001>;
def : RWSysReg<"TRCACVR15",          0b10, 0b001, 0b0010, 0b1110, 0b001>;
def : RWSysReg<"TRCACATR0",          0b10, 0b001, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"TRCACATR1",          0b10, 0b001, 0b0010, 0b0010, 0b010>;
def : RWSysReg<"TRCACATR2",          0b10, 0b001, 0b0010, 0b0100, 0b010>;
def : RWSysReg<"TRCACATR3",          0b10, 0b001, 0b0010, 0b0110, 0b010>;
def : RWSysReg<"TRCACATR4",          0b10, 0b001, 0b0010, 0b1000, 0b010>;
def : RWSysReg<"TRCACATR5",          0b10, 0b001, 0b0010, 0b1010, 0b010>;
def : RWSysReg<"TRCACATR6",          0b10, 0b001, 0b0010, 0b1100, 0b010>;
def : RWSysReg<"TRCACATR7",          0b10, 0b001, 0b0010, 0b1110, 0b010>;
def : RWSysReg<"TRCACATR8",          0b10, 0b001, 0b0010, 0b0000, 0b011>;
def : RWSysReg<"TRCACATR9",          0b10, 0b001, 0b0010, 0b0010, 0b011>;
def : RWSysReg<"TRCACATR10",         0b10, 0b001, 0b0010, 0b0100, 0b011>;
def : RWSysReg<"TRCACATR11",         0b10, 0b001, 0b0010, 0b0110, 0b011>;
def : RWSysReg<"TRCACATR12",         0b10, 0b001, 0b0010, 0b1000, 0b011>;
def : RWSysReg<"TRCACATR13",         0b10, 0b001, 0b0010, 0b1010, 0b011>;
def : RWSysReg<"TRCACATR14",         0b10, 0b001, 0b0010, 0b1100, 0b011>;
def : RWSysReg<"TRCACATR15",         0b10, 0b001, 0b0010, 0b1110, 0b011>;
def : RWSysReg<"TRCDVCVR0",          0b10, 0b001, 0b0010, 0b0000, 0b100>;
def : RWSysReg<"TRCDVCVR1",          0b10, 0b001, 0b0010, 0b0100, 0b100>;
def : RWSysReg<"TRCDVCVR2",          0b10, 0b001, 0b0010, 0b1000, 0b100>;
def : RWSysReg<"TRCDVCVR3",          0b10, 0b001, 0b0010, 0b1100, 0b100>;
def : RWSysReg<"TRCDVCVR4",          0b10, 0b001, 0b0010, 0b0000, 0b101>;
def : RWSysReg<"TRCDVCVR5",          0b10, 0b001, 0b0010, 0b0100, 0b101>;
def : RWSysReg<"TRCDVCVR6",          0b10, 0b001, 0b0010, 0b1000, 0b101>;
def : RWSysReg<"TRCDVCVR7",          0b10, 0b001, 0b0010, 0b1100, 0b101>;
def : RWSysReg<"TRCDVCMR0",          0b10, 0b001, 0b0010, 0b0000, 0b110>;
def : RWSysReg<"TRCDVCMR1",          0b10, 0b001, 0b0010, 0b0100, 0b110>;
def : RWSysReg<"TRCDVCMR2",          0b10, 0b001, 0b0010, 0b1000, 0b110>;
def : RWSysReg<"TRCDVCMR3",          0b10, 0b001, 0b0010, 0b1100, 0b110>;
def : RWSysReg<"TRCDVCMR4",          0b10, 0b001, 0b0010, 0b0000, 0b111>;
def : RWSysReg<"TRCDVCMR5",          0b10, 0b001, 0b0010, 0b0100, 0b111>;
def : RWSysReg<"TRCDVCMR6",          0b10, 0b001, 0b0010, 0b1000, 0b111>;
def : RWSysReg<"TRCDVCMR7",          0b10, 0b001, 0b0010, 0b1100, 0b111>;
def : RWSysReg<"TRCCIDCVR0",         0b10, 0b001, 0b0011, 0b0000, 0b000>;
def : RWSysReg<"TRCCIDCVR1",         0b10, 0b001, 0b0011, 0b0010, 0b000>;
def : RWSysReg<"TRCCIDCVR2",         0b10, 0b001, 0b0011, 0b0100, 0b000>;
def : RWSysReg<"TRCCIDCVR3",         0b10, 0b001, 0b0011, 0b0110, 0b000>;
def : RWSysReg<"TRCCIDCVR4",         0b10, 0b001, 0b0011, 0b1000, 0b000>;
def : RWSysReg<"TRCCIDCVR5",         0b10, 0b001, 0b0011, 0b1010, 0b000>;
def : RWSysReg<"TRCCIDCVR6",         0b10, 0b001, 0b0011, 0b1100, 0b000>;
def : RWSysReg<"TRCCIDCVR7",         0b10, 0b001, 0b0011, 0b1110, 0b000>;
def : RWSysReg<"TRCVMIDCVR0",        0b10, 0b001, 0b0011, 0b0000, 0b001>;
def : RWSysReg<"TRCVMIDCVR1",        0b10, 0b001, 0b0011, 0b0010, 0b001>;
def : RWSysReg<"TRCVMIDCVR2",        0b10, 0b001, 0b0011, 0b0100, 0b001>;
def : RWSysReg<"TRCVMIDCVR3",        0b10, 0b001, 0b0011, 0b0110, 0b001>;
def : RWSysReg<"TRCVMIDCVR4",        0b10, 0b001, 0b0011, 0b1000, 0b001>;
def : RWSysReg<"TRCVMIDCVR5",        0b10, 0b001, 0b0011, 0b1010, 0b001>;
def : RWSysReg<"TRCVMIDCVR6",        0b10, 0b001, 0b0011, 0b1100, 0b001>;
def : RWSysReg<"TRCVMIDCVR7",        0b10, 0b001, 0b0011, 0b1110, 0b001>;
def : RWSysReg<"TRCCIDCCTLR0",       0b10, 0b001, 0b0011, 0b0000, 0b010>;
def : RWSysReg<"TRCCIDCCTLR1",       0b10, 0b001, 0b0011, 0b0001, 0b010>;
def : RWSysReg<"TRCVMIDCCTLR0",      0b10, 0b001, 0b0011, 0b0010, 0b010>;
def : RWSysReg<"TRCVMIDCCTLR1",      0b10, 0b001, 0b0011, 0b0011, 0b010>;
def : RWSysReg<"TRCITCTRL",          0b10, 0b001, 0b0111, 0b0000, 0b100>;
def : RWSysReg<"TRCCLAIMSET",        0b10, 0b001, 0b0111, 0b1000, 0b110>;
def : RWSysReg<"TRCCLAIMCLR",        0b10, 0b001, 0b0111, 0b1001, 0b110>;

// GICv3 registers
//                                 Op0    Op1     CRn     CRm    Op2
def : RWSysReg<"ICC_BPR1_EL1",       0b11, 0b000, 0b1100, 0b1100, 0b011>;
def : RWSysReg<"ICC_BPR0_EL1",       0b11, 0b000, 0b1100, 0b1000, 0b011>;
def : RWSysReg<"ICC_PMR_EL1",        0b11, 0b000, 0b0100, 0b0110, 0b000>;
def : RWSysReg<"ICC_CTLR_EL1",       0b11, 0b000, 0b1100, 0b1100, 0b100>;
def : RWSysReg<"ICC_CTLR_EL3",       0b11, 0b110, 0b1100, 0b1100, 0b100>;
def : RWSysReg<"ICC_SRE_EL1",        0b11, 0b000, 0b1100, 0b1100, 0b101>;
def : RWSysReg<"ICC_SRE_EL2",        0b11, 0b100, 0b1100, 0b1001, 0b101>;
def : RWSysReg<"ICC_SRE_EL3",        0b11, 0b110, 0b1100, 0b1100, 0b101>;
def : RWSysReg<"ICC_IGRPEN0_EL1",    0b11, 0b000, 0b1100, 0b1100, 0b110>;
def : RWSysReg<"ICC_IGRPEN1_EL1",    0b11, 0b000, 0b1100, 0b1100, 0b111>;
def : RWSysReg<"ICC_IGRPEN1_EL3",    0b11, 0b110, 0b1100, 0b1100, 0b111>;
def : RWSysReg<"ICC_AP0R0_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b100>;
def : RWSysReg<"ICC_AP0R1_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b101>;
def : RWSysReg<"ICC_AP0R2_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b110>;
def : RWSysReg<"ICC_AP0R3_EL1",      0b11, 0b000, 0b1100, 0b1000, 0b111>;
def : RWSysReg<"ICC_AP1R0_EL1",      0b11, 0b000, 0b1100, 0b1001, 0b000>;
def : RWSysReg<"ICC_AP1R1_EL1",      0b11, 0b000, 0b1100, 0b1001, 0b001>;
def : RWSysReg<"ICC_AP1R2_EL1",      0b11, 0b000, 0b1100, 0b1001, 0b010>;
def : RWSysReg<"ICC_AP1R3_EL1",      0b11, 0b000, 0b1100, 0b1001, 0b011>;
def : RWSysReg<"ICH_AP0R0_EL2",      0b11, 0b100, 0b1100, 0b1000, 0b000>;
def : RWSysReg<"ICH_AP0R1_EL2",      0b11, 0b100, 0b1100, 0b1000, 0b001>;
def : RWSysReg<"ICH_AP0R2_EL2",      0b11, 0b100, 0b1100, 0b1000, 0b010>;
def : RWSysReg<"ICH_AP0R3_EL2",      0b11, 0b100, 0b1100, 0b1000, 0b011>;
def : RWSysReg<"ICH_AP1R0_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b000>;
def : RWSysReg<"ICH_AP1R1_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b001>;
def : RWSysReg<"ICH_AP1R2_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b010>;
def : RWSysReg<"ICH_AP1R3_EL2",      0b11, 0b100, 0b1100, 0b1001, 0b011>;
def : RWSysReg<"ICH_HCR_EL2",        0b11, 0b100, 0b1100, 0b1011, 0b000>;
def : ROSysReg<"ICH_MISR_EL2",       0b11, 0b100, 0b1100, 0b1011, 0b010>;
def : RWSysReg<"ICH_VMCR_EL2",       0b11, 0b100, 0b1100, 0b1011, 0b111>;
def : RWSysReg<"ICH_LR0_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b000>;
def : RWSysReg<"ICH_LR1_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b001>;
def : RWSysReg<"ICH_LR2_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b010>;
def : RWSysReg<"ICH_LR3_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b011>;
def : RWSysReg<"ICH_LR4_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b100>;
def : RWSysReg<"ICH_LR5_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b101>;
def : RWSysReg<"ICH_LR6_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b110>;
def : RWSysReg<"ICH_LR7_EL2",        0b11, 0b100, 0b1100, 0b1100, 0b111>;
def : RWSysReg<"ICH_LR8_EL2",        0b11, 0b100, 0b1100, 0b1101, 0b000>;
def : RWSysReg<"ICH_LR9_EL2",        0b11, 0b100, 0b1100, 0b1101, 0b001>;
def : RWSysReg<"ICH_LR10_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b010>;
def : RWSysReg<"ICH_LR11_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b011>;
def : RWSysReg<"ICH_LR12_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b100>;
def : RWSysReg<"ICH_LR13_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b101>;
def : RWSysReg<"ICH_LR14_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b110>;
def : RWSysReg<"ICH_LR15_EL2",       0b11, 0b100, 0b1100, 0b1101, 0b111>;

// v8r system registers
let Requires = [{ {AArch64::HasV8_0rOps} }] in {
//Virtualization System Control Register
//                                 Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"VSCTLR_EL2",       0b11, 0b100, 0b0010, 0b0000, 0b000> {
  let AltName = "TTBR0_EL2";
}

//MPU Type Register
//                                 Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"MPUIR_EL1",        0b11, 0b000, 0b0000, 0b0000, 0b100>;
def : RWSysReg<"MPUIR_EL2",        0b11, 0b100, 0b0000, 0b0000, 0b100>;

//Protection Region Enable Register
//                                 Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PRENR_EL1",        0b11, 0b000, 0b0110, 0b0001, 0b001>;
def : RWSysReg<"PRENR_EL2",        0b11, 0b100, 0b0110, 0b0001, 0b001>;

//Protection Region Selection Register
//                                 Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PRSELR_EL1",       0b11, 0b000, 0b0110, 0b0010, 0b001>;
def : RWSysReg<"PRSELR_EL2",       0b11, 0b100, 0b0110, 0b0010, 0b001>;

//Protection Region Base Address Register
//                                 Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PRBAR_EL1",        0b11, 0b000, 0b0110, 0b1000, 0b000>;
def : RWSysReg<"PRBAR_EL2",        0b11, 0b100, 0b0110, 0b1000, 0b000>;

//Protection Region Limit Address Register
//                                 Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PRLAR_EL1",        0b11, 0b000, 0b0110, 0b1000, 0b001>;
def : RWSysReg<"PRLAR_EL2",        0b11, 0b100, 0b0110, 0b1000, 0b001>;

foreach n = 1-15 in {
foreach x = 1-2 in {
//Direct acces to Protection Region Base Address Register for n th MPU region
  def : RWSysReg<!strconcat("PRBAR"#n, "_EL"#x),
    0b11, 0b000, 0b0110, 0b1000, 0b000>{
    let Encoding{5-2} = n;
    let Encoding{13} = !add(x,-1);
  }

  def : RWSysReg<!strconcat("PRLAR"#n, "_EL"#x),
    0b11, 0b000, 0b0110, 0b1000, 0b001>{
    let Encoding{5-2} = n;
    let Encoding{13} = !add(x,-1);
  }
} //foreach x = 1-2 in
} //foreach n = 1-15 in
} //let Requires = [{ {AArch64::HasV8_0rOps} }] in

// v8.1a "Privileged Access Never" extension-specific system registers
let Requires = [{ {AArch64::FeaturePAN} }] in
def : RWSysReg<"PAN", 0b11, 0b000, 0b0100, 0b0010, 0b011>;

// v8.1a "Limited Ordering Regions" extension-specific system registers
//                         Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureLOR} }] in {
def : RWSysReg<"LORSA_EL1",  0b11, 0b000, 0b1010, 0b0100, 0b000>;
def : RWSysReg<"LOREA_EL1",  0b11, 0b000, 0b1010, 0b0100, 0b001>;
def : RWSysReg<"LORN_EL1",   0b11, 0b000, 0b1010, 0b0100, 0b010>;
def : RWSysReg<"LORC_EL1",   0b11, 0b000, 0b1010, 0b0100, 0b011>;
}

// v8.1a "Virtualization Host extensions" system registers
//                              Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureVH} }] in {
def : RWSysReg<"TTBR1_EL2",       0b11, 0b100, 0b0010, 0b0000, 0b001>;
def : RWSysReg<"CNTHV_TVAL_EL2",  0b11, 0b100, 0b1110, 0b0011, 0b000>;
def : RWSysReg<"CNTHV_CVAL_EL2",  0b11, 0b100, 0b1110, 0b0011, 0b010>;
def : RWSysReg<"CNTHV_CTL_EL2",   0b11, 0b100, 0b1110, 0b0011, 0b001>;
def : RWSysReg<"SCTLR_EL12",      0b11, 0b101, 0b0001, 0b0000, 0b000>;
def : RWSysReg<"CPACR_EL12",      0b11, 0b101, 0b0001, 0b0000, 0b010>;
def : RWSysReg<"TTBR0_EL12",      0b11, 0b101, 0b0010, 0b0000, 0b000>;
def : RWSysReg<"TTBR1_EL12",      0b11, 0b101, 0b0010, 0b0000, 0b001>;
def : RWSysReg<"TCR_EL12",        0b11, 0b101, 0b0010, 0b0000, 0b010>;
def : RWSysReg<"AFSR0_EL12",      0b11, 0b101, 0b0101, 0b0001, 0b000>;
def : RWSysReg<"AFSR1_EL12",      0b11, 0b101, 0b0101, 0b0001, 0b001>;
def : RWSysReg<"ESR_EL12",        0b11, 0b101, 0b0101, 0b0010, 0b000>;
def : RWSysReg<"FAR_EL12",        0b11, 0b101, 0b0110, 0b0000, 0b000>;
def : RWSysReg<"MAIR_EL12",       0b11, 0b101, 0b1010, 0b0010, 0b000>;
def : RWSysReg<"AMAIR_EL12",      0b11, 0b101, 0b1010, 0b0011, 0b000>;
def : RWSysReg<"VBAR_EL12",       0b11, 0b101, 0b1100, 0b0000, 0b000>;
def : RWSysReg<"CONTEXTIDR_EL12", 0b11, 0b101, 0b1101, 0b0000, 0b001>;
def : RWSysReg<"CNTKCTL_EL12",    0b11, 0b101, 0b1110, 0b0001, 0b000>;
def : RWSysReg<"CNTP_TVAL_EL02",  0b11, 0b101, 0b1110, 0b0010, 0b000>;
def : RWSysReg<"CNTP_CTL_EL02",   0b11, 0b101, 0b1110, 0b0010, 0b001>;
def : RWSysReg<"CNTP_CVAL_EL02",  0b11, 0b101, 0b1110, 0b0010, 0b010>;
def : RWSysReg<"CNTV_TVAL_EL02",  0b11, 0b101, 0b1110, 0b0011, 0b000>;
def : RWSysReg<"CNTV_CTL_EL02",   0b11, 0b101, 0b1110, 0b0011, 0b001>;
def : RWSysReg<"CNTV_CVAL_EL02",  0b11, 0b101, 0b1110, 0b0011, 0b010>;
def : RWSysReg<"SPSR_EL12",       0b11, 0b101, 0b0100, 0b0000, 0b000>;
def : RWSysReg<"ELR_EL12",        0b11, 0b101, 0b0100, 0b0000, 0b001>;
let Requires = [{ {AArch64::FeatureCONTEXTIDREL2} }] in {
  def : RWSysReg<"CONTEXTIDR_EL2",  0b11, 0b100, 0b1101, 0b0000, 0b001>;
}
}
// v8.2a registers
//                  Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeaturePsUAO} }] in
def : RWSysReg<"UAO", 0b11, 0b000, 0b0100, 0b0010, 0b100>;

// v8.2a "Statistical Profiling extension" registers
//                            Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureSPE} }] in {
def : RWSysReg<"PMBLIMITR_EL1", 0b11, 0b000, 0b1001, 0b1010, 0b000>;
def : RWSysReg<"PMBPTR_EL1",    0b11, 0b000, 0b1001, 0b1010, 0b001>;
def : RWSysReg<"PMBSR_EL1",     0b11, 0b000, 0b1001, 0b1010, 0b011>;
def : ROSysReg<"PMBIDR_EL1",    0b11, 0b000, 0b1001, 0b1010, 0b111>;
def : RWSysReg<"PMSCR_EL2",     0b11, 0b100, 0b1001, 0b1001, 0b000>;
def : RWSysReg<"PMSCR_EL12",    0b11, 0b101, 0b1001, 0b1001, 0b000>;
def : RWSysReg<"PMSCR_EL1",     0b11, 0b000, 0b1001, 0b1001, 0b000>;
def : RWSysReg<"PMSICR_EL1",    0b11, 0b000, 0b1001, 0b1001, 0b010>;
def : RWSysReg<"PMSIRR_EL1",    0b11, 0b000, 0b1001, 0b1001, 0b011>;
def : RWSysReg<"PMSFCR_EL1",    0b11, 0b000, 0b1001, 0b1001, 0b100>;
def : RWSysReg<"PMSEVFR_EL1",   0b11, 0b000, 0b1001, 0b1001, 0b101>;
def : RWSysReg<"PMSLATFR_EL1",  0b11, 0b000, 0b1001, 0b1001, 0b110>;
def : ROSysReg<"PMSIDR_EL1",    0b11, 0b000, 0b1001, 0b1001, 0b111>;
}

// v8.2a "RAS extension" registers
//                         Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureRAS} }] in {
def : RWSysReg<"ERRSELR_EL1",   0b11, 0b000, 0b0101, 0b0011, 0b001>;
def : RWSysReg<"ERXCTLR_EL1",   0b11, 0b000, 0b0101, 0b0100, 0b001>;
def : RWSysReg<"ERXSTATUS_EL1", 0b11, 0b000, 0b0101, 0b0100, 0b010>;
def : RWSysReg<"ERXADDR_EL1",   0b11, 0b000, 0b0101, 0b0100, 0b011>;
def : RWSysReg<"ERXMISC0_EL1",  0b11, 0b000, 0b0101, 0b0101, 0b000>;
def : RWSysReg<"ERXMISC1_EL1",  0b11, 0b000, 0b0101, 0b0101, 0b001>;
def : RWSysReg<"DISR_EL1",      0b11, 0b000, 0b1100, 0b0001, 0b001>;
def : RWSysReg<"VDISR_EL2",     0b11, 0b100, 0b1100, 0b0001, 0b001>;
def : RWSysReg<"VSESR_EL2",     0b11, 0b100, 0b0101, 0b0010, 0b011>;
}

// v8.3a "Pointer authentication extension" registers
//                              Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeaturePAuth} }] in {
def : RWSysReg<"APIAKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b000>;
def : RWSysReg<"APIAKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b001>;
def : RWSysReg<"APIBKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b010>;
def : RWSysReg<"APIBKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0001, 0b011>;
def : RWSysReg<"APDAKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b000>;
def : RWSysReg<"APDAKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b001>;
def : RWSysReg<"APDBKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b010>;
def : RWSysReg<"APDBKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0010, 0b011>;
def : RWSysReg<"APGAKeyLo_EL1", 0b11, 0b000, 0b0010, 0b0011, 0b000>;
def : RWSysReg<"APGAKeyHi_EL1", 0b11, 0b000, 0b0010, 0b0011, 0b001>;
}

// v8.4 "Secure Exception Level 2 extension"
let Requires = [{ {AArch64::FeatureSEL2} }] in {
// v8.4a "Virtualization secure second stage translation" registers
//                           Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"VSTCR_EL2" , 0b11, 0b100, 0b0010, 0b0110, 0b010>;
def : RWSysReg<"VSTTBR_EL2", 0b11, 0b100, 0b0010, 0b0110, 0b000> {
  let Requires = [{ {AArch64::HasV8_0aOps} }];
}

// v8.4a "Virtualization timer" registers
//                                Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"CNTHVS_TVAL_EL2", 0b11, 0b100, 0b1110, 0b0100, 0b000>;
def : RWSysReg<"CNTHVS_CVAL_EL2", 0b11, 0b100, 0b1110, 0b0100, 0b010>;
def : RWSysReg<"CNTHVS_CTL_EL2",  0b11, 0b100, 0b1110, 0b0100, 0b001>;
def : RWSysReg<"CNTHPS_TVAL_EL2", 0b11, 0b100, 0b1110, 0b0101, 0b000>;
def : RWSysReg<"CNTHPS_CVAL_EL2", 0b11, 0b100, 0b1110, 0b0101, 0b010>;
def : RWSysReg<"CNTHPS_CTL_EL2",  0b11, 0b100, 0b1110, 0b0101, 0b001>;

// v8.4a "Virtualization debug state" registers
//                           Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"SDER32_EL2", 0b11, 0b100, 0b0001, 0b0011, 0b001>;
} // FeatureSEL2

// v8.4a RAS registers
//                              Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"ERXPFGCTL_EL1", 0b11, 0b000, 0b0101, 0b0100, 0b101>;
def : RWSysReg<"ERXPFGCDN_EL1", 0b11, 0b000, 0b0101, 0b0100, 0b110>;
def : RWSysReg<"ERXMISC2_EL1",  0b11, 0b000, 0b0101, 0b0101, 0b010>;
def : RWSysReg<"ERXMISC3_EL1",  0b11, 0b000, 0b0101, 0b0101, 0b011>;
def : ROSysReg<"ERXPFGF_EL1",   0b11, 0b000, 0b0101, 0b0100, 0b100>;

// v8.4a MPAM registers
//                             Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureMPAM} }] in {
def : RWSysReg<"MPAM0_EL1",    0b11, 0b000, 0b1010, 0b0101, 0b001>;
def : RWSysReg<"MPAM1_EL1",    0b11, 0b000, 0b1010, 0b0101, 0b000>;
def : RWSysReg<"MPAM2_EL2",    0b11, 0b100, 0b1010, 0b0101, 0b000>;
def : RWSysReg<"MPAM3_EL3",    0b11, 0b110, 0b1010, 0b0101, 0b000>;
def : RWSysReg<"MPAM1_EL12",   0b11, 0b101, 0b1010, 0b0101, 0b000>;
def : RWSysReg<"MPAMHCR_EL2",  0b11, 0b100, 0b1010, 0b0100, 0b000>;
def : RWSysReg<"MPAMVPMV_EL2", 0b11, 0b100, 0b1010, 0b0100, 0b001>;
def : RWSysReg<"MPAMVPM0_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b000>;
def : RWSysReg<"MPAMVPM1_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b001>;
def : RWSysReg<"MPAMVPM2_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b010>;
def : RWSysReg<"MPAMVPM3_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b011>;
def : RWSysReg<"MPAMVPM4_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b100>;
def : RWSysReg<"MPAMVPM5_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b101>;
def : RWSysReg<"MPAMVPM6_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b110>;
def : RWSysReg<"MPAMVPM7_EL2", 0b11, 0b100, 0b1010, 0b0110, 0b111>;
def : ROSysReg<"MPAMIDR_EL1",  0b11, 0b000, 0b1010, 0b0100, 0b100>;
} //FeatureMPAM

// v8.4a Activity Monitor registers
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureAM} }] in {
def : RWSysReg<"AMCR_EL0",         0b11, 0b011, 0b1101, 0b0010, 0b000>;
def : ROSysReg<"AMCFGR_EL0",       0b11, 0b011, 0b1101, 0b0010, 0b001>;
def : ROSysReg<"AMCGCR_EL0",       0b11, 0b011, 0b1101, 0b0010, 0b010>;
def : RWSysReg<"AMUSERENR_EL0",    0b11, 0b011, 0b1101, 0b0010, 0b011>;
def : RWSysReg<"AMCNTENCLR0_EL0",  0b11, 0b011, 0b1101, 0b0010, 0b100>;
def : RWSysReg<"AMCNTENSET0_EL0",  0b11, 0b011, 0b1101, 0b0010, 0b101>;
def : RWSysReg<"AMEVCNTR00_EL0",   0b11, 0b011, 0b1101, 0b0100, 0b000>;
def : RWSysReg<"AMEVCNTR01_EL0",   0b11, 0b011, 0b1101, 0b0100, 0b001>;
def : RWSysReg<"AMEVCNTR02_EL0",   0b11, 0b011, 0b1101, 0b0100, 0b010>;
def : RWSysReg<"AMEVCNTR03_EL0",   0b11, 0b011, 0b1101, 0b0100, 0b011>;
def : ROSysReg<"AMEVTYPER00_EL0",  0b11, 0b011, 0b1101, 0b0110, 0b000>;
def : ROSysReg<"AMEVTYPER01_EL0",  0b11, 0b011, 0b1101, 0b0110, 0b001>;
def : ROSysReg<"AMEVTYPER02_EL0",  0b11, 0b011, 0b1101, 0b0110, 0b010>;
def : ROSysReg<"AMEVTYPER03_EL0",  0b11, 0b011, 0b1101, 0b0110, 0b011>;
def : RWSysReg<"AMCNTENCLR1_EL0",  0b11, 0b011, 0b1101, 0b0011, 0b000>;
def : RWSysReg<"AMCNTENSET1_EL0",  0b11, 0b011, 0b1101, 0b0011, 0b001>;
def : RWSysReg<"AMEVCNTR10_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b000>;
def : RWSysReg<"AMEVCNTR11_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b001>;
def : RWSysReg<"AMEVCNTR12_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b010>;
def : RWSysReg<"AMEVCNTR13_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b011>;
def : RWSysReg<"AMEVCNTR14_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b100>;
def : RWSysReg<"AMEVCNTR15_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b101>;
def : RWSysReg<"AMEVCNTR16_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b110>;
def : RWSysReg<"AMEVCNTR17_EL0",   0b11, 0b011, 0b1101, 0b1100, 0b111>;
def : RWSysReg<"AMEVCNTR18_EL0",   0b11, 0b011, 0b1101, 0b1101, 0b000>;
def : RWSysReg<"AMEVCNTR19_EL0",   0b11, 0b011, 0b1101, 0b1101, 0b001>;
def : RWSysReg<"AMEVCNTR110_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b010>;
def : RWSysReg<"AMEVCNTR111_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b011>;
def : RWSysReg<"AMEVCNTR112_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b100>;
def : RWSysReg<"AMEVCNTR113_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b101>;
def : RWSysReg<"AMEVCNTR114_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b110>;
def : RWSysReg<"AMEVCNTR115_EL0",  0b11, 0b011, 0b1101, 0b1101, 0b111>;
def : RWSysReg<"AMEVTYPER10_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b000>;
def : RWSysReg<"AMEVTYPER11_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b001>;
def : RWSysReg<"AMEVTYPER12_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b010>;
def : RWSysReg<"AMEVTYPER13_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b011>;
def : RWSysReg<"AMEVTYPER14_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b100>;
def : RWSysReg<"AMEVTYPER15_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b101>;
def : RWSysReg<"AMEVTYPER16_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b110>;
def : RWSysReg<"AMEVTYPER17_EL0",  0b11, 0b011, 0b1101, 0b1110, 0b111>;
def : RWSysReg<"AMEVTYPER18_EL0",  0b11, 0b011, 0b1101, 0b1111, 0b000>;
def : RWSysReg<"AMEVTYPER19_EL0",  0b11, 0b011, 0b1101, 0b1111, 0b001>;
def : RWSysReg<"AMEVTYPER110_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b010>;
def : RWSysReg<"AMEVTYPER111_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b011>;
def : RWSysReg<"AMEVTYPER112_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b100>;
def : RWSysReg<"AMEVTYPER113_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b101>;
def : RWSysReg<"AMEVTYPER114_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b110>;
def : RWSysReg<"AMEVTYPER115_EL0", 0b11, 0b011, 0b1101, 0b1111, 0b111>;
} //FeatureAM

// v8.4a Trace Extension registers
//
// Please note that the 8.4 spec also defines these registers:
// TRCIDR1, ID_DFR0_EL1, ID_AA64DFR0_EL1, MDSCR_EL1, MDCR_EL2, and MDCR_EL3,
// but they are already defined above.
//
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureTRACEV8_4} }] in {
def : RWSysReg<"TRFCR_EL1",        0b11, 0b000, 0b0001, 0b0010, 0b001>;
def : RWSysReg<"TRFCR_EL2",        0b11, 0b100, 0b0001, 0b0010, 0b001>;
def : RWSysReg<"TRFCR_EL12",       0b11, 0b101, 0b0001, 0b0010, 0b001>;
} //FeatureTRACEV8_4

// v8.4a Timing insensitivity of data processing instructions
// DIT: Data Independent Timing instructions
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureDIT} }] in {
def : RWSysReg<"DIT",              0b11, 0b011, 0b0100, 0b0010, 0b101>;
} //FeatureDIT

// v8.4a Enhanced Support for Nested Virtualization
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureNV} }] in {
def : RWSysReg<"VNCR_EL2",         0b11, 0b100, 0b0010, 0b0010, 0b000>;
} //FeatureNV

// SVE control registers
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureSVE} }] in {
def : RWSysReg<"ZCR_EL1",          0b11, 0b000, 0b0001, 0b0010, 0b000>;
def : RWSysReg<"ZCR_EL2",          0b11, 0b100, 0b0001, 0b0010, 0b000>;
def : RWSysReg<"ZCR_EL3",          0b11, 0b110, 0b0001, 0b0010, 0b000>;
def : RWSysReg<"ZCR_EL12",         0b11, 0b101, 0b0001, 0b0010, 0b000>;
}

// V8.5a Spectre mitigation SSBS register
//                     Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureSSBS} }] in
def : RWSysReg<"SSBS", 0b11, 0b011, 0b0100, 0b0010, 0b110>;

// v8.5a Memory Tagging Extension
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureMTE} }] in {
def : RWSysReg<"TCO",              0b11, 0b011, 0b0100, 0b0010, 0b111>;
def : RWSysReg<"GCR_EL1",          0b11, 0b000, 0b0001, 0b0000, 0b110>;
def : RWSysReg<"RGSR_EL1",         0b11, 0b000, 0b0001, 0b0000, 0b101>;
def : RWSysReg<"TFSR_EL1",         0b11, 0b000, 0b0101, 0b0110, 0b000>;
def : RWSysReg<"TFSR_EL2",         0b11, 0b100, 0b0101, 0b0110, 0b000>;
def : RWSysReg<"TFSR_EL3",         0b11, 0b110, 0b0101, 0b0110, 0b000>;
def : RWSysReg<"TFSR_EL12",        0b11, 0b101, 0b0101, 0b0110, 0b000>;
def : RWSysReg<"TFSRE0_EL1",       0b11, 0b000, 0b0101, 0b0110, 0b001>;
def : ROSysReg<"GMID_EL1",         0b11, 0b001, 0b0000, 0b0000, 0b100>;
} // HasMTE

// Embedded Trace Extension R/W System registers
let Requires = [{ {AArch64::FeatureETE} }] in {
//              Name            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"TRCRSR",        0b10, 0b001, 0b0000, 0b1010, 0b000>;
//  TRCEXTINSELR0 has the same encoding as ETM TRCEXTINSELR
def : RWSysReg<"TRCEXTINSELR0", 0b10, 0b001, 0b0000, 0b1000, 0b100>;
def : RWSysReg<"TRCEXTINSELR1", 0b10, 0b001, 0b0000, 0b1001, 0b100>;
def : RWSysReg<"TRCEXTINSELR2", 0b10, 0b001, 0b0000, 0b1010, 0b100>;
def : RWSysReg<"TRCEXTINSELR3", 0b10, 0b001, 0b0000, 0b1011, 0b100>;
} // FeatureETE

// Trace Buffer Extension System registers
let Requires = [{ {AArch64::FeatureTRBE} }] in {
//                   Name       Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"TRBLIMITR_EL1", 0b11, 0b000, 0b1001, 0b1011, 0b000>;
def : RWSysReg<"TRBPTR_EL1",    0b11, 0b000, 0b1001, 0b1011, 0b001>;
def : RWSysReg<"TRBBASER_EL1",  0b11, 0b000, 0b1001, 0b1011, 0b010>;
def : RWSysReg<"TRBSR_EL1",     0b11, 0b000, 0b1001, 0b1011, 0b011>;
def : RWSysReg<"TRBMAR_EL1",    0b11, 0b000, 0b1001, 0b1011, 0b100>;
def : RWSysReg<"TRBTRG_EL1",    0b11, 0b000, 0b1001, 0b1011, 0b110>;
def : ROSysReg<"TRBIDR_EL1",    0b11, 0b000, 0b1001, 0b1011, 0b111>;
} // FeatureTRBE


// v8.6a Activity Monitors Virtualization Support
let Requires = [{ {AArch64::FeatureAMVS} }] in {
//              Name            Op0   Op1    CRn     CRm     Op2
def : ROSysReg<"AMCG1IDR_EL0",  0b11, 0b011, 0b1101, 0b0010, 0b110>;
foreach n = 0-15 in {
  foreach x = 0-1 in {
  def : RWSysReg<"AMEVCNTVOFF"#x#n#"_EL2",
    0b11, 0b100, 0b1101, 0b1000, 0b000>{
      let Encoding{4} = x;
      let Encoding{3-0} = n;
    }
  }
}
}

// v8.6a Fine Grained Virtualization Traps
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureFineGrainedTraps} }] in {
def : RWSysReg<"HFGRTR_EL2",       0b11, 0b100, 0b0001, 0b0001, 0b100>;
def : RWSysReg<"HFGWTR_EL2",       0b11, 0b100, 0b0001, 0b0001, 0b101>;
def : RWSysReg<"HFGITR_EL2",       0b11, 0b100, 0b0001, 0b0001, 0b110>;
def : RWSysReg<"HDFGRTR_EL2",      0b11, 0b100, 0b0011, 0b0001, 0b100>;
def : RWSysReg<"HDFGWTR_EL2",      0b11, 0b100, 0b0011, 0b0001, 0b101>;
def : RWSysReg<"HAFGRTR_EL2",      0b11, 0b100, 0b0011, 0b0001, 0b110>;

// v8.9a/v9.4a additions to Fine Grained Traps (FEAT_FGT2)
//                                 Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"HDFGRTR2_EL2",     0b11, 0b100, 0b0011, 0b0001, 0b000>;
def : RWSysReg<"HDFGWTR2_EL2",     0b11, 0b100, 0b0011, 0b0001, 0b001>;
def : RWSysReg<"HFGRTR2_EL2",      0b11, 0b100, 0b0011, 0b0001, 0b010>;
def : RWSysReg<"HFGWTR2_EL2",      0b11, 0b100, 0b0011, 0b0001, 0b011>;
def : RWSysReg<"HFGITR2_EL2",      0b11, 0b100, 0b0011, 0b0001, 0b111>;
}

// v8.6a Enhanced Counter Virtualization
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureEnhancedCounterVirtualization} }] in {
def : RWSysReg<"CNTSCALE_EL2",     0b11, 0b100, 0b1110, 0b0000, 0b100>;
def : RWSysReg<"CNTISCALE_EL2",    0b11, 0b100, 0b1110, 0b0000, 0b101>;
def : RWSysReg<"CNTPOFF_EL2",      0b11, 0b100, 0b1110, 0b0000, 0b110>;
def : RWSysReg<"CNTVFRQ_EL2",      0b11, 0b100, 0b1110, 0b0000, 0b111>;
def : ROSysReg<"CNTPCTSS_EL0",     0b11, 0b011, 0b1110, 0b0000, 0b101>;
def : ROSysReg<"CNTVCTSS_EL0",     0b11, 0b011, 0b1110, 0b0000, 0b110>;
}

// v8.7a LD64B/ST64B Accelerator Extension system register
let Requires = [{ {AArch64::FeatureLS64} }] in
def : RWSysReg<"ACCDATA_EL1",       0b11, 0b000, 0b1101, 0b0000, 0b101>;

// Branch Record Buffer system registers
let Requires = [{ {AArch64::FeatureBRBE} }] in {
def : RWSysReg<"BRBCR_EL1",         0b10, 0b001, 0b1001, 0b0000, 0b000>;
def : RWSysReg<"BRBCR_EL12",        0b10, 0b101, 0b1001, 0b0000, 0b000>;
def : RWSysReg<"BRBCR_EL2",         0b10, 0b100, 0b1001, 0b0000, 0b000>;
def : RWSysReg<"BRBFCR_EL1",        0b10, 0b001, 0b1001, 0b0000, 0b001>;
def : ROSysReg<"BRBIDR0_EL1",       0b10, 0b001, 0b1001, 0b0010, 0b000>;
def : RWSysReg<"BRBINFINJ_EL1",     0b10, 0b001, 0b1001, 0b0001, 0b000>;
def : RWSysReg<"BRBSRCINJ_EL1",     0b10, 0b001, 0b1001, 0b0001, 0b001>;
def : RWSysReg<"BRBTGTINJ_EL1",     0b10, 0b001, 0b1001, 0b0001, 0b010>;
def : RWSysReg<"BRBTS_EL1",         0b10, 0b001, 0b1001, 0b0000, 0b010>;
foreach n = 0-31 in {
  defvar nb = !cast<bits<5>>(n);
  def : ROSysReg<"BRBINF"#n#"_EL1", 0b10, 0b001, 0b1000, nb{3-0}, {nb{4},0b00}>;
  def : ROSysReg<"BRBSRC"#n#"_EL1", 0b10, 0b001, 0b1000, nb{3-0}, {nb{4},0b01}>;
  def : ROSysReg<"BRBTGT"#n#"_EL1", 0b10, 0b001, 0b1000, nb{3-0}, {nb{4},0b10}>;
}
}

// Statistical Profiling Extension system register
let Requires = [{ {AArch64::FeatureSPE_EEF} }] in
def : RWSysReg<"PMSNEVFR_EL1",      0b11, 0b000, 0b1001, 0b1001, 0b001>;

// Cyclone specific system registers
//                                 Op0    Op1     CRn     CRm    Op2
let Requires = [{ {AArch64::FeatureAppleA7SysReg} }] in
def : RWSysReg<"CPM_IOACC_CTL_EL3", 0b11, 0b111, 0b1111, 0b0010, 0b000>;

// Scalable Matrix Extension (SME)
//                                 Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureSME} }] in {
def : RWSysReg<"SMCR_EL1",         0b11, 0b000, 0b0001, 0b0010, 0b110>;
def : RWSysReg<"SMCR_EL2",         0b11, 0b100, 0b0001, 0b0010, 0b110>;
def : RWSysReg<"SMCR_EL3",         0b11, 0b110, 0b0001, 0b0010, 0b110>;
def : RWSysReg<"SMCR_EL12",        0b11, 0b101, 0b0001, 0b0010, 0b110>;
def : RWSysReg<"SVCR",             0b11, 0b011, 0b0100, 0b0010, 0b010>;
def : RWSysReg<"SMPRI_EL1",        0b11, 0b000, 0b0001, 0b0010, 0b100>;
def : RWSysReg<"SMPRIMAP_EL2",     0b11, 0b100, 0b0001, 0b0010, 0b101>;
def : ROSysReg<"SMIDR_EL1",        0b11, 0b001, 0b0000, 0b0000, 0b110>;
def : RWSysReg<"TPIDR2_EL0",       0b11, 0b011, 0b1101, 0b0000, 0b101>;
} // HasSME

// v8.4a MPAM and SME registers
//                              Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureMPAM, AArch64::FeatureSME} }] in {
def : RWSysReg<"MPAMSM_EL1",    0b11, 0b000, 0b1010, 0b0101, 0b011>;
} // HasMPAM, HasSME

// v8.8a Non-Maskable Interrupts
let Requires = [{ {AArch64::FeatureNMI} }] in {
  //                               Op0   Op1    CRn     CRm     Op2
  def : RWSysReg<"ALLINT",         0b11, 0b000, 0b0100, 0b0011, 0b000>;
  def : ROSysReg<"ICC_NMIAR1_EL1", 0b11, 0b000, 0b1100, 0b1001, 0b101>; // FEAT_GICv3_NMI
}

// v9.4a Guarded Control Stack Extension (GCS)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"GCSCR_EL1",   0b11, 0b000, 0b0010, 0b0101, 0b000>;
def : RWSysReg<"GCSPR_EL1",   0b11, 0b000, 0b0010, 0b0101, 0b001>;
def : RWSysReg<"GCSCRE0_EL1", 0b11, 0b000, 0b0010, 0b0101, 0b010>;
def : RWSysReg<"GCSPR_EL0",   0b11, 0b011, 0b0010, 0b0101, 0b001>;
def : RWSysReg<"GCSCR_EL2",   0b11, 0b100, 0b0010, 0b0101, 0b000>;
def : RWSysReg<"GCSPR_EL2",   0b11, 0b100, 0b0010, 0b0101, 0b001>;
def : RWSysReg<"GCSCR_EL12",  0b11, 0b101, 0b0010, 0b0101, 0b000>;
def : RWSysReg<"GCSPR_EL12",  0b11, 0b101, 0b0010, 0b0101, 0b001>;
def : RWSysReg<"GCSCR_EL3",   0b11, 0b110, 0b0010, 0b0101, 0b000>;
def : RWSysReg<"GCSPR_EL3",   0b11, 0b110, 0b0010, 0b0101, 0b001>;

// v8.9a/v9.4a Memory Attribute Index Enhancement (FEAT_AIE)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"AMAIR2_EL1",  0b11, 0b000, 0b1010, 0b0011, 0b001>;
def : RWSysReg<"AMAIR2_EL12", 0b11, 0b101, 0b1010, 0b0011, 0b001>;
def : RWSysReg<"AMAIR2_EL2",  0b11, 0b100, 0b1010, 0b0011, 0b001>;
def : RWSysReg<"AMAIR2_EL3",  0b11, 0b110, 0b1010, 0b0011, 0b001>;
def : RWSysReg<"MAIR2_EL1",   0b11, 0b000, 0b1010, 0b0010, 0b001>;
def : RWSysReg<"MAIR2_EL12",  0b11, 0b101, 0b1010, 0b0010, 0b001>;
def : RWSysReg<"MAIR2_EL2",   0b11, 0b100, 0b1010, 0b0001, 0b001>;
def : RWSysReg<"MAIR2_EL3",   0b11, 0b110, 0b1010, 0b0001, 0b001>;

// v8.9a/9.4a Stage 1 Permission Indirection Extension (FEAT_S1PIE)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PIRE0_EL1",   0b11, 0b000, 0b1010, 0b0010, 0b010>;
def : RWSysReg<"PIRE0_EL12",  0b11, 0b101, 0b1010, 0b0010, 0b010>;
def : RWSysReg<"PIRE0_EL2",   0b11, 0b100, 0b1010, 0b0010, 0b010>;
def : RWSysReg<"PIR_EL1",     0b11, 0b000, 0b1010, 0b0010, 0b011>;
def : RWSysReg<"PIR_EL12",    0b11, 0b101, 0b1010, 0b0010, 0b011>;
def : RWSysReg<"PIR_EL2",     0b11, 0b100, 0b1010, 0b0010, 0b011>;
def : RWSysReg<"PIR_EL3",     0b11, 0b110, 0b1010, 0b0010, 0b011>;

// v8.9a/v9.4a Stage 2 Permission Indirection Extension (FEAT_S2PIE)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"S2PIR_EL2",   0b11, 0b100, 0b1010, 0b0010, 0b101>;

// v8.9a/v9.4a Stage 1 Permission Overlay Extension (FEAT_S1POE)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"POR_EL0",     0b11, 0b011, 0b1010, 0b0010, 0b100>;
def : RWSysReg<"POR_EL1",     0b11, 0b000, 0b1010, 0b0010, 0b100>;
def : RWSysReg<"POR_EL12",    0b11, 0b101, 0b1010, 0b0010, 0b100>;
def : RWSysReg<"POR_EL2",     0b11, 0b100, 0b1010, 0b0010, 0b100>;
def : RWSysReg<"POR_EL3",     0b11, 0b110, 0b1010, 0b0010, 0b100>;

// v8.9a/v9.4a Stage 2 Permission Overlay Extension (FEAT_S2POE)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"S2POR_EL1",   0b11, 0b000, 0b1010, 0b0010, 0b101>;

// v8.9a/v9.4a Extension to System Control Registers (FEAT_SCTLR2)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"SCTLR2_EL1",  0b11, 0b000, 0b0001, 0b0000, 0b011>;
def : RWSysReg<"SCTLR2_EL12", 0b11, 0b101, 0b0001, 0b0000, 0b011>;
def : RWSysReg<"SCTLR2_EL2",  0b11, 0b100, 0b0001, 0b0000, 0b011>;
def : RWSysReg<"SCTLR2_EL3",  0b11, 0b110, 0b0001, 0b0000, 0b011>;

// v8.9a/v9.4a Extension to Translation Control Registers (FEAT_TCR2)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"TCR2_EL1",    0b11, 0b000, 0b0010, 0b0000, 0b011>;
def : RWSysReg<"TCR2_EL12",   0b11, 0b101, 0b0010, 0b0000, 0b011>;
def : RWSysReg<"TCR2_EL2",    0b11, 0b100, 0b0010, 0b0000, 0b011>;

// v8.9a/9.4a Translation Hardening Extension (FEAT_THE)
//                             Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureTHE} }] in {
def : RWSysReg<"RCWMASK_EL1",  0b11, 0b000, 0b1101, 0b0000, 0b110>;
def : RWSysReg<"RCWSMASK_EL1", 0b11, 0b000, 0b1101, 0b0000, 0b011>;
}

// v8.9a/9.4a new Debug feature (FEAT_DEBUGv8p9)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"MDSELR_EL1", 0b10, 0b000, 0b0000, 0b0100, 0b010>;

// v8.9a/9.4a new Performance Monitors Extension (FEAT_PMUv3p9)
//                            Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PMUACR_EL1", 0b11, 0b000, 0b1001, 0b1110, 0b100>;

// v8.9a/9.4a PMU Snapshot Extension (FEAT_PMUv3_SS)
//                                  Op0   Op1    CRn     CRm     Op2
def : ROSysReg<"PMCCNTSVR_EL1",     0b10, 0b000, 0b1110, 0b1011, 0b111>;
def : ROSysReg<"PMICNTSVR_EL1",     0b10, 0b000, 0b1110, 0b1100, 0b000>;
def : RWSysReg<"PMSSCR_EL1",        0b11, 0b000, 0b1001, 0b1101, 0b011>;
foreach n = 0-30 in {
  defvar nb = !cast<bits<5>>(n);
  def : ROSysReg<"PMEVCNTSVR"#n#"_EL1", 0b10, 0b000, 0b1110, {0b10,nb{4-3}}, nb{2-0}>;
}

// v8.9a/v9.4a PMUv3 Fixed-function instruction counter (FEAT_PMUv3_ICNTR)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PMICNTR_EL0",       0b11, 0b011, 0b1001, 0b0100, 0b000>;
def : RWSysReg<"PMICFILTR_EL0",     0b11, 0b011, 0b1001, 0b0110, 0b000>;

// v8.9a/v9.4a PMUv3 Performance Monitors Zero with Mask (FEAT_PMUv3p9/FEAT_PMUv3_ICNTR)
//                                  Op0   Op1    CRn     CRm     Op2
def : WOSysReg<"PMZR_EL0",          0b11, 0b011, 0b1001, 0b1101, 0b100>;

// v8.9a/9.4a Synchronous-Exception-Based Event Profiling extension (FEAT_SEBEP)
//                              Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PMECR_EL1",     0b11, 0b000, 0b1001, 0b1110, 0b101>;
def : RWSysReg<"PMIAR_EL1",     0b11, 0b000, 0b1001, 0b1110, 0b111>;

// v8.9a/9.4a System Performance Monitors Extension (FEAT_SPMU)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"SPMACCESSR_EL1",    0b10, 0b000, 0b1001, 0b1101, 0b011>;
def : RWSysReg<"SPMACCESSR_EL12",   0b10, 0b101, 0b1001, 0b1101, 0b011>;
def : RWSysReg<"SPMACCESSR_EL2",    0b10, 0b100, 0b1001, 0b1101, 0b011>;
def : RWSysReg<"SPMACCESSR_EL3",    0b10, 0b110, 0b1001, 0b1101, 0b011>;
def : RWSysReg<"SPMCNTENCLR_EL0",   0b10, 0b011, 0b1001, 0b1100, 0b010>;
def : RWSysReg<"SPMCNTENSET_EL0",   0b10, 0b011, 0b1001, 0b1100, 0b001>;
def : RWSysReg<"SPMCR_EL0",         0b10, 0b011, 0b1001, 0b1100, 0b000>;
def : ROSysReg<"SPMDEVAFF_EL1",     0b10, 0b000, 0b1001, 0b1101, 0b110>;
def : ROSysReg<"SPMDEVARCH_EL1",    0b10, 0b000, 0b1001, 0b1101, 0b101>;
foreach n = 0-15 in {
  defvar nb = !cast<bits<4>>(n);
  //                                     Op0   Op1    CRn     CRm            Op2
  def : RWSysReg<"SPMEVCNTR"#n#"_EL0",   0b10, 0b011, 0b1110, {0b000,nb{3}}, nb{2-0}>;
  def : RWSysReg<"SPMEVFILT2R"#n#"_EL0", 0b10, 0b011, 0b1110, {0b011,nb{3}}, nb{2-0}>;
  def : RWSysReg<"SPMEVFILTR"#n#"_EL0",  0b10, 0b011, 0b1110, {0b010,nb{3}}, nb{2-0}>;
  def : RWSysReg<"SPMEVTYPER"#n#"_EL0",  0b10, 0b011, 0b1110, {0b001,nb{3}}, nb{2-0}>;
}
//                                  Op0   Op1    CRn     CRm     Op2
def : ROSysReg<"SPMIIDR_EL1",       0b10, 0b000, 0b1001, 0b1101, 0b100>;
def : RWSysReg<"SPMINTENCLR_EL1",   0b10, 0b000, 0b1001, 0b1110, 0b010>;
def : RWSysReg<"SPMINTENSET_EL1",   0b10, 0b000, 0b1001, 0b1110, 0b001>;
def : RWSysReg<"SPMOVSCLR_EL0",     0b10, 0b011, 0b1001, 0b1100, 0b011>;
def : RWSysReg<"SPMOVSSET_EL0",     0b10, 0b011, 0b1001, 0b1110, 0b011>;
def : RWSysReg<"SPMSELR_EL0",       0b10, 0b011, 0b1001, 0b1100, 0b101>;
def : ROSysReg<"SPMCGCR0_EL1",      0b10, 0b000, 0b1001, 0b1101, 0b000>;
def : ROSysReg<"SPMCGCR1_EL1",      0b10, 0b000, 0b1001, 0b1101, 0b001>;
def : ROSysReg<"SPMCFGR_EL1",       0b10, 0b000, 0b1001, 0b1101, 0b111>;
def : RWSysReg<"SPMROOTCR_EL3",     0b10, 0b110, 0b1001, 0b1110, 0b111>;
def : RWSysReg<"SPMSCR_EL1",        0b10, 0b111, 0b1001, 0b1110, 0b111>;

// v8.9a/9.4a Instrumentation Extension (FEAT_ITE)
//                                  Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureITE} }] in {
def : RWSysReg<"TRCITEEDCR",        0b10, 0b001, 0b0000, 0b0010, 0b001>;
def : RWSysReg<"TRCITECR_EL1",      0b11, 0b000, 0b0001, 0b0010, 0b011>;
def : RWSysReg<"TRCITECR_EL12",     0b11, 0b101, 0b0001, 0b0010, 0b011>;
def : RWSysReg<"TRCITECR_EL2",      0b11, 0b100, 0b0001, 0b0010, 0b011>;
}

// v8.9a/9.4a SPE Data Source Filtering (FEAT_SPE_FDS)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PMSDSFR_EL1",       0b11, 0b000, 0b1001, 0b1010, 0b100>;

// v8.9a/9.4a RASv2 (FEAT_RASv2)
//                                  Op0   Op1    CRn     CRm     Op2
let Requires = [{ {AArch64::FeatureRASv2} }] in
def : ROSysReg<"ERXGSR_EL1",        0b11, 0b000, 0b0101, 0b0011, 0b010>;

// v8.9a/9.4a Physical Fault Address (FEAT_PFAR)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PFAR_EL1",          0b11, 0b000, 0b0110, 0b0000, 0b101>;
def : RWSysReg<"PFAR_EL12",         0b11, 0b101, 0b0110, 0b0000, 0b101>;
def : RWSysReg<"PFAR_EL2",          0b11, 0b100, 0b0110, 0b0000, 0b101>;

// v9.4a Exception-based event profiling (FEAT_EBEP)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"PM",                0b11, 0b000, 0b0100, 0b0011, 0b001>;

// 2023 ISA Extension
// AArch64 Floating-point Mode Register controls behaviors of the FP8
// instructions (FEAT_FPMR)
//                                 Op0   Op1    CRn     CRm     Op2
def : ROSysReg<"ID_AA64FPFR0_EL1", 0b11, 0b000, 0b0000, 0b0100, 0b111>;
def : RWSysReg<"FPMR",             0b11, 0b011, 0b0100, 0b0100, 0b010>;

// v9.5a Software Stepping Enhancements (FEAT_STEP2)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"MDSTEPOP_EL1",      0b10, 0b000, 0b0000, 0b0101, 0b010>;

// v9.5a System PMU zero register (FEAT_SPMU2)
//                                  Op0   Op1    CRn     CRm     Op2
def : WOSysReg<"SPMZR_EL0",         0b10, 0b011, 0b1001, 0b1100, 0b100>;

// v9.5a Delegated SError exceptions for EL3 (FEAT_E3DSE)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"VDISR_EL3",         0b11, 0b110, 0b1100, 0b0001, 0b001>;
def : RWSysReg<"VSESR_EL3",         0b11, 0b110, 0b0101, 0b0010, 0b011>;

// v9.5a Hardware Dirty State Tracking Structure (FEAT_HDBSS)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"HDBSSBR_EL2",       0b11, 0b100, 0b0010, 0b0011, 0b010>;
def : RWSysReg<"HDBSSPROD_EL2",     0b11, 0b100, 0b0010, 0b0011, 0b011>;

// v9.5a Hardware Accelerator for Cleaning Dirty State (FEAT_HACDBS)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"HACDBSBR_EL2",      0b11, 0b100, 0b0010, 0b0011, 0b100>;
def : RWSysReg<"HACDBSCONS_EL2",    0b11, 0b100, 0b0010, 0b0011, 0b101>;

// v9.5a Fine Grained Write Trap EL3 (FEAT_FGWTE3)
//                                  Op0   Op1    CRn     CRm     Op2
def : RWSysReg<"FGWTE3_EL3",        0b11, 0b110, 0b0001, 0b0001, 0b101>;
