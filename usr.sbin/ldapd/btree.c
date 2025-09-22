/*	$OpenBSD: btree.c,v 1.38 2017/05/26 21:23:14 sthen Exp $ */

/*
 * Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/tree.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "btree.h"

/* #define DEBUG */

#ifdef DEBUG
# define DPRINTF(...)	do { fprintf(stderr, "%s:%d: ", __func__, __LINE__); \
			     fprintf(stderr, __VA_ARGS__); \
			     fprintf(stderr, "\n"); } while(0)
#else
# define DPRINTF(...)
#endif

#define PAGESIZE	 4096
#define BT_MINKEYS	 4
#define BT_MAGIC	 0xB3DBB3DB
#define BT_VERSION	 4
#define MAXKEYSIZE	 255

#define P_INVALID	 0xFFFFFFFF

#define F_ISSET(w, f)	 (((w) & (f)) == (f))
#define MINIMUM(a,b)	 ((a) < (b) ? (a) : (b))

typedef uint32_t	 pgno_t;
typedef uint16_t	 indx_t;

/* There are four page types: meta, index, leaf and overflow.
 * They all share the same page header.
 */
struct page {				/* represents an on-disk page */
	pgno_t		 pgno;		/* page number */
#define	P_BRANCH	 0x01		/* branch page */
#define	P_LEAF		 0x02		/* leaf page */
#define	P_OVERFLOW	 0x04		/* overflow page */
#define	P_META		 0x08		/* meta page */
#define	P_HEAD		 0x10		/* header page */
	uint32_t	 flags;
#define lower		 b.fb.fb_lower
#define upper		 b.fb.fb_upper
#define p_next_pgno	 b.pb_next_pgno
	union page_bounds {
		struct {
			indx_t	 fb_lower;	/* lower bound of free space */
			indx_t	 fb_upper;	/* upper bound of free space */
		} fb;
		pgno_t		 pb_next_pgno;	/* overflow page linked list */
	} b;
	indx_t		 ptrs[1];		/* dynamic size */
} __packed;

#define PAGEHDRSZ	 offsetof(struct page, ptrs)

#define NUMKEYSP(p)	 (((p)->lower - PAGEHDRSZ) >> 1)
#define NUMKEYS(mp)	 (((mp)->page->lower - PAGEHDRSZ) >> 1)
#define SIZELEFT(mp)	 (indx_t)((mp)->page->upper - (mp)->page->lower)
#define PAGEFILL(bt, mp) (1000 * ((bt)->head.psize - PAGEHDRSZ - SIZELEFT(mp)) / \
				((bt)->head.psize - PAGEHDRSZ))
#define IS_LEAF(mp)	 F_ISSET((mp)->page->flags, P_LEAF)
#define IS_BRANCH(mp)	 F_ISSET((mp)->page->flags, P_BRANCH)
#define IS_OVERFLOW(mp)	 F_ISSET((mp)->page->flags, P_OVERFLOW)

struct bt_head {				/* header page content */
	uint32_t	 magic;
	uint32_t	 version;
	uint32_t	 flags;
	uint32_t	 psize;			/* page size */
} __packed;

struct bt_meta {				/* meta (footer) page content */
#define BT_TOMBSTONE	 0x01			/* file is replaced */
	uint32_t	 flags;
	pgno_t		 root;			/* page number of root page */
	pgno_t		 prev_meta;		/* previous meta page number */
	time_t		 created_at;
	uint32_t	 branch_pages;
	uint32_t	 leaf_pages;
	uint32_t	 overflow_pages;
	uint32_t	 revisions;
	uint32_t	 depth;
	uint64_t	 entries;
	unsigned char	 hash[SHA_DIGEST_LENGTH];
} __packed;

struct btkey {
	size_t			 len;
	char			 str[MAXKEYSIZE];
};

struct mpage {					/* an in-memory cached page */
	RB_ENTRY(mpage)		 entry;		/* page cache entry */
	SIMPLEQ_ENTRY(mpage)	 next;		/* queue of dirty pages */
	TAILQ_ENTRY(mpage)	 lru_next;	/* LRU queue */
	struct mpage		*parent;	/* NULL if root */
	unsigned int		 parent_index;	/* keep track of node index */
	struct btkey		 prefix;
	struct page		*page;
	pgno_t			 pgno;		/* copy of page->pgno */
	short			 ref;		/* increased by cursors */
	short			 dirty;		/* 1 if on dirty queue */
};
RB_HEAD(page_cache, mpage);
SIMPLEQ_HEAD(dirty_queue, mpage);
TAILQ_HEAD(lru_queue, mpage);

static int		 mpage_cmp(struct mpage *a, struct mpage *b);
static struct mpage	*mpage_lookup(struct btree *bt, pgno_t pgno);
static void		 mpage_add(struct btree *bt, struct mpage *mp);
static void		 mpage_free(struct mpage *mp);
static void		 mpage_del(struct btree *bt, struct mpage *mp);
static void		 mpage_flush(struct btree *bt);
static struct mpage	*mpage_copy(struct btree *bt, struct mpage *mp);
static void		 mpage_prune(struct btree *bt);
static void		 mpage_dirty(struct btree *bt, struct mpage *mp);
static struct mpage	*mpage_touch(struct btree *bt, struct mpage *mp);

RB_PROTOTYPE(page_cache, mpage, entry, mpage_cmp);
RB_GENERATE(page_cache, mpage, entry, mpage_cmp);

struct ppage {					/* ordered list of pages */
	SLIST_ENTRY(ppage)	 entry;
	struct mpage		*mpage;
	unsigned int		 ki;		/* cursor index on page */
};
SLIST_HEAD(page_stack, ppage);

#define CURSOR_EMPTY(c)		 SLIST_EMPTY(&(c)->stack)
#define CURSOR_TOP(c)		 SLIST_FIRST(&(c)->stack)
#define CURSOR_POP(c)		 SLIST_REMOVE_HEAD(&(c)->stack, entry)
#define CURSOR_PUSH(c,p)	 SLIST_INSERT_HEAD(&(c)->stack, p, entry)

struct cursor {
	struct btree		*bt;
	struct btree_txn	*txn;
	struct page_stack	 stack;		/* stack of parent pages */
	short			 initialized;	/* 1 if initialized */
	short			 eof;		/* 1 if end is reached */
};

#define METAHASHLEN	 offsetof(struct bt_meta, hash)
#define METADATA(p)	 ((void *)((char *)p + PAGEHDRSZ))

struct node {
#define n_pgno		 p.np_pgno
#define n_dsize		 p.np_dsize
	union {
		pgno_t		 np_pgno;	/* child page number */
		uint32_t	 np_dsize;	/* leaf data size */
	}		 p;
	uint16_t	 ksize;			/* key size */
#define F_BIGDATA	 0x01			/* data put on overflow page */
	uint8_t		 flags;
	char		 data[1];
} __packed;

struct btree_txn {
	pgno_t			 root;		/* current / new root page */
	pgno_t			 next_pgno;	/* next unallocated page */
	struct btree		*bt;		/* btree is ref'd */
	struct dirty_queue	*dirty_queue;	/* modified pages */
#define BT_TXN_RDONLY		 0x01		/* read-only transaction */
#define BT_TXN_ERROR		 0x02		/* an error has occurred */
	unsigned int		 flags;
};

struct btree {
	int			 fd;
	char			*path;
#define BT_FIXPADDING		 0x01		/* internal */
	unsigned int		 flags;
	bt_cmp_func		 cmp;		/* user compare function */
	struct bt_head		 head;
	struct bt_meta		 meta;
	struct page_cache	*page_cache;
	struct lru_queue	*lru_queue;
	struct btree_txn	*txn;		/* current write transaction */
	int			 ref;		/* increased by cursors & txn */
	struct btree_stat	 stat;
	off_t			 size;		/* current file size */
};

#define NODESIZE	 offsetof(struct node, data)

#define INDXSIZE(k)	 (NODESIZE + ((k) == NULL ? 0 : (k)->size))
#define LEAFSIZE(k, d)	 (NODESIZE + (k)->size + (d)->size)
#define NODEPTRP(p, i)	 ((struct node *)((char *)(p) + (p)->ptrs[i]))
#define NODEPTR(mp, i)	 NODEPTRP((mp)->page, i)
#define NODEKEY(node)	 (void *)((node)->data)
#define NODEDATA(node)	 (void *)((char *)(node)->data + (node)->ksize)
#define NODEPGNO(node)	 ((node)->p.np_pgno)
#define NODEDSZ(node)	 ((node)->p.np_dsize)

#define BT_COMMIT_PAGES	 64	/* max number of pages to write in one commit */
#define BT_MAXCACHE_DEF	 1024	/* max number of pages to keep in cache  */

static int		 btree_read_page(struct btree *bt, pgno_t pgno,
			    struct page *page);
static struct mpage	*btree_get_mpage(struct btree *bt, pgno_t pgno);
static int		 btree_search_page_root(struct btree *bt,
			    struct mpage *root, struct btval *key,
			    struct cursor *cursor, int modify,
			    struct mpage **mpp);
static int		 btree_search_page(struct btree *bt,
			    struct btree_txn *txn, struct btval *key,
			    struct cursor *cursor, int modify,
			    struct mpage **mpp);

static int		 btree_write_header(struct btree *bt, int fd);
static int		 btree_read_header(struct btree *bt);
static int		 btree_is_meta_page(struct page *p);
static int		 btree_read_meta(struct btree *bt, pgno_t *p_next);
static int		 btree_write_meta(struct btree *bt, pgno_t root,
			    unsigned int flags);
static void		 btree_ref(struct btree *bt);

static struct node	*btree_search_node(struct btree *bt, struct mpage *mp,
			    struct btval *key, int *exactp, unsigned int *kip);
static int		 btree_add_node(struct btree *bt, struct mpage *mp,
			    indx_t indx, struct btval *key, struct btval *data,
			    pgno_t pgno, uint8_t flags);
static void		 btree_del_node(struct btree *bt, struct mpage *mp,
			    indx_t indx);
static int		 btree_read_data(struct btree *bt, struct mpage *mp,
			    struct node *leaf, struct btval *data);

static int		 btree_rebalance(struct btree *bt, struct mpage *mp);
static int		 btree_update_key(struct btree *bt, struct mpage *mp,
			    indx_t indx, struct btval *key);
static int		 btree_adjust_prefix(struct btree *bt,
			    struct mpage *src, int delta);
static int		 btree_move_node(struct btree *bt, struct mpage *src,
			    indx_t srcindx, struct mpage *dst, indx_t dstindx);
static int		 btree_merge(struct btree *bt, struct mpage *src,
			    struct mpage *dst);
static int		 btree_split(struct btree *bt, struct mpage **mpp,
			    unsigned int *newindxp, struct btval *newkey,
			    struct btval *newdata, pgno_t newpgno);
static struct mpage	*btree_new_page(struct btree *bt, uint32_t flags);
static int		 btree_write_overflow_data(struct btree *bt,
			    struct page *p, struct btval *data);

static void		 cursor_pop_page(struct cursor *cursor);
static struct ppage	 *cursor_push_page(struct cursor *cursor,
			    struct mpage *mp);

static int		 bt_set_key(struct btree *bt, struct mpage *mp,
			    struct node *node, struct btval *key);
static int		 btree_sibling(struct cursor *cursor, int move_right);
static int		 btree_cursor_next(struct cursor *cursor,
			    struct btval *key, struct btval *data);
static int		 btree_cursor_set(struct cursor *cursor,
			    struct btval *key, struct btval *data, int *exactp);
static int		 btree_cursor_first(struct cursor *cursor,
			    struct btval *key, struct btval *data);

static void		 bt_reduce_separator(struct btree *bt, struct node *min,
			    struct btval *sep);
static void		 remove_prefix(struct btree *bt, struct btval *key,
			    size_t pfxlen);
static void		 expand_prefix(struct btree *bt, struct mpage *mp,
			    indx_t indx, struct btkey *expkey);
static void		 concat_prefix(struct btree *bt, char *s1, size_t n1,
			    char *s2, size_t n2, char *cs, size_t *cn);
static void		 common_prefix(struct btree *bt, struct btkey *min,
			    struct btkey *max, struct btkey *pfx);
static void		 find_common_prefix(struct btree *bt, struct mpage *mp);

static size_t		 bt_leaf_size(struct btree *bt, struct btval *key,
			    struct btval *data);
static size_t		 bt_branch_size(struct btree *bt, struct btval *key);

static pgno_t		 btree_compact_tree(struct btree *bt, pgno_t pgno,
			    struct btree *btc);

