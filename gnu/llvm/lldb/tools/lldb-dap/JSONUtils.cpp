//===-- JSONUtils.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string.h>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"

#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBBreakpointLocation.h"
#include "lldb/API/SBDeclaration.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBValue.h"
#include "lldb/Host/PosixApi.h"

#include "DAP.h"
#include "ExceptionBreakpoint.h"
#include "JSONUtils.h"
#include "LLDBUtils.h"

namespace lldb_dap {

void EmplaceSafeString(llvm::json::Object &obj, llvm::StringRef key,
                       llvm::StringRef str) {
  if (LLVM_LIKELY(llvm::json::isUTF8(str)))
    obj.try_emplace(key, str.str());
  else
    obj.try_emplace(key, llvm::json::fixUTF8(str));
}

llvm::StringRef GetAsString(const llvm::json::Value &value) {
  if (auto s = value.getAsString())
    return *s;
  return llvm::StringRef();
}

// Gets a string from a JSON object using the key, or returns an empty string.
llvm::StringRef GetString(const llvm::json::Object &obj, llvm::StringRef key,
                          llvm::StringRef defaultValue) {
  if (std::optional<llvm::StringRef> value = obj.getString(key))
    return *value;
  return defaultValue;
}

llvm::StringRef GetString(const llvm::json::Object *obj, llvm::StringRef key,
                          llvm::StringRef defaultValue) {
  if (obj == nullptr)
    return defaultValue;
  return GetString(*obj, key, defaultValue);
}

// Gets an unsigned integer from a JSON object using the key, or returns the
// specified fail value.
uint64_t GetUnsigned(const llvm::json::Object &obj, llvm::StringRef key,
                     uint64_t fail_value) {
  if (auto value = obj.getInteger(key))
    return (uint64_t)*value;
  return fail_value;
}

uint64_t GetUnsigned(const llvm::json::Object *obj, llvm::StringRef key,
                     uint64_t fail_value) {
  if (obj == nullptr)
    return fail_value;
  return GetUnsigned(*obj, key, fail_value);
}

bool GetBoolean(const llvm::json::Object &obj, llvm::StringRef key,
                bool fail_value) {
  if (auto value = obj.getBoolean(key))
    return *value;
  if (auto value = obj.getInteger(key))
    return *value != 0;
  return fail_value;
}

bool GetBoolean(const llvm::json::Object *obj, llvm::StringRef key,
                bool fail_value) {
  if (obj == nullptr)
    return fail_value;
  return GetBoolean(*obj, key, fail_value);
}

int64_t GetSigned(const llvm::json::Object &obj, llvm::StringRef key,
                  int64_t fail_value) {
  if (auto value = obj.getInteger(key))
    return *value;
  return fail_value;
}

int64_t GetSigned(const llvm::json::Object *obj, llvm::StringRef key,
                  int64_t fail_value) {
  if (obj == nullptr)
    return fail_value;
  return GetSigned(*obj, key, fail_value);
}

bool ObjectContainsKey(const llvm::json::Object &obj, llvm::StringRef key) {
  return obj.find(key) != obj.end();
}

std::vector<std::string> GetStrings(const llvm::json::Object *obj,
                                    llvm::StringRef key) {
  std::vector<std::string> strs;
  auto json_array = obj->getArray(key);
  if (!json_array)
    return strs;
  for (const auto &value : *json_array) {
    switch (value.kind()) {
    case llvm::json::Value::String:
      strs.push_back(value.getAsString()->str());
      break;
    case llvm::json::Value::Number:
    case llvm::json::Value::Boolean:
      strs.push_back(llvm::to_string(value));
      break;
    case llvm::json::Value::Null:
    case llvm::json::Value::Object:
    case llvm::json::Value::Array:
      break;
    }
  }
  return strs;
}

static bool IsClassStructOrUnionType(lldb::SBType t) {
  return (t.GetTypeClass() & (lldb::eTypeClassUnion | lldb::eTypeClassStruct |
                              lldb::eTypeClassArray)) != 0;
}

/// Create a short summary for a container that contains the summary of its
/// first children, so that the user can get a glimpse of its contents at a
/// glance.
static std::optional<std::string>
TryCreateAutoSummaryForContainer(lldb::SBValue &v) {
  // We gate this feature because it performs GetNumChildren(), which can
  // cause performance issues because LLDB needs to complete possibly huge
  // types.
  if (!g_dap.enable_auto_variable_summaries)
    return std::nullopt;

  if (!v.MightHaveChildren())
    return std::nullopt;
  /// As this operation can be potentially slow, we limit the total time spent
  /// fetching children to a few ms.
  const auto max_evaluation_time = std::chrono::milliseconds(10);
  /// We don't want to generate a extremely long summary string, so we limit its
  /// length.
  const size_t max_length = 32;

  auto start = std::chrono::steady_clock::now();
  std::string summary;
  llvm::raw_string_ostream os(summary);
  os << "{";

  llvm::StringRef separator = "";

  for (size_t i = 0, e = v.GetNumChildren(); i < e; ++i) {
    // If we reached the time limit or exceeded the number of characters, we
    // dump `...` to signal that there are more elements in the collection.
    if (summary.size() > max_length ||
        (std::chrono::steady_clock::now() - start) > max_evaluation_time) {
      os << separator << "...";
      break;
    }
    lldb::SBValue child = v.GetChildAtIndex(i);

    if (llvm::StringRef name = child.GetName(); !name.empty()) {
      llvm::StringRef desc;
      if (llvm::StringRef summary = child.GetSummary(); !summary.empty())
        desc = summary;
      else if (llvm::StringRef value = child.GetValue(); !value.empty())
        desc = value;
      else if (IsClassStructOrUnionType(child.GetType()))
        desc = "{...}";
      else
        continue;

      // If the child is an indexed entry, we don't show its index to save
      // characters.
      if (name.starts_with("["))
        os << separator << desc;
      else
        os << separator << name << ":" << desc;
      separator = ", ";
    }
  }
  os << "}";

  if (summary == "{...}" || summary == "{}")
    return std::nullopt;
  return summary;
}

/// Try to create a summary string for the given value that doesn't have a
/// summary of its own.
static std::optional<std::string> TryCreateAutoSummary(lldb::SBValue value) {
  if (!g_dap.enable_auto_variable_summaries)
    return std::nullopt;

  // We use the dereferenced value for generating the summary.
  if (value.GetType().IsPointerType() || value.GetType().IsReferenceType())
    value = value.Dereference();

  // We only support auto summaries for containers.
  return TryCreateAutoSummaryForContainer(value);
}

void FillResponse(const llvm::json::Object &request,
                  llvm::json::Object &response) {
  // Fill in all of the needed response fields to a "request" and set "success"
  // to true by default.
  response.try_emplace("type", "response");
  response.try_emplace("seq", (int64_t)0);
  EmplaceSafeString(response, "command", GetString(request, "command"));
  const int64_t seq = GetSigned(request, "seq", 0);
  response.try_emplace("request_seq", seq);
  response.try_emplace("success", true);
}

// "Scope": {
//   "type": "object",
//   "description": "A Scope is a named container for variables. Optionally
//                   a scope can map to a source or a range within a source.",
//   "properties": {
//     "name": {
//       "type": "string",
//       "description": "Name of the scope such as 'Arguments', 'Locals'."
//     },
//     "presentationHint": {
//       "type": "string",
//       "description": "An optional hint for how to present this scope in the
//                       UI. If this attribute is missing, the scope is shown
//                       with a generic UI.",
//       "_enum": [ "arguments", "locals", "registers" ],
//     },
//     "variablesReference": {
//       "type": "integer",
//       "description": "The variables of this scope can be retrieved by
//                       passing the value of variablesReference to the
//                       VariablesRequest."
//     },
//     "namedVariables": {
//       "type": "integer",
//       "description": "The number of named variables in this scope. The
//                       client can use this optional information to present
//                       the variables in a paged UI and fetch them in chunks."
//     },
//     "indexedVariables": {
//       "type": "integer",
//       "description": "The number of indexed variables in this scope. The
//                       client can use this optional information to present
//                       the variables in a paged UI and fetch them in chunks."
//     },
//     "expensive": {
//       "type": "boolean",
//       "description": "If true, the number of variables in this scope is
//                       large or expensive to retrieve."
//     },
//     "source": {
//       "$ref": "#/definitions/Source",
//       "description": "Optional source for this scope."
//     },
//     "line": {
//       "type": "integer",
//       "description": "Optional start line of the range covered by this
//                       scope."
//     },
//     "column": {
//       "type": "integer",
//       "description": "Optional start column of the range covered by this
//                       scope."
//     },
//     "endLine": {
//       "type": "integer",
//       "description": "Optional end line of the range covered by this scope."
//     },
//     "endColumn": {
//       "type": "integer",
//       "description": "Optional end column of the range covered by this
//                       scope."
//     }
//   },
//   "required": [ "name", "variablesReference", "expensive" ]
// }
llvm::json::Value CreateScope(const llvm::StringRef name,
                              int64_t variablesReference,
                              int64_t namedVariables, bool expensive) {
  llvm::json::Object object;
  EmplaceSafeString(object, "name", name.str());

  // TODO: Support "arguments" scope. At the moment lldb-dap includes the
  // arguments into the "locals" scope.
  if (variablesReference == VARREF_LOCALS) {
    object.try_emplace("presentationHint", "locals");
  } else if (variablesReference == VARREF_REGS) {
    object.try_emplace("presentationHint", "registers");
  }

  object.try_emplace("variablesReference", variablesReference);
  object.try_emplace("expensive", expensive);
  object.try_emplace("namedVariables", namedVariables);
  return llvm::json::Value(std::move(object));
}

// "Breakpoint": {
//   "type": "object",
//   "description": "Information about a Breakpoint created in setBreakpoints
//                   or setFunctionBreakpoints.",
//   "properties": {
//     "id": {
//       "type": "integer",
//       "description": "An optional unique identifier for the breakpoint."
//     },
//     "verified": {
//       "type": "boolean",
//       "description": "If true breakpoint could be set (but not necessarily
//                       at the desired location)."
//     },
//     "message": {
//       "type": "string",
//       "description": "An optional message about the state of the breakpoint.
//                       This is shown to the user and can be used to explain
//                       why a breakpoint could not be verified."
//     },
//     "source": {
//       "$ref": "#/definitions/Source",
//       "description": "The source where the breakpoint is located."
//     },
//     "line": {
//       "type": "integer",
//       "description": "The start line of the actual range covered by the
//                       breakpoint."
//     },
//     "column": {
//       "type": "integer",
//       "description": "An optional start column of the actual range covered
//                       by the breakpoint."
//     },
//     "endLine": {
//       "type": "integer",
//       "description": "An optional end line of the actual range covered by
//                       the breakpoint."
//     },
//     "endColumn": {
//       "type": "integer",
//       "description": "An optional end column of the actual range covered by
//                       the breakpoint. If no end line is given, then the end
//                       column is assumed to be in the start line."
//     }
//   },
//   "required": [ "verified" ]
// }
llvm::json::Value CreateBreakpoint(BreakpointBase *bp,
                                   std::optional<llvm::StringRef> request_path,
                                   std::optional<uint32_t> request_line,
                                   std::optional<uint32_t> request_column) {
  llvm::json::Object object;
  if (request_path)
    object.try_emplace("source", CreateSource(*request_path));
  bp->CreateJsonObject(object);
  // We try to add request_line as a fallback
  if (request_line)
    object.try_emplace("line", *request_line);
  if (request_column)
    object.try_emplace("column", *request_column);
  return llvm::json::Value(std::move(object));
}

static uint64_t GetDebugInfoSizeInSection(lldb::SBSection section) {
  uint64_t debug_info_size = 0;
  llvm::StringRef section_name(section.GetName());
  if (section_name.starts_with(".debug") ||
      section_name.starts_with("__debug") ||
      section_name.starts_with(".apple") || section_name.starts_with("__apple"))
    debug_info_size += section.GetFileByteSize();
  size_t num_sub_sections = section.GetNumSubSections();
  for (size_t i = 0; i < num_sub_sections; i++) {
    debug_info_size +=
        GetDebugInfoSizeInSection(section.GetSubSectionAtIndex(i));
  }
  return debug_info_size;
}

static uint64_t GetDebugInfoSize(lldb::SBModule module) {
  uint64_t debug_info_size = 0;
  size_t num_sections = module.GetNumSections();
  for (size_t i = 0; i < num_sections; i++) {
    debug_info_size += GetDebugInfoSizeInSection(module.GetSectionAtIndex(i));
  }
  return debug_info_size;
}

static std::string ConvertDebugInfoSizeToString(uint64_t debug_info) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  if (debug_info < 1024) {
    oss << debug_info << "B";
  } else if (debug_info < 1024 * 1024) {
    double kb = double(debug_info) / 1024.0;
    oss << kb << "KB";
  } else if (debug_info < 1024 * 1024 * 1024) {
    double mb = double(debug_info) / (1024.0 * 1024.0);
    oss << mb << "MB";
  } else {
    double gb = double(debug_info) / (1024.0 * 1024.0 * 1024.0);
    oss << gb << "GB";
  }
  return oss.str();
}
llvm::json::Value CreateModule(lldb::SBModule &module) {
  llvm::json::Object object;
  if (!module.IsValid())
    return llvm::json::Value(std::move(object));
  const char *uuid = module.GetUUIDString();
  object.try_emplace("id", uuid ? std::string(uuid) : std::string(""));
  object.try_emplace("name", std::string(module.GetFileSpec().GetFilename()));
  char module_path_arr[PATH_MAX];
  module.GetFileSpec().GetPath(module_path_arr, sizeof(module_path_arr));
  std::string module_path(module_path_arr);
  object.try_emplace("path", module_path);
  if (module.GetNumCompileUnits() > 0) {
    std::string symbol_str = "Symbols loaded.";
    std::string debug_info_size;
    uint64_t debug_info = GetDebugInfoSize(module);
    if (debug_info > 0) {
      debug_info_size = ConvertDebugInfoSizeToString(debug_info);
    }
    object.try_emplace("symbolStatus", symbol_str);
    object.try_emplace("debugInfoSize", debug_info_size);
    char symbol_path_arr[PATH_MAX];
    module.GetSymbolFileSpec().GetPath(symbol_path_arr,
                                       sizeof(symbol_path_arr));
    std::string symbol_path(symbol_path_arr);
    object.try_emplace("symbolFilePath", symbol_path);
  } else {
    object.try_emplace("symbolStatus", "Symbols not found.");
  }
  std::string loaded_addr = std::to_string(
      module.GetObjectFileHeaderAddress().GetLoadAddress(g_dap.target));
  object.try_emplace("addressRange", loaded_addr);
  std::string version_str;
  uint32_t version_nums[3];
  uint32_t num_versions =
      module.GetVersion(version_nums, sizeof(version_nums) / sizeof(uint32_t));
  for (uint32_t i = 0; i < num_versions; ++i) {
    if (!version_str.empty())
      version_str += ".";
    version_str += std::to_string(version_nums[i]);
  }
  if (!version_str.empty())
    object.try_emplace("version", version_str);
  return llvm::json::Value(std::move(object));
}

