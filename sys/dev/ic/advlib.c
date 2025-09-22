/*	$OpenBSD: advlib.c,v 1.16 2020/08/08 12:40:55 krw Exp $	*/
/*      $NetBSD: advlib.c,v 1.7 1998/10/28 20:39:46 dante Exp $        */

/*
 * Low level routines for the Advanced Systems Inc. SCSI controllers chips
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Author: Baldassare Dante Profeta <dante@mclink.it>
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Ported from:
 */
/*
 * advansys.c - Linux Host Driver for AdvanSys SCSI Adapters
 *
 * Copyright (c) 1995-1998 Advanced System Products, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <uvm/uvm_extern.h>

#include <dev/ic/adv.h>
#include <dev/ic/advlib.h>

#include <dev/microcode/adw/advmcode.h>


/* #define ASC_DEBUG */

/******************************************************************************/
/*                                Static functions                            */
/******************************************************************************/

/* Initialization routines */
static u_int32_t AscLoadMicroCode(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, u_int16_t *, u_int16_t);
static void AscInitLram(ASC_SOFTC *);
static void AscInitQLinkVar(ASC_SOFTC *);
static int AscResetChipAndScsiBus(bus_space_tag_t, bus_space_handle_t);
static u_int16_t AscGetChipBusType(bus_space_tag_t, bus_space_handle_t);

/* Chip register routines */
static void AscSetBank(bus_space_tag_t, bus_space_handle_t, u_int8_t);

/* RISC Chip routines */
static int AscStartChip(bus_space_tag_t, bus_space_handle_t);
static int AscStopChip(bus_space_tag_t, bus_space_handle_t);
static u_int8_t AscSetChipScsiID(bus_space_tag_t, bus_space_handle_t,
					u_int8_t);
static u_int8_t AscGetChipScsiCtrl(bus_space_tag_t, bus_space_handle_t);
static u_int8_t AscGetChipVersion(bus_space_tag_t, bus_space_handle_t,
					u_int16_t);
static int AscSetRunChipSynRegAtID(bus_space_tag_t, bus_space_handle_t,
					u_int8_t, u_int8_t);
static int AscSetChipSynRegAtID(bus_space_tag_t, bus_space_handle_t,
					u_int8_t, u_int8_t);
static int AscHostReqRiscHalt(bus_space_tag_t, bus_space_handle_t);
static int AscIsChipHalted(bus_space_tag_t, bus_space_handle_t);
static void AscSetChipIH(bus_space_tag_t, bus_space_handle_t, u_int16_t);

/* Lram routines */
static u_int8_t AscReadLramByte(bus_space_tag_t, bus_space_handle_t,
					u_int16_t);
static void AscWriteLramByte(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, u_int8_t);
static u_int16_t AscReadLramWord(bus_space_tag_t, bus_space_handle_t,
					u_int16_t);
static void AscWriteLramWord(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, u_int16_t);
static u_int32_t AscReadLramDWord(bus_space_tag_t, bus_space_handle_t,
					u_int16_t);
static void AscWriteLramDWord(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, u_int32_t);
static void AscMemWordSetLram(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, u_int16_t, int);
static void AscMemWordCopyToLram(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, u_int16_t *, int);
static void AscMemWordCopyFromLram(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, u_int16_t *, int);
static void AscMemDWordCopyToLram(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, u_int32_t *, int);
static u_int32_t AscMemSumLramWord(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, int);
static int AscTestExternalLram(bus_space_tag_t, bus_space_handle_t);

/* MicroCode routines */
static u_int16_t AscInitMicroCodeVar(ASC_SOFTC *);
static u_int32_t AscGetOnePhyAddr(ASC_SOFTC *, u_int8_t *, u_int32_t);
static u_int32_t AscGetSGList(ASC_SOFTC *, u_int8_t *, u_int32_t,
					ASC_SG_HEAD *);

/* EEProm routines */
static int AscWriteEEPCmdReg(bus_space_tag_t, bus_space_handle_t,
					u_int8_t);
static int AscWriteEEPDataReg(bus_space_tag_t, bus_space_handle_t,
					u_int16_t);
static void AscWaitEEPRead(void);
static void AscWaitEEPWrite(void);
static u_int16_t AscReadEEPWord(bus_space_tag_t, bus_space_handle_t,
					u_int8_t);
static u_int16_t AscWriteEEPWord(bus_space_tag_t, bus_space_handle_t,
					u_int8_t, u_int16_t);
static u_int16_t AscGetEEPConfig(bus_space_tag_t, bus_space_handle_t,
					ASCEEP_CONFIG *, u_int16_t);
static int AscSetEEPConfig(bus_space_tag_t, bus_space_handle_t,
					ASCEEP_CONFIG *, u_int16_t);
static int AscSetEEPConfigOnce(bus_space_tag_t, bus_space_handle_t,
					ASCEEP_CONFIG *, u_int16_t);
#ifdef ASC_DEBUG
static void AscPrintEEPConfig(ASCEEP_CONFIG *, u_int16_t);
#endif

/* Interrupt routines */
static void AscIsrChipHalted(ASC_SOFTC *);
static int AscIsrQDone(ASC_SOFTC *);
static int AscWaitTixISRDone(ASC_SOFTC *, u_int8_t);
static int AscWaitISRDone(ASC_SOFTC *);
static u_int8_t _AscCopyLramScsiDoneQ(bus_space_tag_t, bus_space_handle_t,
					u_int16_t, ASC_QDONE_INFO *,
					u_int32_t);
static void AscGetQDoneInfo(bus_space_tag_t, bus_space_handle_t, u_int16_t,
					ASC_QDONE_INFO *);
static void AscToggleIRQAct(bus_space_tag_t, bus_space_handle_t);
static void AscDisableInterrupt(bus_space_tag_t, bus_space_handle_t);
static void AscEnableInterrupt(bus_space_tag_t, bus_space_handle_t);
static u_int8_t AscGetChipIRQ(bus_space_tag_t, bus_space_handle_t,
					u_int16_t);
static u_int8_t AscSetChipIRQ(bus_space_tag_t, bus_space_handle_t,
					u_int8_t, u_int16_t);
static void AscAckInterrupt(bus_space_tag_t, bus_space_handle_t);
static u_int32_t AscGetMaxDmaCount(u_int16_t);
static u_int16_t AscGetIsaDmaChannel(bus_space_tag_t, bus_space_handle_t);
static u_int16_t AscSetIsaDmaChannel(bus_space_tag_t, bus_space_handle_t,
					u_int16_t);
static u_int8_t AscGetIsaDmaSpeed(bus_space_tag_t, bus_space_handle_t);
static u_int8_t AscSetIsaDmaSpeed(bus_space_tag_t, bus_space_handle_t,
					u_int8_t);

/* Messages routines */
static void AscHandleExtMsgIn(ASC_SOFTC *, u_int16_t, u_int8_t,
					ASC_SCSI_BIT_ID_TYPE, int, u_int8_t);
static u_int8_t AscMsgOutSDTR(ASC_SOFTC *, u_int8_t, u_int8_t);

/* SDTR routines */
static void AscSetChipSDTR(bus_space_tag_t, bus_space_handle_t,
					u_int8_t, u_int8_t);
static u_int8_t AscCalSDTRData(ASC_SOFTC *, u_int8_t, u_int8_t);
static u_int8_t AscGetSynPeriodIndex(ASC_SOFTC *, u_int8_t);

/* Queue routines */
static int AscSendScsiQueue(ASC_SOFTC *, ASC_SCSI_Q *, u_int8_t);
static int AscSgListToQueue(int);
static u_int AscGetNumOfFreeQueue(ASC_SOFTC *, u_int8_t, u_int8_t);
static int AscPutReadyQueue(ASC_SOFTC *, ASC_SCSI_Q *, u_int8_t);
static void AscPutSCSIQ(bus_space_tag_t, bus_space_handle_t,
					 u_int16_t, ASC_SCSI_Q *);
static int AscPutReadySgListQueue(ASC_SOFTC *, ASC_SCSI_Q *, u_int8_t);
static u_int8_t AscAllocFreeQueue(bus_space_tag_t, bus_space_handle_t,
					u_int8_t);
static u_int8_t AscAllocMultipleFreeQueue(bus_space_tag_t,
					bus_space_handle_t,
					u_int8_t, u_int8_t);
static int AscStopQueueExe(bus_space_tag_t, bus_space_handle_t);
static void AscStartQueueExe(bus_space_tag_t, bus_space_handle_t);
static void AscCleanUpBusyQueue(bus_space_tag_t, bus_space_handle_t);
static int _AscWaitQDone(bus_space_tag_t, bus_space_handle_t,
					ASC_SCSI_Q *);
static int AscCleanUpDiscQueue(bus_space_tag_t, bus_space_handle_t);

/* Abort and Reset CCB routines */
static int AscRiscHaltedAbortCCB(ASC_SOFTC *, u_int32_t);
static int AscRiscHaltedAbortTIX(ASC_SOFTC *, u_int8_t);

/* Error Handling routines */
static int AscSetLibErrorCode(ASC_SOFTC *, u_int16_t);

/* Handle bugged borads routines */
static int AscTagQueuingSafe(ASC_SCSI_INQUIRY *);
static void AscAsyncFix(ASC_SOFTC *, u_int8_t, ASC_SCSI_INQUIRY *);

/* Miscellaneous routines */
static int AscCompareString(u_char *, u_char *, int);

/* Device oriented routines */
static int DvcEnterCritical(void);
static void DvcLeaveCritical(int);
static void DvcSleepMilliSecond(u_int32_t);
//static void DvcDelayMicroSecond(u_int32_t);
static void DvcDelayNanoSecond(u_int32_t);


/******************************************************************************/
/*                            Initialization routines                         */
/******************************************************************************/

/*
 * This function perform the following steps:
 * - initialize ASC_SOFTC structure with defaults values.
 * - inquire board registers to know what kind of board it is.
 * - keep track of bugged borads.
 */
void
AscInitASC_SOFTC(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int             i;
	u_int8_t        chip_version;

	ASC_SET_CHIP_CONTROL(iot, ioh, ASC_CC_HALT);
	ASC_SET_CHIP_STATUS(iot, ioh, 0);

	sc->bug_fix_cntl = 0;
	sc->pci_fix_asyn_xfer = 0;
	sc->pci_fix_asyn_xfer_always = 0;
	sc->sdtr_done = 0;
	sc->cur_total_qng = 0;
	sc->last_q_shortage = 0;
	sc->use_tagged_qng = 0;
	sc->unit_not_ready = 0;
	sc->queue_full_or_busy = 0;
	sc->host_init_sdtr_index = 0;
	sc->can_tagged_qng = 0;
	sc->cmd_qng_enabled = 0;
	sc->dvc_cntl = ASC_DEF_DVC_CNTL;
	sc->init_sdtr = 0;
	sc->max_total_qng = ASC_DEF_MAX_TOTAL_QNG;
	sc->scsi_reset_wait = 3;
	sc->start_motor = ASC_SCSI_WIDTH_BIT_SET;
	sc->max_dma_count = AscGetMaxDmaCount(sc->bus_type);
	sc->sdtr_enable = ASC_SCSI_WIDTH_BIT_SET;
	sc->disc_enable = ASC_SCSI_WIDTH_BIT_SET;
	sc->chip_scsi_id = ASC_DEF_CHIP_SCSI_ID;
	sc->lib_serial_no = ASC_LIB_SERIAL_NUMBER;
	sc->lib_version = (ASC_LIB_VERSION_MAJOR << 8) | ASC_LIB_VERSION_MINOR;
	chip_version = AscGetChipVersion(iot, ioh, sc->bus_type);
	sc->chip_version = chip_version;
	if ((sc->bus_type & ASC_IS_PCI) &&
	    (chip_version >= ASC_CHIP_VER_PCI_ULTRA_3150)) {
		sc->bus_type = ASC_IS_PCI_ULTRA;
		sc->sdtr_period_tbl[0] = SYN_ULTRA_XFER_NS_0;
		sc->sdtr_period_tbl[1] = SYN_ULTRA_XFER_NS_1;
		sc->sdtr_period_tbl[2] = SYN_ULTRA_XFER_NS_2;
		sc->sdtr_period_tbl[3] = SYN_ULTRA_XFER_NS_3;
		sc->sdtr_period_tbl[4] = SYN_ULTRA_XFER_NS_4;
		sc->sdtr_period_tbl[5] = SYN_ULTRA_XFER_NS_5;
		sc->sdtr_period_tbl[6] = SYN_ULTRA_XFER_NS_6;
		sc->sdtr_period_tbl[7] = SYN_ULTRA_XFER_NS_7;
		sc->sdtr_period_tbl[8] = SYN_ULTRA_XFER_NS_8;
		sc->sdtr_period_tbl[9] = SYN_ULTRA_XFER_NS_9;
		sc->sdtr_period_tbl[10] = SYN_ULTRA_XFER_NS_10;
		sc->sdtr_period_tbl[11] = SYN_ULTRA_XFER_NS_11;
		sc->sdtr_period_tbl[12] = SYN_ULTRA_XFER_NS_12;
		sc->sdtr_period_tbl[13] = SYN_ULTRA_XFER_NS_13;
		sc->sdtr_period_tbl[14] = SYN_ULTRA_XFER_NS_14;
		sc->sdtr_period_tbl[15] = SYN_ULTRA_XFER_NS_15;
		sc->max_sdtr_index = 15;
		if (chip_version == ASC_CHIP_VER_PCI_ULTRA_3150)
			ASC_SET_EXTRA_CONTROL(iot, ioh,
				       (SEC_ACTIVE_NEGATE | SEC_SLEW_RATE));
		else if (chip_version >= ASC_CHIP_VER_PCI_ULTRA_3050)
			ASC_SET_EXTRA_CONTROL(iot, ioh,
				   (SEC_ACTIVE_NEGATE | SEC_ENABLE_FILTER));
	} else {
		sc->sdtr_period_tbl[0] = SYN_XFER_NS_0;
		sc->sdtr_period_tbl[1] = SYN_XFER_NS_1;
		sc->sdtr_period_tbl[2] = SYN_XFER_NS_2;
		sc->sdtr_period_tbl[3] = SYN_XFER_NS_3;
		sc->sdtr_period_tbl[4] = SYN_XFER_NS_4;
		sc->sdtr_period_tbl[5] = SYN_XFER_NS_5;
		sc->sdtr_period_tbl[6] = SYN_XFER_NS_6;
		sc->sdtr_period_tbl[7] = SYN_XFER_NS_7;
		sc->max_sdtr_index = 7;
	}

	if (sc->bus_type == ASC_IS_PCI)
		ASC_SET_EXTRA_CONTROL(iot, ioh,
				      (SEC_ACTIVE_NEGATE | SEC_SLEW_RATE));

	sc->isa_dma_speed = ASC_DEF_ISA_DMA_SPEED;
	if (AscGetChipBusType(iot, ioh) == ASC_IS_ISAPNP) {
		ASC_SET_CHIP_IFC(iot, ioh, ASC_IFC_INIT_DEFAULT);
		sc->bus_type = ASC_IS_ISAPNP;
	}
	if ((sc->bus_type & ASC_IS_ISA) != 0)
		sc->isa_dma_channel = AscGetIsaDmaChannel(iot, ioh);

	for (i = 0; i <= ASC_MAX_TID; i++) {
		sc->cur_dvc_qng[i] = 0;
		sc->max_dvc_qng[i] = ASC_MAX_SCSI1_QNG;
		sc->max_tag_qng[i] = ASC_MAX_INRAM_TAG_QNG;
	}
}


