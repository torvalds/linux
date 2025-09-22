//===-- NSString.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NSString.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

std::map<ConstString, CXXFunctionSummaryFormat::Callback> &
NSString_Additionals::GetAdditionalSummaries() {
  static std::map<ConstString, CXXFunctionSummaryFormat::Callback> g_map;
  return g_map;
}

bool lldb_private::formatters::NSStringSummaryProvider(
    ValueObject &valobj, Stream &stream,
    const TypeSummaryOptions &summary_options) {
  static constexpr llvm::StringLiteral g_TypeHint("NSString");

  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime = ObjCLanguageRuntime::Get(*process_sp);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor.get() || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  ConstString class_name_cs = descriptor->GetClassName();
  llvm::StringRef class_name = class_name_cs.GetStringRef();

  if (class_name.empty())
    return false;

  bool is_tagged_ptr = class_name == "NSTaggedPointerString" &&
                       descriptor->GetTaggedPointerInfo();
  // for a tagged pointer, the descriptor has everything we need
  if (is_tagged_ptr)
    return NSTaggedString_SummaryProvider(valobj, descriptor, stream,
                                          summary_options);

  auto &additionals_map(NSString_Additionals::GetAdditionalSummaries());
  auto iter = additionals_map.find(class_name_cs), end = additionals_map.end();
  if (iter != end)
    return iter->second(valobj, stream, summary_options);

  // if not a tagged pointer that we know about, try the normal route
  uint64_t info_bits_location = valobj_addr + ptr_size;
  if (process_sp->GetByteOrder() != lldb::eByteOrderLittle)
    info_bits_location += 3;

  Status error;

  uint8_t info_bits = process_sp->ReadUnsignedIntegerFromMemory(
      info_bits_location, 1, 0, error);
  if (error.Fail())
    return false;

  bool is_mutable = (info_bits & 1) == 1;
  bool is_inline = (info_bits & 0x60) == 0;
  bool has_explicit_length = (info_bits & (1 | 4)) != 4;
  bool is_unicode = (info_bits & 0x10) == 0x10;
  bool is_path_store = class_name == "NSPathStore2";
  bool has_null = (info_bits & 8) == 8;

  size_t explicit_length = 0;
  if (!has_null && has_explicit_length && !is_path_store) {
    lldb::addr_t explicit_length_offset = 2 * ptr_size;
    if (is_mutable && !is_inline)
      explicit_length_offset =
          explicit_length_offset + ptr_size; //  notInlineMutable.length;
    else if (is_inline)
      explicit_length = explicit_length + 0; // inline1.length;
    else if (!is_inline && !is_mutable)
      explicit_length_offset =
          explicit_length_offset + ptr_size; // notInlineImmutable1.length;
    else
      explicit_length_offset = 0;

    if (explicit_length_offset) {
      explicit_length_offset = valobj_addr + explicit_length_offset;
      explicit_length = process_sp->ReadUnsignedIntegerFromMemory(
          explicit_length_offset, 4, 0, error);
    }
  }

  const llvm::StringSet<> supported_string_classes = {
      "NSString",     "CFMutableStringRef",
      "CFStringRef",  "__NSCFConstantString",
      "__NSCFString", "NSCFConstantString",
      "NSCFString",   "NSPathStore2"};
  if (supported_string_classes.count(class_name) == 0) {
    // not one of us - but tell me class name
    stream.Printf("class name = %s", class_name_cs.GetCString());
    return true;
  }

  llvm::StringRef prefix, suffix;
  if (Language *language = Language::FindPlugin(summary_options.GetLanguage()))
    std::tie(prefix, suffix) = language->GetFormatterPrefixSuffix(g_TypeHint);

  StringPrinter::ReadStringAndDumpToStreamOptions options(valobj);
  options.SetPrefixToken(prefix.str());
  options.SetSuffixToken(suffix.str());

  if (is_mutable) {
    uint64_t location = 2 * ptr_size + valobj_addr;
    location = process_sp->ReadPointerFromMemory(location, error);
    if (error.Fail())
      return false;
    if (has_explicit_length && is_unicode) {
      options.SetLocation(location);
      options.SetTargetSP(valobj.GetTargetSP());
      options.SetStream(&stream);
      options.SetQuote('"');
      options.SetSourceSize(explicit_length);
      options.SetHasSourceSize(has_explicit_length);
      options.SetNeedsZeroTermination(false);
      options.SetIgnoreMaxLength(summary_options.GetCapping() ==
                                 TypeSummaryCapping::eTypeSummaryUncapped);
      options.SetBinaryZeroIsTerminator(false);
      return StringPrinter::ReadStringAndDumpToStream<
          StringPrinter::StringElementType::UTF16>(options);
    } else {
      options.SetLocation(location + 1);
      options.SetTargetSP(valobj.GetTargetSP());
      options.SetStream(&stream);
      options.SetSourceSize(explicit_length);
      options.SetHasSourceSize(has_explicit_length);
      options.SetNeedsZeroTermination(false);
      options.SetIgnoreMaxLength(summary_options.GetCapping() ==
                                 TypeSummaryCapping::eTypeSummaryUncapped);
      options.SetBinaryZeroIsTerminator(false);
      return StringPrinter::ReadStringAndDumpToStream<
          StringPrinter::StringElementType::ASCII>(options);
    }
  } else if (is_inline && has_explicit_length && !is_unicode &&
             !is_path_store && !is_mutable) {
    uint64_t location = 3 * ptr_size + valobj_addr;

    options.SetLocation(location);
    options.SetTargetSP(valobj.GetTargetSP());
    options.SetStream(&stream);
    options.SetQuote('"');
    options.SetSourceSize(explicit_length);
    options.SetHasSourceSize(has_explicit_length);
    options.SetIgnoreMaxLength(summary_options.GetCapping() ==
                               TypeSummaryCapping::eTypeSummaryUncapped);
    return StringPrinter::ReadStringAndDumpToStream<
        StringPrinter::StringElementType::ASCII>(options);
  } else if (is_unicode) {
    uint64_t location = valobj_addr + 2 * ptr_size;
    if (is_inline) {
      if (!has_explicit_length) {
        return false;
      } else
        location += ptr_size;
    } else {
      location = process_sp->ReadPointerFromMemory(location, error);
      if (error.Fail())
        return false;
    }
    options.SetLocation(location);
    options.SetTargetSP(valobj.GetTargetSP());
    options.SetStream(&stream);
    options.SetQuote('"');
    options.SetSourceSize(explicit_length);
    options.SetHasSourceSize(has_explicit_length);
    options.SetNeedsZeroTermination(!has_explicit_length);
    options.SetIgnoreMaxLength(summary_options.GetCapping() ==
                               TypeSummaryCapping::eTypeSummaryUncapped);
    options.SetBinaryZeroIsTerminator(!has_explicit_length);
    return StringPrinter::ReadStringAndDumpToStream<
        StringPrinter::StringElementType::UTF16>(options);
  } else if (is_path_store) {
    // _lengthAndRefCount is the first ivar of NSPathStore2 (after the isa).
    uint64_t length_ivar_offset = 1 * ptr_size;
    CompilerType length_type = valobj.GetCompilerType().GetBasicTypeFromAST(
        lldb::eBasicTypeUnsignedInt);
    ValueObjectSP length_valobj_sp =
        valobj.GetSyntheticChildAtOffset(length_ivar_offset, length_type, true,
                                         ConstString("_lengthAndRefCount"));
    if (!length_valobj_sp)
      return false;
    // Get the length out of _lengthAndRefCount.
    explicit_length = length_valobj_sp->GetValueAsUnsigned(0) >> 20;
    lldb::addr_t location = valobj.GetValueAsUnsigned(0) + ptr_size + 4;

    options.SetLocation(location);
    options.SetTargetSP(valobj.GetTargetSP());
    options.SetStream(&stream);
    options.SetQuote('"');
    options.SetSourceSize(explicit_length);
    options.SetHasSourceSize(has_explicit_length);
    options.SetNeedsZeroTermination(!has_explicit_length);
    options.SetIgnoreMaxLength(summary_options.GetCapping() ==
                               TypeSummaryCapping::eTypeSummaryUncapped);
    options.SetBinaryZeroIsTerminator(!has_explicit_length);
    return StringPrinter::ReadStringAndDumpToStream<
        StringPrinter::StringElementType::UTF16>(options);
  } else if (is_inline) {
    uint64_t location = valobj_addr + 2 * ptr_size;
    if (!has_explicit_length) {
      // in this kind of string, the byte before the string content is a length
      // byte so let's try and use it to handle the embedded NUL case
      Status error;
      explicit_length =
          process_sp->ReadUnsignedIntegerFromMemory(location, 1, 0, error);
      has_explicit_length = !(error.Fail() || explicit_length == 0);
      location++;
    }
    options.SetLocation(location);
    options.SetTargetSP(valobj.GetTargetSP());
    options.SetStream(&stream);
    options.SetSourceSize(explicit_length);
    options.SetHasSourceSize(has_explicit_length);
    options.SetNeedsZeroTermination(!has_explicit_length);
    options.SetIgnoreMaxLength(summary_options.GetCapping() ==
                               TypeSummaryCapping::eTypeSummaryUncapped);
    options.SetBinaryZeroIsTerminator(!has_explicit_length);
    if (has_explicit_length)
      return StringPrinter::ReadStringAndDumpToStream<
          StringPrinter::StringElementType::UTF8>(options);
    else
      return StringPrinter::ReadStringAndDumpToStream<
          StringPrinter::StringElementType::ASCII>(options);
  } else {
    uint64_t location = valobj_addr + 2 * ptr_size;
    location = process_sp->ReadPointerFromMemory(location, error);
    if (error.Fail())
      return false;
    if (has_explicit_length && !has_null)
      explicit_length++; // account for the fact that there is no NULL and we
                         // need to have one added
    options.SetLocation(location);
    options.SetTargetSP(valobj.GetTargetSP());
    options.SetStream(&stream);
    options.SetSourceSize(explicit_length);
    options.SetHasSourceSize(has_explicit_length);
    options.SetIgnoreMaxLength(summary_options.GetCapping() ==
                               TypeSummaryCapping::eTypeSummaryUncapped);
    return StringPrinter::ReadStringAndDumpToStream<
        StringPrinter::StringElementType::ASCII>(options);
  }
}

