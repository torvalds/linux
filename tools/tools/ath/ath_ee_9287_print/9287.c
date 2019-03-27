/*
 * Copyright (c) 2010-2011 Adrian Chadd, Xenion Pty Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <err.h>

typedef enum {
        AH_FALSE = 0,           /* NB: lots of code assumes false is zero */
        AH_TRUE  = 1,
} HAL_BOOL;

typedef enum {
        HAL_OK          = 0,    /* No error */
} HAL_STATUS;

struct ath_hal;

#include "ah_eeprom_v14.h"
#include "ah_eeprom_9287.h"

void
eeprom_9287_base_print(uint16_t *buf)
{
	HAL_EEPROM_9287 *eep = (HAL_EEPROM_9287 *) buf;
	BASE_EEP_9287_HEADER *eh = &eep->ee_base.baseEepHeader;
	int i;

	printf("| Version: 0x%.4x   | Length: 0x%.4x | Checksum: 0x%.4x ",
	    eh->version, eh->length, eh->checksum);
	printf("| CapFlags: 0x%.2x  | eepMisc: 0x%.2x | RegDomain: 0x%.4x 0x%.4x | \n",
	    eh->opCapFlags, eh->eepMisc, eh->regDmn[0], eh->regDmn[1]);
	printf("| MAC: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x ",
	    eh->macAddr[0], eh->macAddr[1], eh->macAddr[2],
	    eh->macAddr[3], eh->macAddr[4], eh->macAddr[5]);
	printf("| RxMask: 0x%.2x | TxMask: 0x%.2x | RfSilent: 0x%.4x | btOptions: 0x%.4x |\n",
	    eh->rxMask, eh->txMask, eh->rfSilent, eh->blueToothOptions);
	printf("| DeviceCap: 0x%.4x | binBuildNumber: %.8x | deviceType: 0x%.2x | openLoopPwrCntl 0x%.2x |\n",
	    eh->deviceCap, eh->binBuildNumber, eh->deviceType, eh->openLoopPwrCntl);
	printf("| pwrTableOffset: %d | tempSensSlope: %d | tempSensSlopePalOn: %d |\n",
	    eh->pwrTableOffset, eh->tempSensSlope, eh->tempSensSlopePalOn);

	printf("Future:\n");
	for (i = 0; i < sizeof(eh->futureBase) / sizeof(uint16_t); i++) {
		printf("0x%.2x ", eh->futureBase[i]);
	}
	printf("\n");
}

void
eeprom_9287_custdata_print(uint16_t *buf)
{
	HAL_EEPROM_9287 *eep = (HAL_EEPROM_9287 *) buf;
	uint8_t *custdata = (uint8_t *) &eep->ee_base.custData;
	int i;

	printf("\n| Custdata:                                       |\n");
	for (i = 0; i < 20; i++) {
		printf("%s0x%.2x %s",
		    i % 16 == 0 ? "| " : "",
		    custdata[i],
		    i % 16 == 15 ? "|\n" : "");
	}
	printf("\n");
}

