//===------ SemaRISCV.cpp ------- RISC-V target-specific routines ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to RISC-V.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaRISCV.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Attr.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/RISCVIntrinsicManager.h"
#include "clang/Sema/Sema.h"
#include "clang/Support/RISCVVIntrinsicUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/TargetParser/RISCVTargetParser.h"
#include <optional>
#include <string>
#include <vector>

using namespace llvm;
using namespace clang;
using namespace clang::RISCV;

using IntrinsicKind = sema::RISCVIntrinsicManager::IntrinsicKind;

namespace {

// Function definition of a RVV intrinsic.
struct RVVIntrinsicDef {
  /// Mapping to which clang built-in function, e.g. __builtin_rvv_vadd.
  std::string BuiltinName;

  /// Function signature, first element is return type.
  RVVTypes Signature;
};

struct RVVOverloadIntrinsicDef {
  // Indexes of RISCVIntrinsicManagerImpl::IntrinsicList.
  SmallVector<uint16_t, 8> Indexes;
};

} // namespace

static const PrototypeDescriptor RVVSignatureTable[] = {
#define DECL_SIGNATURE_TABLE
#include "clang/Basic/riscv_vector_builtin_sema.inc"
#undef DECL_SIGNATURE_TABLE
};

static const PrototypeDescriptor RVSiFiveVectorSignatureTable[] = {
#define DECL_SIGNATURE_TABLE
#include "clang/Basic/riscv_sifive_vector_builtin_sema.inc"
#undef DECL_SIGNATURE_TABLE
};

static const RVVIntrinsicRecord RVVIntrinsicRecords[] = {
#define DECL_INTRINSIC_RECORDS
#include "clang/Basic/riscv_vector_builtin_sema.inc"
#undef DECL_INTRINSIC_RECORDS
};

static const RVVIntrinsicRecord RVSiFiveVectorIntrinsicRecords[] = {
#define DECL_INTRINSIC_RECORDS
#include "clang/Basic/riscv_sifive_vector_builtin_sema.inc"
#undef DECL_INTRINSIC_RECORDS
};

// Get subsequence of signature table.
static ArrayRef<PrototypeDescriptor>
ProtoSeq2ArrayRef(IntrinsicKind K, uint16_t Index, uint8_t Length) {
  switch (K) {
  case IntrinsicKind::RVV:
    return ArrayRef(&RVVSignatureTable[Index], Length);
  case IntrinsicKind::SIFIVE_VECTOR:
    return ArrayRef(&RVSiFiveVectorSignatureTable[Index], Length);
  }
  llvm_unreachable("Unhandled IntrinsicKind");
}

static QualType RVVType2Qual(ASTContext &Context, const RVVType *Type) {
  QualType QT;
  switch (Type->getScalarType()) {
  case ScalarTypeKind::Void:
    QT = Context.VoidTy;
    break;
  case ScalarTypeKind::Size_t:
    QT = Context.getSizeType();
    break;
  case ScalarTypeKind::Ptrdiff_t:
    QT = Context.getPointerDiffType();
    break;
  case ScalarTypeKind::UnsignedLong:
    QT = Context.UnsignedLongTy;
    break;
  case ScalarTypeKind::SignedLong:
    QT = Context.LongTy;
    break;
  case ScalarTypeKind::Boolean:
    QT = Context.BoolTy;
    break;
  case ScalarTypeKind::SignedInteger:
    QT = Context.getIntTypeForBitwidth(Type->getElementBitwidth(), true);
    break;
  case ScalarTypeKind::UnsignedInteger:
    QT = Context.getIntTypeForBitwidth(Type->getElementBitwidth(), false);
    break;
  case ScalarTypeKind::BFloat:
    QT = Context.BFloat16Ty;
    break;
  case ScalarTypeKind::Float:
    switch (Type->getElementBitwidth()) {
    case 64:
      QT = Context.DoubleTy;
      break;
    case 32:
      QT = Context.FloatTy;
      break;
    case 16:
      QT = Context.Float16Ty;
      break;
    default:
      llvm_unreachable("Unsupported floating point width.");
    }
    break;
  case Invalid:
  case Undefined:
    llvm_unreachable("Unhandled type.");
  }
  if (Type->isVector()) {
    if (Type->isTuple())
      QT = Context.getScalableVectorType(QT, *Type->getScale(), Type->getNF());
    else
      QT = Context.getScalableVectorType(QT, *Type->getScale());
  }

  if (Type->isConstant())
    QT = Context.getConstType(QT);

  // Transform the type to a pointer as the last step, if necessary.
  if (Type->isPointer())
    QT = Context.getPointerType(QT);

  return QT;
}

namespace {
class RISCVIntrinsicManagerImpl : public sema::RISCVIntrinsicManager {
private:
  Sema &S;
  ASTContext &Context;
  RVVTypeCache TypeCache;
  bool ConstructedRISCVVBuiltins;
  bool ConstructedRISCVSiFiveVectorBuiltins;

  // List of all RVV intrinsic.
  std::vector<RVVIntrinsicDef> IntrinsicList;
  // Mapping function name to index of IntrinsicList.
  StringMap<uint16_t> Intrinsics;
  // Mapping function name to RVVOverloadIntrinsicDef.
  StringMap<RVVOverloadIntrinsicDef> OverloadIntrinsics;

  // Create RVVIntrinsicDef.
  void InitRVVIntrinsic(const RVVIntrinsicRecord &Record, StringRef SuffixStr,
                        StringRef OverloadedSuffixStr, bool IsMask,
                        RVVTypes &Types, bool HasPolicy, Policy PolicyAttrs);

  // Create FunctionDecl for a vector intrinsic.
  void CreateRVVIntrinsicDecl(LookupResult &LR, IdentifierInfo *II,
                              Preprocessor &PP, uint32_t Index,
                              bool IsOverload);

  void ConstructRVVIntrinsics(ArrayRef<RVVIntrinsicRecord> Recs,
                              IntrinsicKind K);

public:
  RISCVIntrinsicManagerImpl(clang::Sema &S) : S(S), Context(S.Context) {
    ConstructedRISCVVBuiltins = false;
    ConstructedRISCVSiFiveVectorBuiltins = false;
  }

  // Initialize IntrinsicList
  void InitIntrinsicList() override;

  // Create RISC-V vector intrinsic and insert into symbol table if found, and
  // return true, otherwise return false.
  bool CreateIntrinsicIfFound(LookupResult &LR, IdentifierInfo *II,
                              Preprocessor &PP) override;
};
} // namespace

