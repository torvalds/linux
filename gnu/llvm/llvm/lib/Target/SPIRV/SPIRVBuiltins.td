//===-- SPIRVBuiltins.td - Describe SPIRV Builtins ---------*- tablegen -*-===//
 //
 // Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 // See https://llvm.org/LICENSE.txt for license information.
 // SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 //
 //===----------------------------------------------------------------------===//
 //
 // TableGen records defining implementation details of demangled builtin
 // functions and types.
 //
 //===----------------------------------------------------------------------===//

// Define SPIR-V external builtin/instruction sets
def InstructionSet : GenericEnum {
  let FilterClass = "InstructionSet";
  let NameField = "Name";
  let ValueField = "Value";
}

class InstructionSet<bits<32> value> {
  string Name = NAME;
  bits<32> Value = value;
}

def OpenCL_std : InstructionSet<0>;
def GLSL_std_450 : InstructionSet<1>;
def SPV_AMD_shader_trinary_minmax : InstructionSet<2>;
def NonSemantic_Shader_DebugInfo_100 : InstructionSet<3>;

// Define various builtin groups
def BuiltinGroup : GenericEnum {
  let FilterClass = "BuiltinGroup";
}

class BuiltinGroup;

def Extended : BuiltinGroup;
def Relational : BuiltinGroup;
def Group : BuiltinGroup;
def Variable : BuiltinGroup;
def Atomic : BuiltinGroup;
def Barrier : BuiltinGroup;
def Dot : BuiltinGroup;
def Wave : BuiltinGroup;
def GetQuery : BuiltinGroup;
def ImageSizeQuery : BuiltinGroup;
def ImageMiscQuery : BuiltinGroup;
def Convert : BuiltinGroup;
def ReadImage : BuiltinGroup;
def WriteImage : BuiltinGroup;
def SampleImage : BuiltinGroup;
def Select : BuiltinGroup;
def SpecConstant : BuiltinGroup;
def Enqueue : BuiltinGroup;
def AsyncCopy : BuiltinGroup;
def VectorLoadStore : BuiltinGroup;
def LoadStore : BuiltinGroup;
def IntelSubgroups : BuiltinGroup;
def AtomicFloating : BuiltinGroup;
def GroupUniform : BuiltinGroup;
def KernelClock : BuiltinGroup;
def CastToPtr : BuiltinGroup;
def Construct : BuiltinGroup;
def CoopMatr : BuiltinGroup;

//===----------------------------------------------------------------------===//
// Class defining a demangled builtin record. The information in the record
// should be used to expand the builtin into either native SPIR-V instructions
// or an external call (in case of builtins without a direct mapping).
//
// name is the demangled name of the given builtin.
// set specifies which external instruction set the builtin belongs to.
// group specifies to which implementation group given record belongs.
// minNumArgs is the minimum required number of arguments for lowering.
// maxNumArgs specifies the maximum used number of arguments for lowering.
//===----------------------------------------------------------------------===//
class DemangledBuiltin<string name, InstructionSet set, BuiltinGroup group, bits<8> minNumArgs, bits<8> maxNumArgs> {
  string Name = name;
  InstructionSet Set = set;
  BuiltinGroup Group = group;
  bits<8> MinNumArgs = minNumArgs;
  bits<8> MaxNumArgs = maxNumArgs;
}

// Table gathering all the builtins.
def DemangledBuiltins : GenericTable {
  let FilterClass = "DemangledBuiltin";
  let Fields = ["Name", "Set", "Group", "MinNumArgs", "MaxNumArgs"];
  string TypeOf_Set = "InstructionSet";
  string TypeOf_Group = "BuiltinGroup";
}

// Function to lookup builtins by their demangled name and set.
def lookupBuiltin : SearchIndex {
  let Table = DemangledBuiltins;
  let Key = ["Name", "Set"];
}

// Dot builtin record:
def : DemangledBuiltin<"dot", OpenCL_std, Dot, 2, 2>;
def : DemangledBuiltin<"__spirv_Dot", OpenCL_std, Dot, 2, 2>;

// Image builtin records:
def : DemangledBuiltin<"read_imagei", OpenCL_std, ReadImage, 2, 4>;
def : DemangledBuiltin<"read_imageui", OpenCL_std, ReadImage, 2, 4>;
def : DemangledBuiltin<"read_imagef", OpenCL_std, ReadImage, 2, 4>;

def : DemangledBuiltin<"write_imagef", OpenCL_std, WriteImage, 3, 4>;
def : DemangledBuiltin<"write_imagei", OpenCL_std, WriteImage, 3, 4>;
def : DemangledBuiltin<"write_imageui", OpenCL_std, WriteImage, 3, 4>;
def : DemangledBuiltin<"write_imageh", OpenCL_std, WriteImage, 3, 4>;

def : DemangledBuiltin<"__translate_sampler_initializer", OpenCL_std, SampleImage, 1, 1>;
def : DemangledBuiltin<"__spirv_SampledImage", OpenCL_std, SampleImage, 2, 2>;
def : DemangledBuiltin<"__spirv_ImageSampleExplicitLod", OpenCL_std, SampleImage, 3, 4>;

// Select builtin record:
def : DemangledBuiltin<"__spirv_Select", OpenCL_std, Select, 3, 3>;

// Composite Construct builtin record:
def : DemangledBuiltin<"__spirv_CompositeConstruct", OpenCL_std, Construct, 1, 0>;

//===----------------------------------------------------------------------===//
// Class defining an extended builtin record used for lowering into an
// OpExtInst instruction.
//
// name is the demangled name of the given builtin.
// set specifies which external instruction set the builtin belongs to.
// number specifies the number of the instruction in the external set.
//===----------------------------------------------------------------------===//
class ExtendedBuiltin<string name, InstructionSet set, int number> {
  string Name = name;
  InstructionSet Set = set;
  bits<32> Number = number;
}

// Table gathering all the extended builtins.
def ExtendedBuiltins : GenericTable {
  let FilterClass = "ExtendedBuiltin";
  let Fields = ["Name", "Set", "Number"];
  string TypeOf_Set = "InstructionSet";
}

// Function to lookup extended builtins by their name and set.
def lookupExtendedBuiltin : SearchIndex {
  let Table = ExtendedBuiltins;
  let Key = ["Name", "Set"];
}

// Function to lookup extended builtins by their set and number.
def lookupExtendedBuiltinBySetAndNumber : SearchIndex {
  let Table = ExtendedBuiltins;
  let Key = ["Set", "Number"];
}

// OpenCL extended instruction enums
def OpenCLExtInst : GenericEnum {
  let FilterClass = "OpenCLExtInst";
  let NameField = "Name";
  let ValueField = "Value";
}

class OpenCLExtInst<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

// GLSL extended instruction enums
def GLSLExtInst : GenericEnum {
  let FilterClass = "GLSLExtInst";
  let NameField = "Name";
  let ValueField = "Value";
}

class GLSLExtInst<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

def NonSemanticExtInst : GenericEnum {
  let FilterClass = "NonSemanticExtInst";
  let NameField = "Name";
  let ValueField = "Value";
}

class NonSemanticExtInst<string name, bits<32> value> {
  string Name = name;
  bits<32> Value = value;
}

// Multiclass used to define at the same time both a demangled builtin record
// and a corresponding extended builtin record.
multiclass DemangledExtendedBuiltin<string name, InstructionSet set, int number> {
  def : DemangledBuiltin<name, set, Extended, 1, 3>;
  def : ExtendedBuiltin<name, set, number>;

  if !eq(set, OpenCL_std) then {
    def : OpenCLExtInst<name, number>;
  }

  if !eq(set, GLSL_std_450) then {
    def : GLSLExtInst<name, number>;
  }

  if !eq(set, NonSemantic_Shader_DebugInfo_100) then {
    def : NonSemanticExtInst<name, number>;
  }
}

// Extended builtin records:
defm : DemangledExtendedBuiltin<"acos", OpenCL_std, 0>;
defm : DemangledExtendedBuiltin<"acosh", OpenCL_std, 1>;
defm : DemangledExtendedBuiltin<"acospi", OpenCL_std, 2>;
defm : DemangledExtendedBuiltin<"asin", OpenCL_std, 3>;
defm : DemangledExtendedBuiltin<"asinh", OpenCL_std, 4>;
defm : DemangledExtendedBuiltin<"asinpi", OpenCL_std, 5>;
defm : DemangledExtendedBuiltin<"atan", OpenCL_std, 6>;
defm : DemangledExtendedBuiltin<"atan2", OpenCL_std, 7>;
defm : DemangledExtendedBuiltin<"atanh", OpenCL_std, 8>;
defm : DemangledExtendedBuiltin<"atanpi", OpenCL_std, 9>;
defm : DemangledExtendedBuiltin<"atan2pi", OpenCL_std, 10>;
defm : DemangledExtendedBuiltin<"cbrt", OpenCL_std, 11>;
defm : DemangledExtendedBuiltin<"ceil", OpenCL_std, 12>;
defm : DemangledExtendedBuiltin<"copysign", OpenCL_std, 13>;
defm : DemangledExtendedBuiltin<"cos", OpenCL_std, 14>;
defm : DemangledExtendedBuiltin<"cosh", OpenCL_std, 15>;
defm : DemangledExtendedBuiltin<"cospi", OpenCL_std, 16>;
defm : DemangledExtendedBuiltin<"erfc", OpenCL_std, 17>;
defm : DemangledExtendedBuiltin<"erf", OpenCL_std, 18>;
defm : DemangledExtendedBuiltin<"exp", OpenCL_std, 19>;
defm : DemangledExtendedBuiltin<"exp2", OpenCL_std, 20>;
defm : DemangledExtendedBuiltin<"exp10", OpenCL_std, 21>;
defm : DemangledExtendedBuiltin<"expm1", OpenCL_std, 22>;
defm : DemangledExtendedBuiltin<"fabs", OpenCL_std, 23>;
defm : DemangledExtendedBuiltin<"fdim", OpenCL_std, 24>;
defm : DemangledExtendedBuiltin<"floor", OpenCL_std, 25>;
defm : DemangledExtendedBuiltin<"fma", OpenCL_std, 26>;
defm : DemangledExtendedBuiltin<"fmax", OpenCL_std, 27>;
defm : DemangledExtendedBuiltin<"fmin", OpenCL_std, 28>;
defm : DemangledExtendedBuiltin<"fmod", OpenCL_std, 29>;
defm : DemangledExtendedBuiltin<"fract", OpenCL_std, 30>;
defm : DemangledExtendedBuiltin<"frexp", OpenCL_std, 31>;
defm : DemangledExtendedBuiltin<"hypot", OpenCL_std, 32>;
defm : DemangledExtendedBuiltin<"ilogb", OpenCL_std, 33>;
defm : DemangledExtendedBuiltin<"ldexp", OpenCL_std, 34>;
defm : DemangledExtendedBuiltin<"lgamma", OpenCL_std, 35>;
defm : DemangledExtendedBuiltin<"lgamma_r", OpenCL_std, 36>;
defm : DemangledExtendedBuiltin<"log", OpenCL_std, 37>;
defm : DemangledExtendedBuiltin<"log2", OpenCL_std, 38>;
defm : DemangledExtendedBuiltin<"log10", OpenCL_std, 39>;
defm : DemangledExtendedBuiltin<"log1p", OpenCL_std, 40>;
defm : DemangledExtendedBuiltin<"logb", OpenCL_std, 41>;
defm : DemangledExtendedBuiltin<"mad", OpenCL_std, 42>;
defm : DemangledExtendedBuiltin<"maxmag", OpenCL_std, 43>;
defm : DemangledExtendedBuiltin<"minmag", OpenCL_std, 44>;
defm : DemangledExtendedBuiltin<"modf", OpenCL_std, 45>;
defm : DemangledExtendedBuiltin<"nan", OpenCL_std, 46>;
defm : DemangledExtendedBuiltin<"nextafter", OpenCL_std, 47>;
defm : DemangledExtendedBuiltin<"pow", OpenCL_std, 48>;
defm : DemangledExtendedBuiltin<"pown", OpenCL_std, 49>;
defm : DemangledExtendedBuiltin<"powr", OpenCL_std, 50>;
defm : DemangledExtendedBuiltin<"remainder", OpenCL_std, 51>;
defm : DemangledExtendedBuiltin<"remquo", OpenCL_std, 52>;
defm : DemangledExtendedBuiltin<"rint", OpenCL_std, 53>;
defm : DemangledExtendedBuiltin<"rootn", OpenCL_std, 54>;
defm : DemangledExtendedBuiltin<"round", OpenCL_std, 55>;
defm : DemangledExtendedBuiltin<"rsqrt", OpenCL_std, 56>;
defm : DemangledExtendedBuiltin<"sin", OpenCL_std, 57>;
defm : DemangledExtendedBuiltin<"sincos", OpenCL_std, 58>;
defm : DemangledExtendedBuiltin<"sinh", OpenCL_std, 59>;
defm : DemangledExtendedBuiltin<"sinpi", OpenCL_std, 60>;
defm : DemangledExtendedBuiltin<"sqrt", OpenCL_std, 61>;
defm : DemangledExtendedBuiltin<"tan", OpenCL_std, 62>;
defm : DemangledExtendedBuiltin<"tanh", OpenCL_std, 63>;
defm : DemangledExtendedBuiltin<"tanpi", OpenCL_std, 64>;
defm : DemangledExtendedBuiltin<"tgamma", OpenCL_std, 65>;
defm : DemangledExtendedBuiltin<"trunc", OpenCL_std, 66>;
defm : DemangledExtendedBuiltin<"half_cos", OpenCL_std, 67>;
defm : DemangledExtendedBuiltin<"half_divide", OpenCL_std, 68>;
defm : DemangledExtendedBuiltin<"half_exp", OpenCL_std, 69>;
defm : DemangledExtendedBuiltin<"half_exp2", OpenCL_std, 70>;
defm : DemangledExtendedBuiltin<"half_exp10", OpenCL_std, 71>;
defm : DemangledExtendedBuiltin<"half_log", OpenCL_std, 72>;
defm : DemangledExtendedBuiltin<"half_log2", OpenCL_std, 73>;
defm : DemangledExtendedBuiltin<"half_log10", OpenCL_std, 74>;
defm : DemangledExtendedBuiltin<"half_powr", OpenCL_std, 75>;
defm : DemangledExtendedBuiltin<"half_recip", OpenCL_std, 76>;
defm : DemangledExtendedBuiltin<"half_rsqrt", OpenCL_std, 77>;
defm : DemangledExtendedBuiltin<"half_sin", OpenCL_std, 78>;
defm : DemangledExtendedBuiltin<"half_sqrt", OpenCL_std, 79>;
defm : DemangledExtendedBuiltin<"half_tan", OpenCL_std, 80>;
defm : DemangledExtendedBuiltin<"native_cos", OpenCL_std, 81>;
defm : DemangledExtendedBuiltin<"native_divide", OpenCL_std, 82>;
defm : DemangledExtendedBuiltin<"native_exp", OpenCL_std, 83>;
defm : DemangledExtendedBuiltin<"native_exp2", OpenCL_std, 84>;
defm : DemangledExtendedBuiltin<"native_exp10", OpenCL_std, 85>;
defm : DemangledExtendedBuiltin<"native_log", OpenCL_std, 86>;
defm : DemangledExtendedBuiltin<"native_log2", OpenCL_std, 87>;
defm : DemangledExtendedBuiltin<"native_log10", OpenCL_std, 88>;
defm : DemangledExtendedBuiltin<"native_powr", OpenCL_std, 89>;
defm : DemangledExtendedBuiltin<"native_recip", OpenCL_std, 90>;
defm : DemangledExtendedBuiltin<"native_rsqrt", OpenCL_std, 91>;
defm : DemangledExtendedBuiltin<"native_sin", OpenCL_std, 92>;
defm : DemangledExtendedBuiltin<"native_sqrt", OpenCL_std, 93>;
defm : DemangledExtendedBuiltin<"native_tan", OpenCL_std, 94>;
defm : DemangledExtendedBuiltin<"s_abs", OpenCL_std, 141>;
defm : DemangledExtendedBuiltin<"s_abs_diff", OpenCL_std, 142>;
defm : DemangledExtendedBuiltin<"s_add_sat", OpenCL_std, 143>;
defm : DemangledExtendedBuiltin<"u_add_sat", OpenCL_std, 144>;
defm : DemangledExtendedBuiltin<"s_hadd", OpenCL_std, 145>;
defm : DemangledExtendedBuiltin<"u_hadd", OpenCL_std, 146>;
defm : DemangledExtendedBuiltin<"s_rhadd", OpenCL_std, 147>;
defm : DemangledExtendedBuiltin<"u_rhadd", OpenCL_std, 148>;
defm : DemangledExtendedBuiltin<"s_clamp", OpenCL_std, 149>;
defm : DemangledExtendedBuiltin<"u_clamp", OpenCL_std, 150>;
defm : DemangledExtendedBuiltin<"clz", OpenCL_std, 151>;
defm : DemangledExtendedBuiltin<"ctz", OpenCL_std, 152>;
defm : DemangledExtendedBuiltin<"s_mad_hi", OpenCL_std, 153>;
defm : DemangledExtendedBuiltin<"u_mad_sat", OpenCL_std, 154>;
defm : DemangledExtendedBuiltin<"s_mad_sat", OpenCL_std, 155>;
defm : DemangledExtendedBuiltin<"s_max", OpenCL_std, 156>;
defm : DemangledExtendedBuiltin<"u_max", OpenCL_std, 157>;
defm : DemangledExtendedBuiltin<"s_min", OpenCL_std, 158>;
defm : DemangledExtendedBuiltin<"u_min", OpenCL_std, 159>;
defm : DemangledExtendedBuiltin<"s_mul_hi", OpenCL_std, 160>;
defm : DemangledExtendedBuiltin<"rotate", OpenCL_std, 161>;
defm : DemangledExtendedBuiltin<"s_sub_sat", OpenCL_std, 162>;
defm : DemangledExtendedBuiltin<"u_sub_sat", OpenCL_std, 163>;
defm : DemangledExtendedBuiltin<"u_upsample", OpenCL_std, 164>;
defm : DemangledExtendedBuiltin<"s_upsample", OpenCL_std, 165>;
defm : DemangledExtendedBuiltin<"popcount", OpenCL_std, 166>;
defm : DemangledExtendedBuiltin<"s_mad24", OpenCL_std, 167>;
defm : DemangledExtendedBuiltin<"u_mad24", OpenCL_std, 168>;
defm : DemangledExtendedBuiltin<"s_mul24", OpenCL_std, 169>;
defm : DemangledExtendedBuiltin<"u_mul24", OpenCL_std, 170>;
defm : DemangledExtendedBuiltin<"u_abs", OpenCL_std, 201>;
defm : DemangledExtendedBuiltin<"u_abs_diff", OpenCL_std, 202>;
defm : DemangledExtendedBuiltin<"u_mul_hi", OpenCL_std, 203>;
defm : DemangledExtendedBuiltin<"u_mad_hi", OpenCL_std, 204>;
defm : DemangledExtendedBuiltin<"fclamp", OpenCL_std, 95>;
defm : DemangledExtendedBuiltin<"degrees", OpenCL_std, 96>;
defm : DemangledExtendedBuiltin<"fmax_common", OpenCL_std, 97>;
defm : DemangledExtendedBuiltin<"fmin_common", OpenCL_std, 98>;
defm : DemangledExtendedBuiltin<"mix", OpenCL_std, 99>;
defm : DemangledExtendedBuiltin<"radians", OpenCL_std, 100>;
defm : DemangledExtendedBuiltin<"step", OpenCL_std, 101>;
defm : DemangledExtendedBuiltin<"smoothstep", OpenCL_std, 102>;
defm : DemangledExtendedBuiltin<"sign", OpenCL_std, 103>;
defm : DemangledExtendedBuiltin<"cross", OpenCL_std, 104>;
defm : DemangledExtendedBuiltin<"distance", OpenCL_std, 105>;
defm : DemangledExtendedBuiltin<"length", OpenCL_std, 106>;
defm : DemangledExtendedBuiltin<"normalize", OpenCL_std, 107>;
defm : DemangledExtendedBuiltin<"fast_distance", OpenCL_std, 108>;
defm : DemangledExtendedBuiltin<"fast_length", OpenCL_std, 109>;
defm : DemangledExtendedBuiltin<"fast_normalize", OpenCL_std, 110>;
defm : DemangledExtendedBuiltin<"bitselect", OpenCL_std, 186>;
defm : DemangledExtendedBuiltin<"select", OpenCL_std, 187>;
defm : DemangledExtendedBuiltin<"vloadn", OpenCL_std, 171>;
defm : DemangledExtendedBuiltin<"vstoren", OpenCL_std, 172>;
defm : DemangledExtendedBuiltin<"vload_half", OpenCL_std, 173>;
defm : DemangledExtendedBuiltin<"vload_halfn", OpenCL_std, 174>;
defm : DemangledExtendedBuiltin<"vstore_half", OpenCL_std, 175>;
defm : DemangledExtendedBuiltin<"vstore_half_r", OpenCL_std, 176>;
defm : DemangledExtendedBuiltin<"vstore_halfn", OpenCL_std, 177>;
defm : DemangledExtendedBuiltin<"vstore_halfn_r", OpenCL_std, 178>;
defm : DemangledExtendedBuiltin<"vloada_halfn", OpenCL_std, 179>;
defm : DemangledExtendedBuiltin<"vstorea_halfn", OpenCL_std, 180>;
defm : DemangledExtendedBuiltin<"vstorea_halfn_r", OpenCL_std, 181>;
defm : DemangledExtendedBuiltin<"shuffle", OpenCL_std, 182>;
defm : DemangledExtendedBuiltin<"shuffle2", OpenCL_std, 183>;
defm : DemangledExtendedBuiltin<"printf", OpenCL_std, 184>;
defm : DemangledExtendedBuiltin<"prefetch", OpenCL_std, 185>;

defm : DemangledExtendedBuiltin<"Round", GLSL_std_450, 1>;
defm : DemangledExtendedBuiltin<"RoundEven", GLSL_std_450, 2>;
defm : DemangledExtendedBuiltin<"Trunc", GLSL_std_450, 3>;
defm : DemangledExtendedBuiltin<"FAbs", GLSL_std_450, 4>;
defm : DemangledExtendedBuiltin<"SAbs", GLSL_std_450, 5>;
defm : DemangledExtendedBuiltin<"FSign", GLSL_std_450, 6>;
defm : DemangledExtendedBuiltin<"SSign", GLSL_std_450, 7>;
defm : DemangledExtendedBuiltin<"Floor", GLSL_std_450, 8>;
defm : DemangledExtendedBuiltin<"Ceil", GLSL_std_450, 9>;
defm : DemangledExtendedBuiltin<"Fract", GLSL_std_450, 10>;
defm : DemangledExtendedBuiltin<"Radians", GLSL_std_450, 11>;
defm : DemangledExtendedBuiltin<"Degrees", GLSL_std_450, 12>;
defm : DemangledExtendedBuiltin<"Sin", GLSL_std_450, 13>;
defm : DemangledExtendedBuiltin<"Cos", GLSL_std_450, 14>;
defm : DemangledExtendedBuiltin<"Tan", GLSL_std_450, 15>;
defm : DemangledExtendedBuiltin<"Asin", GLSL_std_450, 16>;
defm : DemangledExtendedBuiltin<"Acos", GLSL_std_450, 17>;
defm : DemangledExtendedBuiltin<"Atan", GLSL_std_450, 18>;
defm : DemangledExtendedBuiltin<"Sinh", GLSL_std_450, 19>;
defm : DemangledExtendedBuiltin<"Cosh", GLSL_std_450, 20>;
defm : DemangledExtendedBuiltin<"Tanh", GLSL_std_450, 21>;
defm : DemangledExtendedBuiltin<"Asinh", GLSL_std_450, 22>;
defm : DemangledExtendedBuiltin<"Acosh", GLSL_std_450, 23>;
defm : DemangledExtendedBuiltin<"Atanh", GLSL_std_450, 24>;
defm : DemangledExtendedBuiltin<"Atan2", GLSL_std_450, 25>;
defm : DemangledExtendedBuiltin<"Pow", GLSL_std_450, 26>;
defm : DemangledExtendedBuiltin<"Exp", GLSL_std_450, 27>;
defm : DemangledExtendedBuiltin<"Log", GLSL_std_450, 28>;
defm : DemangledExtendedBuiltin<"Exp2", GLSL_std_450, 29>;
defm : DemangledExtendedBuiltin<"Log2", GLSL_std_450, 30>;
defm : DemangledExtendedBuiltin<"Sqrt", GLSL_std_450, 31>;
defm : DemangledExtendedBuiltin<"InverseSqrt", GLSL_std_450, 32>;
defm : DemangledExtendedBuiltin<"Determinant", GLSL_std_450, 33>;
defm : DemangledExtendedBuiltin<"MatrixInverse", GLSL_std_450, 34>;
defm : DemangledExtendedBuiltin<"Modf", GLSL_std_450, 35>;
defm : DemangledExtendedBuiltin<"ModfStruct", GLSL_std_450, 36>;
defm : DemangledExtendedBuiltin<"FMin", GLSL_std_450, 37>;
defm : DemangledExtendedBuiltin<"UMin", GLSL_std_450, 38>;
defm : DemangledExtendedBuiltin<"SMin", GLSL_std_450, 39>;
defm : DemangledExtendedBuiltin<"FMax", GLSL_std_450, 40>;
defm : DemangledExtendedBuiltin<"UMax", GLSL_std_450, 41>;
defm : DemangledExtendedBuiltin<"SMax", GLSL_std_450, 42>;
defm : DemangledExtendedBuiltin<"FClamp", GLSL_std_450, 43>;
defm : DemangledExtendedBuiltin<"UClamp", GLSL_std_450, 44>;
defm : DemangledExtendedBuiltin<"SClamp", GLSL_std_450, 45>;
defm : DemangledExtendedBuiltin<"FMix", GLSL_std_450, 46>;
defm : DemangledExtendedBuiltin<"Step", GLSL_std_450, 48>;
defm : DemangledExtendedBuiltin<"SmoothStep", GLSL_std_450, 49>;
defm : DemangledExtendedBuiltin<"Fma", GLSL_std_450, 50>;
defm : DemangledExtendedBuiltin<"Frexp", GLSL_std_450, 51>;
defm : DemangledExtendedBuiltin<"FrexpStruct", GLSL_std_450, 52>;
defm : DemangledExtendedBuiltin<"Ldexp", GLSL_std_450, 53>;
defm : DemangledExtendedBuiltin<"PackSnorm4x8", GLSL_std_450, 54>;
defm : DemangledExtendedBuiltin<"PackUnorm4x8", GLSL_std_450, 55>;
defm : DemangledExtendedBuiltin<"PackSnorm2x16", GLSL_std_450, 56>;
defm : DemangledExtendedBuiltin<"PackUnorm2x16", GLSL_std_450, 57>;
defm : DemangledExtendedBuiltin<"PackHalf2x16", GLSL_std_450, 58>;
defm : DemangledExtendedBuiltin<"PackDouble2x32", GLSL_std_450, 59>;
defm : DemangledExtendedBuiltin<"UnpackSnorm2x16", GLSL_std_450, 60>;
defm : DemangledExtendedBuiltin<"UnpackUnorm2x16", GLSL_std_450, 61>;
defm : DemangledExtendedBuiltin<"UnpackHalf2x16", GLSL_std_450, 62>;
defm : DemangledExtendedBuiltin<"UnpackSnorm4x8", GLSL_std_450, 63>;
defm : DemangledExtendedBuiltin<"UnpackUnorm4x8", GLSL_std_450, 64>;
defm : DemangledExtendedBuiltin<"UnpackDouble2x32", GLSL_std_450, 65>;
defm : DemangledExtendedBuiltin<"Length", GLSL_std_450, 66>;
defm : DemangledExtendedBuiltin<"Distance", GLSL_std_450, 67>;
defm : DemangledExtendedBuiltin<"Cross", GLSL_std_450, 68>;
defm : DemangledExtendedBuiltin<"Normalize", GLSL_std_450, 69>;
defm : DemangledExtendedBuiltin<"FaceForward", GLSL_std_450, 70>;
defm : DemangledExtendedBuiltin<"Reflect", GLSL_std_450, 71>;
defm : DemangledExtendedBuiltin<"Refract", GLSL_std_450, 72>;
defm : DemangledExtendedBuiltin<"FindILsb", GLSL_std_450, 73>;
defm : DemangledExtendedBuiltin<"FindSMsb", GLSL_std_450, 74>;
defm : DemangledExtendedBuiltin<"FindUMsb", GLSL_std_450, 75>;
defm : DemangledExtendedBuiltin<"InterpolateAtCentroid", GLSL_std_450, 76>;
defm : DemangledExtendedBuiltin<"InterpolateAtSample", GLSL_std_450, 77>;
defm : DemangledExtendedBuiltin<"InterpolateAtOffset", GLSL_std_450, 78>;
defm : DemangledExtendedBuiltin<"NMin", GLSL_std_450, 79>;
defm : DemangledExtendedBuiltin<"NMax", GLSL_std_450, 80>;
defm : DemangledExtendedBuiltin<"NClamp", GLSL_std_450, 81>;

