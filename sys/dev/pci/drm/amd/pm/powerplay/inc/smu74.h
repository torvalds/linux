/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */


#ifndef SMU74_H
#define SMU74_H

#pragma pack(push, 1)

#define SMU__DGPU_ONLY

#define SMU__NUM_SCLK_DPM_STATE  8
#define SMU__NUM_MCLK_DPM_LEVELS 4
#define SMU__NUM_LCLK_DPM_LEVELS 8
#define SMU__NUM_PCIE_DPM_LEVELS 8

#define EXP_M1  35
#define EXP_M2  92821
#define EXP_B   66629747

#define EXP_M1_1  365
#define EXP_M2_1  658700
#define EXP_B_1   305506134

#define EXP_M1_2  189
#define EXP_M2_2  379692
#define EXP_B_2   194609469

#define EXP_M1_3  99
#define EXP_M2_3  217915
#define EXP_B_3   122255994

#define EXP_M1_4  51
#define EXP_M2_4  122643
#define EXP_B_4   74893384

#define EXP_M1_5  423
#define EXP_M2_5  1103326
#define EXP_B_5   728122621

enum SID_OPTION {
	SID_OPTION_HI,
	SID_OPTION_LO,
	SID_OPTION_COUNT
};

enum Poly3rdOrderCoeff {
	LEAKAGE_TEMPERATURE_SCALAR,
	LEAKAGE_VOLTAGE_SCALAR,
	DYNAMIC_VOLTAGE_SCALAR,
	POLY_3RD_ORDER_COUNT
};

struct SMU7_Poly3rdOrder_Data {
	int32_t a;
	int32_t b;
	int32_t c;
	int32_t d;
	uint8_t a_shift;
	uint8_t b_shift;
	uint8_t c_shift;
	uint8_t x_shift;
};

typedef struct SMU7_Poly3rdOrder_Data SMU7_Poly3rdOrder_Data;

struct Power_Calculator_Data {
	uint16_t NoLoadVoltage;
	uint16_t LoadVoltage;
	uint16_t Resistance;
	uint16_t Temperature;
	uint16_t BaseLeakage;
	uint16_t LkgTempScalar;
	uint16_t LkgVoltScalar;
	uint16_t LkgAreaScalar;
	uint16_t LkgPower;
	uint16_t DynVoltScalar;
	uint32_t Cac;
	uint32_t DynPower;
	uint32_t TotalCurrent;
	uint32_t TotalPower;
};

typedef struct Power_Calculator_Data PowerCalculatorData_t;

struct Gc_Cac_Weight_Data {
	uint8_t index;
	uint32_t value;
};

typedef struct Gc_Cac_Weight_Data GcCacWeight_Data;


typedef struct {
	uint32_t high;
	uint32_t low;
} data_64_t;

typedef struct {
	data_64_t high;
	data_64_t low;
} data_128_t;

#define SMU7_CONTEXT_ID_SMC        1
#define SMU7_CONTEXT_ID_VBIOS      2

#define SMU74_MAX_LEVELS_VDDC            16
#define SMU74_MAX_LEVELS_VDDGFX          16
#define SMU74_MAX_LEVELS_VDDCI           8
#define SMU74_MAX_LEVELS_MVDD            4

#define SMU_MAX_SMIO_LEVELS              4

#define SMU74_MAX_LEVELS_GRAPHICS        SMU__NUM_SCLK_DPM_STATE   /* SCLK + SQ DPM + ULV */
#define SMU74_MAX_LEVELS_MEMORY          SMU__NUM_MCLK_DPM_LEVELS   /* MCLK Levels DPM */
#define SMU74_MAX_LEVELS_GIO             SMU__NUM_LCLK_DPM_LEVELS  /* LCLK Levels */
#define SMU74_MAX_LEVELS_LINK            SMU__NUM_PCIE_DPM_LEVELS  /* PCIe speed and number of lanes */
#define SMU74_MAX_LEVELS_UVD             8   /* VCLK/DCLK levels for UVD */
#define SMU74_MAX_LEVELS_VCE             8   /* ECLK levels for VCE */
#define SMU74_MAX_LEVELS_ACP             8   /* ACLK levels for ACP */
#define SMU74_MAX_LEVELS_SAMU            8   /* SAMCLK levels for SAMU */
#define SMU74_MAX_ENTRIES_SMIO           32  /* Number of entries in SMIO table */

