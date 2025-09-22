// WebAssemblyMachineFunctionInfo.h-WebAssembly machine function info-*- C++ -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares WebAssembly-specific per-machine-function
/// information.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYMACHINEFUNCTIONINFO_H

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/MCSymbolWasm.h"

namespace llvm {
class WebAssemblyTargetLowering;

struct WasmEHFuncInfo;

namespace yaml {
struct WebAssemblyFunctionInfo;
}

/// This class is derived from MachineFunctionInfo and contains private
/// WebAssembly-specific information for each MachineFunction.
class WebAssemblyFunctionInfo final : public MachineFunctionInfo {
  std::vector<MVT> Params;
  std::vector<MVT> Results;
  std::vector<MVT> Locals;

  /// A mapping from CodeGen vreg index to WebAssembly register number.
  std::vector<unsigned> WARegs;

  /// A mapping from CodeGen vreg index to a boolean value indicating whether
  /// the given register is considered to be "stackified", meaning it has been
  /// determined or made to meet the stack requirements:
  ///   - single use (per path)
  ///   - single def (per path)
  ///   - defined and used in LIFO order with other stack registers
  BitVector VRegStackified;

  // A virtual register holding the pointer to the vararg buffer for vararg
  // functions. It is created and set in TLI::LowerFormalArguments and read by
  // TLI::LowerVASTART
  unsigned VarargVreg = -1U;

  // A virtual register holding the base pointer for functions that have
  // overaligned values on the user stack.
  unsigned BasePtrVreg = -1U;
  // A virtual register holding the frame base. This is either FP or SP
  // after it has been replaced by a vreg
  unsigned FrameBaseVreg = -1U;
  // The local holding the frame base. This is either FP or SP
  // after WebAssemblyExplicitLocals
  unsigned FrameBaseLocal = -1U;

  // Function properties.
  bool CFGStackified = false;

public:
  explicit WebAssemblyFunctionInfo(const Function &F,
                                   const TargetSubtargetInfo *STI) {}
  ~WebAssemblyFunctionInfo() override;

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  void initializeBaseYamlFields(MachineFunction &MF,
                                const yaml::WebAssemblyFunctionInfo &YamlMFI);

  void addParam(MVT VT) { Params.push_back(VT); }
  const std::vector<MVT> &getParams() const { return Params; }

  void addResult(MVT VT) { Results.push_back(VT); }
  const std::vector<MVT> &getResults() const { return Results; }

  void clearParamsAndResults() {
    Params.clear();
    Results.clear();
  }

  void setNumLocals(size_t NumLocals) { Locals.resize(NumLocals, MVT::i32); }
  void setLocal(size_t i, MVT VT) { Locals[i] = VT; }
  void addLocal(MVT VT) { Locals.push_back(VT); }
  const std::vector<MVT> &getLocals() const { return Locals; }

  unsigned getVarargBufferVreg() const {
    assert(VarargVreg != -1U && "Vararg vreg hasn't been set");
    return VarargVreg;
  }
  void setVarargBufferVreg(unsigned Reg) { VarargVreg = Reg; }

  unsigned getBasePointerVreg() const {
    assert(BasePtrVreg != -1U && "Base ptr vreg hasn't been set");
    return BasePtrVreg;
  }
  void setFrameBaseVreg(unsigned Reg) { FrameBaseVreg = Reg; }
  unsigned getFrameBaseVreg() const {
    assert(FrameBaseVreg != -1U && "Frame base vreg hasn't been set");
    return FrameBaseVreg;
  }
  void clearFrameBaseVreg() { FrameBaseVreg = -1U; }
  // Return true if the frame base physreg has been replaced by a virtual reg.
  bool isFrameBaseVirtual() const { return FrameBaseVreg != -1U; }
  void setFrameBaseLocal(unsigned Local) { FrameBaseLocal = Local; }
  unsigned getFrameBaseLocal() const {
    assert(FrameBaseLocal != -1U && "Frame base local hasn't been set");
    return FrameBaseLocal;
  }
  void setBasePointerVreg(unsigned Reg) { BasePtrVreg = Reg; }

