/* gzlib.c contains minimal changes required to be compiled with zlibWrapper:
 * - gz_statep was converted to union to work with -Wstrict-aliasing=1      */

/* gzlib.c -- zlib functions common to reading and writing gzip files
 * Copyright (C) 2004-2017 Mark Adler
 * For conditions of distribution and use, see http://www.zlib.net/zlib_license.html
 */

#include "gzguts.h"

#if defined(_WIN32) && !defined(__BORLANDC__) && !defined(__MINGW32__)
#  define LSEEK _lseeki64
#else
#if defined(_LARGEFILE64_SOURCE) && _LFS64_LARGEFILE-0
#  define LSEEK lseek64
#else
#  define LSEEK lseek
#endif
#endif

/* Local functions */
local void gz_reset OF((gz_statep));
local gzFile gz_open OF((const void *, int, const char *));

#if defined UNDER_CE

/* Map the Windows error number in ERROR to a locale-dependent error message
   string and return a pointer to it.  Typically, the values for ERROR come
   from GetLastError.

   The string pointed to shall not be modified by the application, but may be
   overwritten by a subsequent call to gz_strwinerror

   The gz_strwinerror function does not change the current setting of
   GetLastError. */
char ZLIB_INTERNAL *gz_strwinerror (error)
     DWORD error;
{
    static char buf[1024];

    wchar_t *msgbuf;
    DWORD lasterr = GetLastError();
    DWORD chars = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        NULL,
        error,
        0, /* Default language */
        (LPVOID)&msgbuf,
        0,
        NULL);
    if (chars != 0) {
        /* If there is an \r\n appended, zap it.  */
        if (chars >= 2
            && msgbuf[chars - 2] == '\r' && msgbuf[chars - 1] == '\n') {
            chars -= 2;
            msgbuf[chars] = 0;
        }

        if (chars > sizeof (buf) - 1) {
            chars = sizeof (buf) - 1;
            msgbuf[chars] = 0;
        }

        wcstombs(buf, msgbuf, chars + 1);
        LocalFree(msgbuf);
    }
    else {
        sprintf(buf, "unknown win32 error (%ld)", error);
    }

    SetLastError(lasterr);
    return buf;
}

#endif /* UNDER_CE */

/* Reset gzip file state */
local void gz_reset(state)
    gz_statep state;
{
    state.state->x.have = 0;              /* no output data available */
    if (state.state->mode == GZ_READ) {   /* for reading ... */
        state.state->eof = 0;             /* not at end of file */
        state.state->past = 0;            /* have not read past end yet */
        state.state->how = LOOK;          /* look for gzip header */
    }
    state.state->seek = 0;                /* no seek request pending */
    gz_error(state, Z_OK, NULL);    /* clear error */
    state.state->x.pos = 0;               /* no uncompressed data yet */
    state.state->strm.avail_in = 0;       /* no input data yet */
}