#define DPM_NO_LIMIT 0
#define DPM_NO_UP 1
#define DPM_GO_DOWN 2
#define DPM_GO_UP 3

#define SMU7_FIRST_DPM_GRAPHICS_LEVEL    0
#define SMU7_FIRST_DPM_MEMORY_LEVEL      0

#define GPIO_CLAMP_MODE_VRHOT      1
#define GPIO_CLAMP_MODE_THERM      2
#define GPIO_CLAMP_MODE_DC         4

#define SCRATCH_B_TARG_PCIE_INDEX_SHIFT 0
#define SCRATCH_B_TARG_PCIE_INDEX_MASK  (0x7<<SCRATCH_B_TARG_PCIE_INDEX_SHIFT)
#define SCRATCH_B_CURR_PCIE_INDEX_SHIFT 3
#define SCRATCH_B_CURR_PCIE_INDEX_MASK  (0x7<<SCRATCH_B_CURR_PCIE_INDEX_SHIFT)
#define SCRATCH_B_TARG_UVD_INDEX_SHIFT  6
#define SCRATCH_B_TARG_UVD_INDEX_MASK   (0x7<<SCRATCH_B_TARG_UVD_INDEX_SHIFT)
#define SCRATCH_B_CURR_UVD_INDEX_SHIFT  9
#define SCRATCH_B_CURR_UVD_INDEX_MASK   (0x7<<SCRATCH_B_CURR_UVD_INDEX_SHIFT)
#define SCRATCH_B_TARG_VCE_INDEX_SHIFT  12
#define SCRATCH_B_TARG_VCE_INDEX_MASK   (0x7<<SCRATCH_B_TARG_VCE_INDEX_SHIFT)
#define SCRATCH_B_CURR_VCE_INDEX_SHIFT  15
#define SCRATCH_B_CURR_VCE_INDEX_MASK   (0x7<<SCRATCH_B_CURR_VCE_INDEX_SHIFT)
#define SCRATCH_B_TARG_ACP_INDEX_SHIFT  18
#define SCRATCH_B_TARG_ACP_INDEX_MASK   (0x7<<SCRATCH_B_TARG_ACP_INDEX_SHIFT)
#define SCRATCH_B_CURR_ACP_INDEX_SHIFT  21
#define SCRATCH_B_CURR_ACP_INDEX_MASK   (0x7<<SCRATCH_B_CURR_ACP_INDEX_SHIFT)
#define SCRATCH_B_TARG_SAMU_INDEX_SHIFT 24
#define SCRATCH_B_TARG_SAMU_INDEX_MASK  (0x7<<SCRATCH_B_TARG_SAMU_INDEX_SHIFT)
#define SCRATCH_B_CURR_SAMU_INDEX_SHIFT 27
#define SCRATCH_B_CURR_SAMU_INDEX_MASK  (0x7<<SCRATCH_B_CURR_SAMU_INDEX_SHIFT)

/* Virtualization Defines */
#define CG_XDMA_MASK  0x1
#define CG_XDMA_SHIFT 0
#define CG_UVD_MASK   0x2
#define CG_UVD_SHIFT  1
#define CG_VCE_MASK   0x4
#define CG_VCE_SHIFT  2
#define CG_SAMU_MASK  0x8
#define CG_SAMU_SHIFT 3
#define CG_GFX_MASK   0x10
#define CG_GFX_SHIFT  4
#define CG_SDMA_MASK  0x20
#define CG_SDMA_SHIFT 5
#define CG_HDP_MASK   0x40
#define CG_HDP_SHIFT  6
#define CG_MC_MASK    0x80
#define CG_MC_SHIFT   7
#define CG_DRM_MASK   0x100
#define CG_DRM_SHIFT  8
#define CG_ROM_MASK   0x200
#define CG_ROM_SHIFT  9
#define CG_BIF_MASK   0x400
#define CG_BIF_SHIFT  10


