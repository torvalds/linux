//===-- LibStdcpp.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibStdcpp.h"
#include "LibCxx.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/DataFormatters/VectorIterator.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace {

class LibstdcppMapIteratorSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
  /*
   (std::_Rb_tree_iterator<std::pair<const int, std::basic_string<char,
   std::char_traits<char>, std::allocator<char> > > >) ibeg = {
   (_Base_ptr) _M_node = 0x0000000100103910 {
   (std::_Rb_tree_color) _M_color = _S_black
   (std::_Rb_tree_node_base::_Base_ptr) _M_parent = 0x00000001001038c0
   (std::_Rb_tree_node_base::_Base_ptr) _M_left = 0x0000000000000000
   (std::_Rb_tree_node_base::_Base_ptr) _M_right = 0x0000000000000000
   }
   }
   */

public:
  explicit LibstdcppMapIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ExecutionContextRef m_exe_ctx_ref;
  lldb::addr_t m_pair_address = 0;
  CompilerType m_pair_type;
  lldb::ValueObjectSP m_pair_sp;
};

class LibStdcppSharedPtrSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  explicit LibStdcppSharedPtrSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;
private:

  // The lifetime of a ValueObject and all its derivative ValueObjects
  // (children, clones, etc.) is managed by a ClusterManager. These
  // objects are only destroyed when every shared pointer to any of them
  // is destroyed, so we must not store a shared pointer to any ValueObject
  // derived from our backend ValueObject (since we're in the same cluster).
  ValueObject* m_ptr_obj = nullptr; // Underlying pointer (held, not owned)
  ValueObject* m_obj_obj = nullptr; // Underlying object (held, not owned)
};

} // end of anonymous namespace

LibstdcppMapIteratorSyntheticFrontEnd::LibstdcppMapIteratorSyntheticFrontEnd(
    lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_pair_type(),
      m_pair_sp() {
  if (valobj_sp)
    Update();
}

lldb::ChildCacheState LibstdcppMapIteratorSyntheticFrontEnd::Update() {
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  TargetSP target_sp(valobj_sp->GetTargetSP());

  if (!target_sp)
    return lldb::ChildCacheState::eRefetch;

  bool is_64bit = (target_sp->GetArchitecture().GetAddressByteSize() == 8);

  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();

  ValueObjectSP _M_node_sp(valobj_sp->GetChildMemberWithName("_M_node"));
  if (!_M_node_sp)
    return lldb::ChildCacheState::eRefetch;

  m_pair_address = _M_node_sp->GetValueAsUnsigned(0);
  if (m_pair_address == 0)
    return lldb::ChildCacheState::eRefetch;

  m_pair_address += (is_64bit ? 32 : 16);

  CompilerType my_type(valobj_sp->GetCompilerType());
  if (my_type.GetNumTemplateArguments() >= 1) {
    CompilerType pair_type = my_type.GetTypeTemplateArgument(0);
    if (!pair_type)
      return lldb::ChildCacheState::eRefetch;
    m_pair_type = pair_type;
  } else
    return lldb::ChildCacheState::eRefetch;

  return lldb::ChildCacheState::eReuse;
}

llvm::Expected<uint32_t>
LibstdcppMapIteratorSyntheticFrontEnd::CalculateNumChildren() {
  return 2;
}

lldb::ValueObjectSP
LibstdcppMapIteratorSyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (m_pair_address != 0 && m_pair_type) {
    if (!m_pair_sp)
      m_pair_sp = CreateValueObjectFromAddress("pair", m_pair_address,
                                               m_exe_ctx_ref, m_pair_type);
    if (m_pair_sp)
      return m_pair_sp->GetChildAtIndex(idx);
  }
  return lldb::ValueObjectSP();
}

bool LibstdcppMapIteratorSyntheticFrontEnd::MightHaveChildren() { return true; }

size_t LibstdcppMapIteratorSyntheticFrontEnd::GetIndexOfChildWithName(
    ConstString name) {
  if (name == "first")
    return 0;
  if (name == "second")
    return 1;
  return UINT32_MAX;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibstdcppMapIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibstdcppMapIteratorSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

/*
 (lldb) fr var ibeg --ptr-depth 1
 (__gnu_cxx::__normal_iterator<int *, std::vector<int, std::allocator<int> > >)
 ibeg = {
 _M_current = 0x00000001001037a0 {
 *_M_current = 1
 }
 }
 */

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibStdcppVectorIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new VectorIteratorSyntheticFrontEnd(
                          valobj_sp, {ConstString("_M_current")})
                    : nullptr);
}