  void stackifyVReg(MachineRegisterInfo &MRI, unsigned VReg) {
    assert(MRI.getUniqueVRegDef(VReg));
    auto I = Register::virtReg2Index(VReg);
    if (I >= VRegStackified.size())
      VRegStackified.resize(I + 1);
    VRegStackified.set(I);
  }
  void unstackifyVReg(unsigned VReg) {
    auto I = Register::virtReg2Index(VReg);
    if (I < VRegStackified.size())
      VRegStackified.reset(I);
  }
  bool isVRegStackified(unsigned VReg) const {
    auto I = Register::virtReg2Index(VReg);
    if (I >= VRegStackified.size())
      return false;
    return VRegStackified.test(I);
  }

  void initWARegs(MachineRegisterInfo &MRI);
  void setWAReg(unsigned VReg, unsigned WAReg) {
    assert(WAReg != WebAssembly::UnusedReg);
    auto I = Register::virtReg2Index(VReg);
    assert(I < WARegs.size());
    WARegs[I] = WAReg;
  }
  unsigned getWAReg(unsigned VReg) const {
    auto I = Register::virtReg2Index(VReg);
    assert(I < WARegs.size());
    return WARegs[I];
  }

  bool isCFGStackified() const { return CFGStackified; }
  void setCFGStackified(bool Value = true) { CFGStackified = Value; }
};

void computeLegalValueVTs(const WebAssemblyTargetLowering &TLI,
                          LLVMContext &Ctx, const DataLayout &DL, Type *Ty,
                          SmallVectorImpl<MVT> &ValueVTs);

void computeLegalValueVTs(const Function &F, const TargetMachine &TM, Type *Ty,
                          SmallVectorImpl<MVT> &ValueVTs);

// Compute the signature for a given FunctionType (Ty). Note that it's not the
// signature for ContextFunc (ContextFunc is just used to get varous context)
void computeSignatureVTs(const FunctionType *Ty, const Function *TargetFunc,
                         const Function &ContextFunc, const TargetMachine &TM,
                         SmallVectorImpl<MVT> &Params,
                         SmallVectorImpl<MVT> &Results);

void valTypesFromMVTs(ArrayRef<MVT> In, SmallVectorImpl<wasm::ValType> &Out);

wasm::WasmSignature *signatureFromMVTs(MCContext &Ctx,
                                       const SmallVectorImpl<MVT> &Results,
                                       const SmallVectorImpl<MVT> &Params);

namespace yaml {

using BBNumberMap = DenseMap<int, int>;

struct WebAssemblyFunctionInfo final : public yaml::MachineFunctionInfo {
  std::vector<FlowStringValue> Params;
  std::vector<FlowStringValue> Results;
  bool CFGStackified = false;
  // The same as WasmEHFuncInfo's SrcToUnwindDest, but stored in the mapping of
  // BB numbers
  BBNumberMap SrcToUnwindDest;

  WebAssemblyFunctionInfo() = default;
  WebAssemblyFunctionInfo(const llvm::MachineFunction &MF,
                          const llvm::WebAssemblyFunctionInfo &MFI);

  void mappingImpl(yaml::IO &YamlIO) override;
  ~WebAssemblyFunctionInfo() = default;
};

template <> struct MappingTraits<WebAssemblyFunctionInfo> {
  static void mapping(IO &YamlIO, WebAssemblyFunctionInfo &MFI) {
    YamlIO.mapOptional("params", MFI.Params, std::vector<FlowStringValue>());
    YamlIO.mapOptional("results", MFI.Results, std::vector<FlowStringValue>());
    YamlIO.mapOptional("isCFGStackified", MFI.CFGStackified, false);
    YamlIO.mapOptional("wasmEHFuncInfo", MFI.SrcToUnwindDest);
  }
};

template <> struct CustomMappingTraits<BBNumberMap> {
  static void inputOne(IO &YamlIO, StringRef Key,
                       BBNumberMap &SrcToUnwindDest) {
    YamlIO.mapRequired(Key.str().c_str(),
                       SrcToUnwindDest[std::atoi(Key.str().c_str())]);
  }

  static void output(IO &YamlIO, BBNumberMap &SrcToUnwindDest) {
    for (auto KV : SrcToUnwindDest)
      YamlIO.mapRequired(std::to_string(KV.first).c_str(), KV.second);
  }
};

} // end namespace yaml

} // end namespace llvm

#endif
