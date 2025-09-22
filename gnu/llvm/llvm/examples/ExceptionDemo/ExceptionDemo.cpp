//===-- ExceptionDemo.cpp - An example using llvm Exceptions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Demo program which implements an example LLVM exception implementation, and
// shows several test cases including the handling of foreign exceptions.
// It is run with type info types arguments to throw. A test will
// be run for each given type info type. While type info types with the value
// of -1 will trigger a foreign C++ exception to be thrown; type info types
// <= 6 and >= 1 will cause the associated generated exceptions to be thrown
// and caught by generated test functions; and type info types > 6
// will result in exceptions which pass through to the test harness. All other
// type info types are not supported and could cause a crash. In all cases,
// the "finally" blocks of every generated test functions will executed
// regardless of whether or not that test function ignores or catches the
// thrown exception.
//
// examples:
//
// ExceptionDemo
//
//     causes a usage to be printed to stderr
//
// ExceptionDemo 2 3 7 -1
//
//     results in the following cases:
//         - Value 2 causes an exception with a type info type of 2 to be
//           thrown and caught by an inner generated test function.
//         - Value 3 causes an exception with a type info type of 3 to be
//           thrown and caught by an outer generated test function.
//         - Value 7 causes an exception with a type info type of 7 to be
//           thrown and NOT be caught by any generated function.
//         - Value -1 causes a foreign C++ exception to be thrown and not be
//           caught by any generated function
//
//     Cases -1 and 7 are caught by a C++ test harness where the validity of
//         of a C++ catch(...) clause catching a generated exception with a
//         type info type of 7 is explained by: example in rules 1.6.4 in
//         http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html (v1.22)
//
// This code uses code from the llvm compiler-rt project and the llvm
// Kaleidoscope project.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Scalar.h"

// FIXME: Although all systems tested with (Linux, OS X), do not need this
//        header file included. A user on ubuntu reported, undefined symbols
//        for stderr, and fprintf, and the addition of this include fixed the
//        issue for them. Given that LLVM's best practices include the goal
//        of reducing the number of redundant header files included, the
//        correct solution would be to find out why these symbols are not
//        defined for the system in question, and fix the issue by finding out
//        which LLVM header file, if any, would include these symbols.
#include <cstdio>

#include <sstream>
#include <stdexcept>

#include <inttypes.h>

#include <unwind.h>

#ifndef USE_GLOBAL_STR_CONSTS
#define USE_GLOBAL_STR_CONSTS true
#endif

//
// Example types
//

/// This is our simplistic type info
struct OurExceptionType_t {
  /// type info type
  int type;
};


/// This is our Exception class which relies on a negative offset to calculate
/// pointers to its instances from pointers to its unwindException member.
///
/// Note: The above unwind.h defines struct _Unwind_Exception to be aligned
///       on a double word boundary. This is necessary to match the standard:
///       http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
struct OurBaseException_t {
  struct OurExceptionType_t type;

  // Note: This is properly aligned in unwind.h
  struct _Unwind_Exception unwindException;
};


// Note: Not needed since we are C++
typedef struct OurBaseException_t OurException;
typedef struct _Unwind_Exception OurUnwindException;

//
// Various globals used to support typeinfo and generatted exceptions in
// general
//

static std::map<std::string, llvm::Value*> namedValues;

int64_t ourBaseFromUnwindOffset;

const unsigned char ourBaseExcpClassChars[] =
{'o', 'b', 'j', '\0', 'b', 'a', 's', '\0'};


static uint64_t ourBaseExceptionClass = 0;

static std::vector<std::string> ourTypeInfoNames;
static std::map<int, std::string> ourTypeInfoNamesIndex;

static llvm::StructType *ourTypeInfoType;
static llvm::StructType *ourCaughtResultType;
static llvm::StructType *ourExceptionType;
static llvm::StructType *ourUnwindExceptionType;

static llvm::ConstantInt *ourExceptionNotThrownState;
static llvm::ConstantInt *ourExceptionThrownState;
static llvm::ConstantInt *ourExceptionCaughtState;

typedef std::vector<std::string> ArgNames;
typedef std::vector<llvm::Type*> ArgTypes;

//
// Code Generation Utilities
//

/// Utility used to create a function, both declarations and definitions
/// @param module for module instance
/// @param retType function return type
/// @param theArgTypes function's ordered argument types
/// @param theArgNames function's ordered arguments needed if use of this
///        function corresponds to a function definition. Use empty
///        aggregate for function declarations.
/// @param functName function name
/// @param linkage function linkage
/// @param declarationOnly for function declarations
/// @param isVarArg function uses vararg arguments
/// @returns function instance
llvm::Function *createFunction(llvm::Module &module,
                               llvm::Type *retType,
                               const ArgTypes &theArgTypes,
                               const ArgNames &theArgNames,
                               const std::string &functName,
                               llvm::GlobalValue::LinkageTypes linkage,
                               bool declarationOnly,
                               bool isVarArg) {
  llvm::FunctionType *functType =
    llvm::FunctionType::get(retType, theArgTypes, isVarArg);
  llvm::Function *ret =
    llvm::Function::Create(functType, linkage, functName, &module);
  if (!ret || declarationOnly)
    return(ret);

  namedValues.clear();
  unsigned i = 0;
  for (llvm::Function::arg_iterator argIndex = ret->arg_begin();
       i != theArgNames.size();
       ++argIndex, ++i) {

    argIndex->setName(theArgNames[i]);
    namedValues[theArgNames[i]] = argIndex;
  }

  return(ret);
}


/// Create an alloca instruction in the entry block of
/// the parent function.  This is used for mutable variables etc.
/// @param function parent instance
/// @param varName stack variable name
/// @param type stack variable type
/// @param initWith optional constant initialization value
/// @returns AllocaInst instance
static llvm::AllocaInst *createEntryBlockAlloca(llvm::Function &function,
                                                const std::string &varName,
                                                llvm::Type *type,
                                                llvm::Constant *initWith = 0) {
  llvm::BasicBlock &block = function.getEntryBlock();
  llvm::IRBuilder<> tmp(&block, block.begin());
  llvm::AllocaInst *ret = tmp.CreateAlloca(type, 0, varName);

  if (initWith)
    tmp.CreateStore(initWith, ret);

  return(ret);
}


//
// Code Generation Utilities End
//

//
// Runtime C Library functions
//

namespace {
template <typename Type_>
uintptr_t ReadType(const uint8_t *&p) {
  Type_ value;
  memcpy(&value, p, sizeof(Type_));
  p += sizeof(Type_);
  return static_cast<uintptr_t>(value);
}
}