/*
 * This function initialize some ASC_SOFTC fields with values read from
 * on-board EEProm.
 */
u_int16_t
AscInitFromEEP(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	ASCEEP_CONFIG   eep_config_buf;
	ASCEEP_CONFIG  *eep_config;
	u_int16_t       chksum;
	u_int16_t       warn_code;
	u_int16_t       cfg_msw, cfg_lsw;
	int             i;
	int             write_eep = 0;

	warn_code = 0;
	AscWriteLramWord(iot, ioh, ASCV_HALTCODE_W, 0x00FE);
	AscStopQueueExe(iot, ioh);
	if ((AscStopChip(iot, ioh) == FALSE) ||
	    (AscGetChipScsiCtrl(iot, ioh) != 0)) {
		AscResetChipAndScsiBus(iot, ioh);
		DvcSleepMilliSecond(sc->scsi_reset_wait * 1000);
	}
	if (AscIsChipHalted(iot, ioh) == FALSE)
		return (-1);

	ASC_SET_PC_ADDR(iot, ioh, ASC_MCODE_START_ADDR);
	if (ASC_GET_PC_ADDR(iot, ioh) != ASC_MCODE_START_ADDR)
		return (-2);

	eep_config = &eep_config_buf;
	cfg_msw = ASC_GET_CHIP_CFG_MSW(iot, ioh);
	cfg_lsw = ASC_GET_CHIP_CFG_LSW(iot, ioh);
	if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
		cfg_msw &= (~(ASC_CFG_MSW_CLR_MASK));
		warn_code |= ASC_WARN_CFG_MSW_RECOVER;
		ASC_SET_CHIP_CFG_MSW(iot, ioh, cfg_msw);
	}
	chksum = AscGetEEPConfig(iot, ioh, eep_config, sc->bus_type);
#ifdef ASC_DEBUG
	AscPrintEEPConfig(eep_config, chksum);
#endif
	if (chksum == 0)
		chksum = 0xAA55;

	if (ASC_GET_CHIP_STATUS(iot, ioh) & ASC_CSW_AUTO_CONFIG) {
		warn_code |= ASC_WARN_AUTO_CONFIG;
		if (sc->chip_version == 3) {
			if (eep_config->cfg_lsw != cfg_lsw) {
				warn_code |= ASC_WARN_EEPROM_RECOVER;
				eep_config->cfg_lsw = ASC_GET_CHIP_CFG_LSW(iot, ioh);
			}
			if (eep_config->cfg_msw != cfg_msw) {
				warn_code |= ASC_WARN_EEPROM_RECOVER;
				eep_config->cfg_msw = ASC_GET_CHIP_CFG_MSW(iot, ioh);
			}
		}
	}
	eep_config->cfg_msw &= ~ASC_CFG_MSW_CLR_MASK;
	eep_config->cfg_lsw |= ASC_CFG0_HOST_INT_ON;

	if (chksum != eep_config->chksum) {
		if (AscGetChipVersion(iot, ioh, sc->bus_type) ==
		    ASC_CHIP_VER_PCI_ULTRA_3050) {
			eep_config->init_sdtr = 0xFF;
			eep_config->disc_enable = 0xFF;
			eep_config->start_motor = 0xFF;
			eep_config->use_cmd_qng = 0;
			eep_config->max_total_qng = 0xF0;
			eep_config->max_tag_qng = 0x20;
			eep_config->cntl = 0xBFFF;
			eep_config->chip_scsi_id = 7;
			eep_config->no_scam = 0;
			eep_config->adapter_info[0] = 0;
			eep_config->adapter_info[1] = 0;
			eep_config->adapter_info[2] = 0;
			eep_config->adapter_info[3] = 0;
#if BYTE_ORDER == BIG_ENDIAN
			eep_config->adapter_info[5] = 0;
			/* Indicate EEPROM-less board. */
			eep_config->adapter_info[4] = 0xBB;
#else
			eep_config->adapter_info[4] = 0;
			/* Indicate EEPROM-less board. */
			eep_config->adapter_info[5] = 0xBB;
#endif
		} else {
			write_eep = 1;
			warn_code |= ASC_WARN_EEPROM_CHKSUM;
		}
	}
	sc->sdtr_enable = eep_config->init_sdtr;
	sc->disc_enable = eep_config->disc_enable;
	sc->cmd_qng_enabled = eep_config->use_cmd_qng;
	sc->isa_dma_speed = eep_config->isa_dma_speed;
	sc->start_motor = eep_config->start_motor;
	sc->dvc_cntl = eep_config->cntl;
#if BYTE_ORDER == BIG_ENDIAN
	sc->adapter_info[0] = eep_config->adapter_info[1];
	sc->adapter_info[1] = eep_config->adapter_info[0];
	sc->adapter_info[2] = eep_config->adapter_info[3];
	sc->adapter_info[3] = eep_config->adapter_info[2];
	sc->adapter_info[4] = eep_config->adapter_info[5];
	sc->adapter_info[5] = eep_config->adapter_info[4];
#else
	sc->adapter_info[0] = eep_config->adapter_info[0];
	sc->adapter_info[1] = eep_config->adapter_info[1];
	sc->adapter_info[2] = eep_config->adapter_info[2];
	sc->adapter_info[3] = eep_config->adapter_info[3];
	sc->adapter_info[4] = eep_config->adapter_info[4];
	sc->adapter_info[5] = eep_config->adapter_info[5];
#endif

	if (!AscTestExternalLram(iot, ioh)) {
		if (((sc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA)) {
			eep_config->max_total_qng = ASC_MAX_PCI_ULTRA_INRAM_TOTAL_QNG;
			eep_config->max_tag_qng = ASC_MAX_PCI_ULTRA_INRAM_TAG_QNG;
		} else {
			eep_config->cfg_msw |= 0x0800;
			cfg_msw |= 0x0800;
			ASC_SET_CHIP_CFG_MSW(iot, ioh, cfg_msw);
			eep_config->max_total_qng = ASC_MAX_PCI_INRAM_TOTAL_QNG;
			eep_config->max_tag_qng = ASC_MAX_INRAM_TAG_QNG;
		}
	}
	if (eep_config->max_total_qng < ASC_MIN_TOTAL_QNG)
		eep_config->max_total_qng = ASC_MIN_TOTAL_QNG;

	if (eep_config->max_total_qng > ASC_MAX_TOTAL_QNG)
		eep_config->max_total_qng = ASC_MAX_TOTAL_QNG;

	if (eep_config->max_tag_qng > eep_config->max_total_qng)
		eep_config->max_tag_qng = eep_config->max_total_qng;

	if (eep_config->max_tag_qng < ASC_MIN_TAG_Q_PER_DVC)
		eep_config->max_tag_qng = ASC_MIN_TAG_Q_PER_DVC;

	sc->max_total_qng = eep_config->max_total_qng;
	if ((eep_config->use_cmd_qng & eep_config->disc_enable) !=
	    eep_config->use_cmd_qng) {
		eep_config->disc_enable = eep_config->use_cmd_qng;
		warn_code |= ASC_WARN_CMD_QNG_CONFLICT;
	}
	if (sc->bus_type & (ASC_IS_ISA | ASC_IS_VL | ASC_IS_EISA))
		sc->irq_no = AscGetChipIRQ(iot, ioh, sc->bus_type);

	eep_config->chip_scsi_id &= ASC_MAX_TID;
	sc->chip_scsi_id = eep_config->chip_scsi_id;
	if (((sc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA) &&
	    !(sc->dvc_cntl & ASC_CNTL_SDTR_ENABLE_ULTRA)) {
		sc->host_init_sdtr_index = ASC_SDTR_ULTRA_PCI_10MB_INDEX;
	}
	for (i = 0; i <= ASC_MAX_TID; i++) {
		sc->max_tag_qng[i] = eep_config->max_tag_qng;
		sc->sdtr_period_offset[i] = ASC_DEF_SDTR_OFFSET |
			(sc->host_init_sdtr_index << 4);
	}

	eep_config->cfg_msw = ASC_GET_CHIP_CFG_MSW(iot, ioh);
	if (write_eep) {
		AscSetEEPConfig(iot, ioh, eep_config, sc->bus_type);
#ifdef ASC_DEBUG
		AscPrintEEPConfig(eep_config, 0);
#endif
	}

	return (warn_code);
}


u_int16_t
AscInitFromASC_SOFTC(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t       cfg_msw;
	u_int16_t       warn_code;
	u_int16_t       pci_device_id = sc->pci_device_id;

	warn_code = 0;
	cfg_msw = ASC_GET_CHIP_CFG_MSW(iot, ioh);

	if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
		cfg_msw &= (~(ASC_CFG_MSW_CLR_MASK));
		warn_code |= ASC_WARN_CFG_MSW_RECOVER;
		ASC_SET_CHIP_CFG_MSW(iot, ioh, cfg_msw);
	}
	if ((sc->cmd_qng_enabled & sc->disc_enable) != sc->cmd_qng_enabled) {
		sc->disc_enable = sc->cmd_qng_enabled;
		warn_code |= ASC_WARN_CMD_QNG_CONFLICT;
	}
	if (ASC_GET_CHIP_STATUS(iot, ioh) & ASC_CSW_AUTO_CONFIG) {
		warn_code |= ASC_WARN_AUTO_CONFIG;
	}
	if ((sc->bus_type & (ASC_IS_ISA | ASC_IS_VL)) != 0) {
		AscSetChipIRQ(iot, ioh, sc->irq_no, sc->bus_type);
	}
	if (sc->bus_type & ASC_IS_PCI) {
		cfg_msw &= 0xFFC0;
		ASC_SET_CHIP_CFG_MSW(iot, ioh, cfg_msw);

		if ((sc->bus_type & ASC_IS_PCI_ULTRA) != ASC_IS_PCI_ULTRA) {
			if ((pci_device_id == ASC_PCI_DEVICE_ID_REV_A) ||
			    (pci_device_id == ASC_PCI_DEVICE_ID_REV_B)) {
				sc->bug_fix_cntl |= ASC_BUG_FIX_IF_NOT_DWB;
				sc->bug_fix_cntl |= ASC_BUG_FIX_ASYN_USE_SYN;
			}
		}
	} else if (sc->bus_type == ASC_IS_ISAPNP) {
		if (AscGetChipVersion(iot, ioh, sc->bus_type) ==
		    ASC_CHIP_VER_ASYN_BUG) {
			sc->bug_fix_cntl |= ASC_BUG_FIX_ASYN_USE_SYN;
		}
	}
	AscSetChipScsiID(iot, ioh, sc->chip_scsi_id);

	if (sc->bus_type & ASC_IS_ISA) {
		AscSetIsaDmaChannel(iot, ioh, sc->isa_dma_channel);
		AscSetIsaDmaSpeed(iot, ioh, sc->isa_dma_speed);
	}
	return (warn_code);
}


/*
 * - Initialize RISC chip
 * - Initialize Lram
 * - Load uCode into Lram
 * - Enable Interrupts
 */
int
AscInitDriver(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t       chksum;

	if (!AscFindSignature(iot, ioh))
		return (1);

	AscDisableInterrupt(iot, ioh);

	AscInitLram(sc);
	chksum = AscLoadMicroCode(iot, ioh, 0, (u_int16_t *) asc_mcode,
				  asc_mcode_size);
	if (chksum != asc_mcode_chksum)
		return (2);

	if (AscInitMicroCodeVar(sc) == 0)
		return (3);

	AscEnableInterrupt(iot, ioh);

	return (0);
}


int
AscFindSignature(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int16_t       sig_word;

	if (ASC_GET_CHIP_SIGNATURE_BYTE(iot, ioh) == ASC_1000_ID1B) {
		sig_word = ASC_GET_CHIP_SIGNATURE_WORD(iot, ioh);
		if (sig_word == ASC_1000_ID0W ||
		    sig_word == ASC_1000_ID0W_FIX)
			return (1);
	}
	return (0);
}


static void
AscInitLram(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t        i;
	u_int16_t       s_addr;

	AscMemWordSetLram(iot, ioh, ASC_QADR_BEG, 0,
			  (((sc->max_total_qng + 2 + 1) * 64) >> 1));

	i = ASC_MIN_ACTIVE_QNO;
	s_addr = ASC_QADR_BEG + ASC_QBLK_SIZE;
	AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_FWD, i + 1);
	AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_BWD, sc->max_total_qng);
	AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_QNO, i);
	i++;
	s_addr += ASC_QBLK_SIZE;
	for (; i < sc->max_total_qng; i++, s_addr += ASC_QBLK_SIZE) {
		AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_FWD, i + 1);
		AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_BWD, i - 1);
		AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_QNO, i);
	}
	AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_FWD, ASC_QLINK_END);
	AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_BWD, sc->max_total_qng - 1);
	AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_QNO, sc->max_total_qng);
	i++;
	s_addr += ASC_QBLK_SIZE;
	for (; i <= (u_int8_t) (sc->max_total_qng + 3); i++, s_addr += ASC_QBLK_SIZE) {
		AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_FWD, i);
		AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_BWD, i);
		AscWriteLramByte(iot, ioh, s_addr + ASC_SCSIQ_B_QNO, i);
	}
}


void
AscReInitLram(ASC_SOFTC *sc)
{
	AscInitLram(sc);
	AscInitQLinkVar(sc);
}


static void
AscInitQLinkVar(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t        i;
	u_int16_t       lram_addr;

	ASC_PUT_RISC_VAR_FREE_QHEAD(iot, ioh, 1);
	ASC_PUT_RISC_VAR_DONE_QTAIL(iot, ioh, sc->max_total_qng);
	ASC_PUT_VAR_FREE_QHEAD(iot, ioh, 1);
	ASC_PUT_VAR_DONE_QTAIL(iot, ioh, sc->max_total_qng);
	AscWriteLramByte(iot, ioh, ASCV_BUSY_QHEAD_B, sc->max_total_qng + 1);
	AscWriteLramByte(iot, ioh, ASCV_DISC1_QHEAD_B, sc->max_total_qng + 2);
	AscWriteLramByte(iot, ioh, ASCV_TOTAL_READY_Q_B, sc->max_total_qng);
	AscWriteLramWord(iot, ioh, ASCV_ASCDVC_ERR_CODE_W, 0);
	AscWriteLramWord(iot, ioh, ASCV_HALTCODE_W, 0);
	AscWriteLramByte(iot, ioh, ASCV_STOP_CODE_B, 0);
	AscWriteLramByte(iot, ioh, ASCV_SCSIBUSY_B, 0);
	AscWriteLramByte(iot, ioh, ASCV_WTM_FLAG_B, 0);
	ASC_PUT_QDONE_IN_PROGRESS(iot, ioh, 0);
	lram_addr = ASC_QADR_BEG;
	for (i = 0; i < 32; i++, lram_addr += 2)
		AscWriteLramWord(iot, ioh, lram_addr, 0);
}


static int
AscResetChipAndScsiBus(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	while (ASC_GET_CHIP_STATUS(iot, ioh) & ASC_CSW_SCSI_RESET_ACTIVE);

	AscStopChip(iot, ioh);
	ASC_SET_CHIP_CONTROL(iot, ioh, ASC_CC_CHIP_RESET | ASC_CC_SCSI_RESET | ASC_CC_HALT);

	DvcDelayNanoSecond(60000);

	AscSetChipIH(iot, ioh, ASC_INS_RFLAG_WTM);
	AscSetChipIH(iot, ioh, ASC_INS_HALT);
	ASC_SET_CHIP_CONTROL(iot, ioh, ASC_CC_CHIP_RESET | ASC_CC_HALT);
	ASC_SET_CHIP_CONTROL(iot, ioh, ASC_CC_HALT);

	DvcSleepMilliSecond(200);

	ASC_SET_CHIP_STATUS(iot, ioh, ASC_CIW_CLR_SCSI_RESET_INT);
	AscStartChip(iot, ioh);

	DvcSleepMilliSecond(200);

	return (AscIsChipHalted(iot, ioh));
}