static int		 memncmp(const void *s1, size_t n1,
				 const void *s2, size_t n2);
static int		 memnrcmp(const void *s1, size_t n1,
				  const void *s2, size_t n2);

static int
memncmp(const void *s1, size_t n1, const void *s2, size_t n2)
{
	if (n1 < n2) {
		if (memcmp(s1, s2, n1) == 0)
			return -1;
	}
	else if (n1 > n2) {
		if (memcmp(s1, s2, n2) == 0)
			return 1;
	}
	return memcmp(s1, s2, n1);
}

static int
memnrcmp(const void *s1, size_t n1, const void *s2, size_t n2)
{
	const unsigned char	*p1;
	const unsigned char	*p2;

	if (n1 == 0)
		return n2 == 0 ? 0 : -1;

	if (n2 == 0)
		return n1 == 0 ? 0 : 1;

	p1 = (const unsigned char *)s1 + n1 - 1;
	p2 = (const unsigned char *)s2 + n2 - 1;

	while (*p1 == *p2) {
		if (p1 == s1)
			return (p2 == s2) ? 0 : -1;
		if (p2 == s2)
			return (p1 == p2) ? 0 : 1;
		p1--;
		p2--;
	}
	return *p1 - *p2;
}

int
btree_cmp(struct btree *bt, const struct btval *a, const struct btval *b)
{
	return bt->cmp(a, b);
}

static void
common_prefix(struct btree *bt, struct btkey *min, struct btkey *max,
    struct btkey *pfx)
{
	size_t		 n = 0;
	char		*p1;
	char		*p2;

	if (min->len == 0 || max->len == 0) {
		pfx->len = 0;
		return;
	}

	if (F_ISSET(bt->flags, BT_REVERSEKEY)) {
		p1 = min->str + min->len - 1;
		p2 = max->str + max->len - 1;

		while (*p1 == *p2) {
			if (p1 < min->str || p2 < max->str)
				break;
			p1--;
			p2--;
			n++;
		}

		assert(n <= (int)sizeof(pfx->str));
		pfx->len = n;
		bcopy(p2 + 1, pfx->str, n);
	} else {
		p1 = min->str;
		p2 = max->str;

		while (*p1 == *p2) {
			if (n == min->len || n == max->len)
				break;
			p1++;
			p2++;
			n++;
		}

		assert(n <= (int)sizeof(pfx->str));
		pfx->len = n;
		bcopy(max->str, pfx->str, n);
	}
}

static void
remove_prefix(struct btree *bt, struct btval *key, size_t pfxlen)
{
	if (pfxlen == 0 || bt->cmp != NULL)
		return;

	DPRINTF("removing %zu bytes of prefix from key [%.*s]", pfxlen,
	    (int)key->size, (char *)key->data);
	assert(pfxlen <= key->size);
	key->size -= pfxlen;
	if (!F_ISSET(bt->flags, BT_REVERSEKEY))
		key->data = (char *)key->data + pfxlen;
}

static void
expand_prefix(struct btree *bt, struct mpage *mp, indx_t indx,
    struct btkey *expkey)
{
	struct node	*node;

	node = NODEPTR(mp, indx);
	expkey->len = sizeof(expkey->str);
	concat_prefix(bt, mp->prefix.str, mp->prefix.len,
	    NODEKEY(node), node->ksize, expkey->str, &expkey->len);
}

static int
bt_cmp(struct btree *bt, const struct btval *key1, const struct btval *key2,
    struct btkey *pfx)
{
	if (F_ISSET(bt->flags, BT_REVERSEKEY))
		return memnrcmp(key1->data, key1->size - pfx->len,
		    key2->data, key2->size);
	else
		return memncmp((char *)key1->data + pfx->len, key1->size - pfx->len,
		    key2->data, key2->size);
}

void
btval_reset(struct btval *btv)
{
	if (btv) {
		if (btv->mp)
			btv->mp->ref--;
		if (btv->free_data)
			free(btv->data);
		memset(btv, 0, sizeof(*btv));
	}
}

static int
mpage_cmp(struct mpage *a, struct mpage *b)
{
	if (a->pgno > b->pgno)
		return 1;
	if (a->pgno < b->pgno)
		return -1;
	return 0;
}

static struct mpage *
mpage_lookup(struct btree *bt, pgno_t pgno)
{
	struct mpage	 find, *mp;

	find.pgno = pgno;
	mp = RB_FIND(page_cache, bt->page_cache, &find);
	if (mp) {
		bt->stat.hits++;
		/* Update LRU queue. Move page to the end. */
		TAILQ_REMOVE(bt->lru_queue, mp, lru_next);
		TAILQ_INSERT_TAIL(bt->lru_queue, mp, lru_next);
	}
	return mp;
}

static void
mpage_add(struct btree *bt, struct mpage *mp)
{
	assert(RB_INSERT(page_cache, bt->page_cache, mp) == NULL);
	bt->stat.cache_size++;
	TAILQ_INSERT_TAIL(bt->lru_queue, mp, lru_next);
}

static void
mpage_free(struct mpage *mp)
{
	if (mp != NULL) {
		free(mp->page);
		free(mp);
	}
}

static void
mpage_del(struct btree *bt, struct mpage *mp)
{
	assert(RB_REMOVE(page_cache, bt->page_cache, mp) == mp);
	assert(bt->stat.cache_size > 0);
	bt->stat.cache_size--;
	TAILQ_REMOVE(bt->lru_queue, mp, lru_next);
}

static void
mpage_flush(struct btree *bt)
{
	struct mpage	*mp;

	while ((mp = RB_MIN(page_cache, bt->page_cache)) != NULL) {
		mpage_del(bt, mp);
		mpage_free(mp);
	}
}

static struct mpage *
mpage_copy(struct btree *bt, struct mpage *mp)
{
	struct mpage	*copy;

	if ((copy = calloc(1, sizeof(*copy))) == NULL)
		return NULL;
	if ((copy->page = malloc(bt->head.psize)) == NULL) {
		free(copy);
		return NULL;
	}
	bcopy(mp->page, copy->page, bt->head.psize);
	bcopy(&mp->prefix, &copy->prefix, sizeof(mp->prefix));
	copy->parent = mp->parent;
	copy->parent_index = mp->parent_index;
	copy->pgno = mp->pgno;

	return copy;
}

/* Remove the least recently used memory pages until the cache size is
 * within the configured bounds. Pages referenced by cursors or returned
 * key/data are not pruned.
 */
static void
mpage_prune(struct btree *bt)
{
	struct mpage	*mp, *next;

	for (mp = TAILQ_FIRST(bt->lru_queue); mp; mp = next) {
		if (bt->stat.cache_size <= bt->stat.max_cache)
			break;
		next = TAILQ_NEXT(mp, lru_next);
		if (!mp->dirty && mp->ref <= 0) {
			mpage_del(bt, mp);
			mpage_free(mp);
		}
	}
}

/* Mark a page as dirty and push it on the dirty queue.
 */
static void
mpage_dirty(struct btree *bt, struct mpage *mp)
{
	assert(bt != NULL);
	assert(bt->txn != NULL);

	if (!mp->dirty) {
		mp->dirty = 1;
		SIMPLEQ_INSERT_TAIL(bt->txn->dirty_queue, mp, next);
	}
}

/* Touch a page: make it dirty and re-insert into tree with updated pgno.
 */
static struct mpage *
mpage_touch(struct btree *bt, struct mpage *mp)
{
	assert(bt != NULL);
	assert(bt->txn != NULL);
	assert(mp != NULL);

	if (!mp->dirty) {
		DPRINTF("touching page %u -> %u", mp->pgno, bt->txn->next_pgno);
		if (mp->ref == 0)
			mpage_del(bt, mp);
		else {
			if ((mp = mpage_copy(bt, mp)) == NULL)
				return NULL;
		}
		mp->pgno = mp->page->pgno = bt->txn->next_pgno++;
		mpage_dirty(bt, mp);
		mpage_add(bt, mp);

		/* Update the page number to new touched page. */
		if (mp->parent != NULL)
			NODEPGNO(NODEPTR(mp->parent,
			    mp->parent_index)) = mp->pgno;
	}

	return mp;
}

static int
btree_read_page(struct btree *bt, pgno_t pgno, struct page *page)
{
	ssize_t		 rc;

	DPRINTF("reading page %u", pgno);
	bt->stat.reads++;
	if ((rc = pread(bt->fd, page, bt->head.psize, (off_t)pgno*bt->head.psize)) == 0) {
		DPRINTF("page %u doesn't exist", pgno);
		errno = ENOENT;
		return BT_FAIL;
	} else if (rc != (ssize_t)bt->head.psize) {
		if (rc > 0)
			errno = EINVAL;
		DPRINTF("read: %s", strerror(errno));
		return BT_FAIL;
	}

	if (page->pgno != pgno) {
		DPRINTF("page numbers don't match: %u != %u", pgno, page->pgno);
		errno = EINVAL;
		return BT_FAIL;
	}

	DPRINTF("page %u has flags 0x%X", pgno, page->flags);

	return BT_SUCCESS;
}

int
btree_sync(struct btree *bt)
{
	if (!F_ISSET(bt->flags, BT_NOSYNC))
		return fsync(bt->fd);
	return 0;
}

struct btree_txn *
btree_txn_begin(struct btree *bt, int rdonly)
{
	struct btree_txn	*txn;

	if (!rdonly && bt->txn != NULL) {
		DPRINTF("write transaction already begun");
		errno = EBUSY;
		return NULL;
	}

	if ((txn = calloc(1, sizeof(*txn))) == NULL) {
		DPRINTF("calloc: %s", strerror(errno));
		return NULL;
	}

	if (rdonly) {
		txn->flags |= BT_TXN_RDONLY;
	} else {
		txn->dirty_queue = calloc(1, sizeof(*txn->dirty_queue));
		if (txn->dirty_queue == NULL) {
			free(txn);
			return NULL;
		}
		SIMPLEQ_INIT(txn->dirty_queue);

		DPRINTF("taking write lock on txn %p", txn);
		if (flock(bt->fd, LOCK_EX | LOCK_NB) != 0) {
			DPRINTF("flock: %s", strerror(errno));
			errno = EBUSY;
			free(txn->dirty_queue);
			free(txn);
			return NULL;
		}
		bt->txn = txn;
	}

	txn->bt = bt;
	btree_ref(bt);

	if (btree_read_meta(bt, &txn->next_pgno) != BT_SUCCESS) {
		btree_txn_abort(txn);
		return NULL;
	}

	txn->root = bt->meta.root;
	DPRINTF("begin transaction on btree %p, root page %u", bt, txn->root);

	return txn;
}

void
btree_txn_abort(struct btree_txn *txn)
{
	struct mpage	*mp;
	struct btree	*bt;

	if (txn == NULL)
		return;

	bt = txn->bt;
	DPRINTF("abort transaction on btree %p, root page %u", bt, txn->root);

	if (!F_ISSET(txn->flags, BT_TXN_RDONLY)) {
		/* Discard all dirty pages.
		 */
		while (!SIMPLEQ_EMPTY(txn->dirty_queue)) {
			mp = SIMPLEQ_FIRST(txn->dirty_queue);
			assert(mp->ref == 0);	/* cursors should be closed */
			mpage_del(bt, mp);
			SIMPLEQ_REMOVE_HEAD(txn->dirty_queue, next);
			mpage_free(mp);
		}

		DPRINTF("releasing write lock on txn %p", txn);
		txn->bt->txn = NULL;
		if (flock(txn->bt->fd, LOCK_UN) != 0) {
			DPRINTF("failed to unlock fd %d: %s",
			    txn->bt->fd, strerror(errno));
		}
		free(txn->dirty_queue);
	}

	btree_close(txn->bt);
	free(txn);
}

