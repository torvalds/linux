//===- ModuleManager.cpp - Module Manager ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ModuleManager class, which manages a set of loaded
//  modules for the ASTReader.
//
//===----------------------------------------------------------------------===//

#include "clang/Serialization/ModuleManager.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Serialization/GlobalModuleIndex.h"
#include "clang/Serialization/InMemoryModuleCache.h"
#include "clang/Serialization/ModuleFile.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <system_error>

using namespace clang;
using namespace serialization;

ModuleFile *ModuleManager::lookupByFileName(StringRef Name) const {
  auto Entry = FileMgr.getFile(Name, /*OpenFile=*/false,
                               /*CacheFailure=*/false);
  if (Entry)
    return lookup(*Entry);

  return nullptr;
}

ModuleFile *ModuleManager::lookupByModuleName(StringRef Name) const {
  if (const Module *Mod = HeaderSearchInfo.getModuleMap().findModule(Name))
    if (OptionalFileEntryRef File = Mod->getASTFile())
      return lookup(*File);

  return nullptr;
}

ModuleFile *ModuleManager::lookup(const FileEntry *File) const {
  return Modules.lookup(File);
}

std::unique_ptr<llvm::MemoryBuffer>
ModuleManager::lookupBuffer(StringRef Name) {
  auto Entry = FileMgr.getFile(Name, /*OpenFile=*/false,
                               /*CacheFailure=*/false);
  if (!Entry)
    return nullptr;
  return std::move(InMemoryBuffers[*Entry]);
}

static bool checkSignature(ASTFileSignature Signature,
                           ASTFileSignature ExpectedSignature,
                           std::string &ErrorStr) {
  if (!ExpectedSignature || Signature == ExpectedSignature)
    return false;

  ErrorStr =
      Signature ? "signature mismatch" : "could not read module signature";
  return true;
}

static void updateModuleImports(ModuleFile &MF, ModuleFile *ImportedBy,
                                SourceLocation ImportLoc) {
  if (ImportedBy) {
    MF.ImportedBy.insert(ImportedBy);
    ImportedBy->Imports.insert(&MF);
  } else {
    if (!MF.DirectlyImported)
      MF.ImportLoc = ImportLoc;

    MF.DirectlyImported = true;
  }
}

