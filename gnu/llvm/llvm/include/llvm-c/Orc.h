/*===---------------- llvm-c/Orc.h - OrcV2 C bindings -----------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to libLLVMOrcJIT.a, which implements  *|
|* JIT compilation of LLVM IR. Minimal documentation of C API specific issues *|
|* (especially memory ownership rules) is provided. Core Orc concepts are     *|
|* documented in llvm/docs/ORCv2.rst and APIs are documented in the C++       *|
|* headers                                                                    *|
|*                                                                            *|
|* Many exotic languages can interoperate with C code but have a harder time  *|
|* with C++ due to name mangling. So in addition to C, this interface enables *|
|* tools written in such languages.                                           *|
|*                                                                            *|
|* Note: This interface is experimental. It is *NOT* stable, and may be       *|
|*       changed without warning. Only C API usage documentation is           *|
|*       provided. See the C++ documentation for all higher level ORC API     *|
|*       details.                                                             *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_ORC_H
#define LLVM_C_ORC_H

#include "llvm-c/Error.h"
#include "llvm-c/TargetMachine.h"
#include "llvm-c/Types.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCExecutionEngineORC On-Request-Compilation
 * @ingroup LLVMCExecutionEngine
 *
 * @{
 */

/**
 * Represents an address in the executor process.
 */
typedef uint64_t LLVMOrcJITTargetAddress;

/**
 * Represents an address in the executor process.
 */
typedef uint64_t LLVMOrcExecutorAddress;

/**
 * Represents generic linkage flags for a symbol definition.
 */
typedef enum {
  LLVMJITSymbolGenericFlagsNone = 0,
  LLVMJITSymbolGenericFlagsExported = 1U << 0,
  LLVMJITSymbolGenericFlagsWeak = 1U << 1,
  LLVMJITSymbolGenericFlagsCallable = 1U << 2,
  LLVMJITSymbolGenericFlagsMaterializationSideEffectsOnly = 1U << 3
} LLVMJITSymbolGenericFlags;

/**
 * Represents target specific flags for a symbol definition.
 */
typedef uint8_t LLVMJITSymbolTargetFlags;

/**
 * Represents the linkage flags for a symbol definition.
 */
typedef struct {
  uint8_t GenericFlags;
  uint8_t TargetFlags;
} LLVMJITSymbolFlags;

/**
 * Represents an evaluated symbol address and flags.
 */
typedef struct {
  LLVMOrcExecutorAddress Address;
  LLVMJITSymbolFlags Flags;
} LLVMJITEvaluatedSymbol;

/**
 * A reference to an orc::ExecutionSession instance.
 */
typedef struct LLVMOrcOpaqueExecutionSession *LLVMOrcExecutionSessionRef;

/**
 * Error reporter function.
 */
typedef void (*LLVMOrcErrorReporterFunction)(void *Ctx, LLVMErrorRef Err);

/**
 * A reference to an orc::SymbolStringPool.
 */
typedef struct LLVMOrcOpaqueSymbolStringPool *LLVMOrcSymbolStringPoolRef;

/**
 * A reference to an orc::SymbolStringPool table entry.
 */
typedef struct LLVMOrcOpaqueSymbolStringPoolEntry
    *LLVMOrcSymbolStringPoolEntryRef;

/**
 * Represents a pair of a symbol name and LLVMJITSymbolFlags.
 */
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMJITSymbolFlags Flags;
} LLVMOrcCSymbolFlagsMapPair;

/**
 * Represents a list of (SymbolStringPtr, JITSymbolFlags) pairs that can be used
 * to construct a SymbolFlagsMap.
 */
typedef LLVMOrcCSymbolFlagsMapPair *LLVMOrcCSymbolFlagsMapPairs;

/**
 * Represents a pair of a symbol name and an evaluated symbol.
 */
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMJITEvaluatedSymbol Sym;
} LLVMOrcCSymbolMapPair;

/**
 * Represents a list of (SymbolStringPtr, JITEvaluatedSymbol) pairs that can be
 * used to construct a SymbolMap.
 */
typedef LLVMOrcCSymbolMapPair *LLVMOrcCSymbolMapPairs;

/**
 * Represents a SymbolAliasMapEntry
 */
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMJITSymbolFlags Flags;
} LLVMOrcCSymbolAliasMapEntry;

/**
 * Represents a pair of a symbol name and SymbolAliasMapEntry.
 */
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMOrcCSymbolAliasMapEntry Entry;
} LLVMOrcCSymbolAliasMapPair;

/**
 * Represents a list of (SymbolStringPtr, (SymbolStringPtr, JITSymbolFlags))
 * pairs that can be used to construct a SymbolFlagsMap.
 */
typedef LLVMOrcCSymbolAliasMapPair *LLVMOrcCSymbolAliasMapPairs;

/**
 * A reference to an orc::JITDylib instance.
 */
typedef struct LLVMOrcOpaqueJITDylib *LLVMOrcJITDylibRef;

/**
 * Represents a list of LLVMOrcSymbolStringPoolEntryRef and the associated
 * length.
 */
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef *Symbols;
  size_t Length;
} LLVMOrcCSymbolsList;

/**
 * Represents a pair of a JITDylib and LLVMOrcCSymbolsList.
 */
typedef struct {
  LLVMOrcJITDylibRef JD;
  LLVMOrcCSymbolsList Names;
} LLVMOrcCDependenceMapPair;

/**
 * Represents a list of (JITDylibRef, (LLVMOrcSymbolStringPoolEntryRef*,
 * size_t)) pairs that can be used to construct a SymbolDependenceMap.
 */
typedef LLVMOrcCDependenceMapPair *LLVMOrcCDependenceMapPairs;

/**
 * A set of symbols that share dependencies.
 */
typedef struct {
  LLVMOrcCSymbolsList Symbols;
  LLVMOrcCDependenceMapPairs Dependencies;
  size_t NumDependencies;
} LLVMOrcCSymbolDependenceGroup;

/**
 * Lookup kind. This can be used by definition generators when deciding whether
 * to produce a definition for a requested symbol.
 *
 * This enum should be kept in sync with llvm::orc::LookupKind.
 */
typedef enum {
  LLVMOrcLookupKindStatic,
  LLVMOrcLookupKindDLSym
} LLVMOrcLookupKind;

/**
 * JITDylib lookup flags. This can be used by definition generators when
 * deciding whether to produce a definition for a requested symbol.
 *
 * This enum should be kept in sync with llvm::orc::JITDylibLookupFlags.
 */
