//===- SampleProfReader.h - Read LLVM sample profile data -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for reading sample profiles.
//
// NOTE: If you are making changes to this file format, please remember
//       to document them in the Clang documentation at
//       tools/clang/docs/UsersManual.rst.
//
// Text format
// -----------
//
// Sample profiles are written as ASCII text. The file is divided into
// sections, which correspond to each of the functions executed at runtime.
// Each section has the following format
//
//     function1:total_samples:total_head_samples
//      offset1[.discriminator]: number_of_samples [fn1:num fn2:num ... ]
//      offset2[.discriminator]: number_of_samples [fn3:num fn4:num ... ]
//      ...
//      offsetN[.discriminator]: number_of_samples [fn5:num fn6:num ... ]
//      offsetA[.discriminator]: fnA:num_of_total_samples
//       offsetA1[.discriminator]: number_of_samples [fn7:num fn8:num ... ]
//       ...
//      !CFGChecksum: num
//      !Attribute: flags
//
// This is a nested tree in which the indentation represents the nesting level
// of the inline stack. There are no blank lines in the file. And the spacing
// within a single line is fixed. Additional spaces will result in an error
// while reading the file.
//
// Any line starting with the '#' character is completely ignored.
//
// Inlined calls are represented with indentation. The Inline stack is a
// stack of source locations in which the top of the stack represents the
// leaf function, and the bottom of the stack represents the actual
// symbol to which the instruction belongs.
//
// Function names must be mangled in order for the profile loader to
// match them in the current translation unit. The two numbers in the
// function header specify how many total samples were accumulated in the
// function (first number), and the total number of samples accumulated
// in the prologue of the function (second number). This head sample
// count provides an indicator of how frequently the function is invoked.
//
// There are three types of lines in the function body.
//
// * Sampled line represents the profile information of a source location.
// * Callsite line represents the profile information of a callsite.
// * Metadata line represents extra metadata of the function.
//
// Each sampled line may contain several items. Some are optional (marked
// below):
//
// a. Source line offset. This number represents the line number
//    in the function where the sample was collected. The line number is
//    always relative to the line where symbol of the function is
//    defined. So, if the function has its header at line 280, the offset
//    13 is at line 293 in the file.
//
//    Note that this offset should never be a negative number. This could
//    happen in cases like macros. The debug machinery will register the
//    line number at the point of macro expansion. So, if the macro was
//    expanded in a line before the start of the function, the profile
//    converter should emit a 0 as the offset (this means that the optimizers
//    will not be able to associate a meaningful weight to the instructions
//    in the macro).
//
// b. [OPTIONAL] Discriminator. This is used if the sampled program
//    was compiled with DWARF discriminator support
//    (http://wiki.dwarfstd.org/index.php?title=Path_Discriminators).
//    DWARF discriminators are unsigned integer values that allow the
//    compiler to distinguish between multiple execution paths on the
//    same source line location.
//
//    For example, consider the line of code ``if (cond) foo(); else bar();``.
//    If the predicate ``cond`` is true 80% of the time, then the edge
//    into function ``foo`` should be considered to be taken most of the
//    time. But both calls to ``foo`` and ``bar`` are at the same source
//    line, so a sample count at that line is not sufficient. The
//    compiler needs to know which part of that line is taken more
//    frequently.
//
//    This is what discriminators provide. In this case, the calls to
//    ``foo`` and ``bar`` will be at the same line, but will have
//    different discriminator values. This allows the compiler to correctly
//    set edge weights into ``foo`` and ``bar``.
//
// c. Number of samples. This is an integer quantity representing the
//    number of samples collected by the profiler at this source
//    location.
//
// d. [OPTIONAL] Potential call targets and samples. If present, this
//    line contains a call instruction. This models both direct and
//    number of samples. For example,
//
//      130: 7  foo:3  bar:2  baz:7
//
//    The above means that at relative line offset 130 there is a call
//    instruction that calls one of ``foo()``, ``bar()`` and ``baz()``,
//    with ``baz()`` being the relatively more frequently called target.
//
// Each callsite line may contain several items. Some are optional.
//
// a. Source line offset. This number represents the line number of the
//    callsite that is inlined in the profiled binary.
//
// b. [OPTIONAL] Discriminator. Same as the discriminator for sampled line.
//
// c. Number of samples. This is an integer quantity representing the
//    total number of samples collected for the inlined instance at this
//    callsite
//
// Metadata line can occur in lines with one indent only, containing extra
// information for the top-level function. Furthermore, metadata can only
// occur after all the body samples and callsite samples.
// Each metadata line may contain a particular type of metadata, marked by
// the starting characters annotated with !. We process each metadata line
// independently, hence each metadata line has to form an independent piece
// of information that does not require cross-line reference.
// We support the following types of metadata:
//
// a. CFG Checksum (a.k.a. function hash):
//   !CFGChecksum: 12345
// b. CFG Checksum (see ContextAttributeMask):
//   !Atribute: 1
//
//
// Binary format
// -------------
//
// This is a more compact encoding. Numbers are encoded as ULEB128 values
// and all strings are encoded in a name table. The file is organized in
// the following sections:
//
// MAGIC (uint64_t)
//    File identifier computed by function SPMagic() (0x5350524f463432ff)
//
// VERSION (uint32_t)
//    File format version number computed by SPVersion()
//
// SUMMARY
//    TOTAL_COUNT (uint64_t)
//        Total number of samples in the profile.
//    MAX_COUNT (uint64_t)
//        Maximum value of samples on a line.
//    MAX_FUNCTION_COUNT (uint64_t)
//        Maximum number of samples at function entry (head samples).
//    NUM_COUNTS (uint64_t)
//        Number of lines with samples.
//    NUM_FUNCTIONS (uint64_t)
//        Number of functions with samples.
//    NUM_DETAILED_SUMMARY_ENTRIES (size_t)
//        Number of entries in detailed summary
//    DETAILED_SUMMARY
//        A list of detailed summary entry. Each entry consists of
//        CUTOFF (uint32_t)
//            Required percentile of total sample count expressed as a fraction
//            multiplied by 1000000.
//        MIN_COUNT (uint64_t)
//            The minimum number of samples required to reach the target
//            CUTOFF.
//        NUM_COUNTS (uint64_t)
//            Number of samples to get to the desrired percentile.
//
// NAME TABLE
//    SIZE (uint64_t)
//        Number of entries in the name table.
//    NAMES
//        A NUL-separated list of SIZE strings.
//
// FUNCTION BODY (one for each uninlined function body present in the profile)
//    HEAD_SAMPLES (uint64_t) [only for top-level functions]
//        Total number of samples collected at the head (prologue) of the
//        function.
//        NOTE: This field should only be present for top-level functions
//              (i.e., not inlined into any caller). Inlined function calls
//              have no prologue, so they don't need this.
//    NAME_IDX (uint64_t)
//        Index into the name table indicating the function name.
//    SAMPLES (uint64_t)
//        Total number of samples collected in this function.
//    NRECS (uint32_t)
//        Total number of sampling records this function's profile.
//    BODY RECORDS
//        A list of NRECS entries. Each entry contains:
//          OFFSET (uint32_t)
//            Line offset from the start of the function.
//          DISCRIMINATOR (uint32_t)
//            Discriminator value (see description of discriminators
//            in the text format documentation above).
//          SAMPLES (uint64_t)
//            Number of samples collected at this location.
//          NUM_CALLS (uint32_t)
//            Number of non-inlined function calls made at this location. In the
//            case of direct calls, this number will always be 1. For indirect
//            calls (virtual functions and function pointers) this will
//            represent all the actual functions called at runtime.
//          CALL_TARGETS
//            A list of NUM_CALLS entries for each called function:
//               NAME_IDX (uint64_t)
//                  Index into the name table with the callee name.
//               SAMPLES (uint64_t)
//                  Number of samples collected at the call site.
//    NUM_INLINED_FUNCTIONS (uint32_t)
//      Number of callees inlined into this function.
//    INLINED FUNCTION RECORDS
//      A list of NUM_INLINED_FUNCTIONS entries describing each of the inlined
//      callees.
//        OFFSET (uint32_t)
//          Line offset from the start of the function.
//        DISCRIMINATOR (uint32_t)
//          Discriminator value (see description of discriminators
//          in the text format documentation above).
//        FUNCTION BODY
//          A FUNCTION BODY entry describing the inlined function.
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_SAMPLEPROFREADER_H
#define LLVM_PROFILEDATA_SAMPLEPROFREADER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/GCOV.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/ProfileData/SymbolRemappingReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Discriminator.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace llvm {