void AppendBreakpoint(BreakpointBase *bp, llvm::json::Array &breakpoints,
                      std::optional<llvm::StringRef> request_path,
                      std::optional<uint32_t> request_line) {
  breakpoints.emplace_back(CreateBreakpoint(bp, request_path, request_line));
}

// "Event": {
//   "allOf": [ { "$ref": "#/definitions/ProtocolMessage" }, {
//     "type": "object",
//     "description": "Server-initiated event.",
//     "properties": {
//       "type": {
//         "type": "string",
//         "enum": [ "event" ]
//       },
//       "event": {
//         "type": "string",
//         "description": "Type of event."
//       },
//       "body": {
//         "type": [ "array", "boolean", "integer", "null", "number" ,
//                   "object", "string" ],
//         "description": "Event-specific information."
//       }
//     },
//     "required": [ "type", "event" ]
//   }]
// },
// "ProtocolMessage": {
//   "type": "object",
//   "description": "Base class of requests, responses, and events.",
//   "properties": {
//         "seq": {
//           "type": "integer",
//           "description": "Sequence number."
//         },
//         "type": {
//           "type": "string",
//           "description": "Message type.",
//           "_enum": [ "request", "response", "event" ]
//         }
//   },
//   "required": [ "seq", "type" ]
// }
llvm::json::Object CreateEventObject(const llvm::StringRef event_name) {
  llvm::json::Object event;
  event.try_emplace("seq", 0);
  event.try_emplace("type", "event");
  EmplaceSafeString(event, "event", event_name);
  return event;
}

