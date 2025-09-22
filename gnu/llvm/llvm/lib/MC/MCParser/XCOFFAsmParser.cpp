//===- XCOFFAsmParser.cpp - XCOFF Assembly Parser
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"

using namespace llvm;

namespace {

class XCOFFAsmParser : public MCAsmParserExtension {
  MCAsmParser *Parser = nullptr;
  MCAsmLexer *Lexer = nullptr;

  template <bool (XCOFFAsmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler =
        std::make_pair(this, HandleDirective<XCOFFAsmParser, HandlerMethod>);

    getParser().addDirectiveHandler(Directive, Handler);
  }

public:
  XCOFFAsmParser() = default;

  void Initialize(MCAsmParser &P) override {
    Parser = &P;
    Lexer = &Parser->getLexer();
    // Call the base implementation.
    MCAsmParserExtension::Initialize(*Parser);

    addDirectiveHandler<&XCOFFAsmParser::ParseDirectiveCSect>(".csect");
  }
  bool ParseDirectiveCSect(StringRef, SMLoc);
};

} // end anonymous namespace

namespace llvm {

MCAsmParserExtension *createXCOFFAsmParser() { return new XCOFFAsmParser; }

} // end namespace llvm

// .csect QualName [, Number ]
bool XCOFFAsmParser::ParseDirectiveCSect(StringRef, SMLoc) {
  report_fatal_error("XCOFFAsmParser directive not yet supported!");
  return false;
}
