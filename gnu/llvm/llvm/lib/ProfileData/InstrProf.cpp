//===- InstrProf.cpp - Instrumented profiling format support --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for clang's instrumentation based PGO and
// coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "instrprof"

static cl::opt<bool> StaticFuncFullModulePrefix(
    "static-func-full-module-prefix", cl::init(true), cl::Hidden,
    cl::desc("Use full module build paths in the profile counter names for "
             "static functions."));

// This option is tailored to users that have different top-level directory in
// profile-gen and profile-use compilation. Users need to specific the number
// of levels to strip. A value larger than the number of directories in the
// source file will strip all the directory names and only leave the basename.
//
// Note current ThinLTO module importing for the indirect-calls assumes
// the source directory name not being stripped. A non-zero option value here
// can potentially prevent some inter-module indirect-call-promotions.
static cl::opt<unsigned> StaticFuncStripDirNamePrefix(
    "static-func-strip-dirname-prefix", cl::init(0), cl::Hidden,
    cl::desc("Strip specified level of directory name from source path in "
             "the profile counter name for static functions."));

static std::string getInstrProfErrString(instrprof_error Err,
                                         const std::string &ErrMsg = "") {
  std::string Msg;
  raw_string_ostream OS(Msg);

  switch (Err) {
  case instrprof_error::success:
    OS << "success";
    break;
  case instrprof_error::eof:
    OS << "end of File";
    break;
  case instrprof_error::unrecognized_format:
    OS << "unrecognized instrumentation profile encoding format";
    break;
  case instrprof_error::bad_magic:
    OS << "invalid instrumentation profile data (bad magic)";
    break;
  case instrprof_error::bad_header:
    OS << "invalid instrumentation profile data (file header is corrupt)";
    break;
  case instrprof_error::unsupported_version:
    OS << "unsupported instrumentation profile format version";
    break;
  case instrprof_error::unsupported_hash_type:
    OS << "unsupported instrumentation profile hash type";
    break;
  case instrprof_error::too_large:
    OS << "too much profile data";
    break;
  case instrprof_error::truncated:
    OS << "truncated profile data";
    break;
  case instrprof_error::malformed:
    OS << "malformed instrumentation profile data";
    break;
  case instrprof_error::missing_correlation_info:
    OS << "debug info/binary for correlation is required";
    break;
  case instrprof_error::unexpected_correlation_info:
    OS << "debug info/binary for correlation is not necessary";
    break;
  case instrprof_error::unable_to_correlate_profile:
    OS << "unable to correlate profile";
    break;
  case instrprof_error::invalid_prof:
    OS << "invalid profile created. Please file a bug "
          "at: " BUG_REPORT_URL
          " and include the profraw files that caused this error.";
    break;
  case instrprof_error::unknown_function:
    OS << "no profile data available for function";
    break;
  case instrprof_error::hash_mismatch:
    OS << "function control flow change detected (hash mismatch)";
    break;
  case instrprof_error::count_mismatch:
    OS << "function basic block count change detected (counter mismatch)";
    break;
  case instrprof_error::bitmap_mismatch:
    OS << "function bitmap size change detected (bitmap size mismatch)";
    break;
  case instrprof_error::counter_overflow:
    OS << "counter overflow";
    break;
  case instrprof_error::value_site_count_mismatch:
    OS << "function value site count change detected (counter mismatch)";
    break;
  case instrprof_error::compress_failed:
    OS << "failed to compress data (zlib)";
    break;
  case instrprof_error::uncompress_failed:
    OS << "failed to uncompress data (zlib)";
    break;
  case instrprof_error::empty_raw_profile:
    OS << "empty raw profile file";
    break;
  case instrprof_error::zlib_unavailable:
    OS << "profile uses zlib compression but the profile reader was built "
          "without zlib support";
    break;
  case instrprof_error::raw_profile_version_mismatch:
    OS << "raw profile version mismatch";
    break;
  case instrprof_error::counter_value_too_large:
    OS << "excessively large counter value suggests corrupted profile data";
    break;
  }

  // If optional error message is not empty, append it to the message.
  if (!ErrMsg.empty())
    OS << ": " << ErrMsg;

  return OS.str();
}

namespace {

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class InstrProfErrorCategoryType : public std::error_category {
  const char *name() const noexcept override { return "llvm.instrprof"; }

  std::string message(int IE) const override {
    return getInstrProfErrString(static_cast<instrprof_error>(IE));
  }
};

} // end anonymous namespace

const std::error_category &llvm::instrprof_category() {
  static InstrProfErrorCategoryType ErrorCategory;
  return ErrorCategory;
}

namespace {

const char *InstrProfSectNameCommon[] = {
#define INSTR_PROF_SECT_ENTRY(Kind, SectNameCommon, SectNameCoff, Prefix)      \
  SectNameCommon,
#include "llvm/ProfileData/InstrProfData.inc"
};

const char *InstrProfSectNameCoff[] = {
#define INSTR_PROF_SECT_ENTRY(Kind, SectNameCommon, SectNameCoff, Prefix)      \
  SectNameCoff,
#include "llvm/ProfileData/InstrProfData.inc"
};

const char *InstrProfSectNamePrefix[] = {
#define INSTR_PROF_SECT_ENTRY(Kind, SectNameCommon, SectNameCoff, Prefix)      \
  Prefix,
#include "llvm/ProfileData/InstrProfData.inc"
};

} // namespace

