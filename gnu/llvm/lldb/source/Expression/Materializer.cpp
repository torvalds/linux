//===-- Materializer.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/Materializer.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-forward.h"

#include <memory>
#include <optional>

using namespace lldb_private;

// FIXME: these should be retrieved from the target
//        instead of being hard-coded. Currently we
//        assume that persistent vars are materialized
//        as references, and thus pick the size of a
//        64-bit pointer.
static constexpr uint32_t g_default_var_alignment = 8;
static constexpr uint32_t g_default_var_byte_size = 8;

uint32_t Materializer::AddStructMember(Entity &entity) {
  uint32_t size = entity.GetSize();
  uint32_t alignment = entity.GetAlignment();

  uint32_t ret;

  if (m_current_offset == 0)
    m_struct_alignment = alignment;

  if (m_current_offset % alignment)
    m_current_offset += (alignment - (m_current_offset % alignment));

  ret = m_current_offset;

  m_current_offset += size;

  return ret;
}

class EntityPersistentVariable : public Materializer::Entity {
public:
  EntityPersistentVariable(lldb::ExpressionVariableSP &persistent_variable_sp,
                           Materializer::PersistentVariableDelegate *delegate)
      : Entity(), m_persistent_variable_sp(persistent_variable_sp),
        m_delegate(delegate) {
    // Hard-coding to maximum size of a pointer since persistent variables are
    // materialized by reference
    m_size = g_default_var_byte_size;
    m_alignment = g_default_var_alignment;
  }

  void MakeAllocation(IRMemoryMap &map, Status &err) {
    Log *log = GetLog(LLDBLog::Expressions);

    // Allocate a spare memory area to store the persistent variable's
    // contents.

    Status allocate_error;
    const bool zero_memory = false;

    lldb::addr_t mem = map.Malloc(
        m_persistent_variable_sp->GetByteSize().value_or(0), 8,
        lldb::ePermissionsReadable | lldb::ePermissionsWritable,
        IRMemoryMap::eAllocationPolicyMirror, zero_memory, allocate_error);

    if (!allocate_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't allocate a memory area to store %s: %s",
          m_persistent_variable_sp->GetName().GetCString(),
          allocate_error.AsCString());
      return;
    }

    LLDB_LOGF(log, "Allocated %s (0x%" PRIx64 ") successfully",
              m_persistent_variable_sp->GetName().GetCString(), mem);

    // Put the location of the spare memory into the live data of the
    // ValueObject.

    m_persistent_variable_sp->m_live_sp = ValueObjectConstResult::Create(
        map.GetBestExecutionContextScope(),
        m_persistent_variable_sp->GetCompilerType(),
        m_persistent_variable_sp->GetName(), mem, eAddressTypeLoad,
        map.GetAddressByteSize());

    // Clear the flag if the variable will never be deallocated.

    if (m_persistent_variable_sp->m_flags &
        ExpressionVariable::EVKeepInTarget) {
      Status leak_error;
      map.Leak(mem, leak_error);
      m_persistent_variable_sp->m_flags &=
          ~ExpressionVariable::EVNeedsAllocation;
    }

    // Write the contents of the variable to the area.

    Status write_error;

    map.WriteMemory(mem, m_persistent_variable_sp->GetValueBytes(),
                    m_persistent_variable_sp->GetByteSize().value_or(0),
                    write_error);

