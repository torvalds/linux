//===- CIndexer.cpp - Clang-C Source Indexing Library ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Clang-C Source Indexing library.
//
//===----------------------------------------------------------------------===//

#include "CIndexer.h"
#include "CXString.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Driver.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/YAMLParser.h"
#include <cstdio>
#include <mutex>

#ifdef __CYGWIN__
#include <cygwin/version.h>
#include <sys/cygwin.h>
#define _WIN32 1
#endif

#ifdef _WIN32
#include <windows.h>
#elif defined(_AIX)
#include <errno.h>
#include <sys/ldr.h>
#else
#include <dlfcn.h>
#endif

using namespace clang;

#ifdef _AIX
namespace clang {
namespace {

template <typename LibClangPathType>
void getClangResourcesPathImplAIX(LibClangPathType &LibClangPath) {
  int PrevErrno = errno;

  size_t BufSize = 2048u;
  std::unique_ptr<char[]> Buf;
  while (true) {
    Buf = std::make_unique<char []>(BufSize);
    errno = 0;
    int Ret = loadquery(L_GETXINFO, Buf.get(), (unsigned int)BufSize);
    if (Ret != -1)
      break; // loadquery() was successful.
    if (errno != ENOMEM)
      llvm_unreachable("Encountered an unexpected loadquery() failure");

    // errno == ENOMEM; try to allocate more memory.
    if ((BufSize & ~((-1u) >> 1u)) != 0u)
      llvm::report_fatal_error("BufSize needed for loadquery() too large");

    Buf.release();
    BufSize <<= 1u;
  }

  // Extract the function entry point from the function descriptor.
  uint64_t EntryAddr =
      reinterpret_cast<uintptr_t &>(clang_createTranslationUnit);

  // Loop to locate the function entry point in the loadquery() results.
  ld_xinfo *CurInfo = reinterpret_cast<ld_xinfo *>(Buf.get());
  while (true) {
    uint64_t CurTextStart = (uint64_t)CurInfo->ldinfo_textorg;
    uint64_t CurTextEnd = CurTextStart + CurInfo->ldinfo_textsize;
    if (CurTextStart <= EntryAddr && EntryAddr < CurTextEnd)
      break; // Successfully located.

    if (CurInfo->ldinfo_next == 0u)
      llvm::report_fatal_error("Cannot locate entry point in "
                               "the loadquery() results");
    CurInfo = reinterpret_cast<ld_xinfo *>(reinterpret_cast<char *>(CurInfo) +
                                           CurInfo->ldinfo_next);
  }

  LibClangPath += reinterpret_cast<char *>(CurInfo) + CurInfo->ldinfo_filename;
  errno = PrevErrno;
}

} // end anonymous namespace
} // end namespace clang
#endif

const std::string &CIndexer::getClangResourcesPath() {
  // Did we already compute the path?
  if (!ResourcesPath.empty())
    return ResourcesPath;

  SmallString<128> LibClangPath;

  // Find the location where this library lives (libclang.dylib).
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION mbi;
  char path[MAX_PATH];
  VirtualQuery((void *)(uintptr_t)clang_createTranslationUnit, &mbi,
               sizeof(mbi));
  GetModuleFileNameA((HINSTANCE)mbi.AllocationBase, path, MAX_PATH);

#ifdef __CYGWIN__
  char w32path[MAX_PATH];
  strcpy(w32path, path);
#if CYGWIN_VERSION_API_MAJOR > 0 || CYGWIN_VERSION_API_MINOR >= 181
  cygwin_conv_path(CCP_WIN_A_TO_POSIX, w32path, path, MAX_PATH);
#else
  cygwin_conv_to_full_posix_path(w32path, path);
#endif
#endif

  LibClangPath += path;
#elif defined(_AIX)
  getClangResourcesPathImplAIX(LibClangPath);
#else
  bool PathFound = false;
#if defined(CLANG_HAVE_DLFCN_H) && defined(CLANG_HAVE_DLADDR)
  Dl_info info;
  // This silly cast below avoids a C++ warning.
  if (dladdr((void *)(uintptr_t)clang_createTranslationUnit, &info) != 0) {
    // We now have the CIndex directory, locate clang relative to it.
    LibClangPath += info.dli_fname;
    PathFound = true;
  }
#endif
  std::string Path;
  if (!PathFound) {
    if (!(Path = llvm::sys::fs::getMainExecutable(nullptr, nullptr)).empty()) {
      // If we can't get the path using dladdr, try to get the main executable
      // path. This may be needed when we're statically linking libclang with
      // musl libc, for example.
      LibClangPath += Path;
    } else {
      // It's rather unlikely we end up here. But it could happen, so report an
      // error instead of crashing.
      llvm::report_fatal_error("could not locate Clang resource path");
    }
  }

#endif

  // Cache our result.
  ResourcesPath = driver::Driver::GetResourcesPath(LibClangPath);
  return ResourcesPath;
}

