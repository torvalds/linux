/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2004 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by NAI Labs, the
 * Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
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

#include <sys/types.h>
#include <sys/mac.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fts.h>
#include <libgen.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct label_spec {
	struct label_spec_entry {
		regex_t regex;	/* compiled regular expression to match */
		char *regexstr;	/* uncompiled regular expression */
		mode_t mode;	/* mode to possibly match */
		const char *modestr;	/* print-worthy ",-?" mode string */
		char *mactext;	/* MAC label to apply */
		int flags;	/* miscellaneous flags */
#define		F_DONTLABEL	0x01
#define		F_ALWAYSMATCH	0x02
	} *entries,		/* entries[0..nentries] */
	  *match;		/* cached decision for MAC label to apply */
	size_t nentries;	/* size of entries list */
	STAILQ_ENTRY(label_spec) link;
};

struct label_specs {
	STAILQ_HEAD(label_specs_head, label_spec) head;
};

void usage(int) __dead2;
struct label_specs *new_specs(void);
void add_specs(struct label_specs *, const char *, int);
void add_setfmac_specs(struct label_specs *, char *);
void add_spec_line(const char *, int, struct label_spec_entry *, char *);
int apply_specs(struct label_specs *, FTSENT *, int, int);
int specs_empty(struct label_specs *);

static int qflag;

int
main(int argc, char **argv)
{
	FTSENT *ftsent;
	FTS *fts;
	struct label_specs *specs;
	int eflag = 0, xflag = 0, vflag = 0, Rflag = 0, hflag;
	int ch, is_setfmac;
	char *bn;

	bn = basename(argv[0]);
	if (bn == NULL)
		err(1, "basename");
	is_setfmac = strcmp(bn, "setfmac") == 0;
	hflag = is_setfmac ? FTS_LOGICAL : FTS_PHYSICAL;
	specs = new_specs();
	while ((ch = getopt(argc, argv, is_setfmac ? "Rhq" : "ef:qs:vx")) !=
	    -1) {
		switch (ch) {
		case 'R':
			Rflag = 1;
			break;
		case 'e':
			eflag = 1;
			break;
		case 'f':
			add_specs(specs, optarg, 0);
			break;
		case 'h':
			hflag = FTS_PHYSICAL;
			break;
		case 'q':
			qflag = 1;
			break;
		case 's':
			add_specs(specs, optarg, 1);
			break;
		case 'v':
			vflag++;
			break;
		case 'x':
			xflag = FTS_XDEV;
			break;
		default:
			usage(is_setfmac);
		}
	}
	argc -= optind;
	argv += optind;

	if (is_setfmac) {
		if (argc <= 1)	
			usage(is_setfmac);
		add_setfmac_specs(specs, *argv);
		argc--;
		argv++;
	} else {
		if (argc == 0 || specs_empty(specs))
			usage(is_setfmac);
	}
	fts = fts_open(argv, hflag | xflag, NULL);
	if (fts == NULL)
		err(1, "cannot traverse filesystem%s", argc ? "s" : "");
	while ((ftsent = fts_read(fts)) != NULL) {
		switch (ftsent->fts_info) {
		case FTS_DP:		/* skip post-order */
			break;
		case FTS_D:		/* do pre-order */
		case FTS_DC:		/* do cyclic? */
			/* don't ever recurse directories as setfmac(8) */
			if (is_setfmac && !Rflag)
				fts_set(fts, ftsent, FTS_SKIP);
		case FTS_DEFAULT:	/* do default */
		case FTS_F:		/* do regular */
		case FTS_SL:		/* do symlink */
		case FTS_SLNONE:	/* do symlink */
		case FTS_W:		/* do whiteout */
			if (apply_specs(specs, ftsent, hflag, vflag)) {
				if (eflag) {
					errx(1, "labeling not supported in %s",
					    ftsent->fts_path);
				}
				if (!qflag)
					warnx("labeling not supported in %s",
					    ftsent->fts_path);
				fts_set(fts, ftsent, FTS_SKIP);
			}
			break;
		case FTS_DNR:		/* die on all errors */
		case FTS_ERR:
		case FTS_NS:
			err(1, "traversing %s", ftsent->fts_path);
		default:
			errx(1, "CANNOT HAPPEN (%d) traversing %s",
			    ftsent->fts_info, ftsent->fts_path);
		}
	}
	fts_close(fts);
	exit(0);
}

void
usage(int is_setfmac)
{

	if (is_setfmac)
		fprintf(stderr, "usage: setfmac [-Rhq] label file ...\n");
	else
		fprintf(stderr, "usage: setfsmac [-ehqvx] [-f specfile [...]] [-s specfile [...]] file ...\n");
	exit(1);
}