static u_int16_t
AscGetChipBusType(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int16_t       chip_ver;

	chip_ver = ASC_GET_CHIP_VER_NO(iot, ioh);
	if ((chip_ver >= ASC_CHIP_MIN_VER_VL) &&
	    (chip_ver <= ASC_CHIP_MAX_VER_VL)) {
		/*
		 * if(((iop_base & 0x0C30) == 0x0C30) || ((iop_base & 0x0C50)
		 * == 0x0C50)) return (ASC_IS_EISA);
		 */
		return (ASC_IS_VL);
	}
	if ((chip_ver >= ASC_CHIP_MIN_VER_ISA) &&
	    (chip_ver <= ASC_CHIP_MAX_VER_ISA)) {
		if (chip_ver >= ASC_CHIP_MIN_VER_ISA_PNP)
			return (ASC_IS_ISAPNP);

		return (ASC_IS_ISA);
	} else if ((chip_ver >= ASC_CHIP_MIN_VER_PCI) &&
		   (chip_ver <= ASC_CHIP_MAX_VER_PCI))
		return (ASC_IS_PCI);

	return (0);
}


/******************************************************************************/
/*                             Chip register routines                         */
/******************************************************************************/


static void
AscSetBank(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t bank)
{
	u_int8_t        val;

	val = ASC_GET_CHIP_CONTROL(iot, ioh) &
		(~(ASC_CC_SINGLE_STEP | ASC_CC_TEST |
		   ASC_CC_DIAG | ASC_CC_SCSI_RESET |
		   ASC_CC_CHIP_RESET));

	switch (bank) {
	case 1:
		val |= ASC_CC_BANK_ONE;
		break;

	case 2:
		val |= ASC_CC_DIAG | ASC_CC_BANK_ONE;
		break;

	default:
		val &= ~ASC_CC_BANK_ONE;
	}

	ASC_SET_CHIP_CONTROL(iot, ioh, val);
	return;
}


/******************************************************************************/
/*                                 Chip routines                              */
/******************************************************************************/


static int
AscStartChip(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	ASC_SET_CHIP_CONTROL(iot, ioh, 0);
	if ((ASC_GET_CHIP_STATUS(iot, ioh) & ASC_CSW_HALTED) != 0)
		return (0);

	return (1);
}


static int
AscStopChip(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int8_t        cc_val;

	cc_val = ASC_GET_CHIP_CONTROL(iot, ioh) &
		(~(ASC_CC_SINGLE_STEP | ASC_CC_TEST | ASC_CC_DIAG));
	ASC_SET_CHIP_CONTROL(iot, ioh, cc_val | ASC_CC_HALT);
	AscSetChipIH(iot, ioh, ASC_INS_HALT);
	AscSetChipIH(iot, ioh, ASC_INS_RFLAG_WTM);
	if ((ASC_GET_CHIP_STATUS(iot, ioh) & ASC_CSW_HALTED) == 0)
		return (0);

	return (1);
}


static u_int8_t
AscGetChipVersion(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int16_t bus_type)
{
	if (bus_type & ASC_IS_EISA) {
		/*
		 * u_int16_t	eisa_iop; u_int8_t	revision;
		 *
		 * eisa_iop = ASC_GET_EISA_SLOT(iop_base) |
		 * ASC_EISA_REV_IOP_MASK; revision = inp(eisa_iop);
		 * return((ASC_CHIP_MIN_VER_EISA - 1) + revision);
		 */
	}
	return (ASC_GET_CHIP_VER_NO(iot, ioh));
}


static u_int8_t
AscSetChipScsiID(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t new_id)
{
	u_int16_t       cfg_lsw;

	if (ASC_GET_CHIP_SCSI_ID(iot, ioh) == new_id)
		return (new_id);

	cfg_lsw = ASC_GET_CHIP_SCSI_ID(iot, ioh);
	cfg_lsw &= 0xF8FF;
	cfg_lsw |= (new_id & ASC_MAX_TID) << 8;
	ASC_SET_CHIP_CFG_LSW(iot, ioh, cfg_lsw);
	return (ASC_GET_CHIP_SCSI_ID(iot, ioh));
}


static u_int8_t
AscGetChipScsiCtrl(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int8_t        scsi_ctrl;

	AscSetBank(iot, ioh, 1);
	scsi_ctrl = bus_space_read_1(iot, ioh, ASC_IOP_REG_SC);
	AscSetBank(iot, ioh, 0);
	return (scsi_ctrl);
}


static int
AscSetRunChipSynRegAtID(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t tid_no, u_int8_t sdtr_data)
{
	int             retval = FALSE;

	if (AscHostReqRiscHalt(iot, ioh)) {
		retval = AscSetChipSynRegAtID(iot, ioh, tid_no, sdtr_data);
		AscStartChip(iot, ioh);
	}
	return (retval);
}


static int
AscSetChipSynRegAtID(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t id,
    u_int8_t sdtr_data)
{
	ASC_SCSI_BIT_ID_TYPE org_id;
	int             i;
	int             sta = TRUE;

	AscSetBank(iot, ioh, 1);
	org_id = ASC_READ_CHIP_DVC_ID(iot, ioh);
	for (i = 0; i <= ASC_MAX_TID; i++)
		if (org_id == (0x01 << i))
			break;

	org_id = i;
	ASC_WRITE_CHIP_DVC_ID(iot, ioh, id);
	if (ASC_READ_CHIP_DVC_ID(iot, ioh) == (0x01 << id)) {
		AscSetBank(iot, ioh, 0);
		ASC_SET_CHIP_SYN(iot, ioh, sdtr_data);
		if (ASC_GET_CHIP_SYN(iot, ioh) != sdtr_data)
			sta = FALSE;
	} else
		sta = FALSE;

	AscSetBank(iot, ioh, 1);
	ASC_WRITE_CHIP_DVC_ID(iot, ioh, org_id);
	AscSetBank(iot, ioh, 0);
	return (sta);
}


static int
AscHostReqRiscHalt(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int             count = 0;
	int             retval = 0;
	u_int8_t        saved_stop_code;

	if (AscIsChipHalted(iot, ioh))
		return (1);
	saved_stop_code = AscReadLramByte(iot, ioh, ASCV_STOP_CODE_B);
	AscWriteLramByte(iot, ioh, ASCV_STOP_CODE_B,
		      ASC_STOP_HOST_REQ_RISC_HALT | ASC_STOP_REQ_RISC_STOP);

	do {
		if (AscIsChipHalted(iot, ioh)) {
			retval = 1;
			break;
		}
		DvcSleepMilliSecond(100);
	} while (count++ < 20);

	AscWriteLramByte(iot, ioh, ASCV_STOP_CODE_B, saved_stop_code);

	return (retval);
}


static int
AscIsChipHalted(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	if ((ASC_GET_CHIP_STATUS(iot, ioh) & ASC_CSW_HALTED) != 0)
		if ((ASC_GET_CHIP_CONTROL(iot, ioh) & ASC_CC_HALT) != 0)
			return (1);

	return (0);
}


static void
AscSetChipIH(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t ins_code)
{
	AscSetBank(iot, ioh, 1);
	ASC_WRITE_CHIP_IH(iot, ioh, ins_code);
	AscSetBank(iot, ioh, 0);

	return;
}


/******************************************************************************/
/*                                 Lram routines                              */
/******************************************************************************/


static u_int8_t
AscReadLramByte(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t addr)
{
	u_int8_t        byte_data;
	u_int16_t       word_data;

	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, addr & 0xFFFE);
	word_data = ASC_GET_CHIP_LRAM_DATA(iot, ioh);

	if (addr & 1) {
		/* odd address */
		byte_data = (u_int8_t) ((word_data >> 8) & 0xFF);
	} else {
		/* even address */
		byte_data = (u_int8_t) (word_data & 0xFF);
	}

	return (byte_data);
}


static void
AscWriteLramByte(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t addr,
    u_int8_t data)
{
	u_int16_t       word_data;

	word_data = AscReadLramWord(iot, ioh, addr & 0xFFFE);

	if (addr & 1) {
		/* odd address */
		word_data &= 0x00FF;
		word_data |= (((u_int16_t) data) << 8) & 0xFF00;
	} else {
		/* even address */
		word_data &= 0xFF00;
		word_data |= ((u_int16_t) data) & 0x00FF;
	}

	AscWriteLramWord(iot, ioh, addr, word_data);
}


static u_int16_t
AscReadLramWord(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t addr)
{
	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, addr);
	return (ASC_GET_CHIP_LRAM_DATA(iot, ioh));
}


static void
AscWriteLramWord(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t addr,
    u_int16_t data)
{
	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, addr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, data);
}


static u_int32_t
AscReadLramDWord(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t addr)
{
	u_int16_t       low_word, hi_word;

	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, addr);
	low_word = ASC_GET_CHIP_LRAM_DATA(iot, ioh);
	hi_word = ASC_GET_CHIP_LRAM_DATA(iot, ioh);

	return ((((u_int32_t) hi_word) << 16) | (u_int32_t) low_word);
}


static void
AscWriteLramDWord(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t addr,
    u_int32_t data)
{
	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, addr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, (u_int16_t) (data & 0x0000FFFF));
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, (u_int16_t) (data >> 16));
}


static void
AscMemWordSetLram(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t s_addr,
    u_int16_t s_words, int count)
{
	int             i;

	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, s_addr);
	for (i = 0; i < count; i++)
		ASC_SET_CHIP_LRAM_DATA(iot, ioh, s_words);
}


static void
AscMemWordCopyToLram(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int16_t s_addr, u_int16_t *s_buffer, int words)
{
	int             i;

	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, s_addr);
	for (i = 0; i < words; i++, s_buffer++)
		ASC_SET_CHIP_LRAM_DATA_NO_SWAP(iot, ioh, *s_buffer);
}


static void
AscMemWordCopyFromLram(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int16_t s_addr, u_int16_t *s_buffer, int words)
{
	int             i;

	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, s_addr);
	for (i = 0; i < words; i++, s_buffer++)
		*s_buffer = ASC_GET_CHIP_LRAM_DATA_NO_SWAP(iot, ioh);
}


static void
AscMemDWordCopyToLram(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int16_t s_addr, u_int32_t *s_buffer, int dwords)
{
	int             i;
	u_int32_t      *pw;

	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, s_addr);

	pw = s_buffer;
	for (i = 0; i < dwords; i++, pw++) {
		ASC_SET_CHIP_LRAM_DATA(iot, ioh, LO_WORD(*pw));
		DELAY(1);
		ASC_SET_CHIP_LRAM_DATA(iot, ioh, HI_WORD(*pw));
	}
}


static u_int32_t
AscMemSumLramWord(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t s_addr,
    int words)
{
	u_int32_t       sum = 0L;
	u_int16_t       i;

	for (i = 0; i < words; i++, s_addr += 2)
		sum += AscReadLramWord(iot, ioh, s_addr);

	return (sum);
}


static int
AscTestExternalLram(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int16_t       q_addr;
	u_int16_t       saved_word;
	int             retval;

	retval = 0;
	q_addr = ASC_QNO_TO_QADDR(241);
	saved_word = AscReadLramWord(iot, ioh, q_addr);
	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, q_addr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, 0x55AA);
	DvcSleepMilliSecond(10);
	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, q_addr);

	if (ASC_GET_CHIP_LRAM_DATA(iot, ioh) == 0x55AA) {
		retval = 1;
		AscWriteLramWord(iot, ioh, q_addr, saved_word);
	}
	return (retval);
}


/******************************************************************************/
/*                               MicroCode routines                           */
/******************************************************************************/


static u_int16_t
AscInitMicroCodeVar(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t       phy_addr;
	int             i;

	for (i = 0; i <= ASC_MAX_TID; i++)
		ASC_PUT_MCODE_INIT_SDTR_AT_ID(iot, ioh, i,
					      sc->sdtr_period_offset[i]);

	AscInitQLinkVar(sc);
	AscWriteLramByte(iot, ioh, ASCV_DISC_ENABLE_B, sc->disc_enable);
	AscWriteLramByte(iot, ioh, ASCV_HOSTSCSI_ID_B,
			 ASC_TID_TO_TARGET_ID(sc->chip_scsi_id));

	if ((phy_addr = AscGetOnePhyAddr(sc, sc->overrun_buf,
					 ASC_OVERRUN_BSIZE)) == 0L) {
		return (0);
	} else {
		phy_addr = (phy_addr & 0xFFFFFFF8ul) + 8;
		AscWriteLramDWord(iot, ioh, ASCV_OVERRUN_PADDR_D, phy_addr);
		AscWriteLramDWord(iot, ioh, ASCV_OVERRUN_BSIZE_D,
				  ASC_OVERRUN_BSIZE - 8);
	}

	sc->mcode_date = AscReadLramWord(iot, ioh, ASCV_MC_DATE_W);
	sc->mcode_version = AscReadLramWord(iot, ioh, ASCV_MC_VER_W);
	ASC_SET_PC_ADDR(iot, ioh, ASC_MCODE_START_ADDR);

	if (ASC_GET_PC_ADDR(iot, ioh) != ASC_MCODE_START_ADDR) {
		return (0);
	}
	if (AscStartChip(iot, ioh) != 1) {
		return (0);
	}
	return (1);
}


static u_int32_t
AscLoadMicroCode(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t s_addr,
    u_int16_t *mcode_buf, u_int16_t mcode_size)
{
	u_int32_t       chksum;
	u_int16_t       mcode_word_size;
	u_int16_t       mcode_chksum;

	mcode_word_size = mcode_size >> 1;
	/* clear board memory */
	AscMemWordSetLram(iot, ioh, s_addr, 0, mcode_word_size);
	/* copy uCode to board memory */
	AscMemWordCopyToLram(iot, ioh, s_addr, mcode_buf, mcode_word_size);
	chksum = AscMemSumLramWord(iot, ioh, s_addr, mcode_word_size);
	mcode_chksum = AscMemSumLramWord(iot, ioh, ASC_CODE_SEC_BEG,
			   ((mcode_size - s_addr - ASC_CODE_SEC_BEG) >> 1));
	AscWriteLramWord(iot, ioh, ASCV_MCODE_CHKSUM_W, mcode_chksum);
	AscWriteLramWord(iot, ioh, ASCV_MCODE_SIZE_W, mcode_size);

	return (chksum);
}


static u_int32_t
AscGetOnePhyAddr(ASC_SOFTC *sc, u_int8_t *buf_addr, u_int32_t buf_size)
{
	ASC_MIN_SG_HEAD sg_head;

	sg_head.entry_cnt = ASC_MIN_SG_LIST;
	if (AscGetSGList(sc, buf_addr, buf_size, (ASC_SG_HEAD *) & sg_head) !=
	    buf_size) {
		return (0L);
	}
	if (sg_head.entry_cnt > 1) {
		return (0L);
	}
	return (sg_head.sg_list[0].addr);
}


static u_int32_t
AscGetSGList(ASC_SOFTC *sc, u_int8_t *buf_addr, u_int32_t buf_len,
    ASC_SG_HEAD *asc_sg_head_ptr)
{
	u_int32_t       buf_size;

	buf_size = buf_len;
	asc_sg_head_ptr->entry_cnt = 1;
	asc_sg_head_ptr->sg_list[0].addr = (u_int32_t) buf_addr;
	asc_sg_head_ptr->sg_list[0].bytes = buf_size;

	return (buf_size);
}


