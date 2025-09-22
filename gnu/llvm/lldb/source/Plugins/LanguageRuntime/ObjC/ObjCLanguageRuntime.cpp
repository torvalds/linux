//===-- ObjCLanguageRuntime.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/AST/Type.h"

#include "ObjCLanguageRuntime.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Timer.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DJB.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

char ObjCLanguageRuntime::ID = 0;

// Destructor
ObjCLanguageRuntime::~ObjCLanguageRuntime() = default;

ObjCLanguageRuntime::ObjCLanguageRuntime(Process *process)
    : LanguageRuntime(process), m_impl_cache(), m_impl_str_cache(),
      m_has_new_literals_and_indexing(eLazyBoolCalculate),
      m_isa_to_descriptor(), m_hash_to_isa_map(), m_type_size_cache(),
      m_isa_to_descriptor_stop_id(UINT32_MAX), m_complete_class_cache(),
      m_negative_complete_class_cache() {}

bool ObjCLanguageRuntime::IsAllowedRuntimeValue(ConstString name) {
  static ConstString g_self = ConstString("self");
  static ConstString g_cmd = ConstString("_cmd");
  return name == g_self || name == g_cmd;
}

bool ObjCLanguageRuntime::AddClass(ObjCISA isa,
                                   const ClassDescriptorSP &descriptor_sp,
                                   const char *class_name) {
  if (isa != 0) {
    m_isa_to_descriptor[isa] = descriptor_sp;
    // class_name is assumed to be valid
    m_hash_to_isa_map.insert(std::make_pair(llvm::djbHash(class_name), isa));
    return true;
  }
  return false;
}

void ObjCLanguageRuntime::AddToMethodCache(lldb::addr_t class_addr,
                                           lldb::addr_t selector,
                                           lldb::addr_t impl_addr) {
  Log *log = GetLog(LLDBLog::Step);
  if (log) {
    LLDB_LOGF(log,
              "Caching: class 0x%" PRIx64 " selector 0x%" PRIx64
              " implementation 0x%" PRIx64 ".",
              class_addr, selector, impl_addr);
  }
  m_impl_cache.insert(std::pair<ClassAndSel, lldb::addr_t>(
      ClassAndSel(class_addr, selector), impl_addr));
}

void ObjCLanguageRuntime::AddToMethodCache(lldb::addr_t class_addr,
                                           llvm::StringRef sel_str,
                                           lldb::addr_t impl_addr) {
  Log *log = GetLog(LLDBLog::Step);

  LLDB_LOG(log, "Caching: class {0} selector {1} implementation {2}.",
           class_addr, sel_str, impl_addr);

  m_impl_str_cache.insert(std::pair<ClassAndSelStr, lldb::addr_t>(
      ClassAndSelStr(class_addr, sel_str), impl_addr));
}

lldb::addr_t ObjCLanguageRuntime::LookupInMethodCache(lldb::addr_t class_addr,
                                                      lldb::addr_t selector) {
  MsgImplMap::iterator pos, end = m_impl_cache.end();
  pos = m_impl_cache.find(ClassAndSel(class_addr, selector));
  if (pos != end)
    return (*pos).second;
  return LLDB_INVALID_ADDRESS;
}

lldb::addr_t ObjCLanguageRuntime::LookupInMethodCache(lldb::addr_t class_addr,
                                                      llvm::StringRef sel_str) {
  MsgImplStrMap::iterator pos, end = m_impl_str_cache.end();
  pos = m_impl_str_cache.find(ClassAndSelStr(class_addr, sel_str));
  if (pos != end)
    return (*pos).second;
  return LLDB_INVALID_ADDRESS;
}

