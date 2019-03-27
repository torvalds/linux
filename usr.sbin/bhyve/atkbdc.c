/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * Copyright (c) 2015 Nahanni Systems Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <machine/vmm.h>

#include <vmmapi.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <pthread_np.h>

#include "acpi.h"
#include "atkbdc.h"
#include "inout.h"
#include "pci_emul.h"
#include "pci_irq.h"
#include "pci_lpc.h"
#include "ps2kbd.h"
#include "ps2mouse.h"

#define	KBD_DATA_PORT		0x60

#define	KBD_STS_CTL_PORT	0x64

#define	KBDC_RESET		0xfe

#define	KBD_DEV_IRQ		1
#define	AUX_DEV_IRQ		12

/* controller commands */
#define	KBDC_SET_COMMAND_BYTE	0x60
#define	KBDC_GET_COMMAND_BYTE	0x20
#define	KBDC_DISABLE_AUX_PORT	0xa7
#define	KBDC_ENABLE_AUX_PORT	0xa8
#define	KBDC_TEST_AUX_PORT	0xa9
#define	KBDC_TEST_CTRL		0xaa
#define	KBDC_TEST_KBD_PORT	0xab
#define	KBDC_DISABLE_KBD_PORT	0xad
#define	KBDC_ENABLE_KBD_PORT	0xae
#define	KBDC_READ_INPORT	0xc0
#define	KBDC_READ_OUTPORT	0xd0
#define	KBDC_WRITE_OUTPORT	0xd1
#define	KBDC_WRITE_KBD_OUTBUF	0xd2
#define	KBDC_WRITE_AUX_OUTBUF	0xd3
#define	KBDC_WRITE_TO_AUX	0xd4

/* controller command byte (set by KBDC_SET_COMMAND_BYTE) */
#define	KBD_TRANSLATION		0x40
#define	KBD_SYS_FLAG_BIT	0x04
#define	KBD_DISABLE_KBD_PORT	0x10
#define	KBD_DISABLE_AUX_PORT	0x20
#define	KBD_ENABLE_AUX_INT	0x02
#define	KBD_ENABLE_KBD_INT	0x01
#define	KBD_KBD_CONTROL_BITS	(KBD_DISABLE_KBD_PORT | KBD_ENABLE_KBD_INT)
#define	KBD_AUX_CONTROL_BITS	(KBD_DISABLE_AUX_PORT | KBD_ENABLE_AUX_INT)

/* controller status bits */
#define	KBDS_KBD_BUFFER_FULL	0x01
#define KBDS_SYS_FLAG		0x04
#define KBDS_CTRL_FLAG		0x08
#define	KBDS_AUX_BUFFER_FULL	0x20

/* controller output port */
#define	KBDO_KBD_OUTFULL	0x10
#define	KBDO_AUX_OUTFULL	0x20

#define	RAMSZ			32
#define	FIFOSZ			15
#define	CTRL_CMD_FLAG		0x8000

struct kbd_dev {
	bool	irq_active;
	int	irq;

	uint8_t	buffer[FIFOSZ];
	int	brd, bwr;
	int	bcnt;
};

struct aux_dev {
	bool	irq_active;
	int	irq;
};

struct atkbdc_softc {
	struct vmctx *ctx;
	pthread_mutex_t mtx;

	struct ps2kbd_softc	*ps2kbd_sc;
	struct ps2mouse_softc	*ps2mouse_sc;

	uint8_t	status;		/* status register */
	uint8_t	outport;	/* controller output port */
	uint8_t	ram[RAMSZ];	/* byte0 = controller config */

	uint32_t curcmd;	/* current command for next byte */
	uint32_t  ctrlbyte;

	struct kbd_dev kbd;
	struct aux_dev aux;
};

