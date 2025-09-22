//===-- Statistics.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Statistics.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/StructuredData.h"

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

static void EmplaceSafeString(llvm::json::Object &obj, llvm::StringRef key,
                              const std::string &str) {
  if (str.empty())
    return;
  if (LLVM_LIKELY(llvm::json::isUTF8(str)))
    obj.try_emplace(key, str);
  else
    obj.try_emplace(key, llvm::json::fixUTF8(str));
}

json::Value StatsSuccessFail::ToJSON() const {
  return json::Object{{"successes", successes}, {"failures", failures}};
}

static double elapsed(const StatsTimepoint &start, const StatsTimepoint &end) {
  StatsDuration::Duration elapsed =
      end.time_since_epoch() - start.time_since_epoch();
  return elapsed.count();
}

void TargetStats::CollectStats(Target &target) {
  m_module_identifiers.clear();
  for (ModuleSP module_sp : target.GetImages().Modules())
    m_module_identifiers.emplace_back((intptr_t)module_sp.get());
}

json::Value ModuleStats::ToJSON() const {
  json::Object module;
  EmplaceSafeString(module, "path", path);
  EmplaceSafeString(module, "uuid", uuid);
  EmplaceSafeString(module, "triple", triple);
  module.try_emplace("identifier", identifier);
  module.try_emplace("symbolTableParseTime", symtab_parse_time);
  module.try_emplace("symbolTableIndexTime", symtab_index_time);
  module.try_emplace("symbolTableLoadedFromCache", symtab_loaded_from_cache);
  module.try_emplace("symbolTableSavedToCache", symtab_saved_to_cache);
  module.try_emplace("debugInfoParseTime", debug_parse_time);
  module.try_emplace("debugInfoIndexTime", debug_index_time);
  module.try_emplace("debugInfoByteSize", (int64_t)debug_info_size);
  module.try_emplace("debugInfoIndexLoadedFromCache",
                     debug_info_index_loaded_from_cache);
  module.try_emplace("debugInfoIndexSavedToCache",
                     debug_info_index_saved_to_cache);
  module.try_emplace("debugInfoEnabled", debug_info_enabled);
  module.try_emplace("debugInfoHadVariableErrors",
                     debug_info_had_variable_errors);
  module.try_emplace("debugInfoHadIncompleteTypes",
                     debug_info_had_incomplete_types);
  module.try_emplace("symbolTableStripped", symtab_stripped);
  if (!symfile_path.empty())
    module.try_emplace("symbolFilePath", symfile_path);

  if (!symfile_modules.empty()) {
    json::Array symfile_ids;
    for (const auto symfile_id: symfile_modules)
      symfile_ids.emplace_back(symfile_id);
    module.try_emplace("symbolFileModuleIdentifiers", std::move(symfile_ids));
  }

  if (!type_system_stats.empty()) {
    json::Array type_systems;
    for (const auto &entry : type_system_stats) {
      json::Object obj;
      obj.try_emplace(entry.first().str(), entry.second);
      type_systems.emplace_back(std::move(obj));
    }
    module.try_emplace("typeSystemInfo", std::move(type_systems));
  }

  return module;
}

llvm::json::Value ConstStringStats::ToJSON() const {
  json::Object obj;
  obj.try_emplace<int64_t>("bytesTotal", stats.GetBytesTotal());
  obj.try_emplace<int64_t>("bytesUsed", stats.GetBytesUsed());
  obj.try_emplace<int64_t>("bytesUnused", stats.GetBytesUnused());
  return obj;
}

