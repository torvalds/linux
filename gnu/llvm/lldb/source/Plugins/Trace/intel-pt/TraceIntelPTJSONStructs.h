//===-- TraceIntelPTJSONStructs.h -----------------------------*- C++ //-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTJSONSTRUCTS_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTJSONSTRUCTS_H

#include "lldb/Utility/TraceIntelPTGDBRemotePackets.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/JSON.h"
#include <intel-pt.h>
#include <optional>
#include <vector>

namespace lldb_private {
namespace trace_intel_pt {

struct JSONModule {
  std::string system_path;
  std::optional<std::string> file;
  JSONUINT64 load_address;
  std::optional<std::string> uuid;
};

struct JSONThread {
  uint64_t tid;
  std::optional<std::string> ipt_trace;
};

struct JSONProcess {
  uint64_t pid;
  std::optional<std::string> triple;
  std::vector<JSONThread> threads;
  std::vector<JSONModule> modules;
};

struct JSONCpu {
  lldb::cpu_id_t id;
  std::string ipt_trace;
  std::string context_switch_trace;
};

struct JSONKernel {
  std::optional<JSONUINT64> load_address;
  std::string file;
};

struct JSONTraceBundleDescription {
  std::string type;
  pt_cpu cpu_info;
  std::optional<std::vector<JSONProcess>> processes;
  std::optional<std::vector<JSONCpu>> cpus;
  std::optional<LinuxPerfZeroTscConversion> tsc_perf_zero_conversion;
  std::optional<JSONKernel> kernel;

  std::optional<std::vector<lldb::cpu_id_t>> GetCpuIds();
};

llvm::json::Value toJSON(const JSONModule &module);

llvm::json::Value toJSON(const JSONThread &thread);

llvm::json::Value toJSON(const JSONProcess &process);

llvm::json::Value toJSON(const JSONCpu &cpu);

llvm::json::Value toJSON(const pt_cpu &cpu_info);

llvm::json::Value toJSON(const JSONKernel &kernel);

llvm::json::Value toJSON(const JSONTraceBundleDescription &bundle_description);

bool fromJSON(const llvm::json::Value &value, JSONModule &module,
              llvm::json::Path path);

bool fromJSON(const llvm::json::Value &value, JSONThread &thread,
              llvm::json::Path path);

bool fromJSON(const llvm::json::Value &value, JSONProcess &process,
              llvm::json::Path path);

bool fromJSON(const llvm::json::Value &value, JSONCpu &cpu,
              llvm::json::Path path);

bool fromJSON(const llvm::json::Value &value, pt_cpu &cpu_info,
              llvm::json::Path path);

bool fromJSON(const llvm::json::Value &value, JSONModule &kernel,
              llvm::json::Path path);

bool fromJSON(const llvm::json::Value &value,
              JSONTraceBundleDescription &bundle_description,
              llvm::json::Path path);
} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTJSONSTRUCTS_H
