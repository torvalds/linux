//===-- SPIRVRegisterInfo.td - SPIR-V Register defs --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Declarations that describe the SPIR-V register file.
//
//===----------------------------------------------------------------------===//

let Namespace = "SPIRV" in {
  // Pointer types for patterns with the GlobalISelEmitter
  def p32 : PtrValueType <i32, 0>;
  def p64 : PtrValueType <i64, 0>;

  class VTPtrVec<int nelem, PtrValueType ptr>
      : VTVec<nelem, ValueType<ptr.Size, ptr.Value>, ptr.Value> {
    int isPointer = true;
  }

  def v2p32 : VTPtrVec<2, p32>;
  def v2p64 : VTPtrVec<2, p64>;

  // Class for type registers
  def TYPE0 : Register<"TYPE0">;
  def TYPE : RegisterClass<"SPIRV", [i32], 32, (add TYPE0)>;

  // Class for non-type registers
  def ID0 : Register<"ID0">;
  def ID640 : Register<"ID640">;
  def fID0 : Register<"fID0">;
  def fID640 : Register<"fID640">;
  def pID320 : Register<"pID320">;
  def pID640 : Register<"pID640">;
  def vID0 : Register<"vID0">;
  def vfID0 : Register<"vfID0">;
  def vpID320 : Register<"vpID320">;
  def vpID640 : Register<"vpID640">;

  def ID : RegisterClass<"SPIRV", [i32], 32, (add ID0)>;
  def ID64 : RegisterClass<"SPIRV", [i64], 32, (add ID640)>;
  def fID : RegisterClass<"SPIRV", [f32], 32, (add fID0)>;
  def fID64 : RegisterClass<"SPIRV", [f64], 32, (add fID640)>;
  def pID32 : RegisterClass<"SPIRV", [p32], 32, (add pID320)>;
  def pID64 : RegisterClass<"SPIRV", [p64], 32, (add pID640)>;
  def vID : RegisterClass<"SPIRV", [v2i32], 32, (add vID0)>;
  def vfID : RegisterClass<"SPIRV", [v2f32], 32, (add vfID0)>;
  def vpID32 : RegisterClass<"SPIRV", [v2p32], 32, (add vpID320)>;
  def vpID64 : RegisterClass<"SPIRV", [v2p64], 32, (add vpID640)>;

  def ANYID : RegisterClass<
      "SPIRV",
      [i32, i64, f32, f64, p32, p64, v2i32, v2f32, v2p32, v2p64],
      32,
      (add ID0, ID640, fID0, fID640, pID320, pID640, vID0, vfID0, vpID320, vpID640)>;

  // A few instructions like OpName can take ids from both type and non-type
  // instructions, so we need a super-class to allow for both to count as valid
  // arguments for these instructions.
  def ANY : RegisterClass<"SPIRV", [i32], 32, (add TYPE, ID)>;
}
