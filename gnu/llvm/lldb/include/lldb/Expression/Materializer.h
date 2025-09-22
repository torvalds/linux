//===-- Materializer.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_MATERIALIZER_H
#define LLDB_EXPRESSION_MATERIALIZER_H

#include <memory>
#include <vector>

#include "lldb/Expression/IRMemoryMap.h"
#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-types.h"

namespace lldb_private {

class Materializer {
public:
  Materializer() = default;
  ~Materializer();

  class Dematerializer {
  public:
    Dematerializer() = default;

    ~Dematerializer() { Wipe(); }

    void Dematerialize(Status &err, lldb::addr_t frame_top,
                       lldb::addr_t frame_bottom);

    void Wipe();

    bool IsValid() {
      return m_materializer && m_map &&
             (m_process_address != LLDB_INVALID_ADDRESS);
    }

  private:
    friend class Materializer;

    Dematerializer(Materializer &materializer, lldb::StackFrameSP &frame_sp,
                   IRMemoryMap &map, lldb::addr_t process_address)
        : m_materializer(&materializer), m_map(&map),
          m_process_address(process_address) {
      if (frame_sp) {
        m_thread_wp = frame_sp->GetThread();
        m_stack_id = frame_sp->GetStackID();
      }
    }

    Materializer *m_materializer = nullptr;
    lldb::ThreadWP m_thread_wp;
    StackID m_stack_id;
    IRMemoryMap *m_map = nullptr;
    lldb::addr_t m_process_address = LLDB_INVALID_ADDRESS;
  };

  typedef std::shared_ptr<Dematerializer> DematerializerSP;
  typedef std::weak_ptr<Dematerializer> DematerializerWP;

  DematerializerSP Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                               lldb::addr_t process_address, Status &err);

  class PersistentVariableDelegate {
  public:
    PersistentVariableDelegate();
    virtual ~PersistentVariableDelegate();
    virtual ConstString GetName() = 0;
    virtual void DidDematerialize(lldb::ExpressionVariableSP &variable) = 0;
  };

  uint32_t
  AddPersistentVariable(lldb::ExpressionVariableSP &persistent_variable_sp,
                        PersistentVariableDelegate *delegate, Status &err);
  uint32_t AddVariable(lldb::VariableSP &variable_sp, Status &err);

  /// Create entity from supplied ValueObject and count it as a member
  /// of the materialized struct.
  ///
  /// Behaviour is undefined if 'valobj_provider' is empty.
  ///
  /// \param[in] name Name of variable to materialize
  ///
  /// \param[in] valobj_provider When materializing values multiple
  ///            times, this callback gets used to fetch a fresh
  ///            ValueObject corresponding to the supplied frame.
  ///            This is mainly used for conditional breakpoints
  ///            that re-apply an expression whatever the frame
  ///            happens to be when the breakpoint got hit.
  ///
  /// \param[out] err Error status that gets set on error.
  ///
  /// \returns Offset in bytes of the member we just added to the
  ///          materialized struct.
  uint32_t AddValueObject(ConstString name,
                          ValueObjectProviderTy valobj_provider, Status &err);

  uint32_t AddResultVariable(const CompilerType &type, bool is_lvalue,
                             bool keep_in_memory,
                             PersistentVariableDelegate *delegate, Status &err);
  uint32_t AddSymbol(const Symbol &symbol_sp, Status &err);
  uint32_t AddRegister(const RegisterInfo &register_info, Status &err);

  uint32_t GetStructAlignment() { return m_struct_alignment; }

  uint32_t GetStructByteSize() { return m_current_offset; }

  class Entity {
  public:
    Entity() = default;

    virtual ~Entity() = default;

    virtual void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                             lldb::addr_t process_address, Status &err) = 0;
    virtual void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                               lldb::addr_t process_address,
                               lldb::addr_t frame_top,
                               lldb::addr_t frame_bottom, Status &err) = 0;
    virtual void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                           Log *log) = 0;
    virtual void Wipe(IRMemoryMap &map, lldb::addr_t process_address) = 0;

    uint32_t GetAlignment() { return m_alignment; }

    uint32_t GetSize() { return m_size; }

    uint32_t GetOffset() { return m_offset; }

    void SetOffset(uint32_t offset) { m_offset = offset; }

  protected:
    uint32_t m_alignment = 1;
    uint32_t m_size = 0;
    uint32_t m_offset = 0;
  };

private:
  uint32_t AddStructMember(Entity &entity);

  typedef std::unique_ptr<Entity> EntityUP;
  typedef std::vector<EntityUP> EntityVector;

  DematerializerWP m_dematerializer_wp;
  EntityVector m_entities;
  uint32_t m_current_offset = 0;
  uint32_t m_struct_alignment = 8;
};

} // namespace lldb_private

#endif // LLDB_EXPRESSION_MATERIALIZER_H
