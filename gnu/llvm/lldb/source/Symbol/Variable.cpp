//===-- Variable.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/Variable.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/Twine.h"

using namespace lldb;
using namespace lldb_private;

Variable::Variable(lldb::user_id_t uid, const char *name, const char *mangled,
                   const lldb::SymbolFileTypeSP &symfile_type_sp,
                   ValueType scope, SymbolContextScope *context,
                   const RangeList &scope_range, Declaration *decl_ptr,
                   const DWARFExpressionList &location_list, bool external,
                   bool artificial, bool location_is_constant_data,
                   bool static_member)
    : UserID(uid), m_name(name), m_mangled(ConstString(mangled)),
      m_symfile_type_sp(symfile_type_sp), m_scope(scope),
      m_owner_scope(context), m_scope_range(scope_range),
      m_declaration(decl_ptr), m_location_list(location_list), m_external(external),
      m_artificial(artificial), m_loc_is_const_data(location_is_constant_data),
      m_static_member(static_member) {}

Variable::~Variable() = default;

lldb::LanguageType Variable::GetLanguage() const {
  lldb::LanguageType lang = m_mangled.GuessLanguage();
  if (lang != lldb::eLanguageTypeUnknown)
    return lang;

  if (auto *func = m_owner_scope->CalculateSymbolContextFunction()) {
    if ((lang = func->GetLanguage()) != lldb::eLanguageTypeUnknown)
      return lang;
  } else if (auto *comp_unit =
                 m_owner_scope->CalculateSymbolContextCompileUnit()) {
    if ((lang = comp_unit->GetLanguage()) != lldb::eLanguageTypeUnknown)
      return lang;
  }

  return lldb::eLanguageTypeUnknown;
}

ConstString Variable::GetName() const {
  ConstString name = m_mangled.GetName();
  if (name)
    return name;
  return m_name;
}

ConstString Variable::GetUnqualifiedName() const { return m_name; }

bool Variable::NameMatches(ConstString name) const {
  if (m_name == name)
    return true;
  SymbolContext variable_sc;
  m_owner_scope->CalculateSymbolContext(&variable_sc);

  return m_mangled.NameMatches(name);
}
bool Variable::NameMatches(const RegularExpression &regex) const {
  if (regex.Execute(m_name.AsCString()))
    return true;
  if (m_mangled)
    return m_mangled.NameMatches(regex);
  return false;
}

Type *Variable::GetType() {
  if (m_symfile_type_sp)
    return m_symfile_type_sp->GetType();
  return nullptr;
}

void Variable::Dump(Stream *s, bool show_context) const {
  s->Printf("%p: ", static_cast<const void *>(this));
  s->Indent();
  *s << "Variable" << (const UserID &)*this;

  if (m_name)
    *s << ", name = \"" << m_name << "\"";

  if (m_symfile_type_sp) {
    Type *type = m_symfile_type_sp->GetType();
    if (type) {
      s->Format(", type = {{{0:x-16}} {1} (", type->GetID(), type);
      type->DumpTypeName(s);
      s->PutChar(')');
    }
  }

  if (m_scope != eValueTypeInvalid) {
    s->PutCString(", scope = ");
    switch (m_scope) {
    case eValueTypeVariableGlobal:
      s->PutCString(m_external ? "global" : "static");
      break;
    case eValueTypeVariableArgument:
      s->PutCString("parameter");
      break;
    case eValueTypeVariableLocal:
      s->PutCString("local");
      break;
    case eValueTypeVariableThreadLocal:
      s->PutCString("thread local");
      break;
    default:
      s->AsRawOstream() << "??? (" << m_scope << ')';
    }
  }

  if (show_context && m_owner_scope != nullptr) {
    s->PutCString(", context = ( ");
    m_owner_scope->DumpSymbolContext(s);
    s->PutCString(" )");
  }

  bool show_fullpaths = false;
  m_declaration.Dump(s, show_fullpaths);

  if (m_location_list.IsValid()) {
    s->PutCString(", location = ");
    ABISP abi;
    if (m_owner_scope) {
      ModuleSP module_sp(m_owner_scope->CalculateSymbolContextModule());
      if (module_sp)
        abi = ABI::FindPlugin(ProcessSP(), module_sp->GetArchitecture());
    }
    m_location_list.GetDescription(s, lldb::eDescriptionLevelBrief, abi.get());
  }

  if (m_external)
    s->PutCString(", external");

  if (m_artificial)
    s->PutCString(", artificial");

  s->EOL();
}