ModuleManager::AddModuleResult
ModuleManager::addModule(StringRef FileName, ModuleKind Type,
                         SourceLocation ImportLoc, ModuleFile *ImportedBy,
                         unsigned Generation,
                         off_t ExpectedSize, time_t ExpectedModTime,
                         ASTFileSignature ExpectedSignature,
                         ASTFileSignatureReader ReadSignature,
                         ModuleFile *&Module,
                         std::string &ErrorStr) {
  Module = nullptr;

  // Look for the file entry. This only fails if the expected size or
  // modification time differ.
  OptionalFileEntryRef Entry;
  if (Type == MK_ExplicitModule || Type == MK_PrebuiltModule) {
    // If we're not expecting to pull this file out of the module cache, it
    // might have a different mtime due to being moved across filesystems in
    // a distributed build. The size must still match, though. (As must the
    // contents, but we can't check that.)
    ExpectedModTime = 0;
  }
  // Note: ExpectedSize and ExpectedModTime will be 0 for MK_ImplicitModule
  // when using an ASTFileSignature.
  if (lookupModuleFile(FileName, ExpectedSize, ExpectedModTime, Entry)) {
    ErrorStr = "module file out of date";
    return OutOfDate;
  }

  if (!Entry) {
    ErrorStr = "module file not found";
    return Missing;
  }

  // The ModuleManager's use of FileEntry nodes as the keys for its map of
  // loaded modules is less than ideal. Uniqueness for FileEntry nodes is
  // maintained by FileManager, which in turn uses inode numbers on hosts
  // that support that. When coupled with the module cache's proclivity for
  // turning over and deleting stale PCMs, this means entries for different
  // module files can wind up reusing the same underlying inode. When this
  // happens, subsequent accesses to the Modules map will disagree on the
  // ModuleFile associated with a given file. In general, it is not sufficient
  // to resolve this conundrum with a type like FileEntryRef that stores the
  // name of the FileEntry node on first access because of path canonicalization
  // issues. However, the paths constructed for implicit module builds are
  // fully under Clang's control. We *can*, therefore, rely on their structure
  // being consistent across operating systems and across subsequent accesses
  // to the Modules map.
  auto implicitModuleNamesMatch = [](ModuleKind Kind, const ModuleFile *MF,
                                     FileEntryRef Entry) -> bool {
    if (Kind != MK_ImplicitModule)
      return true;
    return Entry.getName() == MF->FileName;
  };

  // Check whether we already loaded this module, before
  if (ModuleFile *ModuleEntry = Modules.lookup(*Entry)) {
    if (implicitModuleNamesMatch(Type, ModuleEntry, *Entry)) {
      // Check the stored signature.
      if (checkSignature(ModuleEntry->Signature, ExpectedSignature, ErrorStr))
        return OutOfDate;

      Module = ModuleEntry;
      updateModuleImports(*ModuleEntry, ImportedBy, ImportLoc);
      return AlreadyLoaded;
    }
  }

  // Allocate a new module.
  auto NewModule = std::make_unique<ModuleFile>(Type, *Entry, Generation);
  NewModule->Index = Chain.size();
  NewModule->FileName = FileName.str();
  NewModule->ImportLoc = ImportLoc;
  NewModule->InputFilesValidationTimestamp = 0;

  if (NewModule->Kind == MK_ImplicitModule) {
    std::string TimestampFilename = NewModule->getTimestampFilename();
    llvm::vfs::Status Status;
    // A cached stat value would be fine as well.
    if (!FileMgr.getNoncachedStatValue(TimestampFilename, Status))
      NewModule->InputFilesValidationTimestamp =
          llvm::sys::toTimeT(Status.getLastModificationTime());
  }

  // Load the contents of the module
  if (std::unique_ptr<llvm::MemoryBuffer> Buffer = lookupBuffer(FileName)) {
    // The buffer was already provided for us.
    NewModule->Buffer = &ModuleCache->addBuiltPCM(FileName, std::move(Buffer));
    // Since the cached buffer is reused, it is safe to close the file
    // descriptor that was opened while stat()ing the PCM in
    // lookupModuleFile() above, it won't be needed any longer.
    Entry->closeFile();
  } else if (llvm::MemoryBuffer *Buffer =
                 getModuleCache().lookupPCM(FileName)) {
    NewModule->Buffer = Buffer;
    // As above, the file descriptor is no longer needed.
    Entry->closeFile();
  } else if (getModuleCache().shouldBuildPCM(FileName)) {
    // Report that the module is out of date, since we tried (and failed) to
    // import it earlier.
    Entry->closeFile();
    return OutOfDate;
  } else {
    // Get a buffer of the file and close the file descriptor when done.
    // The file is volatile because in a parallel build we expect multiple
    // compiler processes to use the same module file rebuilding it if needed.
    //
    // RequiresNullTerminator is false because module files don't need it, and
    // this allows the file to still be mmapped.
    auto Buf = FileMgr.getBufferForFile(NewModule->File,
                                        /*IsVolatile=*/true,
                                        /*RequiresNullTerminator=*/false);

    if (!Buf) {
      ErrorStr = Buf.getError().message();
      return Missing;
    }

    NewModule->Buffer = &getModuleCache().addPCM(FileName, std::move(*Buf));
  }

  // Initialize the stream.
  NewModule->Data = PCHContainerRdr.ExtractPCH(*NewModule->Buffer);

  // Read the signature eagerly now so that we can check it.  Avoid calling
  // ReadSignature unless there's something to check though.
  if (ExpectedSignature && checkSignature(ReadSignature(NewModule->Data),
                                          ExpectedSignature, ErrorStr))
    return OutOfDate;

  // We're keeping this module.  Store it everywhere.
  Module = Modules[*Entry] = NewModule.get();

  updateModuleImports(*NewModule, ImportedBy, ImportLoc);

  if (!NewModule->isModule())
    PCHChain.push_back(NewModule.get());
  if (!ImportedBy)
    Roots.push_back(NewModule.get());

  Chain.push_back(std::move(NewModule));
  return NewlyLoaded;
}

