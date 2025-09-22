//===-- BreakpadRecords.h ------------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_BREAKPAD_BREAKPADRECORDS_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_BREAKPAD_BREAKPADRECORDS_H

#include "lldb/Utility/UUID.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatProviders.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

namespace lldb_private {
namespace breakpad {

class Record {
public:
  enum Kind {
    Module,
    Info,
    File,
    Func,
    Inline,
    InlineOrigin,
    Line,
    Public,
    StackCFI,
    StackWin
  };

  /// Attempt to guess the kind of the record present in the argument without
  /// doing a full parse. The returned kind will always be correct for valid
  /// records, but the full parse can still fail in case of corrupted input.
  static std::optional<Kind> classify(llvm::StringRef Line);

protected:
  Record(Kind K) : TheKind(K) {}

  ~Record() = default;

public:
  Kind getKind() { return TheKind; }

private:
  Kind TheKind;
};

llvm::StringRef toString(Record::Kind K);
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, Record::Kind K) {
  OS << toString(K);
  return OS;
}

class ModuleRecord : public Record {
public:
  static std::optional<ModuleRecord> parse(llvm::StringRef Line);
  ModuleRecord(llvm::Triple::OSType OS, llvm::Triple::ArchType Arch, UUID ID)
      : Record(Module), OS(OS), Arch(Arch), ID(std::move(ID)) {}

  llvm::Triple::OSType OS;
  llvm::Triple::ArchType Arch;
  UUID ID;
};

inline bool operator==(const ModuleRecord &L, const ModuleRecord &R) {
  return L.OS == R.OS && L.Arch == R.Arch && L.ID == R.ID;
}
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const ModuleRecord &R);

class InfoRecord : public Record {
public:
  static std::optional<InfoRecord> parse(llvm::StringRef Line);
  InfoRecord(UUID ID) : Record(Info), ID(std::move(ID)) {}

  UUID ID;
};

inline bool operator==(const InfoRecord &L, const InfoRecord &R) {
  return L.ID == R.ID;
}
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const InfoRecord &R);

class FileRecord : public Record {
public:
  static std::optional<FileRecord> parse(llvm::StringRef Line);
  FileRecord(size_t Number, llvm::StringRef Name)
      : Record(File), Number(Number), Name(Name) {}

  size_t Number;
  llvm::StringRef Name;
};

inline bool operator==(const FileRecord &L, const FileRecord &R) {
  return L.Number == R.Number && L.Name == R.Name;
}
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const FileRecord &R);

class InlineOriginRecord : public Record {
public:
  static std::optional<InlineOriginRecord> parse(llvm::StringRef Line);
  InlineOriginRecord(size_t Number, llvm::StringRef Name)
      : Record(InlineOrigin), Number(Number), Name(Name) {}

  size_t Number;
  llvm::StringRef Name;
};

inline bool operator==(const InlineOriginRecord &L,
                       const InlineOriginRecord &R) {
  return L.Number == R.Number && L.Name == R.Name;
}
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                              const InlineOriginRecord &R);

class FuncRecord : public Record {
public:
  static std::optional<FuncRecord> parse(llvm::StringRef Line);
  FuncRecord(bool Multiple, lldb::addr_t Address, lldb::addr_t Size,
             lldb::addr_t ParamSize, llvm::StringRef Name)
      : Record(Module), Multiple(Multiple), Address(Address), Size(Size),
        ParamSize(ParamSize), Name(Name) {}

  bool Multiple;
  lldb::addr_t Address;
  lldb::addr_t Size;
  lldb::addr_t ParamSize;
  llvm::StringRef Name;
};

bool operator==(const FuncRecord &L, const FuncRecord &R);
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const FuncRecord &R);

class InlineRecord : public Record {
public:
  static std::optional<InlineRecord> parse(llvm::StringRef Line);
  InlineRecord(size_t InlineNestLevel, uint32_t CallSiteLineNum,
               size_t CallSiteFileNum, size_t OriginNum)
      : Record(Inline), InlineNestLevel(InlineNestLevel),
        CallSiteLineNum(CallSiteLineNum), CallSiteFileNum(CallSiteFileNum),
        OriginNum(OriginNum) {}

  size_t InlineNestLevel;
  uint32_t CallSiteLineNum;
  size_t CallSiteFileNum;
  size_t OriginNum;
  // A vector of address range covered by this inline
  std::vector<std::pair<lldb::addr_t, lldb::addr_t>> Ranges;
};

bool operator==(const InlineRecord &L, const InlineRecord &R);
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const InlineRecord &R);

class LineRecord : public Record {
public:
  static std::optional<LineRecord> parse(llvm::StringRef Line);
  LineRecord(lldb::addr_t Address, lldb::addr_t Size, uint32_t LineNum,
             size_t FileNum)
      : Record(Line), Address(Address), Size(Size), LineNum(LineNum),
        FileNum(FileNum) {}

  lldb::addr_t Address;
  lldb::addr_t Size;
  uint32_t LineNum;
  size_t FileNum;
};

bool operator==(const LineRecord &L, const LineRecord &R);
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const LineRecord &R);

class PublicRecord : public Record {
public:
  static std::optional<PublicRecord> parse(llvm::StringRef Line);
  PublicRecord(bool Multiple, lldb::addr_t Address, lldb::addr_t ParamSize,
               llvm::StringRef Name)
      : Record(Module), Multiple(Multiple), Address(Address),
        ParamSize(ParamSize), Name(Name) {}

  bool Multiple;
  lldb::addr_t Address;
  lldb::addr_t ParamSize;
  llvm::StringRef Name;
};

bool operator==(const PublicRecord &L, const PublicRecord &R);
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const PublicRecord &R);

class StackCFIRecord : public Record {
public:
  static std::optional<StackCFIRecord> parse(llvm::StringRef Line);
  StackCFIRecord(lldb::addr_t Address, std::optional<lldb::addr_t> Size,
                 llvm::StringRef UnwindRules)
      : Record(StackCFI), Address(Address), Size(Size),
        UnwindRules(UnwindRules) {}

  lldb::addr_t Address;
  std::optional<lldb::addr_t> Size;
  llvm::StringRef UnwindRules;
};

bool operator==(const StackCFIRecord &L, const StackCFIRecord &R);
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const StackCFIRecord &R);

class StackWinRecord : public Record {
public:
  static std::optional<StackWinRecord> parse(llvm::StringRef Line);

  StackWinRecord(lldb::addr_t RVA, lldb::addr_t CodeSize,
                 lldb::addr_t ParameterSize, lldb::addr_t SavedRegisterSize,
                 lldb::addr_t LocalSize, llvm::StringRef ProgramString)
      : Record(StackWin), RVA(RVA), CodeSize(CodeSize),
        ParameterSize(ParameterSize), SavedRegisterSize(SavedRegisterSize),
        LocalSize(LocalSize), ProgramString(ProgramString) {}

  enum class FrameType : uint8_t { FPO = 0, FrameData = 4 };
  lldb::addr_t RVA;
  lldb::addr_t CodeSize;
  lldb::addr_t ParameterSize;
  lldb::addr_t SavedRegisterSize;
  lldb::addr_t LocalSize;
  llvm::StringRef ProgramString;
};

bool operator==(const StackWinRecord &L, const StackWinRecord &R);
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const StackWinRecord &R);

} // namespace breakpad
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_BREAKPAD_BREAKPADRECORDS_H