namespace llvm {

cl::opt<bool> DoInstrProfNameCompression(
    "enable-name-compression",
    cl::desc("Enable name/filename string compression"), cl::init(true));

cl::opt<bool> EnableVTableValueProfiling(
    "enable-vtable-value-profiling", cl::init(false),
    cl::desc("If true, the virtual table address will be instrumented to know "
             "the types of a C++ pointer. The information is used in indirect "
             "call promotion to do selective vtable-based comparison."));

cl::opt<bool> EnableVTableProfileUse(
    "enable-vtable-profile-use", cl::init(false),
    cl::desc("If ThinLTO and WPD is enabled and this option is true, vtable "
             "profiles will be used by ICP pass for more efficient indirect "
             "call sequence. If false, type profiles won't be used."));

std::string getInstrProfSectionName(InstrProfSectKind IPSK,
                                    Triple::ObjectFormatType OF,
                                    bool AddSegmentInfo) {
  std::string SectName;

  if (OF == Triple::MachO && AddSegmentInfo)
    SectName = InstrProfSectNamePrefix[IPSK];

  if (OF == Triple::COFF)
    SectName += InstrProfSectNameCoff[IPSK];
  else
    SectName += InstrProfSectNameCommon[IPSK];

  if (OF == Triple::MachO && IPSK == IPSK_data && AddSegmentInfo)
    SectName += ",regular,live_support";

  return SectName;
}

std::string InstrProfError::message() const {
  return getInstrProfErrString(Err, Msg);
}

char InstrProfError::ID = 0;

std::string getPGOFuncName(StringRef Name, GlobalValue::LinkageTypes Linkage,
                           StringRef FileName,
                           uint64_t Version LLVM_ATTRIBUTE_UNUSED) {
  // Value names may be prefixed with a binary '1' to indicate
  // that the backend should not modify the symbols due to any platform
  // naming convention. Do not include that '1' in the PGO profile name.
  if (Name[0] == '\1')
    Name = Name.substr(1);

  std::string NewName = std::string(Name);
  if (llvm::GlobalValue::isLocalLinkage(Linkage)) {
    // For local symbols, prepend the main file name to distinguish them.
    // Do not include the full path in the file name since there's no guarantee
    // that it will stay the same, e.g., if the files are checked out from
    // version control in different locations.
    if (FileName.empty())
      NewName = NewName.insert(0, "<unknown>:");
    else
      NewName = NewName.insert(0, FileName.str() + ":");
  }
  return NewName;
}

// Strip NumPrefix level of directory name from PathNameStr. If the number of
// directory separators is less than NumPrefix, strip all the directories and
// leave base file name only.
static StringRef stripDirPrefix(StringRef PathNameStr, uint32_t NumPrefix) {
  uint32_t Count = NumPrefix;
  uint32_t Pos = 0, LastPos = 0;
  for (const auto &CI : PathNameStr) {
    ++Pos;
    if (llvm::sys::path::is_separator(CI)) {
      LastPos = Pos;
      --Count;
    }
    if (Count == 0)
      break;
  }
  return PathNameStr.substr(LastPos);
}

static StringRef getStrippedSourceFileName(const GlobalObject &GO) {
  StringRef FileName(GO.getParent()->getSourceFileName());
  uint32_t StripLevel = StaticFuncFullModulePrefix ? 0 : (uint32_t)-1;
  if (StripLevel < StaticFuncStripDirNamePrefix)
    StripLevel = StaticFuncStripDirNamePrefix;
  if (StripLevel)
    FileName = stripDirPrefix(FileName, StripLevel);
  return FileName;
}

// The PGO name has the format [<filepath>;]<mangled-name> where <filepath>; is
// provided if linkage is local and is used to discriminate possibly identical
// mangled names. ";" is used because it is unlikely to be found in either
// <filepath> or <mangled-name>.
//
// Older compilers used getPGOFuncName() which has the format
// [<filepath>:]<mangled-name>. This caused trouble for Objective-C functions
// which commonly have :'s in their names. We still need to compute this name to
// lookup functions from profiles built by older compilers.
static std::string
getIRPGONameForGlobalObject(const GlobalObject &GO,
                            GlobalValue::LinkageTypes Linkage,
                            StringRef FileName) {
  return GlobalValue::getGlobalIdentifier(GO.getName(), Linkage, FileName);
}

static std::optional<std::string> lookupPGONameFromMetadata(MDNode *MD) {
  if (MD != nullptr) {
    StringRef S = cast<MDString>(MD->getOperand(0))->getString();
    return S.str();
  }
  return {};
}

// Returns the PGO object name. This function has some special handling
// when called in LTO optimization. The following only applies when calling in
// LTO passes (when \c InLTO is true): LTO's internalization privatizes many
// global linkage symbols. This happens after value profile annotation, but
// those internal linkage functions should not have a source prefix.
// Additionally, for ThinLTO mode, exported internal functions are promoted
// and renamed. We need to ensure that the original internal PGO name is
// used when computing the GUID that is compared against the profiled GUIDs.
// To differentiate compiler generated internal symbols from original ones,
// PGOFuncName meta data are created and attached to the original internal
// symbols in the value profile annotation step
// (PGOUseFunc::annotateIndirectCallSites). If a symbol does not have the meta
// data, its original linkage must be non-internal.
static std::string getIRPGOObjectName(const GlobalObject &GO, bool InLTO,
                                      MDNode *PGONameMetadata) {
  if (!InLTO) {
    auto FileName = getStrippedSourceFileName(GO);
    return getIRPGONameForGlobalObject(GO, GO.getLinkage(), FileName);
  }

  // In LTO mode (when InLTO is true), first check if there is a meta data.
  if (auto IRPGOFuncName = lookupPGONameFromMetadata(PGONameMetadata))
    return *IRPGOFuncName;

  // If there is no meta data, the function must be a global before the value
  // profile annotation pass. Its current linkage may be internal if it is
  // internalized in LTO mode.
  return getIRPGONameForGlobalObject(GO, GlobalValue::ExternalLinkage, "");
}

// Returns the IRPGO function name and does special handling when called
// in LTO optimization. See the comments of `getIRPGOObjectName` for details.
std::string getIRPGOFuncName(const Function &F, bool InLTO) {
  return getIRPGOObjectName(F, InLTO, getPGOFuncNameMetadata(F));
}

// Please use getIRPGOFuncName for LLVM IR instrumentation. This function is
// for front-end (Clang, etc) instrumentation.
// The implementation is kept for profile matching from older profiles.
// This is similar to `getIRPGOFuncName` except that this function calls
// 'getPGOFuncName' to get a name and `getIRPGOFuncName` calls
// 'getIRPGONameForGlobalObject'. See the difference between two callees in the
// comments of `getIRPGONameForGlobalObject`.
std::string getPGOFuncName(const Function &F, bool InLTO, uint64_t Version) {
  if (!InLTO) {
    auto FileName = getStrippedSourceFileName(F);
    return getPGOFuncName(F.getName(), F.getLinkage(), FileName, Version);
  }

  // In LTO mode (when InLTO is true), first check if there is a meta data.
  if (auto PGOFuncName = lookupPGONameFromMetadata(getPGOFuncNameMetadata(F)))
    return *PGOFuncName;

  // If there is no meta data, the function must be a global before the value
  // profile annotation pass. Its current linkage may be internal if it is
  // internalized in LTO mode.
  return getPGOFuncName(F.getName(), GlobalValue::ExternalLinkage, "");
}

std::string getPGOName(const GlobalVariable &V, bool InLTO) {
  // PGONameMetadata should be set by compiler at profile use time
  // and read by symtab creation to look up symbols corresponding to
  // a MD5 hash.
  return getIRPGOObjectName(V, InLTO, V.getMetadata(getPGONameMetadataName()));
}

// See getIRPGOObjectName() for a discription of the format.
std::pair<StringRef, StringRef> getParsedIRPGOName(StringRef IRPGOName) {
  auto [FileName, MangledName] = IRPGOName.split(GlobalIdentifierDelimiter);
  if (MangledName.empty())
    return std::make_pair(StringRef(), IRPGOName);
  return std::make_pair(FileName, MangledName);
}

StringRef getFuncNameWithoutPrefix(StringRef PGOFuncName, StringRef FileName) {
  if (FileName.empty())
    return PGOFuncName;
  // Drop the file name including ':' or ';'. See getIRPGONameForGlobalObject as
  // well.
  if (PGOFuncName.starts_with(FileName))
    PGOFuncName = PGOFuncName.drop_front(FileName.size() + 1);
  return PGOFuncName;
}

// \p FuncName is the string used as profile lookup key for the function. A
// symbol is created to hold the name. Return the legalized symbol name.
std::string getPGOFuncNameVarName(StringRef FuncName,
                                  GlobalValue::LinkageTypes Linkage) {
  std::string VarName = std::string(getInstrProfNameVarPrefix());
  VarName += FuncName;

  if (!GlobalValue::isLocalLinkage(Linkage))
    return VarName;

  // Now fix up illegal chars in local VarName that may upset the assembler.
  const char InvalidChars[] = "-:;<>/\"'";
  size_t FoundPos = VarName.find_first_of(InvalidChars);
  while (FoundPos != std::string::npos) {
    VarName[FoundPos] = '_';
    FoundPos = VarName.find_first_of(InvalidChars, FoundPos + 1);
  }
  return VarName;
}

GlobalVariable *createPGOFuncNameVar(Module &M,
                                     GlobalValue::LinkageTypes Linkage,
                                     StringRef PGOFuncName) {
  // We generally want to match the function's linkage, but available_externally
  // and extern_weak both have the wrong semantics, and anything that doesn't
  // need to link across compilation units doesn't need to be visible at all.
  if (Linkage == GlobalValue::ExternalWeakLinkage)
    Linkage = GlobalValue::LinkOnceAnyLinkage;
  else if (Linkage == GlobalValue::AvailableExternallyLinkage)
    Linkage = GlobalValue::LinkOnceODRLinkage;
  else if (Linkage == GlobalValue::InternalLinkage ||
           Linkage == GlobalValue::ExternalLinkage)
    Linkage = GlobalValue::PrivateLinkage;

  auto *Value =
      ConstantDataArray::getString(M.getContext(), PGOFuncName, false);
  auto *FuncNameVar =
      new GlobalVariable(M, Value->getType(), true, Linkage, Value,
                         getPGOFuncNameVarName(PGOFuncName, Linkage));

  // Hide the symbol so that we correctly get a copy for each executable.
  if (!GlobalValue::isLocalLinkage(FuncNameVar->getLinkage()))
    FuncNameVar->setVisibility(GlobalValue::HiddenVisibility);

  return FuncNameVar;
}

GlobalVariable *createPGOFuncNameVar(Function &F, StringRef PGOFuncName) {
  return createPGOFuncNameVar(*F.getParent(), F.getLinkage(), PGOFuncName);
}

Error InstrProfSymtab::create(Module &M, bool InLTO) {
  for (Function &F : M) {
    // Function may not have a name: like using asm("") to overwrite the name.
    // Ignore in this case.
    if (!F.hasName())
      continue;
    if (Error E = addFuncWithName(F, getIRPGOFuncName(F, InLTO)))
      return E;
    // Also use getPGOFuncName() so that we can find records from older profiles
    if (Error E = addFuncWithName(F, getPGOFuncName(F, InLTO)))
      return E;
  }

  SmallVector<MDNode *, 2> Types;
  for (GlobalVariable &G : M.globals()) {
    if (!G.hasName() || !G.hasMetadata(LLVMContext::MD_type))
      continue;
    if (Error E = addVTableWithName(G, getPGOName(G, InLTO)))
      return E;
  }

  Sorted = false;
  finalizeSymtab();
  return Error::success();
}

Error InstrProfSymtab::addVTableWithName(GlobalVariable &VTable,
                                         StringRef VTablePGOName) {
  auto NameToGUIDMap = [&](StringRef Name) -> Error {
    if (Error E = addSymbolName(Name))
      return E;

    bool Inserted = true;
    std::tie(std::ignore, Inserted) =
        MD5VTableMap.try_emplace(GlobalValue::getGUID(Name), &VTable);
    if (!Inserted)
      LLVM_DEBUG(dbgs() << "GUID conflict within one module");
    return Error::success();
  };
  if (Error E = NameToGUIDMap(VTablePGOName))
    return E;

  StringRef CanonicalName = getCanonicalName(VTablePGOName);
  if (CanonicalName != VTablePGOName)
    return NameToGUIDMap(CanonicalName);

  return Error::success();
}

/// \c NameStrings is a string composed of one of more possibly encoded
/// sub-strings. The substrings are separated by 0 or more zero bytes. This
/// method decodes the string and calls `NameCallback` for each substring.
static Error
readAndDecodeStrings(StringRef NameStrings,
                     std::function<Error(StringRef)> NameCallback) {
  const uint8_t *P = NameStrings.bytes_begin();
  const uint8_t *EndP = NameStrings.bytes_end();
  while (P < EndP) {
    uint32_t N;
    uint64_t UncompressedSize = decodeULEB128(P, &N);
    P += N;
    uint64_t CompressedSize = decodeULEB128(P, &N);
    P += N;
    const bool IsCompressed = (CompressedSize != 0);
    SmallVector<uint8_t, 128> UncompressedNameStrings;
    StringRef NameStrings;
    if (IsCompressed) {
      if (!llvm::compression::zlib::isAvailable())
        return make_error<InstrProfError>(instrprof_error::zlib_unavailable);

      if (Error E = compression::zlib::decompress(ArrayRef(P, CompressedSize),
                                                  UncompressedNameStrings,
                                                  UncompressedSize)) {
        consumeError(std::move(E));
        return make_error<InstrProfError>(instrprof_error::uncompress_failed);
      }
      P += CompressedSize;
      NameStrings = toStringRef(UncompressedNameStrings);
    } else {
      NameStrings =
          StringRef(reinterpret_cast<const char *>(P), UncompressedSize);
      P += UncompressedSize;
    }
    // Now parse the name strings.
    SmallVector<StringRef, 0> Names;
    NameStrings.split(Names, getInstrProfNameSeparator());
    for (StringRef &Name : Names)
      if (Error E = NameCallback(Name))
        return E;

    while (P < EndP && *P == 0)
      P++;
  }
  return Error::success();
}

Error InstrProfSymtab::create(StringRef NameStrings) {
  return readAndDecodeStrings(
      NameStrings,
      std::bind(&InstrProfSymtab::addFuncName, this, std::placeholders::_1));
}

Error InstrProfSymtab::create(StringRef FuncNameStrings,
                              StringRef VTableNameStrings) {
  if (Error E = readAndDecodeStrings(FuncNameStrings,
                                     std::bind(&InstrProfSymtab::addFuncName,
                                               this, std::placeholders::_1)))
    return E;

  return readAndDecodeStrings(
      VTableNameStrings,
      std::bind(&InstrProfSymtab::addVTableName, this, std::placeholders::_1));
}

Error InstrProfSymtab::initVTableNamesFromCompressedStrings(
    StringRef CompressedVTableStrings) {
  return readAndDecodeStrings(
      CompressedVTableStrings,
      std::bind(&InstrProfSymtab::addVTableName, this, std::placeholders::_1));
}

StringRef InstrProfSymtab::getCanonicalName(StringRef PGOName) {
  // In ThinLTO, local function may have been promoted to global and have
  // suffix ".llvm." added to the function name. We need to add the
  // stripped function name to the symbol table so that we can find a match
  // from profile.
  //
  // ".__uniq." suffix is used to differentiate internal linkage functions in
  // different modules and should be kept. This is the only suffix with the
  // pattern ".xxx" which is kept before matching, other suffixes similar as
  // ".llvm." will be stripped.
  const std::string UniqSuffix = ".__uniq.";
  size_t Pos = PGOName.find(UniqSuffix);
  if (Pos != StringRef::npos)
    Pos += UniqSuffix.length();
  else
    Pos = 0;

  // Search '.' after ".__uniq." if ".__uniq." exists, otherwise search '.' from
  // the beginning.
  Pos = PGOName.find('.', Pos);
  if (Pos != StringRef::npos && Pos != 0)
    return PGOName.substr(0, Pos);

  return PGOName;
}

Error InstrProfSymtab::addFuncWithName(Function &F, StringRef PGOFuncName) {
  auto NameToGUIDMap = [&](StringRef Name) -> Error {
    if (Error E = addFuncName(Name))
      return E;
    MD5FuncMap.emplace_back(Function::getGUID(Name), &F);
    return Error::success();
  };
  if (Error E = NameToGUIDMap(PGOFuncName))
    return E;

  StringRef CanonicalFuncName = getCanonicalName(PGOFuncName);
  if (CanonicalFuncName != PGOFuncName)
    return NameToGUIDMap(CanonicalFuncName);

  return Error::success();
}

uint64_t InstrProfSymtab::getVTableHashFromAddress(uint64_t Address) {
  // Given a runtime address, look up the hash value in the interval map, and
  // fallback to value 0 if a hash value is not found.
  return VTableAddrMap.lookup(Address, 0);
}

uint64_t InstrProfSymtab::getFunctionHashFromAddress(uint64_t Address) {
  finalizeSymtab();
  auto It = partition_point(AddrToMD5Map, [=](std::pair<uint64_t, uint64_t> A) {
    return A.first < Address;
  });
  // Raw function pointer collected by value profiler may be from
  // external functions that are not instrumented. They won't have
  // mapping data to be used by the deserializer. Force the value to
  // be 0 in this case.
  if (It != AddrToMD5Map.end() && It->first == Address)
    return (uint64_t)It->second;
  return 0;
}

void InstrProfSymtab::dumpNames(raw_ostream &OS) const {
  SmallVector<StringRef, 0> Sorted(NameTab.keys());
  llvm::sort(Sorted);
  for (StringRef S : Sorted)
    OS << S << '\n';
}

Error collectGlobalObjectNameStrings(ArrayRef<std::string> NameStrs,
                                     bool DoCompression, std::string &Result) {
  assert(!NameStrs.empty() && "No name data to emit");

  uint8_t Header[20], *P = Header;
  std::string UncompressedNameStrings =
      join(NameStrs.begin(), NameStrs.end(), getInstrProfNameSeparator());

  assert(StringRef(UncompressedNameStrings)
                 .count(getInstrProfNameSeparator()) == (NameStrs.size() - 1) &&
         "PGO name is invalid (contains separator token)");

  unsigned EncLen = encodeULEB128(UncompressedNameStrings.length(), P);
  P += EncLen;

  auto WriteStringToResult = [&](size_t CompressedLen, StringRef InputStr) {
    EncLen = encodeULEB128(CompressedLen, P);
    P += EncLen;
    char *HeaderStr = reinterpret_cast<char *>(&Header[0]);
    unsigned HeaderLen = P - &Header[0];
    Result.append(HeaderStr, HeaderLen);
    Result += InputStr;
    return Error::success();
  };

  if (!DoCompression) {
    return WriteStringToResult(0, UncompressedNameStrings);
  }

  SmallVector<uint8_t, 128> CompressedNameStrings;
  compression::zlib::compress(arrayRefFromStringRef(UncompressedNameStrings),
                              CompressedNameStrings,
                              compression::zlib::BestSizeCompression);

  return WriteStringToResult(CompressedNameStrings.size(),
                             toStringRef(CompressedNameStrings));
}

StringRef getPGOFuncNameVarInitializer(GlobalVariable *NameVar) {
  auto *Arr = cast<ConstantDataArray>(NameVar->getInitializer());
  StringRef NameStr =
      Arr->isCString() ? Arr->getAsCString() : Arr->getAsString();
  return NameStr;
}

Error collectPGOFuncNameStrings(ArrayRef<GlobalVariable *> NameVars,
                                std::string &Result, bool DoCompression) {
  std::vector<std::string> NameStrs;
  for (auto *NameVar : NameVars) {
    NameStrs.push_back(std::string(getPGOFuncNameVarInitializer(NameVar)));
  }
  return collectGlobalObjectNameStrings(
      NameStrs, compression::zlib::isAvailable() && DoCompression, Result);
}

Error collectVTableStrings(ArrayRef<GlobalVariable *> VTables,
                           std::string &Result, bool DoCompression) {
  std::vector<std::string> VTableNameStrs;
  for (auto *VTable : VTables)
    VTableNameStrs.push_back(getPGOName(*VTable));
  return collectGlobalObjectNameStrings(
      VTableNameStrs, compression::zlib::isAvailable() && DoCompression,
      Result);
}

void InstrProfRecord::accumulateCounts(CountSumOrPercent &Sum) const {
  uint64_t FuncSum = 0;
  Sum.NumEntries += Counts.size();
  for (uint64_t Count : Counts)
    FuncSum += Count;
  Sum.CountSum += FuncSum;

  for (uint32_t VK = IPVK_First; VK <= IPVK_Last; ++VK) {
    uint64_t KindSum = 0;
    uint32_t NumValueSites = getNumValueSites(VK);
    for (size_t I = 0; I < NumValueSites; ++I) {
      for (const auto &V : getValueArrayForSite(VK, I))
        KindSum += V.Count;
    }
    Sum.ValueCounts[VK] += KindSum;
  }
}

void InstrProfValueSiteRecord::overlap(InstrProfValueSiteRecord &Input,
                                       uint32_t ValueKind,
                                       OverlapStats &Overlap,
                                       OverlapStats &FuncLevelOverlap) {
  this->sortByTargetValues();
  Input.sortByTargetValues();
  double Score = 0.0f, FuncLevelScore = 0.0f;
  auto I = ValueData.begin();
  auto IE = ValueData.end();
  auto J = Input.ValueData.begin();
  auto JE = Input.ValueData.end();
  while (I != IE && J != JE) {
    if (I->Value == J->Value) {
      Score += OverlapStats::score(I->Count, J->Count,
                                   Overlap.Base.ValueCounts[ValueKind],
                                   Overlap.Test.ValueCounts[ValueKind]);
      FuncLevelScore += OverlapStats::score(
          I->Count, J->Count, FuncLevelOverlap.Base.ValueCounts[ValueKind],
          FuncLevelOverlap.Test.ValueCounts[ValueKind]);
      ++I;
    } else if (I->Value < J->Value) {
      ++I;
      continue;
    }
    ++J;
  }
  Overlap.Overlap.ValueCounts[ValueKind] += Score;
  FuncLevelOverlap.Overlap.ValueCounts[ValueKind] += FuncLevelScore;
}

// Return false on mismatch.
void InstrProfRecord::overlapValueProfData(uint32_t ValueKind,
                                           InstrProfRecord &Other,
                                           OverlapStats &Overlap,
                                           OverlapStats &FuncLevelOverlap) {
  uint32_t ThisNumValueSites = getNumValueSites(ValueKind);
  assert(ThisNumValueSites == Other.getNumValueSites(ValueKind));
  if (!ThisNumValueSites)
    return;

  std::vector<InstrProfValueSiteRecord> &ThisSiteRecords =
      getOrCreateValueSitesForKind(ValueKind);
  MutableArrayRef<InstrProfValueSiteRecord> OtherSiteRecords =
      Other.getValueSitesForKind(ValueKind);
  for (uint32_t I = 0; I < ThisNumValueSites; I++)
    ThisSiteRecords[I].overlap(OtherSiteRecords[I], ValueKind, Overlap,
                               FuncLevelOverlap);
}

void InstrProfRecord::overlap(InstrProfRecord &Other, OverlapStats &Overlap,
                              OverlapStats &FuncLevelOverlap,
                              uint64_t ValueCutoff) {
  // FuncLevel CountSum for other should already computed and nonzero.
  assert(FuncLevelOverlap.Test.CountSum >= 1.0f);
  accumulateCounts(FuncLevelOverlap.Base);
  bool Mismatch = (Counts.size() != Other.Counts.size());

  // Check if the value profiles mismatch.
  if (!Mismatch) {
    for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind) {
      uint32_t ThisNumValueSites = getNumValueSites(Kind);
      uint32_t OtherNumValueSites = Other.getNumValueSites(Kind);
      if (ThisNumValueSites != OtherNumValueSites) {
        Mismatch = true;
        break;
      }
    }
  }
  if (Mismatch) {
    Overlap.addOneMismatch(FuncLevelOverlap.Test);
    return;
  }