defm : DemangledExtendedBuiltin<"DebugInfoNone", NonSemantic_Shader_DebugInfo_100, 0>;
defm : DemangledExtendedBuiltin<"DebugCompilationUnit", NonSemantic_Shader_DebugInfo_100, 1>;
defm : DemangledExtendedBuiltin<"DebugTypeBasic", NonSemantic_Shader_DebugInfo_100, 2>;
defm : DemangledExtendedBuiltin<"DebugTypePointer", NonSemantic_Shader_DebugInfo_100, 3>;
defm : DemangledExtendedBuiltin<"DebugTypeQualifier", NonSemantic_Shader_DebugInfo_100, 4>;
defm : DemangledExtendedBuiltin<"DebugTypeArray", NonSemantic_Shader_DebugInfo_100, 5>;
defm : DemangledExtendedBuiltin<"DebugTypeVector", NonSemantic_Shader_DebugInfo_100, 6>;
defm : DemangledExtendedBuiltin<"DebugTypedef", NonSemantic_Shader_DebugInfo_100, 7>;
defm : DemangledExtendedBuiltin<"DebugTypeFunction", NonSemantic_Shader_DebugInfo_100, 8>;
defm : DemangledExtendedBuiltin<"DebugTypeEnum", NonSemantic_Shader_DebugInfo_100, 9>;
defm : DemangledExtendedBuiltin<"DebugTypeComposite", NonSemantic_Shader_DebugInfo_100, 10>;
defm : DemangledExtendedBuiltin<"DebugTypeMember", NonSemantic_Shader_DebugInfo_100, 11>;
defm : DemangledExtendedBuiltin<"DebugTypeInheritance", NonSemantic_Shader_DebugInfo_100, 12>;
defm : DemangledExtendedBuiltin<"DebugTypePtrToMember", NonSemantic_Shader_DebugInfo_100, 13>;
defm : DemangledExtendedBuiltin<"DebugTypeTemplate", NonSemantic_Shader_DebugInfo_100, 14>;
defm : DemangledExtendedBuiltin<"DebugTypeTemplateParameter", NonSemantic_Shader_DebugInfo_100, 15>;
defm : DemangledExtendedBuiltin<"DebugTypeTemplateTemplateParameter", NonSemantic_Shader_DebugInfo_100, 16>;
defm : DemangledExtendedBuiltin<"DebugTypeTemplateParameterPack", NonSemantic_Shader_DebugInfo_100, 17>;
defm : DemangledExtendedBuiltin<"DebugGlobalVariable", NonSemantic_Shader_DebugInfo_100, 18>;
defm : DemangledExtendedBuiltin<"DebugFunctionDeclaration", NonSemantic_Shader_DebugInfo_100, 19>;
defm : DemangledExtendedBuiltin<"DebugFunction", NonSemantic_Shader_DebugInfo_100, 20>;
defm : DemangledExtendedBuiltin<"DebugLexicalBlock", NonSemantic_Shader_DebugInfo_100, 21>;
defm : DemangledExtendedBuiltin<"DebugLexicalBlockDiscriminator", NonSemantic_Shader_DebugInfo_100, 22>;
defm : DemangledExtendedBuiltin<"DebugScope", NonSemantic_Shader_DebugInfo_100, 23>;
defm : DemangledExtendedBuiltin<"DebugNoScope", NonSemantic_Shader_DebugInfo_100, 24>;
defm : DemangledExtendedBuiltin<"DebugInlinedAt", NonSemantic_Shader_DebugInfo_100, 25>;
defm : DemangledExtendedBuiltin<"DebugLocalVariable", NonSemantic_Shader_DebugInfo_100, 26>;
defm : DemangledExtendedBuiltin<"DebugInlinedVariable", NonSemantic_Shader_DebugInfo_100, 27>;
defm : DemangledExtendedBuiltin<"DebugDeclare", NonSemantic_Shader_DebugInfo_100, 28>;
defm : DemangledExtendedBuiltin<"DebugValue", NonSemantic_Shader_DebugInfo_100, 29>;
defm : DemangledExtendedBuiltin<"DebugOperation", NonSemantic_Shader_DebugInfo_100, 30>;
defm : DemangledExtendedBuiltin<"DebugExpression", NonSemantic_Shader_DebugInfo_100, 31>;
defm : DemangledExtendedBuiltin<"DebugMacroDef", NonSemantic_Shader_DebugInfo_100, 32>;
defm : DemangledExtendedBuiltin<"DebugMacroUndef", NonSemantic_Shader_DebugInfo_100, 33>;
defm : DemangledExtendedBuiltin<"DebugImportedEntity", NonSemantic_Shader_DebugInfo_100, 34>;
defm : DemangledExtendedBuiltin<"DebugSource", NonSemantic_Shader_DebugInfo_100, 35>;
defm : DemangledExtendedBuiltin<"DebugFunctionDefinition", NonSemantic_Shader_DebugInfo_100, 101>;
defm : DemangledExtendedBuiltin<"DebugSourceContinued", NonSemantic_Shader_DebugInfo_100, 102>;
defm : DemangledExtendedBuiltin<"DebugLine", NonSemantic_Shader_DebugInfo_100, 103>;
defm : DemangledExtendedBuiltin<"DebugNoLine", NonSemantic_Shader_DebugInfo_100, 104>;
defm : DemangledExtendedBuiltin<"DebugBuildIdentifier", NonSemantic_Shader_DebugInfo_100, 105>;
defm : DemangledExtendedBuiltin<"DebugStoragePath", NonSemantic_Shader_DebugInfo_100, 106>;
defm : DemangledExtendedBuiltin<"DebugEntryPoint", NonSemantic_Shader_DebugInfo_100, 107>;
defm : DemangledExtendedBuiltin<"DebugTypeMatrix", NonSemantic_Shader_DebugInfo_100, 108>;
//===----------------------------------------------------------------------===//
// Class defining an native builtin record used for direct translation into a
// SPIR-V instruction.
//
// name is the demangled name of the given builtin.
// set specifies which external instruction set the builtin belongs to.
// opcode specifies the SPIR-V operation code of the generated instruction.
//===----------------------------------------------------------------------===//
class NativeBuiltin<string name, InstructionSet set, Op operation> {
  string Name = name;
  InstructionSet Set = set;
  Op Opcode = operation;
}

// Table gathering all the native builtins.
def NativeBuiltins : GenericTable {
  let FilterClass = "NativeBuiltin";
  let Fields = ["Name", "Set", "Opcode"];
  string TypeOf_Set = "InstructionSet";
}

// Function to lookup native builtins by their name and set.
def lookupNativeBuiltin : SearchIndex {
  let Table = NativeBuiltins;
  let Key = ["Name", "Set"];
}

// Multiclass used to define at the same time both an incoming builtin record
// and a corresponding native builtin record.
multiclass DemangledNativeBuiltin<string name, InstructionSet set, BuiltinGroup group, bits<8> minNumArgs, bits<8> maxNumArgs, Op operation> {
  def : DemangledBuiltin<name, set, group, minNumArgs, maxNumArgs>;
  def : NativeBuiltin<name, set, operation>;
}

// Relational builtin records:
defm : DemangledNativeBuiltin<"isequal", OpenCL_std, Relational, 2, 2, OpFOrdEqual>;
defm : DemangledNativeBuiltin<"__spirv_FOrdEqual", OpenCL_std, Relational, 2, 2, OpFOrdEqual>;
defm : DemangledNativeBuiltin<"isnotequal", OpenCL_std, Relational, 2, 2, OpFUnordNotEqual>;
defm : DemangledNativeBuiltin<"__spirv_FUnordNotEqual", OpenCL_std, Relational, 2, 2, OpFUnordNotEqual>;
defm : DemangledNativeBuiltin<"isgreater", OpenCL_std, Relational, 2, 2, OpFOrdGreaterThan>;
defm : DemangledNativeBuiltin<"__spirv_FOrdGreaterThan", OpenCL_std, Relational, 2, 2, OpFOrdGreaterThan>;
defm : DemangledNativeBuiltin<"isgreaterequal", OpenCL_std, Relational, 2, 2, OpFOrdGreaterThanEqual>;
defm : DemangledNativeBuiltin<"__spirv_FOrdGreaterThanEqual", OpenCL_std, Relational, 2, 2, OpFOrdGreaterThanEqual>;
defm : DemangledNativeBuiltin<"isless", OpenCL_std, Relational, 2, 2, OpFOrdLessThan>;
defm : DemangledNativeBuiltin<"__spirv_FOrdLessThan", OpenCL_std, Relational, 2, 2, OpFOrdLessThan>;
defm : DemangledNativeBuiltin<"islessequal", OpenCL_std, Relational, 2, 2, OpFOrdLessThanEqual>;
defm : DemangledNativeBuiltin<"__spirv_FOrdLessThanEqual", OpenCL_std, Relational, 2, 2, OpFOrdLessThanEqual>;
defm : DemangledNativeBuiltin<"islessgreater", OpenCL_std, Relational, 2, 2, OpFOrdNotEqual>;
defm : DemangledNativeBuiltin<"__spirv_FOrdNotEqual", OpenCL_std, Relational, 2, 2, OpFOrdNotEqual>;
defm : DemangledNativeBuiltin<"isordered", OpenCL_std, Relational, 2, 2, OpOrdered>;
defm : DemangledNativeBuiltin<"__spirv_Ordered", OpenCL_std, Relational, 2, 2, OpOrdered>;
defm : DemangledNativeBuiltin<"isunordered", OpenCL_std, Relational, 2, 2, OpUnordered>;
defm : DemangledNativeBuiltin<"__spirv_Unordered", OpenCL_std, Relational, 2, 2, OpUnordered>;
defm : DemangledNativeBuiltin<"isfinite", OpenCL_std, Relational, 1, 1, OpIsFinite>;
defm : DemangledNativeBuiltin<"__spirv_IsFinite", OpenCL_std, Relational, 1, 1, OpIsFinite>;
defm : DemangledNativeBuiltin<"isinf", OpenCL_std, Relational, 1, 1, OpIsInf>;
defm : DemangledNativeBuiltin<"__spirv_IsInf", OpenCL_std, Relational, 1, 1, OpIsInf>;
defm : DemangledNativeBuiltin<"isnan", OpenCL_std, Relational, 1, 1, OpIsNan>;
defm : DemangledNativeBuiltin<"__spirv_IsNan", OpenCL_std, Relational, 1, 1, OpIsNan>;
defm : DemangledNativeBuiltin<"isnormal", OpenCL_std, Relational, 1, 1, OpIsNormal>;
defm : DemangledNativeBuiltin<"__spirv_IsNormal", OpenCL_std, Relational, 1, 1, OpIsNormal>;
defm : DemangledNativeBuiltin<"signbit", OpenCL_std, Relational, 1, 1, OpSignBitSet>;
defm : DemangledNativeBuiltin<"__spirv_SignBitSet", OpenCL_std, Relational, 1, 1, OpSignBitSet>;
defm : DemangledNativeBuiltin<"any", OpenCL_std, Relational, 1, 1, OpAny>;
defm : DemangledNativeBuiltin<"__spirv_Any", OpenCL_std, Relational, 1, 1, OpAny>;
defm : DemangledNativeBuiltin<"all", OpenCL_std, Relational, 1, 1, OpAll>;
defm : DemangledNativeBuiltin<"__spirv_All", OpenCL_std, Relational, 1, 1, OpAll>;

