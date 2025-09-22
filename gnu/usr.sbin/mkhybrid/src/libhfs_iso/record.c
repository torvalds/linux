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
# include <errno.h>
# include <stdlib.h>

# include "internal.h"
# include "data.h"
# include "record.h"

/*
 * NAME:	record->packcatkey()
 * DESCRIPTION:	pack a catalog record key
 */
void r_packcatkey(CatKeyRec *key, unsigned char *pkey, int *len)
{
  unsigned char *start = pkey;

  d_storeb(&pkey, key->ckrKeyLen);
  d_storeb(&pkey, key->ckrResrv1);
  d_storel(&pkey, key->ckrParID);
  d_stores(&pkey, key->ckrCName, sizeof(key->ckrCName));

  if (len)
    *len = HFS_RECKEYSKIP(start);
}

/*
 * NAME:	record->unpackcatkey()
 * DESCRIPTION:	unpack a catalog record key
 */
void r_unpackcatkey(unsigned char *pkey, CatKeyRec *key)
{
  d_fetchb(&pkey, (char *) &key->ckrKeyLen);
  d_fetchb(&pkey, (char *) &key->ckrResrv1);
  d_fetchl(&pkey, (long *) &key->ckrParID);
  d_fetchs(&pkey, key->ckrCName, sizeof(key->ckrCName));
}

/*
 * NAME:	record->packextkey()
 * DESCRIPTION:	pack an extents record key
 */
void r_packextkey(ExtKeyRec *key, unsigned char *pkey, int *len)
{
  unsigned char *start = pkey;

  d_storeb(&pkey, key->xkrKeyLen);
  d_storeb(&pkey, key->xkrFkType);
  d_storel(&pkey, key->xkrFNum);
  d_storew(&pkey, key->xkrFABN);

  if (len)
    *len = HFS_RECKEYSKIP(start);
}

/*
 * NAME:	record->unpackextkey()
 * DESCRIPTION:	unpack an extents record key
 */
void r_unpackextkey(unsigned char *pkey, ExtKeyRec *key)
{
  d_fetchb(&pkey, (char *) &key->xkrKeyLen);
  d_fetchb(&pkey, (char *) &key->xkrFkType);
  d_fetchl(&pkey, (long *) &key->xkrFNum);
  d_fetchw(&pkey, (short *) &key->xkrFABN);
}

/*
 * NAME:	record->comparecatkeys()
 * DESCRIPTION:	compare two (packed) catalog record keys
 */
int r_comparecatkeys(unsigned char *pkey1, unsigned char *pkey2)
{
  CatKeyRec key1;
  CatKeyRec key2;
  int diff;

  r_unpackcatkey(pkey1, &key1);
  r_unpackcatkey(pkey2, &key2);

  diff = key1.ckrParID - key2.ckrParID;
  if (diff)
    return diff;

  return d_relstring(key1.ckrCName, key2.ckrCName);
}

/*
 * NAME:	record->compareextkeys()
 * DESCRIPTION:	compare two (packed) extents record keys
 */
int r_compareextkeys(unsigned char *pkey1, unsigned char *pkey2)
{
  ExtKeyRec key1;
  ExtKeyRec key2;
  int diff;

  r_unpackextkey(pkey1, &key1);
  r_unpackextkey(pkey2, &key2);

  diff = key1.xkrFNum - key2.xkrFNum;
  if (diff)
    return diff;

  diff = (unsigned char) key1.xkrFkType -
         (unsigned char) key2.xkrFkType;
  if (diff)
    return diff;

  return key1.xkrFABN - key2.xkrFABN;
}

/*
 * NAME:	record->packcatdata()
 * DESCRIPTION:	pack catalog record data
 */
