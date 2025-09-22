//===- llvm/IR/DiagnosticInfo.h - Diagnostic Declaration --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the different classes involved in low level diagnostics.
//
// Diagnostics reporting is still done as part of the LLVMContext.
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DIAGNOSTICINFO_H
#define LLVM_IR_DIAGNOSTICINFO_H

#include "llvm-c/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TypeSize.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string>

namespace llvm {

// Forward declarations.
class DiagnosticPrinter;
class DIFile;
class DISubprogram;
class CallInst;
class Function;
class Instruction;
class InstructionCost;
class Module;
class Type;
class Value;

/// Defines the different supported severity of a diagnostic.
enum DiagnosticSeverity : char {
  DS_Error,
  DS_Warning,
  DS_Remark,
  // A note attaches additional information to one of the previous diagnostic
  // types.
  DS_Note
};

/// Defines the different supported kind of a diagnostic.
/// This enum should be extended with a new ID for each added concrete subclass.
enum DiagnosticKind {
  DK_InlineAsm,
  DK_ResourceLimit,
  DK_StackSize,
  DK_Linker,
  DK_Lowering,
  DK_DebugMetadataVersion,
  DK_DebugMetadataInvalid,
  DK_ISelFallback,
  DK_SampleProfile,
  DK_OptimizationRemark,
  DK_OptimizationRemarkMissed,
  DK_OptimizationRemarkAnalysis,
  DK_OptimizationRemarkAnalysisFPCommute,
  DK_OptimizationRemarkAnalysisAliasing,
  DK_OptimizationFailure,
  DK_FirstRemark = DK_OptimizationRemark,
  DK_LastRemark = DK_OptimizationFailure,
  DK_MachineOptimizationRemark,
  DK_MachineOptimizationRemarkMissed,
  DK_MachineOptimizationRemarkAnalysis,
  DK_FirstMachineRemark = DK_MachineOptimizationRemark,
  DK_LastMachineRemark = DK_MachineOptimizationRemarkAnalysis,
  DK_MIRParser,
  DK_PGOProfile,
  DK_Unsupported,
  DK_SrcMgr,
  DK_DontCall,
  DK_MisExpect,
  DK_FirstPluginKind // Must be last value to work with
                     // getNextAvailablePluginDiagnosticKind
};

/// Get the next available kind ID for a plugin diagnostic.
/// Each time this function is called, it returns a different number.
/// Therefore, a plugin that wants to "identify" its own classes
/// with a dynamic identifier, just have to use this method to get a new ID
/// and assign it to each of its classes.
/// The returned ID will be greater than or equal to DK_FirstPluginKind.
/// Thus, the plugin identifiers will not conflict with the
/// DiagnosticKind values.
int getNextAvailablePluginDiagnosticKind();

/// This is the base abstract class for diagnostic reporting in
/// the backend.
/// The print method must be overloaded by the subclasses to print a
/// user-friendly message in the client of the backend (let us call it a
/// frontend).
class DiagnosticInfo {
private:
  /// Kind defines the kind of report this is about.
  const /* DiagnosticKind */ int Kind;
  /// Severity gives the severity of the diagnostic.
  const DiagnosticSeverity Severity;

  virtual void anchor();
public:
  DiagnosticInfo(/* DiagnosticKind */ int Kind, DiagnosticSeverity Severity)
      : Kind(Kind), Severity(Severity) {}

  virtual ~DiagnosticInfo() = default;

  /* DiagnosticKind */ int getKind() const { return Kind; }
  DiagnosticSeverity getSeverity() const { return Severity; }

  /// Print using the given \p DP a user-friendly message.
  /// This is the default message that will be printed to the user.
  /// It is used when the frontend does not directly take advantage
  /// of the information contained in fields of the subclasses.
  /// The printed message must not end with '.' nor start with a severity
  /// keyword.
  virtual void print(DiagnosticPrinter &DP) const = 0;
};

using DiagnosticHandlerFunction = std::function<void(const DiagnosticInfo &)>;

/// Diagnostic information for inline asm reporting.
/// This is basically a message and an optional location.
class DiagnosticInfoInlineAsm : public DiagnosticInfo {
private:
  /// Optional line information. 0 if not set.
  uint64_t LocCookie = 0;
  /// Message to be reported.
  const Twine &MsgStr;
  /// Optional origin of the problem.
  const Instruction *Instr = nullptr;

public:
  /// \p MsgStr is the message to be reported to the frontend.
  /// This class does not copy \p MsgStr, therefore the reference must be valid
  /// for the whole life time of the Diagnostic.
  DiagnosticInfoInlineAsm(const Twine &MsgStr,
                          DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_InlineAsm, Severity), MsgStr(MsgStr) {}

