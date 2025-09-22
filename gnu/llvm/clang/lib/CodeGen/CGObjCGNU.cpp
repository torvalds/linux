//===------- CGObjCGNU.cpp - Emit LLVM Code from ASTs for a Module --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides Objective-C code generation targeting the GNU runtime.  The
// class in this file generates structures used by the GNU Objective-C runtime
// library.  These structures are defined in objc/objc.h and objc/objc-api.h in
// the GNU runtime distribution.
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGCleanup.h"
#include "CGObjCRuntime.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "CodeGenTypes.h"
#include "SanitizerMetadata.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include <cctype>

using namespace clang;
using namespace CodeGen;

namespace {

/// Class that lazily initialises the runtime function.  Avoids inserting the
/// types and the function declaration into a module if they're not used, and
/// avoids constructing the type more than once if it's used more than once.
class LazyRuntimeFunction {
  CodeGenModule *CGM = nullptr;
  llvm::FunctionType *FTy = nullptr;
  const char *FunctionName = nullptr;
  llvm::FunctionCallee Function = nullptr;

public:
  LazyRuntimeFunction() = default;

  /// Initialises the lazy function with the name, return type, and the types
  /// of the arguments.
  template <typename... Tys>
  void init(CodeGenModule *Mod, const char *name, llvm::Type *RetTy,
            Tys *... Types) {
    CGM = Mod;
    FunctionName = name;
    Function = nullptr;
    if(sizeof...(Tys)) {
      SmallVector<llvm::Type *, 8> ArgTys({Types...});
      FTy = llvm::FunctionType::get(RetTy, ArgTys, false);
    }
    else {
      FTy = llvm::FunctionType::get(RetTy, std::nullopt, false);
    }
  }

  llvm::FunctionType *getType() { return FTy; }

  /// Overloaded cast operator, allows the class to be implicitly cast to an
  /// LLVM constant.
  operator llvm::FunctionCallee() {
    if (!Function) {
      if (!FunctionName)
        return nullptr;
      Function = CGM->CreateRuntimeFunction(FTy, FunctionName);
    }
    return Function;
  }
};


/// GNU Objective-C runtime code generation.  This class implements the parts of
/// Objective-C support that are specific to the GNU family of runtimes (GCC,
/// GNUstep and ObjFW).
class CGObjCGNU : public CGObjCRuntime {
protected:
  /// The LLVM module into which output is inserted
  llvm::Module &TheModule;
  /// strut objc_super.  Used for sending messages to super.  This structure
  /// contains the receiver (object) and the expected class.
  llvm::StructType *ObjCSuperTy;
  /// struct objc_super*.  The type of the argument to the superclass message
  /// lookup functions.
  llvm::PointerType *PtrToObjCSuperTy;
  /// LLVM type for selectors.  Opaque pointer (i8*) unless a header declaring
  /// SEL is included in a header somewhere, in which case it will be whatever
  /// type is declared in that header, most likely {i8*, i8*}.
  llvm::PointerType *SelectorTy;
  /// Element type of SelectorTy.
  llvm::Type *SelectorElemTy;
  /// LLVM i8 type.  Cached here to avoid repeatedly getting it in all of the
  /// places where it's used
  llvm::IntegerType *Int8Ty;
  /// Pointer to i8 - LLVM type of char*, for all of the places where the
  /// runtime needs to deal with C strings.
  llvm::PointerType *PtrToInt8Ty;
  /// struct objc_protocol type
  llvm::StructType *ProtocolTy;
  /// Protocol * type.
  llvm::PointerType *ProtocolPtrTy;
  /// Instance Method Pointer type.  This is a pointer to a function that takes,
  /// at a minimum, an object and a selector, and is the generic type for
  /// Objective-C methods.  Due to differences between variadic / non-variadic
  /// calling conventions, it must always be cast to the correct type before
  /// actually being used.
  llvm::PointerType *IMPTy;
  /// Type of an untyped Objective-C object.  Clang treats id as a built-in type
  /// when compiling Objective-C code, so this may be an opaque pointer (i8*),
  /// but if the runtime header declaring it is included then it may be a
  /// pointer to a structure.
  llvm::PointerType *IdTy;
  /// Element type of IdTy.
  llvm::Type *IdElemTy;
  /// Pointer to a pointer to an Objective-C object.  Used in the new ABI
  /// message lookup function and some GC-related functions.
  llvm::PointerType *PtrToIdTy;
  /// The clang type of id.  Used when using the clang CGCall infrastructure to
  /// call Objective-C methods.
  CanQualType ASTIdTy;
  /// LLVM type for C int type.
  llvm::IntegerType *IntTy;
  /// LLVM type for an opaque pointer.  This is identical to PtrToInt8Ty, but is
  /// used in the code to document the difference between i8* meaning a pointer
  /// to a C string and i8* meaning a pointer to some opaque type.
  llvm::PointerType *PtrTy;
  /// LLVM type for C long type.  The runtime uses this in a lot of places where
  /// it should be using intptr_t, but we can't fix this without breaking
  /// compatibility with GCC...
  llvm::IntegerType *LongTy;
  /// LLVM type for C size_t.  Used in various runtime data structures.
  llvm::IntegerType *SizeTy;
  /// LLVM type for C intptr_t.
  llvm::IntegerType *IntPtrTy;
  /// LLVM type for C ptrdiff_t.  Mainly used in property accessor functions.
  llvm::IntegerType *PtrDiffTy;
  /// LLVM type for C int*.  Used for GCC-ABI-compatible non-fragile instance
  /// variables.
  llvm::PointerType *PtrToIntTy;
  /// LLVM type for Objective-C BOOL type.
  llvm::Type *BoolTy;
  /// 32-bit integer type, to save us needing to look it up every time it's used.
  llvm::IntegerType *Int32Ty;
  /// 64-bit integer type, to save us needing to look it up every time it's used.
  llvm::IntegerType *Int64Ty;
  /// The type of struct objc_property.
  llvm::StructType *PropertyMetadataTy;
  /// Metadata kind used to tie method lookups to message sends.  The GNUstep
  /// runtime provides some LLVM passes that can use this to do things like
  /// automatic IMP caching and speculative inlining.
  unsigned msgSendMDKind;
  /// Does the current target use SEH-based exceptions? False implies
  /// Itanium-style DWARF unwinding.
  bool usesSEHExceptions;
  /// Does the current target uses C++-based exceptions?
  bool usesCxxExceptions;

  /// Helper to check if we are targeting a specific runtime version or later.
  bool isRuntime(ObjCRuntime::Kind kind, unsigned major, unsigned minor=0) {
    const ObjCRuntime &R = CGM.getLangOpts().ObjCRuntime;
    return (R.getKind() == kind) &&
      (R.getVersion() >= VersionTuple(major, minor));
  }

  std::string ManglePublicSymbol(StringRef Name) {
    return (StringRef(CGM.getTriple().isOSBinFormatCOFF() ? "$_" : "._") + Name).str();
  }

  std::string SymbolForProtocol(Twine Name) {
    return (ManglePublicSymbol("OBJC_PROTOCOL_") + Name).str();
  }

  std::string SymbolForProtocolRef(StringRef Name) {
    return (ManglePublicSymbol("OBJC_REF_PROTOCOL_") + Name).str();
  }


  /// Helper function that generates a constant string and returns a pointer to
  /// the start of the string.  The result of this function can be used anywhere
  /// where the C code specifies const char*.
  llvm::Constant *MakeConstantString(StringRef Str, const char *Name = "") {
    ConstantAddress Array =
        CGM.GetAddrOfConstantCString(std::string(Str), Name);
    return Array.getPointer();
  }

  /// Emits a linkonce_odr string, whose name is the prefix followed by the
  /// string value.  This allows the linker to combine the strings between
  /// different modules.  Used for EH typeinfo names, selector strings, and a
  /// few other things.
  llvm::Constant *ExportUniqueString(const std::string &Str,
                                     const std::string &prefix,
                                     bool Private=false) {
    std::string name = prefix + Str;
    auto *ConstStr = TheModule.getGlobalVariable(name);
    if (!ConstStr) {
      llvm::Constant *value = llvm::ConstantDataArray::getString(VMContext,Str);
      auto *GV = new llvm::GlobalVariable(TheModule, value->getType(), true,
              llvm::GlobalValue::LinkOnceODRLinkage, value, name);
      GV->setComdat(TheModule.getOrInsertComdat(name));
      if (Private)
        GV->setVisibility(llvm::GlobalValue::HiddenVisibility);
      ConstStr = GV;
    }
    return ConstStr;
  }

  /// Returns a property name and encoding string.
  llvm::Constant *MakePropertyEncodingString(const ObjCPropertyDecl *PD,
                                             const Decl *Container) {
    assert(!isRuntime(ObjCRuntime::GNUstep, 2));
    if (isRuntime(ObjCRuntime::GNUstep, 1, 6)) {
      std::string NameAndAttributes;
      std::string TypeStr =
        CGM.getContext().getObjCEncodingForPropertyDecl(PD, Container);
      NameAndAttributes += '\0';
      NameAndAttributes += TypeStr.length() + 3;
      NameAndAttributes += TypeStr;
      NameAndAttributes += '\0';
      NameAndAttributes += PD->getNameAsString();
      return MakeConstantString(NameAndAttributes);
    }
    return MakeConstantString(PD->getNameAsString());
  }

  /// Push the property attributes into two structure fields.
  void PushPropertyAttributes(ConstantStructBuilder &Fields,
      const ObjCPropertyDecl *property, bool isSynthesized=true, bool
      isDynamic=true) {
    int attrs = property->getPropertyAttributes();
    // For read-only properties, clear the copy and retain flags
    if (attrs & ObjCPropertyAttribute::kind_readonly) {
      attrs &= ~ObjCPropertyAttribute::kind_copy;
      attrs &= ~ObjCPropertyAttribute::kind_retain;
      attrs &= ~ObjCPropertyAttribute::kind_weak;
      attrs &= ~ObjCPropertyAttribute::kind_strong;
    }
    // The first flags field has the same attribute values as clang uses internally
    Fields.addInt(Int8Ty, attrs & 0xff);
    attrs >>= 8;
    attrs <<= 2;
    // For protocol properties, synthesized and dynamic have no meaning, so we
    // reuse these flags to indicate that this is a protocol property (both set
    // has no meaning, as a property can't be both synthesized and dynamic)
    attrs |= isSynthesized ? (1<<0) : 0;
    attrs |= isDynamic ? (1<<1) : 0;
    // The second field is the next four fields left shifted by two, with the
    // low bit set to indicate whether the field is synthesized or dynamic.
    Fields.addInt(Int8Ty, attrs & 0xff);
    // Two padding fields
    Fields.addInt(Int8Ty, 0);
    Fields.addInt(Int8Ty, 0);
  }

  virtual llvm::Constant *GenerateCategoryProtocolList(const
      ObjCCategoryDecl *OCD);
  virtual ConstantArrayBuilder PushPropertyListHeader(ConstantStructBuilder &Fields,
      int count) {
      // int count;
      Fields.addInt(IntTy, count);
      // int size; (only in GNUstep v2 ABI.
      if (isRuntime(ObjCRuntime::GNUstep, 2)) {
        llvm::DataLayout td(&TheModule);
        Fields.addInt(IntTy, td.getTypeSizeInBits(PropertyMetadataTy) /
            CGM.getContext().getCharWidth());
      }
      // struct objc_property_list *next;
      Fields.add(NULLPtr);
      // struct objc_property properties[]
      return Fields.beginArray(PropertyMetadataTy);
  }
  virtual void PushProperty(ConstantArrayBuilder &PropertiesArray,
            const ObjCPropertyDecl *property,
            const Decl *OCD,
            bool isSynthesized=true, bool
            isDynamic=true) {
    auto Fields = PropertiesArray.beginStruct(PropertyMetadataTy);
    ASTContext &Context = CGM.getContext();
    Fields.add(MakePropertyEncodingString(property, OCD));
    PushPropertyAttributes(Fields, property, isSynthesized, isDynamic);
    auto addPropertyMethod = [&](const ObjCMethodDecl *accessor) {
      if (accessor) {
        std::string TypeStr = Context.getObjCEncodingForMethodDecl(accessor);
        llvm::Constant *TypeEncoding = MakeConstantString(TypeStr);
        Fields.add(MakeConstantString(accessor->getSelector().getAsString()));
        Fields.add(TypeEncoding);
      } else {
        Fields.add(NULLPtr);
        Fields.add(NULLPtr);
      }
    };
    addPropertyMethod(property->getGetterMethodDecl());
    addPropertyMethod(property->getSetterMethodDecl());
    Fields.finishAndAddTo(PropertiesArray);
  }

  /// Ensures that the value has the required type, by inserting a bitcast if
  /// required.  This function lets us avoid inserting bitcasts that are
  /// redundant.
  llvm::Value *EnforceType(CGBuilderTy &B, llvm::Value *V, llvm::Type *Ty) {
    if (V->getType() == Ty)
      return V;
    return B.CreateBitCast(V, Ty);
  }

  // Some zeros used for GEPs in lots of places.
  llvm::Constant *Zeros[2];
  /// Null pointer value.  Mainly used as a terminator in various arrays.
  llvm::Constant *NULLPtr;
  /// LLVM context.
  llvm::LLVMContext &VMContext;

protected:

  /// Placeholder for the class.  Lots of things refer to the class before we've
  /// actually emitted it.  We use this alias as a placeholder, and then replace
  /// it with a pointer to the class structure before finally emitting the
  /// module.
  llvm::GlobalAlias *ClassPtrAlias;
  /// Placeholder for the metaclass.  Lots of things refer to the class before
  /// we've / actually emitted it.  We use this alias as a placeholder, and then
  /// replace / it with a pointer to the metaclass structure before finally
  /// emitting the / module.
  llvm::GlobalAlias *MetaClassPtrAlias;
  /// All of the classes that have been generated for this compilation units.
  std::vector<llvm::Constant*> Classes;
  /// All of the categories that have been generated for this compilation units.
  std::vector<llvm::Constant*> Categories;
  /// All of the Objective-C constant strings that have been generated for this
  /// compilation units.
  std::vector<llvm::Constant*> ConstantStrings;
  /// Map from string values to Objective-C constant strings in the output.
  /// Used to prevent emitting Objective-C strings more than once.  This should
  /// not be required at all - CodeGenModule should manage this list.
  llvm::StringMap<llvm::Constant*> ObjCStrings;
  /// All of the protocols that have been declared.
  llvm::StringMap<llvm::Constant*> ExistingProtocols;
  /// For each variant of a selector, we store the type encoding and a
  /// placeholder value.  For an untyped selector, the type will be the empty
  /// string.  Selector references are all done via the module's selector table,
  /// so we create an alias as a placeholder and then replace it with the real
  /// value later.
  typedef std::pair<std::string, llvm::GlobalAlias*> TypedSelector;
  /// Type of the selector map.  This is roughly equivalent to the structure
  /// used in the GNUstep runtime, which maintains a list of all of the valid
  /// types for a selector in a table.
  typedef llvm::DenseMap<Selector, SmallVector<TypedSelector, 2> >
    SelectorMap;
  /// A map from selectors to selector types.  This allows us to emit all
  /// selectors of the same name and type together.
  SelectorMap SelectorTable;

  /// Selectors related to memory management.  When compiling in GC mode, we
  /// omit these.
  Selector RetainSel, ReleaseSel, AutoreleaseSel;
  /// Runtime functions used for memory management in GC mode.  Note that clang
  /// supports code generation for calling these functions, but neither GNU
  /// runtime actually supports this API properly yet.
  LazyRuntimeFunction IvarAssignFn, StrongCastAssignFn, MemMoveFn, WeakReadFn,
    WeakAssignFn, GlobalAssignFn;

  typedef std::pair<std::string, std::string> ClassAliasPair;
  /// All classes that have aliases set for them.
  std::vector<ClassAliasPair> ClassAliases;

protected:
  /// Function used for throwing Objective-C exceptions.
  LazyRuntimeFunction ExceptionThrowFn;
  /// Function used for rethrowing exceptions, used at the end of \@finally or
  /// \@synchronize blocks.
  LazyRuntimeFunction ExceptionReThrowFn;
  /// Function called when entering a catch function.  This is required for
  /// differentiating Objective-C exceptions and foreign exceptions.
  LazyRuntimeFunction EnterCatchFn;
  /// Function called when exiting from a catch block.  Used to do exception
  /// cleanup.
  LazyRuntimeFunction ExitCatchFn;
  /// Function called when entering an \@synchronize block.  Acquires the lock.
  LazyRuntimeFunction SyncEnterFn;
  /// Function called when exiting an \@synchronize block.  Releases the lock.
  LazyRuntimeFunction SyncExitFn;

private:
  /// Function called if fast enumeration detects that the collection is
  /// modified during the update.
  LazyRuntimeFunction EnumerationMutationFn;
  /// Function for implementing synthesized property getters that return an
  /// object.
  LazyRuntimeFunction GetPropertyFn;
  /// Function for implementing synthesized property setters that return an
  /// object.
  LazyRuntimeFunction SetPropertyFn;
  /// Function used for non-object declared property getters.
  LazyRuntimeFunction GetStructPropertyFn;
  /// Function used for non-object declared property setters.
  LazyRuntimeFunction SetStructPropertyFn;

protected:
  /// The version of the runtime that this class targets.  Must match the
  /// version in the runtime.
  int RuntimeVersion;
  /// The version of the protocol class.  Used to differentiate between ObjC1
  /// and ObjC2 protocols.  Objective-C 1 protocols can not contain optional
  /// components and can not contain declared properties.  We always emit
  /// Objective-C 2 property structures, but we have to pretend that they're
  /// Objective-C 1 property structures when targeting the GCC runtime or it
  /// will abort.
  const int ProtocolVersion;
  /// The version of the class ABI.  This value is used in the class structure
  /// and indicates how various fields should be interpreted.
  const int ClassABIVersion;
  /// Generates an instance variable list structure.  This is a structure
  /// containing a size and an array of structures containing instance variable
  /// metadata.  This is used purely for introspection in the fragile ABI.  In
  /// the non-fragile ABI, it's used for instance variable fixup.
  virtual llvm::Constant *GenerateIvarList(ArrayRef<llvm::Constant *> IvarNames,
                             ArrayRef<llvm::Constant *> IvarTypes,
                             ArrayRef<llvm::Constant *> IvarOffsets,
                             ArrayRef<llvm::Constant *> IvarAlign,
                             ArrayRef<Qualifiers::ObjCLifetime> IvarOwnership);

  /// Generates a method list structure.  This is a structure containing a size
  /// and an array of structures containing method metadata.
  ///
  /// This structure is used by both classes and categories, and contains a next
  /// pointer allowing them to be chained together in a linked list.
  llvm::Constant *GenerateMethodList(StringRef ClassName,
      StringRef CategoryName,
      ArrayRef<const ObjCMethodDecl*> Methods,
      bool isClassMethodList);

  /// Emits an empty protocol.  This is used for \@protocol() where no protocol
  /// is found.  The runtime will (hopefully) fix up the pointer to refer to the
  /// real protocol.
  virtual llvm::Constant *GenerateEmptyProtocol(StringRef ProtocolName);

  /// Generates a list of property metadata structures.  This follows the same
  /// pattern as method and instance variable metadata lists.
  llvm::Constant *GeneratePropertyList(const Decl *Container,
      const ObjCContainerDecl *OCD,
      bool isClassProperty=false,
      bool protocolOptionalProperties=false);

  /// Generates a list of referenced protocols.  Classes, categories, and
  /// protocols all use this structure.
  llvm::Constant *GenerateProtocolList(ArrayRef<std::string> Protocols);

  /// To ensure that all protocols are seen by the runtime, we add a category on
  /// a class defined in the runtime, declaring no methods, but adopting the
  /// protocols.  This is a horribly ugly hack, but it allows us to collect all
  /// of the protocols without changing the ABI.
  void GenerateProtocolHolderCategory();

  /// Generates a class structure.
  llvm::Constant *GenerateClassStructure(
      llvm::Constant *MetaClass,
      llvm::Constant *SuperClass,
      unsigned info,
      const char *Name,
      llvm::Constant *Version,
      llvm::Constant *InstanceSize,
      llvm::Constant *IVars,
      llvm::Constant *Methods,
      llvm::Constant *Protocols,
      llvm::Constant *IvarOffsets,
      llvm::Constant *Properties,
      llvm::Constant *StrongIvarBitmap,
      llvm::Constant *WeakIvarBitmap,
      bool isMeta=false);

  /// Generates a method list.  This is used by protocols to define the required
  /// and optional methods.
  virtual llvm::Constant *GenerateProtocolMethodList(
      ArrayRef<const ObjCMethodDecl*> Methods);
  /// Emits optional and required method lists.
  template<class T>
  void EmitProtocolMethodList(T &&Methods, llvm::Constant *&Required,
      llvm::Constant *&Optional) {
    SmallVector<const ObjCMethodDecl*, 16> RequiredMethods;
    SmallVector<const ObjCMethodDecl*, 16> OptionalMethods;
    for (const auto *I : Methods)
      if (I->isOptional())
        OptionalMethods.push_back(I);
      else
        RequiredMethods.push_back(I);
    Required = GenerateProtocolMethodList(RequiredMethods);
    Optional = GenerateProtocolMethodList(OptionalMethods);
  }

  /// Returns a selector with the specified type encoding.  An empty string is
  /// used to return an untyped selector (with the types field set to NULL).
  virtual llvm::Value *GetTypedSelector(CodeGenFunction &CGF, Selector Sel,
                                        const std::string &TypeEncoding);

  /// Returns the name of ivar offset variables.  In the GNUstep v1 ABI, this
  /// contains the class and ivar names, in the v2 ABI this contains the type
  /// encoding as well.
  virtual std::string GetIVarOffsetVariableName(const ObjCInterfaceDecl *ID,
                                                const ObjCIvarDecl *Ivar) {
    const std::string Name = "__objc_ivar_offset_" + ID->getNameAsString()
      + '.' + Ivar->getNameAsString();
    return Name;
  }
  /// Returns the variable used to store the offset of an instance variable.
  llvm::GlobalVariable *ObjCIvarOffsetVariable(const ObjCInterfaceDecl *ID,
      const ObjCIvarDecl *Ivar);
  /// Emits a reference to a class.  This allows the linker to object if there
  /// is no class of the matching name.
  void EmitClassRef(const std::string &className);

  /// Emits a pointer to the named class
  virtual llvm::Value *GetClassNamed(CodeGenFunction &CGF,
                                     const std::string &Name, bool isWeak);

  /// Looks up the method for sending a message to the specified object.  This
  /// mechanism differs between the GCC and GNU runtimes, so this method must be
  /// overridden in subclasses.
  virtual llvm::Value *LookupIMP(CodeGenFunction &CGF,
                                 llvm::Value *&Receiver,
                                 llvm::Value *cmd,
                                 llvm::MDNode *node,
                                 MessageSendInfo &MSI) = 0;

  /// Looks up the method for sending a message to a superclass.  This
  /// mechanism differs between the GCC and GNU runtimes, so this method must
  /// be overridden in subclasses.
  virtual llvm::Value *LookupIMPSuper(CodeGenFunction &CGF,
                                      Address ObjCSuper,
                                      llvm::Value *cmd,
                                      MessageSendInfo &MSI) = 0;

  /// Libobjc2 uses a bitfield representation where small(ish) bitfields are
  /// stored in a 64-bit value with the low bit set to 1 and the remaining 63
  /// bits set to their values, LSB first, while larger ones are stored in a
  /// structure of this / form:
  ///
  /// struct { int32_t length; int32_t values[length]; };
  ///
  /// The values in the array are stored in host-endian format, with the least
  /// significant bit being assumed to come first in the bitfield.  Therefore,
  /// a bitfield with the 64th bit set will be (int64_t)&{ 2, [0, 1<<31] },
  /// while a bitfield / with the 63rd bit set will be 1<<64.
  llvm::Constant *MakeBitField(ArrayRef<bool> bits);

public:
  CGObjCGNU(CodeGenModule &cgm, unsigned runtimeABIVersion,
      unsigned protocolClassVersion, unsigned classABI=1);

  ConstantAddress GenerateConstantString(const StringLiteral *) override;