// "ExceptionBreakpointsFilter": {
//   "type": "object",
//   "description": "An ExceptionBreakpointsFilter is shown in the UI as an
//                   option for configuring how exceptions are dealt with.",
//   "properties": {
//     "filter": {
//       "type": "string",
//       "description": "The internal ID of the filter. This value is passed
//                       to the setExceptionBreakpoints request."
//     },
//     "label": {
//       "type": "string",
//       "description": "The name of the filter. This will be shown in the UI."
//     },
//     "default": {
//       "type": "boolean",
//       "description": "Initial value of the filter. If not specified a value
//                       'false' is assumed."
//     }
//   },
//   "required": [ "filter", "label" ]
// }
llvm::json::Value
CreateExceptionBreakpointFilter(const ExceptionBreakpoint &bp) {
  llvm::json::Object object;
  EmplaceSafeString(object, "filter", bp.filter);
  EmplaceSafeString(object, "label", bp.label);
  object.try_emplace("default", bp.default_value);
  return llvm::json::Value(std::move(object));
}

// "Source": {
//   "type": "object",
//   "description": "A Source is a descriptor for source code. It is returned
//                   from the debug adapter as part of a StackFrame and it is
//                   used by clients when specifying breakpoints.",
//   "properties": {
//     "name": {
//       "type": "string",
//       "description": "The short name of the source. Every source returned
//                       from the debug adapter has a name. When sending a
//                       source to the debug adapter this name is optional."
//     },
//     "path": {
//       "type": "string",
//       "description": "The path of the source to be shown in the UI. It is
//                       only used to locate and load the content of the
//                       source if no sourceReference is specified (or its
//                       value is 0)."
//     },
//     "sourceReference": {
//       "type": "number",
//       "description": "If sourceReference > 0 the contents of the source must
//                       be retrieved through the SourceRequest (even if a path
//                       is specified). A sourceReference is only valid for a
//                       session, so it must not be used to persist a source."
//     },
//     "presentationHint": {
//       "type": "string",
//       "description": "An optional hint for how to present the source in the
//                       UI. A value of 'deemphasize' can be used to indicate
//                       that the source is not available or that it is
//                       skipped on stepping.",
//       "enum": [ "normal", "emphasize", "deemphasize" ]
//     },
//     "origin": {
//       "type": "string",
//       "description": "The (optional) origin of this source: possible values
//                       'internal module', 'inlined content from source map',
//                       etc."
//     },
//     "sources": {
//       "type": "array",
//       "items": {
//         "$ref": "#/definitions/Source"
//       },
//       "description": "An optional list of sources that are related to this
//                       source. These may be the source that generated this
//                       source."
//     },
//     "adapterData": {
//       "type":["array","boolean","integer","null","number","object","string"],
//       "description": "Optional data that a debug adapter might want to loop
//                       through the client. The client should leave the data
//                       intact and persist it across sessions. The client
//                       should not interpret the data."
//     },
//     "checksums": {
//       "type": "array",
//       "items": {
//         "$ref": "#/definitions/Checksum"
//       },
//       "description": "The checksums associated with this file."
//     }
//   }
// }
llvm::json::Value CreateSource(lldb::SBLineEntry &line_entry) {
  llvm::json::Object object;
  lldb::SBFileSpec file = line_entry.GetFileSpec();
  if (file.IsValid()) {
    const char *name = file.GetFilename();
    if (name)
      EmplaceSafeString(object, "name", name);
    char path[PATH_MAX] = "";
    if (file.GetPath(path, sizeof(path)) &&
        lldb::SBFileSpec::ResolvePath(path, path, PATH_MAX)) {
      EmplaceSafeString(object, "path", std::string(path));
    }
  }
  return llvm::json::Value(std::move(object));
}