  /// \p LocCookie if non-zero gives the line number for this report.
  /// \p MsgStr gives the message.
  /// This class does not copy \p MsgStr, therefore the reference must be valid
  /// for the whole life time of the Diagnostic.
  DiagnosticInfoInlineAsm(uint64_t LocCookie, const Twine &MsgStr,
                          DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_InlineAsm, Severity), LocCookie(LocCookie),
        MsgStr(MsgStr) {}

  /// \p Instr gives the original instruction that triggered the diagnostic.
  /// \p MsgStr gives the message.
  /// This class does not copy \p MsgStr, therefore the reference must be valid
  /// for the whole life time of the Diagnostic.
  /// Same for \p I.
  DiagnosticInfoInlineAsm(const Instruction &I, const Twine &MsgStr,
                          DiagnosticSeverity Severity = DS_Error);

  uint64_t getLocCookie() const { return LocCookie; }
  const Twine &getMsgStr() const { return MsgStr; }
  const Instruction *getInstruction() const { return Instr; }

  /// \see DiagnosticInfo::print.
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_InlineAsm;
  }
};

/// Diagnostic information for debug metadata version reporting.
/// This is basically a module and a version.
class DiagnosticInfoDebugMetadataVersion : public DiagnosticInfo {
private:
  /// The module that is concerned by this debug metadata version diagnostic.
  const Module &M;
  /// The actual metadata version.
  unsigned MetadataVersion;

public:
  /// \p The module that is concerned by this debug metadata version diagnostic.
  /// \p The actual metadata version.
  DiagnosticInfoDebugMetadataVersion(const Module &M, unsigned MetadataVersion,
                                     DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_DebugMetadataVersion, Severity), M(M),
        MetadataVersion(MetadataVersion) {}

  const Module &getModule() const { return M; }
  unsigned getMetadataVersion() const { return MetadataVersion; }

  /// \see DiagnosticInfo::print.
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_DebugMetadataVersion;
  }
};

/// Diagnostic information for stripping invalid debug metadata.
class DiagnosticInfoIgnoringInvalidDebugMetadata : public DiagnosticInfo {
private:
  /// The module that is concerned by this debug metadata version diagnostic.
  const Module &M;

public:
  /// \p The module that is concerned by this debug metadata version diagnostic.
  DiagnosticInfoIgnoringInvalidDebugMetadata(
      const Module &M, DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_DebugMetadataVersion, Severity), M(M) {}

  const Module &getModule() const { return M; }

  /// \see DiagnosticInfo::print.
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_DebugMetadataInvalid;
  }
};

/// Diagnostic information for the sample profiler.
class DiagnosticInfoSampleProfile : public DiagnosticInfo {
public:
  DiagnosticInfoSampleProfile(StringRef FileName, unsigned LineNum,
                              const Twine &Msg,
                              DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_SampleProfile, Severity), FileName(FileName),
        LineNum(LineNum), Msg(Msg) {}
  DiagnosticInfoSampleProfile(StringRef FileName, const Twine &Msg,
                              DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_SampleProfile, Severity), FileName(FileName),
        Msg(Msg) {}
  DiagnosticInfoSampleProfile(const Twine &Msg,
                              DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_SampleProfile, Severity), Msg(Msg) {}

  /// \see DiagnosticInfo::print.
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_SampleProfile;
  }

  StringRef getFileName() const { return FileName; }
  unsigned getLineNum() const { return LineNum; }
  const Twine &getMsg() const { return Msg; }

private:
  /// Name of the input file associated with this diagnostic.
  StringRef FileName;

  /// Line number where the diagnostic occurred. If 0, no line number will
  /// be emitted in the message.
  unsigned LineNum = 0;

  /// Message to report.
  const Twine &Msg;
};

/// Diagnostic information for the PGO profiler.
class DiagnosticInfoPGOProfile : public DiagnosticInfo {
public:
  DiagnosticInfoPGOProfile(const char *FileName, const Twine &Msg,
                           DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_PGOProfile, Severity), FileName(FileName), Msg(Msg) {}

  /// \see DiagnosticInfo::print.
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_PGOProfile;
  }

  const char *getFileName() const { return FileName; }
  const Twine &getMsg() const { return Msg; }

private:
  /// Name of the input file associated with this diagnostic.
  const char *FileName;

  /// Message to report.
  const Twine &Msg;
};

class DiagnosticLocation {
  DIFile *File = nullptr;
  unsigned Line = 0;
  unsigned Column = 0;

public:
  DiagnosticLocation() = default;
  DiagnosticLocation(const DebugLoc &DL);
  DiagnosticLocation(const DISubprogram *SP);