void RISCVIntrinsicManagerImpl::ConstructRVVIntrinsics(
    ArrayRef<RVVIntrinsicRecord> Recs, IntrinsicKind K) {
  const TargetInfo &TI = Context.getTargetInfo();
  static const std::pair<const char *, RVVRequire> FeatureCheckList[] = {
      {"64bit", RVV_REQ_RV64},
      {"xsfvcp", RVV_REQ_Xsfvcp},
      {"xsfvfnrclipxfqf", RVV_REQ_Xsfvfnrclipxfqf},
      {"xsfvfwmaccqqq", RVV_REQ_Xsfvfwmaccqqq},
      {"xsfvqmaccdod", RVV_REQ_Xsfvqmaccdod},
      {"xsfvqmaccqoq", RVV_REQ_Xsfvqmaccqoq},
      {"zvbb", RVV_REQ_Zvbb},
      {"zvbc", RVV_REQ_Zvbc},
      {"zvkb", RVV_REQ_Zvkb},
      {"zvkg", RVV_REQ_Zvkg},
      {"zvkned", RVV_REQ_Zvkned},
      {"zvknha", RVV_REQ_Zvknha},
      {"zvknhb", RVV_REQ_Zvknhb},
      {"zvksed", RVV_REQ_Zvksed},
      {"zvksh", RVV_REQ_Zvksh},
      {"zvfbfwma", RVV_REQ_Zvfbfwma},
      {"zvfbfmin", RVV_REQ_Zvfbfmin},
      {"experimental", RVV_REQ_Experimental}};

  // Construction of RVVIntrinsicRecords need to sync with createRVVIntrinsics
  // in RISCVVEmitter.cpp.
  for (auto &Record : Recs) {
    // Check requirements.
    if (llvm::any_of(FeatureCheckList, [&](const auto &Item) {
          return (Record.RequiredExtensions & Item.second) == Item.second &&
                 !TI.hasFeature(Item.first);
        }))
      continue;

    // Create Intrinsics for each type and LMUL.
    BasicType BaseType = BasicType::Unknown;
    ArrayRef<PrototypeDescriptor> BasicProtoSeq =
        ProtoSeq2ArrayRef(K, Record.PrototypeIndex, Record.PrototypeLength);
    ArrayRef<PrototypeDescriptor> SuffixProto =
        ProtoSeq2ArrayRef(K, Record.SuffixIndex, Record.SuffixLength);
    ArrayRef<PrototypeDescriptor> OverloadedSuffixProto = ProtoSeq2ArrayRef(
        K, Record.OverloadedSuffixIndex, Record.OverloadedSuffixSize);

    PolicyScheme UnMaskedPolicyScheme =
        static_cast<PolicyScheme>(Record.UnMaskedPolicyScheme);
    PolicyScheme MaskedPolicyScheme =
        static_cast<PolicyScheme>(Record.MaskedPolicyScheme);

    const Policy DefaultPolicy;

    llvm::SmallVector<PrototypeDescriptor> ProtoSeq =
        RVVIntrinsic::computeBuiltinTypes(
            BasicProtoSeq, /*IsMasked=*/false,
            /*HasMaskedOffOperand=*/false, Record.HasVL, Record.NF,
            UnMaskedPolicyScheme, DefaultPolicy, Record.IsTuple);

    llvm::SmallVector<PrototypeDescriptor> ProtoMaskSeq;
    if (Record.HasMasked)
      ProtoMaskSeq = RVVIntrinsic::computeBuiltinTypes(
          BasicProtoSeq, /*IsMasked=*/true, Record.HasMaskedOffOperand,
          Record.HasVL, Record.NF, MaskedPolicyScheme, DefaultPolicy,
          Record.IsTuple);

    bool UnMaskedHasPolicy = UnMaskedPolicyScheme != PolicyScheme::SchemeNone;
    bool MaskedHasPolicy = MaskedPolicyScheme != PolicyScheme::SchemeNone;
    SmallVector<Policy> SupportedUnMaskedPolicies =
        RVVIntrinsic::getSupportedUnMaskedPolicies();
    SmallVector<Policy> SupportedMaskedPolicies =
        RVVIntrinsic::getSupportedMaskedPolicies(Record.HasTailPolicy,
                                                 Record.HasMaskPolicy);

    for (unsigned int TypeRangeMaskShift = 0;
         TypeRangeMaskShift <= static_cast<unsigned int>(BasicType::MaxOffset);
         ++TypeRangeMaskShift) {
      unsigned int BaseTypeI = 1 << TypeRangeMaskShift;
      BaseType = static_cast<BasicType>(BaseTypeI);

      if ((BaseTypeI & Record.TypeRangeMask) != BaseTypeI)
        continue;

      if (BaseType == BasicType::Float16) {
        if ((Record.RequiredExtensions & RVV_REQ_Zvfhmin) == RVV_REQ_Zvfhmin) {
          if (!TI.hasFeature("zvfhmin"))
            continue;
        } else if (!TI.hasFeature("zvfh")) {
          continue;
        }
      }

      // Expanded with different LMUL.
      for (int Log2LMUL = -3; Log2LMUL <= 3; Log2LMUL++) {
        if (!(Record.Log2LMULMask & (1 << (Log2LMUL + 3))))
          continue;

        std::optional<RVVTypes> Types =
            TypeCache.computeTypes(BaseType, Log2LMUL, Record.NF, ProtoSeq);

        // Ignored to create new intrinsic if there are any illegal types.
        if (!Types.has_value())
          continue;

        std::string SuffixStr = RVVIntrinsic::getSuffixStr(
            TypeCache, BaseType, Log2LMUL, SuffixProto);
        std::string OverloadedSuffixStr = RVVIntrinsic::getSuffixStr(
            TypeCache, BaseType, Log2LMUL, OverloadedSuffixProto);

        // Create non-masked intrinsic.
        InitRVVIntrinsic(Record, SuffixStr, OverloadedSuffixStr, false, *Types,
                         UnMaskedHasPolicy, DefaultPolicy);

        // Create non-masked policy intrinsic.
        if (Record.UnMaskedPolicyScheme != PolicyScheme::SchemeNone) {
          for (auto P : SupportedUnMaskedPolicies) {
            llvm::SmallVector<PrototypeDescriptor> PolicyPrototype =
                RVVIntrinsic::computeBuiltinTypes(
                    BasicProtoSeq, /*IsMasked=*/false,
                    /*HasMaskedOffOperand=*/false, Record.HasVL, Record.NF,
                    UnMaskedPolicyScheme, P, Record.IsTuple);
            std::optional<RVVTypes> PolicyTypes = TypeCache.computeTypes(
                BaseType, Log2LMUL, Record.NF, PolicyPrototype);
            InitRVVIntrinsic(Record, SuffixStr, OverloadedSuffixStr,
                             /*IsMask=*/false, *PolicyTypes, UnMaskedHasPolicy,
                             P);
          }
        }
        if (!Record.HasMasked)
          continue;
        // Create masked intrinsic.
        std::optional<RVVTypes> MaskTypes =
            TypeCache.computeTypes(BaseType, Log2LMUL, Record.NF, ProtoMaskSeq);
        InitRVVIntrinsic(Record, SuffixStr, OverloadedSuffixStr, true,
                         *MaskTypes, MaskedHasPolicy, DefaultPolicy);
        if (Record.MaskedPolicyScheme == PolicyScheme::SchemeNone)
          continue;
        // Create masked policy intrinsic.
        for (auto P : SupportedMaskedPolicies) {
          llvm::SmallVector<PrototypeDescriptor> PolicyPrototype =
              RVVIntrinsic::computeBuiltinTypes(
                  BasicProtoSeq, /*IsMasked=*/true, Record.HasMaskedOffOperand,
                  Record.HasVL, Record.NF, MaskedPolicyScheme, P,
                  Record.IsTuple);
          std::optional<RVVTypes> PolicyTypes = TypeCache.computeTypes(
              BaseType, Log2LMUL, Record.NF, PolicyPrototype);
          InitRVVIntrinsic(Record, SuffixStr, OverloadedSuffixStr,
                           /*IsMask=*/true, *PolicyTypes, MaskedHasPolicy, P);
        }
      } // End for different LMUL
    } // End for different TypeRange
  }
}

