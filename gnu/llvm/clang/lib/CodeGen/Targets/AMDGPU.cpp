//===- AMDGPU.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "clang/Basic/TargetOptions.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// AMDGPU ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class AMDGPUABIInfo final : public DefaultABIInfo {
private:
  static const unsigned MaxNumRegsForArgsRet = 16;

  unsigned numRegsForType(QualType Ty) const;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Base,
                                         uint64_t Members) const override;

  // Coerce HIP scalar pointer arguments from generic pointers to global ones.
  llvm::Type *coerceKernelArgumentType(llvm::Type *Ty, unsigned FromAS,
                                       unsigned ToAS) const {
    // Single value types.
    auto *PtrTy = llvm::dyn_cast<llvm::PointerType>(Ty);
    if (PtrTy && PtrTy->getAddressSpace() == FromAS)
      return llvm::PointerType::get(Ty->getContext(), ToAS);
    return Ty;
  }

public:
  explicit AMDGPUABIInfo(CodeGen::CodeGenTypes &CGT) :
    DefaultABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyKernelArgumentType(QualType Ty) const;
  ABIArgInfo classifyArgumentType(QualType Ty, bool Variadic,
                                  unsigned &NumRegsLeft) const;

  void computeInfo(CGFunctionInfo &FI) const override;
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

bool AMDGPUABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  return true;
}

bool AMDGPUABIInfo::isHomogeneousAggregateSmallEnough(
  const Type *Base, uint64_t Members) const {
  uint32_t NumRegs = (getContext().getTypeSize(Base) + 31) / 32;

  // Homogeneous Aggregates may occupy at most 16 registers.
  return Members * NumRegs <= MaxNumRegsForArgsRet;
}

/// Estimate number of registers the type will use when passed in registers.
unsigned AMDGPUABIInfo::numRegsForType(QualType Ty) const {
  unsigned NumRegs = 0;

  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // Compute from the number of elements. The reported size is based on the
    // in-memory size, which includes the padding 4th element for 3-vectors.
    QualType EltTy = VT->getElementType();
    unsigned EltSize = getContext().getTypeSize(EltTy);

    // 16-bit element vectors should be passed as packed.
    if (EltSize == 16)
      return (VT->getNumElements() + 1) / 2;

    unsigned EltNumRegs = (EltSize + 31) / 32;
    return EltNumRegs * VT->getNumElements();
  }

  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    assert(!RD->hasFlexibleArrayMember());

    for (const FieldDecl *Field : RD->fields()) {
      QualType FieldTy = Field->getType();
      NumRegs += numRegsForType(FieldTy);
    }

    return NumRegs;
  }

  return (getContext().getTypeSize(Ty) + 31) / 32;
}

void AMDGPUABIInfo::computeInfo(CGFunctionInfo &FI) const {
  llvm::CallingConv::ID CC = FI.getCallingConvention();

  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

  unsigned ArgumentIndex = 0;
  const unsigned numFixedArguments = FI.getNumRequiredArgs();

  unsigned NumRegsLeft = MaxNumRegsForArgsRet;
  for (auto &Arg : FI.arguments()) {
    if (CC == llvm::CallingConv::AMDGPU_KERNEL) {
      Arg.info = classifyKernelArgumentType(Arg.type);
    } else {
      bool FixedArgument = ArgumentIndex++ < numFixedArguments;
      Arg.info = classifyArgumentType(Arg.type, !FixedArgument, NumRegsLeft);
    }
  }
}

RValue AMDGPUABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty, AggValueSlot Slot) const {
  const bool IsIndirect = false;
  const bool AllowHigherAlign = false;
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect,
                          getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(4), AllowHigherAlign, Slot);
}

