//===-- BreakpointSite.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_BREAKPOINTSITE_H
#define LLDB_BREAKPOINT_BREAKPOINTSITE_H

#include <list>
#include <mutex>


#include "lldb/Breakpoint/BreakpointLocationCollection.h"
#include "lldb/Breakpoint/StoppointSite.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-forward.h"

namespace lldb_private {

/// \class BreakpointSite BreakpointSite.h "lldb/Breakpoint/BreakpointSite.h"
/// Class that manages the actual breakpoint that will be inserted into the
/// running program.
///
/// The BreakpointSite class handles the physical breakpoint that is actually
/// inserted in the target program.  As such, it is also the one that  gets
/// hit, when the program stops. It keeps a list of all BreakpointLocations
/// that share this physical site. When the breakpoint is hit, all the
/// locations are informed by the breakpoint site. Breakpoint sites are owned
/// by the process.

class BreakpointSite : public std::enable_shared_from_this<BreakpointSite>,
                       public StoppointSite {
public:
  enum Type {
    eSoftware, // Breakpoint opcode has been written to memory and
               // m_saved_opcode
               // and m_trap_opcode contain the saved and written opcode.
    eHardware, // Breakpoint site is set as a hardware breakpoint
    eExternal  // Breakpoint site is managed by an external debug nub or
               // debug interface where memory reads transparently will not
               // display any breakpoint opcodes.
  };

  typedef lldb::break_id_t SiteID;
  typedef lldb::break_id_t ConstituentID;

  ~BreakpointSite() override;

  // This section manages the breakpoint traps

  /// Returns the Opcode Bytes for this breakpoint
  uint8_t *GetTrapOpcodeBytes();

  /// Returns the Opcode Bytes for this breakpoint - const version
  const uint8_t *GetTrapOpcodeBytes() const;

  /// Get the size of the trap opcode for this address
  size_t GetTrapOpcodeMaxByteSize() const;

  /// Sets the trap opcode
  bool SetTrapOpcode(const uint8_t *trap_opcode, uint32_t trap_opcode_size);

  /// Gets the original instruction bytes that were overwritten by the trap
  uint8_t *GetSavedOpcodeBytes();

  /// Gets the original instruction bytes that were overwritten by the trap
  /// const version
  const uint8_t *GetSavedOpcodeBytes() const;

  /// Says whether \a addr and size \a size intersects with the address \a
  /// intersect_addr
  bool IntersectsRange(lldb::addr_t addr, size_t size,
                       lldb::addr_t *intersect_addr, size_t *intersect_size,
                       size_t *opcode_offset) const;

  /// Tells whether the current breakpoint site is enabled or not
  ///
  /// This is a low-level enable bit for the breakpoint sites.  If a
  /// breakpoint site has no enabled constituents, it should just get removed.
  /// This enable/disable is for the low-level target code to enable and disable
  /// breakpoint sites when single stepping, etc.
  bool IsEnabled() const;

  /// Sets whether the current breakpoint site is enabled or not
  ///
  /// \param[in] enabled
  ///    \b true if the breakpoint is enabled, \b false otherwise.
  void SetEnabled(bool enabled);

  /// Enquires of the breakpoint locations that produced this breakpoint site
  /// whether we should stop at this location.
  ///
  /// \param[in] context
  ///    This contains the information about this stop.
  ///
  /// \return
  ///    \b true if we should stop, \b false otherwise.
  bool ShouldStop(StoppointCallbackContext *context) override;

  /// Standard Dump method
  void Dump(Stream *s) const override;

  /// The "Constituents" are the breakpoint locations that share this breakpoint
  /// site. The method adds the \a constituent to this breakpoint site's
  /// constituent list.
  ///
  /// \param[in] constituent
  ///    \a constituent is the Breakpoint Location to add.
  void AddConstituent(const lldb::BreakpointLocationSP &constituent);

  /// This method returns the number of breakpoint locations currently located
  /// at this breakpoint site.
  ///
  /// \return
  ///    The number of constituents.
  size_t GetNumberOfConstituents();

  /// This method returns the breakpoint location at index \a index located at
  /// this breakpoint site.  The constituents are listed ordinally from 0 to
  /// GetNumberOfConstituents() - 1 so you can use this method to iterate over
  /// the constituents
  ///
  /// \param[in] idx
  ///     The index in the list of constituents for which you wish the
  ///     constituent location.
  ///
  /// \return
  ///    A shared pointer to the breakpoint location at that index.
  lldb::BreakpointLocationSP GetConstituentAtIndex(size_t idx);

  /// This method copies the breakpoint site's constituents into a new
  /// collection. It does this while the constituents mutex is locked.
  ///
  /// \param[out] out_collection
  ///    The BreakpointLocationCollection into which to put the constituents
  ///    of this breakpoint site.
  ///
  /// \return
  ///    The number of elements copied into out_collection.
  size_t CopyConstituentsList(BreakpointLocationCollection &out_collection);

  /// Check whether the constituents of this breakpoint site have any thread
  /// specifiers, and if yes, is \a thread contained in any of these
  /// specifiers.
  ///
  /// \param[in] thread
  ///     The thread against which to test.
  ///
  /// return
  ///     \b true if the collection contains at least one location that
  ///     would be valid for this thread, false otherwise.
  bool ValidForThisThread(Thread &thread);

  /// Print a description of this breakpoint site to the stream \a s.
  /// GetDescription tells you about the breakpoint site's constituents. Use
  /// BreakpointSite::Dump(Stream *) to get information about the breakpoint
  /// site itself.
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

  /// Tell whether a breakpoint has a location at this site.
  ///
  /// \param[in] bp_id
  ///     The breakpoint id to query.
  ///
  /// \result
  ///     \b true if bp_id has a location that is at this site,
  ///     \b false otherwise.
  bool IsBreakpointAtThisSite(lldb::break_id_t bp_id);

  /// Tell whether ALL the breakpoints in the location collection are
  /// internal.
  ///
  /// \result
  ///     \b true if all breakpoint locations are owned by internal breakpoints,
  ///     \b false otherwise.
  bool IsInternal() const;

  bool IsHardware() const override {
    lldbassert(BreakpointSite::Type::eHardware == GetType() ||
               !HardwareRequired());
    return BreakpointSite::Type::eHardware == GetType();
  }

  BreakpointSite::Type GetType() const { return m_type; }

  void SetType(BreakpointSite::Type type) { m_type = type; }

private:
  friend class Process;
  friend class BreakpointLocation;
  // The StopInfoBreakpoint knows when it is processing a hit for a thread for
  // a site, so let it be the one to manage setting the location hit count once
  // and only once.
  friend class StopInfoBreakpoint;

  void BumpHitCounts();

  /// The method removes the constituent at \a break_loc_id from this breakpoint
  /// list.
  size_t RemoveConstituent(lldb::break_id_t break_id,
                           lldb::break_id_t break_loc_id);

  BreakpointSite::Type m_type; ///< The type of this breakpoint site.
  uint8_t m_saved_opcode[8]; ///< The saved opcode bytes if this breakpoint site
                             ///uses trap opcodes.
  uint8_t m_trap_opcode[8];  ///< The opcode that was used to create the
                             ///breakpoint if it is a software breakpoint site.
  bool
      m_enabled; ///< Boolean indicating if this breakpoint site enabled or not.

  // Consider adding an optimization where if there is only one constituent, we
  // don't store a list.  The usual case will be only one constituent...
  BreakpointLocationCollection
      m_constituents; ///< This has the BreakpointLocations
                      /// that share this breakpoint site.
  std::recursive_mutex m_constituents_mutex; ///< This mutex protects the
                                             ///< constituents collection.

  static lldb::break_id_t GetNextID();

  // Only the Process can create breakpoint sites in
  // Process::CreateBreakpointSite (lldb::BreakpointLocationSP &, bool).
  BreakpointSite(const lldb::BreakpointLocationSP &constituent,
                 lldb::addr_t m_addr, bool use_hardware);

  BreakpointSite(const BreakpointSite &) = delete;
  const BreakpointSite &operator=(const BreakpointSite &) = delete;
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_BREAKPOINTSITE_H