llvm::json::Value CreateSource(llvm::StringRef source_path) {
  llvm::json::Object source;
  llvm::StringRef name = llvm::sys::path::filename(source_path);
  EmplaceSafeString(source, "name", name);
  EmplaceSafeString(source, "path", source_path);
  return llvm::json::Value(std::move(source));
}

std::optional<llvm::json::Value> CreateSource(lldb::SBFrame &frame) {
  auto line_entry = frame.GetLineEntry();
  // A line entry of 0 indicates the line is compiler generated i.e. no source
  // file is associated with the frame.
  if (line_entry.GetFileSpec().IsValid() && line_entry.GetLine() != 0)
    return CreateSource(line_entry);

  return {};
}

// "StackFrame": {
//   "type": "object",
//   "description": "A Stackframe contains the source location.",
//   "properties": {
//     "id": {
//       "type": "integer",
//       "description": "An identifier for the stack frame. It must be unique
//                       across all threads. This id can be used to retrieve
//                       the scopes of the frame with the 'scopesRequest' or
//                       to restart the execution of a stackframe."
//     },
//     "name": {
//       "type": "string",
//       "description": "The name of the stack frame, typically a method name."
//     },
//     "source": {
//       "$ref": "#/definitions/Source",
//       "description": "The optional source of the frame."
//     },
//     "line": {
//       "type": "integer",
//       "description": "The line within the file of the frame. If source is
//                       null or doesn't exist, line is 0 and must be ignored."
//     },
//     "column": {
//       "type": "integer",
//       "description": "The column within the line. If source is null or
//                       doesn't exist, column is 0 and must be ignored."
//     },
//     "endLine": {
//       "type": "integer",
//       "description": "An optional end line of the range covered by the
//                       stack frame."
//     },
//     "endColumn": {
//       "type": "integer",
//       "description": "An optional end column of the range covered by the
//                       stack frame."
//     },
//     "instructionPointerReference": {
// 	     "type": "string",
// 	     "description": "A memory reference for the current instruction
// pointer
//                       in this frame."
//     },
//     "moduleId": {
//       "type": ["integer", "string"],
//       "description": "The module associated with this frame, if any."
//     },
//     "presentationHint": {
//       "type": "string",
//       "enum": [ "normal", "label", "subtle" ],
//       "description": "An optional hint for how to present this frame in
//                       the UI. A value of 'label' can be used to indicate
//                       that the frame is an artificial frame that is used
//                       as a visual label or separator. A value of 'subtle'
//                       can be used to change the appearance of a frame in
//                       a 'subtle' way."
//     }
//   },
//   "required": [ "id", "name", "line", "column" ]
// }
llvm::json::Value CreateStackFrame(lldb::SBFrame &frame) {
  llvm::json::Object object;
  int64_t frame_id = MakeDAPFrameID(frame);
  object.try_emplace("id", frame_id);

  std::string frame_name;
  lldb::SBStream stream;
  if (g_dap.frame_format &&
      frame.GetDescriptionWithFormat(g_dap.frame_format, stream).Success()) {
    frame_name = stream.GetData();

    // `function_name` can be a nullptr, which throws an error when assigned to
    // an `std::string`.
  } else if (const char *name = frame.GetDisplayFunctionName()) {
    frame_name = name;
  }

  if (frame_name.empty()) {
    // If the function name is unavailable, display the pc address as a 16-digit
    // hex string, e.g. "0x0000000000012345"
    llvm::raw_string_ostream os(frame_name);
    os << llvm::format_hex(frame.GetPC(), 18);
  }

  // We only include `[opt]` if a custom frame format is not specified.
  if (!g_dap.frame_format && frame.GetFunction().GetIsOptimized())
    frame_name += " [opt]";

  EmplaceSafeString(object, "name", frame_name);

  auto source = CreateSource(frame);

  if (source) {
    object.try_emplace("source", *source);
    auto line_entry = frame.GetLineEntry();
    auto line = line_entry.GetLine();
    if (line && line != LLDB_INVALID_LINE_NUMBER)
      object.try_emplace("line", line);
    else
      object.try_emplace("line", 0);
    auto column = line_entry.GetColumn();
    object.try_emplace("column", column);
  } else {
    object.try_emplace("line", 0);
    object.try_emplace("column", 0);
  }

  const auto pc = frame.GetPC();
  if (pc != LLDB_INVALID_ADDRESS) {
    std::string formatted_addr = "0x" + llvm::utohexstr(pc);
    object.try_emplace("instructionPointerReference", formatted_addr);
  }

  return llvm::json::Value(std::move(object));
}

