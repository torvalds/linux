//===-- MIMGInstructions.td - MIMG Instruction Definitions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// MIMG-specific encoding families to distinguish between semantically
// equivalent machine instructions with different encoding.
//
// - MIMGEncGfx6: encoding introduced with gfx6 (obsoleted for atomics in gfx8)
// - MIMGEncGfx8: encoding introduced with gfx8 for atomics
// - MIMGEncGfx90a: encoding for gfx90a for atomics
// - MIMGEncGfx10Default: gfx10 default (non-NSA) encoding
// - MIMGEncGfx10NSA: gfx10 NSA encoding
// - MIMGEncGfx11Default: gfx11 default (non-NSA) encoding
// - MIMGEncGfx11NSA: gfx11 partial NSA encoding
// - MIMGEncGfx12: gfx12 encoding (partial NSA)
class MIMGEncoding;

def MIMGEncGfx6 : MIMGEncoding;
def MIMGEncGfx8 : MIMGEncoding;
def MIMGEncGfx90a : MIMGEncoding;
def MIMGEncGfx10Default : MIMGEncoding;
def MIMGEncGfx10NSA : MIMGEncoding;
def MIMGEncGfx11Default : MIMGEncoding;
def MIMGEncGfx11NSA : MIMGEncoding;
def MIMGEncGfx12 : MIMGEncoding;

def MIMGEncoding : GenericEnum {
  let FilterClass = "MIMGEncoding";
}

// Represent an ISA-level opcode, independent of the encoding and the
// vdata/vaddr size.
class MIMGBaseOpcode : PredicateControl {
  MIMGBaseOpcode BaseOpcode = !cast<MIMGBaseOpcode>(NAME);
  bit Store = 0;
  bit Atomic = 0;
  bit AtomicX2 = 0; // (f)cmpswap
  bit Sampler = 0;
  bit Gather4 = 0;
  bits<8> NumExtraArgs = 0;
  bit Gradients = 0;
  bit G16 = 0;
  bit Coordinates = 1;
  bit LodOrClampOrMip = 0;
  bit HasD16 = 0;
  bit IsAtomicRet = 0;
  bit MSAA = 0;
  bit BVH = 0;
  bit A16 = 0;
  bit NoReturn = 0;
}

def MIMGBaseOpcode : GenericEnum {
  let FilterClass = "MIMGBaseOpcode";
}

def MIMGBaseOpcodesTable : GenericTable {
  let FilterClass = "MIMGBaseOpcode";
  let CppTypeName = "MIMGBaseOpcodeInfo";
  let Fields = ["BaseOpcode", "Store", "Atomic", "AtomicX2", "Sampler",
                "Gather4", "NumExtraArgs", "Gradients", "G16", "Coordinates",
                "LodOrClampOrMip", "HasD16", "MSAA", "BVH", "A16", "NoReturn"];
  string TypeOf_BaseOpcode = "MIMGBaseOpcode";

  let PrimaryKey = ["BaseOpcode"];
  let PrimaryKeyName = "getMIMGBaseOpcodeInfo";
}

def MIMGDim : GenericEnum {
  let FilterClass = "AMDGPUDimProps";
}

def MIMGDimInfoTable : GenericTable {
  let FilterClass = "AMDGPUDimProps";
  let CppTypeName = "MIMGDimInfo";
  let Fields = ["Dim", "NumCoords", "NumGradients", "MSAA", "DA", "Encoding", "AsmSuffix"];
  string TypeOf_Dim = "MIMGDim";

  let PrimaryKey = ["Dim"];
  let PrimaryKeyName = "getMIMGDimInfo";
}

def getMIMGDimInfoByEncoding : SearchIndex {
  let Table = MIMGDimInfoTable;
  let Key = ["Encoding"];
}

def getMIMGDimInfoByAsmSuffix : SearchIndex {
  let Table = MIMGDimInfoTable;
  let Key = ["AsmSuffix"];
}

def MIMG {
  int NOP = -1;
}

class mimgopc <int gfx12, int gfx11, int gfx10m, int vi = gfx10m, int si = gfx10m> {
  field bits<8> GFX12 = gfx12;
  field bits<8> GFX11 = gfx11;
  field bits<8> GFX10M = gfx10m; // GFX10minus for all but atomics
  field bits<8> VI = vi; // VI is only used for atomic/sampler/gather instructions
  field bits<8> SI = si; // SI is only used for atomic instructions
  bit HAS_GFX12 = !ne(gfx12, MIMG.NOP);
  bit HAS_GFX11 = !ne(gfx11, MIMG.NOP);
  bit HAS_GFX10M = !ne(gfx10m, MIMG.NOP);
  bit HAS_VI = !ne(vi, MIMG.NOP);
  bit HAS_SI = !ne(si, MIMG.NOP);
}

class MIMGLZMapping<MIMGBaseOpcode l, MIMGBaseOpcode lz> {
  MIMGBaseOpcode L = l;
  MIMGBaseOpcode LZ = lz;
}

def MIMGLZMappingTable : GenericTable {
  let FilterClass = "MIMGLZMapping";
  let CppTypeName = "MIMGLZMappingInfo";
  let Fields = ["L", "LZ"];
  string TypeOf_L = "MIMGBaseOpcode";
  string TypeOf_LZ = "MIMGBaseOpcode";

  let PrimaryKey = ["L"];
  let PrimaryKeyName = "getMIMGLZMappingInfo";
}

class MIMGMIPMapping<MIMGBaseOpcode mip, MIMGBaseOpcode nonmip> {
  MIMGBaseOpcode MIP = mip;
  MIMGBaseOpcode NONMIP = nonmip;
}

def MIMGMIPMappingTable : GenericTable {
  let FilterClass = "MIMGMIPMapping";
  let CppTypeName = "MIMGMIPMappingInfo";
  let Fields = ["MIP", "NONMIP"];
  string TypeOf_MIP = "MIMGBaseOpcode";
  string TypeOf_NONMIP = "MIMGBaseOpcode";

  let PrimaryKey = ["MIP"];
  let PrimaryKeyName = "getMIMGMIPMappingInfo";
}

class MIMGBiasMapping<MIMGBaseOpcode bias, MIMGBaseOpcode nobias> {
  MIMGBaseOpcode Bias = bias;
  MIMGBaseOpcode NoBias = nobias;
}

def MIMGBiasMappingTable : GenericTable {
  let FilterClass = "MIMGBiasMapping";
  let CppTypeName = "MIMGBiasMappingInfo";
  let Fields = ["Bias", "NoBias"];
  string TypeOf_Bias = "MIMGBaseOpcode";
  string TypeOf_NoBias = "MIMGBaseOpcode";

  let PrimaryKey = ["Bias"];
  let PrimaryKeyName = "getMIMGBiasMappingInfo";
}

class MIMGOffsetMapping<MIMGBaseOpcode offset, MIMGBaseOpcode nooffset> {
  MIMGBaseOpcode Offset = offset;
  MIMGBaseOpcode NoOffset = nooffset;
}

def MIMGOffsetMappingTable : GenericTable {
  let FilterClass = "MIMGOffsetMapping";
  let CppTypeName = "MIMGOffsetMappingInfo";
  let Fields = ["Offset", "NoOffset"];
  string TypeOf_Offset = "MIMGBaseOpcode";
  string TypeOf_NoOffset = "MIMGBaseOpcode";

  let PrimaryKey = ["Offset"];
  let PrimaryKeyName = "getMIMGOffsetMappingInfo";
}

class MIMGG16Mapping<MIMGBaseOpcode g, MIMGBaseOpcode g16> {
  MIMGBaseOpcode G = g;
  MIMGBaseOpcode G16 = g16;
}

def MIMGG16MappingTable : GenericTable {
  let FilterClass = "MIMGG16Mapping";
  let CppTypeName = "MIMGG16MappingInfo";
  let Fields = ["G", "G16"];
  string TypeOf_G = "MIMGBaseOpcode";
  string TypeOf_G16 = "MIMGBaseOpcode";

  let PrimaryKey = ["G"];
  let PrimaryKeyName = "getMIMGG16MappingInfo";
}

class MIMG_Base <dag outs, string dns = "">
  : InstSI <outs, (ins), "", []> {

  let VM_CNT = 1;
  let EXP_CNT = 1;
  let MIMG = 1;
  let Uses = [EXEC];
  let mayLoad = 1;
  let mayStore = 0;
  let SchedRW = [WriteVMEM];
  let UseNamedOperandTable = 1;
  let hasSideEffects = 0; // XXX ????

  let DecoderNamespace = dns;
  let isAsmParserOnly = !eq(dns, "");
}

class MIMG <dag outs, string dns = "">
  : MIMG_Base <outs, dns> {

  let hasPostISelHook = 1;
  let usesCustomInserter = 1;

  Instruction Opcode = !cast<Instruction>(NAME);
  MIMGBaseOpcode BaseOpcode;
  MIMGEncoding MIMGEncoding;
  bits<8> VDataDwords;
  bits<8> VAddrDwords;

  // If NSA is used this counts number of operands VAddrDwords is split into.
  bits<8> VAddrOperands;
}

class VIMAGE <dag outs, string dns = ""> : MIMG<outs, dns> {
  let MIMG = 0;
  let VIMAGE = 1;
}

class VSAMPLE <dag outs, string dns = ""> : MIMG<outs, dns> {
  let MIMG = 0;
  let VSAMPLE = 1;
}

def MIMGInfoTable : GenericTable {
  let FilterClass = "MIMG";
  let CppTypeName = "MIMGInfo";
  let Fields = ["Opcode", "BaseOpcode", "MIMGEncoding", "VDataDwords",
                "VAddrDwords", "VAddrOperands"];
  string TypeOf_BaseOpcode = "MIMGBaseOpcode";
  string TypeOf_MIMGEncoding = "MIMGEncoding";

  let PrimaryKey = ["BaseOpcode", "MIMGEncoding", "VDataDwords", "VAddrDwords"];
  let PrimaryKeyName = "getMIMGOpcodeHelper";
}

def getMIMGInfo : SearchIndex {
  let Table = MIMGInfoTable;
  let Key = ["Opcode"];
}

class NSAHelper {
  dag AddrIns;
  string AddrAsm;
  int NSA;
}

