//=- AArch64SchedA57WriteRes.td - ARM Cortex-A57 Write Res ---*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains all of the Cortex-A57 specific SchedWriteRes types. The approach
// below is to define a generic SchedWriteRes for every combination of
// latency and microOps. The naming conventions is to use a prefix, one field
// for latency, and one or more microOp count/type designators.
//   Prefix: A57Write
//   Latency: #cyc
//   MicroOp Count/Types: #(B|I|M|L|S|X|W|V)
//   Postfix (optional): (XYZ)_Forward
//
//   The postfix is added to differentiate SchedWriteRes that are used in
//   subsequent SchedReadAdvances.
//
// e.g. A57Write_6cyc_1I_6S_4V means the total latency is 6 and there are
//      11 micro-ops to be issued down one I pipe, six S pipes and four V pipes.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Define Generic 1 micro-op types

def A57Write_5cyc_1L  : SchedWriteRes<[A57UnitL]> { let Latency = 5;  }
def A57Write_5cyc_1M  : SchedWriteRes<[A57UnitM]> { let Latency = 5;  }
def A57Write_5cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 5;  }
def A57Write_5cyc_1V_FP_Forward  : SchedWriteRes<[A57UnitV]> { let Latency = 5; }
def A57Write_5cyc_1W  : SchedWriteRes<[A57UnitW]> { let Latency = 5;  }
def A57Write_5cyc_1W_Mul_Forward  : SchedWriteRes<[A57UnitW]> { let Latency = 5;  }
def A57Write_10cyc_1V : SchedWriteRes<[A57UnitV]> { let Latency = 10; }
def A57Write_17cyc_1W : SchedWriteRes<[A57UnitW]> { let Latency = 17;
                                                    let ReleaseAtCycles = [17]; }
def A57Write_19cyc_1M : SchedWriteRes<[A57UnitM]> { let Latency = 19;
                                                    let ReleaseAtCycles = [19]; }
def A57Write_1cyc_1B  : SchedWriteRes<[A57UnitB]> { let Latency = 1;  }
def A57Write_1cyc_1I  : SchedWriteRes<[A57UnitI]> { let Latency = 1;  }
def A57Write_1cyc_1S  : SchedWriteRes<[A57UnitS]> { let Latency = 1;  }
def A57Write_2cyc_1M  : SchedWriteRes<[A57UnitM]> { let Latency = 2;  }
def A57Write_32cyc_1W : SchedWriteRes<[A57UnitW]> { let Latency = 32;
                                                    let ReleaseAtCycles = [32]; }
def A57Write_35cyc_1M : SchedWriteRes<[A57UnitM]> { let Latency = 35;
                                                    let ReleaseAtCycles = [35]; }
def A57Write_3cyc_1M  : SchedWriteRes<[A57UnitM]> { let Latency = 3;  }
def A57Write_3cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 3;  }
def A57Write_3cyc_1W  : SchedWriteRes<[A57UnitW]> { let Latency = 3;  }
def A57Write_3cyc_1X  : SchedWriteRes<[A57UnitX]> { let Latency = 3;  }
def A57Write_4cyc_1L  : SchedWriteRes<[A57UnitL]> { let Latency = 4;  }
def A57Write_4cyc_1X  : SchedWriteRes<[A57UnitX]> { let Latency = 4;  }
def A57Write_4cyc_1X_NonMul_Forward  : SchedWriteRes<[A57UnitX]> { let Latency = 4;  }
def A57Write_9cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 9;  }
def A57Write_6cyc_1M  : SchedWriteRes<[A57UnitM]> { let Latency = 6;  }
def A57Write_6cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 6;  }


//===----------------------------------------------------------------------===//
// Define Generic 2 micro-op types

