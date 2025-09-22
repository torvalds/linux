//=-HexagonScheduleV71T.td - Hexagon V71 Tiny Core Scheduling Definition ----=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

class HexagonV71TPseudoItin {
  list<InstrItinData> V71TPseudoItin_list = [
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

//
// HVXItin contains some old itineraries still used by a handful of
// instructions. Hopefully, we will be able to get rid of them soon.
def HexagonV71TItinList : DepScalarItinV71T, DepHVXItinV71, HVXItin,
                          HexagonV71TPseudoItin {
  list<InstrItinData> V71TItin_list = [
    InstrItinData<LD_tc_ld_SLOT01, [InstrStage<1, [SLOT0]>],
                                   [3, 1, 1],
                                   [Hex_FWD, Hex_FWD, Hex_FWD]>,
    InstrItinData<ST_tc_st_SLOT01, [InstrStage<1, [SLOT0]>],
                                   [1, 1, 3, 3],
                                   [Hex_FWD, Hex_FWD]>
  ];
  list<InstrItinData> ItinList =
    !listconcat(DepScalarItinV71T_list, V71TItin_list, DepHVXItinV71_list,
                HVXItin_list, V71TPseudoItin_list);
}

def HexagonItinerariesV71T :
      ProcessorItineraries<[SLOT0, SLOT1, SLOT2, SLOT3, SLOT_ENDLOOP,
                            CVI_ST, CVI_XLANE, CVI_SHIFT, CVI_MPY0, CVI_MPY1,
                            CVI_LD, CVI_XLSHF, CVI_MPY01, CVI_ALL,
                            CVI_ALL_NOMEM, CVI_ZW],
                            [Hex_FWD, HVX_FWD],
                            HexagonV71TItinList.ItinList>;

def HexagonModelV71T : SchedMachineModel {
  let IssueWidth = 3;
  let Itineraries = HexagonItinerariesV71T;
  let LoadLatency = 1;
  let CompleteModel = 0;
}

//===----------------------------------------------------------------------===//
// Hexagon V71 Tiny Core Resource Definitions -
//===----------------------------------------------------------------------===//