/******************************************************************************/
/*                                 EEProm routines                            */
/******************************************************************************/


static int
AscWriteEEPCmdReg(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t cmd_reg)
{
	u_int8_t        read_back;
	int             retry;

	retry = 0;

	while (TRUE) {
		ASC_SET_CHIP_EEP_CMD(iot, ioh, cmd_reg);
		DvcSleepMilliSecond(1);
		read_back = ASC_GET_CHIP_EEP_CMD(iot, ioh);
		if (read_back == cmd_reg)
			return (1);

		if (retry++ > ASC_EEP_MAX_RETRY)
			return (0);
	}
}


static int
AscWriteEEPDataReg(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int16_t data_reg)
{
	u_int16_t       read_back;
	int             retry;

	retry = 0;
	while (TRUE) {
		ASC_SET_CHIP_EEP_DATA(iot, ioh, data_reg);
		DvcSleepMilliSecond(1);
		read_back = ASC_GET_CHIP_EEP_DATA(iot, ioh);
		if (read_back == data_reg)
			return (1);

		if (retry++ > ASC_EEP_MAX_RETRY)
			return (0);
	}
}


static void
AscWaitEEPRead(void)
{
	DvcSleepMilliSecond(1);
}


static void
AscWaitEEPWrite(void)
{
	DvcSleepMilliSecond(1);
}


static u_int16_t
AscReadEEPWord(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t addr)
{
	u_int16_t       read_wval;
	u_int8_t        cmd_reg;

	AscWriteEEPCmdReg(iot, ioh, ASC_EEP_CMD_WRITE_DISABLE);
	AscWaitEEPRead();
	cmd_reg = addr | ASC_EEP_CMD_READ;
	AscWriteEEPCmdReg(iot, ioh, cmd_reg);
	AscWaitEEPRead();
	read_wval = ASC_GET_CHIP_EEP_DATA(iot, ioh);
	AscWaitEEPRead();

	return (read_wval);
}


static u_int16_t
AscWriteEEPWord(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t addr,
    u_int16_t word_val)
{
	u_int16_t       read_wval;

	read_wval = AscReadEEPWord(iot, ioh, addr);
	if (read_wval != word_val) {
		AscWriteEEPCmdReg(iot, ioh, ASC_EEP_CMD_WRITE_ABLE);
		AscWaitEEPRead();
		AscWriteEEPDataReg(iot, ioh, word_val);
		AscWaitEEPRead();
		AscWriteEEPCmdReg(iot, ioh, ASC_EEP_CMD_WRITE | addr);
		AscWaitEEPWrite();
		AscWriteEEPCmdReg(iot, ioh, ASC_EEP_CMD_WRITE_DISABLE);
		AscWaitEEPRead();
		return (AscReadEEPWord(iot, ioh, addr));
	}
	return (read_wval);
}


static u_int16_t
AscGetEEPConfig(bus_space_tag_t iot, bus_space_handle_t ioh,
    ASCEEP_CONFIG *cfg_buf, u_int16_t bus_type)
{
	u_int16_t       wval;
	u_int16_t       sum;
	u_int16_t      *wbuf;
	int             cfg_beg;
	int             cfg_end;
	int             s_addr;
	int             isa_pnp_wsize;

	wbuf = (u_int16_t *) cfg_buf;
	sum = 0;
	isa_pnp_wsize = 0;

	for (s_addr = 0; s_addr < (2 + isa_pnp_wsize); s_addr++, wbuf++) {
		wval = AscReadEEPWord(iot, ioh, s_addr);
		sum += wval;
		*wbuf = wval;
	}

	if (bus_type & ASC_IS_VL) {
		cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
		cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
	} else {
		cfg_beg = ASC_EEP_DVC_CFG_BEG;
		cfg_end = ASC_EEP_MAX_DVC_ADDR;
	}

	for (s_addr = cfg_beg; s_addr <= (cfg_end - 1); s_addr++, wbuf++) {
		wval = AscReadEEPWord(iot, ioh, s_addr);
		sum += wval;
		*wbuf = wval;
	}

	*wbuf = AscReadEEPWord(iot, ioh, s_addr);

	return (sum);
}


static int
AscSetEEPConfig(bus_space_tag_t iot, bus_space_handle_t ioh,
    ASCEEP_CONFIG *cfg_buf, u_int16_t bus_type)
{
	int             retry;
	int             n_error;

	retry = 0;
	while (TRUE) {
		if ((n_error = AscSetEEPConfigOnce(iot, ioh, cfg_buf, bus_type)) == 0)
			break;

		if (++retry > ASC_EEP_MAX_RETRY)
			break;
	}

	return (n_error);
}


static int
AscSetEEPConfigOnce(bus_space_tag_t iot, bus_space_handle_t ioh,
    ASCEEP_CONFIG *cfg_buf, u_int16_t bus_type)
{
	int             n_error;
	u_int16_t      *wbuf;
	u_int16_t       sum;
	int             s_addr;
	int             cfg_beg;
	int             cfg_end;

	wbuf = (u_int16_t *) cfg_buf;
	n_error = 0;
	sum = 0;

	for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
		sum += *wbuf;
		if (*wbuf != AscWriteEEPWord(iot, ioh, s_addr, *wbuf))
			n_error++;
	}

	if (bus_type & ASC_IS_VL) {
		cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
		cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
	} else {
		cfg_beg = ASC_EEP_DVC_CFG_BEG;
		cfg_end = ASC_EEP_MAX_DVC_ADDR;
	}

	for (s_addr = cfg_beg; s_addr <= (cfg_end - 1); s_addr++, wbuf++) {
		sum += *wbuf;
		if (*wbuf != AscWriteEEPWord(iot, ioh, s_addr, *wbuf))
			n_error++;
	}

	*wbuf = sum;
	if (sum != AscWriteEEPWord(iot, ioh, s_addr, sum))
		n_error++;

	wbuf = (u_int16_t *) cfg_buf;
	for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
		if (*wbuf != AscReadEEPWord(iot, ioh, s_addr))
			n_error++;
	}

	for (s_addr = cfg_beg; s_addr <= cfg_end; s_addr++, wbuf++) {
		if (*wbuf != AscReadEEPWord(iot, ioh, s_addr))
			n_error++;
	}

	return (n_error);
}


#ifdef ASC_DEBUG
static void
AscPrintEEPConfig(ASCEEP_CONFIG *eep_config, u_int16_t chksum)
{
	printf("---- ASC EEprom settings ----\n");
	printf("cfg_lsw = 0x%x\n", eep_config->cfg_lsw);
	printf("cfg_msw = 0x%x\n", eep_config->cfg_msw);
	printf("init_sdtr = 0x%x\n", eep_config->init_sdtr);
	printf("disc_enable = 0x%x\n", eep_config->disc_enable);
	printf("use_cmd_qng = %d\n", eep_config->use_cmd_qng);
	printf("start_motor = 0x%x\n", eep_config->start_motor);
	printf("max_total_qng = 0x%x\n", eep_config->max_total_qng);
	printf("max_tag_qng = 0x%x\n", eep_config->max_tag_qng);
	printf("bios_scan = 0x%x\n", eep_config->bios_scan);
	printf("power_up_wait = 0x%x\n", eep_config->power_up_wait);
	printf("no_scam = %d\n", eep_config->no_scam);
	printf("chip_scsi_id = %d\n", eep_config->chip_scsi_id);
	printf("isa_dma_speed = %d\n", eep_config->isa_dma_speed);
	printf("cntl = 0x%x\n", eep_config->cntl);
#if BYTE_ORDER == BIG_ENDIAN
	printf("adapter_info[0] = 0x%x\n", eep_config->adapter_info[1]);
	printf("adapter_info[1] = 0x%x\n", eep_config->adapter_info[0]);
	printf("adapter_info[2] = 0x%x\n", eep_config->adapter_info[3]);
	printf("adapter_info[3] = 0x%x\n", eep_config->adapter_info[2]);
	printf("adapter_info[4] = 0x%x\n", eep_config->adapter_info[5]);
	printf("adapter_info[5] = 0x%x\n", eep_config->adapter_info[4]);
#else
	printf("adapter_info[0] = 0x%x\n", eep_config->adapter_info[0]);
	printf("adapter_info[1] = 0x%x\n", eep_config->adapter_info[1]);
	printf("adapter_info[2] = 0x%x\n", eep_config->adapter_info[2]);
	printf("adapter_info[3] = 0x%x\n", eep_config->adapter_info[3]);
	printf("adapter_info[4] = 0x%x\n", eep_config->adapter_info[4]);
	printf("adapter_info[5] = 0x%x\n", eep_config->adapter_info[5]);
#endif
	printf("checksum = 0x%x\n", eep_config->chksum);
	printf("calculated checksum = 0x%x\n", chksum);
	printf("-----------------------------\n");
}
#endif


/******************************************************************************/
/*                               Interrupt routines                           */
/******************************************************************************/


int
AscISR(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t       chipstat;
	u_int16_t       saved_ram_addr;
	u_int8_t        ctrl_reg;
	u_int8_t        saved_ctrl_reg;
	int             int_pending;
	int             status;
	u_int8_t        host_flag;

	int_pending = FALSE;

	ctrl_reg = ASC_GET_CHIP_CONTROL(iot, ioh);
	saved_ctrl_reg = ctrl_reg & (~(ASC_CC_SCSI_RESET | ASC_CC_CHIP_RESET |
			   ASC_CC_SINGLE_STEP | ASC_CC_DIAG | ASC_CC_TEST));
	chipstat = ASC_GET_CHIP_STATUS(iot, ioh);
	if (chipstat & ASC_CSW_SCSI_RESET_LATCH) {
		if (!(sc->bus_type & (ASC_IS_VL | ASC_IS_EISA))) {
			int_pending = TRUE;
			sc->sdtr_done = 0;
			saved_ctrl_reg &= (u_int8_t) (~ASC_CC_HALT);

			while (ASC_GET_CHIP_STATUS(iot, ioh) & ASC_CSW_SCSI_RESET_ACTIVE);

			ASC_SET_CHIP_CONTROL(iot, ioh, (ASC_CC_CHIP_RESET | ASC_CC_HALT));
			ASC_SET_CHIP_CONTROL(iot, ioh, ASC_CC_HALT);
			ASC_SET_CHIP_STATUS(iot, ioh, ASC_CIW_CLR_SCSI_RESET_INT);
			ASC_SET_CHIP_STATUS(iot, ioh, 0);
			chipstat = ASC_GET_CHIP_STATUS(iot, ioh);
		}
	}
	saved_ram_addr = ASC_GET_CHIP_LRAM_ADDR(iot, ioh);
	host_flag = AscReadLramByte(iot, ioh, ASCV_HOST_FLAG_B) &
		(u_int8_t) (~ASC_HOST_FLAG_IN_ISR);
	AscWriteLramByte(iot, ioh, ASCV_HOST_FLAG_B,
			 (host_flag | ASC_HOST_FLAG_IN_ISR));

	if ((chipstat & ASC_CSW_INT_PENDING) || (int_pending)) {
		AscAckInterrupt(iot, ioh);
		int_pending = TRUE;

		if ((chipstat & ASC_CSW_HALTED) &&
		    (ctrl_reg & ASC_CC_SINGLE_STEP)) {
			AscIsrChipHalted(sc);
			saved_ctrl_reg &= ~ASC_CC_HALT;
		} else {
			if (sc->dvc_cntl & ASC_CNTL_INT_MULTI_Q) {
				while (((status = AscIsrQDone(sc)) & 0x01) != 0);
			} else {
				do {
					if ((status = AscIsrQDone(sc)) == 1)
						break;
				} while (status == 0x11);
			}

			if (status & 0x80)
				int_pending = -1;
		}
	}
	AscWriteLramByte(iot, ioh, ASCV_HOST_FLAG_B, host_flag);
	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, saved_ram_addr);
	ASC_SET_CHIP_CONTROL(iot, ioh, saved_ctrl_reg);

	return (1);
	/* return(int_pending); */
}


static int
AscIsrQDone(ASC_SOFTC *sc)
{
	u_int8_t        next_qp;
	u_int8_t        n_q_used;
	u_int8_t        sg_list_qp;
	u_int8_t        sg_queue_cnt;
	u_int8_t        q_cnt;
	u_int8_t        done_q_tail;
	u_int8_t        tid_no;
	ASC_SCSI_BIT_ID_TYPE scsi_busy;
	ASC_SCSI_BIT_ID_TYPE target_id;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t       q_addr;
	u_int16_t       sg_q_addr;
	u_int8_t        cur_target_qng;
	ASC_QDONE_INFO  scsiq_buf;
	ASC_QDONE_INFO *scsiq;
	ASC_ISR_CALLBACK asc_isr_callback;

	asc_isr_callback = (ASC_ISR_CALLBACK) sc->isr_callback;
	n_q_used = 1;
	scsiq = (ASC_QDONE_INFO *) & scsiq_buf;
	done_q_tail = ASC_GET_VAR_DONE_QTAIL(iot, ioh);
	q_addr = ASC_QNO_TO_QADDR(done_q_tail);
	next_qp = AscReadLramByte(iot, ioh, (q_addr + ASC_SCSIQ_B_FWD));

	if (next_qp != ASC_QLINK_END) {
		ASC_PUT_VAR_DONE_QTAIL(iot, ioh, next_qp);
		q_addr = ASC_QNO_TO_QADDR(next_qp);
		sg_queue_cnt = _AscCopyLramScsiDoneQ(iot, ioh, q_addr, scsiq,
						     sc->max_dma_count);
		AscWriteLramByte(iot, ioh, (q_addr + ASC_SCSIQ_B_STATUS),
		      (scsiq->q_status & ~(ASC_QS_READY | ASC_QS_ABORTED)));
		tid_no = ASC_TIX_TO_TID(scsiq->d2.target_ix);
		target_id = ASC_TIX_TO_TARGET_ID(scsiq->d2.target_ix);
		if ((scsiq->cntl & ASC_QC_SG_HEAD) != 0) {
			sg_q_addr = q_addr;
			sg_list_qp = next_qp;
			for (q_cnt = 0; q_cnt < sg_queue_cnt; q_cnt++) {
				sg_list_qp = AscReadLramByte(iot, ioh,
					       sg_q_addr + ASC_SCSIQ_B_FWD);
				sg_q_addr = ASC_QNO_TO_QADDR(sg_list_qp);
				if (sg_list_qp == ASC_QLINK_END) {
					AscSetLibErrorCode(sc, ASCQ_ERR_SG_Q_LINKS);
					scsiq->d3.done_stat = ASC_QD_WITH_ERROR;
					scsiq->d3.host_stat = ASC_QHSTA_D_QDONE_SG_LIST_CORRUPTED;
					panic("AscIsrQDone: Corrupted SG list encountered");
				}
				AscWriteLramByte(iot, ioh,
				sg_q_addr + ASC_SCSIQ_B_STATUS, ASC_QS_FREE);
			}
			n_q_used = sg_queue_cnt + 1;
			ASC_PUT_VAR_DONE_QTAIL(iot, ioh, sg_list_qp);
		}
		if (sc->queue_full_or_busy & target_id) {
			cur_target_qng = AscReadLramByte(iot, ioh,
					ASC_QADR_BEG + scsiq->d2.target_ix);

			if (cur_target_qng < sc->max_dvc_qng[tid_no]) {
				scsi_busy = AscReadLramByte(iot, ioh, ASCV_SCSIBUSY_B);
				scsi_busy &= ~target_id;
				AscWriteLramByte(iot, ioh, ASCV_SCSIBUSY_B, scsi_busy);
				sc->queue_full_or_busy &= ~target_id;
			}
		}
		if (sc->cur_total_qng >= n_q_used) {
			sc->cur_total_qng -= n_q_used;
			if (sc->cur_dvc_qng[tid_no] != 0) {
				sc->cur_dvc_qng[tid_no]--;
			}
		} else {
			AscSetLibErrorCode(sc, ASCQ_ERR_CUR_QNG);
			scsiq->d3.done_stat = ASC_QD_WITH_ERROR;
			panic("AscIsrQDone: Attempting to free more queues than are active");
		}

		if ((scsiq->d2.ccb_ptr == 0UL) || ((scsiq->q_status & ASC_QS_ABORTED) != 0)) {
			return (0x11);
		} else if (scsiq->q_status == ASC_QS_DONE) {
			scsiq->remain_bytes += scsiq->extra_bytes;

			if (scsiq->d3.done_stat == ASC_QD_WITH_ERROR) {
				if (scsiq->d3.host_stat == ASC_QHSTA_M_DATA_OVER_RUN) {
					if ((scsiq->cntl & (ASC_QC_DATA_IN | ASC_QC_DATA_OUT)) == 0) {
						scsiq->d3.done_stat = ASC_QD_NO_ERROR;
						scsiq->d3.host_stat = ASC_QHSTA_NO_ERROR;
					}
				} else if (scsiq->d3.host_stat == ASC_QHSTA_M_HUNG_REQ_SCSI_BUS_RESET) {
					AscStopChip(iot, ioh);
					ASC_SET_CHIP_CONTROL(iot, ioh, (ASC_CC_SCSI_RESET | ASC_CC_HALT));
					DvcDelayNanoSecond(60000);
					ASC_SET_CHIP_CONTROL(iot, ioh, ASC_CC_HALT);
					ASC_SET_CHIP_STATUS(iot, ioh, ASC_CIW_CLR_SCSI_RESET_INT);
					ASC_SET_CHIP_STATUS(iot, ioh, 0);
					ASC_SET_CHIP_CONTROL(iot, ioh, 0);
				}
			}
			(*asc_isr_callback) (sc, scsiq);

			return (1);
		} else {
			AscSetLibErrorCode(sc, ASCQ_ERR_Q_STATUS);
			panic("AscIsrQDone: completed scsiq with unknown status");

			return (0x80);
		}
	}
	return (0);
}


