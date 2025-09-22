/*	$OpenBSD: sdmmc_mem.c,v 1.37 2022/01/10 18:23:39 tobhe Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Routines for SD/MMC memory cards. */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmcvar.h>

#ifdef HIBERNATE
#include <uvm/uvm_extern.h>
#endif

typedef struct { uint32_t _bits[512/32]; } __packed __aligned(4) sdmmc_bitfield512_t;

void	sdmmc_be512_to_bitfield512(sdmmc_bitfield512_t *);

int	sdmmc_decode_csd(struct sdmmc_softc *, sdmmc_response,
	    struct sdmmc_function *);
int	sdmmc_decode_cid(struct sdmmc_softc *, sdmmc_response,
	    struct sdmmc_function *);
void	sdmmc_print_cid(struct sdmmc_cid *);

int	sdmmc_mem_send_op_cond(struct sdmmc_softc *, u_int32_t, u_int32_t *);
int	sdmmc_mem_set_blocklen(struct sdmmc_softc *, struct sdmmc_function *);

int	sdmmc_mem_send_scr(struct sdmmc_softc *, uint32_t *);
int	sdmmc_mem_decode_scr(struct sdmmc_softc *, uint32_t *,
	    struct sdmmc_function *);

int	sdmmc_mem_send_cxd_data(struct sdmmc_softc *, int, void *, size_t);
int	sdmmc_mem_set_bus_width(struct sdmmc_function *, int);
int	sdmmc_mem_mmc_switch(struct sdmmc_function *, uint8_t, uint8_t, uint8_t);
int	sdmmc_mem_signal_voltage(struct sdmmc_softc *, int);

int	sdmmc_mem_sd_init(struct sdmmc_softc *, struct sdmmc_function *);
int	sdmmc_mem_mmc_init(struct sdmmc_softc *, struct sdmmc_function *);
int	sdmmc_mem_single_read_block(struct sdmmc_function *, int, u_char *,
	size_t);
int	sdmmc_mem_read_block_subr(struct sdmmc_function *, bus_dmamap_t,
	int, u_char *, size_t);
int	sdmmc_mem_single_write_block(struct sdmmc_function *, int, u_char *,
	size_t);
int	sdmmc_mem_write_block_subr(struct sdmmc_function *, bus_dmamap_t,
	int, u_char *, size_t);

#ifdef SDMMC_DEBUG
#define DPRINTF(s)	printf s
#else
#define DPRINTF(s)	/**/
#endif

const struct {
	const char *name;
	int v;
	int freq;
} switch_group0_functions[] = {
	/* Default/SDR12 */
	{ "Default/SDR12",	 0,			 25000 },

	/* High-Speed/SDR25 */
	{ "High-Speed/SDR25",	SMC_CAPS_SD_HIGHSPEED,	 50000 },

	/* SDR50 */
	{ "SDR50",		SMC_CAPS_UHS_SDR50,	100000 },

	/* SDR104 */
	{ "SDR104",		SMC_CAPS_UHS_SDR104,	208000 },

	/* DDR50 */
	{ "DDR50",		SMC_CAPS_UHS_DDR50,	 50000 },
};

const int sdmmc_mmc_timings[] = {
	[SDMMC_TIMING_LEGACY]		= 26000,
	[SDMMC_TIMING_HIGHSPEED]	= 52000,
	[SDMMC_TIMING_MMC_DDR52]	= 52000,
	[SDMMC_TIMING_MMC_HS200]	= 200000
};

/*
 * Initialize SD/MMC memory cards and memory in SDIO "combo" cards.
 */
int
sdmmc_mem_enable(struct sdmmc_softc *sc)
{
	uint32_t host_ocr;
	uint32_t card_ocr;
	uint32_t new_ocr;
	uint32_t ocr = 0;
	int error;

	rw_assert_wrlock(&sc->sc_lock);

	/* Set host mode to SD "combo" card or SD memory-only. */
	CLR(sc->sc_flags, SMF_UHS_MODE);
	SET(sc->sc_flags, SMF_SD_MODE|SMF_MEM_MODE);

	/* Reset memory (*must* do that before CMD55 or CMD1). */
	sdmmc_go_idle_state(sc);

	/*
	 * Read the SD/MMC memory OCR value by issuing CMD55 followed
	 * by ACMD41 to read the OCR value from memory-only SD cards.
	 * MMC cards will not respond to CMD55 or ACMD41 and this is
	 * how we distinguish them from SD cards.
	 */
 mmc_mode:
	if (sdmmc_mem_send_op_cond(sc, 0, &card_ocr) != 0) {
		if (ISSET(sc->sc_flags, SMF_SD_MODE) &&
		    !ISSET(sc->sc_flags, SMF_IO_MODE)) {
			/* Not a SD card, switch to MMC mode. */
			CLR(sc->sc_flags, SMF_SD_MODE);
			goto mmc_mode;
		}
		if (!ISSET(sc->sc_flags, SMF_SD_MODE)) {
			DPRINTF(("%s: can't read memory OCR\n",
			    DEVNAME(sc)));
			return 1;
		} else {
			/* Not a "combo" card. */
			CLR(sc->sc_flags, SMF_MEM_MODE);
			return 0;
		}
	}

	/* Set the lowest voltage supported by the card and host. */
	host_ocr = sdmmc_chip_host_ocr(sc->sct, sc->sch);
	if (sdmmc_set_bus_power(sc, host_ocr, card_ocr) != 0) {
		DPRINTF(("%s: can't supply voltage requested by card\n",
		    DEVNAME(sc)));
		return 1;
	}

	/* Tell the card(s) to enter the idle state (again). */
	sdmmc_go_idle_state(sc);

	host_ocr &= card_ocr; /* only allow the common voltages */

	if (ISSET(sc->sc_flags, SMF_SD_MODE)) {
		if (sdmmc_send_if_cond(sc, card_ocr) == 0)
			SET(ocr, MMC_OCR_HCS);

		if (sdmmc_chip_host_ocr(sc->sct, sc->sch) & MMC_OCR_S18A)
			SET(ocr, MMC_OCR_S18A);
	}
	host_ocr |= ocr;

	/* Send the new OCR value until all cards are ready. */
	if (sdmmc_mem_send_op_cond(sc, host_ocr, &new_ocr) != 0) {
		DPRINTF(("%s: can't send memory OCR\n", DEVNAME(sc)));
		return 1;
	}

	if (ISSET(sc->sc_flags, SMF_SD_MODE) && ISSET(new_ocr, MMC_OCR_S18A)) {
		/*
		 * Card and host support low voltage mode, begin switch
		 * sequence.
		 */
		struct sdmmc_command cmd;

		memset(&cmd, 0, sizeof(cmd));
		cmd.c_arg = 0;
		cmd.c_flags = SCF_CMD_AC | SCF_RSP_R1;
		cmd.c_opcode = SD_VOLTAGE_SWITCH;
		DPRINTF(("%s: switching card to 1.8V\n", DEVNAME(sc)));
		error = sdmmc_mmc_command(sc, &cmd);
		if (error) {
			DPRINTF(("%s: voltage switch command failed\n",
			    DEVNAME(sc)));
			return error;
		}

		error = sdmmc_mem_signal_voltage(sc, SDMMC_SIGNAL_VOLTAGE_180);
		if (error)
			return error;

		SET(sc->sc_flags, SMF_UHS_MODE);
	}

	return 0;
}

