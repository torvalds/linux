/* gzwrite.c contains minimal changes required to be compiled with zlibWrapper:
 * - gz_statep was converted to union to work with -Wstrict-aliasing=1      */

 /* gzwrite.c -- zlib functions for writing gzip files
 * Copyright (C) 2004-2017 Mark Adler
 * For conditions of distribution and use, see http://www.zlib.net/zlib_license.html
 */

#include <assert.h>

#include "gzguts.h"

/* Local functions */
local int gz_init OF((gz_statep));
local int gz_comp OF((gz_statep, int));
local int gz_zero OF((gz_statep, z_off64_t));
local z_size_t gz_write OF((gz_statep, voidpc, z_size_t));

/* Initialize state for writing a gzip file.  Mark initialization by setting
   state.state->size to non-zero.  Return -1 on a memory allocation failure, or 0 on
   success. */
local int gz_init(state)
    gz_statep state;
{
    int ret;
    z_streamp strm = &(state.state->strm);

    /* allocate input buffer (double size for gzprintf) */
    state.state->in = (unsigned char*)malloc(state.state->want << 1);
    if (state.state->in == NULL) {
        gz_error(state, Z_MEM_ERROR, "out of memory");
        return -1;
    }

    /* only need output buffer and deflate state if compressing */
    if (!state.state->direct) {
        /* allocate output buffer */
        state.state->out = (unsigned char*)malloc(state.state->want);
        if (state.state->out == NULL) {
            free(state.state->in);
            gz_error(state, Z_MEM_ERROR, "out of memory");
            return -1;
        }

        /* allocate deflate memory, set up for gzip compression */
        strm->zalloc = Z_NULL;
        strm->zfree = Z_NULL;
        strm->opaque = Z_NULL;
        ret = deflateInit2(strm, state.state->level, Z_DEFLATED,
                           MAX_WBITS + 16, DEF_MEM_LEVEL, state.state->strategy);
        if (ret != Z_OK) {
            free(state.state->out);
            free(state.state->in);
            gz_error(state, Z_MEM_ERROR, "out of memory");
            return -1;
        }
        strm->next_in = NULL;
    }

    /* mark state as initialized */
    state.state->size = state.state->want;

    /* initialize write buffer if compressing */
    if (!state.state->direct) {
        strm->avail_out = state.state->size;
        strm->next_out = state.state->out;
        state.state->x.next = strm->next_out;
    }
    return 0;
}

/* Compress whatever is at avail_in and next_in and write to the output file.
   Return -1 if there is an error writing to the output file or if gz_init()
   fails to allocate memory, otherwise 0.  flush is assumed to be a valid
   deflate() flush value.  If flush is Z_FINISH, then the deflate() state is
   reset to start a new gzip stream.  If gz->direct is true, then simply write
   to the output file without compressing, and ignore flush. */
local int gz_comp(state, flush)
    gz_statep state;
    int flush;
{
    int ret, writ;
    unsigned have, put, max = ((unsigned)-1 >> 2) + 1;
    z_streamp strm = &(state.state->strm);

    /* allocate memory if this is the first time through */
    if (state.state->size == 0 && gz_init(state) == -1)
        return -1;

    /* write directly if requested */
    if (state.state->direct) {
        while (strm->avail_in) {
            put = strm->avail_in > max ? max : strm->avail_in;
            writ = (int)write(state.state->fd, strm->next_in, put);
            if (writ < 0) {
                gz_error(state, Z_ERRNO, zstrerror());
                return -1;
            }
            strm->avail_in -= (unsigned)writ;
            strm->next_in += writ;
        }
        return 0;
    }

    /* run deflate() on provided input until it produces no more output */
    ret = Z_OK;
    do {
        /* write out current buffer contents if full, or if flushing, but if
           doing Z_FINISH then don't write until we get to Z_STREAM_END */
        if (strm->avail_out == 0 || (flush != Z_NO_FLUSH &&
            (flush != Z_FINISH || ret == Z_STREAM_END))) {
            while (strm->next_out > state.state->x.next) {
                put = strm->next_out - state.state->x.next > (int)max ? max :
                      (unsigned)(strm->next_out - state.state->x.next);
                writ = (int)write(state.state->fd, state.state->x.next, put);
                if (writ < 0) {
                    gz_error(state, Z_ERRNO, zstrerror());
                    return -1;
                }
                state.state->x.next += writ;
            }
            if (strm->avail_out == 0) {
                strm->avail_out = state.state->size;
                strm->next_out = state.state->out;
                state.state->x.next = state.state->out;
            }
        }

        /* compress */
        have = strm->avail_out;
        ret = deflate(strm, flush);
        if (ret == Z_STREAM_ERROR) {
            gz_error(state, Z_STREAM_ERROR,
                      "internal error: deflate stream corrupt");
            return -1;
        }
        have -= strm->avail_out;
    } while (have);

    /* if that completed a deflate stream, allow another to start */
    if (flush == Z_FINISH)
        deflateReset(strm);

    /* all done, no errors */
    return 0;
}