class raw_ostream;
class Twine;

namespace vfs {
class FileSystem;
} // namespace vfs

namespace sampleprof {

class SampleProfileReader;

/// SampleProfileReaderItaniumRemapper remaps the profile data from a
/// sample profile data reader, by applying a provided set of equivalences
/// between components of the symbol names in the profile.
class SampleProfileReaderItaniumRemapper {
public:
  SampleProfileReaderItaniumRemapper(std::unique_ptr<MemoryBuffer> B,
                                     std::unique_ptr<SymbolRemappingReader> SRR,
                                     SampleProfileReader &R)
      : Buffer(std::move(B)), Remappings(std::move(SRR)), Reader(R) {
    assert(Remappings && "Remappings cannot be nullptr");
  }

  /// Create a remapper from the given remapping file. The remapper will
  /// be used for profile read in by Reader.
  static ErrorOr<std::unique_ptr<SampleProfileReaderItaniumRemapper>>
  create(StringRef Filename, vfs::FileSystem &FS, SampleProfileReader &Reader,
         LLVMContext &C);

  /// Create a remapper from the given Buffer. The remapper will
  /// be used for profile read in by Reader.
  static ErrorOr<std::unique_ptr<SampleProfileReaderItaniumRemapper>>
  create(std::unique_ptr<MemoryBuffer> &B, SampleProfileReader &Reader,
         LLVMContext &C);

