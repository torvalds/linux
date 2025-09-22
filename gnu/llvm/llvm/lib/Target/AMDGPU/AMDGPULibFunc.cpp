//===-- AMDGPULibFunc.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file contains utility functions to work with Itanium mangled names
//
//===----------------------------------------------------------------------===//

#include "AMDGPULibFunc.h"
#include "AMDGPU.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static cl::opt<bool> EnableOCLManglingMismatchWA(
    "amdgpu-enable-ocl-mangling-mismatch-workaround", cl::init(true),
    cl::ReallyHidden,
    cl::desc("Enable the workaround for OCL name mangling mismatch."));

namespace {

enum EManglingParam {
    E_NONE,
    EX_EVENT,
    EX_FLOAT4,
    EX_INTV4,
    EX_RESERVEDID,
    EX_SAMPLER,
    EX_SIZET,
    EX_UINT,
    EX_UINTV4,
    E_ANY,
    E_CONSTPTR_ANY,
    E_CONSTPTR_SWAPGL,
    E_COPY,
    E_IMAGECOORDS,
    E_POINTEE,
    E_SETBASE_I32,
    E_SETBASE_U32,
    E_MAKEBASE_UNS,
    E_V16_OF_POINTEE,
    E_V2_OF_POINTEE,
    E_V3_OF_POINTEE,
    E_V4_OF_POINTEE,
    E_V8_OF_POINTEE,
    E_VLTLPTR_ANY,
};

struct ManglingRule {
   const char *Name;
   unsigned char Lead[2];
   unsigned char Param[5];

   int maxLeadIndex() const { return (std::max)(Lead[0], Lead[1]); }
   int getNumLeads() const { return (Lead[0] ? 1 : 0) + (Lead[1] ? 1 : 0); }

   unsigned getNumArgs() const;

   static StringMap<int> buildManglingRulesMap();
};

// Information about library functions with unmangled names.
class UnmangledFuncInfo {
  const char *Name;
  unsigned NumArgs;

  // Table for all lib functions with unmangled names.
  static const UnmangledFuncInfo Table[];

  // Number of entries in Table.
  static const unsigned TableSize;

