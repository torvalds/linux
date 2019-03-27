/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>

#include <stdio.h>

#include "bhyverun.h"
#include "pci_emul.h"
#include "uart_emul.h"

/*
 * Pick a PCI vid/did of a chip with a single uart at
 * BAR0, that most versions of FreeBSD can understand:
 * Siig CyberSerial 1-port.
 */
#define COM_VENDOR	0x131f
#define COM_DEV		0x2000

static void
pci_uart_intr_assert(void *arg)
{
	struct pci_devinst *pi = arg;

	pci_lintr_assert(pi);
}

static void
pci_uart_intr_deassert(void *arg)
{
	struct pci_devinst *pi = arg;

	pci_lintr_deassert(pi);
}

static void
pci_uart_write(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
	       int baridx, uint64_t offset, int size, uint64_t value)
{

	assert(baridx == 0);
	assert(size == 1);

	uart_write(pi->pi_arg, offset, value);
}

uint64_t
pci_uart_read(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
	      int baridx, uint64_t offset, int size)
{
	uint8_t val;

	assert(baridx == 0);
	assert(size == 1);

	val = uart_read(pi->pi_arg, offset);
	return (val);
}

static int
pci_uart_init(struct vmctx *ctx, struct pci_devinst *pi, char *opts)
{
	struct uart_softc *sc;

	pci_emul_alloc_bar(pi, 0, PCIBAR_IO, UART_IO_BAR_SIZE);
	pci_lintr_request(pi);

	/* initialize config space */
	pci_set_cfgdata16(pi, PCIR_DEVICE, COM_DEV);
	pci_set_cfgdata16(pi, PCIR_VENDOR, COM_VENDOR);
	pci_set_cfgdata8(pi, PCIR_CLASS, PCIC_SIMPLECOMM);

	sc = uart_init(pci_uart_intr_assert, pci_uart_intr_deassert, pi);
	pi->pi_arg = sc;

	if (uart_set_backend(sc, opts) != 0) {
		fprintf(stderr, "Unable to initialize backend '%s' for "
		    "pci uart at %d:%d\n", opts, pi->pi_slot, pi->pi_func);
		return (-1);
	}

	return (0);
}

struct pci_devemu pci_de_com = {
	.pe_emu =	"uart",
	.pe_init =	pci_uart_init,
	.pe_barwrite =	pci_uart_write,
	.pe_barread =	pci_uart_read
};
PCI_EMUL_SET(pci_de_com);
