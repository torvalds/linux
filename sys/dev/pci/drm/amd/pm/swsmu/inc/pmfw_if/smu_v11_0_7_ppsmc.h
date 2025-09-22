/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef SMU_V11_0_7_PPSMC_H
#define SMU_V11_0_7_PPSMC_H

#define PPSMC_VERSION 0x1

// SMU Response Codes:
#define PPSMC_Result_OK                    0x1
#define PPSMC_Result_Failed                0xFF
#define PPSMC_Result_UnknownCmd            0xFE
#define PPSMC_Result_CmdRejectedPrereq     0xFD
#define PPSMC_Result_CmdRejectedBusy       0xFC

// Message Definitions:
// BASIC
#define PPSMC_MSG_TestMessage                    0x1
#define PPSMC_MSG_GetSmuVersion                  0x2
#define PPSMC_MSG_GetDriverIfVersion             0x3
#define PPSMC_MSG_SetAllowedFeaturesMaskLow      0x4
#define PPSMC_MSG_SetAllowedFeaturesMaskHigh     0x5
#define PPSMC_MSG_EnableAllSmuFeatures           0x6
#define PPSMC_MSG_DisableAllSmuFeatures          0x7
#define PPSMC_MSG_EnableSmuFeaturesLow           0x8
#define PPSMC_MSG_EnableSmuFeaturesHigh          0x9
#define PPSMC_MSG_DisableSmuFeaturesLow          0xA
#define PPSMC_MSG_DisableSmuFeaturesHigh         0xB
#define PPSMC_MSG_GetRunningSmuFeaturesLow       0xC
#define PPSMC_MSG_GetRunningSmuFeaturesHigh      0xD
#define PPSMC_MSG_SetDriverDramAddrHigh          0xE
#define PPSMC_MSG_SetDriverDramAddrLow           0xF
#define PPSMC_MSG_SetToolsDramAddrHigh           0x10
#define PPSMC_MSG_SetToolsDramAddrLow            0x11
#define PPSMC_MSG_TransferTableSmu2Dram          0x12
#define PPSMC_MSG_TransferTableDram2Smu          0x13
#define PPSMC_MSG_UseDefaultPPTable              0x14

//BACO/BAMACO/BOMACO
#define PPSMC_MSG_EnterBaco                      0x15
#define PPSMC_MSG_ExitBaco                       0x16
#define PPSMC_MSG_ArmD3                          0x17
#define PPSMC_MSG_BacoAudioD3PME                 0x18

//DPM
#define PPSMC_MSG_SetSoftMinByFreq               0x19
#define PPSMC_MSG_SetSoftMaxByFreq               0x1A
#define PPSMC_MSG_SetHardMinByFreq               0x1B
#define PPSMC_MSG_SetHardMaxByFreq               0x1C
#define PPSMC_MSG_GetMinDpmFreq                  0x1D
#define PPSMC_MSG_GetMaxDpmFreq                  0x1E
#define PPSMC_MSG_GetDpmFreqByIndex              0x1F
#define PPSMC_MSG_OverridePcieParameters         0x20

//DramLog Set DramAddrHigh
#define PPSMC_MSG_DramLogSetDramAddrHigh         0x21

#define PPSMC_MSG_SetWorkloadMask                0x22
#define PPSMC_MSG_SetUclkFastSwitch              0x23
#define PPSMC_MSG_GetVoltageByDpm                0x24
#define PPSMC_MSG_SetVideoFps                    0x25
#define PPSMC_MSG_GetDcModeMaxDpmFreq            0x26

//DramLog Set DramAddrLow
#define PPSMC_MSG_DramLogSetDramAddrLow          0x27

//Power Gating
#define PPSMC_MSG_AllowGfxOff                    0x28
#define PPSMC_MSG_DisallowGfxOff                 0x29
#define PPSMC_MSG_PowerUpVcn                     0x2A
#define PPSMC_MSG_PowerDownVcn	                 0x2B
#define PPSMC_MSG_PowerUpJpeg                    0x2C
#define PPSMC_MSG_PowerDownJpeg	                 0x2D

//Resets
#define PPSMC_MSG_PrepareMp1ForUnload            0x2E

//DramLog Set DramLog SetDramSize 
#define PPSMC_MSG_DramLogSetDramSize             0x2F

#define PPSMC_MSG_Mode1Reset                     0x30

//Set SystemVirtual DramAddrHigh
#define PPSMC_MSG_SetSystemVirtualDramAddrHigh   0x31

//ACDC Power Source
#define PPSMC_MSG_SetPptLimit                    0x32
#define PPSMC_MSG_GetPptLimit                    0x33
#define PPSMC_MSG_ReenableAcDcInterrupt          0x34
#define PPSMC_MSG_NotifyPowerSource              0x35

//BTC
#define PPSMC_MSG_RunDcBtc                       0x36

//Set SystemVirtual DramAddrLow
#define PPSMC_MSG_SetSystemVirtualDramAddrLow    0x38

//Others
#define PPSMC_MSG_SetMemoryChannelEnable         0x39
#define PPSMC_MSG_SetDramBitWidth                0x3A
#define PPSMC_MSG_SetGeminiMode                  0x3B
#define PPSMC_MSG_SetGeminiApertureHigh          0x3C
#define PPSMC_MSG_SetGeminiApertureLow           0x3D

#define PPSMC_MSG_SetTemperatureInputSelect      0x3E
#define PPSMC_MSG_SetFwDstatesMask               0x3F
#define PPSMC_MSG_SetThrottlerMask               0x40

#define PPSMC_MSG_SetExternalClientDfCstateAllow 0x41
#define PPSMC_MSG_EnableOutOfBandMonTesting      0x42
#define PPSMC_MSG_SetMGpuFanBoostLimitRpm        0x43

#define PPSMC_MSG_SetNumBadHbmPagesRetired       0x44
#define PPSMC_MSG_SetGpoFeaturePMask             0x45
#define PPSMC_MSG_SetSMBUSInterrupt              0x46

#define PPSMC_MSG_DisallowGpo                    0x56

#define PPSMC_MSG_Enable2ndUSB20Port             0x57
#define PPSMC_MSG_DriverMode2Reset               0x5D
#define PPSMC_Message_Count                      0x5E

#endif
