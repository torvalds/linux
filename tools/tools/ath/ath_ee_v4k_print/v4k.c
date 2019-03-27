
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

#include <sys/types.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
        AH_FALSE = 0,           /* NB: lots of code assumes false is zero */
        AH_TRUE  = 1,
} HAL_BOOL;

typedef enum {
        HAL_OK          = 0,    /* No error */
} HAL_STATUS;

struct ath_hal;

#include "ah_eeprom_v4k.h"

void
eeprom_v4k_base_print(uint16_t *buf)
{
	HAL_EEPROM_v4k *eep = (HAL_EEPROM_v4k *) buf;
	BASE_EEP4K_HEADER *eh = &eep->ee_base.baseEepHeader;

	printf("| Version: 0x%.4x   | Length: 0x%.4x | Checksum: 0x%.4x ",
	    eh->version, eh->length, eh->checksum);
	printf("| CapFlags: 0x%.2x  | eepMisc: 0x%.2x | RegDomain: 0x%.4x 0x%.4x | \n",
	    eh->opCapFlags, eh->eepMisc, eh->regDmn[0], eh->regDmn[1]);
	printf("| MAC: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x ",
	    eh->macAddr[0], eh->macAddr[1], eh->macAddr[2],
	    eh->macAddr[3], eh->macAddr[4], eh->macAddr[5]);
	printf("| RxMask: 0x%.2x | TxMask: 0x%.2x | RfSilent: 0x%.4x | btOptions: 0x%.4x |\n",
	    eh->rxMask, eh->txMask, eh->rfSilent, eh->blueToothOptions);
	printf("| DeviceCap: 0x%.4x | binBuildNumber: %.8x | deviceType: 0x%.2x | txGainType 0x%.2x |\n",
	    eh->deviceCap, eh->binBuildNumber, eh->deviceType, eh->txGainType);
}

