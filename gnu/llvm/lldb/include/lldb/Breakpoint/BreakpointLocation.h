//===-- BreakpointLocation.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_BREAKPOINTLOCATION_H
#define LLDB_BREAKPOINT_BREAKPOINTLOCATION_H

#include <memory>
#include <mutex>

#include "lldb/Breakpoint/BreakpointOptions.h"
#include "lldb/Breakpoint/StoppointHitCounter.h"
#include "lldb/Core/Address.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// \class BreakpointLocation BreakpointLocation.h
/// "lldb/Breakpoint/BreakpointLocation.h" Class that manages one unique (by
/// address) instance of a logical breakpoint.

/// General Outline:
/// A breakpoint location is defined by the breakpoint that produces it,
/// and the address that resulted in this particular instantiation. Each
/// breakpoint location also may have a breakpoint site if its address has
/// been loaded into the program. Finally it has a settable options object.
///
/// FIXME: Should we also store some fingerprint for the location, so
/// we can map one location to the "equivalent location" on rerun?  This would
/// be useful if you've set options on the locations.

class BreakpointLocation
    : public std::enable_shared_from_this<BreakpointLocation> {
public:
  ~BreakpointLocation();

  /// Gets the load address for this breakpoint location \return
  ///     Returns breakpoint location load address, \b
  ///     LLDB_INVALID_ADDRESS if not yet set.
  lldb::addr_t GetLoadAddress() const;

  /// Gets the Address for this breakpoint location \return
  ///     Returns breakpoint location Address.
  Address &GetAddress();
  /// Gets the Breakpoint that created this breakpoint location \return
  ///     Returns the owning breakpoint.
  Breakpoint &GetBreakpoint();

  Target &GetTarget();

  /// Determines whether we should stop due to a hit at this breakpoint
  /// location.
  ///
  /// Side Effects: This may evaluate the breakpoint condition, and run the
  /// callback.  So this command may do a considerable amount of work.
  ///
  /// \return
  ///     \b true if this breakpoint location thinks we should stop,
  ///     \b false otherwise.
  bool ShouldStop(StoppointCallbackContext *context);

  // The next section deals with various breakpoint options.

  /// If \a enabled is \b true, enable the breakpoint, if \b false disable it.
  void SetEnabled(bool enabled);

  /// Check the Enable/Disable state.
  ///
  /// \return
  ///     \b true if the breakpoint is enabled, \b false if disabled.
  bool IsEnabled() const;

  /// If \a auto_continue is \b true, set the breakpoint to continue when hit.
  void SetAutoContinue(bool auto_continue);

  /// Check the AutoContinue state.
  ///
  /// \return
  ///     \b true if the breakpoint is set to auto-continue, \b false if not.
  bool IsAutoContinue() const;

  /// Return the current Hit Count.
  uint32_t GetHitCount() const { return m_hit_counter.GetValue(); }

  /// Resets the current Hit Count.
  void ResetHitCount() { m_hit_counter.Reset(); }

  /// Return the current Ignore Count.
  ///
  /// \return
  ///     The number of breakpoint hits to be ignored.
  uint32_t GetIgnoreCount() const;

  /// Set the breakpoint to ignore the next \a count breakpoint hits.
  ///
  /// \param[in] n
  ///    The number of breakpoint hits to ignore.
  void SetIgnoreCount(uint32_t n);

  /// Set the callback action invoked when the breakpoint is hit.
  ///
  /// The callback will return a bool indicating whether the target should
  /// stop at this breakpoint or not.
  ///
  /// \param[in] callback
  ///     The method that will get called when the breakpoint is hit.
  ///
  /// \param[in] callback_baton_sp
  ///     A shared pointer to a Baton that provides the void * needed
  ///     for the callback.
  ///
  /// \see lldb_private::Baton
  void SetCallback(BreakpointHitCallback callback,
                   const lldb::BatonSP &callback_baton_sp, bool is_synchronous);

  void SetCallback(BreakpointHitCallback callback, void *baton,
                   bool is_synchronous);

  void ClearCallback();

  /// Set the breakpoint location's condition.
  ///
  /// \param[in] condition
  ///    The condition expression to evaluate when the breakpoint is hit.
  void SetCondition(const char *condition);

  /// Return a pointer to the text of the condition expression.
  ///
  /// \return
  ///    A pointer to the condition expression text, or nullptr if no
  //     condition has been set.
  const char *GetConditionText(size_t *hash = nullptr) const;

  bool ConditionSaysStop(ExecutionContext &exe_ctx, Status &error);

  /// Set the valid thread to be checked when the breakpoint is hit.
  ///
  /// \param[in] thread_id
  ///    If this thread hits the breakpoint, we stop, otherwise not.
  void SetThreadID(lldb::tid_t thread_id);

  lldb::tid_t GetThreadID();

  void SetThreadIndex(uint32_t index);

  uint32_t GetThreadIndex() const;

  void SetThreadName(const char *thread_name);

  const char *GetThreadName() const;

  void SetQueueName(const char *queue_name);

  const char *GetQueueName() const;

  // The next section deals with this location's breakpoint sites.

  /// Try to resolve the breakpoint site for this location.
  ///
  /// \return
  ///     \b true if we were successful at setting a breakpoint site,
  ///     \b false otherwise.
  bool ResolveBreakpointSite();

  /// Clear this breakpoint location's breakpoint site - for instance when
  /// disabling the breakpoint.
  ///
  /// \return
  ///     \b true if there was a breakpoint site to be cleared, \b false
  ///     otherwise.
  bool ClearBreakpointSite();

  /// Return whether this breakpoint location has a breakpoint site. \return
  ///     \b true if there was a breakpoint site for this breakpoint
  ///     location, \b false otherwise.
  bool IsResolved() const;

  lldb::BreakpointSiteSP GetBreakpointSite() const;

  // The next section are generic report functions.

  /// Print a description of this breakpoint location to the stream \a s.
  ///
  /// \param[in] s
  ///     The stream to which to print the description.
  ///
  /// \param[in] level
  ///     The description level that indicates the detail level to
  ///     provide.
  ///
  /// \see lldb::DescriptionLevel
  void GetDescription(Stream *s, lldb::DescriptionLevel level);

  /// Standard "Dump" method.  At present it does nothing.
  void Dump(Stream *s) const;

  /// Use this to set location specific breakpoint options.
  ///
  /// It will create a copy of the containing breakpoint's options if that
  /// hasn't been done already
  ///
  /// \return
  ///    A reference to the breakpoint options.
  BreakpointOptions &GetLocationOptions();

  /// Use this to access breakpoint options from this breakpoint location.
  /// This will return the options that have a setting for the specified
  /// BreakpointOptions kind.
  ///
  /// \param[in] kind
  ///     The particular option you are looking up.
  /// \return
  ///     A pointer to the containing breakpoint's options if this
  ///     location doesn't have its own copy.
  const BreakpointOptions &
  GetOptionsSpecifyingKind(BreakpointOptions::OptionKind kind) const;

  bool ValidForThisThread(Thread &thread);

  /// Invoke the callback action when the breakpoint is hit.
  ///
  /// Meant to be used by the BreakpointLocation class.
  ///
  /// \param[in] context
  ///    Described the breakpoint event.
  ///
  /// \return
  ///     \b true if the target should stop at this breakpoint and \b
  ///     false not.
  bool InvokeCallback(StoppointCallbackContext *context);
  
  /// Report whether the callback for this location is synchronous or not.
  ///
  /// \return
  ///     \b true if the callback is synchronous and \b false if not.
  bool IsCallbackSynchronous();

  /// Returns whether we should resolve Indirect functions in setting the
  /// breakpoint site for this location.
  ///
  /// \return
  ///     \b true if the breakpoint SITE for this location should be set on the
  ///     resolved location for Indirect functions.
  bool ShouldResolveIndirectFunctions() {
    return m_should_resolve_indirect_functions;
  }

  /// Returns whether the address set in the breakpoint site for this location
  /// was found by resolving an indirect symbol.
  ///
  /// \return
  ///     \b true or \b false as given in the description above.
  bool IsIndirect() { return m_is_indirect; }

  void SetIsIndirect(bool is_indirect) { m_is_indirect = is_indirect; }

  /// Returns whether the address set in the breakpoint location was re-routed
  /// to the target of a re-exported symbol.
  ///
  /// \return
  ///     \b true or \b false as given in the description above.
  bool IsReExported() { return m_is_reexported; }

  void SetIsReExported(bool is_reexported) { m_is_reexported = is_reexported; }

  /// Returns whether the two breakpoint locations might represent "equivalent
  /// locations". This is used when modules changed to determine if a Location
  /// in the old module might be the "same as" the input location.
  ///
  /// \param[in] location
  ///    The location to compare against.
  ///
  /// \return
  ///     \b true or \b false as given in the description above.
  bool EquivalentToLocation(BreakpointLocation &location);

  /// Returns the breakpoint location ID.
  lldb::break_id_t GetID() const { return m_loc_id; }

protected:
  friend class BreakpointSite;
  friend class BreakpointLocationList;
  friend class Process;
  friend class StopInfoBreakpoint;

  /// Set the breakpoint site for this location to \a bp_site_sp.
  ///
  /// \param[in] bp_site_sp
  ///      The breakpoint site we are setting for this location.
  ///
  /// \return
  ///     \b true if we were successful at setting the breakpoint site,
  ///     \b false otherwise.
  bool SetBreakpointSite(lldb::BreakpointSiteSP &bp_site_sp);

  void DecrementIgnoreCount();

  /// BreakpointLocation::IgnoreCountShouldStop  can only be called once
  /// per stop.  This method checks first against the loc and then the owner.
  /// It also takes care of decrementing the ignore counters.
  /// If it returns false we should continue, otherwise stop.
  bool IgnoreCountShouldStop();

private:
  void SwapLocation(lldb::BreakpointLocationSP swap_from);

  void BumpHitCount();

  void UndoBumpHitCount();

  /// Updates the thread ID internally.
  ///
  /// This method was created to handle actually mutating the thread ID
  /// internally because SetThreadID broadcasts an event in addition to mutating
  /// state. The constructor calls this instead of SetThreadID to avoid the
  /// broadcast.
  ///
  /// \param[in] thread_id
  ///   The new thread ID.
  void SetThreadIDInternal(lldb::tid_t thread_id);

  // Constructors and Destructors
  //
  // Only the Breakpoint can make breakpoint locations, and it owns them.

  /// Constructor.
  ///
  /// \param[in] owner
  ///     A back pointer to the breakpoint that owns this location.
  ///
  /// \param[in] addr
  ///     The Address defining this location.
  ///
  /// \param[in] tid
  ///     The thread for which this breakpoint location is valid, or
  ///     LLDB_INVALID_THREAD_ID if it is valid for all threads.
  ///
  /// \param[in] hardware
  ///     \b true if a hardware breakpoint is requested.

  BreakpointLocation(lldb::break_id_t bid, Breakpoint &owner,
                     const Address &addr, lldb::tid_t tid, bool hardware,
                     bool check_for_resolver = true);

  // Data members:
  bool m_should_resolve_indirect_functions;
  bool m_is_reexported;
  bool m_is_indirect;
  Address m_address;   ///< The address defining this location.
  Breakpoint &m_owner; ///< The breakpoint that produced this object.
  std::unique_ptr<BreakpointOptions> m_options_up; ///< Breakpoint options
                                                   /// pointer, nullptr if we're
                                                   /// using our breakpoint's
                                                   /// options.
  lldb::BreakpointSiteSP m_bp_site_sp; ///< Our breakpoint site (it may be
                                       ///shared by more than one location.)
  lldb::UserExpressionSP m_user_expression_sp; ///< The compiled expression to
                                               ///use in testing our condition.
  std::mutex m_condition_mutex; ///< Guards parsing and evaluation of the
                                ///condition, which could be evaluated by
                                /// multiple processes.
  size_t m_condition_hash; ///< For testing whether the condition source code
                           ///changed.
  lldb::break_id_t m_loc_id; ///< Breakpoint location ID.
  StoppointHitCounter m_hit_counter; ///< Number of times this breakpoint
                                     /// location has been hit.

  void SetShouldResolveIndirectFunctions(bool do_resolve) {
    m_should_resolve_indirect_functions = do_resolve;
  }

  void SendBreakpointLocationChangedEvent(lldb::BreakpointEventType eventKind);

  BreakpointLocation(const BreakpointLocation &) = delete;
  const BreakpointLocation &operator=(const BreakpointLocation &) = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_BREAKPOINTLOCATION_H
