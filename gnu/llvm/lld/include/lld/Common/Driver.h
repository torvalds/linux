//===- lld/Common/Driver.h - Linker Driver Emulator -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_DRIVER_H
#define LLD_COMMON_DRIVER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

namespace lld {
enum Flavor {
  Invalid,
  Gnu,     // -flavor gnu
  MinGW,   // -flavor gnu MinGW
  WinLink, // -flavor link
  Darwin,  // -flavor darwin
  Wasm,    // -flavor wasm
};

using Driver = bool (*)(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                        llvm::raw_ostream &, bool, bool);

struct DriverDef {
  Flavor f;
  Driver d;
};

struct Result {
  int retCode;
  bool canRunAgain;
};

// Generic entry point when using LLD as a library, safe for re-entry, supports
// crash recovery. Returns a general completion code and a boolean telling
// whether it can be called again. In some cases, a crash could corrupt memory
// and re-entry would not be possible anymore. Use exitLld() in that case to
// properly exit your application and avoid intermittent crashes on exit caused
// by cleanup.
Result lldMain(llvm::ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,
               llvm::raw_ostream &stderrOS, llvm::ArrayRef<DriverDef> drivers);
} // namespace lld

// With this macro, library users must specify which drivers they use, provide
// that information to lldMain() in the `drivers` param, and link the
// corresponding driver library in their executable.
#define LLD_HAS_DRIVER(name)                                                   \
  namespace lld {                                                              \
  namespace name {                                                             \
  bool link(llvm::ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,    \
            llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput);  \
  }                                                                            \
  }

#define LLD_HAS_DRIVER_DUMMY(name)                                             \
  namespace lld {                                                              \
  namespace name {                                                             \
  bool link(llvm::ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,    \
            llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput) { \
                 return false;                                                 \
            }                                                                  \
  }                                                                            \
  }

// An array which declares that all LLD drivers are linked in your executable.
// Must be used along with LLD_HAS_DRIVERS. See examples in LLD unittests.
#define LLD_ALL_DRIVERS                                                        \
  {                                                                            \
    {lld::WinLink, &lld::coff::link}, {lld::Gnu, &lld::elf::link},             \
        {lld::MinGW, &lld::mingw::link}, {lld::Darwin, &lld::macho::link}, {   \
      lld::Wasm, &lld::wasm::link                                              \
    }                                                                          \
  }

#endif
