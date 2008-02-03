/*
 *   Copyright (C) International Business Machines Corp., 2000-2005
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
/*
 *	jfs_xtree.c: extent allocation descriptor B+-tree manager
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_metapage.h"
#include "jfs_dmap.h"
#include "jfs_dinode.h"
#include "jfs_superblock.h"
#include "jfs_debug.h"

/*
 * xtree local flag
 */
#define XT_INSERT	0x00000001

/*
 *	xtree key/entry comparison: extent offset
 *
 * return:
 *	-1: k < start of extent
 *	 0: start_of_extent <= k <= end_of_extent
 *	 1: k > end_of_extent
 */
#define XT_CMP(CMP, K, X, OFFSET64)\
{\
	OFFSET64 = offsetXAD(X);\
	(CMP) = ((K) >= OFFSET64 + lengthXAD(X)) ? 1 :\
		((K) < OFFSET64) ? -1 : 0;\
}

/* write a xad entry */
#define XT_PUTENTRY(XAD, FLAG, OFF, LEN, ADDR)\
{\
	(XAD)->flag = (FLAG);\
	XADoffset((XAD), (OFF));\
	XADlength((XAD), (LEN));\
	XADaddress((XAD), (ADDR));\
}

#define XT_PAGE(IP, MP) BT_PAGE(IP, MP, xtpage_t, i_xtroot)

/* get page buffer for specified block address */
/* ToDo: Replace this ugly macro with a function */
#define XT_GETPAGE(IP, BN, MP, SIZE, P, RC)\
{\
	BT_GETPAGE(IP, BN, MP, xtpage_t, SIZE, P, RC, i_xtroot)\
	if (!(RC))\
	{\
		if ((le16_to_cpu((P)->header.nextindex) < XTENTRYSTART) ||\
		    (le16_to_cpu((P)->header.nextindex) > le16_to_cpu((P)->header.maxentry)) ||\
		    (le16_to_cpu((P)->header.maxentry) > (((BN)==0)?XTROOTMAXSLOT:PSIZE>>L2XTSLOTSIZE)))\
		{\
			jfs_error((IP)->i_sb, "XT_GETPAGE: xtree page corrupt");\
			BT_PUTPAGE(MP);\
			MP = NULL;\
			RC = -EIO;\
		}\
	}\
}

/* for consistency */
#define XT_PUTPAGE(MP) BT_PUTPAGE(MP)

#define XT_GETSEARCH(IP, LEAF, BN, MP, P, INDEX) \
	BT_GETSEARCH(IP, LEAF, BN, MP, xtpage_t, P, INDEX, i_xtroot)
/* xtree entry parameter descriptor */
struct xtsplit {
	struct metapage *mp;
	s16 index;
	u8 flag;
	s64 off;
	s64 addr;
	int len;
	struct pxdlist *pxdlist;
};


/*
 *	statistics
 */
#ifdef CONFIG_JFS_STATISTICS
static struct {
	uint search;
	uint fastSearch;
	uint split;
} xtStat;
#endif


/*
 * forward references
 */
static int xtSearch(struct inode *ip, s64 xoff, s64 *next, int *cmpp,
		    struct btstack * btstack, int flag);

static int xtSplitUp(tid_t tid,
		     struct inode *ip,
		     struct xtsplit * split, struct btstack * btstack);

static int xtSplitPage(tid_t tid, struct inode *ip, struct xtsplit * split,
		       struct metapage ** rmpp, s64 * rbnp);

static int xtSplitRoot(tid_t tid, struct inode *ip,
		       struct xtsplit * split, struct metapage ** rmpp);

#ifdef _STILL_TO_PORT
static int xtDeleteUp(tid_t tid, struct inode *ip, struct metapage * fmp,
		      xtpage_t * fp, struct btstack * btstack);

static int xtSearchNode(struct inode *ip,
			xad_t * xad,
			int *cmpp, struct btstack * btstack, int flag);

static int xtRelink(tid_t tid, struct inode *ip, xtpage_t * fp);
#endif				/*  _STILL_TO_PORT */

/*
 *	xtLookup()
 *
 * function: map a single page into a physical extent;
 */
int xtLookup(struct inode *ip, s64 lstart,
	     s64 llen, int *pflag, s64 * paddr, s32 * plen, int no_check)
{
	int rc = 0;
	struct btstack btstack;
	int cmp;
	s64 bn;
	struct metapage *mp;
	xtpage_t *p;
	int index;
	xad_t *xad;
	s64 next, size, xoff, xend;
	int xlen;
	s64 xaddr;

	*paddr = 0;
	*plen = llen;

	if (!no_check) {
		/* is lookup offset beyond eof ? */
		size = ((u64) ip->i_size + (JFS_SBI(ip->i_sb)->bsize - 1)) >>
		    JFS_SBI(ip->i_sb)->l2bsize;
		if (lstart >= size) {
			jfs_err("xtLookup: lstart (0x%lx) >= size (0x%lx)",
				(ulong) lstart, (ulong) size);
			return 0;
		}
	}

	/*
	 * search for the xad entry covering the logical extent
	 */
//search:
	if ((rc = xtSearch(ip, lstart, &next, &cmp, &btstack, 0))) {
		jfs_err("xtLookup: xtSearch returned %d", rc);
		return rc;
	}

	/*
	 *	compute the physical extent covering logical extent
	 *
	 * N.B. search may have failed (e.g., hole in sparse file),
	 * and returned the index of the next entry.
	 */
	/* retrieve search result */
	XT_GETSEARCH(ip, btstack.top, bn, mp, p, index);

	/* is xad found covering start of logical extent ?
	 * lstart is a page start address,
	 * i.e., lstart cannot start in a hole;
	 */
	if (cmp) {
		if (next)
			*plen = min(next - lstart, llen);
		goto out;
	}

	/*
	 * lxd covered by xad
	 */
	xad = &p->xad[index];
	xoff = offsetXAD(xad);
	xlen = lengthXAD(xad);
	xend = xoff + xlen;
	xaddr = addressXAD(xad);

	/* initialize new pxd */
	*pflag = xad->flag;
	*paddr = xaddr + (lstart - xoff);
	/* a page must be fully covered by an xad */
	*plen = min(xend - lstart, llen);

      out:
	XT_PUTPAGE(mp);

	return rc;
}


/*
 *	xtLookupList()
 *
 * function: map a single logical extent into a list of physical extent;
 *
 * parameter:
 *	struct inode	*ip,
 *	struct lxdlist	*lxdlist,	lxd list (in)
 *	struct xadlist	*xadlist,	xad list (in/out)
 *	int		flag)
 *
 * coverage of lxd by xad under assumption of
 * . lxd's are ordered and disjoint.
 * . xad's are ordered and disjoint.
 *
 * return:
 *	0:	success
 *
 * note: a page being written (even a single byte) is backed fully,
 *	except the last page which is only backed with blocks
 *	required to cover the last byte;
 *	the extent backing a page is fully contained within an xad;
 */
int xtLookupList(struct inode *ip, struct lxdlist * lxdlist,
		 struct xadlist * xadlist, int flag)
{
	int rc = 0;
	struct btstack btstack;
	int cmp;
	s64 bn;
	struct metapage *mp;
	xtpage_t *p;
	int index;
	lxd_t *lxd;
	xad_t *xad, *pxd;
	s64 size, lstart, lend, xstart, xend, pstart;
	s64 llen, xlen, plen;
	s64 xaddr, paddr;
	int nlxd, npxd, maxnpxd;

	npxd = xadlist->nxad = 0;
	maxnpxd = xadlist->maxnxad;
	pxd = xadlist->xad;

	nlxd = lxdlist->nlxd;
	lxd = lxdlist->lxd;

	lstart = offsetLXD(lxd);
	llen = lengthLXD(lxd);
	lend = lstart + llen;

	size = (ip->i_size + (JFS_SBI(ip->i_sb)->bsize - 1)) >>
	    JFS_SBI(ip->i_sb)->l2bsize;

	/*
	 * search for the xad entry covering the logical extent
	 */
      search:
	if (lstart >= size)
		return 0;

	if ((rc = xtSearch(ip, lstart, NULL, &cmp, &btstack, 0)))
		return rc;

	/*
	 *	compute the physical extent covering logical extent
	 *
	 * N.B. search may have failed (e.g., hole in sparse file),
	 * and returned the index of the next entry.
	 */
//map:
	/* retrieve search result */
	XT_GETSEARCH(ip, btstack.top, bn, mp, p, index);

	/* is xad on the next sibling page ? */
	if (index == le16_to_cpu(p->header.nextindex)) {
		if (p->header.flag & BT_ROOT)
			goto mapend;

		if ((bn = le64_to_cpu(p->header.next)) == 0)
			goto mapend;

		XT_PUTPAGE(mp);

		/* get next sibling page */
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;

		index = XTENTRYSTART;
	}

	xad = &p->xad[index];

	/*
	 * is lxd covered by xad ?
	 */
      compare:
	xstart = offsetXAD(xad);
	xlen = lengthXAD(xad);
	xend = xstart + xlen;
	xaddr = addressXAD(xad);

      compare1:
	if (xstart < lstart)
		goto compare2;

	/* (lstart <= xstart) */

	/* lxd is NOT covered by xad */
	if (lend <= xstart) {
		/*
		 * get next lxd
		 */
		if (--nlxd == 0)
			goto mapend;
		lxd++;

		lstart = offsetLXD(lxd);
		llen = lengthLXD(lxd);
		lend = lstart + llen;
		if (lstart >= size)
			goto mapend;

		/* compare with the current xad */
		goto compare1;
	}
	/* lxd is covered by xad */
	else {			/* (xstart < lend) */

		/* initialize new pxd */
		pstart = xstart;
		plen = min(lend - xstart, xlen);
		paddr = xaddr;

		goto cover;
	}

	/* (xstart < lstart) */
      compare2:
	/* lxd is covered by xad */
	if (lstart < xend) {
		/* initialize new pxd */
		pstart = lstart;
		plen = min(xend - lstart, llen);
		paddr = xaddr + (lstart - xstart);

		goto cover;
	}
	/* lxd is NOT covered by xad */
	else {			/* (xend <= lstart) */

		/*
		 * get next xad
		 *
		 * linear search next xad covering lxd on
		 * the current xad page, and then tree search
		 */
		if (index == le16_to_cpu(p->header.nextindex) - 1) {
			if (p->header.flag & BT_ROOT)
				goto mapend;

			XT_PUTPAGE(mp);
			goto search;
		} else {
			index++;
			xad++;

			/* compare with new xad */
			goto compare;
		}
	}

	/*
	 * lxd is covered by xad and a new pxd has been initialized
	 * (lstart <= xstart < lend) or (xstart < lstart < xend)
	 */
      cover:
	/* finalize pxd corresponding to current xad */
	XT_PUTENTRY(pxd, xad->flag, pstart, plen, paddr);

	if (++npxd >= maxnpxd)
		goto mapend;
	pxd++;

	/*
	 * lxd is fully covered by xad
	 */
	if (lend <= xend) {
		/*
		 * get next lxd
		 */
		if (--nlxd == 0)
			goto mapend;
		lxd++;

		lstart = offsetLXD(lxd);
		llen = lengthLXD(lxd);
		lend = lstart + llen;
		if (lstart >= size)
			goto mapend;

		/*
		 * test for old xad covering new lxd
		 * (old xstart < new lstart)
		 */
		goto compare2;
	}
	/*
	 * lxd is partially covered by xad
	 */
	else {			/* (xend < lend) */

		/*
		 * get next xad
		 *
		 * linear search next xad covering lxd on
		 * the current xad page, and then next xad page search
		 */
		if (index == le16_to_cpu(p->header.nextindex) - 1) {
			if (p->header.flag & BT_ROOT)
				goto mapend;

			if ((bn = le64_to_cpu(p->header.next)) == 0)
				goto mapend;

			XT_PUTPAGE(mp);

			/* get next sibling page */
			XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
			if (rc)
				return rc;

			index = XTENTRYSTART;
			xad = &p->xad[index];
		} else {
			index++;
			xad++;
		}

		/*
		 * test for new xad covering old lxd
		 * (old lstart < new xstart)
		 */
		goto compare;
	}

      mapend:
	xadlist->nxad = npxd;

//out:
	XT_PUTPAGE(mp);

	return rc;
}


/*
 *	xtSearch()
 *
 * function:	search for the xad entry covering specified offset.
 *
 * parameters:
 *	ip	- file object;
 *	xoff	- extent offset;
 *	nextp	- address of next extent (if any) for search miss
 *	cmpp	- comparison result:
 *	btstack - traverse stack;
 *	flag	- search process flag (XT_INSERT);
 *
 * returns:
 *	btstack contains (bn, index) of search path traversed to the entry.
 *	*cmpp is set to result of comparison with the entry returned.
 *	the page containing the entry is pinned at exit.
 */