def A57Write_64cyc_2W    : SchedWriteRes<[A57UnitW, A57UnitW]> {
  let Latency     = 64;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [32, 32];
}
def A57Write_6cyc_1I_1L  : SchedWriteRes<[A57UnitI,
                                          A57UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_7cyc_1V_1X  : SchedWriteRes<[A57UnitV,
                                          A57UnitX]> {
  let Latency     = 7;
  let NumMicroOps = 2;
}
def A57Write_8cyc_1L_1V  : SchedWriteRes<[A57UnitL,
                                          A57UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 2;
}
def A57Write_9cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 2;
}
def A57Write_8cyc_2X     : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 8;
  let NumMicroOps = 2;
}
def A57Write_6cyc_2L     : SchedWriteRes<[A57UnitL, A57UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_6cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_6cyc_2W     : SchedWriteRes<[A57UnitW, A57UnitW]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_6cyc_2W_Mul_Forward     : SchedWriteRes<[A57UnitW, A57UnitW]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_5cyc_1I_1L  : SchedWriteRes<[A57UnitI,
                                          A57UnitL]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_5cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_5cyc_2V_FP_Forward     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_5cyc_2X     : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_5cyc_2X_NonMul_Forward     : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_10cyc_1L_1V : SchedWriteRes<[A57UnitL,
                                          A57UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 2;
}
def A57Write_10cyc_2V    : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 2;
}
def A57Write_1cyc_1B_1I  : SchedWriteRes<[A57UnitB,
                                          A57UnitI]> {
  let Latency     = 1;
  let NumMicroOps = 2;
}
def A57Write_1cyc_1I_1S  : SchedWriteRes<[A57UnitI,
                                          A57UnitS]> {
  let Latency     = 1;
  let NumMicroOps = 2;
}
def A57Write_2cyc_1B_1I  : SchedWriteRes<[A57UnitB,
                                          A57UnitI]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}
def A57Write_2cyc_2S     : SchedWriteRes<[A57UnitS, A57UnitS]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}
def A57Write_2cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}
def A57Write_34cyc_2W    : SchedWriteRes<[A57UnitW, A57UnitW]> {
  let Latency     = 34;
  let NumMicroOps = 2;
  let ReleaseAtCycles = [17, 17];
}
def A57Write_3cyc_1I_1M  : SchedWriteRes<[A57UnitI,
                                          A57UnitM]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_3cyc_1I_1S  : SchedWriteRes<[A57UnitI,
                                          A57UnitS]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_3cyc_1S_1V  : SchedWriteRes<[A57UnitS,
                                          A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_3cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_4cyc_1I_1L  : SchedWriteRes<[A57UnitI,
                                          A57UnitL]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}
def A57Write_4cyc_2X     : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}


//===----------------------------------------------------------------------===//
// Define Generic 3 micro-op types

def A57Write_10cyc_3V       : SchedWriteRes<[A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 3;
}
def A57Write_2cyc_1I_2S     : SchedWriteRes<[A57UnitI,
                                             A57UnitS, A57UnitS]> {
  let Latency     = 2;
  let NumMicroOps = 3;
}
def A57Write_3cyc_1I_1S_1V  : SchedWriteRes<[A57UnitI,
                                             A57UnitS,
                                             A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 3;
}
def A57Write_3cyc_1M_2S     : SchedWriteRes<[A57UnitM,
                                             A57UnitS, A57UnitS]> {
  let Latency     = 3;
  let NumMicroOps = 3;
}
def A57Write_3cyc_3S        : SchedWriteRes<[A57UnitS, A57UnitS, A57UnitS]> {
  let Latency     = 3;
  let NumMicroOps = 3;
}
def A57Write_3cyc_2S_1V     : SchedWriteRes<[A57UnitS, A57UnitS,
                                             A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 3;
}
def A57Write_5cyc_1I_2L     : SchedWriteRes<[A57UnitI,
                                             A57UnitL, A57UnitL]> {
  let Latency     = 5;
  let NumMicroOps = 3;
}
def A57Write_6cyc_1I_2L     : SchedWriteRes<[A57UnitI,
                                             A57UnitL, A57UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 3;
}
def A57Write_6cyc_3V        : SchedWriteRes<[A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 3;
}
def A57Write_7cyc_3L        : SchedWriteRes<[A57UnitL, A57UnitL, A57UnitL]> {
  let Latency     = 7;
  let NumMicroOps = 3;
}
def A57Write_8cyc_1I_1L_1V  : SchedWriteRes<[A57UnitI,
                                             A57UnitL,
                                             A57UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 3;
}
def A57Write_8cyc_1L_2V     : SchedWriteRes<[A57UnitL,
                                             A57UnitV, A57UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 3;
}
def A57Write_8cyc_3V        : SchedWriteRes<[A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 3;
}
def A57Write_9cyc_3V        : SchedWriteRes<[A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 3;
}


//===----------------------------------------------------------------------===//
// Define Generic 4 micro-op types