  // Compute overlap for value counts.
  for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind)
    overlapValueProfData(Kind, Other, Overlap, FuncLevelOverlap);

  double Score = 0.0;
  uint64_t MaxCount = 0;
  // Compute overlap for edge counts.
  for (size_t I = 0, E = Other.Counts.size(); I < E; ++I) {
    Score += OverlapStats::score(Counts[I], Other.Counts[I],
                                 Overlap.Base.CountSum, Overlap.Test.CountSum);
    MaxCount = std::max(Other.Counts[I], MaxCount);
  }
  Overlap.Overlap.CountSum += Score;
  Overlap.Overlap.NumEntries += 1;

  if (MaxCount >= ValueCutoff) {
    double FuncScore = 0.0;
    for (size_t I = 0, E = Other.Counts.size(); I < E; ++I)
      FuncScore += OverlapStats::score(Counts[I], Other.Counts[I],
                                       FuncLevelOverlap.Base.CountSum,
                                       FuncLevelOverlap.Test.CountSum);
    FuncLevelOverlap.Overlap.CountSum = FuncScore;
    FuncLevelOverlap.Overlap.NumEntries = Other.Counts.size();
    FuncLevelOverlap.Valid = true;
  }
}

void InstrProfValueSiteRecord::merge(InstrProfValueSiteRecord &Input,
                                     uint64_t Weight,
                                     function_ref<void(instrprof_error)> Warn) {
  this->sortByTargetValues();
  Input.sortByTargetValues();
  auto I = ValueData.begin();
  auto IE = ValueData.end();
  std::vector<InstrProfValueData> Merged;
  Merged.reserve(std::max(ValueData.size(), Input.ValueData.size()));
  for (const InstrProfValueData &J : Input.ValueData) {
    while (I != IE && I->Value < J.Value) {
      Merged.push_back(*I);
      ++I;
    }
    if (I != IE && I->Value == J.Value) {
      bool Overflowed;
      I->Count = SaturatingMultiplyAdd(J.Count, Weight, I->Count, &Overflowed);
      if (Overflowed)
        Warn(instrprof_error::counter_overflow);
      Merged.push_back(*I);
      ++I;
      continue;
    }
    Merged.push_back(J);
  }
  Merged.insert(Merged.end(), I, IE);
  ValueData = std::move(Merged);
}