#define SMU74_DTE_ITERATIONS 5
#define SMU74_DTE_SOURCES 3
#define SMU74_DTE_SINKS 1
#define SMU74_NUM_CPU_TES 0
#define SMU74_NUM_GPU_TES 1
#define SMU74_NUM_NON_TES 2
#define SMU74_DTE_FAN_SCALAR_MIN 0x100
#define SMU74_DTE_FAN_SCALAR_MAX 0x166
#define SMU74_DTE_FAN_TEMP_MAX 93
#define SMU74_DTE_FAN_TEMP_MIN 83


#if defined SMU__FUSION_ONLY
#define SMU7_DTE_ITERATIONS 5
#define SMU7_DTE_SOURCES 5
#define SMU7_DTE_SINKS 3
#define SMU7_NUM_CPU_TES 2
#define SMU7_NUM_GPU_TES 1
#define SMU7_NUM_NON_TES 2
#endif

struct SMU7_HystController_Data {
	uint8_t waterfall_up;
	uint8_t waterfall_down;
	uint8_t waterfall_limit;
	uint8_t spare;
	uint16_t release_cnt;
	uint16_t release_limit;
};

typedef struct SMU7_HystController_Data SMU7_HystController_Data;

struct SMU74_PIDController {
	uint32_t Ki;
	int32_t LFWindupUpperLim;
	int32_t LFWindupLowerLim;
	uint32_t StatePrecision;
	uint32_t LfPrecision;
	uint32_t LfOffset;
	uint32_t MaxState;
	uint32_t MaxLfFraction;
	uint32_t StateShift;
};

typedef struct SMU74_PIDController SMU74_PIDController;

struct SMU7_LocalDpmScoreboard {
	uint32_t PercentageBusy;

	int32_t  PIDError;
	int32_t  PIDIntegral;
	int32_t  PIDOutput;

	uint32_t SigmaDeltaAccum;
	uint32_t SigmaDeltaOutput;
	uint32_t SigmaDeltaLevel;

	uint32_t UtilizationSetpoint;

	uint8_t  TdpClampMode;
	uint8_t  TdcClampMode;
	uint8_t  ThermClampMode;
	uint8_t  VoltageBusy;

	int8_t   CurrLevel;
	int8_t   TargLevel;
	uint8_t  LevelChangeInProgress;
	uint8_t  UpHyst;

	uint8_t  DownHyst;
	uint8_t  VoltageDownHyst;
	uint8_t  DpmEnable;
	uint8_t  DpmRunning;

	uint8_t  DpmForce;
	uint8_t  DpmForceLevel;
	uint8_t  DisplayWatermark;
	uint8_t  McArbIndex;

	uint32_t MinimumPerfSclk;

	uint8_t  AcpiReq;
	uint8_t  AcpiAck;
	uint8_t  GfxClkSlow;
	uint8_t  GpioClampMode;

	uint8_t  spare2;
	uint8_t  EnabledLevelsChange;
	uint8_t  DteClampMode;
	uint8_t  FpsClampMode;

	uint16_t LevelResidencyCounters[SMU74_MAX_LEVELS_GRAPHICS];
	uint16_t LevelSwitchCounters[SMU74_MAX_LEVELS_GRAPHICS];

	void     (*TargetStateCalculator)(uint8_t);
	void     (*SavedTargetStateCalculator)(uint8_t);

	uint16_t AutoDpmInterval;
	uint16_t AutoDpmRange;

	uint8_t  FpsEnabled;
	uint8_t  MaxPerfLevel;
	uint8_t  AllowLowClkInterruptToHost;
	uint8_t  FpsRunning;

	uint32_t MaxAllowedFrequency;

	uint32_t FilteredSclkFrequency;
	uint32_t LastSclkFrequency;
	uint32_t FilteredSclkFrequencyCnt;

	uint8_t MinPerfLevel;
	uint8_t padding[3];

