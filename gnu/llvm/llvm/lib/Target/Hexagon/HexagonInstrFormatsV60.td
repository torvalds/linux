//==- HexagonInstrFormatsV60.td - Hexagon Instruction Formats -*- tablegen -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the Hexagon V60 instruction classes in TableGen format.
//
//===----------------------------------------------------------------------===//

//----------------------------------------------------------------------------//
//                         Instruction Classes Definitions +
//----------------------------------------------------------------------------//

class CVI_VA_Resource<dag outs, dag ins, string asmstr,
                       list<dag> pattern = [], string cstr = "",
                       InstrItinClass itin = CVI_VA>
   : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeCVI_VA>,
     OpcodeHexagon, Requires<[HasV60, UseHVX]>;
