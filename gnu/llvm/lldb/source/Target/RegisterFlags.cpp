//===-- RegisterFlags.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/RegisterFlags.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/StringExtras.h"

#include <limits>
#include <numeric>
#include <optional>

using namespace lldb_private;

RegisterFlags::Field::Field(std::string name, unsigned start, unsigned end)
    : m_name(std::move(name)), m_start(start), m_end(end),
      m_enum_type(nullptr) {
  assert(m_start <= m_end && "Start bit must be <= end bit.");
}

RegisterFlags::Field::Field(std::string name, unsigned bit_position)
    : m_name(std::move(name)), m_start(bit_position), m_end(bit_position),
      m_enum_type(nullptr) {}

RegisterFlags::Field::Field(std::string name, unsigned start, unsigned end,
                            const FieldEnum *enum_type)
    : m_name(std::move(name)), m_start(start), m_end(end),
      m_enum_type(enum_type) {
  if (m_enum_type) {
    // Check that all values fit into this field. The XML parser will also
    // do this check so at runtime nothing should fail this check.
    // We can also make enums in C++ at compile time, which might fail this
    // check, so we catch them before it makes it into a release.
    uint64_t max_value = GetMaxValue();
    UNUSED_IF_ASSERT_DISABLED(max_value);
    for (const auto &enumerator : m_enum_type->GetEnumerators()) {
      UNUSED_IF_ASSERT_DISABLED(enumerator);
      assert(enumerator.m_value <= max_value &&
             "Enumerator value exceeds maximum value for this field");
    }
  }
}

void RegisterFlags::Field::DumpToLog(Log *log) const {
  LLDB_LOG(log, "  Name: \"{0}\" Start: {1} End: {2}", m_name.c_str(), m_start,
           m_end);
}

bool RegisterFlags::Field::Overlaps(const Field &other) const {
  unsigned overlap_start = std::max(GetStart(), other.GetStart());
  unsigned overlap_end = std::min(GetEnd(), other.GetEnd());
  return overlap_start <= overlap_end;
}

unsigned RegisterFlags::Field::PaddingDistance(const Field &other) const {
  assert(!Overlaps(other) &&
         "Cannot get padding distance for overlapping fields.");
  assert((other < (*this)) && "Expected fields in MSB to LSB order.");

  // If they don't overlap they are either next to each other or separated
  // by some number of bits.

  // Where left will be the MSB and right will be the LSB.
  unsigned lhs_start = GetStart();
  unsigned rhs_end = other.GetStart() + other.GetSizeInBits() - 1;

  if (*this < other) {
    lhs_start = other.GetStart();
    rhs_end = GetStart() + GetSizeInBits() - 1;
  }

  return lhs_start - rhs_end - 1;
}

unsigned RegisterFlags::Field::GetSizeInBits(unsigned start, unsigned end) {
  return end - start + 1;
}

unsigned RegisterFlags::Field::GetSizeInBits() const {
  return GetSizeInBits(m_start, m_end);
}

uint64_t RegisterFlags::Field::GetMaxValue(unsigned start, unsigned end) {
  uint64_t max = std::numeric_limits<uint64_t>::max();
  unsigned bits = GetSizeInBits(start, end);
  // If the field is >= 64 bits the shift below would be undefined.
  // We assume the GDB client has discarded any field that would fail this
  // assert, it's only to check information we define directly in C++.
  assert(bits <= 64 && "Cannot handle field with size > 64 bits");
  if (bits < 64) {
    max = ((uint64_t)1 << bits) - 1;
  }
  return max;
}

uint64_t RegisterFlags::Field::GetMaxValue() const {
  return GetMaxValue(m_start, m_end);
}

uint64_t RegisterFlags::Field::GetMask() const {
  return GetMaxValue() << m_start;
}

void RegisterFlags::SetFields(const std::vector<Field> &fields) {
  // We expect that these are unsorted but do not overlap.
  // They could fill the register but may have gaps.
  std::vector<Field> provided_fields = fields;

  m_fields.clear();
  m_fields.reserve(provided_fields.size());

  // ProcessGDBRemote should have sorted these in descending order already.
  assert(std::is_sorted(provided_fields.rbegin(), provided_fields.rend()));

  // Build a new list of fields that includes anonymous (empty name) fields
  // wherever there is a gap. This will simplify processing later.
  std::optional<Field> previous_field;
  unsigned register_msb = (m_size * 8) - 1;
  for (auto field : provided_fields) {
    if (previous_field) {
      unsigned padding = previous_field->PaddingDistance(field);
      if (padding) {
        // -1 to end just before the previous field.
        unsigned end = previous_field->GetStart() - 1;
        // +1 because if you want to pad 1 bit you want to start and end
        // on the same bit.
        m_fields.push_back(Field("", field.GetEnd() + 1, end));
      }
    } else {
      // This is the first field. Check that it starts at the register's MSB.
      if (field.GetEnd() != register_msb)
        m_fields.push_back(Field("", field.GetEnd() + 1, register_msb));
    }
    m_fields.push_back(field);
    previous_field = field;
  }

  // The last field may not extend all the way to bit 0.
  if (previous_field && previous_field->GetStart() != 0)
    m_fields.push_back(Field("", 0, previous_field->GetStart() - 1));
}