    if (!write_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't write %s to the target: %s",
          m_persistent_variable_sp->GetName().AsCString(),
          write_error.AsCString());
      return;
    }
  }

  void DestroyAllocation(IRMemoryMap &map, Status &err) {
    Status deallocate_error;

    map.Free((lldb::addr_t)m_persistent_variable_sp->m_live_sp->GetValue()
                 .GetScalar()
                 .ULongLong(),
             deallocate_error);

    m_persistent_variable_sp->m_live_sp.reset();

    if (!deallocate_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't deallocate memory for %s: %s",
          m_persistent_variable_sp->GetName().GetCString(),
          deallocate_error.AsCString());
    }
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    Log *log = GetLog(LLDBLog::Expressions);

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      LLDB_LOGF(log,
                "EntityPersistentVariable::Materialize [address = 0x%" PRIx64
                ", m_name = %s, m_flags = 0x%hx]",
                (uint64_t)load_addr,
                m_persistent_variable_sp->GetName().AsCString(),
                m_persistent_variable_sp->m_flags);
    }

    if (m_persistent_variable_sp->m_flags &
        ExpressionVariable::EVNeedsAllocation) {
      MakeAllocation(map, err);
      m_persistent_variable_sp->m_flags |=
          ExpressionVariable::EVIsLLDBAllocated;

      if (!err.Success())
        return;
    }

    if ((m_persistent_variable_sp->m_flags &
             ExpressionVariable::EVIsProgramReference &&
         m_persistent_variable_sp->m_live_sp) ||
        m_persistent_variable_sp->m_flags &
            ExpressionVariable::EVIsLLDBAllocated) {
      Status write_error;

      map.WriteScalarToMemory(
          load_addr,
          m_persistent_variable_sp->m_live_sp->GetValue().GetScalar(),
          map.GetAddressByteSize(), write_error);

      if (!write_error.Success()) {
        err.SetErrorStringWithFormat(
            "couldn't write the location of %s to memory: %s",
            m_persistent_variable_sp->GetName().AsCString(),
            write_error.AsCString());
      }
    } else {
      err.SetErrorStringWithFormat(
          "no materialization happened for persistent variable %s",
          m_persistent_variable_sp->GetName().AsCString());
      return;
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    Log *log = GetLog(LLDBLog::Expressions);

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      LLDB_LOGF(log,
                "EntityPersistentVariable::Dematerialize [address = 0x%" PRIx64
                ", m_name = %s, m_flags = 0x%hx]",
                (uint64_t)process_address + m_offset,
                m_persistent_variable_sp->GetName().AsCString(),
                m_persistent_variable_sp->m_flags);
    }

    if (m_delegate) {
      m_delegate->DidDematerialize(m_persistent_variable_sp);
    }

    if ((m_persistent_variable_sp->m_flags &
         ExpressionVariable::EVIsLLDBAllocated) ||
        (m_persistent_variable_sp->m_flags &
         ExpressionVariable::EVIsProgramReference)) {
      if (m_persistent_variable_sp->m_flags &
              ExpressionVariable::EVIsProgramReference &&
          !m_persistent_variable_sp->m_live_sp) {
        // If the reference comes from the program, then the
        // ClangExpressionVariable's live variable data hasn't been set up yet.
        // Do this now.

        lldb::addr_t location;
        Status read_error;

        map.ReadPointerFromMemory(&location, load_addr, read_error);

        if (!read_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't read the address of program-allocated variable %s: %s",
              m_persistent_variable_sp->GetName().GetCString(),
              read_error.AsCString());
          return;
        }

        m_persistent_variable_sp->m_live_sp = ValueObjectConstResult::Create(
            map.GetBestExecutionContextScope(),
            m_persistent_variable_sp.get()->GetCompilerType(),
            m_persistent_variable_sp->GetName(), location, eAddressTypeLoad,
            m_persistent_variable_sp->GetByteSize().value_or(0));

        if (frame_top != LLDB_INVALID_ADDRESS &&
            frame_bottom != LLDB_INVALID_ADDRESS && location >= frame_bottom &&
            location <= frame_top) {
          // If the variable is resident in the stack frame created by the
          // expression, then it cannot be relied upon to stay around.  We
          // treat it as needing reallocation.
          m_persistent_variable_sp->m_flags |=
              ExpressionVariable::EVIsLLDBAllocated;
          m_persistent_variable_sp->m_flags |=
              ExpressionVariable::EVNeedsAllocation;
          m_persistent_variable_sp->m_flags |=
              ExpressionVariable::EVNeedsFreezeDry;
          m_persistent_variable_sp->m_flags &=
              ~ExpressionVariable::EVIsProgramReference;
        }
      }

      lldb::addr_t mem = m_persistent_variable_sp->m_live_sp->GetValue()
                             .GetScalar()
                             .ULongLong();

      if (!m_persistent_variable_sp->m_live_sp) {
        err.SetErrorStringWithFormat(
            "couldn't find the memory area used to store %s",
            m_persistent_variable_sp->GetName().GetCString());
        return;
      }

      if (m_persistent_variable_sp->m_live_sp->GetValue()
              .GetValueAddressType() != eAddressTypeLoad) {
        err.SetErrorStringWithFormat(
            "the address of the memory area for %s is in an incorrect format",
            m_persistent_variable_sp->GetName().GetCString());
        return;
      }

      if (m_persistent_variable_sp->m_flags &
              ExpressionVariable::EVNeedsFreezeDry ||
          m_persistent_variable_sp->m_flags &
              ExpressionVariable::EVKeepInTarget) {
        LLDB_LOGF(log, "Dematerializing %s from 0x%" PRIx64 " (size = %llu)",
                  m_persistent_variable_sp->GetName().GetCString(),
                  (uint64_t)mem,
                  (unsigned long long)m_persistent_variable_sp->GetByteSize()
                      .value_or(0));

        // Read the contents of the spare memory area

        m_persistent_variable_sp->ValueUpdated();

        Status read_error;

        map.ReadMemory(m_persistent_variable_sp->GetValueBytes(), mem,
                       m_persistent_variable_sp->GetByteSize().value_or(0),
                       read_error);

        if (!read_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't read the contents of %s from memory: %s",
              m_persistent_variable_sp->GetName().GetCString(),
              read_error.AsCString());
          return;
        }

        m_persistent_variable_sp->m_flags &=
            ~ExpressionVariable::EVNeedsFreezeDry;
      }
    } else {
      err.SetErrorStringWithFormat(
          "no dematerialization happened for persistent variable %s",
          m_persistent_variable_sp->GetName().AsCString());
      return;
    }

    lldb::ProcessSP process_sp =
        map.GetBestExecutionContextScope()->CalculateProcess();
    if (!process_sp || !process_sp->CanJIT()) {
      // Allocations are not persistent so persistent variables cannot stay
      // materialized.

      m_persistent_variable_sp->m_flags |=
          ExpressionVariable::EVNeedsAllocation;

      DestroyAllocation(map, err);
      if (!err.Success())
        return;
    } else if (m_persistent_variable_sp->m_flags &
                   ExpressionVariable::EVNeedsAllocation &&
               !(m_persistent_variable_sp->m_flags &
                 ExpressionVariable::EVKeepInTarget)) {
      DestroyAllocation(map, err);
      if (!err.Success())
        return;
    }
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    Status err;

    const lldb::addr_t load_addr = process_address + m_offset;

    dump_stream.Printf("0x%" PRIx64 ": EntityPersistentVariable (%s)\n",
                       load_addr,
                       m_persistent_variable_sp->GetName().AsCString());

    {
      dump_stream.Printf("Pointer:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    {
      dump_stream.Printf("Target:\n");

      lldb::addr_t target_address;

      map.ReadPointerFromMemory(&target_address, load_addr, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DataBufferHeap data(m_persistent_variable_sp->GetByteSize().value_or(0),
                            0);

        map.ReadMemory(data.GetBytes(), target_address,
                       m_persistent_variable_sp->GetByteSize().value_or(0),
                       err);

        if (!err.Success()) {
          dump_stream.Printf("  <could not be read>\n");
        } else {
          DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                       target_address);

          dump_stream.PutChar('\n');
        }
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {}

private:
  lldb::ExpressionVariableSP m_persistent_variable_sp;
  Materializer::PersistentVariableDelegate *m_delegate;
};

uint32_t Materializer::AddPersistentVariable(
    lldb::ExpressionVariableSP &persistent_variable_sp,
    PersistentVariableDelegate *delegate, Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  *iter = std::make_unique<EntityPersistentVariable>(persistent_variable_sp,
                                                     delegate);
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

/// Base class for materialization of Variables and ValueObjects.
///
/// Subclasses specify how to obtain the Value which is to be
/// materialized.
class EntityVariableBase : public Materializer::Entity {
public:
  virtual ~EntityVariableBase() = default;

  EntityVariableBase() {
    // Hard-coding to maximum size of a pointer since all variables are
    // materialized by reference
    m_size = g_default_var_byte_size;
    m_alignment = g_default_var_alignment;
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    Log *log = GetLog(LLDBLog::Expressions);

    const lldb::addr_t load_addr = process_address + m_offset;
    if (log) {
      LLDB_LOGF(log,
                "EntityVariable::Materialize [address = 0x%" PRIx64
                ", m_variable_sp = %s]",
                (uint64_t)load_addr, GetName().GetCString());
    }

    ExecutionContextScope *scope = frame_sp.get();

    if (!scope)
      scope = map.GetBestExecutionContextScope();

    lldb::ValueObjectSP valobj_sp = SetupValueObject(scope);

    if (!valobj_sp) {
      err.SetErrorStringWithFormat(
          "couldn't get a value object for variable %s", GetName().AsCString());
      return;
    }

    Status valobj_error = valobj_sp->GetError();

    if (valobj_error.Fail()) {
      err.SetErrorStringWithFormat("couldn't get the value of variable %s: %s",
                                   GetName().AsCString(),
                                   valobj_error.AsCString());
      return;
    }

    if (m_is_reference) {
      DataExtractor valobj_extractor;
      Status extract_error;
      valobj_sp->GetData(valobj_extractor, extract_error);

      if (!extract_error.Success()) {
        err.SetErrorStringWithFormat(
            "couldn't read contents of reference variable %s: %s",
            GetName().AsCString(), extract_error.AsCString());
        return;
      }

      lldb::offset_t offset = 0;
      lldb::addr_t reference_addr = valobj_extractor.GetAddress(&offset);

      Status write_error;
      map.WritePointerToMemory(load_addr, reference_addr, write_error);

      if (!write_error.Success()) {
        err.SetErrorStringWithFormat("couldn't write the contents of reference "
                                     "variable %s to memory: %s",
                                     GetName().AsCString(),
                                     write_error.AsCString());
        return;
      }
    } else {
      AddressType address_type = eAddressTypeInvalid;
      const bool scalar_is_load_address = false;
      lldb::addr_t addr_of_valobj =
          valobj_sp->GetAddressOf(scalar_is_load_address, &address_type);
      if (addr_of_valobj != LLDB_INVALID_ADDRESS) {
        Status write_error;
        map.WritePointerToMemory(load_addr, addr_of_valobj, write_error);

        if (!write_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't write the address of variable %s to memory: %s",
              GetName().AsCString(), write_error.AsCString());
          return;
        }
      } else {
        DataExtractor data;
        Status extract_error;
        valobj_sp->GetData(data, extract_error);
        if (!extract_error.Success()) {
          err.SetErrorStringWithFormat("couldn't get the value of %s: %s",
                                       GetName().AsCString(),
                                       extract_error.AsCString());
          return;
        }

        if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
          err.SetErrorStringWithFormat(
              "trying to create a temporary region for %s but one exists",
              GetName().AsCString());
          return;
        }

        if (data.GetByteSize() < GetByteSize(scope)) {
          if (data.GetByteSize() == 0 && !LocationExpressionIsValid()) {
            err.SetErrorStringWithFormat("the variable '%s' has no location, "
                                         "it may have been optimized out",
                                         GetName().AsCString());
          } else {
            err.SetErrorStringWithFormat(
                "size of variable %s (%" PRIu64
                ") is larger than the ValueObject's size (%" PRIu64 ")",
                GetName().AsCString(), GetByteSize(scope).value_or(0),
                data.GetByteSize());
          }
          return;
        }

        std::optional<size_t> opt_bit_align = GetTypeBitAlign(scope);
        if (!opt_bit_align) {
          err.SetErrorStringWithFormat("can't get the type alignment for %s",
                                       GetName().AsCString());
          return;
        }

        size_t byte_align = (*opt_bit_align + 7) / 8;

        Status alloc_error;
        const bool zero_memory = false;

        m_temporary_allocation = map.Malloc(
            data.GetByteSize(), byte_align,
            lldb::ePermissionsReadable | lldb::ePermissionsWritable,
            IRMemoryMap::eAllocationPolicyMirror, zero_memory, alloc_error);

        m_temporary_allocation_size = data.GetByteSize();

        m_original_data = std::make_shared<DataBufferHeap>(data.GetDataStart(),
                                                           data.GetByteSize());

        if (!alloc_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't allocate a temporary region for %s: %s",
              GetName().AsCString(), alloc_error.AsCString());
          return;
        }

        Status write_error;

        map.WriteMemory(m_temporary_allocation, data.GetDataStart(),
                        data.GetByteSize(), write_error);

        if (!write_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't write to the temporary region for %s: %s",
              GetName().AsCString(), write_error.AsCString());
          return;
        }

        Status pointer_write_error;

        map.WritePointerToMemory(load_addr, m_temporary_allocation,
                                 pointer_write_error);

        if (!pointer_write_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't write the address of the temporary region for %s: %s",
              GetName().AsCString(), pointer_write_error.AsCString());
        }
      }
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    Log *log = GetLog(LLDBLog::Expressions);

    const lldb::addr_t load_addr = process_address + m_offset;
    if (log) {
      LLDB_LOGF(log,
                "EntityVariable::Dematerialize [address = 0x%" PRIx64
                ", m_variable_sp = %s]",
                (uint64_t)load_addr, GetName().AsCString());
    }

    if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
      ExecutionContextScope *scope = frame_sp.get();

      if (!scope)
        scope = map.GetBestExecutionContextScope();

      lldb::ValueObjectSP valobj_sp = SetupValueObject(scope);

      if (!valobj_sp) {
        err.SetErrorStringWithFormat(
            "couldn't get a value object for variable %s",
            GetName().AsCString());
        return;
      }

      lldb_private::DataExtractor data;

      Status extract_error;

      map.GetMemoryData(data, m_temporary_allocation,
                        valobj_sp->GetByteSize().value_or(0), extract_error);

      if (!extract_error.Success()) {
        err.SetErrorStringWithFormat("couldn't get the data for variable %s",
                                     GetName().AsCString());
        return;
      }

      bool actually_write = true;

      if (m_original_data) {
        if ((data.GetByteSize() == m_original_data->GetByteSize()) &&
            !memcmp(m_original_data->GetBytes(), data.GetDataStart(),
                    data.GetByteSize())) {
          actually_write = false;
        }
      }

      Status set_error;

      if (actually_write) {
        valobj_sp->SetData(data, set_error);

        if (!set_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't write the new contents of %s back into the variable",
              GetName().AsCString());
          return;
        }
      }

      Status free_error;

      map.Free(m_temporary_allocation, free_error);

      if (!free_error.Success()) {
        err.SetErrorStringWithFormat(
            "couldn't free the temporary region for %s: %s",
            GetName().AsCString(), free_error.AsCString());
        return;
      }

      m_original_data.reset();
      m_temporary_allocation = LLDB_INVALID_ADDRESS;
      m_temporary_allocation_size = 0;
    }
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    const lldb::addr_t load_addr = process_address + m_offset;
    dump_stream.Printf("0x%" PRIx64 ": EntityVariable\n", load_addr);

    Status err;

    lldb::addr_t ptr = LLDB_INVALID_ADDRESS;

    {
      dump_stream.Printf("Pointer:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                                map.GetByteOrder(), map.GetAddressByteSize());

        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        lldb::offset_t offset = 0;

        ptr = extractor.GetAddress(&offset);

        dump_stream.PutChar('\n');
      }
    }

    if (m_temporary_allocation == LLDB_INVALID_ADDRESS) {
      dump_stream.Printf("Points to process memory:\n");
    } else {
      dump_stream.Printf("Temporary allocation:\n");
    }

    if (ptr == LLDB_INVALID_ADDRESS) {
      dump_stream.Printf("  <could not be be found>\n");
    } else {
      DataBufferHeap data(m_temporary_allocation_size, 0);

      map.ReadMemory(data.GetBytes(), m_temporary_allocation,
                     m_temporary_allocation_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {
    if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
      Status free_error;

      map.Free(m_temporary_allocation, free_error);

      m_temporary_allocation = LLDB_INVALID_ADDRESS;
      m_temporary_allocation_size = 0;
    }
  }

private:
  virtual ConstString GetName() const = 0;

  /// Creates and returns ValueObject tied to this variable
  /// and prepares Entity for materialization.
  ///
  /// Called each time the Materializer (de)materializes a
  /// variable. We re-create the ValueObject based on the
  /// current ExecutionContextScope since clients such as
  /// conditional breakpoints may materialize the same
  /// EntityVariable multiple times with different frames.
  ///
  /// Each subsequent use of the EntityVariableBase interface
  /// will query the newly created ValueObject until this
  /// function is called again.
  virtual lldb::ValueObjectSP
  SetupValueObject(ExecutionContextScope *scope) = 0;

  /// Returns size in bytes of the type associated with this variable
  ///
  /// \returns On success, returns byte size of the type associated
  ///          with this variable. Returns std::nullopt otherwise.
  virtual std::optional<uint64_t>
  GetByteSize(ExecutionContextScope *scope) const = 0;

  /// Returns 'true' if the location expression associated with this variable
  /// is valid.
  virtual bool LocationExpressionIsValid() const = 0;

  /// Returns alignment of the type associated with this variable in bits.
  ///
  /// \returns On success, returns alignment in bits for the type associated
  ///          with this variable. Returns std::nullopt otherwise.
  virtual std::optional<size_t>
  GetTypeBitAlign(ExecutionContextScope *scope) const = 0;

protected:
  bool m_is_reference = false;
  lldb::addr_t m_temporary_allocation = LLDB_INVALID_ADDRESS;
  size_t m_temporary_allocation_size = 0;
  lldb::DataBufferSP m_original_data;
};

