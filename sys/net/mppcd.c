/*-
 * Copyright (c) 2002-2004 Jan Dubiec <jdx@slackware.pl>
 * Copyright (c) 2007 Alexander Motin <mav@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

/*
 * MPPC decompression library.
 * Version 1.0
 *
 * Note that Hi/Fn (later acquired by Exar Corporation) held US patents
 * on some implementation-critical aspects of MPPC compression.
 * These patents lapsed due to non-payment of fees in 2007 and by 2015
 * expired altogether.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <net/mppc.h>

#define	MPPE_HIST_LEN          8192

struct MPPC_decomp_state {
    uint8_t	hist[2*MPPE_HIST_LEN];
    uint16_t	histptr;
};

static uint32_t __inline
getbits(const uint8_t *buf, const uint32_t n, uint32_t *i, uint32_t *l)
{
    static const uint32_t m[] = {0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};
    uint32_t res, ol;

    ol = *l;
    if (*l >= n) {
	*l = (*l) - n;
	res = (buf[*i] & m[ol]) >> (*l);
	if (*l == 0) {
	    *l = 8;
	    (*i)++;
	}
    } else {
	*l = 8 - n + (*l);
	res = (buf[(*i)++] & m[ol]) << 8;
	res = (res | buf[*i]) >> (*l);
    }

    return (res);
}

static uint32_t __inline
getbyte(const uint8_t *buf, const uint32_t i, const uint32_t l)
{
    if (l == 8) {
	return (buf[i]);
    } else {
	return ((((buf[i] << 8) | buf[i+1]) >> l) & 0xff);
    }
}

static void __inline
lamecopy(uint8_t *dst, uint8_t *src, uint32_t len)
{
    while (len--)
	*dst++ = *src++;
}

size_t MPPC_SizeOfDecompressionHistory(void)
{
    return (sizeof(struct MPPC_decomp_state));
}

void MPPC_InitDecompressionHistory(char *history)
{
    struct MPPC_decomp_state      *state = (struct MPPC_decomp_state*)history;

    bzero(history, sizeof(struct MPPC_decomp_state));
    state->histptr = MPPE_HIST_LEN;
}

int MPPC_Decompress(u_char **src, u_char **dst, u_long *srcCnt, u_long *dstCnt, char *history, int flags)
{
    struct MPPC_decomp_state      *state = (struct MPPC_decomp_state*)history;
    uint32_t olen, off, len, bits, val, sig, i, l;
    uint8_t *hist, *s;
    u_char *isrc = *src;
    int	rtn = MPPC_OK;

    if ((flags & MPPC_RESTART_HISTORY) != 0) {
	memcpy(state->hist, state->hist + MPPE_HIST_LEN, MPPE_HIST_LEN);
	state->histptr = MPPE_HIST_LEN;
    }

    hist = state->hist + state->histptr;
    olen = len = i = 0;
    l = 8;
    bits = *srcCnt * 8;
    while (bits >= 8) {
	val = getbyte(isrc, i++, l);
	if (val < 0x80) {		/* literal byte < 0x80 */
	    if (state->histptr < 2*MPPE_HIST_LEN) {
		/* Copy uncompressed byte to the history. */
		(state->hist)[(state->histptr)++] = (uint8_t) val;
	    } else {
		/* Buffer overflow; drop packet. */
		rtn &= ~MPPC_OK;
		return rtn;
	    }
	    olen++;
	    bits -= 8;
	    continue;
	}

	sig = val & 0xc0;
	if (sig == 0x80) {		/* literal byte >= 0x80 */
	    if (state->histptr < 2*MPPE_HIST_LEN) {
		/* Copy uncompressed byte to the history. */
		(state->hist)[(state->histptr)++] = 
		    (uint8_t) (0x80|((val&0x3f)<<1)|getbits(isrc, 1 , &i ,&l));
	    } else {
		/* buffer overflow; drop packet */
		rtn &= ~MPPC_OK;
		return (rtn);
	    }
	    olen++;
	    bits -= 9;
	    continue;
	}

	/* Not a literal byte so it must be an (offset,length) pair */
	/* decode offset */
	sig = val & 0xf0;
	if (sig == 0xf0) {		/* 10-bit offset; 0 <= offset < 64 */
	    off = (((val&0x0f)<<2)|getbits(isrc, 2 , &i ,&l));
	    bits -= 10;
	} else {
	    if (sig == 0xe0) {		/* 12-bit offset; 64 <= offset < 320 */
		off = ((((val&0x0f)<<4)|getbits(isrc, 4 , &i ,&l))+64);
		bits -= 12;
	    } else {
		if ((sig&0xe0) == 0xc0) {/* 16-bit offset; 320 <= offset < 8192 */
		    off = ((((val&0x1f)<<8)|getbyte(isrc, i++, l))+320);
		    bits -= 16;
		    if (off > MPPE_HIST_LEN - 1) {
			rtn &= ~MPPC_OK;
			return (rtn);
		    }
		} else {		/* This shouldn't happen. */
		    rtn &= ~MPPC_OK;
		    return (rtn);
		}
	    }
	}
	/* Decode length of match. */
	val = getbyte(isrc, i, l);
	if ((val & 0x80) == 0x00) {			/* len = 3 */
	    len = 3;
	    bits--;
	    getbits(isrc, 1 , &i ,&l);
	} else if ((val & 0xc0) == 0x80) {		/* 4 <= len < 8 */
	    len = 0x04 | ((val>>4) & 0x03);
	    bits -= 4;
	    getbits(isrc, 4 , &i ,&l);
	} else if ((val & 0xe0) == 0xc0) {		/* 8 <= len < 16 */
	    len = 0x08 | ((val>>2) & 0x07);
	    bits -= 6;
	    getbits(isrc, 6 , &i ,&l);
	} else if ((val & 0xf0) == 0xe0) {		/* 16 <= len < 32 */
	    len = 0x10 | (val & 0x0f);
	    bits -= 8;
	    i++;
	} else {
	    bits -= 8;
	    val = (val << 8) | getbyte(isrc, ++i, l);
	    if ((val & 0xf800) == 0xf000) {		/* 32 <= len < 64 */
		len = 0x0020 | ((val >> 6) & 0x001f);
		bits -= 2;
		getbits(isrc, 2 , &i ,&l);
	    } else if ((val & 0xfc00) == 0xf800) {	/* 64 <= len < 128 */
		len = 0x0040 | ((val >> 4) & 0x003f);
		bits -= 4;
		getbits(isrc, 4 , &i ,&l);
	    } else if ((val & 0xfe00) == 0xfc00) {	/* 128 <= len < 256 */
		len = 0x0080 | ((val >> 2) & 0x007f);
		bits -= 6;
		getbits(isrc, 6 , &i ,&l);
	    } else if ((val & 0xff00) == 0xfe00) {	/* 256 <= len < 512 */
		len = 0x0100 | (val & 0x00ff);
		bits -= 8;
		i++;
	    } else {
		bits -= 8;
		val = (val << 8) | getbyte(isrc, ++i, l);
		if ((val & 0xff8000) == 0xff0000) {	/* 512 <= len < 1024 */
		    len = 0x000200 | ((val >> 6) & 0x0001ff);
		    bits -= 2;
		    getbits(isrc, 2 , &i ,&l);
		} else if ((val & 0xffc000) == 0xff8000) {/* 1024 <= len < 2048 */
		    len = 0x000400 | ((val >> 4) & 0x0003ff);
		    bits -= 4;
		    getbits(isrc, 4 , &i ,&l);
		} else if ((val & 0xffe000) == 0xffc000) {/* 2048 <= len < 4096 */
		    len = 0x000800 | ((val >> 2) & 0x0007ff);
		    bits -= 6;
		    getbits(isrc, 6 , &i ,&l);
		} else if ((val & 0xfff000) == 0xffe000) {/* 4096 <= len < 8192 */
		    len = 0x001000 | (val & 0x000fff);
		    bits -= 8;
		    i++;
		} else {				/* NOTREACHED */
		    rtn &= ~MPPC_OK;
		    return (rtn);
		}
	    }
	}

	s = state->hist + state->histptr;
	state->histptr += len;
	olen += len;
	if (state->histptr < 2*MPPE_HIST_LEN) {
	    /* Copy uncompressed bytes to the history. */

	    /*
	     * In some cases len may be greater than off. It means that memory
	     * areas pointed by s and s-off overlap. To decode that strange case
	     * data should be copied exactly by address increasing to make
	     * some data repeated.
	     */
	    lamecopy(s, s - off, len);
	} else {
	    /* Buffer overflow; drop packet. */
	    rtn &= ~MPPC_OK;
	    return (rtn);
	}
    }

    /* Do PFC decompression. */
    len = olen;
    if ((hist[0] & 0x01) != 0) {
	(*dst)[0] = 0;
	(*dst)++;
	len++;
    }

    if (len <= *dstCnt) {
	/* Copy uncompressed packet to the output buffer. */
	memcpy(*dst, hist, olen);
    } else {
	/* Buffer overflow; drop packet. */
	rtn |= MPPC_DEST_EXHAUSTED;
    }

    *src += *srcCnt;
    *srcCnt = 0;
    *dst += len;
    *dstCnt -= len;

    return (rtn);
}