  RValue
  GenerateMessageSend(CodeGenFunction &CGF, ReturnValueSlot Return,
                      QualType ResultType, Selector Sel,
                      llvm::Value *Receiver, const CallArgList &CallArgs,
                      const ObjCInterfaceDecl *Class,
                      const ObjCMethodDecl *Method) override;
  RValue
  GenerateMessageSendSuper(CodeGenFunction &CGF, ReturnValueSlot Return,
                           QualType ResultType, Selector Sel,
                           const ObjCInterfaceDecl *Class,
                           bool isCategoryImpl, llvm::Value *Receiver,
                           bool IsClassMessage, const CallArgList &CallArgs,
                           const ObjCMethodDecl *Method) override;
  llvm::Value *GetClass(CodeGenFunction &CGF,
                        const ObjCInterfaceDecl *OID) override;
  llvm::Value *GetSelector(CodeGenFunction &CGF, Selector Sel) override;
  Address GetAddrOfSelector(CodeGenFunction &CGF, Selector Sel) override;
  llvm::Value *GetSelector(CodeGenFunction &CGF,
                           const ObjCMethodDecl *Method) override;
  virtual llvm::Constant *GetConstantSelector(Selector Sel,
                                              const std::string &TypeEncoding) {
    llvm_unreachable("Runtime unable to generate constant selector");
  }
  llvm::Constant *GetConstantSelector(const ObjCMethodDecl *M) {
    return GetConstantSelector(M->getSelector(),
        CGM.getContext().getObjCEncodingForMethodDecl(M));
  }
  llvm::Constant *GetEHType(QualType T) override;

  llvm::Function *GenerateMethod(const ObjCMethodDecl *OMD,
                                 const ObjCContainerDecl *CD) override;

  // Map to unify direct method definitions.
  llvm::DenseMap<const ObjCMethodDecl *, llvm::Function *>
      DirectMethodDefinitions;
  void GenerateDirectMethodPrologue(CodeGenFunction &CGF, llvm::Function *Fn,
                                    const ObjCMethodDecl *OMD,
                                    const ObjCContainerDecl *CD) override;
  void GenerateCategory(const ObjCCategoryImplDecl *CMD) override;
  void GenerateClass(const ObjCImplementationDecl *ClassDecl) override;
  void RegisterAlias(const ObjCCompatibleAliasDecl *OAD) override;
  llvm::Value *GenerateProtocolRef(CodeGenFunction &CGF,
                                   const ObjCProtocolDecl *PD) override;
  void GenerateProtocol(const ObjCProtocolDecl *PD) override;

  virtual llvm::Constant *GenerateProtocolRef(const ObjCProtocolDecl *PD);

  llvm::Constant *GetOrEmitProtocol(const ObjCProtocolDecl *PD) override {
    return GenerateProtocolRef(PD);
  }

  llvm::Function *ModuleInitFunction() override;
  llvm::FunctionCallee GetPropertyGetFunction() override;
  llvm::FunctionCallee GetPropertySetFunction() override;
  llvm::FunctionCallee GetOptimizedPropertySetFunction(bool atomic,
                                                       bool copy) override;
  llvm::FunctionCallee GetSetStructFunction() override;
  llvm::FunctionCallee GetGetStructFunction() override;
  llvm::FunctionCallee GetCppAtomicObjectGetFunction() override;
  llvm::FunctionCallee GetCppAtomicObjectSetFunction() override;
  llvm::FunctionCallee EnumerationMutationFunction() override;

  void EmitTryStmt(CodeGenFunction &CGF,
                   const ObjCAtTryStmt &S) override;
  void EmitSynchronizedStmt(CodeGenFunction &CGF,
                            const ObjCAtSynchronizedStmt &S) override;
  void EmitThrowStmt(CodeGenFunction &CGF,
                     const ObjCAtThrowStmt &S,
                     bool ClearInsertionPoint=true) override;
  llvm::Value * EmitObjCWeakRead(CodeGenFunction &CGF,
                                 Address AddrWeakObj) override;
  void EmitObjCWeakAssign(CodeGenFunction &CGF,
                          llvm::Value *src, Address dst) override;
  void EmitObjCGlobalAssign(CodeGenFunction &CGF,
                            llvm::Value *src, Address dest,
                            bool threadlocal=false) override;
  void EmitObjCIvarAssign(CodeGenFunction &CGF, llvm::Value *src,
                          Address dest, llvm::Value *ivarOffset) override;
  void EmitObjCStrongCastAssign(CodeGenFunction &CGF,
                                llvm::Value *src, Address dest) override;
  void EmitGCMemmoveCollectable(CodeGenFunction &CGF, Address DestPtr,
                                Address SrcPtr,
                                llvm::Value *Size) override;
  LValue EmitObjCValueForIvar(CodeGenFunction &CGF, QualType ObjectTy,
                              llvm::Value *BaseValue, const ObjCIvarDecl *Ivar,
                              unsigned CVRQualifiers) override;
  llvm::Value *EmitIvarOffset(CodeGenFunction &CGF,
                              const ObjCInterfaceDecl *Interface,
                              const ObjCIvarDecl *Ivar) override;
  llvm::Value *EmitNSAutoreleasePoolClassRef(CodeGenFunction &CGF) override;
  llvm::Constant *BuildGCBlockLayout(CodeGenModule &CGM,
                                     const CGBlockInfo &blockInfo) override {
    return NULLPtr;
  }
  llvm::Constant *BuildRCBlockLayout(CodeGenModule &CGM,
                                     const CGBlockInfo &blockInfo) override {
    return NULLPtr;
  }

  llvm::Constant *BuildByrefLayout(CodeGenModule &CGM, QualType T) override {
    return NULLPtr;
  }
};

/// Class representing the legacy GCC Objective-C ABI.  This is the default when
/// -fobjc-nonfragile-abi is not specified.
///
/// The GCC ABI target actually generates code that is approximately compatible
/// with the new GNUstep runtime ABI, but refrains from using any features that
/// would not work with the GCC runtime.  For example, clang always generates
/// the extended form of the class structure, and the extra fields are simply
/// ignored by GCC libobjc.
class CGObjCGCC : public CGObjCGNU {
  /// The GCC ABI message lookup function.  Returns an IMP pointing to the
  /// method implementation for this message.
  LazyRuntimeFunction MsgLookupFn;
  /// The GCC ABI superclass message lookup function.  Takes a pointer to a
  /// structure describing the receiver and the class, and a selector as
  /// arguments.  Returns the IMP for the corresponding method.
  LazyRuntimeFunction MsgLookupSuperFn;

protected:
  llvm::Value *LookupIMP(CodeGenFunction &CGF, llvm::Value *&Receiver,
                         llvm::Value *cmd, llvm::MDNode *node,
                         MessageSendInfo &MSI) override {
    CGBuilderTy &Builder = CGF.Builder;
    llvm::Value *args[] = {
            EnforceType(Builder, Receiver, IdTy),
            EnforceType(Builder, cmd, SelectorTy) };
    llvm::CallBase *imp = CGF.EmitRuntimeCallOrInvoke(MsgLookupFn, args);
    imp->setMetadata(msgSendMDKind, node);
    return imp;
  }

  llvm::Value *LookupIMPSuper(CodeGenFunction &CGF, Address ObjCSuper,
                              llvm::Value *cmd, MessageSendInfo &MSI) override {
    CGBuilderTy &Builder = CGF.Builder;
    llvm::Value *lookupArgs[] = {
        EnforceType(Builder, ObjCSuper.emitRawPointer(CGF), PtrToObjCSuperTy),
        cmd};
    return CGF.EmitNounwindRuntimeCall(MsgLookupSuperFn, lookupArgs);
  }

public:
  CGObjCGCC(CodeGenModule &Mod) : CGObjCGNU(Mod, 8, 2) {
    // IMP objc_msg_lookup(id, SEL);
    MsgLookupFn.init(&CGM, "objc_msg_lookup", IMPTy, IdTy, SelectorTy);
    // IMP objc_msg_lookup_super(struct objc_super*, SEL);
    MsgLookupSuperFn.init(&CGM, "objc_msg_lookup_super", IMPTy,
                          PtrToObjCSuperTy, SelectorTy);
  }
};

/// Class used when targeting the new GNUstep runtime ABI.
class CGObjCGNUstep : public CGObjCGNU {
    /// The slot lookup function.  Returns a pointer to a cacheable structure
    /// that contains (among other things) the IMP.
    LazyRuntimeFunction SlotLookupFn;
    /// The GNUstep ABI superclass message lookup function.  Takes a pointer to
    /// a structure describing the receiver and the class, and a selector as
    /// arguments.  Returns the slot for the corresponding method.  Superclass
    /// message lookup rarely changes, so this is a good caching opportunity.
    LazyRuntimeFunction SlotLookupSuperFn;
    /// Specialised function for setting atomic retain properties
    LazyRuntimeFunction SetPropertyAtomic;
    /// Specialised function for setting atomic copy properties
    LazyRuntimeFunction SetPropertyAtomicCopy;
    /// Specialised function for setting nonatomic retain properties
    LazyRuntimeFunction SetPropertyNonAtomic;
    /// Specialised function for setting nonatomic copy properties
    LazyRuntimeFunction SetPropertyNonAtomicCopy;
    /// Function to perform atomic copies of C++ objects with nontrivial copy
    /// constructors from Objective-C ivars.
    LazyRuntimeFunction CxxAtomicObjectGetFn;
    /// Function to perform atomic copies of C++ objects with nontrivial copy
    /// constructors to Objective-C ivars.
    LazyRuntimeFunction CxxAtomicObjectSetFn;
    /// Type of a slot structure pointer.  This is returned by the various
    /// lookup functions.
    llvm::Type *SlotTy;
    /// Type of a slot structure.
    llvm::Type *SlotStructTy;

  public:
    llvm::Constant *GetEHType(QualType T) override;

  protected:
    llvm::Value *LookupIMP(CodeGenFunction &CGF, llvm::Value *&Receiver,
                           llvm::Value *cmd, llvm::MDNode *node,
                           MessageSendInfo &MSI) override {
      CGBuilderTy &Builder = CGF.Builder;
      llvm::FunctionCallee LookupFn = SlotLookupFn;

      // Store the receiver on the stack so that we can reload it later
      RawAddress ReceiverPtr =
          CGF.CreateTempAlloca(Receiver->getType(), CGF.getPointerAlign());
      Builder.CreateStore(Receiver, ReceiverPtr);

      llvm::Value *self;

      if (isa<ObjCMethodDecl>(CGF.CurCodeDecl)) {
        self = CGF.LoadObjCSelf();
      } else {
        self = llvm::ConstantPointerNull::get(IdTy);
      }

      // The lookup function is guaranteed not to capture the receiver pointer.
      if (auto *LookupFn2 = dyn_cast<llvm::Function>(LookupFn.getCallee()))
        LookupFn2->addParamAttr(0, llvm::Attribute::NoCapture);

      llvm::Value *args[] = {
          EnforceType(Builder, ReceiverPtr.getPointer(), PtrToIdTy),
          EnforceType(Builder, cmd, SelectorTy),
          EnforceType(Builder, self, IdTy)};
      llvm::CallBase *slot = CGF.EmitRuntimeCallOrInvoke(LookupFn, args);
      slot->setOnlyReadsMemory();
      slot->setMetadata(msgSendMDKind, node);

      // Load the imp from the slot
      llvm::Value *imp = Builder.CreateAlignedLoad(
          IMPTy, Builder.CreateStructGEP(SlotStructTy, slot, 4),
          CGF.getPointerAlign());

      // The lookup function may have changed the receiver, so make sure we use
      // the new one.
      Receiver = Builder.CreateLoad(ReceiverPtr, true);
      return imp;
    }

    llvm::Value *LookupIMPSuper(CodeGenFunction &CGF, Address ObjCSuper,
                                llvm::Value *cmd,
                                MessageSendInfo &MSI) override {
      CGBuilderTy &Builder = CGF.Builder;
      llvm::Value *lookupArgs[] = {ObjCSuper.emitRawPointer(CGF), cmd};

      llvm::CallInst *slot =
        CGF.EmitNounwindRuntimeCall(SlotLookupSuperFn, lookupArgs);
      slot->setOnlyReadsMemory();

      return Builder.CreateAlignedLoad(
          IMPTy, Builder.CreateStructGEP(SlotStructTy, slot, 4),
          CGF.getPointerAlign());
    }

  public:
    CGObjCGNUstep(CodeGenModule &Mod) : CGObjCGNUstep(Mod, 9, 3, 1) {}
    CGObjCGNUstep(CodeGenModule &Mod, unsigned ABI, unsigned ProtocolABI,
        unsigned ClassABI) :
      CGObjCGNU(Mod, ABI, ProtocolABI, ClassABI) {
      const ObjCRuntime &R = CGM.getLangOpts().ObjCRuntime;

      SlotStructTy = llvm::StructType::get(PtrTy, PtrTy, PtrTy, IntTy, IMPTy);
      SlotTy = llvm::PointerType::getUnqual(SlotStructTy);
      // Slot_t objc_msg_lookup_sender(id *receiver, SEL selector, id sender);
      SlotLookupFn.init(&CGM, "objc_msg_lookup_sender", SlotTy, PtrToIdTy,
                        SelectorTy, IdTy);
      // Slot_t objc_slot_lookup_super(struct objc_super*, SEL);
      SlotLookupSuperFn.init(&CGM, "objc_slot_lookup_super", SlotTy,
                             PtrToObjCSuperTy, SelectorTy);
      // If we're in ObjC++ mode, then we want to make
      llvm::Type *VoidTy = llvm::Type::getVoidTy(VMContext);
      if (usesCxxExceptions) {
        // void *__cxa_begin_catch(void *e)
        EnterCatchFn.init(&CGM, "__cxa_begin_catch", PtrTy, PtrTy);
        // void __cxa_end_catch(void)
        ExitCatchFn.init(&CGM, "__cxa_end_catch", VoidTy);
        // void objc_exception_rethrow(void*)
        ExceptionReThrowFn.init(&CGM, "__cxa_rethrow", PtrTy);
      } else if (usesSEHExceptions) {
        // void objc_exception_rethrow(void)
        ExceptionReThrowFn.init(&CGM, "objc_exception_rethrow", VoidTy);
      } else if (CGM.getLangOpts().CPlusPlus) {
        // void *__cxa_begin_catch(void *e)
        EnterCatchFn.init(&CGM, "__cxa_begin_catch", PtrTy, PtrTy);
        // void __cxa_end_catch(void)
        ExitCatchFn.init(&CGM, "__cxa_end_catch", VoidTy);
        // void _Unwind_Resume_or_Rethrow(void*)
        ExceptionReThrowFn.init(&CGM, "_Unwind_Resume_or_Rethrow", VoidTy,
                                PtrTy);
      } else if (R.getVersion() >= VersionTuple(1, 7)) {
        // id objc_begin_catch(void *e)
        EnterCatchFn.init(&CGM, "objc_begin_catch", IdTy, PtrTy);
        // void objc_end_catch(void)
        ExitCatchFn.init(&CGM, "objc_end_catch", VoidTy);
        // void _Unwind_Resume_or_Rethrow(void*)
        ExceptionReThrowFn.init(&CGM, "objc_exception_rethrow", VoidTy, PtrTy);
      }
      SetPropertyAtomic.init(&CGM, "objc_setProperty_atomic", VoidTy, IdTy,
                             SelectorTy, IdTy, PtrDiffTy);
      SetPropertyAtomicCopy.init(&CGM, "objc_setProperty_atomic_copy", VoidTy,
                                 IdTy, SelectorTy, IdTy, PtrDiffTy);
      SetPropertyNonAtomic.init(&CGM, "objc_setProperty_nonatomic", VoidTy,
                                IdTy, SelectorTy, IdTy, PtrDiffTy);
      SetPropertyNonAtomicCopy.init(&CGM, "objc_setProperty_nonatomic_copy",
                                    VoidTy, IdTy, SelectorTy, IdTy, PtrDiffTy);
      // void objc_setCppObjectAtomic(void *dest, const void *src, void
      // *helper);
      CxxAtomicObjectSetFn.init(&CGM, "objc_setCppObjectAtomic", VoidTy, PtrTy,
                                PtrTy, PtrTy);
      // void objc_getCppObjectAtomic(void *dest, const void *src, void
      // *helper);
      CxxAtomicObjectGetFn.init(&CGM, "objc_getCppObjectAtomic", VoidTy, PtrTy,
                                PtrTy, PtrTy);
    }

    llvm::FunctionCallee GetCppAtomicObjectGetFunction() override {
      // The optimised functions were added in version 1.7 of the GNUstep
      // runtime.
      assert (CGM.getLangOpts().ObjCRuntime.getVersion() >=
          VersionTuple(1, 7));
      return CxxAtomicObjectGetFn;
    }

    llvm::FunctionCallee GetCppAtomicObjectSetFunction() override {
      // The optimised functions were added in version 1.7 of the GNUstep
      // runtime.
      assert (CGM.getLangOpts().ObjCRuntime.getVersion() >=
          VersionTuple(1, 7));
      return CxxAtomicObjectSetFn;
    }

    llvm::FunctionCallee GetOptimizedPropertySetFunction(bool atomic,
                                                         bool copy) override {
      // The optimised property functions omit the GC check, and so are not
      // safe to use in GC mode.  The standard functions are fast in GC mode,
      // so there is less advantage in using them.
      assert ((CGM.getLangOpts().getGC() == LangOptions::NonGC));
      // The optimised functions were added in version 1.7 of the GNUstep
      // runtime.
      assert (CGM.getLangOpts().ObjCRuntime.getVersion() >=
          VersionTuple(1, 7));

      if (atomic) {
        if (copy) return SetPropertyAtomicCopy;
        return SetPropertyAtomic;
      }

      return copy ? SetPropertyNonAtomicCopy : SetPropertyNonAtomic;
    }
};

/// GNUstep Objective-C ABI version 2 implementation.
/// This is the ABI that provides a clean break with the legacy GCC ABI and
/// cleans up a number of things that were added to work around 1980s linkers.
class CGObjCGNUstep2 : public CGObjCGNUstep {
  enum SectionKind
  {
    SelectorSection = 0,
    ClassSection,
    ClassReferenceSection,
    CategorySection,
    ProtocolSection,
    ProtocolReferenceSection,
    ClassAliasSection,
    ConstantStringSection
  };
  /// The subset of `objc_class_flags` used at compile time.
  enum ClassFlags {
    /// This is a metaclass
    ClassFlagMeta = (1 << 0),
    /// This class has been initialised by the runtime (+initialize has been
    /// sent if necessary).
    ClassFlagInitialized = (1 << 8),
  };
  static const char *const SectionsBaseNames[8];
  static const char *const PECOFFSectionsBaseNames[8];
  template<SectionKind K>
  std::string sectionName() {
    if (CGM.getTriple().isOSBinFormatCOFF()) {
      std::string name(PECOFFSectionsBaseNames[K]);
      name += "$m";
      return name;
    }
    return SectionsBaseNames[K];
  }
  /// The GCC ABI superclass message lookup function.  Takes a pointer to a
  /// structure describing the receiver and the class, and a selector as
  /// arguments.  Returns the IMP for the corresponding method.
  LazyRuntimeFunction MsgLookupSuperFn;
  /// Function to ensure that +initialize is sent to a class.
  LazyRuntimeFunction SentInitializeFn;
  /// A flag indicating if we've emitted at least one protocol.
  /// If we haven't, then we need to emit an empty protocol, to ensure that the
  /// __start__objc_protocols and __stop__objc_protocols sections exist.
  bool EmittedProtocol = false;
  /// A flag indicating if we've emitted at least one protocol reference.
  /// If we haven't, then we need to emit an empty protocol, to ensure that the
  /// __start__objc_protocol_refs and __stop__objc_protocol_refs sections
  /// exist.
  bool EmittedProtocolRef = false;
  /// A flag indicating if we've emitted at least one class.
  /// If we haven't, then we need to emit an empty protocol, to ensure that the
  /// __start__objc_classes and __stop__objc_classes sections / exist.
  bool EmittedClass = false;
  /// Generate the name of a symbol for a reference to a class.  Accesses to
  /// classes should be indirected via this.

  typedef std::pair<std::string, std::pair<llvm::GlobalVariable*, int>>
      EarlyInitPair;
  std::vector<EarlyInitPair> EarlyInitList;

  std::string SymbolForClassRef(StringRef Name, bool isWeak) {
    if (isWeak)
      return (ManglePublicSymbol("OBJC_WEAK_REF_CLASS_") + Name).str();
    else
      return (ManglePublicSymbol("OBJC_REF_CLASS_") + Name).str();
  }
  /// Generate the name of a class symbol.
  std::string SymbolForClass(StringRef Name) {
    return (ManglePublicSymbol("OBJC_CLASS_") + Name).str();
  }
  void CallRuntimeFunction(CGBuilderTy &B, StringRef FunctionName,
      ArrayRef<llvm::Value*> Args) {
    SmallVector<llvm::Type *,8> Types;
    for (auto *Arg : Args)
      Types.push_back(Arg->getType());
    llvm::FunctionType *FT = llvm::FunctionType::get(B.getVoidTy(), Types,
        false);
    llvm::FunctionCallee Fn = CGM.CreateRuntimeFunction(FT, FunctionName);
    B.CreateCall(Fn, Args);
  }

  ConstantAddress GenerateConstantString(const StringLiteral *SL) override {

    auto Str = SL->getString();
    CharUnits Align = CGM.getPointerAlign();

    // Look for an existing one
    llvm::StringMap<llvm::Constant*>::iterator old = ObjCStrings.find(Str);
    if (old != ObjCStrings.end())
      return ConstantAddress(old->getValue(), IdElemTy, Align);

    bool isNonASCII = SL->containsNonAscii();

    auto LiteralLength = SL->getLength();

    if ((CGM.getTarget().getPointerWidth(LangAS::Default) == 64) &&
        (LiteralLength < 9) && !isNonASCII) {
      // Tiny strings are only used on 64-bit platforms.  They store 8 7-bit
      // ASCII characters in the high 56 bits, followed by a 4-bit length and a
      // 3-bit tag (which is always 4).
      uint64_t str = 0;
      // Fill in the characters
      for (unsigned i=0 ; i<LiteralLength ; i++)
        str |= ((uint64_t)SL->getCodeUnit(i)) << ((64 - 4 - 3) - (i*7));
      // Fill in the length
      str |= LiteralLength << 3;
      // Set the tag
      str |= 4;
      auto *ObjCStr = llvm::ConstantExpr::getIntToPtr(
          llvm::ConstantInt::get(Int64Ty, str), IdTy);
      ObjCStrings[Str] = ObjCStr;
      return ConstantAddress(ObjCStr, IdElemTy, Align);
    }

    StringRef StringClass = CGM.getLangOpts().ObjCConstantStringClass;

    if (StringClass.empty()) StringClass = "NSConstantString";

    std::string Sym = SymbolForClass(StringClass);

    llvm::Constant *isa = TheModule.getNamedGlobal(Sym);

    if (!isa) {
      isa = new llvm::GlobalVariable(TheModule, IdTy, /* isConstant */false,
              llvm::GlobalValue::ExternalLinkage, nullptr, Sym);
      if (CGM.getTriple().isOSBinFormatCOFF()) {
        cast<llvm::GlobalValue>(isa)->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
      }
    }

    //  struct
    //  {
    //    Class isa;
    //    uint32_t flags;
    //    uint32_t length; // Number of codepoints
    //    uint32_t size; // Number of bytes
    //    uint32_t hash;
    //    const char *data;
    //  };

    ConstantInitBuilder Builder(CGM);
    auto Fields = Builder.beginStruct();
    if (!CGM.getTriple().isOSBinFormatCOFF()) {
      Fields.add(isa);
    } else {
      Fields.addNullPointer(PtrTy);
    }
    // For now, all non-ASCII strings are represented as UTF-16.  As such, the
    // number of bytes is simply double the number of UTF-16 codepoints.  In
    // ASCII strings, the number of bytes is equal to the number of non-ASCII
    // codepoints.
    if (isNonASCII) {
      unsigned NumU8CodeUnits = Str.size();
      // A UTF-16 representation of a unicode string contains at most the same
      // number of code units as a UTF-8 representation.  Allocate that much
      // space, plus one for the final null character.
      SmallVector<llvm::UTF16, 128> ToBuf(NumU8CodeUnits + 1);
      const llvm::UTF8 *FromPtr = (const llvm::UTF8 *)Str.data();
      llvm::UTF16 *ToPtr = &ToBuf[0];
      (void)llvm::ConvertUTF8toUTF16(&FromPtr, FromPtr + NumU8CodeUnits,
          &ToPtr, ToPtr + NumU8CodeUnits, llvm::strictConversion);
      uint32_t StringLength = ToPtr - &ToBuf[0];
      // Add null terminator
      *ToPtr = 0;
      // Flags: 2 indicates UTF-16 encoding
      Fields.addInt(Int32Ty, 2);
      // Number of UTF-16 codepoints
      Fields.addInt(Int32Ty, StringLength);
      // Number of bytes
      Fields.addInt(Int32Ty, StringLength * 2);
      // Hash.  Not currently initialised by the compiler.
      Fields.addInt(Int32Ty, 0);
      // pointer to the data string.
      auto Arr = llvm::ArrayRef(&ToBuf[0], ToPtr + 1);
      auto *C = llvm::ConstantDataArray::get(VMContext, Arr);
      auto *Buffer = new llvm::GlobalVariable(TheModule, C->getType(),
          /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage, C, ".str");
      Buffer->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      Fields.add(Buffer);
    } else {
      // Flags: 0 indicates ASCII encoding
      Fields.addInt(Int32Ty, 0);
      // Number of UTF-16 codepoints, each ASCII byte is a UTF-16 codepoint
      Fields.addInt(Int32Ty, Str.size());
      // Number of bytes
      Fields.addInt(Int32Ty, Str.size());
      // Hash.  Not currently initialised by the compiler.
      Fields.addInt(Int32Ty, 0);
      // Data pointer
      Fields.add(MakeConstantString(Str));
    }
    std::string StringName;
    bool isNamed = !isNonASCII;
    if (isNamed) {
      StringName = ".objc_str_";
      for (int i=0,e=Str.size() ; i<e ; ++i) {
        unsigned char c = Str[i];
        if (isalnum(c))
          StringName += c;
        else if (c == ' ')
          StringName += '_';
        else {
          isNamed = false;
          break;
        }
      }
    }
    llvm::GlobalVariable *ObjCStrGV =
      Fields.finishAndCreateGlobal(
          isNamed ? StringRef(StringName) : ".objc_string",
          Align, false, isNamed ? llvm::GlobalValue::LinkOnceODRLinkage
                                : llvm::GlobalValue::PrivateLinkage);
    ObjCStrGV->setSection(sectionName<ConstantStringSection>());
    if (isNamed) {
      ObjCStrGV->setComdat(TheModule.getOrInsertComdat(StringName));
      ObjCStrGV->setVisibility(llvm::GlobalValue::HiddenVisibility);
    }
    if (CGM.getTriple().isOSBinFormatCOFF()) {
      std::pair<llvm::GlobalVariable*, int> v{ObjCStrGV, 0};
      EarlyInitList.emplace_back(Sym, v);
    }
    ObjCStrings[Str] = ObjCStrGV;
    ConstantStrings.push_back(ObjCStrGV);
    return ConstantAddress(ObjCStrGV, IdElemTy, Align);
  }

