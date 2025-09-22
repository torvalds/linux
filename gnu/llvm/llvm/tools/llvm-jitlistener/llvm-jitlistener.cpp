//===-- llvm-jitlistener.cpp - Utility for testing MCJIT event listener ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is a used by lit tests to verify the MCJIT JITEventListener
// interface.  It registers a mock JIT event listener, generates a module from
// an input IR file and dumps the reported event information to stdout.
//
//===----------------------------------------------------------------------===//

#include "../../lib/ExecutionEngine/IntelJITProfiling/IntelJITEventsWrapper.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <string>

using namespace llvm;

namespace {

typedef std::vector<std::pair<std::string, unsigned int> > SourceLocations;
typedef std::map<uint64_t, SourceLocations> NativeCodeMap;

NativeCodeMap  ReportedDebugFuncs;

int NotifyEvent(iJIT_JVM_EVENT EventType, void *EventSpecificData) {
  switch (EventType) {
    case iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED: {
      if (!EventSpecificData) {
        errs() <<
          "Error: The JIT event listener did not provide a event data.";
        return -1;
      }
      iJIT_Method_Load* msg = static_cast<iJIT_Method_Load*>(EventSpecificData);

      ReportedDebugFuncs[msg->method_id];

      outs() << "Method load [" << msg->method_id << "]: " << msg->method_name
             << ", Size = " << msg->method_size << "\n";

      for(unsigned int i = 0; i < msg->line_number_size; ++i) {
        if (!msg->line_number_table) {
          errs() << "A function with a non-zero line count had no line table.";
          return -1;
        }
        std::pair<std::string, unsigned int> loc(
          std::string(msg->source_file_name),
          msg->line_number_table[i].LineNumber);
        ReportedDebugFuncs[msg->method_id].push_back(loc);
        outs() << "  Line info @ " << msg->line_number_table[i].Offset
               << ": " << msg->source_file_name
               << ", line " << msg->line_number_table[i].LineNumber << "\n";
      }
      outs() << "\n";
    }
    break;
    case iJVM_EVENT_TYPE_METHOD_UNLOAD_START: {
      if (!EventSpecificData) {
        errs() <<
          "Error: The JIT event listener did not provide a event data.";
        return -1;
      }
      unsigned int UnloadId
        = *reinterpret_cast<unsigned int*>(EventSpecificData);
      assert(1 == ReportedDebugFuncs.erase(UnloadId));
      outs() << "Method unload [" << UnloadId << "]\n";
    }
    break;
    default:
      break;
  }
  return 0;
}

int ittNotifyInfo(IttEventType EventType, const char *Name, unsigned int Size) {
  switch (EventType) {
  case LoadBinaryModule: {
    if (!Name) {
      errs() << "Error: The IttNotify event listener did not provide a module "
                "name.";
      return -1;
    }
    outs() << "Module loaded : Name = " << Name << ", Size = " << Size << "\n";
  } break;
  case LoadBinarySection: {
    if (!Name) {
      errs() << "Error: The IttNotify event listener did not provide a section "
                "name.";
      return -1;
    }
    outs() << "Loaded section : Name = " << Name << ", Size = " << Size << "\n";
  } break;
  case UnloadBinaryModule: {
    if (!Name) {
      errs() << "Error: The IttNotify event listener did not provide a module "
                "name.";
      return -1;
    }
    outs() << "Module unloaded : Name = " << Name << ", Size = " << Size
           << "\n";
  } break;
  case UnloadBinarySection: {
    if (!Name) {
      errs() << "Error: The IttNotify event listener did not provide a section "
                "name.";
      return -1;
    }
    outs() << "Unloaded section : Name = " << Name << ", Size = " << Size
           << "\n";
  } break;
  }
  return 0;
}

iJIT_IsProfilingActiveFlags IsProfilingActive(void) {
  // for testing, pretend we have an Intel Parallel Amplifier XE 2011
  // instance attached
  return iJIT_SAMPLING_ON;
}

unsigned int GetNewMethodID(void) {
  static unsigned int id = 0;
  return ++id;
}

class JitEventListenerTest {
protected:
  void InitEE(const std::string &IRFile) {
    // If we have a native target, initialize it to ensure it is linked in and
    // usable by the JIT.
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    // Parse the bitcode...
    SMDiagnostic Err;
    std::unique_ptr<Module> TheModule(parseIRFile(IRFile, Err, Context));
    if (!TheModule) {
      errs() << Err.getMessage();
      return;
    }

    RTDyldMemoryManager *MemMgr = new SectionMemoryManager();
    if (!MemMgr) {
      errs() << "Unable to create memory manager.";
      return;
    }

    // Override the triple to generate ELF on Windows since that's supported
    Triple Tuple(TheModule->getTargetTriple());
    if (Tuple.getTriple().empty())
      Tuple.setTriple(sys::getProcessTriple());

    if (Tuple.isOSWindows() && !Tuple.isOSBinFormatELF()) {
      Tuple.setObjectFormat(Triple::ELF);
      TheModule->setTargetTriple(Tuple.getTriple());
    }

    // Compile the IR
    std::string Error;
    TheJIT.reset(EngineBuilder(std::move(TheModule))
      .setEngineKind(EngineKind::JIT)
      .setErrorStr(&Error)
      .setMCJITMemoryManager(std::unique_ptr<RTDyldMemoryManager>(MemMgr))
      .create());
    if (Error.empty() == false)
      errs() << Error;
  }

  void DestroyEE() {
    TheJIT.reset();
  }

  LLVMContext Context; // Global ownership
  std::unique_ptr<ExecutionEngine> TheJIT;

public:
  void ProcessInput(const std::string &Filename) {
    InitEE(Filename);

    std::unique_ptr<llvm::JITEventListener> Listener(
        JITEventListener::createIntelJITEventListener(new IntelJITEventsWrapper(
            NotifyEvent, ittNotifyInfo, 0, IsProfilingActive, 0, 0,
            GetNewMethodID)));

    TheJIT->RegisterJITEventListener(Listener.get());

    TheJIT->finalizeObject();

    // Destroy the JIT engine instead of unregistering to get unload events.
    DestroyEE();
  }
};



} // end anonymous namespace

static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input IR file>"),
               cl::Required);

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv, "llvm jit event listener test utility\n");

  JitEventListenerTest Test;
  Test.ProcessInput(InputFilename);
  return 0;
}
