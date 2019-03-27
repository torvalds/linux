/*-
 * Copyright (c) 2004 Nate Lawson (SDG)
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

#include <sys/param.h>
#include <sys/bus.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>

enum ops_t {
	OP_NONE,
	OP_LEQ,
	OP_GEQ,
	OP_EQL,
};

enum val_t {
	OEM,
	OEM_REV,
	CREATOR,
	CREATOR_REV,
};

struct acpi_q_rule {
    char	sig[ACPI_NAME_SIZE];	/* Table signature to match */
    enum val_t	val;
    union {
	char	*id;
	enum ops_t op;
    } x;
    union {
	char	*tid;
	int	rev;
    } y;
};

struct acpi_q_entry {
    const struct acpi_q_rule *match;
    int		quirks;
};

#include "acpi_quirks.h"

static int	aq_revcmp(int revision, enum ops_t op, int value);
static int	aq_strcmp(char *actual, char *possible);
static int	aq_match_header(ACPI_TABLE_HEADER *hdr,
		    const struct acpi_q_rule *match);

static int
aq_revcmp(int revision, enum ops_t op, int value)
{
    switch (op) {
    case OP_LEQ:
	if (revision <= value)
	    return (TRUE);
	break;
    case OP_GEQ:
	if (revision >= value)
	    return (TRUE);
	break;
    case OP_EQL:
	if (revision == value)
	    return (TRUE);
	break;
    case OP_NONE:
	return (TRUE);
    default:
	panic("aq_revcmp: invalid op %d", op);
    }

    return (FALSE);
}

static int
aq_strcmp(char *actual, char *possible)
{
    if (actual == NULL || possible == NULL)
	return (TRUE);
    return (strncmp(actual, possible, strlen(possible)) == 0);
}

static int
aq_match_header(ACPI_TABLE_HEADER *hdr, const struct acpi_q_rule *match)
{
    int result;

    result = FALSE;
    switch (match->val) {
    case OEM:
	if (aq_strcmp(hdr->OemId, match->x.id) &&
	    aq_strcmp(hdr->OemTableId, match->y.tid))
	    result = TRUE;
	break;
    case CREATOR:
	if (aq_strcmp(hdr->AslCompilerId, match->x.id))
	    result = TRUE;
	break;
    case OEM_REV:
	if (aq_revcmp(hdr->OemRevision, match->x.op, match->y.rev))
	    result = TRUE;
	break;
    case CREATOR_REV:
	if (aq_revcmp(hdr->AslCompilerRevision, match->x.op, match->y.rev))
	    result = TRUE;
	break;
    }

    return (result);
}

int
acpi_table_quirks(int *quirks)
{
    const struct acpi_q_entry *entry;
    const struct acpi_q_rule *match;
    ACPI_TABLE_HEADER fadt, dsdt, xsdt, *hdr;
    int done;

    /* First, allow the machdep system to set its idea of quirks. */
    KASSERT(quirks != NULL, ("acpi quirks ptr is NULL"));
    acpi_machdep_quirks(quirks);

    if (ACPI_FAILURE(AcpiGetTableHeader(ACPI_SIG_FADT, 0, &fadt)))
	bzero(&fadt, sizeof(fadt));
    if (ACPI_FAILURE(AcpiGetTableHeader(ACPI_SIG_DSDT, 0, &dsdt)))
	bzero(&dsdt, sizeof(dsdt));
    if (ACPI_FAILURE(AcpiGetTableHeader(ACPI_SIG_XSDT, 0, &xsdt)))
	bzero(&xsdt, sizeof(xsdt));

    /* Then, override the quirks with any matched from table signatures. */
    for (entry = acpi_quirks_table; entry->match; entry++) {
	done = TRUE;
	for (match = entry->match; match->sig[0] != '\0'; match++) {
	    if (!strncmp(match->sig, "FADT", ACPI_NAME_SIZE))
		hdr = &fadt;
	    else if (!strncmp(match->sig, ACPI_SIG_DSDT, ACPI_NAME_SIZE))
		hdr = &dsdt;
	    else if (!strncmp(match->sig, ACPI_SIG_XSDT, ACPI_NAME_SIZE))
		hdr = &xsdt;
	    else
		panic("invalid quirk header\n");

	    /* If we don't match any, skip to the next entry. */
	    if (aq_match_header(hdr, match) == FALSE) {
		done = FALSE;
		break;
	    }
	}

	/* If all entries matched, update the quirks and return. */
	if (done) {
	    *quirks = entry->quirks;
	    break;
	}
    }

    return (0);
}
