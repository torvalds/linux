//===- Reproducer.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Reproducer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"

using namespace llvm;
using namespace llvm::dsymutil;

static std::string createReproducerDir(std::error_code &EC) {
  SmallString<128> Root;
  if (const char *Path = getenv("DSYMUTIL_REPRODUCER_PATH")) {
    Root.assign(Path);
    EC = sys::fs::create_directories(Root);
  } else if (const char *Path = getenv("LLVM_DIAGNOSTIC_DIR")) {
    Root.assign(Path);
    llvm::sys::path::append(
        Root, "dsymutil-" + llvm::Twine(llvm::sys::Process::getProcessId()));
    EC = sys::fs::create_directories(Root);
  } else {
    EC = sys::fs::createUniqueDirectory("dsymutil", Root);
  }
  sys::fs::make_absolute(Root);
  return EC ? "" : std::string(Root);
}

Reproducer::Reproducer() : VFS(vfs::getRealFileSystem()) {}
Reproducer::~Reproducer() = default;

ReproducerGenerate::ReproducerGenerate(std::error_code &EC, int Argc,
                                       char **Argv, bool GenerateOnExit)
    : Root(createReproducerDir(EC)), GenerateOnExit(GenerateOnExit) {
  for (int I = 0; I < Argc; ++I)
    Args.push_back(Argv[I]);
  if (!Root.empty())
    FC = std::make_shared<FileCollector>(Root, Root);
  VFS = FileCollector::createCollectorVFS(vfs::getRealFileSystem(), FC);
}

ReproducerGenerate::~ReproducerGenerate() {
  if (GenerateOnExit && !Generated)
    generate();
  else if (!Generated && !Root.empty())
    sys::fs::remove_directories(Root, /* IgnoreErrors */ true);
}

void ReproducerGenerate::generate() {
  if (!FC)
    return;
  Generated = true;
  FC->copyFiles(false);
  SmallString<128> Mapping(Root);
  sys::path::append(Mapping, "mapping.yaml");
  FC->writeMapping(Mapping.str());
  errs() << "********************\n";
  errs() << "Reproducer written to " << Root << '\n';
  errs() << "Please include the reproducer and the following invocation in "
            "your bug report:\n";
  for (llvm::StringRef Arg : Args)
    errs() << Arg << ' ';
  errs() << "--use-reproducer " << Root << '\n';
  errs() << "********************\n";
}

ReproducerUse::~ReproducerUse() = default;

ReproducerUse::ReproducerUse(StringRef Root, std::error_code &EC) {
  SmallString<128> Mapping(Root);
  sys::path::append(Mapping, "mapping.yaml");
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      vfs::getRealFileSystem()->getBufferForFile(Mapping.str());

  if (!Buffer) {
    EC = Buffer.getError();
    return;
  }

  VFS = llvm::vfs::getVFSFromYAML(std::move(Buffer.get()), nullptr, Mapping);
}

llvm::Expected<std::unique_ptr<Reproducer>>
Reproducer::createReproducer(ReproducerMode Mode, StringRef Root, int Argc,
                             char **Argv) {

  std::error_code EC;
  std::unique_ptr<Reproducer> Repro;
  switch (Mode) {
  case ReproducerMode::GenerateOnExit:
    Repro = std::make_unique<ReproducerGenerate>(EC, Argc, Argv, true);
    break;
  case ReproducerMode::GenerateOnCrash:
    Repro = std::make_unique<ReproducerGenerate>(EC, Argc, Argv, false);
    break;
  case ReproducerMode::Use:
    Repro = std::make_unique<ReproducerUse>(Root, EC);
    break;
  case ReproducerMode::Off:
    Repro = std::make_unique<Reproducer>();
    break;
  }
  if (EC)
    return errorCodeToError(EC);
  return {std::move(Repro)};
}