static int xtSearch(struct inode *ip, s64 xoff,	s64 *nextp,
		    int *cmpp, struct btstack * btstack, int flag)
{
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);
	int rc = 0;
	int cmp = 1;		/* init for empty page */
	s64 bn;			/* block number */
	struct metapage *mp;	/* page buffer */
	xtpage_t *p;		/* page */
	xad_t *xad;
	int base, index, lim, btindex;
	struct btframe *btsp;
	int nsplit = 0;		/* number of pages to split */
	s64 t64;
	s64 next = 0;

	INCREMENT(xtStat.search);

	BT_CLR(btstack);

	btstack->nsplit = 0;

	/*
	 *	search down tree from root:
	 *
	 * between two consecutive entries of <Ki, Pi> and <Kj, Pj> of
	 * internal page, child page Pi contains entry with k, Ki <= K < Kj.
	 *
	 * if entry with search key K is not found
	 * internal page search find the entry with largest key Ki
	 * less than K which point to the child page to search;
	 * leaf page search find the entry with smallest key Kj
	 * greater than K so that the returned index is the position of
	 * the entry to be shifted right for insertion of new entry.
	 * for empty tree, search key is greater than any key of the tree.
	 *
	 * by convention, root bn = 0.
	 */
	for (bn = 0;;) {
		/* get/pin the page to search */
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;

		/* try sequential access heuristics with the previous
		 * access entry in target leaf page:
		 * once search narrowed down into the target leaf,
		 * key must either match an entry in the leaf or
		 * key entry does not exist in the tree;
		 */
//fastSearch:
		if ((jfs_ip->btorder & BT_SEQUENTIAL) &&
		    (p->header.flag & BT_LEAF) &&
		    (index = jfs_ip->btindex) <
		    le16_to_cpu(p->header.nextindex)) {
			xad = &p->xad[index];
			t64 = offsetXAD(xad);
			if (xoff < t64 + lengthXAD(xad)) {
				if (xoff >= t64) {
					*cmpp = 0;
					goto out;
				}

				/* stop sequential access heuristics */
				goto binarySearch;
			} else {	/* (t64 + lengthXAD(xad)) <= xoff */

				/* try next sequential entry */
				index++;
				if (index <
				    le16_to_cpu(p->header.nextindex)) {
					xad++;
					t64 = offsetXAD(xad);
					if (xoff < t64 + lengthXAD(xad)) {
						if (xoff >= t64) {
							*cmpp = 0;
							goto out;
						}

						/* miss: key falls between
						 * previous and this entry
						 */
						*cmpp = 1;
						next = t64;
						goto out;
					}

					/* (xoff >= t64 + lengthXAD(xad));
					 * matching entry may be further out:
					 * stop heuristic search
					 */
					/* stop sequential access heuristics */
					goto binarySearch;
				}

				/* (index == p->header.nextindex);
				 * miss: key entry does not exist in
				 * the target leaf/tree
				 */
				*cmpp = 1;
				goto out;
			}

			/*
			 * if hit, return index of the entry found, and
			 * if miss, where new entry with search key is
			 * to be inserted;
			 */
		      out:
			/* compute number of pages to split */
			if (flag & XT_INSERT) {
				if (p->header.nextindex ==	/* little-endian */
				    p->header.maxentry)
					nsplit++;
				else
					nsplit = 0;
				btstack->nsplit = nsplit;
			}

			/* save search result */
			btsp = btstack->top;
			btsp->bn = bn;
			btsp->index = index;
			btsp->mp = mp;

			/* update sequential access heuristics */
			jfs_ip->btindex = index;

			if (nextp)
				*nextp = next;

			INCREMENT(xtStat.fastSearch);
			return 0;
		}

		/* well, ... full search now */
	      binarySearch:
		lim = le16_to_cpu(p->header.nextindex) - XTENTRYSTART;

		/*
		 * binary search with search key K on the current page
		 */
		for (base = XTENTRYSTART; lim; lim >>= 1) {
			index = base + (lim >> 1);

			XT_CMP(cmp, xoff, &p->xad[index], t64);
			if (cmp == 0) {
				/*
				 *	search hit
				 */
				/* search hit - leaf page:
				 * return the entry found
				 */
				if (p->header.flag & BT_LEAF) {
					*cmpp = cmp;

					/* compute number of pages to split */
					if (flag & XT_INSERT) {
						if (p->header.nextindex ==
						    p->header.maxentry)
							nsplit++;
						else
							nsplit = 0;
						btstack->nsplit = nsplit;
					}

					/* save search result */
					btsp = btstack->top;
					btsp->bn = bn;
					btsp->index = index;
					btsp->mp = mp;

					/* init sequential access heuristics */
					btindex = jfs_ip->btindex;
					if (index == btindex ||
					    index == btindex + 1)
						jfs_ip->btorder = BT_SEQUENTIAL;
					else
						jfs_ip->btorder = BT_RANDOM;
					jfs_ip->btindex = index;

					return 0;
				}
				/* search hit - internal page:
				 * descend/search its child page
				 */
				if (index < le16_to_cpu(p->header.nextindex)-1)
					next = offsetXAD(&p->xad[index + 1]);
				goto next;
			}

			if (cmp > 0) {
				base = index + 1;
				--lim;
			}
		}

		/*
		 *	search miss
		 *
		 * base is the smallest index with key (Kj) greater than
		 * search key (K) and may be zero or maxentry index.
		 */
		if (base < le16_to_cpu(p->header.nextindex))
			next = offsetXAD(&p->xad[base]);
		/*
		 * search miss - leaf page:
		 *
		 * return location of entry (base) where new entry with
		 * search key K is to be inserted.
		 */
		if (p->header.flag & BT_LEAF) {
			*cmpp = cmp;

			/* compute number of pages to split */
			if (flag & XT_INSERT) {
				if (p->header.nextindex ==
				    p->header.maxentry)
					nsplit++;
				else
					nsplit = 0;
				btstack->nsplit = nsplit;
			}

			/* save search result */
			btsp = btstack->top;
			btsp->bn = bn;
			btsp->index = base;
			btsp->mp = mp;

			/* init sequential access heuristics */
			btindex = jfs_ip->btindex;
			if (base == btindex || base == btindex + 1)
				jfs_ip->btorder = BT_SEQUENTIAL;
			else
				jfs_ip->btorder = BT_RANDOM;
			jfs_ip->btindex = base;

			if (nextp)
				*nextp = next;

			return 0;
		}

		/*
		 * search miss - non-leaf page:
		 *
		 * if base is non-zero, decrement base by one to get the parent
		 * entry of the child page to search.
		 */
		index = base ? base - 1 : base;

		/*
		 * go down to child page
		 */
	      next:
		/* update number of pages to split */
		if (p->header.nextindex == p->header.maxentry)
			nsplit++;
		else
			nsplit = 0;

		/* push (bn, index) of the parent page/entry */
		if (BT_STACK_FULL(btstack)) {
			jfs_error(ip->i_sb, "stack overrun in xtSearch!");
			XT_PUTPAGE(mp);
			return -EIO;
		}
		BT_PUSH(btstack, bn, index);

		/* get the child page block number */
		bn = addressXAD(&p->xad[index]);

		/* unpin the parent page */
		XT_PUTPAGE(mp);
	}
}

/*
 *	xtInsert()
 *
 * function:
 *
 * parameter:
 *	tid	- transaction id;
 *	ip	- file object;
 *	xflag	- extent flag (XAD_NOTRECORDED):
 *	xoff	- extent offset;
 *	xlen	- extent length;
 *	xaddrp	- extent address pointer (in/out):
 *		if (*xaddrp)
 *			caller allocated data extent at *xaddrp;
 *		else
 *			allocate data extent and return its xaddr;
 *	flag	-
 *
 * return:
 */
int xtInsert(tid_t tid,		/* transaction id */
	     struct inode *ip, int xflag, s64 xoff, s32 xlen, s64 * xaddrp,
	     int flag)
{
	int rc = 0;
	s64 xaddr, hint;
	struct metapage *mp;	/* meta-page buffer */
	xtpage_t *p;		/* base B+-tree index page */
	s64 bn;
	int index, nextindex;
	struct btstack btstack;	/* traverse stack */
	struct xtsplit split;	/* split information */
	xad_t *xad;
	int cmp;
	s64 next;
	struct tlock *tlck;
	struct xtlock *xtlck;

	jfs_info("xtInsert: nxoff:0x%lx nxlen:0x%x", (ulong) xoff, xlen);

	/*
	 *	search for the entry location at which to insert:
	 *
	 * xtFastSearch() and xtSearch() both returns (leaf page
	 * pinned, index at which to insert).
	 * n.b. xtSearch() may return index of maxentry of
	 * the full page.
	 */
	if ((rc = xtSearch(ip, xoff, &next, &cmp, &btstack, XT_INSERT)))
		return rc;

	/* retrieve search result */
	XT_GETSEARCH(ip, btstack.top, bn, mp, p, index);

	/* This test must follow XT_GETSEARCH since mp must be valid if
	 * we branch to out: */
	if ((cmp == 0) || (next && (xlen > next - xoff))) {
		rc = -EEXIST;
		goto out;
	}

	/*
	 * allocate data extent requested
	 *
	 * allocation hint: last xad
	 */
	if ((xaddr = *xaddrp) == 0) {
		if (index > XTENTRYSTART) {
			xad = &p->xad[index - 1];
			hint = addressXAD(xad) + lengthXAD(xad) - 1;
		} else
			hint = 0;
		if ((rc = DQUOT_ALLOC_BLOCK(ip, xlen)))
			goto out;
		if ((rc = dbAlloc(ip, hint, (s64) xlen, &xaddr))) {
			DQUOT_FREE_BLOCK(ip, xlen);
			goto out;
		}
	}

	/*
	 *	insert entry for new extent
	 */
	xflag |= XAD_NEW;

	/*
	 *	if the leaf page is full, split the page and
	 *	propagate up the router entry for the new page from split
	 *
	 * The xtSplitUp() will insert the entry and unpin the leaf page.
	 */
	nextindex = le16_to_cpu(p->header.nextindex);
	if (nextindex == le16_to_cpu(p->header.maxentry)) {
		split.mp = mp;
		split.index = index;
		split.flag = xflag;
		split.off = xoff;
		split.len = xlen;
		split.addr = xaddr;
		split.pxdlist = NULL;
		if ((rc = xtSplitUp(tid, ip, &split, &btstack))) {
			/* undo data extent allocation */
			if (*xaddrp == 0) {
				dbFree(ip, xaddr, (s64) xlen);
				DQUOT_FREE_BLOCK(ip, xlen);
			}
			return rc;
		}

		*xaddrp = xaddr;
		return 0;
	}

	/*
	 *	insert the new entry into the leaf page
	 */
	/*
	 * acquire a transaction lock on the leaf page;
	 *
	 * action: xad insertion/extension;
	 */
	BT_MARK_DIRTY(mp, ip);

	/* if insert into middle, shift right remaining entries. */
	if (index < nextindex)
		memmove(&p->xad[index + 1], &p->xad[index],
			(nextindex - index) * sizeof(xad_t));

	/* insert the new entry: mark the entry NEW */
	xad = &p->xad[index];
	XT_PUTENTRY(xad, xflag, xoff, xlen, xaddr);

	/* advance next available entry index */
	p->header.nextindex =
	    cpu_to_le16(le16_to_cpu(p->header.nextindex) + 1);

	/* Don't log it if there are no links to the file */
	if (!test_cflag(COMMIT_Nolink, ip)) {
		tlck = txLock(tid, ip, mp, tlckXTREE | tlckGROW);
		xtlck = (struct xtlock *) & tlck->lock;
		xtlck->lwm.offset =
		    (xtlck->lwm.offset) ? min(index,
					      (int)xtlck->lwm.offset) : index;
		xtlck->lwm.length =
		    le16_to_cpu(p->header.nextindex) - xtlck->lwm.offset;
	}

	*xaddrp = xaddr;

      out:
	/* unpin the leaf page */
	XT_PUTPAGE(mp);

	return rc;
}


/*
 *	xtSplitUp()
 *
 * function:
 *	split full pages as propagating insertion up the tree
 *
 * parameter:
 *	tid	- transaction id;
 *	ip	- file object;
 *	split	- entry parameter descriptor;
 *	btstack - traverse stack from xtSearch()
 *
 * return:
 */
static int
xtSplitUp(tid_t tid,
	  struct inode *ip, struct xtsplit * split, struct btstack * btstack)
{
	int rc = 0;
	struct metapage *smp;
	xtpage_t *sp;		/* split page */
	struct metapage *rmp;
	s64 rbn;		/* new right page block number */
	struct metapage *rcmp;
	xtpage_t *rcp;		/* right child page */
	s64 rcbn;		/* right child page block number */
	int skip;		/* index of entry of insertion */
	int nextindex;		/* next available entry index of p */
	struct btframe *parent;	/* parent page entry on traverse stack */
	xad_t *xad;
	s64 xaddr;
	int xlen;
	int nsplit;		/* number of pages split */
	struct pxdlist pxdlist;
	pxd_t *pxd;
	struct tlock *tlck;
	struct xtlock *xtlck;

	smp = split->mp;
	sp = XT_PAGE(ip, smp);

	/* is inode xtree root extension/inline EA area free ? */
	if ((sp->header.flag & BT_ROOT) && (!S_ISDIR(ip->i_mode)) &&
	    (le16_to_cpu(sp->header.maxentry) < XTROOTMAXSLOT) &&
	    (JFS_IP(ip)->mode2 & INLINEEA)) {
		sp->header.maxentry = cpu_to_le16(XTROOTMAXSLOT);
		JFS_IP(ip)->mode2 &= ~INLINEEA;

		BT_MARK_DIRTY(smp, ip);
		/*
		 * acquire a transaction lock on the leaf page;
		 *
		 * action: xad insertion/extension;
		 */

		/* if insert into middle, shift right remaining entries. */
		skip = split->index;
		nextindex = le16_to_cpu(sp->header.nextindex);
		if (skip < nextindex)
			memmove(&sp->xad[skip + 1], &sp->xad[skip],
				(nextindex - skip) * sizeof(xad_t));

		/* insert the new entry: mark the entry NEW */
		xad = &sp->xad[skip];
		XT_PUTENTRY(xad, split->flag, split->off, split->len,
			    split->addr);

		/* advance next available entry index */
		sp->header.nextindex =
		    cpu_to_le16(le16_to_cpu(sp->header.nextindex) + 1);

		/* Don't log it if there are no links to the file */
		if (!test_cflag(COMMIT_Nolink, ip)) {
			tlck = txLock(tid, ip, smp, tlckXTREE | tlckGROW);
			xtlck = (struct xtlock *) & tlck->lock;
			xtlck->lwm.offset = (xtlck->lwm.offset) ?
			    min(skip, (int)xtlck->lwm.offset) : skip;
			xtlck->lwm.length =
			    le16_to_cpu(sp->header.nextindex) -
			    xtlck->lwm.offset;
		}

		return 0;
	}

	/*
	 * allocate new index blocks to cover index page split(s)
	 *
	 * allocation hint: ?
	 */
	if (split->pxdlist == NULL) {
		nsplit = btstack->nsplit;
		split->pxdlist = &pxdlist;
		pxdlist.maxnpxd = pxdlist.npxd = 0;
		pxd = &pxdlist.pxd[0];
		xlen = JFS_SBI(ip->i_sb)->nbperpage;
		for (; nsplit > 0; nsplit--, pxd++) {
			if ((rc = dbAlloc(ip, (s64) 0, (s64) xlen, &xaddr))
			    == 0) {
				PXDaddress(pxd, xaddr);
				PXDlength(pxd, xlen);

				pxdlist.maxnpxd++;

				continue;
			}

			/* undo allocation */

			XT_PUTPAGE(smp);
			return rc;
		}
	}

	/*
	 * Split leaf page <sp> into <sp> and a new right page <rp>.
	 *
	 * The split routines insert the new entry into the leaf page,
	 * and acquire txLock as appropriate.
	 * return <rp> pinned and its block number <rpbn>.
	 */
	rc = (sp->header.flag & BT_ROOT) ?
	    xtSplitRoot(tid, ip, split, &rmp) :
	    xtSplitPage(tid, ip, split, &rmp, &rbn);

	XT_PUTPAGE(smp);

