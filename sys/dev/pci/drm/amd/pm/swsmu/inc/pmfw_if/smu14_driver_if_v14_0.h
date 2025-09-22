/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef SMU14_DRIVER_IF_V14_0_H
#define SMU14_DRIVER_IF_V14_0_H

//Increment this version if SkuTable_t or BoardTable_t change
#define PPTABLE_VERSION 0x1B

#define NUM_GFXCLK_DPM_LEVELS    16
#define NUM_SOCCLK_DPM_LEVELS    8
#define NUM_MP0CLK_DPM_LEVELS    2
#define NUM_DCLK_DPM_LEVELS      8
#define NUM_VCLK_DPM_LEVELS      8
#define NUM_DISPCLK_DPM_LEVELS   8
#define NUM_DPPCLK_DPM_LEVELS    8
#define NUM_DPREFCLK_DPM_LEVELS  8
#define NUM_DCFCLK_DPM_LEVELS    8
#define NUM_DTBCLK_DPM_LEVELS    8
#define NUM_UCLK_DPM_LEVELS      6
#define NUM_LINK_LEVELS          3
#define NUM_FCLK_DPM_LEVELS      8
#define NUM_OD_FAN_MAX_POINTS    6

// Feature Control Defines
#define FEATURE_FW_DATA_READ_BIT              0
#define FEATURE_DPM_GFXCLK_BIT                1
#define FEATURE_DPM_GFX_POWER_OPTIMIZER_BIT   2
#define FEATURE_DPM_UCLK_BIT                  3
#define FEATURE_DPM_FCLK_BIT                  4
#define FEATURE_DPM_SOCCLK_BIT                5
#define FEATURE_DPM_LINK_BIT                  6
#define FEATURE_DPM_DCN_BIT                   7
#define FEATURE_VMEMP_SCALING_BIT             8
#define FEATURE_VDDIO_MEM_SCALING_BIT         9
#define FEATURE_DS_GFXCLK_BIT                 10
#define FEATURE_DS_SOCCLK_BIT                 11
#define FEATURE_DS_FCLK_BIT                   12
#define FEATURE_DS_LCLK_BIT                   13
#define FEATURE_DS_DCFCLK_BIT                 14
#define FEATURE_DS_UCLK_BIT                   15
#define FEATURE_GFX_ULV_BIT                   16
#define FEATURE_FW_DSTATE_BIT                 17
#define FEATURE_GFXOFF_BIT                    18
#define FEATURE_BACO_BIT                      19
#define FEATURE_MM_DPM_BIT                    20
#define FEATURE_SOC_MPCLK_DS_BIT              21
#define FEATURE_BACO_MPCLK_DS_BIT             22
#define FEATURE_THROTTLERS_BIT                23
#define FEATURE_SMARTSHIFT_BIT                24
#define FEATURE_GTHR_BIT                      25
#define FEATURE_ACDC_BIT                      26
#define FEATURE_VR0HOT_BIT                    27
#define FEATURE_FW_CTF_BIT                    28
#define FEATURE_FAN_CONTROL_BIT               29
#define FEATURE_GFX_DCS_BIT                   30
#define FEATURE_GFX_READ_MARGIN_BIT           31
#define FEATURE_LED_DISPLAY_BIT               32
#define FEATURE_GFXCLK_SPREAD_SPECTRUM_BIT    33
#define FEATURE_OUT_OF_BAND_MONITOR_BIT       34
#define FEATURE_OPTIMIZED_VMIN_BIT            35
#define FEATURE_GFX_IMU_BIT                   36
#define FEATURE_BOOT_TIME_CAL_BIT             37
#define FEATURE_GFX_PCC_DFLL_BIT              38
#define FEATURE_SOC_CG_BIT                    39
#define FEATURE_DF_CSTATE_BIT                 40
#define FEATURE_GFX_EDC_BIT                   41
#define FEATURE_BOOT_POWER_OPT_BIT            42
#define FEATURE_CLOCK_POWER_DOWN_BYPASS_BIT   43
#define FEATURE_DS_VCN_BIT                    44
#define FEATURE_BACO_CG_BIT                   45
#define FEATURE_MEM_TEMP_READ_BIT             46
#define FEATURE_ATHUB_MMHUB_PG_BIT            47
#define FEATURE_SOC_PCC_BIT                   48
#define FEATURE_EDC_PWRBRK_BIT                49
#define FEATURE_SOC_EDC_XVMIN_BIT             50
#define FEATURE_GFX_PSM_DIDT_BIT              51
#define FEATURE_APT_ALL_ENABLE_BIT            52
#define FEATURE_APT_SQ_THROTTLE_BIT           53
#define FEATURE_APT_PF_DCS_BIT                54
#define FEATURE_GFX_EDC_XVMIN_BIT             55
#define FEATURE_GFX_DIDT_XVMIN_BIT            56
#define FEATURE_FAN_ABNORMAL_BIT              57
#define FEATURE_CLOCK_STRETCH_COMPENSATOR     58
#define FEATURE_SPARE_59_BIT                  59
#define FEATURE_SPARE_60_BIT                  60
#define FEATURE_SPARE_61_BIT                  61
#define FEATURE_SPARE_62_BIT                  62
#define FEATURE_SPARE_63_BIT                  63
#define NUM_FEATURES                          64

#define ALLOWED_FEATURE_CTRL_DEFAULT 0xFFFFFFFFFFFFFFFFULL
#define ALLOWED_FEATURE_CTRL_SCPM        (1 << FEATURE_DPM_GFXCLK_BIT) | \
                                         (1 << FEATURE_DPM_GFX_POWER_OPTIMIZER_BIT) | \
                                         (1 << FEATURE_DPM_UCLK_BIT) | \
                                         (1 << FEATURE_DPM_FCLK_BIT) | \
                                         (1 << FEATURE_DPM_SOCCLK_BIT) | \
                                         (1 << FEATURE_DPM_LINK_BIT) | \
                                         (1 << FEATURE_DPM_DCN_BIT) | \
                                         (1 << FEATURE_DS_GFXCLK_BIT) | \
                                         (1 << FEATURE_DS_SOCCLK_BIT) | \
                                         (1 << FEATURE_DS_FCLK_BIT) | \
                                         (1 << FEATURE_DS_LCLK_BIT) | \
                                         (1 << FEATURE_DS_DCFCLK_BIT) | \
                                         (1 << FEATURE_DS_UCLK_BIT) | \
                                         (1ULL << FEATURE_DS_VCN_BIT)


//For use with feature control messages
typedef enum {
  FEATURE_PWR_ALL,
  FEATURE_PWR_S5,
  FEATURE_PWR_BACO,
  FEATURE_PWR_SOC,
  FEATURE_PWR_GFX,
  FEATURE_PWR_DOMAIN_COUNT,
} FEATURE_PWR_DOMAIN_e;

//For use with feature control + BTC save restore
typedef enum {
  FEATURE_BTC_NOP,
  FEATURE_BTC_SAVE,
  FEATURE_BTC_RESTORE,
  FEATURE_BTC_COUNT,
} FEATURE_BTC_e;

// Debug Overrides Bitmask
#define DEBUG_OVERRIDE_NOT_USE      				   0x00000001
#define DEBUG_OVERRIDE_DISABLE_VOLT_LINK_DCN_FCLK      0x00000002
#define DEBUG_OVERRIDE_DISABLE_VOLT_LINK_MP0_FCLK      0x00000004
#define DEBUG_OVERRIDE_DISABLE_VOLT_LINK_VCN_DCFCLK    0x00000008
#define DEBUG_OVERRIDE_DISABLE_FAST_FCLK_TIMER         0x00000010
#define DEBUG_OVERRIDE_DISABLE_VCN_PG                  0x00000020
#define DEBUG_OVERRIDE_DISABLE_FMAX_VMAX               0x00000040
#define DEBUG_OVERRIDE_DISABLE_IMU_FW_CHECKS           0x00000080
#define DEBUG_OVERRIDE_DISABLE_D0i2_REENTRY_HSR_TIMER_CHECK 0x00000100
#define DEBUG_OVERRIDE_DISABLE_DFLL                    0x00000200
#define DEBUG_OVERRIDE_ENABLE_RLC_VF_BRINGUP_MODE      0x00000400
#define DEBUG_OVERRIDE_DFLL_MASTER_MODE                0x00000800
#define DEBUG_OVERRIDE_ENABLE_PROFILING_MODE           0x00001000
#define DEBUG_OVERRIDE_ENABLE_SOC_VF_BRINGUP_MODE      0x00002000
#define DEBUG_OVERRIDE_ENABLE_PER_WGP_RESIENCY         0x00004000
#define DEBUG_OVERRIDE_DISABLE_MEMORY_VOLTAGE_SCALING  0x00008000
#define DEBUG_OVERRIDE_DFLL_BTC_FCW_LOG                0x00010000

// VR Mapping Bit Defines
#define VR_MAPPING_VR_SELECT_MASK  0x01
#define VR_MAPPING_VR_SELECT_SHIFT 0x00

#define VR_MAPPING_PLANE_SELECT_MASK  0x02
#define VR_MAPPING_PLANE_SELECT_SHIFT 0x01

// PSI Bit Defines
#define PSI_SEL_VR0_PLANE0_PSI0  0x01
#define PSI_SEL_VR0_PLANE0_PSI1  0x02
#define PSI_SEL_VR0_PLANE1_PSI0  0x04
#define PSI_SEL_VR0_PLANE1_PSI1  0x08
#define PSI_SEL_VR1_PLANE0_PSI0  0x10
#define PSI_SEL_VR1_PLANE0_PSI1  0x20
#define PSI_SEL_VR1_PLANE1_PSI0  0x40
#define PSI_SEL_VR1_PLANE1_PSI1  0x80

typedef enum {
  SVI_PSI_0, // Full phase count (default)
  SVI_PSI_1, // Phase count 1st level
  SVI_PSI_2, // Phase count 2nd level
  SVI_PSI_3, // Single phase operation + active diode emulation
  SVI_PSI_4, // Single phase operation + passive diode emulation *optional*
  SVI_PSI_5, // Reserved
  SVI_PSI_6, // Power down to 0V (voltage regulation disabled)
  SVI_PSI_7, // Automated phase shedding and diode emulation
} SVI_PSI_e;

// Throttler Control/Status Bits
#define THROTTLER_TEMP_EDGE_BIT        0
#define THROTTLER_TEMP_HOTSPOT_BIT     1
#define THROTTLER_TEMP_HOTSPOT_GFX_BIT 2
#define THROTTLER_TEMP_HOTSPOT_SOC_BIT 3
#define THROTTLER_TEMP_MEM_BIT         4
#define THROTTLER_TEMP_VR_GFX_BIT      5
#define THROTTLER_TEMP_VR_SOC_BIT      6
#define THROTTLER_TEMP_VR_MEM0_BIT     7
#define THROTTLER_TEMP_VR_MEM1_BIT     8
#define THROTTLER_TEMP_LIQUID0_BIT     9
#define THROTTLER_TEMP_LIQUID1_BIT     10
#define THROTTLER_TEMP_PLX_BIT         11
#define THROTTLER_TDC_GFX_BIT          12
#define THROTTLER_TDC_SOC_BIT          13
#define THROTTLER_PPT0_BIT             14
#define THROTTLER_PPT1_BIT             15
#define THROTTLER_PPT2_BIT             16
#define THROTTLER_PPT3_BIT             17
#define THROTTLER_FIT_BIT              18
#define THROTTLER_GFX_APCC_PLUS_BIT    19
#define THROTTLER_GFX_DVO_BIT          20
#define THROTTLER_COUNT                21