static int
chomp_line(char **line, size_t *linesize)
{
	char *s;
	int freeme = 0;
	
	for (s = *line; (unsigned)(s - *line) < *linesize; s++) {
		if (!isspace(*s))
			break;
	}
	if (*s == '#') {
		**line = '\0';
		*linesize = 0;
		return (freeme);
	}
	memmove(*line, s, *linesize - (s - *line));
	*linesize -= s - *line;
	for (s = &(*line)[*linesize - 1]; s >= *line; s--) {
		if (!isspace(*s))
			break;
	}
	if (s != &(*line)[*linesize - 1]) {
		*linesize = s - *line + 1;
	} else {
		s = malloc(*linesize + 1);
		if (s == NULL)
			err(1, "malloc");
		strncpy(s, *line, *linesize);
		*line = s;
		freeme = 1;
	}
	(*line)[*linesize] = '\0';
	return (freeme);
}

void
add_specs(struct label_specs *specs, const char *file, int is_sebsd)
{
	struct label_spec *spec;
	FILE *fp;
	char *line;
	size_t nlines = 0, linesize;
	int freeline;

	spec = malloc(sizeof(*spec));
	if (spec == NULL)
		err(1, "malloc");
	fp = fopen(file, "r");
	if (fp == NULL)
		err(1, "opening %s", file);
	while ((line = fgetln(fp, &linesize)) != NULL) {
		freeline = chomp_line(&line, &linesize);
		if (linesize > 0) /* only allocate space for non-comments */
			nlines++;
		if (freeline)
			free(line);
	}
	if (ferror(fp))
		err(1, "fgetln on %s", file);
	rewind(fp);
	spec->entries = calloc(nlines, sizeof(*spec->entries));
	if (spec->entries == NULL)
		err(1, "malloc");
	spec->nentries = nlines;
	while (nlines > 0) {
		line = fgetln(fp, &linesize);
		if (line == NULL) {
			if (feof(fp))
				errx(1, "%s ended prematurely", file);
			else
				err(1, "failure reading %s", file);
		}
		freeline = chomp_line(&line, &linesize);
		if (linesize == 0) {
			if (freeline)
				free(line);
			continue;
		}
		add_spec_line(file, is_sebsd, &spec->entries[--nlines], line);
		if (freeline)
			free(line);
	}
	fclose(fp);
	if (!qflag)
		warnx("%s: read %lu specifications", file,
		    (long)spec->nentries);
	STAILQ_INSERT_TAIL(&specs->head, spec, link);
}

void
add_setfmac_specs(struct label_specs *specs, char *label)
{
	struct label_spec *spec;

	spec = malloc(sizeof(*spec));
	if (spec == NULL)
		err(1, "malloc");
	spec->nentries = 1;
	spec->entries = calloc(spec->nentries, sizeof(*spec->entries));
	if (spec->entries == NULL)
		err(1, "malloc");
	/* The _only_ thing specified here is the mactext! */
	spec->entries->mactext = label;
	spec->entries->flags |= F_ALWAYSMATCH;
	STAILQ_INSERT_TAIL(&specs->head, spec, link);
}

void
add_spec_line(const char *file, int is_sebsd, struct label_spec_entry *entry,
    char *line)
{
	char *regexstr, *modestr, *macstr, *regerrorstr;
	size_t size;
	int error;

	regexstr = strtok(line, " \t");
	if (regexstr == NULL)
		errx(1, "%s: need regular expression", file);
	modestr = strtok(NULL, " \t");
	if (modestr == NULL)
		errx(1, "%s: need a label", file);
	macstr = strtok(NULL, " \t");
	if (macstr == NULL) {	/* the mode is just optional */
		macstr = modestr;
		modestr = NULL;
	}
	if (strtok(NULL, " \t") != NULL)
		errx(1, "%s: extraneous fields at end of line", file);
	/* assume we need to anchor this regex */
	if (asprintf(&regexstr, "^%s$", regexstr) == -1)
		err(1, "%s: processing regular expression", file);
	entry->regexstr = regexstr;
	error = regcomp(&entry->regex, regexstr, REG_EXTENDED | REG_NOSUB);
	if (error) {
		size = regerror(error, &entry->regex, NULL, 0);
		regerrorstr = malloc(size);
		if (regerrorstr == NULL)
			err(1, "malloc");
		(void)regerror(error, &entry->regex, regerrorstr, size);
		errx(1, "%s: %s: %s", file, entry->regexstr, regerrorstr);
	}
	if (!is_sebsd) {
		entry->mactext = strdup(macstr);
		if (entry->mactext == NULL)
			err(1, "strdup");
	} else {
		if (asprintf(&entry->mactext, "sebsd/%s", macstr) == -1)
			err(1, "asprintf");
		if (strcmp(macstr, "<<none>>") == 0)
			entry->flags |= F_DONTLABEL;
	}
	if (modestr != NULL) {
		if (strlen(modestr) != 2 || modestr[0] != '-')
			errx(1, "%s: invalid mode string: %s", file, modestr);
		switch (modestr[1]) {
		case 'b':
			entry->mode = S_IFBLK;
			entry->modestr = ",-b";
			break;
		case 'c':
			entry->mode = S_IFCHR;
			entry->modestr = ",-c";
			break;
		case 'd':
			entry->mode = S_IFDIR;
			entry->modestr = ",-d";
			break;
		case 'p':
			entry->mode = S_IFIFO;
			entry->modestr = ",-p";
			break;
		case 'l':
			entry->mode = S_IFLNK;
			entry->modestr = ",-l";
			break;
		case 's':
			entry->mode = S_IFSOCK;
			entry->modestr = ",-s";
			break;
		case '-':
			entry->mode = S_IFREG;
			entry->modestr = ",--";
			break;
		default:
			errx(1, "%s: invalid mode string: %s", file, modestr);
		}
	} else {
		entry->modestr = "";
	}
}