typedef enum {
  LLVMOrcJITDylibLookupFlagsMatchExportedSymbolsOnly,
  LLVMOrcJITDylibLookupFlagsMatchAllSymbols
} LLVMOrcJITDylibLookupFlags;

/**
 * An element type for a JITDylib search order.
 */
typedef struct {
  LLVMOrcJITDylibRef JD;
  LLVMOrcJITDylibLookupFlags JDLookupFlags;
} LLVMOrcCJITDylibSearchOrderElement;

/**
 * A JITDylib search order.
 *
 * The list is terminated with an element containing a null pointer for the JD
 * field.
 */
typedef LLVMOrcCJITDylibSearchOrderElement *LLVMOrcCJITDylibSearchOrder;

/**
 * Symbol lookup flags for lookup sets. This should be kept in sync with
 * llvm::orc::SymbolLookupFlags.
 */
typedef enum {
  LLVMOrcSymbolLookupFlagsRequiredSymbol,
  LLVMOrcSymbolLookupFlagsWeaklyReferencedSymbol
} LLVMOrcSymbolLookupFlags;

/**
 * An element type for a symbol lookup set.
 */
typedef struct {
  LLVMOrcSymbolStringPoolEntryRef Name;
  LLVMOrcSymbolLookupFlags LookupFlags;
} LLVMOrcCLookupSetElement;

/**
 * A set of symbols to look up / generate.
 *
 * The list is terminated with an element containing a null pointer for the
 * Name field.
 *
 * If a client creates an instance of this type then they are responsible for
 * freeing it, and for ensuring that all strings have been retained over the
 * course of its life. Clients receiving a copy from a callback are not
 * responsible for managing lifetime or retain counts.
 */
typedef LLVMOrcCLookupSetElement *LLVMOrcCLookupSet;

/**
 * A reference to a uniquely owned orc::MaterializationUnit instance.
 */
typedef struct LLVMOrcOpaqueMaterializationUnit *LLVMOrcMaterializationUnitRef;

/**
 * A reference to a uniquely owned orc::MaterializationResponsibility instance.
 *
 * Ownership must be passed to a lower-level layer in a JIT stack.
 */
typedef struct LLVMOrcOpaqueMaterializationResponsibility
    *LLVMOrcMaterializationResponsibilityRef;

/**
 * A MaterializationUnit materialize callback.
 *
 * Ownership of the Ctx and MR arguments passes to the callback which must
 * adhere to the LLVMOrcMaterializationResponsibilityRef contract (see comment
 * for that type).
 *
 * If this callback is called then the LLVMOrcMaterializationUnitDestroy
 * callback will NOT be called.
 */
typedef void (*LLVMOrcMaterializationUnitMaterializeFunction)(
    void *Ctx, LLVMOrcMaterializationResponsibilityRef MR);

/**
 * A MaterializationUnit discard callback.
 *
 * Ownership of JD and Symbol remain with the caller: These arguments should
 * not be disposed of or released.
 */
typedef void (*LLVMOrcMaterializationUnitDiscardFunction)(
    void *Ctx, LLVMOrcJITDylibRef JD, LLVMOrcSymbolStringPoolEntryRef Symbol);

/**
 * A MaterializationUnit destruction callback.
 *
 * If a custom MaterializationUnit is destroyed before its Materialize
 * function is called then this function will be called to provide an
 * opportunity for the underlying program representation to be destroyed.
 */
typedef void (*LLVMOrcMaterializationUnitDestroyFunction)(void *Ctx);

/**
 * A reference to an orc::ResourceTracker instance.
 */
typedef struct LLVMOrcOpaqueResourceTracker *LLVMOrcResourceTrackerRef;

/**
 * A reference to an orc::DefinitionGenerator.
 */
typedef struct LLVMOrcOpaqueDefinitionGenerator
    *LLVMOrcDefinitionGeneratorRef;

/**
 * An opaque lookup state object. Instances of this type can be captured to
 * suspend a lookup while a custom generator function attempts to produce a
 * definition.
 *
 * If a client captures a lookup state object then they must eventually call
 * LLVMOrcLookupStateContinueLookup to restart the lookup. This is required
 * in order to release memory allocated for the lookup state, even if errors
 * have occurred while the lookup was suspended (if these errors have made the
 * lookup impossible to complete then it will issue its own error before
 * destruction).
 */
typedef struct LLVMOrcOpaqueLookupState *LLVMOrcLookupStateRef;

/**
 * A custom generator function. This can be used to create a custom generator
 * object using LLVMOrcCreateCustomCAPIDefinitionGenerator. The resulting
 * object can be attached to a JITDylib, via LLVMOrcJITDylibAddGenerator, to
 * receive callbacks when lookups fail to match existing definitions.
 *
 * GeneratorObj will contain the address of the custom generator object.
 *
 * Ctx will contain the context object passed to
 * LLVMOrcCreateCustomCAPIDefinitionGenerator.
 *
 * LookupState will contain a pointer to an LLVMOrcLookupStateRef object. This
 * can optionally be modified to make the definition generation process
 * asynchronous: If the LookupStateRef value is copied, and the original
 * LLVMOrcLookupStateRef set to null, the lookup will be suspended. Once the
 * asynchronous definition process has been completed clients must call
 * LLVMOrcLookupStateContinueLookup to continue the lookup (this should be
 * done unconditionally, even if errors have occurred in the mean time, to
 * free the lookup state memory and notify the query object of the failures).
 * If LookupState is captured this function must return LLVMErrorSuccess.
 *
 * The Kind argument can be inspected to determine the lookup kind (e.g.
 * as-if-during-static-link, or as-if-during-dlsym).
 *
 * The JD argument specifies which JITDylib the definitions should be generated
 * into.
 *
 * The JDLookupFlags argument can be inspected to determine whether the original
 * lookup included non-exported symbols.
 *
 * Finally, the LookupSet argument contains the set of symbols that could not
 * be found in JD already (the set of generation candidates).
 */
typedef LLVMErrorRef (*LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction)(
    LLVMOrcDefinitionGeneratorRef GeneratorObj, void *Ctx,
    LLVMOrcLookupStateRef *LookupState, LLVMOrcLookupKind Kind,
    LLVMOrcJITDylibRef JD, LLVMOrcJITDylibLookupFlags JDLookupFlags,
    LLVMOrcCLookupSet LookupSet, size_t LookupSetSize);

/**
 * Disposer for a custom generator.
 *
 * Will be called by ORC when the JITDylib that the generator is attached to
 * is destroyed.
 */
