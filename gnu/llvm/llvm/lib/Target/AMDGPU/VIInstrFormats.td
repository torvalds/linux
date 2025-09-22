//===-- VIInstrFormats.td - VI Instruction Encodings ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// VI Instruction format definitions.
//
//===----------------------------------------------------------------------===//

class EXPe_vi : EXPe_ComprVM {
  let Inst{31-26} = 0x31; //encoding
}

class VINTRPe_vi <bits<2> op> : VINTRPe <op> {
  let Inst{31-26} = 0x35; // encoding
}
