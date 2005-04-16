/* inffast.c -- process literals and length/distance pairs fast
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

#include <linux/zutil.h>
#include "inftrees.h"
#include "infblock.h"
#include "infcodes.h"
#include "infutil.h"
#include "inffast.h"

struct inflate_codes_state;

/* simplify the use of the inflate_huft type with some defines */
#define exop word.what.Exop
#define bits word.what.Bits

/* macros for bit input with no checking and for returning unused bytes */
#define GRABBITS(j) {while(k<(j)){b|=((uLong)NEXTBYTE)<<k;k+=8;}}
#define UNGRAB {c=z->avail_in-n;c=(k>>3)<c?k>>3:c;n+=c;p-=c;k-=c<<3;}

/* Called with number of bytes left to write in window at least 258
   (the maximum string length) and number of input bytes available
   at least ten.  The ten bytes are six bytes for the longest length/
   distance pair plus four bytes for overloading the bit buffer. */

int zlib_inflate_fast(
	uInt bl,
	uInt bd,
	inflate_huft *tl,
	inflate_huft *td, /* need separate declaration for Borland C++ */
	inflate_blocks_statef *s,
	z_streamp z
)
{
  inflate_huft *t;      /* temporary pointer */
  uInt e;               /* extra bits or operation */
  uLong b;              /* bit buffer */
  uInt k;               /* bits in bit buffer */
  Byte *p;              /* input data pointer */
  uInt n;               /* bytes available there */
  Byte *q;              /* output window write pointer */
  uInt m;               /* bytes to end of window or read pointer */
  uInt ml;              /* mask for literal/length tree */
  uInt md;              /* mask for distance tree */
  uInt c;               /* bytes to copy */
  uInt d;               /* distance back to copy from */
  Byte *r;              /* copy source pointer */

  /* load input, output, bit values */
  LOAD

  /* initialize masks */
  ml = zlib_inflate_mask[bl];
  md = zlib_inflate_mask[bd];

  /* do until not enough input or output space for fast loop */
  do {                          /* assume called with m >= 258 && n >= 10 */
    /* get literal/length code */
    GRABBITS(20)                /* max bits for literal/length code */
    if ((e = (t = tl + ((uInt)b & ml))->exop) == 0)
    {
      DUMPBITS(t->bits)
      *q++ = (Byte)t->base;
      m--;
      continue;
    }
    do {
      DUMPBITS(t->bits)
      if (e & 16)
      {
        /* get extra bits for length */
        e &= 15;
        c = t->base + ((uInt)b & zlib_inflate_mask[e]);
        DUMPBITS(e)

        /* decode distance base of block to copy */
        GRABBITS(15);           /* max bits for distance code */
        e = (t = td + ((uInt)b & md))->exop;
        do {
          DUMPBITS(t->bits)
          if (e & 16)
          {
            /* get extra bits to add to distance base */
            e &= 15;
            GRABBITS(e)         /* get extra bits (up to 13) */
            d = t->base + ((uInt)b & zlib_inflate_mask[e]);
            DUMPBITS(e)

            /* do the copy */
            m -= c;
            r = q - d;
            if (r < s->window)                  /* wrap if needed */
            {
              do {
                r += s->end - s->window;        /* force pointer in window */
              } while (r < s->window);          /* covers invalid distances */
              e = s->end - r;
              if (c > e)
              {
                c -= e;                         /* wrapped copy */
                do {
                    *q++ = *r++;
                } while (--e);
                r = s->window;
                do {
                    *q++ = *r++;
                } while (--c);
              }
              else                              /* normal copy */
              {
                *q++ = *r++;  c--;
                *q++ = *r++;  c--;
                do {
                    *q++ = *r++;
                } while (--c);
              }
            }
            else                                /* normal copy */
            {
              *q++ = *r++;  c--;
              *q++ = *r++;  c--;
              do {
                *q++ = *r++;
              } while (--c);
            }
            break;
          }
          else if ((e & 64) == 0)
          {
            t += t->base;
            e = (t += ((uInt)b & zlib_inflate_mask[e]))->exop;
          }
          else
          {
            z->msg = (char*)"invalid distance code";
            UNGRAB
            UPDATE
            return Z_DATA_ERROR;
          }
        } while (1);
        break;
      }
      if ((e & 64) == 0)
      {
        t += t->base;
        if ((e = (t += ((uInt)b & zlib_inflate_mask[e]))->exop) == 0)
        {
          DUMPBITS(t->bits)
          *q++ = (Byte)t->base;
          m--;
          break;
        }
      }
      else if (e & 32)
      {
        UNGRAB
        UPDATE
        return Z_STREAM_END;
      }
      else
      {
        z->msg = (char*)"invalid literal/length code";
        UNGRAB
        UPDATE
        return Z_DATA_ERROR;
      }
    } while (1);
  } while (m >= 258 && n >= 10);

  /* not enough input or output--restore pointers and return */
  UNGRAB
  UPDATE
  return Z_OK;
}
