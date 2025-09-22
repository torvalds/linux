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

# include "internal.h"
# include "data.h"
# include "btree.h"
# include "node.h"

# define NODESPACE(n)  \
  (HFS_BLOCKSZ - (n).roff[(n).nd.ndNRecs] - 2 * ((n).nd.ndNRecs + 1))

/*
 * NAME:	node->init()
 * DESCRIPTION:	construct an empty node
 */
void n_init(node *np, btree *bt, int type, int height)
{
  np->bt   = bt;
  np->nnum = -1;

  np->nd.ndFLink   = 0;
  np->nd.ndBLink   = 0;
  np->nd.ndType    = type;
  np->nd.ndNHeight = height;
  np->nd.ndNRecs   = 0;
  np->nd.ndResv2   = 0;

  np->rnum    = -1;
  np->roff[0] = 0x00e;

  memset(np->data, 0, sizeof(np->data));
}

/*
 * NAME:	node->new()
 * DESCRIPTION:	allocate a new b*-tree node
 */
int n_new(node *np)
{
  btree *bt = np->bt;
  unsigned long num;

  if (bt->hdr.bthFree == 0)
    {
      ERROR(EIO, "b*-tree full");
      return -1;
    }

  num = 0;
  while (num < bt->hdr.bthNNodes && BMTST(bt->map, num))
    ++num;

  if (num == bt->hdr.bthNNodes)
    {
      ERROR(EIO, "free b*-tree node not found");
      return -1;
    }

  np->nnum = num;

  BMSET(bt->map, num);
  --bt->hdr.bthFree;

  bt->flags |= HFS_UPDATE_BTHDR;

  return 0;
}

/*
 * NAME:	node->free()
 * DESCRIPTION:	deallocate a b*-tree node
 */
void n_free(node *np)
{
  btree *bt = np->bt;

  BMCLR(bt->map, np->nnum);
  ++bt->hdr.bthFree;

  bt->flags |= HFS_UPDATE_BTHDR;
}

/*
 * NAME:	node->compact()
 * DESCRIPTION:	clean up a node, removing deleted records
 */
void n_compact(node *np)
{
  unsigned char *ptr;
  int offset, nrecs, i;

  offset = 0x00e;
  ptr    = np->data + offset;
  nrecs  = 0;

  for (i = 0; i < np->nd.ndNRecs; ++i)
    {
      unsigned char *rec;
      int reclen;

      rec    = HFS_NODEREC(*np, i);
      reclen = np->roff[i + 1] - np->roff[i];

      if (HFS_RECKEYLEN(rec) > 0)
	{
	  np->roff[nrecs++] = offset;
	  offset += reclen;

	  if (ptr == rec)
	    ptr += reclen;
	  else
	    {
	      while (reclen--)
		*ptr++ = *rec++;
	    }
	}
    }

  np->roff[nrecs] = offset;
  np->nd.ndNRecs  = nrecs;
}

/*
 * NAME:	node->search()
 * DESCRIPTION:	locate a record in a node, or the record it should follow
 */
int n_search(node *np, unsigned char *key)
{
  btree *bt = np->bt;
  int i, comp = -1;

  for (i = np->nd.ndNRecs; i--; )
    {
      unsigned char *rec;

      rec = HFS_NODEREC(*np, i);

      if (HFS_RECKEYLEN(rec) == 0)
	continue;  /* deleted record */

      comp = bt->compare(rec, key);

      if (comp <= 0)
	break;
    }

  np->rnum = i;

  return comp == 0;
}

/*
 * NAME:	node->index()
 * DESCRIPTION:	create an index record from a key and node pointer
 */
void n_index(btree *bt, unsigned char *key, unsigned long nnum,
	     unsigned char *record, int *reclen)
{
  if (bt == &bt->f.vol->cat)
    {
      /* force the key length to be 0x25 */

      HFS_RECKEYLEN(record) = 0x25;
      memset(record + 1, 0, 0x25);
      memcpy(record + 1, key + 1, HFS_RECKEYLEN(key));
    }
  else
    memcpy(record, key, HFS_RECKEYSKIP(key));

  d_putl(HFS_RECDATA(record), nnum);

  if (reclen)
    *reclen = HFS_RECKEYSKIP(record) + 4;
}

/*
 * NAME:	node->split()
 * DESCRIPTION:	divide a node into two and insert a record
 */