	uint16_t FpsAlpha;
	uint16_t DeltaTime;
	uint32_t CurrentFps;
	uint32_t FilteredFps;
	uint32_t FrameCount;
	uint32_t FrameCountLast;
	uint16_t FpsTargetScalar;
	uint16_t FpsWaterfallLimitScalar;
	uint16_t FpsAlphaScalar;
	uint16_t spare8;
	SMU7_HystController_Data HystControllerData;
};

typedef struct SMU7_LocalDpmScoreboard SMU7_LocalDpmScoreboard;

#define SMU7_MAX_VOLTAGE_CLIENTS 12

typedef uint8_t (*VoltageChangeHandler_t)(uint16_t, uint8_t);

#define VDDC_MASK    0x00007FFF
#define VDDC_SHIFT   0
#define VDDCI_MASK   0x3FFF8000
#define VDDCI_SHIFT  15
#define PHASES_MASK  0xC0000000
#define PHASES_SHIFT 30

typedef uint32_t SMU_VoltageLevel;

struct SMU7_VoltageScoreboard {

	SMU_VoltageLevel TargetVoltage;
	uint16_t MaxVid;
	uint8_t  HighestVidOffset;
	uint8_t  CurrentVidOffset;

	uint16_t CurrentVddc;
	uint16_t CurrentVddci;


	uint8_t  ControllerBusy;
	uint8_t  CurrentVid;
	uint8_t  CurrentVddciVid;
	uint8_t  padding;

	SMU_VoltageLevel RequestedVoltage[SMU7_MAX_VOLTAGE_CLIENTS];
	SMU_VoltageLevel TargetVoltageState;
	uint8_t  EnabledRequest[SMU7_MAX_VOLTAGE_CLIENTS];

	uint8_t  padding2;
	uint8_t  padding3;
	uint8_t  ControllerEnable;
	uint8_t  ControllerRunning;
	uint16_t CurrentStdVoltageHiSidd;
	uint16_t CurrentStdVoltageLoSidd;
	uint8_t  OverrideVoltage;
	uint8_t  padding4;
	uint8_t  padding5;
	uint8_t  CurrentPhases;

	VoltageChangeHandler_t ChangeVddc;

	VoltageChangeHandler_t ChangeVddci;
	VoltageChangeHandler_t ChangePhase;
	VoltageChangeHandler_t ChangeMvdd;

	VoltageChangeHandler_t functionLinks[6];

	uint16_t *VddcFollower1;

	int16_t  Driver_OD_RequestedVidOffset1;
	int16_t  Driver_OD_RequestedVidOffset2;
};

typedef struct SMU7_VoltageScoreboard SMU7_VoltageScoreboard;

#define SMU7_MAX_PCIE_LINK_SPEEDS 3 /* 0:Gen1 1:Gen2 2:Gen3 */

struct SMU7_PCIeLinkSpeedScoreboard {
	uint8_t     DpmEnable;
	uint8_t     DpmRunning;
	uint8_t     DpmForce;
	uint8_t     DpmForceLevel;

	uint8_t     CurrentLinkSpeed;
	uint8_t     EnabledLevelsChange;
	uint16_t    AutoDpmInterval;

	uint16_t    AutoDpmRange;
	uint16_t    AutoDpmCount;

	uint8_t     DpmMode;
	uint8_t     AcpiReq;
	uint8_t     AcpiAck;
	uint8_t     CurrentLinkLevel;

};

typedef struct SMU7_PCIeLinkSpeedScoreboard SMU7_PCIeLinkSpeedScoreboard;

#define SMU7_LKGE_LUT_NUM_OF_TEMP_ENTRIES 16
#define SMU7_LKGE_LUT_NUM_OF_VOLT_ENTRIES 16

#define SMU7_SCALE_I  7
#define SMU7_SCALE_R 12

struct SMU7_PowerScoreboard {
	PowerCalculatorData_t VddcPowerData[SID_OPTION_COUNT];

	uint32_t TotalGpuPower;
	uint32_t TdcCurrent;

	uint16_t   VddciTotalPower;
	uint16_t   sparesasfsdfd;
	uint16_t   Vddr1Power;
	uint16_t   RocPower;