// Note: using an extern "C" block so that static functions can be used
extern "C" {

// Note: Better ways to decide on bit width
//
/// Prints a 32 bit number, according to the format, to stderr.
/// @param intToPrint integer to print
/// @param format printf like format to use when printing
void print32Int(int intToPrint, const char *format) {
  if (format) {
    // Note: No NULL check
    fprintf(stderr, format, intToPrint);
  }
  else {
    // Note: No NULL check
    fprintf(stderr, "::print32Int(...):NULL arg.\n");
  }
}


// Note: Better ways to decide on bit width
//
/// Prints a 64 bit number, according to the format, to stderr.
/// @param intToPrint integer to print
/// @param format printf like format to use when printing
void print64Int(long int intToPrint, const char *format) {
  if (format) {
    // Note: No NULL check
    fprintf(stderr, format, intToPrint);
  }
  else {
    // Note: No NULL check
    fprintf(stderr, "::print64Int(...):NULL arg.\n");
  }
}


/// Prints a C string to stderr
/// @param toPrint string to print
void printStr(char *toPrint) {
  if (toPrint) {
    fprintf(stderr, "%s", toPrint);
  }
  else {
    fprintf(stderr, "::printStr(...):NULL arg.\n");
  }
}


/// Deletes the true previously allocated exception whose address
/// is calculated from the supplied OurBaseException_t::unwindException
/// member address. Handles (ignores), NULL pointers.
/// @param expToDelete exception to delete
void deleteOurException(OurUnwindException *expToDelete) {
#ifdef DEBUG
  fprintf(stderr,
          "deleteOurException(...).\n");
#endif

  if (expToDelete &&
      (expToDelete->exception_class == ourBaseExceptionClass)) {

    free(((char*) expToDelete) + ourBaseFromUnwindOffset);
  }
}


/// This function is the struct _Unwind_Exception API mandated delete function
/// used by foreign exception handlers when deleting our exception
/// (OurException), instances.
/// @param reason See @link http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
/// @unlink
/// @param expToDelete exception instance to delete
void deleteFromUnwindOurException(_Unwind_Reason_Code reason,
                                  OurUnwindException *expToDelete) {
#ifdef DEBUG
  fprintf(stderr,
          "deleteFromUnwindOurException(...).\n");
#endif

  deleteOurException(expToDelete);
}


/// Creates (allocates on the heap), an exception (OurException instance),
/// of the supplied type info type.
/// @param type type info type
OurUnwindException *createOurException(int type) {
  size_t size = sizeof(OurException);
  OurException *ret = (OurException*) memset(malloc(size), 0, size);
  (ret->type).type = type;
  (ret->unwindException).exception_class = ourBaseExceptionClass;
  (ret->unwindException).exception_cleanup = deleteFromUnwindOurException;

  return(&(ret->unwindException));
}


/// Read a uleb128 encoded value and advance pointer
/// See Variable Length Data in:
/// @link http://dwarfstd.org/Dwarf3.pdf @unlink
/// @param data reference variable holding memory pointer to decode from
/// @returns decoded value
static uintptr_t readULEB128(const uint8_t **data) {
  uintptr_t result = 0;
  uintptr_t shift = 0;
  unsigned char byte;
  const uint8_t *p = *data;

  do {
    byte = *p++;
    result |= (byte & 0x7f) << shift;
    shift += 7;
  }
  while (byte & 0x80);

  *data = p;

  return result;
}


/// Read a sleb128 encoded value and advance pointer
/// See Variable Length Data in:
/// @link http://dwarfstd.org/Dwarf3.pdf @unlink
/// @param data reference variable holding memory pointer to decode from
/// @returns decoded value
static uintptr_t readSLEB128(const uint8_t **data) {
  uintptr_t result = 0;
  uintptr_t shift = 0;
  unsigned char byte;
  const uint8_t *p = *data;

  do {
    byte = *p++;
    result |= (byte & 0x7f) << shift;
    shift += 7;
  }
  while (byte & 0x80);

  *data = p;

  if ((byte & 0x40) && (shift < (sizeof(result) << 3))) {
    result |= (~0 << shift);
  }

  return result;
}

unsigned getEncodingSize(uint8_t Encoding) {
  if (Encoding == llvm::dwarf::DW_EH_PE_omit)
    return 0;

  switch (Encoding & 0x0F) {
  case llvm::dwarf::DW_EH_PE_absptr:
    return sizeof(uintptr_t);
  case llvm::dwarf::DW_EH_PE_udata2:
    return sizeof(uint16_t);
  case llvm::dwarf::DW_EH_PE_udata4:
    return sizeof(uint32_t);
  case llvm::dwarf::DW_EH_PE_udata8:
    return sizeof(uint64_t);
  case llvm::dwarf::DW_EH_PE_sdata2:
    return sizeof(int16_t);
  case llvm::dwarf::DW_EH_PE_sdata4:
    return sizeof(int32_t);
  case llvm::dwarf::DW_EH_PE_sdata8:
    return sizeof(int64_t);
  default:
    // not supported
    abort();
  }
}

/// Read a pointer encoded value and advance pointer
/// See Variable Length Data in:
/// @link http://dwarfstd.org/Dwarf3.pdf @unlink
/// @param data reference variable holding memory pointer to decode from
/// @param encoding dwarf encoding type
/// @returns decoded value
static uintptr_t readEncodedPointer(const uint8_t **data, uint8_t encoding) {
  uintptr_t result = 0;
  const uint8_t *p = *data;

  if (encoding == llvm::dwarf::DW_EH_PE_omit)
    return(result);

  // first get value
  switch (encoding & 0x0F) {
    case llvm::dwarf::DW_EH_PE_absptr:
      result = ReadType<uintptr_t>(p);
      break;
    case llvm::dwarf::DW_EH_PE_uleb128:
      result = readULEB128(&p);
      break;
      // Note: This case has not been tested
    case llvm::dwarf::DW_EH_PE_sleb128:
      result = readSLEB128(&p);
      break;
    case llvm::dwarf::DW_EH_PE_udata2:
      result = ReadType<uint16_t>(p);
      break;
    case llvm::dwarf::DW_EH_PE_udata4:
      result = ReadType<uint32_t>(p);
      break;
    case llvm::dwarf::DW_EH_PE_udata8:
      result = ReadType<uint64_t>(p);
      break;
    case llvm::dwarf::DW_EH_PE_sdata2:
      result = ReadType<int16_t>(p);
      break;
    case llvm::dwarf::DW_EH_PE_sdata4:
      result = ReadType<int32_t>(p);
      break;
    case llvm::dwarf::DW_EH_PE_sdata8:
      result = ReadType<int64_t>(p);
      break;
    default:
      // not supported
      abort();
      break;
  }

  // then add relative offset
  switch (encoding & 0x70) {
    case llvm::dwarf::DW_EH_PE_absptr:
      // do nothing
      break;
    case llvm::dwarf::DW_EH_PE_pcrel:
      result += (uintptr_t)(*data);
      break;
    case llvm::dwarf::DW_EH_PE_textrel:
    case llvm::dwarf::DW_EH_PE_datarel:
    case llvm::dwarf::DW_EH_PE_funcrel:
    case llvm::dwarf::DW_EH_PE_aligned:
    default:
      // not supported
      abort();
      break;
  }

  // then apply indirection
  if (encoding & llvm::dwarf::DW_EH_PE_indirect) {
    result = *((uintptr_t*)result);
  }

  *data = p;

  return result;
}


/// Deals with Dwarf actions matching our type infos
/// (OurExceptionType_t instances). Returns whether or not a dwarf emitted
/// action matches the supplied exception type. If such a match succeeds,
/// the resultAction argument will be set with > 0 index value. Only
/// corresponding llvm.eh.selector type info arguments, cleanup arguments
/// are supported. Filters are not supported.
/// See Variable Length Data in:
/// @link http://dwarfstd.org/Dwarf3.pdf @unlink
/// Also see @link http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html @unlink
/// @param resultAction reference variable which will be set with result
/// @param classInfo our array of type info pointers (to globals)
/// @param actionEntry index into above type info array or 0 (clean up).
///        We do not support filters.
/// @param exceptionClass exception class (_Unwind_Exception::exception_class)
///        of thrown exception.
/// @param exceptionObject thrown _Unwind_Exception instance.
/// @returns whether or not a type info was found. False is returned if only
///          a cleanup was found
static bool handleActionValue(int64_t *resultAction,
                              uint8_t TTypeEncoding,
                              const uint8_t *ClassInfo,
                              uintptr_t actionEntry,
                              uint64_t exceptionClass,
                              struct _Unwind_Exception *exceptionObject) {
  bool ret = false;

  if (!resultAction ||
      !exceptionObject ||
      (exceptionClass != ourBaseExceptionClass))
    return(ret);

  struct OurBaseException_t *excp = (struct OurBaseException_t*)
  (((char*) exceptionObject) + ourBaseFromUnwindOffset);
  struct OurExceptionType_t *excpType = &(excp->type);
  int type = excpType->type;

#ifdef DEBUG
  fprintf(stderr,
          "handleActionValue(...): exceptionObject = <%p>, "
          "excp = <%p>.\n",
          (void*)exceptionObject,
          (void*)excp);
#endif

  const uint8_t *actionPos = (uint8_t*) actionEntry,
  *tempActionPos;
  int64_t typeOffset = 0,
  actionOffset;

  for (int i = 0; true; ++i) {
    // Each emitted dwarf action corresponds to a 2 tuple of
    // type info address offset, and action offset to the next
    // emitted action.
    typeOffset = readSLEB128(&actionPos);
    tempActionPos = actionPos;
    actionOffset = readSLEB128(&tempActionPos);

#ifdef DEBUG
    fprintf(stderr,
            "handleActionValue(...):typeOffset: <%" PRIi64 ">, "
            "actionOffset: <%" PRIi64 ">.\n",
            typeOffset,
            actionOffset);
#endif
    assert((typeOffset >= 0) &&
           "handleActionValue(...):filters are not supported.");

    // Note: A typeOffset == 0 implies that a cleanup llvm.eh.selector
    //       argument has been matched.
    if (typeOffset > 0) {
#ifdef DEBUG
      fprintf(stderr,
              "handleActionValue(...):actionValue <%d> found.\n",
              i);
#endif
      unsigned EncSize = getEncodingSize(TTypeEncoding);
      const uint8_t *EntryP = ClassInfo - typeOffset * EncSize;
      uintptr_t P = readEncodedPointer(&EntryP, TTypeEncoding);
      struct OurExceptionType_t *ThisClassInfo =
        reinterpret_cast<struct OurExceptionType_t *>(P);
      if (ThisClassInfo->type == type) {
        *resultAction = i + 1;
        ret = true;
        break;
      }
    }

#ifdef DEBUG
    fprintf(stderr,
            "handleActionValue(...):actionValue not found.\n");
#endif
    if (!actionOffset)
      break;

    actionPos += actionOffset;
  }

  return(ret);
}


/// Deals with the Language specific data portion of the emitted dwarf code.
/// See @link http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html @unlink
/// @param version unsupported (ignored), unwind version
/// @param lsda language specific data area
/// @param _Unwind_Action actions minimally supported unwind stage
///        (forced specifically not supported)
/// @param exceptionClass exception class (_Unwind_Exception::exception_class)
///        of thrown exception.
/// @param exceptionObject thrown _Unwind_Exception instance.
/// @param context unwind system context
/// @returns minimally supported unwinding control indicator
static _Unwind_Reason_Code handleLsda(int version, const uint8_t *lsda,
                                      _Unwind_Action actions,
                                      _Unwind_Exception_Class exceptionClass,
                                      struct _Unwind_Exception *exceptionObject,
                                      struct _Unwind_Context *context) {
  _Unwind_Reason_Code ret = _URC_CONTINUE_UNWIND;

  if (!lsda)
    return(ret);

#ifdef DEBUG
  fprintf(stderr,
          "handleLsda(...):lsda is non-zero.\n");
#endif

  // Get the current instruction pointer and offset it before next
  // instruction in the current frame which threw the exception.
  uintptr_t pc = _Unwind_GetIP(context)-1;

  // Get beginning current frame's code (as defined by the
  // emitted dwarf code)
  uintptr_t funcStart = _Unwind_GetRegionStart(context);
  uintptr_t pcOffset = pc - funcStart;
  const uint8_t *ClassInfo = NULL;

  // Note: See JITDwarfEmitter::EmitExceptionTable(...) for corresponding
  //       dwarf emission

  // Parse LSDA header.
  uint8_t lpStartEncoding = *lsda++;

  if (lpStartEncoding != llvm::dwarf::DW_EH_PE_omit) {
    readEncodedPointer(&lsda, lpStartEncoding);
  }

  uint8_t ttypeEncoding = *lsda++;
  uintptr_t classInfoOffset;

  if (ttypeEncoding != llvm::dwarf::DW_EH_PE_omit) {
    // Calculate type info locations in emitted dwarf code which
    // were flagged by type info arguments to llvm.eh.selector
    // intrinsic
    classInfoOffset = readULEB128(&lsda);
    ClassInfo = lsda + classInfoOffset;
  }

  // Walk call-site table looking for range that
  // includes current PC.

  uint8_t         callSiteEncoding = *lsda++;
  uint32_t        callSiteTableLength = readULEB128(&lsda);
  const uint8_t   *callSiteTableStart = lsda;
  const uint8_t   *callSiteTableEnd = callSiteTableStart +
  callSiteTableLength;
  const uint8_t   *actionTableStart = callSiteTableEnd;
  const uint8_t   *callSitePtr = callSiteTableStart;

  while (callSitePtr < callSiteTableEnd) {
    uintptr_t start = readEncodedPointer(&callSitePtr,
                                         callSiteEncoding);
    uintptr_t length = readEncodedPointer(&callSitePtr,
                                          callSiteEncoding);
    uintptr_t landingPad = readEncodedPointer(&callSitePtr,
                                              callSiteEncoding);

    // Note: Action value
    uintptr_t actionEntry = readULEB128(&callSitePtr);

    if (exceptionClass != ourBaseExceptionClass) {
      // We have been notified of a foreign exception being thrown,
      // and we therefore need to execute cleanup landing pads
      actionEntry = 0;
    }

    if (landingPad == 0) {
#ifdef DEBUG
      fprintf(stderr,
              "handleLsda(...): No landing pad found.\n");
#endif

      continue; // no landing pad for this entry
    }

    if (actionEntry) {
      actionEntry += ((uintptr_t) actionTableStart) - 1;
    }
    else {
#ifdef DEBUG
      fprintf(stderr,
              "handleLsda(...):No action table found.\n");
#endif
    }

    bool exceptionMatched = false;

    if ((start <= pcOffset) && (pcOffset < (start + length))) {
#ifdef DEBUG
      fprintf(stderr,
              "handleLsda(...): Landing pad found.\n");
#endif
      int64_t actionValue = 0;

      if (actionEntry) {
        exceptionMatched = handleActionValue(&actionValue,
                                             ttypeEncoding,
                                             ClassInfo,
                                             actionEntry,
                                             exceptionClass,
                                             exceptionObject);
      }

      if (!(actions & _UA_SEARCH_PHASE)) {
#ifdef DEBUG
        fprintf(stderr,
                "handleLsda(...): installed landing pad "
                "context.\n");
#endif

        // Found landing pad for the PC.
        // Set Instruction Pointer to so we re-enter function
        // at landing pad. The landing pad is created by the
        // compiler to take two parameters in registers.
        _Unwind_SetGR(context,
                      __builtin_eh_return_data_regno(0),
                      (uintptr_t)exceptionObject);

        // Note: this virtual register directly corresponds
        //       to the return of the llvm.eh.selector intrinsic
        if (!actionEntry || !exceptionMatched) {
          // We indicate cleanup only
          _Unwind_SetGR(context,
                        __builtin_eh_return_data_regno(1),
                        0);
        }
        else {
          // Matched type info index of llvm.eh.selector intrinsic
          // passed here.
          _Unwind_SetGR(context,
                        __builtin_eh_return_data_regno(1),
                        actionValue);
        }

        // To execute landing pad set here
        _Unwind_SetIP(context, funcStart + landingPad);
        ret = _URC_INSTALL_CONTEXT;
      }
      else if (exceptionMatched) {
#ifdef DEBUG
        fprintf(stderr,
                "handleLsda(...): setting handler found.\n");
#endif
        ret = _URC_HANDLER_FOUND;
      }
      else {
        // Note: Only non-clean up handlers are marked as
        //       found. Otherwise the clean up handlers will be
        //       re-found and executed during the clean up
        //       phase.
#ifdef DEBUG
        fprintf(stderr,
                "handleLsda(...): cleanup handler found.\n");
#endif
      }

      break;
    }
  }

  return(ret);
}


/// This is the personality function which is embedded (dwarf emitted), in the
/// dwarf unwind info block. Again see: JITDwarfEmitter.cpp.
/// See @link http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html @unlink
/// @param version unsupported (ignored), unwind version
/// @param _Unwind_Action actions minimally supported unwind stage
///        (forced specifically not supported)
/// @param exceptionClass exception class (_Unwind_Exception::exception_class)
///        of thrown exception.
/// @param exceptionObject thrown _Unwind_Exception instance.
/// @param context unwind system context
/// @returns minimally supported unwinding control indicator
_Unwind_Reason_Code ourPersonality(int version, _Unwind_Action actions,
                                   _Unwind_Exception_Class exceptionClass,
                                   struct _Unwind_Exception *exceptionObject,
                                   struct _Unwind_Context *context) {
#ifdef DEBUG
  fprintf(stderr,
          "We are in ourPersonality(...):actions is <%d>.\n",
          actions);

  if (actions & _UA_SEARCH_PHASE) {
    fprintf(stderr, "ourPersonality(...):In search phase.\n");
  }
  else {
    fprintf(stderr, "ourPersonality(...):In non-search phase.\n");
  }
#endif

  const uint8_t *lsda = (const uint8_t *)_Unwind_GetLanguageSpecificData(context);

#ifdef DEBUG
  fprintf(stderr,
          "ourPersonality(...):lsda = <%p>.\n",
          (void*)lsda);
#endif

  // The real work of the personality function is captured here
  return(handleLsda(version,
                    lsda,
                    actions,
                    exceptionClass,
                    exceptionObject,
                    context));
}


/// Generates our _Unwind_Exception class from a given character array.
/// thereby handling arbitrary lengths (not in standard), and handling
/// embedded \0s.
/// See @link http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html @unlink
/// @param classChars char array to encode. NULL values not checkedf
/// @param classCharsSize number of chars in classChars. Value is not checked.
/// @returns class value
uint64_t genClass(const unsigned char classChars[], size_t classCharsSize)
{
  uint64_t ret = classChars[0];

  for (unsigned i = 1; i < classCharsSize; ++i) {
    ret <<= 8;
    ret += classChars[i];
  }

  return(ret);
}

} // extern "C"

