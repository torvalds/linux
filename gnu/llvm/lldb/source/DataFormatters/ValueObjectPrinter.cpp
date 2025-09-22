//===-- ValueObjectPrinter.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/ValueObjectPrinter.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Stream.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>

using namespace lldb;
using namespace lldb_private;

ValueObjectPrinter::ValueObjectPrinter(ValueObject &valobj, Stream *s)
    : m_orig_valobj(valobj) {
  DumpValueObjectOptions options(valobj);
  Init(valobj, s, options, m_options.m_max_ptr_depth, 0, nullptr);
}

ValueObjectPrinter::ValueObjectPrinter(ValueObject &valobj, Stream *s,
                                       const DumpValueObjectOptions &options)
    : m_orig_valobj(valobj) {
  Init(valobj, s, options, m_options.m_max_ptr_depth, 0, nullptr);
}

ValueObjectPrinter::ValueObjectPrinter(
    ValueObject &valobj, Stream *s, const DumpValueObjectOptions &options,
    const DumpValueObjectOptions::PointerDepth &ptr_depth, uint32_t curr_depth,
    InstancePointersSetSP printed_instance_pointers)
    : m_orig_valobj(valobj) {
  Init(valobj, s, options, ptr_depth, curr_depth, printed_instance_pointers);
}

void ValueObjectPrinter::Init(
    ValueObject &valobj, Stream *s, const DumpValueObjectOptions &options,
    const DumpValueObjectOptions::PointerDepth &ptr_depth, uint32_t curr_depth,
    InstancePointersSetSP printed_instance_pointers) {
  m_cached_valobj = nullptr;
  m_stream = s;
  m_options = options;
  m_ptr_depth = ptr_depth;
  m_curr_depth = curr_depth;
  assert(m_stream && "cannot print to a NULL Stream");
  m_should_print = eLazyBoolCalculate;
  m_is_nil = eLazyBoolCalculate;
  m_is_uninit = eLazyBoolCalculate;
  m_is_ptr = eLazyBoolCalculate;
  m_is_ref = eLazyBoolCalculate;
  m_is_aggregate = eLazyBoolCalculate;
  m_is_instance_ptr = eLazyBoolCalculate;
  m_summary_formatter = {nullptr, false};
  m_value.assign("");
  m_summary.assign("");
  m_error.assign("");
  m_val_summary_ok = false;
  m_printed_instance_pointers =
      printed_instance_pointers
          ? printed_instance_pointers
          : InstancePointersSetSP(new InstancePointersSet());
  SetupMostSpecializedValue();
}

llvm::Error ValueObjectPrinter::PrintValueObject() {
  // If the incoming ValueObject is in an error state, the best we're going to 
  // get out of it is its type.  But if we don't even have that, just print
  // the error and exit early.
  if (m_orig_valobj.GetError().Fail() &&
      !m_orig_valobj.GetCompilerType().IsValid())
    return m_orig_valobj.GetError().ToError();

  if (ShouldPrintValueObject()) {
    PrintLocationIfNeeded();
    m_stream->Indent();

    PrintDecl();
  }

  bool value_printed = false;
  bool summary_printed = false;

  m_val_summary_ok =
      PrintValueAndSummaryIfNeeded(value_printed, summary_printed);

  if (m_val_summary_ok)
    return PrintChildrenIfNeeded(value_printed, summary_printed);
  m_stream->EOL();

  return llvm::Error::success();
}

ValueObject &ValueObjectPrinter::GetMostSpecializedValue() {
  assert(m_cached_valobj && "ValueObjectPrinter must have a valid ValueObject");
  return *m_cached_valobj;
}