typedef void (*LLVMOrcDisposeCAPIDefinitionGeneratorFunction)(void *Ctx);

/**
 * Predicate function for SymbolStringPoolEntries.
 */
typedef int (*LLVMOrcSymbolPredicate)(void *Ctx,
                                      LLVMOrcSymbolStringPoolEntryRef Sym);

/**
 * A reference to an orc::ThreadSafeContext instance.
 */
typedef struct LLVMOrcOpaqueThreadSafeContext *LLVMOrcThreadSafeContextRef;

/**
 * A reference to an orc::ThreadSafeModule instance.
 */
typedef struct LLVMOrcOpaqueThreadSafeModule *LLVMOrcThreadSafeModuleRef;

/**
 * A function for inspecting/mutating IR modules, suitable for use with
 * LLVMOrcThreadSafeModuleWithModuleDo.
 */
typedef LLVMErrorRef (*LLVMOrcGenericIRModuleOperationFunction)(
    void *Ctx, LLVMModuleRef M);

/**
 * A reference to an orc::JITTargetMachineBuilder instance.
 */
typedef struct LLVMOrcOpaqueJITTargetMachineBuilder
    *LLVMOrcJITTargetMachineBuilderRef;

/**
 * A reference to an orc::ObjectLayer instance.
 */
typedef struct LLVMOrcOpaqueObjectLayer *LLVMOrcObjectLayerRef;

/**
 * A reference to an orc::ObjectLinkingLayer instance.
 */
typedef struct LLVMOrcOpaqueObjectLinkingLayer *LLVMOrcObjectLinkingLayerRef;

/**
 * A reference to an orc::IRTransformLayer instance.
 */
typedef struct LLVMOrcOpaqueIRTransformLayer *LLVMOrcIRTransformLayerRef;

/**
 * A function for applying transformations as part of an transform layer.
 *
 * Implementations of this type are responsible for managing the lifetime
 * of the Module pointed to by ModInOut: If the LLVMModuleRef value is
 * overwritten then the function is responsible for disposing of the incoming
 * module. If the module is simply accessed/mutated in-place then ownership
 * returns to the caller and the function does not need to do any lifetime
 * management.
 *
 * Clients can call LLVMOrcLLJITGetIRTransformLayer to obtain the transform
 * layer of a LLJIT instance, and use LLVMOrcIRTransformLayerSetTransform
 * to set the function. This can be used to override the default transform
 * layer.
 */
typedef LLVMErrorRef (*LLVMOrcIRTransformLayerTransformFunction)(
    void *Ctx, LLVMOrcThreadSafeModuleRef *ModInOut,
    LLVMOrcMaterializationResponsibilityRef MR);

/**
 * A reference to an orc::ObjectTransformLayer instance.
 */
typedef struct LLVMOrcOpaqueObjectTransformLayer
    *LLVMOrcObjectTransformLayerRef;

/**
 * A function for applying transformations to an object file buffer.
 *
 * Implementations of this type are responsible for managing the lifetime
 * of the memory buffer pointed to by ObjInOut: If the LLVMMemoryBufferRef
 * value is overwritten then the function is responsible for disposing of the
 * incoming buffer. If the buffer is simply accessed/mutated in-place then
 * ownership returns to the caller and the function does not need to do any
 * lifetime management.
 *
 * The transform is allowed to return an error, in which case the ObjInOut
 * buffer should be disposed of and set to null.
 */
typedef LLVMErrorRef (*LLVMOrcObjectTransformLayerTransformFunction)(
    void *Ctx, LLVMMemoryBufferRef *ObjInOut);

/**
 * A reference to an orc::IndirectStubsManager instance.
 */
typedef struct LLVMOrcOpaqueIndirectStubsManager
    *LLVMOrcIndirectStubsManagerRef;

/**
 * A reference to an orc::LazyCallThroughManager instance.
 */
typedef struct LLVMOrcOpaqueLazyCallThroughManager
    *LLVMOrcLazyCallThroughManagerRef;

/**
 * A reference to an orc::DumpObjects object.
 *
 * Can be used to dump object files to disk with unique names. Useful as an
 * ObjectTransformLayer transform.
 */
typedef struct LLVMOrcOpaqueDumpObjects *LLVMOrcDumpObjectsRef;

/**
 * Attach a custom error reporter function to the ExecutionSession.
 *
 * The error reporter will be called to deliver failure notices that can not be
 * directly reported to a caller. For example, failure to resolve symbols in
 * the JIT linker is typically reported via the error reporter (callers
 * requesting definitions from the JIT will typically be delivered a
 * FailureToMaterialize error instead).
 */
void LLVMOrcExecutionSessionSetErrorReporter(
    LLVMOrcExecutionSessionRef ES, LLVMOrcErrorReporterFunction ReportError,
    void *Ctx);

/**
 * Return a reference to the SymbolStringPool for an ExecutionSession.
 *
 * Ownership of the pool remains with the ExecutionSession: The caller is
 * not required to free the pool.
 */
LLVMOrcSymbolStringPoolRef
LLVMOrcExecutionSessionGetSymbolStringPool(LLVMOrcExecutionSessionRef ES);

/**
 * Clear all unreferenced symbol string pool entries.
 *
 * This can be called at any time to release unused entries in the
 * ExecutionSession's string pool. Since it locks the pool (preventing
 * interning of any new strings) it is recommended that it only be called
 * infrequently, ideally when the caller has reason to believe that some
 * entries will have become unreferenced, e.g. after removing a module or
 * closing a JITDylib.
 */
void LLVMOrcSymbolStringPoolClearDeadEntries(LLVMOrcSymbolStringPoolRef SSP);

/**
 * Intern a string in the ExecutionSession's SymbolStringPool and return a
 * reference to it. This increments the ref-count of the pool entry, and the
 * returned value should be released once the client is done with it by
 * calling LLVMOrcReleaseSymbolStringPoolEntry.
 *
 * Since strings are uniqued within the SymbolStringPool
 * LLVMOrcSymbolStringPoolEntryRefs can be compared by value to test string
 * equality.
 *
 * Note that this function does not perform linker-mangling on the string.
 */
LLVMOrcSymbolStringPoolEntryRef
LLVMOrcExecutionSessionIntern(LLVMOrcExecutionSessionRef ES, const char *Name);