  static StringMap<unsigned> buildNameMap();

public:
  using ID = AMDGPULibFunc::EFuncId;
  constexpr UnmangledFuncInfo(const char *_Name, unsigned _NumArgs)
      : Name(_Name), NumArgs(_NumArgs) {}
  // Get index to Table by function name.
  static bool lookup(StringRef Name, ID &Id);
  static unsigned toIndex(ID Id) {
    assert(static_cast<unsigned>(Id) >
               static_cast<unsigned>(AMDGPULibFunc::EI_LAST_MANGLED) &&
           "Invalid unmangled library function");
    return static_cast<unsigned>(Id) - 1 -
           static_cast<unsigned>(AMDGPULibFunc::EI_LAST_MANGLED);
  }
  static ID toFuncId(unsigned Index) {
    assert(Index < TableSize &&
           "Invalid unmangled library function");
    return static_cast<ID>(
        Index + 1 + static_cast<unsigned>(AMDGPULibFunc::EI_LAST_MANGLED));
  }
  static unsigned getNumArgs(ID Id) { return Table[toIndex(Id)].NumArgs; }
  static StringRef getName(ID Id) { return Table[toIndex(Id)].Name; }
};

unsigned ManglingRule::getNumArgs() const {
   unsigned I=0;
   while (I < (sizeof Param/sizeof Param[0]) && Param[I]) ++I;
   return I;
}

// This table describes function formal argument type rules. The order of rules
// corresponds to the EFuncId enum at AMDGPULibFunc.h
//
// "<func name>", { <leads> }, { <param rules> }
// where:
//  <leads> - list of integers that are one-based indexes of formal argument
//    used to mangle a function name. Other argument types are derived from types
//    of these 'leads'. The order of integers in this list correspond to the
//    order in which these arguments are mangled in the EDG mangling scheme. The
//    same order should be preserved for arguments in the AMDGPULibFunc structure
//    when it is used for mangling. For example:
//    { "vstorea_half", {3,1}, {E_ANY,EX_SIZET,E_ANY}},
//    will be mangled in EDG scheme as  vstorea_half_<3dparam>_<1stparam>
//    When mangling from code use:
//    AMDGPULibFunc insc;
//    insc.param[0] = ... // describe 3rd parameter
//    insc.param[1] = ... // describe 1rd parameter
//
// <param rules> - list of rules used to derive all of the function formal
//    argument types. EX_ prefixed are simple types, other derived from the
//    latest 'lead' argument type in the order of encoding from first to last.
//    E_ANY - use prev lead type, E_CONSTPTR_ANY - make const pointer out of
//    prev lead type, etc. see ParamIterator::getNextParam() for details.

static constexpr ManglingRule manglingRules[] = {
{ "", {0}, {0} },
{ "abs"                             , {1},   {E_ANY}},
{ "abs_diff"                        , {1},   {E_ANY,E_COPY}},
{ "acos"                            , {1},   {E_ANY}},
{ "acosh"                           , {1},   {E_ANY}},
{ "acospi"                          , {1},   {E_ANY}},
{ "add_sat"                         , {1},   {E_ANY,E_COPY}},
{ "all"                             , {1},   {E_ANY}},
{ "any"                             , {1},   {E_ANY}},
{ "asin"                            , {1},   {E_ANY}},
{ "asinh"                           , {1},   {E_ANY}},
{ "asinpi"                          , {1},   {E_ANY}},
{ "async_work_group_copy"           , {1},   {E_ANY,E_CONSTPTR_SWAPGL,EX_SIZET,EX_EVENT}},
{ "async_work_group_strided_copy"   , {1},   {E_ANY,E_CONSTPTR_SWAPGL,EX_SIZET,EX_SIZET,EX_EVENT}},
{ "atan"                            , {1},   {E_ANY}},
{ "atan2"                           , {1},   {E_ANY,E_COPY}},
{ "atan2pi"                         , {1},   {E_ANY,E_COPY}},
{ "atanh"                           , {1},   {E_ANY}},
{ "atanpi"                          , {1},   {E_ANY}},
{ "atomic_add"                      , {1},   {E_VLTLPTR_ANY,E_POINTEE}},
{ "atomic_and"                      , {1},   {E_VLTLPTR_ANY,E_POINTEE}},
{ "atomic_cmpxchg"                  , {1},   {E_VLTLPTR_ANY,E_POINTEE,E_POINTEE}},
{ "atomic_dec"                      , {1},   {E_VLTLPTR_ANY}},
{ "atomic_inc"                      , {1},   {E_VLTLPTR_ANY}},
{ "atomic_max"                      , {1},   {E_VLTLPTR_ANY,E_POINTEE}},
{ "atomic_min"                      , {1},   {E_VLTLPTR_ANY,E_POINTEE}},
{ "atomic_or"                       , {1},   {E_VLTLPTR_ANY,E_POINTEE}},
{ "atomic_sub"                      , {1},   {E_VLTLPTR_ANY,E_POINTEE}},
{ "atomic_xchg"                     , {1},   {E_VLTLPTR_ANY,E_POINTEE}},
{ "atomic_xor"                      , {1},   {E_VLTLPTR_ANY,E_POINTEE}},
{ "bitselect"                       , {1},   {E_ANY,E_COPY,E_COPY}},
{ "cbrt"                            , {1},   {E_ANY}},
{ "ceil"                            , {1},   {E_ANY}},
{ "clamp"                           , {1},   {E_ANY,E_COPY,E_COPY}},
{ "clz"                             , {1},   {E_ANY}},
{ "commit_read_pipe"                , {1},   {E_ANY,EX_RESERVEDID}},
{ "commit_write_pipe"               , {1},   {E_ANY,EX_RESERVEDID}},
{ "copysign"                        , {1},   {E_ANY,E_COPY}},
{ "cos"                             , {1},   {E_ANY}},
{ "cosh"                            , {1},   {E_ANY}},
{ "cospi"                           , {1},   {E_ANY}},
{ "cross"                           , {1},   {E_ANY,E_COPY}},
{ "ctz"                             , {1},   {E_ANY}},
{ "degrees"                         , {1},   {E_ANY}},
{ "distance"                        , {1},   {E_ANY,E_COPY}},
{ "divide"                          , {1},   {E_ANY,E_COPY}},
{ "dot"                             , {1},   {E_ANY,E_COPY}},
{ "erf"                             , {1},   {E_ANY}},
{ "erfc"                            , {1},   {E_ANY}},
{ "exp"                             , {1},   {E_ANY}},
{ "exp10"                           , {1},   {E_ANY}},
{ "exp2"                            , {1},   {E_ANY}},
{ "expm1"                           , {1},   {E_ANY}},
{ "fabs"                            , {1},   {E_ANY}},
{ "fast_distance"                   , {1},   {E_ANY,E_COPY}},
{ "fast_length"                     , {1},   {E_ANY}},
{ "fast_normalize"                  , {1},   {E_ANY}},
{ "fdim"                            , {1},   {E_ANY,E_COPY}},
{ "floor"                           , {1},   {E_ANY}},
{ "fma"                             , {1},   {E_ANY,E_COPY,E_COPY}},
{ "fmax"                            , {1},   {E_ANY,E_COPY}},
{ "fmin"                            , {1},   {E_ANY,E_COPY}},
{ "fmod"                            , {1},   {E_ANY,E_COPY}},
{ "fract"                           , {2},   {E_POINTEE,E_ANY}},
{ "frexp"                           , {1,2}, {E_ANY,E_ANY}},
{ "get_image_array_size"            , {1},   {E_ANY}},
{ "get_image_channel_data_type"     , {1},   {E_ANY}},
{ "get_image_channel_order"         , {1},   {E_ANY}},
{ "get_image_dim"                   , {1},   {E_ANY}},
{ "get_image_height"                , {1},   {E_ANY}},
{ "get_image_width"                 , {1},   {E_ANY}},
{ "get_pipe_max_packets"            , {1},   {E_ANY}},
{ "get_pipe_num_packets"            , {1},   {E_ANY}},
{ "hadd"                            , {1},   {E_ANY,E_COPY}},
{ "hypot"                           , {1},   {E_ANY,E_COPY}},
{ "ilogb"                           , {1},   {E_ANY}},
{ "isequal"                         , {1},   {E_ANY,E_COPY}},
{ "isfinite"                        , {1},   {E_ANY}},
{ "isgreater"                       , {1},   {E_ANY,E_COPY}},
{ "isgreaterequal"                  , {1},   {E_ANY,E_COPY}},
{ "isinf"                           , {1},   {E_ANY}},
{ "isless"                          , {1},   {E_ANY,E_COPY}},
{ "islessequal"                     , {1},   {E_ANY,E_COPY}},
{ "islessgreater"                   , {1},   {E_ANY,E_COPY}},
{ "isnan"                           , {1},   {E_ANY}},
{ "isnormal"                        , {1},   {E_ANY}},
{ "isnotequal"                      , {1},   {E_ANY,E_COPY}},
{ "isordered"                       , {1},   {E_ANY,E_COPY}},
{ "isunordered"                     , {1},   {E_ANY,E_COPY}},
{ "ldexp"                           , {1},   {E_ANY,E_SETBASE_I32}},
{ "length"                          , {1},   {E_ANY}},
{ "lgamma"                          , {1},   {E_ANY}},
{ "lgamma_r"                        , {1,2}, {E_ANY,E_ANY}},
{ "log"                             , {1},   {E_ANY}},
{ "log10"                           , {1},   {E_ANY}},
{ "log1p"                           , {1},   {E_ANY}},
{ "log2"                            , {1},   {E_ANY}},
{ "logb"                            , {1},   {E_ANY}},
{ "mad"                             , {1},   {E_ANY,E_COPY,E_COPY}},
{ "mad24"                           , {1},   {E_ANY,E_COPY,E_COPY}},
{ "mad_hi"                          , {1},   {E_ANY,E_COPY,E_COPY}},
{ "mad_sat"                         , {1},   {E_ANY,E_COPY,E_COPY}},
{ "max"                             , {1},   {E_ANY,E_COPY}},
{ "maxmag"                          , {1},   {E_ANY,E_COPY}},
{ "min"                             , {1},   {E_ANY,E_COPY}},
{ "minmag"                          , {1},   {E_ANY,E_COPY}},
{ "mix"                             , {1},   {E_ANY,E_COPY,E_COPY}},
{ "modf"                            , {2},   {E_POINTEE,E_ANY}},
{ "mul24"                           , {1},   {E_ANY,E_COPY}},
{ "mul_hi"                          , {1},   {E_ANY,E_COPY}},
{ "nan"                             , {1},   {E_ANY}},
{ "nextafter"                       , {1},   {E_ANY,E_COPY}},
{ "normalize"                       , {1},   {E_ANY}},
{ "popcount"                        , {1},   {E_ANY}},
{ "pow"                             , {1},   {E_ANY,E_COPY}},
{ "pown"                            , {1},   {E_ANY,E_SETBASE_I32}},
{ "powr"                            , {1},   {E_ANY,E_COPY}},
{ "prefetch"                        , {1},   {E_CONSTPTR_ANY,EX_SIZET}},
{ "radians"                         , {1},   {E_ANY}},
{ "recip"                           , {1},   {E_ANY}},
{ "remainder"                       , {1},   {E_ANY,E_COPY}},
{ "remquo"                          , {1,3}, {E_ANY,E_COPY,E_ANY}},
{ "reserve_read_pipe"               , {1},   {E_ANY,EX_UINT}},
{ "reserve_write_pipe"              , {1},   {E_ANY,EX_UINT}},
{ "rhadd"                           , {1},   {E_ANY,E_COPY}},
{ "rint"                            , {1},   {E_ANY}},
{ "rootn"                           , {1},   {E_ANY,E_SETBASE_I32}},
{ "rotate"                          , {1},   {E_ANY,E_COPY}},
{ "round"                           , {1},   {E_ANY}},
{ "rsqrt"                           , {1},   {E_ANY}},
{ "select"                          , {1,3}, {E_ANY,E_COPY,E_ANY}},
{ "shuffle"                         , {1,2}, {E_ANY,E_ANY}},
{ "shuffle2"                        , {1,3}, {E_ANY,E_COPY,E_ANY}},
{ "sign"                            , {1},   {E_ANY}},
{ "signbit"                         , {1},   {E_ANY}},
{ "sin"                             , {1},   {E_ANY}},
{ "sincos"                          , {2},   {E_POINTEE,E_ANY}},
{ "sinh"                            , {1},   {E_ANY}},
{ "sinpi"                           , {1},   {E_ANY}},
{ "smoothstep"                      , {1},   {E_ANY,E_COPY,E_COPY}},
{ "sqrt"                            , {1},   {E_ANY}},
{ "step"                            , {1},   {E_ANY,E_COPY}},
{ "sub_group_broadcast"             , {1},   {E_ANY,EX_UINT}},
{ "sub_group_commit_read_pipe"      , {1},   {E_ANY,EX_RESERVEDID}},
{ "sub_group_commit_write_pipe"     , {1},   {E_ANY,EX_RESERVEDID}},
{ "sub_group_reduce_add"            , {1},   {E_ANY}},
{ "sub_group_reduce_max"            , {1},   {E_ANY}},
{ "sub_group_reduce_min"            , {1},   {E_ANY}},
{ "sub_group_reserve_read_pipe"     , {1},   {E_ANY,EX_UINT}},
{ "sub_group_reserve_write_pipe"    , {1},   {E_ANY,EX_UINT}},
{ "sub_group_scan_exclusive_add"    , {1},   {E_ANY}},
{ "sub_group_scan_exclusive_max"    , {1},   {E_ANY}},
{ "sub_group_scan_exclusive_min"    , {1},   {E_ANY}},
{ "sub_group_scan_inclusive_add"    , {1},   {E_ANY}},
{ "sub_group_scan_inclusive_max"    , {1},   {E_ANY}},
{ "sub_group_scan_inclusive_min"    , {1},   {E_ANY}},
{ "sub_sat"                         , {1},   {E_ANY,E_COPY}},
{ "tan"                             , {1},   {E_ANY}},
{ "tanh"                            , {1},   {E_ANY}},
{ "tanpi"                           , {1},   {E_ANY}},
{ "tgamma"                          , {1},   {E_ANY}},
{ "trunc"                           , {1},   {E_ANY}},
{ "upsample"                        , {1},   {E_ANY,E_MAKEBASE_UNS}},
{ "vec_step"                        , {1},   {E_ANY}},
{ "vstore"                          , {3},   {E_POINTEE,EX_SIZET,E_ANY}},
{ "vstore16"                        , {3},   {E_V16_OF_POINTEE,EX_SIZET,E_ANY}},
{ "vstore2"                         , {3},   {E_V2_OF_POINTEE,EX_SIZET,E_ANY}},
{ "vstore3"                         , {3},   {E_V3_OF_POINTEE,EX_SIZET,E_ANY}},
{ "vstore4"                         , {3},   {E_V4_OF_POINTEE,EX_SIZET,E_ANY}},
{ "vstore8"                         , {3},   {E_V8_OF_POINTEE,EX_SIZET,E_ANY}},
{ "work_group_commit_read_pipe"     , {1},   {E_ANY,EX_RESERVEDID}},
{ "work_group_commit_write_pipe"    , {1},   {E_ANY,EX_RESERVEDID}},
{ "work_group_reduce_add"           , {1},   {E_ANY}},
{ "work_group_reduce_max"           , {1},   {E_ANY}},
{ "work_group_reduce_min"           , {1},   {E_ANY}},
{ "work_group_reserve_read_pipe"    , {1},   {E_ANY,EX_UINT}},
{ "work_group_reserve_write_pipe"   , {1},   {E_ANY,EX_UINT}},
{ "work_group_scan_exclusive_add"   , {1},   {E_ANY}},
{ "work_group_scan_exclusive_max"   , {1},   {E_ANY}},
{ "work_group_scan_exclusive_min"   , {1},   {E_ANY}},
{ "work_group_scan_inclusive_add"   , {1},   {E_ANY}},
{ "work_group_scan_inclusive_max"   , {1},   {E_ANY}},
{ "work_group_scan_inclusive_min"   , {1},   {E_ANY}},
{ "write_imagef"                    , {1},   {E_ANY,E_IMAGECOORDS,EX_FLOAT4}},
{ "write_imagei"                    , {1},   {E_ANY,E_IMAGECOORDS,EX_INTV4}},
{ "write_imageui"                   , {1},   {E_ANY,E_IMAGECOORDS,EX_UINTV4}},
{ "ncos"                            , {1},   {E_ANY} },
{ "nexp2"                           , {1},   {E_ANY} },
{ "nfma"                            , {1},   {E_ANY, E_COPY, E_COPY} },
{ "nlog2"                           , {1},   {E_ANY} },
{ "nrcp"                            , {1},   {E_ANY} },
{ "nrsqrt"                          , {1},   {E_ANY} },
{ "nsin"                            , {1},   {E_ANY} },
{ "nsqrt"                           , {1},   {E_ANY} },
{ "ftz"                             , {1},   {E_ANY} },
{ "fldexp"                          , {1},   {E_ANY, EX_UINT} },
{ "class"                           , {1},   {E_ANY, EX_UINT} },
{ "rcbrt"                           , {1},   {E_ANY} },
};

// Library functions with unmangled name.
const UnmangledFuncInfo UnmangledFuncInfo::Table[] = {
    {"__read_pipe_2", 4},
    {"__read_pipe_4", 6},
    {"__write_pipe_2", 4},
    {"__write_pipe_4", 6},
};

const unsigned UnmangledFuncInfo::TableSize =
    std::size(UnmangledFuncInfo::Table);

static AMDGPULibFunc::Param getRetType(AMDGPULibFunc::EFuncId id,
                                       const AMDGPULibFunc::Param (&Leads)[2]) {
  AMDGPULibFunc::Param Res = Leads[0];
  // TBD - This switch may require to be extended for other intrinsics
  switch (id) {
  case AMDGPULibFunc::EI_SINCOS:
    Res.PtrKind = AMDGPULibFunc::BYVALUE;
    break;
  default:
    break;
  }
  return Res;
}

class ParamIterator {
  const AMDGPULibFunc::Param (&Leads)[2];
  const ManglingRule& Rule;
  int Index = 0;
public:
  ParamIterator(const AMDGPULibFunc::Param (&leads)[2],
                const ManglingRule& rule)
    : Leads(leads), Rule(rule) {}

