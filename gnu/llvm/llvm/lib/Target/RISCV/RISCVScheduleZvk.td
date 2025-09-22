//=== RISCVScheduleZvk.td - RISC-V Scheduling Definitions Zvk -*- tablegen ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/// Define scheduler resources associated with def operands.

/// Zvbb extension
defm "" : LMULSchedWrites<"WriteVBREVV">;
defm "" : LMULSchedWrites<"WriteVCLZV">;
defm "" : LMULSchedWrites<"WriteVCPOPV">;
defm "" : LMULSchedWrites<"WriteVCTZV">;
defm "" : LMULSchedWrites<"WriteVWSLLV">;
defm "" : LMULSchedWrites<"WriteVWSLLX">;
defm "" : LMULSchedWrites<"WriteVWSLLI">;

/// Zvbc extension
defm "" : LMULSchedWrites<"WriteVCLMULV">;
defm "" : LMULSchedWrites<"WriteVCLMULX">;

/// Zvkb extension
// VANDN uses WriteVIALU[V|X|I]
defm "" : LMULSchedWrites<"WriteVBREV8V">;
defm "" : LMULSchedWrites<"WriteVREV8V">;
defm "" : LMULSchedWrites<"WriteVRotV">;
defm "" : LMULSchedWrites<"WriteVRotX">;
defm "" : LMULSchedWrites<"WriteVRotI">;

/// Zvkg extension
defm "" : LMULSchedWrites<"WriteVGHSHV">;
defm "" : LMULSchedWrites<"WriteVGMULV">;

/// Zvknha or Zvknhb extensions
defm "" : LMULSchedWrites<"WriteVSHA2CHV">;
defm "" : LMULSchedWrites<"WriteVSHA2CLV">;
defm "" : LMULSchedWrites<"WriteVSHA2MSV">;

/// Zvkned extension
defm "" : LMULSchedWrites<"WriteVAESMVV">;
defm "" : LMULSchedWrites<"WriteVAESKF1V">;
defm "" : LMULSchedWrites<"WriteVAESKF2V">;
defm "" : LMULSchedWrites<"WriteVAESZV">;

/// Zvksed extension
defm "" : LMULSchedWrites<"WriteVSM4KV">;
defm "" : LMULSchedWrites<"WriteVSM4RV">;

/// Zvksh extension
defm "" : LMULSchedWrites<"WriteVSM3CV">;
defm "" : LMULSchedWrites<"WriteVSM3MEV">;

/// Define scheduler resources associated with use operands.
/// Zvbb extension
defm "" : LMULSchedReads<"ReadVBREVV">;
defm "" : LMULSchedReads<"ReadVCLZV">;
defm "" : LMULSchedReads<"ReadVCPOPV">;
defm "" : LMULSchedReads<"ReadVCTZV">;
defm "" : LMULSchedReads<"ReadVWSLLV">;
defm "" : LMULSchedReads<"ReadVWSLLX">;

/// Zvbc extension
defm "" : LMULSchedReads<"ReadVCLMULV">;
defm "" : LMULSchedReads<"ReadVCLMULX">;

/// Zvkb extension
// VANDN uses ReadVIALU[V|X|I]
defm "" : LMULSchedReads<"ReadVBREV8V">;
defm "" : LMULSchedReads<"ReadVREV8V">;
defm "" : LMULSchedReads<"ReadVRotV">;
defm "" : LMULSchedReads<"ReadVRotX">;

/// Zvkg extension
defm "" : LMULSchedReads<"ReadVGHSHV">;
defm "" : LMULSchedReads<"ReadVGMULV">;

/// Zvknha or Zvknhb extensions
defm "" : LMULSchedReads<"ReadVSHA2CHV">;
defm "" : LMULSchedReads<"ReadVSHA2CLV">;
defm "" : LMULSchedReads<"ReadVSHA2MSV">;

/// Zvkned extension
defm "" : LMULSchedReads<"ReadVAESMVV">;
defm "" : LMULSchedReads<"ReadVAESKF1V">;
defm "" : LMULSchedReads<"ReadVAESKF2V">;
defm "" : LMULSchedReads<"ReadVAESZV">;

/// Zvksed extension
defm "" : LMULSchedReads<"ReadVSM4KV">;
defm "" : LMULSchedReads<"ReadVSM4RV">;

/// Zvksh extension
defm "" : LMULSchedReads<"ReadVSM3CV">;
defm "" : LMULSchedReads<"ReadVSM3MEV">;

