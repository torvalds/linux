//===----- GDBRegistrationListener.cpp - Registers objects with GDB -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm-c/ExecutionEngine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include <mutex>

using namespace llvm;
using namespace llvm::object;

// This must be kept in sync with gdb/gdb/jit.h .
extern "C" {

  typedef enum {
    JIT_NOACTION = 0,
    JIT_REGISTER_FN,
    JIT_UNREGISTER_FN
  } jit_actions_t;

  struct jit_code_entry {
    struct jit_code_entry *next_entry;
    struct jit_code_entry *prev_entry;
    const char *symfile_addr;
    uint64_t symfile_size;
  };

  struct jit_descriptor {
    uint32_t version;
    // This should be jit_actions_t, but we want to be specific about the
    // bit-width.
    uint32_t action_flag;
    struct jit_code_entry *relevant_entry;
    struct jit_code_entry *first_entry;
  };

  // We put information about the JITed function in this global, which the
  // debugger reads.  Make sure to specify the version statically, because the
  // debugger checks the version before we can set it during runtime.
  extern struct jit_descriptor __jit_debug_descriptor;

  // Debuggers puts a breakpoint in this function.
  extern "C" void __jit_debug_register_code();
}

namespace {

// FIXME: lli aims to provide both, RuntimeDyld and JITLink, as the dynamic
// loaders for its JIT implementations. And they both offer debugging via the
// GDB JIT interface, which builds on the two well-known symbol names below.
// As these symbols must be unique across the linked executable, we can only
// define them in one of the libraries and make the other depend on it.
// OrcTargetProcess is a minimal stub for embedding a JIT client in remote
// executors. For the moment it seems reasonable to have the definition there
// and let ExecutionEngine depend on it, until we find a better solution.
//
LLVM_ATTRIBUTE_USED void requiredSymbolDefinitionsFromOrcTargetProcess() {
  errs() << (void *)&__jit_debug_register_code
         << (void *)&__jit_debug_descriptor;
}

struct RegisteredObjectInfo {
  RegisteredObjectInfo() = default;

  RegisteredObjectInfo(std::size_t Size, jit_code_entry *Entry,
                       OwningBinary<ObjectFile> Obj)
    : Size(Size), Entry(Entry), Obj(std::move(Obj)) {}

  std::size_t Size;
  jit_code_entry *Entry;
  OwningBinary<ObjectFile> Obj;
};

// Buffer for an in-memory object file in executable memory
typedef llvm::DenseMap<JITEventListener::ObjectKey, RegisteredObjectInfo>
    RegisteredObjectBufferMap;

/// Global access point for the JIT debugging interface designed for use with a
/// singleton toolbox. Handles thread-safe registration and deregistration of
/// object files that are in executable memory managed by the client of this
/// class.
class GDBJITRegistrationListener : public JITEventListener {
  /// Lock used to serialize all jit registration events, since they
  /// modify global variables.
  ///
  /// Only a single instance of GDBJITRegistrationListener is ever created,
  /// and so the lock can be a member variable of that instance. This ensures
  /// destructors are run in the correct order.
  sys::Mutex JITDebugLock;

  /// A map of in-memory object files that have been registered with the
  /// JIT interface.
  RegisteredObjectBufferMap ObjectBufferMap;

  /// Instantiates the JIT service.
  GDBJITRegistrationListener() = default;

  /// Unregisters each object that was previously registered and releases all
  /// internal resources.
  ~GDBJITRegistrationListener() override;

public:
  static GDBJITRegistrationListener &instance() {
    static GDBJITRegistrationListener Instance;
    return Instance;
  }

  /// Creates an entry in the JIT registry for the buffer @p Object,
  /// which must contain an object file in executable memory with any
  /// debug information for the debugger.
  void notifyObjectLoaded(ObjectKey K, const ObjectFile &Obj,
                          const RuntimeDyld::LoadedObjectInfo &L) override;

