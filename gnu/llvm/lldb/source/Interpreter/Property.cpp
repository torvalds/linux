//===-- Property.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/Property.h"

#include "lldb/Core/UserSettingsController.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValues.h"
#include "lldb/Target/Language.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

Property::Property(const PropertyDefinition &definition)
    : m_name(definition.name), m_description(definition.description),
      m_is_global(definition.global) {
  switch (definition.type) {
  case OptionValue::eTypeInvalid:
  case OptionValue::eTypeProperties:
    break;
  case OptionValue::eTypeArch:
    // "definition.default_uint_value" is not used
    // "definition.default_cstr_value" as a string value that represents the
    // default string value for the architecture/triple
    m_value_sp =
        std::make_shared<OptionValueArch>(definition.default_cstr_value);
    break;

  case OptionValue::eTypeArgs:
    // "definition.default_uint_value" is always a OptionValue::Type
    m_value_sp = std::make_shared<OptionValueArgs>();
    break;

  case OptionValue::eTypeArray:
    // "definition.default_uint_value" is always a OptionValue::Type
    m_value_sp =
        std::make_shared<OptionValueArray>(OptionValue::ConvertTypeToMask(
            (OptionValue::Type)definition.default_uint_value));
    break;

  case OptionValue::eTypeBoolean:
    // "definition.default_uint_value" is the default boolean value if
    // "definition.default_cstr_value" is NULL, otherwise interpret
    // "definition.default_cstr_value" as a string value that represents the
    // default value.
    if (definition.default_cstr_value)
      m_value_sp =
          std::make_shared<OptionValueBoolean>(OptionArgParser::ToBoolean(
              llvm::StringRef(definition.default_cstr_value), false, nullptr));
    else
      m_value_sp = std::make_shared<OptionValueBoolean>(
          definition.default_uint_value != 0);
    break;

  case OptionValue::eTypeChar: {
    llvm::StringRef s(definition.default_cstr_value ? definition.default_cstr_value : "");
    m_value_sp = std::make_shared<OptionValueChar>(
        OptionArgParser::ToChar(s, '\0', nullptr));
    break;
  }
  case OptionValue::eTypeDictionary:
    // "definition.default_uint_value" is always a OptionValue::Type
    m_value_sp = std::make_shared<OptionValueDictionary>(
        OptionValue::ConvertTypeToMask(
            (OptionValue::Type)definition.default_uint_value),
        definition.enum_values);
    break;

  case OptionValue::eTypeEnum:
    // "definition.default_uint_value" is the default enumeration value if
    // "definition.default_cstr_value" is NULL, otherwise interpret
    // "definition.default_cstr_value" as a string value that represents the
    // default value.
    {
      OptionValueEnumeration *enum_value = new OptionValueEnumeration(
          definition.enum_values, definition.default_uint_value);
      m_value_sp.reset(enum_value);
      if (definition.default_cstr_value) {
        if (enum_value
                ->SetValueFromString(
                    llvm::StringRef(definition.default_cstr_value))
                .Success()) {
          enum_value->SetDefaultValue(enum_value->GetCurrentValue());
          // Call Clear() since we don't want the value to appear as having
          // been set since we called SetValueFromString() above. Clear will
          // set the current value to the default and clear the boolean that
          // says that the value has been set.
          enum_value->Clear();
        }
      }
    }
    break;

  case OptionValue::eTypeFileLineColumn:
    // "definition.default_uint_value" is not used for a
    // OptionValue::eTypeFileSpecList
    m_value_sp = std::make_shared<OptionValueFileColonLine>();
    break;

  case OptionValue::eTypeFileSpec: {
    // "definition.default_uint_value" represents if the
    // "definition.default_cstr_value" should be resolved or not
    const bool resolve = definition.default_uint_value != 0;
    FileSpec file_spec = FileSpec(definition.default_cstr_value);
    if (resolve)
      FileSystem::Instance().Resolve(file_spec);
    m_value_sp = std::make_shared<OptionValueFileSpec>(file_spec, resolve);
    break;
  }

  case OptionValue::eTypeFileSpecList:
    // "definition.default_uint_value" is not used for a
    // OptionValue::eTypeFileSpecList
    m_value_sp = std::make_shared<OptionValueFileSpecList>();
    break;

  case OptionValue::eTypeFormat:
    // "definition.default_uint_value" is the default format enumeration value
    // if "definition.default_cstr_value" is NULL, otherwise interpret
    // "definition.default_cstr_value" as a string value that represents the
    // default value.
    {
      Format new_format = eFormatInvalid;
      if (definition.default_cstr_value)
        OptionArgParser::ToFormat(definition.default_cstr_value, new_format,
                                  nullptr);
      else
        new_format = (Format)definition.default_uint_value;
      m_value_sp = std::make_shared<OptionValueFormat>(new_format);
    }
    break;

  case OptionValue::eTypeLanguage:
    // "definition.default_uint_value" is the default language enumeration
    // value if "definition.default_cstr_value" is NULL, otherwise interpret
    // "definition.default_cstr_value" as a string value that represents the
    // default value.
    {
      LanguageType new_lang = eLanguageTypeUnknown;
      if (definition.default_cstr_value)
        Language::GetLanguageTypeFromString(
            llvm::StringRef(definition.default_cstr_value));
      else
        new_lang = (LanguageType)definition.default_uint_value;
      m_value_sp = std::make_shared<OptionValueLanguage>(new_lang);
    }
    break;

  case OptionValue::eTypeFormatEntity:
    // "definition.default_cstr_value" as a string value that represents the
    // default
    m_value_sp = std::make_shared<OptionValueFormatEntity>(
        definition.default_cstr_value);
    break;

  case OptionValue::eTypePathMap:
    // "definition.default_uint_value" tells us if notifications should occur
    // for path mappings
    m_value_sp = std::make_shared<OptionValuePathMappings>(
        definition.default_uint_value != 0);
    break;

  case OptionValue::eTypeRegex:
    // "definition.default_uint_value" is used to the regular expression flags
    // "definition.default_cstr_value" the default regular expression value
    // value.
    m_value_sp =
        std::make_shared<OptionValueRegex>(definition.default_cstr_value);
    break;

  case OptionValue::eTypeSInt64: {
    // "definition.default_uint_value" is the default integer value if
    // "definition.default_cstr_value" is NULL, otherwise interpret
    // "definition.default_cstr_value" as a string value that represents the
    // default value.
    int64_t value = 0;
    // FIXME: improve error handling for llvm::to_integer()
    if (definition.default_cstr_value)
      llvm::to_integer(definition.default_cstr_value, value);
    m_value_sp = std::make_shared<OptionValueSInt64>(
        definition.default_cstr_value ? value : definition.default_uint_value);
    break;
  }
  case OptionValue::eTypeUInt64: {
    uint64_t value = 0;
    // FIXME: improve error handling for llvm::to_integer()
    if (definition.default_cstr_value)
      llvm::to_integer(definition.default_cstr_value, value);
    // "definition.default_uint_value" is the default unsigned integer value if
    // "definition.default_cstr_value" is NULL, otherwise interpret
    // "definition.default_cstr_value" as a string value that represents the
    // default value.
    m_value_sp = std::make_shared<OptionValueUInt64>(
        definition.default_cstr_value ? value : definition.default_uint_value);
    break;
  }
  case OptionValue::eTypeUUID:
    // "definition.default_uint_value" is not used for a OptionValue::eTypeUUID
    // "definition.default_cstr_value" can contain a default UUID value
    {
      UUID uuid;
      if (definition.default_cstr_value)
        uuid.SetFromStringRef(definition.default_cstr_value);
      m_value_sp = std::make_shared<OptionValueUUID>(uuid);
    }
    break;

  case OptionValue::eTypeString:
    // "definition.default_uint_value" can contain the string option flags
    // OR'ed together "definition.default_cstr_value" can contain a default
    // string value
    {
      OptionValueString *string_value =
          new OptionValueString(definition.default_cstr_value);
      if (definition.default_uint_value != 0)
        string_value->GetOptions().Reset(definition.default_uint_value);
      m_value_sp.reset(string_value);
    }
    break;
  }
  assert(m_value_sp && "invalid property definition");
}

