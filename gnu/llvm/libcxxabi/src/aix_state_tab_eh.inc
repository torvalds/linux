//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  This file implements the personality and helper functions for the state
//  table based EH used by IBM legacy compilers xlC and xlclang++ on AIX.
//
//===----------------------------------------------------------------------===//

#include <new>
#include <stdio.h>
#include <sys/debug.h>

/*
  The legacy IBM xlC and xlclang++ compilers use the state table for EH
  instead of the range table. Destructors, or addresses of the possible catch
  sites or cleanup code are specified in the state table which is a finite
  state machine (FSM). Each function that has a state table also has an
  autolocal state variable. The state variable represents the current state
  of the function for EH and is found through the traceback table of the
  function during unwinding, which is located at the end of each function.
  The FSM is an array of state entries. Each state entry has the following
  fields:

  * offset/address/pointer - the offset used to locate the object, or the
    address of a global object, or the address of the next state if it is an
    old conditional state change entry;
  * dtor/landing pad - address of the destructor function to invoke,
    or address of the catch block or cleanup code in the user code to branch to;
  * element count/action flag - the number of elements or the flag for actions;
  * element size - if the object is an array this is the size of one element
    of the array;
  * flags - flags used to control how fields in the entry are interpreted;
  * next state - the state to execute next after the action for this state is
    performed. The value of zero indicates the end of the state for this
    function.

  The following is the description of 'element count/action flag' field.
+-----------------------------------------------------------------------------+
| value |      description       |                  action                    |
+-------+------------------------+--------------------------------------------+
| > 1   |   object is an array   | calls __cxa_vec_cleanup to run dtor for    |
|       |                        | each member of the array                   |
+-------+------------------------+--------------------------------------------+
| 1, 0  |   object is a scalar   | calls dtor for the object                  |
+-------+------------------------+--------------------------------------------+
|  -1   |      begin catch       | branches to the handler which performes    |
|       |                        | catch-match. If there is no catch that     |
|       |                        | matches the exception it will be rethrown  |
+-------+------------------------+--------------------------------------------+
|  -2   |       end catch        | ends current catch block and continues     |
|       |                        | attempting to catch the exception          |
+-------+------------------------+--------------------------------------------+
|  -3   |   delete the object    | calls the delete function of the object    |
+-------+------------------------+--------------------------------------------+
|  -4   |      cleanup label     | branches to the user code for cleaning up  |
+-------+------------------------+--------------------------------------------+
*/

