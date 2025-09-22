//==- SystemZInstrDFP.td - Floating-point SystemZ instructions -*- tblgen-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The instructions in this file implement SystemZ decimal floating-point
// arithmetic.  These instructions are inot currently used for code generation,
// are provided for use with the assembler and disassembler only.  If LLVM
// ever supports decimal floating-point types (_Decimal64 etc.), they can
// also be used for code generation for those types.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

// Load and test.
let Uses = [FPC], Defs = [CC] in {
  def LTDTR : UnaryRRE<"ltdtr", 0xB3D6, null_frag, FP64,  FP64>;
  def LTXTR : UnaryRRE<"ltxtr", 0xB3DE, null_frag, FP128, FP128>;
}


//===----------------------------------------------------------------------===//
// Conversion instructions
//===----------------------------------------------------------------------===//

// Convert floating-point values to narrower representations.  The destination
// of LDXTR is a 128-bit value, but only the first register of the pair is used.
let Uses = [FPC] in {
  def LEDTR : TernaryRRFe<"ledtr", 0xB3D5, FP32,  FP64>;
  def LDXTR : TernaryRRFe<"ldxtr", 0xB3DD, FP128, FP128>;
}

// Extend floating-point values to wider representations.
let Uses = [FPC] in {
  def LDETR : BinaryRRFd<"ldetr", 0xB3D4, FP64,  FP32>;
  def LXDTR : BinaryRRFd<"lxdtr", 0xB3DC, FP128, FP64>;
}

// Convert a signed integer value to a floating-point one.
let Uses = [FPC] in {
  def CDGTR : UnaryRRE<"cdgtr", 0xB3F1, null_frag, FP64,  GR64>;
  def CXGTR : UnaryRRE<"cxgtr", 0xB3F9, null_frag, FP128, GR64>;
  let Predicates = [FeatureFPExtension] in {
    def CDGTRA : TernaryRRFe<"cdgtra", 0xB3F1, FP64,  GR64>;
    def CXGTRA : TernaryRRFe<"cxgtra", 0xB3F9, FP128, GR64>;
    def CDFTR : TernaryRRFe<"cdftr", 0xB951, FP64,  GR32>;
    def CXFTR : TernaryRRFe<"cxftr", 0xB959, FP128, GR32>;
  }
}

// Convert an unsigned integer value to a floating-point one.
let Uses = [FPC], Predicates = [FeatureFPExtension] in {
  def CDLGTR : TernaryRRFe<"cdlgtr", 0xB952, FP64,  GR64>;
  def CXLGTR : TernaryRRFe<"cxlgtr", 0xB95A, FP128, GR64>;
  def CDLFTR : TernaryRRFe<"cdlftr", 0xB953, FP64,  GR32>;
  def CXLFTR : TernaryRRFe<"cxlftr", 0xB95B, FP128, GR32>;
}

// Convert a floating-point value to a signed integer value.
let Uses = [FPC], Defs = [CC] in {
  def CGDTR : BinaryRRFe<"cgdtr", 0xB3E1, GR64, FP64>;
  def CGXTR : BinaryRRFe<"cgxtr", 0xB3E9, GR64, FP128>;
  let Predicates = [FeatureFPExtension] in {
    def CGDTRA : TernaryRRFe<"cgdtra", 0xB3E1, GR64, FP64>;
    def CGXTRA : TernaryRRFe<"cgxtra", 0xB3E9, GR64, FP128>;
    def CFDTR : TernaryRRFe<"cfdtr", 0xB941, GR32, FP64>;
    def CFXTR : TernaryRRFe<"cfxtr", 0xB949, GR32, FP128>;
  }
}

// Convert a floating-point value to an unsigned integer value.
let Uses = [FPC], Defs = [CC] in {
  let Predicates = [FeatureFPExtension] in {
    def CLGDTR : TernaryRRFe<"clgdtr", 0xB942, GR64, FP64>;
    def CLGXTR : TernaryRRFe<"clgxtr", 0xB94A, GR64, FP128>;
    def CLFDTR : TernaryRRFe<"clfdtr", 0xB943, GR32, FP64>;
    def CLFXTR : TernaryRRFe<"clfxtr", 0xB94B, GR32, FP128>;
  }
}