  void PushProperty(ConstantArrayBuilder &PropertiesArray,
            const ObjCPropertyDecl *property,
            const Decl *OCD,
            bool isSynthesized=true, bool
            isDynamic=true) override {
    // struct objc_property
    // {
    //   const char *name;
    //   const char *attributes;
    //   const char *type;
    //   SEL getter;
    //   SEL setter;
    // };
    auto Fields = PropertiesArray.beginStruct(PropertyMetadataTy);
    ASTContext &Context = CGM.getContext();
    Fields.add(MakeConstantString(property->getNameAsString()));
    std::string TypeStr =
      CGM.getContext().getObjCEncodingForPropertyDecl(property, OCD);
    Fields.add(MakeConstantString(TypeStr));
    std::string typeStr;
    Context.getObjCEncodingForType(property->getType(), typeStr);
    Fields.add(MakeConstantString(typeStr));
    auto addPropertyMethod = [&](const ObjCMethodDecl *accessor) {
      if (accessor) {
        std::string TypeStr = Context.getObjCEncodingForMethodDecl(accessor);
        Fields.add(GetConstantSelector(accessor->getSelector(), TypeStr));
      } else {
        Fields.add(NULLPtr);
      }
    };
    addPropertyMethod(property->getGetterMethodDecl());
    addPropertyMethod(property->getSetterMethodDecl());
    Fields.finishAndAddTo(PropertiesArray);
  }

  llvm::Constant *
  GenerateProtocolMethodList(ArrayRef<const ObjCMethodDecl*> Methods) override {
    // struct objc_protocol_method_description
    // {
    //   SEL selector;
    //   const char *types;
    // };
    llvm::StructType *ObjCMethodDescTy =
      llvm::StructType::get(CGM.getLLVMContext(),
          { PtrToInt8Ty, PtrToInt8Ty });
    ASTContext &Context = CGM.getContext();
    ConstantInitBuilder Builder(CGM);
    // struct objc_protocol_method_description_list
    // {
    //   int count;
    //   int size;
    //   struct objc_protocol_method_description methods[];
    // };
    auto MethodList = Builder.beginStruct();
    // int count;
    MethodList.addInt(IntTy, Methods.size());
    // int size; // sizeof(struct objc_method_description)
    llvm::DataLayout td(&TheModule);
    MethodList.addInt(IntTy, td.getTypeSizeInBits(ObjCMethodDescTy) /
        CGM.getContext().getCharWidth());
    // struct objc_method_description[]
    auto MethodArray = MethodList.beginArray(ObjCMethodDescTy);
    for (auto *M : Methods) {
      auto Method = MethodArray.beginStruct(ObjCMethodDescTy);
      Method.add(CGObjCGNU::GetConstantSelector(M));
      Method.add(GetTypeString(Context.getObjCEncodingForMethodDecl(M, true)));
      Method.finishAndAddTo(MethodArray);
    }
    MethodArray.finishAndAddTo(MethodList);
    return MethodList.finishAndCreateGlobal(".objc_protocol_method_list",
                                            CGM.getPointerAlign());
  }
  llvm::Constant *GenerateCategoryProtocolList(const ObjCCategoryDecl *OCD)
    override {
    const auto &ReferencedProtocols = OCD->getReferencedProtocols();
    auto RuntimeProtocols = GetRuntimeProtocolList(ReferencedProtocols.begin(),
                                                   ReferencedProtocols.end());
    SmallVector<llvm::Constant *, 16> Protocols;
    for (const auto *PI : RuntimeProtocols)
      Protocols.push_back(GenerateProtocolRef(PI));
    return GenerateProtocolList(Protocols);
  }

  llvm::Value *LookupIMPSuper(CodeGenFunction &CGF, Address ObjCSuper,
                              llvm::Value *cmd, MessageSendInfo &MSI) override {
    // Don't access the slot unless we're trying to cache the result.
    CGBuilderTy &Builder = CGF.Builder;
    llvm::Value *lookupArgs[] = {
        CGObjCGNU::EnforceType(Builder, ObjCSuper.emitRawPointer(CGF),
                               PtrToObjCSuperTy),
        cmd};
    return CGF.EmitNounwindRuntimeCall(MsgLookupSuperFn, lookupArgs);
  }

  llvm::GlobalVariable *GetClassVar(StringRef Name, bool isWeak=false) {
    std::string SymbolName = SymbolForClassRef(Name, isWeak);
    auto *ClassSymbol = TheModule.getNamedGlobal(SymbolName);
    if (ClassSymbol)
      return ClassSymbol;
    ClassSymbol = new llvm::GlobalVariable(TheModule,
        IdTy, false, llvm::GlobalValue::ExternalLinkage,
        nullptr, SymbolName);
    // If this is a weak symbol, then we are creating a valid definition for
    // the symbol, pointing to a weak definition of the real class pointer.  If
    // this is not a weak reference, then we are expecting another compilation
    // unit to provide the real indirection symbol.
    if (isWeak)
      ClassSymbol->setInitializer(new llvm::GlobalVariable(TheModule,
          Int8Ty, false, llvm::GlobalValue::ExternalWeakLinkage,
          nullptr, SymbolForClass(Name)));
    else {
      if (CGM.getTriple().isOSBinFormatCOFF()) {
        IdentifierInfo &II = CGM.getContext().Idents.get(Name);
        TranslationUnitDecl *TUDecl = CGM.getContext().getTranslationUnitDecl();
        DeclContext *DC = TranslationUnitDecl::castToDeclContext(TUDecl);

        const ObjCInterfaceDecl *OID = nullptr;
        for (const auto *Result : DC->lookup(&II))
          if ((OID = dyn_cast<ObjCInterfaceDecl>(Result)))
            break;

        // The first Interface we find may be a @class,
        // which should only be treated as the source of
        // truth in the absence of a true declaration.
        assert(OID && "Failed to find ObjCInterfaceDecl");
        const ObjCInterfaceDecl *OIDDef = OID->getDefinition();
        if (OIDDef != nullptr)
          OID = OIDDef;

        auto Storage = llvm::GlobalValue::DefaultStorageClass;
        if (OID->hasAttr<DLLImportAttr>())
          Storage = llvm::GlobalValue::DLLImportStorageClass;
        else if (OID->hasAttr<DLLExportAttr>())
          Storage = llvm::GlobalValue::DLLExportStorageClass;

        cast<llvm::GlobalValue>(ClassSymbol)->setDLLStorageClass(Storage);
      }
    }
    assert(ClassSymbol->getName() == SymbolName);
    return ClassSymbol;
  }
  llvm::Value *GetClassNamed(CodeGenFunction &CGF,
                             const std::string &Name,
                             bool isWeak) override {
    return CGF.Builder.CreateLoad(
        Address(GetClassVar(Name, isWeak), IdTy, CGM.getPointerAlign()));
  }
  int32_t FlagsForOwnership(Qualifiers::ObjCLifetime Ownership) {
    // typedef enum {
    //   ownership_invalid = 0,
    //   ownership_strong  = 1,
    //   ownership_weak    = 2,
    //   ownership_unsafe  = 3
    // } ivar_ownership;
    int Flag;
    switch (Ownership) {
      case Qualifiers::OCL_Strong:
          Flag = 1;
          break;
      case Qualifiers::OCL_Weak:
          Flag = 2;
          break;
      case Qualifiers::OCL_ExplicitNone:
          Flag = 3;
          break;
      case Qualifiers::OCL_None:
      case Qualifiers::OCL_Autoreleasing:
        assert(Ownership != Qualifiers::OCL_Autoreleasing);
        Flag = 0;
    }
    return Flag;
  }
  llvm::Constant *GenerateIvarList(ArrayRef<llvm::Constant *> IvarNames,
                   ArrayRef<llvm::Constant *> IvarTypes,
                   ArrayRef<llvm::Constant *> IvarOffsets,
                   ArrayRef<llvm::Constant *> IvarAlign,
                   ArrayRef<Qualifiers::ObjCLifetime> IvarOwnership) override {
    llvm_unreachable("Method should not be called!");
  }

  llvm::Constant *GenerateEmptyProtocol(StringRef ProtocolName) override {
    std::string Name = SymbolForProtocol(ProtocolName);
    auto *GV = TheModule.getGlobalVariable(Name);
    if (!GV) {
      // Emit a placeholder symbol.
      GV = new llvm::GlobalVariable(TheModule, ProtocolTy, false,
          llvm::GlobalValue::ExternalLinkage, nullptr, Name);
      GV->setAlignment(CGM.getPointerAlign().getAsAlign());
    }
    return GV;
  }

  /// Existing protocol references.
  llvm::StringMap<llvm::Constant*> ExistingProtocolRefs;

  llvm::Value *GenerateProtocolRef(CodeGenFunction &CGF,
                                   const ObjCProtocolDecl *PD) override {
    auto Name = PD->getNameAsString();
    auto *&Ref = ExistingProtocolRefs[Name];
    if (!Ref) {
      auto *&Protocol = ExistingProtocols[Name];
      if (!Protocol)
        Protocol = GenerateProtocolRef(PD);
      std::string RefName = SymbolForProtocolRef(Name);
      assert(!TheModule.getGlobalVariable(RefName));
      // Emit a reference symbol.
      auto GV = new llvm::GlobalVariable(TheModule, ProtocolPtrTy, false,
                                         llvm::GlobalValue::LinkOnceODRLinkage,
                                         Protocol, RefName);
      GV->setComdat(TheModule.getOrInsertComdat(RefName));
      GV->setSection(sectionName<ProtocolReferenceSection>());
      GV->setAlignment(CGM.getPointerAlign().getAsAlign());
      Ref = GV;
    }
    EmittedProtocolRef = true;
    return CGF.Builder.CreateAlignedLoad(ProtocolPtrTy, Ref,
                                         CGM.getPointerAlign());
  }

  llvm::Constant *GenerateProtocolList(ArrayRef<llvm::Constant*> Protocols) {
    llvm::ArrayType *ProtocolArrayTy = llvm::ArrayType::get(ProtocolPtrTy,
        Protocols.size());
    llvm::Constant * ProtocolArray = llvm::ConstantArray::get(ProtocolArrayTy,
        Protocols);
    ConstantInitBuilder builder(CGM);
    auto ProtocolBuilder = builder.beginStruct();
    ProtocolBuilder.addNullPointer(PtrTy);
    ProtocolBuilder.addInt(SizeTy, Protocols.size());
    ProtocolBuilder.add(ProtocolArray);
    return ProtocolBuilder.finishAndCreateGlobal(".objc_protocol_list",
        CGM.getPointerAlign(), false, llvm::GlobalValue::InternalLinkage);
  }

  void GenerateProtocol(const ObjCProtocolDecl *PD) override {
    // Do nothing - we only emit referenced protocols.
  }
  llvm::Constant *GenerateProtocolRef(const ObjCProtocolDecl *PD) override {
    std::string ProtocolName = PD->getNameAsString();
    auto *&Protocol = ExistingProtocols[ProtocolName];
    if (Protocol)
      return Protocol;

    EmittedProtocol = true;

    auto SymName = SymbolForProtocol(ProtocolName);
    auto *OldGV = TheModule.getGlobalVariable(SymName);

    // Use the protocol definition, if there is one.
    if (const ObjCProtocolDecl *Def = PD->getDefinition())
      PD = Def;
    else {
      // If there is no definition, then create an external linkage symbol and
      // hope that someone else fills it in for us (and fail to link if they
      // don't).
      assert(!OldGV);
      Protocol = new llvm::GlobalVariable(TheModule, ProtocolTy,
        /*isConstant*/false,
        llvm::GlobalValue::ExternalLinkage, nullptr, SymName);
      return Protocol;
    }

    SmallVector<llvm::Constant*, 16> Protocols;
    auto RuntimeProtocols =
        GetRuntimeProtocolList(PD->protocol_begin(), PD->protocol_end());
    for (const auto *PI : RuntimeProtocols)
      Protocols.push_back(GenerateProtocolRef(PI));
    llvm::Constant *ProtocolList = GenerateProtocolList(Protocols);

    // Collect information about methods
    llvm::Constant *InstanceMethodList, *OptionalInstanceMethodList;
    llvm::Constant *ClassMethodList, *OptionalClassMethodList;
    EmitProtocolMethodList(PD->instance_methods(), InstanceMethodList,
        OptionalInstanceMethodList);
    EmitProtocolMethodList(PD->class_methods(), ClassMethodList,
        OptionalClassMethodList);

    // The isa pointer must be set to a magic number so the runtime knows it's
    // the correct layout.
    ConstantInitBuilder builder(CGM);
    auto ProtocolBuilder = builder.beginStruct();
    ProtocolBuilder.add(llvm::ConstantExpr::getIntToPtr(
          llvm::ConstantInt::get(Int32Ty, ProtocolVersion), IdTy));
    ProtocolBuilder.add(MakeConstantString(ProtocolName));
    ProtocolBuilder.add(ProtocolList);
    ProtocolBuilder.add(InstanceMethodList);
    ProtocolBuilder.add(ClassMethodList);
    ProtocolBuilder.add(OptionalInstanceMethodList);
    ProtocolBuilder.add(OptionalClassMethodList);
    // Required instance properties
    ProtocolBuilder.add(GeneratePropertyList(nullptr, PD, false, false));
    // Optional instance properties
    ProtocolBuilder.add(GeneratePropertyList(nullptr, PD, false, true));
    // Required class properties
    ProtocolBuilder.add(GeneratePropertyList(nullptr, PD, true, false));
    // Optional class properties
    ProtocolBuilder.add(GeneratePropertyList(nullptr, PD, true, true));

    auto *GV = ProtocolBuilder.finishAndCreateGlobal(SymName,
        CGM.getPointerAlign(), false, llvm::GlobalValue::ExternalLinkage);
    GV->setSection(sectionName<ProtocolSection>());
    GV->setComdat(TheModule.getOrInsertComdat(SymName));
    if (OldGV) {
      OldGV->replaceAllUsesWith(GV);
      OldGV->removeFromParent();
      GV->setName(SymName);
    }
    Protocol = GV;
    return GV;
  }
  llvm::Value *GetTypedSelector(CodeGenFunction &CGF, Selector Sel,
                                const std::string &TypeEncoding) override {
    return GetConstantSelector(Sel, TypeEncoding);
  }
  std::string GetSymbolNameForTypeEncoding(const std::string &TypeEncoding) {
    std::string MangledTypes = std::string(TypeEncoding);
    // @ is used as a special character in ELF symbol names (used for symbol
    // versioning), so mangle the name to not include it.  Replace it with a
    // character that is not a valid type encoding character (and, being
    // non-printable, never will be!)
    if (CGM.getTriple().isOSBinFormatELF())
      std::replace(MangledTypes.begin(), MangledTypes.end(), '@', '\1');
    // = in dll exported names causes lld to fail when linking on Windows.
    if (CGM.getTriple().isOSWindows())
      std::replace(MangledTypes.begin(), MangledTypes.end(), '=', '\2');
    return MangledTypes;
  }
  llvm::Constant  *GetTypeString(llvm::StringRef TypeEncoding) {
    if (TypeEncoding.empty())
      return NULLPtr;
    std::string MangledTypes =
        GetSymbolNameForTypeEncoding(std::string(TypeEncoding));
    std::string TypesVarName = ".objc_sel_types_" + MangledTypes;
    auto *TypesGlobal = TheModule.getGlobalVariable(TypesVarName);
    if (!TypesGlobal) {
      llvm::Constant *Init = llvm::ConstantDataArray::getString(VMContext,
          TypeEncoding);
      auto *GV = new llvm::GlobalVariable(TheModule, Init->getType(),
          true, llvm::GlobalValue::LinkOnceODRLinkage, Init, TypesVarName);
      GV->setComdat(TheModule.getOrInsertComdat(TypesVarName));
      GV->setVisibility(llvm::GlobalValue::HiddenVisibility);
      TypesGlobal = GV;
    }
    return TypesGlobal;
  }
  llvm::Constant *GetConstantSelector(Selector Sel,
                                      const std::string &TypeEncoding) override {
    std::string MangledTypes = GetSymbolNameForTypeEncoding(TypeEncoding);
    auto SelVarName = (StringRef(".objc_selector_") + Sel.getAsString() + "_" +
      MangledTypes).str();
    if (auto *GV = TheModule.getNamedGlobal(SelVarName))
      return GV;
    ConstantInitBuilder builder(CGM);
    auto SelBuilder = builder.beginStruct();
    SelBuilder.add(ExportUniqueString(Sel.getAsString(), ".objc_sel_name_",
          true));
    SelBuilder.add(GetTypeString(TypeEncoding));
    auto *GV = SelBuilder.finishAndCreateGlobal(SelVarName,
        CGM.getPointerAlign(), false, llvm::GlobalValue::LinkOnceODRLinkage);
    GV->setComdat(TheModule.getOrInsertComdat(SelVarName));
    GV->setVisibility(llvm::GlobalValue::HiddenVisibility);
    GV->setSection(sectionName<SelectorSection>());
    return GV;
  }
  llvm::StructType *emptyStruct = nullptr;

  /// Return pointers to the start and end of a section.  On ELF platforms, we
  /// use the __start_ and __stop_ symbols that GNU-compatible linkers will set
  /// to the start and end of section names, as long as those section names are
  /// valid identifiers and the symbols are referenced but not defined.  On
  /// Windows, we use the fact that MSVC-compatible linkers will lexically sort
  /// by subsections and place everything that we want to reference in a middle
  /// subsection and then insert zero-sized symbols in subsections a and z.
  std::pair<llvm::Constant*,llvm::Constant*>
  GetSectionBounds(StringRef Section) {
    if (CGM.getTriple().isOSBinFormatCOFF()) {
      if (emptyStruct == nullptr) {
        emptyStruct = llvm::StructType::create(VMContext, ".objc_section_sentinel");
        emptyStruct->setBody({}, /*isPacked*/true);
      }
      auto ZeroInit = llvm::Constant::getNullValue(emptyStruct);
      auto Sym = [&](StringRef Prefix, StringRef SecSuffix) {
        auto *Sym = new llvm::GlobalVariable(TheModule, emptyStruct,
            /*isConstant*/false,
            llvm::GlobalValue::LinkOnceODRLinkage, ZeroInit, Prefix +
            Section);
        Sym->setVisibility(llvm::GlobalValue::HiddenVisibility);
        Sym->setSection((Section + SecSuffix).str());
        Sym->setComdat(TheModule.getOrInsertComdat((Prefix +
            Section).str()));
        Sym->setAlignment(CGM.getPointerAlign().getAsAlign());
        return Sym;
      };
      return { Sym("__start_", "$a"), Sym("__stop", "$z") };
    }
    auto *Start = new llvm::GlobalVariable(TheModule, PtrTy,
        /*isConstant*/false,
        llvm::GlobalValue::ExternalLinkage, nullptr, StringRef("__start_") +
        Section);
    Start->setVisibility(llvm::GlobalValue::HiddenVisibility);
    auto *Stop = new llvm::GlobalVariable(TheModule, PtrTy,
        /*isConstant*/false,
        llvm::GlobalValue::ExternalLinkage, nullptr, StringRef("__stop_") +
        Section);
    Stop->setVisibility(llvm::GlobalValue::HiddenVisibility);
    return { Start, Stop };
  }
  CatchTypeInfo getCatchAllTypeInfo() override {
    return CGM.getCXXABI().getCatchAllTypeInfo();
  }
  llvm::Function *ModuleInitFunction() override {
    llvm::Function *LoadFunction = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(VMContext), false),
      llvm::GlobalValue::LinkOnceODRLinkage, ".objcv2_load_function",
      &TheModule);
    LoadFunction->setVisibility(llvm::GlobalValue::HiddenVisibility);
    LoadFunction->setComdat(TheModule.getOrInsertComdat(".objcv2_load_function"));

    llvm::BasicBlock *EntryBB =
        llvm::BasicBlock::Create(VMContext, "entry", LoadFunction);
    CGBuilderTy B(CGM, VMContext);
    B.SetInsertPoint(EntryBB);
    ConstantInitBuilder builder(CGM);
    auto InitStructBuilder = builder.beginStruct();
    InitStructBuilder.addInt(Int64Ty, 0);
    auto &sectionVec = CGM.getTriple().isOSBinFormatCOFF() ? PECOFFSectionsBaseNames : SectionsBaseNames;
    for (auto *s : sectionVec) {
      auto bounds = GetSectionBounds(s);
      InitStructBuilder.add(bounds.first);
      InitStructBuilder.add(bounds.second);
    }
    auto *InitStruct = InitStructBuilder.finishAndCreateGlobal(".objc_init",
        CGM.getPointerAlign(), false, llvm::GlobalValue::LinkOnceODRLinkage);
    InitStruct->setVisibility(llvm::GlobalValue::HiddenVisibility);
    InitStruct->setComdat(TheModule.getOrInsertComdat(".objc_init"));

