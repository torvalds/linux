/* $FreeBSD$ */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "ficl.h"

#if FICL_WANT_FILE
/*
**
** fileaccess.c
**
** Implements all of the File Access word set that can be implemented in portable C.
**
*/

static void pushIor(FICL_VM *pVM, int success)
{
    int ior;
    if (success)
        ior = 0;
    else
        ior = errno;
    stackPushINT(pVM->pStack, ior);
}



static void ficlFopen(FICL_VM *pVM, char *writeMode) /* ( c-addr u fam -- fileid ior ) */
{
    int fam = stackPopINT(pVM->pStack);
    int length = stackPopINT(pVM->pStack);
    void *address = (void *)stackPopPtr(pVM->pStack);
    char mode[4];
    FILE *f;

    char *filename = (char *)alloca(length + 1);
    memcpy(filename, address, length);
    filename[length] = 0;

    *mode = 0;

    switch (FICL_FAM_OPEN_MODE(fam))
        {
        case 0:
            stackPushPtr(pVM->pStack, NULL);
            stackPushINT(pVM->pStack, EINVAL);
            return;
        case FICL_FAM_READ:
            strcat(mode, "r");
            break;
        case FICL_FAM_WRITE:
            strcat(mode, writeMode);
            break;
        case FICL_FAM_READ | FICL_FAM_WRITE:
            strcat(mode, writeMode);
            strcat(mode, "+");
            break;
        }

    strcat(mode, (fam & FICL_FAM_BINARY) ? "b" : "t");

    f = fopen(filename, mode);
    if (f == NULL)
        stackPushPtr(pVM->pStack, NULL);
    else
#ifdef LOADER_VERIEXEC
	if (*mode == 'r' &&
	    verify_file(fileno(f), filename, 0, VE_GUESS) < 0) {
	    fclose(f);
	    stackPushPtr(pVM->pStack, NULL);
	} else
#endif
        {
	    ficlFILE *ff = (ficlFILE *)malloc(sizeof(ficlFILE));
	    strcpy(ff->filename, filename);
	    ff->f = f;
	    stackPushPtr(pVM->pStack, ff);

	    fseek(f, 0, SEEK_SET);
	}
    pushIor(pVM, f != NULL);
}



static void ficlOpenFile(FICL_VM *pVM) /* ( c-addr u fam -- fileid ior ) */
{
    ficlFopen(pVM, "a");
}


static void ficlCreateFile(FICL_VM *pVM) /* ( c-addr u fam -- fileid ior ) */
{
    ficlFopen(pVM, "w");
}


static int closeFiclFILE(ficlFILE *ff) /* ( fileid -- ior ) */
{
    FILE *f = ff->f;
    free(ff);
    return !fclose(f);
}

static void ficlCloseFile(FICL_VM *pVM) /* ( fileid -- ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    pushIor(pVM, closeFiclFILE(ff));
}

static void ficlDeleteFile(FICL_VM *pVM) /* ( c-addr u -- ior ) */
{
    int length = stackPopINT(pVM->pStack);
    void *address = (void *)stackPopPtr(pVM->pStack);

    char *filename = (char *)alloca(length + 1);
    memcpy(filename, address, length);
    filename[length] = 0;

    pushIor(pVM, !unlink(filename));
}

static void ficlRenameFile(FICL_VM *pVM) /* ( c-addr1 u1 c-addr2 u2 -- ior ) */
{
    int length;
    void *address;
    char *from;
    char *to;

    length = stackPopINT(pVM->pStack);
    address = (void *)stackPopPtr(pVM->pStack);
    to = (char *)alloca(length + 1);
    memcpy(to, address, length);
    to[length] = 0;

    length = stackPopINT(pVM->pStack);
    address = (void *)stackPopPtr(pVM->pStack);

    from = (char *)alloca(length + 1);
    memcpy(from, address, length);
    from[length] = 0;

    pushIor(pVM, !rename(from, to));
}

static void ficlFileStatus(FICL_VM *pVM) /* ( c-addr u -- x ior ) */
{
    struct stat statbuf;

    int length = stackPopINT(pVM->pStack);
    void *address = (void *)stackPopPtr(pVM->pStack);

    char *filename = (char *)alloca(length + 1);
    memcpy(filename, address, length);
    filename[length] = 0;

    if (stat(filename, &statbuf) == 0)
    {
        /*
        ** the "x" left on the stack is implementation-defined.
        ** I push the file's access mode (readable, writeable, is directory, etc)
        ** as defined by ANSI C.
        */
        stackPushINT(pVM->pStack, statbuf.st_mode);
        stackPushINT(pVM->pStack, 0);
    }
    else
    {
        stackPushINT(pVM->pStack, -1);
        stackPushINT(pVM->pStack, ENOENT);
    }
}


static void ficlFilePosition(FICL_VM *pVM) /* ( fileid -- ud ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    long ud = ftell(ff->f);
    stackPushINT(pVM->pStack, ud);
    pushIor(pVM, ud != -1);
}



static long fileSize(FILE *f)
{
    struct stat statbuf;
    statbuf.st_size = -1;
    if (fstat(fileno(f), &statbuf) != 0)
        return -1;
    return statbuf.st_size;
}



static void ficlFileSize(FICL_VM *pVM) /* ( fileid -- ud ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    long ud = fileSize(ff->f);
    stackPushINT(pVM->pStack, ud);
    pushIor(pVM, ud != -1);
}



#define nLINEBUF 256
static void ficlIncludeFile(FICL_VM *pVM) /* ( i*x fileid -- j*x ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    CELL id = pVM->sourceID;
    int     result = VM_OUTOFTEXT;
    long currentPosition, totalSize;
    long size;
    pVM->sourceID.p = (void *)ff;

    currentPosition = ftell(ff->f);
    totalSize = fileSize(ff->f);
    size = totalSize - currentPosition;

    if ((totalSize != -1) && (currentPosition != -1) && (size > 0))
        {
        char *buffer = (char *)malloc(size);
        long got = fread(buffer, 1, size, ff->f);
        if (got == size)
            result = ficlExecC(pVM, buffer, size);
        }

#if 0
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    CELL id = pVM->sourceID;
    char    cp[nLINEBUF];
    int     nLine = 0;
    int     keepGoing;
    int     result;
    pVM->sourceID.p = (void *)ff;

    /* feed each line to ficlExec */
    keepGoing = TRUE;
    while (keepGoing && fgets(cp, nLINEBUF, ff->f))
    {
        int len = strlen(cp) - 1;

        nLine++;
        if (len <= 0)
            continue;

        if (cp[len] == '\n')
            cp[len] = '\0';

        result = ficlExec(pVM, cp);

        switch (result)
        {
            case VM_OUTOFTEXT:
            case VM_USEREXIT:
                break;

            default:
                pVM->sourceID = id;
                keepGoing = FALSE;
                break; 
        }
    }
#endif /* 0 */
    /*
    ** Pass an empty line with SOURCE-ID == -1 to flush
    ** any pending REFILLs (as required by FILE wordset)
    */
    pVM->sourceID.i = -1;
    ficlExec(pVM, "");

    pVM->sourceID = id;
    closeFiclFILE(ff);
}



static void ficlReadFile(FICL_VM *pVM) /* ( c-addr u1 fileid -- u2 ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    int length = stackPopINT(pVM->pStack);
    void *address = (void *)stackPopPtr(pVM->pStack);
    int result;

    clearerr(ff->f);
    result = fread(address, 1, length, ff->f);

    stackPushINT(pVM->pStack, result);
    pushIor(pVM, ferror(ff->f) == 0);
}



static void ficlReadLine(FICL_VM *pVM) /* ( c-addr u1 fileid -- u2 flag ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    int length = stackPopINT(pVM->pStack);
    char *address = (char *)stackPopPtr(pVM->pStack);
    int error;
    int flag;

    if (feof(ff->f))
        {
        stackPushINT(pVM->pStack, -1);
        stackPushINT(pVM->pStack, 0);
        stackPushINT(pVM->pStack, 0);
        return;
        }

    clearerr(ff->f);
    *address = 0;
    fgets(address, length, ff->f);

    error = ferror(ff->f);
    if (error != 0)
        {
        stackPushINT(pVM->pStack, -1);
        stackPushINT(pVM->pStack, 0);
        stackPushINT(pVM->pStack, error);
        return;
        }

    length = strlen(address);
    flag = (length > 0);
    if (length && ((address[length - 1] == '\r') || (address[length - 1] == '\n')))
        length--;
    
    stackPushINT(pVM->pStack, length);
    stackPushINT(pVM->pStack, flag);
    stackPushINT(pVM->pStack, 0); /* ior */
}



static void ficlWriteFile(FICL_VM *pVM) /* ( c-addr u1 fileid -- ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    int length = stackPopINT(pVM->pStack);
    void *address = (void *)stackPopPtr(pVM->pStack);

    clearerr(ff->f);
    fwrite(address, 1, length, ff->f);
    pushIor(pVM, ferror(ff->f) == 0);
}



static void ficlWriteLine(FICL_VM *pVM) /* ( c-addr u1 fileid -- ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    size_t length = (size_t)stackPopINT(pVM->pStack);
    void *address = (void *)stackPopPtr(pVM->pStack);

    clearerr(ff->f);
    if (fwrite(address, 1, length, ff->f) == length)
        fwrite("\n", 1, 1, ff->f);
    pushIor(pVM, ferror(ff->f) == 0);
}



static void ficlRepositionFile(FICL_VM *pVM) /* ( ud fileid -- ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    size_t ud = (size_t)stackPopINT(pVM->pStack);

    pushIor(pVM, fseek(ff->f, ud, SEEK_SET) == 0);
}



static void ficlFlushFile(FICL_VM *pVM) /* ( fileid -- ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    pushIor(pVM, fflush(ff->f) == 0);
}



#if FICL_HAVE_FTRUNCATE

static void ficlResizeFile(FICL_VM *pVM) /* ( ud fileid -- ior ) */
{
    ficlFILE *ff = (ficlFILE *)stackPopPtr(pVM->pStack);
    size_t ud = (size_t)stackPopINT(pVM->pStack);

    pushIor(pVM, ftruncate(fileno(ff->f), ud) == 0);
}

#endif /* FICL_HAVE_FTRUNCATE */

#endif /* FICL_WANT_FILE */



void ficlCompileFile(FICL_SYSTEM *pSys)
{
#if FICL_WANT_FILE
    FICL_DICT *dp = pSys->dp;
    assert(dp);

    dictAppendWord(dp, "create-file", ficlCreateFile,  FW_DEFAULT);
    dictAppendWord(dp, "open-file", ficlOpenFile,  FW_DEFAULT);
    dictAppendWord(dp, "close-file", ficlCloseFile,  FW_DEFAULT);
    dictAppendWord(dp, "include-file", ficlIncludeFile,  FW_DEFAULT);
    dictAppendWord(dp, "read-file", ficlReadFile,  FW_DEFAULT);
    dictAppendWord(dp, "read-line", ficlReadLine,  FW_DEFAULT);
    dictAppendWord(dp, "write-file", ficlWriteFile,  FW_DEFAULT);
    dictAppendWord(dp, "write-line", ficlWriteLine,  FW_DEFAULT);
    dictAppendWord(dp, "file-position", ficlFilePosition,  FW_DEFAULT);
    dictAppendWord(dp, "file-size", ficlFileSize,  FW_DEFAULT);
    dictAppendWord(dp, "reposition-file", ficlRepositionFile,  FW_DEFAULT);
    dictAppendWord(dp, "file-status", ficlFileStatus,  FW_DEFAULT);
    dictAppendWord(dp, "flush-file", ficlFlushFile,  FW_DEFAULT);

    dictAppendWord(dp, "delete-file", ficlDeleteFile,  FW_DEFAULT);
    dictAppendWord(dp, "rename-file", ficlRenameFile,  FW_DEFAULT);

#ifdef FICL_HAVE_FTRUNCATE
    dictAppendWord(dp, "resize-file", ficlResizeFile,  FW_DEFAULT);

    ficlSetEnv(pSys, "file", FICL_TRUE);
    ficlSetEnv(pSys, "file-ext", FICL_TRUE);
#endif /* FICL_HAVE_FTRUNCATE */
#else
    (void)pSys;
#endif /* FICL_WANT_FILE */
}