//
// Runtime C Library functions End
//

//
// Code generation functions
//

/// Generates code to print given constant string
/// @param context llvm context
/// @param module code for module instance
/// @param builder builder instance
/// @param toPrint string to print
/// @param useGlobal A value of true (default) indicates a GlobalValue is
///        generated, and is used to hold the constant string. A value of
///        false indicates that the constant string will be stored on the
///        stack.
void generateStringPrint(llvm::LLVMContext &context,
                         llvm::Module &module,
                         llvm::IRBuilder<> &builder,
                         std::string toPrint,
                         bool useGlobal = true) {
  llvm::Function *printFunct = module.getFunction("printStr");

  llvm::Value *stringVar;
  llvm::Constant *stringConstant =
  llvm::ConstantDataArray::getString(context, toPrint);

  if (useGlobal) {
    // Note: Does not work without allocation
    stringVar =
    new llvm::GlobalVariable(module,
                             stringConstant->getType(),
                             true,
                             llvm::GlobalValue::PrivateLinkage,
                             stringConstant,
                             "");
  }
  else {
    stringVar = builder.CreateAlloca(stringConstant->getType());
    builder.CreateStore(stringConstant, stringVar);
  }

  llvm::Value *cast = builder.CreatePointerCast(stringVar,
                                                builder.getPtrTy());
  builder.CreateCall(printFunct, cast);
}


