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

/* APPLE_HYB James Pearson j.pearson@ps.ucl.ac.uk 16/7/97 */

# include <stdlib.h>
# include <unistd.h>
# include <fcntl.h>
# include <errno.h>
# include <string.h>
# include <time.h>
# include <ctype.h>
# include <sys/stat.h>

# include "internal.h"
# include "data.h"
# include "block.h"
# include "low.h"
# include "file.h"
# include "btree.h"
# include "node.h"
# include "record.h"
# include "volume.h"

char *hfs_error = "no error";	/* static error string */

hfsvol *hfs_mounts;		/* linked list of mounted volumes */
hfsvol *hfs_curvol;		/* current volume */

/* High-Level Volume Routines ============================================== */

/*
 * NAME:	hfs->mount()
 * DESCRIPTION:	open an HFS volume; return volume descriptor or 0 (error)
 */
#ifdef APPLE_HYB
hfsvol *hfs_mount(hce_mem *hce, int pnum, int flags)
#else
hfsvol *hfs_mount(char *path, int pnum, int flags)
#endif /* APPLE_HYB */
{
  hfsvol *vol = 0;

#ifndef APPLE_HYB

  struct stat dev;
  /* see if the volume is already mounted */

  if (stat(path, &dev) >= 0)
    {
      struct stat mdev;
      hfsvol *check;

      for (check = hfs_mounts; check; check = check->next)
	{
	  if (fstat(check->fd, &mdev) >= 0 &&
	      mdev.st_dev == dev.st_dev &&
	      mdev.st_ino == dev.st_ino &&
	      (check->pnum == 0 || check->pnum == pnum))
	    {
	      /* verify compatible read/write mode */

	      if (((check->flags & HFS_READONLY) &&
		   ! (flags & O_WRONLY)) ||
		  (! (check->flags & HFS_READONLY) &&
		   (flags & (O_WRONLY | O_RDWR))))
		{
		  vol = check;
		  break;
		}
	    }
	}
    }
#endif /* APPLE_HYB */
  if (vol == 0)
    {
      vol = ALLOC(hfsvol, 1);
      if (vol == 0)
	{
	  ERROR(ENOMEM, 0);
	  return 0;
	}

      vol->flags  = 0;
      vol->pnum   = pnum;
      vol->vstart = 0;
      vol->vlen   = 0;
      vol->lpa    = 0;
      vol->vbm    = 0;
      vol->cwd    = HFS_CNID_ROOTDIR;

      vol->refs   = 0;
      vol->files  = 0;
      vol->dirs   = 0;
      vol->prev   = 0;
      vol->next   = 0;

      vol->ext.map     = 0;
      vol->ext.mapsz   = 0;
      vol->ext.flags   = 0;
      vol->ext.compare = r_compareextkeys;

      vol->cat.map     = 0;
      vol->cat.mapsz   = 0;
      vol->cat.flags   = 0;
      vol->cat.compare = r_comparecatkeys;

      /* open and lock the device */

#ifdef APPLE_HYB
      vol->fd = 3;			/* any +ve number will do? */
      vol->hce = hce;			/* store the extra with the vol info */
#else
      if (flags & (O_WRONLY | O_RDWR))
	{
	  vol->fd = open(path, O_RDWR);
	  if (vol->fd >= 0 && l_lockvol(vol) < 0)
	    {
	      close(vol->fd);
	      vol->fd = -2;
	    }
	}

      if (! (flags & (O_WRONLY | O_RDWR)) ||
	  (vol->fd < 0 &&
	   (errno == EROFS || errno == EACCES || errno == EAGAIN) &&
	   (flags & O_RDWR)))
	{
	  vol->flags |= HFS_READONLY;
	  vol->fd = open(path, O_RDONLY);
	  if (vol->fd >= 0 && l_lockvol(vol) < 0)
	    {
	      close(vol->fd);
	      vol->fd = -2;
	    }
	}

      if (vol->fd < 0)
	{
	  if (vol->fd != -2)
	    ERROR(errno, "error opening device");

	  v_destruct(vol);

	  return 0;
	}
#endif /* APPLE_HYB */

      /* find out what kind of media this is and read the MDB */

      if (l_readblock0(vol) < 0 ||
	  l_readmdb(vol) < 0)
	{
#ifndef APPLE_HYB
	  close(vol->fd);
	  v_destruct(vol);
#endif /* APPLE_HYB */
	  return 0;
	}

      /* verify this is an HFS volume */

      if (vol->mdb.drSigWord != 0x4244)
	{
#ifndef APPLE_HYB
	  close(vol->fd);
#endif /* APPLE_HYB */
	  v_destruct(vol);

	  ERROR(EINVAL, "not a Macintosh HFS volume");
	  return 0;
	}

      /* do minimal consistency checks */

      if (vol->mdb.drAlBlkSiz % HFS_BLOCKSZ != 0)
	{
#ifndef APPLE_HYB
	  close(vol->fd);
#endif /* APPLE_HYB */
	  v_destruct(vol);

	  ERROR(EINVAL, "bad volume allocation block size");
	  return 0;
	}

      if (vol->vlen == 0)
	vol->vlen = vol->mdb.drAlBlSt +
	  vol->mdb.drNmAlBlks * (vol->mdb.drAlBlkSiz / HFS_BLOCKSZ) + 2;

      /* read the volume bitmap and extents/catalog B*-tree headers */

      if (l_readvbm(vol) < 0 ||
	  bt_readhdr(&vol->ext) < 0 ||
	  bt_readhdr(&vol->cat) < 0)
	{
#ifndef APPLE_HYB
	  close(vol->fd);
#endif /* APPLE_HYB */
	  v_destruct(vol);
	  return 0;
	}

      if (! (vol->mdb.drAtrb & HFS_ATRB_UMOUNTED))
	{
	  /* volume was not cleanly unmounted; scavenge free-space */

	  if (v_scavenge(vol) < 0)
	    {
#ifndef APPLE_HYB
	      close(vol->fd);
#endif /* APPLE_HYB */
	      v_destruct(vol);
	      return 0;
	    }
	}

      if (vol->flags & HFS_READONLY)
	vol->mdb.drAtrb |= HFS_ATRB_HLOCKED;
      else
	vol->mdb.drAtrb &= ~HFS_ATRB_HLOCKED;

      vol->prev = 0;
      vol->next = hfs_mounts;

      if (hfs_mounts)
	hfs_mounts->prev = vol;

      hfs_mounts = vol;
    }

  ++vol->refs;

  return hfs_curvol = vol;
}

/*
 * NAME:	hfs->flush()
 * DESCRIPTION:	flush all pending changes to an HFS volume
 */
int hfs_flush(hfsvol *vol)
{
  hfsfile *file;

  if (v_getvol(&vol) < 0)
    return -1;

  for (file = vol->files; file; file = file->next)
    {
      if (f_flush(file) < 0)
	return -1;
    }

  if (v_flush(vol, 0) < 0)
    return -1;

  return 0;
}

/*
 * NAME:	hfs->flushall()
 * DESCRIPTION:	flush all pending changes to all mounted HFS volumes
 */
void hfs_flushall(void)
{
  hfsvol *vol;

  for (vol = hfs_mounts; vol; vol = vol->next)
    hfs_flush(vol);
}