namespace __cxxabiv1 {

extern "C" {

// Macros for debugging the state table parsing.
#ifdef NDEBUG
#  define _LIBCXXABI_TRACE_STATETAB(msg, ...)
#  define _LIBCXXABI_TRACE_STATETAB0(msg)
#  define _LIBCXXABI_TRACE_STATETAB1(msg)
#  define _LIBCXXABI_TRACING_STATETAB 0
#else
static bool state_tab_dbg() {
  static bool checked = false;
  static bool log = false;
  if (!checked) {
    log = (getenv("LIBCXXABI_PRINT_STATTAB") != NULL);
    checked = true;
  }
  return log;
}

#  define _LIBCXXABI_TRACE_STATETAB(msg, ...)                                  \
     do {                                                                      \
       if (state_tab_dbg())                                                    \
         fprintf(stderr, "libcxxabi: " msg, __VA_ARGS__);                      \
     } while (0)
#  define _LIBCXXABI_TRACE_STATETAB0(msg)                                      \
     do {                                                                      \
       if (state_tab_dbg())                                                    \
         fprintf(stderr, "libcxxabi: " msg);                                   \
     } while (0)
#  define _LIBCXXABI_TRACE_STATETAB1(msg)                                      \
     do {                                                                      \
       if (state_tab_dbg())                                                    \
         fprintf(stderr, msg);                                                 \
     } while (0)

#  define _LIBCXXABI_TRACING_STATETAB state_tab_dbg()
#endif // NDEBUG

namespace __state_table_eh {

// Definition of flags for the state table entry field 'action flag'.
enum FSMEntryCount : intptr_t { beginCatch = -1, endCatch = -2, deleteObject = -3, cleanupLabel = -4, terminate = -5 };

// Definition of flags for the state table entry field 'flags'.
enum FSMEntryFlag : int16_t {
  indirect = 0x100,                  // Object was thrown from a function where
                                     // the return value optimization was used.
  oldConditionalStateChange = 0x400, // State table entry is an indirect state
                                     // change, dereference the address in
                                     // offset as int for the target state.
                                     // This is deprecated. This indicates
                                     // the address is direct. (static local).
  conditionalStateChange = 0x800,    // State table entry is an indirect state
                                     // change, dereference the address in
                                     // offset as int for the target state.
                                     // The temporary is an automatic. State
                                     // change is used in cases such as
                                     // (b?(T1(),foo()):(T2(),foo())),throw 42;
                                     // which causes a conditional state change
                                     // so that we know if T1 or T2 need to be
                                     // destroyed.
  thisFlag = 0x01,                   // The address of the object for the
                                     // cleanup action is based on the
                                     // StateVariable::thisValue.
  vBaseFlag = 0x02,                  // The object is of a virtual base class.
  globalObj = 0x04                   // FSMEntry::address is the address of
                                     // a global object.
};

namespace {
// The finite state machine to be walked.
struct FSMEntry {
  union {
    // Offset of the object within its stack frame or containing object.
    intptr_t offset;
    // Address of a global object.
    intptr_t address;
    // Address of the next state if it is an old conditional state change entry.
    intptr_t nextStatePtr;
  };
  union {
    // Address of the destructor function with 1 argument.
    void (*destructor)(void*);
    // Address of the destructor function with 2 arguments.
    void (*xlCDestructor)(void*, size_t);
    // The address of the catch block or cleanup code.
    void* landingPad;
  };
  union {
    // The flag for actions (when the value is negative).
    FSMEntryCount actionFlag;
    // The element count (when the value is positive or zero).
    size_t elementCount;
  };
  size_t elemSize;
  FSMEntryFlag flags;
  uint16_t nextState;
};

struct FSM {
  uint32_t magic; // Magic number of the state table.
  int32_t numberOfStates;
  FSMEntry table[1]; // Actually table[numberOfStates].
};

// The state variable on the stack.
struct StateVariable {
  int32_t state;
  struct FSM* table;
  intptr_t thisValue;
  int32_t ignoreVBasePtrs;
};
} // namespace

// State table magic number
enum FSMMagic : uint32_t {
  number = 0xbeefdead,  // State table generated by xlC compiler.
  number2 = 0xbeeedead, // State table generated by early version xlC compiler.
  number3 = 0x1cedbeef  // State table generated by xlclang++ compiler.
};

constexpr size_t dtorArgument = 0x02; // Flag to destructor indicating to free
                                      // virtual bases, don't delete object.

static void invoke_destructor(FSMEntry* fsmEntry, void* addr) {
  _LIBCXXABI_TRACE_STATETAB("Destruct object=%p, fsmEntry=%p\n", addr, reinterpret_cast<void*>(fsmEntry));
  try {
    if (fsmEntry->elementCount == 1) {
      _LIBCXXABI_TRACE_STATETAB0("calling scalar destructor\n");
      (*fsmEntry->xlCDestructor)(addr, dtorArgument);
      _LIBCXXABI_TRACE_STATETAB0("returned from scalar destructor\n");
    } else {
      _LIBCXXABI_TRACE_STATETAB0("calling vector destructor\n");
      __cxa_vec_cleanup(addr, reinterpret_cast<size_t>(fsmEntry->elementCount), fsmEntry->elemSize,
                        fsmEntry->destructor);
      _LIBCXXABI_TRACE_STATETAB0("returned from vector destructor\n");
    }
  } catch (...) {
    _LIBCXXABI_TRACE_STATETAB0("Uncaught exception in destructor, terminating\n");
    std::terminate();
  }
}

static void invoke_delete(FSMEntry* fsmEntry, void* addr) {
  char* objectAddress = *reinterpret_cast<char**>(addr);

  _LIBCXXABI_TRACE_STATETAB("Delete object=%p, fsmEntry=%p\n", reinterpret_cast<void*>(objectAddress),
                            reinterpret_cast<void*>(fsmEntry));
  try {
    _LIBCXXABI_TRACE_STATETAB0("..calling delete()\n");
    // 'destructor' holds a function pointer to delete().
    (*fsmEntry->xlCDestructor)(objectAddress, fsmEntry->elemSize);
    _LIBCXXABI_TRACE_STATETAB0("..returned from delete()\n");
  } catch (...) {
    _LIBCXXABI_TRACE_STATETAB0("Uncaught exception in delete(), terminating\n");
    std::terminate();
  }
}

// Get the frame address of the current function from its traceback table
// which is at the end of each function.
static uintptr_t get_frame_addr(_Unwind_Context* context) {
  int framePointerReg = 1; // default frame pointer == SP.
  uint32_t* p = reinterpret_cast<uint32_t*>(_Unwind_GetIP(context));

  // Keep looking forward until a word of 0 is found. The traceback
  // table starts at the following word.
  while (*p)
    ++p;
  tbtable* TBTable = reinterpret_cast<tbtable*>(p + 1);

  p = reinterpret_cast<uint32_t*>(&TBTable->tb_ext);

  // Skip field parminfo if it exists.
  if (TBTable->tb.fixedparms || TBTable->tb.floatparms)
    ++p;

  // Skip field tb_offset if it exists.
  if (TBTable->tb.has_tboff)
    ++p;

  // Skip field hand_mask if it exists.
  if (TBTable->tb.int_hndl)
    ++p;

  // Skip fields ctl_info and ctl_info_disp if they exist.
  if (TBTable->tb.has_ctl)
    p += 1 + *p;

  // Skip fields name_len and name if exist.
  if (TBTable->tb.name_present) {
    const uint16_t name_len = *reinterpret_cast<uint16_t*>(p);
    p = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(p) + name_len + sizeof(uint16_t));
  }

  if (TBTable->tb.uses_alloca)
    framePointerReg = *reinterpret_cast<char*>(p);

  return _Unwind_GetGR(context, framePointerReg);
}

// Calculate the object address from the FSM entry.
static void* compute_addr_from_table(FSMEntry* fsmEntry, StateVariable* const state, _Unwind_Context* context) {
  void* addr;
  if (fsmEntry->flags & FSMEntryFlag::globalObj) {
    addr = reinterpret_cast<void*>(fsmEntry->address);
    _LIBCXXABI_TRACE_STATETAB("Address calculation (global obj) addr=fsmEntry->address=%p\n", addr);
  } else if (fsmEntry->flags & FSMEntryFlag::thisFlag) {
    addr = reinterpret_cast<void*>(state->thisValue + fsmEntry->offset);
    _LIBCXXABI_TRACE_STATETAB("Address calculation (this obj) fsmEntry->offset=%ld : "
                              "state->thisValue=%ld addr=(fsmEntry->offset+state->thisValue)=%p\n",
                              fsmEntry->offset, state->thisValue, addr);
  } else if (fsmEntry->flags & FSMEntryFlag::indirect) {
    addr = reinterpret_cast<void*>(
        *reinterpret_cast<char**>(get_frame_addr(context) + static_cast<uintptr_t>(fsmEntry->offset)));
    _LIBCXXABI_TRACE_STATETAB("Address calculation (indirect obj) addr=%p, fsmEntry->offset=%ld \n",
                              addr, fsmEntry->offset);
  } else {
    addr = reinterpret_cast<void*>(get_frame_addr(context) + static_cast<uintptr_t>(fsmEntry->offset));
    _LIBCXXABI_TRACE_STATETAB("Address calculation. (local obj) addr=fsmEntry->offset=%p\n",
                              addr);
  }
  return addr;
}

static void scan_state_tab(scan_results& results, _Unwind_Action actions, bool native_exception,
                           _Unwind_Exception* unwind_exception, _Unwind_Context* context) {
  // Initialize results to found nothing but an error.
  results.ttypeIndex = 0;
  results.actionRecord = 0;
  results.languageSpecificData = 0;
  results.landingPad = 0;
  results.adjustedPtr = 0;
  results.reason = _URC_FATAL_PHASE1_ERROR;

  // Check for consistent actions.
  if (actions & _UA_SEARCH_PHASE) {
    // Do Phase 1
    if (actions & (_UA_CLEANUP_PHASE | _UA_HANDLER_FRAME | _UA_FORCE_UNWIND)) {
      // None of these flags should be set during Phase 1.
      //   Client error
      results.reason = _URC_FATAL_PHASE1_ERROR;
      return;
    }
  } else if (actions & _UA_CLEANUP_PHASE) {
    if ((actions & _UA_HANDLER_FRAME) && (actions & _UA_FORCE_UNWIND)) {
      // _UA_HANDLER_FRAME should only be set if phase 1 found a handler.
      // If _UA_FORCE_UNWIND is set, phase 1 shouldn't have happened.
      //    Client error
      results.reason = _URC_FATAL_PHASE2_ERROR;
      return;
    }
  } else {
    // Neither _UA_SEARCH_PHASE nor _UA_CLEANUP_PHASE is set.
    //   Client error
    results.reason = _URC_FATAL_PHASE1_ERROR;
    return;
  }

  if (_LIBCXXABI_TRACING_STATETAB) {
    _LIBCXXABI_TRACE_STATETAB1("\n");
    _LIBCXXABI_TRACE_STATETAB("%s: actions=%d (", __func__, actions);

    if (_UA_SEARCH_PHASE & actions)
      _LIBCXXABI_TRACE_STATETAB1("_UA_SEARCH_PHASE ");
    if (_UA_CLEANUP_PHASE & actions)
      _LIBCXXABI_TRACE_STATETAB1("_UA_CLEANUP_PHASE ");
    if (_UA_HANDLER_FRAME & actions)
      _LIBCXXABI_TRACE_STATETAB1("_UA_HANDLER_FRAME ");
    if (_UA_FORCE_UNWIND & actions)
      _LIBCXXABI_TRACE_STATETAB1("_UA_FORCE_UNWIND ");
    _LIBCXXABI_TRACE_STATETAB1(")\n");
    _LIBCXXABI_TRACE_STATETAB("       unwind_exception=%p context=%p\n", reinterpret_cast<void*>(unwind_exception),
                              reinterpret_cast<void*>(context));
  }

  // Start scan by getting state table address.
  StateVariable* const state = reinterpret_cast<StateVariable* const>(_Unwind_GetLanguageSpecificData(context));
  if (state->state <= 0) {
    // The state is not correct - give up on this routine.
    _LIBCXXABI_TRACE_STATETAB("state=%d and is <= 0), continue unwinding\n", state->state);
    results.reason = _URC_CONTINUE_UNWIND;
    return;
  }
  // Parse the state table.
  FSM* const fsm = state->table;
  FSMEntry* currFSMEntry;

  if (fsm->magic != FSMMagic::number && fsm->magic != FSMMagic::number2 && fsm->magic != FSMMagic::number3) {
    // Something is wrong with the state table we found.
    if (_UA_SEARCH_PHASE & actions) {
      _LIBCXXABI_TRACE_STATETAB0("Invalid FSM table, return _URC_FATAL_PHASE1_ERROR\n");
      results.reason = _URC_FATAL_PHASE1_ERROR;
    } else if (_UA_CLEANUP_PHASE & actions) {
      _LIBCXXABI_TRACE_STATETAB0("Invalid FSM table, return _URC_FATAL_PHASE2_ERROR\n");
      results.reason = _URC_FATAL_PHASE2_ERROR;
    } else {
      // We should never get here.
      _LIBCXXABI_TRACE_STATETAB0("Invalid FSM table + RT Internal error, return _URC_FATAL_PHASE2_ERROR\n");
      results.reason = _URC_FATAL_PHASE2_ERROR;
    }
    return;
  }

  if (_LIBCXXABI_TRACING_STATETAB) {
    // Print the state table for debugging purposes.
    _LIBCXXABI_TRACE_STATETAB("state->state=%d, state->ignoreVBasePtrs=%d\n", state->state, state->ignoreVBasePtrs);
    _LIBCXXABI_TRACE_STATETAB("fsm->magic=%#x, fsm->numberOfStates=%d\n", fsm->magic, fsm->numberOfStates);
    // Print out the FSM table.
    _LIBCXXABI_TRACE_STATETAB0("FSM table:\n");
    _LIBCXXABI_TRACE_STATETAB("%12s %10s %8s  %10s %7s %7s %7s %7s\n", "Entry Addr", "state", "Offset", "DTR/lpad",
                              "count", "el_size", "flags", "next");
    for (int i = 0; i < fsm->numberOfStates; i++) {
      currFSMEntry = &fsm->table[i];
      _LIBCXXABI_TRACE_STATETAB("%12p (%8d) %8ld  %10p %7ld "
                                "%7ld %#7x %7d\n",
                                reinterpret_cast<void*>(&currFSMEntry), i + 1, currFSMEntry->offset,
                                reinterpret_cast<void*>(currFSMEntry->destructor),
                                currFSMEntry->elementCount, currFSMEntry->elemSize, currFSMEntry->flags,
                                currFSMEntry->nextState);
    }
  }

  if (_UA_SEARCH_PHASE & actions) {
    // Start walking the state table. Use a local copy of state->state so when
    // we return from search phase we don't change the state number.
    int currState = state->state;

    while (currState > 0) {
      currFSMEntry = &fsm->table[currState - 1];
      _LIBCXXABI_TRACE_STATETAB("Processing state=%d, flags=0x%hx\n", currState, currFSMEntry->flags);

      if (currFSMEntry->actionFlag == FSMEntryCount::beginCatch) {
        // Found a catch handler.
        if (fsm->magic == FSMMagic::number) {
          _LIBCXXABI_TRACE_STATETAB0("Found a xlC catch handler, return _URC_FATAL_PHASE1_ERROR\n");
          // xlC catch handlers cannot be entered because they use a
          // proprietary EH runtime that is not interoperable.
          results.reason = _URC_FATAL_PHASE1_ERROR;
          return;
        }
        // xlclang++ compiled frames use CXA-abi EH calls and any catch
        // block will include a catch(...) block so it is safe to assume that
        // the handler is found without checking the catch match. The
        // catch(...) block will rethrow the exception if there isn't a
        // match.
        _LIBCXXABI_TRACE_STATETAB0("Found a catch handler, return _URC_HANDLER_FOUND\n");
        results.reason = _URC_HANDLER_FOUND;
        return;
      }
      if (currFSMEntry->actionFlag == FSMEntryCount::terminate) {
        _LIBCXXABI_TRACE_STATETAB0("Found the terminate state, return _URC_HANDLER_FOUND\n");
        results.reason = _URC_HANDLER_FOUND;
        return;
      }
      if (currFSMEntry->flags & FSMEntryFlag::oldConditionalStateChange) {
        // Deprecated conditional expression.
        currState = *reinterpret_cast<int*>(currFSMEntry->nextStatePtr);
        _LIBCXXABI_TRACE_STATETAB("Flag: FSMEntryFlag::oldConditionalStateChange, dereference "
                                  "currFSMEntry->nextStatePtr(%ld), set state=%d\n",
                                  currFSMEntry->nextStatePtr, currState);
        continue; // We are done this iteration of the loop, since
                  // we changed a state.
      }
      if (currFSMEntry->flags & FSMEntryFlag::conditionalStateChange) {
        void* addr = compute_addr_from_table(currFSMEntry, state, context);
        currState = *reinterpret_cast<int*>(addr);
        _LIBCXXABI_TRACE_STATETAB("Flag: FSMEntryFlag::conditionalStateChange, dereference "
                                  "addr(%p), set state=%d\n", addr, currState);
        continue; // We are done this iteration of the loop, since we
                  // changed the state.
      }
      // Go to the next state.
      currState = currFSMEntry->nextState;
    }
    _LIBCXXABI_TRACE_STATETAB0("No catch handler found, return _URC_CONTINUE_UNWIND\n");
    results.reason = _URC_CONTINUE_UNWIND;
    return;
  }
  if (_UA_CLEANUP_PHASE & actions) {
    // Start walking the state table.
    while (state->state > 0) {
      currFSMEntry = &fsm->table[state->state - 1];

      if (currFSMEntry->actionFlag == FSMEntryCount::terminate) {
        _LIBCXXABI_TRACE_STATETAB0("Reached terminate state. Call terminate.\n");
        std::terminate();
      }
      // Perform action according to the currFSMEntry->actionFlag,
      // except when flag is FSMEntryFlag::conditionalStateChange or
      // FSMEntryFlag::oldConditionalStateChange.
      _LIBCXXABI_TRACE_STATETAB("Processing state=%d, flags=0x%hx\n", state->state, currFSMEntry->flags);
      if (currFSMEntry->flags & FSMEntryFlag::oldConditionalStateChange) {
        state->state = *reinterpret_cast<int*>(currFSMEntry->nextStatePtr);
        _LIBCXXABI_TRACE_STATETAB("Flag: FSMEntryFlag::oldConditionalStateChange, dereference "
                                  "currFSMEntry->nextStatePtr(%ld), set state=%d\n",
                                  currFSMEntry->nextStatePtr, state->state);
        continue; // We are done with this iteration of the loop, since we changed a state.
      }
      if (currFSMEntry->flags & FSMEntryFlag::conditionalStateChange) {
        // A conditional state table entry holds the address of a local
        // that holds the next state.
        void* addr = compute_addr_from_table(currFSMEntry, state, context);
        state->state = *reinterpret_cast<int*>(addr);
        _LIBCXXABI_TRACE_STATETAB("Flag: FSMEntryFlag::conditionalStateChange, dereference "
                                  "addr(%p), set state=%d\n", addr, state->state);
        continue; // We are done with this iteration of the loop, since we changed a state.
      }
      if (currFSMEntry->actionFlag == FSMEntryCount::beginCatch || currFSMEntry->actionFlag == FSMEntryCount::endCatch ||
          currFSMEntry->actionFlag == FSMEntryCount::cleanupLabel) {

        _LIBCXXABI_TRACE_STATETAB(
            "FSMEntryCount::%s: handler %p/%p, return _URC_HANDLER_FOUND\n",
            (currFSMEntry->actionFlag == FSMEntryCount::beginCatch
                 ? "beginCatch"
                 : (currFSMEntry->actionFlag == FSMEntryCount::endCatch ? "endCatch" : "cleanupLabel")),
            currFSMEntry->landingPad, *reinterpret_cast<void**>(currFSMEntry->landingPad));

        state->state = currFSMEntry->nextState;
        results.landingPad = reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(currFSMEntry->landingPad));
        results.reason = _URC_HANDLER_FOUND;
        return;
      }
      if (currFSMEntry->elementCount > 0) {
        if (currFSMEntry->flags & FSMEntryFlag::vBaseFlag && state->ignoreVBasePtrs) {
          _LIBCXXABI_TRACE_STATETAB0("Ignoring virtual base dtor.\n");
        } else {
          // We need to invoke the virtual base destructor. This must be
          // a frame from the legacy xlC compiler as the xlclang++ compiler
          // generates inline cleanup code rather than specifying
          // the destructor via the state table.
          void* addr = compute_addr_from_table(currFSMEntry, state, context);

          // An extra indirect to get to the object according to the object
          // model used by the xlC compiler.
          addr = reinterpret_cast<void*>(*reinterpret_cast<char**>(addr));
          _LIBCXXABI_TRACE_STATETAB("Invoke dtor for object=%p\n", addr);
          invoke_destructor(currFSMEntry, addr);
        }
      } else if (currFSMEntry->actionFlag == FSMEntryCount::deleteObject) {
        void* addr = compute_addr_from_table(currFSMEntry, state, context);
        if (currFSMEntry->flags & FSMEntryFlag::vBaseFlag) {
          // We need to invoke the virtual base delete function. This must be
          // a frame from the legacy xlC compiler as the xlclang++ compiler
          // generates inline cleanup code rather than specifying
          // the delete function via the state table.

          // An extra indirect to get to the object according to the object
          // model used by the xlC compiler.
          addr = reinterpret_cast<void*>(*reinterpret_cast<char**>(addr));
        }
        _LIBCXXABI_TRACE_STATETAB("Delete object at %p\n", addr);
        invoke_delete(currFSMEntry, addr);
      } else {
        _LIBCXXABI_TRACE_STATETAB("Unknown entry in FSM (count=%ld), ignored\n",
                                  currFSMEntry->elementCount);
      } // End of action switching.

      // Go to next state.
      state->state = currFSMEntry->nextState;
    }
    _LIBCXXABI_TRACE_STATETAB0("No catch handler, return _URC_CONTINUE_UNWIND\n");
    results.reason = _URC_CONTINUE_UNWIND;
    return;
  }
  _LIBCXXABI_TRACE_STATETAB0("No state table entry for this exception, call_terminate()\n");
  // It is possible that no state table entry specify how to handle
  // this exception. By spec, terminate it immediately.
  call_terminate(native_exception, unwind_exception);
}

// Personality routine for EH using the state table.
_LIBCXXABI_FUNC_VIS _Unwind_Reason_Code
__xlcxx_personality_v0(int version, _Unwind_Action actions, uint64_t exceptionClass,
                       _Unwind_Exception* unwind_exception, _Unwind_Context* context) {
  if (version != 1 || unwind_exception == 0 || context == 0)
    return _URC_FATAL_PHASE1_ERROR;

  bool native_exception = (exceptionClass & get_vendor_and_language) == (kOurExceptionClass & get_vendor_and_language);
  scan_results results;
  scan_state_tab(results, actions, native_exception, unwind_exception, context);
  if (actions & _UA_SEARCH_PHASE) {
    // Phase 1 search:  All we're looking for in phase 1 is a handler that
    //   halts unwinding
    return results.reason;
  }
  if (actions & _UA_CLEANUP_PHASE) {
    // Phase 2 cleanup:
    if (results.reason == _URC_HANDLER_FOUND) {
      // Store the address of unwind_exception in the stack field
      // reserved for compilers (SP + 3 * sizeof(uintptr_t)) in the stack of
      // the caller of the function containing the landing pad (within the link
      // area for the call to the latter) for __xlc_exception_handle()
      // to retrieve when it is called by the landing pad.
      uintptr_t *currentSP = reinterpret_cast<uintptr_t*>(_Unwind_GetGR(context, 1));
      uintptr_t *callersSP = reinterpret_cast<uintptr_t*>(currentSP[0]);
      callersSP[3] = reinterpret_cast<uintptr_t>(unwind_exception);
      _LIBCXXABI_TRACE_STATETAB("Handshake: save unwind_exception=%p in stack=%p\n",
                                reinterpret_cast<void*>(unwind_exception), reinterpret_cast<void*>(callersSP));
      // Jump to the handler.
      _Unwind_SetIP(context, results.landingPad);
      return _URC_INSTALL_CONTEXT;
    }
    // Did not find a handler. Return the results of the scan. Normally
    // _URC_CONTINUE_UNWIND, but could have been _URC_FATAL_PHASE2_ERROR.
    return results.reason;
  }
  // We were called improperly: neither a phase 1 or phase 2 search.
  return _URC_FATAL_PHASE1_ERROR;
}
} // namespace __state_table_eh