bool lldb_private::formatters::NSAttributedStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  TargetSP target_sp(valobj.GetTargetSP());
  if (!target_sp)
    return false;
  uint32_t addr_size = target_sp->GetArchitecture().GetAddressByteSize();
  uint64_t pointer_value = valobj.GetValueAsUnsigned(0);
  if (!pointer_value)
    return false;
  pointer_value += addr_size;
  CompilerType type(valobj.GetCompilerType());
  ExecutionContext exe_ctx(target_sp, false);
  ValueObjectSP child_ptr_sp(valobj.CreateValueObjectFromAddress(
      "string_ptr", pointer_value, exe_ctx, type));
  if (!child_ptr_sp)
    return false;
  DataExtractor data;
  Status error;
  child_ptr_sp->GetData(data, error);
  if (error.Fail())
    return false;
  ValueObjectSP child_sp(child_ptr_sp->CreateValueObjectFromData(
      "string_data", data, exe_ctx, type));
  child_sp->GetValueAsUnsigned(0);
  if (child_sp)
    return NSStringSummaryProvider(*child_sp, stream, options);
  return false;
}

bool lldb_private::formatters::NSMutableAttributedStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  return NSAttributedStringSummaryProvider(valobj, stream, options);
}

bool lldb_private::formatters::NSTaggedString_SummaryProvider(
    ValueObject &valobj, ObjCLanguageRuntime::ClassDescriptorSP descriptor,
    Stream &stream, const TypeSummaryOptions &summary_options) {
  static constexpr llvm::StringLiteral g_TypeHint("NSString");

  if (!descriptor)
    return false;
  uint64_t len_bits = 0, data_bits = 0;
  if (!descriptor->GetTaggedPointerInfo(&len_bits, &data_bits, nullptr))
    return false;

  static const int g_MaxNonBitmaskedLen = 7; // TAGGED_STRING_UNPACKED_MAXLEN
  static const int g_SixbitMaxLen = 9;
  static const int g_fiveBitMaxLen = 11;

  static const char *sixBitToCharLookup = "eilotrm.apdnsIc ufkMShjTRxgC4013"
                                          "bDNvwyUL2O856P-B79AFKEWV_zGJ/HYX";

  if (len_bits > g_fiveBitMaxLen)
    return false;

  llvm::StringRef prefix, suffix;
  if (Language *language = Language::FindPlugin(summary_options.GetLanguage()))
    std::tie(prefix, suffix) = language->GetFormatterPrefixSuffix(g_TypeHint);

  // this is a fairly ugly trick - pretend that the numeric value is actually a
  // char* this works under a few assumptions: little endian architecture
  // sizeof(uint64_t) > g_MaxNonBitmaskedLen
  if (len_bits <= g_MaxNonBitmaskedLen) {
    stream << prefix;
    stream.Printf("\"%s\"", (const char *)&data_bits);
    stream << suffix;
    return true;
  }

  // if the data is bitmasked, we need to actually process the bytes
  uint8_t bitmask = 0;
  uint8_t shift_offset = 0;

  if (len_bits <= g_SixbitMaxLen) {
    bitmask = 0x03f;
    shift_offset = 6;
  } else {
    bitmask = 0x01f;
    shift_offset = 5;
  }

  std::vector<uint8_t> bytes;
  bytes.resize(len_bits);
  for (; len_bits > 0; data_bits >>= shift_offset, --len_bits) {
    uint8_t packed = data_bits & bitmask;
    bytes.insert(bytes.begin(), sixBitToCharLookup[packed]);
  }

  stream << prefix;
  stream.Printf("\"%s\"", &bytes[0]);
  stream << suffix;
  return true;
}