void InstrProfValueSiteRecord::scale(uint64_t N, uint64_t D,
                                     function_ref<void(instrprof_error)> Warn) {
  for (InstrProfValueData &I : ValueData) {
    bool Overflowed;
    I.Count = SaturatingMultiply(I.Count, N, &Overflowed) / D;
    if (Overflowed)
      Warn(instrprof_error::counter_overflow);
  }
}

// Merge Value Profile data from Src record to this record for ValueKind.
// Scale merged value counts by \p Weight.
void InstrProfRecord::mergeValueProfData(
    uint32_t ValueKind, InstrProfRecord &Src, uint64_t Weight,
    function_ref<void(instrprof_error)> Warn) {
  uint32_t ThisNumValueSites = getNumValueSites(ValueKind);
  uint32_t OtherNumValueSites = Src.getNumValueSites(ValueKind);
  if (ThisNumValueSites != OtherNumValueSites) {
    Warn(instrprof_error::value_site_count_mismatch);
    return;
  }
  if (!ThisNumValueSites)
    return;
  std::vector<InstrProfValueSiteRecord> &ThisSiteRecords =
      getOrCreateValueSitesForKind(ValueKind);
  MutableArrayRef<InstrProfValueSiteRecord> OtherSiteRecords =
      Src.getValueSitesForKind(ValueKind);
  for (uint32_t I = 0; I < ThisNumValueSites; I++)
    ThisSiteRecords[I].merge(OtherSiteRecords[I], Weight, Warn);
}