void
eeprom_9287_modal_print(uint16_t *buf)
{
	HAL_EEPROM_9287 *eep = (HAL_EEPROM_9287 *) buf;
	MODAL_EEP_9287_HEADER *mh = &eep->ee_base.modalHeader;
	int i;

	printf("| antCtrlCommon: 0x%.8x |\n", mh->antCtrlCommon);
	printf("| switchSettling: 0x%.2x |\n", mh->switchSettling);
	printf("| adcDesiredSize: %d |\n", mh->adcDesiredSize);

	for (i = 0; i < AR9287_MAX_CHAINS; i++) {
		printf("| Chain %d:\n", i);
		printf("| antCtrlChain:        0:0x%.4x |\n", mh->antCtrlChain[i]);
		printf("| antennaGainCh:       0:0x%.2x |\n", mh->antennaGainCh[i]);
		printf("| txRxAttenCh:         0:0x%.2x |\n", mh->txRxAttenCh[i]);
		printf("| rxTxMarginCh:        0:0x%.2x |\n", mh->rxTxMarginCh[i]);
		printf("| noiseFloorThresCh:   0:0x%.2x |\n", mh->noiseFloorThreshCh[i]);
		printf("| iqCalICh:            0:0x%.2x |\n", mh->iqCalICh[i]);
		printf("| iqCalQCh:            0:0x%.2x |\n", mh->iqCalQCh[i]);
		printf("| bswAtten:            0:0x%.2x |\n", mh->bswAtten[i]);
		printf("| bswMargin:           0:0x%.2x |\n", mh->bswMargin[i]);
		printf("\n");
	}

	printf("| txEndToXpaOff: 0x%.2x | txEndToRxOn: 0x%.2x | txFrameToXpaOn: 0x%.2x |\n",
	    mh->txEndToXpaOff, mh->txEndToRxOn, mh->txFrameToXpaOn);
	printf("| thres62: 0x%.2x\n", mh->thresh62);
	printf("| xpdGain: 0x%.2x | xpd: 0x%.2x |\n", mh->xpdGain, mh->xpd);

	printf("| pdGainOverlap: 0x%.2x xpaBiasLvl: 0x%.2x |\n", mh->pdGainOverlap, mh->xpaBiasLvl);
	printf("| txFrameToDataStart: 0x%.2x | txFrameToPaOn: 0x%.2x |\n", mh->txFrameToDataStart, mh->txFrameToPaOn);
	printf("| ht40PowerIncForPdadc: 0x%.2x |\n", mh->ht40PowerIncForPdadc);
	printf("| swSettleHt40: 0x%.2x |\n", mh->swSettleHt40);

	printf("| Modal Version: %.2x |\n", mh->version);
	printf("| db1 = %d | db2 = %d |\n", mh->db1, mh->db2);
	printf("| ob_cck = %d | ob_psk = %d | ob_qam = %d | ob_pal_off = %d |\n",
	    mh->ob_cck, mh->ob_psk, mh->ob_qam, mh->ob_pal_off);

	printf("| futureModal: ");
	for (i = 0; i < sizeof(mh->futureModal) / sizeof(uint16_t); i++) {
	    printf("0x%.2x ", mh->futureModal[i]);
	}
	printf("\n");

	/* and now, spur channels */
	for (i = 0; i < AR5416_EEPROM_MODAL_SPURS; i++) {
		printf("| Spur %d: spurChan: 0x%.4x spurRangeLow: 0x%.2x spurRangeHigh: 0x%.2x |\n",
		    i, mh->spurChans[i].spurChan,
		    (int) mh->spurChans[i].spurRangeLow,
		    (int) mh->spurChans[i].spurRangeHigh);
	}
}

static void
eeprom_9287_print_caldata_oploop(struct cal_data_op_loop_ar9287 *f)
{
	int i, j;

	/* XXX flesh out the rest */
	for (i = 0; i < 2; i++) {
		printf("    pwrPdg:");
		for (j = 0; j < 5; j++) {
			printf("[%d][%d]=%d, ", i, j, f->pwrPdg[i][j]);
		}
		printf("\n");

		printf("    vpdPdg:");
		for (j = 0; j < 5; j++) {
			printf("[%d][%d]=%d, ", i, j, f->vpdPdg[i][j]);
		}
		printf("\n");

		printf("    pcdac:");
		for (j = 0; j < 5; j++) {
			printf("[%d][%d]=%d, ", i, j, f->pcdac[i][j]);
		}
		printf("\n");

		printf("    empty:");
		for (j = 0; j < 5; j++) {
			printf("[%d][%d]=%d, ", i, j, f->empty[i][j]);
		}
		printf("\n\n");
	}
}

void
eeprom_9287_calfreqpiers_print(uint16_t *buf)
{
	HAL_EEPROM_9287 *eep = (HAL_EEPROM_9287 *) buf;
	int i, n;

	/* 2ghz cal piers */
	printf("calFreqPier2G: ");
	for (i = 0; i < AR9287_NUM_2G_CAL_PIERS; i++) {
		printf(" 0x%.2x ", eep->ee_base.calFreqPier2G[i]);
	}
	printf("|\n");

	for (i = 0; i < AR9287_NUM_2G_CAL_PIERS; i++) {
		if (eep->ee_base.calFreqPier2G[i] == 0xff)
			continue;
		printf("2Ghz Cal Pier %d\n", i);
		for (n = 0; n < AR9287_MAX_CHAINS; n++) {
			printf("  Chain %d:\n", n);
			eeprom_9287_print_caldata_oploop((void *)&eep->ee_base.calPierData2G[n][i]);
		}
	}

	printf("\n");
}