	if (rc)
		return -EIO;
	/*
	 * propagate up the router entry for the leaf page just split
	 *
	 * insert a router entry for the new page into the parent page,
	 * propagate the insert/split up the tree by walking back the stack
	 * of (bn of parent page, index of child page entry in parent page)
	 * that were traversed during the search for the page that split.
	 *
	 * the propagation of insert/split up the tree stops if the root
	 * splits or the page inserted into doesn't have to split to hold
	 * the new entry.
	 *
	 * the parent entry for the split page remains the same, and
	 * a new entry is inserted at its right with the first key and
	 * block number of the new right page.
	 *
	 * There are a maximum of 3 pages pinned at any time:
	 * right child, left parent and right parent (when the parent splits)
	 * to keep the child page pinned while working on the parent.
	 * make sure that all pins are released at exit.
	 */
	while ((parent = BT_POP(btstack)) != NULL) {
		/* parent page specified by stack frame <parent> */

		/* keep current child pages <rcp> pinned */
		rcmp = rmp;
		rcbn = rbn;
		rcp = XT_PAGE(ip, rcmp);

		/*
		 * insert router entry in parent for new right child page <rp>
		 */
		/* get/pin the parent page <sp> */
		XT_GETPAGE(ip, parent->bn, smp, PSIZE, sp, rc);
		if (rc) {
			XT_PUTPAGE(rcmp);
			return rc;
		}

		/*
		 * The new key entry goes ONE AFTER the index of parent entry,
		 * because the split was to the right.
		 */
		skip = parent->index + 1;

		/*
		 * split or shift right remaining entries of the parent page
		 */
		nextindex = le16_to_cpu(sp->header.nextindex);
		/*
		 * parent page is full - split the parent page
		 */
		if (nextindex == le16_to_cpu(sp->header.maxentry)) {
			/* init for parent page split */
			split->mp = smp;
			split->index = skip;	/* index at insert */
			split->flag = XAD_NEW;
			split->off = offsetXAD(&rcp->xad[XTENTRYSTART]);
			split->len = JFS_SBI(ip->i_sb)->nbperpage;
			split->addr = rcbn;

			/* unpin previous right child page */
			XT_PUTPAGE(rcmp);

			/* The split routines insert the new entry,
			 * and acquire txLock as appropriate.
			 * return <rp> pinned and its block number <rpbn>.
			 */
			rc = (sp->header.flag & BT_ROOT) ?
			    xtSplitRoot(tid, ip, split, &rmp) :
			    xtSplitPage(tid, ip, split, &rmp, &rbn);
			if (rc) {
				XT_PUTPAGE(smp);
				return rc;
			}

			XT_PUTPAGE(smp);
			/* keep new child page <rp> pinned */
		}
		/*
		 * parent page is not full - insert in parent page
		 */
		else {
			/*
			 * insert router entry in parent for the right child
			 * page from the first entry of the right child page:
			 */
			/*
			 * acquire a transaction lock on the parent page;
			 *
			 * action: router xad insertion;
			 */
			BT_MARK_DIRTY(smp, ip);

			/*
			 * if insert into middle, shift right remaining entries
			 */
			if (skip < nextindex)
				memmove(&sp->xad[skip + 1], &sp->xad[skip],
					(nextindex -
					 skip) << L2XTSLOTSIZE);

			/* insert the router entry */
			xad = &sp->xad[skip];
			XT_PUTENTRY(xad, XAD_NEW,
				    offsetXAD(&rcp->xad[XTENTRYSTART]),
				    JFS_SBI(ip->i_sb)->nbperpage, rcbn);

			/* advance next available entry index. */
			sp->header.nextindex =
			    cpu_to_le16(le16_to_cpu(sp->header.nextindex) +
					1);

			/* Don't log it if there are no links to the file */
			if (!test_cflag(COMMIT_Nolink, ip)) {
				tlck = txLock(tid, ip, smp,
					      tlckXTREE | tlckGROW);
				xtlck = (struct xtlock *) & tlck->lock;
				xtlck->lwm.offset = (xtlck->lwm.offset) ?
				    min(skip, (int)xtlck->lwm.offset) : skip;
				xtlck->lwm.length =
				    le16_to_cpu(sp->header.nextindex) -
				    xtlck->lwm.offset;
			}

			/* unpin parent page */
			XT_PUTPAGE(smp);

			/* exit propagate up */
			break;
		}
	}

	/* unpin current right page */
	XT_PUTPAGE(rmp);

	return 0;
}


/*
 *	xtSplitPage()
 *
 * function:
 *	split a full non-root page into
 *	original/split/left page and new right page
 *	i.e., the original/split page remains as left page.
 *
 * parameter:
 *	int		tid,
 *	struct inode	*ip,
 *	struct xtsplit	*split,
 *	struct metapage	**rmpp,
 *	u64		*rbnp,
 *
 * return:
 *	Pointer to page in which to insert or NULL on error.
 */
static int
xtSplitPage(tid_t tid, struct inode *ip,
	    struct xtsplit * split, struct metapage ** rmpp, s64 * rbnp)
{
	int rc = 0;
	struct metapage *smp;
	xtpage_t *sp;
	struct metapage *rmp;
	xtpage_t *rp;		/* new right page allocated */
	s64 rbn;		/* new right page block number */
	struct metapage *mp;
	xtpage_t *p;
	s64 nextbn;
	int skip, maxentry, middle, righthalf, n;
	xad_t *xad;
	struct pxdlist *pxdlist;
	pxd_t *pxd;
	struct tlock *tlck;
	struct xtlock *sxtlck = NULL, *rxtlck = NULL;
	int quota_allocation = 0;

	smp = split->mp;
	sp = XT_PAGE(ip, smp);

	INCREMENT(xtStat.split);

	pxdlist = split->pxdlist;
	pxd = &pxdlist->pxd[pxdlist->npxd];
	pxdlist->npxd++;
	rbn = addressPXD(pxd);

	/* Allocate blocks to quota. */
	if (DQUOT_ALLOC_BLOCK(ip, lengthPXD(pxd))) {
		rc = -EDQUOT;
		goto clean_up;
	}

	quota_allocation += lengthPXD(pxd);

	/*
	 * allocate the new right page for the split
	 */
	rmp = get_metapage(ip, rbn, PSIZE, 1);
	if (rmp == NULL) {
		rc = -EIO;
		goto clean_up;
	}

	jfs_info("xtSplitPage: ip:0x%p smp:0x%p rmp:0x%p", ip, smp, rmp);

	BT_MARK_DIRTY(rmp, ip);
	/*
	 * action: new page;
	 */

	rp = (xtpage_t *) rmp->data;
	rp->header.self = *pxd;
	rp->header.flag = sp->header.flag & BT_TYPE;
	rp->header.maxentry = sp->header.maxentry;	/* little-endian */
	rp->header.nextindex = cpu_to_le16(XTENTRYSTART);

	BT_MARK_DIRTY(smp, ip);
	/* Don't log it if there are no links to the file */
	if (!test_cflag(COMMIT_Nolink, ip)) {
		/*
		 * acquire a transaction lock on the new right page;
		 */
		tlck = txLock(tid, ip, rmp, tlckXTREE | tlckNEW);
		rxtlck = (struct xtlock *) & tlck->lock;
		rxtlck->lwm.offset = XTENTRYSTART;
		/*
		 * acquire a transaction lock on the split page
		 */
		tlck = txLock(tid, ip, smp, tlckXTREE | tlckGROW);
		sxtlck = (struct xtlock *) & tlck->lock;
	}

	/*
	 * initialize/update sibling pointers of <sp> and <rp>
	 */
	nextbn = le64_to_cpu(sp->header.next);
	rp->header.next = cpu_to_le64(nextbn);
	rp->header.prev = cpu_to_le64(addressPXD(&sp->header.self));
	sp->header.next = cpu_to_le64(rbn);

	skip = split->index;

	/*
	 *	sequential append at tail (after last entry of last page)
	 *
	 * if splitting the last page on a level because of appending
	 * a entry to it (skip is maxentry), it's likely that the access is
	 * sequential. adding an empty page on the side of the level is less
	 * work and can push the fill factor much higher than normal.
	 * if we're wrong it's no big deal -  we will do the split the right
	 * way next time.
	 * (it may look like it's equally easy to do a similar hack for
	 * reverse sorted data, that is, split the tree left, but it's not.
	 * Be my guest.)
	 */
	if (nextbn == 0 && skip == le16_to_cpu(sp->header.maxentry)) {
		/*
		 * acquire a transaction lock on the new/right page;
		 *
		 * action: xad insertion;
		 */
		/* insert entry at the first entry of the new right page */
		xad = &rp->xad[XTENTRYSTART];
		XT_PUTENTRY(xad, split->flag, split->off, split->len,
			    split->addr);

		rp->header.nextindex = cpu_to_le16(XTENTRYSTART + 1);

		if (!test_cflag(COMMIT_Nolink, ip)) {
			/* rxtlck->lwm.offset = XTENTRYSTART; */
			rxtlck->lwm.length = 1;
		}

		*rmpp = rmp;
		*rbnp = rbn;

		jfs_info("xtSplitPage: sp:0x%p rp:0x%p", sp, rp);
		return 0;
	}

	/*
	 *	non-sequential insert (at possibly middle page)
	 */

	/*
	 * update previous pointer of old next/right page of <sp>
	 */
	if (nextbn != 0) {
		XT_GETPAGE(ip, nextbn, mp, PSIZE, p, rc);
		if (rc) {
			XT_PUTPAGE(rmp);
			goto clean_up;
		}

		BT_MARK_DIRTY(mp, ip);
		/*
		 * acquire a transaction lock on the next page;
		 *
		 * action:sibling pointer update;
		 */
		if (!test_cflag(COMMIT_Nolink, ip))
			tlck = txLock(tid, ip, mp, tlckXTREE | tlckRELINK);

		p->header.prev = cpu_to_le64(rbn);

		/* sibling page may have been updated previously, or
		 * it may be updated later;
		 */

		XT_PUTPAGE(mp);
	}

	/*
	 * split the data between the split and new/right pages
	 */
	maxentry = le16_to_cpu(sp->header.maxentry);
	middle = maxentry >> 1;
	righthalf = maxentry - middle;

	/*
	 * skip index in old split/left page - insert into left page:
	 */
	if (skip <= middle) {
		/* move right half of split page to the new right page */
		memmove(&rp->xad[XTENTRYSTART], &sp->xad[middle],
			righthalf << L2XTSLOTSIZE);

		/* shift right tail of left half to make room for new entry */
		if (skip < middle)
			memmove(&sp->xad[skip + 1], &sp->xad[skip],
				(middle - skip) << L2XTSLOTSIZE);

		/* insert new entry */
		xad = &sp->xad[skip];
		XT_PUTENTRY(xad, split->flag, split->off, split->len,
			    split->addr);

		/* update page header */
		sp->header.nextindex = cpu_to_le16(middle + 1);
		if (!test_cflag(COMMIT_Nolink, ip)) {
			sxtlck->lwm.offset = (sxtlck->lwm.offset) ?
			    min(skip, (int)sxtlck->lwm.offset) : skip;
		}

		rp->header.nextindex =
		    cpu_to_le16(XTENTRYSTART + righthalf);
	}
	/*
	 * skip index in new right page - insert into right page:
	 */
	else {
		/* move left head of right half to right page */
		n = skip - middle;
		memmove(&rp->xad[XTENTRYSTART], &sp->xad[middle],
			n << L2XTSLOTSIZE);

		/* insert new entry */
		n += XTENTRYSTART;
		xad = &rp->xad[n];
		XT_PUTENTRY(xad, split->flag, split->off, split->len,
			    split->addr);

		/* move right tail of right half to right page */
		if (skip < maxentry)
			memmove(&rp->xad[n + 1], &sp->xad[skip],
				(maxentry - skip) << L2XTSLOTSIZE);

		/* update page header */
		sp->header.nextindex = cpu_to_le16(middle);
		if (!test_cflag(COMMIT_Nolink, ip)) {
			sxtlck->lwm.offset = (sxtlck->lwm.offset) ?
			    min(middle, (int)sxtlck->lwm.offset) : middle;
		}

		rp->header.nextindex = cpu_to_le16(XTENTRYSTART +
						   righthalf + 1);
	}

	if (!test_cflag(COMMIT_Nolink, ip)) {
		sxtlck->lwm.length = le16_to_cpu(sp->header.nextindex) -
		    sxtlck->lwm.offset;

		/* rxtlck->lwm.offset = XTENTRYSTART; */
		rxtlck->lwm.length = le16_to_cpu(rp->header.nextindex) -
		    XTENTRYSTART;
	}

	*rmpp = rmp;
	*rbnp = rbn;

	jfs_info("xtSplitPage: sp:0x%p rp:0x%p", sp, rp);
	return rc;

      clean_up:

	/* Rollback quota allocation. */
	if (quota_allocation)
		DQUOT_FREE_BLOCK(ip, quota_allocation);

	return (rc);
}


/*
 *	xtSplitRoot()
 *
 * function:
 *	split the full root page into original/root/split page and new
 *	right page
 *	i.e., root remains fixed in tree anchor (inode) and the root is
 *	copied to a single new right child page since root page <<
 *	non-root page, and the split root page contains a single entry
 *	for the new right child page.
 *
 * parameter:
 *	int		tid,
 *	struct inode	*ip,
 *	struct xtsplit	*split,
 *	struct metapage	**rmpp)
 *
 * return:
 *	Pointer to page in which to insert or NULL on error.
 */
static int
xtSplitRoot(tid_t tid,
	    struct inode *ip, struct xtsplit * split, struct metapage ** rmpp)
{
	xtpage_t *sp;
	struct metapage *rmp;
	xtpage_t *rp;
	s64 rbn;
	int skip, nextindex;
	xad_t *xad;
	pxd_t *pxd;
	struct pxdlist *pxdlist;
	struct tlock *tlck;
	struct xtlock *xtlck;

	sp = &JFS_IP(ip)->i_xtroot;

	INCREMENT(xtStat.split);

	/*
	 *	allocate a single (right) child page
	 */
	pxdlist = split->pxdlist;
	pxd = &pxdlist->pxd[pxdlist->npxd];
	pxdlist->npxd++;
	rbn = addressPXD(pxd);
	rmp = get_metapage(ip, rbn, PSIZE, 1);
	if (rmp == NULL)
		return -EIO;

	/* Allocate blocks to quota. */
	if (DQUOT_ALLOC_BLOCK(ip, lengthPXD(pxd))) {
		release_metapage(rmp);
		return -EDQUOT;
	}

	jfs_info("xtSplitRoot: ip:0x%p rmp:0x%p", ip, rmp);

	/*
	 * acquire a transaction lock on the new right page;
	 *
	 * action: new page;
	 */
	BT_MARK_DIRTY(rmp, ip);

	rp = (xtpage_t *) rmp->data;
	rp->header.flag =
	    (sp->header.flag & BT_LEAF) ? BT_LEAF : BT_INTERNAL;
	rp->header.self = *pxd;
	rp->header.nextindex = cpu_to_le16(XTENTRYSTART);
	rp->header.maxentry = cpu_to_le16(PSIZE >> L2XTSLOTSIZE);

	/* initialize sibling pointers */
	rp->header.next = 0;
	rp->header.prev = 0;

	/*
	 * copy the in-line root page into new right page extent
	 */
	nextindex = le16_to_cpu(sp->header.maxentry);
	memmove(&rp->xad[XTENTRYSTART], &sp->xad[XTENTRYSTART],
		(nextindex - XTENTRYSTART) << L2XTSLOTSIZE);

	/*
	 * insert the new entry into the new right/child page
	 * (skip index in the new right page will not change)
	 */
	skip = split->index;
	/* if insert into middle, shift right remaining entries */
	if (skip != nextindex)
		memmove(&rp->xad[skip + 1], &rp->xad[skip],
			(nextindex - skip) * sizeof(xad_t));

	xad = &rp->xad[skip];
	XT_PUTENTRY(xad, split->flag, split->off, split->len, split->addr);

	/* update page header */
	rp->header.nextindex = cpu_to_le16(nextindex + 1);

	if (!test_cflag(COMMIT_Nolink, ip)) {
		tlck = txLock(tid, ip, rmp, tlckXTREE | tlckNEW);
		xtlck = (struct xtlock *) & tlck->lock;
		xtlck->lwm.offset = XTENTRYSTART;
		xtlck->lwm.length = le16_to_cpu(rp->header.nextindex) -
		    XTENTRYSTART;
	}

	/*
	 *	reset the root
	 *
	 * init root with the single entry for the new right page
	 * set the 1st entry offset to 0, which force the left-most key
	 * at any level of the tree to be less than any search key.
	 */
	/*
	 * acquire a transaction lock on the root page (in-memory inode);
	 *
	 * action: root split;
	 */
	BT_MARK_DIRTY(split->mp, ip);

	xad = &sp->xad[XTENTRYSTART];
	XT_PUTENTRY(xad, XAD_NEW, 0, JFS_SBI(ip->i_sb)->nbperpage, rbn);

	/* update page header of root */
	sp->header.flag &= ~BT_LEAF;
	sp->header.flag |= BT_INTERNAL;

	sp->header.nextindex = cpu_to_le16(XTENTRYSTART + 1);

	if (!test_cflag(COMMIT_Nolink, ip)) {
		tlck = txLock(tid, ip, split->mp, tlckXTREE | tlckGROW);
		xtlck = (struct xtlock *) & tlck->lock;
		xtlck->lwm.offset = XTENTRYSTART;
		xtlck->lwm.length = 1;
	}