void InstrProfRecord::merge(InstrProfRecord &Other, uint64_t Weight,
                            function_ref<void(instrprof_error)> Warn) {
  // If the number of counters doesn't match we either have bad data
  // or a hash collision.
  if (Counts.size() != Other.Counts.size()) {
    Warn(instrprof_error::count_mismatch);
    return;
  }

  // Special handling of the first count as the PseudoCount.
  CountPseudoKind OtherKind = Other.getCountPseudoKind();
  CountPseudoKind ThisKind = getCountPseudoKind();
  if (OtherKind != NotPseudo || ThisKind != NotPseudo) {
    // We don't allow the merge of a profile with pseudo counts and
    // a normal profile (i.e. without pesudo counts).
    // Profile supplimenation should be done after the profile merge.
    if (OtherKind == NotPseudo || ThisKind == NotPseudo) {
      Warn(instrprof_error::count_mismatch);
      return;
    }
    if (OtherKind == PseudoHot || ThisKind == PseudoHot)
      setPseudoCount(PseudoHot);
    else
      setPseudoCount(PseudoWarm);
    return;
  }

  for (size_t I = 0, E = Other.Counts.size(); I < E; ++I) {
    bool Overflowed;
    uint64_t Value =
        SaturatingMultiplyAdd(Other.Counts[I], Weight, Counts[I], &Overflowed);
    if (Value > getInstrMaxCountValue()) {
      Value = getInstrMaxCountValue();
      Overflowed = true;
    }
    Counts[I] = Value;
    if (Overflowed)
      Warn(instrprof_error::counter_overflow);
  }

  // If the number of bitmap bytes doesn't match we either have bad data
  // or a hash collision.
  if (BitmapBytes.size() != Other.BitmapBytes.size()) {
    Warn(instrprof_error::bitmap_mismatch);
    return;
  }

  // Bitmap bytes are merged by simply ORing them together.
  for (size_t I = 0, E = Other.BitmapBytes.size(); I < E; ++I) {
    BitmapBytes[I] = Other.BitmapBytes[I] | BitmapBytes[I];
  }

  for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind)
    mergeValueProfData(Kind, Other, Weight, Warn);
}

void InstrProfRecord::scaleValueProfData(
    uint32_t ValueKind, uint64_t N, uint64_t D,
    function_ref<void(instrprof_error)> Warn) {
  for (auto &R : getValueSitesForKind(ValueKind))
    R.scale(N, D, Warn);
}

void InstrProfRecord::scale(uint64_t N, uint64_t D,
                            function_ref<void(instrprof_error)> Warn) {
  assert(D != 0 && "D cannot be 0");
  for (auto &Count : this->Counts) {
    bool Overflowed;
    Count = SaturatingMultiply(Count, N, &Overflowed) / D;
    if (Count > getInstrMaxCountValue()) {
      Count = getInstrMaxCountValue();
      Overflowed = true;
    }
    if (Overflowed)
      Warn(instrprof_error::counter_overflow);
  }
  for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind)
    scaleValueProfData(Kind, N, D, Warn);
}

// Map indirect call target name hash to name string.
uint64_t InstrProfRecord::remapValue(uint64_t Value, uint32_t ValueKind,
                                     InstrProfSymtab *SymTab) {
  if (!SymTab)
    return Value;

  if (ValueKind == IPVK_IndirectCallTarget)
    return SymTab->getFunctionHashFromAddress(Value);

  if (ValueKind == IPVK_VTableTarget)
    return SymTab->getVTableHashFromAddress(Value);

  return Value;
}

void InstrProfRecord::addValueData(uint32_t ValueKind, uint32_t Site,
                                   ArrayRef<InstrProfValueData> VData,
                                   InstrProfSymtab *ValueMap) {
  // Remap values.
  std::vector<InstrProfValueData> RemappedVD;
  RemappedVD.reserve(VData.size());
  for (const auto &V : VData) {
    uint64_t NewValue = remapValue(V.Value, ValueKind, ValueMap);
    RemappedVD.push_back({NewValue, V.Count});
  }

  std::vector<InstrProfValueSiteRecord> &ValueSites =
      getOrCreateValueSitesForKind(ValueKind);
  assert(ValueSites.size() == Site);

  // Add a new value site with remapped value profiling data.
  ValueSites.emplace_back(std::move(RemappedVD));
}

void TemporalProfTraceTy::createBPFunctionNodes(
    ArrayRef<TemporalProfTraceTy> Traces, std::vector<BPFunctionNode> &Nodes,
    bool RemoveOutlierUNs) {
  using IDT = BPFunctionNode::IDT;
  using UtilityNodeT = BPFunctionNode::UtilityNodeT;
  UtilityNodeT MaxUN = 0;
  DenseMap<IDT, size_t> IdToFirstTimestamp;
  DenseMap<IDT, UtilityNodeT> IdToFirstUN;
  DenseMap<IDT, SmallVector<UtilityNodeT>> IdToUNs;
  // TODO: We need to use the Trace.Weight field to give more weight to more
  // important utilities
  for (auto &Trace : Traces) {
    size_t CutoffTimestamp = 1;
    for (size_t Timestamp = 0; Timestamp < Trace.FunctionNameRefs.size();
         Timestamp++) {
      IDT Id = Trace.FunctionNameRefs[Timestamp];
      auto [It, WasInserted] = IdToFirstTimestamp.try_emplace(Id, Timestamp);
      if (!WasInserted)
        It->getSecond() = std::min<size_t>(It->getSecond(), Timestamp);
      if (Timestamp >= CutoffTimestamp) {
        ++MaxUN;
        CutoffTimestamp = 2 * Timestamp;
      }
      IdToFirstUN.try_emplace(Id, MaxUN);
    }
    for (auto &[Id, FirstUN] : IdToFirstUN)
      for (auto UN = FirstUN; UN <= MaxUN; ++UN)
        IdToUNs[Id].push_back(UN);
    ++MaxUN;
    IdToFirstUN.clear();
  }

  if (RemoveOutlierUNs) {
    DenseMap<UtilityNodeT, unsigned> UNFrequency;
    for (auto &[Id, UNs] : IdToUNs)
      for (auto &UN : UNs)
        ++UNFrequency[UN];
    // Filter out utility nodes that are too infrequent or too prevalent to make
    // BalancedPartitioning more effective.
    for (auto &[Id, UNs] : IdToUNs)
      llvm::erase_if(UNs, [&](auto &UN) {
        return UNFrequency[UN] <= 1 || 2 * UNFrequency[UN] > IdToUNs.size();
      });
  }

  for (auto &[Id, UNs] : IdToUNs)
    Nodes.emplace_back(Id, UNs);

  // Since BalancedPartitioning is sensitive to the initial order, we explicitly
  // order nodes by their earliest timestamp.
  llvm::sort(Nodes, [&](auto &L, auto &R) {
    return std::make_pair(IdToFirstTimestamp[L.Id], L.Id) <
           std::make_pair(IdToFirstTimestamp[R.Id], R.Id);
  });
}

#define INSTR_PROF_COMMON_API_IMPL
#include "llvm/ProfileData/InstrProfData.inc"

/*!
 * ValueProfRecordClosure Interface implementation for  InstrProfRecord
 *  class. These C wrappers are used as adaptors so that C++ code can be
 *  invoked as callbacks.
 */
uint32_t getNumValueKindsInstrProf(const void *Record) {
  return reinterpret_cast<const InstrProfRecord *>(Record)->getNumValueKinds();
}