// "Thread": {
//   "type": "object",
//   "description": "A Thread",
//   "properties": {
//     "id": {
//       "type": "integer",
//       "description": "Unique identifier for the thread."
//     },
//     "name": {
//       "type": "string",
//       "description": "A name of the thread."
//     }
//   },
//   "required": [ "id", "name" ]
// }
llvm::json::Value CreateThread(lldb::SBThread &thread) {
  llvm::json::Object object;
  object.try_emplace("id", (int64_t)thread.GetThreadID());
  std::string thread_str;
  lldb::SBStream stream;
  if (g_dap.thread_format &&
      thread.GetDescriptionWithFormat(g_dap.thread_format, stream).Success()) {
    thread_str = stream.GetData();
  } else {
    const char *thread_name = thread.GetName();
    const char *queue_name = thread.GetQueueName();

    if (thread_name) {
      thread_str = std::string(thread_name);
    } else if (queue_name) {
      auto kind = thread.GetQueue().GetKind();
      std::string queue_kind_label = "";
      if (kind == lldb::eQueueKindSerial) {
        queue_kind_label = " (serial)";
      } else if (kind == lldb::eQueueKindConcurrent) {
        queue_kind_label = " (concurrent)";
      }

      thread_str =
          llvm::formatv("Thread {0} Queue: {1}{2}", thread.GetIndexID(),
                        queue_name, queue_kind_label)
              .str();
    } else {
      thread_str = llvm::formatv("Thread {0}", thread.GetIndexID()).str();
    }
  }

  EmplaceSafeString(object, "name", thread_str);

  return llvm::json::Value(std::move(object));
}

// "StoppedEvent": {
//   "allOf": [ { "$ref": "#/definitions/Event" }, {
//     "type": "object",
//     "description": "Event message for 'stopped' event type. The event
//                     indicates that the execution of the debuggee has stopped
//                     due to some condition. This can be caused by a break
//                     point previously set, a stepping action has completed,
//                     by executing a debugger statement etc.",
//     "properties": {
//       "event": {
//         "type": "string",
//         "enum": [ "stopped" ]
//       },
//       "body": {
//         "type": "object",
//         "properties": {
//           "reason": {
//             "type": "string",
//             "description": "The reason for the event. For backward
//                             compatibility this string is shown in the UI if
//                             the 'description' attribute is missing (but it
//                             must not be translated).",
//             "_enum": [ "step", "breakpoint", "exception", "pause", "entry" ]
//           },
//           "description": {
//             "type": "string",
//             "description": "The full reason for the event, e.g. 'Paused
//                             on exception'. This string is shown in the UI
//                             as is."
//           },
//           "threadId": {
//             "type": "integer",
//             "description": "The thread which was stopped."
//           },
//           "text": {
//             "type": "string",
//             "description": "Additional information. E.g. if reason is
//                             'exception', text contains the exception name.
//                             This string is shown in the UI."
//           },
//           "allThreadsStopped": {
//             "type": "boolean",
//             "description": "If allThreadsStopped is true, a debug adapter
//                             can announce that all threads have stopped.
//                             The client should use this information to
//                             enable that all threads can be expanded to
//                             access their stacktraces. If the attribute
//                             is missing or false, only the thread with the
//                             given threadId can be expanded."
//           }
//         },
//         "required": [ "reason" ]
//       }
//     },
//     "required": [ "event", "body" ]
//   }]
// }
llvm::json::Value CreateThreadStopped(lldb::SBThread &thread,
                                      uint32_t stop_id) {
  llvm::json::Object event(CreateEventObject("stopped"));
  llvm::json::Object body;
  switch (thread.GetStopReason()) {
  case lldb::eStopReasonTrace:
  case lldb::eStopReasonPlanComplete:
    body.try_emplace("reason", "step");
    break;
  case lldb::eStopReasonBreakpoint: {
    ExceptionBreakpoint *exc_bp = g_dap.GetExceptionBPFromStopReason(thread);
    if (exc_bp) {
      body.try_emplace("reason", "exception");
      EmplaceSafeString(body, "description", exc_bp->label);
    } else {
      body.try_emplace("reason", "breakpoint");
      lldb::break_id_t bp_id = thread.GetStopReasonDataAtIndex(0);
      lldb::break_id_t bp_loc_id = thread.GetStopReasonDataAtIndex(1);
      std::string desc_str =
          llvm::formatv("breakpoint {0}.{1}", bp_id, bp_loc_id);
      body.try_emplace("hitBreakpointIds",
                       llvm::json::Array{llvm::json::Value(bp_id)});
      EmplaceSafeString(body, "description", desc_str);
    }
  } break;
  case lldb::eStopReasonWatchpoint:
  case lldb::eStopReasonInstrumentation:
    body.try_emplace("reason", "breakpoint");
    break;
  case lldb::eStopReasonProcessorTrace:
    body.try_emplace("reason", "processor trace");
    break;
  case lldb::eStopReasonSignal:
  case lldb::eStopReasonException:
    body.try_emplace("reason", "exception");
    break;
  case lldb::eStopReasonExec:
    body.try_emplace("reason", "entry");
    break;
  case lldb::eStopReasonFork:
    body.try_emplace("reason", "fork");
    break;
  case lldb::eStopReasonVFork:
    body.try_emplace("reason", "vfork");
    break;
  case lldb::eStopReasonVForkDone:
    body.try_emplace("reason", "vforkdone");
    break;
  case lldb::eStopReasonThreadExiting:
  case lldb::eStopReasonInvalid:
  case lldb::eStopReasonNone:
    break;
  }
  if (stop_id == 0)
    body.try_emplace("reason", "entry");
  const lldb::tid_t tid = thread.GetThreadID();
  body.try_emplace("threadId", (int64_t)tid);
  // If no description has been set, then set it to the default thread stopped
  // description. If we have breakpoints that get hit and shouldn't be reported
  // as breakpoints, then they will set the description above.
  if (!ObjectContainsKey(body, "description")) {
    char description[1024];
    if (thread.GetStopDescription(description, sizeof(description))) {
      EmplaceSafeString(body, "description", std::string(description));
    }
  }
  // "threadCausedFocus" is used in tests to validate breaking behavior.
  if (tid == g_dap.focus_tid) {
    body.try_emplace("threadCausedFocus", true);
  }
  body.try_emplace("preserveFocusHint", tid != g_dap.focus_tid);
  body.try_emplace("allThreadsStopped", true);
  event.try_emplace("body", std::move(body));
  return llvm::json::Value(std::move(event));
}