    CallRuntimeFunction(B, "__objc_load", {InitStruct});;
    B.CreateRetVoid();
    // Make sure that the optimisers don't delete this function.
    CGM.addCompilerUsedGlobal(LoadFunction);
    // FIXME: Currently ELF only!
    // We have to do this by hand, rather than with @llvm.ctors, so that the
    // linker can remove the duplicate invocations.
    auto *InitVar = new llvm::GlobalVariable(TheModule, LoadFunction->getType(),
        /*isConstant*/false, llvm::GlobalValue::LinkOnceAnyLinkage,
        LoadFunction, ".objc_ctor");
    // Check that this hasn't been renamed.  This shouldn't happen, because
    // this function should be called precisely once.
    assert(InitVar->getName() == ".objc_ctor");
    // In Windows, initialisers are sorted by the suffix.  XCL is for library
    // initialisers, which run before user initialisers.  We are running
    // Objective-C loads at the end of library load.  This means +load methods
    // will run before any other static constructors, but that static
    // constructors can see a fully initialised Objective-C state.
    if (CGM.getTriple().isOSBinFormatCOFF())
        InitVar->setSection(".CRT$XCLz");
    else
    {
      if (CGM.getCodeGenOpts().UseInitArray)
        InitVar->setSection(".init_array");
      else
        InitVar->setSection(".ctors");
    }
    InitVar->setVisibility(llvm::GlobalValue::HiddenVisibility);
    InitVar->setComdat(TheModule.getOrInsertComdat(".objc_ctor"));
    CGM.addUsedGlobal(InitVar);
    for (auto *C : Categories) {
      auto *Cat = cast<llvm::GlobalVariable>(C->stripPointerCasts());
      Cat->setSection(sectionName<CategorySection>());
      CGM.addUsedGlobal(Cat);
    }
    auto createNullGlobal = [&](StringRef Name, ArrayRef<llvm::Constant*> Init,
        StringRef Section) {
      auto nullBuilder = builder.beginStruct();
      for (auto *F : Init)
        nullBuilder.add(F);
      auto GV = nullBuilder.finishAndCreateGlobal(Name, CGM.getPointerAlign(),
          false, llvm::GlobalValue::LinkOnceODRLinkage);
      GV->setSection(Section);
      GV->setComdat(TheModule.getOrInsertComdat(Name));
      GV->setVisibility(llvm::GlobalValue::HiddenVisibility);
      CGM.addUsedGlobal(GV);
      return GV;
    };
    for (auto clsAlias : ClassAliases)
      createNullGlobal(std::string(".objc_class_alias") +
          clsAlias.second, { MakeConstantString(clsAlias.second),
          GetClassVar(clsAlias.first) }, sectionName<ClassAliasSection>());
    // On ELF platforms, add a null value for each special section so that we
    // can always guarantee that the _start and _stop symbols will exist and be
    // meaningful.  This is not required on COFF platforms, where our start and
    // stop symbols will create the section.
    if (!CGM.getTriple().isOSBinFormatCOFF()) {
      createNullGlobal(".objc_null_selector", {NULLPtr, NULLPtr},
          sectionName<SelectorSection>());
      if (Categories.empty())
        createNullGlobal(".objc_null_category", {NULLPtr, NULLPtr,
                      NULLPtr, NULLPtr, NULLPtr, NULLPtr, NULLPtr},
            sectionName<CategorySection>());
      if (!EmittedClass) {
        createNullGlobal(".objc_null_cls_init_ref", NULLPtr,
            sectionName<ClassSection>());
        createNullGlobal(".objc_null_class_ref", { NULLPtr, NULLPtr },
            sectionName<ClassReferenceSection>());
      }
      if (!EmittedProtocol)
        createNullGlobal(".objc_null_protocol", {NULLPtr, NULLPtr, NULLPtr,
            NULLPtr, NULLPtr, NULLPtr, NULLPtr, NULLPtr, NULLPtr, NULLPtr,
            NULLPtr}, sectionName<ProtocolSection>());
      if (!EmittedProtocolRef)
        createNullGlobal(".objc_null_protocol_ref", {NULLPtr},
            sectionName<ProtocolReferenceSection>());
      if (ClassAliases.empty())
        createNullGlobal(".objc_null_class_alias", { NULLPtr, NULLPtr },
            sectionName<ClassAliasSection>());
      if (ConstantStrings.empty()) {
        auto i32Zero = llvm::ConstantInt::get(Int32Ty, 0);
        createNullGlobal(".objc_null_constant_string", { NULLPtr, i32Zero,
            i32Zero, i32Zero, i32Zero, NULLPtr },
            sectionName<ConstantStringSection>());
      }
    }
    ConstantStrings.clear();
    Categories.clear();
    Classes.clear();

    if (EarlyInitList.size() > 0) {
      auto *Init = llvm::Function::Create(llvm::FunctionType::get(CGM.VoidTy,
            {}), llvm::GlobalValue::InternalLinkage, ".objc_early_init",
          &CGM.getModule());
      llvm::IRBuilder<> b(llvm::BasicBlock::Create(CGM.getLLVMContext(), "entry",
            Init));
      for (const auto &lateInit : EarlyInitList) {
        auto *global = TheModule.getGlobalVariable(lateInit.first);
        if (global) {
          llvm::GlobalVariable *GV = lateInit.second.first;
          b.CreateAlignedStore(
              global,
              b.CreateStructGEP(GV->getValueType(), GV, lateInit.second.second),
              CGM.getPointerAlign().getAsAlign());
        }
      }
      b.CreateRetVoid();
      // We can't use the normal LLVM global initialisation array, because we
      // need to specify that this runs early in library initialisation.
      auto *InitVar = new llvm::GlobalVariable(CGM.getModule(), Init->getType(),
          /*isConstant*/true, llvm::GlobalValue::InternalLinkage,
          Init, ".objc_early_init_ptr");
      InitVar->setSection(".CRT$XCLb");
      CGM.addUsedGlobal(InitVar);
    }
    return nullptr;
  }
  /// In the v2 ABI, ivar offset variables use the type encoding in their name
  /// to trigger linker failures if the types don't match.
  std::string GetIVarOffsetVariableName(const ObjCInterfaceDecl *ID,
                                        const ObjCIvarDecl *Ivar) override {
    std::string TypeEncoding;
    CGM.getContext().getObjCEncodingForType(Ivar->getType(), TypeEncoding);
    TypeEncoding = GetSymbolNameForTypeEncoding(TypeEncoding);
    const std::string Name = "__objc_ivar_offset_" + ID->getNameAsString()
      + '.' + Ivar->getNameAsString() + '.' + TypeEncoding;
    return Name;
  }
  llvm::Value *EmitIvarOffset(CodeGenFunction &CGF,
                              const ObjCInterfaceDecl *Interface,
                              const ObjCIvarDecl *Ivar) override {
    const std::string Name = GetIVarOffsetVariableName(Ivar->getContainingInterface(), Ivar);
    llvm::GlobalVariable *IvarOffsetPointer = TheModule.getNamedGlobal(Name);
    if (!IvarOffsetPointer)
      IvarOffsetPointer = new llvm::GlobalVariable(TheModule, IntTy, false,
              llvm::GlobalValue::ExternalLinkage, nullptr, Name);
    CharUnits Align = CGM.getIntAlign();
    llvm::Value *Offset =
        CGF.Builder.CreateAlignedLoad(IntTy, IvarOffsetPointer, Align);
    if (Offset->getType() != PtrDiffTy)
      Offset = CGF.Builder.CreateZExtOrBitCast(Offset, PtrDiffTy);
    return Offset;
  }
  void GenerateClass(const ObjCImplementationDecl *OID) override {
    ASTContext &Context = CGM.getContext();
    bool IsCOFF = CGM.getTriple().isOSBinFormatCOFF();

    // Get the class name
    ObjCInterfaceDecl *classDecl =
        const_cast<ObjCInterfaceDecl *>(OID->getClassInterface());
    std::string className = classDecl->getNameAsString();
    auto *classNameConstant = MakeConstantString(className);

    ConstantInitBuilder builder(CGM);
    auto metaclassFields = builder.beginStruct();
    // struct objc_class *isa;
    metaclassFields.addNullPointer(PtrTy);
    // struct objc_class *super_class;
    metaclassFields.addNullPointer(PtrTy);
    // const char *name;
    metaclassFields.add(classNameConstant);
    // long version;
    metaclassFields.addInt(LongTy, 0);
    // unsigned long info;
    // objc_class_flag_meta
    metaclassFields.addInt(LongTy, ClassFlags::ClassFlagMeta);
    // long instance_size;
    // Setting this to zero is consistent with the older ABI, but it might be
    // more sensible to set this to sizeof(struct objc_class)
    metaclassFields.addInt(LongTy, 0);
    // struct objc_ivar_list *ivars;
    metaclassFields.addNullPointer(PtrTy);
    // struct objc_method_list *methods
    // FIXME: Almost identical code is copied and pasted below for the
    // class, but refactoring it cleanly requires C++14 generic lambdas.
    if (OID->classmeth_begin() == OID->classmeth_end())
      metaclassFields.addNullPointer(PtrTy);
    else {
      SmallVector<ObjCMethodDecl*, 16> ClassMethods;
      ClassMethods.insert(ClassMethods.begin(), OID->classmeth_begin(),
          OID->classmeth_end());
      metaclassFields.add(
          GenerateMethodList(className, "", ClassMethods, true));
    }
    // void *dtable;
    metaclassFields.addNullPointer(PtrTy);
    // IMP cxx_construct;
    metaclassFields.addNullPointer(PtrTy);
    // IMP cxx_destruct;
    metaclassFields.addNullPointer(PtrTy);
    // struct objc_class *subclass_list
    metaclassFields.addNullPointer(PtrTy);
    // struct objc_class *sibling_class
    metaclassFields.addNullPointer(PtrTy);
    // struct objc_protocol_list *protocols;
    metaclassFields.addNullPointer(PtrTy);
    // struct reference_list *extra_data;
    metaclassFields.addNullPointer(PtrTy);
    // long abi_version;
    metaclassFields.addInt(LongTy, 0);
    // struct objc_property_list *properties
    metaclassFields.add(GeneratePropertyList(OID, classDecl, /*isClassProperty*/true));

    auto *metaclass = metaclassFields.finishAndCreateGlobal(
        ManglePublicSymbol("OBJC_METACLASS_") + className,
        CGM.getPointerAlign());

    auto classFields = builder.beginStruct();
    // struct objc_class *isa;
    classFields.add(metaclass);
    // struct objc_class *super_class;
    // Get the superclass name.
    const ObjCInterfaceDecl * SuperClassDecl =
      OID->getClassInterface()->getSuperClass();
    llvm::Constant *SuperClass = nullptr;
    if (SuperClassDecl) {
      auto SuperClassName = SymbolForClass(SuperClassDecl->getNameAsString());
      SuperClass = TheModule.getNamedGlobal(SuperClassName);
      if (!SuperClass)
      {
        SuperClass = new llvm::GlobalVariable(TheModule, PtrTy, false,
            llvm::GlobalValue::ExternalLinkage, nullptr, SuperClassName);
        if (IsCOFF) {
          auto Storage = llvm::GlobalValue::DefaultStorageClass;
          if (SuperClassDecl->hasAttr<DLLImportAttr>())
            Storage = llvm::GlobalValue::DLLImportStorageClass;
          else if (SuperClassDecl->hasAttr<DLLExportAttr>())
            Storage = llvm::GlobalValue::DLLExportStorageClass;

          cast<llvm::GlobalValue>(SuperClass)->setDLLStorageClass(Storage);
        }
      }
      if (!IsCOFF)
        classFields.add(SuperClass);
      else
        classFields.addNullPointer(PtrTy);
    } else
      classFields.addNullPointer(PtrTy);
    // const char *name;
    classFields.add(classNameConstant);
    // long version;
    classFields.addInt(LongTy, 0);
    // unsigned long info;
    // !objc_class_flag_meta
    classFields.addInt(LongTy, 0);
    // long instance_size;
    int superInstanceSize = !SuperClassDecl ? 0 :
      Context.getASTObjCInterfaceLayout(SuperClassDecl).getSize().getQuantity();
    // Instance size is negative for classes that have not yet had their ivar
    // layout calculated.
    classFields.addInt(LongTy,
      0 - (Context.getASTObjCImplementationLayout(OID).getSize().getQuantity() -
      superInstanceSize));

    if (classDecl->all_declared_ivar_begin() == nullptr)
      classFields.addNullPointer(PtrTy);
    else {
      int ivar_count = 0;
      for (const ObjCIvarDecl *IVD = classDecl->all_declared_ivar_begin(); IVD;
           IVD = IVD->getNextIvar()) ivar_count++;
      llvm::DataLayout td(&TheModule);
      // struct objc_ivar_list *ivars;
      ConstantInitBuilder b(CGM);
      auto ivarListBuilder = b.beginStruct();
      // int count;
      ivarListBuilder.addInt(IntTy, ivar_count);
      // size_t size;
      llvm::StructType *ObjCIvarTy = llvm::StructType::get(
        PtrToInt8Ty,
        PtrToInt8Ty,
        PtrToInt8Ty,
        Int32Ty,
        Int32Ty);
      ivarListBuilder.addInt(SizeTy, td.getTypeSizeInBits(ObjCIvarTy) /
          CGM.getContext().getCharWidth());
      // struct objc_ivar ivars[]
      auto ivarArrayBuilder = ivarListBuilder.beginArray();
      for (const ObjCIvarDecl *IVD = classDecl->all_declared_ivar_begin(); IVD;
           IVD = IVD->getNextIvar()) {
        auto ivarTy = IVD->getType();
        auto ivarBuilder = ivarArrayBuilder.beginStruct();
        // const char *name;
        ivarBuilder.add(MakeConstantString(IVD->getNameAsString()));
        // const char *type;
        std::string TypeStr;
        //Context.getObjCEncodingForType(ivarTy, TypeStr, IVD, true);
        Context.getObjCEncodingForMethodParameter(Decl::OBJC_TQ_None, ivarTy, TypeStr, true);
        ivarBuilder.add(MakeConstantString(TypeStr));
        // int *offset;
        uint64_t BaseOffset = ComputeIvarBaseOffset(CGM, OID, IVD);
        uint64_t Offset = BaseOffset - superInstanceSize;
        llvm::Constant *OffsetValue = llvm::ConstantInt::get(IntTy, Offset);
        std::string OffsetName = GetIVarOffsetVariableName(classDecl, IVD);
        llvm::GlobalVariable *OffsetVar = TheModule.getGlobalVariable(OffsetName);
        if (OffsetVar)
          OffsetVar->setInitializer(OffsetValue);
        else
          OffsetVar = new llvm::GlobalVariable(TheModule, IntTy,
            false, llvm::GlobalValue::ExternalLinkage,
            OffsetValue, OffsetName);
        auto ivarVisibility =
            (IVD->getAccessControl() == ObjCIvarDecl::Private ||
             IVD->getAccessControl() == ObjCIvarDecl::Package ||
             classDecl->getVisibility() == HiddenVisibility) ?
                    llvm::GlobalValue::HiddenVisibility :
                    llvm::GlobalValue::DefaultVisibility;
        OffsetVar->setVisibility(ivarVisibility);
        if (ivarVisibility != llvm::GlobalValue::HiddenVisibility)
          CGM.setGVProperties(OffsetVar, OID->getClassInterface());
        ivarBuilder.add(OffsetVar);
        // Ivar size
        ivarBuilder.addInt(Int32Ty,
            CGM.getContext().getTypeSizeInChars(ivarTy).getQuantity());
        // Alignment will be stored as a base-2 log of the alignment.
        unsigned align =
            llvm::Log2_32(Context.getTypeAlignInChars(ivarTy).getQuantity());
        // Objects that require more than 2^64-byte alignment should be impossible!
        assert(align < 64);
        // uint32_t flags;
        // Bits 0-1 are ownership.
        // Bit 2 indicates an extended type encoding
        // Bits 3-8 contain log2(aligment)
        ivarBuilder.addInt(Int32Ty,
            (align << 3) | (1<<2) |
            FlagsForOwnership(ivarTy.getQualifiers().getObjCLifetime()));
        ivarBuilder.finishAndAddTo(ivarArrayBuilder);
      }
      ivarArrayBuilder.finishAndAddTo(ivarListBuilder);
      auto ivarList = ivarListBuilder.finishAndCreateGlobal(".objc_ivar_list",
          CGM.getPointerAlign(), /*constant*/ false,
          llvm::GlobalValue::PrivateLinkage);
      classFields.add(ivarList);
    }
    // struct objc_method_list *methods
    SmallVector<const ObjCMethodDecl*, 16> InstanceMethods;
    InstanceMethods.insert(InstanceMethods.begin(), OID->instmeth_begin(),
        OID->instmeth_end());
    for (auto *propImpl : OID->property_impls())
      if (propImpl->getPropertyImplementation() ==
          ObjCPropertyImplDecl::Synthesize) {
        auto addIfExists = [&](const ObjCMethodDecl *OMD) {
          if (OMD && OMD->hasBody())
            InstanceMethods.push_back(OMD);
        };
        addIfExists(propImpl->getGetterMethodDecl());
        addIfExists(propImpl->getSetterMethodDecl());
      }

    if (InstanceMethods.size() == 0)
      classFields.addNullPointer(PtrTy);
    else
      classFields.add(
          GenerateMethodList(className, "", InstanceMethods, false));

    // void *dtable;
    classFields.addNullPointer(PtrTy);
    // IMP cxx_construct;
    classFields.addNullPointer(PtrTy);
    // IMP cxx_destruct;
    classFields.addNullPointer(PtrTy);
    // struct objc_class *subclass_list
    classFields.addNullPointer(PtrTy);
    // struct objc_class *sibling_class
    classFields.addNullPointer(PtrTy);
    // struct objc_protocol_list *protocols;
    auto RuntimeProtocols = GetRuntimeProtocolList(classDecl->protocol_begin(),
                                                   classDecl->protocol_end());
    SmallVector<llvm::Constant *, 16> Protocols;
    for (const auto *I : RuntimeProtocols)
      Protocols.push_back(GenerateProtocolRef(I));

    if (Protocols.empty())
      classFields.addNullPointer(PtrTy);
    else
      classFields.add(GenerateProtocolList(Protocols));
    // struct reference_list *extra_data;
    classFields.addNullPointer(PtrTy);
    // long abi_version;
    classFields.addInt(LongTy, 0);
    // struct objc_property_list *properties
    classFields.add(GeneratePropertyList(OID, classDecl));

    llvm::GlobalVariable *classStruct =
      classFields.finishAndCreateGlobal(SymbolForClass(className),
        CGM.getPointerAlign(), false, llvm::GlobalValue::ExternalLinkage);

    auto *classRefSymbol = GetClassVar(className);
    classRefSymbol->setSection(sectionName<ClassReferenceSection>());
    classRefSymbol->setInitializer(classStruct);

    if (IsCOFF) {
      // we can't import a class struct.
      if (OID->getClassInterface()->hasAttr<DLLExportAttr>()) {
        classStruct->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
        cast<llvm::GlobalValue>(classRefSymbol)->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
      }

      if (SuperClass) {
        std::pair<llvm::GlobalVariable*, int> v{classStruct, 1};
        EarlyInitList.emplace_back(std::string(SuperClass->getName()),
                                   std::move(v));
      }

    }


    // Resolve the class aliases, if they exist.
    // FIXME: Class pointer aliases shouldn't exist!
    if (ClassPtrAlias) {
      ClassPtrAlias->replaceAllUsesWith(classStruct);
      ClassPtrAlias->eraseFromParent();
      ClassPtrAlias = nullptr;
    }
    if (auto Placeholder =
        TheModule.getNamedGlobal(SymbolForClass(className)))
      if (Placeholder != classStruct) {
        Placeholder->replaceAllUsesWith(classStruct);
        Placeholder->eraseFromParent();
        classStruct->setName(SymbolForClass(className));
      }
    if (MetaClassPtrAlias) {
      MetaClassPtrAlias->replaceAllUsesWith(metaclass);
      MetaClassPtrAlias->eraseFromParent();
      MetaClassPtrAlias = nullptr;
    }
    assert(classStruct->getName() == SymbolForClass(className));

    auto classInitRef = new llvm::GlobalVariable(TheModule,
        classStruct->getType(), false, llvm::GlobalValue::ExternalLinkage,
        classStruct, ManglePublicSymbol("OBJC_INIT_CLASS_") + className);
    classInitRef->setSection(sectionName<ClassSection>());
    CGM.addUsedGlobal(classInitRef);

    EmittedClass = true;
  }
  public:
    CGObjCGNUstep2(CodeGenModule &Mod) : CGObjCGNUstep(Mod, 10, 4, 2) {
      MsgLookupSuperFn.init(&CGM, "objc_msg_lookup_super", IMPTy,
                            PtrToObjCSuperTy, SelectorTy);
      SentInitializeFn.init(&CGM, "objc_send_initialize",
                            llvm::Type::getVoidTy(VMContext), IdTy);
      // struct objc_property
      // {
      //   const char *name;
      //   const char *attributes;
      //   const char *type;
      //   SEL getter;
      //   SEL setter;
      // }
      PropertyMetadataTy =
        llvm::StructType::get(CGM.getLLVMContext(),
            { PtrToInt8Ty, PtrToInt8Ty, PtrToInt8Ty, PtrToInt8Ty, PtrToInt8Ty });
    }

    void GenerateDirectMethodPrologue(CodeGenFunction &CGF, llvm::Function *Fn,
                                      const ObjCMethodDecl *OMD,
                                      const ObjCContainerDecl *CD) override {
      auto &Builder = CGF.Builder;
      bool ReceiverCanBeNull = true;
      auto selfAddr = CGF.GetAddrOfLocalVar(OMD->getSelfDecl());
      auto selfValue = Builder.CreateLoad(selfAddr);

      // Generate:
      //
      // /* unless the receiver is never NULL */
      // if (self == nil) {
      //     return (ReturnType){ };
      // }
      //
      // /* for class methods only to force class lazy initialization */
      // if (!__objc_{class}_initialized)
      // {
      //   objc_send_initialize(class);
      //   __objc_{class}_initialized = 1;
      // }
      //
      // _cmd = @selector(...)
      // ...

      if (OMD->isClassMethod()) {
        const ObjCInterfaceDecl *OID = cast<ObjCInterfaceDecl>(CD);

        // Nullable `Class` expressions cannot be messaged with a direct method
        // so the only reason why the receive can be null would be because
        // of weak linking.
        ReceiverCanBeNull = isWeakLinkedClass(OID);
      }

      llvm::MDBuilder MDHelper(CGM.getLLVMContext());
      if (ReceiverCanBeNull) {
        llvm::BasicBlock *SelfIsNilBlock =
            CGF.createBasicBlock("objc_direct_method.self_is_nil");
        llvm::BasicBlock *ContBlock =
            CGF.createBasicBlock("objc_direct_method.cont");

        // if (self == nil) {
        auto selfTy = cast<llvm::PointerType>(selfValue->getType());
        auto Zero = llvm::ConstantPointerNull::get(selfTy);

        Builder.CreateCondBr(Builder.CreateICmpEQ(selfValue, Zero),
                             SelfIsNilBlock, ContBlock,
                             MDHelper.createUnlikelyBranchWeights());

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

      if (OMD->isClassMethod()) {
        // Prefix of the class type.
        auto *classStart =
            llvm::StructType::get(PtrTy, PtrTy, PtrTy, LongTy, LongTy);
        auto &astContext = CGM.getContext();
        auto flags = Builder.CreateLoad(
            Address{Builder.CreateStructGEP(classStart, selfValue, 4), LongTy,
                    CharUnits::fromQuantity(
                        astContext.getTypeAlign(astContext.UnsignedLongTy))});
        auto isInitialized =
            Builder.CreateAnd(flags, ClassFlags::ClassFlagInitialized);
        llvm::BasicBlock *notInitializedBlock =
            CGF.createBasicBlock("objc_direct_method.class_uninitialized");
        llvm::BasicBlock *initializedBlock =
            CGF.createBasicBlock("objc_direct_method.class_initialized");
        Builder.CreateCondBr(Builder.CreateICmpEQ(isInitialized, Zeros[0]),
                             notInitializedBlock, initializedBlock,
                             MDHelper.createUnlikelyBranchWeights());
        CGF.EmitBlock(notInitializedBlock);
        Builder.SetInsertPoint(notInitializedBlock);
        CGF.EmitRuntimeCall(SentInitializeFn, selfValue);
        Builder.CreateBr(initializedBlock);
        CGF.EmitBlock(initializedBlock);
        Builder.SetInsertPoint(initializedBlock);
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
};

const char *const CGObjCGNUstep2::SectionsBaseNames[8] =
{
"__objc_selectors",
"__objc_classes",
"__objc_class_refs",
"__objc_cats",
"__objc_protocols",
"__objc_protocol_refs",
"__objc_class_aliases",
"__objc_constant_string"
};

const char *const CGObjCGNUstep2::PECOFFSectionsBaseNames[8] =
{
".objcrt$SEL",
".objcrt$CLS",
".objcrt$CLR",
".objcrt$CAT",
".objcrt$PCL",
".objcrt$PCR",
".objcrt$CAL",
".objcrt$STR"
};

/// Support for the ObjFW runtime.
class CGObjCObjFW: public CGObjCGNU {
protected:
  /// The GCC ABI message lookup function.  Returns an IMP pointing to the
  /// method implementation for this message.
  LazyRuntimeFunction MsgLookupFn;
  /// stret lookup function.  While this does not seem to make sense at the
  /// first look, this is required to call the correct forwarding function.
  LazyRuntimeFunction MsgLookupFnSRet;
  /// The GCC ABI superclass message lookup function.  Takes a pointer to a
  /// structure describing the receiver and the class, and a selector as
  /// arguments.  Returns the IMP for the corresponding method.
  LazyRuntimeFunction MsgLookupSuperFn, MsgLookupSuperFnSRet;

  llvm::Value *LookupIMP(CodeGenFunction &CGF, llvm::Value *&Receiver,
                         llvm::Value *cmd, llvm::MDNode *node,
                         MessageSendInfo &MSI) override {
    CGBuilderTy &Builder = CGF.Builder;
    llvm::Value *args[] = {
            EnforceType(Builder, Receiver, IdTy),
            EnforceType(Builder, cmd, SelectorTy) };

    llvm::CallBase *imp;
    if (CGM.ReturnTypeUsesSRet(MSI.CallInfo))
      imp = CGF.EmitRuntimeCallOrInvoke(MsgLookupFnSRet, args);
    else
      imp = CGF.EmitRuntimeCallOrInvoke(MsgLookupFn, args);

    imp->setMetadata(msgSendMDKind, node);
    return imp;
  }

  llvm::Value *LookupIMPSuper(CodeGenFunction &CGF, Address ObjCSuper,
                              llvm::Value *cmd, MessageSendInfo &MSI) override {
    CGBuilderTy &Builder = CGF.Builder;
    llvm::Value *lookupArgs[] = {
        EnforceType(Builder, ObjCSuper.emitRawPointer(CGF), PtrToObjCSuperTy),
        cmd,
    };

    if (CGM.ReturnTypeUsesSRet(MSI.CallInfo))
      return CGF.EmitNounwindRuntimeCall(MsgLookupSuperFnSRet, lookupArgs);
    else
      return CGF.EmitNounwindRuntimeCall(MsgLookupSuperFn, lookupArgs);
  }

  llvm::Value *GetClassNamed(CodeGenFunction &CGF, const std::string &Name,
                             bool isWeak) override {
    if (isWeak)
      return CGObjCGNU::GetClassNamed(CGF, Name, isWeak);

    EmitClassRef(Name);
    std::string SymbolName = "_OBJC_CLASS_" + Name;
    llvm::GlobalVariable *ClassSymbol = TheModule.getGlobalVariable(SymbolName);
    if (!ClassSymbol)
      ClassSymbol = new llvm::GlobalVariable(TheModule, LongTy, false,
                                             llvm::GlobalValue::ExternalLinkage,
                                             nullptr, SymbolName);
    return ClassSymbol;
  }

public:
  CGObjCObjFW(CodeGenModule &Mod): CGObjCGNU(Mod, 9, 3) {
    // IMP objc_msg_lookup(id, SEL);
    MsgLookupFn.init(&CGM, "objc_msg_lookup", IMPTy, IdTy, SelectorTy);
    MsgLookupFnSRet.init(&CGM, "objc_msg_lookup_stret", IMPTy, IdTy,
                         SelectorTy);
    // IMP objc_msg_lookup_super(struct objc_super*, SEL);
    MsgLookupSuperFn.init(&CGM, "objc_msg_lookup_super", IMPTy,
                          PtrToObjCSuperTy, SelectorTy);
    MsgLookupSuperFnSRet.init(&CGM, "objc_msg_lookup_super_stret", IMPTy,
                              PtrToObjCSuperTy, SelectorTy);
  }
};
} // end anonymous namespace

/// Emits a reference to a dummy variable which is emitted with each class.
/// This ensures that a linker error will be generated when trying to link
/// together modules where a referenced class is not defined.
void CGObjCGNU::EmitClassRef(const std::string &className) {
  std::string symbolRef = "__objc_class_ref_" + className;
  // Don't emit two copies of the same symbol
  if (TheModule.getGlobalVariable(symbolRef))
    return;
  std::string symbolName = "__objc_class_name_" + className;
  llvm::GlobalVariable *ClassSymbol = TheModule.getGlobalVariable(symbolName);
  if (!ClassSymbol) {
    ClassSymbol = new llvm::GlobalVariable(TheModule, LongTy, false,
                                           llvm::GlobalValue::ExternalLinkage,
                                           nullptr, symbolName);
  }
  new llvm::GlobalVariable(TheModule, ClassSymbol->getType(), true,
    llvm::GlobalValue::WeakAnyLinkage, ClassSymbol, symbolRef);
}

CGObjCGNU::CGObjCGNU(CodeGenModule &cgm, unsigned runtimeABIVersion,
                     unsigned protocolClassVersion, unsigned classABI)
  : CGObjCRuntime(cgm), TheModule(CGM.getModule()),
    VMContext(cgm.getLLVMContext()), ClassPtrAlias(nullptr),
    MetaClassPtrAlias(nullptr), RuntimeVersion(runtimeABIVersion),
    ProtocolVersion(protocolClassVersion), ClassABIVersion(classABI) {

  msgSendMDKind = VMContext.getMDKindID("GNUObjCMessageSend");
  usesSEHExceptions =
      cgm.getContext().getTargetInfo().getTriple().isWindowsMSVCEnvironment();
  usesCxxExceptions =
      cgm.getContext().getTargetInfo().getTriple().isOSCygMing() &&
      isRuntime(ObjCRuntime::GNUstep, 2);

  CodeGenTypes &Types = CGM.getTypes();
  IntTy = cast<llvm::IntegerType>(
      Types.ConvertType(CGM.getContext().IntTy));
  LongTy = cast<llvm::IntegerType>(
      Types.ConvertType(CGM.getContext().LongTy));
  SizeTy = cast<llvm::IntegerType>(
      Types.ConvertType(CGM.getContext().getSizeType()));
  PtrDiffTy = cast<llvm::IntegerType>(
      Types.ConvertType(CGM.getContext().getPointerDiffType()));
  BoolTy = CGM.getTypes().ConvertType(CGM.getContext().BoolTy);

  Int8Ty = llvm::Type::getInt8Ty(VMContext);
  // C string type.  Used in lots of places.
  PtrToInt8Ty = llvm::PointerType::getUnqual(Int8Ty);
  ProtocolPtrTy = llvm::PointerType::getUnqual(
      Types.ConvertType(CGM.getContext().getObjCProtoType()));

  Zeros[0] = llvm::ConstantInt::get(LongTy, 0);
  Zeros[1] = Zeros[0];
  NULLPtr = llvm::ConstantPointerNull::get(PtrToInt8Ty);
  // Get the selector Type.
  QualType selTy = CGM.getContext().getObjCSelType();
  if (QualType() == selTy) {
    SelectorTy = PtrToInt8Ty;
    SelectorElemTy = Int8Ty;
  } else {
    SelectorTy = cast<llvm::PointerType>(CGM.getTypes().ConvertType(selTy));
    SelectorElemTy = CGM.getTypes().ConvertTypeForMem(selTy->getPointeeType());
  }

  PtrToIntTy = llvm::PointerType::getUnqual(IntTy);
  PtrTy = PtrToInt8Ty;

  Int32Ty = llvm::Type::getInt32Ty(VMContext);
  Int64Ty = llvm::Type::getInt64Ty(VMContext);

  IntPtrTy =
      CGM.getDataLayout().getPointerSizeInBits() == 32 ? Int32Ty : Int64Ty;

  // Object type
  QualType UnqualIdTy = CGM.getContext().getObjCIdType();
  ASTIdTy = CanQualType();
  if (UnqualIdTy != QualType()) {
    ASTIdTy = CGM.getContext().getCanonicalType(UnqualIdTy);
    IdTy = cast<llvm::PointerType>(CGM.getTypes().ConvertType(ASTIdTy));
    IdElemTy = CGM.getTypes().ConvertTypeForMem(
        ASTIdTy.getTypePtr()->getPointeeType());
  } else {
    IdTy = PtrToInt8Ty;
    IdElemTy = Int8Ty;
  }
  PtrToIdTy = llvm::PointerType::getUnqual(IdTy);
  ProtocolTy = llvm::StructType::get(IdTy,
      PtrToInt8Ty, // name
      PtrToInt8Ty, // protocols
      PtrToInt8Ty, // instance methods
      PtrToInt8Ty, // class methods
      PtrToInt8Ty, // optional instance methods
      PtrToInt8Ty, // optional class methods
      PtrToInt8Ty, // properties
      PtrToInt8Ty);// optional properties

  // struct objc_property_gsv1
  // {
  //   const char *name;
  //   char attributes;
  //   char attributes2;
  //   char unused1;
  //   char unused2;
  //   const char *getter_name;
  //   const char *getter_types;
  //   const char *setter_name;
  //   const char *setter_types;
  // }
  PropertyMetadataTy = llvm::StructType::get(CGM.getLLVMContext(), {
      PtrToInt8Ty, Int8Ty, Int8Ty, Int8Ty, Int8Ty, PtrToInt8Ty, PtrToInt8Ty,
      PtrToInt8Ty, PtrToInt8Ty });

  ObjCSuperTy = llvm::StructType::get(IdTy, IdTy);
  PtrToObjCSuperTy = llvm::PointerType::getUnqual(ObjCSuperTy);

  llvm::Type *VoidTy = llvm::Type::getVoidTy(VMContext);

  // void objc_exception_throw(id);
  ExceptionThrowFn.init(&CGM, "objc_exception_throw", VoidTy, IdTy);
  ExceptionReThrowFn.init(&CGM,
                          usesCxxExceptions ? "objc_exception_rethrow"
                                            : "objc_exception_throw",
                          VoidTy, IdTy);
  // int objc_sync_enter(id);
  SyncEnterFn.init(&CGM, "objc_sync_enter", IntTy, IdTy);
  // int objc_sync_exit(id);
  SyncExitFn.init(&CGM, "objc_sync_exit", IntTy, IdTy);

  // void objc_enumerationMutation (id)
  EnumerationMutationFn.init(&CGM, "objc_enumerationMutation", VoidTy, IdTy);

  // id objc_getProperty(id, SEL, ptrdiff_t, BOOL)
  GetPropertyFn.init(&CGM, "objc_getProperty", IdTy, IdTy, SelectorTy,
                     PtrDiffTy, BoolTy);
  // void objc_setProperty(id, SEL, ptrdiff_t, id, BOOL, BOOL)
  SetPropertyFn.init(&CGM, "objc_setProperty", VoidTy, IdTy, SelectorTy,
                     PtrDiffTy, IdTy, BoolTy, BoolTy);
  // void objc_setPropertyStruct(void*, void*, ptrdiff_t, BOOL, BOOL)
  GetStructPropertyFn.init(&CGM, "objc_getPropertyStruct", VoidTy, PtrTy, PtrTy,
                           PtrDiffTy, BoolTy, BoolTy);
  // void objc_setPropertyStruct(void*, void*, ptrdiff_t, BOOL, BOOL)
  SetStructPropertyFn.init(&CGM, "objc_setPropertyStruct", VoidTy, PtrTy, PtrTy,
                           PtrDiffTy, BoolTy, BoolTy);

  // IMP type
  llvm::Type *IMPArgs[] = { IdTy, SelectorTy };
  IMPTy = llvm::PointerType::getUnqual(llvm::FunctionType::get(IdTy, IMPArgs,
              true));

  const LangOptions &Opts = CGM.getLangOpts();
  if ((Opts.getGC() != LangOptions::NonGC) || Opts.ObjCAutoRefCount)
    RuntimeVersion = 10;

  // Don't bother initialising the GC stuff unless we're compiling in GC mode
  if (Opts.getGC() != LangOptions::NonGC) {
    // This is a bit of an hack.  We should sort this out by having a proper
    // CGObjCGNUstep subclass for GC, but we may want to really support the old
    // ABI and GC added in ObjectiveC2.framework, so we fudge it a bit for now
    // Get selectors needed in GC mode
    RetainSel = GetNullarySelector("retain", CGM.getContext());
    ReleaseSel = GetNullarySelector("release", CGM.getContext());
    AutoreleaseSel = GetNullarySelector("autorelease", CGM.getContext());

    // Get functions needed in GC mode

    // id objc_assign_ivar(id, id, ptrdiff_t);
    IvarAssignFn.init(&CGM, "objc_assign_ivar", IdTy, IdTy, IdTy, PtrDiffTy);
    // id objc_assign_strongCast (id, id*)
    StrongCastAssignFn.init(&CGM, "objc_assign_strongCast", IdTy, IdTy,
                            PtrToIdTy);
    // id objc_assign_global(id, id*);
    GlobalAssignFn.init(&CGM, "objc_assign_global", IdTy, IdTy, PtrToIdTy);
    // id objc_assign_weak(id, id*);
    WeakAssignFn.init(&CGM, "objc_assign_weak", IdTy, IdTy, PtrToIdTy);
    // id objc_read_weak(id*);
    WeakReadFn.init(&CGM, "objc_read_weak", IdTy, PtrToIdTy);
    // void *objc_memmove_collectable(void*, void *, size_t);
    MemMoveFn.init(&CGM, "objc_memmove_collectable", PtrTy, PtrTy, PtrTy,
                   SizeTy);
  }
}

llvm::Value *CGObjCGNU::GetClassNamed(CodeGenFunction &CGF,
                                      const std::string &Name, bool isWeak) {
  llvm::Constant *ClassName = MakeConstantString(Name);
  // With the incompatible ABI, this will need to be replaced with a direct
  // reference to the class symbol.  For the compatible nonfragile ABI we are
  // still performing this lookup at run time but emitting the symbol for the
  // class externally so that we can make the switch later.
  //
  // Libobjc2 contains an LLVM pass that replaces calls to objc_lookup_class
  // with memoized versions or with static references if it's safe to do so.
  if (!isWeak)
    EmitClassRef(Name);

  llvm::FunctionCallee ClassLookupFn = CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(IdTy, PtrToInt8Ty, true), "objc_lookup_class");
  return CGF.EmitNounwindRuntimeCall(ClassLookupFn, ClassName);
}

