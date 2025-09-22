//===--- HexagonIICHVX.td -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def CVI_GATHER_PSEUDO : InstrItinClass;
def CVI_VA            : InstrItinClass;

class HVXItin {
  list<InstrItinData> HVXItin_list = [
    InstrItinData<CVI_VA,
      [InstrStage<1, [SLOT0,SLOT1,SLOT2,SLOT3], 0>,
       InstrStage<1, [CVI_XLANE,CVI_SHIFT, CVI_MPY0, CVI_MPY1]>],
      [9, 7, 7, 7], [HVX_FWD, HVX_FWD, HVX_FWD]>,

    // Used by gather pseudo-instructions which are expanded into V6_vgather*
    // and V6_vS32b_new_ai. Even though these instructions use CVI_LD resource,
    // it's not included below to avoid having more than 4 InstrStages and
    // thus changing 'MaxResTerms' to 5. Instead, both SLOT0 and SLOT1 are
    // used, which should be sufficient.
    InstrItinData <CVI_GATHER_PSEUDO,
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>]>];
}
