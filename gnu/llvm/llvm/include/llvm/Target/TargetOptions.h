//===-- llvm/Target/TargetOptions.h - Target Options ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines command line option flags that are shared across various
// targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETOPTIONS_H
#define LLVM_TARGET_TARGETOPTIONS_H

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/MC/MCTargetOptions.h"

#include <memory>

namespace llvm {
  struct fltSemantics;
  class MachineFunction;
  class MemoryBuffer;

  namespace FloatABI {
    enum ABIType {
      Default, // Target-specific (either soft or hard depending on triple, etc).
      Soft,    // Soft float.
      Hard     // Hard float.
    };
  }

  namespace FPOpFusion {
    enum FPOpFusionMode {
      Fast,     // Enable fusion of FP ops wherever it's profitable.
      Standard, // Only allow fusion of 'blessed' ops (currently just fmuladd).
      Strict    // Never fuse FP-ops.
    };
  }

  namespace JumpTable {
    enum JumpTableType {
      Single,          // Use a single table for all indirect jumptable calls.
      Arity,           // Use one table per number of function parameters.
      Simplified,      // Use one table per function type, with types projected
                       // into 4 types: pointer to non-function, struct,
                       // primitive, and function pointer.
      Full             // Use one table per unique function type
    };
  }

  namespace ThreadModel {
    enum Model {
      POSIX,  // POSIX Threads
      Single  // Single Threaded Environment
    };
  }

  enum class BasicBlockSection {
    All,    // Use Basic Block Sections for all basic blocks.  A section
            // for every basic block can significantly bloat object file sizes.
    List,   // Get list of functions & BBs from a file. Selectively enables
            // basic block sections for a subset of basic blocks which can be
            // used to control object size bloats from creating sections.
    Labels, // Do not use Basic Block Sections but label basic blocks.  This
            // is useful when associating profile counts from virtual addresses
            // to basic blocks.
    Preset, // Similar to list but the blocks are identified by passes which
            // seek to use Basic Block Sections, e.g. MachineFunctionSplitter.
            // This option cannot be set via the command line.
    None    // Do not use Basic Block Sections.
  };

  enum class EABI {
    Unknown,
    Default, // Default means not specified
    EABI4,   // Target-specific (either 4, 5 or gnu depending on triple).
    EABI5,
    GNU
  };

  /// Identify a debugger for "tuning" the debug info.
  ///
  /// The "debugger tuning" concept allows us to present a more intuitive
  /// interface that unpacks into different sets of defaults for the various
  /// individual feature-flag settings, that suit the preferences of the
  /// various debuggers.  However, it's worth remembering that debuggers are
  /// not the only consumers of debug info, and some variations in DWARF might
  /// better be treated as target/platform issues. Fundamentally,
  /// o if the feature is useful (or not) to a particular debugger, regardless
  ///   of the target, that's a tuning decision;
  /// o if the feature is useful (or not) on a particular platform, regardless
  ///   of the debugger, that's a target decision.
  /// It's not impossible to see both factors in some specific case.
  enum class DebuggerKind {
    Default, ///< No specific tuning requested.
    GDB,     ///< Tune debug info for gdb.
    LLDB,    ///< Tune debug info for lldb.
    SCE,     ///< Tune debug info for SCE targets (e.g. PS4).
    DBX      ///< Tune debug info for dbx.
  };

  /// Enable abort calls when global instruction selection fails to lower/select
  /// an instruction.
  enum class GlobalISelAbortMode {
    Disable,        // Disable the abort.
    Enable,         // Enable the abort.
    DisableWithDiag // Disable the abort but emit a diagnostic on failure.
  };

  /// Indicates when and how the Swift async frame pointer bit should be set.
  enum class SwiftAsyncFramePointerMode {
    /// Determine whether to set the bit statically or dynamically based
    /// on the deployment target.
    DeploymentBased,
    /// Always set the bit.
    Always,
    /// Never set the bit.
    Never,
  };

  /// \brief Enumeration value for AMDGPU code object version, which is the
  /// code object version times 100.
  enum CodeObjectVersionKind {
    COV_None,
    COV_2 = 200, // Unsupported.
    COV_3 = 300, // Unsupported.
    COV_4 = 400,
    COV_5 = 500,
    COV_6 = 600,
  };