// Atomic builtin records:
defm : DemangledNativeBuiltin<"atomic_init", OpenCL_std, Atomic, 2, 2, OpStore>;
defm : DemangledNativeBuiltin<"atomic_load", OpenCL_std, Atomic, 1, 1, OpAtomicLoad>;
defm : DemangledNativeBuiltin<"atomic_load_explicit", OpenCL_std, Atomic, 2, 3, OpAtomicLoad>;
defm : DemangledNativeBuiltin<"__spirv_AtomicLoad", OpenCL_std, Atomic, 3, 3, OpAtomicLoad>;
defm : DemangledNativeBuiltin<"atomic_store", OpenCL_std, Atomic, 2, 2, OpAtomicStore>;
defm : DemangledNativeBuiltin<"atomic_store_explicit", OpenCL_std, Atomic, 2, 4, OpAtomicStore>;
defm : DemangledNativeBuiltin<"__spirv_AtomicStore", OpenCL_std, Atomic, 4, 4, OpAtomicStore>;
defm : DemangledNativeBuiltin<"atomic_compare_exchange_strong", OpenCL_std, Atomic, 3, 6, OpAtomicCompareExchange>;
defm : DemangledNativeBuiltin<"__spirv_AtomicCompareExchange", OpenCL_std, Atomic, 6, 6, OpAtomicCompareExchange>;
defm : DemangledNativeBuiltin<"atomic_compare_exchange_strong_explicit", OpenCL_std, Atomic, 5, 6, OpAtomicCompareExchange>;
defm : DemangledNativeBuiltin<"atomic_compare_exchange_weak", OpenCL_std, Atomic, 3, 6, OpAtomicCompareExchangeWeak>;
defm : DemangledNativeBuiltin<"atomic_compare_exchange_weak_explicit", OpenCL_std, Atomic, 5, 6, OpAtomicCompareExchangeWeak>;
defm : DemangledNativeBuiltin<"__spirv_AtomicCompareExchangeWeak", OpenCL_std, Atomic, 6, 6, OpAtomicCompareExchangeWeak>;
defm : DemangledNativeBuiltin<"atom_cmpxchg", OpenCL_std, Atomic, 3, 6, OpAtomicCompareExchange>;
defm : DemangledNativeBuiltin<"atomic_cmpxchg", OpenCL_std, Atomic, 3, 6, OpAtomicCompareExchange>;
defm : DemangledNativeBuiltin<"atom_add", OpenCL_std, Atomic, 2, 4, OpAtomicIAdd>;
defm : DemangledNativeBuiltin<"atomic_add", OpenCL_std, Atomic, 2, 4, OpAtomicIAdd>;
defm : DemangledNativeBuiltin<"__spirv_AtomicIAdd", OpenCL_std, Atomic, 4, 4, OpAtomicIAdd>;
defm : DemangledNativeBuiltin<"atom_sub", OpenCL_std, Atomic, 2, 4, OpAtomicISub>;
defm : DemangledNativeBuiltin<"atomic_sub", OpenCL_std, Atomic, 2, 4, OpAtomicISub>;
defm : DemangledNativeBuiltin<"__spirv_AtomicISub", OpenCL_std, Atomic, 4, 4, OpAtomicISub>;
defm : DemangledNativeBuiltin<"atom_or", OpenCL_std, Atomic, 2, 4, OpAtomicOr>;
defm : DemangledNativeBuiltin<"atomic_or", OpenCL_std, Atomic, 2, 4, OpAtomicOr>;
defm : DemangledNativeBuiltin<"__spirv_AtomicOr", OpenCL_std, Atomic, 4, 4, OpAtomicOr>;
defm : DemangledNativeBuiltin<"atom_xor", OpenCL_std, Atomic, 2, 4, OpAtomicXor>;
defm : DemangledNativeBuiltin<"atomic_xor", OpenCL_std, Atomic, 2, 4, OpAtomicXor>;
defm : DemangledNativeBuiltin<"__spirv_AtomicXor", OpenCL_std, Atomic, 4, 4, OpAtomicXor>;
defm : DemangledNativeBuiltin<"atom_and", OpenCL_std, Atomic, 2, 4, OpAtomicAnd>;
defm : DemangledNativeBuiltin<"atomic_and", OpenCL_std, Atomic, 2, 4, OpAtomicAnd>;
defm : DemangledNativeBuiltin<"__spirv_AtomicAnd", OpenCL_std, Atomic, 4, 4, OpAtomicAnd>;
defm : DemangledNativeBuiltin<"atomic_exchange", OpenCL_std, Atomic, 2, 4, OpAtomicExchange>;
defm : DemangledNativeBuiltin<"atomic_exchange_explicit", OpenCL_std, Atomic, 2, 4, OpAtomicExchange>;
defm : DemangledNativeBuiltin<"AtomicEx__spirv_change", OpenCL_std, Atomic, 2, 4, OpAtomicExchange>;
defm : DemangledNativeBuiltin<"__spirv_AtomicExchange", OpenCL_std, Atomic, 4, 4, OpAtomicExchange>;
defm : DemangledNativeBuiltin<"atomic_work_item_fence", OpenCL_std, Atomic, 1, 3, OpMemoryBarrier>;
defm : DemangledNativeBuiltin<"__spirv_MemoryBarrier", OpenCL_std, Atomic, 2, 2, OpMemoryBarrier>;
defm : DemangledNativeBuiltin<"atomic_fetch_add", OpenCL_std, Atomic, 2, 4, OpAtomicIAdd>;
defm : DemangledNativeBuiltin<"atomic_fetch_sub", OpenCL_std, Atomic, 2, 4, OpAtomicISub>;
defm : DemangledNativeBuiltin<"atomic_fetch_or", OpenCL_std, Atomic, 2, 4, OpAtomicOr>;
defm : DemangledNativeBuiltin<"atomic_fetch_xor", OpenCL_std, Atomic, 2, 4, OpAtomicXor>;
defm : DemangledNativeBuiltin<"atomic_fetch_and", OpenCL_std, Atomic, 2, 4, OpAtomicAnd>;
defm : DemangledNativeBuiltin<"atomic_fetch_add_explicit", OpenCL_std, Atomic, 3, 4, OpAtomicIAdd>;
defm : DemangledNativeBuiltin<"atomic_fetch_sub_explicit", OpenCL_std, Atomic, 3, 4, OpAtomicISub>;
defm : DemangledNativeBuiltin<"atomic_fetch_or_explicit", OpenCL_std, Atomic, 3, 4, OpAtomicOr>;
defm : DemangledNativeBuiltin<"atomic_fetch_xor_explicit", OpenCL_std, Atomic, 3, 4, OpAtomicXor>;
defm : DemangledNativeBuiltin<"atomic_fetch_and_explicit", OpenCL_std, Atomic, 3, 4, OpAtomicAnd>;
defm : DemangledNativeBuiltin<"atomic_flag_test_and_set", OpenCL_std, Atomic, 1, 1, OpAtomicFlagTestAndSet>;
defm : DemangledNativeBuiltin<"__spirv_AtomicFlagTestAndSet", OpenCL_std, Atomic, 3, 3, OpAtomicFlagTestAndSet>;
defm : DemangledNativeBuiltin<"atomic_flag_test_and_set_explicit", OpenCL_std, Atomic, 2, 3, OpAtomicFlagTestAndSet>;
defm : DemangledNativeBuiltin<"atomic_flag_clear", OpenCL_std, Atomic, 1, 1, OpAtomicFlagClear>;
defm : DemangledNativeBuiltin<"__spirv_AtomicFlagClear", OpenCL_std, Atomic, 3, 3, OpAtomicFlagClear>;
defm : DemangledNativeBuiltin<"atomic_flag_clear_explicit", OpenCL_std, Atomic, 2, 3, OpAtomicFlagClear>;
defm : DemangledNativeBuiltin<"__spirv_AtomicSMin", OpenCL_std, Atomic, 4, 4, OpAtomicSMin>;
defm : DemangledNativeBuiltin<"__spirv_AtomicSMax", OpenCL_std, Atomic, 4, 4, OpAtomicSMax>;
defm : DemangledNativeBuiltin<"__spirv_AtomicUMin", OpenCL_std, Atomic, 4, 4, OpAtomicUMin>;
defm : DemangledNativeBuiltin<"__spirv_AtomicUMax", OpenCL_std, Atomic, 4, 4, OpAtomicUMax>;

// Barrier builtin records:
defm : DemangledNativeBuiltin<"barrier", OpenCL_std, Barrier, 1, 3, OpControlBarrier>;
defm : DemangledNativeBuiltin<"work_group_barrier", OpenCL_std, Barrier, 1, 3, OpControlBarrier>;
defm : DemangledNativeBuiltin<"__spirv_ControlBarrier", OpenCL_std, Barrier, 3, 3, OpControlBarrier>;

// Kernel enqueue builtin records:
defm : DemangledNativeBuiltin<"__enqueue_kernel_basic", OpenCL_std, Enqueue, 5, 5, OpEnqueueKernel>;
defm : DemangledNativeBuiltin<"__enqueue_kernel_basic_events", OpenCL_std, Enqueue, 8, 8, OpEnqueueKernel>;
defm : DemangledNativeBuiltin<"__enqueue_kernel_varargs", OpenCL_std, Enqueue, 7, 7, OpEnqueueKernel>;
defm : DemangledNativeBuiltin<"__enqueue_kernel_events_varargs", OpenCL_std, Enqueue, 10, 10, OpEnqueueKernel>;
defm : DemangledNativeBuiltin<"__spirv_EnqueueKernel", OpenCL_std, Enqueue, 10, 0, OpEnqueueKernel>;
defm : DemangledNativeBuiltin<"retain_event", OpenCL_std, Enqueue, 1, 1, OpRetainEvent>;
defm : DemangledNativeBuiltin<"__spirv_RetainEvent", OpenCL_std, Enqueue, 1, 1, OpRetainEvent>;
defm : DemangledNativeBuiltin<"release_event", OpenCL_std, Enqueue, 1, 1, OpReleaseEvent>;
defm : DemangledNativeBuiltin<"__spirv_ReleaseEvent", OpenCL_std, Enqueue, 1, 1, OpReleaseEvent>;
defm : DemangledNativeBuiltin<"create_user_event", OpenCL_std, Enqueue, 0, 0, OpCreateUserEvent>;
defm : DemangledNativeBuiltin<"__spirv_CreateUserEvent", OpenCL_std, Enqueue, 0, 0, OpCreateUserEvent>;
defm : DemangledNativeBuiltin<"is_valid_event", OpenCL_std, Enqueue, 1, 1, OpIsValidEvent>;
defm : DemangledNativeBuiltin<"__spirv_IsValidEvent", OpenCL_std, Enqueue, 1, 1, OpIsValidEvent>;
defm : DemangledNativeBuiltin<"set_user_event_status", OpenCL_std, Enqueue, 2, 2, OpSetUserEventStatus>;
defm : DemangledNativeBuiltin<"__spirv_SetUserEventStatus", OpenCL_std, Enqueue, 2, 2, OpSetUserEventStatus>;
defm : DemangledNativeBuiltin<"capture_event_profiling_info", OpenCL_std, Enqueue, 3, 3, OpCaptureEventProfilingInfo>;
defm : DemangledNativeBuiltin<"__spirv_CaptureEventProfilingInfo", OpenCL_std, Enqueue, 3, 3, OpCaptureEventProfilingInfo>;
defm : DemangledNativeBuiltin<"get_default_queue", OpenCL_std, Enqueue, 0, 0, OpGetDefaultQueue>;
defm : DemangledNativeBuiltin<"__spirv_GetDefaultQueue", OpenCL_std, Enqueue, 0, 0, OpGetDefaultQueue>;
defm : DemangledNativeBuiltin<"ndrange_1D", OpenCL_std, Enqueue, 1, 3, OpBuildNDRange>;
defm : DemangledNativeBuiltin<"ndrange_2D", OpenCL_std, Enqueue, 1, 3, OpBuildNDRange>;
defm : DemangledNativeBuiltin<"ndrange_3D", OpenCL_std, Enqueue, 1, 3, OpBuildNDRange>;

// Spec constant builtin records:
defm : DemangledNativeBuiltin<"__spirv_SpecConstant", OpenCL_std, SpecConstant, 2, 2, OpSpecConstant>;
defm : DemangledNativeBuiltin<"__spirv_SpecConstantComposite", OpenCL_std, SpecConstant, 1, 0, OpSpecConstantComposite>;

// Async Copy and Prefetch builtin records:
defm : DemangledNativeBuiltin<"async_work_group_copy", OpenCL_std, AsyncCopy, 4, 4, OpGroupAsyncCopy>;
defm : DemangledNativeBuiltin<"async_work_group_strided_copy", OpenCL_std, AsyncCopy, 5, 5, OpGroupAsyncCopy>;
defm : DemangledNativeBuiltin<"__spirv_GroupAsyncCopy", OpenCL_std, AsyncCopy, 6, 6, OpGroupAsyncCopy>;
defm : DemangledNativeBuiltin<"wait_group_events", OpenCL_std, AsyncCopy, 2, 2, OpGroupWaitEvents>;
defm : DemangledNativeBuiltin<"__spirv_GroupWaitEvents", OpenCL_std, AsyncCopy, 3, 3, OpGroupWaitEvents>;

// Load and store builtin records:
defm : DemangledNativeBuiltin<"__spirv_Load", OpenCL_std, LoadStore, 1, 3, OpLoad>;
defm : DemangledNativeBuiltin<"__spirv_Store", OpenCL_std, LoadStore, 2, 4, OpStore>;

// Address Space Qualifier Functions/Pointers Conversion Instructions:
defm : DemangledNativeBuiltin<"to_global", OpenCL_std, CastToPtr, 1, 1, OpGenericCastToPtr>;
defm : DemangledNativeBuiltin<"to_local", OpenCL_std, CastToPtr, 1, 1, OpGenericCastToPtr>;
defm : DemangledNativeBuiltin<"to_private", OpenCL_std, CastToPtr, 1, 1, OpGenericCastToPtr>;
defm : DemangledNativeBuiltin<"__spirv_GenericCastToPtr_ToGlobal", OpenCL_std, CastToPtr, 2, 2, OpGenericCastToPtr>;
defm : DemangledNativeBuiltin<"__spirv_GenericCastToPtr_ToLocal", OpenCL_std, CastToPtr, 2, 2, OpGenericCastToPtr>;
defm : DemangledNativeBuiltin<"__spirv_GenericCastToPtr_ToPrivate", OpenCL_std, CastToPtr, 2, 2, OpGenericCastToPtr>;
defm : DemangledNativeBuiltin<"__spirv_GenericCastToPtrExplicit_ToGlobal", OpenCL_std, CastToPtr, 2, 2, OpGenericCastToPtr>;
defm : DemangledNativeBuiltin<"__spirv_GenericCastToPtrExplicit_ToLocal", OpenCL_std, CastToPtr, 2, 2, OpGenericCastToPtr>;
defm : DemangledNativeBuiltin<"__spirv_GenericCastToPtrExplicit_ToPrivate", OpenCL_std, CastToPtr, 2, 2, OpGenericCastToPtr>;

// Cooperative Matrix builtin records:
defm : DemangledNativeBuiltin<"__spirv_CooperativeMatrixLoadKHR", OpenCL_std, CoopMatr, 2, 0, OpCooperativeMatrixLoadKHR>;
defm : DemangledNativeBuiltin<"__spirv_CooperativeMatrixStoreKHR", OpenCL_std, CoopMatr, 3, 0, OpCooperativeMatrixStoreKHR>;
defm : DemangledNativeBuiltin<"__spirv_CooperativeMatrixMulAddKHR", OpenCL_std, CoopMatr, 3, 0, OpCooperativeMatrixMulAddKHR>;
defm : DemangledNativeBuiltin<"__spirv_CooperativeMatrixLengthKHR", OpenCL_std, CoopMatr, 1, 1, OpCooperativeMatrixLengthKHR>;