  /// Apply remappings to the profile read by Reader.
  void applyRemapping(LLVMContext &Ctx);

  bool hasApplied() { return RemappingApplied; }

  /// Insert function name into remapper.
  void insert(StringRef FunctionName) { Remappings->insert(FunctionName); }

  /// Query whether there is equivalent in the remapper which has been
  /// inserted.
  bool exist(StringRef FunctionName) {
    return Remappings->lookup(FunctionName);
  }

  /// Return the equivalent name in the profile for \p FunctionName if
  /// it exists.
  std::optional<StringRef> lookUpNameInProfile(StringRef FunctionName);

private:
  // The buffer holding the content read from remapping file.
  std::unique_ptr<MemoryBuffer> Buffer;
  std::unique_ptr<SymbolRemappingReader> Remappings;
  // Map remapping key to the name in the profile. By looking up the
  // key in the remapper, a given new name can be mapped to the
  // cannonical name using the NameMap.
  DenseMap<SymbolRemappingReader::Key, StringRef> NameMap;
  // The Reader the remapper is servicing.
  SampleProfileReader &Reader;
  // Indicate whether remapping has been applied to the profile read
  // by Reader -- by calling applyRemapping.
  bool RemappingApplied = false;
};

/// Sample-based profile reader.
///
/// Each profile contains sample counts for all the functions
/// executed. Inside each function, statements are annotated with the
/// collected samples on all the instructions associated with that
/// statement.
///
/// For this to produce meaningful data, the program needs to be
/// compiled with some debug information (at minimum, line numbers:
/// -gline-tables-only). Otherwise, it will be impossible to match IR
/// instructions to the line numbers collected by the profiler.
///
/// From the profile file, we are interested in collecting the
/// following information:
///
/// * A list of functions included in the profile (mangled names).
///
/// * For each function F:
///   1. The total number of samples collected in F.
///
///   2. The samples collected at each line in F. To provide some
///      protection against source code shuffling, line numbers should
///      be relative to the start of the function.
///
/// The reader supports two file formats: text and binary. The text format
/// is useful for debugging and testing, while the binary format is more
/// compact and I/O efficient. They can both be used interchangeably.
class SampleProfileReader {
public:
  SampleProfileReader(std::unique_ptr<MemoryBuffer> B, LLVMContext &C,
                      SampleProfileFormat Format = SPF_None)
      : Profiles(), Ctx(C), Buffer(std::move(B)), Format(Format) {}