/*
 * handle all the conditions that may halt the board
 * waiting us to intervene
 */
static void
AscIsrChipHalted(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	EXT_MSG         out_msg;
	u_int16_t       int_halt_code;
	u_int16_t       halt_q_addr;
	u_int8_t        halt_qp;
	u_int8_t        target_ix;
	u_int8_t        tag_code;
	u_int8_t        q_status;
	u_int8_t        q_cntl;
	u_int8_t        tid_no;
	u_int8_t        cur_dvc_qng;
	u_int8_t        asyn_sdtr;
	u_int8_t        scsi_status;
	u_int8_t        sdtr_data;
	ASC_SCSI_BIT_ID_TYPE scsi_busy;
	ASC_SCSI_BIT_ID_TYPE target_id;

	int_halt_code = AscReadLramWord(iot, ioh, ASCV_HALTCODE_W);

	halt_qp = AscReadLramByte(iot, ioh, ASCV_CURCDB_B);
	halt_q_addr = ASC_QNO_TO_QADDR(halt_qp);
	target_ix = AscReadLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_TARGET_IX);
	q_cntl = AscReadLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_CNTL);
	tid_no = ASC_TIX_TO_TID(target_ix);
	target_id = ASC_TID_TO_TARGET_ID(tid_no);

	if (sc->pci_fix_asyn_xfer & target_id) {
		asyn_sdtr = ASYN_SDTR_DATA_FIX_PCI_REV_AB;
	} else {
		asyn_sdtr = 0;
	}

	if (int_halt_code == ASC_HALT_DISABLE_ASYN_USE_SYN_FIX) {
		if (sc->pci_fix_asyn_xfer & target_id) {
			AscSetChipSDTR(iot, ioh, 0, tid_no);
			sc->sdtr_data[tid_no] = 0;
		}
		AscWriteLramWord(iot, ioh, ASCV_HALTCODE_W, 0);
	} else if (int_halt_code == ASC_HALT_ENABLE_ASYN_USE_SYN_FIX) {
		if (sc->pci_fix_asyn_xfer & target_id) {
			AscSetChipSDTR(iot, ioh, asyn_sdtr, tid_no);
			sc->sdtr_data[tid_no] = asyn_sdtr;
		}
		AscWriteLramWord(iot, ioh, ASCV_HALTCODE_W, 0);
	} else if (int_halt_code == ASC_HALT_EXTMSG_IN) {
		AscHandleExtMsgIn(sc, halt_q_addr, q_cntl, target_id,
				  tid_no, asyn_sdtr);
		AscWriteLramWord(iot, ioh, ASCV_HALTCODE_W, 0);
	} else if (int_halt_code == ASC_HALT_CHK_CONDITION) {
		q_cntl |= ASC_QC_REQ_SENSE;

		if (sc->init_sdtr & target_id) {
			sc->sdtr_done &= ~target_id;

			sdtr_data = ASC_GET_MCODE_INIT_SDTR_AT_ID(iot, ioh, tid_no);
			q_cntl |= ASC_QC_MSG_OUT;
			AscMsgOutSDTR(sc, sc->sdtr_period_tbl[(sdtr_data >> 4) &
						  (sc->max_sdtr_index - 1)],
				      (sdtr_data & ASC_SYN_MAX_OFFSET));
		}
		AscWriteLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_CNTL, q_cntl);

		tag_code = AscReadLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_TAG_CODE);
		tag_code &= 0xDC;

		if ((sc->pci_fix_asyn_xfer & target_id) &&
		    !(sc->pci_fix_asyn_xfer_always & target_id)) {
			tag_code |= (ASC_TAG_FLAG_DISABLE_DISCONNECT |
				     ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX);
		}
		AscWriteLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_TAG_CODE, tag_code);

		q_status = AscReadLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_STATUS);
		q_status |= ASC_QS_READY | ASC_QS_BUSY;

		AscWriteLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_STATUS, q_status);

		scsi_busy = AscReadLramByte(iot, ioh, ASCV_SCSIBUSY_B);
		scsi_busy &= ~target_id;
		AscWriteLramByte(iot, ioh, ASCV_SCSIBUSY_B, scsi_busy);

		AscWriteLramWord(iot, ioh, ASCV_HALTCODE_W, 0);
	} else if (int_halt_code == ASC_HALT_SDTR_REJECTED) {
		AscMemWordCopyFromLram(iot, ioh, ASCV_MSGOUT_BEG,
			     (u_int16_t *) & out_msg, sizeof(EXT_MSG) >> 1);

		if ((out_msg.msg_type == MS_EXTEND) &&
		    (out_msg.msg_len == MS_SDTR_LEN) &&
		    (out_msg.msg_req == MS_SDTR_CODE)) {
			sc->init_sdtr &= ~target_id;
			sc->sdtr_done &= ~target_id;
			AscSetChipSDTR(iot, ioh, asyn_sdtr, tid_no);
			sc->sdtr_data[tid_no] = asyn_sdtr;
		}
		q_cntl &= ~ASC_QC_MSG_OUT;
		AscWriteLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_CNTL, q_cntl);
		AscWriteLramWord(iot, ioh, ASCV_HALTCODE_W, 0);
	} else if (int_halt_code == ASC_HALT_SS_QUEUE_FULL) {
		scsi_status = AscReadLramByte(iot, ioh,
				       halt_q_addr + ASC_SCSIQ_SCSI_STATUS);
		cur_dvc_qng = AscReadLramByte(iot, ioh, target_ix + ASC_QADR_BEG);

		if ((cur_dvc_qng > 0) && (sc->cur_dvc_qng[tid_no] > 0)) {
			scsi_busy = AscReadLramByte(iot, ioh, ASCV_SCSIBUSY_B);
			scsi_busy |= target_id;
			AscWriteLramByte(iot, ioh, ASCV_SCSIBUSY_B, scsi_busy);
			sc->queue_full_or_busy |= target_id;

			if (scsi_status == SS_QUEUE_FULL) {
				if (cur_dvc_qng > ASC_MIN_TAGGED_CMD) {
					cur_dvc_qng -= 1;
					sc->max_dvc_qng[tid_no] = cur_dvc_qng;

					AscWriteLramByte(iot, ioh,
							 tid_no + ASCV_MAX_DVC_QNG_BEG, cur_dvc_qng);

#if ASC_QUEUE_FLOW_CONTROL
					if ((sc->device[tid_no] != NULL) &&
					    (sc->device[tid_no]->queue_curr_depth > cur_dvc_qng)) {
						sc->device[tid_no]->queue_curr_depth = cur_dvc_qng;
					}
#endif				/* ASC_QUEUE_FLOW_CONTROL */
				}
			}
		}
		AscWriteLramWord(iot, ioh, ASCV_HALTCODE_W, 0);
	}
	return;
}


static int
AscWaitTixISRDone(ASC_SOFTC *sc, u_int8_t target_ix)
{
	u_int8_t        cur_req;
	u_int8_t        tid_no;
	int             i = 0;

	tid_no = ASC_TIX_TO_TID(target_ix);
	while (i++ < 10) {
		if ((cur_req = sc->cur_dvc_qng[tid_no]) == 0)
			break;

		DvcSleepMilliSecond(1000L);
		if (sc->cur_dvc_qng[tid_no] == cur_req)
			break;
	}
	return (1);
}

static int
AscWaitISRDone(ASC_SOFTC *sc)
{
	int             tid;

	for (tid = 0; tid <= ASC_MAX_TID; tid++)
		AscWaitTixISRDone(sc, ASC_TID_TO_TIX(tid));

	return (1);
}


static u_int8_t
_AscCopyLramScsiDoneQ(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int16_t q_addr, ASC_QDONE_INFO *scsiq, u_int32_t max_dma_count)
{
	u_int16_t       _val;
	u_int8_t        sg_queue_cnt;

	AscGetQDoneInfo(iot, ioh, q_addr + ASC_SCSIQ_DONE_INFO_BEG, scsiq);

	_val = AscReadLramWord(iot, ioh, q_addr + ASC_SCSIQ_B_STATUS);
	scsiq->q_status = LO_BYTE(_val);
	scsiq->q_no = HI_BYTE(_val);
	_val = AscReadLramWord(iot, ioh, q_addr + ASC_SCSIQ_B_CNTL);
	scsiq->cntl = LO_BYTE(_val);
	sg_queue_cnt = HI_BYTE(_val);
	_val = AscReadLramWord(iot, ioh, q_addr + ASC_SCSIQ_B_SENSE_LEN);
	scsiq->sense_len = LO_BYTE(_val);
	scsiq->extra_bytes = HI_BYTE(_val);
	scsiq->remain_bytes = AscReadLramWord(iot, ioh,
				     q_addr + ASC_SCSIQ_DW_REMAIN_XFER_CNT);
	scsiq->remain_bytes &= max_dma_count;

	return (sg_queue_cnt);
}


static void
AscGetQDoneInfo(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t addr,
    ASC_QDONE_INFO *scsiq)
{
	u_int16_t	val;

	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, addr);

	val = ASC_GET_CHIP_LRAM_DATA(iot, ioh);
	scsiq->d2.ccb_ptr = MAKELONG(val, ASC_GET_CHIP_LRAM_DATA(iot, ioh));
	val = ASC_GET_CHIP_LRAM_DATA(iot, ioh);
	scsiq->d2.target_ix = LO_BYTE(val);
	scsiq->d2.flag = HI_BYTE(val);
	val = ASC_GET_CHIP_LRAM_DATA(iot, ioh);
	scsiq->d2.cdb_len = LO_BYTE(val);
	scsiq->d2.tag_code = HI_BYTE(val);
	scsiq->d2.vm_id = ASC_GET_CHIP_LRAM_DATA(iot, ioh);

	val = ASC_GET_CHIP_LRAM_DATA(iot, ioh);
	scsiq->d3.done_stat = LO_BYTE(val);
	scsiq->d3.host_stat = HI_BYTE(val);
	val = ASC_GET_CHIP_LRAM_DATA(iot, ioh);
	scsiq->d3.scsi_stat = LO_BYTE(val);
	scsiq->d3.scsi_msg = HI_BYTE(val);
}


static void
AscToggleIRQAct(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	ASC_SET_CHIP_STATUS(iot, ioh, ASC_CIW_IRQ_ACT);
	ASC_SET_CHIP_STATUS(iot, ioh, 0);
}


static void
AscDisableInterrupt(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int16_t       cfg;

	cfg = ASC_GET_CHIP_CFG_LSW(iot, ioh);
	ASC_SET_CHIP_CFG_LSW(iot, ioh, cfg & (~ASC_CFG0_HOST_INT_ON));
}


static void
AscEnableInterrupt(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int16_t       cfg;

	cfg = ASC_GET_CHIP_CFG_LSW(iot, ioh);
	ASC_SET_CHIP_CFG_LSW(iot, ioh, cfg | ASC_CFG0_HOST_INT_ON);
}


static u_int8_t
AscGetChipIRQ(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t bus_type)
{
	u_int16_t       cfg_lsw;
	u_int8_t        chip_irq;

	if (bus_type & ASC_IS_EISA) {
		/*
		 * cfg_lsw = AscGetEisaChipCfg(iot, ioh); chip_irq =
		 * ((cfg_lsw >> 8) & 0x07) + 10; if((chip_irq == 13) ||
		 * (chip_irq > 15)) return (0); return(chip_irq);
		 */
	}
	if ((bus_type & ASC_IS_VL) != 0) {
		cfg_lsw = ASC_GET_CHIP_CFG_LSW(iot, ioh);
		chip_irq = (cfg_lsw >> 2) & 0x07;
		if ((chip_irq == 0) ||
		    (chip_irq == 4) ||
		    (chip_irq == 7)) {
			return (0);
		}
		return (chip_irq + (ASC_MIN_IRQ_NO - 1));
	}
	cfg_lsw = ASC_GET_CHIP_CFG_LSW(iot, ioh);
	chip_irq = (cfg_lsw >> 2) & 0x03;
	if (chip_irq == 3)
		chip_irq += 2;
	return (chip_irq + ASC_MIN_IRQ_NO);
}


static u_int8_t
AscSetChipIRQ(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t irq_no,
    u_int16_t bus_type)
{
	u_int16_t       cfg_lsw;

	if (bus_type & ASC_IS_VL) {
		if (irq_no) {
			if ((irq_no < ASC_MIN_IRQ_NO) || (irq_no > ASC_MAX_IRQ_NO))
				irq_no = 0;
			else
				irq_no -= ASC_MIN_IRQ_NO - 1;
		}

		cfg_lsw = ASC_GET_CHIP_CFG_LSW(iot, ioh) & 0xFFE3;
		cfg_lsw |= 0x0010;
		ASC_SET_CHIP_CFG_LSW(iot, ioh, cfg_lsw);
		AscToggleIRQAct(iot, ioh);
		cfg_lsw = ASC_GET_CHIP_CFG_LSW(iot, ioh) & 0xFFE0;
		cfg_lsw |= (irq_no & 0x07) << 2;
		ASC_SET_CHIP_CFG_LSW(iot, ioh, cfg_lsw);
		AscToggleIRQAct(iot, ioh);

		return (AscGetChipIRQ(iot, ioh, bus_type));
	}
	if (bus_type & ASC_IS_ISA) {
		if (irq_no == 15)
			irq_no -= 2;
		irq_no -= ASC_MIN_IRQ_NO;
		cfg_lsw = ASC_GET_CHIP_CFG_LSW(iot, ioh) & 0xFFF3;
		cfg_lsw |= (irq_no & 0x03) << 2;
		ASC_SET_CHIP_CFG_LSW(iot, ioh, cfg_lsw);

		return (AscGetChipIRQ(iot, ioh, bus_type));
	}
	return (0);
}


