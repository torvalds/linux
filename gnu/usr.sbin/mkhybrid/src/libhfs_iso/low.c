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
# include <stdlib.h>
# include <errno.h>
# include <unistd.h>
# include <fcntl.h>

# include "internal.h"
# include "data.h"
# include "block.h"
# include "low.h"
# include "file.h"

/*
 * NAME:	low->lockvol()
 * DESCRIPTION:	prevent destructive simultaneous access
 */
int l_lockvol(hfsvol *vol)
{
# ifndef NODEVLOCKS

  struct flock lock;

  lock.l_type   = (vol->flags & HFS_READONLY) ? F_RDLCK : F_WRLCK;
  lock.l_start  = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len    = 0;

  if (fcntl(vol->fd, F_SETLK, &lock) < 0)
    {
      ERROR(errno, "unable to obtain lock for device");
      return -1;
    }

# endif

  return 0;
}

/*
 * NAME:	low->readblock0()
 * DESCRIPTION:	read the first sector and get bearings
 */
int l_readblock0(hfsvol *vol)
{
  block b;
  unsigned char *ptr = b;
  Block0 rec;

  if (b_readlb(vol, 0, &b) < 0)
    return -1;

  d_fetchw(&ptr, &rec.sbSig);
  d_fetchw(&ptr, &rec.sbBlkSize);
  d_fetchl(&ptr, &rec.sbBlkCount);
  d_fetchw(&ptr, &rec.sbDevType);
  d_fetchw(&ptr, &rec.sbDevId);
  d_fetchl(&ptr, &rec.sbData);
  d_fetchw(&ptr, &rec.sbDrvrCount);
  d_fetchl(&ptr, &rec.ddBlock);
  d_fetchw(&ptr, &rec.ddSize);
  d_fetchw(&ptr, &rec.ddType);

  switch (rec.sbSig)
    {
    case 0x4552:  /* block device with a partition table */
      {
	if (rec.sbBlkSize != HFS_BLOCKSZ)
	  {
	    ERROR(EINVAL, "unsupported block size");
	    return -1;
	  }

	vol->vlen = rec.sbBlkCount;

	if (l_readpm(vol) < 0)
	  return -1;
      }
      break;

    case 0x4c4b:  /* bootable floppy */
      vol->pnum = 0;
      break;

    default:  /* non-bootable floppy or something else */

      /* some miscreant media may also be partitioned;
	 we attempt to read a partition map, but ignore any failure */

      if (l_readpm(vol) < 0)
	vol->pnum = 0;
    }

  return 0;
}

/*
 * NAME:	low->readpm()
 * DESCRIPTION:	read the partition map and locate an HFS volume
 */
int l_readpm(hfsvol *vol)
{
  block b;
  unsigned char *ptr;
  Partition map;
  unsigned long bnum;
  int pnum;

  bnum = 1;
  pnum = vol->pnum;

  while (1)
    {
      if (b_readlb(vol, bnum, &b) < 0)
	return -1;

      ptr = b;

      d_fetchw(&ptr, &map.pmSig);
      d_fetchw(&ptr, &map.pmSigPad);
      d_fetchl(&ptr, &map.pmMapBlkCnt);
      d_fetchl(&ptr, &map.pmPyPartStart);
      d_fetchl(&ptr, &map.pmPartBlkCnt);

      memcpy(map.pmPartName, ptr, 32);
      map.pmPartName[32] = 0;
      ptr += 32;

      memcpy(map.pmParType, ptr, 32);
      map.pmParType[32] = 0;
      ptr += 32;

      d_fetchl(&ptr, &map.pmLgDataStart);
      d_fetchl(&ptr, &map.pmDataCnt);
      d_fetchl(&ptr, &map.pmPartStatus);
      d_fetchl(&ptr, &map.pmLgBootStart);
      d_fetchl(&ptr, &map.pmBootSize);
      d_fetchl(&ptr, &map.pmBootAddr);
      d_fetchl(&ptr, &map.pmBootAddr2);
      d_fetchl(&ptr, &map.pmBootEntry);
      d_fetchl(&ptr, &map.pmBootEntry2);
      d_fetchl(&ptr, &map.pmBootCksum);

      memcpy(map.pmProcessor, ptr, 16);
      map.pmProcessor[16] = 0;
      ptr += 16;

      if (map.pmSig == 0x5453)
	{
	  /* old partition map sig */

	  ERROR(EINVAL, "unsupported partition map signature");
	  return -1;
	}

      if (map.pmSig != 0x504d)
	{
	  ERROR(EINVAL, "bad partition map");
	  return -1;
	}

      if (strcmp((char *) map.pmParType, "Apple_HFS") == 0 && --pnum == 0)
	{
	  if (map.pmLgDataStart != 0)
	    {
	      ERROR(EINVAL, "unsupported start of partition logical data");
	      return -1;
	    }

	  vol->vstart = map.pmPyPartStart;
	  vol->vlen   = map.pmPartBlkCnt;

	  return 0;
	}

      if (bnum >= map.pmMapBlkCnt)
	{
	  ERROR(EINVAL, "can't find HFS partition");
	  return -1;
	}

      ++bnum;
    }
}