StringRef CIndexer::getClangToolchainPath() {
  if (!ToolchainPath.empty())
    return ToolchainPath;
  StringRef ResourcePath = getClangResourcesPath();
  ToolchainPath =
      std::string(llvm::sys::path::parent_path(llvm::sys::path::parent_path(
          llvm::sys::path::parent_path(ResourcePath))));
  return ToolchainPath;
}

LibclangInvocationReporter::LibclangInvocationReporter(
    CIndexer &Idx, OperationKind Op, unsigned ParseOptions,
    llvm::ArrayRef<const char *> Args,
    llvm::ArrayRef<std::string> InvocationArgs,
    llvm::ArrayRef<CXUnsavedFile> UnsavedFiles) {
  StringRef Path = Idx.getInvocationEmissionPath();
  if (Path.empty())
    return;

  // Create a temporary file for the invocation log.
  SmallString<256> TempPath;
  TempPath = Path;
  llvm::sys::path::append(TempPath, "libclang-%%%%%%%%%%%%");
  int FD;
  if (llvm::sys::fs::createUniqueFile(TempPath, FD, TempPath,
                                      llvm::sys::fs::OF_Text))
    return;
  File = static_cast<std::string>(TempPath);
  llvm::raw_fd_ostream OS(FD, /*ShouldClose=*/true);

  // Write out the information about the invocation to it.
  auto WriteStringKey = [&OS](StringRef Key, StringRef Value) {
    OS << R"(")" << Key << R"(":")";
    OS << llvm::yaml::escape(Value) << '"';
  };
  OS << '{';
  WriteStringKey("toolchain", Idx.getClangToolchainPath());
  OS << ',';
  WriteStringKey("libclang.operation",
                 Op == OperationKind::ParseOperation ? "parse" : "complete");
  OS << ',';
  OS << R"("libclang.opts":)" << ParseOptions;
  OS << ',';
  OS << R"("args":[)";
  for (const auto &I : llvm::enumerate(Args)) {
    if (I.index())
      OS << ',';
    OS << '"' << llvm::yaml::escape(I.value()) << '"';
  }
  if (!InvocationArgs.empty()) {
    OS << R"(],"invocation-args":[)";
    for (const auto &I : llvm::enumerate(InvocationArgs)) {
      if (I.index())
        OS << ',';
      OS << '"' << llvm::yaml::escape(I.value()) << '"';
    }
  }
  if (!UnsavedFiles.empty()) {
    OS << R"(],"unsaved_file_hashes":[)";
    for (const auto &UF : llvm::enumerate(UnsavedFiles)) {
      if (UF.index())
        OS << ',';
      OS << '{';
      WriteStringKey("name", UF.value().Filename);
      OS << ',';
      llvm::MD5 Hash;
      Hash.update(getContents(UF.value()));
      llvm::MD5::MD5Result Result;
      Hash.final(Result);
      SmallString<32> Digest = Result.digest();
      WriteStringKey("md5", Digest);
      OS << '}';
    }
  }
  OS << "]}";
}

LibclangInvocationReporter::~LibclangInvocationReporter() {
  if (!File.empty())
    llvm::sys::fs::remove(File);
}