/* Open a gzip file either by name or file descriptor. */
local gzFile gz_open(path, fd, mode)
    const void *path;
    int fd;
    const char *mode;
{
    gz_statep state;
    z_size_t len;
    int oflag;
#ifdef O_CLOEXEC
    int cloexec = 0;
#endif
#ifdef O_EXCL
    int exclusive = 0;
#endif

    /* check input */
    if (path == NULL)
        return NULL;

    /* allocate gzFile structure to return */
    state.state = (gz_state*)malloc(sizeof(gz_state));
    if (state.state == NULL)
        return NULL;
    state.state->size = 0;            /* no buffers allocated yet */
    state.state->want = GZBUFSIZE;    /* requested buffer size */
    state.state->msg = NULL;          /* no error message yet */

    /* interpret mode */
    state.state->mode = GZ_NONE;
    state.state->level = Z_DEFAULT_COMPRESSION;
    state.state->strategy = Z_DEFAULT_STRATEGY;
    state.state->direct = 0;
    while (*mode) {
        if (*mode >= '0' && *mode <= '9')
            state.state->level = *mode - '0';
        else
            switch (*mode) {
            case 'r':
                state.state->mode = GZ_READ;
                break;
#ifndef NO_GZCOMPRESS
            case 'w':
                state.state->mode = GZ_WRITE;
                break;
            case 'a':
                state.state->mode = GZ_APPEND;
                break;
#endif
            case '+':       /* can't read and write at the same time */
                free(state.state);
                return NULL;
            case 'b':       /* ignore -- will request binary anyway */
                break;
#ifdef O_CLOEXEC
            case 'e':
                cloexec = 1;
                break;
#endif
#ifdef O_EXCL
            case 'x':
                exclusive = 1;
                break;
#endif
            case 'f':
                state.state->strategy = Z_FILTERED;
                break;
            case 'h':
                state.state->strategy = Z_HUFFMAN_ONLY;
                break;
            case 'R':
                state.state->strategy = Z_RLE;
                break;
            case 'F':
                state.state->strategy = Z_FIXED;
                break;
            case 'T':
                state.state->direct = 1;
                break;
            default:        /* could consider as an error, but just ignore */
                ;
            }
        mode++;
    }

    /* must provide an "r", "w", or "a" */
    if (state.state->mode == GZ_NONE) {
        free(state.state);
        return NULL;
    }

    /* can't force transparent read */
    if (state.state->mode == GZ_READ) {
        if (state.state->direct) {
            free(state.state);
            return NULL;
        }
        state.state->direct = 1;      /* for empty file */
    }

    /* save the path name for error messages */
#ifdef WIDECHAR
    if (fd == -2) {
        len = wcstombs(NULL, path, 0);
        if (len == (z_size_t)-1)
            len = 0;
    }
    else
#endif
        len = strlen((const char *)path);
    state.state->path = (char *)malloc(len + 1);
    if (state.state->path == NULL) {
        free(state.state);
        return NULL;
    }
#ifdef WIDECHAR
    if (fd == -2)
        if (len)
            wcstombs(state.state->path, path, len + 1);
        else
            *(state.state->path) = 0;
    else
#endif
#if !defined(NO_snprintf) && !defined(NO_vsnprintf)
        (void)snprintf(state.state->path, len + 1, "%s", (const char *)path);
#else
        strcpy(state.state->path, path);
#endif

    /* compute the flags for open() */
    oflag =
#ifdef O_LARGEFILE
        O_LARGEFILE |
#endif
#ifdef O_BINARY
        O_BINARY |
#endif
#ifdef O_CLOEXEC
        (cloexec ? O_CLOEXEC : 0) |
#endif
        (state.state->mode == GZ_READ ?
         O_RDONLY :
         (O_WRONLY | O_CREAT |
#ifdef O_EXCL
          (exclusive ? O_EXCL : 0) |
#endif
          (state.state->mode == GZ_WRITE ?
           O_TRUNC :
           O_APPEND)));

    /* open the file with the appropriate flags (or just use fd) */
    state.state->fd = fd > -1 ? fd : (
#ifdef WIDECHAR
        fd == -2 ? _wopen(path, oflag, 0666) :
#endif
        open((const char *)path, oflag, 0666));
    if (state.state->fd == -1) {
        free(state.state->path);
        free(state.state);
        return NULL;
    }
    if (state.state->mode == GZ_APPEND) {
        LSEEK(state.state->fd, 0, SEEK_END);  /* so gzoffset() is correct */
        state.state->mode = GZ_WRITE;         /* simplify later checks */
    }

    /* save the current position for rewinding (only if reading) */
    if (state.state->mode == GZ_READ) {
        state.state->start = LSEEK(state.state->fd, 0, SEEK_CUR);
        if (state.state->start == -1) state.state->start = 0;
    }

    /* initialize stream */
    gz_reset(state);

    /* return stream */
    return state.file;
}

/* -- see zlib.h -- */
gzFile ZEXPORT gzopen(path, mode)
    const char *path;
    const char *mode;
{
    return gz_open(path, -1, mode);
}

/* -- see zlib.h -- */
gzFile ZEXPORT gzopen64(path, mode)
    const char *path;
    const char *mode;
{
    return gz_open(path, -1, mode);
}

/* -- see zlib.h -- */
gzFile ZEXPORT gzdopen(fd, mode)
    int fd;
    const char *mode;
{
    char *path;         /* identifier for error messages */
    gzFile gz;

    if (fd == -1 || (path = (char *)malloc(7 + 3 * sizeof(int))) == NULL)
        return NULL;
#if !defined(NO_snprintf) && !defined(NO_vsnprintf)
    (void)snprintf(path, 7 + 3 * sizeof(int), "<fd:%d>", fd);
#else
    sprintf(path, "<fd:%d>", fd);   /* for debugging */
#endif
    gz = gz_open(path, fd, mode);
    free(path);
    return gz;
}

/* -- see zlib.h -- */
#ifdef WIDECHAR
gzFile ZEXPORT gzopen_w(path, mode)
    const wchar_t *path;
    const char *mode;
{
    return gz_open(path, -2, mode);
}
#endif