int
btree_txn_commit(struct btree_txn *txn)
{
	int		 n, done;
	ssize_t		 rc;
	off_t		 size;
	struct mpage	*mp;
	struct btree	*bt;
	struct iovec	 iov[BT_COMMIT_PAGES];

	assert(txn != NULL);
	assert(txn->bt != NULL);

	bt = txn->bt;

	if (F_ISSET(txn->flags, BT_TXN_RDONLY)) {
		DPRINTF("attempt to commit read-only transaction");
		btree_txn_abort(txn);
		errno = EPERM;
		return BT_FAIL;
	}

	if (txn != bt->txn) {
		DPRINTF("attempt to commit unknown transaction");
		btree_txn_abort(txn);
		errno = EINVAL;
		return BT_FAIL;
	}

	if (F_ISSET(txn->flags, BT_TXN_ERROR)) {
		DPRINTF("error flag is set, can't commit");
		btree_txn_abort(txn);
		errno = EINVAL;
		return BT_FAIL;
	}

	if (SIMPLEQ_EMPTY(txn->dirty_queue))
		goto done;

	if (F_ISSET(bt->flags, BT_FIXPADDING)) {
		size = lseek(bt->fd, 0, SEEK_END);
		size += bt->head.psize - (size % bt->head.psize);
		DPRINTF("extending to multiple of page size: %llu", size);
		if (ftruncate(bt->fd, size) != 0) {
			DPRINTF("ftruncate: %s", strerror(errno));
			btree_txn_abort(txn);
			return BT_FAIL;
		}
		bt->flags &= ~BT_FIXPADDING;
	}

	DPRINTF("committing transaction on btree %p, root page %u",
	    bt, txn->root);

	/* Commit up to BT_COMMIT_PAGES dirty pages to disk until done.
	 */
	do {
		n = 0;
		done = 1;
		SIMPLEQ_FOREACH(mp, txn->dirty_queue, next) {
			DPRINTF("committing page %u", mp->pgno);
			iov[n].iov_len = bt->head.psize;
			iov[n].iov_base = mp->page;
			if (++n >= BT_COMMIT_PAGES) {
				done = 0;
				break;
			}
		}

		if (n == 0)
			break;

		DPRINTF("committing %u dirty pages", n);
		rc = writev(bt->fd, iov, n);
		if (rc != (ssize_t)bt->head.psize*n) {
			if (rc > 0)
				DPRINTF("short write, filesystem full?");
			else
				DPRINTF("writev: %s", strerror(errno));
			btree_txn_abort(txn);
			return BT_FAIL;
		}

		/* Remove the dirty flag from the written pages.
		 */
		while (!SIMPLEQ_EMPTY(txn->dirty_queue)) {
			mp = SIMPLEQ_FIRST(txn->dirty_queue);
			mp->dirty = 0;
			SIMPLEQ_REMOVE_HEAD(txn->dirty_queue, next);
			if (--n == 0)
				break;
		}
	} while (!done);

	if (btree_sync(bt) != 0 ||
	    btree_write_meta(bt, txn->root, 0) != BT_SUCCESS ||
	    btree_sync(bt) != 0) {
		btree_txn_abort(txn);
		return BT_FAIL;
	}

done:
	mpage_prune(bt);
	btree_txn_abort(txn);

	return BT_SUCCESS;
}

static int
btree_write_header(struct btree *bt, int fd)
{
	struct stat	 sb;
	struct bt_head	*h;
	struct page	*p;
	ssize_t		 rc;
	unsigned int	 psize;

	DPRINTF("writing header page");
	assert(bt != NULL);

	/*
	 * Ask stat for 'optimal blocksize for I/O', but cap to fit in indx_t.
	 */
	if (fstat(fd, &sb) == 0)
		psize = MINIMUM(32*1024, sb.st_blksize);
	else
		psize = PAGESIZE;

	if ((p = calloc(1, psize)) == NULL)
		return -1;
	p->flags = P_HEAD;

	h = METADATA(p);
	h->magic = BT_MAGIC;
	h->version = BT_VERSION;
	h->psize = psize;
	bcopy(h, &bt->head, sizeof(*h));

	rc = write(fd, p, bt->head.psize);
	free(p);
	if (rc != (ssize_t)bt->head.psize) {
		if (rc > 0)
			DPRINTF("short write, filesystem full?");
		return BT_FAIL;
	}

	return BT_SUCCESS;
}

static int
btree_read_header(struct btree *bt)
{
	char		 page[PAGESIZE];
	struct page	*p;
	struct bt_head	*h;
	int		 rc;

	assert(bt != NULL);

	/* We don't know the page size yet, so use a minimum value.
	 */

	if ((rc = pread(bt->fd, page, PAGESIZE, 0)) == 0) {
		errno = ENOENT;
		return -1;
	} else if (rc != PAGESIZE) {
		if (rc > 0)
			errno = EINVAL;
		DPRINTF("read: %s", strerror(errno));
		return -1;
	}

	p = (struct page *)page;

	if (!F_ISSET(p->flags, P_HEAD)) {
		DPRINTF("page %d not a header page", p->pgno);
		errno = EINVAL;
		return -1;
	}

	h = METADATA(p);
	if (h->magic != BT_MAGIC) {
		DPRINTF("header has invalid magic");
		errno = EINVAL;
		return -1;
	}

	if (h->version != BT_VERSION) {
		DPRINTF("database is version %u, expected version %u",
		    bt->head.version, BT_VERSION);
		errno = EINVAL;
		return -1;
	}

	bcopy(h, &bt->head, sizeof(*h));
	return 0;
}

static int
btree_write_meta(struct btree *bt, pgno_t root, unsigned int flags)
{
	struct mpage	*mp;
	struct bt_meta	*meta;
	ssize_t		 rc;

	DPRINTF("writing meta page for root page %u", root);

	assert(bt != NULL);
	assert(bt->txn != NULL);

	if ((mp = btree_new_page(bt, P_META)) == NULL)
		return -1;

	bt->meta.prev_meta = bt->meta.root;
	bt->meta.root = root;
	bt->meta.flags = flags;
	bt->meta.created_at = time(0);
	bt->meta.revisions++;
	SHA1((unsigned char *)&bt->meta, METAHASHLEN, bt->meta.hash);

	/* Copy the meta data changes to the new meta page. */
	meta = METADATA(mp->page);
	bcopy(&bt->meta, meta, sizeof(*meta));

	rc = write(bt->fd, mp->page, bt->head.psize);
	mp->dirty = 0;
	SIMPLEQ_REMOVE_HEAD(bt->txn->dirty_queue, next);
	if (rc != (ssize_t)bt->head.psize) {
		if (rc > 0)
			DPRINTF("short write, filesystem full?");
		return BT_FAIL;
	}

	if ((bt->size = lseek(bt->fd, 0, SEEK_END)) == -1) {
		DPRINTF("failed to update file size: %s", strerror(errno));
		bt->size = 0;
	}

	return BT_SUCCESS;
}

/* Returns true if page p is a valid meta page, false otherwise.
 */
static int
btree_is_meta_page(struct page *p)
{
	struct bt_meta	*m;
	unsigned char	 hash[SHA_DIGEST_LENGTH];

	m = METADATA(p);
	if (!F_ISSET(p->flags, P_META)) {
		DPRINTF("page %d not a meta page", p->pgno);
		errno = EINVAL;
		return 0;
	}

	if (m->root >= p->pgno && m->root != P_INVALID) {
		DPRINTF("page %d points to an invalid root page", p->pgno);
		errno = EINVAL;
		return 0;
	}

	SHA1((unsigned char *)m, METAHASHLEN, hash);
	if (bcmp(hash, m->hash, SHA_DIGEST_LENGTH) != 0) {
		DPRINTF("page %d has an invalid digest", p->pgno);
		errno = EINVAL;
		return 0;
	}

	return 1;
}

static int
btree_read_meta(struct btree *bt, pgno_t *p_next)
{
	struct mpage	*mp;
	struct bt_meta	*meta;
	pgno_t		 meta_pgno, next_pgno;
	off_t		 size;

	assert(bt != NULL);

	if ((size = lseek(bt->fd, 0, SEEK_END)) == -1)
		goto fail;

	DPRINTF("btree_read_meta: size = %llu", size);

	if (size < bt->size) {
		DPRINTF("file has shrunk!");
		errno = EIO;
		goto fail;
	}

	if (size == bt->head.psize) {		/* there is only the header */
		if (p_next != NULL)
			*p_next = 1;
		return BT_SUCCESS;		/* new file */
	}

	next_pgno = size / bt->head.psize;
	if (next_pgno == 0) {
		DPRINTF("corrupt file");
		errno = EIO;
		goto fail;
	}

	meta_pgno = next_pgno - 1;

	if (size % bt->head.psize != 0) {
		DPRINTF("filesize not a multiple of the page size!");
		bt->flags |= BT_FIXPADDING;
		next_pgno++;
	}

	if (p_next != NULL)
		*p_next = next_pgno;

	if (size == bt->size) {
		DPRINTF("size unchanged, keeping current meta page");
		if (F_ISSET(bt->meta.flags, BT_TOMBSTONE)) {
			DPRINTF("file is dead");
			errno = ESTALE;
			return BT_FAIL;
		} else
			return BT_SUCCESS;
	}
	bt->size = size;

	while (meta_pgno > 0) {
		if ((mp = btree_get_mpage(bt, meta_pgno)) == NULL)
			break;
		if (btree_is_meta_page(mp->page)) {
			meta = METADATA(mp->page);
			DPRINTF("flags = 0x%x", meta->flags);
			if (F_ISSET(meta->flags, BT_TOMBSTONE)) {
				DPRINTF("file is dead");
				errno = ESTALE;
				return BT_FAIL;
			} else {
				/* Make copy of last meta page. */
				bcopy(meta, &bt->meta, sizeof(bt->meta));
				return BT_SUCCESS;
			}
		}
		--meta_pgno;	/* scan backwards to first valid meta page */
	}

	errno = EIO;
fail:
	if (p_next != NULL)
		*p_next = P_INVALID;
	return BT_FAIL;
}

struct btree *
btree_open_fd(int fd, unsigned int flags)
{
	struct btree	*bt;
	int		 fl;

	fl = fcntl(fd, F_GETFL);
	if (fcntl(fd, F_SETFL, fl | O_APPEND) == -1)
		return NULL;

	if ((bt = calloc(1, sizeof(*bt))) == NULL)
		return NULL;
	bt->fd = fd;
	bt->flags = flags;
	bt->flags &= ~BT_FIXPADDING;
	bt->ref = 1;
	bt->meta.root = P_INVALID;

	if ((bt->page_cache = calloc(1, sizeof(*bt->page_cache))) == NULL)
		goto fail;
	bt->stat.max_cache = BT_MAXCACHE_DEF;
	RB_INIT(bt->page_cache);

	if ((bt->lru_queue = calloc(1, sizeof(*bt->lru_queue))) == NULL)
		goto fail;
	TAILQ_INIT(bt->lru_queue);

	if (btree_read_header(bt) != 0) {
		if (errno != ENOENT)
			goto fail;
		DPRINTF("new database");
		btree_write_header(bt, bt->fd);
	}

	if (btree_read_meta(bt, NULL) != 0)
		goto fail;

	DPRINTF("opened database version %u, pagesize %u",
	    bt->head.version, bt->head.psize);
	DPRINTF("timestamp: %s", ctime(&bt->meta.created_at));
	DPRINTF("depth: %u", bt->meta.depth);
	DPRINTF("entries: %llu", bt->meta.entries);
	DPRINTF("revisions: %u", bt->meta.revisions);
	DPRINTF("branch pages: %u", bt->meta.branch_pages);
	DPRINTF("leaf pages: %u", bt->meta.leaf_pages);
	DPRINTF("overflow pages: %u", bt->meta.overflow_pages);
	DPRINTF("root: %u", bt->meta.root);
	DPRINTF("previous meta page: %u", bt->meta.prev_meta);

	return bt;

fail:
	free(bt->lru_queue);
	free(bt->page_cache);
	free(bt);
	return NULL;
}

struct btree *
btree_open(const char *path, unsigned int flags, mode_t mode)
{
	int		 fd, oflags;
	struct btree	*bt;

	if (F_ISSET(flags, BT_RDONLY))
		oflags = O_RDONLY;
	else
		oflags = O_RDWR | O_CREAT | O_APPEND;

	if ((fd = open(path, oflags, mode)) == -1)
		return NULL;

	if ((bt = btree_open_fd(fd, flags)) == NULL)
		close(fd);
	else {
		bt->path = strdup(path);
		DPRINTF("opened btree %p", bt);
	}

	return bt;
}

static void
btree_ref(struct btree *bt)
{
	bt->ref++;
	DPRINTF("ref is now %d on btree %p", bt->ref, bt);
}

void
btree_close(struct btree *bt)
{
	if (bt == NULL)
		return;

	if (--bt->ref == 0) {
		DPRINTF("ref is zero, closing btree %p", bt);
		close(bt->fd);
		mpage_flush(bt);
		free(bt->lru_queue);
		free(bt->path);
		free(bt->page_cache);
		free(bt);
	} else
		DPRINTF("ref is now %d on btree %p", bt->ref, bt);
}

/* Search for key within a leaf page, using binary search.
 * Returns the smallest entry larger or equal to the key.
 * If exactp is non-null, stores whether the found entry was an exact match
 * in *exactp (1 or 0).
 * If kip is non-null, stores the index of the found entry in *kip.
 * If no entry larger of equal to the key is found, returns NULL.
 */