void ValueObjectPrinter::SetupMostSpecializedValue() {
  bool update_success = m_orig_valobj.UpdateValueIfNeeded(true);
  // If we can't find anything better, we'll fall back on the original
  // ValueObject.
  m_cached_valobj = &m_orig_valobj;
  if (update_success) {
    if (m_orig_valobj.IsDynamic()) {
      if (m_options.m_use_dynamic == eNoDynamicValues) {
        ValueObject *static_value = m_orig_valobj.GetStaticValue().get();
        if (static_value)
          m_cached_valobj = static_value;
      }
    } else {
      if (m_options.m_use_dynamic != eNoDynamicValues) {
        ValueObject *dynamic_value =
            m_orig_valobj.GetDynamicValue(m_options.m_use_dynamic).get();
        if (dynamic_value)
          m_cached_valobj = dynamic_value;
      }
    }

    if (m_cached_valobj->IsSynthetic()) {
      if (!m_options.m_use_synthetic) {
        ValueObject *non_synthetic =
            m_cached_valobj->GetNonSyntheticValue().get();
        if (non_synthetic)
          m_cached_valobj = non_synthetic;
      }
    } else {
      if (m_options.m_use_synthetic) {
        ValueObject *synthetic = m_cached_valobj->GetSyntheticValue().get();
        if (synthetic)
          m_cached_valobj = synthetic;
      }
    }
  }
  m_compiler_type = m_cached_valobj->GetCompilerType();
  m_type_flags = m_compiler_type.GetTypeInfo();
  assert(m_cached_valobj &&
         "SetupMostSpecialized value must compute a valid ValueObject");
}

llvm::Expected<std::string> ValueObjectPrinter::GetDescriptionForDisplay() {
  ValueObject &valobj = GetMostSpecializedValue();
  llvm::Expected<std::string> maybe_str = valobj.GetObjectDescription();
  if (maybe_str)
    return maybe_str;

  const char *str = nullptr;
  if (!str)
    str = valobj.GetSummaryAsCString();
  if (!str)
    str = valobj.GetValueAsCString();

  if (!str)
    return maybe_str;
  llvm::consumeError(maybe_str.takeError());
  return str;
}

const char *ValueObjectPrinter::GetRootNameForDisplay() {
  const char *root_valobj_name =
      m_options.m_root_valobj_name.empty()
          ? GetMostSpecializedValue().GetName().AsCString()
          : m_options.m_root_valobj_name.c_str();
  return root_valobj_name ? root_valobj_name : "";
}

bool ValueObjectPrinter::ShouldPrintValueObject() {
  if (m_should_print == eLazyBoolCalculate)
    m_should_print =
        (!m_options.m_flat_output || m_type_flags.Test(eTypeHasValue))
            ? eLazyBoolYes
            : eLazyBoolNo;
  return m_should_print == eLazyBoolYes;
}

bool ValueObjectPrinter::IsNil() {
  if (m_is_nil == eLazyBoolCalculate)
    m_is_nil =
        GetMostSpecializedValue().IsNilReference() ? eLazyBoolYes : eLazyBoolNo;
  return m_is_nil == eLazyBoolYes;
}

bool ValueObjectPrinter::IsUninitialized() {
  if (m_is_uninit == eLazyBoolCalculate)
    m_is_uninit = GetMostSpecializedValue().IsUninitializedReference()
                      ? eLazyBoolYes
                      : eLazyBoolNo;
  return m_is_uninit == eLazyBoolYes;
}

bool ValueObjectPrinter::IsPtr() {
  if (m_is_ptr == eLazyBoolCalculate)
    m_is_ptr = m_type_flags.Test(eTypeIsPointer) ? eLazyBoolYes : eLazyBoolNo;
  return m_is_ptr == eLazyBoolYes;
}

bool ValueObjectPrinter::IsRef() {
  if (m_is_ref == eLazyBoolCalculate)
    m_is_ref = m_type_flags.Test(eTypeIsReference) ? eLazyBoolYes : eLazyBoolNo;
  return m_is_ref == eLazyBoolYes;
}

bool ValueObjectPrinter::IsAggregate() {
  if (m_is_aggregate == eLazyBoolCalculate)
    m_is_aggregate =
        m_type_flags.Test(eTypeHasChildren) ? eLazyBoolYes : eLazyBoolNo;
  return m_is_aggregate == eLazyBoolYes;
}