/**
 * Callback type for ExecutionSession lookups.
 *
 * If Err is LLVMErrorSuccess then Result will contain a pointer to a
 * list of ( SymbolStringPtr, JITEvaluatedSymbol ) pairs of length NumPairs.
 *
 * If Err is a failure value then Result and Ctx are undefined and should
 * not be accessed. The Callback is responsible for handling the error
 * value (e.g. by calling LLVMGetErrorMessage + LLVMDisposeErrorMessage).
 *
 * The caller retains ownership of the Result array and will release all
 * contained symbol names. Clients are responsible for retaining any symbol
 * names that they wish to hold after the function returns.
 */
typedef void (*LLVMOrcExecutionSessionLookupHandleResultFunction)(
    LLVMErrorRef Err, LLVMOrcCSymbolMapPairs Result, size_t NumPairs,
    void *Ctx);

/**
 * Look up symbols in an execution session.
 *
 * This is a wrapper around the general ExecutionSession::lookup function.
 *
 * The SearchOrder argument contains a list of (JITDylibs, JITDylibSearchFlags)
 * pairs that describe the search order. The JITDylibs will be searched in the
 * given order to try to find the symbols in the Symbols argument.
 *
 * The Symbols argument should contain a null-terminated array of
 * (SymbolStringPtr, SymbolLookupFlags) pairs describing the symbols to be
 * searched for. This function takes ownership of the elements of the Symbols
 * array. The Name fields of the Symbols elements are taken to have been
 * retained by the client for this function. The client should *not* release the
 * Name fields, but are still responsible for destroying the array itself.
 *
 * The HandleResult function will be called once all searched for symbols have
 * been found, or an error occurs. The HandleResult function will be passed an
 * LLVMErrorRef indicating success or failure, and (on success) a
 * null-terminated LLVMOrcCSymbolMapPairs array containing the function result,
 * and the Ctx value passed to the lookup function.
 *
 * The client is fully responsible for managing the lifetime of the Ctx object.
 * A common idiom is to allocate the context prior to the lookup and deallocate
 * it in the handler.
 *
 * THIS API IS EXPERIMENTAL AND LIKELY TO CHANGE IN THE NEAR FUTURE!
 */
void LLVMOrcExecutionSessionLookup(
    LLVMOrcExecutionSessionRef ES, LLVMOrcLookupKind K,
    LLVMOrcCJITDylibSearchOrder SearchOrder, size_t SearchOrderSize,
    LLVMOrcCLookupSet Symbols, size_t SymbolsSize,
    LLVMOrcExecutionSessionLookupHandleResultFunction HandleResult, void *Ctx);

/**
 * Increments the ref-count for a SymbolStringPool entry.
 */
void LLVMOrcRetainSymbolStringPoolEntry(LLVMOrcSymbolStringPoolEntryRef S);

/**
 * Reduces the ref-count for of a SymbolStringPool entry.
 */
void LLVMOrcReleaseSymbolStringPoolEntry(LLVMOrcSymbolStringPoolEntryRef S);

/**
 * Return the c-string for the given symbol. This string will remain valid until
 * the entry is freed (once all LLVMOrcSymbolStringPoolEntryRefs have been
 * released).
 */
const char *LLVMOrcSymbolStringPoolEntryStr(LLVMOrcSymbolStringPoolEntryRef S);

/**
 * Reduces the ref-count of a ResourceTracker.
 */
void LLVMOrcReleaseResourceTracker(LLVMOrcResourceTrackerRef RT);

/**
 * Transfers tracking of all resources associated with resource tracker SrcRT
 * to resource tracker DstRT.
 */
void LLVMOrcResourceTrackerTransferTo(LLVMOrcResourceTrackerRef SrcRT,
                                      LLVMOrcResourceTrackerRef DstRT);

/**
 * Remove all resources associated with the given tracker. See
 * ResourceTracker::remove().
 */
LLVMErrorRef LLVMOrcResourceTrackerRemove(LLVMOrcResourceTrackerRef RT);

/**
 * Dispose of a JITDylib::DefinitionGenerator. This should only be called if
 * ownership has not been passed to a JITDylib (e.g. because some error
 * prevented the client from calling LLVMOrcJITDylibAddGenerator).
 */
void LLVMOrcDisposeDefinitionGenerator(LLVMOrcDefinitionGeneratorRef DG);

/**
 * Dispose of a MaterializationUnit.
 */
void LLVMOrcDisposeMaterializationUnit(LLVMOrcMaterializationUnitRef MU);

/**
 * Create a custom MaterializationUnit.
 *
 * Name is a name for this MaterializationUnit to be used for identification
 * and logging purposes (e.g. if this MaterializationUnit produces an
 * object buffer then the name of that buffer will be derived from this name).
 *
 * The Syms list contains the names and linkages of the symbols provided by this
 * unit. This function takes ownership of the elements of the Syms array. The
 * Name fields of the array elements are taken to have been retained for this
 * function. The client should *not* release the elements of the array, but is
 * still responsible for destroying the array itself.
 *
 * The InitSym argument indicates whether or not this MaterializationUnit
 * contains static initializers. If three are no static initializers (the common
 * case) then this argument should be null. If there are static initializers
 * then InitSym should be set to a unique name that also appears in the Syms
 * list with the LLVMJITSymbolGenericFlagsMaterializationSideEffectsOnly flag
 * set. This function takes ownership of the InitSym, which should have been
 * retained twice on behalf of this function: once for the Syms entry and once
 * for InitSym. If clients wish to use the InitSym value after this function
 * returns they must retain it once more for themselves.
 *
 * If any of the symbols in the Syms list is looked up then the Materialize
 * function will be called.
 *
 * If any of the symbols in the Syms list is overridden then the Discard
 * function will be called.
 *
 * The caller owns the underling MaterializationUnit and is responsible for
 * either passing it to a JITDylib (via LLVMOrcJITDylibDefine) or disposing
 * of it by calling LLVMOrcDisposeMaterializationUnit.
 */
LLVMOrcMaterializationUnitRef LLVMOrcCreateCustomMaterializationUnit(
    const char *Name, void *Ctx, LLVMOrcCSymbolFlagsMapPairs Syms,
    size_t NumSyms, LLVMOrcSymbolStringPoolEntryRef InitSym,
    LLVMOrcMaterializationUnitMaterializeFunction Materialize,
    LLVMOrcMaterializationUnitDiscardFunction Discard,
    LLVMOrcMaterializationUnitDestroyFunction Destroy);