//===----------------------------------------------------------------------===//
// Class defining a work/sub group builtin that should be translated into a
// SPIR-V instruction using the defined properties.
//
// name is the demangled name of the given builtin.
// opcode specifies the SPIR-V operation code of the generated instruction.
//===----------------------------------------------------------------------===//
class GroupBuiltin<string name, Op operation> {
  string Name = name;
  Op Opcode = operation;
  bits<32> GroupOperation = !cond(!not(!eq(!find(name, "group_reduce"), -1)) : Reduce.Value,
                                  !not(!eq(!find(name, "group_scan_inclusive"), -1)) : InclusiveScan.Value,
                                  !not(!eq(!find(name, "group_scan_exclusive"), -1)) : ExclusiveScan.Value,
                                  !not(!eq(!find(name, "group_ballot_bit_count"), -1)) : Reduce.Value,
                                  !not(!eq(!find(name, "group_ballot_inclusive_scan"), -1)) : InclusiveScan.Value,
                                  !not(!eq(!find(name, "group_ballot_exclusive_scan"), -1)) : ExclusiveScan.Value,
                                  !not(!eq(!find(name, "group_non_uniform_reduce"), -1)) : Reduce.Value,
                                  !not(!eq(!find(name, "group_non_uniform_scan_inclusive"), -1)) : InclusiveScan.Value,
                                  !not(!eq(!find(name, "group_non_uniform_scan_exclusive"), -1)) : ExclusiveScan.Value,
                                  !not(!eq(!find(name, "group_non_uniform_reduce_logical"), -1)) : Reduce.Value,
                                  !not(!eq(!find(name, "group_non_uniform_scan_inclusive_logical"), -1)) : InclusiveScan.Value,
                                  !not(!eq(!find(name, "group_non_uniform_scan_exclusive_logical"), -1)) : ExclusiveScan.Value,
                                  !not(!eq(!find(name, "group_clustered_reduce"), -1)) : ClusteredReduce.Value,
                                  !not(!eq(!find(name, "group_clustered_reduce_logical"), -1)) : ClusteredReduce.Value,
                                  true : 0);
  bit IsElect = !eq(operation, OpGroupNonUniformElect);
  bit IsAllOrAny = !or(!eq(operation, OpGroupAll),
                       !eq(operation, OpGroupAny),
                       !eq(operation, OpGroupNonUniformAll),
                       !eq(operation, OpGroupNonUniformAny));
  bit IsAllEqual = !eq(operation, OpGroupNonUniformAllEqual);
  bit IsBallot = !eq(operation, OpGroupNonUniformBallot);
  bit IsInverseBallot = !eq(operation, OpGroupNonUniformInverseBallot);
  bit IsBallotBitExtract = !eq(operation, OpGroupNonUniformBallotBitExtract);
  bit IsBallotFindBit = !or(!eq(operation, OpGroupNonUniformBallotFindLSB),
                            !eq(operation, OpGroupNonUniformBallotFindMSB));
  bit IsLogical = !or(!eq(operation, OpGroupNonUniformLogicalAnd),
                      !eq(operation, OpGroupNonUniformLogicalOr),
                      !eq(operation, OpGroupNonUniformLogicalXor),
                      !eq(operation, OpGroupLogicalAndKHR),
                      !eq(operation, OpGroupLogicalOrKHR),
                      !eq(operation, OpGroupLogicalXorKHR));
  bit NoGroupOperation = !or(IsElect, IsAllOrAny, IsAllEqual,
                             IsBallot, IsInverseBallot,
                             IsBallotBitExtract, IsBallotFindBit,
                             !eq(operation, OpGroupNonUniformShuffle),
                             !eq(operation, OpGroupNonUniformShuffleXor),
                             !eq(operation, OpGroupNonUniformShuffleUp),
                             !eq(operation, OpGroupNonUniformShuffleDown),
                             !eq(operation, OpGroupBroadcast),
                             !eq(operation, OpGroupNonUniformBroadcast),
                             !eq(operation, OpGroupNonUniformBroadcastFirst),
                             !eq(operation, OpGroupNonUniformRotateKHR));
  bit HasBoolArg = !or(!and(IsAllOrAny, !eq(IsAllEqual, false)), IsBallot, IsLogical);
}

// Table gathering all the work/sub group builtins.
def GroupBuiltins : GenericTable {
  let FilterClass = "GroupBuiltin";
  let Fields = ["Name", "Opcode", "GroupOperation", "IsElect", "IsAllOrAny",
                "IsAllEqual", "IsBallot", "IsInverseBallot", "IsBallotBitExtract",
                "IsBallotFindBit", "IsLogical", "NoGroupOperation", "HasBoolArg"];
}

// Function to lookup group builtins by their name and set.
def lookupGroupBuiltin : SearchIndex {
  let Table = GroupBuiltins;
  let Key = ["Name"];
}

// Multiclass used to define at the same time both incoming builtin records
// and corresponding work/sub group builtin records.
defvar OnlyWork = 0; defvar OnlySub = 1; defvar WorkOrSub = 2;
multiclass DemangledGroupBuiltin<string name, int level /* OnlyWork/OnlySub/... */, Op operation> {
  assert !and(!ge(level, 0), !le(level, 2)), "group level is invalid: " # level;

  if !or(!eq(level, OnlyWork), !eq(level, WorkOrSub)) then {
    def : DemangledBuiltin<!strconcat("work_", name), OpenCL_std, Group, 0, 4>;
    def : GroupBuiltin<!strconcat("work_", name), operation>;
  }

  if !or(!eq(level, OnlySub), !eq(level, WorkOrSub)) then {
    def : DemangledBuiltin<!strconcat("sub_", name), OpenCL_std, Group, 0, 4>;
    def : GroupBuiltin<!strconcat("sub_", name), operation>;
  }
}

multiclass DemangledGroupBuiltinWrapper<string name, bits<8> minNumArgs, bits<8> maxNumArgs, Op operation> {
    def : DemangledBuiltin<name, OpenCL_std, Group, minNumArgs, maxNumArgs>;
    def : GroupBuiltin<name, operation>;
}

defm : DemangledGroupBuiltin<"group_all", WorkOrSub, OpGroupAll>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupAll", 2, 2, OpGroupAll>;
defm : DemangledGroupBuiltin<"group_any", WorkOrSub, OpGroupAny>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupAny", 2, 2, OpGroupAny>;
defm : DemangledGroupBuiltin<"group_broadcast", WorkOrSub, OpGroupBroadcast>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupBroadcast", 3, 3, OpGroupBroadcast>;
defm : DemangledGroupBuiltin<"group_non_uniform_broadcast", OnlySub, OpGroupNonUniformBroadcast>;
defm : DemangledGroupBuiltin<"group_broadcast_first", OnlySub, OpGroupNonUniformBroadcastFirst>;

// cl_khr_subgroup_non_uniform_vote
defm : DemangledGroupBuiltin<"group_elect", OnlySub, OpGroupNonUniformElect>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformElect", 1, 1, OpGroupNonUniformElect>;
defm : DemangledGroupBuiltin<"group_non_uniform_all", OnlySub, OpGroupNonUniformAll>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformAll", 2, 2, OpGroupNonUniformAll>;
defm : DemangledGroupBuiltin<"group_non_uniform_any", OnlySub, OpGroupNonUniformAny>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformAny", 2, 2, OpGroupNonUniformAny>;
defm : DemangledGroupBuiltin<"group_non_uniform_all_equal", OnlySub, OpGroupNonUniformAllEqual>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformAllEqual", 2, 2, OpGroupNonUniformAllEqual>;

// cl_khr_subgroup_ballot
defm : DemangledGroupBuiltin<"group_ballot", OnlySub, OpGroupNonUniformBallot>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformBallot", 2, 2, OpGroupNonUniformBallot>;
defm : DemangledGroupBuiltin<"group_inverse_ballot", OnlySub, OpGroupNonUniformInverseBallot>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformInverseBallot", 2, 2, OpGroupNonUniformInverseBallot>;
defm : DemangledGroupBuiltin<"group_ballot_bit_extract", OnlySub, OpGroupNonUniformBallotBitExtract>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformBallotBitExtract", 3, 3, OpGroupNonUniformBallotBitExtract>;
defm : DemangledGroupBuiltin<"group_ballot_bit_count", OnlySub, OpGroupNonUniformBallotBitCount>;
defm : DemangledGroupBuiltin<"group_ballot_inclusive_scan", OnlySub, OpGroupNonUniformBallotBitCount>;
defm : DemangledGroupBuiltin<"group_ballot_exclusive_scan", OnlySub, OpGroupNonUniformBallotBitCount>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformBallotBitCount", 3, 3, OpGroupNonUniformBallotBitCount>;
defm : DemangledGroupBuiltin<"group_ballot_find_lsb", OnlySub, OpGroupNonUniformBallotFindLSB>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformBallotFindLSB", 2, 2, OpGroupNonUniformBallotFindLSB>;
defm : DemangledGroupBuiltin<"group_ballot_find_msb", OnlySub, OpGroupNonUniformBallotFindMSB>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformBallotFindMSB", 2, 2, OpGroupNonUniformBallotFindMSB>;

// cl_khr_subgroup_shuffle
defm : DemangledGroupBuiltin<"group_shuffle", OnlySub, OpGroupNonUniformShuffle>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformShuffle", 3, 3, OpGroupNonUniformShuffle>;
defm : DemangledGroupBuiltin<"group_shuffle_xor", OnlySub, OpGroupNonUniformShuffleXor>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformShuffleXor", 3, 3, OpGroupNonUniformShuffleXor>;

// cl_khr_subgroup_shuffle_relative
defm : DemangledGroupBuiltin<"group_shuffle_up", OnlySub, OpGroupNonUniformShuffleUp>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformShuffleUp", 3, 3, OpGroupNonUniformShuffleUp>;
defm : DemangledGroupBuiltin<"group_shuffle_down", OnlySub, OpGroupNonUniformShuffleDown>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformShuffleDown", 3, 3, OpGroupNonUniformShuffleDown>;

defm : DemangledGroupBuiltin<"group_iadd", WorkOrSub, OpGroupIAdd>;
defm : DemangledGroupBuiltin<"group_reduce_adds", WorkOrSub, OpGroupIAdd>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_adds", WorkOrSub, OpGroupIAdd>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_adds", WorkOrSub, OpGroupIAdd>;
defm : DemangledGroupBuiltin<"group_reduce_addu", WorkOrSub, OpGroupIAdd>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_addu", WorkOrSub, OpGroupIAdd>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_addu", WorkOrSub, OpGroupIAdd>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupIAdd", 3, 3, OpGroupIAdd>;

defm : DemangledGroupBuiltin<"group_fadd", WorkOrSub, OpGroupFAdd>;
defm : DemangledGroupBuiltin<"group_reduce_addf", WorkOrSub, OpGroupFAdd>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_addf", WorkOrSub, OpGroupFAdd>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_addf", WorkOrSub, OpGroupFAdd>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupFAdd", 3, 3, OpGroupFAdd>;

defm : DemangledGroupBuiltin<"group_fmin", WorkOrSub, OpGroupFMin>;
defm : DemangledGroupBuiltin<"group_reduce_minf", WorkOrSub, OpGroupFMin>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_minf", WorkOrSub, OpGroupFMin>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_minf", WorkOrSub, OpGroupFMin>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupFMin", 3, 3, OpGroupFMin>;

defm : DemangledGroupBuiltin<"group_umin", WorkOrSub, OpGroupUMin>;
defm : DemangledGroupBuiltin<"group_reduce_minu", WorkOrSub, OpGroupUMin>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_minu", WorkOrSub, OpGroupUMin>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_minu", WorkOrSub, OpGroupUMin>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupUMin", 3, 3, OpGroupUMin>;

defm : DemangledGroupBuiltin<"group_smin", WorkOrSub, OpGroupSMin>;
defm : DemangledGroupBuiltin<"group_reduce_mins", WorkOrSub, OpGroupSMin>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_mins", WorkOrSub, OpGroupSMin>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_mins", WorkOrSub, OpGroupSMin>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupSMin", 3, 3, OpGroupSMin>;

defm : DemangledGroupBuiltin<"group_fmax", WorkOrSub, OpGroupFMax>;
defm : DemangledGroupBuiltin<"group_reduce_maxf", WorkOrSub, OpGroupFMax>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_maxf", WorkOrSub, OpGroupFMax>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_maxf", WorkOrSub, OpGroupFMax>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupFMax", 3, 3, OpGroupFMax>;

defm : DemangledGroupBuiltin<"group_umax", WorkOrSub, OpGroupUMax>;
defm : DemangledGroupBuiltin<"group_reduce_maxu", WorkOrSub, OpGroupUMax>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_maxu", WorkOrSub, OpGroupUMax>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_maxu", WorkOrSub, OpGroupUMax>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupUMax", 3, 3, OpGroupUMax>;

defm : DemangledGroupBuiltin<"group_smax", WorkOrSub, OpGroupSMax>;
defm : DemangledGroupBuiltin<"group_reduce_maxs", WorkOrSub, OpGroupSMax>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_maxs", WorkOrSub, OpGroupSMax>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_maxs", WorkOrSub, OpGroupSMax>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupSMax", 3, 3, OpGroupSMax>;

// cl_khr_subgroup_non_uniform_arithmetic
defm : DemangledGroupBuiltin<"group_non_uniform_iadd", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_addu", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_adds", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_addu", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_adds", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_addu", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_adds", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_addu", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_adds", WorkOrSub, OpGroupNonUniformIAdd>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformIAdd", 3, 4, OpGroupNonUniformIAdd>;

defm : DemangledGroupBuiltin<"group_non_uniform_fadd", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_addf", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_addh", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_addd", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_addf", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_addh", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_addd", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_addf", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_addh", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_addd", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_addf", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_addh", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_addd", WorkOrSub, OpGroupNonUniformFAdd>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformFAdd", 3, 4, OpGroupNonUniformFAdd>;

defm : DemangledGroupBuiltin<"group_non_uniform_imul", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_mulu", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_muls", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_mulu", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_muls", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_mulu", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_muls", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_mulu", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_muls", WorkOrSub, OpGroupNonUniformIMul>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformIMul", 3, 4, OpGroupNonUniformIMul>;

defm : DemangledGroupBuiltin<"group_non_uniform_fmul", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_mulf", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_mulh", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_muld", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_mulf", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_mulh", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_muld", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_mulf", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_mulh", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_muld", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_mulf", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_mulh", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_muld", WorkOrSub, OpGroupNonUniformFMul>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformFMul", 3, 4, OpGroupNonUniformFMul>;

defm : DemangledGroupBuiltin<"group_non_uniform_smin", WorkOrSub, OpGroupNonUniformSMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_mins", WorkOrSub, OpGroupNonUniformSMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_mins", WorkOrSub, OpGroupNonUniformSMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_mins", WorkOrSub, OpGroupNonUniformSMin>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_mins", WorkOrSub, OpGroupNonUniformSMin>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformSMin", 3, 4, OpGroupNonUniformSMin>;

defm : DemangledGroupBuiltin<"group_non_uniform_umin", WorkOrSub, OpGroupNonUniformUMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_minu", WorkOrSub, OpGroupNonUniformUMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_minu", WorkOrSub, OpGroupNonUniformUMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_minu", WorkOrSub, OpGroupNonUniformUMin>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_minu", WorkOrSub, OpGroupNonUniformUMin>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformUMin", 3, 4, OpGroupNonUniformUMin>;

defm : DemangledGroupBuiltin<"group_non_uniform_fmin", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_minf", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_minh", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_mind", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_minf", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_minh", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_mind", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_minf", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_minh", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_mind", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_minf", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_minh", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_mind", WorkOrSub, OpGroupNonUniformFMin>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformFMin", 3, 4, OpGroupNonUniformFMin>;

defm : DemangledGroupBuiltin<"group_non_uniform_smax", WorkOrSub, OpGroupNonUniformSMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_maxs", WorkOrSub, OpGroupNonUniformSMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_maxs", WorkOrSub, OpGroupNonUniformSMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_maxs", WorkOrSub, OpGroupNonUniformSMax>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_maxs", WorkOrSub, OpGroupNonUniformSMax>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformSMax", 3, 4, OpGroupNonUniformSMax>;

defm : DemangledGroupBuiltin<"group_non_uniform_umax", WorkOrSub, OpGroupNonUniformUMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_maxu", WorkOrSub, OpGroupNonUniformUMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_maxu", WorkOrSub, OpGroupNonUniformUMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_maxu", WorkOrSub, OpGroupNonUniformUMax>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_maxu", WorkOrSub, OpGroupNonUniformUMax>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformUMax", 3, 4, OpGroupNonUniformUMax>;

defm : DemangledGroupBuiltin<"group_non_uniform_fmax", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_maxf", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_maxh", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_maxd", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_maxf", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_maxh", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_maxd", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_maxf", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_maxh", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_maxd", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_maxf", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_maxh", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_maxd", WorkOrSub, OpGroupNonUniformFMax>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformFMax", 3, 4, OpGroupNonUniformFMax>;

defm : DemangledGroupBuiltin<"group_non_uniform_iand", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_andu", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_ands", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_andu", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_ands", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_andu", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_ands", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_andu", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_ands", WorkOrSub, OpGroupNonUniformBitwiseAnd>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformBitwiseAnd", 3, 4, OpGroupNonUniformBitwiseAnd>;

defm : DemangledGroupBuiltin<"group_non_uniform_ior", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_oru", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_ors", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_oru", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_ors", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_oru", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_ors", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_oru", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_ors", WorkOrSub, OpGroupNonUniformBitwiseOr>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformBitwiseOr", 3, 4, OpGroupNonUniformBitwiseOr>;

defm : DemangledGroupBuiltin<"group_non_uniform_ixor", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_xoru", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_xors", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_xoru", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_xors", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_xoru", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_xors", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_xoru", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_xors", WorkOrSub, OpGroupNonUniformBitwiseXor>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformBitwiseXor", 3, 4, OpGroupNonUniformBitwiseXor>;

defm : DemangledGroupBuiltin<"group_non_uniform_logical_iand", WorkOrSub, OpGroupNonUniformLogicalAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_logical_ands", WorkOrSub, OpGroupNonUniformLogicalAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_logical_ands", WorkOrSub, OpGroupNonUniformLogicalAnd>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_logical_ands", WorkOrSub, OpGroupNonUniformLogicalAnd>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_logical_and", WorkOrSub, OpGroupNonUniformLogicalAnd>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformLogicalAnd", 3, 4, OpGroupNonUniformLogicalAnd>;

defm : DemangledGroupBuiltin<"group_non_uniform_logical_ior", WorkOrSub, OpGroupNonUniformLogicalOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_logical_ors", WorkOrSub, OpGroupNonUniformLogicalOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_logical_ors", WorkOrSub, OpGroupNonUniformLogicalOr>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_logical_ors", WorkOrSub, OpGroupNonUniformLogicalOr>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_logical_or", WorkOrSub, OpGroupNonUniformLogicalOr>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformLogicalOr", 3, 4, OpGroupNonUniformLogicalOr>;

defm : DemangledGroupBuiltin<"group_non_uniform_logical_ixor", WorkOrSub, OpGroupNonUniformLogicalXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_reduce_logical_xors", WorkOrSub, OpGroupNonUniformLogicalXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_inclusive_logical_xors", WorkOrSub, OpGroupNonUniformLogicalXor>;
defm : DemangledGroupBuiltin<"group_non_uniform_scan_exclusive_logical_xors", WorkOrSub, OpGroupNonUniformLogicalXor>;
defm : DemangledGroupBuiltin<"group_clustered_reduce_logical_xor", WorkOrSub, OpGroupNonUniformLogicalXor>;
defm : DemangledGroupBuiltinWrapper<"__spirv_GroupNonUniformLogicalXor", 3, 4, OpGroupNonUniformLogicalXor>;

// cl_khr_subgroup_rotate / SPV_KHR_subgroup_rotate
defm : DemangledGroupBuiltin<"group_rotate", OnlySub, OpGroupNonUniformRotateKHR>;
defm : DemangledGroupBuiltin<"group_clustered_rotate", OnlySub, OpGroupNonUniformRotateKHR>;

// cl_khr_work_group_uniform_arithmetic / SPV_KHR_uniform_group_instructions
defm : DemangledGroupBuiltin<"group_reduce_imul", OnlyWork, OpGroupIMulKHR>;
defm : DemangledGroupBuiltin<"group_reduce_mulu", OnlyWork, OpGroupIMulKHR>;
defm : DemangledGroupBuiltin<"group_reduce_muls", OnlyWork, OpGroupIMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_imul", OnlyWork, OpGroupIMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_mulu", OnlyWork, OpGroupIMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_muls", OnlyWork, OpGroupIMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_imul", OnlyWork, OpGroupIMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_mulu", OnlyWork, OpGroupIMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_muls", OnlyWork, OpGroupIMulKHR>;

defm : DemangledGroupBuiltin<"group_reduce_mulf", OnlyWork, OpGroupFMulKHR>;
defm : DemangledGroupBuiltin<"group_reduce_mulh", OnlyWork, OpGroupFMulKHR>;
defm : DemangledGroupBuiltin<"group_reduce_muld", OnlyWork, OpGroupFMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_mulf", OnlyWork, OpGroupFMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_mulh", OnlyWork, OpGroupFMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_muld", OnlyWork, OpGroupFMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_mulf", OnlyWork, OpGroupFMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_mulh", OnlyWork, OpGroupFMulKHR>;
defm : DemangledGroupBuiltin<"group_scan_exclusive_muld", OnlyWork, OpGroupFMulKHR>;

defm : DemangledGroupBuiltin<"group_scan_exclusive_and", OnlyWork, OpGroupBitwiseAndKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_and", OnlyWork, OpGroupBitwiseAndKHR>;
defm : DemangledGroupBuiltin<"group_reduce_and", OnlyWork, OpGroupBitwiseAndKHR>;

defm : DemangledGroupBuiltin<"group_scan_exclusive_or", OnlyWork, OpGroupBitwiseOrKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_or", OnlyWork, OpGroupBitwiseOrKHR>;
defm : DemangledGroupBuiltin<"group_reduce_or", OnlyWork, OpGroupBitwiseOrKHR>;

defm : DemangledGroupBuiltin<"group_scan_exclusive_xor", OnlyWork, OpGroupBitwiseXorKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_xor", OnlyWork, OpGroupBitwiseXorKHR>;
defm : DemangledGroupBuiltin<"group_reduce_xor", OnlyWork, OpGroupBitwiseXorKHR>;

defm : DemangledGroupBuiltin<"group_scan_exclusive_logical_and", OnlyWork, OpGroupLogicalAndKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_logical_and", OnlyWork, OpGroupLogicalAndKHR>;
defm : DemangledGroupBuiltin<"group_reduce_logical_and", OnlyWork, OpGroupLogicalAndKHR>;

defm : DemangledGroupBuiltin<"group_scan_exclusive_logical_or", OnlyWork, OpGroupLogicalOrKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_logical_or", OnlyWork, OpGroupLogicalOrKHR>;
defm : DemangledGroupBuiltin<"group_reduce_logical_or", OnlyWork, OpGroupLogicalOrKHR>;

defm : DemangledGroupBuiltin<"group_scan_exclusive_logical_xor", OnlyWork, OpGroupLogicalXorKHR>;
defm : DemangledGroupBuiltin<"group_scan_inclusive_logical_xor", OnlyWork, OpGroupLogicalXorKHR>;
defm : DemangledGroupBuiltin<"group_reduce_logical_xor", OnlyWork, OpGroupLogicalXorKHR>;

// cl_khr_kernel_clock / SPV_KHR_shader_clock
defm : DemangledNativeBuiltin<"clock_read_device", OpenCL_std, KernelClock, 0, 0, OpReadClockKHR>;
defm : DemangledNativeBuiltin<"clock_read_work_group", OpenCL_std, KernelClock, 0, 0, OpReadClockKHR>;
defm : DemangledNativeBuiltin<"clock_read_sub_group", OpenCL_std, KernelClock, 0, 0, OpReadClockKHR>;
defm : DemangledNativeBuiltin<"clock_read_hilo_device", OpenCL_std, KernelClock, 0, 0, OpReadClockKHR>;
defm : DemangledNativeBuiltin<"clock_read_hilo_work_group", OpenCL_std, KernelClock, 0, 0, OpReadClockKHR>;
defm : DemangledNativeBuiltin<"clock_read_hilo_sub_group", OpenCL_std, KernelClock, 0, 0, OpReadClockKHR>;

//===----------------------------------------------------------------------===//
// Class defining an atomic instruction on floating-point numbers.
//
// name is the demangled name of the given builtin.
// opcode specifies the SPIR-V operation code of the generated instruction.
//===----------------------------------------------------------------------===//
class AtomicFloatingBuiltin<string name, Op operation> {
  string Name = name;
  Op Opcode = operation;
}

// Table gathering all builtins for atomic instructions on floating-point numbers
def AtomicFloatingBuiltins : GenericTable {
  let FilterClass = "AtomicFloatingBuiltin";
  let Fields = ["Name", "Opcode"];
}

// Function to lookup builtins by their name and set.
def lookupAtomicFloatingBuiltin : SearchIndex {
  let Table = AtomicFloatingBuiltins;
  let Key = ["Name"];
}

// Multiclass used to define incoming demangled builtin records and
// corresponding builtin records for atomic instructions on floating-point numbers.
multiclass DemangledAtomicFloatingBuiltin<string name, bits<8> minNumArgs, bits<8> maxNumArgs, Op operation> {
  def : DemangledBuiltin<!strconcat("__spirv_AtomicF", name), OpenCL_std, AtomicFloating, minNumArgs, maxNumArgs>;
  def : AtomicFloatingBuiltin<!strconcat("__spirv_AtomicF", name), operation>;
}

// SPV_EXT_shader_atomic_float_add, SPV_EXT_shader_atomic_float_min_max, SPV_EXT_shader_atomic_float16_add
// Atomic add, min and max instruction on floating-point numbers:
defm : DemangledAtomicFloatingBuiltin<"AddEXT", 4, 4, OpAtomicFAddEXT>;
defm : DemangledAtomicFloatingBuiltin<"MinEXT", 4, 4, OpAtomicFMinEXT>;
defm : DemangledAtomicFloatingBuiltin<"MaxEXT", 4, 4, OpAtomicFMaxEXT>;

//===----------------------------------------------------------------------===//
// Class defining a sub group builtin that should be translated into a
// SPIR-V instruction using the SPV_INTEL_subgroups extension.
//
// name is the demangled name of the given builtin.
// opcode specifies the SPIR-V operation code of the generated instruction.
//===----------------------------------------------------------------------===//
class IntelSubgroupsBuiltin<string name, Op operation> {
  string Name = name;
  Op Opcode = operation;
  bit IsBlock = !or(!eq(operation, OpSubgroupBlockReadINTEL),
                    !eq(operation, OpSubgroupBlockWriteINTEL));
  bit IsWrite = !eq(operation, OpSubgroupBlockWriteINTEL);
}

// Table gathering all the Intel sub group builtins.
def IntelSubgroupsBuiltins : GenericTable {
  let FilterClass = "IntelSubgroupsBuiltin";
  let Fields = ["Name", "Opcode", "IsBlock", "IsWrite"];
}

// Function to lookup group builtins by their name and set.
def lookupIntelSubgroupsBuiltin : SearchIndex {
  let Table = IntelSubgroupsBuiltins;
  let Key = ["Name"];
}

// Multiclass used to define incoming builtin records for the SPV_INTEL_subgroups extension
// and corresponding work/sub group builtin records.
multiclass DemangledIntelSubgroupsBuiltin<string name, bits<8> minNumArgs, bits<8> maxNumArgs, Op operation> {
  def : DemangledBuiltin<!strconcat("intel_sub_group_", name), OpenCL_std, IntelSubgroups, minNumArgs, maxNumArgs>;
  def : IntelSubgroupsBuiltin<!strconcat("intel_sub_group_", name), operation>;
}

