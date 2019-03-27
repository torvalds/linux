/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000, Boris Popov
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/exec.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/reboot.h>
#include <sys/linker.h>
#include <sys/stat.h>
#include <sys/module.h>
#define FREEBSD_ELF

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fts.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <machine/elf.h>

#include "ef.h"

#define	MAXRECSIZE	(64 << 10)	/* 64k */
#define check(val)	if ((error = (val)) != 0) break

static bool dflag;	/* do not create a hint file, only write on stdout */
static int verbose;

static FILE *fxref;	/* current hints file */

static const char *xref_file = "linker.hints";

/*
 * A record is stored in the static buffer recbuf before going to disk.
 */
static char recbuf[MAXRECSIZE];
static int recpos;	/* current write position */
static int reccnt;	/* total record written to this file so far */

static void
intalign(void)
{

	recpos = roundup2(recpos, sizeof(int));
}

static void
record_start(void)
{

	recpos = 0;
	memset(recbuf, 0, MAXRECSIZE);
}

static int
record_end(void)
{

	if (recpos == 0)
		return (0);
	reccnt++;
	intalign();
	fwrite(&recpos, sizeof(recpos), 1, fxref);
	return (fwrite(recbuf, recpos, 1, fxref) != 1 ? errno : 0);
}

static int
record_buf(const void *buf, size_t size)
{

	if (MAXRECSIZE - recpos < size)
		errx(1, "record buffer overflow");
	memcpy(recbuf + recpos, buf, size);
	recpos += size;
	return (0);
}

/*
 * An int is stored in host order and aligned
 */
static int
record_int(int val)
{

	intalign();
	return (record_buf(&val, sizeof(val)));
}

/*
 * A string is stored as 1-byte length plus data, no padding
 */
static int
record_string(const char *str)
{
	int error;
	size_t len;
	u_char val;
	
	if (dflag)
		return (0);
	val = len = strlen(str);
	if (len > 255)
		errx(1, "string %s too long", str);
	error = record_buf(&val, sizeof(val));
	if (error != 0)
		return (error);
	return (record_buf(str, len));
}

/* From sys/isa/pnp.c */
static char *
pnp_eisaformat(uint32_t id)
{
	uint8_t *data;
	static char idbuf[8];
	const char  hextoascii[] = "0123456789abcdef";

	id = htole32(id);
	data = (uint8_t *)&id;
	idbuf[0] = '@' + ((data[0] & 0x7c) >> 2);
	idbuf[1] = '@' + (((data[0] & 0x3) << 3) + ((data[1] & 0xe0) >> 5));
	idbuf[2] = '@' + (data[1] & 0x1f);
	idbuf[3] = hextoascii[(data[2] >> 4)];
	idbuf[4] = hextoascii[(data[2] & 0xf)];
	idbuf[5] = hextoascii[(data[3] >> 4)];
	idbuf[6] = hextoascii[(data[3] & 0xf)];
	idbuf[7] = 0;
	return (idbuf);
}

struct pnp_elt
{
	int	pe_kind;	/* What kind of entry */
#define TYPE_SZ_MASK	0x0f
#define TYPE_FLAGGED	0x10	/* all f's is a wildcard */
#define	TYPE_INT	0x20	/* Is a number */
#define TYPE_PAIRED	0x40
#define TYPE_LE		0x80	/* Matches <= this value */
#define TYPE_GE		0x100	/* Matches >= this value */
#define TYPE_MASK	0x200	/* Specifies a mask to follow */
#define TYPE_U8		(1 | TYPE_INT)
#define TYPE_V8		(1 | TYPE_INT | TYPE_FLAGGED)
#define TYPE_G16	(2 | TYPE_INT | TYPE_GE)
#define TYPE_L16	(2 | TYPE_INT | TYPE_LE)
#define TYPE_M16	(2 | TYPE_INT | TYPE_MASK)
#define TYPE_U16	(2 | TYPE_INT)
#define TYPE_V16	(2 | TYPE_INT | TYPE_FLAGGED)
#define TYPE_U32	(4 | TYPE_INT)
#define TYPE_V32	(4 | TYPE_INT | TYPE_FLAGGED)
#define TYPE_W32	(4 | TYPE_INT | TYPE_PAIRED)
#define TYPE_D		7
#define TYPE_Z		8
#define TYPE_P		9
#define TYPE_E		10
#define TYPE_T		11
	int	pe_offset;	/* Offset within the element */
	char *	pe_key;		/* pnp key name */
	TAILQ_ENTRY(pnp_elt) next; /* Link */
};
typedef TAILQ_HEAD(pnp_head, pnp_elt) pnp_list;