  bool isValid() const { return File; }
  /// Return the full path to the file.
  std::string getAbsolutePath() const;
  /// Return the file name relative to the compilation directory.
  StringRef getRelativePath() const;
  unsigned getLine() const { return Line; }
  unsigned getColumn() const { return Column; }
};

/// Common features for diagnostics with an associated location.
class DiagnosticInfoWithLocationBase : public DiagnosticInfo {
  void anchor() override;
public:
  /// \p Fn is the function where the diagnostic is being emitted. \p Loc is
  /// the location information to use in the diagnostic.
  DiagnosticInfoWithLocationBase(enum DiagnosticKind Kind,
                                 enum DiagnosticSeverity Severity,
                                 const Function &Fn,
                                 const DiagnosticLocation &Loc)
      : DiagnosticInfo(Kind, Severity), Fn(Fn), Loc(Loc) {}

  /// Return true if location information is available for this diagnostic.
  bool isLocationAvailable() const { return Loc.isValid(); }

  /// Return a string with the location information for this diagnostic
  /// in the format "file:line:col". If location information is not available,
  /// it returns "<unknown>:0:0".
  std::string getLocationStr() const;

  /// Return location information for this diagnostic in three parts:
  /// the relative source file path, line number and column.
  void getLocation(StringRef &RelativePath, unsigned &Line,
                   unsigned &Column) const;

  /// Return the absolute path tot the file.
  std::string getAbsolutePath() const;
  
  const Function &getFunction() const { return Fn; }
  DiagnosticLocation getLocation() const { return Loc; }

private:
  /// Function where this diagnostic is triggered.
  const Function &Fn;

  /// Debug location where this diagnostic is triggered.
  DiagnosticLocation Loc;
};

/// Diagnostic information for stack size etc. reporting.
/// This is basically a function and a size.
class DiagnosticInfoResourceLimit : public DiagnosticInfoWithLocationBase {
private:
  /// The function that is concerned by this resource limit diagnostic.
  const Function &Fn;

  /// Description of the resource type (e.g. stack size)
  const char *ResourceName;

  /// The computed size usage
  uint64_t ResourceSize;

  // Threshould passed
  uint64_t ResourceLimit;

public:
  /// \p The function that is concerned by this stack size diagnostic.
  /// \p The computed stack size.
  DiagnosticInfoResourceLimit(const Function &Fn, const char *ResourceName,
                              uint64_t ResourceSize, uint64_t ResourceLimit,
                              DiagnosticSeverity Severity = DS_Warning,
                              DiagnosticKind Kind = DK_ResourceLimit);

  const Function &getFunction() const { return Fn; }
  const char *getResourceName() const { return ResourceName; }
  uint64_t getResourceSize() const { return ResourceSize; }
  uint64_t getResourceLimit() const { return ResourceLimit; }

  /// \see DiagnosticInfo::print.
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_ResourceLimit || DI->getKind() == DK_StackSize;
  }
};

class DiagnosticInfoStackSize : public DiagnosticInfoResourceLimit {
  void anchor() override;

public:
  DiagnosticInfoStackSize(const Function &Fn, uint64_t StackSize,
                          uint64_t StackLimit,
                          DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfoResourceLimit(Fn, "stack frame size", StackSize,
                                    StackLimit, Severity, DK_StackSize) {}

  uint64_t getStackSize() const { return getResourceSize(); }
  uint64_t getStackLimit() const { return getResourceLimit(); }

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_StackSize;
  }
};

/// Common features for diagnostics dealing with optimization remarks
/// that are used by both IR and MIR passes.
class DiagnosticInfoOptimizationBase : public DiagnosticInfoWithLocationBase {
public:
  /// Used to set IsVerbose via the stream interface.
  struct setIsVerbose {};

  /// When an instance of this is inserted into the stream, the arguments
  /// following will not appear in the remark printed in the compiler output
  /// (-Rpass) but only in the optimization record file
  /// (-fsave-optimization-record).
  struct setExtraArgs {};

  /// Used in the streaming interface as the general argument type.  It
  /// internally converts everything into a key-value pair.
  struct Argument {
    std::string Key;
    std::string Val;
    // If set, the debug location corresponding to the value.
    DiagnosticLocation Loc;

    explicit Argument(StringRef Str = "") : Key("String"), Val(Str) {}
    Argument(StringRef Key, const Value *V);
    Argument(StringRef Key, const Type *T);
    Argument(StringRef Key, StringRef S);
    Argument(StringRef Key, const char *S) : Argument(Key, StringRef(S)) {};
    Argument(StringRef Key, int N);
    Argument(StringRef Key, float N);
    Argument(StringRef Key, long N);
    Argument(StringRef Key, long long N);
    Argument(StringRef Key, unsigned N);
    Argument(StringRef Key, unsigned long N);
    Argument(StringRef Key, unsigned long long N);
    Argument(StringRef Key, ElementCount EC);
    Argument(StringRef Key, bool B) : Key(Key), Val(B ? "true" : "false") {}
    Argument(StringRef Key, DebugLoc dl);
    Argument(StringRef Key, InstructionCost C);
  };