  AMDGPULibFunc::Param getNextParam();
};

AMDGPULibFunc::Param ParamIterator::getNextParam() {
  AMDGPULibFunc::Param P;
  if (Index >= int(sizeof Rule.Param/sizeof Rule.Param[0])) return P;

  const char R = Rule.Param[Index];
  switch (R) {
  case E_NONE:     break;
  case EX_UINT:
    P.ArgType = AMDGPULibFunc::U32; break;
  case EX_INTV4:
    P.ArgType = AMDGPULibFunc::I32; P.VectorSize = 4; break;
  case EX_UINTV4:
    P.ArgType = AMDGPULibFunc::U32; P.VectorSize = 4; break;
  case EX_FLOAT4:
    P.ArgType = AMDGPULibFunc::F32; P.VectorSize = 4; break;
  case EX_SIZET:
    P.ArgType = AMDGPULibFunc::U64; break;
  case EX_EVENT:
    P.ArgType = AMDGPULibFunc::EVENT;   break;
  case EX_SAMPLER:
    P.ArgType = AMDGPULibFunc::SAMPLER; break;
  case EX_RESERVEDID: break; // TBD
  default:
    if (Index == (Rule.Lead[1] - 1)) P = Leads[1];
    else P = Leads[0];

    switch (R) {
    case E_ANY:
    case E_COPY: break;

    case E_POINTEE:
      P.PtrKind = AMDGPULibFunc::BYVALUE; break;
    case E_V2_OF_POINTEE:
      P.VectorSize = 2; P.PtrKind = AMDGPULibFunc::BYVALUE; break;
    case E_V3_OF_POINTEE:
      P.VectorSize = 3; P.PtrKind = AMDGPULibFunc::BYVALUE; break;
    case E_V4_OF_POINTEE:
      P.VectorSize = 4; P.PtrKind = AMDGPULibFunc::BYVALUE; break;
    case E_V8_OF_POINTEE:
      P.VectorSize = 8; P.PtrKind = AMDGPULibFunc::BYVALUE; break;
    case E_V16_OF_POINTEE:
      P.VectorSize = 16; P.PtrKind = AMDGPULibFunc::BYVALUE; break;
    case E_CONSTPTR_ANY:
      P.PtrKind |= AMDGPULibFunc::CONST; break;
    case E_VLTLPTR_ANY:
      P.PtrKind |= AMDGPULibFunc::VOLATILE; break;
    case E_SETBASE_I32:
      P.ArgType = AMDGPULibFunc::I32; break;
    case E_SETBASE_U32:
      P.ArgType = AMDGPULibFunc::U32; break;

    case E_MAKEBASE_UNS:
      P.ArgType &= ~AMDGPULibFunc::BASE_TYPE_MASK;
      P.ArgType |= AMDGPULibFunc::UINT;
      break;

    case E_IMAGECOORDS:
      switch (P.ArgType) {
      case AMDGPULibFunc::IMG1DA: P.VectorSize = 2; break;
      case AMDGPULibFunc::IMG1DB: P.VectorSize = 1; break;
      case AMDGPULibFunc::IMG2DA: P.VectorSize = 4; break;
      case AMDGPULibFunc::IMG1D:  P.VectorSize = 1; break;
      case AMDGPULibFunc::IMG2D:  P.VectorSize = 2; break;
      case AMDGPULibFunc::IMG3D:  P.VectorSize = 4; break;
      }
      P.PtrKind = AMDGPULibFunc::BYVALUE;
      P.ArgType = AMDGPULibFunc::I32;
      break;

    case E_CONSTPTR_SWAPGL: {
      unsigned AS = AMDGPULibFunc::getAddrSpaceFromEPtrKind(P.PtrKind);
      switch (AS) {
      case AMDGPUAS::GLOBAL_ADDRESS: AS = AMDGPUAS::LOCAL_ADDRESS; break;
      case AMDGPUAS::LOCAL_ADDRESS:  AS = AMDGPUAS::GLOBAL_ADDRESS; break;
      }
      P.PtrKind = AMDGPULibFunc::getEPtrKindFromAddrSpace(AS);
      P.PtrKind |= AMDGPULibFunc::CONST;
      break;
    }

    default:
      llvm_unreachable("Unhandled param rule");
    }
  }
  ++Index;
  return P;
}

inline static void drop_front(StringRef& str, size_t n = 1) {
  str = str.drop_front(n);
}

static bool eatTerm(StringRef& mangledName, const char c) {
  if (mangledName.front() == c) {
    drop_front(mangledName);
    return true;
  }
  return false;
}

template <size_t N>
static bool eatTerm(StringRef& mangledName, const char (&str)[N]) {
  if (mangledName.starts_with(StringRef(str, N - 1))) {
    drop_front(mangledName, N-1);
    return true;
  }
  return false;
}

static int eatNumber(StringRef& s) {
  size_t const savedSize = s.size();
  int n = 0;
  while (!s.empty() && isDigit(s.front())) {
    n = n*10 + s.front() - '0';
    drop_front(s);
  }
  return s.size() < savedSize ? n : -1;
}

static StringRef eatLengthPrefixedName(StringRef& mangledName) {
  int const Len = eatNumber(mangledName);
  if (Len <= 0 || static_cast<size_t>(Len) > mangledName.size())
    return StringRef();
  StringRef Res = mangledName.substr(0, Len);
  drop_front(mangledName, Len);
  return Res;
}

} // end anonymous namespace