/* Compress len zeros to output.  Return -1 on a write error or memory
   allocation failure by gz_comp(), or 0 on success. */
local int gz_zero(state, len)
    gz_statep state;
    z_off64_t len;
{
    int first;
    unsigned n;
    z_streamp strm = &(state.state->strm);

    /* consume whatever's left in the input buffer */
    if (strm->avail_in && gz_comp(state, Z_NO_FLUSH) == -1)
        return -1;

    /* compress len zeros (len guaranteed > 0) */
    first = 1;
    while (len) {
        n = GT_OFF(state.state->size) || (z_off64_t)state.state->size > len ?
            (unsigned)len : state.state->size;
        if (first) {
            memset(state.state->in, 0, n);
            first = 0;
        }
        strm->avail_in = n;
        strm->next_in = state.state->in;
        state.state->x.pos += n;
        if (gz_comp(state, Z_NO_FLUSH) == -1)
            return -1;
        len -= n;
    }
    return 0;
}

/* Write len bytes from buf to file.  Return the number of bytes written.  If
   the returned value is less than len, then there was an error. */
local z_size_t gz_write(state, buf, len)
    gz_statep state;
    voidpc buf;
    z_size_t len;
{
    z_size_t put = len;

    /* if len is zero, avoid unnecessary operations */
    if (len == 0)
        return 0;

    /* allocate memory if this is the first time through */
    if (state.state->size == 0 && gz_init(state) == -1)
        return 0;

    /* check for seek request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_zero(state, state.state->skip) == -1)
            return 0;
    }

    /* for small len, copy to input buffer, otherwise compress directly */
    if (len < state.state->size) {
        /* copy to input buffer, compress when full */
        do {
            z_size_t have, copy;

            if (state.state->strm.avail_in == 0)
                state.state->strm.next_in = state.state->in;
            have = (unsigned)((state.state->strm.next_in + state.state->strm.avail_in) -
                              state.state->in);
            copy = state.state->size - have;
            if (copy > len)
                copy = len;
            memcpy(state.state->in + have, buf, copy);
            state.state->strm.avail_in += copy;
            state.state->x.pos += copy;
            buf = (const char *)buf + copy;
            len -= copy;
            if (len && gz_comp(state, Z_NO_FLUSH) == -1)
                return 0;
        } while (len);
    }
    else {
        /* consume whatever's left in the input buffer */
        if (state.state->strm.avail_in && gz_comp(state, Z_NO_FLUSH) == -1)
            return 0;

        /* directly compress user buffer to file */
        state.state->strm.next_in = (z_const Bytef *)buf;
        do {
            z_size_t n = (unsigned)-1;
            if (n > len)
                n = len;
            state.state->strm.avail_in = (z_uInt)n;
            state.state->x.pos += n;
            if (gz_comp(state, Z_NO_FLUSH) == -1)
                return 0;
            len -= n;
        } while (len);
    }

    /* input was all buffered or compressed */
    return put;
}

/* -- see zlib.h -- */
int ZEXPORT gzwrite(file, buf, len)
    gzFile file;
    voidpc buf;
    unsigned len;
{
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return 0;
    state = (gz_statep)file;

    /* check that we're writing and that there's no error */
    if (state.state->mode != GZ_WRITE || state.state->err != Z_OK)
        return 0;

    /* since an int is returned, make sure len fits in one, otherwise return
       with an error (this avoids a flaw in the interface) */
    if ((int)len < 0) {
        gz_error(state, Z_DATA_ERROR, "requested length does not fit in int");
        return 0;
    }

    /* write len bytes from buf (the return value will fit in an int) */
    return (int)gz_write(state, buf, len);
}

/* -- see zlib.h -- */
z_size_t ZEXPORT gzfwrite(buf, size, nitems, file)
    voidpc buf;
    z_size_t size;
    z_size_t nitems;
    gzFile file;
{
    z_size_t len;
    gz_statep state;

    /* get internal structure */
    assert(size != 0);
    if (file == NULL)
        return 0;
    state = (gz_statep)file;

    /* check that we're writing and that there's no error */
    if (state.state->mode != GZ_WRITE || state.state->err != Z_OK)
        return 0;

    /* compute bytes to read -- error on overflow */
    len = nitems * size;
    if (size && (len / size != nitems)) {
        gz_error(state, Z_STREAM_ERROR, "request does not fit in a size_t");
        return 0;
    }

    /* write len bytes to buf, return the number of full items written */
    return len ? gz_write(state, buf, len) / size : 0;
}