bool ValueObjectPrinter::IsInstancePointer() {
  // you need to do this check on the value's clang type
  ValueObject &valobj = GetMostSpecializedValue();
  if (m_is_instance_ptr == eLazyBoolCalculate)
    m_is_instance_ptr = (valobj.GetValue().GetCompilerType().GetTypeInfo() &
                         eTypeInstanceIsPointer) != 0
                            ? eLazyBoolYes
                            : eLazyBoolNo;
  if ((eLazyBoolYes == m_is_instance_ptr) && valobj.IsBaseClass())
    m_is_instance_ptr = eLazyBoolNo;
  return m_is_instance_ptr == eLazyBoolYes;
}

bool ValueObjectPrinter::PrintLocationIfNeeded() {
  if (m_options.m_show_location) {
    m_stream->Printf("%s: ", GetMostSpecializedValue().GetLocationAsCString());
    return true;
  }
  return false;
}

void ValueObjectPrinter::PrintDecl() {
  bool show_type = true;
  // if we are at the root-level and been asked to hide the root's type, then
  // hide it
  if (m_curr_depth == 0 && m_options.m_hide_root_type)
    show_type = false;
  else
    // otherwise decide according to the usual rules (asked to show types -
    // always at the root level)
    show_type = m_options.m_show_types ||
                (m_curr_depth == 0 && !m_options.m_flat_output);

  StreamString typeName;
  // Figure out which ValueObject we're acting on
  ValueObject &valobj = GetMostSpecializedValue();

  // always show the type at the root level if it is invalid
  if (show_type) {
    // Some ValueObjects don't have types (like registers sets). Only print the
    // type if there is one to print
    ConstString type_name;
    if (m_compiler_type.IsValid()) {
      type_name = m_options.m_use_type_display_name
                      ? valobj.GetDisplayTypeName()
                      : valobj.GetQualifiedTypeName();
    } else {
      // only show an invalid type name if the user explicitly triggered
      // show_type
      if (m_options.m_show_types)
        type_name = ConstString("<invalid type>");
    }

    if (type_name) {
      std::string type_name_str(type_name.GetCString());
      if (m_options.m_hide_pointer_value) {
        for (auto iter = type_name_str.find(" *"); iter != std::string::npos;
             iter = type_name_str.find(" *")) {
          type_name_str.erase(iter, 2);
        }
      }
      typeName << type_name_str.c_str();
    }
  }

  StreamString varName;

  if (ShouldShowName()) {
    if (m_options.m_flat_output)
      valobj.GetExpressionPath(varName);
    else
      varName << GetRootNameForDisplay();
  }

  bool decl_printed = false;
  if (!m_options.m_decl_printing_helper) {
    // if the user didn't give us a custom helper, pick one based upon the
    // language, either the one that this printer is bound to, or the preferred
    // one for the ValueObject
    lldb::LanguageType lang_type =
        (m_options.m_varformat_language == lldb::eLanguageTypeUnknown)
            ? valobj.GetPreferredDisplayLanguage()
            : m_options.m_varformat_language;
    if (Language *lang_plugin = Language::FindPlugin(lang_type)) {
      m_options.m_decl_printing_helper = lang_plugin->GetDeclPrintingHelper();
    }
  }

  if (m_options.m_decl_printing_helper) {
    ConstString type_name_cstr(typeName.GetString());
    ConstString var_name_cstr(varName.GetString());

    DumpValueObjectOptions decl_print_options = m_options;
    // Pass printing helpers an option object that indicates whether the name
    // should be shown or hidden.
    decl_print_options.SetHideName(!ShouldShowName());

    StreamString dest_stream;
    if (m_options.m_decl_printing_helper(type_name_cstr, var_name_cstr,
                                         decl_print_options, dest_stream)) {
      decl_printed = true;
      m_stream->PutCString(dest_stream.GetString());
    }
  }

  // if the helper failed, or there is none, do a default thing
  if (!decl_printed) {
    if (!typeName.Empty())
      m_stream->Printf("(%s) ", typeName.GetData());
    if (!varName.Empty())
      m_stream->Printf("%s =", varName.GetData());
    else if (ShouldShowName())
      m_stream->Printf(" =");
  }
}

