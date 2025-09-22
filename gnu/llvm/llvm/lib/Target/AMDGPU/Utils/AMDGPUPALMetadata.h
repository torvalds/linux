//===-- AMDGPUPALMetadata.h - PAL metadata handling -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// PAL metadata handling
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUPALMETADATA_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUPALMETADATA_H
#include "AMDGPUDelayedMCExpr.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/MsgPackDocument.h"
#include "llvm/MC/MCContext.h"

namespace llvm {

class Module;
class StringRef;

class AMDGPUPALMetadata {
public:
  using RegisterExprMap = DenseMap<unsigned, const MCExpr *>;

private:
  unsigned BlobType = 0;
  msgpack::Document MsgPackDoc;
  msgpack::DocNode Registers;
  msgpack::DocNode HwStages;
  msgpack::DocNode ShaderFunctions;
  bool VersionChecked = false;
  msgpack::DocNode Version;
  // From PAL version >= 3.0
  msgpack::DocNode ComputeRegisters;
  msgpack::DocNode GraphicsRegisters;

  DelayedMCExprs DelayedExprs;
  RegisterExprMap REM;
  bool ResolvedAll = true;

public:
  // Read the amdgpu.pal.metadata supplied by the frontend, ready for
  // per-function modification.
  void readFromIR(Module &M);

  // Set PAL metadata from a binary blob from the applicable .note record.
  // Returns false if bad format.  Blob must remain valid for the lifetime of
  // the Metadata.
  bool setFromBlob(unsigned Type, StringRef Blob);

  // Set the rsrc1 register in the metadata for a particular shader stage.
  // In fact this ORs the value into any previous setting of the register.
  void setRsrc1(unsigned CC, unsigned Val);
  void setRsrc1(unsigned CC, const MCExpr *Val, MCContext &Ctx);

  // Set the rsrc2 register in the metadata for a particular shader stage.
  // In fact this ORs the value into any previous setting of the register.
  void setRsrc2(unsigned CC, unsigned Val);
  void setRsrc2(unsigned CC, const MCExpr *Val, MCContext &Ctx);

  // Set the SPI_PS_INPUT_ENA register in the metadata.
  // In fact this ORs the value into any previous setting of the register.
  void setSpiPsInputEna(unsigned Val);

  // Set the SPI_PS_INPUT_ADDR register in the metadata.
  // In fact this ORs the value into any previous setting of the register.
  void setSpiPsInputAddr(unsigned Val);

  // Get a register from the metadata, or 0 if not currently set.
  unsigned getRegister(unsigned Reg);

  // Set a register in the metadata.
  // In fact this ORs the value into any previous setting of the register.
  void setRegister(unsigned Reg, unsigned Val);
  void setRegister(unsigned Reg, const MCExpr *Val, MCContext &Ctx);

  // Set the entry point name for one shader.
  void setEntryPoint(unsigned CC, StringRef Name);

  // Set the number of used vgprs in the metadata. This is an optional advisory
  // record for logging etc; wave dispatch actually uses the rsrc1 register for
  // the shader stage to determine the number of vgprs to allocate.
  void setNumUsedVgprs(unsigned CC, unsigned Val);
  void setNumUsedVgprs(unsigned CC, const MCExpr *Val, MCContext &Ctx);

  // Set the number of used agprs in the metadata. This is an optional advisory
  // record for logging etc;
  void setNumUsedAgprs(unsigned CC, unsigned Val);
  void setNumUsedAgprs(unsigned CC, const MCExpr *Val);

  // Set the number of used sgprs in the metadata. This is an optional advisory
  // record for logging etc; wave dispatch actually uses the rsrc1 register for
  // the shader stage to determine the number of sgprs to allocate.
  void setNumUsedSgprs(unsigned CC, unsigned Val);
  void setNumUsedSgprs(unsigned CC, const MCExpr *Val, MCContext &Ctx);

  // Set the scratch size in the metadata.
  void setScratchSize(unsigned CC, unsigned Val);
  void setScratchSize(unsigned CC, const MCExpr *Val, MCContext &Ctx);

  // Set the stack frame size of a function in the metadata.
  void setFunctionScratchSize(StringRef FnName, unsigned Val);

  // Set the amount of LDS used in bytes in the metadata. This is an optional
  // advisory record for logging etc; wave dispatch actually uses the rsrc1
  // register for the shader stage to determine the amount of LDS to allocate.
  void setFunctionLdsSize(StringRef FnName, unsigned Val);

