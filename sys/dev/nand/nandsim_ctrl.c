/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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
 */

/* Simulated NAND controller driver */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/time.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>
#include <dev/nand/nandsim.h>
#include <dev/nand/nandsim_log.h>
#include <dev/nand/nandsim_chip.h>
#include "nfc_if.h"

#define ADDRESS_SIZE	5

extern struct sim_ctrl_conf ctrls[MAX_SIM_DEV];

static void	byte_corrupt(struct nandsim_chip *, uint8_t *);

static int	nandsim_attach(device_t);
static int	nandsim_detach(device_t);
static int	nandsim_probe(device_t);

static uint8_t	nandsim_read_byte(device_t);
static uint16_t	nandsim_read_word(device_t);
static int	nandsim_select_cs(device_t, uint8_t);
static void	nandsim_write_byte(device_t, uint8_t);
static void	nandsim_write_word(device_t, uint16_t);
static void	nandsim_read_buf(device_t, void *, uint32_t);
static void	nandsim_write_buf(device_t, void *, uint32_t);
static int	nandsim_send_command(device_t, uint8_t);
static int	nandsim_send_address(device_t, uint8_t);

static device_method_t nandsim_methods[] = {
	DEVMETHOD(device_probe,		nandsim_probe),
	DEVMETHOD(device_attach,	nandsim_attach),
	DEVMETHOD(device_detach,	nandsim_detach),

	DEVMETHOD(nfc_select_cs,	nandsim_select_cs),
	DEVMETHOD(nfc_send_command,	nandsim_send_command),
	DEVMETHOD(nfc_send_address,	nandsim_send_address),
	DEVMETHOD(nfc_read_byte,	nandsim_read_byte),
	DEVMETHOD(nfc_read_word,	nandsim_read_word),
	DEVMETHOD(nfc_write_byte,	nandsim_write_byte),
	DEVMETHOD(nfc_read_buf,		nandsim_read_buf),
	DEVMETHOD(nfc_write_buf,	nandsim_write_buf),

	{ 0, 0 },
};

static driver_t nandsim_driver = {
	"nandsim",
	nandsim_methods,
	sizeof(struct nandsim_softc),
};

static devclass_t nandsim_devclass;
DRIVER_MODULE(nandsim, nexus, nandsim_driver, nandsim_devclass, 0, 0);
DRIVER_MODULE(nandbus, nandsim, nandbus_driver, nandbus_devclass, 0, 0);

static int
nandsim_probe(device_t dev)
{

	device_set_desc(dev, "NAND controller simulator");
	return (BUS_PROBE_DEFAULT);
}

static int
nandsim_attach(device_t dev)
{
	struct nandsim_softc *sc;
	struct sim_ctrl_conf *params;
	struct sim_chip *chip;
	uint16_t *eccpos;
	int i, err;

	sc = device_get_softc(dev);
	params = &ctrls[device_get_unit(dev)];

	if (strlen(params->filename) == 0)
		snprintf(params->filename, FILENAME_SIZE, "ctrl%d.log",
		    params->num);

	nandsim_log_init(sc, params->filename);
	for (i = 0; i < params->num_cs; i++) {
		chip = params->chips[i];
		if (chip && chip->device_id != 0) {
			sc->chips[i] = nandsim_chip_init(sc, i, chip);
			if (chip->features & ONFI_FEAT_16BIT)
				sc->nand_dev.flags |= NAND_16_BIT;
		}
	}

	if (params->ecc_layout[0] != 0xffff)
		eccpos = params->ecc_layout;
	else
		eccpos = NULL;

	nand_init(&sc->nand_dev, dev, params->ecc, 0, 0, eccpos, "nandsim");

	err = nandbus_create(dev);

	return (err);
}

static int
nandsim_detach(device_t dev)
{
	struct nandsim_softc *sc;
	struct sim_ctrl_conf *params;
	int i;

	sc = device_get_softc(dev);
	params = &ctrls[device_get_unit(dev)];

	for (i = 0; i < params->num_cs; i++)
		if (sc->chips[i] != NULL)
			nandsim_chip_destroy(sc->chips[i]);

	nandsim_log_close(sc);

	return (0);
}

static int
nandsim_select_cs(device_t dev, uint8_t cs)
{
	struct nandsim_softc *sc;

	sc = device_get_softc(dev);

	if (cs >= MAX_CS_NUM)
		return (EINVAL);

	sc->active_chip = sc->chips[cs];

	if (sc->active_chip)
		nandsim_log(sc->active_chip, NANDSIM_LOG_EV,
		    "Select cs %d\n", cs);

	return (0);
}

static int
nandsim_send_command(device_t dev, uint8_t command)
{
	struct nandsim_softc *sc;
	struct nandsim_chip *chip;
	struct nandsim_ev *ev;

	sc = device_get_softc(dev);
	chip = sc->active_chip;

	if (chip == NULL)
		return (0);

	nandsim_log(chip, NANDSIM_LOG_EV, "Send command %x\n", command);

	switch (command) {
	case NAND_CMD_READ_ID:
	case NAND_CMD_READ_PARAMETER:
		sc->address_type = ADDR_ID;
		break;
	case NAND_CMD_ERASE:
		sc->address_type = ADDR_ROW;
		break;
	case NAND_CMD_READ:
	case NAND_CMD_PROG:
		sc->address_type = ADDR_ROWCOL;
		break;
	default:
		sc->address_type = ADDR_NONE;
		break;
	}

	if (command == NAND_CMD_STATUS)
		chip->flags |= NANDSIM_CHIP_GET_STATUS;
	else {
		ev = create_event(chip, NANDSIM_EV_CMD, 1);
		*(uint8_t *)ev->data = command;
		send_event(ev);
	}

	return (0);
}