int n_split(node *left, unsigned char *record, int *reclen)
{
  node right;
  int nrecs, i, mid;
  unsigned char *rec;

  right = *left;
  right.nd.ndBLink = left->nnum;

  if (n_new(&right) < 0)
    return -1;

  left->nd.ndFLink = right.nnum;
  nrecs = left->nd.ndNRecs;

  /*
   * Ensure node split leaves enough room for new record.
   * The size calculations used are based on the NODESPACE() macro, but
   * I don't know what the extra 2's and 1's are needed for.
   * John Witford <jwitford@hutch.com.au>
   */
  n_search(&right, record);
  mid = nrecs/2;
  for(;;)
    {
	if (right.rnum < mid)
	{
	    if (   mid > 0
		&& left->roff[mid] + *reclen + 2 > HFS_BLOCKSZ - 2 * (mid + 1))
	    {
		--mid;
		if (mid > 0)
		    continue;
	    }
	}
	else
	{
	    if (   mid < nrecs
		&& right.roff[nrecs] - right.roff[mid] + left->roff[0] + *reclen + 2 > HFS_BLOCKSZ - 2 * (mid + 1))
	    {
		++mid;
		if (mid < nrecs)
		    continue;
	    }
	}
	break;
    }

  for (i = 0; i < nrecs; ++i)
    {
	if (i < mid)
	    rec = HFS_NODEREC(right, i);
	else
	    rec = HFS_NODEREC(*left, i);

	HFS_RECKEYLEN(rec) = 0;
    }

/* original code ...
  for (i = 0; i < nrecs; ++i)
    {
      if (i < nrecs / 2)
	rec = HFS_NODEREC(right, i);
      else
	rec = HFS_NODEREC(*left, i);

      HFS_RECKEYLEN(rec) = 0;
    }
*/
  n_compact(left);
  n_compact(&right);

  n_search(&right, record);
  if (right.rnum >= 0)
    n_insertx(&right, record, *reclen);
  else
    {
      n_search(left, record);
      n_insertx(left, record, *reclen);
    }

  /* store the new/modified nodes */

  if (bt_putnode(left) < 0 ||
      bt_putnode(&right) < 0)
    return -1;

  /* create an index record for the new node in the parent */

  n_index(right.bt, HFS_NODEREC(right, 0), right.nnum, record, reclen);

  /* update link pointers */

  if (left->bt->hdr.bthLNode == left->nnum)
    {
      left->bt->hdr.bthLNode = right.nnum;
      left->bt->flags |= HFS_UPDATE_BTHDR;
    }

  if (right.nd.ndFLink)
    {
      node n;

      n.bt   = right.bt;
      n.nnum = right.nd.ndFLink;

      if (bt_getnode(&n) < 0)
	return -1;

      n.nd.ndBLink = right.nnum;

      if (bt_putnode(&n) < 0)
	return -1;
    }

  return 0;
}

/*
 * NAME:	node->insertx()
 * DESCRIPTION:	insert a record into a node (which must already have room)
 */
void n_insertx(node *np, unsigned char *record, int reclen)
{
  int rnum, i;
  unsigned char *ptr;

  rnum = np->rnum + 1;

  /* push other records down to make room */

  for (ptr = HFS_NODEREC(*np, np->nd.ndNRecs) + reclen;
       ptr > HFS_NODEREC(*np, rnum) + reclen; --ptr)
    *(ptr - 1) = *(ptr - 1 - reclen);

  ++np->nd.ndNRecs;

  for (i = np->nd.ndNRecs; i > rnum; --i)
    np->roff[i] = np->roff[i - 1] + reclen;

  /* write the new record */

  memcpy(HFS_NODEREC(*np, rnum), record, reclen);
}

/*
 * NAME:	node->insert()
 * DESCRIPTION:	insert a new record into a node; return a record for parent
 */
int n_insert(node *np, unsigned char *record, int *reclen)
{
  n_compact(np);

  /* check for free space */

  if (np->nd.ndNRecs >= HFS_MAXRECS ||
      *reclen + 2 > NODESPACE(*np))
    return n_split(np, record, reclen);

  n_insertx(np, record, *reclen);
  *reclen = 0;

  return bt_putnode(np);
}

/*
 * NAME:	node->merge()
 * DESCRIPTION:	combine two nodes into a single node
 */
int n_merge(node *right, node *left, unsigned char *record, int *flag)
{
  int i, offset;

  /* copy records and offsets */

  memcpy(HFS_NODEREC(*left, left->nd.ndNRecs), HFS_NODEREC(*right, 0),
	 right->roff[right->nd.ndNRecs] - right->roff[0]);

  offset = left->roff[left->nd.ndNRecs] - right->roff[0];

  for (i = 1; i <= right->nd.ndNRecs; ++i)
    left->roff[++left->nd.ndNRecs] = offset + right->roff[i];

  /* update link pointers */

  left->nd.ndFLink = right->nd.ndFLink;

  if (bt_putnode(left) < 0)
    return -1;

  if (right->bt->hdr.bthLNode == right->nnum)
    {
      right->bt->hdr.bthLNode = left->nnum;
      right->bt->flags |= HFS_UPDATE_BTHDR;
    }

  if (right->nd.ndFLink)
    {
      node n;

      n.bt   = right->bt;
      n.nnum = right->nd.ndFLink;

      if (bt_getnode(&n) < 0)
	return -1;

      n.nd.ndBLink = left->nnum;

      if (bt_putnode(&n) < 0)
	return -1;
    }

  n_free(right);

  HFS_RECKEYLEN(record) = 0;
  *flag = 1;

  return 0;
}

/*
 * NAME:	node->delete()
 * DESCRIPTION:	remove a record from a node
 */
int n_delete(node *np, unsigned char *record, int *flag)
{
  node left;
  unsigned char *rec;

  rec = HFS_NODEREC(*np, np->rnum);

  HFS_RECKEYLEN(rec) = 0;
  n_compact(np);

  /* see if we can merge with our left sibling */

  left.bt   = np->bt;
  left.nnum = np->nd.ndBLink;

  if (left.nnum > 0)
    {
      if (bt_getnode(&left) < 0)
	return -1;

      if (np->nd.ndNRecs + left.nd.ndNRecs <= HFS_MAXRECS &&
	  np->roff[np->nd.ndNRecs] - np->roff[0] +
	  2 * np->nd.ndNRecs <= NODESPACE(left))
	return n_merge(np, &left, record, flag);
    }

  if (np->rnum == 0)
    {
      /* special case: first record changed; update parent record key */

      n_index(np->bt, HFS_NODEREC(*np, 0), np->nnum, record, 0);
      *flag = 1;
    }

  return bt_putnode(np);
}