uint32_t getNumValueSitesInstrProf(const void *Record, uint32_t VKind) {
  return reinterpret_cast<const InstrProfRecord *>(Record)
      ->getNumValueSites(VKind);
}

uint32_t getNumValueDataInstrProf(const void *Record, uint32_t VKind) {
  return reinterpret_cast<const InstrProfRecord *>(Record)
      ->getNumValueData(VKind);
}

uint32_t getNumValueDataForSiteInstrProf(const void *R, uint32_t VK,
                                         uint32_t S) {
  const auto *IPR = reinterpret_cast<const InstrProfRecord *>(R);
  return IPR->getValueArrayForSite(VK, S).size();
}

void getValueForSiteInstrProf(const void *R, InstrProfValueData *Dst,
                              uint32_t K, uint32_t S) {
  const auto *IPR = reinterpret_cast<const InstrProfRecord *>(R);
  llvm::copy(IPR->getValueArrayForSite(K, S), Dst);
}

ValueProfData *allocValueProfDataInstrProf(size_t TotalSizeInBytes) {
  ValueProfData *VD =
      (ValueProfData *)(new (::operator new(TotalSizeInBytes)) ValueProfData());
  memset(VD, 0, TotalSizeInBytes);
  return VD;
}

static ValueProfRecordClosure InstrProfRecordClosure = {
    nullptr,
    getNumValueKindsInstrProf,
    getNumValueSitesInstrProf,
    getNumValueDataInstrProf,
    getNumValueDataForSiteInstrProf,
    nullptr,
    getValueForSiteInstrProf,
    allocValueProfDataInstrProf};

// Wrapper implementation using the closure mechanism.
uint32_t ValueProfData::getSize(const InstrProfRecord &Record) {
  auto Closure = InstrProfRecordClosure;
  Closure.Record = &Record;
  return getValueProfDataSize(&Closure);
}

// Wrapper implementation using the closure mechanism.
std::unique_ptr<ValueProfData>
ValueProfData::serializeFrom(const InstrProfRecord &Record) {
  InstrProfRecordClosure.Record = &Record;

  std::unique_ptr<ValueProfData> VPD(
      serializeValueProfDataFrom(&InstrProfRecordClosure, nullptr));
  return VPD;
}

void ValueProfRecord::deserializeTo(InstrProfRecord &Record,
                                    InstrProfSymtab *SymTab) {
  Record.reserveSites(Kind, NumValueSites);

  InstrProfValueData *ValueData = getValueProfRecordValueData(this);
  for (uint64_t VSite = 0; VSite < NumValueSites; ++VSite) {
    uint8_t ValueDataCount = this->SiteCountArray[VSite];
    ArrayRef<InstrProfValueData> VDs(ValueData, ValueDataCount);
    Record.addValueData(Kind, VSite, VDs, SymTab);
    ValueData += ValueDataCount;
  }
}

// For writing/serializing,  Old is the host endianness, and  New is
// byte order intended on disk. For Reading/deserialization, Old
// is the on-disk source endianness, and New is the host endianness.
void ValueProfRecord::swapBytes(llvm::endianness Old, llvm::endianness New) {
  using namespace support;

  if (Old == New)
    return;

  if (llvm::endianness::native != Old) {
    sys::swapByteOrder<uint32_t>(NumValueSites);
    sys::swapByteOrder<uint32_t>(Kind);
  }
  uint32_t ND = getValueProfRecordNumValueData(this);
  InstrProfValueData *VD = getValueProfRecordValueData(this);

  // No need to swap byte array: SiteCountArrray.
  for (uint32_t I = 0; I < ND; I++) {
    sys::swapByteOrder<uint64_t>(VD[I].Value);
    sys::swapByteOrder<uint64_t>(VD[I].Count);
  }
  if (llvm::endianness::native == Old) {
    sys::swapByteOrder<uint32_t>(NumValueSites);
    sys::swapByteOrder<uint32_t>(Kind);
  }
}

void ValueProfData::deserializeTo(InstrProfRecord &Record,
                                  InstrProfSymtab *SymTab) {
  if (NumValueKinds == 0)
    return;

  ValueProfRecord *VR = getFirstValueProfRecord(this);
  for (uint32_t K = 0; K < NumValueKinds; K++) {
    VR->deserializeTo(Record, SymTab);
    VR = getValueProfRecordNext(VR);
  }
}

static std::unique_ptr<ValueProfData> allocValueProfData(uint32_t TotalSize) {
  return std::unique_ptr<ValueProfData>(new (::operator new(TotalSize))
                                            ValueProfData());
}

Error ValueProfData::checkIntegrity() {
  if (NumValueKinds > IPVK_Last + 1)
    return make_error<InstrProfError>(
        instrprof_error::malformed, "number of value profile kinds is invalid");
  // Total size needs to be multiple of quadword size.
  if (TotalSize % sizeof(uint64_t))
    return make_error<InstrProfError>(
        instrprof_error::malformed, "total size is not multiples of quardword");

  ValueProfRecord *VR = getFirstValueProfRecord(this);
  for (uint32_t K = 0; K < this->NumValueKinds; K++) {
    if (VR->Kind > IPVK_Last)
      return make_error<InstrProfError>(instrprof_error::malformed,
                                        "value kind is invalid");
    VR = getValueProfRecordNext(VR);
    if ((char *)VR - (char *)this > (ptrdiff_t)TotalSize)
      return make_error<InstrProfError>(
          instrprof_error::malformed,
          "value profile address is greater than total size");
  }
  return Error::success();
}

Expected<std::unique_ptr<ValueProfData>>
ValueProfData::getValueProfData(const unsigned char *D,
                                const unsigned char *const BufferEnd,
                                llvm::endianness Endianness) {
  using namespace support;

  if (D + sizeof(ValueProfData) > BufferEnd)
    return make_error<InstrProfError>(instrprof_error::truncated);

  const unsigned char *Header = D;
  uint32_t TotalSize = endian::readNext<uint32_t>(Header, Endianness);

  if (D + TotalSize > BufferEnd)
    return make_error<InstrProfError>(instrprof_error::too_large);

  std::unique_ptr<ValueProfData> VPD = allocValueProfData(TotalSize);
  memcpy(VPD.get(), D, TotalSize);
  // Byte swap.
  VPD->swapBytesToHost(Endianness);

  Error E = VPD->checkIntegrity();
  if (E)
    return std::move(E);

  return std::move(VPD);
}

void ValueProfData::swapBytesToHost(llvm::endianness Endianness) {
  using namespace support;

  if (Endianness == llvm::endianness::native)
    return;

  sys::swapByteOrder<uint32_t>(TotalSize);
  sys::swapByteOrder<uint32_t>(NumValueKinds);

  ValueProfRecord *VR = getFirstValueProfRecord(this);
  for (uint32_t K = 0; K < NumValueKinds; K++) {
    VR->swapBytes(Endianness, llvm::endianness::native);
    VR = getValueProfRecordNext(VR);
  }
}

void ValueProfData::swapBytesFromHost(llvm::endianness Endianness) {
  using namespace support;

  if (Endianness == llvm::endianness::native)
    return;

  ValueProfRecord *VR = getFirstValueProfRecord(this);
  for (uint32_t K = 0; K < NumValueKinds; K++) {
    ValueProfRecord *NVR = getValueProfRecordNext(VR);
    VR->swapBytes(llvm::endianness::native, Endianness);
    VR = NVR;
  }
  sys::swapByteOrder<uint32_t>(TotalSize);
  sys::swapByteOrder<uint32_t>(NumValueKinds);
}

void annotateValueSite(Module &M, Instruction &Inst,
                       const InstrProfRecord &InstrProfR,
                       InstrProfValueKind ValueKind, uint32_t SiteIdx,
                       uint32_t MaxMDCount) {
  auto VDs = InstrProfR.getValueArrayForSite(ValueKind, SiteIdx);
  if (VDs.empty())
    return;
  uint64_t Sum = 0;
  for (const InstrProfValueData &V : VDs)
    Sum = SaturatingAdd(Sum, V.Count);
  annotateValueSite(M, Inst, VDs, Sum, ValueKind, MaxMDCount);
}