	*rmpp = rmp;

	jfs_info("xtSplitRoot: sp:0x%p rp:0x%p", sp, rp);
	return 0;
}


/*
 *	xtExtend()
 *
 * function: extend in-place;
 *
 * note: existing extent may or may not have been committed.
 * caller is responsible for pager buffer cache update, and
 * working block allocation map update;
 * update pmap: alloc whole extended extent;
 */
int xtExtend(tid_t tid,		/* transaction id */
	     struct inode *ip, s64 xoff,	/* delta extent offset */
	     s32 xlen,		/* delta extent length */
	     int flag)
{
	int rc = 0;
	int cmp;
	struct metapage *mp;	/* meta-page buffer */
	xtpage_t *p;		/* base B+-tree index page */
	s64 bn;
	int index, nextindex, len;
	struct btstack btstack;	/* traverse stack */
	struct xtsplit split;	/* split information */
	xad_t *xad;
	s64 xaddr;
	struct tlock *tlck;
	struct xtlock *xtlck = NULL;

	jfs_info("xtExtend: nxoff:0x%lx nxlen:0x%x", (ulong) xoff, xlen);

	/* there must exist extent to be extended */
	if ((rc = xtSearch(ip, xoff - 1, NULL, &cmp, &btstack, XT_INSERT)))
		return rc;

	/* retrieve search result */
	XT_GETSEARCH(ip, btstack.top, bn, mp, p, index);

	if (cmp != 0) {
		XT_PUTPAGE(mp);
		jfs_error(ip->i_sb, "xtExtend: xtSearch did not find extent");
		return -EIO;
	}

	/* extension must be contiguous */
	xad = &p->xad[index];
	if ((offsetXAD(xad) + lengthXAD(xad)) != xoff) {
		XT_PUTPAGE(mp);
		jfs_error(ip->i_sb, "xtExtend: extension is not contiguous");
		return -EIO;
	}

	/*
	 * acquire a transaction lock on the leaf page;
	 *
	 * action: xad insertion/extension;
	 */
	BT_MARK_DIRTY(mp, ip);
	if (!test_cflag(COMMIT_Nolink, ip)) {
		tlck = txLock(tid, ip, mp, tlckXTREE | tlckGROW);
		xtlck = (struct xtlock *) & tlck->lock;
	}

	/* extend will overflow extent ? */
	xlen = lengthXAD(xad) + xlen;
	if ((len = xlen - MAXXLEN) <= 0)
		goto extendOld;

	/*
	 *	extent overflow: insert entry for new extent
	 */
//insertNew:
	xoff = offsetXAD(xad) + MAXXLEN;
	xaddr = addressXAD(xad) + MAXXLEN;
	nextindex = le16_to_cpu(p->header.nextindex);

	/*
	 *	if the leaf page is full, insert the new entry and
	 *	propagate up the router entry for the new page from split
	 *
	 * The xtSplitUp() will insert the entry and unpin the leaf page.
	 */
	if (nextindex == le16_to_cpu(p->header.maxentry)) {
		/* xtSpliUp() unpins leaf pages */
		split.mp = mp;
		split.index = index + 1;
		split.flag = XAD_NEW;
		split.off = xoff;	/* split offset */
		split.len = len;
		split.addr = xaddr;
		split.pxdlist = NULL;
		if ((rc = xtSplitUp(tid, ip, &split, &btstack)))
			return rc;

		/* get back old page */
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;
		/*
		 * if leaf root has been split, original root has been
		 * copied to new child page, i.e., original entry now
		 * resides on the new child page;
		 */
		if (p->header.flag & BT_INTERNAL) {
			ASSERT(p->header.nextindex ==
			       cpu_to_le16(XTENTRYSTART + 1));
			xad = &p->xad[XTENTRYSTART];
			bn = addressXAD(xad);
			XT_PUTPAGE(mp);

			/* get new child page */
			XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
			if (rc)
				return rc;

			BT_MARK_DIRTY(mp, ip);
			if (!test_cflag(COMMIT_Nolink, ip)) {
				tlck = txLock(tid, ip, mp, tlckXTREE|tlckGROW);
				xtlck = (struct xtlock *) & tlck->lock;
			}
		}
	}
	/*
	 *	insert the new entry into the leaf page
	 */
	else {
		/* insert the new entry: mark the entry NEW */
		xad = &p->xad[index + 1];
		XT_PUTENTRY(xad, XAD_NEW, xoff, len, xaddr);

		/* advance next available entry index */
		p->header.nextindex =
		    cpu_to_le16(le16_to_cpu(p->header.nextindex) + 1);
	}

	/* get back old entry */
	xad = &p->xad[index];
	xlen = MAXXLEN;

	/*
	 * extend old extent
	 */
      extendOld:
	XADlength(xad, xlen);
	if (!(xad->flag & XAD_NEW))
		xad->flag |= XAD_EXTENDED;

	if (!test_cflag(COMMIT_Nolink, ip)) {
		xtlck->lwm.offset =
		    (xtlck->lwm.offset) ? min(index,
					      (int)xtlck->lwm.offset) : index;
		xtlck->lwm.length =
		    le16_to_cpu(p->header.nextindex) - xtlck->lwm.offset;
	}

	/* unpin the leaf page */
	XT_PUTPAGE(mp);

	return rc;
}

#ifdef _NOTYET
/*
 *	xtTailgate()
 *
 * function: split existing 'tail' extent
 *	(split offset >= start offset of tail extent), and
 *	relocate and extend the split tail half;
 *
 * note: existing extent may or may not have been committed.
 * caller is responsible for pager buffer cache update, and
 * working block allocation map update;
 * update pmap: free old split tail extent, alloc new extent;
 */
int xtTailgate(tid_t tid,		/* transaction id */
	       struct inode *ip, s64 xoff,	/* split/new extent offset */
	       s32 xlen,	/* new extent length */
	       s64 xaddr,	/* new extent address */
	       int flag)
{
	int rc = 0;
	int cmp;
	struct metapage *mp;	/* meta-page buffer */
	xtpage_t *p;		/* base B+-tree index page */
	s64 bn;
	int index, nextindex, llen, rlen;
	struct btstack btstack;	/* traverse stack */
	struct xtsplit split;	/* split information */
	xad_t *xad;
	struct tlock *tlck;
	struct xtlock *xtlck = 0;
	struct tlock *mtlck;
	struct maplock *pxdlock;

/*
printf("xtTailgate: nxoff:0x%lx nxlen:0x%x nxaddr:0x%lx\n",
	(ulong)xoff, xlen, (ulong)xaddr);
*/

	/* there must exist extent to be tailgated */
	if ((rc = xtSearch(ip, xoff, NULL, &cmp, &btstack, XT_INSERT)))
		return rc;

	/* retrieve search result */
	XT_GETSEARCH(ip, btstack.top, bn, mp, p, index);

	if (cmp != 0) {
		XT_PUTPAGE(mp);
		jfs_error(ip->i_sb, "xtTailgate: couldn't find extent");
		return -EIO;
	}

	/* entry found must be last entry */
	nextindex = le16_to_cpu(p->header.nextindex);
	if (index != nextindex - 1) {
		XT_PUTPAGE(mp);
		jfs_error(ip->i_sb,
			  "xtTailgate: the entry found is not the last entry");
		return -EIO;
	}

	BT_MARK_DIRTY(mp, ip);
	/*
	 * acquire tlock of the leaf page containing original entry
	 */
	if (!test_cflag(COMMIT_Nolink, ip)) {
		tlck = txLock(tid, ip, mp, tlckXTREE | tlckGROW);
		xtlck = (struct xtlock *) & tlck->lock;
	}

	/* completely replace extent ? */
	xad = &p->xad[index];
/*
printf("xtTailgate: xoff:0x%lx xlen:0x%x xaddr:0x%lx\n",
	(ulong)offsetXAD(xad), lengthXAD(xad), (ulong)addressXAD(xad));
*/
	if ((llen = xoff - offsetXAD(xad)) == 0)
		goto updateOld;

	/*
	 *	partially replace extent: insert entry for new extent
	 */
//insertNew:
	/*
	 *	if the leaf page is full, insert the new entry and
	 *	propagate up the router entry for the new page from split
	 *
	 * The xtSplitUp() will insert the entry and unpin the leaf page.
	 */
	if (nextindex == le16_to_cpu(p->header.maxentry)) {
		/* xtSpliUp() unpins leaf pages */
		split.mp = mp;
		split.index = index + 1;
		split.flag = XAD_NEW;
		split.off = xoff;	/* split offset */
		split.len = xlen;
		split.addr = xaddr;
		split.pxdlist = NULL;
		if ((rc = xtSplitUp(tid, ip, &split, &btstack)))
			return rc;

		/* get back old page */
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;
		/*
		 * if leaf root has been split, original root has been
		 * copied to new child page, i.e., original entry now
		 * resides on the new child page;
		 */
		if (p->header.flag & BT_INTERNAL) {
			ASSERT(p->header.nextindex ==
			       cpu_to_le16(XTENTRYSTART + 1));
			xad = &p->xad[XTENTRYSTART];
			bn = addressXAD(xad);
			XT_PUTPAGE(mp);

			/* get new child page */
			XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
			if (rc)
				return rc;

			BT_MARK_DIRTY(mp, ip);
			if (!test_cflag(COMMIT_Nolink, ip)) {
				tlck = txLock(tid, ip, mp, tlckXTREE|tlckGROW);
				xtlck = (struct xtlock *) & tlck->lock;
			}
		}
	}
	/*
	 *	insert the new entry into the leaf page
	 */
	else {
		/* insert the new entry: mark the entry NEW */
		xad = &p->xad[index + 1];
		XT_PUTENTRY(xad, XAD_NEW, xoff, xlen, xaddr);

		/* advance next available entry index */
		p->header.nextindex =
		    cpu_to_le16(le16_to_cpu(p->header.nextindex) + 1);
	}

	/* get back old XAD */
	xad = &p->xad[index];

	/*
	 * truncate/relocate old extent at split offset
	 */
      updateOld:
	/* update dmap for old/committed/truncated extent */
	rlen = lengthXAD(xad) - llen;
	if (!(xad->flag & XAD_NEW)) {
		/* free from PWMAP at commit */
		if (!test_cflag(COMMIT_Nolink, ip)) {
			mtlck = txMaplock(tid, ip, tlckMAP);
			pxdlock = (struct maplock *) & mtlck->lock;
			pxdlock->flag = mlckFREEPXD;
			PXDaddress(&pxdlock->pxd, addressXAD(xad) + llen);
			PXDlength(&pxdlock->pxd, rlen);
			pxdlock->index = 1;
		}
	} else
		/* free from WMAP */
		dbFree(ip, addressXAD(xad) + llen, (s64) rlen);

	if (llen)
		/* truncate */
		XADlength(xad, llen);
	else
		/* replace */
		XT_PUTENTRY(xad, XAD_NEW, xoff, xlen, xaddr);

	if (!test_cflag(COMMIT_Nolink, ip)) {
		xtlck->lwm.offset = (xtlck->lwm.offset) ?
		    min(index, (int)xtlck->lwm.offset) : index;
		xtlck->lwm.length = le16_to_cpu(p->header.nextindex) -
		    xtlck->lwm.offset;
	}

	/* unpin the leaf page */
	XT_PUTPAGE(mp);

	return rc;
}
#endif /* _NOTYET */

/*
 *	xtUpdate()
 *
 * function: update XAD;
 *
 *	update extent for allocated_but_not_recorded or
 *	compressed extent;
 *
 * parameter:
 *	nxad	- new XAD;
 *		logical extent of the specified XAD must be completely
 *		contained by an existing XAD;
 */