static void
atkbdc_assert_kbd_intr(struct atkbdc_softc *sc)
{
	if ((sc->ram[0] & KBD_ENABLE_KBD_INT) != 0) {
		sc->kbd.irq_active = true;
		vm_isa_pulse_irq(sc->ctx, sc->kbd.irq, sc->kbd.irq);
	}
}

static void
atkbdc_assert_aux_intr(struct atkbdc_softc *sc)
{
	if ((sc->ram[0] & KBD_ENABLE_AUX_INT) != 0) {
		sc->aux.irq_active = true;
		vm_isa_pulse_irq(sc->ctx, sc->aux.irq, sc->aux.irq);
	}
}

static int
atkbdc_kbd_queue_data(struct atkbdc_softc *sc, uint8_t val)
{
	assert(pthread_mutex_isowned_np(&sc->mtx));

	if (sc->kbd.bcnt < FIFOSZ) {
		sc->kbd.buffer[sc->kbd.bwr] = val;
		sc->kbd.bwr = (sc->kbd.bwr + 1) % FIFOSZ;
		sc->kbd.bcnt++;
		sc->status |= KBDS_KBD_BUFFER_FULL;
		sc->outport |= KBDO_KBD_OUTFULL;
	} else {
		printf("atkbd data buffer full\n");
	}

	return (sc->kbd.bcnt < FIFOSZ);
}

static void
atkbdc_kbd_read(struct atkbdc_softc *sc)
{
	const uint8_t translation[256] = {
		0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58,
		0x64, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59,
		0x65, 0x38, 0x2a, 0x70, 0x1d, 0x10, 0x02, 0x5a,
		0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
		0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
		0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
		0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e,
		0x6a, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
		0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x60,
		0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
		0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e,
		0x3a, 0x36, 0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76,
		0x55, 0x56, 0x77, 0x78, 0x79, 0x7a, 0x0e, 0x7b,
		0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e, 0x7f, 0x6f,
		0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
		0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
		0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87,
		0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
		0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
		0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
		0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
		0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
		0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
		0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
		0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
		0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
		0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
		0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
		0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
		0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
		0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
		0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
	};
	uint8_t val;
	uint8_t release = 0;

	assert(pthread_mutex_isowned_np(&sc->mtx));

	if (sc->ram[0] & KBD_TRANSLATION) {
		while (ps2kbd_read(sc->ps2kbd_sc, &val) != -1) {
			if (val == 0xf0) {
				release = 0x80;
				continue;
			} else {
				val = translation[val] | release;
			}
			atkbdc_kbd_queue_data(sc, val);
			break;
		}
	} else {
		while (sc->kbd.bcnt < FIFOSZ) {
			if (ps2kbd_read(sc->ps2kbd_sc, &val) != -1)
				atkbdc_kbd_queue_data(sc, val);
			else
				break;
		}
	}

	if (((sc->ram[0] & KBD_DISABLE_AUX_PORT) ||
	    ps2mouse_fifocnt(sc->ps2mouse_sc) == 0) && sc->kbd.bcnt > 0)
		atkbdc_assert_kbd_intr(sc);
}

static void
atkbdc_aux_poll(struct atkbdc_softc *sc)
{
	if (ps2mouse_fifocnt(sc->ps2mouse_sc) > 0) {
		sc->status |= KBDS_AUX_BUFFER_FULL | KBDS_KBD_BUFFER_FULL;
		sc->outport |= KBDO_AUX_OUTFULL;
		atkbdc_assert_aux_intr(sc);
	}
}

static void
atkbdc_kbd_poll(struct atkbdc_softc *sc)
{
	assert(pthread_mutex_isowned_np(&sc->mtx));

	atkbdc_kbd_read(sc);
}

static void
atkbdc_poll(struct atkbdc_softc *sc)
{
	atkbdc_aux_poll(sc);
	atkbdc_kbd_poll(sc);
}