void r_packcatdata(CatDataRec *data, unsigned char *pdata, int *len)
{
  unsigned char *start = pdata;
  int i;

  d_storeb(&pdata, data->cdrType);
  d_storeb(&pdata, data->cdrResrv2);

  switch (data->cdrType)
    {
    case cdrDirRec:
      d_storew(&pdata, data->u.dir.dirFlags);
      d_storew(&pdata, data->u.dir.dirVal);
      d_storel(&pdata, data->u.dir.dirDirID);
      d_storel(&pdata, data->u.dir.dirCrDat);
      d_storel(&pdata, data->u.dir.dirMdDat);
      d_storel(&pdata, data->u.dir.dirBkDat);

      d_storew(&pdata, data->u.dir.dirUsrInfo.frRect.top);
      d_storew(&pdata, data->u.dir.dirUsrInfo.frRect.left);
      d_storew(&pdata, data->u.dir.dirUsrInfo.frRect.bottom);
      d_storew(&pdata, data->u.dir.dirUsrInfo.frRect.right);
      d_storew(&pdata, data->u.dir.dirUsrInfo.frFlags);
      d_storew(&pdata, data->u.dir.dirUsrInfo.frLocation.v);
      d_storew(&pdata, data->u.dir.dirUsrInfo.frLocation.h);
      d_storew(&pdata, data->u.dir.dirUsrInfo.frView);

      d_storew(&pdata, data->u.dir.dirFndrInfo.frScroll.v);
      d_storew(&pdata, data->u.dir.dirFndrInfo.frScroll.h);
      d_storel(&pdata, data->u.dir.dirFndrInfo.frOpenChain);
      d_storew(&pdata, data->u.dir.dirFndrInfo.frUnused);
      d_storew(&pdata, data->u.dir.dirFndrInfo.frComment);
      d_storel(&pdata, data->u.dir.dirFndrInfo.frPutAway);

      for (i = 0; i < 4; ++i)
	d_storel(&pdata, data->u.dir.dirResrv[i]);

      break;

    case cdrFilRec:
      d_storeb(&pdata, data->u.fil.filFlags);
      d_storeb(&pdata, data->u.fil.filTyp);

      d_storel(&pdata, data->u.fil.filUsrWds.fdType);
      d_storel(&pdata, data->u.fil.filUsrWds.fdCreator);
      d_storew(&pdata, data->u.fil.filUsrWds.fdFlags);
      d_storew(&pdata, data->u.fil.filUsrWds.fdLocation.v);
      d_storew(&pdata, data->u.fil.filUsrWds.fdLocation.h);
      d_storew(&pdata, data->u.fil.filUsrWds.fdFldr);

      d_storel(&pdata, data->u.fil.filFlNum);

      d_storew(&pdata, data->u.fil.filStBlk);
      d_storel(&pdata, data->u.fil.filLgLen);
      d_storel(&pdata, data->u.fil.filPyLen);

      d_storew(&pdata, data->u.fil.filRStBlk);
      d_storel(&pdata, data->u.fil.filRLgLen);
      d_storel(&pdata, data->u.fil.filRPyLen);

      d_storel(&pdata, data->u.fil.filCrDat);
      d_storel(&pdata, data->u.fil.filMdDat);
      d_storel(&pdata, data->u.fil.filBkDat);

      d_storew(&pdata, data->u.fil.filFndrInfo.fdIconID);
      for (i = 0; i < 4; ++i)
	d_storew(&pdata, data->u.fil.filFndrInfo.fdUnused[i]);
      d_storew(&pdata, data->u.fil.filFndrInfo.fdComment);
      d_storel(&pdata, data->u.fil.filFndrInfo.fdPutAway);

      d_storew(&pdata, data->u.fil.filClpSize);

      for (i = 0; i < 3; ++i)
	{
	  d_storew(&pdata, data->u.fil.filExtRec[i].xdrStABN);
	  d_storew(&pdata, data->u.fil.filExtRec[i].xdrNumABlks);
	}

      for (i = 0; i < 3; ++i)
	{
	  d_storew(&pdata, data->u.fil.filRExtRec[i].xdrStABN);
	  d_storew(&pdata, data->u.fil.filRExtRec[i].xdrNumABlks);
	}

      d_storel(&pdata, data->u.fil.filResrv);
      break;

    case cdrThdRec:
      for (i = 0; i < 2; ++i)
	d_storel(&pdata, data->u.dthd.thdResrv[i]);

      d_storel(&pdata, data->u.dthd.thdParID);
      d_stores(&pdata, data->u.dthd.thdCName, sizeof(data->u.dthd.thdCName));
      break;

    case cdrFThdRec:
      for (i = 0; i < 2; ++i)
	d_storel(&pdata, data->u.fthd.fthdResrv[i]);

      d_storel(&pdata, data->u.fthd.fthdParID);
      d_stores(&pdata, data->u.fthd.fthdCName, sizeof(data->u.fthd.fthdCName));
      break;

    default:
      abort();
    }

  if (len)
    *len += pdata - start;
}

/*
 * NAME:	record->unpackcatdata()
 * DESCRIPTION:	unpack catalog record data
 */