bool Variable::DumpDeclaration(Stream *s, bool show_fullpaths,
                               bool show_module) {
  bool dumped_declaration_info = false;
  if (m_owner_scope) {
    SymbolContext sc;
    m_owner_scope->CalculateSymbolContext(&sc);
    sc.block = nullptr;
    sc.line_entry.Clear();
    bool show_inlined_frames = false;
    const bool show_function_arguments = true;
    const bool show_function_name = true;

    dumped_declaration_info = sc.DumpStopContext(
        s, nullptr, Address(), show_fullpaths, show_module, show_inlined_frames,
        show_function_arguments, show_function_name);

    if (sc.function)
      s->PutChar(':');
  }
  if (m_declaration.DumpStopContext(s, false))
    dumped_declaration_info = true;
  return dumped_declaration_info;
}

size_t Variable::MemorySize() const { return sizeof(Variable); }

CompilerDeclContext Variable::GetDeclContext() {
  Type *type = GetType();
  if (type)
    return type->GetSymbolFile()->GetDeclContextContainingUID(GetID());
  return CompilerDeclContext();
}

CompilerDecl Variable::GetDecl() {
  Type *type = GetType();
  return type ? type->GetSymbolFile()->GetDeclForUID(GetID()) : CompilerDecl();
}

void Variable::CalculateSymbolContext(SymbolContext *sc) {
  if (m_owner_scope) {
    m_owner_scope->CalculateSymbolContext(sc);
    sc->variable = this;
  } else
    sc->Clear(false);
}

bool Variable::LocationIsValidForFrame(StackFrame *frame) {
  if (frame) {
    Function *function =
        frame->GetSymbolContext(eSymbolContextFunction).function;
    if (function) {
      TargetSP target_sp(frame->CalculateTarget());

      addr_t loclist_base_load_addr =
          function->GetAddressRange().GetBaseAddress().GetLoadAddress(
              target_sp.get());
      if (loclist_base_load_addr == LLDB_INVALID_ADDRESS)
        return false;
      // It is a location list. We just need to tell if the location list
      // contains the current address when converted to a load address
      return m_location_list.ContainsAddress(
          loclist_base_load_addr,
          frame->GetFrameCodeAddressForSymbolication().GetLoadAddress(
              target_sp.get()));
    }
  }
  return false;
}

bool Variable::LocationIsValidForAddress(const Address &address) {
  // Be sure to resolve the address to section offset prior to calling this
  // function.
  if (address.IsSectionOffset()) {
    // We need to check if the address is valid for both scope range and value
    // range.
    // Empty scope range means block range.
    bool valid_in_scope_range =
        GetScopeRange().IsEmpty() || GetScopeRange().FindEntryThatContains(
                                         address.GetFileAddress()) != nullptr;
    if (!valid_in_scope_range)
      return false;
    SymbolContext sc;
    CalculateSymbolContext(&sc);
    if (sc.module_sp == address.GetModule()) {
      // Is the variable is described by a single location?
      if (m_location_list.IsAlwaysValidSingleExpr()) {
        // Yes it is, the location is valid.
        return true;
      }

      if (sc.function) {
        addr_t loclist_base_file_addr =
            sc.function->GetAddressRange().GetBaseAddress().GetFileAddress();
        if (loclist_base_file_addr == LLDB_INVALID_ADDRESS)
          return false;
        // It is a location list. We just need to tell if the location list
        // contains the current address when converted to a load address
        return m_location_list.ContainsAddress(loclist_base_file_addr,
                                               address.GetFileAddress());
      }
    }
  }
  return false;
}

bool Variable::IsInScope(StackFrame *frame) {
  switch (m_scope) {
  case eValueTypeRegister:
  case eValueTypeRegisterSet:
    return frame != nullptr;

  case eValueTypeConstResult:
  case eValueTypeVariableGlobal:
  case eValueTypeVariableStatic:
  case eValueTypeVariableThreadLocal:
    return true;

  case eValueTypeVariableArgument:
  case eValueTypeVariableLocal:
    if (frame) {
      // We don't have a location list, we just need to see if the block that
      // this variable was defined in is currently
      Block *deepest_frame_block =
          frame->GetSymbolContext(eSymbolContextBlock).block;
      if (deepest_frame_block) {
        SymbolContext variable_sc;
        CalculateSymbolContext(&variable_sc);

        // Check for static or global variable defined at the compile unit
        // level that wasn't defined in a block
        if (variable_sc.block == nullptr)
          return true;

        // Check if the variable is valid in the current block
        if (variable_sc.block != deepest_frame_block &&
            !variable_sc.block->Contains(deepest_frame_block))
          return false;

        // If no scope range is specified then it means that the scope is the
        // same as the scope of the enclosing lexical block.
        if (m_scope_range.IsEmpty())
          return true;

        addr_t file_address = frame->GetFrameCodeAddress().GetFileAddress();
        return m_scope_range.FindEntryThatContains(file_address) != nullptr;
      }
    }
    break;

  default:
    break;
  }
  return false;
}