static void
atkbdc_dequeue_data(struct atkbdc_softc *sc, uint8_t *buf)
{
	assert(pthread_mutex_isowned_np(&sc->mtx));

	if (ps2mouse_read(sc->ps2mouse_sc, buf) == 0) {
		if (ps2mouse_fifocnt(sc->ps2mouse_sc) == 0) {
			if (sc->kbd.bcnt == 0)
				sc->status &= ~(KBDS_AUX_BUFFER_FULL |
				                KBDS_KBD_BUFFER_FULL);
			else
				sc->status &= ~(KBDS_AUX_BUFFER_FULL);
			sc->outport &= ~KBDO_AUX_OUTFULL;
		}

		atkbdc_poll(sc);
		return;
	}

	if (sc->kbd.bcnt > 0) {
		*buf = sc->kbd.buffer[sc->kbd.brd];
		sc->kbd.brd = (sc->kbd.brd + 1) % FIFOSZ;
		sc->kbd.bcnt--;
		if (sc->kbd.bcnt == 0) {
			sc->status &= ~KBDS_KBD_BUFFER_FULL;
			sc->outport &= ~KBDO_KBD_OUTFULL;
		}

		atkbdc_poll(sc);
	}

	if (ps2mouse_fifocnt(sc->ps2mouse_sc) == 0 && sc->kbd.bcnt == 0) {
		sc->status &= ~(KBDS_AUX_BUFFER_FULL | KBDS_KBD_BUFFER_FULL);
	}
}

static int
atkbdc_data_handler(struct vmctx *ctx, int vcpu, int in, int port, int bytes,
    uint32_t *eax, void *arg)
{
	struct atkbdc_softc *sc;
	uint8_t buf;
	int retval;

	if (bytes != 1)
		return (-1);
	sc = arg;
	retval = 0;

	pthread_mutex_lock(&sc->mtx);
	if (in) {
		sc->curcmd = 0;
		if (sc->ctrlbyte != 0) {
			*eax = sc->ctrlbyte & 0xff;
			sc->ctrlbyte = 0;
		} else {
			/* read device buffer; includes kbd cmd responses */
			atkbdc_dequeue_data(sc, &buf);
			*eax = buf;
		}

		sc->status &= ~KBDS_CTRL_FLAG;
		pthread_mutex_unlock(&sc->mtx);
		return (retval);
	}

	if (sc->status & KBDS_CTRL_FLAG) {
		/*
		 * Command byte for the controller.
		 */
		switch (sc->curcmd) {
		case KBDC_SET_COMMAND_BYTE:
			sc->ram[0] = *eax;
			if (sc->ram[0] & KBD_SYS_FLAG_BIT)
				sc->status |= KBDS_SYS_FLAG;
			else
				sc->status &= ~KBDS_SYS_FLAG;
			break;
		case KBDC_WRITE_OUTPORT:
			sc->outport = *eax;
			break;
		case KBDC_WRITE_TO_AUX:
			ps2mouse_write(sc->ps2mouse_sc, *eax, 0);
			atkbdc_poll(sc);
			break;
		case KBDC_WRITE_KBD_OUTBUF:
			atkbdc_kbd_queue_data(sc, *eax);
			break;
		case KBDC_WRITE_AUX_OUTBUF:
			ps2mouse_write(sc->ps2mouse_sc, *eax, 1);
			sc->status |= (KBDS_AUX_BUFFER_FULL | KBDS_KBD_BUFFER_FULL);
			atkbdc_aux_poll(sc);
			break;
		default:
			/* write to particular RAM byte */
			if (sc->curcmd >= 0x61 && sc->curcmd <= 0x7f) {
				int byten;

				byten = (sc->curcmd - 0x60) & 0x1f;
				sc->ram[byten] = *eax & 0xff;
			}
			break;
		}

		sc->curcmd = 0;
		sc->status &= ~KBDS_CTRL_FLAG;

		pthread_mutex_unlock(&sc->mtx);
		return (retval);
	}

	/*
	 * Data byte for the device.
	 */
	ps2kbd_write(sc->ps2kbd_sc, *eax);
	atkbdc_poll(sc);

	pthread_mutex_unlock(&sc->mtx);

	return (retval);
}