	uint16_t   CalcMeasPowerBlend;
	uint8_t    SidOptionPower;
	uint8_t    SidOptionCurrent;

	uint32_t   WinTime;

	uint16_t Telemetry_1_slope;
	uint16_t Telemetry_2_slope;
	int32_t Telemetry_1_offset;
	int32_t Telemetry_2_offset;

	uint32_t VddcCurrentTelemetry;
	uint32_t VddGfxCurrentTelemetry;
	uint32_t VddcPowerTelemetry;
	uint32_t VddGfxPowerTelemetry;
	uint32_t VddciPowerTelemetry;

	uint32_t VddcPower;
	uint32_t VddGfxPower;
	uint32_t VddciPower;

	uint32_t TelemetryCurrent[2];
	uint32_t TelemetryVoltage[2];
	uint32_t TelemetryPower[2];
};

typedef struct SMU7_PowerScoreboard SMU7_PowerScoreboard;

struct SMU7_ThermalScoreboard {
	int16_t  GpuLimit;
	int16_t  GpuHyst;
	uint16_t CurrGnbTemp;
	uint16_t FilteredGnbTemp;

	uint8_t  ControllerEnable;
	uint8_t  ControllerRunning;
	uint8_t  AutoTmonCalInterval;
	uint8_t  AutoTmonCalEnable;

	uint8_t  ThermalDpmEnabled;
	uint8_t  SclkEnabledMask;
	uint8_t  spare[2];
	int32_t  temperature_gradient;

	SMU7_HystController_Data HystControllerData;
	int32_t  WeightedSensorTemperature;
	uint16_t TemperatureLimit[SMU74_MAX_LEVELS_GRAPHICS];
	uint32_t Alpha;
};

typedef struct SMU7_ThermalScoreboard SMU7_ThermalScoreboard;

#define SMU7_SCLK_DPM_CONFIG_MASK                        0x01
#define SMU7_VOLTAGE_CONTROLLER_CONFIG_MASK              0x02
#define SMU7_THERMAL_CONTROLLER_CONFIG_MASK              0x04
#define SMU7_MCLK_DPM_CONFIG_MASK                        0x08
#define SMU7_UVD_DPM_CONFIG_MASK                         0x10
#define SMU7_VCE_DPM_CONFIG_MASK                         0x20
#define SMU7_ACP_DPM_CONFIG_MASK                         0x40
#define SMU7_SAMU_DPM_CONFIG_MASK                        0x80
#define SMU7_PCIEGEN_DPM_CONFIG_MASK                    0x100

#define SMU7_ACP_MCLK_HANDSHAKE_DISABLE                  0x00000001
#define SMU7_ACP_SCLK_HANDSHAKE_DISABLE                  0x00000002
#define SMU7_UVD_MCLK_HANDSHAKE_DISABLE                  0x00000100
#define SMU7_UVD_SCLK_HANDSHAKE_DISABLE                  0x00000200
#define SMU7_VCE_MCLK_HANDSHAKE_DISABLE                  0x00010000
#define SMU7_VCE_SCLK_HANDSHAKE_DISABLE                  0x00020000

/* All 'soft registers' should be uint32_t. */
struct SMU74_SoftRegisters {
	uint32_t        RefClockFrequency;
	uint32_t        PmTimerPeriod;
	uint32_t        FeatureEnables;

	uint32_t        PreVBlankGap;
	uint32_t        VBlankTimeout;
	uint32_t        TrainTimeGap;

	uint32_t        MvddSwitchTime;
	uint32_t        LongestAcpiTrainTime;
	uint32_t        AcpiDelay;
	uint32_t        G5TrainTime;
	uint32_t        DelayMpllPwron;
	uint32_t        VoltageChangeTimeout;

	uint32_t        HandshakeDisables;

	uint8_t         DisplayPhy1Config;
	uint8_t         DisplayPhy2Config;
	uint8_t         DisplayPhy3Config;
	uint8_t         DisplayPhy4Config;

	uint8_t         DisplayPhy5Config;
	uint8_t         DisplayPhy6Config;
	uint8_t         DisplayPhy7Config;
	uint8_t         DisplayPhy8Config;

