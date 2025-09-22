//===- Parser.cpp - Top-Level TableGen Parser implementation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/Parser.h"
#include "TGParser.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

bool llvm::TableGenParseFile(SourceMgr &InputSrcMgr, RecordKeeper &Records) {
  // Initialize the global TableGen source manager by temporarily taking control
  // of the input buffer in `SrcMgr`. This is kind of a hack, but allows for
  // preserving TableGen's current awkward diagnostic behavior. If we can remove
  // this reliance, we could drop all of this.
  SrcMgr = SourceMgr();
  SrcMgr.takeSourceBuffersFrom(InputSrcMgr);
  SrcMgr.setIncludeDirs(InputSrcMgr.getIncludeDirs());
  SrcMgr.setDiagHandler(InputSrcMgr.getDiagHandler(),
                        InputSrcMgr.getDiagContext());

  // Setup the record keeper and try to parse the file.
  auto *MainFileBuffer = SrcMgr.getMemoryBuffer(SrcMgr.getMainFileID());
  Records.saveInputFilename(MainFileBuffer->getBufferIdentifier().str());

  TGParser Parser(SrcMgr, /*Macros=*/std::nullopt, Records,
                  /*NoWarnOnUnusedTemplateArgs=*/false,
                  /*TrackReferenceLocs=*/true);
  bool ParseResult = Parser.ParseFile();

  // After parsing, reclaim the source manager buffers from TableGen's global
  // manager.
  InputSrcMgr.takeSourceBuffersFrom(SrcMgr);
  SrcMgr = SourceMgr();
  return ParseResult;
}