int
sdmmc_mem_signal_voltage(struct sdmmc_softc *sc, int signal_voltage)
{
	int error;

	/*
	 * Stop the clock
	 */
	error = sdmmc_chip_bus_clock(sc->sct, sc->sch, 0, SDMMC_TIMING_LEGACY);
	if (error)
		return error;

	delay(1000);

	/*
	 * Card switch command was successful, update host controller
	 * signal voltage setting.
	 */
	DPRINTF(("%s: switching host to %s\n", DEVNAME(sc),
	    signal_voltage == SDMMC_SIGNAL_VOLTAGE_180 ? "1.8V" : "3.3V"));
	error = sdmmc_chip_signal_voltage(sc->sct, sc->sch, signal_voltage);
	if (error)
		return error;

	delay(5000);

	/*
	 * Switch to SDR12 timing
	 */
	error = sdmmc_chip_bus_clock(sc->sct, sc->sch, SDMMC_SDCLK_25MHZ,
	    SDMMC_TIMING_LEGACY);
	if (error)
		return error;

	delay(1000);

	return 0;
}

/*
 * Read the CSD and CID from all cards and assign each card a unique
 * relative card address (RCA).  CMD2 is ignored by SDIO-only cards.
 */
void
sdmmc_mem_scan(struct sdmmc_softc *sc)
{
	struct sdmmc_command cmd;
	struct sdmmc_function *sf;
	u_int16_t next_rca;
	int error;
	int i;

	rw_assert_wrlock(&sc->sc_lock);

	/*
	 * CMD2 is a broadcast command understood by SD cards and MMC
	 * cards.  All cards begin to respond to the command, but back
	 * off if another card drives the CMD line to a different level.
	 * Only one card will get its entire response through.  That
	 * card remains silent once it has been assigned a RCA.
	 */
	for (i = 0; i < 100; i++) {
		bzero(&cmd, sizeof cmd);
		cmd.c_opcode = MMC_ALL_SEND_CID;
		cmd.c_flags = SCF_CMD_BCR | SCF_RSP_R2;

		error = sdmmc_mmc_command(sc, &cmd);
		if (error == ETIMEDOUT) {
			/* No more cards there. */
			break;
		} else if (error != 0) {
			DPRINTF(("%s: can't read CID\n", DEVNAME(sc)));
			break;
		}

		/* In MMC mode, find the next available RCA. */
		next_rca = 1;
		if (!ISSET(sc->sc_flags, SMF_SD_MODE))
			SIMPLEQ_FOREACH(sf, &sc->sf_head, sf_list)
				next_rca++;

		/* Allocate a sdmmc_function structure. */
		sf = sdmmc_function_alloc(sc);
		sf->rca = next_rca;

		/*
		 * Remember the CID returned in the CMD2 response for
		 * later decoding.
		 */
		bcopy(cmd.c_resp, sf->raw_cid, sizeof sf->raw_cid);

		/*
		 * Silence the card by assigning it a unique RCA, or
		 * querying it for its RCA in the case of SD.
		 */
		if (sdmmc_set_relative_addr(sc, sf) != 0) {
			printf("%s: can't set mem RCA\n", DEVNAME(sc));
			sdmmc_function_free(sf);
			break;
		}

#if 0
		/* Verify that the RCA has been set by selecting the card. */
		if (sdmmc_select_card(sc, sf) != 0) {
			printf("%s: can't select mem RCA %d\n",
			    DEVNAME(sc), sf->rca);
			sdmmc_function_free(sf);
			break;
		}

		/* Deselect. */
		(void)sdmmc_select_card(sc, NULL);
#endif

		/*
		 * If this is a memory-only card, the card responding
		 * first becomes an alias for SDIO function 0.
		 */
		if (sc->sc_fn0 == NULL)
			sc->sc_fn0 = sf;

		SIMPLEQ_INSERT_TAIL(&sc->sf_head, sf, sf_list);
	}

	/*
	 * All cards are either inactive or awaiting further commands.
	 * Read the CSDs and decode the raw CID for each card.
	 */
	SIMPLEQ_FOREACH(sf, &sc->sf_head, sf_list) {
		bzero(&cmd, sizeof cmd);
		cmd.c_opcode = MMC_SEND_CSD;
		cmd.c_arg = MMC_ARG_RCA(sf->rca);
		cmd.c_flags = SCF_CMD_AC | SCF_RSP_R2;

		if (sdmmc_mmc_command(sc, &cmd) != 0) {
			SET(sf->flags, SFF_ERROR);
			continue;
		}

		if (sdmmc_decode_csd(sc, cmd.c_resp, sf) != 0 ||
		    sdmmc_decode_cid(sc, sf->raw_cid, sf) != 0) {
			SET(sf->flags, SFF_ERROR);
			continue;
		}

#ifdef SDMMC_DEBUG
		printf("%s: CID: ", DEVNAME(sc));
		sdmmc_print_cid(&sf->cid);
#endif
	}
}

int
sdmmc_decode_csd(struct sdmmc_softc *sc, sdmmc_response resp,
    struct sdmmc_function *sf)
{
	struct sdmmc_csd *csd = &sf->csd;

	if (ISSET(sc->sc_flags, SMF_SD_MODE)) {
		/*
		 * CSD version 1.0 corresponds to SD system
		 * specification version 1.0 - 1.10. (SanDisk, 3.5.3)
		 */
		csd->csdver = SD_CSD_CSDVER(resp);
		switch (csd->csdver) {
		case SD_CSD_CSDVER_2_0:
			sf->flags |= SFF_SDHC;
			csd->capacity = SD_CSD_V2_CAPACITY(resp);
			csd->read_bl_len = SD_CSD_V2_BL_LEN;
			break;
		case SD_CSD_CSDVER_1_0:
			csd->capacity = SD_CSD_CAPACITY(resp);
			csd->read_bl_len = SD_CSD_READ_BL_LEN(resp);
			break;
		default:
			printf("%s: unknown SD CSD structure version 0x%x\n",
			    DEVNAME(sc), csd->csdver);
			return 1;
			break;
		}
		csd->ccc = SD_CSD_CCC(resp);
	} else {
		csd->csdver = MMC_CSD_CSDVER(resp);
		if (csd->csdver == MMC_CSD_CSDVER_1_0 ||
		    csd->csdver == MMC_CSD_CSDVER_2_0 ||
		    csd->csdver == MMC_CSD_CSDVER_EXT_CSD) {
			csd->mmcver = MMC_CSD_MMCVER(resp);
			csd->capacity = MMC_CSD_CAPACITY(resp);
			csd->read_bl_len = MMC_CSD_READ_BL_LEN(resp);
		} else {
			printf("%s: unknown MMC CSD structure version 0x%x\n",
			    DEVNAME(sc), csd->csdver);
			return 1;
		}
	}
	csd->sector_size = MIN(1 << csd->read_bl_len,
	    sdmmc_chip_host_maxblklen(sc->sct, sc->sch));
	if (csd->sector_size < (1<<csd->read_bl_len))
		csd->capacity *= (1<<csd->read_bl_len) /
		    csd->sector_size;

	return 0;
}

int
sdmmc_decode_cid(struct sdmmc_softc *sc, sdmmc_response resp,
    struct sdmmc_function *sf)
{
	struct sdmmc_cid *cid = &sf->cid;

	if (ISSET(sc->sc_flags, SMF_SD_MODE)) {
		cid->mid = SD_CID_MID(resp);
		cid->oid = SD_CID_OID(resp);
		SD_CID_PNM_CPY(resp, cid->pnm);
		cid->rev = SD_CID_REV(resp);
		cid->psn = SD_CID_PSN(resp);
		cid->mdt = SD_CID_MDT(resp);
	} else {
		switch(sf->csd.mmcver) {
		case MMC_CSD_MMCVER_1_0:
		case MMC_CSD_MMCVER_1_4:
			cid->mid = MMC_CID_MID_V1(resp);
			MMC_CID_PNM_V1_CPY(resp, cid->pnm);
			cid->rev = MMC_CID_REV_V1(resp);
			cid->psn = MMC_CID_PSN_V1(resp);
			cid->mdt = MMC_CID_MDT_V1(resp);
			break;
		case MMC_CSD_MMCVER_2_0:
		case MMC_CSD_MMCVER_3_1:
		case MMC_CSD_MMCVER_4_0:
			cid->mid = MMC_CID_MID_V2(resp);
			cid->oid = MMC_CID_OID_V2(resp);
			MMC_CID_PNM_V2_CPY(resp, cid->pnm);
			cid->psn = MMC_CID_PSN_V2(resp);
			break;
		default:
			printf("%s: unknown MMC version %d\n",
			    DEVNAME(sc), sf->csd.mmcver);
			return 1;
		}
	}
	return 0;
}

#ifdef SDMMC_DEBUG
void
sdmmc_print_cid(struct sdmmc_cid *cid)
{
	printf("mid=0x%02x oid=0x%04x pnm=\"%s\" rev=0x%02x psn=0x%08x"
	    " mdt=%03x\n", cid->mid, cid->oid, cid->pnm, cid->rev, cid->psn,
	    cid->mdt);
}
#endif

int
sdmmc_mem_send_scr(struct sdmmc_softc *sc, uint32_t *scr)
{
	struct sdmmc_command cmd;
	void *ptr = NULL;
	int datalen = 8;
	int error = 0;

	ptr = malloc(datalen, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ptr == NULL)
		return ENOMEM;

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_data = ptr;
	cmd.c_datalen = datalen;
	cmd.c_blklen = datalen;
	cmd.c_arg = 0;
	cmd.c_flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1;
	cmd.c_opcode = SD_APP_SEND_SCR;

	error = sdmmc_app_command(sc, &cmd);
	if (error == 0)
		memcpy(scr, ptr, datalen);

	free(ptr, M_DEVBUF, datalen);

	return error;
}

int
sdmmc_mem_decode_scr(struct sdmmc_softc *sc, uint32_t *raw_scr,
    struct sdmmc_function *sf)
{
	sdmmc_response resp;
	int ver;

	memset(resp, 0, sizeof(resp));
	/*
	 * Change the raw SCR to a response.
	 */
	resp[0] = be32toh(raw_scr[1]) >> 8;		// LSW
	resp[1] = be32toh(raw_scr[0]);			// MSW
	resp[0] |= (resp[1] & 0xff) << 24;
	resp[1] >>= 8;

	ver = SCR_STRUCTURE(resp);
	sf->scr.sd_spec = SCR_SD_SPEC(resp);
	sf->scr.bus_width = SCR_SD_BUS_WIDTHS(resp);

	DPRINTF(("%s: %s: %08x%08x ver=%d, spec=%d, bus width=%d\n",
	    DEVNAME(sc), __func__, resp[1], resp[0],
	    ver, sf->scr.sd_spec, sf->scr.bus_width));

	if (ver != 0) {
		DPRINTF(("%s: unknown SCR structure version: %d\n",
		    DEVNAME(sc), ver));
		return EINVAL;
	}
	return 0;
}

int
sdmmc_mem_send_cxd_data(struct sdmmc_softc *sc, int opcode, void *data,
    size_t datalen)
{
	struct sdmmc_command cmd;
	void *ptr = NULL;
	int error = 0;

	ptr = malloc(datalen, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ptr == NULL)
		return ENOMEM;

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_data = ptr;
	cmd.c_datalen = datalen;
	cmd.c_blklen = datalen;
	cmd.c_opcode = opcode;
	cmd.c_arg = 0;
	cmd.c_flags = SCF_CMD_ADTC | SCF_CMD_READ;
	if (opcode == MMC_SEND_EXT_CSD)
		SET(cmd.c_flags, SCF_RSP_R1);
	else
		SET(cmd.c_flags, SCF_RSP_R2);

	error = sdmmc_mmc_command(sc, &cmd);
	if (error == 0)
		memcpy(data, ptr, datalen);

	free(ptr, M_DEVBUF, datalen);

	return error;
}

int
sdmmc_mem_set_bus_width(struct sdmmc_function *sf, int width)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_command cmd;
	int error;

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = SD_APP_SET_BUS_WIDTH;
	cmd.c_flags = SCF_RSP_R1 | SCF_CMD_AC;

	switch (width) {
	case 1:
		cmd.c_arg = SD_ARG_BUS_WIDTH_1;
		break;

	case 4:
		cmd.c_arg = SD_ARG_BUS_WIDTH_4;
		break;

	default:
		return EINVAL;
	}

	error = sdmmc_app_command(sc, &cmd);
	if (error == 0)
		error = sdmmc_chip_bus_width(sc->sct, sc->sch, width);
	return error;
}

int
sdmmc_mem_sd_switch(struct sdmmc_function *sf, int mode, int group,
    int function, sdmmc_bitfield512_t *status)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_command cmd;
	void *ptr = NULL;
	int gsft, error = 0;
	const int statlen = 64;

	if (sf->scr.sd_spec >= SCR_SD_SPEC_VER_1_10 &&
	    !ISSET(sf->csd.ccc, SD_CSD_CCC_SWITCH))
		return EINVAL;

	if (group <= 0 || group > 6 ||
	    function < 0 || function > 15)
		return EINVAL;

	gsft = (group - 1) << 2;

	ptr = malloc(statlen, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ptr == NULL)
		return ENOMEM;

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_data = ptr;
	cmd.c_datalen = statlen;
	cmd.c_blklen = statlen;
	cmd.c_opcode = SD_SEND_SWITCH_FUNC;
	cmd.c_arg =
	    (!!mode << 31) | (function << gsft) | (0x00ffffff & ~(0xf << gsft));
	cmd.c_flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1;

	error = sdmmc_mmc_command(sc, &cmd);
	if (error == 0) {
		memcpy(status, ptr, statlen);
		sdmmc_be512_to_bitfield512(status);
	}

	free(ptr, M_DEVBUF, statlen);

	return error;
}

int
sdmmc_mem_mmc_switch(struct sdmmc_function *sf, uint8_t set, uint8_t index,
    uint8_t value)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_command cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_SWITCH;
	cmd.c_arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
	    (index << 16) | (value << 8) | set;
	cmd.c_flags = SCF_RSP_R1B | SCF_CMD_AC;

	return sdmmc_mmc_command(sc, &cmd);
}

/*
 * Initialize a SD/MMC memory card.
 */
int
sdmmc_mem_init(struct sdmmc_softc *sc, struct sdmmc_function *sf)
{
	int error = 0;

	rw_assert_wrlock(&sc->sc_lock);

	if (sdmmc_select_card(sc, sf) != 0 ||
	    sdmmc_mem_set_blocklen(sc, sf) != 0)
		error = 1;

	if (ISSET(sc->sc_flags, SMF_SD_MODE))
		error = sdmmc_mem_sd_init(sc, sf);
	else
		error = sdmmc_mem_mmc_init(sc, sf);

	return error;
}

/* make 512-bit BE quantity __bitfield()-compatible */
void
sdmmc_be512_to_bitfield512(sdmmc_bitfield512_t *buf) {
	size_t i;
	uint32_t tmp0, tmp1;
	const size_t bitswords = nitems(buf->_bits);
	for (i = 0; i < bitswords/2; i++) {
		tmp0 = buf->_bits[i];
		tmp1 = buf->_bits[bitswords - 1 - i];
		buf->_bits[i] = be32toh(tmp1);
		buf->_bits[bitswords - 1 - i] = be32toh(tmp0);
	}
}

int
sdmmc_mem_select_transfer_mode(struct sdmmc_softc *sc, int support_func)
{
	if (ISSET(sc->sc_flags, SMF_UHS_MODE)) {
		if (ISSET(sc->sc_caps, SMC_CAPS_UHS_SDR104) &&
		    ISSET(support_func, 1 << SD_ACCESS_MODE_SDR104)) {
			return SD_ACCESS_MODE_SDR104;
		}
		if (ISSET(sc->sc_caps, SMC_CAPS_UHS_DDR50) &&
		    ISSET(support_func, 1 << SD_ACCESS_MODE_DDR50)) {
			return SD_ACCESS_MODE_DDR50;
		}
		if (ISSET(sc->sc_caps, SMC_CAPS_UHS_SDR50) &&
		    ISSET(support_func, 1 << SD_ACCESS_MODE_SDR50)) {
			return SD_ACCESS_MODE_SDR50;
		}
	}
	if (ISSET(sc->sc_caps, SMC_CAPS_SD_HIGHSPEED) &&
	    ISSET(support_func, 1 << SD_ACCESS_MODE_SDR25)) {
		return SD_ACCESS_MODE_SDR25;
	}
	return SD_ACCESS_MODE_SDR12;
}

int
sdmmc_mem_execute_tuning(struct sdmmc_softc *sc, struct sdmmc_function *sf)
{
	int timing = -1;

	if (ISSET(sc->sc_flags, SMF_SD_MODE)) {
		if (!ISSET(sc->sc_flags, SMF_UHS_MODE))
			return 0;

		switch (sf->csd.tran_speed) {
		case 100000:
			timing = SDMMC_TIMING_UHS_SDR50;
			break;
		case 208000:
			timing = SDMMC_TIMING_UHS_SDR104;
			break;
		default:
			return 0;
		}
	} else {
		switch (sf->csd.tran_speed) {
		case 200000:
			timing = SDMMC_TIMING_MMC_HS200;
			break;
		default:
			return 0;
		}
	}

	DPRINTF(("%s: execute tuning for timing %d\n", DEVNAME(sc),
	    timing));

	return sdmmc_chip_execute_tuning(sc->sct, sc->sch, timing);
}

int
sdmmc_mem_sd_init(struct sdmmc_softc *sc, struct sdmmc_function *sf)
{
	int support_func, best_func, error, i;
	sdmmc_bitfield512_t status; /* Switch Function Status */
	uint32_t raw_scr[2];

	/*
	 * All SD cards are supposed to support Default Speed mode
	 * with frequencies up to 25 MHz.  Bump up the clock frequency
	 * now as data transfers don't seem to work on the Realtek
	 * RTS5229 host controller if it is running at a low clock
	 * frequency.  Reading the SCR requires a data transfer.
	 */
	error = sdmmc_chip_bus_clock(sc->sct, sc->sch, SDMMC_SDCLK_25MHZ,
	    SDMMC_TIMING_LEGACY);
	if (error) {
		printf("%s: can't change bus clock\n", DEVNAME(sc));
		return error;
	}

	error = sdmmc_mem_send_scr(sc, raw_scr);
	if (error) {
		printf("%s: SD_SEND_SCR send failed\n", DEVNAME(sc));
		return error;
	}
	error = sdmmc_mem_decode_scr(sc, raw_scr, sf);
	if (error)
		return error;

	if (ISSET(sc->sc_caps, SMC_CAPS_4BIT_MODE) &&
	    ISSET(sf->scr.bus_width, SCR_SD_BUS_WIDTHS_4BIT)) {
		DPRINTF(("%s: change bus width\n", DEVNAME(sc)));
		error = sdmmc_mem_set_bus_width(sf, 4);
		if (error) {
			printf("%s: can't change bus width\n", DEVNAME(sc));
			return error;
		}
	}

	best_func = 0;
	if (sf->scr.sd_spec >= SCR_SD_SPEC_VER_1_10 &&
	    ISSET(sf->csd.ccc, SD_CSD_CCC_SWITCH)) {
		DPRINTF(("%s: switch func mode 0\n", DEVNAME(sc)));
		error = sdmmc_mem_sd_switch(sf, 0, 1, 0, &status);
		if (error) {
			printf("%s: switch func mode 0 failed\n", DEVNAME(sc));
			return error;
		}

		support_func = SFUNC_STATUS_GROUP(&status, 1);

		if (!ISSET(sc->sc_flags, SMF_UHS_MODE) &&
		    (ISSET(support_func, 1 << SD_ACCESS_MODE_SDR50) ||
		     ISSET(support_func, 1 << SD_ACCESS_MODE_DDR50) ||
		     ISSET(support_func, 1 << SD_ACCESS_MODE_SDR104))) {
			/* XXX UHS-I card started in 1.8V mode, switch now */
			error = sdmmc_mem_signal_voltage(sc,
			    SDMMC_SIGNAL_VOLTAGE_180);
			if (error) {
				printf("%s: failed to recover UHS card\n", DEVNAME(sc));
				return error;
			}
			SET(sc->sc_flags, SMF_UHS_MODE);
		}

		for (i = 0; i < nitems(switch_group0_functions); i++) {
			if (!(support_func & (1 << i)))
				continue;
			DPRINTF(("%s: card supports mode %s\n",
			    DEVNAME(sc),
			    switch_group0_functions[i].name));
		}

		best_func = sdmmc_mem_select_transfer_mode(sc, support_func);

		DPRINTF(("%s: using mode %s\n", DEVNAME(sc),
		    switch_group0_functions[best_func].name));
	}

	if (best_func != 0) {
		DPRINTF(("%s: switch func mode 1(func=%d)\n",
		    DEVNAME(sc), best_func));
		error =
		    sdmmc_mem_sd_switch(sf, 1, 1, best_func, &status);
		if (error) {
			printf("%s: switch func mode 1 failed:"
			    " group 1 function %d(0x%2x)\n",
			    DEVNAME(sc), best_func, support_func);
			return error;
		}
		sf->csd.tran_speed =
		    switch_group0_functions[best_func].freq;

		/* Wait 400KHz x 8 clock (2.5us * 8 + slop) */
		delay(25);

		/* change bus clock */
		error = sdmmc_chip_bus_clock(sc->sct, sc->sch,
		    sf->csd.tran_speed, SDMMC_TIMING_HIGHSPEED);
		if (error) {
			printf("%s: can't change bus clock\n", DEVNAME(sc));
			return error;
		}

		/* execute tuning (UHS) */
		error = sdmmc_mem_execute_tuning(sc, sf);
		if (error) {
			printf("%s: can't execute SD tuning\n", DEVNAME(sc));
			return error;
		}
	}

	return 0;
}

int
sdmmc_mem_mmc_init(struct sdmmc_softc *sc, struct sdmmc_function *sf)
{
	int width, value;
	int card_type;
	int error = 0;
	u_int8_t ext_csd[512];
	int speed = 20000;
	int timing = SDMMC_TIMING_LEGACY;
	u_int32_t sectors = 0;

	error = sdmmc_chip_bus_clock(sc->sct, sc->sch, speed, timing);
	if (error) {
		printf("%s: can't change bus clock\n", DEVNAME(sc));
		return error;
	}

	if (sf->csd.mmcver >= MMC_CSD_MMCVER_4_0) {
		/* read EXT_CSD */
		error = sdmmc_mem_send_cxd_data(sc,
		    MMC_SEND_EXT_CSD, ext_csd, sizeof(ext_csd));
		if (error != 0) {
			SET(sf->flags, SFF_ERROR);
			printf("%s: can't read EXT_CSD\n", DEVNAME(sc));
			return error;
		}

		card_type = ext_csd[EXT_CSD_CARD_TYPE];

		if (card_type & EXT_CSD_CARD_TYPE_F_HS200_1_8V &&
		    ISSET(sc->sc_caps, SMC_CAPS_MMC_HS200)) {
			speed = 200000;
			timing = SDMMC_TIMING_MMC_HS200;
		} else if (card_type & EXT_CSD_CARD_TYPE_F_DDR52_1_8V &&
		    ISSET(sc->sc_caps, SMC_CAPS_MMC_DDR52)) {
			speed = 52000;
			timing = SDMMC_TIMING_MMC_DDR52;
		} else if (card_type & EXT_CSD_CARD_TYPE_F_52M &&
		    ISSET(sc->sc_caps, SMC_CAPS_MMC_HIGHSPEED)) {
			speed = 52000;
			timing = SDMMC_TIMING_HIGHSPEED;
		} else if (card_type & EXT_CSD_CARD_TYPE_F_26M) {
			speed = 26000;
		} else {
			printf("%s: unknown CARD_TYPE 0x%x\n", DEVNAME(sc),
			    ext_csd[EXT_CSD_CARD_TYPE]);
		}

		if (ISSET(sc->sc_caps, SMC_CAPS_8BIT_MODE)) {
			width = 8;
			value = EXT_CSD_BUS_WIDTH_8;
		} else if (ISSET(sc->sc_caps, SMC_CAPS_4BIT_MODE)) {
			width = 4;
			value = EXT_CSD_BUS_WIDTH_4;
		} else {
			width = 1;
			value = EXT_CSD_BUS_WIDTH_1;
		}

		if (width != 1) {
			error = sdmmc_mem_mmc_switch(sf, EXT_CSD_CMD_SET_NORMAL,
			    EXT_CSD_BUS_WIDTH, value);
			if (error == 0)
				error = sdmmc_chip_bus_width(sc->sct,
				    sc->sch, width);
			else {
				DPRINTF(("%s: can't change bus width"
				    " (%d bit)\n", DEVNAME(sc), width));
				return error;
			}

			/* XXXX: need bus test? (using by CMD14 & CMD19) */
			sdmmc_delay(10000);
		}

		if (timing != SDMMC_TIMING_LEGACY) {
			switch (timing) {
			case SDMMC_TIMING_MMC_HS200:
				value = EXT_CSD_HS_TIMING_HS200;
				break;
			case SDMMC_TIMING_MMC_DDR52:
			case SDMMC_TIMING_HIGHSPEED:
				value = EXT_CSD_HS_TIMING_HS;
				break;
			}

			/* switch to high speed timing */
			error = sdmmc_mem_mmc_switch(sf, EXT_CSD_CMD_SET_NORMAL,
			    EXT_CSD_HS_TIMING, value);
			if (error != 0) {
				printf("%s: can't change timing\n",
				    DEVNAME(sc));
				return error;
			}

			sdmmc_delay(10000);
		}

		KASSERT(timing < nitems(sdmmc_mmc_timings));
		sf->csd.tran_speed = sdmmc_mmc_timings[timing];

		if (timing != SDMMC_TIMING_LEGACY) {
			/* read EXT_CSD again */
			error = sdmmc_mem_send_cxd_data(sc,
			    MMC_SEND_EXT_CSD, ext_csd, sizeof(ext_csd));
			if (error != 0) {
				printf("%s: can't re-read EXT_CSD\n", DEVNAME(sc));
				return error;
			}
			if (ext_csd[EXT_CSD_HS_TIMING] != value) {
				printf("%s, HS_TIMING set failed\n", DEVNAME(sc));
				return EINVAL;
			}
		}

		error = sdmmc_chip_bus_clock(sc->sct, sc->sch, speed, SDMMC_TIMING_HIGHSPEED);
		if (error != 0) {
			printf("%s: can't change bus clock\n", DEVNAME(sc));
			return error;
		}

		if (timing == SDMMC_TIMING_MMC_DDR52) {
			switch (width) {
			case 4:
				value = EXT_CSD_BUS_WIDTH_4_DDR;
				break;
			case 8:
				value = EXT_CSD_BUS_WIDTH_8_DDR;
				break;
			}

			error = sdmmc_mem_mmc_switch(sf, EXT_CSD_CMD_SET_NORMAL,
			    EXT_CSD_BUS_WIDTH, value);
			if (error) {
				printf("%s: can't switch to DDR\n",
				    DEVNAME(sc));
				return error;
			}

			sdmmc_delay(10000);

			error = sdmmc_chip_signal_voltage(sc->sct, sc->sch,
			    SDMMC_SIGNAL_VOLTAGE_180);
			if (error) {
				printf("%s: can't switch signalling voltage\n",
				    DEVNAME(sc));
				return error;
			}

			error = sdmmc_chip_bus_clock(sc->sct, sc->sch, speed, timing);
			if (error != 0) {
				printf("%s: can't change bus clock\n", DEVNAME(sc));
				return error;
			}

			sdmmc_delay(10000);
		}

		sectors = ext_csd[EXT_CSD_SEC_COUNT + 0] << 0 |
		    ext_csd[EXT_CSD_SEC_COUNT + 1] << 8  |
		    ext_csd[EXT_CSD_SEC_COUNT + 2] << 16 |
		    ext_csd[EXT_CSD_SEC_COUNT + 3] << 24;

		if (sectors > (2u * 1024 * 1024 * 1024) / 512) {
			sf->flags |= SFF_SDHC;
			sf->csd.capacity = sectors;
		}

		if (timing == SDMMC_TIMING_MMC_HS200) {
			/* execute tuning (HS200) */
			error = sdmmc_mem_execute_tuning(sc, sf);
			if (error) {
				printf("%s: can't execute MMC tuning\n", DEVNAME(sc));
				return error;
			}
		}
	}

	return error;
}