  /// Removes the internal registration of @p Object, and
  /// frees associated resources.
  /// Returns true if @p Object was found in ObjectBufferMap.
  void notifyFreeingObject(ObjectKey K) override;

private:
  /// Deregister the debug info for the given object file from the debugger
  /// and delete any temporary copies.  This private method does not remove
  /// the function from Map so that it can be called while iterating over Map.
  void deregisterObjectInternal(RegisteredObjectBufferMap::iterator I);
};

/// Do the registration.
void NotifyDebugger(jit_code_entry* JITCodeEntry) {
  __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;

  // Insert this entry at the head of the list.
  JITCodeEntry->prev_entry = nullptr;
  jit_code_entry* NextEntry = __jit_debug_descriptor.first_entry;
  JITCodeEntry->next_entry = NextEntry;
  if (NextEntry) {
    NextEntry->prev_entry = JITCodeEntry;
  }
  __jit_debug_descriptor.first_entry = JITCodeEntry;
  __jit_debug_descriptor.relevant_entry = JITCodeEntry;
  __jit_debug_register_code();
}

GDBJITRegistrationListener::~GDBJITRegistrationListener() {
  // Free all registered object files.
  std::lock_guard<llvm::sys::Mutex> locked(JITDebugLock);
  for (RegisteredObjectBufferMap::iterator I = ObjectBufferMap.begin(),
                                           E = ObjectBufferMap.end();
       I != E; ++I) {
    // Call the private method that doesn't update the map so our iterator
    // doesn't break.
    deregisterObjectInternal(I);
  }
  ObjectBufferMap.clear();
}

void GDBJITRegistrationListener::notifyObjectLoaded(
    ObjectKey K, const ObjectFile &Obj,
    const RuntimeDyld::LoadedObjectInfo &L) {

  OwningBinary<ObjectFile> DebugObj = L.getObjectForDebug(Obj);

  // Bail out if debug objects aren't supported.
  if (!DebugObj.getBinary())
    return;

  const char *Buffer = DebugObj.getBinary()->getMemoryBufferRef().getBufferStart();
  size_t      Size = DebugObj.getBinary()->getMemoryBufferRef().getBufferSize();

  std::lock_guard<llvm::sys::Mutex> locked(JITDebugLock);
  assert(!ObjectBufferMap.contains(K) &&
         "Second attempt to perform debug registration.");
  jit_code_entry* JITCodeEntry = new jit_code_entry();

  if (!JITCodeEntry) {
    llvm::report_fatal_error(
      "Allocation failed when registering a JIT entry!\n");
  } else {
    JITCodeEntry->symfile_addr = Buffer;
    JITCodeEntry->symfile_size = Size;

    ObjectBufferMap[K] =
        RegisteredObjectInfo(Size, JITCodeEntry, std::move(DebugObj));
    NotifyDebugger(JITCodeEntry);
  }
}

void GDBJITRegistrationListener::notifyFreeingObject(ObjectKey K) {
  std::lock_guard<llvm::sys::Mutex> locked(JITDebugLock);
  RegisteredObjectBufferMap::iterator I = ObjectBufferMap.find(K);

  if (I != ObjectBufferMap.end()) {
    deregisterObjectInternal(I);
    ObjectBufferMap.erase(I);
  }
}

void GDBJITRegistrationListener::deregisterObjectInternal(
    RegisteredObjectBufferMap::iterator I) {

  jit_code_entry*& JITCodeEntry = I->second.Entry;

  // Do the unregistration.
  {
    __jit_debug_descriptor.action_flag = JIT_UNREGISTER_FN;

    // Remove the jit_code_entry from the linked list.
    jit_code_entry* PrevEntry = JITCodeEntry->prev_entry;
    jit_code_entry* NextEntry = JITCodeEntry->next_entry;

    if (NextEntry) {
      NextEntry->prev_entry = PrevEntry;
    }
    if (PrevEntry) {
      PrevEntry->next_entry = NextEntry;
    }
    else {
      assert(__jit_debug_descriptor.first_entry == JITCodeEntry);
      __jit_debug_descriptor.first_entry = NextEntry;
    }

    // Tell the debugger which entry we removed, and unregister the code.
    __jit_debug_descriptor.relevant_entry = JITCodeEntry;
    __jit_debug_register_code();
  }

  delete JITCodeEntry;
  JITCodeEntry = nullptr;
}

} // end namespace

namespace llvm {

JITEventListener* JITEventListener::createGDBRegistrationListener() {
  return &GDBJITRegistrationListener::instance();
}

} // namespace llvm

LLVMJITEventListenerRef LLVMCreateGDBRegistrationListener(void)
{
  return wrap(JITEventListener::createGDBRegistrationListener());
}