bool ValueObjectPrinter::CheckScopeIfNeeded() {
  if (m_options.m_scope_already_checked)
    return true;
  return GetMostSpecializedValue().IsInScope();
}

TypeSummaryImpl *ValueObjectPrinter::GetSummaryFormatter(bool null_if_omitted) {
  if (!m_summary_formatter.second) {
    TypeSummaryImpl *entry =
        m_options.m_summary_sp
            ? m_options.m_summary_sp.get()
            : GetMostSpecializedValue().GetSummaryFormat().get();

    if (m_options.m_omit_summary_depth > 0)
      entry = nullptr;
    m_summary_formatter.first = entry;
    m_summary_formatter.second = true;
  }
  if (m_options.m_omit_summary_depth > 0 && null_if_omitted)
    return nullptr;
  return m_summary_formatter.first;
}

static bool IsPointerValue(const CompilerType &type) {
  Flags type_flags(type.GetTypeInfo());
  if (type_flags.AnySet(eTypeInstanceIsPointer | eTypeIsPointer))
    return type_flags.AllClear(eTypeIsBuiltIn);
  return false;
}

void ValueObjectPrinter::GetValueSummaryError(std::string &value,
                                              std::string &summary,
                                              std::string &error) {
  lldb::Format format = m_options.m_format;
  ValueObject &valobj = GetMostSpecializedValue();
  // if I am printing synthetized elements, apply the format to those elements
  // only
  if (m_options.m_pointer_as_array)
    valobj.GetValueAsCString(lldb::eFormatDefault, value);
  else if (format != eFormatDefault && format != valobj.GetFormat())
    valobj.GetValueAsCString(format, value);
  else {
    const char *val_cstr = valobj.GetValueAsCString();
    if (val_cstr)
      value.assign(val_cstr);
  }
  const char *err_cstr = valobj.GetError().AsCString();
  if (err_cstr)
    error.assign(err_cstr);

  if (!ShouldPrintValueObject())
    return;

  if (IsNil()) {
    lldb::LanguageType lang_type =
        (m_options.m_varformat_language == lldb::eLanguageTypeUnknown)
            ? valobj.GetPreferredDisplayLanguage()
            : m_options.m_varformat_language;
    if (Language *lang_plugin = Language::FindPlugin(lang_type)) {
      summary.assign(lang_plugin->GetNilReferenceSummaryString().str());
    } else {
      // We treat C as the fallback language rather than as a separate Language
      // plugin.
      summary.assign("NULL");
    }
  } else if (IsUninitialized()) {
    summary.assign("<uninitialized>");
  } else if (m_options.m_omit_summary_depth == 0) {
    TypeSummaryImpl *entry = GetSummaryFormatter();
    if (entry) {
      valobj.GetSummaryAsCString(entry, summary,
                                 m_options.m_varformat_language);
    } else {
      const char *sum_cstr =
          valobj.GetSummaryAsCString(m_options.m_varformat_language);
      if (sum_cstr)
        summary.assign(sum_cstr);
    }
  }
}

