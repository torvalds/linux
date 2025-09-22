//= ScanfFormatString.cpp - Analysis of printf format strings --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Handling of format string in scanf and friends.  The structure of format
// strings for fscanf() are described in C99 7.19.6.2.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/FormatString.h"
#include "FormatStringParsing.h"
#include "clang/Basic/TargetInfo.h"

using clang::analyze_format_string::ArgType;
using clang::analyze_format_string::FormatStringHandler;
using clang::analyze_format_string::LengthModifier;
using clang::analyze_format_string::OptionalAmount;
using clang::analyze_format_string::ConversionSpecifier;
using clang::analyze_scanf::ScanfConversionSpecifier;
using clang::analyze_scanf::ScanfSpecifier;
using clang::UpdateOnReturn;
using namespace clang;

typedef clang::analyze_format_string::SpecifierResult<ScanfSpecifier>
        ScanfSpecifierResult;

static bool ParseScanList(FormatStringHandler &H,
                          ScanfConversionSpecifier &CS,
                          const char *&Beg, const char *E) {
  const char *I = Beg;
  const char *start = I - 1;
  UpdateOnReturn <const char*> UpdateBeg(Beg, I);

  // No more characters?
  if (I == E) {
    H.HandleIncompleteScanList(start, I);
    return true;
  }

  // Special case: ']' is the first character.
  if (*I == ']') {
    if (++I == E) {
      H.HandleIncompleteScanList(start, I - 1);
      return true;
    }
  }

  // Special case: "^]" are the first characters.
  if (I + 1 != E && I[0] == '^' && I[1] == ']') {
    I += 2;
    if (I == E) {
      H.HandleIncompleteScanList(start, I - 1);
      return true;
    }
  }

  // Look for a ']' character which denotes the end of the scan list.
  while (*I != ']') {
    if (++I == E) {
      H.HandleIncompleteScanList(start, I - 1);
      return true;
    }
  }

  CS.setEndScanList(I);
  return false;
}

