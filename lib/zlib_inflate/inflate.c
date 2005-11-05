/* inflate.c -- zlib interface to inflate modules
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

#include <linux/zutil.h>
#include "infblock.h"
#include "infutil.h"

int zlib_inflate_workspacesize(void)
{
  return sizeof(struct inflate_workspace);
}


int zlib_inflateReset(
	z_streamp z
)
{
  if (z == NULL || z->state == NULL || z->workspace == NULL)
    return Z_STREAM_ERROR;
  z->total_in = z->total_out = 0;
  z->msg = NULL;
  z->state->mode = z->state->nowrap ? BLOCKS : METHOD;
  zlib_inflate_blocks_reset(z->state->blocks, z, NULL);
  return Z_OK;
}


int zlib_inflateEnd(
	z_streamp z
)
{
  if (z == NULL || z->state == NULL || z->workspace == NULL)
    return Z_STREAM_ERROR;
  if (z->state->blocks != NULL)
    zlib_inflate_blocks_free(z->state->blocks, z);
  z->state = NULL;
  return Z_OK;
}


int zlib_inflateInit2_(
	z_streamp z,
	int w,
	const char *version,
	int stream_size
)
{
  if (version == NULL || version[0] != ZLIB_VERSION[0] ||
      stream_size != sizeof(z_stream) || z->workspace == NULL)
      return Z_VERSION_ERROR;

  /* initialize state */
  z->msg = NULL;
  z->state = &WS(z)->internal_state;
  z->state->blocks = NULL;

  /* handle undocumented nowrap option (no zlib header or check) */
  z->state->nowrap = 0;
  if (w < 0)
  {
    w = - w;
    z->state->nowrap = 1;
  }

  /* set window size */
  if (w < 8 || w > 15)
  {
    zlib_inflateEnd(z);
    return Z_STREAM_ERROR;
  }
  z->state->wbits = (uInt)w;

  /* create inflate_blocks state */
  if ((z->state->blocks =
      zlib_inflate_blocks_new(z, z->state->nowrap ? NULL : zlib_adler32, (uInt)1 << w))
      == NULL)
  {
    zlib_inflateEnd(z);
    return Z_MEM_ERROR;
  }

  /* reset state */
  zlib_inflateReset(z);
  return Z_OK;
}


/*
 * At the end of a Deflate-compressed PPP packet, we expect to have seen
 * a `stored' block type value but not the (zero) length bytes.
 */
static int zlib_inflate_packet_flush(inflate_blocks_statef *s)
{
    if (s->mode != LENS)
	return Z_DATA_ERROR;
    s->mode = TYPE;
    return Z_OK;
}


int zlib_inflateInit_(
	z_streamp z,
	const char *version,
	int stream_size
)
{
  return zlib_inflateInit2_(z, DEF_WBITS, version, stream_size);
}

#undef NEEDBYTE
#undef NEXTBYTE
#define NEEDBYTE {if(z->avail_in==0)goto empty;r=trv;}
#define NEXTBYTE (z->avail_in--,z->total_in++,*z->next_in++)

int zlib_inflate(
	z_streamp z,
	int f
)
{
  int r, trv;
  uInt b;

  if (z == NULL || z->state == NULL || z->next_in == NULL)
    return Z_STREAM_ERROR;
  trv = f == Z_FINISH ? Z_BUF_ERROR : Z_OK;
  r = Z_BUF_ERROR;
  while (1) switch (z->state->mode)
  {
    case METHOD:
      NEEDBYTE
      if (((z->state->sub.method = NEXTBYTE) & 0xf) != Z_DEFLATED)
      {
        z->state->mode = I_BAD;
        z->msg = (char*)"unknown compression method";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      if ((z->state->sub.method >> 4) + 8 > z->state->wbits)
      {
        z->state->mode = I_BAD;
        z->msg = (char*)"invalid window size";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      z->state->mode = FLAG;
    case FLAG:
      NEEDBYTE
      b = NEXTBYTE;
      if (((z->state->sub.method << 8) + b) % 31)
      {
        z->state->mode = I_BAD;
        z->msg = (char*)"incorrect header check";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      if (!(b & PRESET_DICT))
      {
        z->state->mode = BLOCKS;
        break;
      }
      z->state->mode = DICT4;
    case DICT4:
      NEEDBYTE
      z->state->sub.check.need = (uLong)NEXTBYTE << 24;
      z->state->mode = DICT3;
    case DICT3:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE << 16;
      z->state->mode = DICT2;
    case DICT2:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE << 8;
      z->state->mode = DICT1;
    case DICT1:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE;
      z->adler = z->state->sub.check.need;
      z->state->mode = DICT0;
      return Z_NEED_DICT;
    case DICT0:
      z->state->mode = I_BAD;
      z->msg = (char*)"need dictionary";
      z->state->sub.marker = 0;       /* can try inflateSync */
      return Z_STREAM_ERROR;
    case BLOCKS:
      r = zlib_inflate_blocks(z->state->blocks, z, r);
      if (f == Z_PACKET_FLUSH && z->avail_in == 0 && z->avail_out != 0)
	  r = zlib_inflate_packet_flush(z->state->blocks);
      if (r == Z_DATA_ERROR)
      {
        z->state->mode = I_BAD;
        z->state->sub.marker = 0;       /* can try inflateSync */
        break;
      }
      if (r == Z_OK)
        r = trv;
      if (r != Z_STREAM_END)
        return r;
      r = trv;
      zlib_inflate_blocks_reset(z->state->blocks, z, &z->state->sub.check.was);
      if (z->state->nowrap)
      {
        z->state->mode = I_DONE;
        break;
      }
      z->state->mode = CHECK4;
    case CHECK4:
      NEEDBYTE
      z->state->sub.check.need = (uLong)NEXTBYTE << 24;
      z->state->mode = CHECK3;
    case CHECK3:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE << 16;
      z->state->mode = CHECK2;
    case CHECK2:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE << 8;
      z->state->mode = CHECK1;
    case CHECK1:
      NEEDBYTE
      z->state->sub.check.need += (uLong)NEXTBYTE;

      if (z->state->sub.check.was != z->state->sub.check.need)
      {
        z->state->mode = I_BAD;
        z->msg = (char*)"incorrect data check";
        z->state->sub.marker = 5;       /* can't try inflateSync */
        break;
      }
      z->state->mode = I_DONE;
    case I_DONE:
      return Z_STREAM_END;
    case I_BAD:
      return Z_DATA_ERROR;
    default:
      return Z_STREAM_ERROR;
  }
 empty:
  if (f != Z_PACKET_FLUSH)
    return r;
  z->state->mode = I_BAD;
  z->msg = (char *)"need more for packet flush";
  z->state->sub.marker = 0;       /* can try inflateSync */
  return Z_DATA_ERROR;
}