void RISCVIntrinsicManagerImpl::InitIntrinsicList() {

  if (S.RISCV().DeclareRVVBuiltins && !ConstructedRISCVVBuiltins) {
    ConstructedRISCVVBuiltins = true;
    ConstructRVVIntrinsics(RVVIntrinsicRecords, IntrinsicKind::RVV);
  }
  if (S.RISCV().DeclareSiFiveVectorBuiltins &&
      !ConstructedRISCVSiFiveVectorBuiltins) {
    ConstructedRISCVSiFiveVectorBuiltins = true;
    ConstructRVVIntrinsics(RVSiFiveVectorIntrinsicRecords,
                           IntrinsicKind::SIFIVE_VECTOR);
  }
}

// Compute name and signatures for intrinsic with practical types.
void RISCVIntrinsicManagerImpl::InitRVVIntrinsic(
    const RVVIntrinsicRecord &Record, StringRef SuffixStr,
    StringRef OverloadedSuffixStr, bool IsMasked, RVVTypes &Signature,
    bool HasPolicy, Policy PolicyAttrs) {
  // Function name, e.g. vadd_vv_i32m1.
  std::string Name = Record.Name;
  if (!SuffixStr.empty())
    Name += "_" + SuffixStr.str();

  // Overloaded function name, e.g. vadd.
  std::string OverloadedName;
  if (!Record.OverloadedName)
    OverloadedName = StringRef(Record.Name).split("_").first.str();
  else
    OverloadedName = Record.OverloadedName;
  if (!OverloadedSuffixStr.empty())
    OverloadedName += "_" + OverloadedSuffixStr.str();

  // clang built-in function name, e.g. __builtin_rvv_vadd.
  std::string BuiltinName = std::string(Record.Name);

  RVVIntrinsic::updateNamesAndPolicy(IsMasked, HasPolicy, Name, BuiltinName,
                                     OverloadedName, PolicyAttrs,
                                     Record.HasFRMRoundModeOp);

  // Put into IntrinsicList.
  uint16_t Index = IntrinsicList.size();
  assert(IntrinsicList.size() == (size_t)Index &&
         "Intrinsics indices overflow.");
  IntrinsicList.push_back({BuiltinName, Signature});

  // Creating mapping to Intrinsics.
  Intrinsics.insert({Name, Index});

  // Get the RVVOverloadIntrinsicDef.
  RVVOverloadIntrinsicDef &OverloadIntrinsicDef =
      OverloadIntrinsics[OverloadedName];

  // And added the index.
  OverloadIntrinsicDef.Indexes.push_back(Index);
}

void RISCVIntrinsicManagerImpl::CreateRVVIntrinsicDecl(LookupResult &LR,
                                                       IdentifierInfo *II,
                                                       Preprocessor &PP,
                                                       uint32_t Index,
                                                       bool IsOverload) {
  ASTContext &Context = S.Context;
  RVVIntrinsicDef &IDef = IntrinsicList[Index];
  RVVTypes Sigs = IDef.Signature;
  size_t SigLength = Sigs.size();
  RVVType *ReturnType = Sigs[0];
  QualType RetType = RVVType2Qual(Context, ReturnType);
  SmallVector<QualType, 8> ArgTypes;
  QualType BuiltinFuncType;

  // Skip return type, and convert RVVType to QualType for arguments.
  for (size_t i = 1; i < SigLength; ++i)
    ArgTypes.push_back(RVVType2Qual(Context, Sigs[i]));

  FunctionProtoType::ExtProtoInfo PI(
      Context.getDefaultCallingConvention(false, false, true));

  PI.Variadic = false;

  SourceLocation Loc = LR.getNameLoc();
  BuiltinFuncType = Context.getFunctionType(RetType, ArgTypes, PI);
  DeclContext *Parent = Context.getTranslationUnitDecl();

  FunctionDecl *RVVIntrinsicDecl = FunctionDecl::Create(
      Context, Parent, Loc, Loc, II, BuiltinFuncType, /*TInfo=*/nullptr,
      SC_Extern, S.getCurFPFeatures().isFPConstrained(),
      /*isInlineSpecified*/ false,
      /*hasWrittenPrototype*/ true);

  // Create Decl objects for each parameter, adding them to the
  // FunctionDecl.
  const auto *FP = cast<FunctionProtoType>(BuiltinFuncType);
  SmallVector<ParmVarDecl *, 8> ParmList;
  for (unsigned IParm = 0, E = FP->getNumParams(); IParm != E; ++IParm) {
    ParmVarDecl *Parm =
        ParmVarDecl::Create(Context, RVVIntrinsicDecl, Loc, Loc, nullptr,
                            FP->getParamType(IParm), nullptr, SC_None, nullptr);
    Parm->setScopeInfo(0, IParm);
    ParmList.push_back(Parm);
  }
  RVVIntrinsicDecl->setParams(ParmList);

  // Add function attributes.
  if (IsOverload)
    RVVIntrinsicDecl->addAttr(OverloadableAttr::CreateImplicit(Context));

  // Setup alias to __builtin_rvv_*
  IdentifierInfo &IntrinsicII =
      PP.getIdentifierTable().get("__builtin_rvv_" + IDef.BuiltinName);
  RVVIntrinsicDecl->addAttr(
      BuiltinAliasAttr::CreateImplicit(S.Context, &IntrinsicII));

  // Add to symbol table.
  LR.addDecl(RVVIntrinsicDecl);
}

bool RISCVIntrinsicManagerImpl::CreateIntrinsicIfFound(LookupResult &LR,
                                                       IdentifierInfo *II,
                                                       Preprocessor &PP) {
  StringRef Name = II->getName();
  if (!Name.consume_front("__riscv_"))
    return false;

  // Lookup the function name from the overload intrinsics first.
  auto OvIItr = OverloadIntrinsics.find(Name);
  if (OvIItr != OverloadIntrinsics.end()) {
    const RVVOverloadIntrinsicDef &OvIntrinsicDef = OvIItr->second;
    for (auto Index : OvIntrinsicDef.Indexes)
      CreateRVVIntrinsicDecl(LR, II, PP, Index,
                             /*IsOverload*/ true);

    // If we added overloads, need to resolve the lookup result.
    LR.resolveKind();
    return true;
  }

  // Lookup the function name from the intrinsics.
  auto Itr = Intrinsics.find(Name);
  if (Itr != Intrinsics.end()) {
    CreateRVVIntrinsicDecl(LR, II, PP, Itr->second,
                           /*IsOverload*/ false);
    return true;
  }

  // It's not an RVV intrinsics.
  return false;
}

