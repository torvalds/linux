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

#ifndef __SMU_V14_0_0_PPSMC_H__
#define __SMU_V14_0_0_PPSMC_H__

/*! @mainpage PMFW-PPS (PPLib) Message Interface
  This documentation contains the subsections:\n\n
  @ref ResponseCodes\n
  @ref definitions\n
  @ref enums\n
*/

/** @def PPS_PMFW_IF_VER
* PPS (PPLib) to PMFW IF version 1.0
*/
#define PPS_PMFW_IF_VER "1.0" ///< Major.Minor

/** @defgroup ResponseCodes PMFW Response Codes
*  @{
*/
// SMU Response Codes:
#define PPSMC_Result_OK                    0x1  ///< Message Response OK
#define PPSMC_Result_Failed                0xFF ///< Message Response Failed
#define PPSMC_Result_UnknownCmd            0xFE ///< Message Response Unknown Command
#define PPSMC_Result_CmdRejectedPrereq     0xFD ///< Message Response Command Failed Prerequisite
#define PPSMC_Result_CmdRejectedBusy       0xFC ///< Message Response Command Rejected due to PMFW is busy. Sender should retry sending this message
/** @}*/

/** @defgroup definitions Message definitions
*  @{
*/
// Message Definitions:
#define PPSMC_MSG_TestMessage                   0x01 ///< To check if PMFW is alive and responding. Requirement specified by PMFW team
#define PPSMC_MSG_GetPmfwVersion                0x02 ///< Get PMFW version
#define PPSMC_MSG_GetDriverIfVersion            0x03 ///< Get PMFW_DRIVER_IF version
#define PPSMC_MSG_PowerDownVcn1                 0x04 ///< Power down VCN1
#define PPSMC_MSG_PowerUpVcn1                   0x05 ///< Power up VCN1; VCN1 is power gated by default
#define PPSMC_MSG_PowerDownVcn0                 0x06 ///< Power down VCN0
#define PPSMC_MSG_PowerUpVcn0                   0x07 ///< Power up VCN0; VCN0 is power gated by default
#define PPSMC_MSG_SetHardMinVcn0                0x08 ///< For wireless display
#define PPSMC_MSG_SetSoftMinGfxclk              0x09 ///< Set SoftMin for GFXCLK, argument is frequency in MHz
#define PPSMC_MSG_SetHardMinVcn1                0x0A ///< For wireless display
#define PPSMC_MSG_SetSoftMinVcn1                0x0B ///< Set soft min for VCN1 clocks (VCLK1 and DCLK1)
#define PPSMC_MSG_PrepareMp1ForUnload           0x0C ///< Prepare PMFW for GFX driver unload
#define PPSMC_MSG_SetDriverDramAddrHigh         0x0D ///< Set high 32 bits of DRAM address for Driver table transfer
#define PPSMC_MSG_SetDriverDramAddrLow          0x0E ///< Set low 32 bits of DRAM address for Driver table transfer
#define PPSMC_MSG_TransferTableSmu2Dram         0x0F ///< Transfer driver interface table from PMFW SRAM to DRAM
#define PPSMC_MSG_TransferTableDram2Smu         0x10 ///< Transfer driver interface table from DRAM to PMFW SRAM
#define PPSMC_MSG_GfxDeviceDriverReset          0x11 ///< Request GFX mode 2 reset
#define PPSMC_MSG_GetEnabledSmuFeatures         0x12 ///< Get enabled features in PMFW
#define PPSMC_MSG_SetHardMinSocclkByFreq        0x13 ///< Set hard min for SOC CLK
#define PPSMC_MSG_SetSoftMinFclk                0x14 ///< Set hard min for FCLK
#define PPSMC_MSG_SetSoftMinVcn0                0x15 ///< Set soft min for VCN0 clocks (VCLK0 and DCLK0)
#define PPSMC_MSG_EnableGfxImu                  0x16 ///< Enable GFX IMU
#define PPSMC_MSG_spare_0x17                    0x17 ///< Get GFX clock frequency
#define PPSMC_MSG_spare_0x18                    0x18 ///< Get FCLK frequency
#define PPSMC_MSG_AllowGfxOff                   0x19 ///< Inform PMFW of allowing GFXOFF entry
#define PPSMC_MSG_DisallowGfxOff                0x1A ///< Inform PMFW of disallowing GFXOFF entry
#define PPSMC_MSG_SetSoftMaxGfxClk              0x1B ///< Set soft max for GFX CLK
#define PPSMC_MSG_SetHardMinGfxClk              0x1C ///< Set hard min for GFX CLK
#define PPSMC_MSG_SetSoftMaxSocclkByFreq        0x1D ///< Set soft max for SOC CLK
#define PPSMC_MSG_SetSoftMaxFclkByFreq          0x1E ///< Set soft max for FCLK
#define PPSMC_MSG_SetSoftMaxVcn0                0x1F ///< Set soft max for VCN0 clocks (VCLK0 and DCLK0)
#define PPSMC_MSG_spare_0x20                    0x20 ///< Set power limit percentage
#define PPSMC_MSG_PowerDownJpeg0                0x21 ///< Power down Jpeg of VCN0
#define PPSMC_MSG_PowerUpJpeg0                  0x22 ///< Power up Jpeg of VCN0; VCN0 is power gated by default
#define PPSMC_MSG_SetHardMinFclkByFreq          0x23 ///< Set hard min for FCLK
#define PPSMC_MSG_SetSoftMinSocclkByFreq        0x24 ///< Set soft min for SOC CLK
#define PPSMC_MSG_AllowZstates                  0x25 ///< Inform PMFM of allowing Zstate entry, i.e. no Miracast activity
#define PPSMC_MSG_PowerDownJpeg1                0x26 ///< Power down Jpeg of VCN1
#define PPSMC_MSG_PowerUpJpeg1                  0x27 ///< Power up Jpeg of VCN1; VCN1 is power gated by default
#define PPSMC_MSG_SetSoftMaxVcn1                0x28 ///< Set soft max for VCN1 clocks (VCLK1 and DCLK1)
#define PPSMC_MSG_PowerDownIspByTile            0x29 ///< ISP is power gated by default
#define PPSMC_MSG_PowerUpIspByTile              0x2A ///< This message is used to power up ISP tiles and enable the ISP DPM
#define PPSMC_MSG_SetHardMinIspiclkByFreq       0x2B ///< Set HardMin by frequency for ISPICLK
#define PPSMC_MSG_SetHardMinIspxclkByFreq       0x2C ///< Set HardMin by frequency for ISPXCLK
#define PPSMC_MSG_PowerDownUmsch                0x2D ///< Power down VCN0.UMSCH (aka VSCH) scheduler
#define PPSMC_MSG_PowerUpUmsch                  0x2E ///< Power up VCN0.UMSCH (aka VSCH) scheduler
#define PPSMC_Message_IspStutterOn_MmhubPgDis   0x2F ///< ISP StutterOn mmHub PgDis
#define PPSMC_Message_IspStutterOff_MmhubPgEn   0x30 ///< ISP StufferOff mmHub PgEn
#define PPSMC_MSG_PowerUpVpe                    0x31 ///< Power up VPE
#define PPSMC_MSG_PowerDownVpe                  0x32 ///< Power down VPE
#define PPSMC_MSG_GetVpeDpmTable                0x33 ///< Get VPE DPM table
#define PPSMC_MSG_EnableLSdma                   0x34 ///< Enable LSDMA
#define PPSMC_MSG_DisableLSdma                  0x35 ///< Disable LSDMA
#define PPSMC_MSG_SetSoftMaxVpe                 0x36 ///<
#define PPSMC_MSG_SetSoftMinVpe                 0x37 ///<
#define PPSMC_MSG_MALLPowerController           0x38 ///< Set MALL control
#define PPSMC_MSG_MALLPowerState                0x39 ///< Enter/Exit MALL PG
#define PPSMC_Message_Count                     0x3A ///< Total number of PPSMC messages
/** @}*/

/**
* @defgroup enums Enum Definitions
*  @{
*/

/** @enum Mode_Reset_e
* Mode reset type, argument for PPSMC_MSG_GfxDeviceDriverReset
*/
//argument for PPSMC_MSG_GfxDeviceDriverReset
typedef enum {
  MODE1_RESET = 1,  ///< Mode reset type 1
  MODE2_RESET = 2   ///< Mode reset type 2
} Mode_Reset_e;

/** @}*/

/** @enum ZStates_e
* Zstate types, argument for PPSMC_MSG_AllowZstates
*/
//Argument for PPSMC_MSG_AllowZstates
typedef enum  {
  DISALLOW_ZSTATES = 0, ///< Disallow Zstates
  ALLOW_ZSTATES_Z8 = 8, ///< Allows Z8 only
  ALLOW_ZSTATES_Z9 = 9, ///< Allows Z9 and Z8
} ZStates_e;

/** @}*/
#endif