// The following are EH helper functions for xlclang++ compiled code.

// __xlc_catch_matchv2
// Check whether the thrown object matches the catch handler's exception
// declaration. If there is a match, the function returns true with adjusted
// address of the thrown object. Otherwise, returns false.
_LIBCXXABI_FUNC_VIS bool
__xlc_catch_matchv2(_Unwind_Exception* exceptionObject, std::type_info* catchTypeInfo, void*& obj) {
  _LIBCXXABI_TRACE_STATETAB("Entering %s, exceptionObject=%p\n", __func__, reinterpret_cast<void*>(exceptionObject));

  if (!__isOurExceptionClass(exceptionObject)) {
    _LIBCXXABI_TRACE_STATETAB0("No match, not a C++ exception\n");
    return false;
  }

  __cxa_exception* exceptionHeader = 0;

  if (__getExceptionClass(exceptionObject) == kOurDependentExceptionClass) {
    // Walk to the __cxa_dependent_exception primary exception for the
    // exception object and its type_info.
    __cxa_dependent_exception* dependentExceptionHeader =
        reinterpret_cast<__cxa_dependent_exception*>(exceptionObject + 1) - 1;
    exceptionHeader = reinterpret_cast<__cxa_exception*>(dependentExceptionHeader->primaryException) - 1;
    _LIBCXXABI_TRACE_STATETAB("exceptionObject 0x%p is a dependent, primary 0x%p\n",
                              reinterpret_cast<void*>(exceptionObject),
                              reinterpret_cast<void*>(&exceptionHeader->unwindHeader));
    exceptionObject = &exceptionHeader->unwindHeader;
  } else {
    _LIBCXXABI_TRACE_STATETAB("exceptionObject %p is NOT a dependent\n", reinterpret_cast<void*>(exceptionObject));
    exceptionHeader = reinterpret_cast<__cxa_exception*>(exceptionObject + 1) - 1;
  }

  void* thrownObject = reinterpret_cast<void*>(exceptionObject + 1);
  std::type_info* throwTypeInfo = exceptionHeader->exceptionType;

  // Get the type info for the thrown type and this catch clause and
  // see if the catch caluse can catch that type.

  __cxxabiv1::__shim_type_info* catchType = reinterpret_cast<__cxxabiv1::__shim_type_info*>(catchTypeInfo);
  __cxxabiv1::__shim_type_info* throwType = reinterpret_cast<__cxxabiv1::__shim_type_info*>(throwTypeInfo);
  _LIBCXXABI_TRACE_STATETAB("UnwindException=%p, thrownObject=%p, throwTypeInfo=%p(%s), catchTypeInfo=%p(%s)\n",
                            reinterpret_cast<void*>(exceptionObject), thrownObject, reinterpret_cast<void*>(throwType),
                            throwType->name(), reinterpret_cast<void*>(catchType), catchType->name());
  if (catchType->can_catch(throwType, thrownObject)) {
    exceptionHeader->adjustedPtr = thrownObject;
    obj = thrownObject;
    _LIBCXXABI_TRACE_STATETAB("Match found for thrownObject=%p\n", thrownObject);
    return true;
  }
  _LIBCXXABI_TRACE_STATETAB0("No match\n");
  return false;
}