/// Represents an Entity constructed from a VariableSP.
///
/// This class is used for materialization of variables for which
/// the user has a VariableSP on hand. The ValueObject is then
/// derived from the associated DWARF location expression when needed
/// by the Materializer.
class EntityVariable : public EntityVariableBase {
public:
  EntityVariable(lldb::VariableSP &variable_sp) : m_variable_sp(variable_sp) {
    m_is_reference =
        m_variable_sp->GetType()->GetForwardCompilerType().IsReferenceType();
  }

  ConstString GetName() const override { return m_variable_sp->GetName(); }

  lldb::ValueObjectSP SetupValueObject(ExecutionContextScope *scope) override {
    assert(m_variable_sp != nullptr);
    return ValueObjectVariable::Create(scope, m_variable_sp);
  }

  std::optional<uint64_t>
  GetByteSize(ExecutionContextScope *scope) const override {
    return m_variable_sp->GetType()->GetByteSize(scope);
  }

  bool LocationExpressionIsValid() const override {
    return m_variable_sp->LocationExpressionList().IsValid();
  }

  std::optional<size_t>
  GetTypeBitAlign(ExecutionContextScope *scope) const override {
    return m_variable_sp->GetType()->GetLayoutCompilerType().GetTypeBitAlign(
        scope);
  }

private:
  lldb::VariableSP m_variable_sp; ///< Variable that this entity is based on.
};

