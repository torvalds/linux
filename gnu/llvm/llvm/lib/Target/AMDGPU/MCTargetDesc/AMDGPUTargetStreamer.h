//===-- AMDGPUTargetStreamer.h - AMDGPU Target Streamer --------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUTARGETSTREAMER_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUTARGETSTREAMER_H

#include "Utils/AMDGPUBaseInfo.h"
#include "Utils/AMDGPUPALMetadata.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {

class MCELFStreamer;
class MCSymbol;
class formatted_raw_ostream;

namespace AMDGPU {

struct AMDGPUMCKernelCodeT;
struct MCKernelDescriptor;
namespace HSAMD {
struct Metadata;
}
} // namespace AMDGPU

class AMDGPUTargetStreamer : public MCTargetStreamer {
  AMDGPUPALMetadata PALMetadata;

protected:
  // TODO: Move HSAMetadataStream to AMDGPUTargetStreamer.
  std::optional<AMDGPU::IsaInfo::AMDGPUTargetID> TargetID;
  unsigned CodeObjectVersion;

  MCContext &getContext() const { return Streamer.getContext(); }

public:
  AMDGPUTargetStreamer(MCStreamer &S)
      : MCTargetStreamer(S),
        // Assume the default COV for now, EmitDirectiveAMDHSACodeObjectVersion
        // will update this if it is encountered.
        CodeObjectVersion(AMDGPU::getDefaultAMDHSACodeObjectVersion()) {}

  AMDGPUPALMetadata *getPALMetadata() { return &PALMetadata; }

  virtual void EmitDirectiveAMDGCNTarget(){};

  virtual void EmitDirectiveAMDHSACodeObjectVersion(unsigned COV) {
    CodeObjectVersion = COV;
  }

  virtual void EmitAMDKernelCodeT(AMDGPU::AMDGPUMCKernelCodeT &Header) {};

  virtual void EmitAMDGPUSymbolType(StringRef SymbolName, unsigned Type){};

  virtual void emitAMDGPULDS(MCSymbol *Symbol, unsigned Size, Align Alignment) {
  }

  /// \returns True on success, false on failure.
  virtual bool EmitISAVersion() { return true; }

  /// \returns True on success, false on failure.
  virtual bool EmitHSAMetadataV3(StringRef HSAMetadataString);

  /// Emit HSA Metadata
  ///
  /// When \p Strict is true, known metadata elements must already be
  /// well-typed. When \p Strict is false, known types are inferred and
  /// the \p HSAMetadata structure is updated with the correct types.
  ///
  /// \returns True on success, false on failure.
  virtual bool EmitHSAMetadata(msgpack::Document &HSAMetadata, bool Strict) {
    return true;
  }

  /// \returns True on success, false on failure.
  virtual bool EmitHSAMetadata(const AMDGPU::HSAMD::Metadata &HSAMetadata) {
    return true;
  }

  /// \returns True on success, false on failure.
  virtual bool EmitCodeEnd(const MCSubtargetInfo &STI) { return true; }

  /// \returns True on success, false on failure.
  virtual bool EmitKernargPreloadHeader(const MCSubtargetInfo &STI,
                                        bool TrapEnabled) {
    return true;
  }

  virtual void
  EmitAmdhsaKernelDescriptor(const MCSubtargetInfo &STI, StringRef KernelName,
                             const AMDGPU::MCKernelDescriptor &KernelDescriptor,
                             const MCExpr *NextVGPR, const MCExpr *NextSGPR,
                             const MCExpr *ReserveVCC,
                             const MCExpr *ReserveFlatScr) {}

  static StringRef getArchNameFromElfMach(unsigned ElfMach);
  static unsigned getElfMach(StringRef GPU);