  class TargetOptions {
  public:
    TargetOptions()
        : UnsafeFPMath(false), NoInfsFPMath(false), NoNaNsFPMath(false),
          NoTrappingFPMath(true), NoSignedZerosFPMath(false),
          ApproxFuncFPMath(false), EnableAIXExtendedAltivecABI(false),
          HonorSignDependentRoundingFPMathOption(false), NoZerosInBSS(false),
          GuaranteedTailCallOpt(false), StackSymbolOrdering(true),
          EnableFastISel(false), EnableGlobalISel(false), UseInitArray(false),
          DisableIntegratedAS(false), FunctionSections(false),
          DataSections(false), IgnoreXCOFFVisibility(false),
          XCOFFTracebackTable(true), UniqueSectionNames(true),
          UniqueBasicBlockSectionNames(false), SeparateNamedSections(false),
          TrapUnreachable(false), NoTrapAfterNoreturn(false), TLSSize(0),
          EmulatedTLS(false), EnableTLSDESC(false), EnableIPRA(false),
          EmitStackSizeSection(false), EnableMachineOutliner(false),
          EnableMachineFunctionSplitter(false), SupportsDefaultOutlining(false),
          EmitAddrsig(false), BBAddrMap(false), EmitCallSiteInfo(false),
          SupportsDebugEntryValues(false), EnableDebugEntryValues(false),
          ValueTrackingVariableLocations(false), ForceDwarfFrameSection(false),
          XRayFunctionIndex(true), DebugStrictDwarf(false), Hotpatch(false),
          PPCGenScalarMASSEntries(false), JMCInstrument(false),
          EnableCFIFixup(false), MisExpect(false), XCOFFReadOnlyPointers(false),
          FPDenormalMode(DenormalMode::IEEE, DenormalMode::IEEE) {}

    /// DisableFramePointerElim - This returns true if frame pointer elimination
    /// optimization should be disabled for the given machine function.
    bool DisableFramePointerElim(const MachineFunction &MF) const;

    /// FramePointerIsReserved - This returns true if the frame pointer must
    /// always either point to a new frame record or be un-modified in the given
    /// function.
    bool FramePointerIsReserved(const MachineFunction &MF) const;

    /// If greater than 0, override the default value of
    /// MCAsmInfo::BinutilsVersion.
    std::pair<int, int> BinutilsVersion{0, 0};

    /// UnsafeFPMath - This flag is enabled when the
    /// -enable-unsafe-fp-math flag is specified on the command line.  When
    /// this flag is off (the default), the code generator is not allowed to
    /// produce results that are "less precise" than IEEE allows.  This includes
    /// use of X86 instructions like FSIN and FCOS instead of libcalls.
    unsigned UnsafeFPMath : 1;

    /// NoInfsFPMath - This flag is enabled when the
    /// -enable-no-infs-fp-math flag is specified on the command line. When
    /// this flag is off (the default), the code generator is not allowed to
    /// assume the FP arithmetic arguments and results are never +-Infs.
    unsigned NoInfsFPMath : 1;

    /// NoNaNsFPMath - This flag is enabled when the
    /// -enable-no-nans-fp-math flag is specified on the command line. When
    /// this flag is off (the default), the code generator is not allowed to
    /// assume the FP arithmetic arguments and results are never NaNs.
    unsigned NoNaNsFPMath : 1;

    /// NoTrappingFPMath - This flag is enabled when the
    /// -enable-no-trapping-fp-math is specified on the command line. This
    /// specifies that there are no trap handlers to handle exceptions.
    unsigned NoTrappingFPMath : 1;

    /// NoSignedZerosFPMath - This flag is enabled when the
    /// -enable-no-signed-zeros-fp-math is specified on the command line. This
    /// specifies that optimizations are allowed to treat the sign of a zero
    /// argument or result as insignificant.
    unsigned NoSignedZerosFPMath : 1;

    /// ApproxFuncFPMath - This flag is enabled when the
    /// -enable-approx-func-fp-math is specified on the command line. This
    /// specifies that optimizations are allowed to substitute math functions
    /// with approximate calculations
    unsigned ApproxFuncFPMath : 1;

    /// EnableAIXExtendedAltivecABI - This flag returns true when -vec-extabi is
    /// specified. The code generator is then able to use both volatile and
    /// nonvolitle vector registers. When false, the code generator only uses
    /// volatile vector registers which is the default setting on AIX.
    unsigned EnableAIXExtendedAltivecABI : 1;

    /// HonorSignDependentRoundingFPMath - This returns true when the
    /// -enable-sign-dependent-rounding-fp-math is specified.  If this returns
    /// false (the default), the code generator is allowed to assume that the
    /// rounding behavior is the default (round-to-zero for all floating point
    /// to integer conversions, and round-to-nearest for all other arithmetic
    /// truncations).  If this is enabled (set to true), the code generator must
    /// assume that the rounding mode may dynamically change.
    unsigned HonorSignDependentRoundingFPMathOption : 1;
    bool HonorSignDependentRoundingFPMath() const;

