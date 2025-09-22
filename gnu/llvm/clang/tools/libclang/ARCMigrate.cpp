//===- ARCMigrate.cpp - Clang-C ARC Migration Library ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the main API hooks in the Clang-C ARC Migration library.
//
//===----------------------------------------------------------------------===//

#include "clang-c/Index.h"
#include "CXString.h"
#include "clang/ARCMigrate/ARCMT.h"
#include "clang/Config/config.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "llvm/Support/FileSystem.h"

using namespace clang;
using namespace arcmt;

namespace {

struct Remap {
  std::vector<std::pair<std::string, std::string> > Vec;
};

} // anonymous namespace.

//===----------------------------------------------------------------------===//
// libClang public APIs.
//===----------------------------------------------------------------------===//

CXRemapping clang_getRemappings(const char *migrate_dir_path) {
#if !CLANG_ENABLE_ARCMT
  llvm::errs() << "error: feature not enabled in this build\n";
  return nullptr;
#else
  bool Logging = ::getenv("LIBCLANG_LOGGING");

  if (!migrate_dir_path) {
    if (Logging)
      llvm::errs() << "clang_getRemappings was called with NULL parameter\n";
    return nullptr;
  }

  if (!llvm::sys::fs::exists(migrate_dir_path)) {
    if (Logging) {
      llvm::errs() << "Error by clang_getRemappings(\"" << migrate_dir_path
                   << "\")\n";
      llvm::errs() << "\"" << migrate_dir_path << "\" does not exist\n";
    }
    return nullptr;
  }

  TextDiagnosticBuffer diagBuffer;
  std::unique_ptr<Remap> remap(new Remap());

  bool err = arcmt::getFileRemappings(remap->Vec, migrate_dir_path,&diagBuffer);

  if (err) {
    if (Logging) {
      llvm::errs() << "Error by clang_getRemappings(\"" << migrate_dir_path
                   << "\")\n";
      for (TextDiagnosticBuffer::const_iterator
             I = diagBuffer.err_begin(), E = diagBuffer.err_end(); I != E; ++I)
        llvm::errs() << I->second << '\n';
    }
    return nullptr;
  }

  return remap.release();
#endif
}

CXRemapping clang_getRemappingsFromFileList(const char **filePaths,
                                            unsigned numFiles) {
#if !CLANG_ENABLE_ARCMT
  llvm::errs() << "error: feature not enabled in this build\n";
  return nullptr;
#else
  bool Logging = ::getenv("LIBCLANG_LOGGING");

  std::unique_ptr<Remap> remap(new Remap());

  if (numFiles == 0) {
    if (Logging)
      llvm::errs() << "clang_getRemappingsFromFileList was called with "
                      "numFiles=0\n";
    return remap.release();
  }

  if (!filePaths) {
    if (Logging)
      llvm::errs() << "clang_getRemappingsFromFileList was called with "
                      "NULL filePaths\n";
    return nullptr;
  }

  TextDiagnosticBuffer diagBuffer;
  SmallVector<StringRef, 32> Files(filePaths, filePaths + numFiles);

  bool err = arcmt::getFileRemappingsFromFileList(remap->Vec, Files,
                                                  &diagBuffer);

  if (err) {
    if (Logging) {
      llvm::errs() << "Error by clang_getRemappingsFromFileList\n";
      for (TextDiagnosticBuffer::const_iterator
             I = diagBuffer.err_begin(), E = diagBuffer.err_end(); I != E; ++I)
        llvm::errs() << I->second << '\n';
    }
    return remap.release();
  }

  return remap.release();
#endif
}

unsigned clang_remap_getNumFiles(CXRemapping map) {
  return static_cast<Remap *>(map)->Vec.size();
  
}

void clang_remap_getFilenames(CXRemapping map, unsigned index,
                              CXString *original, CXString *transformed) {
  if (original)
    *original = cxstring::createDup(
                    static_cast<Remap *>(map)->Vec[index].first);
  if (transformed)
    *transformed = cxstring::createDup(
                    static_cast<Remap *>(map)->Vec[index].second);
}

void clang_remap_dispose(CXRemapping map) {
  delete static_cast<Remap *>(map);
}
