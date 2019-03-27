/* gzread.c contains minimal changes required to be compiled with zlibWrapper:
 * - gz_statep was converted to union to work with -Wstrict-aliasing=1      */

 /* gzread.c -- zlib functions for reading gzip files
 * Copyright (C) 2004, 2005, 2010, 2011, 2012, 2013, 2016 Mark Adler
 * For conditions of distribution and use, see http://www.zlib.net/zlib_license.html
 */

#include "gzguts.h"

/* Local functions */
local int gz_load OF((gz_statep, unsigned char *, unsigned, unsigned *));
local int gz_avail OF((gz_statep));
local int gz_look OF((gz_statep));
local int gz_decomp OF((gz_statep));
local int gz_fetch OF((gz_statep));
local int gz_skip OF((gz_statep, z_off64_t));
local z_size_t gz_read OF((gz_statep, voidp, z_size_t));

/* Use read() to load a buffer -- return -1 on error, otherwise 0.  Read from
   state.state->fd, and update state.state->eof, state.state->err, and state.state->msg as appropriate.
   This function needs to loop on read(), since read() is not guaranteed to
   read the number of bytes requested, depending on the type of descriptor. */
local int gz_load(state, buf, len, have)
    gz_statep state;
    unsigned char *buf;
    unsigned len;
    unsigned *have;
{
    ssize_t ret;
    unsigned get, max = ((unsigned)-1 >> 2) + 1;

    *have = 0;
    do {
        get = len - *have;
        if (get > max)
            get = max;
        ret = read(state.state->fd, buf + *have, get);
        if (ret <= 0)
            break;
        *have += (unsigned)ret;
    } while (*have < len);
    if (ret < 0) {
        gz_error(state, Z_ERRNO, zstrerror());
        return -1;
    }
    if (ret == 0)
        state.state->eof = 1;
    return 0;
}

/* Load up input buffer and set eof flag if last data loaded -- return -1 on
   error, 0 otherwise.  Note that the eof flag is set when the end of the input
   file is reached, even though there may be unused data in the buffer.  Once
   that data has been used, no more attempts will be made to read the file.
   If strm->avail_in != 0, then the current data is moved to the beginning of
   the input buffer, and then the remainder of the buffer is loaded with the
   available data from the input file. */
local int gz_avail(state)
    gz_statep state;
{
    unsigned got;
    z_streamp strm = &(state.state->strm);

    if (state.state->err != Z_OK && state.state->err != Z_BUF_ERROR)
        return -1;
    if (state.state->eof == 0) {
        if (strm->avail_in) {       /* copy what's there to the start */
            unsigned char *p = state.state->in;
            unsigned const char *q = strm->next_in;
            unsigned n = strm->avail_in;
            do {
                *p++ = *q++;
            } while (--n);
        }
        if (gz_load(state, state.state->in + strm->avail_in,
                    state.state->size - strm->avail_in, &got) == -1)
            return -1;
        strm->avail_in += got;
        strm->next_in = state.state->in;
    }
    return 0;
}

/* Look for gzip header, set up for inflate or copy.  state.state->x.have must be 0.
   If this is the first time in, allocate required memory.  state.state->how will be
   left unchanged if there is no more input data available, will be set to COPY
   if there is no gzip header and direct copying will be performed, or it will
   be set to GZIP for decompression.  If direct copying, then leftover input
   data from the input buffer will be copied to the output buffer.  In that
   case, all further file reads will be directly to either the output buffer or
   a user buffer.  If decompressing, the inflate state will be initialized.
   gz_look() will return 0 on success or -1 on failure. */