lldb::TypeSP
ObjCLanguageRuntime::LookupInCompleteClassCache(ConstString &name) {
  CompleteClassMap::iterator complete_class_iter =
      m_complete_class_cache.find(name);

  if (complete_class_iter != m_complete_class_cache.end()) {
    // Check the weak pointer to make sure the type hasn't been unloaded
    TypeSP complete_type_sp(complete_class_iter->second.lock());

    if (complete_type_sp)
      return complete_type_sp;
    else
      m_complete_class_cache.erase(name);
  }

  if (m_negative_complete_class_cache.count(name) > 0)
    return TypeSP();

  const ModuleList &modules = m_process->GetTarget().GetImages();

  SymbolContextList sc_list;
  modules.FindSymbolsWithNameAndType(name, eSymbolTypeObjCClass, sc_list);
  const size_t matching_symbols = sc_list.GetSize();

  if (matching_symbols) {
    SymbolContext sc;

    sc_list.GetContextAtIndex(0, sc);

    ModuleSP module_sp(sc.module_sp);

    if (!module_sp)
      return TypeSP();

    TypeQuery query(name.GetStringRef(), TypeQueryOptions::e_exact_match);
    TypeResults results;
    module_sp->FindTypes(query, results);
    for (const TypeSP &type_sp : results.GetTypeMap().Types()) {
      if (TypeSystemClang::IsObjCObjectOrInterfaceType(
              type_sp->GetForwardCompilerType())) {
        if (TypePayloadClang(type_sp->GetPayload()).IsCompleteObjCClass()) {
          m_complete_class_cache[name] = type_sp;
          return type_sp;
        }
      }
    }
  }
  m_negative_complete_class_cache.insert(name);
  return TypeSP();
}

size_t ObjCLanguageRuntime::GetByteOffsetForIvar(CompilerType &parent_qual_type,
                                                 const char *ivar_name) {
  return LLDB_INVALID_IVAR_OFFSET;
}

bool ObjCLanguageRuntime::ClassDescriptor::IsPointerValid(
    lldb::addr_t value, uint32_t ptr_size, bool allow_NULLs, bool allow_tagged,
    bool check_version_specific) const {
  if (!value)
    return allow_NULLs;
  if ((value % 2) == 1 && allow_tagged)
    return true;
  if ((value % ptr_size) == 0)
    return (check_version_specific ? CheckPointer(value, ptr_size) : true);
  else
    return false;
}

ObjCLanguageRuntime::ObjCISA
ObjCLanguageRuntime::GetISA(ConstString name) {
  ISAToDescriptorIterator pos = GetDescriptorIterator(name);
  if (pos != m_isa_to_descriptor.end())
    return pos->first;
  return 0;
}

ObjCLanguageRuntime::ISAToDescriptorIterator
ObjCLanguageRuntime::GetDescriptorIterator(ConstString name) {
  ISAToDescriptorIterator end = m_isa_to_descriptor.end();

  if (name) {
    UpdateISAToDescriptorMap();
    if (m_hash_to_isa_map.empty()) {
      // No name hashes were provided, we need to just linearly power through
      // the names and find a match
      for (ISAToDescriptorIterator pos = m_isa_to_descriptor.begin();
           pos != end; ++pos) {
        if (pos->second->GetClassName() == name)
          return pos;
      }
    } else {
      // Name hashes were provided, so use them to efficiently lookup name to
      // isa/descriptor
      const uint32_t name_hash = llvm::djbHash(name.GetStringRef());
      std::pair<HashToISAIterator, HashToISAIterator> range =
          m_hash_to_isa_map.equal_range(name_hash);
      for (HashToISAIterator range_pos = range.first; range_pos != range.second;
           ++range_pos) {
        ISAToDescriptorIterator pos =
            m_isa_to_descriptor.find(range_pos->second);
        if (pos != m_isa_to_descriptor.end()) {
          if (pos->second->GetClassName() == name)
            return pos;
        }
      }
    }
  }
  return end;
}

std::pair<ObjCLanguageRuntime::ISAToDescriptorIterator,
          ObjCLanguageRuntime::ISAToDescriptorIterator>
ObjCLanguageRuntime::GetDescriptorIteratorPair(bool update_if_needed) {
  if (update_if_needed)
    UpdateISAToDescriptorMapIfNeeded();

  return std::pair<ObjCLanguageRuntime::ISAToDescriptorIterator,
                   ObjCLanguageRuntime::ISAToDescriptorIterator>(
      m_isa_to_descriptor.begin(), m_isa_to_descriptor.end());
}