    /// NoZerosInBSS - By default some codegens place zero-initialized data to
    /// .bss section. This flag disables such behaviour (necessary, e.g. for
    /// crt*.o compiling).
    unsigned NoZerosInBSS : 1;

    /// GuaranteedTailCallOpt - This flag is enabled when -tailcallopt is
    /// specified on the commandline. When the flag is on, participating targets
    /// will perform tail call optimization on all calls which use the fastcc
    /// calling convention and which satisfy certain target-independent
    /// criteria (being at the end of a function, having the same return type
    /// as their parent function, etc.), using an alternate ABI if necessary.
    unsigned GuaranteedTailCallOpt : 1;

    /// StackSymbolOrdering - When true, this will allow CodeGen to order
    /// the local stack symbols (for code size, code locality, or any other
    /// heuristics). When false, the local symbols are left in whatever order
    /// they were generated. Default is true.
    unsigned StackSymbolOrdering : 1;

    /// EnableFastISel - This flag enables fast-path instruction selection
    /// which trades away generated code quality in favor of reducing
    /// compile time.
    unsigned EnableFastISel : 1;

    /// EnableGlobalISel - This flag enables global instruction selection.
    unsigned EnableGlobalISel : 1;

    /// EnableGlobalISelAbort - Control abort behaviour when global instruction
    /// selection fails to lower/select an instruction.
    GlobalISelAbortMode GlobalISelAbort = GlobalISelAbortMode::Enable;

    /// Control when and how the Swift async frame pointer bit should
    /// be set.
    SwiftAsyncFramePointerMode SwiftAsyncFramePointer =
        SwiftAsyncFramePointerMode::Always;

    /// UseInitArray - Use .init_array instead of .ctors for static
    /// constructors.
    unsigned UseInitArray : 1;

    /// Disable the integrated assembler.
    unsigned DisableIntegratedAS : 1;

    /// Emit functions into separate sections.
    unsigned FunctionSections : 1;

    /// Emit data into separate sections.
    unsigned DataSections : 1;

    /// Do not emit visibility attribute for xcoff.
    unsigned IgnoreXCOFFVisibility : 1;

    /// Emit XCOFF traceback table.
    unsigned XCOFFTracebackTable : 1;

    unsigned UniqueSectionNames : 1;

    /// Use unique names for basic block sections.
    unsigned UniqueBasicBlockSectionNames : 1;

    /// Emit named sections with the same name into different sections.
    unsigned SeparateNamedSections : 1;

    /// Emit target-specific trap instruction for 'unreachable' IR instructions.
    unsigned TrapUnreachable : 1;

    /// Do not emit a trap instruction for 'unreachable' IR instructions behind
    /// noreturn calls, even if TrapUnreachable is true.
    unsigned NoTrapAfterNoreturn : 1;

    /// Bit size of immediate TLS offsets (0 == use the default).
    unsigned TLSSize : 8;

    /// EmulatedTLS - This flag enables emulated TLS model, using emutls
    /// function in the runtime library..
    unsigned EmulatedTLS : 1;

    /// EnableTLSDESC - This flag enables TLS Descriptors.
    unsigned EnableTLSDESC : 1;

    /// This flag enables InterProcedural Register Allocation (IPRA).
    unsigned EnableIPRA : 1;

    /// Emit section containing metadata on function stack sizes.
    unsigned EmitStackSizeSection : 1;

    /// Enables the MachineOutliner pass.
    unsigned EnableMachineOutliner : 1;

    /// Enables the MachineFunctionSplitter pass.
    unsigned EnableMachineFunctionSplitter : 1;

    /// Set if the target supports default outlining behaviour.
    unsigned SupportsDefaultOutlining : 1;

    /// Emit address-significance table.
    unsigned EmitAddrsig : 1;

    // Emit the SHT_LLVM_BB_ADDR_MAP section containing basic block address
    // which can be used to map virtual addresses to machine basic blocks.
    unsigned BBAddrMap : 1;

    /// Emit basic blocks into separate sections.
    BasicBlockSection BBSections = BasicBlockSection::None;

    /// Memory Buffer that contains information on sampled basic blocks and used
    /// to selectively generate basic block sections.
    std::shared_ptr<MemoryBuffer> BBSectionsFuncListBuf;

    /// The flag enables call site info production. It is used only for debug
    /// info, and it is restricted only to optimized code. This can be used for
    /// something else, so that should be controlled in the frontend.
    unsigned EmitCallSiteInfo : 1;
    /// Set if the target supports the debug entry values by default.
    unsigned SupportsDebugEntryValues : 1;
    /// When set to true, the EnableDebugEntryValues option forces production
    /// of debug entry values even if the target does not officially support
    /// it. Useful for testing purposes only. This flag should never be checked
    /// directly, always use \ref ShouldEmitDebugEntryValues instead.
     unsigned EnableDebugEntryValues : 1;
    /// NOTE: There are targets that still do not support the debug entry values
    /// production.
    bool ShouldEmitDebugEntryValues() const;