/// Generates code to print given runtime integer according to constant
/// string format, and a given print function.
/// @param context llvm context
/// @param module code for module instance
/// @param builder builder instance
/// @param printFunct function used to "print" integer
/// @param toPrint string to print
/// @param format printf like formating string for print
/// @param useGlobal A value of true (default) indicates a GlobalValue is
///        generated, and is used to hold the constant string. A value of
///        false indicates that the constant string will be stored on the
///        stack.
void generateIntegerPrint(llvm::LLVMContext &context,
                          llvm::Module &module,
                          llvm::IRBuilder<> &builder,
                          llvm::Function &printFunct,
                          llvm::Value &toPrint,
                          std::string format,
                          bool useGlobal = true) {
  llvm::Constant *stringConstant =
    llvm::ConstantDataArray::getString(context, format);
  llvm::Value *stringVar;

  if (useGlobal) {
    // Note: Does not seem to work without allocation
    stringVar =
    new llvm::GlobalVariable(module,
                             stringConstant->getType(),
                             true,
                             llvm::GlobalValue::PrivateLinkage,
                             stringConstant,
                             "");
  }
  else {
    stringVar = builder.CreateAlloca(stringConstant->getType());
    builder.CreateStore(stringConstant, stringVar);
  }

  llvm::Value *cast = builder.CreateBitCast(stringVar,
                                            builder.getPtrTy());
  builder.CreateCall(&printFunct, {&toPrint, cast});
}


/// Generates code to handle finally block type semantics: always runs
/// regardless of whether a thrown exception is passing through or the
/// parent function is simply exiting. In addition to printing some state
/// to stderr, this code will resume the exception handling--runs the
/// unwind resume block, if the exception has not been previously caught
/// by a catch clause, and will otherwise execute the end block (terminator
/// block). In addition this function creates the corresponding function's
/// stack storage for the exception pointer and catch flag status.
/// @param context llvm context
/// @param module code for module instance
/// @param builder builder instance
/// @param toAddTo parent function to add block to
/// @param blockName block name of new "finally" block.
/// @param functionId output id used for printing
/// @param terminatorBlock terminator "end" block
/// @param unwindResumeBlock unwind resume block
/// @param exceptionCaughtFlag reference exception caught/thrown status storage
/// @param exceptionStorage reference to exception pointer storage
/// @param caughtResultStorage reference to landingpad result storage
/// @returns newly created block
static llvm::BasicBlock *createFinallyBlock(llvm::LLVMContext &context,
                                            llvm::Module &module,
                                            llvm::IRBuilder<> &builder,
                                            llvm::Function &toAddTo,
                                            std::string &blockName,
                                            std::string &functionId,
                                            llvm::BasicBlock &terminatorBlock,
                                            llvm::BasicBlock &unwindResumeBlock,
                                            llvm::Value **exceptionCaughtFlag,
                                            llvm::Value **exceptionStorage,
                                            llvm::Value **caughtResultStorage) {
  assert(exceptionCaughtFlag &&
         "ExceptionDemo::createFinallyBlock(...):exceptionCaughtFlag "
         "is NULL");
  assert(exceptionStorage &&
         "ExceptionDemo::createFinallyBlock(...):exceptionStorage "
         "is NULL");
  assert(caughtResultStorage &&
         "ExceptionDemo::createFinallyBlock(...):caughtResultStorage "
         "is NULL");

  *exceptionCaughtFlag = createEntryBlockAlloca(toAddTo,
                                         "exceptionCaught",
                                         ourExceptionNotThrownState->getType(),
                                         ourExceptionNotThrownState);

  llvm::PointerType *exceptionStorageType = builder.getPtrTy();
  *exceptionStorage = createEntryBlockAlloca(toAddTo,
                                             "exceptionStorage",
                                             exceptionStorageType,
                                             llvm::ConstantPointerNull::get(
                                               exceptionStorageType));
  *caughtResultStorage = createEntryBlockAlloca(toAddTo,
                                              "caughtResultStorage",
                                              ourCaughtResultType,
                                              llvm::ConstantAggregateZero::get(
                                                ourCaughtResultType));

  llvm::BasicBlock *ret = llvm::BasicBlock::Create(context,
                                                   blockName,
                                                   &toAddTo);

  builder.SetInsertPoint(ret);

  std::ostringstream bufferToPrint;
  bufferToPrint << "Gen: Executing finally block "
    << blockName << " in " << functionId << "\n";
  generateStringPrint(context,
                      module,
                      builder,
                      bufferToPrint.str(),
                      USE_GLOBAL_STR_CONSTS);

  llvm::SwitchInst *theSwitch = builder.CreateSwitch(builder.CreateLoad(
                                                       *exceptionCaughtFlag),
                                                     &terminatorBlock,
                                                     2);
  theSwitch->addCase(ourExceptionCaughtState, &terminatorBlock);
  theSwitch->addCase(ourExceptionThrownState, &unwindResumeBlock);

  return(ret);
}


