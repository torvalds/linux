/*-
 * Copyright (c) 2017 Ilya Bakulin
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "cam_sdio.h"

/* Use CMD52 to read or write a single byte */
int
sdio_rw_direct(struct cam_device *dev,
	       uint8_t func_number,
	       uint32_t addr,
	       uint8_t is_write,
	       uint8_t *data, uint8_t *resp) {
	union ccb *ccb;
	uint32_t flags;
	uint32_t arg;
	int retval = 0;

	ccb = cam_getccb(dev);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		return (-1);
	}
	bzero(&(&ccb->ccb_h)[1],
	      sizeof(union ccb) - sizeof(struct ccb_hdr));

	flags = MMC_RSP_R5 | MMC_CMD_AC;
	arg = SD_IO_RW_FUNC(func_number) | SD_IO_RW_ADR(addr);
	if (is_write)
		arg |= SD_IO_RW_WR | SD_IO_RW_RAW | SD_IO_RW_DAT(*data);

	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ 0,
		       /*cbfcnp*/ NULL,
		       /*flags*/ CAM_DIR_NONE,
		       /*mmc_opcode*/ SD_IO_RW_DIRECT,
		       /*mmc_arg*/ arg,
		       /*mmc_flags*/ flags,
		       /*mmc_data*/ 0,
		       /*timeout*/ 5000);

	if (((retval = cam_send_ccb(dev, ccb)) < 0)
	    || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);
		return (-1);
	}

	*resp = ccb->mmcio.cmd.resp[0] & 0xFF;
	cam_freeccb(ccb);
	return (retval);
}

/*
 * CMD53 -- IO_RW_EXTENDED
 * Use to read or write memory blocks
 *
 * is_increment=1: FIFO mode
 * blk_count > 0: block mode
 */
int
sdio_rw_extended(struct cam_device *dev,
		 uint8_t func_number,
		 uint32_t addr,
		 uint8_t is_write,
		 caddr_t data, size_t datalen,
		 uint8_t is_increment,
		 uint16_t blk_count) {
	union ccb *ccb;
	uint32_t flags;
	uint32_t arg;
	uint32_t cam_flags;
	uint8_t resp;
	struct mmc_data mmcd;
	int retval = 0;

	if (blk_count != 0) {
		warnx("%s: block mode is not supported yet", __func__);
		return (-1);
	}

	ccb = cam_getccb(dev);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		return (-1);
	}
	bzero(&(&ccb->ccb_h)[1],
	      sizeof(union ccb) - sizeof(struct ccb_hdr));

	flags = MMC_RSP_R5 | MMC_CMD_ADTC;
	arg = SD_IO_RW_FUNC(func_number) | SD_IO_RW_ADR(addr) |
		SD_IOE_RW_LEN(datalen);

	if (is_increment)
		arg |= SD_IO_RW_INCR;

	mmcd.data = data;
	mmcd.len = datalen;
	mmcd.xfer_len = 0; /* not used by MMCCAM */
	mmcd.mrq = NULL; /* not used by MMCCAM */

	if (is_write) {
		arg |= SD_IO_RW_WR;
		cam_flags = CAM_DIR_OUT;
		mmcd.flags = MMC_DATA_WRITE;
	} else {
		cam_flags = CAM_DIR_IN;
		mmcd.flags = MMC_DATA_READ;
	}
	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ 0,
		       /*cbfcnp*/ NULL,
		       /*flags*/ cam_flags,
		       /*mmc_opcode*/ SD_IO_RW_EXTENDED,
		       /*mmc_arg*/ arg,
		       /*mmc_flags*/ flags,
		       /*mmc_data*/ &mmcd,
		       /*timeout*/ 5000);

	if (((retval = cam_send_ccb(dev, ccb)) < 0)
	    || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);
		return (-1);
	}

	resp = ccb->mmcio.cmd.resp[0] & 0xFF;
	if (resp != 0)
		warn("Response from CMD53 is not 0?!");
	cam_freeccb(ccb);
	return (retval);
}


int
sdio_read_bool_for_func(struct cam_device *dev, uint32_t addr, uint8_t func_number, uint8_t *is_enab) {
	uint8_t resp;
	int ret;

	ret = sdio_rw_direct(dev, 0, addr, 0, NULL, &resp);
	if (ret < 0)
		return ret;

	*is_enab = (resp & (1 << func_number)) > 0 ? 1 : 0;

	return (0);
}