int xtUpdate(tid_t tid, struct inode *ip, xad_t * nxad)
{				/* new XAD */
	int rc = 0;
	int cmp;
	struct metapage *mp;	/* meta-page buffer */
	xtpage_t *p;		/* base B+-tree index page */
	s64 bn;
	int index0, index, newindex, nextindex;
	struct btstack btstack;	/* traverse stack */
	struct xtsplit split;	/* split information */
	xad_t *xad, *lxad, *rxad;
	int xflag;
	s64 nxoff, xoff;
	int nxlen, xlen, lxlen, rxlen;
	s64 nxaddr, xaddr;
	struct tlock *tlck;
	struct xtlock *xtlck = NULL;
	int newpage = 0;

	/* there must exist extent to be tailgated */
	nxoff = offsetXAD(nxad);
	nxlen = lengthXAD(nxad);
	nxaddr = addressXAD(nxad);

	if ((rc = xtSearch(ip, nxoff, NULL, &cmp, &btstack, XT_INSERT)))
		return rc;

	/* retrieve search result */
	XT_GETSEARCH(ip, btstack.top, bn, mp, p, index0);

	if (cmp != 0) {
		XT_PUTPAGE(mp);
		jfs_error(ip->i_sb, "xtUpdate: Could not find extent");
		return -EIO;
	}

	BT_MARK_DIRTY(mp, ip);
	/*
	 * acquire tlock of the leaf page containing original entry
	 */
	if (!test_cflag(COMMIT_Nolink, ip)) {
		tlck = txLock(tid, ip, mp, tlckXTREE | tlckGROW);
		xtlck = (struct xtlock *) & tlck->lock;
	}

	xad = &p->xad[index0];
	xflag = xad->flag;
	xoff = offsetXAD(xad);
	xlen = lengthXAD(xad);
	xaddr = addressXAD(xad);

	/* nXAD must be completely contained within XAD */
	if ((xoff > nxoff) ||
	    (nxoff + nxlen > xoff + xlen)) {
		XT_PUTPAGE(mp);
		jfs_error(ip->i_sb,
			  "xtUpdate: nXAD in not completely contained within XAD");
		return -EIO;
	}

	index = index0;
	newindex = index + 1;
	nextindex = le16_to_cpu(p->header.nextindex);

#ifdef  _JFS_WIP_NOCOALESCE
	if (xoff < nxoff)
		goto updateRight;

	/*
	 * replace XAD with nXAD
	 */
      replace:			/* (nxoff == xoff) */
	if (nxlen == xlen) {
		/* replace XAD with nXAD:recorded */
		*xad = *nxad;
		xad->flag = xflag & ~XAD_NOTRECORDED;

		goto out;
	} else			/* (nxlen < xlen) */
		goto updateLeft;
#endif				/* _JFS_WIP_NOCOALESCE */

/* #ifdef _JFS_WIP_COALESCE */
	if (xoff < nxoff)
		goto coalesceRight;

	/*
	 * coalesce with left XAD
	 */
//coalesceLeft: /* (xoff == nxoff) */
	/* is XAD first entry of page ? */
	if (index == XTENTRYSTART)
		goto replace;

	/* is nXAD logically and physically contiguous with lXAD ? */
	lxad = &p->xad[index - 1];
	lxlen = lengthXAD(lxad);
	if (!(lxad->flag & XAD_NOTRECORDED) &&
	    (nxoff == offsetXAD(lxad) + lxlen) &&
	    (nxaddr == addressXAD(lxad) + lxlen) &&
	    (lxlen + nxlen < MAXXLEN)) {
		/* extend right lXAD */
		index0 = index - 1;
		XADlength(lxad, lxlen + nxlen);

		/* If we just merged two extents together, need to make sure the
		 * right extent gets logged.  If the left one is marked XAD_NEW,
		 * then we know it will be logged.  Otherwise, mark as
		 * XAD_EXTENDED
		 */
		if (!(lxad->flag & XAD_NEW))
			lxad->flag |= XAD_EXTENDED;

		if (xlen > nxlen) {
			/* truncate XAD */
			XADoffset(xad, xoff + nxlen);
			XADlength(xad, xlen - nxlen);
			XADaddress(xad, xaddr + nxlen);
			goto out;
		} else {	/* (xlen == nxlen) */

			/* remove XAD */
			if (index < nextindex - 1)
				memmove(&p->xad[index], &p->xad[index + 1],
					(nextindex - index -
					 1) << L2XTSLOTSIZE);

			p->header.nextindex =
			    cpu_to_le16(le16_to_cpu(p->header.nextindex) -
					1);

			index = index0;
			newindex = index + 1;
			nextindex = le16_to_cpu(p->header.nextindex);
			xoff = nxoff = offsetXAD(lxad);
			xlen = nxlen = lxlen + nxlen;
			xaddr = nxaddr = addressXAD(lxad);
			goto coalesceRight;
		}
	}

	/*
	 * replace XAD with nXAD
	 */
      replace:			/* (nxoff == xoff) */
	if (nxlen == xlen) {
		/* replace XAD with nXAD:recorded */
		*xad = *nxad;
		xad->flag = xflag & ~XAD_NOTRECORDED;

		goto coalesceRight;
	} else			/* (nxlen < xlen) */
		goto updateLeft;

	/*
	 * coalesce with right XAD
	 */
      coalesceRight:		/* (xoff <= nxoff) */
	/* is XAD last entry of page ? */
	if (newindex == nextindex) {
		if (xoff == nxoff)
			goto out;
		goto updateRight;
	}

	/* is nXAD logically and physically contiguous with rXAD ? */
	rxad = &p->xad[index + 1];
	rxlen = lengthXAD(rxad);
	if (!(rxad->flag & XAD_NOTRECORDED) &&
	    (nxoff + nxlen == offsetXAD(rxad)) &&
	    (nxaddr + nxlen == addressXAD(rxad)) &&
	    (rxlen + nxlen < MAXXLEN)) {
		/* extend left rXAD */
		XADoffset(rxad, nxoff);
		XADlength(rxad, rxlen + nxlen);
		XADaddress(rxad, nxaddr);

		/* If we just merged two extents together, need to make sure
		 * the left extent gets logged.  If the right one is marked
		 * XAD_NEW, then we know it will be logged.  Otherwise, mark as
		 * XAD_EXTENDED
		 */
		if (!(rxad->flag & XAD_NEW))
			rxad->flag |= XAD_EXTENDED;

		if (xlen > nxlen)
			/* truncate XAD */
			XADlength(xad, xlen - nxlen);
		else {		/* (xlen == nxlen) */

			/* remove XAD */
			memmove(&p->xad[index], &p->xad[index + 1],
				(nextindex - index - 1) << L2XTSLOTSIZE);

			p->header.nextindex =
			    cpu_to_le16(le16_to_cpu(p->header.nextindex) -
					1);
		}

		goto out;
	} else if (xoff == nxoff)
		goto out;

	if (xoff >= nxoff) {
		XT_PUTPAGE(mp);
		jfs_error(ip->i_sb, "xtUpdate: xoff >= nxoff");
		return -EIO;
	}
/* #endif _JFS_WIP_COALESCE */

	/*
	 * split XAD into (lXAD, nXAD):
	 *
	 *          |---nXAD--->
	 * --|----------XAD----------|--
	 *   |-lXAD-|
	 */
      updateRight:		/* (xoff < nxoff) */
	/* truncate old XAD as lXAD:not_recorded */
	xad = &p->xad[index];
	XADlength(xad, nxoff - xoff);

	/* insert nXAD:recorded */
	if (nextindex == le16_to_cpu(p->header.maxentry)) {

		/* xtSpliUp() unpins leaf pages */
		split.mp = mp;
		split.index = newindex;
		split.flag = xflag & ~XAD_NOTRECORDED;
		split.off = nxoff;
		split.len = nxlen;
		split.addr = nxaddr;
		split.pxdlist = NULL;
		if ((rc = xtSplitUp(tid, ip, &split, &btstack)))
			return rc;

		/* get back old page */
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;
		/*
		 * if leaf root has been split, original root has been
		 * copied to new child page, i.e., original entry now
		 * resides on the new child page;
		 */
		if (p->header.flag & BT_INTERNAL) {
			ASSERT(p->header.nextindex ==
			       cpu_to_le16(XTENTRYSTART + 1));
			xad = &p->xad[XTENTRYSTART];
			bn = addressXAD(xad);
			XT_PUTPAGE(mp);

			/* get new child page */
			XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
			if (rc)
				return rc;

			BT_MARK_DIRTY(mp, ip);
			if (!test_cflag(COMMIT_Nolink, ip)) {
				tlck = txLock(tid, ip, mp, tlckXTREE|tlckGROW);
				xtlck = (struct xtlock *) & tlck->lock;
			}
		} else {
			/* is nXAD on new page ? */
			if (newindex >
			    (le16_to_cpu(p->header.maxentry) >> 1)) {
				newindex =
				    newindex -
				    le16_to_cpu(p->header.nextindex) +
				    XTENTRYSTART;
				newpage = 1;
			}
		}
	} else {
		/* if insert into middle, shift right remaining entries */
		if (newindex < nextindex)
			memmove(&p->xad[newindex + 1], &p->xad[newindex],
				(nextindex - newindex) << L2XTSLOTSIZE);

		/* insert the entry */
		xad = &p->xad[newindex];
		*xad = *nxad;
		xad->flag = xflag & ~XAD_NOTRECORDED;

		/* advance next available entry index. */
		p->header.nextindex =
		    cpu_to_le16(le16_to_cpu(p->header.nextindex) + 1);
	}

	/*
	 * does nXAD force 3-way split ?
	 *
	 *          |---nXAD--->|
	 * --|----------XAD-------------|--
	 *   |-lXAD-|           |-rXAD -|
	 */
	if (nxoff + nxlen == xoff + xlen)
		goto out;

	/* reorient nXAD as XAD for further split XAD into (nXAD, rXAD) */
	if (newpage) {
		/* close out old page */
		if (!test_cflag(COMMIT_Nolink, ip)) {
			xtlck->lwm.offset = (xtlck->lwm.offset) ?
			    min(index0, (int)xtlck->lwm.offset) : index0;
			xtlck->lwm.length =
			    le16_to_cpu(p->header.nextindex) -
			    xtlck->lwm.offset;
		}

		bn = le64_to_cpu(p->header.next);
		XT_PUTPAGE(mp);

		/* get new right page */
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;

		BT_MARK_DIRTY(mp, ip);
		if (!test_cflag(COMMIT_Nolink, ip)) {
			tlck = txLock(tid, ip, mp, tlckXTREE | tlckGROW);
			xtlck = (struct xtlock *) & tlck->lock;
		}

		index0 = index = newindex;
	} else
		index++;

	newindex = index + 1;
	nextindex = le16_to_cpu(p->header.nextindex);
	xlen = xlen - (nxoff - xoff);
	xoff = nxoff;
	xaddr = nxaddr;

	/* recompute split pages */
	if (nextindex == le16_to_cpu(p->header.maxentry)) {
		XT_PUTPAGE(mp);

		if ((rc = xtSearch(ip, nxoff, NULL, &cmp, &btstack, XT_INSERT)))
			return rc;

		/* retrieve search result */
		XT_GETSEARCH(ip, btstack.top, bn, mp, p, index0);

		if (cmp != 0) {
			XT_PUTPAGE(mp);
			jfs_error(ip->i_sb, "xtUpdate: xtSearch failed");
			return -EIO;
		}

		if (index0 != index) {
			XT_PUTPAGE(mp);
			jfs_error(ip->i_sb,
				  "xtUpdate: unexpected value of index");
			return -EIO;
		}
	}

	/*
	 * split XAD into (nXAD, rXAD)
	 *
	 *          ---nXAD---|
	 * --|----------XAD----------|--
	 *                    |-rXAD-|
	 */
      updateLeft:		/* (nxoff == xoff) && (nxlen < xlen) */
	/* update old XAD with nXAD:recorded */
	xad = &p->xad[index];
	*xad = *nxad;
	xad->flag = xflag & ~XAD_NOTRECORDED;

	/* insert rXAD:not_recorded */
	xoff = xoff + nxlen;
	xlen = xlen - nxlen;
	xaddr = xaddr + nxlen;
	if (nextindex == le16_to_cpu(p->header.maxentry)) {
/*
printf("xtUpdate.updateLeft.split p:0x%p\n", p);
*/
		/* xtSpliUp() unpins leaf pages */
		split.mp = mp;
		split.index = newindex;
		split.flag = xflag;
		split.off = xoff;
		split.len = xlen;
		split.addr = xaddr;
		split.pxdlist = NULL;
		if ((rc = xtSplitUp(tid, ip, &split, &btstack)))
			return rc;

		/* get back old page */
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;

		/*
		 * if leaf root has been split, original root has been
		 * copied to new child page, i.e., original entry now
		 * resides on the new child page;
		 */
		if (p->header.flag & BT_INTERNAL) {
			ASSERT(p->header.nextindex ==
			       cpu_to_le16(XTENTRYSTART + 1));
			xad = &p->xad[XTENTRYSTART];
			bn = addressXAD(xad);
			XT_PUTPAGE(mp);

			/* get new child page */
			XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
			if (rc)
				return rc;

			BT_MARK_DIRTY(mp, ip);
			if (!test_cflag(COMMIT_Nolink, ip)) {
				tlck = txLock(tid, ip, mp, tlckXTREE|tlckGROW);
				xtlck = (struct xtlock *) & tlck->lock;
			}
		}
	} else {
		/* if insert into middle, shift right remaining entries */
		if (newindex < nextindex)
			memmove(&p->xad[newindex + 1], &p->xad[newindex],
				(nextindex - newindex) << L2XTSLOTSIZE);

		/* insert the entry */
		xad = &p->xad[newindex];
		XT_PUTENTRY(xad, xflag, xoff, xlen, xaddr);

		/* advance next available entry index. */
		p->header.nextindex =
		    cpu_to_le16(le16_to_cpu(p->header.nextindex) + 1);
	}

      out:
	if (!test_cflag(COMMIT_Nolink, ip)) {
		xtlck->lwm.offset = (xtlck->lwm.offset) ?
		    min(index0, (int)xtlck->lwm.offset) : index0;
		xtlck->lwm.length = le16_to_cpu(p->header.nextindex) -
		    xtlck->lwm.offset;
	}

	/* unpin the leaf page */
	XT_PUTPAGE(mp);

	return rc;
}


/*
 *	xtAppend()
 *
 * function: grow in append mode from contiguous region specified ;
 *
 * parameter:
 *	tid		- transaction id;
 *	ip		- file object;
 *	xflag		- extent flag:
 *	xoff		- extent offset;
 *	maxblocks	- max extent length;
 *	xlen		- extent length (in/out);
 *	xaddrp		- extent address pointer (in/out):
 *	flag		-
 *
 * return:
 */
int xtAppend(tid_t tid,		/* transaction id */
	     struct inode *ip, int xflag, s64 xoff, s32 maxblocks,
	     s32 * xlenp,	/* (in/out) */
	     s64 * xaddrp,	/* (in/out) */
	     int flag)
{
	int rc = 0;
	struct metapage *mp;	/* meta-page buffer */
	xtpage_t *p;		/* base B+-tree index page */
	s64 bn, xaddr;
	int index, nextindex;
	struct btstack btstack;	/* traverse stack */
	struct xtsplit split;	/* split information */
	xad_t *xad;
	int cmp;
	struct tlock *tlck;
	struct xtlock *xtlck;
	int nsplit, nblocks, xlen;
	struct pxdlist pxdlist;
	pxd_t *pxd;
	s64 next;

	xaddr = *xaddrp;
	xlen = *xlenp;
	jfs_info("xtAppend: xoff:0x%lx maxblocks:%d xlen:%d xaddr:0x%lx",
		 (ulong) xoff, maxblocks, xlen, (ulong) xaddr);

	/*
	 *	search for the entry location at which to insert:
	 *
	 * xtFastSearch() and xtSearch() both returns (leaf page
	 * pinned, index at which to insert).
	 * n.b. xtSearch() may return index of maxentry of
	 * the full page.
	 */
	if ((rc = xtSearch(ip, xoff, &next, &cmp, &btstack, XT_INSERT)))
		return rc;

	/* retrieve search result */
	XT_GETSEARCH(ip, btstack.top, bn, mp, p, index);

	if (cmp == 0) {
		rc = -EEXIST;
		goto out;
	}

	if (next)
		xlen = min(xlen, (int)(next - xoff));
//insert:
	/*
	 *	insert entry for new extent
	 */
	xflag |= XAD_NEW;

	/*
	 *	if the leaf page is full, split the page and
	 *	propagate up the router entry for the new page from split
	 *
	 * The xtSplitUp() will insert the entry and unpin the leaf page.
	 */
	nextindex = le16_to_cpu(p->header.nextindex);
	if (nextindex < le16_to_cpu(p->header.maxentry))
		goto insertLeaf;

	/*
	 * allocate new index blocks to cover index page split(s)
	 */
	nsplit = btstack.nsplit;
	split.pxdlist = &pxdlist;
	pxdlist.maxnpxd = pxdlist.npxd = 0;
	pxd = &pxdlist.pxd[0];
	nblocks = JFS_SBI(ip->i_sb)->nbperpage;
	for (; nsplit > 0; nsplit--, pxd++, xaddr += nblocks, maxblocks -= nblocks) {
		if ((rc = dbAllocBottomUp(ip, xaddr, (s64) nblocks)) == 0) {
			PXDaddress(pxd, xaddr);
			PXDlength(pxd, nblocks);

			pxdlist.maxnpxd++;

			continue;
		}

		/* undo allocation */

		goto out;
	}

	xlen = min(xlen, maxblocks);

	/*
	 * allocate data extent requested
	 */
	if ((rc = dbAllocBottomUp(ip, xaddr, (s64) xlen)))
		goto out;

	split.mp = mp;
	split.index = index;
	split.flag = xflag;
	split.off = xoff;
	split.len = xlen;
	split.addr = xaddr;
	if ((rc = xtSplitUp(tid, ip, &split, &btstack))) {
		/* undo data extent allocation */
		dbFree(ip, *xaddrp, (s64) * xlenp);

		return rc;
	}

	*xaddrp = xaddr;
	*xlenp = xlen;
	return 0;

	/*
	 *	insert the new entry into the leaf page
	 */
      insertLeaf:
	/*
	 * allocate data extent requested
	 */
	if ((rc = dbAllocBottomUp(ip, xaddr, (s64) xlen)))
		goto out;

	BT_MARK_DIRTY(mp, ip);
	/*
	 * acquire a transaction lock on the leaf page;
	 *
	 * action: xad insertion/extension;
	 */
	tlck = txLock(tid, ip, mp, tlckXTREE | tlckGROW);
	xtlck = (struct xtlock *) & tlck->lock;

	/* insert the new entry: mark the entry NEW */
	xad = &p->xad[index];
	XT_PUTENTRY(xad, xflag, xoff, xlen, xaddr);

	/* advance next available entry index */
	p->header.nextindex =
	    cpu_to_le16(le16_to_cpu(p->header.nextindex) + 1);

	xtlck->lwm.offset =
	    (xtlck->lwm.offset) ? min(index,(int) xtlck->lwm.offset) : index;
	xtlck->lwm.length = le16_to_cpu(p->header.nextindex) -
	    xtlck->lwm.offset;

	*xaddrp = xaddr;
	*xlenp = xlen;

      out:
	/* unpin the leaf page */
	XT_PUTPAGE(mp);

	return rc;
}
#ifdef _STILL_TO_PORT