AMDGPUMangledLibFunc::AMDGPUMangledLibFunc() {
  FuncId = EI_NONE;
  FKind = NOPFX;
  Leads[0].reset();
  Leads[1].reset();
  Name.clear();
}

AMDGPUUnmangledLibFunc::AMDGPUUnmangledLibFunc() {
  FuncId = EI_NONE;
  FuncTy = nullptr;
}

AMDGPUMangledLibFunc::AMDGPUMangledLibFunc(
    EFuncId id, const AMDGPUMangledLibFunc &copyFrom) {
  FuncId = id;
  FKind = copyFrom.FKind;
  Leads[0] = copyFrom.Leads[0];
  Leads[1] = copyFrom.Leads[1];
}

AMDGPUMangledLibFunc::AMDGPUMangledLibFunc(EFuncId id, FunctionType *FT,
                                           bool SignedInts) {
  FuncId = id;
  unsigned NumArgs = FT->getNumParams();
  if (NumArgs >= 1)
    Leads[0] = Param::getFromTy(FT->getParamType(0), SignedInts);
  if (NumArgs >= 2)
    Leads[1] = Param::getFromTy(FT->getParamType(1), SignedInts);
}

///////////////////////////////////////////////////////////////////////////////
// Demangling

static int parseVecSize(StringRef& mangledName) {
  size_t const Len = eatNumber(mangledName);
  switch (Len) {
  case 2: case 3: case 4: case 8: case 16:
    return Len;
  default:
    break;
  }
  return 1;
}