lldb_private::formatters::VectorIteratorSyntheticFrontEnd::
    VectorIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp,
                                    llvm::ArrayRef<ConstString> item_names)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(),
      m_item_names(item_names), m_item_sp() {
  if (valobj_sp)
    Update();
}

lldb::ChildCacheState VectorIteratorSyntheticFrontEnd::Update() {
  m_item_sp.reset();

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  ValueObjectSP item_ptr =
      formatters::GetChildMemberWithName(*valobj_sp, m_item_names);
  if (!item_ptr)
    return lldb::ChildCacheState::eRefetch;
  if (item_ptr->GetValueAsUnsigned(0) == 0)
    return lldb::ChildCacheState::eRefetch;
  Status err;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  m_item_sp = CreateValueObjectFromAddress(
      "item", item_ptr->GetValueAsUnsigned(0), m_exe_ctx_ref,
      item_ptr->GetCompilerType().GetPointeeType());
  if (err.Fail())
    m_item_sp.reset();
  return lldb::ChildCacheState::eRefetch;
}

llvm::Expected<uint32_t>
VectorIteratorSyntheticFrontEnd::CalculateNumChildren() {
  return 1;
}

lldb::ValueObjectSP
VectorIteratorSyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (idx == 0)
    return m_item_sp;
  return lldb::ValueObjectSP();
}

bool VectorIteratorSyntheticFrontEnd::MightHaveChildren() { return true; }

size_t VectorIteratorSyntheticFrontEnd::GetIndexOfChildWithName(
    ConstString name) {
  if (name == "item")
    return 0;
  return UINT32_MAX;
}

bool lldb_private::formatters::LibStdcppStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  const bool scalar_is_load_addr = true;
  AddressType addr_type;
  lldb::addr_t addr_of_string = LLDB_INVALID_ADDRESS;
  if (valobj.IsPointerOrReferenceType()) {
    Status error;
    ValueObjectSP pointee_sp = valobj.Dereference(error);
    if (pointee_sp && error.Success())
      addr_of_string = pointee_sp->GetAddressOf(scalar_is_load_addr, &addr_type);
  } else
    addr_of_string =
        valobj.GetAddressOf(scalar_is_load_addr, &addr_type);
  if (addr_of_string != LLDB_INVALID_ADDRESS) {
    switch (addr_type) {
    case eAddressTypeLoad: {
      ProcessSP process_sp(valobj.GetProcessSP());
      if (!process_sp)
        return false;

      StringPrinter::ReadStringAndDumpToStreamOptions options(valobj);
      Status error;
      lldb::addr_t addr_of_data =
          process_sp->ReadPointerFromMemory(addr_of_string, error);
      if (error.Fail() || addr_of_data == 0 ||
          addr_of_data == LLDB_INVALID_ADDRESS)
        return false;
      options.SetLocation(addr_of_data);
      options.SetTargetSP(valobj.GetTargetSP());
      options.SetStream(&stream);
      options.SetNeedsZeroTermination(false);
      options.SetBinaryZeroIsTerminator(true);
      lldb::addr_t size_of_data = process_sp->ReadPointerFromMemory(
          addr_of_string + process_sp->GetAddressByteSize(), error);
      if (error.Fail())
        return false;
      options.SetSourceSize(size_of_data);
      options.SetHasSourceSize(true);

      if (!StringPrinter::ReadStringAndDumpToStream<
              StringPrinter::StringElementType::UTF8>(options)) {
        stream.Printf("Summary Unavailable");
        return true;
      } else
        return true;
    } break;
    case eAddressTypeHost:
      break;
    case eAddressTypeInvalid:
    case eAddressTypeFile:
      break;
    }
  }
  return false;
}