  /// \p PassName is the name of the pass emitting this diagnostic. \p
  /// RemarkName is a textual identifier for the remark (single-word,
  /// camel-case). \p Fn is the function where the diagnostic is being emitted.
  /// \p Loc is the location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location.
  DiagnosticInfoOptimizationBase(enum DiagnosticKind Kind,
                                 enum DiagnosticSeverity Severity,
                                 const char *PassName, StringRef RemarkName,
                                 const Function &Fn,
                                 const DiagnosticLocation &Loc)
      : DiagnosticInfoWithLocationBase(Kind, Severity, Fn, Loc),
        PassName(PassName), RemarkName(RemarkName) {}

  void insert(StringRef S);
  void insert(Argument A);
  void insert(setIsVerbose V);
  void insert(setExtraArgs EA);

  /// \see DiagnosticInfo::print.
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if this optimization remark is enabled by one of
  /// of the LLVM command line flags (-pass-remarks, -pass-remarks-missed,
  /// or -pass-remarks-analysis). Note that this only handles the LLVM
  /// flags. We cannot access Clang flags from here (they are handled
  /// in BackendConsumer::OptimizationRemarkHandler).
  virtual bool isEnabled() const = 0;

  StringRef getPassName() const { return PassName; }
  StringRef getRemarkName() const { return RemarkName; }
  std::string getMsg() const;
  std::optional<uint64_t> getHotness() const { return Hotness; }
  void setHotness(std::optional<uint64_t> H) { Hotness = H; }

  bool isVerbose() const { return IsVerbose; }

  ArrayRef<Argument> getArgs() const { return Args; }

  static bool classof(const DiagnosticInfo *DI) {
    return (DI->getKind() >= DK_FirstRemark &&
            DI->getKind() <= DK_LastRemark) ||
           (DI->getKind() >= DK_FirstMachineRemark &&
            DI->getKind() <= DK_LastMachineRemark);
  }

  bool isPassed() const {
    return (getKind() == DK_OptimizationRemark ||
            getKind() == DK_MachineOptimizationRemark);
  }

  bool isMissed() const {
    return (getKind() == DK_OptimizationRemarkMissed ||
            getKind() == DK_MachineOptimizationRemarkMissed);
  }

  bool isAnalysis() const {
    return (getKind() == DK_OptimizationRemarkAnalysis ||
            getKind() == DK_MachineOptimizationRemarkAnalysis);
  }

protected:
  /// Name of the pass that triggers this report. If this matches the
  /// regular expression given in -Rpass=regexp, then the remark will
  /// be emitted.
  const char *PassName;

  /// Textual identifier for the remark (single-word, camel-case). Can be used
  /// by external tools reading the output file for optimization remarks to
  /// identify the remark.
  StringRef RemarkName;

  /// If profile information is available, this is the number of times the
  /// corresponding code was executed in a profile instrumentation run.
  std::optional<uint64_t> Hotness;

  /// Arguments collected via the streaming interface.
  SmallVector<Argument, 4> Args;

  /// The remark is expected to be noisy.
  bool IsVerbose = false;

  /// If positive, the index of the first argument that only appear in
  /// the optimization records and not in the remark printed in the compiler
  /// output.
  int FirstExtraArgIndex = -1;
};

/// Allow the insertion operator to return the actual remark type rather than a
/// common base class.  This allows returning the result of the insertion
/// directly by value, e.g. return OptimizationRemarkAnalysis(...) << "blah".
template <class RemarkT>
RemarkT &
operator<<(RemarkT &R,
           std::enable_if_t<
               std::is_base_of<DiagnosticInfoOptimizationBase, RemarkT>::value,
               StringRef>
               S) {
  R.insert(S);
  return R;
}

/// Also allow r-value for the remark to allow insertion into a
/// temporarily-constructed remark.
template <class RemarkT>
RemarkT &
operator<<(RemarkT &&R,
           std::enable_if_t<
               std::is_base_of<DiagnosticInfoOptimizationBase, RemarkT>::value,
               StringRef>
               S) {
  R.insert(S);
  return R;
}

template <class RemarkT>
RemarkT &
operator<<(RemarkT &R,
           std::enable_if_t<
               std::is_base_of<DiagnosticInfoOptimizationBase, RemarkT>::value,
               DiagnosticInfoOptimizationBase::Argument>
               A) {
  R.insert(A);
  return R;
}