static struct node *
btree_search_node(struct btree *bt, struct mpage *mp, struct btval *key,
    int *exactp, unsigned int *kip)
{
	unsigned int	 i = 0;
	int		 low, high;
	int		 rc = 0;
	struct node	*node;
	struct btval	 nodekey;

	DPRINTF("searching %lu keys in %s page %u with prefix [%.*s]",
	    NUMKEYS(mp),
	    IS_LEAF(mp) ? "leaf" : "branch",
	    mp->pgno, (int)mp->prefix.len, (char *)mp->prefix.str);

	assert(NUMKEYS(mp) > 0);

	memset(&nodekey, 0, sizeof(nodekey));

	low = IS_LEAF(mp) ? 0 : 1;
	high = NUMKEYS(mp) - 1;
	while (low <= high) {
		i = (low + high) >> 1;
		node = NODEPTR(mp, i);

		nodekey.size = node->ksize;
		nodekey.data = NODEKEY(node);

		if (bt->cmp)
			rc = bt->cmp(key, &nodekey);
		else
			rc = bt_cmp(bt, key, &nodekey, &mp->prefix);

		if (IS_LEAF(mp))
			DPRINTF("found leaf index %u [%.*s], rc = %d",
			    i, (int)nodekey.size, (char *)nodekey.data, rc);
		else
			DPRINTF("found branch index %u [%.*s -> %u], rc = %d",
			    i, (int)node->ksize, (char *)NODEKEY(node),
			    node->n_pgno, rc);

		if (rc == 0)
			break;
		if (rc > 0)
			low = i + 1;
		else
			high = i - 1;
	}

	if (rc > 0) {	/* Found entry is less than the key. */
		i++;	/* Skip to get the smallest entry larger than key. */
		if (i >= NUMKEYS(mp))
			/* There is no entry larger or equal to the key. */
			return NULL;
	}
	if (exactp)
		*exactp = (rc == 0);
	if (kip)	/* Store the key index if requested. */
		*kip = i;

	return NODEPTR(mp, i);
}

static void
cursor_pop_page(struct cursor *cursor)
{
	struct ppage	*top;

	top = CURSOR_TOP(cursor);
	CURSOR_POP(cursor);
	top->mpage->ref--;

	DPRINTF("popped page %u off cursor %p", top->mpage->pgno, cursor);

	free(top);
}

static struct ppage *
cursor_push_page(struct cursor *cursor, struct mpage *mp)
{
	struct ppage	*ppage;

	DPRINTF("pushing page %u on cursor %p", mp->pgno, cursor);

	if ((ppage = calloc(1, sizeof(*ppage))) == NULL)
		return NULL;
	ppage->mpage = mp;
	mp->ref++;
	CURSOR_PUSH(cursor, ppage);
	return ppage;
}

static struct mpage *
btree_get_mpage(struct btree *bt, pgno_t pgno)
{
	struct mpage	*mp;

	mp = mpage_lookup(bt, pgno);
	if (mp == NULL) {
		if ((mp = calloc(1, sizeof(*mp))) == NULL)
			return NULL;
		if ((mp->page = malloc(bt->head.psize)) == NULL) {
			free(mp);
			return NULL;
		}
		if (btree_read_page(bt, pgno, mp->page) != BT_SUCCESS) {
			mpage_free(mp);
			return NULL;
		}
		mp->pgno = pgno;
		mpage_add(bt, mp);
	} else
		DPRINTF("returning page %u from cache", pgno);

	return mp;
}

static void
concat_prefix(struct btree *bt, char *s1, size_t n1, char *s2, size_t n2,
    char *cs, size_t *cn)
{
	assert(*cn >= n1 + n2);
	if (F_ISSET(bt->flags, BT_REVERSEKEY)) {
		bcopy(s2, cs, n2);
		bcopy(s1, cs + n2, n1);
	} else {
		bcopy(s1, cs, n1);
		bcopy(s2, cs + n1, n2);
	}
	*cn = n1 + n2;
}

static void
find_common_prefix(struct btree *bt, struct mpage *mp)
{
	indx_t			 lbound = 0, ubound = 0;
	struct mpage		*lp, *up;
	struct btkey		 lprefix, uprefix;

	mp->prefix.len = 0;
	if (bt->cmp != NULL)
		return;

	lp = mp;
	while (lp->parent != NULL) {
		if (lp->parent_index > 0) {
			lbound = lp->parent_index;
			break;
		}
		lp = lp->parent;
	}

	up = mp;
	while (up->parent != NULL) {
		if (up->parent_index + 1 < (indx_t)NUMKEYS(up->parent)) {
			ubound = up->parent_index + 1;
			break;
		}
		up = up->parent;
	}

	if (lp->parent != NULL && up->parent != NULL) {
		expand_prefix(bt, lp->parent, lbound, &lprefix);
		expand_prefix(bt, up->parent, ubound, &uprefix);
		common_prefix(bt, &lprefix, &uprefix, &mp->prefix);
	}
	else if (mp->parent)
		bcopy(&mp->parent->prefix, &mp->prefix, sizeof(mp->prefix));

	DPRINTF("found common prefix [%.*s] (len %zu) for page %u",
	    (int)mp->prefix.len, mp->prefix.str, mp->prefix.len, mp->pgno);
}

static int
btree_search_page_root(struct btree *bt, struct mpage *root, struct btval *key,
    struct cursor *cursor, int modify, struct mpage **mpp)
{
	struct mpage	*mp, *parent;

	if (cursor && cursor_push_page(cursor, root) == NULL)
		return BT_FAIL;

	mp = root;
	while (IS_BRANCH(mp)) {
		unsigned int	 i = 0;
		struct node	*node;

		DPRINTF("branch page %u has %lu keys", mp->pgno, NUMKEYS(mp));
		assert(NUMKEYS(mp) > 1);
		DPRINTF("found index 0 to page %u", NODEPGNO(NODEPTR(mp, 0)));

		if (key == NULL)	/* Initialize cursor to first page. */
			i = 0;
		else {
			int	 exact;
			node = btree_search_node(bt, mp, key, &exact, &i);
			if (node == NULL)
				i = NUMKEYS(mp) - 1;
			else if (!exact) {
				assert(i > 0);
				i--;
			}
		}

		if (key)
			DPRINTF("following index %u for key %.*s",
			    i, (int)key->size, (char *)key->data);
		assert(i >= 0 && i < NUMKEYS(mp));
		node = NODEPTR(mp, i);

		if (cursor)
			CURSOR_TOP(cursor)->ki = i;

		parent = mp;
		if ((mp = btree_get_mpage(bt, NODEPGNO(node))) == NULL)
			return BT_FAIL;
		mp->parent = parent;
		mp->parent_index = i;
		find_common_prefix(bt, mp);

		if (cursor && cursor_push_page(cursor, mp) == NULL)
			return BT_FAIL;

		if (modify && (mp = mpage_touch(bt, mp)) == NULL)
			return BT_FAIL;
	}

	if (!IS_LEAF(mp)) {
		DPRINTF("internal error, index points to a %02X page!?",
		    mp->page->flags);
		return BT_FAIL;
	}

	DPRINTF("found leaf page %u for key %.*s", mp->pgno,
	    key ? (int)key->size : 0, key ? (char *)key->data : NULL);

	*mpp = mp;
	return BT_SUCCESS;
}

/* Search for the page a given key should be in.
 * Stores a pointer to the found page in *mpp.
 * If key is NULL, search for the lowest page (used by btree_cursor_first).
 * If cursor is non-null, pushes parent pages on the cursor stack.
 * If modify is true, visited pages are updated with new page numbers.
 */
static int
btree_search_page(struct btree *bt, struct btree_txn *txn, struct btval *key,
    struct cursor *cursor, int modify, struct mpage **mpp)
{
	int		 rc;
	pgno_t		 root;
	struct mpage	*mp;

	/* Can't modify pages outside a transaction. */
	if (txn == NULL && modify) {
		errno = EINVAL;
		return BT_FAIL;
	}

	/* Choose which root page to start with. If a transaction is given
         * use the root page from the transaction, otherwise read the last
         * committed root page.
	 */
	if (txn == NULL) {
		if ((rc = btree_read_meta(bt, NULL)) != BT_SUCCESS)
			return rc;
		root = bt->meta.root;
	} else if (F_ISSET(txn->flags, BT_TXN_ERROR)) {
		DPRINTF("transaction has failed, must abort");
		errno = EINVAL;
		return BT_FAIL;
	} else
		root = txn->root;

	if (root == P_INVALID) {		/* Tree is empty. */
		DPRINTF("tree is empty");
		errno = ENOENT;
		return BT_FAIL;
	}

	if ((mp = btree_get_mpage(bt, root)) == NULL)
		return BT_FAIL;

	DPRINTF("root page has flags 0x%X", mp->page->flags);

	assert(mp->parent == NULL);
	assert(mp->prefix.len == 0);

	if (modify && !mp->dirty) {
		if ((mp = mpage_touch(bt, mp)) == NULL)
			return BT_FAIL;
		txn->root = mp->pgno;
	}

	return btree_search_page_root(bt, mp, key, cursor, modify, mpp);
}

static int
btree_read_data(struct btree *bt, struct mpage *mp, struct node *leaf,
    struct btval *data)
{
	struct mpage	*omp;		/* overflow mpage */
	size_t		 psz;
	size_t		 max;
	size_t		 sz = 0;
	pgno_t		 pgno;

	memset(data, 0, sizeof(*data));
	max = bt->head.psize - PAGEHDRSZ;

	if (!F_ISSET(leaf->flags, F_BIGDATA)) {
		data->size = leaf->n_dsize;
		if (data->size > 0) {
			if (mp == NULL) {
				if ((data->data = malloc(data->size)) == NULL)
					return BT_FAIL;
				bcopy(NODEDATA(leaf), data->data, data->size);
				data->free_data = 1;
				data->mp = NULL;
			} else {
				data->data = NODEDATA(leaf);
				data->free_data = 0;
				data->mp = mp;
				mp->ref++;
			}
		}
		return BT_SUCCESS;
	}

	/* Read overflow data.
	 */
	DPRINTF("allocating %u byte for overflow data", leaf->n_dsize);
	if ((data->data = malloc(leaf->n_dsize)) == NULL)
		return BT_FAIL;
	data->size = leaf->n_dsize;
	data->free_data = 1;
	data->mp = NULL;
	bcopy(NODEDATA(leaf), &pgno, sizeof(pgno));
	for (sz = 0; sz < data->size; ) {
		if ((omp = btree_get_mpage(bt, pgno)) == NULL ||
		    !F_ISSET(omp->page->flags, P_OVERFLOW)) {
			DPRINTF("read overflow page %u failed", pgno);
			free(data->data);
			mpage_free(omp);
			return BT_FAIL;
		}
		psz = data->size - sz;
		if (psz > max)
			psz = max;
		bcopy(omp->page->ptrs, (char *)data->data + sz, psz);
		sz += psz;
		pgno = omp->page->p_next_pgno;
	}

	return BT_SUCCESS;
}

int
btree_txn_get(struct btree *bt, struct btree_txn *txn,
    struct btval *key, struct btval *data)
{
	int		 rc, exact;
	struct node	*leaf;
	struct mpage	*mp;

	assert(key);
	assert(data);
	DPRINTF("===> get key [%.*s]", (int)key->size, (char *)key->data);

	if (bt != NULL && txn != NULL && bt != txn->bt) {
		errno = EINVAL;
		return BT_FAIL;
	}

	if (bt == NULL) {
		if (txn == NULL) {
			errno = EINVAL;
			return BT_FAIL;
		}
		bt = txn->bt;
	}

	if (key->size == 0 || key->size > MAXKEYSIZE) {
		errno = EINVAL;
		return BT_FAIL;
	}

	if ((rc = btree_search_page(bt, txn, key, NULL, 0, &mp)) != BT_SUCCESS)
		return rc;

	leaf = btree_search_node(bt, mp, key, &exact, NULL);
	if (leaf && exact)
		rc = btree_read_data(bt, mp, leaf, data);
	else {
		errno = ENOENT;
		rc = BT_FAIL;
	}

	mpage_prune(bt);
	return rc;
}

