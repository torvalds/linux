//===-- PdbFPOProgramToDWARFExpression.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PdbFPOProgramToDWARFExpression.h"
#include "CodeViewRegisterMapping.h"

#include "lldb/Symbol/PostfixExpression.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Stream.h"
#include "llvm/ADT/DenseMap.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::postfix;

static uint32_t ResolveLLDBRegisterNum(llvm::StringRef reg_name, llvm::Triple::ArchType arch_type) {
  // lookup register name to get lldb register number
  llvm::codeview::CPUType cpu_type;
  switch (arch_type) {
    case llvm::Triple::ArchType::aarch64:
      cpu_type = llvm::codeview::CPUType::ARM64;
      break;

    default:
      cpu_type = llvm::codeview::CPUType::X64;
      break;
  }

  llvm::ArrayRef<llvm::EnumEntry<uint16_t>> register_names =
      llvm::codeview::getRegisterNames(cpu_type);
  auto it = llvm::find_if(
      register_names,
      [&reg_name](const llvm::EnumEntry<uint16_t> &register_entry) {
        return reg_name.compare_insensitive(register_entry.Name) == 0;
      });

  if (it == register_names.end())
    return LLDB_INVALID_REGNUM;

  auto reg_id = static_cast<llvm::codeview::RegisterId>(it->Value);
  return npdb::GetLLDBRegisterNumber(arch_type, reg_id);
}

static Node *ResolveFPOProgram(llvm::StringRef program,
                             llvm::StringRef register_name,
                             llvm::Triple::ArchType arch_type,
                             llvm::BumpPtrAllocator &alloc) {
  std::vector<std::pair<llvm::StringRef, Node *>> parsed =
      postfix::ParseFPOProgram(program, alloc);

  for (auto it = parsed.begin(), end = parsed.end(); it != end; ++it) {
    // Emplace valid dependent subtrees to make target assignment independent
    // from predecessors. Resolve all other SymbolNodes as registers.
    bool success =
        ResolveSymbols(it->second, [&](SymbolNode &symbol) -> Node * {
          for (const auto &pair : llvm::make_range(parsed.begin(), it)) {
            if (pair.first == symbol.GetName())
              return pair.second;
          }

          uint32_t reg_num =
              ResolveLLDBRegisterNum(symbol.GetName().drop_front(1), arch_type);

          if (reg_num == LLDB_INVALID_REGNUM)
            return nullptr;

          return MakeNode<RegisterNode>(alloc, reg_num);
        });
    if (!success)
      return nullptr;

    if (it->first == register_name) {
      // found target assignment program - no need to parse further
      return it->second;
    }
  }

  return nullptr;
}

bool lldb_private::npdb::TranslateFPOProgramToDWARFExpression(
    llvm::StringRef program, llvm::StringRef register_name,
    llvm::Triple::ArchType arch_type, Stream &stream) {
  llvm::BumpPtrAllocator node_alloc;
  Node *target_program =
      ResolveFPOProgram(program, register_name, arch_type, node_alloc);
  if (target_program == nullptr) {
    return false;
  }

  ToDWARF(*target_program, stream);
  return true;
}
