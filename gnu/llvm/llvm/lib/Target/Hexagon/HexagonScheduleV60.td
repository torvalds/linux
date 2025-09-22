//=-HexagonScheduleV60.td - HexagonV60 Scheduling Definitions *- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


// There are four SLOTS (four parallel pipelines) in Hexagon V60 machine.
// This file describes that machine information.
//
//    |===========|==================================================|
//    | PIPELINE  |              Instruction Classes                 |
//    |===========|==================================================|
//    | SLOT0     |  LD       ST    ALU32     MEMOP     NV    SYSTEM |
//    |-----------|--------------------------------------------------|
//    | SLOT1     |  LD       ST    ALU32                            |
//    |-----------|--------------------------------------------------|
//    | SLOT2     |  XTYPE          ALU32     J         JR           |
//    |-----------|--------------------------------------------------|
//    | SLOT3     |  XTYPE          ALU32     J         CR           |
//    |===========|==================================================|
//
//
// In addition to using the above SLOTS, there are also six vector pipelines
// in the CVI co-processor in the Hexagon V60 machine.
//
//      |=========| |=========| |=========| |=========| |=========| |=========|
// SLOT | CVI_LD  | |CVI_MPY3 | |CVI_MPY2 | |CVI_SHIFT| |CVI_XLANE| | CVI_ST  |
// ==== |=========| |=========| |=========| |=========| |=========| |=========|
// S0-3 |         | | CVI_VA  | | CVI_VA  | | CVI_VA  | | CVI_VA  | |         |
// S2-3 |         | | CVI_VX  | | CVI_VX  | |         | |         | |         |
// S0-3 |         | |         | |         | |         | | CVI_VP  | |         |
// S0-3 |         | |         | |         | | CVI_VS  | |         | |         |
// S0-1 |(CVI_LD) | | CVI_LD  | | CVI_LD  | | CVI_LD  | | CVI_LD  | |         |
// S0-1 |(C*TMP_LD) |         | |         | |         | |         | |         |
// S01  |(C*_LDU) | |         | |         | |         | | C*_LDU  | |         |
// S0   |         | | CVI_ST  | | CVI_ST  | | CVI_ST  | | CVI_ST  | |(CVI_ST) |
// S0   |         | |         | |         | |         | |         | |(C*TMP_ST)
// S01  |         | |         | |         | |         | | VSTU    | |(C*_STU) |
//      |=========| |=========| |=========| |=========| |=========| |=========|
//                  |=====================| |=====================|
//                  | CVI_MPY2 & CVI_MPY3 | |CVI_XLANE & CVI_SHIFT|
//                  |=====================| |=====================|
// S0-3             | CVI_VA_DV           | | CVI_VA_DV           |
// S0-3             |                     | | CVI_VP_DV           |
// S2-3             | CVI_VX_DV           | |                     |
//                  |=====================| |=====================|
//      |=====================================================================|
// S0-3 | CVI_HIST   Histogram                                                |
// S0123| CVI_VA_EXT Extract                                                  |
//      |=====================================================================|

def HexagonV60ItinList : DepScalarItinV60, ScalarItin,
                         DepHVXItinV60,
                         HVXItin, PseudoItin {
  list<InstrItinData> ItinList =
    !listconcat(DepScalarItinV60_list, ScalarItin_list,
                DepHVXItinV60_list, HVXItin_list, PseudoItin_list);
}

def HexagonItinerariesV60 :
      ProcessorItineraries<[SLOT0, SLOT1, SLOT2, SLOT3, SLOT_ENDLOOP,
                            CVI_ST, CVI_XLANE, CVI_SHIFT, CVI_MPY0, CVI_MPY1,
                            CVI_LD, CVI_XLSHF, CVI_MPY01, CVI_ALL,
                            CVI_ALL_NOMEM, CVI_ZW],
                            [Hex_FWD, HVX_FWD], HexagonV60ItinList.ItinList>;

def HexagonModelV60 : SchedMachineModel {
  // Max issue per cycle == bundle width.
  let IssueWidth = 4;
  let Itineraries = HexagonItinerariesV60;
  let LoadLatency = 1;
  let CompleteModel = 0;
}

//===----------------------------------------------------------------------===//
// Hexagon V60 Resource Definitions -
//===----------------------------------------------------------------------===//