const char *GetNonNullVariableName(lldb::SBValue v) {
  const char *name = v.GetName();
  return name ? name : "<null>";
}

std::string CreateUniqueVariableNameForDisplay(lldb::SBValue v,
                                               bool is_name_duplicated) {
  lldb::SBStream name_builder;
  name_builder.Print(GetNonNullVariableName(v));
  if (is_name_duplicated) {
    lldb::SBDeclaration declaration = v.GetDeclaration();
    const char *file_name = declaration.GetFileSpec().GetFilename();
    const uint32_t line = declaration.GetLine();

    if (file_name != nullptr && line > 0)
      name_builder.Printf(" @ %s:%u", file_name, line);
    else if (const char *location = v.GetLocation())
      name_builder.Printf(" @ %s", location);
  }
  return name_builder.GetData();
}

VariableDescription::VariableDescription(lldb::SBValue v, bool format_hex,
                                         bool is_name_duplicated,
                                         std::optional<std::string> custom_name)
    : v(v) {
  name = custom_name
             ? *custom_name
             : CreateUniqueVariableNameForDisplay(v, is_name_duplicated);

  type_obj = v.GetType();
  std::string raw_display_type_name =
      llvm::StringRef(type_obj.GetDisplayTypeName()).str();
  display_type_name =
      !raw_display_type_name.empty() ? raw_display_type_name : NO_TYPENAME;

  // Only format hex/default if there is no existing special format.
  if (v.GetFormat() == lldb::eFormatDefault ||
      v.GetFormat() == lldb::eFormatHex) {
    if (format_hex)
      v.SetFormat(lldb::eFormatHex);
    else
      v.SetFormat(lldb::eFormatDefault);
  }

  llvm::raw_string_ostream os_display_value(display_value);

  if (lldb::SBError sb_error = v.GetError(); sb_error.Fail()) {
    error = sb_error.GetCString();
    os_display_value << "<error: " << error << ">";
  } else {
    value = llvm::StringRef(v.GetValue()).str();
    summary = llvm::StringRef(v.GetSummary()).str();
    if (summary.empty())
      auto_summary = TryCreateAutoSummary(v);

    std::optional<std::string> effective_summary =
        !summary.empty() ? summary : auto_summary;

    if (!value.empty()) {
      os_display_value << value;
      if (effective_summary)
        os_display_value << " " << *effective_summary;
    } else if (effective_summary) {
      os_display_value << *effective_summary;

      // As last resort, we print its type and address if available.
    } else {
      if (!raw_display_type_name.empty()) {
        os_display_value << raw_display_type_name;
        lldb::addr_t address = v.GetLoadAddress();
        if (address != LLDB_INVALID_ADDRESS)
          os_display_value << " @ " << llvm::format_hex(address, 0);
      }
    }
  }

  lldb::SBStream evaluateStream;
  v.GetExpressionPath(evaluateStream);
  evaluate_name = llvm::StringRef(evaluateStream.GetData()).str();
}

llvm::json::Object VariableDescription::GetVariableExtensionsJSON() {
  llvm::json::Object extensions;
  if (error)
    EmplaceSafeString(extensions, "error", *error);
  if (!value.empty())
    EmplaceSafeString(extensions, "value", value);
  if (!summary.empty())
    EmplaceSafeString(extensions, "summary", summary);
  if (auto_summary)
    EmplaceSafeString(extensions, "autoSummary", *auto_summary);

  if (lldb::SBDeclaration decl = v.GetDeclaration(); decl.IsValid()) {
    llvm::json::Object decl_obj;
    if (lldb::SBFileSpec file = decl.GetFileSpec(); file.IsValid()) {
      char path[PATH_MAX] = "";
      if (file.GetPath(path, sizeof(path)) &&
          lldb::SBFileSpec::ResolvePath(path, path, PATH_MAX)) {
        decl_obj.try_emplace("path", std::string(path));
      }
    }

    if (int line = decl.GetLine())
      decl_obj.try_emplace("line", line);
    if (int column = decl.GetColumn())
      decl_obj.try_emplace("column", column);

    if (!decl_obj.empty())
      extensions.try_emplace("declaration", std::move(decl_obj));
  }
  return extensions;
}

