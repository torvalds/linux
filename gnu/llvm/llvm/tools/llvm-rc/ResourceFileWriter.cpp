//===-- ResourceFileWriter.cpp --------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This implements the visitor serializing resources to a .res stream.
//
//===---------------------------------------------------------------------===//

#include "ResourceFileWriter.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"

using namespace llvm::support;

// Take an expression returning llvm::Error and forward the error if it exists.
#define RETURN_IF_ERROR(Expr)                                                  \
  if (auto Err = (Expr))                                                       \
    return Err;

namespace llvm {
namespace rc {

// Class that employs RAII to save the current FileWriter object state
// and revert to it as soon as we leave the scope. This is useful if resources
// declare their own resource-local statements.
class ContextKeeper {
  ResourceFileWriter *FileWriter;
  ResourceFileWriter::ObjectInfo SavedInfo;

public:
  ContextKeeper(ResourceFileWriter *V)
      : FileWriter(V), SavedInfo(V->ObjectData) {}
  ~ContextKeeper() { FileWriter->ObjectData = SavedInfo; }
};

static Error createError(const Twine &Message,
                         std::errc Type = std::errc::invalid_argument) {
  return make_error<StringError>(Message, std::make_error_code(Type));
}

static Error checkNumberFits(uint32_t Number, size_t MaxBits,
                             const Twine &FieldName) {
  assert(1 <= MaxBits && MaxBits <= 32);
  if (!(Number >> MaxBits))
    return Error::success();
  return createError(FieldName + " (" + Twine(Number) + ") does not fit in " +
                         Twine(MaxBits) + " bits.",
                     std::errc::value_too_large);
}

template <typename FitType>
static Error checkNumberFits(uint32_t Number, const Twine &FieldName) {
  return checkNumberFits(Number, sizeof(FitType) * 8, FieldName);
}

// A similar function for signed integers.
template <typename FitType>
static Error checkSignedNumberFits(uint32_t Number, const Twine &FieldName,
                                   bool CanBeNegative) {
  int32_t SignedNum = Number;
  if (SignedNum < std::numeric_limits<FitType>::min() ||
      SignedNum > std::numeric_limits<FitType>::max())
    return createError(FieldName + " (" + Twine(SignedNum) +
                           ") does not fit in " + Twine(sizeof(FitType) * 8) +
                           "-bit signed integer type.",
                       std::errc::value_too_large);

  if (!CanBeNegative && SignedNum < 0)
    return createError(FieldName + " (" + Twine(SignedNum) +
                       ") cannot be negative.");

  return Error::success();
}

static Error checkRCInt(RCInt Number, const Twine &FieldName) {
  if (Number.isLong())
    return Error::success();
  return checkNumberFits<uint16_t>(Number, FieldName);
}

static Error checkIntOrString(IntOrString Value, const Twine &FieldName) {
  if (!Value.isInt())
    return Error::success();
  return checkNumberFits<uint16_t>(Value.getInt(), FieldName);
}

static bool stripQuotes(StringRef &Str, bool &IsLongString) {
  if (!Str.contains('"'))
    return false;

  // Just take the contents of the string, checking if it's been marked long.
  IsLongString = Str.starts_with_insensitive("L");
  if (IsLongString)
    Str = Str.drop_front();

  bool StripSuccess = Str.consume_front("\"") && Str.consume_back("\"");
  (void)StripSuccess;
  assert(StripSuccess && "Strings should be enclosed in quotes.");
  return true;
}

static UTF16 cp1252ToUnicode(unsigned char C) {
  static const UTF16 Map80[] = {
      0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
      0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
      0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
      0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
  };
  if (C >= 0x80 && C <= 0x9F)
    return Map80[C - 0x80];
  return C;
}

// Describes a way to handle '\0' characters when processing the string.
// rc.exe tool sometimes behaves in a weird way in postprocessing.
// If the string to be output is equivalent to a C-string (e.g. in MENU
// titles), string is (predictably) truncated after first 0-byte.
// When outputting a string table, the behavior is equivalent to appending
// '\0\0' at the end of the string, and then stripping the string
// before the first '\0\0' occurrence.
// Finally, when handling strings in user-defined resources, 0-bytes
// aren't stripped, nor do they terminate the string.

enum class NullHandlingMethod {
  UserResource,   // Don't terminate string on '\0'.
  CutAtNull,      // Terminate string on '\0'.
  CutAtDoubleNull // Terminate string on '\0\0'; strip final '\0'.
};

// Parses an identifier or string and returns a processed version of it:
//   * Strip the string boundary quotes.
//   * Convert the input code page characters to UTF16.
//   * Squash "" to a single ".
//   * Replace the escape sequences with their processed version.
// For identifiers, this is no-op.
static Error processString(StringRef Str, NullHandlingMethod NullHandler,
                           bool &IsLongString, SmallVectorImpl<UTF16> &Result,
                           int CodePage) {
  bool IsString = stripQuotes(Str, IsLongString);
  SmallVector<UTF16, 128> Chars;

  // Convert the input bytes according to the chosen codepage.
  if (CodePage == CpUtf8) {
    convertUTF8ToUTF16String(Str, Chars);
  } else if (CodePage == CpWin1252) {
    for (char C : Str)
      Chars.push_back(cp1252ToUnicode((unsigned char)C));
  } else {
    // For other, unknown codepages, only allow plain ASCII input.
    for (char C : Str) {
      if ((unsigned char)C > 0x7F)
        return createError("Non-ASCII 8-bit codepoint (" + Twine(C) +
                           ") can't be interpreted in the current codepage");
      Chars.push_back((unsigned char)C);
    }
  }

  if (!IsString) {
    // It's an identifier if it's not a string. Make all characters uppercase.
    for (UTF16 &Ch : Chars) {
      assert(Ch <= 0x7F && "We didn't allow identifiers to be non-ASCII");
      Ch = toupper(Ch);
    }
    Result.swap(Chars);
    return Error::success();
  }
  Result.reserve(Chars.size());
  size_t Pos = 0;

  auto AddRes = [&Result, NullHandler, IsLongString](UTF16 Char) -> Error {
    if (!IsLongString) {
      if (NullHandler == NullHandlingMethod::UserResource) {
        // Narrow strings in user-defined resources are *not* output in
        // UTF-16 format.
        if (Char > 0xFF)
          return createError("Non-8-bit codepoint (" + Twine(Char) +
                             ") can't occur in a user-defined narrow string");
      }
    }

    Result.push_back(Char);
    return Error::success();
  };
  auto AddEscapedChar = [AddRes, IsLongString, CodePage](UTF16 Char) -> Error {
    if (!IsLongString) {
      // Escaped chars in narrow strings have to be interpreted according to
      // the chosen code page.
      if (Char > 0xFF)
        return createError("Non-8-bit escaped char (" + Twine(Char) +
                           ") can't occur in narrow string");
      if (CodePage == CpUtf8) {
        if (Char >= 0x80)
          return createError("Unable to interpret single byte (" + Twine(Char) +
                             ") as UTF-8");
      } else if (CodePage == CpWin1252) {
        Char = cp1252ToUnicode(Char);
      } else {
        // Unknown/unsupported codepage, only allow ASCII input.
        if (Char > 0x7F)
          return createError("Non-ASCII 8-bit codepoint (" + Twine(Char) +
                             ") can't "
                             "occur in a non-Unicode string");
      }
    }

    return AddRes(Char);
  };

  while (Pos < Chars.size()) {
    UTF16 CurChar = Chars[Pos];
    ++Pos;

    // Strip double "".
    if (CurChar == '"') {
      if (Pos == Chars.size() || Chars[Pos] != '"')
        return createError("Expected \"\"");
      ++Pos;
      RETURN_IF_ERROR(AddRes('"'));
      continue;
    }

    if (CurChar == '\\') {
      UTF16 TypeChar = Chars[Pos];
      ++Pos;

      if (TypeChar == 'x' || TypeChar == 'X') {
        // Read a hex number. Max number of characters to read differs between
        // narrow and wide strings.
        UTF16 ReadInt = 0;
        size_t RemainingChars = IsLongString ? 4 : 2;
        // We don't want to read non-ASCII hex digits. std:: functions past
        // 0xFF invoke UB.
        //
        // FIXME: actually, Microsoft version probably doesn't check this
        // condition and uses their Unicode version of 'isxdigit'. However,
        // there are some hex-digit Unicode character outside of ASCII, and
        // some of these are actually accepted by rc.exe, the notable example
        // being fullwidth forms (U+FF10..U+FF19 etc.) These can be written
        // instead of ASCII digits in \x... escape sequence and get accepted.
        // However, the resulting hexcodes seem totally unpredictable.
        // We think it's infeasible to try to reproduce this behavior, nor to
        // put effort in order to detect it.
        while (RemainingChars && Pos < Chars.size() && Chars[Pos] < 0x80) {
          if (!isxdigit(Chars[Pos]))
            break;
          char Digit = tolower(Chars[Pos]);
          ++Pos;

          ReadInt <<= 4;
          if (isdigit(Digit))
            ReadInt |= Digit - '0';
          else
            ReadInt |= Digit - 'a' + 10;

          --RemainingChars;
        }

        RETURN_IF_ERROR(AddEscapedChar(ReadInt));
        continue;
      }

      if (TypeChar >= '0' && TypeChar < '8') {
        // Read an octal number. Note that we've already read the first digit.
        UTF16 ReadInt = TypeChar - '0';
        size_t RemainingChars = IsLongString ? 6 : 2;

        while (RemainingChars && Pos < Chars.size() && Chars[Pos] >= '0' &&
               Chars[Pos] < '8') {
          ReadInt <<= 3;
          ReadInt |= Chars[Pos] - '0';
          --RemainingChars;
          ++Pos;
        }

        RETURN_IF_ERROR(AddEscapedChar(ReadInt));

        continue;
      }

      switch (TypeChar) {
      case 'A':
      case 'a':
        // Windows '\a' translates into '\b' (Backspace).
        RETURN_IF_ERROR(AddRes('\b'));
        break;

      case 'n': // Somehow, RC doesn't recognize '\N' and '\R'.
        RETURN_IF_ERROR(AddRes('\n'));
        break;

      case 'r':
        RETURN_IF_ERROR(AddRes('\r'));
        break;

      case 'T':
      case 't':
        RETURN_IF_ERROR(AddRes('\t'));
        break;

      case '\\':
        RETURN_IF_ERROR(AddRes('\\'));
        break;

      case '"':
        // RC accepts \" only if another " comes afterwards; then, \"" means
        // a single ".
        if (Pos == Chars.size() || Chars[Pos] != '"')
          return createError("Expected \\\"\"");
        ++Pos;
        RETURN_IF_ERROR(AddRes('"'));
        break;

      default:
        // If TypeChar means nothing, \ is should be output to stdout with
        // following char. However, rc.exe consumes these characters when
        // dealing with wide strings.
        if (!IsLongString) {
          RETURN_IF_ERROR(AddRes('\\'));
          RETURN_IF_ERROR(AddRes(TypeChar));
        }
        break;
      }

      continue;
    }

    // If nothing interesting happens, just output the character.
    RETURN_IF_ERROR(AddRes(CurChar));
  }

  switch (NullHandler) {
  case NullHandlingMethod::CutAtNull:
    for (size_t Pos = 0; Pos < Result.size(); ++Pos)
      if (Result[Pos] == '\0')
        Result.resize(Pos);
    break;

  case NullHandlingMethod::CutAtDoubleNull:
    for (size_t Pos = 0; Pos + 1 < Result.size(); ++Pos)
      if (Result[Pos] == '\0' && Result[Pos + 1] == '\0')
        Result.resize(Pos);
    if (Result.size() > 0 && Result.back() == '\0')
      Result.pop_back();
    break;

  case NullHandlingMethod::UserResource:
    break;
  }

  return Error::success();
}

uint64_t ResourceFileWriter::writeObject(const ArrayRef<uint8_t> Data) {
  uint64_t Result = tell();
  FS->write((const char *)Data.begin(), Data.size());
  return Result;
}

Error ResourceFileWriter::writeCString(StringRef Str, bool WriteTerminator) {
  SmallVector<UTF16, 128> ProcessedString;
  bool IsLongString;
  RETURN_IF_ERROR(processString(Str, NullHandlingMethod::CutAtNull,
                                IsLongString, ProcessedString,
                                Params.CodePage));
  for (auto Ch : ProcessedString)
    writeInt<uint16_t>(Ch);
  if (WriteTerminator)
    writeInt<uint16_t>(0);
  return Error::success();
}

Error ResourceFileWriter::writeIdentifier(const IntOrString &Ident) {
  return writeIntOrString(Ident);
}

Error ResourceFileWriter::writeIntOrString(const IntOrString &Value) {
  if (!Value.isInt())
    return writeCString(Value.getString());

  writeInt<uint16_t>(0xFFFF);
  writeInt<uint16_t>(Value.getInt());
  return Error::success();
}

void ResourceFileWriter::writeRCInt(RCInt Value) {
  if (Value.isLong())
    writeInt<uint32_t>(Value);
  else
    writeInt<uint16_t>(Value);
}

Error ResourceFileWriter::appendFile(StringRef Filename) {
  bool IsLong;
  stripQuotes(Filename, IsLong);

  auto File = loadFile(Filename);
  if (!File)
    return File.takeError();

  *FS << (*File)->getBuffer();
  return Error::success();
}

void ResourceFileWriter::padStream(uint64_t Length) {
  assert(Length > 0);
  uint64_t Location = tell();
  Location %= Length;
  uint64_t Pad = (Length - Location) % Length;
  for (uint64_t i = 0; i < Pad; ++i)
    writeInt<uint8_t>(0);
}

Error ResourceFileWriter::handleError(Error Err, const RCResource *Res) {
  if (Err)
    return joinErrors(createError("Error in " + Res->getResourceTypeName() +
                                  " statement (ID " + Twine(Res->ResName) +
                                  "): "),
                      std::move(Err));
  return Error::success();
}

Error ResourceFileWriter::visitNullResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeNullBody);
}

