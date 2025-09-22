//=- HexagonScheduleV67T.td - Hexagon V67 Tiny Core Scheduling Definitions --=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class HexagonV67TPseudoItin {
  list<InstrItinData> V67TPseudoItin_list = [
    InstrItinData<PSEUDO, [InstrStage<1, [SLOT0, SLOT2, SLOT3]>], [2, 1, 1],
                          [Hex_FWD, Hex_FWD, Hex_FWD]>,
    InstrItinData<PSEUDOM, [InstrStage<1, [SLOT2, SLOT3], 0>,
                            InstrStage<1, [SLOT2, SLOT3]>],
                           [2, 1, 1],
                           [Hex_FWD, Hex_FWD, Hex_FWD]>,
    InstrItinData<DUPLEX, [InstrStage<1, [SLOT0]>],
                          [2, 1, 1]>,
    InstrItinData<tc_ENDLOOP, [InstrStage<1, [SLOT_ENDLOOP]>], [2]>
  ];
}

// V67TItin_list and HVXItin contain some old itineraries
// still used by a handful of instructions. Hopefully, we will be able to
// get rid of them soon.
def HexagonV67TItinList : DepScalarItinV67T,
                          DepHVXItinV67, HVXItin, HexagonV67TPseudoItin {
  list<InstrItinData> V67TItin_list = [
    InstrItinData<LD_tc_ld_SLOT01, [InstrStage<1, [SLOT0]>],
                                   [3, 1, 1],
                                   [Hex_FWD, Hex_FWD, Hex_FWD]>,
    InstrItinData<ST_tc_st_SLOT01, [InstrStage<1, [SLOT0]>],
                                   [1, 1, 3, 3],
                                   [Hex_FWD, Hex_FWD]>
  ];

  list<InstrItinData> ItinList =
    !listconcat(DepScalarItinV67T_list,
                DepHVXItinV67_list, V67TItin_list,
                HVXItin_list, V67TPseudoItin_list);
}

def HexagonItinerariesV67T :
      ProcessorItineraries<[SLOT0, SLOT1, SLOT2, SLOT3, SLOT_ENDLOOP,
                            CVI_ST, CVI_XLANE, CVI_SHIFT, CVI_MPY0, CVI_MPY1,
                            CVI_LD, CVI_XLSHF, CVI_MPY01, CVI_ALL,
                            CVI_ALL_NOMEM, CVI_ZW],
                            [Hex_FWD, HVX_FWD],
                            HexagonV67TItinList.ItinList>;


def HexagonModelV67T : SchedMachineModel {
  let IssueWidth = 3;
  let Itineraries = HexagonItinerariesV67T;
  let LoadLatency = 1;
  let CompleteModel = 0;
}

//===----------------------------------------------------------------------===//
// Hexagon V67 Tiny Core Resource Definitions -
//===----------------------------------------------------------------------===//
