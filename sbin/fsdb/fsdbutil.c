/*	$NetBSD: fsdbutil.c,v 1.2 1995/10/08 23:18:12 thorpej Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 1995 John T. Kohl
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <ctype.h>
#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <timeconv.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <sys/ioctl.h>

#include "fsdb.h"
#include "fsck.h"

void prtblknos(struct uufsd *disk, union dinode *dp);

char **
crack(char *line, int *argc)
{
    static char *argv[8];
    int i;
    char *p, *val;
    for (p = line, i = 0; p != NULL && i < 8; i++) {
	while ((val = strsep(&p, " \t\n")) != NULL && *val == '\0')
	    /**/;
	if (val)
	    argv[i] = val;
	else
	    break;
    }
    *argc = i;
    return argv;
}

char **
recrack(char *line, int *argc, int argc_max)
{
    static char *argv[8];
    int i;
    char *p, *val;
    for (p = line, i = 0; p != NULL && i < 8 && i < argc_max - 1; i++) {
	while ((val = strsep(&p, " \t\n")) != NULL && *val == '\0')
	    /**/;
	if (val)
	    argv[i] = val;
	else
	    break;
    }
    argv[i] = argv[i - 1] + strlen(argv[i - 1]) + 1;
    argv[i][strcspn(argv[i], "\n")] = '\0';
    *argc = i + 1;
    return argv;
}

int
argcount(struct cmdtable *cmdp, int argc, char *argv[])
{
    if (cmdp->minargc == cmdp->maxargc)
	warnx("command `%s' takes %u arguments, got %u", cmdp->cmd,
	    cmdp->minargc-1, argc-1);
    else
	warnx("command `%s' takes from %u to %u arguments",
	      cmdp->cmd, cmdp->minargc-1, cmdp->maxargc-1);
	    
    warnx("usage: %s: %s", cmdp->cmd, cmdp->helptxt);
    return 1;
}

void
printstat(const char *cp, ino_t inum, union dinode *dp)
{
    struct group *grp;
    struct passwd *pw;
    ufs2_daddr_t blocks;
    int64_t gen;
    char *p;
    time_t t;

    printf("%s: ", cp);
    switch (DIP(dp, di_mode) & IFMT) {
    case IFDIR:
	puts("directory");
	break;
    case IFREG:
	puts("regular file");
	break;
    case IFBLK:
	printf("block special (%#jx)", (uintmax_t)DIP(dp, di_rdev));
	break;
    case IFCHR:
	printf("character special (%#jx)", DIP(dp, di_rdev));
	break;
    case IFLNK:
	fputs("symlink",stdout);
	if (DIP(dp, di_size) > 0 &&
	    DIP(dp, di_size) < sblock.fs_maxsymlinklen &&
	    DIP(dp, di_blocks) == 0) {
	    if (sblock.fs_magic == FS_UFS1_MAGIC)
		p = (caddr_t)dp->dp1.di_db;
	    else
		p = (caddr_t)dp->dp2.di_db;
	    printf(" to `%.*s'\n", (int) DIP(dp, di_size), p);
	} else {
	    putchar('\n');
	}
	break;
    case IFSOCK:
	puts("socket");
	break;
    case IFIFO:
	puts("fifo");
	break;
    }
    printf("I=%ju MODE=%o SIZE=%ju", (uintmax_t)inum, DIP(dp, di_mode),
	(uintmax_t)DIP(dp, di_size));
    if (sblock.fs_magic != FS_UFS1_MAGIC) {
	t = _time64_to_time(dp->dp2.di_birthtime);
	p = ctime(&t);
	printf("\n\tBTIME=%15.15s %4.4s [%d nsec]", &p[4], &p[20],
	   dp->dp2.di_birthnsec);
    }
    if (sblock.fs_magic == FS_UFS1_MAGIC)
	t = _time32_to_time(dp->dp1.di_mtime);
    else
	t = _time64_to_time(dp->dp2.di_mtime);
    p = ctime(&t);
    printf("\n\tMTIME=%15.15s %4.4s [%d nsec]", &p[4], &p[20],
	   DIP(dp, di_mtimensec));
    if (sblock.fs_magic == FS_UFS1_MAGIC)
	t = _time32_to_time(dp->dp1.di_ctime);
    else
	t = _time64_to_time(dp->dp2.di_ctime);
    p = ctime(&t);
    printf("\n\tCTIME=%15.15s %4.4s [%d nsec]", &p[4], &p[20],
	   DIP(dp, di_ctimensec));
    if (sblock.fs_magic == FS_UFS1_MAGIC)
	t = _time32_to_time(dp->dp1.di_atime);
    else
	t = _time64_to_time(dp->dp2.di_atime);
    p = ctime(&t);
    printf("\n\tATIME=%15.15s %4.4s [%d nsec]\n", &p[4], &p[20],
	   DIP(dp, di_atimensec));

    if ((pw = getpwuid(DIP(dp, di_uid))))
	printf("OWNER=%s ", pw->pw_name);
    else
	printf("OWNUID=%u ", DIP(dp, di_uid));
    if ((grp = getgrgid(DIP(dp, di_gid))))
	printf("GRP=%s ", grp->gr_name);
    else
	printf("GID=%u ", DIP(dp, di_gid));

    blocks = DIP(dp, di_blocks);
    gen = DIP(dp, di_gen);
    printf("LINKCNT=%d FLAGS=%#x BLKCNT=%jx GEN=%jx\n", DIP(dp, di_nlink),
	DIP(dp, di_flags), (intmax_t)blocks, (intmax_t)gen);
}


int
checkactive(void)
{
    if (!curinode) {
	warnx("no current inode\n");
	return 0;
    }
    return 1;
}

int
checkactivedir(void)
{
    if (!curinode) {
	warnx("no current inode\n");
	return 0;
    }
    if ((DIP(curinode, di_mode) & IFMT) != IFDIR) {
	warnx("inode %ju not a directory", (uintmax_t)curinum);
	return 0;
    }
    return 1;
}

int
printactive(int doblocks)
{
    if (!checkactive())
	return 1;
    switch (DIP(curinode, di_mode) & IFMT) {
    case IFDIR:
    case IFREG:
    case IFBLK:
    case IFCHR:
    case IFLNK:
    case IFSOCK:
    case IFIFO:
	if (doblocks)
	    prtblknos(&disk, curinode);
	else
	    printstat("current inode", curinum, curinode);
	break;
    case 0:
	printf("current inode %ju: unallocated inode\n", (uintmax_t)curinum);
	break;
    default:
	printf("current inode %ju: screwy itype 0%o (mode 0%o)?\n",
	    (uintmax_t)curinum, DIP(curinode, di_mode) & IFMT,
	    DIP(curinode, di_mode));
	break;
    }
    return 0;
}
