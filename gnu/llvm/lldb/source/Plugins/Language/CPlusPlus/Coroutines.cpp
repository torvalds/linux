//===-- Coroutines.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Coroutines.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

static lldb::addr_t GetCoroFramePtrFromHandle(ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return LLDB_INVALID_ADDRESS;

  // We expect a single pointer in the `coroutine_handle` class.
  // We don't care about its name.
  if (valobj_sp->GetNumChildrenIgnoringErrors() != 1)
    return LLDB_INVALID_ADDRESS;
  ValueObjectSP ptr_sp(valobj_sp->GetChildAtIndex(0));
  if (!ptr_sp)
    return LLDB_INVALID_ADDRESS;
  if (!ptr_sp->GetCompilerType().IsPointerType())
    return LLDB_INVALID_ADDRESS;

  AddressType addr_type;
  lldb::addr_t frame_ptr_addr = ptr_sp->GetPointerValue(&addr_type);
  if (!frame_ptr_addr || frame_ptr_addr == LLDB_INVALID_ADDRESS)
    return LLDB_INVALID_ADDRESS;
  lldbassert(addr_type == AddressType::eAddressTypeLoad);
  if (addr_type != AddressType::eAddressTypeLoad)
    return LLDB_INVALID_ADDRESS;

  return frame_ptr_addr;
}

static Function *ExtractDestroyFunction(lldb::TargetSP target_sp,
                                        lldb::addr_t frame_ptr_addr) {
  lldb::ProcessSP process_sp = target_sp->GetProcessSP();
  auto ptr_size = process_sp->GetAddressByteSize();

  Status error;
  auto destroy_func_ptr_addr = frame_ptr_addr + ptr_size;
  lldb::addr_t destroy_func_addr =
      process_sp->ReadPointerFromMemory(destroy_func_ptr_addr, error);
  if (error.Fail())
    return nullptr;

  Address destroy_func_address;
  if (!target_sp->ResolveLoadAddress(destroy_func_addr, destroy_func_address))
    return nullptr;

  return destroy_func_address.CalculateSymbolContextFunction();
}

static CompilerType InferPromiseType(Function &destroy_func) {
  Block &block = destroy_func.GetBlock(true);
  auto variable_list = block.GetBlockVariableList(true);

  // clang generates an artificial `__promise` variable inside the
  // `destroy` function. Look for it.
  auto promise_var = variable_list->FindVariable(ConstString("__promise"));
  if (!promise_var)
    return {};
  if (!promise_var->IsArtificial())
    return {};

  Type *promise_type = promise_var->GetType();
  if (!promise_type)
    return {};
  return promise_type->GetForwardCompilerType();
}

bool lldb_private::formatters::StdlibCoroutineHandleSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  lldb::addr_t frame_ptr_addr =
      GetCoroFramePtrFromHandle(valobj.GetNonSyntheticValue());
  if (frame_ptr_addr == LLDB_INVALID_ADDRESS)
    return false;

  if (frame_ptr_addr == 0) {
    stream << "nullptr";
  } else {
    stream.Printf("coro frame = 0x%" PRIx64, frame_ptr_addr);
  }

  return true;
}

lldb_private::formatters::StdlibCoroutineHandleSyntheticFrontEnd::
    StdlibCoroutineHandleSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

lldb_private::formatters::StdlibCoroutineHandleSyntheticFrontEnd::
    ~StdlibCoroutineHandleSyntheticFrontEnd() = default;

llvm::Expected<uint32_t> lldb_private::formatters::
    StdlibCoroutineHandleSyntheticFrontEnd::CalculateNumChildren() {
  if (!m_resume_ptr_sp || !m_destroy_ptr_sp)
    return 0;

  return m_promise_ptr_sp ? 3 : 2;
}

lldb::ValueObjectSP lldb_private::formatters::
    StdlibCoroutineHandleSyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  switch (idx) {
  case 0:
    return m_resume_ptr_sp;
  case 1:
    return m_destroy_ptr_sp;
  case 2:
    return m_promise_ptr_sp;
  }
  return lldb::ValueObjectSP();
}

