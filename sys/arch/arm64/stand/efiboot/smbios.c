/*	$OpenBSD: smbios.c,v 1.1 2022/12/07 23:04:26 patrick Exp $	*/
/*
 * Copyright (c) 2006 Gordon Willem Klok <gklok@cogeco.ca>
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>

#include <machine/smbiosvar.h>

#include <lib/libkern/libkern.h>
#include <stand/boot/cmd.h>

#include "libsa.h"

#undef DPRINTF
#if defined(SMBIOSDEBUG)
#define DPRINTF(x...)	do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif

struct smbios_entry smbios_entry;

const char *smbios_uninfo[] = {
	"System",
	"Not ",
	"To be",
	"SYS-"
};

char smbios_bios_date[64];
char smbios_board_vendor[64];
char smbios_board_prod[64];
char smbios_board_serial[64];

void smbios_info(void);
char *fixstring(char *);

char *hw_vendor, *hw_prod, *hw_ver, *hw_serial;

void
smbios_init(void *smbios)
{
	struct smbios_struct_bios *sb;
	struct smbtable bios;
	char scratch[64];
	char *sminfop;
	uint64_t addr;

	if (smbios == NULL)
		return;

	if (strncmp(smbios, "_SM_", 4) == 0) {
		struct smbhdr *hdr = smbios;
		uint8_t *p, checksum = 0;
		int i;

		if (hdr->len != sizeof(*hdr))
			return;
		for (i = 0, p = (uint8_t *)hdr; i < hdr->len; i++)
			checksum += p[i];
		if (checksum != 0)
			return;

		DPRINTF("SMBIOS %d.%d", hdr->majrev, hdr->minrev);

		smbios_entry.len = hdr->size;
		smbios_entry.mjr = hdr->majrev;
		smbios_entry.min = hdr->minrev;
		smbios_entry.count = hdr->count;

		addr = hdr->addr;
	} else if (strncmp(smbios, "_SM3_", 5) == 0) {
		struct smb3hdr *hdr = smbios;
		uint8_t *p, checksum = 0;
		int i;

		if (hdr->len != sizeof(*hdr) || hdr->epr != 0x01)
			return;
		for (i = 0, p = (uint8_t *)hdr; i < hdr->len; i++)
			checksum += p[i];
		if (checksum != 0)
			return;

		DPRINTF("SMBIOS %d.%d.%d", hdr->majrev, hdr->minrev,
		    hdr->docrev);

		smbios_entry.len = hdr->size;
		smbios_entry.mjr = hdr->majrev;
		smbios_entry.min = hdr->minrev;
		smbios_entry.count = -1;

		addr = hdr->addr;
	} else {
		DPRINTF("Unsupported SMBIOS entry point\n");
		return;
	}

	smbios_entry.addr = (uint8_t *)addr;

	bios.cookie = 0;
	if (smbios_find_table(SMBIOS_TYPE_BIOS, &bios)) {
		sb = bios.tblhdr;
		DPRINTF("SMBIOS:");
		if ((smbios_get_string(&bios, sb->vendor,
		    scratch, sizeof(scratch))) != NULL)
			DPRINTF(" vendor %s",
			    fixstring(scratch));
		if ((smbios_get_string(&bios, sb->version,
		    scratch, sizeof(scratch))) != NULL)
			DPRINTF(" version \"%s\"",
			    fixstring(scratch));
		if ((smbios_get_string(&bios, sb->release,
		    scratch, sizeof(scratch))) != NULL) {
			sminfop = fixstring(scratch);
			if (sminfop != NULL) {
				strlcpy(smbios_bios_date,
				    sminfop,
				    sizeof(smbios_bios_date));
				DPRINTF(" date %s", sminfop);
			}
		}

		smbios_info();
		DPRINTF("\n");
	}

	return;
}

/*
 * smbios_find_table() takes a caller supplied smbios struct type and
 * a pointer to a handle (struct smbtable) returning one if the structure
 * is successfully located and zero otherwise. Callers should take care
 * to initialize the cookie field of the smbtable structure to zero before
 * the first invocation of this function.
 * Multiple tables of the same type can be located by repeatedly calling
 * smbios_find_table with the same arguments.
 */
int
smbios_find_table(uint8_t type, struct smbtable *st)
{
	uint8_t *va, *end;
	struct smbtblhdr *hdr;
	int ret = 0, tcount = 1;

	va = smbios_entry.addr;
	end = va + smbios_entry.len;

	/*
	 * The cookie field of the smtable structure is used to locate
	 * multiple instances of a table of an arbitrary type. Following the
	 * successful location of a table, the type is encoded as bits 0:7 of
	 * the cookie value, the offset in terms of the number of structures
	 * preceding that referenced by the handle is encoded in bits 15:31.
	 */
	if ((st->cookie & 0xfff) == type && st->cookie >> 16) {
		if ((uint8_t *)st->hdr >= va && (uint8_t *)st->hdr < end) {
			hdr = st->hdr;
			if (hdr->type == type) {
				va = (uint8_t *)hdr + hdr->size;
				for (; va + 1 < end; va++)
					if (*va == 0 && *(va + 1) == 0)
						break;
				va += 2;
				tcount = st->cookie >> 16;
			}
		}
	}
	for (; va + sizeof(struct smbtblhdr) < end &&
	    tcount <= smbios_entry.count; tcount++) {
		hdr = (struct smbtblhdr *)va;
		if (hdr->type == type) {
			ret = 1;
			st->hdr = hdr;
			st->tblhdr = va + sizeof(struct smbtblhdr);
			st->cookie = (tcount + 1) << 16 | type;
			break;
		}
		if (hdr->type == SMBIOS_TYPE_EOT)
			break;
		va += hdr->size;
		for (; va + 1 < end; va++)
			if (*va == 0 && *(va + 1) == 0)
				break;
		va += 2;
	}
	return ret;
}

