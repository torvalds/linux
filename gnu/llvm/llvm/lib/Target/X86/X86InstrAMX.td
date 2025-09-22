//===---- X86InstrAMX.td - AMX Instruction Set Extension --*- tablegen -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions that make up the Intel AMX instruction
// set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// AMX instructions

multiclass AMX_TILE_COMMON<string Suffix, Predicate HasEGPR> {
let Predicates = [HasAMXTILE, HasEGPR, In64BitMode] in {
  let hasSideEffects = 1,
      Defs = [TMM0,TMM1,TMM2,TMM3,TMM4,TMM5,TMM6,TMM7] in
  def LDTILECFG#Suffix : I<0x49, MRM0m, (outs), (ins opaquemem:$src),
                           "ldtilecfg\t$src",
                           [(int_x86_ldtilecfg addr:$src)]>,
                         T8, PS;
  let hasSideEffects = 1 in
  def STTILECFG#Suffix : I<0x49, MRM0m, (outs), (ins opaquemem:$src),
                           "sttilecfg\t$src",
                           [(int_x86_sttilecfg addr:$src)]>,
                         T8, PD;
  let mayLoad = 1 in
  def TILELOADD#Suffix : I<0x4b, MRMSrcMemFSIB, (outs TILE:$dst),
                           (ins sibmem:$src),
                           "tileloadd\t{$src, $dst|$dst, $src}", []>,
                         T8, XD;
  let mayLoad = 1 in
  def TILELOADDT1#Suffix : I<0x4b, MRMSrcMemFSIB, (outs TILE:$dst),
                             (ins sibmem:$src),
                             "tileloaddt1\t{$src, $dst|$dst, $src}", []>,
                           T8, PD;
  let mayStore = 1 in
  def TILESTORED#Suffix : I<0x4b, MRMDestMemFSIB, (outs),
                            (ins sibmem:$dst, TILE:$src),
                            "tilestored\t{$src, $dst|$dst, $src}", []>,
                          T8, XS;
}
}

let SchedRW = [WriteSystem] in {
  defm "" : AMX_TILE_COMMON<"", NoEGPR>, VEX;
  defm "" : AMX_TILE_COMMON<"_EVEX", HasEGPR>, EVEX, NoCD8;

  let Predicates = [HasAMXTILE, In64BitMode] in {
    let Defs = [TMM0,TMM1,TMM2,TMM3,TMM4,TMM5,TMM6,TMM7] in
    def TILERELEASE : I<0x49, MRM_C0, (outs), (ins),
                        "tilerelease", [(int_x86_tilerelease)]>, VEX, T8, PS;
    def TILEZERO : I<0x49, MRMr0, (outs TILE:$dst), (ins),
                     "tilezero\t$dst", []>,
                     VEX, T8, XD;

    // Pseduo instruction for RA.
    let isPseudo = true, mayLoad = 1, hasSideEffects = 1,
        Defs = [TMM0,TMM1,TMM2,TMM3,TMM4,TMM5,TMM6,TMM7] in
    def PLDTILECFGV : PseudoI<(outs), (ins opaquemem:$src), []>;
    let isPseudo = true, mayLoad = 1 in
    def PTILELOADDV : PseudoI<(outs TILE:$dst), (ins GR16:$src1,
                                                     GR16:$src2,
                                                     opaquemem:$src3), []>;
    let isPseudo = true, mayLoad = 1 in
    def PTILELOADDT1V : PseudoI<(outs TILE:$dst), (ins GR16:$src1,
                                                       GR16:$src2,
                                                       opaquemem:$src3), []>;
    let isPseudo = true, mayStore = 1 in
    def PTILESTOREDV : PseudoI<(outs), (ins GR16:$src1,
                                            GR16:$src2, opaquemem:$src3,
                                            TILE:$src4), []>;
    let isPseudo = true, isReMaterializable = 1, isAsCheapAsAMove = 1,
        canFoldAsLoad = 1, usesCustomInserter = 1 in
      def PTILEZEROV : PseudoI<(outs TILE:$dst), (ins GR16:$src1, GR16:$src2),
                                [(set TILE:$dst, (int_x86_tilezero_internal
                                  GR16:$src1, GR16:$src2))]>;

    let usesCustomInserter = 1 in {
      // Pseudo instructions, using immediates instead of tile registers.
      // To be translated to the actual instructions in X86ISelLowering.cpp
      let mayLoad = 1 in
      def PTILELOADD : PseudoI<(outs), (ins u8imm:$src1, sibmem:$src2), []>;
      let mayLoad = 1 in
      def PTILELOADDT1 : PseudoI<(outs), (ins u8imm:$src1,
                                          sibmem:$src2), []>;
      let mayStore = 1 in
      def PTILESTORED : PseudoI<(outs), (ins i8mem:$dst, u8imm:$src), []>;
      def PTILEZERO : PseudoI<(outs), (ins u8imm:$src),
                              [(int_x86_tilezero timm:$src)]>;
    }
  } // Predicates
} // SchedRW

