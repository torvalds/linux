// FormatString.cpp - Common stuff for handling printf/scanf formats -*- C++ -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared details for processing format strings of printf and scanf
// (and friends).
//
//===----------------------------------------------------------------------===//

#include "FormatStringParsing.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/Support/ConvertUTF.h"
#include <optional>

using clang::analyze_format_string::ArgType;
using clang::analyze_format_string::FormatStringHandler;
using clang::analyze_format_string::FormatSpecifier;
using clang::analyze_format_string::LengthModifier;
using clang::analyze_format_string::OptionalAmount;
using clang::analyze_format_string::ConversionSpecifier;
using namespace clang;

// Key function to FormatStringHandler.
FormatStringHandler::~FormatStringHandler() {}

//===----------------------------------------------------------------------===//
// Functions for parsing format strings components in both printf and
// scanf format strings.
//===----------------------------------------------------------------------===//

OptionalAmount
clang::analyze_format_string::ParseAmount(const char *&Beg, const char *E) {
  const char *I = Beg;
  UpdateOnReturn <const char*> UpdateBeg(Beg, I);

  unsigned accumulator = 0;
  bool hasDigits = false;

  for ( ; I != E; ++I) {
    char c = *I;
    if (c >= '0' && c <= '9') {
      hasDigits = true;
      accumulator = (accumulator * 10) + (c - '0');
      continue;
    }

    if (hasDigits)
      return OptionalAmount(OptionalAmount::Constant, accumulator, Beg, I - Beg,
          false);

    break;
  }

  return OptionalAmount();
}

OptionalAmount
clang::analyze_format_string::ParseNonPositionAmount(const char *&Beg,
                                                     const char *E,
                                                     unsigned &argIndex) {
  if (*Beg == '*') {
    ++Beg;
    return OptionalAmount(OptionalAmount::Arg, argIndex++, Beg, 0, false);
  }

  return ParseAmount(Beg, E);
}

OptionalAmount
clang::analyze_format_string::ParsePositionAmount(FormatStringHandler &H,
                                                  const char *Start,
                                                  const char *&Beg,
                                                  const char *E,
                                                  PositionContext p) {
  if (*Beg == '*') {
    const char *I = Beg + 1;
    const OptionalAmount &Amt = ParseAmount(I, E);

    if (Amt.getHowSpecified() == OptionalAmount::NotSpecified) {
      H.HandleInvalidPosition(Beg, I - Beg, p);
      return OptionalAmount(false);
    }

    if (I == E) {
      // No more characters left?
      H.HandleIncompleteSpecifier(Start, E - Start);
      return OptionalAmount(false);
    }

    assert(Amt.getHowSpecified() == OptionalAmount::Constant);

    if (*I == '$') {
      // Handle positional arguments

      // Special case: '*0$', since this is an easy mistake.
      if (Amt.getConstantAmount() == 0) {
        H.HandleZeroPosition(Beg, I - Beg + 1);
        return OptionalAmount(false);
      }

      const char *Tmp = Beg;
      Beg = ++I;

      return OptionalAmount(OptionalAmount::Arg, Amt.getConstantAmount() - 1,
                            Tmp, 0, true);
    }

    H.HandleInvalidPosition(Beg, I - Beg, p);
    return OptionalAmount(false);
  }

  return ParseAmount(Beg, E);
}


bool
clang::analyze_format_string::ParseFieldWidth(FormatStringHandler &H,
                                              FormatSpecifier &CS,
                                              const char *Start,
                                              const char *&Beg, const char *E,
                                              unsigned *argIndex) {
  // FIXME: Support negative field widths.
  if (argIndex) {
    CS.setFieldWidth(ParseNonPositionAmount(Beg, E, *argIndex));
  }
  else {
    const OptionalAmount Amt =
      ParsePositionAmount(H, Start, Beg, E,
                          analyze_format_string::FieldWidthPos);

    if (Amt.isInvalid())
      return true;
    CS.setFieldWidth(Amt);
  }
  return false;
}