json::Value
TargetStats::ToJSON(Target &target,
                    const lldb_private::StatisticsOptions &options) {
  json::Object target_metrics_json;
  ProcessSP process_sp = target.GetProcessSP();
  const bool summary_only = options.GetSummaryOnly();
  const bool include_modules = options.GetIncludeModules();
  if (!summary_only) {
    CollectStats(target);

    json::Array json_module_uuid_array;
    for (auto module_identifier : m_module_identifiers)
      json_module_uuid_array.emplace_back(module_identifier);

    target_metrics_json.try_emplace(m_expr_eval.name, m_expr_eval.ToJSON());
    target_metrics_json.try_emplace(m_frame_var.name, m_frame_var.ToJSON());
    if (include_modules)
      target_metrics_json.try_emplace("moduleIdentifiers",
                                      std::move(json_module_uuid_array));

    if (m_launch_or_attach_time && m_first_private_stop_time) {
      double elapsed_time =
          elapsed(*m_launch_or_attach_time, *m_first_private_stop_time);
      target_metrics_json.try_emplace("launchOrAttachTime", elapsed_time);
    }
    if (m_launch_or_attach_time && m_first_public_stop_time) {
      double elapsed_time =
          elapsed(*m_launch_or_attach_time, *m_first_public_stop_time);
      target_metrics_json.try_emplace("firstStopTime", elapsed_time);
    }
    target_metrics_json.try_emplace("targetCreateTime",
                                    m_create_time.get().count());

    json::Array breakpoints_array;
    double totalBreakpointResolveTime = 0.0;
    // Report both the normal breakpoint list and the internal breakpoint list.
    for (int i = 0; i < 2; ++i) {
      BreakpointList &breakpoints = target.GetBreakpointList(i == 1);
      std::unique_lock<std::recursive_mutex> lock;
      breakpoints.GetListMutex(lock);
      size_t num_breakpoints = breakpoints.GetSize();
      for (size_t i = 0; i < num_breakpoints; i++) {
        Breakpoint *bp = breakpoints.GetBreakpointAtIndex(i).get();
        breakpoints_array.push_back(bp->GetStatistics());
        totalBreakpointResolveTime += bp->GetResolveTime().count();
      }
    }
    target_metrics_json.try_emplace("breakpoints",
                                    std::move(breakpoints_array));
    target_metrics_json.try_emplace("totalBreakpointResolveTime",
                                    totalBreakpointResolveTime);

    if (process_sp) {
      UnixSignalsSP unix_signals_sp = process_sp->GetUnixSignals();
      if (unix_signals_sp)
        target_metrics_json.try_emplace(
            "signals", unix_signals_sp->GetHitCountStatistics());
    }
  }

  // Counting "totalSharedLibraryEventHitCount" from breakpoints of kind
  // "shared-library-event".
  {
    uint32_t shared_library_event_breakpoint_hit_count = 0;
    // The "shared-library-event" is only found in the internal breakpoint list.
    BreakpointList &breakpoints = target.GetBreakpointList(/* internal */ true);
    std::unique_lock<std::recursive_mutex> lock;
    breakpoints.GetListMutex(lock);
    size_t num_breakpoints = breakpoints.GetSize();
    for (size_t i = 0; i < num_breakpoints; i++) {
      Breakpoint *bp = breakpoints.GetBreakpointAtIndex(i).get();
      if (strcmp(bp->GetBreakpointKind(), "shared-library-event") == 0)
        shared_library_event_breakpoint_hit_count += bp->GetHitCount();
    }

    target_metrics_json.try_emplace("totalSharedLibraryEventHitCount",
                                    shared_library_event_breakpoint_hit_count);
  }

  if (process_sp) {
    uint32_t stop_id = process_sp->GetStopID();
    target_metrics_json.try_emplace("stopCount", stop_id);

    llvm::StringRef dyld_plugin_name;
    if (process_sp->GetDynamicLoader())
      dyld_plugin_name = process_sp->GetDynamicLoader()->GetPluginName();
    target_metrics_json.try_emplace("dyldPluginName", dyld_plugin_name);
  }
  target_metrics_json.try_emplace("sourceMapDeduceCount",
                                  m_source_map_deduce_count);
  return target_metrics_json;
}

void TargetStats::SetLaunchOrAttachTime() {
  m_launch_or_attach_time = StatsClock::now();
  m_first_private_stop_time = std::nullopt;
}

void TargetStats::SetFirstPrivateStopTime() {
  // Launching and attaching has many paths depending on if synchronous mode
  // was used or if we are stopping at the entry point or not. Only set the
  // first stop time if it hasn't already been set.
  if (!m_first_private_stop_time)
    m_first_private_stop_time = StatsClock::now();
}

void TargetStats::SetFirstPublicStopTime() {
  // Launching and attaching has many paths depending on if synchronous mode
  // was used or if we are stopping at the entry point or not. Only set the
  // first stop time if it hasn't already been set.
  if (!m_first_public_stop_time)
    m_first_public_stop_time = StatsClock::now();
}

void TargetStats::IncreaseSourceMapDeduceCount() {
  ++m_source_map_deduce_count;
}

