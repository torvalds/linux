//===- IFSHandler.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/
///
/// \file
/// This file declares an interface for reading and writing .ifs (text-based
/// InterFace Stub) files.
///
//===-----------------------------------------------------------------------===/

#ifndef LLVM_INTERFACESTUB_IFSHANDLER_H
#define LLVM_INTERFACESTUB_IFSHANDLER_H

#include "IFSStub.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VersionTuple.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;
class Error;
class StringRef;

namespace ifs {

struct IFSStub;

const VersionTuple IFSVersionCurrent(3, 0);

/// Attempts to read an IFS interface file from a StringRef buffer.
Expected<std::unique_ptr<IFSStub>> readIFSFromBuffer(StringRef Buf);

/// Attempts to write an IFS interface file to a raw_ostream.
Error writeIFSToOutputStream(raw_ostream &OS, const IFSStub &Stub);

/// Override the target platform inforation in the text stub.
Error overrideIFSTarget(IFSStub &Stub, std::optional<IFSArch> OverrideArch,
                        std::optional<IFSEndiannessType> OverrideEndianness,
                        std::optional<IFSBitWidthType> OverrideBitWidth,
                        std::optional<std::string> OverrideTriple);

/// Validate the target platform inforation in the text stub.
Error validateIFSTarget(IFSStub &Stub, bool ParseTriple);

/// Strips target platform information from the text stub.
void stripIFSTarget(IFSStub &Stub, bool StripTriple, bool StripArch,
                    bool StripEndianness, bool StripBitWidth);

Error filterIFSSyms(IFSStub &Stub, bool StripUndefined,
                    const std::vector<std::string> &Exclude = {});

/// Parse llvm triple string into a IFSTarget struct.
IFSTarget parseTriple(StringRef TripleStr);

} // end namespace ifs
} // end namespace llvm

#endif // LLVM_INTERFACESTUB_IFSHANDLER_H