/*
 * NAME:	low->readmdb()
 * DESCRIPTION:	read the master directory block into memory
 */
int l_readmdb(hfsvol *vol)
{
  block b;
  unsigned char *ptr = b;
  MDB *mdb = &vol->mdb;
  hfsfile *ext = &vol->ext.f;
  hfsfile *cat = &vol->cat.f;
  int i;

  if (b_readlb(vol, 2, &b) < 0)
    return -1;

  d_fetchw(&ptr, &mdb->drSigWord);
  d_fetchl(&ptr, &mdb->drCrDate);
  d_fetchl(&ptr, &mdb->drLsMod);
  d_fetchw(&ptr, &mdb->drAtrb);
  d_fetchw(&ptr, (short *) &mdb->drNmFls);
  d_fetchw(&ptr, (short *) &mdb->drVBMSt);
  d_fetchw(&ptr, (short *) &mdb->drAllocPtr);
  d_fetchw(&ptr, (short *) &mdb->drNmAlBlks);
  d_fetchl(&ptr, (long *) &mdb->drAlBlkSiz);
  d_fetchl(&ptr, (long *) &mdb->drClpSiz);
  d_fetchw(&ptr, (short *) &mdb->drAlBlSt);
  d_fetchl(&ptr, &mdb->drNxtCNID);
  d_fetchw(&ptr, (short *) &mdb->drFreeBks);

  d_fetchs(&ptr, mdb->drVN, sizeof(mdb->drVN));

  if (ptr - b != 64)
    abort();

  d_fetchl(&ptr, &mdb->drVolBkUp);
  d_fetchw(&ptr, &mdb->drVSeqNum);
  d_fetchl(&ptr, (long *) &mdb->drWrCnt);
  d_fetchl(&ptr, (long *) &mdb->drXTClpSiz);
  d_fetchl(&ptr, (long *) &mdb->drCTClpSiz);
  d_fetchw(&ptr, (short *) &mdb->drNmRtDirs);
  d_fetchl(&ptr, (long *) &mdb->drFilCnt);
  d_fetchl(&ptr, (long *) &mdb->drDirCnt);

  for (i = 0; i < 8; ++i)
    d_fetchl(&ptr, &mdb->drFndrInfo[i]);

  if (ptr - b != 124)
    abort();

  d_fetchw(&ptr, (short *) &mdb->drVCSize);
  d_fetchw(&ptr, (short *) &mdb->drVBMCSize);
  d_fetchw(&ptr, (short *) &mdb->drCtlCSize);

  d_fetchl(&ptr, (long *) &mdb->drXTFlSize);

  for (i = 0; i < 3; ++i)
    {
      d_fetchw(&ptr, (short *) &mdb->drXTExtRec[i].xdrStABN);
      d_fetchw(&ptr, (short *) &mdb->drXTExtRec[i].xdrNumABlks);
    }

  if (ptr - b != 146)
    abort();

  d_fetchl(&ptr, (long *) &mdb->drCTFlSize);

  for (i = 0; i < 3; ++i)
    {
      d_fetchw(&ptr, (short *) &mdb->drCTExtRec[i].xdrStABN);
      d_fetchw(&ptr, (short *) &mdb->drCTExtRec[i].xdrNumABlks);
    }

  if (ptr - b != 162)
    abort();

  vol->lpa = mdb->drAlBlkSiz / HFS_BLOCKSZ;

  /* extents pseudo-file structs */

  ext->vol   = vol;
  ext->parid = 0;
  strcpy(ext->name, "extents overflow");

  ext->cat.cdrType          = cdrFilRec;
  /* ext->cat.cdrResrv2 */
  ext->cat.u.fil.filFlags   = 0;
  ext->cat.u.fil.filTyp     = 0;
  /* ext->cat.u.fil.filUsrWds */
  ext->cat.u.fil.filFlNum   = HFS_CNID_EXT;
  ext->cat.u.fil.filStBlk   = mdb->drXTExtRec[0].xdrStABN;
  ext->cat.u.fil.filLgLen   = mdb->drXTFlSize;
  ext->cat.u.fil.filPyLen   = mdb->drXTFlSize;
  ext->cat.u.fil.filRStBlk  = 0;
  ext->cat.u.fil.filRLgLen  = 0;
  ext->cat.u.fil.filRPyLen  = 0;
  ext->cat.u.fil.filCrDat   = mdb->drCrDate;
  ext->cat.u.fil.filMdDat   = mdb->drLsMod;
  ext->cat.u.fil.filBkDat   = 0;
  /* ext->cat.u.fil.filFndrInfo */
  ext->cat.u.fil.filClpSize = 0;

  memcpy(ext->cat.u.fil.filExtRec, mdb->drXTExtRec, sizeof(ExtDataRec));
  for (i = 0; i < 3; ++i)
    {
      ext->cat.u.fil.filRExtRec[i].xdrStABN    = 0;
      ext->cat.u.fil.filRExtRec[i].xdrNumABlks = 0;
    }
  f_selectfork(ext, 0);

  ext->clump = mdb->drXTClpSiz;
  ext->flags = 0;

  ext->prev = ext->next = 0;

  /* catalog pseudo-file structs */

  cat->vol   = vol;
  cat->parid = 0;
  strcpy(cat->name, "catalog");

  cat->cat.cdrType          = cdrFilRec;
  /* cat->cat.cdrResrv2 */
  cat->cat.u.fil.filFlags   = 0;
  cat->cat.u.fil.filTyp     = 0;
  /* cat->cat.u.fil.filUsrWds */
  cat->cat.u.fil.filFlNum   = HFS_CNID_CAT;
  cat->cat.u.fil.filStBlk   = mdb->drCTExtRec[0].xdrStABN;
  cat->cat.u.fil.filLgLen   = mdb->drCTFlSize;
  cat->cat.u.fil.filPyLen   = mdb->drCTFlSize;
  cat->cat.u.fil.filRStBlk  = 0;
  cat->cat.u.fil.filRLgLen  = 0;
  cat->cat.u.fil.filRPyLen  = 0;
  cat->cat.u.fil.filCrDat   = mdb->drCrDate;
  cat->cat.u.fil.filMdDat   = mdb->drLsMod;
  cat->cat.u.fil.filBkDat   = 0;
  /* cat->cat.u.fil.filFndrInfo */
  cat->cat.u.fil.filClpSize = 0;

  memcpy(cat->cat.u.fil.filExtRec, mdb->drCTExtRec, sizeof(ExtDataRec));
  for (i = 0; i < 3; ++i)
    {
      cat->cat.u.fil.filRExtRec[i].xdrStABN    = 0;
      cat->cat.u.fil.filRExtRec[i].xdrNumABlks = 0;
    }
  f_selectfork(cat, 0);

  cat->clump = mdb->drCTClpSiz;
  cat->flags = 0;

  cat->prev = cat->next = 0;

  return 0;
}