static int
atkbdc_sts_ctl_handler(struct vmctx *ctx, int vcpu, int in, int port,
    int bytes, uint32_t *eax, void *arg)
{
	struct atkbdc_softc *sc;
	int	error, retval;

	if (bytes != 1)
		return (-1);

	sc = arg;
	retval = 0;

	pthread_mutex_lock(&sc->mtx);

	if (in) {
		/* read status register */
		*eax = sc->status;
		pthread_mutex_unlock(&sc->mtx);
		return (retval);
	}


	sc->curcmd = 0;
	sc->status |= KBDS_CTRL_FLAG;
	sc->ctrlbyte = 0;

	switch (*eax) {
	case KBDC_GET_COMMAND_BYTE:
		sc->ctrlbyte = CTRL_CMD_FLAG | sc->ram[0];
		break;
	case KBDC_TEST_CTRL:
		sc->ctrlbyte = CTRL_CMD_FLAG | 0x55;
		break;
	case KBDC_TEST_AUX_PORT:
	case KBDC_TEST_KBD_PORT:
		sc->ctrlbyte = CTRL_CMD_FLAG | 0;
		break;
	case KBDC_READ_INPORT:
		sc->ctrlbyte = CTRL_CMD_FLAG | 0;
		break;
	case KBDC_READ_OUTPORT:
		sc->ctrlbyte = CTRL_CMD_FLAG | sc->outport;
		break;
	case KBDC_SET_COMMAND_BYTE:
	case KBDC_WRITE_OUTPORT:
	case KBDC_WRITE_KBD_OUTBUF:
	case KBDC_WRITE_AUX_OUTBUF:
		sc->curcmd = *eax;
		break;
	case KBDC_DISABLE_KBD_PORT:
		sc->ram[0] |= KBD_DISABLE_KBD_PORT;
		break;
	case KBDC_ENABLE_KBD_PORT:
		sc->ram[0] &= ~KBD_DISABLE_KBD_PORT;
		if (sc->kbd.bcnt > 0)
			sc->status |= KBDS_KBD_BUFFER_FULL;
		atkbdc_poll(sc);
		break;
	case KBDC_WRITE_TO_AUX:
		sc->curcmd = *eax;
		break;
	case KBDC_DISABLE_AUX_PORT:
		sc->ram[0] |= KBD_DISABLE_AUX_PORT;
		ps2mouse_toggle(sc->ps2mouse_sc, 0);
		sc->status &= ~(KBDS_AUX_BUFFER_FULL | KBDS_KBD_BUFFER_FULL);
		sc->outport &= ~KBDS_AUX_BUFFER_FULL;
		break;
	case KBDC_ENABLE_AUX_PORT:
		sc->ram[0] &= ~KBD_DISABLE_AUX_PORT;
		ps2mouse_toggle(sc->ps2mouse_sc, 1);
		if (ps2mouse_fifocnt(sc->ps2mouse_sc) > 0)
			sc->status |= KBDS_AUX_BUFFER_FULL | KBDS_KBD_BUFFER_FULL;
		break;
	case KBDC_RESET:		/* Pulse "reset" line */
		error = vm_suspend(ctx, VM_SUSPEND_RESET);
		assert(error == 0 || errno == EALREADY);
		break;
	default:
		if (*eax >= 0x21 && *eax <= 0x3f) {
			/* read "byte N" from RAM */
			int	byten;

			byten = (*eax - 0x20) & 0x1f;
			sc->ctrlbyte = CTRL_CMD_FLAG | sc->ram[byten];
		}
		break;
	}

	pthread_mutex_unlock(&sc->mtx);

	if (sc->ctrlbyte != 0) {
		sc->status |= KBDS_KBD_BUFFER_FULL;
		sc->status &= ~KBDS_AUX_BUFFER_FULL;
		atkbdc_assert_kbd_intr(sc);
	} else if (ps2mouse_fifocnt(sc->ps2mouse_sc) > 0 &&
	           (sc->ram[0] & KBD_DISABLE_AUX_PORT) == 0) {
		sc->status |= KBDS_AUX_BUFFER_FULL | KBDS_KBD_BUFFER_FULL;
		atkbdc_assert_aux_intr(sc);
	} else if (sc->kbd.bcnt > 0 && (sc->ram[0] & KBD_DISABLE_KBD_PORT) == 0) {
		sc->status |= KBDS_KBD_BUFFER_FULL;
		atkbdc_assert_kbd_intr(sc);
	}

	return (retval);
}

