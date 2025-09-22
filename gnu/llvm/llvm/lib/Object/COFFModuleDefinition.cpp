//===--- COFFModuleDefinition.cpp - Simple DEF parser ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Windows-specific.
// A parser for the module-definition file (.def file).
//
// The format of module-definition files are described in this document:
// https://msdn.microsoft.com/en-us/library/28d6s79h.aspx
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/COFFModuleDefinition.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"

using namespace llvm::COFF;
using namespace llvm;

namespace llvm {
namespace object {

enum Kind {
  Unknown,
  Eof,
  Identifier,
  Comma,
  Equal,
  EqualEqual,
  KwBase,
  KwConstant,
  KwData,
  KwExports,
  KwExportAs,
  KwHeapsize,
  KwLibrary,
  KwName,
  KwNoname,
  KwPrivate,
  KwStacksize,
  KwVersion,
};

struct Token {
  explicit Token(Kind T = Unknown, StringRef S = "") : K(T), Value(S) {}
  Kind K;
  StringRef Value;
};

static bool isDecorated(StringRef Sym, bool MingwDef) {
  // In def files, the symbols can either be listed decorated or undecorated.
  //
  // - For cdecl symbols, only the undecorated form is allowed.
  // - For fastcall and vectorcall symbols, both fully decorated or
  //   undecorated forms can be present.
  // - For stdcall symbols in non-MinGW environments, the decorated form is
  //   fully decorated with leading underscore and trailing stack argument
  //   size - like "_Func@0".
  // - In MinGW def files, a decorated stdcall symbol does not include the
  //   leading underscore though, like "Func@0".

  // This function controls whether a leading underscore should be added to
  // the given symbol name or not. For MinGW, treat a stdcall symbol name such
  // as "Func@0" as undecorated, i.e. a leading underscore must be added.
  // For non-MinGW, look for '@' in the whole string and consider "_Func@0"
  // as decorated, i.e. don't add any more leading underscores.
  // We can't check for a leading underscore here, since function names
  // themselves can start with an underscore, while a second one still needs
  // to be added.
  return Sym.starts_with("@") || Sym.contains("@@") || Sym.starts_with("?") ||
         (!MingwDef && Sym.contains('@'));
}

class Lexer {
public:
  Lexer(StringRef S) : Buf(S) {}

  Token lex() {
    Buf = Buf.trim();
    if (Buf.empty())
      return Token(Eof);

    switch (Buf[0]) {
    case '\0':
      return Token(Eof);
    case ';': {
      size_t End = Buf.find('\n');
      Buf = (End == Buf.npos) ? "" : Buf.drop_front(End);
      return lex();
    }
    case '=':
      Buf = Buf.drop_front();
      if (Buf.consume_front("="))
        return Token(EqualEqual, "==");
      return Token(Equal, "=");
    case ',':
      Buf = Buf.drop_front();
      return Token(Comma, ",");
    case '"': {
      StringRef S;
      std::tie(S, Buf) = Buf.substr(1).split('"');
      return Token(Identifier, S);
    }
    default: {
      size_t End = Buf.find_first_of("=,;\r\n \t\v");
      StringRef Word = Buf.substr(0, End);
      Kind K = llvm::StringSwitch<Kind>(Word)
                   .Case("BASE", KwBase)
                   .Case("CONSTANT", KwConstant)
                   .Case("DATA", KwData)
                   .Case("EXPORTS", KwExports)
                   .Case("EXPORTAS", KwExportAs)
                   .Case("HEAPSIZE", KwHeapsize)
                   .Case("LIBRARY", KwLibrary)
                   .Case("NAME", KwName)
                   .Case("NONAME", KwNoname)
                   .Case("PRIVATE", KwPrivate)
                   .Case("STACKSIZE", KwStacksize)
                   .Case("VERSION", KwVersion)
                   .Default(Identifier);
      Buf = (End == Buf.npos) ? "" : Buf.drop_front(End);
      return Token(K, Word);
    }
    }
  }

private:
  StringRef Buf;
};

class Parser {
public:
  explicit Parser(StringRef S, MachineTypes M, bool B, bool AU)
      : Lex(S), Machine(M), MingwDef(B), AddUnderscores(AU) {
    if (Machine != IMAGE_FILE_MACHINE_I386)
      AddUnderscores = false;
  }

  Expected<COFFModuleDefinition> parse() {
    do {
      if (Error Err = parseOne())
        return std::move(Err);
    } while (Tok.K != Eof);
    return Info;
  }

private:
  void read() {
    if (Stack.empty()) {
      Tok = Lex.lex();
      return;
    }
    Tok = Stack.back();
    Stack.pop_back();
  }

  Error readAsInt(uint64_t *I) {
    read();
    if (Tok.K != Identifier || Tok.Value.getAsInteger(10, *I))
      return createError("integer expected");
    return Error::success();
  }

  Error expect(Kind Expected, StringRef Msg) {
    read();
    if (Tok.K != Expected)
      return createError(Msg);
    return Error::success();
  }

  void unget() { Stack.push_back(Tok); }

