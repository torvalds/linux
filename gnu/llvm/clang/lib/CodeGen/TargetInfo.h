//===---- TargetInfo.h - Encapsulate target details -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function
// definition used to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_TARGETINFO_H
#define LLVM_CLANG_LIB_CODEGEN_TARGETINFO_H

#include "CGBuilder.h"
#include "CGValue.h"
#include "CodeGenModule.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SyncScope.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class Constant;
class GlobalValue;
class Type;
class Value;
}

namespace clang {
class Decl;

namespace CodeGen {
class ABIInfo;
class CallArgList;
class CodeGenFunction;
class CGBlockInfo;
class SwiftABIInfo;

/// TargetCodeGenInfo - This class organizes various target-specific
/// codegeneration issues, like target-specific attributes, builtins and so
/// on.
class TargetCodeGenInfo {
  std::unique_ptr<ABIInfo> Info;

protected:
  // Target hooks supporting Swift calling conventions. The target must
  // initialize this field if it claims to support these calling conventions
  // by returning true from TargetInfo::checkCallingConvention for them.
  std::unique_ptr<SwiftABIInfo> SwiftInfo;

  // Returns ABI info helper for the target. This is for use by derived classes.
  template <typename T> const T &getABIInfo() const {
    return static_cast<const T &>(*Info);
  }

public:
  TargetCodeGenInfo(std::unique_ptr<ABIInfo> Info);
  virtual ~TargetCodeGenInfo();

  /// getABIInfo() - Returns ABI info helper for the target.
  const ABIInfo &getABIInfo() const { return *Info; }

  /// Returns Swift ABI info helper for the target.
  const SwiftABIInfo &getSwiftABIInfo() const {
    assert(SwiftInfo && "Swift ABI info has not been initialized");
    return *SwiftInfo;
  }

  /// setTargetAttributes - Provides a convenient hook to handle extra
  /// target-specific attributes for the given global.
  virtual void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                                   CodeGen::CodeGenModule &M) const {}

  /// emitTargetMetadata - Provides a convenient hook to handle extra
  /// target-specific metadata for the given globals.
  virtual void emitTargetMetadata(
      CodeGen::CodeGenModule &CGM,
      const llvm::MapVector<GlobalDecl, StringRef> &MangledDeclNames) const {}

  /// Provides a convenient hook to handle extra target-specific globals.
  virtual void emitTargetGlobals(CodeGen::CodeGenModule &CGM) const {}

  /// Any further codegen related checks that need to be done on a function
  /// signature in a target specific manner.
  virtual void checkFunctionABI(CodeGenModule &CGM,
                                const FunctionDecl *Decl) const {}

  /// Any further codegen related checks that need to be done on a function call
  /// in a target specific manner.
  virtual void checkFunctionCallABI(CodeGenModule &CGM, SourceLocation CallLoc,
                                    const FunctionDecl *Caller,
                                    const FunctionDecl *Callee,
                                    const CallArgList &Args,
                                    QualType ReturnType) const {}

  /// Determines the size of struct _Unwind_Exception on this platform,
  /// in 8-bit units.  The Itanium ABI defines this as:
  ///   struct _Unwind_Exception {
  ///     uint64 exception_class;
  ///     _Unwind_Exception_Cleanup_Fn exception_cleanup;
  ///     uint64 private_1;
  ///     uint64 private_2;
  ///   };
  virtual unsigned getSizeOfUnwindException() const;

  /// Controls whether __builtin_extend_pointer should sign-extend
  /// pointers to uint64_t or zero-extend them (the default).  Has
  /// no effect for targets:
  ///   - that have 64-bit pointers, or
  ///   - that cannot address through registers larger than pointers, or
  ///   - that implicitly ignore/truncate the top bits when addressing
  ///     through such registers.
  virtual bool extendPointerWithSExt() const { return false; }

  /// Determines the DWARF register number for the stack pointer, for
  /// exception-handling purposes.  Implements __builtin_dwarf_sp_column.
  ///
  /// Returns -1 if the operation is unsupported by this target.
  virtual int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const {
    return -1;
  }

  /// Initializes the given DWARF EH register-size table, a char*.
  /// Implements __builtin_init_dwarf_reg_size_table.
  ///
  /// Returns true if the operation is unsupported by this target.
  virtual bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                       llvm::Value *Address) const {
    return true;
  }

  /// Performs the code-generation required to convert a return
  /// address as stored by the system into the actual address of the
  /// next instruction that will be executed.
  ///
  /// Used by __builtin_extract_return_addr().
  virtual llvm::Value *decodeReturnAddress(CodeGen::CodeGenFunction &CGF,
                                           llvm::Value *Address) const {
    return Address;
  }