/// Generates catch block semantics which print a string to indicate type of
/// catch executed, sets an exception caught flag, and executes passed in
/// end block (terminator block).
/// @param context llvm context
/// @param module code for module instance
/// @param builder builder instance
/// @param toAddTo parent function to add block to
/// @param blockName block name of new "catch" block.
/// @param functionId output id used for printing
/// @param terminatorBlock terminator "end" block
/// @param exceptionCaughtFlag exception caught/thrown status
/// @returns newly created block
static llvm::BasicBlock *createCatchBlock(llvm::LLVMContext &context,
                                          llvm::Module &module,
                                          llvm::IRBuilder<> &builder,
                                          llvm::Function &toAddTo,
                                          std::string &blockName,
                                          std::string &functionId,
                                          llvm::BasicBlock &terminatorBlock,
                                          llvm::Value &exceptionCaughtFlag) {

  llvm::BasicBlock *ret = llvm::BasicBlock::Create(context,
                                                   blockName,
                                                   &toAddTo);

  builder.SetInsertPoint(ret);

  std::ostringstream bufferToPrint;
  bufferToPrint << "Gen: Executing catch block "
  << blockName
  << " in "
  << functionId
  << std::endl;
  generateStringPrint(context,
                      module,
                      builder,
                      bufferToPrint.str(),
                      USE_GLOBAL_STR_CONSTS);
  builder.CreateStore(ourExceptionCaughtState, &exceptionCaughtFlag);
  builder.CreateBr(&terminatorBlock);

  return(ret);
}


/// Generates a function which invokes a function (toInvoke) and, whose
/// unwind block will "catch" the type info types correspondingly held in the
/// exceptionTypesToCatch argument. If the toInvoke function throws an
/// exception which does not match any type info types contained in
/// exceptionTypesToCatch, the generated code will call _Unwind_Resume
/// with the raised exception. On the other hand the generated code will
/// normally exit if the toInvoke function does not throw an exception.
/// The generated "finally" block is always run regardless of the cause of
/// the generated function exit.
/// The generated function is returned after being verified.
/// @param module code for module instance
/// @param builder builder instance
/// @param fpm a function pass manager holding optional IR to IR
///        transformations
/// @param toInvoke inner function to invoke
/// @param ourId id used to printing purposes
/// @param numExceptionsToCatch length of exceptionTypesToCatch array
/// @param exceptionTypesToCatch array of type info types to "catch"
/// @returns generated function
static llvm::Function *createCatchWrappedInvokeFunction(
    llvm::Module &module, llvm::IRBuilder<> &builder,
    llvm::legacy::FunctionPassManager &fpm, llvm::Function &toInvoke,
    std::string ourId, unsigned numExceptionsToCatch,
    unsigned exceptionTypesToCatch[]) {

  llvm::LLVMContext &context = module.getContext();
  llvm::Function *toPrint32Int = module.getFunction("print32Int");

  ArgTypes argTypes;
  argTypes.push_back(builder.getInt32Ty());

  ArgNames argNames;
  argNames.push_back("exceptTypeToThrow");

  llvm::Function *ret = createFunction(module,
                                       builder.getVoidTy(),
                                       argTypes,
                                       argNames,
                                       ourId,
                                       llvm::Function::ExternalLinkage,
                                       false,
                                       false);

  // Block which calls invoke
  llvm::BasicBlock *entryBlock = llvm::BasicBlock::Create(context,
                                                          "entry",
                                                          ret);
  // Normal block for invoke
  llvm::BasicBlock *normalBlock = llvm::BasicBlock::Create(context,
                                                           "normal",
                                                           ret);
  // Unwind block for invoke
  llvm::BasicBlock *exceptionBlock = llvm::BasicBlock::Create(context,
                                                              "exception",
                                                              ret);

  // Block which routes exception to correct catch handler block
  llvm::BasicBlock *exceptionRouteBlock = llvm::BasicBlock::Create(context,
                                                             "exceptionRoute",
                                                             ret);

  // Foreign exception handler
  llvm::BasicBlock *externalExceptionBlock = llvm::BasicBlock::Create(context,
                                                          "externalException",
                                                          ret);

  // Block which calls _Unwind_Resume
  llvm::BasicBlock *unwindResumeBlock = llvm::BasicBlock::Create(context,
                                                               "unwindResume",
                                                               ret);

  // Clean up block which delete exception if needed
  llvm::BasicBlock *endBlock = llvm::BasicBlock::Create(context, "end", ret);

  std::string nextName;
  std::vector<llvm::BasicBlock*> catchBlocks(numExceptionsToCatch);
  llvm::Value *exceptionCaughtFlag = NULL;
  llvm::Value *exceptionStorage = NULL;
  llvm::Value *caughtResultStorage = NULL;

  // Finally block which will branch to unwindResumeBlock if
  // exception is not caught. Initializes/allocates stack locations.
  llvm::BasicBlock *finallyBlock = createFinallyBlock(context,
                                                      module,
                                                      builder,
                                                      *ret,
                                                      nextName = "finally",
                                                      ourId,
                                                      *endBlock,
                                                      *unwindResumeBlock,
                                                      &exceptionCaughtFlag,
                                                      &exceptionStorage,
                                                      &caughtResultStorage
                                                      );

  for (unsigned i = 0; i < numExceptionsToCatch; ++i) {
    nextName = ourTypeInfoNames[exceptionTypesToCatch[i]];

    // One catch block per type info to be caught
    catchBlocks[i] = createCatchBlock(context,
                                      module,
                                      builder,
                                      *ret,
                                      nextName,
                                      ourId,
                                      *finallyBlock,
                                      *exceptionCaughtFlag);
  }

  // Entry Block

  builder.SetInsertPoint(entryBlock);

  std::vector<llvm::Value*> args;
  args.push_back(namedValues["exceptTypeToThrow"]);
  builder.CreateInvoke(&toInvoke,
                       normalBlock,
                       exceptionBlock,
                       args);

  // End Block

  builder.SetInsertPoint(endBlock);

  generateStringPrint(context,
                      module,
                      builder,
                      "Gen: In end block: exiting in " + ourId + ".\n",
                      USE_GLOBAL_STR_CONSTS);
  llvm::Function *deleteOurException = module.getFunction("deleteOurException");

  // Note: function handles NULL exceptions
  builder.CreateCall(deleteOurException,
                     builder.CreateLoad(exceptionStorage));
  builder.CreateRetVoid();

  // Normal Block

  builder.SetInsertPoint(normalBlock);

  generateStringPrint(context,
                      module,
                      builder,
                      "Gen: No exception in " + ourId + "!\n",
                      USE_GLOBAL_STR_CONSTS);

  // Finally block is always called
  builder.CreateBr(finallyBlock);

  // Unwind Resume Block

  builder.SetInsertPoint(unwindResumeBlock);

  builder.CreateResume(builder.CreateLoad(caughtResultStorage));

  // Exception Block

  builder.SetInsertPoint(exceptionBlock);

  llvm::Function *personality = module.getFunction("ourPersonality");
  ret->setPersonalityFn(personality);

  llvm::LandingPadInst *caughtResult =
    builder.CreateLandingPad(ourCaughtResultType,
                             numExceptionsToCatch,
                             "landingPad");

  caughtResult->setCleanup(true);

  for (unsigned i = 0; i < numExceptionsToCatch; ++i) {
    // Set up type infos to be caught
    caughtResult->addClause(module.getGlobalVariable(
                             ourTypeInfoNames[exceptionTypesToCatch[i]]));
  }

  llvm::Value *unwindException = builder.CreateExtractValue(caughtResult, 0);
  llvm::Value *retTypeInfoIndex = builder.CreateExtractValue(caughtResult, 1);

  // FIXME: Redundant storage which, beyond utilizing value of
  //        caughtResultStore for unwindException storage, may be alleviated
  //        altogether with a block rearrangement
  builder.CreateStore(caughtResult, caughtResultStorage);
  builder.CreateStore(unwindException, exceptionStorage);
  builder.CreateStore(ourExceptionThrownState, exceptionCaughtFlag);

  // Retrieve exception_class member from thrown exception
  // (_Unwind_Exception instance). This member tells us whether or not
  // the exception is foreign.
  llvm::Value *unwindExceptionClass =
      builder.CreateLoad(builder.CreateStructGEP(
          ourUnwindExceptionType,
          builder.CreatePointerCast(unwindException,
                                    ourUnwindExceptionType->getPointerTo()),
          0));

  // Branch to the externalExceptionBlock if the exception is foreign or
  // to a catch router if not. Either way the finally block will be run.
  builder.CreateCondBr(builder.CreateICmpEQ(unwindExceptionClass,
                            llvm::ConstantInt::get(builder.getInt64Ty(),
                                                   ourBaseExceptionClass)),
                       exceptionRouteBlock,
                       externalExceptionBlock);

  // External Exception Block

  builder.SetInsertPoint(externalExceptionBlock);

  generateStringPrint(context,
                      module,
                      builder,
                      "Gen: Foreign exception received.\n",
                      USE_GLOBAL_STR_CONSTS);

  // Branch to the finally block
  builder.CreateBr(finallyBlock);

  // Exception Route Block

  builder.SetInsertPoint(exceptionRouteBlock);

  // Casts exception pointer (_Unwind_Exception instance) to parent
  // (OurException instance).
  //
  // Note: ourBaseFromUnwindOffset is usually negative
  llvm::Value *typeInfoThrown = builder.CreatePointerCast(
                                  builder.CreateConstGEP1_64(unwindException,
                                                       ourBaseFromUnwindOffset),
                                  ourExceptionType->getPointerTo());

  // Retrieve thrown exception type info type
  //
  // Note: Index is not relative to pointer but instead to structure
  //       unlike a true getelementptr (GEP) instruction
  typeInfoThrown = builder.CreateStructGEP(ourExceptionType, typeInfoThrown, 0);

  llvm::Value *typeInfoThrownType =
      builder.CreateStructGEP(builder.getPtrTy(), typeInfoThrown, 0);

  generateIntegerPrint(context,
                       module,
                       builder,
                       *toPrint32Int,
                       *(builder.CreateLoad(typeInfoThrownType)),
                       "Gen: Exception type <%d> received (stack unwound) "
                       " in " +
                       ourId +
                       ".\n",
                       USE_GLOBAL_STR_CONSTS);

  // Route to matched type info catch block or run cleanup finally block
  llvm::SwitchInst *switchToCatchBlock = builder.CreateSwitch(retTypeInfoIndex,
                                                          finallyBlock,
                                                          numExceptionsToCatch);

  unsigned nextTypeToCatch;

  for (unsigned i = 1; i <= numExceptionsToCatch; ++i) {
    nextTypeToCatch = i - 1;
    switchToCatchBlock->addCase(llvm::ConstantInt::get(
                                   llvm::Type::getInt32Ty(context), i),
                                catchBlocks[nextTypeToCatch]);
  }

  llvm::verifyFunction(*ret);
  fpm.run(*ret);

  return(ret);
}