  virtual ~SampleProfileReader() = default;

  /// Read and validate the file header.
  virtual std::error_code readHeader() = 0;

  /// Set the bits for FS discriminators. Parameter Pass specify the sequence
  /// number, Pass == i is for the i-th round of adding FS discriminators.
  /// Pass == 0 is for using base discriminators.
  void setDiscriminatorMaskedBitFrom(FSDiscriminatorPass P) {
    MaskedBitFrom = getFSPassBitEnd(P);
  }

  /// Get the bitmask the discriminators: For FS profiles, return the bit
  /// mask for this pass. For non FS profiles, return (unsigned) -1.
  uint32_t getDiscriminatorMask() const {
    if (!ProfileIsFS)
      return 0xFFFFFFFF;
    assert((MaskedBitFrom != 0) && "MaskedBitFrom is not set properly");
    return getN1Bits(MaskedBitFrom);
  }

  /// The interface to read sample profiles from the associated file.
  std::error_code read() {
    if (std::error_code EC = readImpl())
      return EC;
    if (Remapper)
      Remapper->applyRemapping(Ctx);
    FunctionSamples::UseMD5 = useMD5();
    return sampleprof_error::success;
  }

  /// The implementaion to read sample profiles from the associated file.
  virtual std::error_code readImpl() = 0;

  /// Print the profile for \p FunctionSamples on stream \p OS.
  void dumpFunctionProfile(const FunctionSamples &FS, raw_ostream &OS = dbgs());

  /// Collect functions with definitions in Module M. For reader which
  /// support loading function profiles on demand, return true when the
  /// reader has been given a module. Always return false for reader
  /// which doesn't support loading function profiles on demand.
  virtual bool collectFuncsFromModule() { return false; }

  /// Print all the profiles on stream \p OS.
  void dump(raw_ostream &OS = dbgs());

  /// Print all the profiles on stream \p OS in the JSON format.
  void dumpJson(raw_ostream &OS = dbgs());

  /// Return the samples collected for function \p F.
  FunctionSamples *getSamplesFor(const Function &F) {
    // The function name may have been updated by adding suffix. Call
    // a helper to (optionally) strip off suffixes so that we can
    // match against the original function name in the profile.
    StringRef CanonName = FunctionSamples::getCanonicalFnName(F);
    return getSamplesFor(CanonName);
  }

  /// Return the samples collected for function \p F.
  FunctionSamples *getSamplesFor(StringRef Fname) {
    auto It = Profiles.find(FunctionId(Fname));
    if (It != Profiles.end())
      return &It->second;

    if (Remapper) {
      if (auto NameInProfile = Remapper->lookUpNameInProfile(Fname)) {
        auto It = Profiles.find(FunctionId(*NameInProfile));
        if (It != Profiles.end())
          return &It->second;
      }
    }
    return nullptr;
  }

  /// Return all the profiles.
  SampleProfileMap &getProfiles() { return Profiles; }

  /// Report a parse error message.
  void reportError(int64_t LineNumber, const Twine &Msg) const {
    Ctx.diagnose(DiagnosticInfoSampleProfile(Buffer->getBufferIdentifier(),
                                             LineNumber, Msg));
  }

  /// Create a sample profile reader appropriate to the file format.
  /// Create a remapper underlying if RemapFilename is not empty.
  /// Parameter P specifies the FSDiscriminatorPass.
  static ErrorOr<std::unique_ptr<SampleProfileReader>>
  create(StringRef Filename, LLVMContext &C, vfs::FileSystem &FS,
         FSDiscriminatorPass P = FSDiscriminatorPass::Base,
         StringRef RemapFilename = "");