/*
 * this function finds the data from the pnp table, as described by the
 * the description and creates a new output (new_desc). This output table
 * is a form that's easier for the agent that's automatically loading the
 * modules.
 *
 * The format output is the simplified string from this routine in the
 * same basic format as the pnp string, as documented in sys/module.h.
 * First a string describing the format is output, the a count of the
 * number of records, then each record. The format string also describes
 * the length of each entry (though it isn't a fixed length when strings
 * are present).
 *
 *	type	Output		Meaning
 *	I	uint32_t	Integer equality comparison
 *	J	uint32_t	Pair of uint16_t fields converted to native
 *				byte order. The two fields both must match.
 *	G	uint32_t	Greater than or equal to
 *	L	uint32_t	Less than or equal to
 *	M	uint32_t	Mask of which fields to test. Fields that
 *				take up space increment the count. This
 *				field must be first, and resets the count.
 *	D	string		Description of the device this pnp info is for
 *	Z	string		pnp string must match this
 *	T	nothing		T fields set pnp values that must be true for
 *				the entire table.
 * Values are packed the same way that other values are packed in this file.
 * Strings and int32_t's start on a 32-bit boundary and are padded with 0
 * bytes. Objects that are smaller than uint32_t are converted, without
 * sign extension to uint32_t to simplify parsing downstream.
 */
static int
parse_pnp_list(const char *desc, char **new_desc, pnp_list *list)
{
	const char *walker, *ep;
	const char *colon, *semi;
	struct pnp_elt *elt;
	char *nd;
	char type[8], key[32];
	int off;

	walker = desc;
	ep = desc + strlen(desc);
	off = 0;
	nd = *new_desc = malloc(strlen(desc) + 1);
	if (verbose > 1)
		printf("Converting %s into a list\n", desc);
	while (walker < ep) {
		colon = strchr(walker, ':');
		semi = strchr(walker, ';');
		if (semi != NULL && semi < colon)
			goto err;
		if (colon - walker > sizeof(type))
			goto err;
		strncpy(type, walker, colon - walker);
		type[colon - walker] = '\0';
		if (semi != NULL) {
			if (semi - colon >= sizeof(key))
				goto err;
			strncpy(key, colon + 1, semi - colon - 1);
			key[semi - colon - 1] = '\0';
			walker = semi + 1;
			/* Fail safe if we have spaces after ; */
			while (walker < ep && isspace(*walker))
				walker++;
		} else {
			if (strlen(colon + 1) >= sizeof(key))
				goto err;
			strcpy(key, colon + 1);
			walker = ep;
		}
		if (verbose > 1)
			printf("Found type %s for name %s\n", type, key);
		/* Skip pointer place holders */
		if (strcmp(type, "P") == 0) {
			off += sizeof(void *);
			continue;
		}

		/*
		 * Add a node of the appropriate type
		 */
		elt = malloc(sizeof(struct pnp_elt) + strlen(key) + 1);
		TAILQ_INSERT_TAIL(list, elt, next);
		elt->pe_key = (char *)(elt + 1);
		elt->pe_offset = off;
		if (strcmp(type, "U8") == 0)
			elt->pe_kind = TYPE_U8;
		else if (strcmp(type, "V8") == 0)
			elt->pe_kind = TYPE_V8;
		else if (strcmp(type, "G16") == 0)
			elt->pe_kind = TYPE_G16;
		else if (strcmp(type, "L16") == 0)
			elt->pe_kind = TYPE_L16;
		else if (strcmp(type, "M16") == 0)
			elt->pe_kind = TYPE_M16;
		else if (strcmp(type, "U16") == 0)
			elt->pe_kind = TYPE_U16;
		else if (strcmp(type, "V16") == 0)
			elt->pe_kind = TYPE_V16;
		else if (strcmp(type, "U32") == 0)
			elt->pe_kind = TYPE_U32;
		else if (strcmp(type, "V32") == 0)
			elt->pe_kind = TYPE_V32;
		else if (strcmp(type, "W32") == 0)
			elt->pe_kind = TYPE_W32;
		else if (strcmp(type, "D") == 0)	/* description char * */
			elt->pe_kind = TYPE_D;
		else if (strcmp(type, "Z") == 0)	/* char * to match */
			elt->pe_kind = TYPE_Z;
		else if (strcmp(type, "P") == 0)	/* Pointer -- ignored */
			elt->pe_kind = TYPE_P;
		else if (strcmp(type, "E") == 0)	/* EISA PNP ID, as uint32_t */
			elt->pe_kind = TYPE_E;
		else if (strcmp(type, "T") == 0)
			elt->pe_kind = TYPE_T;
		else
			goto err;
		/*
		 * Maybe the rounding here needs to be more nuanced and/or somehow
		 * architecture specific. Fortunately, most tables in the system
		 * have sane ordering of types.
		 */
		if (elt->pe_kind & TYPE_INT) {
			elt->pe_offset = roundup2(elt->pe_offset, elt->pe_kind & TYPE_SZ_MASK);
			off = elt->pe_offset + (elt->pe_kind & TYPE_SZ_MASK);
		} else if (elt->pe_kind == TYPE_E) {
			/* Type E stored as Int, displays as string */
			elt->pe_offset = roundup2(elt->pe_offset, sizeof(uint32_t));
			off = elt->pe_offset + sizeof(uint32_t);
		} else if (elt->pe_kind == TYPE_T) {
			/* doesn't actually consume space in the table */
			off = elt->pe_offset;
		} else {
			elt->pe_offset = roundup2(elt->pe_offset, sizeof(void *));
			off = elt->pe_offset + sizeof(void *);
		}
		if (elt->pe_kind & TYPE_PAIRED) {
			char *word, *ctx;

			for (word = strtok_r(key, "/", &ctx);
			     word; word = strtok_r(NULL, "/", &ctx)) {
				sprintf(nd, "%c:%s;", elt->pe_kind & TYPE_FLAGGED ? 'J' : 'I',
				    word);
				nd += strlen(nd);
			}
			
		}
		else {
			if (elt->pe_kind & TYPE_FLAGGED)
				*nd++ = 'J';
			else if (elt->pe_kind & TYPE_GE)
				*nd++ = 'G';
			else if (elt->pe_kind & TYPE_LE)
				*nd++ = 'L';
			else if (elt->pe_kind & TYPE_MASK)
				*nd++ = 'M';
			else if (elt->pe_kind & TYPE_INT)
				*nd++ = 'I';
			else if (elt->pe_kind == TYPE_D)
				*nd++ = 'D';
			else if (elt->pe_kind == TYPE_Z || elt->pe_kind == TYPE_E)
				*nd++ = 'Z';
			else if (elt->pe_kind == TYPE_T)
				*nd++ = 'T';
			else
				errx(1, "Impossible type %x\n", elt->pe_kind);
			*nd++ = ':';
			strcpy(nd, key);
			nd += strlen(nd);
			*nd++ = ';';
		}
	}
	*nd++ = '\0';
	return (0);
err:
	errx(1, "Parse error of description string %s", desc);
}