char *
smbios_get_string(struct smbtable *st, uint8_t indx, char *dest, size_t len)
{
	uint8_t *va, *end;
	char *ret = NULL;
	int i;

	va = (uint8_t *)st->hdr + st->hdr->size;
	end = smbios_entry.addr + smbios_entry.len;
	for (i = 1; va < end && i < indx && *va; i++)
		while (*va++)
			;
	if (i == indx) {
		if (va + len < end) {
			ret = dest;
			memcpy(ret, va, len);
			ret[len - 1] = '\0';
		}
	}

	return ret;
}

char *
fixstring(char *s)
{
	char *p, *e;
#if 0
	int i;

	for (i = 0; i < nitems(smbios_uninfo); i++)
		if ((strncasecmp(s, smbios_uninfo[i],
		    strlen(smbios_uninfo[i]))) == 0)
			return NULL;
#endif
	/*
	 * Remove leading and trailing whitespace
	 */
	for (p = s; *p == ' '; p++)
		;
	/*
	 * Special case entire string is whitespace
	 */
	if (p == s + strlen(s))
		return NULL;
	for (e = s + strlen(s) - 1; e > s && *e == ' '; e--)
		;
	if (p > s || e < s + strlen(s) - 1) {
		memmove(s, p, e - p + 1);
		s[e - p + 1] = '\0';
	}

	return s;
}

void
smbios_info(void)
{
	char *sminfop, sminfo[64];
	struct smbtable stbl, btbl;
	struct smbios_sys *sys;
	struct smbios_board *board;
	int infolen, havebb;
	char *p;

	if (smbios_entry.mjr < 2)
		return;
	/*
	 * According to the spec the system table among others is required,
	 * if it is not we do not bother with this smbios implementation.
	 */
	stbl.cookie = btbl.cookie = 0;
	if (!smbios_find_table(SMBIOS_TYPE_SYSTEM, &stbl))
		return;
	havebb = smbios_find_table(SMBIOS_TYPE_BASEBOARD, &btbl);

	sys = (struct smbios_sys *)stbl.tblhdr;
	if (havebb) {
		board = (struct smbios_board *)btbl.tblhdr;

		sminfop = NULL;
		if ((p = smbios_get_string(&btbl, board->vendor,
		    sminfo, sizeof(sminfo))) != NULL)
			sminfop = fixstring(p);
		if (sminfop)
			strlcpy(smbios_board_vendor, sminfop,
			    sizeof(smbios_board_vendor));

		sminfop = NULL;
		if ((p = smbios_get_string(&btbl, board->product,
		    sminfo, sizeof(sminfo))) != NULL)
			sminfop = fixstring(p);
		if (sminfop)
			strlcpy(smbios_board_prod, sminfop,
			    sizeof(smbios_board_prod));

		sminfop = NULL;
		if ((p = smbios_get_string(&btbl, board->serial,
		    sminfo, sizeof(sminfo))) != NULL)
			sminfop = fixstring(p);
		if (sminfop)
			strlcpy(smbios_board_serial, sminfop,
			    sizeof(smbios_board_serial));
	}
	/*
	 * Some smbios implementations have no system vendor or
	 * product strings, some have very uninformative data which is
	 * harder to work around and we must rely upon various
	 * heuristics to detect this. In both cases we attempt to fall
	 * back on the base board information in the perhaps naive
	 * belief that motherboard vendors will supply this
	 * information.
	 */
	sminfop = NULL;
	if ((p = smbios_get_string(&stbl, sys->vendor, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop == NULL) {
		if (havebb) {
			if ((p = smbios_get_string(&btbl, board->vendor,
			    sminfo, sizeof(sminfo))) != NULL)
				sminfop = fixstring(p);
		}
	}
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_vendor = alloc(infolen);
		if (hw_vendor)
			strlcpy(hw_vendor, sminfop, infolen);
		sminfop = NULL;
	}
	if ((p = smbios_get_string(&stbl, sys->product, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop == NULL) {
		if (havebb) {
			if ((p = smbios_get_string(&btbl, board->product,
			    sminfo, sizeof(sminfo))) != NULL)
				sminfop = fixstring(p);
		}
	}
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_prod = alloc(infolen);
		if (hw_prod)
			strlcpy(hw_prod, sminfop, infolen);
		sminfop = NULL;
	}
	if (hw_vendor != NULL && hw_prod != NULL)
		DPRINTF("\nSMBIOS: %s %s", hw_vendor, hw_prod);
	if ((p = smbios_get_string(&stbl, sys->version, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_ver = alloc(infolen);
		if (hw_ver)
			strlcpy(hw_ver, sminfop, infolen);
		sminfop = NULL;
	}
	if ((p = smbios_get_string(&stbl, sys->serial, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_serial = alloc(infolen);
		if (hw_serial)
			strlcpy(hw_serial, sminfop, infolen);
	}
}