/**
 * Create a MaterializationUnit to define the given symbols as pointing to
 * the corresponding raw addresses.
 *
 * This function takes ownership of the elements of the Syms array. The Name
 * fields of the array elements are taken to have been retained for this
 * function. This allows the following pattern...
 *
 *   size_t NumPairs;
 *   LLVMOrcCSymbolMapPairs Sym;
 *   -- Build Syms array --
 *   LLVMOrcMaterializationUnitRef MU =
 *       LLVMOrcAbsoluteSymbols(Syms, NumPairs);
 *
 * ... without requiring cleanup of the elements of the Sym array afterwards.
 *
 * The client is still responsible for deleting the Sym array itself.
 *
 * If a client wishes to reuse elements of the Sym array after this call they
 * must explicitly retain each of the elements for themselves.
 */
LLVMOrcMaterializationUnitRef
LLVMOrcAbsoluteSymbols(LLVMOrcCSymbolMapPairs Syms, size_t NumPairs);

/**
 * Create a MaterializationUnit to define lazy re-expots. These are callable
 * entry points that call through to the given symbols.
 *
 * This function takes ownership of the CallableAliases array. The Name
 * fields of the array elements are taken to have been retained for this
 * function. This allows the following pattern...
 *
 *   size_t NumPairs;
 *   LLVMOrcCSymbolAliasMapPairs CallableAliases;
 *   -- Build CallableAliases array --
 *   LLVMOrcMaterializationUnitRef MU =
 *      LLVMOrcLazyReexports(LCTM, ISM, JD, CallableAliases, NumPairs);
 *
 * ... without requiring cleanup of the elements of the CallableAliases array afterwards.
 *
 * The client is still responsible for deleting the CallableAliases array itself.
 *
 * If a client wishes to reuse elements of the CallableAliases array after this call they
 * must explicitly retain each of the elements for themselves.
 */
LLVMOrcMaterializationUnitRef LLVMOrcLazyReexports(
    LLVMOrcLazyCallThroughManagerRef LCTM, LLVMOrcIndirectStubsManagerRef ISM,
    LLVMOrcJITDylibRef SourceRef, LLVMOrcCSymbolAliasMapPairs CallableAliases,
    size_t NumPairs);
// TODO: ImplSymbolMad SrcJDLoc

/**
 * Disposes of the passed MaterializationResponsibility object.
 *
 * This should only be done after the symbols covered by the object have either
 * been resolved and emitted (via
 * LLVMOrcMaterializationResponsibilityNotifyResolved and
 * LLVMOrcMaterializationResponsibilityNotifyEmitted) or failed (via
 * LLVMOrcMaterializationResponsibilityFailMaterialization).
 */
void LLVMOrcDisposeMaterializationResponsibility(
    LLVMOrcMaterializationResponsibilityRef MR);

/**
 * Returns the target JITDylib that these symbols are being materialized into.
 */
LLVMOrcJITDylibRef LLVMOrcMaterializationResponsibilityGetTargetDylib(
    LLVMOrcMaterializationResponsibilityRef MR);

/**
 * Returns the ExecutionSession for this MaterializationResponsibility.
 */
LLVMOrcExecutionSessionRef
LLVMOrcMaterializationResponsibilityGetExecutionSession(
    LLVMOrcMaterializationResponsibilityRef MR);

/**
 * Returns the symbol flags map for this responsibility instance.
 *
 * The length of the array is returned in NumPairs and the caller is responsible
 * for the returned memory and needs to call LLVMOrcDisposeCSymbolFlagsMap.
 *
 * To use the returned symbols beyond the livetime of the
 * MaterializationResponsibility requires the caller to retain the symbols
 * explicitly.
 */
LLVMOrcCSymbolFlagsMapPairs LLVMOrcMaterializationResponsibilityGetSymbols(
    LLVMOrcMaterializationResponsibilityRef MR, size_t *NumPairs);

/**
 * Disposes of the passed LLVMOrcCSymbolFlagsMap.
 *
 * Does not release the entries themselves.
 */
void LLVMOrcDisposeCSymbolFlagsMap(LLVMOrcCSymbolFlagsMapPairs Pairs);

/**
 * Returns the initialization pseudo-symbol, if any. This symbol will also
 * be present in the SymbolFlagsMap for this MaterializationResponsibility
 * object.
 *
 * The returned symbol is not retained over any mutating operation of the
 * MaterializationResponsbility or beyond the lifetime thereof.
 */
LLVMOrcSymbolStringPoolEntryRef
LLVMOrcMaterializationResponsibilityGetInitializerSymbol(
    LLVMOrcMaterializationResponsibilityRef MR);

/**
 * Returns the names of any symbols covered by this
 * MaterializationResponsibility object that have queries pending. This
 * information can be used to return responsibility for unrequested symbols
 * back to the JITDylib via the delegate method.
 */
LLVMOrcSymbolStringPoolEntryRef *
LLVMOrcMaterializationResponsibilityGetRequestedSymbols(
    LLVMOrcMaterializationResponsibilityRef MR, size_t *NumSymbols);

/**
 * Disposes of the passed LLVMOrcSymbolStringPoolEntryRef* .
 *
 * Does not release the symbols themselves.
 */
void LLVMOrcDisposeSymbols(LLVMOrcSymbolStringPoolEntryRef *Symbols);

/**
 * Notifies the target JITDylib that the given symbols have been resolved.
 * This will update the given symbols' addresses in the JITDylib, and notify
 * any pending queries on the given symbols of their resolution. The given
 * symbols must be ones covered by this MaterializationResponsibility
 * instance. Individual calls to this method may resolve a subset of the
 * symbols, but all symbols must have been resolved prior to calling emit.
 *
 * This method will return an error if any symbols being resolved have been
 * moved to the error state due to the failure of a dependency. If this
 * method returns an error then clients should log it and call
 * LLVMOrcMaterializationResponsibilityFailMaterialization. If no dependencies
 * have been registered for the symbols covered by this
 * MaterializationResponsibility then this method is guaranteed to return
 * LLVMErrorSuccess.
 */
LLVMErrorRef LLVMOrcMaterializationResponsibilityNotifyResolved(
    LLVMOrcMaterializationResponsibilityRef MR, LLVMOrcCSymbolMapPairs Symbols,
    size_t NumPairs);