std::string VariableDescription::GetResult(llvm::StringRef context) {
  // In repl context, the results can be displayed as multiple lines so more
  // detailed descriptions can be returned.
  if (context != "repl")
    return display_value;

  if (!v.IsValid())
    return display_value;

  // Try the SBValue::GetDescription(), which may call into language runtime
  // specific formatters (see ValueObjectPrinter).
  lldb::SBStream stream;
  v.GetDescription(stream);
  llvm::StringRef description = stream.GetData();
  return description.trim().str();
}

// "Variable": {
//   "type": "object",
//   "description": "A Variable is a name/value pair. Optionally a variable
//                   can have a 'type' that is shown if space permits or when
//                   hovering over the variable's name. An optional 'kind' is
//                   used to render additional properties of the variable,
//                   e.g. different icons can be used to indicate that a
//                   variable is public or private. If the value is
//                   structured (has children), a handle is provided to
//                   retrieve the children with the VariablesRequest. If
//                   the number of named or indexed children is large, the
//                   numbers should be returned via the optional
//                   'namedVariables' and 'indexedVariables' attributes. The
//                   client can use this optional information to present the
//                   children in a paged UI and fetch them in chunks.",
//   "properties": {
//     "name": {
//       "type": "string",
//       "description": "The variable's name."
//     },
//     "value": {
//       "type": "string",
//       "description": "The variable's value. This can be a multi-line text,
//                       e.g. for a function the body of a function."
//     },
//     "type": {
//       "type": "string",
//       "description": "The type of the variable's value. Typically shown in
//                       the UI when hovering over the value."
//     },
//     "presentationHint": {
//       "$ref": "#/definitions/VariablePresentationHint",
//       "description": "Properties of a variable that can be used to determine
//                       how to render the variable in the UI."
//     },
//     "evaluateName": {
//       "type": "string",
//       "description": "Optional evaluatable name of this variable which can
//                       be passed to the 'EvaluateRequest' to fetch the
//                       variable's value."
//     },
//     "variablesReference": {
//       "type": "integer",
//       "description": "If variablesReference is > 0, the variable is
//                       structured and its children can be retrieved by
//                       passing variablesReference to the VariablesRequest."
//     },
//     "namedVariables": {
//       "type": "integer",
//       "description": "The number of named child variables. The client can
//                       use this optional information to present the children
//                       in a paged UI and fetch them in chunks."
//     },
//     "indexedVariables": {
//       "type": "integer",
//       "description": "The number of indexed child variables. The client
//                       can use this optional information to present the
//                       children in a paged UI and fetch them in chunks."
//     }
//
//
//     "$__lldb_extensions": {
//       "description": "Unofficial extensions to the protocol",
//       "properties": {
//         "declaration": {
//         "type": "object",
//         "description": "The source location where the variable was declared.
//                         This value won't be present if no declaration is
//                         available.",
//         "properties": {
//           "path": {
//             "type": "string",
//             "description": "The source file path where the variable was
//                            declared."
//           },
//           "line": {
//             "type": "number",
//             "description": "The 1-indexed source line where the variable was
//                             declared."
//           },
//           "column": {
//             "type": "number",
//             "description": "The 1-indexed source column where the variable
//                             was declared."
//           }
//         }
//       },
//       "value":
//         "type": "string",
//         "description": "The internal value of the variable as returned by
//                         This is effectively SBValue.GetValue(). The other
//                         `value` entry in the top-level variable response is,
//                          on the other hand, just a display string for the
//                          variable."
//       },
//       "summary":
//         "type": "string",
//         "description": "The summary string of the variable. This is
//                         effectively SBValue.GetSummary()."
//       },
//       "autoSummary":
//         "type": "string",
//         "description": "The auto generated summary if using
//                         `enableAutoVariableSummaries`."
//       },
//       "error":
//         "type": "string",
//         "description": "An error message generated if LLDB couldn't inspect
//                         the variable."
//       }
//     }
//   },
//   "required": [ "name", "value", "variablesReference" ]
// }
llvm::json::Value CreateVariable(lldb::SBValue v, int64_t variablesReference,
                                 int64_t varID, bool format_hex,
                                 bool is_name_duplicated,
                                 std::optional<std::string> custom_name) {
  VariableDescription desc(v, format_hex, is_name_duplicated, custom_name);
  llvm::json::Object object;
  EmplaceSafeString(object, "name", desc.name);
  EmplaceSafeString(object, "value", desc.display_value);

  if (!desc.evaluate_name.empty())
    EmplaceSafeString(object, "evaluateName", desc.evaluate_name);

  // If we have a type with many children, we would like to be able to
  // give a hint to the IDE that the type has indexed children so that the
  // request can be broken up in grabbing only a few children at a time. We
  // want to be careful and only call "v.GetNumChildren()" if we have an array
  // type or if we have a synthetic child provider producing indexed children.
  // We don't want to call "v.GetNumChildren()" on all objects as class, struct
  // and union types don't need to be completed if they are never expanded. So
  // we want to avoid calling this to only cases where we it makes sense to keep
  // performance high during normal debugging.

  // If we have an array type, say that it is indexed and provide the number
  // of children in case we have a huge array. If we don't do this, then we
  // might take a while to produce all children at onces which can delay your
  // debug session.
  if (desc.type_obj.IsArrayType()) {
    object.try_emplace("indexedVariables", v.GetNumChildren());
  } else if (v.IsSynthetic()) {
    // For a type with a synthetic child provider, the SBType of "v" won't tell
    // us anything about what might be displayed. Instead, we check if the first
    // child's name is "[0]" and then say it is indexed. We call
    // GetNumChildren() only if the child name matches to avoid a potentially
    // expensive operation.
    if (lldb::SBValue first_child = v.GetChildAtIndex(0)) {
      llvm::StringRef first_child_name = first_child.GetName();
      if (first_child_name == "[0]") {
        size_t num_children = v.GetNumChildren();
        // If we are creating a "[raw]" fake child for each synthetic type, we
        // have to account for it when returning indexed variables.
        if (g_dap.enable_synthetic_child_debugging)
          ++num_children;
        object.try_emplace("indexedVariables", num_children);
      }
    }
  }
  EmplaceSafeString(object, "type", desc.display_type_name);
  if (varID != INT64_MAX)
    object.try_emplace("id", varID);
  if (v.MightHaveChildren())
    object.try_emplace("variablesReference", variablesReference);
  else
    object.try_emplace("variablesReference", (int64_t)0);

  object.try_emplace("$__lldb_extensions", desc.GetVariableExtensionsJSON());
  return llvm::json::Value(std::move(object));
}