lldb::ChildCacheState
lldb_private::formatters::StdlibCoroutineHandleSyntheticFrontEnd::Update() {
  m_resume_ptr_sp.reset();
  m_destroy_ptr_sp.reset();
  m_promise_ptr_sp.reset();

  ValueObjectSP valobj_sp = m_backend.GetNonSyntheticValue();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;

  lldb::addr_t frame_ptr_addr = GetCoroFramePtrFromHandle(valobj_sp);
  if (frame_ptr_addr == 0 || frame_ptr_addr == LLDB_INVALID_ADDRESS)
    return lldb::ChildCacheState::eRefetch;

  auto ts = valobj_sp->GetCompilerType().GetTypeSystem();
  auto ast_ctx = ts.dyn_cast_or_null<TypeSystemClang>();
  if (!ast_ctx)
    return lldb::ChildCacheState::eRefetch;

  // Create the `resume` and `destroy` children.
  lldb::TargetSP target_sp = m_backend.GetTargetSP();
  auto &exe_ctx = m_backend.GetExecutionContextRef();
  lldb::ProcessSP process_sp = target_sp->GetProcessSP();
  auto ptr_size = process_sp->GetAddressByteSize();
  CompilerType void_type = ast_ctx->GetBasicType(lldb::eBasicTypeVoid);
  CompilerType coro_func_type = ast_ctx->CreateFunctionType(
      /*result_type=*/void_type, /*args=*/&void_type, /*num_args=*/1,
      /*is_variadic=*/false, /*qualifiers=*/0);
  CompilerType coro_func_ptr_type = coro_func_type.GetPointerType();
  m_resume_ptr_sp = CreateValueObjectFromAddress(
      "resume", frame_ptr_addr + 0 * ptr_size, exe_ctx, coro_func_ptr_type);
  lldbassert(m_resume_ptr_sp);
  m_destroy_ptr_sp = CreateValueObjectFromAddress(
      "destroy", frame_ptr_addr + 1 * ptr_size, exe_ctx, coro_func_ptr_type);
  lldbassert(m_destroy_ptr_sp);

  // Get the `promise_type` from the template argument
  CompilerType promise_type(
      valobj_sp->GetCompilerType().GetTypeTemplateArgument(0));
  if (!promise_type)
    return lldb::ChildCacheState::eRefetch;

  // Try to infer the promise_type if it was type-erased
  if (promise_type.IsVoidType()) {
    if (Function *destroy_func =
            ExtractDestroyFunction(target_sp, frame_ptr_addr)) {
      if (CompilerType inferred_type = InferPromiseType(*destroy_func)) {
        promise_type = inferred_type;
      }
    }
  }

  // If we don't know the promise type, we don't display the `promise` member.
  // `CreateValueObjectFromAddress` below would fail for `void` types.
  if (promise_type.IsVoidType()) {
    return lldb::ChildCacheState::eRefetch;
  }

  // Add the `promise` member. We intentionally add `promise` as a pointer type
  // instead of a value type, and don't automatically dereference this pointer.
  // We do so to avoid potential very deep recursion in case there is a cycle
  // formed between `std::coroutine_handle`s and their promises.
  lldb::ValueObjectSP promise = CreateValueObjectFromAddress(
      "promise", frame_ptr_addr + 2 * ptr_size, exe_ctx, promise_type);
  Status error;
  lldb::ValueObjectSP promisePtr = promise->AddressOf(error);
  if (error.Success())
    m_promise_ptr_sp = promisePtr->Clone(ConstString("promise"));

  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::StdlibCoroutineHandleSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t StdlibCoroutineHandleSyntheticFrontEnd::GetIndexOfChildWithName(
    ConstString name) {
  if (!m_resume_ptr_sp || !m_destroy_ptr_sp)
    return UINT32_MAX;

  if (name == ConstString("resume"))
    return 0;
  if (name == ConstString("destroy"))
    return 1;
  if (name == ConstString("promise_ptr") && m_promise_ptr_sp)
    return 2;

  return UINT32_MAX;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::StdlibCoroutineHandleSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new StdlibCoroutineHandleSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}
