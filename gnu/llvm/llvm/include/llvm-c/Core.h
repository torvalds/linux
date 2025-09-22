/*===-- llvm-c/Core.h - Core Library C Interface ------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMCore.a, which implements    *|
|* the LLVM intermediate representation.                                      *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_CORE_H
#define LLVM_C_CORE_H

#include "llvm-c/Deprecated.h"
#include "llvm-c/ErrorHandling.h"
#include "llvm-c/ExternC.h"

#include "llvm-c/Types.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMC LLVM-C: C interface to LLVM
 *
 * This module exposes parts of the LLVM library as a C API.
 *
 * @{
 */

/**
 * @defgroup LLVMCTransforms Transforms
 */

/**
 * @defgroup LLVMCCore Core
 *
 * This modules provide an interface to libLLVMCore, which implements
 * the LLVM intermediate representation as well as other related types
 * and utilities.
 *
 * Many exotic languages can interoperate with C code but have a harder time
 * with C++ due to name mangling. So in addition to C, this interface enables
 * tools written in such languages.
 *
 * @{
 */

/**
 * @defgroup LLVMCCoreTypes Types and Enumerations
 *
 * @{
 */

/// External users depend on the following values being stable. It is not safe
/// to reorder them.
typedef enum {
  /* Terminator Instructions */
  LLVMRet            = 1,
  LLVMBr             = 2,
  LLVMSwitch         = 3,
  LLVMIndirectBr     = 4,
  LLVMInvoke         = 5,
  /* removed 6 due to API changes */
  LLVMUnreachable    = 7,
  LLVMCallBr         = 67,

  /* Standard Unary Operators */
  LLVMFNeg           = 66,

  /* Standard Binary Operators */
  LLVMAdd            = 8,
  LLVMFAdd           = 9,
  LLVMSub            = 10,
  LLVMFSub           = 11,
  LLVMMul            = 12,
  LLVMFMul           = 13,
  LLVMUDiv           = 14,
  LLVMSDiv           = 15,
  LLVMFDiv           = 16,
  LLVMURem           = 17,
  LLVMSRem           = 18,
  LLVMFRem           = 19,

  /* Logical Operators */
  LLVMShl            = 20,
  LLVMLShr           = 21,
  LLVMAShr           = 22,
  LLVMAnd            = 23,
  LLVMOr             = 24,
  LLVMXor            = 25,

  /* Memory Operators */
  LLVMAlloca         = 26,
  LLVMLoad           = 27,
  LLVMStore          = 28,
  LLVMGetElementPtr  = 29,

  /* Cast Operators */
  LLVMTrunc          = 30,
  LLVMZExt           = 31,
  LLVMSExt           = 32,
  LLVMFPToUI         = 33,
  LLVMFPToSI         = 34,
  LLVMUIToFP         = 35,
  LLVMSIToFP         = 36,
  LLVMFPTrunc        = 37,
  LLVMFPExt          = 38,
  LLVMPtrToInt       = 39,
  LLVMIntToPtr       = 40,
  LLVMBitCast        = 41,
  LLVMAddrSpaceCast  = 60,

  /* Other Operators */
  LLVMICmp           = 42,
  LLVMFCmp           = 43,
  LLVMPHI            = 44,
  LLVMCall           = 45,
  LLVMSelect         = 46,
  LLVMUserOp1        = 47,
  LLVMUserOp2        = 48,
  LLVMVAArg          = 49,
  LLVMExtractElement = 50,
  LLVMInsertElement  = 51,
  LLVMShuffleVector  = 52,
  LLVMExtractValue   = 53,
  LLVMInsertValue    = 54,
  LLVMFreeze         = 68,

  /* Atomic operators */
  LLVMFence          = 55,
  LLVMAtomicCmpXchg  = 56,
  LLVMAtomicRMW      = 57,

  /* Exception Handling Operators */
  LLVMResume         = 58,
  LLVMLandingPad     = 59,
  LLVMCleanupRet     = 61,
  LLVMCatchRet       = 62,
  LLVMCatchPad       = 63,
  LLVMCleanupPad     = 64,
  LLVMCatchSwitch    = 65
} LLVMOpcode;

typedef enum {
  LLVMVoidTypeKind,      /**< type with no size */
  LLVMHalfTypeKind,      /**< 16 bit floating point type */
  LLVMFloatTypeKind,     /**< 32 bit floating point type */
  LLVMDoubleTypeKind,    /**< 64 bit floating point type */
  LLVMX86_FP80TypeKind,  /**< 80 bit floating point type (X87) */
  LLVMFP128TypeKind,     /**< 128 bit floating point type (112-bit mantissa)*/
  LLVMPPC_FP128TypeKind, /**< 128 bit floating point type (two 64-bits) */
  LLVMLabelTypeKind,     /**< Labels */
  LLVMIntegerTypeKind,   /**< Arbitrary bit width integers */
  LLVMFunctionTypeKind,  /**< Functions */
  LLVMStructTypeKind,    /**< Structures */
  LLVMArrayTypeKind,     /**< Arrays */
  LLVMPointerTypeKind,   /**< Pointers */
  LLVMVectorTypeKind,    /**< Fixed width SIMD vector type */
  LLVMMetadataTypeKind,  /**< Metadata */
  LLVMX86_MMXTypeKind,   /**< X86 MMX */
  LLVMTokenTypeKind,     /**< Tokens */
  LLVMScalableVectorTypeKind, /**< Scalable SIMD vector type */
  LLVMBFloatTypeKind,    /**< 16 bit brain floating point type */
  LLVMX86_AMXTypeKind,   /**< X86 AMX */
  LLVMTargetExtTypeKind, /**< Target extension type */
} LLVMTypeKind;

typedef enum {
  LLVMExternalLinkage,    /**< Externally visible function */
  LLVMAvailableExternallyLinkage,
  LLVMLinkOnceAnyLinkage, /**< Keep one copy of function when linking (inline)*/
  LLVMLinkOnceODRLinkage, /**< Same, but only replaced by something
                            equivalent. */
  LLVMLinkOnceODRAutoHideLinkage, /**< Obsolete */
  LLVMWeakAnyLinkage,     /**< Keep one copy of function when linking (weak) */
  LLVMWeakODRLinkage,     /**< Same, but only replaced by something
                            equivalent. */
  LLVMAppendingLinkage,   /**< Special purpose, only applies to global arrays */
  LLVMInternalLinkage,    /**< Rename collisions when linking (static
                               functions) */
  LLVMPrivateLinkage,     /**< Like Internal, but omit from symbol table */
  LLVMDLLImportLinkage,   /**< Obsolete */
  LLVMDLLExportLinkage,   /**< Obsolete */
  LLVMExternalWeakLinkage,/**< ExternalWeak linkage description */
  LLVMGhostLinkage,       /**< Obsolete */
  LLVMCommonLinkage,      /**< Tentative definitions */
  LLVMLinkerPrivateLinkage, /**< Like Private, but linker removes. */
  LLVMLinkerPrivateWeakLinkage /**< Like LinkerPrivate, but is weak. */
} LLVMLinkage;

typedef enum {
  LLVMDefaultVisibility,  /**< The GV is visible */
  LLVMHiddenVisibility,   /**< The GV is hidden */
  LLVMProtectedVisibility /**< The GV is protected */
} LLVMVisibility;

typedef enum {
  LLVMNoUnnamedAddr,    /**< Address of the GV is significant. */
  LLVMLocalUnnamedAddr, /**< Address of the GV is locally insignificant. */
  LLVMGlobalUnnamedAddr /**< Address of the GV is globally insignificant. */
} LLVMUnnamedAddr;

typedef enum {
  LLVMDefaultStorageClass   = 0,
  LLVMDLLImportStorageClass = 1, /**< Function to be imported from DLL. */
  LLVMDLLExportStorageClass = 2  /**< Function to be accessible from DLL. */
} LLVMDLLStorageClass;

typedef enum {
  LLVMCCallConv             = 0,
  LLVMFastCallConv          = 8,
  LLVMColdCallConv          = 9,
  LLVMGHCCallConv           = 10,
  LLVMHiPECallConv          = 11,
  LLVMAnyRegCallConv        = 13,
  LLVMPreserveMostCallConv  = 14,
  LLVMPreserveAllCallConv   = 15,
  LLVMSwiftCallConv         = 16,
  LLVMCXXFASTTLSCallConv    = 17,
  LLVMX86StdcallCallConv    = 64,
  LLVMX86FastcallCallConv   = 65,
  LLVMARMAPCSCallConv       = 66,
  LLVMARMAAPCSCallConv      = 67,
  LLVMARMAAPCSVFPCallConv   = 68,
  LLVMMSP430INTRCallConv    = 69,
  LLVMX86ThisCallCallConv   = 70,
  LLVMPTXKernelCallConv     = 71,
  LLVMPTXDeviceCallConv     = 72,
  LLVMSPIRFUNCCallConv      = 75,
  LLVMSPIRKERNELCallConv    = 76,
  LLVMIntelOCLBICallConv    = 77,
  LLVMX8664SysVCallConv     = 78,
  LLVMWin64CallConv         = 79,
  LLVMX86VectorCallCallConv = 80,
  LLVMHHVMCallConv          = 81,
  LLVMHHVMCCallConv         = 82,
  LLVMX86INTRCallConv       = 83,
  LLVMAVRINTRCallConv       = 84,
  LLVMAVRSIGNALCallConv     = 85,
  LLVMAVRBUILTINCallConv    = 86,
  LLVMAMDGPUVSCallConv      = 87,
  LLVMAMDGPUGSCallConv      = 88,
  LLVMAMDGPUPSCallConv      = 89,
  LLVMAMDGPUCSCallConv      = 90,
  LLVMAMDGPUKERNELCallConv  = 91,
  LLVMX86RegCallCallConv    = 92,
  LLVMAMDGPUHSCallConv      = 93,
  LLVMMSP430BUILTINCallConv = 94,
  LLVMAMDGPULSCallConv      = 95,
  LLVMAMDGPUESCallConv      = 96
} LLVMCallConv;

typedef enum {
  LLVMArgumentValueKind,
  LLVMBasicBlockValueKind,
  LLVMMemoryUseValueKind,
  LLVMMemoryDefValueKind,
  LLVMMemoryPhiValueKind,

  LLVMFunctionValueKind,
  LLVMGlobalAliasValueKind,
  LLVMGlobalIFuncValueKind,
  LLVMGlobalVariableValueKind,
  LLVMBlockAddressValueKind,
  LLVMConstantExprValueKind,
  LLVMConstantArrayValueKind,
  LLVMConstantStructValueKind,
  LLVMConstantVectorValueKind,

  LLVMUndefValueValueKind,
  LLVMConstantAggregateZeroValueKind,
  LLVMConstantDataArrayValueKind,
  LLVMConstantDataVectorValueKind,
  LLVMConstantIntValueKind,
  LLVMConstantFPValueKind,
  LLVMConstantPointerNullValueKind,
  LLVMConstantTokenNoneValueKind,

  LLVMMetadataAsValueValueKind,
  LLVMInlineAsmValueKind,

  LLVMInstructionValueKind,
  LLVMPoisonValueValueKind,
  LLVMConstantTargetNoneValueKind,
  LLVMConstantPtrAuthValueKind,
} LLVMValueKind;

typedef enum {
  LLVMIntEQ = 32, /**< equal */
  LLVMIntNE,      /**< not equal */
  LLVMIntUGT,     /**< unsigned greater than */
  LLVMIntUGE,     /**< unsigned greater or equal */
  LLVMIntULT,     /**< unsigned less than */
  LLVMIntULE,     /**< unsigned less or equal */
  LLVMIntSGT,     /**< signed greater than */
  LLVMIntSGE,     /**< signed greater or equal */
  LLVMIntSLT,     /**< signed less than */
  LLVMIntSLE      /**< signed less or equal */
} LLVMIntPredicate;

typedef enum {
  LLVMRealPredicateFalse, /**< Always false (always folded) */
  LLVMRealOEQ,            /**< True if ordered and equal */
  LLVMRealOGT,            /**< True if ordered and greater than */
  LLVMRealOGE,            /**< True if ordered and greater than or equal */
  LLVMRealOLT,            /**< True if ordered and less than */
  LLVMRealOLE,            /**< True if ordered and less than or equal */
  LLVMRealONE,            /**< True if ordered and operands are unequal */
  LLVMRealORD,            /**< True if ordered (no nans) */
  LLVMRealUNO,            /**< True if unordered: isnan(X) | isnan(Y) */
  LLVMRealUEQ,            /**< True if unordered or equal */
  LLVMRealUGT,            /**< True if unordered or greater than */
  LLVMRealUGE,            /**< True if unordered, greater than, or equal */
  LLVMRealULT,            /**< True if unordered or less than */
  LLVMRealULE,            /**< True if unordered, less than, or equal */
  LLVMRealUNE,            /**< True if unordered or not equal */
  LLVMRealPredicateTrue   /**< Always true (always folded) */
} LLVMRealPredicate;

typedef enum {
  LLVMLandingPadCatch,    /**< A catch clause   */
  LLVMLandingPadFilter    /**< A filter clause  */
} LLVMLandingPadClauseTy;

typedef enum {
  LLVMNotThreadLocal = 0,
  LLVMGeneralDynamicTLSModel,
  LLVMLocalDynamicTLSModel,
  LLVMInitialExecTLSModel,
  LLVMLocalExecTLSModel
} LLVMThreadLocalMode;

typedef enum {
  LLVMAtomicOrderingNotAtomic = 0, /**< A load or store which is not atomic */
  LLVMAtomicOrderingUnordered = 1, /**< Lowest level of atomicity, guarantees
                                     somewhat sane results, lock free. */
  LLVMAtomicOrderingMonotonic = 2, /**< guarantees that if you take all the
                                     operations affecting a specific address,
                                     a consistent ordering exists */
  LLVMAtomicOrderingAcquire = 4, /**< Acquire provides a barrier of the sort
                                   necessary to acquire a lock to access other
                                   memory with normal loads and stores. */
  LLVMAtomicOrderingRelease = 5, /**< Release is similar to Acquire, but with
                                   a barrier of the sort necessary to release
                                   a lock. */
  LLVMAtomicOrderingAcquireRelease = 6, /**< provides both an Acquire and a
                                          Release barrier (for fences and
                                          operations which both read and write
                                           memory). */
  LLVMAtomicOrderingSequentiallyConsistent = 7 /**< provides Acquire semantics
                                                 for loads and Release
                                                 semantics for stores.
                                                 Additionally, it guarantees
                                                 that a total ordering exists
                                                 between all
                                                 SequentiallyConsistent
                                                 operations. */
} LLVMAtomicOrdering;

typedef enum {
  LLVMAtomicRMWBinOpXchg, /**< Set the new value and return the one old */
  LLVMAtomicRMWBinOpAdd,  /**< Add a value and return the old one */
  LLVMAtomicRMWBinOpSub,  /**< Subtract a value and return the old one */
  LLVMAtomicRMWBinOpAnd,  /**< And a value and return the old one */
  LLVMAtomicRMWBinOpNand, /**< Not-And a value and return the old one */
  LLVMAtomicRMWBinOpOr,   /**< OR a value and return the old one */
  LLVMAtomicRMWBinOpXor,  /**< Xor a value and return the old one */
  LLVMAtomicRMWBinOpMax,  /**< Sets the value if it's greater than the
                            original using a signed comparison and return
                            the old one */
  LLVMAtomicRMWBinOpMin,  /**< Sets the value if it's Smaller than the
                            original using a signed comparison and return
                            the old one */
  LLVMAtomicRMWBinOpUMax, /**< Sets the value if it's greater than the
                           original using an unsigned comparison and return
                           the old one */
  LLVMAtomicRMWBinOpUMin, /**< Sets the value if it's greater than the
                            original using an unsigned comparison and return
                            the old one */
  LLVMAtomicRMWBinOpFAdd, /**< Add a floating point value and return the
                            old one */
  LLVMAtomicRMWBinOpFSub, /**< Subtract a floating point value and return the
                          old one */
  LLVMAtomicRMWBinOpFMax, /**< Sets the value if it's greater than the
                           original using an floating point comparison and
                           return the old one */
  LLVMAtomicRMWBinOpFMin, /**< Sets the value if it's smaller than the
                           original using an floating point comparison and
                           return the old one */
  LLVMAtomicRMWBinOpUIncWrap, /**< Increments the value, wrapping back to zero
                               when incremented above input value */
  LLVMAtomicRMWBinOpUDecWrap, /**< Decrements the value, wrapping back to
                               the input value when decremented below zero */
} LLVMAtomicRMWBinOp;

typedef enum {
    LLVMDSError,
    LLVMDSWarning,
    LLVMDSRemark,
    LLVMDSNote
} LLVMDiagnosticSeverity;

typedef enum {
  LLVMInlineAsmDialectATT,
  LLVMInlineAsmDialectIntel
} LLVMInlineAsmDialect;

typedef enum {
  /**
   * Emits an error if two values disagree, otherwise the resulting value is
   * that of the operands.
   *
   * @see Module::ModFlagBehavior::Error
   */
  LLVMModuleFlagBehaviorError,
  /**
   * Emits a warning if two values disagree. The result value will be the
   * operand for the flag from the first module being linked.
   *
   * @see Module::ModFlagBehavior::Warning
   */
  LLVMModuleFlagBehaviorWarning,
  /**
   * Adds a requirement that another module flag be present and have a
   * specified value after linking is performed. The value must be a metadata
   * pair, where the first element of the pair is the ID of the module flag
   * to be restricted, and the second element of the pair is the value the
   * module flag should be restricted to. This behavior can be used to
   * restrict the allowable results (via triggering of an error) of linking
   * IDs with the **Override** behavior.
   *
   * @see Module::ModFlagBehavior::Require
   */
  LLVMModuleFlagBehaviorRequire,
  /**
   * Uses the specified value, regardless of the behavior or value of the
   * other module. If both modules specify **Override**, but the values
   * differ, an error will be emitted.
   *
   * @see Module::ModFlagBehavior::Override
   */
  LLVMModuleFlagBehaviorOverride,
  /**
   * Appends the two values, which are required to be metadata nodes.
   *
   * @see Module::ModFlagBehavior::Append
   */
  LLVMModuleFlagBehaviorAppend,
  /**
   * Appends the two values, which are required to be metadata
   * nodes. However, duplicate entries in the second list are dropped
   * during the append operation.
   *
   * @see Module::ModFlagBehavior::AppendUnique
   */
  LLVMModuleFlagBehaviorAppendUnique,
} LLVMModuleFlagBehavior;

/**
 * Attribute index are either LLVMAttributeReturnIndex,
 * LLVMAttributeFunctionIndex or a parameter number from 1 to N.
 */
enum {
  LLVMAttributeReturnIndex = 0U,
  // ISO C restricts enumerator values to range of 'int'
  // (4294967295 is too large)
  // LLVMAttributeFunctionIndex = ~0U,
  LLVMAttributeFunctionIndex = -1,
};

typedef unsigned LLVMAttributeIndex;

/**
 * Tail call kind for LLVMSetTailCallKind and LLVMGetTailCallKind.
 *
 * Note that 'musttail' implies 'tail'.
 *
 * @see CallInst::TailCallKind
 */
typedef enum {
  LLVMTailCallKindNone = 0,
  LLVMTailCallKindTail = 1,
  LLVMTailCallKindMustTail = 2,
  LLVMTailCallKindNoTail = 3,
} LLVMTailCallKind;

enum {
  LLVMFastMathAllowReassoc = (1 << 0),
  LLVMFastMathNoNaNs = (1 << 1),
  LLVMFastMathNoInfs = (1 << 2),
  LLVMFastMathNoSignedZeros = (1 << 3),
  LLVMFastMathAllowReciprocal = (1 << 4),
  LLVMFastMathAllowContract = (1 << 5),
  LLVMFastMathApproxFunc = (1 << 6),
  LLVMFastMathNone = 0,
  LLVMFastMathAll = LLVMFastMathAllowReassoc | LLVMFastMathNoNaNs |
                    LLVMFastMathNoInfs | LLVMFastMathNoSignedZeros |
                    LLVMFastMathAllowReciprocal | LLVMFastMathAllowContract |
                    LLVMFastMathApproxFunc,
};