  /// Performs the code-generation required to convert the address
  /// of an instruction into a return address suitable for storage
  /// by the system in a return slot.
  ///
  /// Used by __builtin_frob_return_addr().
  virtual llvm::Value *encodeReturnAddress(CodeGen::CodeGenFunction &CGF,
                                           llvm::Value *Address) const {
    return Address;
  }

  /// Performs a target specific test of a floating point value for things
  /// like IsNaN, Infinity, ... Nullptr is returned if no implementation
  /// exists.
  virtual llvm::Value *
  testFPKind(llvm::Value *V, unsigned BuiltinID, CGBuilderTy &Builder,
             CodeGenModule &CGM) const {
    assert(V->getType()->isFloatingPointTy() && "V should have an FP type.");
    return nullptr;
  }

  /// Corrects the low-level LLVM type for a given constraint and "usual"
  /// type.
  ///
  /// \returns A pointer to a new LLVM type, possibly the same as the original
  /// on success; 0 on failure.
  virtual llvm::Type *adjustInlineAsmType(CodeGen::CodeGenFunction &CGF,
                                          StringRef Constraint,
                                          llvm::Type *Ty) const {
    return Ty;
  }

  /// Target hook to decide whether an inline asm operand can be passed
  /// by value.
  virtual bool isScalarizableAsmOperand(CodeGen::CodeGenFunction &CGF,
                                        llvm::Type *Ty) const {
    return false;
  }

  /// Adds constraints and types for result registers.
  virtual void addReturnRegisterOutputs(
      CodeGen::CodeGenFunction &CGF, CodeGen::LValue ReturnValue,
      std::string &Constraints, std::vector<llvm::Type *> &ResultRegTypes,
      std::vector<llvm::Type *> &ResultTruncRegTypes,
      std::vector<CodeGen::LValue> &ResultRegDests, std::string &AsmString,
      unsigned NumOutputs) const {}

  /// doesReturnSlotInterfereWithArgs - Return true if the target uses an
  /// argument slot for an 'sret' type.
  virtual bool doesReturnSlotInterfereWithArgs() const { return true; }

  /// Retrieve the address of a function to call immediately before
  /// calling objc_retainAutoreleasedReturnValue.  The
  /// implementation of objc_autoreleaseReturnValue sniffs the
  /// instruction stream following its return address to decide
  /// whether it's a call to objc_retainAutoreleasedReturnValue.
  /// This can be prohibitively expensive, depending on the
  /// relocation model, and so on some targets it instead sniffs for
  /// a particular instruction sequence.  This functions returns
  /// that instruction sequence in inline assembly, which will be
  /// empty if none is required.
  virtual StringRef getARCRetainAutoreleasedReturnValueMarker() const {
    return "";
  }

  /// Determine whether a call to objc_retainAutoreleasedReturnValue or
  /// objc_unsafeClaimAutoreleasedReturnValue should be marked as 'notail'.
  virtual bool markARCOptimizedReturnCallsAsNoTail() const { return false; }

  /// Return a constant used by UBSan as a signature to identify functions
  /// possessing type information, or 0 if the platform is unsupported.
  /// This magic number is invalid instruction encoding in many targets.
  virtual llvm::Constant *
  getUBSanFunctionSignature(CodeGen::CodeGenModule &CGM) const {
    return llvm::ConstantInt::get(CGM.Int32Ty, 0xc105cafe);
  }

  /// Determine whether a call to an unprototyped functions under
  /// the given calling convention should use the variadic
  /// convention or the non-variadic convention.
  ///
  /// There's a good reason to make a platform's variadic calling
  /// convention be different from its non-variadic calling
  /// convention: the non-variadic arguments can be passed in
  /// registers (better for performance), and the variadic arguments
  /// can be passed on the stack (also better for performance).  If
  /// this is done, however, unprototyped functions *must* use the
  /// non-variadic convention, because C99 states that a call
  /// through an unprototyped function type must succeed if the
  /// function was defined with a non-variadic prototype with
  /// compatible parameters.  Therefore, splitting the conventions
  /// makes it impossible to call a variadic function through an
  /// unprototyped type.  Since function prototypes came out in the
  /// late 1970s, this is probably an acceptable trade-off.
  /// Nonetheless, not all platforms are willing to make it, and in
  /// particularly x86-64 bends over backwards to make the
  /// conventions compatible.
  ///
  /// The default is false.  This is correct whenever:
  ///   - the conventions are exactly the same, because it does not
  ///     matter and the resulting IR will be somewhat prettier in
  ///     certain cases; or
  ///   - the conventions are substantively different in how they pass
  ///     arguments, because in this case using the variadic convention
  ///     will lead to C99 violations.
  ///
  /// However, some platforms make the conventions identical except
  /// for passing additional out-of-band information to a variadic
  /// function: for example, x86-64 passes the number of SSE
  /// arguments in %al.  On these platforms, it is desirable to
  /// call unprototyped functions using the variadic convention so
  /// that unprototyped calls to varargs functions still succeed.
  ///
  /// Relatedly, platforms which pass the fixed arguments to this:
  ///   A foo(B, C, D);
  /// differently than they would pass them to this:
  ///   A foo(B, C, D, ...);
  /// may need to adjust the debugger-support code in Sema to do the
  /// right thing when calling a function with no know signature.
  virtual bool isNoProtoCallVariadic(const CodeGen::CallArgList &args,
                                     const FunctionNoProtoType *fnType) const;