static int
btree_sibling(struct cursor *cursor, int move_right)
{
	int		 rc;
	struct node	*indx;
	struct ppage	*parent, *top;
	struct mpage	*mp;

	top = CURSOR_TOP(cursor);
	if ((parent = SLIST_NEXT(top, entry)) == NULL) {
		errno = ENOENT;
		return BT_FAIL;			/* root has no siblings */
	}

	DPRINTF("parent page is page %u, index %u",
	    parent->mpage->pgno, parent->ki);

	cursor_pop_page(cursor);
	if (move_right ? (parent->ki + 1 >= NUMKEYS(parent->mpage))
		       : (parent->ki == 0)) {
		DPRINTF("no more keys left, moving to %s sibling",
		    move_right ? "right" : "left");
		if ((rc = btree_sibling(cursor, move_right)) != BT_SUCCESS)
			return rc;
		parent = CURSOR_TOP(cursor);
	} else {
		if (move_right)
			parent->ki++;
		else
			parent->ki--;
		DPRINTF("just moving to %s index key %u",
		    move_right ? "right" : "left", parent->ki);
	}
	assert(IS_BRANCH(parent->mpage));

	indx = NODEPTR(parent->mpage, parent->ki);
	if ((mp = btree_get_mpage(cursor->bt, indx->n_pgno)) == NULL)
		return BT_FAIL;
	mp->parent = parent->mpage;
	mp->parent_index = parent->ki;

	cursor_push_page(cursor, mp);
	find_common_prefix(cursor->bt, mp);

	return BT_SUCCESS;
}

static int
bt_set_key(struct btree *bt, struct mpage *mp, struct node *node,
    struct btval *key)
{
	if (key == NULL)
		return 0;

	if (mp->prefix.len > 0) {
		key->size = node->ksize + mp->prefix.len;
		key->data = malloc(key->size);
		if (key->data == NULL)
			return -1;
		concat_prefix(bt,
		    mp->prefix.str, mp->prefix.len,
		    NODEKEY(node), node->ksize,
		    key->data, &key->size);
		key->free_data = 1;
	} else {
		key->size = node->ksize;
		key->data = NODEKEY(node);
		key->free_data = 0;
		key->mp = mp;
		mp->ref++;
	}

	return 0;
}

static int
btree_cursor_next(struct cursor *cursor, struct btval *key, struct btval *data)
{
	struct ppage	*top;
	struct mpage	*mp;
	struct node	*leaf;

	if (cursor->eof) {
		errno = ENOENT;
		return BT_FAIL;
	}

	assert(cursor->initialized);

	top = CURSOR_TOP(cursor);
	mp = top->mpage;

	DPRINTF("cursor_next: top page is %u in cursor %p", mp->pgno, cursor);

	if (top->ki + 1 >= NUMKEYS(mp)) {
		DPRINTF("=====> move to next sibling page");
		if (btree_sibling(cursor, 1) != BT_SUCCESS) {
			cursor->eof = 1;
			return BT_FAIL;
		}
		top = CURSOR_TOP(cursor);
		mp = top->mpage;
		DPRINTF("next page is %u, key index %u", mp->pgno, top->ki);
	} else
		top->ki++;

	DPRINTF("==> cursor points to page %u with %lu keys, key index %u",
	    mp->pgno, NUMKEYS(mp), top->ki);

	assert(IS_LEAF(mp));
	leaf = NODEPTR(mp, top->ki);

	if (data && btree_read_data(cursor->bt, mp, leaf, data) != BT_SUCCESS)
		return BT_FAIL;

	if (bt_set_key(cursor->bt, mp, leaf, key) != 0)
		return BT_FAIL;

	return BT_SUCCESS;
}

static int
btree_cursor_set(struct cursor *cursor, struct btval *key, struct btval *data,
    int *exactp)
{
	int		 rc;
	struct node	*leaf;
	struct mpage	*mp;
	struct ppage	*top;

	assert(cursor);
	assert(key);
	assert(key->size > 0);

	rc = btree_search_page(cursor->bt, cursor->txn, key, cursor, 0, &mp);
	if (rc != BT_SUCCESS)
		return rc;
	assert(IS_LEAF(mp));

	top = CURSOR_TOP(cursor);
	leaf = btree_search_node(cursor->bt, mp, key, exactp, &top->ki);
	if (exactp != NULL && !*exactp) {
		/* BT_CURSOR_EXACT specified and not an exact match. */
		errno = ENOENT;
		return BT_FAIL;
	}

	if (leaf == NULL) {
		DPRINTF("===> inexact leaf not found, goto sibling");
		if (btree_sibling(cursor, 1) != BT_SUCCESS)
			return BT_FAIL;		/* no entries matched */
		top = CURSOR_TOP(cursor);
		top->ki = 0;
		mp = top->mpage;
		assert(IS_LEAF(mp));
		leaf = NODEPTR(mp, 0);
	}

	cursor->initialized = 1;
	cursor->eof = 0;

	if (data && btree_read_data(cursor->bt, mp, leaf, data) != BT_SUCCESS)
		return BT_FAIL;

	if (bt_set_key(cursor->bt, mp, leaf, key) != 0)
		return BT_FAIL;
	DPRINTF("==> cursor placed on key %.*s",
	    (int)key->size, (char *)key->data);

	return BT_SUCCESS;
}

static int
btree_cursor_first(struct cursor *cursor, struct btval *key, struct btval *data)
{
	int		 rc;
	struct mpage	*mp;
	struct node	*leaf;

	rc = btree_search_page(cursor->bt, cursor->txn, NULL, cursor, 0, &mp);
	if (rc != BT_SUCCESS)
		return rc;
	assert(IS_LEAF(mp));

	leaf = NODEPTR(mp, 0);
	cursor->initialized = 1;
	cursor->eof = 0;

	if (data && btree_read_data(cursor->bt, mp, leaf, data) != BT_SUCCESS)
		return BT_FAIL;

	if (bt_set_key(cursor->bt, mp, leaf, key) != 0)
		return BT_FAIL;

	return BT_SUCCESS;
}

int
btree_cursor_get(struct cursor *cursor, struct btval *key, struct btval *data,
    enum cursor_op op)
{
	int		 rc;
	int		 exact = 0;

	assert(cursor);

	switch (op) {
	case BT_CURSOR:
	case BT_CURSOR_EXACT:
		while (CURSOR_TOP(cursor) != NULL)
			cursor_pop_page(cursor);
		if (key == NULL || key->size == 0 || key->size > MAXKEYSIZE) {
			errno = EINVAL;
			rc = BT_FAIL;
		} else if (op == BT_CURSOR_EXACT)
			rc = btree_cursor_set(cursor, key, data, &exact);
		else
			rc = btree_cursor_set(cursor, key, data, NULL);
		break;
	case BT_NEXT:
		if (!cursor->initialized)
			rc = btree_cursor_first(cursor, key, data);
		else
			rc = btree_cursor_next(cursor, key, data);
		break;
	case BT_FIRST:
		while (CURSOR_TOP(cursor) != NULL)
			cursor_pop_page(cursor);
		rc = btree_cursor_first(cursor, key, data);
		break;
	default:
		DPRINTF("unhandled/unimplemented cursor operation %u", op);
		rc = BT_FAIL;
		break;
	}

	mpage_prune(cursor->bt);

	return rc;
}

static struct mpage *
btree_new_page(struct btree *bt, uint32_t flags)
{
	struct mpage	*mp;

	assert(bt != NULL);
	assert(bt->txn != NULL);

	DPRINTF("allocating new mpage %u, page size %u",
	    bt->txn->next_pgno, bt->head.psize);
	if ((mp = calloc(1, sizeof(*mp))) == NULL)
		return NULL;
	if ((mp->page = malloc(bt->head.psize)) == NULL) {
		free(mp);
		return NULL;
	}
	mp->pgno = mp->page->pgno = bt->txn->next_pgno++;
	mp->page->flags = flags;
	mp->page->lower = PAGEHDRSZ;
	mp->page->upper = bt->head.psize;

	if (IS_BRANCH(mp))
		bt->meta.branch_pages++;
	else if (IS_LEAF(mp))
		bt->meta.leaf_pages++;
	else if (IS_OVERFLOW(mp))
		bt->meta.overflow_pages++;

	mpage_add(bt, mp);
	mpage_dirty(bt, mp);

	return mp;
}

static size_t
bt_leaf_size(struct btree *bt, struct btval *key, struct btval *data)
{
	size_t		 sz;

	sz = LEAFSIZE(key, data);
	if (data->size >= bt->head.psize / BT_MINKEYS) {
		/* put on overflow page */
		sz -= data->size - sizeof(pgno_t);
	}

	return sz + sizeof(indx_t);
}

static size_t
bt_branch_size(struct btree *bt, struct btval *key)
{
	size_t		 sz;

	sz = INDXSIZE(key);
	if (sz >= bt->head.psize / BT_MINKEYS) {
		/* put on overflow page */
		/* not implemented */
		/* sz -= key->size - sizeof(pgno_t); */
	}

	return sz + sizeof(indx_t);
}

static int
btree_write_overflow_data(struct btree *bt, struct page *p, struct btval *data)
{
	size_t		 done = 0;
	size_t		 sz;
	size_t		 max;
	pgno_t		*linkp;			/* linked page stored here */
	struct mpage	*next = NULL;

	max = bt->head.psize - PAGEHDRSZ;

	while (done < data->size) {
		if (next != NULL)
			p = next->page;
		linkp = &p->p_next_pgno;
		if (data->size - done > max) {
			/* need another overflow page */
			if ((next = btree_new_page(bt, P_OVERFLOW)) == NULL)
				return BT_FAIL;
			*linkp = next->pgno;
			DPRINTF("linking overflow page %u", next->pgno);
		} else
			*linkp = 0;		/* indicates end of list */
		sz = data->size - done;
		if (sz > max)
			sz = max;
		DPRINTF("copying %zu bytes to overflow page %u", sz, p->pgno);
		bcopy((char *)data->data + done, p->ptrs, sz);
		done += sz;
	}

	return BT_SUCCESS;
}

/* Key prefix should already be stripped.
 */
static int
btree_add_node(struct btree *bt, struct mpage *mp, indx_t indx,
    struct btval *key, struct btval *data, pgno_t pgno, uint8_t flags)
{
	unsigned int	 i;
	size_t		 node_size = NODESIZE;
	indx_t		 ofs;
	struct node	*node;
	struct page	*p;
	struct mpage	*ofp = NULL;		/* overflow page */

	p = mp->page;
	assert(p->upper >= p->lower);

	DPRINTF("add node [%.*s] to %s page %u at index %d, key size %zu",
	    key ? (int)key->size : 0, key ? (char *)key->data : NULL,
	    IS_LEAF(mp) ? "leaf" : "branch",
	    mp->pgno, indx, key ? key->size : 0);

	if (key != NULL)
		node_size += key->size;

	if (IS_LEAF(mp)) {
		assert(data);
		node_size += data->size;
		if (F_ISSET(flags, F_BIGDATA)) {
			/* Data already on overflow page. */
			node_size -= data->size - sizeof(pgno_t);
		} else if (data->size >= bt->head.psize / BT_MINKEYS) {
			/* Put data on overflow page. */
			DPRINTF("data size is %zu, put on overflow page",
			    data->size);
			node_size -= data->size - sizeof(pgno_t);
			if ((ofp = btree_new_page(bt, P_OVERFLOW)) == NULL)
				return BT_FAIL;
			DPRINTF("allocated overflow page %u", ofp->pgno);
			flags |= F_BIGDATA;
		}
	}

	if (node_size + sizeof(indx_t) > SIZELEFT(mp)) {
		DPRINTF("not enough room in page %u, got %lu ptrs",
		    mp->pgno, NUMKEYS(mp));
		DPRINTF("upper - lower = %u - %u = %u", p->upper, p->lower,
		    p->upper - p->lower);
		DPRINTF("node size = %zu", node_size);
		return BT_FAIL;
	}

	/* Move higher pointers up one slot. */
	for (i = NUMKEYS(mp); i > indx; i--)
		p->ptrs[i] = p->ptrs[i - 1];

	/* Adjust free space offsets. */
	ofs = p->upper - node_size;
	assert(ofs >= p->lower + sizeof(indx_t));
	p->ptrs[indx] = ofs;
	p->upper = ofs;
	p->lower += sizeof(indx_t);

	/* Write the node data. */
	node = NODEPTR(mp, indx);
	node->ksize = (key == NULL) ? 0 : key->size;
	node->flags = flags;
	if (IS_LEAF(mp))
		node->n_dsize = data->size;
	else
		node->n_pgno = pgno;

	if (key)
		bcopy(key->data, NODEKEY(node), key->size);