/**
 * Flags to indicate what fast-math-style optimizations are allowed
 * on operations.
 *
 * See https://llvm.org/docs/LangRef.html#fast-math-flags
 */
typedef unsigned LLVMFastMathFlags;

enum {
  LLVMGEPFlagInBounds = (1 << 0),
  LLVMGEPFlagNUSW = (1 << 1),
  LLVMGEPFlagNUW = (1 << 2),
};

/**
 * Flags that constrain the allowed wrap semantics of a getelementptr
 * instruction.
 *
 * See https://llvm.org/docs/LangRef.html#getelementptr-instruction
 */
typedef unsigned LLVMGEPNoWrapFlags;

/**
 * @}
 */

/** Deallocate and destroy all ManagedStatic variables.
    @see llvm::llvm_shutdown
    @see ManagedStatic */
void LLVMShutdown(void);

/*===-- Version query -----------------------------------------------------===*/

/**
 * Return the major, minor, and patch version of LLVM
 *
 * The version components are returned via the function's three output
 * parameters or skipped if a NULL pointer was supplied.
 */
void LLVMGetVersion(unsigned *Major, unsigned *Minor, unsigned *Patch);

/*===-- Error handling ----------------------------------------------------===*/

char *LLVMCreateMessage(const char *Message);
void LLVMDisposeMessage(char *Message);

/**
 * @defgroup LLVMCCoreContext Contexts
 *
 * Contexts are execution states for the core LLVM IR system.
 *
 * Most types are tied to a context instance. Multiple contexts can
 * exist simultaneously. A single context is not thread safe. However,
 * different contexts can execute on different threads simultaneously.
 *
 * @{
 */

typedef void (*LLVMDiagnosticHandler)(LLVMDiagnosticInfoRef, void *);
typedef void (*LLVMYieldCallback)(LLVMContextRef, void *);

/**
 * Create a new context.
 *
 * Every call to this function should be paired with a call to
 * LLVMContextDispose() or the context will leak memory.
 */
LLVMContextRef LLVMContextCreate(void);

/**
 * Obtain the global context instance.
 */
LLVMContextRef LLVMGetGlobalContext(void);

/**
 * Set the diagnostic handler for this context.
 */
void LLVMContextSetDiagnosticHandler(LLVMContextRef C,
                                     LLVMDiagnosticHandler Handler,
                                     void *DiagnosticContext);

/**
 * Get the diagnostic handler of this context.
 */
LLVMDiagnosticHandler LLVMContextGetDiagnosticHandler(LLVMContextRef C);

/**
 * Get the diagnostic context of this context.
 */
void *LLVMContextGetDiagnosticContext(LLVMContextRef C);

/**
 * Set the yield callback function for this context.
 *
 * @see LLVMContext::setYieldCallback()
 */
void LLVMContextSetYieldCallback(LLVMContextRef C, LLVMYieldCallback Callback,
                                 void *OpaqueHandle);

/**
 * Retrieve whether the given context is set to discard all value names.
 *
 * @see LLVMContext::shouldDiscardValueNames()
 */
LLVMBool LLVMContextShouldDiscardValueNames(LLVMContextRef C);

/**
 * Set whether the given context discards all value names.
 *
 * If true, only the names of GlobalValue objects will be available in the IR.
 * This can be used to save memory and runtime, especially in release mode.
 *
 * @see LLVMContext::setDiscardValueNames()
 */
void LLVMContextSetDiscardValueNames(LLVMContextRef C, LLVMBool Discard);

/**
 * Destroy a context instance.
 *
 * This should be called for every call to LLVMContextCreate() or memory
 * will be leaked.
 */
void LLVMContextDispose(LLVMContextRef C);

/**
 * Return a string representation of the DiagnosticInfo. Use
 * LLVMDisposeMessage to free the string.
 *
 * @see DiagnosticInfo::print()
 */
char *LLVMGetDiagInfoDescription(LLVMDiagnosticInfoRef DI);

/**
 * Return an enum LLVMDiagnosticSeverity.
 *
 * @see DiagnosticInfo::getSeverity()
 */
LLVMDiagnosticSeverity LLVMGetDiagInfoSeverity(LLVMDiagnosticInfoRef DI);

unsigned LLVMGetMDKindIDInContext(LLVMContextRef C, const char *Name,
                                  unsigned SLen);
unsigned LLVMGetMDKindID(const char *Name, unsigned SLen);

/**
 * Return an unique id given the name of a enum attribute,
 * or 0 if no attribute by that name exists.
 *
 * See http://llvm.org/docs/LangRef.html#parameter-attributes
 * and http://llvm.org/docs/LangRef.html#function-attributes
 * for the list of available attributes.
 *
 * NB: Attribute names and/or id are subject to change without
 * going through the C API deprecation cycle.
 */
unsigned LLVMGetEnumAttributeKindForName(const char *Name, size_t SLen);
unsigned LLVMGetLastEnumAttributeKind(void);

/**
 * Create an enum attribute.
 */
LLVMAttributeRef LLVMCreateEnumAttribute(LLVMContextRef C, unsigned KindID,
                                         uint64_t Val);

/**
 * Get the unique id corresponding to the enum attribute
 * passed as argument.
 */
unsigned LLVMGetEnumAttributeKind(LLVMAttributeRef A);

/**
 * Get the enum attribute's value. 0 is returned if none exists.
 */
uint64_t LLVMGetEnumAttributeValue(LLVMAttributeRef A);

/**
 * Create a type attribute
 */
LLVMAttributeRef LLVMCreateTypeAttribute(LLVMContextRef C, unsigned KindID,
                                         LLVMTypeRef type_ref);

/**
 * Get the type attribute's value.
 */
LLVMTypeRef LLVMGetTypeAttributeValue(LLVMAttributeRef A);

/**
 * Create a ConstantRange attribute.
 *
 * LowerWords and UpperWords need to be NumBits divided by 64 rounded up
 * elements long.
 */
LLVMAttributeRef LLVMCreateConstantRangeAttribute(LLVMContextRef C,
                                                  unsigned KindID,
                                                  unsigned NumBits,
                                                  const uint64_t LowerWords[],
                                                  const uint64_t UpperWords[]);

/**
 * Create a string attribute.
 */
LLVMAttributeRef LLVMCreateStringAttribute(LLVMContextRef C,
                                           const char *K, unsigned KLength,
                                           const char *V, unsigned VLength);

/**
 * Get the string attribute's kind.
 */
const char *LLVMGetStringAttributeKind(LLVMAttributeRef A, unsigned *Length);

/**
 * Get the string attribute's value.
 */
const char *LLVMGetStringAttributeValue(LLVMAttributeRef A, unsigned *Length);

/**
 * Check for the different types of attributes.
 */
LLVMBool LLVMIsEnumAttribute(LLVMAttributeRef A);
LLVMBool LLVMIsStringAttribute(LLVMAttributeRef A);
LLVMBool LLVMIsTypeAttribute(LLVMAttributeRef A);

/**
 * Obtain a Type from a context by its registered name.
 */
