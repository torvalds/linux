//===- llvm/IR/DiagnosticPrinter.cpp - Diagnostic Printer -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a diagnostic printer relying on raw_ostream.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(char C) {
  Stream << C;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(unsigned char C) {
  Stream << C;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(signed char C) {
  Stream << C;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(StringRef Str) {
  Stream << Str;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(const char *Str) {
  Stream << Str;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(
    const std::string &Str) {
  Stream << Str;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(unsigned long N) {
  Stream << N;
  return *this;
}
DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(long N) {
  Stream << N;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(
    unsigned long long N) {
  Stream << N;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(long long N) {
  Stream << N;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(const void *P) {
  Stream << P;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(unsigned int N) {
  Stream << N;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(int N) {
  Stream << N;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(double N) {
  Stream << N;
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(const Twine &Str) {
  Str.print(Stream);
  return *this;
}

// IR related types.
DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(const Value &V) {
  Stream << V.getName();
  return *this;
}

DiagnosticPrinter &DiagnosticPrinterRawOStream::operator<<(const Module &M) {
  Stream << M.getModuleIdentifier();
  return *this;
}

// Other types.
DiagnosticPrinter &DiagnosticPrinterRawOStream::
operator<<(const SMDiagnostic &Diag) {
  // We don't have to print the SMDiagnostic kind, as the diagnostic severity
  // is printed by the diagnostic handler.
  Diag.print("", Stream, /*ShowColors=*/true, /*ShowKindLabel=*/false);
  return *this;
}