void ModuleManager::removeModules(ModuleIterator First) {
  auto Last = end();
  if (First == Last)
    return;

  // Explicitly clear VisitOrder since we might not notice it is stale.
  VisitOrder.clear();

  // Collect the set of module file pointers that we'll be removing.
  llvm::SmallPtrSet<ModuleFile *, 4> victimSet(
      (llvm::pointer_iterator<ModuleIterator>(First)),
      (llvm::pointer_iterator<ModuleIterator>(Last)));

  auto IsVictim = [&](ModuleFile *MF) {
    return victimSet.count(MF);
  };
  // Remove any references to the now-destroyed modules.
  for (auto I = begin(); I != First; ++I) {
    I->Imports.remove_if(IsVictim);
    I->ImportedBy.remove_if(IsVictim);
  }
  llvm::erase_if(Roots, IsVictim);

  // Remove the modules from the PCH chain.
  for (auto I = First; I != Last; ++I) {
    if (!I->isModule()) {
      PCHChain.erase(llvm::find(PCHChain, &*I), PCHChain.end());
      break;
    }
  }

  // Delete the modules.
  for (ModuleIterator victim = First; victim != Last; ++victim)
    Modules.erase(victim->File);

  Chain.erase(Chain.begin() + (First - begin()), Chain.end());
}

void
ModuleManager::addInMemoryBuffer(StringRef FileName,
                                 std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  const FileEntry *Entry =
      FileMgr.getVirtualFile(FileName, Buffer->getBufferSize(), 0);
  InMemoryBuffers[Entry] = std::move(Buffer);
}

std::unique_ptr<ModuleManager::VisitState> ModuleManager::allocateVisitState() {
  // Fast path: if we have a cached state, use it.
  if (FirstVisitState) {
    auto Result = std::move(FirstVisitState);
    FirstVisitState = std::move(Result->NextState);
    return Result;
  }

  // Allocate and return a new state.
  return std::make_unique<VisitState>(size());
}

void ModuleManager::returnVisitState(std::unique_ptr<VisitState> State) {
  assert(State->NextState == nullptr && "Visited state is in list?");
  State->NextState = std::move(FirstVisitState);
  FirstVisitState = std::move(State);
}

void ModuleManager::setGlobalIndex(GlobalModuleIndex *Index) {
  GlobalIndex = Index;
  if (!GlobalIndex) {
    ModulesInCommonWithGlobalIndex.clear();
    return;
  }

  // Notify the global module index about all of the modules we've already
  // loaded.
  for (ModuleFile &M : *this)
    if (!GlobalIndex->loadedModuleFile(&M))
      ModulesInCommonWithGlobalIndex.push_back(&M);
}

void ModuleManager::moduleFileAccepted(ModuleFile *MF) {
  if (!GlobalIndex || GlobalIndex->loadedModuleFile(MF))
    return;

  ModulesInCommonWithGlobalIndex.push_back(MF);
}

ModuleManager::ModuleManager(FileManager &FileMgr,
                             InMemoryModuleCache &ModuleCache,
                             const PCHContainerReader &PCHContainerRdr,
                             const HeaderSearch &HeaderSearchInfo)
    : FileMgr(FileMgr), ModuleCache(&ModuleCache),
      PCHContainerRdr(PCHContainerRdr), HeaderSearchInfo(HeaderSearchInfo) {}