LLVMTypeRef LLVMGetTypeByName2(LLVMContextRef C, const char *Name);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreModule Modules
 *
 * Modules represent the top-level structure in an LLVM program. An LLVM
 * module is effectively a translation unit or a collection of
 * translation units merged together.
 *
 * @{
 */

/**
 * Create a new, empty module in the global context.
 *
 * This is equivalent to calling LLVMModuleCreateWithNameInContext with
 * LLVMGetGlobalContext() as the context parameter.
 *
 * Every invocation should be paired with LLVMDisposeModule() or memory
 * will be leaked.
 */
LLVMModuleRef LLVMModuleCreateWithName(const char *ModuleID);

/**
 * Create a new, empty module in a specific context.
 *
 * Every invocation should be paired with LLVMDisposeModule() or memory
 * will be leaked.
 */
LLVMModuleRef LLVMModuleCreateWithNameInContext(const char *ModuleID,
                                                LLVMContextRef C);
/**
 * Return an exact copy of the specified module.
 */
LLVMModuleRef LLVMCloneModule(LLVMModuleRef M);

/**
 * Destroy a module instance.
 *
 * This must be called for every created module or memory will be
 * leaked.
 */
void LLVMDisposeModule(LLVMModuleRef M);

/**
 * Soon to be deprecated.
 * See https://llvm.org/docs/RemoveDIsDebugInfo.html#c-api-changes
 *
 * Returns true if the module is in the new debug info mode which uses
 * non-instruction debug records instead of debug intrinsics for variable
 * location tracking.
 */
LLVMBool LLVMIsNewDbgInfoFormat(LLVMModuleRef M);

/**
 * Soon to be deprecated.
 * See https://llvm.org/docs/RemoveDIsDebugInfo.html#c-api-changes
 *
 * Convert module into desired debug info format.
 */
void LLVMSetIsNewDbgInfoFormat(LLVMModuleRef M, LLVMBool UseNewFormat);

/**
 * Obtain the identifier of a module.
 *
 * @param M Module to obtain identifier of
 * @param Len Out parameter which holds the length of the returned string.
 * @return The identifier of M.
 * @see Module::getModuleIdentifier()
 */
const char *LLVMGetModuleIdentifier(LLVMModuleRef M, size_t *Len);

/**
 * Set the identifier of a module to a string Ident with length Len.
 *
 * @param M The module to set identifier
 * @param Ident The string to set M's identifier to
 * @param Len Length of Ident
 * @see Module::setModuleIdentifier()
 */
void LLVMSetModuleIdentifier(LLVMModuleRef M, const char *Ident, size_t Len);

/**
 * Obtain the module's original source file name.
 *
 * @param M Module to obtain the name of
 * @param Len Out parameter which holds the length of the returned string
 * @return The original source file name of M
 * @see Module::getSourceFileName()
 */
const char *LLVMGetSourceFileName(LLVMModuleRef M, size_t *Len);

/**
 * Set the original source file name of a module to a string Name with length
 * Len.
 *
 * @param M The module to set the source file name of
 * @param Name The string to set M's source file name to
 * @param Len Length of Name
 * @see Module::setSourceFileName()
 */
void LLVMSetSourceFileName(LLVMModuleRef M, const char *Name, size_t Len);

/**
 * Obtain the data layout for a module.
 *
 * @see Module::getDataLayoutStr()
 *
 * LLVMGetDataLayout is DEPRECATED, as the name is not only incorrect,
 * but match the name of another method on the module. Prefer the use
 * of LLVMGetDataLayoutStr, which is not ambiguous.
 */
const char *LLVMGetDataLayoutStr(LLVMModuleRef M);
const char *LLVMGetDataLayout(LLVMModuleRef M);

/**
 * Set the data layout for a module.
 *
 * @see Module::setDataLayout()
 */
void LLVMSetDataLayout(LLVMModuleRef M, const char *DataLayoutStr);

/**
 * Obtain the target triple for a module.
 *
 * @see Module::getTargetTriple()
 */
const char *LLVMGetTarget(LLVMModuleRef M);

/**
 * Set the target triple for a module.
 *
 * @see Module::setTargetTriple()
 */
void LLVMSetTarget(LLVMModuleRef M, const char *Triple);

/**
 * Returns the module flags as an array of flag-key-value triples.  The caller
 * is responsible for freeing this array by calling
 * \c LLVMDisposeModuleFlagsMetadata.
 *
 * @see Module::getModuleFlagsMetadata()
 */
LLVMModuleFlagEntry *LLVMCopyModuleFlagsMetadata(LLVMModuleRef M, size_t *Len);

/**
 * Destroys module flags metadata entries.
 */
void LLVMDisposeModuleFlagsMetadata(LLVMModuleFlagEntry *Entries);

/**
 * Returns the flag behavior for a module flag entry at a specific index.
 *
 * @see Module::ModuleFlagEntry::Behavior
 */
LLVMModuleFlagBehavior
LLVMModuleFlagEntriesGetFlagBehavior(LLVMModuleFlagEntry *Entries,
                                     unsigned Index);

/**
 * Returns the key for a module flag entry at a specific index.
 *
 * @see Module::ModuleFlagEntry::Key
 */
const char *LLVMModuleFlagEntriesGetKey(LLVMModuleFlagEntry *Entries,
                                        unsigned Index, size_t *Len);

/**
 * Returns the metadata for a module flag entry at a specific index.
 *
 * @see Module::ModuleFlagEntry::Val
 */
LLVMMetadataRef LLVMModuleFlagEntriesGetMetadata(LLVMModuleFlagEntry *Entries,
                                                 unsigned Index);

/**
 * Add a module-level flag to the module-level flags metadata if it doesn't
 * already exist.
 *
 * @see Module::getModuleFlag()
 */
LLVMMetadataRef LLVMGetModuleFlag(LLVMModuleRef M,
                                  const char *Key, size_t KeyLen);

/**
 * Add a module-level flag to the module-level flags metadata if it doesn't
 * already exist.
 *
 * @see Module::addModuleFlag()
 */
void LLVMAddModuleFlag(LLVMModuleRef M, LLVMModuleFlagBehavior Behavior,
                       const char *Key, size_t KeyLen,
                       LLVMMetadataRef Val);

/**
 * Dump a representation of a module to stderr.
 *
 * @see Module::dump()
 */
void LLVMDumpModule(LLVMModuleRef M);

/**
 * Print a representation of a module to a file. The ErrorMessage needs to be
 * disposed with LLVMDisposeMessage. Returns 0 on success, 1 otherwise.
 *
 * @see Module::print()
 */
LLVMBool LLVMPrintModuleToFile(LLVMModuleRef M, const char *Filename,
                               char **ErrorMessage);

/**
 * Return a string representation of the module. Use
 * LLVMDisposeMessage to free the string.
 *
 * @see Module::print()
 */
char *LLVMPrintModuleToString(LLVMModuleRef M);

/**
 * Get inline assembly for a module.
 *
 * @see Module::getModuleInlineAsm()
 */
const char *LLVMGetModuleInlineAsm(LLVMModuleRef M, size_t *Len);

/**
 * Set inline assembly for a module.
 *
 * @see Module::setModuleInlineAsm()
 */
void LLVMSetModuleInlineAsm2(LLVMModuleRef M, const char *Asm, size_t Len);

/**
 * Append inline assembly to a module.
 *
 * @see Module::appendModuleInlineAsm()
 */
void LLVMAppendModuleInlineAsm(LLVMModuleRef M, const char *Asm, size_t Len);

/**
 * Create the specified uniqued inline asm string.
 *
 * @see InlineAsm::get()
 */
LLVMValueRef LLVMGetInlineAsm(LLVMTypeRef Ty, const char *AsmString,
                              size_t AsmStringSize, const char *Constraints,
                              size_t ConstraintsSize, LLVMBool HasSideEffects,
                              LLVMBool IsAlignStack,
                              LLVMInlineAsmDialect Dialect, LLVMBool CanThrow);

/**
 * Get the template string used for an inline assembly snippet
 *
 */
const char *LLVMGetInlineAsmAsmString(LLVMValueRef InlineAsmVal, size_t *Len);

/**
 * Get the raw constraint string for an inline assembly snippet
 *
 */
const char *LLVMGetInlineAsmConstraintString(LLVMValueRef InlineAsmVal,
                                             size_t *Len);

/**
 * Get the dialect used by the inline asm snippet
 *
 */
LLVMInlineAsmDialect LLVMGetInlineAsmDialect(LLVMValueRef InlineAsmVal);

/**
 * Get the function type of the inline assembly snippet. The same type that
 * was passed into LLVMGetInlineAsm originally
 *
 * @see LLVMGetInlineAsm
 *
 */
LLVMTypeRef LLVMGetInlineAsmFunctionType(LLVMValueRef InlineAsmVal);

/**
 * Get if the inline asm snippet has side effects
 *
 */
LLVMBool LLVMGetInlineAsmHasSideEffects(LLVMValueRef InlineAsmVal);

/**
 * Get if the inline asm snippet needs an aligned stack
 *
 */
LLVMBool LLVMGetInlineAsmNeedsAlignedStack(LLVMValueRef InlineAsmVal);

/**
 * Get if the inline asm snippet may unwind the stack
 *
 */
LLVMBool LLVMGetInlineAsmCanUnwind(LLVMValueRef InlineAsmVal);

/**
 * Obtain the context to which this module is associated.
 *
 * @see Module::getContext()
 */
LLVMContextRef LLVMGetModuleContext(LLVMModuleRef M);

/** Deprecated: Use LLVMGetTypeByName2 instead. */
LLVMTypeRef LLVMGetTypeByName(LLVMModuleRef M, const char *Name);

/**
 * Obtain an iterator to the first NamedMDNode in a Module.
 *
 * @see llvm::Module::named_metadata_begin()
 */
LLVMNamedMDNodeRef LLVMGetFirstNamedMetadata(LLVMModuleRef M);

/**
 * Obtain an iterator to the last NamedMDNode in a Module.
 *
 * @see llvm::Module::named_metadata_end()
 */
LLVMNamedMDNodeRef LLVMGetLastNamedMetadata(LLVMModuleRef M);

/**
 * Advance a NamedMDNode iterator to the next NamedMDNode.
 *
 * Returns NULL if the iterator was already at the end and there are no more
 * named metadata nodes.
 */
LLVMNamedMDNodeRef LLVMGetNextNamedMetadata(LLVMNamedMDNodeRef NamedMDNode);

/**
 * Decrement a NamedMDNode iterator to the previous NamedMDNode.
 *
 * Returns NULL if the iterator was already at the beginning and there are
 * no previous named metadata nodes.
 */
LLVMNamedMDNodeRef LLVMGetPreviousNamedMetadata(LLVMNamedMDNodeRef NamedMDNode);

/**
 * Retrieve a NamedMDNode with the given name, returning NULL if no such
 * node exists.
 *
 * @see llvm::Module::getNamedMetadata()
 */
LLVMNamedMDNodeRef LLVMGetNamedMetadata(LLVMModuleRef M,
                                        const char *Name, size_t NameLen);

/**
 * Retrieve a NamedMDNode with the given name, creating a new node if no such
 * node exists.
 *
 * @see llvm::Module::getOrInsertNamedMetadata()
 */
LLVMNamedMDNodeRef LLVMGetOrInsertNamedMetadata(LLVMModuleRef M,
                                                const char *Name,
                                                size_t NameLen);

/**
 * Retrieve the name of a NamedMDNode.
 *
 * @see llvm::NamedMDNode::getName()
 */
const char *LLVMGetNamedMetadataName(LLVMNamedMDNodeRef NamedMD,
                                     size_t *NameLen);

/**
 * Obtain the number of operands for named metadata in a module.
 *
 * @see llvm::Module::getNamedMetadata()
 */
unsigned LLVMGetNamedMetadataNumOperands(LLVMModuleRef M, const char *Name);

/**
 * Obtain the named metadata operands for a module.
 *
 * The passed LLVMValueRef pointer should refer to an array of
 * LLVMValueRef at least LLVMGetNamedMetadataNumOperands long. This
 * array will be populated with the LLVMValueRef instances. Each
 * instance corresponds to a llvm::MDNode.
 *
 * @see llvm::Module::getNamedMetadata()
 * @see llvm::MDNode::getOperand()
 */
void LLVMGetNamedMetadataOperands(LLVMModuleRef M, const char *Name,
                                  LLVMValueRef *Dest);

/**
 * Add an operand to named metadata.
 *
 * @see llvm::Module::getNamedMetadata()
 * @see llvm::MDNode::addOperand()
 */
void LLVMAddNamedMetadataOperand(LLVMModuleRef M, const char *Name,
                                 LLVMValueRef Val);

/**
 * Return the directory of the debug location for this value, which must be
 * an llvm::Instruction, llvm::GlobalVariable, or llvm::Function.
 *
 * @see llvm::Instruction::getDebugLoc()
 * @see llvm::GlobalVariable::getDebugInfo()
 * @see llvm::Function::getSubprogram()
 */
const char *LLVMGetDebugLocDirectory(LLVMValueRef Val, unsigned *Length);

/**
 * Return the filename of the debug location for this value, which must be
 * an llvm::Instruction, llvm::GlobalVariable, or llvm::Function.
 *
 * @see llvm::Instruction::getDebugLoc()
 * @see llvm::GlobalVariable::getDebugInfo()
 * @see llvm::Function::getSubprogram()
 */
const char *LLVMGetDebugLocFilename(LLVMValueRef Val, unsigned *Length);

/**
 * Return the line number of the debug location for this value, which must be
 * an llvm::Instruction, llvm::GlobalVariable, or llvm::Function.
 *
 * @see llvm::Instruction::getDebugLoc()
 * @see llvm::GlobalVariable::getDebugInfo()
 * @see llvm::Function::getSubprogram()
 */
unsigned LLVMGetDebugLocLine(LLVMValueRef Val);

/**
 * Return the column number of the debug location for this value, which must be
 * an llvm::Instruction.
 *
 * @see llvm::Instruction::getDebugLoc()
 */
unsigned LLVMGetDebugLocColumn(LLVMValueRef Val);

/**
 * Add a function to a module under a specified name.
 *
 * @see llvm::Function::Create()
 */
LLVMValueRef LLVMAddFunction(LLVMModuleRef M, const char *Name,
                             LLVMTypeRef FunctionTy);

/**
 * Obtain a Function value from a Module by its name.
 *
 * The returned value corresponds to a llvm::Function value.
 *
 * @see llvm::Module::getFunction()
 */
LLVMValueRef LLVMGetNamedFunction(LLVMModuleRef M, const char *Name);

/**
 * Obtain an iterator to the first Function in a Module.
 *
 * @see llvm::Module::begin()
 */
LLVMValueRef LLVMGetFirstFunction(LLVMModuleRef M);

/**
 * Obtain an iterator to the last Function in a Module.
 *
 * @see llvm::Module::end()
 */
LLVMValueRef LLVMGetLastFunction(LLVMModuleRef M);

/**
 * Advance a Function iterator to the next Function.
 *
 * Returns NULL if the iterator was already at the end and there are no more
 * functions.
 */
LLVMValueRef LLVMGetNextFunction(LLVMValueRef Fn);

/**
 * Decrement a Function iterator to the previous Function.
 *
 * Returns NULL if the iterator was already at the beginning and there are
 * no previous functions.
 */
LLVMValueRef LLVMGetPreviousFunction(LLVMValueRef Fn);

/** Deprecated: Use LLVMSetModuleInlineAsm2 instead. */
void LLVMSetModuleInlineAsm(LLVMModuleRef M, const char *Asm);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreType Types
 *
 * Types represent the type of a value.
 *
 * Types are associated with a context instance. The context internally
 * deduplicates types so there is only 1 instance of a specific type
 * alive at a time. In other words, a unique type is shared among all
 * consumers within a context.
 *
 * A Type in the C API corresponds to llvm::Type.
 *
 * Types have the following hierarchy:
 *
 *   types:
 *     integer type
 *     real type
 *     function type
 *     sequence types:
 *       array type
 *       pointer type
 *       vector type
 *     void type
 *     label type
 *     opaque type
 *
 * @{
 */

/**
 * Obtain the enumerated type of a Type instance.
 *
 * @see llvm::Type:getTypeID()
 */
LLVMTypeKind LLVMGetTypeKind(LLVMTypeRef Ty);

/**
 * Whether the type has a known size.
 *
 * Things that don't have a size are abstract types, labels, and void.a
 *
 * @see llvm::Type::isSized()
 */
LLVMBool LLVMTypeIsSized(LLVMTypeRef Ty);

/**
 * Obtain the context to which this type instance is associated.
 *
 * @see llvm::Type::getContext()
 */
LLVMContextRef LLVMGetTypeContext(LLVMTypeRef Ty);

/**
 * Dump a representation of a type to stderr.
 *
 * @see llvm::Type::dump()
 */
void LLVMDumpType(LLVMTypeRef Val);

/**
 * Return a string representation of the type. Use
 * LLVMDisposeMessage to free the string.
 *
 * @see llvm::Type::print()
 */
char *LLVMPrintTypeToString(LLVMTypeRef Val);

/**
 * @defgroup LLVMCCoreTypeInt Integer Types
 *
 * Functions in this section operate on integer types.
 *
 * @{
 */

/**
 * Obtain an integer type from a context with specified bit width.
 */
LLVMTypeRef LLVMInt1TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt8TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt16TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt32TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt64TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMInt128TypeInContext(LLVMContextRef C);
LLVMTypeRef LLVMIntTypeInContext(LLVMContextRef C, unsigned NumBits);

/**
 * Obtain an integer type from the global context with a specified bit
 * width.
 */
LLVMTypeRef LLVMInt1Type(void);
LLVMTypeRef LLVMInt8Type(void);
LLVMTypeRef LLVMInt16Type(void);
LLVMTypeRef LLVMInt32Type(void);
LLVMTypeRef LLVMInt64Type(void);
LLVMTypeRef LLVMInt128Type(void);
LLVMTypeRef LLVMIntType(unsigned NumBits);
unsigned LLVMGetIntTypeWidth(LLVMTypeRef IntegerTy);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreTypeFloat Floating Point Types
 *
 * @{
 */

/**
 * Obtain a 16-bit floating point type from a context.
 */
LLVMTypeRef LLVMHalfTypeInContext(LLVMContextRef C);

/**
 * Obtain a 16-bit brain floating point type from a context.
 */
LLVMTypeRef LLVMBFloatTypeInContext(LLVMContextRef C);

/**
 * Obtain a 32-bit floating point type from a context.
 */
LLVMTypeRef LLVMFloatTypeInContext(LLVMContextRef C);

/**
 * Obtain a 64-bit floating point type from a context.
 */
LLVMTypeRef LLVMDoubleTypeInContext(LLVMContextRef C);

/**
 * Obtain a 80-bit floating point type (X87) from a context.
 */
LLVMTypeRef LLVMX86FP80TypeInContext(LLVMContextRef C);

/**
 * Obtain a 128-bit floating point type (112-bit mantissa) from a
 * context.
 */
LLVMTypeRef LLVMFP128TypeInContext(LLVMContextRef C);

/**
 * Obtain a 128-bit floating point type (two 64-bits) from a context.
 */
LLVMTypeRef LLVMPPCFP128TypeInContext(LLVMContextRef C);

/**
 * Obtain a floating point type from the global context.
 *
 * These map to the functions in this group of the same name.
 */
LLVMTypeRef LLVMHalfType(void);
LLVMTypeRef LLVMBFloatType(void);
LLVMTypeRef LLVMFloatType(void);
LLVMTypeRef LLVMDoubleType(void);
LLVMTypeRef LLVMX86FP80Type(void);
LLVMTypeRef LLVMFP128Type(void);
LLVMTypeRef LLVMPPCFP128Type(void);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreTypeFunction Function Types
 *
 * @{
 */

/**
 * Obtain a function type consisting of a specified signature.
 *
 * The function is defined as a tuple of a return Type, a list of
 * parameter types, and whether the function is variadic.
 */
LLVMTypeRef LLVMFunctionType(LLVMTypeRef ReturnType,
                             LLVMTypeRef *ParamTypes, unsigned ParamCount,
                             LLVMBool IsVarArg);

/**
 * Returns whether a function type is variadic.
 */
LLVMBool LLVMIsFunctionVarArg(LLVMTypeRef FunctionTy);

/**
 * Obtain the Type this function Type returns.
 */
LLVMTypeRef LLVMGetReturnType(LLVMTypeRef FunctionTy);

/**
 * Obtain the number of parameters this function accepts.
 */
unsigned LLVMCountParamTypes(LLVMTypeRef FunctionTy);

/**
 * Obtain the types of a function's parameters.
 *
 * The Dest parameter should point to a pre-allocated array of
 * LLVMTypeRef at least LLVMCountParamTypes() large. On return, the
 * first LLVMCountParamTypes() entries in the array will be populated
 * with LLVMTypeRef instances.
 *
 * @param FunctionTy The function type to operate on.
 * @param Dest Memory address of an array to be filled with result.
 */
void LLVMGetParamTypes(LLVMTypeRef FunctionTy, LLVMTypeRef *Dest);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreTypeStruct Structure Types
 *
 * These functions relate to LLVMTypeRef instances.
 *
 * @see llvm::StructType
 *
 * @{
 */

/**
 * Create a new structure type in a context.
 *
 * A structure is specified by a list of inner elements/types and
 * whether these can be packed together.
 *
 * @see llvm::StructType::create()
 */
LLVMTypeRef LLVMStructTypeInContext(LLVMContextRef C, LLVMTypeRef *ElementTypes,
                                    unsigned ElementCount, LLVMBool Packed);

/**
 * Create a new structure type in the global context.
 *
 * @see llvm::StructType::create()
 */
LLVMTypeRef LLVMStructType(LLVMTypeRef *ElementTypes, unsigned ElementCount,
                           LLVMBool Packed);

/**
 * Create an empty structure in a context having a specified name.
 *
 * @see llvm::StructType::create()
 */
LLVMTypeRef LLVMStructCreateNamed(LLVMContextRef C, const char *Name);

/**
 * Obtain the name of a structure.
 *
 * @see llvm::StructType::getName()
 */
const char *LLVMGetStructName(LLVMTypeRef Ty);

/**
 * Set the contents of a structure type.
 *
 * @see llvm::StructType::setBody()
 */
void LLVMStructSetBody(LLVMTypeRef StructTy, LLVMTypeRef *ElementTypes,
                       unsigned ElementCount, LLVMBool Packed);

/**
 * Get the number of elements defined inside the structure.
 *
 * @see llvm::StructType::getNumElements()
 */
unsigned LLVMCountStructElementTypes(LLVMTypeRef StructTy);

/**
 * Get the elements within a structure.
 *
 * The function is passed the address of a pre-allocated array of
 * LLVMTypeRef at least LLVMCountStructElementTypes() long. After
 * invocation, this array will be populated with the structure's
 * elements. The objects in the destination array will have a lifetime
 * of the structure type itself, which is the lifetime of the context it
 * is contained in.
 */
void LLVMGetStructElementTypes(LLVMTypeRef StructTy, LLVMTypeRef *Dest);

/**
 * Get the type of the element at a given index in the structure.
 *
 * @see llvm::StructType::getTypeAtIndex()
 */
LLVMTypeRef LLVMStructGetTypeAtIndex(LLVMTypeRef StructTy, unsigned i);

/**
 * Determine whether a structure is packed.
 *
 * @see llvm::StructType::isPacked()
 */
LLVMBool LLVMIsPackedStruct(LLVMTypeRef StructTy);

/**
 * Determine whether a structure is opaque.
 *
 * @see llvm::StructType::isOpaque()
 */
LLVMBool LLVMIsOpaqueStruct(LLVMTypeRef StructTy);

/**
 * Determine whether a structure is literal.
 *
 * @see llvm::StructType::isLiteral()
 */
LLVMBool LLVMIsLiteralStruct(LLVMTypeRef StructTy);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreTypeSequential Sequential Types
 *
 * Sequential types represents "arrays" of types. This is a super class
 * for array, vector, and pointer types.
 *
 * @{
 */

/**
 * Obtain the element type of an array or vector type.
 *
 * @see llvm::SequentialType::getElementType()
 */
LLVMTypeRef LLVMGetElementType(LLVMTypeRef Ty);

/**
 * Returns type's subtypes
 *
 * @see llvm::Type::subtypes()
 */
void LLVMGetSubtypes(LLVMTypeRef Tp, LLVMTypeRef *Arr);

/**
 *  Return the number of types in the derived type.
 *
 * @see llvm::Type::getNumContainedTypes()
 */
unsigned LLVMGetNumContainedTypes(LLVMTypeRef Tp);

/**
 * Create a fixed size array type that refers to a specific type.
 *
 * The created type will exist in the context that its element type
 * exists in.
 *
 * @deprecated LLVMArrayType is deprecated in favor of the API accurate
 * LLVMArrayType2
 * @see llvm::ArrayType::get()
 */
LLVMTypeRef LLVMArrayType(LLVMTypeRef ElementType, unsigned ElementCount);

/**
 * Create a fixed size array type that refers to a specific type.
 *
 * The created type will exist in the context that its element type
 * exists in.
 *
 * @see llvm::ArrayType::get()
 */
LLVMTypeRef LLVMArrayType2(LLVMTypeRef ElementType, uint64_t ElementCount);

/**
 * Obtain the length of an array type.
 *
 * This only works on types that represent arrays.
 *
 * @deprecated LLVMGetArrayLength is deprecated in favor of the API accurate
 * LLVMGetArrayLength2
 * @see llvm::ArrayType::getNumElements()
 */
unsigned LLVMGetArrayLength(LLVMTypeRef ArrayTy);

/**
 * Obtain the length of an array type.
 *
 * This only works on types that represent arrays.
 *
 * @see llvm::ArrayType::getNumElements()
 */
uint64_t LLVMGetArrayLength2(LLVMTypeRef ArrayTy);

/**
 * Create a pointer type that points to a defined type.
 *
 * The created type will exist in the context that its pointee type
 * exists in.
 *
 * @see llvm::PointerType::get()
 */
LLVMTypeRef LLVMPointerType(LLVMTypeRef ElementType, unsigned AddressSpace);

/**
 * Determine whether a pointer is opaque.
 *
 * True if this is an instance of an opaque PointerType.
 *
 * @see llvm::Type::isOpaquePointerTy()
 */
LLVMBool LLVMPointerTypeIsOpaque(LLVMTypeRef Ty);

/**
 * Create an opaque pointer type in a context.
 *
 * @see llvm::PointerType::get()
 */
LLVMTypeRef LLVMPointerTypeInContext(LLVMContextRef C, unsigned AddressSpace);

/**
 * Obtain the address space of a pointer type.
 *
 * This only works on types that represent pointers.
 *
 * @see llvm::PointerType::getAddressSpace()
 */
unsigned LLVMGetPointerAddressSpace(LLVMTypeRef PointerTy);

/**
 * Create a vector type that contains a defined type and has a specific
 * number of elements.
 *
 * The created type will exist in the context thats its element type
 * exists in.
 *
 * @see llvm::VectorType::get()
 */
LLVMTypeRef LLVMVectorType(LLVMTypeRef ElementType, unsigned ElementCount);

/**
 * Create a vector type that contains a defined type and has a scalable
 * number of elements.
 *
 * The created type will exist in the context thats its element type
 * exists in.
 *
 * @see llvm::ScalableVectorType::get()
 */
LLVMTypeRef LLVMScalableVectorType(LLVMTypeRef ElementType,
                                   unsigned ElementCount);

/**
 * Obtain the (possibly scalable) number of elements in a vector type.
 *
 * This only works on types that represent vectors (fixed or scalable).
 *
 * @see llvm::VectorType::getNumElements()
 */
unsigned LLVMGetVectorSize(LLVMTypeRef VectorTy);

/**
 * Get the pointer value for the associated ConstantPtrAuth constant.
 *
 * @see llvm::ConstantPtrAuth::getPointer
 */
LLVMValueRef LLVMGetConstantPtrAuthPointer(LLVMValueRef PtrAuth);

/**
 * Get the key value for the associated ConstantPtrAuth constant.
 *
 * @see llvm::ConstantPtrAuth::getKey
 */
LLVMValueRef LLVMGetConstantPtrAuthKey(LLVMValueRef PtrAuth);

/**
 * Get the discriminator value for the associated ConstantPtrAuth constant.
 *
 * @see llvm::ConstantPtrAuth::getDiscriminator
 */
LLVMValueRef LLVMGetConstantPtrAuthDiscriminator(LLVMValueRef PtrAuth);

/**
 * Get the address discriminator value for the associated ConstantPtrAuth
 * constant.
 *
 * @see llvm::ConstantPtrAuth::getAddrDiscriminator
 */
LLVMValueRef LLVMGetConstantPtrAuthAddrDiscriminator(LLVMValueRef PtrAuth);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreTypeOther Other Types
 *
 * @{
 */

/**
 * Create a void type in a context.
 */
LLVMTypeRef LLVMVoidTypeInContext(LLVMContextRef C);

/**
 * Create a label type in a context.
 */
LLVMTypeRef LLVMLabelTypeInContext(LLVMContextRef C);

/**
 * Create a X86 MMX type in a context.
 */
LLVMTypeRef LLVMX86MMXTypeInContext(LLVMContextRef C);

/**
 * Create a X86 AMX type in a context.
 */
LLVMTypeRef LLVMX86AMXTypeInContext(LLVMContextRef C);

/**
 * Create a token type in a context.
 */
LLVMTypeRef LLVMTokenTypeInContext(LLVMContextRef C);

/**
 * Create a metadata type in a context.
 */
LLVMTypeRef LLVMMetadataTypeInContext(LLVMContextRef C);

/**
 * These are similar to the above functions except they operate on the
 * global context.
 */
LLVMTypeRef LLVMVoidType(void);
LLVMTypeRef LLVMLabelType(void);
LLVMTypeRef LLVMX86MMXType(void);
LLVMTypeRef LLVMX86AMXType(void);

/**
 * Create a target extension type in LLVM context.
 */
LLVMTypeRef LLVMTargetExtTypeInContext(LLVMContextRef C, const char *Name,
                                       LLVMTypeRef *TypeParams,
                                       unsigned TypeParamCount,
                                       unsigned *IntParams,
                                       unsigned IntParamCount);

/**
 * Obtain the name for this target extension type.
 *
 * @see llvm::TargetExtType::getName()
 */
const char *LLVMGetTargetExtTypeName(LLVMTypeRef TargetExtTy);

/**
 * Obtain the number of type parameters for this target extension type.
 *
 * @see llvm::TargetExtType::getNumTypeParameters()
 */
unsigned LLVMGetTargetExtTypeNumTypeParams(LLVMTypeRef TargetExtTy);

/**
 * Get the type parameter at the given index for the target extension type.
 *
 * @see llvm::TargetExtType::getTypeParameter()
 */
LLVMTypeRef LLVMGetTargetExtTypeTypeParam(LLVMTypeRef TargetExtTy,
                                          unsigned Idx);

/**
 * Obtain the number of int parameters for this target extension type.
 *
 * @see llvm::TargetExtType::getNumIntParameters()
 */
unsigned LLVMGetTargetExtTypeNumIntParams(LLVMTypeRef TargetExtTy);

/**
 * Get the int parameter at the given index for the target extension type.
 *
 * @see llvm::TargetExtType::getIntParameter()
 */
unsigned LLVMGetTargetExtTypeIntParam(LLVMTypeRef TargetExtTy, unsigned Idx);

/**
 * @}
 */

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValues Values
 *
 * The bulk of LLVM's object model consists of values, which comprise a very
 * rich type hierarchy.
 *
 * LLVMValueRef essentially represents llvm::Value. There is a rich
 * hierarchy of classes within this type. Depending on the instance
 * obtained, not all APIs are available.
 *
 * Callers can determine the type of an LLVMValueRef by calling the
 * LLVMIsA* family of functions (e.g. LLVMIsAArgument()). These
 * functions are defined by a macro, so it isn't obvious which are
 * available by looking at the Doxygen source code. Instead, look at the
 * source definition of LLVM_FOR_EACH_VALUE_SUBCLASS and note the list
 * of value names given. These value names also correspond to classes in
 * the llvm::Value hierarchy.
 *
 * @{
 */

// Currently, clang-format tries to format the LLVM_FOR_EACH_VALUE_SUBCLASS
// macro in a progressively-indented fashion, which is not desired
// clang-format off

#define LLVM_FOR_EACH_VALUE_SUBCLASS(macro) \
  macro(Argument)                           \
  macro(BasicBlock)                         \
  macro(InlineAsm)                          \
  macro(User)                               \
    macro(Constant)                         \
      macro(BlockAddress)                   \
      macro(ConstantAggregateZero)          \
      macro(ConstantArray)                  \
      macro(ConstantDataSequential)         \
        macro(ConstantDataArray)            \
        macro(ConstantDataVector)           \
      macro(ConstantExpr)                   \
      macro(ConstantFP)                     \
      macro(ConstantInt)                    \
      macro(ConstantPointerNull)            \
      macro(ConstantStruct)                 \
      macro(ConstantTokenNone)              \
      macro(ConstantVector)                 \
      macro(ConstantPtrAuth)                \
      macro(GlobalValue)                    \
        macro(GlobalAlias)                  \
        macro(GlobalObject)                 \
          macro(Function)                   \
          macro(GlobalVariable)             \
          macro(GlobalIFunc)                \
      macro(UndefValue)                     \
      macro(PoisonValue)                    \
    macro(Instruction)                      \
      macro(UnaryOperator)                  \
      macro(BinaryOperator)                 \
      macro(CallInst)                       \
        macro(IntrinsicInst)                \
          macro(DbgInfoIntrinsic)           \
            macro(DbgVariableIntrinsic)     \
              macro(DbgDeclareInst)         \
            macro(DbgLabelInst)             \
          macro(MemIntrinsic)               \
            macro(MemCpyInst)               \
            macro(MemMoveInst)              \
            macro(MemSetInst)               \
      macro(CmpInst)                        \
        macro(FCmpInst)                     \
        macro(ICmpInst)                     \
      macro(ExtractElementInst)             \
      macro(GetElementPtrInst)              \
      macro(InsertElementInst)              \
      macro(InsertValueInst)                \
      macro(LandingPadInst)                 \
      macro(PHINode)                        \
      macro(SelectInst)                     \
      macro(ShuffleVectorInst)              \
      macro(StoreInst)                      \
      macro(BranchInst)                     \
      macro(IndirectBrInst)                 \
      macro(InvokeInst)                     \
      macro(ReturnInst)                     \
      macro(SwitchInst)                     \
      macro(UnreachableInst)                \
      macro(ResumeInst)                     \
      macro(CleanupReturnInst)              \
      macro(CatchReturnInst)                \
      macro(CatchSwitchInst)                \
      macro(CallBrInst)                     \
      macro(FuncletPadInst)                 \
        macro(CatchPadInst)                 \
        macro(CleanupPadInst)               \
      macro(UnaryInstruction)               \
        macro(AllocaInst)                   \
        macro(CastInst)                     \
          macro(AddrSpaceCastInst)          \
          macro(BitCastInst)                \
          macro(FPExtInst)                  \
          macro(FPToSIInst)                 \
          macro(FPToUIInst)                 \
          macro(FPTruncInst)                \
          macro(IntToPtrInst)               \
          macro(PtrToIntInst)               \
          macro(SExtInst)                   \
          macro(SIToFPInst)                 \
          macro(TruncInst)                  \
          macro(UIToFPInst)                 \
          macro(ZExtInst)                   \
        macro(ExtractValueInst)             \
        macro(LoadInst)                     \
        macro(VAArgInst)                    \
        macro(FreezeInst)                   \
      macro(AtomicCmpXchgInst)              \
      macro(AtomicRMWInst)                  \
      macro(FenceInst)

// clang-format on

/**
 * @defgroup LLVMCCoreValueGeneral General APIs
 *
 * Functions in this section work on all LLVMValueRef instances,
 * regardless of their sub-type. They correspond to functions available
 * on llvm::Value.
 *
 * @{
 */

/**
 * Obtain the type of a value.
 *
 * @see llvm::Value::getType()
 */
LLVMTypeRef LLVMTypeOf(LLVMValueRef Val);

/**
 * Obtain the enumerated type of a Value instance.
 *
 * @see llvm::Value::getValueID()
 */
LLVMValueKind LLVMGetValueKind(LLVMValueRef Val);

/**
 * Obtain the string name of a value.
 *
 * @see llvm::Value::getName()
 */
const char *LLVMGetValueName2(LLVMValueRef Val, size_t *Length);

/**
 * Set the string name of a value.
 *
 * @see llvm::Value::setName()
 */
void LLVMSetValueName2(LLVMValueRef Val, const char *Name, size_t NameLen);

/**
 * Dump a representation of a value to stderr.
 *
 * @see llvm::Value::dump()
 */
void LLVMDumpValue(LLVMValueRef Val);

/**
 * Return a string representation of the value. Use
 * LLVMDisposeMessage to free the string.
 *
 * @see llvm::Value::print()
 */
char *LLVMPrintValueToString(LLVMValueRef Val);

/**
 * Return a string representation of the DbgRecord. Use
 * LLVMDisposeMessage to free the string.
 *
 * @see llvm::DbgRecord::print()
 */
char *LLVMPrintDbgRecordToString(LLVMDbgRecordRef Record);

/**
 * Replace all uses of a value with another one.
 *
 * @see llvm::Value::replaceAllUsesWith()
 */
void LLVMReplaceAllUsesWith(LLVMValueRef OldVal, LLVMValueRef NewVal);

/**
 * Determine whether the specified value instance is constant.
 */
LLVMBool LLVMIsConstant(LLVMValueRef Val);

/**
 * Determine whether a value instance is undefined.
 */
LLVMBool LLVMIsUndef(LLVMValueRef Val);

/**
 * Determine whether a value instance is poisonous.
 */
LLVMBool LLVMIsPoison(LLVMValueRef Val);

/**
 * Convert value instances between types.
 *
 * Internally, an LLVMValueRef is "pinned" to a specific type. This
 * series of functions allows you to cast an instance to a specific
 * type.
 *
 * If the cast is not valid for the specified type, NULL is returned.
 *
 * @see llvm::dyn_cast_or_null<>
 */
#define LLVM_DECLARE_VALUE_CAST(name) \
  LLVMValueRef LLVMIsA##name(LLVMValueRef Val);
LLVM_FOR_EACH_VALUE_SUBCLASS(LLVM_DECLARE_VALUE_CAST)

LLVMValueRef LLVMIsAMDNode(LLVMValueRef Val);
LLVMValueRef LLVMIsAValueAsMetadata(LLVMValueRef Val);
LLVMValueRef LLVMIsAMDString(LLVMValueRef Val);

/** Deprecated: Use LLVMGetValueName2 instead. */
const char *LLVMGetValueName(LLVMValueRef Val);
/** Deprecated: Use LLVMSetValueName2 instead. */
void LLVMSetValueName(LLVMValueRef Val, const char *Name);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueUses Usage
 *
 * This module defines functions that allow you to inspect the uses of a
 * LLVMValueRef.
 *
 * It is possible to obtain an LLVMUseRef for any LLVMValueRef instance.
 * Each LLVMUseRef (which corresponds to a llvm::Use instance) holds a
 * llvm::User and llvm::Value.
 *
 * @{
 */

/**
 * Obtain the first use of a value.
 *
 * Uses are obtained in an iterator fashion. First, call this function
 * to obtain a reference to the first use. Then, call LLVMGetNextUse()
 * on that instance and all subsequently obtained instances until
 * LLVMGetNextUse() returns NULL.
 *
 * @see llvm::Value::use_begin()
 */
LLVMUseRef LLVMGetFirstUse(LLVMValueRef Val);

/**
 * Obtain the next use of a value.
 *
 * This effectively advances the iterator. It returns NULL if you are on
 * the final use and no more are available.
 */
LLVMUseRef LLVMGetNextUse(LLVMUseRef U);

/**
 * Obtain the user value for a user.
 *
 * The returned value corresponds to a llvm::User type.
 *
 * @see llvm::Use::getUser()
 */
LLVMValueRef LLVMGetUser(LLVMUseRef U);

/**
 * Obtain the value this use corresponds to.
 *
 * @see llvm::Use::get().
 */
LLVMValueRef LLVMGetUsedValue(LLVMUseRef U);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueUser User value
 *
 * Function in this group pertain to LLVMValueRef instances that descent
 * from llvm::User. This includes constants, instructions, and
 * operators.
 *
 * @{
 */

/**
 * Obtain an operand at a specific index in a llvm::User value.
 *
 * @see llvm::User::getOperand()
 */
LLVMValueRef LLVMGetOperand(LLVMValueRef Val, unsigned Index);

/**
 * Obtain the use of an operand at a specific index in a llvm::User value.
 *
 * @see llvm::User::getOperandUse()
 */
LLVMUseRef LLVMGetOperandUse(LLVMValueRef Val, unsigned Index);

/**
 * Set an operand at a specific index in a llvm::User value.
 *
 * @see llvm::User::setOperand()
 */
void LLVMSetOperand(LLVMValueRef User, unsigned Index, LLVMValueRef Val);

/**
 * Obtain the number of operands in a llvm::User value.
 *
 * @see llvm::User::getNumOperands()
 */
int LLVMGetNumOperands(LLVMValueRef Val);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueConstant Constants
 *
 * This section contains APIs for interacting with LLVMValueRef that
 * correspond to llvm::Constant instances.
 *
 * These functions will work for any LLVMValueRef in the llvm::Constant
 * class hierarchy.
 *
 * @{
 */

/**
 * Obtain a constant value referring to the null instance of a type.
 *
 * @see llvm::Constant::getNullValue()
 */
LLVMValueRef LLVMConstNull(LLVMTypeRef Ty); /* all zeroes */

/**
 * Obtain a constant value referring to the instance of a type
 * consisting of all ones.
 *
 * This is only valid for integer types.
 *
 * @see llvm::Constant::getAllOnesValue()
 */
LLVMValueRef LLVMConstAllOnes(LLVMTypeRef Ty);

/**
 * Obtain a constant value referring to an undefined value of a type.
 *
 * @see llvm::UndefValue::get()
 */
LLVMValueRef LLVMGetUndef(LLVMTypeRef Ty);

/**
 * Obtain a constant value referring to a poison value of a type.
 *
 * @see llvm::PoisonValue::get()
 */
LLVMValueRef LLVMGetPoison(LLVMTypeRef Ty);

/**
 * Determine whether a value instance is null.
 *
 * @see llvm::Constant::isNullValue()
 */
LLVMBool LLVMIsNull(LLVMValueRef Val);

/**
 * Obtain a constant that is a constant pointer pointing to NULL for a
 * specified type.
 */
LLVMValueRef LLVMConstPointerNull(LLVMTypeRef Ty);

/**
 * @defgroup LLVMCCoreValueConstantScalar Scalar constants
 *
 * Functions in this group model LLVMValueRef instances that correspond
 * to constants referring to scalar types.
 *
 * For integer types, the LLVMTypeRef parameter should correspond to a
 * llvm::IntegerType instance and the returned LLVMValueRef will
 * correspond to a llvm::ConstantInt.
 *
 * For floating point types, the LLVMTypeRef returned corresponds to a
 * llvm::ConstantFP.
 *
 * @{
 */

/**
 * Obtain a constant value for an integer type.
 *
 * The returned value corresponds to a llvm::ConstantInt.
 *
 * @see llvm::ConstantInt::get()
 *
 * @param IntTy Integer type to obtain value of.
 * @param N The value the returned instance should refer to.
 * @param SignExtend Whether to sign extend the produced value.
 */
LLVMValueRef LLVMConstInt(LLVMTypeRef IntTy, unsigned long long N,
                          LLVMBool SignExtend);

/**
 * Obtain a constant value for an integer of arbitrary precision.
 *
 * @see llvm::ConstantInt::get()
 */
LLVMValueRef LLVMConstIntOfArbitraryPrecision(LLVMTypeRef IntTy,
                                              unsigned NumWords,
                                              const uint64_t Words[]);

/**
 * Obtain a constant value for an integer parsed from a string.
 *
 * A similar API, LLVMConstIntOfStringAndSize is also available. If the
 * string's length is available, it is preferred to call that function
 * instead.
 *
 * @see llvm::ConstantInt::get()
 */
LLVMValueRef LLVMConstIntOfString(LLVMTypeRef IntTy, const char *Text,
                                  uint8_t Radix);

/**
 * Obtain a constant value for an integer parsed from a string with
 * specified length.
 *
 * @see llvm::ConstantInt::get()
 */
LLVMValueRef LLVMConstIntOfStringAndSize(LLVMTypeRef IntTy, const char *Text,
                                         unsigned SLen, uint8_t Radix);

/**
 * Obtain a constant value referring to a double floating point value.
 */
LLVMValueRef LLVMConstReal(LLVMTypeRef RealTy, double N);

/**
 * Obtain a constant for a floating point value parsed from a string.
 *
 * A similar API, LLVMConstRealOfStringAndSize is also available. It
 * should be used if the input string's length is known.
 */
LLVMValueRef LLVMConstRealOfString(LLVMTypeRef RealTy, const char *Text);

/**
 * Obtain a constant for a floating point value parsed from a string.
 */
LLVMValueRef LLVMConstRealOfStringAndSize(LLVMTypeRef RealTy, const char *Text,
                                          unsigned SLen);

/**
 * Obtain the zero extended value for an integer constant value.
 *
 * @see llvm::ConstantInt::getZExtValue()
 */
unsigned long long LLVMConstIntGetZExtValue(LLVMValueRef ConstantVal);

/**
 * Obtain the sign extended value for an integer constant value.
 *
 * @see llvm::ConstantInt::getSExtValue()
 */
long long LLVMConstIntGetSExtValue(LLVMValueRef ConstantVal);

/**
 * Obtain the double value for an floating point constant value.
 * losesInfo indicates if some precision was lost in the conversion.
 *
 * @see llvm::ConstantFP::getDoubleValue
 */
double LLVMConstRealGetDouble(LLVMValueRef ConstantVal, LLVMBool *losesInfo);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueConstantComposite Composite Constants
 *
 * Functions in this group operate on composite constants.
 *
 * @{
 */

/**
 * Create a ConstantDataSequential and initialize it with a string.
 *
 * @deprecated LLVMConstStringInContext is deprecated in favor of the API
 * accurate LLVMConstStringInContext2
 * @see llvm::ConstantDataArray::getString()
 */
LLVMValueRef LLVMConstStringInContext(LLVMContextRef C, const char *Str,
                                      unsigned Length, LLVMBool DontNullTerminate);

/**
 * Create a ConstantDataSequential and initialize it with a string.
 *
 * @see llvm::ConstantDataArray::getString()
 */
LLVMValueRef LLVMConstStringInContext2(LLVMContextRef C, const char *Str,
                                       size_t Length,
                                       LLVMBool DontNullTerminate);

/**
 * Create a ConstantDataSequential with string content in the global context.
 *
 * This is the same as LLVMConstStringInContext except it operates on the
 * global context.
 *
 * @see LLVMConstStringInContext()
 * @see llvm::ConstantDataArray::getString()
 */
LLVMValueRef LLVMConstString(const char *Str, unsigned Length,
                             LLVMBool DontNullTerminate);

/**
 * Returns true if the specified constant is an array of i8.
 *
 * @see ConstantDataSequential::getAsString()
 */
LLVMBool LLVMIsConstantString(LLVMValueRef c);

/**
 * Get the given constant data sequential as a string.
 *
 * @see ConstantDataSequential::getAsString()
 */
const char *LLVMGetAsString(LLVMValueRef c, size_t *Length);

/**
 * Create an anonymous ConstantStruct with the specified values.
 *
 * @see llvm::ConstantStruct::getAnon()
 */
LLVMValueRef LLVMConstStructInContext(LLVMContextRef C,
                                      LLVMValueRef *ConstantVals,
                                      unsigned Count, LLVMBool Packed);

/**
 * Create a ConstantStruct in the global Context.
 *
 * This is the same as LLVMConstStructInContext except it operates on the
 * global Context.
 *
 * @see LLVMConstStructInContext()
 */
LLVMValueRef LLVMConstStruct(LLVMValueRef *ConstantVals, unsigned Count,
                             LLVMBool Packed);

/**
 * Create a ConstantArray from values.
 *
 * @deprecated LLVMConstArray is deprecated in favor of the API accurate
 * LLVMConstArray2
 * @see llvm::ConstantArray::get()
 */
LLVMValueRef LLVMConstArray(LLVMTypeRef ElementTy,
                            LLVMValueRef *ConstantVals, unsigned Length);

/**
 * Create a ConstantArray from values.
 *
 * @see llvm::ConstantArray::get()
 */
LLVMValueRef LLVMConstArray2(LLVMTypeRef ElementTy, LLVMValueRef *ConstantVals,
                             uint64_t Length);

/**
 * Create a non-anonymous ConstantStruct from values.
 *
 * @see llvm::ConstantStruct::get()
 */
LLVMValueRef LLVMConstNamedStruct(LLVMTypeRef StructTy,
                                  LLVMValueRef *ConstantVals,
                                  unsigned Count);

/**
 * Get element of a constant aggregate (struct, array or vector) at the
 * specified index. Returns null if the index is out of range, or it's not
 * possible to determine the element (e.g., because the constant is a
 * constant expression.)
 *
 * @see llvm::Constant::getAggregateElement()
 */
LLVMValueRef LLVMGetAggregateElement(LLVMValueRef C, unsigned Idx);

/**
 * Get an element at specified index as a constant.
 *
 * @see ConstantDataSequential::getElementAsConstant()
 */
LLVM_ATTRIBUTE_C_DEPRECATED(
    LLVMValueRef LLVMGetElementAsConstant(LLVMValueRef C, unsigned idx),
    "Use LLVMGetAggregateElement instead");

/**
 * Create a ConstantVector from values.
 *
 * @see llvm::ConstantVector::get()
 */
LLVMValueRef LLVMConstVector(LLVMValueRef *ScalarConstantVals, unsigned Size);

/**
 * Create a ConstantPtrAuth constant with the given values.
 *
 * @see llvm::ConstantPtrAuth::get()
 */
LLVMValueRef LLVMConstantPtrAuth(LLVMValueRef Ptr, LLVMValueRef Key,
                                 LLVMValueRef Disc, LLVMValueRef AddrDisc);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueConstantExpressions Constant Expressions
 *
 * Functions in this group correspond to APIs on llvm::ConstantExpr.
 *
 * @see llvm::ConstantExpr.
 *
 * @{
 */
LLVMOpcode LLVMGetConstOpcode(LLVMValueRef ConstantVal);
LLVMValueRef LLVMAlignOf(LLVMTypeRef Ty);
LLVMValueRef LLVMSizeOf(LLVMTypeRef Ty);
LLVMValueRef LLVMConstNeg(LLVMValueRef ConstantVal);
LLVMValueRef LLVMConstNSWNeg(LLVMValueRef ConstantVal);
LLVM_ATTRIBUTE_C_DEPRECATED(
    LLVMValueRef LLVMConstNUWNeg(LLVMValueRef ConstantVal),
    "Use LLVMConstNull instead.");
LLVMValueRef LLVMConstNot(LLVMValueRef ConstantVal);
LLVMValueRef LLVMConstAdd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstNSWAdd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstNUWAdd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstSub(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstNSWSub(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstNUWSub(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstMul(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstNSWMul(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstNUWMul(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstXor(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant);
LLVMValueRef LLVMConstGEP2(LLVMTypeRef Ty, LLVMValueRef ConstantVal,
                           LLVMValueRef *ConstantIndices, unsigned NumIndices);
LLVMValueRef LLVMConstInBoundsGEP2(LLVMTypeRef Ty, LLVMValueRef ConstantVal,
                                   LLVMValueRef *ConstantIndices,
                                   unsigned NumIndices);
/**
 * Creates a constant GetElementPtr expression. Similar to LLVMConstGEP2, but
 * allows specifying the no-wrap flags.
 *
 * @see llvm::ConstantExpr::getGetElementPtr()
 */
LLVMValueRef LLVMConstGEPWithNoWrapFlags(LLVMTypeRef Ty,
                                         LLVMValueRef ConstantVal,
                                         LLVMValueRef *ConstantIndices,
                                         unsigned NumIndices,
                                         LLVMGEPNoWrapFlags NoWrapFlags);
LLVMValueRef LLVMConstTrunc(LLVMValueRef ConstantVal, LLVMTypeRef ToType);
LLVMValueRef LLVMConstPtrToInt(LLVMValueRef ConstantVal, LLVMTypeRef ToType);
LLVMValueRef LLVMConstIntToPtr(LLVMValueRef ConstantVal, LLVMTypeRef ToType);
LLVMValueRef LLVMConstBitCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType);
LLVMValueRef LLVMConstAddrSpaceCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType);
LLVMValueRef LLVMConstTruncOrBitCast(LLVMValueRef ConstantVal,
                                     LLVMTypeRef ToType);
LLVMValueRef LLVMConstPointerCast(LLVMValueRef ConstantVal,
                                  LLVMTypeRef ToType);
LLVMValueRef LLVMConstExtractElement(LLVMValueRef VectorConstant,
                                     LLVMValueRef IndexConstant);
LLVMValueRef LLVMConstInsertElement(LLVMValueRef VectorConstant,
                                    LLVMValueRef ElementValueConstant,
                                    LLVMValueRef IndexConstant);
LLVMValueRef LLVMConstShuffleVector(LLVMValueRef VectorAConstant,
                                    LLVMValueRef VectorBConstant,
                                    LLVMValueRef MaskConstant);
LLVMValueRef LLVMBlockAddress(LLVMValueRef F, LLVMBasicBlockRef BB);

/**
 * Gets the function associated with a given BlockAddress constant value.
 */
LLVMValueRef LLVMGetBlockAddressFunction(LLVMValueRef BlockAddr);

/**
 * Gets the basic block associated with a given BlockAddress constant value.
 */
LLVMBasicBlockRef LLVMGetBlockAddressBasicBlock(LLVMValueRef BlockAddr);

/** Deprecated: Use LLVMGetInlineAsm instead. */
LLVMValueRef LLVMConstInlineAsm(LLVMTypeRef Ty,
                                const char *AsmString, const char *Constraints,
                                LLVMBool HasSideEffects, LLVMBool IsAlignStack);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueConstantGlobals Global Values
 *
 * This group contains functions that operate on global values. Functions in
 * this group relate to functions in the llvm::GlobalValue class tree.
 *
 * @see llvm::GlobalValue
 *
 * @{
 */

LLVMModuleRef LLVMGetGlobalParent(LLVMValueRef Global);
LLVMBool LLVMIsDeclaration(LLVMValueRef Global);
LLVMLinkage LLVMGetLinkage(LLVMValueRef Global);
void LLVMSetLinkage(LLVMValueRef Global, LLVMLinkage Linkage);
const char *LLVMGetSection(LLVMValueRef Global);
void LLVMSetSection(LLVMValueRef Global, const char *Section);
LLVMVisibility LLVMGetVisibility(LLVMValueRef Global);
void LLVMSetVisibility(LLVMValueRef Global, LLVMVisibility Viz);
LLVMDLLStorageClass LLVMGetDLLStorageClass(LLVMValueRef Global);
void LLVMSetDLLStorageClass(LLVMValueRef Global, LLVMDLLStorageClass Class);
LLVMUnnamedAddr LLVMGetUnnamedAddress(LLVMValueRef Global);
void LLVMSetUnnamedAddress(LLVMValueRef Global, LLVMUnnamedAddr UnnamedAddr);

/**
 * Returns the "value type" of a global value.  This differs from the formal
 * type of a global value which is always a pointer type.
 *
 * @see llvm::GlobalValue::getValueType()
 */
LLVMTypeRef LLVMGlobalGetValueType(LLVMValueRef Global);

/** Deprecated: Use LLVMGetUnnamedAddress instead. */
LLVMBool LLVMHasUnnamedAddr(LLVMValueRef Global);
/** Deprecated: Use LLVMSetUnnamedAddress instead. */
void LLVMSetUnnamedAddr(LLVMValueRef Global, LLVMBool HasUnnamedAddr);

/**
 * @defgroup LLVMCCoreValueWithAlignment Values with alignment
 *
 * Functions in this group only apply to values with alignment, i.e.
 * global variables, load and store instructions.
 */

/**
 * Obtain the preferred alignment of the value.
 * @see llvm::AllocaInst::getAlignment()
 * @see llvm::LoadInst::getAlignment()
 * @see llvm::StoreInst::getAlignment()
 * @see llvm::AtomicRMWInst::setAlignment()
 * @see llvm::AtomicCmpXchgInst::setAlignment()
 * @see llvm::GlobalValue::getAlignment()
 */
unsigned LLVMGetAlignment(LLVMValueRef V);

/**
 * Set the preferred alignment of the value.
 * @see llvm::AllocaInst::setAlignment()
 * @see llvm::LoadInst::setAlignment()
 * @see llvm::StoreInst::setAlignment()
 * @see llvm::AtomicRMWInst::setAlignment()
 * @see llvm::AtomicCmpXchgInst::setAlignment()
 * @see llvm::GlobalValue::setAlignment()
 */
void LLVMSetAlignment(LLVMValueRef V, unsigned Bytes);

/**
 * Sets a metadata attachment, erasing the existing metadata attachment if
 * it already exists for the given kind.
 *
 * @see llvm::GlobalObject::setMetadata()
 */
void LLVMGlobalSetMetadata(LLVMValueRef Global, unsigned Kind,
                           LLVMMetadataRef MD);

/**
 * Erases a metadata attachment of the given kind if it exists.
 *
 * @see llvm::GlobalObject::eraseMetadata()
 */
void LLVMGlobalEraseMetadata(LLVMValueRef Global, unsigned Kind);

/**
 * Removes all metadata attachments from this value.
 *
 * @see llvm::GlobalObject::clearMetadata()
 */
void LLVMGlobalClearMetadata(LLVMValueRef Global);

/**
 * Retrieves an array of metadata entries representing the metadata attached to
 * this value. The caller is responsible for freeing this array by calling
 * \c LLVMDisposeValueMetadataEntries.
 *
 * @see llvm::GlobalObject::getAllMetadata()
 */
LLVMValueMetadataEntry *LLVMGlobalCopyAllMetadata(LLVMValueRef Value,
                                                  size_t *NumEntries);

/**
 * Destroys value metadata entries.
 */
void LLVMDisposeValueMetadataEntries(LLVMValueMetadataEntry *Entries);

/**
 * Returns the kind of a value metadata entry at a specific index.
 */
unsigned LLVMValueMetadataEntriesGetKind(LLVMValueMetadataEntry *Entries,
                                         unsigned Index);

/**
 * Returns the underlying metadata node of a value metadata entry at a
 * specific index.
 */
LLVMMetadataRef
LLVMValueMetadataEntriesGetMetadata(LLVMValueMetadataEntry *Entries,
                                    unsigned Index);

/**
 * @}
 */

/**
 * @defgroup LLVMCoreValueConstantGlobalVariable Global Variables
 *
 * This group contains functions that operate on global variable values.
 *
 * @see llvm::GlobalVariable
 *
 * @{
 */
LLVMValueRef LLVMAddGlobal(LLVMModuleRef M, LLVMTypeRef Ty, const char *Name);
LLVMValueRef LLVMAddGlobalInAddressSpace(LLVMModuleRef M, LLVMTypeRef Ty,
                                         const char *Name,
                                         unsigned AddressSpace);
LLVMValueRef LLVMGetNamedGlobal(LLVMModuleRef M, const char *Name);
LLVMValueRef LLVMGetFirstGlobal(LLVMModuleRef M);
LLVMValueRef LLVMGetLastGlobal(LLVMModuleRef M);
LLVMValueRef LLVMGetNextGlobal(LLVMValueRef GlobalVar);
LLVMValueRef LLVMGetPreviousGlobal(LLVMValueRef GlobalVar);
void LLVMDeleteGlobal(LLVMValueRef GlobalVar);
LLVMValueRef LLVMGetInitializer(LLVMValueRef GlobalVar);
void LLVMSetInitializer(LLVMValueRef GlobalVar, LLVMValueRef ConstantVal);
LLVMBool LLVMIsThreadLocal(LLVMValueRef GlobalVar);
void LLVMSetThreadLocal(LLVMValueRef GlobalVar, LLVMBool IsThreadLocal);
LLVMBool LLVMIsGlobalConstant(LLVMValueRef GlobalVar);
void LLVMSetGlobalConstant(LLVMValueRef GlobalVar, LLVMBool IsConstant);
LLVMThreadLocalMode LLVMGetThreadLocalMode(LLVMValueRef GlobalVar);
void LLVMSetThreadLocalMode(LLVMValueRef GlobalVar, LLVMThreadLocalMode Mode);
LLVMBool LLVMIsExternallyInitialized(LLVMValueRef GlobalVar);
void LLVMSetExternallyInitialized(LLVMValueRef GlobalVar, LLVMBool IsExtInit);

/**
 * @}
 */

/**
 * @defgroup LLVMCoreValueConstantGlobalAlias Global Aliases
 *
 * This group contains function that operate on global alias values.
 *
 * @see llvm::GlobalAlias
 *
 * @{
 */

/**
 * Add a GlobalAlias with the given value type, address space and aliasee.
 *
 * @see llvm::GlobalAlias::create()
 */
LLVMValueRef LLVMAddAlias2(LLVMModuleRef M, LLVMTypeRef ValueTy,
                           unsigned AddrSpace, LLVMValueRef Aliasee,
                           const char *Name);

/**
 * Obtain a GlobalAlias value from a Module by its name.
 *
 * The returned value corresponds to a llvm::GlobalAlias value.
 *
 * @see llvm::Module::getNamedAlias()
 */
LLVMValueRef LLVMGetNamedGlobalAlias(LLVMModuleRef M,
                                     const char *Name, size_t NameLen);

/**
 * Obtain an iterator to the first GlobalAlias in a Module.
 *
 * @see llvm::Module::alias_begin()
 */
LLVMValueRef LLVMGetFirstGlobalAlias(LLVMModuleRef M);

/**
 * Obtain an iterator to the last GlobalAlias in a Module.
 *
 * @see llvm::Module::alias_end()
 */
LLVMValueRef LLVMGetLastGlobalAlias(LLVMModuleRef M);

/**
 * Advance a GlobalAlias iterator to the next GlobalAlias.
 *
 * Returns NULL if the iterator was already at the end and there are no more
 * global aliases.
 */
LLVMValueRef LLVMGetNextGlobalAlias(LLVMValueRef GA);

/**
 * Decrement a GlobalAlias iterator to the previous GlobalAlias.
 *
 * Returns NULL if the iterator was already at the beginning and there are
 * no previous global aliases.
 */
LLVMValueRef LLVMGetPreviousGlobalAlias(LLVMValueRef GA);

/**
 * Retrieve the target value of an alias.
 */
LLVMValueRef LLVMAliasGetAliasee(LLVMValueRef Alias);

/**
 * Set the target value of an alias.
 */
void LLVMAliasSetAliasee(LLVMValueRef Alias, LLVMValueRef Aliasee);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueFunction Function values
 *
 * Functions in this group operate on LLVMValueRef instances that
 * correspond to llvm::Function instances.
 *
 * @see llvm::Function
 *
 * @{
 */

/**
 * Remove a function from its containing module and deletes it.
 *
 * @see llvm::Function::eraseFromParent()
 */
void LLVMDeleteFunction(LLVMValueRef Fn);

/**
 * Check whether the given function has a personality function.
 *
 * @see llvm::Function::hasPersonalityFn()
 */
LLVMBool LLVMHasPersonalityFn(LLVMValueRef Fn);

/**
 * Obtain the personality function attached to the function.
 *
 * @see llvm::Function::getPersonalityFn()
 */
LLVMValueRef LLVMGetPersonalityFn(LLVMValueRef Fn);

/**
 * Set the personality function attached to the function.
 *
 * @see llvm::Function::setPersonalityFn()
 */
void LLVMSetPersonalityFn(LLVMValueRef Fn, LLVMValueRef PersonalityFn);

/**
 * Obtain the intrinsic ID number which matches the given function name.
 *
 * @see llvm::Function::lookupIntrinsicID()
 */
unsigned LLVMLookupIntrinsicID(const char *Name, size_t NameLen);

/**
 * Obtain the ID number from a function instance.
 *
 * @see llvm::Function::getIntrinsicID()
 */
unsigned LLVMGetIntrinsicID(LLVMValueRef Fn);

/**
 * Create or insert the declaration of an intrinsic.  For overloaded intrinsics,
 * parameter types must be provided to uniquely identify an overload.
 *
 * @see llvm::Intrinsic::getDeclaration()
 */
LLVMValueRef LLVMGetIntrinsicDeclaration(LLVMModuleRef Mod,
                                         unsigned ID,
                                         LLVMTypeRef *ParamTypes,
                                         size_t ParamCount);

/**
 * Retrieves the type of an intrinsic.  For overloaded intrinsics, parameter
 * types must be provided to uniquely identify an overload.
 *
 * @see llvm::Intrinsic::getType()
 */
LLVMTypeRef LLVMIntrinsicGetType(LLVMContextRef Ctx, unsigned ID,
                                 LLVMTypeRef *ParamTypes, size_t ParamCount);

/**
 * Retrieves the name of an intrinsic.
 *
 * @see llvm::Intrinsic::getName()
 */
const char *LLVMIntrinsicGetName(unsigned ID, size_t *NameLength);

/** Deprecated: Use LLVMIntrinsicCopyOverloadedName2 instead. */
const char *LLVMIntrinsicCopyOverloadedName(unsigned ID,
                                            LLVMTypeRef *ParamTypes,
                                            size_t ParamCount,
                                            size_t *NameLength);

/**
 * Copies the name of an overloaded intrinsic identified by a given list of
 * parameter types.
 *
 * Unlike LLVMIntrinsicGetName, the caller is responsible for freeing the
 * returned string.
 *
 * This version also supports unnamed types.
 *
 * @see llvm::Intrinsic::getName()
 */
const char *LLVMIntrinsicCopyOverloadedName2(LLVMModuleRef Mod, unsigned ID,
                                             LLVMTypeRef *ParamTypes,
                                             size_t ParamCount,
                                             size_t *NameLength);

/**
 * Obtain if the intrinsic identified by the given ID is overloaded.
 *
 * @see llvm::Intrinsic::isOverloaded()
 */
LLVMBool LLVMIntrinsicIsOverloaded(unsigned ID);

/**
 * Obtain the calling function of a function.
 *
 * The returned value corresponds to the LLVMCallConv enumeration.
 *
 * @see llvm::Function::getCallingConv()
 */
unsigned LLVMGetFunctionCallConv(LLVMValueRef Fn);

/**
 * Set the calling convention of a function.
 *
 * @see llvm::Function::setCallingConv()
 *
 * @param Fn Function to operate on
 * @param CC LLVMCallConv to set calling convention to
 */
void LLVMSetFunctionCallConv(LLVMValueRef Fn, unsigned CC);

/**
 * Obtain the name of the garbage collector to use during code
 * generation.
 *
 * @see llvm::Function::getGC()
 */
const char *LLVMGetGC(LLVMValueRef Fn);

/**
 * Define the garbage collector to use during code generation.
 *
 * @see llvm::Function::setGC()
 */
void LLVMSetGC(LLVMValueRef Fn, const char *Name);

/**
 * Gets the prefix data associated with a function. Only valid on functions, and
 * only if LLVMHasPrefixData returns true.
 * See https://llvm.org/docs/LangRef.html#prefix-data
 */
LLVMValueRef LLVMGetPrefixData(LLVMValueRef Fn);

/**
 * Check if a given function has prefix data. Only valid on functions.
 * See https://llvm.org/docs/LangRef.html#prefix-data
 */
LLVMBool LLVMHasPrefixData(LLVMValueRef Fn);

/**
 * Sets the prefix data for the function. Only valid on functions.
 * See https://llvm.org/docs/LangRef.html#prefix-data
 */
void LLVMSetPrefixData(LLVMValueRef Fn, LLVMValueRef prefixData);

/**
 * Gets the prologue data associated with a function. Only valid on functions,
 * and only if LLVMHasPrologueData returns true.
 * See https://llvm.org/docs/LangRef.html#prologue-data
 */
LLVMValueRef LLVMGetPrologueData(LLVMValueRef Fn);

/**
 * Check if a given function has prologue data. Only valid on functions.
 * See https://llvm.org/docs/LangRef.html#prologue-data
 */
LLVMBool LLVMHasPrologueData(LLVMValueRef Fn);

/**
 * Sets the prologue data for the function. Only valid on functions.
 * See https://llvm.org/docs/LangRef.html#prologue-data
 */
void LLVMSetPrologueData(LLVMValueRef Fn, LLVMValueRef prologueData);

/**
 * Add an attribute to a function.
 *
 * @see llvm::Function::addAttribute()
 */
void LLVMAddAttributeAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx,
                             LLVMAttributeRef A);
unsigned LLVMGetAttributeCountAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx);
void LLVMGetAttributesAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx,
                              LLVMAttributeRef *Attrs);
LLVMAttributeRef LLVMGetEnumAttributeAtIndex(LLVMValueRef F,
                                             LLVMAttributeIndex Idx,
                                             unsigned KindID);
LLVMAttributeRef LLVMGetStringAttributeAtIndex(LLVMValueRef F,
                                               LLVMAttributeIndex Idx,
                                               const char *K, unsigned KLen);
void LLVMRemoveEnumAttributeAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx,
                                    unsigned KindID);
void LLVMRemoveStringAttributeAtIndex(LLVMValueRef F, LLVMAttributeIndex Idx,
                                      const char *K, unsigned KLen);

/**
 * Add a target-dependent attribute to a function
 * @see llvm::AttrBuilder::addAttribute()
 */
void LLVMAddTargetDependentFunctionAttr(LLVMValueRef Fn, const char *A,
                                        const char *V);

/**
 * @defgroup LLVMCCoreValueFunctionParameters Function Parameters
 *
 * Functions in this group relate to arguments/parameters on functions.
 *
 * Functions in this group expect LLVMValueRef instances that correspond
 * to llvm::Function instances.
 *
 * @{
 */

/**
 * Obtain the number of parameters in a function.
 *
 * @see llvm::Function::arg_size()
 */
unsigned LLVMCountParams(LLVMValueRef Fn);

/**
 * Obtain the parameters in a function.
 *
 * The takes a pointer to a pre-allocated array of LLVMValueRef that is
 * at least LLVMCountParams() long. This array will be filled with
 * LLVMValueRef instances which correspond to the parameters the
 * function receives. Each LLVMValueRef corresponds to a llvm::Argument
 * instance.
 *
 * @see llvm::Function::arg_begin()
 */
void LLVMGetParams(LLVMValueRef Fn, LLVMValueRef *Params);

/**
 * Obtain the parameter at the specified index.
 *
 * Parameters are indexed from 0.
 *
 * @see llvm::Function::arg_begin()
 */
LLVMValueRef LLVMGetParam(LLVMValueRef Fn, unsigned Index);

/**
 * Obtain the function to which this argument belongs.
 *
 * Unlike other functions in this group, this one takes an LLVMValueRef
 * that corresponds to a llvm::Attribute.
 *
 * The returned LLVMValueRef is the llvm::Function to which this
 * argument belongs.
 */
LLVMValueRef LLVMGetParamParent(LLVMValueRef Inst);

/**
 * Obtain the first parameter to a function.
 *
 * @see llvm::Function::arg_begin()
 */
LLVMValueRef LLVMGetFirstParam(LLVMValueRef Fn);

/**
 * Obtain the last parameter to a function.
 *
 * @see llvm::Function::arg_end()
 */
LLVMValueRef LLVMGetLastParam(LLVMValueRef Fn);

/**
 * Obtain the next parameter to a function.
 *
 * This takes an LLVMValueRef obtained from LLVMGetFirstParam() (which is
 * actually a wrapped iterator) and obtains the next parameter from the
 * underlying iterator.
 */
LLVMValueRef LLVMGetNextParam(LLVMValueRef Arg);

/**
 * Obtain the previous parameter to a function.
 *
 * This is the opposite of LLVMGetNextParam().
 */
LLVMValueRef LLVMGetPreviousParam(LLVMValueRef Arg);

/**
 * Set the alignment for a function parameter.
 *
 * @see llvm::Argument::addAttr()
 * @see llvm::AttrBuilder::addAlignmentAttr()
 */
void LLVMSetParamAlignment(LLVMValueRef Arg, unsigned Align);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueGlobalIFunc IFuncs
 *
 * Functions in this group relate to indirect functions.
 *
 * Functions in this group expect LLVMValueRef instances that correspond
 * to llvm::GlobalIFunc instances.
 *
 * @{
 */

/**
 * Add a global indirect function to a module under a specified name.
 *
 * @see llvm::GlobalIFunc::create()
 */
LLVMValueRef LLVMAddGlobalIFunc(LLVMModuleRef M,
                                const char *Name, size_t NameLen,
                                LLVMTypeRef Ty, unsigned AddrSpace,
                                LLVMValueRef Resolver);

/**
 * Obtain a GlobalIFunc value from a Module by its name.
 *
 * The returned value corresponds to a llvm::GlobalIFunc value.
 *
 * @see llvm::Module::getNamedIFunc()
 */
LLVMValueRef LLVMGetNamedGlobalIFunc(LLVMModuleRef M,
                                     const char *Name, size_t NameLen);

/**
 * Obtain an iterator to the first GlobalIFunc in a Module.
 *
 * @see llvm::Module::ifunc_begin()
 */
LLVMValueRef LLVMGetFirstGlobalIFunc(LLVMModuleRef M);

/**
 * Obtain an iterator to the last GlobalIFunc in a Module.
 *
 * @see llvm::Module::ifunc_end()
 */
LLVMValueRef LLVMGetLastGlobalIFunc(LLVMModuleRef M);

/**
 * Advance a GlobalIFunc iterator to the next GlobalIFunc.
 *
 * Returns NULL if the iterator was already at the end and there are no more
 * global aliases.
 */
LLVMValueRef LLVMGetNextGlobalIFunc(LLVMValueRef IFunc);

/**
 * Decrement a GlobalIFunc iterator to the previous GlobalIFunc.
 *
 * Returns NULL if the iterator was already at the beginning and there are
 * no previous global aliases.
 */
LLVMValueRef LLVMGetPreviousGlobalIFunc(LLVMValueRef IFunc);

/**
 * Retrieves the resolver function associated with this indirect function, or
 * NULL if it doesn't not exist.
 *
 * @see llvm::GlobalIFunc::getResolver()
 */
LLVMValueRef LLVMGetGlobalIFuncResolver(LLVMValueRef IFunc);

/**
 * Sets the resolver function associated with this indirect function.
 *
 * @see llvm::GlobalIFunc::setResolver()
 */
void LLVMSetGlobalIFuncResolver(LLVMValueRef IFunc, LLVMValueRef Resolver);

/**
 * Remove a global indirect function from its parent module and delete it.
 *
 * @see llvm::GlobalIFunc::eraseFromParent()
 */
void LLVMEraseGlobalIFunc(LLVMValueRef IFunc);

/**
 * Remove a global indirect function from its parent module.
 *
 * This unlinks the global indirect function from its containing module but
 * keeps it alive.
 *
 * @see llvm::GlobalIFunc::removeFromParent()
 */
void LLVMRemoveGlobalIFunc(LLVMValueRef IFunc);

/**
 * @}
 */

/**
 * @}
 */

/**
 * @}
 */

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueMetadata Metadata
 *
 * @{
 */

/**
 * Create an MDString value from a given string value.
 *
 * The MDString value does not take ownership of the given string, it remains
 * the responsibility of the caller to free it.
 *
 * @see llvm::MDString::get()
 */
LLVMMetadataRef LLVMMDStringInContext2(LLVMContextRef C, const char *Str,
                                       size_t SLen);

/**
 * Create an MDNode value with the given array of operands.
 *
 * @see llvm::MDNode::get()
 */
LLVMMetadataRef LLVMMDNodeInContext2(LLVMContextRef C, LLVMMetadataRef *MDs,
                                     size_t Count);

/**
 * Obtain a Metadata as a Value.
 */
LLVMValueRef LLVMMetadataAsValue(LLVMContextRef C, LLVMMetadataRef MD);

/**
 * Obtain a Value as a Metadata.
 */
LLVMMetadataRef LLVMValueAsMetadata(LLVMValueRef Val);

/**
 * Obtain the underlying string from a MDString value.
 *
 * @param V Instance to obtain string from.
 * @param Length Memory address which will hold length of returned string.
 * @return String data in MDString.
 */
const char *LLVMGetMDString(LLVMValueRef V, unsigned *Length);

/**
 * Obtain the number of operands from an MDNode value.
 *
 * @param V MDNode to get number of operands from.
 * @return Number of operands of the MDNode.
 */
unsigned LLVMGetMDNodeNumOperands(LLVMValueRef V);

/**
 * Obtain the given MDNode's operands.
 *
 * The passed LLVMValueRef pointer should point to enough memory to hold all of
 * the operands of the given MDNode (see LLVMGetMDNodeNumOperands) as
 * LLVMValueRefs. This memory will be populated with the LLVMValueRefs of the
 * MDNode's operands.
 *
 * @param V MDNode to get the operands from.
 * @param Dest Destination array for operands.
 */
void LLVMGetMDNodeOperands(LLVMValueRef V, LLVMValueRef *Dest);

/**
 * Replace an operand at a specific index in a llvm::MDNode value.
 *
 * @see llvm::MDNode::replaceOperandWith()
 */
void LLVMReplaceMDNodeOperandWith(LLVMValueRef V, unsigned Index,
                                  LLVMMetadataRef Replacement);

/** Deprecated: Use LLVMMDStringInContext2 instead. */
LLVMValueRef LLVMMDStringInContext(LLVMContextRef C, const char *Str,
                                   unsigned SLen);
/** Deprecated: Use LLVMMDStringInContext2 instead. */
LLVMValueRef LLVMMDString(const char *Str, unsigned SLen);
/** Deprecated: Use LLVMMDNodeInContext2 instead. */
LLVMValueRef LLVMMDNodeInContext(LLVMContextRef C, LLVMValueRef *Vals,
                                 unsigned Count);
/** Deprecated: Use LLVMMDNodeInContext2 instead. */
LLVMValueRef LLVMMDNode(LLVMValueRef *Vals, unsigned Count);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreOperandBundle Operand Bundles
 *
 * Functions in this group operate on LLVMOperandBundleRef instances that
 * correspond to llvm::OperandBundleDef instances.
 *
 * @see llvm::OperandBundleDef
 *
 * @{
 */

/**
 * Create a new operand bundle.
 *
 * Every invocation should be paired with LLVMDisposeOperandBundle() or memory
 * will be leaked.
 *
 * @param Tag Tag name of the operand bundle
 * @param TagLen Length of Tag
 * @param Args Memory address of an array of bundle operands
 * @param NumArgs Length of Args
 */
LLVMOperandBundleRef LLVMCreateOperandBundle(const char *Tag, size_t TagLen,
                                             LLVMValueRef *Args,
                                             unsigned NumArgs);

/**
 * Destroy an operand bundle.
 *
 * This must be called for every created operand bundle or memory will be
 * leaked.
 */
void LLVMDisposeOperandBundle(LLVMOperandBundleRef Bundle);

/**
 * Obtain the tag of an operand bundle as a string.
 *
 * @param Bundle Operand bundle to obtain tag of.
 * @param Len Out parameter which holds the length of the returned string.
 * @return The tag name of Bundle.
 * @see OperandBundleDef::getTag()
 */
const char *LLVMGetOperandBundleTag(LLVMOperandBundleRef Bundle, size_t *Len);

/**
 * Obtain the number of operands for an operand bundle.
 *
 * @param Bundle Operand bundle to obtain operand count of.
 * @return The number of operands.
 * @see OperandBundleDef::input_size()
 */
unsigned LLVMGetNumOperandBundleArgs(LLVMOperandBundleRef Bundle);

/**
 * Obtain the operand for an operand bundle at the given index.
 *
 * @param Bundle Operand bundle to obtain operand of.
 * @param Index An operand index, must be less than
 * LLVMGetNumOperandBundleArgs().
 * @return The operand.
 */
LLVMValueRef LLVMGetOperandBundleArgAtIndex(LLVMOperandBundleRef Bundle,
                                            unsigned Index);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueBasicBlock Basic Block
 *
 * A basic block represents a single entry single exit section of code.
 * Basic blocks contain a list of instructions which form the body of
 * the block.
 *
 * Basic blocks belong to functions. They have the type of label.
 *
 * Basic blocks are themselves values. However, the C API models them as
 * LLVMBasicBlockRef.
 *
 * @see llvm::BasicBlock
 *
 * @{
 */

/**
 * Convert a basic block instance to a value type.
 */
LLVMValueRef LLVMBasicBlockAsValue(LLVMBasicBlockRef BB);

/**
 * Determine whether an LLVMValueRef is itself a basic block.
 */
LLVMBool LLVMValueIsBasicBlock(LLVMValueRef Val);

/**
 * Convert an LLVMValueRef to an LLVMBasicBlockRef instance.
 */
LLVMBasicBlockRef LLVMValueAsBasicBlock(LLVMValueRef Val);

/**
 * Obtain the string name of a basic block.
 */
const char *LLVMGetBasicBlockName(LLVMBasicBlockRef BB);

/**
 * Obtain the function to which a basic block belongs.
 *
 * @see llvm::BasicBlock::getParent()
 */
LLVMValueRef LLVMGetBasicBlockParent(LLVMBasicBlockRef BB);

/**
 * Obtain the terminator instruction for a basic block.
 *
 * If the basic block does not have a terminator (it is not well-formed
 * if it doesn't), then NULL is returned.
 *
 * The returned LLVMValueRef corresponds to an llvm::Instruction.
 *
 * @see llvm::BasicBlock::getTerminator()
 */
LLVMValueRef LLVMGetBasicBlockTerminator(LLVMBasicBlockRef BB);

/**
 * Obtain the number of basic blocks in a function.
 *
 * @param Fn Function value to operate on.
 */
unsigned LLVMCountBasicBlocks(LLVMValueRef Fn);

/**
 * Obtain all of the basic blocks in a function.
 *
 * This operates on a function value. The BasicBlocks parameter is a
 * pointer to a pre-allocated array of LLVMBasicBlockRef of at least
 * LLVMCountBasicBlocks() in length. This array is populated with
 * LLVMBasicBlockRef instances.
 */
void LLVMGetBasicBlocks(LLVMValueRef Fn, LLVMBasicBlockRef *BasicBlocks);

/**
 * Obtain the first basic block in a function.
 *
 * The returned basic block can be used as an iterator. You will likely
 * eventually call into LLVMGetNextBasicBlock() with it.
 *
 * @see llvm::Function::begin()
 */
LLVMBasicBlockRef LLVMGetFirstBasicBlock(LLVMValueRef Fn);

/**
 * Obtain the last basic block in a function.
 *
 * @see llvm::Function::end()
 */
LLVMBasicBlockRef LLVMGetLastBasicBlock(LLVMValueRef Fn);

/**
 * Advance a basic block iterator.
 */
LLVMBasicBlockRef LLVMGetNextBasicBlock(LLVMBasicBlockRef BB);

/**
 * Go backwards in a basic block iterator.
 */
LLVMBasicBlockRef LLVMGetPreviousBasicBlock(LLVMBasicBlockRef BB);

/**
 * Obtain the basic block that corresponds to the entry point of a
 * function.
 *
 * @see llvm::Function::getEntryBlock()
 */
LLVMBasicBlockRef LLVMGetEntryBasicBlock(LLVMValueRef Fn);

/**
 * Insert the given basic block after the insertion point of the given builder.
 *
 * The insertion point must be valid.
 *
 * @see llvm::Function::BasicBlockListType::insertAfter()
 */
void LLVMInsertExistingBasicBlockAfterInsertBlock(LLVMBuilderRef Builder,
                                                  LLVMBasicBlockRef BB);

/**
 * Append the given basic block to the basic block list of the given function.
 *
 * @see llvm::Function::BasicBlockListType::push_back()
 */
void LLVMAppendExistingBasicBlock(LLVMValueRef Fn,
                                  LLVMBasicBlockRef BB);

/**
 * Create a new basic block without inserting it into a function.
 *
 * @see llvm::BasicBlock::Create()
 */
LLVMBasicBlockRef LLVMCreateBasicBlockInContext(LLVMContextRef C,
                                                const char *Name);

/**
 * Append a basic block to the end of a function.
 *
 * @see llvm::BasicBlock::Create()
 */
LLVMBasicBlockRef LLVMAppendBasicBlockInContext(LLVMContextRef C,
                                                LLVMValueRef Fn,
                                                const char *Name);

/**
 * Append a basic block to the end of a function using the global
 * context.
 *
 * @see llvm::BasicBlock::Create()
 */
LLVMBasicBlockRef LLVMAppendBasicBlock(LLVMValueRef Fn, const char *Name);

/**
 * Insert a basic block in a function before another basic block.
 *
 * The function to add to is determined by the function of the
 * passed basic block.
 *
 * @see llvm::BasicBlock::Create()
 */
LLVMBasicBlockRef LLVMInsertBasicBlockInContext(LLVMContextRef C,
                                                LLVMBasicBlockRef BB,
                                                const char *Name);

/**
 * Insert a basic block in a function using the global context.
 *
 * @see llvm::BasicBlock::Create()
 */
LLVMBasicBlockRef LLVMInsertBasicBlock(LLVMBasicBlockRef InsertBeforeBB,
                                       const char *Name);

/**
 * Remove a basic block from a function and delete it.
 *
 * This deletes the basic block from its containing function and deletes
 * the basic block itself.
 *
 * @see llvm::BasicBlock::eraseFromParent()
 */
void LLVMDeleteBasicBlock(LLVMBasicBlockRef BB);

/**
 * Remove a basic block from a function.
 *
 * This deletes the basic block from its containing function but keep
 * the basic block alive.
 *
 * @see llvm::BasicBlock::removeFromParent()
 */
void LLVMRemoveBasicBlockFromParent(LLVMBasicBlockRef BB);

/**
 * Move a basic block to before another one.
 *
 * @see llvm::BasicBlock::moveBefore()
 */
void LLVMMoveBasicBlockBefore(LLVMBasicBlockRef BB, LLVMBasicBlockRef MovePos);

/**
 * Move a basic block to after another one.
 *
 * @see llvm::BasicBlock::moveAfter()
 */
void LLVMMoveBasicBlockAfter(LLVMBasicBlockRef BB, LLVMBasicBlockRef MovePos);

/**
 * Obtain the first instruction in a basic block.
 *
 * The returned LLVMValueRef corresponds to a llvm::Instruction
 * instance.
 */
LLVMValueRef LLVMGetFirstInstruction(LLVMBasicBlockRef BB);

/**
 * Obtain the last instruction in a basic block.
 *
 * The returned LLVMValueRef corresponds to an LLVM:Instruction.
 */
LLVMValueRef LLVMGetLastInstruction(LLVMBasicBlockRef BB);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueInstruction Instructions
 *
 * Functions in this group relate to the inspection and manipulation of
 * individual instructions.
 *
 * In the C++ API, an instruction is modeled by llvm::Instruction. This
 * class has a large number of descendents. llvm::Instruction is a
 * llvm::Value and in the C API, instructions are modeled by
 * LLVMValueRef.
 *
 * This group also contains sub-groups which operate on specific
 * llvm::Instruction types, e.g. llvm::CallInst.
 *
 * @{
 */

/**
 * Determine whether an instruction has any metadata attached.
 */
int LLVMHasMetadata(LLVMValueRef Val);

/**
 * Return metadata associated with an instruction value.
 */
LLVMValueRef LLVMGetMetadata(LLVMValueRef Val, unsigned KindID);

/**
 * Set metadata associated with an instruction value.
 */
void LLVMSetMetadata(LLVMValueRef Val, unsigned KindID, LLVMValueRef Node);

/**
 * Returns the metadata associated with an instruction value, but filters out
 * all the debug locations.
 *
 * @see llvm::Instruction::getAllMetadataOtherThanDebugLoc()
 */
LLVMValueMetadataEntry *
LLVMInstructionGetAllMetadataOtherThanDebugLoc(LLVMValueRef Instr,
                                               size_t *NumEntries);

/**
 * Obtain the basic block to which an instruction belongs.
 *
 * @see llvm::Instruction::getParent()
 */
LLVMBasicBlockRef LLVMGetInstructionParent(LLVMValueRef Inst);

/**
 * Obtain the instruction that occurs after the one specified.
 *
 * The next instruction will be from the same basic block.
 *
 * If this is the last instruction in a basic block, NULL will be
 * returned.
 */
LLVMValueRef LLVMGetNextInstruction(LLVMValueRef Inst);

/**
 * Obtain the instruction that occurred before this one.
 *
 * If the instruction is the first instruction in a basic block, NULL
 * will be returned.
 */
LLVMValueRef LLVMGetPreviousInstruction(LLVMValueRef Inst);

/**
 * Remove an instruction.
 *
 * The instruction specified is removed from its containing building
 * block but is kept alive.
 *
 * @see llvm::Instruction::removeFromParent()
 */
void LLVMInstructionRemoveFromParent(LLVMValueRef Inst);

/**
 * Remove and delete an instruction.
 *
 * The instruction specified is removed from its containing building
 * block and then deleted.
 *
 * @see llvm::Instruction::eraseFromParent()
 */
void LLVMInstructionEraseFromParent(LLVMValueRef Inst);

/**
 * Delete an instruction.
 *
 * The instruction specified is deleted. It must have previously been
 * removed from its containing building block.
 *
 * @see llvm::Value::deleteValue()
 */
void LLVMDeleteInstruction(LLVMValueRef Inst);

/**
 * Obtain the code opcode for an individual instruction.
 *
 * @see llvm::Instruction::getOpCode()
 */
LLVMOpcode LLVMGetInstructionOpcode(LLVMValueRef Inst);

/**
 * Obtain the predicate of an instruction.
 *
 * This is only valid for instructions that correspond to llvm::ICmpInst.
 *
 * @see llvm::ICmpInst::getPredicate()
 */
LLVMIntPredicate LLVMGetICmpPredicate(LLVMValueRef Inst);

/**
 * Obtain the float predicate of an instruction.
 *
 * This is only valid for instructions that correspond to llvm::FCmpInst.
 *
 * @see llvm::FCmpInst::getPredicate()
 */
LLVMRealPredicate LLVMGetFCmpPredicate(LLVMValueRef Inst);

/**
 * Create a copy of 'this' instruction that is identical in all ways
 * except the following:
 *   * The instruction has no parent
 *   * The instruction has no name
 *
 * @see llvm::Instruction::clone()
 */
LLVMValueRef LLVMInstructionClone(LLVMValueRef Inst);

/**
 * Determine whether an instruction is a terminator. This routine is named to
 * be compatible with historical functions that did this by querying the
 * underlying C++ type.
 *
 * @see llvm::Instruction::isTerminator()
 */
LLVMValueRef LLVMIsATerminatorInst(LLVMValueRef Inst);

/**
 * @defgroup LLVMCCoreValueInstructionCall Call Sites and Invocations
 *
 * Functions in this group apply to instructions that refer to call
 * sites and invocations. These correspond to C++ types in the
 * llvm::CallInst class tree.
 *
 * @{
 */

/**
 * Obtain the argument count for a call instruction.
 *
 * This expects an LLVMValueRef that corresponds to a llvm::CallInst,
 * llvm::InvokeInst, or llvm:FuncletPadInst.
 *
 * @see llvm::CallInst::getNumArgOperands()
 * @see llvm::InvokeInst::getNumArgOperands()
 * @see llvm::FuncletPadInst::getNumArgOperands()
 */
unsigned LLVMGetNumArgOperands(LLVMValueRef Instr);

/**
 * Set the calling convention for a call instruction.
 *
 * This expects an LLVMValueRef that corresponds to a llvm::CallInst or
 * llvm::InvokeInst.
 *
 * @see llvm::CallInst::setCallingConv()
 * @see llvm::InvokeInst::setCallingConv()
 */
void LLVMSetInstructionCallConv(LLVMValueRef Instr, unsigned CC);

/**
 * Obtain the calling convention for a call instruction.
 *
 * This is the opposite of LLVMSetInstructionCallConv(). Reads its
 * usage.
 *
 * @see LLVMSetInstructionCallConv()
 */
unsigned LLVMGetInstructionCallConv(LLVMValueRef Instr);

void LLVMSetInstrParamAlignment(LLVMValueRef Instr, LLVMAttributeIndex Idx,
                                unsigned Align);

void LLVMAddCallSiteAttribute(LLVMValueRef C, LLVMAttributeIndex Idx,
                              LLVMAttributeRef A);
unsigned LLVMGetCallSiteAttributeCount(LLVMValueRef C, LLVMAttributeIndex Idx);
void LLVMGetCallSiteAttributes(LLVMValueRef C, LLVMAttributeIndex Idx,
                               LLVMAttributeRef *Attrs);
LLVMAttributeRef LLVMGetCallSiteEnumAttribute(LLVMValueRef C,
                                              LLVMAttributeIndex Idx,
                                              unsigned KindID);
LLVMAttributeRef LLVMGetCallSiteStringAttribute(LLVMValueRef C,
                                                LLVMAttributeIndex Idx,
                                                const char *K, unsigned KLen);
void LLVMRemoveCallSiteEnumAttribute(LLVMValueRef C, LLVMAttributeIndex Idx,
                                     unsigned KindID);
void LLVMRemoveCallSiteStringAttribute(LLVMValueRef C, LLVMAttributeIndex Idx,
                                       const char *K, unsigned KLen);

/**
 * Obtain the function type called by this instruction.
 *
 * @see llvm::CallBase::getFunctionType()
 */
LLVMTypeRef LLVMGetCalledFunctionType(LLVMValueRef C);

/**
 * Obtain the pointer to the function invoked by this instruction.
 *
 * This expects an LLVMValueRef that corresponds to a llvm::CallInst or
 * llvm::InvokeInst.
 *
 * @see llvm::CallInst::getCalledOperand()
 * @see llvm::InvokeInst::getCalledOperand()
 */
LLVMValueRef LLVMGetCalledValue(LLVMValueRef Instr);

/**
 * Obtain the number of operand bundles attached to this instruction.
 *
 * This only works on llvm::CallInst and llvm::InvokeInst instructions.
 *
 * @see llvm::CallBase::getNumOperandBundles()
 */
unsigned LLVMGetNumOperandBundles(LLVMValueRef C);

/**
 * Obtain the operand bundle attached to this instruction at the given index.
 * Use LLVMDisposeOperandBundle to free the operand bundle.
 *
 * This only works on llvm::CallInst and llvm::InvokeInst instructions.
 */
LLVMOperandBundleRef LLVMGetOperandBundleAtIndex(LLVMValueRef C,
                                                 unsigned Index);

/**
 * Obtain whether a call instruction is a tail call.
 *
 * This only works on llvm::CallInst instructions.
 *
 * @see llvm::CallInst::isTailCall()
 */
LLVMBool LLVMIsTailCall(LLVMValueRef CallInst);

/**
 * Set whether a call instruction is a tail call.
 *
 * This only works on llvm::CallInst instructions.
 *
 * @see llvm::CallInst::setTailCall()
 */
void LLVMSetTailCall(LLVMValueRef CallInst, LLVMBool IsTailCall);

/**
 * Obtain a tail call kind of the call instruction.
 *
 * @see llvm::CallInst::setTailCallKind()
 */
LLVMTailCallKind LLVMGetTailCallKind(LLVMValueRef CallInst);

/**
 * Set the call kind of the call instruction.
 *
 * @see llvm::CallInst::getTailCallKind()
 */
void LLVMSetTailCallKind(LLVMValueRef CallInst, LLVMTailCallKind kind);

/**
 * Return the normal destination basic block.
 *
 * This only works on llvm::InvokeInst instructions.
 *
 * @see llvm::InvokeInst::getNormalDest()
 */
LLVMBasicBlockRef LLVMGetNormalDest(LLVMValueRef InvokeInst);

/**
 * Return the unwind destination basic block.
 *
 * Works on llvm::InvokeInst, llvm::CleanupReturnInst, and
 * llvm::CatchSwitchInst instructions.
 *
 * @see llvm::InvokeInst::getUnwindDest()
 * @see llvm::CleanupReturnInst::getUnwindDest()
 * @see llvm::CatchSwitchInst::getUnwindDest()
 */
LLVMBasicBlockRef LLVMGetUnwindDest(LLVMValueRef InvokeInst);

/**
 * Set the normal destination basic block.
 *
 * This only works on llvm::InvokeInst instructions.
 *
 * @see llvm::InvokeInst::setNormalDest()
 */
void LLVMSetNormalDest(LLVMValueRef InvokeInst, LLVMBasicBlockRef B);

/**
 * Set the unwind destination basic block.
 *
 * Works on llvm::InvokeInst, llvm::CleanupReturnInst, and
 * llvm::CatchSwitchInst instructions.
 *
 * @see llvm::InvokeInst::setUnwindDest()
 * @see llvm::CleanupReturnInst::setUnwindDest()
 * @see llvm::CatchSwitchInst::setUnwindDest()
 */
void LLVMSetUnwindDest(LLVMValueRef InvokeInst, LLVMBasicBlockRef B);

/**
 * Get the default destination of a CallBr instruction.
 *
 * @see llvm::CallBrInst::getDefaultDest()
 */
LLVMBasicBlockRef LLVMGetCallBrDefaultDest(LLVMValueRef CallBr);

/**
 * Get the number of indirect destinations of a CallBr instruction.
 *
 * @see llvm::CallBrInst::getNumIndirectDests()

 */
unsigned LLVMGetCallBrNumIndirectDests(LLVMValueRef CallBr);

/**
 * Get the indirect destination of a CallBr instruction at the given index.
 *
 * @see llvm::CallBrInst::getIndirectDest()
 */
LLVMBasicBlockRef LLVMGetCallBrIndirectDest(LLVMValueRef CallBr, unsigned Idx);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueInstructionTerminator Terminators
 *
 * Functions in this group only apply to instructions for which
 * LLVMIsATerminatorInst returns true.
 *
 * @{
 */

/**
 * Return the number of successors that this terminator has.
 *
 * @see llvm::Instruction::getNumSuccessors
 */
unsigned LLVMGetNumSuccessors(LLVMValueRef Term);

/**
 * Return the specified successor.
 *
 * @see llvm::Instruction::getSuccessor
 */
LLVMBasicBlockRef LLVMGetSuccessor(LLVMValueRef Term, unsigned i);

/**
 * Update the specified successor to point at the provided block.
 *
 * @see llvm::Instruction::setSuccessor
 */
void LLVMSetSuccessor(LLVMValueRef Term, unsigned i, LLVMBasicBlockRef block);

/**
 * Return if a branch is conditional.
 *
 * This only works on llvm::BranchInst instructions.
 *
 * @see llvm::BranchInst::isConditional
 */
LLVMBool LLVMIsConditional(LLVMValueRef Branch);

/**
 * Return the condition of a branch instruction.
 *
 * This only works on llvm::BranchInst instructions.
 *
 * @see llvm::BranchInst::getCondition
 */
LLVMValueRef LLVMGetCondition(LLVMValueRef Branch);

/**
 * Set the condition of a branch instruction.
 *
 * This only works on llvm::BranchInst instructions.
 *
 * @see llvm::BranchInst::setCondition
 */
void LLVMSetCondition(LLVMValueRef Branch, LLVMValueRef Cond);

/**
 * Obtain the default destination basic block of a switch instruction.
 *
 * This only works on llvm::SwitchInst instructions.
 *
 * @see llvm::SwitchInst::getDefaultDest()
 */
LLVMBasicBlockRef LLVMGetSwitchDefaultDest(LLVMValueRef SwitchInstr);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueInstructionAlloca Allocas
 *
 * Functions in this group only apply to instructions that map to
 * llvm::AllocaInst instances.
 *
 * @{
 */

/**
 * Obtain the type that is being allocated by the alloca instruction.
 */
LLVMTypeRef LLVMGetAllocatedType(LLVMValueRef Alloca);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueInstructionGetElementPointer GEPs
 *
 * Functions in this group only apply to instructions that map to
 * llvm::GetElementPtrInst instances.
 *
 * @{
 */

/**
 * Check whether the given GEP operator is inbounds.
 */
LLVMBool LLVMIsInBounds(LLVMValueRef GEP);

/**
 * Set the given GEP instruction to be inbounds or not.
 */
void LLVMSetIsInBounds(LLVMValueRef GEP, LLVMBool InBounds);

/**
 * Get the source element type of the given GEP operator.
 */
LLVMTypeRef LLVMGetGEPSourceElementType(LLVMValueRef GEP);

/**
 * Get the no-wrap related flags for the given GEP instruction.
 *
 * @see llvm::GetElementPtrInst::getNoWrapFlags
 */
LLVMGEPNoWrapFlags LLVMGEPGetNoWrapFlags(LLVMValueRef GEP);

/**
 * Set the no-wrap related flags for the given GEP instruction.
 *
 * @see llvm::GetElementPtrInst::setNoWrapFlags
 */
void LLVMGEPSetNoWrapFlags(LLVMValueRef GEP, LLVMGEPNoWrapFlags NoWrapFlags);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueInstructionPHINode PHI Nodes
 *
 * Functions in this group only apply to instructions that map to
 * llvm::PHINode instances.
 *
 * @{
 */

/**
 * Add an incoming value to the end of a PHI list.
 */
void LLVMAddIncoming(LLVMValueRef PhiNode, LLVMValueRef *IncomingValues,
                     LLVMBasicBlockRef *IncomingBlocks, unsigned Count);

/**
 * Obtain the number of incoming basic blocks to a PHI node.
 */
unsigned LLVMCountIncoming(LLVMValueRef PhiNode);

/**
 * Obtain an incoming value to a PHI node as an LLVMValueRef.
 */
LLVMValueRef LLVMGetIncomingValue(LLVMValueRef PhiNode, unsigned Index);

/**
 * Obtain an incoming value to a PHI node as an LLVMBasicBlockRef.
 */
LLVMBasicBlockRef LLVMGetIncomingBlock(LLVMValueRef PhiNode, unsigned Index);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreValueInstructionExtractValue ExtractValue
 * @defgroup LLVMCCoreValueInstructionInsertValue InsertValue
 *
 * Functions in this group only apply to instructions that map to
 * llvm::ExtractValue and llvm::InsertValue instances.
 *
 * @{
 */

/**
 * Obtain the number of indices.
 * NB: This also works on GEP operators.
 */
unsigned LLVMGetNumIndices(LLVMValueRef Inst);

/**
 * Obtain the indices as an array.
 */
const unsigned *LLVMGetIndices(LLVMValueRef Inst);

/**
 * @}
 */

/**
 * @}
 */

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreInstructionBuilder Instruction Builders
 *
 * An instruction builder represents a point within a basic block and is
 * the exclusive means of building instructions using the C interface.
 *
 * @{
 */

LLVMBuilderRef LLVMCreateBuilderInContext(LLVMContextRef C);
LLVMBuilderRef LLVMCreateBuilder(void);
/**
 * Set the builder position before Instr but after any attached debug records,
 * or if Instr is null set the position to the end of Block.
 */
void LLVMPositionBuilder(LLVMBuilderRef Builder, LLVMBasicBlockRef Block,
                         LLVMValueRef Instr);
/**
 * Set the builder position before Instr and any attached debug records,
 * or if Instr is null set the position to the end of Block.
 */
void LLVMPositionBuilderBeforeDbgRecords(LLVMBuilderRef Builder,
                                         LLVMBasicBlockRef Block,
                                         LLVMValueRef Inst);
/**
 * Set the builder position before Instr but after any attached debug records.
 */
void LLVMPositionBuilderBefore(LLVMBuilderRef Builder, LLVMValueRef Instr);
/**
 * Set the builder position before Instr and any attached debug records.
 */
void LLVMPositionBuilderBeforeInstrAndDbgRecords(LLVMBuilderRef Builder,
                                                 LLVMValueRef Instr);
void LLVMPositionBuilderAtEnd(LLVMBuilderRef Builder, LLVMBasicBlockRef Block);
LLVMBasicBlockRef LLVMGetInsertBlock(LLVMBuilderRef Builder);
void LLVMClearInsertionPosition(LLVMBuilderRef Builder);
void LLVMInsertIntoBuilder(LLVMBuilderRef Builder, LLVMValueRef Instr);
void LLVMInsertIntoBuilderWithName(LLVMBuilderRef Builder, LLVMValueRef Instr,
                                   const char *Name);
void LLVMDisposeBuilder(LLVMBuilderRef Builder);

/* Metadata */

/**
 * Get location information used by debugging information.
 *
 * @see llvm::IRBuilder::getCurrentDebugLocation()
 */
LLVMMetadataRef LLVMGetCurrentDebugLocation2(LLVMBuilderRef Builder);

/**
 * Set location information used by debugging information.
 *
 * To clear the location metadata of the given instruction, pass NULL to \p Loc.
 *
 * @see llvm::IRBuilder::SetCurrentDebugLocation()
 */
void LLVMSetCurrentDebugLocation2(LLVMBuilderRef Builder, LLVMMetadataRef Loc);

/**
 * Attempts to set the debug location for the given instruction using the
 * current debug location for the given builder.  If the builder has no current
 * debug location, this function is a no-op.
 *
 * @deprecated LLVMSetInstDebugLocation is deprecated in favor of the more general
 *             LLVMAddMetadataToInst.
 *
 * @see llvm::IRBuilder::SetInstDebugLocation()
 */
void LLVMSetInstDebugLocation(LLVMBuilderRef Builder, LLVMValueRef Inst);

/**
 * Adds the metadata registered with the given builder to the given instruction.
 *
 * @see llvm::IRBuilder::AddMetadataToInst()
 */
void LLVMAddMetadataToInst(LLVMBuilderRef Builder, LLVMValueRef Inst);

/**
 * Get the dafult floating-point math metadata for a given builder.
 *
 * @see llvm::IRBuilder::getDefaultFPMathTag()
 */
LLVMMetadataRef LLVMBuilderGetDefaultFPMathTag(LLVMBuilderRef Builder);

/**
 * Set the default floating-point math metadata for the given builder.
 *
 * To clear the metadata, pass NULL to \p FPMathTag.
 *
 * @see llvm::IRBuilder::setDefaultFPMathTag()
 */
void LLVMBuilderSetDefaultFPMathTag(LLVMBuilderRef Builder,
                                    LLVMMetadataRef FPMathTag);

/**
 * Deprecated: Passing the NULL location will crash.
 * Use LLVMGetCurrentDebugLocation2 instead.
 */
void LLVMSetCurrentDebugLocation(LLVMBuilderRef Builder, LLVMValueRef L);
/**
 * Deprecated: Returning the NULL location will crash.
 * Use LLVMGetCurrentDebugLocation2 instead.
 */
LLVMValueRef LLVMGetCurrentDebugLocation(LLVMBuilderRef Builder);

/* Terminators */
LLVMValueRef LLVMBuildRetVoid(LLVMBuilderRef);
LLVMValueRef LLVMBuildRet(LLVMBuilderRef, LLVMValueRef V);
LLVMValueRef LLVMBuildAggregateRet(LLVMBuilderRef, LLVMValueRef *RetVals,
                                   unsigned N);
LLVMValueRef LLVMBuildBr(LLVMBuilderRef, LLVMBasicBlockRef Dest);
LLVMValueRef LLVMBuildCondBr(LLVMBuilderRef, LLVMValueRef If,
                             LLVMBasicBlockRef Then, LLVMBasicBlockRef Else);
LLVMValueRef LLVMBuildSwitch(LLVMBuilderRef, LLVMValueRef V,
                             LLVMBasicBlockRef Else, unsigned NumCases);
LLVMValueRef LLVMBuildIndirectBr(LLVMBuilderRef B, LLVMValueRef Addr,
                                 unsigned NumDests);
LLVMValueRef LLVMBuildCallBr(LLVMBuilderRef B, LLVMTypeRef Ty, LLVMValueRef Fn,
                             LLVMBasicBlockRef DefaultDest,
                             LLVMBasicBlockRef *IndirectDests,
                             unsigned NumIndirectDests, LLVMValueRef *Args,
                             unsigned NumArgs, LLVMOperandBundleRef *Bundles,
                             unsigned NumBundles, const char *Name);
LLVMValueRef LLVMBuildInvoke2(LLVMBuilderRef, LLVMTypeRef Ty, LLVMValueRef Fn,
                              LLVMValueRef *Args, unsigned NumArgs,
                              LLVMBasicBlockRef Then, LLVMBasicBlockRef Catch,
                              const char *Name);
LLVMValueRef LLVMBuildInvokeWithOperandBundles(
    LLVMBuilderRef, LLVMTypeRef Ty, LLVMValueRef Fn, LLVMValueRef *Args,
    unsigned NumArgs, LLVMBasicBlockRef Then, LLVMBasicBlockRef Catch,
    LLVMOperandBundleRef *Bundles, unsigned NumBundles, const char *Name);
LLVMValueRef LLVMBuildUnreachable(LLVMBuilderRef);

/* Exception Handling */
LLVMValueRef LLVMBuildResume(LLVMBuilderRef B, LLVMValueRef Exn);
LLVMValueRef LLVMBuildLandingPad(LLVMBuilderRef B, LLVMTypeRef Ty,
                                 LLVMValueRef PersFn, unsigned NumClauses,
                                 const char *Name);
LLVMValueRef LLVMBuildCleanupRet(LLVMBuilderRef B, LLVMValueRef CatchPad,
                                 LLVMBasicBlockRef BB);
LLVMValueRef LLVMBuildCatchRet(LLVMBuilderRef B, LLVMValueRef CatchPad,
                               LLVMBasicBlockRef BB);
LLVMValueRef LLVMBuildCatchPad(LLVMBuilderRef B, LLVMValueRef ParentPad,
                               LLVMValueRef *Args, unsigned NumArgs,
                               const char *Name);
LLVMValueRef LLVMBuildCleanupPad(LLVMBuilderRef B, LLVMValueRef ParentPad,
                                 LLVMValueRef *Args, unsigned NumArgs,
                                 const char *Name);
LLVMValueRef LLVMBuildCatchSwitch(LLVMBuilderRef B, LLVMValueRef ParentPad,
                                  LLVMBasicBlockRef UnwindBB,
                                  unsigned NumHandlers, const char *Name);

/* Add a case to the switch instruction */
void LLVMAddCase(LLVMValueRef Switch, LLVMValueRef OnVal,
                 LLVMBasicBlockRef Dest);

/* Add a destination to the indirectbr instruction */
void LLVMAddDestination(LLVMValueRef IndirectBr, LLVMBasicBlockRef Dest);

/* Get the number of clauses on the landingpad instruction */
unsigned LLVMGetNumClauses(LLVMValueRef LandingPad);

/* Get the value of the clause at index Idx on the landingpad instruction */
LLVMValueRef LLVMGetClause(LLVMValueRef LandingPad, unsigned Idx);

/* Add a catch or filter clause to the landingpad instruction */
void LLVMAddClause(LLVMValueRef LandingPad, LLVMValueRef ClauseVal);

/* Get the 'cleanup' flag in the landingpad instruction */
LLVMBool LLVMIsCleanup(LLVMValueRef LandingPad);

/* Set the 'cleanup' flag in the landingpad instruction */
void LLVMSetCleanup(LLVMValueRef LandingPad, LLVMBool Val);

/* Add a destination to the catchswitch instruction */
void LLVMAddHandler(LLVMValueRef CatchSwitch, LLVMBasicBlockRef Dest);

/* Get the number of handlers on the catchswitch instruction */
unsigned LLVMGetNumHandlers(LLVMValueRef CatchSwitch);

/**
 * Obtain the basic blocks acting as handlers for a catchswitch instruction.
 *
 * The Handlers parameter should point to a pre-allocated array of
 * LLVMBasicBlockRefs at least LLVMGetNumHandlers() large. On return, the
 * first LLVMGetNumHandlers() entries in the array will be populated
 * with LLVMBasicBlockRef instances.
 *
 * @param CatchSwitch The catchswitch instruction to operate on.
 * @param Handlers Memory address of an array to be filled with basic blocks.
 */
void LLVMGetHandlers(LLVMValueRef CatchSwitch, LLVMBasicBlockRef *Handlers);

/* Funclets */

/* Get the number of funcletpad arguments. */
LLVMValueRef LLVMGetArgOperand(LLVMValueRef Funclet, unsigned i);

/* Set a funcletpad argument at the given index. */
void LLVMSetArgOperand(LLVMValueRef Funclet, unsigned i, LLVMValueRef value);

/**
 * Get the parent catchswitch instruction of a catchpad instruction.
 *
 * This only works on llvm::CatchPadInst instructions.
 *
 * @see llvm::CatchPadInst::getCatchSwitch()
 */
LLVMValueRef LLVMGetParentCatchSwitch(LLVMValueRef CatchPad);

/**
 * Set the parent catchswitch instruction of a catchpad instruction.
 *
 * This only works on llvm::CatchPadInst instructions.
 *
 * @see llvm::CatchPadInst::setCatchSwitch()
 */
void LLVMSetParentCatchSwitch(LLVMValueRef CatchPad, LLVMValueRef CatchSwitch);

/* Arithmetic */
LLVMValueRef LLVMBuildAdd(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name);
LLVMValueRef LLVMBuildNSWAdd(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                             const char *Name);
LLVMValueRef LLVMBuildNUWAdd(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                             const char *Name);
LLVMValueRef LLVMBuildFAdd(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildSub(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name);
LLVMValueRef LLVMBuildNSWSub(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                             const char *Name);
LLVMValueRef LLVMBuildNUWSub(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                             const char *Name);
LLVMValueRef LLVMBuildFSub(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildMul(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name);
LLVMValueRef LLVMBuildNSWMul(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                             const char *Name);
LLVMValueRef LLVMBuildNUWMul(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                             const char *Name);
LLVMValueRef LLVMBuildFMul(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildUDiv(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildExactUDiv(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                                const char *Name);
LLVMValueRef LLVMBuildSDiv(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildExactSDiv(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                                const char *Name);
LLVMValueRef LLVMBuildFDiv(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildURem(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildSRem(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildFRem(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildShl(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildLShr(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildAShr(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildAnd(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name);
LLVMValueRef LLVMBuildOr(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name);
LLVMValueRef LLVMBuildXor(LLVMBuilderRef, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name);
LLVMValueRef LLVMBuildBinOp(LLVMBuilderRef B, LLVMOpcode Op,
                            LLVMValueRef LHS, LLVMValueRef RHS,
                            const char *Name);
LLVMValueRef LLVMBuildNeg(LLVMBuilderRef, LLVMValueRef V, const char *Name);
LLVMValueRef LLVMBuildNSWNeg(LLVMBuilderRef B, LLVMValueRef V,
                             const char *Name);
LLVM_ATTRIBUTE_C_DEPRECATED(LLVMValueRef LLVMBuildNUWNeg(LLVMBuilderRef B,
                                                         LLVMValueRef V,
                                                         const char *Name),
                            "Use LLVMBuildNeg + LLVMSetNUW instead.");
LLVMValueRef LLVMBuildFNeg(LLVMBuilderRef, LLVMValueRef V, const char *Name);
LLVMValueRef LLVMBuildNot(LLVMBuilderRef, LLVMValueRef V, const char *Name);

LLVMBool LLVMGetNUW(LLVMValueRef ArithInst);
void LLVMSetNUW(LLVMValueRef ArithInst, LLVMBool HasNUW);
LLVMBool LLVMGetNSW(LLVMValueRef ArithInst);
void LLVMSetNSW(LLVMValueRef ArithInst, LLVMBool HasNSW);
LLVMBool LLVMGetExact(LLVMValueRef DivOrShrInst);
void LLVMSetExact(LLVMValueRef DivOrShrInst, LLVMBool IsExact);

/**
 * Gets if the instruction has the non-negative flag set.
 * Only valid for zext instructions.
 */
LLVMBool LLVMGetNNeg(LLVMValueRef NonNegInst);
/**
 * Sets the non-negative flag for the instruction.
 * Only valid for zext instructions.
 */
void LLVMSetNNeg(LLVMValueRef NonNegInst, LLVMBool IsNonNeg);

/**
 * Get the flags for which fast-math-style optimizations are allowed for this
 * value.
 *
 * Only valid on floating point instructions.
 * @see LLVMCanValueUseFastMathFlags
 */
LLVMFastMathFlags LLVMGetFastMathFlags(LLVMValueRef FPMathInst);

/**
 * Sets the flags for which fast-math-style optimizations are allowed for this
 * value.
 *
 * Only valid on floating point instructions.
 * @see LLVMCanValueUseFastMathFlags
 */
void LLVMSetFastMathFlags(LLVMValueRef FPMathInst, LLVMFastMathFlags FMF);

/**
 * Check if a given value can potentially have fast math flags.
 *
 * Will return true for floating point arithmetic instructions, and for select,
 * phi, and call instructions whose type is a floating point type, or a vector
 * or array thereof. See https://llvm.org/docs/LangRef.html#fast-math-flags
 */
LLVMBool LLVMCanValueUseFastMathFlags(LLVMValueRef Inst);

/**
 * Gets whether the instruction has the disjoint flag set.
 * Only valid for or instructions.
 */
LLVMBool LLVMGetIsDisjoint(LLVMValueRef Inst);
/**
 * Sets the disjoint flag for the instruction.
 * Only valid for or instructions.
 */
void LLVMSetIsDisjoint(LLVMValueRef Inst, LLVMBool IsDisjoint);

/* Memory */
LLVMValueRef LLVMBuildMalloc(LLVMBuilderRef, LLVMTypeRef Ty, const char *Name);
LLVMValueRef LLVMBuildArrayMalloc(LLVMBuilderRef, LLVMTypeRef Ty,
                                  LLVMValueRef Val, const char *Name);

/**
 * Creates and inserts a memset to the specified pointer and the
 * specified value.
 *
 * @see llvm::IRRBuilder::CreateMemSet()
 */
LLVMValueRef LLVMBuildMemSet(LLVMBuilderRef B, LLVMValueRef Ptr,
                             LLVMValueRef Val, LLVMValueRef Len,
                             unsigned Align);
/**
 * Creates and inserts a memcpy between the specified pointers.
 *
 * @see llvm::IRRBuilder::CreateMemCpy()
 */
LLVMValueRef LLVMBuildMemCpy(LLVMBuilderRef B,
                             LLVMValueRef Dst, unsigned DstAlign,
                             LLVMValueRef Src, unsigned SrcAlign,
                             LLVMValueRef Size);
/**
 * Creates and inserts a memmove between the specified pointers.
 *
 * @see llvm::IRRBuilder::CreateMemMove()
 */
LLVMValueRef LLVMBuildMemMove(LLVMBuilderRef B,
                              LLVMValueRef Dst, unsigned DstAlign,
                              LLVMValueRef Src, unsigned SrcAlign,
                              LLVMValueRef Size);

LLVMValueRef LLVMBuildAlloca(LLVMBuilderRef, LLVMTypeRef Ty, const char *Name);
LLVMValueRef LLVMBuildArrayAlloca(LLVMBuilderRef, LLVMTypeRef Ty,
                                  LLVMValueRef Val, const char *Name);
LLVMValueRef LLVMBuildFree(LLVMBuilderRef, LLVMValueRef PointerVal);
LLVMValueRef LLVMBuildLoad2(LLVMBuilderRef, LLVMTypeRef Ty,
                            LLVMValueRef PointerVal, const char *Name);
LLVMValueRef LLVMBuildStore(LLVMBuilderRef, LLVMValueRef Val, LLVMValueRef Ptr);
LLVMValueRef LLVMBuildGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
                           LLVMValueRef Pointer, LLVMValueRef *Indices,
                           unsigned NumIndices, const char *Name);
LLVMValueRef LLVMBuildInBoundsGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
                                   LLVMValueRef Pointer, LLVMValueRef *Indices,
                                   unsigned NumIndices, const char *Name);
/**
 * Creates a GetElementPtr instruction. Similar to LLVMBuildGEP2, but allows
 * specifying the no-wrap flags.
 *
 * @see llvm::IRBuilder::CreateGEP()
 */
LLVMValueRef LLVMBuildGEPWithNoWrapFlags(LLVMBuilderRef B, LLVMTypeRef Ty,
                                         LLVMValueRef Pointer,
                                         LLVMValueRef *Indices,
                                         unsigned NumIndices, const char *Name,
                                         LLVMGEPNoWrapFlags NoWrapFlags);
LLVMValueRef LLVMBuildStructGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
                                 LLVMValueRef Pointer, unsigned Idx,
                                 const char *Name);
LLVMValueRef LLVMBuildGlobalString(LLVMBuilderRef B, const char *Str,
                                   const char *Name);
LLVMValueRef LLVMBuildGlobalStringPtr(LLVMBuilderRef B, const char *Str,
                                      const char *Name);
LLVMBool LLVMGetVolatile(LLVMValueRef MemoryAccessInst);
void LLVMSetVolatile(LLVMValueRef MemoryAccessInst, LLVMBool IsVolatile);
LLVMBool LLVMGetWeak(LLVMValueRef CmpXchgInst);
void LLVMSetWeak(LLVMValueRef CmpXchgInst, LLVMBool IsWeak);
LLVMAtomicOrdering LLVMGetOrdering(LLVMValueRef MemoryAccessInst);
void LLVMSetOrdering(LLVMValueRef MemoryAccessInst, LLVMAtomicOrdering Ordering);
LLVMAtomicRMWBinOp LLVMGetAtomicRMWBinOp(LLVMValueRef AtomicRMWInst);
void LLVMSetAtomicRMWBinOp(LLVMValueRef AtomicRMWInst, LLVMAtomicRMWBinOp BinOp);

/* Casts */
LLVMValueRef LLVMBuildTrunc(LLVMBuilderRef, LLVMValueRef Val,
                            LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildZExt(LLVMBuilderRef, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildSExt(LLVMBuilderRef, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildFPToUI(LLVMBuilderRef, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildFPToSI(LLVMBuilderRef, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildUIToFP(LLVMBuilderRef, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildSIToFP(LLVMBuilderRef, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildFPTrunc(LLVMBuilderRef, LLVMValueRef Val,
                              LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildFPExt(LLVMBuilderRef, LLVMValueRef Val,
                            LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildPtrToInt(LLVMBuilderRef, LLVMValueRef Val,
                               LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildIntToPtr(LLVMBuilderRef, LLVMValueRef Val,
                               LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildBitCast(LLVMBuilderRef, LLVMValueRef Val,
                              LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildAddrSpaceCast(LLVMBuilderRef, LLVMValueRef Val,
                                    LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildZExtOrBitCast(LLVMBuilderRef, LLVMValueRef Val,
                                    LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildSExtOrBitCast(LLVMBuilderRef, LLVMValueRef Val,
                                    LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildTruncOrBitCast(LLVMBuilderRef, LLVMValueRef Val,
                                     LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildCast(LLVMBuilderRef B, LLVMOpcode Op, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildPointerCast(LLVMBuilderRef, LLVMValueRef Val,
                                  LLVMTypeRef DestTy, const char *Name);
LLVMValueRef LLVMBuildIntCast2(LLVMBuilderRef, LLVMValueRef Val,
                               LLVMTypeRef DestTy, LLVMBool IsSigned,
                               const char *Name);
LLVMValueRef LLVMBuildFPCast(LLVMBuilderRef, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name);

/** Deprecated: This cast is always signed. Use LLVMBuildIntCast2 instead. */
LLVMValueRef LLVMBuildIntCast(LLVMBuilderRef, LLVMValueRef Val, /*Signed cast!*/
                              LLVMTypeRef DestTy, const char *Name);

LLVMOpcode LLVMGetCastOpcode(LLVMValueRef Src, LLVMBool SrcIsSigned,
                             LLVMTypeRef DestTy, LLVMBool DestIsSigned);

/* Comparisons */
LLVMValueRef LLVMBuildICmp(LLVMBuilderRef, LLVMIntPredicate Op,
                           LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);
LLVMValueRef LLVMBuildFCmp(LLVMBuilderRef, LLVMRealPredicate Op,
                           LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name);

/* Miscellaneous instructions */
LLVMValueRef LLVMBuildPhi(LLVMBuilderRef, LLVMTypeRef Ty, const char *Name);
LLVMValueRef LLVMBuildCall2(LLVMBuilderRef, LLVMTypeRef, LLVMValueRef Fn,
                            LLVMValueRef *Args, unsigned NumArgs,
                            const char *Name);
LLVMValueRef
LLVMBuildCallWithOperandBundles(LLVMBuilderRef, LLVMTypeRef, LLVMValueRef Fn,
                                LLVMValueRef *Args, unsigned NumArgs,
                                LLVMOperandBundleRef *Bundles,
                                unsigned NumBundles, const char *Name);
LLVMValueRef LLVMBuildSelect(LLVMBuilderRef, LLVMValueRef If,
                             LLVMValueRef Then, LLVMValueRef Else,
                             const char *Name);
LLVMValueRef LLVMBuildVAArg(LLVMBuilderRef, LLVMValueRef List, LLVMTypeRef Ty,
                            const char *Name);
LLVMValueRef LLVMBuildExtractElement(LLVMBuilderRef, LLVMValueRef VecVal,
                                     LLVMValueRef Index, const char *Name);
LLVMValueRef LLVMBuildInsertElement(LLVMBuilderRef, LLVMValueRef VecVal,
                                    LLVMValueRef EltVal, LLVMValueRef Index,
                                    const char *Name);
LLVMValueRef LLVMBuildShuffleVector(LLVMBuilderRef, LLVMValueRef V1,
                                    LLVMValueRef V2, LLVMValueRef Mask,
                                    const char *Name);
LLVMValueRef LLVMBuildExtractValue(LLVMBuilderRef, LLVMValueRef AggVal,
                                   unsigned Index, const char *Name);
LLVMValueRef LLVMBuildInsertValue(LLVMBuilderRef, LLVMValueRef AggVal,
                                  LLVMValueRef EltVal, unsigned Index,
                                  const char *Name);
LLVMValueRef LLVMBuildFreeze(LLVMBuilderRef, LLVMValueRef Val,
                             const char *Name);

LLVMValueRef LLVMBuildIsNull(LLVMBuilderRef, LLVMValueRef Val,
                             const char *Name);
LLVMValueRef LLVMBuildIsNotNull(LLVMBuilderRef, LLVMValueRef Val,
                                const char *Name);
LLVMValueRef LLVMBuildPtrDiff2(LLVMBuilderRef, LLVMTypeRef ElemTy,
                               LLVMValueRef LHS, LLVMValueRef RHS,
                               const char *Name);
LLVMValueRef LLVMBuildFence(LLVMBuilderRef B, LLVMAtomicOrdering ordering,
                            LLVMBool singleThread, const char *Name);
LLVMValueRef LLVMBuildAtomicRMW(LLVMBuilderRef B, LLVMAtomicRMWBinOp op,
                                LLVMValueRef PTR, LLVMValueRef Val,
                                LLVMAtomicOrdering ordering,
                                LLVMBool singleThread);
LLVMValueRef LLVMBuildAtomicCmpXchg(LLVMBuilderRef B, LLVMValueRef Ptr,
                                    LLVMValueRef Cmp, LLVMValueRef New,
                                    LLVMAtomicOrdering SuccessOrdering,
                                    LLVMAtomicOrdering FailureOrdering,
                                    LLVMBool SingleThread);

/**
 * Get the number of elements in the mask of a ShuffleVector instruction.
 */
unsigned LLVMGetNumMaskElements(LLVMValueRef ShuffleVectorInst);

/**
 * \returns a constant that specifies that the result of a \c ShuffleVectorInst
 * is undefined.
 */
int LLVMGetUndefMaskElem(void);

/**
 * Get the mask value at position Elt in the mask of a ShuffleVector
 * instruction.
 *
 * \Returns the result of \c LLVMGetUndefMaskElem() if the mask value is
 * poison at that position.
 */
int LLVMGetMaskValue(LLVMValueRef ShuffleVectorInst, unsigned Elt);

LLVMBool LLVMIsAtomicSingleThread(LLVMValueRef AtomicInst);
void LLVMSetAtomicSingleThread(LLVMValueRef AtomicInst, LLVMBool SingleThread);

LLVMAtomicOrdering LLVMGetCmpXchgSuccessOrdering(LLVMValueRef CmpXchgInst);
void LLVMSetCmpXchgSuccessOrdering(LLVMValueRef CmpXchgInst,
                                   LLVMAtomicOrdering Ordering);
LLVMAtomicOrdering LLVMGetCmpXchgFailureOrdering(LLVMValueRef CmpXchgInst);
void LLVMSetCmpXchgFailureOrdering(LLVMValueRef CmpXchgInst,
                                   LLVMAtomicOrdering Ordering);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreModuleProvider Module Providers
 *
 * @{
 */

/**
 * Changes the type of M so it can be passed to FunctionPassManagers and the
 * JIT.  They take ModuleProviders for historical reasons.
 */
LLVMModuleProviderRef
LLVMCreateModuleProviderForExistingModule(LLVMModuleRef M);

/**
 * Destroys the module M.
 */
void LLVMDisposeModuleProvider(LLVMModuleProviderRef M);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreMemoryBuffers Memory Buffers
 *
 * @{
 */

LLVMBool LLVMCreateMemoryBufferWithContentsOfFile(const char *Path,
                                                  LLVMMemoryBufferRef *OutMemBuf,
                                                  char **OutMessage);
LLVMBool LLVMCreateMemoryBufferWithSTDIN(LLVMMemoryBufferRef *OutMemBuf,
                                         char **OutMessage);
LLVMMemoryBufferRef LLVMCreateMemoryBufferWithMemoryRange(const char *InputData,
                                                          size_t InputDataLength,
                                                          const char *BufferName,
                                                          LLVMBool RequiresNullTerminator);
LLVMMemoryBufferRef LLVMCreateMemoryBufferWithMemoryRangeCopy(const char *InputData,
                                                              size_t InputDataLength,
                                                              const char *BufferName);
const char *LLVMGetBufferStart(LLVMMemoryBufferRef MemBuf);
size_t LLVMGetBufferSize(LLVMMemoryBufferRef MemBuf);
void LLVMDisposeMemoryBuffer(LLVMMemoryBufferRef MemBuf);

/**
 * @}
 */

/**
 * @defgroup LLVMCCorePassManagers Pass Managers
 * @ingroup LLVMCCore
 *
 * @{
 */

/** Constructs a new whole-module pass pipeline. This type of pipeline is
    suitable for link-time optimization and whole-module transformations.
    @see llvm::PassManager::PassManager */
LLVMPassManagerRef LLVMCreatePassManager(void);

/** Constructs a new function-by-function pass pipeline over the module
    provider. It does not take ownership of the module provider. This type of
    pipeline is suitable for code generation and JIT compilation tasks.
    @see llvm::FunctionPassManager::FunctionPassManager */
LLVMPassManagerRef LLVMCreateFunctionPassManagerForModule(LLVMModuleRef M);

/** Deprecated: Use LLVMCreateFunctionPassManagerForModule instead. */
LLVMPassManagerRef LLVMCreateFunctionPassManager(LLVMModuleProviderRef MP);

/** Initializes, executes on the provided module, and finalizes all of the
    passes scheduled in the pass manager. Returns 1 if any of the passes
    modified the module, 0 otherwise.
    @see llvm::PassManager::run(Module&) */
LLVMBool LLVMRunPassManager(LLVMPassManagerRef PM, LLVMModuleRef M);

/** Initializes all of the function passes scheduled in the function pass
    manager. Returns 1 if any of the passes modified the module, 0 otherwise.
    @see llvm::FunctionPassManager::doInitialization */
LLVMBool LLVMInitializeFunctionPassManager(LLVMPassManagerRef FPM);

/** Executes all of the function passes scheduled in the function pass manager
    on the provided function. Returns 1 if any of the passes modified the
    function, false otherwise.
    @see llvm::FunctionPassManager::run(Function&) */
LLVMBool LLVMRunFunctionPassManager(LLVMPassManagerRef FPM, LLVMValueRef F);

/** Finalizes all of the function passes scheduled in the function pass
    manager. Returns 1 if any of the passes modified the module, 0 otherwise.
    @see llvm::FunctionPassManager::doFinalization */
LLVMBool LLVMFinalizeFunctionPassManager(LLVMPassManagerRef FPM);

/** Frees the memory of a pass pipeline. For function pipelines, does not free
    the module provider.
    @see llvm::PassManagerBase::~PassManagerBase. */
void LLVMDisposePassManager(LLVMPassManagerRef PM);

/**
 * @}
 */

/**
 * @defgroup LLVMCCoreThreading Threading
 *
 * Handle the structures needed to make LLVM safe for multithreading.
 *
 * @{
 */

/** Deprecated: Multi-threading can only be enabled/disabled with the compile
    time define LLVM_ENABLE_THREADS.  This function always returns
    LLVMIsMultithreaded(). */
LLVMBool LLVMStartMultithreaded(void);

/** Deprecated: Multi-threading can only be enabled/disabled with the compile
    time define LLVM_ENABLE_THREADS. */
void LLVMStopMultithreaded(void);

/** Check whether LLVM is executing in thread-safe mode or not.
    @see llvm::llvm_is_multithreaded */
LLVMBool LLVMIsMultithreaded(void);

/**
 * @}
 */

/**
 * @}
 */

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif /* LLVM_C_CORE_H */
