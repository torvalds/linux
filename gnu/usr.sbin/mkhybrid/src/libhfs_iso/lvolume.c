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

# include <stdlib.h>
# include <string.h>
# include <errno.h>
# include <time.h>

# include "internal.h"
# include "data.h"
# include "low.h"
# include "btree.h"
# include "record.h"
# include "volume.h"

/*
 * NAME:	vol->catsearch()
 * DESCRIPTION:	search catalog tree
 */
int v_catsearch(hfsvol *vol, long parid, char *name,
		CatDataRec *data, char *cname, node *np)
{
  CatKeyRec key;
  unsigned char pkey[HFS_CATKEYLEN];
  node n;
  unsigned char *ptr;
  int found;

  if (np == 0)
    np = &n;

  r_makecatkey(&key, parid, name);
  r_packcatkey(&key, pkey, 0);

  found = bt_search(&vol->cat, pkey, np);
  if (found <= 0)
    return found;

  ptr = HFS_NODEREC(*np, np->rnum);

  if (cname)
    {
      r_unpackcatkey(ptr, &key);
      strcpy(cname, key.ckrCName);
    }

  if (data)
    r_unpackcatdata(HFS_RECDATA(ptr), data);

  return 1;
}

/*
 * NAME:	vol->extsearch()
 * DESCRIPTION:	search extents tree
 */
int v_extsearch(hfsfile *file, unsigned int fabn, ExtDataRec *data, node *np)
{
  ExtKeyRec key;
  ExtDataRec extsave;
  unsigned int fabnsave;
  unsigned char pkey[HFS_EXTKEYLEN];
  node n;
  unsigned char *ptr;
  int found;

  if (np == 0)
    np = &n;

  r_makeextkey(&key, file->fork, file->cat.u.fil.filFlNum, fabn);
  r_packextkey(&key, pkey, 0);

  /* in case bt_search() clobbers these */

  memcpy(&extsave, &file->ext, sizeof(ExtDataRec));
  fabnsave = file->fabn;

  found = bt_search(&file->vol->ext, pkey, np);

  memcpy(&file->ext, &extsave, sizeof(ExtDataRec));
  file->fabn = fabnsave;

  if (found <= 0)
    return found;

  if (data)
    {
      ptr = HFS_NODEREC(*np, np->rnum);
      r_unpackextdata(HFS_RECDATA(ptr), data);
    }

  return 1;
}

/*
 * NAME:	vol->getthread()
 * DESCRIPTION:	retrieve catalog thread information for a file or directory
 */
int v_getthread(hfsvol *vol, long id, CatDataRec *thread, node *np, int type)
{
  CatDataRec rec;
  int found;

  if (thread == 0)
    thread = &rec;

  found = v_catsearch(vol, id, "", thread, 0, np);
  if (found <= 0)
    return found;

  if (thread->cdrType != type)
    {
      ERROR(EIO, "bad thread record");
      return -1;
    }

  return 1;
}

/*
 * NAME:	vol->putcatrec()
 * DESCRIPTION:	store catalog information
 */
int v_putcatrec(CatDataRec *data, node *np)
{
  unsigned char pdata[HFS_CATDATALEN], *ptr;
  int len = 0;

  r_packcatdata(data, pdata, &len);

  ptr = HFS_NODEREC(*np, np->rnum);
  memcpy(HFS_RECDATA(ptr), pdata, len);

  return bt_putnode(np);
}

/*
 * NAME:	vol->putextrec()
 * DESCRIPTION:	store extent information
 */
int v_putextrec(ExtDataRec *data, node *np)
{
  unsigned char pdata[HFS_EXTDATALEN], *ptr;
  int len = 0;

  r_packextdata(data, pdata, &len);

  ptr = HFS_NODEREC(*np, np->rnum);
  memcpy(HFS_RECDATA(ptr), pdata, len);

  return bt_putnode(np);
}

/*
 * NAME:	vol->allocblocks()
 * DESCRIPTION:	allocate a contiguous range of blocks
 */