def A57Write_2cyc_2I_2S    : SchedWriteRes<[A57UnitI, A57UnitI,
                                            A57UnitS, A57UnitS]> {
  let Latency     = 2;
  let NumMicroOps = 4;
}
def A57Write_3cyc_2I_2S    : SchedWriteRes<[A57UnitI, A57UnitI,
                                            A57UnitS, A57UnitS]> {
  let Latency     = 3;
  let NumMicroOps = 4;
}
def A57Write_3cyc_1I_3S    : SchedWriteRes<[A57UnitI,
                                            A57UnitS, A57UnitS, A57UnitS]> {
  let Latency     = 3;
  let NumMicroOps = 4;
}
def A57Write_3cyc_1I_2S_1V : SchedWriteRes<[A57UnitI,
                                            A57UnitS, A57UnitS,
                                            A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 4;
}
def A57Write_4cyc_4S       : SchedWriteRes<[A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS]> {
  let Latency     = 4;
  let NumMicroOps = 4;
}
def A57Write_7cyc_1I_3L    : SchedWriteRes<[A57UnitI,
                                            A57UnitL, A57UnitL, A57UnitL]> {
  let Latency     = 7;
  let NumMicroOps = 4;
}
def A57Write_5cyc_2I_2L    : SchedWriteRes<[A57UnitI, A57UnitI,
                                            A57UnitL, A57UnitL]> {
  let Latency     = 5;
  let NumMicroOps = 4;
}
def A57Write_8cyc_1I_1L_2V : SchedWriteRes<[A57UnitI,
                                            A57UnitL,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 4;
}
def A57Write_8cyc_4L       : SchedWriteRes<[A57UnitL, A57UnitL,
                                            A57UnitL, A57UnitL]> {
  let Latency     = 8;
  let NumMicroOps = 4;
}
def A57Write_9cyc_2L_2V    : SchedWriteRes<[A57UnitL, A57UnitL,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 4;
}
def A57Write_9cyc_1L_3V    : SchedWriteRes<[A57UnitL,
                                            A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 4;
}
def A57Write_12cyc_4V      : SchedWriteRes<[A57UnitV, A57UnitV,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 12;
  let NumMicroOps = 4;
}


//===----------------------------------------------------------------------===//
// Define Generic 5 micro-op types

def A57Write_3cyc_3S_2V    : SchedWriteRes<[A57UnitS, A57UnitS, A57UnitS,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 5;
}
def A57Write_8cyc_1I_4L    : SchedWriteRes<[A57UnitI,
                                            A57UnitL, A57UnitL,
                                            A57UnitL, A57UnitL]> {
  let Latency     = 8;
  let NumMicroOps = 5;
}
def A57Write_4cyc_1I_4S    : SchedWriteRes<[A57UnitI,
                                            A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS]> {
  let Latency     = 4;
  let NumMicroOps = 5;
}
def A57Write_9cyc_1I_2L_2V : SchedWriteRes<[A57UnitI,
                                            A57UnitL, A57UnitL,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 5;
}
def A57Write_9cyc_1I_1L_3V : SchedWriteRes<[A57UnitI,
                                            A57UnitL,
                                            A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 5;
}
def A57Write_9cyc_2L_3V    : SchedWriteRes<[A57UnitL, A57UnitL,
                                            A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 5;
}
def A57Write_9cyc_5V       : SchedWriteRes<[A57UnitV, A57UnitV, A57UnitV,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 5;
}


//===----------------------------------------------------------------------===//
// Define Generic 6 micro-op types

def A57Write_3cyc_1I_3S_2V : SchedWriteRes<[A57UnitI,
                                            A57UnitS, A57UnitS, A57UnitS,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 6;
}
def A57Write_4cyc_2I_4S    : SchedWriteRes<[A57UnitI, A57UnitI,
                                            A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS]> {
  let Latency     = 4;
  let NumMicroOps = 6;
}
def A57Write_4cyc_4S_2V    : SchedWriteRes<[A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 6;
}
def A57Write_6cyc_6S       : SchedWriteRes<[A57UnitS, A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS, A57UnitS]> {
  let Latency     = 6;
  let NumMicroOps = 6;
}
def A57Write_9cyc_1I_2L_3V : SchedWriteRes<[A57UnitI,
                                            A57UnitL, A57UnitL,
                                            A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 6;
}
def A57Write_9cyc_1I_1L_4V : SchedWriteRes<[A57UnitI,
                                            A57UnitL,
                                            A57UnitV, A57UnitV,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 6;
}
def A57Write_9cyc_2L_4V    : SchedWriteRes<[A57UnitL, A57UnitL,
                                            A57UnitV, A57UnitV,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 6;
}


//===----------------------------------------------------------------------===//
// Define Generic 7 micro-op types