Status Variable::GetValuesForVariableExpressionPath(
    llvm::StringRef variable_expr_path, ExecutionContextScope *scope,
    GetVariableCallback callback, void *baton, VariableList &variable_list,
    ValueObjectList &valobj_list) {
  Status error;
  if (!callback || variable_expr_path.empty()) {
    error.SetErrorString("unknown error");
    return error;
  }

  switch (variable_expr_path.front()) {
  case '*':
    error = Variable::GetValuesForVariableExpressionPath(
        variable_expr_path.drop_front(), scope, callback, baton, variable_list,
        valobj_list);
    if (error.Fail()) {
      error.SetErrorString("unknown error");
      return error;
    }
    for (uint32_t i = 0; i < valobj_list.GetSize();) {
      Status tmp_error;
      ValueObjectSP valobj_sp(
          valobj_list.GetValueObjectAtIndex(i)->Dereference(tmp_error));
      if (tmp_error.Fail()) {
        variable_list.RemoveVariableAtIndex(i);
        valobj_list.RemoveValueObjectAtIndex(i);
      } else {
        valobj_list.SetValueObjectAtIndex(i, valobj_sp);
        ++i;
      }
    }
    return error;
  case '&': {
    error = Variable::GetValuesForVariableExpressionPath(
        variable_expr_path.drop_front(), scope, callback, baton, variable_list,
        valobj_list);
    if (error.Success()) {
      for (uint32_t i = 0; i < valobj_list.GetSize();) {
        Status tmp_error;
        ValueObjectSP valobj_sp(
            valobj_list.GetValueObjectAtIndex(i)->AddressOf(tmp_error));
        if (tmp_error.Fail()) {
          variable_list.RemoveVariableAtIndex(i);
          valobj_list.RemoveValueObjectAtIndex(i);
        } else {
          valobj_list.SetValueObjectAtIndex(i, valobj_sp);
          ++i;
        }
      }
    } else {
      error.SetErrorString("unknown error");
    }
    return error;
  } break;

  default: {
    static RegularExpression g_regex(
        llvm::StringRef("^([A-Za-z_:][A-Za-z_0-9:]*)(.*)"));
    llvm::SmallVector<llvm::StringRef, 2> matches;
    variable_list.Clear();
    if (!g_regex.Execute(variable_expr_path, &matches)) {
      error.SetErrorStringWithFormatv(
          "unable to extract a variable name from '{0}'", variable_expr_path);
      return error;
    }
    std::string variable_name = matches[1].str();
    if (!callback(baton, variable_name.c_str(), variable_list)) {
      error.SetErrorString("unknown error");
      return error;
    }
    uint32_t i = 0;
    while (i < variable_list.GetSize()) {
      VariableSP var_sp(variable_list.GetVariableAtIndex(i));
      ValueObjectSP valobj_sp;
      if (!var_sp) {
        variable_list.RemoveVariableAtIndex(i);
        continue;
      }
      ValueObjectSP variable_valobj_sp(
          ValueObjectVariable::Create(scope, var_sp));
      if (!variable_valobj_sp) {
        variable_list.RemoveVariableAtIndex(i);
        continue;
      }

      llvm::StringRef variable_sub_expr_path =
          variable_expr_path.drop_front(variable_name.size());
      if (!variable_sub_expr_path.empty()) {
        valobj_sp = variable_valobj_sp->GetValueForExpressionPath(
            variable_sub_expr_path);
        if (!valobj_sp) {
          error.SetErrorStringWithFormatv(
              "invalid expression path '{0}' for variable '{1}'",
              variable_sub_expr_path, var_sp->GetName().GetCString());
          variable_list.RemoveVariableAtIndex(i);
          continue;
        }
      } else {
        // Just the name of a variable with no extras
        valobj_sp = variable_valobj_sp;
      }

      valobj_list.Append(valobj_sp);
      ++i;
    }

    if (variable_list.GetSize() > 0) {
      error.Clear();
      return error;
    }
  } break;
  }
  error.SetErrorString("unknown error");
  return error;
}

