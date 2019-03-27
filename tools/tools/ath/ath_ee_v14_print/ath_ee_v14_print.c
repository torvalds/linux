
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

void
load_eeprom_dump(const char *file, uint16_t *buf)
{
	unsigned int r[8];
	FILE *fp;
	char b[1024];
	int i;

	fp = fopen(file, "r");
	if (!fp)
		err(1, "fopen");

	while (!feof(fp)) {
		if (fgets(b, 1024, fp) == NULL)
			break;
		if (feof(fp))
			break;
		if (strlen(b) > 0)
			b[strlen(b)-1] = '\0';
		if (strlen(b) == 0)
			break;
		sscanf(b, "%x: %x %x %x %x %x %x %x %x\n",
		    &i, &r[0], &r[1], &r[2], &r[3], &r[4],
		    &r[5], &r[6], &r[7]);
		buf[i++] = r[0];
		buf[i++] = r[1];
		buf[i++] = r[2];
		buf[i++] = r[3];
		buf[i++] = r[4];
		buf[i++] = r[5];
		buf[i++] = r[6];
		buf[i++] = r[7];
	}
	fclose(fp);
}

static void
eeprom_v14_base_print(uint16_t *buf)
{
	HAL_EEPROM_v14 *eep = (HAL_EEPROM_v14 *) buf;
	BASE_EEP_HEADER *eh = &eep->ee_base.baseEepHeader;
	int i;

	printf("| Version: 0x%.4x   | Length: 0x%.4x | Checksum: 0x%.4x ",
	    eh->version, eh->length, eh->checksum);
	printf("| CapFlags: 0x%.2x\n", eh->opCapFlags);

	printf("| eepMisc: 0x%.2x | RegDomain: 0x%.4x 0x%.4x | \n",
	    eh->eepMisc, eh->regDmn[0], eh->regDmn[1]);
	printf("| MAC: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x ",
	    eh->macAddr[0], eh->macAddr[1], eh->macAddr[2],
	    eh->macAddr[3], eh->macAddr[4], eh->macAddr[5]);
	printf("| RxMask: 0x%.2x | TxMask: 0x%.2x | RfSilent: 0x%.4x | btOptions: 0x%.4x |\n",
	    eh->rxMask, eh->txMask, eh->rfSilent, eh->blueToothOptions);
	printf("| DeviceCap: 0x%.4x | binBuildNumber: %.8x | deviceType: 0x%.2x |\n",
	    eh->deviceCap, eh->binBuildNumber, eh->deviceType);

	printf("| pwdclkind: 0x%.2x | fastClk5g: 0x%.2x | divChain: 0x%.2x | rxGainType: 0x%.2x |\n",
	    (int) eh->pwdclkind, (int) eh->fastClk5g, (int) eh->divChain,
	    (int) eh->rxGainType);

	printf("| dacHiPwrMode_5G: 0x%.2x | openLoopPwrCntl: 0x%.2x | dacLpMode: 0x%.2x\n",
	    (int) eh->dacHiPwrMode_5G, (int) eh->openLoopPwrCntl, (int) eh->dacLpMode);
	printf("| txGainType: 0x%.2x | rcChainMask: 0x%.2x |\n",
	    (int) eh->txGainType, (int) eh->rcChainMask);

	printf("| desiredScaleCCK: 0x%.2x | pwr_table_offset: 0x%.2x | frac_n_5g: %.2x\n",
	    (int) eh->desiredScaleCCK, (int) eh->pwr_table_offset, (int) eh->frac_n_5g);

	/* because it's convienent */
	printf("| antennaGainMax[0]: 0x%.2x antennaGainMax[1]: 0x%.2x |\n",
	    eep->ee_antennaGainMax[0], eep->ee_antennaGainMax[1]);

	printf(" | futureBase:");
	for (i = 0; i < sizeof(eh->futureBase) / sizeof(uint8_t); i++) 
		printf(" %.2x", (int) eh->futureBase[i]);
	printf("\n");
}

static void
eeprom_v14_custdata_print(uint16_t *buf)
{
	HAL_EEPROM_v14 *eep = (HAL_EEPROM_v14 *) buf;
	uint8_t *custdata = (uint8_t *) &eep->ee_base.custData;
	int i;

	printf("\n| Custdata:                                       |\n");
	for (i = 0; i < 64; i++) {
		printf("%s0x%.2x %s",
		    i % 16 == 0 ? "| " : "",
		    custdata[i],
		    i % 16 == 15 ? "|\n" : "");
	}
}