ABIArgInfo AMDGPUABIInfo::classifyReturnType(QualType RetTy) const {
  if (isAggregateTypeForABI(RetTy)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // returned by value.
    if (!getRecordArgABI(RetTy, getCXXABI())) {
      // Ignore empty structs/unions.
      if (isEmptyRecord(getContext(), RetTy, true))
        return ABIArgInfo::getIgnore();

      // Lower single-element structs to just return a regular value.
      if (const Type *SeltTy = isSingleElementStruct(RetTy, getContext()))
        return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));

      if (const RecordType *RT = RetTy->getAs<RecordType>()) {
        const RecordDecl *RD = RT->getDecl();
        if (RD->hasFlexibleArrayMember())
          return DefaultABIInfo::classifyReturnType(RetTy);
      }

      // Pack aggregates <= 4 bytes into single VGPR or pair.
      uint64_t Size = getContext().getTypeSize(RetTy);
      if (Size <= 16)
        return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));

      if (Size <= 32)
        return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));

      if (Size <= 64) {
        llvm::Type *I32Ty = llvm::Type::getInt32Ty(getVMContext());
        return ABIArgInfo::getDirect(llvm::ArrayType::get(I32Ty, 2));
      }

      if (numRegsForType(RetTy) <= MaxNumRegsForArgsRet)
        return ABIArgInfo::getDirect();
    }
  }

  // Otherwise just do the default thing.
  return DefaultABIInfo::classifyReturnType(RetTy);
}

/// For kernels all parameters are really passed in a special buffer. It doesn't
/// make sense to pass anything byval, so everything must be direct.
ABIArgInfo AMDGPUABIInfo::classifyKernelArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // TODO: Can we omit empty structs?

  if (const Type *SeltTy = isSingleElementStruct(Ty, getContext()))
    Ty = QualType(SeltTy, 0);

  llvm::Type *OrigLTy = CGT.ConvertType(Ty);
  llvm::Type *LTy = OrigLTy;
  if (getContext().getLangOpts().HIP) {
    LTy = coerceKernelArgumentType(
        OrigLTy, /*FromAS=*/getContext().getTargetAddressSpace(LangAS::Default),
        /*ToAS=*/getContext().getTargetAddressSpace(LangAS::cuda_device));
  }

  // FIXME: Should also use this for OpenCL, but it requires addressing the
  // problem of kernels being called.
  //
  // FIXME: This doesn't apply the optimization of coercing pointers in structs
  // to global address space when using byref. This would require implementing a
  // new kind of coercion of the in-memory type when for indirect arguments.
  if (!getContext().getLangOpts().OpenCL && LTy == OrigLTy &&
      isAggregateTypeForABI(Ty)) {
    return ABIArgInfo::getIndirectAliased(
        getContext().getTypeAlignInChars(Ty),
        getContext().getTargetAddressSpace(LangAS::opencl_constant),
        false /*Realign*/, nullptr /*Padding*/);
  }

  // If we set CanBeFlattened to true, CodeGen will expand the struct to its
  // individual elements, which confuses the Clover OpenCL backend; therefore we
  // have to set it to false here. Other args of getDirect() are just defaults.
  return ABIArgInfo::getDirect(LTy, 0, nullptr, false);
}

ABIArgInfo AMDGPUABIInfo::classifyArgumentType(QualType Ty, bool Variadic,
                                               unsigned &NumRegsLeft) const {
  assert(NumRegsLeft <= MaxNumRegsForArgsRet && "register estimate underflow");

  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (Variadic) {
    return ABIArgInfo::getDirect(/*T=*/nullptr,
                                 /*Offset=*/0,
                                 /*Padding=*/nullptr,
                                 /*CanBeFlattened=*/false,
                                 /*Align=*/0);
  }

  if (isAggregateTypeForABI(Ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (auto RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    // Lower single-element structs to just pass a regular value. TODO: We
    // could do reasonable-size multiple-element structs too, using getExpand(),
    // though watch out for things like bitfields.
    if (const Type *SeltTy = isSingleElementStruct(Ty, getContext()))
      return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));

    if (const RecordType *RT = Ty->getAs<RecordType>()) {
      const RecordDecl *RD = RT->getDecl();
      if (RD->hasFlexibleArrayMember())
        return DefaultABIInfo::classifyArgumentType(Ty);
    }

    // Pack aggregates <= 8 bytes into single VGPR or pair.
    uint64_t Size = getContext().getTypeSize(Ty);
    if (Size <= 64) {
      unsigned NumRegs = (Size + 31) / 32;
      NumRegsLeft -= std::min(NumRegsLeft, NumRegs);

      if (Size <= 16)
        return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));

      if (Size <= 32)
        return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));

      // XXX: Should this be i64 instead, and should the limit increase?
      llvm::Type *I32Ty = llvm::Type::getInt32Ty(getVMContext());
      return ABIArgInfo::getDirect(llvm::ArrayType::get(I32Ty, 2));
    }

    if (NumRegsLeft > 0) {
      unsigned NumRegs = numRegsForType(Ty);
      if (NumRegsLeft >= NumRegs) {
        NumRegsLeft -= NumRegs;
        return ABIArgInfo::getDirect();
      }
    }

    // Use pass-by-reference in stead of pass-by-value for struct arguments in
    // function ABI.
    return ABIArgInfo::getIndirectAliased(
        getContext().getTypeAlignInChars(Ty),
        getContext().getTargetAddressSpace(LangAS::opencl_private));
  }

  // Otherwise just do the default thing.
  ABIArgInfo ArgInfo = DefaultABIInfo::classifyArgumentType(Ty);
  if (!ArgInfo.isIndirect()) {
    unsigned NumRegs = numRegsForType(Ty);
    NumRegsLeft -= std::min(NumRegs, NumRegsLeft);
  }

  return ArgInfo;
}

class AMDGPUTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  AMDGPUTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<AMDGPUABIInfo>(CGT)) {}

  void setFunctionDeclAttributes(const FunctionDecl *FD, llvm::Function *F,
                                 CodeGenModule &CGM) const;

  void emitTargetGlobals(CodeGen::CodeGenModule &CGM) const override;

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
  unsigned getOpenCLKernelCallingConv() const override;

  llvm::Constant *getNullPointer(const CodeGen::CodeGenModule &CGM,
      llvm::PointerType *T, QualType QT) const override;

  LangAS getASTAllocaAddressSpace() const override {
    return getLangASFromTargetAS(
        getABIInfo().getDataLayout().getAllocaAddrSpace());
  }
  LangAS getGlobalVarAddressSpace(CodeGenModule &CGM,
                                  const VarDecl *D) const override;
  llvm::SyncScope::ID getLLVMSyncScopeID(const LangOptions &LangOpts,
                                         SyncScope Scope,
                                         llvm::AtomicOrdering Ordering,
                                         llvm::LLVMContext &Ctx) const override;
  llvm::Value *createEnqueuedBlockKernel(CodeGenFunction &CGF,
                                         llvm::Function *BlockInvokeFunc,
                                         llvm::Type *BlockTy) const override;
  bool shouldEmitStaticExternCAliases() const override;
  bool shouldEmitDWARFBitFieldSeparators() const override;
  void setCUDAKernelCallingConvention(const FunctionType *&FT) const override;
};
}

static bool requiresAMDGPUProtectedVisibility(const Decl *D,
                                              llvm::GlobalValue *GV) {
  if (GV->getVisibility() != llvm::GlobalValue::HiddenVisibility)
    return false;

  return !D->hasAttr<OMPDeclareTargetDeclAttr>() &&
         (D->hasAttr<OpenCLKernelAttr>() ||
          (isa<FunctionDecl>(D) && D->hasAttr<CUDAGlobalAttr>()) ||
          (isa<VarDecl>(D) &&
           (D->hasAttr<CUDADeviceAttr>() || D->hasAttr<CUDAConstantAttr>() ||
            cast<VarDecl>(D)->getType()->isCUDADeviceBuiltinSurfaceType() ||
            cast<VarDecl>(D)->getType()->isCUDADeviceBuiltinTextureType())));
}

