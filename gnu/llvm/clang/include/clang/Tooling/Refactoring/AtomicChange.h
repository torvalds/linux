//===--- AtomicChange.h - AtomicChange class --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines AtomicChange which is used to create a set of source
//  changes, e.g. replacements and header insertions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_ATOMICCHANGE_H
#define LLVM_CLANG_TOOLING_REFACTORING_ATOMICCHANGE_H

#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/ADT/Any.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace clang {
namespace tooling {

/// An atomic change is used to create and group a set of source edits,
/// e.g. replacements or header insertions. Edits in an AtomicChange should be
/// related, e.g. replacements for the same type reference and the corresponding
/// header insertion/deletion.
///
/// An AtomicChange is uniquely identified by a key and will either be fully
/// applied or not applied at all.
///
/// Calling setError on an AtomicChange stores the error message and marks it as
/// bad, i.e. none of its source edits will be applied.
class AtomicChange {
public:
  /// Creates an atomic change around \p KeyPosition with the key being a
  /// concatenation of the file name and the offset of \p KeyPosition.
  /// \p KeyPosition should be the location of the key syntactical element that
  /// is being changed, e.g. the call to a refactored method.
  AtomicChange(const SourceManager &SM, SourceLocation KeyPosition);

  AtomicChange(const SourceManager &SM, SourceLocation KeyPosition,
               llvm::Any Metadata);

  /// Creates an atomic change for \p FilePath with a customized key.
  AtomicChange(llvm::StringRef FilePath, llvm::StringRef Key)
      : Key(Key), FilePath(FilePath) {}

  AtomicChange(AtomicChange &&) = default;
  AtomicChange(const AtomicChange &) = default;

  AtomicChange &operator=(AtomicChange &&) = default;
  AtomicChange &operator=(const AtomicChange &) = default;

  bool operator==(const AtomicChange &Other) const;

  /// Returns the atomic change as a YAML string.
  std::string toYAMLString();

  /// Converts a YAML-encoded automic change to AtomicChange.
  static AtomicChange convertFromYAML(llvm::StringRef YAMLContent);

  /// Returns the key of this change, which is a concatenation of the
  /// file name and offset of the key position.
  const std::string &getKey() const { return Key; }

  /// Returns the path of the file containing this atomic change.
  const std::string &getFilePath() const { return FilePath; }

  /// If this change could not be created successfully, e.g. because of
  /// conflicts among replacements, use this to set an error description.
  /// Thereby, places that cannot be fixed automatically can be gathered when
  /// applying changes.
  void setError(llvm::StringRef Error) { this->Error = std::string(Error); }

  /// Returns whether an error has been set on this list.
  bool hasError() const { return !Error.empty(); }

  /// Returns the error message or an empty string if it does not exist.
  const std::string &getError() const { return Error; }

  /// Adds a replacement that replaces the given Range with
  /// ReplacementText.
  /// \returns An llvm::Error carrying ReplacementError on error.
  llvm::Error replace(const SourceManager &SM, const CharSourceRange &Range,
                      llvm::StringRef ReplacementText);

  /// Adds a replacement that replaces range [Loc, Loc+Length) with
  /// \p Text.
  /// \returns An llvm::Error carrying ReplacementError on error.
  llvm::Error replace(const SourceManager &SM, SourceLocation Loc,
                      unsigned Length, llvm::StringRef Text);

  /// Adds a replacement that inserts \p Text at \p Loc. If this
  /// insertion conflicts with an existing insertion (at the same position),
  /// this will be inserted before/after the existing insertion depending on
  /// \p InsertAfter. Users should use `replace` with `Length=0` instead if they
  /// do not want conflict resolving by default. If the conflicting replacement
  /// is not an insertion, an error is returned.
  ///
  /// \returns An llvm::Error carrying ReplacementError on error.
  llvm::Error insert(const SourceManager &SM, SourceLocation Loc,
                     llvm::StringRef Text, bool InsertAfter = true);

  /// Adds a header into the file that contains the key position.
  /// Header can be in angle brackets or double quotation marks. By default
  /// (header is not quoted), header will be surrounded with double quotes.
  void addHeader(llvm::StringRef Header);

  /// Removes a header from the file that contains the key position.
  void removeHeader(llvm::StringRef Header);

  /// Returns a const reference to existing replacements.
  const Replacements &getReplacements() const { return Replaces; }

  Replacements &getReplacements() { return Replaces; }

  llvm::ArrayRef<std::string> getInsertedHeaders() const {
    return InsertedHeaders;
  }

  llvm::ArrayRef<std::string> getRemovedHeaders() const {
    return RemovedHeaders;
  }

  const llvm::Any &getMetadata() const { return Metadata; }

private:
  AtomicChange() {}

  AtomicChange(std::string Key, std::string FilePath, std::string Error,
               std::vector<std::string> InsertedHeaders,
               std::vector<std::string> RemovedHeaders,
               clang::tooling::Replacements Replaces);

  // This uniquely identifies an AtomicChange.
  std::string Key;
  std::string FilePath;
  std::string Error;
  std::vector<std::string> InsertedHeaders;
  std::vector<std::string> RemovedHeaders;
  tooling::Replacements Replaces;

  // This field stores metadata which is ignored for the purposes of applying
  // edits to source, but may be useful for other consumers of AtomicChanges. In
  // particular, consumers can use this to direct how they want to consume each
  // edit.
  llvm::Any Metadata;
};

using AtomicChanges = std::vector<AtomicChange>;

// Defines specs for applying changes.
struct ApplyChangesSpec {
  // If true, cleans up redundant/erroneous code around changed code with
  // clang-format's cleanup functionality, e.g. redundant commas around deleted
  // parameter or empty namespaces introduced by deletions.
  bool Cleanup = true;

  format::FormatStyle Style = format::getNoStyle();

  // Options for selectively formatting changes with clang-format:
  // kAll: Format all changed lines.
  // kNone: Don't format anything.
  // kViolations: Format lines exceeding the `ColumnLimit` in `Style`.
  enum FormatOption { kAll, kNone, kViolations };

  FormatOption Format = kNone;
};

/// Applies all AtomicChanges in \p Changes to the \p Code.
///
/// This completely ignores the file path in each change and replaces them with
/// \p FilePath, i.e. callers are responsible for ensuring all changes are for
/// the same file.
///
/// \returns The changed code if all changes are applied successfully;
/// otherwise, an llvm::Error carrying llvm::StringError is returned (the Error
/// message can be converted to string with `llvm::toString()` and the
/// error_code should be ignored).
llvm::Expected<std::string>
applyAtomicChanges(llvm::StringRef FilePath, llvm::StringRef Code,
                   llvm::ArrayRef<AtomicChange> Changes,
                   const ApplyChangesSpec &Spec);

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_ATOMICCHANGE_H
