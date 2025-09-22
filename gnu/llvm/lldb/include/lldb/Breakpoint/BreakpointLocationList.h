//===-- BreakpointLocationList.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_BREAKPOINTLOCATIONLIST_H
#define LLDB_BREAKPOINT_BREAKPOINTLOCATIONLIST_H

#include <map>
#include <mutex>
#include <vector>

#include "lldb/Core/Address.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// \class BreakpointLocationList BreakpointLocationList.h
/// "lldb/Breakpoint/BreakpointLocationList.h" This class is used by
/// Breakpoint to manage a list of breakpoint locations, each breakpoint
/// location in the list has a unique ID, and is unique by Address as well.
class BreakpointLocationList {
  // Only Breakpoints can make the location list, or add elements to it. This
  // is not just some random collection of locations.  Rather, the act of
  // adding the location to this list sets its ID, and implicitly all the
  // locations have the same breakpoint ID as well.  If you need a generic
  // container for breakpoint locations, use BreakpointLocationCollection.
  friend class Breakpoint;

public:
  virtual ~BreakpointLocationList();

  /// Standard "Dump" method.  At present it does nothing.
  void Dump(Stream *s) const;

  /// Returns a shared pointer to the breakpoint location at address \a addr -
  /// const version.
  ///
  /// \param[in] addr
  ///     The address to look for.
  ///
  /// \result
  ///     A shared pointer to the breakpoint. May contain a nullptr
  ///     pointer if the breakpoint doesn't exist.
  const lldb::BreakpointLocationSP FindByAddress(const Address &addr) const;

  /// Returns a shared pointer to the breakpoint location with id \a breakID,
  /// const version.
  ///
  /// \param[in] breakID
  ///     The breakpoint location ID to seek for.
  ///
  /// \result
  ///     A shared pointer to the breakpoint. May contain a nullptr
  ///     pointer if the breakpoint doesn't exist.
  lldb::BreakpointLocationSP FindByID(lldb::break_id_t breakID) const;

  /// Returns the breakpoint location id to the breakpoint location at address
  /// \a addr.
  ///
  /// \param[in] addr
  ///     The address to match.
  ///
  /// \result
  ///     The ID of the breakpoint location, or LLDB_INVALID_BREAK_ID.
  lldb::break_id_t FindIDByAddress(const Address &addr);

  /// Returns a breakpoint location list of the breakpoint locations in the
  /// module \a module.  This list is allocated, and owned by the caller.
  ///
  /// \param[in] module
  ///     The module to seek in.
  ///
  /// \param[in] bp_loc_list
  ///     A breakpoint collection that gets any breakpoint locations
  ///     that match \a module appended to.
  ///
  /// \result
  ///     The number of matches
  size_t FindInModule(Module *module,
                      BreakpointLocationCollection &bp_loc_list);

  /// Returns a shared pointer to the breakpoint location with index \a i.
  ///
  /// \param[in] i
  ///     The breakpoint location index to seek for.
  ///
  /// \result
  ///     A shared pointer to the breakpoint. May contain a nullptr
  ///     pointer if the breakpoint doesn't exist.
  lldb::BreakpointLocationSP GetByIndex(size_t i);

  /// Returns a shared pointer to the breakpoint location with index \a i,
  /// const version.
  ///
  /// \param[in] i
  ///     The breakpoint location index to seek for.
  ///
  /// \result
  ///     A shared pointer to the breakpoint. May contain a nullptr
  ///     pointer if the breakpoint doesn't exist.
  const lldb::BreakpointLocationSP GetByIndex(size_t i) const;

  /// Removes all the locations in this list from their breakpoint site owners
  /// list.
  void ClearAllBreakpointSites();

  /// Tells all the breakpoint locations in this list to attempt to resolve
  /// any possible breakpoint sites.
  void ResolveAllBreakpointSites();

  /// Returns the number of breakpoint locations in this list with resolved
  /// breakpoints.
  ///
  /// \result
  ///     Number of qualifying breakpoint locations.
  size_t GetNumResolvedLocations() const;

  /// Returns the number hit count of all locations in this list.
  ///
  /// \result
  ///     Hit count of all locations in this list.
  uint32_t GetHitCount() const;

  /// Resets the hit count of all locations in this list.
  void ResetHitCount();

  /// Enquires of the breakpoint location in this list with ID \a breakID
  /// whether we should stop.
  ///
  /// \param[in] context
  ///     This contains the information about this stop.
  ///
  /// \param[in] breakID
  ///     This break ID that we hit.
  ///
  /// \return
  ///     \b true if we should stop, \b false otherwise.
  bool ShouldStop(StoppointCallbackContext *context, lldb::break_id_t breakID);

  /// Returns the number of elements in this breakpoint location list.
  ///
  /// \result
  ///     The number of elements.
  size_t GetSize() const { return m_locations.size(); }

  /// Print a description of the breakpoint locations in this list to the
  /// stream \a s.
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

protected:
  /// This is the standard constructor.
  ///
  /// It creates an empty breakpoint location list. It is protected here
  /// because only Breakpoints are allowed to create the breakpoint location
  /// list.
  BreakpointLocationList(Breakpoint &owner);

  lldb::BreakpointLocationSP Create(const Address &addr,
                                    bool resolve_indirect_symbols);

  void StartRecordingNewLocations(BreakpointLocationCollection &new_locations);

  void StopRecordingNewLocations();

  lldb::BreakpointLocationSP AddLocation(const Address &addr,
                                         bool resolve_indirect_symbols,
                                         bool *new_location = nullptr);

  void SwapLocation(lldb::BreakpointLocationSP to_location_sp,
                    lldb::BreakpointLocationSP from_location_sp);

  bool RemoveLocation(const lldb::BreakpointLocationSP &bp_loc_sp);

  void RemoveLocationByIndex(size_t idx);

  void RemoveInvalidLocations(const ArchSpec &arch);

  void Compact();

  typedef std::vector<lldb::BreakpointLocationSP> collection;
  typedef std::map<lldb_private::Address, lldb::BreakpointLocationSP,
                   Address::ModulePointerAndOffsetLessThanFunctionObject>
      addr_map;

  Breakpoint &m_owner;
  collection m_locations; // Vector of locations, sorted by ID
  addr_map m_address_to_location;
  mutable std::recursive_mutex m_mutex;
  lldb::break_id_t m_next_id;
  BreakpointLocationCollection *m_new_location_recorder;

public:
  typedef AdaptedIterable<collection, lldb::BreakpointLocationSP,
                          vector_adapter>
      BreakpointLocationIterable;

  BreakpointLocationIterable BreakpointLocations() {
    return BreakpointLocationIterable(m_locations);
  }
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_BREAKPOINTLOCATIONLIST_H