void AMDGPUTargetCodeGenInfo::setFunctionDeclAttributes(
    const FunctionDecl *FD, llvm::Function *F, CodeGenModule &M) const {
  const auto *ReqdWGS =
      M.getLangOpts().OpenCL ? FD->getAttr<ReqdWorkGroupSizeAttr>() : nullptr;
  const bool IsOpenCLKernel =
      M.getLangOpts().OpenCL && FD->hasAttr<OpenCLKernelAttr>();
  const bool IsHIPKernel = M.getLangOpts().HIP && FD->hasAttr<CUDAGlobalAttr>();

  const auto *FlatWGS = FD->getAttr<AMDGPUFlatWorkGroupSizeAttr>();
  if (ReqdWGS || FlatWGS) {
    M.handleAMDGPUFlatWorkGroupSizeAttr(F, FlatWGS, ReqdWGS);
  } else if (IsOpenCLKernel || IsHIPKernel) {
    // By default, restrict the maximum size to a value specified by
    // --gpu-max-threads-per-block=n or its default value for HIP.
    const unsigned OpenCLDefaultMaxWorkGroupSize = 256;
    const unsigned DefaultMaxWorkGroupSize =
        IsOpenCLKernel ? OpenCLDefaultMaxWorkGroupSize
                       : M.getLangOpts().GPUMaxThreadsPerBlock;
    std::string AttrVal =
        std::string("1,") + llvm::utostr(DefaultMaxWorkGroupSize);
    F->addFnAttr("amdgpu-flat-work-group-size", AttrVal);
  }

  if (const auto *Attr = FD->getAttr<AMDGPUWavesPerEUAttr>())
    M.handleAMDGPUWavesPerEUAttr(F, Attr);

  if (const auto *Attr = FD->getAttr<AMDGPUNumSGPRAttr>()) {
    unsigned NumSGPR = Attr->getNumSGPR();

    if (NumSGPR != 0)
      F->addFnAttr("amdgpu-num-sgpr", llvm::utostr(NumSGPR));
  }

  if (const auto *Attr = FD->getAttr<AMDGPUNumVGPRAttr>()) {
    uint32_t NumVGPR = Attr->getNumVGPR();

    if (NumVGPR != 0)
      F->addFnAttr("amdgpu-num-vgpr", llvm::utostr(NumVGPR));
  }

  if (const auto *Attr = FD->getAttr<AMDGPUMaxNumWorkGroupsAttr>()) {
    uint32_t X = Attr->getMaxNumWorkGroupsX()
                     ->EvaluateKnownConstInt(M.getContext())
                     .getExtValue();
    // Y and Z dimensions default to 1 if not specified
    uint32_t Y = Attr->getMaxNumWorkGroupsY()
                     ? Attr->getMaxNumWorkGroupsY()
                           ->EvaluateKnownConstInt(M.getContext())
                           .getExtValue()
                     : 1;
    uint32_t Z = Attr->getMaxNumWorkGroupsZ()
                     ? Attr->getMaxNumWorkGroupsZ()
                           ->EvaluateKnownConstInt(M.getContext())
                           .getExtValue()
                     : 1;

    llvm::SmallString<32> AttrVal;
    llvm::raw_svector_ostream OS(AttrVal);
    OS << X << ',' << Y << ',' << Z;

    F->addFnAttr("amdgpu-max-num-workgroups", AttrVal.str());
  }
}

/// Emits control constants used to change per-architecture behaviour in the
/// AMDGPU ROCm device libraries.
void AMDGPUTargetCodeGenInfo::emitTargetGlobals(
    CodeGen::CodeGenModule &CGM) const {
  StringRef Name = "__oclc_ABI_version";
  llvm::GlobalVariable *OriginalGV = CGM.getModule().getNamedGlobal(Name);
  if (OriginalGV && !llvm::GlobalVariable::isExternalLinkage(OriginalGV->getLinkage()))
    return;

  if (CGM.getTarget().getTargetOpts().CodeObjectVersion ==
      llvm::CodeObjectVersionKind::COV_None)
    return;

  auto *Type = llvm::IntegerType::getIntNTy(CGM.getModule().getContext(), 32);
  llvm::Constant *COV = llvm::ConstantInt::get(
      Type, CGM.getTarget().getTargetOpts().CodeObjectVersion);

  // It needs to be constant weak_odr without externally_initialized so that
  // the load instuction can be eliminated by the IPSCCP.
  auto *GV = new llvm::GlobalVariable(
      CGM.getModule(), Type, true, llvm::GlobalValue::WeakODRLinkage, COV, Name,
      nullptr, llvm::GlobalValue::ThreadLocalMode::NotThreadLocal,
      CGM.getContext().getTargetAddressSpace(LangAS::opencl_constant));
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Local);
  GV->setVisibility(llvm::GlobalValue::VisibilityTypes::HiddenVisibility);

  // Replace any external references to this variable with the new global.
  if (OriginalGV) {
    OriginalGV->replaceAllUsesWith(GV);
    GV->takeName(OriginalGV);
    OriginalGV->eraseFromParent();
  }
}

void AMDGPUTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (requiresAMDGPUProtectedVisibility(D, GV)) {
    GV->setVisibility(llvm::GlobalValue::ProtectedVisibility);
    GV->setDSOLocal(true);
  }

  if (GV->isDeclaration())
    return;

  llvm::Function *F = dyn_cast<llvm::Function>(GV);
  if (!F)
    return;

  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (FD)
    setFunctionDeclAttributes(FD, F, M);

  if (M.getContext().getTargetInfo().allowAMDGPUUnsafeFPAtomics())
    F->addFnAttr("amdgpu-unsafe-fp-atomics", "true");

  if (!getABIInfo().getCodeGenOpts().EmitIEEENaNCompliantInsts)
    F->addFnAttr("amdgpu-ieee", "false");
}

unsigned AMDGPUTargetCodeGenInfo::getOpenCLKernelCallingConv() const {
  return llvm::CallingConv::AMDGPU_KERNEL;
}

// Currently LLVM assumes null pointers always have value 0,
// which results in incorrectly transformed IR. Therefore, instead of
// emitting null pointers in private and local address spaces, a null
// pointer in generic address space is emitted which is casted to a
// pointer in local or private address space.
llvm::Constant *AMDGPUTargetCodeGenInfo::getNullPointer(
    const CodeGen::CodeGenModule &CGM, llvm::PointerType *PT,
    QualType QT) const {
  if (CGM.getContext().getTargetNullPointerValue(QT) == 0)
    return llvm::ConstantPointerNull::get(PT);

  auto &Ctx = CGM.getContext();
  auto NPT = llvm::PointerType::get(
      PT->getContext(), Ctx.getTargetAddressSpace(LangAS::opencl_generic));
  return llvm::ConstantExpr::getAddrSpaceCast(
      llvm::ConstantPointerNull::get(NPT), PT);
}

LangAS
AMDGPUTargetCodeGenInfo::getGlobalVarAddressSpace(CodeGenModule &CGM,
                                                  const VarDecl *D) const {
  assert(!CGM.getLangOpts().OpenCL &&
         !(CGM.getLangOpts().CUDA && CGM.getLangOpts().CUDAIsDevice) &&
         "Address space agnostic languages only");
  LangAS DefaultGlobalAS = getLangASFromTargetAS(
      CGM.getContext().getTargetAddressSpace(LangAS::opencl_global));
  if (!D)
    return DefaultGlobalAS;

  LangAS AddrSpace = D->getType().getAddressSpace();
  if (AddrSpace != LangAS::Default)
    return AddrSpace;

  // Only promote to address space 4 if VarDecl has constant initialization.
  if (D->getType().isConstantStorage(CGM.getContext(), false, false) &&
      D->hasConstantInitialization()) {
    if (auto ConstAS = CGM.getTarget().getConstantAddressSpace())
      return *ConstAS;
  }
  return DefaultGlobalAS;
}

llvm::SyncScope::ID
AMDGPUTargetCodeGenInfo::getLLVMSyncScopeID(const LangOptions &LangOpts,
                                            SyncScope Scope,
                                            llvm::AtomicOrdering Ordering,
                                            llvm::LLVMContext &Ctx) const {
  std::string Name;
  switch (Scope) {
  case SyncScope::HIPSingleThread:
  case SyncScope::SingleScope:
    Name = "singlethread";
    break;
  case SyncScope::HIPWavefront:
  case SyncScope::OpenCLSubGroup:
  case SyncScope::WavefrontScope:
    Name = "wavefront";
    break;
  case SyncScope::HIPWorkgroup:
  case SyncScope::OpenCLWorkGroup:
  case SyncScope::WorkgroupScope:
    Name = "workgroup";
    break;
  case SyncScope::HIPAgent:
  case SyncScope::OpenCLDevice:
  case SyncScope::DeviceScope:
    Name = "agent";
    break;
  case SyncScope::SystemScope:
  case SyncScope::HIPSystem:
  case SyncScope::OpenCLAllSVMDevices:
    Name = "";
    break;
  }

  if (Ordering != llvm::AtomicOrdering::SequentiallyConsistent) {
    if (!Name.empty())
      Name = Twine(Twine(Name) + Twine("-")).str();

    Name = Twine(Twine(Name) + Twine("one-as")).str();
  }

  return Ctx.getOrInsertSyncScopeID(Name);
}