	if (IS_LEAF(mp)) {
		assert(key);
		if (ofp == NULL) {
			if (F_ISSET(flags, F_BIGDATA))
				bcopy(data->data, node->data + key->size,
				    sizeof(pgno_t));
			else
				bcopy(data->data, node->data + key->size,
				    data->size);
		} else {
			bcopy(&ofp->pgno, node->data + key->size,
			    sizeof(pgno_t));
			if (btree_write_overflow_data(bt, ofp->page,
			    data) == BT_FAIL)
				return BT_FAIL;
		}
	}

	return BT_SUCCESS;
}

static void
btree_del_node(struct btree *bt, struct mpage *mp, indx_t indx)
{
	unsigned int	 sz;
	indx_t		 i, j, numkeys, ptr;
	struct node	*node;
	char		*base;

	DPRINTF("delete node %u on %s page %u", indx,
	    IS_LEAF(mp) ? "leaf" : "branch", mp->pgno);
	assert(indx < NUMKEYS(mp));

	node = NODEPTR(mp, indx);
	sz = NODESIZE + node->ksize;
	if (IS_LEAF(mp)) {
		if (F_ISSET(node->flags, F_BIGDATA))
			sz += sizeof(pgno_t);
		else
			sz += NODEDSZ(node);
	}

	ptr = mp->page->ptrs[indx];
	numkeys = NUMKEYS(mp);
	for (i = j = 0; i < numkeys; i++) {
		if (i != indx) {
			mp->page->ptrs[j] = mp->page->ptrs[i];
			if (mp->page->ptrs[i] < ptr)
				mp->page->ptrs[j] += sz;
			j++;
		}
	}

	base = (char *)mp->page + mp->page->upper;
	bcopy(base, base + sz, ptr - mp->page->upper);

	mp->page->lower -= sizeof(indx_t);
	mp->page->upper += sz;
}

struct cursor *
btree_txn_cursor_open(struct btree *bt, struct btree_txn *txn)
{
	struct cursor	*cursor;

	if (bt != NULL && txn != NULL && bt != txn->bt) {
		errno = EINVAL;
		return NULL;
	}

	if (bt == NULL) {
		if (txn == NULL) {
			errno = EINVAL;
			return NULL;
		}
		bt = txn->bt;
	}

	if ((cursor = calloc(1, sizeof(*cursor))) != NULL) {
		SLIST_INIT(&cursor->stack);
		cursor->bt = bt;
		cursor->txn = txn;
		btree_ref(bt);
	}

	return cursor;
}

void
btree_cursor_close(struct cursor *cursor)
{
	if (cursor != NULL) {
		while (!CURSOR_EMPTY(cursor))
			cursor_pop_page(cursor);

		btree_close(cursor->bt);
		free(cursor);
	}
}

static int
btree_update_key(struct btree *bt, struct mpage *mp, indx_t indx,
    struct btval *key)
{
	indx_t			 ptr, i, numkeys;
	int			 delta;
	size_t			 len;
	struct node		*node;
	char			*base;

	node = NODEPTR(mp, indx);
	ptr = mp->page->ptrs[indx];
	DPRINTF("update key %u (ofs %u) [%.*s] to [%.*s] on page %u",
	    indx, ptr,
	    (int)node->ksize, (char *)NODEKEY(node),
	    (int)key->size, (char *)key->data,
	    mp->pgno);

	if (key->size != node->ksize) {
		delta = key->size - node->ksize;
		if (delta > 0 && SIZELEFT(mp) < delta) {
			DPRINTF("OUCH! Not enough room, delta = %d", delta);
			return BT_FAIL;
		}

		numkeys = NUMKEYS(mp);
		for (i = 0; i < numkeys; i++) {
			if (mp->page->ptrs[i] <= ptr)
				mp->page->ptrs[i] -= delta;
		}

		base = (char *)mp->page + mp->page->upper;
		len = ptr - mp->page->upper + NODESIZE;
		bcopy(base, base - delta, len);
		mp->page->upper -= delta;

		node = NODEPTR(mp, indx);
		node->ksize = key->size;
	}

	bcopy(key->data, NODEKEY(node), key->size);

	return BT_SUCCESS;
}

static int
btree_adjust_prefix(struct btree *bt, struct mpage *src, int delta)
{
	indx_t		 i;
	struct node	*node;
	struct btkey	 tmpkey;
	struct btval	 key;

	DPRINTF("adjusting prefix lengths on page %u with delta %d",
	    src->pgno, delta);
	assert(delta != 0);

	for (i = 0; i < NUMKEYS(src); i++) {
		node = NODEPTR(src, i);
		tmpkey.len = node->ksize - delta;
		if (delta > 0) {
			if (F_ISSET(bt->flags, BT_REVERSEKEY))
				bcopy(NODEKEY(node), tmpkey.str, tmpkey.len);
			else
				bcopy((char *)NODEKEY(node) + delta, tmpkey.str,
				    tmpkey.len);
		} else {
			if (F_ISSET(bt->flags, BT_REVERSEKEY)) {
				bcopy(NODEKEY(node), tmpkey.str, node->ksize);
				bcopy(src->prefix.str, tmpkey.str + node->ksize,
				    -delta);
			} else {
				bcopy(src->prefix.str + src->prefix.len + delta,
				    tmpkey.str, -delta);
				bcopy(NODEKEY(node), tmpkey.str - delta,
				    node->ksize);
			}
		}
		key.size = tmpkey.len;
		key.data = tmpkey.str;
		if (btree_update_key(bt, src, i, &key) != BT_SUCCESS)
			return BT_FAIL;
	}

	return BT_SUCCESS;
}

/* Move a node from src to dst.
 */
static int
btree_move_node(struct btree *bt, struct mpage *src, indx_t srcindx,
    struct mpage *dst, indx_t dstindx)
{
	int			 rc;
	unsigned int		 pfxlen, mp_pfxlen = 0;
	struct node		*srcnode;
	struct mpage		*mp = NULL;
	struct btkey		 tmpkey, srckey;
	struct btval		 key, data;

	assert(src->parent);
	assert(dst->parent);

	srcnode = NODEPTR(src, srcindx);
	DPRINTF("moving %s node %u [%.*s] on page %u to node %u on page %u",
	    IS_LEAF(src) ? "leaf" : "branch",
	    srcindx,
	    (int)srcnode->ksize, (char *)NODEKEY(srcnode),
	    src->pgno,
	    dstindx, dst->pgno);

	find_common_prefix(bt, src);

	if (IS_BRANCH(src)) {
		/* Need to check if the page the moved node points to
		 * changes prefix.
		 */
		if ((mp = btree_get_mpage(bt, NODEPGNO(srcnode))) == NULL)
			return BT_FAIL;
		mp->parent = src;
		mp->parent_index = srcindx;
		find_common_prefix(bt, mp);
		mp_pfxlen = mp->prefix.len;
	}

	/* Mark src and dst as dirty. */
	if ((src = mpage_touch(bt, src)) == NULL ||
	    (dst = mpage_touch(bt, dst)) == NULL)
		return BT_FAIL;

	find_common_prefix(bt, dst);

	/* Check if src node has destination page prefix. Otherwise the
	 * destination page must expand its prefix on all its nodes.
	 */
	srckey.len = srcnode->ksize;
	bcopy(NODEKEY(srcnode), srckey.str, srckey.len);
	common_prefix(bt, &srckey, &dst->prefix, &tmpkey);
	if (tmpkey.len != dst->prefix.len) {
		if (btree_adjust_prefix(bt, dst,
		    tmpkey.len - dst->prefix.len) != BT_SUCCESS)
			return BT_FAIL;
		bcopy(&tmpkey, &dst->prefix, sizeof(tmpkey));
	}

	if (srcindx == 0 && IS_BRANCH(src)) {
		struct mpage	*low;

		/* must find the lowest key below src
		 */
		assert(btree_search_page_root(bt, src, NULL, NULL, 0,
		    &low) == BT_SUCCESS);
		expand_prefix(bt, low, 0, &srckey);
		DPRINTF("found lowest key [%.*s] on leaf page %u",
		    (int)srckey.len, srckey.str, low->pgno);
	} else {
		srckey.len = srcnode->ksize;
		bcopy(NODEKEY(srcnode), srckey.str, srcnode->ksize);
	}
	find_common_prefix(bt, src);

	/* expand the prefix */
	tmpkey.len = sizeof(tmpkey.str);
	concat_prefix(bt, src->prefix.str, src->prefix.len,
	    srckey.str, srckey.len, tmpkey.str, &tmpkey.len);

	/* Add the node to the destination page. Adjust prefix for
	 * destination page.
	 */
	key.size = tmpkey.len;
	key.data = tmpkey.str;
	remove_prefix(bt, &key, dst->prefix.len);
	data.size = NODEDSZ(srcnode);
	data.data = NODEDATA(srcnode);
	rc = btree_add_node(bt, dst, dstindx, &key, &data, NODEPGNO(srcnode),
	    srcnode->flags);
	if (rc != BT_SUCCESS)
		return rc;

	/* Delete the node from the source page.
	 */
	btree_del_node(bt, src, srcindx);

	/* Update the parent separators.
	 */
	if (srcindx == 0 && src->parent_index != 0) {
		expand_prefix(bt, src, 0, &tmpkey);
		key.size = tmpkey.len;
		key.data = tmpkey.str;
		remove_prefix(bt, &key, src->parent->prefix.len);

		DPRINTF("update separator for source page %u to [%.*s]",
		    src->pgno, (int)key.size, (char *)key.data);
		if (btree_update_key(bt, src->parent, src->parent_index,
		    &key) != BT_SUCCESS)
			return BT_FAIL;
	}

	if (srcindx == 0 && IS_BRANCH(src)) {
		struct btval	 nullkey;
		nullkey.size = 0;
		assert(btree_update_key(bt, src, 0, &nullkey) == BT_SUCCESS);
	}

	if (dstindx == 0 && dst->parent_index != 0) {
		expand_prefix(bt, dst, 0, &tmpkey);
		key.size = tmpkey.len;
		key.data = tmpkey.str;
		remove_prefix(bt, &key, dst->parent->prefix.len);

		DPRINTF("update separator for destination page %u to [%.*s]",
		    dst->pgno, (int)key.size, (char *)key.data);
		if (btree_update_key(bt, dst->parent, dst->parent_index,
		    &key) != BT_SUCCESS)
			return BT_FAIL;
	}

	if (dstindx == 0 && IS_BRANCH(dst)) {
		struct btval	 nullkey;
		nullkey.size = 0;
		assert(btree_update_key(bt, dst, 0, &nullkey) == BT_SUCCESS);
	}

	/* We can get a new page prefix here!
	 * Must update keys in all nodes of this page!
	 */
	pfxlen = src->prefix.len;
	find_common_prefix(bt, src);
	if (src->prefix.len != pfxlen) {
		if (btree_adjust_prefix(bt, src,
		    src->prefix.len - pfxlen) != BT_SUCCESS)
			return BT_FAIL;
	}

	pfxlen = dst->prefix.len;
	find_common_prefix(bt, dst);
	if (dst->prefix.len != pfxlen) {
		if (btree_adjust_prefix(bt, dst,
		    dst->prefix.len - pfxlen) != BT_SUCCESS)
			return BT_FAIL;
	}

	if (IS_BRANCH(dst)) {
		assert(mp);
		mp->parent = dst;
		mp->parent_index = dstindx;
		find_common_prefix(bt, mp);
		if (mp->prefix.len != mp_pfxlen) {
			DPRINTF("moved branch node has changed prefix");
			if ((mp = mpage_touch(bt, mp)) == NULL)
				return BT_FAIL;
			if (btree_adjust_prefix(bt, mp,
			    mp->prefix.len - mp_pfxlen) != BT_SUCCESS)
				return BT_FAIL;
		}
	}

	return BT_SUCCESS;
}

static int
btree_merge(struct btree *bt, struct mpage *src, struct mpage *dst)
{
	int			 rc;
	indx_t			 i;
	unsigned int		 pfxlen;
	struct node		*srcnode;
	struct btkey		 tmpkey, dstpfx;
	struct btval		 key, data;

	DPRINTF("merging page %u and %u", src->pgno, dst->pgno);

	assert(src->parent);	/* can't merge root page */
	assert(dst->parent);
	assert(bt->txn != NULL);

	/* Mark src and dst as dirty. */
	if ((src = mpage_touch(bt, src)) == NULL ||
	    (dst = mpage_touch(bt, dst)) == NULL)
		return BT_FAIL;

	find_common_prefix(bt, src);
	find_common_prefix(bt, dst);

	/* Check if source nodes has destination page prefix. Otherwise
	 * the destination page must expand its prefix on all its nodes.
	 */
	common_prefix(bt, &src->prefix, &dst->prefix, &dstpfx);
	if (dstpfx.len != dst->prefix.len) {
		if (btree_adjust_prefix(bt, dst,
		    dstpfx.len - dst->prefix.len) != BT_SUCCESS)
			return BT_FAIL;
		bcopy(&dstpfx, &dst->prefix, sizeof(dstpfx));
	}

	/* Move all nodes from src to dst.
	 */
	for (i = 0; i < NUMKEYS(src); i++) {
		srcnode = NODEPTR(src, i);

		/* If branch node 0 (implicit key), find the real key.
		 */
		if (i == 0 && IS_BRANCH(src)) {
			struct mpage	*low;

			/* must find the lowest key below src
			 */
			assert(btree_search_page_root(bt, src, NULL, NULL, 0,
			    &low) == BT_SUCCESS);
			expand_prefix(bt, low, 0, &tmpkey);
			DPRINTF("found lowest key [%.*s] on leaf page %u",
			    (int)tmpkey.len, tmpkey.str, low->pgno);
		} else {
			expand_prefix(bt, src, i, &tmpkey);
		}

		key.size = tmpkey.len;
		key.data = tmpkey.str;

		remove_prefix(bt, &key, dst->prefix.len);
		data.size = NODEDSZ(srcnode);
		data.data = NODEDATA(srcnode);
		rc = btree_add_node(bt, dst, NUMKEYS(dst), &key,
		    &data, NODEPGNO(srcnode), srcnode->flags);
		if (rc != BT_SUCCESS)
			return rc;
	}

	DPRINTF("dst page %u now has %lu keys (%.1f%% filled)",
	    dst->pgno, NUMKEYS(dst), (float)PAGEFILL(bt, dst) / 10);

	/* Unlink the src page from parent.
	 */
	btree_del_node(bt, src->parent, src->parent_index);
	if (src->parent_index == 0) {
		key.size = 0;
		if (btree_update_key(bt, src->parent, 0, &key) != BT_SUCCESS)
			return BT_FAIL;

		pfxlen = src->prefix.len;
		find_common_prefix(bt, src);
		assert (src->prefix.len == pfxlen);
	}

	if (IS_LEAF(src))
		bt->meta.leaf_pages--;
	else
		bt->meta.branch_pages--;

	return btree_rebalance(bt, src->parent);
}

#define FILL_THRESHOLD	 250

static int
btree_rebalance(struct btree *bt, struct mpage *mp)
{
	struct node	*node;
	struct mpage	*parent;
	struct mpage	*root;
	struct mpage	*neighbor = NULL;
	indx_t		 si = 0, di = 0;

	assert(bt != NULL);
	assert(bt->txn != NULL);
	assert(mp != NULL);

	DPRINTF("rebalancing %s page %u (has %lu keys, %.1f%% full)",
	    IS_LEAF(mp) ? "leaf" : "branch",
	    mp->pgno, NUMKEYS(mp), (float)PAGEFILL(bt, mp) / 10);

	if (PAGEFILL(bt, mp) >= FILL_THRESHOLD) {
		DPRINTF("no need to rebalance page %u, above fill threshold",
		    mp->pgno);
		return BT_SUCCESS;
	}

	parent = mp->parent;

	if (parent == NULL) {
		if (NUMKEYS(mp) == 0) {
			DPRINTF("tree is completely empty");
			bt->txn->root = P_INVALID;
			bt->meta.depth--;
			bt->meta.leaf_pages--;
		} else if (IS_BRANCH(mp) && NUMKEYS(mp) == 1) {
			DPRINTF("collapsing root page!");
			bt->txn->root = NODEPGNO(NODEPTR(mp, 0));
			if ((root = btree_get_mpage(bt, bt->txn->root)) == NULL)
				return BT_FAIL;
			root->parent = NULL;
			bt->meta.depth--;
			bt->meta.branch_pages--;
		} else
			DPRINTF("root page doesn't need rebalancing");
		return BT_SUCCESS;
	}

	/* The parent (branch page) must have at least 2 pointers,
	 * otherwise the tree is invalid.
	 */
	assert(NUMKEYS(parent) > 1);

	/* Leaf page fill factor is below the threshold.
	 * Try to move keys from left or right neighbor, or
	 * merge with a neighbor page.
	 */

	/* Find neighbors.
	 */
	if (mp->parent_index == 0) {
		/* We're the leftmost leaf in our parent.
		 */
		DPRINTF("reading right neighbor");
		node = NODEPTR(parent, mp->parent_index + 1);
		if ((neighbor = btree_get_mpage(bt, NODEPGNO(node))) == NULL)
			return BT_FAIL;
		neighbor->parent_index = mp->parent_index + 1;
		si = 0;
		di = NUMKEYS(mp);
	} else {
		/* There is at least one neighbor to the left.
		 */
		DPRINTF("reading left neighbor");
		node = NODEPTR(parent, mp->parent_index - 1);
		if ((neighbor = btree_get_mpage(bt, NODEPGNO(node))) == NULL)
			return BT_FAIL;
		neighbor->parent_index = mp->parent_index - 1;
		si = NUMKEYS(neighbor) - 1;
		di = 0;
	}
	neighbor->parent = parent;

	DPRINTF("found neighbor page %u (%lu keys, %.1f%% full)",
	    neighbor->pgno, NUMKEYS(neighbor), (float)PAGEFILL(bt, neighbor) / 10);

	/* If the neighbor page is above threshold and has at least two
	 * keys, move one key from it.
	 *
	 * Otherwise we should try to merge them, but that might not be
	 * possible, even if both are below threshold, as prefix expansion
	 * might make keys larger. FIXME: detect this
	 */
	if (PAGEFILL(bt, neighbor) >= FILL_THRESHOLD && NUMKEYS(neighbor) >= 2)
		return btree_move_node(bt, neighbor, si, mp, di);
	else { /* FIXME: if (has_enough_room()) */
		if (mp->parent_index == 0)
			return btree_merge(bt, neighbor, mp);
		else
			return btree_merge(bt, mp, neighbor);
	}
}

int
btree_txn_del(struct btree *bt, struct btree_txn *txn,
    struct btval *key, struct btval *data)
{
	int		 rc, exact, close_txn = 0;
	unsigned int	 ki;
	struct node	*leaf;
	struct mpage	*mp;

	DPRINTF("========> delete key %.*s", (int)key->size, (char *)key->data);

	assert(key != NULL);

	if (bt != NULL && txn != NULL && bt != txn->bt) {
		errno = EINVAL;
		return BT_FAIL;
	}

	if (txn != NULL && F_ISSET(txn->flags, BT_TXN_RDONLY)) {
		errno = EINVAL;
		return BT_FAIL;
	}

	if (bt == NULL) {
		if (txn == NULL) {
			errno = EINVAL;
			return BT_FAIL;
		}
		bt = txn->bt;
	}

	if (key->size == 0 || key->size > MAXKEYSIZE) {
		errno = EINVAL;
		return BT_FAIL;
	}

	if (txn == NULL) {
		close_txn = 1;
		if ((txn = btree_txn_begin(bt, 0)) == NULL)
			return BT_FAIL;
	}

	if ((rc = btree_search_page(bt, txn, key, NULL, 1, &mp)) != BT_SUCCESS)
		goto done;

	leaf = btree_search_node(bt, mp, key, &exact, &ki);
	if (leaf == NULL || !exact) {
		errno = ENOENT;
		rc = BT_FAIL;
		goto done;
	}

	if (data && (rc = btree_read_data(bt, NULL, leaf, data)) != BT_SUCCESS)
		goto done;

	btree_del_node(bt, mp, ki);
	bt->meta.entries--;
	rc = btree_rebalance(bt, mp);
	if (rc != BT_SUCCESS)
		txn->flags |= BT_TXN_ERROR;

done:
	if (close_txn) {
		if (rc == BT_SUCCESS)
			rc = btree_txn_commit(txn);
		else
			btree_txn_abort(txn);
	}
	mpage_prune(bt);
	return rc;
}

/* Reduce the length of the prefix separator <sep> to the minimum length that
 * still makes it uniquely distinguishable from <min>.
 *
 * <min> is guaranteed to be sorted less than <sep>
 *
 * On return, <sep> is modified to the minimum length.
 */
static void
bt_reduce_separator(struct btree *bt, struct node *min, struct btval *sep)
{
	size_t		 n = 0;
	char		*p1;
	char		*p2;

	if (F_ISSET(bt->flags, BT_REVERSEKEY)) {

		assert(sep->size > 0);

		p1 = (char *)NODEKEY(min) + min->ksize - 1;
		p2 = (char *)sep->data + sep->size - 1;

		while (p1 >= (char *)NODEKEY(min) && *p1 == *p2) {
			assert(p2 > (char *)sep->data);
			p1--;
			p2--;
			n++;
		}

		sep->data = p2;
		sep->size = n + 1;
	} else {

		assert(min->ksize > 0);
		assert(sep->size > 0);

		p1 = (char *)NODEKEY(min);
		p2 = (char *)sep->data;

		while (*p1 == *p2) {
			p1++;
			p2++;
			n++;
			if (n == min->ksize || n == sep->size)
				break;
		}

		sep->size = n + 1;
	}

	DPRINTF("reduced separator to [%.*s] > [%.*s]",
	    (int)sep->size, (char *)sep->data,
	    (int)min->ksize, (char *)NODEKEY(min));
}

/* Split page <*mpp>, and insert <key,(data|newpgno)> in either left or
 * right sibling, at index <*newindxp> (as if unsplit). Updates *mpp and
 * *newindxp with the actual values after split, ie if *mpp and *newindxp
 * refer to a node in the new right sibling page.
 */
static int
btree_split(struct btree *bt, struct mpage **mpp, unsigned int *newindxp,
    struct btval *newkey, struct btval *newdata, pgno_t newpgno)
{
	uint8_t		 flags;
	int		 rc = BT_SUCCESS, ins_new = 0;
	indx_t		 newindx;
	pgno_t		 pgno = 0;
	size_t		 orig_pfx_len, left_pfx_diff, right_pfx_diff, pfx_diff;
	unsigned int	 i, j, split_indx;
	struct node	*node;
	struct mpage	*pright, *p, *mp;
	struct btval	 sepkey, rkey, rdata;
	struct btkey	 tmpkey;
	struct page	*copy;

	assert(bt != NULL);
	assert(bt->txn != NULL);

	mp = *mpp;
	newindx = *newindxp;

	DPRINTF("-----> splitting %s page %u and adding [%.*s] at index %d",
	    IS_LEAF(mp) ? "leaf" : "branch", mp->pgno,
	    (int)newkey->size, (char *)newkey->data, *newindxp);
	DPRINTF("page %u has prefix [%.*s]", mp->pgno,
	    (int)mp->prefix.len, (char *)mp->prefix.str);
	orig_pfx_len = mp->prefix.len;

	if (mp->parent == NULL) {
		if ((mp->parent = btree_new_page(bt, P_BRANCH)) == NULL)
			return BT_FAIL;
		mp->parent_index = 0;
		bt->txn->root = mp->parent->pgno;
		DPRINTF("root split! new root = %u", mp->parent->pgno);
		bt->meta.depth++;

		/* Add left (implicit) pointer. */
		if (btree_add_node(bt, mp->parent, 0, NULL, NULL,
		    mp->pgno, 0) != BT_SUCCESS)
			return BT_FAIL;
	} else {
		DPRINTF("parent branch page is %u", mp->parent->pgno);
	}

	/* Create a right sibling. */
	if ((pright = btree_new_page(bt, mp->page->flags)) == NULL)
		return BT_FAIL;
	pright->parent = mp->parent;
	pright->parent_index = mp->parent_index + 1;
	DPRINTF("new right sibling: page %u", pright->pgno);

	/* Move half of the keys to the right sibling. */
	if ((copy = malloc(bt->head.psize)) == NULL)
		return BT_FAIL;
	bcopy(mp->page, copy, bt->head.psize);
	assert(mp->ref == 0);				/* XXX */
	memset(&mp->page->ptrs, 0, bt->head.psize - PAGEHDRSZ);
	mp->page->lower = PAGEHDRSZ;
	mp->page->upper = bt->head.psize;

	split_indx = NUMKEYSP(copy) / 2 + 1;

	/* First find the separating key between the split pages.
	 */
	memset(&sepkey, 0, sizeof(sepkey));
	if (newindx == split_indx) {
		sepkey.size = newkey->size;
		sepkey.data = newkey->data;
		remove_prefix(bt, &sepkey, mp->prefix.len);
	} else {
		node = NODEPTRP(copy, split_indx);
		sepkey.size = node->ksize;
		sepkey.data = NODEKEY(node);
	}