// FIXME: Much of this is copy-paste from ParsePrintfSpecifier.
// We can possibly refactor.
static ScanfSpecifierResult ParseScanfSpecifier(FormatStringHandler &H,
                                                const char *&Beg,
                                                const char *E,
                                                unsigned &argIndex,
                                                const LangOptions &LO,
                                                const TargetInfo &Target) {
  using namespace clang::analyze_format_string;
  using namespace clang::analyze_scanf;
  const char *I = Beg;
  const char *Start = nullptr;
  UpdateOnReturn <const char*> UpdateBeg(Beg, I);

    // Look for a '%' character that indicates the start of a format specifier.
  for ( ; I != E ; ++I) {
    char c = *I;
    if (c == '\0') {
        // Detect spurious null characters, which are likely errors.
      H.HandleNullChar(I);
      return true;
    }
    if (c == '%') {
      Start = I++;  // Record the start of the format specifier.
      break;
    }
  }

    // No format specifier found?
  if (!Start)
    return false;

  if (I == E) {
      // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  ScanfSpecifier FS;
  if (ParseArgPosition(H, FS, Start, I, E))
    return true;

  if (I == E) {
      // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  // Look for '*' flag if it is present.
  if (*I == '*') {
    FS.setSuppressAssignment(I);
    if (++I == E) {
      H.HandleIncompleteSpecifier(Start, E - Start);
      return true;
    }
  }

  // Look for the field width (if any).  Unlike printf, this is either
  // a fixed integer or isn't present.
  const OptionalAmount &Amt = clang::analyze_format_string::ParseAmount(I, E);
  if (Amt.getHowSpecified() != OptionalAmount::NotSpecified) {
    assert(Amt.getHowSpecified() == OptionalAmount::Constant);
    FS.setFieldWidth(Amt);

    if (I == E) {
      // No more characters left?
      H.HandleIncompleteSpecifier(Start, E - Start);
      return true;
    }
  }

  // Look for the length modifier.
  if (ParseLengthModifier(FS, I, E, LO, /*IsScanf=*/true) && I == E) {
      // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  // Detect spurious null characters, which are likely errors.
  if (*I == '\0') {
    H.HandleNullChar(I);
    return true;
  }

  // Finally, look for the conversion specifier.
  const char *conversionPosition = I++;
  ScanfConversionSpecifier::Kind k = ScanfConversionSpecifier::InvalidSpecifier;
  switch (*conversionPosition) {
    default:
      break;
    case '%': k = ConversionSpecifier::PercentArg;   break;
    case 'b': k = ConversionSpecifier::bArg; break;
    case 'A': k = ConversionSpecifier::AArg; break;
    case 'E': k = ConversionSpecifier::EArg; break;
    case 'F': k = ConversionSpecifier::FArg; break;
    case 'G': k = ConversionSpecifier::GArg; break;
    case 'X': k = ConversionSpecifier::XArg; break;
    case 'a': k = ConversionSpecifier::aArg; break;
    case 'd': k = ConversionSpecifier::dArg; break;
    case 'e': k = ConversionSpecifier::eArg; break;
    case 'f': k = ConversionSpecifier::fArg; break;
    case 'g': k = ConversionSpecifier::gArg; break;
    case 'i': k = ConversionSpecifier::iArg; break;
    case 'n': k = ConversionSpecifier::nArg; break;
    case 'c': k = ConversionSpecifier::cArg; break;
    case 'C': k = ConversionSpecifier::CArg; break;
    case 'S': k = ConversionSpecifier::SArg; break;
    case '[': k = ConversionSpecifier::ScanListArg; break;
    case 'u': k = ConversionSpecifier::uArg; break;
    case 'x': k = ConversionSpecifier::xArg; break;
    case 'o': k = ConversionSpecifier::oArg; break;
    case 's': k = ConversionSpecifier::sArg; break;
    case 'p': k = ConversionSpecifier::pArg; break;
    // Apple extensions
      // Apple-specific
    case 'D':
      if (Target.getTriple().isOSDarwin())
        k = ConversionSpecifier::DArg;
      break;
    case 'O':
      if (Target.getTriple().isOSDarwin())
        k = ConversionSpecifier::OArg;
      break;
    case 'U':
      if (Target.getTriple().isOSDarwin())
        k = ConversionSpecifier::UArg;
      break;
  }
  ScanfConversionSpecifier CS(conversionPosition, k);
  if (k == ScanfConversionSpecifier::ScanListArg) {
    if (ParseScanList(H, CS, I, E))
      return true;
  }
  FS.setConversionSpecifier(CS);
  if (CS.consumesDataArgument() && !FS.getSuppressAssignment()
      && !FS.usesPositionalArg())
    FS.setArgIndex(argIndex++);

  // FIXME: '%' and '*' doesn't make sense.  Issue a warning.
  // FIXME: 'ConsumedSoFar' and '*' doesn't make sense.

  if (k == ScanfConversionSpecifier::InvalidSpecifier) {
    unsigned Len = I - Beg;
    if (ParseUTF8InvalidSpecifier(Beg, E, Len)) {
      CS.setEndScanList(Beg + Len);
      FS.setConversionSpecifier(CS);
    }
    // Assume the conversion takes one argument.
    return !H.HandleInvalidScanfConversionSpecifier(FS, Beg, Len);
  }
  return ScanfSpecifierResult(Start, FS);
}

ArgType ScanfSpecifier::getArgType(ASTContext &Ctx) const {
  const ScanfConversionSpecifier &CS = getConversionSpecifier();

  if (!CS.consumesDataArgument())
    return ArgType::Invalid();

  switch(CS.getKind()) {
    // Signed int.
    case ConversionSpecifier::dArg:
    case ConversionSpecifier::DArg:
    case ConversionSpecifier::iArg:
      switch (LM.getKind()) {
        case LengthModifier::None:
          return ArgType::PtrTo(Ctx.IntTy);
        case LengthModifier::AsChar:
          return ArgType::PtrTo(ArgType::AnyCharTy);
        case LengthModifier::AsShort:
          return ArgType::PtrTo(Ctx.ShortTy);
        case LengthModifier::AsLong:
          return ArgType::PtrTo(Ctx.LongTy);
        case LengthModifier::AsLongLong:
        case LengthModifier::AsQuad:
          return ArgType::PtrTo(Ctx.LongLongTy);
        case LengthModifier::AsInt64:
          return ArgType::PtrTo(ArgType(Ctx.LongLongTy, "__int64"));
        case LengthModifier::AsIntMax:
          return ArgType::PtrTo(ArgType(Ctx.getIntMaxType(), "intmax_t"));
        case LengthModifier::AsSizeT:
          return ArgType::PtrTo(ArgType(Ctx.getSignedSizeType(), "ssize_t"));
        case LengthModifier::AsPtrDiff:
          return ArgType::PtrTo(ArgType(Ctx.getPointerDiffType(), "ptrdiff_t"));
        case LengthModifier::AsLongDouble:
          // GNU extension.
          return ArgType::PtrTo(Ctx.LongLongTy);
        case LengthModifier::AsAllocate:
        case LengthModifier::AsMAllocate:
        case LengthModifier::AsInt32:
        case LengthModifier::AsInt3264:
        case LengthModifier::AsWide:
        case LengthModifier::AsShortLong:
          return ArgType::Invalid();
      }
      llvm_unreachable("Unsupported LengthModifier Type");

    // Unsigned int.
    case ConversionSpecifier::bArg:
    case ConversionSpecifier::oArg:
    case ConversionSpecifier::OArg:
    case ConversionSpecifier::uArg:
    case ConversionSpecifier::UArg:
    case ConversionSpecifier::xArg:
    case ConversionSpecifier::XArg:
      switch (LM.getKind()) {
        case LengthModifier::None:
          return ArgType::PtrTo(Ctx.UnsignedIntTy);
        case LengthModifier::AsChar:
          return ArgType::PtrTo(Ctx.UnsignedCharTy);
        case LengthModifier::AsShort:
          return ArgType::PtrTo(Ctx.UnsignedShortTy);
        case LengthModifier::AsLong:
          return ArgType::PtrTo(Ctx.UnsignedLongTy);
        case LengthModifier::AsLongLong:
        case LengthModifier::AsQuad:
          return ArgType::PtrTo(Ctx.UnsignedLongLongTy);
        case LengthModifier::AsInt64:
          return ArgType::PtrTo(ArgType(Ctx.UnsignedLongLongTy, "unsigned __int64"));
        case LengthModifier::AsIntMax:
          return ArgType::PtrTo(ArgType(Ctx.getUIntMaxType(), "uintmax_t"));
        case LengthModifier::AsSizeT:
          return ArgType::PtrTo(ArgType(Ctx.getSizeType(), "size_t"));
        case LengthModifier::AsPtrDiff:
          return ArgType::PtrTo(
              ArgType(Ctx.getUnsignedPointerDiffType(), "unsigned ptrdiff_t"));
        case LengthModifier::AsLongDouble:
          // GNU extension.
          return ArgType::PtrTo(Ctx.UnsignedLongLongTy);
        case LengthModifier::AsAllocate:
        case LengthModifier::AsMAllocate:
        case LengthModifier::AsInt32:
        case LengthModifier::AsInt3264:
        case LengthModifier::AsWide:
        case LengthModifier::AsShortLong:
          return ArgType::Invalid();
      }
      llvm_unreachable("Unsupported LengthModifier Type");

    // Float.
    case ConversionSpecifier::aArg:
    case ConversionSpecifier::AArg:
    case ConversionSpecifier::eArg:
    case ConversionSpecifier::EArg:
    case ConversionSpecifier::fArg:
    case ConversionSpecifier::FArg:
    case ConversionSpecifier::gArg:
    case ConversionSpecifier::GArg:
      switch (LM.getKind()) {
        case LengthModifier::None:
          return ArgType::PtrTo(Ctx.FloatTy);
        case LengthModifier::AsLong:
          return ArgType::PtrTo(Ctx.DoubleTy);
        case LengthModifier::AsLongDouble:
          return ArgType::PtrTo(Ctx.LongDoubleTy);
        default:
          return ArgType::Invalid();
      }

    // Char, string and scanlist.
    case ConversionSpecifier::cArg:
    case ConversionSpecifier::sArg:
    case ConversionSpecifier::ScanListArg:
      switch (LM.getKind()) {
        case LengthModifier::None:
          return ArgType::PtrTo(ArgType::AnyCharTy);
        case LengthModifier::AsLong:
        case LengthModifier::AsWide:
          return ArgType::PtrTo(ArgType(Ctx.getWideCharType(), "wchar_t"));
        case LengthModifier::AsAllocate:
        case LengthModifier::AsMAllocate:
          return ArgType::PtrTo(ArgType::CStrTy);
        case LengthModifier::AsShort:
          if (Ctx.getTargetInfo().getTriple().isOSMSVCRT())
            return ArgType::PtrTo(ArgType::AnyCharTy);
          [[fallthrough]];
        default:
          return ArgType::Invalid();
      }
    case ConversionSpecifier::CArg:
    case ConversionSpecifier::SArg:
      // FIXME: Mac OS X specific?
      switch (LM.getKind()) {
        case LengthModifier::None:
        case LengthModifier::AsWide:
          return ArgType::PtrTo(ArgType(Ctx.getWideCharType(), "wchar_t"));
        case LengthModifier::AsAllocate:
        case LengthModifier::AsMAllocate:
          return ArgType::PtrTo(ArgType(ArgType::WCStrTy, "wchar_t *"));
        case LengthModifier::AsShort:
          if (Ctx.getTargetInfo().getTriple().isOSMSVCRT())
            return ArgType::PtrTo(ArgType::AnyCharTy);
          [[fallthrough]];
        default:
          return ArgType::Invalid();
      }

    // Pointer.
    case ConversionSpecifier::pArg:
      return ArgType::PtrTo(ArgType::CPointerTy);

    // Write-back.
    case ConversionSpecifier::nArg:
      switch (LM.getKind()) {
        case LengthModifier::None:
          return ArgType::PtrTo(Ctx.IntTy);
        case LengthModifier::AsChar:
          return ArgType::PtrTo(Ctx.SignedCharTy);
        case LengthModifier::AsShort:
          return ArgType::PtrTo(Ctx.ShortTy);
        case LengthModifier::AsLong:
          return ArgType::PtrTo(Ctx.LongTy);
        case LengthModifier::AsLongLong:
        case LengthModifier::AsQuad:
          return ArgType::PtrTo(Ctx.LongLongTy);
        case LengthModifier::AsInt64:
          return ArgType::PtrTo(ArgType(Ctx.LongLongTy, "__int64"));
        case LengthModifier::AsIntMax:
          return ArgType::PtrTo(ArgType(Ctx.getIntMaxType(), "intmax_t"));
        case LengthModifier::AsSizeT:
          return ArgType::PtrTo(ArgType(Ctx.getSignedSizeType(), "ssize_t"));
        case LengthModifier::AsPtrDiff:
          return ArgType::PtrTo(ArgType(Ctx.getPointerDiffType(), "ptrdiff_t"));
        case LengthModifier::AsLongDouble:
          return ArgType(); // FIXME: Is this a known extension?
        case LengthModifier::AsAllocate:
        case LengthModifier::AsMAllocate:
        case LengthModifier::AsInt32:
        case LengthModifier::AsInt3264:
        case LengthModifier::AsWide:
        case LengthModifier::AsShortLong:
          return ArgType::Invalid();
        }

    default:
      break;
  }

  return ArgType();
}

bool ScanfSpecifier::fixType(QualType QT, QualType RawQT,
                             const LangOptions &LangOpt,
                             ASTContext &Ctx) {

  // %n is different from other conversion specifiers; don't try to fix it.
  if (CS.getKind() == ConversionSpecifier::nArg)
    return false;

  if (!QT->isPointerType())
    return false;

  QualType PT = QT->getPointeeType();

  // If it's an enum, get its underlying type.
  if (const EnumType *ETy = PT->getAs<EnumType>()) {
    // Don't try to fix incomplete enums.
    if (!ETy->getDecl()->isComplete())
      return false;
    PT = ETy->getDecl()->getIntegerType();
  }

  const BuiltinType *BT = PT->getAs<BuiltinType>();
  if (!BT)
    return false;

  // Pointer to a character.
  if (PT->isAnyCharacterType()) {
    CS.setKind(ConversionSpecifier::sArg);
    if (PT->isWideCharType())
      LM.setKind(LengthModifier::AsWideChar);
    else
      LM.setKind(LengthModifier::None);

    // If we know the target array length, we can use it as a field width.
    if (const ConstantArrayType *CAT = Ctx.getAsConstantArrayType(RawQT)) {
      if (CAT->getSizeModifier() == ArraySizeModifier::Normal)
        FieldWidth = OptionalAmount(OptionalAmount::Constant,
                                    CAT->getZExtSize() - 1, "", 0, false);
    }
    return true;
  }

  // Figure out the length modifier.
  switch (BT->getKind()) {
    // no modifier
    case BuiltinType::UInt:
    case BuiltinType::Int:
    case BuiltinType::Float:
      LM.setKind(LengthModifier::None);
      break;

    // hh
    case BuiltinType::Char_U:
    case BuiltinType::UChar:
    case BuiltinType::Char_S:
    case BuiltinType::SChar:
      LM.setKind(LengthModifier::AsChar);
      break;

    // h
    case BuiltinType::Short:
    case BuiltinType::UShort:
      LM.setKind(LengthModifier::AsShort);
      break;

    // l
    case BuiltinType::Long:
    case BuiltinType::ULong:
    case BuiltinType::Double:
      LM.setKind(LengthModifier::AsLong);
      break;

    // ll
    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
      LM.setKind(LengthModifier::AsLongLong);
      break;

    // L
    case BuiltinType::LongDouble:
      LM.setKind(LengthModifier::AsLongDouble);
      break;

    // Don't know.
    default:
      return false;
  }

  // Handle size_t, ptrdiff_t, etc. that have dedicated length modifiers in C99.
  if (LangOpt.C99 || LangOpt.CPlusPlus11)
    namedTypeToLengthModifier(PT, LM);

  // If fixing the length modifier was enough, we are done.
  if (hasValidLengthModifier(Ctx.getTargetInfo(), LangOpt)) {
    const analyze_scanf::ArgType &AT = getArgType(Ctx);
    if (AT.isValid() && AT.matchesType(Ctx, QT))
      return true;
  }

  // Figure out the conversion specifier.
  if (PT->isRealFloatingType())
    CS.setKind(ConversionSpecifier::fArg);
  else if (PT->isSignedIntegerType())
    CS.setKind(ConversionSpecifier::dArg);
  else if (PT->isUnsignedIntegerType())
    CS.setKind(ConversionSpecifier::uArg);
  else
    llvm_unreachable("Unexpected type");

  return true;
}

void ScanfSpecifier::toString(raw_ostream &os) const {
  os << "%";

  if (usesPositionalArg())
    os << getPositionalArgIndex() << "$";
  if (SuppressAssignment)
    os << "*";

  FieldWidth.toString(os);
  os << LM.toString();
  os << CS.toString();
}

bool clang::analyze_format_string::ParseScanfString(FormatStringHandler &H,
                                                    const char *I,
                                                    const char *E,
                                                    const LangOptions &LO,
                                                    const TargetInfo &Target) {

  unsigned argIndex = 0;

  // Keep looking for a format specifier until we have exhausted the string.
  while (I != E) {
    const ScanfSpecifierResult &FSR = ParseScanfSpecifier(H, I, E, argIndex,
                                                          LO, Target);
    // Did a fail-stop error of any kind occur when parsing the specifier?
    // If so, don't do any more processing.
    if (FSR.shouldStop())
      return true;
      // Did we exhaust the string or encounter an error that
      // we can recover from?
    if (!FSR.hasValue())
      continue;
      // We have a format specifier.  Pass it to the callback.
    if (!H.HandleScanfSpecifier(FSR.getValue(), FSR.getStart(),
                                I - FSR.getStart())) {
      return true;
    }
  }
  assert(I == E && "Format string not exhausted");
  return false;
}