template <class RemarkT>
RemarkT &
operator<<(RemarkT &&R,
           std::enable_if_t<
               std::is_base_of<DiagnosticInfoOptimizationBase, RemarkT>::value,
               DiagnosticInfoOptimizationBase::Argument>
               A) {
  R.insert(A);
  return R;
}

template <class RemarkT>
RemarkT &
operator<<(RemarkT &R,
           std::enable_if_t<
               std::is_base_of<DiagnosticInfoOptimizationBase, RemarkT>::value,
               DiagnosticInfoOptimizationBase::setIsVerbose>
               V) {
  R.insert(V);
  return R;
}

template <class RemarkT>
RemarkT &
operator<<(RemarkT &&R,
           std::enable_if_t<
               std::is_base_of<DiagnosticInfoOptimizationBase, RemarkT>::value,
               DiagnosticInfoOptimizationBase::setIsVerbose>
               V) {
  R.insert(V);
  return R;
}

template <class RemarkT>
RemarkT &
operator<<(RemarkT &R,
           std::enable_if_t<
               std::is_base_of<DiagnosticInfoOptimizationBase, RemarkT>::value,
               DiagnosticInfoOptimizationBase::setExtraArgs>
               EA) {
  R.insert(EA);
  return R;
}

/// Common features for diagnostics dealing with optimization remarks
/// that are used by IR passes.
class DiagnosticInfoIROptimization : public DiagnosticInfoOptimizationBase {
  void anchor() override;
public:
  /// \p PassName is the name of the pass emitting this diagnostic. \p
  /// RemarkName is a textual identifier for the remark (single-word,
  /// camel-case). \p Fn is the function where the diagnostic is being emitted.
  /// \p Loc is the location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location. \p CodeRegion is IR value (currently basic block) that the
  /// optimization operates on. This is currently used to provide run-time
  /// hotness information with PGO.
  DiagnosticInfoIROptimization(enum DiagnosticKind Kind,
                               enum DiagnosticSeverity Severity,
                               const char *PassName, StringRef RemarkName,
                               const Function &Fn,
                               const DiagnosticLocation &Loc,
                               const Value *CodeRegion = nullptr)
      : DiagnosticInfoOptimizationBase(Kind, Severity, PassName, RemarkName, Fn,
                                       Loc),
        CodeRegion(CodeRegion) {}

  /// This is ctor variant allows a pass to build an optimization remark
  /// from an existing remark.
  ///
  /// This is useful when a transformation pass (e.g LV) wants to emit a remark
  /// (\p Orig) generated by one of its analyses (e.g. LAA) as its own analysis
  /// remark.  The string \p Prepend will be emitted before the original
  /// message.
  DiagnosticInfoIROptimization(const char *PassName, StringRef Prepend,
                               const DiagnosticInfoIROptimization &Orig)
      : DiagnosticInfoOptimizationBase(
            (DiagnosticKind)Orig.getKind(), Orig.getSeverity(), PassName,
            Orig.RemarkName, Orig.getFunction(), Orig.getLocation()),
        CodeRegion(Orig.getCodeRegion()) {
    *this << Prepend;
    std::copy(Orig.Args.begin(), Orig.Args.end(), std::back_inserter(Args));
  }

  /// Legacy interface.
  /// \p PassName is the name of the pass emitting this diagnostic.
  /// \p Fn is the function where the diagnostic is being emitted. \p Loc is
  /// the location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location. \p Msg is the message to show. Note that this class does not
  /// copy this message, so this reference must be valid for the whole life time
  /// of the diagnostic.
  DiagnosticInfoIROptimization(enum DiagnosticKind Kind,
                               enum DiagnosticSeverity Severity,
                               const char *PassName, const Function &Fn,
                               const DiagnosticLocation &Loc, const Twine &Msg)
      : DiagnosticInfoOptimizationBase(Kind, Severity, PassName, "", Fn, Loc) {
    *this << Msg.str();
  }

  const Value *getCodeRegion() const { return CodeRegion; }

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() >= DK_FirstRemark && DI->getKind() <= DK_LastRemark;
  }

private:
  /// The IR value (currently basic block) that the optimization operates on.
  /// This is currently used to provide run-time hotness information with PGO.
  const Value *CodeRegion = nullptr;
};

/// Diagnostic information for applied optimization remarks.
class OptimizationRemark : public DiagnosticInfoIROptimization {
public:
  /// \p PassName is the name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass=, then the diagnostic will
  /// be emitted. \p RemarkName is a textual identifier for the remark (single-
  /// word, camel-case). \p Loc is the debug location and \p CodeRegion is the
  /// region that the optimization operates on (currently only block is
  /// supported).
  OptimizationRemark(const char *PassName, StringRef RemarkName,
                     const DiagnosticLocation &Loc, const Value *CodeRegion);

