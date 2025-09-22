/*===-- llvm-c/Support.h - C Interface Types declarations ---------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines types used by the C interface to LLVM.                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TYPES_H
#define LLVM_C_TYPES_H

#include "llvm-c/DataTypes.h"
#include "llvm-c/ExternC.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCSupportTypes Types and Enumerations
 *
 * @{
 */

typedef int LLVMBool;

/* Opaque types. */

/**
 * LLVM uses a polymorphic type hierarchy which C cannot represent, therefore
 * parameters must be passed as base types. Despite the declared types, most
 * of the functions provided operate only on branches of the type hierarchy.
 * The declared parameter names are descriptive and specify which type is
 * required. Additionally, each type hierarchy is documented along with the
 * functions that operate upon it. For more detail, refer to LLVM's C++ code.
 * If in doubt, refer to Core.cpp, which performs parameter downcasts in the
 * form unwrap<RequiredType>(Param).
 */

/**
 * Used to pass regions of memory through LLVM interfaces.
 *
 * @see llvm::MemoryBuffer
 */
typedef struct LLVMOpaqueMemoryBuffer *LLVMMemoryBufferRef;

/**
 * The top-level container for all LLVM global data. See the LLVMContext class.
 */
typedef struct LLVMOpaqueContext *LLVMContextRef;

/**
 * The top-level container for all other LLVM Intermediate Representation (IR)
 * objects.
 *
 * @see llvm::Module
 */
typedef struct LLVMOpaqueModule *LLVMModuleRef;

/**
 * Each value in the LLVM IR has a type, an LLVMTypeRef.
 *
 * @see llvm::Type
 */
typedef struct LLVMOpaqueType *LLVMTypeRef;

/**
 * Represents an individual value in LLVM IR.
 *
 * This models llvm::Value.
 */
typedef struct LLVMOpaqueValue *LLVMValueRef;

/**
 * Represents a basic block of instructions in LLVM IR.
 *
 * This models llvm::BasicBlock.
 */
typedef struct LLVMOpaqueBasicBlock *LLVMBasicBlockRef;

/**
 * Represents an LLVM Metadata.
 *
 * This models llvm::Metadata.
 */
typedef struct LLVMOpaqueMetadata *LLVMMetadataRef;

/**
 * Represents an LLVM Named Metadata Node.
 *
 * This models llvm::NamedMDNode.
 */
typedef struct LLVMOpaqueNamedMDNode *LLVMNamedMDNodeRef;

/**
 * Represents an entry in a Global Object's metadata attachments.
 *
 * This models std::pair<unsigned, MDNode *>
 */
typedef struct LLVMOpaqueValueMetadataEntry LLVMValueMetadataEntry;

/**
 * Represents an LLVM basic block builder.
 *
 * This models llvm::IRBuilder.
 */
typedef struct LLVMOpaqueBuilder *LLVMBuilderRef;

/**
 * Represents an LLVM debug info builder.
 *
 * This models llvm::DIBuilder.
 */
typedef struct LLVMOpaqueDIBuilder *LLVMDIBuilderRef;

/**
 * Interface used to provide a module to JIT or interpreter.
 * This is now just a synonym for llvm::Module, but we have to keep using the
 * different type to keep binary compatibility.
 */
typedef struct LLVMOpaqueModuleProvider *LLVMModuleProviderRef;

/** @see llvm::PassManagerBase */
typedef struct LLVMOpaquePassManager *LLVMPassManagerRef;

/**
 * Used to get the users and usees of a Value.
 *
 * @see llvm::Use */
typedef struct LLVMOpaqueUse *LLVMUseRef;

/**
 * @see llvm::OperandBundleDef
 */
typedef struct LLVMOpaqueOperandBundle *LLVMOperandBundleRef;

/**
 * Used to represent an attributes.
 *
 * @see llvm::Attribute
 */
typedef struct LLVMOpaqueAttributeRef *LLVMAttributeRef;

/**
 * @see llvm::DiagnosticInfo
 */
typedef struct LLVMOpaqueDiagnosticInfo *LLVMDiagnosticInfoRef;

/**
 * @see llvm::Comdat
 */
typedef struct LLVMComdat *LLVMComdatRef;

/**
 * @see llvm::Module::ModuleFlagEntry
 */
typedef struct LLVMOpaqueModuleFlagEntry LLVMModuleFlagEntry;

/**
 * @see llvm::JITEventListener
 */
typedef struct LLVMOpaqueJITEventListener *LLVMJITEventListenerRef;

/**
 * @see llvm::object::Binary
 */
typedef struct LLVMOpaqueBinary *LLVMBinaryRef;

/**
 * @see llvm::DbgRecord
 */
typedef struct LLVMOpaqueDbgRecord *LLVMDbgRecordRef;

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif
