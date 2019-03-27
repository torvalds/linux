/*-
 * Copyright (c) 2000 Daniel Capo Sobral
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
 *
 *	$FreeBSD$
 */

/*******************************************************************
** l o a d e r . c
** Additional FICL words designed for FreeBSD's loader
** 
*******************************************************************/

#ifdef TESTMAIN
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#else
#include <stand.h>
#endif
#include "bootstrap.h"
#include <string.h>
#include <uuid.h>
#include "ficl.h"

/*		FreeBSD's loader interaction words and extras
 *
 * 		setenv      ( value n name n' -- )
 * 		setenv?     ( value n name n' flag -- )
 * 		getenv      ( addr n -- addr' n' | -1 )
 * 		unsetenv    ( addr n -- )
 * 		copyin      ( addr addr' len -- )
 * 		copyout     ( addr addr' len -- )
 * 		findfile    ( name len type len' -- addr )
 * 		pnpdevices  ( -- addr )
 * 		pnphandlers ( -- addr )
 * 		ccall       ( [[...[p10] p9] ... p1] n addr -- result )
 *		uuid-from-string ( addr n -- addr' )
 *		uuid-to-string ( addr' -- addr n )
 * 		.#	    ( value -- )
 */

void
ficlSetenv(FICL_VM *pVM)
{
#ifndef TESTMAIN
	char	*name, *value;
#endif
	char	*namep, *valuep;
	int	names, values;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 4, 0);
#endif
	names = stackPopINT(pVM->pStack);
	namep = (char*) stackPopPtr(pVM->pStack);
	values = stackPopINT(pVM->pStack);
	valuep = (char*) stackPopPtr(pVM->pStack);

#ifndef TESTMAIN
	name = (char*) ficlMalloc(names+1);
	if (!name)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(name, namep, names);
	name[names] = '\0';
	value = (char*) ficlMalloc(values+1);
	if (!value)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(value, valuep, values);
	value[values] = '\0';

	setenv(name, value, 1);
	ficlFree(name);
	ficlFree(value);
#endif

	return;
}

void
ficlSetenvq(FICL_VM *pVM)
{
#ifndef TESTMAIN
	char	*name, *value;
#endif
	char	*namep, *valuep;
	int	names, values, overwrite;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 5, 0);
#endif
	overwrite = stackPopINT(pVM->pStack);
	names = stackPopINT(pVM->pStack);
	namep = (char*) stackPopPtr(pVM->pStack);
	values = stackPopINT(pVM->pStack);
	valuep = (char*) stackPopPtr(pVM->pStack);

#ifndef TESTMAIN
	name = (char*) ficlMalloc(names+1);
	if (!name)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(name, namep, names);
	name[names] = '\0';
	value = (char*) ficlMalloc(values+1);
	if (!value)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(value, valuep, values);
	value[values] = '\0';

	setenv(name, value, overwrite);
	ficlFree(name);
	ficlFree(value);
#endif

	return;
}

void
ficlGetenv(FICL_VM *pVM)
{
#ifndef TESTMAIN
	char	*name, *value;
#endif
	char	*namep;
	int	names;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 2, 2);
#endif
	names = stackPopINT(pVM->pStack);
	namep = (char*) stackPopPtr(pVM->pStack);

#ifndef TESTMAIN
	name = (char*) ficlMalloc(names+1);
	if (!name)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(name, namep, names);
	name[names] = '\0';

	value = getenv(name);
	ficlFree(name);

	if(value != NULL) {
		stackPushPtr(pVM->pStack, value);
		stackPushINT(pVM->pStack, strlen(value));
	} else
#endif
		stackPushINT(pVM->pStack, -1);

	return;
}

void
ficlUnsetenv(FICL_VM *pVM)
{
#ifndef TESTMAIN
	char	*name;
#endif
	char	*namep;
	int	names;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 2, 0);
#endif
	names = stackPopINT(pVM->pStack);
	namep = (char*) stackPopPtr(pVM->pStack);

#ifndef TESTMAIN
	name = (char*) ficlMalloc(names+1);
	if (!name)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(name, namep, names);
	name[names] = '\0';

	unsetenv(name);
	ficlFree(name);
#endif

	return;
}

void
ficlCopyin(FICL_VM *pVM)
{
	void*		src;
	vm_offset_t	dest;
	size_t		len;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 3, 0);
#endif

	len = stackPopINT(pVM->pStack);
	dest = stackPopINT(pVM->pStack);
	src = stackPopPtr(pVM->pStack);

#ifndef TESTMAIN
	archsw.arch_copyin(src, dest, len);
#endif

	return;
}

void
ficlCopyout(FICL_VM *pVM)
{
	void*		dest;
	vm_offset_t	src;
	size_t		len;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 3, 0);
#endif

	len = stackPopINT(pVM->pStack);
	dest = stackPopPtr(pVM->pStack);
	src = stackPopINT(pVM->pStack);

#ifndef TESTMAIN
	archsw.arch_copyout(src, dest, len);
#endif

	return;
}

void
ficlFindfile(FICL_VM *pVM)
{
#ifndef TESTMAIN
	char	*name, *type;
#endif
	char	*namep, *typep;
	struct	preloaded_file* fp;
	int	names, types;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 4, 1);
#endif

	types = stackPopINT(pVM->pStack);
	typep = (char*) stackPopPtr(pVM->pStack);
	names = stackPopINT(pVM->pStack);
	namep = (char*) stackPopPtr(pVM->pStack);
#ifndef TESTMAIN
	name = (char*) ficlMalloc(names+1);
	if (!name)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(name, namep, names);
	name[names] = '\0';
	type = (char*) ficlMalloc(types+1);
	if (!type)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(type, typep, types);
	type[types] = '\0';

	fp = file_findfile(name, type);
#else
	fp = NULL;
#endif
	stackPushPtr(pVM->pStack, fp);

	return;
}

void
ficlCcall(FICL_VM *pVM)
{
	int (*func)(int, ...);
	int result, p[10];
	int nparam, i;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 2, 0);
#endif

	func = stackPopPtr(pVM->pStack);
	nparam = stackPopINT(pVM->pStack);

#if FICL_ROBUST > 1
	vmCheckStack(pVM, nparam, 1);
#endif

	for (i = 0; i < nparam; i++)
		p[i] = stackPopINT(pVM->pStack);

	result = func(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8],
	    p[9]);

	stackPushINT(pVM->pStack, result);

	return;
}

void
ficlUuidFromString(FICL_VM *pVM)
{
#ifndef	TESTMAIN
	char	*uuid;
	uint32_t status;
#endif
	char	*uuidp;
	int	uuids;
	uuid_t	*u;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 2, 0);
#endif

	uuids = stackPopINT(pVM->pStack);
	uuidp = (char *) stackPopPtr(pVM->pStack);

#ifndef	TESTMAIN
	uuid = (char *)ficlMalloc(uuids + 1);
	if (!uuid)
		vmThrowErr(pVM, "Error: out of memory");
	strncpy(uuid, uuidp, uuids);
	uuid[uuids] = '\0';

	u = (uuid_t *)ficlMalloc(sizeof (*u));

	uuid_from_string(uuid, u, &status);
	ficlFree(uuid);
	if (status != uuid_s_ok) {
		ficlFree(u);
		u = NULL;
	}
#else
	u = NULL;
#endif
	stackPushPtr(pVM->pStack, u);


	return;
}

void
ficlUuidToString(FICL_VM *pVM)
{
#ifndef	TESTMAIN
	char	*uuid;
	uint32_t status;
#endif
	uuid_t	*u;

#if FICL_ROBUST > 1
	vmCheckStack(pVM, 1, 0);
#endif

	u = (uuid_t *)stackPopPtr(pVM->pStack);

#ifndef	TESTMAIN
	uuid_to_string(u, &uuid, &status);
	if (status != uuid_s_ok) {
		stackPushPtr(pVM->pStack, uuid);
		stackPushINT(pVM->pStack, strlen(uuid));
	} else
#endif
		stackPushINT(pVM->pStack, -1);

	return;
}

/**************************************************************************
                        f i c l E x e c F D
** reads in text from file fd and passes it to ficlExec()
 * returns VM_OUTOFTEXT on success or the ficlExec() error code on
 * failure.
 */ 
#define nLINEBUF 256
int ficlExecFD(FICL_VM *pVM, int fd)
{
    char    cp[nLINEBUF];
    int     nLine = 0, rval = VM_OUTOFTEXT;
    char    ch;
    CELL    id;

    id = pVM->sourceID;
    pVM->sourceID.i = fd;

    /* feed each line to ficlExec */
    while (1) {
	int status, i;

	i = 0;
	while ((status = read(fd, &ch, 1)) > 0 && ch != '\n')
	    cp[i++] = ch;
        nLine++;
	if (!i) {
	    if (status < 1)
		break;
	    continue;
	}
        rval = ficlExecC(pVM, cp, i);
	if(rval != VM_QUIT && rval != VM_USEREXIT && rval != VM_OUTOFTEXT)
        {
            pVM->sourceID = id;
            return rval; 
        }
    }
    /*
    ** Pass an empty line with SOURCE-ID == -1 to flush
    ** any pending REFILLs (as required by FILE wordset)
    */
    pVM->sourceID.i = -1;
    ficlExec(pVM, "");

    pVM->sourceID = id;
    return rval;
}

static void displayCellNoPad(FICL_VM *pVM)
{
    CELL c;
#if FICL_ROBUST > 1
    vmCheckStack(pVM, 1, 0);
#endif
    c = stackPop(pVM->pStack);
    ltoa((c).i, pVM->pad, pVM->base);
    vmTextOut(pVM, pVM->pad, 0);
    return;
}

/*      isdir? - Return whether an fd corresponds to a directory.
 *
 * isdir? ( fd -- bool )
 */
static void isdirQuestion(FICL_VM *pVM)
{
    struct stat sb;
    FICL_INT flag;
    int fd;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 1, 1);
#endif

    fd = stackPopINT(pVM->pStack);
    flag = FICL_FALSE;
    do {
        if (fd < 0)
            break;
        if (fstat(fd, &sb) < 0)
            break;
        if (!S_ISDIR(sb.st_mode))
            break;
        flag = FICL_TRUE;
    } while (0);
    stackPushINT(pVM->pStack, flag);
}

/*          fopen - open a file and return new fd on stack.
 *
 * fopen ( ptr count mode -- fd )
 */
static void pfopen(FICL_VM *pVM)
{
    int     mode, fd, count;
    char    *ptr, *name;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 3, 1);
#endif

    mode = stackPopINT(pVM->pStack);    /* get mode */
    count = stackPopINT(pVM->pStack);   /* get count */
    ptr = stackPopPtr(pVM->pStack);     /* get ptr */

    if ((count < 0) || (ptr == NULL)) {
        stackPushINT(pVM->pStack, -1);
        return;
    }

    /* ensure that the string is null terminated */
    name = (char *)malloc(count+1);
    bcopy(ptr,name,count);
    name[count] = 0;

    /* open the file */
    fd = open(name, mode);
    free(name);
    stackPushINT(pVM->pStack, fd);
    return;
}
 
/*          fclose - close a file who's fd is on stack.
 *
 * fclose ( fd -- )
 */
static void pfclose(FICL_VM *pVM)
{
    int fd;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 1, 0);
#endif
    fd = stackPopINT(pVM->pStack); /* get fd */
    if (fd != -1)
	close(fd);
    return;
}

/*          fread - read file contents
 *
 * fread  ( fd buf nbytes  -- nread )
 */
static void pfread(FICL_VM *pVM)
{
    int     fd, len;
    char *buf;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 3, 1);
#endif
    len = stackPopINT(pVM->pStack); /* get number of bytes to read */
    buf = stackPopPtr(pVM->pStack); /* get buffer */
    fd = stackPopINT(pVM->pStack); /* get fd */
    if (len > 0 && buf && fd != -1)
	stackPushINT(pVM->pStack, read(fd, buf, len));
    else
	stackPushINT(pVM->pStack, -1);
    return;
}

/*      freaddir - read directory contents
 *
 * freaddir ( fd -- ptr len TRUE | FALSE )
 */
static void pfreaddir(FICL_VM *pVM)
{
#ifdef TESTMAIN
    static struct dirent dirent;
    struct stat sb;
    char *buf;
    off_t off, ptr;
    u_int blksz;
    int bufsz;
#endif
    struct dirent *d;
    int fd;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 1, 3);
#endif

    fd = stackPopINT(pVM->pStack);
#if TESTMAIN
    /*
     * The readdirfd() function is specific to the loader environment.
     * We do the best we can to make freaddir work, but it's not at
     * all guaranteed.
     */
    d = NULL;
    buf = NULL;
    do {
	if (fd == -1)
	    break;
	if (fstat(fd, &sb) == -1)
	    break;
	blksz = (sb.st_blksize) ? sb.st_blksize : getpagesize();
	if ((blksz & (blksz - 1)) != 0)
	    break;
	buf = malloc(blksz);
	if (buf == NULL)
	    break;
	off = lseek(fd, 0LL, SEEK_CUR);
	if (off == -1)
	    break;
	ptr = off;
	if (lseek(fd, 0, SEEK_SET) == -1)
	    break;
	bufsz = getdents(fd, buf, blksz);
	while (bufsz > 0 && bufsz <= ptr) {
	    ptr -= bufsz;
	    bufsz = getdents(fd, buf, blksz);
	}
	if (bufsz <= 0)
	    break;
	d = (void *)(buf + ptr);
	dirent = *d;
	off += d->d_reclen;
	d = (lseek(fd, off, SEEK_SET) != off) ? NULL : &dirent;
    } while (0);
    if (buf != NULL)
	free(buf);
#else
    d = readdirfd(fd);
#endif
    if (d != NULL) {
        stackPushPtr(pVM->pStack, d->d_name);
        stackPushINT(pVM->pStack, strlen(d->d_name));
        stackPushINT(pVM->pStack, FICL_TRUE);
    } else {
        stackPushINT(pVM->pStack, FICL_FALSE);
    }
}

/*          fload - interpret file contents
 *
 * fload  ( fd -- )
 */
static void pfload(FICL_VM *pVM)
{
    int     fd;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 1, 0);
#endif
    fd = stackPopINT(pVM->pStack); /* get fd */
    if (fd != -1)
	ficlExecFD(pVM, fd);
    return;
}

/*          fwrite - write file contents
 *
 * fwrite  ( fd buf nbytes  -- nwritten )
 */
static void pfwrite(FICL_VM *pVM)
{
    int     fd, len;
    char *buf;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 3, 1);
#endif
    len = stackPopINT(pVM->pStack); /* get number of bytes to read */
    buf = stackPopPtr(pVM->pStack); /* get buffer */
    fd = stackPopINT(pVM->pStack); /* get fd */
    if (len > 0 && buf && fd != -1)
	stackPushINT(pVM->pStack, write(fd, buf, len));
    else
	stackPushINT(pVM->pStack, -1);
    return;
}

/*          fseek - seek to a new position in a file
 *
 * fseek  ( fd ofs whence  -- pos )
 */
static void pfseek(FICL_VM *pVM)
{
    int     fd, pos, whence;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 3, 1);
#endif
    whence = stackPopINT(pVM->pStack);
    pos = stackPopINT(pVM->pStack);
    fd = stackPopINT(pVM->pStack);
    stackPushINT(pVM->pStack, lseek(fd, pos, whence));
    return;
}

/*           key - get a character from stdin
 *
 * key ( -- char )
 */
static void key(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckStack(pVM, 0, 1);
#endif
    stackPushINT(pVM->pStack, getchar());
    return;
}

/*           key? - check for a character from stdin (FACILITY)
 *
 * key? ( -- flag )
 */
static void keyQuestion(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckStack(pVM, 0, 1);
#endif
#ifdef TESTMAIN
    /* XXX Since we don't fiddle with termios, let it always succeed... */
    stackPushINT(pVM->pStack, FICL_TRUE);
#else
    /* But here do the right thing. */
    stackPushINT(pVM->pStack, ischar()? FICL_TRUE : FICL_FALSE);
#endif
    return;
}

/* seconds - gives number of seconds since beginning of time
 *
 * beginning of time is defined as:
 *
 *	BTX	- number of seconds since midnight
 *	FreeBSD	- number of seconds since Jan 1 1970
 *
 * seconds ( -- u )
 */
static void pseconds(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckStack(pVM,0,1);
#endif
    stackPushUNS(pVM->pStack, (FICL_UNS) time(NULL));
    return;
}

/* ms - wait at least that many milliseconds (FACILITY)
 *
 * ms ( u -- )
 *
 */
static void ms(FICL_VM *pVM)
{
#if FICL_ROBUST > 1
    vmCheckStack(pVM,1,0);
#endif
#ifdef TESTMAIN
    usleep(stackPopUNS(pVM->pStack)*1000);
#else
    delay(stackPopUNS(pVM->pStack)*1000);
#endif
    return;
}

/*           fkey - get a character from a file
 *
 * fkey ( file -- char )
 */
static void fkey(FICL_VM *pVM)
{
    int i, fd;
    char ch;

#if FICL_ROBUST > 1
    vmCheckStack(pVM, 1, 1);
#endif
    fd = stackPopINT(pVM->pStack);
    i = read(fd, &ch, 1);
    stackPushINT(pVM->pStack, i > 0 ? ch : -1);
    return;
}


/*
** Retrieves free space remaining on the dictionary
*/

static void freeHeap(FICL_VM *pVM)
{
    stackPushINT(pVM->pStack, dictCellsAvail(ficlGetDict(pVM->pSys)));
}


/******************* Increase dictionary size on-demand ******************/
 
static void ficlDictThreshold(FICL_VM *pVM)
{
    stackPushPtr(pVM->pStack, &dictThreshold);
}
 
static void ficlDictIncrease(FICL_VM *pVM)
{
    stackPushPtr(pVM->pStack, &dictIncrease);
}

/**************************************************************************
                        f i c l C o m p i l e P l a t f o r m
** Build FreeBSD platform extensions into the system dictionary
**************************************************************************/
void ficlCompilePlatform(FICL_SYSTEM *pSys)
{
    ficlCompileFcn **fnpp;
    FICL_DICT *dp = pSys->dp;
    assert (dp);

    dictAppendWord(dp, ".#",        displayCellNoPad,    FW_DEFAULT);
    dictAppendWord(dp, "isdir?",    isdirQuestion,  FW_DEFAULT);
    dictAppendWord(dp, "fopen",	    pfopen,	    FW_DEFAULT);
    dictAppendWord(dp, "fclose",    pfclose,	    FW_DEFAULT);
    dictAppendWord(dp, "fread",	    pfread,	    FW_DEFAULT);
    dictAppendWord(dp, "freaddir",  pfreaddir,	    FW_DEFAULT);
    dictAppendWord(dp, "fload",	    pfload,	    FW_DEFAULT);
    dictAppendWord(dp, "fkey",	    fkey,	    FW_DEFAULT);
    dictAppendWord(dp, "fseek",     pfseek,	    FW_DEFAULT);
    dictAppendWord(dp, "fwrite",    pfwrite,	    FW_DEFAULT);
    dictAppendWord(dp, "key",	    key,	    FW_DEFAULT);
    dictAppendWord(dp, "key?",	    keyQuestion,    FW_DEFAULT);
    dictAppendWord(dp, "ms",        ms,             FW_DEFAULT);
    dictAppendWord(dp, "seconds",   pseconds,       FW_DEFAULT);
    dictAppendWord(dp, "heap?",     freeHeap,       FW_DEFAULT);
    dictAppendWord(dp, "dictthreshold", ficlDictThreshold, FW_DEFAULT);
    dictAppendWord(dp, "dictincrease", ficlDictIncrease, FW_DEFAULT);

    dictAppendWord(dp, "setenv",    ficlSetenv,	    FW_DEFAULT);
    dictAppendWord(dp, "setenv?",   ficlSetenvq,    FW_DEFAULT);
    dictAppendWord(dp, "getenv",    ficlGetenv,	    FW_DEFAULT);
    dictAppendWord(dp, "unsetenv",  ficlUnsetenv,   FW_DEFAULT);
    dictAppendWord(dp, "copyin",    ficlCopyin,	    FW_DEFAULT);
    dictAppendWord(dp, "copyout",   ficlCopyout,    FW_DEFAULT);
    dictAppendWord(dp, "findfile",  ficlFindfile,   FW_DEFAULT);
    dictAppendWord(dp, "ccall",	    ficlCcall,	    FW_DEFAULT);
    dictAppendWord(dp, "uuid-from-string", ficlUuidFromString, FW_DEFAULT);
    dictAppendWord(dp, "uuid-to-string", ficlUuidToString, FW_DEFAULT);

    SET_FOREACH(fnpp, Xficl_compile_set)
	(*fnpp)(pSys);

#if defined(__i386__)
    ficlSetEnv(pSys, "arch-i386",         FICL_TRUE);
    ficlSetEnv(pSys, "arch-powerpc",      FICL_FALSE);
#elif defined(__powerpc__)
    ficlSetEnv(pSys, "arch-i386",         FICL_FALSE);
    ficlSetEnv(pSys, "arch-powerpc",      FICL_TRUE);
#endif

    return;
}