// This has to perform the lookup every time, since posing and related
// techniques can modify the name -> class mapping.
llvm::Value *CGObjCGNU::GetClass(CodeGenFunction &CGF,
                                 const ObjCInterfaceDecl *OID) {
  auto *Value =
      GetClassNamed(CGF, OID->getNameAsString(), OID->isWeakImported());
  if (auto *ClassSymbol = dyn_cast<llvm::GlobalVariable>(Value))
    CGM.setGVProperties(ClassSymbol, OID);
  return Value;
}

llvm::Value *CGObjCGNU::EmitNSAutoreleasePoolClassRef(CodeGenFunction &CGF) {
  auto *Value  = GetClassNamed(CGF, "NSAutoreleasePool", false);
  if (CGM.getTriple().isOSBinFormatCOFF()) {
    if (auto *ClassSymbol = dyn_cast<llvm::GlobalVariable>(Value)) {
      IdentifierInfo &II = CGF.CGM.getContext().Idents.get("NSAutoreleasePool");
      TranslationUnitDecl *TUDecl = CGM.getContext().getTranslationUnitDecl();
      DeclContext *DC = TranslationUnitDecl::castToDeclContext(TUDecl);

      const VarDecl *VD = nullptr;
      for (const auto *Result : DC->lookup(&II))
        if ((VD = dyn_cast<VarDecl>(Result)))
          break;

      CGM.setGVProperties(ClassSymbol, VD);
    }
  }
  return Value;
}

llvm::Value *CGObjCGNU::GetTypedSelector(CodeGenFunction &CGF, Selector Sel,
                                         const std::string &TypeEncoding) {
  SmallVectorImpl<TypedSelector> &Types = SelectorTable[Sel];
  llvm::GlobalAlias *SelValue = nullptr;

  for (SmallVectorImpl<TypedSelector>::iterator i = Types.begin(),
      e = Types.end() ; i!=e ; i++) {
    if (i->first == TypeEncoding) {
      SelValue = i->second;
      break;
    }
  }
  if (!SelValue) {
    SelValue = llvm::GlobalAlias::create(SelectorElemTy, 0,
                                         llvm::GlobalValue::PrivateLinkage,
                                         ".objc_selector_" + Sel.getAsString(),
                                         &TheModule);
    Types.emplace_back(TypeEncoding, SelValue);
  }

  return SelValue;
}

Address CGObjCGNU::GetAddrOfSelector(CodeGenFunction &CGF, Selector Sel) {
  llvm::Value *SelValue = GetSelector(CGF, Sel);

  // Store it to a temporary.  Does this satisfy the semantics of
  // GetAddrOfSelector?  Hopefully.
  Address tmp = CGF.CreateTempAlloca(SelValue->getType(),
                                     CGF.getPointerAlign());
  CGF.Builder.CreateStore(SelValue, tmp);
  return tmp;
}

llvm::Value *CGObjCGNU::GetSelector(CodeGenFunction &CGF, Selector Sel) {
  return GetTypedSelector(CGF, Sel, std::string());
}

llvm::Value *CGObjCGNU::GetSelector(CodeGenFunction &CGF,
                                    const ObjCMethodDecl *Method) {
  std::string SelTypes = CGM.getContext().getObjCEncodingForMethodDecl(Method);
  return GetTypedSelector(CGF, Method->getSelector(), SelTypes);
}

llvm::Constant *CGObjCGNU::GetEHType(QualType T) {
  if (T->isObjCIdType() || T->isObjCQualifiedIdType()) {
    // With the old ABI, there was only one kind of catchall, which broke
    // foreign exceptions.  With the new ABI, we use __objc_id_typeinfo as
    // a pointer indicating object catchalls, and NULL to indicate real
    // catchalls
    if (CGM.getLangOpts().ObjCRuntime.isNonFragile()) {
      return MakeConstantString("@id");
    } else {
      return nullptr;
    }
  }

  // All other types should be Objective-C interface pointer types.
  const ObjCObjectPointerType *OPT = T->getAs<ObjCObjectPointerType>();
  assert(OPT && "Invalid @catch type.");
  const ObjCInterfaceDecl *IDecl = OPT->getObjectType()->getInterface();
  assert(IDecl && "Invalid @catch type.");
  return MakeConstantString(IDecl->getIdentifier()->getName());
}

llvm::Constant *CGObjCGNUstep::GetEHType(QualType T) {
  if (usesSEHExceptions)
    return CGM.getCXXABI().getAddrOfRTTIDescriptor(T);

  if (!CGM.getLangOpts().CPlusPlus && !usesCxxExceptions)
    return CGObjCGNU::GetEHType(T);

  // For Objective-C++, we want to provide the ability to catch both C++ and
  // Objective-C objects in the same function.

  // There's a particular fixed type info for 'id'.
  if (T->isObjCIdType() ||
      T->isObjCQualifiedIdType()) {
    llvm::Constant *IDEHType =
      CGM.getModule().getGlobalVariable("__objc_id_type_info");
    if (!IDEHType)
      IDEHType =
        new llvm::GlobalVariable(CGM.getModule(), PtrToInt8Ty,
                                 false,
                                 llvm::GlobalValue::ExternalLinkage,
                                 nullptr, "__objc_id_type_info");
    return IDEHType;
  }

  const ObjCObjectPointerType *PT =
    T->getAs<ObjCObjectPointerType>();
  assert(PT && "Invalid @catch type.");
  const ObjCInterfaceType *IT = PT->getInterfaceType();
  assert(IT && "Invalid @catch type.");
  std::string className =
      std::string(IT->getDecl()->getIdentifier()->getName());

  std::string typeinfoName = "__objc_eh_typeinfo_" + className;

  // Return the existing typeinfo if it exists
  if (llvm::Constant *typeinfo = TheModule.getGlobalVariable(typeinfoName))
    return typeinfo;

  // Otherwise create it.

  // vtable for gnustep::libobjc::__objc_class_type_info
  // It's quite ugly hard-coding this.  Ideally we'd generate it using the host
  // platform's name mangling.
  const char *vtableName = "_ZTVN7gnustep7libobjc22__objc_class_type_infoE";
  auto *Vtable = TheModule.getGlobalVariable(vtableName);
  if (!Vtable) {
    Vtable = new llvm::GlobalVariable(TheModule, PtrToInt8Ty, true,
                                      llvm::GlobalValue::ExternalLinkage,
                                      nullptr, vtableName);
  }
  llvm::Constant *Two = llvm::ConstantInt::get(IntTy, 2);
  auto *BVtable =
      llvm::ConstantExpr::getGetElementPtr(Vtable->getValueType(), Vtable, Two);

  llvm::Constant *typeName =
    ExportUniqueString(className, "__objc_eh_typename_");

  ConstantInitBuilder builder(CGM);
  auto fields = builder.beginStruct();
  fields.add(BVtable);
  fields.add(typeName);
  llvm::Constant *TI =
    fields.finishAndCreateGlobal("__objc_eh_typeinfo_" + className,
                                 CGM.getPointerAlign(),
                                 /*constant*/ false,
                                 llvm::GlobalValue::LinkOnceODRLinkage);
  return TI;
}

/// Generate an NSConstantString object.
ConstantAddress CGObjCGNU::GenerateConstantString(const StringLiteral *SL) {

  std::string Str = SL->getString().str();
  CharUnits Align = CGM.getPointerAlign();

  // Look for an existing one
  llvm::StringMap<llvm::Constant*>::iterator old = ObjCStrings.find(Str);
  if (old != ObjCStrings.end())
    return ConstantAddress(old->getValue(), Int8Ty, Align);

  StringRef StringClass = CGM.getLangOpts().ObjCConstantStringClass;

  if (StringClass.empty()) StringClass = "NSConstantString";

  std::string Sym = "_OBJC_CLASS_";
  Sym += StringClass;

  llvm::Constant *isa = TheModule.getNamedGlobal(Sym);

  if (!isa)
    isa = new llvm::GlobalVariable(TheModule, IdTy, /* isConstant */ false,
                                   llvm::GlobalValue::ExternalWeakLinkage,
                                   nullptr, Sym);

  ConstantInitBuilder Builder(CGM);
  auto Fields = Builder.beginStruct();
  Fields.add(isa);
  Fields.add(MakeConstantString(Str));
  Fields.addInt(IntTy, Str.size());
  llvm::Constant *ObjCStr = Fields.finishAndCreateGlobal(".objc_str", Align);
  ObjCStrings[Str] = ObjCStr;
  ConstantStrings.push_back(ObjCStr);
  return ConstantAddress(ObjCStr, Int8Ty, Align);
}

