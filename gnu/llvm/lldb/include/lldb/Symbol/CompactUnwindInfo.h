//===-- CompactUnwindInfo.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_COMPACTUNWINDINFO_H
#define LLDB_SYMBOL_COMPACTUNWINDINFO_H

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/lldb-private.h"
#include <mutex>
#include <vector>

namespace lldb_private {

// Compact Unwind info is an unwind format used on Darwin.  The unwind
// instructions for typical compiler-generated functions can be expressed in a
// 32-bit encoding. The format includes a two-level index so the unwind
// information for a function can be found by two binary searches in the
// section.  It can represent both stack frames that use a frame-pointer
// register and frameless functions, on i386/x86_64 for instance.  When a
// function is too complex to be represented in the compact unwind format, it
// calls out to eh_frame unwind instructions.

// On Mac OS X / iOS, a function will have either a compact unwind
// representation or an eh_frame representation.  If lldb is going to benefit
// from the compiler's description about saved register locations, it must be
// able to read both sources of information.

class CompactUnwindInfo {
public:
  CompactUnwindInfo(ObjectFile &objfile, lldb::SectionSP &section);

  ~CompactUnwindInfo();

  bool GetUnwindPlan(Target &target, Address addr, UnwindPlan &unwind_plan);

  bool IsValid(const lldb::ProcessSP &process_sp);

private:
  // The top level index entries of the compact unwind info
  //   (internal representation of struct
  //   unwind_info_section_header_index_entry)
  // There are relatively few of these (one per 500/1000 functions, depending
  // on format) so creating them on first scan will not be too costly.
  struct UnwindIndex {
    uint32_t function_offset = 0; // The offset of the first function covered by
                                  // this index
    uint32_t second_level = 0;    // The offset (inside unwind_info sect) to the
                                  // second level page for this index
    // (either UNWIND_SECOND_LEVEL_REGULAR or UNWIND_SECOND_LEVEL_COMPRESSED)
    uint32_t lsda_array_start = 0; // The offset (inside unwind_info sect) LSDA
                                   // array for this index
    uint32_t lsda_array_end =
        0; // The offset to the LSDA array for the NEXT index
    bool sentinal_entry = false; // There is an empty index at the end which
                                 // provides the upper bound of
    // function addresses that are described

    UnwindIndex() = default;

    bool operator<(const CompactUnwindInfo::UnwindIndex &rhs) const {
      return function_offset < rhs.function_offset;
    }

    bool operator==(const CompactUnwindInfo::UnwindIndex &rhs) const {
      return function_offset == rhs.function_offset;
    }
  };

  // An internal object used to store the information we retrieve about a
  // function -- the encoding bits and possibly the LSDA/personality function.
  struct FunctionInfo {
    uint32_t encoding = 0; // compact encoding 32-bit value for this function
    Address lsda_address; // the address of the LSDA data for this function
    Address personality_ptr_address; // the address where the personality
                                     // routine addr can be found

    uint32_t valid_range_offset_start = 0; // first offset that this encoding is
                                           // valid for (start of the function)
    uint32_t valid_range_offset_end =
        0; // the offset of the start of the next function
    FunctionInfo() = default;
  };

  struct UnwindHeader {
    uint32_t version;
    uint32_t common_encodings_array_offset = 0;
    uint32_t common_encodings_array_count = 0;
    uint32_t personality_array_offset = 0;
    uint32_t personality_array_count = 0;

    UnwindHeader() = default;
  };

  void ScanIndex(const lldb::ProcessSP &process_sp);

  bool GetCompactUnwindInfoForFunction(Target &target, Address address,
                                       FunctionInfo &unwind_info);

  lldb::offset_t
  BinarySearchRegularSecondPage(uint32_t entry_page_offset,
                                uint32_t entry_count, uint32_t function_offset,
                                uint32_t *entry_func_start_offset,
                                uint32_t *entry_func_end_offset);

  uint32_t BinarySearchCompressedSecondPage(uint32_t entry_page_offset,
                                            uint32_t entry_count,
                                            uint32_t function_offset_to_find,
                                            uint32_t function_offset_base,
                                            uint32_t *entry_func_start_offset,
                                            uint32_t *entry_func_end_offset);

  uint32_t GetLSDAForFunctionOffset(uint32_t lsda_offset, uint32_t lsda_count,
                                    uint32_t function_offset);

  bool CreateUnwindPlan_x86_64(Target &target, FunctionInfo &function_info,
                               UnwindPlan &unwind_plan,
                               Address pc_or_function_start);

  bool CreateUnwindPlan_i386(Target &target, FunctionInfo &function_info,
                             UnwindPlan &unwind_plan,
                             Address pc_or_function_start);

  bool CreateUnwindPlan_arm64(Target &target, FunctionInfo &function_info,
                              UnwindPlan &unwind_plan,
                              Address pc_or_function_start);

  bool CreateUnwindPlan_armv7(Target &target, FunctionInfo &function_info,
                              UnwindPlan &unwind_plan,
                              Address pc_or_function_start);

  ObjectFile &m_objfile;
  lldb::SectionSP m_section_sp;
  lldb::WritableDataBufferSP
      m_section_contents_if_encrypted; // if the binary is
                                       // encrypted, read the
                                       // sect contents
  // out of live memory and cache them here
  std::mutex m_mutex;
  std::vector<UnwindIndex> m_indexes;

  LazyBool m_indexes_computed; // eLazyBoolYes once we've tried to parse the
                               // unwind info
  // eLazyBoolNo means we cannot parse the unwind info & should not retry
  // eLazyBoolCalculate means we haven't tried to parse it yet

  DataExtractor m_unwindinfo_data;
  bool m_unwindinfo_data_computed; // true once we've mapped in the unwindinfo
                                   // data

  UnwindHeader m_unwind_header;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_COMPACTUNWINDINFO_H
