//===------- JITLoaderVTune.cpp - Register profiler objects -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register objects for access by profilers via the VTune JIT interface.
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderVTune.h"
#include "llvm/ExecutionEngine/Orc/Shared/VTuneSharedStructs.h"
#include <map>

#if LLVM_USE_INTEL_JITEVENTS
#include "IntelJITEventsWrapper.h"
#include "ittnotify.h"

using namespace llvm;
using namespace llvm::orc;

namespace {
class JITEventWrapper {
public:
  static std::unique_ptr<IntelJITEventsWrapper> Wrapper;
};
std::unique_ptr<IntelJITEventsWrapper> JITEventWrapper::Wrapper;
} // namespace

static Error registerJITLoaderVTuneRegisterImpl(const VTuneMethodBatch &MB) {
  const size_t StringsSize = MB.Strings.size();

  for (const auto &MethodInfo : MB.Methods) {
    iJIT_Method_Load MethodMessage;
    memset(&MethodMessage, 0, sizeof(iJIT_Method_Load));

    MethodMessage.method_id = MethodInfo.MethodID;
    if (MethodInfo.NameSI != 0 && MethodInfo.NameSI < StringsSize) {
      MethodMessage.method_name =
          const_cast<char *>(MB.Strings.at(MethodInfo.NameSI).data());
    } else {
      MethodMessage.method_name = NULL;
    }
    if (MethodInfo.ClassFileSI != 0 && MethodInfo.ClassFileSI < StringsSize) {
      MethodMessage.class_file_name =
          const_cast<char *>(MB.Strings.at(MethodInfo.ClassFileSI).data());
    } else {
      MethodMessage.class_file_name = NULL;
    }
    if (MethodInfo.SourceFileSI != 0 && MethodInfo.SourceFileSI < StringsSize) {
      MethodMessage.source_file_name =
          const_cast<char *>(MB.Strings.at(MethodInfo.SourceFileSI).data());
    } else {
      MethodMessage.source_file_name = NULL;
    }

    MethodMessage.method_load_address = MethodInfo.LoadAddr.toPtr<void *>();
    MethodMessage.method_size = MethodInfo.LoadSize;
    MethodMessage.class_id = 0;

    MethodMessage.user_data = NULL;
    MethodMessage.user_data_size = 0;
    MethodMessage.env = iJDE_JittingAPI;

    std::vector<LineNumberInfo> LineInfo;
    for (const auto &LInfo : MethodInfo.LineTable) {
      LineInfo.push_back(LineNumberInfo{LInfo.first, LInfo.second});
    }

    if (LineInfo.size() == 0) {
      MethodMessage.line_number_size = 0;
      MethodMessage.line_number_table = 0;
    } else {
      MethodMessage.line_number_size = LineInfo.size();
      MethodMessage.line_number_table = &*LineInfo.begin();
    }
    JITEventWrapper::Wrapper->iJIT_NotifyEvent(
        iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, &MethodMessage);
  }

  return Error::success();
}

static void registerJITLoaderVTuneUnregisterImpl(
    const std::vector<std::pair<uint64_t, uint64_t>> &UM) {
  for (auto &Method : UM) {
    JITEventWrapper::Wrapper->iJIT_NotifyEvent(
        iJVM_EVENT_TYPE_METHOD_UNLOAD_START,
        const_cast<uint64_t *>(&Method.first));
  }
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerVTuneImpl(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  if (!JITEventWrapper::Wrapper)
    JITEventWrapper::Wrapper.reset(new IntelJITEventsWrapper);

  return WrapperFunction<SPSError(SPSVTuneMethodBatch)>::handle(
             Data, Size, registerJITLoaderVTuneRegisterImpl)
      .release();
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_unregisterVTuneImpl(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  return WrapperFunction<void(SPSVTuneUnloadedMethodIDs)>::handle(
             Data, Size, registerJITLoaderVTuneUnregisterImpl)
      .release();
}

// For Testing: following code comes from llvm-jitlistener.cpp in llvm tools
namespace {
using SourceLocations = std::vector<std::pair<std::string, unsigned int>>;
using NativeCodeMap = std::map<uint64_t, SourceLocations>;
NativeCodeMap ReportedDebugFuncs;
} // namespace

static int NotifyEvent(iJIT_JVM_EVENT EventType, void *EventSpecificData) {
  switch (EventType) {
  case iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED: {
    if (!EventSpecificData) {
      errs() << "Error: The JIT event listener did not provide a event data.";
      return -1;
    }
    iJIT_Method_Load *msg = static_cast<iJIT_Method_Load *>(EventSpecificData);

    ReportedDebugFuncs[msg->method_id];

    outs() << "Method load [" << msg->method_id << "]: " << msg->method_name
           << ", Size = " << msg->method_size << "\n";

    for (unsigned int i = 0; i < msg->line_number_size; ++i) {
      if (!msg->line_number_table) {
        errs() << "A function with a non-zero line count had no line table.";
        return -1;
      }
      std::pair<std::string, unsigned int> loc(
          std::string(msg->source_file_name),
          msg->line_number_table[i].LineNumber);
      ReportedDebugFuncs[msg->method_id].push_back(loc);
      outs() << "  Line info @ " << msg->line_number_table[i].Offset << ": "
             << msg->source_file_name << ", line "
             << msg->line_number_table[i].LineNumber << "\n";
    }
    outs() << "\n";
  } break;
  case iJVM_EVENT_TYPE_METHOD_UNLOAD_START: {
    if (!EventSpecificData) {
      errs() << "Error: The JIT event listener did not provide a event data.";
      return -1;
    }
    unsigned int UnloadId =
        *reinterpret_cast<unsigned int *>(EventSpecificData);
    assert(1 == ReportedDebugFuncs.erase(UnloadId));
    outs() << "Method unload [" << UnloadId << "]\n";
  } break;
  default:
    break;
  }
  return 0;
}

static iJIT_IsProfilingActiveFlags IsProfilingActive(void) {
  // for testing, pretend we have an Intel Parallel Amplifier XE 2011
  // instance attached
  return iJIT_SAMPLING_ON;
}

static unsigned int GetNewMethodID(void) {
  static unsigned int id = 0;
  return ++id;
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_test_registerVTuneImpl(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  JITEventWrapper::Wrapper.reset(new IntelJITEventsWrapper(
      NotifyEvent, NULL, NULL, IsProfilingActive, 0, 0, GetNewMethodID));
  return WrapperFunction<SPSError(SPSVTuneMethodBatch)>::handle(
             Data, Size, registerJITLoaderVTuneRegisterImpl)
      .release();
}

#else

using namespace llvm;
using namespace llvm::orc;

static Error unsupportedBatch(const VTuneMethodBatch &MB) {
  return llvm::make_error<StringError>("unsupported for Intel VTune",
                                       inconvertibleErrorCode());
}

static void unsuppported(const std::vector<std::pair<uint64_t, uint64_t>> &UM) {

}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerVTuneImpl(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  return WrapperFunction<SPSError(SPSVTuneMethodBatch)>::handle(
             Data, Size, unsupportedBatch)
      .release();
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_unregisterVTuneImpl(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  return WrapperFunction<void(SPSVTuneUnloadedMethodIDs)>::handle(Data, Size,
                                                                  unsuppported)
      .release();
}

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_test_registerVTuneImpl(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  return WrapperFunction<SPSError(SPSVTuneMethodBatch)>::handle(
             Data, Size, unsupportedBatch)
      .release();
}

#endif
