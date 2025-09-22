//===-- Watchpoint.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_WATCHPOINT_H
#define LLDB_BREAKPOINT_WATCHPOINT_H

#include <memory>
#include <string>

#include "lldb/Breakpoint/StoppointSite.h"
#include "lldb/Breakpoint/WatchpointOptions.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class Watchpoint : public std::enable_shared_from_this<Watchpoint>,
                   public StoppointSite {
public:
  class WatchpointEventData : public EventData {
  public:
    WatchpointEventData(lldb::WatchpointEventType sub_type,
                        const lldb::WatchpointSP &new_watchpoint_sp);

    ~WatchpointEventData() override;

    static llvm::StringRef GetFlavorString();

    llvm::StringRef GetFlavor() const override;

    lldb::WatchpointEventType GetWatchpointEventType() const;

    lldb::WatchpointSP &GetWatchpoint();

    void Dump(Stream *s) const override;

    static lldb::WatchpointEventType
    GetWatchpointEventTypeFromEvent(const lldb::EventSP &event_sp);

    static lldb::WatchpointSP
    GetWatchpointFromEvent(const lldb::EventSP &event_sp);

    static const WatchpointEventData *
    GetEventDataFromEvent(const Event *event_sp);

  private:
    lldb::WatchpointEventType m_watchpoint_event;
    lldb::WatchpointSP m_new_watchpoint_sp;

    WatchpointEventData(const WatchpointEventData &) = delete;
    const WatchpointEventData &operator=(const WatchpointEventData &) = delete;
  };

  Watchpoint(Target &target, lldb::addr_t addr, uint32_t size,
             const CompilerType *type, bool hardware = true);

  ~Watchpoint() override;

  bool IsEnabled() const;

  // This doesn't really enable/disable the watchpoint.   It is currently just
  // for use in the Process plugin's {Enable,Disable}Watchpoint, which should
  // be used instead.
  void SetEnabled(bool enabled, bool notify = true);

  bool IsHardware() const override;

  bool ShouldStop(StoppointCallbackContext *context) override;
  
  bool WatchpointRead() const;
  bool WatchpointWrite() const;
  bool WatchpointModify() const;
  uint32_t GetIgnoreCount() const;
  void SetIgnoreCount(uint32_t n);
  void SetWatchpointType(uint32_t type, bool notify = true);
  void SetDeclInfo(const std::string &str);
  std::string GetWatchSpec();
  void SetWatchSpec(const std::string &str);
  bool WatchedValueReportable(const ExecutionContext &exe_ctx);

  // Snapshot management interface.
  bool IsWatchVariable() const;
  void SetWatchVariable(bool val);
  bool CaptureWatchedValue(const ExecutionContext &exe_ctx);

  /// \struct WatchpointVariableContext
  /// \brief Represents the context of a watchpoint variable.
  ///
  /// This struct encapsulates the information related to a watchpoint variable,
  /// including the watch ID and the execution context in which it is being
  /// used. This struct is passed as a Baton to the \b
  /// VariableWatchpointDisabler breakpoint callback.
  struct WatchpointVariableContext {
    /// \brief Constructor for WatchpointVariableContext.
    /// \param watch_id The ID of the watchpoint.
    /// \param exe_ctx The execution context associated with the watchpoint.
    WatchpointVariableContext(lldb::watch_id_t watch_id,
                              ExecutionContext exe_ctx)
        : watch_id(watch_id), exe_ctx(exe_ctx) {}

    lldb::watch_id_t watch_id; ///< The ID of the watchpoint.
    ExecutionContext
        exe_ctx; ///< The execution context associated with the watchpoint.
  };

  class WatchpointVariableBaton : public TypedBaton<WatchpointVariableContext> {
  public:
    WatchpointVariableBaton(std::unique_ptr<WatchpointVariableContext> Data)
        : TypedBaton(std::move(Data)) {}
  };

  bool SetupVariableWatchpointDisabler(lldb::StackFrameSP frame_sp) const;

  /// Callback routine to disable the watchpoint set on a local variable when
  ///  it goes out of scope.
  static bool VariableWatchpointDisabler(
      void *baton, lldb_private::StoppointCallbackContext *context,
      lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  void GetDescription(Stream *s, lldb::DescriptionLevel level);
  void Dump(Stream *s) const override;
  bool DumpSnapshots(Stream *s, const char *prefix = nullptr) const;
  void DumpWithLevel(Stream *s, lldb::DescriptionLevel description_level) const;
  Target &GetTarget() { return m_target; }
  const Status &GetError() { return m_error; }

  /// Returns the WatchpointOptions structure set for this watchpoint.
  ///
  /// \return
  ///     A pointer to this watchpoint's WatchpointOptions.
  WatchpointOptions *GetOptions() { return &m_options; }

  /// Set the callback action invoked when the watchpoint is hit.
  ///
  /// \param[in] callback
  ///    The method that will get called when the watchpoint is hit.
  /// \param[in] callback_baton
  ///    A void * pointer that will get passed back to the callback function.
  /// \param[in] is_synchronous
  ///    If \b true the callback will be run on the private event thread
  ///    before the stop event gets reported.  If false, the callback will get
  ///    handled on the public event thread after the stop has been posted.
  void SetCallback(WatchpointHitCallback callback, void *callback_baton,
                   bool is_synchronous = false);

  void SetCallback(WatchpointHitCallback callback,
                   const lldb::BatonSP &callback_baton_sp,
                   bool is_synchronous = false);

  void ClearCallback();

  /// Invoke the callback action when the watchpoint is hit.
  ///
  /// \param[in] context
  ///     Described the watchpoint event.
  ///
  /// \return
  ///     \b true if the target should stop at this watchpoint and \b false not.
  bool InvokeCallback(StoppointCallbackContext *context);

  // Condition
  /// Set the watchpoint's condition.
  ///
  /// \param[in] condition
  ///    The condition expression to evaluate when the watchpoint is hit.
  ///    Pass in nullptr to clear the condition.
  void SetCondition(const char *condition);

  /// Return a pointer to the text of the condition expression.
  ///
  /// \return
  ///    A pointer to the condition expression text, or nullptr if no
  //     condition has been set.
  const char *GetConditionText() const;

  void TurnOnEphemeralMode();

  void TurnOffEphemeralMode();

  bool IsDisabledDuringEphemeralMode();

  const CompilerType &GetCompilerType() { return m_type; }

private:
  friend class Target;
  friend class WatchpointList;
  friend class StopInfoWatchpoint; // This needs to call UndoHitCount()

  void ResetHistoricValues() {
    m_old_value_sp.reset();
    m_new_value_sp.reset();
  }

  void UndoHitCount() { m_hit_counter.Decrement(); }

  Target &m_target;
  bool m_enabled;           // Is this watchpoint enabled
  bool m_is_hardware;       // Is this a hardware watchpoint
  bool m_is_watch_variable; // True if set via 'watchpoint set variable'.
  bool m_is_ephemeral;      // True if the watchpoint is in the ephemeral mode,
                            // meaning that it is
  // undergoing a pair of temporary disable/enable actions to avoid recursively
  // triggering further watchpoint events.
  uint32_t m_disabled_count; // Keep track of the count that the watchpoint is
                             // disabled while in ephemeral mode.
  // At the end of the ephemeral mode when the watchpoint is to be enabled
  // again, we check the count, if it is more than 1, it means the user-
  // supplied actions actually want the watchpoint to be disabled!
  uint32_t m_watch_read : 1, // 1 if we stop when the watched data is read from
      m_watch_write : 1,     // 1 if we stop when the watched data is written to
      m_watch_modify : 1;    // 1 if we stop when the watched data is changed
  uint32_t m_ignore_count;      // Number of times to ignore this watchpoint
  std::string m_decl_str;       // Declaration information, if any.
  std::string m_watch_spec_str; // Spec for the watchpoint.
  lldb::ValueObjectSP m_old_value_sp;
  lldb::ValueObjectSP m_new_value_sp;
  CompilerType m_type;
  Status m_error; // An error object describing errors associated with this
                  // watchpoint.
  WatchpointOptions m_options; // Settable watchpoint options, which is a
                               // delegate to handle the callback machinery.
  std::unique_ptr<UserExpression> m_condition_up; // The condition to test.

  void SetID(lldb::watch_id_t id) { m_id = id; }

  void SendWatchpointChangedEvent(lldb::WatchpointEventType eventKind);

  Watchpoint(const Watchpoint &) = delete;
  const Watchpoint &operator=(const Watchpoint &) = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_WATCHPOINT_H