static void
AscAckInterrupt(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int8_t        host_flag;
	u_int8_t        risc_flag;
	u_int16_t       loop;

	loop = 0;
	do {
		risc_flag = AscReadLramByte(iot, ioh, ASCV_RISC_FLAG_B);
		if (loop++ > 0x7FFF)
			break;
	} while ((risc_flag & ASC_RISC_FLAG_GEN_INT) != 0);

	host_flag = AscReadLramByte(iot, ioh, ASCV_HOST_FLAG_B) &
		(~ASC_HOST_FLAG_ACK_INT);
	AscWriteLramByte(iot, ioh, ASCV_HOST_FLAG_B,
			 host_flag | ASC_HOST_FLAG_ACK_INT);
	ASC_SET_CHIP_STATUS(iot, ioh, ASC_CIW_INT_ACK);

	loop = 0;
	while (ASC_GET_CHIP_STATUS(iot, ioh) & ASC_CSW_INT_PENDING) {
		ASC_SET_CHIP_STATUS(iot, ioh, ASC_CIW_INT_ACK);
		if (loop++ > 3)
			break;
	}

	AscWriteLramByte(iot, ioh, ASCV_HOST_FLAG_B, host_flag);
}


static u_int32_t
AscGetMaxDmaCount(u_int16_t bus_type)
{
	if (bus_type & ASC_IS_ISA)
		return (ASC_MAX_ISA_DMA_COUNT);
	else if (bus_type & (ASC_IS_EISA | ASC_IS_VL))
		return (ASC_MAX_VL_DMA_COUNT);
	return (ASC_MAX_PCI_DMA_COUNT);
}


static u_int16_t
AscGetIsaDmaChannel(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int16_t       channel;

	channel = ASC_GET_CHIP_CFG_LSW(iot, ioh) & 0x0003;
	if (channel == 0x03)
		return (0);
	else if (channel == 0x00)
		return (7);
	return (channel + 4);
}


static u_int16_t
AscSetIsaDmaChannel(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int16_t dma_channel)
{
	u_int16_t       cfg_lsw;
	u_int8_t        value;

	if ((dma_channel >= 5) && (dma_channel <= 7)) {
		if (dma_channel == 7)
			value = 0x00;
		else
			value = dma_channel - 4;
		cfg_lsw = ASC_GET_CHIP_CFG_LSW(iot, ioh) & 0xFFFC;
		cfg_lsw |= value;
		ASC_SET_CHIP_CFG_LSW(iot, ioh, cfg_lsw);
		return (AscGetIsaDmaChannel(iot, ioh));
	}
	return (0);
}


static u_int8_t
AscGetIsaDmaSpeed(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	u_int8_t        speed_value;

	AscSetBank(iot, ioh, 1);
	speed_value = ASC_READ_CHIP_DMA_SPEED(iot, ioh);
	speed_value &= 0x07;
	AscSetBank(iot, ioh, 0);
	return (speed_value);
}


static u_int8_t
AscSetIsaDmaSpeed(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t speed_value)
{
	speed_value &= 0x07;
	AscSetBank(iot, ioh, 1);
	ASC_WRITE_CHIP_DMA_SPEED(iot, ioh, speed_value);
	AscSetBank(iot, ioh, 0);
	return (AscGetIsaDmaSpeed(iot, ioh));
}


/******************************************************************************/
/*                              Messages routines                             */
/******************************************************************************/


static void
AscHandleExtMsgIn(ASC_SOFTC *sc, u_int16_t halt_q_addr, u_int8_t q_cntl,
    ASC_SCSI_BIT_ID_TYPE target_id, int tid_no, u_int8_t asyn_sdtr)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	EXT_MSG         ext_msg;
	u_int8_t        sdtr_data;
	int             sdtr_accept;

	AscMemWordCopyFromLram(iot, ioh, ASCV_MSGIN_BEG,
			     (u_int16_t *) & ext_msg, sizeof(EXT_MSG) >> 1);

	if (ext_msg.msg_type == MS_EXTEND &&
	    ext_msg.msg_req == MS_SDTR_CODE &&
	    ext_msg.msg_len == MS_SDTR_LEN) {
		sdtr_accept = TRUE;

		if (ext_msg.req_ack_offset > ASC_SYN_MAX_OFFSET) {
			sdtr_accept = FALSE;
			ext_msg.req_ack_offset = ASC_SYN_MAX_OFFSET;
		}
		if ((ext_msg.xfer_period <
		     sc->sdtr_period_tbl[sc->host_init_sdtr_index]) ||
		    (ext_msg.xfer_period >
		     sc->sdtr_period_tbl[sc->max_sdtr_index])) {
			sdtr_accept = FALSE;
			ext_msg.xfer_period = sc->sdtr_period_tbl[sc->host_init_sdtr_index];
		}
		if (sdtr_accept) {
			sdtr_data = AscCalSDTRData(sc, ext_msg.xfer_period,
						   ext_msg.req_ack_offset);
			if (sdtr_data == 0xFF) {
				q_cntl |= ASC_QC_MSG_OUT;
				sc->init_sdtr &= ~target_id;
				sc->sdtr_done &= ~target_id;
				AscSetChipSDTR(iot, ioh, asyn_sdtr, tid_no);
				sc->sdtr_data[tid_no] = asyn_sdtr;
			}
		}
		if (ext_msg.req_ack_offset == 0) {
			q_cntl &= ~ASC_QC_MSG_OUT;
			sc->init_sdtr &= ~target_id;
			sc->sdtr_done &= ~target_id;
			AscSetChipSDTR(iot, ioh, asyn_sdtr, tid_no);
		} else {
			if (sdtr_accept && (q_cntl & ASC_QC_MSG_OUT)) {
				q_cntl &= ~ASC_QC_MSG_OUT;
				sc->sdtr_done |= target_id;
				sc->init_sdtr |= target_id;
				sc->pci_fix_asyn_xfer &= ~target_id;
				sdtr_data = AscCalSDTRData(sc, ext_msg.xfer_period,
						    ext_msg.req_ack_offset);
				AscSetChipSDTR(iot, ioh, sdtr_data, tid_no);
				sc->sdtr_data[tid_no] = sdtr_data;
			} else {
				q_cntl |= ASC_QC_MSG_OUT;
				AscMsgOutSDTR(sc, ext_msg.xfer_period,
					      ext_msg.req_ack_offset);
				sc->pci_fix_asyn_xfer &= ~target_id;
				sdtr_data = AscCalSDTRData(sc, ext_msg.xfer_period,
						    ext_msg.req_ack_offset);
				AscSetChipSDTR(iot, ioh, sdtr_data, tid_no);
				sc->sdtr_data[tid_no] = sdtr_data;
				sc->sdtr_done |= target_id;
				sc->init_sdtr |= target_id;
			}
		}
	} else if (ext_msg.msg_type == MS_EXTEND &&
		   ext_msg.msg_req == MS_WDTR_CODE &&
		   ext_msg.msg_len == MS_WDTR_LEN) {
		ext_msg.wdtr_width = 0;
		AscMemWordCopyToLram(iot, ioh, ASCV_MSGOUT_BEG,
			     (u_int16_t *) & ext_msg, sizeof(EXT_MSG) >> 1);
		q_cntl |= ASC_QC_MSG_OUT;
	} else {
		ext_msg.msg_type = M1_MSG_REJECT;
		AscMemWordCopyToLram(iot, ioh, ASCV_MSGOUT_BEG,
			     (u_int16_t *) & ext_msg, sizeof(EXT_MSG) >> 1);
		q_cntl |= ASC_QC_MSG_OUT;
	}

	AscWriteLramByte(iot, ioh, halt_q_addr + ASC_SCSIQ_B_CNTL, q_cntl);
}


static u_int8_t
AscMsgOutSDTR(ASC_SOFTC *sc, u_int8_t sdtr_period, u_int8_t sdtr_offset)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	EXT_MSG         sdtr_buf;
	u_int8_t        sdtr_period_index;

	sdtr_buf.msg_type = MS_EXTEND;
	sdtr_buf.msg_len = MS_SDTR_LEN;
	sdtr_buf.msg_req = MS_SDTR_CODE;
	sdtr_buf.xfer_period = sdtr_period;
	sdtr_offset &= ASC_SYN_MAX_OFFSET;
	sdtr_buf.req_ack_offset = sdtr_offset;
	if ((sdtr_period_index = AscGetSynPeriodIndex(sc, sdtr_period)) <=
	    sc->max_sdtr_index) {
		AscMemWordCopyToLram(iot, ioh, ASCV_MSGOUT_BEG,
			    (u_int16_t *) & sdtr_buf, sizeof(EXT_MSG) >> 1);
		return ((sdtr_period_index << 4) | sdtr_offset);
	} else {
		sdtr_buf.req_ack_offset = 0;
		AscMemWordCopyToLram(iot, ioh, ASCV_MSGOUT_BEG,
			    (u_int16_t *) & sdtr_buf, sizeof(EXT_MSG) >> 1);
		return (0);
	}
}


/******************************************************************************/
/*                                  SDTR routines                             */
/******************************************************************************/


static void
AscSetChipSDTR(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t sdtr_data,
    u_int8_t tid_no)
{
	AscSetChipSynRegAtID(iot, ioh, tid_no, sdtr_data);
	AscWriteLramByte(iot, ioh, tid_no + ASCV_SDTR_DONE_BEG, sdtr_data);
}


static u_int8_t
AscCalSDTRData(ASC_SOFTC *sc, u_int8_t sdtr_period, u_int8_t syn_offset)
{
	u_int8_t        byte;
	u_int8_t        sdtr_period_ix;

	sdtr_period_ix = AscGetSynPeriodIndex(sc, sdtr_period);
	if (sdtr_period_ix > sc->max_sdtr_index)
		return (0xFF);

	byte = (sdtr_period_ix << 4) | (syn_offset & ASC_SYN_MAX_OFFSET);
	return (byte);
}


static u_int8_t
AscGetSynPeriodIndex(ASC_SOFTC *sc, u_int8_t syn_time)
{
	u_int8_t       *period_table;
	int             max_index;
	int             min_index;
	int             i;

	period_table = sc->sdtr_period_tbl;
	max_index = sc->max_sdtr_index;
	min_index = sc->host_init_sdtr_index;
	if ((syn_time <= period_table[max_index])) {
		for (i = min_index; i < (max_index - 1); i++) {
			if (syn_time <= period_table[i])
				return (i);
		}

		return (max_index);
	} else
		return (max_index + 1);
}


/******************************************************************************/
/*                                 Queue routines                             */
/******************************************************************************/

/*
 * Send a command to the board
 */
int
AscExeScsiQueue(ASC_SOFTC *sc, ASC_SCSI_Q *scsiq)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	ASC_SG_HEAD    *sg_head = scsiq->sg_head;
	int             retval;
	int             n_q_required;
	int             disable_syn_offset_one_fix;
	int             i;
	u_int32_t       addr;
	u_int16_t       sg_entry_cnt = 0;
	u_int16_t       sg_entry_cnt_minus_one = 0;
	u_int8_t        target_ix;
	u_int8_t        tid_no;
	u_int8_t        sdtr_data;
	u_int8_t        extra_bytes;
	u_int8_t        scsi_cmd;
	u_int32_t       data_cnt;

	scsiq->q1.q_no = 0;
	if ((scsiq->q2.tag_code & ASC_TAG_FLAG_EXTRA_BYTES) == 0)
		scsiq->q1.extra_bytes = 0;

	retval = ASC_BUSY;
	target_ix = scsiq->q2.target_ix;
	tid_no = ASC_TIX_TO_TID(target_ix);
	n_q_required = 1;

	if (scsiq->cdbptr[0] == SCSICMD_RequestSense)
		if ((sc->init_sdtr & scsiq->q1.target_id) != 0) {
			sc->sdtr_done &= ~scsiq->q1.target_id;
			sdtr_data = ASC_GET_MCODE_INIT_SDTR_AT_ID(iot, ioh, tid_no);
			AscMsgOutSDTR(sc, sc->sdtr_period_tbl[(sdtr_data >> 4) &
						  (sc->max_sdtr_index - 1)],
				      sdtr_data & ASC_SYN_MAX_OFFSET);
			scsiq->q1.cntl |= (ASC_QC_MSG_OUT | ASC_QC_URGENT);
		}
	/*
	 * if there is just one segment into S/G list then
	 * map it as it was a single request, filling
	 * data_addr and data_cnt of ASC_SCSIQ structure.
	 */
	if ((scsiq->q1.cntl & ASC_QC_SG_HEAD) != 0) {
		sg_entry_cnt = sg_head->entry_cnt;

		if (sg_entry_cnt < 1)
			panic("AscExeScsiQueue: Queue with QC_SG_HEAD set but %d segs.",
			      sg_entry_cnt);

		if (sg_entry_cnt > ASC_MAX_SG_LIST)
			panic("AscExeScsiQueue: Queue with too many segs.");

		if (sg_entry_cnt == 1) {
			scsiq->q1.data_addr = sg_head->sg_list[0].addr;
			scsiq->q1.data_cnt = sg_head->sg_list[0].bytes;
			scsiq->q1.cntl &= ~(ASC_QC_SG_HEAD | ASC_QC_SG_SWAP_QUEUE);
		}
		sg_entry_cnt_minus_one = sg_entry_cnt - 1;
	}
	scsi_cmd = scsiq->cdbptr[0];
	disable_syn_offset_one_fix = FALSE;
	if ((sc->pci_fix_asyn_xfer & scsiq->q1.target_id) &&
	    !(sc->pci_fix_asyn_xfer_always & scsiq->q1.target_id)) {
		if (scsiq->q1.cntl & ASC_QC_SG_HEAD) {
			data_cnt = 0;
			for (i = 0; i < sg_entry_cnt; i++)
				data_cnt += sg_head->sg_list[i].bytes;
		} else {
			data_cnt = scsiq->q1.data_cnt;
		}

		if (data_cnt != 0ul) {
			if (data_cnt < 512ul) {
				disable_syn_offset_one_fix = TRUE;
			} else {
				if (scsi_cmd == SCSICMD_Inquiry ||
				    scsi_cmd == SCSICMD_RequestSense ||
				    scsi_cmd == SCSICMD_ReadCapacity ||
				    scsi_cmd == SCSICMD_ReadTOC ||
				    scsi_cmd == SCSICMD_ModeSelect6 ||
				    scsi_cmd == SCSICMD_ModeSense6 ||
				    scsi_cmd == SCSICMD_ModeSelect10 ||
				    scsi_cmd == SCSICMD_ModeSense10) {
					disable_syn_offset_one_fix = TRUE;
				}
			}
		}
	}
	if (disable_syn_offset_one_fix) {
		scsiq->q2.tag_code &= ~M2_QTAG_MSG_SIMPLE;
		scsiq->q2.tag_code |= (ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX |
				       ASC_TAG_FLAG_DISABLE_DISCONNECT);
	} else {
		scsiq->q2.tag_code &= 0x23;
	}

	if ((scsiq->q1.cntl & ASC_QC_SG_HEAD) != 0) {
		if (sc->bug_fix_cntl) {
			if (sc->bug_fix_cntl & ASC_BUG_FIX_IF_NOT_DWB) {
				if ((scsi_cmd == SCSICMD_Read6) || (scsi_cmd == SCSICMD_Read10)) {
					addr = sg_head->sg_list[sg_entry_cnt_minus_one].addr +
						sg_head->sg_list[sg_entry_cnt_minus_one].bytes;
					extra_bytes = addr & 0x0003;
					if ((extra_bytes != 0) &&
					    ((scsiq->q2.tag_code & ASC_TAG_FLAG_EXTRA_BYTES) == 0)) {
						scsiq->q2.tag_code |= ASC_TAG_FLAG_EXTRA_BYTES;
						scsiq->q1.extra_bytes = extra_bytes;
						sg_head->sg_list[sg_entry_cnt_minus_one].bytes -=
							extra_bytes;
					}
				}
			}
		}
		sg_head->entry_to_copy = sg_head->entry_cnt;
		n_q_required = AscSgListToQueue(sg_entry_cnt);
		if ((AscGetNumOfFreeQueue(sc, target_ix, n_q_required) >= n_q_required)
		    || ((scsiq->q1.cntl & ASC_QC_URGENT) != 0)) {
			retval = AscSendScsiQueue(sc, scsiq, n_q_required);
		}
	} else {
		if (sc->bug_fix_cntl) {
			if (sc->bug_fix_cntl & ASC_BUG_FIX_IF_NOT_DWB) {
				if ((scsi_cmd == SCSICMD_Read6) || (scsi_cmd == SCSICMD_Read10)) {
					addr = scsiq->q1.data_addr + scsiq->q1.data_cnt;
					extra_bytes = addr & 0x0003;
					if ((extra_bytes != 0) &&
					    ((scsiq->q2.tag_code & ASC_TAG_FLAG_EXTRA_BYTES) == 0)) {
						if ((scsiq->q1.data_cnt & 0x01FF) == 0) {
							scsiq->q2.tag_code |= ASC_TAG_FLAG_EXTRA_BYTES;
							scsiq->q1.data_cnt -= extra_bytes;
							scsiq->q1.extra_bytes = extra_bytes;
						}
					}
				}
			}
		}
		n_q_required = 1;
		if ((AscGetNumOfFreeQueue(sc, target_ix, 1) >= 1) ||
		    ((scsiq->q1.cntl & ASC_QC_URGENT) != 0)) {
			retval = AscSendScsiQueue(sc, scsiq, n_q_required);
		}
	}

	return (retval);
}