// FW DState Features Control Bits
#define FW_DSTATE_SOC_ULV_BIT               0
#define FW_DSTATE_G6_HSR_BIT                1
#define FW_DSTATE_G6_PHY_VMEMP_OFF_BIT      2
#define FW_DSTATE_SMN_DS_BIT                3
#define FW_DSTATE_MP1_WHISPER_MODE_BIT      4
#define FW_DSTATE_SOC_LIV_MIN_BIT           5
#define FW_DSTATE_SOC_PLL_PWRDN_BIT         6
#define FW_DSTATE_MEM_PLL_PWRDN_BIT         7
#define FW_DSTATE_MALL_ALLOC_BIT            8
#define FW_DSTATE_MEM_PSI_BIT               9
#define FW_DSTATE_HSR_NON_STROBE_BIT        10
#define FW_DSTATE_MP0_ENTER_WFI_BIT         11
#define FW_DSTATE_MALL_FLUSH_BIT            12
#define FW_DSTATE_SOC_PSI_BIT               13
#define FW_DSTATE_MMHUB_INTERLOCK_BIT       14
#define FW_DSTATE_D0i3_2_QUIET_FW_BIT       15
#define FW_DSTATE_CLDO_PRG_BIT              16
#define FW_DSTATE_DF_PLL_PWRDN_BIT          17

//LED Display Mask & Control Bits
#define LED_DISPLAY_GFX_DPM_BIT            0
#define LED_DISPLAY_PCIE_BIT               1
#define LED_DISPLAY_ERROR_BIT              2


#define MEM_TEMP_READ_OUT_OF_BAND_BIT          0
#define MEM_TEMP_READ_IN_BAND_REFRESH_BIT      1
#define MEM_TEMP_READ_IN_BAND_DUMMY_PSTATE_BIT 2

typedef enum {
  SMARTSHIFT_VERSION_1,
  SMARTSHIFT_VERSION_2,
  SMARTSHIFT_VERSION_3,
} SMARTSHIFT_VERSION_e;

typedef enum {
  FOPT_CALC_AC_CALC_DC,
  FOPT_PPTABLE_AC_CALC_DC,
  FOPT_CALC_AC_PPTABLE_DC,
  FOPT_PPTABLE_AC_PPTABLE_DC,
} FOPT_CALC_e;

typedef enum {
  DRAM_BIT_WIDTH_DISABLED = 0,
  DRAM_BIT_WIDTH_X_8 = 8,
  DRAM_BIT_WIDTH_X_16 = 16,
  DRAM_BIT_WIDTH_X_32 = 32,
  DRAM_BIT_WIDTH_X_64 = 64,
  DRAM_BIT_WIDTH_X_128 = 128,
  DRAM_BIT_WIDTH_COUNT,
} DRAM_BIT_WIDTH_TYPE_e;

//I2C Interface
#define NUM_I2C_CONTROLLERS                8

#define I2C_CONTROLLER_ENABLED             1
#define I2C_CONTROLLER_DISABLED            0

#define MAX_SW_I2C_COMMANDS                24

typedef enum {
  I2C_CONTROLLER_PORT_0 = 0,  //CKSVII2C0
  I2C_CONTROLLER_PORT_1 = 1,  //CKSVII2C1
  I2C_CONTROLLER_PORT_COUNT,
} I2cControllerPort_e;

typedef enum {
  I2C_CONTROLLER_NAME_VR_GFX = 0,
  I2C_CONTROLLER_NAME_VR_SOC,
  I2C_CONTROLLER_NAME_VR_VMEMP,
  I2C_CONTROLLER_NAME_VR_VDDIO,
  I2C_CONTROLLER_NAME_LIQUID0,
  I2C_CONTROLLER_NAME_LIQUID1,
  I2C_CONTROLLER_NAME_PLX,
  I2C_CONTROLLER_NAME_FAN_INTAKE,
  I2C_CONTROLLER_NAME_COUNT,
} I2cControllerName_e;

typedef enum {
  I2C_CONTROLLER_THROTTLER_TYPE_NONE = 0,
  I2C_CONTROLLER_THROTTLER_VR_GFX,
  I2C_CONTROLLER_THROTTLER_VR_SOC,
  I2C_CONTROLLER_THROTTLER_VR_VMEMP,
  I2C_CONTROLLER_THROTTLER_VR_VDDIO,
  I2C_CONTROLLER_THROTTLER_LIQUID0,
  I2C_CONTROLLER_THROTTLER_LIQUID1,
  I2C_CONTROLLER_THROTTLER_PLX,
  I2C_CONTROLLER_THROTTLER_FAN_INTAKE,
  I2C_CONTROLLER_THROTTLER_INA3221,
  I2C_CONTROLLER_THROTTLER_COUNT,
} I2cControllerThrottler_e;

typedef enum {
  I2C_CONTROLLER_PROTOCOL_VR_XPDE132G5,
  I2C_CONTROLLER_PROTOCOL_VR_IR35217,
  I2C_CONTROLLER_PROTOCOL_TMP_MAX31875,
  I2C_CONTROLLER_PROTOCOL_INA3221,
  I2C_CONTROLLER_PROTOCOL_TMP_MAX6604,
  I2C_CONTROLLER_PROTOCOL_COUNT,
} I2cControllerProtocol_e;

typedef struct {
  uint8_t   Enabled;
  uint8_t   Speed;
  uint8_t   SlaveAddress;
  uint8_t   ControllerPort;
  uint8_t   ControllerName;
  uint8_t   ThermalThrotter;
  uint8_t   I2cProtocol;
  uint8_t   PaddingConfig;
} I2cControllerConfig_t;

typedef enum {
  I2C_PORT_SVD_SCL = 0,
  I2C_PORT_GPIO,
} I2cPort_e;

typedef enum {
  I2C_SPEED_FAST_50K = 0,      //50  Kbits/s
  I2C_SPEED_FAST_100K,         //100 Kbits/s
  I2C_SPEED_FAST_400K,         //400 Kbits/s
  I2C_SPEED_FAST_PLUS_1M,      //1   Mbits/s (in fast mode)
  I2C_SPEED_HIGH_1M,           //1   Mbits/s (in high speed mode)
  I2C_SPEED_HIGH_2M,           //2.3 Mbits/s
  I2C_SPEED_COUNT,
} I2cSpeed_e;

typedef enum {
  I2C_CMD_READ = 0,
  I2C_CMD_WRITE,
  I2C_CMD_COUNT,
} I2cCmdType_e;

#define CMDCONFIG_STOP_BIT             0
#define CMDCONFIG_RESTART_BIT          1
#define CMDCONFIG_READWRITE_BIT        2 //bit should be 0 for read, 1 for write

#define CMDCONFIG_STOP_MASK           (1 << CMDCONFIG_STOP_BIT)
#define CMDCONFIG_RESTART_MASK        (1 << CMDCONFIG_RESTART_BIT)
#define CMDCONFIG_READWRITE_MASK      (1 << CMDCONFIG_READWRITE_BIT)

typedef struct {
  uint8_t ReadWriteData;  //Return data for read. Data to send for write
  uint8_t CmdConfig; //Includes whether associated command should have a stop or restart command, and is a read or write
} SwI2cCmd_t; //SW I2C Command Table

typedef struct {
  uint8_t     I2CcontrollerPort; //CKSVII2C0(0) or //CKSVII2C1(1)
  uint8_t     I2CSpeed;          //Use I2cSpeed_e to indicate speed to select
  uint8_t     SlaveAddress;      //Slave address of device
  uint8_t     NumCmds;           //Number of commands

  SwI2cCmd_t  SwI2cCmds[MAX_SW_I2C_COMMANDS];
} SwI2cRequest_t; // SW I2C Request Table

typedef struct {
  SwI2cRequest_t SwI2cRequest;

  uint32_t Spare[8];
  uint32_t MmHubPadding[8]; // SMU internal use
} SwI2cRequestExternal_t;

typedef struct {
  uint64_t mca_umc_status;
  uint64_t mca_umc_addr;

  uint16_t ce_count_lo_chip;
  uint16_t ce_count_hi_chip;

  uint32_t eccPadding;
} EccInfo_t;

typedef struct {
  EccInfo_t  EccInfo[24];
} EccInfoTable_t;

#define EPCS_HIGH_POWER                  600
#define EPCS_NORMAL_POWER                450
#define EPCS_LOW_POWER                   300
#define EPCS_SHORTED_POWER               150
#define EPCS_NO_BOOTUP                   0

typedef enum{
  EPCS_SHORTED_LIMIT,
  EPCS_LOW_POWER_LIMIT,
  EPCS_NORMAL_POWER_LIMIT,
  EPCS_HIGH_POWER_LIMIT,
  EPCS_NOT_CONFIGURED,
  EPCS_STATUS_COUNT,
} EPCS_STATUS_e;

//D3HOT sequences
typedef enum {
  BACO_SEQUENCE,
  MSR_SEQUENCE,
  BAMACO_SEQUENCE,
  ULPS_SEQUENCE,
  D3HOT_SEQUENCE_COUNT,
} D3HOTSequence_e;

//This is aligned with RSMU PGFSM Register Mapping
typedef enum {
  PG_DYNAMIC_MODE = 0,
  PG_STATIC_MODE,
} PowerGatingMode_e;

//This is aligned with RSMU PGFSM Register Mapping
typedef enum {
  PG_POWER_DOWN = 0,
  PG_POWER_UP,
} PowerGatingSettings_e;

typedef struct {
  uint32_t a;  // store in IEEE float format in this variable
  uint32_t b;  // store in IEEE float format in this variable
  uint32_t c;  // store in IEEE float format in this variable
} QuadraticInt_t;

typedef struct {
  uint32_t m;  // store in IEEE float format in this variable
  uint32_t b;  // store in IEEE float format in this variable
} LinearInt_t;

typedef struct {
  uint32_t a;  // store in IEEE float format in this variable
  uint32_t b;  // store in IEEE float format in this variable
  uint32_t c;  // store in IEEE float format in this variable
} DroopInt_t;

typedef enum {
  DCS_ARCH_DISABLED,
  DCS_ARCH_FADCS,
  DCS_ARCH_ASYNC,
} DCS_ARCH_e;

//Only Clks that have DPM descriptors are listed here
typedef enum {
  PPCLK_GFXCLK = 0,
  PPCLK_SOCCLK,
  PPCLK_UCLK,
  PPCLK_FCLK,
  PPCLK_DCLK_0,
  PPCLK_VCLK_0,
  PPCLK_DISPCLK,
  PPCLK_DPPCLK,
  PPCLK_DPREFCLK,
  PPCLK_DCFCLK,
  PPCLK_DTBCLK,
  PPCLK_COUNT,
} PPCLK_e;

typedef enum {
  VOLTAGE_MODE_PPTABLE = 0,
  VOLTAGE_MODE_FUSES,
  VOLTAGE_MODE_COUNT,
} VOLTAGE_MODE_e;

typedef enum {
  AVFS_VOLTAGE_GFX = 0,
  AVFS_VOLTAGE_SOC,
  AVFS_VOLTAGE_COUNT,
} AVFS_VOLTAGE_TYPE_e;

typedef enum {
  AVFS_TEMP_COLD = 0,
  AVFS_TEMP_HOT,
  AVFS_TEMP_COUNT,
} AVFS_TEMP_e;

typedef enum {
  AVFS_D_G,
  AVFS_D_COUNT,
} AVFS_D_e;


typedef enum {
  UCLK_DIV_BY_1 = 0,
  UCLK_DIV_BY_2,
  UCLK_DIV_BY_4,
  UCLK_DIV_BY_8,
} UCLK_DIV_e;

typedef enum {
  GPIO_INT_POLARITY_ACTIVE_LOW = 0,
  GPIO_INT_POLARITY_ACTIVE_HIGH,
} GpioIntPolarity_e;

typedef enum {
  PWR_CONFIG_TDP = 0,
  PWR_CONFIG_TGP,
  PWR_CONFIG_TCP_ESTIMATED,
  PWR_CONFIG_TCP_MEASURED,
  PWR_CONFIG_TBP_DESKTOP,
  PWR_CONFIG_TBP_MOBILE,
} PwrConfig_e;

