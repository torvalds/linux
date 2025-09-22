//===-- ARMMCAsmInfo.cpp - ARM asm properties -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the ARMMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "ARMMCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

void ARMMCAsmInfoDarwin::anchor() { }

ARMMCAsmInfoDarwin::ARMMCAsmInfoDarwin(const Triple &TheTriple) {
  if ((TheTriple.getArch() == Triple::armeb) ||
      (TheTriple.getArch() == Triple::thumbeb))
    IsLittleEndian = false;

  Data64bitsDirective = nullptr;
  CommentString = "@";
  Code16Directive = ".code\t16";
  Code32Directive = ".code\t32";
  UseDataRegionDirectives = true;

  SupportsDebugInformation = true;

  // Conditional Thumb 4-byte instructions can have an implicit IT.
  MaxInstLength = 6;

  // Exceptions handling
  ExceptionsType = (TheTriple.isOSDarwin() && !TheTriple.isWatchABI())
                       ? ExceptionHandling::SjLj
                       : ExceptionHandling::DwarfCFI;
}

void ARMELFMCAsmInfo::anchor() { }

ARMELFMCAsmInfo::ARMELFMCAsmInfo(const Triple &TheTriple) {
  if ((TheTriple.getArch() == Triple::armeb) ||
      (TheTriple.getArch() == Triple::thumbeb))
    IsLittleEndian = false;

  // ".comm align is in bytes but .align is pow-2."
  AlignmentIsInBytes = false;

  Data64bitsDirective = nullptr;
  CommentString = "@";
  Code16Directive = ".code\t16";
  Code32Directive = ".code\t32";

  SupportsDebugInformation = true;

  // Conditional Thumb 4-byte instructions can have an implicit IT.
  MaxInstLength = 6;

  // Exceptions handling
  switch (TheTriple.getOS()) {
  case Triple::NetBSD:
    ExceptionsType = ExceptionHandling::DwarfCFI;
    break;
  default:
    ExceptionsType = ExceptionHandling::ARM;
    break;
  }

  // foo(plt) instead of foo@plt
  UseParensForSymbolVariant = true;
}

void ARMELFMCAsmInfo::setUseIntegratedAssembler(bool Value) {
  UseIntegratedAssembler = Value;
  if (!UseIntegratedAssembler) {
    // gas doesn't handle VFP register names in cfi directives,
    // so don't use register names with external assembler.
    // See https://sourceware.org/bugzilla/show_bug.cgi?id=16694
    DwarfRegNumForCFI = true;
  }
}

void ARMCOFFMCAsmInfoMicrosoft::anchor() { }

ARMCOFFMCAsmInfoMicrosoft::ARMCOFFMCAsmInfoMicrosoft() {
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::WinEH;
  WinEHEncodingType = WinEH::EncodingType::Itanium;
  PrivateGlobalPrefix = "$M";
  PrivateLabelPrefix = "$M";
  CommentString = "@";

  // Conditional Thumb 4-byte instructions can have an implicit IT.
  MaxInstLength = 6;
}

void ARMCOFFMCAsmInfoGNU::anchor() { }

ARMCOFFMCAsmInfoGNU::ARMCOFFMCAsmInfoGNU() {
  AlignmentIsInBytes = false;
  HasSingleParameterDotFile = true;

  CommentString = "@";
  Code16Directive = ".code\t16";
  Code32Directive = ".code\t32";
  PrivateGlobalPrefix = ".L";
  PrivateLabelPrefix = ".L";

  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::WinEH;
  WinEHEncodingType = WinEH::EncodingType::Itanium;
  UseParensForSymbolVariant = true;

  DwarfRegNumForCFI = false;

  // Conditional Thumb 4-byte instructions can have an implicit IT.
  MaxInstLength = 6;
}