static int
nandsim_send_address(device_t dev, uint8_t addr)
{
	struct nandsim_ev *ev;
	struct nandsim_softc *sc;
	struct nandsim_chip *chip;

	sc = device_get_softc(dev);
	chip = sc->active_chip;

	if (chip == NULL)
		return (0);

	KASSERT((sc->address_type != ADDR_NONE), ("unexpected address"));
	nandsim_log(chip, NANDSIM_LOG_EV, "Send addr %x\n", addr);

	ev = create_event(chip, NANDSIM_EV_ADDR, 1);

	*((uint8_t *)(ev->data)) = addr;

	send_event(ev);
	return (0);
}

static uint8_t
nandsim_read_byte(device_t dev)
{
	struct nandsim_softc *sc;
	struct nandsim_chip *chip;
	uint8_t ret = 0xff;

	sc = device_get_softc(dev);
	chip = sc->active_chip;

	if (chip && !(chip->flags & NANDSIM_CHIP_FROZEN)) {
		if (chip->flags & NANDSIM_CHIP_GET_STATUS) {
			nandsim_chip_timeout(chip);
			ret = nandchip_get_status(chip);
			chip->flags &= ~NANDSIM_CHIP_GET_STATUS;
		} else if (chip->data.index < chip->data.size) {
			ret = chip->data.data_ptr[chip->data.index++];
			byte_corrupt(chip, &ret);
		}
		nandsim_log(chip, NANDSIM_LOG_DATA, "read %02x\n", ret);
	}

	return (ret);
}

static uint16_t
nandsim_read_word(device_t dev)
{
	struct nandsim_softc *sc;
	struct nandsim_chip *chip;
	uint16_t *data_ptr;
	uint16_t ret = 0xffff;
	uint8_t  *byte_ret = (uint8_t *)&ret;

	sc = device_get_softc(dev);
	chip = sc->active_chip;

	if (chip && !(chip->flags & NANDSIM_CHIP_FROZEN)) {
		if (chip->data.index < chip->data.size - 1) {
			data_ptr =
			    (uint16_t *)&(chip->data.data_ptr[chip->data.index]);
			ret = *data_ptr;
			chip->data.index += 2;
			byte_corrupt(chip, byte_ret);
			byte_corrupt(chip, byte_ret + 1);
		}
		nandsim_log(chip, NANDSIM_LOG_DATA, "read %04x\n", ret);
	}

	return (ret);
}

static void
nandsim_write_byte(device_t dev, uint8_t byte)
{
	struct nandsim_softc *sc;
	struct nandsim_chip *chip;

	sc = device_get_softc(dev);
	chip = sc->active_chip;

	if (chip && !(chip->flags & NANDSIM_CHIP_FROZEN) &&
	    (chip->data.index < chip->data.size)) {
		byte_corrupt(chip, &byte);
		chip->data.data_ptr[chip->data.index] &= byte;
		chip->data.index++;
		nandsim_log(chip, NANDSIM_LOG_DATA, "write %02x\n", byte);
	}
}

static void
nandsim_write_word(device_t dev, uint16_t word)
{
	struct nandsim_softc *sc;
	struct nandsim_chip *chip;
	uint16_t *data_ptr;
	uint8_t  *byte_ptr = (uint8_t *)&word;

	sc = device_get_softc(dev);
	chip = sc->active_chip;

	if (chip && !(chip->flags & NANDSIM_CHIP_FROZEN)) {
		if ((chip->data.index + 1) < chip->data.size) {
			byte_corrupt(chip, byte_ptr);
			byte_corrupt(chip, byte_ptr + 1);
			data_ptr =
			    (uint16_t *)&(chip->data.data_ptr[chip->data.index]);
			*data_ptr &= word;
			chip->data.index += 2;
		}

		nandsim_log(chip, NANDSIM_LOG_DATA, "write %04x\n", word);
	}
}

static void
nandsim_read_buf(device_t dev, void *buf, uint32_t len)
{
	struct nandsim_softc *sc;
	uint16_t *buf16 = (uint16_t *)buf;
	uint8_t *buf8 = (uint8_t *)buf;
	int i;

	sc = device_get_softc(dev);

	if (sc->nand_dev.flags & NAND_16_BIT) {
		for (i = 0; i < len / 2; i++)
			buf16[i] = nandsim_read_word(dev);
	} else {
		for (i = 0; i < len; i++)
			buf8[i] = nandsim_read_byte(dev);
	}
}

static void
nandsim_write_buf(device_t dev, void *buf, uint32_t len)
{
	struct nandsim_softc *sc;
	uint16_t *buf16 = (uint16_t *)buf;
	uint8_t *buf8 = (uint8_t *)buf;
	int i;

	sc = device_get_softc(dev);

	if (sc->nand_dev.flags & NAND_16_BIT) {
		for (i = 0; i < len / 2; i++)
			nandsim_write_word(dev, buf16[i]);
	} else {
		for (i = 0; i < len; i++)
			nandsim_write_byte(dev, buf8[i]);
	}
}

static void
byte_corrupt(struct nandsim_chip *chip, uint8_t *byte)
{
	uint32_t rand;
	uint8_t bit;

	rand = random();
	if ((rand % 1000000) < chip->error_ratio) {
		bit = rand % 8;
		if (*byte & (1 << bit))
			*byte &= ~(1 << bit);
		else
			*byte |= (1 << bit);
	}
}