bool lldb_private::formatters::LibStdcppWStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  const bool scalar_is_load_addr = true;
  AddressType addr_type;
  lldb::addr_t addr_of_string =
      valobj.GetAddressOf(scalar_is_load_addr, &addr_type);
  if (addr_of_string != LLDB_INVALID_ADDRESS) {
    switch (addr_type) {
    case eAddressTypeLoad: {
      ProcessSP process_sp(valobj.GetProcessSP());
      if (!process_sp)
        return false;

      CompilerType wchar_compiler_type =
          valobj.GetCompilerType().GetBasicTypeFromAST(lldb::eBasicTypeWChar);

      if (!wchar_compiler_type)
        return false;

      // Safe to pass nullptr for exe_scope here.
      std::optional<uint64_t> size = wchar_compiler_type.GetBitSize(nullptr);
      if (!size)
        return false;
      const uint32_t wchar_size = *size;

      StringPrinter::ReadStringAndDumpToStreamOptions options(valobj);
      Status error;
      lldb::addr_t addr_of_data =
          process_sp->ReadPointerFromMemory(addr_of_string, error);
      if (error.Fail() || addr_of_data == 0 ||
          addr_of_data == LLDB_INVALID_ADDRESS)
        return false;
      options.SetLocation(addr_of_data);
      options.SetTargetSP(valobj.GetTargetSP());
      options.SetStream(&stream);
      options.SetNeedsZeroTermination(false);
      options.SetBinaryZeroIsTerminator(false);
      lldb::addr_t size_of_data = process_sp->ReadPointerFromMemory(
          addr_of_string + process_sp->GetAddressByteSize(), error);
      if (error.Fail())
        return false;
      options.SetSourceSize(size_of_data);
      options.SetHasSourceSize(true);
      options.SetPrefixToken("L");

      switch (wchar_size) {
      case 8:
        return StringPrinter::ReadStringAndDumpToStream<
            StringPrinter::StringElementType::UTF8>(options);
      case 16:
        return StringPrinter::ReadStringAndDumpToStream<
            StringPrinter::StringElementType::UTF16>(options);
      case 32:
        return StringPrinter::ReadStringAndDumpToStream<
            StringPrinter::StringElementType::UTF32>(options);
      default:
        stream.Printf("size for wchar_t is not valid");
        return true;
      }
      return true;
    } break;
    case eAddressTypeHost:
      break;
    case eAddressTypeInvalid:
    case eAddressTypeFile:
      break;
    }
  }
  return false;
}

LibStdcppSharedPtrSyntheticFrontEnd::LibStdcppSharedPtrSyntheticFrontEnd(
    lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

llvm::Expected<uint32_t>
LibStdcppSharedPtrSyntheticFrontEnd::CalculateNumChildren() {
  return 1;
}

lldb::ValueObjectSP
LibStdcppSharedPtrSyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (idx == 0)
    return m_ptr_obj->GetSP();
  if (idx == 1) {
    if (m_ptr_obj && !m_obj_obj) {
      Status error;
      ValueObjectSP obj_obj = m_ptr_obj->Dereference(error);
      if (error.Success())
        m_obj_obj = obj_obj->Clone(ConstString("object")).get();
    }
    if (m_obj_obj)
      return m_obj_obj->GetSP();
  }
  return lldb::ValueObjectSP();
}

lldb::ChildCacheState LibStdcppSharedPtrSyntheticFrontEnd::Update() {
  auto backend = m_backend.GetSP();
  if (!backend)
    return lldb::ChildCacheState::eRefetch;

  auto valobj_sp = backend->GetNonSyntheticValue();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  auto ptr_obj_sp = valobj_sp->GetChildMemberWithName("_M_ptr");
  if (!ptr_obj_sp)
    return lldb::ChildCacheState::eRefetch;

  m_ptr_obj = ptr_obj_sp->Clone(ConstString("pointer")).get();
  m_obj_obj = nullptr;

  return lldb::ChildCacheState::eRefetch;
}

bool LibStdcppSharedPtrSyntheticFrontEnd::MightHaveChildren() { return true; }

size_t LibStdcppSharedPtrSyntheticFrontEnd::GetIndexOfChildWithName(
    ConstString name) {
  if (name == "pointer")
    return 0;
  if (name == "object" || name == "$$dereference$$")
    return 1;
  return UINT32_MAX;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibStdcppSharedPtrSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibStdcppSharedPtrSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

bool lldb_private::formatters::LibStdcppSmartPointerSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());
  if (!valobj_sp)
    return false;

  ValueObjectSP ptr_sp(valobj_sp->GetChildMemberWithName("_M_ptr"));
  if (!ptr_sp)
    return false;

  ValueObjectSP usecount_sp(
      valobj_sp->GetChildAtNamePath({"_M_refcount", "_M_pi", "_M_use_count"}));
  if (!usecount_sp)
    return false;

  if (ptr_sp->GetValueAsUnsigned(0) == 0 ||
      usecount_sp->GetValueAsUnsigned(0) == 0) {
    stream.Printf("nullptr");
    return true;
  }

  Status error;
  ValueObjectSP pointee_sp = ptr_sp->Dereference(error);
  if (pointee_sp && error.Success()) {
    if (pointee_sp->DumpPrintableRepresentation(
            stream, ValueObject::eValueObjectRepresentationStyleSummary,
            lldb::eFormatInvalid,
            ValueObject::PrintableRepresentationSpecialCases::eDisable,
            false)) {
      return true;
    }
  }

  stream.Printf("ptr = 0x%" PRIx64, ptr_sp->GetValueAsUnsigned(0));
  return true;
}