/*
 * NAME:	hfs->umount()
 * DESCRIPTION:	close an HFS volume
 */
#ifdef APPLE_HYB
/* extra argument used to alter the position of the extents/catalog files */
int hfs_umount(hfsvol *vol, long end)
#else
int hfs_umount(hfsvol *vol)
#endif /* APPLE_HYB */
{
  int result = 0;

  if (v_getvol(&vol) < 0)
    return -1;

  if (--vol->refs)
    return v_flush(vol, 0);

  /* close all open files and directories */

  while (vol->files)
#ifdef APPLE_HYB
    hfs_close(vol->files, 0, 0);
#else
    hfs_close(vol->files);
#endif /* APPLE_HYB */

  while (vol->dirs)
    hfs_closedir(vol->dirs);

#ifdef APPLE_HYB
  if (end)
  {
	/*  move extents and catalog to end of volume ... */
  	long vbmsz = (vol->vlen / vol->lpa + 4095) / 4096;

	/* we are adding this "files" to the end of the ISO volume,
	   so calculate this address in HFS speak ... */
/*	end -= vol->mdb.drAlBlSt; */
	end -= (vol->mdb.drAlBlSt + vol->hce->hfs_map_size);
	end /= vol->lpa;

	/* catalog file ... */
	vol->ext.f.cat.u.fil.filExtRec[0].xdrStABN = end;
 	vol->mdb.drXTExtRec[0].xdrStABN = end;

	/* move postition to start of extents file */
	end += vol->cat.f.cat.u.fil.filExtRec[0].xdrStABN;

	/* extents file ... */
	vol->cat.f.cat.u.fil.filExtRec[0].xdrStABN = end;
	vol->mdb.drCTExtRec[0].xdrStABN = end;

	/* the volume bitmap is wrong as we have "moved" files
	   about - simple just set the whole lot (it's a readonly volume
	   anyway!) */
	memset(vol->vbm, 0xff, vbmsz*HFS_BLOCKSZ);

	/* set the free blocks to zero */
	vol->mdb.drFreeBks = 0;

	/* flag changes for flushing later */
	vol->flags |= HFS_UPDATE_VBM;
	vol->flags |= HFS_UPDATE_MDB;
	vol->mdb.drAtrb |= HFS_ATRB_HLOCKED;
	vol->ext.flags |= HFS_UPDATE_BTHDR;
	vol->cat.flags |= HFS_UPDATE_BTHDR;
  }
#endif /* APPLE_HYB */

  if (v_flush(vol, 1) < 0)
    result = -1;

#ifndef APPLE_HYB
  if (close(vol->fd) < 0 && result == 0)
    {
      ERROR(errno, "error closing device");
      result = -1;
    }
#endif /* APPLE_HYB */

  if (vol->prev)
    vol->prev->next = vol->next;
  if (vol->next)
    vol->next->prev = vol->prev;

  if (vol == hfs_mounts)
    hfs_mounts = vol->next;
  if (vol == hfs_curvol)
    hfs_curvol = 0;

  v_destruct(vol);

  return result;
}

/*
 * NAME:	hfs->umountall()
 * DESCRIPTION:	unmount all mounted volumes
 */
void hfs_umountall(void)
{
  while (hfs_mounts)
#ifdef APPLE_HYB
    continue;
#else
    hfs_umount(hfs_mounts);
#endif /* APPLE_HYB */
}

/*
 * NAME:	hfs->getvol()
 * DESCRIPTION:	return a pointer to a mounted volume
 */
hfsvol *hfs_getvol(char *name)
{
  hfsvol *vol;

  if (name == 0)
    return hfs_curvol;

  for (vol = hfs_mounts; vol; vol = vol->next)
    {
      if (d_relstring(name, vol->mdb.drVN) == 0)
	return vol;
    }

  return 0;
}

/*
 * NAME:	hfs->setvol()
 * DESCRIPTION:	change the current volume
 */
void hfs_setvol(hfsvol *vol)
{
  hfs_curvol = vol;
}

/*
 * NAME:	hfs->vstat()
 * DESCRIPTION:	return volume statistics
 */
int hfs_vstat(hfsvol *vol, hfsvolent *ent)
{
  if (v_getvol(&vol) < 0)
    return -1;

  strcpy(ent->name, vol->mdb.drVN);

  ent->flags     = (vol->flags & HFS_READONLY) ? HFS_ISLOCKED : 0;
  ent->totbytes  = vol->mdb.drNmAlBlks * vol->mdb.drAlBlkSiz;
  ent->freebytes = vol->mdb.drFreeBks  * vol->mdb.drAlBlkSiz;
  ent->crdate    = d_toutime(vol->mdb.drCrDate);
  ent->mddate    = d_toutime(vol->mdb.drLsMod);

  return 0;
}

/*
 * NAME:	hfs->format()
 * DESCRIPTION:	write a new filesystem
 */