class MIMGNSAHelper<int num_addrs,
                    list<RegisterClass> addr_types=!listsplat(VGPR_32, num_addrs)>
  : NSAHelper<> {
  list<string> AddrAsmNames = !foreach(i, !range(num_addrs), "vaddr" # i);
  let AddrIns = !dag(ins, addr_types, AddrAsmNames);
  let AddrAsm = "[$" # !interleave(AddrAsmNames, ", $") # "]";

  let NSA = !if(!le(num_addrs, 1), ?,
            !if(!le(num_addrs, 5), 1,
            !if(!le(num_addrs, 9), 2,
            !if(!le(num_addrs, 13), 3, ?))));
}

class PartialNSAHelper<int num_addrs, int max_addr, RegisterClass LastAddrRC>
  : NSAHelper<> {

  list<RegisterClass> addr_types =
    !if(!ge(num_addrs, max_addr),
      !listconcat(!listsplat(VGPR_32, !sub(max_addr, 1)), [LastAddrRC]),
      !listsplat(VGPR_32, num_addrs));

  int VAddrCount = !if(!gt(num_addrs, max_addr), max_addr, num_addrs);
  list<string> AddrAsmNames =  !foreach(i, !range(VAddrCount), "vaddr" # i);

  let AddrIns = !dag(ins, addr_types, AddrAsmNames);
  let AddrAsm = "[$" # !interleave(AddrAsmNames, ", $") # "]";
  let NSA = 1;
}

// Base class of all pre-gfx10 MIMG instructions.
class MIMG_gfx6789<bits<8> op, dag outs, string dns = "">
  : MIMG<outs, dns>, MIMGe_gfx6789<op> {
  let SubtargetPredicate = isGFX6GFX7GFX8GFX9NotGFX90A;
  let AssemblerPredicate = isGFX6GFX7GFX8GFX9NotGFX90A;

  let MIMGEncoding = MIMGEncGfx6;
  let VAddrOperands = 1;

  let d16 = !if(BaseOpcode.HasD16, ?, 0);
}

class MIMG_gfx90a<bits<8> op, dag outs, string dns = "">
  : MIMG<outs, dns>, MIMGe_gfx90a<op> {
  let SubtargetPredicate = isGFX90APlus;
  let AssemblerPredicate = isGFX90APlus;

  let MIMGEncoding = MIMGEncGfx90a;
  let VAddrOperands = 1;

  let d16 = !if(BaseOpcode.HasD16, ?, 0);
}

// Base class of all non-NSA gfx10 MIMG instructions.
class MIMG_gfx10<int op, dag outs, string dns = "">
  : MIMG<outs, dns>, MIMGe_gfx10<op> {
  let SubtargetPredicate = isGFX10Only;
  let AssemblerPredicate = isGFX10Only;

  let MIMGEncoding = MIMGEncGfx10Default;
  let VAddrOperands = 1;

  let d16 = !if(BaseOpcode.HasD16, ?, 0);
  let nsa = 0;
}

// Base class for all NSA MIMG instructions.
// Note that 1-dword addresses always use non-NSA variants.
class MIMG_nsa_gfx10<int op, dag outs, int num_addrs, string dns="">
  : MIMG<outs, dns>, MIMGe_gfx10<op> {
  let SubtargetPredicate = isGFX10Only;
  let AssemblerPredicate = isGFX10Only;

  let MIMGEncoding = MIMGEncGfx10NSA;
  let VAddrOperands = num_addrs;

  MIMGNSAHelper nsah = MIMGNSAHelper<num_addrs>;
  dag AddrIns = nsah.AddrIns;
  string AddrAsm = nsah.AddrAsm;

  let d16 = !if(BaseOpcode.HasD16, ?, 0);
  let nsa = nsah.NSA;
}

// Base class of all non-NSA gfx11 MIMG instructions.
class MIMG_gfx11<int op, dag outs, string dns = "">
  : MIMG<outs, dns>, MIMGe_gfx11<op> {
  let SubtargetPredicate = isGFX11Only;
  let AssemblerPredicate = isGFX11Only;

  let MIMGEncoding = MIMGEncGfx11Default;
  let VAddrOperands = 1;

  let d16 = !if(BaseOpcode.HasD16, ?, 0);
  let nsa = 0;
}

// Base class for all NSA MIMG instructions.
// Note that 1-dword addresses always use non-NSA variants.
class MIMG_nsa_gfx11<int op, dag outs, int num_addrs, string dns="",
                     list<RegisterClass> addr_types=[],
                     RegisterClass LastAddrRC = VGPR_32>
  : MIMG<outs, dns>, MIMGe_gfx11<op> {
  let SubtargetPredicate = isGFX11Only;
  let AssemblerPredicate = isGFX11Only;

  let MIMGEncoding = MIMGEncGfx11NSA;
  let VAddrOperands = num_addrs;

  NSAHelper nsah = !if(!empty(addr_types),
                        PartialNSAHelper<num_addrs, 5, LastAddrRC>,
                        MIMGNSAHelper<num_addrs, addr_types>);
  dag AddrIns = nsah.AddrIns;
  string AddrAsm = nsah.AddrAsm;

  let d16 = !if(BaseOpcode.HasD16, ?, 0);
  let nsa = nsah.NSA;
}

class VIMAGE_gfx12<int op, dag outs, int num_addrs, string dns="",
                   list<RegisterClass> addr_types=[]>
  : VIMAGE<outs, dns>, VIMAGEe<op> {
  let SubtargetPredicate = isGFX12Plus;
  let AssemblerPredicate = isGFX12Plus;

  let MIMGEncoding = MIMGEncGfx12;
  let VAddrOperands = num_addrs;

  MIMGNSAHelper nsah = !if(!empty(addr_types),
                           MIMGNSAHelper<num_addrs>,
                           MIMGNSAHelper<num_addrs, addr_types>);
  dag AddrIns = nsah.AddrIns;
  string AddrAsm = !if(!eq(num_addrs, 1), "$vaddr0", nsah.AddrAsm);

  let d16 = !if(BaseOpcode.HasD16, ?, 0);
  let vaddr1 = !if(!lt(num_addrs, 2), 0, ?);
  let vaddr2 = !if(!lt(num_addrs, 3), 0, ?);
  let vaddr3 = !if(!lt(num_addrs, 4), 0, ?);
  let vaddr4 = !if(!lt(num_addrs, 5), 0, ?);
}

class VSAMPLE_gfx12<int op, dag outs, int num_addrs, string dns="",
                    RegisterClass Addr3RC>
  : VSAMPLE<outs, dns>, VSAMPLEe<op> {
  let SubtargetPredicate = isGFX12Plus;
  let AssemblerPredicate = isGFX12Plus;

  let MIMGEncoding = MIMGEncGfx12;
  let VAddrOperands = num_addrs;

  PartialNSAHelper nsah = PartialNSAHelper<num_addrs, 4, Addr3RC>;

  dag AddrIns = nsah.AddrIns;
  string AddrAsm = !if(!eq(num_addrs, 1), "$vaddr0", nsah.AddrAsm);

  let d16 = !if(BaseOpcode.HasD16, ?, 0);
  let vaddr1 = !if(!lt(num_addrs, 2), 0, ?);
  let vaddr2 = !if(!lt(num_addrs, 3), 0, ?);
  let vaddr3 = !if(!lt(num_addrs, 4), 0, ?);
}

class MIMG_NoSampler_Helper <mimgopc op, string asm,
                             RegisterClass dst_rc,
                             RegisterClass addr_rc,
                             string dns="">
  : MIMG_gfx6789 <op.GFX10M, (outs dst_rc:$vdata), dns> {
  let InOperandList = !con((ins addr_rc:$vaddr, SReg_256:$srsrc,
                                DMask:$dmask, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, TFE:$tfe, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc$dmask$unorm$cpol$r128$tfe$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_NoSampler_Helper_gfx90a <mimgopc op, string asm,
                                    RegisterClass dst_rc,
                                    RegisterClass addr_rc,
                                    string dns="">
  : MIMG_gfx90a <op.GFX10M, (outs getLdStRegisterOperand<dst_rc>.ret:$vdata), dns> {
  let InOperandList = !con((ins addr_rc:$vaddr, SReg_256:$srsrc,
                                DMask:$dmask, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc$dmask$unorm$cpol$r128$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_NoSampler_gfx10<mimgopc op, string opcode,
                           RegisterClass DataRC, RegisterClass AddrRC,
                           string dns="">
  : MIMG_gfx10<op.GFX10M, (outs DataRC:$vdata), dns> {
  let InOperandList = !con((ins AddrRC:$vaddr0, SReg_256:$srsrc, DMask:$dmask,
                                Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, $vaddr0, $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_NoSampler_nsa_gfx10<mimgopc op, string opcode,
                               RegisterClass DataRC, int num_addrs,
                               string dns="">
  : MIMG_nsa_gfx10<op.GFX10M, (outs DataRC:$vdata), num_addrs, dns> {
  let InOperandList = !con(AddrIns,
                           (ins SReg_256:$srsrc, DMask:$dmask,
                                Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_NoSampler_gfx11<mimgopc op, string opcode,
                           RegisterClass DataRC, RegisterClass AddrRC,
                           string dns="">
  : MIMG_gfx11<op.GFX11, (outs DataRC:$vdata), dns> {
  let InOperandList = !con((ins AddrRC:$vaddr0, SReg_256:$srsrc, DMask:$dmask,
                                Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, $vaddr0, $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_NoSampler_nsa_gfx11<mimgopc op, string opcode,
                               RegisterClass DataRC, int num_addrs,
                               string dns="">
  : MIMG_nsa_gfx11<op.GFX11, (outs DataRC:$vdata), num_addrs, dns> {
  let InOperandList = !con(AddrIns,
                           (ins SReg_256:$srsrc, DMask:$dmask,
                                Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class VIMAGE_NoSampler_gfx12<mimgopc op, string opcode,
                             RegisterClass DataRC, int num_addrs,
                             string dns="">
  : VIMAGE_gfx12<op.GFX12, (outs DataRC:$vdata), num_addrs, dns> {
  let InOperandList = !con(AddrIns,
                           (ins SReg_256:$rsrc, DMask:$dmask, Dim:$dim,
                                CPol:$cpol, R128A16:$r128, A16:$a16, TFE:$tfe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $rsrc$dmask$dim$cpol$r128$a16$tfe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class VSAMPLE_Sampler_gfx12<mimgopc op, string opcode, RegisterClass DataRC,
                            int num_addrs, RegisterClass Addr3RC = VGPR_32,
                            string dns="">
  : VSAMPLE_gfx12<op.GFX12, (outs DataRC:$vdata), num_addrs, dns, Addr3RC> {
  let InOperandList = !con(AddrIns,
                           (ins SReg_256:$rsrc),
                           !if(BaseOpcode.Sampler, (ins SReg_128:$samp), (ins)),
                           (ins DMask:$dmask, Dim:$dim, UNorm:$unorm,
                                CPol:$cpol, R128A16:$r128, A16:$a16, TFE:$tfe,
                                LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $rsrc"
                    #!if(BaseOpcode.Sampler, ", $samp", "")
                    #"$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class VSAMPLE_Sampler_nortn_gfx12<mimgopc op, string opcode,
                            int num_addrs, RegisterClass Addr3RC = VGPR_32,
                            string dns="">
  : VSAMPLE_gfx12<op.GFX12, (outs), num_addrs, dns, Addr3RC> {
  let InOperandList = !con(AddrIns,
                           (ins SReg_256:$rsrc),
                           !if(BaseOpcode.Sampler, (ins SReg_128:$samp), (ins)),
                           (ins DMask:$dmask, Dim:$dim, UNorm:$unorm,
                                CPol:$cpol, R128A16:$r128, A16:$a16, TFE:$tfe,
                                LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" off, "#AddrAsm#", $rsrc"
                    #!if(BaseOpcode.Sampler, ", $samp", "")
                    #"$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
  // Force vdata to VGPR0 as no result will be returned.
  let vdata = 0;
}

multiclass MIMG_NoSampler_Src_Helper <mimgopc op, string asm,
                                      RegisterClass dst_rc, bit enableDisasm,
                                      bit ExtendedImageInst = 1,
                                      bit isVSample = 0> {
  let VAddrDwords = 1 in {
    let ssamp = 0 in {
      if op.HAS_GFX10M then {
        def _V1 : MIMG_NoSampler_Helper <op, asm, dst_rc, VGPR_32,
                                         !if(enableDisasm, "GFX8", "")>;
        if !not(ExtendedImageInst) then
        def _V1_gfx90a : MIMG_NoSampler_Helper_gfx90a <op, asm, dst_rc, VGPR_32,
                                       !if(enableDisasm, "GFX90A", "")>;
        def _V1_gfx10 : MIMG_NoSampler_gfx10<op, asm, dst_rc, VGPR_32,
                                             !if(enableDisasm, "GFX10", "")>;
      }
      if op.HAS_GFX11 then {
        def _V1_gfx11 : MIMG_NoSampler_gfx11<op, asm, dst_rc, VGPR_32,
                                             !if(enableDisasm, "GFX11", "")>;
      }
    }
    if op.HAS_GFX12 then {
      if isVSample then {
        let samp = 0 in
          def _V1_gfx12 : VSAMPLE_Sampler_gfx12<op, asm, dst_rc, 1>;
      }
      else {
        def _V1_gfx12 : VIMAGE_NoSampler_gfx12<op, asm, dst_rc, 1,
                                               !if(enableDisasm, "GFX12", "")>;
      }
    }
  }
  let VAddrDwords = 2 in {
    let ssamp = 0 in {
      if op.HAS_GFX10M then {
        def _V2 : MIMG_NoSampler_Helper <op, asm, dst_rc, VReg_64>;
        if !not(ExtendedImageInst) then
        def _V2_gfx90a : MIMG_NoSampler_Helper_gfx90a <op, asm, dst_rc, VReg_64>;
        def _V2_gfx10 : MIMG_NoSampler_gfx10<op, asm, dst_rc, VReg_64>;
        def _V2_nsa_gfx10 : MIMG_NoSampler_nsa_gfx10<op, asm, dst_rc, 2>;
      }
      if op.HAS_GFX11 then {
        def _V2_gfx11 : MIMG_NoSampler_gfx11<op, asm, dst_rc, VReg_64>;
        def _V2_nsa_gfx11 : MIMG_NoSampler_nsa_gfx11<op, asm, dst_rc, 2>;
      }
    }
    if op.HAS_GFX12 then {
      if isVSample then {
        let samp = 0 in
          def _V2_gfx12 : VSAMPLE_Sampler_gfx12<op, asm, dst_rc, 2>;
      }
      else {
        def _V2_gfx12 : VIMAGE_NoSampler_gfx12<op, asm, dst_rc, 2>;
      }
    }
  }
  let VAddrDwords = 3 in {
    let ssamp = 0 in {
      if op.HAS_GFX10M then {
        def _V3 : MIMG_NoSampler_Helper <op, asm, dst_rc, VReg_96>;
        if !not(ExtendedImageInst) then
        def _V3_gfx90a : MIMG_NoSampler_Helper_gfx90a <op, asm, dst_rc, VReg_96>;
        def _V3_gfx10 : MIMG_NoSampler_gfx10<op, asm, dst_rc, VReg_96>;
        def _V3_nsa_gfx10 : MIMG_NoSampler_nsa_gfx10<op, asm, dst_rc, 3>;
      }
      if op.HAS_GFX11 then {
        def _V3_gfx11 : MIMG_NoSampler_gfx11<op, asm, dst_rc, VReg_96>;
        def _V3_nsa_gfx11 : MIMG_NoSampler_nsa_gfx11<op, asm, dst_rc, 3>;
      }
    }
    if op.HAS_GFX12 then {
      if isVSample then {
        let samp = 0 in
          def _V3_gfx12 : VSAMPLE_Sampler_gfx12<op, asm, dst_rc, 3>;
      }
      else {
        def _V3_gfx12 : VIMAGE_NoSampler_gfx12<op, asm, dst_rc, 3>;
      }
    }
  }
  let VAddrDwords = 4 in {
    let ssamp = 0 in {
      if op.HAS_GFX10M then {
        def _V4 : MIMG_NoSampler_Helper <op, asm, dst_rc, VReg_128>;
        if !not(ExtendedImageInst) then
        def _V4_gfx90a : MIMG_NoSampler_Helper_gfx90a <op, asm, dst_rc, VReg_128>;
        def _V4_gfx10 : MIMG_NoSampler_gfx10<op, asm, dst_rc, VReg_128>;
        def _V4_nsa_gfx10 : MIMG_NoSampler_nsa_gfx10<op, asm, dst_rc, 4,
                                                     !if(enableDisasm, "GFX10", "")>;
      }
      if op.HAS_GFX11 then {
        def _V4_gfx11 : MIMG_NoSampler_gfx11<op, asm, dst_rc, VReg_128>;
        def _V4_nsa_gfx11 : MIMG_NoSampler_nsa_gfx11<op, asm, dst_rc, 4,
                                                     !if(enableDisasm, "GFX11", "")>;
      }
    }
    if op.HAS_GFX12 then {
      if isVSample then {
        let samp = 0 in
          def _V4_gfx12 : VSAMPLE_Sampler_gfx12<op, asm, dst_rc, 4, VGPR_32,
                                                !if(enableDisasm, "GFX12", "")>;
      }
      else {
        def _V4_gfx12 : VIMAGE_NoSampler_gfx12<op, asm, dst_rc, 4,
                                               !if(enableDisasm, "GFX12", "")>;
      }
    }
  }
}

multiclass MIMG_NoSampler <mimgopc op, string asm, bit has_d16, bit mip = 0,
                           bit isResInfo = 0,
                           bit msaa = 0> {
  def "" : MIMGBaseOpcode {
    let Coordinates = !not(isResInfo);
    let LodOrClampOrMip = mip;
    let HasD16 = has_d16;
    let MSAA = msaa;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME),
      mayLoad = !not(isResInfo) in {
    let VDataDwords = 1 in
    defm _V1 : MIMG_NoSampler_Src_Helper <op, asm, VGPR_32, 1, msaa>;
    let VDataDwords = 2 in
    defm _V2 : MIMG_NoSampler_Src_Helper <op, asm, VReg_64, 0, msaa>;
    let VDataDwords = 3 in
    defm _V3 : MIMG_NoSampler_Src_Helper <op, asm, VReg_96, 0, msaa>;
    let VDataDwords = 4 in
    defm _V4 : MIMG_NoSampler_Src_Helper <op, asm, VReg_128, 0, msaa>;
    let VDataDwords = 5 in
    defm _V5 : MIMG_NoSampler_Src_Helper <op, asm, VReg_160, 0, msaa>;
  }
}

class MIMG_Store_Helper <mimgopc op, string asm,
                         RegisterClass data_rc,
                         RegisterClass addr_rc,
                         string dns = "">
  : MIMG_gfx6789<op.GFX10M, (outs), dns> {
  let InOperandList = !con((ins data_rc:$vdata, addr_rc:$vaddr, SReg_256:$srsrc,
                                DMask:$dmask, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, TFE:$tfe, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc$dmask$unorm$cpol$r128$tfe$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_Store_Helper_gfx90a <mimgopc op, string asm,
                                RegisterClass data_rc,
                                RegisterClass addr_rc,
                                string dns = "">
  : MIMG_gfx90a<op.GFX10M, (outs), dns> {
  let InOperandList = !con((ins getLdStRegisterOperand<data_rc>.ret:$vdata,
                                addr_rc:$vaddr, SReg_256:$srsrc,
                                DMask:$dmask, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc$dmask$unorm$cpol$r128$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_Store_gfx10<mimgopc op, string opcode,
                       RegisterClass DataRC, RegisterClass AddrRC,
                       string dns="">
  : MIMG_gfx10<op.GFX10M, (outs), dns> {
  let InOperandList = !con((ins DataRC:$vdata, AddrRC:$vaddr0, SReg_256:$srsrc,
                                DMask:$dmask, Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, $vaddr0, $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_Store_nsa_gfx10<mimgopc op, string opcode,
                           RegisterClass DataRC, int num_addrs,
                           string dns="">
  : MIMG_nsa_gfx10<op.GFX10M, (outs), num_addrs, dns> {
  let InOperandList = !con((ins DataRC:$vdata),
                           AddrIns,
                           (ins SReg_256:$srsrc, DMask:$dmask,
                                Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_Store_gfx11<mimgopc op, string opcode,
                       RegisterClass DataRC, RegisterClass AddrRC,
                       string dns="">
  : MIMG_gfx11<op.GFX11, (outs), dns> {
  let InOperandList = !con((ins DataRC:$vdata, AddrRC:$vaddr0, SReg_256:$srsrc,
                                DMask:$dmask, Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, $vaddr0, $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_Store_nsa_gfx11<mimgopc op, string opcode,
                           RegisterClass DataRC, int num_addrs,
                           string dns="">
  : MIMG_nsa_gfx11<op.GFX11, (outs), num_addrs, dns> {
  let InOperandList = !con((ins DataRC:$vdata),
                           AddrIns,
                           (ins SReg_256:$srsrc, DMask:$dmask,
                                Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

class VIMAGE_Store_gfx12<mimgopc op, string opcode,
                         RegisterClass DataRC, int num_addrs,
                         string dns="">
  : VIMAGE_gfx12<op.GFX12, (outs), num_addrs, dns> {
  let InOperandList = !con((ins DataRC:$vdata),
                           AddrIns,
                           (ins SReg_256:$rsrc, DMask:$dmask, Dim:$dim,
                                CPol:$cpol, R128A16:$r128, A16:$a16, TFE:$tfe),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $rsrc$dmask$dim$cpol$r128$a16$tfe"
                    #!if(BaseOpcode.HasD16, "$d16", "");
}

multiclass MIMG_Store_Addr_Helper <mimgopc op, string asm,
                                  RegisterClass data_rc,
                                  bit enableDisasm> {
  let mayLoad = 0, mayStore = 1, hasSideEffects = 0, hasPostISelHook = 0,
      DisableWQM = 1 in {
    let VAddrDwords = 1 in {
      let ssamp = 0 in {
        if op.HAS_GFX10M then {
          def _V1 : MIMG_Store_Helper <op, asm, data_rc, VGPR_32,
                                      !if(enableDisasm, "GFX8", "")>;
          let hasPostISelHook = 1 in
          def _V1_gfx90a : MIMG_Store_Helper_gfx90a <op, asm, data_rc, VGPR_32,
                                      !if(enableDisasm, "GFX90A", "")>;
          def _V1_gfx10 : MIMG_Store_gfx10 <op, asm, data_rc, VGPR_32,
                                            !if(enableDisasm, "GFX10", "")>;
        }
        if op.HAS_GFX11 then {
          def _V1_gfx11 : MIMG_Store_gfx11 <op, asm, data_rc, VGPR_32,
                                            !if(enableDisasm, "GFX11", "")>;
        }
      }
      if op.HAS_GFX12 then {
        def _V1_gfx12 : VIMAGE_Store_gfx12 <op, asm, data_rc, 1,
                                            !if(enableDisasm, "GFX12", "")>;
      }
    }
    let VAddrDwords = 2 in {
      let ssamp = 0 in {
        if op.HAS_GFX10M then {
          def _V2 : MIMG_Store_Helper <op, asm, data_rc, VReg_64>;
          def _V2_gfx90a : MIMG_Store_Helper_gfx90a <op, asm, data_rc, VReg_64>;
          def _V2_gfx10 : MIMG_Store_gfx10 <op, asm, data_rc, VReg_64>;
          def _V2_nsa_gfx10 : MIMG_Store_nsa_gfx10 <op, asm, data_rc, 2>;
        }
        if op.HAS_GFX11 then {
          def _V2_gfx11 : MIMG_Store_gfx11 <op, asm, data_rc, VReg_64>;
          def _V2_nsa_gfx11 : MIMG_Store_nsa_gfx11 <op, asm, data_rc, 2>;
        }
      }
      if op.HAS_GFX12 then {
        def _V2_gfx12 : VIMAGE_Store_gfx12 <op, asm, data_rc, 2>;
      }
    }
    let VAddrDwords = 3 in {
      let ssamp = 0 in {
        if op.HAS_GFX10M then {
          def _V3 : MIMG_Store_Helper <op, asm, data_rc, VReg_96>;
          def _V3_gfx90a : MIMG_Store_Helper_gfx90a <op, asm, data_rc, VReg_96>;
          def _V3_gfx10 : MIMG_Store_gfx10 <op, asm, data_rc, VReg_96>;
          def _V3_nsa_gfx10 : MIMG_Store_nsa_gfx10 <op, asm, data_rc, 3>;
        }
        if op.HAS_GFX11 then {
          def _V3_gfx11 : MIMG_Store_gfx11 <op, asm, data_rc, VReg_96>;
          def _V3_nsa_gfx11 : MIMG_Store_nsa_gfx11 <op, asm, data_rc, 3>;
        }
      }
      if op.HAS_GFX12 then {
        def _V3_gfx12 : VIMAGE_Store_gfx12 <op, asm, data_rc, 3>;
      }
    }
    let VAddrDwords = 4 in {
      let ssamp = 0 in {
        if op.HAS_GFX10M then {
          def _V4 : MIMG_Store_Helper <op, asm, data_rc, VReg_128>;
          def _V4_gfx90a : MIMG_Store_Helper_gfx90a <op, asm, data_rc, VReg_128>;
          def _V4_gfx10 : MIMG_Store_gfx10 <op, asm, data_rc, VReg_128>;
          def _V4_nsa_gfx10 : MIMG_Store_nsa_gfx10 <op, asm, data_rc, 4,
                                                          !if(enableDisasm, "GFX10", "")>;
        }
        if op.HAS_GFX11 then {
          def _V4_gfx11 : MIMG_Store_gfx11 <op, asm, data_rc, VReg_128>;
          def _V4_nsa_gfx11 : MIMG_Store_nsa_gfx11 <op, asm, data_rc, 4,
                                                          !if(enableDisasm, "GFX11", "")>;
        }
      }
      if op.HAS_GFX12 then {
        def _V4_gfx12 : VIMAGE_Store_gfx12 <op, asm, data_rc, 4,
                                            !if(enableDisasm, "GFX12", "")>;
      }
    }
  }
}

multiclass MIMG_Store <mimgopc op, string asm, bit has_d16, bit mip = 0> {
  def "" : MIMGBaseOpcode {
    let Store = 1;
    let LodOrClampOrMip = mip;
    let HasD16 = has_d16;
    let NoReturn = 1;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME) in {
    let VDataDwords = 1 in
    defm _V1 : MIMG_Store_Addr_Helper <op, asm, VGPR_32, 1>;
    let VDataDwords = 2 in
    defm _V2 : MIMG_Store_Addr_Helper <op, asm, VReg_64, 0>;
    let VDataDwords = 3 in
    defm _V3 : MIMG_Store_Addr_Helper <op, asm, VReg_96, 0>;
    let VDataDwords = 4 in
    defm _V4 : MIMG_Store_Addr_Helper <op, asm, VReg_128, 0>;
    let VDataDwords = 5 in
    defm _V5 : MIMG_Store_Addr_Helper <op, asm, VReg_160, 0>;
  }
}

class MIMG_Atomic_gfx6789_base <bits<8> op, string asm, RegisterClass data_rc,
                                RegisterClass addr_rc, string dns="">
  : MIMG_gfx6789 <op, (outs data_rc:$vdst), dns> {
  let Constraints = "$vdst = $vdata";

  let InOperandList = (ins data_rc:$vdata, addr_rc:$vaddr, SReg_256:$srsrc,
                           DMask:$dmask, UNorm:$unorm, CPol:$cpol,
                           R128A16:$r128, TFE:$tfe, LWE:$lwe, DA:$da);
  let AsmString = asm#" $vdst, $vaddr, $srsrc$dmask$unorm$cpol$r128$tfe$lwe$da";
}

class MIMG_Atomic_gfx90a_base <bits<8> op, string asm, RegisterClass data_rc,
                               RegisterClass addr_rc, string dns="">
  : MIMG_gfx90a <op, (outs getLdStRegisterOperand<data_rc>.ret:$vdst), dns> {
  let Constraints = "$vdst = $vdata";

  let InOperandList = (ins getLdStRegisterOperand<data_rc>.ret:$vdata,
                           addr_rc:$vaddr, SReg_256:$srsrc,
                           DMask:$dmask, UNorm:$unorm, CPol:$cpol,
                           R128A16:$r128, LWE:$lwe, DA:$da);
  let AsmString = asm#" $vdst, $vaddr, $srsrc$dmask$unorm$cpol$r128$lwe$da";
}

class MIMG_Atomic_si<mimgopc op, string asm, RegisterClass data_rc,
                     RegisterClass addr_rc, bit enableDasm = 0>
  : MIMG_Atomic_gfx6789_base<op.SI, asm, data_rc, addr_rc,
                             !if(enableDasm, "GFX6GFX7", "")> {
  let AssemblerPredicate = isGFX6GFX7;
}

class MIMG_Atomic_vi<mimgopc op, string asm, RegisterClass data_rc,
                     RegisterClass addr_rc, bit enableDasm = 0>
  : MIMG_Atomic_gfx6789_base<op.VI, asm, data_rc, addr_rc, !if(enableDasm, "GFX8", "")> {
  let AssemblerPredicate = isGFX8GFX9NotGFX90A;
  let MIMGEncoding = MIMGEncGfx8;
}

class MIMG_Atomic_gfx90a<mimgopc op, string asm, RegisterClass data_rc,
                         RegisterClass addr_rc, bit enableDasm = 0>
  : MIMG_Atomic_gfx90a_base<op.VI, asm, data_rc, addr_rc, !if(enableDasm, "GFX90A", "")> {
  let AssemblerPredicate = isGFX90APlus;
  let MIMGEncoding = MIMGEncGfx90a;
}

class MIMG_Atomic_gfx10<mimgopc op, string opcode,
                        RegisterClass DataRC, RegisterClass AddrRC,
                        bit enableDisasm = 0>
  : MIMG_gfx10<!cast<int>(op.GFX10M), (outs DataRC:$vdst),
               !if(enableDisasm, "GFX10", "")> {
  let Constraints = "$vdst = $vdata";

  let InOperandList = (ins DataRC:$vdata, AddrRC:$vaddr0, SReg_256:$srsrc,
                           DMask:$dmask, Dim:$dim, UNorm:$unorm, CPol:$cpol,
                           R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe);
  let AsmString = opcode#" $vdst, $vaddr0, $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe";
}

class MIMG_Atomic_nsa_gfx10<mimgopc op, string opcode,
                            RegisterClass DataRC, int num_addrs,
                            bit enableDisasm = 0>
  : MIMG_nsa_gfx10<!cast<int>(op.GFX10M), (outs DataRC:$vdst), num_addrs,
                   !if(enableDisasm, "GFX10", "")> {
  let Constraints = "$vdst = $vdata";

  let InOperandList = !con((ins DataRC:$vdata),
                           AddrIns,
                           (ins SReg_256:$srsrc, DMask:$dmask,
                                Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe";
}

class MIMG_Atomic_gfx11<mimgopc op, string opcode,
                        RegisterClass DataRC, RegisterClass AddrRC,
                        bit enableDisasm = 0>
  : MIMG_gfx11<!cast<int>(op.GFX11), (outs DataRC:$vdst),
               !if(enableDisasm, "GFX11", "")> {
  let Constraints = "$vdst = $vdata";

  let InOperandList = (ins DataRC:$vdata, AddrRC:$vaddr0, SReg_256:$srsrc,
                           DMask:$dmask, Dim:$dim, UNorm:$unorm, CPol:$cpol,
                           R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe);
  let AsmString = opcode#" $vdst, $vaddr0, $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe";
}

class MIMG_Atomic_nsa_gfx11<mimgopc op, string opcode,
                            RegisterClass DataRC, int num_addrs,
                            bit enableDisasm = 0>
  : MIMG_nsa_gfx11<!cast<int>(op.GFX11), (outs DataRC:$vdst), num_addrs,
                   !if(enableDisasm, "GFX11", "")> {
  let Constraints = "$vdst = $vdata";

  let InOperandList = !con((ins DataRC:$vdata),
                           AddrIns,
                           (ins SReg_256:$srsrc, DMask:$dmask,
                                Dim:$dim, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $srsrc$dmask$dim$unorm$cpol$r128$a16$tfe$lwe";
}

class VIMAGE_Atomic_gfx12<mimgopc op, string opcode, RegisterClass DataRC,
                          int num_addrs, bit enableDisasm = 0>
  : VIMAGE_gfx12<!cast<int>(op.GFX12), (outs DataRC:$vdst), num_addrs,
                  !if(enableDisasm, "GFX12", "")> {
  let Constraints = "$vdst = $vdata";

  let InOperandList = !con((ins DataRC:$vdata),
                           AddrIns,
                           (ins SReg_256:$rsrc, DMask:$dmask, Dim:$dim,
                                 CPol:$cpol, R128A16:$r128, A16:$a16, TFE:$tfe));
  let AsmString = opcode#" $vdata, "#AddrAsm#", $rsrc$dmask$dim$cpol$r128$a16$tfe";
}

class VIMAGE_Atomic_gfx12_Renamed<mimgopc op, string renamed,
                                  RegisterClass DataRC, int num_addrs,
                                  bit enableDisasm = 0>
  : VIMAGE_Atomic_gfx12<op, renamed, DataRC, num_addrs, enableDisasm>;

multiclass MIMG_Atomic_Addr_Helper_m <mimgopc op, string asm,
                                      RegisterClass data_rc,
                                      bit enableDasm = 0,
                                      bit isFP = 0,
                                      string renamed = ""> {
  let hasSideEffects = 1, // FIXME: remove this
      mayLoad = 1, mayStore = 1, hasPostISelHook = 0, DisableWQM = 1,
      FPAtomic = isFP in {
    let VAddrDwords = 1 in {
      let ssamp = 0 in {
        if op.HAS_SI then {
          def _V1_si : MIMG_Atomic_si <op, asm, data_rc, VGPR_32, enableDasm>;
        }
        if op.HAS_VI then {
          def _V1_vi : MIMG_Atomic_vi <op, asm, data_rc, VGPR_32, enableDasm>;
          let hasPostISelHook = 1 in
          def _V1_gfx90a : MIMG_Atomic_gfx90a <op, asm, data_rc, VGPR_32, enableDasm>;
        }
        if op.HAS_GFX10M then {
          def _V1_gfx10 : MIMG_Atomic_gfx10 <op, asm, data_rc, VGPR_32, enableDasm>;
        }
        if op.HAS_GFX11 then {
          def _V1_gfx11 : MIMG_Atomic_gfx11 <op, asm, data_rc, VGPR_32, enableDasm>;
        }
      }
      if op.HAS_GFX12 then {
        if !empty(renamed) then
          def _V1_gfx12 : VIMAGE_Atomic_gfx12 <op, asm, data_rc, 1, enableDasm>;
        else
          def _V1_gfx12 : VIMAGE_Atomic_gfx12_Renamed <op, renamed, data_rc, 1, enableDasm>;
      }
    }
    let VAddrDwords = 2 in {
      let ssamp = 0 in {
        if op.HAS_SI then {
          def _V2_si : MIMG_Atomic_si <op, asm, data_rc, VReg_64, 0>;
        }
        if op.HAS_VI then {
          def _V2_vi : MIMG_Atomic_vi <op, asm, data_rc, VReg_64, 0>;
          def _V2_gfx90a : MIMG_Atomic_gfx90a <op, asm, data_rc, VReg_64, 0>;
        }
        if op.HAS_GFX10M then {
          def _V2_gfx10 : MIMG_Atomic_gfx10 <op, asm, data_rc, VReg_64, 0>;
          def _V2_nsa_gfx10 : MIMG_Atomic_nsa_gfx10 <op, asm, data_rc, 2, 0>;
        }
        if op.HAS_GFX11 then {
          def _V2_gfx11 : MIMG_Atomic_gfx11 <op, asm, data_rc, VReg_64, 0>;
          def _V2_nsa_gfx11 : MIMG_Atomic_nsa_gfx11 <op, asm, data_rc, 2, 0>;
        }
      }
      if op.HAS_GFX12 then {
        if !empty(renamed) then
          def _V2_gfx12 : VIMAGE_Atomic_gfx12 <op, asm, data_rc, 2, 0>;
        else
          def _V2_gfx12 : VIMAGE_Atomic_gfx12_Renamed <op, renamed, data_rc, 2, 0>;
      }
    }
    let VAddrDwords = 3 in {
      let ssamp = 0 in {
        if op.HAS_SI then {
          def _V3_si : MIMG_Atomic_si <op, asm, data_rc, VReg_96, 0>;
        }
        if op.HAS_VI then {
          def _V3_vi : MIMG_Atomic_vi <op, asm, data_rc, VReg_96, 0>;
          def _V3_gfx90a : MIMG_Atomic_gfx90a <op, asm, data_rc, VReg_96, 0>;
        }
        if op.HAS_GFX10M then {
          def _V3_gfx10 : MIMG_Atomic_gfx10 <op, asm, data_rc, VReg_96, 0>;
          def _V3_nsa_gfx10 : MIMG_Atomic_nsa_gfx10 <op, asm, data_rc, 3, 0>;
        }
        if op.HAS_GFX11 then {
          def _V3_gfx11 : MIMG_Atomic_gfx11 <op, asm, data_rc, VReg_96, 0>;
          def _V3_nsa_gfx11 : MIMG_Atomic_nsa_gfx11 <op, asm, data_rc, 3, 0>;
        }
      }
      if op.HAS_GFX12 then {
        if !empty(renamed) then
          def _V3_gfx12 : VIMAGE_Atomic_gfx12 <op, asm, data_rc, 3, 0>;
        else
          def _V3_gfx12 : VIMAGE_Atomic_gfx12_Renamed <op, renamed, data_rc, 3, 0>;
      }
    }
    let VAddrDwords = 4 in {
      let ssamp = 0 in {
        if op.HAS_SI then {
          def _V4_si : MIMG_Atomic_si <op, asm, data_rc, VReg_128, 0>;
        }
        if op.HAS_VI then {
          def _V4_vi : MIMG_Atomic_vi <op, asm, data_rc, VReg_128, 0>;
          def _V4_gfx90a : MIMG_Atomic_gfx90a <op, asm, data_rc, VReg_128, 0>;
        }
        if op.HAS_GFX10M then {
          def _V4_gfx10 : MIMG_Atomic_gfx10 <op, asm, data_rc, VReg_128, 0>;
          def _V4_nsa_gfx10 : MIMG_Atomic_nsa_gfx10 <op, asm, data_rc, 4, enableDasm>;
        }
        if op.HAS_GFX11 then {
          def _V4_gfx11 : MIMG_Atomic_gfx11 <op, asm, data_rc, VReg_128, 0>;
          def _V4_nsa_gfx11 : MIMG_Atomic_nsa_gfx11 <op, asm, data_rc, 4, enableDasm>;
        }
      }
      if op.HAS_GFX12 then {
        if !empty(renamed) then
          def _V4_gfx12 : VIMAGE_Atomic_gfx12 <op, asm, data_rc, 4, enableDasm>;
        else
          def _V4_gfx12 : VIMAGE_Atomic_gfx12_Renamed <op, renamed, data_rc, 4, enableDasm>;
      }
    }
  }
  if !and(op.HAS_GFX12, !not(!empty(renamed))) then
    def : AMDGPUMnemonicAlias<asm, renamed> {
      let AssemblerPredicate = isGFX12Plus;
      bit IsAtomicRet; // Unused
      MIMGBaseOpcode BaseOpcode; // Unused
      int VDataDwords; // Unused
    }
}

multiclass MIMG_Atomic <mimgopc op, string asm, bit isCmpSwap = 0, bit isFP = 0,
                        string renamed = ""> { // 64-bit atomics
  let IsAtomicRet = 1 in {
    def "" : MIMGBaseOpcode {
      let Atomic = 1;
      let AtomicX2 = isCmpSwap;
    }

    let BaseOpcode = !cast<MIMGBaseOpcode>(NAME) in {
      // _V* variants have different dst size, but the size is encoded implicitly,
      // using dmask and tfe. Only 32-bit variant is registered with disassembler.
      // Other variants are reconstructed by disassembler using dmask and tfe.
      let VDataDwords = !if(isCmpSwap, 2, 1) in
      defm _V1 : MIMG_Atomic_Addr_Helper_m <op, asm, !if(isCmpSwap, VReg_64, VGPR_32), 1, isFP, renamed>;
      let VDataDwords = !if(isCmpSwap, 4, 2) in
      defm _V2 : MIMG_Atomic_Addr_Helper_m <op, asm, !if(isCmpSwap, VReg_128, VReg_64), 0, isFP, renamed>;
      let VDataDwords = !if(isCmpSwap, 2, 2) in
      defm _V3 : MIMG_Atomic_Addr_Helper_m <op, asm, VReg_96, 0, isFP, renamed>;
      let VDataDwords = !if(isCmpSwap, 4, 4) in
      defm _V4 : MIMG_Atomic_Addr_Helper_m <op, asm, VReg_160, 0, isFP, renamed>;
    }
  } // End IsAtomicRet = 1
}

multiclass MIMG_Atomic_Renamed <mimgopc op, string asm, string renamed,
                                bit isCmpSwap = 0, bit isFP = 0>
  : MIMG_Atomic <op, asm, isCmpSwap, isFP, renamed>;

class MIMG_Sampler_Helper <mimgopc op, string asm, RegisterClass dst_rc,
                           RegisterClass src_rc, string dns="">
  : MIMG_gfx6789 <op.VI, (outs dst_rc:$vdata), dns> {
  let InOperandList = !con((ins src_rc:$vaddr, SReg_256:$srsrc, SReg_128:$ssamp,
                                DMask:$dmask, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, TFE:$tfe, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc, $ssamp$dmask$unorm$cpol$r128$tfe$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_Sampler_gfx90a<mimgopc op, string asm, RegisterClass dst_rc,
                          RegisterClass src_rc, string dns="">
  : MIMG_gfx90a<op.GFX10M, (outs getLdStRegisterOperand<dst_rc>.ret:$vdata), dns> {
  let InOperandList = !con((ins src_rc:$vaddr, SReg_256:$srsrc, SReg_128:$ssamp,
                                DMask:$dmask, UNorm:$unorm, CPol:$cpol,
                                R128A16:$r128, LWE:$lwe, DA:$da),
                           !if(BaseOpcode.HasD16, (ins D16:$d16), (ins)));
  let AsmString = asm#" $vdata, $vaddr, $srsrc, $ssamp$dmask$unorm$cpol$r128$lwe$da"
                      #!if(BaseOpcode.HasD16, "$d16", "");
}

class MIMG_Sampler_OpList_gfx10p<dag OpPrefix, bit HasD16> {
  dag ret = !con(OpPrefix,
                 (ins SReg_256:$srsrc, SReg_128:$ssamp,
                  DMask:$dmask, Dim:$dim, UNorm:$unorm, CPol:$cpol,
                  R128A16:$r128, A16:$a16, TFE:$tfe, LWE:$lwe),
                 !if(HasD16, (ins D16:$d16), (ins)));
}

class MIMG_Sampler_Asm_gfx10p<string opcode, string AsmPrefix, bit HasD16> {
  string ret = opcode#" "#AsmPrefix#", $srsrc, $ssamp$dmask$dim$unorm"
               #"$cpol$r128$a16$tfe$lwe"
               #!if(HasD16, "$d16", "");
}

class MIMG_Sampler_gfx10<mimgopc op, string opcode,
                         RegisterClass DataRC, RegisterClass AddrRC,
                         string dns="">
  : MIMG_gfx10<op.GFX10M, (outs DataRC:$vdata), dns> {
  let InOperandList = MIMG_Sampler_OpList_gfx10p<(ins AddrRC:$vaddr0), BaseOpcode.HasD16>.ret;
  let AsmString = MIMG_Sampler_Asm_gfx10p<opcode, "$vdata, $vaddr0", BaseOpcode.HasD16>.ret;
}

class MIMG_Sampler_nsa_gfx10<mimgopc op, string opcode,
                             RegisterClass DataRC, int num_addrs,
                             string dns="">
  : MIMG_nsa_gfx10<op.GFX10M, (outs DataRC:$vdata), num_addrs, dns> {
  let InOperandList = MIMG_Sampler_OpList_gfx10p<AddrIns, BaseOpcode.HasD16>.ret;
  let AsmString = MIMG_Sampler_Asm_gfx10p<opcode, " $vdata, "#AddrAsm, BaseOpcode.HasD16>.ret;
}

class MIMG_Sampler_nortn_gfx10<mimgopc op, string opcode,
                         RegisterClass AddrRC,
                         string dns="">
  : MIMG_gfx10<op.GFX10M, (outs), dns> {
  let InOperandList = MIMG_Sampler_OpList_gfx10p<(ins AddrRC:$vaddr0), BaseOpcode.HasD16>.ret;
  let AsmString = MIMG_Sampler_Asm_gfx10p<opcode, "off, $vaddr0", BaseOpcode.HasD16>.ret;
  // Force vdata to VGPR0 as no result will be returned.
  let vdata = 0;
}

class MIMG_Sampler_nortn_nsa_gfx10<mimgopc op, string opcode,
                         int num_addrs,
                         string dns="">
  : MIMG_nsa_gfx10<op.GFX10M, (outs), num_addrs, dns> {
  let InOperandList = MIMG_Sampler_OpList_gfx10p<AddrIns, BaseOpcode.HasD16>.ret;
  let AsmString = MIMG_Sampler_Asm_gfx10p<opcode, " off, "#AddrAsm, BaseOpcode.HasD16>.ret;
  // Force vdata to VGPR0 as no result will be returned.
  let vdata = 0;
}

class MIMG_Sampler_gfx11<mimgopc op, string opcode,
                         RegisterClass DataRC, RegisterClass AddrRC,
                         string dns="">
  : MIMG_gfx11<op.GFX11, (outs DataRC:$vdata), dns> {
  let InOperandList = MIMG_Sampler_OpList_gfx10p<(ins AddrRC:$vaddr0), BaseOpcode.HasD16>.ret;
  let AsmString = MIMG_Sampler_Asm_gfx10p<opcode, "$vdata, $vaddr0", BaseOpcode.HasD16>.ret;
}

class MIMG_Sampler_nsa_gfx11<mimgopc op, string opcode,
                             RegisterClass DataRC, int num_addrs,
                             RegisterClass LastVAddrSize, string dns="">
  : MIMG_nsa_gfx11<op.GFX11, (outs DataRC:$vdata), num_addrs, dns, [],
                   LastVAddrSize> {
  let InOperandList = MIMG_Sampler_OpList_gfx10p<AddrIns, BaseOpcode.HasD16>.ret;
  let AsmString = MIMG_Sampler_Asm_gfx10p<opcode, " $vdata, "#AddrAsm, BaseOpcode.HasD16>.ret;
}

class MIMG_Sampler_nortn_gfx11<mimgopc op, string opcode,
                                  RegisterClass AddrRC,
                                  string dns="">
  : MIMG_gfx11<op.GFX11, (outs), dns> {
  let InOperandList = MIMG_Sampler_OpList_gfx10p<(ins AddrRC:$vaddr0), BaseOpcode.HasD16>.ret;
  let AsmString = MIMG_Sampler_Asm_gfx10p<opcode, "off, $vaddr0", BaseOpcode.HasD16>.ret;
  let vdata = 0;
}

class MIMG_Sampler_nortn_nsa_gfx11<mimgopc op, string opcode,
                                      int num_addrs,
                                      RegisterClass LastVAddrSize, string dns="">
  : MIMG_nsa_gfx11<op.GFX11, (outs), num_addrs, dns, [], LastVAddrSize> {
  let InOperandList = MIMG_Sampler_OpList_gfx10p<AddrIns, BaseOpcode.HasD16>.ret;
  let AsmString = MIMG_Sampler_Asm_gfx10p<opcode, "off, "#AddrAsm, BaseOpcode.HasD16>.ret;
  let vdata = 0;
}

class MIMGAddrSize<int dw, bit enable_disasm, int AddrDW = dw> {
  int NumWords = dw;

  RegisterClass RegClass = !if(!le(AddrDW, 0), ?,
                           !if(!eq(AddrDW, 1), VGPR_32,
                           !if(!eq(AddrDW, 2), VReg_64,
                           !if(!eq(AddrDW, 3), VReg_96,
                           !if(!eq(AddrDW, 4), VReg_128,
                           !if(!eq(AddrDW, 5), VReg_160,
                           !if(!eq(AddrDW, 6), VReg_192,
                           !if(!eq(AddrDW, 7), VReg_224,
                           !if(!eq(AddrDW, 8), VReg_256,
                           !if(!eq(AddrDW, 9), VReg_288,
                           !if(!eq(AddrDW, 10), VReg_320,
                           !if(!eq(AddrDW, 11), VReg_352,
                           !if(!eq(AddrDW, 12), VReg_384,
                           !if(!le(AddrDW, 16), VReg_512, ?))))))))))))));

  // Whether the instruction variant with this vaddr size should be enabled for
  // the auto-generated disassembler.
  bit Disassemble = enable_disasm;
}

// Returns the MIMGAddrSize with the size of last VAddr for partial NSA
class LastVAddrSize <int dw, int max_idx, bit enable_disasm>
  : MIMGAddrSize<dw, enable_disasm,
                 !if(!gt(dw, max_idx), !sub(dw, max_idx), 0)>;

// Return whether x is in lst.
class isIntInList<int x, list<int> lst> {
  bit ret = !foldl(0, lst, lhs, y, !or(lhs, !eq(x, y)));
}

// Return whether a value inside the range [min, max] (endpoints inclusive)
// is in the given list.
class isRangeInList<int min, int max, list<int> lst> {
  bit ret = !foldl(0, lst, lhs, y, !or(lhs, !and(!le(min, y), !le(y, max))));
}

class MIMGAddrSizes_dw_range<list<int> range> {
  int Min = !head(range);
  int Max = !if(!empty(!tail(range)), Min, !head(!tail(range)));
}

class MIMG_Sampler_AddrSizes<AMDGPUSampleVariant sample, bit isG16,
                             int nsa_max_addr = 5, bit includeNSA1 = 0> {
  // List of all possible numbers of address words, taking all combinations of
  // A16 and image dimension into account (note: no MSAA, since this is for
  // sample/gather ops).
  list<int> AllNumAddrWords =
    !foreach(dw, !if(sample.Gradients,
                     !if(isG16,
                         !if(!eq(sample.LodOrClamp, ""),
                             [2, 3, 4, 5, 6, 7],
                             [2, 3, 4, 5, 6, 7, 8]),
                         !if(!eq(sample.LodOrClamp, ""),
                             [2, 3, 4, 5, 6, 7, 8, 9],
                             [2, 3, 4, 5, 6, 7, 8, 9, 10])),
                     !if(!eq(sample.LodOrClamp, ""),
                         [1, 2, 3],
                         [1, 2, 3, 4])),
             !add(dw, !size(sample.ExtraAddrArgs)));

  // Generate machine instructions based on possible register classes for the
  // required numbers of address words. The disassembler defaults to the
  // smallest register class.
  list<MIMGAddrSize> MachineInstrs =
    !foldl([]<MIMGAddrSize>,
           !foreach(range,
                    // V4 is generated for V3 and V4
                    // V8 is generated for V5 through V8
                    // V16 is generated for V13 through V16
                    [[1],[2],[3],[3,4],[5],[6],[7],[5,8],[9],[10],[11],[12],[13,16]],
                    MIMGAddrSizes_dw_range<range>),
           lhs, dw,
           !if(isRangeInList<dw.Min, dw.Max, AllNumAddrWords>.ret,
               !listconcat(lhs, [MIMGAddrSize<dw.Max, !empty(lhs)>]),
               lhs));

  // For NSA, generate machine instructions for all possible numbers of words
  // except 1 (which is already covered by the non-NSA case).
  // The disassembler defaults to the largest number of arguments among the
  // variants with the same number of NSA words, and custom code then derives
  // the exact variant based on the sample variant and the image dimension.
  list<MIMGAddrSize> NSAInstrs =
    !foldl([]<MIMGAddrSize>, [[12, 11, 10], [9, 8, 7, 6], [5, 4, 3, 2]], prev, nsa_group,
           !listconcat(prev,
                       !foldl([]<MIMGAddrSize>, nsa_group, lhs, dw,
                              !if(isIntInList<dw, AllNumAddrWords>.ret,
                                  !listconcat(lhs, [MIMGAddrSize<dw, !empty(lhs)>]),
                                  lhs))));

  // In NSA format if there is a requirement for more VGPRs than the format
  // supports, then the rest are sequential after the last one. Generate
  // machine instructions for all possible number of words. The disassembler
  // defaults to the largest number of arguments but no larger than max nsa
  // size. List is generated with the register class needed for last vaddr since
  // it is the only one that could have a register other than VGPR32.
  int EnableDisasmNum = !foldl(!head(AllNumAddrWords), !tail(AllNumAddrWords),
                               acc, var, !if(!le(var, nsa_max_addr), var, acc));
  list<int> PossibleVariants =
    !listconcat([12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2], !if(includeNSA1, [1], []));
  list<LastVAddrSize> PartialNSAInstrs =
        !foldl([]<LastVAddrSize>, PossibleVariants, lhs, dw,
               !if(isIntInList<dw, AllNumAddrWords>.ret,
                   !listconcat(lhs, [LastVAddrSize<dw, !sub(nsa_max_addr, 1),
                                                   !eq(dw, EnableDisasmNum)>]),
                   lhs));
}

multiclass MIMG_Sampler_Src_Helper <mimgopc op, string asm,
                                    AMDGPUSampleVariant sample, RegisterClass dst_rc,
                                    bit enableDisasm = 0,
                                    bit ExtendedImageInst = 1, bit isG16 = 0> {
  foreach addr = MIMG_Sampler_AddrSizes<sample, isG16>.MachineInstrs in {
    let VAddrDwords = addr.NumWords in {
      if op.HAS_GFX10M then {
        def _V # addr.NumWords
          : MIMG_Sampler_Helper <op, asm, dst_rc, addr.RegClass,
                                 !if(!and(enableDisasm, addr.Disassemble), "GFX8", "")>;
        if !not(ExtendedImageInst) then
        def _V # addr.NumWords # _gfx90a
          : MIMG_Sampler_gfx90a <op, asm, dst_rc, addr.RegClass,
                                 !if(!and(enableDisasm, addr.Disassemble), "GFX90A", "")>;
        def _V # addr.NumWords # _gfx10
          : MIMG_Sampler_gfx10 <op, asm, dst_rc, addr.RegClass,
                                 !if(!and(enableDisasm, addr.Disassemble), "GFX10", "")>;
      }
      if op.HAS_GFX11 then {
        def _V # addr.NumWords # _gfx11
          : MIMG_Sampler_gfx11 <op, asm, dst_rc, addr.RegClass,
                                 !if(!and(enableDisasm, addr.Disassemble), "GFX11", "")>;
      }
    }
  }

  foreach addr = MIMG_Sampler_AddrSizes<sample, isG16>.NSAInstrs in {
    let VAddrDwords = addr.NumWords in {
      if op.HAS_GFX10M then {
        def _V # addr.NumWords # _nsa_gfx10
          : MIMG_Sampler_nsa_gfx10<op, asm, dst_rc, addr.NumWords,
                                   !if(!and(enableDisasm, addr.Disassemble), "GFX10", "")>;
      }
    }
  }

  foreach addr = MIMG_Sampler_AddrSizes<sample, isG16, 5/*MaxNSASize*/>.PartialNSAInstrs in {
    let VAddrDwords = addr.NumWords in {
      if op.HAS_GFX11 then {
        def _V # addr.NumWords # _nsa_gfx11
          : MIMG_Sampler_nsa_gfx11<op, asm, dst_rc, addr.NumWords, addr.RegClass,
                                   !if(!and(enableDisasm, addr.Disassemble), "GFX11", "")>;
      }
    }
  }

  foreach addr = MIMG_Sampler_AddrSizes<sample, isG16, 4/*MaxNSASize*/, 1>.PartialNSAInstrs in {
    let VAddrDwords = addr.NumWords in {
      if op.HAS_GFX12 then {
        def _V # addr.NumWords # _gfx12
          : VSAMPLE_Sampler_gfx12<op, asm, dst_rc, addr.NumWords, addr.RegClass,
                                  !if(!and(enableDisasm, addr.Disassemble), "GFX12", "")>;
      }
    }
  }
}

class MIMG_Sampler_BaseOpcode<AMDGPUSampleVariant sample>
  : MIMGBaseOpcode {
  let Sampler = 1;
  let NumExtraArgs = !size(sample.ExtraAddrArgs);
  let Gradients = sample.Gradients;
  let LodOrClampOrMip = !ne(sample.LodOrClamp, "");
}

multiclass MIMG_Sampler_NoReturn <mimgopc op, AMDGPUSampleVariant sample, bit wqm = 0, bit isG16, string asm> {
  def "" : MIMG_Sampler_BaseOpcode<sample> {
    let HasD16 = 1;
    let G16 = isG16;
    let NoReturn = 1;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME), WQM = wqm,
      mayLoad = 1, mayStore = 1, VDataDwords = 0 in {
    foreach addr = MIMG_Sampler_AddrSizes<sample, isG16>.MachineInstrs in {
      let VAddrDwords = addr.NumWords in {
        if op.HAS_GFX10M then {
          def _V # addr.NumWords # _gfx10
            : MIMG_Sampler_nortn_gfx10 <op, asm, addr.RegClass>;
        }
        if op.HAS_GFX11 then {
          def _V # addr.NumWords # _gfx11
            : MIMG_Sampler_nortn_gfx11 <op, asm, addr.RegClass>;
        }
      }
    }

    foreach addr = MIMG_Sampler_AddrSizes<sample, isG16>.NSAInstrs in {
      let VAddrDwords = addr.NumWords in {
        if op.HAS_GFX10M then {
          def _V # addr.NumWords # _nsa_gfx10
            : MIMG_Sampler_nortn_nsa_gfx10<op, asm, addr.NumWords>;
        }
      }
    }

    foreach addr = MIMG_Sampler_AddrSizes<sample, isG16, 5/*MaxNSASize*/>.PartialNSAInstrs in {
      let VAddrDwords = addr.NumWords in {
        if op.HAS_GFX11 then {
          def _V # addr.NumWords # _nsa_gfx11
            : MIMG_Sampler_nortn_nsa_gfx11<op, asm, addr.NumWords, addr.RegClass>;
        }
      }
    }

    foreach addr = MIMG_Sampler_AddrSizes<sample, isG16, 4/*MaxNSASize*/, 1>.PartialNSAInstrs in {
      let VAddrDwords = addr.NumWords in {
        if op.HAS_GFX12 then {
          def _V # addr.NumWords # _gfx12
            : VSAMPLE_Sampler_nortn_gfx12<op, asm, addr.NumWords, addr.RegClass>;
        }
      }
    }
  }
}

multiclass MIMG_Sampler <mimgopc op, AMDGPUSampleVariant sample, bit wqm = 0,
                         bit isG16 = 0, bit isGetLod = 0,
                         string asm = "image_sample"#sample.LowerCaseMod#!if(isG16, "_g16", ""),
                         bit ExtendedImageInst = !ne(sample.LowerCaseMod, "")> {
  def "" : MIMG_Sampler_BaseOpcode<sample> {
    let HasD16 = !not(isGetLod);
    let G16 = isG16;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME), WQM = wqm,
      mayLoad = !not(isGetLod) in {
    let VDataDwords = 1 in
    defm _V1 : MIMG_Sampler_Src_Helper<op, asm, sample, VGPR_32, 1, ExtendedImageInst, isG16>;
    let VDataDwords = 2 in
    defm _V2 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_64, 0, ExtendedImageInst, isG16>;
    let VDataDwords = 3 in
    defm _V3 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_96, 0, ExtendedImageInst, isG16>;
    let VDataDwords = 4 in
    defm _V4 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_128, 0, ExtendedImageInst, isG16>;
    let VDataDwords = 5 in
    defm _V5 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_160, 0, ExtendedImageInst, isG16>;
  }

  if !not(isGetLod) then
  defm "_nortn" : MIMG_Sampler_NoReturn <op, sample, wqm, isG16, asm>;
}

multiclass MIMG_Sampler_WQM <mimgopc op, AMDGPUSampleVariant sample>
    : MIMG_Sampler<op, sample, 1>;

multiclass MIMG_Gather <mimgopc op, AMDGPUSampleVariant sample, bit wqm = 0,
                        string asm = "image_gather4"#sample.LowerCaseMod> {
  def "" : MIMG_Sampler_BaseOpcode<sample> {
    let HasD16 = 1;
    let Gather4 = 1;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME), WQM = wqm,
      Gather4 = 1 in {
    let VDataDwords = 2 in
    defm _V2 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_64>; /* for packed D16 only */
    let VDataDwords = 4 in
    defm _V4 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_128, 1>;
    let VDataDwords = 5 in
    defm _V5 : MIMG_Sampler_Src_Helper<op, asm, sample, VReg_160>;
  }
}

multiclass MIMG_Gather_WQM <mimgopc op, AMDGPUSampleVariant sample>
    : MIMG_Gather<op, sample, 1>;

class MIMG_IntersectRay_Helper<bit Is64, bit IsA16> {
  int num_addrs = !if(Is64, !if(IsA16, 9, 12), !if(IsA16, 8, 11));
  RegisterClass RegClass = MIMGAddrSize<num_addrs, 0>.RegClass;
  int VAddrDwords = !srl(RegClass.Size, 5);

  int GFX11PlusNSAAddrs = !if(IsA16, 4, 5);
  RegisterClass node_ptr_type = !if(Is64, VReg_64, VGPR_32);
  list<RegisterClass> GFX11PlusAddrTypes =
          !if(IsA16,
              [node_ptr_type, VGPR_32, VReg_96, VReg_96],
              [node_ptr_type, VGPR_32, VReg_96, VReg_96, VReg_96]);
}

class MIMG_IntersectRay_gfx10<mimgopc op, string opcode, RegisterClass AddrRC>
    : MIMG_gfx10<op.GFX10M, (outs VReg_128:$vdata), "GFX10"> {
  let InOperandList = (ins AddrRC:$vaddr0, SReg_128:$srsrc, A16:$a16);
  let AsmString = opcode#" $vdata, $vaddr0, $srsrc$a16";

  let nsa = 0;
}

class MIMG_IntersectRay_nsa_gfx10<mimgopc op, string opcode, int num_addrs>
    : MIMG_nsa_gfx10<op.GFX10M, (outs VReg_128:$vdata), num_addrs, "GFX10"> {
  let InOperandList = !con(nsah.AddrIns, (ins SReg_128:$srsrc, A16:$a16));
  let AsmString = opcode#" $vdata, "#nsah.AddrAsm#", $srsrc$a16";
}

class MIMG_IntersectRay_gfx11<mimgopc op, string opcode, RegisterClass AddrRC>
    : MIMG_gfx11<op.GFX11, (outs VReg_128:$vdata), "GFX11"> {
  let InOperandList = (ins AddrRC:$vaddr0, SReg_128:$srsrc, A16:$a16);
  let AsmString = opcode#" $vdata, $vaddr0, $srsrc$a16";

  let nsa = 0;
}

class MIMG_IntersectRay_nsa_gfx11<mimgopc op, string opcode, int num_addrs,
                                  list<RegisterClass> addr_types>
    : MIMG_nsa_gfx11<op.GFX11, (outs VReg_128:$vdata), num_addrs, "GFX11",
                     addr_types> {
  let InOperandList = !con(nsah.AddrIns, (ins SReg_128:$srsrc, A16:$a16));
  let AsmString = opcode#" $vdata, "#nsah.AddrAsm#", $srsrc$a16";
}

class VIMAGE_IntersectRay_gfx12<mimgopc op, string opcode, int num_addrs,
                                list<RegisterClass> addr_types>
    : VIMAGE_gfx12<op.GFX12, (outs VReg_128:$vdata),
                   num_addrs, "GFX12", addr_types> {
  let InOperandList = !con(nsah.AddrIns, (ins SReg_128:$rsrc, A16:$a16));
  let AsmString = opcode#" $vdata, "#nsah.AddrAsm#", $rsrc$a16";
}

multiclass MIMG_IntersectRay<mimgopc op, string opcode, bit Is64, bit IsA16> {
  defvar info = MIMG_IntersectRay_Helper<Is64, IsA16>;
  def "" : MIMGBaseOpcode {
    let BVH = 1;
    let A16 = IsA16;
  }
  let dmask = 0xf,
      d16 = 0,
      cpol = 0,
      tfe = 0,
      r128 = 1,
      dim = {0, 0, 0},
      a16 = IsA16,
      d16 = 0,
      BaseOpcode = !cast<MIMGBaseOpcode>(NAME),
      VDataDwords = 4 in {
    let unorm = 1,
        lwe = 0,
        ssamp = 0 in {
      if op.HAS_GFX10M then
      def _sa_gfx10 : MIMG_IntersectRay_gfx10<op, opcode, info.RegClass> {
        let VAddrDwords = info.VAddrDwords;
      }
      if op.HAS_GFX11 then
      def _sa_gfx11 : MIMG_IntersectRay_gfx11<op, opcode, info.RegClass> {
        let VAddrDwords = info.VAddrDwords;
      }
      if op.HAS_GFX10M then
      def _nsa_gfx10 : MIMG_IntersectRay_nsa_gfx10<op, opcode, info.num_addrs> {
        let VAddrDwords = info.num_addrs;
      }
      if op.HAS_GFX11 then
      def _nsa_gfx11 : MIMG_IntersectRay_nsa_gfx11<op, opcode,
                                                  info.GFX11PlusNSAAddrs,
                                                  info.GFX11PlusAddrTypes> {
        let VAddrDwords = info.num_addrs;
      }
    }
    def _gfx12 : VIMAGE_IntersectRay_gfx12<op, opcode, info.GFX11PlusNSAAddrs,
                                           info.GFX11PlusAddrTypes> {
      let VAddrDwords = info.num_addrs;
    }
  }
}

multiclass MIMG_MSAA_Load <mimgopc op, string asm> {
  def "" : MIMGBaseOpcode {
    let HasD16 = 1;
    let Gather4 = 1; /* for appropriate dmask handling */
    let MSAA = 1;
  }

  let BaseOpcode = !cast<MIMGBaseOpcode>(NAME),
    Gather4 = 1, hasPostISelHook = 0, mayLoad = 1 in {
    let VDataDwords = 2 in
    defm _V2 : MIMG_NoSampler_Src_Helper<op, asm, VReg_64, 0, 0, 1>; /* packed D16 */
    let VDataDwords = 3 in
    defm _V3 : MIMG_NoSampler_Src_Helper<op, asm, VReg_96, 0, 0, 1>; /* packed D16 + tfe */
    let VDataDwords = 4 in
    defm _V4 : MIMG_NoSampler_Src_Helper<op, asm, VReg_128, 1, 0, 1>;
    let VDataDwords = 5 in
    defm _V5 : MIMG_NoSampler_Src_Helper<op, asm, VReg_160, 0, 0, 1>;
  }
}

//===----------------------------------------------------------------------===//
// MIMG Instructions
//===----------------------------------------------------------------------===//
let OtherPredicates = [HasImageInsts] in {

defm IMAGE_LOAD                 : MIMG_NoSampler <mimgopc<0x00, 0x00, 0x00>, "image_load", 1>;
defm IMAGE_LOAD_MIP             : MIMG_NoSampler <mimgopc<0x01, 0x01, 0x01>, "image_load_mip", 1, 1>;
defm IMAGE_LOAD_PCK             : MIMG_NoSampler <mimgopc<0x02, 0x02, 0x02>, "image_load_pck", 0>;
defm IMAGE_LOAD_PCK_SGN         : MIMG_NoSampler <mimgopc<0x03, 0x03, 0x03>, "image_load_pck_sgn", 0>;
defm IMAGE_LOAD_MIP_PCK         : MIMG_NoSampler <mimgopc<0x04, 0x04, 0x04>, "image_load_mip_pck", 0, 1>;
defm IMAGE_LOAD_MIP_PCK_SGN     : MIMG_NoSampler <mimgopc<0x05, 0x05, 0x05>, "image_load_mip_pck_sgn", 0, 1>;
defm IMAGE_STORE                : MIMG_Store <mimgopc<0x06, 0x06, 0x08>, "image_store", 1>;
defm IMAGE_STORE_MIP            : MIMG_Store <mimgopc<0x07, 0x07, 0x09>, "image_store_mip", 1, 1>;
defm IMAGE_STORE_PCK            : MIMG_Store <mimgopc<0x08, 0x08, 0x0a>, "image_store_pck", 0>;
defm IMAGE_STORE_MIP_PCK        : MIMG_Store <mimgopc<0x09, 0x09, 0x0b>, "image_store_mip_pck", 0, 1>;

defm IMAGE_GET_RESINFO          : MIMG_NoSampler <mimgopc<0x17, 0x17, 0x0e, 0x0e, 0x0e>, "image_get_resinfo", 0, 1, 1>;

defm IMAGE_ATOMIC_SWAP          : MIMG_Atomic <mimgopc<0x0a, 0x0a, 0x0f, 0x10, 0x0f>, "image_atomic_swap">;
defm IMAGE_ATOMIC_CMPSWAP       : MIMG_Atomic <mimgopc<0x0b, 0x0b, 0x10, 0x11, 0x10>, "image_atomic_cmpswap", 1>;
defm IMAGE_ATOMIC_ADD           : MIMG_Atomic_Renamed <mimgopc<0x0c, 0x0c, 0x11, 0x12, 0x11>, "image_atomic_add", "image_atomic_add_uint">;
defm IMAGE_ATOMIC_SUB           : MIMG_Atomic_Renamed <mimgopc<0x0d, 0x0d, 0x12, 0x13, 0x12>, "image_atomic_sub", "image_atomic_sub_uint">;
defm IMAGE_ATOMIC_RSUB          : MIMG_Atomic <mimgopc<MIMG.NOP, MIMG.NOP, MIMG.NOP, MIMG.NOP, 0x13>, "image_atomic_rsub">;
defm IMAGE_ATOMIC_SMIN          : MIMG_Atomic_Renamed <mimgopc<0x0e, 0x0e, 0x14>, "image_atomic_smin", "image_atomic_min_int">;
defm IMAGE_ATOMIC_UMIN          : MIMG_Atomic_Renamed <mimgopc<0x0f, 0x0f, 0x15>, "image_atomic_umin", "image_atomic_min_uint">;
defm IMAGE_ATOMIC_SMAX          : MIMG_Atomic_Renamed <mimgopc<0x10, 0x10, 0x16>, "image_atomic_smax", "image_atomic_max_int">;
defm IMAGE_ATOMIC_UMAX          : MIMG_Atomic_Renamed <mimgopc<0x11, 0x11, 0x17>, "image_atomic_umax", "image_atomic_max_uint">;
defm IMAGE_ATOMIC_AND           : MIMG_Atomic <mimgopc<0x12, 0x12, 0x18>, "image_atomic_and">;
defm IMAGE_ATOMIC_OR            : MIMG_Atomic <mimgopc<0x13, 0x13, 0x19>, "image_atomic_or">;
defm IMAGE_ATOMIC_XOR           : MIMG_Atomic <mimgopc<0x14, 0x14, 0x1a>, "image_atomic_xor">;
defm IMAGE_ATOMIC_INC           : MIMG_Atomic_Renamed <mimgopc<0x15, 0x15, 0x1b>, "image_atomic_inc", "image_atomic_inc_uint">;
defm IMAGE_ATOMIC_DEC           : MIMG_Atomic_Renamed <mimgopc<0x16, 0x16, 0x1c>, "image_atomic_dec", "image_atomic_dec_uint">;
defm IMAGE_ATOMIC_FCMPSWAP      : MIMG_Atomic <mimgopc<MIMG.NOP, MIMG.NOP, 0x1d, MIMG.NOP>, "image_atomic_fcmpswap", 1, 1>;
defm IMAGE_ATOMIC_FMIN          : MIMG_Atomic <mimgopc<MIMG.NOP, MIMG.NOP, 0x1e, MIMG.NOP>, "image_atomic_fmin", 0, 1>;
defm IMAGE_ATOMIC_FMAX          : MIMG_Atomic <mimgopc<MIMG.NOP, MIMG.NOP, 0x1f, MIMG.NOP>, "image_atomic_fmax", 0, 1>;
defm IMAGE_ATOMIC_PK_ADD_F16    : MIMG_Atomic <mimgopc<0x86, MIMG.NOP, MIMG.NOP, MIMG.NOP, MIMG.NOP>, "image_atomic_pk_add_f16", 0, 1>;
defm IMAGE_ATOMIC_PK_ADD_BF16   : MIMG_Atomic <mimgopc<0x87, MIMG.NOP, MIMG.NOP, MIMG.NOP, MIMG.NOP>, "image_atomic_pk_add_bf16", 0, 1>;
defm IMAGE_ATOMIC_ADD_FLT       : MIMG_Atomic <mimgopc<0x83, MIMG.NOP, MIMG.NOP, MIMG.NOP>, "image_atomic_add_flt", 0, 1>;
defm IMAGE_ATOMIC_MIN_FLT       : MIMG_Atomic <mimgopc<0x84, MIMG.NOP, MIMG.NOP, MIMG.NOP>, "image_atomic_min_num_flt", 0, 1, "image_atomic_min_flt">;
defm IMAGE_ATOMIC_MAX_FLT       : MIMG_Atomic <mimgopc<0x85, MIMG.NOP, MIMG.NOP, MIMG.NOP>, "image_atomic_max_num_flt", 0, 1, "image_atomic_max_flt">;

defm IMAGE_SAMPLE               : MIMG_Sampler_WQM <mimgopc<0x1b, 0x1b, 0x20>, AMDGPUSample>;
let OtherPredicates = [HasImageInsts, HasExtendedImageInsts] in {
defm IMAGE_SAMPLE_CL            : MIMG_Sampler_WQM <mimgopc<0x40, 0x40, 0x21>, AMDGPUSample_cl>;
defm IMAGE_SAMPLE_D             : MIMG_Sampler <mimgopc<0x1c, 0x1c, 0x22>, AMDGPUSample_d>;
defm IMAGE_SAMPLE_D_CL          : MIMG_Sampler <mimgopc<0x41, 0x41, 0x23>, AMDGPUSample_d_cl>;
defm IMAGE_SAMPLE_L             : MIMG_Sampler <mimgopc<0x1d, 0x1d, 0x24>, AMDGPUSample_l>;
defm IMAGE_SAMPLE_B             : MIMG_Sampler_WQM <mimgopc<0x1e, 0x1e, 0x25>, AMDGPUSample_b>;
defm IMAGE_SAMPLE_B_CL          : MIMG_Sampler_WQM <mimgopc<0x42, 0x42, 0x26>, AMDGPUSample_b_cl>;
defm IMAGE_SAMPLE_LZ            : MIMG_Sampler <mimgopc<0x1f, 0x1f, 0x27>, AMDGPUSample_lz>;
defm IMAGE_SAMPLE_C             : MIMG_Sampler_WQM <mimgopc<0x20, 0x20, 0x28>, AMDGPUSample_c>;
defm IMAGE_SAMPLE_C_CL          : MIMG_Sampler_WQM <mimgopc<0x43, 0x43, 0x29>, AMDGPUSample_c_cl>;
defm IMAGE_SAMPLE_C_D           : MIMG_Sampler <mimgopc<0x21, 0x21, 0x2a>, AMDGPUSample_c_d>;
defm IMAGE_SAMPLE_C_D_CL        : MIMG_Sampler <mimgopc<0x44, 0x44, 0x2b>, AMDGPUSample_c_d_cl>;
defm IMAGE_SAMPLE_C_L           : MIMG_Sampler <mimgopc<0x22, 0x22, 0x2c>, AMDGPUSample_c_l>;
defm IMAGE_SAMPLE_C_B           : MIMG_Sampler_WQM <mimgopc<0x23, 0x23, 0x2d>, AMDGPUSample_c_b>;
defm IMAGE_SAMPLE_C_B_CL        : MIMG_Sampler_WQM <mimgopc<0x45, 0x45, 0x2e>, AMDGPUSample_c_b_cl>;
defm IMAGE_SAMPLE_C_LZ          : MIMG_Sampler <mimgopc<0x24, 0x24, 0x2f>, AMDGPUSample_c_lz>;
defm IMAGE_SAMPLE_O             : MIMG_Sampler_WQM <mimgopc<0x25, 0x25, 0x30>, AMDGPUSample_o>;
defm IMAGE_SAMPLE_CL_O          : MIMG_Sampler_WQM <mimgopc<0x46, 0x46, 0x31>, AMDGPUSample_cl_o>;
defm IMAGE_SAMPLE_D_O           : MIMG_Sampler <mimgopc<0x26, 0x26, 0x32>, AMDGPUSample_d_o>;
defm IMAGE_SAMPLE_D_CL_O        : MIMG_Sampler <mimgopc<0x47, 0x47, 0x33>, AMDGPUSample_d_cl_o>;
defm IMAGE_SAMPLE_L_O           : MIMG_Sampler <mimgopc<0x27, 0x27, 0x34>, AMDGPUSample_l_o>;
defm IMAGE_SAMPLE_B_O           : MIMG_Sampler_WQM <mimgopc<0x28, 0x28, 0x35>, AMDGPUSample_b_o>;
defm IMAGE_SAMPLE_B_CL_O        : MIMG_Sampler_WQM <mimgopc<0x48, 0x48, 0x36>, AMDGPUSample_b_cl_o>;
defm IMAGE_SAMPLE_LZ_O          : MIMG_Sampler <mimgopc<0x29, 0x29, 0x37>, AMDGPUSample_lz_o>;
defm IMAGE_SAMPLE_C_O           : MIMG_Sampler_WQM <mimgopc<0x2a, 0x2a, 0x38>, AMDGPUSample_c_o>;
defm IMAGE_SAMPLE_C_CL_O        : MIMG_Sampler_WQM <mimgopc<0x49, 0x49, 0x39>, AMDGPUSample_c_cl_o>;
defm IMAGE_SAMPLE_C_D_O         : MIMG_Sampler <mimgopc<0x2b, 0x2b, 0x3a>, AMDGPUSample_c_d_o>;
defm IMAGE_SAMPLE_C_D_CL_O      : MIMG_Sampler <mimgopc<0x4a, 0x4a, 0x3b>, AMDGPUSample_c_d_cl_o>;
defm IMAGE_SAMPLE_C_L_O         : MIMG_Sampler <mimgopc<0x2c, 0x2c, 0x3c>, AMDGPUSample_c_l_o>;
defm IMAGE_SAMPLE_C_B_CL_O      : MIMG_Sampler_WQM <mimgopc<0x4b, 0x4b, 0x3e>, AMDGPUSample_c_b_cl_o>;
defm IMAGE_SAMPLE_C_B_O         : MIMG_Sampler_WQM <mimgopc<0x2d, 0x2d, 0x3d>, AMDGPUSample_c_b_o>;
defm IMAGE_SAMPLE_C_LZ_O        : MIMG_Sampler <mimgopc<0x2e, 0x2e, 0x3f>, AMDGPUSample_c_lz_o>;
defm IMAGE_GATHER4              : MIMG_Gather_WQM <mimgopc<0x2f, 0x2f, 0x40>, AMDGPUSample>;
defm IMAGE_GATHER4_CL           : MIMG_Gather_WQM <mimgopc<0x60, 0x60, 0x41>, AMDGPUSample_cl>;
defm IMAGE_GATHER4_L            : MIMG_Gather <mimgopc<0x30, 0x30, 0x44>, AMDGPUSample_l>;
defm IMAGE_GATHER4_B            : MIMG_Gather_WQM <mimgopc<0x31, 0x31, 0x45>, AMDGPUSample_b>;
defm IMAGE_GATHER4_B_CL         : MIMG_Gather_WQM <mimgopc<0x61, 0x61, 0x46>, AMDGPUSample_b_cl>;
defm IMAGE_GATHER4_LZ           : MIMG_Gather <mimgopc<0x32, 0x32, 0x47>, AMDGPUSample_lz>;
defm IMAGE_GATHER4_C            : MIMG_Gather_WQM <mimgopc<0x33, 0x33, 0x48>, AMDGPUSample_c>;
defm IMAGE_GATHER4_C_CL         : MIMG_Gather_WQM <mimgopc<0x62, 0x62, 0x49>, AMDGPUSample_c_cl>;
defm IMAGE_GATHER4_C_L          : MIMG_Gather <mimgopc<0x63, 0x63, 0x4c>, AMDGPUSample_c_l>;
defm IMAGE_GATHER4_C_B          : MIMG_Gather_WQM <mimgopc<0x64, 0x64, 0x4d>, AMDGPUSample_c_b>;
defm IMAGE_GATHER4_C_B_CL       : MIMG_Gather_WQM <mimgopc<0x65, 0x65, 0x4e>, AMDGPUSample_c_b_cl>;
defm IMAGE_GATHER4_C_LZ         : MIMG_Gather <mimgopc<0x34, 0x34, 0x4f>, AMDGPUSample_c_lz>;
defm IMAGE_GATHER4_O            : MIMG_Gather_WQM <mimgopc<0x35, 0x35, 0x50>, AMDGPUSample_o>;
defm IMAGE_GATHER4_CL_O         : MIMG_Gather_WQM <mimgopc<MIMG.NOP, MIMG.NOP, 0x51>, AMDGPUSample_cl_o>;
defm IMAGE_GATHER4_L_O          : MIMG_Gather <mimgopc<MIMG.NOP, MIMG.NOP, 0x54>, AMDGPUSample_l_o>;
defm IMAGE_GATHER4_B_O          : MIMG_Gather_WQM <mimgopc<MIMG.NOP, MIMG.NOP, 0x55>, AMDGPUSample_b_o>;
defm IMAGE_GATHER4_B_CL_O       : MIMG_Gather <mimgopc<MIMG.NOP, MIMG.NOP, 0x56>, AMDGPUSample_b_cl_o>;
defm IMAGE_GATHER4_LZ_O         : MIMG_Gather <mimgopc<0x36, 0x36, 0x57>, AMDGPUSample_lz_o>;
defm IMAGE_GATHER4_C_O          : MIMG_Gather_WQM <mimgopc<MIMG.NOP, MIMG.NOP, 0x58>, AMDGPUSample_c_o>;
defm IMAGE_GATHER4_C_CL_O       : MIMG_Gather_WQM <mimgopc<MIMG.NOP, MIMG.NOP, 0x59>, AMDGPUSample_c_cl_o>;
defm IMAGE_GATHER4_C_L_O        : MIMG_Gather <mimgopc<MIMG.NOP, MIMG.NOP, 0x5c>, AMDGPUSample_c_l_o>;
defm IMAGE_GATHER4_C_B_O        : MIMG_Gather_WQM <mimgopc<MIMG.NOP, MIMG.NOP, 0x5d>, AMDGPUSample_c_b_o>;
defm IMAGE_GATHER4_C_B_CL_O     : MIMG_Gather_WQM <mimgopc<MIMG.NOP, MIMG.NOP, 0x5e>, AMDGPUSample_c_b_cl_o>;
defm IMAGE_GATHER4_C_LZ_O       : MIMG_Gather <mimgopc<0x37, 0x37, 0x5f>, AMDGPUSample_c_lz_o>;

let OtherPredicates = [HasImageInsts, HasExtendedImageInsts, isGFX9Plus] in
defm IMAGE_GATHER4H             : MIMG_Gather <mimgopc<0x90, 0x90, 0x61, 0x42>, AMDGPUSample, 1, "image_gather4h">;

defm IMAGE_GET_LOD              : MIMG_Sampler <mimgopc<0x38, 0x38, 0x60>, AMDGPUSample, 1, 0, 1, "image_get_lod">;

defm IMAGE_SAMPLE_CD            : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x68>, AMDGPUSample_cd>;
defm IMAGE_SAMPLE_CD_CL         : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x69>, AMDGPUSample_cd_cl>;
defm IMAGE_SAMPLE_C_CD          : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x6a>, AMDGPUSample_c_cd>;
defm IMAGE_SAMPLE_C_CD_CL       : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x6b>, AMDGPUSample_c_cd_cl>;
defm IMAGE_SAMPLE_CD_O          : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x6c>, AMDGPUSample_cd_o>;
defm IMAGE_SAMPLE_CD_CL_O       : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x6d>, AMDGPUSample_cd_cl_o>;
defm IMAGE_SAMPLE_C_CD_O        : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x6e>, AMDGPUSample_c_cd_o>;
defm IMAGE_SAMPLE_C_CD_CL_O     : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x6f>, AMDGPUSample_c_cd_cl_o>;
} // End OtherPredicates = [HasImageInsts, HasExtendedImageInsts]

let OtherPredicates = [HasImageInsts, HasExtendedImageInsts, HasG16] in {
defm IMAGE_SAMPLE_D_G16         : MIMG_Sampler <mimgopc<0x39, 0x39, 0xa2>, AMDGPUSample_d, 0, 1>;
defm IMAGE_SAMPLE_D_CL_G16      : MIMG_Sampler <mimgopc<0x5f, 0x5f, 0xa3>, AMDGPUSample_d_cl, 0, 1>;
defm IMAGE_SAMPLE_C_D_G16       : MIMG_Sampler <mimgopc<0x3a, 0x3a, 0xaa>, AMDGPUSample_c_d, 0, 1>;
defm IMAGE_SAMPLE_C_D_CL_G16    : MIMG_Sampler <mimgopc<0x54, 0x54, 0xab>, AMDGPUSample_c_d_cl, 0, 1>;
defm IMAGE_SAMPLE_D_O_G16       : MIMG_Sampler <mimgopc<0x3b, 0x3b, 0xb2>, AMDGPUSample_d_o, 0, 1>;
defm IMAGE_SAMPLE_D_CL_O_G16    : MIMG_Sampler <mimgopc<0x55, 0x55, 0xb3>, AMDGPUSample_d_cl_o, 0, 1>;
defm IMAGE_SAMPLE_C_D_O_G16     : MIMG_Sampler <mimgopc<0x3c, 0x3c, 0xba>, AMDGPUSample_c_d_o, 0, 1>;
defm IMAGE_SAMPLE_C_D_CL_O_G16  : MIMG_Sampler <mimgopc<0x56, 0x56, 0xbb>, AMDGPUSample_c_d_cl_o, 0, 1>;
defm IMAGE_SAMPLE_CD_G16        : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0xe8>, AMDGPUSample_cd, 0, 1>;
defm IMAGE_SAMPLE_CD_CL_G16     : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0xe9>, AMDGPUSample_cd_cl, 0, 1>;
defm IMAGE_SAMPLE_C_CD_G16      : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0xea>, AMDGPUSample_c_cd, 0, 1>;
defm IMAGE_SAMPLE_C_CD_CL_G16   : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0xeb>, AMDGPUSample_c_cd_cl, 0, 1>;
defm IMAGE_SAMPLE_CD_O_G16      : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0xec>, AMDGPUSample_cd_o, 0, 1>;
defm IMAGE_SAMPLE_CD_CL_O_G16   : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0xed>, AMDGPUSample_cd_cl_o, 0, 1>;
defm IMAGE_SAMPLE_C_CD_O_G16    : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0xee>, AMDGPUSample_c_cd_o, 0, 1>;
defm IMAGE_SAMPLE_C_CD_CL_O_G16 : MIMG_Sampler <mimgopc<MIMG.NOP, MIMG.NOP, 0xef>, AMDGPUSample_c_cd_cl_o, 0, 1>;
} // End OtherPredicates = [HasImageInsts, HasExtendedImageInsts, HasG16]

//def IMAGE_RSRC256 : MIMG_NoPattern_RSRC256 <"image_rsrc256", mimgopc<0x7e>>;
//def IMAGE_SAMPLER : MIMG_NoPattern_ <"image_sampler", mimgopc<0x7f>>;

let OtherPredicates = [HasImageInsts, HasGFX10_AEncoding, isGFX10Only] in
defm IMAGE_MSAA_LOAD_X : MIMG_NoSampler <mimgopc<MIMG.NOP, MIMG.NOP, 0x80>, "image_msaa_load", 1, 0, 0, 1>;

let OtherPredicates = [HasImageInsts, HasGFX10_AEncoding] in {
defm IMAGE_MSAA_LOAD : MIMG_MSAA_Load <mimgopc<0x18, 0x18, MIMG.NOP>, "image_msaa_load">;

defm IMAGE_BVH_INTERSECT_RAY       : MIMG_IntersectRay<mimgopc<0x19, 0x19, 0xe6>, "image_bvh_intersect_ray", 0, 0>;
defm IMAGE_BVH_INTERSECT_RAY_a16   : MIMG_IntersectRay<mimgopc<0x19, 0x19, 0xe6>, "image_bvh_intersect_ray", 0, 1>;
defm IMAGE_BVH64_INTERSECT_RAY     : MIMG_IntersectRay<mimgopc<0x1a, 0x1a, 0xe7>, "image_bvh64_intersect_ray", 1, 0>;
defm IMAGE_BVH64_INTERSECT_RAY_a16 : MIMG_IntersectRay<mimgopc<0x1a, 0x1a, 0xe7>, "image_bvh64_intersect_ray", 1, 1>;
} // End OtherPredicates = [HasImageInsts, HasGFX10_AEncoding]

} // End let OtherPredicates = [HasImageInsts]

/********** ========================================= **********/
/********** Table of dimension-aware image intrinsics **********/
/********** ========================================= **********/

class ImageDimIntrinsicInfo<AMDGPUImageDimIntrinsic I> {
  Intrinsic Intr = I;
  MIMGBaseOpcode BaseOpcode = !cast<MIMGBaseOpcode>(!strconcat("IMAGE_", I.P.OpMod));
  AMDGPUDimProps Dim = I.P.Dim;
  AMDGPUImageDimIntrinsicEval DimEval = AMDGPUImageDimIntrinsicEval<I.P>;

  bits<8> NumOffsetArgs = DimEval.NumOffsetArgs;
  bits<8> NumBiasArgs = DimEval.NumBiasArgs;
  bits<8> NumZCompareArgs = DimEval.NumZCompareArgs;
  bits<8> NumGradients = DimEval.NumGradientArgs;
  bits<8> NumDmask = DimEval.NumDmaskArgs;
  bits<8> NumData = DimEval.NumDataArgs;
  bits<8> NumVAddrs = DimEval.NumVAddrArgs;
  bits<8> NumArgs = !add(DimEval.CachePolicyArgIndex, 1);

  bits<8> DMaskIndex = DimEval.DmaskArgIndex;
  bits<8> VAddrStart = DimEval.VAddrArgIndex;
  bits<8> OffsetIndex = DimEval.OffsetArgIndex;
  bits<8> BiasIndex = DimEval.BiasArgIndex;
  bits<8> ZCompareIndex = DimEval.ZCompareArgIndex;
  bits<8> GradientStart = DimEval.GradientArgIndex;
  bits<8> CoordStart = DimEval.CoordArgIndex;
  bits<8> LodIndex = DimEval.LodArgIndex;
  bits<8> MipIndex = DimEval.MipArgIndex;
  bits<8> VAddrEnd = !add(DimEval.VAddrArgIndex, DimEval.NumVAddrArgs);
  bits<8> RsrcIndex = DimEval.RsrcArgIndex;
  bits<8> SampIndex = DimEval.SampArgIndex;
  bits<8> UnormIndex = DimEval.UnormArgIndex;
  bits<8> TexFailCtrlIndex = DimEval.TexFailCtrlArgIndex;
  bits<8> CachePolicyIndex = DimEval.CachePolicyArgIndex;

  bits<8> BiasTyArg = !add(I.P.NumRetAndDataAnyTypes,
    !if(!eq(NumOffsetArgs, 0), 0, I.P.ExtraAddrArgs[0].Type.isAny));
  bits<8> GradientTyArg = !add(I.P.NumRetAndDataAnyTypes,
    !foldl(0, I.P.ExtraAddrArgs, cnt, arg, !add(cnt, arg.Type.isAny)));
  bits<8> CoordTyArg = !add(GradientTyArg, !if(I.P.Gradients, 1, 0));
}

def ImageDimIntrinsicTable : GenericTable {
  let FilterClass = "ImageDimIntrinsicInfo";
  let Fields = ["Intr", "BaseOpcode", "Dim", "NumOffsetArgs", "NumBiasArgs", "NumZCompareArgs", "NumGradients", "NumDmask", "NumData", "NumVAddrs", "NumArgs",
    "DMaskIndex", "VAddrStart", "OffsetIndex", "BiasIndex", "ZCompareIndex", "GradientStart", "CoordStart", "LodIndex", "MipIndex", "VAddrEnd",
    "RsrcIndex", "SampIndex", "UnormIndex", "TexFailCtrlIndex", "CachePolicyIndex",
    "BiasTyArg", "GradientTyArg", "CoordTyArg"];
  string TypeOf_BaseOpcode = "MIMGBaseOpcode";
  string TypeOf_Dim = "MIMGDim";

  let PrimaryKey = ["Intr"];
  let PrimaryKeyName = "getImageDimIntrinsicInfo";
  let PrimaryKeyEarlyOut = 1;
}

def getImageDimIntrinsicByBaseOpcode : SearchIndex {
  let Table = ImageDimIntrinsicTable;
  let Key = ["BaseOpcode", "Dim"];
}

foreach intr = !listconcat(AMDGPUImageDimIntrinsics,
                           AMDGPUImageDimAtomicIntrinsics) in {
  def : ImageDimIntrinsicInfo<intr>;
}

// L to LZ Optimization Mapping
def : MIMGLZMapping<IMAGE_SAMPLE_L, IMAGE_SAMPLE_LZ>;
def : MIMGLZMapping<IMAGE_SAMPLE_C_L, IMAGE_SAMPLE_C_LZ>;
def : MIMGLZMapping<IMAGE_SAMPLE_L_O, IMAGE_SAMPLE_LZ_O>;
def : MIMGLZMapping<IMAGE_SAMPLE_C_L_O, IMAGE_SAMPLE_C_LZ_O>;
def : MIMGLZMapping<IMAGE_GATHER4_L, IMAGE_GATHER4_LZ>;
def : MIMGLZMapping<IMAGE_GATHER4_C_L, IMAGE_GATHER4_C_LZ>;
def : MIMGLZMapping<IMAGE_GATHER4_L_O, IMAGE_GATHER4_LZ_O>;
def : MIMGLZMapping<IMAGE_GATHER4_C_L_O, IMAGE_GATHER4_C_LZ_O>;
def : MIMGLZMapping<IMAGE_SAMPLE_L_nortn, IMAGE_SAMPLE_LZ_nortn>;
def : MIMGLZMapping<IMAGE_SAMPLE_C_L_nortn, IMAGE_SAMPLE_C_LZ_nortn>;
def : MIMGLZMapping<IMAGE_SAMPLE_L_O_nortn, IMAGE_SAMPLE_LZ_O_nortn>;
def : MIMGLZMapping<IMAGE_SAMPLE_C_L_O_nortn, IMAGE_SAMPLE_C_LZ_O_nortn>;

// MIP to NONMIP Optimization Mapping
def : MIMGMIPMapping<IMAGE_LOAD_MIP, IMAGE_LOAD>;
def : MIMGMIPMapping<IMAGE_STORE_MIP, IMAGE_STORE>;

// Bias to NoBias Optimization Mapping
def : MIMGBiasMapping<IMAGE_SAMPLE_B, IMAGE_SAMPLE>;
def : MIMGBiasMapping<IMAGE_SAMPLE_B_CL, IMAGE_SAMPLE_CL>;
def : MIMGBiasMapping<IMAGE_SAMPLE_C_B, IMAGE_SAMPLE_C>;
def : MIMGBiasMapping<IMAGE_SAMPLE_C_B_CL, IMAGE_SAMPLE_C_CL>;
def : MIMGBiasMapping<IMAGE_SAMPLE_B_O, IMAGE_SAMPLE_O>;
def : MIMGBiasMapping<IMAGE_SAMPLE_B_CL_O, IMAGE_SAMPLE_CL_O>;
def : MIMGBiasMapping<IMAGE_SAMPLE_C_B_O, IMAGE_SAMPLE_C_O>;
def : MIMGBiasMapping<IMAGE_SAMPLE_C_B_CL_O, IMAGE_SAMPLE_C_CL_O>;
def : MIMGBiasMapping<IMAGE_GATHER4_B, IMAGE_GATHER4>;
def : MIMGBiasMapping<IMAGE_GATHER4_B_CL, IMAGE_GATHER4_CL>;
def : MIMGBiasMapping<IMAGE_GATHER4_C_B, IMAGE_GATHER4_C>;
def : MIMGBiasMapping<IMAGE_GATHER4_C_B_CL, IMAGE_GATHER4_C_CL>;
def : MIMGBiasMapping<IMAGE_GATHER4_B_O, IMAGE_GATHER4_O>;
def : MIMGBiasMapping<IMAGE_GATHER4_B_CL_O, IMAGE_GATHER4_CL_O>;
def : MIMGBiasMapping<IMAGE_GATHER4_C_B_O, IMAGE_GATHER4_C_O>;
def : MIMGBiasMapping<IMAGE_GATHER4_C_B_CL_O, IMAGE_GATHER4_C_CL_O>;
def : MIMGBiasMapping<IMAGE_SAMPLE_B_nortn, IMAGE_SAMPLE_nortn>;
def : MIMGBiasMapping<IMAGE_SAMPLE_B_CL_nortn, IMAGE_SAMPLE_CL_nortn>;
def : MIMGBiasMapping<IMAGE_SAMPLE_C_B_nortn, IMAGE_SAMPLE_C_nortn>;
def : MIMGBiasMapping<IMAGE_SAMPLE_C_B_CL_nortn, IMAGE_SAMPLE_C_CL_nortn>;
def : MIMGBiasMapping<IMAGE_SAMPLE_B_O_nortn, IMAGE_SAMPLE_O_nortn>;
def : MIMGBiasMapping<IMAGE_SAMPLE_B_CL_O_nortn, IMAGE_SAMPLE_CL_O_nortn>;
def : MIMGBiasMapping<IMAGE_SAMPLE_C_B_O_nortn, IMAGE_SAMPLE_C_O_nortn>;
def : MIMGBiasMapping<IMAGE_SAMPLE_C_B_CL_O_nortn, IMAGE_SAMPLE_C_CL_O_nortn>;

// Offset to NoOffset Optimization Mapping
def : MIMGOffsetMapping<IMAGE_SAMPLE_O, IMAGE_SAMPLE>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CL_O, IMAGE_SAMPLE_CL>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_D_O, IMAGE_SAMPLE_D>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_D_CL_O, IMAGE_SAMPLE_D_CL>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_D_O_G16, IMAGE_SAMPLE_D_G16>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_D_CL_O_G16, IMAGE_SAMPLE_D_CL_G16>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_L_O, IMAGE_SAMPLE_L>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_B_O, IMAGE_SAMPLE_B>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_B_CL_O, IMAGE_SAMPLE_B_CL>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_LZ_O, IMAGE_SAMPLE_LZ>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_O, IMAGE_SAMPLE_C>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CL_O, IMAGE_SAMPLE_C_CL>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_D_O, IMAGE_SAMPLE_C_D>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_D_CL_O, IMAGE_SAMPLE_C_D_CL>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_D_O_G16, IMAGE_SAMPLE_C_D_G16>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_D_CL_O_G16, IMAGE_SAMPLE_C_D_CL_G16>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_L_O, IMAGE_SAMPLE_C_L>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_B_CL_O, IMAGE_SAMPLE_C_B_CL>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_B_O, IMAGE_SAMPLE_C_B>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_LZ_O, IMAGE_SAMPLE_C_LZ>;
def : MIMGOffsetMapping<IMAGE_GATHER4_O, IMAGE_GATHER4>;
def : MIMGOffsetMapping<IMAGE_GATHER4_CL_O, IMAGE_GATHER4_CL>;
def : MIMGOffsetMapping<IMAGE_GATHER4_L_O, IMAGE_GATHER4_L>;
def : MIMGOffsetMapping<IMAGE_GATHER4_B_O, IMAGE_GATHER4_B>;
def : MIMGOffsetMapping<IMAGE_GATHER4_B_CL_O, IMAGE_GATHER4_B_CL>;
def : MIMGOffsetMapping<IMAGE_GATHER4_LZ_O, IMAGE_GATHER4_LZ>;
def : MIMGOffsetMapping<IMAGE_GATHER4_C_O, IMAGE_GATHER4_C>;
def : MIMGOffsetMapping<IMAGE_GATHER4_C_CL_O, IMAGE_GATHER4_C_CL>;
def : MIMGOffsetMapping<IMAGE_GATHER4_C_L_O, IMAGE_GATHER4_C_L>;
def : MIMGOffsetMapping<IMAGE_GATHER4_C_B_O, IMAGE_GATHER4_C_B>;
def : MIMGOffsetMapping<IMAGE_GATHER4_C_B_CL_O, IMAGE_GATHER4_C_B_CL>;
def : MIMGOffsetMapping<IMAGE_GATHER4_C_LZ_O, IMAGE_GATHER4_C_LZ>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CD_O, IMAGE_SAMPLE_CD>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CD_CL_O, IMAGE_SAMPLE_CD_CL>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CD_O, IMAGE_SAMPLE_C_CD>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CD_CL_O, IMAGE_SAMPLE_C_CD_CL>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CD_O_G16, IMAGE_SAMPLE_CD_G16>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CD_CL_O_G16, IMAGE_SAMPLE_CD_CL_G16>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CD_O_G16, IMAGE_SAMPLE_C_CD_G16>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CD_CL_O_G16, IMAGE_SAMPLE_C_CD_CL_G16>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_O_nortn, IMAGE_SAMPLE_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CL_O_nortn, IMAGE_SAMPLE_CL_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_D_O_nortn, IMAGE_SAMPLE_D_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_D_CL_O_nortn, IMAGE_SAMPLE_D_CL_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_D_O_G16_nortn, IMAGE_SAMPLE_D_G16_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_D_CL_O_G16_nortn, IMAGE_SAMPLE_D_CL_G16_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_L_O_nortn, IMAGE_SAMPLE_L_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_B_O_nortn, IMAGE_SAMPLE_B_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_B_CL_O_nortn, IMAGE_SAMPLE_B_CL_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_LZ_O_nortn, IMAGE_SAMPLE_LZ_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_O_nortn, IMAGE_SAMPLE_C_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CL_O_nortn, IMAGE_SAMPLE_C_CL_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_D_O_nortn, IMAGE_SAMPLE_C_D_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_D_CL_O_nortn, IMAGE_SAMPLE_C_D_CL_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_D_O_G16_nortn, IMAGE_SAMPLE_C_D_G16_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_D_CL_O_G16_nortn, IMAGE_SAMPLE_C_D_CL_G16_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_L_O_nortn, IMAGE_SAMPLE_C_L_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_B_CL_O_nortn, IMAGE_SAMPLE_C_B_CL_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_B_O_nortn, IMAGE_SAMPLE_C_B_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_LZ_O_nortn, IMAGE_SAMPLE_C_LZ_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CD_O_nortn, IMAGE_SAMPLE_CD>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CD_CL_O_nortn, IMAGE_SAMPLE_CD_CL_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CD_O_nortn, IMAGE_SAMPLE_C_CD_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CD_CL_O_nortn, IMAGE_SAMPLE_C_CD_CL_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CD_O_G16_nortn, IMAGE_SAMPLE_CD_G16_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_CD_CL_O_G16_nortn, IMAGE_SAMPLE_CD_CL_G16_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CD_O_G16_nortn, IMAGE_SAMPLE_C_CD_G16_nortn>;
def : MIMGOffsetMapping<IMAGE_SAMPLE_C_CD_CL_O_G16_nortn, IMAGE_SAMPLE_C_CD_CL_G16_nortn>;

// G to G16 Optimization Mapping
def : MIMGG16Mapping<IMAGE_SAMPLE_D, IMAGE_SAMPLE_D_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_D_CL, IMAGE_SAMPLE_D_CL_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_D, IMAGE_SAMPLE_C_D_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_D_CL, IMAGE_SAMPLE_C_D_CL_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_D_O, IMAGE_SAMPLE_D_O_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_D_CL_O, IMAGE_SAMPLE_D_CL_O_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_D_O, IMAGE_SAMPLE_C_D_O_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_D_CL_O, IMAGE_SAMPLE_C_D_CL_O_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_CD, IMAGE_SAMPLE_CD_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_CD_CL, IMAGE_SAMPLE_CD_CL_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_CD, IMAGE_SAMPLE_C_CD_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_CD_CL, IMAGE_SAMPLE_C_CD_CL_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_CD_O, IMAGE_SAMPLE_CD_O_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_CD_CL_O, IMAGE_SAMPLE_CD_CL_O_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_CD_O, IMAGE_SAMPLE_C_CD_O_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_CD_CL_O, IMAGE_SAMPLE_C_CD_CL_O_G16>;
def : MIMGG16Mapping<IMAGE_SAMPLE_D_nortn, IMAGE_SAMPLE_D_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_D_CL_nortn, IMAGE_SAMPLE_D_CL_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_D_nortn, IMAGE_SAMPLE_C_D_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_D_CL_nortn, IMAGE_SAMPLE_C_D_CL_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_D_O_nortn, IMAGE_SAMPLE_D_O_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_D_CL_O_nortn, IMAGE_SAMPLE_D_CL_O_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_D_O_nortn, IMAGE_SAMPLE_C_D_O_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_D_CL_O_nortn, IMAGE_SAMPLE_C_D_CL_O_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_CD_nortn, IMAGE_SAMPLE_CD_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_CD_CL_nortn, IMAGE_SAMPLE_CD_CL_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_CD_nortn, IMAGE_SAMPLE_C_CD_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_CD_CL_nortn, IMAGE_SAMPLE_C_CD_CL_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_CD_O_nortn, IMAGE_SAMPLE_CD_O_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_CD_CL_O_nortn, IMAGE_SAMPLE_CD_CL_O_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_CD_O_nortn, IMAGE_SAMPLE_C_CD_O_G16_nortn>;
def : MIMGG16Mapping<IMAGE_SAMPLE_C_CD_CL_O_nortn, IMAGE_SAMPLE_C_CD_CL_O_G16_nortn>;
