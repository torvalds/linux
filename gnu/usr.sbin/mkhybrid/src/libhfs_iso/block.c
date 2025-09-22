/*
 * hfsutils - tools for reading and writing Macintosh HFS volumes
 * Copyright (C) 1996, 1997 Robert Leslie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

# include <string.h>
# include <sys/types.h>
# include <unistd.h>
# include <errno.h>
# include <time.h>

# include "internal.h"
# include "data.h"
# include "block.h"
# include "low.h"

#ifdef DEBUG
#include <stdio.h>
#endif /* DEBUG */

/*
 * NAME:	block->readlb()
 * DESCRIPTION:	read a logical block from a volume
 */
int b_readlb(hfsvol *vol, unsigned long num, block *bp)
{
#ifdef APPLE_HYB
  block *b;
  hce_mem *hce;

#ifdef DEBUG
  fprintf(stderr,"b_readlb: start block = %d\n", vol->vstart + num);
#endif /* DEBUG */

  hce = vol->hce;

/* Check to see if requested block is in the HFS header or catalog/exents
   files. If it is, read info from memory copy. If not, then something
   has gone horribly wrong ... */

  if (num < hce->hfs_hdr_size) 
      b = (block *)hce->hfs_hdr + num;
  else if (num < hce->hfs_hdr_size + hce->hfs_ce_size)
      b = (block *)hce->hfs_ce + num - hce->hfs_hdr_size;
  else
    {
      ERROR(EIO, "should not happen!");
      return -1;
    }

  memcpy(bp, b, HFS_BLOCKSZ);

#else
  int bytes;

  if (lseek(vol->fd, (vol->vstart + num) * HFS_BLOCKSZ, SEEK_SET) < 0)
    {
      ERROR(errno, "error seeking device");
      return -1;
    }

  bytes = read(vol->fd, bp, HFS_BLOCKSZ);
  if (bytes < 0)
    {
      ERROR(errno, "error reading from device");
      return -1;
    }
  else if (bytes == 0)
    {
      ERROR(EIO, "read EOF on volume");
      return -1;
    }
  else if (bytes != HFS_BLOCKSZ)
    {
      ERROR(EIO, "read incomplete block");
      return -1;
    }
#endif /* APPLE_HYB */
  return 0;
}

/*
 * NAME:	block->writelb()
 * DESCRIPTION:	write a logical block to a volume
 */
int b_writelb(hfsvol *vol, unsigned long num, block *bp)
{
#ifdef APPLE_HYB
  block *b;
  hce_mem *hce;

#ifdef DEBUG
  fprintf(stderr,"b_writelb: start block = %d\n", vol->vstart + num);
#endif /* DEBUG */

   hce = vol->hce;

/* Check to see if requested block is in the HFS header or catalog/exents
   files. If it is, write info to memory copy. If not, then it's a block
   for an ordinary file - and as we are writing the files later, then just
   ignore and return OK */
  if (num < hce->hfs_hdr_size) 
      b = (block *)hce->hfs_hdr + num;
  else if (num < hce->hfs_hdr_size + hce->hfs_ce_size)
      b = (block *)hce->hfs_ce + num - hce->hfs_hdr_size;
  else
    {
#ifdef DEBUG
      fprintf(stderr,"b_writelb: ignoring\n");
#endif /* DEBUG */
      return 0;
    }

  memcpy(b, bp, HFS_BLOCKSZ);

#else
  int bytes;

  if (lseek(vol->fd, (vol->vstart + num) * HFS_BLOCKSZ, SEEK_SET) < 0)
    {
      ERROR(errno, "error seeking device");
      return -1;
    }

  bytes = write(vol->fd, bp, HFS_BLOCKSZ);

  if (bytes < 0)
    {
      ERROR(errno, "error writing to device");
      return -1;
    }
  else if (bytes != HFS_BLOCKSZ)
    {
      ERROR(EIO, "wrote incomplete block");
      return -1;
    }
#endif /* APPLE_HYB */
  return 0;
}

/*
 * NAME:	block->readab()
 * DESCRIPTION:	read a block from an allocation block from a volume
 */
int b_readab(hfsvol *vol,
	     unsigned int anum, unsigned int index, block *bp)
{
  /* verify the allocation block exists and is marked as in-use */

  if (anum >= vol->mdb.drNmAlBlks)
    {
      ERROR(EIO, "read nonexistent block");
      return -1;
    }
  else if (vol->vbm && ! BMTST(vol->vbm, anum))
    {
      ERROR(EIO, "read unallocated block");
      return -1;
    }

  return b_readlb(vol, vol->mdb.drAlBlSt + anum * vol->lpa + index, bp);
}

/*
 * NAME:	b->writeab()
 * DESCRIPTION:	write a block to an allocation block to a volume
 */
int b_writeab(hfsvol *vol,
	      unsigned int anum, unsigned int index, block *bp)
{
  /* verify the allocation block exists and is marked as in-use */

  if (anum >= vol->mdb.drNmAlBlks)
    {
      ERROR(EIO, "write nonexistent block");
      return -1;
    }
  else if (vol->vbm && ! BMTST(vol->vbm, anum))
    {
      ERROR(EIO, "write unallocated block");
      return -1;
    }

  vol->mdb.drAtrb &= ~HFS_ATRB_UMOUNTED;
  vol->mdb.drLsMod = d_tomtime(time(0));
  ++vol->mdb.drWrCnt;

  vol->flags |= HFS_UPDATE_MDB;

  return b_writelb(vol, vol->mdb.drAlBlSt + anum * vol->lpa + index, bp);
}