/// Generates function which throws either an exception matched to a runtime
/// determined type info type (argument to generated function), or if this
/// runtime value matches nativeThrowType, throws a foreign exception by
/// calling nativeThrowFunct.
/// @param module code for module instance
/// @param builder builder instance
/// @param fpm a function pass manager holding optional IR to IR
///        transformations
/// @param ourId id used to printing purposes
/// @param nativeThrowType a runtime argument of this value results in
///        nativeThrowFunct being called to generate/throw exception.
/// @param nativeThrowFunct function which will throw a foreign exception
///        if the above nativeThrowType matches generated function's arg.
/// @returns generated function
static llvm::Function *
createThrowExceptionFunction(llvm::Module &module, llvm::IRBuilder<> &builder,
                             llvm::legacy::FunctionPassManager &fpm,
                             std::string ourId, int32_t nativeThrowType,
                             llvm::Function &nativeThrowFunct) {
  llvm::LLVMContext &context = module.getContext();
  namedValues.clear();
  ArgTypes unwindArgTypes;
  unwindArgTypes.push_back(builder.getInt32Ty());
  ArgNames unwindArgNames;
  unwindArgNames.push_back("exceptTypeToThrow");

  llvm::Function *ret = createFunction(module,
                                       builder.getVoidTy(),
                                       unwindArgTypes,
                                       unwindArgNames,
                                       ourId,
                                       llvm::Function::ExternalLinkage,
                                       false,
                                       false);

  // Throws either one of our exception or a native C++ exception depending
  // on a runtime argument value containing a type info type.
  llvm::BasicBlock *entryBlock = llvm::BasicBlock::Create(context,
                                                          "entry",
                                                          ret);
  // Throws a foreign exception
  llvm::BasicBlock *nativeThrowBlock = llvm::BasicBlock::Create(context,
                                                                "nativeThrow",
                                                                ret);
  // Throws one of our Exceptions
  llvm::BasicBlock *generatedThrowBlock = llvm::BasicBlock::Create(context,
                                                             "generatedThrow",
                                                             ret);
  // Retrieved runtime type info type to throw
  llvm::Value *exceptionType = namedValues["exceptTypeToThrow"];

  // nativeThrowBlock block

  builder.SetInsertPoint(nativeThrowBlock);

  // Throws foreign exception
  builder.CreateCall(&nativeThrowFunct, exceptionType);
  builder.CreateUnreachable();

  // entry block

  builder.SetInsertPoint(entryBlock);

  llvm::Function *toPrint32Int = module.getFunction("print32Int");
  generateIntegerPrint(context,
                       module,
                       builder,
                       *toPrint32Int,
                       *exceptionType,
                       "\nGen: About to throw exception type <%d> in " +
                       ourId +
                       ".\n",
                       USE_GLOBAL_STR_CONSTS);

  // Switches on runtime type info type value to determine whether or not
  // a foreign exception is thrown. Defaults to throwing one of our
  // generated exceptions.
  llvm::SwitchInst *theSwitch = builder.CreateSwitch(exceptionType,
                                                     generatedThrowBlock,
                                                     1);

  theSwitch->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context),
                                            nativeThrowType),
                     nativeThrowBlock);

  // generatedThrow block

  builder.SetInsertPoint(generatedThrowBlock);

  llvm::Function *createOurException = module.getFunction("createOurException");
  llvm::Function *raiseOurException = module.getFunction(
                                        "_Unwind_RaiseException");

  // Creates exception to throw with runtime type info type.
  llvm::Value *exception = builder.CreateCall(createOurException,
                                              namedValues["exceptTypeToThrow"]);

  // Throw generated Exception
  builder.CreateCall(raiseOurException, exception);
  builder.CreateUnreachable();

  llvm::verifyFunction(*ret);
  fpm.run(*ret);

  return(ret);
}

static void createStandardUtilityFunctions(unsigned numTypeInfos,
                                           llvm::Module &module,
                                           llvm::IRBuilder<> &builder);