/* - TBD for defragmentaion/reorganization -
 *
 *	xtDelete()
 *
 * function:
 *	delete the entry with the specified key.
 *
 *	N.B.: whole extent of the entry is assumed to be deleted.
 *
 * parameter:
 *
 * return:
 *	ENOENT: if the entry is not found.
 *
 * exception:
 */
int xtDelete(tid_t tid, struct inode *ip, s64 xoff, s32 xlen, int flag)
{
	int rc = 0;
	struct btstack btstack;
	int cmp;
	s64 bn;
	struct metapage *mp;
	xtpage_t *p;
	int index, nextindex;
	struct tlock *tlck;
	struct xtlock *xtlck;

	/*
	 * find the matching entry; xtSearch() pins the page
	 */
	if ((rc = xtSearch(ip, xoff, NULL, &cmp, &btstack, 0)))
		return rc;

	XT_GETSEARCH(ip, btstack.top, bn, mp, p, index);
	if (cmp) {
		/* unpin the leaf page */
		XT_PUTPAGE(mp);
		return -ENOENT;
	}

	/*
	 * delete the entry from the leaf page
	 */
	nextindex = le16_to_cpu(p->header.nextindex);
	p->header.nextindex =
	    cpu_to_le16(le16_to_cpu(p->header.nextindex) - 1);

	/*
	 * if the leaf page bocome empty, free the page
	 */
	if (p->header.nextindex == cpu_to_le16(XTENTRYSTART))
		return (xtDeleteUp(tid, ip, mp, p, &btstack));

	BT_MARK_DIRTY(mp, ip);
	/*
	 * acquire a transaction lock on the leaf page;
	 *
	 * action:xad deletion;
	 */
	tlck = txLock(tid, ip, mp, tlckXTREE);
	xtlck = (struct xtlock *) & tlck->lock;
	xtlck->lwm.offset =
	    (xtlck->lwm.offset) ? min(index, xtlck->lwm.offset) : index;

	/* if delete from middle, shift left/compact the remaining entries */
	if (index < nextindex - 1)
		memmove(&p->xad[index], &p->xad[index + 1],
			(nextindex - index - 1) * sizeof(xad_t));

	XT_PUTPAGE(mp);

	return 0;
}


/* - TBD for defragmentaion/reorganization -
 *
 *	xtDeleteUp()
 *
 * function:
 *	free empty pages as propagating deletion up the tree
 *
 * parameter:
 *
 * return:
 */
static int
xtDeleteUp(tid_t tid, struct inode *ip,
	   struct metapage * fmp, xtpage_t * fp, struct btstack * btstack)
{
	int rc = 0;
	struct metapage *mp;
	xtpage_t *p;
	int index, nextindex;
	s64 xaddr;
	int xlen;
	struct btframe *parent;
	struct tlock *tlck;
	struct xtlock *xtlck;

	/*
	 * keep root leaf page which has become empty
	 */
	if (fp->header.flag & BT_ROOT) {
		/* keep the root page */
		fp->header.flag &= ~BT_INTERNAL;
		fp->header.flag |= BT_LEAF;
		fp->header.nextindex = cpu_to_le16(XTENTRYSTART);

		/* XT_PUTPAGE(fmp); */

		return 0;
	}

	/*
	 * free non-root leaf page
	 */
	if ((rc = xtRelink(tid, ip, fp))) {
		XT_PUTPAGE(fmp);
		return rc;
	}

	xaddr = addressPXD(&fp->header.self);
	xlen = lengthPXD(&fp->header.self);
	/* free the page extent */
	dbFree(ip, xaddr, (s64) xlen);

	/* free the buffer page */
	discard_metapage(fmp);