/**
 * Notifies the target JITDylib (and any pending queries on that JITDylib)
 * that all symbols covered by this MaterializationResponsibility instance
 * have been emitted.
 *
 * This function takes ownership of the symbols in the Dependencies struct.
 * This allows the following pattern...
 *
 *   LLVMOrcSymbolStringPoolEntryRef Names[] = {...};
 *   LLVMOrcCDependenceMapPair Dependence = {JD, {Names, sizeof(Names)}}
 *   LLVMOrcMaterializationResponsibilityAddDependencies(JD, Name, &Dependence,
 * 1);
 *
 * ... without requiring cleanup of the elements of the Names array afterwards.
 *
 * The client is still responsible for deleting the Dependencies.Names arrays,
 * and the Dependencies array itself.
 *
 * This method will return an error if any symbols being resolved have been
 * moved to the error state due to the failure of a dependency. If this
 * method returns an error then clients should log it and call
 * LLVMOrcMaterializationResponsibilityFailMaterialization.
 * If no dependencies have been registered for the symbols covered by this
 * MaterializationResponsibility then this method is guaranteed to return
 * LLVMErrorSuccess.
 */
LLVMErrorRef LLVMOrcMaterializationResponsibilityNotifyEmitted(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcCSymbolDependenceGroup *SymbolDepGroups, size_t NumSymbolDepGroups);

/**
 * Attempt to claim responsibility for new definitions. This method can be
 * used to claim responsibility for symbols that are added to a
 * materialization unit during the compilation process (e.g. literal pool
 * symbols). Symbol linkage rules are the same as for symbols that are
 * defined up front: duplicate strong definitions will result in errors.
 * Duplicate weak definitions will be discarded (in which case they will
 * not be added to this responsibility instance).
 *
 * This method can be used by materialization units that want to add
 * additional symbols at materialization time (e.g. stubs, compile
 * callbacks, metadata)
 */
LLVMErrorRef LLVMOrcMaterializationResponsibilityDefineMaterializing(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcCSymbolFlagsMapPairs Pairs, size_t NumPairs);

/**
 * Notify all not-yet-emitted covered by this MaterializationResponsibility
 * instance that an error has occurred.
 * This will remove all symbols covered by this MaterializationResponsibility
 * from the target JITDylib, and send an error to any queries waiting on
 * these symbols.
 */
void LLVMOrcMaterializationResponsibilityFailMaterialization(
    LLVMOrcMaterializationResponsibilityRef MR);

/**
 * Transfers responsibility to the given MaterializationUnit for all
 * symbols defined by that MaterializationUnit. This allows
 * materializers to break up work based on run-time information (e.g.
 * by introspecting which symbols have actually been looked up and
 * materializing only those).
 */
LLVMErrorRef LLVMOrcMaterializationResponsibilityReplace(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcMaterializationUnitRef MU);

/**
 * Delegates responsibility for the given symbols to the returned
 * materialization responsibility. Useful for breaking up work between
 * threads, or different kinds of materialization processes.
 *
 * The caller retains responsibility of the the passed
 * MaterializationResponsibility.
 */
LLVMErrorRef LLVMOrcMaterializationResponsibilityDelegate(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcSymbolStringPoolEntryRef *Symbols, size_t NumSymbols,
    LLVMOrcMaterializationResponsibilityRef *Result);

/**
 * Create a "bare" JITDylib.
 *
 * The client is responsible for ensuring that the JITDylib's name is unique,
 * e.g. by calling LLVMOrcExecutionSessionGetJTIDylibByName first.
 *
 * This call does not install any library code or symbols into the newly
 * created JITDylib. The client is responsible for all configuration.
 */
LLVMOrcJITDylibRef
LLVMOrcExecutionSessionCreateBareJITDylib(LLVMOrcExecutionSessionRef ES,
                                          const char *Name);

/**
 * Create a JITDylib.
 *
 * The client is responsible for ensuring that the JITDylib's name is unique,
 * e.g. by calling LLVMOrcExecutionSessionGetJTIDylibByName first.
 *
 * If a Platform is attached to the ExecutionSession then
 * Platform::setupJITDylib will be called to install standard platform symbols
 * (e.g. standard library interposes). If no Platform is installed then this
 * call is equivalent to LLVMExecutionSessionRefCreateBareJITDylib and will
 * always return success.
 */
LLVMErrorRef
LLVMOrcExecutionSessionCreateJITDylib(LLVMOrcExecutionSessionRef ES,
                                      LLVMOrcJITDylibRef *Result,
                                      const char *Name);

/**
 * Returns the JITDylib with the given name, or NULL if no such JITDylib
 * exists.
 */
LLVMOrcJITDylibRef
LLVMOrcExecutionSessionGetJITDylibByName(LLVMOrcExecutionSessionRef ES,
                                         const char *Name);

/**
 * Return a reference to a newly created resource tracker associated with JD.
 * The tracker is returned with an initial ref-count of 1, and must be released
 * with LLVMOrcReleaseResourceTracker when no longer needed.
 */
LLVMOrcResourceTrackerRef
LLVMOrcJITDylibCreateResourceTracker(LLVMOrcJITDylibRef JD);

/**
 * Return a reference to the default resource tracker for the given JITDylib.
 * This operation will increase the retain count of the tracker: Clients should
 * call LLVMOrcReleaseResourceTracker when the result is no longer needed.
 */
LLVMOrcResourceTrackerRef
LLVMOrcJITDylibGetDefaultResourceTracker(LLVMOrcJITDylibRef JD);

/**
 * Add the given MaterializationUnit to the given JITDylib.
 *
 * If this operation succeeds then JITDylib JD will take ownership of MU.
 * If the operation fails then ownership remains with the caller who should
 * call LLVMOrcDisposeMaterializationUnit to destroy it.
 */
LLVMErrorRef LLVMOrcJITDylibDefine(LLVMOrcJITDylibRef JD,
                                   LLVMOrcMaterializationUnitRef MU);

/**
 * Calls remove on all trackers associated with this JITDylib, see
 * JITDylib::clear().
 */
LLVMErrorRef LLVMOrcJITDylibClear(LLVMOrcJITDylibRef JD);

/**
 * Add a DefinitionGenerator to the given JITDylib.
 *
 * The JITDylib will take ownership of the given generator: The client is no
 * longer responsible for managing its memory.
 */
void LLVMOrcJITDylibAddGenerator(LLVMOrcJITDylibRef JD,
                                 LLVMOrcDefinitionGeneratorRef DG);

/**
 * Create a custom generator.
 *
 * The F argument will be used to implement the DefinitionGenerator's
 * tryToGenerate method (see
 * LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction).
 *
 * Ctx is a context object that will be passed to F. This argument is
 * permitted to be null.
 *
 * Dispose is the disposal function for Ctx. This argument is permitted to be
 * null (in which case the client is responsible for the lifetime of Ctx).
 */
