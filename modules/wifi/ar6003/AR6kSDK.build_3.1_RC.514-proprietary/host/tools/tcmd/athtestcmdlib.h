/*
 * Copyright (c) 2006 Atheros Communications Inc.
 * All rights reserved.
 * 
 * 
// The software source and binaries included in this development package are
// licensed, not sold. You, or your company, received the package under one
// or more license agreements. The rights granted to you are specifically
// listed in these license agreement(s). All other rights remain with Atheros
// Communications, Inc., its subsidiaries, or the respective owner including
// those listed on the included copyright notices.  Distribution of any
// portion of this package must be in strict compliance with the license
// agreement(s) terms.
// </copyright>
// 
// <summary>
// 	Wifi driver for AR6002
// </summary>
//
 * 
 */
#ifndef _ATH_TESTCMD_LIB_H
#define _ATH_TESTCMD_LIB_H

#ifdef __cplusplus
extern "C" {
#endif 

typedef enum _AthDataRate {
	ATH_RATE_1M,
	ATH_RATE_2M,
	ATH_RATE_5_5M,
	ATH_RATE_11M,
	ATH_RATE_6M,
	ATH_RATE_9M,
	ATH_RATE_12M,
	ATH_RATE_18M,
	ATH_RATE_24M,
	ATH_RATE_36M,
	ATH_RATE_48M,
	ATH_RATE_54M,
	ATH_RATE_6_5M,
	ATH_RATE_13M,
	ATH_RATE_19_5M,
	ATH_RATE_26M,
	ATH_RATE_39M,
	ATH_RATE_52M,
	ATH_RATE_58_5M,
	ATH_RATE_65M,
    ATH_RATE_HT40_13_5M,
    ATH_RATE_HT40_27M,
    ATH_RATE_HT40_40_5M,
    ATH_RATE_HT40_54M,
    ATH_RATE_HT40_81M,
    ATH_RATE_HT40_108M,
    ATH_RATE_HT40_121_5M,
    ATH_RATE_HT40_135M,
} AthDataRate;

typedef enum _AthHtMode {
    ATH_NOHT,
    ATH_HT20,
    ATH_HT40Minus,
    ATH_HT40Plus,
} AthHtMode;

int athApiInit(void);
void athApiCleanup(void);
void athChannelSet(int channel);
void athRateSet(AthDataRate r);
void athTxPowerSet(int txpwr);
void athHtModeSet(AthHtMode mode);

/** @breif Enable long preamble */
int athSetLongPreamble(int enable);

/** @breif Set the interval between frames in aifs number
 *  @param slot aifs slot 0->SIFS, 1->PIFS, 2->DIFS, ... 253 */
void athSetAifsNum(int slot);

void athTxPacketSizeSet(int size);
void athShortGuardSet(int enable);
int athTxSineStart(void);
int athTx99Start(void);
int athTxFrameStart(void);
int athTxStop(void);
int athRxPacketStart(void);
int athRxPacketStop(void);
uint32_t athRxGetErrorFrameNum(void);
uint32_t athRxGetGoodFrameNum(void);
const char *athGetErrorString(void);

#ifdef __cplusplus
} /* extern "C" */
#endif 

#endif 