int v_allocblocks(hfsvol *vol, ExtDescriptor *blocks)
{
  unsigned int request, found, foundat, start, end, pt;
  block *vbm;
  int wrap = 0;

  if (vol->mdb.drFreeBks == 0)
    {
      ERROR(ENOSPC, "volume full");
      return -1;
    }

  request = blocks->xdrNumABlks;
  found   = 0;
  foundat = 0;
  start   = vol->mdb.drAllocPtr;
  end     = vol->mdb.drNmAlBlks;
  pt      = start;
  vbm     = vol->vbm;

  if (request == 0)
    abort();

  while (1)
    {
      unsigned int mark;

      /* skip blocks in use */

      while (pt < end && BMTST(vbm, pt))
	++pt;

      if (wrap && pt >= start)
	break;

      /* count blocks not in use */

      mark = pt;
      while (pt < end && pt - mark < request && ! BMTST(vbm, pt))
	++pt;

      if (pt - mark > found)
	{
	  found   = pt - mark;
	  foundat = mark;
	}

      if (pt == end)
	pt = 0, wrap = 1;

      if (found == request)
	break;
    }

  if (found == 0 || found > vol->mdb.drFreeBks)
    {
      ERROR(EIO, "bad volume bitmap or free block count");
      return -1;
    }

  blocks->xdrStABN    = foundat;
  blocks->xdrNumABlks = found;

  vol->mdb.drAllocPtr = pt;
  vol->mdb.drFreeBks -= found;

  for (pt = foundat; pt < foundat + found; ++pt)
    BMSET(vbm, pt);

  vol->flags |= HFS_UPDATE_MDB | HFS_UPDATE_VBM;

  return 0;
}

/*
 * NAME:	vol->freeblocks()
 * DESCRIPTION:	deallocate a contiguous range of blocks
 */
void v_freeblocks(hfsvol *vol, ExtDescriptor *blocks)
{
  unsigned int start, len, pt;
  block *vbm;

  start = blocks->xdrStABN;
  len   = blocks->xdrNumABlks;
  vbm   = vol->vbm;

  vol->mdb.drFreeBks += len;

  for (pt = start; pt < start + len; ++pt)
    BMCLR(vbm, pt);

  vol->flags |= HFS_UPDATE_MDB | HFS_UPDATE_VBM;
}

/*
 * NAME:	vol->resolve()
 * DESCRIPTION:	translate a pathname; return catalog information
 */
int v_resolve(hfsvol **vol, char *path, CatDataRec *data,
	      long *parid, char *fname, node *np)
{
  long dirid;
  char name[HFS_MAX_FLEN + 1], *nptr;
  int found;

  if (*path == 0)
    {
      ERROR(ENOENT, "empty path");
      return -1;
    }

  if (parid)
    *parid = 0;

  nptr = strchr(path, ':');

  if (*path == ':' || nptr == 0)
    {
      dirid = (*vol)->cwd;  /* relative path */

      if (*path == ':')
	++path;

      if (*path == 0)
	{
	  found = v_getdthread(*vol, dirid, data, 0);
	  if (found <= 0)
	    return found;

	  if (parid)
	    *parid = data->u.dthd.thdParID;

	  return v_catsearch(*vol, data->u.dthd.thdParID,
			     data->u.dthd.thdCName, data, fname, np);
	}
    }
  else
    {
      hfsvol *check;

      dirid = HFS_CNID_ROOTPAR;  /* absolute path */

      if (nptr - path > HFS_MAX_VLEN)
	{
	  ERROR(ENAMETOOLONG, 0);
	  return -1;
	}

      strncpy(name, path, nptr - path);
      name[nptr - path] = 0;

      for (check = hfs_mounts; check; check = check->next)
	{
	  if (d_relstring(check->mdb.drVN, name) == 0)
	    {
	      *vol = check;
	      break;
	    }
	}
    }

  while (1)
    {
      while (*path == ':')
	{
	  ++path;

	  found = v_getdthread(*vol, dirid, data, 0);
	  if (found <= 0)
	    return found;

	  dirid = data->u.dthd.thdParID;
	}

      if (*path == 0)
	{
	  found = v_getdthread(*vol, dirid, data, 0);
	  if (found <= 0)
	    return found;

	  if (parid)
	    *parid = data->u.dthd.thdParID;

	  return v_catsearch(*vol, data->u.dthd.thdParID,
			     data->u.dthd.thdCName, data, fname, np);
	}

      nptr = name;
      while (nptr < name + sizeof(name) - 1 && *path && *path != ':')
	*nptr++ = *path++;

      if (*path && *path != ':')
	{
	  ERROR(ENAMETOOLONG, 0);
	  return -1;
	}

      *nptr = 0;
      if (*path == ':')
	++path;

      if (parid)
	*parid = dirid;

      found = v_catsearch(*vol, dirid, name, data, fname, np);
      if (found < 0)
	return -1;

      if (found == 0)
	{
	  if (*path && parid)
	    *parid = 0;

	  if (*path == 0 && fname)
	    strcpy(fname, name);

	  return 0;
	}

      switch (data->cdrType)
	{
	case cdrDirRec:
	  if (*path == 0)
	    return 1;

	  dirid = data->u.dir.dirDirID;
	  break;

	case cdrFilRec:
	  if (*path == 0)
	    return 1;

	  ERROR(ENOTDIR, "invalid pathname");
	  return -1;

	default:
	  ERROR(EIO, "unexpected catalog record");
	  return -1;
	}
    }
}