static void
eeprom_v14_modal_print(uint16_t *buf, int m)
{
	HAL_EEPROM_v14 *eep = (HAL_EEPROM_v14 *) buf;
	MODAL_EEP_HEADER *mh = &eep->ee_base.modalHeader[m];
	int i;

	printf("| antCtrlCommon: 0x%.8x |\n", mh->antCtrlCommon);
	printf("| switchSettling: 0x%.2x |\n", mh->switchSettling);
	printf("| adcDesiredSize: %d |\n| pgaDesiredSize: %.2f dBm |\n",
	    mh->adcDesiredSize, (float) mh->pgaDesiredSize / 2.0);

	printf("| antCtrlChain:        0:0x%.8x 1:0x%.8x 2:0x%.8x |\n",
	    mh->antCtrlChain[0], mh->antCtrlChain[1], mh->antCtrlChain[2]);
	printf("| antennaGainCh:       0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->antennaGainCh[0], mh->antennaGainCh[1], mh->antennaGainCh[2]);
	printf("| txRxAttenCh:         0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->txRxAttenCh[0], mh->txRxAttenCh[1], mh->txRxAttenCh[2]);
	printf("| rxTxMarginCh:        0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->rxTxMarginCh[0], mh->rxTxMarginCh[1], mh->rxTxMarginCh[2]);
 	printf("| noiseFloorThresCh:   0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->noiseFloorThreshCh[0], mh->noiseFloorThreshCh[1], mh->noiseFloorThreshCh[2]);
	printf("| xlnaGainCh:          0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->xlnaGainCh[0], mh->xlnaGainCh[1], mh->xlnaGainCh[2]);
	printf("| iqCalICh:            0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n| iqCalQCh:            0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->iqCalICh[0], mh->iqCalICh[1], mh->iqCalICh[2],
	    mh->iqCalQCh[0], mh->iqCalQCh[1], mh->iqCalQCh[2]);
	printf("| bswAtten:            0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->bswAtten[0], mh->bswAtten[1], mh->bswAtten[2]);
	printf("| bswMargin:           0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->bswMargin[0], mh->bswMargin[1], mh->bswMargin[2]);
	printf("| xatten2Db:           0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->xatten2Db[0], mh->xatten2Db[1], mh->xatten2Db[2]);
	printf("| xatten2Margin:       0:0x%.2x   1:0x%.2x   2:0x%.2x   |\n",
	    mh->xatten2Margin[0], mh->xatten2Margin[1], mh->xatten2Margin[2]);

	printf("| txEndToXpaOff: 0x%.2x | txEndToRxOn: 0x%.2x | txFrameToXpaOn: 0x%.2x |\n",
	    mh->txEndToXpaOff, mh->txEndToRxOn, mh->txFrameToXpaOn);

	printf("| thres62: 0x%.2x\n", mh->thresh62);

	printf("| xpdGain: 0x%.2x | xpd: 0x%.2x |\n", mh->xpdGain, mh->xpd);
	printf("| xpaBiasLvlFreq: 0:0x%.4x 1:0x%.4x 2:0x%.4x |\n",
	    mh->xpaBiasLvlFreq[0], mh->xpaBiasLvlFreq[1], mh->xpaBiasLvlFreq[2]);

	printf("| pdGainOverlap: 0x%.2x | ob: 0x%.2x | db: 0x%.2x | xpaBiasLvl: 0x%.2x |\n",
	    mh->pdGainOverlap, mh->ob, mh->db, mh->xpaBiasLvl);

	printf("| pwrDecreaseFor2Chain: 0x%.2x | pwrDecreaseFor3Chain: 0x%.2x | txFrameToDataStart: 0x%.2x | txFrameToPaOn: 0x%.2x |\n",
	    mh->pwrDecreaseFor2Chain, mh->pwrDecreaseFor3Chain, mh->txFrameToDataStart,
	    mh->txFrameToPaOn);

	printf("| ht40PowerIncForPdadc: 0x%.2x |\n", mh->ht40PowerIncForPdadc);

	printf("| swSettleHt40: 0x%.2x |\n", mh->swSettleHt40);

	printf("| ob_ch1: 0x%.2x | db_ch1: 0x%.2x |\n", mh->ob_ch1, mh->db_ch1);

	printf("| flagBits: 0x%.2x | miscBits: 0x%.2x |\n", mh->flagBits, mh->miscBits);


	printf("| futureModal: 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x |\n",
	    mh->futureModal[0],
	    mh->futureModal[1],
	    mh->futureModal[2],
	    mh->futureModal[3],
	    mh->futureModal[4],
	    mh->futureModal[5]);

	/* and now, spur channels */
	for (i = 0; i < AR5416_EEPROM_MODAL_SPURS; i++) {
		printf("| Spur %d: spurChan: 0x%.4x spurRangeLow: 0x%.2x spurRangeHigh: 0x%.2x |\n",
		    i, mh->spurChans[i].spurChan,
		    (int) mh->spurChans[i].spurRangeLow,
		    (int) mh->spurChans[i].spurRangeHigh);
	}
}

static void
eeprom_v14_print_caldata_perfreq_op_loop(CAL_DATA_PER_FREQ_OP_LOOP *f)
{
	int i, j;
	for (i = 0; i < 2; i++) {
		printf("    Gain: %d:\n", i);
		for (j = 0; j < 5; j++) {
			printf("      %d: pwrPdg: %d, vpdPdg: %d, pcdac: %d, empty: %d\n",
			    j, f->pwrPdg[i][j], f->vpdPdg[i][j], f->pcdac[i][j], f->empty[i][j]);
		}
		printf("\n");
	}
}

static void
eeprom_v14_print_caldata_perfreq(CAL_DATA_PER_FREQ *f)
{
	int i, j;

	for (i = 0; i < AR5416_NUM_PD_GAINS; i++) {
		printf("    Gain %d: pwr dBm/vpd: ", i);
		for (j = 0; j < AR5416_PD_GAIN_ICEPTS; j++) {
			/* These are stored in 0.25dBm increments */
			printf("%d:(%.2f/%d) ", j, (float) f->pwrPdg[i][j] / 4.00,
			    f->vpdPdg[i][j]);
		}
		printf("\n");
	}
}

static void
eeprom_v14_calfreqpiers_print(uint16_t *buf)
{
	HAL_EEPROM_v14 *eep = (HAL_EEPROM_v14 *) buf;
	int i, n;

	/* 2ghz cal piers */
	printf("calFreqPier2G: ");
	for (i = 0; i < AR5416_NUM_2G_CAL_PIERS; i++) {
		printf(" 0x%.2x ", eep->ee_base.calFreqPier2G[i]);
	}
	printf("|\n");

	for (i = 0; i < AR5416_NUM_2G_CAL_PIERS; i++) {
		if (eep->ee_base.calFreqPier2G[i] == 0xff)
			continue;
		printf("2Ghz Cal Pier %d\n", i);
		for (n = 0; n < AR5416_MAX_CHAINS; n++) {
			printf("  Chain %d:\n", n);
			if (eep->ee_base.baseEepHeader.openLoopPwrCntl)
				eeprom_v14_print_caldata_perfreq_op_loop((void *) (&eep->ee_base.calPierData2G[n][i]));
			else
				eeprom_v14_print_caldata_perfreq(&eep->ee_base.calPierData2G[n][i]);
		}
	}

	printf("\n");

	/* 5ghz cal piers */
	printf("calFreqPier5G: ");
	for (i = 0; i < AR5416_NUM_5G_CAL_PIERS; i++) {
		printf(" 0x%.2x ", eep->ee_base.calFreqPier5G[i]);
	}
	printf("|\n");
	for (i = 0; i < AR5416_NUM_5G_CAL_PIERS; i++) {
		if (eep->ee_base.calFreqPier5G[i] == 0xff)
			continue;
		printf("5Ghz Cal Pier %d\n", i);
		for (n = 0; n < AR5416_MAX_CHAINS; n++) {
			printf("  Chain %d:\n", n);
			if (eep->ee_base.baseEepHeader.openLoopPwrCntl)
				eeprom_v14_print_caldata_perfreq_op_loop((void *) (&eep->ee_base.calPierData5G[n][i]));
			else
				eeprom_v14_print_caldata_perfreq(&eep->ee_base.calPierData5G[n][i]);
		}
	}
}

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

static void
eeprom_v14_print_targets(uint16_t *buf)
{
	HAL_EEPROM_v14 *eep = (HAL_EEPROM_v14 *) buf;
	int i;

	/* 2ghz rates */
	printf("2Ghz CCK:\n");
	for (i = 0; i < AR5416_NUM_2G_CCK_TARGET_POWERS; i++) {
		eeprom_v14_target_legacy_print(&eep->ee_base.calTargetPowerCck[i]);
	}
	printf("2Ghz 11g:\n");
	for (i = 0; i < AR5416_NUM_2G_20_TARGET_POWERS; i++) {
		eeprom_v14_target_legacy_print(&eep->ee_base.calTargetPower2G[i]);
	}
	printf("2Ghz HT20:\n");
	for (i = 0; i < AR5416_NUM_2G_20_TARGET_POWERS; i++) {
		eeprom_v14_target_ht_print(&eep->ee_base.calTargetPower2GHT20[i]);
	}
	printf("2Ghz HT40:\n");
	for (i = 0; i < AR5416_NUM_2G_40_TARGET_POWERS; i++) {
		eeprom_v14_target_ht_print(&eep->ee_base.calTargetPower2GHT40[i]);
	}

	/* 5ghz rates */
	printf("5Ghz 11a:\n");
	for (i = 0; i < AR5416_NUM_5G_20_TARGET_POWERS; i++) {
		eeprom_v14_target_legacy_print(&eep->ee_base.calTargetPower5G[i]);
	}
	printf("5Ghz HT20:\n");
	for (i = 0; i < AR5416_NUM_5G_20_TARGET_POWERS; i++) {
		eeprom_v14_target_ht_print(&eep->ee_base.calTargetPower5GHT20[i]);
	}
	printf("5Ghz HT40:\n");
	for (i = 0; i < AR5416_NUM_5G_40_TARGET_POWERS; i++) {
		eeprom_v14_target_ht_print(&eep->ee_base.calTargetPower5GHT40[i]);
	}

}

static void
eeprom_v14_ctl_edge_print(CAL_CTL_DATA *ctl)
{
	int i, j;
	uint8_t pow, flag;

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		printf("  chain %d: ", i);
		for (j = 0; j < AR5416_NUM_BAND_EDGES; j++) {
			pow = ctl->ctlEdges[i][j].tPowerFlag & 0x3f;
			flag = (ctl->ctlEdges[i][j].tPowerFlag & 0xc0) >> 6;
			printf(" %d:pow=%d,flag=%.2x", j, pow, flag);
		}
		printf("\n");
	}
}

static void
eeprom_v14_ctl_print(uint16_t *buf)
{
	HAL_EEPROM_v14 *eep = (HAL_EEPROM_v14 *) buf;
	int i;

	for (i = 0; i < AR5416_NUM_CTLS; i++) {
		if (eep->ee_base.ctlIndex[i] == 0)
			continue;
		printf("| ctlIndex: offset %d, value %d\n", i, eep->ee_base.ctlIndex[i]);
		eeprom_v14_ctl_edge_print(&eep->ee_base.ctlData[i]);
	}
}

static void
eeprom_v14_print_edges(uint16_t *buf)
{
	HAL_EEPROM_v14 *eep = (HAL_EEPROM_v14 *) buf;
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
#if 0
typedef struct {
        uint16_t        rdEdge;
	uint16_t        twice_rdEdgePower;
		HAL_BOOL        flag;
	} RD_EDGES_POWER;

#endif
}

void
usage(char *argv[])
{
	printf("Usage: %s <eeprom dump file>\n", argv[0]);
	printf("\n");
	printf("  The eeprom dump file is a text hexdump of an EEPROM.\n");
	printf("  The lines must be formatted as follows:\n");
	printf("  0xAAAA: 0xDD 0xDD 0xDD 0xDD 0xDD 0xDD 0xDD 0xDD\n");
	printf("  where each line must have exactly eight data bytes.\n");
	exit(127);
}

int
main(int argc, char *argv[])
{
	uint16_t *eep = NULL;
	eep = calloc(4096, sizeof(int16_t));

	if (argc < 2)
		usage(argv);

	load_eeprom_dump(argv[1], eep);

	eeprom_v14_base_print(eep);
	eeprom_v14_custdata_print(eep);

	/* 2.4ghz */
	printf("\n2.4ghz:\n");
	eeprom_v14_modal_print(eep, 1);
	/* 5ghz */
	printf("\n5ghz:\n");
	eeprom_v14_modal_print(eep, 0);
	printf("\n");

	eeprom_v14_calfreqpiers_print(eep);
	printf("\n");

	eeprom_v14_print_targets(eep);
	printf("\n");

	eeprom_v14_ctl_print(eep);
	printf("\n");

	eeprom_v14_print_edges(eep);
	printf("\n");

	free(eep);
	exit(0);
}
