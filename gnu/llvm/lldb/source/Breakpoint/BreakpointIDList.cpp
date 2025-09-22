//===-- BreakpointIDList.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-enumerations.h"
#include "lldb/Breakpoint/BreakpointIDList.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

using namespace lldb;
using namespace lldb_private;

// class BreakpointIDList

BreakpointIDList::BreakpointIDList() : m_breakpoint_ids() {}

BreakpointIDList::~BreakpointIDList() = default;

size_t BreakpointIDList::GetSize() const { return m_breakpoint_ids.size(); }

BreakpointID BreakpointIDList::GetBreakpointIDAtIndex(size_t index) const {
  return ((index < m_breakpoint_ids.size()) ? m_breakpoint_ids[index]
                                            : BreakpointID());
}

bool BreakpointIDList::RemoveBreakpointIDAtIndex(size_t index) {
  if (index >= m_breakpoint_ids.size())
    return false;

  m_breakpoint_ids.erase(m_breakpoint_ids.begin() + index);
  return true;
}

void BreakpointIDList::Clear() { m_breakpoint_ids.clear(); }

bool BreakpointIDList::AddBreakpointID(BreakpointID bp_id) {
  m_breakpoint_ids.push_back(bp_id);

  return true; // We don't do any verification in this function, so always
               // return true.
}

bool BreakpointIDList::Contains(BreakpointID bp_id) const {
  return llvm::is_contained(m_breakpoint_ids, bp_id);
}

//  This function takes OLD_ARGS, which is usually the result of breaking the
//  command string arguments into
//  an array of space-separated strings, and searches through the arguments for
//  any breakpoint ID range specifiers.
//  Any string in the array that is not part of an ID range specifier is copied
//  directly into NEW_ARGS.  If any
//  ID range specifiers are found, the range is interpreted and a list of
//  canonical breakpoint IDs corresponding to
//  all the current breakpoints and locations in the range are added to
//  NEW_ARGS.  When this function is done,
//  NEW_ARGS should be a copy of OLD_ARGS, with and ID range specifiers replaced
//  by the members of the range.