#ifdef APPLE_HYB
int hfs_format(hce_mem *hce, int pnum, char *vname)
#else
int hfs_format(char *path, int pnum, char *vname)
#endif /* APPLE_HYB */
{
  hfsvol vol;
  btree *ext = &vol.ext;
  btree *cat = &vol.cat;
  unsigned int vbmsz;
  int i, result = 0;
  block vbm[16];
  char *map;

  if (strchr(vname, ':'))
    {
      ERROR(EINVAL, "volume name may not contain colons");
      return -1;
    }

  i = strlen(vname);
  if (i < 1 || i > HFS_MAX_VLEN)
    {
      ERROR(EINVAL, "volume name must be 1-27 chars");
      return -1;
    }

  vol.flags  = 0;
  vol.pnum   = pnum;
  vol.vstart = 0;
  vol.vlen   = 0;
  vol.lpa    = 0;
  vol.vbm    = vbm;
  vol.cwd    = HFS_CNID_ROOTDIR;

  vol.refs   = 0;
  vol.files  = 0;
  vol.dirs   = 0;
  vol.prev   = 0;
  vol.next   = 0;

#ifndef APPLE_HYB
  vol.fd = open(path, O_RDWR);
  if (vol.fd < 0)
    {
      ERROR(errno, "error opening device for writing");
      return -1;
    }

  if (l_lockvol(&vol) < 0)
    {
      close(vol.fd);
      return -1;
    }
#endif /* APPLE_HYB */
  if (pnum > 0)
    {
      if (l_readpm(&vol) < 0)
	{
	  close(vol.fd);
	  return -1;
	}
    }
  else  /* determine size of entire device */
    {
#ifdef APPLE_HYB
      vol.vlen = hce->hfs_vol_size/HFS_BLOCKSZ;
#else
      unsigned long low, high, mid;
      block b;

      for (low = 0, high = 2879; b_readlb(&vol, high, &b) >= 0; high *= 2)
	low = high;

      while (low < high - 1)
	{
	  mid = (low + high) / 2;

	  if (b_readlb(&vol, mid, &b) < 0)
	    high = mid;
	  else
	    low = mid;
	}

      vol.vlen = low + 1;
#endif /* APPLE_HYB */
    }

  if (vol.vlen < 800 * 1024 / HFS_BLOCKSZ)
    {
#ifndef APPLE_HYB
      close(vol.fd);
#endif /* APPLE_HYB */

      ERROR(EINVAL, "volume size must be >= 800K");
      return -1;
    }

  /* initialize volume geometry */

#ifdef APPLE_HYB
  /* force lpa to be a multiple of 4 (i.e. 2048/512) - as calculated 
     earlier */
  vol.lpa = hce->Csize/HFS_BLOCKSZ;
#else
  vol.lpa = 1 + vol.vlen / 65536;
#endif /* APPLE_HYB */

  vbmsz = (vol.vlen / vol.lpa + 4095) / 4096;

  vol.mdb.drSigWord  = 0x4244;
  vol.mdb.drCrDate   = d_tomtime(time(0));
  vol.mdb.drLsMod    = vol.mdb.drCrDate;
  vol.mdb.drAtrb     = 0;
  vol.mdb.drNmFls    = 0;
  vol.mdb.drVBMSt    = 3;
  vol.mdb.drAllocPtr = 0;
  vol.mdb.drNmAlBlks = (vol.vlen - 5 - vbmsz) / vol.lpa;
  vol.mdb.drAlBlkSiz = vol.lpa * HFS_BLOCKSZ;
  vol.mdb.drClpSiz   = vol.mdb.drAlBlkSiz * 4;
  vol.mdb.drAlBlSt   = 3 + vbmsz;
#ifdef APPLE_HYB
  /* round up start block to a muliple of lpa - important later */
/*vol.mdb.drAlBlSt   = ((vol.mdb.drAlBlSt + vol.lpa -  1) / vol.lpa) * vol.lpa; 
*/
  /* take in accout alignment of files wrt HFS volume start i.e we want
     drAlBlSt plus hfs_map_size to me a multiple of lpa */
  vol.mdb.drAlBlSt   = ((vol.mdb.drAlBlSt + hce->hfs_map_size + vol.lpa -  1) / vol.lpa) * vol.lpa; 
  vol.mdb.drAlBlSt   -= hce->hfs_map_size;
#endif /* APPLE_HYB */
  vol.mdb.drNxtCNID  = HFS_CNID_ROOTDIR;  /* modified later */
  vol.mdb.drFreeBks  = vol.mdb.drNmAlBlks;

  strcpy(vol.mdb.drVN, vname);

  vol.mdb.drVolBkUp  = 0;
  vol.mdb.drVSeqNum  = 0;
  vol.mdb.drWrCnt    = 0;
  vol.mdb.drXTClpSiz = vol.mdb.drNmAlBlks / 128 * vol.mdb.drAlBlkSiz;
#ifdef APPLE_HYB
  /* adjust size of extents/catalog upwards as we may have rounded up
     allocation size */
  i = 1 + vol.vlen / 65536;

  vol.mdb.drXTClpSiz = (vol.mdb.drXTClpSiz * vol.lpa) / i;

  /* round up to lpa size */
  vol.mdb.drXTClpSiz = ((vol.mdb.drXTClpSiz + vol.mdb.drAlBlkSiz - 1) /
				vol.mdb.drAlBlkSiz) * vol.mdb.drAlBlkSiz;

  /* ignore above, use what we have already calculated ... */
  vol.mdb.drXTClpSiz = hce->XTCsize;

  /* make Catalog file CTC (default twice) as big - prevents further allocation
     later which we don't want - this seems to work OK ... */
/*vol.mdb.drCTClpSiz = vol.mdb.drXTClpSiz * CTC; */
  vol.mdb.drCTClpSiz = vol.mdb.drXTClpSiz * hce->ctc_size;

  /* we want to put things at the end of the volume later, so we'll
     cheat here ... shouldn't matter, as we only need the volume read
     only anyway (we won't be adding files later!) - leave some extra
     space for the alternative MDB (in the last allocation block) */

  vol.mdb.drNmAlBlks = vol.mdb.drFreeBks = vol.vlen / vol.lpa - 1;
#else
  vol.mdb.drCTClpSiz = vol.mdb.drXTClpSiz;
#endif /* APPLE_HYB */
  vol.mdb.drNmRtDirs = 0;
  vol.mdb.drFilCnt   = 0;
  vol.mdb.drDirCnt   = -1;  /* incremented when root folder is created */

  for (i = 0; i < 8; ++i)
    vol.mdb.drFndrInfo[i] = 0;

  vol.mdb.drVCSize   = 0;
  vol.mdb.drVBMCSize = 0;
  vol.mdb.drCtlCSize = 0;

  vol.mdb.drXTFlSize = 0;
  vol.mdb.drCTFlSize = 0;

  for (i = 0; i < 3; ++i)
    {
      vol.mdb.drXTExtRec[i].xdrStABN    = 0;
      vol.mdb.drXTExtRec[i].xdrNumABlks = 0;

      vol.mdb.drCTExtRec[i].xdrStABN    = 0;
      vol.mdb.drCTExtRec[i].xdrNumABlks = 0;
    }

  /* initialize volume bitmap */

  memset(vol.vbm, 0, sizeof(vbm));

#ifdef APPLE_HYB
  /* We don't want to write anything out at the moment, so we allocate
     memory to hold the HFS "header" info and extents/catalog files.
     Any reads/writes from/to these parts of the volume are trapped and
     stored in memory. */

  /* blocks up to the first unallocated block == HFS "header" info
     This will be placed in the first 32kb of the ISO volume later */
  hce->hfs_hdr_size = vol.mdb.drAlBlSt;

  /* size of the extents and catalog files. This will be added
     to the end of the ISO volume later */
  hce->hfs_ce_size =  vol.mdb.drXTClpSiz + vol.mdb.drCTClpSiz;

  /* we also allocate space for the Desktop file and the alternative
     MDB while we're here */
  FREE(hce->hfs_ce);
  hce->hfs_ce = ALLOC(unsigned char, (hce->hfs_ce_size + vol.mdb.drClpSiz
					+ vol.mdb.drAlBlkSiz));

  /* allocate memory for the map and hdr */
  FREE(hce->hfs_map);
  hce->hfs_map = ALLOC(unsigned char, ((hce->hfs_hdr_size + hce->hfs_map_size)
			*HFS_BLOCKSZ));

  if (hce->hfs_ce == 0 || hce->hfs_map == 0)
    {
      ERROR(ENOMEM, 0);
      result = -1;
    }

  /* hfs_hdr is immediately after the hfs_map */
  hce->hfs_hdr = hce->hfs_map + hce->hfs_map_size*HFS_BLOCKSZ;

  /* size needed in HFS_BLOCKSZ blocks for later use */
  hce->hfs_ce_size /= HFS_BLOCKSZ;

  /* note size of Desktop file */
  hce->hfs_dt_size = vol.mdb.drClpSiz/HFS_BLOCKSZ;

  /* total size of catalog/extents and desktop */
  hce->hfs_tot_size = hce->hfs_ce_size + hce->hfs_dt_size;

  /* alternative MDB in the last alocation block */
  hce->hfs_alt_mdb = hce->hfs_ce + hce->hfs_tot_size*HFS_BLOCKSZ;

  /* add the MDB to the total size */
  hce->hfs_tot_size += vol.lpa;

  /* store this info in the volume info */
  vol.hce = hce;

#endif /* APPLE_HYB */

  /* create extents overflow file */

  ext->f.vol   = &vol;
  ext->f.parid = 0;
  strcpy(ext->f.name, "extents overflow");

  ext->f.cat.cdrType          = cdrFilRec;
  /* ext->f.cat.cdrResrv2 */
  ext->f.cat.u.fil.filFlags   = 0;
  ext->f.cat.u.fil.filTyp     = 0;
  /* ext->f.cat.u.fil.filUsrWds */
  ext->f.cat.u.fil.filFlNum   = HFS_CNID_EXT;
  ext->f.cat.u.fil.filStBlk   = 0;
  ext->f.cat.u.fil.filLgLen   = 0;
  ext->f.cat.u.fil.filPyLen   = 0;
  ext->f.cat.u.fil.filRStBlk  = 0;
  ext->f.cat.u.fil.filRLgLen  = 0;
  ext->f.cat.u.fil.filRPyLen  = 0;
  ext->f.cat.u.fil.filCrDat   = vol.mdb.drCrDate;
  ext->f.cat.u.fil.filMdDat   = vol.mdb.drLsMod;
  ext->f.cat.u.fil.filBkDat   = 0;
  /* ext->f.cat.u.fil.filFndrInfo */
  ext->f.cat.u.fil.filClpSize = 0;

  for (i = 0; i < 3; ++i)
    {
      ext->f.cat.u.fil.filExtRec[i].xdrStABN     = 0;
      ext->f.cat.u.fil.filExtRec[i].xdrNumABlks  = 0;

      ext->f.cat.u.fil.filRExtRec[i].xdrStABN    = 0;
      ext->f.cat.u.fil.filRExtRec[i].xdrNumABlks = 0;
    }
  /* ext->f.cat.u.fil.filResrv */
  f_selectfork(&ext->f, 0);

  ext->f.clump = vol.mdb.drXTClpSiz;
  ext->f.flags = 0;

  ext->f.prev = ext->f.next = 0;

  n_init(&ext->hdrnd, ext, ndHdrNode, 0);

  ext->hdrnd.nnum       = 0;
  ext->hdrnd.nd.ndNRecs = 3;
  ext->hdrnd.roff[1]    = 0x078;
  ext->hdrnd.roff[2]    = 0x0f8;
  ext->hdrnd.roff[3]    = 0x1f8;

  memset(HFS_NODEREC(ext->hdrnd, 1), 0, 128);

  ext->hdr.bthDepth    = 0;
  ext->hdr.bthRoot     = 0;
  ext->hdr.bthNRecs    = 0;
  ext->hdr.bthFNode    = 0;
  ext->hdr.bthLNode    = 0;
  ext->hdr.bthNodeSize = HFS_BLOCKSZ;
  ext->hdr.bthKeyLen   = 0x07;
  ext->hdr.bthNNodes   = 0;
  ext->hdr.bthFree     = 0;
  for (i = 0; i < 76; ++i)
    ext->hdr.bthResv[i] = 0;

  map = ALLOC(char, HFS_MAP1SZ);
  if (map == 0)
    {
      if (result == 0)
	{
	  ERROR(ENOMEM, 0);
	  result = -1;
	}
    }
  else
    {
      memset(map, 0, HFS_MAP1SZ);
      BMSET(map, 0);
    }

  ext->map     = map;
  ext->mapsz   = HFS_MAP1SZ;
  ext->flags   = HFS_UPDATE_BTHDR;
  ext->compare = r_compareextkeys;

  if (result == 0 && bt_space(ext, 1) < 0)
    result = -1;

  --ext->hdr.bthFree;

  /* create catalog file */

  cat->f.vol   = &vol;
  cat->f.parid = 0;
  strcpy(cat->f.name, "catalog");

  cat->f.cat.cdrType          = cdrFilRec;
  /* cat->f.cat.cdrResrv2 */
  cat->f.cat.u.fil.filFlags   = 0;
  cat->f.cat.u.fil.filTyp     = 0;
  /* cat->f.cat.u.fil.filUsrWds */
  cat->f.cat.u.fil.filFlNum   = HFS_CNID_CAT;
  cat->f.cat.u.fil.filStBlk   = 0;
  cat->f.cat.u.fil.filLgLen   = 0;
  cat->f.cat.u.fil.filPyLen   = 0;
  cat->f.cat.u.fil.filRStBlk  = 0;
  cat->f.cat.u.fil.filRLgLen  = 0;
  cat->f.cat.u.fil.filRPyLen  = 0;
  cat->f.cat.u.fil.filCrDat   = vol.mdb.drCrDate;
  cat->f.cat.u.fil.filMdDat   = vol.mdb.drLsMod;
  cat->f.cat.u.fil.filBkDat   = 0;
  /* cat->f.cat.u.fil.filFndrInfo */
  cat->f.cat.u.fil.filClpSize = 0;

  for (i = 0; i < 3; ++i)
    {
      cat->f.cat.u.fil.filExtRec[i].xdrStABN     = 0;
      cat->f.cat.u.fil.filExtRec[i].xdrNumABlks  = 0;

      cat->f.cat.u.fil.filRExtRec[i].xdrStABN    = 0;
      cat->f.cat.u.fil.filRExtRec[i].xdrNumABlks = 0;
    }
  /* cat->f.cat.u.fil.filResrv */
  f_selectfork(&cat->f, 0);

  cat->f.clump = vol.mdb.drCTClpSiz;
  cat->f.flags = 0;

  cat->f.prev = cat->f.next = 0;

  n_init(&cat->hdrnd, cat, ndHdrNode, 0);

  cat->hdrnd.nnum       = 0;
  cat->hdrnd.nd.ndNRecs = 3;
  cat->hdrnd.roff[1]    = 0x078;
  cat->hdrnd.roff[2]    = 0x0f8;
  cat->hdrnd.roff[3]    = 0x1f8;

  memset(HFS_NODEREC(cat->hdrnd, 1), 0, 128);

  cat->hdr.bthDepth    = 0;
  cat->hdr.bthRoot     = 0;
  cat->hdr.bthNRecs    = 0;
  cat->hdr.bthFNode    = 0;
  cat->hdr.bthLNode    = 0;
  cat->hdr.bthNodeSize = HFS_BLOCKSZ;
  cat->hdr.bthKeyLen   = 0x25;
  cat->hdr.bthNNodes   = 0;
  cat->hdr.bthFree     = 0;
  for (i = 0; i < 76; ++i)
    cat->hdr.bthResv[i] = 0;

  map = ALLOC(char, HFS_MAP1SZ);
  if (map == 0)
    {
      if (result == 0)
	{
	  ERROR(ENOMEM, 0);
	  result = -1;
	}
    }
  else
    {
      memset(map, 0, HFS_MAP1SZ);
      BMSET(map, 0);
    }

  cat->map     = map;
  cat->mapsz   = HFS_MAP1SZ;
  cat->flags   = HFS_UPDATE_BTHDR;
  cat->compare = r_comparecatkeys;

  if (result == 0 && bt_space(cat, 1) < 0)
    result = -1;

  --cat->hdr.bthFree;

  /* create root folder */

  if (result == 0 && v_newfolder(&vol, HFS_CNID_ROOTPAR, vname) < 0)
    result = -1;

  vol.mdb.drNxtCNID = 16;

  /* finish up */

  if (result == 0)
    {
      block b;

      /* write boot blocks */

      memset(&b, 0, sizeof(b));
      b_writelb(&vol, 0, &b);
      b_writelb(&vol, 1, &b);

      /* flush other disk state */

      vol.flags |= HFS_UPDATE_MDB | HFS_UPDATE_ALTMDB | HFS_UPDATE_VBM;

      if (v_flush(&vol, 1) < 0)
	result = -1;
    }
#ifndef APPLE_HYB
  if (close(vol.fd) < 0 && result == 0)
    {
      ERROR(errno, "error closing device");
      result = -1;
    }
#endif /* APPLE_HYB */
  FREE(vol.ext.map);
  FREE(vol.cat.map);

  return result;
}

