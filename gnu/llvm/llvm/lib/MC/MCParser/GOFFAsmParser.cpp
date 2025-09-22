//===- GOFFAsmParser.cpp - GOFF Assembly Parser ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCParser/MCAsmParserExtension.h"

using namespace llvm;

namespace {

class GOFFAsmParser : public MCAsmParserExtension {
  template <bool (GOFFAsmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler =
        std::make_pair(this, HandleDirective<GOFFAsmParser, HandlerMethod>);

    getParser().addDirectiveHandler(Directive, Handler);
  }

public:
  GOFFAsmParser() = default;

  void Initialize(MCAsmParser &Parser) override {
    // Call the base implementation.
    this->MCAsmParserExtension::Initialize(Parser);
  }
};

} // namespace

namespace llvm {

MCAsmParserExtension *createGOFFAsmParser() { return new GOFFAsmParser; }

} // namespace llvm