llvm::json::Value CreateCompileUnit(lldb::SBCompileUnit unit) {
  llvm::json::Object object;
  char unit_path_arr[PATH_MAX];
  unit.GetFileSpec().GetPath(unit_path_arr, sizeof(unit_path_arr));
  std::string unit_path(unit_path_arr);
  object.try_emplace("compileUnitPath", unit_path);
  return llvm::json::Value(std::move(object));
}

/// See
/// https://microsoft.github.io/debug-adapter-protocol/specification#Reverse_Requests_RunInTerminal
llvm::json::Object
CreateRunInTerminalReverseRequest(const llvm::json::Object &launch_request,
                                  llvm::StringRef debug_adaptor_path,
                                  llvm::StringRef comm_file,
                                  lldb::pid_t debugger_pid) {
  llvm::json::Object run_in_terminal_args;
  // This indicates the IDE to open an embedded terminal, instead of opening the
  // terminal in a new window.
  run_in_terminal_args.try_emplace("kind", "integrated");

  auto launch_request_arguments = launch_request.getObject("arguments");
  // The program path must be the first entry in the "args" field
  std::vector<std::string> args = {debug_adaptor_path.str(), "--comm-file",
                                   comm_file.str()};
  if (debugger_pid != LLDB_INVALID_PROCESS_ID) {
    args.push_back("--debugger-pid");
    args.push_back(std::to_string(debugger_pid));
  }
  args.push_back("--launch-target");
  args.push_back(GetString(launch_request_arguments, "program").str());
  std::vector<std::string> target_args =
      GetStrings(launch_request_arguments, "args");
  args.insert(args.end(), target_args.begin(), target_args.end());
  run_in_terminal_args.try_emplace("args", args);

  const auto cwd = GetString(launch_request_arguments, "cwd");
  if (!cwd.empty())
    run_in_terminal_args.try_emplace("cwd", cwd);

  // We need to convert the input list of environments variables into a
  // dictionary
  std::vector<std::string> envs = GetStrings(launch_request_arguments, "env");
  llvm::json::Object environment;
  for (const std::string &env : envs) {
    size_t index = env.find('=');
    environment.try_emplace(env.substr(0, index), env.substr(index + 1));
  }
  run_in_terminal_args.try_emplace("env",
                                   llvm::json::Value(std::move(environment)));

  return run_in_terminal_args;
}

// Keep all the top level items from the statistics dump, except for the
// "modules" array. It can be huge and cause delay
// Array and dictionary value will return as <key, JSON string> pairs
void FilterAndGetValueForKey(const lldb::SBStructuredData data, const char *key,
                             llvm::json::Object &out) {
  lldb::SBStructuredData value = data.GetValueForKey(key);
  std::string key_utf8 = llvm::json::fixUTF8(key);
  if (strcmp(key, "modules") == 0)
    return;
  switch (value.GetType()) {
  case lldb::eStructuredDataTypeFloat:
    out.try_emplace(key_utf8, value.GetFloatValue());
    break;
  case lldb::eStructuredDataTypeUnsignedInteger:
    out.try_emplace(key_utf8, value.GetIntegerValue((uint64_t)0));
    break;
  case lldb::eStructuredDataTypeSignedInteger:
    out.try_emplace(key_utf8, value.GetIntegerValue((int64_t)0));
    break;
  case lldb::eStructuredDataTypeArray: {
    lldb::SBStream contents;
    value.GetAsJSON(contents);
    out.try_emplace(key_utf8, llvm::json::fixUTF8(contents.GetData()));
  } break;
  case lldb::eStructuredDataTypeBoolean:
    out.try_emplace(key_utf8, value.GetBooleanValue());
    break;
  case lldb::eStructuredDataTypeString: {
    // Get the string size before reading
    const size_t str_length = value.GetStringValue(nullptr, 0);
    std::string str(str_length + 1, 0);
    value.GetStringValue(&str[0], str_length);
    out.try_emplace(key_utf8, llvm::json::fixUTF8(str));
  } break;
  case lldb::eStructuredDataTypeDictionary: {
    lldb::SBStream contents;
    value.GetAsJSON(contents);
    out.try_emplace(key_utf8, llvm::json::fixUTF8(contents.GetData()));
  } break;
  case lldb::eStructuredDataTypeNull:
  case lldb::eStructuredDataTypeGeneric:
  case lldb::eStructuredDataTypeInvalid:
    break;
  }
}

void addStatistic(llvm::json::Object &event) {
  lldb::SBStructuredData statistics = g_dap.target.GetStatistics();
  bool is_dictionary =
      statistics.GetType() == lldb::eStructuredDataTypeDictionary;
  if (!is_dictionary)
    return;
  llvm::json::Object stats_body;

  lldb::SBStringList keys;
  if (!statistics.GetKeys(keys))
    return;
  for (size_t i = 0; i < keys.GetSize(); i++) {
    const char *key = keys.GetStringAtIndex(i);
    FilterAndGetValueForKey(statistics, key, stats_body);
  }
  event.try_emplace("statistics", std::move(stats_body));
}

llvm::json::Object CreateTerminatedEventObject() {
  llvm::json::Object event(CreateEventObject("terminated"));
  addStatistic(event);
  return event;
}

std::string JSONToString(const llvm::json::Value &json) {
  std::string data;
  llvm::raw_string_ostream os(data);
  os << json;
  os.flush();
  return data;
}

} // namespace lldb_dap