RegisterFlags::RegisterFlags(std::string id, unsigned size,
                             const std::vector<Field> &fields)
    : m_id(std::move(id)), m_size(size) {
  SetFields(fields);
}

void RegisterFlags::DumpToLog(Log *log) const {
  LLDB_LOG(log, "ID: \"{0}\" Size: {1}", m_id.c_str(), m_size);
  for (const Field &field : m_fields)
    field.DumpToLog(log);
}

static StreamString FormatCell(const StreamString &content,
                               unsigned column_width) {
  unsigned pad = column_width - content.GetString().size();
  std::string pad_l;
  std::string pad_r;
  if (pad) {
    pad_l = std::string(pad / 2, ' ');
    pad_r = std::string((pad / 2) + (pad % 2), ' ');
  }

  StreamString aligned;
  aligned.Printf("|%s%s%s", pad_l.c_str(), content.GetString().data(),
                 pad_r.c_str());
  return aligned;
}

static void EmitTable(std::string &out, std::array<std::string, 3> &table) {
  // Close the table.
  for (std::string &line : table)
    line += '|';

  out += std::accumulate(table.begin() + 1, table.end(), table.front(),
                         [](std::string lhs, const auto &rhs) {
                           return std::move(lhs) + "\n" + rhs;
                         });
}

std::string RegisterFlags::AsTable(uint32_t max_width) const {
  std::string table;
  // position / gridline / name
  std::array<std::string, 3> lines;
  uint32_t current_width = 0;

  for (const RegisterFlags::Field &field : m_fields) {
    StreamString position;
    if (field.GetEnd() == field.GetStart())
      position.Printf(" %d ", field.GetEnd());
    else
      position.Printf(" %d-%d ", field.GetEnd(), field.GetStart());

    StreamString name;
    name.Printf(" %s ", field.GetName().c_str());

    unsigned column_width = position.GetString().size();
    unsigned name_width = name.GetString().size();
    if (name_width > column_width)
      column_width = name_width;

    // If the next column would overflow and we have already formatted at least
    // one column, put out what we have and move to a new table on the next line
    // (+1 here because we need to cap the ends with '|'). If this is the first
    // column, just let it overflow and we'll wrap next time around. There's not
    // much we can do with a very small terminal.
    if (current_width && ((current_width + column_width + 1) >= max_width)) {
      EmitTable(table, lines);
      // Blank line between each.
      table += "\n\n";

      for (std::string &line : lines)
        line.clear();
      current_width = 0;
    }

    StreamString aligned_position = FormatCell(position, column_width);
    lines[0] += aligned_position.GetString();
    StreamString grid;
    grid << '|' << std::string(column_width, '-');
    lines[1] += grid.GetString();
    StreamString aligned_name = FormatCell(name, column_width);
    lines[2] += aligned_name.GetString();

    // +1 for the left side '|'.
    current_width += column_width + 1;
  }

  // If we didn't overflow and still have table to print out.
  if (lines[0].size())
    EmitTable(table, lines);

  return table;
}

// Print enums as:
// value = name, value2 = name2
// Subject to the limits of the terminal width.
static void DumpEnumerators(StreamString &strm, size_t indent,
                            size_t current_width, uint32_t max_width,
                            const FieldEnum::Enumerators &enumerators) {
  for (auto it = enumerators.cbegin(); it != enumerators.cend(); ++it) {
    StreamString enumerator_strm;
    // The first enumerator of a line doesn't need to be separated.
    if (current_width != indent)
      enumerator_strm << ' ';

    enumerator_strm.Printf("%" PRIu64 " = %s", it->m_value, it->m_name.c_str());

    // Don't put "," after the last enumerator.
    if (std::next(it) != enumerators.cend())
      enumerator_strm << ",";

    llvm::StringRef enumerator_string = enumerator_strm.GetString();
    // If printing the next enumerator would take us over the width, start
    // a new line. However, if we're printing the first enumerator of this
    // line, don't start a new one. Resulting in there being at least one per
    // line.
    //
    // This means for very small widths we get:
    // A: 0 = foo,
    //    1 = bar
    // Instead of:
    // A:
    //    0 = foo,
    //    1 = bar
    if ((current_width + enumerator_string.size() > max_width) &&
        current_width != indent) {
      current_width = indent;
      strm << '\n' << std::string(indent, ' ');
      // We're going to a new line so we don't need a space before the
      // name of the enumerator.
      enumerator_string = enumerator_string.drop_front();
    }

    current_width += enumerator_string.size();
    strm << enumerator_string;
  }
}