  const std::optional<AMDGPU::IsaInfo::AMDGPUTargetID> &getTargetID() const {
    return TargetID;
  }
  std::optional<AMDGPU::IsaInfo::AMDGPUTargetID> &getTargetID() {
    return TargetID;
  }
  void initializeTargetID(const MCSubtargetInfo &STI) {
    assert(TargetID == std::nullopt && "TargetID can only be initialized once");
    TargetID.emplace(STI);
  }
  void initializeTargetID(const MCSubtargetInfo &STI, StringRef FeatureString) {
    initializeTargetID(STI);

    assert(getTargetID() != std::nullopt && "TargetID is None");
    getTargetID()->setTargetIDFromFeaturesString(FeatureString);
  }
};

class AMDGPUTargetAsmStreamer final : public AMDGPUTargetStreamer {
  formatted_raw_ostream &OS;
public:
  AMDGPUTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void finish() override;

  void EmitDirectiveAMDGCNTarget() override;

  void EmitDirectiveAMDHSACodeObjectVersion(unsigned COV) override;

  void EmitAMDKernelCodeT(AMDGPU::AMDGPUMCKernelCodeT &Header) override;

  void EmitAMDGPUSymbolType(StringRef SymbolName, unsigned Type) override;

  void emitAMDGPULDS(MCSymbol *Sym, unsigned Size, Align Alignment) override;

  /// \returns True on success, false on failure.
  bool EmitISAVersion() override;

  /// \returns True on success, false on failure.
  bool EmitHSAMetadata(msgpack::Document &HSAMetadata, bool Strict) override;

  /// \returns True on success, false on failure.
  bool EmitCodeEnd(const MCSubtargetInfo &STI) override;

  /// \returns True on success, false on failure.
  bool EmitKernargPreloadHeader(const MCSubtargetInfo &STI,
                                bool TrapEnabled) override;

  void
  EmitAmdhsaKernelDescriptor(const MCSubtargetInfo &STI, StringRef KernelName,
                             const AMDGPU::MCKernelDescriptor &KernelDescriptor,
                             const MCExpr *NextVGPR, const MCExpr *NextSGPR,
                             const MCExpr *ReserveVCC,
                             const MCExpr *ReserveFlatScr) override;
};

class AMDGPUTargetELFStreamer final : public AMDGPUTargetStreamer {
  const MCSubtargetInfo &STI;
  MCStreamer &Streamer;

  void EmitNote(StringRef Name, const MCExpr *DescSize, unsigned NoteType,
                function_ref<void(MCELFStreamer &)> EmitDesc);

  unsigned getEFlags();

  unsigned getEFlagsR600();
  unsigned getEFlagsAMDGCN();

  unsigned getEFlagsUnknownOS();
  unsigned getEFlagsAMDHSA();
  unsigned getEFlagsAMDPAL();
  unsigned getEFlagsMesa3D();

  unsigned getEFlagsV3();
  unsigned getEFlagsV4();
  unsigned getEFlagsV6();

public:
  AMDGPUTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  MCELFStreamer &getStreamer();

  void finish() override;

  void EmitDirectiveAMDGCNTarget() override;

  void EmitAMDKernelCodeT(AMDGPU::AMDGPUMCKernelCodeT &Header) override;

  void EmitAMDGPUSymbolType(StringRef SymbolName, unsigned Type) override;

  void emitAMDGPULDS(MCSymbol *Sym, unsigned Size, Align Alignment) override;

  /// \returns True on success, false on failure.
  bool EmitISAVersion() override;

  /// \returns True on success, false on failure.
  bool EmitHSAMetadata(msgpack::Document &HSAMetadata, bool Strict) override;

  /// \returns True on success, false on failure.
  bool EmitCodeEnd(const MCSubtargetInfo &STI) override;

  /// \returns True on success, false on failure.
  bool EmitKernargPreloadHeader(const MCSubtargetInfo &STI,
                                bool TrapEnabled) override;

  void
  EmitAmdhsaKernelDescriptor(const MCSubtargetInfo &STI, StringRef KernelName,
                             const AMDGPU::MCKernelDescriptor &KernelDescriptor,
                             const MCExpr *NextVGPR, const MCExpr *NextSGPR,
                             const MCExpr *ReserveVCC,
                             const MCExpr *ReserveFlatScr) override;
};
}
#endif