let Predicates = [HasAMXINT8, In64BitMode] in {
  let SchedRW = [WriteSystem] in {
    let Constraints = "$src1 = $dst" in {
      def TDPBSSD : I<0x5e, MRMSrcReg4VOp3, (outs TILE:$dst),
                      (ins TILE:$src1, TILE:$src2, TILE:$src3),
                      "tdpbssd\t{$src3, $src2, $dst|$dst, $src2, $src3}", []>,
                      VEX, VVVV, T8, XD;
      def TDPBSUD : I<0x5e, MRMSrcReg4VOp3, (outs TILE:$dst),
                      (ins TILE:$src1, TILE:$src2, TILE:$src3),
                      "tdpbsud\t{$src3, $src2, $dst|$dst, $src2, $src3}", []>,
                      VEX, VVVV, T8, XS;
      def TDPBUSD : I<0x5e, MRMSrcReg4VOp3, (outs TILE:$dst),
                      (ins TILE:$src1, TILE:$src2, TILE:$src3),
                      "tdpbusd\t{$src3, $src2, $dst|$dst, $src2, $src3}", []>,
                      VEX, VVVV, T8, PD;
      def TDPBUUD : I<0x5e, MRMSrcReg4VOp3, (outs TILE:$dst),
                      (ins TILE:$src1, TILE:$src2, TILE:$src3),
                      "tdpbuud\t{$src3, $src2, $dst|$dst, $src2, $src3}", []>,
                      VEX, VVVV, T8;
    }

    // Pseduo instruction for RA.
    let isPseudo = true, Constraints = "$src4 = $dst" in {
      def PTDPBSSDV : PseudoI<(outs TILE:$dst), (ins GR16:$src1,
                              GR16:$src2, GR16:$src3, TILE:$src4,
                              TILE:$src5, TILE:$src6),
                              [(set TILE: $dst,
                              (int_x86_tdpbssd_internal GR16:$src1, GR16:$src2,
                              GR16:$src3, TILE:$src4, TILE:$src5, TILE:$src6))]>;
      def PTDPBSUDV : PseudoI<(outs TILE: $dst), (ins GR16:$src1,
                              GR16:$src2, GR16:$src3, TILE:$src4,
                              TILE:$src5, TILE:$src6),
                              [(set TILE: $dst,
                              (int_x86_tdpbsud_internal GR16:$src1, GR16:$src2,
                               GR16:$src3, TILE:$src4, TILE:$src5, TILE:$src6))]>;
      def PTDPBUSDV : PseudoI<(outs TILE: $dst), (ins GR16:$src1,
                              GR16:$src2, GR16:$src3, TILE:$src4,
                              TILE:$src5, TILE:$src6),
                              [(set TILE: $dst,
                              (int_x86_tdpbusd_internal GR16:$src1, GR16:$src2,
                              GR16:$src3, TILE:$src4, TILE:$src5, TILE:$src6))]>;
      def PTDPBUUDV : PseudoI<(outs TILE: $dst), (ins GR16:$src1,
                              GR16:$src2, GR16:$src3, TILE:$src4,
                              TILE:$src5, TILE:$src6),
                              [(set TILE: $dst,
                              (int_x86_tdpbuud_internal GR16:$src1, GR16:$src2,
                              GR16:$src3, TILE:$src4, TILE:$src5, TILE:$src6))]>;
    }

    let usesCustomInserter = 1 in {
      // Pseudo instructions, using immediates instead of tile registers.
      // To be translated to the actual instructions in X86ISelLowering.cpp
      def PTDPBSSD : PseudoI<(outs), (ins u8imm:$src1,
                             u8imm:$src2, u8imm:$src3),
                             [(int_x86_tdpbssd timm:$src1,
                               timm:$src2, timm:$src3)]>;
      def PTDPBSUD : PseudoI<(outs), (ins u8imm:$src1,
                             u8imm:$src2, u8imm:$src3),
                             [(int_x86_tdpbsud timm:$src1,
                               timm:$src2, timm:$src3)]>;
      def PTDPBUSD : PseudoI<(outs), (ins u8imm:$src1,
                             u8imm:$src2, u8imm:$src3),
                             [(int_x86_tdpbusd timm:$src1,
                               timm:$src2, timm:$src3)]>;
      def PTDPBUUD : PseudoI<(outs), (ins u8imm:$src1,
                             u8imm:$src2, u8imm:$src3),
                             [(int_x86_tdpbuud timm:$src1,
                               timm:$src2, timm:$src3)]>;
    }
  }
} // HasAMXTILE

