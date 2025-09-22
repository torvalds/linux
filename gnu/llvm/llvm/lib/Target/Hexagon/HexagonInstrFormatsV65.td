//==- HexagonInstrFormatsV65.td - Hexagon Instruction Formats -*- tablegen -==//
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
//                         Hexagon Instruction Flags +
//
//                        *** Must match BaseInfo.h ***
//----------------------------------------------------------------------------//

//----------------------------------------------------------------------------//
//                         Instruction Classes Definitions +
//----------------------------------------------------------------------------//

class CVI_VA_Resource_NoOpcode<dag outs, dag ins, string asmstr,
                       list<dag> pattern = [], string cstr = "",
                       InstrItinClass itin = CVI_VA>
   : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeCVI_VA>;

class CVI_GATHER_TMP_LD_Resource_NoOpcode<dag outs, dag ins, string asmstr,
                        list<dag> pattern = [], string cstr = "",
                        InstrItinClass itin = CVI_GATHER_PSEUDO>
   : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeCVI_GATHER>;