///Generates a message send where the super is the receiver.  This is a message
///send to self with special delivery semantics indicating which class's method
///should be called.
RValue
CGObjCGNU::GenerateMessageSendSuper(CodeGenFunction &CGF,
                                    ReturnValueSlot Return,
                                    QualType ResultType,
                                    Selector Sel,
                                    const ObjCInterfaceDecl *Class,
                                    bool isCategoryImpl,
                                    llvm::Value *Receiver,
                                    bool IsClassMessage,
                                    const CallArgList &CallArgs,
                                    const ObjCMethodDecl *Method) {
  CGBuilderTy &Builder = CGF.Builder;
  if (CGM.getLangOpts().getGC() == LangOptions::GCOnly) {
    if (Sel == RetainSel || Sel == AutoreleaseSel) {
      return RValue::get(EnforceType(Builder, Receiver,
                  CGM.getTypes().ConvertType(ResultType)));
    }
    if (Sel == ReleaseSel) {
      return RValue::get(nullptr);
    }
  }

  llvm::Value *cmd = GetSelector(CGF, Sel);
  CallArgList ActualArgs;

  ActualArgs.add(RValue::get(EnforceType(Builder, Receiver, IdTy)), ASTIdTy);
  ActualArgs.add(RValue::get(cmd), CGF.getContext().getObjCSelType());
  ActualArgs.addFrom(CallArgs);

  MessageSendInfo MSI = getMessageSendInfo(Method, ResultType, ActualArgs);

  llvm::Value *ReceiverClass = nullptr;
  bool isV2ABI = isRuntime(ObjCRuntime::GNUstep, 2);
  if (isV2ABI) {
    ReceiverClass = GetClassNamed(CGF,
        Class->getSuperClass()->getNameAsString(), /*isWeak*/false);
    if (IsClassMessage)  {
      // Load the isa pointer of the superclass is this is a class method.
      ReceiverClass = Builder.CreateBitCast(ReceiverClass,
                                            llvm::PointerType::getUnqual(IdTy));
      ReceiverClass =
        Builder.CreateAlignedLoad(IdTy, ReceiverClass, CGF.getPointerAlign());
    }
    ReceiverClass = EnforceType(Builder, ReceiverClass, IdTy);
  } else {
    if (isCategoryImpl) {
      llvm::FunctionCallee classLookupFunction = nullptr;
      if (IsClassMessage)  {
        classLookupFunction = CGM.CreateRuntimeFunction(llvm::FunctionType::get(
              IdTy, PtrTy, true), "objc_get_meta_class");
      } else {
        classLookupFunction = CGM.CreateRuntimeFunction(llvm::FunctionType::get(
              IdTy, PtrTy, true), "objc_get_class");
      }
      ReceiverClass = Builder.CreateCall(classLookupFunction,
          MakeConstantString(Class->getNameAsString()));
    } else {
      // Set up global aliases for the metaclass or class pointer if they do not
      // already exist.  These will are forward-references which will be set to
      // pointers to the class and metaclass structure created for the runtime
      // load function.  To send a message to super, we look up the value of the
      // super_class pointer from either the class or metaclass structure.
      if (IsClassMessage)  {
        if (!MetaClassPtrAlias) {
          MetaClassPtrAlias = llvm::GlobalAlias::create(
              IdElemTy, 0, llvm::GlobalValue::InternalLinkage,
              ".objc_metaclass_ref" + Class->getNameAsString(), &TheModule);
        }
        ReceiverClass = MetaClassPtrAlias;
      } else {
        if (!ClassPtrAlias) {
          ClassPtrAlias = llvm::GlobalAlias::create(
              IdElemTy, 0, llvm::GlobalValue::InternalLinkage,
              ".objc_class_ref" + Class->getNameAsString(), &TheModule);
        }
        ReceiverClass = ClassPtrAlias;
      }
    }
    // Cast the pointer to a simplified version of the class structure
    llvm::Type *CastTy = llvm::StructType::get(IdTy, IdTy);
    ReceiverClass = Builder.CreateBitCast(ReceiverClass,
                                          llvm::PointerType::getUnqual(CastTy));
    // Get the superclass pointer
    ReceiverClass = Builder.CreateStructGEP(CastTy, ReceiverClass, 1);
    // Load the superclass pointer
    ReceiverClass =
      Builder.CreateAlignedLoad(IdTy, ReceiverClass, CGF.getPointerAlign());
  }
  // Construct the structure used to look up the IMP
  llvm::StructType *ObjCSuperTy =
      llvm::StructType::get(Receiver->getType(), IdTy);

  Address ObjCSuper = CGF.CreateTempAlloca(ObjCSuperTy,
                              CGF.getPointerAlign());

  Builder.CreateStore(Receiver, Builder.CreateStructGEP(ObjCSuper, 0));
  Builder.CreateStore(ReceiverClass, Builder.CreateStructGEP(ObjCSuper, 1));

  // Get the IMP
  llvm::Value *imp = LookupIMPSuper(CGF, ObjCSuper, cmd, MSI);
  imp = EnforceType(Builder, imp, MSI.MessengerType);

  llvm::Metadata *impMD[] = {
      llvm::MDString::get(VMContext, Sel.getAsString()),
      llvm::MDString::get(VMContext, Class->getSuperClass()->getNameAsString()),
      llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt1Ty(VMContext), IsClassMessage))};
  llvm::MDNode *node = llvm::MDNode::get(VMContext, impMD);

  CGCallee callee(CGCalleeInfo(), imp);

  llvm::CallBase *call;
  RValue msgRet = CGF.EmitCall(MSI.CallInfo, callee, Return, ActualArgs, &call);
  call->setMetadata(msgSendMDKind, node);
  return msgRet;
}

/// Generate code for a message send expression.
RValue
CGObjCGNU::GenerateMessageSend(CodeGenFunction &CGF,
                               ReturnValueSlot Return,
                               QualType ResultType,
                               Selector Sel,
                               llvm::Value *Receiver,
                               const CallArgList &CallArgs,
                               const ObjCInterfaceDecl *Class,
                               const ObjCMethodDecl *Method) {
  CGBuilderTy &Builder = CGF.Builder;

  // Strip out message sends to retain / release in GC mode
  if (CGM.getLangOpts().getGC() == LangOptions::GCOnly) {
    if (Sel == RetainSel || Sel == AutoreleaseSel) {
      return RValue::get(EnforceType(Builder, Receiver,
                  CGM.getTypes().ConvertType(ResultType)));
    }
    if (Sel == ReleaseSel) {
      return RValue::get(nullptr);
    }
  }

  bool isDirect = Method && Method->isDirectMethod();

  IdTy = cast<llvm::PointerType>(CGM.getTypes().ConvertType(ASTIdTy));
  llvm::Value *cmd;
  if (!isDirect) {
    if (Method)
      cmd = GetSelector(CGF, Method);
    else
      cmd = GetSelector(CGF, Sel);
    cmd = EnforceType(Builder, cmd, SelectorTy);
  }

  Receiver = EnforceType(Builder, Receiver, IdTy);

  llvm::Metadata *impMD[] = {
      llvm::MDString::get(VMContext, Sel.getAsString()),
      llvm::MDString::get(VMContext, Class ? Class->getNameAsString() : ""),
      llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt1Ty(VMContext), Class != nullptr))};
  llvm::MDNode *node = llvm::MDNode::get(VMContext, impMD);

  CallArgList ActualArgs;
  ActualArgs.add(RValue::get(Receiver), ASTIdTy);
  if (!isDirect)
    ActualArgs.add(RValue::get(cmd), CGF.getContext().getObjCSelType());
  ActualArgs.addFrom(CallArgs);

  MessageSendInfo MSI = getMessageSendInfo(Method, ResultType, ActualArgs);

  // Message sends are expected to return a zero value when the
  // receiver is nil.  At one point, this was only guaranteed for
  // simple integer and pointer types, but expectations have grown
  // over time.
  //
  // Given a nil receiver, the GNU runtime's message lookup will
  // return a stub function that simply sets various return-value
  // registers to zero and then returns.  That's good enough for us
  // if and only if (1) the calling conventions of that stub are
  // compatible with the signature we're using and (2) the registers
  // it sets are sufficient to produce a zero value of the return type.
  // Rather than doing a whole target-specific analysis, we assume it
  // only works for void, integer, and pointer types, and in all
  // other cases we do an explicit nil check is emitted code.  In
  // addition to ensuring we produce a zero value for other types, this
  // sidesteps the few outright CC incompatibilities we know about that
  // could otherwise lead to crashes, like when a method is expected to
  // return on the x87 floating point stack or adjust the stack pointer
  // because of an indirect return.
  bool hasParamDestroyedInCallee = false;
  bool requiresExplicitZeroResult = false;
  bool requiresNilReceiverCheck = [&] {
    // We never need a check if we statically know the receiver isn't nil.
    if (!canMessageReceiverBeNull(CGF, Method, /*IsSuper*/ false,
                                  Class, Receiver))
      return false;

    // If there's a consumed argument, we need a nil check.
    if (Method && Method->hasParamDestroyedInCallee()) {
      hasParamDestroyedInCallee = true;
    }

    // If the return value isn't flagged as unused, and the result
    // type isn't in our narrow set where we assume compatibility,
    // we need a nil check to ensure a nil value.
    if (!Return.isUnused()) {
      if (ResultType->isVoidType()) {
        // void results are definitely okay.
      } else if (ResultType->hasPointerRepresentation() &&
                 CGM.getTypes().isZeroInitializable(ResultType)) {
        // Pointer types should be fine as long as they have
        // bitwise-zero null pointers.  But do we need to worry
        // about unusual address spaces?
      } else if (ResultType->isIntegralOrEnumerationType()) {
        // Bitwise zero should always be zero for integral types.
        // FIXME: we probably need a size limit here, but we've
        // never imposed one before
      } else {
        // Otherwise, use an explicit check just to be sure, unless we're
        // calling a direct method, where the implementation does this for us.
        requiresExplicitZeroResult = !isDirect;
      }
    }

    return hasParamDestroyedInCallee || requiresExplicitZeroResult;
  }();

  // We will need to explicitly zero-initialize an aggregate result slot
  // if we generally require explicit zeroing and we have an aggregate
  // result.
  bool requiresExplicitAggZeroing =
    requiresExplicitZeroResult && CGF.hasAggregateEvaluationKind(ResultType);

  // The block we're going to end up in after any message send or nil path.
  llvm::BasicBlock *continueBB = nullptr;
  // The block that eventually branched to continueBB along the nil path.
  llvm::BasicBlock *nilPathBB = nullptr;
  // The block to do explicit work in along the nil path, if necessary.
  llvm::BasicBlock *nilCleanupBB = nullptr;

  // Emit the nil-receiver check.
  if (requiresNilReceiverCheck) {
    llvm::BasicBlock *messageBB = CGF.createBasicBlock("msgSend");
    continueBB = CGF.createBasicBlock("continue");

    // If we need to zero-initialize an aggregate result or destroy
    // consumed arguments, we'll need a separate cleanup block.
    // Otherwise we can just branch directly to the continuation block.
    if (requiresExplicitAggZeroing || hasParamDestroyedInCallee) {
      nilCleanupBB = CGF.createBasicBlock("nilReceiverCleanup");
    } else {
      nilPathBB = Builder.GetInsertBlock();
    }

    llvm::Value *isNil = Builder.CreateICmpEQ(Receiver,
            llvm::Constant::getNullValue(Receiver->getType()));
    Builder.CreateCondBr(isNil, nilCleanupBB ? nilCleanupBB : continueBB,
                         messageBB);
    CGF.EmitBlock(messageBB);
  }

  // Get the IMP to call
  llvm::Value *imp;

  // If this is a direct method, just emit it here.
  if (isDirect)
    imp = GenerateMethod(Method, Method->getClassInterface());
  else
    // If we have non-legacy dispatch specified, we try using the
    // objc_msgSend() functions.  These are not supported on all platforms
    // (or all runtimes on a given platform), so we
    switch (CGM.getCodeGenOpts().getObjCDispatchMethod()) {
    case CodeGenOptions::Legacy:
      imp = LookupIMP(CGF, Receiver, cmd, node, MSI);
      break;
    case CodeGenOptions::Mixed:
    case CodeGenOptions::NonLegacy:
      StringRef name = "objc_msgSend";
      if (CGM.ReturnTypeUsesFPRet(ResultType)) {
        name = "objc_msgSend_fpret";
      } else if (CGM.ReturnTypeUsesSRet(MSI.CallInfo)) {
        name = "objc_msgSend_stret";

        // The address of the memory block is be passed in x8 for POD type,
        // or in x0 for non-POD type (marked as inreg).
        bool shouldCheckForInReg =
            CGM.getContext()
                .getTargetInfo()
                .getTriple()
                .isWindowsMSVCEnvironment() &&
            CGM.getContext().getTargetInfo().getTriple().isAArch64();
        if (shouldCheckForInReg && CGM.ReturnTypeHasInReg(MSI.CallInfo)) {
          name = "objc_msgSend_stret2";
        }
      }
      // The actual types here don't matter - we're going to bitcast the
      // function anyway
      imp = CGM.CreateRuntimeFunction(llvm::FunctionType::get(IdTy, IdTy, true),
                                      name)
                .getCallee();
    }

  // Reset the receiver in case the lookup modified it
  ActualArgs[0] = CallArg(RValue::get(Receiver), ASTIdTy);

  imp = EnforceType(Builder, imp, MSI.MessengerType);

  llvm::CallBase *call;
  CGCallee callee(CGCalleeInfo(), imp);
  RValue msgRet = CGF.EmitCall(MSI.CallInfo, callee, Return, ActualArgs, &call);
  if (!isDirect)
    call->setMetadata(msgSendMDKind, node);

  if (requiresNilReceiverCheck) {
    llvm::BasicBlock *nonNilPathBB = CGF.Builder.GetInsertBlock();
    CGF.Builder.CreateBr(continueBB);

    // Emit the nil path if we decided it was necessary above.
    if (nilCleanupBB) {
      CGF.EmitBlock(nilCleanupBB);

      if (hasParamDestroyedInCallee) {
        destroyCalleeDestroyedArguments(CGF, Method, CallArgs);
      }

      if (requiresExplicitAggZeroing) {
        assert(msgRet.isAggregate());
        Address addr = msgRet.getAggregateAddress();
        CGF.EmitNullInitialization(addr, ResultType);
      }

      nilPathBB = CGF.Builder.GetInsertBlock();
      CGF.Builder.CreateBr(continueBB);
    }

    // Enter the continuation block and emit a phi if required.
    CGF.EmitBlock(continueBB);
    if (msgRet.isScalar()) {
      // If the return type is void, do nothing
      if (llvm::Value *v = msgRet.getScalarVal()) {
        llvm::PHINode *phi = Builder.CreatePHI(v->getType(), 2);
        phi->addIncoming(v, nonNilPathBB);
        phi->addIncoming(CGM.EmitNullConstant(ResultType), nilPathBB);
        msgRet = RValue::get(phi);
      }
    } else if (msgRet.isAggregate()) {
      // Aggregate zeroing is handled in nilCleanupBB when it's required.
    } else /* isComplex() */ {
      std::pair<llvm::Value*,llvm::Value*> v = msgRet.getComplexVal();
      llvm::PHINode *phi = Builder.CreatePHI(v.first->getType(), 2);
      phi->addIncoming(v.first, nonNilPathBB);
      phi->addIncoming(llvm::Constant::getNullValue(v.first->getType()),
                       nilPathBB);
      llvm::PHINode *phi2 = Builder.CreatePHI(v.second->getType(), 2);
      phi2->addIncoming(v.second, nonNilPathBB);
      phi2->addIncoming(llvm::Constant::getNullValue(v.second->getType()),
                        nilPathBB);
      msgRet = RValue::getComplex(phi, phi2);
    }
  }
  return msgRet;
}

/// Generates a MethodList.  Used in construction of a objc_class and
/// objc_category structures.
llvm::Constant *CGObjCGNU::
GenerateMethodList(StringRef ClassName,
                   StringRef CategoryName,
                   ArrayRef<const ObjCMethodDecl*> Methods,
                   bool isClassMethodList) {
  if (Methods.empty())
    return NULLPtr;

  ConstantInitBuilder Builder(CGM);

  auto MethodList = Builder.beginStruct();
  MethodList.addNullPointer(CGM.Int8PtrTy);
  MethodList.addInt(Int32Ty, Methods.size());

  // Get the method structure type.
  llvm::StructType *ObjCMethodTy =
    llvm::StructType::get(CGM.getLLVMContext(), {
      PtrToInt8Ty, // Really a selector, but the runtime creates it us.
      PtrToInt8Ty, // Method types
      IMPTy        // Method pointer
    });
  bool isV2ABI = isRuntime(ObjCRuntime::GNUstep, 2);
  if (isV2ABI) {
    // size_t size;
    llvm::DataLayout td(&TheModule);
    MethodList.addInt(SizeTy, td.getTypeSizeInBits(ObjCMethodTy) /
        CGM.getContext().getCharWidth());
    ObjCMethodTy =
      llvm::StructType::get(CGM.getLLVMContext(), {
        IMPTy,       // Method pointer
        PtrToInt8Ty, // Selector
        PtrToInt8Ty  // Extended type encoding
      });
  } else {
    ObjCMethodTy =
      llvm::StructType::get(CGM.getLLVMContext(), {
        PtrToInt8Ty, // Really a selector, but the runtime creates it us.
        PtrToInt8Ty, // Method types
        IMPTy        // Method pointer
      });
  }
  auto MethodArray = MethodList.beginArray();
  ASTContext &Context = CGM.getContext();
  for (const auto *OMD : Methods) {
    llvm::Constant *FnPtr =
      TheModule.getFunction(getSymbolNameForMethod(OMD));
    assert(FnPtr && "Can't generate metadata for method that doesn't exist");
    auto Method = MethodArray.beginStruct(ObjCMethodTy);
    if (isV2ABI) {
      Method.add(FnPtr);
      Method.add(GetConstantSelector(OMD->getSelector(),
          Context.getObjCEncodingForMethodDecl(OMD)));
      Method.add(MakeConstantString(Context.getObjCEncodingForMethodDecl(OMD, true)));
    } else {
      Method.add(MakeConstantString(OMD->getSelector().getAsString()));
      Method.add(MakeConstantString(Context.getObjCEncodingForMethodDecl(OMD)));
      Method.add(FnPtr);
    }
    Method.finishAndAddTo(MethodArray);
  }
  MethodArray.finishAndAddTo(MethodList);

  // Create an instance of the structure
  return MethodList.finishAndCreateGlobal(".objc_method_list",
                                          CGM.getPointerAlign());
}

/// Generates an IvarList.  Used in construction of a objc_class.
llvm::Constant *CGObjCGNU::
GenerateIvarList(ArrayRef<llvm::Constant *> IvarNames,
                 ArrayRef<llvm::Constant *> IvarTypes,
                 ArrayRef<llvm::Constant *> IvarOffsets,
                 ArrayRef<llvm::Constant *> IvarAlign,
                 ArrayRef<Qualifiers::ObjCLifetime> IvarOwnership) {
  if (IvarNames.empty())
    return NULLPtr;

  ConstantInitBuilder Builder(CGM);

  // Structure containing array count followed by array.
  auto IvarList = Builder.beginStruct();
  IvarList.addInt(IntTy, (int)IvarNames.size());

  // Get the ivar structure type.
  llvm::StructType *ObjCIvarTy =
      llvm::StructType::get(PtrToInt8Ty, PtrToInt8Ty, IntTy);

  // Array of ivar structures.
  auto Ivars = IvarList.beginArray(ObjCIvarTy);
  for (unsigned int i = 0, e = IvarNames.size() ; i < e ; i++) {
    auto Ivar = Ivars.beginStruct(ObjCIvarTy);
    Ivar.add(IvarNames[i]);
    Ivar.add(IvarTypes[i]);
    Ivar.add(IvarOffsets[i]);
    Ivar.finishAndAddTo(Ivars);
  }
  Ivars.finishAndAddTo(IvarList);

  // Create an instance of the structure
  return IvarList.finishAndCreateGlobal(".objc_ivar_list",
                                        CGM.getPointerAlign());
}

/// Generate a class structure
llvm::Constant *CGObjCGNU::GenerateClassStructure(
    llvm::Constant *MetaClass,
    llvm::Constant *SuperClass,
    unsigned info,
    const char *Name,
    llvm::Constant *Version,
    llvm::Constant *InstanceSize,
    llvm::Constant *IVars,
    llvm::Constant *Methods,
    llvm::Constant *Protocols,
    llvm::Constant *IvarOffsets,
    llvm::Constant *Properties,
    llvm::Constant *StrongIvarBitmap,
    llvm::Constant *WeakIvarBitmap,
    bool isMeta) {
  // Set up the class structure
  // Note:  Several of these are char*s when they should be ids.  This is
  // because the runtime performs this translation on load.
  //
  // Fields marked New ABI are part of the GNUstep runtime.  We emit them
  // anyway; the classes will still work with the GNU runtime, they will just
  // be ignored.
  llvm::StructType *ClassTy = llvm::StructType::get(
      PtrToInt8Ty,        // isa
      PtrToInt8Ty,        // super_class
      PtrToInt8Ty,        // name
      LongTy,             // version
      LongTy,             // info
      LongTy,             // instance_size
      IVars->getType(),   // ivars
      Methods->getType(), // methods
      // These are all filled in by the runtime, so we pretend
      PtrTy, // dtable
      PtrTy, // subclass_list
      PtrTy, // sibling_class
      PtrTy, // protocols
      PtrTy, // gc_object_type
      // New ABI:
      LongTy,                 // abi_version
      IvarOffsets->getType(), // ivar_offsets
      Properties->getType(),  // properties
      IntPtrTy,               // strong_pointers
      IntPtrTy                // weak_pointers
      );

  ConstantInitBuilder Builder(CGM);
  auto Elements = Builder.beginStruct(ClassTy);

  // Fill in the structure

  // isa
  Elements.add(MetaClass);
  // super_class
  Elements.add(SuperClass);
  // name
  Elements.add(MakeConstantString(Name, ".class_name"));
  // version
  Elements.addInt(LongTy, 0);
  // info
  Elements.addInt(LongTy, info);
  // instance_size
  if (isMeta) {
    llvm::DataLayout td(&TheModule);
    Elements.addInt(LongTy,
                    td.getTypeSizeInBits(ClassTy) /
                      CGM.getContext().getCharWidth());
  } else
    Elements.add(InstanceSize);
  // ivars
  Elements.add(IVars);
  // methods
  Elements.add(Methods);
  // These are all filled in by the runtime, so we pretend
  // dtable
  Elements.add(NULLPtr);
  // subclass_list
  Elements.add(NULLPtr);
  // sibling_class
  Elements.add(NULLPtr);
  // protocols
  Elements.add(Protocols);
  // gc_object_type
  Elements.add(NULLPtr);
  // abi_version
  Elements.addInt(LongTy, ClassABIVersion);
  // ivar_offsets
  Elements.add(IvarOffsets);
  // properties
  Elements.add(Properties);
  // strong_pointers
  Elements.add(StrongIvarBitmap);
  // weak_pointers
  Elements.add(WeakIvarBitmap);
  // Create an instance of the structure
  // This is now an externally visible symbol, so that we can speed up class
  // messages in the next ABI.  We may already have some weak references to
  // this, so check and fix them properly.
  std::string ClassSym((isMeta ? "_OBJC_METACLASS_": "_OBJC_CLASS_") +
          std::string(Name));
  llvm::GlobalVariable *ClassRef = TheModule.getNamedGlobal(ClassSym);
  llvm::Constant *Class =
    Elements.finishAndCreateGlobal(ClassSym, CGM.getPointerAlign(), false,
                                   llvm::GlobalValue::ExternalLinkage);
  if (ClassRef) {
    ClassRef->replaceAllUsesWith(Class);
    ClassRef->removeFromParent();
    Class->setName(ClassSym);
  }
  return Class;
}

llvm::Constant *CGObjCGNU::
GenerateProtocolMethodList(ArrayRef<const ObjCMethodDecl*> Methods) {
  // Get the method structure type.
  llvm::StructType *ObjCMethodDescTy =
    llvm::StructType::get(CGM.getLLVMContext(), { PtrToInt8Ty, PtrToInt8Ty });
  ASTContext &Context = CGM.getContext();
  ConstantInitBuilder Builder(CGM);
  auto MethodList = Builder.beginStruct();
  MethodList.addInt(IntTy, Methods.size());
  auto MethodArray = MethodList.beginArray(ObjCMethodDescTy);
  for (auto *M : Methods) {
    auto Method = MethodArray.beginStruct(ObjCMethodDescTy);
    Method.add(MakeConstantString(M->getSelector().getAsString()));
    Method.add(MakeConstantString(Context.getObjCEncodingForMethodDecl(M)));
    Method.finishAndAddTo(MethodArray);
  }
  MethodArray.finishAndAddTo(MethodList);
  return MethodList.finishAndCreateGlobal(".objc_method_list",
                                          CGM.getPointerAlign());
}