bool
clang::analyze_format_string::ParseArgPosition(FormatStringHandler &H,
                                               FormatSpecifier &FS,
                                               const char *Start,
                                               const char *&Beg,
                                               const char *E) {
  const char *I = Beg;

  const OptionalAmount &Amt = ParseAmount(I, E);

  if (I == E) {
    // No more characters left?
    H.HandleIncompleteSpecifier(Start, E - Start);
    return true;
  }

  if (Amt.getHowSpecified() == OptionalAmount::Constant && *(I++) == '$') {
    // Warn that positional arguments are non-standard.
    H.HandlePosition(Start, I - Start);

    // Special case: '%0$', since this is an easy mistake.
    if (Amt.getConstantAmount() == 0) {
      H.HandleZeroPosition(Start, I - Start);
      return true;
    }

    FS.setArgIndex(Amt.getConstantAmount() - 1);
    FS.setUsesPositionalArg();
    // Update the caller's pointer if we decided to consume
    // these characters.
    Beg = I;
    return false;
  }

  return false;
}

bool
clang::analyze_format_string::ParseVectorModifier(FormatStringHandler &H,
                                                  FormatSpecifier &FS,
                                                  const char *&I,
                                                  const char *E,
                                                  const LangOptions &LO) {
  if (!LO.OpenCL)
    return false;

  const char *Start = I;
  if (*I == 'v') {
    ++I;

    if (I == E) {
      H.HandleIncompleteSpecifier(Start, E - Start);
      return true;
    }

    OptionalAmount NumElts = ParseAmount(I, E);
    if (NumElts.getHowSpecified() != OptionalAmount::Constant) {
      H.HandleIncompleteSpecifier(Start, E - Start);
      return true;
    }

    FS.setVectorNumElts(NumElts);
  }

  return false;
}

bool
clang::analyze_format_string::ParseLengthModifier(FormatSpecifier &FS,
                                                  const char *&I,
                                                  const char *E,
                                                  const LangOptions &LO,
                                                  bool IsScanf) {
  LengthModifier::Kind lmKind = LengthModifier::None;
  const char *lmPosition = I;
  switch (*I) {
    default:
      return false;
    case 'h':
      ++I;
      if (I != E && *I == 'h') {
        ++I;
        lmKind = LengthModifier::AsChar;
      } else if (I != E && *I == 'l' && LO.OpenCL) {
        ++I;
        lmKind = LengthModifier::AsShortLong;
      } else {
        lmKind = LengthModifier::AsShort;
      }
      break;
    case 'l':
      ++I;
      if (I != E && *I == 'l') {
        ++I;
        lmKind = LengthModifier::AsLongLong;
      } else {
        lmKind = LengthModifier::AsLong;
      }
      break;
    case 'j': lmKind = LengthModifier::AsIntMax;     ++I; break;
    case 'z': lmKind = LengthModifier::AsSizeT;      ++I; break;
    case 't': lmKind = LengthModifier::AsPtrDiff;    ++I; break;
    case 'L': lmKind = LengthModifier::AsLongDouble; ++I; break;
    case 'q': lmKind = LengthModifier::AsQuad;       ++I; break;
    case 'a':
      if (IsScanf && !LO.C99 && !LO.CPlusPlus11) {
        // For scanf in C90, look at the next character to see if this should
        // be parsed as the GNU extension 'a' length modifier. If not, this
        // will be parsed as a conversion specifier.
        ++I;
        if (I != E && (*I == 's' || *I == 'S' || *I == '[')) {
          lmKind = LengthModifier::AsAllocate;
          break;
        }
        --I;
      }
      return false;
    case 'm':
      if (IsScanf) {
        lmKind = LengthModifier::AsMAllocate;
        ++I;
        break;
      }
      return false;
    // printf: AsInt64, AsInt32, AsInt3264
    // scanf:  AsInt64
    case 'I':
      if (I + 1 != E && I + 2 != E) {
        if (I[1] == '6' && I[2] == '4') {
          I += 3;
          lmKind = LengthModifier::AsInt64;
          break;
        }
        if (IsScanf)
          return false;

        if (I[1] == '3' && I[2] == '2') {
          I += 3;
          lmKind = LengthModifier::AsInt32;
          break;
        }
      }
      ++I;
      lmKind = LengthModifier::AsInt3264;
      break;
    case 'w':
      lmKind = LengthModifier::AsWide; ++I; break;
  }
  LengthModifier lm(lmPosition, lmKind);
  FS.setLengthModifier(lm);
  return true;
}

bool clang::analyze_format_string::ParseUTF8InvalidSpecifier(
    const char *SpecifierBegin, const char *FmtStrEnd, unsigned &Len) {
  if (SpecifierBegin + 1 >= FmtStrEnd)
    return false;

  const llvm::UTF8 *SB =
      reinterpret_cast<const llvm::UTF8 *>(SpecifierBegin + 1);
  const llvm::UTF8 *SE = reinterpret_cast<const llvm::UTF8 *>(FmtStrEnd);
  const char FirstByte = *SB;

  // If the invalid specifier is a multibyte UTF-8 string, return the
  // total length accordingly so that the conversion specifier can be
  // properly updated to reflect a complete UTF-8 specifier.
  unsigned NumBytes = llvm::getNumBytesForUTF8(FirstByte);
  if (NumBytes == 1)
    return false;
  if (SB + NumBytes > SE)
    return false;

  Len = NumBytes + 1;
  return true;
}

