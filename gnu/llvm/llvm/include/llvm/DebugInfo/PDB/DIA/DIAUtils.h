//===- DIAUtils.h - Utility functions for working with DIA ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAUTILS_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ConvertUTF.h"

template <typename Obj>
std::string invokeBstrMethod(Obj &Object,
                             HRESULT (__stdcall Obj::*Func)(BSTR *)) {
  CComBSTR Str16;
  HRESULT Result = (Object.*Func)(&Str16);
  if (S_OK != Result)
    return std::string();

  std::string Str8;
  llvm::ArrayRef<char> StrBytes(reinterpret_cast<char *>(Str16.m_str),
                                Str16.ByteLength());
  llvm::convertUTF16ToUTF8String(StrBytes, Str8);
  return Str8;
}

#endif // LLVM_DEBUGINFO_PDB_DIA_DIAUTILS_H