	uint32_t        AverageGraphicsActivity;
	uint32_t        AverageMemoryActivity;
	uint32_t        AverageGioActivity;

	uint8_t         SClkDpmEnabledLevels;
	uint8_t         MClkDpmEnabledLevels;
	uint8_t         LClkDpmEnabledLevels;
	uint8_t         PCIeDpmEnabledLevels;

	uint8_t         UVDDpmEnabledLevels;
	uint8_t         SAMUDpmEnabledLevels;
	uint8_t         ACPDpmEnabledLevels;
	uint8_t         VCEDpmEnabledLevels;

	uint32_t        DRAM_LOG_ADDR_H;
	uint32_t        DRAM_LOG_ADDR_L;
	uint32_t        DRAM_LOG_PHY_ADDR_H;
	uint32_t        DRAM_LOG_PHY_ADDR_L;
	uint32_t        DRAM_LOG_BUFF_SIZE;
	uint32_t        UlvEnterCount;
	uint32_t        UlvTime;
	uint32_t        UcodeLoadStatus;
	uint32_t        AllowMvddSwitch;
	uint8_t         Activity_Weight;
	uint8_t         Reserved8[3];
};

typedef struct SMU74_SoftRegisters SMU74_SoftRegisters;

struct SMU74_Firmware_Header {
	uint32_t Digest[5];
	uint32_t Version;
	uint32_t HeaderSize;
	uint32_t Flags;
	uint32_t EntryPoint;
	uint32_t CodeSize;
	uint32_t ImageSize;

	uint32_t Rtos;
	uint32_t SoftRegisters;
	uint32_t DpmTable;
	uint32_t FanTable;
	uint32_t CacConfigTable;
	uint32_t CacStatusTable;

	uint32_t mcRegisterTable;

	uint32_t mcArbDramTimingTable;

	uint32_t PmFuseTable;
	uint32_t Globals;
	uint32_t ClockStretcherTable;
	uint32_t VftTable;
	uint32_t Reserved1;
	uint32_t AvfsTable;
	uint32_t AvfsCksOffGbvTable;
	uint32_t AvfsMeanNSigma;
	uint32_t AvfsSclkOffsetTable;
	uint32_t Reserved[16];
	uint32_t Signature;
};

typedef struct SMU74_Firmware_Header SMU74_Firmware_Header;

#define SMU7_FIRMWARE_HEADER_LOCATION 0x20000

enum  DisplayConfig {
	PowerDown = 1,
	DP54x4,
	DP54x2,
	DP54x1,
	DP27x4,
	DP27x2,
	DP27x1,
	HDMI297,
	HDMI162,
	LVDS,
	DP324x4,
	DP324x2,
	DP324x1
};


#define MC_BLOCK_COUNT 1
#define CPL_BLOCK_COUNT 5
#define SE_BLOCK_COUNT 15
#define GC_BLOCK_COUNT 24

struct SMU7_Local_Cac {
	uint8_t BlockId;
	uint8_t SignalId;
	uint8_t Threshold;
	uint8_t Padding;
};

typedef struct SMU7_Local_Cac SMU7_Local_Cac;

struct SMU7_Local_Cac_Table {

	SMU7_Local_Cac CplLocalCac[CPL_BLOCK_COUNT];
	SMU7_Local_Cac McLocalCac[MC_BLOCK_COUNT];
	SMU7_Local_Cac SeLocalCac[SE_BLOCK_COUNT];
	SMU7_Local_Cac GcLocalCac[GC_BLOCK_COUNT];
};

typedef struct SMU7_Local_Cac_Table SMU7_Local_Cac_Table;

#pragma pack(pop)

/* Description of Clock Gating bitmask for Tonga:
 * System Clock Gating
 */