  /// Create a sample profile reader from the supplied memory buffer.
  /// Create a remapper underlying if RemapFilename is not empty.
  /// Parameter P specifies the FSDiscriminatorPass.
  static ErrorOr<std::unique_ptr<SampleProfileReader>>
  create(std::unique_ptr<MemoryBuffer> &B, LLVMContext &C, vfs::FileSystem &FS,
         FSDiscriminatorPass P = FSDiscriminatorPass::Base,
         StringRef RemapFilename = "");

  /// Return the profile summary.
  ProfileSummary &getSummary() const { return *Summary; }

  MemoryBuffer *getBuffer() const { return Buffer.get(); }

  /// \brief Return the profile format.
  SampleProfileFormat getFormat() const { return Format; }

  /// Whether input profile is based on pseudo probes.
  bool profileIsProbeBased() const { return ProfileIsProbeBased; }

  /// Whether input profile is fully context-sensitive.
  bool profileIsCS() const { return ProfileIsCS; }

  /// Whether input profile contains ShouldBeInlined contexts.
  bool profileIsPreInlined() const { return ProfileIsPreInlined; }

  /// Whether input profile is flow-sensitive.
  bool profileIsFS() const { return ProfileIsFS; }

  virtual std::unique_ptr<ProfileSymbolList> getProfileSymbolList() {
    return nullptr;
  };

  /// It includes all the names that have samples either in outline instance
  /// or inline instance.
  virtual std::vector<FunctionId> *getNameTable() { return nullptr; }
  virtual bool dumpSectionInfo(raw_ostream &OS = dbgs()) { return false; };

  /// Return whether names in the profile are all MD5 numbers.
  bool useMD5() const { return ProfileIsMD5; }

  /// Force the profile to use MD5 in Sample contexts, even if function names
  /// are present.
  virtual void setProfileUseMD5() { ProfileIsMD5 = true; }

  /// Don't read profile without context if the flag is set. This is only meaningful
  /// for ExtBinary format.
  virtual void setSkipFlatProf(bool Skip) {}
  /// Return whether any name in the profile contains ".__uniq." suffix.
  virtual bool hasUniqSuffix() { return false; }

  SampleProfileReaderItaniumRemapper *getRemapper() { return Remapper.get(); }

  void setModule(const Module *Mod) { M = Mod; }

protected:
  /// Map every function to its associated profile.
  ///
  /// The profile of every function executed at runtime is collected
  /// in the structure FunctionSamples. This maps function objects
  /// to their corresponding profiles.
  SampleProfileMap Profiles;

  /// LLVM context used to emit diagnostics.
  LLVMContext &Ctx;

  /// Memory buffer holding the profile file.
  std::unique_ptr<MemoryBuffer> Buffer;

  /// Profile summary information.
  std::unique_ptr<ProfileSummary> Summary;

  /// Take ownership of the summary of this reader.
  static std::unique_ptr<ProfileSummary>
  takeSummary(SampleProfileReader &Reader) {
    return std::move(Reader.Summary);
  }

  /// Compute summary for this profile.
  void computeSummary();

  std::unique_ptr<SampleProfileReaderItaniumRemapper> Remapper;

  /// \brief Whether samples are collected based on pseudo probes.
  bool ProfileIsProbeBased = false;

  /// Whether function profiles are context-sensitive flat profiles.
  bool ProfileIsCS = false;

  /// Whether function profile contains ShouldBeInlined contexts.
  bool ProfileIsPreInlined = false;

  /// Number of context-sensitive profiles.
  uint32_t CSProfileCount = 0;

  /// Whether the function profiles use FS discriminators.
  bool ProfileIsFS = false;

  /// \brief The format of sample.
  SampleProfileFormat Format = SPF_None;

  /// \brief The current module being compiled if SampleProfileReader
  /// is used by compiler. If SampleProfileReader is used by other
  /// tools which are not compiler, M is usually nullptr.
  const Module *M = nullptr;

  /// Zero out the discriminator bits higher than bit MaskedBitFrom (0 based).
  /// The default is to keep all the bits.
  uint32_t MaskedBitFrom = 31;