bool Variable::DumpLocations(Stream *s, const Address &address) {
  SymbolContext sc;
  CalculateSymbolContext(&sc);
  ABISP abi;
  if (m_owner_scope) {
    ModuleSP module_sp(m_owner_scope->CalculateSymbolContextModule());
    if (module_sp)
      abi = ABI::FindPlugin(ProcessSP(), module_sp->GetArchitecture());
  }

  const addr_t file_addr = address.GetFileAddress();
  if (sc.function) {
    addr_t loclist_base_file_addr =
        sc.function->GetAddressRange().GetBaseAddress().GetFileAddress();
    if (loclist_base_file_addr == LLDB_INVALID_ADDRESS)
      return false;
    return m_location_list.DumpLocations(s, eDescriptionLevelBrief,
                                         loclist_base_file_addr, file_addr,
                                         abi.get());
  }
  return false;
}

static void PrivateAutoComplete(
    StackFrame *frame, llvm::StringRef partial_path,
    const llvm::Twine
        &prefix_path, // Anything that has been resolved already will be in here
    const CompilerType &compiler_type, CompletionRequest &request);

static void PrivateAutoCompleteMembers(
    StackFrame *frame, const std::string &partial_member_name,
    llvm::StringRef partial_path,
    const llvm::Twine
        &prefix_path, // Anything that has been resolved already will be in here
    const CompilerType &compiler_type, CompletionRequest &request) {

  // We are in a type parsing child members
  const uint32_t num_bases = compiler_type.GetNumDirectBaseClasses();

  if (num_bases > 0) {
    for (uint32_t i = 0; i < num_bases; ++i) {
      CompilerType base_class_type =
          compiler_type.GetDirectBaseClassAtIndex(i, nullptr);

      PrivateAutoCompleteMembers(frame, partial_member_name, partial_path,
                                 prefix_path,
                                 base_class_type.GetCanonicalType(), request);
    }
  }

  const uint32_t num_vbases = compiler_type.GetNumVirtualBaseClasses();

  if (num_vbases > 0) {
    for (uint32_t i = 0; i < num_vbases; ++i) {
      CompilerType vbase_class_type =
          compiler_type.GetVirtualBaseClassAtIndex(i, nullptr);

      PrivateAutoCompleteMembers(frame, partial_member_name, partial_path,
                                 prefix_path,
                                 vbase_class_type.GetCanonicalType(), request);
    }
  }

  // We are in a type parsing child members
  const uint32_t num_fields = compiler_type.GetNumFields();

  if (num_fields > 0) {
    for (uint32_t i = 0; i < num_fields; ++i) {
      std::string member_name;

      CompilerType member_compiler_type = compiler_type.GetFieldAtIndex(
          i, member_name, nullptr, nullptr, nullptr);

      if (partial_member_name.empty()) {
        request.AddCompletion((prefix_path + member_name).str());
      } else if (llvm::StringRef(member_name)
                     .starts_with(partial_member_name)) {
        if (member_name == partial_member_name) {
          PrivateAutoComplete(
              frame, partial_path,
              prefix_path + member_name, // Anything that has been resolved
                                         // already will be in here
              member_compiler_type.GetCanonicalType(), request);
        } else if (partial_path.empty()) {
          request.AddCompletion((prefix_path + member_name).str());
        }
      }
    }
  }
}