bool ValueObjectPrinter::PrintValueAndSummaryIfNeeded(bool &value_printed,
                                                      bool &summary_printed) {
  bool error_printed = false;
  if (ShouldPrintValueObject()) {
    if (!CheckScopeIfNeeded())
      m_error.assign("out of scope");
    if (m_error.empty()) {
      GetValueSummaryError(m_value, m_summary, m_error);
    }
    if (m_error.size()) {
      // we need to support scenarios in which it is actually fine for a value
      // to have no type but - on the other hand - if we get an error *AND*
      // have no type, we try to get out gracefully, since most often that
      // combination means "could not resolve a type" and the default failure
      // mode is quite ugly
      if (!m_compiler_type.IsValid()) {
        m_stream->Printf(" <could not resolve type>");
        return false;
      }

      error_printed = true;
      m_stream->Printf(" <%s>\n", m_error.c_str());
    } else {
      // Make sure we have a value and make sure the summary didn't specify
      // that the value should not be printed - and do not print the value if
      // this thing is nil (but show the value if the user passes a format
      // explicitly)
      TypeSummaryImpl *entry = GetSummaryFormatter();
      ValueObject &valobj = GetMostSpecializedValue();
      const bool has_nil_or_uninitialized_summary =
          (IsNil() || IsUninitialized()) && !m_summary.empty();
      if (!has_nil_or_uninitialized_summary && !m_value.empty() &&
          (entry == nullptr ||
           (entry->DoesPrintValue(&valobj) ||
            m_options.m_format != eFormatDefault) ||
           m_summary.empty()) &&
          !m_options.m_hide_value) {
        if (m_options.m_hide_pointer_value &&
            IsPointerValue(valobj.GetCompilerType())) {
        } else {
          if (ShouldShowName())
            m_stream->PutChar(' ');
          m_stream->PutCString(m_value);
          value_printed = true;
        }
      }

      if (m_summary.size()) {
        if (ShouldShowName() || value_printed)
          m_stream->PutChar(' ');
        m_stream->PutCString(m_summary);
        summary_printed = true;
      }
    }
  }
  return !error_printed;
}

llvm::Error
ValueObjectPrinter::PrintObjectDescriptionIfNeeded(bool value_printed,
                                                   bool summary_printed) {
  if (ShouldPrintValueObject()) {
    // let's avoid the overly verbose no description error for a nil thing
    if (m_options.m_use_objc && !IsNil() && !IsUninitialized() &&
        (!m_options.m_pointer_as_array)) {
      if (!m_options.m_hide_value || ShouldShowName())
        *m_stream << ' ';
      llvm::Expected<std::string> object_desc =
          (value_printed || summary_printed)
              ? GetMostSpecializedValue().GetObjectDescription()
              : GetDescriptionForDisplay();
      if (!object_desc) {
        // If no value or summary was printed, surface the error.
        if (!value_printed && !summary_printed)
          return object_desc.takeError();
        // Otherwise gently nudge the user that they should have used
        // `p` instead of `po`. Unfortunately we cannot be more direct
        // about this, because we don't actually know what the user did.
        *m_stream << "warning: no object description available\n";
        llvm::consumeError(object_desc.takeError());
      } else {
        *m_stream << *object_desc;
        // If the description already ends with a \n don't add another one.
        if (object_desc->empty() || object_desc->back() != '\n')
          *m_stream << '\n';
      }
      return llvm::Error::success();
    }
  }
  return llvm::Error::success();
}

bool DumpValueObjectOptions::PointerDepth::CanAllowExpansion() const {
  switch (m_mode) {
  case Mode::Always:
  case Mode::Default:
    return m_count > 0;
  case Mode::Never:
    return false;
  }
  return false;
}

bool ValueObjectPrinter::ShouldPrintChildren(
    DumpValueObjectOptions::PointerDepth &curr_ptr_depth) {
  const bool is_ref = IsRef();
  const bool is_ptr = IsPtr();
  const bool is_uninit = IsUninitialized();

  if (is_uninit)
    return false;

  // If we have reached the maximum depth we shouldn't print any more children.
  if (HasReachedMaximumDepth())
    return false;

  // if the user has specified an element count, always print children as it is
  // explicit user demand being honored
  if (m_options.m_pointer_as_array)
    return true;

  if (m_options.m_use_objc)
    return false;

  bool print_children = true;
  ValueObject &valobj = GetMostSpecializedValue();
  if (TypeSummaryImpl *type_summary = GetSummaryFormatter())
    print_children = type_summary->DoesPrintChildren(&valobj);

  // We will show children for all concrete types. We won't show pointer
  // contents unless a pointer depth has been specified. We won't reference
  // contents unless the reference is the root object (depth of zero).

  // Use a new temporary pointer depth in case we override the current
  // pointer depth below...

  if (is_ptr || is_ref) {
    // We have a pointer or reference whose value is an address. Make sure
    // that address is not NULL
    AddressType ptr_address_type;
    if (valobj.GetPointerValue(&ptr_address_type) == 0)
      return false;

    const bool is_root_level = m_curr_depth == 0;

    if (is_ref && is_root_level && print_children) {
      // If this is the root object (depth is zero) that we are showing and
      // it is a reference, and no pointer depth has been supplied print out
      // what it references. Don't do this at deeper depths otherwise we can
      // end up with infinite recursion...
      return true;
    }

    return curr_ptr_depth.CanAllowExpansion();
  }

  return print_children || m_summary.empty();
}