Error ResourceFileWriter::visitAcceleratorsResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeAcceleratorsBody);
}

Error ResourceFileWriter::visitBitmapResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeBitmapBody);
}

Error ResourceFileWriter::visitCursorResource(const RCResource *Res) {
  return handleError(visitIconOrCursorResource(Res), Res);
}

Error ResourceFileWriter::visitDialogResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeDialogBody);
}

Error ResourceFileWriter::visitIconResource(const RCResource *Res) {
  return handleError(visitIconOrCursorResource(Res), Res);
}

Error ResourceFileWriter::visitCaptionStmt(const CaptionStmt *Stmt) {
  ObjectData.Caption = Stmt->Value;
  return Error::success();
}

Error ResourceFileWriter::visitClassStmt(const ClassStmt *Stmt) {
  ObjectData.Class = Stmt->Value;
  return Error::success();
}

Error ResourceFileWriter::visitHTMLResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeHTMLBody);
}

Error ResourceFileWriter::visitMenuResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeMenuBody);
}

Error ResourceFileWriter::visitMenuExResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeMenuExBody);
}

Error ResourceFileWriter::visitStringTableResource(const RCResource *Base) {
  const auto *Res = cast<StringTableResource>(Base);

  ContextKeeper RAII(this);
  RETURN_IF_ERROR(Res->applyStmts(this));

  for (auto &String : Res->Table) {
    RETURN_IF_ERROR(checkNumberFits<uint16_t>(String.first, "String ID"));
    uint16_t BundleID = String.first >> 4;
    StringTableInfo::BundleKey Key(BundleID, ObjectData.LanguageInfo);
    auto &BundleData = StringTableData.BundleData;
    auto Iter = BundleData.find(Key);

    if (Iter == BundleData.end()) {
      // Need to create a bundle.
      StringTableData.BundleList.push_back(Key);
      auto EmplaceResult = BundleData.emplace(
          Key, StringTableInfo::Bundle(ObjectData, Res->MemoryFlags));
      assert(EmplaceResult.second && "Could not create a bundle");
      Iter = EmplaceResult.first;
    }

    RETURN_IF_ERROR(
        insertStringIntoBundle(Iter->second, String.first, String.second));
  }

  return Error::success();
}