/* High-Level Directory Routines =========================================== */

/*
 * NAME:	hfs->chdir()
 * DESCRIPTION:	change current HFS directory
 */
int hfs_chdir(hfsvol *vol, char *path)
{
  CatDataRec data;

  if (v_getvol(&vol) < 0 ||
      v_resolve(&vol, path, &data, 0, 0, 0) <= 0)
    return -1;

  if (data.cdrType != cdrDirRec)
    {
      ERROR(ENOTDIR, 0);
      return -1;
    }

  vol->cwd = data.u.dir.dirDirID;

  return 0;
}

/*
 * NAME:	hfs->getcwd()
 * DESCRIPTION:	return the current working directory ID
 */
long hfs_getcwd(hfsvol *vol)
{
  if (v_getvol(&vol) < 0)
    return 0;

  return vol->cwd;
}

/*
 * NAME:	hfs->setcwd()
 * DESCRIPTION:	set the current working directory ID
 */
int hfs_setcwd(hfsvol *vol, long id)
{
  if (v_getvol(&vol) < 0)
    return -1;

  if (id == vol->cwd)
    return 0;

  /* make sure the directory exists */

  if (v_getdthread(vol, id, 0, 0) <= 0)
    return -1;

  vol->cwd = id;

  return 0;
}

/*
 * NAME:	hfs->dirinfo()
 * DESCRIPTION:	given a directory ID, return its (name and) parent ID
 */