/* XXX these should just reference the v14 print routines */
static void
eeprom_v14_target_legacy_print(CAL_TARGET_POWER_LEG *l)
{
	int i;
	if (l->bChannel == 0xff)
		return;
	printf("  bChannel: %d;", l->bChannel);
	for (i = 0; i < 4; i++) {
		printf(" %.2f", (float) l->tPow2x[i] / 2.0);
	}
	printf(" (dBm)\n");
}

static void
eeprom_v14_target_ht_print(CAL_TARGET_POWER_HT *l)
{
	int i;
	if (l->bChannel == 0xff)
		return;
	printf("  bChannel: %d;", l->bChannel);
	for (i = 0; i < 8; i++) {
		printf(" %.2f", (float) l->tPow2x[i] / 2.0);
	}
	printf(" (dBm)\n");
}

void
eeprom_9287_print_targets(uint16_t *buf)
{
	HAL_EEPROM_9287 *eep = (HAL_EEPROM_9287 *) buf;
	int i;

	/* 2ghz rates */
	printf("2Ghz CCK:\n");
	for (i = 0; i < AR9287_NUM_2G_CCK_TARGET_POWERS; i++) {
		eeprom_v14_target_legacy_print(&eep->ee_base.calTargetPowerCck[i]);
	}
	printf("2Ghz 11g:\n");
	for (i = 0; i < AR9287_NUM_2G_20_TARGET_POWERS; i++) {
		eeprom_v14_target_legacy_print(&eep->ee_base.calTargetPower2G[i]);
	}
	printf("2Ghz HT20:\n");
	for (i = 0; i < AR9287_NUM_2G_20_TARGET_POWERS; i++) {
		eeprom_v14_target_ht_print(&eep->ee_base.calTargetPower2GHT20[i]);
	}
	printf("2Ghz HT40:\n");
	for (i = 0; i < AR9287_NUM_2G_40_TARGET_POWERS; i++) {
		eeprom_v14_target_ht_print(&eep->ee_base.calTargetPower2GHT40[i]);
	}

}

static void
eeprom_9287_ctl_edge_print(struct cal_ctl_data_ar9287 *ctl)
{
	int i, j;
	uint8_t pow, flag;

	for (i = 0; i < AR9287_MAX_CHAINS; i++) {
		printf("  chain %d: ", i);
		for (j = 0; j < AR9287_NUM_BAND_EDGES; j++) {
			pow = ctl->ctlEdges[i][j].tPowerFlag & 0x3f;
			flag = (ctl->ctlEdges[i][j].tPowerFlag & 0xc0) >> 6;
			printf(" %d:pow=%d,flag=%.2x", j, pow, flag);
		}
		printf("\n");
	}
}

void
eeprom_9287_ctl_print(uint16_t *buf)
{
	HAL_EEPROM_9287 *eep = (HAL_EEPROM_9287 *) buf;
	int i;

	for (i = 0; i < AR9287_NUM_CTLS; i++) {
		if (eep->ee_base.ctlIndex[i] == 0)
			continue;
		printf("| ctlIndex: offset %d, value %d\n", i, eep->ee_base.ctlIndex[i]);
		eeprom_9287_ctl_edge_print(&eep->ee_base.ctlData[i]);
	}
}

void
eeprom_9287_print_edges(uint16_t *buf)
{
	HAL_EEPROM_9287 *eep = (HAL_EEPROM_9287 *) buf;
	int i;

	printf("| eeNumCtls: %d\n", eep->ee_numCtls);
	for (i = 0; i < NUM_EDGES*eep->ee_numCtls; i++) {
		/* XXX is flag 8 or 32 bits? */
		printf("|  edge %2d/%2d: rdEdge: %5d EdgePower: %.2f dBm Flag: 0x%.8x\n",
			i / NUM_EDGES, i % NUM_EDGES,
			eep->ee_rdEdgesPower[i].rdEdge,
			(float) eep->ee_rdEdgesPower[i].twice_rdEdgePower / 2.0,
			eep->ee_rdEdgesPower[i].flag);

		if (i % NUM_EDGES == (NUM_EDGES -1))
			printf("|\n");
	}
}

void
eeprom_9287_print_other(uint16_t *buf)
{
	HAL_EEPROM_9287 *eep = (HAL_EEPROM_9287 *) buf;
}