/* -- see zlib.h -- */
int ZEXPORT gzputc(file, c)
    gzFile file;
    int c;
{
    unsigned have;
    unsigned char buf[1];
    gz_statep state;
    z_streamp strm;

    /* get internal structure */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;
    strm = &(state.state->strm);

    /* check that we're writing and that there's no error */
    if (state.state->mode != GZ_WRITE || state.state->err != Z_OK)
        return -1;

    /* check for seek request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_zero(state, state.state->skip) == -1)
            return -1;
    }

    /* try writing to input buffer for speed (state.state->size == 0 if buffer not
       initialized) */
    if (state.state->size) {
        if (strm->avail_in == 0)
            strm->next_in = state.state->in;
        have = (unsigned)((strm->next_in + strm->avail_in) - state.state->in);
        if (have < state.state->size) {
            state.state->in[have] = (unsigned char)c;
            strm->avail_in++;
            state.state->x.pos++;
            return c & 0xff;
        }
    }

    /* no room in buffer or not initialized, use gz_write() */
    buf[0] = (unsigned char)c;
    if (gz_write(state, buf, 1) != 1)
        return -1;
    return c & 0xff;
}

/* -- see zlib.h -- */
int ZEXPORT gzputs(file, str)
    gzFile file;
    const char *str;
{
    int ret;
    z_size_t len;
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;

    /* check that we're writing and that there's no error */
    if (state.state->mode != GZ_WRITE || state.state->err != Z_OK)
        return -1;

    /* write string */
    len = strlen(str);
    ret = (int)gz_write(state, str, len);
    return ret == 0 && len != 0 ? -1 : ret;
}

#if defined(STDC) || defined(Z_HAVE_STDARG_H)
#include <stdarg.h>

/* -- see zlib.h -- */
int ZEXPORTVA gzvprintf(gzFile file, const char *format, va_list va)
{
    int len;
    unsigned left;
    char *next;
    gz_statep state;
    z_streamp strm;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_statep)file;
    strm = &(state.state->strm);

    /* check that we're writing and that there's no error */
    if (state.state->mode != GZ_WRITE || state.state->err != Z_OK)
        return Z_STREAM_ERROR;

    /* make sure we have some buffer space */
    if (state.state->size == 0 && gz_init(state) == -1)
        return state.state->err;

    /* check for seek request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_zero(state, state.state->skip) == -1)
            return state.state->err;
    }

    /* do the printf() into the input buffer, put length in len -- the input
       buffer is double-sized just for this function, so there is guaranteed to
       be state.state->size bytes available after the current contents */
    if (strm->avail_in == 0)
        strm->next_in = state.state->in;
    next = (char *)(state.state->in + (strm->next_in - state.state->in) + strm->avail_in);
    next[state.state->size - 1] = 0;
#ifdef NO_vsnprintf
#  ifdef HAS_vsprintf_void
    (void)vsprintf(next, format, va);
    for (len = 0; len < state.state->size; len++)
        if (next[len] == 0) break;
#  else
    len = vsprintf(next, format, va);
#  endif
#else
#  ifdef HAS_vsnprintf_void
    (void)vsnprintf(next, state.state->size, format, va);
    len = strlen(next);
#  else
    len = vsnprintf(next, state.state->size, format, va);
#  endif
#endif

    /* check that printf() results fit in buffer */
    if (len == 0 || (unsigned)len >= state.state->size || next[state.state->size - 1] != 0)
        return 0;

    /* update buffer and position, compress first half if past that */
    strm->avail_in += (unsigned)len;
    state.state->x.pos += len;
    if (strm->avail_in >= state.state->size) {
        left = strm->avail_in - state.state->size;
        strm->avail_in = state.state->size;
        if (gz_comp(state, Z_NO_FLUSH) == -1)
            return state.state->err;
        memcpy(state.state->in, state.state->in + state.state->size, left);
        strm->next_in = state.state->in;
        strm->avail_in = left;
    }
    return len;
}

int ZEXPORTVA gzprintf(gzFile file, const char *format, ...)
{
    va_list va;
    int ret;

    va_start(va, format);
    ret = gzvprintf(file, format, va);
    va_end(va);
    return ret;
}

#else /* !STDC && !Z_HAVE_STDARG_H */