int hfs_dirinfo(hfsvol *vol, long *id, char *name)
{
  CatDataRec thread;

  if (v_getvol(&vol) < 0 ||
      v_getdthread(vol, *id, &thread, 0) <= 0)
    return -1;

  *id = thread.u.dthd.thdParID;

  if (name)
    strcpy(name, thread.u.dthd.thdCName);

  return 0;
}

/*
 * NAME:	hfs->opendir()
 * DESCRIPTION:	prepare to read the contents of a directory
 */
hfsdir *hfs_opendir(hfsvol *vol, char *path)
{
  hfsdir *dir;
  CatKeyRec key;
  CatDataRec data;
  unsigned char pkey[HFS_CATKEYLEN];

  if (v_getvol(&vol) < 0)
    return 0;

  dir = ALLOC(hfsdir, 1);
  if (dir == 0)
    {
      ERROR(ENOMEM, 0);
      return 0;
    }

  dir->vol = vol;

  if (*path == 0)
    {
      /* meta-directory containing root dirs from all mounted volumes */

      dir->dirid = 0;
      dir->vptr  = hfs_mounts;
    }
  else
    {
      if (v_resolve(&vol, path, &data, 0, 0, 0) <= 0)
	{
	  FREE(dir);
	  return 0;
	}

      if (data.cdrType != cdrDirRec)
	{
	  FREE(dir);
	  ERROR(ENOTDIR, 0);
	  return 0;
	}

      dir->dirid = data.u.dir.dirDirID;
      dir->vptr  = 0;

      r_makecatkey(&key, dir->dirid, "");
      r_packcatkey(&key, pkey, 0);

      if (bt_search(&vol->cat, pkey, &dir->n) <= 0)
	{
	  FREE(dir);
	  return 0;
	}
    }

  dir->prev = 0;
  dir->next = vol->dirs;

  if (vol->dirs)
    vol->dirs->prev = dir;

  vol->dirs = dir;

  return dir;
}

/*
 * NAME:	hfs->readdir()
 * DESCRIPTION:	return the next entry in the directory
 */
int hfs_readdir(hfsdir *dir, hfsdirent *ent)
{
  CatKeyRec key;
  CatDataRec data;
  unsigned char *ptr;

  if (dir->dirid == 0)
    {
      hfsvol *vol;
      char cname[HFS_MAX_FLEN + 1];

      for (vol = hfs_mounts; vol; vol = vol->next)
	{
	  if (vol == dir->vptr)
	    break;
	}

      if (vol == 0)
	{
	  ERROR(ENOENT, "no more entries");
	  return -1;
	}

      if (v_getdthread(vol, HFS_CNID_ROOTDIR, &data, 0) <= 0 ||
	  v_catsearch(vol, HFS_CNID_ROOTPAR, data.u.dthd.thdCName,
		      &data, cname, 0) < 0)
	return -1;

      r_unpackdirent(HFS_CNID_ROOTPAR, cname, &data, ent);

      dir->vptr = vol->next;

      return 0;
    }

  if (dir->n.rnum == -1)
    {
      ERROR(ENOENT, "no more entries");
      return -1;
    }

  while (1)
    {
      ++dir->n.rnum;

      while (dir->n.rnum >= dir->n.nd.ndNRecs)
	{
	  dir->n.nnum = dir->n.nd.ndFLink;
	  if (dir->n.nnum == 0)
	    {
	      dir->n.rnum = -1;
	      ERROR(ENOENT, "no more entries");
	      return -1;
	    }

	  if (bt_getnode(&dir->n) < 0)
	    {
	      dir->n.rnum = -1;
	      return -1;
	    }

	  dir->n.rnum = 0;
	}

      ptr = HFS_NODEREC(dir->n, dir->n.rnum);

      r_unpackcatkey(ptr, &key);

      if (key.ckrParID != dir->dirid)
	{
	  dir->n.rnum = -1;
	  ERROR(ENOENT, "no more entries");
	  return -1;
	}

      r_unpackcatdata(HFS_RECDATA(ptr), &data);

      switch (data.cdrType)
	{
	case cdrDirRec:
	case cdrFilRec:
	  r_unpackdirent(key.ckrParID, key.ckrCName, &data, ent);
	  return 0;

	case cdrThdRec:
	case cdrFThdRec:
	  break;

	default:
	  dir->n.rnum = -1;

	  ERROR(EIO, "unexpected directory entry found");
	  return -1;
	}
    }
}

/*
 * NAME:	hfs->closedir()
 * DESCRIPTION:	stop reading a directory
 */
int hfs_closedir(hfsdir *dir)
{
  hfsvol *vol = dir->vol;

  if (dir->prev)
    dir->prev->next = dir->next;
  if (dir->next)
    dir->next->prev = dir->prev;
  if (dir == vol->dirs)
    vol->dirs = dir->next;

  FREE(dir);

  return 0;
}

/* High-Level File Routines ================================================ */

/*
 * NAME:	hfs->open()
 * DESCRIPTION:	prepare a file for I/O
 */