  /// Same as above, but the debug location and code region are derived from \p
  /// Instr.
  OptimizationRemark(const char *PassName, StringRef RemarkName,
                     const Instruction *Inst);

  /// Same as above, but the debug location and code region are derived from \p
  /// Func.
  OptimizationRemark(const char *PassName, StringRef RemarkName,
                     const Function *Func);

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemark;
  }

  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  bool isEnabled() const override;

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass=, then the
  /// diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic
  /// will include the source code location. \p Msg is the message to show.
  /// Note that this class does not copy this message, so this reference
  /// must be valid for the whole life time of the diagnostic.
  OptimizationRemark(const char *PassName, const Function &Fn,
                     const DiagnosticLocation &Loc, const Twine &Msg)
      : DiagnosticInfoIROptimization(DK_OptimizationRemark, DS_Remark, PassName,
                                     Fn, Loc, Msg) {}
};

/// Diagnostic information for missed-optimization remarks.
class OptimizationRemarkMissed : public DiagnosticInfoIROptimization {
public:
  /// \p PassName is the name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-missed=, then the
  /// diagnostic will be emitted. \p RemarkName is a textual identifier for the
  /// remark (single-word, camel-case). \p Loc is the debug location and \p
  /// CodeRegion is the region that the optimization operates on (currently only
  /// block is supported).
  OptimizationRemarkMissed(const char *PassName, StringRef RemarkName,
                           const DiagnosticLocation &Loc,
                           const Value *CodeRegion);

  /// Same as above but \p Inst is used to derive code region and debug
  /// location.
  OptimizationRemarkMissed(const char *PassName, StringRef RemarkName,
                           const Instruction *Inst);

  /// Same as above but \p F is used to derive code region and debug
  /// location.
  OptimizationRemarkMissed(const char *PassName, StringRef RemarkName,
                           const Function *F);

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemarkMissed;
  }

  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  bool isEnabled() const override;

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass-missed=, then the
  /// diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic
  /// will include the source code location. \p Msg is the message to show.
  /// Note that this class does not copy this message, so this reference
  /// must be valid for the whole life time of the diagnostic.
  OptimizationRemarkMissed(const char *PassName, const Function &Fn,
                           const DiagnosticLocation &Loc, const Twine &Msg)
      : DiagnosticInfoIROptimization(DK_OptimizationRemarkMissed, DS_Remark,
                                     PassName, Fn, Loc, Msg) {}
};

/// Diagnostic information for optimization analysis remarks.
class OptimizationRemarkAnalysis : public DiagnosticInfoIROptimization {
public:
  /// \p PassName is the name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-analysis=, then the
  /// diagnostic will be emitted. \p RemarkName is a textual identifier for the
  /// remark (single-word, camel-case). \p Loc is the debug location and \p
  /// CodeRegion is the region that the optimization operates on (currently only
  /// block is supported).
  OptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                             const DiagnosticLocation &Loc,
                             const Value *CodeRegion);

  /// This is ctor variant allows a pass to build an optimization remark
  /// from an existing remark.
  ///
  /// This is useful when a transformation pass (e.g LV) wants to emit a remark
  /// (\p Orig) generated by one of its analyses (e.g. LAA) as its own analysis
  /// remark.  The string \p Prepend will be emitted before the original
  /// message.
  OptimizationRemarkAnalysis(const char *PassName, StringRef Prepend,
                             const OptimizationRemarkAnalysis &Orig)
      : DiagnosticInfoIROptimization(PassName, Prepend, Orig) {}

  /// Same as above but \p Inst is used to derive code region and debug
  /// location.
  OptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                             const Instruction *Inst);

  /// Same as above but \p F is used to derive code region and debug
  /// location.
  OptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                             const Function *F);

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemarkAnalysis;
  }

  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  bool isEnabled() const override;

  static const char *AlwaysPrint;

  bool shouldAlwaysPrint() const { return getPassName() == AlwaysPrint; }

protected:
  OptimizationRemarkAnalysis(enum DiagnosticKind Kind, const char *PassName,
                             const Function &Fn, const DiagnosticLocation &Loc,
                             const Twine &Msg)
      : DiagnosticInfoIROptimization(Kind, DS_Remark, PassName, Fn, Loc, Msg) {}

  OptimizationRemarkAnalysis(enum DiagnosticKind Kind, const char *PassName,
                             StringRef RemarkName,
                             const DiagnosticLocation &Loc,
                             const Value *CodeRegion);

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass-analysis=, then
  /// the diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic will
  /// include the source code location. \p Msg is the message to show. Note that
  /// this class does not copy this message, so this reference must be valid for
  /// the whole life time of the diagnostic.
  OptimizationRemarkAnalysis(const char *PassName, const Function &Fn,
                             const DiagnosticLocation &Loc, const Twine &Msg)
      : DiagnosticInfoIROptimization(DK_OptimizationRemarkAnalysis, DS_Remark,
                                     PassName, Fn, Loc, Msg) {}
};

