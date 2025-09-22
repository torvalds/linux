//===-- TraceIntelPTGDBRemotePackets.cpp ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/TraceIntelPTGDBRemotePackets.h"

using namespace llvm;
using namespace llvm::json;

namespace lldb_private {

const char *IntelPTDataKinds::kProcFsCpuInfo = "procfsCpuInfo";
const char *IntelPTDataKinds::kIptTrace = "iptTrace";
const char *IntelPTDataKinds::kPerfContextSwitchTrace =
    "perfContextSwitchTrace";

bool TraceIntelPTStartRequest::IsPerCpuTracing() const {
  return per_cpu_tracing.value_or(false);
}

json::Value toJSON(const JSONUINT64 &uint64, bool hex) {
  if (hex)
    return json::Value(formatv("{0:x+}", uint64.value));
  else
    return json::Value(formatv("{0}", uint64.value));
}

bool fromJSON(const json::Value &value, JSONUINT64 &uint64, Path path) {
  if (std::optional<uint64_t> val = value.getAsUINT64()) {
    uint64.value = *val;
    return true;
  } else if (std::optional<StringRef> val = value.getAsString()) {
    if (!val->getAsInteger(/*radix=*/0, uint64.value))
      return true;
    path.report("invalid string number");
  }
  path.report("invalid number or string number");
  return false;
}

bool fromJSON(const json::Value &value, TraceIntelPTStartRequest &packet,
              Path path) {
  ObjectMapper o(value, path);
  if (!(o && fromJSON(value, (TraceStartRequest &)packet, path) &&
        o.map("enableTsc", packet.enable_tsc) &&
        o.map("psbPeriod", packet.psb_period) &&
        o.map("iptTraceSize", packet.ipt_trace_size)))
    return false;

  if (packet.IsProcessTracing()) {
    if (!o.map("processBufferSizeLimit", packet.process_buffer_size_limit) ||
        !o.map("perCpuTracing", packet.per_cpu_tracing) ||
        !o.map("disableCgroupTracing", packet.disable_cgroup_filtering))
      return false;
  }
  return true;
}

json::Value toJSON(const TraceIntelPTStartRequest &packet) {
  json::Value base = toJSON((const TraceStartRequest &)packet);
  json::Object &obj = *base.getAsObject();
  obj.try_emplace("iptTraceSize", packet.ipt_trace_size);
  obj.try_emplace("processBufferSizeLimit", packet.process_buffer_size_limit);
  obj.try_emplace("psbPeriod", packet.psb_period);
  obj.try_emplace("enableTsc", packet.enable_tsc);
  obj.try_emplace("perCpuTracing", packet.per_cpu_tracing);
  obj.try_emplace("disableCgroupTracing", packet.disable_cgroup_filtering);
  return base;
}

uint64_t LinuxPerfZeroTscConversion::ToNanos(uint64_t tsc) const {
  uint64_t quot = tsc >> time_shift;
  uint64_t rem_flag = (((uint64_t)1 << time_shift) - 1);
  uint64_t rem = tsc & rem_flag;
  return time_zero.value + quot * time_mult + ((rem * time_mult) >> time_shift);
}

uint64_t LinuxPerfZeroTscConversion::ToTSC(uint64_t nanos) const {
  uint64_t time = nanos - time_zero.value;
  uint64_t quot = time / time_mult;
  uint64_t rem = time % time_mult;
  return (quot << time_shift) + (rem << time_shift) / time_mult;
}

json::Value toJSON(const LinuxPerfZeroTscConversion &packet) {
  return json::Value(json::Object{
      {"timeMult", packet.time_mult},
      {"timeShift", packet.time_shift},
      {"timeZero", toJSON(packet.time_zero, /*hex=*/false)},
  });
}

bool fromJSON(const json::Value &value, LinuxPerfZeroTscConversion &packet,
              json::Path path) {
  ObjectMapper o(value, path);
  uint64_t time_mult, time_shift;
  if (!(o && o.map("timeMult", time_mult) && o.map("timeShift", time_shift) &&
        o.map("timeZero", packet.time_zero)))
    return false;
  packet.time_mult = time_mult;
  packet.time_shift = time_shift;
  return true;
}

bool fromJSON(const json::Value &value, TraceIntelPTGetStateResponse &packet,
              json::Path path) {
  ObjectMapper o(value, path);
  return o && fromJSON(value, (TraceGetStateResponse &)packet, path) &&
         o.map("tscPerfZeroConversion", packet.tsc_perf_zero_conversion) &&
         o.map("usingCgroupFiltering", packet.using_cgroup_filtering);
}

json::Value toJSON(const TraceIntelPTGetStateResponse &packet) {
  json::Value base = toJSON((const TraceGetStateResponse &)packet);
  json::Object &obj = *base.getAsObject();
  obj.insert({"tscPerfZeroConversion", packet.tsc_perf_zero_conversion});
  obj.insert({"usingCgroupFiltering", packet.using_cgroup_filtering});
  return base;
}

} // namespace lldb_private