void ObjCLanguageRuntime::ReadObjCLibraryIfNeeded(
    const ModuleList &module_list) {
  if (!HasReadObjCLibrary()) {
    std::lock_guard<std::recursive_mutex> guard(module_list.GetMutex());

    size_t num_modules = module_list.GetSize();
    for (size_t i = 0; i < num_modules; i++) {
      auto mod = module_list.GetModuleAtIndex(i);
      if (IsModuleObjCLibrary(mod)) {
        ReadObjCLibrary(mod);
        break;
      }
    }
  }
}

ObjCLanguageRuntime::ObjCISA
ObjCLanguageRuntime::GetParentClass(ObjCLanguageRuntime::ObjCISA isa) {
  ClassDescriptorSP objc_class_sp(GetClassDescriptorFromISA(isa));
  if (objc_class_sp) {
    ClassDescriptorSP objc_super_class_sp(objc_class_sp->GetSuperclass());
    if (objc_super_class_sp)
      return objc_super_class_sp->GetISA();
  }
  return 0;
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetClassDescriptorFromClassName(
    ConstString class_name) {
  ISAToDescriptorIterator pos = GetDescriptorIterator(class_name);
  if (pos != m_isa_to_descriptor.end())
    return pos->second;
  return ClassDescriptorSP();
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetClassDescriptor(ValueObject &valobj) {
  ClassDescriptorSP objc_class_sp;
  // if we get an invalid VO (which might still happen when playing around with
  // pointers returned by the expression parser, don't consider this a valid
  // ObjC object)
  if (valobj.GetCompilerType().IsValid()) {
    addr_t isa_pointer = valobj.GetPointerValue();
    if (isa_pointer != LLDB_INVALID_ADDRESS) {
      ExecutionContext exe_ctx(valobj.GetExecutionContextRef());

      Process *process = exe_ctx.GetProcessPtr();
      if (process) {
        Status error;
        ObjCISA isa = process->ReadPointerFromMemory(isa_pointer, error);
        if (isa != LLDB_INVALID_ADDRESS)
          objc_class_sp = GetClassDescriptorFromISA(isa);
      }
    }
  }
  return objc_class_sp;
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetNonKVOClassDescriptor(ValueObject &valobj) {
  ObjCLanguageRuntime::ClassDescriptorSP objc_class_sp(
      GetClassDescriptor(valobj));
  if (objc_class_sp) {
    if (!objc_class_sp->IsKVO())
      return objc_class_sp;

    ClassDescriptorSP non_kvo_objc_class_sp(objc_class_sp->GetSuperclass());
    if (non_kvo_objc_class_sp && non_kvo_objc_class_sp->IsValid())
      return non_kvo_objc_class_sp;
  }
  return ClassDescriptorSP();
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetClassDescriptorFromISA(ObjCISA isa) {
  if (isa) {
    UpdateISAToDescriptorMap();

    ObjCLanguageRuntime::ISAToDescriptorIterator pos =
        m_isa_to_descriptor.find(isa);
    if (pos != m_isa_to_descriptor.end())
      return pos->second;

    if (ABISP abi_sp = m_process->GetABI()) {
      pos = m_isa_to_descriptor.find(abi_sp->FixCodeAddress(isa));
      if (pos != m_isa_to_descriptor.end())
        return pos->second;
    }
  }
  return ClassDescriptorSP();
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetNonKVOClassDescriptor(ObjCISA isa) {
  if (isa) {
    ClassDescriptorSP objc_class_sp = GetClassDescriptorFromISA(isa);
    if (objc_class_sp && objc_class_sp->IsValid()) {
      if (!objc_class_sp->IsKVO())
        return objc_class_sp;

      ClassDescriptorSP non_kvo_objc_class_sp(objc_class_sp->GetSuperclass());
      if (non_kvo_objc_class_sp && non_kvo_objc_class_sp->IsValid())
        return non_kvo_objc_class_sp;
    }
  }
  return ClassDescriptorSP();
}

CompilerType
ObjCLanguageRuntime::EncodingToType::RealizeType(const char *name,
                                                 bool for_expression) {
  if (m_scratch_ast_ctx_sp)
    return RealizeType(*m_scratch_ast_ctx_sp, name, for_expression);
  return CompilerType();
}

ObjCLanguageRuntime::EncodingToType::~EncodingToType() = default;

ObjCLanguageRuntime::EncodingToTypeSP ObjCLanguageRuntime::GetEncodingToType() {
  return nullptr;
}

std::optional<uint64_t>
ObjCLanguageRuntime::GetTypeBitSize(const CompilerType &compiler_type) {
  void *opaque_ptr = compiler_type.GetOpaqueQualType();
  uint64_t cached_size = m_type_size_cache.Lookup(opaque_ptr);
  if (cached_size > 0)
    return cached_size;

  ClassDescriptorSP class_descriptor_sp =
      GetClassDescriptorFromClassName(compiler_type.GetTypeName());
  if (!class_descriptor_sp)
    return {};

  int32_t max_offset = INT32_MIN;
  uint64_t sizeof_max = 0;
  bool found = false;

  for (size_t idx = 0; idx < class_descriptor_sp->GetNumIVars(); idx++) {
    const auto &ivar = class_descriptor_sp->GetIVarAtIndex(idx);
    int32_t cur_offset = ivar.m_offset;
    if (cur_offset > max_offset) {
      max_offset = cur_offset;
      sizeof_max = ivar.m_size;
      found = true;
    }
  }

  uint64_t size = 8 * (max_offset + sizeof_max);
  if (found && size > 0) {
    m_type_size_cache.Insert(opaque_ptr, size);
    return size;
  }

  return {};
}

lldb::BreakpointPreconditionSP
ObjCLanguageRuntime::GetBreakpointExceptionPrecondition(LanguageType language,
                                                        bool throw_bp) {
  if (language != eLanguageTypeObjC)
    return lldb::BreakpointPreconditionSP();
  if (!throw_bp)
    return lldb::BreakpointPreconditionSP();
  BreakpointPreconditionSP precondition_sp(
      new ObjCLanguageRuntime::ObjCExceptionPrecondition());
  return precondition_sp;
}

// Exception breakpoint Precondition class for ObjC:
void ObjCLanguageRuntime::ObjCExceptionPrecondition::AddClassName(
    const char *class_name) {
  m_class_names.insert(class_name);
}

ObjCLanguageRuntime::ObjCExceptionPrecondition::ObjCExceptionPrecondition() =
    default;

bool ObjCLanguageRuntime::ObjCExceptionPrecondition::EvaluatePrecondition(
    StoppointCallbackContext &context) {
  return true;
}

void ObjCLanguageRuntime::ObjCExceptionPrecondition::GetDescription(
    Stream &stream, lldb::DescriptionLevel level) {}

Status ObjCLanguageRuntime::ObjCExceptionPrecondition::ConfigurePrecondition(
    Args &args) {
  Status error;
  if (args.GetArgumentCount() > 0)
    error.SetErrorString(
        "The ObjC Exception breakpoint doesn't support extra options.");
  return error;
}

std::optional<CompilerType>
ObjCLanguageRuntime::GetRuntimeType(CompilerType base_type) {
  CompilerType class_type;
  bool is_pointer_type = false;

  if (TypeSystemClang::IsObjCObjectPointerType(base_type, &class_type))
    is_pointer_type = true;
  else if (TypeSystemClang::IsObjCObjectOrInterfaceType(base_type))
    class_type = base_type;
  else
    return std::nullopt;

  if (!class_type)
    return std::nullopt;

  ConstString class_name(class_type.GetTypeName());
  if (!class_name)
    return std::nullopt;

  TypeSP complete_objc_class_type_sp = LookupInCompleteClassCache(class_name);
  if (!complete_objc_class_type_sp)
    return std::nullopt;

  CompilerType complete_class(
      complete_objc_class_type_sp->GetFullCompilerType());
  if (complete_class.GetCompleteType()) {
    if (is_pointer_type)
      return complete_class.GetPointerType();
    else
      return complete_class;
  }

  return std::nullopt;
}