	/*
	 * propagate page deletion up the index tree
	 *
	 * If the delete from the parent page makes it empty,
	 * continue all the way up the tree.
	 * stop if the root page is reached (which is never deleted) or
	 * if the entry deletion does not empty the page.
	 */
	while ((parent = BT_POP(btstack)) != NULL) {
		/* get/pin the parent page <sp> */
		XT_GETPAGE(ip, parent->bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;

		index = parent->index;

		/* delete the entry for the freed child page from parent.
		 */
		nextindex = le16_to_cpu(p->header.nextindex);

		/*
		 * the parent has the single entry being deleted:
		 * free the parent page which has become empty.
		 */
		if (nextindex == 1) {
			if (p->header.flag & BT_ROOT) {
				/* keep the root page */
				p->header.flag &= ~BT_INTERNAL;
				p->header.flag |= BT_LEAF;
				p->header.nextindex =
				    cpu_to_le16(XTENTRYSTART);

				/* XT_PUTPAGE(mp); */

				break;
			} else {
				/* free the parent page */
				if ((rc = xtRelink(tid, ip, p)))
					return rc;

				xaddr = addressPXD(&p->header.self);
				/* free the page extent */
				dbFree(ip, xaddr,
				       (s64) JFS_SBI(ip->i_sb)->nbperpage);

				/* unpin/free the buffer page */
				discard_metapage(mp);

				/* propagate up */
				continue;
			}
		}
		/*
		 * the parent has other entries remaining:
		 * delete the router entry from the parent page.
		 */
		else {
			BT_MARK_DIRTY(mp, ip);
			/*
			 * acquire a transaction lock on the leaf page;
			 *
			 * action:xad deletion;
			 */
			tlck = txLock(tid, ip, mp, tlckXTREE);
			xtlck = (struct xtlock *) & tlck->lock;
			xtlck->lwm.offset =
			    (xtlck->lwm.offset) ? min(index,
						      xtlck->lwm.
						      offset) : index;

			/* if delete from middle,
			 * shift left/compact the remaining entries in the page
			 */
			if (index < nextindex - 1)
				memmove(&p->xad[index], &p->xad[index + 1],
					(nextindex - index -
					 1) << L2XTSLOTSIZE);

			p->header.nextindex =
			    cpu_to_le16(le16_to_cpu(p->header.nextindex) -
					1);
			jfs_info("xtDeleteUp(entry): 0x%lx[%d]",
				 (ulong) parent->bn, index);
		}

		/* unpin the parent page */
		XT_PUTPAGE(mp);

		/* exit propagation up */
		break;
	}

	return 0;
}


/*
 * NAME:	xtRelocate()
 *
 * FUNCTION:	relocate xtpage or data extent of regular file;
 *		This function is mainly used by defragfs utility.
 *
 * NOTE:	This routine does not have the logic to handle
 *		uncommitted allocated extent. The caller should call
 *		txCommit() to commit all the allocation before call
 *		this routine.
 */
int
xtRelocate(tid_t tid, struct inode * ip, xad_t * oxad,	/* old XAD */
	   s64 nxaddr,		/* new xaddr */
	   int xtype)
{				/* extent type: XTPAGE or DATAEXT */
	int rc = 0;
	struct tblock *tblk;
	struct tlock *tlck;
	struct xtlock *xtlck;
	struct metapage *mp, *pmp, *lmp, *rmp;	/* meta-page buffer */
	xtpage_t *p, *pp, *rp, *lp;	/* base B+-tree index page */
	xad_t *xad;
	pxd_t *pxd;
	s64 xoff, xsize;
	int xlen;
	s64 oxaddr, sxaddr, dxaddr, nextbn, prevbn;
	cbuf_t *cp;
	s64 offset, nbytes, nbrd, pno;
	int nb, npages, nblks;
	s64 bn;
	int cmp;
	int index;
	struct pxd_lock *pxdlock;
	struct btstack btstack;	/* traverse stack */

	xtype = xtype & EXTENT_TYPE;

	xoff = offsetXAD(oxad);
	oxaddr = addressXAD(oxad);
	xlen = lengthXAD(oxad);

	/* validate extent offset */
	offset = xoff << JFS_SBI(ip->i_sb)->l2bsize;
	if (offset >= ip->i_size)
		return -ESTALE;	/* stale extent */

	jfs_info("xtRelocate: xtype:%d xoff:0x%lx xlen:0x%x xaddr:0x%lx:0x%lx",
		 xtype, (ulong) xoff, xlen, (ulong) oxaddr, (ulong) nxaddr);

	/*
	 *	1. get and validate the parent xtpage/xad entry
	 *	covering the source extent to be relocated;
	 */
	if (xtype == DATAEXT) {
		/* search in leaf entry */
		rc = xtSearch(ip, xoff, NULL, &cmp, &btstack, 0);
		if (rc)
			return rc;

		/* retrieve search result */
		XT_GETSEARCH(ip, btstack.top, bn, pmp, pp, index);

		if (cmp) {
			XT_PUTPAGE(pmp);
			return -ESTALE;
		}

		/* validate for exact match with a single entry */
		xad = &pp->xad[index];
		if (addressXAD(xad) != oxaddr || lengthXAD(xad) != xlen) {
			XT_PUTPAGE(pmp);
			return -ESTALE;
		}
	} else {		/* (xtype == XTPAGE) */

		/* search in internal entry */
		rc = xtSearchNode(ip, oxad, &cmp, &btstack, 0);
		if (rc)
			return rc;

		/* retrieve search result */
		XT_GETSEARCH(ip, btstack.top, bn, pmp, pp, index);

		if (cmp) {
			XT_PUTPAGE(pmp);
			return -ESTALE;
		}

		/* xtSearchNode() validated for exact match with a single entry
		 */
		xad = &pp->xad[index];
	}
	jfs_info("xtRelocate: parent xad entry validated.");

	/*
	 *	2. relocate the extent
	 */
	if (xtype == DATAEXT) {
		/* if the extent is allocated-but-not-recorded
		 * there is no real data to be moved in this extent,
		 */
		if (xad->flag & XAD_NOTRECORDED)
			goto out;
		else
			/* release xtpage for cmRead()/xtLookup() */
			XT_PUTPAGE(pmp);

		/*
		 *	cmRelocate()
		 *
		 * copy target data pages to be relocated;
		 *
		 * data extent must start at page boundary and
		 * multiple of page size (except the last data extent);
		 * read in each page of the source data extent into cbuf,
		 * update the cbuf extent descriptor of the page to be
		 * homeward bound to new dst data extent
		 * copy the data from the old extent to new extent.
		 * copy is essential for compressed files to avoid problems
		 * that can arise if there was a change in compression
		 * algorithms.
		 * it is a good strategy because it may disrupt cache
		 * policy to keep the pages in memory afterwards.
		 */
		offset = xoff << JFS_SBI(ip->i_sb)->l2bsize;
		assert((offset & CM_OFFSET) == 0);
		nbytes = xlen << JFS_SBI(ip->i_sb)->l2bsize;
		pno = offset >> CM_L2BSIZE;
		npages = (nbytes + (CM_BSIZE - 1)) >> CM_L2BSIZE;
/*
		npages = ((offset + nbytes - 1) >> CM_L2BSIZE) -
			  (offset >> CM_L2BSIZE) + 1;
*/
		sxaddr = oxaddr;
		dxaddr = nxaddr;

		/* process the request one cache buffer at a time */
		for (nbrd = 0; nbrd < nbytes; nbrd += nb,
		     offset += nb, pno++, npages--) {
			/* compute page size */
			nb = min(nbytes - nbrd, CM_BSIZE);

			/* get the cache buffer of the page */
			if (rc = cmRead(ip, offset, npages, &cp))
				break;

			assert(addressPXD(&cp->cm_pxd) == sxaddr);
			assert(!cp->cm_modified);

			/* bind buffer with the new extent address */
			nblks = nb >> JFS_IP(ip->i_sb)->l2bsize;
			cmSetXD(ip, cp, pno, dxaddr, nblks);

			/* release the cbuf, mark it as modified */
			cmPut(cp, true);

			dxaddr += nblks;
			sxaddr += nblks;
		}

		/* get back parent page */
		if ((rc = xtSearch(ip, xoff, NULL, &cmp, &btstack, 0)))
			return rc;

		XT_GETSEARCH(ip, btstack.top, bn, pmp, pp, index);
		jfs_info("xtRelocate: target data extent relocated.");
	} else {		/* (xtype == XTPAGE) */

		/*
		 * read in the target xtpage from the source extent;
		 */
		XT_GETPAGE(ip, oxaddr, mp, PSIZE, p, rc);
		if (rc) {
			XT_PUTPAGE(pmp);
			return rc;
		}

		/*
		 * read in sibling pages if any to update sibling pointers;
		 */
		rmp = NULL;
		if (p->header.next) {
			nextbn = le64_to_cpu(p->header.next);
			XT_GETPAGE(ip, nextbn, rmp, PSIZE, rp, rc);
			if (rc) {
				XT_PUTPAGE(pmp);
				XT_PUTPAGE(mp);
				return (rc);
			}
		}

		lmp = NULL;
		if (p->header.prev) {
			prevbn = le64_to_cpu(p->header.prev);
			XT_GETPAGE(ip, prevbn, lmp, PSIZE, lp, rc);
			if (rc) {
				XT_PUTPAGE(pmp);
				XT_PUTPAGE(mp);
				if (rmp)
					XT_PUTPAGE(rmp);
				return (rc);
			}
		}

		/* at this point, all xtpages to be updated are in memory */

		/*
		 * update sibling pointers of sibling xtpages if any;
		 */
		if (lmp) {
			BT_MARK_DIRTY(lmp, ip);
			tlck = txLock(tid, ip, lmp, tlckXTREE | tlckRELINK);
			lp->header.next = cpu_to_le64(nxaddr);
			XT_PUTPAGE(lmp);
		}

		if (rmp) {
			BT_MARK_DIRTY(rmp, ip);
			tlck = txLock(tid, ip, rmp, tlckXTREE | tlckRELINK);
			rp->header.prev = cpu_to_le64(nxaddr);
			XT_PUTPAGE(rmp);
		}

		/*
		 * update the target xtpage to be relocated
		 *
		 * update the self address of the target page
		 * and write to destination extent;
		 * redo image covers the whole xtpage since it is new page
		 * to the destination extent;
		 * update of bmap for the free of source extent
		 * of the target xtpage itself:
		 * update of bmap for the allocation of destination extent
		 * of the target xtpage itself:
		 * update of bmap for the extents covered by xad entries in
		 * the target xtpage is not necessary since they are not
		 * updated;
		 * if not committed before this relocation,
		 * target page may contain XAD_NEW entries which must
		 * be scanned for bmap update (logredo() always
		 * scan xtpage REDOPAGE image for bmap update);
		 * if committed before this relocation (tlckRELOCATE),
		 * scan may be skipped by commit() and logredo();
		 */
		BT_MARK_DIRTY(mp, ip);
		/* tlckNEW init xtlck->lwm.offset = XTENTRYSTART; */
		tlck = txLock(tid, ip, mp, tlckXTREE | tlckNEW);
		xtlck = (struct xtlock *) & tlck->lock;

		/* update the self address in the xtpage header */
		pxd = &p->header.self;
		PXDaddress(pxd, nxaddr);

		/* linelock for the after image of the whole page */
		xtlck->lwm.length =
		    le16_to_cpu(p->header.nextindex) - xtlck->lwm.offset;

		/* update the buffer extent descriptor of target xtpage */
		xsize = xlen << JFS_SBI(ip->i_sb)->l2bsize;
		bmSetXD(mp, nxaddr, xsize);

		/* unpin the target page to new homeward bound */
		XT_PUTPAGE(mp);
		jfs_info("xtRelocate: target xtpage relocated.");
	}

	/*
	 *	3. acquire maplock for the source extent to be freed;
	 *
	 * acquire a maplock saving the src relocated extent address;
	 * to free of the extent at commit time;
	 */
      out:
	/* if DATAEXT relocation, write a LOG_UPDATEMAP record for
	 * free PXD of the source data extent (logredo() will update
	 * bmap for free of source data extent), and update bmap for
	 * free of the source data extent;
	 */
	if (xtype == DATAEXT)
		tlck = txMaplock(tid, ip, tlckMAP);
	/* if XTPAGE relocation, write a LOG_NOREDOPAGE record
	 * for the source xtpage (logredo() will init NoRedoPage
	 * filter and will also update bmap for free of the source
	 * xtpage), and update bmap for free of the source xtpage;
	 * N.B. We use tlckMAP instead of tlkcXTREE because there
	 *      is no buffer associated with this lock since the buffer
	 *      has been redirected to the target location.
	 */
	else			/* (xtype == XTPAGE) */
		tlck = txMaplock(tid, ip, tlckMAP | tlckRELOCATE);

	pxdlock = (struct pxd_lock *) & tlck->lock;
	pxdlock->flag = mlckFREEPXD;
	PXDaddress(&pxdlock->pxd, oxaddr);
	PXDlength(&pxdlock->pxd, xlen);
	pxdlock->index = 1;

	/*
	 *	4. update the parent xad entry for relocation;
	 *
	 * acquire tlck for the parent entry with XAD_NEW as entry
	 * update which will write LOG_REDOPAGE and update bmap for
	 * allocation of XAD_NEW destination extent;
	 */
	jfs_info("xtRelocate: update parent xad entry.");
	BT_MARK_DIRTY(pmp, ip);
	tlck = txLock(tid, ip, pmp, tlckXTREE | tlckGROW);
	xtlck = (struct xtlock *) & tlck->lock;

	/* update the XAD with the new destination extent; */
	xad = &pp->xad[index];
	xad->flag |= XAD_NEW;
	XADaddress(xad, nxaddr);

	xtlck->lwm.offset = min(index, xtlck->lwm.offset);
	xtlck->lwm.length = le16_to_cpu(pp->header.nextindex) -
	    xtlck->lwm.offset;

	/* unpin the parent xtpage */
	XT_PUTPAGE(pmp);

	return rc;
}


/*
 *	xtSearchNode()
 *
 * function:	search for the internal xad entry covering specified extent.
 *		This function is mainly used by defragfs utility.
 *
 * parameters:
 *	ip	- file object;
 *	xad	- extent to find;
 *	cmpp	- comparison result:
 *	btstack - traverse stack;
 *	flag	- search process flag;
 *
 * returns:
 *	btstack contains (bn, index) of search path traversed to the entry.
 *	*cmpp is set to result of comparison with the entry returned.
 *	the page containing the entry is pinned at exit.
 */
static int xtSearchNode(struct inode *ip, xad_t * xad,	/* required XAD entry */
			int *cmpp, struct btstack * btstack, int flag)
{
	int rc = 0;
	s64 xoff, xaddr;
	int xlen;
	int cmp = 1;		/* init for empty page */
	s64 bn;			/* block number */
	struct metapage *mp;	/* meta-page buffer */
	xtpage_t *p;		/* page */
	int base, index, lim;
	struct btframe *btsp;
	s64 t64;

	BT_CLR(btstack);

	xoff = offsetXAD(xad);
	xlen = lengthXAD(xad);
	xaddr = addressXAD(xad);

	/*
	 *	search down tree from root:
	 *
	 * between two consecutive entries of <Ki, Pi> and <Kj, Pj> of
	 * internal page, child page Pi contains entry with k, Ki <= K < Kj.
	 *
	 * if entry with search key K is not found
	 * internal page search find the entry with largest key Ki
	 * less than K which point to the child page to search;
	 * leaf page search find the entry with smallest key Kj
	 * greater than K so that the returned index is the position of
	 * the entry to be shifted right for insertion of new entry.
	 * for empty tree, search key is greater than any key of the tree.
	 *
	 * by convention, root bn = 0.
	 */
	for (bn = 0;;) {
		/* get/pin the page to search */
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;
		if (p->header.flag & BT_LEAF) {
			XT_PUTPAGE(mp);
			return -ESTALE;
		}

		lim = le16_to_cpu(p->header.nextindex) - XTENTRYSTART;

		/*
		 * binary search with search key K on the current page
		 */
		for (base = XTENTRYSTART; lim; lim >>= 1) {
			index = base + (lim >> 1);

			XT_CMP(cmp, xoff, &p->xad[index], t64);
			if (cmp == 0) {
				/*
				 *	search hit
				 *
				 * verify for exact match;
				 */
				if (xaddr == addressXAD(&p->xad[index]) &&
				    xoff == offsetXAD(&p->xad[index])) {
					*cmpp = cmp;

					/* save search result */
					btsp = btstack->top;
					btsp->bn = bn;
					btsp->index = index;
					btsp->mp = mp;

					return 0;
				}

				/* descend/search its child page */
				goto next;
			}

			if (cmp > 0) {
				base = index + 1;
				--lim;
			}
		}

		/*
		 *	search miss - non-leaf page:
		 *
		 * base is the smallest index with key (Kj) greater than
		 * search key (K) and may be zero or maxentry index.
		 * if base is non-zero, decrement base by one to get the parent
		 * entry of the child page to search.
		 */
		index = base ? base - 1 : base;

		/*
		 * go down to child page
		 */
	      next:
		/* get the child page block number */
		bn = addressXAD(&p->xad[index]);

		/* unpin the parent page */
		XT_PUTPAGE(mp);
	}
}


/*
 *	xtRelink()
 *
 * function:
 *	link around a freed page.
 *
 * Parameter:
 *	int		tid,
 *	struct inode	*ip,
 *	xtpage_t	*p)
 *
 * returns:
 */
static int xtRelink(tid_t tid, struct inode *ip, xtpage_t * p)
{
	int rc = 0;
	struct metapage *mp;
	s64 nextbn, prevbn;
	struct tlock *tlck;

	nextbn = le64_to_cpu(p->header.next);
	prevbn = le64_to_cpu(p->header.prev);

	/* update prev pointer of the next page */
	if (nextbn != 0) {
		XT_GETPAGE(ip, nextbn, mp, PSIZE, p, rc);
		if (rc)
			return rc;

		/*
		 * acquire a transaction lock on the page;
		 *
		 * action: update prev pointer;
		 */
		BT_MARK_DIRTY(mp, ip);
		tlck = txLock(tid, ip, mp, tlckXTREE | tlckRELINK);

		/* the page may already have been tlock'd */

		p->header.prev = cpu_to_le64(prevbn);

		XT_PUTPAGE(mp);
	}

	/* update next pointer of the previous page */
	if (prevbn != 0) {
		XT_GETPAGE(ip, prevbn, mp, PSIZE, p, rc);
		if (rc)
			return rc;

		/*
		 * acquire a transaction lock on the page;
		 *
		 * action: update next pointer;
		 */
		BT_MARK_DIRTY(mp, ip);
		tlck = txLock(tid, ip, mp, tlckXTREE | tlckRELINK);

		/* the page may already have been tlock'd */

		p->header.next = le64_to_cpu(nextbn);

		XT_PUTPAGE(mp);
	}

	return 0;
}
#endif				/*  _STILL_TO_PORT */


/*
 *	xtInitRoot()
 *
 * initialize file root (inline in inode)
 */
void xtInitRoot(tid_t tid, struct inode *ip)
{
	xtpage_t *p;

	/*
	 * acquire a transaction lock on the root
	 *
	 * action:
	 */
	txLock(tid, ip, (struct metapage *) &JFS_IP(ip)->bxflag,
		      tlckXTREE | tlckNEW);
	p = &JFS_IP(ip)->i_xtroot;

	p->header.flag = DXD_INDEX | BT_ROOT | BT_LEAF;
	p->header.nextindex = cpu_to_le16(XTENTRYSTART);

	if (S_ISDIR(ip->i_mode))
		p->header.maxentry = cpu_to_le16(XTROOTINITSLOT_DIR);
	else {
		p->header.maxentry = cpu_to_le16(XTROOTINITSLOT);
		ip->i_size = 0;
	}


	return;
}


/*
 * We can run into a deadlock truncating a file with a large number of
 * xtree pages (large fragmented file).  A robust fix would entail a
 * reservation system where we would reserve a number of metadata pages
 * and tlocks which we would be guaranteed without a deadlock.  Without
 * this, a partial fix is to limit number of metadata pages we will lock
 * in a single transaction.  Currently we will truncate the file so that
 * no more than 50 leaf pages will be locked.  The caller of xtTruncate
 * will be responsible for ensuring that the current transaction gets
 * committed, and that subsequent transactions are created to truncate
 * the file further if needed.
 */
#define MAX_TRUNCATE_LEAVES 50

/*
 *	xtTruncate()
 *
 * function:
 *	traverse for truncation logging backward bottom up;
 *	terminate at the last extent entry at the current subtree
 *	root page covering new down size.
 *	truncation may occur within the last extent entry.
 *
 * parameter:
 *	int		tid,
 *	struct inode	*ip,
 *	s64		newsize,
 *	int		type)	{PWMAP, PMAP, WMAP; DELETE, TRUNCATE}
 *
 * return:
 *
 * note:
 *	PWMAP:
 *	 1. truncate (non-COMMIT_NOLINK file)
 *	    by jfs_truncate() or jfs_open(O_TRUNC):
 *	    xtree is updated;
 *	 2. truncate index table of directory when last entry removed
 *	map update via tlock at commit time;
 *	PMAP:
 *	 Call xtTruncate_pmap instead
 *	WMAP:
 *	 1. remove (free zero link count) on last reference release
 *	    (pmap has been freed at commit zero link count);
 *	 2. truncate (COMMIT_NOLINK file, i.e., tmp file):
 *	    xtree is updated;
 *	 map update directly at truncation time;
 *
 *	if (DELETE)
 *		no LOG_NOREDOPAGE is required (NOREDOFILE is sufficient);
 *	else if (TRUNCATE)
 *		must write LOG_NOREDOPAGE for deleted index page;
 *
 * pages may already have been tlocked by anonymous transactions
 * during file growth (i.e., write) before truncation;
 *
 * except last truncated entry, deleted entries remains as is
 * in the page (nextindex is updated) for other use
 * (e.g., log/update allocation map): this avoid copying the page
 * info but delay free of pages;
 *
 */
s64 xtTruncate(tid_t tid, struct inode *ip, s64 newsize, int flag)
{
	int rc = 0;
	s64 teof;
	struct metapage *mp;
	xtpage_t *p;
	s64 bn;
	int index, nextindex;
	xad_t *xad;
	s64 xoff, xaddr;
	int xlen, len, freexlen;
	struct btstack btstack;
	struct btframe *parent;
	struct tblock *tblk = NULL;
	struct tlock *tlck = NULL;
	struct xtlock *xtlck = NULL;
	struct xdlistlock xadlock;	/* maplock for COMMIT_WMAP */
	struct pxd_lock *pxdlock;		/* maplock for COMMIT_WMAP */
	s64 nfreed;
	int freed, log;
	int locked_leaves = 0;

	/* save object truncation type */
	if (tid) {
		tblk = tid_to_tblock(tid);
		tblk->xflag |= flag;
	}

	nfreed = 0;

	flag &= COMMIT_MAP;
	assert(flag != COMMIT_PMAP);

	if (flag == COMMIT_PWMAP)
		log = 1;
	else {
		log = 0;
		xadlock.flag = mlckFREEXADLIST;
		xadlock.index = 1;
	}

	/*
	 * if the newsize is not an integral number of pages,
	 * the file between newsize and next page boundary will
	 * be cleared.
	 * if truncating into a file hole, it will cause
	 * a full block to be allocated for the logical block.
	 */

	/*
	 * release page blocks of truncated region <teof, eof>
	 *
	 * free the data blocks from the leaf index blocks.
	 * delete the parent index entries corresponding to
	 * the freed child data/index blocks.
	 * free the index blocks themselves which aren't needed
	 * in new sized file.
	 *
	 * index blocks are updated only if the blocks are to be
	 * retained in the new sized file.
	 * if type is PMAP, the data and index pages are NOT
	 * freed, and the data and index blocks are NOT freed
	 * from working map.
	 * (this will allow continued access of data/index of
	 * temporary file (zerolink count file truncated to zero-length)).
	 */
	teof = (newsize + (JFS_SBI(ip->i_sb)->bsize - 1)) >>
	    JFS_SBI(ip->i_sb)->l2bsize;

	/* clear stack */
	BT_CLR(&btstack);

	/*
	 * start with root
	 *
	 * root resides in the inode
	 */
	bn = 0;

	/*
	 * first access of each page:
	 */
      getPage:
	XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
	if (rc)
		return rc;

	/* process entries backward from last index */
	index = le16_to_cpu(p->header.nextindex) - 1;


	/* Since this is the rightmost page at this level, and we may have
	 * already freed a page that was formerly to the right, let's make
	 * sure that the next pointer is zero.
	 */
	if (p->header.next) {
		if (log)
			/*
			 * Make sure this change to the header is logged.
			 * If we really truncate this leaf, the flag
			 * will be changed to tlckTRUNCATE
			 */
			tlck = txLock(tid, ip, mp, tlckXTREE|tlckGROW);
		BT_MARK_DIRTY(mp, ip);
		p->header.next = 0;
	}

	if (p->header.flag & BT_INTERNAL)
		goto getChild;

	/*
	 *	leaf page
	 */
	freed = 0;

	/* does region covered by leaf page precede Teof ? */
	xad = &p->xad[index];
	xoff = offsetXAD(xad);
	xlen = lengthXAD(xad);
	if (teof >= xoff + xlen) {
		XT_PUTPAGE(mp);
		goto getParent;
	}

	/* (re)acquire tlock of the leaf page */
	if (log) {
		if (++locked_leaves > MAX_TRUNCATE_LEAVES) {
			/*
			 * We need to limit the size of the transaction
			 * to avoid exhausting pagecache & tlocks
			 */
			XT_PUTPAGE(mp);
			newsize = (xoff + xlen) << JFS_SBI(ip->i_sb)->l2bsize;
			goto getParent;
		}
		tlck = txLock(tid, ip, mp, tlckXTREE);
		tlck->type = tlckXTREE | tlckTRUNCATE;
		xtlck = (struct xtlock *) & tlck->lock;
		xtlck->hwm.offset = le16_to_cpu(p->header.nextindex) - 1;
	}
	BT_MARK_DIRTY(mp, ip);

	/*
	 * scan backward leaf page entries
	 */
	for (; index >= XTENTRYSTART; index--) {
		xad = &p->xad[index];
		xoff = offsetXAD(xad);
		xlen = lengthXAD(xad);
		xaddr = addressXAD(xad);

		/*
		 * The "data" for a directory is indexed by the block
		 * device's address space.  This metadata must be invalidated
		 * here
		 */
		if (S_ISDIR(ip->i_mode) && (teof == 0))
			invalidate_xad_metapages(ip, *xad);
		/*
		 * entry beyond eof: continue scan of current page
		 *          xad
		 * ---|---=======------->
		 *   eof
		 */
		if (teof < xoff) {
			nfreed += xlen;
			continue;
		}

		/*
		 * (xoff <= teof): last entry to be deleted from page;
		 * If other entries remain in page: keep and update the page.
		 */

		/*
		 * eof == entry_start: delete the entry
		 *           xad
		 * -------|=======------->
		 *       eof
		 *
		 */
		if (teof == xoff) {
			nfreed += xlen;

			if (index == XTENTRYSTART)
				break;

			nextindex = index;
		}
		/*
		 * eof within the entry: truncate the entry.
		 *          xad
		 * -------===|===------->
		 *          eof
		 */
		else if (teof < xoff + xlen) {
			/* update truncated entry */
			len = teof - xoff;
			freexlen = xlen - len;
			XADlength(xad, len);

			/* save pxd of truncated extent in tlck */
			xaddr += len;
			if (log) {	/* COMMIT_PWMAP */
				xtlck->lwm.offset = (xtlck->lwm.offset) ?
				    min(index, (int)xtlck->lwm.offset) : index;
				xtlck->lwm.length = index + 1 -
				    xtlck->lwm.offset;
				xtlck->twm.offset = index;
				pxdlock = (struct pxd_lock *) & xtlck->pxdlock;
				pxdlock->flag = mlckFREEPXD;
				PXDaddress(&pxdlock->pxd, xaddr);
				PXDlength(&pxdlock->pxd, freexlen);
			}
			/* free truncated extent */
			else {	/* COMMIT_WMAP */

				pxdlock = (struct pxd_lock *) & xadlock;
				pxdlock->flag = mlckFREEPXD;
				PXDaddress(&pxdlock->pxd, xaddr);
				PXDlength(&pxdlock->pxd, freexlen);
				txFreeMap(ip, pxdlock, NULL, COMMIT_WMAP);

				/* reset map lock */
				xadlock.flag = mlckFREEXADLIST;
			}

			/* current entry is new last entry; */
			nextindex = index + 1;

			nfreed += freexlen;
		}
		/*
		 * eof beyond the entry:
		 *          xad
		 * -------=======---|--->
		 *                 eof
		 */
		else {		/* (xoff + xlen < teof) */

			nextindex = index + 1;
		}

		if (nextindex < le16_to_cpu(p->header.nextindex)) {
			if (!log) {	/* COMMIT_WAMP */
				xadlock.xdlist = &p->xad[nextindex];
				xadlock.count =
				    le16_to_cpu(p->header.nextindex) -
				    nextindex;
				txFreeMap(ip, (struct maplock *) & xadlock,
					  NULL, COMMIT_WMAP);
			}
			p->header.nextindex = cpu_to_le16(nextindex);
		}

		XT_PUTPAGE(mp);

		/* assert(freed == 0); */
		goto getParent;
	}			/* end scan of leaf page entries */

	freed = 1;

	/*
	 * leaf page become empty: free the page if type != PMAP
	 */
	if (log) {		/* COMMIT_PWMAP */
		/* txCommit() with tlckFREE:
		 * free data extents covered by leaf [XTENTRYSTART:hwm);
		 * invalidate leaf if COMMIT_PWMAP;
		 * if (TRUNCATE), will write LOG_NOREDOPAGE;
		 */
		tlck->type = tlckXTREE | tlckFREE;
	} else {		/* COMMIT_WAMP */

		/* free data extents covered by leaf */
		xadlock.xdlist = &p->xad[XTENTRYSTART];
		xadlock.count =
		    le16_to_cpu(p->header.nextindex) - XTENTRYSTART;
		txFreeMap(ip, (struct maplock *) & xadlock, NULL, COMMIT_WMAP);
	}

	if (p->header.flag & BT_ROOT) {
		p->header.flag &= ~BT_INTERNAL;
		p->header.flag |= BT_LEAF;
		p->header.nextindex = cpu_to_le16(XTENTRYSTART);

		XT_PUTPAGE(mp);	/* debug */
		goto out;
	} else {
		if (log) {	/* COMMIT_PWMAP */
			/* page will be invalidated at tx completion
			 */
			XT_PUTPAGE(mp);
		} else {	/* COMMIT_WMAP */

			if (mp->lid)
				lid_to_tlock(mp->lid)->flag |= tlckFREELOCK;

			/* invalidate empty leaf page */
			discard_metapage(mp);
		}
	}

	/*
	 * the leaf page become empty: delete the parent entry
	 * for the leaf page if the parent page is to be kept
	 * in the new sized file.
	 */

	/*
	 * go back up to the parent page
	 */
      getParent:
	/* pop/restore parent entry for the current child page */
	if ((parent = BT_POP(&btstack)) == NULL)
		/* current page must have been root */
		goto out;

	/* get back the parent page */
	bn = parent->bn;
	XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
	if (rc)
		return rc;

	index = parent->index;

	/*
	 * child page was not empty:
	 */
	if (freed == 0) {
		/* has any entry deleted from parent ? */
		if (index < le16_to_cpu(p->header.nextindex) - 1) {
			/* (re)acquire tlock on the parent page */
			if (log) {	/* COMMIT_PWMAP */
				/* txCommit() with tlckTRUNCATE:
				 * free child extents covered by parent [);
				 */
				tlck = txLock(tid, ip, mp, tlckXTREE);
				xtlck = (struct xtlock *) & tlck->lock;
				if (!(tlck->type & tlckTRUNCATE)) {
					xtlck->hwm.offset =
					    le16_to_cpu(p->header.
							nextindex) - 1;
					tlck->type =
					    tlckXTREE | tlckTRUNCATE;
				}
			} else {	/* COMMIT_WMAP */

				/* free child extents covered by parent */
				xadlock.xdlist = &p->xad[index + 1];
				xadlock.count =
				    le16_to_cpu(p->header.nextindex) -
				    index - 1;
				txFreeMap(ip, (struct maplock *) & xadlock,
					  NULL, COMMIT_WMAP);
			}
			BT_MARK_DIRTY(mp, ip);

			p->header.nextindex = cpu_to_le16(index + 1);
		}
		XT_PUTPAGE(mp);
		goto getParent;
	}

	/*
	 * child page was empty:
	 */
	nfreed += lengthXAD(&p->xad[index]);

	/*
	 * During working map update, child page's tlock must be handled
	 * before parent's.  This is because the parent's tlock will cause
	 * the child's disk space to be marked available in the wmap, so
	 * it's important that the child page be released by that time.
	 *
	 * ToDo:  tlocks should be on doubly-linked list, so we can
	 * quickly remove it and add it to the end.
	 */

	/*
	 * Move parent page's tlock to the end of the tid's tlock list
	 */
	if (log && mp->lid && (tblk->last != mp->lid) &&
	    lid_to_tlock(mp->lid)->tid) {
		lid_t lid = mp->lid;
		struct tlock *prev;

		tlck = lid_to_tlock(lid);

		if (tblk->next == lid)
			tblk->next = tlck->next;
		else {
			for (prev = lid_to_tlock(tblk->next);
			     prev->next != lid;
			     prev = lid_to_tlock(prev->next)) {
				assert(prev->next);
			}
			prev->next = tlck->next;
		}
		lid_to_tlock(tblk->last)->next = lid;
		tlck->next = 0;
		tblk->last = lid;
	}

	/*
	 * parent page become empty: free the page
	 */
	if (index == XTENTRYSTART) {
		if (log) {	/* COMMIT_PWMAP */
			/* txCommit() with tlckFREE:
			 * free child extents covered by parent;
			 * invalidate parent if COMMIT_PWMAP;
			 */
			tlck = txLock(tid, ip, mp, tlckXTREE);
			xtlck = (struct xtlock *) & tlck->lock;
			xtlck->hwm.offset =
			    le16_to_cpu(p->header.nextindex) - 1;
			tlck->type = tlckXTREE | tlckFREE;
		} else {	/* COMMIT_WMAP */

			/* free child extents covered by parent */
			xadlock.xdlist = &p->xad[XTENTRYSTART];
			xadlock.count =
			    le16_to_cpu(p->header.nextindex) -
			    XTENTRYSTART;
			txFreeMap(ip, (struct maplock *) & xadlock, NULL,
				  COMMIT_WMAP);
		}
		BT_MARK_DIRTY(mp, ip);

		if (p->header.flag & BT_ROOT) {
			p->header.flag &= ~BT_INTERNAL;
			p->header.flag |= BT_LEAF;
			p->header.nextindex = cpu_to_le16(XTENTRYSTART);
			if (le16_to_cpu(p->header.maxentry) == XTROOTMAXSLOT) {
				/*
				 * Shrink root down to allow inline
				 * EA (otherwise fsck complains)
				 */
				p->header.maxentry =
				    cpu_to_le16(XTROOTINITSLOT);
				JFS_IP(ip)->mode2 |= INLINEEA;
			}

			XT_PUTPAGE(mp);	/* debug */
			goto out;
		} else {
			if (log) {	/* COMMIT_PWMAP */
				/* page will be invalidated at tx completion
				 */
				XT_PUTPAGE(mp);
			} else {	/* COMMIT_WMAP */

				if (mp->lid)
					lid_to_tlock(mp->lid)->flag |=
						tlckFREELOCK;

				/* invalidate parent page */
				discard_metapage(mp);
			}

			/* parent has become empty and freed:
			 * go back up to its parent page
			 */
			/* freed = 1; */
			goto getParent;
		}
	}
	/*
	 * parent page still has entries for front region;
	 */
	else {
		/* try truncate region covered by preceding entry
		 * (process backward)
		 */
		index--;

		/* go back down to the child page corresponding
		 * to the entry
		 */
		goto getChild;
	}

	/*
	 *	internal page: go down to child page of current entry
	 */
      getChild:
	/* save current parent entry for the child page */
	if (BT_STACK_FULL(&btstack)) {
		jfs_error(ip->i_sb, "stack overrun in xtTruncate!");
		XT_PUTPAGE(mp);
		return -EIO;
	}
	BT_PUSH(&btstack, bn, index);

	/* get child page */
	xad = &p->xad[index];
	bn = addressXAD(xad);

	/*
	 * first access of each internal entry:
	 */
	/* release parent page */
	XT_PUTPAGE(mp);

	/* process the child page */
	goto getPage;

      out:
	/*
	 * update file resource stat
	 */
	/* set size
	 */
	if (S_ISDIR(ip->i_mode) && !newsize)
		ip->i_size = 1;	/* fsck hates zero-length directories */
	else
		ip->i_size = newsize;

	/* update quota allocation to reflect freed blocks */
	DQUOT_FREE_BLOCK(ip, nfreed);

	/*
	 * free tlock of invalidated pages
	 */
	if (flag == COMMIT_WMAP)
		txFreelock(ip);

	return newsize;
}


/*
 *	xtTruncate_pmap()
 *
 * function:
 *	Perform truncate to zero length for deleted file, leaving the
 *	the xtree and working map untouched.  This allows the file to
 *	be accessed via open file handles, while the delete of the file
 *	is committed to disk.
 *
 * parameter:
 *	tid_t		tid,
 *	struct inode	*ip,
 *	s64		committed_size)
 *
 * return: new committed size
 *
 * note:
 *
 *	To avoid deadlock by holding too many transaction locks, the
 *	truncation may be broken up into multiple transactions.
 *	The committed_size keeps track of part of the file has been
 *	freed from the pmaps.
 */
s64 xtTruncate_pmap(tid_t tid, struct inode *ip, s64 committed_size)
{
	s64 bn;
	struct btstack btstack;
	int cmp;
	int index;
	int locked_leaves = 0;
	struct metapage *mp;
	xtpage_t *p;
	struct btframe *parent;
	int rc;
	struct tblock *tblk;
	struct tlock *tlck = NULL;
	xad_t *xad;
	int xlen;
	s64 xoff;
	struct xtlock *xtlck = NULL;

	/* save object truncation type */
	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_PMAP;

	/* clear stack */
	BT_CLR(&btstack);

	if (committed_size) {
		xoff = (committed_size >> JFS_SBI(ip->i_sb)->l2bsize) - 1;
		rc = xtSearch(ip, xoff, NULL, &cmp, &btstack, 0);
		if (rc)
			return rc;

		XT_GETSEARCH(ip, btstack.top, bn, mp, p, index);

		if (cmp != 0) {
			XT_PUTPAGE(mp);
			jfs_error(ip->i_sb,
				  "xtTruncate_pmap: did not find extent");
			return -EIO;
		}
	} else {
		/*
		 * start with root
		 *
		 * root resides in the inode
		 */
		bn = 0;

		/*
		 * first access of each page:
		 */
      getPage:
		XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
		if (rc)
			return rc;

		/* process entries backward from last index */
		index = le16_to_cpu(p->header.nextindex) - 1;

		if (p->header.flag & BT_INTERNAL)
			goto getChild;
	}

	/*
	 *	leaf page
	 */

	if (++locked_leaves > MAX_TRUNCATE_LEAVES) {
		/*
		 * We need to limit the size of the transaction
		 * to avoid exhausting pagecache & tlocks
		 */
		xad = &p->xad[index];
		xoff = offsetXAD(xad);
		xlen = lengthXAD(xad);
		XT_PUTPAGE(mp);
		return (xoff + xlen) << JFS_SBI(ip->i_sb)->l2bsize;
	}
	tlck = txLock(tid, ip, mp, tlckXTREE);
	tlck->type = tlckXTREE | tlckFREE;
	xtlck = (struct xtlock *) & tlck->lock;
	xtlck->hwm.offset = index;


	XT_PUTPAGE(mp);

	/*
	 * go back up to the parent page
	 */
      getParent:
	/* pop/restore parent entry for the current child page */
	if ((parent = BT_POP(&btstack)) == NULL)
		/* current page must have been root */
		goto out;

	/* get back the parent page */
	bn = parent->bn;
	XT_GETPAGE(ip, bn, mp, PSIZE, p, rc);
	if (rc)
		return rc;

	index = parent->index;

	/*
	 * parent page become empty: free the page
	 */
	if (index == XTENTRYSTART) {
		/* txCommit() with tlckFREE:
		 * free child extents covered by parent;
		 * invalidate parent if COMMIT_PWMAP;
		 */
		tlck = txLock(tid, ip, mp, tlckXTREE);
		xtlck = (struct xtlock *) & tlck->lock;
		xtlck->hwm.offset = le16_to_cpu(p->header.nextindex) - 1;
		tlck->type = tlckXTREE | tlckFREE;

		XT_PUTPAGE(mp);

		if (p->header.flag & BT_ROOT) {

			goto out;
		} else {
			goto getParent;
		}
	}
	/*
	 * parent page still has entries for front region;
	 */
	else
		index--;
	/*
	 *	internal page: go down to child page of current entry
	 */
      getChild:
	/* save current parent entry for the child page */
	if (BT_STACK_FULL(&btstack)) {
		jfs_error(ip->i_sb, "stack overrun in xtTruncate_pmap!");
		XT_PUTPAGE(mp);
		return -EIO;
	}
	BT_PUSH(&btstack, bn, index);

	/* get child page */
	xad = &p->xad[index];
	bn = addressXAD(xad);

	/*
	 * first access of each internal entry:
	 */
	/* release parent page */
	XT_PUTPAGE(mp);

	/* process the child page */
	goto getPage;

      out:

	return 0;
}

#ifdef CONFIG_JFS_STATISTICS
int jfs_xtstat_read(char *buffer, char **start, off_t offset, int length,
		    int *eof, void *data)
{
	int len = 0;
	off_t begin;

	len += sprintf(buffer,
		       "JFS Xtree statistics\n"
		       "====================\n"
		       "searches = %d\n"
		       "fast searches = %d\n"
		       "splits = %d\n",
		       xtStat.search,
		       xtStat.fastSearch,
		       xtStat.split);

	begin = offset;
	*start = buffer + begin;
	len -= begin;

	if (len > length)
		len = length;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
#endif