Error ResourceFileWriter::visitUserDefinedResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeUserDefinedBody);
}

Error ResourceFileWriter::visitVersionInfoResource(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeVersionInfoBody);
}

Error ResourceFileWriter::visitCharacteristicsStmt(
    const CharacteristicsStmt *Stmt) {
  ObjectData.Characteristics = Stmt->Value;
  return Error::success();
}

Error ResourceFileWriter::visitExStyleStmt(const ExStyleStmt *Stmt) {
  ObjectData.ExStyle = Stmt->Value;
  return Error::success();
}

Error ResourceFileWriter::visitFontStmt(const FontStmt *Stmt) {
  RETURN_IF_ERROR(checkNumberFits<uint16_t>(Stmt->Size, "Font size"));
  RETURN_IF_ERROR(checkNumberFits<uint16_t>(Stmt->Weight, "Font weight"));
  RETURN_IF_ERROR(checkNumberFits<uint8_t>(Stmt->Charset, "Font charset"));
  ObjectInfo::FontInfo Font{Stmt->Size, Stmt->Name, Stmt->Weight, Stmt->Italic,
                            Stmt->Charset};
  ObjectData.Font.emplace(Font);
  return Error::success();
}

Error ResourceFileWriter::visitLanguageStmt(const LanguageResource *Stmt) {
  RETURN_IF_ERROR(checkNumberFits(Stmt->Lang, 10, "Primary language ID"));
  RETURN_IF_ERROR(checkNumberFits(Stmt->SubLang, 6, "Sublanguage ID"));
  ObjectData.LanguageInfo = Stmt->Lang | (Stmt->SubLang << 10);
  return Error::success();
}

Error ResourceFileWriter::visitStyleStmt(const StyleStmt *Stmt) {
  ObjectData.Style = Stmt->Value;
  return Error::success();
}

Error ResourceFileWriter::visitVersionStmt(const VersionStmt *Stmt) {
  ObjectData.VersionInfo = Stmt->Value;
  return Error::success();
}

Error ResourceFileWriter::visitMenuStmt(const MenuStmt *Stmt) {
  ObjectData.Menu = Stmt->Value;
  return Error::success();
}

Error ResourceFileWriter::writeResource(
    const RCResource *Res,
    Error (ResourceFileWriter::*BodyWriter)(const RCResource *)) {
  // We don't know the sizes yet.
  object::WinResHeaderPrefix HeaderPrefix{ulittle32_t(0U), ulittle32_t(0U)};
  uint64_t HeaderLoc = writeObject(HeaderPrefix);

  auto ResType = Res->getResourceType();
  RETURN_IF_ERROR(checkIntOrString(ResType, "Resource type"));
  RETURN_IF_ERROR(checkIntOrString(Res->ResName, "Resource ID"));
  RETURN_IF_ERROR(handleError(writeIdentifier(ResType), Res));
  RETURN_IF_ERROR(handleError(writeIdentifier(Res->ResName), Res));

  // Apply the resource-local optional statements.
  ContextKeeper RAII(this);
  RETURN_IF_ERROR(handleError(Res->applyStmts(this), Res));

  padStream(sizeof(uint32_t));
  object::WinResHeaderSuffix HeaderSuffix{
      ulittle32_t(0), // DataVersion; seems to always be 0
      ulittle16_t(Res->MemoryFlags), ulittle16_t(ObjectData.LanguageInfo),
      ulittle32_t(ObjectData.VersionInfo),
      ulittle32_t(ObjectData.Characteristics)};
  writeObject(HeaderSuffix);

  uint64_t DataLoc = tell();
  RETURN_IF_ERROR(handleError((this->*BodyWriter)(Res), Res));
  // RETURN_IF_ERROR(handleError(dumpResource(Ctx)));

  // Update the sizes.
  HeaderPrefix.DataSize = tell() - DataLoc;
  HeaderPrefix.HeaderSize = DataLoc - HeaderLoc;
  writeObjectAt(HeaderPrefix, HeaderLoc);
  padStream(sizeof(uint32_t));

  return Error::success();
}

// --- NullResource helpers. --- //

Error ResourceFileWriter::writeNullBody(const RCResource *) {
  return Error::success();
}

// --- AcceleratorsResource helpers. --- //

Error ResourceFileWriter::writeSingleAccelerator(
    const AcceleratorsResource::Accelerator &Obj, bool IsLastItem) {
  using Accelerator = AcceleratorsResource::Accelerator;
  using Opt = Accelerator::Options;

  struct AccelTableEntry {
    ulittle16_t Flags;
    ulittle16_t ANSICode;
    ulittle16_t Id;
    uint16_t Padding;
  } Entry{ulittle16_t(0), ulittle16_t(0), ulittle16_t(0), 0};

  bool IsASCII = Obj.Flags & Opt::ASCII, IsVirtKey = Obj.Flags & Opt::VIRTKEY;

  // Remove ASCII flags (which doesn't occur in .res files).
  Entry.Flags = Obj.Flags & ~Opt::ASCII;

  if (IsLastItem)
    Entry.Flags |= 0x80;

  RETURN_IF_ERROR(checkNumberFits<uint16_t>(Obj.Id, "ACCELERATORS entry ID"));
  Entry.Id = ulittle16_t(Obj.Id);

  auto createAccError = [&Obj](const char *Msg) {
    return createError("Accelerator ID " + Twine(Obj.Id) + ": " + Msg);
  };

  if (IsASCII && IsVirtKey)
    return createAccError("Accelerator can't be both ASCII and VIRTKEY");

  if (!IsVirtKey && (Obj.Flags & (Opt::ALT | Opt::SHIFT | Opt::CONTROL)))
    return createAccError("Can only apply ALT, SHIFT or CONTROL to VIRTKEY"
                          " accelerators");

  if (Obj.Event.isInt()) {
    if (!IsASCII && !IsVirtKey)
      return createAccError(
          "Accelerator with a numeric event must be either ASCII"
          " or VIRTKEY");

    uint32_t EventVal = Obj.Event.getInt();
    RETURN_IF_ERROR(
        checkNumberFits<uint16_t>(EventVal, "Numeric event key ID"));
    Entry.ANSICode = ulittle16_t(EventVal);
    writeObject(Entry);
    return Error::success();
  }

  StringRef Str = Obj.Event.getString();
  bool IsWide;
  stripQuotes(Str, IsWide);

  if (Str.size() == 0 || Str.size() > 2)
    return createAccError(
        "Accelerator string events should have length 1 or 2");

  if (Str[0] == '^') {
    if (Str.size() == 1)
      return createAccError("No character following '^' in accelerator event");
    if (IsVirtKey)
      return createAccError(
          "VIRTKEY accelerator events can't be preceded by '^'");

    char Ch = Str[1];
    if (Ch >= 'a' && Ch <= 'z')
      Entry.ANSICode = ulittle16_t(Ch - 'a' + 1);
    else if (Ch >= 'A' && Ch <= 'Z')
      Entry.ANSICode = ulittle16_t(Ch - 'A' + 1);
    else
      return createAccError("Control character accelerator event should be"
                            " alphabetic");

    writeObject(Entry);
    return Error::success();
  }

  if (Str.size() == 2)
    return createAccError("Event string should be one-character, possibly"
                          " preceded by '^'");

  uint8_t EventCh = Str[0];
  // The original tool just warns in this situation. We chose to fail.
  if (IsVirtKey && !isalnum(EventCh))
    return createAccError("Non-alphanumeric characters cannot describe virtual"
                          " keys");
  if (EventCh > 0x7F)
    return createAccError("Non-ASCII description of accelerator");

  if (IsVirtKey)
    EventCh = toupper(EventCh);
  Entry.ANSICode = ulittle16_t(EventCh);
  writeObject(Entry);
  return Error::success();
}

