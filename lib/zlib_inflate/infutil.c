/* inflate_util.c -- data and routines common to blocks and codes
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

#include <linux/zutil.h>
#include "infblock.h"
#include "inftrees.h"
#include "infcodes.h"
#include "infutil.h"

struct inflate_codes_state;

/* And'ing with mask[n] masks the lower n bits */
uInt zlib_inflate_mask[17] = {
    0x0000,
    0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};


/* copy as much as possible from the sliding window to the output area */
int zlib_inflate_flush(
	inflate_blocks_statef *s,
	z_streamp z,
	int r
)
{
  uInt n;
  Byte *p;
  Byte *q;

  /* local copies of source and destination pointers */
  p = z->next_out;
  q = s->read;

  /* compute number of bytes to copy as far as end of window */
  n = (uInt)((q <= s->write ? s->write : s->end) - q);
  if (n > z->avail_out) n = z->avail_out;
  if (n && r == Z_BUF_ERROR) r = Z_OK;

  /* update counters */
  z->avail_out -= n;
  z->total_out += n;

  /* update check information */
  if (s->checkfn != NULL)
    z->adler = s->check = (*s->checkfn)(s->check, q, n);

  /* copy as far as end of window */
  memcpy(p, q, n);
  p += n;
  q += n;

  /* see if more to copy at beginning of window */
  if (q == s->end)
  {
    /* wrap pointers */
    q = s->window;
    if (s->write == s->end)
      s->write = s->window;

    /* compute bytes to copy */
    n = (uInt)(s->write - q);
    if (n > z->avail_out) n = z->avail_out;
    if (n && r == Z_BUF_ERROR) r = Z_OK;

    /* update counters */
    z->avail_out -= n;
    z->total_out += n;

    /* update check information */
    if (s->checkfn != NULL)
      z->adler = s->check = (*s->checkfn)(s->check, q, n);

    /* copy */
    memcpy(p, q, n);
    p += n;
    q += n;
  }

  /* update pointers */
  z->next_out = p;
  s->read = q;

  /* done */
  return r;
}