//===----------------------------------------------------------------------===//
// Methods on ArgType.
//===----------------------------------------------------------------------===//

clang::analyze_format_string::ArgType::MatchKind
ArgType::matchesType(ASTContext &C, QualType argTy) const {
  // When using the format attribute in C++, you can receive a function or an
  // array that will necessarily decay to a pointer when passed to the final
  // format consumer. Apply decay before type comparison.
  if (argTy->canDecayToPointerType())
    argTy = C.getDecayedType(argTy);

  if (Ptr) {
    // It has to be a pointer.
    const PointerType *PT = argTy->getAs<PointerType>();
    if (!PT)
      return NoMatch;

    // We cannot write through a const qualified pointer.
    if (PT->getPointeeType().isConstQualified())
      return NoMatch;

    argTy = PT->getPointeeType();
  }

  switch (K) {
    case InvalidTy:
      llvm_unreachable("ArgType must be valid");

    case UnknownTy:
      return Match;

    case AnyCharTy: {
      if (const auto *ETy = argTy->getAs<EnumType>()) {
        // If the enum is incomplete we know nothing about the underlying type.
        // Assume that it's 'int'. Do not use the underlying type for a scoped
        // enumeration.
        if (!ETy->getDecl()->isComplete())
          return NoMatch;
        if (ETy->isUnscopedEnumerationType())
          argTy = ETy->getDecl()->getIntegerType();
      }

      if (const auto *BT = argTy->getAs<BuiltinType>()) {
        // The types are perfectly matched?
        switch (BT->getKind()) {
        default:
          break;
        case BuiltinType::Char_S:
        case BuiltinType::SChar:
        case BuiltinType::UChar:
        case BuiltinType::Char_U:
            return Match;
        case BuiltinType::Bool:
          if (!Ptr)
            return Match;
          break;
        }
        // "Partially matched" because of promotions?
        if (!Ptr) {
          switch (BT->getKind()) {
          default:
            break;
          case BuiltinType::Int:
          case BuiltinType::UInt:
            return MatchPromotion;
          case BuiltinType::Short:
          case BuiltinType::UShort:
          case BuiltinType::WChar_S:
          case BuiltinType::WChar_U:
            return NoMatchPromotionTypeConfusion;
          }
        }
      }
      return NoMatch;
    }

    case SpecificTy: {
      if (const EnumType *ETy = argTy->getAs<EnumType>()) {
        // If the enum is incomplete we know nothing about the underlying type.
        // Assume that it's 'int'. Do not use the underlying type for a scoped
        // enumeration as that needs an exact match.
        if (!ETy->getDecl()->isComplete())
          argTy = C.IntTy;
        else if (ETy->isUnscopedEnumerationType())
          argTy = ETy->getDecl()->getIntegerType();
      }

      if (argTy->isSaturatedFixedPointType())
        argTy = C.getCorrespondingUnsaturatedType(argTy);

      argTy = C.getCanonicalType(argTy).getUnqualifiedType();

      if (T == argTy)
        return Match;
      if (const auto *BT = argTy->getAs<BuiltinType>()) {
        // Check if the only difference between them is signed vs unsigned
        // if true, return match signedness.
        switch (BT->getKind()) {
          default:
            break;
          case BuiltinType::Bool:
            if (Ptr && (T == C.UnsignedCharTy || T == C.SignedCharTy))
              return NoMatch;
            [[fallthrough]];
          case BuiltinType::Char_S:
          case BuiltinType::SChar:
            if (T == C.UnsignedShortTy || T == C.ShortTy)
              return NoMatchTypeConfusion;
            if (T == C.UnsignedCharTy)
              return NoMatchSignedness;
            if (T == C.SignedCharTy)
              return Match;
            break;
          case BuiltinType::Char_U:
          case BuiltinType::UChar:
            if (T == C.UnsignedShortTy || T == C.ShortTy)
              return NoMatchTypeConfusion;
            if (T == C.UnsignedCharTy)
              return Match;
            if (T == C.SignedCharTy)
              return NoMatchSignedness;
            break;
          case BuiltinType::Short:
            if (T == C.UnsignedShortTy)
              return NoMatchSignedness;
            break;
          case BuiltinType::UShort:
            if (T == C.ShortTy)
              return NoMatchSignedness;
            break;
          case BuiltinType::Int:
            if (T == C.UnsignedIntTy)
              return NoMatchSignedness;
            break;
          case BuiltinType::UInt:
            if (T == C.IntTy)
              return NoMatchSignedness;
            break;
          case BuiltinType::Long:
            if (T == C.UnsignedLongTy)
              return NoMatchSignedness;
            break;
          case BuiltinType::ULong:
            if (T == C.LongTy)
              return NoMatchSignedness;
            break;
          case BuiltinType::LongLong:
            if (T == C.UnsignedLongLongTy)
              return NoMatchSignedness;
            break;
          case BuiltinType::ULongLong:
            if (T == C.LongLongTy)
              return NoMatchSignedness;
            break;
          }
          // "Partially matched" because of promotions?
          if (!Ptr) {
            switch (BT->getKind()) {
            default:
              break;
            case BuiltinType::Bool:
              if (T == C.IntTy || T == C.UnsignedIntTy)
                return MatchPromotion;
              break;
            case BuiltinType::Int:
            case BuiltinType::UInt:
              if (T == C.SignedCharTy || T == C.UnsignedCharTy ||
                  T == C.ShortTy || T == C.UnsignedShortTy || T == C.WCharTy ||
                  T == C.WideCharTy)
                return MatchPromotion;
              break;
            case BuiltinType::Char_U:
              if (T == C.UnsignedIntTy)
                return MatchPromotion;
              if (T == C.UnsignedShortTy)
                return NoMatchPromotionTypeConfusion;
              break;
            case BuiltinType::Char_S:
              if (T == C.IntTy)
                return MatchPromotion;
              if (T == C.ShortTy)
                return NoMatchPromotionTypeConfusion;
              break;
            case BuiltinType::Half:
            case BuiltinType::Float:
              if (T == C.DoubleTy)
                return MatchPromotion;
              break;
            case BuiltinType::Short:
            case BuiltinType::UShort:
              if (T == C.SignedCharTy || T == C.UnsignedCharTy)
                return NoMatchPromotionTypeConfusion;
              break;
            case BuiltinType::WChar_U:
            case BuiltinType::WChar_S:
              if (T != C.WCharTy && T != C.WideCharTy)
                return NoMatchPromotionTypeConfusion;
            }
          }
      }
      return NoMatch;
    }

    case CStrTy: {
      const PointerType *PT = argTy->getAs<PointerType>();
      if (!PT)
        return NoMatch;
      QualType pointeeTy = PT->getPointeeType();
      if (const BuiltinType *BT = pointeeTy->getAs<BuiltinType>())
        switch (BT->getKind()) {
          case BuiltinType::Char_U:
          case BuiltinType::UChar:
          case BuiltinType::Char_S:
          case BuiltinType::SChar:
            return Match;
          default:
            break;
        }

      return NoMatch;
    }

    case WCStrTy: {
      const PointerType *PT = argTy->getAs<PointerType>();
      if (!PT)
        return NoMatch;
      QualType pointeeTy =
        C.getCanonicalType(PT->getPointeeType()).getUnqualifiedType();
      return pointeeTy == C.getWideCharType() ? Match : NoMatch;
    }

    case WIntTy: {
      QualType WInt = C.getCanonicalType(C.getWIntType()).getUnqualifiedType();

      if (C.getCanonicalType(argTy).getUnqualifiedType() == WInt)
        return Match;

      QualType PromoArg = C.isPromotableIntegerType(argTy)
                              ? C.getPromotedIntegerType(argTy)
                              : argTy;
      PromoArg = C.getCanonicalType(PromoArg).getUnqualifiedType();

      // If the promoted argument is the corresponding signed type of the
      // wint_t type, then it should match.
      if (PromoArg->hasSignedIntegerRepresentation() &&
          C.getCorrespondingUnsignedType(PromoArg) == WInt)
        return Match;

      return WInt == PromoArg ? Match : NoMatch;
    }

    case CPointerTy:
      if (argTy->isVoidPointerType()) {
        return Match;
      } if (argTy->isPointerType() || argTy->isObjCObjectPointerType() ||
            argTy->isBlockPointerType() || argTy->isNullPtrType()) {
        return NoMatchPedantic;
      } else {
        return NoMatch;
      }

    case ObjCPointerTy: {
      if (argTy->getAs<ObjCObjectPointerType>() ||
          argTy->getAs<BlockPointerType>())
        return Match;

      // Handle implicit toll-free bridging.
      if (const PointerType *PT = argTy->getAs<PointerType>()) {
        // Things such as CFTypeRef are really just opaque pointers
        // to C structs representing CF types that can often be bridged
        // to Objective-C objects.  Since the compiler doesn't know which
        // structs can be toll-free bridged, we just accept them all.
        QualType pointee = PT->getPointeeType();
        if (pointee->getAsStructureType() || pointee->isVoidType())
          return Match;
      }
      return NoMatch;
    }
  }

  llvm_unreachable("Invalid ArgType Kind!");
}

