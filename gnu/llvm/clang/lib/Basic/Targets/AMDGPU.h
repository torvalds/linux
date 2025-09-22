//===--- AMDGPU.h - Declare AMDGPU target feature support -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares AMDGPU TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_AMDGPU_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_AMDGPU_H

#include "clang/Basic/TargetID.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/AMDGPUAddrSpace.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY AMDGPUTargetInfo final : public TargetInfo {

  static const char *const GCCRegNames[];

  static const LangASMap AMDGPUDefIsGenMap;
  static const LangASMap AMDGPUDefIsPrivMap;

  llvm::AMDGPU::GPUKind GPUKind;
  unsigned GPUFeatures;
  unsigned WavefrontSize;

  /// Whether to use cumode or WGP mode. True for cumode. False for WGP mode.
  bool CUMode;

  /// Whether having image instructions.
  bool HasImage = false;

  /// Target ID is device name followed by optional feature name postfixed
  /// by plus or minus sign delimitted by colon, e.g. gfx908:xnack+:sramecc-.
  /// If the target ID contains feature+, map it to true.
  /// If the target ID contains feature-, map it to false.
  /// If the target ID does not contain a feature (default), do not map it.
  llvm::StringMap<bool> OffloadArchFeatures;
  std::string TargetID;

  bool hasFP64() const {
    return getTriple().getArch() == llvm::Triple::amdgcn ||
           !!(GPUFeatures & llvm::AMDGPU::FEATURE_FP64);
  }

  /// Has fast fma f32
  bool hasFastFMAF() const {
    return !!(GPUFeatures & llvm::AMDGPU::FEATURE_FAST_FMA_F32);
  }

  /// Has fast fma f64
  bool hasFastFMA() const {
    return getTriple().getArch() == llvm::Triple::amdgcn;
  }

  bool hasFMAF() const {
    return getTriple().getArch() == llvm::Triple::amdgcn ||
           !!(GPUFeatures & llvm::AMDGPU::FEATURE_FMA);
  }

  bool hasFullRateDenormalsF32() const {
    return !!(GPUFeatures & llvm::AMDGPU::FEATURE_FAST_DENORMAL_F32);
  }

  bool hasLDEXPF() const {
    return getTriple().getArch() == llvm::Triple::amdgcn ||
           !!(GPUFeatures & llvm::AMDGPU::FEATURE_LDEXP);
  }

  static bool isAMDGCN(const llvm::Triple &TT) {
    return TT.getArch() == llvm::Triple::amdgcn;
  }

  static bool isR600(const llvm::Triple &TT) {
    return TT.getArch() == llvm::Triple::r600;
  }