Error ResourceFileWriter::writeAcceleratorsBody(const RCResource *Base) {
  auto *Res = cast<AcceleratorsResource>(Base);
  size_t AcceleratorId = 0;
  for (auto &Acc : Res->Accelerators) {
    ++AcceleratorId;
    RETURN_IF_ERROR(
        writeSingleAccelerator(Acc, AcceleratorId == Res->Accelerators.size()));
  }
  return Error::success();
}

// --- BitmapResource helpers. --- //

Error ResourceFileWriter::writeBitmapBody(const RCResource *Base) {
  StringRef Filename = cast<BitmapResource>(Base)->BitmapLoc;
  bool IsLong;
  stripQuotes(Filename, IsLong);

  auto File = loadFile(Filename);
  if (!File)
    return File.takeError();

  StringRef Buffer = (*File)->getBuffer();

  // Skip the 14 byte BITMAPFILEHEADER.
  constexpr size_t BITMAPFILEHEADER_size = 14;
  if (Buffer.size() < BITMAPFILEHEADER_size || Buffer[0] != 'B' ||
      Buffer[1] != 'M')
    return createError("Incorrect bitmap file.");

  *FS << Buffer.substr(BITMAPFILEHEADER_size);
  return Error::success();
}

// --- CursorResource and IconResource helpers. --- //

// ICONRESDIR structure. Describes a single icon in resource group.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648016.aspx
struct IconResDir {
  uint8_t Width;
  uint8_t Height;
  uint8_t ColorCount;
  uint8_t Reserved;
};

// CURSORDIR structure. Describes a single cursor in resource group.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648011(v=vs.85).aspx
struct CursorDir {
  ulittle16_t Width;
  ulittle16_t Height;
};

// RESDIRENTRY structure, stripped from the last item. Stripping made
// for compatibility with RESDIR.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648026(v=vs.85).aspx
struct ResourceDirEntryStart {
  union {
    CursorDir Cursor; // Used in CURSOR resources.
    IconResDir Icon;  // Used in .ico and .cur files, and ICON resources.
  };
  ulittle16_t Planes;   // HotspotX (.cur files but not CURSOR resource).
  ulittle16_t BitCount; // HotspotY (.cur files but not CURSOR resource).
  ulittle32_t Size;
  // ulittle32_t ImageOffset;  // Offset to image data (ICONDIRENTRY only).
  // ulittle16_t IconID;       // Resource icon ID (RESDIR only).
};

// BITMAPINFOHEADER structure. Describes basic information about the bitmap
// being read.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/dd183376(v=vs.85).aspx
struct BitmapInfoHeader {
  ulittle32_t Size;
  ulittle32_t Width;
  ulittle32_t Height;
  ulittle16_t Planes;
  ulittle16_t BitCount;
  ulittle32_t Compression;
  ulittle32_t SizeImage;
  ulittle32_t XPelsPerMeter;
  ulittle32_t YPelsPerMeter;
  ulittle32_t ClrUsed;
  ulittle32_t ClrImportant;
};

// Group icon directory header. Called ICONDIR in .ico/.cur files and
// NEWHEADER in .res files.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648023(v=vs.85).aspx
struct GroupIconDir {
  ulittle16_t Reserved; // Always 0.
  ulittle16_t ResType;  // 1 for icons, 2 for cursors.
  ulittle16_t ResCount; // Number of items.
};

enum class IconCursorGroupType { Icon, Cursor };

class SingleIconCursorResource : public RCResource {
public:
  IconCursorGroupType Type;
  const ResourceDirEntryStart &Header;
  ArrayRef<uint8_t> Image;

  SingleIconCursorResource(IconCursorGroupType ResourceType,
                           const ResourceDirEntryStart &HeaderEntry,
                           ArrayRef<uint8_t> ImageData, uint16_t Flags)
      : RCResource(Flags), Type(ResourceType), Header(HeaderEntry),
        Image(ImageData) {}

  Twine getResourceTypeName() const override { return "Icon/cursor image"; }
  IntOrString getResourceType() const override {
    return Type == IconCursorGroupType::Icon ? RkSingleIcon : RkSingleCursor;
  }
  ResourceKind getKind() const override { return RkSingleCursorOrIconRes; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkSingleCursorOrIconRes;
  }
};

class IconCursorGroupResource : public RCResource {
public:
  IconCursorGroupType Type;
  GroupIconDir Header;
  std::vector<ResourceDirEntryStart> ItemEntries;

  IconCursorGroupResource(IconCursorGroupType ResourceType,
                          const GroupIconDir &HeaderData,
                          std::vector<ResourceDirEntryStart> &&Entries)
      : Type(ResourceType), Header(HeaderData),
        ItemEntries(std::move(Entries)) {}

  Twine getResourceTypeName() const override { return "Icon/cursor group"; }
  IntOrString getResourceType() const override {
    return Type == IconCursorGroupType::Icon ? RkIconGroup : RkCursorGroup;
  }
  ResourceKind getKind() const override { return RkCursorOrIconGroupRes; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkCursorOrIconGroupRes;
  }
};

Error ResourceFileWriter::writeSingleIconOrCursorBody(const RCResource *Base) {
  auto *Res = cast<SingleIconCursorResource>(Base);
  if (Res->Type == IconCursorGroupType::Cursor) {
    // In case of cursors, two WORDS are appended to the beginning
    // of the resource: HotspotX (Planes in RESDIRENTRY),
    // and HotspotY (BitCount).
    //
    // Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648026.aspx
    //  (Remarks section).
    writeObject(Res->Header.Planes);
    writeObject(Res->Header.BitCount);
  }

  writeObject(Res->Image);
  return Error::success();
}

Error ResourceFileWriter::writeIconOrCursorGroupBody(const RCResource *Base) {
  auto *Res = cast<IconCursorGroupResource>(Base);
  writeObject(Res->Header);
  for (auto Item : Res->ItemEntries) {
    writeObject(Item);
    writeInt(IconCursorID++);
  }
  return Error::success();
}

Error ResourceFileWriter::visitSingleIconOrCursor(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeSingleIconOrCursorBody);
}

Error ResourceFileWriter::visitIconOrCursorGroup(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeIconOrCursorGroupBody);
}

