/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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

#ifndef lint
static const char rcsid[] =
    "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/pciio.h>

#include <err.h>
#include <stdio.h>

#include <dev/pci/pcireg.h>

#include "pciconf.h"

struct bit_table {
	uint32_t mask;
	const char *desc;
};

/* Error indicators in the PCI status register (PCIR_STATUS). */
static struct bit_table pci_status[] = {
	{ PCIM_STATUS_MDPERR, "Master Data Parity Error" },
	{ PCIM_STATUS_STABORT, "Sent Target-Abort" },
	{ PCIM_STATUS_RTABORT, "Received Target-Abort" },
	{ PCIM_STATUS_RMABORT, "Received Master-Abort" },
	{ PCIM_STATUS_SERR, "Signalled System Error" },
	{ PCIM_STATUS_PERR, "Detected Parity Error" },
	{ 0, NULL },
};

/* Valid error indicator bits in PCIR_STATUS. */
#define	PCI_ERRORS	(PCIM_STATUS_MDPERR | PCIM_STATUS_STABORT |	\
			 PCIM_STATUS_RTABORT | PCIM_STATUS_RMABORT |	\
			 PCIM_STATUS_SERR | PCIM_STATUS_PERR)

/* Error indicators in the PCI-Express device status register. */
static struct bit_table pcie_device_status[] = {
	{ PCIEM_STA_CORRECTABLE_ERROR, "Correctable Error Detected" },
	{ PCIEM_STA_NON_FATAL_ERROR, "Non-Fatal Error Detected" },	
	{ PCIEM_STA_FATAL_ERROR, "Fatal Error Detected" },	
	{ PCIEM_STA_UNSUPPORTED_REQ, "Unsupported Request Detected" },	
	{ 0, NULL },
};

/* Valid error indicator bits in the PCI-Express device status register. */
#define	PCIE_ERRORS	(PCIEM_STA_CORRECTABLE_ERROR |		\
			 PCIEM_STA_NON_FATAL_ERROR |			\
			 PCIEM_STA_FATAL_ERROR |			\
			 PCIEM_STA_UNSUPPORTED_REQ)

/* AER Uncorrected errors. */
static struct bit_table aer_uc[] = {
	{ PCIM_AER_UC_TRAINING_ERROR, "Link Training Error" },
	{ PCIM_AER_UC_DL_PROTOCOL_ERROR, "Data Link Protocol Error" },
	{ PCIM_AER_UC_SURPRISE_LINK_DOWN, "Surprise Link Down Error" },
	{ PCIM_AER_UC_POISONED_TLP, "Poisoned TLP" },
	{ PCIM_AER_UC_FC_PROTOCOL_ERROR, "Flow Control Protocol Error" },
	{ PCIM_AER_UC_COMPLETION_TIMEOUT, "Completion Timeout" },
	{ PCIM_AER_UC_COMPLETER_ABORT, "Completer Abort" },
	{ PCIM_AER_UC_UNEXPECTED_COMPLETION, "Unexpected Completion" },
	{ PCIM_AER_UC_RECEIVER_OVERFLOW, "Receiver Overflow Error" },
	{ PCIM_AER_UC_MALFORMED_TLP, "Malformed TLP" },
	{ PCIM_AER_UC_ECRC_ERROR, "ECRC Error" },
	{ PCIM_AER_UC_UNSUPPORTED_REQUEST, "Unsupported Request" },
	{ PCIM_AER_UC_ACS_VIOLATION, "ACS Violation" },
	{ PCIM_AER_UC_INTERNAL_ERROR, "Uncorrectable Internal Error" },
	{ PCIM_AER_UC_MC_BLOCKED_TLP, "MC Blocked TLP" },
	{ PCIM_AER_UC_ATOMIC_EGRESS_BLK, "AtomicOp Egress Blocked" },
	{ PCIM_AER_UC_TLP_PREFIX_BLOCKED, "TLP Prefix Blocked Error" },
	{ 0, NULL },
};

/* AER Corrected errors. */
static struct bit_table aer_cor[] = {
	{ PCIM_AER_COR_RECEIVER_ERROR, "Receiver Error" },
	{ PCIM_AER_COR_BAD_TLP, "Bad TLP" },
	{ PCIM_AER_COR_BAD_DLLP, "Bad DLLP" },
	{ PCIM_AER_COR_REPLAY_ROLLOVER, "REPLAY_NUM Rollover" },
	{ PCIM_AER_COR_REPLAY_TIMEOUT, "Replay Timer Timeout" },
	{ PCIM_AER_COR_ADVISORY_NF_ERROR, "Advisory Non-Fatal Error" },
	{ PCIM_AER_COR_INTERNAL_ERROR, "Corrected Internal Error" },
	{ PCIM_AER_COR_HEADER_LOG_OVFLOW, "Header Log Overflow" },
	{ 0, NULL },
};

static void
print_bits(const char *header, struct bit_table *table, uint32_t mask)
{
	int first;

	first = 1;
	for (; table->desc != NULL; table++)
		if (mask & table->mask) {
			if (first) {
				printf("%14s = ", header);
				first = 0;
			} else
				printf("                 ");
			printf("%s\n", table->desc);
			mask &= ~table->mask;
		}
	if (mask != 0) {
		if (first)
			printf("%14s = ", header);
		else
			printf("                 ");
		printf("Unknown: 0x%08x\n", mask);
	}
}

void
list_errors(int fd, struct pci_conf *p)
{
	uint32_t mask, severity;
	uint16_t sta, aer;
	uint8_t pcie;

	/* First check for standard PCI errors. */
	sta = read_config(fd, &p->pc_sel, PCIR_STATUS, 2);
	print_bits("PCI errors", pci_status, sta & PCI_ERRORS);

	/* See if this is a PCI-express device. */
	pcie = pci_find_cap(fd, p, PCIY_EXPRESS);
	if (pcie == 0)
		return;

	/* Check for PCI-e errors. */
	sta = read_config(fd, &p->pc_sel, pcie + PCIER_DEVICE_STA, 2);
	print_bits("PCI-e errors", pcie_device_status, sta & PCIE_ERRORS);

	/* See if this device supports AER. */
	aer = pcie_find_cap(fd, p, PCIZ_AER);
	if (aer == 0)
		return;

	/* Check for uncorrected errors. */
	mask = read_config(fd, &p->pc_sel, aer + PCIR_AER_UC_STATUS, 4);
        severity = read_config(fd, &p->pc_sel, aer + PCIR_AER_UC_SEVERITY, 4);
	print_bits("Fatal", aer_uc, mask & severity);
	print_bits("Non-fatal", aer_uc, mask & ~severity);

	/* Check for corrected errors. */
	mask = read_config(fd, &p->pc_sel, aer + PCIR_AER_COR_STATUS, 4);
	print_bits("Corrected", aer_cor, mask);
}
