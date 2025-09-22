//===- HexagonSchedule.td - Hexagon Scheduling Definitions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def Hex_FWD : Bypass;
def HVX_FWD : Bypass;

// Functional Units.
def SLOT0       : FuncUnit;
def SLOT1       : FuncUnit;
def SLOT2       : FuncUnit;
def SLOT3       : FuncUnit;
// Endloop is a pseudo instruction that is encoded with 2 bits in a packet
// rather than taking an execution slot. This special unit is needed
// to schedule an ENDLOOP with 4 other instructions.
def SLOT_ENDLOOP: FuncUnit;

// CVI pipes from the "Hexagon Multimedia Co-Processor Extensions Arch Spec".
def CVI_ST     : FuncUnit;
def CVI_XLANE  : FuncUnit;
def CVI_SHIFT  : FuncUnit;
def CVI_MPY0   : FuncUnit;
def CVI_MPY1   : FuncUnit;
def CVI_LD     : FuncUnit;
def CVI_ZW     : FuncUnit; // Z register write port

// Combined functional units.
def CVI_XLSHF  : FuncUnit;
def CVI_MPY01  : FuncUnit;
def CVI_ALL    : FuncUnit;
def CVI_ALL_NOMEM : FuncUnit;

// Combined functional unit data.
def HexagonComboFuncsV60 :
    ComboFuncUnits<[
      ComboFuncData<CVI_XLSHF    , [CVI_XLANE, CVI_SHIFT]>,
      ComboFuncData<CVI_MPY01    , [CVI_MPY0, CVI_MPY1]>,
      ComboFuncData<CVI_ALL      , [CVI_ST, CVI_XLANE, CVI_SHIFT,
                                    CVI_MPY0, CVI_MPY1, CVI_LD]>,
      ComboFuncData<CVI_ALL_NOMEM, [CVI_XLANE, CVI_SHIFT, CVI_MPY0, CVI_MPY1]>
    ]>;

// Itinerary classes.
def PSEUDO          : InstrItinClass;
def PSEUDOM         : InstrItinClass;
def DUPLEX          : InstrItinClass;
def tc_ENDLOOP      : InstrItinClass;

//===----------------------------------------------------------------------===//
// Auto-generated itinerary classes
//===----------------------------------------------------------------------===//
include "HexagonDepIICScalar.td"
include "HexagonDepIICHVX.td"

include "HexagonScheduleV5.td"
include "HexagonScheduleV55.td"

include "HexagonIICScalar.td"
include "HexagonIICHVX.td"
include "HexagonScheduleV60.td"

include "HexagonScheduleV62.td"
include "HexagonScheduleV65.td"
include "HexagonScheduleV66.td"
include "HexagonScheduleV67.td"
include "HexagonScheduleV67T.td"
include "HexagonScheduleV68.td"
include "HexagonScheduleV69.td"
include "HexagonScheduleV71.td"
include "HexagonScheduleV71T.td"
include "HexagonScheduleV73.td"
