//===--- AMDGPUHSAMetadataStreamer.cpp --------------------------*- C++ -*-===//
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

#include "AMDGPUHSAMetadataStreamer.h"
#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUTargetStreamer.h"
#include "SIMachineFunctionInfo.h"
#include "SIProgramInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
using namespace llvm;

static std::pair<Type *, Align> getArgumentTypeAlign(const Argument &Arg,
                                                     const DataLayout &DL) {
  Type *Ty = Arg.getType();
  MaybeAlign ArgAlign;
  if (Arg.hasByRefAttr()) {
    Ty = Arg.getParamByRefType();
    ArgAlign = Arg.getParamAlign();
  }

  if (!ArgAlign)
    ArgAlign = DL.getABITypeAlign(Ty);

  return std::pair(Ty, *ArgAlign);
}

namespace llvm {

static cl::opt<bool> DumpHSAMetadata(
    "amdgpu-dump-hsa-metadata",
    cl::desc("Dump AMDGPU HSA Metadata"));
static cl::opt<bool> VerifyHSAMetadata(
    "amdgpu-verify-hsa-metadata",
    cl::desc("Verify AMDGPU HSA Metadata"));

namespace AMDGPU::HSAMD {

//===----------------------------------------------------------------------===//
// HSAMetadataStreamerV4
//===----------------------------------------------------------------------===//

void MetadataStreamerMsgPackV4::dump(StringRef HSAMetadataString) const {
  errs() << "AMDGPU HSA Metadata:\n" << HSAMetadataString << '\n';
}

void MetadataStreamerMsgPackV4::verify(StringRef HSAMetadataString) const {
  errs() << "AMDGPU HSA Metadata Parser Test: ";

  msgpack::Document FromHSAMetadataString;

  if (!FromHSAMetadataString.fromYAML(HSAMetadataString)) {
    errs() << "FAIL\n";
    return;
  }

  std::string ToHSAMetadataString;
  raw_string_ostream StrOS(ToHSAMetadataString);
  FromHSAMetadataString.toYAML(StrOS);

  errs() << (HSAMetadataString == StrOS.str() ? "PASS" : "FAIL") << '\n';
  if (HSAMetadataString != ToHSAMetadataString) {
    errs() << "Original input: " << HSAMetadataString << '\n'
           << "Produced output: " << StrOS.str() << '\n';
  }
}

std::optional<StringRef>
MetadataStreamerMsgPackV4::getAccessQualifier(StringRef AccQual) const {
  return StringSwitch<std::optional<StringRef>>(AccQual)
      .Case("read_only", StringRef("read_only"))
      .Case("write_only", StringRef("write_only"))
      .Case("read_write", StringRef("read_write"))
      .Default(std::nullopt);
}

std::optional<StringRef> MetadataStreamerMsgPackV4::getAddressSpaceQualifier(
    unsigned AddressSpace) const {
  switch (AddressSpace) {
  case AMDGPUAS::PRIVATE_ADDRESS:
    return StringRef("private");
  case AMDGPUAS::GLOBAL_ADDRESS:
    return StringRef("global");
  case AMDGPUAS::CONSTANT_ADDRESS:
    return StringRef("constant");
  case AMDGPUAS::LOCAL_ADDRESS:
    return StringRef("local");
  case AMDGPUAS::FLAT_ADDRESS:
    return StringRef("generic");
  case AMDGPUAS::REGION_ADDRESS:
    return StringRef("region");
  default:
    return std::nullopt;
  }
}

StringRef
MetadataStreamerMsgPackV4::getValueKind(Type *Ty, StringRef TypeQual,
                                        StringRef BaseTypeName) const {
  if (TypeQual.contains("pipe"))
    return "pipe";

  return StringSwitch<StringRef>(BaseTypeName)
      .Case("image1d_t", "image")
      .Case("image1d_array_t", "image")
      .Case("image1d_buffer_t", "image")
      .Case("image2d_t", "image")
      .Case("image2d_array_t", "image")
      .Case("image2d_array_depth_t", "image")
      .Case("image2d_array_msaa_t", "image")
      .Case("image2d_array_msaa_depth_t", "image")
      .Case("image2d_depth_t", "image")
      .Case("image2d_msaa_t", "image")
      .Case("image2d_msaa_depth_t", "image")
      .Case("image3d_t", "image")
      .Case("sampler_t", "sampler")
      .Case("queue_t", "queue")
      .Default(isa<PointerType>(Ty)
                   ? (Ty->getPointerAddressSpace() == AMDGPUAS::LOCAL_ADDRESS
                          ? "dynamic_shared_pointer"
                          : "global_buffer")
                   : "by_value");
}

std::string MetadataStreamerMsgPackV4::getTypeName(Type *Ty,
                                                   bool Signed) const {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID: {
    if (!Signed)
      return (Twine('u') + getTypeName(Ty, true)).str();

    auto BitWidth = Ty->getIntegerBitWidth();
    switch (BitWidth) {
    case 8:
      return "char";
    case 16:
      return "short";
    case 32:
      return "int";
    case 64:
      return "long";
    default:
      return (Twine('i') + Twine(BitWidth)).str();
    }
  }
  case Type::HalfTyID:
    return "half";
  case Type::FloatTyID:
    return "float";
  case Type::DoubleTyID:
    return "double";
  case Type::FixedVectorTyID: {
    auto VecTy = cast<FixedVectorType>(Ty);
    auto ElTy = VecTy->getElementType();
    auto NumElements = VecTy->getNumElements();
    return (Twine(getTypeName(ElTy, Signed)) + Twine(NumElements)).str();
  }
  default:
    return "unknown";
  }
}

msgpack::ArrayDocNode
MetadataStreamerMsgPackV4::getWorkGroupDimensions(MDNode *Node) const {
  auto Dims = HSAMetadataDoc->getArrayNode();
  if (Node->getNumOperands() != 3)
    return Dims;

  for (auto &Op : Node->operands())
    Dims.push_back(Dims.getDocument()->getNode(
        uint64_t(mdconst::extract<ConstantInt>(Op)->getZExtValue())));
  return Dims;
}

void MetadataStreamerMsgPackV4::emitVersion() {
  auto Version = HSAMetadataDoc->getArrayNode();
  Version.push_back(Version.getDocument()->getNode(VersionMajorV4));
  Version.push_back(Version.getDocument()->getNode(VersionMinorV4));
  getRootMetadata("amdhsa.version") = Version;
}

void MetadataStreamerMsgPackV4::emitTargetID(
    const IsaInfo::AMDGPUTargetID &TargetID) {
  getRootMetadata("amdhsa.target") =
      HSAMetadataDoc->getNode(TargetID.toString(), /*Copy=*/true);
}

void MetadataStreamerMsgPackV4::emitPrintf(const Module &Mod) {
  auto Node = Mod.getNamedMetadata("llvm.printf.fmts");
  if (!Node)
    return;

  auto Printf = HSAMetadataDoc->getArrayNode();
  for (auto *Op : Node->operands())
    if (Op->getNumOperands())
      Printf.push_back(Printf.getDocument()->getNode(
          cast<MDString>(Op->getOperand(0))->getString(), /*Copy=*/true));
  getRootMetadata("amdhsa.printf") = Printf;
}

void MetadataStreamerMsgPackV4::emitKernelLanguage(const Function &Func,
                                                   msgpack::MapDocNode Kern) {
  // TODO: What about other languages?
  auto Node = Func.getParent()->getNamedMetadata("opencl.ocl.version");
  if (!Node || !Node->getNumOperands())
    return;
  auto Op0 = Node->getOperand(0);
  if (Op0->getNumOperands() <= 1)
    return;

  Kern[".language"] = Kern.getDocument()->getNode("OpenCL C");
  auto LanguageVersion = Kern.getDocument()->getArrayNode();
  LanguageVersion.push_back(Kern.getDocument()->getNode(
      mdconst::extract<ConstantInt>(Op0->getOperand(0))->getZExtValue()));
  LanguageVersion.push_back(Kern.getDocument()->getNode(
      mdconst::extract<ConstantInt>(Op0->getOperand(1))->getZExtValue()));
  Kern[".language_version"] = LanguageVersion;
}

void MetadataStreamerMsgPackV4::emitKernelAttrs(const Function &Func,
                                                msgpack::MapDocNode Kern) {

  if (auto Node = Func.getMetadata("reqd_work_group_size"))
    Kern[".reqd_workgroup_size"] = getWorkGroupDimensions(Node);
  if (auto Node = Func.getMetadata("work_group_size_hint"))
    Kern[".workgroup_size_hint"] = getWorkGroupDimensions(Node);
  if (auto Node = Func.getMetadata("vec_type_hint")) {
    Kern[".vec_type_hint"] = Kern.getDocument()->getNode(
        getTypeName(
            cast<ValueAsMetadata>(Node->getOperand(0))->getType(),
            mdconst::extract<ConstantInt>(Node->getOperand(1))->getZExtValue()),
        /*Copy=*/true);
  }
  if (Func.hasFnAttribute("runtime-handle")) {
    Kern[".device_enqueue_symbol"] = Kern.getDocument()->getNode(
        Func.getFnAttribute("runtime-handle").getValueAsString().str(),
        /*Copy=*/true);
  }
  if (Func.hasFnAttribute("device-init"))
    Kern[".kind"] = Kern.getDocument()->getNode("init");
  else if (Func.hasFnAttribute("device-fini"))
    Kern[".kind"] = Kern.getDocument()->getNode("fini");
}

void MetadataStreamerMsgPackV4::emitKernelArgs(const MachineFunction &MF,
                                               msgpack::MapDocNode Kern) {
  auto &Func = MF.getFunction();
  unsigned Offset = 0;
  auto Args = HSAMetadataDoc->getArrayNode();
  for (auto &Arg : Func.args())
    emitKernelArg(Arg, Offset, Args);

  emitHiddenKernelArgs(MF, Offset, Args);

  Kern[".args"] = Args;
}

void MetadataStreamerMsgPackV4::emitKernelArg(const Argument &Arg,
                                              unsigned &Offset,
                                              msgpack::ArrayDocNode Args) {
  auto Func = Arg.getParent();
  auto ArgNo = Arg.getArgNo();
  const MDNode *Node;

  StringRef Name;
  Node = Func->getMetadata("kernel_arg_name");
  if (Node && ArgNo < Node->getNumOperands())
    Name = cast<MDString>(Node->getOperand(ArgNo))->getString();
  else if (Arg.hasName())
    Name = Arg.getName();

  StringRef TypeName;
  Node = Func->getMetadata("kernel_arg_type");
  if (Node && ArgNo < Node->getNumOperands())
    TypeName = cast<MDString>(Node->getOperand(ArgNo))->getString();

  StringRef BaseTypeName;
  Node = Func->getMetadata("kernel_arg_base_type");
  if (Node && ArgNo < Node->getNumOperands())
    BaseTypeName = cast<MDString>(Node->getOperand(ArgNo))->getString();

  StringRef ActAccQual;
  // Do we really need NoAlias check here?
  if (Arg.getType()->isPointerTy() && Arg.hasNoAliasAttr()) {
    if (Arg.onlyReadsMemory())
      ActAccQual = "read_only";
    else if (Arg.hasAttribute(Attribute::WriteOnly))
      ActAccQual = "write_only";
  }

  StringRef AccQual;
  Node = Func->getMetadata("kernel_arg_access_qual");
  if (Node && ArgNo < Node->getNumOperands())
    AccQual = cast<MDString>(Node->getOperand(ArgNo))->getString();

  StringRef TypeQual;
  Node = Func->getMetadata("kernel_arg_type_qual");
  if (Node && ArgNo < Node->getNumOperands())
    TypeQual = cast<MDString>(Node->getOperand(ArgNo))->getString();

  const DataLayout &DL = Func->getDataLayout();

  MaybeAlign PointeeAlign;
  Type *Ty = Arg.hasByRefAttr() ? Arg.getParamByRefType() : Arg.getType();

  // FIXME: Need to distinguish in memory alignment from pointer alignment.
  if (auto PtrTy = dyn_cast<PointerType>(Ty)) {
    if (PtrTy->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS)
      PointeeAlign = Arg.getParamAlign().valueOrOne();
  }

  // There's no distinction between byval aggregates and raw aggregates.
  Type *ArgTy;
  Align ArgAlign;
  std::tie(ArgTy, ArgAlign) = getArgumentTypeAlign(Arg, DL);

  emitKernelArg(DL, ArgTy, ArgAlign,
                getValueKind(ArgTy, TypeQual, BaseTypeName), Offset, Args,
                PointeeAlign, Name, TypeName, BaseTypeName, ActAccQual,
                AccQual, TypeQual);
}

void MetadataStreamerMsgPackV4::emitKernelArg(
    const DataLayout &DL, Type *Ty, Align Alignment, StringRef ValueKind,
    unsigned &Offset, msgpack::ArrayDocNode Args, MaybeAlign PointeeAlign,
    StringRef Name, StringRef TypeName, StringRef BaseTypeName,
    StringRef ActAccQual, StringRef AccQual, StringRef TypeQual) {
  auto Arg = Args.getDocument()->getMapNode();

  if (!Name.empty())
    Arg[".name"] = Arg.getDocument()->getNode(Name, /*Copy=*/true);
  if (!TypeName.empty())
    Arg[".type_name"] = Arg.getDocument()->getNode(TypeName, /*Copy=*/true);
  auto Size = DL.getTypeAllocSize(Ty);
  Arg[".size"] = Arg.getDocument()->getNode(Size);
  Offset = alignTo(Offset, Alignment);
  Arg[".offset"] = Arg.getDocument()->getNode(Offset);
  Offset += Size;
  Arg[".value_kind"] = Arg.getDocument()->getNode(ValueKind, /*Copy=*/true);
  if (PointeeAlign)
    Arg[".pointee_align"] = Arg.getDocument()->getNode(PointeeAlign->value());

  if (auto PtrTy = dyn_cast<PointerType>(Ty))
    if (auto Qualifier = getAddressSpaceQualifier(PtrTy->getAddressSpace()))
      // Limiting address space to emit only for a certain ValueKind.
      if (ValueKind == "global_buffer" || ValueKind == "dynamic_shared_pointer")
        Arg[".address_space"] = Arg.getDocument()->getNode(*Qualifier,
                                                           /*Copy=*/true);

  if (auto AQ = getAccessQualifier(AccQual))
    Arg[".access"] = Arg.getDocument()->getNode(*AQ, /*Copy=*/true);

  if (auto AAQ = getAccessQualifier(ActAccQual))
    Arg[".actual_access"] = Arg.getDocument()->getNode(*AAQ, /*Copy=*/true);

  SmallVector<StringRef, 1> SplitTypeQuals;
  TypeQual.split(SplitTypeQuals, " ", -1, false);
  for (StringRef Key : SplitTypeQuals) {
    if (Key == "const")
      Arg[".is_const"] = Arg.getDocument()->getNode(true);
    else if (Key == "restrict")
      Arg[".is_restrict"] = Arg.getDocument()->getNode(true);
    else if (Key == "volatile")
      Arg[".is_volatile"] = Arg.getDocument()->getNode(true);
    else if (Key == "pipe")
      Arg[".is_pipe"] = Arg.getDocument()->getNode(true);
  }

  Args.push_back(Arg);
}

void MetadataStreamerMsgPackV4::emitHiddenKernelArgs(
    const MachineFunction &MF, unsigned &Offset, msgpack::ArrayDocNode Args) {
  auto &Func = MF.getFunction();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  unsigned HiddenArgNumBytes = ST.getImplicitArgNumBytes(Func);
  if (!HiddenArgNumBytes)
    return;

  const Module *M = Func.getParent();
  auto &DL = M->getDataLayout();
  auto Int64Ty = Type::getInt64Ty(Func.getContext());

  Offset = alignTo(Offset, ST.getAlignmentForImplicitArgPtr());

  if (HiddenArgNumBytes >= 8)
    emitKernelArg(DL, Int64Ty, Align(8), "hidden_global_offset_x", Offset,
                  Args);
  if (HiddenArgNumBytes >= 16)
    emitKernelArg(DL, Int64Ty, Align(8), "hidden_global_offset_y", Offset,
                  Args);
  if (HiddenArgNumBytes >= 24)
    emitKernelArg(DL, Int64Ty, Align(8), "hidden_global_offset_z", Offset,
                  Args);

  auto Int8PtrTy =
      PointerType::get(Func.getContext(), AMDGPUAS::GLOBAL_ADDRESS);

  if (HiddenArgNumBytes >= 32) {
    // We forbid the use of features requiring hostcall when compiling OpenCL
    // before code object V5, which makes the mutual exclusion between the
    // "printf buffer" and "hostcall buffer" here sound.
    if (M->getNamedMetadata("llvm.printf.fmts"))
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_printf_buffer", Offset,
                    Args);
    else if (!Func.hasFnAttribute("amdgpu-no-hostcall-ptr"))
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_hostcall_buffer", Offset,
                    Args);
    else
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_none", Offset, Args);
  }

  // Emit "default queue" and "completion action" arguments if enqueue kernel is
  // used, otherwise emit dummy "none" arguments.
  if (HiddenArgNumBytes >= 40) {
    if (!Func.hasFnAttribute("amdgpu-no-default-queue")) {
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_default_queue", Offset,
                    Args);
    } else {
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_none", Offset, Args);
    }
  }

  if (HiddenArgNumBytes >= 48) {
    if (!Func.hasFnAttribute("amdgpu-no-completion-action")) {
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_completion_action", Offset,
                    Args);
    } else {
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_none", Offset, Args);
    }
  }

  // Emit the pointer argument for multi-grid object.
  if (HiddenArgNumBytes >= 56) {
    if (!Func.hasFnAttribute("amdgpu-no-multigrid-sync-arg")) {
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_multigrid_sync_arg", Offset,
                    Args);
    } else {
      emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_none", Offset, Args);
    }
  }
}

