//WebAssemblyRegisterInfo.td-Describe the WebAssembly Registers -*- tablegen -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the WebAssembly register classes and some nominal
/// physical registers.
///
//===----------------------------------------------------------------------===//

class WebAssemblyReg<string n> : Register<n> {
  let Namespace = "WebAssembly";
}

class WebAssemblyRegClass<list<ValueType> regTypes, int alignment, dag regList>
     : RegisterClass<"WebAssembly", regTypes, alignment, regList>;

//===----------------------------------------------------------------------===//
// Registers
//===----------------------------------------------------------------------===//

// Special registers used as the frame and stack pointer.
//
// WebAssembly may someday supports mixed 32-bit and 64-bit heaps in the same
// application, which requires separate width FP and SP.
def FP32 : WebAssemblyReg<"%FP32">;
def FP64 : WebAssemblyReg<"%FP64">;
def SP32 : WebAssemblyReg<"%SP32">;
def SP64 : WebAssemblyReg<"%SP64">;

// The register allocation framework requires register classes have at least
// one register, so we define a few for the integer / floating point register
// classes since we otherwise don't need a physical register in those classes.
// These are also used a "types" in the generated assembly matcher.
def I32_0 : WebAssemblyReg<"%i32.0">;
def I64_0 : WebAssemblyReg<"%i64.0">;
def F32_0 : WebAssemblyReg<"%f32.0">;
def F64_0 : WebAssemblyReg<"%f64.0">;

def V128_0: WebAssemblyReg<"%v128">;

def FUNCREF_0 : WebAssemblyReg<"%funcref.0">;
def EXTERNREF_0 : WebAssemblyReg<"%externref.0">;
def EXNREF_0 : WebAssemblyReg<"%exnref.0">;

// The value stack "register". This is an opaque entity which serves to order
// uses and defs that must remain in LIFO order.
def VALUE_STACK : WebAssemblyReg<"STACK">;

// The incoming arguments "register". This is an opaque entity which serves to
// order the ARGUMENT instructions that are emulating live-in registers and
// must not be scheduled below other instructions.
def ARGUMENTS : WebAssemblyReg<"ARGUMENTS">;

//===----------------------------------------------------------------------===//
//  Register classes
//===----------------------------------------------------------------------===//

def I32 : WebAssemblyRegClass<[i32], 32, (add FP32, SP32, I32_0)>;
def I64 : WebAssemblyRegClass<[i64], 64, (add FP64, SP64, I64_0)>;
def F32 : WebAssemblyRegClass<[f32], 32, (add F32_0)>;
def F64 : WebAssemblyRegClass<[f64], 64, (add F64_0)>;
def V128 : WebAssemblyRegClass<[v8f16, v4f32, v2f64, v2i64, v4i32, v16i8,
                                v8i16],
                               128, (add V128_0)>;
def FUNCREF : WebAssemblyRegClass<[funcref], 0, (add FUNCREF_0)>;
def EXTERNREF : WebAssemblyRegClass<[externref], 0, (add EXTERNREF_0)>;
def EXNREF : WebAssemblyRegClass<[exnref], 0, (add EXNREF_0)>;
