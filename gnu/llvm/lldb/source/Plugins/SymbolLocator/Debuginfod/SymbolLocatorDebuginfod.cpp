//===-- SymbolLocatorDebuginfod.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolLocatorDebuginfod.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/Debuginfod/Debuginfod.h"
#include "llvm/Debuginfod/HTTPClient.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(SymbolLocatorDebuginfod)

namespace {

#define LLDB_PROPERTIES_symbollocatordebuginfod
#include "SymbolLocatorDebuginfodProperties.inc"

enum {
#define LLDB_PROPERTIES_symbollocatordebuginfod
#include "SymbolLocatorDebuginfodPropertiesEnum.inc"
};

class PluginProperties : public Properties {
public:
  static llvm::StringRef GetSettingName() {
    return SymbolLocatorDebuginfod::GetPluginNameStatic();
  }

  PluginProperties() {
    m_collection_sp = std::make_shared<OptionValueProperties>(GetSettingName());
    m_collection_sp->Initialize(g_symbollocatordebuginfod_properties);

    // We need to read the default value first to read the environment variable.
    llvm::SmallVector<llvm::StringRef> urls = llvm::getDefaultDebuginfodUrls();
    Args arg_urls{urls};
    m_collection_sp->SetPropertyAtIndexFromArgs(ePropertyServerURLs, arg_urls);

    m_collection_sp->SetValueChangedCallback(
        ePropertyServerURLs, [this] { ServerURLsChangedCallback(); });
  }

  Args GetDebugInfoDURLs() const {
    Args urls;
    m_collection_sp->GetPropertyAtIndexAsArgs(ePropertyServerURLs, urls);
    return urls;
  }

  llvm::Expected<std::string> GetCachePath() {
    OptionValueString *s =
        m_collection_sp->GetPropertyAtIndexAsOptionValueString(
            ePropertySymbolCachePath);
    // If we don't have a valid cache location, use the default one.
    if (!s || !s->GetCurrentValueAsRef().size()) {
      llvm::Expected<std::string> maybeCachePath =
          llvm::getDefaultDebuginfodCacheDirectory();
      if (!maybeCachePath)
        return maybeCachePath;
      return *maybeCachePath;
    }
    return s->GetCurrentValue();
  }

  std::chrono::milliseconds GetTimeout() const {
    std::optional<uint64_t> seconds =
        m_collection_sp->GetPropertyAtIndexAs<uint64_t>(ePropertyTimeout);
    if (seconds && *seconds != 0) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::seconds(*seconds));
    } else {
      return llvm::getDefaultDebuginfodTimeout();
    }
  }

private:
  void ServerURLsChangedCallback() {
    m_server_urls = GetDebugInfoDURLs();
    llvm::SmallVector<llvm::StringRef> dbginfod_urls;
    llvm::for_each(m_server_urls, [&](const auto &obj) {
      dbginfod_urls.push_back(obj.ref());
    });
    llvm::setDefaultDebuginfodUrls(dbginfod_urls);
  }
  // Storage for the StringRef's used within the Debuginfod library.
  Args m_server_urls;
};

} // namespace

static PluginProperties &GetGlobalPluginProperties() {
  static PluginProperties g_settings;
  return g_settings;
}

SymbolLocatorDebuginfod::SymbolLocatorDebuginfod() : SymbolLocator() {}

void SymbolLocatorDebuginfod::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(
        GetPluginNameStatic(), GetPluginDescriptionStatic(), CreateInstance,
        LocateExecutableObjectFile, LocateExecutableSymbolFile, nullptr,
        nullptr, SymbolLocatorDebuginfod::DebuggerInitialize);
    llvm::HTTPClient::initialize();
  });
}

void SymbolLocatorDebuginfod::DebuggerInitialize(Debugger &debugger) {
  if (!PluginManager::GetSettingForSymbolLocatorPlugin(
          debugger, PluginProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForSymbolLocatorPlugin(
        debugger, GetGlobalPluginProperties().GetValueProperties(),
        "Properties for the Debuginfod Symbol Locator plug-in.",
        is_global_setting);
  }
}

void SymbolLocatorDebuginfod::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
  llvm::HTTPClient::cleanup();
}

llvm::StringRef SymbolLocatorDebuginfod::GetPluginDescriptionStatic() {
  return "Debuginfod symbol locator.";
}

SymbolLocator *SymbolLocatorDebuginfod::CreateInstance() {
  return new SymbolLocatorDebuginfod();
}

static std::optional<FileSpec>
GetFileForModule(const ModuleSpec &module_spec,
                 std::function<std::string(llvm::object::BuildID)> UrlBuilder) {
  const UUID &module_uuid = module_spec.GetUUID();
  // Don't bother if we don't have a valid UUID, Debuginfod isn't available,
  // or if the 'symbols.enable-external-lookup' setting is false.
  if (!module_uuid.IsValid() || !llvm::canUseDebuginfod() ||
      !ModuleList::GetGlobalModuleListProperties().GetEnableExternalLookup())
    return {};

  // Grab LLDB's Debuginfod overrides from the
  // plugin.symbol-locator.debuginfod.* settings.
  PluginProperties &plugin_props = GetGlobalPluginProperties();
  llvm::Expected<std::string> cache_path_or_err = plugin_props.GetCachePath();
  // A cache location is *required*.
  if (!cache_path_or_err)
    return {};
  std::string cache_path = *cache_path_or_err;
  llvm::SmallVector<llvm::StringRef> debuginfod_urls =
      llvm::getDefaultDebuginfodUrls();
  std::chrono::milliseconds timeout = plugin_props.GetTimeout();

  // We're ready to ask the Debuginfod library to find our file.
  llvm::object::BuildID build_id(module_uuid.GetBytes());
  std::string url_path = UrlBuilder(build_id);
  std::string cache_key = llvm::getDebuginfodCacheKey(url_path);
  llvm::Expected<std::string> result = llvm::getCachedOrDownloadArtifact(
      cache_key, url_path, cache_path, debuginfod_urls, timeout);
  if (result)
    return FileSpec(*result);

  Log *log = GetLog(LLDBLog::Symbols);
  auto err_message = llvm::toString(result.takeError());
  LLDB_LOGV(log,
            "Debuginfod failed to download symbol artifact {0} with error {1}",
            url_path, err_message);
  return {};
}

std::optional<ModuleSpec> SymbolLocatorDebuginfod::LocateExecutableObjectFile(
    const ModuleSpec &module_spec) {
  return GetFileForModule(module_spec, llvm::getDebuginfodExecutableUrlPath);
}

std::optional<FileSpec> SymbolLocatorDebuginfod::LocateExecutableSymbolFile(
    const ModuleSpec &module_spec, const FileSpecList &default_search_paths) {
  return GetFileForModule(module_spec, llvm::getDebuginfodDebuginfoUrlPath);
}