/// Represents an Entity constructed from a VariableSP.
///
/// This class is used for materialization of variables for
/// which the user does not have a VariableSP available (e.g.,
/// when materializing ivars).
class EntityValueObject : public EntityVariableBase {
public:
  EntityValueObject(ConstString name, ValueObjectProviderTy provider)
      : m_name(name), m_valobj_provider(std::move(provider)) {
    assert(m_valobj_provider);
  }

  ConstString GetName() const override { return m_name; }

  lldb::ValueObjectSP SetupValueObject(ExecutionContextScope *scope) override {
    m_valobj_sp =
        m_valobj_provider(GetName(), scope->CalculateStackFrame().get());

    if (m_valobj_sp)
      m_is_reference = m_valobj_sp->GetCompilerType().IsReferenceType();

    return m_valobj_sp;
  }

  std::optional<uint64_t>
  GetByteSize(ExecutionContextScope *scope) const override {
    if (m_valobj_sp)
      return m_valobj_sp->GetCompilerType().GetByteSize(scope);

    return {};
  }

  bool LocationExpressionIsValid() const override {
    if (m_valobj_sp)
      return m_valobj_sp->GetError().Success();

    return false;
  }

  std::optional<size_t>
  GetTypeBitAlign(ExecutionContextScope *scope) const override {
    if (m_valobj_sp)
      return m_valobj_sp->GetCompilerType().GetTypeBitAlign(scope);

    return {};
  }

private:
  ConstString m_name;
  lldb::ValueObjectSP m_valobj_sp;
  ValueObjectProviderTy m_valobj_provider;
};