    // When set to true, use experimental new debug variable location tracking,
    // which seeks to follow the values of variables rather than their location,
    // post isel.
    unsigned ValueTrackingVariableLocations : 1;

    /// Emit DWARF debug frame section.
    unsigned ForceDwarfFrameSection : 1;

    /// Emit XRay Function Index section
    unsigned XRayFunctionIndex : 1;

    /// When set to true, don't use DWARF extensions in later DWARF versions.
    /// By default, it is set to false.
    unsigned DebugStrictDwarf : 1;

    /// Emit the hotpatch flag in CodeView debug.
    unsigned Hotpatch : 1;

    /// Enables scalar MASS conversions
    unsigned PPCGenScalarMASSEntries : 1;

    /// Enable JustMyCode instrumentation.
    unsigned JMCInstrument : 1;

    /// Enable the CFIFixup pass.
    unsigned EnableCFIFixup : 1;

    /// When set to true, enable MisExpect Diagnostics
    /// By default, it is set to false
    unsigned MisExpect : 1;

    /// When set to true, const objects with relocatable address values are put
    /// into the RO data section.
    unsigned XCOFFReadOnlyPointers : 1;

    /// Name of the stack usage file (i.e., .su file) if user passes
    /// -fstack-usage. If empty, it can be implied that -fstack-usage is not
    /// passed on the command line.
    std::string StackUsageOutput;

    /// If greater than 0, override TargetLoweringBase::PrefLoopAlignment.
    unsigned LoopAlignment = 0;

    /// FloatABIType - This setting is set by -float-abi=xxx option is specfied
    /// on the command line. This setting may either be Default, Soft, or Hard.
    /// Default selects the target's default behavior. Soft selects the ABI for
    /// software floating point, but does not indicate that FP hardware may not
    /// be used. Such a combination is unfortunately popular (e.g.
    /// arm-apple-darwin). Hard presumes that the normal FP ABI is used.
    FloatABI::ABIType FloatABIType = FloatABI::Default;

    /// AllowFPOpFusion - This flag is set by the -fp-contract=xxx option.
    /// This controls the creation of fused FP ops that store intermediate
    /// results in higher precision than IEEE allows (E.g. FMAs).
    ///
    /// Fast mode - allows formation of fused FP ops whenever they're
    /// profitable.
    /// Standard mode - allow fusion only for 'blessed' FP ops. At present the
    /// only blessed op is the fmuladd intrinsic. In the future more blessed ops
    /// may be added.
    /// Strict mode - allow fusion only if/when it can be proven that the excess
    /// precision won't effect the result.
    ///
    /// Note: This option only controls formation of fused ops by the
    /// optimizers.  Fused operations that are explicitly specified (e.g. FMA
    /// via the llvm.fma.* intrinsic) will always be honored, regardless of
    /// the value of this option.
    FPOpFusion::FPOpFusionMode AllowFPOpFusion = FPOpFusion::Standard;

    /// ThreadModel - This flag specifies the type of threading model to assume
    /// for things like atomics
    ThreadModel::Model ThreadModel = ThreadModel::POSIX;

    /// EABIVersion - This flag specifies the EABI version
    EABI EABIVersion = EABI::Default;

    /// Which debugger to tune for.
    DebuggerKind DebuggerTuning = DebuggerKind::Default;

  private:
    /// Flushing mode to assume in default FP environment.
    DenormalMode FPDenormalMode;

    /// Flushing mode to assume in default FP environment, for float/vector of
    /// float.
    DenormalMode FP32DenormalMode;

  public:
    void setFPDenormalMode(DenormalMode Mode) {
      FPDenormalMode = Mode;
    }

    void setFP32DenormalMode(DenormalMode Mode) {
      FP32DenormalMode = Mode;
    }

    DenormalMode getRawFPDenormalMode() const {
      return FPDenormalMode;
    }

    DenormalMode getRawFP32DenormalMode() const {
      return FP32DenormalMode;
    }

    DenormalMode getDenormalMode(const fltSemantics &FPType) const;

    /// What exception model to use
    ExceptionHandling ExceptionModel = ExceptionHandling::None;

    /// Machine level options.
    MCTargetOptions MCOptions;

    /// Stores the filename/path of the final .o/.obj file, to be written in the
    /// debug information. This is used for emitting the CodeView S_OBJNAME
    /// record.
    std::string ObjectFilenameForDebug;
  };

} // End llvm namespace

#endif