typedef struct {
  uint8_t        Padding;
  uint8_t        SnapToDiscrete;      // 0 - Fine grained DPM, 1 - Discrete DPM
  uint8_t        NumDiscreteLevels;   // Set to 2 (Fmin, Fmax) when using fine grained DPM, otherwise set to # discrete levels used
  uint8_t        CalculateFopt;       // Indication whether FW should calculate Fopt or use values below. Reference FOPT_CALC_e
  LinearInt_t    ConversionToAvfsClk; // Transfer function to AVFS Clock (GHz->GHz)
  uint32_t       Padding3[3];
  uint16_t       Padding4;
  uint16_t       FoptimalDc;          //Foptimal frequency in DC power mode.
  uint16_t       FoptimalAc;          //Foptimal frequency in AC power mode.
  uint16_t       Padding2;
} DpmDescriptor_t;

typedef enum  {
  PPT_THROTTLER_PPT0,
  PPT_THROTTLER_PPT1,
  PPT_THROTTLER_PPT2,
  PPT_THROTTLER_PPT3,
  PPT_THROTTLER_COUNT
} PPT_THROTTLER_e;

typedef enum  {
  TEMP_EDGE,
  TEMP_HOTSPOT,
  TEMP_HOTSPOT_GFX,
  TEMP_HOTSPOT_SOC,
  TEMP_MEM,
  TEMP_VR_GFX,
  TEMP_VR_SOC,
  TEMP_VR_MEM0,
  TEMP_VR_MEM1,
  TEMP_LIQUID0,
  TEMP_LIQUID1,
  TEMP_PLX,
  TEMP_COUNT,
} TEMP_e;

typedef enum {
  TDC_THROTTLER_GFX,
  TDC_THROTTLER_SOC,
  TDC_THROTTLER_COUNT
} TDC_THROTTLER_e;

typedef enum {
  SVI_PLANE_VDD_GFX,
  SVI_PLANE_VDD_SOC,
  SVI_PLANE_VDDCI_MEM,
  SVI_PLANE_VDDIO_MEM,
  SVI_PLANE_COUNT,
} SVI_PLANE_e;

typedef enum {
  PMFW_VOLT_PLANE_GFX,
  PMFW_VOLT_PLANE_SOC,
  PMFW_VOLT_PLANE_COUNT
} PMFW_VOLT_PLANE_e;

typedef enum {
  CUSTOMER_VARIANT_ROW,
  CUSTOMER_VARIANT_FALCON,
  CUSTOMER_VARIANT_COUNT,
} CUSTOMER_VARIANT_e;

typedef enum {
  POWER_SOURCE_AC,
  POWER_SOURCE_DC,
  POWER_SOURCE_COUNT,
} POWER_SOURCE_e;

typedef enum {
  MEM_VENDOR_PLACEHOLDER0,  // 0
  MEM_VENDOR_SAMSUNG,       // 1
  MEM_VENDOR_INFINEON,      // 2
  MEM_VENDOR_ELPIDA,        // 3
  MEM_VENDOR_ETRON,         // 4
  MEM_VENDOR_NANYA,         // 5
  MEM_VENDOR_HYNIX,         // 6
  MEM_VENDOR_MOSEL,         // 7
  MEM_VENDOR_WINBOND,       // 8
  MEM_VENDOR_ESMT,          // 9
  MEM_VENDOR_PLACEHOLDER1,  // 10
  MEM_VENDOR_PLACEHOLDER2,  // 11
  MEM_VENDOR_PLACEHOLDER3,  // 12
  MEM_VENDOR_PLACEHOLDER4,  // 13
  MEM_VENDOR_PLACEHOLDER5,  // 14
  MEM_VENDOR_MICRON,        // 15
  MEM_VENDOR_COUNT,
} MEM_VENDOR_e;

typedef enum {
  PP_GRTAVFS_HW_CPO_CTL_ZONE0,
  PP_GRTAVFS_HW_CPO_CTL_ZONE1,
  PP_GRTAVFS_HW_CPO_CTL_ZONE2,
  PP_GRTAVFS_HW_CPO_CTL_ZONE3,
  PP_GRTAVFS_HW_CPO_CTL_ZONE4,
  PP_GRTAVFS_HW_CPO_EN_0_31_ZONE0,
  PP_GRTAVFS_HW_CPO_EN_32_63_ZONE0,
  PP_GRTAVFS_HW_CPO_EN_0_31_ZONE1,
  PP_GRTAVFS_HW_CPO_EN_32_63_ZONE1,
  PP_GRTAVFS_HW_CPO_EN_0_31_ZONE2,
  PP_GRTAVFS_HW_CPO_EN_32_63_ZONE2,
  PP_GRTAVFS_HW_CPO_EN_0_31_ZONE3,
  PP_GRTAVFS_HW_CPO_EN_32_63_ZONE3,
  PP_GRTAVFS_HW_CPO_EN_0_31_ZONE4,
  PP_GRTAVFS_HW_CPO_EN_32_63_ZONE4,
  PP_GRTAVFS_HW_ZONE0_VF,
  PP_GRTAVFS_HW_ZONE1_VF1,
  PP_GRTAVFS_HW_ZONE2_VF2,
  PP_GRTAVFS_HW_ZONE3_VF3,
  PP_GRTAVFS_HW_VOLTAGE_GB,
  PP_GRTAVFS_HW_CPOSCALINGCTRL_ZONE0,
  PP_GRTAVFS_HW_CPOSCALINGCTRL_ZONE1,
  PP_GRTAVFS_HW_CPOSCALINGCTRL_ZONE2,
  PP_GRTAVFS_HW_CPOSCALINGCTRL_ZONE3,
  PP_GRTAVFS_HW_CPOSCALINGCTRL_ZONE4,
  PP_GRTAVFS_HW_RESERVED_0,
  PP_GRTAVFS_HW_RESERVED_1,
  PP_GRTAVFS_HW_RESERVED_2,
  PP_GRTAVFS_HW_RESERVED_3,
  PP_GRTAVFS_HW_RESERVED_4,
  PP_GRTAVFS_HW_RESERVED_5,
  PP_GRTAVFS_HW_RESERVED_6,
  PP_GRTAVFS_HW_FUSE_COUNT,
} PP_GRTAVFS_HW_FUSE_e;

typedef enum {
  PP_GRTAVFS_FW_COMMON_PPVMIN_Z1_HOT_T0,
  PP_GRTAVFS_FW_COMMON_PPVMIN_Z1_COLD_T0,
  PP_GRTAVFS_FW_COMMON_PPVMIN_Z2_HOT_T0,
  PP_GRTAVFS_FW_COMMON_PPVMIN_Z2_COLD_T0,
  PP_GRTAVFS_FW_COMMON_PPVMIN_Z3_HOT_T0,
  PP_GRTAVFS_FW_COMMON_PPVMIN_Z3_COLD_T0,
  PP_GRTAVFS_FW_COMMON_PPVMIN_Z4_HOT_T0,
  PP_GRTAVFS_FW_COMMON_PPVMIN_Z4_COLD_T0,
  PP_GRTAVFS_FW_COMMON_SRAM_RM_Z0,
  PP_GRTAVFS_FW_COMMON_SRAM_RM_Z1,
  PP_GRTAVFS_FW_COMMON_SRAM_RM_Z2,
  PP_GRTAVFS_FW_COMMON_SRAM_RM_Z3,
  PP_GRTAVFS_FW_COMMON_SRAM_RM_Z4,
  PP_GRTAVFS_FW_COMMON_FUSE_COUNT,
} PP_GRTAVFS_FW_COMMON_FUSE_e;

typedef enum {
  PP_GRTAVFS_FW_SEP_FUSE_GB1_PWL_VOLTAGE_NEG_1,
  PP_GRTAVFS_FW_SEP_FUSE_GB1_PWL_VOLTAGE_0,
  PP_GRTAVFS_FW_SEP_FUSE_GB1_PWL_VOLTAGE_1,
  PP_GRTAVFS_FW_SEP_FUSE_GB1_PWL_VOLTAGE_2,
  PP_GRTAVFS_FW_SEP_FUSE_GB1_PWL_VOLTAGE_3,
  PP_GRTAVFS_FW_SEP_FUSE_GB1_PWL_VOLTAGE_4,
  PP_GRTAVFS_FW_SEP_FUSE_GB2_PWL_VOLTAGE_NEG_1,
  PP_GRTAVFS_FW_SEP_FUSE_GB2_PWL_VOLTAGE_0,
  PP_GRTAVFS_FW_SEP_FUSE_GB2_PWL_VOLTAGE_1,
  PP_GRTAVFS_FW_SEP_FUSE_GB2_PWL_VOLTAGE_2,
  PP_GRTAVFS_FW_SEP_FUSE_GB2_PWL_VOLTAGE_3,
  PP_GRTAVFS_FW_SEP_FUSE_GB2_PWL_VOLTAGE_4,
  PP_GRTAVFS_FW_SEP_FUSE_VF_NEG_1_FREQUENCY,
  PP_GRTAVFS_FW_SEP_FUSE_VF4_FREQUENCY,
  PP_GRTAVFS_FW_SEP_FUSE_FREQUENCY_TO_COUNT_SCALER_0,
  PP_GRTAVFS_FW_SEP_FUSE_FREQUENCY_TO_COUNT_SCALER_1,
  PP_GRTAVFS_FW_SEP_FUSE_FREQUENCY_TO_COUNT_SCALER_2,
  PP_GRTAVFS_FW_SEP_FUSE_FREQUENCY_TO_COUNT_SCALER_3,
  PP_GRTAVFS_FW_SEP_FUSE_FREQUENCY_TO_COUNT_SCALER_4,
  PP_GRTAVFS_FW_SEP_FUSE_COUNT,
} PP_GRTAVFS_FW_SEP_FUSE_e;

#define PP_NUM_RTAVFS_PWL_ZONES 5
#define PP_NUM_PSM_DIDT_PWL_ZONES 3

// VBIOS or PPLIB configures telemetry slope and offset. Only slope expected to be set for SVI3
// Slope Q1.7, Offset Q1.2
typedef struct {
  int8_t   Offset; // in Amps
  uint8_t  Padding;
  uint16_t MaxCurrent; // in Amps
} SviTelemetryScale_t;

#define PP_NUM_OD_VF_CURVE_POINTS PP_NUM_RTAVFS_PWL_ZONES + 1

#define PP_OD_FEATURE_GFX_VF_CURVE_BIT       0
#define PP_OD_FEATURE_GFX_VMAX_BIT           1
#define PP_OD_FEATURE_SOC_VMAX_BIT           2
#define PP_OD_FEATURE_PPT_BIT                3
#define PP_OD_FEATURE_FAN_CURVE_BIT          4
#define PP_OD_FEATURE_FAN_LEGACY_BIT         5
#define PP_OD_FEATURE_FULL_CTRL_BIT          6
#define PP_OD_FEATURE_TDC_BIT                7
#define PP_OD_FEATURE_GFXCLK_BIT             8
#define PP_OD_FEATURE_UCLK_BIT               9
#define PP_OD_FEATURE_FCLK_BIT               10
#define PP_OD_FEATURE_ZERO_FAN_BIT           11
#define PP_OD_FEATURE_TEMPERATURE_BIT        12
#define PP_OD_FEATURE_EDC_BIT                13
#define PP_OD_FEATURE_COUNT                  14

typedef enum {
  PP_OD_POWER_FEATURE_ALWAYS_ENABLED,
  PP_OD_POWER_FEATURE_DISABLED_WHILE_GAMING,
  PP_OD_POWER_FEATURE_ALWAYS_DISABLED,
} PP_OD_POWER_FEATURE_e;

typedef enum {
  FAN_MODE_AUTO = 0,
  FAN_MODE_MANUAL_LINEAR,
} FanMode_e;

