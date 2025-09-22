//===- DiagnosticNames.h - Defines a table of all builtin diagnostics ------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_DIAGTOOL_DIAGNOSTICNAMES_H
#define LLVM_CLANG_TOOLS_DIAGTOOL_DIAGNOSTICNAMES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"

namespace diagtool {

  struct DiagnosticRecord {
    const char *NameStr;
    short DiagID;
    uint8_t NameLen;

    llvm::StringRef getName() const {
      return llvm::StringRef(NameStr, NameLen);
    }

    bool operator<(const DiagnosticRecord &Other) const {
      return getName() < Other.getName();
    }
  };

  /// Get every diagnostic in the system, sorted by name.
  llvm::ArrayRef<DiagnosticRecord> getBuiltinDiagnosticsByName();

  /// Get a diagnostic by its ID.
  const DiagnosticRecord &getDiagnosticForID(short DiagID);


  struct GroupRecord {
    uint16_t NameOffset;
    uint16_t Members;
    uint16_t SubGroups;

    llvm::StringRef getName() const;

    template<typename RecordType>
    class group_iterator {
      const short *CurrentID;

      friend struct GroupRecord;
      group_iterator(const short *Start) : CurrentID(Start) {
        if (CurrentID && *CurrentID == -1)
          CurrentID = nullptr;
      }

    public:
      typedef RecordType                 value_type;
      typedef const value_type &         reference;
      typedef const value_type *         pointer;
      typedef std::forward_iterator_tag  iterator_category;
      typedef std::ptrdiff_t             difference_type;

      inline reference operator*() const;
      inline pointer operator->() const {
        return &operator*();
      }

      inline short getID() const {
        return *CurrentID;
      }

      group_iterator &operator++() {
        ++CurrentID;
        if (*CurrentID == -1)
          CurrentID = nullptr;
        return *this;
      }

      bool operator==(const group_iterator &Other) const {
        return CurrentID == Other.CurrentID;
      }

      bool operator!=(const group_iterator &Other) const {
        return CurrentID != Other.CurrentID;
      }
    };

    typedef group_iterator<GroupRecord> subgroup_iterator;
    subgroup_iterator subgroup_begin() const;
    subgroup_iterator subgroup_end() const;
    llvm::iterator_range<subgroup_iterator> subgroups() const;

    typedef group_iterator<DiagnosticRecord> diagnostics_iterator;
    diagnostics_iterator diagnostics_begin() const;
    diagnostics_iterator diagnostics_end() const;
    llvm::iterator_range<diagnostics_iterator> diagnostics() const;

    bool operator<(llvm::StringRef Other) const {
      return getName() < Other;
    }
  };

  /// Get every diagnostic group in the system, sorted by name.
  llvm::ArrayRef<GroupRecord> getDiagnosticGroups();

  template<>
  inline GroupRecord::subgroup_iterator::reference
  GroupRecord::subgroup_iterator::operator*() const {
    return getDiagnosticGroups()[*CurrentID];
  }

  template<>
  inline GroupRecord::diagnostics_iterator::reference
  GroupRecord::diagnostics_iterator::operator*() const {
    return getDiagnosticForID(*CurrentID);
  }
} // end namespace diagtool

#endif