/*
 * NAME:	vol->destruct()
 * DESCRIPTION:	free memory consumed by a volume descriptor
 */
void v_destruct(hfsvol *vol)
{
  FREE(vol->vbm);

  FREE(vol->ext.map);
  FREE(vol->cat.map);

  FREE(vol);
}

/*
 * NAME:	vol->getvol()
 * DESCRIPTION:	validate a volume reference
 */
int v_getvol(hfsvol **vol)
{
  if (*vol == 0)
    {
      if (hfs_curvol == 0)
	{
	  ERROR(EINVAL, "no volume is current");
	  return -1;
	}

      *vol = hfs_curvol;
    }

  return 0;
}

/*
 * NAME:	vol->flush()
 * DESCRIPTION:	flush all pending changes (B*-tree, MDB, VBM) to disk
 */
int v_flush(hfsvol *vol, int umounting)
{
  if (! (vol->flags & HFS_READONLY))
    {
      if ((vol->ext.flags & HFS_UPDATE_BTHDR) &&
	  bt_writehdr(&vol->ext) < 0)
	return -1;

      if ((vol->cat.flags & HFS_UPDATE_BTHDR) &&
	  bt_writehdr(&vol->cat) < 0)
	return -1;

      if ((vol->flags & HFS_UPDATE_VBM) &&
	  l_writevbm(vol) < 0)
	return -1;

      if (umounting &&
	  ! (vol->mdb.drAtrb & HFS_ATRB_UMOUNTED))
	{
	  vol->mdb.drAtrb |= HFS_ATRB_UMOUNTED;
	  vol->flags |= HFS_UPDATE_MDB;
	}

      if ((vol->flags & (HFS_UPDATE_MDB | HFS_UPDATE_ALTMDB)) &&
	  l_writemdb(vol) < 0)
	return -1;
    }

  return 0;
}

/*
 * NAME:	vol->adjvalence()
 * DESCRIPTION:	update a volume's valence counts
 */
int v_adjvalence(hfsvol *vol, long parid, int isdir, int adj)
{
  node n;
  CatDataRec data;

  if (isdir)
    vol->mdb.drDirCnt += adj;
  else
    vol->mdb.drFilCnt += adj;

  vol->flags |= HFS_UPDATE_MDB;

  if (parid == HFS_CNID_ROOTDIR)
    {
      if (isdir)
	vol->mdb.drNmRtDirs += adj;
      else
	vol->mdb.drNmFls    += adj;
    }
  else if (parid == HFS_CNID_ROOTPAR)
    return 0;

  if (v_getdthread(vol, parid, &data, 0) <= 0 ||
      v_catsearch(vol, data.u.dthd.thdParID, data.u.dthd.thdCName,
		  &data, 0, &n) <= 0 ||
      data.cdrType != cdrDirRec)
    {
      ERROR(EIO, "can't find parent directory");
      return -1;
    }

  data.u.dir.dirVal  += adj;
  data.u.dir.dirMdDat = d_tomtime(time(0));

  return v_putcatrec(&data, &n);
}

/*
 * NAME:	vol->newfolder()
 * DESCRIPTION:	create a new HFS folder
 */