bool AMDGPUTargetCodeGenInfo::shouldEmitStaticExternCAliases() const {
  return false;
}

bool AMDGPUTargetCodeGenInfo::shouldEmitDWARFBitFieldSeparators() const {
  return true;
}

void AMDGPUTargetCodeGenInfo::setCUDAKernelCallingConvention(
    const FunctionType *&FT) const {
  FT = getABIInfo().getContext().adjustFunctionType(
      FT, FT->getExtInfo().withCallingConv(CC_OpenCLKernel));
}

/// Create an OpenCL kernel for an enqueued block.
///
/// The type of the first argument (the block literal) is the struct type
/// of the block literal instead of a pointer type. The first argument
/// (block literal) is passed directly by value to the kernel. The kernel
/// allocates the same type of struct on stack and stores the block literal
/// to it and passes its pointer to the block invoke function. The kernel
/// has "enqueued-block" function attribute and kernel argument metadata.
llvm::Value *AMDGPUTargetCodeGenInfo::createEnqueuedBlockKernel(
    CodeGenFunction &CGF, llvm::Function *Invoke, llvm::Type *BlockTy) const {
  auto &Builder = CGF.Builder;
  auto &C = CGF.getLLVMContext();

  auto *InvokeFT = Invoke->getFunctionType();
  llvm::SmallVector<llvm::Type *, 2> ArgTys;
  llvm::SmallVector<llvm::Metadata *, 8> AddressQuals;
  llvm::SmallVector<llvm::Metadata *, 8> AccessQuals;
  llvm::SmallVector<llvm::Metadata *, 8> ArgTypeNames;
  llvm::SmallVector<llvm::Metadata *, 8> ArgBaseTypeNames;
  llvm::SmallVector<llvm::Metadata *, 8> ArgTypeQuals;
  llvm::SmallVector<llvm::Metadata *, 8> ArgNames;

  ArgTys.push_back(BlockTy);
  ArgTypeNames.push_back(llvm::MDString::get(C, "__block_literal"));
  AddressQuals.push_back(llvm::ConstantAsMetadata::get(Builder.getInt32(0)));
  ArgBaseTypeNames.push_back(llvm::MDString::get(C, "__block_literal"));
  ArgTypeQuals.push_back(llvm::MDString::get(C, ""));
  AccessQuals.push_back(llvm::MDString::get(C, "none"));
  ArgNames.push_back(llvm::MDString::get(C, "block_literal"));
  for (unsigned I = 1, E = InvokeFT->getNumParams(); I < E; ++I) {
    ArgTys.push_back(InvokeFT->getParamType(I));
    ArgTypeNames.push_back(llvm::MDString::get(C, "void*"));
    AddressQuals.push_back(llvm::ConstantAsMetadata::get(Builder.getInt32(3)));
    AccessQuals.push_back(llvm::MDString::get(C, "none"));
    ArgBaseTypeNames.push_back(llvm::MDString::get(C, "void*"));
    ArgTypeQuals.push_back(llvm::MDString::get(C, ""));
    ArgNames.push_back(
        llvm::MDString::get(C, (Twine("local_arg") + Twine(I)).str()));
  }
  std::string Name = Invoke->getName().str() + "_kernel";
  auto *FT = llvm::FunctionType::get(llvm::Type::getVoidTy(C), ArgTys, false);
  auto *F = llvm::Function::Create(FT, llvm::GlobalValue::InternalLinkage, Name,
                                   &CGF.CGM.getModule());
  F->setCallingConv(llvm::CallingConv::AMDGPU_KERNEL);

  llvm::AttrBuilder KernelAttrs(C);
  // FIXME: The invoke isn't applying the right attributes either
  // FIXME: This is missing setTargetAttributes
  CGF.CGM.addDefaultFunctionDefinitionAttributes(KernelAttrs);
  KernelAttrs.addAttribute("enqueued-block");
  F->addFnAttrs(KernelAttrs);

  auto IP = CGF.Builder.saveIP();
  auto *BB = llvm::BasicBlock::Create(C, "entry", F);
  Builder.SetInsertPoint(BB);
  const auto BlockAlign = CGF.CGM.getDataLayout().getPrefTypeAlign(BlockTy);
  auto *BlockPtr = Builder.CreateAlloca(BlockTy, nullptr);
  BlockPtr->setAlignment(BlockAlign);
  Builder.CreateAlignedStore(F->arg_begin(), BlockPtr, BlockAlign);
  auto *Cast = Builder.CreatePointerCast(BlockPtr, InvokeFT->getParamType(0));
  llvm::SmallVector<llvm::Value *, 2> Args;
  Args.push_back(Cast);
  for (llvm::Argument &A : llvm::drop_begin(F->args()))
    Args.push_back(&A);
  llvm::CallInst *call = Builder.CreateCall(Invoke, Args);
  call->setCallingConv(Invoke->getCallingConv());
  Builder.CreateRetVoid();
  Builder.restoreIP(IP);

  F->setMetadata("kernel_arg_addr_space", llvm::MDNode::get(C, AddressQuals));
  F->setMetadata("kernel_arg_access_qual", llvm::MDNode::get(C, AccessQuals));
  F->setMetadata("kernel_arg_type", llvm::MDNode::get(C, ArgTypeNames));
  F->setMetadata("kernel_arg_base_type",
                 llvm::MDNode::get(C, ArgBaseTypeNames));
  F->setMetadata("kernel_arg_type_qual", llvm::MDNode::get(C, ArgTypeQuals));
  if (CGF.CGM.getCodeGenOpts().EmitOpenCLArgMetadata)
    F->setMetadata("kernel_arg_name", llvm::MDNode::get(C, ArgNames));

  return F;
}