typedef enum {
  OD_NO_ERROR,
  OD_REQUEST_ADVANCED_NOT_SUPPORTED,
  OD_UNSUPPORTED_FEATURE,
  OD_INVALID_FEATURE_COMBO_ERROR,
  OD_GFXCLK_VF_CURVE_OFFSET_ERROR,
  OD_VDD_GFX_VMAX_ERROR,
  OD_VDD_SOC_VMAX_ERROR,
  OD_PPT_ERROR,
  OD_FAN_MIN_PWM_ERROR,
  OD_FAN_ACOUSTIC_TARGET_ERROR,
  OD_FAN_ACOUSTIC_LIMIT_ERROR,
  OD_FAN_TARGET_TEMP_ERROR,
  OD_FAN_ZERO_RPM_STOP_TEMP_ERROR,
  OD_FAN_CURVE_PWM_ERROR,
  OD_FAN_CURVE_TEMP_ERROR,
  OD_FULL_CTRL_GFXCLK_ERROR,
  OD_FULL_CTRL_UCLK_ERROR,
  OD_FULL_CTRL_FCLK_ERROR,
  OD_FULL_CTRL_VDD_GFX_ERROR,
  OD_FULL_CTRL_VDD_SOC_ERROR,
  OD_TDC_ERROR,
  OD_GFXCLK_ERROR,
  OD_UCLK_ERROR,
  OD_FCLK_ERROR,
  OD_OP_TEMP_ERROR,
  OD_OP_GFX_EDC_ERROR,
  OD_OP_GFX_PCC_ERROR,
  OD_POWER_FEATURE_CTRL_ERROR,
} OD_FAIL_e;

typedef struct {
  uint32_t               FeatureCtrlMask;

  //Voltage control
  int16_t                VoltageOffsetPerZoneBoundary[PP_NUM_OD_VF_CURVE_POINTS];

  uint16_t               VddGfxVmax;         // in mV
  uint16_t               VddSocVmax;

  uint8_t                IdlePwrSavingFeaturesCtrl;
  uint8_t                RuntimePwrSavingFeaturesCtrl;
  uint16_t               Padding;

  //Frequency changes
  int16_t                GfxclkFoffset;
  uint16_t               Padding1;
  uint16_t               UclkFmin;
  uint16_t               UclkFmax;
  uint16_t               FclkFmin;
  uint16_t               FclkFmax;

  //PPT
  int16_t                Ppt;         // %
  int16_t                Tdc;

  //Fan control
  uint8_t                FanLinearPwmPoints[NUM_OD_FAN_MAX_POINTS];
  uint8_t                FanLinearTempPoints[NUM_OD_FAN_MAX_POINTS];
  uint16_t               FanMinimumPwm;
  uint16_t               AcousticTargetRpmThreshold;
  uint16_t               AcousticLimitRpmThreshold;
  uint16_t               FanTargetTemperature; // Degree Celcius
  uint8_t                FanZeroRpmEnable;
  uint8_t                FanZeroRpmStopTemp;
  uint8_t                FanMode;
  uint8_t                MaxOpTemp;

  uint8_t                AdvancedOdModeEnabled;
  uint8_t                Padding2[3];

  uint16_t               GfxVoltageFullCtrlMode;
  uint16_t               SocVoltageFullCtrlMode;
  uint16_t               GfxclkFullCtrlMode;
  uint16_t               UclkFullCtrlMode;
  uint16_t               FclkFullCtrlMode;
  uint16_t               Padding3;

  int16_t                GfxEdc;
  int16_t                GfxPccLimitControl;

  uint16_t               GfxclkFmaxVmax;
  uint8_t                GfxclkFmaxVmaxTemperature;
  uint8_t                Padding4[1];

  uint32_t               Spare[9];
  uint32_t               MmHubPadding[8]; // SMU internal use. Adding here instead of external as a workaround
} OverDriveTable_t;

typedef struct {
  OverDriveTable_t OverDriveTable;

} OverDriveTableExternal_t;

typedef struct {
  uint32_t               FeatureCtrlMask;

  //Gfx Vf Curve
  int16_t                VoltageOffsetPerZoneBoundary[PP_NUM_OD_VF_CURVE_POINTS];
  //gfx Vmax
  uint16_t               VddGfxVmax;         // in mV
  //soc Vmax
  uint16_t               VddSocVmax;

  //gfxclk
  int16_t                GfxclkFoffset;
  uint16_t               Padding;
  //uclk
  uint16_t               UclkFmin;             // MHz
  uint16_t               UclkFmax;             // MHz
  //fclk
  uint16_t               FclkFmin;
  uint16_t               FclkFmax;

  //PPT
  int16_t                Ppt;         // %
  //TDC
  int16_t                Tdc;

  //Fan Curve
  uint8_t                FanLinearPwmPoints[NUM_OD_FAN_MAX_POINTS];
  uint8_t                FanLinearTempPoints[NUM_OD_FAN_MAX_POINTS];
  //Fan Legacy
  uint16_t               FanMinimumPwm;
  uint16_t               AcousticTargetRpmThreshold;
  uint16_t               AcousticLimitRpmThreshold;
  uint16_t               FanTargetTemperature; // Degree Celcius
  //zero fan
  uint8_t                FanZeroRpmEnable;
  //temperature
  uint8_t                MaxOpTemp;
  uint8_t                Padding1[2];

  //Full Ctrl
  uint16_t               GfxVoltageFullCtrlMode;
  uint16_t               SocVoltageFullCtrlMode;
  uint16_t               GfxclkFullCtrlMode;
  uint16_t               UclkFullCtrlMode;
  uint16_t               FclkFullCtrlMode;
  //EDC
  int16_t                GfxEdc;
  int16_t                GfxPccLimitControl;
  int16_t                Padding2;

  uint32_t               Spare[5];
} OverDriveLimits_t;

typedef enum {
  BOARD_GPIO_SMUIO_0,
  BOARD_GPIO_SMUIO_1,
  BOARD_GPIO_SMUIO_2,
  BOARD_GPIO_SMUIO_3,
  BOARD_GPIO_SMUIO_4,
  BOARD_GPIO_SMUIO_5,
  BOARD_GPIO_SMUIO_6,
  BOARD_GPIO_SMUIO_7,
  BOARD_GPIO_SMUIO_8,
  BOARD_GPIO_SMUIO_9,
  BOARD_GPIO_SMUIO_10,
  BOARD_GPIO_SMUIO_11,
  BOARD_GPIO_SMUIO_12,
  BOARD_GPIO_SMUIO_13,
  BOARD_GPIO_SMUIO_14,
  BOARD_GPIO_SMUIO_15,
  BOARD_GPIO_SMUIO_16,
  BOARD_GPIO_SMUIO_17,
  BOARD_GPIO_SMUIO_18,
  BOARD_GPIO_SMUIO_19,
  BOARD_GPIO_SMUIO_20,
  BOARD_GPIO_SMUIO_21,
  BOARD_GPIO_SMUIO_22,
  BOARD_GPIO_SMUIO_23,
  BOARD_GPIO_SMUIO_24,
  BOARD_GPIO_SMUIO_25,
  BOARD_GPIO_SMUIO_26,
  BOARD_GPIO_SMUIO_27,
  BOARD_GPIO_SMUIO_28,
  BOARD_GPIO_SMUIO_29,
  BOARD_GPIO_SMUIO_30,
  BOARD_GPIO_SMUIO_31,
  MAX_BOARD_GPIO_SMUIO_NUM,
  BOARD_GPIO_DC_GEN_A,
  BOARD_GPIO_DC_GEN_B,
  BOARD_GPIO_DC_GEN_C,
  BOARD_GPIO_DC_GEN_D,
  BOARD_GPIO_DC_GEN_E,
  BOARD_GPIO_DC_GEN_F,
  BOARD_GPIO_DC_GEN_G,
  BOARD_GPIO_DC_GENLK_CLK,
  BOARD_GPIO_DC_GENLK_VSYNC,
  BOARD_GPIO_DC_SWAPLOCK_A,
  BOARD_GPIO_DC_SWAPLOCK_B,
  MAX_BOARD_DC_GPIO_NUM,
  BOARD_GPIO_LV_EN,
} BOARD_GPIO_TYPE_e;

#define INVALID_BOARD_GPIO 0xFF


typedef struct {
  //PLL 0
  uint16_t InitImuClk;
  uint16_t InitSocclk;
  uint16_t InitMpioclk;
  uint16_t InitSmnclk;
  //PLL 1
  uint16_t InitDispClk;
  uint16_t InitDppClk;
  uint16_t InitDprefclk;
  uint16_t InitDcfclk;
  uint16_t InitDtbclk;
  uint16_t InitDbguSocClk;
  //PLL 2
  uint16_t InitGfxclk_bypass;
  uint16_t InitMp1clk;
  uint16_t InitLclk;
  uint16_t InitDbguBacoClk;
  uint16_t InitBaco400clk;
  uint16_t InitBaco1200clk_bypass;
  uint16_t InitBaco700clk_bypass;
  uint16_t InitBaco500clk;
  // PLL 3
  uint16_t InitDclk0;
  uint16_t InitVclk0;
  // PLL 4
  uint16_t InitFclk;
  uint16_t Padding1;
  // PLL 5
  //UCLK clocks, assumed all UCLK instances will be the same.
  uint8_t InitUclkLevel;    // =0,1,2,3,4,5 frequency from FreqTableUclk

  uint8_t Padding[3];

  uint32_t InitVcoFreqPll0; //smu_socclk_t
  uint32_t InitVcoFreqPll1; //smu_displayclk_t
  uint32_t InitVcoFreqPll2; //smu_nbioclk_t
  uint32_t InitVcoFreqPll3; //smu_vcnclk_t
  uint32_t InitVcoFreqPll4; //smu_fclk_t
  uint32_t InitVcoFreqPll5; //smu_uclk_01_t
  uint32_t InitVcoFreqPll6; //smu_uclk_23_t
  uint32_t InitVcoFreqPll7; //smu_uclk_45_t
  uint32_t InitVcoFreqPll8; //smu_uclk_67_t

  //encoding will be SVI3
  uint16_t InitGfx;       // In mV(Q2) ,  should be 0?
  uint16_t InitSoc;       // In mV(Q2)
  uint16_t InitVddIoMem;  // In mV(Q2) MemVdd
  uint16_t InitVddCiMem;  // In mV(Q2) VMemP

  //uint16_t Padding2;

  uint32_t Spare[8];
} BootValues_t;

typedef struct {
   uint16_t Power[PPT_THROTTLER_COUNT][POWER_SOURCE_COUNT]; // Watts
  uint16_t Tdc[TDC_THROTTLER_COUNT];             // Amps

  uint16_t Temperature[TEMP_COUNT]; // Celsius

  uint8_t  PwmLimitMin;
  uint8_t  PwmLimitMax;
  uint8_t  FanTargetTemperature;
  uint8_t  Spare1[1];

  uint16_t AcousticTargetRpmThresholdMin;
  uint16_t AcousticTargetRpmThresholdMax;

  uint16_t AcousticLimitRpmThresholdMin;
  uint16_t AcousticLimitRpmThresholdMax;

  uint16_t  PccLimitMin;
  uint16_t  PccLimitMax;

  uint16_t  FanStopTempMin;
  uint16_t  FanStopTempMax;
  uint16_t  FanStartTempMin;
  uint16_t  FanStartTempMax;

  uint16_t  PowerMinPpt0[POWER_SOURCE_COUNT];
  uint32_t  Spare[11];
} MsgLimits_t;

typedef struct {
  uint16_t BaseClockAc;
  uint16_t GameClockAc;
  uint16_t BoostClockAc;
  uint16_t BaseClockDc;
  uint16_t GameClockDc;
  uint16_t BoostClockDc;
  uint16_t MaxReportedClock;
  uint16_t Padding;
  uint32_t Reserved[3];
} DriverReportedClocks_t;