local int gz_look(state)
    gz_statep state;
{
    z_streamp strm = &(state.state->strm);

    /* allocate read buffers and inflate memory */
    if (state.state->size == 0) {
        /* allocate buffers */
        state.state->in = (unsigned char *)malloc(state.state->want);
        state.state->out = (unsigned char *)malloc(state.state->want << 1);
        if (state.state->in == NULL || state.state->out == NULL) {
            free(state.state->out);
            free(state.state->in);
            gz_error(state, Z_MEM_ERROR, "out of memory");
            return -1;
        }
        state.state->size = state.state->want;

        /* allocate inflate memory */
        state.state->strm.zalloc = Z_NULL;
        state.state->strm.zfree = Z_NULL;
        state.state->strm.opaque = Z_NULL;
        state.state->strm.avail_in = 0;
        state.state->strm.next_in = Z_NULL;
        if (inflateInit2(&(state.state->strm), 15 + 16) != Z_OK) {    /* gunzip */
            free(state.state->out);
            free(state.state->in);
            state.state->size = 0;
            gz_error(state, Z_MEM_ERROR, "out of memory");
            return -1;
        }
    }

    /* get at least the magic bytes in the input buffer */
    if (strm->avail_in < 2) {
        if (gz_avail(state) == -1)
            return -1;
        if (strm->avail_in == 0)
            return 0;
    }

    /* look for gzip magic bytes -- if there, do gzip decoding (note: there is
       a logical dilemma here when considering the case of a partially written
       gzip file, to wit, if a single 31 byte is written, then we cannot tell
       whether this is a single-byte file, or just a partially written gzip
       file -- for here we assume that if a gzip file is being written, then
       the header will be written in a single operation, so that reading a
       single byte is sufficient indication that it is not a gzip file) */
    if (strm->avail_in > 1 &&
            ((strm->next_in[0] == 31 && strm->next_in[1] == 139) /* gz header */
            || (strm->next_in[0] == 40 && strm->next_in[1] == 181))) { /* zstd header */
        inflateReset(strm);
        state.state->how = GZIP;
        state.state->direct = 0;
        return 0;
    }

    /* no gzip header -- if we were decoding gzip before, then this is trailing
       garbage.  Ignore the trailing garbage and finish. */
    if (state.state->direct == 0) {
        strm->avail_in = 0;
        state.state->eof = 1;
        state.state->x.have = 0;
        return 0;
    }

    /* doing raw i/o, copy any leftover input to output -- this assumes that
       the output buffer is larger than the input buffer, which also assures
       space for gzungetc() */
    state.state->x.next = state.state->out;
    if (strm->avail_in) {
        memcpy(state.state->x.next, strm->next_in, strm->avail_in);
        state.state->x.have = strm->avail_in;
        strm->avail_in = 0;
    }
    state.state->how = COPY;
    state.state->direct = 1;
    return 0;
}

/* Decompress from input to the provided next_out and avail_out in the state.
   On return, state.state->x.have and state.state->x.next point to the just decompressed
   data.  If the gzip stream completes, state.state->how is reset to LOOK to look for
   the next gzip stream or raw data, once state.state->x.have is depleted.  Returns 0
   on success, -1 on failure. */
local int gz_decomp(state)
    gz_statep state;
{
    int ret = Z_OK;
    unsigned had;
    z_streamp strm = &(state.state->strm);

    /* fill output buffer up to end of deflate stream */
    had = strm->avail_out;
    do {
        /* get more input for inflate() */
        if (strm->avail_in == 0 && gz_avail(state) == -1)
            return -1;
        if (strm->avail_in == 0) {
            gz_error(state, Z_BUF_ERROR, "unexpected end of file");
            break;
        }

        /* decompress and handle errors */
        ret = inflate(strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT) {
            gz_error(state, Z_STREAM_ERROR,
                     "internal error: inflate stream corrupt");
            return -1;
        }
        if (ret == Z_MEM_ERROR) {
            gz_error(state, Z_MEM_ERROR, "out of memory");
            return -1;
        }
        if (ret == Z_DATA_ERROR) {              /* deflate stream invalid */
            gz_error(state, Z_DATA_ERROR,
                     strm->msg == NULL ? "compressed data error" : strm->msg);
            return -1;
        }
    } while (strm->avail_out && ret != Z_STREAM_END);

    /* update available output */
    state.state->x.have = had - strm->avail_out;
    state.state->x.next = strm->next_out - state.state->x.have;

    /* if the gzip stream completed successfully, look for another */
    if (ret == Z_STREAM_END)
        state.state->how = LOOK;

    /* good decompression */
    return 0;
}

