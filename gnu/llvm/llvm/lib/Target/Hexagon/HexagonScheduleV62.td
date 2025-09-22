//=-HexagonScheduleV62.td - HexagonV62 Scheduling Definitions *- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ScalarItin contains some old itineraries still used by a
// handful of instructions. Hopefully, we will be able to get rid of them soon.

def HexagonV62ItinList : DepScalarItinV62, ScalarItin,
                         DepHVXItinV62, HVXItin, PseudoItin {
  list<InstrItinData> ItinList =
    !listconcat(DepScalarItinV62_list, ScalarItin_list,
                DepHVXItinV62_list, HVXItin_list, PseudoItin_list);
}

def HexagonItinerariesV62 :
      ProcessorItineraries<[SLOT0, SLOT1, SLOT2, SLOT3, SLOT_ENDLOOP,
                            CVI_ST, CVI_XLANE, CVI_SHIFT, CVI_MPY0, CVI_MPY1,
                            CVI_LD, CVI_XLSHF, CVI_MPY01, CVI_ALL,
                            CVI_ALL_NOMEM, CVI_ZW],
                           [Hex_FWD, HVX_FWD], HexagonV62ItinList.ItinList>;

def HexagonModelV62 : SchedMachineModel {
  // Max issue per cycle == bundle width.
  let IssueWidth = 4;
  let Itineraries = HexagonItinerariesV62;
  let LoadLatency = 1;
  let CompleteModel = 0;
}

//===----------------------------------------------------------------------===//
// Hexagon V62 Resource Definitions -
//===----------------------------------------------------------------------===//