/// Creates test code by generating and organizing these functions into the
/// test case. The test case consists of an outer function setup to invoke
/// an inner function within an environment having multiple catch and single
/// finally blocks. This inner function is also setup to invoke a throw
/// function within an evironment similar in nature to the outer function's
/// catch and finally blocks. Each of these two functions catch mutually
/// exclusive subsets (even or odd) of the type info types configured
/// for this this. All generated functions have a runtime argument which
/// holds a type info type to throw that each function takes and passes it
/// to the inner one if such a inner function exists. This type info type is
/// looked at by the generated throw function to see whether or not it should
/// throw a generated exception with the same type info type, or instead call
/// a supplied a function which in turn will throw a foreign exception.
/// @param module code for module instance
/// @param builder builder instance
/// @param fpm a function pass manager holding optional IR to IR
///        transformations
/// @param nativeThrowFunctName name of external function which will throw
///        a foreign exception
/// @returns outermost generated test function.
llvm::Function *
createUnwindExceptionTest(llvm::Module &module, llvm::IRBuilder<> &builder,
                          llvm::legacy::FunctionPassManager &fpm,
                          std::string nativeThrowFunctName) {
  // Number of type infos to generate
  unsigned numTypeInfos = 6;

  // Initialze intrisics and external functions to use along with exception
  // and type info globals.
  createStandardUtilityFunctions(numTypeInfos,
                                 module,
                                 builder);
  llvm::Function *nativeThrowFunct = module.getFunction(nativeThrowFunctName);

  // Create exception throw function using the value ~0 to cause
  // foreign exceptions to be thrown.
  llvm::Function *throwFunct = createThrowExceptionFunction(module,
                                                            builder,
                                                            fpm,
                                                            "throwFunct",
                                                            ~0,
                                                            *nativeThrowFunct);
  // Inner function will catch even type infos
  unsigned innerExceptionTypesToCatch[] = {6, 2, 4};
  size_t numExceptionTypesToCatch = sizeof(innerExceptionTypesToCatch) /
                                    sizeof(unsigned);

  // Generate inner function.
  llvm::Function *innerCatchFunct = createCatchWrappedInvokeFunction(module,
                                                    builder,
                                                    fpm,
                                                    *throwFunct,
                                                    "innerCatchFunct",
                                                    numExceptionTypesToCatch,
                                                    innerExceptionTypesToCatch);

  // Outer function will catch odd type infos
  unsigned outerExceptionTypesToCatch[] = {3, 1, 5};
  numExceptionTypesToCatch = sizeof(outerExceptionTypesToCatch) /
  sizeof(unsigned);

  // Generate outer function
  llvm::Function *outerCatchFunct = createCatchWrappedInvokeFunction(module,
                                                    builder,
                                                    fpm,
                                                    *innerCatchFunct,
                                                    "outerCatchFunct",
                                                    numExceptionTypesToCatch,
                                                    outerExceptionTypesToCatch);

  // Return outer function to run
  return(outerCatchFunct);
}

namespace {
/// Represents our foreign exceptions
class OurCppRunException : public std::runtime_error {
public:
  OurCppRunException(const std::string reason) :
  std::runtime_error(reason) {}

  OurCppRunException (const OurCppRunException &toCopy) :
  std::runtime_error(toCopy) {}

  OurCppRunException &operator = (const OurCppRunException &toCopy) {
    return(reinterpret_cast<OurCppRunException&>(
                                 std::runtime_error::operator=(toCopy)));
  }

  ~OurCppRunException(void) throw() override {}
};
} // end anonymous namespace

/// Throws foreign C++ exception.
/// @param ignoreIt unused parameter that allows function to match implied
///        generated function contract.
extern "C"
void throwCppException (int32_t ignoreIt) {
  throw(OurCppRunException("thrown by throwCppException(...)"));
}

typedef void (*OurExceptionThrowFunctType) (int32_t typeToThrow);

/// This is a test harness which runs test by executing generated
/// function with a type info type to throw. Harness wraps the execution
/// of generated function in a C++ try catch clause.
/// @param engine execution engine to use for executing generated function.
///        This demo program expects this to be a JIT instance for demo
///        purposes.
/// @param function generated test function to run
/// @param typeToThrow type info type of generated exception to throw, or
///        indicator to cause foreign exception to be thrown.
static
void runExceptionThrow(llvm::ExecutionEngine *engine,
                       llvm::Function *function,
                       int32_t typeToThrow) {

  // Find test's function pointer
  OurExceptionThrowFunctType functPtr =
    reinterpret_cast<OurExceptionThrowFunctType>(
       reinterpret_cast<intptr_t>(engine->getPointerToFunction(function)));

  try {
    // Run test
    (*functPtr)(typeToThrow);
  }
  catch (OurCppRunException exc) {
    // Catch foreign C++ exception
    fprintf(stderr,
            "\nrunExceptionThrow(...):In C++ catch OurCppRunException "
            "with reason: %s.\n",
            exc.what());
  }
  catch (...) {
    // Catch all exceptions including our generated ones. This latter
    // functionality works according to the example in rules 1.6.4 of
    // http://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html (v1.22),
    // given that these will be exceptions foreign to C++
    // (the _Unwind_Exception::exception_class should be different from
    // the one used by C++).
    fprintf(stderr,
            "\nrunExceptionThrow(...):In C++ catch all.\n");
  }
}

//
// End test functions
//

typedef llvm::ArrayRef<llvm::Type*> TypeArray;

/// This initialization routine creates type info globals and
/// adds external function declarations to module.
/// @param numTypeInfos number of linear type info associated type info types
///        to create as GlobalVariable instances, starting with the value 1.
/// @param module code for module instance
/// @param builder builder instance
static void createStandardUtilityFunctions(unsigned numTypeInfos,
                                           llvm::Module &module,
                                           llvm::IRBuilder<> &builder) {

  llvm::LLVMContext &context = module.getContext();

  // Exception initializations

  // Setup exception catch state
  ourExceptionNotThrownState =
    llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), 0),
  ourExceptionThrownState =
    llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), 1),
  ourExceptionCaughtState =
    llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), 2),



  // Create our type info type
  ourTypeInfoType = llvm::StructType::get(context,
                                          TypeArray(builder.getInt32Ty()));

  llvm::Type *caughtResultFieldTypes[] = {
    builder.getPtrTy(),
    builder.getInt32Ty()
  };

  // Create our landingpad result type
  ourCaughtResultType = llvm::StructType::get(context,
                                            TypeArray(caughtResultFieldTypes));

  // Create OurException type
  ourExceptionType = llvm::StructType::get(context,
                                           TypeArray(ourTypeInfoType));

  // Create portion of _Unwind_Exception type
  //
  // Note: Declaring only a portion of the _Unwind_Exception struct.
  //       Does this cause problems?
  ourUnwindExceptionType =
    llvm::StructType::get(context,
                    TypeArray(builder.getInt64Ty()));

  struct OurBaseException_t dummyException;

  // Calculate offset of OurException::unwindException member.
  ourBaseFromUnwindOffset = ((uintptr_t) &dummyException) -
                            ((uintptr_t) &(dummyException.unwindException));

#ifdef DEBUG
  fprintf(stderr,
          "createStandardUtilityFunctions(...):ourBaseFromUnwindOffset "
          "= %" PRIi64 ", sizeof(struct OurBaseException_t) - "
          "sizeof(struct _Unwind_Exception) = %lu.\n",
          ourBaseFromUnwindOffset,
          sizeof(struct OurBaseException_t) -
          sizeof(struct _Unwind_Exception));
