//===- Job.h - Commands to Execute ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_JOB_H
#define LLVM_CLANG_DRIVER_JOB_H

#include "clang/Basic/LLVM.h"
#include "clang/Driver/InputInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Program.h"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clang {
namespace driver {

class Action;
class InputInfo;
class Tool;

struct CrashReportInfo {
  StringRef Filename;
  StringRef VFSPath;

  CrashReportInfo(StringRef Filename, StringRef VFSPath)
      : Filename(Filename), VFSPath(VFSPath) {}
};

// Encodes the kind of response file supported for a command invocation.
// Response files are necessary if the command line gets too large, requiring
// the arguments to be transferred to a file.
struct ResponseFileSupport {
  enum ResponseFileKind {
    // Provides full support for response files, which means we can transfer
    // all tool input arguments to a file.
    RF_Full,
    // Input file names can live in a file, but flags can't. This is a special
    // case for old versions of Apple's ld64.
    RF_FileList,
    // Does not support response files: all arguments must be passed via
    // command line.
    RF_None
  };
  /// The level of support for response files.
  ResponseFileKind ResponseKind;

  /// The encoding to use when writing response files on Windows. Ignored on
  /// other host OSes.
  ///
  /// Windows use cases: - GCC and Binutils on mingw only accept ANSI response
  /// files encoded with the system current code page.
  /// - MSVC's CL.exe and LINK.exe accept UTF16 on Windows.
  /// - Clang accepts both UTF8 and UTF16.
  ///
  /// FIXME: When GNU tools learn how to parse UTF16 on Windows, we should
  /// always use UTF16 for Windows, which is the Windows official encoding for
  /// international characters.
  llvm::sys::WindowsEncodingMethod ResponseEncoding;

  /// What prefix to use for the command-line argument when passing a response
  /// file.
  const char *ResponseFlag;

  /// Returns a ResponseFileSupport indicating that response files are not
  /// supported.
  static constexpr ResponseFileSupport None() {
    return {RF_None, llvm::sys::WEM_UTF8, nullptr};
  }

  /// Returns a ResponseFileSupport indicating that response files are
  /// supported, using the @file syntax. On windows, the file is written in the
  /// UTF8 encoding. On other OSes, no re-encoding occurs.
  static constexpr ResponseFileSupport AtFileUTF8() {
    return {RF_Full, llvm::sys::WEM_UTF8, "@"};
  }

  /// Returns a ResponseFileSupport indicating that response files are
  /// supported, using the @file syntax. On windows, the file is written in the
  /// current ANSI code-page encoding. On other OSes, no re-encoding occurs.
  static constexpr ResponseFileSupport AtFileCurCP() {
    return {RF_Full, llvm::sys::WEM_CurrentCodePage, "@"};
  }

  /// Returns a ResponseFileSupport indicating that response files are
  /// supported, using the @file syntax. On windows, the file is written in the
  /// UTF-16 encoding. On other OSes, no re-encoding occurs.
  static constexpr ResponseFileSupport AtFileUTF16() {
    return {RF_Full, llvm::sys::WEM_UTF16, "@"};
  }
};

/// Command - An executable path/name and argument vector to
/// execute.
class Command {
  /// Source - The action which caused the creation of this job.
  const Action &Source;

  /// Tool - The tool which caused the creation of this job.
  const Tool &Creator;

  /// Whether and how to generate response files if the arguments are too long.
  ResponseFileSupport ResponseSupport;

  /// The executable to run.
  const char *Executable;

  /// Optional argument to prepend.
  const char *PrependArg;

  /// The list of program arguments (not including the implicit first
  /// argument, which will be the executable).
  llvm::opt::ArgStringList Arguments;

  /// The list of program inputs.
  std::vector<InputInfo> InputInfoList;

  /// The list of program arguments which are outputs. May be empty.
  std::vector<std::string> OutputFilenames;

  /// Response file name, if this command is set to use one, or nullptr
  /// otherwise
  const char *ResponseFile = nullptr;

  /// The input file list in case we need to emit a file list instead of a
  /// proper response file
  llvm::opt::ArgStringList InputFileList;

  /// String storage if we need to create a new argument to specify a response
  /// file
  std::string ResponseFileFlag;

  /// See Command::setEnvironment
  std::vector<const char *> Environment;

  /// Optional redirection for stdin, stdout, stderr.
  std::vector<std::optional<std::string>> RedirectFiles;

  /// Information on executable run provided by OS.
  mutable std::optional<llvm::sys::ProcessStatistics> ProcStat;

  /// When a response file is needed, we try to put most arguments in an
  /// exclusive file, while others remains as regular command line arguments.
  /// This functions fills a vector with the regular command line arguments,
  /// argv, excluding the ones passed in a response file.
  void buildArgvForResponseFile(llvm::SmallVectorImpl<const char *> &Out) const;