msgpack::MapDocNode
MetadataStreamerMsgPackV4::getHSAKernelProps(const MachineFunction &MF,
                                             const SIProgramInfo &ProgramInfo,
                                             unsigned CodeObjectVersion) const {
  const GCNSubtarget &STM = MF.getSubtarget<GCNSubtarget>();
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();
  const Function &F = MF.getFunction();

  auto Kern = HSAMetadataDoc->getMapNode();

  Align MaxKernArgAlign;
  Kern[".kernarg_segment_size"] = Kern.getDocument()->getNode(
      STM.getKernArgSegmentSize(F, MaxKernArgAlign));
  Kern[".group_segment_fixed_size"] =
      Kern.getDocument()->getNode(ProgramInfo.LDSSize);
  DelayedExprs->assignDocNode(Kern[".private_segment_fixed_size"],
                              msgpack::Type::UInt, ProgramInfo.ScratchSize);
  if (CodeObjectVersion >= AMDGPU::AMDHSA_COV5) {
    DelayedExprs->assignDocNode(Kern[".uses_dynamic_stack"],
                                msgpack::Type::Boolean,
                                ProgramInfo.DynamicCallStack);
  }

  if (CodeObjectVersion >= AMDGPU::AMDHSA_COV5 && STM.supportsWGP())
    Kern[".workgroup_processor_mode"] =
        Kern.getDocument()->getNode(ProgramInfo.WgpMode);

  // FIXME: The metadata treats the minimum as 16?
  Kern[".kernarg_segment_align"] =
      Kern.getDocument()->getNode(std::max(Align(4), MaxKernArgAlign).value());
  Kern[".wavefront_size"] =
      Kern.getDocument()->getNode(STM.getWavefrontSize());
  DelayedExprs->assignDocNode(Kern[".sgpr_count"], msgpack::Type::UInt,
                              ProgramInfo.NumSGPR);
  DelayedExprs->assignDocNode(Kern[".vgpr_count"], msgpack::Type::UInt,
                              ProgramInfo.NumVGPR);

  // Only add AGPR count to metadata for supported devices
  if (STM.hasMAIInsts()) {
    DelayedExprs->assignDocNode(Kern[".agpr_count"], msgpack::Type::UInt,
                                ProgramInfo.NumAccVGPR);
  }

  Kern[".max_flat_workgroup_size"] =
      Kern.getDocument()->getNode(MFI.getMaxFlatWorkGroupSize());
  unsigned NumWGX = MFI.getMaxNumWorkGroupsX();
  unsigned NumWGY = MFI.getMaxNumWorkGroupsY();
  unsigned NumWGZ = MFI.getMaxNumWorkGroupsZ();
  if (NumWGX != 0 && NumWGY != 0 && NumWGZ != 0) {
    Kern[".max_num_workgroups_x"] = Kern.getDocument()->getNode(NumWGX);
    Kern[".max_num_workgroups_y"] = Kern.getDocument()->getNode(NumWGY);
    Kern[".max_num_workgroups_z"] = Kern.getDocument()->getNode(NumWGZ);
  }
  Kern[".sgpr_spill_count"] =
      Kern.getDocument()->getNode(MFI.getNumSpilledSGPRs());
  Kern[".vgpr_spill_count"] =
      Kern.getDocument()->getNode(MFI.getNumSpilledVGPRs());

  return Kern;
}