/* -- see zlib.h -- */
int ZEXPORT gzbuffer(file, size)
    gzFile file;
    unsigned size;
{
    gz_statep state;

    /* get internal structure and check integrity */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;
    if (state.state->mode != GZ_READ && state.state->mode != GZ_WRITE)
        return -1;

    /* make sure we haven't already allocated memory */
    if (state.state->size != 0)
        return -1;

    /* check and set requested size */
    if ((size << 1) < size)
        return -1;              /* need to be able to double it */
    if (size < 2)
        size = 2;               /* need two bytes to check magic header */
    state.state->want = size;
    return 0;
}

/* -- see zlib.h -- */
int ZEXPORT gzrewind(file)
    gzFile file;
{
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;

    /* check that we're reading and that there's no error */
    if (state.state->mode != GZ_READ ||
            (state.state->err != Z_OK && state.state->err != Z_BUF_ERROR))
        return -1;

    /* back up and start over */
    if (LSEEK(state.state->fd, state.state->start, SEEK_SET) == -1)
        return -1;
    gz_reset(state);
    return 0;
}

/* -- see zlib.h -- */
z_off64_t ZEXPORT gzseek64(file, offset, whence)
    gzFile file;
    z_off64_t offset;
    int whence;
{
    unsigned n;
    z_off64_t ret;
    gz_statep state;

    /* get internal structure and check integrity */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;
    if (state.state->mode != GZ_READ && state.state->mode != GZ_WRITE)
        return -1;

    /* check that there's no error */
    if (state.state->err != Z_OK && state.state->err != Z_BUF_ERROR)
        return -1;

    /* can only seek from start or relative to current position */
    if (whence != SEEK_SET && whence != SEEK_CUR)
        return -1;

    /* normalize offset to a SEEK_CUR specification */
    if (whence == SEEK_SET)
        offset -= state.state->x.pos;
    else if (state.state->seek)
        offset += state.state->skip;
    state.state->seek = 0;

    /* if within raw area while reading, just go there */
    if (state.state->mode == GZ_READ && state.state->how == COPY &&
            state.state->x.pos + offset >= 0) {
        ret = LSEEK(state.state->fd, offset - state.state->x.have, SEEK_CUR);
        if (ret == -1)
            return -1;
        state.state->x.have = 0;
        state.state->eof = 0;
        state.state->past = 0;
        state.state->seek = 0;
        gz_error(state, Z_OK, NULL);
        state.state->strm.avail_in = 0;
        state.state->x.pos += offset;
        return state.state->x.pos;
    }

    /* calculate skip amount, rewinding if needed for back seek when reading */
    if (offset < 0) {
        if (state.state->mode != GZ_READ)         /* writing -- can't go backwards */
            return -1;
        offset += state.state->x.pos;
        if (offset < 0)                     /* before start of file! */
            return -1;
        if (gzrewind(file) == -1)           /* rewind, then skip to offset */
            return -1;
    }

    /* if reading, skip what's in output buffer (one less gzgetc() check) */
    if (state.state->mode == GZ_READ) {
        n = GT_OFF(state.state->x.have) || (z_off64_t)state.state->x.have > offset ?
            (unsigned)offset : state.state->x.have;
        state.state->x.have -= n;
        state.state->x.next += n;
        state.state->x.pos += n;
        offset -= n;
    }

    /* request skip (if not zero) */
    if (offset) {
        state.state->seek = 1;
        state.state->skip = offset;
    }
    return state.state->x.pos + offset;
}

/* -- see zlib.h -- */
z_off_t ZEXPORT gzseek(file, offset, whence)
    gzFile file;
    z_off_t offset;
    int whence;
{
    z_off64_t ret;

    ret = gzseek64(file, (z_off64_t)offset, whence);
    return ret == (z_off_t)ret ? (z_off_t)ret : -1;
}

/* -- see zlib.h -- */
z_off64_t ZEXPORT gztell64(file)
    gzFile file;
{
    gz_statep state;

    /* get internal structure and check integrity */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;
    if (state.state->mode != GZ_READ && state.state->mode != GZ_WRITE)
        return -1;

    /* return position */
    return state.state->x.pos + (state.state->seek ? state.state->skip : 0);
}

/* -- see zlib.h -- */
z_off_t ZEXPORT gztell(file)
    gzFile file;
{
    z_off64_t ret;

    ret = gztell64(file);
    return ret == (z_off_t)ret ? (z_off_t)ret : -1;
}