void annotateValueSite(Module &M, Instruction &Inst,
                       ArrayRef<InstrProfValueData> VDs,
                       uint64_t Sum, InstrProfValueKind ValueKind,
                       uint32_t MaxMDCount) {
  if (VDs.empty())
    return;
  LLVMContext &Ctx = M.getContext();
  MDBuilder MDHelper(Ctx);
  SmallVector<Metadata *, 3> Vals;
  // Tag
  Vals.push_back(MDHelper.createString("VP"));
  // Value Kind
  Vals.push_back(MDHelper.createConstant(
      ConstantInt::get(Type::getInt32Ty(Ctx), ValueKind)));
  // Total Count
  Vals.push_back(
      MDHelper.createConstant(ConstantInt::get(Type::getInt64Ty(Ctx), Sum)));

  // Value Profile Data
  uint32_t MDCount = MaxMDCount;
  for (const auto &VD : VDs) {
    Vals.push_back(MDHelper.createConstant(
        ConstantInt::get(Type::getInt64Ty(Ctx), VD.Value)));
    Vals.push_back(MDHelper.createConstant(
        ConstantInt::get(Type::getInt64Ty(Ctx), VD.Count)));
    if (--MDCount == 0)
      break;
  }
  Inst.setMetadata(LLVMContext::MD_prof, MDNode::get(Ctx, Vals));
}

MDNode *mayHaveValueProfileOfKind(const Instruction &Inst,
                                  InstrProfValueKind ValueKind) {
  MDNode *MD = Inst.getMetadata(LLVMContext::MD_prof);
  if (!MD)
    return nullptr;

  if (MD->getNumOperands() < 5)
    return nullptr;

  MDString *Tag = cast<MDString>(MD->getOperand(0));
  if (!Tag || Tag->getString() != "VP")
    return nullptr;

  // Now check kind:
  ConstantInt *KindInt = mdconst::dyn_extract<ConstantInt>(MD->getOperand(1));
  if (!KindInt)
    return nullptr;
  if (KindInt->getZExtValue() != ValueKind)
    return nullptr;

  return MD;
}

SmallVector<InstrProfValueData, 4>
getValueProfDataFromInst(const Instruction &Inst, InstrProfValueKind ValueKind,
                         uint32_t MaxNumValueData, uint64_t &TotalC,
                         bool GetNoICPValue) {
  // Four inline elements seem to work well in practice.  With MaxNumValueData,
  // this array won't grow very big anyway.
  SmallVector<InstrProfValueData, 4> ValueData;
  MDNode *MD = mayHaveValueProfileOfKind(Inst, ValueKind);
  if (!MD)
    return ValueData;
  const unsigned NOps = MD->getNumOperands();
  // Get total count
  ConstantInt *TotalCInt = mdconst::dyn_extract<ConstantInt>(MD->getOperand(2));
  if (!TotalCInt)
    return ValueData;
  TotalC = TotalCInt->getZExtValue();

  ValueData.reserve((NOps - 3) / 2);
  for (unsigned I = 3; I < NOps; I += 2) {
    if (ValueData.size() >= MaxNumValueData)
      break;
    ConstantInt *Value = mdconst::dyn_extract<ConstantInt>(MD->getOperand(I));
    ConstantInt *Count =
        mdconst::dyn_extract<ConstantInt>(MD->getOperand(I + 1));
    if (!Value || !Count) {
      ValueData.clear();
      return ValueData;
    }
    uint64_t CntValue = Count->getZExtValue();
    if (!GetNoICPValue && (CntValue == NOMORE_ICP_MAGICNUM))
      continue;
    InstrProfValueData V;
    V.Value = Value->getZExtValue();
    V.Count = CntValue;
    ValueData.push_back(V);
  }
  return ValueData;
}

MDNode *getPGOFuncNameMetadata(const Function &F) {
  return F.getMetadata(getPGOFuncNameMetadataName());
}

static void createPGONameMetadata(GlobalObject &GO, StringRef MetadataName,
                                  StringRef PGOName) {
  // Only for internal linkage functions or global variables. The name is not
  // the same as PGO name for these global objects.
  if (GO.getName() == PGOName)
    return;

  // Don't create duplicated metadata.
  if (GO.getMetadata(MetadataName))
    return;

  LLVMContext &C = GO.getContext();
  MDNode *N = MDNode::get(C, MDString::get(C, PGOName));
  GO.setMetadata(MetadataName, N);
}

void createPGOFuncNameMetadata(Function &F, StringRef PGOFuncName) {
  return createPGONameMetadata(F, getPGOFuncNameMetadataName(), PGOFuncName);
}

void createPGONameMetadata(GlobalObject &GO, StringRef PGOName) {
  return createPGONameMetadata(GO, getPGONameMetadataName(), PGOName);
}

bool needsComdatForCounter(const GlobalObject &GO, const Module &M) {
  if (GO.hasComdat())
    return true;

  if (!Triple(M.getTargetTriple()).supportsCOMDAT())
    return false;

  // See createPGOFuncNameVar for more details. To avoid link errors, profile
  // counters for function with available_externally linkage needs to be changed
  // to linkonce linkage. On ELF based systems, this leads to weak symbols to be
  // created. Without using comdat, duplicate entries won't be removed by the
  // linker leading to increased data segement size and raw profile size. Even
  // worse, since the referenced counter from profile per-function data object
  // will be resolved to the common strong definition, the profile counts for
  // available_externally functions will end up being duplicated in raw profile
  // data. This can result in distorted profile as the counts of those dups
  // will be accumulated by the profile merger.
  GlobalValue::LinkageTypes Linkage = GO.getLinkage();
  if (Linkage != GlobalValue::ExternalWeakLinkage &&
      Linkage != GlobalValue::AvailableExternallyLinkage)
    return false;

  return true;
}

// Check if INSTR_PROF_RAW_VERSION_VAR is defined.
bool isIRPGOFlagSet(const Module *M) {
  const GlobalVariable *IRInstrVar =
      M->getNamedGlobal(INSTR_PROF_QUOTE(INSTR_PROF_RAW_VERSION_VAR));
  if (!IRInstrVar || IRInstrVar->hasLocalLinkage())
    return false;

  // For CSPGO+LTO, this variable might be marked as non-prevailing and we only
  // have the decl.
  if (IRInstrVar->isDeclaration())
    return true;

  // Check if the flag is set.
  if (!IRInstrVar->hasInitializer())
    return false;

  auto *InitVal = dyn_cast_or_null<ConstantInt>(IRInstrVar->getInitializer());
  if (!InitVal)
    return false;
  return (InitVal->getZExtValue() & VARIANT_MASK_IR_PROF) != 0;
}

// Check if we can safely rename this Comdat function.
bool canRenameComdatFunc(const Function &F, bool CheckAddressTaken) {
  if (F.getName().empty())
    return false;
  if (!needsComdatForCounter(F, *(F.getParent())))
    return false;
  // Unsafe to rename the address-taken function (which can be used in
  // function comparison).
  if (CheckAddressTaken && F.hasAddressTaken())
    return false;
  // Only safe to do if this function may be discarded if it is not used
  // in the compilation unit.
  if (!GlobalValue::isDiscardableIfUnused(F.getLinkage()))
    return false;

  // For AvailableExternallyLinkage functions.
  if (!F.hasComdat()) {
    assert(F.getLinkage() == GlobalValue::AvailableExternallyLinkage);
    return true;
  }
  return true;
}

// Create the variable for the profile file name.
void createProfileFileNameVar(Module &M, StringRef InstrProfileOutput) {
  if (InstrProfileOutput.empty())
    return;
  Constant *ProfileNameConst =
      ConstantDataArray::getString(M.getContext(), InstrProfileOutput, true);
  GlobalVariable *ProfileNameVar = new GlobalVariable(
      M, ProfileNameConst->getType(), true, GlobalValue::WeakAnyLinkage,
      ProfileNameConst, INSTR_PROF_QUOTE(INSTR_PROF_PROFILE_NAME_VAR));
  ProfileNameVar->setVisibility(GlobalValue::HiddenVisibility);
  Triple TT(M.getTargetTriple());
  if (TT.supportsCOMDAT()) {
    ProfileNameVar->setLinkage(GlobalValue::ExternalLinkage);
    ProfileNameVar->setComdat(M.getOrInsertComdat(
        StringRef(INSTR_PROF_QUOTE(INSTR_PROF_PROFILE_NAME_VAR))));
  }
}