/*
 * NAME:	low->writemdb()
 * DESCRIPTION:	write the master directory block to disk
 */
int l_writemdb(hfsvol *vol)
{
  block b;
  unsigned char *ptr = b;
  MDB *mdb = &vol->mdb;
  hfsfile *ext = &vol->ext.f;
  hfsfile *cat = &vol->cat.f;
  int i;

  memset(&b, 0, sizeof(b));

  mdb->drXTFlSize = ext->cat.u.fil.filPyLen;
  mdb->drXTClpSiz = ext->clump;
  memcpy(mdb->drXTExtRec, ext->cat.u.fil.filExtRec, sizeof(ExtDataRec));

  mdb->drCTFlSize = cat->cat.u.fil.filPyLen;
  mdb->drCTClpSiz = cat->clump;
  memcpy(mdb->drCTExtRec, cat->cat.u.fil.filExtRec, sizeof(ExtDataRec));

  d_storew(&ptr, mdb->drSigWord);
  d_storel(&ptr, mdb->drCrDate);
  d_storel(&ptr, mdb->drLsMod);
  d_storew(&ptr, mdb->drAtrb);
  d_storew(&ptr, mdb->drNmFls);
  d_storew(&ptr, mdb->drVBMSt);
  d_storew(&ptr, mdb->drAllocPtr);
  d_storew(&ptr, mdb->drNmAlBlks);
  d_storel(&ptr, mdb->drAlBlkSiz);
  d_storel(&ptr, mdb->drClpSiz);
  d_storew(&ptr, mdb->drAlBlSt);
  d_storel(&ptr, mdb->drNxtCNID);
  d_storew(&ptr, mdb->drFreeBks);
  d_stores(&ptr, mdb->drVN, sizeof(mdb->drVN));

  if (ptr - b != 64)
    abort();

  d_storel(&ptr, mdb->drVolBkUp);
  d_storew(&ptr, mdb->drVSeqNum);
  d_storel(&ptr, mdb->drWrCnt);
  d_storel(&ptr, mdb->drXTClpSiz);
  d_storel(&ptr, mdb->drCTClpSiz);
  d_storew(&ptr, mdb->drNmRtDirs);
  d_storel(&ptr, mdb->drFilCnt);
  d_storel(&ptr, mdb->drDirCnt);

  for (i = 0; i < 8; ++i)
    d_storel(&ptr, mdb->drFndrInfo[i]);

  if (ptr - b != 124)
    abort();

  d_storew(&ptr, mdb->drVCSize);
  d_storew(&ptr, mdb->drVBMCSize);
  d_storew(&ptr, mdb->drCtlCSize);
  d_storel(&ptr, mdb->drXTFlSize);

  for (i = 0; i < 3; ++i)
    {
      d_storew(&ptr, mdb->drXTExtRec[i].xdrStABN);
      d_storew(&ptr, mdb->drXTExtRec[i].xdrNumABlks);
    }

  if (ptr - b != 146)
    abort();

  d_storel(&ptr, mdb->drCTFlSize);

  for (i = 0; i < 3; ++i)
    {
      d_storew(&ptr, mdb->drCTExtRec[i].xdrStABN);
      d_storew(&ptr, mdb->drCTExtRec[i].xdrNumABlks);
    }

  if (ptr - b != 162)
    abort();

  if (b_writelb(vol, 2, &b) < 0)
    return -1;
  if (vol->flags & HFS_UPDATE_ALTMDB)
    {
#ifdef APPLE_HYB
      /* "write" alternative MDB to memory copy */
      memcpy(vol->hce->hfs_alt_mdb, &b, sizeof(b));
#else
      if (b_writelb(vol, vol->vlen - 2, &b) < 0)
	return -1;
#endif /* APPLE_HYB */
    }
  vol->flags &= ~(HFS_UPDATE_MDB | HFS_UPDATE_ALTMDB);

  return 0;
}