llvm::Error BreakpointIDList::FindAndReplaceIDRanges(
    Args &old_args, Target *target, bool allow_locations,
    BreakpointName::Permissions ::PermissionKinds purpose, Args &new_args) {
  llvm::StringRef range_from;
  llvm::StringRef range_to;
  llvm::StringRef current_arg;
  std::set<std::string> names_found;

  for (size_t i = 0; i < old_args.size(); ++i) {
    bool is_range = false;

    current_arg = old_args[i].ref();
    if (!allow_locations && current_arg.contains('.')) {
      new_args.Clear();
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "Breakpoint locations not allowed, saw location: %s.",
          current_arg.str().c_str());
    }

    Status error;

    std::tie(range_from, range_to) =
        BreakpointIDList::SplitIDRangeExpression(current_arg);
    if (!range_from.empty() && !range_to.empty()) {
      is_range = true;
    } else if (BreakpointID::StringIsBreakpointName(current_arg, error)) {
      if (!error.Success()) {
        new_args.Clear();
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       error.AsCString());
      } else
        names_found.insert(std::string(current_arg));
    } else if ((i + 2 < old_args.size()) &&
               BreakpointID::IsRangeIdentifier(old_args[i + 1].ref()) &&
               BreakpointID::IsValidIDExpression(current_arg) &&
               BreakpointID::IsValidIDExpression(old_args[i + 2].ref())) {
      range_from = current_arg;
      range_to = old_args[i + 2].ref();
      is_range = true;
      i = i + 2;
    } else {
      // See if user has specified id.*
      llvm::StringRef tmp_str = old_args[i].ref();
      auto [prefix, suffix] = tmp_str.split('.');
      if (suffix == "*" && BreakpointID::IsValidIDExpression(prefix)) {

        BreakpointSP breakpoint_sp;
        auto bp_id = BreakpointID::ParseCanonicalReference(prefix);
        if (bp_id)
          breakpoint_sp = target->GetBreakpointByID(bp_id->GetBreakpointID());
        if (!breakpoint_sp) {
          new_args.Clear();
          return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                         "'%d' is not a valid breakpoint ID.\n",
                                         bp_id->GetBreakpointID());
        }
        const size_t num_locations = breakpoint_sp->GetNumLocations();
        for (size_t j = 0; j < num_locations; ++j) {
          BreakpointLocation *bp_loc =
              breakpoint_sp->GetLocationAtIndex(j).get();
          StreamString canonical_id_str;
          BreakpointID::GetCanonicalReference(
              &canonical_id_str, bp_id->GetBreakpointID(), bp_loc->GetID());
          new_args.AppendArgument(canonical_id_str.GetString());
        }
      }
    }

    if (!is_range) {
      new_args.AppendArgument(current_arg);
      continue;
    }

    auto start_bp = BreakpointID::ParseCanonicalReference(range_from);
    auto end_bp = BreakpointID::ParseCanonicalReference(range_to);

    if (!start_bp ||
        !target->GetBreakpointByID(start_bp->GetBreakpointID())) {
      new_args.Clear();
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "'%s' is not a valid breakpoint ID.\n",
                                     range_from.str().c_str());
    }

    if (!end_bp ||
        !target->GetBreakpointByID(end_bp->GetBreakpointID())) {
      new_args.Clear();
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "'%s' is not a valid breakpoint ID.\n",
                                     range_to.str().c_str());
    }
    break_id_t start_bp_id = start_bp->GetBreakpointID();
    break_id_t start_loc_id = start_bp->GetLocationID();
    break_id_t end_bp_id = end_bp->GetBreakpointID();
    break_id_t end_loc_id = end_bp->GetLocationID();
    if (((start_loc_id == LLDB_INVALID_BREAK_ID) &&
            (end_loc_id != LLDB_INVALID_BREAK_ID)) ||
        ((start_loc_id != LLDB_INVALID_BREAK_ID) &&
         (end_loc_id == LLDB_INVALID_BREAK_ID))) {
      new_args.Clear();
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Invalid breakpoint id range:  Either "
                                     "both ends of range must specify"
                                     " a breakpoint location, or neither can "
                                     "specify a breakpoint location.");
    }

    // We have valid range starting & ending breakpoint IDs.  Go through all
    // the breakpoints in the target and find all the breakpoints that fit into
    // this range, and add them to new_args.

    // Next check to see if we have location id's.  If so, make sure the
    // start_bp_id and end_bp_id are for the same breakpoint; otherwise we have
    // an illegal range: breakpoint id ranges that specify bp locations are NOT
    // allowed to cross major bp id numbers.

    if ((start_loc_id != LLDB_INVALID_BREAK_ID) ||
        (end_loc_id != LLDB_INVALID_BREAK_ID)) {
      if (start_bp_id != end_bp_id) {
        new_args.Clear();
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            "Invalid range: Ranges that specify particular breakpoint "
            "locations"
            " must be within the same major breakpoint; you specified two"
            " different major breakpoints, %d and %d.\n",
            start_bp_id, end_bp_id);
      }
    }

    const BreakpointList &breakpoints = target->GetBreakpointList();
    const size_t num_breakpoints = breakpoints.GetSize();
    for (size_t j = 0; j < num_breakpoints; ++j) {
      Breakpoint *breakpoint = breakpoints.GetBreakpointAtIndex(j).get();
      break_id_t cur_bp_id = breakpoint->GetID();

      if ((cur_bp_id < start_bp_id) || (cur_bp_id > end_bp_id))
        continue;

      const size_t num_locations = breakpoint->GetNumLocations();

      if ((cur_bp_id == start_bp_id) &&
          (start_loc_id != LLDB_INVALID_BREAK_ID)) {
        for (size_t k = 0; k < num_locations; ++k) {
          BreakpointLocation *bp_loc = breakpoint->GetLocationAtIndex(k).get();
          if ((bp_loc->GetID() >= start_loc_id) &&
              (bp_loc->GetID() <= end_loc_id)) {
            StreamString canonical_id_str;
            BreakpointID::GetCanonicalReference(&canonical_id_str, cur_bp_id,
                                                bp_loc->GetID());
            new_args.AppendArgument(canonical_id_str.GetString());
          }
        }
      } else if ((cur_bp_id == end_bp_id) &&
                 (end_loc_id != LLDB_INVALID_BREAK_ID)) {
        for (size_t k = 0; k < num_locations; ++k) {
          BreakpointLocation *bp_loc = breakpoint->GetLocationAtIndex(k).get();
          if (bp_loc->GetID() <= end_loc_id) {
            StreamString canonical_id_str;
            BreakpointID::GetCanonicalReference(&canonical_id_str, cur_bp_id,
                                                bp_loc->GetID());
            new_args.AppendArgument(canonical_id_str.GetString());
          }
        }
      } else {
        StreamString canonical_id_str;
        BreakpointID::GetCanonicalReference(&canonical_id_str, cur_bp_id,
                                            LLDB_INVALID_BREAK_ID);
        new_args.AppendArgument(canonical_id_str.GetString());
      }
    }
  }

  // Okay, now see if we found any names, and if we did, add them:
  if (target && !names_found.empty()) {
    Status error;
    // Remove any names that aren't visible for this purpose:
    auto iter = names_found.begin();
    while (iter != names_found.end()) {
      BreakpointName *bp_name = target->FindBreakpointName(ConstString(*iter),
                                                           true,
                                                           error);
      if (bp_name && !bp_name->GetPermission(purpose))
        iter = names_found.erase(iter);
      else
        iter++;
    }
    
    if (!names_found.empty()) {
      for (BreakpointSP bkpt_sp : target->GetBreakpointList().Breakpoints()) {
        for (const std::string &name : names_found) {
          if (bkpt_sp->MatchesName(name.c_str())) {
            StreamString canonical_id_str;
            BreakpointID::GetCanonicalReference(
                &canonical_id_str, bkpt_sp->GetID(), LLDB_INVALID_BREAK_ID);
            new_args.AppendArgument(canonical_id_str.GetString());
          }
        }
      }
    }
  }
  return llvm::Error::success();
}

std::pair<llvm::StringRef, llvm::StringRef>
BreakpointIDList::SplitIDRangeExpression(llvm::StringRef in_string) {
  for (auto specifier_str : BreakpointID::GetRangeSpecifiers()) {
    size_t idx = in_string.find(specifier_str);
    if (idx == llvm::StringRef::npos)
      continue;
    llvm::StringRef right1 = in_string.drop_front(idx);

    llvm::StringRef from = in_string.take_front(idx);
    llvm::StringRef to = right1.drop_front(specifier_str.size());

    if (BreakpointID::IsValidIDExpression(from) &&
        BreakpointID::IsValidIDExpression(to)) {
      return std::make_pair(from, to);
    }
  }

  return std::pair<llvm::StringRef, llvm::StringRef>();
}