static void PrivateAutoComplete(
    StackFrame *frame, llvm::StringRef partial_path,
    const llvm::Twine
        &prefix_path, // Anything that has been resolved already will be in here
    const CompilerType &compiler_type, CompletionRequest &request) {
  //    printf ("\nPrivateAutoComplete()\n\tprefix_path = '%s'\n\tpartial_path =
  //    '%s'\n", prefix_path.c_str(), partial_path.c_str());
  std::string remaining_partial_path;

  const lldb::TypeClass type_class = compiler_type.GetTypeClass();
  if (partial_path.empty()) {
    if (compiler_type.IsValid()) {
      switch (type_class) {
      default:
      case eTypeClassArray:
      case eTypeClassBlockPointer:
      case eTypeClassBuiltin:
      case eTypeClassComplexFloat:
      case eTypeClassComplexInteger:
      case eTypeClassEnumeration:
      case eTypeClassFunction:
      case eTypeClassMemberPointer:
      case eTypeClassReference:
      case eTypeClassTypedef:
      case eTypeClassVector: {
        request.AddCompletion(prefix_path.str());
      } break;

      case eTypeClassClass:
      case eTypeClassStruct:
      case eTypeClassUnion:
        if (prefix_path.str().back() != '.')
          request.AddCompletion((prefix_path + ".").str());
        break;

      case eTypeClassObjCObject:
      case eTypeClassObjCInterface:
        break;
      case eTypeClassObjCObjectPointer:
      case eTypeClassPointer: {
        bool omit_empty_base_classes = true;
        if (llvm::expectedToStdOptional(
                compiler_type.GetNumChildren(omit_empty_base_classes, nullptr))
                .value_or(0))
          request.AddCompletion((prefix_path + "->").str());
        else {
          request.AddCompletion(prefix_path.str());
        }
      } break;
      }
    } else {
      if (frame) {
        const bool get_file_globals = true;

        VariableList *variable_list = frame->GetVariableList(get_file_globals,
                                                             nullptr);

        if (variable_list) {
          for (const VariableSP &var_sp : *variable_list)
            request.AddCompletion(var_sp->GetName().AsCString());
        }
      }
    }
  } else {
    const char ch = partial_path[0];
    switch (ch) {
    case '*':
      if (prefix_path.str().empty()) {
        PrivateAutoComplete(frame, partial_path.substr(1), "*", compiler_type,
                            request);
      }
      break;

    case '&':
      if (prefix_path.isTriviallyEmpty()) {
        PrivateAutoComplete(frame, partial_path.substr(1), std::string("&"),
                            compiler_type, request);
      }
      break;

    case '-':
      if (partial_path.size() > 1 && partial_path[1] == '>' &&
          !prefix_path.str().empty()) {
        switch (type_class) {
        case lldb::eTypeClassPointer: {
          CompilerType pointee_type(compiler_type.GetPointeeType());
          if (partial_path.size() > 2 && partial_path[2]) {
            // If there is more after the "->", then search deeper
            PrivateAutoComplete(frame, partial_path.substr(2),
                                prefix_path + "->",
                                pointee_type.GetCanonicalType(), request);
          } else {
            // Nothing after the "->", so list all members
            PrivateAutoCompleteMembers(
                frame, std::string(), std::string(), prefix_path + "->",
                pointee_type.GetCanonicalType(), request);
          }
        } break;
        default:
          break;
        }
      }
      break;

    case '.':
      if (compiler_type.IsValid()) {
        switch (type_class) {
        case lldb::eTypeClassUnion:
        case lldb::eTypeClassStruct:
        case lldb::eTypeClassClass:
          if (partial_path.size() > 1 && partial_path[1]) {
            // If there is more after the ".", then search deeper
            PrivateAutoComplete(frame, partial_path.substr(1),
                                prefix_path + ".", compiler_type, request);

          } else {
            // Nothing after the ".", so list all members
            PrivateAutoCompleteMembers(frame, std::string(), partial_path,
                                       prefix_path + ".", compiler_type,
                                       request);
          }
          break;
        default:
          break;
        }
      }
      break;
    default:
      if (isalpha(ch) || ch == '_' || ch == '$') {
        const size_t partial_path_len = partial_path.size();
        size_t pos = 1;
        while (pos < partial_path_len) {
          const char curr_ch = partial_path[pos];
          if (isalnum(curr_ch) || curr_ch == '_' || curr_ch == '$') {
            ++pos;
            continue;
          }
          break;
        }

        std::string token(std::string(partial_path), 0, pos);
        remaining_partial_path = std::string(partial_path.substr(pos));

        if (compiler_type.IsValid()) {
          PrivateAutoCompleteMembers(frame, token, remaining_partial_path,
                                     prefix_path, compiler_type, request);
        } else if (frame) {
          // We haven't found our variable yet
          const bool get_file_globals = true;

          VariableList *variable_list =
              frame->GetVariableList(get_file_globals, nullptr);

          if (!variable_list)
            break;

          for (VariableSP var_sp : *variable_list) {

            if (!var_sp)
              continue;

            llvm::StringRef variable_name = var_sp->GetName().GetStringRef();
            if (variable_name.starts_with(token)) {
              if (variable_name == token) {
                Type *variable_type = var_sp->GetType();
                if (variable_type) {
                  CompilerType variable_compiler_type(
                      variable_type->GetForwardCompilerType());
                  PrivateAutoComplete(
                      frame, remaining_partial_path,
                      prefix_path + token, // Anything that has been resolved
                                           // already will be in here
                      variable_compiler_type.GetCanonicalType(), request);
                } else {
                  request.AddCompletion((prefix_path + variable_name).str());
                }
              } else if (remaining_partial_path.empty()) {
                request.AddCompletion((prefix_path + variable_name).str());
              }
            }
          }
        }
      }
      break;
    }
  }
}

void Variable::AutoComplete(const ExecutionContext &exe_ctx,
                            CompletionRequest &request) {
  CompilerType compiler_type;

  PrivateAutoComplete(exe_ctx.GetFramePtr(), request.GetCursorArgumentPrefix(),
                      "", compiler_type, request);
}
