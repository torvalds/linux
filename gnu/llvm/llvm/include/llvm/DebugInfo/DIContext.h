//===- DIContext.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines DIContext, an abstract data structure that holds
// debug information data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DICONTEXT_H
#define LLVM_DEBUGINFO_DICONTEXT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace llvm {

/// A format-neutral container for source line information.
struct DILineInfo {
  // DILineInfo contains "<invalid>" for function/filename it cannot fetch.
  static constexpr const char *const BadString = "<invalid>";
  // Use "??" instead of "<invalid>" to make our output closer to addr2line.
  static constexpr const char *const Addr2LineBadString = "??";
  std::string FileName;
  std::string FunctionName;
  std::string StartFileName;
  // Full source corresponding to `FileName`
  std::optional<StringRef> Source;
  // Source code for this particular line
  // (in case if `Source` is not available)
  std::optional<StringRef> LineSource;
  uint32_t Line = 0;
  uint32_t Column = 0;
  uint32_t StartLine = 0;
  std::optional<uint64_t> StartAddress;

  // DWARF-specific.
  uint32_t Discriminator = 0;

  DILineInfo()
      : FileName(BadString), FunctionName(BadString), StartFileName(BadString) {
  }

  bool operator==(const DILineInfo &RHS) const {
    return Line == RHS.Line && Column == RHS.Column &&
           FileName == RHS.FileName && FunctionName == RHS.FunctionName &&
           StartFileName == RHS.StartFileName && StartLine == RHS.StartLine &&
           Discriminator == RHS.Discriminator;
  }

  bool operator!=(const DILineInfo &RHS) const { return !(*this == RHS); }

  bool operator<(const DILineInfo &RHS) const {
    return std::tie(FileName, FunctionName, StartFileName, Line, Column,
                    StartLine, Discriminator) <
           std::tie(RHS.FileName, RHS.FunctionName, RHS.StartFileName, RHS.Line,
                    RHS.Column, RHS.StartLine, RHS.Discriminator);
  }

  explicit operator bool() const { return *this != DILineInfo(); }

  void dump(raw_ostream &OS) {
    OS << "Line info: ";
    if (FileName != BadString)
      OS << "file '" << FileName << "', ";
    if (FunctionName != BadString)
      OS << "function '" << FunctionName << "', ";
    OS << "line " << Line << ", ";
    OS << "column " << Column << ", ";
    if (StartFileName != BadString)
      OS << "start file '" << StartFileName << "', ";
    OS << "start line " << StartLine << '\n';
  }
};

using DILineInfoTable = SmallVector<std::pair<uint64_t, DILineInfo>, 16>;

/// A format-neutral container for inlined code description.
class DIInliningInfo {
  SmallVector<DILineInfo, 4> Frames;

public:
  DIInliningInfo() = default;

  /// Returns the frame at `Index`. Frames are stored in bottom-up
  /// (leaf-to-root) order with increasing index.
  const DILineInfo &getFrame(unsigned Index) const {
    assert(Index < Frames.size());
    return Frames[Index];
  }

  DILineInfo *getMutableFrame(unsigned Index) {
    assert(Index < Frames.size());
    return &Frames[Index];
  }

  uint32_t getNumberOfFrames() const { return Frames.size(); }

  void addFrame(const DILineInfo &Frame) { Frames.push_back(Frame); }

  void resize(unsigned i) { Frames.resize(i); }
};

/// Container for description of a global variable.
struct DIGlobal {
  std::string Name;
  uint64_t Start = 0;
  uint64_t Size = 0;
  std::string DeclFile;
  uint64_t DeclLine = 0;

  DIGlobal() : Name(DILineInfo::BadString) {}
};

struct DILocal {
  std::string FunctionName;
  std::string Name;
  std::string DeclFile;
  uint64_t DeclLine = 0;
  std::optional<int64_t> FrameOffset;
  std::optional<uint64_t> Size;
  std::optional<uint64_t> TagOffset;
};

/// A DINameKind is passed to name search methods to specify a
/// preference regarding the type of name resolution the caller wants.
enum class DINameKind { None, ShortName, LinkageName };

/// Controls which fields of DILineInfo container should be filled
/// with data.
struct DILineInfoSpecifier {
  enum class FileLineInfoKind {
    None,
    // RawValue is whatever the compiler stored in the filename table.  Could be
    // a full path, could be something else.
    RawValue,
    BaseNameOnly,
    // Relative to the compilation directory.
    RelativeFilePath,
    AbsoluteFilePath
  };
  using FunctionNameKind = DINameKind;

  FileLineInfoKind FLIKind;
  FunctionNameKind FNKind;

  DILineInfoSpecifier(FileLineInfoKind FLIKind = FileLineInfoKind::RawValue,
                      FunctionNameKind FNKind = FunctionNameKind::None)
      : FLIKind(FLIKind), FNKind(FNKind) {}

  inline bool operator==(const DILineInfoSpecifier &RHS) const {
    return FLIKind == RHS.FLIKind && FNKind == RHS.FNKind;
  }
};

/// This is just a helper to programmatically construct DIDumpType.
enum DIDumpTypeCounter {
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME, OPTION)        \
  DIDT_ID_##ENUM_NAME,
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
  DIDT_ID_UUID,
  DIDT_ID_Count
};
static_assert(DIDT_ID_Count <= 32, "section types overflow storage");

