//===-- DWARFExpression.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_DWARFEXPRESSION_H
#define LLDB_EXPRESSION_DWARFEXPRESSION_H

#include "lldb/Core/Address.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private.h"
#include "llvm/DebugInfo/DWARF/DWARFLocationExpression.h"
#include <functional>

namespace lldb_private {

namespace plugin {
namespace dwarf {
class DWARFUnit;
} // namespace dwarf
} // namespace plugin

/// \class DWARFExpression DWARFExpression.h
/// "lldb/Expression/DWARFExpression.h" Encapsulates a DWARF location
/// expression and interprets it.
///
/// DWARF location expressions are used in two ways by LLDB.  The first
/// use is to find entities specified in the debug information, since their
/// locations are specified in precisely this language.  The second is to
/// interpret expressions without having to run the target in cases where the
/// overhead from copying JIT-compiled code into the target is too high or
/// where the target cannot be run.  This class encapsulates a single DWARF
/// location expression or a location list and interprets it.
class DWARFExpression {
public:
  DWARFExpression();

  /// Constructor
  ///
  /// \param[in] data
  ///     A data extractor configured to read the DWARF location expression's
  ///     bytecode.
  DWARFExpression(const DataExtractor &data);

  /// Destructor
  ~DWARFExpression();

  /// Return true if the location expression contains data
  bool IsValid() const;

  /// Return the address specified by the first
  /// DW_OP_{addr, addrx, GNU_addr_index} in the operation stream.
  ///
  /// \param[in] dwarf_cu
  ///     The dwarf unit this expression belongs to. Only required to resolve
  ///     DW_OP{addrx, GNU_addr_index}.
  ///
  /// \param[out] error
  ///     If the location stream contains unknown DW_OP opcodes or the
  ///     data is missing, \a error will be set to \b true.
  ///
  /// \return
  ///     The address specified by the operation, if the operation exists, or
  ///     LLDB_INVALID_ADDRESS otherwise.
  lldb::addr_t GetLocation_DW_OP_addr(const plugin::dwarf::DWARFUnit *dwarf_cu,
                                      bool &error) const;

  bool Update_DW_OP_addr(const plugin::dwarf::DWARFUnit *dwarf_cu,
                         lldb::addr_t file_addr);

  void UpdateValue(uint64_t const_value, lldb::offset_t const_value_byte_size,
                   uint8_t addr_byte_size);

  bool
  ContainsThreadLocalStorage(const plugin::dwarf::DWARFUnit *dwarf_cu) const;

  bool LinkThreadLocalStorage(
      const plugin::dwarf::DWARFUnit *dwarf_cu,
      std::function<lldb::addr_t(lldb::addr_t file_addr)> const
          &link_address_callback);

  /// Return the call-frame-info style register kind
  lldb::RegisterKind GetRegisterKind() const;

  /// Set the call-frame-info style register kind
  ///
  /// \param[in] reg_kind
  ///     The register kind.
  void SetRegisterKind(lldb::RegisterKind reg_kind);

  /// Evaluate a DWARF location expression in a particular context
  ///
  /// \param[in] exe_ctx
  ///     The execution context in which to evaluate the location
  ///     expression.  The location expression may access the target's
  ///     memory, especially if it comes from the expression parser.
  ///
  /// \param[in] opcode_ctx
  ///     The module which defined the expression.
  ///
  /// \param[in] opcodes
  ///     This is a static method so the opcodes need to be provided
  ///     explicitly.
  ///
  ///  \param[in] reg_ctx
  ///     An optional parameter which provides a RegisterContext for use
  ///     when evaluating the expression (i.e. for fetching register values).
  ///     Normally this will come from the ExecutionContext's StackFrame but
  ///     in the case where an expression needs to be evaluated while building
  ///     the stack frame list, this short-cut is available.
  ///
  /// \param[in] reg_set
  ///     The call-frame-info style register kind.
  ///
  /// \param[in] initial_value_ptr
  ///     A value to put on top of the interpreter stack before evaluating
  ///     the expression, if the expression is parametrized.  Can be NULL.
  ///
  /// \param[in] result
  ///     A value into which the result of evaluating the expression is
  ///     to be placed.
  ///
  /// \param[in] error_ptr
  ///     If non-NULL, used to report errors in expression evaluation.
  ///
  /// \return
  ///     True on success; false otherwise.  If error_ptr is non-NULL,
  ///     details of the failure are provided through it.
  static llvm::Expected<Value>
  Evaluate(ExecutionContext *exe_ctx, RegisterContext *reg_ctx,
           lldb::ModuleSP module_sp, const DataExtractor &opcodes,
           const plugin::dwarf::DWARFUnit *dwarf_cu,
           const lldb::RegisterKind reg_set, const Value *initial_value_ptr,
           const Value *object_address_ptr);

  static bool ParseDWARFLocationList(const plugin::dwarf::DWARFUnit *dwarf_cu,
                                     const DataExtractor &data,
                                     DWARFExpressionList *loc_list);

  bool GetExpressionData(DataExtractor &data) const {
    data = m_data;
    return data.GetByteSize() > 0;
  }

  void DumpLocation(Stream *s, lldb::DescriptionLevel level, ABI *abi) const;

  bool MatchesOperand(StackFrame &frame, const Instruction::Operand &op) const;

private:
  /// A data extractor capable of reading opcode bytes
  DataExtractor m_data;

  /// One of the defines that starts with LLDB_REGKIND_
  lldb::RegisterKind m_reg_kind = lldb::eRegisterKindDWARF;
};

} // namespace lldb_private

#endif // LLDB_EXPRESSION_DWARFEXPRESSION_H