ArgType ArgType::makeVectorType(ASTContext &C, unsigned NumElts) const {
  // Check for valid vector element types.
  if (T.isNull())
    return ArgType::Invalid();

  QualType Vec = C.getExtVectorType(T, NumElts);
  return ArgType(Vec, Name);
}

QualType ArgType::getRepresentativeType(ASTContext &C) const {
  QualType Res;
  switch (K) {
    case InvalidTy:
      llvm_unreachable("No representative type for Invalid ArgType");
    case UnknownTy:
      llvm_unreachable("No representative type for Unknown ArgType");
    case AnyCharTy:
      Res = C.CharTy;
      break;
    case SpecificTy:
      Res = T;
      break;
    case CStrTy:
      Res = C.getPointerType(C.CharTy);
      break;
    case WCStrTy:
      Res = C.getPointerType(C.getWideCharType());
      break;
    case ObjCPointerTy:
      Res = C.ObjCBuiltinIdTy;
      break;
    case CPointerTy:
      Res = C.VoidPtrTy;
      break;
    case WIntTy: {
      Res = C.getWIntType();
      break;
    }
  }

  if (Ptr)
    Res = C.getPointerType(Res);
  return Res;
}

std::string ArgType::getRepresentativeTypeName(ASTContext &C) const {
  std::string S = getRepresentativeType(C).getAsString(C.getPrintingPolicy());

  std::string Alias;
  if (Name) {
    // Use a specific name for this type, e.g. "size_t".
    Alias = Name;
    if (Ptr) {
      // If ArgType is actually a pointer to T, append an asterisk.
      Alias += (Alias[Alias.size()-1] == '*') ? "*" : " *";
    }
    // If Alias is the same as the underlying type, e.g. wchar_t, then drop it.
    if (S == Alias)
      Alias.clear();
  }

  if (!Alias.empty())
    return std::string("'") + Alias + "' (aka '" + S + "')";
  return std::string("'") + S + "'";
}