Error OverlapStats::accumulateCounts(const std::string &BaseFilename,
                                     const std::string &TestFilename,
                                     bool IsCS) {
  auto GetProfileSum = [IsCS](const std::string &Filename,
                              CountSumOrPercent &Sum) -> Error {
    // This function is only used from llvm-profdata that doesn't use any kind
    // of VFS. Just create a default RealFileSystem to read profiles.
    auto FS = vfs::getRealFileSystem();
    auto ReaderOrErr = InstrProfReader::create(Filename, *FS);
    if (Error E = ReaderOrErr.takeError()) {
      return E;
    }
    auto Reader = std::move(ReaderOrErr.get());
    Reader->accumulateCounts(Sum, IsCS);
    return Error::success();
  };
  auto Ret = GetProfileSum(BaseFilename, Base);
  if (Ret)
    return Ret;
  Ret = GetProfileSum(TestFilename, Test);
  if (Ret)
    return Ret;
  this->BaseFilename = &BaseFilename;
  this->TestFilename = &TestFilename;
  Valid = true;
  return Error::success();
}

void OverlapStats::addOneMismatch(const CountSumOrPercent &MismatchFunc) {
  Mismatch.NumEntries += 1;
  Mismatch.CountSum += MismatchFunc.CountSum / Test.CountSum;
  for (unsigned I = 0; I < IPVK_Last - IPVK_First + 1; I++) {
    if (Test.ValueCounts[I] >= 1.0f)
      Mismatch.ValueCounts[I] +=
          MismatchFunc.ValueCounts[I] / Test.ValueCounts[I];
  }
}

void OverlapStats::addOneUnique(const CountSumOrPercent &UniqueFunc) {
  Unique.NumEntries += 1;
  Unique.CountSum += UniqueFunc.CountSum / Test.CountSum;
  for (unsigned I = 0; I < IPVK_Last - IPVK_First + 1; I++) {
    if (Test.ValueCounts[I] >= 1.0f)
      Unique.ValueCounts[I] += UniqueFunc.ValueCounts[I] / Test.ValueCounts[I];
  }
}

void OverlapStats::dump(raw_fd_ostream &OS) const {
  if (!Valid)
    return;

  const char *EntryName =
      (Level == ProgramLevel ? "functions" : "edge counters");
  if (Level == ProgramLevel) {
    OS << "Profile overlap infomation for base_profile: " << *BaseFilename
       << " and test_profile: " << *TestFilename << "\nProgram level:\n";
  } else {
    OS << "Function level:\n"
       << "  Function: " << FuncName << " (Hash=" << FuncHash << ")\n";
  }

  OS << "  # of " << EntryName << " overlap: " << Overlap.NumEntries << "\n";
  if (Mismatch.NumEntries)
    OS << "  # of " << EntryName << " mismatch: " << Mismatch.NumEntries
       << "\n";
  if (Unique.NumEntries)
    OS << "  # of " << EntryName
       << " only in test_profile: " << Unique.NumEntries << "\n";

  OS << "  Edge profile overlap: " << format("%.3f%%", Overlap.CountSum * 100)
     << "\n";
  if (Mismatch.NumEntries)
    OS << "  Mismatched count percentage (Edge): "
       << format("%.3f%%", Mismatch.CountSum * 100) << "\n";
  if (Unique.NumEntries)
    OS << "  Percentage of Edge profile only in test_profile: "
       << format("%.3f%%", Unique.CountSum * 100) << "\n";
  OS << "  Edge profile base count sum: " << format("%.0f", Base.CountSum)
     << "\n"
     << "  Edge profile test count sum: " << format("%.0f", Test.CountSum)
     << "\n";

  for (unsigned I = 0; I < IPVK_Last - IPVK_First + 1; I++) {
    if (Base.ValueCounts[I] < 1.0f && Test.ValueCounts[I] < 1.0f)
      continue;
    char ProfileKindName[20] = {0};
    switch (I) {
    case IPVK_IndirectCallTarget:
      strncpy(ProfileKindName, "IndirectCall", 19);
      break;
    case IPVK_MemOPSize:
      strncpy(ProfileKindName, "MemOP", 19);
      break;
    case IPVK_VTableTarget:
      strncpy(ProfileKindName, "VTable", 19);
      break;
    default:
      snprintf(ProfileKindName, 19, "VP[%d]", I);
      break;
    }
    OS << "  " << ProfileKindName
       << " profile overlap: " << format("%.3f%%", Overlap.ValueCounts[I] * 100)
       << "\n";
    if (Mismatch.NumEntries)
      OS << "  Mismatched count percentage (" << ProfileKindName
         << "): " << format("%.3f%%", Mismatch.ValueCounts[I] * 100) << "\n";
    if (Unique.NumEntries)
      OS << "  Percentage of " << ProfileKindName
         << " profile only in test_profile: "
         << format("%.3f%%", Unique.ValueCounts[I] * 100) << "\n";
    OS << "  " << ProfileKindName
       << " profile base count sum: " << format("%.0f", Base.ValueCounts[I])
       << "\n"
       << "  " << ProfileKindName
       << " profile test count sum: " << format("%.0f", Test.ValueCounts[I])
       << "\n";
  }
}

namespace IndexedInstrProf {
Expected<Header> Header::readFromBuffer(const unsigned char *Buffer) {
  using namespace support;
  static_assert(std::is_standard_layout_v<Header>,
                "Use standard layout for Header for simplicity");
  Header H;

  H.Magic = endian::readNext<uint64_t, llvm::endianness::little>(Buffer);
  // Check the magic number.
  if (H.Magic != IndexedInstrProf::Magic)
    return make_error<InstrProfError>(instrprof_error::bad_magic);

  // Read the version.
  H.Version = endian::readNext<uint64_t, llvm::endianness::little>(Buffer);
  if (H.getIndexedProfileVersion() >
      IndexedInstrProf::ProfVersion::CurrentVersion)
    return make_error<InstrProfError>(instrprof_error::unsupported_version);

  static_assert(IndexedInstrProf::ProfVersion::CurrentVersion == Version12,
                "Please update the reader as needed when a new field is added "
                "or when indexed profile version gets bumped.");

  Buffer += sizeof(uint64_t); // Skip Header.Unused field.
  H.HashType = endian::readNext<uint64_t, llvm::endianness::little>(Buffer);
  H.HashOffset = endian::readNext<uint64_t, llvm::endianness::little>(Buffer);
  if (H.getIndexedProfileVersion() >= 8)
    H.MemProfOffset =
        endian::readNext<uint64_t, llvm::endianness::little>(Buffer);
  if (H.getIndexedProfileVersion() >= 9)
    H.BinaryIdOffset =
        endian::readNext<uint64_t, llvm::endianness::little>(Buffer);
  // Version 11 is handled by this condition.
  if (H.getIndexedProfileVersion() >= 10)
    H.TemporalProfTracesOffset =
        endian::readNext<uint64_t, llvm::endianness::little>(Buffer);
  if (H.getIndexedProfileVersion() >= 12)
    H.VTableNamesOffset =
        endian::readNext<uint64_t, llvm::endianness::little>(Buffer);
  return H;
}

uint64_t Header::getIndexedProfileVersion() const {
  return GET_VERSION(Version);
}

size_t Header::size() const {
  switch (getIndexedProfileVersion()) {
    // To retain backward compatibility, new fields must be appended to the end
    // of the header, and byte offset of existing fields shouldn't change when
    // indexed profile version gets incremented.
    static_assert(
        IndexedInstrProf::ProfVersion::CurrentVersion == Version12,
        "Please update the size computation below if a new field has "
        "been added to the header; for a version bump without new "
        "fields, add a case statement to fall through to the latest version.");
  case 12ull:
    return 72;
  case 11ull:
    [[fallthrough]];
  case 10ull:
    return 64;
  case 9ull:
    return 56;
  case 8ull:
    return 48;
  default: // Version7 (when the backwards compatible header was introduced).
    return 40;
  }
}

} // namespace IndexedInstrProf

} // end namespace llvm