bool ValueObjectPrinter::ShouldExpandEmptyAggregates() {
  TypeSummaryImpl *entry = GetSummaryFormatter();

  if (!entry)
    return true;

  return entry->DoesPrintEmptyAggregates();
}

ValueObject &ValueObjectPrinter::GetValueObjectForChildrenGeneration() {
  return GetMostSpecializedValue();
}

void ValueObjectPrinter::PrintChildrenPreamble(bool value_printed,
                                               bool summary_printed) {
  if (m_options.m_flat_output) {
    if (ShouldPrintValueObject())
      m_stream->EOL();
  } else {
    if (ShouldPrintValueObject()) {
      if (IsRef()) {
        m_stream->PutCString(": ");
      } else if (value_printed || summary_printed || ShouldShowName()) {
        m_stream->PutChar(' ');
      }
      m_stream->PutCString("{\n");
    }
    m_stream->IndentMore();
  }
}

void ValueObjectPrinter::PrintChild(
    ValueObjectSP child_sp,
    const DumpValueObjectOptions::PointerDepth &curr_ptr_depth) {
  const uint32_t consumed_summary_depth = m_options.m_pointer_as_array ? 0 : 1;
  const bool does_consume_ptr_depth =
      ((IsPtr() && !m_options.m_pointer_as_array) || IsRef());

  DumpValueObjectOptions child_options(m_options);
  child_options.SetFormat(m_options.m_format)
      .SetSummary()
      .SetRootValueObjectName();
  child_options.SetScopeChecked(true)
      .SetHideName(m_options.m_hide_name)
      .SetHideValue(m_options.m_hide_value)
      .SetOmitSummaryDepth(child_options.m_omit_summary_depth > 1
                               ? child_options.m_omit_summary_depth -
                                     consumed_summary_depth
                               : 0)
      .SetElementCount(0);

  if (child_sp.get()) {
    auto ptr_depth = curr_ptr_depth;
    if (does_consume_ptr_depth)
      ptr_depth = curr_ptr_depth.Decremented();

    ValueObjectPrinter child_printer(*(child_sp.get()), m_stream, child_options,
                                     ptr_depth, m_curr_depth + 1,
                                     m_printed_instance_pointers);
    llvm::Error error = child_printer.PrintValueObject();
    if (error) {
      if (m_stream)
        *m_stream << "error: " << toString(std::move(error));
      else
        llvm::consumeError(std::move(error));
    }
  }
}

llvm::Expected<uint32_t>
ValueObjectPrinter::GetMaxNumChildrenToPrint(bool &print_dotdotdot) {
  ValueObject &synth_valobj = GetValueObjectForChildrenGeneration();

  if (m_options.m_pointer_as_array)
    return m_options.m_pointer_as_array.m_element_count;

  const uint32_t max_num_children =
      m_options.m_ignore_cap ? UINT32_MAX
                             : GetMostSpecializedValue()
                                   .GetTargetSP()
                                   ->GetMaximumNumberOfChildrenToDisplay();
  // Ask for one more child than the maximum to see if we should print "...".
  auto num_children_or_err = synth_valobj.GetNumChildren(
      llvm::SaturatingAdd(max_num_children, uint32_t(1)));
  if (!num_children_or_err)
    return num_children_or_err;
  if (*num_children_or_err > max_num_children) {
    print_dotdotdot = true;
    return max_num_children;
  }
  return num_children_or_err;
}