  Error parseOne() {
    read();
    switch (Tok.K) {
    case Eof:
      return Error::success();
    case KwExports:
      for (;;) {
        read();
        if (Tok.K != Identifier) {
          unget();
          return Error::success();
        }
        if (Error Err = parseExport())
          return Err;
      }
    case KwHeapsize:
      return parseNumbers(&Info.HeapReserve, &Info.HeapCommit);
    case KwStacksize:
      return parseNumbers(&Info.StackReserve, &Info.StackCommit);
    case KwLibrary:
    case KwName: {
      bool IsDll = Tok.K == KwLibrary; // Check before parseName.
      std::string Name;
      if (Error Err = parseName(&Name, &Info.ImageBase))
        return Err;

      Info.ImportName = Name;

      // Set the output file, but don't override /out if it was already passed.
      if (Info.OutputFile.empty()) {
        Info.OutputFile = Name;
        // Append the appropriate file extension if not already present.
        if (!sys::path::has_extension(Name))
          Info.OutputFile += IsDll ? ".dll" : ".exe";
      }

      return Error::success();
    }
    case KwVersion:
      return parseVersion(&Info.MajorImageVersion, &Info.MinorImageVersion);
    default:
      return createError("unknown directive: " + Tok.Value);
    }
  }

  Error parseExport() {
    COFFShortExport E;
    E.Name = std::string(Tok.Value);
    read();
    if (Tok.K == Equal) {
      read();
      if (Tok.K != Identifier)
        return createError("identifier expected, but got " + Tok.Value);
      E.ExtName = E.Name;
      E.Name = std::string(Tok.Value);
    } else {
      unget();
    }

    if (AddUnderscores) {
      if (!isDecorated(E.Name, MingwDef))
        E.Name = (std::string("_").append(E.Name));
      if (!E.ExtName.empty() && !isDecorated(E.ExtName, MingwDef))
        E.ExtName = (std::string("_").append(E.ExtName));
    }

    for (;;) {
      read();
      if (Tok.K == Identifier && Tok.Value[0] == '@') {
        if (Tok.Value == "@") {
          // "foo @ 10"
          read();
          Tok.Value.getAsInteger(10, E.Ordinal);
        } else if (Tok.Value.drop_front().getAsInteger(10, E.Ordinal)) {
          // "foo \n @bar" - Not an ordinal modifier at all, but the next
          // export (fastcall decorated) - complete the current one.
          unget();
          Info.Exports.push_back(E);
          return Error::success();
        }
        // "foo @10"
        read();
        if (Tok.K == KwNoname) {
          E.Noname = true;
        } else {
          unget();
        }
        continue;
      }
      if (Tok.K == KwData) {
        E.Data = true;
        continue;
      }
      if (Tok.K == KwConstant) {
        E.Constant = true;
        continue;
      }
      if (Tok.K == KwPrivate) {
        E.Private = true;
        continue;
      }
      if (Tok.K == EqualEqual) {
        read();
        E.ImportName = std::string(Tok.Value);
        continue;
      }
      // EXPORTAS must be at the end of export definition
      if (Tok.K == KwExportAs) {
        read();
        if (Tok.K == Eof)
          return createError(
              "unexpected end of file, EXPORTAS identifier expected");
        E.ExportAs = std::string(Tok.Value);
      } else {
        unget();
      }
      Info.Exports.push_back(E);
      return Error::success();
    }
  }

  // HEAPSIZE/STACKSIZE reserve[,commit]
  Error parseNumbers(uint64_t *Reserve, uint64_t *Commit) {
    if (Error Err = readAsInt(Reserve))
      return Err;
    read();
    if (Tok.K != Comma) {
      unget();
      Commit = nullptr;
      return Error::success();
    }
    if (Error Err = readAsInt(Commit))
      return Err;
    return Error::success();
  }

  // NAME outputPath [BASE=address]
  Error parseName(std::string *Out, uint64_t *Baseaddr) {
    read();
    if (Tok.K == Identifier) {
      *Out = std::string(Tok.Value);
    } else {
      *Out = "";
      unget();
      return Error::success();
    }
    read();
    if (Tok.K == KwBase) {
      if (Error Err = expect(Equal, "'=' expected"))
        return Err;
      if (Error Err = readAsInt(Baseaddr))
        return Err;
    } else {
      unget();
      *Baseaddr = 0;
    }
    return Error::success();
  }

  // VERSION major[.minor]
  Error parseVersion(uint32_t *Major, uint32_t *Minor) {
    read();
    if (Tok.K != Identifier)
      return createError("identifier expected, but got " + Tok.Value);
    StringRef V1, V2;
    std::tie(V1, V2) = Tok.Value.split('.');
    if (V1.getAsInteger(10, *Major))
      return createError("integer expected, but got " + Tok.Value);
    if (V2.empty())
      *Minor = 0;
    else if (V2.getAsInteger(10, *Minor))
      return createError("integer expected, but got " + Tok.Value);
    return Error::success();
  }

  Lexer Lex;
  Token Tok;
  std::vector<Token> Stack;
  MachineTypes Machine;
  COFFModuleDefinition Info;
  bool MingwDef;
  bool AddUnderscores;
};

Expected<COFFModuleDefinition> parseCOFFModuleDefinition(MemoryBufferRef MB,
                                                         MachineTypes Machine,
                                                         bool MingwDef,
                                                         bool AddUnderscores) {
  return Parser(MB.getBuffer(), Machine, MingwDef, AddUnderscores).parse();
}

} // namespace object
} // namespace llvm
