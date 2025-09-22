//===-- ValueObjectPrinter.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_VALUEOBJECTPRINTER_H
#define LLDB_DATAFORMATTERS_VALUEOBJECTPRINTER_H

#include "lldb/lldb-private.h"
#include "lldb/lldb-public.h"

#include "lldb/Utility/Flags.h"

#include "lldb/DataFormatters/DumpValueObjectOptions.h"
#include "lldb/Symbol/CompilerType.h"

namespace lldb_private {

class ValueObjectPrinter {
  /// The ValueObjectPrinter is a one-shot printer for ValueObjects.  It
  /// does not retain the ValueObject it is printing, that is the job of
  /// its caller.  It also doesn't attempt to track changes in the
  /// ValueObject, e.g. changing synthetic child providers or changing
  /// dynamic versus static versus synthetic settings.
public:
  ValueObjectPrinter(ValueObject &valobj, Stream *s);

  ValueObjectPrinter(ValueObject &valobj, Stream *s,
                     const DumpValueObjectOptions &options);

  ~ValueObjectPrinter() = default;

  llvm::Error PrintValueObject();

protected:
  typedef std::set<uint64_t> InstancePointersSet;
  typedef std::shared_ptr<InstancePointersSet> InstancePointersSetSP;

  InstancePointersSetSP m_printed_instance_pointers;

  /// Only this class (and subclasses, if any) should ever be
  /// concerned with the depth mechanism.
  ValueObjectPrinter(ValueObject &valobj, Stream *s,
                     const DumpValueObjectOptions &options,
                     const DumpValueObjectOptions::PointerDepth &ptr_depth,
                     uint32_t curr_depth,
                     InstancePointersSetSP printed_instance_pointers);

  /// Ee should actually be using delegating constructors here but
  /// some versions of GCC still have trouble with those.
  void Init(ValueObject &valobj, Stream *s,
            const DumpValueObjectOptions &options,
            const DumpValueObjectOptions::PointerDepth &ptr_depth,
            uint32_t curr_depth,
            InstancePointersSetSP printed_instance_pointers);

  /// Cache the ValueObject we are actually going to print.  If this
  /// ValueObject has a Dynamic type, we return that, if either the original
  /// ValueObject or its Dynamic type has a Synthetic provider, return that.
  /// This will never return an empty ValueObject, since we use the ValueObject
  /// to carry errors.
  /// Note, this gets called when making the printer object, and uses the
  /// use dynamic and use synthetic settings of the ValueObject being printed,
  /// so changes made to these settings won't affect already made
  /// ValueObjectPrinters. SetupMostSpecializedValue();
  ///
  /// Access the cached "most specialized value" - that is the one to use for
  /// printing the value object's value.  However, be sure to use
  /// GetValueForChildGeneration when you are generating the children of this
  /// value.
  ValueObject &GetMostSpecializedValue();

  void SetupMostSpecializedValue();

  llvm::Expected<std::string> GetDescriptionForDisplay();

  const char *GetRootNameForDisplay();

  bool ShouldPrintValueObject();

  bool IsNil();

  bool IsUninitialized();

  bool IsPtr();

  bool IsRef();

  bool IsInstancePointer();

  bool IsAggregate();

  bool PrintLocationIfNeeded();

  void PrintDecl();

  bool CheckScopeIfNeeded();

  bool ShouldPrintEmptyBrackets(bool value_printed, bool summary_printed);

  TypeSummaryImpl *GetSummaryFormatter(bool null_if_omitted = true);

  void GetValueSummaryError(std::string &value, std::string &summary,
                            std::string &error);

  bool PrintValueAndSummaryIfNeeded(bool &value_printed, bool &summary_printed);

  llvm::Error PrintObjectDescriptionIfNeeded(bool value_printed,
                                             bool summary_printed);

  bool
  ShouldPrintChildren(DumpValueObjectOptions::PointerDepth &curr_ptr_depth);

  bool ShouldExpandEmptyAggregates();

  ValueObject &GetValueObjectForChildrenGeneration();

  void PrintChildrenPreamble(bool value_printed, bool summary_printed);

  void PrintChildrenPostamble(bool print_dotdotdot);

  lldb::ValueObjectSP GenerateChild(ValueObject &synth_valobj, size_t idx);

  void PrintChild(lldb::ValueObjectSP child_sp,
                  const DumpValueObjectOptions::PointerDepth &curr_ptr_depth);

  llvm::Expected<uint32_t> GetMaxNumChildrenToPrint(bool &print_dotdotdot);

  void
  PrintChildren(bool value_printed, bool summary_printed,
                const DumpValueObjectOptions::PointerDepth &curr_ptr_depth);

  llvm::Error PrintChildrenIfNeeded(bool value_printed, bool summary_printed);

  bool PrintChildrenOneLiner(bool hide_names);

  bool HasReachedMaximumDepth();

private:
  bool ShouldShowName() const;

  ValueObject &m_orig_valobj;
  /// Cache the current "most specialized" value.  Don't use this
  /// directly, use GetMostSpecializedValue.
  ValueObject *m_cached_valobj;
  Stream *m_stream;
  DumpValueObjectOptions m_options;
  Flags m_type_flags;
  CompilerType m_compiler_type;
  DumpValueObjectOptions::PointerDepth m_ptr_depth;
  uint32_t m_curr_depth;
  LazyBool m_should_print;
  LazyBool m_is_nil;
  LazyBool m_is_uninit;
  LazyBool m_is_ptr;
  LazyBool m_is_ref;
  LazyBool m_is_aggregate;
  LazyBool m_is_instance_ptr;
  std::pair<TypeSummaryImpl *, bool> m_summary_formatter;
  std::string m_value;
  std::string m_summary;
  std::string m_error;
  bool m_val_summary_ok;

  friend struct StringSummaryFormat;

  ValueObjectPrinter(const ValueObjectPrinter &) = delete;
  const ValueObjectPrinter &operator=(const ValueObjectPrinter &) = delete;
};

} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_VALUEOBJECTPRINTER_H
