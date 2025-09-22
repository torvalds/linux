/*	$OpenBSD: fstab.c,v 1.22 2016/03/17 23:48:42 mmcc Exp $ */
/*
 * Copyright (c) 1980, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <fstab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *_fs_fp;
static struct fstab _fs_fstab;

static int fstabscan(void);

static int
fstabscan(void)
{
	char *cp;
#define	MAXLINELENGTH	1024
	static char line[MAXLINELENGTH];
	char subline[MAXLINELENGTH];
	const char *errstr;
	char *last;
	int typexx;

	for (;;) {
		if (!(cp = fgets(line, sizeof(line), _fs_fp)))
			return(0);
/* OLD_STYLE_FSTAB */
		if (!strpbrk(cp, " \t")) {
			_fs_fstab.fs_spec = strtok_r(cp, ":\n", &last);
			if (!_fs_fstab.fs_spec || *_fs_fstab.fs_spec == '#')
				continue;
			_fs_fstab.fs_file = strtok_r(NULL, ":\n", &last);
			_fs_fstab.fs_type = strtok_r(NULL, ":\n", &last);
			if (_fs_fstab.fs_type) {
				if (!strcmp(_fs_fstab.fs_type, FSTAB_XX))
					continue;
				_fs_fstab.fs_mntops = _fs_fstab.fs_type;
				_fs_fstab.fs_vfstype =
				    strcmp(_fs_fstab.fs_type, FSTAB_SW) ?
				    "ufs" : "swap";
				if ((cp = strtok_r(NULL, ":\n", &last))) {
					_fs_fstab.fs_freq = strtonum(cp, 0,
					    INT_MAX, &errstr);
					if (errstr)
						goto bad;
					if ((cp = strtok_r(NULL,
					    ":\n", &last))) {
						_fs_fstab.fs_passno =
						    strtonum(cp, 0, INT_MAX,
						    &errstr);
						if (errstr)
							goto bad;
						return(1);
					}
				}
			}
			goto bad;
		}
/* OLD_STYLE_FSTAB */
		_fs_fstab.fs_spec = strtok_r(cp, " \t\n", &last);
		if (!_fs_fstab.fs_spec || *_fs_fstab.fs_spec == '#')
			continue;
		_fs_fstab.fs_file = strtok_r(NULL, " \t\n", &last);
		_fs_fstab.fs_vfstype = strtok_r(NULL, " \t\n", &last);
		_fs_fstab.fs_mntops = strtok_r(NULL, " \t\n", &last);
		if (_fs_fstab.fs_mntops == NULL)
			goto bad;
		_fs_fstab.fs_freq = 0;
		_fs_fstab.fs_passno = 0;
		if ((cp = strtok_r(NULL, " \t\n", &last)) != NULL) {
			_fs_fstab.fs_freq = strtonum(cp, 0, INT_MAX, &errstr);
			if (errstr)
				goto bad;
			if ((cp = strtok_r(NULL, " \t\n", &last)) != NULL) {
				_fs_fstab.fs_passno = strtonum(cp, 0, INT_MAX,
				    &errstr);
				if (errstr)
					goto bad;
			}
		}
		strlcpy(subline, _fs_fstab.fs_mntops, sizeof subline);
		for (typexx = 0, cp = strtok_r(subline, ",", &last); cp;
		     cp = strtok_r(NULL, ",", &last)) {
			if (strlen(cp) != 2)
				continue;
			if (!strcmp(cp, FSTAB_RW)) {
				_fs_fstab.fs_type = FSTAB_RW;
				break;
			}
			if (!strcmp(cp, FSTAB_RQ)) {
				_fs_fstab.fs_type = FSTAB_RQ;
				break;
			}
			if (!strcmp(cp, FSTAB_RO)) {
				_fs_fstab.fs_type = FSTAB_RO;
				break;
			}
			if (!strcmp(cp, FSTAB_SW)) {
				_fs_fstab.fs_type = FSTAB_SW;
				break;
			}
			if (!strcmp(cp, FSTAB_XX)) {
				_fs_fstab.fs_type = FSTAB_XX;
				typexx++;
				break;
			}
		}
		if (typexx)
			continue;
		if (cp != NULL)
			return(1);

bad:		/* We silently ignore all bogus lines */
		;
	}
}

struct fstab *
getfsent(void)
{
	if ((!_fs_fp && !setfsent()) || !fstabscan())
		return(NULL);
	return(&_fs_fstab);
}

struct fstab *
getfsspec(const char *name)
{
	if (setfsent())
		while (fstabscan())
			if (!strcmp(_fs_fstab.fs_spec, name))
				return(&_fs_fstab);
	return(NULL);
}

struct fstab *
getfsfile(const char *name)
{
	if (setfsent())
		while (fstabscan())
			if (!strcmp(_fs_fstab.fs_file, name))
				return(&_fs_fstab);
	return(NULL);
}

int
setfsent(void)
{
	struct stat sbuf;

	if (_fs_fp) {
		rewind(_fs_fp);
		return(1);
	}

	if (stat(_PATH_FSTAB, &sbuf) != 0)
		goto fail;
	if ((sbuf.st_size == 0) || ((sbuf.st_mode & S_IFMT) != S_IFREG)) {
		errno = EFTYPE;
		goto fail;
	}

	if ((_fs_fp = fopen(_PATH_FSTAB, "re")))
		return(1);

fail:
	return(0);
}
DEF_WEAK(setfsent);

void
endfsent(void)
{
	if (_fs_fp) {
		(void)fclose(_fs_fp);
		_fs_fp = NULL;
	}
}