  /// Encodes an array of C strings into a single string separated by whitespace.
  /// This function will also put in quotes arguments that have whitespaces and
  /// will escape the regular backslashes (used in Windows paths) and quotes.
  /// The results are the contents of a response file, written into a raw_ostream.
  void writeResponseFile(raw_ostream &OS) const;

public:
  /// Whether to print the input filenames when executing.
  bool PrintInputFilenames = false;

  /// Whether the command will be executed in this process or not.
  bool InProcess = false;

  Command(const Action &Source, const Tool &Creator,
          ResponseFileSupport ResponseSupport, const char *Executable,
          const llvm::opt::ArgStringList &Arguments, ArrayRef<InputInfo> Inputs,
          ArrayRef<InputInfo> Outputs = std::nullopt,
          const char *PrependArg = nullptr);
  // FIXME: This really shouldn't be copyable, but is currently copied in some
  // error handling in Driver::generateCompilationDiagnostics.
  Command(const Command &) = default;
  virtual ~Command() = default;

  virtual void Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
                     CrashReportInfo *CrashInfo = nullptr) const;

  virtual int Execute(ArrayRef<std::optional<StringRef>> Redirects,
                      std::string *ErrMsg, bool *ExecutionFailed) const;

  /// getSource - Return the Action which caused the creation of this job.
  const Action &getSource() const { return Source; }

  /// getCreator - Return the Tool which caused the creation of this job.
  const Tool &getCreator() const { return Creator; }

  /// Returns the kind of response file supported by the current invocation.
  const ResponseFileSupport &getResponseFileSupport() {
    return ResponseSupport;
  }

  /// Set to pass arguments via a response file when launching the command
  void setResponseFile(const char *FileName);

  /// Set an input file list, necessary if you specified an RF_FileList response
  /// file support.
  void setInputFileList(llvm::opt::ArgStringList List) {
    InputFileList = std::move(List);
  }

  /// Sets the environment to be used by the new process.
  /// \param NewEnvironment An array of environment variables.
  /// \remark If the environment remains unset, then the environment
  ///         from the parent process will be used.
  virtual void setEnvironment(llvm::ArrayRef<const char *> NewEnvironment);

  void
  setRedirectFiles(const std::vector<std::optional<std::string>> &Redirects);

  void replaceArguments(llvm::opt::ArgStringList List) {
    Arguments = std::move(List);
  }

  void replaceExecutable(const char *Exe) { Executable = Exe; }

  const char *getExecutable() const { return Executable; }

  const llvm::opt::ArgStringList &getArguments() const { return Arguments; }

  const std::vector<InputInfo> &getInputInfos() const { return InputInfoList; }

  const std::vector<std::string> &getOutputFilenames() const {
    return OutputFilenames;
  }

  std::optional<llvm::sys::ProcessStatistics> getProcessStatistics() const {
    return ProcStat;
  }

protected:
  /// Optionally print the filenames to be compiled
  void PrintFileNames() const;
};

/// Use the CC1 tool callback when available, to avoid creating a new process
class CC1Command : public Command {
public:
  CC1Command(const Action &Source, const Tool &Creator,
             ResponseFileSupport ResponseSupport, const char *Executable,
             const llvm::opt::ArgStringList &Arguments,
             ArrayRef<InputInfo> Inputs,
             ArrayRef<InputInfo> Outputs = std::nullopt,
             const char *PrependArg = nullptr);

  void Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
             CrashReportInfo *CrashInfo = nullptr) const override;

  int Execute(ArrayRef<std::optional<StringRef>> Redirects, std::string *ErrMsg,
              bool *ExecutionFailed) const override;

  void setEnvironment(llvm::ArrayRef<const char *> NewEnvironment) override;
};

/// JobList - A sequence of jobs to perform.
class JobList {
public:
  using list_type = SmallVector<std::unique_ptr<Command>, 4>;
  using size_type = list_type::size_type;
  using iterator = llvm::pointee_iterator<list_type::iterator>;
  using const_iterator = llvm::pointee_iterator<list_type::const_iterator>;

private:
  list_type Jobs;

public:
  void Print(llvm::raw_ostream &OS, const char *Terminator,
             bool Quote, CrashReportInfo *CrashInfo = nullptr) const;

  /// Add a job to the list (taking ownership).
  void addJob(std::unique_ptr<Command> J) { Jobs.push_back(std::move(J)); }

  /// Clear the job list.
  void clear();

  const list_type &getJobs() const { return Jobs; }

  bool empty() const { return Jobs.empty(); }
  size_type size() const { return Jobs.size(); }
  iterator begin() { return Jobs.begin(); }
  const_iterator begin() const { return Jobs.begin(); }
  iterator end() { return Jobs.end(); }
  const_iterator end() const { return Jobs.end(); }
};

} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_DRIVER_JOB_H
