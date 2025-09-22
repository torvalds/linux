//===-- ModuleUtils.h - Functions to manipulate Modules ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on Modules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MODULEUTILS_H
#define LLVM_TRANSFORMS_UTILS_MODULEUTILS_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <utility> // for std::pair

namespace llvm {
template <typename T> class SmallVectorImpl;

template <typename T> class ArrayRef;
class Module;
class Function;
class FunctionCallee;
class GlobalIFunc;
class GlobalValue;
class Constant;
class Value;
class Type;

/// Append F to the list of global ctors of module M with the given Priority.
/// This wraps the function in the appropriate structure and stores it along
/// side other global constructors. For details see
/// https://llvm.org/docs/LangRef.html#the-llvm-global-ctors-global-variable
void appendToGlobalCtors(Module &M, Function *F, int Priority,
                         Constant *Data = nullptr);

/// Same as appendToGlobalCtors(), but for global dtors.
void appendToGlobalDtors(Module &M, Function *F, int Priority,
                         Constant *Data = nullptr);

/// Sets the KCFI type for the function. Used for compiler-generated functions
/// that are indirectly called in instrumented code.
void setKCFIType(Module &M, Function &F, StringRef MangledType);

FunctionCallee declareSanitizerInitFunction(Module &M, StringRef InitName,
                                            ArrayRef<Type *> InitArgTypes,
                                            bool Weak = false);

/// Creates sanitizer constructor function.
/// \return Returns pointer to constructor.
Function *createSanitizerCtor(Module &M, StringRef CtorName);

/// Creates sanitizer constructor function, and calls sanitizer's init
/// function from it.
/// \return Returns pair of pointers to constructor, and init functions
/// respectively.
std::pair<Function *, FunctionCallee> createSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    StringRef VersionCheckName = StringRef(), bool Weak = false);

/// Creates sanitizer constructor function lazily. If a constructor and init
/// function already exist, this function returns it. Otherwise it calls \c
/// createSanitizerCtorAndInitFunctions. The FunctionsCreatedCallback is invoked
/// in that case, passing the new Ctor and Init function.
///
/// \return Returns pair of pointers to constructor, and init functions
/// respectively.
std::pair<Function *, FunctionCallee> getOrCreateSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    function_ref<void(Function *, FunctionCallee)> FunctionsCreatedCallback,
    StringRef VersionCheckName = StringRef(), bool Weak = false);

/// Rename all the anon globals in the module using a hash computed from
/// the list of public globals in the module.
bool nameUnamedGlobals(Module &M);

/// Adds global values to the llvm.used list.
void appendToUsed(Module &M, ArrayRef<GlobalValue *> Values);

/// Adds global values to the llvm.compiler.used list.
void appendToCompilerUsed(Module &M, ArrayRef<GlobalValue *> Values);

/// Removes global values from the llvm.used and llvm.compiler.used arrays. \p
/// ShouldRemove should return true for any initializer field that should not be
/// included in the replacement global.
void removeFromUsedLists(Module &M,
                         function_ref<bool(Constant *)> ShouldRemove);

/// Filter out potentially dead comdat functions where other entries keep the
/// entire comdat group alive.
///
/// This is designed for cases where functions appear to become dead but remain
/// alive due to other live entries in their comdat group.
///
/// The \p DeadComdatFunctions container should only have pointers to
/// `Function`s which are members of a comdat group and are believed to be
/// dead.
///
/// After this routine finishes, the only remaining `Function`s in \p
/// DeadComdatFunctions are those where every member of the comdat is listed
/// and thus removing them is safe (provided *all* are removed).
void filterDeadComdatFunctions(
    SmallVectorImpl<Function *> &DeadComdatFunctions);

/// Produce a unique identifier for this module by taking the MD5 sum of
/// the names of the module's strong external symbols that are not comdat
/// members.
///
/// This identifier is normally guaranteed to be unique, or the program would
/// fail to link due to multiply defined symbols.
///
/// If the module has no strong external symbols (such a module may still have a
/// semantic effect if it performs global initialization), we cannot produce a
/// unique identifier for this module, so we return the empty string.
std::string getUniqueModuleId(Module *M);

/// Embed the memory buffer \p Buf into the module \p M as a global using the
/// specified section name. Also provide a metadata entry to identify it in the
/// module using the same section name.
void embedBufferInModule(Module &M, MemoryBufferRef Buf, StringRef SectionName,
                         Align Alignment = Align(1));

/// Lower all calls to ifuncs by replacing uses with indirect calls loaded out
/// of a global table initialized in a global constructor. This will introduce
/// one constructor function and adds it to llvm.global_ctors. The constructor
/// will call the resolver function once for each ifunc.
///
/// Leaves any unhandled constant initializer uses as-is.
///
/// If \p IFuncsToLower is empty, all ifuncs in the module will be lowered.
/// If \p IFuncsToLower is non-empty, only the selected ifuncs will be lowered.
///
/// The processed ifuncs without remaining users will be removed from the
/// module.
bool lowerGlobalIFuncUsersAsGlobalCtor(
    Module &M, ArrayRef<GlobalIFunc *> IFuncsToLower = {});

} // End llvm namespace

#endif // LLVM_TRANSFORMS_UTILS_MODULEUTILS_H
