/*-
 * Copyright (c) 2009, 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Ed Schouten under sponsorship from the
 * FreeBSD Foundation.
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

#include <sys/types.h>
#include <sys/fnv_hash.h>
#include <sys/endian.h>
#include <sys/param.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VFNT_MAPS 4
#define VFNT_MAP_NORMAL 0
#define VFNT_MAP_NORMAL_RH 1
#define VFNT_MAP_BOLD 2
#define VFNT_MAP_BOLD_RH 3

static unsigned int width = 8, wbytes, height = 16;

struct glyph {
	TAILQ_ENTRY(glyph)	 g_list;
	SLIST_ENTRY(glyph)	 g_hash;
	uint8_t			*g_data;
	unsigned int		 g_index;
};

#define	FONTCVT_NHASH 4096
TAILQ_HEAD(glyph_list, glyph);
static SLIST_HEAD(, glyph) glyph_hash[FONTCVT_NHASH];
static struct glyph_list glyphs[VFNT_MAPS] = {
	TAILQ_HEAD_INITIALIZER(glyphs[0]),
	TAILQ_HEAD_INITIALIZER(glyphs[1]),
	TAILQ_HEAD_INITIALIZER(glyphs[2]),
	TAILQ_HEAD_INITIALIZER(glyphs[3]),
};
static unsigned int glyph_total, glyph_count[4], glyph_unique, glyph_dupe;

struct mapping {
	TAILQ_ENTRY(mapping)	 m_list;
	unsigned int		 m_char;
	unsigned int		 m_length;
	struct glyph		*m_glyph;
};

TAILQ_HEAD(mapping_list, mapping);
static struct mapping_list maps[VFNT_MAPS] = {
	TAILQ_HEAD_INITIALIZER(maps[0]),
	TAILQ_HEAD_INITIALIZER(maps[1]),
	TAILQ_HEAD_INITIALIZER(maps[2]),
	TAILQ_HEAD_INITIALIZER(maps[3]),
};
static unsigned int mapping_total, map_count[4], map_folded_count[4],
    mapping_unique, mapping_dupe;

static void
usage(void)
{

	(void)fprintf(stderr,
"usage: vtfontcvt [-w width] [-h height] [-v] normal.bdf [bold.bdf] out.fnt\n");
	exit(1);
}

static void *
xmalloc(size_t size)
{
	void *m;

	if ((m = malloc(size)) == NULL)
		errx(1, "memory allocation failure");
	return (m);
}

static int
add_mapping(struct glyph *gl, unsigned int c, unsigned int map_idx)
{
	struct mapping *mp;
	struct mapping_list *ml;

	mapping_total++;

	mp = xmalloc(sizeof *mp);
	mp->m_char = c;
	mp->m_glyph = gl;
	mp->m_length = 0;

	ml = &maps[map_idx];
	if (TAILQ_LAST(ml, mapping_list) != NULL &&
	    TAILQ_LAST(ml, mapping_list)->m_char >= c)
		errx(1, "Bad ordering at character %u", c);
	TAILQ_INSERT_TAIL(ml, mp, m_list);

	map_count[map_idx]++;
	mapping_unique++;

	return (0);
}

static int
dedup_mapping(unsigned int map_idx)
{
	struct mapping *mp_bold, *mp_normal, *mp_temp;
	unsigned normal_map_idx = map_idx - VFNT_MAP_BOLD;

	assert(map_idx == VFNT_MAP_BOLD || map_idx == VFNT_MAP_BOLD_RH);
	mp_normal = TAILQ_FIRST(&maps[normal_map_idx]);
	TAILQ_FOREACH_SAFE(mp_bold, &maps[map_idx], m_list, mp_temp) {
		while (mp_normal->m_char < mp_bold->m_char)
			mp_normal = TAILQ_NEXT(mp_normal, m_list);
		if (mp_bold->m_char != mp_normal->m_char)
			errx(1, "Character %u not in normal font!",
			    mp_bold->m_char);
		if (mp_bold->m_glyph != mp_normal->m_glyph)
			continue;

		/* No mapping is needed if it's equal to the normal mapping. */
		TAILQ_REMOVE(&maps[map_idx], mp_bold, m_list);
		free(mp_bold);
		mapping_dupe++;
	}
	return (0);
}