// Convert a packed value to a floating-point one.
def CDSTR : UnaryRRE<"cdstr", 0xB3F3, null_frag, FP64,  GR64>;
def CXSTR : UnaryRRE<"cxstr", 0xB3FB, null_frag, FP128, GR128>;
def CDUTR : UnaryRRE<"cdutr", 0xB3F2, null_frag, FP64,  GR64>;
def CXUTR : UnaryRRE<"cxutr", 0xB3FA, null_frag, FP128, GR128>;

// Convert a floating-point value to a packed value.
def CSDTR : BinaryRRFd<"csdtr", 0xB3E3, GR64,  FP64>;
def CSXTR : BinaryRRFd<"csxtr", 0xB3EB, GR128, FP128>;
def CUDTR : UnaryRRE<"cudtr", 0xB3E2, null_frag, GR64,  FP64>;
def CUXTR : UnaryRRE<"cuxtr", 0xB3EA, null_frag, GR128, FP128>;

// Convert from/to memory values in the zoned format.
let Predicates = [FeatureDFPZonedConversion] in {
  def CDZT : BinaryRSL<"cdzt", 0xEDAA, FP64>;
  def CXZT : BinaryRSL<"cxzt", 0xEDAB, FP128>;
  def CZDT : StoreBinaryRSL<"czdt", 0xEDA8, FP64>;
  def CZXT : StoreBinaryRSL<"czxt", 0xEDA9, FP128>;
}

// Convert from/to memory values in the packed format.
let Predicates = [FeatureDFPPackedConversion] in {
  def CDPT : BinaryRSL<"cdpt", 0xEDAE, FP64>;
  def CXPT : BinaryRSL<"cxpt", 0xEDAF, FP128>;
  def CPDT : StoreBinaryRSL<"cpdt", 0xEDAC, FP64>;
  def CPXT : StoreBinaryRSL<"cpxt", 0xEDAD, FP128>;
}

// Perform floating-point operation.
let Defs = [CC, R1L, F0Q], Uses = [FPC, R0L, F4Q] in
  def PFPO : SideEffectInherentE<"pfpo", 0x010A>;


//===----------------------------------------------------------------------===//
// Unary arithmetic
//===----------------------------------------------------------------------===//

// Round to an integer, with the second operand (M3) specifying the rounding
// mode.  M4 can be set to 4 to suppress detection of inexact conditions.
let Uses = [FPC] in {
  def FIDTR : TernaryRRFe<"fidtr", 0xB3D7, FP64,  FP64>;
  def FIXTR : TernaryRRFe<"fixtr", 0xB3DF, FP128, FP128>;
}

// Extract biased exponent.
def EEDTR : UnaryRRE<"eedtr", 0xB3E5, null_frag, FP64,  FP64>;
def EEXTR : UnaryRRE<"eextr", 0xB3ED, null_frag, FP128, FP128>;

// Extract significance.
def ESDTR : UnaryRRE<"esdtr", 0xB3E7, null_frag, FP64,  FP64>;
def ESXTR : UnaryRRE<"esxtr", 0xB3EF, null_frag, FP128, FP128>;


//===----------------------------------------------------------------------===//
// Binary arithmetic
//===----------------------------------------------------------------------===//

// Addition.
let Uses = [FPC], Defs = [CC] in {
  let isCommutable = 1 in {
    def ADTR : BinaryRRFa<"adtr", 0xB3D2, null_frag, FP64,  FP64,  FP64>;
    def AXTR : BinaryRRFa<"axtr", 0xB3DA, null_frag, FP128, FP128, FP128>;
  }
  let Predicates = [FeatureFPExtension] in {
    def ADTRA : TernaryRRFa<"adtra", 0xB3D2, FP64,  FP64,  FP64>;
    def AXTRA : TernaryRRFa<"axtra", 0xB3DA, FP128, FP128, FP128>;
  }
}

// Subtraction.
let Uses = [FPC], Defs = [CC] in {
  def SDTR : BinaryRRFa<"sdtr", 0xB3D3, null_frag, FP64,  FP64,  FP64>;
  def SXTR : BinaryRRFa<"sxtr", 0xB3DB, null_frag, FP128, FP128, FP128>;
  let Predicates = [FeatureFPExtension] in {
    def SDTRA : TernaryRRFa<"sdtra", 0xB3D3, FP64,  FP64,  FP64>;
    def SXTRA : TernaryRRFa<"sxtra", 0xB3DB, FP128, FP128, FP128>;
  }
}