  /// Whether the profile uses MD5 for Sample Contexts and function names. This
  /// can be one-way overriden by the user to force use MD5.
  bool ProfileIsMD5 = false;
};

class SampleProfileReaderText : public SampleProfileReader {
public:
  SampleProfileReaderText(std::unique_ptr<MemoryBuffer> B, LLVMContext &C)
      : SampleProfileReader(std::move(B), C, SPF_Text) {}

  /// Read and validate the file header.
  std::error_code readHeader() override { return sampleprof_error::success; }

  /// Read sample profiles from the associated file.
  std::error_code readImpl() override;

  /// Return true if \p Buffer is in the format supported by this class.
  static bool hasFormat(const MemoryBuffer &Buffer);

  /// Text format sample profile does not support MD5 for now.
  void setProfileUseMD5() override {}

private:
  /// CSNameTable is used to save full context vectors. This serves as an
  /// underlying immutable buffer for all clients.
  std::list<SampleContextFrameVector> CSNameTable;
};

class SampleProfileReaderBinary : public SampleProfileReader {
public:
  SampleProfileReaderBinary(std::unique_ptr<MemoryBuffer> B, LLVMContext &C,
                            SampleProfileFormat Format = SPF_None)
      : SampleProfileReader(std::move(B), C, Format) {}

  /// Read and validate the file header.
  std::error_code readHeader() override;

  /// Read sample profiles from the associated file.
  std::error_code readImpl() override;

  /// It includes all the names that have samples either in outline instance
  /// or inline instance.
  std::vector<FunctionId> *getNameTable() override {
    return &NameTable;
  }

protected:
  /// Read a numeric value of type T from the profile.
  ///
  /// If an error occurs during decoding, a diagnostic message is emitted and
  /// EC is set.
  ///
  /// \returns the read value.
  template <typename T> ErrorOr<T> readNumber();

  /// Read a numeric value of type T from the profile. The value is saved
  /// without encoded.
  template <typename T> ErrorOr<T> readUnencodedNumber();

  /// Read a string from the profile.
  ///
  /// If an error occurs during decoding, a diagnostic message is emitted and
  /// EC is set.
  ///
  /// \returns the read value.
  ErrorOr<StringRef> readString();

  /// Read the string index and check whether it overflows the table.
  template <typename T> inline ErrorOr<size_t> readStringIndex(T &Table);

  /// Read the next function profile instance.
  std::error_code readFuncProfile(const uint8_t *Start);

  /// Read the contents of the given profile instance.
  std::error_code readProfile(FunctionSamples &FProfile);

  /// Read the contents of Magic number and Version number.
  std::error_code readMagicIdent();

  /// Read profile summary.
  std::error_code readSummary();

  /// Read the whole name table.
  std::error_code readNameTable();

  /// Read a string indirectly via the name table. Optionally return the index.
  ErrorOr<FunctionId> readStringFromTable(size_t *RetIdx = nullptr);

  /// Read a context indirectly via the CSNameTable. Optionally return the
  /// index.
  ErrorOr<SampleContextFrames> readContextFromTable(size_t *RetIdx = nullptr);

  /// Read a context indirectly via the CSNameTable if the profile has context,
  /// otherwise same as readStringFromTable, also return its hash value.
  ErrorOr<std::pair<SampleContext, uint64_t>> readSampleContextFromTable();

  /// Points to the current location in the buffer.
  const uint8_t *Data = nullptr;

  /// Points to the end of the buffer.
  const uint8_t *End = nullptr;

  /// Function name table.
  std::vector<FunctionId> NameTable;

  /// CSNameTable is used to save full context vectors. It is the backing buffer
  /// for SampleContextFrames.
  std::vector<SampleContextFrameVector> CSNameTable;

  /// Table to cache MD5 values of sample contexts corresponding to
  /// readSampleContextFromTable(), used to index into Profiles or
  /// FuncOffsetTable.
  std::vector<uint64_t> MD5SampleContextTable;