public:
  AMDGPUTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void setAddressSpaceMap(bool DefaultIsPrivate);

  void adjust(DiagnosticsEngine &Diags, LangOptions &Opts) override;

  uint64_t getPointerWidthV(LangAS AS) const override {
    if (isR600(getTriple()))
      return 32;
    unsigned TargetAS = getTargetAddressSpace(AS);

    if (TargetAS == llvm::AMDGPUAS::PRIVATE_ADDRESS ||
        TargetAS == llvm::AMDGPUAS::LOCAL_ADDRESS)
      return 32;

    return 64;
  }

  uint64_t getPointerAlignV(LangAS AddrSpace) const override {
    return getPointerWidthV(AddrSpace);
  }

  uint64_t getMaxPointerWidth() const override {
    return getTriple().getArch() == llvm::Triple::amdgcn ? 64 : 32;
  }

  bool hasBFloat16Type() const override { return isAMDGCN(getTriple()); }

  std::string_view getClobbers() const override { return ""; }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return std::nullopt;
  }

  /// Accepted register names: (n, m is unsigned integer, n < m)
  /// v
  /// s
  /// a
  /// {vn}, {v[n]}
  /// {sn}, {s[n]}
  /// {an}, {a[n]}
  /// {S} , where S is a special register name
  ////{v[n:m]}
  /// {s[n:m]}
  /// {a[n:m]}
  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    static const ::llvm::StringSet<> SpecialRegs({
        "exec", "vcc", "flat_scratch", "m0", "scc", "tba", "tma",
        "flat_scratch_lo", "flat_scratch_hi", "vcc_lo", "vcc_hi", "exec_lo",
        "exec_hi", "tma_lo", "tma_hi", "tba_lo", "tba_hi",
    });

    switch (*Name) {
    case 'I':
      Info.setRequiresImmediate(-16, 64);
      return true;
    case 'J':
      Info.setRequiresImmediate(-32768, 32767);
      return true;
    case 'A':
    case 'B':
    case 'C':
      Info.setRequiresImmediate();
      return true;
    default:
      break;
    }

    StringRef S(Name);

    if (S == "DA" || S == "DB") {
      Name++;
      Info.setRequiresImmediate();
      return true;
    }

    bool HasLeftParen = S.consume_front("{");
    if (S.empty())
      return false;
    if (S.front() != 'v' && S.front() != 's' && S.front() != 'a') {
      if (!HasLeftParen)
        return false;
      auto E = S.find('}');
      if (!SpecialRegs.count(S.substr(0, E)))
        return false;
      S = S.drop_front(E + 1);
      if (!S.empty())
        return false;
      // Found {S} where S is a special register.
      Info.setAllowsRegister();
      Name = S.data() - 1;
      return true;
    }
    S = S.drop_front();
    if (!HasLeftParen) {
      if (!S.empty())
        return false;
      // Found s, v or a.
      Info.setAllowsRegister();
      Name = S.data() - 1;
      return true;
    }
    bool HasLeftBracket = S.consume_front("[");
    unsigned long long N;
    if (S.empty() || consumeUnsignedInteger(S, 10, N))
      return false;
    if (S.consume_front(":")) {
      if (!HasLeftBracket)
        return false;
      unsigned long long M;
      if (consumeUnsignedInteger(S, 10, M) || N >= M)
        return false;
    }
    if (HasLeftBracket) {
      if (!S.consume_front("]"))
        return false;
    }
    if (!S.consume_front("}"))
      return false;
    if (!S.empty())
      return false;
    // Found {vn}, {sn}, {an}, {v[n]}, {s[n]}, {a[n]}, {v[n:m]}, {s[n:m]}
    // or {a[n:m]}.
    Info.setAllowsRegister();
    Name = S.data() - 1;
    return true;
  }

  // \p Constraint will be left pointing at the last character of
  // the constraint.  In practice, it won't be changed unless the
  // constraint is longer than one character.
  std::string convertConstraint(const char *&Constraint) const override {

    StringRef S(Constraint);
    if (S == "DA" || S == "DB") {
      return std::string("^") + std::string(Constraint++, 2);
    }

    const char *Begin = Constraint;
    TargetInfo::ConstraintInfo Info("", "");
    if (validateAsmConstraint(Constraint, Info))
      return std::string(Begin).substr(0, Constraint - Begin + 1);

    Constraint = Begin;
    return std::string(1, *Constraint);
  }

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeatureVec) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool useFP16ConversionIntrinsics() const override { return false; }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }

  bool isValidCPUName(StringRef Name) const override {
    if (getTriple().getArch() == llvm::Triple::amdgcn)
      return llvm::AMDGPU::parseArchAMDGCN(Name) != llvm::AMDGPU::GK_NONE;
    return llvm::AMDGPU::parseArchR600(Name) != llvm::AMDGPU::GK_NONE;
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override {
    if (getTriple().getArch() == llvm::Triple::amdgcn) {
      GPUKind = llvm::AMDGPU::parseArchAMDGCN(Name);
      GPUFeatures = llvm::AMDGPU::getArchAttrAMDGCN(GPUKind);
    } else {
      GPUKind = llvm::AMDGPU::parseArchR600(Name);
      GPUFeatures = llvm::AMDGPU::getArchAttrR600(GPUKind);
    }

    return GPUKind != llvm::AMDGPU::GK_NONE;
  }

  void setSupportedOpenCLOpts() override {
    auto &Opts = getSupportedOpenCLOpts();
    Opts["cl_clang_storage_class_specifiers"] = true;
    Opts["__cl_clang_variadic_functions"] = true;
    Opts["__cl_clang_function_pointers"] = true;
    Opts["__cl_clang_non_portable_kernel_param_types"] = true;
    Opts["__cl_clang_bitfields"] = true;

    bool IsAMDGCN = isAMDGCN(getTriple());

    Opts["cl_khr_fp64"] = hasFP64();
    Opts["__opencl_c_fp64"] = hasFP64();

    if (IsAMDGCN || GPUKind >= llvm::AMDGPU::GK_CEDAR) {
      Opts["cl_khr_byte_addressable_store"] = true;
      Opts["cl_khr_global_int32_base_atomics"] = true;
      Opts["cl_khr_global_int32_extended_atomics"] = true;
      Opts["cl_khr_local_int32_base_atomics"] = true;
      Opts["cl_khr_local_int32_extended_atomics"] = true;
    }

    if (IsAMDGCN) {
      Opts["cl_khr_fp16"] = true;
      Opts["cl_khr_int64_base_atomics"] = true;
      Opts["cl_khr_int64_extended_atomics"] = true;
      Opts["cl_khr_mipmap_image"] = true;
      Opts["cl_khr_mipmap_image_writes"] = true;
      Opts["cl_khr_subgroups"] = true;
      Opts["cl_amd_media_ops"] = true;
      Opts["cl_amd_media_ops2"] = true;

      Opts["__opencl_c_images"] = true;
      Opts["__opencl_c_3d_image_writes"] = true;
      Opts["cl_khr_3d_image_writes"] = true;
    }
  }

  LangAS getOpenCLTypeAddrSpace(OpenCLTypeKind TK) const override {
    switch (TK) {
    case OCLTK_Image:
      return LangAS::opencl_constant;

    case OCLTK_ClkEvent:
    case OCLTK_Queue:
    case OCLTK_ReserveID:
      return LangAS::opencl_global;

    default:
      return TargetInfo::getOpenCLTypeAddrSpace(TK);
    }
  }

  LangAS getOpenCLBuiltinAddressSpace(unsigned AS) const override {
    switch (AS) {
    case 0:
      return LangAS::opencl_generic;
    case 1:
      return LangAS::opencl_global;
    case 3:
      return LangAS::opencl_local;
    case 4:
      return LangAS::opencl_constant;
    case 5:
      return LangAS::opencl_private;
    default:
      return getLangASFromTargetAS(AS);
    }
  }

  LangAS getCUDABuiltinAddressSpace(unsigned AS) const override {
    switch (AS) {
    case 0:
      return LangAS::Default;
    case 1:
      return LangAS::cuda_device;
    case 3:
      return LangAS::cuda_shared;
    case 4:
      return LangAS::cuda_constant;
    default:
      return getLangASFromTargetAS(AS);
    }
  }

  std::optional<LangAS> getConstantAddressSpace() const override {
    return getLangASFromTargetAS(llvm::AMDGPUAS::CONSTANT_ADDRESS);
  }

  const llvm::omp::GV &getGridValue() const override {
    switch (WavefrontSize) {
    case 32:
      return llvm::omp::getAMDGPUGridValues<32>();
    case 64:
      return llvm::omp::getAMDGPUGridValues<64>();
    default:
      llvm_unreachable("getGridValue not implemented for this wavesize");
    }
  }

  /// \returns Target specific vtbl ptr address space.
  unsigned getVtblPtrAddressSpace() const override {
    return static_cast<unsigned>(llvm::AMDGPUAS::CONSTANT_ADDRESS);
  }

  /// \returns If a target requires an address within a target specific address
  /// space \p AddressSpace to be converted in order to be used, then return the
  /// corresponding target specific DWARF address space.
  ///
  /// \returns Otherwise return std::nullopt and no conversion will be emitted
  /// in the DWARF.
  std::optional<unsigned>
  getDWARFAddressSpace(unsigned AddressSpace) const override {
    const unsigned DWARF_Private = 1;
    const unsigned DWARF_Local = 2;
    if (AddressSpace == llvm::AMDGPUAS::PRIVATE_ADDRESS) {
      return DWARF_Private;
    } else if (AddressSpace == llvm::AMDGPUAS::LOCAL_ADDRESS) {
      return DWARF_Local;
    } else {
      return std::nullopt;
    }
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    default:
      return CCCR_Warning;
    case CC_C:
    case CC_OpenCLKernel:
    case CC_AMDGPUKernelCall:
      return CCCR_OK;
    }
  }

  // In amdgcn target the null pointer in global, constant, and generic
  // address space has value 0 but in private and local address space has
  // value ~0.
  uint64_t getNullPointerValue(LangAS AS) const override {
    // FIXME: Also should handle region.
    return (AS == LangAS::opencl_local || AS == LangAS::opencl_private ||
            AS == LangAS::sycl_local || AS == LangAS::sycl_private)
               ? ~0
               : 0;
  }

  void setAuxTarget(const TargetInfo *Aux) override;

  bool hasBitIntType() const override { return true; }

  // Record offload arch features since they are needed for defining the
  // pre-defined macros.
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override {
    auto TargetIDFeatures =
        getAllPossibleTargetIDFeatures(getTriple(), getArchNameAMDGCN(GPUKind));
    for (const auto &F : Features) {
      assert(F.front() == '+' || F.front() == '-');
      if (F == "+wavefrontsize64")
        WavefrontSize = 64;
      else if (F == "+cumode")
        CUMode = true;
      else if (F == "-cumode")
        CUMode = false;
      else if (F == "+image-insts")
        HasImage = true;
      bool IsOn = F.front() == '+';
      StringRef Name = StringRef(F).drop_front();
      if (!llvm::is_contained(TargetIDFeatures, Name))
        continue;
      assert(!OffloadArchFeatures.contains(Name));
      OffloadArchFeatures[Name] = IsOn;
    }
    return true;
  }

  std::optional<std::string> getTargetID() const override {
    if (!isAMDGCN(getTriple()))
      return std::nullopt;
    // When -target-cpu is not set, we assume generic code that it is valid
    // for all GPU and use an empty string as target ID to represent that.
    if (GPUKind == llvm::AMDGPU::GK_NONE)
      return std::string("");
    return getCanonicalTargetID(getArchNameAMDGCN(GPUKind),
                                OffloadArchFeatures);
  }

  bool hasHIPImageSupport() const override { return HasImage; }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_AMDGPU_H
