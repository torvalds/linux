/*	$OpenBSD: btree.h,v 1.6 2010/07/02 01:43:00 martinh Exp $ */

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

#ifndef _btree_h_
#define _btree_h_

#include <openssl/sha.h>

struct mpage;
struct cursor;
struct btree_txn;

struct btval {
	void		*data;
	size_t		 size;
	int		 free_data;		/* true if data malloc'd */
	struct mpage	*mp;			/* ref'd memory page */
};

typedef int		 (*bt_cmp_func)(const struct btval *a,
					const struct btval *b);
typedef void		 (*bt_prefix_func)(const struct btval *a,
					   const struct btval *b,
					   struct btval *sep);

#define BT_NOOVERWRITE	 1

enum cursor_op {				/* cursor operations */
	BT_CURSOR,				/* position at given key */
	BT_CURSOR_EXACT,			/* position at key, or fail */
	BT_FIRST,
	BT_NEXT,
	BT_LAST,				/* not implemented */
	BT_PREV					/* not implemented */
};

/* return codes */
#define BT_FAIL		-1
#define BT_SUCCESS	 0

/* btree flags */
#define BT_NOSYNC		 0x02		/* don't fsync after commit */
#define BT_RDONLY		 0x04		/* read only */
#define BT_REVERSEKEY		 0x08		/* use reverse string keys */

struct btree_stat {
	unsigned long long int	 hits;		/* cache hits */
	unsigned long long int	 reads;		/* page reads */
	unsigned int		 max_cache;	/* max cached pages */
	unsigned int		 cache_size;	/* current cache size */
	unsigned int		 branch_pages;
	unsigned int		 leaf_pages;
	unsigned int		 overflow_pages;
	unsigned int		 revisions;
	unsigned int		 depth;
	unsigned long long int	 entries;
	unsigned int		 psize;
	time_t			 created_at;
};

struct btree		*btree_open_fd(int fd, unsigned int flags);
struct btree		*btree_open(const char *path, unsigned int flags,
			    mode_t mode);
void			 btree_close(struct btree *bt);
const struct btree_stat	*btree_stat(struct btree *bt);

struct btree_txn	*btree_txn_begin(struct btree *bt, int rdonly);
int			 btree_txn_commit(struct btree_txn *txn);
void			 btree_txn_abort(struct btree_txn *txn);

int			 btree_txn_get(struct btree *bt, struct btree_txn *txn,
			    struct btval *key, struct btval *data);
int			 btree_txn_put(struct btree *bt, struct btree_txn *txn,
			    struct btval *key, struct btval *data,
			    unsigned int flags);
int			 btree_txn_del(struct btree *bt, struct btree_txn *txn,
			    struct btval *key, struct btval *data);

#define btree_get(bt, key, data)	 \
			 btree_txn_get(bt, NULL, key, data)
#define btree_put(bt, key, data, flags)	 \
			 btree_txn_put(bt, NULL, key, data, flags)
#define btree_del(bt, key, data)	 \
			 btree_txn_del(bt, NULL, key, data)

void			 btree_set_cache_size(struct btree *bt,
			    unsigned int cache_size);
unsigned int		 btree_get_flags(struct btree *bt);
const char		*btree_get_path(struct btree *bt);

#define btree_cursor_open(bt)	 \
			 btree_txn_cursor_open(bt, NULL)
struct cursor		*btree_txn_cursor_open(struct btree *bt,
			    struct btree_txn *txn);
void			 btree_cursor_close(struct cursor *cursor);
int			 btree_cursor_get(struct cursor *cursor,
			    struct btval *key, struct btval *data,
			    enum cursor_op op);

int			 btree_sync(struct btree *bt);
int			 btree_compact(struct btree *bt);
int			 btree_revert(struct btree *bt);

int			 btree_cmp(struct btree *bt, const struct btval *a,
			     const struct btval *b);
void			 btval_reset(struct btval *btv);

#endif