/* Fetch data and put it in the output buffer.  Assumes state.state->x.have is 0.
   Data is either copied from the input file or decompressed from the input
   file depending on state.state->how.  If state.state->how is LOOK, then a gzip header is
   looked for to determine whether to copy or decompress.  Returns -1 on error,
   otherwise 0.  gz_fetch() will leave state.state->how as COPY or GZIP unless the
   end of the input file has been reached and all data has been processed.  */
local int gz_fetch(state)
    gz_statep state;
{
    z_streamp strm = &(state.state->strm);

    do {
        switch(state.state->how) {
        case LOOK:      /* -> LOOK, COPY (only if never GZIP), or GZIP */
            if (gz_look(state) == -1)
                return -1;
            if (state.state->how == LOOK)
                return 0;
            break;
        case COPY:      /* -> COPY */
            if (gz_load(state, state.state->out, state.state->size << 1, &(state.state->x.have))
                    == -1)
                return -1;
            state.state->x.next = state.state->out;
            return 0;
        case GZIP:      /* -> GZIP or LOOK (if end of gzip stream) */
            strm->avail_out = state.state->size << 1;
            strm->next_out = state.state->out;
            if (gz_decomp(state) == -1)
                return -1;
        }
    } while (state.state->x.have == 0 && (!state.state->eof || strm->avail_in));
    return 0;
}

/* Skip len uncompressed bytes of output.  Return -1 on error, 0 on success. */
local int gz_skip(state, len)
    gz_statep state;
    z_off64_t len;
{
    unsigned n;

    /* skip over len bytes or reach end-of-file, whichever comes first */
    while (len)
        /* skip over whatever is in output buffer */
        if (state.state->x.have) {
            n = GT_OFF(state.state->x.have) || (z_off64_t)state.state->x.have > len ?
                (unsigned)len : state.state->x.have;
            state.state->x.have -= n;
            state.state->x.next += n;
            state.state->x.pos += n;
            len -= n;
        }

        /* output buffer empty -- return if we're at the end of the input */
        else if (state.state->eof && state.state->strm.avail_in == 0)
            break;

        /* need more data to skip -- load up output buffer */
        else {
            /* get more output, looking for header if required */
            if (gz_fetch(state) == -1)
                return -1;
        }
    return 0;
}

/* Read len bytes into buf from file, or less than len up to the end of the
   input.  Return the number of bytes read.  If zero is returned, either the
   end of file was reached, or there was an error.  state.state->err must be
   consulted in that case to determine which. */
local z_size_t gz_read(state, buf, len)
    gz_statep state;
    voidp buf;
    z_size_t len;
{
    z_size_t got;
    unsigned n;

    /* if len is zero, avoid unnecessary operations */
    if (len == 0)
        return 0;

    /* process a skip request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_skip(state, state.state->skip) == -1)
            return 0;
    }

    /* get len bytes to buf, or less than len if at the end */
    got = 0;
    do {
        /* set n to the maximum amount of len that fits in an unsigned int */
        n = -1;
        if (n > len)
            n = (unsigned)len;

        /* first just try copying data from the output buffer */
        if (state.state->x.have) {
            if (state.state->x.have < n)
                n = state.state->x.have;
            memcpy(buf, state.state->x.next, n);
            state.state->x.next += n;
            state.state->x.have -= n;
        }

        /* output buffer empty -- return if we're at the end of the input */
        else if (state.state->eof && state.state->strm.avail_in == 0) {
            state.state->past = 1;        /* tried to read past end */
            break;
        }

        /* need output data -- for small len or new stream load up our output
           buffer */
        else if (state.state->how == LOOK || n < (state.state->size << 1)) {
            /* get more output, looking for header if required */
            if (gz_fetch(state) == -1)
                return 0;
            continue;       /* no progress yet -- go back to copy above */
            /* the copy above assures that we will leave with space in the
               output buffer, allowing at least one gzungetc() to succeed */
        }

        /* large len -- read directly into user buffer */
        else if (state.state->how == COPY) {      /* read directly */
            if (gz_load(state, (unsigned char *)buf, n, &n) == -1)
                return 0;
        }

        /* large len -- decompress directly into user buffer */
        else {  /* state.state->how == GZIP */
            state.state->strm.avail_out = n;
            state.state->strm.next_out = (unsigned char *)buf;
            if (gz_decomp(state) == -1)
                return 0;
            n = state.state->x.have;
            state.state->x.have = 0;
        }

        /* update progress */
        len -= n;
        buf = (char *)buf + n;
        got += n;
        state.state->x.pos += n;
    } while (len);

    /* return number of bytes read into user buffer */
    return got;
}

/* -- see zlib.h -- */
int ZEXPORT gzread(file, buf, len)
    gzFile file;
    voidp buf;
    unsigned len;
{
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;

    /* check that we're reading and that there's no (serious) error */
    if (state.state->mode != GZ_READ ||
            (state.state->err != Z_OK && state.state->err != Z_BUF_ERROR))
        return -1;

    /* since an int is returned, make sure len fits in one, otherwise return
       with an error (this avoids a flaw in the interface) */
    if ((int)len < 0) {
        gz_error(state, Z_STREAM_ERROR, "request does not fit in an int");
        return -1;
    }

    /* read len or fewer bytes to buf */
    len = (unsigned)gz_read(state, buf, len);

    /* check for an error */
    if (len == 0 && state.state->err != Z_OK && state.state->err != Z_BUF_ERROR)
        return -1;

    /* return the number of bytes read (this is assured to fit in an int) */
    return (int)len;
}

/* -- see zlib.h -- */
z_size_t ZEXPORT gzfread(buf, size, nitems, file)
    voidp buf;
    z_size_t size;
    z_size_t nitems;
    gzFile file;
{
    z_size_t len;
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return 0;
    state = (gz_statep)file;

    /* check that we're reading and that there's no (serious) error */
    if (state.state->mode != GZ_READ ||
            (state.state->err != Z_OK && state.state->err != Z_BUF_ERROR))
        return 0;

    /* compute bytes to read -- error on overflow */
    len = nitems * size;
    if (size && len / size != nitems) {
        gz_error(state, Z_STREAM_ERROR, "request does not fit in a size_t");
        return 0;
    }

    /* read len or fewer bytes to buf, return the number of full items read */
    return len ? gz_read(state, buf, len) / size : 0;
}

/* -- see zlib.h -- */
#if ZLIB_VERNUM >= 0x1261
#ifdef Z_PREFIX_SET
#  undef z_gzgetc
#else
#  undef gzgetc
#endif
#endif

#if ZLIB_VERNUM == 0x1260
#  undef gzgetc
#endif

#if ZLIB_VERNUM <= 0x1250
ZEXTERN int ZEXPORT gzgetc OF((gzFile file));
ZEXTERN int ZEXPORT gzgetc_ OF((gzFile file));
#endif

int ZEXPORT gzgetc(file)
    gzFile file;
{
    int ret;
    unsigned char buf[1];
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;

    /* check that we're reading and that there's no (serious) error */
    if (state.state->mode != GZ_READ ||
        (state.state->err != Z_OK && state.state->err != Z_BUF_ERROR))
        return -1;

    /* try output buffer (no need to check for skip request) */
    if (state.state->x.have) {
        state.state->x.have--;
        state.state->x.pos++;
        return *(state.state->x.next)++;
    }

    /* nothing there -- try gz_read() */
    ret = (unsigned)gz_read(state, buf, 1);
    return ret < 1 ? -1 : buf[0];
}

int ZEXPORT gzgetc_(file)
gzFile file;
{
    return gzgetc(file);
}