  // Set the number of used vgprs in the metadata. This is an optional advisory
  // record for logging etc; wave dispatch actually uses the rsrc1 register for
  // the shader stage to determine the number of vgprs to allocate.
  void setFunctionNumUsedVgprs(StringRef FnName, unsigned Val);
  void setFunctionNumUsedVgprs(StringRef FnName, const MCExpr *Val);

  // Set the number of used sgprs in the metadata. This is an optional advisory
  // record for logging etc; wave dispatch actually uses the rsrc1 register for
  // the shader stage to determine the number of sgprs to allocate.
  void setFunctionNumUsedSgprs(StringRef FnName, unsigned Val);
  void setFunctionNumUsedSgprs(StringRef FnName, const MCExpr *Val);

  // Set the hardware register bit in PAL metadata to enable wave32 on the
  // shader of the given calling convention.
  void setWave32(unsigned CC);

  // Emit the accumulated PAL metadata as asm directives.
  // This is called from AMDGPUTargetAsmStreamer::Finish().
  void toString(std::string &S);

  // Set PAL metadata from YAML text.
  bool setFromString(StringRef S);

  // Get .note record vendor name of metadata blob to be emitted.
  const char *getVendor() const;

  // Get .note record type of metadata blob to be emitted:
  // ELF::NT_AMD_PAL_METADATA (legacy key=val format), or
  // ELF::NT_AMDGPU_METADATA (MsgPack format), or
  // 0 (no PAL metadata).
  unsigned getType() const;

  // Emit the accumulated PAL metadata as a binary blob.
  // This is called from AMDGPUTargetELFStreamer::Finish().
  void toBlob(unsigned Type, std::string &S);

  // Get the msgpack::Document for the PAL metadata.
  msgpack::Document *getMsgPackDoc() { return &MsgPackDoc; }

  // Set legacy PAL metadata format.
  void setLegacy();

  unsigned getPALMajorVersion();
  unsigned getPALMinorVersion();

  void setHwStage(unsigned CC, StringRef field, unsigned Val);
  void setHwStage(unsigned CC, StringRef field, bool Val);
  void setHwStage(unsigned CC, StringRef field, msgpack::Type Type,
                  const MCExpr *Val);

  void setComputeRegisters(StringRef field, unsigned Val);
  void setComputeRegisters(StringRef field, bool Val);

  // If the field does not exist will return nullptr rather than creating a new
  // entry (which is the behaviour of the other functions).
  msgpack::DocNode *refComputeRegister(StringRef field);
  bool checkComputeRegisters(StringRef field, unsigned Val);
  bool checkComputeRegisters(StringRef field, bool Val);

  void setGraphicsRegisters(StringRef field, unsigned Val);
  void setGraphicsRegisters(StringRef field, bool Val);
  void setGraphicsRegisters(StringRef field1, StringRef field2, unsigned Val);
  void setGraphicsRegisters(StringRef field1, StringRef field2, bool Val);

  // Erase all PAL metadata.
  void reset();

  bool resolvedAllMCExpr();

private:
  // Return whether the blob type is legacy PAL metadata.
  bool isLegacy() const;

  // Reference (create if necessary) the node for the registers map.
  msgpack::DocNode &refRegisters();

  // Get (create if necessary) the registers map.
  msgpack::MapDocNode getRegisters();

  // Reference (create if necessary) the node for the shader functions map.
  msgpack::DocNode &refShaderFunctions();

  // Get (create if necessary) the shader functions map.
  msgpack::MapDocNode getShaderFunctions();

  // Get (create if necessary) a function in the shader functions map.
  msgpack::MapDocNode getShaderFunction(StringRef Name);

  // Reference (create if necessary) the node for the compute_registers map.
  msgpack::DocNode &refComputeRegisters();

  // Get (create if necessary) the .compute_registers entry.
  msgpack::MapDocNode getComputeRegisters();

  // Reference (create if necessary) the node for the graphics registers map.
  msgpack::DocNode &refGraphicsRegisters();

  // Get (create if necessary) the .graphics_registers entry.
  msgpack::MapDocNode getGraphicsRegisters();

  // Reference (create if necessary) the node for the hardware_stages map.
  msgpack::DocNode &refHwStage();

  // Get (create if necessary) the .hardware_stages entry for the given calling
  // convention.
  msgpack::MapDocNode getHwStage(unsigned CC);

  // Get the PAL version major (idx 0) or minor (idx 1). This is an internal
  // helper for the public wrapper functions that request Major or Minor
  unsigned getPALVersion(unsigned idx);

  bool setFromLegacyBlob(StringRef Blob);
  bool setFromMsgPackBlob(StringRef Blob);
  void toLegacyBlob(std::string &Blob);
  void toMsgPackBlob(std::string &Blob);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUPALMETADATA_H