namespace clang {
std::unique_ptr<clang::sema::RISCVIntrinsicManager>
CreateRISCVIntrinsicManager(Sema &S) {
  return std::make_unique<RISCVIntrinsicManagerImpl>(S);
}

bool SemaRISCV::CheckLMUL(CallExpr *TheCall, unsigned ArgNum) {
  llvm::APSInt Result;

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check constant-ness first.
  if (SemaRef.BuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  int64_t Val = Result.getSExtValue();
  if ((Val >= 0 && Val <= 3) || (Val >= 5 && Val <= 7))
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_riscv_builtin_invalid_lmul)
         << Arg->getSourceRange();
}

static bool CheckInvalidVLENandLMUL(const TargetInfo &TI, CallExpr *TheCall,
                                    Sema &S, QualType Type, int EGW) {
  assert((EGW == 128 || EGW == 256) && "EGW can only be 128 or 256 bits");

  // LMUL * VLEN >= EGW
  ASTContext::BuiltinVectorTypeInfo Info =
      S.Context.getBuiltinVectorTypeInfo(Type->castAs<BuiltinType>());
  unsigned ElemSize = S.Context.getTypeSize(Info.ElementType);
  unsigned MinElemCount = Info.EC.getKnownMinValue();

  unsigned EGS = EGW / ElemSize;
  // If EGS is less than or equal to the minimum number of elements, then the
  // type is valid.
  if (EGS <= MinElemCount)
    return false;

  // Otherwise, we need vscale to be at least EGS / MinElemCont.
  assert(EGS % MinElemCount == 0);
  unsigned VScaleFactor = EGS / MinElemCount;
  // Vscale is VLEN/RVVBitsPerBlock.
  unsigned MinRequiredVLEN = VScaleFactor * llvm::RISCV::RVVBitsPerBlock;
  std::string RequiredExt = "zvl" + std::to_string(MinRequiredVLEN) + "b";
  if (!TI.hasFeature(RequiredExt))
    return S.Diag(TheCall->getBeginLoc(),
                  diag::err_riscv_type_requires_extension)
           << Type << RequiredExt;

  return false;
}

bool SemaRISCV::CheckBuiltinFunctionCall(const TargetInfo &TI,
                                         unsigned BuiltinID,
                                         CallExpr *TheCall) {
  ASTContext &Context = getASTContext();
  // vmulh.vv, vmulh.vx, vmulhu.vv, vmulhu.vx, vmulhsu.vv, vmulhsu.vx,
  // vsmul.vv, vsmul.vx are not included for EEW=64 in Zve64*.
  switch (BuiltinID) {
  default:
    break;
  case RISCVVector::BI__builtin_rvv_vmulhsu_vv:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vx:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vv_tu:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vx_tu:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vv_m:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vx_m:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vv_mu:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vx_mu:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vv_tum:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vx_tum:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vmulhsu_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vmulhu_vv:
  case RISCVVector::BI__builtin_rvv_vmulhu_vx:
  case RISCVVector::BI__builtin_rvv_vmulhu_vv_tu:
  case RISCVVector::BI__builtin_rvv_vmulhu_vx_tu:
  case RISCVVector::BI__builtin_rvv_vmulhu_vv_m:
  case RISCVVector::BI__builtin_rvv_vmulhu_vx_m:
  case RISCVVector::BI__builtin_rvv_vmulhu_vv_mu:
  case RISCVVector::BI__builtin_rvv_vmulhu_vx_mu:
  case RISCVVector::BI__builtin_rvv_vmulhu_vv_tum:
  case RISCVVector::BI__builtin_rvv_vmulhu_vx_tum:
  case RISCVVector::BI__builtin_rvv_vmulhu_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vmulhu_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vmulh_vv:
  case RISCVVector::BI__builtin_rvv_vmulh_vx:
  case RISCVVector::BI__builtin_rvv_vmulh_vv_tu:
  case RISCVVector::BI__builtin_rvv_vmulh_vx_tu:
  case RISCVVector::BI__builtin_rvv_vmulh_vv_m:
  case RISCVVector::BI__builtin_rvv_vmulh_vx_m:
  case RISCVVector::BI__builtin_rvv_vmulh_vv_mu:
  case RISCVVector::BI__builtin_rvv_vmulh_vx_mu:
  case RISCVVector::BI__builtin_rvv_vmulh_vv_tum:
  case RISCVVector::BI__builtin_rvv_vmulh_vx_tum:
  case RISCVVector::BI__builtin_rvv_vmulh_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vmulh_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vsmul_vv:
  case RISCVVector::BI__builtin_rvv_vsmul_vx:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_tu:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_tu:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_m:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_m:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_mu:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_mu:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_tum:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_tum:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_tumu: {
    ASTContext::BuiltinVectorTypeInfo Info = Context.getBuiltinVectorTypeInfo(
        TheCall->getType()->castAs<BuiltinType>());

    if (Context.getTypeSize(Info.ElementType) == 64 && !TI.hasFeature("v"))
      return Diag(TheCall->getBeginLoc(),
                  diag::err_riscv_builtin_requires_extension)
             << /* IsExtension */ true << TheCall->getSourceRange() << "v";

    break;
  }
  }

  switch (BuiltinID) {
  case RISCVVector::BI__builtin_rvv_vsetvli:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 3) ||
           CheckLMUL(TheCall, 2);
  case RISCVVector::BI__builtin_rvv_vsetvlimax:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3) ||
           CheckLMUL(TheCall, 1);
  case RISCVVector::BI__builtin_rvv_vget_v: {
    ASTContext::BuiltinVectorTypeInfo ResVecInfo =
        Context.getBuiltinVectorTypeInfo(cast<BuiltinType>(
            TheCall->getType().getCanonicalType().getTypePtr()));
    ASTContext::BuiltinVectorTypeInfo VecInfo =
        Context.getBuiltinVectorTypeInfo(cast<BuiltinType>(
            TheCall->getArg(0)->getType().getCanonicalType().getTypePtr()));
    unsigned MaxIndex;
    if (VecInfo.NumVectors != 1) // vget for tuple type
      MaxIndex = VecInfo.NumVectors;
    else // vget for non-tuple type
      MaxIndex = (VecInfo.EC.getKnownMinValue() * VecInfo.NumVectors) /
                 (ResVecInfo.EC.getKnownMinValue() * ResVecInfo.NumVectors);
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, MaxIndex - 1);
  }
  case RISCVVector::BI__builtin_rvv_vset_v: {
    ASTContext::BuiltinVectorTypeInfo ResVecInfo =
        Context.getBuiltinVectorTypeInfo(cast<BuiltinType>(
            TheCall->getType().getCanonicalType().getTypePtr()));
    ASTContext::BuiltinVectorTypeInfo VecInfo =
        Context.getBuiltinVectorTypeInfo(cast<BuiltinType>(
            TheCall->getArg(2)->getType().getCanonicalType().getTypePtr()));
    unsigned MaxIndex;
    if (ResVecInfo.NumVectors != 1) // vset for tuple type
      MaxIndex = ResVecInfo.NumVectors;
    else // vset fo non-tuple type
      MaxIndex = (ResVecInfo.EC.getKnownMinValue() * ResVecInfo.NumVectors) /
                 (VecInfo.EC.getKnownMinValue() * VecInfo.NumVectors);
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, MaxIndex - 1);
  }
  // Vector Crypto
  case RISCVVector::BI__builtin_rvv_vaeskf1_vi_tu:
  case RISCVVector::BI__builtin_rvv_vaeskf2_vi_tu:
  case RISCVVector::BI__builtin_rvv_vaeskf2_vi:
  case RISCVVector::BI__builtin_rvv_vsm4k_vi_tu: {
    QualType Op1Type = TheCall->getArg(0)->getType();
    QualType Op2Type = TheCall->getArg(1)->getType();
    return CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op1Type, 128) ||
           CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op2Type, 128) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 31);
  }
  case RISCVVector::BI__builtin_rvv_vsm3c_vi_tu:
  case RISCVVector::BI__builtin_rvv_vsm3c_vi: {
    QualType Op1Type = TheCall->getArg(0)->getType();
    return CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op1Type, 256) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 31);
  }
  case RISCVVector::BI__builtin_rvv_vaeskf1_vi:
  case RISCVVector::BI__builtin_rvv_vsm4k_vi: {
    QualType Op1Type = TheCall->getArg(0)->getType();
    return CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op1Type, 128) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31);
  }
  case RISCVVector::BI__builtin_rvv_vaesdf_vv:
  case RISCVVector::BI__builtin_rvv_vaesdf_vs:
  case RISCVVector::BI__builtin_rvv_vaesdm_vv:
  case RISCVVector::BI__builtin_rvv_vaesdm_vs:
  case RISCVVector::BI__builtin_rvv_vaesef_vv:
  case RISCVVector::BI__builtin_rvv_vaesef_vs:
  case RISCVVector::BI__builtin_rvv_vaesem_vv:
  case RISCVVector::BI__builtin_rvv_vaesem_vs:
  case RISCVVector::BI__builtin_rvv_vaesz_vs:
  case RISCVVector::BI__builtin_rvv_vsm4r_vv:
  case RISCVVector::BI__builtin_rvv_vsm4r_vs:
  case RISCVVector::BI__builtin_rvv_vaesdf_vv_tu:
  case RISCVVector::BI__builtin_rvv_vaesdf_vs_tu:
  case RISCVVector::BI__builtin_rvv_vaesdm_vv_tu:
  case RISCVVector::BI__builtin_rvv_vaesdm_vs_tu:
  case RISCVVector::BI__builtin_rvv_vaesef_vv_tu:
  case RISCVVector::BI__builtin_rvv_vaesef_vs_tu:
  case RISCVVector::BI__builtin_rvv_vaesem_vv_tu:
  case RISCVVector::BI__builtin_rvv_vaesem_vs_tu:
  case RISCVVector::BI__builtin_rvv_vaesz_vs_tu:
  case RISCVVector::BI__builtin_rvv_vsm4r_vv_tu:
  case RISCVVector::BI__builtin_rvv_vsm4r_vs_tu: {
    QualType Op1Type = TheCall->getArg(0)->getType();
    QualType Op2Type = TheCall->getArg(1)->getType();
    return CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op1Type, 128) ||
           CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op2Type, 128);
  }
  case RISCVVector::BI__builtin_rvv_vsha2ch_vv:
  case RISCVVector::BI__builtin_rvv_vsha2cl_vv:
  case RISCVVector::BI__builtin_rvv_vsha2ms_vv:
  case RISCVVector::BI__builtin_rvv_vsha2ch_vv_tu:
  case RISCVVector::BI__builtin_rvv_vsha2cl_vv_tu:
  case RISCVVector::BI__builtin_rvv_vsha2ms_vv_tu: {
    QualType Op1Type = TheCall->getArg(0)->getType();
    QualType Op2Type = TheCall->getArg(1)->getType();
    QualType Op3Type = TheCall->getArg(2)->getType();
    ASTContext::BuiltinVectorTypeInfo Info =
        Context.getBuiltinVectorTypeInfo(Op1Type->castAs<BuiltinType>());
    uint64_t ElemSize = Context.getTypeSize(Info.ElementType);
    if (ElemSize == 64 && !TI.hasFeature("zvknhb"))
      return Diag(TheCall->getBeginLoc(),
                  diag::err_riscv_builtin_requires_extension)
             << /* IsExtension */ true << TheCall->getSourceRange() << "zvknb";

    return CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op1Type,
                                   ElemSize * 4) ||
           CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op2Type,
                                   ElemSize * 4) ||
           CheckInvalidVLENandLMUL(TI, TheCall, SemaRef, Op3Type, ElemSize * 4);
  }

  case RISCVVector::BI__builtin_rvv_sf_vc_i_se:
    // bit_27_26, bit_24_20, bit_11_7, simm5, sew, log2lmul
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 31) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, -16, 15) ||
           CheckLMUL(TheCall, 5);
  case RISCVVector::BI__builtin_rvv_sf_vc_iv_se:
    // bit_27_26, bit_11_7, vs2, simm5
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, -16, 15);
  case RISCVVector::BI__builtin_rvv_sf_vc_v_i:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_i_se:
    // bit_27_26, bit_24_20, simm5
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, -16, 15);
  case RISCVVector::BI__builtin_rvv_sf_vc_v_iv:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_iv_se:
    // bit_27_26, vs2, simm5
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, -16, 15);
  case RISCVVector::BI__builtin_rvv_sf_vc_ivv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_ivw_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_ivv:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_ivw:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_ivv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_ivw_se:
    // bit_27_26, vd, vs2, simm5
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, -16, 15);
  case RISCVVector::BI__builtin_rvv_sf_vc_x_se:
    // bit_27_26, bit_24_20, bit_11_7, xs1, sew, log2lmul
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 31) ||
           CheckLMUL(TheCall, 5);
  case RISCVVector::BI__builtin_rvv_sf_vc_xv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_vv_se:
    // bit_27_26, bit_11_7, vs2, xs1/vs1
  case RISCVVector::BI__builtin_rvv_sf_vc_v_x:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_x_se:
    // bit_27_26, bit_24-20, xs1
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31);
  case RISCVVector::BI__builtin_rvv_sf_vc_vvv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_xvv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_vvw_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_xvw_se:
    // bit_27_26, vd, vs2, xs1
  case RISCVVector::BI__builtin_rvv_sf_vc_v_xv:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_vv:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_xv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_vv_se:
    // bit_27_26, vs2, xs1/vs1
  case RISCVVector::BI__builtin_rvv_sf_vc_v_xvv:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_vvv:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_xvw:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_vvw:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_xvv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_vvv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_xvw_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_vvw_se:
    // bit_27_26, vd, vs2, xs1/vs1
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 3);
  case RISCVVector::BI__builtin_rvv_sf_vc_fv_se:
    // bit_26, bit_11_7, vs2, fs1
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 1) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31);
  case RISCVVector::BI__builtin_rvv_sf_vc_fvv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_fvw_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_fvv:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_fvw:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_fvv_se:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_fvw_se:
    // bit_26, vd, vs2, fs1
  case RISCVVector::BI__builtin_rvv_sf_vc_v_fv:
  case RISCVVector::BI__builtin_rvv_sf_vc_v_fv_se:
    // bit_26, vs2, fs1
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 1);
  // Check if byteselect is in [0, 3]
  case RISCV::BI__builtin_riscv_aes32dsi:
  case RISCV::BI__builtin_riscv_aes32dsmi:
  case RISCV::BI__builtin_riscv_aes32esi:
  case RISCV::BI__builtin_riscv_aes32esmi:
  case RISCV::BI__builtin_riscv_sm4ks:
  case RISCV::BI__builtin_riscv_sm4ed:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 3);
  // Check if rnum is in [0, 10]
  case RISCV::BI__builtin_riscv_aes64ks1i:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 10);
  // Check if value range for vxrm is in [0, 3]
  case RISCVVector::BI__builtin_rvv_vaaddu_vv:
  case RISCVVector::BI__builtin_rvv_vaaddu_vx:
  case RISCVVector::BI__builtin_rvv_vaadd_vv:
  case RISCVVector::BI__builtin_rvv_vaadd_vx:
  case RISCVVector::BI__builtin_rvv_vasubu_vv:
  case RISCVVector::BI__builtin_rvv_vasubu_vx:
  case RISCVVector::BI__builtin_rvv_vasub_vv:
  case RISCVVector::BI__builtin_rvv_vasub_vx:
  case RISCVVector::BI__builtin_rvv_vsmul_vv:
  case RISCVVector::BI__builtin_rvv_vsmul_vx:
  case RISCVVector::BI__builtin_rvv_vssra_vv:
  case RISCVVector::BI__builtin_rvv_vssra_vx:
  case RISCVVector::BI__builtin_rvv_vssrl_vv:
  case RISCVVector::BI__builtin_rvv_vssrl_vx:
  case RISCVVector::BI__builtin_rvv_vnclip_wv:
  case RISCVVector::BI__builtin_rvv_vnclip_wx:
  case RISCVVector::BI__builtin_rvv_vnclipu_wv:
  case RISCVVector::BI__builtin_rvv_vnclipu_wx:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 3);
  case RISCVVector::BI__builtin_rvv_vaaddu_vv_tu:
  case RISCVVector::BI__builtin_rvv_vaaddu_vx_tu:
  case RISCVVector::BI__builtin_rvv_vaadd_vv_tu:
  case RISCVVector::BI__builtin_rvv_vaadd_vx_tu:
  case RISCVVector::BI__builtin_rvv_vasubu_vv_tu:
  case RISCVVector::BI__builtin_rvv_vasubu_vx_tu:
  case RISCVVector::BI__builtin_rvv_vasub_vv_tu:
  case RISCVVector::BI__builtin_rvv_vasub_vx_tu:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_tu:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_tu:
  case RISCVVector::BI__builtin_rvv_vssra_vv_tu:
  case RISCVVector::BI__builtin_rvv_vssra_vx_tu:
  case RISCVVector::BI__builtin_rvv_vssrl_vv_tu:
  case RISCVVector::BI__builtin_rvv_vssrl_vx_tu:
  case RISCVVector::BI__builtin_rvv_vnclip_wv_tu:
  case RISCVVector::BI__builtin_rvv_vnclip_wx_tu:
  case RISCVVector::BI__builtin_rvv_vnclipu_wv_tu:
  case RISCVVector::BI__builtin_rvv_vnclipu_wx_tu:
  case RISCVVector::BI__builtin_rvv_vaaddu_vv_m:
  case RISCVVector::BI__builtin_rvv_vaaddu_vx_m:
  case RISCVVector::BI__builtin_rvv_vaadd_vv_m:
  case RISCVVector::BI__builtin_rvv_vaadd_vx_m:
  case RISCVVector::BI__builtin_rvv_vasubu_vv_m:
  case RISCVVector::BI__builtin_rvv_vasubu_vx_m:
  case RISCVVector::BI__builtin_rvv_vasub_vv_m:
  case RISCVVector::BI__builtin_rvv_vasub_vx_m:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_m:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_m:
  case RISCVVector::BI__builtin_rvv_vssra_vv_m:
  case RISCVVector::BI__builtin_rvv_vssra_vx_m:
  case RISCVVector::BI__builtin_rvv_vssrl_vv_m:
  case RISCVVector::BI__builtin_rvv_vssrl_vx_m:
  case RISCVVector::BI__builtin_rvv_vnclip_wv_m:
  case RISCVVector::BI__builtin_rvv_vnclip_wx_m:
  case RISCVVector::BI__builtin_rvv_vnclipu_wv_m:
  case RISCVVector::BI__builtin_rvv_vnclipu_wx_m:
    return SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 3);
  case RISCVVector::BI__builtin_rvv_vaaddu_vv_tum:
  case RISCVVector::BI__builtin_rvv_vaaddu_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vaaddu_vv_mu:
  case RISCVVector::BI__builtin_rvv_vaaddu_vx_tum:
  case RISCVVector::BI__builtin_rvv_vaaddu_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vaaddu_vx_mu:
  case RISCVVector::BI__builtin_rvv_vaadd_vv_tum:
  case RISCVVector::BI__builtin_rvv_vaadd_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vaadd_vv_mu:
  case RISCVVector::BI__builtin_rvv_vaadd_vx_tum:
  case RISCVVector::BI__builtin_rvv_vaadd_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vaadd_vx_mu:
  case RISCVVector::BI__builtin_rvv_vasubu_vv_tum:
  case RISCVVector::BI__builtin_rvv_vasubu_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vasubu_vv_mu:
  case RISCVVector::BI__builtin_rvv_vasubu_vx_tum:
  case RISCVVector::BI__builtin_rvv_vasubu_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vasubu_vx_mu:
  case RISCVVector::BI__builtin_rvv_vasub_vv_tum:
  case RISCVVector::BI__builtin_rvv_vasub_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vasub_vv_mu:
  case RISCVVector::BI__builtin_rvv_vasub_vx_tum:
  case RISCVVector::BI__builtin_rvv_vasub_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vasub_vx_mu:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_mu:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_mu:
  case RISCVVector::BI__builtin_rvv_vssra_vv_mu:
  case RISCVVector::BI__builtin_rvv_vssra_vx_mu:
  case RISCVVector::BI__builtin_rvv_vssrl_vv_mu:
  case RISCVVector::BI__builtin_rvv_vssrl_vx_mu:
  case RISCVVector::BI__builtin_rvv_vnclip_wv_mu:
  case RISCVVector::BI__builtin_rvv_vnclip_wx_mu:
  case RISCVVector::BI__builtin_rvv_vnclipu_wv_mu:
  case RISCVVector::BI__builtin_rvv_vnclipu_wx_mu:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_tum:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_tum:
  case RISCVVector::BI__builtin_rvv_vssra_vv_tum:
  case RISCVVector::BI__builtin_rvv_vssra_vx_tum:
  case RISCVVector::BI__builtin_rvv_vssrl_vv_tum:
  case RISCVVector::BI__builtin_rvv_vssrl_vx_tum:
  case RISCVVector::BI__builtin_rvv_vnclip_wv_tum:
  case RISCVVector::BI__builtin_rvv_vnclip_wx_tum:
  case RISCVVector::BI__builtin_rvv_vnclipu_wv_tum:
  case RISCVVector::BI__builtin_rvv_vnclipu_wx_tum:
  case RISCVVector::BI__builtin_rvv_vsmul_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vsmul_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vssra_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vssra_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vssrl_vv_tumu:
  case RISCVVector::BI__builtin_rvv_vssrl_vx_tumu:
  case RISCVVector::BI__builtin_rvv_vnclip_wv_tumu:
  case RISCVVector::BI__builtin_rvv_vnclip_wx_tumu:
  case RISCVVector::BI__builtin_rvv_vnclipu_wv_tumu:
  case RISCVVector::BI__builtin_rvv_vnclipu_wx_tumu:
    return SemaRef.BuiltinConstantArgRange(TheCall, 4, 0, 3);
  case RISCVVector::BI__builtin_rvv_vfsqrt_v_rm:
  case RISCVVector::BI__builtin_rvv_vfrec7_v_rm:
  case RISCVVector::BI__builtin_rvv_vfcvt_x_f_v_rm:
  case RISCVVector::BI__builtin_rvv_vfcvt_xu_f_v_rm:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_x_v_rm:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_xu_v_rm:
  case RISCVVector::BI__builtin_rvv_vfwcvt_x_f_v_rm:
  case RISCVVector::BI__builtin_rvv_vfwcvt_xu_f_v_rm:
  case RISCVVector::BI__builtin_rvv_vfncvt_x_f_w_rm:
  case RISCVVector::BI__builtin_rvv_vfncvt_xu_f_w_rm:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_x_w_rm:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_xu_w_rm:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_f_w_rm:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 4);
  case RISCVVector::BI__builtin_rvv_vfadd_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfadd_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfsub_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfsub_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfrsub_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfwadd_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfwadd_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfwsub_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfwsub_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfwadd_wv_rm:
  case RISCVVector::BI__builtin_rvv_vfwadd_wf_rm:
  case RISCVVector::BI__builtin_rvv_vfwsub_wv_rm:
  case RISCVVector::BI__builtin_rvv_vfwsub_wf_rm:
  case RISCVVector::BI__builtin_rvv_vfmul_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfmul_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfdiv_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfdiv_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfrdiv_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfwmul_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfwmul_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfredosum_vs_rm:
  case RISCVVector::BI__builtin_rvv_vfredusum_vs_rm:
  case RISCVVector::BI__builtin_rvv_vfwredosum_vs_rm:
  case RISCVVector::BI__builtin_rvv_vfwredusum_vs_rm:
  case RISCVVector::BI__builtin_rvv_vfsqrt_v_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfrec7_v_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfcvt_x_f_v_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfcvt_xu_f_v_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_x_v_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_xu_v_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwcvt_x_f_v_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwcvt_xu_f_v_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfncvt_x_f_w_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfncvt_xu_f_w_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_x_w_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_xu_w_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_f_w_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfsqrt_v_rm_m:
  case RISCVVector::BI__builtin_rvv_vfrec7_v_rm_m:
  case RISCVVector::BI__builtin_rvv_vfcvt_x_f_v_rm_m:
  case RISCVVector::BI__builtin_rvv_vfcvt_xu_f_v_rm_m:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_x_v_rm_m:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_xu_v_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwcvt_x_f_v_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwcvt_xu_f_v_rm_m:
  case RISCVVector::BI__builtin_rvv_vfncvt_x_f_w_rm_m:
  case RISCVVector::BI__builtin_rvv_vfncvt_xu_f_w_rm_m:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_x_w_rm_m:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_xu_w_rm_m:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_f_w_rm_m:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 4);
  case RISCVVector::BI__builtin_rvv_vfadd_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfadd_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfsub_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfsub_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfrsub_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwadd_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwadd_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwsub_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwsub_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwadd_wv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwadd_wf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwsub_wv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwsub_wf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmul_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmul_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfdiv_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfdiv_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfrdiv_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwmul_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwmul_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfredosum_vs_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfredusum_vs_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwredosum_vs_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwredusum_vs_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmacc_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfmacc_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfmsac_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfmsac_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfmadd_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfmadd_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfmsub_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfmsub_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vv_rm:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vf_rm:
  case RISCVVector::BI__builtin_rvv_vfmacc_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmacc_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmsac_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmsac_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmadd_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmadd_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmsub_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfmsub_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vv_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vf_rm_tu:
  case RISCVVector::BI__builtin_rvv_vfadd_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfadd_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfsub_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfsub_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfrsub_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwadd_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwadd_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwsub_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwsub_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwadd_wv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwadd_wf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwsub_wv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwsub_wf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmul_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmul_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfdiv_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfdiv_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfrdiv_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwmul_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwmul_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfredosum_vs_rm_m:
  case RISCVVector::BI__builtin_rvv_vfredusum_vs_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwredosum_vs_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwredusum_vs_rm_m:
  case RISCVVector::BI__builtin_rvv_vfsqrt_v_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfrec7_v_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfcvt_x_f_v_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfcvt_xu_f_v_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_x_v_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_xu_v_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwcvt_x_f_v_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwcvt_xu_f_v_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfncvt_x_f_w_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfncvt_xu_f_w_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_x_w_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_xu_w_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_f_w_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfsqrt_v_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfrec7_v_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfcvt_x_f_v_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfcvt_xu_f_v_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_x_v_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_xu_v_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwcvt_x_f_v_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwcvt_xu_f_v_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfncvt_x_f_w_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfncvt_xu_f_w_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_x_w_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_xu_w_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_f_w_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfsqrt_v_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfrec7_v_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfcvt_x_f_v_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfcvt_xu_f_v_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_x_v_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfcvt_f_xu_v_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwcvt_x_f_v_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwcvt_xu_f_v_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfncvt_x_f_w_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfncvt_xu_f_w_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_x_w_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_xu_w_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfncvt_f_f_w_rm_mu:
    return SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 4);
  case RISCVVector::BI__builtin_rvv_vfmacc_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmacc_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmsac_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmsac_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmadd_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmadd_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmsub_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfmsub_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vv_rm_m:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vf_rm_m:
  case RISCVVector::BI__builtin_rvv_vfadd_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfadd_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfsub_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfsub_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfrsub_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwadd_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwadd_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwsub_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwsub_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwadd_wv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwadd_wf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwsub_wv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwsub_wf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmul_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmul_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfdiv_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfdiv_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfrdiv_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwmul_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwmul_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmacc_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmacc_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmsac_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmsac_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmadd_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmadd_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmsub_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfmsub_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vv_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vf_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfredosum_vs_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfredusum_vs_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwredosum_vs_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfwredusum_vs_rm_tum:
  case RISCVVector::BI__builtin_rvv_vfadd_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfadd_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfsub_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfsub_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfrsub_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwadd_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwadd_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwsub_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwsub_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwadd_wv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwadd_wf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwsub_wv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwsub_wf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmul_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmul_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfdiv_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfdiv_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfrdiv_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwmul_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwmul_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmacc_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmacc_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmsac_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmsac_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmadd_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmadd_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmsub_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfmsub_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vv_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vf_rm_tumu:
  case RISCVVector::BI__builtin_rvv_vfadd_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfadd_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfsub_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfsub_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfrsub_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwadd_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwadd_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwsub_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwsub_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwadd_wv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwadd_wf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwsub_wv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwsub_wf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmul_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmul_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfdiv_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfdiv_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfrdiv_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwmul_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwmul_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmacc_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmacc_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfnmacc_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmsac_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmsac_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfnmsac_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmadd_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmadd_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfnmadd_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmsub_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfmsub_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfnmsub_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwmacc_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwnmacc_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwmsac_vf_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vv_rm_mu:
  case RISCVVector::BI__builtin_rvv_vfwnmsac_vf_rm_mu:
    return SemaRef.BuiltinConstantArgRange(TheCall, 4, 0, 4);
  case RISCV::BI__builtin_riscv_ntl_load:
  case RISCV::BI__builtin_riscv_ntl_store:
    DeclRefExpr *DRE =
        cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());
    assert((BuiltinID == RISCV::BI__builtin_riscv_ntl_store ||
            BuiltinID == RISCV::BI__builtin_riscv_ntl_load) &&
           "Unexpected RISC-V nontemporal load/store builtin!");
    bool IsStore = BuiltinID == RISCV::BI__builtin_riscv_ntl_store;
    unsigned NumArgs = IsStore ? 3 : 2;

    if (SemaRef.checkArgCountAtLeast(TheCall, NumArgs - 1))
      return true;

    if (SemaRef.checkArgCountAtMost(TheCall, NumArgs))
      return true;

    // Domain value should be compile-time constant.
    // 2 <= domain <= 5
    if (TheCall->getNumArgs() == NumArgs &&
        SemaRef.BuiltinConstantArgRange(TheCall, NumArgs - 1, 2, 5))
      return true;

    Expr *PointerArg = TheCall->getArg(0);
    ExprResult PointerArgResult =
        SemaRef.DefaultFunctionArrayLvalueConversion(PointerArg);

    if (PointerArgResult.isInvalid())
      return true;
    PointerArg = PointerArgResult.get();

    const PointerType *PtrType = PointerArg->getType()->getAs<PointerType>();
    if (!PtrType) {
      Diag(DRE->getBeginLoc(), diag::err_nontemporal_builtin_must_be_pointer)
          << PointerArg->getType() << PointerArg->getSourceRange();
      return true;
    }

    QualType ValType = PtrType->getPointeeType();
    ValType = ValType.getUnqualifiedType();
    if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
        !ValType->isBlockPointerType() && !ValType->isFloatingType() &&
        !ValType->isVectorType() && !ValType->isRVVSizelessBuiltinType()) {
      Diag(DRE->getBeginLoc(),
           diag::err_nontemporal_builtin_must_be_pointer_intfltptr_or_vector)
          << PointerArg->getType() << PointerArg->getSourceRange();
      return true;
    }

    if (!IsStore) {
      TheCall->setType(ValType);
      return false;
    }

    ExprResult ValArg = TheCall->getArg(1);
    InitializedEntity Entity = InitializedEntity::InitializeParameter(
        Context, ValType, /*consume*/ false);
    ValArg =
        SemaRef.PerformCopyInitialization(Entity, SourceLocation(), ValArg);
    if (ValArg.isInvalid())
      return true;

    TheCall->setArg(1, ValArg.get());
    TheCall->setType(Context.VoidTy);
    return false;
  }

  return false;
}

