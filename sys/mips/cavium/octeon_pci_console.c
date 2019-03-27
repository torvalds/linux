/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Juli Mallett <jmallett@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/reboot.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/cvmx-bootmem.h>
#include <contrib/octeon-sdk/cvmx-interrupt.h>
#include <contrib/octeon-sdk/octeon-pci-console.h>

#ifdef OCTEON_VENDOR_RADISYS
#define	OPCIC_FLAG_RSYS		(0x00000001)

#define	OPCIC_RSYS_FIFO_SIZE	(0x2000)
#endif

struct opcic_softc {
	unsigned sc_flags;
	uint64_t sc_base_addr;
};

static struct opcic_softc opcic_instance;

static cn_probe_t opcic_cnprobe;
static cn_init_t opcic_cninit;
static cn_term_t opcic_cnterm;
static cn_getc_t opcic_cngetc;
static cn_putc_t opcic_cnputc;
static cn_grab_t opcic_cngrab;
static cn_ungrab_t opcic_cnungrab;

#ifdef OCTEON_VENDOR_RADISYS
static int opcic_rsys_cngetc(struct opcic_softc *);
static void opcic_rsys_cnputc(struct opcic_softc *, int);
#endif

CONSOLE_DRIVER(opcic);

static void
opcic_cnprobe(struct consdev *cp)
{
	const struct cvmx_bootmem_named_block_desc *pci_console_block;
	struct opcic_softc *sc;

	sc = &opcic_instance;
	sc->sc_flags = 0;
	sc->sc_base_addr = 0;

	cp->cn_pri = CN_DEAD;

	switch (cvmx_sysinfo_get()->board_type) {
#ifdef OCTEON_VENDOR_RADISYS
	case CVMX_BOARD_TYPE_CUST_RADISYS_RSYS4GBE:
		pci_console_block =
		    cvmx_bootmem_find_named_block("rsys_gbl_memory");
		if (pci_console_block != NULL) {
			sc->sc_flags |= OPCIC_FLAG_RSYS;
			sc->sc_base_addr = pci_console_block->base_addr;
			break;
		}
#endif
	default:
		pci_console_block =
		    cvmx_bootmem_find_named_block(OCTEON_PCI_CONSOLE_BLOCK_NAME);
		if (pci_console_block == NULL)
			return;
		sc->sc_base_addr = pci_console_block->base_addr;
		break;
	}

	cp->cn_arg = sc;
	snprintf(cp->cn_name, sizeof cp->cn_name, "opcic@%p", cp->cn_arg);
	cp->cn_pri = (boothowto & RB_SERIAL) ? CN_REMOTE : CN_NORMAL;
}

static void
opcic_cninit(struct consdev *cp)
{
	(void)cp;
}

static void
opcic_cnterm(struct consdev *cp)
{
	(void)cp;
}

static int
opcic_cngetc(struct consdev *cp)
{
	struct opcic_softc *sc;
	char ch;
	int rv;

	sc = cp->cn_arg;

#ifdef OCTEON_VENDOR_RADISYS
	if ((sc->sc_flags & OPCIC_FLAG_RSYS) != 0)
		return (opcic_rsys_cngetc(sc));
#endif

	rv = octeon_pci_console_read(sc->sc_base_addr, 0, &ch, 1,
	    OCT_PCI_CON_FLAG_NONBLOCK);
	if (rv != 1)
		return (-1);
	return (ch);
}

static void
opcic_cnputc(struct consdev *cp, int c)
{
	struct opcic_softc *sc;
	char ch;
	int rv;

	sc = cp->cn_arg;
	ch = c;

#ifdef OCTEON_VENDOR_RADISYS
	if ((sc->sc_flags & OPCIC_FLAG_RSYS) != 0) {
		opcic_rsys_cnputc(sc, c);
		return;
	}
#endif

	rv = octeon_pci_console_write(sc->sc_base_addr, 0, &ch, 1, 0);
	if (rv == -1)
		panic("%s: octeon_pci_console_write failed.", __func__);
}

static void
opcic_cngrab(struct consdev *cp)
{
	(void)cp;
}

static void
opcic_cnungrab(struct consdev *cp)
{
	(void)cp;
}

#ifdef OCTEON_VENDOR_RADISYS
static int
opcic_rsys_cngetc(struct opcic_softc *sc)
{
	uint64_t gbl_base;
	uint64_t console_base;
	uint64_t console_rbuf;
	uint64_t console_rcnt[2];
	uint16_t rcnt[2];
	uint16_t roff;
	int c;

	gbl_base = CVMX_ADD_IO_SEG(sc->sc_base_addr);
	console_base = gbl_base + 0x10;

	console_rbuf = console_base + 0x2018;
	console_rcnt[0] = console_base + 0x08;
	console_rcnt[1] = console_base + 0x0a;

	/* Check if there is anything new in the FIFO.  */
	rcnt[0] = cvmx_read64_uint16(console_rcnt[0]);
	rcnt[1] = cvmx_read64_uint16(console_rcnt[1]);
	if (rcnt[0] == rcnt[1])
		return (-1);

	/* Get first new character in the FIFO.  */
	if (rcnt[0] != 0)
		roff = rcnt[0] - 1;
	else
		roff = OPCIC_RSYS_FIFO_SIZE - 1;
	c = cvmx_read64_uint8(console_rbuf + roff);

	/* Advance FIFO.  */
	rcnt[1] = (rcnt[1] + 1) % OPCIC_RSYS_FIFO_SIZE;
	cvmx_write64_uint16(console_rcnt[1], rcnt[1]);

	return (c);
}

static void
opcic_rsys_cnputc(struct opcic_softc *sc, int c)
{
	uint64_t gbl_base;
	uint64_t console_base;
	uint64_t console_wbuf;
	uint64_t console_wcnt;
	uint16_t wcnt;

	gbl_base = CVMX_ADD_IO_SEG(sc->sc_base_addr);
	console_base = gbl_base + 0x10;

	console_wbuf = console_base + 0x0018;
	console_wcnt = console_base + 0x0c;

	/* Append character to FIFO.  */
	wcnt = cvmx_read64_uint16(console_wcnt) % OPCIC_RSYS_FIFO_SIZE;
	cvmx_write64_uint8(console_wbuf + wcnt, (uint8_t)c);
	cvmx_write64_uint16(console_wcnt, wcnt + 1);
}
#endif