int v_newfolder(hfsvol *vol, long parid, char *name)
{
  CatKeyRec key;
  CatDataRec data;
  long id;
  unsigned char record[HFS_CATRECMAXLEN];
  int i, reclen;

  if (bt_space(&vol->cat, 2) < 0)
    return -1;

  id = vol->mdb.drNxtCNID++;
  vol->flags |= HFS_UPDATE_MDB;

  /* create directory record */

  data.cdrType   = cdrDirRec;
  data.cdrResrv2 = 0;

  data.u.dir.dirFlags = 0;
  data.u.dir.dirVal   = 0;
  data.u.dir.dirDirID = id;
  data.u.dir.dirCrDat = d_tomtime(time(0));
  data.u.dir.dirMdDat = data.u.dir.dirCrDat;
  data.u.dir.dirBkDat = 0;

  memset(&data.u.dir.dirUsrInfo,  0, sizeof(data.u.dir.dirUsrInfo));
  memset(&data.u.dir.dirFndrInfo, 0, sizeof(data.u.dir.dirFndrInfo));
  for (i = 0; i < 4; ++i)
    data.u.dir.dirResrv[i] = 0;

  r_makecatkey(&key, parid, name);
  r_packcatkey(&key, record, &reclen);
  r_packcatdata(&data, HFS_RECDATA(record), &reclen);

  if (bt_insert(&vol->cat, record, reclen) < 0)
    return -1;

  /* create thread record */

  data.cdrType   = cdrThdRec;
  data.cdrResrv2 = 0;

  data.u.dthd.thdResrv[0] = 0;
  data.u.dthd.thdResrv[1] = 0;
  data.u.dthd.thdParID    = parid;
  strcpy(data.u.dthd.thdCName, name);

  r_makecatkey(&key, id, "");
  r_packcatkey(&key, record, &reclen);
  r_packcatdata(&data, HFS_RECDATA(record), &reclen);

  if (bt_insert(&vol->cat, record, reclen) < 0 ||
      v_adjvalence(vol, parid, 1, 1) < 0)
    return -1;

  return 0;
}

/*
 * NAME:	markexts()
 * DESCRIPTION:	set bits from an extent record in the volume bitmap
 */
static
void markexts(block *vbm, ExtDataRec *exts)
{
  int i;
  unsigned int start, len;

  for (i = 0; i < 3; ++i)
    {
      for (start = (*exts)[i].xdrStABN,
	     len = (*exts)[i].xdrNumABlks; len--; ++start)
	BMSET(vbm, start);
    }
}

/*
 * NAME:	vol->scavenge()
 * DESCRIPTION:	safeguard blocks in the volume bitmap
 */
int v_scavenge(hfsvol *vol)
{
  block *vbm = vol->vbm;
  node n;
  unsigned int pt, blks;

  if (vbm == 0)
    return 0;

  markexts(vbm, &vol->mdb.drXTExtRec);
  markexts(vbm, &vol->mdb.drCTExtRec);

  vol->flags |= HFS_UPDATE_VBM;

  /* scavenge the extents overflow file */

  n.bt   = &vol->ext;
  n.nnum = vol->ext.hdr.bthFNode;

  if (n.nnum > 0)
    {
      if (bt_getnode(&n) < 0)
	return -1;

      n.rnum = 0;

      while (1)
	{
	  ExtDataRec data;
	  unsigned char *ptr;

	  while (n.rnum >= n.nd.ndNRecs)
	    {
	      n.nnum = n.nd.ndFLink;
	      if (n.nnum == 0)
		break;

	      if (bt_getnode(&n) < 0)
		return -1;

	      n.rnum = 0;
	    }

	  if (n.nnum == 0)
	    break;

	  ptr = HFS_NODEREC(n, n.rnum);
	  r_unpackextdata(HFS_RECDATA(ptr), &data);

	  markexts(vbm, &data);

	  ++n.rnum;
	}
    }

  /* scavenge the catalog file */

  n.bt   = &vol->cat;
  n.nnum = vol->cat.hdr.bthFNode;

  if (n.nnum > 0)
    {
      if (bt_getnode(&n) < 0)
	return -1;

      n.rnum = 0;

      while (1)
	{
	  CatDataRec data;
	  unsigned char *ptr;

	  while (n.rnum >= n.nd.ndNRecs)
	    {
	      n.nnum = n.nd.ndFLink;
	      if (n.nnum == 0)
		break;

	      if (bt_getnode(&n) < 0)
		return -1;

	      n.rnum = 0;
	    }

	  if (n.nnum == 0)
	    break;

	  ptr = HFS_NODEREC(n, n.rnum);
	  r_unpackcatdata(HFS_RECDATA(ptr), &data);

	  if (data.cdrType == cdrFilRec)
	    {
	      markexts(vbm, &data.u.fil.filExtRec);
	      markexts(vbm, &data.u.fil.filRExtRec);
	    }

	  ++n.rnum;
	}
    }

  for (blks = 0, pt = vol->mdb.drNmAlBlks; pt--; )
    {
      if (! BMTST(vbm, pt))
	++blks;
    }

  if (vol->mdb.drFreeBks != blks)
    {
      vol->mdb.drFreeBks = blks;
      vol->flags |= HFS_UPDATE_MDB;
    }

  return 0;
}