void CodeGenModule::handleAMDGPUFlatWorkGroupSizeAttr(
    llvm::Function *F, const AMDGPUFlatWorkGroupSizeAttr *FlatWGS,
    const ReqdWorkGroupSizeAttr *ReqdWGS, int32_t *MinThreadsVal,
    int32_t *MaxThreadsVal) {
  unsigned Min = 0;
  unsigned Max = 0;
  if (FlatWGS) {
    Min = FlatWGS->getMin()->EvaluateKnownConstInt(getContext()).getExtValue();
    Max = FlatWGS->getMax()->EvaluateKnownConstInt(getContext()).getExtValue();
  }
  if (ReqdWGS && Min == 0 && Max == 0)
    Min = Max = ReqdWGS->getXDim() * ReqdWGS->getYDim() * ReqdWGS->getZDim();

  if (Min != 0) {
    assert(Min <= Max && "Min must be less than or equal Max");

    if (MinThreadsVal)
      *MinThreadsVal = Min;
    if (MaxThreadsVal)
      *MaxThreadsVal = Max;
    std::string AttrVal = llvm::utostr(Min) + "," + llvm::utostr(Max);
    if (F)
      F->addFnAttr("amdgpu-flat-work-group-size", AttrVal);
  } else
    assert(Max == 0 && "Max must be zero");
}

void CodeGenModule::handleAMDGPUWavesPerEUAttr(
    llvm::Function *F, const AMDGPUWavesPerEUAttr *Attr) {
  unsigned Min =
      Attr->getMin()->EvaluateKnownConstInt(getContext()).getExtValue();
  unsigned Max =
      Attr->getMax()
          ? Attr->getMax()->EvaluateKnownConstInt(getContext()).getExtValue()
          : 0;

  if (Min != 0) {
    assert((Max == 0 || Min <= Max) && "Min must be less than or equal Max");

    std::string AttrVal = llvm::utostr(Min);
    if (Max != 0)
      AttrVal = AttrVal + "," + llvm::utostr(Max);
    F->addFnAttr("amdgpu-waves-per-eu", AttrVal);
  } else
    assert(Max == 0 && "Max must be zero");
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createAMDGPUTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<AMDGPUTargetCodeGenInfo>(CGM.getTypes());
}