typedef struct {
  uint8_t           DcBtcEnabled;
  uint8_t           Padding[3];

  uint16_t          DcTol;            // mV Q2
  uint16_t          DcBtcGb;       // mV Q2

  uint16_t          DcBtcMin;       // mV Q2
  uint16_t          DcBtcMax;       // mV Q2

  LinearInt_t       DcBtcGbScalar;
} AvfsDcBtcParams_t;

typedef struct {
  uint16_t       AvfsTemp[AVFS_TEMP_COUNT]; //in degrees C
  uint16_t      VftFMin;  // in MHz
  uint16_t      VInversion; // in mV Q2
  QuadraticInt_t qVft[AVFS_TEMP_COUNT];
  QuadraticInt_t qAvfsGb;
  QuadraticInt_t qAvfsGb2;
} AvfsFuseOverride_t;

//all settings maintained by PFE team
typedef struct {
  uint8_t      Version;
  uint8_t      Spare8[3];
  // SECTION: Feature Control
  uint32_t     FeaturesToRun[NUM_FEATURES / 32]; // Features that PMFW will attempt to enable. Use FEATURE_*_BIT as mapping
  // SECTION: FW DSTATE Settings
  uint32_t     FwDStateMask;           // See FW_DSTATE_*_BIT for mapping
  // SECTION: Advanced Options
  uint32_t     DebugOverrides;

  uint32_t     Spare[2];
} PFE_Settings_t;