/// Selects which debug sections get dumped.
enum DIDumpType : unsigned {
  DIDT_Null,
  DIDT_All = ~0U,
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME, OPTION)        \
  DIDT_##ENUM_NAME = 1U << DIDT_ID_##ENUM_NAME,
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
  DIDT_UUID = 1 << DIDT_ID_UUID,
};

/// Container for dump options that control which debug information will be
/// dumped.
struct DIDumpOptions {
  unsigned DumpType = DIDT_All;
  unsigned ChildRecurseDepth = -1U;
  unsigned ParentRecurseDepth = -1U;
  uint16_t Version = 0; // DWARF version to assume when extracting.
  uint8_t AddrSize = 4; // Address byte size to assume when extracting.
  bool ShowAddresses = true;
  bool ShowChildren = false;
  bool ShowParents = false;
  bool ShowForm = false;
  bool SummarizeTypes = false;
  bool Verbose = false;
  bool DisplayRawContents = false;
  bool IsEH = false;
  bool DumpNonSkeleton = false;
  bool ShowAggregateErrors = false;
  std::string JsonErrSummaryFile;
  std::function<llvm::StringRef(uint64_t DwarfRegNum, bool IsEH)>
      GetNameForDWARFReg;

  /// Return default option set for printing a single DIE without children.
  static DIDumpOptions getForSingleDIE() {
    DIDumpOptions Opts;
    Opts.ChildRecurseDepth = 0;
    Opts.ParentRecurseDepth = 0;
    return Opts;
  }

  /// Return the options with RecurseDepth set to 0 unless explicitly required.
  DIDumpOptions noImplicitRecursion() const {
    DIDumpOptions Opts = *this;
    if (ChildRecurseDepth == -1U && !ShowChildren)
      Opts.ChildRecurseDepth = 0;
    if (ParentRecurseDepth == -1U && !ShowParents)
      Opts.ParentRecurseDepth = 0;
    return Opts;
  }

  std::function<void(Error)> RecoverableErrorHandler =
      WithColor::defaultErrorHandler;
  std::function<void(Error)> WarningHandler = WithColor::defaultWarningHandler;
};

class DIContext {
public:
  enum DIContextKind { CK_DWARF, CK_PDB, CK_BTF };

  DIContext(DIContextKind K) : Kind(K) {}
  virtual ~DIContext() = default;

  DIContextKind getKind() const { return Kind; }

  virtual void dump(raw_ostream &OS, DIDumpOptions DumpOpts) = 0;

  virtual bool verify(raw_ostream &OS, DIDumpOptions DumpOpts = {}) {
    // No verifier? Just say things went well.
    return true;
  }

  virtual DILineInfo getLineInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;
  virtual DILineInfo
  getLineInfoForDataAddress(object::SectionedAddress Address) = 0;
  virtual DILineInfoTable getLineInfoForAddressRange(
      object::SectionedAddress Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;
  virtual DIInliningInfo getInliningInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;

  virtual std::vector<DILocal>
  getLocalsForAddress(object::SectionedAddress Address) = 0;

private:
  const DIContextKind Kind;
};

/// An inferface for inquiring the load address of a loaded object file
/// to be used by the DIContext implementations when applying relocations
/// on the fly.
class LoadedObjectInfo {
protected:
  LoadedObjectInfo() = default;
  LoadedObjectInfo(const LoadedObjectInfo &) = default;

public:
  virtual ~LoadedObjectInfo() = default;

  /// Obtain the Load Address of a section by SectionRef.
  ///
  /// Calculate the address of the given section.
  /// The section need not be present in the local address space. The addresses
  /// need to be consistent with the addresses used to query the DIContext and
  /// the output of this function should be deterministic, i.e. repeated calls
  /// with the same Sec should give the same address.
  virtual uint64_t getSectionLoadAddress(const object::SectionRef &Sec) const {
    return 0;
  }

  /// If conveniently available, return the content of the given Section.
  ///
  /// When the section is available in the local address space, in relocated
  /// (loaded) form, e.g. because it was relocated by a JIT for execution, this
  /// function should provide the contents of said section in `Data`. If the
  /// loaded section is not available, or the cost of retrieving it would be
  /// prohibitive, this function should return false. In that case, relocations
  /// will be read from the local (unrelocated) object file and applied on the
  /// fly. Note that this method is used purely for optimzation purposes in the
  /// common case of JITting in the local address space, so returning false
  /// should always be correct.
  virtual bool getLoadedSectionContents(const object::SectionRef &Sec,
                                        StringRef &Data) const {
    return false;
  }

  // FIXME: This is untested and unused anywhere in the LLVM project, it's
  // used/needed by Julia (an external project). It should have some coverage
  // (at least tests, but ideally example functionality).
  /// Obtain a copy of this LoadedObjectInfo.
  virtual std::unique_ptr<LoadedObjectInfo> clone() const = 0;
};

template <typename Derived, typename Base = LoadedObjectInfo>
struct LoadedObjectInfoHelper : Base {
protected:
  LoadedObjectInfoHelper(const LoadedObjectInfoHelper &) = default;
  LoadedObjectInfoHelper() = default;

public:
  template <typename... Ts>
  LoadedObjectInfoHelper(Ts &&...Args) : Base(std::forward<Ts>(Args)...) {}

  std::unique_ptr<llvm::LoadedObjectInfo> clone() const override {
    return std::make_unique<Derived>(static_cast<const Derived &>(*this));
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DICONTEXT_H