#define CG_SYS_BITMASK_FIRST_BIT      0  /* First bit of Sys CG bitmask */
#define CG_SYS_BITMASK_LAST_BIT       9  /* Last bit of Sys CG bitmask */
#define CG_SYS_BIF_MGLS_SHIFT         0
#define CG_SYS_ROM_SHIFT              1
#define CG_SYS_MC_MGCG_SHIFT          2
#define CG_SYS_MC_MGLS_SHIFT          3
#define CG_SYS_SDMA_MGCG_SHIFT        4
#define CG_SYS_SDMA_MGLS_SHIFT        5
#define CG_SYS_DRM_MGCG_SHIFT         6
#define CG_SYS_HDP_MGCG_SHIFT         7
#define CG_SYS_HDP_MGLS_SHIFT         8
#define CG_SYS_DRM_MGLS_SHIFT         9
#define CG_SYS_BIF_MGCG_SHIFT         10

#define CG_SYS_BIF_MGLS_MASK          0x1
#define CG_SYS_ROM_MASK               0x2
#define CG_SYS_MC_MGCG_MASK           0x4
#define CG_SYS_MC_MGLS_MASK           0x8
#define CG_SYS_SDMA_MGCG_MASK         0x10
#define CG_SYS_SDMA_MGLS_MASK         0x20
#define CG_SYS_DRM_MGCG_MASK          0x40
#define CG_SYS_HDP_MGCG_MASK          0x80
#define CG_SYS_HDP_MGLS_MASK          0x100
#define CG_SYS_DRM_MGLS_MASK          0x200
#define CG_SYS_BIF_MGCG_MASK          0x400

/* Graphics Clock Gating */
#define CG_GFX_BITMASK_FIRST_BIT      16 /* First bit of Gfx CG bitmask */
#define CG_GFX_BITMASK_LAST_BIT       24 /* Last bit of Gfx CG bitmask */

#define CG_GFX_CGCG_SHIFT             16
#define CG_GFX_CGLS_SHIFT             17
#define CG_CPF_MGCG_SHIFT             18
#define CG_RLC_MGCG_SHIFT             19
#define CG_GFX_OTHERS_MGCG_SHIFT      20
#define CG_GFX_3DCG_SHIFT             21
#define CG_GFX_3DLS_SHIFT             22
#define CG_GFX_RLC_LS_SHIFT           23
#define CG_GFX_CP_LS_SHIFT            24

#define CG_GFX_CGCG_MASK              0x00010000
#define CG_GFX_CGLS_MASK              0x00020000
#define CG_CPF_MGCG_MASK              0x00040000
#define CG_RLC_MGCG_MASK              0x00080000
#define CG_GFX_OTHERS_MGCG_MASK       0x00100000
#define CG_GFX_3DCG_MASK              0x00200000
#define CG_GFX_3DLS_MASK              0x00400000
#define CG_GFX_RLC_LS_MASK            0x00800000
#define CG_GFX_CP_LS_MASK             0x01000000


/* Voltage Regulator Configuration
VR Config info is contained in dpmTable.VRConfig */

#define VRCONF_VDDC_MASK         0x000000FF
#define VRCONF_VDDC_SHIFT        0
#define VRCONF_VDDGFX_MASK       0x0000FF00
#define VRCONF_VDDGFX_SHIFT      8
#define VRCONF_VDDCI_MASK        0x00FF0000
#define VRCONF_VDDCI_SHIFT       16
#define VRCONF_MVDD_MASK         0xFF000000
#define VRCONF_MVDD_SHIFT        24

#define VR_MERGED_WITH_VDDC      0
#define VR_SVI2_PLANE_1          1
#define VR_SVI2_PLANE_2          2
#define VR_SMIO_PATTERN_1        3
#define VR_SMIO_PATTERN_2        4
#define VR_STATIC_VOLTAGE        5

/* Clock Stretcher Configuration */

#define CLOCK_STRETCHER_MAX_ENTRIES 0x4
#define CKS_LOOKUPTable_MAX_ENTRIES 0x4

/* The 'settings' field is subdivided in the following way: */
#define CLOCK_STRETCHER_SETTING_DDT_MASK             0x01
#define CLOCK_STRETCHER_SETTING_DDT_SHIFT            0x0
#define CLOCK_STRETCHER_SETTING_STRETCH_AMOUNT_MASK  0x1E
#define CLOCK_STRETCHER_SETTING_STRETCH_AMOUNT_SHIFT 0x1
#define CLOCK_STRETCHER_SETTING_ENABLE_MASK          0x80
#define CLOCK_STRETCHER_SETTING_ENABLE_SHIFT         0x7

