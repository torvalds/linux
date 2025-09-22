//===-- AMDGPULibFunc.h ----------------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _AMDGPU_LIBFUNC_H_
#define _AMDGPU_LIBFUNC_H_

#include "llvm/ADT/StringRef.h"
#include <memory>

namespace llvm {

class FunctionCallee;
class FunctionType;
class Function;
class Module;
class Type;

class AMDGPULibFuncBase {
public:
  enum EFuncId {
    EI_NONE,

    // IMPORTANT: enums below should go in ascending by 1 value order
    // because they are used as indexes in the mangling rules table.
    // don't use explicit value assignment.
    //
    // There are two types of library functions: those with mangled
    // name and those with unmangled name. The enums for the library
    // functions with mangled name are defined before enums for the
    // library functions with unmangled name. The enum for the last
    // library function with mangled name is EI_LAST_MANGLED.
    //
    // Library functions with mangled name.
    EI_ABS,
    EI_ABS_DIFF,
    EI_ACOS,
    EI_ACOSH,
    EI_ACOSPI,
    EI_ADD_SAT,
    EI_ALL,
    EI_ANY,
    EI_ASIN,
    EI_ASINH,
    EI_ASINPI,
    EI_ASYNC_WORK_GROUP_COPY,
    EI_ASYNC_WORK_GROUP_STRIDED_COPY,
    EI_ATAN,
    EI_ATAN2,
    EI_ATAN2PI,
    EI_ATANH,
    EI_ATANPI,
    EI_ATOMIC_ADD,
    EI_ATOMIC_AND,
    EI_ATOMIC_CMPXCHG,
    EI_ATOMIC_DEC,
    EI_ATOMIC_INC,
    EI_ATOMIC_MAX,
    EI_ATOMIC_MIN,
    EI_ATOMIC_OR,
    EI_ATOMIC_SUB,
    EI_ATOMIC_XCHG,
    EI_ATOMIC_XOR,
    EI_BITSELECT,
    EI_CBRT,
    EI_CEIL,
    EI_CLAMP,
    EI_CLZ,
    EI_COMMIT_READ_PIPE,
    EI_COMMIT_WRITE_PIPE,
    EI_COPYSIGN,
    EI_COS,
    EI_COSH,
    EI_COSPI,
    EI_CROSS,
    EI_CTZ,
    EI_DEGREES,
    EI_DISTANCE,
    EI_DIVIDE,
    EI_DOT,
    EI_ERF,
    EI_ERFC,
    EI_EXP,
    EI_EXP10,
    EI_EXP2,
    EI_EXPM1,
    EI_FABS,
    EI_FAST_DISTANCE,
    EI_FAST_LENGTH,
    EI_FAST_NORMALIZE,
    EI_FDIM,
    EI_FLOOR,
    EI_FMA,
    EI_FMAX,
    EI_FMIN,
    EI_FMOD,
    EI_FRACT,
    EI_FREXP,
    EI_GET_IMAGE_ARRAY_SIZE,
    EI_GET_IMAGE_CHANNEL_DATA_TYPE,
    EI_GET_IMAGE_CHANNEL_ORDER,
    EI_GET_IMAGE_DIM,
    EI_GET_IMAGE_HEIGHT,
    EI_GET_IMAGE_WIDTH,
    EI_GET_PIPE_MAX_PACKETS,
    EI_GET_PIPE_NUM_PACKETS,
    EI_HADD,
    EI_HYPOT,
    EI_ILOGB,
    EI_ISEQUAL,
    EI_ISFINITE,
    EI_ISGREATER,
    EI_ISGREATEREQUAL,
    EI_ISINF,
    EI_ISLESS,
    EI_ISLESSEQUAL,
    EI_ISLESSGREATER,
    EI_ISNAN,
    EI_ISNORMAL,
    EI_ISNOTEQUAL,
    EI_ISORDERED,
    EI_ISUNORDERED,
    EI_LDEXP,
    EI_LENGTH,
    EI_LGAMMA,
    EI_LGAMMA_R,
    EI_LOG,
    EI_LOG10,
    EI_LOG1P,
    EI_LOG2,
    EI_LOGB,
    EI_MAD,
    EI_MAD24,
    EI_MAD_HI,
    EI_MAD_SAT,
    EI_MAX,
    EI_MAXMAG,
    EI_MIN,
    EI_MINMAG,
    EI_MIX,
    EI_MODF,
    EI_MUL24,
    EI_MUL_HI,
    EI_NAN,
    EI_NEXTAFTER,
    EI_NORMALIZE,
    EI_POPCOUNT,
    EI_POW,
    EI_POWN,
    EI_POWR,
    EI_PREFETCH,
    EI_RADIANS,
    EI_RECIP,
    EI_REMAINDER,
    EI_REMQUO,
    EI_RESERVE_READ_PIPE,
    EI_RESERVE_WRITE_PIPE,
    EI_RHADD,
    EI_RINT,
    EI_ROOTN,
    EI_ROTATE,
    EI_ROUND,
    EI_RSQRT,
    EI_SELECT,
    EI_SHUFFLE,
    EI_SHUFFLE2,
    EI_SIGN,
    EI_SIGNBIT,
    EI_SIN,
    EI_SINCOS,
    EI_SINH,
    EI_SINPI,
    EI_SMOOTHSTEP,
    EI_SQRT,
    EI_STEP,
    EI_SUB_GROUP_BROADCAST,
    EI_SUB_GROUP_COMMIT_READ_PIPE,
    EI_SUB_GROUP_COMMIT_WRITE_PIPE,
    EI_SUB_GROUP_REDUCE_ADD,
    EI_SUB_GROUP_REDUCE_MAX,
    EI_SUB_GROUP_REDUCE_MIN,
    EI_SUB_GROUP_RESERVE_READ_PIPE,
    EI_SUB_GROUP_RESERVE_WRITE_PIPE,
    EI_SUB_GROUP_SCAN_EXCLUSIVE_ADD,
    EI_SUB_GROUP_SCAN_EXCLUSIVE_MAX,
    EI_SUB_GROUP_SCAN_EXCLUSIVE_MIN,
    EI_SUB_GROUP_SCAN_INCLUSIVE_ADD,
    EI_SUB_GROUP_SCAN_INCLUSIVE_MAX,
    EI_SUB_GROUP_SCAN_INCLUSIVE_MIN,
    EI_SUB_SAT,
    EI_TAN,
    EI_TANH,
    EI_TANPI,
    EI_TGAMMA,
    EI_TRUNC,
    EI_UPSAMPLE,
    EI_VEC_STEP,
    EI_VSTORE,
    EI_VSTORE16,
    EI_VSTORE2,
    EI_VSTORE3,
    EI_VSTORE4,
    EI_VSTORE8,
    EI_WORK_GROUP_COMMIT_READ_PIPE,
    EI_WORK_GROUP_COMMIT_WRITE_PIPE,
    EI_WORK_GROUP_REDUCE_ADD,
    EI_WORK_GROUP_REDUCE_MAX,
    EI_WORK_GROUP_REDUCE_MIN,
    EI_WORK_GROUP_RESERVE_READ_PIPE,
    EI_WORK_GROUP_RESERVE_WRITE_PIPE,
    EI_WORK_GROUP_SCAN_EXCLUSIVE_ADD,
    EI_WORK_GROUP_SCAN_EXCLUSIVE_MAX,
    EI_WORK_GROUP_SCAN_EXCLUSIVE_MIN,
    EI_WORK_GROUP_SCAN_INCLUSIVE_ADD,
    EI_WORK_GROUP_SCAN_INCLUSIVE_MAX,
    EI_WORK_GROUP_SCAN_INCLUSIVE_MIN,
    EI_WRITE_IMAGEF,
    EI_WRITE_IMAGEI,
    EI_WRITE_IMAGEUI,
    EI_NCOS,
    EI_NEXP2,
    EI_NFMA,
    EI_NLOG2,
    EI_NRCP,
    EI_NRSQRT,
    EI_NSIN,
    EI_NSQRT,
    EI_FTZ,
    EI_FLDEXP,
    EI_CLASS,
    EI_RCBRT,
    EI_LAST_MANGLED =
        EI_RCBRT, /* The last library function with mangled name */