/// Diagnostic information for optimization analysis remarks related to
/// floating-point non-commutativity.
class OptimizationRemarkAnalysisFPCommute : public OptimizationRemarkAnalysis {
  void anchor() override;
public:
  /// \p PassName is the name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-analysis=, then the
  /// diagnostic will be emitted. \p RemarkName is a textual identifier for the
  /// remark (single-word, camel-case). \p Loc is the debug location and \p
  /// CodeRegion is the region that the optimization operates on (currently only
  /// block is supported). The front-end will append its own message related to
  /// options that address floating-point non-commutativity.
  OptimizationRemarkAnalysisFPCommute(const char *PassName,
                                      StringRef RemarkName,
                                      const DiagnosticLocation &Loc,
                                      const Value *CodeRegion)
      : OptimizationRemarkAnalysis(DK_OptimizationRemarkAnalysisFPCommute,
                                   PassName, RemarkName, Loc, CodeRegion) {}

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemarkAnalysisFPCommute;
  }

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass-analysis=, then
  /// the diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic will
  /// include the source code location. \p Msg is the message to show. The
  /// front-end will append its own message related to options that address
  /// floating-point non-commutativity. Note that this class does not copy this
  /// message, so this reference must be valid for the whole life time of the
  /// diagnostic.
  OptimizationRemarkAnalysisFPCommute(const char *PassName, const Function &Fn,
                                      const DiagnosticLocation &Loc,
                                      const Twine &Msg)
      : OptimizationRemarkAnalysis(DK_OptimizationRemarkAnalysisFPCommute,
                                   PassName, Fn, Loc, Msg) {}
};

/// Diagnostic information for optimization analysis remarks related to
/// pointer aliasing.
class OptimizationRemarkAnalysisAliasing : public OptimizationRemarkAnalysis {
  void anchor() override;
public:
  /// \p PassName is the name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-analysis=, then the
  /// diagnostic will be emitted. \p RemarkName is a textual identifier for the
  /// remark (single-word, camel-case). \p Loc is the debug location and \p
  /// CodeRegion is the region that the optimization operates on (currently only
  /// block is supported). The front-end will append its own message related to
  /// options that address pointer aliasing legality.
  OptimizationRemarkAnalysisAliasing(const char *PassName, StringRef RemarkName,
                                     const DiagnosticLocation &Loc,
                                     const Value *CodeRegion)
      : OptimizationRemarkAnalysis(DK_OptimizationRemarkAnalysisAliasing,
                                   PassName, RemarkName, Loc, CodeRegion) {}

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemarkAnalysisAliasing;
  }

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass-analysis=, then
  /// the diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic will
  /// include the source code location. \p Msg is the message to show. The
  /// front-end will append its own message related to options that address
  /// pointer aliasing legality. Note that this class does not copy this
  /// message, so this reference must be valid for the whole life time of the
  /// diagnostic.
  OptimizationRemarkAnalysisAliasing(const char *PassName, const Function &Fn,
                                     const DiagnosticLocation &Loc,
                                     const Twine &Msg)
      : OptimizationRemarkAnalysis(DK_OptimizationRemarkAnalysisAliasing,
                                   PassName, Fn, Loc, Msg) {}
};

/// Diagnostic information for machine IR parser.
// FIXME: Remove this, use DiagnosticInfoSrcMgr instead.
class DiagnosticInfoMIRParser : public DiagnosticInfo {
  const SMDiagnostic &Diagnostic;

public:
  DiagnosticInfoMIRParser(DiagnosticSeverity Severity,
                          const SMDiagnostic &Diagnostic)
      : DiagnosticInfo(DK_MIRParser, Severity), Diagnostic(Diagnostic) {}

  const SMDiagnostic &getDiagnostic() const { return Diagnostic; }

  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MIRParser;
  }
};

/// Diagnostic information for ISel fallback path.
class DiagnosticInfoISelFallback : public DiagnosticInfo {
  /// The function that is concerned by this diagnostic.
  const Function &Fn;

public:
  DiagnosticInfoISelFallback(const Function &Fn,
                             DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_ISelFallback, Severity), Fn(Fn) {}