hfsfile *hfs_open(hfsvol *vol, char *path)
{
  hfsfile *file;

  if (v_getvol(&vol) < 0)
    return 0;

  file = ALLOC(hfsfile, 1);
  if (file == 0)
    {
      ERROR(ENOMEM, 0);
      return 0;
    }

  if (v_resolve(&vol, path, &file->cat, &file->parid, file->name, 0) <= 0)
    {
      FREE(file);
      return 0;
    }

  if (file->cat.cdrType != cdrFilRec)
    {
      FREE(file);
      ERROR(EISDIR, 0);
      return 0;
    }

  file->vol   = vol;
  file->clump = file->cat.u.fil.filClpSize;
  file->flags = 0;

  f_selectfork(file, 0);

  file->prev = 0;
  file->next = vol->files;

  if (vol->files)
    vol->files->prev = file;

  vol->files = file;

  return file;
}

/*
 * NAME:	hfs->setfork()
 * DESCRIPTION:	select file fork for I/O operations
 */
int hfs_setfork(hfsfile *file, int fork)
{
  int result = 0;

  if (! (file->vol->flags & HFS_READONLY) &&
      f_trunc(file) < 0)
    result = -1;

  f_selectfork(file, fork);

  return result;
}

/*
 * NAME:	hfs->getfork()
 * DESCRIPTION:	return the current fork for I/O operations
 */
int hfs_getfork(hfsfile *file)
{
  return file->fork != fkData;
}

/*
 * NAME:	hfs->read()
 * DESCRIPTION:	read from an open file
 */
long hfs_read(hfsfile *file, void *buf, unsigned long len)
{
  unsigned long *lglen, count;
  unsigned char *ptr = buf;

  f_getptrs(file, &lglen, 0, 0);

  if (file->pos + len > *lglen)
    len = *lglen - file->pos;

  count = len;
  while (count)
    {
      block b;
      unsigned long bnum, offs, chunk;

      bnum  = file->pos / HFS_BLOCKSZ;
      offs  = file->pos % HFS_BLOCKSZ;

      chunk = HFS_BLOCKSZ - offs;
      if (chunk > count)
	chunk = count;

      if (f_getblock(file, bnum, &b) < 0)
	return -1;

      memcpy(ptr, b + offs, chunk);
      ptr += chunk;

      file->pos += chunk;
      count     -= chunk;
    }

  return len;
}

/*
 * NAME:	hfs->write()
 * DESCRIPTION:	write to an open file
 */
long hfs_write(hfsfile *file, void *buf, unsigned long len)
{
  unsigned long *lglen, *pylen, count;
  unsigned char *ptr = buf;

  if (file->vol->flags & HFS_READONLY)
    {
      ERROR(EROFS, 0);
      return -1;
    }

  f_getptrs(file, &lglen, &pylen, 0);

  count = len;

  /* set flag to update (at least) the modification time */

  if (count)
    {
      file->cat.u.fil.filMdDat = d_tomtime(time(0));
      file->flags |= HFS_UPDATE_CATREC;
    }

  while (count)
    {
      block b;
      unsigned long bnum, offs, chunk;

      bnum  = file->pos / HFS_BLOCKSZ;
      offs  = file->pos % HFS_BLOCKSZ;

      chunk = HFS_BLOCKSZ - offs;
      if (chunk > count)
	chunk = count;

      if (file->pos + chunk > *pylen)
	{
	  if (bt_space(&file->vol->ext, 1) < 0 ||
	      f_alloc(file) < 0)
	    return -1;
	}
#ifndef APPLE_HYB
      /* Ignore this part as we are always writing new files to an empty disk
	 i.e. offs will always be 0 */

      if (offs > 0 || chunk < HFS_BLOCKSZ)
	{
	  if (f_getblock(file, bnum, &b) < 0)
	    return -1;
	}
#endif /* APPLE_HYB */
      memcpy(b + offs, ptr, chunk);
      ptr += chunk;

      if (f_putblock(file, bnum, &b) < 0)
	return -1;

      file->pos += chunk;
      count     -= chunk;

      if (file->pos > *lglen)
	*lglen = file->pos;
    }

  return len;
}

/*
 * NAME:	hfs->truncate()
 * DESCRIPTION:	truncate an open file
 */
int hfs_truncate(hfsfile *file, unsigned long len)
{
  unsigned long *lglen;

  f_getptrs(file, &lglen, 0, 0);

  if (*lglen > len)
    {
      if (file->vol->flags & HFS_READONLY)
	{
	  ERROR(EROFS, 0);
	  return -1;
	}

      *lglen = len;

      file->cat.u.fil.filMdDat = d_tomtime(time(0));
      file->flags |= HFS_UPDATE_CATREC;

      if (file->pos > len)
	file->pos = len;
    }

  return 0;
}

/*
 * NAME:	hfs->lseek()
 * DESCRIPTION:	change file seek pointer
 */
long hfs_lseek(hfsfile *file, long offset, int from)
{
  unsigned long *lglen;
  long newpos;

  f_getptrs(file, &lglen, 0, 0);

  switch (from)
    {
    case SEEK_SET:
      newpos = offset;
      break;

    case SEEK_CUR:
      newpos = file->pos + offset;
      break;

    case SEEK_END:
      newpos = *lglen + offset;
      break;

    default:
      ERROR(EINVAL, 0);
      return -1;
    }

  if (newpos < 0)
    newpos = 0;
  else if (newpos > *lglen)
    newpos = *lglen;

  file->pos = newpos;

  return newpos;
}

/*
 * NAME:	hfs->close()
 * DESCRIPTION:	close a file
 */