/* -- see zlib.h -- */
int ZEXPORTVA gzprintf (file, format, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
                       a11, a12, a13, a14, a15, a16, a17, a18, a19, a20)
    gzFile file;
    const char *format;
    int a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
        a11, a12, a13, a14, a15, a16, a17, a18, a19, a20;
{
    unsigned len, left;
    char *next;
    gz_statep state;
    z_streamp strm;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_statep)file;
    strm = &(state.state->strm);

    /* check that can really pass pointer in ints */
    if (sizeof(int) != sizeof(void *))
        return Z_STREAM_ERROR;

    /* check that we're writing and that there's no error */
    if (state.state->mode != GZ_WRITE || state.state->err != Z_OK)
        return Z_STREAM_ERROR;

    /* make sure we have some buffer space */
    if (state.state->size == 0 && gz_init(state) == -1)
        return state.state->error;

    /* check for seek request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_zero(state, state.state->skip) == -1)
            return state.state->error;
    }

    /* do the printf() into the input buffer, put length in len -- the input
       buffer is double-sized just for this function, so there is guaranteed to
       be state.state->size bytes available after the current contents */
    if (strm->avail_in == 0)
        strm->next_in = state.state->in;
    next = (char *)(strm->next_in + strm->avail_in);
    next[state.state->size - 1] = 0;
#ifdef NO_snprintf
#  ifdef HAS_sprintf_void
    sprintf(next, format, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,
            a13, a14, a15, a16, a17, a18, a19, a20);
    for (len = 0; len < size; len++)
        if (next[len] == 0)
            break;
#  else
    len = sprintf(next, format, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,
                  a12, a13, a14, a15, a16, a17, a18, a19, a20);
#  endif
#else
#  ifdef HAS_snprintf_void
    snprintf(next, state.state->size, format, a1, a2, a3, a4, a5, a6, a7, a8, a9,
             a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20);
    len = strlen(next);
#  else
    len = snprintf(next, state.state->size, format, a1, a2, a3, a4, a5, a6, a7, a8,
                   a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20);
#  endif
#endif

    /* check that printf() results fit in buffer */
    if (len == 0 || len >= state.state->size || next[state.state->size - 1] != 0)
        return 0;

    /* update buffer and position, compress first half if past that */
    strm->avail_in += len;
    state.state->x.pos += len;
    if (strm->avail_in >= state.state->size) {
        left = strm->avail_in - state.state->size;
        strm->avail_in = state.state->size;
        if (gz_comp(state, Z_NO_FLUSH) == -1)
            return state.state->err;
        memcpy(state.state->in, state.state->in + state.state->size, left);
        strm->next_in = state.state->in;
        strm->avail_in = left;
    }
    return (int)len;
}

#endif

/* -- see zlib.h -- */
int ZEXPORT gzflush(file, flush)
    gzFile file;
    int flush;
{
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_statep)file;

    /* check that we're writing and that there's no error */
    if (state.state->mode != GZ_WRITE || state.state->err != Z_OK)
        return Z_STREAM_ERROR;

    /* check flush parameter */
    if (flush < 0 || flush > Z_FINISH)
        return Z_STREAM_ERROR;

    /* check for seek request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_zero(state, state.state->skip) == -1)
            return state.state->err;
    }

    /* compress remaining data with requested flush */
    (void)gz_comp(state, flush);
    return state.state->err;
}

/* -- see zlib.h -- */
int ZEXPORT gzsetparams(file, level, strategy)
    gzFile file;
    int level;
    int strategy;
{
    gz_statep state;
    z_streamp strm;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_statep)file;
    strm = &(state.state->strm);

    /* check that we're writing and that there's no error */
    if (state.state->mode != GZ_WRITE || state.state->err != Z_OK)
        return Z_STREAM_ERROR;

    /* if no change is requested, then do nothing */
    if (level == state.state->level && strategy == state.state->strategy)
        return Z_OK;

    /* check for seek request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_zero(state, state.state->skip) == -1)
            return state.state->err;
    }

    /* change compression parameters for subsequent input */
    if (state.state->size) {
        /* flush previous input with previous parameters before changing */
        if (strm->avail_in && gz_comp(state, Z_BLOCK) == -1)
            return state.state->err;
        deflateParams(strm, level, strategy);
    }
    state.state->level = level;
    state.state->strategy = strategy;
    return Z_OK;
}

/* -- see zlib.h -- */
int ZEXPORT gzclose_w(file)
    gzFile file;
{
    int ret = Z_OK;
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_statep)file;

    /* check that we're writing */
    if (state.state->mode != GZ_WRITE)
        return Z_STREAM_ERROR;

    /* check for seek request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_zero(state, state.state->skip) == -1)
            ret = state.state->err;
    }

    /* flush, free memory, and close file */
    if (gz_comp(state, Z_FINISH) == -1)
        ret = state.state->err;
    if (state.state->size) {
        if (!state.state->direct) {
            (void)deflateEnd(&(state.state->strm));
            free(state.state->out);
        }
        free(state.state->in);
    }
    gz_error(state, Z_OK, NULL);
    free(state.state->path);
    if (close(state.state->fd) == -1)
        ret = Z_ERRNO;
    free(state.state);
    return ret;
}