  /// Gets the linker options necessary to link a dependent library on this
  /// platform.
  virtual void getDependentLibraryOption(llvm::StringRef Lib,
                                         llvm::SmallString<24> &Opt) const;

  /// Gets the linker options necessary to detect object file mismatches on
  /// this platform.
  virtual void getDetectMismatchOption(llvm::StringRef Name,
                                       llvm::StringRef Value,
                                       llvm::SmallString<32> &Opt) const {}

  /// Get LLVM calling convention for OpenCL kernel.
  virtual unsigned getOpenCLKernelCallingConv() const;

  /// Get target specific null pointer.
  /// \param T is the LLVM type of the null pointer.
  /// \param QT is the clang QualType of the null pointer.
  /// \return ConstantPointerNull with the given type \p T.
  /// Each target can override it to return its own desired constant value.
  virtual llvm::Constant *getNullPointer(const CodeGen::CodeGenModule &CGM,
      llvm::PointerType *T, QualType QT) const;

  /// Get target favored AST address space of a global variable for languages
  /// other than OpenCL and CUDA.
  /// If \p D is nullptr, returns the default target favored address space
  /// for global variable.
  virtual LangAS getGlobalVarAddressSpace(CodeGenModule &CGM,
                                          const VarDecl *D) const;

  /// Get the AST address space for alloca.
  virtual LangAS getASTAllocaAddressSpace() const { return LangAS::Default; }

  Address performAddrSpaceCast(CodeGen::CodeGenFunction &CGF, Address Addr,
                               LangAS SrcAddr, LangAS DestAddr,
                               llvm::Type *DestTy,
                               bool IsNonNull = false) const;

  /// Perform address space cast of an expression of pointer type.
  /// \param V is the LLVM value to be casted to another address space.
  /// \param SrcAddr is the language address space of \p V.
  /// \param DestAddr is the targeted language address space.
  /// \param DestTy is the destination LLVM pointer type.
  /// \param IsNonNull is the flag indicating \p V is known to be non null.
  virtual llvm::Value *performAddrSpaceCast(CodeGen::CodeGenFunction &CGF,
                                            llvm::Value *V, LangAS SrcAddr,
                                            LangAS DestAddr, llvm::Type *DestTy,
                                            bool IsNonNull = false) const;

  /// Perform address space cast of a constant expression of pointer type.
  /// \param V is the LLVM constant to be casted to another address space.
  /// \param SrcAddr is the language address space of \p V.
  /// \param DestAddr is the targeted language address space.
  /// \param DestTy is the destination LLVM pointer type.
  virtual llvm::Constant *performAddrSpaceCast(CodeGenModule &CGM,
                                               llvm::Constant *V,
                                               LangAS SrcAddr, LangAS DestAddr,
                                               llvm::Type *DestTy) const;

  /// Get address space of pointer parameter for __cxa_atexit.
  virtual LangAS getAddrSpaceOfCxaAtexitPtrParam() const {
    return LangAS::Default;
  }

  /// Get the syncscope used in LLVM IR.
  virtual llvm::SyncScope::ID getLLVMSyncScopeID(const LangOptions &LangOpts,
                                                 SyncScope Scope,
                                                 llvm::AtomicOrdering Ordering,
                                                 llvm::LLVMContext &Ctx) const;

