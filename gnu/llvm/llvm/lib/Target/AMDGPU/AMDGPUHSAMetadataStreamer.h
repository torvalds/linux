//===--- AMDGPUHSAMetadataStreamer.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU HSA Metadata Streamer.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUHSAMETADATASTREAMER_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUHSAMETADATASTREAMER_H

#include "Utils/AMDGPUDelayedMCExpr.h"
#include "llvm/BinaryFormat/MsgPackDocument.h"
#include "llvm/Support/AMDGPUMetadata.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class AMDGPUTargetStreamer;
class Argument;
class DataLayout;
class Function;
class MachineFunction;
class MDNode;
class Module;
struct SIProgramInfo;
class Type;

namespace AMDGPU {

namespace IsaInfo {
class AMDGPUTargetID;
}

namespace HSAMD {

class MetadataStreamer {
public:
  virtual ~MetadataStreamer() = default;

  virtual bool emitTo(AMDGPUTargetStreamer &TargetStreamer) = 0;

  virtual void begin(const Module &Mod,
                     const IsaInfo::AMDGPUTargetID &TargetID) = 0;

  virtual void end() = 0;

  virtual void emitKernel(const MachineFunction &MF,
                          const SIProgramInfo &ProgramInfo) = 0;

protected:
  virtual void emitVersion() = 0;
  virtual void emitHiddenKernelArgs(const MachineFunction &MF, unsigned &Offset,
                                    msgpack::ArrayDocNode Args) = 0;
  virtual void emitKernelAttrs(const Function &Func,
                               msgpack::MapDocNode Kern) = 0;
};

class LLVM_EXTERNAL_VISIBILITY MetadataStreamerMsgPackV4
    : public MetadataStreamer {
protected:
  std::unique_ptr<DelayedMCExprs> DelayedExprs =
      std::make_unique<DelayedMCExprs>();

  std::unique_ptr<msgpack::Document> HSAMetadataDoc =
      std::make_unique<msgpack::Document>();

  void dump(StringRef HSAMetadataString) const;

  void verify(StringRef HSAMetadataString) const;

  std::optional<StringRef> getAccessQualifier(StringRef AccQual) const;

  std::optional<StringRef>
  getAddressSpaceQualifier(unsigned AddressSpace) const;

  StringRef getValueKind(Type *Ty, StringRef TypeQual,
                         StringRef BaseTypeName) const;

  std::string getTypeName(Type *Ty, bool Signed) const;

  msgpack::ArrayDocNode getWorkGroupDimensions(MDNode *Node) const;

  msgpack::MapDocNode getHSAKernelProps(const MachineFunction &MF,
                                        const SIProgramInfo &ProgramInfo,
                                        unsigned CodeObjectVersion) const;

  void emitVersion() override;

  void emitTargetID(const IsaInfo::AMDGPUTargetID &TargetID);

  void emitPrintf(const Module &Mod);

  void emitKernelLanguage(const Function &Func, msgpack::MapDocNode Kern);

  void emitKernelAttrs(const Function &Func, msgpack::MapDocNode Kern) override;

  void emitKernelArgs(const MachineFunction &MF, msgpack::MapDocNode Kern);

  void emitKernelArg(const Argument &Arg, unsigned &Offset,
                     msgpack::ArrayDocNode Args);

  void emitKernelArg(const DataLayout &DL, Type *Ty, Align Alignment,
                     StringRef ValueKind, unsigned &Offset,
                     msgpack::ArrayDocNode Args,
                     MaybeAlign PointeeAlign = std::nullopt,
                     StringRef Name = "", StringRef TypeName = "",
                     StringRef BaseTypeName = "", StringRef ActAccQual = "",
                     StringRef AccQual = "", StringRef TypeQual = "");

  void emitHiddenKernelArgs(const MachineFunction &MF, unsigned &Offset,
                            msgpack::ArrayDocNode Args) override;

  msgpack::DocNode &getRootMetadata(StringRef Key) {
    return HSAMetadataDoc->getRoot().getMap(/*Convert=*/true)[Key];
  }

  msgpack::DocNode &getHSAMetadataRoot() {
    return HSAMetadataDoc->getRoot();
  }

public:
  MetadataStreamerMsgPackV4() = default;
  ~MetadataStreamerMsgPackV4() = default;

  bool emitTo(AMDGPUTargetStreamer &TargetStreamer) override;

  void begin(const Module &Mod,
             const IsaInfo::AMDGPUTargetID &TargetID) override;

  void end() override;

  void emitKernel(const MachineFunction &MF,
                  const SIProgramInfo &ProgramInfo) override;
};

class MetadataStreamerMsgPackV5 : public MetadataStreamerMsgPackV4 {
protected:
  void emitVersion() override;
  void emitHiddenKernelArgs(const MachineFunction &MF, unsigned &Offset,
                            msgpack::ArrayDocNode Args) override;
  void emitKernelAttrs(const Function &Func, msgpack::MapDocNode Kern) override;

public:
  MetadataStreamerMsgPackV5() = default;
  ~MetadataStreamerMsgPackV5() = default;
};

class MetadataStreamerMsgPackV6 final : public MetadataStreamerMsgPackV5 {
protected:
  void emitVersion() override;

public:
  MetadataStreamerMsgPackV6() = default;
  ~MetadataStreamerMsgPackV6() = default;
};

} // end namespace HSAMD
} // end namespace AMDGPU
} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUHSAMETADATASTREAMER_H
