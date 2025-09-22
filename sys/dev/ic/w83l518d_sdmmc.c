/*	$OpenBSD: w83l518d_sdmmc.c,v 1.6 2024/09/01 03:08:56 jsg Exp $	*/
/*	$NetBSD: w83l518d_sdmmc.c,v 1.1 2009/09/30 20:44:50 jmcneill Exp $ */

/*
 * Copyright (c) 2009 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/proc.h>

#include <machine/bus.h>

#include <dev/sdmmc/sdmmcvar.h>
#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmc_ioreg.h>

#include <dev/ic/w83l518dreg.h>
#include <dev/ic/w83l518dvar.h>
#include <dev/ic/w83l518d_sdmmc.h>

/* #define WB_SDMMC_DEBUG */

#ifdef WB_SDMMC_DEBUG
static int wb_sdmmc_debug = 1;
#else
static int wb_sdmmc_debug = 0;
#endif

#define REPORT(_wb, ...)					\
	if (wb_sdmmc_debug > 0)					\
		printf(__VA_ARGS__)

int	wb_sdmmc_host_reset(sdmmc_chipset_handle_t);
uint32_t	wb_sdmmc_host_ocr(sdmmc_chipset_handle_t);
int	wb_sdmmc_host_maxblklen(sdmmc_chipset_handle_t);
int	wb_sdmmc_card_detect(sdmmc_chipset_handle_t);
#ifdef notyet
int	wb_sdmmc_write_protect(sdmmc_chipset_handle_t);
#endif
int	wb_sdmmc_bus_power(sdmmc_chipset_handle_t, uint32_t);
int	wb_sdmmc_bus_clock(sdmmc_chipset_handle_t, int, int);
int	wb_sdmmc_bus_width(sdmmc_chipset_handle_t, int);
void	wb_sdmmc_exec_command(sdmmc_chipset_handle_t,
			      struct sdmmc_command *);
void	wb_sdmmc_card_intr_mask(sdmmc_chipset_handle_t, int);
void	wb_sdmmc_card_intr_ack(sdmmc_chipset_handle_t);

struct sdmmc_chip_functions wb_sdmmc_chip_functions = {
	/* host controller reset */
	wb_sdmmc_host_reset,
	/* host controller capabilities */
	wb_sdmmc_host_ocr,
	wb_sdmmc_host_maxblklen,
	/* card detection */
	wb_sdmmc_card_detect,
#ifdef notyet
	.write_protect = wb_sdmmc_write_protect,
#endif
	/* bus power and clock frequency */
	wb_sdmmc_bus_power,
	wb_sdmmc_bus_clock,
	wb_sdmmc_bus_width,
	/* command execution */
	wb_sdmmc_exec_command,
	/* card interrupt */
	wb_sdmmc_card_intr_mask,
	wb_sdmmc_card_intr_ack
};

void	wb_sdmmc_read_data(struct wb_softc *, uint8_t *, int);
void	wb_sdmmc_write_data(struct wb_softc *, uint8_t *, int);
void	wb_sdmmc_discover(void *);
int	wb_sdmmc_enable(struct wb_softc *);
int	wb_sdmmc_disable(struct wb_softc *);
int	wb_sdmmc_transfer_data(struct wb_softc *, struct sdmmc_command *);
void	wb_sdmmc_rsp_read_long(struct wb_softc *, struct sdmmc_command *);
void	wb_sdmmc_rsp_read_short(struct wb_softc *, struct sdmmc_command *);

void
wb_sdmmc_read_data(struct wb_softc *wb, uint8_t *data, int len)
{
	bus_space_read_multi_1(wb->wb_iot, wb->wb_ioh, WB_SD_FIFO, data, len);
}

void
wb_sdmmc_write_data(struct wb_softc *wb, uint8_t *data, int len)
{
	bus_space_write_multi_1(wb->wb_iot, wb->wb_ioh, WB_SD_FIFO, data, len);
}

void
wb_sdmmc_discover(void *opaque)
{
	struct wb_softc *wb = opaque;

	REPORT(wb, "TRACE: discover(wb)\n");

	sdmmc_needs_discover(wb->wb_sdmmc_dev);
}

int
wb_sdmmc_enable(struct wb_softc *wb)
{
	int i = 5000;

	REPORT(wb, "TRACE: enable(wb)\n");

	/* put the device in a known state */
	wb_idx_write(wb, WB_INDEX_SETUP, WB_SETUP_SOFT_RST);
	while (--i > 0 && wb_idx_read(wb, WB_INDEX_SETUP) & WB_SETUP_SOFT_RST)
		delay(10);
	if (i == 0) {
		printf("%s: timeout resetting device\n", wb->wb_dev.dv_xname);
		return 0;
	}
	wb_idx_write(wb, WB_INDEX_CLK, WB_CLK_375K);
	wb_idx_write(wb, WB_INDEX_FIFOEN, 0);
	wb_idx_write(wb, WB_INDEX_DMA, 0);
	wb_idx_write(wb, WB_INDEX_PBSMSB, 0);
	wb_idx_write(wb, WB_INDEX_PBSLSB, 0);
	/* drain FIFO */
	while ((wb_read(wb, WB_SD_FIFOSTS) & WB_FIFO_EMPTY) == 0)
		wb_read(wb, WB_SD_FIFO);

	wb_write(wb, WB_SD_CSR, 0);

	wb_write(wb, WB_SD_INTCTL, WB_INT_DEFAULT);

	wb_sdmmc_card_detect(wb);

	return 1;
}

int
wb_sdmmc_disable(struct wb_softc *wb)
{
	uint8_t val;

	REPORT(wb, "TRACE: disable(wb)\n");

	val = wb_read(wb, WB_SD_CSR);
	val |= WB_CSR_POWER_N;
	wb_write(wb, WB_SD_CSR, val);

	return 1;
}

void
wb_sdmmc_attach(struct wb_softc *wb)
{
	struct sdmmcbus_attach_args saa;

	timeout_set(&wb->wb_sdmmc_to, wb_sdmmc_discover, wb);

	wb->wb_sdmmc_width = 1;

	if (wb_sdmmc_enable(wb) == 0)
		return;

	memset(&saa, 0, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.sct = &wb_sdmmc_chip_functions;
	saa.sch = wb;
	saa.flags = SMF_STOP_AFTER_MULTIPLE;

	wb->wb_sdmmc_dev = config_found(&wb->wb_dev, &saa, NULL);
}

int
wb_sdmmc_detach(struct wb_softc *wb, int flags)
{
	int rv;

	if (wb->wb_sdmmc_dev) {
		rv = config_detach(wb->wb_sdmmc_dev, flags);
		if (rv)
			return rv;
	}
	wb_sdmmc_disable(wb);

	timeout_del(&wb->wb_sdmmc_to);

	return 0;
}

/*
 * SD/MMC interface
 */
int
wb_sdmmc_host_reset(sdmmc_chipset_handle_t sch)
{
	REPORT(sch, "TRACE: sdmmc/host_reset(wb)\n");

	return 0;
}

uint32_t
wb_sdmmc_host_ocr(sdmmc_chipset_handle_t sch)
{
	REPORT(sch, "TRACE: sdmmc/host_ocr(wb)\n");

	return MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V;
}

int
wb_sdmmc_host_maxblklen(sdmmc_chipset_handle_t sch)
{
	REPORT(sch, "TRACE: sdmmc/host_maxblklen(wb)\n");

	return 512;	/* XXX */
}

int
wb_sdmmc_card_detect(sdmmc_chipset_handle_t sch)
{
	struct wb_softc *wb = sch;
	int rv;

	wb_led(wb, 1);
	rv = (wb_read(wb, WB_SD_CSR) & WB_CSR_CARD_PRESENT) ? 1 : 0;
	wb_led(wb, 0);

	REPORT(wb, "TRACE: sdmmc/card_detect(wb) -> %d\n", rv);

	return rv;
}

#ifdef notyet
int
wb_sdmmc_write_protect(sdmmc_chipset_handle_t sch)
{
	struct wb_softc *wb = sch;
	int rv;

	wb_led(wb, 1);
	rv = (wb_read(wb, WB_SD_CSR) & WB_CSR_WRITE_PROTECT) ? 1 : 0;
	wb_led(wb, 0);

	REPORT(wb, "TRACE: sdmmc/write_protect(wb) -> %d\n", rv);

	return rv;
}
#endif

int
wb_sdmmc_bus_power(sdmmc_chipset_handle_t sch, uint32_t ocr)
{
	REPORT(sch, "TRACE: sdmmc/bus_power(wb, ocr=%d)\n", ocr);

	return 0;
}

int
wb_sdmmc_bus_clock(sdmmc_chipset_handle_t sch, int freq, int timing)
{
	struct wb_softc *wb = sch;
	uint8_t clk;

	REPORT(wb, "TRACE: sdmmc/bus_clock(wb, freq=%d)\n", freq);

	if (freq >= 24000)
		clk = WB_CLK_24M;
	else if (freq >= 16000)
		clk = WB_CLK_16M;
	else if (freq >= 12000)
		clk = WB_CLK_12M;
	else
		clk = WB_CLK_375K;

	if (wb_idx_read(wb, WB_INDEX_CLK) != clk)
		wb_idx_write(wb, WB_INDEX_CLK, clk);

	return 0;
}

int
wb_sdmmc_bus_width(sdmmc_chipset_handle_t sch, int width)
{
	struct wb_softc *wb = sch;

	REPORT(wb, "TRACE: sdmmc/bus_width(wb, width=%d)\n", width);

	if (width != 1 && width != 4)
		return 1;

	wb->wb_sdmmc_width = width;

	return 0;
}

void
wb_sdmmc_rsp_read_long(struct wb_softc *wb, struct sdmmc_command *cmd)
{
	uint8_t *p = (uint8_t *)cmd->c_resp;
	int i;

	if (wb_idx_read(wb, WB_INDEX_RESPLEN) != 1) {
		cmd->c_error = ENXIO;
		return;
	}

	for (i = 12; i >= 0; i -= 4) {
		p[0] = wb_idx_read(wb, WB_INDEX_RESP(i + 0));
		p[1] = wb_idx_read(wb, WB_INDEX_RESP(i + 1));
		p[2] = wb_idx_read(wb, WB_INDEX_RESP(i + 2));
		p[3] = wb_idx_read(wb, WB_INDEX_RESP(i + 3));
		p += 4;
	}
}

void
wb_sdmmc_rsp_read_short(struct wb_softc *wb, struct sdmmc_command *cmd)
{
	uint8_t *p = (uint8_t *)cmd->c_resp;

	if (wb_idx_read(wb, WB_INDEX_RESPLEN) != 0) {
		cmd->c_error = ENXIO;
		return;
	}

#if BYTE_ORDER == LITTLE_ENDIAN
	p[3] = wb_idx_read(wb, WB_INDEX_RESP(12));
	p[2] = wb_idx_read(wb, WB_INDEX_RESP(13));
	p[1] = wb_idx_read(wb, WB_INDEX_RESP(14));
	p[0] = wb_idx_read(wb, WB_INDEX_RESP(15));
#else
	p[0] = wb_idx_read(wb, WB_INDEX_RESP(12));
	p[1] = wb_idx_read(wb, WB_INDEX_RESP(13));
	p[2] = wb_idx_read(wb, WB_INDEX_RESP(14));
	p[3] = wb_idx_read(wb, WB_INDEX_RESP(15));
#endif
}

int
wb_sdmmc_transfer_data(struct wb_softc *wb, struct sdmmc_command *cmd)
{
	uint8_t fifosts;
	int datalen, retry = 5000;

	if (wb->wb_sdmmc_intsts & WB_INT_CARD)
		return EIO;

	fifosts = wb_read(wb, WB_SD_FIFOSTS);
	if (ISSET(cmd->c_flags, SCF_CMD_READ)) {
		if (fifosts & WB_FIFO_EMPTY) {
			while (--retry > 0) {
				fifosts = wb_read(wb, WB_SD_FIFOSTS);
				if ((fifosts & WB_FIFO_EMPTY) == 0)
					break;
				delay(100);
			}
			if (retry == 0)
				return EBUSY;
		}

		if (fifosts & WB_FIFO_FULL)
			datalen = 16;
		else
			datalen = fifosts & WB_FIFO_DEPTH_MASK;
	} else {
		if (fifosts & WB_FIFO_FULL) {
			while (--retry > 0) {
				fifosts = wb_read(wb, WB_SD_FIFOSTS);
				if ((fifosts & WB_FIFO_FULL) == 0)
					break;
				delay(100);
			}
			if (retry == 0)
				return EBUSY;
		}

		if (fifosts & WB_FIFO_EMPTY)
			datalen = 16;
		else
			datalen = 16 - (fifosts & WB_FIFO_DEPTH_MASK);
	}

	datalen = MIN(datalen, cmd->c_resid);
	if (datalen > 0) {
		if (ISSET(cmd->c_flags, SCF_CMD_READ))
			wb_sdmmc_read_data(wb, cmd->c_buf, datalen);
		else
			wb_sdmmc_write_data(wb, cmd->c_buf, datalen);

		cmd->c_buf += datalen;
		cmd->c_resid -= datalen;
	}

	return 0;
}

void
wb_sdmmc_exec_command(sdmmc_chipset_handle_t sch, struct sdmmc_command *cmd)
{
	static const int opcodes[] = {
		11, 17, 18, 20, 24, 25, 26, 27, 30, 42, 51, 56
	};
	struct wb_softc *wb = sch;
	uint8_t val;
	int blklen;
	int error;
	int i, retry;
	int s;

	REPORT(wb, "TRACE: sdmmc/exec_command(wb, cmd) "
	    "opcode %d flags 0x%x data %p datalen %d\n",
	    cmd->c_opcode, cmd->c_flags, cmd->c_data, cmd->c_datalen);

	if (cmd->c_datalen > 0) {
		/* controller only supports a select number of data opcodes */
		for (i = 0; i < nitems(opcodes); i++)
			if (opcodes[i] == cmd->c_opcode)
				break;
		if (i == nitems(opcodes)) {
			cmd->c_error = EINVAL;
			goto done;
		}

		/* Fragment the data into proper blocks */
		blklen = MIN(cmd->c_datalen, cmd->c_blklen);

		if (cmd->c_datalen % blklen > 0) {
			printf("%s: data is not a multiple of %u bytes\n",
			    wb->wb_dev.dv_xname, blklen);
			cmd->c_error = EINVAL;
			goto done;
		}

		/* setup block size registers */
		blklen = blklen + 2 * wb->wb_sdmmc_width;
		wb_idx_write(wb, WB_INDEX_PBSMSB,
		    ((blklen >> 4) & 0xf0) | (wb->wb_sdmmc_width / 4));
		wb_idx_write(wb, WB_INDEX_PBSLSB, blklen & 0xff);

		/* clear FIFO */
		val = wb_idx_read(wb, WB_INDEX_SETUP);
		val |= WB_SETUP_FIFO_RST;
		wb_idx_write(wb, WB_INDEX_SETUP, val);
		while (wb_idx_read(wb, WB_INDEX_SETUP) & WB_SETUP_FIFO_RST)
			;

		cmd->c_resid = cmd->c_datalen;
		cmd->c_buf = cmd->c_data;

		/* setup FIFO thresholds */
		if (ISSET(cmd->c_flags, SCF_CMD_READ))
			wb_idx_write(wb, WB_INDEX_FIFOEN, WB_FIFOEN_FULL | 8);
		else {
			wb_idx_write(wb, WB_INDEX_FIFOEN, WB_FIFOEN_EMPTY | 8);

			/* pre-fill the FIFO on write */
			error = wb_sdmmc_transfer_data(wb, cmd);
			if (error) {
				cmd->c_error = error;
				goto done;
			}
		}
	}

	s = splsdmmc();
	wb->wb_sdmmc_intsts = 0;
	wb_write(wb, WB_SD_COMMAND, cmd->c_opcode);
	wb_write(wb, WB_SD_COMMAND, (cmd->c_arg >> 24) & 0xff);
	wb_write(wb, WB_SD_COMMAND, (cmd->c_arg >> 16) & 0xff);
	wb_write(wb, WB_SD_COMMAND, (cmd->c_arg >> 8) & 0xff);
	wb_write(wb, WB_SD_COMMAND, (cmd->c_arg >> 0) & 0xff);
	splx(s);

	retry = 100000;
	while (wb_idx_read(wb, WB_INDEX_STATUS) & WB_STATUS_CARD_TRAFFIC) {
		if (--retry == 0)
			break;
		delay(1);
	}
	if (wb_idx_read(wb, WB_INDEX_STATUS) & WB_STATUS_CARD_TRAFFIC) {
		REPORT(wb,
		    "command timed out, WB_INDEX_STATUS = 0x%02x\n",
		    wb_idx_read(wb, WB_INDEX_STATUS));
		cmd->c_error = ETIMEDOUT;
		goto done;
	}

	if (ISSET(cmd->c_flags, SCF_RSP_PRESENT)) {
		if (wb->wb_sdmmc_intsts & WB_INT_TIMEOUT) {
			cmd->c_error = ETIMEDOUT;
			goto done;
		}

		if (ISSET(cmd->c_flags, SCF_RSP_136))
			wb_sdmmc_rsp_read_long(wb, cmd);
		else
			wb_sdmmc_rsp_read_short(wb, cmd);
	}

	if (cmd->c_error == 0 && cmd->c_datalen > 0) {
		wb_led(wb, 1);
		while (cmd->c_resid > 0) {
			error = wb_sdmmc_transfer_data(wb, cmd);
			if (error) {
				cmd->c_error = error;
				break;
			}
		}
		wb_led(wb, 0);
	}

done:
	SET(cmd->c_flags, SCF_ITSDONE);

	if (cmd->c_error) {
		REPORT(wb,
		    "cmd error = %d, op = %d [%s] "
		    "blklen %d datalen %d resid %d\n",
		    cmd->c_error, cmd->c_opcode,
		    ISSET(cmd->c_flags, SCF_CMD_READ) ? "rd" : "wr",
		    cmd->c_blklen, cmd->c_datalen, cmd->c_resid);
	}
}
				      
void
wb_sdmmc_card_intr_mask(sdmmc_chipset_handle_t sch, int enable)
{
	REPORT(sch, "TRACE: sdmmc/card_enable_intr(wb, enable=%d)\n", enable);
}

void
wb_sdmmc_card_intr_ack(sdmmc_chipset_handle_t sch)
{
	REPORT(sch, "TRACE: sdmmc/card_intr_ack(wb)\n");
}

/*
 * intr handler 
 */
int
wb_sdmmc_intr(struct wb_softc *wb)
{
	uint8_t val;

	val = wb_read(wb, WB_SD_INTSTS);
	if (val == 0xff || val == 0x00)
		return 0;

	if (wb->wb_sdmmc_dev == NULL)
		return 1;

	wb->wb_sdmmc_intsts |= val;

	REPORT(wb, "WB_SD_INTSTS = %b\n", val,
	    "\20\1TC\2BUSYEND\3PROGEND\4TIMEOUT"
	    "\5CRC\6FIFO\7CARD\010PENDING");

	if (val & WB_INT_CARD)
		timeout_add_msec(&wb->wb_sdmmc_to, 250);

	return 1;
}
