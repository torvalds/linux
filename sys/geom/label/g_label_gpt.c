/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Marius Nuennerich
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/kobj.h>
#include <sys/gpt.h>
#include <sys/sbuf.h>

#include <geom/geom.h>
#include <geom/label/g_label.h>
#include <geom/part/g_part.h>

#define	PART_CLASS_NAME	"PART"
#define	SCHEME_NAME	"GPT"

#define	G_LABEL_GPT_VOLUME_DIR	"gpt"
#define	G_LABEL_GPT_ID_DIR	"gptid"

/* XXX: Also defined in geom/part/g_part_gpt.c */
struct g_part_gpt_entry {
	struct g_part_entry     base;
	struct gpt_ent          ent;
};

/* XXX: Shamelessly stolen from g_part_gpt.c */
static void
sbuf_nprintf_utf16(struct sbuf *sb, uint16_t *str, size_t len)
{
	u_int bo;
	uint32_t ch;
	uint16_t c;

	bo = LITTLE_ENDIAN;	/* GPT is little-endian */
	while (len > 0 && *str != 0) {
		ch = (bo == BIG_ENDIAN) ? be16toh(*str) : le16toh(*str);
		str++, len--;
		if ((ch & 0xf800) == 0xd800) {
			if (len > 0) {
				c = (bo == BIG_ENDIAN) ? be16toh(*str)
				    : le16toh(*str);
				str++, len--;
			} else
				c = 0xfffd;
			if ((ch & 0x400) == 0 && (c & 0xfc00) == 0xdc00) {
				ch = ((ch & 0x3ff) << 10) + (c & 0x3ff);
				ch += 0x10000;
			} else
				ch = 0xfffd;
		} else if (ch == 0xfffe) { /* BOM (U+FEFF) swapped. */
			bo = (bo == BIG_ENDIAN) ? LITTLE_ENDIAN : BIG_ENDIAN;
			continue;
		} else if (ch == 0xfeff) /* BOM (U+FEFF) unswapped. */
			continue;

		/* Write the Unicode character in UTF-8 */
		if (ch < 0x80)
			sbuf_printf(sb, "%c", ch);
		else if (ch < 0x800)
			sbuf_printf(sb, "%c%c", 0xc0 | (ch >> 6),
			    0x80 | (ch & 0x3f));
		else if (ch < 0x10000)
			sbuf_printf(sb, "%c%c%c", 0xe0 | (ch >> 12),
			    0x80 | ((ch >> 6) & 0x3f), 0x80 | (ch & 0x3f));
		else if (ch < 0x200000)
			sbuf_printf(sb, "%c%c%c%c", 0xf0 | (ch >> 18),
			    0x80 | ((ch >> 12) & 0x3f),
			    0x80 | ((ch >> 6) & 0x3f), 0x80 | (ch & 0x3f));
	}
}

static void
g_label_gpt_taste(struct g_consumer *cp, char *label, size_t size)
{
	struct g_provider *pp;
	struct g_part_table *tp;
	struct g_part_gpt_entry *part_gpt_entry;
	struct sbuf *lbl;

	g_topology_assert_not();
	pp = cp->provider;
	tp = (struct g_part_table *)pp->geom->softc;
	label[0] = '\0';

	/* We taste only partitions handled by GPART */
	if (strncmp(pp->geom->class->name, PART_CLASS_NAME, sizeof(PART_CLASS_NAME)))
		return;
	/* and only GPT */
	if (strncmp(tp->gpt_scheme->name, SCHEME_NAME, sizeof(SCHEME_NAME)))
		return;

	part_gpt_entry = (struct g_part_gpt_entry *)pp->private;

	/*
	 * Create sbuf with biggest possible size.
	 * We need max. 4 bytes for every 2-byte utf16 char.
	 */
	lbl = sbuf_new(NULL, NULL, sizeof(part_gpt_entry->ent.ent_name) << 1, SBUF_FIXEDLEN);
	/* Size is the number of characters, not bytes */
	sbuf_nprintf_utf16(lbl, part_gpt_entry->ent.ent_name, sizeof(part_gpt_entry->ent.ent_name) >> 1);
	sbuf_finish(lbl);
	strlcpy(label, sbuf_data(lbl), size);
	sbuf_delete(lbl);
}

static void
g_label_gpt_uuid_taste(struct g_consumer *cp, char *label, size_t size)
{
	struct g_provider *pp;
	struct g_part_table *tp;
	struct g_part_gpt_entry *part_gpt_entry;

	g_topology_assert_not();
	pp = cp->provider;
	tp = (struct g_part_table *)pp->geom->softc;
	label[0] = '\0';

	/* We taste only partitions handled by GPART */
	if (strncmp(pp->geom->class->name, PART_CLASS_NAME, sizeof(PART_CLASS_NAME)))
		return;
	/* and only GPT */
	if (strncmp(tp->gpt_scheme->name, SCHEME_NAME, sizeof(SCHEME_NAME)))
		return;

	part_gpt_entry = (struct g_part_gpt_entry *)pp->private;
	snprintf_uuid(label, size, &part_gpt_entry->ent.ent_uuid);
}

struct g_label_desc g_label_gpt = {
	.ld_taste = g_label_gpt_taste,
	.ld_dir = G_LABEL_GPT_VOLUME_DIR,
	.ld_enabled = 1
};

struct g_label_desc g_label_gpt_uuid = {
	.ld_taste = g_label_gpt_uuid_taste,
	.ld_dir = G_LABEL_GPT_ID_DIR,
	.ld_enabled = 1
};

G_LABEL_INIT(gpt, g_label_gpt, "Create device nodes for GPT labels");
G_LABEL_INIT(gptid, g_label_gpt_uuid, "Create device nodes for GPT UUIDs");