    // Library functions with unmangled name.
    EI_READ_PIPE_2,
    EI_READ_PIPE_4,
    EI_WRITE_PIPE_2,
    EI_WRITE_PIPE_4,

    EX_INTRINSICS_COUNT
  };

  enum ENamePrefix {
    NOPFX,
    NATIVE,
    HALF
  };

  enum EType {
    B8  = 1,
    B16 = 2,
    B32 = 3,
    B64 = 4,
    SIZE_MASK = 7,
    FLOAT = 0x10,
    INT   = 0x20,
    UINT  = 0x30,
    BASE_TYPE_MASK = 0x30,
    U8  =  UINT | B8,
    U16 =  UINT | B16,
    U32 =  UINT | B32,
    U64 =  UINT | B64,
    I8  =   INT | B8,
    I16 =   INT | B16,
    I32 =   INT | B32,
    I64 =   INT | B64,
    F16 = FLOAT | B16,
    F32 = FLOAT | B32,
    F64 = FLOAT | B64,
    IMG1DA = 0x80,
    IMG1DB,
    IMG2DA,
    IMG1D,
    IMG2D,
    IMG3D,
    SAMPLER,
    EVENT,
    DUMMY
  };

  enum EPtrKind {
    BYVALUE = 0,
    ADDR_SPACE = 0xF, // Address space takes value 0x1 ~ 0xF.
    CONST      = 0x10,
    VOLATILE   = 0x20
  };