/*
 * NAME:	low->readvbm()
 * DESCRIPTION:	read the volume bit map into memory
 */
int l_readvbm(hfsvol *vol)
{
  int vbmst = vol->mdb.drVBMSt;
  int vbmsz = (vol->mdb.drNmAlBlks + 4095) / 4096;
  block *bp;

  if (vol->mdb.drAlBlSt - vbmst < vbmsz)
    {
      ERROR(EIO, "volume bitmap collides with volume data");
      return -1;
    }

  bp = ALLOC(block, vbmsz);
  if (bp == 0)
    {
      ERROR(ENOMEM, 0);
      return -1;
    }

  vol->vbm = bp;

  while (vbmsz--)
    {
      if (b_readlb(vol, vbmst++, bp++) < 0)
	{
	  FREE(vol->vbm);
	  vol->vbm = 0;

	  return -1;
	}
    }

  return 0;
}

/*
 * NAME:	low->writevbm()
 * DESCRIPTION:	write the volume bit map to disk
 */
int l_writevbm(hfsvol *vol)
{
  int vbmst = vol->mdb.drVBMSt;
  int vbmsz = (vol->mdb.drNmAlBlks + 4095) / 4096;
  block *bp = vol->vbm;

  while (vbmsz--)
    {
      if (b_writelb(vol, vbmst++, bp++) < 0)
	return -1;
    }

  vol->flags &= ~HFS_UPDATE_VBM;

  return 0;
}