def A57Write_10cyc_3L_4V : SchedWriteRes<[A57UnitL, A57UnitL, A57UnitL,
                                          A57UnitV, A57UnitV,
                                          A57UnitV, A57UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 7;
}
def A57Write_4cyc_1I_4S_2V  : SchedWriteRes<[A57UnitI,
                                             A57UnitS, A57UnitS,
                                             A57UnitS, A57UnitS,
                                             A57UnitV, A57UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 7;
}
def A57Write_6cyc_1I_6S     : SchedWriteRes<[A57UnitI,
                                          A57UnitS, A57UnitS, A57UnitS,
                                          A57UnitS, A57UnitS, A57UnitS]> {
  let Latency     = 6;
  let NumMicroOps = 7;
}
def A57Write_9cyc_1I_2L_4V  : SchedWriteRes<[A57UnitI,
                                             A57UnitL, A57UnitL,
                                             A57UnitV, A57UnitV,
                                             A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 7;
}
def A57Write_12cyc_7V       : SchedWriteRes<[A57UnitV, A57UnitV, A57UnitV,
                                             A57UnitV, A57UnitV,
                                             A57UnitV, A57UnitV]> {
  let Latency     = 12;
  let NumMicroOps = 7;
}


//===----------------------------------------------------------------------===//
// Define Generic 8 micro-op types

def A57Write_10cyc_1I_3L_4V : SchedWriteRes<[A57UnitI,
                                             A57UnitL, A57UnitL, A57UnitL,
                                             A57UnitV, A57UnitV,
                                             A57UnitV, A57UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 8;
}
def A57Write_11cyc_4L_4V : SchedWriteRes<[A57UnitL, A57UnitL,
                                          A57UnitL, A57UnitL,
                                          A57UnitV, A57UnitV,
                                          A57UnitV, A57UnitV]> {
  let Latency     = 11;
  let NumMicroOps = 8;
}
def A57Write_8cyc_8S  : SchedWriteRes<[A57UnitS, A57UnitS,
                                       A57UnitS, A57UnitS,
                                       A57UnitS, A57UnitS,
                                       A57UnitS, A57UnitS]> {
  let Latency     = 8;
  let NumMicroOps = 8;
}


//===----------------------------------------------------------------------===//
// Define Generic 9 micro-op types

def A57Write_8cyc_1I_8S     : SchedWriteRes<[A57UnitI,
                                            A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS]> {
  let Latency     = 8;
  let NumMicroOps = 9;
}
def A57Write_11cyc_1I_4L_4V : SchedWriteRes<[A57UnitI,
                                             A57UnitL, A57UnitL,
                                             A57UnitL, A57UnitL,
                                             A57UnitV, A57UnitV,
                                             A57UnitV, A57UnitV]> {
  let Latency     = 11;
  let NumMicroOps = 9;
}
def A57Write_15cyc_9V       : SchedWriteRes<[A57UnitV, A57UnitV, A57UnitV,
                                             A57UnitV, A57UnitV, A57UnitV,
                                             A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 15;
  let NumMicroOps = 9;
}


//===----------------------------------------------------------------------===//
// Define Generic 10 micro-op types

def A57Write_6cyc_6S_4V : SchedWriteRes<[A57UnitS, A57UnitS, A57UnitS,
                                         A57UnitS, A57UnitS, A57UnitS,
                                         A57UnitV, A57UnitV,
                                         A57UnitV, A57UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 10;
}


//===----------------------------------------------------------------------===//
// Define Generic 11 micro-op types

def A57Write_6cyc_1I_6S_4V : SchedWriteRes<[A57UnitI,
                                            A57UnitS, A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS, A57UnitS,
                                            A57UnitV, A57UnitV,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 11;
}


//===----------------------------------------------------------------------===//
// Define Generic 12 micro-op types

def A57Write_8cyc_8S_4V : SchedWriteRes<[A57UnitS, A57UnitS, A57UnitS, A57UnitS,
                                         A57UnitS, A57UnitS, A57UnitS, A57UnitS,
                                         A57UnitV, A57UnitV,
                                         A57UnitV, A57UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 12;
}

//===----------------------------------------------------------------------===//
// Define Generic 13 micro-op types

def A57Write_8cyc_1I_8S_4V : SchedWriteRes<[A57UnitI,
                                            A57UnitS, A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS, A57UnitS,
                                            A57UnitS, A57UnitS,
                                            A57UnitV, A57UnitV,
                                            A57UnitV, A57UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 13;
}