// cl_intel_subgroups
defm : DemangledIntelSubgroupsBuiltin<"shuffle", 2, 2, OpSubgroupShuffleINTEL>;
defm : DemangledIntelSubgroupsBuiltin<"shuffle_down", 3, 3, OpSubgroupShuffleDownINTEL>;
defm : DemangledIntelSubgroupsBuiltin<"shuffle_up", 3, 3, OpSubgroupShuffleUpINTEL>;
defm : DemangledIntelSubgroupsBuiltin<"shuffle_xor", 2, 2, OpSubgroupShuffleXorINTEL>;
foreach i = ["", "2", "4", "8"] in {
  // cl_intel_subgroups
  defm : DemangledIntelSubgroupsBuiltin<!strconcat("block_read",  i), 1, 2, OpSubgroupBlockReadINTEL>;
  defm : DemangledIntelSubgroupsBuiltin<!strconcat("block_write", i), 2, 3, OpSubgroupBlockWriteINTEL>;
  // cl_intel_subgroups_short
  defm : DemangledIntelSubgroupsBuiltin<!strconcat("block_read_ui",  i), 1, 2, OpSubgroupBlockReadINTEL>;
  defm : DemangledIntelSubgroupsBuiltin<!strconcat("block_write_ui", i), 2, 3, OpSubgroupBlockWriteINTEL>;
}
// cl_intel_subgroups_char, cl_intel_subgroups_short, cl_intel_subgroups_long
foreach i = ["", "2", "4", "8", "16"] in {
  foreach j = ["c", "s", "l"] in {
    defm : DemangledIntelSubgroupsBuiltin<!strconcat("block_read_u", j,  i), 1, 2, OpSubgroupBlockReadINTEL>;
    defm : DemangledIntelSubgroupsBuiltin<!strconcat("block_write_u", j, i), 2, 3, OpSubgroupBlockWriteINTEL>;
  }
}
// OpSubgroupImageBlockReadINTEL and OpSubgroupImageBlockWriteINTEL are to be resolved later on (in code)

// Multiclass used to define builtin wrappers for the SPV_INTEL_subgroups extension.
multiclass DemangledIntelSubgroupsBuiltinWrapper<string name, bits<8> numArgs, Op operation> {
  def : DemangledBuiltin<!strconcat("__spirv_", name), OpenCL_std, IntelSubgroups, numArgs, numArgs>;
  def : IntelSubgroupsBuiltin<!strconcat("__spirv_", name), operation>;
}

defm : DemangledIntelSubgroupsBuiltinWrapper<"SubgroupShuffleINTEL", 2, OpSubgroupShuffleINTEL>;
defm : DemangledIntelSubgroupsBuiltinWrapper<"SubgroupShuffleDownINTEL", 3, OpSubgroupShuffleDownINTEL>;
defm : DemangledIntelSubgroupsBuiltinWrapper<"SubgroupShuffleUpINTEL", 3, OpSubgroupShuffleUpINTEL>;
defm : DemangledIntelSubgroupsBuiltinWrapper<"SubgroupShuffleXorINTEL", 2, OpSubgroupShuffleXorINTEL>;
defm : DemangledIntelSubgroupsBuiltinWrapper<"SubgroupBlockReadINTEL", 1, OpSubgroupBlockReadINTEL>;
defm : DemangledIntelSubgroupsBuiltinWrapper<"SubgroupBlockWriteINTEL", 2, OpSubgroupBlockWriteINTEL>;
defm : DemangledIntelSubgroupsBuiltinWrapper<"SubgroupImageBlockReadINTEL", 2, OpSubgroupImageBlockReadINTEL>;
defm : DemangledIntelSubgroupsBuiltinWrapper<"SubgroupImageBlockWriteINTEL", 3, OpSubgroupImageBlockWriteINTEL>;

//===----------------------------------------------------------------------===//
// Class defining a builtin for group operations within uniform control flow.
// It should be translated into a SPIR-V instruction using
// the SPV_KHR_uniform_group_instructions extension.
//
// name is the demangled name of the given builtin.
// opcode specifies the SPIR-V operation code of the generated instruction.
//===----------------------------------------------------------------------===//
class GroupUniformBuiltin<string name, Op operation> {
  string Name = name;
  Op Opcode = operation;
  bit IsLogical = !or(!eq(operation, OpGroupLogicalAndKHR),
                      !eq(operation, OpGroupLogicalOrKHR),
                      !eq(operation, OpGroupLogicalXorKHR));
}

// Table gathering all the Intel sub group builtins.
def GroupUniformBuiltins : GenericTable {
  let FilterClass = "GroupUniformBuiltin";
  let Fields = ["Name", "Opcode", "IsLogical"];
}

// Function to lookup group builtins by their name and set.
def lookupGroupUniformBuiltin : SearchIndex {
  let Table = GroupUniformBuiltins;
  let Key = ["Name"];
}

// Multiclass used to define incoming builtin records for
// the SPV_KHR_uniform_group_instructions extension
// and corresponding work group builtin records.
multiclass DemangledGroupUniformBuiltin<string name, bits<8> minNumArgs, bits<8> maxNumArgs, Op operation> {
  def : DemangledBuiltin<!strconcat("__spirv_Group", name), OpenCL_std, GroupUniform, minNumArgs, maxNumArgs>;
  def : GroupUniformBuiltin<!strconcat("__spirv_Group", name), operation>;
}

// cl_khr_work_group_uniform_arithmetic / SPV_KHR_uniform_group_instructions
defm : DemangledGroupUniformBuiltin<"IMulKHR", 3, 3, OpGroupIMulKHR>;
defm : DemangledGroupUniformBuiltin<"FMulKHR", 3, 3, OpGroupFMulKHR>;
defm : DemangledGroupUniformBuiltin<"BitwiseAndKHR", 3, 3, OpGroupBitwiseAndKHR>;
defm : DemangledGroupUniformBuiltin<"BitwiseOrKHR", 3, 3, OpGroupBitwiseOrKHR>;
defm : DemangledGroupUniformBuiltin<"BitwiseXorKHR", 3, 3, OpGroupBitwiseXorKHR>;
defm : DemangledGroupUniformBuiltin<"LogicalAndKHR", 3, 3, OpGroupLogicalAndKHR>;
defm : DemangledGroupUniformBuiltin<"LogicalOrKHR", 3, 3, OpGroupLogicalOrKHR>;
defm : DemangledGroupUniformBuiltin<"LogicalXorKHR", 3, 3, OpGroupLogicalXorKHR>;

//===----------------------------------------------------------------------===//
// Class defining a get builtin record used for lowering builtin calls such as
// "get_sub_group_eq_mask" or "get_global_id" to SPIR-V instructions.
//
// name is the demangled name of the given builtin.
// set specifies which external instruction set the builtin belongs to.
// value specifies the value of the BuiltIn enum.
//===----------------------------------------------------------------------===//
class GetBuiltin<string name, InstructionSet set, BuiltIn value> {
  string Name = name;
  InstructionSet Set = set;
  BuiltIn Value = value;
}

// Table gathering all the get builtin records.
def GetBuiltins : GenericTable {
  let FilterClass = "GetBuiltin";
  let Fields = ["Name", "Set", "Value"];
  string TypeOf_Set = "InstructionSet";
  string TypeOf_Value = "BuiltIn";
}

// Function to lookup get builtin records by their name and set.
def lookupGetBuiltin : SearchIndex {
  let Table = GetBuiltins;
  let Key = ["Name", "Set"];
}

// Multiclass used to define at the same time both a demangled builtin record
// and a corresponding get builtin record.
multiclass DemangledGetBuiltin<string name, InstructionSet set, BuiltinGroup group, BuiltIn value> {
  def : DemangledBuiltin<name, set, group, 0, 1>;
  def : GetBuiltin<name, set, value>;
}

// Builtin variable records:
defm : DemangledGetBuiltin<"get_sub_group_eq_mask", OpenCL_std, Variable, SubgroupEqMask>;
defm : DemangledGetBuiltin<"get_sub_group_ge_mask", OpenCL_std, Variable, SubgroupGeMask>;
defm : DemangledGetBuiltin<"get_sub_group_gt_mask", OpenCL_std, Variable, SubgroupGtMask>;
defm : DemangledGetBuiltin<"get_sub_group_le_mask", OpenCL_std, Variable, SubgroupLeMask>;
defm : DemangledGetBuiltin<"get_sub_group_lt_mask", OpenCL_std, Variable, SubgroupLtMask>;
defm : DemangledGetBuiltin<"__spirv_BuiltInGlobalLinearId", OpenCL_std, Variable, GlobalLinearId>;
defm : DemangledGetBuiltin<"__spirv_BuiltInGlobalInvocationId", OpenCL_std, Variable, GlobalInvocationId>;

// GetQuery builtin records:
defm : DemangledGetBuiltin<"get_local_id", OpenCL_std, GetQuery, LocalInvocationId>;
defm : DemangledGetBuiltin<"get_global_id", OpenCL_std, GetQuery, GlobalInvocationId>;
defm : DemangledGetBuiltin<"get_local_size", OpenCL_std, GetQuery, WorkgroupSize>;
defm : DemangledGetBuiltin<"get_global_size", OpenCL_std, GetQuery, GlobalSize>;
defm : DemangledGetBuiltin<"get_group_id", OpenCL_std, GetQuery, WorkgroupId>;
defm : DemangledGetBuiltin<"get_enqueued_local_size", OpenCL_std, GetQuery, EnqueuedWorkgroupSize>;
defm : DemangledGetBuiltin<"get_num_groups", OpenCL_std, GetQuery, NumWorkgroups>;
defm : DemangledGetBuiltin<"__hlsl_wave_get_lane_index", GLSL_std_450, Wave, SubgroupLocalInvocationId>;

//===----------------------------------------------------------------------===//
// Class defining an image query builtin record used for lowering the OpenCL
// "get_image_*" calls into OpImageQuerySize/OpImageQuerySizeLod instructions.
//
// name is the demangled name of the given builtin.
// set specifies which external instruction set the builtin belongs to.
// component specifies the unsigned number of the query component.
//===----------------------------------------------------------------------===//
class ImageQueryBuiltin<string name, InstructionSet set, bits<32> component> {
  string Name = name;
  InstructionSet Set = set;
  bits<32> Component = component;
}

// Table gathering all the image query builtins.
def ImageQueryBuiltins : GenericTable {
  let FilterClass = "ImageQueryBuiltin";
  let Fields = ["Name", "Set", "Component"];
  string TypeOf_Set = "InstructionSet";
}

// Function to lookup image query builtins by their name and set.
def lookupImageQueryBuiltin : SearchIndex {
  let Table = ImageQueryBuiltins;
  let Key = ["Name", "Set"];
}

// Multiclass used to define at the same time both a demangled builtin record
// and a corresponding image query builtin record.
multiclass DemangledImageQueryBuiltin<string name, InstructionSet set, int component> {
  def : DemangledBuiltin<name, set, ImageSizeQuery, 1, 1>;
  def : ImageQueryBuiltin<name, set, component>;
}

// Image query builtin records:
defm : DemangledImageQueryBuiltin<"get_image_width", OpenCL_std, 0>;
defm : DemangledImageQueryBuiltin<"get_image_height", OpenCL_std, 1>;
defm : DemangledImageQueryBuiltin<"get_image_depth", OpenCL_std, 2>;
defm : DemangledImageQueryBuiltin<"get_image_dim", OpenCL_std, 0>;
defm : DemangledImageQueryBuiltin<"get_image_array_size", OpenCL_std, 3>;

defm : DemangledNativeBuiltin<"get_image_num_samples", OpenCL_std, ImageMiscQuery, 1, 1, OpImageQuerySamples>;
defm : DemangledNativeBuiltin<"get_image_num_mip_levels", OpenCL_std, ImageMiscQuery, 1, 1, OpImageQueryLevels>;

//===----------------------------------------------------------------------===//
// Class defining a "convert_destType<_sat><_roundingMode>" call record for
// lowering into OpConvert instructions.
//
// name is the demangled name of the given builtin.
// set specifies which external instruction set the builtin belongs to.
//===----------------------------------------------------------------------===//
class ConvertBuiltin<string name, InstructionSet set> {
  string Name = name;
  InstructionSet Set = set;
  bit IsDestinationSigned = !eq(!find(name, "convert_u"), -1);
  bit IsSaturated = !not(!eq(!find(name, "_sat"), -1));
  bit IsRounded = !not(!eq(!find(name, "_rt"), -1));
  bit IsBfloat16 = !or(!not(!eq(!find(name, "BF16"), -1)),
                       !not(!eq(!find(name, "bfloat16"), -1)));
  FPRoundingMode RoundingMode = !cond(!not(!eq(!find(name, "_rte"), -1)) : RTE,
                                  !not(!eq(!find(name, "_rtz"), -1)) : RTZ,
                                  !not(!eq(!find(name, "_rtp"), -1)) : RTP,
                                  !not(!eq(!find(name, "_rtn"), -1)) : RTN,
                                  true : RTE);
}

// Table gathering all the convert builtins.
def ConvertBuiltins : GenericTable {
  let FilterClass = "ConvertBuiltin";
  let Fields = ["Name", "Set", "IsDestinationSigned", "IsSaturated",
                "IsRounded", "IsBfloat16", "RoundingMode"];
  string TypeOf_Set = "InstructionSet";
  string TypeOf_RoundingMode = "FPRoundingMode";
}

// Function to lookup convert builtins by their name and set.
def lookupConvertBuiltin : SearchIndex {
  let Table = ConvertBuiltins;
  let Key = ["Name", "Set"];
}

// Multiclass used to define at the same time both a demangled builtin records
// and a corresponding convert builtin records.
multiclass DemangledConvertBuiltin<string name, InstructionSet set> {
  // Create records for scalar and 2, 4, 8, and 16 element vector conversions.
  foreach i = ["", "2", "3", "4", "8", "16"] in {
    // Also create records for each rounding mode.
    foreach j = ["", "_rte", "_rtz", "_rtp", "_rtn"] in {
      def : DemangledBuiltin<!strconcat(name, i, j), set, Convert, 1, 1>;
      def : ConvertBuiltin<!strconcat(name, i, j), set>;

      // Create records with the "_sat" modifier for all conversions except
      // those targeting floating-point types.
      if !eq(!find(name, "float"), -1) then {
        def : DemangledBuiltin<!strconcat(name, i, "_sat", j), set, Convert, 1, 1>;
        def : ConvertBuiltin<!strconcat(name, i, "_sat", j), set>;
      }
    }
  }
}

// Explicit conversion builtin records:
defm : DemangledConvertBuiltin<"convert_char", OpenCL_std>;
defm : DemangledConvertBuiltin<"convert_uchar", OpenCL_std>;
defm : DemangledConvertBuiltin<"convert_short", OpenCL_std>;
defm : DemangledConvertBuiltin<"convert_ushort", OpenCL_std>;
defm : DemangledConvertBuiltin<"convert_int", OpenCL_std>;
defm : DemangledConvertBuiltin<"convert_uint", OpenCL_std>;
defm : DemangledConvertBuiltin<"convert_long", OpenCL_std>;
defm : DemangledConvertBuiltin<"convert_ulong", OpenCL_std>;
defm : DemangledConvertBuiltin<"convert_float", OpenCL_std>;

