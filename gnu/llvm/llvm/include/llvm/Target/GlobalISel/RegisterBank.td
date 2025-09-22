//===- RegisterBank.td - Register bank definitions ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

class RegisterBank<string name, list<RegisterClass> classes> {
  string Name = name;
  list<RegisterClass> RegisterClasses = classes;
}