#ifdef APPLE_HYB
/* extra args are used to set the start of the forks in the ISO volume */
int hfs_close(hfsfile *file, long dext, long rext)
{
  int offset;
#else
int hfs_close(hfsfile *file)
{
#endif /* APPLE_HYB */
  hfsvol *vol = file->vol;
  int result = 0;

  if (f_trunc(file) < 0 ||
      f_flush(file) < 0)
    result = -1;

#ifdef APPLE_HYB
  /* "start" of file is relative to the first available block */
  offset = vol->hce->hfs_hdr_size + vol->hce->hfs_map_size;
  /* update the "real" starting extent and re-flush the file */
  if (dext)
    file->cat.u.fil.filExtRec[0].xdrStABN = (dext - offset)/vol->lpa;

  if (rext)
    file->cat.u.fil.filRExtRec[0].xdrStABN = (rext - offset)/vol->lpa;

  if (dext || rext)
    file->flags |= HFS_UPDATE_CATREC;

  if (f_flush(file) < 0)
    result = -1;
#endif /*APPLE_HYB */

  if (file->prev)
    file->prev->next = file->next;
  if (file->next)
    file->next->prev = file->prev;
  if (file == vol->files)
    vol->files = file->next;

  FREE(file);

  return result;
}

/* High-Level Catalog Routines ============================================= */

/*
 * NAME:	hfs->stat()
 * DESCRIPTION:	return catalog information for an arbitrary path
 */
int hfs_stat(hfsvol *vol, char *path, hfsdirent *ent)
{
  CatDataRec data;
  long parid;
  char name[HFS_MAX_FLEN + 1];

  if (v_getvol(&vol) < 0 ||
      v_resolve(&vol, path, &data, &parid, name, 0) <= 0)
    return -1;

  r_unpackdirent(parid, name, &data, ent);

  return 0;
}

/*
 * NAME:	hfs->fstat()
 * DESCRIPTION:	return catalog information for an open file
 */
int hfs_fstat(hfsfile *file, hfsdirent *ent)
{
  r_unpackdirent(file->parid, file->name, &file->cat, ent);

  return 0;
}

/*
 * NAME:	hfs->setattr()
 * DESCRIPTION:	change a file's attributes
 */
int hfs_setattr(hfsvol *vol, char *path, hfsdirent *ent)
{
  CatDataRec data;
  node n;

  if (v_getvol(&vol) < 0 ||
      v_resolve(&vol, path, &data, 0, 0, &n) <= 0)
    return -1;

  if (vol->flags & HFS_READONLY)
    {
      ERROR(EROFS, 0);
      return -1;
    }

  r_packdirent(&data, ent);

  if (v_putcatrec(&data, &n) < 0)
    return -1;

  return 0;
}

/*
 * NAME:	hfs->fsetattr()
 * DESCRIPTION:	change an open file's attributes
 */
int hfs_fsetattr(hfsfile *file, hfsdirent *ent)
{
  if (file->vol->flags & HFS_READONLY)
    {
      ERROR(EROFS, 0);
      return -1;
    }

  r_packdirent(&file->cat, ent);

  file->flags |= HFS_UPDATE_CATREC;

  return 0;
}

/*
 * NAME:	hfs->mkdir()
 * DESCRIPTION:	create a new directory
 */
int hfs_mkdir(hfsvol *vol, char *path)
{
  CatDataRec data;
  long parid;
  char name[HFS_MAX_FLEN + 1];
  int found;

  if (v_getvol(&vol) < 0)
    return -1;

  found = v_resolve(&vol, path, &data, &parid, name, 0);
  if (found < 0 || parid == 0)
    return -1;
  else if (found)
    {
      ERROR(EEXIST, 0);
      return -1;
    }

  if (parid == HFS_CNID_ROOTPAR)
    {
      ERROR(EINVAL, 0);
      return -1;
    }

  if (vol->flags & HFS_READONLY)
    {
      ERROR(EROFS, 0);
      return -1;
    }

  if (v_newfolder(vol, parid, name) < 0)
    return -1;

  return 0;
}

/*
 * NAME:	hfs->rmdir()
 * DESCRIPTION:	delete an empty directory
 */
int hfs_rmdir(hfsvol *vol, char *path)
{
  CatKeyRec key;
  CatDataRec data;
  long parid;
  char name[HFS_MAX_FLEN + 1];
  unsigned char pkey[HFS_CATKEYLEN];

  if (v_getvol(&vol) < 0 ||
      v_resolve(&vol, path, &data, &parid, name, 0) <= 0)
    return -1;

  if (data.cdrType != cdrDirRec)
    {
      ERROR(ENOTDIR, 0);
      return -1;
    }

  if (data.u.dir.dirVal != 0)
    {
      ERROR(ENOTEMPTY, 0);
      return -1;
    }

  if (parid == HFS_CNID_ROOTPAR)
    {
      ERROR(EINVAL, 0);
      return -1;
    }

  if (vol->flags & HFS_READONLY)
    {
      ERROR(EROFS, 0);
      return -1;
    }

  /* delete directory record */

  r_makecatkey(&key, parid, name);
  r_packcatkey(&key, pkey, 0);

  if (bt_delete(&vol->cat, pkey) < 0)
    return -1;

  /* delete thread record */

  r_makecatkey(&key, data.u.dir.dirDirID, "");
  r_packcatkey(&key, pkey, 0);

  if (bt_delete(&vol->cat, pkey) < 0 ||
      v_adjvalence(vol, parid, 1, -1) < 0)
    return -1;

  return 0;
}

/*
 * NAME:	hfs->create()
 * DESCRIPTION:	create a new file
 */
int hfs_create(hfsvol *vol, char *path, char *type, char *creator)
{
  CatKeyRec key;
  CatDataRec data;
  long id, parid;
  char name[HFS_MAX_FLEN + 1];
  unsigned char record[HFS_CATRECMAXLEN];
  int found, i, reclen;

  if (v_getvol(&vol) < 0)
    return -1;

  found = v_resolve(&vol, path, &data, &parid, name, 0);
  if (found < 0 || parid == 0)
    return -1;
  else if (found)
    {
      ERROR(EEXIST, 0);
      return -1;
    }

  if (parid == HFS_CNID_ROOTPAR)
    {
      ERROR(EINVAL, 0);
      return -1;
    }

  if (vol->flags & HFS_READONLY)
    {
      ERROR(EROFS, 0);
      return -1;
    }

  /* create file `name' in parent `parid' */

  if (bt_space(&vol->cat, 1) < 0)
    return -1;

  id = vol->mdb.drNxtCNID++;
  vol->flags |= HFS_UPDATE_MDB;

  /* create file record */

  data.cdrType   = cdrFilRec;
  data.cdrResrv2 = 0;

  data.u.fil.filFlags = 0;
  data.u.fil.filTyp   = 0;

  memset(&data.u.fil.filUsrWds, 0, sizeof(data.u.fil.filUsrWds));

  data.u.fil.filUsrWds.fdType    = d_getl((unsigned char *) type);
  data.u.fil.filUsrWds.fdCreator = d_getl((unsigned char *) creator);

  data.u.fil.filFlNum  = id;
  data.u.fil.filStBlk  = 0;
  data.u.fil.filLgLen  = 0;
  data.u.fil.filPyLen  = 0;
  data.u.fil.filRStBlk = 0;
  data.u.fil.filRLgLen = 0;
  data.u.fil.filRPyLen = 0;
  data.u.fil.filCrDat  = d_tomtime(time(0));
  data.u.fil.filMdDat  = data.u.fil.filCrDat;
  data.u.fil.filBkDat  = 0;

  memset(&data.u.fil.filFndrInfo, 0, sizeof(data.u.fil.filFndrInfo));

  data.u.fil.filClpSize = 0;

  for (i = 0; i < 3; ++i)
    {
      data.u.fil.filExtRec[i].xdrStABN     = 0;
      data.u.fil.filExtRec[i].xdrNumABlks  = 0;

      data.u.fil.filRExtRec[i].xdrStABN    = 0;
      data.u.fil.filRExtRec[i].xdrNumABlks = 0;
    }

  data.u.fil.filResrv = 0;

  r_makecatkey(&key, parid, name);
  r_packcatkey(&key, record, &reclen);
  r_packcatdata(&data, HFS_RECDATA(record), &reclen);

  if (bt_insert(&vol->cat, record, reclen) < 0 ||
      v_adjvalence(vol, parid, 0, 1) < 0)
    return -1;

  return 0;
}

/*
 * NAME:	hfs->delete()
 * DESCRIPTION:	remove both forks of a file
 */
int hfs_delete(hfsvol *vol, char *path)
{
  hfsfile file;
  CatKeyRec key;
  unsigned char pkey[HFS_CATKEYLEN];
  int found;

  if (v_getvol(&vol) < 0 ||
      v_resolve(&vol, path, &file.cat, &file.parid, file.name, 0) <= 0)
    return -1;

  if (file.cat.cdrType != cdrFilRec)
    {
      ERROR(EISDIR, 0);
      return -1;
    }

  if (file.parid == HFS_CNID_ROOTPAR)
    {
      ERROR(EINVAL, 0);
      return -1;
    }

  if (vol->flags & HFS_READONLY)
    {
      ERROR(EROFS, 0);
      return -1;
    }

  /* free disk blocks */

  file.vol   = vol;
  file.flags = 0;

  file.cat.u.fil.filLgLen  = 0;
  file.cat.u.fil.filRLgLen = 0;

  f_selectfork(&file, 0);
  if (f_trunc(&file) < 0)
    return -1;

  f_selectfork(&file, 1);
  if (f_trunc(&file) < 0)
    return -1;

  /* delete file record */

  r_makecatkey(&key, file.parid, file.name);
  r_packcatkey(&key, pkey, 0);

  if (bt_delete(&vol->cat, pkey) < 0 ||
      v_adjvalence(vol, file.parid, 0, -1) < 0)
    return -1;

  /* delete file thread, if any */

  found = v_getfthread(vol, file.cat.u.fil.filFlNum, 0, 0);
  if (found < 0)
    return -1;

  if (found)
    {
      r_makecatkey(&key, file.cat.u.fil.filFlNum, "");
      r_packcatkey(&key, pkey, 0);

      if (bt_delete(&vol->cat, pkey) < 0)
	return -1;
    }

  return 0;
}

/*
 * NAME:	hfs->rename()
 * DESCRIPTION:	change the name of and/or move a file or directory
 */
int hfs_rename(hfsvol *vol, char *srcpath, char *dstpath)
{
  hfsvol *srcvol;
  CatDataRec src, dst;
  long srcid, dstid;
  CatKeyRec key;
  char srcname[HFS_MAX_FLEN + 1], dstname[HFS_MAX_FLEN + 1];
  unsigned char record[HFS_CATRECMAXLEN];
  int found, isdir, moving, reclen;
  node n;

  if (v_getvol(&vol) < 0 ||
      v_resolve(&vol, srcpath, &src, &srcid, srcname, 0) <= 0)
    return -1;

  isdir  = (src.cdrType == cdrDirRec);
  srcvol = vol;

  found = v_resolve(&vol, dstpath, &dst, &dstid, dstname, 0);
  if (found < 0)
    return -1;

  if (vol != srcvol)
    {
      ERROR(EINVAL, "can't move across volumes");
      return -1;
    }

  if (dstid == 0)
    {
      ERROR(ENOENT, "bad destination path");
      return -1;
    }

  if (found &&
      dst.cdrType == cdrDirRec &&
      dst.u.dir.dirDirID != src.u.dir.dirDirID)
    {
      dstid = dst.u.dir.dirDirID;
      strcpy(dstname, srcname);

      found = v_catsearch(vol, dstid, dstname, 0, 0, 0);
      if (found < 0)
	return -1;
    }

  moving = (srcid != dstid);

  if (found)
    {
      char *ptr;

      ptr = strrchr(dstpath, ':');
      if (ptr == 0)
	ptr = dstpath;
      else
	++ptr;

      if (*ptr)
	strcpy(dstname, ptr);

      if (! moving && strcmp(srcname, dstname) == 0)
	return 0;  /* source and destination are the same */

      if (moving || d_relstring(srcname, dstname))
	{
	  ERROR(EEXIST, "can't use destination name");
	  return -1;
	}
    }

  /* can't move anything into the root directory's parent */

  if (moving && dstid == HFS_CNID_ROOTPAR)
    {
      ERROR(EINVAL, "can't move above root directory");
      return -1;
    }

  if (moving && isdir)
    {
      long id;

      /* can't move root directory anywhere */

      if (src.u.dir.dirDirID == HFS_CNID_ROOTDIR)
	{
	  ERROR(EINVAL, "can't move root directory");
	  return -1;
	}

      /* make sure we aren't trying to move a directory inside itself */

      for (id = dstid; id != HFS_CNID_ROOTDIR; id = dst.u.dthd.thdParID)
	{
	  if (id == src.u.dir.dirDirID)
	    {
	      ERROR(EINVAL, "can't move directory inside itself");
	      return -1;
	    }

	  if (v_getdthread(vol, id, &dst, 0) <= 0)
	    return -1;
	}
    }

  if (vol->flags & HFS_READONLY)
    {
      ERROR(EROFS, 0);
      return -1;
    }

  /* change volume name */

  if (dstid == HFS_CNID_ROOTPAR)
    {
      if (strlen(dstname) > HFS_MAX_VLEN)
	{
	  ERROR(ENAMETOOLONG, 0);
	  return -1;
	}

      strcpy(vol->mdb.drVN, dstname);
      vol->flags |= HFS_UPDATE_MDB;
    }

  /* remove source record */

  r_makecatkey(&key, srcid, srcname);
  r_packcatkey(&key, record, 0);

  if (bt_delete(&vol->cat, record) < 0)
    return -1;

  /* insert destination record */

  r_makecatkey(&key, dstid, dstname);
  r_packcatkey(&key, record, &reclen);
  r_packcatdata(&src, HFS_RECDATA(record), &reclen);

  if (bt_insert(&vol->cat, record, reclen) < 0)
    return -1;

  /* update thread record */

  if (isdir)
    {
      if (v_getdthread(vol, src.u.dir.dirDirID, &dst, &n) <= 0)
	return -1;

      dst.u.dthd.thdParID = dstid;
      strcpy(dst.u.dthd.thdCName, dstname);

      if (v_putcatrec(&dst, &n) < 0)
	return -1;
    }
  else
    {
      found = v_getfthread(vol, src.u.fil.filFlNum, &dst, &n);
      if (found < 0)
	return -1;

      if (found)
	{
	  dst.u.fthd.fthdParID = dstid;
	  strcpy(dst.u.fthd.fthdCName, dstname);

	  if (v_putcatrec(&dst, &n) < 0)
	    return -1;
	}
    }

  /* update directory valences */

  if (moving)
    {
      if (v_adjvalence(vol, srcid, isdir, -1) < 0 ||
	  v_adjvalence(vol, dstid, isdir,  1) < 0)
	return -1;
    }

  return 0;
}
#ifdef APPLE_HYB
/*
 * NAME:        hfs->hfs_get_drAllocPtr()
 * DESCRIPTION: get the current start of next allocation search 
 */
unsigned short
hfs_get_drAllocPtr(hfsfile *file)
{
  return(file->vol->mdb.drAllocPtr);
}

/*
 * NAME:        hfs->hfs_set_drAllocPtr()
 * DESCRIPTION: set the current start of next allocation search 
 */
int
hfs_set_drAllocPtr(hfsfile *file, unsigned short drAllocPtr, int size)
{
  hfsvol *vol = file->vol;
  int result = 0;

  /* truncate the current fork */
  if (f_trunc(file) < 0 ||
      f_flush(file) < 0)
    result = -1;

  /* convert the fork size into allocation blocks */
  size = (size + vol->mdb.drAlBlkSiz - 1)/vol->mdb.drAlBlkSiz;

  /* set the start of next allocation search to be after this fork */
  vol->mdb.drAllocPtr = drAllocPtr + size;

  vol->flags |= HFS_UPDATE_MDB;

  return result;
}

/*
 * NAME:        hfs->vsetbless()
 * DESCRIPTION: set blessed folder
 *
 * adapted from vsetattr() from v3.2.6
 */
void
hfs_vsetbless(hfsvol *vol, unsigned long cnid) 
{ 
  vol->mdb.drFndrInfo[0] = cnid;
  
  vol->flags |= HFS_UPDATE_MDB;
}
#endif /* APPLE_HYB */