// __xlc_throw_badexception
// This function is for xlclang++. It allocates and throws a bad_exception.
// During unwinding for this bad_exception, the previous exception which is
// not matching the throw spec will be cleaned up. Thus having the same
// effect as replace the top most exception (which is bad) with a bad_exception.
_LIBCXXABI_FUNC_VIS void __xlc_throw_badexception() {
  _LIBCXXABI_TRACE_STATETAB("Entering function: %s\n\n", __func__);
  void* newexception = new (__cxa_allocate_exception(sizeof(std::bad_exception))) std::bad_exception;
  __cxa_throw(newexception, const_cast<std::type_info*>(&typeid(std::bad_exception)), 0);
}

// skip_non_cxx_eh_aware_frames
// This function skips non-C++ EH aware stack frames by unwinding from the
// stack frame pointed by 'Sp' and returns the first C++ EH aware stack frame
// found. 'Pc' is an instruction address inside the function that owns the
// stack frame pointed to by 'Sp'.
static uintptr_t* skip_non_cxx_eh_aware_frames(uint32_t* Pc, uintptr_t* Sp) {
  uint32_t* currentPc = Pc;
  uintptr_t* currentStack = Sp;

  // Loop until a C++ EH aware frame is found or the return address is 0,
  // which is the return address of the startup function '__start'.
  while (currentPc != 0) {
    uint32_t* p = currentPc;

    // Keep looking forward until a word of 0 is found. The traceback
    // table starts at the following word.
    while (*p)
      ++p;
    tbtable* TBTable = reinterpret_cast<tbtable*>(p + 1);

    // A stack frame with a C++ state table is C++ EH aware.
    if (TBTable->tb.lang == TB_CPLUSPLUS && TBTable->tb.has_ctl)
      return currentStack;

    // Move up one stack frame.
    currentStack = reinterpret_cast<uintptr_t*>(currentStack[0]);
    // Get the value of the LR (saved, prior to incrementing the SP, by the
    // prolog of the function just inspected) from the frame.
    currentPc = reinterpret_cast<uint32_t*>(currentStack[2]);
  }
  // This should not happen.
  _LIBCXXABI_TRACE_STATETAB0("skip_non_cxx_eh_aware_frames() reached the end of stack frames, aborting\n");
  abort();
}

