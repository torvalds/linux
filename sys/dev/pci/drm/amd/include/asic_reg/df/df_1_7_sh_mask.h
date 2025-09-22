/*
 * Copyright (C) 2018  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _df_1_7_SH_MASK_HEADER
#define _df_1_7_SH_MASK_HEADER

/* FabricConfigAccessControl */
#define FabricConfigAccessControl__CfgRegInstAccEn__SHIFT						0x0
#define FabricConfigAccessControl__CfgRegInstAccRegLock__SHIFT						0x1
#define FabricConfigAccessControl__CfgRegInstID__SHIFT							0x10
#define FabricConfigAccessControl__CfgRegInstAccEn_MASK							0x00000001L
#define FabricConfigAccessControl__CfgRegInstAccRegLock_MASK						0x00000002L
#define FabricConfigAccessControl__CfgRegInstID_MASK							0x00FF0000L

/* DF_PIE_AON0_DfGlobalClkGater */
#define DF_PIE_AON0_DfGlobalClkGater__MGCGMode__SHIFT							0x0
#define DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK							0x0000000FL

/* DF_CS_AON0_DramBaseAddress0 */
#define DF_CS_AON0_DramBaseAddress0__AddrRngVal__SHIFT							0x0
#define DF_CS_AON0_DramBaseAddress0__LgcyMmioHoleEn__SHIFT						0x1
#define DF_CS_AON0_DramBaseAddress0__IntLvNumChan__SHIFT						0x4
#define DF_CS_AON0_DramBaseAddress0__IntLvAddrSel__SHIFT						0x8
#define DF_CS_AON0_DramBaseAddress0__DramBaseAddr__SHIFT						0xc
#define DF_CS_AON0_DramBaseAddress0__AddrRngVal_MASK							0x00000001L
#define DF_CS_AON0_DramBaseAddress0__LgcyMmioHoleEn_MASK						0x00000002L
#define DF_CS_AON0_DramBaseAddress0__IntLvNumChan_MASK							0x000000F0L
#define DF_CS_AON0_DramBaseAddress0__IntLvAddrSel_MASK							0x00000700L
#define DF_CS_AON0_DramBaseAddress0__DramBaseAddr_MASK							0xFFFFF000L

//DF_CS_AON0_CoherentSlaveModeCtrlA0
#define DF_CS_AON0_CoherentSlaveModeCtrlA0__ForceParWrRMW__SHIFT					0x3
#define DF_CS_AON0_CoherentSlaveModeCtrlA0__ForceParWrRMW_MASK						0x00000008L

#endif
