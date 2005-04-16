/* inflate.c -- zlib interface to inflate modules
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include <linux/zutil.h>
#include "infblock.h"
#include "infutil.h"

int zlib_inflateSync(
	z_streamp z
)
{
  uInt n;       /* number of bytes to look at */
  Byte *p;      /* pointer to bytes */
  uInt m;       /* number of marker bytes found in a row */
  uLong r, w;   /* temporaries to save total_in and total_out */

  /* set up */
  if (z == NULL || z->state == NULL)
    return Z_STREAM_ERROR;
  if (z->state->mode != I_BAD)
  {
    z->state->mode = I_BAD;
    z->state->sub.marker = 0;
  }
  if ((n = z->avail_in) == 0)
    return Z_BUF_ERROR;
  p = z->next_in;
  m = z->state->sub.marker;

  /* search */
  while (n && m < 4)
  {
    static const Byte mark[4] = {0, 0, 0xff, 0xff};
    if (*p == mark[m])
      m++;
    else if (*p)
      m = 0;
    else
      m = 4 - m;
    p++, n--;
  }

  /* restore */
  z->total_in += p - z->next_in;
  z->next_in = p;
  z->avail_in = n;
  z->state->sub.marker = m;

  /* return no joy or set up to restart on a new block */
  if (m != 4)
    return Z_DATA_ERROR;
  r = z->total_in;  w = z->total_out;
  zlib_inflateReset(z);
  z->total_in = r;  z->total_out = w;
  z->state->mode = BLOCKS;
  return Z_OK;
}


/* Returns true if inflate is currently at the end of a block generated
 * by Z_SYNC_FLUSH or Z_FULL_FLUSH. This function is used by one PPP
 * implementation to provide an additional safety check. PPP uses Z_SYNC_FLUSH
 * but removes the length bytes of the resulting empty stored block. When
 * decompressing, PPP checks that at the end of input packet, inflate is
 * waiting for these length bytes.
 */
int zlib_inflateSyncPoint(
	z_streamp z
)
{
  if (z == NULL || z->state == NULL || z->state->blocks == NULL)
    return Z_STREAM_ERROR;
  return zlib_inflate_blocks_sync_point(z->state->blocks);
}

/*
 * This subroutine adds the data at next_in/avail_in to the output history
 * without performing any output.  The output buffer must be "caught up";
 * i.e. no pending output (hence s->read equals s->write), and the state must
 * be BLOCKS (i.e. we should be willing to see the start of a series of
 * BLOCKS).  On exit, the output will also be caught up, and the checksum
 * will have been updated if need be.
 */
static int zlib_inflate_addhistory(inflate_blocks_statef *s,
				      z_stream              *z)
{
    uLong b;              /* bit buffer */  /* NOT USED HERE */
    uInt k;               /* bits in bit buffer */ /* NOT USED HERE */
    uInt t;               /* temporary storage */
    Byte *p;              /* input data pointer */
    uInt n;               /* bytes available there */
    Byte *q;              /* output window write pointer */
    uInt m;               /* bytes to end of window or read pointer */

    if (s->read != s->write)
	return Z_STREAM_ERROR;
    if (s->mode != TYPE)
	return Z_DATA_ERROR;

    /* we're ready to rock */
    LOAD
    /* while there is input ready, copy to output buffer, moving
     * pointers as needed.
     */
    while (n) {
	t = n;  /* how many to do */
	/* is there room until end of buffer? */
	if (t > m) t = m;
	/* update check information */
	if (s->checkfn != NULL)
	    s->check = (*s->checkfn)(s->check, q, t);
	memcpy(q, p, t);
	q += t;
	p += t;
	n -= t;
	z->total_out += t;
	s->read = q;    /* drag read pointer forward */
/*      WWRAP  */ 	/* expand WWRAP macro by hand to handle s->read */
	if (q == s->end) {
	    s->read = q = s->window;
	    m = WAVAIL;
	}
    }
    UPDATE
    return Z_OK;
}


/*
 * This subroutine adds the data at next_in/avail_in to the output history
 * without performing any output.  The output buffer must be "caught up";
 * i.e. no pending output (hence s->read equals s->write), and the state must
 * be BLOCKS (i.e. we should be willing to see the start of a series of
 * BLOCKS).  On exit, the output will also be caught up, and the checksum
 * will have been updated if need be.
 */

int zlib_inflateIncomp(
	z_stream *z

)
{
    if (z->state->mode != BLOCKS)
	return Z_DATA_ERROR;
    return zlib_inflate_addhistory(z->state->blocks, z);
}