bool MetadataStreamerMsgPackV4::emitTo(AMDGPUTargetStreamer &TargetStreamer) {
  DelayedExprs->resolveDelayedExpressions();
  return TargetStreamer.EmitHSAMetadata(*HSAMetadataDoc, true);
}

void MetadataStreamerMsgPackV4::begin(const Module &Mod,
                                      const IsaInfo::AMDGPUTargetID &TargetID) {
  emitVersion();
  emitTargetID(TargetID);
  emitPrintf(Mod);
  getRootMetadata("amdhsa.kernels") = HSAMetadataDoc->getArrayNode();
  DelayedExprs->clear();
}

void MetadataStreamerMsgPackV4::end() {
  DelayedExprs->resolveDelayedExpressions();
  std::string HSAMetadataString;
  raw_string_ostream StrOS(HSAMetadataString);
  HSAMetadataDoc->toYAML(StrOS);

  if (DumpHSAMetadata)
    dump(StrOS.str());
  if (VerifyHSAMetadata)
    verify(StrOS.str());
}

void MetadataStreamerMsgPackV4::emitKernel(const MachineFunction &MF,
                                           const SIProgramInfo &ProgramInfo) {
  auto &Func = MF.getFunction();
  if (Func.getCallingConv() != CallingConv::AMDGPU_KERNEL &&
      Func.getCallingConv() != CallingConv::SPIR_KERNEL)
    return;

  auto CodeObjectVersion =
      AMDGPU::getAMDHSACodeObjectVersion(*Func.getParent());
  auto Kern = getHSAKernelProps(MF, ProgramInfo, CodeObjectVersion);

  auto Kernels =
      getRootMetadata("amdhsa.kernels").getArray(/*Convert=*/true);

  {
    Kern[".name"] = Kern.getDocument()->getNode(Func.getName());
    Kern[".symbol"] = Kern.getDocument()->getNode(
        (Twine(Func.getName()) + Twine(".kd")).str(), /*Copy=*/true);
    emitKernelLanguage(Func, Kern);
    emitKernelAttrs(Func, Kern);
    emitKernelArgs(MF, Kern);
  }

  Kernels.push_back(Kern);
}