void
atkbdc_event(struct atkbdc_softc *sc, int iskbd)
{
	pthread_mutex_lock(&sc->mtx);

	if (iskbd)
		atkbdc_kbd_poll(sc);
	else
		atkbdc_aux_poll(sc);
	pthread_mutex_unlock(&sc->mtx);
}

void
atkbdc_init(struct vmctx *ctx)
{
	struct inout_port iop;
	struct atkbdc_softc *sc;
	int error;

	sc = calloc(1, sizeof(struct atkbdc_softc));
	sc->ctx = ctx;

	pthread_mutex_init(&sc->mtx, NULL);

	bzero(&iop, sizeof(struct inout_port));
	iop.name = "atkdbc";
	iop.port = KBD_STS_CTL_PORT;
	iop.size = 1;
	iop.flags = IOPORT_F_INOUT;
	iop.handler = atkbdc_sts_ctl_handler;
	iop.arg = sc;

	error = register_inout(&iop);
	assert(error == 0);

	bzero(&iop, sizeof(struct inout_port));
	iop.name = "atkdbc";
	iop.port = KBD_DATA_PORT;
	iop.size = 1;
	iop.flags = IOPORT_F_INOUT;
	iop.handler = atkbdc_data_handler;
	iop.arg = sc;

	error = register_inout(&iop);
	assert(error == 0);

	pci_irq_reserve(KBD_DEV_IRQ);
	sc->kbd.irq = KBD_DEV_IRQ;

	pci_irq_reserve(AUX_DEV_IRQ);
	sc->aux.irq = AUX_DEV_IRQ;

	sc->ps2kbd_sc = ps2kbd_init(sc);
	sc->ps2mouse_sc = ps2mouse_init(sc);
}

static void
atkbdc_dsdt(void)
{

	dsdt_line("");
	dsdt_line("Device (KBD)");
	dsdt_line("{");
	dsdt_line("  Name (_HID, EisaId (\"PNP0303\"))");
	dsdt_line("  Name (_CRS, ResourceTemplate ()");
	dsdt_line("  {");
	dsdt_indent(2);
	dsdt_fixed_ioport(KBD_DATA_PORT, 1);
	dsdt_fixed_ioport(KBD_STS_CTL_PORT, 1);
	dsdt_fixed_irq(1);
	dsdt_unindent(2);
	dsdt_line("  })");
	dsdt_line("}");

	dsdt_line("");
	dsdt_line("Device (MOU)");
	dsdt_line("{");
	dsdt_line("  Name (_HID, EisaId (\"PNP0F13\"))");
	dsdt_line("  Name (_CRS, ResourceTemplate ()");
	dsdt_line("  {");
	dsdt_indent(2);
	dsdt_fixed_ioport(KBD_DATA_PORT, 1);
	dsdt_fixed_ioport(KBD_STS_CTL_PORT, 1);
	dsdt_fixed_irq(12);
	dsdt_unindent(2);
	dsdt_line("  })");
	dsdt_line("}");
}
LPC_DSDT(atkbdc_dsdt);