Property::Property(llvm::StringRef name, llvm::StringRef desc, bool is_global,
                   const lldb::OptionValueSP &value_sp)
    : m_name(name), m_description(desc), m_value_sp(value_sp),
      m_is_global(is_global) {}

bool Property::DumpQualifiedName(Stream &strm) const {
  if (!m_name.empty()) {
    if (m_value_sp->DumpQualifiedName(strm))
      strm.PutChar('.');
    strm << m_name;
    return true;
  }
  return false;
}

void Property::Dump(const ExecutionContext *exe_ctx, Stream &strm,
                    uint32_t dump_mask) const {
  if (m_value_sp) {
    const bool dump_desc = dump_mask & OptionValue::eDumpOptionDescription;
    const bool dump_cmd = dump_mask & OptionValue::eDumpOptionCommand;
    const bool transparent = m_value_sp->ValueIsTransparent();
    if (dump_cmd && !transparent)
      strm << "settings set -f ";
    if (dump_desc || !transparent) {
      if ((dump_mask & OptionValue::eDumpOptionName) && !m_name.empty()) {
        DumpQualifiedName(strm);
        if (dump_mask & ~OptionValue::eDumpOptionName)
          strm.PutChar(' ');
      }
    }
    if (dump_desc) {
      llvm::StringRef desc = GetDescription();
      if (!desc.empty())
        strm << "-- " << desc;

      if (transparent && (dump_mask == (OptionValue::eDumpOptionName |
                                        OptionValue::eDumpOptionDescription)))
        strm.EOL();
    }
    m_value_sp->DumpValue(exe_ctx, strm, dump_mask);
  }
}

void Property::DumpDescription(CommandInterpreter &interpreter, Stream &strm,
                               uint32_t output_width,
                               bool display_qualified_name) const {
  if (!m_value_sp)
    return;
  llvm::StringRef desc = GetDescription();

  if (desc.empty())
    return;

  StreamString qualified_name;
  const OptionValueProperties *sub_properties = m_value_sp->GetAsProperties();
  if (sub_properties) {
    strm.EOL();

    if (m_value_sp->DumpQualifiedName(qualified_name))
      strm.Printf("'%s' variables:\n\n", qualified_name.GetData());
    sub_properties->DumpAllDescriptions(interpreter, strm);
  } else {
    if (display_qualified_name) {
      StreamString qualified_name;
      DumpQualifiedName(qualified_name);
      interpreter.OutputFormattedHelpText(strm, qualified_name.GetString(),
                                          "--", desc, output_width);
    } else {
      interpreter.OutputFormattedHelpText(strm, m_name, "--", desc,
                                          output_width);
    }
  }
}

void Property::SetValueChangedCallback(std::function<void()> callback) {
  if (m_value_sp)
    m_value_sp->SetValueChangedCallback(std::move(callback));
}