Error ResourceFileWriter::visitIconOrCursorResource(const RCResource *Base) {
  IconCursorGroupType Type;
  StringRef FileStr;
  IntOrString ResName = Base->ResName;

  if (auto *IconRes = dyn_cast<IconResource>(Base)) {
    FileStr = IconRes->IconLoc;
    Type = IconCursorGroupType::Icon;
  } else {
    auto *CursorRes = cast<CursorResource>(Base);
    FileStr = CursorRes->CursorLoc;
    Type = IconCursorGroupType::Cursor;
  }

  bool IsLong;
  stripQuotes(FileStr, IsLong);
  auto File = loadFile(FileStr);

  if (!File)
    return File.takeError();

  BinaryStreamReader Reader((*File)->getBuffer(), llvm::endianness::little);

  // Read the file headers.
  //   - At the beginning, ICONDIR/NEWHEADER header.
  //   - Then, a number of RESDIR headers follow. These contain offsets
  //       to data.
  const GroupIconDir *Header;

  RETURN_IF_ERROR(Reader.readObject(Header));
  if (Header->Reserved != 0)
    return createError("Incorrect icon/cursor Reserved field; should be 0.");
  uint16_t NeededType = Type == IconCursorGroupType::Icon ? 1 : 2;
  if (Header->ResType != NeededType)
    return createError("Incorrect icon/cursor ResType field; should be " +
                       Twine(NeededType) + ".");

  uint16_t NumItems = Header->ResCount;

  // Read single ico/cur headers.
  std::vector<ResourceDirEntryStart> ItemEntries;
  ItemEntries.reserve(NumItems);
  std::vector<uint32_t> ItemOffsets(NumItems);
  for (size_t ID = 0; ID < NumItems; ++ID) {
    const ResourceDirEntryStart *Object;
    RETURN_IF_ERROR(Reader.readObject(Object));
    ItemEntries.push_back(*Object);
    RETURN_IF_ERROR(Reader.readInteger(ItemOffsets[ID]));
  }

  // Now write each icon/cursors one by one. At first, all the contents
  // without ICO/CUR header. This is described by SingleIconCursorResource.
  for (size_t ID = 0; ID < NumItems; ++ID) {
    // Load the fragment of file.
    Reader.setOffset(ItemOffsets[ID]);
    ArrayRef<uint8_t> Image;
    RETURN_IF_ERROR(Reader.readArray(Image, ItemEntries[ID].Size));
    SingleIconCursorResource SingleRes(Type, ItemEntries[ID], Image,
                                       Base->MemoryFlags);
    SingleRes.setName(IconCursorID + ID);
    RETURN_IF_ERROR(visitSingleIconOrCursor(&SingleRes));
  }

  // Now, write all the headers concatenated into a separate resource.
  for (size_t ID = 0; ID < NumItems; ++ID) {
    // We need to rewrite the cursor headers, and fetch actual values
    // for Planes/BitCount.
    const auto &OldHeader = ItemEntries[ID];
    ResourceDirEntryStart NewHeader = OldHeader;

    if (Type == IconCursorGroupType::Cursor) {
      NewHeader.Cursor.Width = OldHeader.Icon.Width;
      // Each cursor in fact stores two bitmaps, one under another.
      // Height provided in cursor definition describes the height of the
      // cursor, whereas the value existing in resource definition describes
      // the height of the bitmap. Therefore, we need to double this height.
      NewHeader.Cursor.Height = OldHeader.Icon.Height * 2;

      // Two WORDs were written at the beginning of the resource (hotspot
      // location). This is reflected in Size field.
      NewHeader.Size += 2 * sizeof(uint16_t);
    }

    // Now, we actually need to read the bitmap header to find
    // the number of planes and the number of bits per pixel.
    Reader.setOffset(ItemOffsets[ID]);
    const BitmapInfoHeader *BMPHeader;
    RETURN_IF_ERROR(Reader.readObject(BMPHeader));
    if (BMPHeader->Size == sizeof(BitmapInfoHeader)) {
      NewHeader.Planes = BMPHeader->Planes;
      NewHeader.BitCount = BMPHeader->BitCount;
    } else {
      // A PNG .ico file.
      // https://blogs.msdn.microsoft.com/oldnewthing/20101022-00/?p=12473
      // "The image must be in 32bpp"
      NewHeader.Planes = 1;
      NewHeader.BitCount = 32;
    }

    ItemEntries[ID] = NewHeader;
  }

  IconCursorGroupResource HeaderRes(Type, *Header, std::move(ItemEntries));
  HeaderRes.setName(ResName);
  if (Base->MemoryFlags & MfPreload) {
    HeaderRes.MemoryFlags |= MfPreload;
    HeaderRes.MemoryFlags &= ~MfPure;
  }
  RETURN_IF_ERROR(visitIconOrCursorGroup(&HeaderRes));

  return Error::success();
}

// --- DialogResource helpers. --- //

Error ResourceFileWriter::writeSingleDialogControl(const Control &Ctl,
                                                   bool IsExtended) {
  // Each control should be aligned to DWORD.
  padStream(sizeof(uint32_t));

  auto TypeInfo = Control::SupportedCtls.lookup(Ctl.Type);
  IntWithNotMask CtlStyle(TypeInfo.Style);
  CtlStyle |= Ctl.Style.value_or(RCInt(0));
  uint32_t CtlExtStyle = Ctl.ExtStyle.value_or(0);

  // DIALOG(EX) item header prefix.
  if (!IsExtended) {
    struct {
      ulittle32_t Style;
      ulittle32_t ExtStyle;
    } Prefix{ulittle32_t(CtlStyle.getValue()), ulittle32_t(CtlExtStyle)};
    writeObject(Prefix);
  } else {
    struct {
      ulittle32_t HelpID;
      ulittle32_t ExtStyle;
      ulittle32_t Style;
    } Prefix{ulittle32_t(Ctl.HelpID.value_or(0)), ulittle32_t(CtlExtStyle),
             ulittle32_t(CtlStyle.getValue())};
    writeObject(Prefix);
  }

  // Common fixed-length part.
  RETURN_IF_ERROR(checkSignedNumberFits<int16_t>(
      Ctl.X, "Dialog control x-coordinate", true));
  RETURN_IF_ERROR(checkSignedNumberFits<int16_t>(
      Ctl.Y, "Dialog control y-coordinate", true));
  RETURN_IF_ERROR(
      checkSignedNumberFits<int16_t>(Ctl.Width, "Dialog control width", false));
  RETURN_IF_ERROR(checkSignedNumberFits<int16_t>(
      Ctl.Height, "Dialog control height", false));
  struct {
    ulittle16_t X;
    ulittle16_t Y;
    ulittle16_t Width;
    ulittle16_t Height;
  } Middle{ulittle16_t(Ctl.X), ulittle16_t(Ctl.Y), ulittle16_t(Ctl.Width),
           ulittle16_t(Ctl.Height)};
  writeObject(Middle);

  // ID; it's 16-bit in DIALOG and 32-bit in DIALOGEX.
  if (!IsExtended) {
    // It's common to use -1, i.e. UINT32_MAX, for controls one doesn't
    // want to refer to later.
    if (Ctl.ID != static_cast<uint32_t>(-1))
      RETURN_IF_ERROR(checkNumberFits<uint16_t>(
          Ctl.ID, "Control ID in simple DIALOG resource"));
    writeInt<uint16_t>(Ctl.ID);
  } else {
    writeInt<uint32_t>(Ctl.ID);
  }

  // Window class - either 0xFFFF + 16-bit integer or a string.
  RETURN_IF_ERROR(writeIntOrString(Ctl.Class));

  // Element caption/reference ID. ID is preceded by 0xFFFF.
  RETURN_IF_ERROR(checkIntOrString(Ctl.Title, "Control reference ID"));
  RETURN_IF_ERROR(writeIntOrString(Ctl.Title));

  // # bytes of extra creation data count. Don't pass any.
  writeInt<uint16_t>(0);

  return Error::success();
}