  struct Param {
    unsigned char ArgType = 0;
    unsigned char VectorSize = 1;
    unsigned char PtrKind = 0;

    unsigned char Reserved = 0;

    void reset() {
      ArgType = 0;
      VectorSize = 1;
      PtrKind = 0;
    }

    static Param getIntN(unsigned char NumElts) {
      return Param{I32, NumElts, 0, 0};
    }

    static Param getFromTy(Type *Ty, bool Signed);

    template <typename Stream>
    void mangleItanium(Stream& os);
  };
  static bool isMangled(EFuncId Id) {
    return static_cast<unsigned>(Id) <= static_cast<unsigned>(EI_LAST_MANGLED);
  }

  static unsigned getEPtrKindFromAddrSpace(unsigned AS) {
    assert(((AS + 1) & ~ADDR_SPACE) == 0);
    return AS + 1;
  }

  static unsigned getAddrSpaceFromEPtrKind(unsigned Kind) {
    Kind = Kind & ADDR_SPACE;
    assert(Kind >= 1);
    return Kind - 1;
  }
};

class AMDGPULibFuncImpl : public AMDGPULibFuncBase {
public:
  AMDGPULibFuncImpl() = default;
  virtual ~AMDGPULibFuncImpl() = default;

  /// Get unmangled name for mangled library function and name for unmangled
  /// library function.
  virtual std::string getName() const = 0;
  virtual unsigned getNumArgs() const = 0;
  EFuncId getId() const { return FuncId; }
  ENamePrefix getPrefix() const { return FKind; }

  bool isMangled() const { return AMDGPULibFuncBase::isMangled(FuncId); }

  void setId(EFuncId id) { FuncId = id; }
  virtual bool parseFuncName(StringRef &mangledName) = 0;

  /// \return The mangled function name for mangled library functions
  /// and unmangled function name for unmangled library functions.
  virtual std::string mangle() const = 0;

  void setName(StringRef N) { Name = std::string(N); }
  void setPrefix(ENamePrefix pfx) { FKind = pfx; }

  virtual FunctionType *getFunctionType(Module &M) const = 0;

protected:
  EFuncId FuncId;
  std::string Name;
  ENamePrefix FKind = NOPFX;
};

/// Wrapper class for AMDGPULIbFuncImpl
class AMDGPULibFunc : public AMDGPULibFuncBase {
public:
  explicit AMDGPULibFunc() : Impl(std::unique_ptr<AMDGPULibFuncImpl>()) {}
  AMDGPULibFunc(const AMDGPULibFunc &F);
  /// Clone a mangled library func with the Id \p Id and argument info from \p
  /// CopyFrom.
  explicit AMDGPULibFunc(EFuncId Id, const AMDGPULibFunc &CopyFrom);
  explicit AMDGPULibFunc(EFuncId Id, FunctionType *FT, bool SignedInts);