static int
AscSendScsiQueue(ASC_SOFTC *sc, ASC_SCSI_Q *scsiq, u_int8_t n_q_required)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t        free_q_head;
	u_int8_t        next_qp;
	u_int8_t        tid_no;
	u_int8_t        target_ix;
	int             retval;

	target_ix = scsiq->q2.target_ix;
	tid_no = ASC_TIX_TO_TID(target_ix);
	retval = ASC_BUSY;
	free_q_head = ASC_GET_VAR_FREE_QHEAD(iot, ioh);

	if ((next_qp = AscAllocMultipleFreeQueue(iot, ioh, free_q_head, n_q_required))
	    != ASC_QLINK_END) {
		if (n_q_required > 1) {
			sc->last_q_shortage = 0;
			scsiq->sg_head->queue_cnt = n_q_required - 1;
		}
		scsiq->q1.q_no = free_q_head;

		if ((retval = AscPutReadySgListQueue(sc, scsiq, free_q_head)) == ASC_NOERROR) {
			ASC_PUT_VAR_FREE_QHEAD(iot, ioh, next_qp);
			sc->cur_total_qng += n_q_required;
			sc->cur_dvc_qng[tid_no]++;
		}
	}
	return (retval);
}


static int
AscPutReadySgListQueue(ASC_SOFTC *sc, ASC_SCSI_Q *scsiq, u_int8_t q_no)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int             retval;
	int             i;
	ASC_SG_HEAD    *sg_head;
	ASC_SG_LIST_Q   scsi_sg_q;
	u_int32_t       saved_data_addr;
	u_int32_t       saved_data_cnt;
	u_int16_t       sg_list_dwords;
	u_int16_t       sg_index;
	u_int16_t       sg_entry_cnt;
	u_int16_t       q_addr;
	u_int8_t        next_qp;

	saved_data_addr = scsiq->q1.data_addr;
	saved_data_cnt = scsiq->q1.data_cnt;

	if ((sg_head = scsiq->sg_head) != 0) {
		scsiq->q1.data_addr = sg_head->sg_list[0].addr;
		scsiq->q1.data_cnt = sg_head->sg_list[0].bytes;
		sg_entry_cnt = sg_head->entry_cnt - 1;
		if (sg_entry_cnt != 0) {
			q_addr = ASC_QNO_TO_QADDR(q_no);
			sg_index = 1;
			scsiq->q1.sg_queue_cnt = sg_head->queue_cnt;
			scsi_sg_q.sg_head_qp = q_no;
			scsi_sg_q.cntl = ASC_QCSG_SG_XFER_LIST;

			for (i = 0; i < sg_head->queue_cnt; i++) {
				scsi_sg_q.seq_no = i + 1;
				if (sg_entry_cnt > ASC_SG_LIST_PER_Q) {
					sg_list_dwords = ASC_SG_LIST_PER_Q * 2;
					sg_entry_cnt -= ASC_SG_LIST_PER_Q;
					if (i == 0) {
						scsi_sg_q.sg_list_cnt = ASC_SG_LIST_PER_Q;
						scsi_sg_q.sg_cur_list_cnt = ASC_SG_LIST_PER_Q;
					} else {
						scsi_sg_q.sg_list_cnt = ASC_SG_LIST_PER_Q - 1;
						scsi_sg_q.sg_cur_list_cnt = ASC_SG_LIST_PER_Q - 1;
					}
				} else {
					scsi_sg_q.cntl |= ASC_QCSG_SG_XFER_END;
					sg_list_dwords = sg_entry_cnt << 1;
					if (i == 0) {
						scsi_sg_q.sg_list_cnt = sg_entry_cnt;
						scsi_sg_q.sg_cur_list_cnt = sg_entry_cnt;
					} else {
						scsi_sg_q.sg_list_cnt = sg_entry_cnt - 1;
						scsi_sg_q.sg_cur_list_cnt = sg_entry_cnt - 1;
					}

					sg_entry_cnt = 0;
				}

				next_qp = AscReadLramByte(iot, ioh, q_addr + ASC_SCSIQ_B_FWD);
				scsi_sg_q.q_no = next_qp;
				q_addr = ASC_QNO_TO_QADDR(next_qp);

				/*
				 * Tell the board how many entries are in the S/G list
				 */
				AscMemWordCopyToLram(iot, ioh, q_addr + ASC_SCSIQ_SGHD_CPY_BEG,
							(u_int16_t *) & scsi_sg_q,
							sizeof(ASC_SG_LIST_Q) >> 1);
				/*
				 * Tell the board the addresses of the S/G list segments
				 */
				AscMemDWordCopyToLram(iot, ioh, q_addr + ASC_SGQ_LIST_BEG,
							(u_int32_t *) & sg_head->sg_list[sg_index],
							sg_list_dwords);
				sg_index += ASC_SG_LIST_PER_Q;
			}
		}
	}
	retval = AscPutReadyQueue(sc, scsiq, q_no);
	scsiq->q1.data_addr = saved_data_addr;
	scsiq->q1.data_cnt = saved_data_cnt;
	return (retval);
}


static int
AscPutReadyQueue(ASC_SOFTC *sc, ASC_SCSI_Q *scsiq, u_int8_t q_no)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t       q_addr;
	u_int8_t        tid_no;
	u_int8_t        sdtr_data;
	u_int8_t        syn_period_ix;
	u_int8_t        syn_offset;

	if (((sc->init_sdtr & scsiq->q1.target_id) != 0) &&
	    ((sc->sdtr_done & scsiq->q1.target_id) == 0)) {
		tid_no = ASC_TIX_TO_TID(scsiq->q2.target_ix);
		sdtr_data = ASC_GET_MCODE_INIT_SDTR_AT_ID(iot, ioh, tid_no);
		syn_period_ix = (sdtr_data >> 4) & (sc->max_sdtr_index - 1);
		syn_offset = sdtr_data & ASC_SYN_MAX_OFFSET;
		AscMsgOutSDTR(sc, sc->sdtr_period_tbl[syn_period_ix], syn_offset);
		scsiq->q1.cntl |= ASC_QC_MSG_OUT;
	}
	q_addr = ASC_QNO_TO_QADDR(q_no);

	if ((scsiq->q1.target_id & sc->use_tagged_qng) == 0) {
		scsiq->q2.tag_code &= ~M2_QTAG_MSG_SIMPLE;
	}
	scsiq->q1.status = ASC_QS_FREE;
	AscMemWordCopyToLram(iot, ioh, q_addr + ASC_SCSIQ_CDB_BEG,
		       (u_int16_t *) scsiq->cdbptr, scsiq->q2.cdb_len >> 1);

	AscPutSCSIQ(iot, ioh, q_addr + ASC_SCSIQ_CPY_BEG, scsiq);

	/*
	 * Let's start the command
	 */
	AscWriteLramWord(iot, ioh, q_addr + ASC_SCSIQ_B_STATUS,
			 (scsiq->q1.q_no << 8) | ASC_QS_READY);

	return (ASC_NOERROR);
}


static void
AscPutSCSIQ(bus_space_tag_t iot, bus_space_handle_t ioh, u_int16_t addr,
    ASC_SCSI_Q *scsiq)
{
	u_int16_t	val;

	ASC_SET_CHIP_LRAM_ADDR(iot, ioh, addr);

	/* ASC_SCSIQ_1 */
	val = MAKEWORD(scsiq->q1.cntl, scsiq->q1.sg_queue_cnt);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = MAKEWORD(scsiq->q1.target_id, scsiq->q1.target_lun);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = LO_WORD(scsiq->q1.data_addr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = HI_WORD(scsiq->q1.data_addr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = LO_WORD(scsiq->q1.data_cnt);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = HI_WORD(scsiq->q1.data_cnt);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = LO_WORD(scsiq->q1.sense_addr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = HI_WORD(scsiq->q1.sense_addr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = MAKEWORD(scsiq->q1.sense_len, scsiq->q1.extra_bytes);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);

	/* ASC_SCSIQ_2 */
	val = LO_WORD(scsiq->q2.ccb_ptr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = HI_WORD(scsiq->q2.ccb_ptr);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = MAKEWORD(scsiq->q2.target_ix, scsiq->q2.flag);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	val = MAKEWORD(scsiq->q2.cdb_len, scsiq->q2.tag_code);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, val);
	ASC_SET_CHIP_LRAM_DATA(iot, ioh, scsiq->q2.vm_id);
}


static int
AscSgListToQueue(int sg_list)
{
	int             n_sg_list_qs;

	n_sg_list_qs = ((sg_list - 1) / ASC_SG_LIST_PER_Q);
	if (((sg_list - 1) % ASC_SG_LIST_PER_Q) != 0)
		n_sg_list_qs++;

	return (n_sg_list_qs + 1);
}


static u_int
AscGetNumOfFreeQueue(ASC_SOFTC *sc, u_int8_t target_ix, u_int8_t n_qs)
{
	u_int           cur_used_qs;
	u_int           cur_free_qs;

	if (n_qs == 1) {
		cur_used_qs = sc->cur_total_qng +
			sc->last_q_shortage +
			ASC_MIN_FREE_Q;
	} else {
		cur_used_qs = sc->cur_total_qng + ASC_MIN_FREE_Q;
	}

	if ((cur_used_qs + n_qs) <= sc->max_total_qng) {
		cur_free_qs = sc->max_total_qng - cur_used_qs;
		return (cur_free_qs);
	}
	if (n_qs > 1)
		if ((n_qs > sc->last_q_shortage) &&
		    (n_qs <= (sc->max_total_qng - ASC_MIN_FREE_Q))) {
			sc->last_q_shortage = n_qs;
		}
	return (0);
}


static u_int8_t
AscAllocFreeQueue(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t free_q_head)
{
	u_int16_t       q_addr;
	u_int8_t        next_qp;
	u_int8_t        q_status;

	q_addr = ASC_QNO_TO_QADDR(free_q_head);
	q_status = AscReadLramByte(iot, ioh, q_addr + ASC_SCSIQ_B_STATUS);
	next_qp = AscReadLramByte(iot, ioh, q_addr + ASC_SCSIQ_B_FWD);
	if (((q_status & ASC_QS_READY) == 0) && (next_qp != ASC_QLINK_END))
		return (next_qp);

	return (ASC_QLINK_END);
}


static u_int8_t
AscAllocMultipleFreeQueue(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t free_q_head, u_int8_t n_free_q)
{
	u_int8_t        i;

	for (i = 0; i < n_free_q; i++) {
		free_q_head = AscAllocFreeQueue(iot, ioh, free_q_head);
		if (free_q_head == ASC_QLINK_END)
			break;
	}

	return (free_q_head);
}


static int
AscStopQueueExe(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int             count = 0;

	if (AscReadLramByte(iot, ioh, ASCV_STOP_CODE_B) == 0) {
		AscWriteLramByte(iot, ioh, ASCV_STOP_CODE_B, ASC_STOP_REQ_RISC_STOP);
		do {
			if (AscReadLramByte(iot, ioh, ASCV_STOP_CODE_B) &
			    ASC_STOP_ACK_RISC_STOP)
				return (1);

			DvcSleepMilliSecond(100);
		} while (count++ < 20);
	}
	return (0);
}


static void
AscStartQueueExe(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	if (AscReadLramByte(iot, ioh, ASCV_STOP_CODE_B) != 0)
		AscWriteLramByte(iot, ioh, ASCV_STOP_CODE_B, 0);
}


static void
AscCleanUpBusyQueue(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int             count = 0;
	u_int8_t        stop_code;

	if (AscReadLramByte(iot, ioh, ASCV_STOP_CODE_B) != 0) {
		AscWriteLramByte(iot, ioh, ASCV_STOP_CODE_B, ASC_STOP_CLEAN_UP_BUSY_Q);
		do {
			stop_code = AscReadLramByte(iot, ioh, ASCV_STOP_CODE_B);
			if ((stop_code & ASC_STOP_CLEAN_UP_BUSY_Q) == 0)
				break;

			DvcSleepMilliSecond(100);
		} while (count++ < 20);
	}
}


static int
_AscWaitQDone(bus_space_tag_t iot, bus_space_handle_t ioh, ASC_SCSI_Q *scsiq)
{
	u_int16_t       q_addr;
	u_int8_t        q_status;
	int             count = 0;

	while (scsiq->q1.q_no == 0);

	q_addr = ASC_QNO_TO_QADDR(scsiq->q1.q_no);
	do {
		q_status = AscReadLramByte(iot, ioh, q_addr + ASC_SCSIQ_B_STATUS);
		DvcSleepMilliSecond(100L);
		if (count++ > 30)
			return (0);

	} while ((q_status & ASC_QS_READY) != 0);

	return (1);
}


static int
AscCleanUpDiscQueue(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int             count;
	u_int8_t        stop_code;

	count = 0;
	if (AscReadLramByte(iot, ioh, ASCV_STOP_CODE_B) != 0) {
		AscWriteLramByte(iot, ioh, ASCV_STOP_CODE_B, ASC_STOP_CLEAN_UP_DISC_Q);
		do {
			stop_code = AscReadLramByte(iot, ioh, ASCV_STOP_CODE_B);
			if ((stop_code & ASC_STOP_CLEAN_UP_DISC_Q) == 0)
				break;

			DvcSleepMilliSecond(100);
		} while (count++ < 20);
	}
	return (1);
}


/******************************************************************************/
/*                           Abort and Reset CCB routines                     */
/******************************************************************************/


int
AscAbortCCB(ASC_SOFTC *sc, u_int32_t ccb)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int             retval;
	ASC_SCSI_BIT_ID_TYPE saved_unit_not_ready;

	retval = -1;
	saved_unit_not_ready = sc->unit_not_ready;
	sc->unit_not_ready = 0xFF;
	AscWaitISRDone(sc);
	if (AscStopQueueExe(iot, ioh) == 1) {
		if (AscRiscHaltedAbortCCB(sc, ccb) == 1) {
			retval = 1;
			AscCleanUpBusyQueue(iot, ioh);
			AscStartQueueExe(iot, ioh);
		} else {
			retval = 0;
			AscStartQueueExe(iot, ioh);
		}
	}
	sc->unit_not_ready = saved_unit_not_ready;

	return (retval);
}


static int
AscRiscHaltedAbortCCB(ASC_SOFTC *sc, u_int32_t ccb)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t       q_addr;
	u_int8_t        q_no;
	ASC_QDONE_INFO  scsiq_buf;
	ASC_QDONE_INFO *scsiq;
	ASC_ISR_CALLBACK asc_isr_callback;
	int             last_int_level;

	asc_isr_callback = (ASC_ISR_CALLBACK) sc->isr_callback;
	last_int_level = DvcEnterCritical();
	scsiq = (ASC_QDONE_INFO *) & scsiq_buf;

	for (q_no = ASC_MIN_ACTIVE_QNO; q_no <= sc->max_total_qng; q_no++) {
		q_addr = ASC_QNO_TO_QADDR(q_no);
		scsiq->d2.ccb_ptr = AscReadLramDWord(iot, ioh,
					       q_addr + ASC_SCSIQ_D_CCBPTR);
		if (scsiq->d2.ccb_ptr == ccb) {
			_AscCopyLramScsiDoneQ(iot, ioh, q_addr, scsiq, sc->max_dma_count);
			if (((scsiq->q_status & ASC_QS_READY) != 0)
			    && ((scsiq->q_status & ASC_QS_ABORTED) == 0)
			  && ((scsiq->cntl & ASC_QCSG_SG_XFER_LIST) == 0)) {
				scsiq->q_status |= ASC_QS_ABORTED;
				scsiq->d3.done_stat = ASC_QD_ABORTED_BY_HOST;
				AscWriteLramDWord(iot, ioh, q_addr + ASC_SCSIQ_D_CCBPTR, 0L);
				AscWriteLramByte(iot, ioh, q_addr + ASC_SCSIQ_B_STATUS,
						 scsiq->q_status);
				(*asc_isr_callback) (sc, scsiq);
				DvcLeaveCritical(last_int_level);
				return (1);
			}
		}
	}

	DvcLeaveCritical(last_int_level);
	return (0);
}


static int
AscRiscHaltedAbortTIX(ASC_SOFTC *sc, u_int8_t target_ix)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t       q_addr;
	u_int8_t        q_no;
	ASC_QDONE_INFO  scsiq_buf;
	ASC_QDONE_INFO *scsiq;
	ASC_ISR_CALLBACK asc_isr_callback;
	int             last_int_level;

	asc_isr_callback = (ASC_ISR_CALLBACK) sc->isr_callback;
	last_int_level = DvcEnterCritical();
	scsiq = (ASC_QDONE_INFO *) & scsiq_buf;
	for (q_no = ASC_MIN_ACTIVE_QNO; q_no <= sc->max_total_qng; q_no++) {
		q_addr = ASC_QNO_TO_QADDR(q_no);
		_AscCopyLramScsiDoneQ(iot, ioh, q_addr, scsiq, sc->max_dma_count);
		if (((scsiq->q_status & ASC_QS_READY) != 0) &&
		    ((scsiq->q_status & ASC_QS_ABORTED) == 0) &&
		    ((scsiq->cntl & ASC_QCSG_SG_XFER_LIST) == 0)) {
			if (scsiq->d2.target_ix == target_ix) {
				scsiq->q_status |= ASC_QS_ABORTED;
				scsiq->d3.done_stat = ASC_QD_ABORTED_BY_HOST;
				AscWriteLramDWord(iot, ioh, q_addr + ASC_SCSIQ_D_CCBPTR, 0L);
				AscWriteLramByte(iot, ioh, q_addr + ASC_SCSIQ_B_STATUS,
						 scsiq->q_status);
				(*asc_isr_callback) (sc, scsiq);
			}
		}
	}
	DvcLeaveCritical(last_int_level);
	return (1);
}