Error ResourceFileWriter::writeDialogBody(const RCResource *Base) {
  auto *Res = cast<DialogResource>(Base);

  // Default style: WS_POPUP | WS_BORDER | WS_SYSMENU.
  const uint32_t DefaultStyle = 0x80880000;
  const uint32_t StyleFontFlag = 0x40;
  const uint32_t StyleCaptionFlag = 0x00C00000;

  uint32_t UsedStyle = ObjectData.Style.value_or(DefaultStyle);
  if (ObjectData.Font)
    UsedStyle |= StyleFontFlag;
  else
    UsedStyle &= ~StyleFontFlag;

  // Actually, in case of empty (but existent) caption, the examined field
  // is equal to "\"\"". That's why empty captions are still noticed.
  if (ObjectData.Caption != "")
    UsedStyle |= StyleCaptionFlag;

  const uint16_t DialogExMagic = 0xFFFF;
  uint32_t ExStyle = ObjectData.ExStyle.value_or(0);

  // Write DIALOG(EX) header prefix. These are pretty different.
  if (!Res->IsExtended) {
    // We cannot let the higher word of DefaultStyle be equal to 0xFFFF.
    // In such a case, whole object (in .res file) is equivalent to a
    // DIALOGEX. It might lead to access violation/segmentation fault in
    // resource readers. For example,
    //   1 DIALOG 0, 0, 0, 65432
    //   STYLE 0xFFFF0001 {}
    // would be compiled to a DIALOGEX with 65432 controls.
    if ((UsedStyle >> 16) == DialogExMagic)
      return createError("16 higher bits of DIALOG resource style cannot be"
                         " equal to 0xFFFF");

    struct {
      ulittle32_t Style;
      ulittle32_t ExtStyle;
    } Prefix{ulittle32_t(UsedStyle),
             ulittle32_t(ExStyle)};

    writeObject(Prefix);
  } else {
    struct {
      ulittle16_t Version;
      ulittle16_t Magic;
      ulittle32_t HelpID;
      ulittle32_t ExtStyle;
      ulittle32_t Style;
    } Prefix{ulittle16_t(1), ulittle16_t(DialogExMagic),
             ulittle32_t(Res->HelpID), ulittle32_t(ExStyle), ulittle32_t(UsedStyle)};

    writeObject(Prefix);
  }

  // Now, a common part. First, fixed-length fields.
  RETURN_IF_ERROR(checkNumberFits<uint16_t>(Res->Controls.size(),
                                            "Number of dialog controls"));
  RETURN_IF_ERROR(
      checkSignedNumberFits<int16_t>(Res->X, "Dialog x-coordinate", true));
  RETURN_IF_ERROR(
      checkSignedNumberFits<int16_t>(Res->Y, "Dialog y-coordinate", true));
  RETURN_IF_ERROR(
      checkSignedNumberFits<int16_t>(Res->Width, "Dialog width", false));
  RETURN_IF_ERROR(
      checkSignedNumberFits<int16_t>(Res->Height, "Dialog height", false));
  struct {
    ulittle16_t Count;
    ulittle16_t PosX;
    ulittle16_t PosY;
    ulittle16_t DialogWidth;
    ulittle16_t DialogHeight;
  } Middle{ulittle16_t(Res->Controls.size()), ulittle16_t(Res->X),
           ulittle16_t(Res->Y), ulittle16_t(Res->Width),
           ulittle16_t(Res->Height)};
  writeObject(Middle);

  // MENU field.
  RETURN_IF_ERROR(writeIntOrString(ObjectData.Menu));

  // Window CLASS field.
  RETURN_IF_ERROR(writeIntOrString(ObjectData.Class));

  // Window title or a single word equal to 0.
  RETURN_IF_ERROR(writeCString(ObjectData.Caption));

  // If there *is* a window font declared, output its data.
  auto &Font = ObjectData.Font;
  if (Font) {
    writeInt<uint16_t>(Font->Size);
    // Additional description occurs only in DIALOGEX.
    if (Res->IsExtended) {
      writeInt<uint16_t>(Font->Weight);
      writeInt<uint8_t>(Font->IsItalic);
      writeInt<uint8_t>(Font->Charset);
    }
    RETURN_IF_ERROR(writeCString(Font->Typeface));
  }

  auto handleCtlError = [&](Error &&Err, const Control &Ctl) -> Error {
    if (!Err)
      return Error::success();
    return joinErrors(createError("Error in " + Twine(Ctl.Type) +
                                  " control  (ID " + Twine(Ctl.ID) + "):"),
                      std::move(Err));
  };

  for (auto &Ctl : Res->Controls)
    RETURN_IF_ERROR(
        handleCtlError(writeSingleDialogControl(Ctl, Res->IsExtended), Ctl));

  return Error::success();
}

// --- HTMLResource helpers. --- //

Error ResourceFileWriter::writeHTMLBody(const RCResource *Base) {
  return appendFile(cast<HTMLResource>(Base)->HTMLLoc);
}

// --- MenuResource helpers. --- //

Error ResourceFileWriter::writeMenuDefinition(
    const std::unique_ptr<MenuDefinition> &Def, uint16_t Flags) {
  // https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-menuitemtemplate
  assert(Def);
  const MenuDefinition *DefPtr = Def.get();

  if (auto *MenuItemPtr = dyn_cast<MenuItem>(DefPtr)) {
    writeInt<uint16_t>(Flags);
    // Some resource files use -1, i.e. UINT32_MAX, for empty menu items.
    if (MenuItemPtr->Id != static_cast<uint32_t>(-1))
      RETURN_IF_ERROR(
          checkNumberFits<uint16_t>(MenuItemPtr->Id, "MENUITEM action ID"));
    writeInt<uint16_t>(MenuItemPtr->Id);
    RETURN_IF_ERROR(writeCString(MenuItemPtr->Name));
    return Error::success();
  }

  if (isa<MenuSeparator>(DefPtr)) {
    writeInt<uint16_t>(Flags);
    writeInt<uint32_t>(0);
    return Error::success();
  }

  auto *PopupPtr = cast<PopupItem>(DefPtr);
  writeInt<uint16_t>(Flags);
  RETURN_IF_ERROR(writeCString(PopupPtr->Name));
  return writeMenuDefinitionList(PopupPtr->SubItems);
}

Error ResourceFileWriter::writeMenuExDefinition(
    const std::unique_ptr<MenuDefinition> &Def, uint16_t Flags) {
  // https://learn.microsoft.com/en-us/windows/win32/menurc/menuex-template-item
  assert(Def);
  const MenuDefinition *DefPtr = Def.get();

  padStream(sizeof(uint32_t));
  if (auto *MenuItemPtr = dyn_cast<MenuExItem>(DefPtr)) {
    writeInt<uint32_t>(MenuItemPtr->Type);
    writeInt<uint32_t>(MenuItemPtr->State);
    writeInt<uint32_t>(MenuItemPtr->Id);
    writeInt<uint16_t>(Flags);
    padStream(sizeof(uint16_t));
    RETURN_IF_ERROR(writeCString(MenuItemPtr->Name));
    return Error::success();
  }

  auto *PopupPtr = cast<PopupExItem>(DefPtr);
  writeInt<uint32_t>(PopupPtr->Type);
  writeInt<uint32_t>(PopupPtr->State);
  writeInt<uint32_t>(PopupPtr->Id);
  writeInt<uint16_t>(Flags);
  padStream(sizeof(uint16_t));
  RETURN_IF_ERROR(writeCString(PopupPtr->Name));
  writeInt<uint32_t>(PopupPtr->HelpId);
  return writeMenuExDefinitionList(PopupPtr->SubItems);
}

Error ResourceFileWriter::writeMenuDefinitionList(
    const MenuDefinitionList &List) {
  for (auto &Def : List.Definitions) {
    uint16_t Flags = Def->getResFlags();
    // Last element receives an additional 0x80 flag.
    const uint16_t LastElementFlag = 0x0080;
    if (&Def == &List.Definitions.back())
      Flags |= LastElementFlag;

    RETURN_IF_ERROR(writeMenuDefinition(Def, Flags));
  }
  return Error::success();
}