  /// Interface class for filling custom fields of a block literal for OpenCL.
  class TargetOpenCLBlockHelper {
  public:
    typedef std::pair<llvm::Value *, StringRef> ValueTy;
    TargetOpenCLBlockHelper() {}
    virtual ~TargetOpenCLBlockHelper() {}
    /// Get the custom field types for OpenCL blocks.
    virtual llvm::SmallVector<llvm::Type *, 1> getCustomFieldTypes() = 0;
    /// Get the custom field values for OpenCL blocks.
    virtual llvm::SmallVector<ValueTy, 1>
    getCustomFieldValues(CodeGenFunction &CGF, const CGBlockInfo &Info) = 0;
    virtual bool areAllCustomFieldValuesConstant(const CGBlockInfo &Info) = 0;
    /// Get the custom field values for OpenCL blocks if all values are LLVM
    /// constants.
    virtual llvm::SmallVector<llvm::Constant *, 1>
    getCustomFieldValues(CodeGenModule &CGM, const CGBlockInfo &Info) = 0;
  };
  virtual TargetOpenCLBlockHelper *getTargetOpenCLBlockHelper() const {
    return nullptr;
  }

  /// Create an OpenCL kernel for an enqueued block. The kernel function is
  /// a wrapper for the block invoke function with target-specific calling
  /// convention and ABI as an OpenCL kernel. The wrapper function accepts
  /// block context and block arguments in target-specific way and calls
  /// the original block invoke function.
  virtual llvm::Value *
  createEnqueuedBlockKernel(CodeGenFunction &CGF,
                            llvm::Function *BlockInvokeFunc,
                            llvm::Type *BlockTy) const;

  /// \return true if the target supports alias from the unmangled name to the
  /// mangled name of functions declared within an extern "C" region and marked
  /// as 'used', and having internal linkage.
  virtual bool shouldEmitStaticExternCAliases() const { return true; }

  /// \return true if annonymous zero-sized bitfields should be emitted to
  /// correctly distinguish between struct types whose memory layout is the
  /// same, but whose layout may differ when used as argument passed by value
  virtual bool shouldEmitDWARFBitFieldSeparators() const { return false; }

  virtual void setCUDAKernelCallingConvention(const FunctionType *&FT) const {}

  /// Return the device-side type for the CUDA device builtin surface type.
  virtual llvm::Type *getCUDADeviceBuiltinSurfaceDeviceType() const {
    // By default, no change from the original one.
    return nullptr;
  }
  /// Return the device-side type for the CUDA device builtin texture type.
  virtual llvm::Type *getCUDADeviceBuiltinTextureDeviceType() const {
    // By default, no change from the original one.
    return nullptr;
  }

  /// Return the WebAssembly externref reference type.
  virtual llvm::Type *getWasmExternrefReferenceType() const { return nullptr; }

  /// Return the WebAssembly funcref reference type.
  virtual llvm::Type *getWasmFuncrefReferenceType() const { return nullptr; }

  /// Emit the device-side copy of the builtin surface type.
  virtual bool emitCUDADeviceBuiltinSurfaceDeviceCopy(CodeGenFunction &CGF,
                                                      LValue Dst,
                                                      LValue Src) const {
    // DO NOTHING by default.
    return false;
  }
  /// Emit the device-side copy of the builtin texture type.
  virtual bool emitCUDADeviceBuiltinTextureDeviceCopy(CodeGenFunction &CGF,
                                                      LValue Dst,
                                                      LValue Src) const {
    // DO NOTHING by default.
    return false;
  }

  /// Return an LLVM type that corresponds to an OpenCL type.
  virtual llvm::Type *getOpenCLType(CodeGenModule &CGM, const Type *T) const {
    return nullptr;
  }

  // Set the Branch Protection Attributes of the Function accordingly to the
  // BPI. Remove attributes that contradict with current BPI.
  static void
  setBranchProtectionFnAttributes(const TargetInfo::BranchProtectionInfo &BPI,
                                  llvm::Function &F);

  // Add the Branch Protection Attributes of the FuncAttrs.
  static void
  initBranchProtectionFnAttributes(const TargetInfo::BranchProtectionInfo &BPI,
                                   llvm::AttrBuilder &FuncAttrs);

protected:
  static std::string qualifyWindowsLibrary(StringRef Lib);

  void addStackProbeTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                                     CodeGen::CodeGenModule &CGM) const;
};

std::unique_ptr<TargetCodeGenInfo>
createDefaultTargetCodeGenInfo(CodeGenModule &CGM);

enum class AArch64ABIKind {
  AAPCS = 0,
  DarwinPCS,
  Win64,
  AAPCSSoft,
  PAuthTest,
};

std::unique_ptr<TargetCodeGenInfo>
createAArch64TargetCodeGenInfo(CodeGenModule &CGM, AArch64ABIKind Kind);

