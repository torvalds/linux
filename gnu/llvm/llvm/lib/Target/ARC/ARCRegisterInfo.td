//===- ARCRegisterInfo.td - ARC Register defs --------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the ARC register file
//===----------------------------------------------------------------------===//

class ARCReg<string n, list<string> altNames> : Register<n, altNames> {
  field bits<6> HwEncoding;
  let Namespace = "ARC";
}

// Registers are identified with 6-bit ID numbers.
// Core - 32-bit core registers
class Core<int num, string n, list<string>altNames=[]> : ARCReg<n, altNames> {
  let HWEncoding = num;
}

// Auxilary register
class Aux<int num, string n, list<string> altNames=[]> : ARCReg<n, altNames> {
  let HWEncoding = num;
}

// Integer registers
foreach i = 0 - 3 in
  def R#i : Core<i, "%r"#i>, DwarfRegNum<[i]>;

let CostPerUse=[1] in {
  foreach i = 4 - 11 in
    def R#i : Core<i, "%r"#i>, DwarfRegNum<[i]>;
}

foreach i = 12 - 15 in
  def R#i : Core<i, "%r"#i>, DwarfRegNum<[i]>;

let CostPerUse=[1] in {

  foreach i = 16 - 25 in
    def R#i : Core<i, "%r"#i>, DwarfRegNum<[i]>;

  def GP : Core<26, "%gp",["%r26"]>, DwarfRegNum<[26]>;
  def FP : Core<27, "%fp", ["%r27"]>, DwarfRegNum<[27]>;
  def SP : Core<28, "%sp", ["%r28"]>, DwarfRegNum<[28]>;
  def ILINK : Core<29, "%ilink">, DwarfRegNum<[29]>;
  def R30 : Core<30, "%r30">, DwarfRegNum<[30]>;
  def BLINK : Core<31, "%blink">, DwarfRegNum<[31]>;

  // Define extended core registers R32..R63
  foreach i = 32 - 63 in
    def R#i : Core<i, "%r"#i>, DwarfRegNum<[i]>;
}

// Auxilary registers
let CostPerUse=[1] in {
  def STATUS32 : Aux<10, "status32">; // No DwarfRegNum defined in the ARC ABI
}

def GPR32: RegisterClass<"ARC", [i32], 32,
  (add (sequence "R%u", 0, 25), GP, FP, SP, ILINK, R30, BLINK, (sequence "R%u", 32, 63))> {
  let AltOrders=[(add (sequence "R%u", 0, 25), GP, FP, SP, ILINK, R30, BLINK)];
  let AltOrderSelect = [{
      // When referenced in a C++ code block like this
      // 0 is all Core32 regs
      // 1 is AltOrders[0]
      // 2 is AltOrders[1] and so on
      return 1;
    }];
}

def SREG : RegisterClass<"ARC", [i32], 1, (add STATUS32)>;

def GPR_S : RegisterClass<"ARC", [i32], 8,
  (add R0, R1, R2, R3, R12, R13, R14, R15)>;