/*
 * AscResetDevice calls _AscWaitQDone which requires interrupt enabled,
 * so we cannot use this function with the actual NetBSD SCSI layer
 * because at boot time interrupts are disabled.
 */
int
AscResetDevice(ASC_SOFTC *sc, u_char target_ix)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int             retval;
	u_int8_t        tid_no;
	ASC_SCSI_BIT_ID_TYPE target_id;
	int             i;
	ASC_SCSI_REQ_Q  scsiq_buf;
	ASC_SCSI_REQ_Q *scsiq;
	u_int8_t       *buf;
	ASC_SCSI_BIT_ID_TYPE saved_unit_not_ready;


	tid_no = ASC_TIX_TO_TID(target_ix);
	target_id = ASC_TID_TO_TARGET_ID(tid_no);
	saved_unit_not_ready = sc->unit_not_ready;
	sc->unit_not_ready = target_id;
	retval = ASC_ERROR;

	AscWaitTixISRDone(sc, target_ix);

	if (AscStopQueueExe(iot, ioh) == 1) {
		if (AscRiscHaltedAbortTIX(sc, target_ix) == 1) {
			AscCleanUpBusyQueue(iot, ioh);
			AscStartQueueExe(iot, ioh);
			AscWaitTixISRDone(sc, target_ix);
			retval = ASC_NOERROR;
			scsiq = (ASC_SCSI_REQ_Q *) & scsiq_buf;
			buf = (u_char *) & scsiq_buf;
			for (i = 0; i < sizeof(ASC_SCSI_REQ_Q); i++)
				*buf++ = 0x00;
			scsiq->q1.status = (u_char) ASC_QS_READY;
			scsiq->q2.cdb_len = 6;
			scsiq->q2.tag_code = M2_QTAG_MSG_SIMPLE;
			scsiq->q1.target_id = target_id;
			scsiq->q2.target_ix = ASC_TIDLUN_TO_IX(tid_no, 0);
			scsiq->cdbptr = (u_int8_t *) scsiq->cdb;
			scsiq->q1.cntl = ASC_QC_NO_CALLBACK | ASC_QC_MSG_OUT | ASC_QC_URGENT;
			AscWriteLramByte(iot, ioh, ASCV_MSGOUT_BEG, M1_BUS_DVC_RESET);
			sc->unit_not_ready &= ~target_id;
			sc->sdtr_done |= target_id;
			if (AscExeScsiQueue(sc, (ASC_SCSI_Q *) scsiq) == ASC_NOERROR) {
				sc->unit_not_ready = target_id;
				DvcSleepMilliSecond(1000);
				_AscWaitQDone(iot, ioh, (ASC_SCSI_Q *) scsiq);
				if (AscStopQueueExe(iot, ioh) == ASC_NOERROR) {
					AscCleanUpDiscQueue(iot, ioh);
					AscStartQueueExe(iot, ioh);
					if (sc->pci_fix_asyn_xfer & target_id)
						AscSetRunChipSynRegAtID(iot, ioh, tid_no,
								ASYN_SDTR_DATA_FIX_PCI_REV_AB);
					AscWaitTixISRDone(sc, target_ix);
				}
			} else
				retval = ASC_BUSY;
			sc->sdtr_done &= ~target_id;
		} else {
			retval = ASC_ERROR;
			AscStartQueueExe(iot, ioh);
		}
	}
	sc->unit_not_ready = saved_unit_not_ready;
	return (retval);
}


int
AscResetBus(ASC_SOFTC *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int             retval;
	int             i;

	sc->unit_not_ready = 0xFF;
	retval = ASC_NOERROR;

	AscWaitISRDone(sc);
	AscStopQueueExe(iot, ioh);
	sc->sdtr_done = 0;
	AscResetChipAndScsiBus(iot, ioh);
	DvcSleepMilliSecond((u_long) ((u_int16_t) sc->scsi_reset_wait * 1000));
	AscReInitLram(sc);
	for (i = 0; i <= ASC_MAX_TID; i++) {
		sc->cur_dvc_qng[i] = 0;
		if (sc->pci_fix_asyn_xfer & (ASC_SCSI_BIT_ID_TYPE) (0x01 << i))
			AscSetChipSynRegAtID(iot, ioh, i, ASYN_SDTR_DATA_FIX_PCI_REV_AB);
	}

	ASC_SET_PC_ADDR(iot, ioh, ASC_MCODE_START_ADDR);
	if (ASC_GET_PC_ADDR(iot, ioh) != ASC_MCODE_START_ADDR)
		retval = ASC_ERROR;

	if (AscStartChip(iot, ioh) == 0)
		retval = ASC_ERROR;

	AscStartQueueExe(iot, ioh);
	sc->unit_not_ready = 0;
	sc->queue_full_or_busy = 0;
	return (retval);
}


/******************************************************************************/
/*                            Error Handling routines                         */
/******************************************************************************/


static int
AscSetLibErrorCode(ASC_SOFTC *sc, u_int16_t err_code)
{
	/*
	 * if(sc->err_code == 0) { sc->err_code = err_code;
	 */ AscWriteLramWord(sc->sc_iot, sc->sc_ioh, ASCV_ASCDVC_ERR_CODE_W,
			       err_code);
	/*
	 * }
	 */
	return (err_code);
}


/******************************************************************************/
/*                            Handle bugged borads routines                   */
/******************************************************************************/


void
AscInquiryHandling(ASC_SOFTC *sc, u_int8_t tid_no, ASC_SCSI_INQUIRY *inq)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	ASC_SCSI_BIT_ID_TYPE tid_bit = ASC_TIX_TO_TARGET_ID(tid_no);
	ASC_SCSI_BIT_ID_TYPE orig_init_sdtr, orig_use_tagged_qng;

	orig_init_sdtr = sc->init_sdtr;
	orig_use_tagged_qng = sc->use_tagged_qng;

	sc->init_sdtr &= ~tid_bit;
	sc->can_tagged_qng &= ~tid_bit;
	sc->use_tagged_qng &= ~tid_bit;

	if (inq->byte3.rsp_data_fmt >= 2 || inq->byte2.ansi_apr_ver >= 2) {
		if ((sc->sdtr_enable & tid_bit) && inq->byte7.Sync)
			sc->init_sdtr |= tid_bit;

		if ((sc->cmd_qng_enabled & tid_bit) && inq->byte7.CmdQue)
			if (AscTagQueuingSafe(inq)) {
				sc->use_tagged_qng |= tid_bit;
				sc->can_tagged_qng |= tid_bit;
			}
	}
	if (orig_use_tagged_qng != sc->use_tagged_qng) {
		AscWriteLramByte(iot, ioh, ASCV_DISC_ENABLE_B,
				 sc->disc_enable);
		AscWriteLramByte(iot, ioh, ASCV_USE_TAGGED_QNG_B,
				 sc->use_tagged_qng);
		AscWriteLramByte(iot, ioh, ASCV_CAN_TAGGED_QNG_B,
				 sc->can_tagged_qng);

		sc->max_dvc_qng[tid_no] =
			sc->max_tag_qng[tid_no];
		AscWriteLramByte(iot, ioh, ASCV_MAX_DVC_QNG_BEG + tid_no,
				 sc->max_dvc_qng[tid_no]);
	}
	if (orig_init_sdtr != sc->init_sdtr)
		AscAsyncFix(sc, tid_no, inq);
}


static int
AscTagQueuingSafe(ASC_SCSI_INQUIRY *inq)
{
	if ((inq->add_len >= 32) &&
	    (AscCompareString(inq->vendor_id, "QUANTUM XP34301", 15) == 0) &&
	    (AscCompareString(inq->product_rev_level, "1071", 4) == 0)) {
		return 0;
	}
	return 1;
}


static void
AscAsyncFix(ASC_SOFTC *sc, u_int8_t tid_no, ASC_SCSI_INQUIRY *inq)
{
	u_int8_t        dvc_type;
	ASC_SCSI_BIT_ID_TYPE tid_bits;

	dvc_type = inq->byte0.peri_dvc_type;
	tid_bits = ASC_TIX_TO_TARGET_ID(tid_no);

	if (sc->bug_fix_cntl & ASC_BUG_FIX_ASYN_USE_SYN) {
		if (!(sc->init_sdtr & tid_bits)) {
			if ((dvc_type == SCSI_TYPE_CDROM) &&
			(AscCompareString(inq->vendor_id, "HP ", 3) == 0)) {
				sc->pci_fix_asyn_xfer_always |= tid_bits;
			}
			sc->pci_fix_asyn_xfer |= tid_bits;
			if ((dvc_type == SCSI_TYPE_PROC) ||
			    (dvc_type == SCSI_TYPE_SCANNER)) {
				sc->pci_fix_asyn_xfer &= ~tid_bits;
			}
			if ((dvc_type == SCSI_TYPE_SASD) &&
			    (AscCompareString(inq->vendor_id, "TANDBERG", 8) == 0) &&
			    (AscCompareString(inq->product_id, " TDC 36", 7) == 0)) {
				sc->pci_fix_asyn_xfer &= ~tid_bits;
			}
			if ((dvc_type == SCSI_TYPE_SASD) &&
			    (AscCompareString(inq->vendor_id, "WANGTEK ", 8) == 0)) {
				sc->pci_fix_asyn_xfer &= ~tid_bits;
			}
			if ((dvc_type == SCSI_TYPE_CDROM) &&
			    (AscCompareString(inq->vendor_id, "NEC	 ", 8) == 0) &&
			    (AscCompareString(inq->product_id, "CD-ROM DRIVE	", 16) == 0)) {
				sc->pci_fix_asyn_xfer &= ~tid_bits;
			}
			if ((dvc_type == SCSI_TYPE_CDROM) &&
			    (AscCompareString(inq->vendor_id, "YAMAHA", 6) == 0) &&
			    (AscCompareString(inq->product_id, "CDR400", 6) == 0)) {
				sc->pci_fix_asyn_xfer &= ~tid_bits;
			}
			if (sc->pci_fix_asyn_xfer & tid_bits) {
				AscSetRunChipSynRegAtID(sc->sc_iot, sc->sc_ioh, tid_no,
					     ASYN_SDTR_DATA_FIX_PCI_REV_AB);
			}
		}
	}
}


/******************************************************************************/
/*                              Miscellaneous routines                        */
/******************************************************************************/


static int
AscCompareString(u_char *str1, u_char *str2, int len)
{
	int             i;
	int             diff;

	for (i = 0; i < len; i++) {
		diff = (int) (str1[i] - str2[i]);
		if (diff != 0)
			return (diff);
	}

	return (0);
}


/******************************************************************************/
/*                            Device oriented routines                        */
/******************************************************************************/


static int
DvcEnterCritical(void)
{
	int             s;

	s = splbio();
	return (s);
}


static void
DvcLeaveCritical(int s)
{
	splx(s);
}


static void
DvcSleepMilliSecond(u_int32_t n)
{
	DELAY(n * 1000);
}

#ifdef UNUSED
static void
DvcDelayMicroSecond(u_int32_t n)
{
	DELAY(n);
}
#endif

static void
DvcDelayNanoSecond(u_int32_t n)
{
	DELAY((n + 999) / 1000);
}