// __xlc_exception_handle
// This function is for xlclang++. It returns the address of the exception
// object stored in the reserved field in the stack of the caller of the
// function that calls __xlc_exception_handle() (within the link area for the
// call to the latter). The address is stored by the personality routine for
// xlclang++ compiled code. If __xlc_exception_handle() is called by
// non-C++ EH aware functions, their frames are skipped until a C++ EH aware
// frame is found.
// Note: make sure __xlc_exception_handle() is a non-leaf function. Currently
// it calls skip_non_cxx_eh_aware_frames(), which in turn calls abort().
_LIBCXXABI_FUNC_VIS uintptr_t __xlc_exception_handle() {
  // Get the SP of this function, i.e., __xlc_exception_handle().
  uintptr_t* lastStack = reinterpret_cast<uintptr_t*>(__builtin_frame_address(0));
  // Move one frame up to the frame of the caller of __xlc_exception_handle().
  lastStack = reinterpret_cast<uintptr_t*>(lastStack[0]);
  // Get the return address of this function, i.e., __xlc_exception_handle().
  uint32_t* returnAddress = reinterpret_cast<uint32_t*>(__builtin_return_address(0));

  // Skip non-C++ EH aware frames and get the first C++ EH aware frame.
  uintptr_t* callerStack = skip_non_cxx_eh_aware_frames(returnAddress, lastStack);

  // Get the SP of the caller of the C++ EH aware caller.
  callerStack = reinterpret_cast<uintptr_t*>(callerStack[0]);
  // Retrieve the exception object in the stack slot saved by the personality.
  uintptr_t exceptionObject = callerStack[3];
  _LIBCXXABI_TRACE_STATETAB("Handshake: retrieve exceptionObject=%p from stack=%p\n",
                            reinterpret_cast<void*>(exceptionObject), reinterpret_cast<void*>(callerStack));
  return exceptionObject;
}