/*
 * Get or set the card's memory OCR value (SD or MMC).
 */
int
sdmmc_mem_send_op_cond(struct sdmmc_softc *sc, u_int32_t ocr,
    u_int32_t *ocrp)
{
	struct sdmmc_command cmd;
	int error;
	int i;

	rw_assert_wrlock(&sc->sc_lock);

	/*
	 * If we change the OCR value, retry the command until the OCR
	 * we receive in response has the "CARD BUSY" bit set, meaning
	 * that all cards are ready for identification.
	 */
	for (i = 0; i < 100; i++) {
		bzero(&cmd, sizeof cmd);
		cmd.c_arg = ocr;
		cmd.c_flags = SCF_CMD_BCR | SCF_RSP_R3;

		if (ISSET(sc->sc_flags, SMF_SD_MODE)) {
			cmd.c_opcode = SD_APP_OP_COND;
			error = sdmmc_app_command(sc, &cmd);
		} else {
			cmd.c_arg &= ~MMC_OCR_ACCESS_MODE_MASK;
			cmd.c_arg |= MMC_OCR_ACCESS_MODE_SECTOR;
			cmd.c_opcode = MMC_SEND_OP_COND;
			error = sdmmc_mmc_command(sc, &cmd);
		}
		if (error != 0)
			break;
		if (ISSET(MMC_R3(cmd.c_resp), MMC_OCR_MEM_READY) ||
		    ocr == 0)
			break;
		error = ETIMEDOUT;
		sdmmc_delay(10000);
	}
	if (error == 0 && ocrp != NULL)
		*ocrp = MMC_R3(cmd.c_resp);

	return error;
}

/*
 * Set the read block length appropriately for this card, according to
 * the card CSD register value.
 */
int
sdmmc_mem_set_blocklen(struct sdmmc_softc *sc, struct sdmmc_function *sf)
{
	struct sdmmc_command cmd;

	rw_assert_wrlock(&sc->sc_lock);

	bzero(&cmd, sizeof cmd);
	cmd.c_opcode = MMC_SET_BLOCKLEN;
	cmd.c_arg = sf->csd.sector_size;
	cmd.c_flags = SCF_CMD_AC | SCF_RSP_R1;
	DPRINTF(("%s: read_bl_len=%d sector_size=%d\n", DEVNAME(sc),
	    1 << sf->csd.read_bl_len, sf->csd.sector_size));

	return sdmmc_mmc_command(sc, &cmd);
}

int
sdmmc_mem_read_block_subr(struct sdmmc_function *sf, bus_dmamap_t dmap,
    int blkno, u_char *data, size_t datalen)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_command cmd;
	int error;


	if ((error = sdmmc_select_card(sc, sf)) != 0)
		goto err;

	bzero(&cmd, sizeof cmd);
	cmd.c_data = data;
	cmd.c_datalen = datalen;
	cmd.c_blklen = sf->csd.sector_size;
	cmd.c_opcode = (datalen / cmd.c_blklen) > 1 ?
	    MMC_READ_BLOCK_MULTIPLE : MMC_READ_BLOCK_SINGLE;
	if (sf->flags & SFF_SDHC)
		cmd.c_arg = blkno;
	else
		cmd.c_arg = blkno << 9;
	cmd.c_flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1;
	cmd.c_dmamap = dmap;

	error = sdmmc_mmc_command(sc, &cmd);
	if (error != 0)
		goto err;

	if (ISSET(sc->sc_flags, SMF_STOP_AFTER_MULTIPLE) &&
	    cmd.c_opcode == MMC_READ_BLOCK_MULTIPLE) {
		bzero(&cmd, sizeof cmd);
		cmd.c_opcode = MMC_STOP_TRANSMISSION;
		cmd.c_arg = MMC_ARG_RCA(sf->rca);
		cmd.c_flags = SCF_CMD_AC | SCF_RSP_R1B;
		error = sdmmc_mmc_command(sc, &cmd);
		if (error != 0)
			goto err;
	}

	do {
		bzero(&cmd, sizeof cmd);
		cmd.c_opcode = MMC_SEND_STATUS;
		cmd.c_arg = MMC_ARG_RCA(sf->rca);
		cmd.c_flags = SCF_CMD_AC | SCF_RSP_R1;
		error = sdmmc_mmc_command(sc, &cmd);
		if (error != 0)
			break;
		/* XXX time out */
	} while (!ISSET(MMC_R1(cmd.c_resp), MMC_R1_READY_FOR_DATA));

err:
	return (error);
}

int
sdmmc_mem_single_read_block(struct sdmmc_function *sf, int blkno, u_char *data,
    size_t datalen)
{
	int error = 0;
	int i;

	for (i = 0; i < datalen / sf->csd.sector_size; i++) {
		error = sdmmc_mem_read_block_subr(sf, NULL,  blkno + i,
		    data + i * sf->csd.sector_size, sf->csd.sector_size);
		if (error)
			break;
	}

	return (error);
}

int
sdmmc_mem_read_block(struct sdmmc_function *sf, int blkno, u_char *data,
    size_t datalen)
{
	struct sdmmc_softc *sc = sf->sc;
	int error;

	rw_enter_write(&sc->sc_lock);

	if (ISSET(sc->sc_caps, SMC_CAPS_SINGLE_ONLY)) {
		error = sdmmc_mem_single_read_block(sf, blkno, data, datalen);
		goto out;
	}

	if (!ISSET(sc->sc_caps, SMC_CAPS_DMA)) {
		error = sdmmc_mem_read_block_subr(sf, NULL, blkno,
		    data, datalen);
		goto out;
	}

	/* DMA transfer */
	error = bus_dmamap_load(sc->sc_dmat, sc->sc_dmap, data, datalen,
	    NULL, BUS_DMA_NOWAIT|BUS_DMA_READ);
	if (error)
		goto out;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, 0, datalen,
	    BUS_DMASYNC_PREREAD);

	error = sdmmc_mem_read_block_subr(sf, sc->sc_dmap, blkno, data,
	    datalen);
	if (error)
		goto unload;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, 0, datalen,
	    BUS_DMASYNC_POSTREAD);