	if (IS_LEAF(mp) && bt->cmp == NULL) {
		/* Find the smallest separator. */
		/* Ref: Prefix B-trees, R. Bayer, K. Unterauer, 1977 */
		node = NODEPTRP(copy, split_indx - 1);
		bt_reduce_separator(bt, node, &sepkey);
	}

	/* Fix separator wrt parent prefix. */
	if (bt->cmp == NULL) {
		tmpkey.len = sizeof(tmpkey.str);
		concat_prefix(bt, mp->prefix.str, mp->prefix.len,
		    sepkey.data, sepkey.size, tmpkey.str, &tmpkey.len);
		sepkey.data = tmpkey.str;
		sepkey.size = tmpkey.len;
	}

	DPRINTF("separator is [%.*s]", (int)sepkey.size, (char *)sepkey.data);

	/* Copy separator key to the parent.
	 */
	if (SIZELEFT(pright->parent) < bt_branch_size(bt, &sepkey)) {
		rc = btree_split(bt, &pright->parent, &pright->parent_index,
		    &sepkey, NULL, pright->pgno);

		/* Right page might now have changed parent.
		 * Check if left page also changed parent.
		 */
		if (pright->parent != mp->parent &&
		    mp->parent_index >= NUMKEYS(mp->parent)) {
			mp->parent = pright->parent;
			mp->parent_index = pright->parent_index - 1;
		}
	} else {
		remove_prefix(bt, &sepkey, pright->parent->prefix.len);
		rc = btree_add_node(bt, pright->parent, pright->parent_index,
		    &sepkey, NULL, pright->pgno, 0);
	}
	if (rc != BT_SUCCESS) {
		free(copy);
		return BT_FAIL;
	}

	/* Update prefix for right and left page, if the parent was split.
	 */
	find_common_prefix(bt, pright);
	assert(orig_pfx_len <= pright->prefix.len);
	right_pfx_diff = pright->prefix.len - orig_pfx_len;

	find_common_prefix(bt, mp);
	assert(orig_pfx_len <= mp->prefix.len);
	left_pfx_diff = mp->prefix.len - orig_pfx_len;

	for (i = j = 0; i <= NUMKEYSP(copy); j++) {
		if (i < split_indx) {
			/* Re-insert in left sibling. */
			p = mp;
			pfx_diff = left_pfx_diff;
		} else {
			/* Insert in right sibling. */
			if (i == split_indx)
				/* Reset insert index for right sibling. */
				j = (i == newindx && ins_new);
			p = pright;
			pfx_diff = right_pfx_diff;
		}

		if (i == newindx && !ins_new) {
			/* Insert the original entry that caused the split. */
			rkey.data = newkey->data;
			rkey.size = newkey->size;
			if (IS_LEAF(mp)) {
				rdata.data = newdata->data;
				rdata.size = newdata->size;
			} else
				pgno = newpgno;
			flags = 0;
			pfx_diff = p->prefix.len;

			ins_new = 1;

			/* Update page and index for the new key. */
			*newindxp = j;
			*mpp = p;
		} else if (i == NUMKEYSP(copy)) {
			break;
		} else {
			node = NODEPTRP(copy, i);
			rkey.data = NODEKEY(node);
			rkey.size = node->ksize;
			if (IS_LEAF(mp)) {
				rdata.data = NODEDATA(node);
				rdata.size = node->n_dsize;
			} else
				pgno = node->n_pgno;
			flags = node->flags;

			i++;
		}

		if (!IS_LEAF(mp) && j == 0) {
			/* First branch index doesn't need key data. */
			rkey.size = 0;
		} else
			remove_prefix(bt, &rkey, pfx_diff);

		rc = btree_add_node(bt, p, j, &rkey, &rdata, pgno,flags);
	}

	free(copy);
	return rc;
}

int
btree_txn_put(struct btree *bt, struct btree_txn *txn,
    struct btval *key, struct btval *data, unsigned int flags)
{
	int		 rc = BT_SUCCESS, exact, close_txn = 0;
	unsigned int	 ki;
	struct node	*leaf;
	struct mpage	*mp;
	struct btval	 xkey;

	assert(key != NULL);
	assert(data != NULL);

	if (bt != NULL && txn != NULL && bt != txn->bt) {
		errno = EINVAL;
		return BT_FAIL;
	}

	if (txn != NULL && F_ISSET(txn->flags, BT_TXN_RDONLY)) {
		errno = EINVAL;
		return BT_FAIL;
	}

	if (bt == NULL) {
		if (txn == NULL) {
			errno = EINVAL;
			return BT_FAIL;
		}
		bt = txn->bt;
	}

	if (key->size == 0 || key->size > MAXKEYSIZE) {
		errno = EINVAL;
		return BT_FAIL;
	}

	DPRINTF("==> put key %.*s, size %zu, data size %zu",
		(int)key->size, (char *)key->data, key->size, data->size);

	if (txn == NULL) {
		close_txn = 1;
		if ((txn = btree_txn_begin(bt, 0)) == NULL)
			return BT_FAIL;
	}

	rc = btree_search_page(bt, txn, key, NULL, 1, &mp);
	if (rc == BT_SUCCESS) {
		leaf = btree_search_node(bt, mp, key, &exact, &ki);
		if (leaf && exact) {
			if (F_ISSET(flags, BT_NOOVERWRITE)) {
				DPRINTF("duplicate key %.*s",
				    (int)key->size, (char *)key->data);
				errno = EEXIST;
				rc = BT_FAIL;
				goto done;
			}
			btree_del_node(bt, mp, ki);
		}
		if (leaf == NULL) {		/* append if not found */
			ki = NUMKEYS(mp);
			DPRINTF("appending key at index %d", ki);
		}
	} else if (errno == ENOENT) {
		/* new file, just write a root leaf page */
		DPRINTF("allocating new root leaf page");
		if ((mp = btree_new_page(bt, P_LEAF)) == NULL) {
			rc = BT_FAIL;
			goto done;
		}
		txn->root = mp->pgno;
		bt->meta.depth++;
		ki = 0;
	}
	else
		goto done;

	assert(IS_LEAF(mp));
	DPRINTF("there are %lu keys, should insert new key at index %u",
		NUMKEYS(mp), ki);

	/* Copy the key pointer as it is modified by the prefix code. The
	 * caller might have malloc'ed the data.
	 */
	xkey.data = key->data;
	xkey.size = key->size;

	if (SIZELEFT(mp) < bt_leaf_size(bt, key, data)) {
		rc = btree_split(bt, &mp, &ki, &xkey, data, P_INVALID);
	} else {
		/* There is room already in this leaf page. */
		remove_prefix(bt, &xkey, mp->prefix.len);
		rc = btree_add_node(bt, mp, ki, &xkey, data, 0, 0);
	}

	if (rc != BT_SUCCESS)
		txn->flags |= BT_TXN_ERROR;
	else
		bt->meta.entries++;

done:
	if (close_txn) {
		if (rc == BT_SUCCESS)
			rc = btree_txn_commit(txn);
		else
			btree_txn_abort(txn);
	}
	mpage_prune(bt);
	return rc;
}

static pgno_t
btree_compact_tree(struct btree *bt, pgno_t pgno, struct btree *btc)
{
	ssize_t		 rc;
	indx_t		 i;
	pgno_t		*pnext, next;
	struct node	*node;
	struct page	*p;
	struct mpage	*mp;

	/* Get the page and make a copy of it.
	 */
	if ((mp = btree_get_mpage(bt, pgno)) == NULL)
		return P_INVALID;
	if ((p = malloc(bt->head.psize)) == NULL)
		return P_INVALID;
	bcopy(mp->page, p, bt->head.psize);

        /* Go through all nodes in the (copied) page and update the
         * page pointers.
	 */
	if (F_ISSET(p->flags, P_BRANCH)) {
		for (i = 0; i < NUMKEYSP(p); i++) {
			node = NODEPTRP(p, i);
			node->n_pgno = btree_compact_tree(bt, node->n_pgno, btc);
			if (node->n_pgno == P_INVALID) {
				free(p);
				return P_INVALID;
			}
		}
	} else if (F_ISSET(p->flags, P_LEAF)) {
		for (i = 0; i < NUMKEYSP(p); i++) {
			node = NODEPTRP(p, i);
			if (F_ISSET(node->flags, F_BIGDATA)) {
				bcopy(NODEDATA(node), &next, sizeof(next));
				next = btree_compact_tree(bt, next, btc);
				if (next == P_INVALID) {
					free(p);
					return P_INVALID;
				}
				bcopy(&next, NODEDATA(node), sizeof(next));
			}
		}
	} else if (F_ISSET(p->flags, P_OVERFLOW)) {
		pnext = &p->p_next_pgno;
		if (*pnext > 0) {
			*pnext = btree_compact_tree(bt, *pnext, btc);
			if (*pnext == P_INVALID) {
				free(p);
				return P_INVALID;
			}
		}
	} else
		assert(0);

	pgno = p->pgno = btc->txn->next_pgno++;
	rc = write(btc->fd, p, bt->head.psize);
	free(p);
	if (rc != (ssize_t)bt->head.psize)
		return P_INVALID;
	mpage_prune(bt);
	return pgno;
}

int
btree_compact(struct btree *bt)
{
	char			*compact_path = NULL;
	struct btree		*btc;
	struct btree_txn	*txn, *txnc = NULL;
	int			 fd;
	pgno_t			 root;

	assert(bt != NULL);

	DPRINTF("compacting btree %p with path %s", bt, bt->path);

	if (bt->path == NULL) {
		errno = EINVAL;
		return BT_FAIL;
	}

	if ((txn = btree_txn_begin(bt, 0)) == NULL)
		return BT_FAIL;

	if (asprintf(&compact_path, "%s.compact.XXXXXX", bt->path) == -1) {
		btree_txn_abort(txn);
		return BT_FAIL;
	}
	fd = mkstemp(compact_path);
	if (fd == -1) {
		free(compact_path);
		btree_txn_abort(txn);
		return BT_FAIL;
	}

	if ((btc = btree_open_fd(fd, 0)) == NULL)
		goto failed;
	bcopy(&bt->meta, &btc->meta, sizeof(bt->meta));
	btc->meta.revisions = 0;

	if ((txnc = btree_txn_begin(btc, 0)) == NULL)
		goto failed;

	if (bt->meta.root != P_INVALID) {
		root = btree_compact_tree(bt, bt->meta.root, btc);
		if (root == P_INVALID)
			goto failed;
		if (btree_write_meta(btc, root, 0) != BT_SUCCESS)
			goto failed;
	}

	fsync(fd);

	DPRINTF("renaming %s to %s", compact_path, bt->path);
	if (rename(compact_path, bt->path) != 0)
		goto failed;

	/* Write a "tombstone" meta page so other processes can pick up
	 * the change and re-open the file.
	 */
	if (btree_write_meta(bt, P_INVALID, BT_TOMBSTONE) != BT_SUCCESS)
		goto failed;

	btree_txn_abort(txn);
	btree_txn_abort(txnc);
	free(compact_path);
	btree_close(btc);
	mpage_prune(bt);
	return 0;

failed:
	btree_txn_abort(txn);
	btree_txn_abort(txnc);
	unlink(compact_path);
	free(compact_path);
	btree_close(btc);
	mpage_prune(bt);
	return BT_FAIL;
}

/* Reverts the last change. Truncates the file at the last root page.
 */
int
btree_revert(struct btree *bt)
{
	if (btree_read_meta(bt, NULL) != 0)
		return -1;

	DPRINTF("truncating file at page %u", bt->meta.root);
	return ftruncate(bt->fd, bt->head.psize * bt->meta.root);
}

void
btree_set_cache_size(struct btree *bt, unsigned int cache_size)
{
	bt->stat.max_cache = cache_size;
}

unsigned int
btree_get_flags(struct btree *bt)
{
	return (bt->flags & ~BT_FIXPADDING);
}

const char *
btree_get_path(struct btree *bt)
{
	return bt->path;
}

const struct btree_stat *
btree_stat(struct btree *bt)
{
	if (bt == NULL)
		return NULL;

	bt->stat.branch_pages = bt->meta.branch_pages;
	bt->stat.leaf_pages = bt->meta.leaf_pages;
	bt->stat.overflow_pages = bt->meta.overflow_pages;
	bt->stat.revisions = bt->meta.revisions;
	bt->stat.depth = bt->meta.depth;
	bt->stat.entries = bt->meta.entries;
	bt->stat.psize = bt->head.psize;
	bt->stat.created_at = bt->meta.created_at;

	return &bt->stat;
}