// Create the protocol list structure used in classes, categories and so on
llvm::Constant *
CGObjCGNU::GenerateProtocolList(ArrayRef<std::string> Protocols) {

  ConstantInitBuilder Builder(CGM);
  auto ProtocolList = Builder.beginStruct();
  ProtocolList.add(NULLPtr);
  ProtocolList.addInt(LongTy, Protocols.size());

  auto Elements = ProtocolList.beginArray(PtrToInt8Ty);
  for (const std::string *iter = Protocols.begin(), *endIter = Protocols.end();
      iter != endIter ; iter++) {
    llvm::Constant *protocol = nullptr;
    llvm::StringMap<llvm::Constant*>::iterator value =
      ExistingProtocols.find(*iter);
    if (value == ExistingProtocols.end()) {
      protocol = GenerateEmptyProtocol(*iter);
    } else {
      protocol = value->getValue();
    }
    Elements.add(protocol);
  }
  Elements.finishAndAddTo(ProtocolList);
  return ProtocolList.finishAndCreateGlobal(".objc_protocol_list",
                                            CGM.getPointerAlign());
}

llvm::Value *CGObjCGNU::GenerateProtocolRef(CodeGenFunction &CGF,
                                            const ObjCProtocolDecl *PD) {
  auto protocol = GenerateProtocolRef(PD);
  llvm::Type *T =
      CGM.getTypes().ConvertType(CGM.getContext().getObjCProtoType());
  return CGF.Builder.CreateBitCast(protocol, llvm::PointerType::getUnqual(T));
}

llvm::Constant *CGObjCGNU::GenerateProtocolRef(const ObjCProtocolDecl *PD) {
  llvm::Constant *&protocol = ExistingProtocols[PD->getNameAsString()];
  if (!protocol)
    GenerateProtocol(PD);
  assert(protocol && "Unknown protocol");
  return protocol;
}

llvm::Constant *
CGObjCGNU::GenerateEmptyProtocol(StringRef ProtocolName) {
  llvm::Constant *ProtocolList = GenerateProtocolList({});
  llvm::Constant *MethodList = GenerateProtocolMethodList({});
  // Protocols are objects containing lists of the methods implemented and
  // protocols adopted.
  ConstantInitBuilder Builder(CGM);
  auto Elements = Builder.beginStruct();

  // The isa pointer must be set to a magic number so the runtime knows it's
  // the correct layout.
  Elements.add(llvm::ConstantExpr::getIntToPtr(
          llvm::ConstantInt::get(Int32Ty, ProtocolVersion), IdTy));

  Elements.add(MakeConstantString(ProtocolName, ".objc_protocol_name"));
  Elements.add(ProtocolList); /* .protocol_list */
  Elements.add(MethodList);   /* .instance_methods */
  Elements.add(MethodList);   /* .class_methods */
  Elements.add(MethodList);   /* .optional_instance_methods */
  Elements.add(MethodList);   /* .optional_class_methods */
  Elements.add(NULLPtr);      /* .properties */
  Elements.add(NULLPtr);      /* .optional_properties */
  return Elements.finishAndCreateGlobal(SymbolForProtocol(ProtocolName),
                                        CGM.getPointerAlign());
}

void CGObjCGNU::GenerateProtocol(const ObjCProtocolDecl *PD) {
  if (PD->isNonRuntimeProtocol())
    return;

  std::string ProtocolName = PD->getNameAsString();

  // Use the protocol definition, if there is one.
  if (const ObjCProtocolDecl *Def = PD->getDefinition())
    PD = Def;

  SmallVector<std::string, 16> Protocols;
  for (const auto *PI : PD->protocols())
    Protocols.push_back(PI->getNameAsString());
  SmallVector<const ObjCMethodDecl*, 16> InstanceMethods;
  SmallVector<const ObjCMethodDecl*, 16> OptionalInstanceMethods;
  for (const auto *I : PD->instance_methods())
    if (I->isOptional())
      OptionalInstanceMethods.push_back(I);
    else
      InstanceMethods.push_back(I);
  // Collect information about class methods:
  SmallVector<const ObjCMethodDecl*, 16> ClassMethods;
  SmallVector<const ObjCMethodDecl*, 16> OptionalClassMethods;
  for (const auto *I : PD->class_methods())
    if (I->isOptional())
      OptionalClassMethods.push_back(I);
    else
      ClassMethods.push_back(I);

  llvm::Constant *ProtocolList = GenerateProtocolList(Protocols);
  llvm::Constant *InstanceMethodList =
    GenerateProtocolMethodList(InstanceMethods);
  llvm::Constant *ClassMethodList =
    GenerateProtocolMethodList(ClassMethods);
  llvm::Constant *OptionalInstanceMethodList =
    GenerateProtocolMethodList(OptionalInstanceMethods);
  llvm::Constant *OptionalClassMethodList =
    GenerateProtocolMethodList(OptionalClassMethods);

  // Property metadata: name, attributes, isSynthesized, setter name, setter
  // types, getter name, getter types.
  // The isSynthesized value is always set to 0 in a protocol.  It exists to
  // simplify the runtime library by allowing it to use the same data
  // structures for protocol metadata everywhere.

  llvm::Constant *PropertyList =
    GeneratePropertyList(nullptr, PD, false, false);
  llvm::Constant *OptionalPropertyList =
    GeneratePropertyList(nullptr, PD, false, true);

  // Protocols are objects containing lists of the methods implemented and
  // protocols adopted.
  // The isa pointer must be set to a magic number so the runtime knows it's
  // the correct layout.
  ConstantInitBuilder Builder(CGM);
  auto Elements = Builder.beginStruct();
  Elements.add(
      llvm::ConstantExpr::getIntToPtr(
          llvm::ConstantInt::get(Int32Ty, ProtocolVersion), IdTy));
  Elements.add(MakeConstantString(ProtocolName));
  Elements.add(ProtocolList);
  Elements.add(InstanceMethodList);
  Elements.add(ClassMethodList);
  Elements.add(OptionalInstanceMethodList);
  Elements.add(OptionalClassMethodList);
  Elements.add(PropertyList);
  Elements.add(OptionalPropertyList);
  ExistingProtocols[ProtocolName] =
      Elements.finishAndCreateGlobal(".objc_protocol", CGM.getPointerAlign());
}
void CGObjCGNU::GenerateProtocolHolderCategory() {
  // Collect information about instance methods

  ConstantInitBuilder Builder(CGM);
  auto Elements = Builder.beginStruct();

  const std::string ClassName = "__ObjC_Protocol_Holder_Ugly_Hack";
  const std::string CategoryName = "AnotherHack";
  Elements.add(MakeConstantString(CategoryName));
  Elements.add(MakeConstantString(ClassName));
  // Instance method list
  Elements.add(GenerateMethodList(ClassName, CategoryName, {}, false));
  // Class method list
  Elements.add(GenerateMethodList(ClassName, CategoryName, {}, true));

  // Protocol list
  ConstantInitBuilder ProtocolListBuilder(CGM);
  auto ProtocolList = ProtocolListBuilder.beginStruct();
  ProtocolList.add(NULLPtr);
  ProtocolList.addInt(LongTy, ExistingProtocols.size());
  auto ProtocolElements = ProtocolList.beginArray(PtrTy);
  for (auto iter = ExistingProtocols.begin(), endIter = ExistingProtocols.end();
       iter != endIter ; iter++) {
    ProtocolElements.add(iter->getValue());
  }
  ProtocolElements.finishAndAddTo(ProtocolList);
  Elements.add(ProtocolList.finishAndCreateGlobal(".objc_protocol_list",
                                                  CGM.getPointerAlign()));
  Categories.push_back(
      Elements.finishAndCreateGlobal("", CGM.getPointerAlign()));
}

/// Libobjc2 uses a bitfield representation where small(ish) bitfields are
/// stored in a 64-bit value with the low bit set to 1 and the remaining 63
/// bits set to their values, LSB first, while larger ones are stored in a
/// structure of this / form:
///
/// struct { int32_t length; int32_t values[length]; };
///
/// The values in the array are stored in host-endian format, with the least
/// significant bit being assumed to come first in the bitfield.  Therefore, a
/// bitfield with the 64th bit set will be (int64_t)&{ 2, [0, 1<<31] }, while a
/// bitfield / with the 63rd bit set will be 1<<64.
llvm::Constant *CGObjCGNU::MakeBitField(ArrayRef<bool> bits) {
  int bitCount = bits.size();
  int ptrBits = CGM.getDataLayout().getPointerSizeInBits();
  if (bitCount < ptrBits) {
    uint64_t val = 1;
    for (int i=0 ; i<bitCount ; ++i) {
      if (bits[i]) val |= 1ULL<<(i+1);
    }
    return llvm::ConstantInt::get(IntPtrTy, val);
  }
  SmallVector<llvm::Constant *, 8> values;
  int v=0;
  while (v < bitCount) {
    int32_t word = 0;
    for (int i=0 ; (i<32) && (v<bitCount)  ; ++i) {
      if (bits[v]) word |= 1<<i;
      v++;
    }
    values.push_back(llvm::ConstantInt::get(Int32Ty, word));
  }

  ConstantInitBuilder builder(CGM);
  auto fields = builder.beginStruct();
  fields.addInt(Int32Ty, values.size());
  auto array = fields.beginArray();
  for (auto *v : values) array.add(v);
  array.finishAndAddTo(fields);

  llvm::Constant *GS =
    fields.finishAndCreateGlobal("", CharUnits::fromQuantity(4));
  llvm::Constant *ptr = llvm::ConstantExpr::getPtrToInt(GS, IntPtrTy);
  return ptr;
}

llvm::Constant *CGObjCGNU::GenerateCategoryProtocolList(const
    ObjCCategoryDecl *OCD) {
  const auto &RefPro = OCD->getReferencedProtocols();
  const auto RuntimeProtos =
      GetRuntimeProtocolList(RefPro.begin(), RefPro.end());
  SmallVector<std::string, 16> Protocols;
  for (const auto *PD : RuntimeProtos)
    Protocols.push_back(PD->getNameAsString());
  return GenerateProtocolList(Protocols);
}

void CGObjCGNU::GenerateCategory(const ObjCCategoryImplDecl *OCD) {
  const ObjCInterfaceDecl *Class = OCD->getClassInterface();
  std::string ClassName = Class->getNameAsString();
  std::string CategoryName = OCD->getNameAsString();

  // Collect the names of referenced protocols
  const ObjCCategoryDecl *CatDecl = OCD->getCategoryDecl();

  ConstantInitBuilder Builder(CGM);
  auto Elements = Builder.beginStruct();
  Elements.add(MakeConstantString(CategoryName));
  Elements.add(MakeConstantString(ClassName));
  // Instance method list
  SmallVector<ObjCMethodDecl*, 16> InstanceMethods;
  InstanceMethods.insert(InstanceMethods.begin(), OCD->instmeth_begin(),
      OCD->instmeth_end());
  Elements.add(
      GenerateMethodList(ClassName, CategoryName, InstanceMethods, false));

  // Class method list

  SmallVector<ObjCMethodDecl*, 16> ClassMethods;
  ClassMethods.insert(ClassMethods.begin(), OCD->classmeth_begin(),
      OCD->classmeth_end());
  Elements.add(GenerateMethodList(ClassName, CategoryName, ClassMethods, true));

  // Protocol list
  Elements.add(GenerateCategoryProtocolList(CatDecl));
  if (isRuntime(ObjCRuntime::GNUstep, 2)) {
    const ObjCCategoryDecl *Category =
      Class->FindCategoryDeclaration(OCD->getIdentifier());
    if (Category) {
      // Instance properties
      Elements.add(GeneratePropertyList(OCD, Category, false));
      // Class properties
      Elements.add(GeneratePropertyList(OCD, Category, true));
    } else {
      Elements.addNullPointer(PtrTy);
      Elements.addNullPointer(PtrTy);
    }
  }

  Categories.push_back(Elements.finishAndCreateGlobal(
      std::string(".objc_category_") + ClassName + CategoryName,
      CGM.getPointerAlign()));
}

llvm::Constant *CGObjCGNU::GeneratePropertyList(const Decl *Container,
    const ObjCContainerDecl *OCD,
    bool isClassProperty,
    bool protocolOptionalProperties) {

  SmallVector<const ObjCPropertyDecl *, 16> Properties;
  llvm::SmallPtrSet<const IdentifierInfo*, 16> PropertySet;
  bool isProtocol = isa<ObjCProtocolDecl>(OCD);
  ASTContext &Context = CGM.getContext();

  std::function<void(const ObjCProtocolDecl *Proto)> collectProtocolProperties
    = [&](const ObjCProtocolDecl *Proto) {
      for (const auto *P : Proto->protocols())
        collectProtocolProperties(P);
      for (const auto *PD : Proto->properties()) {
        if (isClassProperty != PD->isClassProperty())
          continue;
        // Skip any properties that are declared in protocols that this class
        // conforms to but are not actually implemented by this class.
        if (!isProtocol && !Context.getObjCPropertyImplDeclForPropertyDecl(PD, Container))
          continue;
        if (!PropertySet.insert(PD->getIdentifier()).second)
          continue;
        Properties.push_back(PD);
      }
    };

  if (const ObjCInterfaceDecl *OID = dyn_cast<ObjCInterfaceDecl>(OCD))
    for (const ObjCCategoryDecl *ClassExt : OID->known_extensions())
      for (auto *PD : ClassExt->properties()) {
        if (isClassProperty != PD->isClassProperty())
          continue;
        PropertySet.insert(PD->getIdentifier());
        Properties.push_back(PD);
      }

  for (const auto *PD : OCD->properties()) {
    if (isClassProperty != PD->isClassProperty())
      continue;
    // If we're generating a list for a protocol, skip optional / required ones
    // when generating the other list.
    if (isProtocol && (protocolOptionalProperties != PD->isOptional()))
      continue;
    // Don't emit duplicate metadata for properties that were already in a
    // class extension.
    if (!PropertySet.insert(PD->getIdentifier()).second)
      continue;

    Properties.push_back(PD);
  }

  if (const ObjCInterfaceDecl *OID = dyn_cast<ObjCInterfaceDecl>(OCD))
    for (const auto *P : OID->all_referenced_protocols())
      collectProtocolProperties(P);
  else if (const ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(OCD))
    for (const auto *P : CD->protocols())
      collectProtocolProperties(P);

  auto numProperties = Properties.size();

  if (numProperties == 0)
    return NULLPtr;

  ConstantInitBuilder builder(CGM);
  auto propertyList = builder.beginStruct();
  auto properties = PushPropertyListHeader(propertyList, numProperties);

  // Add all of the property methods need adding to the method list and to the
  // property metadata list.
  for (auto *property : Properties) {
    bool isSynthesized = false;
    bool isDynamic = false;
    if (!isProtocol) {
      auto *propertyImpl = Context.getObjCPropertyImplDeclForPropertyDecl(property, Container);
      if (propertyImpl) {
        isSynthesized = (propertyImpl->getPropertyImplementation() ==
            ObjCPropertyImplDecl::Synthesize);
        isDynamic = (propertyImpl->getPropertyImplementation() ==
            ObjCPropertyImplDecl::Dynamic);
      }
    }
    PushProperty(properties, property, Container, isSynthesized, isDynamic);
  }
  properties.finishAndAddTo(propertyList);

  return propertyList.finishAndCreateGlobal(".objc_property_list",
                                            CGM.getPointerAlign());
}

void CGObjCGNU::RegisterAlias(const ObjCCompatibleAliasDecl *OAD) {
  // Get the class declaration for which the alias is specified.
  ObjCInterfaceDecl *ClassDecl =
    const_cast<ObjCInterfaceDecl *>(OAD->getClassInterface());
  ClassAliases.emplace_back(ClassDecl->getNameAsString(),
                            OAD->getNameAsString());
}

void CGObjCGNU::GenerateClass(const ObjCImplementationDecl *OID) {
  ASTContext &Context = CGM.getContext();

  // Get the superclass name.
  const ObjCInterfaceDecl * SuperClassDecl =
    OID->getClassInterface()->getSuperClass();
  std::string SuperClassName;
  if (SuperClassDecl) {
    SuperClassName = SuperClassDecl->getNameAsString();
    EmitClassRef(SuperClassName);
  }

  // Get the class name
  ObjCInterfaceDecl *ClassDecl =
      const_cast<ObjCInterfaceDecl *>(OID->getClassInterface());
  std::string ClassName = ClassDecl->getNameAsString();

  // Emit the symbol that is used to generate linker errors if this class is
  // referenced in other modules but not declared.
  std::string classSymbolName = "__objc_class_name_" + ClassName;
  if (auto *symbol = TheModule.getGlobalVariable(classSymbolName)) {
    symbol->setInitializer(llvm::ConstantInt::get(LongTy, 0));
  } else {
    new llvm::GlobalVariable(TheModule, LongTy, false,
                             llvm::GlobalValue::ExternalLinkage,
                             llvm::ConstantInt::get(LongTy, 0),
                             classSymbolName);
  }

  // Get the size of instances.
  int instanceSize =
    Context.getASTObjCImplementationLayout(OID).getSize().getQuantity();

  // Collect information about instance variables.
  SmallVector<llvm::Constant*, 16> IvarNames;
  SmallVector<llvm::Constant*, 16> IvarTypes;
  SmallVector<llvm::Constant*, 16> IvarOffsets;
  SmallVector<llvm::Constant*, 16> IvarAligns;
  SmallVector<Qualifiers::ObjCLifetime, 16> IvarOwnership;

  ConstantInitBuilder IvarOffsetBuilder(CGM);
  auto IvarOffsetValues = IvarOffsetBuilder.beginArray(PtrToIntTy);
  SmallVector<bool, 16> WeakIvars;
  SmallVector<bool, 16> StrongIvars;

  int superInstanceSize = !SuperClassDecl ? 0 :
    Context.getASTObjCInterfaceLayout(SuperClassDecl).getSize().getQuantity();
  // For non-fragile ivars, set the instance size to 0 - {the size of just this
  // class}.  The runtime will then set this to the correct value on load.
  if (CGM.getLangOpts().ObjCRuntime.isNonFragile()) {
    instanceSize = 0 - (instanceSize - superInstanceSize);
  }

  for (const ObjCIvarDecl *IVD = ClassDecl->all_declared_ivar_begin(); IVD;
       IVD = IVD->getNextIvar()) {
      // Store the name
      IvarNames.push_back(MakeConstantString(IVD->getNameAsString()));
      // Get the type encoding for this ivar
      std::string TypeStr;
      Context.getObjCEncodingForType(IVD->getType(), TypeStr, IVD);
      IvarTypes.push_back(MakeConstantString(TypeStr));
      IvarAligns.push_back(llvm::ConstantInt::get(IntTy,
            Context.getTypeSize(IVD->getType())));
      // Get the offset
      uint64_t BaseOffset = ComputeIvarBaseOffset(CGM, OID, IVD);
      uint64_t Offset = BaseOffset;
      if (CGM.getLangOpts().ObjCRuntime.isNonFragile()) {
        Offset = BaseOffset - superInstanceSize;
      }
      llvm::Constant *OffsetValue = llvm::ConstantInt::get(IntTy, Offset);
      // Create the direct offset value
      std::string OffsetName = "__objc_ivar_offset_value_" + ClassName +"." +
          IVD->getNameAsString();

      llvm::GlobalVariable *OffsetVar = TheModule.getGlobalVariable(OffsetName);
      if (OffsetVar) {
        OffsetVar->setInitializer(OffsetValue);
        // If this is the real definition, change its linkage type so that
        // different modules will use this one, rather than their private
        // copy.
        OffsetVar->setLinkage(llvm::GlobalValue::ExternalLinkage);
      } else
        OffsetVar = new llvm::GlobalVariable(TheModule, Int32Ty,
          false, llvm::GlobalValue::ExternalLinkage,
          OffsetValue, OffsetName);
      IvarOffsets.push_back(OffsetValue);
      IvarOffsetValues.add(OffsetVar);
      Qualifiers::ObjCLifetime lt = IVD->getType().getQualifiers().getObjCLifetime();
      IvarOwnership.push_back(lt);
      switch (lt) {
        case Qualifiers::OCL_Strong:
          StrongIvars.push_back(true);
          WeakIvars.push_back(false);
          break;
        case Qualifiers::OCL_Weak:
          StrongIvars.push_back(false);
          WeakIvars.push_back(true);
          break;
        default:
          StrongIvars.push_back(false);
          WeakIvars.push_back(false);
      }
  }
  llvm::Constant *StrongIvarBitmap = MakeBitField(StrongIvars);
  llvm::Constant *WeakIvarBitmap = MakeBitField(WeakIvars);
  llvm::GlobalVariable *IvarOffsetArray =
    IvarOffsetValues.finishAndCreateGlobal(".ivar.offsets",
                                           CGM.getPointerAlign());

  // Collect information about instance methods
  SmallVector<const ObjCMethodDecl*, 16> InstanceMethods;
  InstanceMethods.insert(InstanceMethods.begin(), OID->instmeth_begin(),
      OID->instmeth_end());

  SmallVector<const ObjCMethodDecl*, 16> ClassMethods;
  ClassMethods.insert(ClassMethods.begin(), OID->classmeth_begin(),
      OID->classmeth_end());

  llvm::Constant *Properties = GeneratePropertyList(OID, ClassDecl);

  // Collect the names of referenced protocols
  auto RefProtocols = ClassDecl->protocols();
  auto RuntimeProtocols =
      GetRuntimeProtocolList(RefProtocols.begin(), RefProtocols.end());
  SmallVector<std::string, 16> Protocols;
  for (const auto *I : RuntimeProtocols)
    Protocols.push_back(I->getNameAsString());

  // Get the superclass pointer.
  llvm::Constant *SuperClass;
  if (!SuperClassName.empty()) {
    SuperClass = MakeConstantString(SuperClassName, ".super_class_name");
  } else {
    SuperClass = llvm::ConstantPointerNull::get(PtrToInt8Ty);
  }
  // Empty vector used to construct empty method lists
  SmallVector<llvm::Constant*, 1>  empty;
  // Generate the method and instance variable lists
  llvm::Constant *MethodList = GenerateMethodList(ClassName, "",
      InstanceMethods, false);
  llvm::Constant *ClassMethodList = GenerateMethodList(ClassName, "",
      ClassMethods, true);
  llvm::Constant *IvarList = GenerateIvarList(IvarNames, IvarTypes,
      IvarOffsets, IvarAligns, IvarOwnership);
  // Irrespective of whether we are compiling for a fragile or non-fragile ABI,
  // we emit a symbol containing the offset for each ivar in the class.  This
  // allows code compiled for the non-Fragile ABI to inherit from code compiled
  // for the legacy ABI, without causing problems.  The converse is also
  // possible, but causes all ivar accesses to be fragile.

  // Offset pointer for getting at the correct field in the ivar list when
  // setting up the alias.  These are: The base address for the global, the
  // ivar array (second field), the ivar in this list (set for each ivar), and
  // the offset (third field in ivar structure)
  llvm::Type *IndexTy = Int32Ty;
  llvm::Constant *offsetPointerIndexes[] = {Zeros[0],
      llvm::ConstantInt::get(IndexTy, ClassABIVersion > 1 ? 2 : 1), nullptr,
      llvm::ConstantInt::get(IndexTy, ClassABIVersion > 1 ? 3 : 2) };

  unsigned ivarIndex = 0;
  for (const ObjCIvarDecl *IVD = ClassDecl->all_declared_ivar_begin(); IVD;
       IVD = IVD->getNextIvar()) {
      const std::string Name = GetIVarOffsetVariableName(ClassDecl, IVD);
      offsetPointerIndexes[2] = llvm::ConstantInt::get(IndexTy, ivarIndex);
      // Get the correct ivar field
      llvm::Constant *offsetValue = llvm::ConstantExpr::getGetElementPtr(
          cast<llvm::GlobalVariable>(IvarList)->getValueType(), IvarList,
          offsetPointerIndexes);
      // Get the existing variable, if one exists.
      llvm::GlobalVariable *offset = TheModule.getNamedGlobal(Name);
      if (offset) {
        offset->setInitializer(offsetValue);
        // If this is the real definition, change its linkage type so that
        // different modules will use this one, rather than their private
        // copy.
        offset->setLinkage(llvm::GlobalValue::ExternalLinkage);
      } else
        // Add a new alias if there isn't one already.
        new llvm::GlobalVariable(TheModule, offsetValue->getType(),
                false, llvm::GlobalValue::ExternalLinkage, offsetValue, Name);
      ++ivarIndex;
  }
  llvm::Constant *ZeroPtr = llvm::ConstantInt::get(IntPtrTy, 0);

  //Generate metaclass for class methods
  llvm::Constant *MetaClassStruct = GenerateClassStructure(
      NULLPtr, NULLPtr, 0x12L, ClassName.c_str(), nullptr, Zeros[0],
      NULLPtr, ClassMethodList, NULLPtr, NULLPtr,
      GeneratePropertyList(OID, ClassDecl, true), ZeroPtr, ZeroPtr, true);
  CGM.setGVProperties(cast<llvm::GlobalValue>(MetaClassStruct),
                      OID->getClassInterface());

  // Generate the class structure
  llvm::Constant *ClassStruct = GenerateClassStructure(
      MetaClassStruct, SuperClass, 0x11L, ClassName.c_str(), nullptr,
      llvm::ConstantInt::get(LongTy, instanceSize), IvarList, MethodList,
      GenerateProtocolList(Protocols), IvarOffsetArray, Properties,
      StrongIvarBitmap, WeakIvarBitmap);
  CGM.setGVProperties(cast<llvm::GlobalValue>(ClassStruct),
                      OID->getClassInterface());

  // Resolve the class aliases, if they exist.
  if (ClassPtrAlias) {
    ClassPtrAlias->replaceAllUsesWith(ClassStruct);
    ClassPtrAlias->eraseFromParent();
    ClassPtrAlias = nullptr;
  }
  if (MetaClassPtrAlias) {
    MetaClassPtrAlias->replaceAllUsesWith(MetaClassStruct);
    MetaClassPtrAlias->eraseFromParent();
    MetaClassPtrAlias = nullptr;
  }

  // Add class structure to list to be added to the symtab later
  Classes.push_back(ClassStruct);
}

