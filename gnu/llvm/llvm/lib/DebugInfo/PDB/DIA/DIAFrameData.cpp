//===- DIAFrameData.cpp - DIA impl. of IPDBFrameData -------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAFrameData.h"
#include "llvm/DebugInfo/PDB/DIA/DIASession.h"
#include "llvm/DebugInfo/PDB/DIA/DIAUtils.h"

using namespace llvm::pdb;

DIAFrameData::DIAFrameData(CComPtr<IDiaFrameData> DiaFrameData)
    : FrameData(DiaFrameData) {}

template <typename ArgType>
ArgType
PrivateGetDIAValue(IDiaFrameData *FrameData,
                   HRESULT (__stdcall IDiaFrameData::*Method)(ArgType *)) {
  ArgType Value;
  if (S_OK == (FrameData->*Method)(&Value))
    return static_cast<ArgType>(Value);

  return ArgType();
}

uint32_t DIAFrameData::getAddressOffset() const {
  return PrivateGetDIAValue(FrameData, &IDiaFrameData::get_addressOffset);
}

uint32_t DIAFrameData::getAddressSection() const {
  return PrivateGetDIAValue(FrameData, &IDiaFrameData::get_addressSection);
}

uint32_t DIAFrameData::getLengthBlock() const {
  return PrivateGetDIAValue(FrameData, &IDiaFrameData::get_lengthBlock);
}

std::string DIAFrameData::getProgram() const {
  return invokeBstrMethod(*FrameData, &IDiaFrameData::get_program);
}

uint32_t DIAFrameData::getRelativeVirtualAddress() const {
  return PrivateGetDIAValue(FrameData,
                            &IDiaFrameData::get_relativeVirtualAddress);
}

uint64_t DIAFrameData::getVirtualAddress() const {
  return PrivateGetDIAValue(FrameData, &IDiaFrameData::get_virtualAddress);
}