struct SMU_ClockStretcherDataTableEntry {
	uint8_t minVID;
	uint8_t maxVID;
	uint16_t setting;
};
typedef struct SMU_ClockStretcherDataTableEntry SMU_ClockStretcherDataTableEntry;

struct SMU_ClockStretcherDataTable {
	SMU_ClockStretcherDataTableEntry ClockStretcherDataTableEntry[CLOCK_STRETCHER_MAX_ENTRIES];
};
typedef struct SMU_ClockStretcherDataTable SMU_ClockStretcherDataTable;

struct SMU_CKS_LOOKUPTableEntry {
	uint16_t minFreq;
	uint16_t maxFreq;

	uint8_t setting;
	uint8_t padding[3];
};
typedef struct SMU_CKS_LOOKUPTableEntry SMU_CKS_LOOKUPTableEntry;

struct SMU_CKS_LOOKUPTable {
	SMU_CKS_LOOKUPTableEntry CKS_LOOKUPTableEntry[CKS_LOOKUPTable_MAX_ENTRIES];
};
typedef struct SMU_CKS_LOOKUPTable SMU_CKS_LOOKUPTable;

struct AgmAvfsData_t {
	uint16_t avgPsmCount[28];
	uint16_t minPsmCount[28];
};

typedef struct AgmAvfsData_t AgmAvfsData_t;

enum VFT_COLUMNS {
	SCLK0,
	SCLK1,
	SCLK2,
	SCLK3,
	SCLK4,
	SCLK5,
	SCLK6,
	SCLK7,

	NUM_VFT_COLUMNS
};

#define VFT_TABLE_DEFINED

#define TEMP_RANGE_MAXSTEPS 12

struct VFT_CELL_t {
	uint16_t Voltage;
};

typedef struct VFT_CELL_t VFT_CELL_t;

struct VFT_TABLE_t {
	VFT_CELL_t    Cell[TEMP_RANGE_MAXSTEPS][NUM_VFT_COLUMNS];
	uint16_t      AvfsGbv[NUM_VFT_COLUMNS];
	uint16_t      BtcGbv[NUM_VFT_COLUMNS];
	uint16_t      Temperature[TEMP_RANGE_MAXSTEPS];

	uint8_t       NumTemperatureSteps;
	uint8_t       padding[3];
};

typedef struct VFT_TABLE_t VFT_TABLE_t;


/* Total margin, root mean square of Fmax + DC + Platform */
struct AVFS_Margin_t {
	VFT_CELL_t Cell[NUM_VFT_COLUMNS];
};
typedef struct AVFS_Margin_t AVFS_Margin_t;

#define BTCGB_VDROOP_TABLE_MAX_ENTRIES 2
#define AVFSGB_VDROOP_TABLE_MAX_ENTRIES 2

struct GB_VDROOP_TABLE_t {
	int32_t a0;
	int32_t a1;
	int32_t a2;
	uint32_t spare;
};
typedef struct GB_VDROOP_TABLE_t GB_VDROOP_TABLE_t;

struct AVFS_CksOff_Gbv_t {
	VFT_CELL_t Cell[NUM_VFT_COLUMNS];
};
typedef struct AVFS_CksOff_Gbv_t AVFS_CksOff_Gbv_t;

struct AVFS_meanNsigma_t {
	uint32_t Aconstant[3];
	uint16_t DC_tol_sigma;
	uint16_t Platform_mean;
	uint16_t Platform_sigma;
	uint16_t PSM_Age_CompFactor;
	uint8_t  Static_Voltage_Offset[NUM_VFT_COLUMNS];
};
typedef struct AVFS_meanNsigma_t AVFS_meanNsigma_t;

struct AVFS_Sclk_Offset_t {
	uint16_t Sclk_Offset[8];
};
typedef struct AVFS_Sclk_Offset_t AVFS_Sclk_Offset_t;

#endif


