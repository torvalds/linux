//===-- DumpRegisterValue.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/DumpRegisterValue.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/DumpValueObjectOptions.h"
#include "lldb/Target/RegisterFlags.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-private-types.h"
#include "llvm/ADT/bit.h"

using namespace lldb;

template <typename T>
static void dump_type_value(lldb_private::CompilerType &fields_type, T value,
                            lldb_private::ExecutionContextScope *exe_scope,
                            const lldb_private::RegisterInfo &reg_info,
                            lldb_private::Stream &strm) {
  lldb::ByteOrder target_order = exe_scope->CalculateProcess()->GetByteOrder();

  // For the bitfield types we generate, it is expected that the fields are
  // in what is usually a big endian order. Most significant field first.
  // This is also clang's internal ordering and the order we want to print
  // them. On a big endian host this all matches up, for a little endian
  // host we have to swap the order of the fields before display.
  if (target_order == lldb::ByteOrder::eByteOrderLittle) {
    value = reg_info.flags_type->ReverseFieldOrder(value);
  }

  // Then we need to match the target's endian on a byte level as well.
  if (lldb_private::endian::InlHostByteOrder() != target_order)
    value = llvm::byteswap(value);

  lldb_private::DataExtractor data_extractor{
      &value, sizeof(T), lldb_private::endian::InlHostByteOrder(), 8};

  lldb::ValueObjectSP vobj_sp = lldb_private::ValueObjectConstResult::Create(
      exe_scope, fields_type, lldb_private::ConstString(), data_extractor);
  lldb_private::DumpValueObjectOptions dump_options;
  lldb_private::DumpValueObjectOptions::ChildPrintingDecider decider =
      [](lldb_private::ConstString varname) {
        // Unnamed bit-fields are padding that we don't want to show.
        return varname.GetLength();
      };
  dump_options.SetChildPrintingDecider(decider).SetHideRootType(true);

  if (llvm::Error error = vobj_sp->Dump(strm, dump_options))
    strm << "error: " << toString(std::move(error));
}

void lldb_private::DumpRegisterValue(const RegisterValue &reg_val, Stream &s,
                                     const RegisterInfo &reg_info,
                                     bool prefix_with_name,
                                     bool prefix_with_alt_name, Format format,
                                     uint32_t reg_name_right_align_at,
                                     ExecutionContextScope *exe_scope,
                                     bool print_flags, TargetSP target_sp) {
  DataExtractor data;
  if (!reg_val.GetData(data))
    return;

  bool name_printed = false;
  // For simplicity, alignment of the register name printing applies only in
  // the most common case where:
  //
  //     prefix_with_name^prefix_with_alt_name is true
  //
  StreamString format_string;
  if (reg_name_right_align_at && (prefix_with_name ^ prefix_with_alt_name))
    format_string.Printf("%%%us", reg_name_right_align_at);
  else
    format_string.Printf("%%s");
  std::string fmt = std::string(format_string.GetString());
  if (prefix_with_name) {
    if (reg_info.name) {
      s.Printf(fmt.c_str(), reg_info.name);
      name_printed = true;
    } else if (reg_info.alt_name) {
      s.Printf(fmt.c_str(), reg_info.alt_name);
      prefix_with_alt_name = false;
      name_printed = true;
    }
  }
  if (prefix_with_alt_name) {
    if (name_printed)
      s.PutChar('/');
    if (reg_info.alt_name) {
      s.Printf(fmt.c_str(), reg_info.alt_name);
      name_printed = true;
    } else if (!name_printed) {
      // No alternate name but we were asked to display a name, so show the
      // main name
      s.Printf(fmt.c_str(), reg_info.name);
      name_printed = true;
    }
  }
  if (name_printed)
    s.PutCString(" = ");

  if (format == eFormatDefault)
    format = reg_info.format;

  DumpDataExtractor(data, &s,
                    0,                    // Offset in "data"
                    format,               // Format to use when dumping
                    reg_info.byte_size,   // item_byte_size
                    1,                    // item_count
                    UINT32_MAX,           // num_per_line
                    LLDB_INVALID_ADDRESS, // base_addr
                    0,                    // item_bit_size
                    0,                    // item_bit_offset
                    exe_scope);

  if (!print_flags || !reg_info.flags_type || !exe_scope || !target_sp ||
      (reg_info.byte_size != 4 && reg_info.byte_size != 8))
    return;

  CompilerType fields_type = target_sp->GetRegisterType(
      reg_info.name, *reg_info.flags_type, reg_info.byte_size);

  // Use a new stream so we can remove a trailing newline later.
  StreamString fields_stream;

  if (reg_info.byte_size == 4) {
    dump_type_value(fields_type, reg_val.GetAsUInt32(), exe_scope, reg_info,
                    fields_stream);
  } else {
    dump_type_value(fields_type, reg_val.GetAsUInt64(), exe_scope, reg_info,
                    fields_stream);
  }

  // Registers are indented like:
  // (lldb) register read foo
  //     foo = 0x12345678
  // So we need to indent to match that.

  // First drop the extra newline that the value printer added. The register
  // command will add one itself.
  llvm::StringRef fields_str = fields_stream.GetString().drop_back();

  // End the line that contains "    foo = 0x12345678".
  s.EOL();

  // Then split the value lines and indent each one.
  bool first = true;
  while (fields_str.size()) {
    std::pair<llvm::StringRef, llvm::StringRef> split = fields_str.split('\n');
    fields_str = split.second;
    // Indent as far as the register name did.
    s.Printf(fmt.c_str(), "");

    // Lines after the first won't have " = " so compensate for that.
    if (!first)
      s << "   ";
    first = false;

    s << split.first;

    // On the last line we don't want a newline because the command will add
    // one too.
    if (fields_str.size())
      s.EOL();
  }
}