static AMDGPULibFunc::ENamePrefix parseNamePrefix(StringRef& mangledName) {
  std::pair<StringRef, StringRef> const P = mangledName.split('_');
  AMDGPULibFunc::ENamePrefix Pfx =
    StringSwitch<AMDGPULibFunc::ENamePrefix>(P.first)
    .Case("native", AMDGPULibFunc::NATIVE)
    .Case("half"  , AMDGPULibFunc::HALF)
    .Default(AMDGPULibFunc::NOPFX);

  if (Pfx != AMDGPULibFunc::NOPFX)
    mangledName = P.second;

  return Pfx;
}

StringMap<int> ManglingRule::buildManglingRulesMap() {
  StringMap<int> Map(std::size(manglingRules));
  int Id = 0;
  for (auto Rule : manglingRules)
    Map.insert({Rule.Name, Id++});
  return Map;
}

bool AMDGPUMangledLibFunc::parseUnmangledName(StringRef FullName) {
  static const StringMap<int> manglingRulesMap =
      ManglingRule::buildManglingRulesMap();
  FuncId = static_cast<EFuncId>(manglingRulesMap.lookup(FullName));
  return FuncId != EI_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// Itanium Demangling

namespace {
struct ItaniumParamParser {
  AMDGPULibFunc::Param Prev;
  bool parseItaniumParam(StringRef& param, AMDGPULibFunc::Param &res);
};
} // namespace

bool ItaniumParamParser::parseItaniumParam(StringRef& param,
                                           AMDGPULibFunc::Param &res) {
  res.reset();
  if (param.empty()) return false;

  // parse pointer prefix
  if (eatTerm(param, 'P')) {
    if (eatTerm(param, 'K')) res.PtrKind |= AMDGPULibFunc::CONST;
    if (eatTerm(param, 'V')) res.PtrKind |= AMDGPULibFunc::VOLATILE;
    unsigned AS;
    if (!eatTerm(param, "U3AS")) {
      AS = 0;
    } else {
      AS = param.front() - '0';
      drop_front(param, 1);
    }
    res.PtrKind |= AMDGPULibFuncBase::getEPtrKindFromAddrSpace(AS);
  } else {
    res.PtrKind = AMDGPULibFunc::BYVALUE;
  }

  // parse vector size
  if (eatTerm(param,"Dv")) {
    res.VectorSize = parseVecSize(param);
    if (res.VectorSize==1 || !eatTerm(param, '_')) return false;
  }

  // parse type
  char const TC = param.front();
  if (isDigit(TC)) {
    res.ArgType = StringSwitch<AMDGPULibFunc::EType>
      (eatLengthPrefixedName(param))
      .Case("ocl_image1darray" , AMDGPULibFunc::IMG1DA)
      .Case("ocl_image1dbuffer", AMDGPULibFunc::IMG1DB)
      .Case("ocl_image2darray" , AMDGPULibFunc::IMG2DA)
      .Case("ocl_image1d"      , AMDGPULibFunc::IMG1D)
      .Case("ocl_image2d"      , AMDGPULibFunc::IMG2D)
      .Case("ocl_image3d"      , AMDGPULibFunc::IMG3D)
      .Case("ocl_event"        , AMDGPULibFunc::DUMMY)
      .Case("ocl_sampler"      , AMDGPULibFunc::DUMMY)
      .Default(AMDGPULibFunc::DUMMY);
  } else {
    drop_front(param);
    switch (TC) {
    case 'h': res.ArgType =  AMDGPULibFunc::U8; break;
    case 't': res.ArgType = AMDGPULibFunc::U16; break;
    case 'j': res.ArgType = AMDGPULibFunc::U32; break;
    case 'm': res.ArgType = AMDGPULibFunc::U64; break;
    case 'c': res.ArgType =  AMDGPULibFunc::I8; break;
    case 's': res.ArgType = AMDGPULibFunc::I16; break;
    case 'i': res.ArgType = AMDGPULibFunc::I32; break;
    case 'l': res.ArgType = AMDGPULibFunc::I64; break;
    case 'f': res.ArgType = AMDGPULibFunc::F32; break;
    case 'd': res.ArgType = AMDGPULibFunc::F64; break;
    case 'D': if (!eatTerm(param, 'h')) return false;
              res.ArgType = AMDGPULibFunc::F16; break;
    case 'S':
      if (!eatTerm(param, '_')) {
        eatNumber(param);
        if (!eatTerm(param, '_')) return false;
      }
      res.VectorSize = Prev.VectorSize;
      res.ArgType    = Prev.ArgType;
      break;
    default:;
    }
  }
  if (res.ArgType == 0) return false;
  Prev.VectorSize = res.VectorSize;
  Prev.ArgType    = res.ArgType;
  return true;
}

bool AMDGPUMangledLibFunc::parseFuncName(StringRef &mangledName) {
  StringRef Name = eatLengthPrefixedName(mangledName);
  FKind = parseNamePrefix(Name);
  if (!parseUnmangledName(Name))
    return false;

  const ManglingRule& Rule = manglingRules[FuncId];
  ItaniumParamParser Parser;
  for (int I=0; I < Rule.maxLeadIndex(); ++I) {
    Param P;
    if (!Parser.parseItaniumParam(mangledName, P))
      return false;

    if ((I + 1) == Rule.Lead[0]) Leads[0] = P;
    if ((I + 1) == Rule.Lead[1]) Leads[1] = P;
  }
  return true;
}

bool AMDGPUUnmangledLibFunc::parseFuncName(StringRef &Name) {
  if (!UnmangledFuncInfo::lookup(Name, FuncId))
    return false;
  setName(Name);
  return true;
}

bool AMDGPULibFunc::parse(StringRef FuncName, AMDGPULibFunc &F) {
  if (FuncName.empty()) {
    F.Impl = std::unique_ptr<AMDGPULibFuncImpl>();
    return false;
  }

  if (eatTerm(FuncName, "_Z"))
    F.Impl = std::make_unique<AMDGPUMangledLibFunc>();
  else
    F.Impl = std::make_unique<AMDGPUUnmangledLibFunc>();
  if (F.Impl->parseFuncName(FuncName))
    return true;

  F.Impl = std::unique_ptr<AMDGPULibFuncImpl>();
  return false;
}

StringRef AMDGPUMangledLibFunc::getUnmangledName(StringRef mangledName) {
  StringRef S = mangledName;
  if (eatTerm(S, "_Z"))
    return eatLengthPrefixedName(S);
  return StringRef();
}

///////////////////////////////////////////////////////////////////////////////
// Mangling

template <typename Stream>
void AMDGPUMangledLibFunc::writeName(Stream &OS) const {
  const char *Pfx = "";
  switch (FKind) {
  case NATIVE: Pfx = "native_"; break;
  case HALF:   Pfx = "half_";   break;
  default: break;
  }
  if (!Name.empty()) {
    OS << Pfx << Name;
  } else if (FuncId != EI_NONE) {
    OS << Pfx;
    const StringRef& S = manglingRules[FuncId].Name;
    OS.write(S.data(), S.size());
  }
}

std::string AMDGPUMangledLibFunc::mangle() const { return mangleNameItanium(); }

///////////////////////////////////////////////////////////////////////////////
// Itanium Mangling

static const char *getItaniumTypeName(AMDGPULibFunc::EType T) {
  switch (T) {
  case AMDGPULibFunc::U8:      return "h";
  case AMDGPULibFunc::U16:     return "t";
  case AMDGPULibFunc::U32:     return "j";
  case AMDGPULibFunc::U64:     return "m";
  case AMDGPULibFunc::I8:      return "c";
  case AMDGPULibFunc::I16:     return "s";
  case AMDGPULibFunc::I32:     return "i";
  case AMDGPULibFunc::I64:     return "l";
  case AMDGPULibFunc::F16:     return "Dh";
  case AMDGPULibFunc::F32:     return "f";
  case AMDGPULibFunc::F64:     return "d";
  case AMDGPULibFunc::IMG1DA:  return "16ocl_image1darray";
  case AMDGPULibFunc::IMG1DB:  return "17ocl_image1dbuffer";
  case AMDGPULibFunc::IMG2DA:  return "16ocl_image2darray";
  case AMDGPULibFunc::IMG1D:   return "11ocl_image1d";
  case AMDGPULibFunc::IMG2D:   return "11ocl_image2d";
  case AMDGPULibFunc::IMG3D:   return "11ocl_image3d";
  case AMDGPULibFunc::SAMPLER: return "11ocl_sampler";
  case AMDGPULibFunc::EVENT:   return "9ocl_event";
  default:
    llvm_unreachable("Unhandled param type");
  }
  return nullptr;
}

namespace {
// Itanium mangling ABI says:
// "5.1.8. Compression
// ... Each non-terminal in the grammar for which <substitution> appears on the
// right-hand side is both a source of future substitutions and a candidate
// for being substituted. There are two exceptions that appear to be
// substitution candidates from the grammar, but are explicitly excluded:
// 1. <builtin-type> other than vendor extended types ..."

// For the purpose of functions the following productions make sense for the
// substitution:
//  <type> ::= <builtin-type>
//    ::= <class-enum-type>
//    ::= <array-type>
//    ::=<CV-qualifiers> <type>
//    ::= P <type>                # pointer-to
//    ::= <substitution>
//
// Note that while types like images, samplers and events are by the ABI encoded
// using <class-enum-type> production rule they're not used for substitution
// because clang consider them as builtin types.
//
// DvNN_ type is GCC extension for vectors and is a subject for the
// substitution.

class ItaniumMangler {
  SmallVector<AMDGPULibFunc::Param, 10> Str; // list of accumulated substitutions
  bool  UseAddrSpace;

  int findSubst(const AMDGPULibFunc::Param& P) const {
    for(unsigned I = 0; I < Str.size(); ++I) {
      const AMDGPULibFunc::Param& T = Str[I];
      if (P.PtrKind    == T.PtrKind &&
          P.VectorSize == T.VectorSize &&
          P.ArgType    == T.ArgType) {
        return I;
      }
    }
    return -1;
  }

  template <typename Stream>
  bool trySubst(Stream& os, const AMDGPULibFunc::Param& p) {
    int const subst = findSubst(p);
    if (subst < 0) return false;
    // Substitutions are mangled as S(XX)?_ where XX is a hexadecimal number
    // 0   1    2
    // S_  S0_  S1_
    if (subst == 0) os << "S_";
    else os << 'S' << (subst-1) << '_';
    return true;
  }

public:
  ItaniumMangler(bool useAddrSpace)
    : UseAddrSpace(useAddrSpace) {}

  template <typename Stream>
  void operator()(Stream& os, AMDGPULibFunc::Param p) {

    // Itanium mangling ABI 5.1.8. Compression:
    // Logically, the substitutable components of a mangled name are considered
    // left-to-right, components before the composite structure of which they
    // are a part. If a component has been encountered before, it is substituted
    // as described below. This decision is independent of whether its components
    // have been substituted, so an implementation may optimize by considering
    // large structures for substitution before their components. If a component
    // has not been encountered before, its mangling is identified, and it is
    // added to a dictionary of substitution candidates. No entity is added to
    // the dictionary twice.
    AMDGPULibFunc::Param Ptr;

    if (p.PtrKind) {
      if (trySubst(os, p)) return;
      os << 'P';
      if (p.PtrKind & AMDGPULibFunc::CONST) os << 'K';
      if (p.PtrKind & AMDGPULibFunc::VOLATILE) os << 'V';
      unsigned AS = UseAddrSpace
                        ? AMDGPULibFuncBase::getAddrSpaceFromEPtrKind(p.PtrKind)
                        : 0;
      if (EnableOCLManglingMismatchWA || AS != 0)
        os << "U3AS" << AS;
      Ptr = p;
      p.PtrKind = 0;
    }

    if (p.VectorSize > 1) {
      if (trySubst(os, p)) goto exit;
      Str.push_back(p);
      os << "Dv" << static_cast<unsigned>(p.VectorSize) << '_';
    }

    os << getItaniumTypeName((AMDGPULibFunc::EType)p.ArgType);

  exit:
    if (Ptr.ArgType) Str.push_back(Ptr);
  }
};
} // namespace

std::string AMDGPUMangledLibFunc::mangleNameItanium() const {
  SmallString<128> Buf;
  raw_svector_ostream S(Buf);
  SmallString<128> NameBuf;
  raw_svector_ostream Name(NameBuf);
  writeName(Name);
  const StringRef& NameStr = Name.str();
  S << "_Z" << static_cast<int>(NameStr.size()) << NameStr;

  ItaniumMangler Mangler(true);
  ParamIterator I(Leads, manglingRules[FuncId]);
  Param P;
  while ((P = I.getNextParam()).ArgType != 0)
    Mangler(S, P);
  return std::string(S.str());
}

///////////////////////////////////////////////////////////////////////////////
// Misc

AMDGPULibFuncBase::Param AMDGPULibFuncBase::Param::getFromTy(Type *Ty,
                                                             bool Signed) {
  Param P;
  if (FixedVectorType *VT = dyn_cast<FixedVectorType>(Ty)) {
    P.VectorSize = VT->getNumElements();
    Ty = VT->getElementType();
  }

  switch (Ty->getTypeID()) {
  case Type::FloatTyID:
    P.ArgType = AMDGPULibFunc::F32;
    break;
  case Type::DoubleTyID:
    P.ArgType = AMDGPULibFunc::F64;
    break;
  case Type::HalfTyID:
    P.ArgType = AMDGPULibFunc::F16;
    break;
  case Type::IntegerTyID:
    switch (cast<IntegerType>(Ty)->getBitWidth()) {
    case 8:
      P.ArgType = Signed ? AMDGPULibFunc::I8 : AMDGPULibFunc::U8;
      break;
    case 16:
      P.ArgType = Signed ? AMDGPULibFunc::I16 : AMDGPULibFunc::U16;
      break;
    case 32:
      P.ArgType = Signed ? AMDGPULibFunc::I32 : AMDGPULibFunc::U32;
      break;
    case 64:
      P.ArgType = Signed ? AMDGPULibFunc::I64 : AMDGPULibFunc::U64;
      break;
    default:
      llvm_unreachable("unhandled libcall argument type");
    }

    break;
  default:
    llvm_unreachable("unhandled libcall argument type");
  }

  return P;
}

static Type* getIntrinsicParamType(
  LLVMContext& C,
  const AMDGPULibFunc::Param& P,
  bool useAddrSpace) {
  Type* T = nullptr;
  switch (P.ArgType) {
  case AMDGPULibFunc::U8:
  case AMDGPULibFunc::I8:   T = Type::getInt8Ty(C);   break;
  case AMDGPULibFunc::U16:
  case AMDGPULibFunc::I16:  T = Type::getInt16Ty(C);  break;
  case AMDGPULibFunc::U32:
  case AMDGPULibFunc::I32:  T = Type::getInt32Ty(C);  break;
  case AMDGPULibFunc::U64:
  case AMDGPULibFunc::I64:  T = Type::getInt64Ty(C);  break;
  case AMDGPULibFunc::F16:  T = Type::getHalfTy(C);   break;
  case AMDGPULibFunc::F32:  T = Type::getFloatTy(C);  break;
  case AMDGPULibFunc::F64:  T = Type::getDoubleTy(C); break;

  case AMDGPULibFunc::IMG1DA:
  case AMDGPULibFunc::IMG1DB:
  case AMDGPULibFunc::IMG2DA:
  case AMDGPULibFunc::IMG1D:
  case AMDGPULibFunc::IMG2D:
  case AMDGPULibFunc::IMG3D:
    T = StructType::create(C,"ocl_image")->getPointerTo(); break;
  case AMDGPULibFunc::SAMPLER:
    T = StructType::create(C,"ocl_sampler")->getPointerTo(); break;
  case AMDGPULibFunc::EVENT:
    T = StructType::create(C,"ocl_event")->getPointerTo(); break;
  default:
    llvm_unreachable("Unhandled param type");
    return nullptr;
  }
  if (P.VectorSize > 1)
    T = FixedVectorType::get(T, P.VectorSize);
  if (P.PtrKind != AMDGPULibFunc::BYVALUE)
    T = useAddrSpace ? T->getPointerTo((P.PtrKind & AMDGPULibFunc::ADDR_SPACE)
                                       - 1)
                     : T->getPointerTo();
  return T;
}

FunctionType *AMDGPUMangledLibFunc::getFunctionType(Module &M) const {
  LLVMContext& C = M.getContext();
  std::vector<Type*> Args;
  ParamIterator I(Leads, manglingRules[FuncId]);
  Param P;
  while ((P=I.getNextParam()).ArgType != 0)
    Args.push_back(getIntrinsicParamType(C, P, true));

  return FunctionType::get(
    getIntrinsicParamType(C, getRetType(FuncId, Leads), true),
    Args, false);
}

unsigned AMDGPUMangledLibFunc::getNumArgs() const {
  return manglingRules[FuncId].getNumArgs();
}

unsigned AMDGPUUnmangledLibFunc::getNumArgs() const {
  return UnmangledFuncInfo::getNumArgs(FuncId);
}

std::string AMDGPUMangledLibFunc::getName() const {
  SmallString<128> Buf;
  raw_svector_ostream OS(Buf);
  writeName(OS);
  return std::string(OS.str());
}

bool AMDGPULibFunc::isCompatibleSignature(const FunctionType *FuncTy) const {
  // TODO: Validate types make sense
  return !FuncTy->isVarArg() && FuncTy->getNumParams() == getNumArgs();
}

Function *AMDGPULibFunc::getFunction(Module *M, const AMDGPULibFunc &fInfo) {
  std::string FuncName = fInfo.mangle();
  Function *F = dyn_cast_or_null<Function>(
    M->getValueSymbolTable().lookup(FuncName));
  if (!F || F->isDeclaration())
    return nullptr;

  if (F->hasFnAttribute(Attribute::NoBuiltin))
    return nullptr;

  if (!fInfo.isCompatibleSignature(F->getFunctionType()))
    return nullptr;

  return F;
}

FunctionCallee AMDGPULibFunc::getOrInsertFunction(Module *M,
                                                  const AMDGPULibFunc &fInfo) {
  std::string const FuncName = fInfo.mangle();
  Function *F = dyn_cast_or_null<Function>(
    M->getValueSymbolTable().lookup(FuncName));

  if (F) {
    if (F->hasFnAttribute(Attribute::NoBuiltin))
      return nullptr;
    if (!F->isDeclaration() &&
        fInfo.isCompatibleSignature(F->getFunctionType()))
      return F;
  }

  FunctionType *FuncTy = fInfo.getFunctionType(*M);

  bool hasPtr = false;
  for (FunctionType::param_iterator
         PI = FuncTy->param_begin(),
         PE = FuncTy->param_end();
       PI != PE; ++PI) {
    const Type* argTy = static_cast<const Type*>(*PI);
    if (argTy->isPointerTy()) {
      hasPtr = true;
      break;
    }
  }

  FunctionCallee C;
  if (hasPtr) {
    // Do not set extra attributes for functions with pointer arguments.
    C = M->getOrInsertFunction(FuncName, FuncTy);
  } else {
    AttributeList Attr;
    LLVMContext &Ctx = M->getContext();
    Attr = Attr.addFnAttribute(
        Ctx, Attribute::getWithMemoryEffects(Ctx, MemoryEffects::readOnly()));
    Attr = Attr.addFnAttribute(Ctx, Attribute::NoUnwind);
    C = M->getOrInsertFunction(FuncName, FuncTy, Attr);
  }

  return C;
}

StringMap<unsigned> UnmangledFuncInfo::buildNameMap() {
  StringMap<unsigned> Map;
  for (unsigned I = 0; I != TableSize; ++I)
    Map[Table[I].Name] = I;
  return Map;
}

bool UnmangledFuncInfo::lookup(StringRef Name, ID &Id) {
  static const StringMap<unsigned> Map = buildNameMap();
  auto Loc = Map.find(Name);
  if (Loc != Map.end()) {
    Id = toFuncId(Loc->second);
    return true;
  }
  Id = AMDGPULibFunc::EI_NONE;
  return false;
}

AMDGPULibFunc::AMDGPULibFunc(const AMDGPULibFunc &F) {
  if (auto *MF = dyn_cast<AMDGPUMangledLibFunc>(F.Impl.get()))
    Impl = std::make_unique<AMDGPUMangledLibFunc>(*MF);
  else if (auto *UMF = dyn_cast<AMDGPUUnmangledLibFunc>(F.Impl.get()))
    Impl = std::make_unique<AMDGPUUnmangledLibFunc>(*UMF);
  else
    Impl = std::unique_ptr<AMDGPULibFuncImpl>();
}

AMDGPULibFunc &AMDGPULibFunc::operator=(const AMDGPULibFunc &F) {
  if (this == &F)
    return *this;
  new (this) AMDGPULibFunc(F);
  return *this;
}

AMDGPULibFunc::AMDGPULibFunc(EFuncId Id, const AMDGPULibFunc &CopyFrom) {
  assert(AMDGPULibFuncBase::isMangled(Id) && CopyFrom.isMangled() &&
         "not supported");
  Impl = std::make_unique<AMDGPUMangledLibFunc>(
      Id, *cast<AMDGPUMangledLibFunc>(CopyFrom.Impl.get()));
}

AMDGPULibFunc::AMDGPULibFunc(EFuncId Id, FunctionType *FT, bool SignedInts) {
  Impl = std::make_unique<AMDGPUMangledLibFunc>(Id, FT, SignedInts);
}

AMDGPULibFunc::AMDGPULibFunc(StringRef Name, FunctionType *FT) {
  Impl = std::make_unique<AMDGPUUnmangledLibFunc>(Name, FT);
}

void AMDGPULibFunc::initMangled() {
  Impl = std::make_unique<AMDGPUMangledLibFunc>();
}

AMDGPULibFunc::Param *AMDGPULibFunc::getLeads() {
  if (!Impl)
    initMangled();
  return cast<AMDGPUMangledLibFunc>(Impl.get())->Leads;
}

const AMDGPULibFunc::Param *AMDGPULibFunc::getLeads() const {
  return cast<const AMDGPUMangledLibFunc>(Impl.get())->Leads;
}