typedef struct {
  // SECTION: Version
  uint32_t Version; // should be unique to each SKU(i.e if any value changes in below structure then this value must be different)

  // SECTION: Miscellaneous Configuration
  uint8_t      TotalPowerConfig;    // Determines how PMFW calculates the power. Use defines from PwrConfig_e
  uint8_t      CustomerVariant; //To specify if this PPTable is intended for a particular customer. Use defines from CUSTOMER_VARIANT_e
  uint8_t      MemoryTemperatureTypeMask; // Bit mapping indicating which methods of memory temperature reading are enabled. Use defines from MEM_TEMP_*BIT
  uint8_t      SmartShiftVersion; // Determine what SmartShift feature version is supported Use defines from SMARTSHIFT_VERSION_e

  // SECTION: Infrastructure Limits
  uint8_t  SocketPowerLimitSpare[10];

  //if set to 1, SocketPowerLimitAc and SocketPowerLimitDc will be interpreted as legacy programs(i.e absolute power). If 0, all except index 0 will be scalars
  //relative index 0
  uint8_t  EnableLegacyPptLimit;
  uint8_t  UseInputTelemetry; //applicable to SVI3 only and only to be set if VRs support

  uint8_t  SmartShiftMinReportedPptinDcs; //minimum possible active power consumption for this SKU. Used for SmartShift power reporting

  uint8_t  PaddingPpt[7];

  uint16_t HwCtfTempLimit; // In degrees Celsius. Temperature above which HW will trigger CTF. Consumed by VBIOS only

  uint16_t PaddingInfra;

  // Per year normalized Vmax state failure rates (sum of the two domains divided by life time in years)
  uint32_t FitControllerFailureRateLimit; //in IEEE float
  //Expected GFX Duty Cycle at Vmax.
  uint32_t FitControllerGfxDutyCycle; // in IEEE float
  //Expected SOC Duty Cycle at Vmax.
  uint32_t FitControllerSocDutyCycle; // in IEEE float

  //This offset will be deducted from the controller output to before it goes through the SOC Vset limiter block.
  uint32_t FitControllerSocOffset;  //in IEEE float

  uint32_t     GfxApccPlusResidencyLimit; // Percentage value. Used by APCC+ controller to control PCC residency to some value

  // SECTION: Throttler settings
  uint32_t ThrottlerControlMask;   // See THROTTLER_*_BIT for mapping


  // SECTION: Voltage Control Parameters
  uint16_t  UlvVoltageOffset[PMFW_VOLT_PLANE_COUNT]; // In mV(Q2). ULV offset used in either GFX_ULV or SOC_ULV(part of FW_DSTATE)

  uint8_t      Padding[2];
  uint16_t     DeepUlvVoltageOffsetSoc;        // In mV(Q2)  Long Idle Vmin (deep ULV), for VDD_SOC as part of FW_DSTATE

  // Voltage Limits
  uint16_t     DefaultMaxVoltage[PMFW_VOLT_PLANE_COUNT]; // In mV(Q2) Maximum voltage without FIT controller enabled
  uint16_t     BoostMaxVoltage[PMFW_VOLT_PLANE_COUNT]; // In mV(Q2) Maximum voltage with FIT controller enabled

  //Vmin Optimizations
  int16_t         VminTempHystersis[PMFW_VOLT_PLANE_COUNT]; // Celsius Temperature hysteresis for switching between low/high temperature values for Vmin
  int16_t         VminTempThreshold[PMFW_VOLT_PLANE_COUNT]; // Celsius Temperature threshold for switching between low/high temperature values for Vmin
  uint16_t        Vmin_Hot_T0[PMFW_VOLT_PLANE_COUNT];            //In mV(Q2) Initial (pre-aging) Vset to be used at hot.
  uint16_t        Vmin_Cold_T0[PMFW_VOLT_PLANE_COUNT];           //In mV(Q2) Initial (pre-aging) Vset to be used at cold.
  uint16_t        Vmin_Hot_Eol[PMFW_VOLT_PLANE_COUNT];           //In mV(Q2) End-of-life Vset to be used at hot.
  uint16_t        Vmin_Cold_Eol[PMFW_VOLT_PLANE_COUNT];          //In mV(Q2) End-of-life Vset to be used at cold.
  uint16_t        Vmin_Aging_Offset[PMFW_VOLT_PLANE_COUNT];      //In mV(Q2) Worst-case aging margin
  uint16_t        Spare_Vmin_Plat_Offset_Hot[PMFW_VOLT_PLANE_COUNT];   //In mV(Q2) Platform offset apply to T0 Hot
  uint16_t        Spare_Vmin_Plat_Offset_Cold[PMFW_VOLT_PLANE_COUNT];  //In mV(Q2) Platform offset apply to T0 Cold

  //This is a fixed/minimum VMIN aging degradation offset which is applied at T0. This reflects the minimum amount of aging already accounted for.
  uint16_t        VcBtcFixedVminAgingOffset[PMFW_VOLT_PLANE_COUNT];
  //Linear offset or GB term to account for mis-correlation between PSM and Vmin shift trends across parts.
  uint16_t        VcBtcVmin2PsmDegrationGb[PMFW_VOLT_PLANE_COUNT];
  //Scalar coefficient of the PSM aging degradation function
  uint32_t        VcBtcPsmA[PMFW_VOLT_PLANE_COUNT];                   // A_PSM
  //Exponential coefficient of the PSM aging degradation function
  uint32_t        VcBtcPsmB[PMFW_VOLT_PLANE_COUNT];                   // B_PSM
  //Scalar coefficient of the VMIN aging degradation function. Specified as worst case between hot and cold.
  uint32_t        VcBtcVminA[PMFW_VOLT_PLANE_COUNT];                  // A_VMIN
  //Exponential coefficient of the VMIN aging degradation function. Specified as worst case between hot and cold.
  uint32_t        VcBtcVminB[PMFW_VOLT_PLANE_COUNT];                  // B_VMIN

  uint8_t         PerPartVminEnabled[PMFW_VOLT_PLANE_COUNT];
  uint8_t         VcBtcEnabled[PMFW_VOLT_PLANE_COUNT];

  uint16_t        SocketPowerLimitAcTau[PPT_THROTTLER_COUNT]; // Time constant of LPF in ms
  uint16_t        SocketPowerLimitDcTau[PPT_THROTTLER_COUNT]; // Time constant of LPF in ms

  QuadraticInt_t  Gfx_Vmin_droop;
  QuadraticInt_t  Soc_Vmin_droop;
  uint32_t        SpareVmin[6];

  //SECTION: DPM Configuration 1
  DpmDescriptor_t DpmDescriptor[PPCLK_COUNT];

  uint16_t      FreqTableGfx        [NUM_GFXCLK_DPM_LEVELS  ];     // In MHz
  uint16_t      FreqTableVclk       [NUM_VCLK_DPM_LEVELS    ];     // In MHz
  uint16_t      FreqTableDclk       [NUM_DCLK_DPM_LEVELS    ];     // In MHz
  uint16_t      FreqTableSocclk     [NUM_SOCCLK_DPM_LEVELS  ];     // In MHz
  uint16_t      FreqTableUclk       [NUM_UCLK_DPM_LEVELS    ];     // In MHz
  uint16_t      FreqTableShadowUclk [NUM_UCLK_DPM_LEVELS    ];     // In MHz
  uint16_t      FreqTableDispclk    [NUM_DISPCLK_DPM_LEVELS ];     // In MHz
  uint16_t      FreqTableDppClk     [NUM_DPPCLK_DPM_LEVELS  ];     // In MHz
  uint16_t      FreqTableDprefclk   [NUM_DPREFCLK_DPM_LEVELS];     // In MHz
  uint16_t      FreqTableDcfclk     [NUM_DCFCLK_DPM_LEVELS  ];     // In MHz
  uint16_t      FreqTableDtbclk     [NUM_DTBCLK_DPM_LEVELS  ];     // In MHz
  uint16_t      FreqTableFclk       [NUM_FCLK_DPM_LEVELS    ];     // In MHz

  uint32_t      DcModeMaxFreq     [PPCLK_COUNT            ];     // In MHz

  uint16_t      GfxclkAibFmax;
  uint16_t      GfxDpmPadding;

  //GFX Idle Power Settings
  uint16_t      GfxclkFgfxoffEntry;   // Entry in RLC stage (PLL), in Mhz
  uint16_t      GfxclkFgfxoffExitImu; // Exit/Entry in IMU stage (BYPASS), in Mhz
  uint16_t      GfxclkFgfxoffExitRlc; // Exit in RLC stage (PLL), in Mhz
  uint16_t      GfxclkThrottleClock;  //Used primarily in DCS
  uint8_t       EnableGfxPowerStagesGpio; //Genlk_vsync GPIO flag used to control gfx power stages
  uint8_t       GfxIdlePadding;

  uint8_t       SmsRepairWRCKClkDivEn;
  uint8_t       SmsRepairWRCKClkDivVal;
  uint8_t       GfxOffEntryEarlyMGCGEn;
  uint8_t       GfxOffEntryForceCGCGEn;
  uint8_t       GfxOffEntryForceCGCGDelayEn;
  uint8_t       GfxOffEntryForceCGCGDelayVal; // in microseconds

  uint16_t      GfxclkFreqGfxUlv; // in MHz
  uint8_t       GfxIdlePadding2[2];
  uint32_t      GfxOffEntryHysteresis; //For RLC to count after it enters CGCG, and before triggers GFXOFF entry
  uint32_t      GfxoffSpare[15];

  // DFLL
  uint16_t      DfllMstrOscConfigA; //Used for voltage sensitivity slope tuning: 0 = (en_leaker << 9) | (en_vint1_reduce << 8) | (gain_code << 6) | (bias_code << 3) | (vint1_code << 1) | en_bias
  uint16_t      DfllSlvOscConfigA; //Used for voltage sensitivity slope tuning: 0 = (en_leaker << 9) | (en_vint1_reduce << 8) | (gain_code << 6) | (bias_code << 3) | (vint1_code << 1) | en_bias
  uint32_t      DfllBtcMasterScalerM;
  int32_t       DfllBtcMasterScalerB;
  uint32_t      DfllBtcSlaveScalerM;
  int32_t       DfllBtcSlaveScalerB;

  uint32_t      DfllPccAsWaitCtrl; //GDFLL_AS_WAIT_CTRL_PCC register value to be passed to RLC msg
  uint32_t      DfllPccAsStepCtrl; //GDFLL_AS_STEP_CTRL_PCC register value to be passed to RLC msg
  uint32_t      GfxDfllSpare[9];

  // DVO
  uint32_t        DvoPsmDownThresholdVoltage; //Voltage float
  uint32_t        DvoPsmUpThresholdVoltage; //Voltage float
  uint32_t        DvoFmaxLowScaler; //Unitless float

  // GFX DCS
  uint32_t      PaddingDcs;

  uint16_t      DcsMinGfxOffTime;     //Minimum amount of time PMFW shuts GFX OFF as part of GFX DCS phase
  uint16_t      DcsMaxGfxOffTime;      //Maximum amount of time PMFW can shut GFX OFF as part of GFX DCS phase at a stretch.

  uint32_t      DcsMinCreditAccum;    //Min amount of positive credit accumulation before waking GFX up as part of DCS.

  uint16_t      DcsExitHysteresis;    //The min amount of time power credit accumulator should have a value > 0 before SMU exits the DCS throttling phase.
  uint16_t      DcsTimeout;           //This is the amount of time SMU FW waits for RLC to put GFX into GFXOFF before reverting to the fallback mechanism of throttling GFXCLK to Fmin.

  uint32_t      DcsPfGfxFopt;         //Default to GFX FMIN
  uint32_t      DcsPfUclkFopt;        //Default to UCLK FMIN

  uint8_t       FoptEnabled;
  uint8_t       DcsSpare2[3];
  uint32_t      DcsFoptM;             //Tuning paramters to shift Fopt calculation, IEEE754 float
  uint32_t      DcsFoptB;             //Tuning paramters to shift Fopt calculation, IEEE754 float
  uint32_t      DcsSpare[9];

  // UCLK section
  uint8_t       UseStrobeModeOptimizations; //Set to indicate that FW should use strobe mode optimizations
  uint8_t       PaddingMem[3];

  uint8_t       UclkDpmPstates             [NUM_UCLK_DPM_LEVELS];     // 6 Primary SW DPM states (6 + 6 Shadow)
  uint8_t       UclkDpmShadowPstates       [NUM_UCLK_DPM_LEVELS];      // 6 Shadow SW DPM states (6 + 6 Shadow)
  uint8_t       FreqTableUclkDiv           [NUM_UCLK_DPM_LEVELS];     // 0:Div-1, 1:Div-1/2, 2:Div-1/4, 3:Div-1/8
  uint8_t       FreqTableShadowUclkDiv     [NUM_UCLK_DPM_LEVELS];     // 0:Div-1, 1:Div-1/2, 2:Div-1/4, 3:Div-1/8
  uint16_t      MemVmempVoltage            [NUM_UCLK_DPM_LEVELS];     // mV(Q2)
  uint16_t      MemVddioVoltage            [NUM_UCLK_DPM_LEVELS];     // mV(Q2)
  uint16_t      DalDcModeMaxUclkFreq;
  uint8_t       PaddingsMem[2];
  //FCLK Section
  uint32_t      PaddingFclk;

  // Link DPM Settings
  uint8_t       PcieGenSpeed[NUM_LINK_LEVELS];           ///< 0:PciE-gen1 1:PciE-gen2 2:PciE-gen3 3:PciE-gen4 4:PciE-gen5
  uint8_t       PcieLaneCount[NUM_LINK_LEVELS];          ///< 1=x1, 2=x2, 3=x4, 4=x8, 5=x12, 6=x16
  uint16_t      LclkFreq[NUM_LINK_LEVELS];

  // SECTION: VDD_GFX AVFS
  uint8_t       OverrideGfxAvfsFuses;
  uint8_t       GfxAvfsPadding[1];
  uint16_t      DroopGBStDev;

  uint32_t      SocHwRtAvfsFuses[PP_GRTAVFS_HW_FUSE_COUNT];   //new added for Soc domain
  uint32_t      GfxL2HwRtAvfsFuses[PP_GRTAVFS_HW_FUSE_COUNT]; //see fusedoc for encoding
  //uint32_t      GfxSeHwRtAvfsFuses[PP_GRTAVFS_HW_FUSE_COUNT];

  uint16_t      PsmDidt_Vcross[PP_NUM_PSM_DIDT_PWL_ZONES-1];
  uint32_t      PsmDidt_StaticDroop_A[PP_NUM_PSM_DIDT_PWL_ZONES];
  uint32_t      PsmDidt_StaticDroop_B[PP_NUM_PSM_DIDT_PWL_ZONES];
  uint32_t      PsmDidt_DynDroop_A[PP_NUM_PSM_DIDT_PWL_ZONES];
  uint32_t      PsmDidt_DynDroop_B[PP_NUM_PSM_DIDT_PWL_ZONES];
  uint32_t      spare_HwRtAvfsFuses[19];

  uint32_t      SocCommonRtAvfs[PP_GRTAVFS_FW_COMMON_FUSE_COUNT];
  uint32_t      GfxCommonRtAvfs[PP_GRTAVFS_FW_COMMON_FUSE_COUNT];

  uint32_t      SocFwRtAvfsFuses[PP_GRTAVFS_FW_SEP_FUSE_COUNT];
  uint32_t      GfxL2FwRtAvfsFuses[PP_GRTAVFS_FW_SEP_FUSE_COUNT];
  //uint32_t      GfxSeFwRtAvfsFuses[PP_GRTAVFS_FW_SEP_FUSE_COUNT];
  uint32_t      spare_FwRtAvfsFuses[PP_GRTAVFS_FW_SEP_FUSE_COUNT];

  uint32_t      Soc_Droop_PWL_F[PP_NUM_RTAVFS_PWL_ZONES];
  uint32_t      Soc_Droop_PWL_a[PP_NUM_RTAVFS_PWL_ZONES];
  uint32_t      Soc_Droop_PWL_b[PP_NUM_RTAVFS_PWL_ZONES];
  uint32_t      Soc_Droop_PWL_c[PP_NUM_RTAVFS_PWL_ZONES];

  uint32_t      Gfx_Droop_PWL_F[PP_NUM_RTAVFS_PWL_ZONES];
  uint32_t      Gfx_Droop_PWL_a[PP_NUM_RTAVFS_PWL_ZONES];
  uint32_t      Gfx_Droop_PWL_b[PP_NUM_RTAVFS_PWL_ZONES];
  uint32_t      Gfx_Droop_PWL_c[PP_NUM_RTAVFS_PWL_ZONES];

  uint32_t      Gfx_Static_PWL_Offset[PP_NUM_RTAVFS_PWL_ZONES];
  uint32_t      Soc_Static_PWL_Offset[PP_NUM_RTAVFS_PWL_ZONES];

  uint32_t      dGbV_dT_vmin;
  uint32_t      dGbV_dT_vmax;

  uint32_t      PaddingV2F[4];

  AvfsDcBtcParams_t DcBtcGfxParams;
  QuadraticInt_t    SSCurve_GFX;
  uint32_t   GfxAvfsSpare[29];

  //SECTION: VDD_SOC AVFS
  uint8_t      OverrideSocAvfsFuses;
  uint8_t      MinSocAvfsRevision;
  uint8_t      SocAvfsPadding[2];

  AvfsFuseOverride_t SocAvfsFuseOverride[AVFS_D_COUNT];

  DroopInt_t        dBtcGbSoc[AVFS_D_COUNT];            // GHz->V BtcGb

  LinearInt_t       qAgingGb[AVFS_D_COUNT];          // GHz->V

  QuadraticInt_t    qStaticVoltageOffset[AVFS_D_COUNT]; // GHz->V

  AvfsDcBtcParams_t DcBtcSocParams[AVFS_D_COUNT];

  QuadraticInt_t    SSCurve_SOC;
  uint32_t   SocAvfsSpare[29];

  //SECTION: Boot clock and voltage values
  BootValues_t BootValues;

  //SECTION: Driver Reported Clocks
  DriverReportedClocks_t DriverReportedClocks;

  //SECTION: Message Limits
  MsgLimits_t MsgLimits;

  //SECTION: OverDrive Limits
  OverDriveLimits_t OverDriveLimitsBasicMin;
  OverDriveLimits_t OverDriveLimitsBasicMax;
  OverDriveLimits_t OverDriveLimitsAdvancedMin;
  OverDriveLimits_t OverDriveLimitsAdvancedMax;

  // Section: Total Board Power idle vs active coefficients
  uint8_t     TotalBoardPowerSupport;
  uint8_t     TotalBoardPowerPadding[1];
  uint16_t    TotalBoardPowerRoc;

  //PMFW-11158
  QuadraticInt_t qFeffCoeffGameClock[POWER_SOURCE_COUNT];
  QuadraticInt_t qFeffCoeffBaseClock[POWER_SOURCE_COUNT];
  QuadraticInt_t qFeffCoeffBoostClock[POWER_SOURCE_COUNT];

  // APT GFX to UCLK mapping
  int32_t     AptUclkGfxclkLookup[POWER_SOURCE_COUNT][6];
  uint32_t    AptUclkGfxclkLookupHyst[POWER_SOURCE_COUNT][6];
  uint32_t    AptPadding;

  // Xvmin didt
  QuadraticInt_t  GfxXvminDidtDroopThresh;
  uint32_t        GfxXvminDidtResetDDWait;
  uint32_t        GfxXvminDidtClkStopWait;
  uint32_t        GfxXvminDidtFcsStepCtrl;
  uint32_t        GfxXvminDidtFcsWaitCtrl;

  // PSM based didt controller
  uint32_t        PsmModeEnabled; //0: all disabled 1: static mode only 2: dynamic mode only 3:static + dynamic mode
  uint32_t        P2v_a; // floating point in U32 format
  uint32_t        P2v_b;
  uint32_t        P2v_c;
  uint32_t        T2p_a;
  uint32_t        T2p_b;
  uint32_t        T2p_c;
  uint32_t        P2vTemp;
  QuadraticInt_t  PsmDidtStaticSettings;
  QuadraticInt_t  PsmDidtDynamicSettings;
  uint8_t         PsmDidtAvgDiv;
  uint8_t         PsmDidtForceStall;
  uint16_t        PsmDidtReleaseTimer;
  uint32_t        PsmDidtStallPattern; //Will be written to both pattern 1 and didt_static_level_prog
  // CAC EDC
  uint32_t        CacEdcCacLeakageC0;
  uint32_t        CacEdcCacLeakageC1;
  uint32_t        CacEdcCacLeakageC2;
  uint32_t        CacEdcCacLeakageC3;
  uint32_t        CacEdcCacLeakageC4;
  uint32_t        CacEdcCacLeakageC5;
  uint32_t        CacEdcGfxClkScalar;
  uint32_t        CacEdcGfxClkIntercept;
  uint32_t        CacEdcCac_m;
  uint32_t        CacEdcCac_b;
  uint32_t        CacEdcCurrLimitGuardband;
  uint32_t        CacEdcDynToTotalCacRatio;
  // GFX EDC XVMIN
  uint32_t        XVmin_Gfx_EdcThreshScalar;
  uint32_t        XVmin_Gfx_EdcEnableFreq;
  uint32_t        XVmin_Gfx_EdcPccAsStepCtrl;
  uint32_t        XVmin_Gfx_EdcPccAsWaitCtrl;
  uint16_t        XVmin_Gfx_EdcThreshold;
  uint16_t        XVmin_Gfx_EdcFiltHysWaitCtrl;
  // SOC EDC XVMIN
  uint32_t        XVmin_Soc_EdcThreshScalar;
  uint32_t        XVmin_Soc_EdcEnableFreq;
  uint32_t        XVmin_Soc_EdcThreshold; // LPF: number of cycles Xvmin_trig_filt will react.
  uint16_t        XVmin_Soc_EdcStepUpTime; // 10 bit, refclk count to step up throttle when PCC remains asserted.
  uint16_t        XVmin_Soc_EdcStepDownTime;// 10 bit, refclk count to step down throttle when PCC remains asserted.
  uint8_t         XVmin_Soc_EdcInitPccStep; // 3 bit, First Pcc Step number that will applied when PCC asserts.
  uint8_t         PaddingSocEdc[3];

  // Fuse Override for SOC and GFX XVMIN
  uint8_t         GfxXvminFuseOverride;
  uint8_t         SocXvminFuseOverride;
  uint8_t         PaddingXvminFuseOverride[2];
  uint8_t         GfxXvminFddTempLow;  // bit 7: sign, bit 0-6: ABS value
  uint8_t         GfxXvminFddTempHigh; // bit 7: sign, bit 0-6: ABS value
  uint8_t         SocXvminFddTempLow;  // bit 7: sign, bit 0-6: ABS value
  uint8_t         SocXvminFddTempHigh; // bit 7: sign, bit 0-6: ABS value


  uint16_t        GfxXvminFddVolt0;    // low voltage, in VID
  uint16_t        GfxXvminFddVolt1;    // mid voltage, in VID
  uint16_t        GfxXvminFddVolt2;    // high voltage, in VID
  uint16_t        SocXvminFddVolt0;    // low voltage, in VID
  uint16_t        SocXvminFddVolt1;    // mid voltage, in VID
  uint16_t        SocXvminFddVolt2;    // high voltage, in VID
  uint16_t        GfxXvminDsFddDsm[6]; // XVMIN DS, same organization with fuse
  uint16_t        GfxXvminEdcFddDsm[6];// XVMIN GFX EDC, same organization with fuse
  uint16_t        SocXvminEdcFddDsm[6];// XVMIN SOC EDC, same organization with fuse

  // SECTION: Sku Reserved
  uint32_t        Spare;

  // Padding for MMHUB - do not modify this
  uint32_t     MmHubPadding[8];
} SkuTable_t;