static int
parse_entry(struct mod_metadata *md, const char *cval,
    struct elf_file *ef, const char *kldname)
{
	struct mod_depend mdp;
	struct mod_version mdv;
	struct mod_pnp_match_info pnp;
	char descr[1024];
	Elf_Off data;
	int error, i;
	size_t len;
	char *walker;
	void *table;

	data = (Elf_Off)md->md_data;
	error = 0;
	record_start();
	switch (md->md_type) {
	case MDT_DEPEND:
		if (!dflag)
			break;
		check(EF_SEG_READ(ef, data, sizeof(mdp), &mdp));
		printf("  depends on %s.%d (%d,%d)\n", cval,
		    mdp.md_ver_preferred, mdp.md_ver_minimum, mdp.md_ver_maximum);
		break;
	case MDT_VERSION:
		check(EF_SEG_READ(ef, data, sizeof(mdv), &mdv));
		if (dflag) {
			printf("  interface %s.%d\n", cval, mdv.mv_version);
		} else {
			record_int(MDT_VERSION);
			record_string(cval);
			record_int(mdv.mv_version);
			record_string(kldname);
		}
		break;
	case MDT_MODULE:
		if (dflag) {
			printf("  module %s\n", cval);
		} else {
			record_int(MDT_MODULE);
			record_string(cval);
			record_string(kldname);
		}
		break;
	case MDT_PNP_INFO:
		check(EF_SEG_READ_REL(ef, data, sizeof(pnp), &pnp));
		check(EF_SEG_READ_STRING(ef, (Elf_Off)pnp.descr, sizeof(descr), descr));
		descr[sizeof(descr) - 1] = '\0';
		if (dflag) {
			printf("  pnp info for bus %s format %s %d entries of %d bytes\n",
			    cval, descr, pnp.num_entry, pnp.entry_len);
		} else {
			pnp_list list;
			struct pnp_elt *elt, *elt_tmp;
			char *new_descr;

			if (verbose > 1)
				printf("  pnp info for bus %s format %s %d entries of %d bytes\n",
				    cval, descr, pnp.num_entry, pnp.entry_len);
			/*
			 * Parse descr to weed out the chaff and to create a list
			 * of offsets to output.
			 */
			TAILQ_INIT(&list);
			parse_pnp_list(descr, &new_descr, &list);
			record_int(MDT_PNP_INFO);
			record_string(cval);
			record_string(new_descr);
			record_int(pnp.num_entry);
			len = pnp.num_entry * pnp.entry_len;
			walker = table = malloc(len);
			check(EF_SEG_READ_REL(ef, (Elf_Off)pnp.table, len, table));

			/*
			 * Walk the list and output things. We've collapsed all the
			 * variant forms of the table down to just ints and strings.
			 */
			for (i = 0; i < pnp.num_entry; i++) {
				TAILQ_FOREACH(elt, &list, next) {
					uint8_t v1;
					uint16_t v2;
					uint32_t v4;
					int	value;
					char buffer[1024];

					if (elt->pe_kind == TYPE_W32) {
						memcpy(&v4, walker + elt->pe_offset, sizeof(v4));
						value = v4 & 0xffff;
						record_int(value);
						if (verbose > 1)
							printf("W32:%#x", value);
						value = (v4 >> 16) & 0xffff;
						record_int(value);
						if (verbose > 1)
							printf(":%#x;", value);
					} else if (elt->pe_kind & TYPE_INT) {
						switch (elt->pe_kind & TYPE_SZ_MASK) {
						case 1:
							memcpy(&v1, walker + elt->pe_offset, sizeof(v1));
							if ((elt->pe_kind & TYPE_FLAGGED) && v1 == 0xff)
								value = -1;
							else
								value = v1;
							break;
						case 2:
							memcpy(&v2, walker + elt->pe_offset, sizeof(v2));
							if ((elt->pe_kind & TYPE_FLAGGED) && v2 == 0xffff)
								value = -1;
							else
								value = v2;
							break;
						case 4:
							memcpy(&v4, walker + elt->pe_offset, sizeof(v4));
							if ((elt->pe_kind & TYPE_FLAGGED) && v4 == 0xffffffff)
								value = -1;
							else
								value = v4;
							break;
						default:
							errx(1, "Invalid size somehow %#x", elt->pe_kind);
						}
						if (verbose > 1)
							printf("I:%#x;", value);
						record_int(value);
					} else if (elt->pe_kind == TYPE_T) {
						/* Do nothing */
					} else { /* E, Z or D -- P already filtered */
						if (elt->pe_kind == TYPE_E) {
							memcpy(&v4, walker + elt->pe_offset, sizeof(v4));
							strcpy(buffer, pnp_eisaformat(v4));
						} else {
							char *ptr;

							ptr = *(char **)(walker + elt->pe_offset);
							buffer[0] = '\0';
							if (ptr != NULL) {
								EF_SEG_READ_STRING(ef, (Elf_Off)ptr,
								    sizeof(buffer), buffer);
								buffer[sizeof(buffer) - 1] = '\0';
							}
						}
						if (verbose > 1)
							printf("%c:%s;", elt->pe_kind == TYPE_E ? 'E' : (elt->pe_kind == TYPE_Z ? 'Z' : 'D'), buffer);
						record_string(buffer);
					}
				}
				if (verbose > 1)
					printf("\n");
				walker += pnp.entry_len;
			}
			/* Now free it */
			TAILQ_FOREACH_SAFE(elt, &list, next, elt_tmp) {
				TAILQ_REMOVE(&list, elt, next);
				free(elt);
			}
			free(table);
		}
		break;
	default:
		warnx("unknown metadata record %d in file %s", md->md_type, kldname);
	}
	if (!error)
		record_end();
	return (error);
}