//===----------------------------------------------------------------------===//
// HSAMetadataStreamerV5
//===----------------------------------------------------------------------===//

void MetadataStreamerMsgPackV5::emitVersion() {
  auto Version = HSAMetadataDoc->getArrayNode();
  Version.push_back(Version.getDocument()->getNode(VersionMajorV5));
  Version.push_back(Version.getDocument()->getNode(VersionMinorV5));
  getRootMetadata("amdhsa.version") = Version;
}

void MetadataStreamerMsgPackV5::emitHiddenKernelArgs(
    const MachineFunction &MF, unsigned &Offset, msgpack::ArrayDocNode Args) {
  auto &Func = MF.getFunction();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  // No implicit kernel argument is used.
  if (ST.getImplicitArgNumBytes(Func) == 0)
    return;

  const Module *M = Func.getParent();
  auto &DL = M->getDataLayout();
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();

  auto Int64Ty = Type::getInt64Ty(Func.getContext());
  auto Int32Ty = Type::getInt32Ty(Func.getContext());
  auto Int16Ty = Type::getInt16Ty(Func.getContext());

  Offset = alignTo(Offset, ST.getAlignmentForImplicitArgPtr());
  emitKernelArg(DL, Int32Ty, Align(4), "hidden_block_count_x", Offset, Args);
  emitKernelArg(DL, Int32Ty, Align(4), "hidden_block_count_y", Offset, Args);
  emitKernelArg(DL, Int32Ty, Align(4), "hidden_block_count_z", Offset, Args);

  emitKernelArg(DL, Int16Ty, Align(2), "hidden_group_size_x", Offset, Args);
  emitKernelArg(DL, Int16Ty, Align(2), "hidden_group_size_y", Offset, Args);
  emitKernelArg(DL, Int16Ty, Align(2), "hidden_group_size_z", Offset, Args);

  emitKernelArg(DL, Int16Ty, Align(2), "hidden_remainder_x", Offset, Args);
  emitKernelArg(DL, Int16Ty, Align(2), "hidden_remainder_y", Offset, Args);
  emitKernelArg(DL, Int16Ty, Align(2), "hidden_remainder_z", Offset, Args);

  // Reserved for hidden_tool_correlation_id.
  Offset += 8;

  Offset += 8; // Reserved.

  emitKernelArg(DL, Int64Ty, Align(8), "hidden_global_offset_x", Offset, Args);
  emitKernelArg(DL, Int64Ty, Align(8), "hidden_global_offset_y", Offset, Args);
  emitKernelArg(DL, Int64Ty, Align(8), "hidden_global_offset_z", Offset, Args);

  emitKernelArg(DL, Int16Ty, Align(2), "hidden_grid_dims", Offset, Args);

  Offset += 6; // Reserved.
  auto Int8PtrTy =
      PointerType::get(Func.getContext(), AMDGPUAS::GLOBAL_ADDRESS);

  if (M->getNamedMetadata("llvm.printf.fmts")) {
    emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_printf_buffer", Offset,
                  Args);
  } else {
    Offset += 8; // Skipped.
  }

  if (!Func.hasFnAttribute("amdgpu-no-hostcall-ptr")) {
    emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_hostcall_buffer", Offset,
                  Args);
  } else {
    Offset += 8; // Skipped.
  }

  if (!Func.hasFnAttribute("amdgpu-no-multigrid-sync-arg")) {
    emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_multigrid_sync_arg", Offset,
                Args);
  } else {
    Offset += 8; // Skipped.
  }

  if (!Func.hasFnAttribute("amdgpu-no-heap-ptr"))
    emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_heap_v1", Offset, Args);
  else
    Offset += 8; // Skipped.

  if (!Func.hasFnAttribute("amdgpu-no-default-queue")) {
    emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_default_queue", Offset,
                  Args);
  } else {
    Offset += 8; // Skipped.
  }

  if (!Func.hasFnAttribute("amdgpu-no-completion-action")) {
    emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_completion_action", Offset,
                  Args);
  } else {
    Offset += 8; // Skipped.
  }

  // Emit argument for hidden dynamic lds size
  if (MFI.isDynamicLDSUsed()) {
    emitKernelArg(DL, Int32Ty, Align(4), "hidden_dynamic_lds_size", Offset,
                  Args);
  } else {
    Offset += 4; // skipped
  }

  Offset += 68; // Reserved.

  // hidden_private_base and hidden_shared_base are only when the subtarget has
  // ApertureRegs.
  if (!ST.hasApertureRegs()) {
    emitKernelArg(DL, Int32Ty, Align(4), "hidden_private_base", Offset, Args);
    emitKernelArg(DL, Int32Ty, Align(4), "hidden_shared_base", Offset, Args);
  } else {
    Offset += 8; // Skipped.
  }

  if (MFI.getUserSGPRInfo().hasQueuePtr())
    emitKernelArg(DL, Int8PtrTy, Align(8), "hidden_queue_ptr", Offset, Args);
}

void MetadataStreamerMsgPackV5::emitKernelAttrs(const Function &Func,
                                                msgpack::MapDocNode Kern) {
  MetadataStreamerMsgPackV4::emitKernelAttrs(Func, Kern);

  if (Func.getFnAttribute("uniform-work-group-size").getValueAsBool())
    Kern[".uniform_work_group_size"] = Kern.getDocument()->getNode(1);
}

//===----------------------------------------------------------------------===//
// HSAMetadataStreamerV6
//===----------------------------------------------------------------------===//

void MetadataStreamerMsgPackV6::emitVersion() {
  auto Version = HSAMetadataDoc->getArrayNode();
  Version.push_back(Version.getDocument()->getNode(VersionMajorV6));
  Version.push_back(Version.getDocument()->getNode(VersionMinorV6));
  getRootMetadata("amdhsa.version") = Version;
}

} // end namespace AMDGPU::HSAMD
} // end namespace llvm