void SemaRISCV::checkRVVTypeSupport(QualType Ty, SourceLocation Loc, Decl *D,
                                    const llvm::StringMap<bool> &FeatureMap) {
  ASTContext::BuiltinVectorTypeInfo Info =
      SemaRef.Context.getBuiltinVectorTypeInfo(Ty->castAs<BuiltinType>());
  unsigned EltSize = SemaRef.Context.getTypeSize(Info.ElementType);
  unsigned MinElts = Info.EC.getKnownMinValue();

  if (Info.ElementType->isSpecificBuiltinType(BuiltinType::Double) &&
      !FeatureMap.lookup("zve64d"))
    Diag(Loc, diag::err_riscv_type_requires_extension, D) << Ty << "zve64d";
  // (ELEN, LMUL) pairs of (8, mf8), (16, mf4), (32, mf2), (64, m1) requires at
  // least zve64x
  else if (((EltSize == 64 && Info.ElementType->isIntegerType()) ||
            MinElts == 1) &&
           !FeatureMap.lookup("zve64x"))
    Diag(Loc, diag::err_riscv_type_requires_extension, D) << Ty << "zve64x";
  else if (Info.ElementType->isFloat16Type() && !FeatureMap.lookup("zvfh") &&
           !FeatureMap.lookup("zvfhmin"))
    Diag(Loc, diag::err_riscv_type_requires_extension, D)
        << Ty << "zvfh or zvfhmin";
  else if (Info.ElementType->isBFloat16Type() && !FeatureMap.lookup("zvfbfmin"))
    Diag(Loc, diag::err_riscv_type_requires_extension, D) << Ty << "zvfbfmin";
  else if (Info.ElementType->isSpecificBuiltinType(BuiltinType::Float) &&
           !FeatureMap.lookup("zve32f"))
    Diag(Loc, diag::err_riscv_type_requires_extension, D) << Ty << "zve32f";
  // Given that caller already checked isRVVType() before calling this function,
  // if we don't have at least zve32x supported, then we need to emit error.
  else if (!FeatureMap.lookup("zve32x"))
    Diag(Loc, diag::err_riscv_type_requires_extension, D) << Ty << "zve32x";
}