void ValueObjectPrinter::PrintChildrenPostamble(bool print_dotdotdot) {
  if (!m_options.m_flat_output) {
    if (print_dotdotdot) {
      GetMostSpecializedValue()
          .GetTargetSP()
          ->GetDebugger()
          .GetCommandInterpreter()
          .ChildrenTruncated();
      m_stream->Indent("...\n");
    }
    m_stream->IndentLess();
    m_stream->Indent("}\n");
  }
}

bool ValueObjectPrinter::ShouldPrintEmptyBrackets(bool value_printed,
                                                  bool summary_printed) {
  ValueObject &synth_valobj = GetValueObjectForChildrenGeneration();

  if (!IsAggregate())
    return false;

  if (!m_options.m_reveal_empty_aggregates) {
    if (value_printed || summary_printed)
      return false;
  }

  if (synth_valobj.MightHaveChildren())
    return true;

  if (m_val_summary_ok)
    return false;

  return true;
}

static constexpr size_t PhysicalIndexForLogicalIndex(size_t base, size_t stride,
                                                     size_t logical) {
  return base + logical * stride;
}

ValueObjectSP ValueObjectPrinter::GenerateChild(ValueObject &synth_valobj,
                                                size_t idx) {
  if (m_options.m_pointer_as_array) {
    // if generating pointer-as-array children, use GetSyntheticArrayMember
    return synth_valobj.GetSyntheticArrayMember(
        PhysicalIndexForLogicalIndex(
            m_options.m_pointer_as_array.m_base_element,
            m_options.m_pointer_as_array.m_stride, idx),
        true);
  } else {
    // otherwise, do the usual thing
    return synth_valobj.GetChildAtIndex(idx);
  }
}

void ValueObjectPrinter::PrintChildren(
    bool value_printed, bool summary_printed,
    const DumpValueObjectOptions::PointerDepth &curr_ptr_depth) {
  ValueObject &synth_valobj = GetValueObjectForChildrenGeneration();

  bool print_dotdotdot = false;
  auto num_children_or_err = GetMaxNumChildrenToPrint(print_dotdotdot);
  if (!num_children_or_err) {
    *m_stream << " <" << llvm::toString(num_children_or_err.takeError()) << '>';
    return;
  }
  uint32_t num_children = *num_children_or_err;
  if (num_children) {
    bool any_children_printed = false;

    for (size_t idx = 0; idx < num_children; ++idx) {
      if (ValueObjectSP child_sp = GenerateChild(synth_valobj, idx)) {
        if (m_options.m_child_printing_decider &&
            !m_options.m_child_printing_decider(child_sp->GetName()))
          continue;
        if (!any_children_printed) {
          PrintChildrenPreamble(value_printed, summary_printed);
          any_children_printed = true;
        }
        PrintChild(child_sp, curr_ptr_depth);
      }
    }

    if (any_children_printed)
      PrintChildrenPostamble(print_dotdotdot);
    else {
      if (ShouldPrintEmptyBrackets(value_printed, summary_printed)) {
        if (ShouldPrintValueObject())
          m_stream->PutCString(" {}\n");
        else
          m_stream->EOL();
      } else
        m_stream->EOL();
    }
  } else if (ShouldPrintEmptyBrackets(value_printed, summary_printed)) {
    // Aggregate, no children...
    if (ShouldPrintValueObject()) {
      // if it has a synthetic value, then don't print {}, the synthetic
      // children are probably only being used to vend a value
      if (GetMostSpecializedValue().DoesProvideSyntheticValue() ||
          !ShouldExpandEmptyAggregates())
        m_stream->PutCString("\n");
      else
        m_stream->PutCString(" {}\n");
    }
  } else {
    if (ShouldPrintValueObject())
      m_stream->EOL();
  }
}