  const Function &getFunction() const { return Fn; }

  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_ISelFallback;
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(DiagnosticInfo, LLVMDiagnosticInfoRef)

/// Diagnostic information for optimization failures.
class DiagnosticInfoOptimizationFailure : public DiagnosticInfoIROptimization {
public:
  /// \p Fn is the function where the diagnostic is being emitted. \p Loc is
  /// the location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location. \p Msg is the message to show. Note that this class does not
  /// copy this message, so this reference must be valid for the whole life time
  /// of the diagnostic.
  DiagnosticInfoOptimizationFailure(const Function &Fn,
                                    const DiagnosticLocation &Loc,
                                    const Twine &Msg)
      : DiagnosticInfoIROptimization(DK_OptimizationFailure, DS_Warning,
                                     nullptr, Fn, Loc, Msg) {}

  /// \p PassName is the name of the pass emitting this diagnostic.  \p
  /// RemarkName is a textual identifier for the remark (single-word,
  /// camel-case).  \p Loc is the debug location and \p CodeRegion is the
  /// region that the optimization operates on (currently basic block is
  /// supported).
  DiagnosticInfoOptimizationFailure(const char *PassName, StringRef RemarkName,
                                    const DiagnosticLocation &Loc,
                                    const Value *CodeRegion);

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationFailure;
  }

  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  bool isEnabled() const override;
};

/// Diagnostic information for unsupported feature in backend.
class DiagnosticInfoUnsupported : public DiagnosticInfoWithLocationBase {
private:
  Twine Msg;

public:
  /// \p Fn is the function where the diagnostic is being emitted. \p Loc is
  /// the location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location. \p Msg is the message to show. Note that this class does not
  /// copy this message, so this reference must be valid for the whole life time
  /// of the diagnostic.
  DiagnosticInfoUnsupported(
      const Function &Fn, const Twine &Msg,
      const DiagnosticLocation &Loc = DiagnosticLocation(),
      DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfoWithLocationBase(DK_Unsupported, Severity, Fn, Loc),
        Msg(Msg) {}

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_Unsupported;
  }

  const Twine &getMessage() const { return Msg; }

  void print(DiagnosticPrinter &DP) const override;
};

/// Diagnostic information for MisExpect analysis.
class DiagnosticInfoMisExpect : public DiagnosticInfoWithLocationBase {
public:
  DiagnosticInfoMisExpect(const Instruction *Inst, Twine &Msg);

  /// \see DiagnosticInfo::print.
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MisExpect;
  }

  const Twine &getMsg() const { return Msg; }

private:
  /// Message to report.
  const Twine &Msg;
};

static DiagnosticSeverity getDiagnosticSeverity(SourceMgr::DiagKind DK) {
  switch (DK) {
  case llvm::SourceMgr::DK_Error:
    return DS_Error;
    break;
  case llvm::SourceMgr::DK_Warning:
    return DS_Warning;
    break;
  case llvm::SourceMgr::DK_Note:
    return DS_Note;
    break;
  case llvm::SourceMgr::DK_Remark:
    return DS_Remark;
    break;
  }
  llvm_unreachable("unknown SourceMgr::DiagKind");
}

/// Diagnostic information for SMDiagnostic reporting.
class DiagnosticInfoSrcMgr : public DiagnosticInfo {
  const SMDiagnostic &Diagnostic;
  StringRef ModName;

  // For inlineasm !srcloc translation.
  bool InlineAsmDiag;
  uint64_t LocCookie;

public:
  DiagnosticInfoSrcMgr(const SMDiagnostic &Diagnostic, StringRef ModName,
                       bool InlineAsmDiag = true, uint64_t LocCookie = 0)
      : DiagnosticInfo(DK_SrcMgr, getDiagnosticSeverity(Diagnostic.getKind())),
        Diagnostic(Diagnostic), ModName(ModName), InlineAsmDiag(InlineAsmDiag),
        LocCookie(LocCookie) {}

  StringRef getModuleName() const { return ModName; }
  bool isInlineAsmDiag() const { return InlineAsmDiag; }
  const SMDiagnostic &getSMDiag() const { return Diagnostic; }
  uint64_t getLocCookie() const { return LocCookie; }
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_SrcMgr;
  }
};

void diagnoseDontCall(const CallInst &CI);

class DiagnosticInfoDontCall : public DiagnosticInfo {
  StringRef CalleeName;
  StringRef Note;
  uint64_t LocCookie;

public:
  DiagnosticInfoDontCall(StringRef CalleeName, StringRef Note,
                         DiagnosticSeverity DS, uint64_t LocCookie)
      : DiagnosticInfo(DK_DontCall, DS), CalleeName(CalleeName), Note(Note),
        LocCookie(LocCookie) {}
  StringRef getFunctionName() const { return CalleeName; }
  StringRef getNote() const { return Note; }
  uint64_t getLocCookie() const { return LocCookie; }
  void print(DiagnosticPrinter &DP) const override;
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_DontCall;
  }
};

} // end namespace llvm

#endif // LLVM_IR_DIAGNOSTICINFO_H