void ModuleManager::visit(llvm::function_ref<bool(ModuleFile &M)> Visitor,
                          llvm::SmallPtrSetImpl<ModuleFile *> *ModuleFilesHit) {
  // If the visitation order vector is the wrong size, recompute the order.
  if (VisitOrder.size() != Chain.size()) {
    unsigned N = size();
    VisitOrder.clear();
    VisitOrder.reserve(N);

    // Record the number of incoming edges for each module. When we
    // encounter a module with no incoming edges, push it into the queue
    // to seed the queue.
    SmallVector<ModuleFile *, 4> Queue;
    Queue.reserve(N);
    llvm::SmallVector<unsigned, 4> UnusedIncomingEdges;
    UnusedIncomingEdges.resize(size());
    for (ModuleFile &M : llvm::reverse(*this)) {
      unsigned Size = M.ImportedBy.size();
      UnusedIncomingEdges[M.Index] = Size;
      if (!Size)
        Queue.push_back(&M);
    }

    // Traverse the graph, making sure to visit a module before visiting any
    // of its dependencies.
    while (!Queue.empty()) {
      ModuleFile *CurrentModule = Queue.pop_back_val();
      VisitOrder.push_back(CurrentModule);

      // For any module that this module depends on, push it on the
      // stack (if it hasn't already been marked as visited).
      for (ModuleFile *M : llvm::reverse(CurrentModule->Imports)) {
        // Remove our current module as an impediment to visiting the
        // module we depend on. If we were the last unvisited module
        // that depends on this particular module, push it into the
        // queue to be visited.
        unsigned &NumUnusedEdges = UnusedIncomingEdges[M->Index];
        if (NumUnusedEdges && (--NumUnusedEdges == 0))
          Queue.push_back(M);
      }
    }

    assert(VisitOrder.size() == N && "Visitation order is wrong?");

    FirstVisitState = nullptr;
  }

  auto State = allocateVisitState();
  unsigned VisitNumber = State->NextVisitNumber++;

  // If the caller has provided us with a hit-set that came from the global
  // module index, mark every module file in common with the global module
  // index that is *not* in that set as 'visited'.
  if (ModuleFilesHit && !ModulesInCommonWithGlobalIndex.empty()) {
    for (unsigned I = 0, N = ModulesInCommonWithGlobalIndex.size(); I != N; ++I)
    {
      ModuleFile *M = ModulesInCommonWithGlobalIndex[I];
      if (!ModuleFilesHit->count(M))
        State->VisitNumber[M->Index] = VisitNumber;
    }
  }

  for (unsigned I = 0, N = VisitOrder.size(); I != N; ++I) {
    ModuleFile *CurrentModule = VisitOrder[I];
    // Should we skip this module file?
    if (State->VisitNumber[CurrentModule->Index] == VisitNumber)
      continue;

    // Visit the module.
    assert(State->VisitNumber[CurrentModule->Index] == VisitNumber - 1);
    State->VisitNumber[CurrentModule->Index] = VisitNumber;
    if (!Visitor(*CurrentModule))
      continue;

    // The visitor has requested that cut off visitation of any
    // module that the current module depends on. To indicate this
    // behavior, we mark all of the reachable modules as having been visited.
    ModuleFile *NextModule = CurrentModule;
    do {
      // For any module that this module depends on, push it on the
      // stack (if it hasn't already been marked as visited).
      for (llvm::SetVector<ModuleFile *>::iterator
             M = NextModule->Imports.begin(),
             MEnd = NextModule->Imports.end();
           M != MEnd; ++M) {
        if (State->VisitNumber[(*M)->Index] != VisitNumber) {
          State->Stack.push_back(*M);
          State->VisitNumber[(*M)->Index] = VisitNumber;
        }
      }

      if (State->Stack.empty())
        break;

      // Pop the next module off the stack.
      NextModule = State->Stack.pop_back_val();
    } while (true);
  }

  returnVisitState(std::move(State));
}

bool ModuleManager::lookupModuleFile(StringRef FileName, off_t ExpectedSize,
                                     time_t ExpectedModTime,
                                     OptionalFileEntryRef &File) {
  if (FileName == "-") {
    File = expectedToOptional(FileMgr.getSTDIN());
    return false;
  }

  // Open the file immediately to ensure there is no race between stat'ing and
  // opening the file.
  File = FileMgr.getOptionalFileRef(FileName, /*OpenFile=*/true,
                                    /*CacheFailure=*/false);

  if (File &&
      ((ExpectedSize && ExpectedSize != File->getSize()) ||
       (ExpectedModTime && ExpectedModTime != File->getModificationTime())))
    // Do not destroy File, as it may be referenced. If we need to rebuild it,
    // it will be destroyed by removeModules.
    return true;

  return false;
}

#ifndef NDEBUG
namespace llvm {

  template<>
  struct GraphTraits<ModuleManager> {
    using NodeRef = ModuleFile *;
    using ChildIteratorType = llvm::SetVector<ModuleFile *>::const_iterator;
    using nodes_iterator = pointer_iterator<ModuleManager::ModuleConstIterator>;

    static ChildIteratorType child_begin(NodeRef Node) {
      return Node->Imports.begin();
    }

    static ChildIteratorType child_end(NodeRef Node) {
      return Node->Imports.end();
    }

    static nodes_iterator nodes_begin(const ModuleManager &Manager) {
      return nodes_iterator(Manager.begin());
    }

    static nodes_iterator nodes_end(const ModuleManager &Manager) {
      return nodes_iterator(Manager.end());
    }
  };

  template<>
  struct DOTGraphTraits<ModuleManager> : public DefaultDOTGraphTraits {
    explicit DOTGraphTraits(bool IsSimple = false)
        : DefaultDOTGraphTraits(IsSimple) {}

    static bool renderGraphFromBottomUp() { return true; }

    std::string getNodeLabel(ModuleFile *M, const ModuleManager&) {
      return M->ModuleName;
    }
  };

} // namespace llvm

void ModuleManager::viewGraph() {
  llvm::ViewGraph(*this, "Modules");
}
#endif
