/* infblock.c -- interpret and process block types to last block
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

#include <linux/zutil.h>
#include "infblock.h"
#include "inftrees.h"
#include "infcodes.h"
#include "infutil.h"

struct inflate_codes_state;

/* simplify the use of the inflate_huft type with some defines */
#define exop word.what.Exop
#define bits word.what.Bits

/* Table for deflate from PKZIP's appnote.txt. */
static const uInt border[] = { /* Order of the bit length code lengths */
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/*
   Notes beyond the 1.93a appnote.txt:

   1. Distance pointers never point before the beginning of the output
      stream.
   2. Distance pointers can point back across blocks, up to 32k away.
   3. There is an implied maximum of 7 bits for the bit length table and
      15 bits for the actual data.
   4. If only one code exists, then it is encoded using one bit.  (Zero
      would be more efficient, but perhaps a little confusing.)  If two
      codes exist, they are coded using one bit each (0 and 1).
   5. There is no way of sending zero distance codes--a dummy must be
      sent if there are none.  (History: a pre 2.0 version of PKZIP would
      store blocks with no distance codes, but this was discovered to be
      too harsh a criterion.)  Valid only for 1.93a.  2.04c does allow
      zero distance codes, which is sent as one code of zero bits in
      length.
   6. There are up to 286 literal/length codes.  Code 256 represents the
      end-of-block.  Note however that the static length tree defines
      288 codes just to fill out the Huffman codes.  Codes 286 and 287
      cannot be used though, since there is no length base or extra bits
      defined for them.  Similarily, there are up to 30 distance codes.
      However, static trees define 32 codes (all 5 bits) to fill out the
      Huffman codes, but the last two had better not show up in the data.
   7. Unzip can check dynamic Huffman blocks for complete code sets.
      The exception is that a single code would not be complete (see #4).
   8. The five bits following the block type is really the number of
      literal codes sent minus 257.
   9. Length codes 8,16,16 are interpreted as 13 length codes of 8 bits
      (1+6+6).  Therefore, to output three times the length, you output
      three codes (1+1+1), whereas to output four times the same length,
      you only need two codes (1+3).  Hmm.
  10. In the tree reconstruction algorithm, Code = Code + Increment
      only if BitLength(i) is not zero.  (Pretty obvious.)
  11. Correction: 4 Bits: # of Bit Length codes - 4     (4 - 19)
  12. Note: length code 284 can represent 227-258, but length code 285
      really is 258.  The last length deserves its own, short code
      since it gets used a lot in very redundant files.  The length
      258 is special since 258 - 3 (the min match length) is 255.
  13. The literal/length and distance code bit lengths are read as a
      single stream of lengths.  It is possible (and advantageous) for
      a repeat code (16, 17, or 18) to go across the boundary between
      the two sets of lengths.
 */


void zlib_inflate_blocks_reset(
	inflate_blocks_statef *s,
	z_streamp z,
	uLong *c
)
{
  if (c != NULL)
    *c = s->check;
  if (s->mode == CODES)
    zlib_inflate_codes_free(s->sub.decode.codes, z);
  s->mode = TYPE;
  s->bitk = 0;
  s->bitb = 0;
  s->read = s->write = s->window;
  if (s->checkfn != NULL)
    z->adler = s->check = (*s->checkfn)(0L, NULL, 0);
}

inflate_blocks_statef *zlib_inflate_blocks_new(
	z_streamp z,
	check_func c,
	uInt w
)
{
  inflate_blocks_statef *s;

  s = &WS(z)->working_blocks_state;
  s->hufts = WS(z)->working_hufts;
  s->window = WS(z)->working_window;
  s->end = s->window + w;
  s->checkfn = c;
  s->mode = TYPE;
  zlib_inflate_blocks_reset(s, z, NULL);
  return s;
}


int zlib_inflate_blocks(
	inflate_blocks_statef *s,
	z_streamp z,
	int r
)
{
  uInt t;               /* temporary storage */
  uLong b;              /* bit buffer */
  uInt k;               /* bits in bit buffer */
  Byte *p;              /* input data pointer */
  uInt n;               /* bytes available there */
  Byte *q;              /* output window write pointer */
  uInt m;               /* bytes to end of window or read pointer */

  /* copy input/output information to locals (UPDATE macro restores) */
  LOAD

  /* process input based on current state */
  while (1) switch (s->mode)
  {
    case TYPE:
      NEEDBITS(3)
      t = (uInt)b & 7;
      s->last = t & 1;
      switch (t >> 1)
      {
        case 0:                         /* stored */
          DUMPBITS(3)
          t = k & 7;                    /* go to byte boundary */
          DUMPBITS(t)
          s->mode = LENS;               /* get length of stored block */
          break;
        case 1:                         /* fixed */
          {
            uInt bl, bd;
            inflate_huft *tl, *td;

            zlib_inflate_trees_fixed(&bl, &bd, &tl, &td, s->hufts, z);
            s->sub.decode.codes = zlib_inflate_codes_new(bl, bd, tl, td, z);
            if (s->sub.decode.codes == NULL)
            {
              r = Z_MEM_ERROR;
              LEAVE
            }
          }
          DUMPBITS(3)
          s->mode = CODES;
          break;
        case 2:                         /* dynamic */
          DUMPBITS(3)
          s->mode = TABLE;
          break;
        case 3:                         /* illegal */
          DUMPBITS(3)
          s->mode = B_BAD;
          z->msg = (char*)"invalid block type";
          r = Z_DATA_ERROR;
          LEAVE
      }
      break;
    case LENS:
      NEEDBITS(32)
      if ((((~b) >> 16) & 0xffff) != (b & 0xffff))
      {
        s->mode = B_BAD;
        z->msg = (char*)"invalid stored block lengths";
        r = Z_DATA_ERROR;
        LEAVE
      }
      s->sub.left = (uInt)b & 0xffff;
      b = k = 0;                      /* dump bits */
      s->mode = s->sub.left ? STORED : (s->last ? DRY : TYPE);
      break;
    case STORED:
      if (n == 0)
        LEAVE
      NEEDOUT
      t = s->sub.left;
      if (t > n) t = n;
      if (t > m) t = m;
      memcpy(q, p, t);
      p += t;  n -= t;
      q += t;  m -= t;
      if ((s->sub.left -= t) != 0)
        break;
      s->mode = s->last ? DRY : TYPE;
      break;
    case TABLE:
      NEEDBITS(14)
      s->sub.trees.table = t = (uInt)b & 0x3fff;
#ifndef PKZIP_BUG_WORKAROUND
      if ((t & 0x1f) > 29 || ((t >> 5) & 0x1f) > 29)
      {
        s->mode = B_BAD;
        z->msg = (char*)"too many length or distance symbols";
        r = Z_DATA_ERROR;
        LEAVE
      }
#endif
      {
      	s->sub.trees.blens = WS(z)->working_blens;
      }
      DUMPBITS(14)
      s->sub.trees.index = 0;
      s->mode = BTREE;
    case BTREE:
      while (s->sub.trees.index < 4 + (s->sub.trees.table >> 10))
      {
        NEEDBITS(3)
        s->sub.trees.blens[border[s->sub.trees.index++]] = (uInt)b & 7;
        DUMPBITS(3)
      }
      while (s->sub.trees.index < 19)
        s->sub.trees.blens[border[s->sub.trees.index++]] = 0;
      s->sub.trees.bb = 7;
      t = zlib_inflate_trees_bits(s->sub.trees.blens, &s->sub.trees.bb,
				  &s->sub.trees.tb, s->hufts, z);
      if (t != Z_OK)
      {
        r = t;
        if (r == Z_DATA_ERROR)
          s->mode = B_BAD;
        LEAVE
      }
      s->sub.trees.index = 0;
      s->mode = DTREE;
    case DTREE:
      while (t = s->sub.trees.table,
             s->sub.trees.index < 258 + (t & 0x1f) + ((t >> 5) & 0x1f))
      {
        inflate_huft *h;
        uInt i, j, c;

        t = s->sub.trees.bb;
        NEEDBITS(t)
        h = s->sub.trees.tb + ((uInt)b & zlib_inflate_mask[t]);
        t = h->bits;
        c = h->base;
        if (c < 16)
        {
          DUMPBITS(t)
          s->sub.trees.blens[s->sub.trees.index++] = c;
        }
        else /* c == 16..18 */
        {
          i = c == 18 ? 7 : c - 14;
          j = c == 18 ? 11 : 3;
          NEEDBITS(t + i)
          DUMPBITS(t)
          j += (uInt)b & zlib_inflate_mask[i];
          DUMPBITS(i)
          i = s->sub.trees.index;
          t = s->sub.trees.table;
          if (i + j > 258 + (t & 0x1f) + ((t >> 5) & 0x1f) ||
              (c == 16 && i < 1))
          {
            s->mode = B_BAD;
            z->msg = (char*)"invalid bit length repeat";
            r = Z_DATA_ERROR;
            LEAVE
          }
          c = c == 16 ? s->sub.trees.blens[i - 1] : 0;
          do {
            s->sub.trees.blens[i++] = c;
          } while (--j);
          s->sub.trees.index = i;
        }
      }
      s->sub.trees.tb = NULL;
      {
        uInt bl, bd;
        inflate_huft *tl, *td;
        inflate_codes_statef *c;

        bl = 9;         /* must be <= 9 for lookahead assumptions */
        bd = 6;         /* must be <= 9 for lookahead assumptions */
        t = s->sub.trees.table;
        t = zlib_inflate_trees_dynamic(257 + (t & 0x1f), 1 + ((t >> 5) & 0x1f),
				       s->sub.trees.blens, &bl, &bd, &tl, &td,
				       s->hufts, z);
        if (t != Z_OK)
        {
          if (t == (uInt)Z_DATA_ERROR)
            s->mode = B_BAD;
          r = t;
          LEAVE
        }
        if ((c = zlib_inflate_codes_new(bl, bd, tl, td, z)) == NULL)
        {
          r = Z_MEM_ERROR;
          LEAVE
        }
        s->sub.decode.codes = c;
      }
      s->mode = CODES;
    case CODES:
      UPDATE
      if ((r = zlib_inflate_codes(s, z, r)) != Z_STREAM_END)
        return zlib_inflate_flush(s, z, r);
      r = Z_OK;
      zlib_inflate_codes_free(s->sub.decode.codes, z);
      LOAD
      if (!s->last)
      {
        s->mode = TYPE;
        break;
      }
      s->mode = DRY;
    case DRY:
      FLUSH
      if (s->read != s->write)
        LEAVE
      s->mode = B_DONE;
    case B_DONE:
      r = Z_STREAM_END;
      LEAVE
    case B_BAD:
      r = Z_DATA_ERROR;
      LEAVE
    default:
      r = Z_STREAM_ERROR;
      LEAVE
  }
}


int zlib_inflate_blocks_free(
	inflate_blocks_statef *s,
	z_streamp z
)
{
  zlib_inflate_blocks_reset(s, z, NULL);
  return Z_OK;
}


void zlib_inflate_set_dictionary(
	inflate_blocks_statef *s,
	const Byte *d,
	uInt  n
)
{
  memcpy(s->window, d, n);
  s->read = s->write = s->window + n;
}


/* Returns true if inflate is currently at the end of a block generated
 * by Z_SYNC_FLUSH or Z_FULL_FLUSH. 
 * IN assertion: s != NULL
 */
int zlib_inflate_blocks_sync_point(
	inflate_blocks_statef *s
)
{
  return s->mode == LENS;
}