int
sdio_set_bool_for_func(struct cam_device *dev, uint32_t addr, uint8_t func_number, int enable) {
	uint8_t resp;
	int ret;
	uint8_t is_enabled;

	ret = sdio_rw_direct(dev, 0, addr, 0, NULL, &resp);
	if (ret != 0)
		return ret;

	is_enabled = resp & (1 << func_number);
	if ((is_enabled !=0 && enable == 1) || (is_enabled == 0 && enable == 0))
		return 0;

	if (enable)
		resp |= 1 << func_number;
	else
		resp &= ~ (1 << func_number);

	ret = sdio_rw_direct(dev, 0, addr, 1, &resp, &resp);

	return ret;
}

/* Conventional I/O functions */
uint8_t
sdio_read_1(struct cam_device *dev, uint8_t func_number, uint32_t addr, int *ret) {
	uint8_t val;
	*ret = sdio_rw_direct(dev, func_number, addr, 0, NULL, &val);
	return val;
}

int
sdio_write_1(struct cam_device *dev, uint8_t func_number, uint32_t addr, uint8_t val) {
	uint8_t _val;
	return sdio_rw_direct(dev, func_number, addr, 0, &val, &_val);
}

uint16_t
sdio_read_2(struct cam_device *dev, uint8_t func_number, uint32_t addr, int *ret) {
	uint16_t val;
	*ret = sdio_rw_extended(dev, func_number, addr,
				/* is_write */ 0,
				/* data */ (caddr_t) &val,
				/* datalen */ sizeof(val),
				/* is_increment */ 1,
				/* blk_count */ 0
		);
	return val;
}


int
sdio_write_2(struct cam_device *dev, uint8_t func_number, uint32_t addr, uint16_t val) {
	return sdio_rw_extended(dev, func_number, addr,
				/* is_write */ 1,
				/* data */ (caddr_t) &val,
				/* datalen */ sizeof(val),
				/* is_increment */ 1,
				/* blk_count */ 0
		);
}

uint32_t
sdio_read_4(struct cam_device *dev, uint8_t func_number, uint32_t addr, int *ret) {
	uint32_t val;
	*ret = sdio_rw_extended(dev, func_number, addr,
				/* is_write */ 0,
				/* data */ (caddr_t) &val,
				/* datalen */ sizeof(val),
				/* is_increment */ 1,
				/* blk_count */ 0
		);
	return val;
}


int
sdio_write_4(struct cam_device *dev, uint8_t func_number, uint32_t addr, uint32_t val) {
	return sdio_rw_extended(dev, func_number, addr,
				/* is_write */ 1,
				/* data */ (caddr_t) &val,
				/* datalen */ sizeof(val),
				/* is_increment */ 1,
				/* blk_count */ 0
		);
}

/* Higher-level wrappers for certain management operations */
int
sdio_is_func_ready(struct cam_device *dev, uint8_t func_number, uint8_t *is_enab) {
	return sdio_read_bool_for_func(dev, SD_IO_CCCR_FN_READY, func_number, is_enab);
}

int
sdio_is_func_enabled(struct cam_device *dev, uint8_t func_number, uint8_t *is_enab) {
	return sdio_read_bool_for_func(dev, SD_IO_CCCR_FN_ENABLE, func_number, is_enab);
}

int
sdio_func_enable(struct cam_device *dev, uint8_t func_number, int enable) {
	return sdio_set_bool_for_func(dev, SD_IO_CCCR_FN_ENABLE, func_number, enable);
}

int
sdio_is_func_intr_enabled(struct cam_device *dev, uint8_t func_number, uint8_t *is_enab) {
	return sdio_read_bool_for_func(dev, SD_IO_CCCR_INT_ENABLE, func_number, is_enab);
}

int
sdio_func_intr_enable(struct cam_device *dev, uint8_t func_number, int enable) {
	return sdio_set_bool_for_func(dev, SD_IO_CCCR_INT_ENABLE, func_number, enable);
}

int
sdio_card_set_bus_width(struct cam_device *dev, enum mmc_bus_width bw) {
	int ret;
	uint8_t ctl_val;
	ret = sdio_rw_direct(dev, 0, SD_IO_CCCR_BUS_WIDTH, 0, NULL, &ctl_val);
	if (ret < 0) {
		warn("Error getting CCCR_BUS_WIDTH value");
		return ret;
	}
	ctl_val &= ~0x3;
	switch (bw) {
	case bus_width_1:
		/* Already set to 1-bit */
		break;
	case bus_width_4:
		ctl_val |= CCCR_BUS_WIDTH_4;
		break;
	case bus_width_8:
		warn("Cannot do 8-bit on SDIO yet");
		return -1;
		break;
	}
	ret = sdio_rw_direct(dev, 0, SD_IO_CCCR_BUS_WIDTH, 1, &ctl_val, &ctl_val);
	if (ret < 0) {
		warn("Error setting CCCR_BUS_WIDTH value");
		return ret;
	}
	return ret;
}

int
sdio_func_read_cis(struct cam_device *dev, uint8_t func_number,
		   uint32_t cis_addr, struct cis_info *info) {
	uint8_t tuple_id, tuple_len, tuple_count;
	uint32_t addr;

	char *cis1_info[4];
	int start, i, ch, count, ret;
	char cis1_info_buf[256];

	tuple_count = 0; /* Use to prevent infinite loop in case of parse errors */
	memset(cis1_info_buf, 0, 256);
	do {
		addr = cis_addr;
		tuple_id = sdio_read_1(dev, 0, addr++, &ret);
		if (tuple_id == SD_IO_CISTPL_END)
			break;
		if (tuple_id == 0) {
			cis_addr++;
			continue;
		}
		tuple_len = sdio_read_1(dev, 0, addr++, &ret);
		if (tuple_len == 0 && tuple_id != 0x00) {
			warn("Parse error: 0-length tuple %02X\n", tuple_id);
			return -1;
		}

		switch (tuple_id) {
		case SD_IO_CISTPL_VERS_1:
			addr += 2;
			for (count = 0, start = 0, i = 0;
			     (count < 4) && ((i + 4) < 256); i++) {
				ch = sdio_read_1(dev, 0, addr + i, &ret);
				printf("count=%d, start=%d, i=%d, Got %c (0x%02x)\n", count, start, i, ch, ch);
				if (ch == 0xff)
					break;
				cis1_info_buf[i] = ch;
				if (ch == 0) {
					cis1_info[count] =
						cis1_info_buf + start;
					start = i + 1;
					count++;
				}
			}
			printf("Card info:");
			for (i=0; i<4; i++)
				if (cis1_info[i])
					printf(" %s", cis1_info[i]);
			printf("\n");
			break;
		case SD_IO_CISTPL_MANFID:
			info->man_id =  sdio_read_1(dev, 0, addr++, &ret);
			info->man_id |= sdio_read_1(dev, 0, addr++, &ret) << 8;

			info->prod_id =  sdio_read_1(dev, 0, addr++, &ret);
			info->prod_id |= sdio_read_1(dev, 0, addr++, &ret) << 8;
			break;
		case SD_IO_CISTPL_FUNCID:
			/* not sure if we need to parse it? */
			break;
		case SD_IO_CISTPL_FUNCE:
			if (tuple_len < 4) {
				printf("FUNCE is too short: %d\n", tuple_len);
				break;
			}
			if (func_number == 0) {
				/* skip extended_data */
				addr++;
				info->max_block_size  = sdio_read_1(dev, 0, addr++, &ret);
				info->max_block_size |= sdio_read_1(dev, 0, addr++, &ret) << 8;
			} else {
				info->max_block_size  = sdio_read_1(dev, 0, addr + 0xC, &ret);
				info->max_block_size |= sdio_read_1(dev, 0, addr + 0xD, &ret) << 8;
			}
			break;
		default:
			warnx("Skipping tuple ID %02X len %02X\n", tuple_id, tuple_len);
		}
		cis_addr += tuple_len + 2;
		tuple_count++;
	} while (tuple_count < 20);

	return 0;
}

uint32_t
sdio_get_common_cis_addr(struct cam_device *dev) {
	uint32_t addr;
	int ret;

	addr =  sdio_read_1(dev, 0, SD_IO_CCCR_CISPTR, &ret);
	addr |= sdio_read_1(dev, 0, SD_IO_CCCR_CISPTR + 1, &ret) << 8;
	addr |= sdio_read_1(dev, 0, SD_IO_CCCR_CISPTR + 2, &ret) << 16;

	if (addr < SD_IO_CIS_START || addr > SD_IO_CIS_START + SD_IO_CIS_SIZE) {
		warn("Bad CIS address: %04X\n", addr);
		addr = 0;
	}

	return addr;
}

void sdio_card_reset(struct cam_device *dev) {
	int ret;
	uint8_t ctl_val;
	ret = sdio_rw_direct(dev, 0, SD_IO_CCCR_CTL, 0, NULL, &ctl_val);
	if (ret < 0)
		errx(1, "Error getting CCCR_CTL value");
	ctl_val |= CCCR_CTL_RES;
	ret = sdio_rw_direct(dev, 0, SD_IO_CCCR_CTL, 1, &ctl_val, &ctl_val);
	if (ret < 0)
		errx(1, "Error setting CCCR_CTL value");
}