llvm::Function *CGObjCGNU::ModuleInitFunction() {
  // Only emit an ObjC load function if no Objective-C stuff has been called
  if (Classes.empty() && Categories.empty() && ConstantStrings.empty() &&
      ExistingProtocols.empty() && SelectorTable.empty())
    return nullptr;

  // Add all referenced protocols to a category.
  GenerateProtocolHolderCategory();

  llvm::StructType *selStructTy = dyn_cast<llvm::StructType>(SelectorElemTy);
  if (!selStructTy) {
    selStructTy = llvm::StructType::get(CGM.getLLVMContext(),
                                        { PtrToInt8Ty, PtrToInt8Ty });
  }

  // Generate statics list:
  llvm::Constant *statics = NULLPtr;
  if (!ConstantStrings.empty()) {
    llvm::GlobalVariable *fileStatics = [&] {
      ConstantInitBuilder builder(CGM);
      auto staticsStruct = builder.beginStruct();

      StringRef stringClass = CGM.getLangOpts().ObjCConstantStringClass;
      if (stringClass.empty()) stringClass = "NXConstantString";
      staticsStruct.add(MakeConstantString(stringClass,
                                           ".objc_static_class_name"));

      auto array = staticsStruct.beginArray();
      array.addAll(ConstantStrings);
      array.add(NULLPtr);
      array.finishAndAddTo(staticsStruct);

      return staticsStruct.finishAndCreateGlobal(".objc_statics",
                                                 CGM.getPointerAlign());
    }();

    ConstantInitBuilder builder(CGM);
    auto allStaticsArray = builder.beginArray(fileStatics->getType());
    allStaticsArray.add(fileStatics);
    allStaticsArray.addNullPointer(fileStatics->getType());

    statics = allStaticsArray.finishAndCreateGlobal(".objc_statics_ptr",
                                                    CGM.getPointerAlign());
  }

  // Array of classes, categories, and constant objects.

  SmallVector<llvm::GlobalAlias*, 16> selectorAliases;
  unsigned selectorCount;

  // Pointer to an array of selectors used in this module.
  llvm::GlobalVariable *selectorList = [&] {
    ConstantInitBuilder builder(CGM);
    auto selectors = builder.beginArray(selStructTy);
    auto &table = SelectorTable; // MSVC workaround
    std::vector<Selector> allSelectors;
    for (auto &entry : table)
      allSelectors.push_back(entry.first);
    llvm::sort(allSelectors);

    for (auto &untypedSel : allSelectors) {
      std::string selNameStr = untypedSel.getAsString();
      llvm::Constant *selName = ExportUniqueString(selNameStr, ".objc_sel_name");

      for (TypedSelector &sel : table[untypedSel]) {
        llvm::Constant *selectorTypeEncoding = NULLPtr;
        if (!sel.first.empty())
          selectorTypeEncoding =
            MakeConstantString(sel.first, ".objc_sel_types");

        auto selStruct = selectors.beginStruct(selStructTy);
        selStruct.add(selName);
        selStruct.add(selectorTypeEncoding);
        selStruct.finishAndAddTo(selectors);

        // Store the selector alias for later replacement
        selectorAliases.push_back(sel.second);
      }
    }

    // Remember the number of entries in the selector table.
    selectorCount = selectors.size();

    // NULL-terminate the selector list.  This should not actually be required,
    // because the selector list has a length field.  Unfortunately, the GCC
    // runtime decides to ignore the length field and expects a NULL terminator,
    // and GCC cooperates with this by always setting the length to 0.
    auto selStruct = selectors.beginStruct(selStructTy);
    selStruct.add(NULLPtr);
    selStruct.add(NULLPtr);
    selStruct.finishAndAddTo(selectors);

    return selectors.finishAndCreateGlobal(".objc_selector_list",
                                           CGM.getPointerAlign());
  }();

  // Now that all of the static selectors exist, create pointers to them.
  for (unsigned i = 0; i < selectorCount; ++i) {
    llvm::Constant *idxs[] = {
      Zeros[0],
      llvm::ConstantInt::get(Int32Ty, i)
    };
    // FIXME: We're generating redundant loads and stores here!
    llvm::Constant *selPtr = llvm::ConstantExpr::getGetElementPtr(
        selectorList->getValueType(), selectorList, idxs);
    selectorAliases[i]->replaceAllUsesWith(selPtr);
    selectorAliases[i]->eraseFromParent();
  }

  llvm::GlobalVariable *symtab = [&] {
    ConstantInitBuilder builder(CGM);
    auto symtab = builder.beginStruct();

    // Number of static selectors
    symtab.addInt(LongTy, selectorCount);

    symtab.add(selectorList);

    // Number of classes defined.
    symtab.addInt(CGM.Int16Ty, Classes.size());
    // Number of categories defined
    symtab.addInt(CGM.Int16Ty, Categories.size());

    // Create an array of classes, then categories, then static object instances
    auto classList = symtab.beginArray(PtrToInt8Ty);
    classList.addAll(Classes);
    classList.addAll(Categories);
    //  NULL-terminated list of static object instances (mainly constant strings)
    classList.add(statics);
    classList.add(NULLPtr);
    classList.finishAndAddTo(symtab);

    // Construct the symbol table.
    return symtab.finishAndCreateGlobal("", CGM.getPointerAlign());
  }();

  // The symbol table is contained in a module which has some version-checking
  // constants
  llvm::Constant *module = [&] {
    llvm::Type *moduleEltTys[] = {
      LongTy, LongTy, PtrToInt8Ty, symtab->getType(), IntTy
    };
    llvm::StructType *moduleTy = llvm::StructType::get(
        CGM.getLLVMContext(),
        ArrayRef(moduleEltTys).drop_back(unsigned(RuntimeVersion < 10)));

    ConstantInitBuilder builder(CGM);
    auto module = builder.beginStruct(moduleTy);
    // Runtime version, used for ABI compatibility checking.
    module.addInt(LongTy, RuntimeVersion);
    // sizeof(ModuleTy)
    module.addInt(LongTy, CGM.getDataLayout().getTypeStoreSize(moduleTy));

    // The path to the source file where this module was declared
    SourceManager &SM = CGM.getContext().getSourceManager();
    OptionalFileEntryRef mainFile = SM.getFileEntryRefForID(SM.getMainFileID());
    std::string path =
        (mainFile->getDir().getName() + "/" + mainFile->getName()).str();
    module.add(MakeConstantString(path, ".objc_source_file_name"));
    module.add(symtab);

    if (RuntimeVersion >= 10) {
      switch (CGM.getLangOpts().getGC()) {
      case LangOptions::GCOnly:
        module.addInt(IntTy, 2);
        break;
      case LangOptions::NonGC:
        if (CGM.getLangOpts().ObjCAutoRefCount)
          module.addInt(IntTy, 1);
        else
          module.addInt(IntTy, 0);
        break;
      case LangOptions::HybridGC:
        module.addInt(IntTy, 1);
        break;
      }
    }

    return module.finishAndCreateGlobal("", CGM.getPointerAlign());
  }();

  // Create the load function calling the runtime entry point with the module
  // structure
  llvm::Function * LoadFunction = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(VMContext), false),
      llvm::GlobalValue::InternalLinkage, ".objc_load_function",
      &TheModule);
  llvm::BasicBlock *EntryBB =
      llvm::BasicBlock::Create(VMContext, "entry", LoadFunction);
  CGBuilderTy Builder(CGM, VMContext);
  Builder.SetInsertPoint(EntryBB);

  llvm::FunctionType *FT =
    llvm::FunctionType::get(Builder.getVoidTy(), module->getType(), true);
  llvm::FunctionCallee Register =
      CGM.CreateRuntimeFunction(FT, "__objc_exec_class");
  Builder.CreateCall(Register, module);

  if (!ClassAliases.empty()) {
    llvm::Type *ArgTypes[2] = {PtrTy, PtrToInt8Ty};
    llvm::FunctionType *RegisterAliasTy =
      llvm::FunctionType::get(Builder.getVoidTy(),
                              ArgTypes, false);
    llvm::Function *RegisterAlias = llvm::Function::Create(
      RegisterAliasTy,
      llvm::GlobalValue::ExternalWeakLinkage, "class_registerAlias_np",
      &TheModule);
    llvm::BasicBlock *AliasBB =
      llvm::BasicBlock::Create(VMContext, "alias", LoadFunction);
    llvm::BasicBlock *NoAliasBB =
      llvm::BasicBlock::Create(VMContext, "no_alias", LoadFunction);

    // Branch based on whether the runtime provided class_registerAlias_np()
    llvm::Value *HasRegisterAlias = Builder.CreateICmpNE(RegisterAlias,
            llvm::Constant::getNullValue(RegisterAlias->getType()));
    Builder.CreateCondBr(HasRegisterAlias, AliasBB, NoAliasBB);

    // The true branch (has alias registration function):
    Builder.SetInsertPoint(AliasBB);
    // Emit alias registration calls:
    for (std::vector<ClassAliasPair>::iterator iter = ClassAliases.begin();
       iter != ClassAliases.end(); ++iter) {
       llvm::Constant *TheClass =
          TheModule.getGlobalVariable("_OBJC_CLASS_" + iter->first, true);
       if (TheClass) {
         Builder.CreateCall(RegisterAlias,
                            {TheClass, MakeConstantString(iter->second)});
       }
    }
    // Jump to end:
    Builder.CreateBr(NoAliasBB);

    // Missing alias registration function, just return from the function:
    Builder.SetInsertPoint(NoAliasBB);
  }
  Builder.CreateRetVoid();

  return LoadFunction;
}

llvm::Function *CGObjCGNU::GenerateMethod(const ObjCMethodDecl *OMD,
                                          const ObjCContainerDecl *CD) {
  CodeGenTypes &Types = CGM.getTypes();
  llvm::FunctionType *MethodTy =
    Types.GetFunctionType(Types.arrangeObjCMethodDeclaration(OMD));

  bool isDirect = OMD->isDirectMethod();
  std::string FunctionName =
      getSymbolNameForMethod(OMD, /*include category*/ !isDirect);

  if (!isDirect)
    return llvm::Function::Create(MethodTy,
                                  llvm::GlobalVariable::InternalLinkage,
                                  FunctionName, &TheModule);

  auto *COMD = OMD->getCanonicalDecl();
  auto I = DirectMethodDefinitions.find(COMD);
  llvm::Function *OldFn = nullptr, *Fn = nullptr;

  if (I == DirectMethodDefinitions.end()) {
    auto *F =
        llvm::Function::Create(MethodTy, llvm::GlobalVariable::ExternalLinkage,
                               FunctionName, &TheModule);
    DirectMethodDefinitions.insert(std::make_pair(COMD, F));
    return F;
  }

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
  Fn = llvm::Function::Create(MethodTy, llvm::GlobalValue::ExternalLinkage, "",
                              &CGM.getModule());
  Fn->takeName(OldFn);
  OldFn->replaceAllUsesWith(Fn);
  OldFn->eraseFromParent();

  // Replace the cached function in the map.
  I->second = Fn;
  return Fn;
}

void CGObjCGNU::GenerateDirectMethodPrologue(CodeGenFunction &CGF,
                                             llvm::Function *Fn,
                                             const ObjCMethodDecl *OMD,
                                             const ObjCContainerDecl *CD) {
  // GNU runtime doesn't support direct calls at this time
}

llvm::FunctionCallee CGObjCGNU::GetPropertyGetFunction() {
  return GetPropertyFn;
}

llvm::FunctionCallee CGObjCGNU::GetPropertySetFunction() {
  return SetPropertyFn;
}

llvm::FunctionCallee CGObjCGNU::GetOptimizedPropertySetFunction(bool atomic,
                                                                bool copy) {
  return nullptr;
}

llvm::FunctionCallee CGObjCGNU::GetGetStructFunction() {
  return GetStructPropertyFn;
}

llvm::FunctionCallee CGObjCGNU::GetSetStructFunction() {
  return SetStructPropertyFn;
}

llvm::FunctionCallee CGObjCGNU::GetCppAtomicObjectGetFunction() {
  return nullptr;
}

llvm::FunctionCallee CGObjCGNU::GetCppAtomicObjectSetFunction() {
  return nullptr;
}

llvm::FunctionCallee CGObjCGNU::EnumerationMutationFunction() {
  return EnumerationMutationFn;
}

void CGObjCGNU::EmitSynchronizedStmt(CodeGenFunction &CGF,
                                     const ObjCAtSynchronizedStmt &S) {
  EmitAtSynchronizedStmt(CGF, S, SyncEnterFn, SyncExitFn);
}


void CGObjCGNU::EmitTryStmt(CodeGenFunction &CGF,
                            const ObjCAtTryStmt &S) {
  // Unlike the Apple non-fragile runtimes, which also uses
  // unwind-based zero cost exceptions, the GNU Objective C runtime's
  // EH support isn't a veneer over C++ EH.  Instead, exception
  // objects are created by objc_exception_throw and destroyed by
  // the personality function; this avoids the need for bracketing
  // catch handlers with calls to __blah_begin_catch/__blah_end_catch
  // (or even _Unwind_DeleteException), but probably doesn't
  // interoperate very well with foreign exceptions.
  //
  // In Objective-C++ mode, we actually emit something equivalent to the C++
  // exception handler.
  EmitTryCatchStmt(CGF, S, EnterCatchFn, ExitCatchFn, ExceptionReThrowFn);
}

void CGObjCGNU::EmitThrowStmt(CodeGenFunction &CGF,
                              const ObjCAtThrowStmt &S,
                              bool ClearInsertionPoint) {
  llvm::Value *ExceptionAsObject;
  bool isRethrow = false;

  if (const Expr *ThrowExpr = S.getThrowExpr()) {
    llvm::Value *Exception = CGF.EmitObjCThrowOperand(ThrowExpr);
    ExceptionAsObject = Exception;
  } else {
    assert((!CGF.ObjCEHValueStack.empty() && CGF.ObjCEHValueStack.back()) &&
           "Unexpected rethrow outside @catch block.");
    ExceptionAsObject = CGF.ObjCEHValueStack.back();
    isRethrow = true;
  }
  if (isRethrow && (usesSEHExceptions || usesCxxExceptions)) {
    // For SEH, ExceptionAsObject may be undef, because the catch handler is
    // not passed it for catchalls and so it is not visible to the catch
    // funclet.  The real thrown object will still be live on the stack at this
    // point and will be rethrown.  If we are explicitly rethrowing the object
    // that was passed into the `@catch` block, then this code path is not
    // reached and we will instead call `objc_exception_throw` with an explicit
    // argument.
    llvm::CallBase *Throw = CGF.EmitRuntimeCallOrInvoke(ExceptionReThrowFn);
    Throw->setDoesNotReturn();
  } else {
    ExceptionAsObject = CGF.Builder.CreateBitCast(ExceptionAsObject, IdTy);
    llvm::CallBase *Throw =
        CGF.EmitRuntimeCallOrInvoke(ExceptionThrowFn, ExceptionAsObject);
    Throw->setDoesNotReturn();
  }
  CGF.Builder.CreateUnreachable();
  if (ClearInsertionPoint)
    CGF.Builder.ClearInsertionPoint();
}

llvm::Value * CGObjCGNU::EmitObjCWeakRead(CodeGenFunction &CGF,
                                          Address AddrWeakObj) {
  CGBuilderTy &B = CGF.Builder;
  return B.CreateCall(
      WeakReadFn, EnforceType(B, AddrWeakObj.emitRawPointer(CGF), PtrToIdTy));
}

void CGObjCGNU::EmitObjCWeakAssign(CodeGenFunction &CGF,
                                   llvm::Value *src, Address dst) {
  CGBuilderTy &B = CGF.Builder;
  src = EnforceType(B, src, IdTy);
  llvm::Value *dstVal = EnforceType(B, dst.emitRawPointer(CGF), PtrToIdTy);
  B.CreateCall(WeakAssignFn, {src, dstVal});
}

void CGObjCGNU::EmitObjCGlobalAssign(CodeGenFunction &CGF,
                                     llvm::Value *src, Address dst,
                                     bool threadlocal) {
  CGBuilderTy &B = CGF.Builder;
  src = EnforceType(B, src, IdTy);
  llvm::Value *dstVal = EnforceType(B, dst.emitRawPointer(CGF), PtrToIdTy);
  // FIXME. Add threadloca assign API
  assert(!threadlocal && "EmitObjCGlobalAssign - Threal Local API NYI");
  B.CreateCall(GlobalAssignFn, {src, dstVal});
}

void CGObjCGNU::EmitObjCIvarAssign(CodeGenFunction &CGF,
                                   llvm::Value *src, Address dst,
                                   llvm::Value *ivarOffset) {
  CGBuilderTy &B = CGF.Builder;
  src = EnforceType(B, src, IdTy);
  llvm::Value *dstVal = EnforceType(B, dst.emitRawPointer(CGF), IdTy);
  B.CreateCall(IvarAssignFn, {src, dstVal, ivarOffset});
}

void CGObjCGNU::EmitObjCStrongCastAssign(CodeGenFunction &CGF,
                                         llvm::Value *src, Address dst) {
  CGBuilderTy &B = CGF.Builder;
  src = EnforceType(B, src, IdTy);
  llvm::Value *dstVal = EnforceType(B, dst.emitRawPointer(CGF), PtrToIdTy);
  B.CreateCall(StrongCastAssignFn, {src, dstVal});
}

void CGObjCGNU::EmitGCMemmoveCollectable(CodeGenFunction &CGF,
                                         Address DestPtr,
                                         Address SrcPtr,
                                         llvm::Value *Size) {
  CGBuilderTy &B = CGF.Builder;
  llvm::Value *DestPtrVal = EnforceType(B, DestPtr.emitRawPointer(CGF), PtrTy);
  llvm::Value *SrcPtrVal = EnforceType(B, SrcPtr.emitRawPointer(CGF), PtrTy);

  B.CreateCall(MemMoveFn, {DestPtrVal, SrcPtrVal, Size});
}

llvm::GlobalVariable *CGObjCGNU::ObjCIvarOffsetVariable(
                              const ObjCInterfaceDecl *ID,
                              const ObjCIvarDecl *Ivar) {
  const std::string Name = GetIVarOffsetVariableName(ID, Ivar);
  // Emit the variable and initialize it with what we think the correct value
  // is.  This allows code compiled with non-fragile ivars to work correctly
  // when linked against code which isn't (most of the time).
  llvm::GlobalVariable *IvarOffsetPointer = TheModule.getNamedGlobal(Name);
  if (!IvarOffsetPointer)
    IvarOffsetPointer = new llvm::GlobalVariable(
        TheModule, llvm::PointerType::getUnqual(VMContext), false,
        llvm::GlobalValue::ExternalLinkage, nullptr, Name);
  return IvarOffsetPointer;
}

LValue CGObjCGNU::EmitObjCValueForIvar(CodeGenFunction &CGF,
                                       QualType ObjectTy,
                                       llvm::Value *BaseValue,
                                       const ObjCIvarDecl *Ivar,
                                       unsigned CVRQualifiers) {
  const ObjCInterfaceDecl *ID =
    ObjectTy->castAs<ObjCObjectType>()->getInterface();
  return EmitValueForIvarAtOffset(CGF, ID, BaseValue, Ivar, CVRQualifiers,
                                  EmitIvarOffset(CGF, ID, Ivar));
}

static const ObjCInterfaceDecl *FindIvarInterface(ASTContext &Context,
                                                  const ObjCInterfaceDecl *OID,
                                                  const ObjCIvarDecl *OIVD) {
  for (const ObjCIvarDecl *next = OID->all_declared_ivar_begin(); next;
       next = next->getNextIvar()) {
    if (OIVD == next)
      return OID;
  }

  // Otherwise check in the super class.
  if (const ObjCInterfaceDecl *Super = OID->getSuperClass())
    return FindIvarInterface(Context, Super, OIVD);

  return nullptr;
}

llvm::Value *CGObjCGNU::EmitIvarOffset(CodeGenFunction &CGF,
                         const ObjCInterfaceDecl *Interface,
                         const ObjCIvarDecl *Ivar) {
  if (CGM.getLangOpts().ObjCRuntime.isNonFragile()) {
    Interface = FindIvarInterface(CGM.getContext(), Interface, Ivar);

    // The MSVC linker cannot have a single global defined as LinkOnceAnyLinkage
    // and ExternalLinkage, so create a reference to the ivar global and rely on
    // the definition being created as part of GenerateClass.
    if (RuntimeVersion < 10 ||
        CGF.CGM.getTarget().getTriple().isKnownWindowsMSVCEnvironment())
      return CGF.Builder.CreateZExtOrBitCast(
          CGF.Builder.CreateAlignedLoad(
              Int32Ty,
              CGF.Builder.CreateAlignedLoad(
                  llvm::PointerType::getUnqual(VMContext),
                  ObjCIvarOffsetVariable(Interface, Ivar),
                  CGF.getPointerAlign(), "ivar"),
              CharUnits::fromQuantity(4)),
          PtrDiffTy);
    std::string name = "__objc_ivar_offset_value_" +
      Interface->getNameAsString() +"." + Ivar->getNameAsString();
    CharUnits Align = CGM.getIntAlign();
    llvm::Value *Offset = TheModule.getGlobalVariable(name);
    if (!Offset) {
      auto GV = new llvm::GlobalVariable(TheModule, IntTy,
          false, llvm::GlobalValue::LinkOnceAnyLinkage,
          llvm::Constant::getNullValue(IntTy), name);
      GV->setAlignment(Align.getAsAlign());
      Offset = GV;
    }
    Offset = CGF.Builder.CreateAlignedLoad(IntTy, Offset, Align);
    if (Offset->getType() != PtrDiffTy)
      Offset = CGF.Builder.CreateZExtOrBitCast(Offset, PtrDiffTy);
    return Offset;
  }
  uint64_t Offset = ComputeIvarBaseOffset(CGF.CGM, Interface, Ivar);
  return llvm::ConstantInt::get(PtrDiffTy, Offset, /*isSigned*/true);
}

CGObjCRuntime *
clang::CodeGen::CreateGNUObjCRuntime(CodeGenModule &CGM) {
  auto Runtime = CGM.getLangOpts().ObjCRuntime;
  switch (Runtime.getKind()) {
  case ObjCRuntime::GNUstep:
    if (Runtime.getVersion() >= VersionTuple(2, 0))
      return new CGObjCGNUstep2(CGM);
    return new CGObjCGNUstep(CGM);

  case ObjCRuntime::GCC:
    return new CGObjCGCC(CGM);

  case ObjCRuntime::ObjFW:
    return new CGObjCObjFW(CGM);

  case ObjCRuntime::FragileMacOSX:
  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    llvm_unreachable("these runtimes are not GNU runtimes");
  }
  llvm_unreachable("bad runtime");
}
