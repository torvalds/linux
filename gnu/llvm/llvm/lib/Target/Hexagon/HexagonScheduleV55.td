//=-HexagonScheduleV55.td - HexagonV55 Scheduling Definitions -*- tablegen -*=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


class HexagonV55PseudoItin {
  list<InstrItinData> V55PseudoItin_list = [
    InstrItinData<PSEUDO, [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3]>],
                          [1, 1, 1]>,
    InstrItinData<PSEUDOM, [InstrStage<1, [SLOT2, SLOT3], 0>,
                            InstrStage<1, [SLOT2, SLOT3]>], [1, 1, 1]>,
    InstrItinData<DUPLEX,     [InstrStage<1, [SLOT0]>], [1, 1, 1]>,
    InstrItinData<tc_ENDLOOP, [InstrStage<1, [SLOT_ENDLOOP]>], [2]>
  ];
}

def HexagonV55ItinList : DepScalarItinV55,
                         HexagonV55PseudoItin {
  list<InstrItinData> V55Itin_list = [
    InstrItinData<LD_tc_ld_SLOT01, [InstrStage<1, [SLOT0, SLOT1]>], [2, 1]>,
    InstrItinData<ST_tc_st_SLOT01, [InstrStage<1, [SLOT0, SLOT1]>],
                                   [1, 1, 1]>
  ];
  list<InstrItinData> ItinList =
    !listconcat(V55Itin_list, DepScalarItinV55_list,
                V55PseudoItin_list);
}

def HexagonItinerariesV55 :
      ProcessorItineraries<[SLOT0, SLOT1, SLOT2, SLOT3, SLOT_ENDLOOP],
                           [Hex_FWD], HexagonV55ItinList.ItinList>;

def HexagonModelV55 : SchedMachineModel {
  // Max issue per cycle == bundle width.
  let IssueWidth = 4;
  let Itineraries = HexagonItinerariesV55;
  let LoadLatency = 1;
  let CompleteModel = 0;
}

//===----------------------------------------------------------------------===//
// Hexagon V55 Resource Definitions -
//===----------------------------------------------------------------------===//