static int
read_kld(char *filename, char *kldname)
{
	struct mod_metadata md;
	struct elf_file ef;
	void **p, **orgp;
	int error, eftype;
	long start, finish, entries;
	char cval[MAXMODNAME + 1];

	if (verbose || dflag)
		printf("%s\n", filename);
	error = ef_open(filename, &ef, verbose);
	if (error != 0) {
		error = ef_obj_open(filename, &ef, verbose);
		if (error != 0) {
			if (verbose)
				warnc(error, "elf_open(%s)", filename);
			return (error);
		}
	}
	eftype = EF_GET_TYPE(&ef);
	if (eftype != EFT_KLD && eftype != EFT_KERNEL)  {
		EF_CLOSE(&ef);
		return (0);
	}
	do {
		check(EF_LOOKUP_SET(&ef, MDT_SETNAME, &start, &finish,
		    &entries));
		check(EF_SEG_READ_ENTRY_REL(&ef, start, sizeof(*p) * entries,
		    (void *)&p));
		orgp = p;
		while(entries--) {
			check(EF_SEG_READ_REL(&ef, (Elf_Off)*p, sizeof(md),
			    &md));
			p++;
			check(EF_SEG_READ_STRING(&ef, (Elf_Off)md.md_cval,
			    sizeof(cval), cval));
			parse_entry(&md, cval, &ef, kldname);
		}
		if (error != 0)
			warnc(error, "error while reading %s", filename);
		free(orgp);
	} while(0);
	EF_CLOSE(&ef);
	return (error);
}