// xlclang++ may generate calls to __Deleted_Virtual.
_LIBCXXABI_FUNC_VIS void __Deleted_Virtual() { abort(); }

// __catchThrownException is called during AIX library initialization and
// termination to handle exceptions.  An implementation is also provided in
// libC.a(shrcore.o).  This implementation is provided for applications that
// link with -lc++ (the xlclang++ or ibm-clang++ link default.)
_LIBCXXABI_FUNC_VIS int
__catchThrownException(void (*cdfunc)(void),   // function which may fail
                       void (*cleanup)(void*), // cleanup function
                       void* cleanuparg,       // parameter to cleanup function
                       int action) {           // control exception throwing and termination
  enum Action : int { None = 0, Rethrow = 1, Terminate = 2 };
  if (!cdfunc)
    return 0;
  if (action == Action::Rethrow && !cleanup) {
    // No cleanup and rethrow is effectively no-op.
    // Avoid the catch handler when possible to allow exceptions generated
    // from xlC binaries to flow through.
    (*cdfunc)();
    return 0;
  }
  try {
    (*cdfunc)();
  } catch (...) {
    if (action == Action::Terminate)
      std::terminate();
    if (cleanup)
      (*cleanup)(cleanuparg);
    if (action == Action::Rethrow)
      throw;
    assert(action == Action::None);
    return -1; // FAILED
  }
  return 0;
}

} // extern "C"

}  // __cxxabiv1
