//===-- NVPTXRegisterInfo.td - NVPTX Register defs ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the PTX register file
//===----------------------------------------------------------------------===//

class NVPTXReg<string n> : Register<n> {
  let Namespace = "NVPTX";
}

class NVPTXRegClass<list<ValueType> regTypes, int alignment, dag regList>
     : RegisterClass <"NVPTX", regTypes, alignment, regList>;

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//

// Special Registers used as stack pointer
def VRFrame32         : NVPTXReg<"%SP">;
def VRFrame64         : NVPTXReg<"%SP">;
def VRFrameLocal32    : NVPTXReg<"%SPL">;
def VRFrameLocal64    : NVPTXReg<"%SPL">;

// Special Registers used as the stack
def VRDepot  : NVPTXReg<"%Depot">;

// We use virtual registers, but define a few physical registers here to keep
// SDAG and the MachineInstr layers happy.
foreach i = 0...4 in {
  def P#i  : NVPTXReg<"%p"#i>;  // Predicate
  def RS#i : NVPTXReg<"%rs"#i>; // 16-bit
  def R#i  : NVPTXReg<"%r"#i>;  // 32-bit
  def RL#i : NVPTXReg<"%rd"#i>; // 64-bit
  def RQ#i : NVPTXReg<"%rq"#i>; // 128-bit
  def H#i  : NVPTXReg<"%h"#i>;  // 16-bit float
  def HH#i : NVPTXReg<"%hh"#i>; // 2x16-bit float
  def F#i  : NVPTXReg<"%f"#i>;  // 32-bit float
  def FL#i : NVPTXReg<"%fd"#i>; // 64-bit float

  // Arguments
  def ia#i : NVPTXReg<"%ia"#i>;
  def la#i : NVPTXReg<"%la"#i>;
  def fa#i : NVPTXReg<"%fa"#i>;
  def da#i : NVPTXReg<"%da"#i>;
}

foreach i = 0...31 in {
  def ENVREG#i : NVPTXReg<"%envreg"#i>;
}

//===----------------------------------------------------------------------===//
//  Register classes
//===----------------------------------------------------------------------===//
def Int1Regs : NVPTXRegClass<[i1], 8, (add (sequence "P%u", 0, 4))>;
def Int16Regs : NVPTXRegClass<[i16, f16, bf16], 16, (add (sequence "RS%u", 0, 4))>;
def Int32Regs : NVPTXRegClass<[i32, v2f16, v2bf16, v2i16, v4i8], 32,
                              (add (sequence "R%u", 0, 4),
                              VRFrame32, VRFrameLocal32)>;
def Int64Regs : NVPTXRegClass<[i64], 64, (add (sequence "RL%u", 0, 4), VRFrame64, VRFrameLocal64)>;
// 128-bit regs are not defined as general regs in NVPTX. They are used for inlineASM only.
def Int128Regs : NVPTXRegClass<[i128], 128, (add (sequence "RQ%u", 0, 4))>;
def Float32Regs : NVPTXRegClass<[f32], 32, (add (sequence "F%u", 0, 4))>;
def Float64Regs : NVPTXRegClass<[f64], 64, (add (sequence "FL%u", 0, 4))>;
def Int32ArgRegs : NVPTXRegClass<[i32], 32, (add (sequence "ia%u", 0, 4))>;
def Int64ArgRegs : NVPTXRegClass<[i64], 64, (add (sequence "la%u", 0, 4))>;
def Float32ArgRegs : NVPTXRegClass<[f32], 32, (add (sequence "fa%u", 0, 4))>;
def Float64ArgRegs : NVPTXRegClass<[f64], 64, (add (sequence "da%u", 0, 4))>;

// Read NVPTXRegisterInfo.cpp to see how VRFrame and VRDepot are used.
def SpecialRegs : NVPTXRegClass<[i32], 32, (add VRFrame32, VRFrameLocal32, VRDepot,
                                            (sequence "ENVREG%u", 0, 31))>;