/* -- see zlib.h -- */
int ZEXPORT gzungetc(c, file)
    int c;
    gzFile file;
{
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return -1;
    state = (gz_statep)file;

    /* check that we're reading and that there's no (serious) error */
    if (state.state->mode != GZ_READ ||
        (state.state->err != Z_OK && state.state->err != Z_BUF_ERROR))
        return -1;

    /* process a skip request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_skip(state, state.state->skip) == -1)
            return -1;
    }

    /* can't push EOF */
    if (c < 0)
        return -1;

    /* if output buffer empty, put byte at end (allows more pushing) */
    if (state.state->x.have == 0) {
        state.state->x.have = 1;
        state.state->x.next = state.state->out + (state.state->size << 1) - 1;
        state.state->x.next[0] = (unsigned char)c;
        state.state->x.pos--;
        state.state->past = 0;
        return c;
    }

    /* if no room, give up (must have already done a gzungetc()) */
    if (state.state->x.have == (state.state->size << 1)) {
        gz_error(state, Z_DATA_ERROR, "out of room to push characters");
        return -1;
    }

    /* slide output data if needed and insert byte before existing data */
    if (state.state->x.next == state.state->out) {
        unsigned char *src = state.state->out + state.state->x.have;
        unsigned char *dest = state.state->out + (state.state->size << 1);
        while (src > state.state->out)
            *--dest = *--src;
        state.state->x.next = dest;
    }
    state.state->x.have++;
    state.state->x.next--;
    state.state->x.next[0] = (unsigned char)c;
    state.state->x.pos--;
    state.state->past = 0;
    return c;
}

/* -- see zlib.h -- */
char * ZEXPORT gzgets(file, buf, len)
    gzFile file;
    char *buf;
    int len;
{
    unsigned left, n;
    char *str;
    unsigned char *eol;
    gz_statep state;

    /* check parameters and get internal structure */
    if (file == NULL || buf == NULL || len < 1)
        return NULL;
    state = (gz_statep)file;

    /* check that we're reading and that there's no (serious) error */
    if (state.state->mode != GZ_READ ||
        (state.state->err != Z_OK && state.state->err != Z_BUF_ERROR))
        return NULL;

    /* process a skip request */
    if (state.state->seek) {
        state.state->seek = 0;
        if (gz_skip(state, state.state->skip) == -1)
            return NULL;
    }

    /* copy output bytes up to new line or len - 1, whichever comes first --
       append a terminating zero to the string (we don't check for a zero in
       the contents, let the user worry about that) */
    str = buf;
    left = (unsigned)len - 1;
    if (left) do {
        /* assure that something is in the output buffer */
        if (state.state->x.have == 0 && gz_fetch(state) == -1)
            return NULL;                /* error */
        if (state.state->x.have == 0) {       /* end of file */
            state.state->past = 1;            /* read past end */
            break;                      /* return what we have */
        }

        /* look for end-of-line in current output buffer */
        n = state.state->x.have > left ? left : state.state->x.have;
        eol = (unsigned char *)memchr(state.state->x.next, '\n', n);
        if (eol != NULL)
            n = (unsigned)(eol - state.state->x.next) + 1;

        /* copy through end-of-line, or remainder if not found */
        memcpy(buf, state.state->x.next, n);
        state.state->x.have -= n;
        state.state->x.next += n;
        state.state->x.pos += n;
        left -= n;
        buf += n;
    } while (left && eol == NULL);

    /* return terminated string, or if nothing, end of file */
    if (buf == str)
        return NULL;
    buf[0] = 0;
    return str;
}

/* -- see zlib.h -- */
int ZEXPORT gzdirect(file)
    gzFile file;
{
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return 0;
    state = (gz_statep)file;

    /* if the state is not known, but we can find out, then do so (this is
       mainly for right after a gzopen() or gzdopen()) */
    if (state.state->mode == GZ_READ && state.state->how == LOOK && state.state->x.have == 0)
        (void)gz_look(state);

    /* return 1 if transparent, 0 if processing a gzip stream */
    return state.state->direct;
}

/* -- see zlib.h -- */
int ZEXPORT gzclose_r(file)
    gzFile file;
{
    int ret, err;
    gz_statep state;

    /* get internal structure */
    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_statep)file;

    /* check that we're reading */
    if (state.state->mode != GZ_READ)
        return Z_STREAM_ERROR;

    /* free memory and close file */
    if (state.state->size) {
        inflateEnd(&(state.state->strm));
        free(state.state->out);
        free(state.state->in);
    }
    err = state.state->err == Z_BUF_ERROR ? Z_BUF_ERROR : Z_OK;
    gz_error(state, Z_OK, NULL);
    free(state.state->path);
    ret = close(state.state->fd);
    free(state.state);
    return ret ? Z_ERRNO : err;
}
