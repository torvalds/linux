//===- EPCGenericMemoryAccess.h - Generic EPC MemoryAccess impl -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements ExecutorProcessControl::MemoryAccess by making calls to
// ExecutorProcessControl::callWrapperAsync.
//
// This simplifies the implementaton of new ExecutorProcessControl instances,
// as this implementation will always work (at the cost of some performance
// overhead for the calls).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_EPCGENERICMEMORYACCESS_H
#define LLVM_EXECUTIONENGINE_ORC_EPCGENERICMEMORYACCESS_H

#include "llvm/ExecutionEngine/Orc/Core.h"

namespace llvm {
namespace orc {

class EPCGenericMemoryAccess : public ExecutorProcessControl::MemoryAccess {
public:
  /// Function addresses for memory access.
  struct FuncAddrs {
    ExecutorAddr WriteUInt8s;
    ExecutorAddr WriteUInt16s;
    ExecutorAddr WriteUInt32s;
    ExecutorAddr WriteUInt64s;
    ExecutorAddr WriteBuffers;
    ExecutorAddr WritePointers;
  };

  /// Create an EPCGenericMemoryAccess instance from a given set of
  /// function addrs.
  EPCGenericMemoryAccess(ExecutorProcessControl &EPC, FuncAddrs FAs)
      : EPC(EPC), FAs(FAs) {}

  void writeUInt8sAsync(ArrayRef<tpctypes::UInt8Write> Ws,
                        WriteResultFn OnWriteComplete) override {
    using namespace shared;
    EPC.callSPSWrapperAsync<void(SPSSequence<SPSMemoryAccessUInt8Write>)>(
        FAs.WriteUInt8s, std::move(OnWriteComplete), Ws);
  }

  void writeUInt16sAsync(ArrayRef<tpctypes::UInt16Write> Ws,
                         WriteResultFn OnWriteComplete) override {
    using namespace shared;
    EPC.callSPSWrapperAsync<void(SPSSequence<SPSMemoryAccessUInt16Write>)>(
        FAs.WriteUInt16s, std::move(OnWriteComplete), Ws);
  }

  void writeUInt32sAsync(ArrayRef<tpctypes::UInt32Write> Ws,
                         WriteResultFn OnWriteComplete) override {
    using namespace shared;
    EPC.callSPSWrapperAsync<void(SPSSequence<SPSMemoryAccessUInt32Write>)>(
        FAs.WriteUInt32s, std::move(OnWriteComplete), Ws);
  }

  void writeUInt64sAsync(ArrayRef<tpctypes::UInt64Write> Ws,
                         WriteResultFn OnWriteComplete) override {
    using namespace shared;
    EPC.callSPSWrapperAsync<void(SPSSequence<SPSMemoryAccessUInt64Write>)>(
        FAs.WriteUInt64s, std::move(OnWriteComplete), Ws);
  }

  void writeBuffersAsync(ArrayRef<tpctypes::BufferWrite> Ws,
                         WriteResultFn OnWriteComplete) override {
    using namespace shared;
    EPC.callSPSWrapperAsync<void(SPSSequence<SPSMemoryAccessBufferWrite>)>(
        FAs.WriteBuffers, std::move(OnWriteComplete), Ws);
  }

  void writePointersAsync(ArrayRef<tpctypes::PointerWrite> Ws,
                          WriteResultFn OnWriteComplete) override {
    using namespace shared;
    EPC.callSPSWrapperAsync<void(SPSSequence<SPSMemoryAccessPointerWrite>)>(
        FAs.WritePointers, std::move(OnWriteComplete), Ws);
  }

private:
  ExecutorProcessControl &EPC;
  FuncAddrs FAs;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_EPCGENERICMEMORYACCESS_H