typedef struct {
  uint8_t SlewRateConditions;
  uint8_t LoadLineAdjust;
  uint8_t VoutOffset;
  uint8_t VidMax;
  uint8_t VidMin;
  uint8_t TenBitTelEn;
  uint8_t SixteenBitTelEn;
  uint8_t OcpThresh;
  uint8_t OcpWarnThresh;
  uint8_t OcpSettings;
  uint8_t VrhotThresh;
  uint8_t OtpThresh;
  uint8_t UvpOvpDeltaRef;
  uint8_t PhaseShed;
  uint8_t Padding[10];
  uint32_t SettingOverrideMask;
} Svi3RegulatorSettings_t;

typedef struct {
  // SECTION: Version
  uint32_t    Version; //should be unique to each board type

  // SECTION: I2C Control
  I2cControllerConfig_t  I2cControllers[NUM_I2C_CONTROLLERS];

  //SECTION SVI3 Board Parameters
  uint8_t      SlaveAddrMapping[SVI_PLANE_COUNT];
  uint8_t      VrPsiSupport[SVI_PLANE_COUNT];

  uint32_t     Svi3SvcSpeed;
  uint8_t      EnablePsi6[SVI_PLANE_COUNT];       // only applicable in SVI3

  // SECTION: Voltage Regulator Settings
  Svi3RegulatorSettings_t  Svi3RegSettings[SVI_PLANE_COUNT];

  // SECTION: GPIO Settings
  uint8_t      LedOffGpio;
  uint8_t      FanOffGpio;
  uint8_t      GfxVrPowerStageOffGpio;

  uint8_t      AcDcGpio;        // GPIO pin configured for AC/DC switching
  uint8_t      AcDcPolarity;    // GPIO polarity for AC/DC switching
  uint8_t      VR0HotGpio;      // GPIO pin configured for VR0 HOT event
  uint8_t      VR0HotPolarity;  // GPIO polarity for VR0 HOT event

  uint8_t      GthrGpio;        // GPIO pin configured for GTHR Event
  uint8_t      GthrPolarity;    // replace GPIO polarity for GTHR

  // LED Display Settings
  uint8_t      LedPin0;         // GPIO number for LedPin[0]
  uint8_t      LedPin1;         // GPIO number for LedPin[1]
  uint8_t      LedPin2;         // GPIO number for LedPin[2]
  uint8_t      LedEnableMask;

  uint8_t      LedPcie;        // GPIO number for PCIE results
  uint8_t      LedError;       // GPIO number for Error Cases
  uint8_t      PaddingLed;

  // SECTION: Clock Spread Spectrum

  // UCLK Spread Spectrum
  uint8_t      UclkTrainingModeSpreadPercent; // Q4.4
  uint8_t      UclkSpreadPadding;
  uint16_t     UclkSpreadFreq;      // kHz

  // UCLK Spread Spectrum
  uint8_t      UclkSpreadPercent[MEM_VENDOR_COUNT];

  // DFLL Spread Spectrum
  uint8_t      GfxclkSpreadEnable;

  // FCLK Spread Spectrum
  uint8_t      FclkSpreadPercent;   // Q4.4
  uint16_t     FclkSpreadFreq;      // kHz

  // Section: Memory Config
  uint8_t      DramWidth; // Width of interface to the channel for each DRAM module. See DRAM_BIT_WIDTH_TYPE_e
  uint8_t      PaddingMem1[7];

  // SECTION: UMC feature flags
  uint8_t      HsrEnabled;
  uint8_t      VddqOffEnabled;
  uint8_t      PaddingUmcFlags[2];

  uint32_t    Paddign1;
  uint32_t    BacoEntryDelay; // in milliseconds. Amount of time FW will wait to trigger BACO entry after receiving entry notification from OS

  uint8_t     FuseWritePowerMuxPresent;
  uint8_t     FuseWritePadding[3];

  // SECTION: EDC Params
  uint32_t    LoadlineGfx;
  uint32_t    LoadlineSoc;
  uint32_t    GfxEdcLimit;
  uint32_t    SocEdcLimit;

  uint32_t    RestBoardPower;         //power consumed by board that is not captured by the SVI3 input telemetry
  uint32_t    ConnectorsImpedance;   // impedance of the input ATX power connectors

  uint8_t      EpcsSens0;       //GPIO number for External Power Connector Support Sense0
  uint8_t      EpcsSens1;       //GPIO Number for External Power Connector Support Sense1
  uint8_t      PaddingEpcs[2];

  // SECTION: Board Reserved
  uint32_t    BoardSpare[52];

  // SECTION: Structure Padding

  // Padding for MMHUB - do not modify this
  uint32_t     MmHubPadding[8];
} BoardTable_t;

typedef struct {
  // SECTION: Infrastructure Limits
  uint16_t    SocketPowerLimitAc[PPT_THROTTLER_COUNT]; // In Watts. Power limit that PMFW attempts to control to in AC mode. Multiple limits supported

  uint16_t    VrTdcLimit[TDC_THROTTLER_COUNT];         // In Amperes. Current limit associated with VR regulator maximum temperature

  int16_t     TotalIdleBoardPowerM;
  int16_t     TotalIdleBoardPowerB;
  int16_t     TotalBoardPowerM;
  int16_t     TotalBoardPowerB;

  uint16_t    TemperatureLimit[TEMP_COUNT]; // In degrees Celsius. Temperature limit associated with each input

  // SECTION: Fan Control
  uint16_t    FanStopTemp[TEMP_COUNT];          //Celsius
  uint16_t    FanStartTemp[TEMP_COUNT];         //Celsius

  uint16_t    FanGain[TEMP_COUNT];

  uint16_t    FanPwmMin;
  uint16_t    AcousticTargetRpmThreshold;
  uint16_t    AcousticLimitRpmThreshold;
  uint16_t    FanMaximumRpm;
  uint16_t    MGpuAcousticLimitRpmThreshold;
  uint16_t    FanTargetGfxclk;
  uint32_t    TempInputSelectMask;
  uint8_t     FanZeroRpmEnable;
  uint8_t     FanTachEdgePerRev;
  uint16_t    FanPadding;
  uint16_t    FanTargetTemperature[TEMP_COUNT];

  // The following are AFC override parameters. Leave at 0 to use FW defaults.
  int16_t     FuzzyFan_ErrorSetDelta;
  int16_t     FuzzyFan_ErrorRateSetDelta;
  int16_t     FuzzyFan_PwmSetDelta;
  uint16_t    FanPadding2;

  uint16_t    FwCtfLimit[TEMP_COUNT];

  uint16_t    IntakeTempEnableRPM;
  int16_t     IntakeTempOffsetTemp;
  uint16_t    IntakeTempReleaseTemp;
  uint16_t    IntakeTempHighIntakeAcousticLimit;

  uint16_t    IntakeTempAcouticLimitReleaseRate;
  int16_t     FanAbnormalTempLimitOffset;    // FanStalledTempLimitOffset
  uint16_t    FanStalledTriggerRpm;          //
  uint16_t    FanAbnormalTriggerRpmCoeff;    // FanAbnormalTriggerRpm

  uint16_t    FanSpare[1];
  uint8_t     FanIntakeSensorSupport;
  uint8_t     FanIntakePadding;
  uint32_t    FanSpare2[12];

  uint32_t ODFeatureCtrlMask;

  uint16_t TemperatureLimit_Hynix; // In degrees Celsius. Memory temperature limit associated with Hynix
  uint16_t TemperatureLimit_Micron; // In degrees Celsius. Memory temperature limit associated with Micron
  uint16_t TemperatureFwCtfLimit_Hynix;
  uint16_t TemperatureFwCtfLimit_Micron;

  // SECTION: Board Reserved
  uint16_t    PlatformTdcLimit[TDC_THROTTLER_COUNT];             // In Amperes. Current limit associated with platform maximum temperature per VR current rail
  uint16_t    SocketPowerLimitDc[PPT_THROTTLER_COUNT];  // In Watts. Power limit that PMFW attempts to control to in DC mode. Multiple limits supported
  uint16_t    SocketPowerLimitSmartShift2; // In Watts. Power limit used SmartShift
  uint16_t    CustomSkuSpare16b;
  uint32_t    CustomSkuSpare32b[10];

  // SECTION: Structure Padding

  // Padding for MMHUB - do not modify this
  uint32_t    MmHubPadding[8];
} CustomSkuTable_t;

typedef struct {
  PFE_Settings_t PFE_Settings;
  SkuTable_t SkuTable;
  CustomSkuTable_t CustomSkuTable;
  BoardTable_t BoardTable;
} PPTable_t;

typedef struct {
  // Time constant parameters for clock averages in ms
  uint16_t     GfxclkAverageLpfTau;
  uint16_t     FclkAverageLpfTau;
  uint16_t     UclkAverageLpfTau;
  uint16_t     GfxActivityLpfTau;
  uint16_t     UclkActivityLpfTau;
  uint16_t     UclkMaxActivityLpfTau;
  uint16_t     SocketPowerLpfTau;
  uint16_t     VcnClkAverageLpfTau;
  uint16_t     VcnUsageAverageLpfTau;
  uint16_t     PcieActivityLpTau;
} DriverSmuConfig_t;

typedef struct {
  DriverSmuConfig_t DriverSmuConfig;

  uint32_t     Spare[8];
  // Padding - ignore
  uint32_t     MmHubPadding[8]; // SMU internal use
} DriverSmuConfigExternal_t;