LLVMOrcDefinitionGeneratorRef LLVMOrcCreateCustomCAPIDefinitionGenerator(
    LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction F, void *Ctx,
    LLVMOrcDisposeCAPIDefinitionGeneratorFunction Dispose);

/**
 * Continue a lookup that was suspended in a generator (see
 * LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction).
 */
void LLVMOrcLookupStateContinueLookup(LLVMOrcLookupStateRef S,
                                      LLVMErrorRef Err);

/**
 * Get a DynamicLibrarySearchGenerator that will reflect process symbols into
 * the JITDylib. On success the resulting generator is owned by the client.
 * Ownership is typically transferred by adding the instance to a JITDylib
 * using LLVMOrcJITDylibAddGenerator,
 *
 * The GlobalPrefix argument specifies the character that appears on the front
 * of linker-mangled symbols for the target platform (e.g. '_' on MachO).
 * If non-null, this character will be stripped from the start of all symbol
 * strings before passing the remaining substring to dlsym.
 *
 * The optional Filter and Ctx arguments can be used to supply a symbol name
 * filter: Only symbols for which the filter returns true will be visible to
 * JIT'd code. If the Filter argument is null then all process symbols will
 * be visible to JIT'd code. Note that the symbol name passed to the Filter
 * function is the full mangled symbol: The client is responsible for stripping
 * the global prefix if present.
 */
LLVMErrorRef LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(
    LLVMOrcDefinitionGeneratorRef *Result, char GlobalPrefx,
    LLVMOrcSymbolPredicate Filter, void *FilterCtx);

/**
 * Get a LLVMOrcCreateDynamicLibararySearchGeneratorForPath that will reflect
 * library symbols into the JITDylib. On success the resulting generator is
 * owned by the client. Ownership is typically transferred by adding the
 * instance to a JITDylib using LLVMOrcJITDylibAddGenerator,
 *
 * The GlobalPrefix argument specifies the character that appears on the front
 * of linker-mangled symbols for the target platform (e.g. '_' on MachO).
 * If non-null, this character will be stripped from the start of all symbol
 * strings before passing the remaining substring to dlsym.
 *
 * The optional Filter and Ctx arguments can be used to supply a symbol name
 * filter: Only symbols for which the filter returns true will be visible to
 * JIT'd code. If the Filter argument is null then all library symbols will
 * be visible to JIT'd code. Note that the symbol name passed to the Filter
 * function is the full mangled symbol: The client is responsible for stripping
 * the global prefix if present.
 * 
 * THIS API IS EXPERIMENTAL AND LIKELY TO CHANGE IN THE NEAR FUTURE!
 * 
 */
LLVMErrorRef LLVMOrcCreateDynamicLibrarySearchGeneratorForPath(
    LLVMOrcDefinitionGeneratorRef *Result, const char *FileName,
    char GlobalPrefix, LLVMOrcSymbolPredicate Filter, void *FilterCtx);

/**
 * Get a LLVMOrcCreateStaticLibrarySearchGeneratorForPath that will reflect
 * static library symbols into the JITDylib. On success the resulting
 * generator is owned by the client. Ownership is typically transferred by
 * adding the instance to a JITDylib using LLVMOrcJITDylibAddGenerator,
 *
 * Call with the optional TargetTriple argument will succeed if the file at
 * the given path is a static library or a MachO universal binary containing a
 * static library that is compatible with the given triple. Otherwise it will
 * return an error.
 *
 * THIS API IS EXPERIMENTAL AND LIKELY TO CHANGE IN THE NEAR FUTURE!
 * 
 */
LLVMErrorRef LLVMOrcCreateStaticLibrarySearchGeneratorForPath(
    LLVMOrcDefinitionGeneratorRef *Result, LLVMOrcObjectLayerRef ObjLayer,
    const char *FileName, const char *TargetTriple);

/**
 * Create a ThreadSafeContext containing a new LLVMContext.
 *
 * Ownership of the underlying ThreadSafeContext data is shared: Clients
 * can and should dispose of their ThreadSafeContext as soon as they no longer
 * need to refer to it directly. Other references (e.g. from ThreadSafeModules)
 * will keep the data alive as long as it is needed.
 */
LLVMOrcThreadSafeContextRef LLVMOrcCreateNewThreadSafeContext(void);

/**
 * Get a reference to the wrapped LLVMContext.
 */
LLVMContextRef
LLVMOrcThreadSafeContextGetContext(LLVMOrcThreadSafeContextRef TSCtx);

/**
 * Dispose of a ThreadSafeContext.
 */
void LLVMOrcDisposeThreadSafeContext(LLVMOrcThreadSafeContextRef TSCtx);

/**
 * Create a ThreadSafeModule wrapper around the given LLVM module. This takes
 * ownership of the M argument which should not be disposed of or referenced
 * after this function returns.
 *
 * Ownership of the ThreadSafeModule is unique: If it is transferred to the JIT
 * (e.g. by LLVMOrcLLJITAddLLVMIRModule) then the client is no longer
 * responsible for it. If it is not transferred to the JIT then the client
 * should call LLVMOrcDisposeThreadSafeModule to dispose of it.
 */
LLVMOrcThreadSafeModuleRef
LLVMOrcCreateNewThreadSafeModule(LLVMModuleRef M,
                                 LLVMOrcThreadSafeContextRef TSCtx);

/**
 * Dispose of a ThreadSafeModule. This should only be called if ownership has
 * not been passed to LLJIT (e.g. because some error prevented the client from
 * adding this to the JIT).
 */
void LLVMOrcDisposeThreadSafeModule(LLVMOrcThreadSafeModuleRef TSM);

/**
 * Apply the given function to the module contained in this ThreadSafeModule.
 */
LLVMErrorRef
LLVMOrcThreadSafeModuleWithModuleDo(LLVMOrcThreadSafeModuleRef TSM,
                                    LLVMOrcGenericIRModuleOperationFunction F,
                                    void *Ctx);

/**
 * Create a JITTargetMachineBuilder by detecting the host.
 *
 * On success the client owns the resulting JITTargetMachineBuilder. It must be
 * passed to a consuming operation (e.g.
 * LLVMOrcLLJITBuilderSetJITTargetMachineBuilder) or disposed of by calling
 * LLVMOrcDisposeJITTargetMachineBuilder.
 */
LLVMErrorRef LLVMOrcJITTargetMachineBuilderDetectHost(
    LLVMOrcJITTargetMachineBuilderRef *Result);