std::unique_ptr<TargetCodeGenInfo>
createWindowsAArch64TargetCodeGenInfo(CodeGenModule &CGM, AArch64ABIKind K);

std::unique_ptr<TargetCodeGenInfo>
createAMDGPUTargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createARCTargetCodeGenInfo(CodeGenModule &CGM);

enum class ARMABIKind {
  APCS = 0,
  AAPCS = 1,
  AAPCS_VFP = 2,
  AAPCS16_VFP = 3,
};

std::unique_ptr<TargetCodeGenInfo>
createARMTargetCodeGenInfo(CodeGenModule &CGM, ARMABIKind Kind);

std::unique_ptr<TargetCodeGenInfo>
createWindowsARMTargetCodeGenInfo(CodeGenModule &CGM, ARMABIKind K);

std::unique_ptr<TargetCodeGenInfo>
createAVRTargetCodeGenInfo(CodeGenModule &CGM, unsigned NPR, unsigned NRR);

std::unique_ptr<TargetCodeGenInfo>
createBPFTargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createCSKYTargetCodeGenInfo(CodeGenModule &CGM, unsigned FLen);

std::unique_ptr<TargetCodeGenInfo>
createHexagonTargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createLanaiTargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createLoongArchTargetCodeGenInfo(CodeGenModule &CGM, unsigned GRLen,
                                 unsigned FLen);

std::unique_ptr<TargetCodeGenInfo>
createM68kTargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createMIPSTargetCodeGenInfo(CodeGenModule &CGM, bool IsOS32);

std::unique_ptr<TargetCodeGenInfo>
createMSP430TargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createNVPTXTargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createPNaClTargetCodeGenInfo(CodeGenModule &CGM);

enum class PPC64_SVR4_ABIKind {
  ELFv1 = 0,
  ELFv2,
};

std::unique_ptr<TargetCodeGenInfo>
createAIXTargetCodeGenInfo(CodeGenModule &CGM, bool Is64Bit);

std::unique_ptr<TargetCodeGenInfo>
createPPC32TargetCodeGenInfo(CodeGenModule &CGM, bool SoftFloatABI);

std::unique_ptr<TargetCodeGenInfo>
createPPC64TargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createPPC64_SVR4_TargetCodeGenInfo(CodeGenModule &CGM, PPC64_SVR4_ABIKind Kind,
                                   bool SoftFloatABI);

std::unique_ptr<TargetCodeGenInfo>
createRISCVTargetCodeGenInfo(CodeGenModule &CGM, unsigned XLen, unsigned FLen,
                             bool EABI);

std::unique_ptr<TargetCodeGenInfo>
createCommonSPIRTargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createSPIRVTargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createSparcV8TargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createSparcV9TargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createSystemZTargetCodeGenInfo(CodeGenModule &CGM, bool HasVector,
                               bool SoftFloatABI);

std::unique_ptr<TargetCodeGenInfo>
createTCETargetCodeGenInfo(CodeGenModule &CGM);

std::unique_ptr<TargetCodeGenInfo>
createVETargetCodeGenInfo(CodeGenModule &CGM);

enum class WebAssemblyABIKind {
  MVP = 0,
  ExperimentalMV = 1,
};

std::unique_ptr<TargetCodeGenInfo>
createWebAssemblyTargetCodeGenInfo(CodeGenModule &CGM, WebAssemblyABIKind K);

/// The AVX ABI level for X86 targets.
enum class X86AVXABILevel {
  None,
  AVX,
  AVX512,
};

std::unique_ptr<TargetCodeGenInfo> createX86_32TargetCodeGenInfo(
    CodeGenModule &CGM, bool DarwinVectorABI, bool Win32StructABI,
    unsigned NumRegisterParameters, bool SoftFloatABI);

std::unique_ptr<TargetCodeGenInfo>
createWinX86_32TargetCodeGenInfo(CodeGenModule &CGM, bool DarwinVectorABI,
                                 bool Win32StructABI,
                                 unsigned NumRegisterParameters);

std::unique_ptr<TargetCodeGenInfo>
createX86_64TargetCodeGenInfo(CodeGenModule &CGM, X86AVXABILevel AVXLevel);

std::unique_ptr<TargetCodeGenInfo>
createWinX86_64TargetCodeGenInfo(CodeGenModule &CGM, X86AVXABILevel AVXLevel);

std::unique_ptr<TargetCodeGenInfo>
createXCoreTargetCodeGenInfo(CodeGenModule &CGM);

} // namespace CodeGen
} // namespace clang

#endif // LLVM_CLANG_LIB_CODEGEN_TARGETINFO_H