//===----------------------------------------------------------------------===//
// Methods on OptionalAmount.
//===----------------------------------------------------------------------===//

ArgType
analyze_format_string::OptionalAmount::getArgType(ASTContext &Ctx) const {
  return Ctx.IntTy;
}

//===----------------------------------------------------------------------===//
// Methods on LengthModifier.
//===----------------------------------------------------------------------===//

const char *
analyze_format_string::LengthModifier::toString() const {
  switch (kind) {
  case AsChar:
    return "hh";
  case AsShort:
    return "h";
  case AsShortLong:
    return "hl";
  case AsLong: // or AsWideChar
    return "l";
  case AsLongLong:
    return "ll";
  case AsQuad:
    return "q";
  case AsIntMax:
    return "j";
  case AsSizeT:
    return "z";
  case AsPtrDiff:
    return "t";
  case AsInt32:
    return "I32";
  case AsInt3264:
    return "I";
  case AsInt64:
    return "I64";
  case AsLongDouble:
    return "L";
  case AsAllocate:
    return "a";
  case AsMAllocate:
    return "m";
  case AsWide:
    return "w";
  case None:
    return "";
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Methods on ConversionSpecifier.
//===----------------------------------------------------------------------===//

const char *ConversionSpecifier::toString() const {
  switch (kind) {
  case bArg: return "b";
  case BArg: return "B";
  case dArg: return "d";
  case DArg: return "D";
  case iArg: return "i";
  case oArg: return "o";
  case OArg: return "O";
  case uArg: return "u";
  case UArg: return "U";
  case xArg: return "x";
  case XArg: return "X";
  case fArg: return "f";
  case FArg: return "F";
  case eArg: return "e";
  case EArg: return "E";
  case gArg: return "g";
  case GArg: return "G";
  case aArg: return "a";
  case AArg: return "A";
  case cArg: return "c";
  case sArg: return "s";
  case pArg: return "p";
  case PArg:
    return "P";
  case nArg: return "n";
  case PercentArg:  return "%";
  case ScanListArg: return "[";
  case InvalidSpecifier: return nullptr;

  // POSIX unicode extensions.
  case CArg: return "C";
  case SArg: return "S";

  // Objective-C specific specifiers.
  case ObjCObjArg: return "@";

  // FreeBSD kernel specific specifiers.
  case FreeBSDbArg: return "b";
  case FreeBSDDArg: return "D";
  case FreeBSDrArg: return "r";
  case FreeBSDyArg: return "y";

  // GlibC specific specifiers.
  case PrintErrno: return "m";

  // MS specific specifiers.
  case ZArg: return "Z";

  // ISO/IEC TR 18037 (fixed-point) specific specifiers.
  case rArg:
    return "r";
  case RArg:
    return "R";
  case kArg:
    return "k";
  case KArg:
    return "K";
  }
  return nullptr;
}

std::optional<ConversionSpecifier>
ConversionSpecifier::getStandardSpecifier() const {
  ConversionSpecifier::Kind NewKind;

  switch (getKind()) {
  default:
    return std::nullopt;
  case DArg:
    NewKind = dArg;
    break;
  case UArg:
    NewKind = uArg;
    break;
  case OArg:
    NewKind = oArg;
    break;
  }

  ConversionSpecifier FixedCS(*this);
  FixedCS.setKind(NewKind);
  return FixedCS;
}

//===----------------------------------------------------------------------===//
// Methods on OptionalAmount.
//===----------------------------------------------------------------------===//

void OptionalAmount::toString(raw_ostream &os) const {
  switch (hs) {
  case Invalid:
  case NotSpecified:
    return;
  case Arg:
    if (UsesDotPrefix)
        os << ".";
    if (usesPositionalArg())
      os << "*" << getPositionalArgIndex() << "$";
    else
      os << "*";
    break;
  case Constant:
    if (UsesDotPrefix)
        os << ".";
    os << amt;
    break;
  }
}

bool FormatSpecifier::hasValidLengthModifier(const TargetInfo &Target,
                                             const LangOptions &LO) const {
  switch (LM.getKind()) {
    case LengthModifier::None:
      return true;

    // Handle most integer flags
    case LengthModifier::AsShort:
      // Length modifier only applies to FP vectors.
      if (LO.OpenCL && CS.isDoubleArg())
        return !VectorNumElts.isInvalid();

      if (CS.isFixedPointArg())
        return true;

      if (Target.getTriple().isOSMSVCRT()) {
        switch (CS.getKind()) {
          case ConversionSpecifier::cArg:
          case ConversionSpecifier::CArg:
          case ConversionSpecifier::sArg:
          case ConversionSpecifier::SArg:
          case ConversionSpecifier::ZArg:
            return true;
          default:
            break;
        }
      }
      [[fallthrough]];
    case LengthModifier::AsChar:
    case LengthModifier::AsLongLong:
    case LengthModifier::AsQuad:
    case LengthModifier::AsIntMax:
    case LengthModifier::AsSizeT:
    case LengthModifier::AsPtrDiff:
      switch (CS.getKind()) {
        case ConversionSpecifier::bArg:
        case ConversionSpecifier::BArg:
        case ConversionSpecifier::dArg:
        case ConversionSpecifier::DArg:
        case ConversionSpecifier::iArg:
        case ConversionSpecifier::oArg:
        case ConversionSpecifier::OArg:
        case ConversionSpecifier::uArg:
        case ConversionSpecifier::UArg:
        case ConversionSpecifier::xArg:
        case ConversionSpecifier::XArg:
        case ConversionSpecifier::nArg:
          return true;
        case ConversionSpecifier::FreeBSDbArg:
          return Target.getTriple().isOSFreeBSD() ||
                 Target.getTriple().isPS4() ||
                 Target.getTriple().isOSOpenBSD();
        case ConversionSpecifier::FreeBSDrArg:
        case ConversionSpecifier::FreeBSDyArg:
          return Target.getTriple().isOSFreeBSD() || Target.getTriple().isPS();
        default:
          return false;
      }

    case LengthModifier::AsShortLong:
      return LO.OpenCL && !VectorNumElts.isInvalid();

    // Handle 'l' flag
    case LengthModifier::AsLong: // or AsWideChar
      if (CS.isDoubleArg()) {
        // Invalid for OpenCL FP scalars.
        if (LO.OpenCL && VectorNumElts.isInvalid())
          return false;
        return true;
      }

      if (CS.isFixedPointArg())
        return true;

      switch (CS.getKind()) {
        case ConversionSpecifier::bArg:
        case ConversionSpecifier::BArg:
        case ConversionSpecifier::dArg:
        case ConversionSpecifier::DArg:
        case ConversionSpecifier::iArg:
        case ConversionSpecifier::oArg:
        case ConversionSpecifier::OArg:
        case ConversionSpecifier::uArg:
        case ConversionSpecifier::UArg:
        case ConversionSpecifier::xArg:
        case ConversionSpecifier::XArg:
        case ConversionSpecifier::nArg:
        case ConversionSpecifier::cArg:
        case ConversionSpecifier::sArg:
        case ConversionSpecifier::ScanListArg:
        case ConversionSpecifier::ZArg:
          return true;
        case ConversionSpecifier::FreeBSDbArg:
          return Target.getTriple().isOSFreeBSD() ||
                 Target.getTriple().isPS4() ||
                 Target.getTriple().isOSOpenBSD();
        case ConversionSpecifier::FreeBSDrArg:
        case ConversionSpecifier::FreeBSDyArg:
          return Target.getTriple().isOSFreeBSD() || Target.getTriple().isPS();
        default:
          return false;
      }

    case LengthModifier::AsLongDouble:
      switch (CS.getKind()) {
        case ConversionSpecifier::aArg:
        case ConversionSpecifier::AArg:
        case ConversionSpecifier::fArg:
        case ConversionSpecifier::FArg:
        case ConversionSpecifier::eArg:
        case ConversionSpecifier::EArg:
        case ConversionSpecifier::gArg:
        case ConversionSpecifier::GArg:
          return true;
        // GNU libc extension.
        case ConversionSpecifier::dArg:
        case ConversionSpecifier::iArg:
        case ConversionSpecifier::oArg:
        case ConversionSpecifier::uArg:
        case ConversionSpecifier::xArg:
        case ConversionSpecifier::XArg:
          return !Target.getTriple().isOSDarwin() &&
                 !Target.getTriple().isOSWindows();
        default:
          return false;
      }

    case LengthModifier::AsAllocate:
      switch (CS.getKind()) {
        case ConversionSpecifier::sArg:
        case ConversionSpecifier::SArg:
        case ConversionSpecifier::ScanListArg:
          return true;
        default:
          return false;
      }

    case LengthModifier::AsMAllocate:
      switch (CS.getKind()) {
        case ConversionSpecifier::cArg:
        case ConversionSpecifier::CArg:
        case ConversionSpecifier::sArg:
        case ConversionSpecifier::SArg:
        case ConversionSpecifier::ScanListArg:
          return true;
        default:
          return false;
      }
    case LengthModifier::AsInt32:
    case LengthModifier::AsInt3264:
    case LengthModifier::AsInt64:
      switch (CS.getKind()) {
        case ConversionSpecifier::dArg:
        case ConversionSpecifier::iArg:
        case ConversionSpecifier::oArg:
        case ConversionSpecifier::uArg:
        case ConversionSpecifier::xArg:
        case ConversionSpecifier::XArg:
          return Target.getTriple().isOSMSVCRT();
        default:
          return false;
      }
    case LengthModifier::AsWide:
      switch (CS.getKind()) {
        case ConversionSpecifier::cArg:
        case ConversionSpecifier::CArg:
        case ConversionSpecifier::sArg:
        case ConversionSpecifier::SArg:
        case ConversionSpecifier::ZArg:
          return Target.getTriple().isOSMSVCRT();
        default:
          return false;
      }
  }
  llvm_unreachable("Invalid LengthModifier Kind!");
}

bool FormatSpecifier::hasStandardLengthModifier() const {
  switch (LM.getKind()) {
    case LengthModifier::None:
    case LengthModifier::AsChar:
    case LengthModifier::AsShort:
    case LengthModifier::AsLong:
    case LengthModifier::AsLongLong:
    case LengthModifier::AsIntMax:
    case LengthModifier::AsSizeT:
    case LengthModifier::AsPtrDiff:
    case LengthModifier::AsLongDouble:
      return true;
    case LengthModifier::AsAllocate:
    case LengthModifier::AsMAllocate:
    case LengthModifier::AsQuad:
    case LengthModifier::AsInt32:
    case LengthModifier::AsInt3264:
    case LengthModifier::AsInt64:
    case LengthModifier::AsWide:
    case LengthModifier::AsShortLong: // ???
      return false;
  }
  llvm_unreachable("Invalid LengthModifier Kind!");
}

bool FormatSpecifier::hasStandardConversionSpecifier(
    const LangOptions &LangOpt) const {
  switch (CS.getKind()) {
    case ConversionSpecifier::bArg:
    case ConversionSpecifier::BArg:
    case ConversionSpecifier::cArg:
    case ConversionSpecifier::dArg:
    case ConversionSpecifier::iArg:
    case ConversionSpecifier::oArg:
    case ConversionSpecifier::uArg:
    case ConversionSpecifier::xArg:
    case ConversionSpecifier::XArg:
    case ConversionSpecifier::fArg:
    case ConversionSpecifier::FArg:
    case ConversionSpecifier::eArg:
    case ConversionSpecifier::EArg:
    case ConversionSpecifier::gArg:
    case ConversionSpecifier::GArg:
    case ConversionSpecifier::aArg:
    case ConversionSpecifier::AArg:
    case ConversionSpecifier::sArg:
    case ConversionSpecifier::pArg:
    case ConversionSpecifier::nArg:
    case ConversionSpecifier::ObjCObjArg:
    case ConversionSpecifier::ScanListArg:
    case ConversionSpecifier::PercentArg:
    case ConversionSpecifier::PArg:
      return true;
    case ConversionSpecifier::CArg:
    case ConversionSpecifier::SArg:
      return LangOpt.ObjC;
    case ConversionSpecifier::InvalidSpecifier:
    case ConversionSpecifier::FreeBSDbArg:
    case ConversionSpecifier::FreeBSDDArg:
    case ConversionSpecifier::FreeBSDrArg:
    case ConversionSpecifier::FreeBSDyArg:
    case ConversionSpecifier::PrintErrno:
    case ConversionSpecifier::DArg:
    case ConversionSpecifier::OArg:
    case ConversionSpecifier::UArg:
    case ConversionSpecifier::ZArg:
      return false;
    case ConversionSpecifier::rArg:
    case ConversionSpecifier::RArg:
    case ConversionSpecifier::kArg:
    case ConversionSpecifier::KArg:
      return LangOpt.FixedPoint;
  }
  llvm_unreachable("Invalid ConversionSpecifier Kind!");
}

bool FormatSpecifier::hasStandardLengthConversionCombination() const {
  if (LM.getKind() == LengthModifier::AsLongDouble) {
    switch(CS.getKind()) {
        case ConversionSpecifier::dArg:
        case ConversionSpecifier::iArg:
        case ConversionSpecifier::oArg:
        case ConversionSpecifier::uArg:
        case ConversionSpecifier::xArg:
        case ConversionSpecifier::XArg:
        case ConversionSpecifier::FreeBSDbArg:
          return false;
        default:
          return true;
    }
  }
  return true;
}

std::optional<LengthModifier>
FormatSpecifier::getCorrectedLengthModifier() const {
  if (CS.isAnyIntArg() || CS.getKind() == ConversionSpecifier::nArg) {
    if (LM.getKind() == LengthModifier::AsLongDouble ||
        LM.getKind() == LengthModifier::AsQuad) {
      LengthModifier FixedLM(LM);
      FixedLM.setKind(LengthModifier::AsLongLong);
      return FixedLM;
    }
  }

  return std::nullopt;
}

bool FormatSpecifier::namedTypeToLengthModifier(QualType QT,
                                                LengthModifier &LM) {
  for (/**/; const auto *TT = QT->getAs<TypedefType>();
       QT = TT->getDecl()->getUnderlyingType()) {
    const TypedefNameDecl *Typedef = TT->getDecl();
    const IdentifierInfo *Identifier = Typedef->getIdentifier();
    if (Identifier->getName() == "size_t") {
      LM.setKind(LengthModifier::AsSizeT);
      return true;
    } else if (Identifier->getName() == "ssize_t") {
      // Not C99, but common in Unix.
      LM.setKind(LengthModifier::AsSizeT);
      return true;
    } else if (Identifier->getName() == "intmax_t") {
      LM.setKind(LengthModifier::AsIntMax);
      return true;
    } else if (Identifier->getName() == "uintmax_t") {
      LM.setKind(LengthModifier::AsIntMax);
      return true;
    } else if (Identifier->getName() == "ptrdiff_t") {
      LM.setKind(LengthModifier::AsPtrDiff);
      return true;
    }
  }
  return false;
}
