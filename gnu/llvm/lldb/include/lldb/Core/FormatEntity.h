//===-- FormatEntity.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_FORMATENTITY_H
#define LLDB_CORE_FORMATENTITY_H

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-types.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <string>
#include <vector>

namespace lldb_private {
class Address;
class CompletionRequest;
class ExecutionContext;
class FileSpec;
class Status;
class Stream;
class StringList;
class SymbolContext;
class ValueObject;
}

namespace llvm {
class StringRef;
}

namespace lldb_private {
namespace FormatEntity {
struct Entry {
  enum class Type {
    Invalid,
    ParentNumber,
    ParentString,
    EscapeCode,
    Root,
    String,
    Scope,
    Variable,
    VariableSynthetic,
    ScriptVariable,
    ScriptVariableSynthetic,
    AddressLoad,
    AddressFile,
    AddressLoadOrFile,
    ProcessID,
    ProcessFile,
    ScriptProcess,
    ThreadID,
    ThreadProtocolID,
    ThreadIndexID,
    ThreadName,
    ThreadQueue,
    ThreadStopReason,
    ThreadStopReasonRaw,
    ThreadReturnValue,
    ThreadCompletedExpression,
    ScriptThread,
    ThreadInfo,
    TargetArch,
    ScriptTarget,
    ModuleFile,
    File,
    Lang,
    FrameIndex,
    FrameNoDebug,
    FrameRegisterPC,
    FrameRegisterSP,
    FrameRegisterFP,
    FrameRegisterFlags,
    FrameRegisterByName,
    FrameIsArtificial,
    ScriptFrame,
    FunctionID,
    FunctionDidChange,
    FunctionInitialFunction,
    FunctionName,
    FunctionNameWithArgs,
    FunctionNameNoArgs,
    FunctionMangledName,
    FunctionAddrOffset,
    FunctionAddrOffsetConcrete,
    FunctionLineOffset,
    FunctionPCOffset,
    FunctionInitial,
    FunctionChanged,
    FunctionIsOptimized,
    LineEntryFile,
    LineEntryLineNumber,
    LineEntryColumn,
    LineEntryStartAddress,
    LineEntryEndAddress,
    CurrentPCArrow
  };

  struct Definition {
    /// The name/string placeholder that corresponds to this definition.
    const char *name;
    /// Insert this exact string into the output
    const char *string = nullptr;
    /// Entry::Type corresponding to this definition.
    const Entry::Type type;
    /// Data that is returned as the value of the format string.
    const uint64_t data = 0;
    /// The number of children of this node in the tree of format strings.
    const uint32_t num_children = 0;
    /// An array of "num_children" Definition entries.
    const Definition *children = nullptr;
    /// Whether the separator is kept during parsing or not.  It's used
    /// for entries with parameters.
    const bool keep_separator = false;

    constexpr Definition(const char *name, const FormatEntity::Entry::Type t)
        : name(name), type(t) {}

    constexpr Definition(const char *name, const char *string)
        : name(name), string(string), type(Entry::Type::EscapeCode) {}

    constexpr Definition(const char *name, const FormatEntity::Entry::Type t,
                         const uint64_t data)
        : name(name), type(t), data(data) {}

    constexpr Definition(const char *name, const FormatEntity::Entry::Type t,
                         const uint64_t num_children,
                         const Definition *children,
                         const bool keep_separator = false)
        : name(name), type(t), num_children(num_children), children(children),
          keep_separator(keep_separator) {}
  };

  template <size_t N>
  static constexpr Definition
  DefinitionWithChildren(const char *name, const FormatEntity::Entry::Type t,
                         const Definition (&children)[N],
                         bool keep_separator = false) {
    return Definition(name, t, N, children, keep_separator);
  }

  Entry(Type t = Type::Invalid, const char *s = nullptr,
        const char *f = nullptr)
      : string(s ? s : ""), printf_format(f ? f : ""), type(t) {}

  Entry(llvm::StringRef s);
  Entry(char ch);

  void AppendChar(char ch);

  void AppendText(const llvm::StringRef &s);

  void AppendText(const char *cstr);

  void AppendEntry(const Entry &&entry) { children.push_back(entry); }

  void Clear() {
    string.clear();
    printf_format.clear();
    children.clear();
    type = Type::Invalid;
    fmt = lldb::eFormatDefault;
    number = 0;
    deref = false;
  }

  static const char *TypeToCString(Type t);

  void Dump(Stream &s, int depth = 0) const;

  bool operator==(const Entry &rhs) const {
    if (string != rhs.string)
      return false;
    if (printf_format != rhs.printf_format)
      return false;
    const size_t n = children.size();
    const size_t m = rhs.children.size();
    for (size_t i = 0; i < std::min<size_t>(n, m); ++i) {
      if (!(children[i] == rhs.children[i]))
        return false;
    }
    if (children != rhs.children)
      return false;
    if (type != rhs.type)
      return false;
    if (fmt != rhs.fmt)
      return false;
    if (deref != rhs.deref)
      return false;
    return true;
  }

  std::string string;
  std::string printf_format;
  std::vector<Entry> children;
  Type type;
  lldb::Format fmt = lldb::eFormatDefault;
  lldb::addr_t number = 0;
  bool deref = false;
};

bool Format(const Entry &entry, Stream &s, const SymbolContext *sc,
            const ExecutionContext *exe_ctx, const Address *addr,
            ValueObject *valobj, bool function_changed, bool initial_function);

bool FormatStringRef(const llvm::StringRef &format, Stream &s,
                     const SymbolContext *sc, const ExecutionContext *exe_ctx,
                     const Address *addr, ValueObject *valobj,
                     bool function_changed, bool initial_function);

bool FormatCString(const char *format, Stream &s, const SymbolContext *sc,
                   const ExecutionContext *exe_ctx, const Address *addr,
                   ValueObject *valobj, bool function_changed,
                   bool initial_function);

Status Parse(const llvm::StringRef &format, Entry &entry);

Status ExtractVariableInfo(llvm::StringRef &format_str,
                           llvm::StringRef &variable_name,
                           llvm::StringRef &variable_format);

void AutoComplete(lldb_private::CompletionRequest &request);

// Format the current elements into the stream \a s.
//
// The root element will be stripped off and the format str passed in will be
// either an empty string (print a description of this object), or contain a
// `.`-separated series like a domain name that identifies further
//  sub-elements to display.
bool FormatFileSpec(const FileSpec &file, Stream &s, llvm::StringRef elements,
                    llvm::StringRef element_format);

/// For each variable in 'args' this function writes the variable
/// name and it's pretty-printed value representation to 'out_stream'
/// in following format:
///
/// \verbatim
/// name_1=repr_1, name_2=repr_2 ...
/// \endverbatim
void PrettyPrintFunctionArguments(Stream &out_stream, VariableList const &args,
                                  ExecutionContextScope *exe_scope);
} // namespace FormatEntity
} // namespace lldb_private

#endif // LLDB_CORE_FORMATENTITY_H