uint32_t Materializer::AddVariable(lldb::VariableSP &variable_sp, Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  *iter = std::make_unique<EntityVariable>(variable_sp);
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

uint32_t Materializer::AddValueObject(ConstString name,
                                      ValueObjectProviderTy valobj_provider,
                                      Status &err) {
  assert(valobj_provider);
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  *iter = std::make_unique<EntityValueObject>(name, std::move(valobj_provider));
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

class EntityResultVariable : public Materializer::Entity {
public:
  EntityResultVariable(const CompilerType &type, bool is_program_reference,
                       bool keep_in_memory,
                       Materializer::PersistentVariableDelegate *delegate)
      : Entity(), m_type(type), m_is_program_reference(is_program_reference),
        m_keep_in_memory(keep_in_memory), m_delegate(delegate) {
    // Hard-coding to maximum size of a pointer since all results are
    // materialized by reference
    m_size = g_default_var_byte_size;
    m_alignment = g_default_var_alignment;
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    if (!m_is_program_reference) {
      if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
        err.SetErrorString("Trying to create a temporary region for the result "
                           "but one exists");
        return;
      }

      const lldb::addr_t load_addr = process_address + m_offset;

      ExecutionContextScope *exe_scope = frame_sp.get();
      if (!exe_scope)
        exe_scope = map.GetBestExecutionContextScope();

      std::optional<uint64_t> byte_size = m_type.GetByteSize(exe_scope);
      if (!byte_size) {
        err.SetErrorStringWithFormat("can't get size of type \"%s\"",
                                     m_type.GetTypeName().AsCString());
        return;
      }

      std::optional<size_t> opt_bit_align = m_type.GetTypeBitAlign(exe_scope);
      if (!opt_bit_align) {
        err.SetErrorStringWithFormat("can't get the alignment of type  \"%s\"",
                                     m_type.GetTypeName().AsCString());
        return;
      }

      size_t byte_align = (*opt_bit_align + 7) / 8;

      Status alloc_error;
      const bool zero_memory = true;

      m_temporary_allocation = map.Malloc(
          *byte_size, byte_align,
          lldb::ePermissionsReadable | lldb::ePermissionsWritable,
          IRMemoryMap::eAllocationPolicyMirror, zero_memory, alloc_error);
      m_temporary_allocation_size = *byte_size;

      if (!alloc_error.Success()) {
        err.SetErrorStringWithFormat(
            "couldn't allocate a temporary region for the result: %s",
            alloc_error.AsCString());
        return;
      }

      Status pointer_write_error;

      map.WritePointerToMemory(load_addr, m_temporary_allocation,
                               pointer_write_error);

      if (!pointer_write_error.Success()) {
        err.SetErrorStringWithFormat("couldn't write the address of the "
                                     "temporary region for the result: %s",
                                     pointer_write_error.AsCString());
      }
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    err.Clear();

    ExecutionContextScope *exe_scope = frame_sp.get();
    if (!exe_scope)
      exe_scope = map.GetBestExecutionContextScope();

    if (!exe_scope) {
      err.SetErrorString("Couldn't dematerialize a result variable: invalid "
                         "execution context scope");
      return;
    }

    lldb::addr_t address;
    Status read_error;
    const lldb::addr_t load_addr = process_address + m_offset;

    map.ReadPointerFromMemory(&address, load_addr, read_error);

    if (!read_error.Success()) {
      err.SetErrorString("Couldn't dematerialize a result variable: couldn't "
                         "read its address");
      return;
    }

    lldb::TargetSP target_sp = exe_scope->CalculateTarget();

    if (!target_sp) {
      err.SetErrorString("Couldn't dematerialize a result variable: no target");
      return;
    }

    auto type_system_or_err =
        target_sp->GetScratchTypeSystemForLanguage(m_type.GetMinimumLanguage());

    if (auto error = type_system_or_err.takeError()) {
      err.SetErrorStringWithFormat("Couldn't dematerialize a result variable: "
                                   "couldn't get the corresponding type "
                                   "system: %s",
                                   llvm::toString(std::move(error)).c_str());
      return;
    }
    auto ts = *type_system_or_err;
    if (!ts) {
      err.SetErrorStringWithFormat("Couldn't dematerialize a result variable: "
                                   "couldn't corresponding type system is "
                                   "no longer live.");
      return;
    }
    PersistentExpressionState *persistent_state =
        ts->GetPersistentExpressionState();

    if (!persistent_state) {
      err.SetErrorString("Couldn't dematerialize a result variable: "
                         "corresponding type system doesn't handle persistent "
                         "variables");
      return;
    }

    ConstString name = m_delegate
                           ? m_delegate->GetName()
                           : persistent_state->GetNextPersistentVariableName();

    lldb::ExpressionVariableSP ret = persistent_state->CreatePersistentVariable(
        exe_scope, name, m_type, map.GetByteOrder(), map.GetAddressByteSize());

    if (!ret) {
      err.SetErrorStringWithFormat("couldn't dematerialize a result variable: "
                                   "failed to make persistent variable %s",
                                   name.AsCString());
      return;
    }

    lldb::ProcessSP process_sp =
        map.GetBestExecutionContextScope()->CalculateProcess();

    if (m_delegate) {
      m_delegate->DidDematerialize(ret);
    }

    bool can_persist =
        (m_is_program_reference && process_sp && process_sp->CanJIT() &&
         !(address >= frame_bottom && address < frame_top));

    if (can_persist && m_keep_in_memory) {
      ret->m_live_sp = ValueObjectConstResult::Create(exe_scope, m_type, name,
                                                      address, eAddressTypeLoad,
                                                      map.GetAddressByteSize());
    }

    ret->ValueUpdated();

    const size_t pvar_byte_size = ret->GetByteSize().value_or(0);
    uint8_t *pvar_data = ret->GetValueBytes();

    map.ReadMemory(pvar_data, address, pvar_byte_size, read_error);

    if (!read_error.Success()) {
      err.SetErrorString(
          "Couldn't dematerialize a result variable: couldn't read its memory");
      return;
    }

    if (!can_persist || !m_keep_in_memory) {
      ret->m_flags |= ExpressionVariable::EVNeedsAllocation;

      if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
        Status free_error;
        map.Free(m_temporary_allocation, free_error);
      }
    } else {
      ret->m_flags |= ExpressionVariable::EVIsLLDBAllocated;
    }

    m_temporary_allocation = LLDB_INVALID_ADDRESS;
    m_temporary_allocation_size = 0;
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    const lldb::addr_t load_addr = process_address + m_offset;

    dump_stream.Printf("0x%" PRIx64 ": EntityResultVariable\n", load_addr);

    Status err;

    lldb::addr_t ptr = LLDB_INVALID_ADDRESS;

    {
      dump_stream.Printf("Pointer:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                                map.GetByteOrder(), map.GetAddressByteSize());

        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        lldb::offset_t offset = 0;

        ptr = extractor.GetAddress(&offset);

        dump_stream.PutChar('\n');
      }
    }

    if (m_temporary_allocation == LLDB_INVALID_ADDRESS) {
      dump_stream.Printf("Points to process memory:\n");
    } else {
      dump_stream.Printf("Temporary allocation:\n");
    }

    if (ptr == LLDB_INVALID_ADDRESS) {
      dump_stream.Printf("  <could not be be found>\n");
    } else {
      DataBufferHeap data(m_temporary_allocation_size, 0);

      map.ReadMemory(data.GetBytes(), m_temporary_allocation,
                     m_temporary_allocation_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {
    if (!m_keep_in_memory && m_temporary_allocation != LLDB_INVALID_ADDRESS) {
      Status free_error;

      map.Free(m_temporary_allocation, free_error);
    }

    m_temporary_allocation = LLDB_INVALID_ADDRESS;
    m_temporary_allocation_size = 0;
  }

private:
  CompilerType m_type;
  bool m_is_program_reference;
  bool m_keep_in_memory;

  lldb::addr_t m_temporary_allocation = LLDB_INVALID_ADDRESS;
  size_t m_temporary_allocation_size = 0;
  Materializer::PersistentVariableDelegate *m_delegate;
};

uint32_t Materializer::AddResultVariable(const CompilerType &type,
                                         bool is_program_reference,
                                         bool keep_in_memory,
                                         PersistentVariableDelegate *delegate,
                                         Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  *iter = std::make_unique<EntityResultVariable>(type, is_program_reference,
                                                 keep_in_memory, delegate);
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

class EntitySymbol : public Materializer::Entity {
public:
  EntitySymbol(const Symbol &symbol) : Entity(), m_symbol(symbol) {
    // Hard-coding to maximum size of a symbol
    m_size = g_default_var_byte_size;
    m_alignment = g_default_var_alignment;
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    Log *log = GetLog(LLDBLog::Expressions);

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      LLDB_LOGF(log,
                "EntitySymbol::Materialize [address = 0x%" PRIx64
                ", m_symbol = %s]",
                (uint64_t)load_addr, m_symbol.GetName().AsCString());
    }

    const Address sym_address = m_symbol.GetAddress();

    ExecutionContextScope *exe_scope = frame_sp.get();
    if (!exe_scope)
      exe_scope = map.GetBestExecutionContextScope();

    lldb::TargetSP target_sp;

    if (exe_scope)
      target_sp = map.GetBestExecutionContextScope()->CalculateTarget();

    if (!target_sp) {
      err.SetErrorStringWithFormat(
          "couldn't resolve symbol %s because there is no target",
          m_symbol.GetName().AsCString());
      return;
    }

    lldb::addr_t resolved_address = sym_address.GetLoadAddress(target_sp.get());

    if (resolved_address == LLDB_INVALID_ADDRESS)
      resolved_address = sym_address.GetFileAddress();

    Status pointer_write_error;

    map.WritePointerToMemory(load_addr, resolved_address, pointer_write_error);

    if (!pointer_write_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't write the address of symbol %s: %s",
          m_symbol.GetName().AsCString(), pointer_write_error.AsCString());
      return;
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    Log *log = GetLog(LLDBLog::Expressions);

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      LLDB_LOGF(log,
                "EntitySymbol::Dematerialize [address = 0x%" PRIx64
                ", m_symbol = %s]",
                (uint64_t)load_addr, m_symbol.GetName().AsCString());
    }

    // no work needs to be done
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    Status err;

    const lldb::addr_t load_addr = process_address + m_offset;

    dump_stream.Printf("0x%" PRIx64 ": EntitySymbol (%s)\n", load_addr,
                       m_symbol.GetName().AsCString());

    {
      dump_stream.Printf("Pointer:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {}

private:
  Symbol m_symbol;
};

uint32_t Materializer::AddSymbol(const Symbol &symbol_sp, Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  *iter = std::make_unique<EntitySymbol>(symbol_sp);
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

class EntityRegister : public Materializer::Entity {
public:
  EntityRegister(const RegisterInfo &register_info)
      : Entity(), m_register_info(register_info) {
    // Hard-coding alignment conservatively
    m_size = m_register_info.byte_size;
    m_alignment = m_register_info.byte_size;
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    Log *log = GetLog(LLDBLog::Expressions);

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      LLDB_LOGF(log,
                "EntityRegister::Materialize [address = 0x%" PRIx64
                ", m_register_info = %s]",
                (uint64_t)load_addr, m_register_info.name);
    }

    RegisterValue reg_value;

    if (!frame_sp.get()) {
      err.SetErrorStringWithFormat(
          "couldn't materialize register %s without a stack frame",
          m_register_info.name);
      return;
    }

    lldb::RegisterContextSP reg_context_sp = frame_sp->GetRegisterContext();

    if (!reg_context_sp->ReadRegister(&m_register_info, reg_value)) {
      err.SetErrorStringWithFormat("couldn't read the value of register %s",
                                   m_register_info.name);
      return;
    }

    DataExtractor register_data;

    if (!reg_value.GetData(register_data)) {
      err.SetErrorStringWithFormat("couldn't get the data for register %s",
                                   m_register_info.name);
      return;
    }

    if (register_data.GetByteSize() != m_register_info.byte_size) {
      err.SetErrorStringWithFormat(
          "data for register %s had size %llu but we expected %llu",
          m_register_info.name, (unsigned long long)register_data.GetByteSize(),
          (unsigned long long)m_register_info.byte_size);
      return;
    }

    m_register_contents = std::make_shared<DataBufferHeap>(
        register_data.GetDataStart(), register_data.GetByteSize());

    Status write_error;

    map.WriteMemory(load_addr, register_data.GetDataStart(),
                    register_data.GetByteSize(), write_error);

    if (!write_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't write the contents of register %s: %s",
          m_register_info.name, write_error.AsCString());
      return;
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    Log *log = GetLog(LLDBLog::Expressions);

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      LLDB_LOGF(log,
                "EntityRegister::Dematerialize [address = 0x%" PRIx64
                ", m_register_info = %s]",
                (uint64_t)load_addr, m_register_info.name);
    }

    Status extract_error;

    DataExtractor register_data;

    if (!frame_sp.get()) {
      err.SetErrorStringWithFormat(
          "couldn't dematerialize register %s without a stack frame",
          m_register_info.name);
      return;
    }

    lldb::RegisterContextSP reg_context_sp = frame_sp->GetRegisterContext();

    map.GetMemoryData(register_data, load_addr, m_register_info.byte_size,
                      extract_error);

    if (!extract_error.Success()) {
      err.SetErrorStringWithFormat("couldn't get the data for register %s: %s",
                                   m_register_info.name,
                                   extract_error.AsCString());
      return;
    }

    if (!memcmp(register_data.GetDataStart(), m_register_contents->GetBytes(),
                register_data.GetByteSize())) {
      // No write required, and in particular we avoid errors if the register
      // wasn't writable

      m_register_contents.reset();
      return;
    }

    m_register_contents.reset();

    RegisterValue register_value(register_data.GetData(),
                                 register_data.GetByteOrder());

    if (!reg_context_sp->WriteRegister(&m_register_info, register_value)) {
      err.SetErrorStringWithFormat("couldn't write the value of register %s",
                                   m_register_info.name);
      return;
    }
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    Status err;

    const lldb::addr_t load_addr = process_address + m_offset;

    dump_stream.Printf("0x%" PRIx64 ": EntityRegister (%s)\n", load_addr,
                       m_register_info.name);

    {
      dump_stream.Printf("Value:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {}

private:
  RegisterInfo m_register_info;
  lldb::DataBufferSP m_register_contents;
};

uint32_t Materializer::AddRegister(const RegisterInfo &register_info,
                                   Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  *iter = std::make_unique<EntityRegister>(register_info);
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

Materializer::~Materializer() {
  DematerializerSP dematerializer_sp = m_dematerializer_wp.lock();

  if (dematerializer_sp)
    dematerializer_sp->Wipe();
}

Materializer::DematerializerSP
Materializer::Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                          lldb::addr_t process_address, Status &error) {
  ExecutionContextScope *exe_scope = frame_sp.get();
  if (!exe_scope)
    exe_scope = map.GetBestExecutionContextScope();

  DematerializerSP dematerializer_sp = m_dematerializer_wp.lock();

  if (dematerializer_sp) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't materialize: already materialized");
  }

  DematerializerSP ret(
      new Dematerializer(*this, frame_sp, map, process_address));

  if (!exe_scope) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't materialize: target doesn't exist");
  }

  for (EntityUP &entity_up : m_entities) {
    entity_up->Materialize(frame_sp, map, process_address, error);

    if (!error.Success())
      return DematerializerSP();
  }

  if (Log *log = GetLog(LLDBLog::Expressions)) {
    LLDB_LOGF(
        log,
        "Materializer::Materialize (frame_sp = %p, process_address = 0x%" PRIx64
        ") materialized:",
        static_cast<void *>(frame_sp.get()), process_address);
    for (EntityUP &entity_up : m_entities)
      entity_up->DumpToLog(map, process_address, log);
  }

  m_dematerializer_wp = ret;

  return ret;
}

void Materializer::Dematerializer::Dematerialize(Status &error,
                                                 lldb::addr_t frame_bottom,
                                                 lldb::addr_t frame_top) {
  lldb::StackFrameSP frame_sp;

  lldb::ThreadSP thread_sp = m_thread_wp.lock();
  if (thread_sp)
    frame_sp = thread_sp->GetFrameWithStackID(m_stack_id);

  ExecutionContextScope *exe_scope = frame_sp.get();
  if (!exe_scope)
    exe_scope = m_map->GetBestExecutionContextScope();

  if (!IsValid()) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't dematerialize: invalid dematerializer");
  }

  if (!exe_scope) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't dematerialize: target is gone");
  } else {
    if (Log *log = GetLog(LLDBLog::Expressions)) {
      LLDB_LOGF(log,
                "Materializer::Dematerialize (frame_sp = %p, process_address "
                "= 0x%" PRIx64 ") about to dematerialize:",
                static_cast<void *>(frame_sp.get()), m_process_address);
      for (EntityUP &entity_up : m_materializer->m_entities)
        entity_up->DumpToLog(*m_map, m_process_address, log);
    }

    for (EntityUP &entity_up : m_materializer->m_entities) {
      entity_up->Dematerialize(frame_sp, *m_map, m_process_address, frame_top,
                               frame_bottom, error);

      if (!error.Success())
        break;
    }
  }

  Wipe();
}

void Materializer::Dematerializer::Wipe() {
  if (!IsValid())
    return;

  for (EntityUP &entity_up : m_materializer->m_entities) {
    entity_up->Wipe(*m_map, m_process_address);
  }

  m_materializer = nullptr;
  m_map = nullptr;
  m_process_address = LLDB_INVALID_ADDRESS;
}

Materializer::PersistentVariableDelegate::PersistentVariableDelegate() =
    default;
Materializer::PersistentVariableDelegate::~PersistentVariableDelegate() =
    default;