void
eeprom_v4k_custdata_print(uint16_t *buf)
{
	HAL_EEPROM_v4k *eep = (HAL_EEPROM_v4k *) buf;
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
eeprom_v4k_modal_print(uint16_t *buf)
{
	HAL_EEPROM_v4k *eep = (HAL_EEPROM_v4k *) buf;
	MODAL_EEP4K_HEADER *mh = &eep->ee_base.modalHeader;
	int i;

	printf("| antCtrlCommon: 0x%.8x |\n", mh->antCtrlCommon);
	printf("| switchSettling: 0x%.2x |\n", mh->switchSettling);
	printf("| adcDesiredSize: %d |\n| pgaDesiredSize: %.2f dBm |\n",
	    mh->adcDesiredSize, (float) mh->pgaDesiredSize / 2.0);

	printf("| antCtrlChain:        0:0x%.4x |\n", mh->antCtrlChain[0]);
	printf("| antennaGainCh:       0:0x%.2x |\n", mh->antennaGainCh[0]);
	printf("| txRxAttenCh:         0:0x%.2x |\n", mh->txRxAttenCh[0]);
	printf("| rxTxMarginCh:        0:0x%.2x |\n", mh->rxTxMarginCh[0]);
 	printf("| noiseFloorThresCh:   0:0x%.2x |\n", mh->noiseFloorThreshCh[0]);
	printf("| xlnaGainCh:          0:0x%.2x |\n", mh->xlnaGainCh[0]);
	printf("| iqCalICh:            0:0x%.2x |\n", mh->iqCalICh[0]);
	printf("| iqCalQCh:            0:0x%.2x |\n", mh->iqCalQCh[0]);
	printf("| bswAtten:            0:0x%.2x |\n", mh->bswAtten[0]);
	printf("| bswMargin:           0:0x%.2x |\n", mh->bswMargin[0]);
	printf("| xatten2Db:           0:0x%.2x |\n", mh->xatten2Db[0]);
	printf("| xatten2Margin:       0:0x%.2x |\n", mh->xatten2Margin[0]);

	printf("| txEndToXpaOff: 0x%.2x | txEndToRxOn: 0x%.2x | txFrameToXpaOn: 0x%.2x |\n",
	    mh->txEndToXpaOff, mh->txEndToRxOn, mh->txFrameToXpaOn);
	printf("| thres62: 0x%.2x\n", mh->thresh62);
	printf("| xpdGain: 0x%.2x | xpd: 0x%.2x |\n", mh->xpdGain, mh->xpd);

	printf("| pdGainOverlap: 0x%.2x xpaBiasLvl: 0x%.2x |\n", mh->pdGainOverlap, mh->xpaBiasLvl);
	printf("| txFrameToDataStart: 0x%.2x | txFrameToPaOn: 0x%.2x |\n", mh->txFrameToDataStart, mh->txFrameToPaOn);
	printf("| ht40PowerIncForPdadc: 0x%.2x |\n", mh->ht40PowerIncForPdadc);
	printf("| swSettleHt40: 0x%.2x |\n", mh->swSettleHt40);

	printf("| ob_0: 0x%.2x | ob_1: 0x%.2x | ob_2: 0x%.2x | ob_3: 0x%.2x |\n",
	    mh->ob_0, mh->ob_1, mh->ob_2, mh->ob_3);
	printf("| db_1_0: 0x%.2x | db_1_1: 0x%.2x | db_1_2: 0x%.2x | db_1_3: 0x%.2x db_1_4: 0x%.2x|\n",
	    mh->db1_0, mh->db1_1, mh->db1_2, mh->db1_3, mh->db1_4);
	printf("| db_1_0: 0x%.2x | db_1_1: 0x%.2x | db_1_2: 0x%.2x | db_1_3: 0x%.2x db_1_4: 0x%.2x|\n",
	    mh->db2_0, mh->db2_1, mh->db2_2, mh->db2_3, mh->db2_4);

	printf("| antdiv_ctl1: 0x%.2x antdiv_ctl2: 0x%.2x |\n", mh->antdiv_ctl1, mh->antdiv_ctl2);

	printf("| Modal Version: %.2x |\n", mh->version);

	printf("| tx_diversity: 0x%.2x |\n", mh->tx_diversity);
	printf("| flc_pwr_thresh: 0x%.2x |\n", mh->flc_pwr_thresh);
	printf("| bb_scale_smrt_antenna: 0x%.2x |\n", mh->bb_scale_smrt_antenna);
	printf("| futureModal: 0x%.2x |\n", mh->futureModal[0]);

	/* and now, spur channels */
	for (i = 0; i < AR5416_EEPROM_MODAL_SPURS; i++) {
		printf("| Spur %d: spurChan: 0x%.4x spurRangeLow: 0x%.2x spurRangeHigh: 0x%.2x |\n",
		    i, mh->spurChans[i].spurChan,
		    (int) mh->spurChans[i].spurRangeLow,
		    (int) mh->spurChans[i].spurRangeHigh);
	}
}

static void
eeprom_v4k_print_caldata_perfreq(CAL_DATA_PER_FREQ_4K *f)
{
	int i, j;

	for (i = 0; i < AR5416_4K_NUM_PD_GAINS; i++) {
		printf("    Gain %d: pwr dBm/vpd: ", i);
		for (j = 0; j < AR5416_PD_GAIN_ICEPTS; j++) {
			/* These are stored in 0.25dBm increments */
			/* XXX is this assumption correct for ar9285? */
			/* XXX shouldn't we care about the power table offset, if there is one? */
			printf("%d:(%.2f/%d) ", j, (float) f->pwrPdg[i][j] / 4.00,
			    f->vpdPdg[i][j]);
		}
		printf("\n");
	}
}

void
eeprom_v4k_calfreqpiers_print(uint16_t *buf)
{
	HAL_EEPROM_v4k *eep = (HAL_EEPROM_v4k *) buf;
	int i, n;

	/* 2ghz cal piers */
	printf("calFreqPier2G: ");
	for (i = 0; i < AR5416_4K_NUM_2G_CAL_PIERS; i++) {
		printf(" 0x%.2x ", eep->ee_base.calFreqPier2G[i]);
	}
	printf("|\n");

	for (i = 0; i < AR5416_4K_NUM_2G_CAL_PIERS; i++) {
		if (eep->ee_base.calFreqPier2G[i] == 0xff)
			continue;
		printf("2Ghz Cal Pier %d\n", i);
		for (n = 0; n < AR5416_4K_MAX_CHAINS; n++) {
			printf("  Chain %d:\n", n);
			eeprom_v4k_print_caldata_perfreq(&eep->ee_base.calPierData2G[n][i]);
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
eeprom_v4k_print_targets(uint16_t *buf)
{
	HAL_EEPROM_v4k *eep = (HAL_EEPROM_v4k *) buf;
	int i;

	/* 2ghz rates */
	printf("2Ghz CCK:\n");
	for (i = 0; i < AR5416_4K_NUM_2G_CCK_TARGET_POWERS; i++) {
		eeprom_v14_target_legacy_print(&eep->ee_base.calTargetPowerCck[i]);
	}
	printf("2Ghz 11g:\n");
	for (i = 0; i < AR5416_4K_NUM_2G_20_TARGET_POWERS; i++) {
		eeprom_v14_target_legacy_print(&eep->ee_base.calTargetPower2G[i]);
	}
	printf("2Ghz HT20:\n");
	for (i = 0; i < AR5416_4K_NUM_2G_20_TARGET_POWERS; i++) {
		eeprom_v14_target_ht_print(&eep->ee_base.calTargetPower2GHT20[i]);
	}
	printf("2Ghz HT40:\n");
	for (i = 0; i < AR5416_4K_NUM_2G_40_TARGET_POWERS; i++) {
		eeprom_v14_target_ht_print(&eep->ee_base.calTargetPower2GHT40[i]);
	}

}

static void
eeprom_v4k_ctl_edge_print(CAL_CTL_DATA_4K *ctl)
{
	int i, j;
	uint8_t pow, flag;

	for (i = 0; i < AR5416_4K_MAX_CHAINS; i++) {
		printf("  chain %d: ", i);
		for (j = 0; j < AR5416_4K_NUM_BAND_EDGES; j++) {
			pow = ctl->ctlEdges[i][j].tPowerFlag & 0x3f;
			flag = (ctl->ctlEdges[i][j].tPowerFlag & 0xc0) >> 6;
			printf(" %d:pow=%d,flag=%.2x", j, pow, flag);
		}
		printf("\n");
	}
}

void
eeprom_v4k_ctl_print(uint16_t *buf)
{
	HAL_EEPROM_v4k *eep = (HAL_EEPROM_v4k *) buf;
	int i;

	for (i = 0; i < AR5416_4K_NUM_CTLS; i++) {
		if (eep->ee_base.ctlIndex[i] == 0)
			continue;
		printf("| ctlIndex: offset %d, value %d\n", i, eep->ee_base.ctlIndex[i]);
		eeprom_v4k_ctl_edge_print(&eep->ee_base.ctlData[i]);
	}
}

void
eeprom_v4k_print_edges(uint16_t *buf)
{
	HAL_EEPROM_v4k *eep = (HAL_EEPROM_v4k *) buf;
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
eeprom_v4k_print_other(uint16_t *buf)
{
	HAL_EEPROM_v4k *eep = (HAL_EEPROM_v4k *) buf;
	printf("| ee_antennaGainMax: %.2x\n", eep->ee_antennaGainMax);
}