  /// Construct an unmangled library function on the fly.
  explicit AMDGPULibFunc(StringRef FName, FunctionType *FT);

  AMDGPULibFunc &operator=(const AMDGPULibFunc &F);

  /// Get unmangled name for mangled library function and name for unmangled
  /// library function.
  std::string getName() const { return Impl->getName(); }
  unsigned getNumArgs() const { return Impl->getNumArgs(); }
  EFuncId getId() const { return Impl->getId(); }
  ENamePrefix getPrefix() const { return Impl->getPrefix(); }
  /// Get leading parameters for mangled lib functions.
  Param *getLeads();
  const Param *getLeads() const;

  bool isMangled() const { return Impl->isMangled(); }
  void setId(EFuncId Id) { Impl->setId(Id); }
  bool parseFuncName(StringRef &MangledName) {
    return Impl->parseFuncName(MangledName);
  }

  // Validate the call type matches the expected libfunc type.
  bool isCompatibleSignature(const FunctionType *FuncTy) const;

  /// \return The mangled function name for mangled library functions
  /// and unmangled function name for unmangled library functions.
  std::string mangle() const { return Impl->mangle(); }

  void setName(StringRef N) { Impl->setName(N); }
  void setPrefix(ENamePrefix PFX) { Impl->setPrefix(PFX); }

  FunctionType *getFunctionType(Module &M) const {
    return Impl->getFunctionType(M);
  }
  static Function *getFunction(llvm::Module *M, const AMDGPULibFunc &fInfo);

  static FunctionCallee getOrInsertFunction(llvm::Module *M,
                                            const AMDGPULibFunc &fInfo);
  static bool parse(StringRef MangledName, AMDGPULibFunc &Ptr);

private:
  /// Initialize as a mangled library function.
  void initMangled();
  std::unique_ptr<AMDGPULibFuncImpl> Impl;
};

class AMDGPUMangledLibFunc : public AMDGPULibFuncImpl {
public:
  Param Leads[2];

  explicit AMDGPUMangledLibFunc();
  explicit AMDGPUMangledLibFunc(EFuncId id,
                                const AMDGPUMangledLibFunc &copyFrom);
  explicit AMDGPUMangledLibFunc(EFuncId id, FunctionType *FT,
                                bool SignedInts = true);

  std::string getName() const override;
  unsigned getNumArgs() const override;
  FunctionType *getFunctionType(Module &M) const override;
  static StringRef getUnmangledName(StringRef MangledName);

  bool parseFuncName(StringRef &mangledName) override;

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const AMDGPULibFuncImpl *F) { return F->isMangled(); }

  std::string mangle() const override;

private:
  std::string mangleNameItanium() const;

  std::string mangleName(StringRef Name) const;
  bool parseUnmangledName(StringRef MangledName);

  template <typename Stream> void writeName(Stream &OS) const;
};

class AMDGPUUnmangledLibFunc : public AMDGPULibFuncImpl {
  FunctionType *FuncTy;

public:
  explicit AMDGPUUnmangledLibFunc();
  explicit AMDGPUUnmangledLibFunc(StringRef FName, FunctionType *FT) {
    Name = std::string(FName);
    FuncTy = FT;
  }
  std::string getName() const override { return Name; }
  unsigned getNumArgs() const override;
  FunctionType *getFunctionType(Module &M) const override { return FuncTy; }

  bool parseFuncName(StringRef &Name) override;

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const AMDGPULibFuncImpl *F) { return !F->isMangled(); }

  std::string mangle() const override { return Name; }

  void setFunctionType(FunctionType *FT) { FuncTy = FT; }
};
}
#endif // _AMDGPU_LIBFUNC_H_