let Predicates = [HasAMXBF16, In64BitMode] in {
  let SchedRW = [WriteSystem] in {
    let Constraints = "$src1 = $dst" in
    def TDPBF16PS : I<0x5c, MRMSrcReg4VOp3, (outs TILE:$dst),
                      (ins TILE:$src1, TILE:$src2, TILE:$src3),
                      "tdpbf16ps\t{$src3, $src2, $dst|$dst, $src2, $src3}",
                      []>, VEX, VVVV, T8, XS;

    // Pseduo instruction for RA.
    let isPseudo = true, Constraints = "$src4 = $dst" in
      def PTDPBF16PSV : PseudoI<(outs TILE: $dst), (ins GR16:$src1,
                                 GR16:$src2, GR16:$src3, TILE:$src4,
                                 TILE:$src5, TILE:$src6),
                                 [(set TILE: $dst,
                                  (int_x86_tdpbf16ps_internal GR16:$src1,
                                   GR16:$src2, GR16:$src3, TILE:$src4,
                                   TILE:$src5, TILE:$src6))]>;

    let usesCustomInserter = 1 in {
      // Pseudo instructions, using immediates instead of tile registers.
      // To be translated to the actual instructions in X86ISelLowering.cpp
      def PTDPBF16PS : PseudoI<(outs), (ins u8imm:$src1,
                               u8imm:$src2, u8imm:$src3),
                               [(int_x86_tdpbf16ps timm:$src1,
                                 timm:$src2, timm:$src3)]>;
    }
  }
} // HasAMXTILE, HasAMXBF16

//AMX-FP16
let Predicates = [HasAMXFP16, In64BitMode] in {
  let SchedRW = [WriteSystem] in {
    let Constraints = "$src1 = $dst" in {
      def TDPFP16PS : I<0x5c, MRMSrcReg4VOp3, (outs TILE:$dst),
                        (ins TILE:$src1, TILE:$src2, TILE:$src3),
                        "tdpfp16ps\t{$src3, $src2, $src1|$src1, $src2, $src3}",
                        []>, VEX, VVVV, T8, XD;
    }

    // Pseduo instruction for RA.
    let isPseudo = true, Constraints = "$src4 = $dst" in {
      def PTDPFP16PSV : PseudoI<(outs TILE: $dst), (ins GR16:$src1,
                                 GR16:$src2, GR16:$src3, TILE:$src4,
                                 TILE:$src5, TILE:$src6),
                                 [(set TILE: $dst,
                                  (int_x86_tdpfp16ps_internal GR16:$src1,
                                   GR16:$src2, GR16:$src3, TILE:$src4,
                                   TILE:$src5, TILE:$src6))]>;
    }

    let  usesCustomInserter = 1 in {
      def PTDPFP16PS : PseudoI<(outs), (ins u8imm:$src1,
                               u8imm:$src2, u8imm:$src3),
                               [(int_x86_tdpfp16ps timm:$src1,
                                 timm:$src2, timm:$src3)]>;
    }
  }
} // HasAMXTILE, HasAMXFP16

let Predicates = [HasAMXCOMPLEX, In64BitMode] in {
  let SchedRW = [WriteSystem] in {
    let Constraints = "$src1 = $dst" in {
      def TCMMIMFP16PS   : I<0x6c, MRMSrcReg4VOp3, (outs TILE:$dst),
                            (ins TILE:$src1, TILE:$src2, TILE:$src3),
                            "tcmmimfp16ps\t{$src3, $src2, $src1|$src1, $src2, $src3}",
                            []>, T8, PD, VEX, VVVV;
      def TCMMRLFP16PS : I<0x6c, MRMSrcReg4VOp3, (outs TILE:$dst),
                            (ins TILE:$src1, TILE:$src2, TILE:$src3),
                            "tcmmrlfp16ps\t{$src3, $src2, $src1|$src1, $src2, $src3}",
                            []>, VEX, VVVV, WIG, T8;

    } // Constraints = "$src1 = $dst"

    let Constraints = "$src4 = $dst" in {
      def PTCMMIMFP16PSV : PseudoI<(outs TILE:$dst), (ins GR16:$src1,
                                  GR16:$src2, GR16:$src3, TILE:$src4,
                                  TILE:$src5, TILE:$src6),
                                  [(set TILE: $dst,
                                  (int_x86_tcmmimfp16ps_internal GR16:$src1, GR16:$src2,
                                   GR16:$src3, TILE:$src4, TILE:$src5, TILE:$src6))]>;
      def PTCMMRLFP16PSV : PseudoI<(outs TILE:$dst), (ins GR16:$src1,
                                  GR16:$src2, GR16:$src3, TILE:$src4,
                                  TILE:$src5, TILE:$src6),
                                  [(set TILE: $dst,
                                  (int_x86_tcmmrlfp16ps_internal GR16:$src1, GR16:$src2,
                                   GR16:$src3, TILE:$src4, TILE:$src5, TILE:$src6))]>;
    }

    let usesCustomInserter = 1 in {
      def PTCMMIMFP16PS : PseudoI<(outs), (ins u8imm:$src1,
                                u8imm:$src2, u8imm:$src3),
                                [(int_x86_tcmmimfp16ps timm:$src1,
                                  timm:$src2, timm:$src3)]>;
      def PTCMMRLFP16PS : PseudoI<(outs), (ins u8imm:$src1,
                                u8imm:$src2, u8imm:$src3),
                                [(int_x86_tcmmrlfp16ps timm:$src1,
                                  timm:$src2, timm:$src3)]>;
    }
  } // SchedRW = [WriteSystem]
}