static struct glyph *
add_glyph(const uint8_t *bytes, unsigned int map_idx, int fallback)
{
	struct glyph *gl;
	int hash;

	glyph_total++;
	glyph_count[map_idx]++;

	hash = fnv_32_buf(bytes, wbytes * height, FNV1_32_INIT) % FONTCVT_NHASH;
	SLIST_FOREACH(gl, &glyph_hash[hash], g_hash) {
		if (memcmp(gl->g_data, bytes, wbytes * height) == 0) {
			glyph_dupe++;
			return (gl);
		}
	}

	gl = xmalloc(sizeof *gl);
	gl->g_data = xmalloc(wbytes * height);
	memcpy(gl->g_data, bytes, wbytes * height);
	if (fallback)
		TAILQ_INSERT_HEAD(&glyphs[map_idx], gl, g_list);
	else
		TAILQ_INSERT_TAIL(&glyphs[map_idx], gl, g_list);
	SLIST_INSERT_HEAD(&glyph_hash[hash], gl, g_hash);

	glyph_unique++;
	return (gl);
}

static int
add_char(unsigned curchar, unsigned map_idx, uint8_t *bytes, uint8_t *bytes_r)
{
	struct glyph *gl;

	/* Prevent adding two glyphs for 0xFFFD */
	if (curchar == 0xFFFD) {
		if (map_idx < VFNT_MAP_BOLD)
			gl = add_glyph(bytes, 0, 1);
	} else if (curchar >= 0x20) {
		gl = add_glyph(bytes, map_idx, 0);
		if (add_mapping(gl, curchar, map_idx) != 0)
			return (1);
		if (bytes_r != NULL) {
			gl = add_glyph(bytes_r, map_idx + 1, 0);
			if (add_mapping(gl, curchar,
			    map_idx + 1) != 0)
				return (1);
		}
	}
	return (0);
}


static int
parse_bitmap_line(uint8_t *left, uint8_t *right, unsigned int line,
    unsigned int dwidth)
{
	uint8_t *p;
	unsigned int i, subline;

	if (dwidth != width && dwidth != width * 2)
		errx(1, "Bitmap with unsupported width %u!", dwidth);

	/* Move pixel data right to simplify splitting double characters. */
	line >>= (howmany(dwidth, 8) * 8) - dwidth;

	for (i = dwidth / width; i > 0; i--) {
		p = (i == 2) ? right : left;

		subline = line & ((1 << width) - 1);
		subline <<= (howmany(width, 8) * 8) - width;

		if (wbytes == 1) {
			*p = subline;
		} else if (wbytes == 2) {
			*p++ = subline >> 8;
			*p = subline;
		} else {
			errx(1, "Unsupported wbytes %u!", wbytes);
		}

		line >>= width;
	}

	return (0);
}

static int
parse_bdf(FILE *fp, unsigned int map_idx)
{
	char *ln;
	size_t length;
	uint8_t bytes[wbytes * height], bytes_r[wbytes * height];
	unsigned int curchar = 0, dwidth = 0, i, line;

	while ((ln = fgetln(fp, &length)) != NULL) {
		ln[length - 1] = '\0';

		if (strncmp(ln, "ENCODING ", 9) == 0) {
			curchar = atoi(ln + 9);
		}

		if (strncmp(ln, "DWIDTH ", 7) == 0) {
			dwidth = atoi(ln + 7);
		}

		if (strncmp(ln, "BITMAP", 6) == 0 &&
		    (ln[6] == ' ' || ln[6] == '\0')) {
			/*
			 * Assume that the next _height_ lines are bitmap
			 * data.  ENDCHAR is allowed to terminate the bitmap
			 * early but is not otherwise checked; any extra data
			 * is ignored.
			 */
			for (i = 0; i < height; i++) {
				if ((ln = fgetln(fp, &length)) == NULL)
					errx(1, "Unexpected EOF!");
				ln[length - 1] = '\0';
				if (strcmp(ln, "ENDCHAR") == 0) {
					memset(bytes + i * wbytes, 0,
					    (height - i) * wbytes);
					memset(bytes_r + i * wbytes, 0,
					    (height - i) * wbytes);
					break;
				}
				sscanf(ln, "%x", &line);
				if (parse_bitmap_line(bytes + i * wbytes,
				     bytes_r + i * wbytes, line, dwidth) != 0)
					return (1);
			}

			if (add_char(curchar, map_idx, bytes,
			    dwidth == width * 2 ? bytes_r : NULL) != 0)
				return (1);
		}
	}

	return (0);
}