void r_unpackcatdata(unsigned char *pdata, CatDataRec *data)
{
  int i;

  d_fetchb(&pdata, (char *) &data->cdrType);
  d_fetchb(&pdata, (char *) &data->cdrResrv2);

  switch (data->cdrType)
    {
    case cdrDirRec:
      d_fetchw(&pdata, &data->u.dir.dirFlags);
      d_fetchw(&pdata, (short *) &data->u.dir.dirVal);
      d_fetchl(&pdata, (long *) &data->u.dir.dirDirID);
      d_fetchl(&pdata, &data->u.dir.dirCrDat);
      d_fetchl(&pdata, &data->u.dir.dirMdDat);
      d_fetchl(&pdata, &data->u.dir.dirBkDat);

      d_fetchw(&pdata, &data->u.dir.dirUsrInfo.frRect.top);
      d_fetchw(&pdata, &data->u.dir.dirUsrInfo.frRect.left);
      d_fetchw(&pdata, &data->u.dir.dirUsrInfo.frRect.bottom);
      d_fetchw(&pdata, &data->u.dir.dirUsrInfo.frRect.right);
      d_fetchw(&pdata, &data->u.dir.dirUsrInfo.frFlags);
      d_fetchw(&pdata, &data->u.dir.dirUsrInfo.frLocation.v);
      d_fetchw(&pdata, &data->u.dir.dirUsrInfo.frLocation.h);
      d_fetchw(&pdata, &data->u.dir.dirUsrInfo.frView);

      d_fetchw(&pdata, &data->u.dir.dirFndrInfo.frScroll.v);
      d_fetchw(&pdata, &data->u.dir.dirFndrInfo.frScroll.h);
      d_fetchl(&pdata, &data->u.dir.dirFndrInfo.frOpenChain);
      d_fetchw(&pdata, &data->u.dir.dirFndrInfo.frUnused);
      d_fetchw(&pdata, &data->u.dir.dirFndrInfo.frComment);
      d_fetchl(&pdata, &data->u.dir.dirFndrInfo.frPutAway);

      for (i = 0; i < 4; ++i)
	d_fetchl(&pdata, &data->u.dir.dirResrv[i]);

      break;

    case cdrFilRec:
      d_fetchb(&pdata, (char *) &data->u.fil.filFlags);
      d_fetchb(&pdata, (char *) &data->u.fil.filTyp);

      d_fetchl(&pdata, &data->u.fil.filUsrWds.fdType);
      d_fetchl(&pdata, &data->u.fil.filUsrWds.fdCreator);
      d_fetchw(&pdata, &data->u.fil.filUsrWds.fdFlags);
      d_fetchw(&pdata, &data->u.fil.filUsrWds.fdLocation.v);
      d_fetchw(&pdata, &data->u.fil.filUsrWds.fdLocation.h);
      d_fetchw(&pdata, &data->u.fil.filUsrWds.fdFldr);

      d_fetchl(&pdata, (long *) &data->u.fil.filFlNum);

      d_fetchw(&pdata, (short *) &data->u.fil.filStBlk);
      d_fetchl(&pdata, (long *) &data->u.fil.filLgLen);
      d_fetchl(&pdata, (long *) &data->u.fil.filPyLen);

      d_fetchw(&pdata, (short *) &data->u.fil.filRStBlk);
      d_fetchl(&pdata, (long *) &data->u.fil.filRLgLen);
      d_fetchl(&pdata, (long *) &data->u.fil.filRPyLen);

      d_fetchl(&pdata, &data->u.fil.filCrDat);
      d_fetchl(&pdata, &data->u.fil.filMdDat);
      d_fetchl(&pdata, &data->u.fil.filBkDat);

      d_fetchw(&pdata, &data->u.fil.filFndrInfo.fdIconID);
      for (i = 0; i < 4; ++i)
	d_fetchw(&pdata, &data->u.fil.filFndrInfo.fdUnused[i]);
      d_fetchw(&pdata, &data->u.fil.filFndrInfo.fdComment);
      d_fetchl(&pdata, &data->u.fil.filFndrInfo.fdPutAway);

      d_fetchw(&pdata, (short *) &data->u.fil.filClpSize);

      for (i = 0; i < 3; ++i)
	{
	  d_fetchw(&pdata, (short *) &data->u.fil.filExtRec[i].xdrStABN);
	  d_fetchw(&pdata, (short *) &data->u.fil.filExtRec[i].xdrNumABlks);
	}

      for (i = 0; i < 3; ++i)
	{
	  d_fetchw(&pdata, (short *) &data->u.fil.filRExtRec[i].xdrStABN);
	  d_fetchw(&pdata, (short *) &data->u.fil.filRExtRec[i].xdrNumABlks);
	}

      d_fetchl(&pdata, &data->u.fil.filResrv);
      break;

    case cdrThdRec:
      for (i = 0; i < 2; ++i)
	d_fetchl(&pdata, &data->u.dthd.thdResrv[i]);

      d_fetchl(&pdata, (long *) &data->u.dthd.thdParID);
      d_fetchs(&pdata, data->u.dthd.thdCName, sizeof(data->u.dthd.thdCName));
      break;

    case cdrFThdRec:
      for (i = 0; i < 2; ++i)
	d_fetchl(&pdata, &data->u.fthd.fthdResrv[i]);

      d_fetchl(&pdata, (long *) &data->u.fthd.fthdParID);
      d_fetchs(&pdata, data->u.fthd.fthdCName, sizeof(data->u.fthd.fthdCName));
      break;

    default:
      abort();
    }
}