Error ResourceFileWriter::writeMenuExDefinitionList(
    const MenuDefinitionList &List) {
  for (auto &Def : List.Definitions) {
    uint16_t Flags = Def->getResFlags();
    // Last element receives an additional 0x80 flag.
    const uint16_t LastElementFlag = 0x0080;
    if (&Def == &List.Definitions.back())
      Flags |= LastElementFlag;

    RETURN_IF_ERROR(writeMenuExDefinition(Def, Flags));
  }
  return Error::success();
}

Error ResourceFileWriter::writeMenuBody(const RCResource *Base) {
  // At first, MENUHEADER structure. In fact, these are two WORDs equal to 0.
  // Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648018.aspx
  writeInt<uint32_t>(0);

  return writeMenuDefinitionList(cast<MenuResource>(Base)->Elements);
}

Error ResourceFileWriter::writeMenuExBody(const RCResource *Base) {
  // At first, MENUEX_TEMPLATE_HEADER structure.
  // Ref:
  // https://learn.microsoft.com/en-us/windows/win32/menurc/menuex-template-header
  writeInt<uint16_t>(1);
  writeInt<uint16_t>(4);
  writeInt<uint32_t>(0);

  return writeMenuExDefinitionList(cast<MenuExResource>(Base)->Elements);
}

// --- StringTableResource helpers. --- //

class BundleResource : public RCResource {
public:
  using BundleType = ResourceFileWriter::StringTableInfo::Bundle;
  BundleType Bundle;

  BundleResource(const BundleType &StrBundle)
      : RCResource(StrBundle.MemoryFlags), Bundle(StrBundle) {}
  IntOrString getResourceType() const override { return 6; }

  ResourceKind getKind() const override { return RkStringTableBundle; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkStringTableBundle;
  }
  Twine getResourceTypeName() const override { return "STRINGTABLE"; }
};

Error ResourceFileWriter::visitStringTableBundle(const RCResource *Res) {
  return writeResource(Res, &ResourceFileWriter::writeStringTableBundleBody);
}

Error ResourceFileWriter::insertStringIntoBundle(
    StringTableInfo::Bundle &Bundle, uint16_t StringID,
    const std::vector<StringRef> &String) {
  uint16_t StringLoc = StringID & 15;
  if (Bundle.Data[StringLoc])
    return createError("Multiple STRINGTABLE strings located under ID " +
                       Twine(StringID));
  Bundle.Data[StringLoc] = String;
  return Error::success();
}

Error ResourceFileWriter::writeStringTableBundleBody(const RCResource *Base) {
  auto *Res = cast<BundleResource>(Base);
  for (size_t ID = 0; ID < Res->Bundle.Data.size(); ++ID) {
    // The string format is a tiny bit different here. We
    // first output the size of the string, and then the string itself
    // (which is not null-terminated).
    SmallVector<UTF16, 128> Data;
    if (Res->Bundle.Data[ID]) {
      bool IsLongString;
      for (StringRef S : *Res->Bundle.Data[ID])
        RETURN_IF_ERROR(processString(S, NullHandlingMethod::CutAtDoubleNull,
                                      IsLongString, Data, Params.CodePage));
      if (AppendNull)
        Data.push_back('\0');
    }
    RETURN_IF_ERROR(
        checkNumberFits<uint16_t>(Data.size(), "STRINGTABLE string size"));
    writeInt<uint16_t>(Data.size());
    for (auto Char : Data)
      writeInt(Char);
  }
  return Error::success();
}

Error ResourceFileWriter::dumpAllStringTables() {
  for (auto Key : StringTableData.BundleList) {
    auto Iter = StringTableData.BundleData.find(Key);
    assert(Iter != StringTableData.BundleData.end());

    // For a moment, revert the context info to moment of bundle declaration.
    ContextKeeper RAII(this);
    ObjectData = Iter->second.DeclTimeInfo;

    BundleResource Res(Iter->second);
    // Bundle #(k+1) contains keys [16k, 16k + 15].
    Res.setName(Key.first + 1);
    RETURN_IF_ERROR(visitStringTableBundle(&Res));
  }
  return Error::success();
}

// --- UserDefinedResource helpers. --- //

Error ResourceFileWriter::writeUserDefinedBody(const RCResource *Base) {
  auto *Res = cast<UserDefinedResource>(Base);

  if (Res->IsFileResource)
    return appendFile(Res->FileLoc);

  for (auto &Elem : Res->Contents) {
    if (Elem.isInt()) {
      RETURN_IF_ERROR(
          checkRCInt(Elem.getInt(), "Number in user-defined resource"));
      writeRCInt(Elem.getInt());
      continue;
    }

    SmallVector<UTF16, 128> ProcessedString;
    bool IsLongString;
    RETURN_IF_ERROR(
        processString(Elem.getString(), NullHandlingMethod::UserResource,
                      IsLongString, ProcessedString, Params.CodePage));

    for (auto Ch : ProcessedString) {
      if (IsLongString) {
        writeInt(Ch);
        continue;
      }

      RETURN_IF_ERROR(checkNumberFits<uint8_t>(
          Ch, "Character in narrow string in user-defined resource"));
      writeInt<uint8_t>(Ch);
    }
  }

  return Error::success();
}

// --- VersionInfoResourceResource helpers. --- //

Error ResourceFileWriter::writeVersionInfoBlock(const VersionInfoBlock &Blk) {
  // Output the header if the block has name.
  bool OutputHeader = Blk.Name != "";
  uint64_t LengthLoc;

  padStream(sizeof(uint32_t));
  if (OutputHeader) {
    LengthLoc = writeInt<uint16_t>(0);
    writeInt<uint16_t>(0);
    writeInt<uint16_t>(1); // true
    RETURN_IF_ERROR(writeCString(Blk.Name));
    padStream(sizeof(uint32_t));
  }

  for (const std::unique_ptr<VersionInfoStmt> &Item : Blk.Stmts) {
    VersionInfoStmt *ItemPtr = Item.get();

    if (auto *BlockPtr = dyn_cast<VersionInfoBlock>(ItemPtr)) {
      RETURN_IF_ERROR(writeVersionInfoBlock(*BlockPtr));
      continue;
    }

    auto *ValuePtr = cast<VersionInfoValue>(ItemPtr);
    RETURN_IF_ERROR(writeVersionInfoValue(*ValuePtr));
  }

  if (OutputHeader) {
    uint64_t CurLoc = tell();
    writeObjectAt(ulittle16_t(CurLoc - LengthLoc), LengthLoc);
  }

  return Error::success();
}