static void
set_width(int w)
{

	if (w <= 0 || w > 128)
		errx(1, "invalid width %d", w);
	width = w;
	wbytes = howmany(width, 8);
}

static int
parse_hex(FILE *fp, unsigned int map_idx)
{
	char *ln, *p;
	char fmt_str[8];
	size_t length;
	uint8_t *bytes = NULL, *bytes_r = NULL;
	unsigned curchar = 0, i, line, chars_per_row, dwidth;
	int rv = 0;

	while ((ln = fgetln(fp, &length)) != NULL) {
		ln[length - 1] = '\0';

		if (strncmp(ln, "# Height: ", 10) == 0) {
			if (bytes != NULL)
				errx(1, "malformed input: Height tag after font data");
			height = atoi(ln + 10);
		} else if (strncmp(ln, "# Width: ", 9) == 0) {
			if (bytes != NULL)
				errx(1, "malformed input: Width tag after font data");
			set_width(atoi(ln + 9));
		} else if (sscanf(ln, "%6x:", &curchar)) {
			if (bytes == NULL) {
				bytes = xmalloc(wbytes * height);
				bytes_r = xmalloc(wbytes * height);
			}
			/* ln is guaranteed to have a colon here. */
			p = strchr(ln, ':') + 1;
			chars_per_row = strlen(p) / height;
			dwidth = width;
			if (chars_per_row / 2 > (width + 7) / 8)
				dwidth *= 2; /* Double-width character. */
			snprintf(fmt_str, sizeof(fmt_str), "%%%ux",
			    chars_per_row);

			for (i = 0; i < height; i++) {
				sscanf(p, fmt_str, &line);
				p += chars_per_row;
				if (parse_bitmap_line(bytes + i * wbytes,
				    bytes_r + i * wbytes, line, dwidth) != 0) {
					rv = 1;
					goto out;
				}
			}

			if (add_char(curchar, map_idx, bytes,
			    dwidth == width * 2 ? bytes_r : NULL) != 0) {
				rv = 1;
				goto out;
			}
		}
	}
out:
	free(bytes);
	free(bytes_r);
	return (rv);
}

static int
parse_file(const char *filename, unsigned int map_idx)
{
	FILE *fp;
	size_t len;
	int rv;

	fp = fopen(filename, "r");
	if (fp == NULL) {
		perror(filename);
		return (1);
	}
	len = strlen(filename);
	if (len > 4 && strcasecmp(filename + len - 4, ".hex") == 0)
		rv = parse_hex(fp, map_idx);
	else
		rv = parse_bdf(fp, map_idx);
	fclose(fp);
	return (rv);
}

static void
number_glyphs(void)
{
	struct glyph *gl;
	unsigned int i, idx = 0;

	for (i = 0; i < VFNT_MAPS; i++)
		TAILQ_FOREACH(gl, &glyphs[i], g_list)
			gl->g_index = idx++;
}

static int
write_glyphs(FILE *fp)
{
	struct glyph *gl;
	unsigned int i;

	for (i = 0; i < VFNT_MAPS; i++) {
		TAILQ_FOREACH(gl, &glyphs[i], g_list)
			if (fwrite(gl->g_data, wbytes * height, 1, fp) != 1)
				return (1);
	}
	return (0);
}

static void
fold_mappings(unsigned int map_idx)
{
	struct mapping_list *ml = &maps[map_idx];
	struct mapping *mn, *mp, *mbase;

	mp = mbase = TAILQ_FIRST(ml);
	for (mp = mbase = TAILQ_FIRST(ml); mp != NULL; mp = mn) {
		mn = TAILQ_NEXT(mp, m_list);
		if (mn != NULL && mn->m_char == mp->m_char + 1 &&
		    mn->m_glyph->g_index == mp->m_glyph->g_index + 1)
			continue;
		mbase->m_length = mp->m_char - mbase->m_char + 1;
		mbase = mp = mn;
		map_folded_count[map_idx]++;
	}
}

struct file_mapping {
	uint32_t	source;
	uint16_t	destination;
	uint16_t	length;
} __packed;