// Multiplication.
let Uses = [FPC] in {
  let isCommutable = 1 in {
    def MDTR : BinaryRRFa<"mdtr", 0xB3D0, null_frag, FP64,  FP64,  FP64>;
    def MXTR : BinaryRRFa<"mxtr", 0xB3D8, null_frag, FP128, FP128, FP128>;
  }
  let Predicates = [FeatureFPExtension] in {
    def MDTRA : TernaryRRFa<"mdtra", 0xB3D0, FP64,  FP64,  FP64>;
    def MXTRA : TernaryRRFa<"mxtra", 0xB3D8, FP128, FP128, FP128>;
  }
}

// Division.
let Uses = [FPC] in {
  def DDTR : BinaryRRFa<"ddtr", 0xB3D1, null_frag, FP64,  FP64,  FP64>;
  def DXTR : BinaryRRFa<"dxtr", 0xB3D9, null_frag, FP128, FP128, FP128>;
  let Predicates = [FeatureFPExtension] in {
    def DDTRA : TernaryRRFa<"ddtra", 0xB3D1, FP64,  FP64,  FP64>;
    def DXTRA : TernaryRRFa<"dxtra", 0xB3D9, FP128, FP128, FP128>;
  }
}

// Quantize.
let Uses = [FPC] in {
  def QADTR : TernaryRRFb<"qadtr", 0xB3F5, FP64,  FP64,  FP64>;
  def QAXTR : TernaryRRFb<"qaxtr", 0xB3FD, FP128, FP128, FP128>;
}

// Reround.
let Uses = [FPC] in {
  def RRDTR : TernaryRRFb<"rrdtr", 0xB3F7, FP64,  FP64,  FP64>;
  def RRXTR : TernaryRRFb<"rrxtr", 0xB3FF, FP128, FP128, FP128>;
}

// Shift significand left/right.
def SLDT : BinaryRXF<"sldt", 0xED40, null_frag, FP64,  FP64,  null_frag, 0>;
def SLXT : BinaryRXF<"slxt", 0xED48, null_frag, FP128, FP128, null_frag, 0>;
def SRDT : BinaryRXF<"srdt", 0xED41, null_frag, FP64,  FP64,  null_frag, 0>;
def SRXT : BinaryRXF<"srxt", 0xED49, null_frag, FP128, FP128, null_frag, 0>;

// Insert biased exponent.
def IEDTR : BinaryRRFb<"iedtr", 0xB3F6, null_frag, FP64,  FP64,   FP64>;
def IEXTR : BinaryRRFb<"iextr", 0xB3FE, null_frag, FP128, FP128, FP128>;


//===----------------------------------------------------------------------===//
// Comparisons
//===----------------------------------------------------------------------===//

// Compare.
let Uses = [FPC], Defs = [CC] in {
  def CDTR : CompareRRE<"cdtr", 0xB3E4, null_frag, FP64,  FP64>;
  def CXTR : CompareRRE<"cxtr", 0xB3EC, null_frag, FP128, FP128>;
}

// Compare and signal.
let Uses = [FPC], Defs = [CC] in {
  def KDTR : CompareRRE<"kdtr", 0xB3E0, null_frag, FP64,  FP64>;
  def KXTR : CompareRRE<"kxtr", 0xB3E8, null_frag, FP128, FP128>;
}

// Compare biased exponent.
let Defs = [CC] in {
  def CEDTR : CompareRRE<"cedtr", 0xB3F4, null_frag, FP64,  FP64>;
  def CEXTR : CompareRRE<"cextr", 0xB3FC, null_frag, FP128, FP128>;
}

// Test Data Class.
let Defs = [CC] in {
  def TDCET : TestRXE<"tdcet", 0xED50, null_frag, FP32>;
  def TDCDT : TestRXE<"tdcdt", 0xED54, null_frag, FP64>;
  def TDCXT : TestRXE<"tdcxt", 0xED58, null_frag, FP128>;
}

// Test Data Group.
let Defs = [CC] in {
  def TDGET : TestRXE<"tdget", 0xED51, null_frag, FP32>;
  def TDGDT : TestRXE<"tdgdt", 0xED55, null_frag, FP64>;
  def TDGXT : TestRXE<"tdgxt", 0xED59, null_frag, FP128>;
}