/*
 * NAME:	record->packextdata()
 * DESCRIPTION:	pack extent record data
 */
void r_packextdata(ExtDataRec *data, unsigned char *pdata, int *len)
{
  unsigned char *start = pdata;
  int i;

  for (i = 0; i < 3; ++i)
    {
      d_storew(&pdata, (*data)[i].xdrStABN);
      d_storew(&pdata, (*data)[i].xdrNumABlks);
    }

  if (len)
    *len += pdata - start;
}

/*
 * NAME:	record->unpackextdata()
 * DESCRIPTION:	unpack extent record data
 */
void r_unpackextdata(unsigned char *pdata, ExtDataRec *data)
{
  int i;

  for (i = 0; i < 3; ++i)
    {
      d_fetchw(&pdata, (short *) &(*data)[i].xdrStABN);
      d_fetchw(&pdata, (short *) &(*data)[i].xdrNumABlks);
    }
}

/*
 * NAME:	record->makecatkey()
 * DESCRIPTION:	construct a catalog record key
 */
void r_makecatkey(CatKeyRec *key, long parid, char *name)
{
  int len;

  len = strlen(name) + 1;

  key->ckrKeyLen = 0x05 + len + (len & 1);
  key->ckrResrv1 = 0;
  key->ckrParID  = parid;

  strcpy(key->ckrCName, name);
}

/*
 * NAME:	record->makeextkey()
 * DESCRIPTION:	construct an extents record key
 */
void r_makeextkey(ExtKeyRec *key, int fork, long fnum, unsigned int fabn)
{
  key->xkrKeyLen = 0x07;
  key->xkrFkType = fork;
  key->xkrFNum   = fnum;
  key->xkrFABN   = fabn;
}

/*
 * NAME:	record->unpackdirent()
 * DESCRIPTION:	unpack catalog information into hfsdirent structure
 */
void r_unpackdirent(long parid, char *name, CatDataRec *data, hfsdirent *ent)
{
  strcpy(ent->name, name);
  ent->parid = parid;

  switch (data->cdrType)
    {
    case cdrDirRec:
      ent->flags   = HFS_ISDIR;
      ent->cnid    = data->u.dir.dirDirID;
      ent->crdate  = d_toutime(data->u.dir.dirCrDat);
      ent->mddate  = d_toutime(data->u.dir.dirMdDat);
      ent->dsize   = data->u.dir.dirVal;
      ent->rsize   = 0;

      ent->type[0] = ent->creator[0] = 0;

      ent->fdflags = data->u.dir.dirUsrInfo.frFlags;
      break;

    case cdrFilRec:
      ent->flags   = (data->u.fil.filFlags & (1 << 0)) ? HFS_ISLOCKED : 0;
      ent->cnid    = data->u.fil.filFlNum;
      ent->crdate  = d_toutime(data->u.fil.filCrDat);
      ent->mddate  = d_toutime(data->u.fil.filMdDat);
      ent->dsize   = data->u.fil.filLgLen;
      ent->rsize   = data->u.fil.filRLgLen;

      d_putl((unsigned char *) ent->type,    data->u.fil.filUsrWds.fdType);
      d_putl((unsigned char *) ent->creator, data->u.fil.filUsrWds.fdCreator);

      ent->type[4] = ent->creator[4] = 0;

      ent->fdflags = data->u.fil.filUsrWds.fdFlags;
      break;
    }
}

/*
 * NAME:	record->packdirent()
 * DESCRIPTION:	make changes to a catalog record
 */
void r_packdirent(CatDataRec *data, hfsdirent *ent)
{
  switch (data->cdrType)
    {
    case cdrDirRec:
      data->u.dir.dirCrDat = d_tomtime(ent->crdate);
      data->u.dir.dirMdDat = d_tomtime(ent->mddate);

      data->u.dir.dirUsrInfo.frFlags = ent->fdflags;
      break;

    case cdrFilRec:
      if (ent->flags & HFS_ISLOCKED)
	data->u.fil.filFlags |=  (1 << 0);
      else
	data->u.fil.filFlags &= ~(1 << 0);

      data->u.fil.filCrDat = d_tomtime(ent->crdate);
      data->u.fil.filMdDat = d_tomtime(ent->mddate);

      data->u.fil.filUsrWds.fdType    = d_getl((unsigned char *) ent->type);
      data->u.fil.filUsrWds.fdCreator = d_getl((unsigned char *) ent->creator);

      data->u.fil.filUsrWds.fdFlags   = ent->fdflags;
      break;
    }
}