/**
 * Create a JITTargetMachineBuilder from the given TargetMachine template.
 *
 * This operation takes ownership of the given TargetMachine and destroys it
 * before returing. The resulting JITTargetMachineBuilder is owned by the client
 * and must be passed to a consuming operation (e.g.
 * LLVMOrcLLJITBuilderSetJITTargetMachineBuilder) or disposed of by calling
 * LLVMOrcDisposeJITTargetMachineBuilder.
 */
LLVMOrcJITTargetMachineBuilderRef
LLVMOrcJITTargetMachineBuilderCreateFromTargetMachine(LLVMTargetMachineRef TM);

/**
 * Dispose of a JITTargetMachineBuilder.
 */
void LLVMOrcDisposeJITTargetMachineBuilder(
    LLVMOrcJITTargetMachineBuilderRef JTMB);

/**
 * Returns the target triple for the given JITTargetMachineBuilder as a string.
 *
 * The caller owns the resulting string as must dispose of it by calling
 * LLVMDisposeMessage
 */
char *LLVMOrcJITTargetMachineBuilderGetTargetTriple(
    LLVMOrcJITTargetMachineBuilderRef JTMB);

/**
 * Sets the target triple for the given JITTargetMachineBuilder to the given
 * string.
 */
void LLVMOrcJITTargetMachineBuilderSetTargetTriple(
    LLVMOrcJITTargetMachineBuilderRef JTMB, const char *TargetTriple);

/**
 * Add an object to an ObjectLayer to the given JITDylib.
 *
 * Adds a buffer representing an object file to the given JITDylib using the
 * given ObjectLayer instance. This operation transfers ownership of the buffer
 * to the ObjectLayer instance. The buffer should not be disposed of or
 * referenced once this function returns.
 *
 * Resources associated with the given object will be tracked by the given
 * JITDylib's default ResourceTracker.
 */
LLVMErrorRef LLVMOrcObjectLayerAddObjectFile(LLVMOrcObjectLayerRef ObjLayer,
                                             LLVMOrcJITDylibRef JD,
                                             LLVMMemoryBufferRef ObjBuffer);

/**
 * Add an object to an ObjectLayer using the given ResourceTracker.
 *
 * Adds a buffer representing an object file to the given ResourceTracker's
 * JITDylib using the given ObjectLayer instance. This operation transfers
 * ownership of the buffer to the ObjectLayer instance. The buffer should not
 * be disposed of or referenced once this function returns.
 *
 * Resources associated with the given object will be tracked by
 * ResourceTracker RT.
 */
LLVMErrorRef
LLVMOrcObjectLayerAddObjectFileWithRT(LLVMOrcObjectLayerRef ObjLayer,
                                      LLVMOrcResourceTrackerRef RT,
                                      LLVMMemoryBufferRef ObjBuffer);

/**
 * Emit an object buffer to an ObjectLayer.
 *
 * Ownership of the responsibility object and object buffer pass to this
 * function. The client is not responsible for cleanup.
 */
void LLVMOrcObjectLayerEmit(LLVMOrcObjectLayerRef ObjLayer,
                            LLVMOrcMaterializationResponsibilityRef R,
                            LLVMMemoryBufferRef ObjBuffer);

/**
 * Dispose of an ObjectLayer.
 */
void LLVMOrcDisposeObjectLayer(LLVMOrcObjectLayerRef ObjLayer);

void LLVMOrcIRTransformLayerEmit(LLVMOrcIRTransformLayerRef IRTransformLayer,
                                 LLVMOrcMaterializationResponsibilityRef MR,
                                 LLVMOrcThreadSafeModuleRef TSM);

/**
 * Set the transform function of the provided transform layer, passing through a
 * pointer to user provided context.
 */
void LLVMOrcIRTransformLayerSetTransform(
    LLVMOrcIRTransformLayerRef IRTransformLayer,
    LLVMOrcIRTransformLayerTransformFunction TransformFunction, void *Ctx);

/**
 * Set the transform function on an LLVMOrcObjectTransformLayer.
 */
void LLVMOrcObjectTransformLayerSetTransform(
    LLVMOrcObjectTransformLayerRef ObjTransformLayer,
    LLVMOrcObjectTransformLayerTransformFunction TransformFunction, void *Ctx);

/**
 * Create a LocalIndirectStubsManager from the given target triple.
 *
 * The resulting IndirectStubsManager is owned by the client
 * and must be disposed of by calling LLVMOrcDisposeDisposeIndirectStubsManager.
 */
LLVMOrcIndirectStubsManagerRef
LLVMOrcCreateLocalIndirectStubsManager(const char *TargetTriple);

/**
 * Dispose of an IndirectStubsManager.
 */
void LLVMOrcDisposeIndirectStubsManager(LLVMOrcIndirectStubsManagerRef ISM);

LLVMErrorRef LLVMOrcCreateLocalLazyCallThroughManager(
    const char *TargetTriple, LLVMOrcExecutionSessionRef ES,
    LLVMOrcJITTargetAddress ErrorHandlerAddr,
    LLVMOrcLazyCallThroughManagerRef *LCTM);

/**
 * Dispose of an LazyCallThroughManager.
 */
void LLVMOrcDisposeLazyCallThroughManager(
    LLVMOrcLazyCallThroughManagerRef LCTM);

/**
 * Create a DumpObjects instance.
 *
 * DumpDir specifies the path to write dumped objects to. DumpDir may be empty
 * in which case files will be dumped to the working directory.
 *
 * IdentifierOverride specifies a file name stem to use when dumping objects.
 * If empty then each MemoryBuffer's identifier will be used (with a .o suffix
 * added if not already present). If an identifier override is supplied it will
 * be used instead, along with an incrementing counter (since all buffers will
 * use the same identifier, the resulting files will be named <ident>.o,
 * <ident>.2.o, <ident>.3.o, and so on). IdentifierOverride should not contain
 * an extension, as a .o suffix will be added by DumpObjects.
 */
LLVMOrcDumpObjectsRef LLVMOrcCreateDumpObjects(const char *DumpDir,
                                               const char *IdentifierOverride);

/**
 * Dispose of a DumpObjects instance.
 */
void LLVMOrcDisposeDumpObjects(LLVMOrcDumpObjectsRef DumpObjects);

/**
 * Dump the contents of the given MemoryBuffer.
 */
LLVMErrorRef LLVMOrcDumpObjects_CallOperator(LLVMOrcDumpObjectsRef DumpObjects,
                                             LLVMMemoryBufferRef *ObjBuffer);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif /* LLVM_C_ORC_H */
