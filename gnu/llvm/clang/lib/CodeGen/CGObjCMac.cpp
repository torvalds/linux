//===------- CGObjCMac.cpp - Interface to Apple Objective-C Runtime -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides Objective-C code generation targeting the Apple runtime.
//
//===----------------------------------------------------------------------===//

#include "CGBlocks.h"
#include "CGCleanup.h"
#include "CGObjCRuntime.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/LangOptions.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/UniqueVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>

using namespace clang;
using namespace CodeGen;

namespace {

// FIXME: We should find a nicer way to make the labels for metadata, string
// concatenation is lame.

class ObjCCommonTypesHelper {
protected:
  llvm::LLVMContext &VMContext;

private:
  // The types of these functions don't really matter because we
  // should always bitcast before calling them.

  /// id objc_msgSend (id, SEL, ...)
  ///
  /// The default messenger, used for sends whose ABI is unchanged from
  /// the all-integer/pointer case.
  llvm::FunctionCallee getMessageSendFn() const {
    // Add the non-lazy-bind attribute, since objc_msgSend is likely to
    // be called a lot.
    llvm::Type *params[] = { ObjectPtrTy, SelectorPtrTy };
    return CGM.CreateRuntimeFunction(
        llvm::FunctionType::get(ObjectPtrTy, params, true), "objc_msgSend",
        llvm::AttributeList::get(CGM.getLLVMContext(),
                                 llvm::AttributeList::FunctionIndex,
                                 llvm::Attribute::NonLazyBind));
  }

  /// void objc_msgSend_stret (id, SEL, ...)
  ///
  /// The messenger used when the return value is an aggregate returned
  /// by indirect reference in the first argument, and therefore the
  /// self and selector parameters are shifted over by one.
  llvm::FunctionCallee getMessageSendStretFn() const {
    llvm::Type *params[] = { ObjectPtrTy, SelectorPtrTy };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(CGM.VoidTy,
                                                             params, true),
                                     "objc_msgSend_stret");
  }

  /// [double | long double] objc_msgSend_fpret(id self, SEL op, ...)
  ///
  /// The messenger used when the return value is returned on the x87
  /// floating-point stack; without a special entrypoint, the nil case
  /// would be unbalanced.
  llvm::FunctionCallee getMessageSendFpretFn() const {
    llvm::Type *params[] = { ObjectPtrTy, SelectorPtrTy };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(CGM.DoubleTy,
                                                             params, true),
                                     "objc_msgSend_fpret");
  }

  /// _Complex long double objc_msgSend_fp2ret(id self, SEL op, ...)
  ///
  /// The messenger used when the return value is returned in two values on the
  /// x87 floating point stack; without a special entrypoint, the nil case
  /// would be unbalanced. Only used on 64-bit X86.
  llvm::FunctionCallee getMessageSendFp2retFn() const {
    llvm::Type *params[] = { ObjectPtrTy, SelectorPtrTy };
    llvm::Type *longDoubleType = llvm::Type::getX86_FP80Ty(VMContext);
    llvm::Type *resultType =
        llvm::StructType::get(longDoubleType, longDoubleType);

    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(resultType,
                                                             params, true),
                                     "objc_msgSend_fp2ret");
  }

  /// id objc_msgSendSuper(struct objc_super *super, SEL op, ...)
  ///
  /// The messenger used for super calls, which have different dispatch
  /// semantics.  The class passed is the superclass of the current
  /// class.
  llvm::FunctionCallee getMessageSendSuperFn() const {
    llvm::Type *params[] = { SuperPtrTy, SelectorPtrTy };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                             params, true),
                                     "objc_msgSendSuper");
  }

  /// id objc_msgSendSuper2(struct objc_super *super, SEL op, ...)
  ///
  /// A slightly different messenger used for super calls.  The class
  /// passed is the current class.
  llvm::FunctionCallee getMessageSendSuperFn2() const {
    llvm::Type *params[] = { SuperPtrTy, SelectorPtrTy };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                             params, true),
                                     "objc_msgSendSuper2");
  }

  /// void objc_msgSendSuper_stret(void *stretAddr, struct objc_super *super,
  ///                              SEL op, ...)
  ///
  /// The messenger used for super calls which return an aggregate indirectly.
  llvm::FunctionCallee getMessageSendSuperStretFn() const {
    llvm::Type *params[] = { Int8PtrTy, SuperPtrTy, SelectorPtrTy };
    return CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGM.VoidTy, params, true),
      "objc_msgSendSuper_stret");
  }

  /// void objc_msgSendSuper2_stret(void * stretAddr, struct objc_super *super,
  ///                               SEL op, ...)
  ///
  /// objc_msgSendSuper_stret with the super2 semantics.
  llvm::FunctionCallee getMessageSendSuperStretFn2() const {
    llvm::Type *params[] = { Int8PtrTy, SuperPtrTy, SelectorPtrTy };
    return CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGM.VoidTy, params, true),
      "objc_msgSendSuper2_stret");
  }

  llvm::FunctionCallee getMessageSendSuperFpretFn() const {
    // There is no objc_msgSendSuper_fpret? How can that work?
    return getMessageSendSuperFn();
  }

  llvm::FunctionCallee getMessageSendSuperFpretFn2() const {
    // There is no objc_msgSendSuper_fpret? How can that work?
    return getMessageSendSuperFn2();
  }

protected:
  CodeGen::CodeGenModule &CGM;

public:
  llvm::IntegerType *ShortTy, *IntTy, *LongTy;
  llvm::PointerType *Int8PtrTy, *Int8PtrPtrTy;
  llvm::PointerType *Int8PtrProgramASTy;
  llvm::Type *IvarOffsetVarTy;

  /// ObjectPtrTy - LLVM type for object handles (typeof(id))
  llvm::PointerType *ObjectPtrTy;

  /// PtrObjectPtrTy - LLVM type for id *
  llvm::PointerType *PtrObjectPtrTy;

  /// SelectorPtrTy - LLVM type for selector handles (typeof(SEL))
  llvm::PointerType *SelectorPtrTy;

private:
  /// ProtocolPtrTy - LLVM type for external protocol handles
  /// (typeof(Protocol))
  llvm::Type *ExternalProtocolPtrTy;

public:
  llvm::Type *getExternalProtocolPtrTy() {
    if (!ExternalProtocolPtrTy) {
      // FIXME: It would be nice to unify this with the opaque type, so that the
      // IR comes out a bit cleaner.
      CodeGen::CodeGenTypes &Types = CGM.getTypes();
      ASTContext &Ctx = CGM.getContext();
      llvm::Type *T = Types.ConvertType(Ctx.getObjCProtoType());
      ExternalProtocolPtrTy = llvm::PointerType::getUnqual(T);
    }

    return ExternalProtocolPtrTy;
  }

  // SuperCTy - clang type for struct objc_super.
  QualType SuperCTy;
  // SuperPtrCTy - clang type for struct objc_super *.
  QualType SuperPtrCTy;

  /// SuperTy - LLVM type for struct objc_super.
  llvm::StructType *SuperTy;
  /// SuperPtrTy - LLVM type for struct objc_super *.
  llvm::PointerType *SuperPtrTy;

  /// PropertyTy - LLVM type for struct objc_property (struct _prop_t
  /// in GCC parlance).
  llvm::StructType *PropertyTy;

  /// PropertyListTy - LLVM type for struct objc_property_list
  /// (_prop_list_t in GCC parlance).
  llvm::StructType *PropertyListTy;
  /// PropertyListPtrTy - LLVM type for struct objc_property_list*.
  llvm::PointerType *PropertyListPtrTy;

  // MethodTy - LLVM type for struct objc_method.
  llvm::StructType *MethodTy;

  /// CacheTy - LLVM type for struct objc_cache.
  llvm::Type *CacheTy;
  /// CachePtrTy - LLVM type for struct objc_cache *.
  llvm::PointerType *CachePtrTy;

  llvm::FunctionCallee getGetPropertyFn() {
    CodeGen::CodeGenTypes &Types = CGM.getTypes();
    ASTContext &Ctx = CGM.getContext();
    // id objc_getProperty (id, SEL, ptrdiff_t, bool)
    CanQualType IdType = Ctx.getCanonicalParamType(Ctx.getObjCIdType());
    CanQualType SelType = Ctx.getCanonicalParamType(Ctx.getObjCSelType());
    CanQualType Params[] = {
        IdType, SelType,
        Ctx.getPointerDiffType()->getCanonicalTypeUnqualified(), Ctx.BoolTy};
    llvm::FunctionType *FTy =
        Types.GetFunctionType(
          Types.arrangeBuiltinFunctionDeclaration(IdType, Params));
    return CGM.CreateRuntimeFunction(FTy, "objc_getProperty");
  }

  llvm::FunctionCallee getSetPropertyFn() {
    CodeGen::CodeGenTypes &Types = CGM.getTypes();
    ASTContext &Ctx = CGM.getContext();
    // void objc_setProperty (id, SEL, ptrdiff_t, id, bool, bool)
    CanQualType IdType = Ctx.getCanonicalParamType(Ctx.getObjCIdType());
    CanQualType SelType = Ctx.getCanonicalParamType(Ctx.getObjCSelType());
    CanQualType Params[] = {
        IdType,
        SelType,
        Ctx.getPointerDiffType()->getCanonicalTypeUnqualified(),
        IdType,
        Ctx.BoolTy,
        Ctx.BoolTy};
    llvm::FunctionType *FTy =
        Types.GetFunctionType(
          Types.arrangeBuiltinFunctionDeclaration(Ctx.VoidTy, Params));
    return CGM.CreateRuntimeFunction(FTy, "objc_setProperty");
  }

  llvm::FunctionCallee getOptimizedSetPropertyFn(bool atomic, bool copy) {
    CodeGen::CodeGenTypes &Types = CGM.getTypes();
    ASTContext &Ctx = CGM.getContext();
    // void objc_setProperty_atomic(id self, SEL _cmd,
    //                              id newValue, ptrdiff_t offset);
    // void objc_setProperty_nonatomic(id self, SEL _cmd,
    //                                 id newValue, ptrdiff_t offset);
    // void objc_setProperty_atomic_copy(id self, SEL _cmd,
    //                                   id newValue, ptrdiff_t offset);
    // void objc_setProperty_nonatomic_copy(id self, SEL _cmd,
    //                                      id newValue, ptrdiff_t offset);

    SmallVector<CanQualType,4> Params;
    CanQualType IdType = Ctx.getCanonicalParamType(Ctx.getObjCIdType());
    CanQualType SelType = Ctx.getCanonicalParamType(Ctx.getObjCSelType());
    Params.push_back(IdType);
    Params.push_back(SelType);
    Params.push_back(IdType);
    Params.push_back(Ctx.getPointerDiffType()->getCanonicalTypeUnqualified());
    llvm::FunctionType *FTy =
        Types.GetFunctionType(
          Types.arrangeBuiltinFunctionDeclaration(Ctx.VoidTy, Params));
    const char *name;
    if (atomic && copy)
      name = "objc_setProperty_atomic_copy";
    else if (atomic && !copy)
      name = "objc_setProperty_atomic";
    else if (!atomic && copy)
      name = "objc_setProperty_nonatomic_copy";
    else
      name = "objc_setProperty_nonatomic";

    return CGM.CreateRuntimeFunction(FTy, name);
  }

  llvm::FunctionCallee getCopyStructFn() {
    CodeGen::CodeGenTypes &Types = CGM.getTypes();
    ASTContext &Ctx = CGM.getContext();
    // void objc_copyStruct (void *, const void *, size_t, bool, bool)
    SmallVector<CanQualType,5> Params;
    Params.push_back(Ctx.VoidPtrTy);
    Params.push_back(Ctx.VoidPtrTy);
    Params.push_back(Ctx.getSizeType());
    Params.push_back(Ctx.BoolTy);
    Params.push_back(Ctx.BoolTy);
    llvm::FunctionType *FTy =
        Types.GetFunctionType(
          Types.arrangeBuiltinFunctionDeclaration(Ctx.VoidTy, Params));
    return CGM.CreateRuntimeFunction(FTy, "objc_copyStruct");
  }

  /// This routine declares and returns address of:
  /// void objc_copyCppObjectAtomic(
  ///         void *dest, const void *src,
  ///         void (*copyHelper) (void *dest, const void *source));
  llvm::FunctionCallee getCppAtomicObjectFunction() {
    CodeGen::CodeGenTypes &Types = CGM.getTypes();
    ASTContext &Ctx = CGM.getContext();
    /// void objc_copyCppObjectAtomic(void *dest, const void *src, void *helper);
    SmallVector<CanQualType,3> Params;
    Params.push_back(Ctx.VoidPtrTy);
    Params.push_back(Ctx.VoidPtrTy);
    Params.push_back(Ctx.VoidPtrTy);
    llvm::FunctionType *FTy =
        Types.GetFunctionType(
          Types.arrangeBuiltinFunctionDeclaration(Ctx.VoidTy, Params));
    return CGM.CreateRuntimeFunction(FTy, "objc_copyCppObjectAtomic");
  }

  llvm::FunctionCallee getEnumerationMutationFn() {
    CodeGen::CodeGenTypes &Types = CGM.getTypes();
    ASTContext &Ctx = CGM.getContext();
    // void objc_enumerationMutation (id)
    SmallVector<CanQualType,1> Params;
    Params.push_back(Ctx.getCanonicalParamType(Ctx.getObjCIdType()));
    llvm::FunctionType *FTy =
        Types.GetFunctionType(
          Types.arrangeBuiltinFunctionDeclaration(Ctx.VoidTy, Params));
    return CGM.CreateRuntimeFunction(FTy, "objc_enumerationMutation");
  }

  llvm::FunctionCallee getLookUpClassFn() {
    CodeGen::CodeGenTypes &Types = CGM.getTypes();
    ASTContext &Ctx = CGM.getContext();
    // Class objc_lookUpClass (const char *)
    SmallVector<CanQualType,1> Params;
    Params.push_back(
      Ctx.getCanonicalType(Ctx.getPointerType(Ctx.CharTy.withConst())));
    llvm::FunctionType *FTy =
        Types.GetFunctionType(Types.arrangeBuiltinFunctionDeclaration(
                                Ctx.getCanonicalType(Ctx.getObjCClassType()),
                                Params));
    return CGM.CreateRuntimeFunction(FTy, "objc_lookUpClass");
  }

  /// GcReadWeakFn -- LLVM objc_read_weak (id *src) function.
  llvm::FunctionCallee getGcReadWeakFn() {
    // id objc_read_weak (id *)
    llvm::Type *args[] = { ObjectPtrTy->getPointerTo() };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(ObjectPtrTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_read_weak");
  }

  /// GcAssignWeakFn -- LLVM objc_assign_weak function.
  llvm::FunctionCallee getGcAssignWeakFn() {
    // id objc_assign_weak (id, id *)
    llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo() };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(ObjectPtrTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_assign_weak");
  }

  /// GcAssignGlobalFn -- LLVM objc_assign_global function.
  llvm::FunctionCallee getGcAssignGlobalFn() {
    // id objc_assign_global(id, id *)
    llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo() };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(ObjectPtrTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_assign_global");
  }

  /// GcAssignThreadLocalFn -- LLVM objc_assign_threadlocal function.
  llvm::FunctionCallee getGcAssignThreadLocalFn() {
    // id objc_assign_threadlocal(id src, id * dest)
    llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo() };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(ObjectPtrTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_assign_threadlocal");
  }

  /// GcAssignIvarFn -- LLVM objc_assign_ivar function.
  llvm::FunctionCallee getGcAssignIvarFn() {
    // id objc_assign_ivar(id, id *, ptrdiff_t)
    llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo(),
                           CGM.PtrDiffTy };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(ObjectPtrTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_assign_ivar");
  }

  /// GcMemmoveCollectableFn -- LLVM objc_memmove_collectable function.
  llvm::FunctionCallee GcMemmoveCollectableFn() {
    // void *objc_memmove_collectable(void *dst, const void *src, size_t size)
    llvm::Type *args[] = { Int8PtrTy, Int8PtrTy, LongTy };
    llvm::FunctionType *FTy = llvm::FunctionType::get(Int8PtrTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_memmove_collectable");
  }

  /// GcAssignStrongCastFn -- LLVM objc_assign_strongCast function.
  llvm::FunctionCallee getGcAssignStrongCastFn() {
    // id objc_assign_strongCast(id, id *)
    llvm::Type *args[] = { ObjectPtrTy, ObjectPtrTy->getPointerTo() };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(ObjectPtrTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_assign_strongCast");
  }

  /// ExceptionThrowFn - LLVM objc_exception_throw function.
  llvm::FunctionCallee getExceptionThrowFn() {
    // void objc_exception_throw(id)
    llvm::Type *args[] = { ObjectPtrTy };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.VoidTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_exception_throw");
  }

  /// ExceptionRethrowFn - LLVM objc_exception_rethrow function.
  llvm::FunctionCallee getExceptionRethrowFn() {
    // void objc_exception_rethrow(void)
    llvm::FunctionType *FTy = llvm::FunctionType::get(CGM.VoidTy, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_exception_rethrow");
  }

  /// SyncEnterFn - LLVM object_sync_enter function.
  llvm::FunctionCallee getSyncEnterFn() {
    // int objc_sync_enter (id)
    llvm::Type *args[] = { ObjectPtrTy };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.IntTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_sync_enter");
  }

  /// SyncExitFn - LLVM object_sync_exit function.
  llvm::FunctionCallee getSyncExitFn() {
    // int objc_sync_exit (id)
    llvm::Type *args[] = { ObjectPtrTy };
    llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.IntTy, args, false);
    return CGM.CreateRuntimeFunction(FTy, "objc_sync_exit");
  }

  llvm::FunctionCallee getSendFn(bool IsSuper) const {
    return IsSuper ? getMessageSendSuperFn() : getMessageSendFn();
  }

  llvm::FunctionCallee getSendFn2(bool IsSuper) const {
    return IsSuper ? getMessageSendSuperFn2() : getMessageSendFn();
  }

  llvm::FunctionCallee getSendStretFn(bool IsSuper) const {
    return IsSuper ? getMessageSendSuperStretFn() : getMessageSendStretFn();
  }

  llvm::FunctionCallee getSendStretFn2(bool IsSuper) const {
    return IsSuper ? getMessageSendSuperStretFn2() : getMessageSendStretFn();
  }

  llvm::FunctionCallee getSendFpretFn(bool IsSuper) const {
    return IsSuper ? getMessageSendSuperFpretFn() : getMessageSendFpretFn();
  }

  llvm::FunctionCallee getSendFpretFn2(bool IsSuper) const {
    return IsSuper ? getMessageSendSuperFpretFn2() : getMessageSendFpretFn();
  }

  llvm::FunctionCallee getSendFp2retFn(bool IsSuper) const {
    return IsSuper ? getMessageSendSuperFn() : getMessageSendFp2retFn();
  }

  llvm::FunctionCallee getSendFp2RetFn2(bool IsSuper) const {
    return IsSuper ? getMessageSendSuperFn2() : getMessageSendFp2retFn();
  }

  ObjCCommonTypesHelper(CodeGen::CodeGenModule &cgm);
};

/// ObjCTypesHelper - Helper class that encapsulates lazy
/// construction of varies types used during ObjC generation.
class ObjCTypesHelper : public ObjCCommonTypesHelper {
public:
  /// SymtabTy - LLVM type for struct objc_symtab.
  llvm::StructType *SymtabTy;
  /// SymtabPtrTy - LLVM type for struct objc_symtab *.
  llvm::PointerType *SymtabPtrTy;
  /// ModuleTy - LLVM type for struct objc_module.
  llvm::StructType *ModuleTy;

  /// ProtocolTy - LLVM type for struct objc_protocol.
  llvm::StructType *ProtocolTy;
  /// ProtocolPtrTy - LLVM type for struct objc_protocol *.
  llvm::PointerType *ProtocolPtrTy;
  /// ProtocolExtensionTy - LLVM type for struct
  /// objc_protocol_extension.
  llvm::StructType *ProtocolExtensionTy;
  /// ProtocolExtensionTy - LLVM type for struct
  /// objc_protocol_extension *.
  llvm::PointerType *ProtocolExtensionPtrTy;
  /// MethodDescriptionTy - LLVM type for struct
  /// objc_method_description.
  llvm::StructType *MethodDescriptionTy;
  /// MethodDescriptionListTy - LLVM type for struct
  /// objc_method_description_list.
  llvm::StructType *MethodDescriptionListTy;
  /// MethodDescriptionListPtrTy - LLVM type for struct
  /// objc_method_description_list *.
  llvm::PointerType *MethodDescriptionListPtrTy;
  /// ProtocolListTy - LLVM type for struct objc_property_list.
  llvm::StructType *ProtocolListTy;
  /// ProtocolListPtrTy - LLVM type for struct objc_property_list*.
  llvm::PointerType *ProtocolListPtrTy;
  /// CategoryTy - LLVM type for struct objc_category.
  llvm::StructType *CategoryTy;
  /// ClassTy - LLVM type for struct objc_class.
  llvm::StructType *ClassTy;
  /// ClassPtrTy - LLVM type for struct objc_class *.
  llvm::PointerType *ClassPtrTy;
  /// ClassExtensionTy - LLVM type for struct objc_class_ext.
  llvm::StructType *ClassExtensionTy;
  /// ClassExtensionPtrTy - LLVM type for struct objc_class_ext *.
  llvm::PointerType *ClassExtensionPtrTy;
  // IvarTy - LLVM type for struct objc_ivar.
  llvm::StructType *IvarTy;
  /// IvarListTy - LLVM type for struct objc_ivar_list.
  llvm::StructType *IvarListTy;
  /// IvarListPtrTy - LLVM type for struct objc_ivar_list *.
  llvm::PointerType *IvarListPtrTy;
  /// MethodListTy - LLVM type for struct objc_method_list.
  llvm::StructType *MethodListTy;
  /// MethodListPtrTy - LLVM type for struct objc_method_list *.
  llvm::PointerType *MethodListPtrTy;

  /// ExceptionDataTy - LLVM type for struct _objc_exception_data.
  llvm::StructType *ExceptionDataTy;

  /// ExceptionTryEnterFn - LLVM objc_exception_try_enter function.
  llvm::FunctionCallee getExceptionTryEnterFn() {
    llvm::Type *params[] = { ExceptionDataTy->getPointerTo() };
    return CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGM.VoidTy, params, false),
      "objc_exception_try_enter");
  }

  /// ExceptionTryExitFn - LLVM objc_exception_try_exit function.
  llvm::FunctionCallee getExceptionTryExitFn() {
    llvm::Type *params[] = { ExceptionDataTy->getPointerTo() };
    return CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGM.VoidTy, params, false),
      "objc_exception_try_exit");
  }

  /// ExceptionExtractFn - LLVM objc_exception_extract function.
  llvm::FunctionCallee getExceptionExtractFn() {
    llvm::Type *params[] = { ExceptionDataTy->getPointerTo() };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                             params, false),
                                     "objc_exception_extract");
  }

  /// ExceptionMatchFn - LLVM objc_exception_match function.
  llvm::FunctionCallee getExceptionMatchFn() {
    llvm::Type *params[] = { ClassPtrTy, ObjectPtrTy };
    return CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGM.Int32Ty, params, false),
      "objc_exception_match");
  }

  /// SetJmpFn - LLVM _setjmp function.
  llvm::FunctionCallee getSetJmpFn() {
    // This is specifically the prototype for x86.
    llvm::Type *params[] = { CGM.Int32Ty->getPointerTo() };
    return CGM.CreateRuntimeFunction(
        llvm::FunctionType::get(CGM.Int32Ty, params, false), "_setjmp",
        llvm::AttributeList::get(CGM.getLLVMContext(),
                                 llvm::AttributeList::FunctionIndex,
                                 llvm::Attribute::NonLazyBind));
  }

public:
  ObjCTypesHelper(CodeGen::CodeGenModule &cgm);
};

/// ObjCNonFragileABITypesHelper - will have all types needed by objective-c's
/// modern abi
class ObjCNonFragileABITypesHelper : public ObjCCommonTypesHelper {
public:
  // MethodListnfABITy - LLVM for struct _method_list_t
  llvm::StructType *MethodListnfABITy;

  // MethodListnfABIPtrTy - LLVM for struct _method_list_t*
  llvm::PointerType *MethodListnfABIPtrTy;

  // ProtocolnfABITy = LLVM for struct _protocol_t
  llvm::StructType *ProtocolnfABITy;

  // ProtocolnfABIPtrTy = LLVM for struct _protocol_t*
  llvm::PointerType *ProtocolnfABIPtrTy;

  // ProtocolListnfABITy - LLVM for struct _objc_protocol_list
  llvm::StructType *ProtocolListnfABITy;

  // ProtocolListnfABIPtrTy - LLVM for struct _objc_protocol_list*
  llvm::PointerType *ProtocolListnfABIPtrTy;

  // ClassnfABITy - LLVM for struct _class_t
  llvm::StructType *ClassnfABITy;

  // ClassnfABIPtrTy - LLVM for struct _class_t*
  llvm::PointerType *ClassnfABIPtrTy;

  // IvarnfABITy - LLVM for struct _ivar_t
  llvm::StructType *IvarnfABITy;

  // IvarListnfABITy - LLVM for struct _ivar_list_t
  llvm::StructType *IvarListnfABITy;

  // IvarListnfABIPtrTy = LLVM for struct _ivar_list_t*
  llvm::PointerType *IvarListnfABIPtrTy;

  // ClassRonfABITy - LLVM for struct _class_ro_t
  llvm::StructType *ClassRonfABITy;

  // ImpnfABITy - LLVM for id (*)(id, SEL, ...)
  llvm::PointerType *ImpnfABITy;

  // CategorynfABITy - LLVM for struct _category_t
  llvm::StructType *CategorynfABITy;

  // New types for nonfragile abi messaging.

  // MessageRefTy - LLVM for:
  // struct _message_ref_t {
  //   IMP messenger;
  //   SEL name;
  // };
  llvm::StructType *MessageRefTy;
  // MessageRefCTy - clang type for struct _message_ref_t
  QualType MessageRefCTy;

  // MessageRefPtrTy - LLVM for struct _message_ref_t*
  llvm::Type *MessageRefPtrTy;
  // MessageRefCPtrTy - clang type for struct _message_ref_t*
  QualType MessageRefCPtrTy;

  // SuperMessageRefTy - LLVM for:
  // struct _super_message_ref_t {
  //   SUPER_IMP messenger;
  //   SEL name;
  // };
  llvm::StructType *SuperMessageRefTy;

  // SuperMessageRefPtrTy - LLVM for struct _super_message_ref_t*
  llvm::PointerType *SuperMessageRefPtrTy;

  llvm::FunctionCallee getMessageSendFixupFn() {
    // id objc_msgSend_fixup(id, struct message_ref_t*, ...)
    llvm::Type *params[] = { ObjectPtrTy, MessageRefPtrTy };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                             params, true),
                                     "objc_msgSend_fixup");
  }

  llvm::FunctionCallee getMessageSendFpretFixupFn() {
    // id objc_msgSend_fpret_fixup(id, struct message_ref_t*, ...)
    llvm::Type *params[] = { ObjectPtrTy, MessageRefPtrTy };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                             params, true),
                                     "objc_msgSend_fpret_fixup");
  }

  llvm::FunctionCallee getMessageSendStretFixupFn() {
    // id objc_msgSend_stret_fixup(id, struct message_ref_t*, ...)
    llvm::Type *params[] = { ObjectPtrTy, MessageRefPtrTy };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                             params, true),
                                     "objc_msgSend_stret_fixup");
  }

  llvm::FunctionCallee getMessageSendSuper2FixupFn() {
    // id objc_msgSendSuper2_fixup (struct objc_super *,
    //                              struct _super_message_ref_t*, ...)
    llvm::Type *params[] = { SuperPtrTy, SuperMessageRefPtrTy };
    return  CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                              params, true),
                                      "objc_msgSendSuper2_fixup");
  }

  llvm::FunctionCallee getMessageSendSuper2StretFixupFn() {
    // id objc_msgSendSuper2_stret_fixup(struct objc_super *,
    //                                   struct _super_message_ref_t*, ...)
    llvm::Type *params[] = { SuperPtrTy, SuperMessageRefPtrTy };
    return  CGM.CreateRuntimeFunction(llvm::FunctionType::get(ObjectPtrTy,
                                                              params, true),
                                      "objc_msgSendSuper2_stret_fixup");
  }

  llvm::FunctionCallee getObjCEndCatchFn() {
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(CGM.VoidTy, false),
                                     "objc_end_catch");
  }

  llvm::FunctionCallee getObjCBeginCatchFn() {
    llvm::Type *params[] = { Int8PtrTy };
    return CGM.CreateRuntimeFunction(llvm::FunctionType::get(Int8PtrTy,
                                                             params, false),
                                     "objc_begin_catch");
  }

  /// Class objc_loadClassref (void *)
  ///
  /// Loads from a classref. For Objective-C stub classes, this invokes the
  /// initialization callback stored inside the stub. For all other classes
  /// this simply dereferences the pointer.
  llvm::FunctionCallee getLoadClassrefFn() const {
    // Add the non-lazy-bind attribute, since objc_loadClassref is likely to
    // be called a lot.
    //
    // Also it is safe to make it readnone, since we never load or store the
    // classref except by calling this function.
    llvm::Type *params[] = { Int8PtrPtrTy };
    llvm::LLVMContext &C = CGM.getLLVMContext();
    llvm::AttributeSet AS = llvm::AttributeSet::get(C, {
        llvm::Attribute::get(C, llvm::Attribute::NonLazyBind),
        llvm::Attribute::getWithMemoryEffects(C, llvm::MemoryEffects::none()),
        llvm::Attribute::get(C, llvm::Attribute::NoUnwind),
    });
    llvm::FunctionCallee F = CGM.CreateRuntimeFunction(
        llvm::FunctionType::get(ClassnfABIPtrTy, params, false),
        "objc_loadClassref",
        llvm::AttributeList::get(CGM.getLLVMContext(),
                                 llvm::AttributeList::FunctionIndex, AS));
    if (!CGM.getTriple().isOSBinFormatCOFF())
      cast<llvm::Function>(F.getCallee())->setLinkage(
        llvm::Function::ExternalWeakLinkage);

    return F;
  }

  llvm::StructType *EHTypeTy;
  llvm::Type *EHTypePtrTy;

  ObjCNonFragileABITypesHelper(CodeGen::CodeGenModule &cgm);
};

enum class ObjCLabelType {
  ClassName,
  MethodVarName,
  MethodVarType,
  PropertyName,
};

class CGObjCCommonMac : public CodeGen::CGObjCRuntime {
public:
  class SKIP_SCAN {
  public:
    unsigned skip;
    unsigned scan;
    SKIP_SCAN(unsigned _skip = 0, unsigned _scan = 0)
      : skip(_skip), scan(_scan) {}
  };

  /// opcode for captured block variables layout 'instructions'.
  /// In the following descriptions, 'I' is the value of the immediate field.
  /// (field following the opcode).
  ///
  enum BLOCK_LAYOUT_OPCODE {
    /// An operator which affects how the following layout should be
    /// interpreted.
    ///   I == 0: Halt interpretation and treat everything else as
    ///           a non-pointer.  Note that this instruction is equal
    ///           to '\0'.
    ///   I != 0: Currently unused.
    BLOCK_LAYOUT_OPERATOR            = 0,

    /// The next I+1 bytes do not contain a value of object pointer type.
    /// Note that this can leave the stream unaligned, meaning that
    /// subsequent word-size instructions do not begin at a multiple of
    /// the pointer size.
    BLOCK_LAYOUT_NON_OBJECT_BYTES    = 1,

    /// The next I+1 words do not contain a value of object pointer type.
    /// This is simply an optimized version of BLOCK_LAYOUT_BYTES for
    /// when the required skip quantity is a multiple of the pointer size.
    BLOCK_LAYOUT_NON_OBJECT_WORDS    = 2,

    /// The next I+1 words are __strong pointers to Objective-C
    /// objects or blocks.
    BLOCK_LAYOUT_STRONG              = 3,

    /// The next I+1 words are pointers to __block variables.
    BLOCK_LAYOUT_BYREF               = 4,

    /// The next I+1 words are __weak pointers to Objective-C
    /// objects or blocks.
    BLOCK_LAYOUT_WEAK                = 5,

    /// The next I+1 words are __unsafe_unretained pointers to
    /// Objective-C objects or blocks.
    BLOCK_LAYOUT_UNRETAINED          = 6

    /// The next I+1 words are block or object pointers with some
    /// as-yet-unspecified ownership semantics.  If we add more
    /// flavors of ownership semantics, values will be taken from
    /// this range.
    ///
    /// This is included so that older tools can at least continue
    /// processing the layout past such things.
    //BLOCK_LAYOUT_OWNERSHIP_UNKNOWN = 7..10,

    /// All other opcodes are reserved.  Halt interpretation and
    /// treat everything else as opaque.
  };

  class RUN_SKIP {
  public:
    enum BLOCK_LAYOUT_OPCODE opcode;
    CharUnits block_var_bytepos;
    CharUnits block_var_size;
    RUN_SKIP(enum BLOCK_LAYOUT_OPCODE Opcode = BLOCK_LAYOUT_OPERATOR,
             CharUnits BytePos = CharUnits::Zero(),
             CharUnits Size = CharUnits::Zero())
    : opcode(Opcode), block_var_bytepos(BytePos),  block_var_size(Size) {}

    // Allow sorting based on byte pos.
    bool operator<(const RUN_SKIP &b) const {
      return block_var_bytepos < b.block_var_bytepos;
    }
  };

protected:
  llvm::LLVMContext &VMContext;
  // FIXME! May not be needing this after all.
  unsigned ObjCABI;

  // arc/mrr layout of captured block literal variables.
  SmallVector<RUN_SKIP, 16> RunSkipBlockVars;

  /// LazySymbols - Symbols to generate a lazy reference for. See
  /// DefinedSymbols and FinishModule().
  llvm::SetVector<IdentifierInfo*> LazySymbols;

  /// DefinedSymbols - External symbols which are defined by this
  /// module. The symbols in this list and LazySymbols are used to add
  /// special linker symbols which ensure that Objective-C modules are
  /// linked properly.
  llvm::SetVector<IdentifierInfo*> DefinedSymbols;

  /// ClassNames - uniqued class names.
  llvm::StringMap<llvm::GlobalVariable*> ClassNames;

  /// MethodVarNames - uniqued method variable names.
  llvm::DenseMap<Selector, llvm::GlobalVariable*> MethodVarNames;

  /// DefinedCategoryNames - list of category names in form Class_Category.
  llvm::SmallSetVector<llvm::CachedHashString, 16> DefinedCategoryNames;

  /// MethodVarTypes - uniqued method type signatures. We have to use
  /// a StringMap here because have no other unique reference.
  llvm::StringMap<llvm::GlobalVariable*> MethodVarTypes;

  /// MethodDefinitions - map of methods which have been defined in
  /// this translation unit.
  llvm::DenseMap<const ObjCMethodDecl*, llvm::Function*> MethodDefinitions;

  /// DirectMethodDefinitions - map of direct methods which have been defined in
  /// this translation unit.
  llvm::DenseMap<const ObjCMethodDecl*, llvm::Function*> DirectMethodDefinitions;

  /// PropertyNames - uniqued method variable names.
  llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> PropertyNames;

  /// ClassReferences - uniqued class references.
  llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> ClassReferences;

  /// SelectorReferences - uniqued selector references.
  llvm::DenseMap<Selector, llvm::GlobalVariable*> SelectorReferences;

  /// Protocols - Protocols for which an objc_protocol structure has
  /// been emitted. Forward declarations are handled by creating an
  /// empty structure whose initializer is filled in when/if defined.
  llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> Protocols;

  /// DefinedProtocols - Protocols which have actually been
  /// defined. We should not need this, see FIXME in GenerateProtocol.
  llvm::DenseSet<IdentifierInfo*> DefinedProtocols;

  /// DefinedClasses - List of defined classes.
  SmallVector<llvm::GlobalValue*, 16> DefinedClasses;

  /// ImplementedClasses - List of @implemented classes.
  SmallVector<const ObjCInterfaceDecl*, 16> ImplementedClasses;

  /// DefinedNonLazyClasses - List of defined "non-lazy" classes.
  SmallVector<llvm::GlobalValue*, 16> DefinedNonLazyClasses;

  /// DefinedCategories - List of defined categories.
  SmallVector<llvm::GlobalValue*, 16> DefinedCategories;

  /// DefinedStubCategories - List of defined categories on class stubs.
  SmallVector<llvm::GlobalValue*, 16> DefinedStubCategories;

  /// DefinedNonLazyCategories - List of defined "non-lazy" categories.
  SmallVector<llvm::GlobalValue*, 16> DefinedNonLazyCategories;

  /// Cached reference to the class for constant strings. This value has type
  /// int * but is actually an Obj-C class pointer.
  llvm::WeakTrackingVH ConstantStringClassRef;

  /// The LLVM type corresponding to NSConstantString.
  llvm::StructType *NSConstantStringType = nullptr;

  llvm::StringMap<llvm::GlobalVariable *> NSConstantStringMap;

  /// GetMethodVarName - Return a unique constant for the given
  /// selector's name. The return value has type char *.
  llvm::Constant *GetMethodVarName(Selector Sel);
  llvm::Constant *GetMethodVarName(IdentifierInfo *Ident);

  /// GetMethodVarType - Return a unique constant for the given
  /// method's type encoding string. The return value has type char *.

  // FIXME: This is a horrible name.
  llvm::Constant *GetMethodVarType(const ObjCMethodDecl *D,
                                   bool Extended = false);
  llvm::Constant *GetMethodVarType(const FieldDecl *D);

  /// GetPropertyName - Return a unique constant for the given
  /// name. The return value has type char *.
  llvm::Constant *GetPropertyName(IdentifierInfo *Ident);

  // FIXME: This can be dropped once string functions are unified.
  llvm::Constant *GetPropertyTypeString(const ObjCPropertyDecl *PD,
                                        const Decl *Container);

  /// GetClassName - Return a unique constant for the given selector's
  /// runtime name (which may change via use of objc_runtime_name attribute on
  /// class or protocol definition. The return value has type char *.
  llvm::Constant *GetClassName(StringRef RuntimeName);

  llvm::Function *GetMethodDefinition(const ObjCMethodDecl *MD);

  /// BuildIvarLayout - Builds ivar layout bitmap for the class
  /// implementation for the __strong or __weak case.
  ///
  /// \param hasMRCWeakIvars - Whether we are compiling in MRC and there
  ///   are any weak ivars defined directly in the class.  Meaningless unless
  ///   building a weak layout.  Does not guarantee that the layout will
  ///   actually have any entries, because the ivar might be under-aligned.
  llvm::Constant *BuildIvarLayout(const ObjCImplementationDecl *OI,
                                  CharUnits beginOffset,
                                  CharUnits endOffset,
                                  bool forStrongLayout,
                                  bool hasMRCWeakIvars);

  llvm::Constant *BuildStrongIvarLayout(const ObjCImplementationDecl *OI,
                                        CharUnits beginOffset,
                                        CharUnits endOffset) {
    return BuildIvarLayout(OI, beginOffset, endOffset, true, false);
  }

  llvm::Constant *BuildWeakIvarLayout(const ObjCImplementationDecl *OI,
                                      CharUnits beginOffset,
                                      CharUnits endOffset,
                                      bool hasMRCWeakIvars) {
    return BuildIvarLayout(OI, beginOffset, endOffset, false, hasMRCWeakIvars);
  }

  Qualifiers::ObjCLifetime getBlockCaptureLifetime(QualType QT, bool ByrefLayout);

  void UpdateRunSkipBlockVars(bool IsByref,
                              Qualifiers::ObjCLifetime LifeTime,
                              CharUnits FieldOffset,
                              CharUnits FieldSize);

  void BuildRCBlockVarRecordLayout(const RecordType *RT,
                                   CharUnits BytePos, bool &HasUnion,
                                   bool ByrefLayout=false);

  void BuildRCRecordLayout(const llvm::StructLayout *RecLayout,
                           const RecordDecl *RD,
                           ArrayRef<const FieldDecl*> RecFields,
                           CharUnits BytePos, bool &HasUnion,
                           bool ByrefLayout);

  uint64_t InlineLayoutInstruction(SmallVectorImpl<unsigned char> &Layout);

  llvm::Constant *getBitmapBlockLayout(bool ComputeByrefLayout);

  /// GetIvarLayoutName - Returns a unique constant for the given
  /// ivar layout bitmap.
  llvm::Constant *GetIvarLayoutName(IdentifierInfo *Ident,
                                    const ObjCCommonTypesHelper &ObjCTypes);

  /// EmitPropertyList - Emit the given property list. The return
  /// value has type PropertyListPtrTy.
  llvm::Constant *EmitPropertyList(Twine Name,
                                   const Decl *Container,
                                   const ObjCContainerDecl *OCD,
                                   const ObjCCommonTypesHelper &ObjCTypes,
                                   bool IsClassProperty);

  /// EmitProtocolMethodTypes - Generate the array of extended method type
  /// strings. The return value has type Int8PtrPtrTy.
  llvm::Constant *EmitProtocolMethodTypes(Twine Name,
                                          ArrayRef<llvm::Constant*> MethodTypes,
                                       const ObjCCommonTypesHelper &ObjCTypes);

  /// GetProtocolRef - Return a reference to the internal protocol
  /// description, creating an empty one if it has not been
  /// defined. The return value has type ProtocolPtrTy.
  llvm::Constant *GetProtocolRef(const ObjCProtocolDecl *PD);

  /// Return a reference to the given Class using runtime calls rather than
  /// by a symbol reference.
  llvm::Value *EmitClassRefViaRuntime(CodeGenFunction &CGF,
                                      const ObjCInterfaceDecl *ID,
                                      ObjCCommonTypesHelper &ObjCTypes);

  std::string GetSectionName(StringRef Section, StringRef MachOAttributes);

public:
  /// CreateMetadataVar - Create a global variable with internal
  /// linkage for use by the Objective-C runtime.
  ///
  /// This is a convenience wrapper which not only creates the
  /// variable, but also sets the section and alignment and adds the
  /// global to the "llvm.used" list.
  ///
  /// \param Name - The variable name.
  /// \param Init - The variable initializer; this is also used to
  ///   define the type of the variable.
  /// \param Section - The section the variable should go into, or empty.
  /// \param Align - The alignment for the variable, or 0.
  /// \param AddToUsed - Whether the variable should be added to
  ///   "llvm.used".
  llvm::GlobalVariable *CreateMetadataVar(Twine Name,
                                          ConstantStructBuilder &Init,
                                          StringRef Section, CharUnits Align,
                                          bool AddToUsed);
  llvm::GlobalVariable *CreateMetadataVar(Twine Name,
                                          llvm::Constant *Init,
                                          StringRef Section, CharUnits Align,
                                          bool AddToUsed);

  llvm::GlobalVariable *CreateCStringLiteral(StringRef Name,
                                             ObjCLabelType LabelType,
                                             bool ForceNonFragileABI = false,
                                             bool NullTerminate = true);

protected:
  CodeGen::RValue EmitMessageSend(CodeGen::CodeGenFunction &CGF,
                                  ReturnValueSlot Return,
                                  QualType ResultType,
                                  Selector Sel,
                                  llvm::Value *Arg0,
                                  QualType Arg0Ty,
                                  bool IsSuper,
                                  const CallArgList &CallArgs,
                                  const ObjCMethodDecl *OMD,
                                  const ObjCInterfaceDecl *ClassReceiver,
                                  const ObjCCommonTypesHelper &ObjCTypes);

  /// EmitImageInfo - Emit the image info marker used to encode some module
  /// level information.
  void EmitImageInfo();

public:
  CGObjCCommonMac(CodeGen::CodeGenModule &cgm)
      : CGObjCRuntime(cgm), VMContext(cgm.getLLVMContext()) {}

  bool isNonFragileABI() const {
    return ObjCABI == 2;
  }

  ConstantAddress GenerateConstantString(const StringLiteral *SL) override;
  ConstantAddress GenerateConstantNSString(const StringLiteral *SL);

  llvm::Function *GenerateMethod(const ObjCMethodDecl *OMD,
                                 const ObjCContainerDecl *CD=nullptr) override;

  llvm::Function *GenerateDirectMethod(const ObjCMethodDecl *OMD,
                                       const ObjCContainerDecl *CD);

  void GenerateDirectMethodPrologue(CodeGenFunction &CGF, llvm::Function *Fn,
                                    const ObjCMethodDecl *OMD,
                                    const ObjCContainerDecl *CD) override;

  void GenerateProtocol(const ObjCProtocolDecl *PD) override;

  /// GetOrEmitProtocolRef - Get a forward reference to the protocol
  /// object for the given declaration, emitting it if needed. These
  /// forward references will be filled in with empty bodies if no
  /// definition is seen. The return value has type ProtocolPtrTy.
  virtual llvm::Constant *GetOrEmitProtocolRef(const ObjCProtocolDecl *PD)=0;

  virtual llvm::Constant *getNSConstantStringClassRef() = 0;

  llvm::Constant *BuildGCBlockLayout(CodeGen::CodeGenModule &CGM,
                                     const CGBlockInfo &blockInfo) override;
  llvm::Constant *BuildRCBlockLayout(CodeGen::CodeGenModule &CGM,
                                     const CGBlockInfo &blockInfo) override;
  std::string getRCBlockLayoutStr(CodeGen::CodeGenModule &CGM,
                                  const CGBlockInfo &blockInfo) override;

  llvm::Constant *BuildByrefLayout(CodeGen::CodeGenModule &CGM,
                                   QualType T) override;

private:
  void fillRunSkipBlockVars(CodeGenModule &CGM, const CGBlockInfo &blockInfo);
};

namespace {

enum class MethodListType {
  CategoryInstanceMethods,
  CategoryClassMethods,
  InstanceMethods,
  ClassMethods,
  ProtocolInstanceMethods,
  ProtocolClassMethods,
  OptionalProtocolInstanceMethods,
  OptionalProtocolClassMethods,
};

/// A convenience class for splitting the methods of a protocol into
/// the four interesting groups.
class ProtocolMethodLists {
public:
  enum Kind {
    RequiredInstanceMethods,
    RequiredClassMethods,
    OptionalInstanceMethods,
    OptionalClassMethods
  };
  enum {
    NumProtocolMethodLists = 4
  };

  static MethodListType getMethodListKind(Kind kind) {
    switch (kind) {
    case RequiredInstanceMethods:
      return MethodListType::ProtocolInstanceMethods;
    case RequiredClassMethods:
      return MethodListType::ProtocolClassMethods;
    case OptionalInstanceMethods:
      return MethodListType::OptionalProtocolInstanceMethods;
    case OptionalClassMethods:
      return MethodListType::OptionalProtocolClassMethods;
    }
    llvm_unreachable("bad kind");
  }

  SmallVector<const ObjCMethodDecl *, 4> Methods[NumProtocolMethodLists];

  static ProtocolMethodLists get(const ObjCProtocolDecl *PD) {
    ProtocolMethodLists result;

    for (auto *MD : PD->methods()) {
      size_t index = (2 * size_t(MD->isOptional()))
                   + (size_t(MD->isClassMethod()));
      result.Methods[index].push_back(MD);
    }

    return result;
  }

  template <class Self>
  SmallVector<llvm::Constant*, 8> emitExtendedTypesArray(Self *self) const {
    // In both ABIs, the method types list is parallel with the
    // concatenation of the methods arrays in the following order:
    //   instance methods
    //   class methods
    //   optional instance methods
    //   optional class methods
    SmallVector<llvm::Constant*, 8> result;

    // Methods is already in the correct order for both ABIs.
    for (auto &list : Methods) {
      for (auto MD : list) {
        result.push_back(self->GetMethodVarType(MD, true));
      }
    }

    return result;
  }

  template <class Self>
  llvm::Constant *emitMethodList(Self *self, const ObjCProtocolDecl *PD,
                                 Kind kind) const {
    return self->emitMethodList(PD->getObjCRuntimeNameAsString(),
                                getMethodListKind(kind), Methods[kind]);
  }
};

} // end anonymous namespace

class CGObjCMac : public CGObjCCommonMac {
private:
  friend ProtocolMethodLists;

  ObjCTypesHelper ObjCTypes;

  /// EmitModuleInfo - Another marker encoding module level
  /// information.
  void EmitModuleInfo();

  /// EmitModuleSymols - Emit module symbols, the list of defined
  /// classes and categories. The result has type SymtabPtrTy.
  llvm::Constant *EmitModuleSymbols();

  /// FinishModule - Write out global data structures at the end of
  /// processing a translation unit.
  void FinishModule();

  /// EmitClassExtension - Generate the class extension structure used
  /// to store the weak ivar layout and properties. The return value
  /// has type ClassExtensionPtrTy.
  llvm::Constant *EmitClassExtension(const ObjCImplementationDecl *ID,
                                     CharUnits instanceSize,
                                     bool hasMRCWeakIvars,
                                     bool isMetaclass);

  /// EmitClassRef - Return a Value*, of type ObjCTypes.ClassPtrTy,
  /// for the given class.
  llvm::Value *EmitClassRef(CodeGenFunction &CGF,
                            const ObjCInterfaceDecl *ID);

  llvm::Value *EmitClassRefFromId(CodeGenFunction &CGF,
                                  IdentifierInfo *II);

  llvm::Value *EmitNSAutoreleasePoolClassRef(CodeGenFunction &CGF) override;

  /// EmitSuperClassRef - Emits reference to class's main metadata class.
  llvm::Value *EmitSuperClassRef(const ObjCInterfaceDecl *ID);

  /// EmitIvarList - Emit the ivar list for the given
  /// implementation. If ForClass is true the list of class ivars
  /// (i.e. metaclass ivars) is emitted, otherwise the list of
  /// interface ivars will be emitted. The return value has type
  /// IvarListPtrTy.
  llvm::Constant *EmitIvarList(const ObjCImplementationDecl *ID,
                               bool ForClass);

  /// EmitMetaClass - Emit a forward reference to the class structure
  /// for the metaclass of the given interface. The return value has
  /// type ClassPtrTy.
  llvm::Constant *EmitMetaClassRef(const ObjCInterfaceDecl *ID);

  /// EmitMetaClass - Emit a class structure for the metaclass of the
  /// given implementation. The return value has type ClassPtrTy.
  llvm::Constant *EmitMetaClass(const ObjCImplementationDecl *ID,
                                llvm::Constant *Protocols,
                                ArrayRef<const ObjCMethodDecl *> Methods);

  void emitMethodConstant(ConstantArrayBuilder &builder,
                          const ObjCMethodDecl *MD);

  void emitMethodDescriptionConstant(ConstantArrayBuilder &builder,
                                     const ObjCMethodDecl *MD);

  /// EmitMethodList - Emit the method list for the given
  /// implementation. The return value has type MethodListPtrTy.
  llvm::Constant *emitMethodList(Twine Name, MethodListType MLT,
                                 ArrayRef<const ObjCMethodDecl *> Methods);

  /// GetOrEmitProtocol - Get the protocol object for the given
  /// declaration, emitting it if necessary. The return value has type
  /// ProtocolPtrTy.
  llvm::Constant *GetOrEmitProtocol(const ObjCProtocolDecl *PD) override;

  /// GetOrEmitProtocolRef - Get a forward reference to the protocol
  /// object for the given declaration, emitting it if needed. These
  /// forward references will be filled in with empty bodies if no
  /// definition is seen. The return value has type ProtocolPtrTy.
  llvm::Constant *GetOrEmitProtocolRef(const ObjCProtocolDecl *PD) override;

  /// EmitProtocolExtension - Generate the protocol extension
  /// structure used to store optional instance and class methods, and
  /// protocol properties. The return value has type
  /// ProtocolExtensionPtrTy.
  llvm::Constant *
  EmitProtocolExtension(const ObjCProtocolDecl *PD,
                        const ProtocolMethodLists &methodLists);

  /// EmitProtocolList - Generate the list of referenced
  /// protocols. The return value has type ProtocolListPtrTy.
  llvm::Constant *EmitProtocolList(Twine Name,
                                   ObjCProtocolDecl::protocol_iterator begin,
                                   ObjCProtocolDecl::protocol_iterator end);

  /// EmitSelector - Return a Value*, of type ObjCTypes.SelectorPtrTy,
  /// for the given selector.
  llvm::Value *EmitSelector(CodeGenFunction &CGF, Selector Sel);
  ConstantAddress EmitSelectorAddr(Selector Sel);

public:
  CGObjCMac(CodeGen::CodeGenModule &cgm);

  llvm::Constant *getNSConstantStringClassRef() override;

  llvm::Function *ModuleInitFunction() override;

  CodeGen::RValue GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                                      ReturnValueSlot Return,
                                      QualType ResultType,
                                      Selector Sel, llvm::Value *Receiver,
                                      const CallArgList &CallArgs,
                                      const ObjCInterfaceDecl *Class,
                                      const ObjCMethodDecl *Method) override;

  CodeGen::RValue
  GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                           ReturnValueSlot Return, QualType ResultType,
                           Selector Sel, const ObjCInterfaceDecl *Class,
                           bool isCategoryImpl, llvm::Value *Receiver,
                           bool IsClassMessage, const CallArgList &CallArgs,
                           const ObjCMethodDecl *Method) override;

  llvm::Value *GetClass(CodeGenFunction &CGF,
                        const ObjCInterfaceDecl *ID) override;

  llvm::Value *GetSelector(CodeGenFunction &CGF, Selector Sel) override;
  Address GetAddrOfSelector(CodeGenFunction &CGF, Selector Sel) override;

  /// The NeXT/Apple runtimes do not support typed selectors; just emit an
  /// untyped one.
  llvm::Value *GetSelector(CodeGenFunction &CGF,
                           const ObjCMethodDecl *Method) override;

  llvm::Constant *GetEHType(QualType T) override;

  void GenerateCategory(const ObjCCategoryImplDecl *CMD) override;

  void GenerateClass(const ObjCImplementationDecl *ClassDecl) override;

  void RegisterAlias(const ObjCCompatibleAliasDecl *OAD) override {}

  llvm::Value *GenerateProtocolRef(CodeGenFunction &CGF,
                                   const ObjCProtocolDecl *PD) override;

  llvm::FunctionCallee GetPropertyGetFunction() override;
  llvm::FunctionCallee GetPropertySetFunction() override;
  llvm::FunctionCallee GetOptimizedPropertySetFunction(bool atomic,
                                                       bool copy) override;
  llvm::FunctionCallee GetGetStructFunction() override;
  llvm::FunctionCallee GetSetStructFunction() override;
  llvm::FunctionCallee GetCppAtomicObjectGetFunction() override;
  llvm::FunctionCallee GetCppAtomicObjectSetFunction() override;
  llvm::FunctionCallee EnumerationMutationFunction() override;

  void EmitTryStmt(CodeGen::CodeGenFunction &CGF,
                   const ObjCAtTryStmt &S) override;
  void EmitSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                            const ObjCAtSynchronizedStmt &S) override;
  void EmitTryOrSynchronizedStmt(CodeGen::CodeGenFunction &CGF, const Stmt &S);
  void EmitThrowStmt(CodeGen::CodeGenFunction &CGF, const ObjCAtThrowStmt &S,
                     bool ClearInsertionPoint=true) override;
  llvm::Value * EmitObjCWeakRead(CodeGen::CodeGenFunction &CGF,
                                 Address AddrWeakObj) override;
  void EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                          llvm::Value *src, Address dst) override;
  void EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                            llvm::Value *src, Address dest,
                            bool threadlocal = false) override;
  void EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                          llvm::Value *src, Address dest,
                          llvm::Value *ivarOffset) override;
  void EmitObjCStrongCastAssign(CodeGen::CodeGenFunction &CGF,
                                llvm::Value *src, Address dest) override;
  void EmitGCMemmoveCollectable(CodeGen::CodeGenFunction &CGF,
                                Address dest, Address src,
                                llvm::Value *size) override;

  LValue EmitObjCValueForIvar(CodeGen::CodeGenFunction &CGF, QualType ObjectTy,
                              llvm::Value *BaseValue, const ObjCIvarDecl *Ivar,
                              unsigned CVRQualifiers) override;
  llvm::Value *EmitIvarOffset(CodeGen::CodeGenFunction &CGF,
                              const ObjCInterfaceDecl *Interface,
                              const ObjCIvarDecl *Ivar) override;
};

class CGObjCNonFragileABIMac : public CGObjCCommonMac {
private:
  friend ProtocolMethodLists;
  ObjCNonFragileABITypesHelper ObjCTypes;
  llvm::GlobalVariable* ObjCEmptyCacheVar;
  llvm::Constant* ObjCEmptyVtableVar;

  /// SuperClassReferences - uniqued super class references.
  llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> SuperClassReferences;

  /// MetaClassReferences - uniqued meta class references.
  llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> MetaClassReferences;

  /// EHTypeReferences - uniqued class ehtype references.
  llvm::DenseMap<IdentifierInfo*, llvm::GlobalVariable*> EHTypeReferences;

  /// VTableDispatchMethods - List of methods for which we generate
  /// vtable-based message dispatch.
  llvm::DenseSet<Selector> VTableDispatchMethods;

  /// DefinedMetaClasses - List of defined meta-classes.
  std::vector<llvm::GlobalValue*> DefinedMetaClasses;

  /// isVTableDispatchedSelector - Returns true if SEL is a
  /// vtable-based selector.
  bool isVTableDispatchedSelector(Selector Sel);

  /// FinishNonFragileABIModule - Write out global data structures at the end of
  /// processing a translation unit.
  void FinishNonFragileABIModule();

  /// AddModuleClassList - Add the given list of class pointers to the
  /// module with the provided symbol and section names.
  void AddModuleClassList(ArrayRef<llvm::GlobalValue *> Container,
                          StringRef SymbolName, StringRef SectionName);

  llvm::GlobalVariable * BuildClassRoTInitializer(unsigned flags,
                                              unsigned InstanceStart,
                                              unsigned InstanceSize,
                                              const ObjCImplementationDecl *ID);
  llvm::GlobalVariable *BuildClassObject(const ObjCInterfaceDecl *CI,
                                         bool isMetaclass,
                                         llvm::Constant *IsAGV,
                                         llvm::Constant *SuperClassGV,
                                         llvm::Constant *ClassRoGV,
                                         bool HiddenVisibility);

  void emitMethodConstant(ConstantArrayBuilder &builder,
                            const ObjCMethodDecl *MD,
                            bool forProtocol);

  /// Emit the method list for the given implementation. The return value
  /// has type MethodListnfABITy.
  llvm::Constant *emitMethodList(Twine Name, MethodListType MLT,
                                 ArrayRef<const ObjCMethodDecl *> Methods);

  /// EmitIvarList - Emit the ivar list for the given
  /// implementation. If ForClass is true the list of class ivars
  /// (i.e. metaclass ivars) is emitted, otherwise the list of
  /// interface ivars will be emitted. The return value has type
  /// IvarListnfABIPtrTy.
  llvm::Constant *EmitIvarList(const ObjCImplementationDecl *ID);

  llvm::Constant *EmitIvarOffsetVar(const ObjCInterfaceDecl *ID,
                                    const ObjCIvarDecl *Ivar,
                                    unsigned long int offset);

  /// GetOrEmitProtocol - Get the protocol object for the given
  /// declaration, emitting it if necessary. The return value has type
  /// ProtocolPtrTy.
  llvm::Constant *GetOrEmitProtocol(const ObjCProtocolDecl *PD) override;

  /// GetOrEmitProtocolRef - Get a forward reference to the protocol
  /// object for the given declaration, emitting it if needed. These
  /// forward references will be filled in with empty bodies if no
  /// definition is seen. The return value has type ProtocolPtrTy.
  llvm::Constant *GetOrEmitProtocolRef(const ObjCProtocolDecl *PD) override;

  /// EmitProtocolList - Generate the list of referenced
  /// protocols. The return value has type ProtocolListPtrTy.
  llvm::Constant *EmitProtocolList(Twine Name,
                                   ObjCProtocolDecl::protocol_iterator begin,
                                   ObjCProtocolDecl::protocol_iterator end);

  CodeGen::RValue EmitVTableMessageSend(CodeGen::CodeGenFunction &CGF,
                                        ReturnValueSlot Return,
                                        QualType ResultType,
                                        Selector Sel,
                                        llvm::Value *Receiver,
                                        QualType Arg0Ty,
                                        bool IsSuper,
                                        const CallArgList &CallArgs,
                                        const ObjCMethodDecl *Method);

  /// GetClassGlobal - Return the global variable for the Objective-C
  /// class of the given name.
  llvm::Constant *GetClassGlobal(StringRef Name,
                                 ForDefinition_t IsForDefinition,
                                 bool Weak = false, bool DLLImport = false);
  llvm::Constant *GetClassGlobal(const ObjCInterfaceDecl *ID,
                                 bool isMetaclass,
                                 ForDefinition_t isForDefinition);

  llvm::Constant *GetClassGlobalForClassRef(const ObjCInterfaceDecl *ID);

  llvm::Value *EmitLoadOfClassRef(CodeGenFunction &CGF,
                                  const ObjCInterfaceDecl *ID,
                                  llvm::GlobalVariable *Entry);

  /// EmitClassRef - Return a Value*, of type ObjCTypes.ClassPtrTy,
  /// for the given class reference.
  llvm::Value *EmitClassRef(CodeGenFunction &CGF,
                            const ObjCInterfaceDecl *ID);

  llvm::Value *EmitClassRefFromId(CodeGenFunction &CGF,
                                  IdentifierInfo *II,
                                  const ObjCInterfaceDecl *ID);

  llvm::Value *EmitNSAutoreleasePoolClassRef(CodeGenFunction &CGF) override;

  /// EmitSuperClassRef - Return a Value*, of type ObjCTypes.ClassPtrTy,
  /// for the given super class reference.
  llvm::Value *EmitSuperClassRef(CodeGenFunction &CGF,
                                 const ObjCInterfaceDecl *ID);

  /// EmitMetaClassRef - Return a Value * of the address of _class_t
  /// meta-data
  llvm::Value *EmitMetaClassRef(CodeGenFunction &CGF,
                                const ObjCInterfaceDecl *ID, bool Weak);

  /// ObjCIvarOffsetVariable - Returns the ivar offset variable for
  /// the given ivar.
  ///
  llvm::GlobalVariable * ObjCIvarOffsetVariable(
    const ObjCInterfaceDecl *ID,
    const ObjCIvarDecl *Ivar);

  /// EmitSelector - Return a Value*, of type ObjCTypes.SelectorPtrTy,
  /// for the given selector.
  llvm::Value *EmitSelector(CodeGenFunction &CGF, Selector Sel);
  ConstantAddress EmitSelectorAddr(Selector Sel);

  /// GetInterfaceEHType - Get the cached ehtype for the given Objective-C
  /// interface. The return value has type EHTypePtrTy.
  llvm::Constant *GetInterfaceEHType(const ObjCInterfaceDecl *ID,
                                     ForDefinition_t IsForDefinition);

  StringRef getMetaclassSymbolPrefix() const { return "OBJC_METACLASS_$_"; }

  StringRef getClassSymbolPrefix() const { return "OBJC_CLASS_$_"; }

  void GetClassSizeInfo(const ObjCImplementationDecl *OID,
                        uint32_t &InstanceStart,
                        uint32_t &InstanceSize);

  // Shamelessly stolen from Analysis/CFRefCount.cpp
  Selector GetNullarySelector(const char* name) const {
    const IdentifierInfo *II = &CGM.getContext().Idents.get(name);
    return CGM.getContext().Selectors.getSelector(0, &II);
  }

  Selector GetUnarySelector(const char* name) const {
    const IdentifierInfo *II = &CGM.getContext().Idents.get(name);
    return CGM.getContext().Selectors.getSelector(1, &II);
  }

  /// ImplementationIsNonLazy - Check whether the given category or
  /// class implementation is "non-lazy".
  bool ImplementationIsNonLazy(const ObjCImplDecl *OD) const;

  bool IsIvarOffsetKnownIdempotent(const CodeGen::CodeGenFunction &CGF,
                                   const ObjCIvarDecl *IV) {
    // Annotate the load as an invariant load iff inside an instance method
    // and ivar belongs to instance method's class and one of its super class.
    // This check is needed because the ivar offset is a lazily
    // initialised value that may depend on objc_msgSend to perform a fixup on
    // the first message dispatch.
    //
    // An additional opportunity to mark the load as invariant arises when the
    // base of the ivar access is a parameter to an Objective C method.
    // However, because the parameters are not available in the current
    // interface, we cannot perform this check.
    //
    // Note that for direct methods, because objc_msgSend is skipped,
    // and that the method may be inlined, this optimization actually
    // can't be performed.
    if (const ObjCMethodDecl *MD =
          dyn_cast_or_null<ObjCMethodDecl>(CGF.CurFuncDecl))
      if (MD->isInstanceMethod() && !MD->isDirectMethod())
        if (const ObjCInterfaceDecl *ID = MD->getClassInterface())
          return IV->getContainingInterface()->isSuperClassOf(ID);
    return false;
  }

  bool isClassLayoutKnownStatically(const ObjCInterfaceDecl *ID) {
    // Test a class by checking its superclasses up to
    // its base class if it has one.
    for (; ID; ID = ID->getSuperClass()) {
      // The layout of base class NSObject
      // is guaranteed to be statically known
      if (ID->getIdentifier()->getName() == "NSObject")
        return true;

      // If we cannot see the @implementation of a class,
      // we cannot statically know the class layout.
      if (!ID->getImplementation())
        return false;
    }
    return false;
  }

public:
  CGObjCNonFragileABIMac(CodeGen::CodeGenModule &cgm);

  llvm::Constant *getNSConstantStringClassRef() override;

  llvm::Function *ModuleInitFunction() override;

  CodeGen::RValue GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                                      ReturnValueSlot Return,
                                      QualType ResultType, Selector Sel,
                                      llvm::Value *Receiver,
                                      const CallArgList &CallArgs,
                                      const ObjCInterfaceDecl *Class,
                                      const ObjCMethodDecl *Method) override;

  CodeGen::RValue
  GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                           ReturnValueSlot Return, QualType ResultType,
                           Selector Sel, const ObjCInterfaceDecl *Class,
                           bool isCategoryImpl, llvm::Value *Receiver,
                           bool IsClassMessage, const CallArgList &CallArgs,
                           const ObjCMethodDecl *Method) override;

  llvm::Value *GetClass(CodeGenFunction &CGF,
                        const ObjCInterfaceDecl *ID) override;

  llvm::Value *GetSelector(CodeGenFunction &CGF, Selector Sel) override
    { return EmitSelector(CGF, Sel); }
  Address GetAddrOfSelector(CodeGenFunction &CGF, Selector Sel) override
    { return EmitSelectorAddr(Sel); }

  /// The NeXT/Apple runtimes do not support typed selectors; just emit an
  /// untyped one.
  llvm::Value *GetSelector(CodeGenFunction &CGF,
                           const ObjCMethodDecl *Method) override
    { return EmitSelector(CGF, Method->getSelector()); }

  void GenerateCategory(const ObjCCategoryImplDecl *CMD) override;

  void GenerateClass(const ObjCImplementationDecl *ClassDecl) override;

  void RegisterAlias(const ObjCCompatibleAliasDecl *OAD) override {}

  llvm::Value *GenerateProtocolRef(CodeGenFunction &CGF,
                                   const ObjCProtocolDecl *PD) override;

  llvm::Constant *GetEHType(QualType T) override;

  llvm::FunctionCallee GetPropertyGetFunction() override {
    return ObjCTypes.getGetPropertyFn();
  }
  llvm::FunctionCallee GetPropertySetFunction() override {
    return ObjCTypes.getSetPropertyFn();
  }

  llvm::FunctionCallee GetOptimizedPropertySetFunction(bool atomic,
                                                       bool copy) override {
    return ObjCTypes.getOptimizedSetPropertyFn(atomic, copy);
  }

  llvm::FunctionCallee GetSetStructFunction() override {
    return ObjCTypes.getCopyStructFn();
  }

  llvm::FunctionCallee GetGetStructFunction() override {
    return ObjCTypes.getCopyStructFn();
  }

  llvm::FunctionCallee GetCppAtomicObjectSetFunction() override {
    return ObjCTypes.getCppAtomicObjectFunction();
  }

  llvm::FunctionCallee GetCppAtomicObjectGetFunction() override {
    return ObjCTypes.getCppAtomicObjectFunction();
  }

  llvm::FunctionCallee EnumerationMutationFunction() override {
    return ObjCTypes.getEnumerationMutationFn();
  }

  void EmitTryStmt(CodeGen::CodeGenFunction &CGF,
                   const ObjCAtTryStmt &S) override;
  void EmitSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                            const ObjCAtSynchronizedStmt &S) override;
  void EmitThrowStmt(CodeGen::CodeGenFunction &CGF, const ObjCAtThrowStmt &S,
                     bool ClearInsertionPoint=true) override;
  llvm::Value * EmitObjCWeakRead(CodeGen::CodeGenFunction &CGF,
                                 Address AddrWeakObj) override;
  void EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                          llvm::Value *src, Address edst) override;
  void EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                            llvm::Value *src, Address dest,
                            bool threadlocal = false) override;
  void EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                          llvm::Value *src, Address dest,
                          llvm::Value *ivarOffset) override;
  void EmitObjCStrongCastAssign(CodeGen::CodeGenFunction &CGF,
                                llvm::Value *src, Address dest) override;
  void EmitGCMemmoveCollectable(CodeGen::CodeGenFunction &CGF,
                                Address dest, Address src,
                                llvm::Value *size) override;
  LValue EmitObjCValueForIvar(CodeGen::CodeGenFunction &CGF, QualType ObjectTy,
                              llvm::Value *BaseValue, const ObjCIvarDecl *Ivar,
                              unsigned CVRQualifiers) override;
  llvm::Value *EmitIvarOffset(CodeGen::CodeGenFunction &CGF,
                              const ObjCInterfaceDecl *Interface,
                              const ObjCIvarDecl *Ivar) override;
};

/// A helper class for performing the null-initialization of a return
/// value.
struct NullReturnState {
  llvm::BasicBlock *NullBB = nullptr;
  NullReturnState() = default;

  /// Perform a null-check of the given receiver.
  void init(CodeGenFunction &CGF, llvm::Value *receiver) {
    // Make blocks for the null-receiver and call edges.
    NullBB = CGF.createBasicBlock("msgSend.null-receiver");
    llvm::BasicBlock *callBB = CGF.createBasicBlock("msgSend.call");

    // Check for a null receiver and, if there is one, jump to the
    // null-receiver block.  There's no point in trying to avoid it:
    // we're always going to put *something* there, because otherwise
    // we shouldn't have done this null-check in the first place.
    llvm::Value *isNull = CGF.Builder.CreateIsNull(receiver);
    CGF.Builder.CreateCondBr(isNull, NullBB, callBB);

    // Otherwise, start performing the call.
    CGF.EmitBlock(callBB);
  }

  /// Complete the null-return operation.  It is valid to call this
  /// regardless of whether 'init' has been called.
  RValue complete(CodeGenFunction &CGF,
                  ReturnValueSlot returnSlot,
                  RValue result,
                  QualType resultType,
                  const CallArgList &CallArgs,
                  const ObjCMethodDecl *Method) {
    // If we never had to do a null-check, just use the raw result.
    if (!NullBB) return result;

    // The continuation block.  This will be left null if we don't have an
    // IP, which can happen if the method we're calling is marked noreturn.
    llvm::BasicBlock *contBB = nullptr;

    // Finish the call path.
    llvm::BasicBlock *callBB = CGF.Builder.GetInsertBlock();
    if (callBB) {
      contBB = CGF.createBasicBlock("msgSend.cont");
      CGF.Builder.CreateBr(contBB);
    }

    // Okay, start emitting the null-receiver block.
    CGF.EmitBlock(NullBB);

    // Destroy any consumed arguments we've got.
    if (Method) {
      CGObjCRuntime::destroyCalleeDestroyedArguments(CGF, Method, CallArgs);
    }

    // The phi code below assumes that we haven't needed any control flow yet.
    assert(CGF.Builder.GetInsertBlock() == NullBB);

    // If we've got a void return, just jump to the continuation block.
    if (result.isScalar() && resultType->isVoidType()) {
      // No jumps required if the message-send was noreturn.
      if (contBB) CGF.EmitBlock(contBB);
      return result;
    }

    // If we've got a scalar return, build a phi.
    if (result.isScalar()) {
      // Derive the null-initialization value.
      llvm::Value *null =
          CGF.EmitFromMemory(CGF.CGM.EmitNullConstant(resultType), resultType);

      // If no join is necessary, just flow out.
      if (!contBB) return RValue::get(null);

      // Otherwise, build a phi.
      CGF.EmitBlock(contBB);
      llvm::PHINode *phi = CGF.Builder.CreatePHI(null->getType(), 2);
      phi->addIncoming(result.getScalarVal(), callBB);
      phi->addIncoming(null, NullBB);
      return RValue::get(phi);
    }

    // If we've got an aggregate return, null the buffer out.
    // FIXME: maybe we should be doing things differently for all the
    // cases where the ABI has us returning (1) non-agg values in
    // memory or (2) agg values in registers.
    if (result.isAggregate()) {
      assert(result.isAggregate() && "null init of non-aggregate result?");
      if (!returnSlot.isUnused())
        CGF.EmitNullInitialization(result.getAggregateAddress(), resultType);
      if (contBB) CGF.EmitBlock(contBB);
      return result;
    }

    // Complex types.
    CGF.EmitBlock(contBB);
    CodeGenFunction::ComplexPairTy callResult = result.getComplexVal();

    // Find the scalar type and its zero value.
    llvm::Type *scalarTy = callResult.first->getType();
    llvm::Constant *scalarZero = llvm::Constant::getNullValue(scalarTy);

    // Build phis for both coordinates.
    llvm::PHINode *real = CGF.Builder.CreatePHI(scalarTy, 2);
    real->addIncoming(callResult.first, callBB);
    real->addIncoming(scalarZero, NullBB);
    llvm::PHINode *imag = CGF.Builder.CreatePHI(scalarTy, 2);
    imag->addIncoming(callResult.second, callBB);
    imag->addIncoming(scalarZero, NullBB);
    return RValue::getComplex(real, imag);
  }
};

} // end anonymous namespace

/* *** Helper Functions *** */

/// getConstantGEP() - Help routine to construct simple GEPs.
static llvm::Constant *getConstantGEP(llvm::LLVMContext &VMContext,
                                      llvm::GlobalVariable *C, unsigned idx0,
                                      unsigned idx1) {
  llvm::Value *Idxs[] = {
    llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), idx0),
    llvm::ConstantInt::get(llvm::Type::getInt32Ty(VMContext), idx1)
  };
  return llvm::ConstantExpr::getGetElementPtr(C->getValueType(), C, Idxs);
}

/// hasObjCExceptionAttribute - Return true if this class or any super
/// class has the __objc_exception__ attribute.
static bool hasObjCExceptionAttribute(ASTContext &Context,
                                      const ObjCInterfaceDecl *OID) {
  if (OID->hasAttr<ObjCExceptionAttr>())
    return true;
  if (const ObjCInterfaceDecl *Super = OID->getSuperClass())
    return hasObjCExceptionAttribute(Context, Super);
  return false;
}

static llvm::GlobalValue::LinkageTypes
getLinkageTypeForObjCMetadata(CodeGenModule &CGM, StringRef Section) {
  if (CGM.getTriple().isOSBinFormatMachO() &&
      (Section.empty() || Section.starts_with("__DATA")))
    return llvm::GlobalValue::InternalLinkage;
  return llvm::GlobalValue::PrivateLinkage;
}

/// A helper function to create an internal or private global variable.
static llvm::GlobalVariable *
finishAndCreateGlobal(ConstantInitBuilder::StructBuilder &Builder,
                     const llvm::Twine &Name, CodeGenModule &CGM) {
  std::string SectionName;
  if (CGM.getTriple().isOSBinFormatMachO())
    SectionName = "__DATA, __objc_const";
  auto *GV = Builder.finishAndCreateGlobal(
      Name, CGM.getPointerAlign(), /*constant*/ false,
      getLinkageTypeForObjCMetadata(CGM, SectionName));
  GV->setSection(SectionName);
  return GV;
}

/* *** CGObjCMac Public Interface *** */

CGObjCMac::CGObjCMac(CodeGen::CodeGenModule &cgm) : CGObjCCommonMac(cgm),
                                                    ObjCTypes(cgm) {
  ObjCABI = 1;
  EmitImageInfo();
}

/// GetClass - Return a reference to the class for the given interface
/// decl.
llvm::Value *CGObjCMac::GetClass(CodeGenFunction &CGF,
                                 const ObjCInterfaceDecl *ID) {
  return EmitClassRef(CGF, ID);
}

/// GetSelector - Return the pointer to the unique'd string for this selector.
llvm::Value *CGObjCMac::GetSelector(CodeGenFunction &CGF, Selector Sel) {
  return EmitSelector(CGF, Sel);
}
Address CGObjCMac::GetAddrOfSelector(CodeGenFunction &CGF, Selector Sel) {
  return EmitSelectorAddr(Sel);
}
llvm::Value *CGObjCMac::GetSelector(CodeGenFunction &CGF, const ObjCMethodDecl
                                    *Method) {
  return EmitSelector(CGF, Method->getSelector());
}

llvm::Constant *CGObjCMac::GetEHType(QualType T) {
  if (T->isObjCIdType() ||
      T->isObjCQualifiedIdType()) {
    return CGM.GetAddrOfRTTIDescriptor(
              CGM.getContext().getObjCIdRedefinitionType(), /*ForEH=*/true);
  }
  if (T->isObjCClassType() ||
      T->isObjCQualifiedClassType()) {
    return CGM.GetAddrOfRTTIDescriptor(
             CGM.getContext().getObjCClassRedefinitionType(), /*ForEH=*/true);
  }
  if (T->isObjCObjectPointerType())
    return CGM.GetAddrOfRTTIDescriptor(T,  /*ForEH=*/true);

  llvm_unreachable("asking for catch type for ObjC type in fragile runtime");
}

/// Generate a constant CFString object.
/*
  struct __builtin_CFString {
  const int *isa; // point to __CFConstantStringClassReference
  int flags;
  const char *str;
  long length;
  };
*/

/// or Generate a constant NSString object.
/*
   struct __builtin_NSString {
     const int *isa; // point to __NSConstantStringClassReference
     const char *str;
     unsigned int length;
   };
*/

ConstantAddress
CGObjCCommonMac::GenerateConstantString(const StringLiteral *SL) {
  return (!CGM.getLangOpts().NoConstantCFStrings
            ? CGM.GetAddrOfConstantCFString(SL)
            : GenerateConstantNSString(SL));
}

static llvm::StringMapEntry<llvm::GlobalVariable *> &
GetConstantStringEntry(llvm::StringMap<llvm::GlobalVariable *> &Map,
                       const StringLiteral *Literal, unsigned &StringLength) {
  StringRef String = Literal->getString();
  StringLength = String.size();
  return *Map.insert(std::make_pair(String, nullptr)).first;
}

llvm::Constant *CGObjCMac::getNSConstantStringClassRef() {
  if (llvm::Value *V = ConstantStringClassRef)
    return cast<llvm::Constant>(V);

  auto &StringClass = CGM.getLangOpts().ObjCConstantStringClass;
  std::string str =
    StringClass.empty() ? "_NSConstantStringClassReference"
                        : "_" + StringClass + "ClassReference";

  llvm::Type *PTy = llvm::ArrayType::get(CGM.IntTy, 0);
  auto GV = CGM.CreateRuntimeVariable(PTy, str);
  ConstantStringClassRef = GV;
  return GV;
}

llvm::Constant *CGObjCNonFragileABIMac::getNSConstantStringClassRef() {
  if (llvm::Value *V = ConstantStringClassRef)
    return cast<llvm::Constant>(V);

  auto &StringClass = CGM.getLangOpts().ObjCConstantStringClass;
  std::string str =
    StringClass.empty() ? "OBJC_CLASS_$_NSConstantString"
                        : "OBJC_CLASS_$_" + StringClass;
  llvm::Constant *GV = GetClassGlobal(str, NotForDefinition);
  ConstantStringClassRef = GV;
  return GV;
}

ConstantAddress
CGObjCCommonMac::GenerateConstantNSString(const StringLiteral *Literal) {
  unsigned StringLength = 0;
  llvm::StringMapEntry<llvm::GlobalVariable *> &Entry =
    GetConstantStringEntry(NSConstantStringMap, Literal, StringLength);

  if (auto *C = Entry.second)
    return ConstantAddress(
        C, C->getValueType(), CharUnits::fromQuantity(C->getAlignment()));

  // If we don't already have it, get _NSConstantStringClassReference.
  llvm::Constant *Class = getNSConstantStringClassRef();

  // If we don't already have it, construct the type for a constant NSString.
  if (!NSConstantStringType) {
    NSConstantStringType =
        llvm::StructType::create({CGM.UnqualPtrTy, CGM.Int8PtrTy, CGM.IntTy},
                                 "struct.__builtin_NSString");
  }

  ConstantInitBuilder Builder(CGM);
  auto Fields = Builder.beginStruct(NSConstantStringType);

  // Class pointer.
  Fields.add(Class);

  // String pointer.
  llvm::Constant *C =
    llvm::ConstantDataArray::getString(VMContext, Entry.first());

  llvm::GlobalValue::LinkageTypes Linkage = llvm::GlobalValue::PrivateLinkage;
  bool isConstant = !CGM.getLangOpts().WritableStrings;

  auto *GV = new llvm::GlobalVariable(CGM.getModule(), C->getType(), isConstant,
                                      Linkage, C, ".str");
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  // Don't enforce the target's minimum global alignment, since the only use
  // of the string is via this class initializer.
  GV->setAlignment(llvm::Align(1));
  Fields.add(GV);

  // String length.
  Fields.addInt(CGM.IntTy, StringLength);

  // The struct.
  CharUnits Alignment = CGM.getPointerAlign();
  GV = Fields.finishAndCreateGlobal("_unnamed_nsstring_", Alignment,
                                    /*constant*/ true,
                                    llvm::GlobalVariable::PrivateLinkage);
  const char *NSStringSection = "__OBJC,__cstring_object,regular,no_dead_strip";
  const char *NSStringNonFragileABISection =
      "__DATA,__objc_stringobj,regular,no_dead_strip";
  // FIXME. Fix section.
  GV->setSection(CGM.getLangOpts().ObjCRuntime.isNonFragile()
                     ? NSStringNonFragileABISection
                     : NSStringSection);
  Entry.second = GV;

  return ConstantAddress(GV, GV->getValueType(), Alignment);
}

enum {
  kCFTaggedObjectID_Integer = (1 << 1) + 1
};

/// Generates a message send where the super is the receiver.  This is
/// a message send to self with special delivery semantics indicating
/// which class's method should be called.
CodeGen::RValue
CGObjCMac::GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                                    ReturnValueSlot Return,
                                    QualType ResultType,
                                    Selector Sel,
                                    const ObjCInterfaceDecl *Class,
                                    bool isCategoryImpl,
                                    llvm::Value *Receiver,
                                    bool IsClassMessage,
                                    const CodeGen::CallArgList &CallArgs,
                                    const ObjCMethodDecl *Method) {
  // Create and init a super structure; this is a (receiver, class)
  // pair we will pass to objc_msgSendSuper.
  RawAddress ObjCSuper = CGF.CreateTempAlloca(
      ObjCTypes.SuperTy, CGF.getPointerAlign(), "objc_super");
  llvm::Value *ReceiverAsObject =
    CGF.Builder.CreateBitCast(Receiver, ObjCTypes.ObjectPtrTy);
  CGF.Builder.CreateStore(ReceiverAsObject,
                          CGF.Builder.CreateStructGEP(ObjCSuper, 0));

  // If this is a class message the metaclass is passed as the target.
  llvm::Type *ClassTyPtr = llvm::PointerType::getUnqual(ObjCTypes.ClassTy);
  llvm::Value *Target;
  if (IsClassMessage) {
    if (isCategoryImpl) {
      // Message sent to 'super' in a class method defined in a category
      // implementation requires an odd treatment.
      // If we are in a class method, we must retrieve the
      // _metaclass_ for the current class, pointed at by
      // the class's "isa" pointer.  The following assumes that
      // isa" is the first ivar in a class (which it must be).
      Target = EmitClassRef(CGF, Class->getSuperClass());
      Target = CGF.Builder.CreateStructGEP(ObjCTypes.ClassTy, Target, 0);
      Target = CGF.Builder.CreateAlignedLoad(ClassTyPtr, Target,
                                             CGF.getPointerAlign());
    } else {
      llvm::Constant *MetaClassPtr = EmitMetaClassRef(Class);
      llvm::Value *SuperPtr =
          CGF.Builder.CreateStructGEP(ObjCTypes.ClassTy, MetaClassPtr, 1);
      llvm::Value *Super = CGF.Builder.CreateAlignedLoad(ClassTyPtr, SuperPtr,
                                                         CGF.getPointerAlign());
      Target = Super;
    }
  } else if (isCategoryImpl)
    Target = EmitClassRef(CGF, Class->getSuperClass());
  else {
    llvm::Value *ClassPtr = EmitSuperClassRef(Class);
    ClassPtr = CGF.Builder.CreateStructGEP(ObjCTypes.ClassTy, ClassPtr, 1);
    Target = CGF.Builder.CreateAlignedLoad(ClassTyPtr, ClassPtr,
                                           CGF.getPointerAlign());
  }
  // FIXME: We shouldn't need to do this cast, rectify the ASTContext and
  // ObjCTypes types.
  llvm::Type *ClassTy =
    CGM.getTypes().ConvertType(CGF.getContext().getObjCClassType());
  Target = CGF.Builder.CreateBitCast(Target, ClassTy);
  CGF.Builder.CreateStore(Target, CGF.Builder.CreateStructGEP(ObjCSuper, 1));
  return EmitMessageSend(CGF, Return, ResultType, Sel, ObjCSuper.getPointer(),
                         ObjCTypes.SuperPtrCTy, true, CallArgs, Method, Class,
                         ObjCTypes);
}

/// Generate code for a message send expression.
CodeGen::RValue CGObjCMac::GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                                               ReturnValueSlot Return,
                                               QualType ResultType,
                                               Selector Sel,
                                               llvm::Value *Receiver,
                                               const CallArgList &CallArgs,
                                               const ObjCInterfaceDecl *Class,
                                               const ObjCMethodDecl *Method) {
  return EmitMessageSend(CGF, Return, ResultType, Sel, Receiver,
                         CGF.getContext().getObjCIdType(), false, CallArgs,
                         Method, Class, ObjCTypes);
}

CodeGen::RValue
CGObjCCommonMac::EmitMessageSend(CodeGen::CodeGenFunction &CGF,
                                 ReturnValueSlot Return,
                                 QualType ResultType,
                                 Selector Sel,
                                 llvm::Value *Arg0,
                                 QualType Arg0Ty,
                                 bool IsSuper,
                                 const CallArgList &CallArgs,
                                 const ObjCMethodDecl *Method,
                                 const ObjCInterfaceDecl *ClassReceiver,
                                 const ObjCCommonTypesHelper &ObjCTypes) {
  CodeGenTypes &Types = CGM.getTypes();
  auto selTy = CGF.getContext().getObjCSelType();
  llvm::Value *SelValue = llvm::UndefValue::get(Types.ConvertType(selTy));

  CallArgList ActualArgs;
  if (!IsSuper)
    Arg0 = CGF.Builder.CreateBitCast(Arg0, ObjCTypes.ObjectPtrTy);
  ActualArgs.add(RValue::get(Arg0), Arg0Ty);
  if (!Method || !Method->isDirectMethod())
    ActualArgs.add(RValue::get(SelValue), selTy);
  ActualArgs.addFrom(CallArgs);

  // If we're calling a method, use the formal signature.
  MessageSendInfo MSI = getMessageSendInfo(Method, ResultType, ActualArgs);

  if (Method)
    assert(CGM.getContext().getCanonicalType(Method->getReturnType()) ==
               CGM.getContext().getCanonicalType(ResultType) &&
           "Result type mismatch!");

  bool ReceiverCanBeNull =
    canMessageReceiverBeNull(CGF, Method, IsSuper, ClassReceiver, Arg0);

  bool RequiresNullCheck = false;
  bool RequiresSelValue = true;

  llvm::FunctionCallee Fn = nullptr;
  if (Method && Method->isDirectMethod()) {
    assert(!IsSuper);
    Fn = GenerateDirectMethod(Method, Method->getClassInterface());
    // Direct methods will synthesize the proper `_cmd` internally,
    // so just don't bother with setting the `_cmd` argument.
    RequiresSelValue = false;
  } else if (CGM.ReturnSlotInterferesWithArgs(MSI.CallInfo)) {
    if (ReceiverCanBeNull) RequiresNullCheck = true;
    Fn = (ObjCABI == 2) ?  ObjCTypes.getSendStretFn2(IsSuper)
      : ObjCTypes.getSendStretFn(IsSuper);
  } else if (CGM.ReturnTypeUsesFPRet(ResultType)) {
    Fn = (ObjCABI == 2) ? ObjCTypes.getSendFpretFn2(IsSuper)
      : ObjCTypes.getSendFpretFn(IsSuper);
  } else if (CGM.ReturnTypeUsesFP2Ret(ResultType)) {
    Fn = (ObjCABI == 2) ? ObjCTypes.getSendFp2RetFn2(IsSuper)
      : ObjCTypes.getSendFp2retFn(IsSuper);
  } else {
    // arm64 uses objc_msgSend for stret methods and yet null receiver check
    // must be made for it.
    if (ReceiverCanBeNull && CGM.ReturnTypeUsesSRet(MSI.CallInfo))
      RequiresNullCheck = true;
    Fn = (ObjCABI == 2) ? ObjCTypes.getSendFn2(IsSuper)
      : ObjCTypes.getSendFn(IsSuper);
  }

  // Cast function to proper signature
  llvm::Constant *BitcastFn = cast<llvm::Constant>(
      CGF.Builder.CreateBitCast(Fn.getCallee(), MSI.MessengerType));

  // We don't need to emit a null check to zero out an indirect result if the
  // result is ignored.
  if (Return.isUnused())
    RequiresNullCheck = false;

  // Emit a null-check if there's a consumed argument other than the receiver.
  if (!RequiresNullCheck && Method && Method->hasParamDestroyedInCallee())
    RequiresNullCheck = true;

  NullReturnState nullReturn;
  if (RequiresNullCheck) {
    nullReturn.init(CGF, Arg0);
  }

  // If a selector value needs to be passed, emit the load before the call.
  if (RequiresSelValue) {
    SelValue = GetSelector(CGF, Sel);
    ActualArgs[1] = CallArg(RValue::get(SelValue), selTy);
  }

  llvm::CallBase *CallSite;
  CGCallee Callee = CGCallee::forDirect(BitcastFn);
  RValue rvalue = CGF.EmitCall(MSI.CallInfo, Callee, Return, ActualArgs,
                               &CallSite);

  // Mark the call as noreturn if the method is marked noreturn and the
  // receiver cannot be null.
  if (Method && Method->hasAttr<NoReturnAttr>() && !ReceiverCanBeNull) {
    CallSite->setDoesNotReturn();
  }

  return nullReturn.complete(CGF, Return, rvalue, ResultType, CallArgs,
                             RequiresNullCheck ? Method : nullptr);
}

static Qualifiers::GC GetGCAttrTypeForType(ASTContext &Ctx, QualType FQT,
                                           bool pointee = false) {
  // Note that GC qualification applies recursively to C pointer types
  // that aren't otherwise decorated.  This is weird, but it's probably
  // an intentional workaround to the unreliable placement of GC qualifiers.
  if (FQT.isObjCGCStrong())
    return Qualifiers::Strong;

  if (FQT.isObjCGCWeak())
    return Qualifiers::Weak;

  if (auto ownership = FQT.getObjCLifetime()) {
    // Ownership does not apply recursively to C pointer types.
    if (pointee) return Qualifiers::GCNone;
    switch (ownership) {
    case Qualifiers::OCL_Weak: return Qualifiers::Weak;
    case Qualifiers::OCL_Strong: return Qualifiers::Strong;
    case Qualifiers::OCL_ExplicitNone: return Qualifiers::GCNone;
    case Qualifiers::OCL_Autoreleasing: llvm_unreachable("autoreleasing ivar?");
    case Qualifiers::OCL_None: llvm_unreachable("known nonzero");
    }
    llvm_unreachable("bad objc ownership");
  }

  // Treat unqualified retainable pointers as strong.
  if (FQT->isObjCObjectPointerType() || FQT->isBlockPointerType())
    return Qualifiers::Strong;

  // Walk into C pointer types, but only in GC.
  if (Ctx.getLangOpts().getGC() != LangOptions::NonGC) {
    if (const PointerType *PT = FQT->getAs<PointerType>())
      return GetGCAttrTypeForType(Ctx, PT->getPointeeType(), /*pointee*/ true);
  }

  return Qualifiers::GCNone;
}

namespace {
  struct IvarInfo {
    CharUnits Offset;
    uint64_t SizeInWords;
    IvarInfo(CharUnits offset, uint64_t sizeInWords)
      : Offset(offset), SizeInWords(sizeInWords) {}

    // Allow sorting based on byte pos.
    bool operator<(const IvarInfo &other) const {
      return Offset < other.Offset;
    }
  };

  /// A helper class for building GC layout strings.
  class IvarLayoutBuilder {
    CodeGenModule &CGM;

    /// The start of the layout.  Offsets will be relative to this value,
    /// and entries less than this value will be silently discarded.
    CharUnits InstanceBegin;

    /// The end of the layout.  Offsets will never exceed this value.
    CharUnits InstanceEnd;

    /// Whether we're generating the strong layout or the weak layout.
    bool ForStrongLayout;

    /// Whether the offsets in IvarsInfo might be out-of-order.
    bool IsDisordered = false;

    llvm::SmallVector<IvarInfo, 8> IvarsInfo;

  public:
    IvarLayoutBuilder(CodeGenModule &CGM, CharUnits instanceBegin,
                      CharUnits instanceEnd, bool forStrongLayout)
      : CGM(CGM), InstanceBegin(instanceBegin), InstanceEnd(instanceEnd),
        ForStrongLayout(forStrongLayout) {
    }

    void visitRecord(const RecordType *RT, CharUnits offset);

    template <class Iterator, class GetOffsetFn>
    void visitAggregate(Iterator begin, Iterator end,
                        CharUnits aggrOffset,
                        const GetOffsetFn &getOffset);

    void visitField(const FieldDecl *field, CharUnits offset);

    /// Add the layout of a block implementation.
    void visitBlock(const CGBlockInfo &blockInfo);

    /// Is there any information for an interesting bitmap?
    bool hasBitmapData() const { return !IvarsInfo.empty(); }

    llvm::Constant *buildBitmap(CGObjCCommonMac &CGObjC,
                                llvm::SmallVectorImpl<unsigned char> &buffer);

    static void dump(ArrayRef<unsigned char> buffer) {
      const unsigned char *s = buffer.data();
      for (unsigned i = 0, e = buffer.size(); i < e; i++)
        if (!(s[i] & 0xf0))
          printf("0x0%x%s", s[i], s[i] != 0 ? ", " : "");
        else
          printf("0x%x%s",  s[i], s[i] != 0 ? ", " : "");
      printf("\n");
    }
  };
} // end anonymous namespace

llvm::Constant *CGObjCCommonMac::BuildGCBlockLayout(CodeGenModule &CGM,
                                                const CGBlockInfo &blockInfo) {

  llvm::Constant *nullPtr = llvm::Constant::getNullValue(CGM.Int8PtrTy);
  if (CGM.getLangOpts().getGC() == LangOptions::NonGC)
    return nullPtr;

  IvarLayoutBuilder builder(CGM, CharUnits::Zero(), blockInfo.BlockSize,
                            /*for strong layout*/ true);

  builder.visitBlock(blockInfo);

  if (!builder.hasBitmapData())
    return nullPtr;

  llvm::SmallVector<unsigned char, 32> buffer;
  llvm::Constant *C = builder.buildBitmap(*this, buffer);
  if (CGM.getLangOpts().ObjCGCBitmapPrint && !buffer.empty()) {
    printf("\n block variable layout for block: ");
    builder.dump(buffer);
  }

  return C;
}

void IvarLayoutBuilder::visitBlock(const CGBlockInfo &blockInfo) {
  // __isa is the first field in block descriptor and must assume by runtime's
  // convention that it is GC'able.
  IvarsInfo.push_back(IvarInfo(CharUnits::Zero(), 1));

  const BlockDecl *blockDecl = blockInfo.getBlockDecl();

  // Ignore the optional 'this' capture: C++ objects are not assumed
  // to be GC'ed.

  CharUnits lastFieldOffset;

  // Walk the captured variables.
  for (const auto &CI : blockDecl->captures()) {
    const VarDecl *variable = CI.getVariable();
    QualType type = variable->getType();

    const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);

    // Ignore constant captures.
    if (capture.isConstant()) continue;

    CharUnits fieldOffset = capture.getOffset();

    // Block fields are not necessarily ordered; if we detect that we're
    // adding them out-of-order, make sure we sort later.
    if (fieldOffset < lastFieldOffset)
      IsDisordered = true;
    lastFieldOffset = fieldOffset;

    // __block variables are passed by their descriptor address.
    if (CI.isByRef()) {
      IvarsInfo.push_back(IvarInfo(fieldOffset, /*size in words*/ 1));
      continue;
    }

    assert(!type->isArrayType() && "array variable should not be caught");
    if (const RecordType *record = type->getAs<RecordType>()) {
      visitRecord(record, fieldOffset);
      continue;
    }

    Qualifiers::GC GCAttr = GetGCAttrTypeForType(CGM.getContext(), type);

    if (GCAttr == Qualifiers::Strong) {
      assert(CGM.getContext().getTypeSize(type) ==
             CGM.getTarget().getPointerWidth(LangAS::Default));
      IvarsInfo.push_back(IvarInfo(fieldOffset, /*size in words*/ 1));
    }
  }
}

/// getBlockCaptureLifetime - This routine returns life time of the captured
/// block variable for the purpose of block layout meta-data generation. FQT is
/// the type of the variable captured in the block.
Qualifiers::ObjCLifetime CGObjCCommonMac::getBlockCaptureLifetime(QualType FQT,
                                                                  bool ByrefLayout) {
  // If it has an ownership qualifier, we're done.
  if (auto lifetime = FQT.getObjCLifetime())
    return lifetime;

  // If it doesn't, and this is ARC, it has no ownership.
  if (CGM.getLangOpts().ObjCAutoRefCount)
    return Qualifiers::OCL_None;

  // In MRC, retainable pointers are owned by non-__block variables.
  if (FQT->isObjCObjectPointerType() || FQT->isBlockPointerType())
    return ByrefLayout ? Qualifiers::OCL_ExplicitNone : Qualifiers::OCL_Strong;

  return Qualifiers::OCL_None;
}

void CGObjCCommonMac::UpdateRunSkipBlockVars(bool IsByref,
                                             Qualifiers::ObjCLifetime LifeTime,
                                             CharUnits FieldOffset,
                                             CharUnits FieldSize) {
  // __block variables are passed by their descriptor address.
  if (IsByref)
    RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_BYREF, FieldOffset,
                                        FieldSize));
  else if (LifeTime == Qualifiers::OCL_Strong)
    RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_STRONG, FieldOffset,
                                        FieldSize));
  else if (LifeTime == Qualifiers::OCL_Weak)
    RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_WEAK, FieldOffset,
                                        FieldSize));
  else if (LifeTime == Qualifiers::OCL_ExplicitNone)
    RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_UNRETAINED, FieldOffset,
                                        FieldSize));
  else
    RunSkipBlockVars.push_back(RUN_SKIP(BLOCK_LAYOUT_NON_OBJECT_BYTES,
                                        FieldOffset,
                                        FieldSize));
}

void CGObjCCommonMac::BuildRCRecordLayout(const llvm::StructLayout *RecLayout,
                                          const RecordDecl *RD,
                                          ArrayRef<const FieldDecl*> RecFields,
                                          CharUnits BytePos, bool &HasUnion,
                                          bool ByrefLayout) {
  bool IsUnion = (RD && RD->isUnion());
  CharUnits MaxUnionSize = CharUnits::Zero();
  const FieldDecl *MaxField = nullptr;
  const FieldDecl *LastFieldBitfieldOrUnnamed = nullptr;
  CharUnits MaxFieldOffset = CharUnits::Zero();
  CharUnits LastBitfieldOrUnnamedOffset = CharUnits::Zero();

  if (RecFields.empty())
    return;
  unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();

  for (unsigned i = 0, e = RecFields.size(); i != e; ++i) {
    const FieldDecl *Field = RecFields[i];
    // Note that 'i' here is actually the field index inside RD of Field,
    // although this dependency is hidden.
    const ASTRecordLayout &RL = CGM.getContext().getASTRecordLayout(RD);
    CharUnits FieldOffset =
      CGM.getContext().toCharUnitsFromBits(RL.getFieldOffset(i));

    // Skip over unnamed or bitfields
    if (!Field->getIdentifier() || Field->isBitField()) {
      LastFieldBitfieldOrUnnamed = Field;
      LastBitfieldOrUnnamedOffset = FieldOffset;
      continue;
    }

    LastFieldBitfieldOrUnnamed = nullptr;
    QualType FQT = Field->getType();
    if (FQT->isRecordType() || FQT->isUnionType()) {
      if (FQT->isUnionType())
        HasUnion = true;

      BuildRCBlockVarRecordLayout(FQT->castAs<RecordType>(),
                                  BytePos + FieldOffset, HasUnion);
      continue;
    }

    if (const ArrayType *Array = CGM.getContext().getAsArrayType(FQT)) {
      auto *CArray = cast<ConstantArrayType>(Array);
      uint64_t ElCount = CArray->getZExtSize();
      assert(CArray && "only array with known element size is supported");
      FQT = CArray->getElementType();
      while (const ArrayType *Array = CGM.getContext().getAsArrayType(FQT)) {
        auto *CArray = cast<ConstantArrayType>(Array);
        ElCount *= CArray->getZExtSize();
        FQT = CArray->getElementType();
      }
      if (FQT->isRecordType() && ElCount) {
        int OldIndex = RunSkipBlockVars.size() - 1;
        auto *RT = FQT->castAs<RecordType>();
        BuildRCBlockVarRecordLayout(RT, BytePos + FieldOffset, HasUnion);

        // Replicate layout information for each array element. Note that
        // one element is already done.
        uint64_t ElIx = 1;
        for (int FirstIndex = RunSkipBlockVars.size() - 1 ;ElIx < ElCount; ElIx++) {
          CharUnits Size = CGM.getContext().getTypeSizeInChars(RT);
          for (int i = OldIndex+1; i <= FirstIndex; ++i)
            RunSkipBlockVars.push_back(
              RUN_SKIP(RunSkipBlockVars[i].opcode,
              RunSkipBlockVars[i].block_var_bytepos + Size*ElIx,
              RunSkipBlockVars[i].block_var_size));
        }
        continue;
      }
    }
    CharUnits FieldSize = CGM.getContext().getTypeSizeInChars(Field->getType());
    if (IsUnion) {
      CharUnits UnionIvarSize = FieldSize;
      if (UnionIvarSize > MaxUnionSize) {
        MaxUnionSize = UnionIvarSize;
        MaxField = Field;
        MaxFieldOffset = FieldOffset;
      }
    } else {
      UpdateRunSkipBlockVars(false,
                             getBlockCaptureLifetime(FQT, ByrefLayout),
                             BytePos + FieldOffset,
                             FieldSize);
    }
  }

  if (LastFieldBitfieldOrUnnamed) {
    if (LastFieldBitfieldOrUnnamed->isBitField()) {
      // Last field was a bitfield. Must update the info.
      uint64_t BitFieldSize
        = LastFieldBitfieldOrUnnamed->getBitWidthValue(CGM.getContext());
      unsigned UnsSize = (BitFieldSize / ByteSizeInBits) +
                        ((BitFieldSize % ByteSizeInBits) != 0);
      CharUnits Size = CharUnits::fromQuantity(UnsSize);
      Size += LastBitfieldOrUnnamedOffset;
      UpdateRunSkipBlockVars(false,
                             getBlockCaptureLifetime(LastFieldBitfieldOrUnnamed->getType(),
                                                     ByrefLayout),
                             BytePos + LastBitfieldOrUnnamedOffset,
                             Size);
    } else {
      assert(!LastFieldBitfieldOrUnnamed->getIdentifier() &&"Expected unnamed");
      // Last field was unnamed. Must update skip info.
      CharUnits FieldSize
        = CGM.getContext().getTypeSizeInChars(LastFieldBitfieldOrUnnamed->getType());
      UpdateRunSkipBlockVars(false,
                             getBlockCaptureLifetime(LastFieldBitfieldOrUnnamed->getType(),
                                                     ByrefLayout),
                             BytePos + LastBitfieldOrUnnamedOffset,
                             FieldSize);
    }
  }

  if (MaxField)
    UpdateRunSkipBlockVars(false,
                           getBlockCaptureLifetime(MaxField->getType(), ByrefLayout),
                           BytePos + MaxFieldOffset,
                           MaxUnionSize);
}

void CGObjCCommonMac::BuildRCBlockVarRecordLayout(const RecordType *RT,
                                                  CharUnits BytePos,
                                                  bool &HasUnion,
                                                  bool ByrefLayout) {
  const RecordDecl *RD = RT->getDecl();
  SmallVector<const FieldDecl*, 16> Fields(RD->fields());
  llvm::Type *Ty = CGM.getTypes().ConvertType(QualType(RT, 0));
  const llvm::StructLayout *RecLayout =
    CGM.getDataLayout().getStructLayout(cast<llvm::StructType>(Ty));

  BuildRCRecordLayout(RecLayout, RD, Fields, BytePos, HasUnion, ByrefLayout);
}

/// InlineLayoutInstruction - This routine produce an inline instruction for the
/// block variable layout if it can. If not, it returns 0. Rules are as follow:
/// If ((uintptr_t) layout) < (1 << 12), the layout is inline. In the 64bit world,
/// an inline layout of value 0x0000000000000xyz is interpreted as follows:
/// x captured object pointers of BLOCK_LAYOUT_STRONG. Followed by
/// y captured object of BLOCK_LAYOUT_BYREF. Followed by
/// z captured object of BLOCK_LAYOUT_WEAK. If any of the above is missing, zero
/// replaces it. For example, 0x00000x00 means x BLOCK_LAYOUT_STRONG and no
/// BLOCK_LAYOUT_BYREF and no BLOCK_LAYOUT_WEAK objects are captured.
uint64_t CGObjCCommonMac::InlineLayoutInstruction(
                                    SmallVectorImpl<unsigned char> &Layout) {
  uint64_t Result = 0;
  if (Layout.size() <= 3) {
    unsigned size = Layout.size();
    unsigned strong_word_count = 0, byref_word_count=0, weak_word_count=0;
    unsigned char inst;
    enum BLOCK_LAYOUT_OPCODE opcode ;
    switch (size) {
      case 3:
        inst = Layout[0];
        opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
        if (opcode == BLOCK_LAYOUT_STRONG)
          strong_word_count = (inst & 0xF)+1;
        else
          return 0;
        inst = Layout[1];
        opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
        if (opcode == BLOCK_LAYOUT_BYREF)
          byref_word_count = (inst & 0xF)+1;
        else
          return 0;
        inst = Layout[2];
        opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
        if (opcode == BLOCK_LAYOUT_WEAK)
          weak_word_count = (inst & 0xF)+1;
        else
          return 0;
        break;

      case 2:
        inst = Layout[0];
        opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
        if (opcode == BLOCK_LAYOUT_STRONG) {
          strong_word_count = (inst & 0xF)+1;
          inst = Layout[1];
          opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
          if (opcode == BLOCK_LAYOUT_BYREF)
            byref_word_count = (inst & 0xF)+1;
          else if (opcode == BLOCK_LAYOUT_WEAK)
            weak_word_count = (inst & 0xF)+1;
          else
            return 0;
        }
        else if (opcode == BLOCK_LAYOUT_BYREF) {
          byref_word_count = (inst & 0xF)+1;
          inst = Layout[1];
          opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
          if (opcode == BLOCK_LAYOUT_WEAK)
            weak_word_count = (inst & 0xF)+1;
          else
            return 0;
        }
        else
          return 0;
        break;

      case 1:
        inst = Layout[0];
        opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
        if (opcode == BLOCK_LAYOUT_STRONG)
          strong_word_count = (inst & 0xF)+1;
        else if (opcode == BLOCK_LAYOUT_BYREF)
          byref_word_count = (inst & 0xF)+1;
        else if (opcode == BLOCK_LAYOUT_WEAK)
          weak_word_count = (inst & 0xF)+1;
        else
          return 0;
        break;

      default:
        return 0;
    }

    // Cannot inline when any of the word counts is 15. Because this is one less
    // than the actual work count (so 15 means 16 actual word counts),
    // and we can only display 0 thru 15 word counts.
    if (strong_word_count == 16 || byref_word_count == 16 || weak_word_count == 16)
      return 0;

    unsigned count =
      (strong_word_count != 0) + (byref_word_count != 0) + (weak_word_count != 0);

    if (size == count) {
      if (strong_word_count)
        Result = strong_word_count;
      Result <<= 4;
      if (byref_word_count)
        Result += byref_word_count;
      Result <<= 4;
      if (weak_word_count)
        Result += weak_word_count;
    }
  }
  return Result;
}

llvm::Constant *CGObjCCommonMac::getBitmapBlockLayout(bool ComputeByrefLayout) {
  llvm::Constant *nullPtr = llvm::Constant::getNullValue(CGM.Int8PtrTy);
  if (RunSkipBlockVars.empty())
    return nullPtr;
  unsigned WordSizeInBits = CGM.getTarget().getPointerWidth(LangAS::Default);
  unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();
  unsigned WordSizeInBytes = WordSizeInBits/ByteSizeInBits;

  // Sort on byte position; captures might not be allocated in order,
  // and unions can do funny things.
  llvm::array_pod_sort(RunSkipBlockVars.begin(), RunSkipBlockVars.end());
  SmallVector<unsigned char, 16> Layout;

  unsigned size = RunSkipBlockVars.size();
  for (unsigned i = 0; i < size; i++) {
    enum BLOCK_LAYOUT_OPCODE opcode = RunSkipBlockVars[i].opcode;
    CharUnits start_byte_pos = RunSkipBlockVars[i].block_var_bytepos;
    CharUnits end_byte_pos = start_byte_pos;
    unsigned j = i+1;
    while (j < size) {
      if (opcode == RunSkipBlockVars[j].opcode) {
        end_byte_pos = RunSkipBlockVars[j++].block_var_bytepos;
        i++;
      }
      else
        break;
    }
    CharUnits size_in_bytes =
    end_byte_pos - start_byte_pos + RunSkipBlockVars[j-1].block_var_size;
    if (j < size) {
      CharUnits gap =
      RunSkipBlockVars[j].block_var_bytepos -
      RunSkipBlockVars[j-1].block_var_bytepos - RunSkipBlockVars[j-1].block_var_size;
      size_in_bytes += gap;
    }
    CharUnits residue_in_bytes = CharUnits::Zero();
    if (opcode == BLOCK_LAYOUT_NON_OBJECT_BYTES) {
      residue_in_bytes = size_in_bytes % WordSizeInBytes;
      size_in_bytes -= residue_in_bytes;
      opcode = BLOCK_LAYOUT_NON_OBJECT_WORDS;
    }

    unsigned size_in_words = size_in_bytes.getQuantity() / WordSizeInBytes;
    while (size_in_words >= 16) {
      // Note that value in imm. is one less that the actual
      // value. So, 0xf means 16 words follow!
      unsigned char inst = (opcode << 4) | 0xf;
      Layout.push_back(inst);
      size_in_words -= 16;
    }
    if (size_in_words > 0) {
      // Note that value in imm. is one less that the actual
      // value. So, we subtract 1 away!
      unsigned char inst = (opcode << 4) | (size_in_words-1);
      Layout.push_back(inst);
    }
    if (residue_in_bytes > CharUnits::Zero()) {
      unsigned char inst =
      (BLOCK_LAYOUT_NON_OBJECT_BYTES << 4) | (residue_in_bytes.getQuantity()-1);
      Layout.push_back(inst);
    }
  }

  while (!Layout.empty()) {
    unsigned char inst = Layout.back();
    enum BLOCK_LAYOUT_OPCODE opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
    if (opcode == BLOCK_LAYOUT_NON_OBJECT_BYTES || opcode == BLOCK_LAYOUT_NON_OBJECT_WORDS)
      Layout.pop_back();
    else
      break;
  }

  uint64_t Result = InlineLayoutInstruction(Layout);
  if (Result != 0) {
    // Block variable layout instruction has been inlined.
    if (CGM.getLangOpts().ObjCGCBitmapPrint) {
      if (ComputeByrefLayout)
        printf("\n Inline BYREF variable layout: ");
      else
        printf("\n Inline block variable layout: ");
      printf("0x0%" PRIx64 "", Result);
      if (auto numStrong = (Result & 0xF00) >> 8)
        printf(", BL_STRONG:%d", (int) numStrong);
      if (auto numByref = (Result & 0x0F0) >> 4)
        printf(", BL_BYREF:%d", (int) numByref);
      if (auto numWeak = (Result & 0x00F) >> 0)
        printf(", BL_WEAK:%d", (int) numWeak);
      printf(", BL_OPERATOR:0\n");
    }
    return llvm::ConstantInt::get(CGM.IntPtrTy, Result);
  }

  unsigned char inst = (BLOCK_LAYOUT_OPERATOR << 4) | 0;
  Layout.push_back(inst);
  std::string BitMap;
  for (unsigned i = 0, e = Layout.size(); i != e; i++)
    BitMap += Layout[i];

  if (CGM.getLangOpts().ObjCGCBitmapPrint) {
    if (ComputeByrefLayout)
      printf("\n Byref variable layout: ");
    else
      printf("\n Block variable layout: ");
    for (unsigned i = 0, e = BitMap.size(); i != e; i++) {
      unsigned char inst = BitMap[i];
      enum BLOCK_LAYOUT_OPCODE opcode = (enum BLOCK_LAYOUT_OPCODE) (inst >> 4);
      unsigned delta = 1;
      switch (opcode) {
        case BLOCK_LAYOUT_OPERATOR:
          printf("BL_OPERATOR:");
          delta = 0;
          break;
        case BLOCK_LAYOUT_NON_OBJECT_BYTES:
          printf("BL_NON_OBJECT_BYTES:");
          break;
        case BLOCK_LAYOUT_NON_OBJECT_WORDS:
          printf("BL_NON_OBJECT_WORD:");
          break;
        case BLOCK_LAYOUT_STRONG:
          printf("BL_STRONG:");
          break;
        case BLOCK_LAYOUT_BYREF:
          printf("BL_BYREF:");
          break;
        case BLOCK_LAYOUT_WEAK:
          printf("BL_WEAK:");
          break;
        case BLOCK_LAYOUT_UNRETAINED:
          printf("BL_UNRETAINED:");
          break;
      }
      // Actual value of word count is one more that what is in the imm.
      // field of the instruction
      printf("%d", (inst & 0xf) + delta);
      if (i < e-1)
        printf(", ");
      else
        printf("\n");
    }
  }

  auto *Entry = CreateCStringLiteral(BitMap, ObjCLabelType::ClassName,
                                     /*ForceNonFragileABI=*/true,
                                     /*NullTerminate=*/false);
  return getConstantGEP(VMContext, Entry, 0, 0);
}

static std::string getBlockLayoutInfoString(
    const SmallVectorImpl<CGObjCCommonMac::RUN_SKIP> &RunSkipBlockVars,
    bool HasCopyDisposeHelpers) {
  std::string Str;
  for (const CGObjCCommonMac::RUN_SKIP &R : RunSkipBlockVars) {
    if (R.opcode == CGObjCCommonMac::BLOCK_LAYOUT_UNRETAINED) {
      // Copy/dispose helpers don't have any information about
      // __unsafe_unretained captures, so unconditionally concatenate a string.
      Str += "u";
    } else if (HasCopyDisposeHelpers) {
      // Information about __strong, __weak, or byref captures has already been
      // encoded into the names of the copy/dispose helpers. We have to add a
      // string here only when the copy/dispose helpers aren't generated (which
      // happens when the block is non-escaping).
      continue;
    } else {
      switch (R.opcode) {
      case CGObjCCommonMac::BLOCK_LAYOUT_STRONG:
        Str += "s";
        break;
      case CGObjCCommonMac::BLOCK_LAYOUT_BYREF:
        Str += "r";
        break;
      case CGObjCCommonMac::BLOCK_LAYOUT_WEAK:
        Str += "w";
        break;
      default:
        continue;
      }
    }
    Str += llvm::to_string(R.block_var_bytepos.getQuantity());
    Str += "l" + llvm::to_string(R.block_var_size.getQuantity());
  }
  return Str;
}

void CGObjCCommonMac::fillRunSkipBlockVars(CodeGenModule &CGM,
                                           const CGBlockInfo &blockInfo) {
  assert(CGM.getLangOpts().getGC() == LangOptions::NonGC);

  RunSkipBlockVars.clear();
  bool hasUnion = false;

  unsigned WordSizeInBits = CGM.getTarget().getPointerWidth(LangAS::Default);
  unsigned ByteSizeInBits = CGM.getTarget().getCharWidth();
  unsigned WordSizeInBytes = WordSizeInBits/ByteSizeInBits;

  const BlockDecl *blockDecl = blockInfo.getBlockDecl();

  // Calculate the basic layout of the block structure.
  const llvm::StructLayout *layout =
  CGM.getDataLayout().getStructLayout(blockInfo.StructureType);

  // Ignore the optional 'this' capture: C++ objects are not assumed
  // to be GC'ed.
  if (blockInfo.BlockHeaderForcedGapSize != CharUnits::Zero())
    UpdateRunSkipBlockVars(false, Qualifiers::OCL_None,
                           blockInfo.BlockHeaderForcedGapOffset,
                           blockInfo.BlockHeaderForcedGapSize);
  // Walk the captured variables.
  for (const auto &CI : blockDecl->captures()) {
    const VarDecl *variable = CI.getVariable();
    QualType type = variable->getType();

    const CGBlockInfo::Capture &capture = blockInfo.getCapture(variable);

    // Ignore constant captures.
    if (capture.isConstant()) continue;

    CharUnits fieldOffset =
       CharUnits::fromQuantity(layout->getElementOffset(capture.getIndex()));

    assert(!type->isArrayType() && "array variable should not be caught");
    if (!CI.isByRef())
      if (const RecordType *record = type->getAs<RecordType>()) {
        BuildRCBlockVarRecordLayout(record, fieldOffset, hasUnion);
        continue;
      }
    CharUnits fieldSize;
    if (CI.isByRef())
      fieldSize = CharUnits::fromQuantity(WordSizeInBytes);
    else
      fieldSize = CGM.getContext().getTypeSizeInChars(type);
    UpdateRunSkipBlockVars(CI.isByRef(), getBlockCaptureLifetime(type, false),
                           fieldOffset, fieldSize);
  }
}

llvm::Constant *
CGObjCCommonMac::BuildRCBlockLayout(CodeGenModule &CGM,
                                    const CGBlockInfo &blockInfo) {
  fillRunSkipBlockVars(CGM, blockInfo);
  return getBitmapBlockLayout(false);
}

std::string CGObjCCommonMac::getRCBlockLayoutStr(CodeGenModule &CGM,
                                                 const CGBlockInfo &blockInfo) {
  fillRunSkipBlockVars(CGM, blockInfo);
  return getBlockLayoutInfoString(RunSkipBlockVars, blockInfo.NeedsCopyDispose);
}

llvm::Constant *CGObjCCommonMac::BuildByrefLayout(CodeGen::CodeGenModule &CGM,
                                                  QualType T) {
  assert(CGM.getLangOpts().getGC() == LangOptions::NonGC);
  assert(!T->isArrayType() && "__block array variable should not be caught");
  CharUnits fieldOffset;
  RunSkipBlockVars.clear();
  bool hasUnion = false;
  if (const RecordType *record = T->getAs<RecordType>()) {
    BuildRCBlockVarRecordLayout(record, fieldOffset, hasUnion, true /*ByrefLayout */);
    llvm::Constant *Result = getBitmapBlockLayout(true);
    if (isa<llvm::ConstantInt>(Result))
      Result = llvm::ConstantExpr::getIntToPtr(Result, CGM.Int8PtrTy);
    return Result;
  }
  llvm::Constant *nullPtr = llvm::Constant::getNullValue(CGM.Int8PtrTy);
  return nullPtr;
}

llvm::Value *CGObjCMac::GenerateProtocolRef(CodeGenFunction &CGF,
                                            const ObjCProtocolDecl *PD) {
  // FIXME: I don't understand why gcc generates this, or where it is
  // resolved. Investigate. Its also wasteful to look this up over and over.
  LazySymbols.insert(&CGM.getContext().Idents.get("Protocol"));

  return GetProtocolRef(PD);
}

void CGObjCCommonMac::GenerateProtocol(const ObjCProtocolDecl *PD) {
  // FIXME: We shouldn't need this, the protocol decl should contain enough
  // information to tell us whether this was a declaration or a definition.
  DefinedProtocols.insert(PD->getIdentifier());

  // If we have generated a forward reference to this protocol, emit
  // it now. Otherwise do nothing, the protocol objects are lazily
  // emitted.
  if (Protocols.count(PD->getIdentifier()))
    GetOrEmitProtocol(PD);
}

llvm::Constant *CGObjCCommonMac::GetProtocolRef(const ObjCProtocolDecl *PD) {
  if (DefinedProtocols.count(PD->getIdentifier()))
    return GetOrEmitProtocol(PD);

  return GetOrEmitProtocolRef(PD);
}

llvm::Value *CGObjCCommonMac::EmitClassRefViaRuntime(
               CodeGenFunction &CGF,
               const ObjCInterfaceDecl *ID,
               ObjCCommonTypesHelper &ObjCTypes) {
  llvm::FunctionCallee lookUpClassFn = ObjCTypes.getLookUpClassFn();

  llvm::Value *className = CGF.CGM
                               .GetAddrOfConstantCString(std::string(
                                   ID->getObjCRuntimeNameAsString()))
                               .getPointer();
  ASTContext &ctx = CGF.CGM.getContext();
  className =
      CGF.Builder.CreateBitCast(className,
                                CGF.ConvertType(
                                  ctx.getPointerType(ctx.CharTy.withConst())));
  llvm::CallInst *call = CGF.Builder.CreateCall(lookUpClassFn, className);
  call->setDoesNotThrow();
  return call;
}

/*
// Objective-C 1.0 extensions
struct _objc_protocol {
struct _objc_protocol_extension *isa;
char *protocol_name;
struct _objc_protocol_list *protocol_list;
struct _objc__method_prototype_list *instance_methods;
struct _objc__method_prototype_list *class_methods
};

See EmitProtocolExtension().
*/
llvm::Constant *CGObjCMac::GetOrEmitProtocol(const ObjCProtocolDecl *PD) {
  llvm::GlobalVariable *Entry = Protocols[PD->getIdentifier()];

  // Early exit if a defining object has already been generated.
  if (Entry && Entry->hasInitializer())
    return Entry;

  // Use the protocol definition, if there is one.
  if (const ObjCProtocolDecl *Def = PD->getDefinition())
    PD = Def;

  // FIXME: I don't understand why gcc generates this, or where it is
  // resolved. Investigate. Its also wasteful to look this up over and over.
  LazySymbols.insert(&CGM.getContext().Idents.get("Protocol"));

  // Construct method lists.
  auto methodLists = ProtocolMethodLists::get(PD);

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ProtocolTy);
  values.add(EmitProtocolExtension(PD, methodLists));
  values.add(GetClassName(PD->getObjCRuntimeNameAsString()));
  values.add(EmitProtocolList("OBJC_PROTOCOL_REFS_" + PD->getName(),
                              PD->protocol_begin(), PD->protocol_end()));
  values.add(methodLists.emitMethodList(this, PD,
                              ProtocolMethodLists::RequiredInstanceMethods));
  values.add(methodLists.emitMethodList(this, PD,
                              ProtocolMethodLists::RequiredClassMethods));

  if (Entry) {
    // Already created, update the initializer.
    assert(Entry->hasPrivateLinkage());
    values.finishAndSetAsInitializer(Entry);
  } else {
    Entry = values.finishAndCreateGlobal("OBJC_PROTOCOL_" + PD->getName(),
                                         CGM.getPointerAlign(),
                                         /*constant*/ false,
                                         llvm::GlobalValue::PrivateLinkage);
    Entry->setSection("__OBJC,__protocol,regular,no_dead_strip");

    Protocols[PD->getIdentifier()] = Entry;
  }
  CGM.addCompilerUsedGlobal(Entry);

  return Entry;
}

llvm::Constant *CGObjCMac::GetOrEmitProtocolRef(const ObjCProtocolDecl *PD) {
  llvm::GlobalVariable *&Entry = Protocols[PD->getIdentifier()];

  if (!Entry) {
    // We use the initializer as a marker of whether this is a forward
    // reference or not. At module finalization we add the empty
    // contents for protocols which were referenced but never defined.
    Entry = new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.ProtocolTy,
                                     false, llvm::GlobalValue::PrivateLinkage,
                                     nullptr, "OBJC_PROTOCOL_" + PD->getName());
    Entry->setSection("__OBJC,__protocol,regular,no_dead_strip");
    // FIXME: Is this necessary? Why only for protocol?
    Entry->setAlignment(llvm::Align(4));
  }

  return Entry;
}

/*
  struct _objc_protocol_extension {
  uint32_t size;
  struct objc_method_description_list *optional_instance_methods;
  struct objc_method_description_list *optional_class_methods;
  struct objc_property_list *instance_properties;
  const char ** extendedMethodTypes;
  struct objc_property_list *class_properties;
  };
*/
llvm::Constant *
CGObjCMac::EmitProtocolExtension(const ObjCProtocolDecl *PD,
                                 const ProtocolMethodLists &methodLists) {
  auto optInstanceMethods =
    methodLists.emitMethodList(this, PD,
                               ProtocolMethodLists::OptionalInstanceMethods);
  auto optClassMethods =
    methodLists.emitMethodList(this, PD,
                               ProtocolMethodLists::OptionalClassMethods);

  auto extendedMethodTypes =
    EmitProtocolMethodTypes("OBJC_PROTOCOL_METHOD_TYPES_" + PD->getName(),
                            methodLists.emitExtendedTypesArray(this),
                            ObjCTypes);

  auto instanceProperties =
    EmitPropertyList("OBJC_$_PROP_PROTO_LIST_" + PD->getName(), nullptr, PD,
                     ObjCTypes, false);
  auto classProperties =
    EmitPropertyList("OBJC_$_CLASS_PROP_PROTO_LIST_" + PD->getName(), nullptr,
                     PD, ObjCTypes, true);

  // Return null if no extension bits are used.
  if (optInstanceMethods->isNullValue() &&
      optClassMethods->isNullValue() &&
      extendedMethodTypes->isNullValue() &&
      instanceProperties->isNullValue() &&
      classProperties->isNullValue()) {
    return llvm::Constant::getNullValue(ObjCTypes.ProtocolExtensionPtrTy);
  }

  uint64_t size =
    CGM.getDataLayout().getTypeAllocSize(ObjCTypes.ProtocolExtensionTy);

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ProtocolExtensionTy);
  values.addInt(ObjCTypes.IntTy, size);
  values.add(optInstanceMethods);
  values.add(optClassMethods);
  values.add(instanceProperties);
  values.add(extendedMethodTypes);
  values.add(classProperties);

  // No special section, but goes in llvm.used
  return CreateMetadataVar("_OBJC_PROTOCOLEXT_" + PD->getName(), values,
                           StringRef(), CGM.getPointerAlign(), true);
}

/*
  struct objc_protocol_list {
    struct objc_protocol_list *next;
    long count;
    Protocol *list[];
  };
*/
llvm::Constant *
CGObjCMac::EmitProtocolList(Twine name,
                            ObjCProtocolDecl::protocol_iterator begin,
                            ObjCProtocolDecl::protocol_iterator end) {
  // Just return null for empty protocol lists
  auto PDs = GetRuntimeProtocolList(begin, end);
  if (PDs.empty())
    return llvm::Constant::getNullValue(ObjCTypes.ProtocolListPtrTy);

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct();

  // This field is only used by the runtime.
  values.addNullPointer(ObjCTypes.ProtocolListPtrTy);

  // Reserve a slot for the count.
  auto countSlot = values.addPlaceholder();

  auto refsArray = values.beginArray(ObjCTypes.ProtocolPtrTy);
  for (const auto *Proto : PDs)
    refsArray.add(GetProtocolRef(Proto));

  auto count = refsArray.size();

  // This list is null terminated.
  refsArray.addNullPointer(ObjCTypes.ProtocolPtrTy);

  refsArray.finishAndAddTo(values);
  values.fillPlaceholderWithInt(countSlot, ObjCTypes.LongTy, count);

  StringRef section;
  if (CGM.getTriple().isOSBinFormatMachO())
    section = "__OBJC,__cat_cls_meth,regular,no_dead_strip";

  llvm::GlobalVariable *GV =
      CreateMetadataVar(name, values, section, CGM.getPointerAlign(), false);
  return GV;
}

static void
PushProtocolProperties(llvm::SmallPtrSet<const IdentifierInfo*,16> &PropertySet,
                       SmallVectorImpl<const ObjCPropertyDecl *> &Properties,
                       const ObjCProtocolDecl *Proto,
                       bool IsClassProperty) {
  for (const auto *PD : Proto->properties()) {
    if (IsClassProperty != PD->isClassProperty())
      continue;
    if (!PropertySet.insert(PD->getIdentifier()).second)
      continue;
    Properties.push_back(PD);
  }

  for (const auto *P : Proto->protocols())
    PushProtocolProperties(PropertySet, Properties, P, IsClassProperty);
}

/*
  struct _objc_property {
    const char * const name;
    const char * const attributes;
  };

  struct _objc_property_list {
    uint32_t entsize; // sizeof (struct _objc_property)
    uint32_t prop_count;
    struct _objc_property[prop_count];
  };
*/
llvm::Constant *CGObjCCommonMac::EmitPropertyList(Twine Name,
                                       const Decl *Container,
                                       const ObjCContainerDecl *OCD,
                                       const ObjCCommonTypesHelper &ObjCTypes,
                                       bool IsClassProperty) {
  if (IsClassProperty) {
    // Make this entry NULL for OS X with deployment target < 10.11, for iOS
    // with deployment target < 9.0.
    const llvm::Triple &Triple = CGM.getTarget().getTriple();
    if ((Triple.isMacOSX() && Triple.isMacOSXVersionLT(10, 11)) ||
        (Triple.isiOS() && Triple.isOSVersionLT(9)))
      return llvm::Constant::getNullValue(ObjCTypes.PropertyListPtrTy);
  }

  SmallVector<const ObjCPropertyDecl *, 16> Properties;
  llvm::SmallPtrSet<const IdentifierInfo*, 16> PropertySet;

  if (const ObjCInterfaceDecl *OID = dyn_cast<ObjCInterfaceDecl>(OCD))
    for (const ObjCCategoryDecl *ClassExt : OID->known_extensions())
      for (auto *PD : ClassExt->properties()) {
        if (IsClassProperty != PD->isClassProperty())
          continue;
        if (PD->isDirectProperty())
          continue;
        PropertySet.insert(PD->getIdentifier());
        Properties.push_back(PD);
      }

  for (const auto *PD : OCD->properties()) {
    if (IsClassProperty != PD->isClassProperty())
      continue;
    // Don't emit duplicate metadata for properties that were already in a
    // class extension.
    if (!PropertySet.insert(PD->getIdentifier()).second)
      continue;
    if (PD->isDirectProperty())
      continue;
    Properties.push_back(PD);
  }

  if (const ObjCInterfaceDecl *OID = dyn_cast<ObjCInterfaceDecl>(OCD)) {
    for (const auto *P : OID->all_referenced_protocols())
      PushProtocolProperties(PropertySet, Properties, P, IsClassProperty);
  }
  else if (const ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(OCD)) {
    for (const auto *P : CD->protocols())
      PushProtocolProperties(PropertySet, Properties, P, IsClassProperty);
  }

  // Return null for empty list.
  if (Properties.empty())
    return llvm::Constant::getNullValue(ObjCTypes.PropertyListPtrTy);

  unsigned propertySize =
    CGM.getDataLayout().getTypeAllocSize(ObjCTypes.PropertyTy);

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct();
  values.addInt(ObjCTypes.IntTy, propertySize);
  values.addInt(ObjCTypes.IntTy, Properties.size());
  auto propertiesArray = values.beginArray(ObjCTypes.PropertyTy);
  for (auto PD : Properties) {
    auto property = propertiesArray.beginStruct(ObjCTypes.PropertyTy);
    property.add(GetPropertyName(PD->getIdentifier()));
    property.add(GetPropertyTypeString(PD, Container));
    property.finishAndAddTo(propertiesArray);
  }
  propertiesArray.finishAndAddTo(values);

  StringRef Section;
  if (CGM.getTriple().isOSBinFormatMachO())
    Section = (ObjCABI == 2) ? "__DATA, __objc_const"
                             : "__OBJC,__property,regular,no_dead_strip";

  llvm::GlobalVariable *GV =
      CreateMetadataVar(Name, values, Section, CGM.getPointerAlign(), true);
  return GV;
}

llvm::Constant *
CGObjCCommonMac::EmitProtocolMethodTypes(Twine Name,
                                         ArrayRef<llvm::Constant*> MethodTypes,
                                         const ObjCCommonTypesHelper &ObjCTypes) {
  // Return null for empty list.
  if (MethodTypes.empty())
    return llvm::Constant::getNullValue(ObjCTypes.Int8PtrPtrTy);

  llvm::ArrayType *AT = llvm::ArrayType::get(ObjCTypes.Int8PtrTy,
                                             MethodTypes.size());
  llvm::Constant *Init = llvm::ConstantArray::get(AT, MethodTypes);

  StringRef Section;
  if (CGM.getTriple().isOSBinFormatMachO() && ObjCABI == 2)
    Section = "__DATA, __objc_const";

  llvm::GlobalVariable *GV =
      CreateMetadataVar(Name, Init, Section, CGM.getPointerAlign(), true);
  return GV;
}

/*
  struct _objc_category {
  char *category_name;
  char *class_name;
  struct _objc_method_list *instance_methods;
  struct _objc_method_list *class_methods;
  struct _objc_protocol_list *protocols;
  uint32_t size; // sizeof(struct _objc_category)
  struct _objc_property_list *instance_properties;
  struct _objc_property_list *class_properties;
  };
*/
void CGObjCMac::GenerateCategory(const ObjCCategoryImplDecl *OCD) {
  unsigned Size = CGM.getDataLayout().getTypeAllocSize(ObjCTypes.CategoryTy);

  // FIXME: This is poor design, the OCD should have a pointer to the category
  // decl. Additionally, note that Category can be null for the @implementation
  // w/o an @interface case. Sema should just create one for us as it does for
  // @implementation so everyone else can live life under a clear blue sky.
  const ObjCInterfaceDecl *Interface = OCD->getClassInterface();
  const ObjCCategoryDecl *Category =
    Interface->FindCategoryDeclaration(OCD->getIdentifier());

  SmallString<256> ExtName;
  llvm::raw_svector_ostream(ExtName) << Interface->getName() << '_'
                                     << OCD->getName();

  ConstantInitBuilder Builder(CGM);
  auto Values = Builder.beginStruct(ObjCTypes.CategoryTy);

  enum {
    InstanceMethods,
    ClassMethods,
    NumMethodLists
  };
  SmallVector<const ObjCMethodDecl *, 16> Methods[NumMethodLists];
  for (const auto *MD : OCD->methods()) {
    if (!MD->isDirectMethod())
      Methods[unsigned(MD->isClassMethod())].push_back(MD);
  }

  Values.add(GetClassName(OCD->getName()));
  Values.add(GetClassName(Interface->getObjCRuntimeNameAsString()));
  LazySymbols.insert(Interface->getIdentifier());

  Values.add(emitMethodList(ExtName, MethodListType::CategoryInstanceMethods,
                            Methods[InstanceMethods]));
  Values.add(emitMethodList(ExtName, MethodListType::CategoryClassMethods,
                            Methods[ClassMethods]));
  if (Category) {
    Values.add(
        EmitProtocolList("OBJC_CATEGORY_PROTOCOLS_" + ExtName.str(),
                         Category->protocol_begin(), Category->protocol_end()));
  } else {
    Values.addNullPointer(ObjCTypes.ProtocolListPtrTy);
  }
  Values.addInt(ObjCTypes.IntTy, Size);

  // If there is no category @interface then there can be no properties.
  if (Category) {
    Values.add(EmitPropertyList("_OBJC_$_PROP_LIST_" + ExtName.str(),
                                OCD, Category, ObjCTypes, false));
    Values.add(EmitPropertyList("_OBJC_$_CLASS_PROP_LIST_" + ExtName.str(),
                                OCD, Category, ObjCTypes, true));
  } else {
    Values.addNullPointer(ObjCTypes.PropertyListPtrTy);
    Values.addNullPointer(ObjCTypes.PropertyListPtrTy);
  }

  llvm::GlobalVariable *GV =
      CreateMetadataVar("OBJC_CATEGORY_" + ExtName.str(), Values,
                        "__OBJC,__category,regular,no_dead_strip",
                        CGM.getPointerAlign(), true);
  DefinedCategories.push_back(GV);
  DefinedCategoryNames.insert(llvm::CachedHashString(ExtName));
  // method definition entries must be clear for next implementation.
  MethodDefinitions.clear();
}

enum FragileClassFlags {
  /// Apparently: is not a meta-class.
  FragileABI_Class_Factory                 = 0x00001,

  /// Is a meta-class.
  FragileABI_Class_Meta                    = 0x00002,

  /// Has a non-trivial constructor or destructor.
  FragileABI_Class_HasCXXStructors         = 0x02000,

  /// Has hidden visibility.
  FragileABI_Class_Hidden                  = 0x20000,

  /// Class implementation was compiled under ARC.
  FragileABI_Class_CompiledByARC           = 0x04000000,

  /// Class implementation was compiled under MRC and has MRC weak ivars.
  /// Exclusive with CompiledByARC.
  FragileABI_Class_HasMRCWeakIvars         = 0x08000000,
};

enum NonFragileClassFlags {
  /// Is a meta-class.
  NonFragileABI_Class_Meta                 = 0x00001,

  /// Is a root class.
  NonFragileABI_Class_Root                 = 0x00002,

  /// Has a non-trivial constructor or destructor.
  NonFragileABI_Class_HasCXXStructors      = 0x00004,

  /// Has hidden visibility.
  NonFragileABI_Class_Hidden               = 0x00010,

  /// Has the exception attribute.
  NonFragileABI_Class_Exception            = 0x00020,

  /// (Obsolete) ARC-specific: this class has a .release_ivars method
  NonFragileABI_Class_HasIvarReleaser      = 0x00040,

  /// Class implementation was compiled under ARC.
  NonFragileABI_Class_CompiledByARC        = 0x00080,

  /// Class has non-trivial destructors, but zero-initialization is okay.
  NonFragileABI_Class_HasCXXDestructorOnly = 0x00100,

  /// Class implementation was compiled under MRC and has MRC weak ivars.
  /// Exclusive with CompiledByARC.
  NonFragileABI_Class_HasMRCWeakIvars      = 0x00200,
};

static bool hasWeakMember(QualType type) {
  if (type.getObjCLifetime() == Qualifiers::OCL_Weak) {
    return true;
  }

  if (auto recType = type->getAs<RecordType>()) {
    for (auto *field : recType->getDecl()->fields()) {
      if (hasWeakMember(field->getType()))
        return true;
    }
  }

  return false;
}

/// For compatibility, we only want to set the "HasMRCWeakIvars" flag
/// (and actually fill in a layout string) if we really do have any
/// __weak ivars.
static bool hasMRCWeakIvars(CodeGenModule &CGM,
                            const ObjCImplementationDecl *ID) {
  if (!CGM.getLangOpts().ObjCWeak) return false;
  assert(CGM.getLangOpts().getGC() == LangOptions::NonGC);

  for (const ObjCIvarDecl *ivar =
         ID->getClassInterface()->all_declared_ivar_begin();
       ivar; ivar = ivar->getNextIvar()) {
    if (hasWeakMember(ivar->getType()))
      return true;
  }

  return false;
}

/*
  struct _objc_class {
  Class isa;
  Class super_class;
  const char *name;
  long version;
  long info;
  long instance_size;
  struct _objc_ivar_list *ivars;
  struct _objc_method_list *methods;
  struct _objc_cache *cache;
  struct _objc_protocol_list *protocols;
  // Objective-C 1.0 extensions (<rdr://4585769>)
  const char *ivar_layout;
  struct _objc_class_ext *ext;
  };

  See EmitClassExtension();
*/
void CGObjCMac::GenerateClass(const ObjCImplementationDecl *ID) {
  IdentifierInfo *RuntimeName =
      &CGM.getContext().Idents.get(ID->getObjCRuntimeNameAsString());
  DefinedSymbols.insert(RuntimeName);

  std::string ClassName = ID->getNameAsString();
  // FIXME: Gross
  ObjCInterfaceDecl *Interface =
    const_cast<ObjCInterfaceDecl*>(ID->getClassInterface());
  llvm::Constant *Protocols =
      EmitProtocolList("OBJC_CLASS_PROTOCOLS_" + ID->getName(),
                       Interface->all_referenced_protocol_begin(),
                       Interface->all_referenced_protocol_end());
  unsigned Flags = FragileABI_Class_Factory;
  if (ID->hasNonZeroConstructors() || ID->hasDestructors())
    Flags |= FragileABI_Class_HasCXXStructors;

  bool hasMRCWeak = false;

  if (CGM.getLangOpts().ObjCAutoRefCount)
    Flags |= FragileABI_Class_CompiledByARC;
  else if ((hasMRCWeak = hasMRCWeakIvars(CGM, ID)))
    Flags |= FragileABI_Class_HasMRCWeakIvars;

  CharUnits Size =
    CGM.getContext().getASTObjCImplementationLayout(ID).getSize();

  // FIXME: Set CXX-structors flag.
  if (ID->getClassInterface()->getVisibility() == HiddenVisibility)
    Flags |= FragileABI_Class_Hidden;

  enum {
    InstanceMethods,
    ClassMethods,
    NumMethodLists
  };
  SmallVector<const ObjCMethodDecl *, 16> Methods[NumMethodLists];
  for (const auto *MD : ID->methods()) {
    if (!MD->isDirectMethod())
      Methods[unsigned(MD->isClassMethod())].push_back(MD);
  }

  for (const auto *PID : ID->property_impls()) {
    if (PID->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize) {
      if (PID->getPropertyDecl()->isDirectProperty())
        continue;
      if (ObjCMethodDecl *MD = PID->getGetterMethodDecl())
        if (GetMethodDefinition(MD))
          Methods[InstanceMethods].push_back(MD);
      if (ObjCMethodDecl *MD = PID->getSetterMethodDecl())
        if (GetMethodDefinition(MD))
          Methods[InstanceMethods].push_back(MD);
    }
  }

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ClassTy);
  values.add(EmitMetaClass(ID, Protocols, Methods[ClassMethods]));
  if (ObjCInterfaceDecl *Super = Interface->getSuperClass()) {
    // Record a reference to the super class.
    LazySymbols.insert(Super->getIdentifier());

    values.add(GetClassName(Super->getObjCRuntimeNameAsString()));
  } else {
    values.addNullPointer(ObjCTypes.ClassPtrTy);
  }
  values.add(GetClassName(ID->getObjCRuntimeNameAsString()));
  // Version is always 0.
  values.addInt(ObjCTypes.LongTy, 0);
  values.addInt(ObjCTypes.LongTy, Flags);
  values.addInt(ObjCTypes.LongTy, Size.getQuantity());
  values.add(EmitIvarList(ID, false));
  values.add(emitMethodList(ID->getName(), MethodListType::InstanceMethods,
                            Methods[InstanceMethods]));
  // cache is always NULL.
  values.addNullPointer(ObjCTypes.CachePtrTy);
  values.add(Protocols);
  values.add(BuildStrongIvarLayout(ID, CharUnits::Zero(), Size));
  values.add(EmitClassExtension(ID, Size, hasMRCWeak,
                                /*isMetaclass*/ false));

  std::string Name("OBJC_CLASS_");
  Name += ClassName;
  const char *Section = "__OBJC,__class,regular,no_dead_strip";
  // Check for a forward reference.
  llvm::GlobalVariable *GV = CGM.getModule().getGlobalVariable(Name, true);
  if (GV) {
    assert(GV->getValueType() == ObjCTypes.ClassTy &&
           "Forward metaclass reference has incorrect type.");
    values.finishAndSetAsInitializer(GV);
    GV->setSection(Section);
    GV->setAlignment(CGM.getPointerAlign().getAsAlign());
    CGM.addCompilerUsedGlobal(GV);
  } else
    GV = CreateMetadataVar(Name, values, Section, CGM.getPointerAlign(), true);
  DefinedClasses.push_back(GV);
  ImplementedClasses.push_back(Interface);
  // method definition entries must be clear for next implementation.
  MethodDefinitions.clear();
}

llvm::Constant *CGObjCMac::EmitMetaClass(const ObjCImplementationDecl *ID,
                                         llvm::Constant *Protocols,
                                ArrayRef<const ObjCMethodDecl*> Methods) {
  unsigned Flags = FragileABI_Class_Meta;
  unsigned Size = CGM.getDataLayout().getTypeAllocSize(ObjCTypes.ClassTy);

  if (ID->getClassInterface()->getVisibility() == HiddenVisibility)
    Flags |= FragileABI_Class_Hidden;

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ClassTy);
  // The isa for the metaclass is the root of the hierarchy.
  const ObjCInterfaceDecl *Root = ID->getClassInterface();
  while (const ObjCInterfaceDecl *Super = Root->getSuperClass())
    Root = Super;
  values.add(GetClassName(Root->getObjCRuntimeNameAsString()));
  // The super class for the metaclass is emitted as the name of the
  // super class. The runtime fixes this up to point to the
  // *metaclass* for the super class.
  if (ObjCInterfaceDecl *Super = ID->getClassInterface()->getSuperClass()) {
    values.add(GetClassName(Super->getObjCRuntimeNameAsString()));
  } else {
    values.addNullPointer(ObjCTypes.ClassPtrTy);
  }
  values.add(GetClassName(ID->getObjCRuntimeNameAsString()));
  // Version is always 0.
  values.addInt(ObjCTypes.LongTy, 0);
  values.addInt(ObjCTypes.LongTy, Flags);
  values.addInt(ObjCTypes.LongTy, Size);
  values.add(EmitIvarList(ID, true));
  values.add(emitMethodList(ID->getName(), MethodListType::ClassMethods,
                            Methods));
  // cache is always NULL.
  values.addNullPointer(ObjCTypes.CachePtrTy);
  values.add(Protocols);
  // ivar_layout for metaclass is always NULL.
  values.addNullPointer(ObjCTypes.Int8PtrTy);
  // The class extension is used to store class properties for metaclasses.
  values.add(EmitClassExtension(ID, CharUnits::Zero(), false/*hasMRCWeak*/,
                                /*isMetaclass*/true));

  std::string Name("OBJC_METACLASS_");
  Name += ID->getName();

  // Check for a forward reference.
  llvm::GlobalVariable *GV = CGM.getModule().getGlobalVariable(Name, true);
  if (GV) {
    assert(GV->getValueType() == ObjCTypes.ClassTy &&
           "Forward metaclass reference has incorrect type.");
    values.finishAndSetAsInitializer(GV);
  } else {
    GV = values.finishAndCreateGlobal(Name, CGM.getPointerAlign(),
                                      /*constant*/ false,
                                      llvm::GlobalValue::PrivateLinkage);
  }
  GV->setSection("__OBJC,__meta_class,regular,no_dead_strip");
  CGM.addCompilerUsedGlobal(GV);

  return GV;
}

llvm::Constant *CGObjCMac::EmitMetaClassRef(const ObjCInterfaceDecl *ID) {
  std::string Name = "OBJC_METACLASS_" + ID->getNameAsString();

  // FIXME: Should we look these up somewhere other than the module. Its a bit
  // silly since we only generate these while processing an implementation, so
  // exactly one pointer would work if know when we entered/exitted an
  // implementation block.

  // Check for an existing forward reference.
  // Previously, metaclass with internal linkage may have been defined.
  // pass 'true' as 2nd argument so it is returned.
  llvm::GlobalVariable *GV = CGM.getModule().getGlobalVariable(Name, true);
  if (!GV)
    GV = new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.ClassTy, false,
                                  llvm::GlobalValue::PrivateLinkage, nullptr,
                                  Name);

  assert(GV->getValueType() == ObjCTypes.ClassTy &&
         "Forward metaclass reference has incorrect type.");
  return GV;
}

llvm::Value *CGObjCMac::EmitSuperClassRef(const ObjCInterfaceDecl *ID) {
  std::string Name = "OBJC_CLASS_" + ID->getNameAsString();
  llvm::GlobalVariable *GV = CGM.getModule().getGlobalVariable(Name, true);

  if (!GV)
    GV = new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.ClassTy, false,
                                  llvm::GlobalValue::PrivateLinkage, nullptr,
                                  Name);

  assert(GV->getValueType() == ObjCTypes.ClassTy &&
         "Forward class metadata reference has incorrect type.");
  return GV;
}

/*
  Emit a "class extension", which in this specific context means extra
  data that doesn't fit in the normal fragile-ABI class structure, and
  has nothing to do with the language concept of a class extension.

  struct objc_class_ext {
  uint32_t size;
  const char *weak_ivar_layout;
  struct _objc_property_list *properties;
  };
*/
llvm::Constant *
CGObjCMac::EmitClassExtension(const ObjCImplementationDecl *ID,
                              CharUnits InstanceSize, bool hasMRCWeakIvars,
                              bool isMetaclass) {
  // Weak ivar layout.
  llvm::Constant *layout;
  if (isMetaclass) {
    layout = llvm::ConstantPointerNull::get(CGM.Int8PtrTy);
  } else {
    layout = BuildWeakIvarLayout(ID, CharUnits::Zero(), InstanceSize,
                                 hasMRCWeakIvars);
  }

  // Properties.
  llvm::Constant *propertyList =
    EmitPropertyList((isMetaclass ? Twine("_OBJC_$_CLASS_PROP_LIST_")
                                  : Twine("_OBJC_$_PROP_LIST_"))
                        + ID->getName(),
                     ID, ID->getClassInterface(), ObjCTypes, isMetaclass);

  // Return null if no extension bits are used.
  if (layout->isNullValue() && propertyList->isNullValue()) {
    return llvm::Constant::getNullValue(ObjCTypes.ClassExtensionPtrTy);
  }

  uint64_t size =
    CGM.getDataLayout().getTypeAllocSize(ObjCTypes.ClassExtensionTy);

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ClassExtensionTy);
  values.addInt(ObjCTypes.IntTy, size);
  values.add(layout);
  values.add(propertyList);

  return CreateMetadataVar("OBJC_CLASSEXT_" + ID->getName(), values,
                           "__OBJC,__class_ext,regular,no_dead_strip",
                           CGM.getPointerAlign(), true);
}

/*
  struct objc_ivar {
    char *ivar_name;
    char *ivar_type;
    int ivar_offset;
  };

  struct objc_ivar_list {
    int ivar_count;
    struct objc_ivar list[count];
  };
*/
llvm::Constant *CGObjCMac::EmitIvarList(const ObjCImplementationDecl *ID,
                                        bool ForClass) {
  // When emitting the root class GCC emits ivar entries for the
  // actual class structure. It is not clear if we need to follow this
  // behavior; for now lets try and get away with not doing it. If so,
  // the cleanest solution would be to make up an ObjCInterfaceDecl
  // for the class.
  if (ForClass)
    return llvm::Constant::getNullValue(ObjCTypes.IvarListPtrTy);

  const ObjCInterfaceDecl *OID = ID->getClassInterface();

  ConstantInitBuilder builder(CGM);
  auto ivarList = builder.beginStruct();
  auto countSlot = ivarList.addPlaceholder();
  auto ivars = ivarList.beginArray(ObjCTypes.IvarTy);

  for (const ObjCIvarDecl *IVD = OID->all_declared_ivar_begin();
       IVD; IVD = IVD->getNextIvar()) {
    // Ignore unnamed bit-fields.
    if (!IVD->getDeclName())
      continue;

    auto ivar = ivars.beginStruct(ObjCTypes.IvarTy);
    ivar.add(GetMethodVarName(IVD->getIdentifier()));
    ivar.add(GetMethodVarType(IVD));
    ivar.addInt(ObjCTypes.IntTy, ComputeIvarBaseOffset(CGM, OID, IVD));
    ivar.finishAndAddTo(ivars);
  }

  // Return null for empty list.
  auto count = ivars.size();
  if (count == 0) {
    ivars.abandon();
    ivarList.abandon();
    return llvm::Constant::getNullValue(ObjCTypes.IvarListPtrTy);
  }

  ivars.finishAndAddTo(ivarList);
  ivarList.fillPlaceholderWithInt(countSlot, ObjCTypes.IntTy, count);

  llvm::GlobalVariable *GV;
  GV = CreateMetadataVar("OBJC_INSTANCE_VARIABLES_" + ID->getName(), ivarList,
                         "__OBJC,__instance_vars,regular,no_dead_strip",
                         CGM.getPointerAlign(), true);
  return GV;
}

/// Build a struct objc_method_description constant for the given method.
///
/// struct objc_method_description {
///   SEL method_name;
///   char *method_types;
/// };
void CGObjCMac::emitMethodDescriptionConstant(ConstantArrayBuilder &builder,
                                              const ObjCMethodDecl *MD) {
  auto description = builder.beginStruct(ObjCTypes.MethodDescriptionTy);
  description.add(GetMethodVarName(MD->getSelector()));
  description.add(GetMethodVarType(MD));
  description.finishAndAddTo(builder);
}

/// Build a struct objc_method constant for the given method.
///
/// struct objc_method {
///   SEL method_name;
///   char *method_types;
///   void *method;
/// };
void CGObjCMac::emitMethodConstant(ConstantArrayBuilder &builder,
                                   const ObjCMethodDecl *MD) {
  llvm::Function *fn = GetMethodDefinition(MD);
  assert(fn && "no definition registered for method");

  auto method = builder.beginStruct(ObjCTypes.MethodTy);
  method.add(GetMethodVarName(MD->getSelector()));
  method.add(GetMethodVarType(MD));
  method.add(fn);
  method.finishAndAddTo(builder);
}

/// Build a struct objc_method_list or struct objc_method_description_list,
/// as appropriate.
///
/// struct objc_method_list {
///   struct objc_method_list *obsolete;
///   int count;
///   struct objc_method methods_list[count];
/// };
///
/// struct objc_method_description_list {
///   int count;
///   struct objc_method_description list[count];
/// };
llvm::Constant *CGObjCMac::emitMethodList(Twine name, MethodListType MLT,
                                 ArrayRef<const ObjCMethodDecl *> methods) {
  StringRef prefix;
  StringRef section;
  bool forProtocol = false;
  switch (MLT) {
  case MethodListType::CategoryInstanceMethods:
    prefix = "OBJC_CATEGORY_INSTANCE_METHODS_";
    section = "__OBJC,__cat_inst_meth,regular,no_dead_strip";
    forProtocol = false;
    break;
  case MethodListType::CategoryClassMethods:
    prefix = "OBJC_CATEGORY_CLASS_METHODS_";
    section = "__OBJC,__cat_cls_meth,regular,no_dead_strip";
    forProtocol = false;
    break;
  case MethodListType::InstanceMethods:
    prefix = "OBJC_INSTANCE_METHODS_";
    section = "__OBJC,__inst_meth,regular,no_dead_strip";
    forProtocol = false;
    break;
  case MethodListType::ClassMethods:
    prefix = "OBJC_CLASS_METHODS_";
    section = "__OBJC,__cls_meth,regular,no_dead_strip";
    forProtocol = false;
    break;
  case MethodListType::ProtocolInstanceMethods:
    prefix = "OBJC_PROTOCOL_INSTANCE_METHODS_";
    section = "__OBJC,__cat_inst_meth,regular,no_dead_strip";
    forProtocol = true;
    break;
  case MethodListType::ProtocolClassMethods:
    prefix = "OBJC_PROTOCOL_CLASS_METHODS_";
    section = "__OBJC,__cat_cls_meth,regular,no_dead_strip";
    forProtocol = true;
    break;
  case MethodListType::OptionalProtocolInstanceMethods:
    prefix = "OBJC_PROTOCOL_INSTANCE_METHODS_OPT_";
    section = "__OBJC,__cat_inst_meth,regular,no_dead_strip";
    forProtocol = true;
    break;
  case MethodListType::OptionalProtocolClassMethods:
    prefix = "OBJC_PROTOCOL_CLASS_METHODS_OPT_";
    section = "__OBJC,__cat_cls_meth,regular,no_dead_strip";
    forProtocol = true;
    break;
  }

  // Return null for empty list.
  if (methods.empty())
    return llvm::Constant::getNullValue(forProtocol
                                        ? ObjCTypes.MethodDescriptionListPtrTy
                                        : ObjCTypes.MethodListPtrTy);

  // For protocols, this is an objc_method_description_list, which has
  // a slightly different structure.
  if (forProtocol) {
    ConstantInitBuilder builder(CGM);
    auto values = builder.beginStruct();
    values.addInt(ObjCTypes.IntTy, methods.size());
    auto methodArray = values.beginArray(ObjCTypes.MethodDescriptionTy);
    for (auto MD : methods) {
      emitMethodDescriptionConstant(methodArray, MD);
    }
    methodArray.finishAndAddTo(values);

    llvm::GlobalVariable *GV = CreateMetadataVar(prefix + name, values, section,
                                                 CGM.getPointerAlign(), true);
    return GV;
  }

  // Otherwise, it's an objc_method_list.
  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct();
  values.addNullPointer(ObjCTypes.Int8PtrTy);
  values.addInt(ObjCTypes.IntTy, methods.size());
  auto methodArray = values.beginArray(ObjCTypes.MethodTy);
  for (auto MD : methods) {
    if (!MD->isDirectMethod())
      emitMethodConstant(methodArray, MD);
  }
  methodArray.finishAndAddTo(values);

  llvm::GlobalVariable *GV = CreateMetadataVar(prefix + name, values, section,
                                               CGM.getPointerAlign(), true);
  return GV;
}

llvm::Function *CGObjCCommonMac::GenerateMethod(const ObjCMethodDecl *OMD,
                                                const ObjCContainerDecl *CD) {
  llvm::Function *Method;

  if (OMD->isDirectMethod()) {
    Method = GenerateDirectMethod(OMD, CD);
  } else {
    auto Name = getSymbolNameForMethod(OMD);

    CodeGenTypes &Types = CGM.getTypes();
    llvm::FunctionType *MethodTy =
        Types.GetFunctionType(Types.arrangeObjCMethodDeclaration(OMD));
    Method =
        llvm::Function::Create(MethodTy, llvm::GlobalValue::InternalLinkage,
                               Name, &CGM.getModule());
  }

  MethodDefinitions.insert(std::make_pair(OMD, Method));

  return Method;
}

llvm::Function *
CGObjCCommonMac::GenerateDirectMethod(const ObjCMethodDecl *OMD,
                                      const ObjCContainerDecl *CD) {
  auto *COMD = OMD->getCanonicalDecl();
  auto I = DirectMethodDefinitions.find(COMD);
  llvm::Function *OldFn = nullptr, *Fn = nullptr;

  if (I != DirectMethodDefinitions.end()) {
    // Objective-C allows for the declaration and implementation types
    // to differ slightly.
    //
    // If we're being asked for the Function associated for a method
    // implementation, a previous value might have been cached
    // based on the type of the canonical declaration.
    //
    // If these do not match, then we'll replace this function with
    // a new one that has the proper type below.
    if (!OMD->getBody() || COMD->getReturnType() == OMD->getReturnType())
      return I->second;
    OldFn = I->second;
  }

  CodeGenTypes &Types = CGM.getTypes();
  llvm::FunctionType *MethodTy =
    Types.GetFunctionType(Types.arrangeObjCMethodDeclaration(OMD));

  if (OldFn) {
    Fn = llvm::Function::Create(MethodTy, llvm::GlobalValue::ExternalLinkage,
                                "", &CGM.getModule());
    Fn->takeName(OldFn);
    OldFn->replaceAllUsesWith(Fn);
    OldFn->eraseFromParent();

    // Replace the cached function in the map.
    I->second = Fn;
  } else {
    auto Name = getSymbolNameForMethod(OMD, /*include category*/ false);

    Fn = llvm::Function::Create(MethodTy, llvm::GlobalValue::ExternalLinkage,
                                Name, &CGM.getModule());
    DirectMethodDefinitions.insert(std::make_pair(COMD, Fn));
  }

  return Fn;
}

void CGObjCCommonMac::GenerateDirectMethodPrologue(
    CodeGenFunction &CGF, llvm::Function *Fn, const ObjCMethodDecl *OMD,
    const ObjCContainerDecl *CD) {
  auto &Builder = CGF.Builder;
  bool ReceiverCanBeNull = true;
  auto selfAddr = CGF.GetAddrOfLocalVar(OMD->getSelfDecl());
  auto selfValue = Builder.CreateLoad(selfAddr);

  // Generate:
  //
  // /* for class methods only to force class lazy initialization */
  // self = [self self];
  //
  // /* unless the receiver is never NULL */
  // if (self == nil) {
  //     return (ReturnType){ };
  // }
  //
  // _cmd = @selector(...)
  // ...

  if (OMD->isClassMethod()) {
    const ObjCInterfaceDecl *OID = cast<ObjCInterfaceDecl>(CD);
    assert(OID &&
           "GenerateDirectMethod() should be called with the Class Interface");
    Selector SelfSel = GetNullarySelector("self", CGM.getContext());
    auto ResultType = CGF.getContext().getObjCIdType();
    RValue result;
    CallArgList Args;

    // TODO: If this method is inlined, the caller might know that `self` is
    // already initialized; for example, it might be an ordinary Objective-C
    // method which always receives an initialized `self`, or it might have just
    // forced initialization on its own.
    //
    // We should find a way to eliminate this unnecessary initialization in such
    // cases in LLVM.
    result = GeneratePossiblySpecializedMessageSend(
        CGF, ReturnValueSlot(), ResultType, SelfSel, selfValue, Args, OID,
        nullptr, true);
    Builder.CreateStore(result.getScalarVal(), selfAddr);

    // Nullable `Class` expressions cannot be messaged with a direct method
    // so the only reason why the receive can be null would be because
    // of weak linking.
    ReceiverCanBeNull = isWeakLinkedClass(OID);
  }

  if (ReceiverCanBeNull) {
    llvm::BasicBlock *SelfIsNilBlock =
        CGF.createBasicBlock("objc_direct_method.self_is_nil");
    llvm::BasicBlock *ContBlock =
        CGF.createBasicBlock("objc_direct_method.cont");

    // if (self == nil) {
    auto selfTy = cast<llvm::PointerType>(selfValue->getType());
    auto Zero = llvm::ConstantPointerNull::get(selfTy);

    llvm::MDBuilder MDHelper(CGM.getLLVMContext());
    Builder.CreateCondBr(Builder.CreateICmpEQ(selfValue, Zero), SelfIsNilBlock,
                         ContBlock, MDHelper.createUnlikelyBranchWeights());

    CGF.EmitBlock(SelfIsNilBlock);

    //   return (ReturnType){ };
    auto retTy = OMD->getReturnType();
    Builder.SetInsertPoint(SelfIsNilBlock);
    if (!retTy->isVoidType()) {
      CGF.EmitNullInitialization(CGF.ReturnValue, retTy);
    }
    CGF.EmitBranchThroughCleanup(CGF.ReturnBlock);
    // }

    // rest of the body
    CGF.EmitBlock(ContBlock);
    Builder.SetInsertPoint(ContBlock);
  }

  // only synthesize _cmd if it's referenced
  if (OMD->getCmdDecl()->isUsed()) {
    // `_cmd` is not a parameter to direct methods, so storage must be
    // explicitly declared for it.
    CGF.EmitVarDecl(*OMD->getCmdDecl());
    Builder.CreateStore(GetSelector(CGF, OMD),
                        CGF.GetAddrOfLocalVar(OMD->getCmdDecl()));
  }
}

llvm::GlobalVariable *CGObjCCommonMac::CreateMetadataVar(Twine Name,
                                               ConstantStructBuilder &Init,
                                                         StringRef Section,
                                                         CharUnits Align,
                                                         bool AddToUsed) {
  llvm::GlobalValue::LinkageTypes LT =
      getLinkageTypeForObjCMetadata(CGM, Section);
  llvm::GlobalVariable *GV =
      Init.finishAndCreateGlobal(Name, Align, /*constant*/ false, LT);
  if (!Section.empty())
    GV->setSection(Section);
  if (AddToUsed)
    CGM.addCompilerUsedGlobal(GV);
  return GV;
}

llvm::GlobalVariable *CGObjCCommonMac::CreateMetadataVar(Twine Name,
                                                         llvm::Constant *Init,
                                                         StringRef Section,
                                                         CharUnits Align,
                                                         bool AddToUsed) {
  llvm::Type *Ty = Init->getType();
  llvm::GlobalValue::LinkageTypes LT =
      getLinkageTypeForObjCMetadata(CGM, Section);
  llvm::GlobalVariable *GV =
      new llvm::GlobalVariable(CGM.getModule(), Ty, false, LT, Init, Name);
  if (!Section.empty())
    GV->setSection(Section);
  GV->setAlignment(Align.getAsAlign());
  if (AddToUsed)
    CGM.addCompilerUsedGlobal(GV);
  return GV;
}

llvm::GlobalVariable *
CGObjCCommonMac::CreateCStringLiteral(StringRef Name, ObjCLabelType Type,
                                      bool ForceNonFragileABI,
                                      bool NullTerminate) {
  StringRef Label;
  switch (Type) {
  case ObjCLabelType::ClassName:     Label = "OBJC_CLASS_NAME_"; break;
  case ObjCLabelType::MethodVarName: Label = "OBJC_METH_VAR_NAME_"; break;
  case ObjCLabelType::MethodVarType: Label = "OBJC_METH_VAR_TYPE_"; break;
  case ObjCLabelType::PropertyName:  Label = "OBJC_PROP_NAME_ATTR_"; break;
  }

  bool NonFragile = ForceNonFragileABI || isNonFragileABI();

  StringRef Section;
  switch (Type) {
  case ObjCLabelType::ClassName:
    Section = NonFragile ? "__TEXT,__objc_classname,cstring_literals"
                         : "__TEXT,__cstring,cstring_literals";
    break;
  case ObjCLabelType::MethodVarName:
    Section = NonFragile ? "__TEXT,__objc_methname,cstring_literals"
                         : "__TEXT,__cstring,cstring_literals";
    break;
  case ObjCLabelType::MethodVarType:
    Section = NonFragile ? "__TEXT,__objc_methtype,cstring_literals"
                         : "__TEXT,__cstring,cstring_literals";
    break;
  case ObjCLabelType::PropertyName:
    Section = NonFragile ? "__TEXT,__objc_methname,cstring_literals"
                         : "__TEXT,__cstring,cstring_literals";
    break;
  }

  llvm::Constant *Value =
      llvm::ConstantDataArray::getString(VMContext, Name, NullTerminate);
  llvm::GlobalVariable *GV =
      new llvm::GlobalVariable(CGM.getModule(), Value->getType(),
                               /*isConstant=*/true,
                               llvm::GlobalValue::PrivateLinkage, Value, Label);
  if (CGM.getTriple().isOSBinFormatMachO())
    GV->setSection(Section);
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(CharUnits::One().getAsAlign());
  CGM.addCompilerUsedGlobal(GV);

  return GV;
}

llvm::Function *CGObjCMac::ModuleInitFunction() {
  // Abuse this interface function as a place to finalize.
  FinishModule();
  return nullptr;
}

llvm::FunctionCallee CGObjCMac::GetPropertyGetFunction() {
  return ObjCTypes.getGetPropertyFn();
}

llvm::FunctionCallee CGObjCMac::GetPropertySetFunction() {
  return ObjCTypes.getSetPropertyFn();
}

llvm::FunctionCallee CGObjCMac::GetOptimizedPropertySetFunction(bool atomic,
                                                                bool copy) {
  return ObjCTypes.getOptimizedSetPropertyFn(atomic, copy);
}

llvm::FunctionCallee CGObjCMac::GetGetStructFunction() {
  return ObjCTypes.getCopyStructFn();
}

llvm::FunctionCallee CGObjCMac::GetSetStructFunction() {
  return ObjCTypes.getCopyStructFn();
}

llvm::FunctionCallee CGObjCMac::GetCppAtomicObjectGetFunction() {
  return ObjCTypes.getCppAtomicObjectFunction();
}

llvm::FunctionCallee CGObjCMac::GetCppAtomicObjectSetFunction() {
  return ObjCTypes.getCppAtomicObjectFunction();
}

llvm::FunctionCallee CGObjCMac::EnumerationMutationFunction() {
  return ObjCTypes.getEnumerationMutationFn();
}

void CGObjCMac::EmitTryStmt(CodeGenFunction &CGF, const ObjCAtTryStmt &S) {
  return EmitTryOrSynchronizedStmt(CGF, S);
}

void CGObjCMac::EmitSynchronizedStmt(CodeGenFunction &CGF,
                                     const ObjCAtSynchronizedStmt &S) {
  return EmitTryOrSynchronizedStmt(CGF, S);
}

namespace {
  struct PerformFragileFinally final : EHScopeStack::Cleanup {
    const Stmt &S;
    Address SyncArgSlot;
    Address CallTryExitVar;
    Address ExceptionData;
    ObjCTypesHelper &ObjCTypes;
    PerformFragileFinally(const Stmt *S,
                          Address SyncArgSlot,
                          Address CallTryExitVar,
                          Address ExceptionData,
                          ObjCTypesHelper *ObjCTypes)
      : S(*S), SyncArgSlot(SyncArgSlot), CallTryExitVar(CallTryExitVar),
        ExceptionData(ExceptionData), ObjCTypes(*ObjCTypes) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      // Check whether we need to call objc_exception_try_exit.
      // In optimized code, this branch will always be folded.
      llvm::BasicBlock *FinallyCallExit =
        CGF.createBasicBlock("finally.call_exit");
      llvm::BasicBlock *FinallyNoCallExit =
        CGF.createBasicBlock("finally.no_call_exit");
      CGF.Builder.CreateCondBr(CGF.Builder.CreateLoad(CallTryExitVar),
                               FinallyCallExit, FinallyNoCallExit);

      CGF.EmitBlock(FinallyCallExit);
      CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionTryExitFn(),
                                  ExceptionData.emitRawPointer(CGF));

      CGF.EmitBlock(FinallyNoCallExit);

      if (isa<ObjCAtTryStmt>(S)) {
        if (const ObjCAtFinallyStmt* FinallyStmt =
              cast<ObjCAtTryStmt>(S).getFinallyStmt()) {
          // Don't try to do the @finally if this is an EH cleanup.
          if (flags.isForEHCleanup()) return;

          // Save the current cleanup destination in case there's
          // control flow inside the finally statement.
          llvm::Value *CurCleanupDest =
            CGF.Builder.CreateLoad(CGF.getNormalCleanupDestSlot());

          CGF.EmitStmt(FinallyStmt->getFinallyBody());

          if (CGF.HaveInsertPoint()) {
            CGF.Builder.CreateStore(CurCleanupDest,
                                    CGF.getNormalCleanupDestSlot());
          } else {
            // Currently, the end of the cleanup must always exist.
            CGF.EnsureInsertPoint();
          }
        }
      } else {
        // Emit objc_sync_exit(expr); as finally's sole statement for
        // @synchronized.
        llvm::Value *SyncArg = CGF.Builder.CreateLoad(SyncArgSlot);
        CGF.EmitNounwindRuntimeCall(ObjCTypes.getSyncExitFn(), SyncArg);
      }
    }
  };

  class FragileHazards {
    CodeGenFunction &CGF;
    SmallVector<llvm::Value*, 20> Locals;
    llvm::DenseSet<llvm::BasicBlock*> BlocksBeforeTry;

    llvm::InlineAsm *ReadHazard;
    llvm::InlineAsm *WriteHazard;

    llvm::FunctionType *GetAsmFnType();

    void collectLocals();
    void emitReadHazard(CGBuilderTy &Builder);

  public:
    FragileHazards(CodeGenFunction &CGF);

    void emitWriteHazard();
    void emitHazardsInNewBlocks();
  };
} // end anonymous namespace

/// Create the fragile-ABI read and write hazards based on the current
/// state of the function, which is presumed to be immediately prior
/// to a @try block.  These hazards are used to maintain correct
/// semantics in the face of optimization and the fragile ABI's
/// cavalier use of setjmp/longjmp.
FragileHazards::FragileHazards(CodeGenFunction &CGF) : CGF(CGF) {
  collectLocals();

  if (Locals.empty()) return;

  // Collect all the blocks in the function.
  for (llvm::Function::iterator
         I = CGF.CurFn->begin(), E = CGF.CurFn->end(); I != E; ++I)
    BlocksBeforeTry.insert(&*I);

  llvm::FunctionType *AsmFnTy = GetAsmFnType();

  // Create a read hazard for the allocas.  This inhibits dead-store
  // optimizations and forces the values to memory.  This hazard is
  // inserted before any 'throwing' calls in the protected scope to
  // reflect the possibility that the variables might be read from the
  // catch block if the call throws.
  {
    std::string Constraint;
    for (unsigned I = 0, E = Locals.size(); I != E; ++I) {
      if (I) Constraint += ',';
      Constraint += "*m";
    }

    ReadHazard = llvm::InlineAsm::get(AsmFnTy, "", Constraint, true, false);
  }

  // Create a write hazard for the allocas.  This inhibits folding
  // loads across the hazard.  This hazard is inserted at the
  // beginning of the catch path to reflect the possibility that the
  // variables might have been written within the protected scope.
  {
    std::string Constraint;
    for (unsigned I = 0, E = Locals.size(); I != E; ++I) {
      if (I) Constraint += ',';
      Constraint += "=*m";
    }

    WriteHazard = llvm::InlineAsm::get(AsmFnTy, "", Constraint, true, false);
  }
}

/// Emit a write hazard at the current location.
void FragileHazards::emitWriteHazard() {
  if (Locals.empty()) return;

  llvm::CallInst *Call = CGF.EmitNounwindRuntimeCall(WriteHazard, Locals);
  for (auto Pair : llvm::enumerate(Locals))
    Call->addParamAttr(Pair.index(), llvm::Attribute::get(
        CGF.getLLVMContext(), llvm::Attribute::ElementType,
        cast<llvm::AllocaInst>(Pair.value())->getAllocatedType()));
}

void FragileHazards::emitReadHazard(CGBuilderTy &Builder) {
  assert(!Locals.empty());
  llvm::CallInst *call = Builder.CreateCall(ReadHazard, Locals);
  call->setDoesNotThrow();
  call->setCallingConv(CGF.getRuntimeCC());
  for (auto Pair : llvm::enumerate(Locals))
    call->addParamAttr(Pair.index(), llvm::Attribute::get(
        Builder.getContext(), llvm::Attribute::ElementType,
        cast<llvm::AllocaInst>(Pair.value())->getAllocatedType()));
}

/// Emit read hazards in all the protected blocks, i.e. all the blocks
/// which have been inserted since the beginning of the try.
void FragileHazards::emitHazardsInNewBlocks() {
  if (Locals.empty()) return;

  CGBuilderTy Builder(CGF, CGF.getLLVMContext());

  // Iterate through all blocks, skipping those prior to the try.
  for (llvm::Function::iterator
         FI = CGF.CurFn->begin(), FE = CGF.CurFn->end(); FI != FE; ++FI) {
    llvm::BasicBlock &BB = *FI;
    if (BlocksBeforeTry.count(&BB)) continue;

    // Walk through all the calls in the block.
    for (llvm::BasicBlock::iterator
           BI = BB.begin(), BE = BB.end(); BI != BE; ++BI) {
      llvm::Instruction &I = *BI;

      // Ignore instructions that aren't non-intrinsic calls.
      // These are the only calls that can possibly call longjmp.
      if (!isa<llvm::CallInst>(I) && !isa<llvm::InvokeInst>(I))
        continue;
      if (isa<llvm::IntrinsicInst>(I))
        continue;

      // Ignore call sites marked nounwind.  This may be questionable,
      // since 'nounwind' doesn't necessarily mean 'does not call longjmp'.
      if (cast<llvm::CallBase>(I).doesNotThrow())
        continue;

      // Insert a read hazard before the call.  This will ensure that
      // any writes to the locals are performed before making the
      // call.  If the call throws, then this is sufficient to
      // guarantee correctness as long as it doesn't also write to any
      // locals.
      Builder.SetInsertPoint(&BB, BI);
      emitReadHazard(Builder);
    }
  }
}

static void addIfPresent(llvm::DenseSet<llvm::Value*> &S, Address V) {
  if (V.isValid())
    if (llvm::Value *Ptr = V.getBasePointer())
      S.insert(Ptr);
}

void FragileHazards::collectLocals() {
  // Compute a set of allocas to ignore.
  llvm::DenseSet<llvm::Value*> AllocasToIgnore;
  addIfPresent(AllocasToIgnore, CGF.ReturnValue);
  addIfPresent(AllocasToIgnore, CGF.NormalCleanupDest);

  // Collect all the allocas currently in the function.  This is
  // probably way too aggressive.
  llvm::BasicBlock &Entry = CGF.CurFn->getEntryBlock();
  for (llvm::BasicBlock::iterator
         I = Entry.begin(), E = Entry.end(); I != E; ++I)
    if (isa<llvm::AllocaInst>(*I) && !AllocasToIgnore.count(&*I))
      Locals.push_back(&*I);
}

llvm::FunctionType *FragileHazards::GetAsmFnType() {
  SmallVector<llvm::Type *, 16> tys(Locals.size());
  for (unsigned i = 0, e = Locals.size(); i != e; ++i)
    tys[i] = Locals[i]->getType();
  return llvm::FunctionType::get(CGF.VoidTy, tys, false);
}

/*

  Objective-C setjmp-longjmp (sjlj) Exception Handling
  --

  A catch buffer is a setjmp buffer plus:
    - a pointer to the exception that was caught
    - a pointer to the previous exception data buffer
    - two pointers of reserved storage
  Therefore catch buffers form a stack, with a pointer to the top
  of the stack kept in thread-local storage.

  objc_exception_try_enter pushes a catch buffer onto the EH stack.
  objc_exception_try_exit pops the given catch buffer, which is
    required to be the top of the EH stack.
  objc_exception_throw pops the top of the EH stack, writes the
    thrown exception into the appropriate field, and longjmps
    to the setjmp buffer.  It crashes the process (with a printf
    and an abort()) if there are no catch buffers on the stack.
  objc_exception_extract just reads the exception pointer out of the
    catch buffer.

  There's no reason an implementation couldn't use a light-weight
  setjmp here --- something like __builtin_setjmp, but API-compatible
  with the heavyweight setjmp.  This will be more important if we ever
  want to implement correct ObjC/C++ exception interactions for the
  fragile ABI.

  Note that for this use of setjmp/longjmp to be correct in the presence of
  optimization, we use inline assembly on the set of local variables to force
  flushing locals to memory immediately before any protected calls and to
  inhibit optimizing locals across the setjmp->catch edge.

  The basic framework for a @try-catch-finally is as follows:
  {
  objc_exception_data d;
  id _rethrow = null;
  bool _call_try_exit = true;

  objc_exception_try_enter(&d);
  if (!setjmp(d.jmp_buf)) {
  ... try body ...
  } else {
  // exception path
  id _caught = objc_exception_extract(&d);

  // enter new try scope for handlers
  if (!setjmp(d.jmp_buf)) {
  ... match exception and execute catch blocks ...

  // fell off end, rethrow.
  _rethrow = _caught;
  ... jump-through-finally to finally_rethrow ...
  } else {
  // exception in catch block
  _rethrow = objc_exception_extract(&d);
  _call_try_exit = false;
  ... jump-through-finally to finally_rethrow ...
  }
  }
  ... jump-through-finally to finally_end ...

  finally:
  if (_call_try_exit)
  objc_exception_try_exit(&d);

  ... finally block ....
  ... dispatch to finally destination ...

  finally_rethrow:
  objc_exception_throw(_rethrow);

  finally_end:
  }

  This framework differs slightly from the one gcc uses, in that gcc
  uses _rethrow to determine if objc_exception_try_exit should be called
  and if the object should be rethrown. This breaks in the face of
  throwing nil and introduces unnecessary branches.

  We specialize this framework for a few particular circumstances:

  - If there are no catch blocks, then we avoid emitting the second
  exception handling context.

  - If there is a catch-all catch block (i.e. @catch(...) or @catch(id
  e)) we avoid emitting the code to rethrow an uncaught exception.

  - FIXME: If there is no @finally block we can do a few more
  simplifications.

  Rethrows and Jumps-Through-Finally
  --

  '@throw;' is supported by pushing the currently-caught exception
  onto ObjCEHStack while the @catch blocks are emitted.

  Branches through the @finally block are handled with an ordinary
  normal cleanup.  We do not register an EH cleanup; fragile-ABI ObjC
  exceptions are not compatible with C++ exceptions, and this is
  hardly the only place where this will go wrong.

  @synchronized(expr) { stmt; } is emitted as if it were:
    id synch_value = expr;
    objc_sync_enter(synch_value);
    @try { stmt; } @finally { objc_sync_exit(synch_value); }
*/

void CGObjCMac::EmitTryOrSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                                          const Stmt &S) {
  bool isTry = isa<ObjCAtTryStmt>(S);

  // A destination for the fall-through edges of the catch handlers to
  // jump to.
  CodeGenFunction::JumpDest FinallyEnd =
    CGF.getJumpDestInCurrentScope("finally.end");

  // A destination for the rethrow edge of the catch handlers to jump
  // to.
  CodeGenFunction::JumpDest FinallyRethrow =
    CGF.getJumpDestInCurrentScope("finally.rethrow");

  // For @synchronized, call objc_sync_enter(sync.expr). The
  // evaluation of the expression must occur before we enter the
  // @synchronized.  We can't avoid a temp here because we need the
  // value to be preserved.  If the backend ever does liveness
  // correctly after setjmp, this will be unnecessary.
  Address SyncArgSlot = Address::invalid();
  if (!isTry) {
    llvm::Value *SyncArg =
      CGF.EmitScalarExpr(cast<ObjCAtSynchronizedStmt>(S).getSynchExpr());
    SyncArg = CGF.Builder.CreateBitCast(SyncArg, ObjCTypes.ObjectPtrTy);
    CGF.EmitNounwindRuntimeCall(ObjCTypes.getSyncEnterFn(), SyncArg);

    SyncArgSlot = CGF.CreateTempAlloca(SyncArg->getType(),
                                       CGF.getPointerAlign(), "sync.arg");
    CGF.Builder.CreateStore(SyncArg, SyncArgSlot);
  }

  // Allocate memory for the setjmp buffer.  This needs to be kept
  // live throughout the try and catch blocks.
  Address ExceptionData = CGF.CreateTempAlloca(ObjCTypes.ExceptionDataTy,
                                               CGF.getPointerAlign(),
                                               "exceptiondata.ptr");

  // Create the fragile hazards.  Note that this will not capture any
  // of the allocas required for exception processing, but will
  // capture the current basic block (which extends all the way to the
  // setjmp call) as "before the @try".
  FragileHazards Hazards(CGF);

  // Create a flag indicating whether the cleanup needs to call
  // objc_exception_try_exit.  This is true except when
  //   - no catches match and we're branching through the cleanup
  //     just to rethrow the exception, or
  //   - a catch matched and we're falling out of the catch handler.
  // The setjmp-safety rule here is that we should always store to this
  // variable in a place that dominates the branch through the cleanup
  // without passing through any setjmps.
  Address CallTryExitVar = CGF.CreateTempAlloca(CGF.Builder.getInt1Ty(),
                                                CharUnits::One(),
                                                "_call_try_exit");

  // A slot containing the exception to rethrow.  Only needed when we
  // have both a @catch and a @finally.
  Address PropagatingExnVar = Address::invalid();

  // Push a normal cleanup to leave the try scope.
  CGF.EHStack.pushCleanup<PerformFragileFinally>(NormalAndEHCleanup, &S,
                                                 SyncArgSlot,
                                                 CallTryExitVar,
                                                 ExceptionData,
                                                 &ObjCTypes);

  // Enter a try block:
  //  - Call objc_exception_try_enter to push ExceptionData on top of
  //    the EH stack.
  CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionTryEnterFn(),
                              ExceptionData.emitRawPointer(CGF));

  //  - Call setjmp on the exception data buffer.
  llvm::Constant *Zero = llvm::ConstantInt::get(CGF.Builder.getInt32Ty(), 0);
  llvm::Value *GEPIndexes[] = { Zero, Zero, Zero };
  llvm::Value *SetJmpBuffer = CGF.Builder.CreateGEP(
      ObjCTypes.ExceptionDataTy, ExceptionData.emitRawPointer(CGF), GEPIndexes,
      "setjmp_buffer");
  llvm::CallInst *SetJmpResult = CGF.EmitNounwindRuntimeCall(
      ObjCTypes.getSetJmpFn(), SetJmpBuffer, "setjmp_result");
  SetJmpResult->setCanReturnTwice();

  // If setjmp returned 0, enter the protected block; otherwise,
  // branch to the handler.
  llvm::BasicBlock *TryBlock = CGF.createBasicBlock("try");
  llvm::BasicBlock *TryHandler = CGF.createBasicBlock("try.handler");
  llvm::Value *DidCatch =
    CGF.Builder.CreateIsNotNull(SetJmpResult, "did_catch_exception");
  CGF.Builder.CreateCondBr(DidCatch, TryHandler, TryBlock);

  // Emit the protected block.
  CGF.EmitBlock(TryBlock);
  CGF.Builder.CreateStore(CGF.Builder.getTrue(), CallTryExitVar);
  CGF.EmitStmt(isTry ? cast<ObjCAtTryStmt>(S).getTryBody()
                     : cast<ObjCAtSynchronizedStmt>(S).getSynchBody());

  CGBuilderTy::InsertPoint TryFallthroughIP = CGF.Builder.saveAndClearIP();

  // Emit the exception handler block.
  CGF.EmitBlock(TryHandler);

  // Don't optimize loads of the in-scope locals across this point.
  Hazards.emitWriteHazard();

  // For a @synchronized (or a @try with no catches), just branch
  // through the cleanup to the rethrow block.
  if (!isTry || !cast<ObjCAtTryStmt>(S).getNumCatchStmts()) {
    // Tell the cleanup not to re-pop the exit.
    CGF.Builder.CreateStore(CGF.Builder.getFalse(), CallTryExitVar);
    CGF.EmitBranchThroughCleanup(FinallyRethrow);

  // Otherwise, we have to match against the caught exceptions.
  } else {
    // Retrieve the exception object.  We may emit multiple blocks but
    // nothing can cross this so the value is already in SSA form.
    llvm::CallInst *Caught = CGF.EmitNounwindRuntimeCall(
        ObjCTypes.getExceptionExtractFn(), ExceptionData.emitRawPointer(CGF),
        "caught");

    // Push the exception to rethrow onto the EH value stack for the
    // benefit of any @throws in the handlers.
    CGF.ObjCEHValueStack.push_back(Caught);

    const ObjCAtTryStmt* AtTryStmt = cast<ObjCAtTryStmt>(&S);

    bool HasFinally = (AtTryStmt->getFinallyStmt() != nullptr);

    llvm::BasicBlock *CatchBlock = nullptr;
    llvm::BasicBlock *CatchHandler = nullptr;
    if (HasFinally) {
      // Save the currently-propagating exception before
      // objc_exception_try_enter clears the exception slot.
      PropagatingExnVar = CGF.CreateTempAlloca(Caught->getType(),
                                               CGF.getPointerAlign(),
                                               "propagating_exception");
      CGF.Builder.CreateStore(Caught, PropagatingExnVar);

      // Enter a new exception try block (in case a @catch block
      // throws an exception).
      CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionTryEnterFn(),
                                  ExceptionData.emitRawPointer(CGF));

      llvm::CallInst *SetJmpResult =
        CGF.EmitNounwindRuntimeCall(ObjCTypes.getSetJmpFn(),
                                    SetJmpBuffer, "setjmp.result");
      SetJmpResult->setCanReturnTwice();

      llvm::Value *Threw =
        CGF.Builder.CreateIsNotNull(SetJmpResult, "did_catch_exception");

      CatchBlock = CGF.createBasicBlock("catch");
      CatchHandler = CGF.createBasicBlock("catch_for_catch");
      CGF.Builder.CreateCondBr(Threw, CatchHandler, CatchBlock);

      CGF.EmitBlock(CatchBlock);
    }

    CGF.Builder.CreateStore(CGF.Builder.getInt1(HasFinally), CallTryExitVar);

    // Handle catch list. As a special case we check if everything is
    // matched and avoid generating code for falling off the end if
    // so.
    bool AllMatched = false;
    for (const ObjCAtCatchStmt *CatchStmt : AtTryStmt->catch_stmts()) {
      const VarDecl *CatchParam = CatchStmt->getCatchParamDecl();
      const ObjCObjectPointerType *OPT = nullptr;

      // catch(...) always matches.
      if (!CatchParam) {
        AllMatched = true;
      } else {
        OPT = CatchParam->getType()->getAs<ObjCObjectPointerType>();

        // catch(id e) always matches under this ABI, since only
        // ObjC exceptions end up here in the first place.
        // FIXME: For the time being we also match id<X>; this should
        // be rejected by Sema instead.
        if (OPT && (OPT->isObjCIdType() || OPT->isObjCQualifiedIdType()))
          AllMatched = true;
      }

      // If this is a catch-all, we don't need to test anything.
      if (AllMatched) {
        CodeGenFunction::RunCleanupsScope CatchVarCleanups(CGF);

        if (CatchParam) {
          CGF.EmitAutoVarDecl(*CatchParam);
          assert(CGF.HaveInsertPoint() && "DeclStmt destroyed insert point?");

          // These types work out because ConvertType(id) == i8*.
          EmitInitOfCatchParam(CGF, Caught, CatchParam);
        }

        CGF.EmitStmt(CatchStmt->getCatchBody());

        // The scope of the catch variable ends right here.
        CatchVarCleanups.ForceCleanup();

        CGF.EmitBranchThroughCleanup(FinallyEnd);
        break;
      }

      assert(OPT && "Unexpected non-object pointer type in @catch");
      const ObjCObjectType *ObjTy = OPT->getObjectType();

      // FIXME: @catch (Class c) ?
      ObjCInterfaceDecl *IDecl = ObjTy->getInterface();
      assert(IDecl && "Catch parameter must have Objective-C type!");

      // Check if the @catch block matches the exception object.
      llvm::Value *Class = EmitClassRef(CGF, IDecl);

      llvm::Value *matchArgs[] = { Class, Caught };
      llvm::CallInst *Match =
        CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionMatchFn(),
                                    matchArgs, "match");

      llvm::BasicBlock *MatchedBlock = CGF.createBasicBlock("match");
      llvm::BasicBlock *NextCatchBlock = CGF.createBasicBlock("catch.next");

      CGF.Builder.CreateCondBr(CGF.Builder.CreateIsNotNull(Match, "matched"),
                               MatchedBlock, NextCatchBlock);

      // Emit the @catch block.
      CGF.EmitBlock(MatchedBlock);

      // Collect any cleanups for the catch variable.  The scope lasts until
      // the end of the catch body.
      CodeGenFunction::RunCleanupsScope CatchVarCleanups(CGF);

      CGF.EmitAutoVarDecl(*CatchParam);
      assert(CGF.HaveInsertPoint() && "DeclStmt destroyed insert point?");

      // Initialize the catch variable.
      llvm::Value *Tmp =
        CGF.Builder.CreateBitCast(Caught,
                                  CGF.ConvertType(CatchParam->getType()));
      EmitInitOfCatchParam(CGF, Tmp, CatchParam);

      CGF.EmitStmt(CatchStmt->getCatchBody());

      // We're done with the catch variable.
      CatchVarCleanups.ForceCleanup();

      CGF.EmitBranchThroughCleanup(FinallyEnd);

      CGF.EmitBlock(NextCatchBlock);
    }

    CGF.ObjCEHValueStack.pop_back();

    // If nothing wanted anything to do with the caught exception,
    // kill the extract call.
    if (Caught->use_empty())
      Caught->eraseFromParent();

    if (!AllMatched)
      CGF.EmitBranchThroughCleanup(FinallyRethrow);

    if (HasFinally) {
      // Emit the exception handler for the @catch blocks.
      CGF.EmitBlock(CatchHandler);

      // In theory we might now need a write hazard, but actually it's
      // unnecessary because there's no local-accessing code between
      // the try's write hazard and here.
      //Hazards.emitWriteHazard();

      // Extract the new exception and save it to the
      // propagating-exception slot.
      assert(PropagatingExnVar.isValid());
      llvm::CallInst *NewCaught = CGF.EmitNounwindRuntimeCall(
          ObjCTypes.getExceptionExtractFn(), ExceptionData.emitRawPointer(CGF),
          "caught");
      CGF.Builder.CreateStore(NewCaught, PropagatingExnVar);

      // Don't pop the catch handler; the throw already did.
      CGF.Builder.CreateStore(CGF.Builder.getFalse(), CallTryExitVar);
      CGF.EmitBranchThroughCleanup(FinallyRethrow);
    }
  }

  // Insert read hazards as required in the new blocks.
  Hazards.emitHazardsInNewBlocks();

  // Pop the cleanup.
  CGF.Builder.restoreIP(TryFallthroughIP);
  if (CGF.HaveInsertPoint())
    CGF.Builder.CreateStore(CGF.Builder.getTrue(), CallTryExitVar);
  CGF.PopCleanupBlock();
  CGF.EmitBlock(FinallyEnd.getBlock(), true);

  // Emit the rethrow block.
  CGBuilderTy::InsertPoint SavedIP = CGF.Builder.saveAndClearIP();
  CGF.EmitBlock(FinallyRethrow.getBlock(), true);
  if (CGF.HaveInsertPoint()) {
    // If we have a propagating-exception variable, check it.
    llvm::Value *PropagatingExn;
    if (PropagatingExnVar.isValid()) {
      PropagatingExn = CGF.Builder.CreateLoad(PropagatingExnVar);

    // Otherwise, just look in the buffer for the exception to throw.
    } else {
      llvm::CallInst *Caught = CGF.EmitNounwindRuntimeCall(
          ObjCTypes.getExceptionExtractFn(), ExceptionData.emitRawPointer(CGF));
      PropagatingExn = Caught;
    }

    CGF.EmitNounwindRuntimeCall(ObjCTypes.getExceptionThrowFn(),
                                PropagatingExn);
    CGF.Builder.CreateUnreachable();
  }

  CGF.Builder.restoreIP(SavedIP);
}

void CGObjCMac::EmitThrowStmt(CodeGen::CodeGenFunction &CGF,
                              const ObjCAtThrowStmt &S,
                              bool ClearInsertionPoint) {
  llvm::Value *ExceptionAsObject;

  if (const Expr *ThrowExpr = S.getThrowExpr()) {
    llvm::Value *Exception = CGF.EmitObjCThrowOperand(ThrowExpr);
    ExceptionAsObject =
      CGF.Builder.CreateBitCast(Exception, ObjCTypes.ObjectPtrTy);
  } else {
    assert((!CGF.ObjCEHValueStack.empty() && CGF.ObjCEHValueStack.back()) &&
           "Unexpected rethrow outside @catch block.");
    ExceptionAsObject = CGF.ObjCEHValueStack.back();
  }

  CGF.EmitRuntimeCall(ObjCTypes.getExceptionThrowFn(), ExceptionAsObject)
    ->setDoesNotReturn();
  CGF.Builder.CreateUnreachable();

  // Clear the insertion point to indicate we are in unreachable code.
  if (ClearInsertionPoint)
    CGF.Builder.ClearInsertionPoint();
}

/// EmitObjCWeakRead - Code gen for loading value of a __weak
/// object: objc_read_weak (id *src)
///
llvm::Value * CGObjCMac::EmitObjCWeakRead(CodeGen::CodeGenFunction &CGF,
                                          Address AddrWeakObj) {
  llvm::Type* DestTy = AddrWeakObj.getElementType();
  llvm::Value *AddrWeakObjVal = CGF.Builder.CreateBitCast(
      AddrWeakObj.emitRawPointer(CGF), ObjCTypes.PtrObjectPtrTy);
  llvm::Value *read_weak =
    CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcReadWeakFn(),
                                AddrWeakObjVal, "weakread");
  read_weak = CGF.Builder.CreateBitCast(read_weak, DestTy);
  return read_weak;
}

/// EmitObjCWeakAssign - Code gen for assigning to a __weak object.
/// objc_assign_weak (id src, id *dst)
///
void CGObjCMac::EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *src, Address dst) {
  llvm::Type * SrcTy = src->getType();
  if (!isa<llvm::PointerType>(SrcTy)) {
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
    assert(Size <= 8 && "does not support size > 8");
    src = (Size == 4) ? CGF.Builder.CreateBitCast(src, CGM.Int32Ty)
                      : CGF.Builder.CreateBitCast(src, CGM.Int64Ty);
    src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
  }
  src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
  llvm::Value *dstVal = CGF.Builder.CreateBitCast(dst.emitRawPointer(CGF),
                                                  ObjCTypes.PtrObjectPtrTy);
  llvm::Value *args[] = { src, dstVal };
  CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignWeakFn(),
                              args, "weakassign");
}

/// EmitObjCGlobalAssign - Code gen for assigning to a __strong object.
/// objc_assign_global (id src, id *dst)
///
void CGObjCMac::EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                                     llvm::Value *src, Address dst,
                                     bool threadlocal) {
  llvm::Type * SrcTy = src->getType();
  if (!isa<llvm::PointerType>(SrcTy)) {
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
    assert(Size <= 8 && "does not support size > 8");
    src = (Size == 4) ? CGF.Builder.CreateBitCast(src, CGM.Int32Ty)
                      : CGF.Builder.CreateBitCast(src, CGM.Int64Ty);
    src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
  }
  src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
  llvm::Value *dstVal = CGF.Builder.CreateBitCast(dst.emitRawPointer(CGF),
                                                  ObjCTypes.PtrObjectPtrTy);
  llvm::Value *args[] = {src, dstVal};
  if (!threadlocal)
    CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignGlobalFn(),
                                args, "globalassign");
  else
    CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignThreadLocalFn(),
                                args, "threadlocalassign");
}

/// EmitObjCIvarAssign - Code gen for assigning to a __strong object.
/// objc_assign_ivar (id src, id *dst, ptrdiff_t ivaroffset)
///
void CGObjCMac::EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *src, Address dst,
                                   llvm::Value *ivarOffset) {
  assert(ivarOffset && "EmitObjCIvarAssign - ivarOffset is NULL");
  llvm::Type * SrcTy = src->getType();
  if (!isa<llvm::PointerType>(SrcTy)) {
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
    assert(Size <= 8 && "does not support size > 8");
    src = (Size == 4) ? CGF.Builder.CreateBitCast(src, CGM.Int32Ty)
                      : CGF.Builder.CreateBitCast(src, CGM.Int64Ty);
    src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
  }
  src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
  llvm::Value *dstVal = CGF.Builder.CreateBitCast(dst.emitRawPointer(CGF),
                                                  ObjCTypes.PtrObjectPtrTy);
  llvm::Value *args[] = {src, dstVal, ivarOffset};
  CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignIvarFn(), args);
}

/// EmitObjCStrongCastAssign - Code gen for assigning to a __strong cast object.
/// objc_assign_strongCast (id src, id *dst)
///
void CGObjCMac::EmitObjCStrongCastAssign(CodeGen::CodeGenFunction &CGF,
                                         llvm::Value *src, Address dst) {
  llvm::Type * SrcTy = src->getType();
  if (!isa<llvm::PointerType>(SrcTy)) {
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
    assert(Size <= 8 && "does not support size > 8");
    src = (Size == 4) ? CGF.Builder.CreateBitCast(src, CGM.Int32Ty)
                      : CGF.Builder.CreateBitCast(src, CGM.Int64Ty);
    src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
  }
  src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
  llvm::Value *dstVal = CGF.Builder.CreateBitCast(dst.emitRawPointer(CGF),
                                                  ObjCTypes.PtrObjectPtrTy);
  llvm::Value *args[] = {src, dstVal};
  CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignStrongCastFn(),
                              args, "strongassign");
}

void CGObjCMac::EmitGCMemmoveCollectable(CodeGen::CodeGenFunction &CGF,
                                         Address DestPtr, Address SrcPtr,
                                         llvm::Value *size) {
  llvm::Value *args[] = {DestPtr.emitRawPointer(CGF),
                         SrcPtr.emitRawPointer(CGF), size};
  CGF.EmitNounwindRuntimeCall(ObjCTypes.GcMemmoveCollectableFn(), args);
}

/// EmitObjCValueForIvar - Code Gen for ivar reference.
///
LValue CGObjCMac::EmitObjCValueForIvar(CodeGen::CodeGenFunction &CGF,
                                       QualType ObjectTy,
                                       llvm::Value *BaseValue,
                                       const ObjCIvarDecl *Ivar,
                                       unsigned CVRQualifiers) {
  const ObjCInterfaceDecl *ID =
    ObjectTy->castAs<ObjCObjectType>()->getInterface();
  return EmitValueForIvarAtOffset(CGF, ID, BaseValue, Ivar, CVRQualifiers,
                                  EmitIvarOffset(CGF, ID, Ivar));
}

llvm::Value *CGObjCMac::EmitIvarOffset(CodeGen::CodeGenFunction &CGF,
                                       const ObjCInterfaceDecl *Interface,
                                       const ObjCIvarDecl *Ivar) {
  uint64_t Offset = ComputeIvarBaseOffset(CGM, Interface, Ivar);
  return llvm::ConstantInt::get(
    CGM.getTypes().ConvertType(CGM.getContext().LongTy),
    Offset);
}

/* *** Private Interface *** */

std::string CGObjCCommonMac::GetSectionName(StringRef Section,
                                            StringRef MachOAttributes) {
  switch (CGM.getTriple().getObjectFormat()) {
  case llvm::Triple::UnknownObjectFormat:
    llvm_unreachable("unexpected object file format");
  case llvm::Triple::MachO: {
    if (MachOAttributes.empty())
      return ("__DATA," + Section).str();
    return ("__DATA," + Section + "," + MachOAttributes).str();
  }
  case llvm::Triple::ELF:
    assert(Section.starts_with("__") && "expected the name to begin with __");
    return Section.substr(2).str();
  case llvm::Triple::COFF:
    assert(Section.starts_with("__") && "expected the name to begin with __");
    return ("." + Section.substr(2) + "$B").str();
  case llvm::Triple::Wasm:
  case llvm::Triple::GOFF:
  case llvm::Triple::SPIRV:
  case llvm::Triple::XCOFF:
  case llvm::Triple::DXContainer:
    llvm::report_fatal_error(
        "Objective-C support is unimplemented for object file format");
  }

  llvm_unreachable("Unhandled llvm::Triple::ObjectFormatType enum");
}

/// EmitImageInfo - Emit the image info marker used to encode some module
/// level information.
///
/// See: <rdr://4810609&4810587&4810587>
/// struct IMAGE_INFO {
///   unsigned version;
///   unsigned flags;
/// };
enum ImageInfoFlags {
  eImageInfo_FixAndContinue      = (1 << 0), // This flag is no longer set by clang.
  eImageInfo_GarbageCollected    = (1 << 1),
  eImageInfo_GCOnly              = (1 << 2),
  eImageInfo_OptimizedByDyld     = (1 << 3), // This flag is set by the dyld shared cache.

  // A flag indicating that the module has no instances of a @synthesize of a
  // superclass variable. This flag used to be consumed by the runtime to work
  // around miscompile by gcc.
  eImageInfo_CorrectedSynthesize = (1 << 4), // This flag is no longer set by clang.
  eImageInfo_ImageIsSimulated    = (1 << 5),
  eImageInfo_ClassProperties     = (1 << 6)
};

void CGObjCCommonMac::EmitImageInfo() {
  unsigned version = 0; // Version is unused?
  std::string Section =
      (ObjCABI == 1)
          ? "__OBJC,__image_info,regular"
          : GetSectionName("__objc_imageinfo", "regular,no_dead_strip");

  // Generate module-level named metadata to convey this information to the
  // linker and code-gen.
  llvm::Module &Mod = CGM.getModule();

  // Add the ObjC ABI version to the module flags.
  Mod.addModuleFlag(llvm::Module::Error, "Objective-C Version", ObjCABI);
  Mod.addModuleFlag(llvm::Module::Error, "Objective-C Image Info Version",
                    version);
  Mod.addModuleFlag(llvm::Module::Error, "Objective-C Image Info Section",
                    llvm::MDString::get(VMContext, Section));

  auto Int8Ty = llvm::Type::getInt8Ty(VMContext);
  if (CGM.getLangOpts().getGC() == LangOptions::NonGC) {
    // Non-GC overrides those files which specify GC.
    Mod.addModuleFlag(llvm::Module::Error,
                      "Objective-C Garbage Collection",
                      llvm::ConstantInt::get(Int8Ty,0));
  } else {
    // Add the ObjC garbage collection value.
    Mod.addModuleFlag(llvm::Module::Error,
                      "Objective-C Garbage Collection",
                      llvm::ConstantInt::get(Int8Ty,
                        (uint8_t)eImageInfo_GarbageCollected));

    if (CGM.getLangOpts().getGC() == LangOptions::GCOnly) {
      // Add the ObjC GC Only value.
      Mod.addModuleFlag(llvm::Module::Error, "Objective-C GC Only",
                        eImageInfo_GCOnly);

      // Require that GC be specified and set to eImageInfo_GarbageCollected.
      llvm::Metadata *Ops[2] = {
          llvm::MDString::get(VMContext, "Objective-C Garbage Collection"),
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
              Int8Ty, eImageInfo_GarbageCollected))};
      Mod.addModuleFlag(llvm::Module::Require, "Objective-C GC Only",
                        llvm::MDNode::get(VMContext, Ops));
    }
  }

  // Indicate whether we're compiling this to run on a simulator.
  if (CGM.getTarget().getTriple().isSimulatorEnvironment())
    Mod.addModuleFlag(llvm::Module::Error, "Objective-C Is Simulated",
                      eImageInfo_ImageIsSimulated);

  // Indicate whether we are generating class properties.
  Mod.addModuleFlag(llvm::Module::Error, "Objective-C Class Properties",
                    eImageInfo_ClassProperties);
}

// struct objc_module {
//   unsigned long version;
//   unsigned long size;
//   const char *name;
//   Symtab symtab;
// };

// FIXME: Get from somewhere
static const int ModuleVersion = 7;

void CGObjCMac::EmitModuleInfo() {
  uint64_t Size = CGM.getDataLayout().getTypeAllocSize(ObjCTypes.ModuleTy);

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ModuleTy);
  values.addInt(ObjCTypes.LongTy, ModuleVersion);
  values.addInt(ObjCTypes.LongTy, Size);
  // This used to be the filename, now it is unused. <rdr://4327263>
  values.add(GetClassName(StringRef("")));
  values.add(EmitModuleSymbols());
  CreateMetadataVar("OBJC_MODULES", values,
                    "__OBJC,__module_info,regular,no_dead_strip",
                    CGM.getPointerAlign(), true);
}

llvm::Constant *CGObjCMac::EmitModuleSymbols() {
  unsigned NumClasses = DefinedClasses.size();
  unsigned NumCategories = DefinedCategories.size();

  // Return null if no symbols were defined.
  if (!NumClasses && !NumCategories)
    return llvm::Constant::getNullValue(ObjCTypes.SymtabPtrTy);

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct();
  values.addInt(ObjCTypes.LongTy, 0);
  values.addNullPointer(ObjCTypes.SelectorPtrTy);
  values.addInt(ObjCTypes.ShortTy, NumClasses);
  values.addInt(ObjCTypes.ShortTy, NumCategories);

  // The runtime expects exactly the list of defined classes followed
  // by the list of defined categories, in a single array.
  auto array = values.beginArray(ObjCTypes.Int8PtrTy);
  for (unsigned i=0; i<NumClasses; i++) {
    const ObjCInterfaceDecl *ID = ImplementedClasses[i];
    assert(ID);
    if (ObjCImplementationDecl *IMP = ID->getImplementation())
      // We are implementing a weak imported interface. Give it external linkage
      if (ID->isWeakImported() && !IMP->isWeakImported())
        DefinedClasses[i]->setLinkage(llvm::GlobalVariable::ExternalLinkage);

    array.add(DefinedClasses[i]);
  }
  for (unsigned i=0; i<NumCategories; i++)
    array.add(DefinedCategories[i]);

  array.finishAndAddTo(values);

  llvm::GlobalVariable *GV = CreateMetadataVar(
      "OBJC_SYMBOLS", values, "__OBJC,__symbols,regular,no_dead_strip",
      CGM.getPointerAlign(), true);
  return GV;
}

llvm::Value *CGObjCMac::EmitClassRefFromId(CodeGenFunction &CGF,
                                           IdentifierInfo *II) {
  LazySymbols.insert(II);

  llvm::GlobalVariable *&Entry = ClassReferences[II];

  if (!Entry) {
    Entry =
        CreateMetadataVar("OBJC_CLASS_REFERENCES_", GetClassName(II->getName()),
                          "__OBJC,__cls_refs,literal_pointers,no_dead_strip",
                          CGM.getPointerAlign(), true);
  }

  return CGF.Builder.CreateAlignedLoad(Entry->getValueType(), Entry,
                                       CGF.getPointerAlign());
}

llvm::Value *CGObjCMac::EmitClassRef(CodeGenFunction &CGF,
                                     const ObjCInterfaceDecl *ID) {
  // If the class has the objc_runtime_visible attribute, we need to
  // use the Objective-C runtime to get the class.
  if (ID->hasAttr<ObjCRuntimeVisibleAttr>())
    return EmitClassRefViaRuntime(CGF, ID, ObjCTypes);

  IdentifierInfo *RuntimeName =
      &CGM.getContext().Idents.get(ID->getObjCRuntimeNameAsString());
  return EmitClassRefFromId(CGF, RuntimeName);
}

llvm::Value *CGObjCMac::EmitNSAutoreleasePoolClassRef(CodeGenFunction &CGF) {
  IdentifierInfo *II = &CGM.getContext().Idents.get("NSAutoreleasePool");
  return EmitClassRefFromId(CGF, II);
}

llvm::Value *CGObjCMac::EmitSelector(CodeGenFunction &CGF, Selector Sel) {
  return CGF.Builder.CreateLoad(EmitSelectorAddr(Sel));
}

ConstantAddress CGObjCMac::EmitSelectorAddr(Selector Sel) {
  CharUnits Align = CGM.getPointerAlign();

  llvm::GlobalVariable *&Entry = SelectorReferences[Sel];
  if (!Entry) {
    Entry = CreateMetadataVar(
        "OBJC_SELECTOR_REFERENCES_", GetMethodVarName(Sel),
        "__OBJC,__message_refs,literal_pointers,no_dead_strip", Align, true);
    Entry->setExternallyInitialized(true);
  }

  return ConstantAddress(Entry, ObjCTypes.SelectorPtrTy, Align);
}

llvm::Constant *CGObjCCommonMac::GetClassName(StringRef RuntimeName) {
    llvm::GlobalVariable *&Entry = ClassNames[RuntimeName];
    if (!Entry)
      Entry = CreateCStringLiteral(RuntimeName, ObjCLabelType::ClassName);
    return getConstantGEP(VMContext, Entry, 0, 0);
}

llvm::Function *CGObjCCommonMac::GetMethodDefinition(const ObjCMethodDecl *MD) {
  return MethodDefinitions.lookup(MD);
}

/// GetIvarLayoutName - Returns a unique constant for the given
/// ivar layout bitmap.
llvm::Constant *CGObjCCommonMac::GetIvarLayoutName(IdentifierInfo *Ident,
                                       const ObjCCommonTypesHelper &ObjCTypes) {
  return llvm::Constant::getNullValue(ObjCTypes.Int8PtrTy);
}

void IvarLayoutBuilder::visitRecord(const RecordType *RT,
                                    CharUnits offset) {
  const RecordDecl *RD = RT->getDecl();

  // If this is a union, remember that we had one, because it might mess
  // up the ordering of layout entries.
  if (RD->isUnion())
    IsDisordered = true;

  const ASTRecordLayout *recLayout = nullptr;
  visitAggregate(RD->field_begin(), RD->field_end(), offset,
                 [&](const FieldDecl *field) -> CharUnits {
    if (!recLayout)
      recLayout = &CGM.getContext().getASTRecordLayout(RD);
    auto offsetInBits = recLayout->getFieldOffset(field->getFieldIndex());
    return CGM.getContext().toCharUnitsFromBits(offsetInBits);
  });
}

template <class Iterator, class GetOffsetFn>
void IvarLayoutBuilder::visitAggregate(Iterator begin, Iterator end,
                                       CharUnits aggregateOffset,
                                       const GetOffsetFn &getOffset) {
  for (; begin != end; ++begin) {
    auto field = *begin;

    // Skip over bitfields.
    if (field->isBitField()) {
      continue;
    }

    // Compute the offset of the field within the aggregate.
    CharUnits fieldOffset = aggregateOffset + getOffset(field);

    visitField(field, fieldOffset);
  }
}

/// Collect layout information for the given fields into IvarsInfo.
void IvarLayoutBuilder::visitField(const FieldDecl *field,
                                   CharUnits fieldOffset) {
  QualType fieldType = field->getType();

  // Drill down into arrays.
  uint64_t numElts = 1;
  if (auto arrayType = CGM.getContext().getAsIncompleteArrayType(fieldType)) {
    numElts = 0;
    fieldType = arrayType->getElementType();
  }
  // Unlike incomplete arrays, constant arrays can be nested.
  while (auto arrayType = CGM.getContext().getAsConstantArrayType(fieldType)) {
    numElts *= arrayType->getZExtSize();
    fieldType = arrayType->getElementType();
  }

  assert(!fieldType->isArrayType() && "ivar of non-constant array type?");

  // If we ended up with a zero-sized array, we've done what we can do within
  // the limits of this layout encoding.
  if (numElts == 0) return;

  // Recurse if the base element type is a record type.
  if (auto recType = fieldType->getAs<RecordType>()) {
    size_t oldEnd = IvarsInfo.size();

    visitRecord(recType, fieldOffset);

    // If we have an array, replicate the first entry's layout information.
    auto numEltEntries = IvarsInfo.size() - oldEnd;
    if (numElts != 1 && numEltEntries != 0) {
      CharUnits eltSize = CGM.getContext().getTypeSizeInChars(recType);
      for (uint64_t eltIndex = 1; eltIndex != numElts; ++eltIndex) {
        // Copy the last numEltEntries onto the end of the array, adjusting
        // each for the element size.
        for (size_t i = 0; i != numEltEntries; ++i) {
          auto firstEntry = IvarsInfo[oldEnd + i];
          IvarsInfo.push_back(IvarInfo(firstEntry.Offset + eltIndex * eltSize,
                                       firstEntry.SizeInWords));
        }
      }
    }

    return;
  }

  // Classify the element type.
  Qualifiers::GC GCAttr = GetGCAttrTypeForType(CGM.getContext(), fieldType);

  // If it matches what we're looking for, add an entry.
  if ((ForStrongLayout && GCAttr == Qualifiers::Strong)
      || (!ForStrongLayout && GCAttr == Qualifiers::Weak)) {
    assert(CGM.getContext().getTypeSizeInChars(fieldType)
             == CGM.getPointerSize());
    IvarsInfo.push_back(IvarInfo(fieldOffset, numElts));
  }
}

/// buildBitmap - This routine does the horsework of taking the offsets of
/// strong/weak references and creating a bitmap.  The bitmap is also
/// returned in the given buffer, suitable for being passed to \c dump().
llvm::Constant *IvarLayoutBuilder::buildBitmap(CGObjCCommonMac &CGObjC,
                                llvm::SmallVectorImpl<unsigned char> &buffer) {
  // The bitmap is a series of skip/scan instructions, aligned to word
  // boundaries.  The skip is performed first.
  const unsigned char MaxNibble = 0xF;
  const unsigned char SkipMask = 0xF0, SkipShift = 4;
  const unsigned char ScanMask = 0x0F, ScanShift = 0;

  assert(!IvarsInfo.empty() && "generating bitmap for no data");

  // Sort the ivar info on byte position in case we encounterred a
  // union nested in the ivar list.
  if (IsDisordered) {
    // This isn't a stable sort, but our algorithm should handle it fine.
    llvm::array_pod_sort(IvarsInfo.begin(), IvarsInfo.end());
  } else {
    assert(llvm::is_sorted(IvarsInfo));
  }
  assert(IvarsInfo.back().Offset < InstanceEnd);

  assert(buffer.empty());

  // Skip the next N words.
  auto skip = [&](unsigned numWords) {
    assert(numWords > 0);

    // Try to merge into the previous byte.  Since scans happen second, we
    // can't do this if it includes a scan.
    if (!buffer.empty() && !(buffer.back() & ScanMask)) {
      unsigned lastSkip = buffer.back() >> SkipShift;
      if (lastSkip < MaxNibble) {
        unsigned claimed = std::min(MaxNibble - lastSkip, numWords);
        numWords -= claimed;
        lastSkip += claimed;
        buffer.back() = (lastSkip << SkipShift);
      }
    }

    while (numWords >= MaxNibble) {
      buffer.push_back(MaxNibble << SkipShift);
      numWords -= MaxNibble;
    }
    if (numWords) {
      buffer.push_back(numWords << SkipShift);
    }
  };

  // Scan the next N words.
  auto scan = [&](unsigned numWords) {
    assert(numWords > 0);

    // Try to merge into the previous byte.  Since scans happen second, we can
    // do this even if it includes a skip.
    if (!buffer.empty()) {
      unsigned lastScan = (buffer.back() & ScanMask) >> ScanShift;
      if (lastScan < MaxNibble) {
        unsigned claimed = std::min(MaxNibble - lastScan, numWords);
        numWords -= claimed;
        lastScan += claimed;
        buffer.back() = (buffer.back() & SkipMask) | (lastScan << ScanShift);
      }
    }

    while (numWords >= MaxNibble) {
      buffer.push_back(MaxNibble << ScanShift);
      numWords -= MaxNibble;
    }
    if (numWords) {
      buffer.push_back(numWords << ScanShift);
    }
  };

  // One past the end of the last scan.
  unsigned endOfLastScanInWords = 0;
  const CharUnits WordSize = CGM.getPointerSize();

  // Consider all the scan requests.
  for (auto &request : IvarsInfo) {
    CharUnits beginOfScan = request.Offset - InstanceBegin;

    // Ignore scan requests that don't start at an even multiple of the
    // word size.  We can't encode them.
    if ((beginOfScan % WordSize) != 0) continue;

    // Ignore scan requests that start before the instance start.
    // This assumes that scans never span that boundary.  The boundary
    // isn't the true start of the ivars, because in the fragile-ARC case
    // it's rounded up to word alignment, but the test above should leave
    // us ignoring that possibility.
    if (beginOfScan.isNegative()) {
      assert(request.Offset + request.SizeInWords * WordSize <= InstanceBegin);
      continue;
    }

    unsigned beginOfScanInWords = beginOfScan / WordSize;
    unsigned endOfScanInWords = beginOfScanInWords + request.SizeInWords;

    // If the scan starts some number of words after the last one ended,
    // skip forward.
    if (beginOfScanInWords > endOfLastScanInWords) {
      skip(beginOfScanInWords - endOfLastScanInWords);

    // Otherwise, start scanning where the last left off.
    } else {
      beginOfScanInWords = endOfLastScanInWords;

      // If that leaves us with nothing to scan, ignore this request.
      if (beginOfScanInWords >= endOfScanInWords) continue;
    }

    // Scan to the end of the request.
    assert(beginOfScanInWords < endOfScanInWords);
    scan(endOfScanInWords - beginOfScanInWords);
    endOfLastScanInWords = endOfScanInWords;
  }

  if (buffer.empty())
    return llvm::ConstantPointerNull::get(CGM.Int8PtrTy);

  // For GC layouts, emit a skip to the end of the allocation so that we
  // have precise information about the entire thing.  This isn't useful
  // or necessary for the ARC-style layout strings.
  if (CGM.getLangOpts().getGC() != LangOptions::NonGC) {
    unsigned lastOffsetInWords =
      (InstanceEnd - InstanceBegin + WordSize - CharUnits::One()) / WordSize;
    if (lastOffsetInWords > endOfLastScanInWords) {
      skip(lastOffsetInWords - endOfLastScanInWords);
    }
  }

  // Null terminate the string.
  buffer.push_back(0);

  auto *Entry = CGObjC.CreateCStringLiteral(
      reinterpret_cast<char *>(buffer.data()), ObjCLabelType::ClassName);
  return getConstantGEP(CGM.getLLVMContext(), Entry, 0, 0);
}

/// BuildIvarLayout - Builds ivar layout bitmap for the class
/// implementation for the __strong or __weak case.
/// The layout map displays which words in ivar list must be skipped
/// and which must be scanned by GC (see below). String is built of bytes.
/// Each byte is divided up in two nibbles (4-bit each). Left nibble is count
/// of words to skip and right nibble is count of words to scan. So, each
/// nibble represents up to 15 workds to skip or scan. Skipping the rest is
/// represented by a 0x00 byte which also ends the string.
/// 1. when ForStrongLayout is true, following ivars are scanned:
/// - id, Class
/// - object *
/// - __strong anything
///
/// 2. When ForStrongLayout is false, following ivars are scanned:
/// - __weak anything
///
llvm::Constant *
CGObjCCommonMac::BuildIvarLayout(const ObjCImplementationDecl *OMD,
                                 CharUnits beginOffset, CharUnits endOffset,
                                 bool ForStrongLayout, bool HasMRCWeakIvars) {
  // If this is MRC, and we're either building a strong layout or there
  // are no weak ivars, bail out early.
  llvm::Type *PtrTy = CGM.Int8PtrTy;
  if (CGM.getLangOpts().getGC() == LangOptions::NonGC &&
      !CGM.getLangOpts().ObjCAutoRefCount &&
      (ForStrongLayout || !HasMRCWeakIvars))
    return llvm::Constant::getNullValue(PtrTy);

  const ObjCInterfaceDecl *OI = OMD->getClassInterface();
  SmallVector<const ObjCIvarDecl*, 32> ivars;

  // GC layout strings include the complete object layout, possibly
  // inaccurately in the non-fragile ABI; the runtime knows how to fix this
  // up.
  //
  // ARC layout strings only include the class's ivars.  In non-fragile
  // runtimes, that means starting at InstanceStart, rounded up to word
  // alignment.  In fragile runtimes, there's no InstanceStart, so it means
  // starting at the offset of the first ivar, rounded up to word alignment.
  //
  // MRC weak layout strings follow the ARC style.
  CharUnits baseOffset;
  if (CGM.getLangOpts().getGC() == LangOptions::NonGC) {
    for (const ObjCIvarDecl *IVD = OI->all_declared_ivar_begin();
         IVD; IVD = IVD->getNextIvar())
      ivars.push_back(IVD);

    if (isNonFragileABI()) {
      baseOffset = beginOffset; // InstanceStart
    } else if (!ivars.empty()) {
      baseOffset =
        CharUnits::fromQuantity(ComputeIvarBaseOffset(CGM, OMD, ivars[0]));
    } else {
      baseOffset = CharUnits::Zero();
    }

    baseOffset = baseOffset.alignTo(CGM.getPointerAlign());
  }
  else {
    CGM.getContext().DeepCollectObjCIvars(OI, true, ivars);

    baseOffset = CharUnits::Zero();
  }

  if (ivars.empty())
    return llvm::Constant::getNullValue(PtrTy);

  IvarLayoutBuilder builder(CGM, baseOffset, endOffset, ForStrongLayout);

  builder.visitAggregate(ivars.begin(), ivars.end(), CharUnits::Zero(),
                         [&](const ObjCIvarDecl *ivar) -> CharUnits {
      return CharUnits::fromQuantity(ComputeIvarBaseOffset(CGM, OMD, ivar));
  });

  if (!builder.hasBitmapData())
    return llvm::Constant::getNullValue(PtrTy);

  llvm::SmallVector<unsigned char, 4> buffer;
  llvm::Constant *C = builder.buildBitmap(*this, buffer);

   if (CGM.getLangOpts().ObjCGCBitmapPrint && !buffer.empty()) {
    printf("\n%s ivar layout for class '%s': ",
           ForStrongLayout ? "strong" : "weak",
           OMD->getClassInterface()->getName().str().c_str());
    builder.dump(buffer);
  }
  return C;
}

llvm::Constant *CGObjCCommonMac::GetMethodVarName(Selector Sel) {
  llvm::GlobalVariable *&Entry = MethodVarNames[Sel];
  // FIXME: Avoid std::string in "Sel.getAsString()"
  if (!Entry)
    Entry = CreateCStringLiteral(Sel.getAsString(), ObjCLabelType::MethodVarName);
  return getConstantGEP(VMContext, Entry, 0, 0);
}

// FIXME: Merge into a single cstring creation function.
llvm::Constant *CGObjCCommonMac::GetMethodVarName(IdentifierInfo *ID) {
  return GetMethodVarName(CGM.getContext().Selectors.getNullarySelector(ID));
}

llvm::Constant *CGObjCCommonMac::GetMethodVarType(const FieldDecl *Field) {
  std::string TypeStr;
  CGM.getContext().getObjCEncodingForType(Field->getType(), TypeStr, Field);

  llvm::GlobalVariable *&Entry = MethodVarTypes[TypeStr];
  if (!Entry)
    Entry = CreateCStringLiteral(TypeStr, ObjCLabelType::MethodVarType);
  return getConstantGEP(VMContext, Entry, 0, 0);
}

llvm::Constant *CGObjCCommonMac::GetMethodVarType(const ObjCMethodDecl *D,
                                                  bool Extended) {
  std::string TypeStr =
    CGM.getContext().getObjCEncodingForMethodDecl(D, Extended);

  llvm::GlobalVariable *&Entry = MethodVarTypes[TypeStr];
  if (!Entry)
    Entry = CreateCStringLiteral(TypeStr, ObjCLabelType::MethodVarType);
  return getConstantGEP(VMContext, Entry, 0, 0);
}

// FIXME: Merge into a single cstring creation function.
llvm::Constant *CGObjCCommonMac::GetPropertyName(IdentifierInfo *Ident) {
  llvm::GlobalVariable *&Entry = PropertyNames[Ident];
  if (!Entry)
    Entry = CreateCStringLiteral(Ident->getName(), ObjCLabelType::PropertyName);
  return getConstantGEP(VMContext, Entry, 0, 0);
}

// FIXME: Merge into a single cstring creation function.
// FIXME: This Decl should be more precise.
llvm::Constant *
CGObjCCommonMac::GetPropertyTypeString(const ObjCPropertyDecl *PD,
                                       const Decl *Container) {
  std::string TypeStr =
    CGM.getContext().getObjCEncodingForPropertyDecl(PD, Container);
  return GetPropertyName(&CGM.getContext().Idents.get(TypeStr));
}

void CGObjCMac::FinishModule() {
  EmitModuleInfo();

  // Emit the dummy bodies for any protocols which were referenced but
  // never defined.
  for (auto &entry : Protocols) {
    llvm::GlobalVariable *global = entry.second;
    if (global->hasInitializer())
      continue;

    ConstantInitBuilder builder(CGM);
    auto values = builder.beginStruct(ObjCTypes.ProtocolTy);
    values.addNullPointer(ObjCTypes.ProtocolExtensionPtrTy);
    values.add(GetClassName(entry.first->getName()));
    values.addNullPointer(ObjCTypes.ProtocolListPtrTy);
    values.addNullPointer(ObjCTypes.MethodDescriptionListPtrTy);
    values.addNullPointer(ObjCTypes.MethodDescriptionListPtrTy);
    values.finishAndSetAsInitializer(global);
    CGM.addCompilerUsedGlobal(global);
  }

  // Add assembler directives to add lazy undefined symbol references
  // for classes which are referenced but not defined. This is
  // important for correct linker interaction.
  //
  // FIXME: It would be nice if we had an LLVM construct for this.
  if ((!LazySymbols.empty() || !DefinedSymbols.empty()) &&
      CGM.getTriple().isOSBinFormatMachO()) {
    SmallString<256> Asm;
    Asm += CGM.getModule().getModuleInlineAsm();
    if (!Asm.empty() && Asm.back() != '\n')
      Asm += '\n';

    llvm::raw_svector_ostream OS(Asm);
    for (const auto *Sym : DefinedSymbols)
      OS << "\t.objc_class_name_" << Sym->getName() << "=0\n"
         << "\t.globl .objc_class_name_" << Sym->getName() << "\n";
    for (const auto *Sym : LazySymbols)
      OS << "\t.lazy_reference .objc_class_name_" << Sym->getName() << "\n";
    for (const auto &Category : DefinedCategoryNames)
      OS << "\t.objc_category_name_" << Category << "=0\n"
         << "\t.globl .objc_category_name_" << Category << "\n";

    CGM.getModule().setModuleInlineAsm(OS.str());
  }
}

CGObjCNonFragileABIMac::CGObjCNonFragileABIMac(CodeGen::CodeGenModule &cgm)
    : CGObjCCommonMac(cgm), ObjCTypes(cgm), ObjCEmptyCacheVar(nullptr),
      ObjCEmptyVtableVar(nullptr) {
  ObjCABI = 2;
}

/* *** */

ObjCCommonTypesHelper::ObjCCommonTypesHelper(CodeGen::CodeGenModule &cgm)
  : VMContext(cgm.getLLVMContext()), CGM(cgm), ExternalProtocolPtrTy(nullptr)
{
  CodeGen::CodeGenTypes &Types = CGM.getTypes();
  ASTContext &Ctx = CGM.getContext();
  unsigned ProgramAS = CGM.getDataLayout().getProgramAddressSpace();

  ShortTy = cast<llvm::IntegerType>(Types.ConvertType(Ctx.ShortTy));
  IntTy = CGM.IntTy;
  LongTy = cast<llvm::IntegerType>(Types.ConvertType(Ctx.LongTy));
  Int8PtrTy = CGM.Int8PtrTy;
  Int8PtrProgramASTy = llvm::PointerType::get(CGM.Int8Ty, ProgramAS);
  Int8PtrPtrTy = CGM.Int8PtrPtrTy;

  // arm64 targets use "int" ivar offset variables. All others,
  // including OS X x86_64 and Windows x86_64, use "long" ivar offsets.
  if (CGM.getTarget().getTriple().getArch() == llvm::Triple::aarch64)
    IvarOffsetVarTy = IntTy;
  else
    IvarOffsetVarTy = LongTy;

  ObjectPtrTy =
    cast<llvm::PointerType>(Types.ConvertType(Ctx.getObjCIdType()));
  PtrObjectPtrTy =
    llvm::PointerType::getUnqual(ObjectPtrTy);
  SelectorPtrTy =
    cast<llvm::PointerType>(Types.ConvertType(Ctx.getObjCSelType()));

  // I'm not sure I like this. The implicit coordination is a bit
  // gross. We should solve this in a reasonable fashion because this
  // is a pretty common task (match some runtime data structure with
  // an LLVM data structure).

  // FIXME: This is leaked.
  // FIXME: Merge with rewriter code?

  // struct _objc_super {
  //   id self;
  //   Class cls;
  // }
  RecordDecl *RD = RecordDecl::Create(
      Ctx, TagTypeKind::Struct, Ctx.getTranslationUnitDecl(), SourceLocation(),
      SourceLocation(), &Ctx.Idents.get("_objc_super"));
  RD->addDecl(FieldDecl::Create(Ctx, RD, SourceLocation(), SourceLocation(),
                                nullptr, Ctx.getObjCIdType(), nullptr, nullptr,
                                false, ICIS_NoInit));
  RD->addDecl(FieldDecl::Create(Ctx, RD, SourceLocation(), SourceLocation(),
                                nullptr, Ctx.getObjCClassType(), nullptr,
                                nullptr, false, ICIS_NoInit));
  RD->completeDefinition();

  SuperCTy = Ctx.getTagDeclType(RD);
  SuperPtrCTy = Ctx.getPointerType(SuperCTy);

  SuperTy = cast<llvm::StructType>(Types.ConvertType(SuperCTy));
  SuperPtrTy = llvm::PointerType::getUnqual(SuperTy);

  // struct _prop_t {
  //   char *name;
  //   char *attributes;
  // }
  PropertyTy = llvm::StructType::create("struct._prop_t", Int8PtrTy, Int8PtrTy);

  // struct _prop_list_t {
  //   uint32_t entsize;      // sizeof(struct _prop_t)
  //   uint32_t count_of_properties;
  //   struct _prop_t prop_list[count_of_properties];
  // }
  PropertyListTy = llvm::StructType::create(
      "struct._prop_list_t", IntTy, IntTy, llvm::ArrayType::get(PropertyTy, 0));
  // struct _prop_list_t *
  PropertyListPtrTy = llvm::PointerType::getUnqual(PropertyListTy);

  // struct _objc_method {
  //   SEL _cmd;
  //   char *method_type;
  //   char *_imp;
  // }
  MethodTy = llvm::StructType::create("struct._objc_method", SelectorPtrTy,
                                      Int8PtrTy, Int8PtrProgramASTy);

  // struct _objc_cache *
  CacheTy = llvm::StructType::create(VMContext, "struct._objc_cache");
  CachePtrTy = llvm::PointerType::getUnqual(CacheTy);
}

ObjCTypesHelper::ObjCTypesHelper(CodeGen::CodeGenModule &cgm)
  : ObjCCommonTypesHelper(cgm) {
  // struct _objc_method_description {
  //   SEL name;
  //   char *types;
  // }
  MethodDescriptionTy = llvm::StructType::create(
      "struct._objc_method_description", SelectorPtrTy, Int8PtrTy);

  // struct _objc_method_description_list {
  //   int count;
  //   struct _objc_method_description[1];
  // }
  MethodDescriptionListTy =
      llvm::StructType::create("struct._objc_method_description_list", IntTy,
                               llvm::ArrayType::get(MethodDescriptionTy, 0));

  // struct _objc_method_description_list *
  MethodDescriptionListPtrTy =
    llvm::PointerType::getUnqual(MethodDescriptionListTy);

  // Protocol description structures

  // struct _objc_protocol_extension {
  //   uint32_t size;  // sizeof(struct _objc_protocol_extension)
  //   struct _objc_method_description_list *optional_instance_methods;
  //   struct _objc_method_description_list *optional_class_methods;
  //   struct _objc_property_list *instance_properties;
  //   const char ** extendedMethodTypes;
  //   struct _objc_property_list *class_properties;
  // }
  ProtocolExtensionTy = llvm::StructType::create(
      "struct._objc_protocol_extension", IntTy, MethodDescriptionListPtrTy,
      MethodDescriptionListPtrTy, PropertyListPtrTy, Int8PtrPtrTy,
      PropertyListPtrTy);

  // struct _objc_protocol_extension *
  ProtocolExtensionPtrTy = llvm::PointerType::getUnqual(ProtocolExtensionTy);

  // Handle recursive construction of Protocol and ProtocolList types

  ProtocolTy =
    llvm::StructType::create(VMContext, "struct._objc_protocol");

  ProtocolListTy =
    llvm::StructType::create(VMContext, "struct._objc_protocol_list");
  ProtocolListTy->setBody(llvm::PointerType::getUnqual(ProtocolListTy), LongTy,
                          llvm::ArrayType::get(ProtocolTy, 0));

  // struct _objc_protocol {
  //   struct _objc_protocol_extension *isa;
  //   char *protocol_name;
  //   struct _objc_protocol **_objc_protocol_list;
  //   struct _objc_method_description_list *instance_methods;
  //   struct _objc_method_description_list *class_methods;
  // }
  ProtocolTy->setBody(ProtocolExtensionPtrTy, Int8PtrTy,
                      llvm::PointerType::getUnqual(ProtocolListTy),
                      MethodDescriptionListPtrTy, MethodDescriptionListPtrTy);

  // struct _objc_protocol_list *
  ProtocolListPtrTy = llvm::PointerType::getUnqual(ProtocolListTy);

  ProtocolPtrTy = llvm::PointerType::getUnqual(ProtocolTy);

  // Class description structures

  // struct _objc_ivar {
  //   char *ivar_name;
  //   char *ivar_type;
  //   int  ivar_offset;
  // }
  IvarTy = llvm::StructType::create("struct._objc_ivar", Int8PtrTy, Int8PtrTy,
                                    IntTy);

  // struct _objc_ivar_list *
  IvarListTy =
    llvm::StructType::create(VMContext, "struct._objc_ivar_list");
  IvarListPtrTy = llvm::PointerType::getUnqual(IvarListTy);

  // struct _objc_method_list *
  MethodListTy =
    llvm::StructType::create(VMContext, "struct._objc_method_list");
  MethodListPtrTy = llvm::PointerType::getUnqual(MethodListTy);

  // struct _objc_class_extension *
  ClassExtensionTy = llvm::StructType::create(
      "struct._objc_class_extension", IntTy, Int8PtrTy, PropertyListPtrTy);
  ClassExtensionPtrTy = llvm::PointerType::getUnqual(ClassExtensionTy);

  ClassTy = llvm::StructType::create(VMContext, "struct._objc_class");

  // struct _objc_class {
  //   Class isa;
  //   Class super_class;
  //   char *name;
  //   long version;
  //   long info;
  //   long instance_size;
  //   struct _objc_ivar_list *ivars;
  //   struct _objc_method_list *methods;
  //   struct _objc_cache *cache;
  //   struct _objc_protocol_list *protocols;
  //   char *ivar_layout;
  //   struct _objc_class_ext *ext;
  // };
  ClassTy->setBody(llvm::PointerType::getUnqual(ClassTy),
                   llvm::PointerType::getUnqual(ClassTy), Int8PtrTy, LongTy,
                   LongTy, LongTy, IvarListPtrTy, MethodListPtrTy, CachePtrTy,
                   ProtocolListPtrTy, Int8PtrTy, ClassExtensionPtrTy);

  ClassPtrTy = llvm::PointerType::getUnqual(ClassTy);

  // struct _objc_category {
  //   char *category_name;
  //   char *class_name;
  //   struct _objc_method_list *instance_method;
  //   struct _objc_method_list *class_method;
  //   struct _objc_protocol_list *protocols;
  //   uint32_t size;  // sizeof(struct _objc_category)
  //   struct _objc_property_list *instance_properties;// category's @property
  //   struct _objc_property_list *class_properties;
  // }
  CategoryTy = llvm::StructType::create(
      "struct._objc_category", Int8PtrTy, Int8PtrTy, MethodListPtrTy,
      MethodListPtrTy, ProtocolListPtrTy, IntTy, PropertyListPtrTy,
      PropertyListPtrTy);

  // Global metadata structures

  // struct _objc_symtab {
  //   long sel_ref_cnt;
  //   SEL *refs;
  //   short cls_def_cnt;
  //   short cat_def_cnt;
  //   char *defs[cls_def_cnt + cat_def_cnt];
  // }
  SymtabTy = llvm::StructType::create("struct._objc_symtab", LongTy,
                                      SelectorPtrTy, ShortTy, ShortTy,
                                      llvm::ArrayType::get(Int8PtrTy, 0));
  SymtabPtrTy = llvm::PointerType::getUnqual(SymtabTy);

  // struct _objc_module {
  //   long version;
  //   long size;   // sizeof(struct _objc_module)
  //   char *name;
  //   struct _objc_symtab* symtab;
  //  }
  ModuleTy = llvm::StructType::create("struct._objc_module", LongTy, LongTy,
                                      Int8PtrTy, SymtabPtrTy);

  // FIXME: This is the size of the setjmp buffer and should be target
  // specific. 18 is what's used on 32-bit X86.
  uint64_t SetJmpBufferSize = 18;

  // Exceptions
  llvm::Type *StackPtrTy = llvm::ArrayType::get(CGM.Int8PtrTy, 4);

  ExceptionDataTy = llvm::StructType::create(
      "struct._objc_exception_data",
      llvm::ArrayType::get(CGM.Int32Ty, SetJmpBufferSize), StackPtrTy);
}

ObjCNonFragileABITypesHelper::ObjCNonFragileABITypesHelper(CodeGen::CodeGenModule &cgm)
  : ObjCCommonTypesHelper(cgm) {
  // struct _method_list_t {
  //   uint32_t entsize;  // sizeof(struct _objc_method)
  //   uint32_t method_count;
  //   struct _objc_method method_list[method_count];
  // }
  MethodListnfABITy =
      llvm::StructType::create("struct.__method_list_t", IntTy, IntTy,
                               llvm::ArrayType::get(MethodTy, 0));
  // struct method_list_t *
  MethodListnfABIPtrTy = llvm::PointerType::getUnqual(MethodListnfABITy);

  // struct _protocol_t {
  //   id isa;  // NULL
  //   const char * const protocol_name;
  //   const struct _protocol_list_t * protocol_list; // super protocols
  //   const struct method_list_t * const instance_methods;
  //   const struct method_list_t * const class_methods;
  //   const struct method_list_t *optionalInstanceMethods;
  //   const struct method_list_t *optionalClassMethods;
  //   const struct _prop_list_t * properties;
  //   const uint32_t size;  // sizeof(struct _protocol_t)
  //   const uint32_t flags;  // = 0
  //   const char ** extendedMethodTypes;
  //   const char *demangledName;
  //   const struct _prop_list_t * class_properties;
  // }

  // Holder for struct _protocol_list_t *
  ProtocolListnfABITy =
    llvm::StructType::create(VMContext, "struct._objc_protocol_list");

  ProtocolnfABITy = llvm::StructType::create(
      "struct._protocol_t", ObjectPtrTy, Int8PtrTy,
      llvm::PointerType::getUnqual(ProtocolListnfABITy), MethodListnfABIPtrTy,
      MethodListnfABIPtrTy, MethodListnfABIPtrTy, MethodListnfABIPtrTy,
      PropertyListPtrTy, IntTy, IntTy, Int8PtrPtrTy, Int8PtrTy,
      PropertyListPtrTy);

  // struct _protocol_t*
  ProtocolnfABIPtrTy = llvm::PointerType::getUnqual(ProtocolnfABITy);

  // struct _protocol_list_t {
  //   long protocol_count;   // Note, this is 32/64 bit
  //   struct _protocol_t *[protocol_count];
  // }
  ProtocolListnfABITy->setBody(LongTy,
                               llvm::ArrayType::get(ProtocolnfABIPtrTy, 0));

  // struct _objc_protocol_list*
  ProtocolListnfABIPtrTy = llvm::PointerType::getUnqual(ProtocolListnfABITy);

  // struct _ivar_t {
  //   unsigned [long] int *offset;  // pointer to ivar offset location
  //   char *name;
  //   char *type;
  //   uint32_t alignment;
  //   uint32_t size;
  // }
  IvarnfABITy = llvm::StructType::create(
      "struct._ivar_t", llvm::PointerType::getUnqual(IvarOffsetVarTy),
      Int8PtrTy, Int8PtrTy, IntTy, IntTy);

  // struct _ivar_list_t {
  //   uint32 entsize;  // sizeof(struct _ivar_t)
  //   uint32 count;
  //   struct _iver_t list[count];
  // }
  IvarListnfABITy =
      llvm::StructType::create("struct._ivar_list_t", IntTy, IntTy,
                               llvm::ArrayType::get(IvarnfABITy, 0));

  IvarListnfABIPtrTy = llvm::PointerType::getUnqual(IvarListnfABITy);

  // struct _class_ro_t {
  //   uint32_t const flags;
  //   uint32_t const instanceStart;
  //   uint32_t const instanceSize;
  //   uint32_t const reserved;  // only when building for 64bit targets
  //   const uint8_t * const ivarLayout;
  //   const char *const name;
  //   const struct _method_list_t * const baseMethods;
  //   const struct _objc_protocol_list *const baseProtocols;
  //   const struct _ivar_list_t *const ivars;
  //   const uint8_t * const weakIvarLayout;
  //   const struct _prop_list_t * const properties;
  // }

  // FIXME. Add 'reserved' field in 64bit abi mode!
  ClassRonfABITy = llvm::StructType::create(
      "struct._class_ro_t", IntTy, IntTy, IntTy, Int8PtrTy, Int8PtrTy,
      MethodListnfABIPtrTy, ProtocolListnfABIPtrTy, IvarListnfABIPtrTy,
      Int8PtrTy, PropertyListPtrTy);

  // ImpnfABITy - LLVM for id (*)(id, SEL, ...)
  llvm::Type *params[] = { ObjectPtrTy, SelectorPtrTy };
  ImpnfABITy = llvm::FunctionType::get(ObjectPtrTy, params, false)
                 ->getPointerTo();

  // struct _class_t {
  //   struct _class_t *isa;
  //   struct _class_t * const superclass;
  //   void *cache;
  //   IMP *vtable;
  //   struct class_ro_t *ro;
  // }

  ClassnfABITy = llvm::StructType::create(VMContext, "struct._class_t");
  ClassnfABITy->setBody(llvm::PointerType::getUnqual(ClassnfABITy),
                        llvm::PointerType::getUnqual(ClassnfABITy), CachePtrTy,
                        llvm::PointerType::getUnqual(ImpnfABITy),
                        llvm::PointerType::getUnqual(ClassRonfABITy));

  // LLVM for struct _class_t *
  ClassnfABIPtrTy = llvm::PointerType::getUnqual(ClassnfABITy);

  // struct _category_t {
  //   const char * const name;
  //   struct _class_t *const cls;
  //   const struct _method_list_t * const instance_methods;
  //   const struct _method_list_t * const class_methods;
  //   const struct _protocol_list_t * const protocols;
  //   const struct _prop_list_t * const properties;
  //   const struct _prop_list_t * const class_properties;
  //   const uint32_t size;
  // }
  CategorynfABITy = llvm::StructType::create(
      "struct._category_t", Int8PtrTy, ClassnfABIPtrTy, MethodListnfABIPtrTy,
      MethodListnfABIPtrTy, ProtocolListnfABIPtrTy, PropertyListPtrTy,
      PropertyListPtrTy, IntTy);

  // New types for nonfragile abi messaging.
  CodeGen::CodeGenTypes &Types = CGM.getTypes();
  ASTContext &Ctx = CGM.getContext();

  // MessageRefTy - LLVM for:
  // struct _message_ref_t {
  //   IMP messenger;
  //   SEL name;
  // };

  // First the clang type for struct _message_ref_t
  RecordDecl *RD = RecordDecl::Create(
      Ctx, TagTypeKind::Struct, Ctx.getTranslationUnitDecl(), SourceLocation(),
      SourceLocation(), &Ctx.Idents.get("_message_ref_t"));
  RD->addDecl(FieldDecl::Create(Ctx, RD, SourceLocation(), SourceLocation(),
                                nullptr, Ctx.VoidPtrTy, nullptr, nullptr, false,
                                ICIS_NoInit));
  RD->addDecl(FieldDecl::Create(Ctx, RD, SourceLocation(), SourceLocation(),
                                nullptr, Ctx.getObjCSelType(), nullptr, nullptr,
                                false, ICIS_NoInit));
  RD->completeDefinition();

  MessageRefCTy = Ctx.getTagDeclType(RD);
  MessageRefCPtrTy = Ctx.getPointerType(MessageRefCTy);
  MessageRefTy = cast<llvm::StructType>(Types.ConvertType(MessageRefCTy));

  // MessageRefPtrTy - LLVM for struct _message_ref_t*
  MessageRefPtrTy = llvm::PointerType::getUnqual(MessageRefTy);

  // SuperMessageRefTy - LLVM for:
  // struct _super_message_ref_t {
  //   SUPER_IMP messenger;
  //   SEL name;
  // };
  SuperMessageRefTy = llvm::StructType::create("struct._super_message_ref_t",
                                               ImpnfABITy, SelectorPtrTy);

  // SuperMessageRefPtrTy - LLVM for struct _super_message_ref_t*
  SuperMessageRefPtrTy = llvm::PointerType::getUnqual(SuperMessageRefTy);


  // struct objc_typeinfo {
  //   const void** vtable; // objc_ehtype_vtable + 2
  //   const char*  name;    // c++ typeinfo string
  //   Class        cls;
  // };
  EHTypeTy = llvm::StructType::create("struct._objc_typeinfo",
                                      llvm::PointerType::getUnqual(Int8PtrTy),
                                      Int8PtrTy, ClassnfABIPtrTy);
  EHTypePtrTy = llvm::PointerType::getUnqual(EHTypeTy);
}

llvm::Function *CGObjCNonFragileABIMac::ModuleInitFunction() {
  FinishNonFragileABIModule();

  return nullptr;
}

void CGObjCNonFragileABIMac::AddModuleClassList(
    ArrayRef<llvm::GlobalValue *> Container, StringRef SymbolName,
    StringRef SectionName) {
  unsigned NumClasses = Container.size();

  if (!NumClasses)
    return;

  SmallVector<llvm::Constant*, 8> Symbols(NumClasses);
  for (unsigned i=0; i<NumClasses; i++)
    Symbols[i] = Container[i];

  llvm::Constant *Init =
    llvm::ConstantArray::get(llvm::ArrayType::get(ObjCTypes.Int8PtrTy,
                                                  Symbols.size()),
                             Symbols);

  // Section name is obtained by calling GetSectionName, which returns
  // sections in the __DATA segment on MachO.
  assert((!CGM.getTriple().isOSBinFormatMachO() ||
          SectionName.starts_with("__DATA")) &&
         "SectionName expected to start with __DATA on MachO");
  llvm::GlobalVariable *GV = new llvm::GlobalVariable(
      CGM.getModule(), Init->getType(), false,
      llvm::GlobalValue::PrivateLinkage, Init, SymbolName);
  GV->setAlignment(CGM.getDataLayout().getABITypeAlign(Init->getType()));
  GV->setSection(SectionName);
  CGM.addCompilerUsedGlobal(GV);
}

void CGObjCNonFragileABIMac::FinishNonFragileABIModule() {
  // nonfragile abi has no module definition.

  // Build list of all implemented class addresses in array
  // L_OBJC_LABEL_CLASS_$.

  for (unsigned i=0, NumClasses=ImplementedClasses.size(); i<NumClasses; i++) {
    const ObjCInterfaceDecl *ID = ImplementedClasses[i];
    assert(ID);
    if (ObjCImplementationDecl *IMP = ID->getImplementation())
      // We are implementing a weak imported interface. Give it external linkage
      if (ID->isWeakImported() && !IMP->isWeakImported()) {
        DefinedClasses[i]->setLinkage(llvm::GlobalVariable::ExternalLinkage);
        DefinedMetaClasses[i]->setLinkage(llvm::GlobalVariable::ExternalLinkage);
      }
  }

  AddModuleClassList(DefinedClasses, "OBJC_LABEL_CLASS_$",
                     GetSectionName("__objc_classlist",
                                    "regular,no_dead_strip"));

  AddModuleClassList(DefinedNonLazyClasses, "OBJC_LABEL_NONLAZY_CLASS_$",
                     GetSectionName("__objc_nlclslist",
                                    "regular,no_dead_strip"));

  // Build list of all implemented category addresses in array
  // L_OBJC_LABEL_CATEGORY_$.
  AddModuleClassList(DefinedCategories, "OBJC_LABEL_CATEGORY_$",
                     GetSectionName("__objc_catlist",
                                    "regular,no_dead_strip"));
  AddModuleClassList(DefinedStubCategories, "OBJC_LABEL_STUB_CATEGORY_$",
                     GetSectionName("__objc_catlist2",
                                    "regular,no_dead_strip"));
  AddModuleClassList(DefinedNonLazyCategories, "OBJC_LABEL_NONLAZY_CATEGORY_$",
                     GetSectionName("__objc_nlcatlist",
                                    "regular,no_dead_strip"));

  EmitImageInfo();
}

/// isVTableDispatchedSelector - Returns true if SEL is not in the list of
/// VTableDispatchMethods; false otherwise. What this means is that
/// except for the 19 selectors in the list, we generate 32bit-style
/// message dispatch call for all the rest.
bool CGObjCNonFragileABIMac::isVTableDispatchedSelector(Selector Sel) {
  // At various points we've experimented with using vtable-based
  // dispatch for all methods.
  switch (CGM.getCodeGenOpts().getObjCDispatchMethod()) {
  case CodeGenOptions::Legacy:
    return false;
  case CodeGenOptions::NonLegacy:
    return true;
  case CodeGenOptions::Mixed:
    break;
  }

  // If so, see whether this selector is in the white-list of things which must
  // use the new dispatch convention. We lazily build a dense set for this.
  if (VTableDispatchMethods.empty()) {
    VTableDispatchMethods.insert(GetNullarySelector("alloc"));
    VTableDispatchMethods.insert(GetNullarySelector("class"));
    VTableDispatchMethods.insert(GetNullarySelector("self"));
    VTableDispatchMethods.insert(GetNullarySelector("isFlipped"));
    VTableDispatchMethods.insert(GetNullarySelector("length"));
    VTableDispatchMethods.insert(GetNullarySelector("count"));

    // These are vtable-based if GC is disabled.
    // Optimistically use vtable dispatch for hybrid compiles.
    if (CGM.getLangOpts().getGC() != LangOptions::GCOnly) {
      VTableDispatchMethods.insert(GetNullarySelector("retain"));
      VTableDispatchMethods.insert(GetNullarySelector("release"));
      VTableDispatchMethods.insert(GetNullarySelector("autorelease"));
    }

    VTableDispatchMethods.insert(GetUnarySelector("allocWithZone"));
    VTableDispatchMethods.insert(GetUnarySelector("isKindOfClass"));
    VTableDispatchMethods.insert(GetUnarySelector("respondsToSelector"));
    VTableDispatchMethods.insert(GetUnarySelector("objectForKey"));
    VTableDispatchMethods.insert(GetUnarySelector("objectAtIndex"));
    VTableDispatchMethods.insert(GetUnarySelector("isEqualToString"));
    VTableDispatchMethods.insert(GetUnarySelector("isEqual"));

    // These are vtable-based if GC is enabled.
    // Optimistically use vtable dispatch for hybrid compiles.
    if (CGM.getLangOpts().getGC() != LangOptions::NonGC) {
      VTableDispatchMethods.insert(GetNullarySelector("hash"));
      VTableDispatchMethods.insert(GetUnarySelector("addObject"));

      // "countByEnumeratingWithState:objects:count"
      const IdentifierInfo *KeyIdents[] = {
          &CGM.getContext().Idents.get("countByEnumeratingWithState"),
          &CGM.getContext().Idents.get("objects"),
          &CGM.getContext().Idents.get("count")};
      VTableDispatchMethods.insert(
        CGM.getContext().Selectors.getSelector(3, KeyIdents));
    }
  }

  return VTableDispatchMethods.count(Sel);
}

/// BuildClassRoTInitializer - generate meta-data for:
/// struct _class_ro_t {
///   uint32_t const flags;
///   uint32_t const instanceStart;
///   uint32_t const instanceSize;
///   uint32_t const reserved;  // only when building for 64bit targets
///   const uint8_t * const ivarLayout;
///   const char *const name;
///   const struct _method_list_t * const baseMethods;
///   const struct _protocol_list_t *const baseProtocols;
///   const struct _ivar_list_t *const ivars;
///   const uint8_t * const weakIvarLayout;
///   const struct _prop_list_t * const properties;
/// }
///
llvm::GlobalVariable * CGObjCNonFragileABIMac::BuildClassRoTInitializer(
  unsigned flags,
  unsigned InstanceStart,
  unsigned InstanceSize,
  const ObjCImplementationDecl *ID) {
  std::string ClassName = std::string(ID->getObjCRuntimeNameAsString());

  CharUnits beginInstance = CharUnits::fromQuantity(InstanceStart);
  CharUnits endInstance = CharUnits::fromQuantity(InstanceSize);

  bool hasMRCWeak = false;
  if (CGM.getLangOpts().ObjCAutoRefCount)
    flags |= NonFragileABI_Class_CompiledByARC;
  else if ((hasMRCWeak = hasMRCWeakIvars(CGM, ID)))
    flags |= NonFragileABI_Class_HasMRCWeakIvars;

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ClassRonfABITy);

  values.addInt(ObjCTypes.IntTy, flags);
  values.addInt(ObjCTypes.IntTy, InstanceStart);
  values.addInt(ObjCTypes.IntTy, InstanceSize);
  values.add((flags & NonFragileABI_Class_Meta)
                ? GetIvarLayoutName(nullptr, ObjCTypes)
                : BuildStrongIvarLayout(ID, beginInstance, endInstance));
  values.add(GetClassName(ID->getObjCRuntimeNameAsString()));

  // const struct _method_list_t * const baseMethods;
  SmallVector<const ObjCMethodDecl*, 16> methods;
  if (flags & NonFragileABI_Class_Meta) {
    for (const auto *MD : ID->class_methods())
      if (!MD->isDirectMethod())
        methods.push_back(MD);
  } else {
    for (const auto *MD : ID->instance_methods())
      if (!MD->isDirectMethod())
        methods.push_back(MD);
  }

  values.add(emitMethodList(ID->getObjCRuntimeNameAsString(),
                            (flags & NonFragileABI_Class_Meta)
                               ? MethodListType::ClassMethods
                               : MethodListType::InstanceMethods,
                            methods));

  const ObjCInterfaceDecl *OID = ID->getClassInterface();
  assert(OID && "CGObjCNonFragileABIMac::BuildClassRoTInitializer");
  values.add(EmitProtocolList("_OBJC_CLASS_PROTOCOLS_$_"
                                + OID->getObjCRuntimeNameAsString(),
                              OID->all_referenced_protocol_begin(),
                              OID->all_referenced_protocol_end()));

  if (flags & NonFragileABI_Class_Meta) {
    values.addNullPointer(ObjCTypes.IvarListnfABIPtrTy);
    values.add(GetIvarLayoutName(nullptr, ObjCTypes));
    values.add(EmitPropertyList(
        "_OBJC_$_CLASS_PROP_LIST_" + ID->getObjCRuntimeNameAsString(),
        ID, ID->getClassInterface(), ObjCTypes, true));
  } else {
    values.add(EmitIvarList(ID));
    values.add(BuildWeakIvarLayout(ID, beginInstance, endInstance, hasMRCWeak));
    values.add(EmitPropertyList(
        "_OBJC_$_PROP_LIST_" + ID->getObjCRuntimeNameAsString(),
        ID, ID->getClassInterface(), ObjCTypes, false));
  }

  llvm::SmallString<64> roLabel;
  llvm::raw_svector_ostream(roLabel)
      << ((flags & NonFragileABI_Class_Meta) ? "_OBJC_METACLASS_RO_$_"
                                             : "_OBJC_CLASS_RO_$_")
      << ClassName;

  return finishAndCreateGlobal(values, roLabel, CGM);
}

/// Build the metaclass object for a class.
///
/// struct _class_t {
///   struct _class_t *isa;
///   struct _class_t * const superclass;
///   void *cache;
///   IMP *vtable;
///   struct class_ro_t *ro;
/// }
///
llvm::GlobalVariable *
CGObjCNonFragileABIMac::BuildClassObject(const ObjCInterfaceDecl *CI,
                                         bool isMetaclass,
                                         llvm::Constant *IsAGV,
                                         llvm::Constant *SuperClassGV,
                                         llvm::Constant *ClassRoGV,
                                         bool HiddenVisibility) {
  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ClassnfABITy);
  values.add(IsAGV);
  if (SuperClassGV) {
    values.add(SuperClassGV);
  } else {
    values.addNullPointer(ObjCTypes.ClassnfABIPtrTy);
  }
  values.add(ObjCEmptyCacheVar);
  values.add(ObjCEmptyVtableVar);
  values.add(ClassRoGV);

  llvm::GlobalVariable *GV =
    cast<llvm::GlobalVariable>(GetClassGlobal(CI, isMetaclass, ForDefinition));
  values.finishAndSetAsInitializer(GV);

  if (CGM.getTriple().isOSBinFormatMachO())
    GV->setSection("__DATA, __objc_data");
  GV->setAlignment(CGM.getDataLayout().getABITypeAlign(ObjCTypes.ClassnfABITy));
  if (!CGM.getTriple().isOSBinFormatCOFF())
    if (HiddenVisibility)
      GV->setVisibility(llvm::GlobalValue::HiddenVisibility);
  return GV;
}

bool CGObjCNonFragileABIMac::ImplementationIsNonLazy(
    const ObjCImplDecl *OD) const {
  return OD->getClassMethod(GetNullarySelector("load")) != nullptr ||
         OD->getClassInterface()->hasAttr<ObjCNonLazyClassAttr>() ||
         OD->hasAttr<ObjCNonLazyClassAttr>();
}

void CGObjCNonFragileABIMac::GetClassSizeInfo(const ObjCImplementationDecl *OID,
                                              uint32_t &InstanceStart,
                                              uint32_t &InstanceSize) {
  const ASTRecordLayout &RL =
    CGM.getContext().getASTObjCImplementationLayout(OID);

  // InstanceSize is really instance end.
  InstanceSize = RL.getDataSize().getQuantity();

  // If there are no fields, the start is the same as the end.
  if (!RL.getFieldCount())
    InstanceStart = InstanceSize;
  else
    InstanceStart = RL.getFieldOffset(0) / CGM.getContext().getCharWidth();
}

static llvm::GlobalValue::DLLStorageClassTypes getStorage(CodeGenModule &CGM,
                                                          StringRef Name) {
  IdentifierInfo &II = CGM.getContext().Idents.get(Name);
  TranslationUnitDecl *TUDecl = CGM.getContext().getTranslationUnitDecl();
  DeclContext *DC = TranslationUnitDecl::castToDeclContext(TUDecl);

  const VarDecl *VD = nullptr;
  for (const auto *Result : DC->lookup(&II))
    if ((VD = dyn_cast<VarDecl>(Result)))
      break;

  if (!VD)
    return llvm::GlobalValue::DLLImportStorageClass;
  if (VD->hasAttr<DLLExportAttr>())
    return llvm::GlobalValue::DLLExportStorageClass;
  if (VD->hasAttr<DLLImportAttr>())
    return llvm::GlobalValue::DLLImportStorageClass;
  return llvm::GlobalValue::DefaultStorageClass;
}

void CGObjCNonFragileABIMac::GenerateClass(const ObjCImplementationDecl *ID) {
  if (!ObjCEmptyCacheVar) {
    ObjCEmptyCacheVar =
        new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.CacheTy, false,
                                 llvm::GlobalValue::ExternalLinkage, nullptr,
                                 "_objc_empty_cache");
    if (CGM.getTriple().isOSBinFormatCOFF())
      ObjCEmptyCacheVar->setDLLStorageClass(getStorage(CGM, "_objc_empty_cache"));

    // Only OS X with deployment version <10.9 use the empty vtable symbol
    const llvm::Triple &Triple = CGM.getTarget().getTriple();
    if (Triple.isMacOSX() && Triple.isMacOSXVersionLT(10, 9))
      ObjCEmptyVtableVar =
          new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.ImpnfABITy, false,
                                   llvm::GlobalValue::ExternalLinkage, nullptr,
                                   "_objc_empty_vtable");
    else
      ObjCEmptyVtableVar =
        llvm::ConstantPointerNull::get(ObjCTypes.ImpnfABITy->getPointerTo());
  }

  // FIXME: Is this correct (that meta class size is never computed)?
  uint32_t InstanceStart =
    CGM.getDataLayout().getTypeAllocSize(ObjCTypes.ClassnfABITy);
  uint32_t InstanceSize = InstanceStart;
  uint32_t flags = NonFragileABI_Class_Meta;

  llvm::Constant *SuperClassGV, *IsAGV;

  const auto *CI = ID->getClassInterface();
  assert(CI && "CGObjCNonFragileABIMac::GenerateClass - class is 0");

  // Build the flags for the metaclass.
  bool classIsHidden = (CGM.getTriple().isOSBinFormatCOFF())
                           ? !CI->hasAttr<DLLExportAttr>()
                           : CI->getVisibility() == HiddenVisibility;
  if (classIsHidden)
    flags |= NonFragileABI_Class_Hidden;

  // FIXME: why is this flag set on the metaclass?
  // ObjC metaclasses have no fields and don't really get constructed.
  if (ID->hasNonZeroConstructors() || ID->hasDestructors()) {
    flags |= NonFragileABI_Class_HasCXXStructors;
    if (!ID->hasNonZeroConstructors())
      flags |= NonFragileABI_Class_HasCXXDestructorOnly;
  }

  if (!CI->getSuperClass()) {
    // class is root
    flags |= NonFragileABI_Class_Root;

    SuperClassGV = GetClassGlobal(CI, /*metaclass*/ false, NotForDefinition);
    IsAGV = GetClassGlobal(CI, /*metaclass*/ true, NotForDefinition);
  } else {
    // Has a root. Current class is not a root.
    const ObjCInterfaceDecl *Root = ID->getClassInterface();
    while (const ObjCInterfaceDecl *Super = Root->getSuperClass())
      Root = Super;

    const auto *Super = CI->getSuperClass();
    IsAGV = GetClassGlobal(Root, /*metaclass*/ true, NotForDefinition);
    SuperClassGV = GetClassGlobal(Super, /*metaclass*/ true, NotForDefinition);
  }

  llvm::GlobalVariable *CLASS_RO_GV =
      BuildClassRoTInitializer(flags, InstanceStart, InstanceSize, ID);

  llvm::GlobalVariable *MetaTClass =
    BuildClassObject(CI, /*metaclass*/ true,
                     IsAGV, SuperClassGV, CLASS_RO_GV, classIsHidden);
  CGM.setGVProperties(MetaTClass, CI);
  DefinedMetaClasses.push_back(MetaTClass);

  // Metadata for the class
  flags = 0;
  if (classIsHidden)
    flags |= NonFragileABI_Class_Hidden;

  if (ID->hasNonZeroConstructors() || ID->hasDestructors()) {
    flags |= NonFragileABI_Class_HasCXXStructors;

    // Set a flag to enable a runtime optimization when a class has
    // fields that require destruction but which don't require
    // anything except zero-initialization during construction.  This
    // is most notably true of __strong and __weak types, but you can
    // also imagine there being C++ types with non-trivial default
    // constructors that merely set all fields to null.
    if (!ID->hasNonZeroConstructors())
      flags |= NonFragileABI_Class_HasCXXDestructorOnly;
  }

  if (hasObjCExceptionAttribute(CGM.getContext(), CI))
    flags |= NonFragileABI_Class_Exception;

  if (!CI->getSuperClass()) {
    flags |= NonFragileABI_Class_Root;
    SuperClassGV = nullptr;
  } else {
    // Has a root. Current class is not a root.
    const auto *Super = CI->getSuperClass();
    SuperClassGV = GetClassGlobal(Super, /*metaclass*/ false, NotForDefinition);
  }

  GetClassSizeInfo(ID, InstanceStart, InstanceSize);
  CLASS_RO_GV =
      BuildClassRoTInitializer(flags, InstanceStart, InstanceSize, ID);

  llvm::GlobalVariable *ClassMD =
    BuildClassObject(CI, /*metaclass*/ false,
                     MetaTClass, SuperClassGV, CLASS_RO_GV, classIsHidden);
  CGM.setGVProperties(ClassMD, CI);
  DefinedClasses.push_back(ClassMD);
  ImplementedClasses.push_back(CI);

  // Determine if this class is also "non-lazy".
  if (ImplementationIsNonLazy(ID))
    DefinedNonLazyClasses.push_back(ClassMD);

  // Force the definition of the EHType if necessary.
  if (flags & NonFragileABI_Class_Exception)
    (void) GetInterfaceEHType(CI, ForDefinition);
  // Make sure method definition entries are all clear for next implementation.
  MethodDefinitions.clear();
}

/// GenerateProtocolRef - This routine is called to generate code for
/// a protocol reference expression; as in:
/// @code
///   @protocol(Proto1);
/// @endcode
/// It generates a weak reference to l_OBJC_PROTOCOL_REFERENCE_$_Proto1
/// which will hold address of the protocol meta-data.
///
llvm::Value *CGObjCNonFragileABIMac::GenerateProtocolRef(CodeGenFunction &CGF,
                                                         const ObjCProtocolDecl *PD) {

  // This routine is called for @protocol only. So, we must build definition
  // of protocol's meta-data (not a reference to it!)
  assert(!PD->isNonRuntimeProtocol() &&
         "attempting to get a protocol ref to a static protocol.");
  llvm::Constant *Init = GetOrEmitProtocol(PD);

  std::string ProtocolName("_OBJC_PROTOCOL_REFERENCE_$_");
  ProtocolName += PD->getObjCRuntimeNameAsString();

  CharUnits Align = CGF.getPointerAlign();

  llvm::GlobalVariable *PTGV = CGM.getModule().getGlobalVariable(ProtocolName);
  if (PTGV)
    return CGF.Builder.CreateAlignedLoad(PTGV->getValueType(), PTGV, Align);
  PTGV = new llvm::GlobalVariable(CGM.getModule(), Init->getType(), false,
                                  llvm::GlobalValue::WeakAnyLinkage, Init,
                                  ProtocolName);
  PTGV->setSection(GetSectionName("__objc_protorefs",
                                  "coalesced,no_dead_strip"));
  PTGV->setVisibility(llvm::GlobalValue::HiddenVisibility);
  PTGV->setAlignment(Align.getAsAlign());
  if (!CGM.getTriple().isOSBinFormatMachO())
    PTGV->setComdat(CGM.getModule().getOrInsertComdat(ProtocolName));
  CGM.addUsedGlobal(PTGV);
  return CGF.Builder.CreateAlignedLoad(PTGV->getValueType(), PTGV, Align);
}

/// GenerateCategory - Build metadata for a category implementation.
/// struct _category_t {
///   const char * const name;
///   struct _class_t *const cls;
///   const struct _method_list_t * const instance_methods;
///   const struct _method_list_t * const class_methods;
///   const struct _protocol_list_t * const protocols;
///   const struct _prop_list_t * const properties;
///   const struct _prop_list_t * const class_properties;
///   const uint32_t size;
/// }
///
void CGObjCNonFragileABIMac::GenerateCategory(const ObjCCategoryImplDecl *OCD) {
  const ObjCInterfaceDecl *Interface = OCD->getClassInterface();
  const char *Prefix = "_OBJC_$_CATEGORY_";

  llvm::SmallString<64> ExtCatName(Prefix);
  ExtCatName += Interface->getObjCRuntimeNameAsString();
  ExtCatName += "_$_";
  ExtCatName += OCD->getNameAsString();

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.CategorynfABITy);
  values.add(GetClassName(OCD->getIdentifier()->getName()));
  // meta-class entry symbol
  values.add(GetClassGlobal(Interface, /*metaclass*/ false, NotForDefinition));
  std::string listName =
      (Interface->getObjCRuntimeNameAsString() + "_$_" + OCD->getName()).str();

  SmallVector<const ObjCMethodDecl *, 16> instanceMethods;
  SmallVector<const ObjCMethodDecl *, 8> classMethods;
  for (const auto *MD : OCD->methods()) {
    if (MD->isDirectMethod())
      continue;
    if (MD->isInstanceMethod()) {
      instanceMethods.push_back(MD);
    } else {
      classMethods.push_back(MD);
    }
  }

  auto instanceMethodList = emitMethodList(
      listName, MethodListType::CategoryInstanceMethods, instanceMethods);
  auto classMethodList = emitMethodList(
      listName, MethodListType::CategoryClassMethods, classMethods);
  values.add(instanceMethodList);
  values.add(classMethodList);
  // Keep track of whether we have actual metadata to emit.
  bool isEmptyCategory =
      instanceMethodList->isNullValue() && classMethodList->isNullValue();

  const ObjCCategoryDecl *Category =
      Interface->FindCategoryDeclaration(OCD->getIdentifier());
  if (Category) {
    SmallString<256> ExtName;
    llvm::raw_svector_ostream(ExtName)
        << Interface->getObjCRuntimeNameAsString() << "_$_" << OCD->getName();
    auto protocolList =
        EmitProtocolList("_OBJC_CATEGORY_PROTOCOLS_$_" +
                             Interface->getObjCRuntimeNameAsString() + "_$_" +
                             Category->getName(),
                         Category->protocol_begin(), Category->protocol_end());
    auto propertyList = EmitPropertyList("_OBJC_$_PROP_LIST_" + ExtName.str(),
                                         OCD, Category, ObjCTypes, false);
    auto classPropertyList =
        EmitPropertyList("_OBJC_$_CLASS_PROP_LIST_" + ExtName.str(), OCD,
                         Category, ObjCTypes, true);
    values.add(protocolList);
    values.add(propertyList);
    values.add(classPropertyList);
    isEmptyCategory &= protocolList->isNullValue() &&
                       propertyList->isNullValue() &&
                       classPropertyList->isNullValue();
  } else {
    values.addNullPointer(ObjCTypes.ProtocolListnfABIPtrTy);
    values.addNullPointer(ObjCTypes.PropertyListPtrTy);
    values.addNullPointer(ObjCTypes.PropertyListPtrTy);
  }

  if (isEmptyCategory) {
    // Empty category, don't emit any metadata.
    values.abandon();
    MethodDefinitions.clear();
    return;
  }

  unsigned Size =
      CGM.getDataLayout().getTypeAllocSize(ObjCTypes.CategorynfABITy);
  values.addInt(ObjCTypes.IntTy, Size);

  llvm::GlobalVariable *GCATV =
      finishAndCreateGlobal(values, ExtCatName.str(), CGM);
  CGM.addCompilerUsedGlobal(GCATV);
  if (Interface->hasAttr<ObjCClassStubAttr>())
    DefinedStubCategories.push_back(GCATV);
  else
    DefinedCategories.push_back(GCATV);

  // Determine if this category is also "non-lazy".
  if (ImplementationIsNonLazy(OCD))
    DefinedNonLazyCategories.push_back(GCATV);
  // method definition entries must be clear for next implementation.
  MethodDefinitions.clear();
}

/// emitMethodConstant - Return a struct objc_method constant.  If
/// forProtocol is true, the implementation will be null; otherwise,
/// the method must have a definition registered with the runtime.
///
/// struct _objc_method {
///   SEL _cmd;
///   char *method_type;
///   char *_imp;
/// }
void CGObjCNonFragileABIMac::emitMethodConstant(ConstantArrayBuilder &builder,
                                                const ObjCMethodDecl *MD,
                                                bool forProtocol) {
  auto method = builder.beginStruct(ObjCTypes.MethodTy);
  method.add(GetMethodVarName(MD->getSelector()));
  method.add(GetMethodVarType(MD));

  if (forProtocol) {
    // Protocol methods have no implementation. So, this entry is always NULL.
    method.addNullPointer(ObjCTypes.Int8PtrProgramASTy);
  } else {
    llvm::Function *fn = GetMethodDefinition(MD);
    assert(fn && "no definition for method?");
    method.add(fn);
  }

  method.finishAndAddTo(builder);
}

/// Build meta-data for method declarations.
///
/// struct _method_list_t {
///   uint32_t entsize;  // sizeof(struct _objc_method)
///   uint32_t method_count;
///   struct _objc_method method_list[method_count];
/// }
///
llvm::Constant *
CGObjCNonFragileABIMac::emitMethodList(Twine name, MethodListType kind,
                              ArrayRef<const ObjCMethodDecl *> methods) {
  // Return null for empty list.
  if (methods.empty())
    return llvm::Constant::getNullValue(ObjCTypes.MethodListnfABIPtrTy);

  StringRef prefix;
  bool forProtocol;
  switch (kind) {
  case MethodListType::CategoryInstanceMethods:
    prefix = "_OBJC_$_CATEGORY_INSTANCE_METHODS_";
    forProtocol = false;
    break;
  case MethodListType::CategoryClassMethods:
    prefix = "_OBJC_$_CATEGORY_CLASS_METHODS_";
    forProtocol = false;
    break;
  case MethodListType::InstanceMethods:
    prefix = "_OBJC_$_INSTANCE_METHODS_";
    forProtocol = false;
    break;
  case MethodListType::ClassMethods:
    prefix = "_OBJC_$_CLASS_METHODS_";
    forProtocol = false;
    break;

  case MethodListType::ProtocolInstanceMethods:
    prefix = "_OBJC_$_PROTOCOL_INSTANCE_METHODS_";
    forProtocol = true;
    break;
  case MethodListType::ProtocolClassMethods:
    prefix = "_OBJC_$_PROTOCOL_CLASS_METHODS_";
    forProtocol = true;
    break;
  case MethodListType::OptionalProtocolInstanceMethods:
    prefix = "_OBJC_$_PROTOCOL_INSTANCE_METHODS_OPT_";
    forProtocol = true;
    break;
  case MethodListType::OptionalProtocolClassMethods:
    prefix = "_OBJC_$_PROTOCOL_CLASS_METHODS_OPT_";
    forProtocol = true;
    break;
  }

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct();

  // sizeof(struct _objc_method)
  unsigned Size = CGM.getDataLayout().getTypeAllocSize(ObjCTypes.MethodTy);
  values.addInt(ObjCTypes.IntTy, Size);
  // method_count
  values.addInt(ObjCTypes.IntTy, methods.size());
  auto methodArray = values.beginArray(ObjCTypes.MethodTy);
  for (auto MD : methods)
    emitMethodConstant(methodArray, MD, forProtocol);
  methodArray.finishAndAddTo(values);

  llvm::GlobalVariable *GV = finishAndCreateGlobal(values, prefix + name, CGM);
  CGM.addCompilerUsedGlobal(GV);
  return GV;
}

/// ObjCIvarOffsetVariable - Returns the ivar offset variable for
/// the given ivar.
llvm::GlobalVariable *
CGObjCNonFragileABIMac::ObjCIvarOffsetVariable(const ObjCInterfaceDecl *ID,
                                               const ObjCIvarDecl *Ivar) {
  const ObjCInterfaceDecl *Container = Ivar->getContainingInterface();
  llvm::SmallString<64> Name("OBJC_IVAR_$_");
  Name += Container->getObjCRuntimeNameAsString();
  Name += ".";
  Name += Ivar->getName();
  llvm::GlobalVariable *IvarOffsetGV = CGM.getModule().getGlobalVariable(Name);
  if (!IvarOffsetGV) {
    IvarOffsetGV =
        new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.IvarOffsetVarTy,
                                 false, llvm::GlobalValue::ExternalLinkage,
                                 nullptr, Name.str());
    if (CGM.getTriple().isOSBinFormatCOFF()) {
      bool IsPrivateOrPackage =
          Ivar->getAccessControl() == ObjCIvarDecl::Private ||
          Ivar->getAccessControl() == ObjCIvarDecl::Package;

      const ObjCInterfaceDecl *ContainingID = Ivar->getContainingInterface();

      if (ContainingID->hasAttr<DLLImportAttr>())
        IvarOffsetGV
            ->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
      else if (ContainingID->hasAttr<DLLExportAttr>() && !IsPrivateOrPackage)
        IvarOffsetGV
            ->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
    }
  }
  return IvarOffsetGV;
}

llvm::Constant *
CGObjCNonFragileABIMac::EmitIvarOffsetVar(const ObjCInterfaceDecl *ID,
                                          const ObjCIvarDecl *Ivar,
                                          unsigned long int Offset) {
  llvm::GlobalVariable *IvarOffsetGV = ObjCIvarOffsetVariable(ID, Ivar);
  IvarOffsetGV->setInitializer(
      llvm::ConstantInt::get(ObjCTypes.IvarOffsetVarTy, Offset));
  IvarOffsetGV->setAlignment(
      CGM.getDataLayout().getABITypeAlign(ObjCTypes.IvarOffsetVarTy));

  if (!CGM.getTriple().isOSBinFormatCOFF()) {
    // FIXME: This matches gcc, but shouldn't the visibility be set on the use
    // as well (i.e., in ObjCIvarOffsetVariable).
    if (Ivar->getAccessControl() == ObjCIvarDecl::Private ||
        Ivar->getAccessControl() == ObjCIvarDecl::Package ||
        ID->getVisibility() == HiddenVisibility)
      IvarOffsetGV->setVisibility(llvm::GlobalValue::HiddenVisibility);
    else
      IvarOffsetGV->setVisibility(llvm::GlobalValue::DefaultVisibility);
  }

  // If ID's layout is known, then make the global constant. This serves as a
  // useful assertion: we'll never use this variable to calculate ivar offsets,
  // so if the runtime tries to patch it then we should crash.
  if (isClassLayoutKnownStatically(ID))
    IvarOffsetGV->setConstant(true);

  if (CGM.getTriple().isOSBinFormatMachO())
    IvarOffsetGV->setSection("__DATA, __objc_ivar");
  return IvarOffsetGV;
}

/// EmitIvarList - Emit the ivar list for the given
/// implementation. The return value has type
/// IvarListnfABIPtrTy.
///  struct _ivar_t {
///   unsigned [long] int *offset;  // pointer to ivar offset location
///   char *name;
///   char *type;
///   uint32_t alignment;
///   uint32_t size;
/// }
/// struct _ivar_list_t {
///   uint32 entsize;  // sizeof(struct _ivar_t)
///   uint32 count;
///   struct _iver_t list[count];
/// }
///

llvm::Constant *CGObjCNonFragileABIMac::EmitIvarList(
  const ObjCImplementationDecl *ID) {

  ConstantInitBuilder builder(CGM);
  auto ivarList = builder.beginStruct();
  ivarList.addInt(ObjCTypes.IntTy,
                  CGM.getDataLayout().getTypeAllocSize(ObjCTypes.IvarnfABITy));
  auto ivarCountSlot = ivarList.addPlaceholder();
  auto ivars = ivarList.beginArray(ObjCTypes.IvarnfABITy);

  const ObjCInterfaceDecl *OID = ID->getClassInterface();
  assert(OID && "CGObjCNonFragileABIMac::EmitIvarList - null interface");

  // FIXME. Consolidate this with similar code in GenerateClass.

  for (const ObjCIvarDecl *IVD = OID->all_declared_ivar_begin();
       IVD; IVD = IVD->getNextIvar()) {
    // Ignore unnamed bit-fields.
    if (!IVD->getDeclName())
      continue;

    auto ivar = ivars.beginStruct(ObjCTypes.IvarnfABITy);
    ivar.add(EmitIvarOffsetVar(ID->getClassInterface(), IVD,
                               ComputeIvarBaseOffset(CGM, ID, IVD)));
    ivar.add(GetMethodVarName(IVD->getIdentifier()));
    ivar.add(GetMethodVarType(IVD));
    llvm::Type *FieldTy =
      CGM.getTypes().ConvertTypeForMem(IVD->getType());
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(FieldTy);
    unsigned Align = CGM.getContext().getPreferredTypeAlign(
      IVD->getType().getTypePtr()) >> 3;
    Align = llvm::Log2_32(Align);
    ivar.addInt(ObjCTypes.IntTy, Align);
    // NOTE. Size of a bitfield does not match gcc's, because of the
    // way bitfields are treated special in each. But I am told that
    // 'size' for bitfield ivars is ignored by the runtime so it does
    // not matter.  If it matters, there is enough info to get the
    // bitfield right!
    ivar.addInt(ObjCTypes.IntTy, Size);
    ivar.finishAndAddTo(ivars);
  }
  // Return null for empty list.
  if (ivars.empty()) {
    ivars.abandon();
    ivarList.abandon();
    return llvm::Constant::getNullValue(ObjCTypes.IvarListnfABIPtrTy);
  }

  auto ivarCount = ivars.size();
  ivars.finishAndAddTo(ivarList);
  ivarList.fillPlaceholderWithInt(ivarCountSlot, ObjCTypes.IntTy, ivarCount);

  const char *Prefix = "_OBJC_$_INSTANCE_VARIABLES_";
  llvm::GlobalVariable *GV = finishAndCreateGlobal(
      ivarList, Prefix + OID->getObjCRuntimeNameAsString(), CGM);
  CGM.addCompilerUsedGlobal(GV);
  return GV;
}

llvm::Constant *CGObjCNonFragileABIMac::GetOrEmitProtocolRef(
  const ObjCProtocolDecl *PD) {
  llvm::GlobalVariable *&Entry = Protocols[PD->getIdentifier()];

  assert(!PD->isNonRuntimeProtocol() &&
         "attempting to GetOrEmit a non-runtime protocol");
  if (!Entry) {
    // We use the initializer as a marker of whether this is a forward
    // reference or not. At module finalization we add the empty
    // contents for protocols which were referenced but never defined.
    llvm::SmallString<64> Protocol;
    llvm::raw_svector_ostream(Protocol) << "_OBJC_PROTOCOL_$_"
                                        << PD->getObjCRuntimeNameAsString();

    Entry = new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.ProtocolnfABITy,
                                     false, llvm::GlobalValue::ExternalLinkage,
                                     nullptr, Protocol);
    if (!CGM.getTriple().isOSBinFormatMachO())
      Entry->setComdat(CGM.getModule().getOrInsertComdat(Protocol));
  }

  return Entry;
}

/// GetOrEmitProtocol - Generate the protocol meta-data:
/// @code
/// struct _protocol_t {
///   id isa;  // NULL
///   const char * const protocol_name;
///   const struct _protocol_list_t * protocol_list; // super protocols
///   const struct method_list_t * const instance_methods;
///   const struct method_list_t * const class_methods;
///   const struct method_list_t *optionalInstanceMethods;
///   const struct method_list_t *optionalClassMethods;
///   const struct _prop_list_t * properties;
///   const uint32_t size;  // sizeof(struct _protocol_t)
///   const uint32_t flags;  // = 0
///   const char ** extendedMethodTypes;
///   const char *demangledName;
///   const struct _prop_list_t * class_properties;
/// }
/// @endcode
///

llvm::Constant *CGObjCNonFragileABIMac::GetOrEmitProtocol(
  const ObjCProtocolDecl *PD) {
  llvm::GlobalVariable *Entry = Protocols[PD->getIdentifier()];

  // Early exit if a defining object has already been generated.
  if (Entry && Entry->hasInitializer())
    return Entry;

  // Use the protocol definition, if there is one.
  assert(PD->hasDefinition() &&
         "emitting protocol metadata without definition");
  PD = PD->getDefinition();

  auto methodLists = ProtocolMethodLists::get(PD);

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.ProtocolnfABITy);

  // isa is NULL
  values.addNullPointer(ObjCTypes.ObjectPtrTy);
  values.add(GetClassName(PD->getObjCRuntimeNameAsString()));
  values.add(EmitProtocolList("_OBJC_$_PROTOCOL_REFS_"
                                + PD->getObjCRuntimeNameAsString(),
                               PD->protocol_begin(),
                               PD->protocol_end()));
  values.add(methodLists.emitMethodList(this, PD,
                                 ProtocolMethodLists::RequiredInstanceMethods));
  values.add(methodLists.emitMethodList(this, PD,
                                 ProtocolMethodLists::RequiredClassMethods));
  values.add(methodLists.emitMethodList(this, PD,
                                 ProtocolMethodLists::OptionalInstanceMethods));
  values.add(methodLists.emitMethodList(this, PD,
                                 ProtocolMethodLists::OptionalClassMethods));
  values.add(EmitPropertyList(
               "_OBJC_$_PROP_LIST_" + PD->getObjCRuntimeNameAsString(),
               nullptr, PD, ObjCTypes, false));
  uint32_t Size =
    CGM.getDataLayout().getTypeAllocSize(ObjCTypes.ProtocolnfABITy);
  values.addInt(ObjCTypes.IntTy, Size);
  values.addInt(ObjCTypes.IntTy, 0);
  values.add(EmitProtocolMethodTypes("_OBJC_$_PROTOCOL_METHOD_TYPES_"
                                       + PD->getObjCRuntimeNameAsString(),
                                     methodLists.emitExtendedTypesArray(this),
                                     ObjCTypes));

  // const char *demangledName;
  values.addNullPointer(ObjCTypes.Int8PtrTy);

  values.add(EmitPropertyList(
      "_OBJC_$_CLASS_PROP_LIST_" + PD->getObjCRuntimeNameAsString(),
      nullptr, PD, ObjCTypes, true));

  if (Entry) {
    // Already created, fix the linkage and update the initializer.
    Entry->setLinkage(llvm::GlobalValue::WeakAnyLinkage);
    values.finishAndSetAsInitializer(Entry);
  } else {
    llvm::SmallString<64> symbolName;
    llvm::raw_svector_ostream(symbolName)
      << "_OBJC_PROTOCOL_$_" << PD->getObjCRuntimeNameAsString();

    Entry = values.finishAndCreateGlobal(symbolName, CGM.getPointerAlign(),
                                         /*constant*/ false,
                                         llvm::GlobalValue::WeakAnyLinkage);
    if (!CGM.getTriple().isOSBinFormatMachO())
      Entry->setComdat(CGM.getModule().getOrInsertComdat(symbolName));

    Protocols[PD->getIdentifier()] = Entry;
  }
  Entry->setVisibility(llvm::GlobalValue::HiddenVisibility);
  CGM.addUsedGlobal(Entry);

  // Use this protocol meta-data to build protocol list table in section
  // __DATA, __objc_protolist
  llvm::SmallString<64> ProtocolRef;
  llvm::raw_svector_ostream(ProtocolRef) << "_OBJC_LABEL_PROTOCOL_$_"
                                         << PD->getObjCRuntimeNameAsString();

  llvm::GlobalVariable *PTGV =
    new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.ProtocolnfABIPtrTy,
                             false, llvm::GlobalValue::WeakAnyLinkage, Entry,
                             ProtocolRef);
  if (!CGM.getTriple().isOSBinFormatMachO())
    PTGV->setComdat(CGM.getModule().getOrInsertComdat(ProtocolRef));
  PTGV->setAlignment(
      CGM.getDataLayout().getABITypeAlign(ObjCTypes.ProtocolnfABIPtrTy));
  PTGV->setSection(GetSectionName("__objc_protolist",
                                  "coalesced,no_dead_strip"));
  PTGV->setVisibility(llvm::GlobalValue::HiddenVisibility);
  CGM.addUsedGlobal(PTGV);
  return Entry;
}

/// EmitProtocolList - Generate protocol list meta-data:
/// @code
/// struct _protocol_list_t {
///   long protocol_count;   // Note, this is 32/64 bit
///   struct _protocol_t[protocol_count];
/// }
/// @endcode
///
llvm::Constant *
CGObjCNonFragileABIMac::EmitProtocolList(Twine Name,
                                      ObjCProtocolDecl::protocol_iterator begin,
                                      ObjCProtocolDecl::protocol_iterator end) {
  // Just return null for empty protocol lists
  auto Protocols = GetRuntimeProtocolList(begin, end);
  if (Protocols.empty())
    return llvm::Constant::getNullValue(ObjCTypes.ProtocolListnfABIPtrTy);

  SmallVector<llvm::Constant *, 16> ProtocolRefs;
  ProtocolRefs.reserve(Protocols.size());

  for (const auto *PD : Protocols)
    ProtocolRefs.push_back(GetProtocolRef(PD));

  // If all of the protocols in the protocol list are objc_non_runtime_protocol
  // just return null
  if (ProtocolRefs.size() == 0)
    return llvm::Constant::getNullValue(ObjCTypes.ProtocolListnfABIPtrTy);

  // FIXME: We shouldn't need to do this lookup here, should we?
  SmallString<256> TmpName;
  Name.toVector(TmpName);
  llvm::GlobalVariable *GV =
    CGM.getModule().getGlobalVariable(TmpName.str(), true);
  if (GV)
    return GV;

  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct();
  auto countSlot = values.addPlaceholder();

  // A null-terminated array of protocols.
  auto array = values.beginArray(ObjCTypes.ProtocolnfABIPtrTy);
  for (auto const &proto : ProtocolRefs)
    array.add(proto);
  auto count = array.size();
  array.addNullPointer(ObjCTypes.ProtocolnfABIPtrTy);

  array.finishAndAddTo(values);
  values.fillPlaceholderWithInt(countSlot, ObjCTypes.LongTy, count);

  GV = finishAndCreateGlobal(values, Name, CGM);
  CGM.addCompilerUsedGlobal(GV);
  return GV;
}

/// EmitObjCValueForIvar - Code Gen for nonfragile ivar reference.
/// This code gen. amounts to generating code for:
/// @code
/// (type *)((char *)base + _OBJC_IVAR_$_.ivar;
/// @encode
///
LValue CGObjCNonFragileABIMac::EmitObjCValueForIvar(
                                               CodeGen::CodeGenFunction &CGF,
                                               QualType ObjectTy,
                                               llvm::Value *BaseValue,
                                               const ObjCIvarDecl *Ivar,
                                               unsigned CVRQualifiers) {
  ObjCInterfaceDecl *ID = ObjectTy->castAs<ObjCObjectType>()->getInterface();
  llvm::Value *Offset = EmitIvarOffset(CGF, ID, Ivar);
  return EmitValueForIvarAtOffset(CGF, ID, BaseValue, Ivar, CVRQualifiers,
                                  Offset);
}

llvm::Value *
CGObjCNonFragileABIMac::EmitIvarOffset(CodeGen::CodeGenFunction &CGF,
                                       const ObjCInterfaceDecl *Interface,
                                       const ObjCIvarDecl *Ivar) {
  llvm::Value *IvarOffsetValue;
  if (isClassLayoutKnownStatically(Interface)) {
    IvarOffsetValue = llvm::ConstantInt::get(
        ObjCTypes.IvarOffsetVarTy,
        ComputeIvarBaseOffset(CGM, Interface->getImplementation(), Ivar));
  } else {
    llvm::GlobalVariable *GV = ObjCIvarOffsetVariable(Interface, Ivar);
    IvarOffsetValue =
        CGF.Builder.CreateAlignedLoad(GV->getValueType(), GV,
                                      CGF.getSizeAlign(), "ivar");
    if (IsIvarOffsetKnownIdempotent(CGF, Ivar))
      cast<llvm::LoadInst>(IvarOffsetValue)
          ->setMetadata(llvm::LLVMContext::MD_invariant_load,
                        llvm::MDNode::get(VMContext, std::nullopt));
  }

  // This could be 32bit int or 64bit integer depending on the architecture.
  // Cast it to 64bit integer value, if it is a 32bit integer ivar offset value
  //  as this is what caller always expects.
  if (ObjCTypes.IvarOffsetVarTy == ObjCTypes.IntTy)
    IvarOffsetValue = CGF.Builder.CreateIntCast(
        IvarOffsetValue, ObjCTypes.LongTy, true, "ivar.conv");
  return IvarOffsetValue;
}

static void appendSelectorForMessageRefTable(std::string &buffer,
                                             Selector selector) {
  if (selector.isUnarySelector()) {
    buffer += selector.getNameForSlot(0);
    return;
  }

  for (unsigned i = 0, e = selector.getNumArgs(); i != e; ++i) {
    buffer += selector.getNameForSlot(i);
    buffer += '_';
  }
}

/// Emit a "vtable" message send.  We emit a weak hidden-visibility
/// struct, initially containing the selector pointer and a pointer to
/// a "fixup" variant of the appropriate objc_msgSend.  To call, we
/// load and call the function pointer, passing the address of the
/// struct as the second parameter.  The runtime determines whether
/// the selector is currently emitted using vtable dispatch; if so, it
/// substitutes a stub function which simply tail-calls through the
/// appropriate vtable slot, and if not, it substitues a stub function
/// which tail-calls objc_msgSend.  Both stubs adjust the selector
/// argument to correctly point to the selector.
RValue
CGObjCNonFragileABIMac::EmitVTableMessageSend(CodeGenFunction &CGF,
                                              ReturnValueSlot returnSlot,
                                              QualType resultType,
                                              Selector selector,
                                              llvm::Value *arg0,
                                              QualType arg0Type,
                                              bool isSuper,
                                              const CallArgList &formalArgs,
                                              const ObjCMethodDecl *method) {
  // Compute the actual arguments.
  CallArgList args;

  // First argument: the receiver / super-call structure.
  if (!isSuper)
    arg0 = CGF.Builder.CreateBitCast(arg0, ObjCTypes.ObjectPtrTy);
  args.add(RValue::get(arg0), arg0Type);

  // Second argument: a pointer to the message ref structure.  Leave
  // the actual argument value blank for now.
  args.add(RValue::get(nullptr), ObjCTypes.MessageRefCPtrTy);

  args.insert(args.end(), formalArgs.begin(), formalArgs.end());

  MessageSendInfo MSI = getMessageSendInfo(method, resultType, args);

  NullReturnState nullReturn;

  // Find the function to call and the mangled name for the message
  // ref structure.  Using a different mangled name wouldn't actually
  // be a problem; it would just be a waste.
  //
  // The runtime currently never uses vtable dispatch for anything
  // except normal, non-super message-sends.
  // FIXME: don't use this for that.
  llvm::FunctionCallee fn = nullptr;
  std::string messageRefName("_");
  if (CGM.ReturnSlotInterferesWithArgs(MSI.CallInfo)) {
    if (isSuper) {
      fn = ObjCTypes.getMessageSendSuper2StretFixupFn();
      messageRefName += "objc_msgSendSuper2_stret_fixup";
    } else {
      nullReturn.init(CGF, arg0);
      fn = ObjCTypes.getMessageSendStretFixupFn();
      messageRefName += "objc_msgSend_stret_fixup";
    }
  } else if (!isSuper && CGM.ReturnTypeUsesFPRet(resultType)) {
    fn = ObjCTypes.getMessageSendFpretFixupFn();
    messageRefName += "objc_msgSend_fpret_fixup";
  } else {
    if (isSuper) {
      fn = ObjCTypes.getMessageSendSuper2FixupFn();
      messageRefName += "objc_msgSendSuper2_fixup";
    } else {
      fn = ObjCTypes.getMessageSendFixupFn();
      messageRefName += "objc_msgSend_fixup";
    }
  }
  assert(fn && "CGObjCNonFragileABIMac::EmitMessageSend");
  messageRefName += '_';

  // Append the selector name, except use underscores anywhere we
  // would have used colons.
  appendSelectorForMessageRefTable(messageRefName, selector);

  llvm::GlobalVariable *messageRef
    = CGM.getModule().getGlobalVariable(messageRefName);
  if (!messageRef) {
    // Build the message ref structure.
    ConstantInitBuilder builder(CGM);
    auto values = builder.beginStruct();
    values.add(cast<llvm::Constant>(fn.getCallee()));
    values.add(GetMethodVarName(selector));
    messageRef = values.finishAndCreateGlobal(messageRefName,
                                              CharUnits::fromQuantity(16),
                                              /*constant*/ false,
                                        llvm::GlobalValue::WeakAnyLinkage);
    messageRef->setVisibility(llvm::GlobalValue::HiddenVisibility);
    messageRef->setSection(GetSectionName("__objc_msgrefs", "coalesced"));
  }

  bool requiresnullCheck = false;
  if (CGM.getLangOpts().ObjCAutoRefCount && method)
    for (const auto *ParamDecl : method->parameters()) {
      if (ParamDecl->isDestroyedInCallee()) {
        if (!nullReturn.NullBB)
          nullReturn.init(CGF, arg0);
        requiresnullCheck = true;
        break;
      }
    }

  Address mref =
    Address(CGF.Builder.CreateBitCast(messageRef, ObjCTypes.MessageRefPtrTy),
            ObjCTypes.MessageRefTy, CGF.getPointerAlign());

  // Update the message ref argument.
  args[1].setRValue(RValue::get(mref, CGF));

  // Load the function to call from the message ref table.
  Address calleeAddr = CGF.Builder.CreateStructGEP(mref, 0);
  llvm::Value *calleePtr = CGF.Builder.CreateLoad(calleeAddr, "msgSend_fn");

  calleePtr = CGF.Builder.CreateBitCast(calleePtr, MSI.MessengerType);
  CGCallee callee(CGCalleeInfo(), calleePtr);

  RValue result = CGF.EmitCall(MSI.CallInfo, callee, returnSlot, args);
  return nullReturn.complete(CGF, returnSlot, result, resultType, formalArgs,
                             requiresnullCheck ? method : nullptr);
}

/// Generate code for a message send expression in the nonfragile abi.
CodeGen::RValue
CGObjCNonFragileABIMac::GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                                            ReturnValueSlot Return,
                                            QualType ResultType,
                                            Selector Sel,
                                            llvm::Value *Receiver,
                                            const CallArgList &CallArgs,
                                            const ObjCInterfaceDecl *Class,
                                            const ObjCMethodDecl *Method) {
  return isVTableDispatchedSelector(Sel)
    ? EmitVTableMessageSend(CGF, Return, ResultType, Sel,
                            Receiver, CGF.getContext().getObjCIdType(),
                            false, CallArgs, Method)
    : EmitMessageSend(CGF, Return, ResultType, Sel,
                      Receiver, CGF.getContext().getObjCIdType(),
                      false, CallArgs, Method, Class, ObjCTypes);
}

llvm::Constant *
CGObjCNonFragileABIMac::GetClassGlobal(const ObjCInterfaceDecl *ID,
                                       bool metaclass,
                                       ForDefinition_t isForDefinition) {
  auto prefix =
    (metaclass ? getMetaclassSymbolPrefix() : getClassSymbolPrefix());
  return GetClassGlobal((prefix + ID->getObjCRuntimeNameAsString()).str(),
                        isForDefinition,
                        ID->isWeakImported(),
                        !isForDefinition
                          && CGM.getTriple().isOSBinFormatCOFF()
                          && ID->hasAttr<DLLImportAttr>());
}

llvm::Constant *
CGObjCNonFragileABIMac::GetClassGlobal(StringRef Name,
                                       ForDefinition_t IsForDefinition,
                                       bool Weak, bool DLLImport) {
  llvm::GlobalValue::LinkageTypes L =
      Weak ? llvm::GlobalValue::ExternalWeakLinkage
           : llvm::GlobalValue::ExternalLinkage;

  llvm::GlobalVariable *GV = CGM.getModule().getGlobalVariable(Name);
  if (!GV || GV->getValueType() != ObjCTypes.ClassnfABITy) {
    auto *NewGV = new llvm::GlobalVariable(ObjCTypes.ClassnfABITy, false, L,
                                           nullptr, Name);

    if (DLLImport)
      NewGV->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);

    if (GV) {
      GV->replaceAllUsesWith(NewGV);
      GV->eraseFromParent();
    }
    GV = NewGV;
    CGM.getModule().insertGlobalVariable(GV);
  }

  assert(GV->getLinkage() == L);
  return GV;
}

llvm::Constant *
CGObjCNonFragileABIMac::GetClassGlobalForClassRef(const ObjCInterfaceDecl *ID) {
  llvm::Constant *ClassGV = GetClassGlobal(ID, /*metaclass*/ false,
                                           NotForDefinition);

  if (!ID->hasAttr<ObjCClassStubAttr>())
    return ClassGV;

  ClassGV = llvm::ConstantExpr::getPointerCast(ClassGV, ObjCTypes.Int8PtrTy);

  // Stub classes are pointer-aligned. Classrefs pointing at stub classes
  // must set the least significant bit set to 1.
  auto *Idx = llvm::ConstantInt::get(CGM.Int32Ty, 1);
  return llvm::ConstantExpr::getGetElementPtr(CGM.Int8Ty, ClassGV, Idx);
}

llvm::Value *
CGObjCNonFragileABIMac::EmitLoadOfClassRef(CodeGenFunction &CGF,
                                           const ObjCInterfaceDecl *ID,
                                           llvm::GlobalVariable *Entry) {
  if (ID && ID->hasAttr<ObjCClassStubAttr>()) {
    // Classrefs pointing at Objective-C stub classes must be loaded by calling
    // a special runtime function.
    return CGF.EmitRuntimeCall(
      ObjCTypes.getLoadClassrefFn(), Entry, "load_classref_result");
  }

  CharUnits Align = CGF.getPointerAlign();
  return CGF.Builder.CreateAlignedLoad(Entry->getValueType(), Entry, Align);
}

llvm::Value *
CGObjCNonFragileABIMac::EmitClassRefFromId(CodeGenFunction &CGF,
                                           IdentifierInfo *II,
                                           const ObjCInterfaceDecl *ID) {
  llvm::GlobalVariable *&Entry = ClassReferences[II];

  if (!Entry) {
    llvm::Constant *ClassGV;
    if (ID) {
      ClassGV = GetClassGlobalForClassRef(ID);
    } else {
      ClassGV = GetClassGlobal((getClassSymbolPrefix() + II->getName()).str(),
                               NotForDefinition);
      assert(ClassGV->getType() == ObjCTypes.ClassnfABIPtrTy &&
             "classref was emitted with the wrong type?");
    }

    std::string SectionName =
        GetSectionName("__objc_classrefs", "regular,no_dead_strip");
    Entry = new llvm::GlobalVariable(
        CGM.getModule(), ClassGV->getType(), false,
        getLinkageTypeForObjCMetadata(CGM, SectionName), ClassGV,
        "OBJC_CLASSLIST_REFERENCES_$_");
    Entry->setAlignment(CGF.getPointerAlign().getAsAlign());
    if (!ID || !ID->hasAttr<ObjCClassStubAttr>())
      Entry->setSection(SectionName);

    CGM.addCompilerUsedGlobal(Entry);
  }

  return EmitLoadOfClassRef(CGF, ID, Entry);
}

llvm::Value *CGObjCNonFragileABIMac::EmitClassRef(CodeGenFunction &CGF,
                                                  const ObjCInterfaceDecl *ID) {
  // If the class has the objc_runtime_visible attribute, we need to
  // use the Objective-C runtime to get the class.
  if (ID->hasAttr<ObjCRuntimeVisibleAttr>())
    return EmitClassRefViaRuntime(CGF, ID, ObjCTypes);

  return EmitClassRefFromId(CGF, ID->getIdentifier(), ID);
}

llvm::Value *CGObjCNonFragileABIMac::EmitNSAutoreleasePoolClassRef(
                                                    CodeGenFunction &CGF) {
  IdentifierInfo *II = &CGM.getContext().Idents.get("NSAutoreleasePool");
  return EmitClassRefFromId(CGF, II, nullptr);
}

llvm::Value *
CGObjCNonFragileABIMac::EmitSuperClassRef(CodeGenFunction &CGF,
                                          const ObjCInterfaceDecl *ID) {
  llvm::GlobalVariable *&Entry = SuperClassReferences[ID->getIdentifier()];

  if (!Entry) {
    llvm::Constant *ClassGV = GetClassGlobalForClassRef(ID);
    std::string SectionName =
        GetSectionName("__objc_superrefs", "regular,no_dead_strip");
    Entry = new llvm::GlobalVariable(CGM.getModule(), ClassGV->getType(), false,
                                     llvm::GlobalValue::PrivateLinkage, ClassGV,
                                     "OBJC_CLASSLIST_SUP_REFS_$_");
    Entry->setAlignment(CGF.getPointerAlign().getAsAlign());
    Entry->setSection(SectionName);
    CGM.addCompilerUsedGlobal(Entry);
  }

  return EmitLoadOfClassRef(CGF, ID, Entry);
}

/// EmitMetaClassRef - Return a Value * of the address of _class_t
/// meta-data
///
llvm::Value *CGObjCNonFragileABIMac::EmitMetaClassRef(CodeGenFunction &CGF,
                                                      const ObjCInterfaceDecl *ID,
                                                      bool Weak) {
  CharUnits Align = CGF.getPointerAlign();
  llvm::GlobalVariable * &Entry = MetaClassReferences[ID->getIdentifier()];
  if (!Entry) {
    auto MetaClassGV = GetClassGlobal(ID, /*metaclass*/ true, NotForDefinition);
    std::string SectionName =
        GetSectionName("__objc_superrefs", "regular,no_dead_strip");
    Entry = new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.ClassnfABIPtrTy,
                                     false, llvm::GlobalValue::PrivateLinkage,
                                     MetaClassGV, "OBJC_CLASSLIST_SUP_REFS_$_");
    Entry->setAlignment(Align.getAsAlign());
    Entry->setSection(SectionName);
    CGM.addCompilerUsedGlobal(Entry);
  }

  return CGF.Builder.CreateAlignedLoad(ObjCTypes.ClassnfABIPtrTy, Entry, Align);
}

/// GetClass - Return a reference to the class for the given interface
/// decl.
llvm::Value *CGObjCNonFragileABIMac::GetClass(CodeGenFunction &CGF,
                                              const ObjCInterfaceDecl *ID) {
  if (ID->isWeakImported()) {
    auto ClassGV = GetClassGlobal(ID, /*metaclass*/ false, NotForDefinition);
    (void)ClassGV;
    assert(!isa<llvm::GlobalVariable>(ClassGV) ||
           cast<llvm::GlobalVariable>(ClassGV)->hasExternalWeakLinkage());
  }

  return EmitClassRef(CGF, ID);
}

/// Generates a message send where the super is the receiver.  This is
/// a message send to self with special delivery semantics indicating
/// which class's method should be called.
CodeGen::RValue
CGObjCNonFragileABIMac::GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                                                 ReturnValueSlot Return,
                                                 QualType ResultType,
                                                 Selector Sel,
                                                 const ObjCInterfaceDecl *Class,
                                                 bool isCategoryImpl,
                                                 llvm::Value *Receiver,
                                                 bool IsClassMessage,
                                                 const CodeGen::CallArgList &CallArgs,
                                                 const ObjCMethodDecl *Method) {
  // ...
  // Create and init a super structure; this is a (receiver, class)
  // pair we will pass to objc_msgSendSuper.
  RawAddress ObjCSuper = CGF.CreateTempAlloca(
      ObjCTypes.SuperTy, CGF.getPointerAlign(), "objc_super");

  llvm::Value *ReceiverAsObject =
    CGF.Builder.CreateBitCast(Receiver, ObjCTypes.ObjectPtrTy);
  CGF.Builder.CreateStore(ReceiverAsObject,
                          CGF.Builder.CreateStructGEP(ObjCSuper, 0));

  // If this is a class message the metaclass is passed as the target.
  llvm::Value *Target;
  if (IsClassMessage)
      Target = EmitMetaClassRef(CGF, Class, Class->isWeakImported());
  else
    Target = EmitSuperClassRef(CGF, Class);

  // FIXME: We shouldn't need to do this cast, rectify the ASTContext and
  // ObjCTypes types.
  llvm::Type *ClassTy =
    CGM.getTypes().ConvertType(CGF.getContext().getObjCClassType());
  Target = CGF.Builder.CreateBitCast(Target, ClassTy);
  CGF.Builder.CreateStore(Target, CGF.Builder.CreateStructGEP(ObjCSuper, 1));

  return (isVTableDispatchedSelector(Sel))
    ? EmitVTableMessageSend(CGF, Return, ResultType, Sel,
                            ObjCSuper.getPointer(), ObjCTypes.SuperPtrCTy,
                            true, CallArgs, Method)
    : EmitMessageSend(CGF, Return, ResultType, Sel,
                      ObjCSuper.getPointer(), ObjCTypes.SuperPtrCTy,
                      true, CallArgs, Method, Class, ObjCTypes);
}

llvm::Value *CGObjCNonFragileABIMac::EmitSelector(CodeGenFunction &CGF,
                                                  Selector Sel) {
  Address Addr = EmitSelectorAddr(Sel);

  llvm::LoadInst* LI = CGF.Builder.CreateLoad(Addr);
  LI->setMetadata(llvm::LLVMContext::MD_invariant_load,
                  llvm::MDNode::get(VMContext, std::nullopt));
  return LI;
}

ConstantAddress CGObjCNonFragileABIMac::EmitSelectorAddr(Selector Sel) {
  llvm::GlobalVariable *&Entry = SelectorReferences[Sel];
  CharUnits Align = CGM.getPointerAlign();
  if (!Entry) {
    std::string SectionName =
        GetSectionName("__objc_selrefs", "literal_pointers,no_dead_strip");
    Entry = new llvm::GlobalVariable(
        CGM.getModule(), ObjCTypes.SelectorPtrTy, false,
        getLinkageTypeForObjCMetadata(CGM, SectionName), GetMethodVarName(Sel),
        "OBJC_SELECTOR_REFERENCES_");
    Entry->setExternallyInitialized(true);
    Entry->setSection(SectionName);
    Entry->setAlignment(Align.getAsAlign());
    CGM.addCompilerUsedGlobal(Entry);
  }

  return ConstantAddress(Entry, ObjCTypes.SelectorPtrTy, Align);
}

/// EmitObjCIvarAssign - Code gen for assigning to a __strong object.
/// objc_assign_ivar (id src, id *dst, ptrdiff_t)
///
void CGObjCNonFragileABIMac::EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                                                llvm::Value *src,
                                                Address dst,
                                                llvm::Value *ivarOffset) {
  llvm::Type * SrcTy = src->getType();
  if (!isa<llvm::PointerType>(SrcTy)) {
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
    assert(Size <= 8 && "does not support size > 8");
    src = (Size == 4 ? CGF.Builder.CreateBitCast(src, ObjCTypes.IntTy)
           : CGF.Builder.CreateBitCast(src, ObjCTypes.LongTy));
    src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
  }
  src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
  llvm::Value *dstVal = CGF.Builder.CreateBitCast(dst.emitRawPointer(CGF),
                                                  ObjCTypes.PtrObjectPtrTy);
  llvm::Value *args[] = {src, dstVal, ivarOffset};
  CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignIvarFn(), args);
}

/// EmitObjCStrongCastAssign - Code gen for assigning to a __strong cast object.
/// objc_assign_strongCast (id src, id *dst)
///
void CGObjCNonFragileABIMac::EmitObjCStrongCastAssign(
  CodeGen::CodeGenFunction &CGF,
  llvm::Value *src, Address dst) {
  llvm::Type * SrcTy = src->getType();
  if (!isa<llvm::PointerType>(SrcTy)) {
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
    assert(Size <= 8 && "does not support size > 8");
    src = (Size == 4 ? CGF.Builder.CreateBitCast(src, ObjCTypes.IntTy)
           : CGF.Builder.CreateBitCast(src, ObjCTypes.LongTy));
    src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
  }
  src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
  llvm::Value *dstVal = CGF.Builder.CreateBitCast(dst.emitRawPointer(CGF),
                                                  ObjCTypes.PtrObjectPtrTy);
  llvm::Value *args[] = {src, dstVal};
  CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignStrongCastFn(),
                              args, "weakassign");
}

void CGObjCNonFragileABIMac::EmitGCMemmoveCollectable(
    CodeGen::CodeGenFunction &CGF, Address DestPtr, Address SrcPtr,
    llvm::Value *Size) {
  llvm::Value *args[] = {DestPtr.emitRawPointer(CGF),
                         SrcPtr.emitRawPointer(CGF), Size};
  CGF.EmitNounwindRuntimeCall(ObjCTypes.GcMemmoveCollectableFn(), args);
}

/// EmitObjCWeakRead - Code gen for loading value of a __weak
/// object: objc_read_weak (id *src)
///
llvm::Value * CGObjCNonFragileABIMac::EmitObjCWeakRead(
  CodeGen::CodeGenFunction &CGF,
  Address AddrWeakObj) {
  llvm::Type *DestTy = AddrWeakObj.getElementType();
  llvm::Value *AddrWeakObjVal = CGF.Builder.CreateBitCast(
      AddrWeakObj.emitRawPointer(CGF), ObjCTypes.PtrObjectPtrTy);
  llvm::Value *read_weak =
    CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcReadWeakFn(),
                                AddrWeakObjVal, "weakread");
  read_weak = CGF.Builder.CreateBitCast(read_weak, DestTy);
  return read_weak;
}

/// EmitObjCWeakAssign - Code gen for assigning to a __weak object.
/// objc_assign_weak (id src, id *dst)
///
void CGObjCNonFragileABIMac::EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                                                llvm::Value *src, Address dst) {
  llvm::Type * SrcTy = src->getType();
  if (!isa<llvm::PointerType>(SrcTy)) {
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
    assert(Size <= 8 && "does not support size > 8");
    src = (Size == 4 ? CGF.Builder.CreateBitCast(src, ObjCTypes.IntTy)
           : CGF.Builder.CreateBitCast(src, ObjCTypes.LongTy));
    src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
  }
  src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
  llvm::Value *dstVal = CGF.Builder.CreateBitCast(dst.emitRawPointer(CGF),
                                                  ObjCTypes.PtrObjectPtrTy);
  llvm::Value *args[] = {src, dstVal};
  CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignWeakFn(),
                              args, "weakassign");
}

/// EmitObjCGlobalAssign - Code gen for assigning to a __strong object.
/// objc_assign_global (id src, id *dst)
///
void CGObjCNonFragileABIMac::EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                                          llvm::Value *src, Address dst,
                                          bool threadlocal) {
  llvm::Type * SrcTy = src->getType();
  if (!isa<llvm::PointerType>(SrcTy)) {
    unsigned Size = CGM.getDataLayout().getTypeAllocSize(SrcTy);
    assert(Size <= 8 && "does not support size > 8");
    src = (Size == 4 ? CGF.Builder.CreateBitCast(src, ObjCTypes.IntTy)
           : CGF.Builder.CreateBitCast(src, ObjCTypes.LongTy));
    src = CGF.Builder.CreateIntToPtr(src, ObjCTypes.Int8PtrTy);
  }
  src = CGF.Builder.CreateBitCast(src, ObjCTypes.ObjectPtrTy);
  llvm::Value *dstVal = CGF.Builder.CreateBitCast(dst.emitRawPointer(CGF),
                                                  ObjCTypes.PtrObjectPtrTy);
  llvm::Value *args[] = {src, dstVal};
  if (!threadlocal)
    CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignGlobalFn(),
                                args, "globalassign");
  else
    CGF.EmitNounwindRuntimeCall(ObjCTypes.getGcAssignThreadLocalFn(),
                                args, "threadlocalassign");
}

void
CGObjCNonFragileABIMac::EmitSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                                             const ObjCAtSynchronizedStmt &S) {
  EmitAtSynchronizedStmt(CGF, S, ObjCTypes.getSyncEnterFn(),
                         ObjCTypes.getSyncExitFn());
}

llvm::Constant *
CGObjCNonFragileABIMac::GetEHType(QualType T) {
  // There's a particular fixed type info for 'id'.
  if (T->isObjCIdType() || T->isObjCQualifiedIdType()) {
    auto *IDEHType = CGM.getModule().getGlobalVariable("OBJC_EHTYPE_id");
    if (!IDEHType) {
      IDEHType =
          new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.EHTypeTy, false,
                                   llvm::GlobalValue::ExternalLinkage, nullptr,
                                   "OBJC_EHTYPE_id");
      if (CGM.getTriple().isOSBinFormatCOFF())
        IDEHType->setDLLStorageClass(getStorage(CGM, "OBJC_EHTYPE_id"));
    }
    return IDEHType;
  }

  // All other types should be Objective-C interface pointer types.
  const ObjCObjectPointerType *PT = T->getAs<ObjCObjectPointerType>();
  assert(PT && "Invalid @catch type.");

  const ObjCInterfaceType *IT = PT->getInterfaceType();
  assert(IT && "Invalid @catch type.");

  return GetInterfaceEHType(IT->getDecl(), NotForDefinition);
}

void CGObjCNonFragileABIMac::EmitTryStmt(CodeGen::CodeGenFunction &CGF,
                                         const ObjCAtTryStmt &S) {
  EmitTryCatchStmt(CGF, S, ObjCTypes.getObjCBeginCatchFn(),
                   ObjCTypes.getObjCEndCatchFn(),
                   ObjCTypes.getExceptionRethrowFn());
}

/// EmitThrowStmt - Generate code for a throw statement.
void CGObjCNonFragileABIMac::EmitThrowStmt(CodeGen::CodeGenFunction &CGF,
                                           const ObjCAtThrowStmt &S,
                                           bool ClearInsertionPoint) {
  if (const Expr *ThrowExpr = S.getThrowExpr()) {
    llvm::Value *Exception = CGF.EmitObjCThrowOperand(ThrowExpr);
    Exception = CGF.Builder.CreateBitCast(Exception, ObjCTypes.ObjectPtrTy);
    llvm::CallBase *Call =
        CGF.EmitRuntimeCallOrInvoke(ObjCTypes.getExceptionThrowFn(), Exception);
    Call->setDoesNotReturn();
  } else {
    llvm::CallBase *Call =
        CGF.EmitRuntimeCallOrInvoke(ObjCTypes.getExceptionRethrowFn());
    Call->setDoesNotReturn();
  }

  CGF.Builder.CreateUnreachable();
  if (ClearInsertionPoint)
    CGF.Builder.ClearInsertionPoint();
}

llvm::Constant *
CGObjCNonFragileABIMac::GetInterfaceEHType(const ObjCInterfaceDecl *ID,
                                           ForDefinition_t IsForDefinition) {
  llvm::GlobalVariable * &Entry = EHTypeReferences[ID->getIdentifier()];
  StringRef ClassName = ID->getObjCRuntimeNameAsString();

  // If we don't need a definition, return the entry if found or check
  // if we use an external reference.
  if (!IsForDefinition) {
    if (Entry)
      return Entry;

    // If this type (or a super class) has the __objc_exception__
    // attribute, emit an external reference.
    if (hasObjCExceptionAttribute(CGM.getContext(), ID)) {
      std::string EHTypeName = ("OBJC_EHTYPE_$_" + ClassName).str();
      Entry = new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.EHTypeTy,
                                       false, llvm::GlobalValue::ExternalLinkage,
                                       nullptr, EHTypeName);
      CGM.setGVProperties(Entry, ID);
      return Entry;
    }
  }

  // Otherwise we need to either make a new entry or fill in the initializer.
  assert((!Entry || !Entry->hasInitializer()) && "Duplicate EHType definition");

  std::string VTableName = "objc_ehtype_vtable";
  auto *VTableGV = CGM.getModule().getGlobalVariable(VTableName);
  if (!VTableGV) {
    VTableGV =
        new llvm::GlobalVariable(CGM.getModule(), ObjCTypes.Int8PtrTy, false,
                                 llvm::GlobalValue::ExternalLinkage, nullptr,
                                 VTableName);
    if (CGM.getTriple().isOSBinFormatCOFF())
      VTableGV->setDLLStorageClass(getStorage(CGM, VTableName));
  }

  llvm::Value *VTableIdx = llvm::ConstantInt::get(CGM.Int32Ty, 2);
  ConstantInitBuilder builder(CGM);
  auto values = builder.beginStruct(ObjCTypes.EHTypeTy);
  values.add(
    llvm::ConstantExpr::getInBoundsGetElementPtr(VTableGV->getValueType(),
                                                 VTableGV, VTableIdx));
  values.add(GetClassName(ClassName));
  values.add(GetClassGlobal(ID, /*metaclass*/ false, NotForDefinition));

  llvm::GlobalValue::LinkageTypes L = IsForDefinition
                                          ? llvm::GlobalValue::ExternalLinkage
                                          : llvm::GlobalValue::WeakAnyLinkage;
  if (Entry) {
    values.finishAndSetAsInitializer(Entry);
    Entry->setAlignment(CGM.getPointerAlign().getAsAlign());
  } else {
    Entry = values.finishAndCreateGlobal("OBJC_EHTYPE_$_" + ClassName,
                                         CGM.getPointerAlign(),
                                         /*constant*/ false,
                                         L);
    if (hasObjCExceptionAttribute(CGM.getContext(), ID))
      CGM.setGVProperties(Entry, ID);
  }
  assert(Entry->getLinkage() == L);

  if (!CGM.getTriple().isOSBinFormatCOFF())
    if (ID->getVisibility() == HiddenVisibility)
      Entry->setVisibility(llvm::GlobalValue::HiddenVisibility);

  if (IsForDefinition)
    if (CGM.getTriple().isOSBinFormatMachO())
      Entry->setSection("__DATA,__objc_const");

  return Entry;
}

/* *** */

CodeGen::CGObjCRuntime *
CodeGen::CreateMacObjCRuntime(CodeGen::CodeGenModule &CGM) {
  switch (CGM.getLangOpts().ObjCRuntime.getKind()) {
  case ObjCRuntime::FragileMacOSX:
  return new CGObjCMac(CGM);

  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    return new CGObjCNonFragileABIMac(CGM);

  case ObjCRuntime::GNUstep:
  case ObjCRuntime::GCC:
  case ObjCRuntime::ObjFW:
    llvm_unreachable("these runtimes are not Mac runtimes");
  }
  llvm_unreachable("bad runtime");
}