  /// The starting address of the table of MD5 values of sample contexts. For
  /// fixed length MD5 non-CS profile it is same as MD5NameMemStart because
  /// hashes of non-CS contexts are already in the profile. Otherwise it points
  /// to the start of MD5SampleContextTable.
  const uint64_t *MD5SampleContextStart = nullptr;

private:
  std::error_code readSummaryEntry(std::vector<ProfileSummaryEntry> &Entries);
  virtual std::error_code verifySPMagic(uint64_t Magic) = 0;
};

class SampleProfileReaderRawBinary : public SampleProfileReaderBinary {
private:
  std::error_code verifySPMagic(uint64_t Magic) override;

public:
  SampleProfileReaderRawBinary(std::unique_ptr<MemoryBuffer> B, LLVMContext &C,
                               SampleProfileFormat Format = SPF_Binary)
      : SampleProfileReaderBinary(std::move(B), C, Format) {}

  /// \brief Return true if \p Buffer is in the format supported by this class.
  static bool hasFormat(const MemoryBuffer &Buffer);
};

/// SampleProfileReaderExtBinaryBase/SampleProfileWriterExtBinaryBase defines
/// the basic structure of the extensible binary format.
/// The format is organized in sections except the magic and version number
/// at the beginning. There is a section table before all the sections, and
/// each entry in the table describes the entry type, start, size and
/// attributes. The format in each section is defined by the section itself.
///
/// It is easy to add a new section while maintaining the backward
/// compatibility of the profile. Nothing extra needs to be done. If we want
/// to extend an existing section, like add cache misses information in
/// addition to the sample count in the profile body, we can add a new section
/// with the extension and retire the existing section, and we could choose
/// to keep the parser of the old section if we want the reader to be able
/// to read both new and old format profile.
///
/// SampleProfileReaderExtBinary/SampleProfileWriterExtBinary define the
/// commonly used sections of a profile in extensible binary format. It is
/// possible to define other types of profile inherited from
/// SampleProfileReaderExtBinaryBase/SampleProfileWriterExtBinaryBase.
class SampleProfileReaderExtBinaryBase : public SampleProfileReaderBinary {
private:
  std::error_code decompressSection(const uint8_t *SecStart,
                                    const uint64_t SecSize,
                                    const uint8_t *&DecompressBuf,
                                    uint64_t &DecompressBufSize);

  BumpPtrAllocator Allocator;

protected:
  std::vector<SecHdrTableEntry> SecHdrTable;
  std::error_code readSecHdrTableEntry(uint64_t Idx);
  std::error_code readSecHdrTable();

  std::error_code readFuncMetadata(bool ProfileHasAttribute);
  std::error_code readFuncMetadata(bool ProfileHasAttribute,
                                   FunctionSamples *FProfile);
  std::error_code readFuncOffsetTable();
  std::error_code readFuncProfiles();
  std::error_code readNameTableSec(bool IsMD5, bool FixedLengthMD5);
  std::error_code readCSNameTableSec();
  std::error_code readProfileSymbolList();

  std::error_code readHeader() override;
  std::error_code verifySPMagic(uint64_t Magic) override = 0;
  virtual std::error_code readOneSection(const uint8_t *Start, uint64_t Size,
                                         const SecHdrTableEntry &Entry);
  // placeholder for subclasses to dispatch their own section readers.
  virtual std::error_code readCustomSection(const SecHdrTableEntry &Entry) = 0;

  /// Determine which container readFuncOffsetTable() should populate, the list
  /// FuncOffsetList or the map FuncOffsetTable.
  bool useFuncOffsetList() const;

  std::unique_ptr<ProfileSymbolList> ProfSymList;

  /// The table mapping from a function context's MD5 to the offset of its
  /// FunctionSample towards file start.
  /// At most one of FuncOffsetTable and FuncOffsetList is populated.
  DenseMap<hash_code, uint64_t> FuncOffsetTable;

  /// The list version of FuncOffsetTable. This is used if every entry is
  /// being accessed.
  std::vector<std::pair<SampleContext, uint64_t>> FuncOffsetList;

