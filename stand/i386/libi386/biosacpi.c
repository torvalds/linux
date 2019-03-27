/*-
 * Copyright (c) 2001 Michael Smith <msmith@freebsd.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <machine/stdarg.h>
#include <bootstrap.h>
#include <btxv86.h>
#include "libi386.h"

#include "platform/acfreebsd.h"
#include "acconfig.h"
#define ACPI_SYSTEM_XFACE
#include "actypes.h"
#include "actbl.h"

/*
 * Detect ACPI and export information about the ACPI BIOS into the
 * environment.
 */

static ACPI_TABLE_RSDP	*biosacpi_find_rsdp(void);
static ACPI_TABLE_RSDP	*biosacpi_search_rsdp(char *base, int length);

#define RSDP_CHECKSUM_LENGTH 20

void
biosacpi_detect(void)
{
    ACPI_TABLE_RSDP	*rsdp;
    char		buf[24];
    int			revision;

    /* locate and validate the RSDP */
    if ((rsdp = biosacpi_find_rsdp()) == NULL)
	return;

    /*
     * Report the RSDP to the kernel. While this can be found with
     * a BIOS boot, the RSDP may be elsewhere when booted from UEFI.
     * The old code used the 'hints' method to communite this to
     * the kernel. However, while convenient, the 'hints' method
     * is fragile and does not work when static hints are compiled
     * into the kernel. Instead, move to setting different tunables
     * that start with acpi. The old 'hints' can be removed before
     * we branch for FreeBSD 12.
     */
    sprintf(buf, "0x%08x", VTOP(rsdp));
    setenv("hint.acpi.0.rsdp", buf, 1);
    setenv("acpi.rsdp", buf, 1);
    revision = rsdp->Revision;
    if (revision == 0)
	revision = 1;
    sprintf(buf, "%d", revision);
    setenv("hint.acpi.0.revision", buf, 1);
    setenv("acpi.revision", buf, 1);
    strncpy(buf, rsdp->OemId, sizeof(rsdp->OemId));
    buf[sizeof(rsdp->OemId)] = '\0';
    setenv("hint.acpi.0.oem", buf, 1);
    setenv("acpi.oem", buf, 1);
    sprintf(buf, "0x%08x", rsdp->RsdtPhysicalAddress);
    setenv("hint.acpi.0.rsdt", buf, 1);
    setenv("acpi.rsdt", buf, 1);
    if (revision >= 2) {
	/* XXX extended checksum? */
	sprintf(buf, "0x%016llx", rsdp->XsdtPhysicalAddress);
	setenv("hint.acpi.0.xsdt", buf, 1);
	setenv("acpi.xsdt", buf, 1);
	sprintf(buf, "%d", rsdp->Length);
	setenv("hint.acpi.0.xsdt_length", buf, 1);
	setenv("acpi.xsdt_length", buf, 1);
    }
}

/*
 * Find the RSDP in low memory.  See section 5.2.2 of the ACPI spec.
 */
static ACPI_TABLE_RSDP *
biosacpi_find_rsdp(void)
{
    ACPI_TABLE_RSDP	*rsdp;
    uint16_t		*addr;

    /* EBDA is the 1 KB addressed by the 16 bit pointer at 0x40E. */
    addr = (uint16_t *)PTOV(0x40E);
    if ((rsdp = biosacpi_search_rsdp((char *)(*addr << 4), 0x400)) != NULL)
	return (rsdp);

    /* Check the upper memory BIOS space, 0xe0000 - 0xfffff. */
    if ((rsdp = biosacpi_search_rsdp((char *)0xe0000, 0x20000)) != NULL)
	return (rsdp);

    return (NULL);
}

static ACPI_TABLE_RSDP *
biosacpi_search_rsdp(char *base, int length)
{
    ACPI_TABLE_RSDP	*rsdp;
    uint8_t		*cp, sum;
    int			ofs, idx;

    /* search on 16-byte boundaries */
    for (ofs = 0; ofs < length; ofs += 16) {
	rsdp = (ACPI_TABLE_RSDP *)PTOV(base + ofs);

	/* compare signature, validate checksum */
	if (!strncmp(rsdp->Signature, ACPI_SIG_RSDP, strlen(ACPI_SIG_RSDP))) {
	    cp = (uint8_t *)rsdp;
	    sum = 0;
	    for (idx = 0; idx < RSDP_CHECKSUM_LENGTH; idx++)
		sum += *(cp + idx);
	    if (sum != 0)
		continue;
	    return(rsdp);
	}
    }
    return(NULL);
}