bool ValueObjectPrinter::PrintChildrenOneLiner(bool hide_names) {
  ValueObject &synth_valobj = GetValueObjectForChildrenGeneration();

  bool print_dotdotdot = false;
  auto num_children_or_err = GetMaxNumChildrenToPrint(print_dotdotdot);
  if (!num_children_or_err) {
    *m_stream << '<' << llvm::toString(num_children_or_err.takeError()) << '>';
    return true;
  }
  uint32_t num_children = *num_children_or_err;

  if (num_children) {
    m_stream->PutChar('(');

    bool did_print_children = false;
    for (uint32_t idx = 0; idx < num_children; ++idx) {
      lldb::ValueObjectSP child_sp(synth_valobj.GetChildAtIndex(idx));
      if (child_sp)
        child_sp = child_sp->GetQualifiedRepresentationIfAvailable(
            m_options.m_use_dynamic, m_options.m_use_synthetic);
      if (child_sp) {
        if (m_options.m_child_printing_decider &&
            !m_options.m_child_printing_decider(child_sp->GetName()))
          continue;
        if (idx && did_print_children)
          m_stream->PutCString(", ");
        did_print_children = true;
        if (!hide_names) {
          const char *name = child_sp.get()->GetName().AsCString();
          if (name && *name) {
            m_stream->PutCString(name);
            m_stream->PutCString(" = ");
          }
        }
        child_sp->DumpPrintableRepresentation(
            *m_stream, ValueObject::eValueObjectRepresentationStyleSummary,
            m_options.m_format,
            ValueObject::PrintableRepresentationSpecialCases::eDisable);
      }
    }

    if (print_dotdotdot)
      m_stream->PutCString(", ...)");
    else
      m_stream->PutChar(')');
  }
  return true;
}

llvm::Error ValueObjectPrinter::PrintChildrenIfNeeded(bool value_printed,
                                                      bool summary_printed) {
  auto error = PrintObjectDescriptionIfNeeded(value_printed, summary_printed);
  if (error)
    return error;

  ValueObject &valobj = GetMostSpecializedValue();

  DumpValueObjectOptions::PointerDepth curr_ptr_depth = m_ptr_depth;
  const bool print_children = ShouldPrintChildren(curr_ptr_depth);
  const bool print_oneline =
      (curr_ptr_depth.CanAllowExpansion() || m_options.m_show_types ||
       !m_options.m_allow_oneliner_mode || m_options.m_flat_output ||
       (m_options.m_pointer_as_array) || m_options.m_show_location)
          ? false
          : DataVisualization::ShouldPrintAsOneLiner(valobj);
  if (print_children && IsInstancePointer()) {
    uint64_t instance_ptr_value = valobj.GetValueAsUnsigned(0);
    if (m_printed_instance_pointers->count(instance_ptr_value)) {
      // We already printed this instance-is-pointer thing, so don't expand it.
      m_stream->PutCString(" {...}\n");
      return llvm::Error::success();
    } else {
      // Remember this guy for future reference.
      m_printed_instance_pointers->emplace(instance_ptr_value);
    }
  }

  if (print_children) {
    if (print_oneline) {
      m_stream->PutChar(' ');
      PrintChildrenOneLiner(false);
      m_stream->EOL();
    } else
      PrintChildren(value_printed, summary_printed, curr_ptr_depth);
  } else if (HasReachedMaximumDepth() && IsAggregate() &&
             ShouldPrintValueObject()) {
    m_stream->PutCString("{...}\n");
    // The maximum child depth has been reached. If `m_max_depth` is the default
    // (i.e. the user has _not_ customized it), then lldb presents a warning to
    // the user. The warning tells the user that the limit has been reached, but
    // more importantly tells them how to expand the limit if desired.
    if (m_options.m_max_depth_is_default)
      valobj.GetTargetSP()
          ->GetDebugger()
          .GetCommandInterpreter()
          .SetReachedMaximumDepth();
  } else
    m_stream->EOL();
  return llvm::Error::success();
}

bool ValueObjectPrinter::HasReachedMaximumDepth() {
  return m_curr_depth >= m_options.m_max_depth;
}

bool ValueObjectPrinter::ShouldShowName() const {
  if (m_curr_depth == 0)
    return !m_options.m_hide_root_name && !m_options.m_hide_name;
  return !m_options.m_hide_name;
}