/// Are the two types RVV-bitcast-compatible types? I.e. is bitcasting from the
/// first RVV type (e.g. an RVV scalable type) to the second type (e.g. an RVV
/// VLS type) allowed?
///
/// This will also return false if the two given types do not make sense from
/// the perspective of RVV bitcasts.
bool SemaRISCV::isValidRVVBitcast(QualType srcTy, QualType destTy) {
  assert(srcTy->isVectorType() || destTy->isVectorType());

  auto ValidScalableConversion = [](QualType FirstType, QualType SecondType) {
    if (!FirstType->isRVVSizelessBuiltinType())
      return false;

    const auto *VecTy = SecondType->getAs<VectorType>();
    return VecTy && VecTy->getVectorKind() == VectorKind::RVVFixedLengthData;
  };

  return ValidScalableConversion(srcTy, destTy) ||
         ValidScalableConversion(destTy, srcTy);
}

void SemaRISCV::handleInterruptAttr(Decl *D, const ParsedAttr &AL) {
  // Warn about repeated attributes.
  if (const auto *A = D->getAttr<RISCVInterruptAttr>()) {
    Diag(AL.getRange().getBegin(),
         diag::warn_riscv_repeated_interrupt_attribute);
    Diag(A->getLocation(), diag::note_riscv_repeated_interrupt_attribute);
    return;
  }

  // Check the attribute argument. Argument is optional.
  if (!AL.checkAtMostNumArgs(SemaRef, 1))
    return;

  StringRef Str;
  SourceLocation ArgLoc;

  // 'machine'is the default interrupt mode.
  if (AL.getNumArgs() == 0)
    Str = "machine";
  else if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  // Semantic checks for a function with the 'interrupt' attribute:
  // - Must be a function.
  // - Must have no parameters.
  // - Must have the 'void' return type.
  // - The attribute itself must either have no argument or one of the
  //   valid interrupt types, see [RISCVInterruptDocs].

  if (D->getFunctionType() == nullptr) {
    Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedFunction;
    return;
  }

  if (hasFunctionProto(D) && getFunctionOrMethodNumParams(D) != 0) {
    Diag(D->getLocation(), diag::warn_interrupt_attribute_invalid)
        << /*RISC-V*/ 2 << 0;
    return;
  }

  if (!getFunctionOrMethodResultType(D)->isVoidType()) {
    Diag(D->getLocation(), diag::warn_interrupt_attribute_invalid)
        << /*RISC-V*/ 2 << 1;
    return;
  }

  RISCVInterruptAttr::InterruptType Kind;
  if (!RISCVInterruptAttr::ConvertStrToInterruptType(Str, Kind)) {
    Diag(AL.getLoc(), diag::warn_attribute_type_not_supported)
        << AL << Str << ArgLoc;
    return;
  }

  D->addAttr(::new (getASTContext())
                 RISCVInterruptAttr(getASTContext(), AL, Kind));
}

bool SemaRISCV::isAliasValid(unsigned BuiltinID, StringRef AliasName) {
  return BuiltinID >= RISCV::FirstRVVBuiltin &&
         BuiltinID <= RISCV::LastRVVBuiltin;
}

SemaRISCV::SemaRISCV(Sema &S) : SemaBase(S) {}

} // namespace clang
