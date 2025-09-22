//===-- lldb-private-types.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_LLDB_PRIVATE_TYPES_H
#define LLDB_LLDB_PRIVATE_TYPES_H

#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"

#include <type_traits>

namespace llvm {
namespace sys {
class DynamicLibrary;
}
}

namespace lldb_private {
class Platform;
class ExecutionContext;
class RegisterFlags;

typedef llvm::sys::DynamicLibrary (*LoadPluginCallbackType)(
    const lldb::DebuggerSP &debugger_sp, const FileSpec &spec, Status &error);

/// Every register is described in detail including its name, alternate name
/// (optional), encoding, size in bytes and the default display format.
struct RegisterInfo {
  /// Name of this register, can't be NULL.
  const char *name;
  /// Alternate name of this register, can be NULL.
  const char *alt_name;
  /// Size in bytes of the register.
  uint32_t byte_size;
  /// The byte offset in the register context data where this register's
  /// value is found.
  /// This is optional, and can be 0 if a particular RegisterContext does not
  /// need to address its registers by byte offset.
  uint32_t byte_offset;
  /// Encoding of the register bits.
  lldb::Encoding encoding;
  /// Default display format.
  lldb::Format format;
  /// Holds all of the various register numbers for all register kinds.
  uint32_t kinds[lldb::kNumRegisterKinds]; //
  /// List of registers (terminated with LLDB_INVALID_REGNUM). If this value is
  /// not null, all registers in this list will be read first, at which point
  /// the value for this register will be valid. For example, the value list
  /// for ah would be eax (x86) or rax (x64). Register numbers are
  /// of eRegisterKindLLDB. If multiple registers are listed, the final
  /// value will be the concatenation of them.
  uint32_t *value_regs;
  /// List of registers (terminated with LLDB_INVALID_REGNUM). If this value is
  /// not null, all registers in this list will be invalidated when the value of
  /// this register changes. For example, the invalidate list for eax would be
  /// rax ax, ah, and al.
  uint32_t *invalidate_regs;
  /// If not nullptr, a type defined by XML descriptions.
  /// Register info tables are constructed as const, but this field may need to
  /// be updated if a specific target OS has a different layout. To enable that,
  /// this is mutable. The data pointed to is still const, so you must swap a
  /// whole set of flags for another.
  mutable const RegisterFlags *flags_type;

  llvm::ArrayRef<uint8_t> data(const uint8_t *context_base) const {
    return llvm::ArrayRef<uint8_t>(context_base + byte_offset, byte_size);
  }

  llvm::MutableArrayRef<uint8_t> mutable_data(uint8_t *context_base) const {
    return llvm::MutableArrayRef<uint8_t>(context_base + byte_offset,
                                          byte_size);
  }
};
static_assert(std::is_trivial<RegisterInfo>::value,
              "RegisterInfo must be trivial.");

/// Registers are grouped into register sets
struct RegisterSet {
  /// Name of this register set.
  const char *name;
  /// A short name for this register set.
  const char *short_name;
  /// The number of registers in REGISTERS array below.
  size_t num_registers;
  /// An array of register indices in this set. The values in this array are
  /// *indices* (not register numbers) into a particular RegisterContext's
  /// register array.  For example, if eax is defined at index 4 for a
  /// particular RegisterContext, eax would be included in this RegisterSet by
  /// adding the value 4.  Not by adding the value lldb_eax_i386.
  const uint32_t *registers;
};

/// A type-erased pair of llvm::dwarf::SourceLanguageName and version.
struct SourceLanguage {
  SourceLanguage() = default;
  SourceLanguage(lldb::LanguageType language_type);
  SourceLanguage(uint16_t name, uint32_t version)
      : name(name), version(version) {}
  SourceLanguage(std::optional<std::pair<uint16_t, uint32_t>> name_vers)
      : name(name_vers ? name_vers->first : 0),
        version(name_vers ? name_vers->second : 0) {}
  operator bool() const { return name > 0; }
  lldb::LanguageType AsLanguageType() const;
  llvm::StringRef GetDescription() const;
  bool IsC() const;
  bool IsObjC() const;
  bool IsCPlusPlus() const;
  uint16_t name = 0;
  uint32_t version = 0;
};

struct OptionEnumValueElement {
  int64_t value;
  const char *string_value;
  const char *usage;
};

using OptionEnumValues = llvm::ArrayRef<OptionEnumValueElement>;

struct OptionValidator {
  virtual ~OptionValidator() = default;
  virtual bool IsValid(Platform &platform,
                       const ExecutionContext &target) const = 0;
  virtual const char *ShortConditionString() const = 0;
  virtual const char *LongConditionString() const = 0;
};

typedef struct type128 { uint64_t x[2]; } type128;
typedef struct type256 { uint64_t x[4]; } type256;

/// Functor that returns a ValueObjectSP for a variable given its name
/// and the StackFrame of interest. Used primarily in the Materializer
/// to refetch a ValueObject when the ExecutionContextScope changes.
using ValueObjectProviderTy =
    std::function<lldb::ValueObjectSP(ConstString, StackFrame *)>;

typedef void (*DebuggerDestroyCallback)(lldb::user_id_t debugger_id,
                                        void *baton);
typedef bool (*CommandOverrideCallbackWithResult)(
    void *baton, const char **argv, lldb_private::CommandReturnObject &result);
} // namespace lldb_private

#endif // LLDB_LLDB_PRIVATE_TYPES_H