std::string RegisterFlags::DumpEnums(uint32_t max_width) const {
  StreamString strm;
  bool printed_enumerators_once = false;

  for (const auto &field : m_fields) {
    const FieldEnum *enum_type = field.GetEnum();
    if (!enum_type)
      continue;

    const FieldEnum::Enumerators &enumerators = enum_type->GetEnumerators();
    if (enumerators.empty())
      continue;

    // Break between enumerators of different fields.
    if (printed_enumerators_once)
      strm << "\n\n";
    else
      printed_enumerators_once = true;

    std::string name_string = field.GetName() + ": ";
    size_t indent = name_string.size();
    size_t current_width = indent;

    strm << name_string;

    DumpEnumerators(strm, indent, current_width, max_width, enumerators);
  }

  return strm.GetString().str();
}

void RegisterFlags::EnumsToXML(Stream &strm, llvm::StringSet<> &seen) const {
  for (const Field &field : m_fields)
    if (const FieldEnum *enum_type = field.GetEnum()) {
      const std::string &id = enum_type->GetID();
      if (!seen.contains(id)) {
        enum_type->ToXML(strm, GetSize());
        seen.insert(id);
      }
    }
}

void FieldEnum::ToXML(Stream &strm, unsigned size) const {
  // Example XML:
  // <enum id="foo" size="4">
  //  <evalue name="bar" value="1"/>
  // </enum>
  // Note that "size" is only emitted for GDB compatibility, LLDB does not need
  // it.

  strm.Indent();
  strm << "<enum id=\"" << GetID() << "\" ";
  // This is the size of the underlying enum type if this were a C type.
  // In other words, the size of the register in bytes.
  strm.Printf("size=\"%d\"", size);

  const Enumerators &enumerators = GetEnumerators();
  if (enumerators.empty()) {
    strm << "/>\n";
    return;
  }

  strm << ">\n";
  strm.IndentMore();
  for (const auto &enumerator : enumerators) {
    strm.Indent();
    enumerator.ToXML(strm);
    strm.PutChar('\n');
  }
  strm.IndentLess();
  strm.Indent("</enum>\n");
}

void FieldEnum::Enumerator::ToXML(Stream &strm) const {
  std::string escaped_name;
  llvm::raw_string_ostream escape_strm(escaped_name);
  llvm::printHTMLEscaped(m_name, escape_strm);
  strm.Printf("<evalue name=\"%s\" value=\"%" PRIu64 "\"/>",
              escaped_name.c_str(), m_value);
}

void FieldEnum::Enumerator::DumpToLog(Log *log) const {
  LLDB_LOG(log, "  Name: \"{0}\" Value: {1}", m_name.c_str(), m_value);
}

void FieldEnum::DumpToLog(Log *log) const {
  LLDB_LOG(log, "ID: \"{0}\"", m_id.c_str());
  for (const auto &enumerator : GetEnumerators())
    enumerator.DumpToLog(log);
}

void RegisterFlags::ToXML(Stream &strm) const {
  // Example XML:
  // <flags id="cpsr_flags" size="4">
  //   <field name="incorrect" start="0" end="0"/>
  // </flags>
  strm.Indent();
  strm << "<flags id=\"" << GetID() << "\" ";
  strm.Printf("size=\"%d\"", GetSize());
  strm << ">";
  for (const Field &field : m_fields) {
    // Skip padding fields.
    if (field.GetName().empty())
      continue;

    strm << "\n";
    strm.IndentMore();
    field.ToXML(strm);
    strm.IndentLess();
  }
  strm.PutChar('\n');
  strm.Indent("</flags>\n");
}

void RegisterFlags::Field::ToXML(Stream &strm) const {
  // Example XML with an enum:
  // <field name="correct" start="0" end="0" type="some_enum">
  // Without:
  // <field name="correct" start="0" end="0"/>
  strm.Indent();
  strm << "<field name=\"";

  std::string escaped_name;
  llvm::raw_string_ostream escape_strm(escaped_name);
  llvm::printHTMLEscaped(GetName(), escape_strm);
  strm << escaped_name << "\" ";

  strm.Printf("start=\"%d\" end=\"%d\"", GetStart(), GetEnd());

  if (const FieldEnum *enum_type = GetEnum())
    strm << " type=\"" << enum_type->GetID() << "\"";

  strm << "/>";
}

FieldEnum::FieldEnum(std::string id, const Enumerators &enumerators)
    : m_id(id), m_enumerators(enumerators) {
  for (const auto &enumerator : m_enumerators) {
    UNUSED_IF_ASSERT_DISABLED(enumerator);
    assert(enumerator.m_name.size() && "Enumerator name cannot be empty");
  }
}