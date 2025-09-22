//===- minidump2yaml.cpp - Minidump to yaml conversion tool -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "obj2yaml.h"
#include "llvm/Object/Minidump.h"
#include "llvm/ObjectYAML/MinidumpYAML.h"
#include "llvm/Support/YAMLTraits.h"

using namespace llvm;

Error minidump2yaml(raw_ostream &Out, const object::MinidumpFile &Obj) {
  auto ExpectedObject = MinidumpYAML::Object::create(Obj);
  if (!ExpectedObject)
    return ExpectedObject.takeError();
  yaml::Output Output(Out);
  Output << *ExpectedObject;
  return llvm::Error::success();
}