bool DebuggerStats::g_collecting_stats = false;

llvm::json::Value DebuggerStats::ReportStatistics(
    Debugger &debugger, Target *target,
    const lldb_private::StatisticsOptions &options) {

  const bool summary_only = options.GetSummaryOnly();
  const bool load_all_debug_info = options.GetLoadAllDebugInfo();
  const bool include_targets = options.GetIncludeTargets();
  const bool include_modules = options.GetIncludeModules();
  const bool include_transcript = options.GetIncludeTranscript();

  json::Array json_targets;
  json::Array json_modules;
  double symtab_parse_time = 0.0;
  double symtab_index_time = 0.0;
  double debug_parse_time = 0.0;
  double debug_index_time = 0.0;
  uint32_t symtabs_loaded = 0;
  uint32_t symtabs_saved = 0;
  uint32_t debug_index_loaded = 0;
  uint32_t debug_index_saved = 0;
  uint64_t debug_info_size = 0;

  std::vector<ModuleStats> modules;
  std::lock_guard<std::recursive_mutex> guard(
      Module::GetAllocationModuleCollectionMutex());
  const uint64_t num_modules = Module::GetNumberAllocatedModules();
  uint32_t num_debug_info_enabled_modules = 0;
  uint32_t num_modules_has_debug_info = 0;
  uint32_t num_modules_with_variable_errors = 0;
  uint32_t num_modules_with_incomplete_types = 0;
  uint32_t num_stripped_modules = 0;
  for (size_t image_idx = 0; image_idx < num_modules; ++image_idx) {
    Module *module = Module::GetAllocatedModuleAtIndex(image_idx);
    ModuleStats module_stat;
    module_stat.symtab_parse_time = module->GetSymtabParseTime().get().count();
    module_stat.symtab_index_time = module->GetSymtabIndexTime().get().count();
    Symtab *symtab = module->GetSymtab();
    if (symtab) {
      module_stat.symtab_loaded_from_cache = symtab->GetWasLoadedFromCache();
      if (module_stat.symtab_loaded_from_cache)
        ++symtabs_loaded;
      module_stat.symtab_saved_to_cache = symtab->GetWasSavedToCache();
      if (module_stat.symtab_saved_to_cache)
        ++symtabs_saved;
    }
    SymbolFile *sym_file = module->GetSymbolFile();
    if (sym_file) {
      if (!summary_only) {
        if (sym_file->GetObjectFile() != module->GetObjectFile())
          module_stat.symfile_path =
              sym_file->GetObjectFile()->GetFileSpec().GetPath();
        ModuleList symbol_modules = sym_file->GetDebugInfoModules();
        for (const auto &symbol_module : symbol_modules.Modules())
          module_stat.symfile_modules.push_back((intptr_t)symbol_module.get());
      }
      module_stat.debug_info_index_loaded_from_cache =
          sym_file->GetDebugInfoIndexWasLoadedFromCache();
      if (module_stat.debug_info_index_loaded_from_cache)
        ++debug_index_loaded;
      module_stat.debug_info_index_saved_to_cache =
          sym_file->GetDebugInfoIndexWasSavedToCache();
      if (module_stat.debug_info_index_saved_to_cache)
        ++debug_index_saved;
      module_stat.debug_index_time = sym_file->GetDebugInfoIndexTime().count();
      module_stat.debug_parse_time = sym_file->GetDebugInfoParseTime().count();
      module_stat.debug_info_size =
          sym_file->GetDebugInfoSize(load_all_debug_info);
      module_stat.symtab_stripped = module->GetObjectFile()->IsStripped();
      if (module_stat.symtab_stripped)
        ++num_stripped_modules;
      module_stat.debug_info_enabled = sym_file->GetLoadDebugInfoEnabled() &&
                                       module_stat.debug_info_size > 0;
      module_stat.debug_info_had_variable_errors =
          sym_file->GetDebugInfoHadFrameVariableErrors();
      if (module_stat.debug_info_enabled)
        ++num_debug_info_enabled_modules;
      if (module_stat.debug_info_size > 0)
        ++num_modules_has_debug_info;
      if (module_stat.debug_info_had_variable_errors)
        ++num_modules_with_variable_errors;
    }
    symtab_parse_time += module_stat.symtab_parse_time;
    symtab_index_time += module_stat.symtab_index_time;
    debug_parse_time += module_stat.debug_parse_time;
    debug_index_time += module_stat.debug_index_time;
    debug_info_size += module_stat.debug_info_size;
    module->ForEachTypeSystem([&](lldb::TypeSystemSP ts) {
      if (auto stats = ts->ReportStatistics())
        module_stat.type_system_stats.insert({ts->GetPluginName(), *stats});
      if (ts->GetHasForcefullyCompletedTypes())
        module_stat.debug_info_had_incomplete_types = true;
      return true;
    });
    if (module_stat.debug_info_had_incomplete_types)
      ++num_modules_with_incomplete_types;

    if (include_modules) {
      module_stat.identifier = (intptr_t)module;
      module_stat.path = module->GetFileSpec().GetPath();
      if (ConstString object_name = module->GetObjectName()) {
        module_stat.path.append(1, '(');
        module_stat.path.append(object_name.GetStringRef().str());
        module_stat.path.append(1, ')');
      }
      module_stat.uuid = module->GetUUID().GetAsString();
      module_stat.triple = module->GetArchitecture().GetTriple().str();
      json_modules.emplace_back(module_stat.ToJSON());
    }
  }

  json::Object global_stats{
      {"totalSymbolTableParseTime", symtab_parse_time},
      {"totalSymbolTableIndexTime", symtab_index_time},
      {"totalSymbolTablesLoadedFromCache", symtabs_loaded},
      {"totalSymbolTablesSavedToCache", symtabs_saved},
      {"totalDebugInfoParseTime", debug_parse_time},
      {"totalDebugInfoIndexTime", debug_index_time},
      {"totalDebugInfoIndexLoadedFromCache", debug_index_loaded},
      {"totalDebugInfoIndexSavedToCache", debug_index_saved},
      {"totalDebugInfoByteSize", debug_info_size},
      {"totalModuleCount", num_modules},
      {"totalModuleCountHasDebugInfo", num_modules_has_debug_info},
      {"totalModuleCountWithVariableErrors", num_modules_with_variable_errors},
      {"totalModuleCountWithIncompleteTypes",
       num_modules_with_incomplete_types},
      {"totalDebugInfoEnabled", num_debug_info_enabled_modules},
      {"totalSymbolTableStripped", num_stripped_modules},
  };

  if (include_targets) {
    if (target) {
      json_targets.emplace_back(target->ReportStatistics(options));
    } else {
      for (const auto &target : debugger.GetTargetList().Targets())
        json_targets.emplace_back(target->ReportStatistics(options));
    }
    global_stats.try_emplace("targets", std::move(json_targets));
  }

  ConstStringStats const_string_stats;
  json::Object json_memory{
      {"strings", const_string_stats.ToJSON()},
  };
  global_stats.try_emplace("memory", std::move(json_memory));
  if (!summary_only) {
    json::Value cmd_stats = debugger.GetCommandInterpreter().GetStatistics();
    global_stats.try_emplace("commands", std::move(cmd_stats));
  }

  if (include_modules) {
    global_stats.try_emplace("modules", std::move(json_modules));
  }

  if (include_transcript) {
    // When transcript is available, add it to the to-be-returned statistics.
    //
    // NOTE:
    // When the statistics is polled by an LLDB command:
    // - The transcript in the returned statistics *will NOT* contain the
    //   returned statistics itself (otherwise infinite recursion).
    // - The returned statistics *will* be written to the internal transcript
    //   buffer. It *will* appear in the next statistcs or transcript poll.
    //
    // For example, let's say the following commands are run in order:
    // - "version"
    // - "statistics dump"  <- call it "A"
    // - "statistics dump"  <- call it "B"
    // The output of "A" will contain the transcript of "version" and
    // "statistics dump" (A), with the latter having empty output. The output
    // of B will contain the trascnript of "version", "statistics dump" (A),
    // "statistics dump" (B), with A's output populated and B's output empty.
    const StructuredData::Array &transcript =
        debugger.GetCommandInterpreter().GetTranscript();
    if (transcript.GetSize() != 0) {
      std::string buffer;
      llvm::raw_string_ostream ss(buffer);
      json::OStream json_os(ss);
      transcript.Serialize(json_os);
      if (auto json_transcript = llvm::json::parse(ss.str()))
        global_stats.try_emplace("transcript",
                                 std::move(json_transcript.get()));
    }
  }

  return std::move(global_stats);
}