multiclass UnsupportedSchedZvbb {
let Unsupported = true in {
defm "" : LMULWriteRes<"WriteVBREVV", []>;
defm "" : LMULWriteRes<"WriteVCLZV", []>;
defm "" : LMULWriteRes<"WriteVCPOPV", []>;
defm "" : LMULWriteRes<"WriteVCTZV", []>;
defm "" : LMULWriteRes<"WriteVWSLLV", []>;
defm "" : LMULWriteRes<"WriteVWSLLX", []>;
defm "" : LMULWriteRes<"WriteVWSLLI", []>;

defm "" : LMULReadAdvance<"ReadVBREVV", 0>;
defm "" : LMULReadAdvance<"ReadVCLZV", 0>;
defm "" : LMULReadAdvance<"ReadVCPOPV", 0>;
defm "" : LMULReadAdvance<"ReadVCTZV", 0>;
defm "" : LMULReadAdvance<"ReadVWSLLV", 0>;
defm "" : LMULReadAdvance<"ReadVWSLLX", 0>;
}
}

multiclass UnsupportedSchedZvbc {
let Unsupported = true in {
defm "" : LMULWriteRes<"WriteVCLMULV", []>;
defm "" : LMULWriteRes<"WriteVCLMULX", []>;

defm "" : LMULReadAdvance<"ReadVCLMULV", 0>; 
defm "" : LMULReadAdvance<"ReadVCLMULX", 0>;
}
}

multiclass UnsupportedSchedZvkb {
let Unsupported = true in {
defm "" : LMULWriteRes<"WriteVBREV8V", []>;
defm "" : LMULWriteRes<"WriteVREV8V", []>;
defm "" : LMULWriteRes<"WriteVRotV", []>;
defm "" : LMULWriteRes<"WriteVRotX", []>;
defm "" : LMULWriteRes<"WriteVRotI", []>;

defm "" : LMULReadAdvance<"ReadVBREV8V", 0>;
defm "" : LMULReadAdvance<"ReadVREV8V", 0>;
defm "" : LMULReadAdvance<"ReadVRotV", 0>;
defm "" : LMULReadAdvance<"ReadVRotX", 0>;
}
}

multiclass UnsupportedSchedZvkg {
let Unsupported = true in {
defm "" : LMULWriteRes<"WriteVGHSHV", []>;
defm "" : LMULWriteRes<"WriteVGMULV", []>;

defm "" : LMULReadAdvance<"ReadVGHSHV", 0>;
defm "" : LMULReadAdvance<"ReadVGMULV", 0>;
}
}

multiclass UnsupportedSchedZvknhaOrZvknhb {
let Unsupported = true in {
defm "" : LMULWriteRes<"WriteVSHA2CHV", []>;
defm "" : LMULWriteRes<"WriteVSHA2CLV", []>;
defm "" : LMULWriteRes<"WriteVSHA2MSV", []>;

defm "" : LMULReadAdvance<"ReadVSHA2CHV", 0>;
defm "" : LMULReadAdvance<"ReadVSHA2CLV", 0>;
defm "" : LMULReadAdvance<"ReadVSHA2MSV", 0>;
}
}

multiclass UnsupportedSchedZvkned {
let Unsupported = true in {
defm "" : LMULWriteRes<"WriteVAESMVV", []>;
defm "" : LMULWriteRes<"WriteVAESKF1V", []>;
defm "" : LMULWriteRes<"WriteVAESKF2V", []>;
defm "" : LMULWriteRes<"WriteVAESZV", []>;

defm "" : LMULReadAdvance<"ReadVAESMVV", 0>;
defm "" : LMULReadAdvance<"ReadVAESKF1V", 0>;
defm "" : LMULReadAdvance<"ReadVAESKF2V", 0>;
defm "" : LMULReadAdvance<"ReadVAESZV", 0>;
}
}

multiclass UnsupportedSchedZvksed {
let Unsupported = true in {
defm "" : LMULWriteRes<"WriteVSM4KV", []>;
defm "" : LMULWriteRes<"WriteVSM4RV", []>;

defm "" : LMULReadAdvance<"ReadVSM4KV", 0>;
defm "" : LMULReadAdvance<"ReadVSM4RV", 0>;
}
}

multiclass UnsupportedSchedZvksh {
let Unsupported = true in {
defm "" : LMULWriteRes<"WriteVSM3CV", []>;
defm "" : LMULWriteRes<"WriteVSM3MEV", []>;

defm "" : LMULReadAdvance<"ReadVSM3CV", 0>;
defm "" : LMULReadAdvance<"ReadVSM3MEV", 0>;
}
}

// Helper class to define all RISC-V Vector Crypto extensions as unsupported
multiclass UnsupportedSchedZvk {
defm "" : UnsupportedSchedZvbb;
defm "" : UnsupportedSchedZvbc;
defm "" : UnsupportedSchedZvkb;
defm "" : UnsupportedSchedZvkg;
defm "" : UnsupportedSchedZvknhaOrZvknhb;
defm "" : UnsupportedSchedZvkned;
defm "" : UnsupportedSchedZvksed;
defm "" : UnsupportedSchedZvksh;
}
