/*
 * Copyright (c) 1983, 1987, 1993
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

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/types.h>
#define DKTYPENAMES
#include <sys/disklabel.h>
#include <ufs/ffs/fs.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

static u_int	gettype(char *, const char * const *);

struct disklabel *
getdiskbyname(const char *name)
{
	static struct	disklabel disk;
	struct	disklabel *dp = &disk;
	struct partition *pp;
	char	*buf;
	char	*db_array[2] = { _PATH_DISKTAB, 0 };
	char	*cp, *cq;
	char	part, psize[3], pbsize[3],
		pfsize[3], poffset[3], ptype[3];
	int	partnum, maxpartnum = 0;

	if (cgetent(&buf, db_array, (char *) name) < 0)
		return NULL;

	bzero((char *)&disk, sizeof(disk));
	/*
	 * typename
	 */
	cq = dp->d_typename;
	cp = buf;
	while (cq < dp->d_typename + sizeof(dp->d_typename) - 1 &&
	    (*cq = *cp) && *cq != '|' && *cq != ':')
		cq++, cp++;
	*cq = '\0';

#define getnumdflt(field, dname, dflt) \
	{ long f; (field) = (cgetnum(buf, dname, &f) == -1) ? (dflt) : f; }
#define	getnum(field, dname) \
	{ long f; cgetnum(buf, dname, &f); field = f; }

	getnumdflt(dp->d_secsize, "se", DEV_BSIZE);
	getnum(dp->d_ntracks, "nt");
	getnum(dp->d_nsectors, "ns");
	getnum(dp->d_ncylinders, "nc");

	if (cgetstr(buf, "dt", &cq) > 0)
		dp->d_type = (u_short)gettype(cq, dktypenames);
	else
		getnumdflt(dp->d_type, "dt", 0);
	getnumdflt(dp->d_secpercyl, "sc", dp->d_nsectors * dp->d_ntracks);
	/* XXX */
	dp->d_secperunith = 0;
	getnumdflt(dp->d_secperunit, "su", dp->d_secpercyl * dp->d_ncylinders);
	strlcpy(psize, "px", sizeof psize);
	strlcpy(pbsize, "bx", sizeof pbsize);
	strlcpy(pfsize, "fx", sizeof pfsize);
	strlcpy(poffset, "ox", sizeof poffset);
	strlcpy(ptype, "tx", sizeof ptype);
	pp = &dp->d_partitions[0];
	dp->d_version = 1;
	for (partnum = 0; partnum < MAXPARTITIONS; partnum++, pp++) {
		long f;

		psize[1] = pbsize[1] = pfsize[1] = poffset[1] = ptype[1] =
		    DL_PARTNUM2NAME(partnum);
		/* XXX */
		if (cgetnum(buf, psize, &f) == -1)
			DL_SETPSIZE(pp, 0);
		else {
			u_int32_t fsize, frag = 8;

			DL_SETPSIZE(pp, f);
			/* XXX */
			pp->p_offseth = 0;
			getnum(pp->p_offset, poffset);
			getnumdflt(fsize, pfsize, 0);
			if (fsize) {
				long bsize;

				if (cgetnum(buf, pbsize, &bsize) == 0)
					frag = bsize / fsize;
				pp->p_fragblock =
				    DISKLABELV1_FFS_FRAGBLOCK(fsize, frag);
			}
			getnumdflt(pp->p_fstype, ptype, 0);
			if (pp->p_fstype == 0 && cgetstr(buf, ptype, &cq) > 0)
				pp->p_fstype = (u_char)gettype(cq, fstypenames);
			maxpartnum = partnum;
		}
	}
	dp->d_npartitions = maxpartnum;
	dp->d_magic = DISKMAGIC;
	dp->d_magic2 = DISKMAGIC;
	free(buf);
	return (dp);
}

static u_int
gettype(char *t, const char * const *names)
{
	const char * const *nm;

	for (nm = names; *nm; nm++)
		if (strcasecmp(t, *nm) == 0)
			return (nm - names);
	if (isdigit((u_char)*t))
		return ((u_int)strtonum(t, 0, USHRT_MAX, NULL));
	return (0);
}