unload:
	bus_dmamap_unload(sc->sc_dmat, sc->sc_dmap);

out:
	rw_exit(&sc->sc_lock);
	return (error);
}

int
sdmmc_mem_write_block_subr(struct sdmmc_function *sf, bus_dmamap_t dmap,
    int blkno, u_char *data, size_t datalen)
{
	struct sdmmc_softc *sc = sf->sc;
	struct sdmmc_command cmd;
	int error;

	if ((error = sdmmc_select_card(sc, sf)) != 0)
		goto err;

	bzero(&cmd, sizeof cmd);
	cmd.c_data = data;
	cmd.c_datalen = datalen;
	cmd.c_blklen = sf->csd.sector_size;
	cmd.c_opcode = (datalen / cmd.c_blklen) > 1 ?
	    MMC_WRITE_BLOCK_MULTIPLE : MMC_WRITE_BLOCK_SINGLE;
	if (sf->flags & SFF_SDHC)
		cmd.c_arg = blkno;
	else
		cmd.c_arg = blkno << 9;
	cmd.c_flags = SCF_CMD_ADTC | SCF_RSP_R1;
	cmd.c_dmamap = dmap;

	error = sdmmc_mmc_command(sc, &cmd);
	if (error != 0)
		goto err;

	if (ISSET(sc->sc_flags, SMF_STOP_AFTER_MULTIPLE) &&
	    cmd.c_opcode == MMC_WRITE_BLOCK_MULTIPLE) {
		bzero(&cmd, sizeof cmd);
		cmd.c_opcode = MMC_STOP_TRANSMISSION;
		cmd.c_flags = SCF_CMD_AC | SCF_RSP_R1B;
		error = sdmmc_mmc_command(sc, &cmd);
		if (error != 0)
			goto err;
	}

	do {
		bzero(&cmd, sizeof cmd);
		cmd.c_opcode = MMC_SEND_STATUS;
		cmd.c_arg = MMC_ARG_RCA(sf->rca);
		cmd.c_flags = SCF_CMD_AC | SCF_RSP_R1;
		error = sdmmc_mmc_command(sc, &cmd);
		if (error != 0)
			break;
		/* XXX time out */
	} while (!ISSET(MMC_R1(cmd.c_resp), MMC_R1_READY_FOR_DATA));

err:
	return (error);
}

int
sdmmc_mem_single_write_block(struct sdmmc_function *sf, int blkno, u_char *data,
    size_t datalen)
{
	int error = 0;
	int i;

	for (i = 0; i < datalen / sf->csd.sector_size; i++) {
		error = sdmmc_mem_write_block_subr(sf, NULL, blkno + i,
		    data + i * sf->csd.sector_size, sf->csd.sector_size);
		if (error)
			break;
	}

	return (error);
}

int
sdmmc_mem_write_block(struct sdmmc_function *sf, int blkno, u_char *data,
    size_t datalen)
{
	struct sdmmc_softc *sc = sf->sc;
	int error;

	rw_enter_write(&sc->sc_lock);

	if (ISSET(sc->sc_caps, SMC_CAPS_SINGLE_ONLY)) {
		error = sdmmc_mem_single_write_block(sf, blkno, data, datalen);
		goto out;
	}

	if (!ISSET(sc->sc_caps, SMC_CAPS_DMA)) {
		error = sdmmc_mem_write_block_subr(sf, NULL, blkno,
		    data, datalen);
		goto out;
	}

	/* DMA transfer */
	error = bus_dmamap_load(sc->sc_dmat, sc->sc_dmap, data, datalen,
	    NULL, BUS_DMA_NOWAIT|BUS_DMA_WRITE);
	if (error)
		goto out;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, 0, datalen,
	    BUS_DMASYNC_PREWRITE);

	error = sdmmc_mem_write_block_subr(sf, sc->sc_dmap, blkno, data,
	    datalen);
	if (error)
		goto unload;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, 0, datalen,
	    BUS_DMASYNC_POSTWRITE);
unload:
	bus_dmamap_unload(sc->sc_dmat, sc->sc_dmap);

out:
	rw_exit(&sc->sc_lock);
	return (error);
}

#ifdef HIBERNATE
int
sdmmc_mem_hibernate_write(struct sdmmc_function *sf, daddr_t blkno,
    u_char *data, size_t datalen)
{
	struct sdmmc_softc *sc = sf->sc;
	int i, error;
	struct bus_dmamap dmamap;
	paddr_t phys_addr;

	if (ISSET(sc->sc_caps, SMC_CAPS_SINGLE_ONLY)) {
		for (i = 0; i < datalen / sf->csd.sector_size; i++) {
			error = sdmmc_mem_write_block_subr(sf, NULL, blkno + i,
			    data + i * sf->csd.sector_size,
			    sf->csd.sector_size);
			if (error)
				return (error);
		}
	} else if (!ISSET(sc->sc_caps, SMC_CAPS_DMA)) {
		return (sdmmc_mem_write_block_subr(sf, NULL, blkno, data,
		    datalen));
	}

	/* pretend we're bus_dmamap_load */
	bzero(&dmamap, sizeof(dmamap));
	pmap_extract(pmap_kernel(), (vaddr_t)data, &phys_addr);
	dmamap.dm_mapsize = datalen;
	dmamap.dm_nsegs = 1;
	dmamap.dm_segs[0].ds_addr = phys_addr;
	dmamap.dm_segs[0].ds_len = datalen;
	return (sdmmc_mem_write_block_subr(sf, &dmamap, blkno, data, datalen));
}
#endif