#endif

  size_t numChars = sizeof(ourBaseExcpClassChars) / sizeof(char);

  // Create our _Unwind_Exception::exception_class value
  ourBaseExceptionClass = genClass(ourBaseExcpClassChars, numChars);

  // Type infos

  std::string baseStr = "typeInfo", typeInfoName;
  std::ostringstream typeInfoNameBuilder;
  std::vector<llvm::Constant*> structVals;

  llvm::Constant *nextStruct;

  // Generate each type info
  //
  // Note: First type info is not used.
  for (unsigned i = 0; i <= numTypeInfos; ++i) {
    structVals.clear();
    structVals.push_back(llvm::ConstantInt::get(builder.getInt32Ty(), i));
    nextStruct = llvm::ConstantStruct::get(ourTypeInfoType, structVals);

    typeInfoNameBuilder.str("");
    typeInfoNameBuilder << baseStr << i;
    typeInfoName = typeInfoNameBuilder.str();

    // Note: Does not seem to work without allocation
    new llvm::GlobalVariable(module,
                             ourTypeInfoType,
                             true,
                             llvm::GlobalValue::ExternalLinkage,
                             nextStruct,
                             typeInfoName);

    ourTypeInfoNames.push_back(typeInfoName);
    ourTypeInfoNamesIndex[i] = typeInfoName;
  }

  ArgNames argNames;
  ArgTypes argTypes;
  llvm::Function *funct = NULL;

  // print32Int

  llvm::Type *retType = builder.getVoidTy();

  argTypes.clear();
  argTypes.push_back(builder.getInt32Ty());
  argTypes.push_back(builder.getPtrTy());

  argNames.clear();

  createFunction(module,
                 retType,
                 argTypes,
                 argNames,
                 "print32Int",
                 llvm::Function::ExternalLinkage,
                 true,
                 false);

  // print64Int

  retType = builder.getVoidTy();

  argTypes.clear();
  argTypes.push_back(builder.getInt64Ty());
  argTypes.push_back(builder.getPtrTy());

  argNames.clear();

  createFunction(module,
                 retType,
                 argTypes,
                 argNames,
                 "print64Int",
                 llvm::Function::ExternalLinkage,
                 true,
                 false);

  // printStr

  retType = builder.getVoidTy();

  argTypes.clear();
  argTypes.push_back(builder.getPtrTy());

  argNames.clear();

  createFunction(module,
                 retType,
                 argTypes,
                 argNames,
                 "printStr",
                 llvm::Function::ExternalLinkage,
                 true,
                 false);

  // throwCppException

  retType = builder.getVoidTy();

  argTypes.clear();
  argTypes.push_back(builder.getInt32Ty());

  argNames.clear();

  createFunction(module,
                 retType,
                 argTypes,
                 argNames,
                 "throwCppException",
                 llvm::Function::ExternalLinkage,
                 true,
                 false);

  // deleteOurException

  retType = builder.getVoidTy();

  argTypes.clear();
  argTypes.push_back(builder.getPtrTy());

  argNames.clear();

  createFunction(module,
                 retType,
                 argTypes,
                 argNames,
                 "deleteOurException",
                 llvm::Function::ExternalLinkage,
                 true,
                 false);

  // createOurException

  retType = builder.getPtrTy();

  argTypes.clear();
  argTypes.push_back(builder.getInt32Ty());

  argNames.clear();

  createFunction(module,
                 retType,
                 argTypes,
                 argNames,
                 "createOurException",
                 llvm::Function::ExternalLinkage,
                 true,
                 false);

  // _Unwind_RaiseException

  retType = builder.getInt32Ty();

  argTypes.clear();
  argTypes.push_back(builder.getPtrTy());

  argNames.clear();

  funct = createFunction(module,
                         retType,
                         argTypes,
                         argNames,
                         "_Unwind_RaiseException",
                         llvm::Function::ExternalLinkage,
                         true,
                         false);

  funct->setDoesNotReturn();

  // _Unwind_Resume

  retType = builder.getInt32Ty();

  argTypes.clear();
  argTypes.push_back(builder.getPtrTy());

  argNames.clear();

  funct = createFunction(module,
                         retType,
                         argTypes,
                         argNames,
                         "_Unwind_Resume",
                         llvm::Function::ExternalLinkage,
                         true,
                         false);

  funct->setDoesNotReturn();

  // ourPersonality

  retType = builder.getInt32Ty();

  argTypes.clear();
  argTypes.push_back(builder.getInt32Ty());
  argTypes.push_back(builder.getInt32Ty());
  argTypes.push_back(builder.getInt64Ty());
  argTypes.push_back(builder.getPtrTy());
  argTypes.push_back(builder.getPtrTy());

  argNames.clear();

  createFunction(module,
                 retType,
                 argTypes,
                 argNames,
                 "ourPersonality",
                 llvm::Function::ExternalLinkage,
                 true,
                 false);

  // llvm.eh.typeid.for intrinsic

  getDeclaration(&module, llvm::Intrinsic::eh_typeid_for, builder.getPtrTy());
}


//===----------------------------------------------------------------------===//
// Main test driver code.
//===----------------------------------------------------------------------===//

/// Demo main routine which takes the type info types to throw. A test will
/// be run for each given type info type. While type info types with the value
/// of -1 will trigger a foreign C++ exception to be thrown; type info types
/// <= 6 and >= 1 will be caught by test functions; and type info types > 6
/// will result in exceptions which pass through to the test harness. All other
/// type info types are not supported and could cause a crash.
int main(int argc, char *argv[]) {
  if (argc == 1) {
    fprintf(stderr,
            "\nUsage: ExceptionDemo <exception type to throw> "
            "[<type 2>...<type n>].\n"
            "   Each type must have the value of 1 - 6 for "
            "generated exceptions to be caught;\n"
            "   the value -1 for foreign C++ exceptions to be "
            "generated and thrown;\n"
            "   or the values > 6 for exceptions to be ignored.\n"
            "\nTry: ExceptionDemo 2 3 7 -1\n"
            "   for a full test.\n\n");
    return(0);
  }

  // If not set, exception handling will not be turned on
  llvm::TargetOptions Opts;

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::LLVMContext Context;
  llvm::IRBuilder<> theBuilder(Context);

  // Make the module, which holds all the code.
  std::unique_ptr<llvm::Module> Owner =
      std::make_unique<llvm::Module>("my cool jit", Context);
  llvm::Module *module = Owner.get();

  std::unique_ptr<llvm::RTDyldMemoryManager> MemMgr(new llvm::SectionMemoryManager());

  // Build engine with JIT
  llvm::EngineBuilder factory(std::move(Owner));
  factory.setEngineKind(llvm::EngineKind::JIT);
  factory.setTargetOptions(Opts);
  factory.setMCJITMemoryManager(std::move(MemMgr));
  llvm::ExecutionEngine *executionEngine = factory.create();

  {
    llvm::legacy::FunctionPassManager fpm(module);

    // Set up the optimizer pipeline.
    // Start with registering info about how the
    // target lays out data structures.
    module->setDataLayout(executionEngine->getDataLayout());

    // Optimizations turned on
#ifdef ADD_OPT_PASSES

    // Basic AliasAnslysis support for GVN.
    fpm.add(llvm::createBasicAliasAnalysisPass());

    // Promote allocas to registers.
    fpm.add(llvm::createPromoteMemoryToRegisterPass());

    // Do simple "peephole" optimizations and bit-twiddling optzns.
    fpm.add(llvm::createInstructionCombiningPass());

    // Reassociate expressions.
    fpm.add(llvm::createReassociatePass());

    // Eliminate Common SubExpressions.
    fpm.add(llvm::createGVNPass());

    // Simplify the control flow graph (deleting unreachable
    // blocks, etc).
    fpm.add(llvm::createCFGSimplificationPass());
#endif  // ADD_OPT_PASSES

    fpm.doInitialization();

    // Generate test code using function throwCppException(...) as
    // the function which throws foreign exceptions.
    llvm::Function *toRun =
    createUnwindExceptionTest(*module,
                              theBuilder,
                              fpm,
                              "throwCppException");

    executionEngine->finalizeObject();

#ifndef NDEBUG
    fprintf(stderr, "\nBegin module dump:\n\n");

    module->dump();

    fprintf(stderr, "\nEnd module dump:\n");
#endif

    fprintf(stderr, "\n\nBegin Test:\n");

    for (int i = 1; i < argc; ++i) {
      // Run test for each argument whose value is the exception
      // type to throw.
      runExceptionThrow(executionEngine,
                        toRun,
                        (unsigned) strtoul(argv[i], NULL, 10));
    }

    fprintf(stderr, "\nEnd Test:\n\n");
  }

  delete executionEngine;

  return 0;
}