int
specs_empty(struct label_specs *specs)
{

	return (STAILQ_EMPTY(&specs->head));
}

int
apply_specs(struct label_specs *specs, FTSENT *ftsent, int hflag, int vflag)
{
	regmatch_t pmatch;
	struct label_spec *ls;
	struct label_spec_entry *ent;
	char *regerrorstr, *macstr;
	size_t size;
	mac_t mac;
	int error, matchedby;

	/*
	 * Work through file context sources in order of specification
	 * on the command line, and through their entries in reverse
	 * order to find the "last" (hopefully "best") match.
	 */
	matchedby = 0;
	STAILQ_FOREACH(ls, &specs->head, link) {
		for (ls->match = NULL, ent = ls->entries;
		    ent < &ls->entries[ls->nentries]; ent++) {
			if (ent->flags & F_ALWAYSMATCH)
				goto matched;
			if (ent->mode != 0 &&
			    (ftsent->fts_statp->st_mode & S_IFMT) != ent->mode)
				continue;
			pmatch.rm_so = 0;
			pmatch.rm_eo = ftsent->fts_pathlen;
			error = regexec(&ent->regex, ftsent->fts_path, 1,
			    &pmatch, REG_STARTEND);
			switch (error) {
			case REG_NOMATCH:
				continue;
			case 0:
				break;
			default:
				size = regerror(error, &ent->regex, NULL, 0);
				regerrorstr = malloc(size);
				if (regerrorstr == NULL)
					err(1, "malloc");
				(void)regerror(error, &ent->regex, regerrorstr,
				    size);
				errx(1, "%s: %s", ent->regexstr, regerrorstr);
			}
		matched:
			ls->match = ent;
			if (vflag) {
				if (matchedby == 0) {
					printf("%s matched by ",
					    ftsent->fts_path);
					matchedby = 1;
				}
				printf("%s(%s%s,%s)", matchedby == 2 ? "," : "",
				    ent->regexstr, ent->modestr, ent->mactext);
				if (matchedby == 1)
					matchedby = 2;
			}
			break;
		}
	}
	if (vflag && matchedby)
		printf("\n");
	size = 0;
	STAILQ_FOREACH(ls, &specs->head, link) {
		/* cached match decision */
		if (ls->match && (ls->match->flags & F_DONTLABEL) == 0)
			 /* add length of "x\0"/"y," */
			size += strlen(ls->match->mactext) + 1;
	}
	if (size == 0)
		return (0);
	macstr = malloc(size);
	if (macstr == NULL)
		err(1, "malloc");
	*macstr = '\0';
	STAILQ_FOREACH(ls, &specs->head, link) {
		/* cached match decision */
		if (ls->match && (ls->match->flags & F_DONTLABEL) == 0) {
			if (*macstr != '\0')
				strcat(macstr, ",");
			strcat(macstr, ls->match->mactext);
		}
	}
	if (mac_from_text(&mac, macstr))
		err(1, "mac_from_text(%s)", macstr);
	if ((hflag == FTS_PHYSICAL ? mac_set_link(ftsent->fts_accpath, mac) :
	    mac_set_file(ftsent->fts_accpath, mac)) != 0) {
		if (errno == EOPNOTSUPP) {
			mac_free(mac);
			free(macstr);
			return (1);
		}
		err(1, "mac_set_link(%s, %s)", ftsent->fts_path, macstr);
	}
	mac_free(mac);
	free(macstr);
	return (0);
}

struct label_specs *
new_specs(void)
{
	struct label_specs *specs;

	specs = malloc(sizeof(*specs));
	if (specs == NULL)
		err(1, "malloc");
	STAILQ_INIT(&specs->head);
	return (specs);
}
