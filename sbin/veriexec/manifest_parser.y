%{
/*-
 * Copyright (c) 2004-2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/stat.h>
#include <stdio.h>
#include <ctype.h>
#include <err.h>
#include <sysexits.h>
#include <libsecureboot.h>

#include "veriexec.h"

int yylex(void);
void yyerror(const char *);

/* function prototypes */
static int convert(char *fp, unsigned int count, unsigned char *out);
static void do_ioctl(void);
static int get_fingerprint_type(const char *fp_type);

/* ioctl parameter struct */
#ifdef MAXLABELLEN
static struct verified_exec_label_params lparams;
static struct verified_exec_params *params = &lparams.params;
#else
static struct verified_exec_params oparams;
static struct verified_exec_params *params = &oparams;
#endif

#ifndef SHA256_DIGEST_LENGTH
# define SHA_DIGEST_LENGTH br_sha1_SIZE
# define SHA256_DIGEST_LENGTH br_sha256_SIZE
# define SHA384_DIGEST_LENGTH br_sha384_SIZE
#endif

static int fmode;
 
extern int lineno;
extern int dev_fd;

struct fingerprint_type {
	const char *fp_type;
	int fp_size;
};

/* static globals */
static const struct fingerprint_type fingerprint_table[] = {
	{ "sha1", SHA_DIGEST_LENGTH },
	{ "sha256", SHA256_DIGEST_LENGTH },
#if MAXFINGERPRINTLEN > 32
	{ "sha384", SHA384_DIGEST_LENGTH },
#endif
	{ NULL, 0 }
};

/*
 * Indicate to lexer our version.
 * A token #>NUMBER will be consumed (and discared)
 * by lexer if parser_version > NUMBER
 * Otherwise the rest of the line will be discared
 * as for a comment.
 */
int parser_version = 1;
 
%}

%union {
	char *string;
	int  intval;
}

%token EOL
%token <string> EQ
%token <string> PATH
%token <string> STRING

%%

statement: /* empty */
	| statement path attributes eol
	| statement error eol {
		yyclearin; /* discard lookahead */
		yyerrok;   /* no more error */
		fprintf(stderr,
		    "skipping to next fingerprint\n");
	}
	;

attributes: /* empty */
	| attributes flag
	| attributes attr
	;

attr: STRING EQ STRING
{
	int fptype;

	fptype = get_fingerprint_type($1);

	/*
	 * There's only one attribute we care about
	 */
	if (fingerprint_table[fptype].fp_size) {
		strlcpy(params->fp_type, $1, sizeof(params->fp_type));
		if (convert($3, fingerprint_table[fptype].fp_size,
			params->fingerprint) < 0) {
			yyerror("bad fingerprint");
			YYERROR;
		}
	} else if (strcmp($1, "label") == 0) {
		static int warned_labels = 0;

#ifdef VERIEXEC_LABEL
		strlcpy(lparams.label, $3, sizeof(lparams.label));
		VERBOSE(3, ("version=%d label=%s\n", VeriexecVersion,
			lparams.label));
		if (VeriexecVersion > 1) {
			params->flags |= VERIEXEC_LABEL;
		} else
#endif
		if (!warned_labels) {
			warnx("ignoring labels");
			warned_labels = 1;
		}
	} else if (strcmp($1, "mode") == 0) {
		fmode = (int)strtol($3, NULL, 8);
	}
};

flag: STRING
{
	/*
	 * indirect only matters if the interpreter itself is not
	 * executable.
	 */
	if (!strcmp($1, "indirect")) {
		params->flags |= VERIEXEC_INDIRECT;
	} else if (!strcmp($1, "no_ptrace")) {
		params->flags |= VERIEXEC_NOTRACE;
	} else if (!strcmp($1, "trusted")) {
		params->flags |= VERIEXEC_TRUSTED;
	} else if (!strcmp($1, "no_fips")) {
#ifdef VERIEXEC_NOFIPS
		params->flags |= VERIEXEC_NOFIPS;
#endif
	}
}
;

path: PATH 
{
	if (strlen($1) >= MAXPATHLEN) {
		yyerror("Path >= MAXPATHLEN");
		YYERROR;
	}
	/*
	 * The majority of files in the manifest are relative
	 * to the package mount point, but we want absolute paths.
	 * Prepending '/' is actually all we need.
	 */
	if (snprintf(params->file, sizeof(params->file), "%s%s%s",
		Cdir ? Cdir : "",
		($1[0] == '/') ? "" : "/",
		$1) >= (int)sizeof(params->file)) {
		errx(EX_DATAERR, "cannot form pathname");
	}
	params->flags = 0;
	fmode = -1;			/* unknown */
};

eol: EOL
{
	if (!YYRECOVERING()) { /* Don't do the ioctl if we saw an error */
		do_ioctl();
	}
	params->fp_type[0] = '\0';	/* invalidate it */
};

%%

void
manifest_parser_init(void)
{
	params->fp_type[0] = '\0';      /* invalidate it */
}

int
get_fingerprint_type(const char *fp_type)
{
	int i;

	for (i = 0; fingerprint_table[i].fp_type; i++)
		if (!strcmp(fp_type, fingerprint_table[i].fp_type))
			break;

	return (i);
}

/*
 * Convert: takes the hexadecimal string pointed to by fp and converts
 * it to a "count" byte binary number which is stored in the array pointed to
 * by out.  Returns -1 if the conversion fails.
 */
static int
convert(char *fp, unsigned int count, unsigned char *out)
{
        unsigned int i;
        int value;
        
        for (i = 0; i < count; i++) {
		value = 0;
		if (isdigit(fp[i * 2]))
			value += fp[i * 2] - '0';
		else if (isxdigit(fp[i * 2]))
			value += 10 + tolower(fp[i * 2]) - 'a';
		else
			return (-1);
		value <<= 4;
		if (isdigit(fp[i * 2 + 1]))
			value += fp[i * 2 + 1] - '0';
		else if (isxdigit(fp[i * 2 + 1]))
			value += 10 + tolower(fp[i * 2 + 1]) - 'a';
		else
			return (-1);
		out[i] = value;
	}

	return (i);
}

/*
 * Perform the load of the fingerprint.  Assumes that the fingerprint
 * pseudo-device is opened and the file handle is in fd.
 */
static void
do_ioctl(void)
{
	struct stat st;

	if (params->fp_type[0] == '\0') {
		VERBOSE(1,("skipping %s\n", params->file));
		return;
	}

	/*
	 * See if the path is executable, if not put it on the FILE list.
	 */
	if (fmode > 0) {
		if (!(fmode & (S_IXUSR|S_IXGRP|S_IXOTH))) {
			params->flags |= VERIEXEC_FILE;
		}
	} else if (stat(params->file, &st) == 0) {
		if (!(st.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH))) {
			params->flags |= VERIEXEC_FILE;
		}
	}
	/*
	 * We may be forcing some flags...
	 */
	params->flags |= ForceFlags;
	VERBOSE(1, ("loading %s for %s %s flags=%#x\n",
		params->fp_type,
		(params->flags == VERIEXEC_FILE) ? "file" : "executable",
		params->file, params->flags));

#ifdef VERIEXEC_LABEL
	if (params->flags & VERIEXEC_LABEL) {
		if (ioctl(dev_fd, VERIEXEC_LABEL_LOAD, &lparams) < 0)
			warn("cannot update veriexec label for %s",
			    params->file);
	} else
#endif
	if (ioctl(dev_fd, VERIEXEC_SIGNED_LOAD, params) < 0)
		warn("cannot update veriexec for %s", params->file);
	params->fp_type[0] = '\0';
}