Error ResourceFileWriter::writeVersionInfoValue(const VersionInfoValue &Val) {
  // rc has a peculiar algorithm to output VERSIONINFO VALUEs. Each VALUE
  // is a mapping from the key (string) to the value (a sequence of ints or
  // a sequence of strings).
  //
  // If integers are to be written: width of each integer written depends on
  // whether it's been declared 'long' (it's DWORD then) or not (it's WORD).
  // ValueLength defined in structure referenced below is then the total
  // number of bytes taken by these integers.
  //
  // If strings are to be written: characters are always WORDs.
  // Moreover, '\0' character is written after the last string, and between
  // every two strings separated by comma (if strings are not comma-separated,
  // they're simply concatenated). ValueLength is equal to the number of WORDs
  // written (that is, half of the bytes written).
  //
  // Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms646994.aspx
  bool HasStrings = false, HasInts = false;
  for (auto &Item : Val.Values)
    (Item.isInt() ? HasInts : HasStrings) = true;

  assert((HasStrings || HasInts) && "VALUE must have at least one argument");
  if (HasStrings && HasInts)
    return createError(Twine("VALUE ") + Val.Key +
                       " cannot contain both strings and integers");

  padStream(sizeof(uint32_t));
  auto LengthLoc = writeInt<uint16_t>(0);
  auto ValLengthLoc = writeInt<uint16_t>(0);
  writeInt<uint16_t>(HasStrings);
  RETURN_IF_ERROR(writeCString(Val.Key));
  padStream(sizeof(uint32_t));

  auto DataLoc = tell();
  for (size_t Id = 0; Id < Val.Values.size(); ++Id) {
    auto &Item = Val.Values[Id];
    if (Item.isInt()) {
      auto Value = Item.getInt();
      RETURN_IF_ERROR(checkRCInt(Value, "VERSIONINFO integer value"));
      writeRCInt(Value);
      continue;
    }

    bool WriteTerminator =
        Id == Val.Values.size() - 1 || Val.HasPrecedingComma[Id + 1];
    RETURN_IF_ERROR(writeCString(Item.getString(), WriteTerminator));
  }

  auto CurLoc = tell();
  auto ValueLength = CurLoc - DataLoc;
  if (HasStrings) {
    assert(ValueLength % 2 == 0);
    ValueLength /= 2;
  }
  writeObjectAt(ulittle16_t(CurLoc - LengthLoc), LengthLoc);
  writeObjectAt(ulittle16_t(ValueLength), ValLengthLoc);
  return Error::success();
}

template <typename Ty>
static Ty getWithDefault(const StringMap<Ty> &Map, StringRef Key,
                         const Ty &Default) {
  auto Iter = Map.find(Key);
  if (Iter != Map.end())
    return Iter->getValue();
  return Default;
}

Error ResourceFileWriter::writeVersionInfoBody(const RCResource *Base) {
  auto *Res = cast<VersionInfoResource>(Base);

  const auto &FixedData = Res->FixedData;

  struct /* VS_FIXEDFILEINFO */ {
    ulittle32_t Signature = ulittle32_t(0xFEEF04BD);
    ulittle32_t StructVersion = ulittle32_t(0x10000);
    // It's weird to have most-significant DWORD first on the little-endian
    // machines, but let it be this way.
    ulittle32_t FileVersionMS;
    ulittle32_t FileVersionLS;
    ulittle32_t ProductVersionMS;
    ulittle32_t ProductVersionLS;
    ulittle32_t FileFlagsMask;
    ulittle32_t FileFlags;
    ulittle32_t FileOS;
    ulittle32_t FileType;
    ulittle32_t FileSubtype;
    // MS implementation seems to always set these fields to 0.
    ulittle32_t FileDateMS = ulittle32_t(0);
    ulittle32_t FileDateLS = ulittle32_t(0);
  } FixedInfo;

  // First, VS_VERSIONINFO.
  auto LengthLoc = writeInt<uint16_t>(0);
  writeInt<uint16_t>(sizeof(FixedInfo));
  writeInt<uint16_t>(0);
  cantFail(writeCString("VS_VERSION_INFO"));
  padStream(sizeof(uint32_t));

  using VersionInfoFixed = VersionInfoResource::VersionInfoFixed;
  auto GetField = [&](VersionInfoFixed::VersionInfoFixedType Type) {
    static const SmallVector<uint32_t, 4> DefaultOut{0, 0, 0, 0};
    if (!FixedData.IsTypePresent[(int)Type])
      return DefaultOut;
    return FixedData.FixedInfo[(int)Type];
  };

  auto FileVer = GetField(VersionInfoFixed::FtFileVersion);
  RETURN_IF_ERROR(checkNumberFits<uint16_t>(*llvm::max_element(FileVer),
                                            "FILEVERSION fields"));
  FixedInfo.FileVersionMS = (FileVer[0] << 16) | FileVer[1];
  FixedInfo.FileVersionLS = (FileVer[2] << 16) | FileVer[3];

  auto ProdVer = GetField(VersionInfoFixed::FtProductVersion);
  RETURN_IF_ERROR(checkNumberFits<uint16_t>(*llvm::max_element(ProdVer),
                                            "PRODUCTVERSION fields"));
  FixedInfo.ProductVersionMS = (ProdVer[0] << 16) | ProdVer[1];
  FixedInfo.ProductVersionLS = (ProdVer[2] << 16) | ProdVer[3];

  FixedInfo.FileFlagsMask = GetField(VersionInfoFixed::FtFileFlagsMask)[0];
  FixedInfo.FileFlags = GetField(VersionInfoFixed::FtFileFlags)[0];
  FixedInfo.FileOS = GetField(VersionInfoFixed::FtFileOS)[0];
  FixedInfo.FileType = GetField(VersionInfoFixed::FtFileType)[0];
  FixedInfo.FileSubtype = GetField(VersionInfoFixed::FtFileSubtype)[0];

  writeObject(FixedInfo);
  padStream(sizeof(uint32_t));

  RETURN_IF_ERROR(writeVersionInfoBlock(Res->MainBlock));

  // FIXME: check overflow?
  writeObjectAt(ulittle16_t(tell() - LengthLoc), LengthLoc);

  return Error::success();
}

Expected<std::unique_ptr<MemoryBuffer>>
ResourceFileWriter::loadFile(StringRef File) const {
  SmallString<128> Path;
  SmallString<128> Cwd;
  std::unique_ptr<MemoryBuffer> Result;

  // 0. The file path is absolute or has a root directory, so we shouldn't
  // try to append it on top of other base directories. (An absolute path
  // must have a root directory, but e.g. the path "\dir\file" on windows
  // isn't considered absolute, but it does have a root directory. As long as
  // sys::path::append doesn't handle appending an absolute path or a path
  // starting with a root directory on top of a base, we must handle this
  // case separately at the top. C++17's path::append handles that case
  // properly though, so if using that to append paths below, this early
  // exception case could be removed.)
  if (sys::path::has_root_directory(File))
    return errorOrToExpected(MemoryBuffer::getFile(
        File, /*IsText=*/false, /*RequiresNullTerminator=*/false));

  // 1. The current working directory.
  sys::fs::current_path(Cwd);
  Path.assign(Cwd.begin(), Cwd.end());
  sys::path::append(Path, File);
  if (sys::fs::exists(Path))
    return errorOrToExpected(MemoryBuffer::getFile(
        Path, /*IsText=*/false, /*RequiresNullTerminator=*/false));

  // 2. The directory of the input resource file, if it is different from the
  // current working directory.
  StringRef InputFileDir = sys::path::parent_path(Params.InputFilePath);
  Path.assign(InputFileDir.begin(), InputFileDir.end());
  sys::path::append(Path, File);
  if (sys::fs::exists(Path))
    return errorOrToExpected(MemoryBuffer::getFile(
        Path, /*IsText=*/false, /*RequiresNullTerminator=*/false));

  // 3. All of the include directories specified on the command line.
  for (StringRef ForceInclude : Params.Include) {
    Path.assign(ForceInclude.begin(), ForceInclude.end());
    sys::path::append(Path, File);
    if (sys::fs::exists(Path))
      return errorOrToExpected(MemoryBuffer::getFile(
          Path, /*IsText=*/false, /*RequiresNullTerminator=*/false));
  }

  if (!Params.NoInclude) {
    if (auto Result = llvm::sys::Process::FindInEnvPath("INCLUDE", File))
      return errorOrToExpected(MemoryBuffer::getFile(
          *Result, /*IsText=*/false, /*RequiresNullTerminator=*/false));
  }

  return make_error<StringError>("error : file not found : " + Twine(File),
                                 inconvertibleErrorCode());
}

} // namespace rc
} // namespace llvm