  /// The set containing the functions to use when compiling a module.
  DenseSet<StringRef> FuncsToUse;

  /// If SkipFlatProf is true, skip the sections with
  /// SecFlagFlat flag.
  bool SkipFlatProf = false;

public:
  SampleProfileReaderExtBinaryBase(std::unique_ptr<MemoryBuffer> B,
                                   LLVMContext &C, SampleProfileFormat Format)
      : SampleProfileReaderBinary(std::move(B), C, Format) {}

  /// Read sample profiles in extensible format from the associated file.
  std::error_code readImpl() override;

  /// Get the total size of all \p Type sections.
  uint64_t getSectionSize(SecType Type);
  /// Get the total size of header and all sections.
  uint64_t getFileSize();
  bool dumpSectionInfo(raw_ostream &OS = dbgs()) override;

  /// Collect functions with definitions in Module M. Return true if
  /// the reader has been given a module.
  bool collectFuncsFromModule() override;

  std::unique_ptr<ProfileSymbolList> getProfileSymbolList() override {
    return std::move(ProfSymList);
  };

  void setSkipFlatProf(bool Skip) override { SkipFlatProf = Skip; }
};

class SampleProfileReaderExtBinary : public SampleProfileReaderExtBinaryBase {
private:
  std::error_code verifySPMagic(uint64_t Magic) override;
  std::error_code readCustomSection(const SecHdrTableEntry &Entry) override {
    // Update the data reader pointer to the end of the section.
    Data = End;
    return sampleprof_error::success;
  };

public:
  SampleProfileReaderExtBinary(std::unique_ptr<MemoryBuffer> B, LLVMContext &C,
                               SampleProfileFormat Format = SPF_Ext_Binary)
      : SampleProfileReaderExtBinaryBase(std::move(B), C, Format) {}

  /// \brief Return true if \p Buffer is in the format supported by this class.
  static bool hasFormat(const MemoryBuffer &Buffer);
};

using InlineCallStack = SmallVector<FunctionSamples *, 10>;

// Supported histogram types in GCC.  Currently, we only need support for
// call target histograms.
enum HistType {
  HIST_TYPE_INTERVAL,
  HIST_TYPE_POW2,
  HIST_TYPE_SINGLE_VALUE,
  HIST_TYPE_CONST_DELTA,
  HIST_TYPE_INDIR_CALL,
  HIST_TYPE_AVERAGE,
  HIST_TYPE_IOR,
  HIST_TYPE_INDIR_CALL_TOPN
};

class SampleProfileReaderGCC : public SampleProfileReader {
public:
  SampleProfileReaderGCC(std::unique_ptr<MemoryBuffer> B, LLVMContext &C)
      : SampleProfileReader(std::move(B), C, SPF_GCC),
        GcovBuffer(Buffer.get()) {}

  /// Read and validate the file header.
  std::error_code readHeader() override;

  /// Read sample profiles from the associated file.
  std::error_code readImpl() override;

  /// Return true if \p Buffer is in the format supported by this class.
  static bool hasFormat(const MemoryBuffer &Buffer);

protected:
  std::error_code readNameTable();
  std::error_code readOneFunctionProfile(const InlineCallStack &InlineStack,
                                         bool Update, uint32_t Offset);
  std::error_code readFunctionProfiles();
  std::error_code skipNextWord();
  template <typename T> ErrorOr<T> readNumber();
  ErrorOr<StringRef> readString();

  /// Read the section tag and check that it's the same as \p Expected.
  std::error_code readSectionTag(uint32_t Expected);

  /// GCOV buffer containing the profile.
  GCOVBuffer GcovBuffer;

  /// Function names in this profile.
  std::vector<std::string> Names;

  /// GCOV tags used to separate sections in the profile file.
  static const uint32_t GCOVTagAFDOFileNames = 0xaa000000;
  static const uint32_t GCOVTagAFDOFunction = 0xac000000;
};

} // end namespace sampleprof

} // end namespace llvm

#endif // LLVM_PROFILEDATA_SAMPLEPROFREADER_H
