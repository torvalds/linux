/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/bus.h>

#include <dev/iicbus/iicoc.h>

#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/mips-extns.h> /* needed by board.h */

#include <mips/nlm/board.h>

/*
 * We have to read the EEPROM in early boot (now only for MAC addr)
 * but later for board information.  Use simple polled mode driver
 * for I2C
 */
#define	oc_read_reg(reg)	nlm_read_reg(eeprom_i2c_base, reg)
#define	oc_write_reg(reg, val)	nlm_write_reg(eeprom_i2c_base, reg, val)

static uint64_t eeprom_i2c_base;

static int
oc_wait_on_status(uint8_t bit)
{
	int tries = I2C_TIMEOUT;
	uint8_t status;

	do {
		status = oc_read_reg(OC_I2C_STATUS_REG);
	} while ((status & bit) != 0 && --tries > 0);

	return (tries == 0 ? -1: 0);
}

static int
oc_rd_cmd(uint8_t cmd)
{
	uint8_t data;

		oc_write_reg(OC_I2C_CMD_REG, cmd);
		if (oc_wait_on_status(OC_STATUS_TIP) < 0)
			return (-1);

	data = oc_read_reg(OC_I2C_DATA_REG);
	return (data);
}

static int
oc_wr_cmd(uint8_t data, uint8_t cmd)
{
	oc_write_reg(OC_I2C_DATA_REG, data);
	oc_write_reg(OC_I2C_CMD_REG, cmd);

	if (oc_wait_on_status(OC_STATUS_TIP) < 0)
		return (-1);
	return (0);
}

int
nlm_board_eeprom_read(int node, int bus, int addr, int offs, uint8_t *buf,
    int sz)
{
	int rd, i;
	char *err = NULL;

	eeprom_i2c_base = nlm_pcicfg_base(XLP_IO_I2C_OFFSET(node, bus)) +
	    XLP_IO_PCI_HDRSZ;

	if (oc_wait_on_status(OC_STATUS_BUSY) < 0) {
		err = "Not idle";
		goto err_exit;
	}

	/* write start */
	if (oc_wr_cmd(addr, OC_COMMAND_START)) {
		err = "I2C write start failed.";
		goto err_exit;
	}

	if (oc_read_reg(OC_I2C_STATUS_REG) & OC_STATUS_NACK) {
		err = "No ack after start";
		goto err_exit_stop;
	}

	if (oc_read_reg(OC_I2C_STATUS_REG) & OC_STATUS_AL) {
		err = "I2C Bus Arbitration Lost";
		goto err_exit_stop;
	}

	/* Write offset */
	if (oc_wr_cmd(offs, OC_COMMAND_WRITE)) {
		err = "I2C write slave offset failed.";
		goto err_exit_stop;
	}

	if (oc_read_reg(OC_I2C_STATUS_REG) & OC_STATUS_NACK) {
		err = "No ack after write";
		goto err_exit_stop;
	}

	/* read start */
	if (oc_wr_cmd(addr | 1, OC_COMMAND_START)) {
		err = "I2C read start failed.";
		goto err_exit_stop;
	}

	if (oc_read_reg(OC_I2C_STATUS_REG) & OC_STATUS_NACK) {
		err = "No ack after read start";
		goto err_exit_stop;
	}

	for (i = 0; i < sz - 1; i++) {
		if ((rd = oc_rd_cmd(OC_COMMAND_READ)) < 0) {
			err = "I2C read data byte failed.";
			goto err_exit_stop;
		}
	buf[i] = rd;
	}

	/* last byte */
	if ((rd = oc_rd_cmd(OC_COMMAND_RDNACK)) < 0) {
		err = "I2C read last data byte failed.";
		goto err_exit_stop;
	}
	buf[sz - 1] = rd;

err_exit_stop:
	oc_write_reg(OC_I2C_CMD_REG, OC_COMMAND_STOP);
	if (oc_wait_on_status(OC_STATUS_BUSY) < 0)
		printf("%s: stop failed", __func__);

err_exit:
	if (err) {
		printf("%s: Failed (%s)\n", __func__, err);
		return (-1);
	}
	return (0);
}