/*
 * Create a temp file in directory root, make sure we don't
 * overflow the buffer for the destination name
 */
static FILE *
maketempfile(char *dest, const char *root)
{
	char *p;
	int n, fd;

	p = strrchr(root, '/');
	n = p != NULL ? p - root + 1 : 0;
	if (snprintf(dest, MAXPATHLEN, "%.*slhint.XXXXXX", n, root) >=
	    MAXPATHLEN) {
		errno = ENAMETOOLONG;
		return (NULL);
	}

	fd = mkstemp(dest);
	if (fd < 0)
		return (NULL);
	fchmod(fd, 0644);	/* nothing secret in the file */
	return (fdopen(fd, "w+"));
}

static char xrefname[MAXPATHLEN], tempname[MAXPATHLEN];

static void
usage(void)
{

	fprintf(stderr, "%s\n",
	    "usage: kldxref [-Rdv] [-f hintsfile] path ..."
	);
	exit(1);
}

static int
compare(const FTSENT *const *a, const FTSENT *const *b)
{

	if ((*a)->fts_info == FTS_D && (*b)->fts_info != FTS_D)
		return (1);
	if ((*a)->fts_info != FTS_D && (*b)->fts_info == FTS_D)
		return (-1);
	return (strcmp((*a)->fts_name, (*b)->fts_name));
}

int
main(int argc, char *argv[])
{
	FTS *ftsp;
	FTSENT *p;
	int opt, fts_options, ival;
	struct stat sb;

	fts_options = FTS_PHYSICAL;

	while ((opt = getopt(argc, argv, "Rdf:v")) != -1) {
		switch (opt) {
		case 'd':	/* no hint file, only print on stdout */
			dflag = true;
			break;
		case 'f':	/* use this name instead of linker.hints */
			xref_file = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'R':	/* recurse on directories */
			fts_options |= FTS_COMFOLLOW;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	if (argc - optind < 1)
		usage();
	argc -= optind;
	argv += optind;

	if (stat(argv[0], &sb) != 0)
		err(1, "%s", argv[0]);
	if ((sb.st_mode & S_IFDIR) == 0) {
		errno = ENOTDIR;
		err(1, "%s", argv[0]);
	}

	ftsp = fts_open(argv, fts_options, compare);
	if (ftsp == NULL)
		exit(1);

	for (;;) {
		p = fts_read(ftsp);
		if ((p == NULL || p->fts_info == FTS_D) && fxref) {
			/* close and rename the current hint file */
			fclose(fxref);
			fxref = NULL;
			if (reccnt != 0) {
				rename(tempname, xrefname);
			} else {
				/* didn't find any entry, ignore this file */
				unlink(tempname);
				unlink(xrefname);
			}
		}
		if (p == NULL)
			break;
		if (p->fts_info == FTS_D && !dflag) {
			/* visiting a new directory, create a new hint file */
			snprintf(xrefname, sizeof(xrefname), "%s/%s",
			    ftsp->fts_path, xref_file);
			fxref = maketempfile(tempname, ftsp->fts_path);
			if (fxref == NULL)
				err(1, "can't create %s", tempname);
			ival = 1;
			fwrite(&ival, sizeof(ival), 1, fxref);
			reccnt = 0;
		}
		/* skip non-files and separate debug files */
		if (p->fts_info != FTS_F)
			continue;
		if (p->fts_namelen >= 6 &&
		    strcmp(p->fts_name + p->fts_namelen - 6, ".debug") == 0)
			continue;
		if (p->fts_namelen >= 8 &&
		    strcmp(p->fts_name + p->fts_namelen - 8, ".symbols") == 0)
			continue;
		read_kld(p->fts_path, p->fts_name);
	}
	fts_close(ftsp);
	return (0);
}
