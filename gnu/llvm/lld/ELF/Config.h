//===- Config.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_CONFIG_H
#define LLD_ELF_CONFIG_H

#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/PrettyStackTrace.h"
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace lld::elf {

class InputFile;
class BinaryFile;
class BitcodeFile;
class ELFFileBase;
class SharedFile;
class InputSectionBase;
class EhInputSection;
class Symbol;
class BitcodeCompiler;

enum ELFKind : uint8_t {
  ELFNoneKind,
  ELF32LEKind,
  ELF32BEKind,
  ELF64LEKind,
  ELF64BEKind
};

// For -Bno-symbolic, -Bsymbolic-non-weak-functions, -Bsymbolic-functions,
// -Bsymbolic-non-weak, -Bsymbolic.
enum class BsymbolicKind { None, NonWeakFunctions, Functions, NonWeak, All };

// For --build-id.
enum class BuildIdKind { None, Fast, Md5, Sha1, Hexstring, Uuid };

// For --call-graph-profile-sort={none,hfsort,cdsort}.
enum class CGProfileSortKind { None, Hfsort, Cdsort };

// For --discard-{all,locals,none}.
enum class DiscardPolicy { Default, All, Locals, None };

// For --icf={none,safe,all}.
enum class ICFLevel { None, Safe, All };

// For --strip-{all,debug}.
enum class StripPolicy { None, All, Debug };

// For --unresolved-symbols.
enum class UnresolvedPolicy { ReportError, Warn, Ignore };

// For --orphan-handling.
enum class OrphanHandlingPolicy { Place, Warn, Error };

// For --sort-section and linkerscript sorting rules.
enum class SortSectionPolicy {
  Default,
  None,
  Alignment,
  Name,
  Priority,
  Reverse,
};

// For --target2
enum class Target2Policy { Abs, Rel, GotRel };

// For tracking ARM Float Argument PCS
enum class ARMVFPArgKind { Default, Base, VFP, ToolChain };

// For -z noseparate-code, -z separate-code and -z separate-loadable-segments.
enum class SeparateSegmentKind { None, Code, Loadable };

// For -z *stack
enum class GnuStackKind { None, Exec, NoExec };

// For --lto=
enum LtoKind : uint8_t {UnifiedThin, UnifiedRegular, Default};

// For -z gcs=
enum class GcsPolicy { Implicit, Never, Always };

struct SymbolVersion {
  llvm::StringRef name;
  bool isExternCpp;
  bool hasWildcard;
};

// This struct contains symbols version definition that
// can be found in version script if it is used for link.
struct VersionDefinition {
  llvm::StringRef name;
  uint16_t id;
  SmallVector<SymbolVersion, 0> nonLocalPatterns;
  SmallVector<SymbolVersion, 0> localPatterns;
};

class LinkerDriver {
public:
  void linkerMain(ArrayRef<const char *> args);
  void addFile(StringRef path, bool withLOption);
  void addLibrary(StringRef name);

private:
  void createFiles(llvm::opt::InputArgList &args);
  void inferMachineType();
  template <class ELFT> void link(llvm::opt::InputArgList &args);
  template <class ELFT> void compileBitcodeFiles(bool skipLinkedOutput);
  bool tryAddFatLTOFile(MemoryBufferRef mb, StringRef archiveName,
                        uint64_t offsetInArchive, bool lazy);
  // True if we are in --whole-archive and --no-whole-archive.
  bool inWholeArchive = false;

  // True if we are in --start-lib and --end-lib.
  bool inLib = false;

  std::unique_ptr<BitcodeCompiler> lto;
  std::vector<InputFile *> files;
  InputFile *armCmseImpLib = nullptr;

public:
  SmallVector<std::pair<StringRef, unsigned>, 0> archiveFiles;
};

// This struct contains the global configuration for the linker.
// Most fields are direct mapping from the command line options
// and such fields have the same name as the corresponding options.
// Most fields are initialized by the ctx.driver.
struct Config {
  uint8_t osabi = 0;
  uint32_t andFeatures = 0;
  llvm::CachePruningPolicy thinLTOCachePolicy;
  llvm::SetVector<llvm::CachedHashString> dependencyFiles; // for --dependency-file
  llvm::StringMap<uint64_t> sectionStartMap;
  llvm::StringRef bfdname;
  llvm::StringRef chroot;
  llvm::StringRef dependencyFile;
  llvm::StringRef dwoDir;
  llvm::StringRef dynamicLinker;
  llvm::StringRef entry;
  llvm::StringRef emulation;
  llvm::StringRef fini;
  llvm::StringRef init;
  llvm::StringRef ltoAAPipeline;
  llvm::StringRef ltoCSProfileFile;
  llvm::StringRef ltoNewPmPasses;
  llvm::StringRef ltoObjPath;
  llvm::StringRef ltoSampleProfile;
  llvm::StringRef mapFile;
  llvm::StringRef outputFile;
  llvm::StringRef optRemarksFilename;
  std::optional<uint64_t> optRemarksHotnessThreshold = 0;
  llvm::StringRef optRemarksPasses;
  llvm::StringRef optRemarksFormat;
  llvm::StringRef optStatsFilename;
  llvm::StringRef progName;
  llvm::StringRef printArchiveStats;
  llvm::StringRef printSymbolOrder;
  llvm::StringRef soName;
  llvm::StringRef sysroot;
  llvm::StringRef thinLTOCacheDir;
  llvm::StringRef thinLTOIndexOnlyArg;
  llvm::StringRef whyExtract;
  llvm::StringRef cmseInputLib;
  llvm::StringRef cmseOutputLib;
  StringRef zBtiReport = "none";
  StringRef zCetReport = "none";
  StringRef zPauthReport = "none";
  StringRef zGcsReport = "none";
  bool ltoBBAddrMap;
  llvm::StringRef ltoBasicBlockSections;
  std::pair<llvm::StringRef, llvm::StringRef> thinLTOObjectSuffixReplace;
  llvm::StringRef thinLTOPrefixReplaceOld;
  llvm::StringRef thinLTOPrefixReplaceNew;
  llvm::StringRef thinLTOPrefixReplaceNativeObject;
  std::string rpath;
  llvm::SmallVector<VersionDefinition, 0> versionDefinitions;
  llvm::SmallVector<llvm::StringRef, 0> auxiliaryList;
  llvm::SmallVector<llvm::StringRef, 0> filterList;
  llvm::SmallVector<llvm::StringRef, 0> passPlugins;
  llvm::SmallVector<llvm::StringRef, 0> searchPaths;
  llvm::SmallVector<llvm::StringRef, 0> symbolOrderingFile;
  llvm::SmallVector<llvm::StringRef, 0> thinLTOModulesToCompile;
  llvm::SmallVector<llvm::StringRef, 0> undefined;
  llvm::SmallVector<SymbolVersion, 0> dynamicList;
  llvm::SmallVector<uint8_t, 0> buildIdVector;
  llvm::SmallVector<llvm::StringRef, 0> mllvmOpts;
  llvm::MapVector<std::pair<const InputSectionBase *, const InputSectionBase *>,
                  uint64_t>
      callGraphProfile;
  bool cmseImplib = false;
  bool allowMultipleDefinition;
  bool fatLTOObjects;
  bool androidPackDynRelocs = false;
  bool armHasArmISA = false;
  bool armHasThumb2ISA = false;
  bool armHasBlx = false;
  bool armHasMovtMovw = false;
  bool armJ1J2BranchEncoding = false;
  bool armCMSESupport = false;
  bool asNeeded = false;
  bool armBe8 = false;
  BsymbolicKind bsymbolic = BsymbolicKind::None;
  CGProfileSortKind callGraphProfileSort;
  bool checkSections;
  bool checkDynamicRelocs;
  std::optional<llvm::DebugCompressionType> compressDebugSections;
  llvm::SmallVector<
      std::tuple<llvm::GlobPattern, llvm::DebugCompressionType, unsigned>, 0>
      compressSections;
  bool cref;
  llvm::SmallVector<std::pair<llvm::GlobPattern, uint64_t>, 0>
      deadRelocInNonAlloc;
  bool debugNames;
  bool demangle = true;
  bool dependentLibraries;
  bool disableVerify;
  bool ehFrameHdr;
  bool emitLLVM;
  bool emitRelocs;
  bool enableNewDtags;
  bool enableNonContiguousRegions;
  bool executeOnly;
  bool exportDynamic;
  bool fixCortexA53Errata843419;
  bool fixCortexA8;
  bool formatBinary = false;
  bool fortranCommon;
  bool gcSections;
  bool gdbIndex;
  bool gnuHash = false;
  bool gnuUnique;
  bool hasDynSymTab;
  bool ignoreDataAddressEquality;
  bool ignoreFunctionAddressEquality;
  bool ltoCSProfileGenerate;
  bool ltoPGOWarnMismatch;
  bool ltoDebugPassManager;
  bool ltoEmitAsm;
  bool ltoUniqueBasicBlockSectionNames;
  bool ltoValidateAllVtablesHaveTypeInfos;
  bool ltoWholeProgramVisibility;
  bool mergeArmExidx;
  bool mipsN32Abi = false;
  bool mmapOutputFile;
  bool nmagic;
  bool noDynamicLinker = false;
  bool noinhibitExec;
  bool nostdlib;
  bool oFormatBinary;
  bool omagic;
  bool optEB = false;
  bool optEL = false;
  bool optimizeBBJumps;
  bool optRemarksWithHotness;
  bool picThunk;
  bool pie;
  bool printGcSections;
  bool printIcfSections;
  bool printMemoryUsage;
  bool rejectMismatch;
  bool relax;
  bool relaxGP;
  bool relocatable;
  bool resolveGroups;
  bool relrGlibc = false;
  bool relrPackDynRelocs = false;
  llvm::DenseSet<llvm::StringRef> saveTempsArgs;
  llvm::SmallVector<std::pair<llvm::GlobPattern, uint32_t>, 0> shuffleSections;
  bool singleRoRx;
  bool shared;
  bool symbolic;
  bool isStatic = false;
  bool sysvHash = false;
  bool target1Rel;
  bool trace;
  bool thinLTOEmitImportsFiles;
  bool thinLTOEmitIndexFiles;
  bool thinLTOIndexOnly;
  bool timeTraceEnabled;
  bool tocOptimize;
  bool pcRelOptimize;
  bool undefinedVersion;
  bool unique;
  bool useAndroidRelrTags = false;
  bool warnBackrefs;
  llvm::SmallVector<llvm::GlobPattern, 0> warnBackrefsExclude;
  bool warnCommon;
  bool warnMissingEntry;
  bool warnSymbolOrdering;
  bool writeAddends;
  bool zCombreloc;
  bool zCopyreloc;
  bool zForceBti;
  bool zForceIbt;
  bool zGlobal;
  bool zHazardplt;
  bool zIfuncNoplt;
  bool zInitfirst;
  bool zInterpose;
  bool zKeepTextSectionPrefix;
  bool zLrodataAfterBss;
  bool zNoBtCfi;
  bool zNodefaultlib;
  bool zNodelete;
  bool zNodlopen;
  bool zNow;
  bool zOrigin;
  bool zPacPlt;
  bool zRelro;
  bool zRodynamic;
  bool zShstk;
  bool zStartStopGC;
  uint8_t zStartStopVisibility;
  bool zText;
  bool zRetpolineplt;
  bool zWxneeded;
  DiscardPolicy discard;
  GnuStackKind zGnustack;
  ICFLevel icf;
  OrphanHandlingPolicy orphanHandling;
  SortSectionPolicy sortSection;
  StripPolicy strip;
  UnresolvedPolicy unresolvedSymbols;
  UnresolvedPolicy unresolvedSymbolsInShlib;
  Target2Policy target2;
  GcsPolicy zGcs;
  bool power10Stubs;
  ARMVFPArgKind armVFPArgs = ARMVFPArgKind::Default;
  BuildIdKind buildId = BuildIdKind::None;
  SeparateSegmentKind zSeparate;
  ELFKind ekind = ELFNoneKind;
  uint16_t emachine = llvm::ELF::EM_NONE;
  std::optional<uint64_t> imageBase;
  // commonPageSize and maxPageSize are influenced by nmagic or omagic
  // so may be set to 1 if either of those options is given.
  uint64_t commonPageSize;
  uint64_t maxPageSize;
  // textAlignPageSize is the target max page size for the purpose
  // of aligning text sections, which may be unaligned if given nmagic
  uint64_t textAlignPageSize;
  uint64_t mipsGotSize;
  uint64_t zStackSize;
  unsigned ltoPartitions;
  unsigned ltoo;
  llvm::CodeGenOptLevel ltoCgo;
  unsigned optimize;
  StringRef thinLTOJobs;
  unsigned timeTraceGranularity;
  int32_t splitStackAdjustSize;
  StringRef packageMetadata;

  // The following config options do not directly correspond to any
  // particular command line options.

  // True if we need to pass through relocations in input files to the
  // output file. Usually false because we consume relocations.
  bool copyRelocs;

  // True if the target is ELF64. False if ELF32.
  bool is64;

  // True if the target is little-endian. False if big-endian.
  bool isLE;

  // endianness::little if isLE is true. endianness::big otherwise.
  llvm::endianness endianness;

  // True if the target is the little-endian MIPS64.
  //
  // The reason why we have this variable only for the MIPS is because
  // we use this often.  Some ELF headers for MIPS64EL are in a
  // mixed-endian (which is horrible and I'd say that's a serious spec
  // bug), and we need to know whether we are reading MIPS ELF files or
  // not in various places.
  //
  // (Note that MIPS64EL is not a typo for MIPS64LE. This is the official
  // name whatever that means. A fun hypothesis is that "EL" is short for
  // little-endian written in the little-endian order, but I don't know
  // if that's true.)
  bool isMips64EL;

  // True if we need to set the DF_STATIC_TLS flag to an output file, which
  // works as a hint to the dynamic loader that the shared object contains code
  // compiled with the initial-exec TLS model.
  bool hasTlsIe = false;

  // Holds set of ELF header flags for the target.
  uint32_t eflags = 0;

  // The ELF spec defines two types of relocation table entries, RELA and
  // REL. RELA is a triplet of (offset, info, addend) while REL is a
  // tuple of (offset, info). Addends for REL are implicit and read from
  // the location where the relocations are applied. So, REL is more
  // compact than RELA but requires a bit of more work to process.
  //
  // (From the linker writer's view, this distinction is not necessary.
  // If the ELF had chosen whichever and sticked with it, it would have
  // been easier to write code to process relocations, but it's too late
  // to change the spec.)
  //
  // Each ABI defines its relocation type. IsRela is true if target
  // uses RELA. As far as we know, all 64-bit ABIs are using RELA. A
  // few 32-bit ABIs are using RELA too.
  bool isRela;

  // True if we are creating position-independent code.
  bool isPic;

  // 4 for ELF32, 8 for ELF64.
  int wordsize;

  // Mode of MTE to write to the ELF note. Should be one of NT_MEMTAG_ASYNC (for
  // async), NT_MEMTAG_SYNC (for sync), or NT_MEMTAG_LEVEL_NONE (for none). If
  // async or sync is enabled, write the ELF note specifying the default MTE
  // mode.
  int androidMemtagMode;
  // Signal to the dynamic loader to enable heap MTE.
  bool androidMemtagHeap;
  // Signal to the dynamic loader that this binary expects stack MTE. Generally,
  // this means to map the primary and thread stacks as PROT_MTE. Note: This is
  // not supported on Android 11 & 12.
  bool androidMemtagStack;

  // When using a unified pre-link LTO pipeline, specify the backend LTO mode.
  LtoKind ltoKind = LtoKind::Default;

  unsigned threadCount;

  // If an input file equals a key, remap it to the value.
  llvm::DenseMap<llvm::StringRef, llvm::StringRef> remapInputs;
  // If an input file matches a wildcard pattern, remap it to the value.
  llvm::SmallVector<std::pair<llvm::GlobPattern, llvm::StringRef>, 0>
      remapInputsWildcards;
};
struct ConfigWrapper {
  Config c;
  Config *operator->() { return &c; }
};

LLVM_LIBRARY_VISIBILITY extern ConfigWrapper config;

struct DuplicateSymbol {
  const Symbol *sym;
  const InputFile *file;
  InputSectionBase *section;
  uint64_t value;
};

struct Ctx {
  LinkerDriver driver;
  SmallVector<std::unique_ptr<MemoryBuffer>> memoryBuffers;
  SmallVector<ELFFileBase *, 0> objectFiles;
  SmallVector<SharedFile *, 0> sharedFiles;
  SmallVector<BinaryFile *, 0> binaryFiles;
  SmallVector<BitcodeFile *, 0> bitcodeFiles;
  SmallVector<BitcodeFile *, 0> lazyBitcodeFiles;
  SmallVector<InputSectionBase *, 0> inputSections;
  SmallVector<EhInputSection *, 0> ehInputSections;
  // Duplicate symbol candidates.
  SmallVector<DuplicateSymbol, 0> duplicates;
  // Symbols in a non-prevailing COMDAT group which should be changed to an
  // Undefined.
  SmallVector<std::pair<Symbol *, unsigned>, 0> nonPrevailingSyms;
  // A tuple of (reference, extractedFile, sym). Used by --why-extract=.
  SmallVector<std::tuple<std::string, const InputFile *, const Symbol &>, 0>
      whyExtractRecords;
  // A mapping from a symbol to an InputFile referencing it backward. Used by
  // --warn-backrefs.
  llvm::DenseMap<const Symbol *,
                 std::pair<const InputFile *, const InputFile *>>
      backwardReferences;
  llvm::SmallSet<llvm::StringRef, 0> auxiliaryFiles;
  // InputFile for linker created symbols with no source location.
  InputFile *internalFile;
  // True if SHT_LLVM_SYMPART is used.
  std::atomic<bool> hasSympart{false};
  // True if there are TLS IE relocations. Set DF_STATIC_TLS if -shared.
  std::atomic<bool> hasTlsIe{false};
  // True if we need to reserve two .got entries for local-dynamic TLS model.
  std::atomic<bool> needsTlsLd{false};
  // True if all native vtable symbols have corresponding type info symbols
  // during LTO.
  bool ltoAllVtablesHaveTypeInfos;

  // Each symbol assignment and DEFINED(sym) reference is assigned an increasing
  // order. Each DEFINED(sym) evaluation checks whether the reference happens
  // before a possible `sym = expr;`.
  unsigned scriptSymOrderCounter = 1;
  llvm::DenseMap<const Symbol *, unsigned> scriptSymOrder;

  void reset();

  llvm::raw_fd_ostream openAuxiliaryFile(llvm::StringRef, std::error_code &);

  ArrayRef<uint8_t> aarch64PauthAbiCoreInfo;
};

LLVM_LIBRARY_VISIBILITY extern Ctx ctx;

// The first two elements of versionDefinitions represent VER_NDX_LOCAL and
// VER_NDX_GLOBAL. This helper returns other elements.
static inline ArrayRef<VersionDefinition> namedVersionDefs() {
  return llvm::ArrayRef(config->versionDefinitions).slice(2);
}

void errorOrWarn(const Twine &msg);

static inline void internalLinkerError(StringRef loc, const Twine &msg) {
  errorOrWarn(loc + "internal linker error: " + msg + "\n" +
              llvm::getBugReportMsg());
}

} // namespace lld::elf

#endif