defm : DemangledNativeBuiltin<"__spirv_ConvertFToU", OpenCL_std, Convert, 1, 1, OpConvertFToU>;
defm : DemangledNativeBuiltin<"__spirv_ConvertFToS", OpenCL_std, Convert, 1, 1, OpConvertFToS>;
defm : DemangledNativeBuiltin<"__spirv_ConvertSToF", OpenCL_std, Convert, 1, 1, OpConvertSToF>;
defm : DemangledNativeBuiltin<"__spirv_ConvertUToF", OpenCL_std, Convert, 1, 1, OpConvertUToF>;
defm : DemangledNativeBuiltin<"__spirv_UConvert", OpenCL_std, Convert, 1, 1, OpUConvert>;
defm : DemangledNativeBuiltin<"__spirv_SConvert", OpenCL_std, Convert, 1, 1, OpSConvert>;
defm : DemangledNativeBuiltin<"__spirv_FConvert", OpenCL_std, Convert, 1, 1, OpFConvert>;
defm : DemangledNativeBuiltin<"__spirv_QuantizeToF16", OpenCL_std, Convert, 1, 1, OpQuantizeToF16>;
defm : DemangledNativeBuiltin<"__spirv_ConvertPtrToU", OpenCL_std, Convert, 1, 1, OpConvertPtrToU>;
defm : DemangledNativeBuiltin<"__spirv_SatConvertSToU", OpenCL_std, Convert, 1, 1, OpSatConvertSToU>;
defm : DemangledNativeBuiltin<"__spirv_SatConvertUToS", OpenCL_std, Convert, 1, 1, OpSatConvertUToS>;
defm : DemangledNativeBuiltin<"__spirv_ConvertUToPtr", OpenCL_std, Convert, 1, 1, OpConvertUToPtr>;

// cl_intel_bfloat16_conversions / SPV_INTEL_bfloat16_conversion
// Multiclass used to define at the same time both a demangled builtin records
// and a corresponding convert builtin records.
multiclass DemangledBF16ConvertBuiltin<string name1, string name2> {
  // Create records for scalar and vector conversions.
  foreach i = ["", "2", "3", "4", "8", "16"] in {
    def : DemangledBuiltin<!strconcat("intel_convert_", name1, i, name2, i), OpenCL_std, Convert, 1, 1>;
    def : ConvertBuiltin<!strconcat("intel_convert_", name1, i, name2, i), OpenCL_std>;
  }
}

defm : DemangledBF16ConvertBuiltin<"bfloat16", "_as_ushort">;
defm : DemangledBF16ConvertBuiltin<"as_bfloat16", "_float">;

foreach conv = ["FToBF16INTEL", "BF16ToFINTEL"] in {
  def : DemangledBuiltin<!strconcat("__spirv_Convert", conv), OpenCL_std, Convert, 1, 1>;
  def : ConvertBuiltin<!strconcat("__spirv_Convert", conv), OpenCL_std>;
}

//===----------------------------------------------------------------------===//
// Class defining a vector data load/store builtin record used for lowering
// into OpExtInst instruction.
//
// name is the demangled name of the given builtin.
// set specifies which external instruction set the builtin belongs to.
// number specifies the number of the instruction in the external set.
//===----------------------------------------------------------------------===//
class VectorLoadStoreBuiltin<string name, InstructionSet set, int number> {
  string Name = name;
  InstructionSet Set = set;
  bits<32> Number = number;
  bits<32> ElementCount = !cond(!not(!eq(!find(name, "2"), -1)) : 2,
                                !not(!eq(!find(name, "3"), -1)) : 3,
                                !not(!eq(!find(name, "4"), -1)) : 4,
                                !not(!eq(!find(name, "8"), -1)) : 8,
                                !not(!eq(!find(name, "16"), -1)) : 16,
                                true : 1);
  bit IsRounded = !not(!eq(!find(name, "_rt"), -1));
  FPRoundingMode RoundingMode = !cond(!not(!eq(!find(name, "_rte"), -1)) : RTE,
                                      !not(!eq(!find(name, "_rtz"), -1)) : RTZ,
                                      !not(!eq(!find(name, "_rtp"), -1)) : RTP,
                                      !not(!eq(!find(name, "_rtn"), -1)) : RTN,
                                      true : RTE);
}

// Table gathering all the vector data load/store builtins.
def VectorLoadStoreBuiltins : GenericTable {
  let FilterClass = "VectorLoadStoreBuiltin";
  let Fields = ["Name", "Set", "Number", "ElementCount", "IsRounded", "RoundingMode"];
  string TypeOf_Set = "InstructionSet";
  string TypeOf_RoundingMode = "FPRoundingMode";
}

// Function to lookup vector data load/store builtins by their name and set.
def lookupVectorLoadStoreBuiltin : SearchIndex {
  let Table = VectorLoadStoreBuiltins;
  let Key = ["Name", "Set"];
}

// Multiclass used to define at the same time both a demangled builtin record
// and a corresponding vector data load/store builtin record.
multiclass DemangledVectorLoadStoreBuiltin<string name, bits<8> minNumArgs, bits<8> maxNumArgs, int number> {
  def : DemangledBuiltin<name, OpenCL_std, VectorLoadStore, minNumArgs, maxNumArgs>;
  def : VectorLoadStoreBuiltin<name, OpenCL_std, number>;
}

// Create records for scalar and 2, 4, 8, and 16 vector element count.
foreach i = ["", "2", "3", "4", "8", "16"] in {
  if !eq(i, "") then {
    defm : DemangledVectorLoadStoreBuiltin<"vload_half", 2, 2, 173>;
    defm : DemangledVectorLoadStoreBuiltin<"vstore_half", 3, 3, 175>;
  } else {
    defm : DemangledVectorLoadStoreBuiltin<!strconcat("vload_half", i), 3, 3, 174>;
    defm : DemangledVectorLoadStoreBuiltin<!strconcat("vstore_half", i), 3, 3, 177>;
  }
  defm : DemangledVectorLoadStoreBuiltin<!strconcat("vload", i), 2, 2, 171>;
  defm : DemangledVectorLoadStoreBuiltin<!strconcat("vstore", i), 3, 3, 172>;
  defm : DemangledVectorLoadStoreBuiltin<!strconcat("vloada_half", i), 2, 2, 174>;
  defm : DemangledVectorLoadStoreBuiltin<!strconcat("vstorea_half", i), 3, 3, 180>;

  // Also create records for each rounding mode.
  foreach j = ["_rte", "_rtz", "_rtp", "_rtn"] in {
    if !eq(i, "") then {
      defm : DemangledVectorLoadStoreBuiltin<!strconcat("vstore_half", j), 3, 3, 176>;
    } else {
      defm : DemangledVectorLoadStoreBuiltin<!strconcat("vstore_half", i, j), 3, 3, 178>;
    }
    defm : DemangledVectorLoadStoreBuiltin<!strconcat("vstorea_half", i, j), 3, 3, 181>;
  }
}

//===----------------------------------------------------------------------===//
// Class defining implementation details of SPIR-V builtin types. The info
// in the record is used for lowering into OpType.
//
// name is the name of the given SPIR-V builtin type.
// operation specifies the SPIR-V opcode the StructType should be lowered to.
//===----------------------------------------------------------------------===//
class BuiltinType<string name, Op operation> {
  string Name = name;
  Op Opcode = operation;
}

// Table gathering all the builtin type records.
def BuiltinTypes : GenericTable {
  let FilterClass = "BuiltinType";
  let Fields = ["Name", "Opcode"];
}

// Function to lookup builtin types by their demangled name.
def lookupBuiltinType : SearchIndex {
  let Table = BuiltinTypes;
  let Key = ["Name"];
}

def : BuiltinType<"spirv.ReserveId", OpTypeReserveId>;
def : BuiltinType<"spirv.PipeStorage", OpTypePipeStorage>;
def : BuiltinType<"spirv.Queue", OpTypeQueue>;
def : BuiltinType<"spirv.Event", OpTypeEvent>;
def : BuiltinType<"spirv.Sampler", OpTypeSampler>;
def : BuiltinType<"spirv.DeviceEvent", OpTypeDeviceEvent>;
def : BuiltinType<"spirv.Image", OpTypeImage>;
def : BuiltinType<"spirv.SampledImage", OpTypeSampledImage>;
def : BuiltinType<"spirv.Pipe", OpTypePipe>;
def : BuiltinType<"spirv.CooperativeMatrixKHR", OpTypeCooperativeMatrixKHR>;

//===----------------------------------------------------------------------===//
// Class matching an OpenCL builtin type name to an equivalent SPIR-V
// builtin type literal.
//
// name is the name of the given OpenCL builtin type.
// spirvTypeLiteral is the literal of an equivalent SPIR-V builtin type.
//===----------------------------------------------------------------------===//
class OpenCLType<string name, string spirvTypeLiteral> {
  string Name = name;
  string SpirvTypeLiteral = spirvTypeLiteral;
}

// Table gathering all the OpenCL type records.
def OpenCLTypes : GenericTable {
  let FilterClass = "OpenCLType";
  let Fields = ["Name", "SpirvTypeLiteral"];
}

// Function to lookup OpenCL types by their name.
def lookupOpenCLType : SearchIndex {
  let Table = OpenCLTypes;
  let Key = ["Name"];
}

def : OpenCLType<"opencl.reserve_id_t", "spirv.ReserveId">;
def : OpenCLType<"opencl.event_t", "spirv.Event">;
def : OpenCLType<"opencl.queue_t", "spirv.Queue">;
def : OpenCLType<"opencl.sampler_t", "spirv.Sampler">;
def : OpenCLType<"opencl.clk_event_t", "spirv.DeviceEvent">;

foreach aq = ["_t", "_ro_t", "_wo_t", "_rw_t"] in {
  defvar p = !cond(!not(!eq(!find(aq, "_rw_t"), -1)) : "2",
                   !not(!eq(!find(aq, "_wo_t"), -1)) : "1",
                                                true : "0");
  def : OpenCLType<!strconcat("opencl.pipe", aq), 
                   !strconcat("spirv.Pipe._", p)>;
}

foreach aq = ["_t", "_ro_t", "_wo_t", "_rw_t"] in {
  defvar p7 = !cond(!not(!eq(!find(aq, "_rw_t"), -1)) : "2",
                    !not(!eq(!find(aq, "_wo_t"), -1)) : "1",
                                                 true : "0");

  def : OpenCLType<!strconcat("opencl.image1d", aq), 
                   !strconcat("spirv.Image._void_0_0_0_0_0_0_", p7)>;
  def : OpenCLType<!strconcat("opencl.image1d_array", aq), 
                   !strconcat("spirv.Image._void_0_0_1_0_0_0_", p7)>;
  def : OpenCLType<!strconcat("opencl.image1d_buffer", aq), 
                   !strconcat("spirv.Image._void_5_0_0_0_0_0_", p7)>;

  foreach a1 = ["", "_array"] in {
    foreach a2 = ["", "_msaa"] in {
      foreach a3 = ["", "_depth"] in {
        defvar p2 = !cond(!not(!eq(!find(a3, "_depth"), -1)) : "1", true : "0");
        defvar p3 = !cond(!not(!eq(!find(a1, "_array"), -1))  : "1", true : "0");
        defvar p4 = !cond(!not(!eq(!find(a2, "msaa"), -1))  : "1", true : "0");

        def : OpenCLType<!strconcat("opencl.image2d", a1, a2, a3, aq), 
                         !strconcat("spirv.Image._void_1_", p2 , "_", p3, "_", p4, "_0_0_", p7)>;
      }
    }
  }
  
  def : OpenCLType<!strconcat("opencl.image3d", aq), 
                   !strconcat("spirv.Image._void_2_0_0_0_0_0_", p7)>;
}

//===----------------------------------------------------------------------===//
// Classes definining various OpenCL enums.
//===----------------------------------------------------------------------===//

// OpenCL memory_scope enum
def CLMemoryScope : GenericEnum {
  let FilterClass = "CLMemoryScope";
  let NameField = "Name";
  let ValueField = "Value";
}

class CLMemoryScope<bits<32> value> {
  string Name = NAME;
  bits<32> Value = value;
}

def memory_scope_work_item : CLMemoryScope<0>;
def memory_scope_work_group : CLMemoryScope<1>;
def memory_scope_device : CLMemoryScope<2>;
def memory_scope_all_svm_devices : CLMemoryScope<3>;
def memory_scope_sub_group : CLMemoryScope<4>;

// OpenCL sampler addressing mode/bitmask enum
def CLSamplerAddressingMode : GenericEnum {
  let FilterClass = "CLSamplerAddressingMode";
  let NameField = "Name";
  let ValueField = "Value";
}

class CLSamplerAddressingMode<bits<32> value> {
  string Name = NAME;
  bits<32> Value = value;
}

def CLK_ADDRESS_NONE : CLSamplerAddressingMode<0x0>;
def CLK_ADDRESS_CLAMP : CLSamplerAddressingMode<0x4>;
def CLK_ADDRESS_CLAMP_TO_EDGE : CLSamplerAddressingMode<0x2>;
def CLK_ADDRESS_REPEAT : CLSamplerAddressingMode<0x6>;
def CLK_ADDRESS_MIRRORED_REPEAT : CLSamplerAddressingMode<0x8>;
def CLK_ADDRESS_MODE_MASK : CLSamplerAddressingMode<0xE>;
def CLK_NORMALIZED_COORDS_FALSE : CLSamplerAddressingMode<0x0>;
def CLK_NORMALIZED_COORDS_TRUE : CLSamplerAddressingMode<0x1>;
def CLK_FILTER_NEAREST : CLSamplerAddressingMode<0x10>;
def CLK_FILTER_LINEAR : CLSamplerAddressingMode<0x20>;

// OpenCL memory fences
def CLMemoryFenceFlags : GenericEnum {
  let FilterClass = "CLMemoryFenceFlags";
  let NameField = "Name";
  let ValueField = "Value";
}

class CLMemoryFenceFlags<bits<32> value> {
  string Name = NAME;
  bits<32> Value = value;
}

def CLK_LOCAL_MEM_FENCE : CLMemoryFenceFlags<0x1>;
def CLK_GLOBAL_MEM_FENCE : CLMemoryFenceFlags<0x2>;
def CLK_IMAGE_MEM_FENCE : CLMemoryFenceFlags<0x4>;
