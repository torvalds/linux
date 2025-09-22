//===--- NVPTX.h - Declare NVPTX target feature support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares NVPTX TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_NVPTX_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_NVPTX_H

#include "clang/Basic/Cuda.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

namespace clang {
namespace targets {

static const unsigned NVPTXAddrSpaceMap[] = {
    0, // Default
    1, // opencl_global
    3, // opencl_local
    4, // opencl_constant
    0, // opencl_private
    // FIXME: generic has to be added to the target
    0, // opencl_generic
    1, // opencl_global_device
    1, // opencl_global_host
    1, // cuda_device
    4, // cuda_constant
    3, // cuda_shared
    1, // sycl_global
    1, // sycl_global_device
    1, // sycl_global_host
    3, // sycl_local
    0, // sycl_private
    0, // ptr32_sptr
    0, // ptr32_uptr
    0, // ptr64
    0, // hlsl_groupshared
    // Wasm address space values for this target are dummy values,
    // as it is only enabled for Wasm targets.
    20, // wasm_funcref
};

/// The DWARF address class. Taken from
/// https://docs.nvidia.com/cuda/archive/10.0/ptx-writers-guide-to-interoperability/index.html#cuda-specific-dwarf
static const int NVPTXDWARFAddrSpaceMap[] = {
    -1, // Default, opencl_private or opencl_generic - not defined
    5,  // opencl_global
    -1,
    8,  // opencl_local or cuda_shared
    4,  // opencl_constant or cuda_constant
};

class LLVM_LIBRARY_VISIBILITY NVPTXTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];
  OffloadArch GPU;
  uint32_t PTXVersion;
  std::unique_ptr<TargetInfo> HostTarget;

public:
  NVPTXTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts,
                  unsigned TargetPointerWidth);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool useFP16ConversionIntrinsics() const override { return false; }

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override {
    if (GPU != OffloadArch::UNUSED)
      Features[OffloadArchToString(GPU)] = true;
    Features["ptx" + std::to_string(PTXVersion)] = true;
    return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
  }

  bool hasFeature(StringRef Feature) const override;

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    // No aliases.
    return std::nullopt;
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    switch (*Name) {
    default:
      return false;
    case 'c':
    case 'h':
    case 'r':
    case 'l':
    case 'f':
    case 'd':
    case 'q':
      Info.setAllowsRegister();
      return true;
    }
  }

  std::string_view getClobbers() const override {
    // FIXME: Is this really right?
    return "";
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  bool isValidCPUName(StringRef Name) const override {
    return StringToOffloadArch(Name) != OffloadArch::UNKNOWN;
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {
    for (int i = static_cast<int>(OffloadArch::SM_20);
         i < static_cast<int>(OffloadArch::Generic); ++i)
      Values.emplace_back(OffloadArchToString(static_cast<OffloadArch>(i)));
  }

  bool setCPU(const std::string &Name) override {
    GPU = StringToOffloadArch(Name);
    return GPU != OffloadArch::UNKNOWN;
  }

  void setSupportedOpenCLOpts() override {
    auto &Opts = getSupportedOpenCLOpts();
    Opts["cl_clang_storage_class_specifiers"] = true;
    Opts["__cl_clang_function_pointers"] = true;
    Opts["__cl_clang_variadic_functions"] = true;
    Opts["__cl_clang_non_portable_kernel_param_types"] = true;
    Opts["__cl_clang_bitfields"] = true;

    Opts["cl_khr_fp64"] = true;
    Opts["__opencl_c_fp64"] = true;
    Opts["cl_khr_byte_addressable_store"] = true;
    Opts["cl_khr_global_int32_base_atomics"] = true;
    Opts["cl_khr_global_int32_extended_atomics"] = true;
    Opts["cl_khr_local_int32_base_atomics"] = true;
    Opts["cl_khr_local_int32_extended_atomics"] = true;
  }

  const llvm::omp::GV &getGridValue() const override {
    return llvm::omp::NVPTXGridValues;
  }

  /// \returns If a target requires an address within a target specific address
  /// space \p AddressSpace to be converted in order to be used, then return the
  /// corresponding target specific DWARF address space.
  ///
  /// \returns Otherwise return std::nullopt and no conversion will be emitted
  /// in the DWARF.
  std::optional<unsigned>
  getDWARFAddressSpace(unsigned AddressSpace) const override {
    if (AddressSpace >= std::size(NVPTXDWARFAddrSpaceMap) ||
        NVPTXDWARFAddrSpaceMap[AddressSpace] < 0)
      return std::nullopt;
    return NVPTXDWARFAddrSpaceMap[AddressSpace];
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    // CUDA compilations support all of the host's calling conventions.
    //
    // TODO: We should warn if you apply a non-default CC to anything other than
    // a host function.
    if (HostTarget)
      return HostTarget->checkCallingConvention(CC);
    return CCCR_Warning;
  }

  bool hasBitIntType() const override { return true; }
  bool hasBFloat16Type() const override { return true; }

  OffloadArch getGPU() const { return GPU; }
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_NVPTX_H