static int
write_mappings(FILE *fp, unsigned int map_idx)
{
	struct mapping_list *ml = &maps[map_idx];
	struct mapping *mp;
	struct file_mapping fm;
	unsigned int i = 0, j = 0;

	TAILQ_FOREACH(mp, ml, m_list) {
		j++;
		if (mp->m_length > 0) {
			i += mp->m_length;
			fm.source = htobe32(mp->m_char);
			fm.destination = htobe16(mp->m_glyph->g_index);
			fm.length = htobe16(mp->m_length - 1);
			if (fwrite(&fm, sizeof fm, 1, fp) != 1)
				return (1);
		}
	}
	assert(i == j);
	return (0);
}

struct file_header {
	uint8_t		magic[8];
	uint8_t		width;
	uint8_t		height;
	uint16_t	pad;
	uint32_t	glyph_count;
	uint32_t	map_count[4];
} __packed;

static int
write_fnt(const char *filename)
{
	FILE *fp;
	struct file_header fh = {
		.magic = "VFNT0002",
	};

	fp = fopen(filename, "wb");
	if (fp == NULL) {
		perror(filename);
		return (1);
	}

	fh.width = width;
	fh.height = height;
	fh.glyph_count = htobe32(glyph_unique);
	fh.map_count[0] = htobe32(map_folded_count[0]);
	fh.map_count[1] = htobe32(map_folded_count[1]);
	fh.map_count[2] = htobe32(map_folded_count[2]);
	fh.map_count[3] = htobe32(map_folded_count[3]);
	if (fwrite(&fh, sizeof fh, 1, fp) != 1) {
		perror(filename);
		fclose(fp);
		return (1);
	}

	if (write_glyphs(fp) != 0 ||
	    write_mappings(fp, VFNT_MAP_NORMAL) != 0 ||
	    write_mappings(fp, 1) != 0 ||
	    write_mappings(fp, VFNT_MAP_BOLD) != 0 ||
	    write_mappings(fp, 3) != 0) {
		perror(filename);
		fclose(fp);
		return (1);
	}

	fclose(fp);
	return (0);
}

static void
print_font_info(void)
{
	printf(
"Statistics:\n"
"- glyph_total:                 %6u\n"
"- glyph_normal:                %6u\n"
"- glyph_normal_right:          %6u\n"
"- glyph_bold:                  %6u\n"
"- glyph_bold_right:            %6u\n"
"- glyph_unique:                %6u\n"
"- glyph_dupe:                  %6u\n"
"- mapping_total:               %6u\n"
"- mapping_normal:              %6u\n"
"- mapping_normal_folded:       %6u\n"
"- mapping_normal_right:        %6u\n"
"- mapping_normal_right_folded: %6u\n"
"- mapping_bold:                %6u\n"
"- mapping_bold_folded:         %6u\n"
"- mapping_bold_right:          %6u\n"
"- mapping_bold_right_folded:   %6u\n"
"- mapping_unique:              %6u\n"
"- mapping_dupe:                %6u\n",
	    glyph_total,
	    glyph_count[0],
	    glyph_count[1],
	    glyph_count[2],
	    glyph_count[3],
	    glyph_unique, glyph_dupe,
	    mapping_total,
	    map_count[0], map_folded_count[0],
	    map_count[1], map_folded_count[1],
	    map_count[2], map_folded_count[2],
	    map_count[3], map_folded_count[3],
	    mapping_unique, mapping_dupe);
}

int
main(int argc, char *argv[])
{
	int ch, val, verbose = 0;

	assert(sizeof(struct file_header) == 32);
	assert(sizeof(struct file_mapping) == 8);

	while ((ch = getopt(argc, argv, "h:vw:")) != -1) {
		switch (ch) {
		case 'h':
			val = atoi(optarg);
			if (val <= 0 || val > 128)
				errx(1, "Invalid height %d", val);
			height = val;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			set_width(atoi(optarg));
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 2 || argc > 3)
		usage();

	wbytes = howmany(width, 8);

	if (parse_file(argv[0], VFNT_MAP_NORMAL) != 0)
		return (1);
	argc--;
	argv++;
	if (argc == 2) {
		if (parse_file(argv[0], VFNT_MAP_BOLD) != 0)
			return (1);
		argc--;
		argv++;
	}
	number_glyphs();
	dedup_mapping(VFNT_MAP_BOLD);
	dedup_mapping(VFNT_MAP_BOLD_RH);
	fold_mappings(0);
	fold_mappings(1);
	fold_mappings(2);
	fold_mappings(3);
	if (write_fnt(argv[0]) != 0)
		return (1);

	if (verbose)
		print_font_info();

	return (0);
}