/* -- see zlib.h -- */
z_off64_t ZEXPORT gzoffset64(file)
    gzFile file;
{
    z_off64_t offset;
    gz_statep state;

    /* get internal structure and check integrity */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;
    if (state.state->mode != GZ_READ && state.state->mode != GZ_WRITE)
        return -1;

    /* compute and return effective offset in file */
    offset = LSEEK(state.state->fd, 0, SEEK_CUR);
    if (offset == -1)
        return -1;
    if (state.state->mode == GZ_READ)             /* reading */
        offset -= state.state->strm.avail_in;     /* don't count buffered input */
    return offset;
}

/* -- see zlib.h -- */
z_off_t ZEXPORT gzoffset(file)
    gzFile file;
{
    z_off64_t ret;

    ret = gzoffset64(file);
    return ret == (z_off_t)ret ? (z_off_t)ret : -1;
}

/* -- see zlib.h -- */
int ZEXPORT gzeof(file)
    gzFile file;
{
    gz_statep state;

    /* get internal structure and check integrity */
    if (file == NULL)
        return 0;
    state = (gz_statep)file;
    if (state.state->mode != GZ_READ && state.state->mode != GZ_WRITE)
        return 0;

    /* return end-of-file state */
    return state.state->mode == GZ_READ ? state.state->past : 0;
}

/* -- see zlib.h -- */
const char * ZEXPORT gzerror(file, errnum)
    gzFile file;
    int *errnum;
{
    gz_statep state;

    /* get internal structure and check integrity */
    if (file == NULL)
        return NULL;
    state = (gz_statep)file;
    if (state.state->mode != GZ_READ && state.state->mode != GZ_WRITE)
        return NULL;

    /* return error information */
    if (errnum != NULL)
        *errnum = state.state->err;
    return state.state->err == Z_MEM_ERROR ? "out of memory" :
                                       (state.state->msg == NULL ? "" : state.state->msg);
}

/* -- see zlib.h -- */
void ZEXPORT gzclearerr(file)
    gzFile file;
{
    gz_statep state;

    /* get internal structure and check integrity */
    if (file == NULL)
        return;
    state = (gz_statep)file;
    if (state.state->mode != GZ_READ && state.state->mode != GZ_WRITE)
        return;

    /* clear error and end-of-file */
    if (state.state->mode == GZ_READ) {
        state.state->eof = 0;
        state.state->past = 0;
    }
    gz_error(state, Z_OK, NULL);
}

/* Create an error message in allocated memory and set state.state->err and
   state.state->msg accordingly.  Free any previous error message already there.  Do
   not try to free or allocate space if the error is Z_MEM_ERROR (out of
   memory).  Simply save the error message as a static string.  If there is an
   allocation failure constructing the error message, then convert the error to
   out of memory. */
void ZLIB_INTERNAL gz_error(state, err, msg)
    gz_statep state;
    int err;
    const char *msg;
{
    /* free previously allocated message and clear */
    if (state.state->msg != NULL) {
        if (state.state->err != Z_MEM_ERROR)
            free(state.state->msg);
        state.state->msg = NULL;
    }

    /* if fatal, set state.state->x.have to 0 so that the gzgetc() macro fails */
    if (err != Z_OK && err != Z_BUF_ERROR)
        state.state->x.have = 0;

    /* set error code, and if no message, then done */
    state.state->err = err;
    if (msg == NULL)
        return;

    /* for an out of memory error, return literal string when requested */
    if (err == Z_MEM_ERROR)
        return;

    /* construct error message with path */
    if ((state.state->msg = (char *)malloc(strlen(state.state->path) + strlen(msg) + 3)) ==
            NULL) {
        state.state->err = Z_MEM_ERROR;
        return;
    }
#if !defined(NO_snprintf) && !defined(NO_vsnprintf)
    (void)snprintf(state.state->msg, strlen(state.state->path) + strlen(msg) + 3,
                   "%s%s%s", state.state->path, ": ", msg);
#else
    strcpy(state.state->msg, state.state->path);
    strcat(state.state->msg, ": ");
    strcat(state.state->msg, msg);
#endif
}

#ifndef INT_MAX
/* portably return maximum value for an int (when limits.h presumed not
   available) -- we need to do this to cover cases where 2's complement not
   used, since C standard permits 1's complement and sign-bit representations,
   otherwise we could just use ((unsigned)-1) >> 1 */
unsigned ZLIB_INTERNAL gz_intmax()
{
    unsigned p, q;

    p = 1;
    do {
        q = p;
        p <<= 1;
        p++;
    } while (p > q);
    return q >> 1;
}
#endif
