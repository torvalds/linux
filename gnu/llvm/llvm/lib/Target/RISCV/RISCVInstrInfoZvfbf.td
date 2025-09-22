//===-- RISCVInstrInfoZvfbf.td - 'Zvfbf*' instructions -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'Zvfbfmin'
// extension, providing vector conversion instructions for BFloat16.
// This version is still experimental as the 'Zvfbfmin' extension hasn't been
// ratified yet.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZvfbfmin], Constraints = "@earlyclobber $vd",
    mayRaiseFPException = true in {
let RVVConstraint = WidenCvt in
defm VFWCVTBF16_F_F_V : VWCVTF_FV_VS2<"vfwcvtbf16.f.f.v", 0b010010, 0b01101>;
let Uses = [FRM] in
defm VFNCVTBF16_F_F_W : VNCVTF_FV_VS2<"vfncvtbf16.f.f.w", 0b010010, 0b11101>;
}

let Predicates = [HasStdExtZvfbfwma],
    Constraints = "@earlyclobber $vd_wb, $vd = $vd_wb",
    RVVConstraint = WidenV, Uses = [FRM], mayRaiseFPException = true in {
defm VFWMACCBF16_V : VWMAC_FV_V_F<"vfwmaccbf16", 0b111011>;
}
