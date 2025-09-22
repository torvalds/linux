//===- tools/dsymutil/Reproducer.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_DSYMUTIL_REPRODUCER_H
#define LLVM_TOOLS_DSYMUTIL_REPRODUCER_H

#include "llvm/Support/FileCollector.h"
#include "llvm/Support/VirtualFileSystem.h"

namespace llvm {
namespace dsymutil {

/// The reproducer mode.
enum class ReproducerMode {
  GenerateOnExit,
  GenerateOnCrash,
  Use,
  Off,
};

/// The reproducer class manages the sate related to reproducers in dsymutil.
/// Instances should be created with Reproducer::createReproducer. An instance
/// of this class is returned when reproducers are off. The VFS returned by
/// this instance is the real file system.
class Reproducer {
public:
  Reproducer();
  virtual ~Reproducer();

  IntrusiveRefCntPtr<vfs::FileSystem> getVFS() const { return VFS; }

  virtual void generate(){};

  /// Create a Reproducer instance based on the given mode.
  static llvm::Expected<std::unique_ptr<Reproducer>>
  createReproducer(ReproducerMode Mode, StringRef Root, int Argc, char **Argv);

protected:
  IntrusiveRefCntPtr<vfs::FileSystem> VFS;
};

/// Reproducer instance used to generate a new reproducer. The VFS returned by
/// this instance is a FileCollectorFileSystem that tracks every file used by
/// dsymutil.
class ReproducerGenerate : public Reproducer {
public:
  ReproducerGenerate(std::error_code &EC, int Argc, char **Argv,
                     bool GenerateOnExit);
  ~ReproducerGenerate() override;

  void generate() override;

private:
  /// The path to the reproducer.
  std::string Root;

  /// The FileCollector used by the FileCollectorFileSystem.
  std::shared_ptr<FileCollector> FC;

  /// The input arguments to build the reproducer invocation.
  llvm::SmallVector<llvm::StringRef, 0> Args;

  /// Whether to generate the reproducer on destruction.
  bool GenerateOnExit = false;

  /// Whether we already generated the reproducer.
  bool Generated = false;
};

/// Reproducer instance used to use an existing reproducer. The VFS returned by
/// this instance is a RedirectingFileSystem that remaps paths to their
/// counterpart in the reproducer.
class ReproducerUse : public Reproducer {
public:
  ReproducerUse(StringRef Root, std::error_code &EC);
  ~ReproducerUse() override;

private:
  /// The path to the reproducer.
  std::string Root;
};

} // end namespace dsymutil
} // end namespace llvm

#endif // LLVM_TOOLS_DSYMUTIL_REPRODUCER_H