typedef struct {

  uint16_t       FreqTableGfx      [NUM_GFXCLK_DPM_LEVELS  ];     // In MHz
  uint16_t       FreqTableVclk     [NUM_VCLK_DPM_LEVELS    ];     // In MHz
  uint16_t       FreqTableDclk     [NUM_DCLK_DPM_LEVELS    ];     // In MHz
  uint16_t       FreqTableSocclk   [NUM_SOCCLK_DPM_LEVELS  ];     // In MHz
  uint16_t       FreqTableUclk     [NUM_UCLK_DPM_LEVELS    ];     // In MHz
  uint16_t       FreqTableDispclk  [NUM_DISPCLK_DPM_LEVELS ];     // In MHz
  uint16_t       FreqTableDppClk   [NUM_DPPCLK_DPM_LEVELS  ];     // In MHz
  uint16_t       FreqTableDprefclk [NUM_DPREFCLK_DPM_LEVELS];     // In MHz
  uint16_t       FreqTableDcfclk   [NUM_DCFCLK_DPM_LEVELS  ];     // In MHz
  uint16_t       FreqTableDtbclk   [NUM_DTBCLK_DPM_LEVELS  ];     // In MHz
  uint16_t       FreqTableFclk     [NUM_FCLK_DPM_LEVELS    ];     // In MHz

  uint16_t       DcModeMaxFreq     [PPCLK_COUNT            ];     // In MHz

  uint16_t       Padding;

  uint32_t Spare[32];

  // Padding - ignore
  uint32_t     MmHubPadding[8]; // SMU internal use

} DriverInfoTable_t;

typedef struct {
  uint32_t CurrClock[PPCLK_COUNT];

  uint16_t AverageGfxclkFrequencyTarget;
  uint16_t AverageGfxclkFrequencyPreDs;
  uint16_t AverageGfxclkFrequencyPostDs;
  uint16_t AverageFclkFrequencyPreDs;
  uint16_t AverageFclkFrequencyPostDs;
  uint16_t AverageMemclkFrequencyPreDs  ; // this is scaled to actual memory clock
  uint16_t AverageMemclkFrequencyPostDs  ; // this is scaled to actual memory clock
  uint16_t AverageVclk0Frequency  ;
  uint16_t AverageDclk0Frequency  ;
  uint16_t AverageVclk1Frequency  ;
  uint16_t AverageDclk1Frequency  ;
  uint16_t AveragePCIeBusy        ;
  uint16_t dGPU_W_MAX             ;
  uint16_t padding                ;

  uint16_t MovingAverageGfxclkFrequencyTarget;
  uint16_t MovingAverageGfxclkFrequencyPreDs;
  uint16_t MovingAverageGfxclkFrequencyPostDs;
  uint16_t MovingAverageFclkFrequencyPreDs;
  uint16_t MovingAverageFclkFrequencyPostDs;
  uint16_t MovingAverageMemclkFrequencyPreDs;
  uint16_t MovingAverageMemclkFrequencyPostDs;
  uint16_t MovingAverageVclk0Frequency;
  uint16_t MovingAverageDclk0Frequency;
  uint16_t MovingAverageGfxActivity;
  uint16_t MovingAverageUclkActivity;
  uint16_t MovingAverageVcn0ActivityPercentage;
  uint16_t MovingAveragePCIeBusy;
  uint16_t MovingAverageUclkActivity_MAX;
  uint16_t MovingAverageSocketPower;
  uint16_t MovingAveragePadding;

  uint32_t MetricsCounter         ;

  uint16_t AvgVoltage[SVI_PLANE_COUNT];
  uint16_t AvgCurrent[SVI_PLANE_COUNT];

  uint16_t AverageGfxActivity    ;
  uint16_t AverageUclkActivity   ;
  uint16_t AverageVcn0ActivityPercentage;
  uint16_t Vcn1ActivityPercentage  ;

  uint32_t EnergyAccumulator;
  uint16_t AverageSocketPower;
  uint16_t AverageTotalBoardPower;

  uint16_t AvgTemperature[TEMP_COUNT];
  uint16_t AvgTemperatureFanIntake;

  uint8_t  PcieRate               ;
  uint8_t  PcieWidth              ;

  uint8_t  AvgFanPwm;
  uint8_t  Padding[1];
  uint16_t AvgFanRpm;


  uint8_t  ThrottlingPercentage[THROTTLER_COUNT];
  uint8_t  VmaxThrottlingPercentage;
  uint8_t  padding1[2];

  //metrics for D3hot entry/exit and driver ARM msgs
  uint32_t D3HotEntryCountPerMode[D3HOT_SEQUENCE_COUNT];
  uint32_t D3HotExitCountPerMode[D3HOT_SEQUENCE_COUNT];
  uint32_t ArmMsgReceivedCountPerMode[D3HOT_SEQUENCE_COUNT];

  uint16_t ApuSTAPMSmartShiftLimit;
  uint16_t ApuSTAPMLimit;
  uint16_t AvgApuSocketPower;

  uint16_t AverageUclkActivity_MAX;

  uint32_t PublicSerialNumberLower;
  uint32_t PublicSerialNumberUpper;

} SmuMetrics_t;

typedef struct {
  SmuMetrics_t SmuMetrics;
  uint32_t Spare[30];

  // Padding - ignore
  uint32_t     MmHubPadding[8]; // SMU internal use
} SmuMetricsExternal_t;

typedef struct {
  uint8_t  WmSetting;
  uint8_t  Flags;
  uint8_t  Padding[2];

} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4

typedef enum {
  WATERMARKS_CLOCK_RANGE = 0,
  WATERMARKS_DUMMY_PSTATE,
  WATERMARKS_MALL,
  WATERMARKS_COUNT,
} WATERMARKS_FLAGS_e;

typedef struct {
  // Watermarks
  WatermarkRowGeneric_t WatermarkRow[NUM_WM_RANGES];
} Watermarks_t;

typedef struct {
  Watermarks_t Watermarks;
  uint32_t  Spare[16];

  uint32_t     MmHubPadding[8]; // SMU internal use
} WatermarksExternal_t;

typedef struct {
  uint16_t avgPsmCount[76];
  uint16_t minPsmCount[76];
  uint16_t maxPsmCount[76];
  float    avgPsmVoltage[76];
  float    minPsmVoltage[76];
  float    maxPsmVoltage[76];
} AvfsDebugTable_t;

typedef struct {
  AvfsDebugTable_t AvfsDebugTable;

  uint32_t     MmHubPadding[8]; // SMU internal use
} AvfsDebugTableExternal_t;


typedef struct {
  uint8_t   Gfx_ActiveHystLimit;
  uint8_t   Gfx_IdleHystLimit;
  uint8_t   Gfx_FPS;
  uint8_t   Gfx_MinActiveFreqType;
  uint8_t   Gfx_BoosterFreqType;
  uint8_t   PaddingGfx;
  uint16_t  Gfx_MinActiveFreq;              // MHz
  uint16_t  Gfx_BoosterFreq;                // MHz
  uint16_t  Gfx_PD_Data_time_constant;      // Time constant of PD controller in ms
  uint32_t  Gfx_PD_Data_limit_a;            // Q16
  uint32_t  Gfx_PD_Data_limit_b;            // Q16
  uint32_t  Gfx_PD_Data_limit_c;            // Q16
  uint32_t  Gfx_PD_Data_error_coeff;        // Q16
  uint32_t  Gfx_PD_Data_error_rate_coeff;   // Q16

  uint8_t   Fclk_ActiveHystLimit;
  uint8_t   Fclk_IdleHystLimit;
  uint8_t   Fclk_FPS;
  uint8_t   Fclk_MinActiveFreqType;
  uint8_t   Fclk_BoosterFreqType;
  uint8_t   PaddingFclk;
  uint16_t  Fclk_MinActiveFreq;              // MHz
  uint16_t  Fclk_BoosterFreq;                // MHz
  uint16_t  Fclk_PD_Data_time_constant;      // Time constant of PD controller in ms
  uint32_t  Fclk_PD_Data_limit_a;            // Q16
  uint32_t  Fclk_PD_Data_limit_b;            // Q16
  uint32_t  Fclk_PD_Data_limit_c;            // Q16
  uint32_t  Fclk_PD_Data_error_coeff;        // Q16
  uint32_t  Fclk_PD_Data_error_rate_coeff;   // Q16

  uint32_t  Mem_UpThreshold_Limit[NUM_UCLK_DPM_LEVELS];          // Q16
  uint8_t   Mem_UpHystLimit[NUM_UCLK_DPM_LEVELS];
  uint16_t  Mem_DownHystLimit[NUM_UCLK_DPM_LEVELS];
  uint16_t  Mem_Fps;

} DpmActivityMonitorCoeffInt_t;


typedef struct {
  DpmActivityMonitorCoeffInt_t DpmActivityMonitorCoeffInt;
  uint32_t     MmHubPadding[8]; // SMU internal use
} DpmActivityMonitorCoeffIntExternal_t;



// Workload bits
#define WORKLOAD_PPLIB_DEFAULT_BIT        0
#define WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT 1
#define WORKLOAD_PPLIB_POWER_SAVING_BIT   2
#define WORKLOAD_PPLIB_VIDEO_BIT          3
#define WORKLOAD_PPLIB_VR_BIT             4
#define WORKLOAD_PPLIB_COMPUTE_BIT        5
#define WORKLOAD_PPLIB_CUSTOM_BIT         6
#define WORKLOAD_PPLIB_WINDOW_3D_BIT      7
#define WORKLOAD_PPLIB_DIRECT_ML_BIT      8
#define WORKLOAD_PPLIB_CGVDI_BIT          9
#define WORKLOAD_PPLIB_COUNT              10


// These defines are used with the following messages:
// SMC_MSG_TransferTableDram2Smu
// SMC_MSG_TransferTableSmu2Dram

// Table transfer status
#define TABLE_TRANSFER_OK         0x0
#define TABLE_TRANSFER_FAILED     0xFF
#define TABLE_TRANSFER_PENDING    0xAB

#define TABLE_PPT_FAILED                          0x100
#define TABLE_TDC_FAILED                          0x200
#define TABLE_TEMP_FAILED                         0x400
#define TABLE_FAN_TARGET_TEMP_FAILED              0x800
#define TABLE_FAN_STOP_TEMP_FAILED               0x1000
#define TABLE_FAN_START_TEMP_FAILED              0x2000
#define TABLE_FAN_PWM_MIN_FAILED                 0x4000
#define TABLE_ACOUSTIC_TARGET_RPM_FAILED         0x8000
#define TABLE_ACOUSTIC_LIMIT_RPM_FAILED         0x10000
#define TABLE_MGPU_ACOUSTIC_TARGET_RPM_FAILED   0x20000

// Table types
#define TABLE_PPTABLE            0
#define TABLE_COMBO_PPTABLE           1
#define TABLE_WATERMARKS              2
#define TABLE_AVFS_PSM_DEBUG          3
#define TABLE_PMSTATUSLOG             4
#define TABLE_SMU_METRICS             5
#define TABLE_DRIVER_SMU_CONFIG       6
#define TABLE_ACTIVITY_MONITOR_COEFF  7
#define TABLE_OVERDRIVE               8
#define TABLE_I2C_COMMANDS            9
#define TABLE_DRIVER_INFO             10
#define TABLE_ECCINFO                 11
#define TABLE_CUSTOM_SKUTABLE         12
#define TABLE_COUNT                   13

//IH Interupt ID
#define IH_INTERRUPT_ID_TO_DRIVER                   0xFE
#define IH_INTERRUPT_CONTEXT_ID_BACO                0x2
#define IH_INTERRUPT_CONTEXT_ID_AC                  0x3
#define IH_INTERRUPT_CONTEXT_ID_DC                  0x4
#define IH_INTERRUPT_CONTEXT_ID_AUDIO_D0            0x5
#define IH_INTERRUPT_CONTEXT_ID_AUDIO_D3            0x6
#define IH_INTERRUPT_CONTEXT_ID_THERMAL_THROTTLING  0x7
#define IH_INTERRUPT_CONTEXT_ID_FAN_ABNORMAL        0x8
#define IH_INTERRUPT_CONTEXT_ID_FAN_RECOVERY        0x9
#define IH_INTERRUPT_CONTEXT_ID_DYNAMIC_TABLE       0xA

#endif
