/*	$OpenBSD: tree.h,v 1.31 2023/03/08 04:43:09 guenther Exp $	*/
/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_SYS_TREE_H_
#define	_SYS_TREE_H_

#include <sys/_null.h>

/*
 * This file defines data structures for different types of trees:
 * splay trees and red-black trees.
 *
 * A splay tree is a self-organizing data structure.  Every operation
 * on the tree causes a splay to happen.  The splay moves the requested
 * node to the root of the tree and partly rebalances it.
 *
 * This has the benefit that request locality causes faster lookups as
 * the requested nodes move to the top of the tree.  On the other hand,
 * every lookup causes memory writes.
 *
 * The Balance Theorem bounds the total access time for m operations
 * and n inserts on an initially empty tree as O((m + n)lg n).  The
 * amortized cost for a sequence of m accesses to a splay tree is O(lg n);
 *
 * A red-black tree is a binary search tree with the node color as an
 * extra attribute.  It fulfills a set of conditions:
 *	- every search path from the root to a leaf consists of the
 *	  same number of black nodes,
 *	- each red node (except for the root) has a black parent,
 *	- each leaf node is black.
 *
 * Every operation on a red-black tree is bounded as O(lg n).
 * The maximum height of a red-black tree is 2lg (n+1).
 */

#define SPLAY_HEAD(name, type)						\
struct name {								\
	struct type *sph_root; /* root of the tree */			\
}

#define SPLAY_INITIALIZER(root)						\
	{ NULL }

#define SPLAY_INIT(root) do {						\
	(root)->sph_root = NULL;					\
} while (0)

#define SPLAY_ENTRY(type)						\
struct {								\
	struct type *spe_left; /* left element */			\
	struct type *spe_right; /* right element */			\
}

#define SPLAY_LEFT(elm, field)		(elm)->field.spe_left
#define SPLAY_RIGHT(elm, field)		(elm)->field.spe_right
#define SPLAY_ROOT(head)		(head)->sph_root
#define SPLAY_EMPTY(head)		(SPLAY_ROOT(head) == NULL)

/* SPLAY_ROTATE_{LEFT,RIGHT} expect that tmp hold SPLAY_{RIGHT,LEFT} */
#define SPLAY_ROTATE_RIGHT(head, tmp, field) do {			\
	SPLAY_LEFT((head)->sph_root, field) = SPLAY_RIGHT(tmp, field);	\
	SPLAY_RIGHT(tmp, field) = (head)->sph_root;			\
	(head)->sph_root = tmp;						\
} while (0)

#define SPLAY_ROTATE_LEFT(head, tmp, field) do {			\
	SPLAY_RIGHT((head)->sph_root, field) = SPLAY_LEFT(tmp, field);	\
	SPLAY_LEFT(tmp, field) = (head)->sph_root;			\
	(head)->sph_root = tmp;						\
} while (0)

#define SPLAY_LINKLEFT(head, tmp, field) do {				\
	SPLAY_LEFT(tmp, field) = (head)->sph_root;			\
	tmp = (head)->sph_root;						\
	(head)->sph_root = SPLAY_LEFT((head)->sph_root, field);		\
} while (0)

#define SPLAY_LINKRIGHT(head, tmp, field) do {				\
	SPLAY_RIGHT(tmp, field) = (head)->sph_root;			\
	tmp = (head)->sph_root;						\
	(head)->sph_root = SPLAY_RIGHT((head)->sph_root, field);	\
} while (0)

#define SPLAY_ASSEMBLE(head, node, left, right, field) do {		\
	SPLAY_RIGHT(left, field) = SPLAY_LEFT((head)->sph_root, field);	\
	SPLAY_LEFT(right, field) = SPLAY_RIGHT((head)->sph_root, field);\
	SPLAY_LEFT((head)->sph_root, field) = SPLAY_RIGHT(node, field);	\
	SPLAY_RIGHT((head)->sph_root, field) = SPLAY_LEFT(node, field);	\
} while (0)

/* Generates prototypes and inline functions */

#define SPLAY_PROTOTYPE(name, type, field, cmp)				\
void name##_SPLAY(struct name *, struct type *);			\
void name##_SPLAY_MINMAX(struct name *, int);				\
struct type *name##_SPLAY_INSERT(struct name *, struct type *);		\
struct type *name##_SPLAY_REMOVE(struct name *, struct type *);		\
									\
/* Finds the node with the same key as elm */				\
static __unused __inline struct type *					\
name##_SPLAY_FIND(struct name *head, struct type *elm)			\
{									\
	if (SPLAY_EMPTY(head))						\
		return(NULL);						\
	name##_SPLAY(head, elm);					\
	if ((cmp)(elm, (head)->sph_root) == 0)				\
		return (head->sph_root);				\
	return (NULL);							\
}									\
									\
static __unused __inline struct type *					\
name##_SPLAY_NEXT(struct name *head, struct type *elm)			\
{									\
	name##_SPLAY(head, elm);					\
	if (SPLAY_RIGHT(elm, field) != NULL) {				\
		elm = SPLAY_RIGHT(elm, field);				\
		while (SPLAY_LEFT(elm, field) != NULL) {		\
			elm = SPLAY_LEFT(elm, field);			\
		}							\
	} else								\
		elm = NULL;						\
	return (elm);							\
}									\
									\
static __unused __inline struct type *					\
name##_SPLAY_MIN_MAX(struct name *head, int val)			\
{									\
	name##_SPLAY_MINMAX(head, val);					\
        return (SPLAY_ROOT(head));					\
}

/* Main splay operation.
 * Moves node close to the key of elm to top
 */
#define SPLAY_GENERATE(name, type, field, cmp)				\
struct type *								\
name##_SPLAY_INSERT(struct name *head, struct type *elm)		\
{									\
    if (SPLAY_EMPTY(head)) {						\
	    SPLAY_LEFT(elm, field) = SPLAY_RIGHT(elm, field) = NULL;	\
    } else {								\
	    int __comp;							\
	    name##_SPLAY(head, elm);					\
	    __comp = (cmp)(elm, (head)->sph_root);			\
	    if(__comp < 0) {						\
		    SPLAY_LEFT(elm, field) = SPLAY_LEFT((head)->sph_root, field);\
		    SPLAY_RIGHT(elm, field) = (head)->sph_root;		\
		    SPLAY_LEFT((head)->sph_root, field) = NULL;		\
	    } else if (__comp > 0) {					\
		    SPLAY_RIGHT(elm, field) = SPLAY_RIGHT((head)->sph_root, field);\
		    SPLAY_LEFT(elm, field) = (head)->sph_root;		\
		    SPLAY_RIGHT((head)->sph_root, field) = NULL;	\
	    } else							\
		    return ((head)->sph_root);				\
    }									\
    (head)->sph_root = (elm);						\
    return (NULL);							\
}									\
									\
struct type *								\
name##_SPLAY_REMOVE(struct name *head, struct type *elm)		\
{									\
	struct type *__tmp;						\
	if (SPLAY_EMPTY(head))						\
		return (NULL);						\
	name##_SPLAY(head, elm);					\
	if ((cmp)(elm, (head)->sph_root) == 0) {			\
		if (SPLAY_LEFT((head)->sph_root, field) == NULL) {	\
			(head)->sph_root = SPLAY_RIGHT((head)->sph_root, field);\
		} else {						\
			__tmp = SPLAY_RIGHT((head)->sph_root, field);	\
			(head)->sph_root = SPLAY_LEFT((head)->sph_root, field);\
			name##_SPLAY(head, elm);			\
			SPLAY_RIGHT((head)->sph_root, field) = __tmp;	\
		}							\
		return (elm);						\
	}								\
	return (NULL);							\
}									\
									\
void									\
name##_SPLAY(struct name *head, struct type *elm)			\
{									\
	struct type __node, *__left, *__right, *__tmp;			\
	int __comp;							\
\
	SPLAY_LEFT(&__node, field) = SPLAY_RIGHT(&__node, field) = NULL;\
	__left = __right = &__node;					\
\
	while ((__comp = (cmp)(elm, (head)->sph_root))) {		\
		if (__comp < 0) {					\
			__tmp = SPLAY_LEFT((head)->sph_root, field);	\
			if (__tmp == NULL)				\
				break;					\
			if ((cmp)(elm, __tmp) < 0){			\
				SPLAY_ROTATE_RIGHT(head, __tmp, field);	\
				if (SPLAY_LEFT((head)->sph_root, field) == NULL)\
					break;				\
			}						\
			SPLAY_LINKLEFT(head, __right, field);		\
		} else if (__comp > 0) {				\
			__tmp = SPLAY_RIGHT((head)->sph_root, field);	\
			if (__tmp == NULL)				\
				break;					\
			if ((cmp)(elm, __tmp) > 0){			\
				SPLAY_ROTATE_LEFT(head, __tmp, field);	\
				if (SPLAY_RIGHT((head)->sph_root, field) == NULL)\
					break;				\
			}						\
			SPLAY_LINKRIGHT(head, __left, field);		\
		}							\
	}								\
	SPLAY_ASSEMBLE(head, &__node, __left, __right, field);		\
}									\
									\
/* Splay with either the minimum or the maximum element			\
 * Used to find minimum or maximum element in tree.			\
 */									\
void name##_SPLAY_MINMAX(struct name *head, int __comp) \
{									\
	struct type __node, *__left, *__right, *__tmp;			\
\
	SPLAY_LEFT(&__node, field) = SPLAY_RIGHT(&__node, field) = NULL;\
	__left = __right = &__node;					\
\
	while (1) {							\
		if (__comp < 0) {					\
			__tmp = SPLAY_LEFT((head)->sph_root, field);	\
			if (__tmp == NULL)				\
				break;					\
			if (__comp < 0){				\
				SPLAY_ROTATE_RIGHT(head, __tmp, field);	\
				if (SPLAY_LEFT((head)->sph_root, field) == NULL)\
					break;				\
			}						\
			SPLAY_LINKLEFT(head, __right, field);		\
		} else if (__comp > 0) {				\
			__tmp = SPLAY_RIGHT((head)->sph_root, field);	\
			if (__tmp == NULL)				\
				break;					\
			if (__comp > 0) {				\
				SPLAY_ROTATE_LEFT(head, __tmp, field);	\
				if (SPLAY_RIGHT((head)->sph_root, field) == NULL)\
					break;				\
			}						\
			SPLAY_LINKRIGHT(head, __left, field);		\
		}							\
	}								\
	SPLAY_ASSEMBLE(head, &__node, __left, __right, field);		\
}

#define SPLAY_NEGINF	-1
#define SPLAY_INF	1

#define SPLAY_INSERT(name, x, y)	name##_SPLAY_INSERT(x, y)
#define SPLAY_REMOVE(name, x, y)	name##_SPLAY_REMOVE(x, y)
#define SPLAY_FIND(name, x, y)		name##_SPLAY_FIND(x, y)
#define SPLAY_NEXT(name, x, y)		name##_SPLAY_NEXT(x, y)
#define SPLAY_MIN(name, x)		(SPLAY_EMPTY(x) ? NULL	\
					: name##_SPLAY_MIN_MAX(x, SPLAY_NEGINF))
#define SPLAY_MAX(name, x)		(SPLAY_EMPTY(x) ? NULL	\
					: name##_SPLAY_MIN_MAX(x, SPLAY_INF))

#define SPLAY_FOREACH(x, name, head)					\
	for ((x) = SPLAY_MIN(name, head);				\
	     (x) != NULL;						\
	     (x) = SPLAY_NEXT(name, head, x))

/* Macros that define a red-black tree */
#define RB_HEAD(name, type)						\
struct name {								\
	struct type *rbh_root; /* root of the tree */			\
}

#define RB_INITIALIZER(root)						\
	{ NULL }

#define RB_INIT(root) do {						\
	(root)->rbh_root = NULL;					\
} while (0)

#define RB_BLACK	0
#define RB_RED		1
#define RB_ENTRY(type)							\
struct {								\
	struct type *rbe_left;		/* left element */		\
	struct type *rbe_right;		/* right element */		\
	struct type *rbe_parent;	/* parent element */		\
	int rbe_color;			/* node color */		\
}

#define RB_LEFT(elm, field)		(elm)->field.rbe_left
#define RB_RIGHT(elm, field)		(elm)->field.rbe_right
#define RB_PARENT(elm, field)		(elm)->field.rbe_parent
#define RB_COLOR(elm, field)		(elm)->field.rbe_color
#define RB_ROOT(head)			(head)->rbh_root
#define RB_EMPTY(head)			(RB_ROOT(head) == NULL)

#define RB_SET(elm, parent, field) do {					\
	RB_PARENT(elm, field) = parent;					\
	RB_LEFT(elm, field) = RB_RIGHT(elm, field) = NULL;		\
	RB_COLOR(elm, field) = RB_RED;					\
} while (0)

#define RB_SET_BLACKRED(black, red, field) do {				\
	RB_COLOR(black, field) = RB_BLACK;				\
	RB_COLOR(red, field) = RB_RED;					\
} while (0)

#ifndef RB_AUGMENT
#define RB_AUGMENT(x)	do {} while (0)
#endif

#define RB_ROTATE_LEFT(head, elm, tmp, field) do {			\
	(tmp) = RB_RIGHT(elm, field);					\
	if ((RB_RIGHT(elm, field) = RB_LEFT(tmp, field))) {		\
		RB_PARENT(RB_LEFT(tmp, field), field) = (elm);		\
	}								\
	RB_AUGMENT(elm);						\
	if ((RB_PARENT(tmp, field) = RB_PARENT(elm, field))) {		\
		if ((elm) == RB_LEFT(RB_PARENT(elm, field), field))	\
			RB_LEFT(RB_PARENT(elm, field), field) = (tmp);	\
		else							\
			RB_RIGHT(RB_PARENT(elm, field), field) = (tmp);	\
	} else								\
		(head)->rbh_root = (tmp);				\
	RB_LEFT(tmp, field) = (elm);					\
	RB_PARENT(elm, field) = (tmp);					\
	RB_AUGMENT(tmp);						\
	if ((RB_PARENT(tmp, field)))					\
		RB_AUGMENT(RB_PARENT(tmp, field));			\
} while (0)

#define RB_ROTATE_RIGHT(head, elm, tmp, field) do {			\
	(tmp) = RB_LEFT(elm, field);					\
	if ((RB_LEFT(elm, field) = RB_RIGHT(tmp, field))) {		\
		RB_PARENT(RB_RIGHT(tmp, field), field) = (elm);		\
	}								\
	RB_AUGMENT(elm);						\
	if ((RB_PARENT(tmp, field) = RB_PARENT(elm, field))) {		\
		if ((elm) == RB_LEFT(RB_PARENT(elm, field), field))	\
			RB_LEFT(RB_PARENT(elm, field), field) = (tmp);	\
		else							\
			RB_RIGHT(RB_PARENT(elm, field), field) = (tmp);	\
	} else								\
		(head)->rbh_root = (tmp);				\
	RB_RIGHT(tmp, field) = (elm);					\
	RB_PARENT(elm, field) = (tmp);					\
	RB_AUGMENT(tmp);						\
	if ((RB_PARENT(tmp, field)))					\
		RB_AUGMENT(RB_PARENT(tmp, field));			\
} while (0)

/* Generates prototypes and inline functions */
#define	RB_PROTOTYPE(name, type, field, cmp)				\
	RB_PROTOTYPE_INTERNAL(name, type, field, cmp,)
#define	RB_PROTOTYPE_STATIC(name, type, field, cmp)			\
	RB_PROTOTYPE_INTERNAL(name, type, field, cmp, __attribute__((__unused__)) static)
#define RB_PROTOTYPE_INTERNAL(name, type, field, cmp, attr)		\
attr void name##_RB_INSERT_COLOR(struct name *, struct type *);		\
attr void name##_RB_REMOVE_COLOR(struct name *, struct type *, struct type *);\
attr struct type *name##_RB_REMOVE(struct name *, struct type *);	\
attr struct type *name##_RB_INSERT(struct name *, struct type *);	\
attr struct type *name##_RB_FIND(struct name *, struct type *);		\
attr struct type *name##_RB_NFIND(struct name *, struct type *);	\
attr struct type *name##_RB_NEXT(struct type *);			\
attr struct type *name##_RB_PREV(struct type *);			\
attr struct type *name##_RB_MINMAX(struct name *, int);			\
									\

/* Main rb operation.
 * Moves node close to the key of elm to top
 */
#define	RB_GENERATE(name, type, field, cmp)				\
	RB_GENERATE_INTERNAL(name, type, field, cmp,)
#define	RB_GENERATE_STATIC(name, type, field, cmp)			\
	RB_GENERATE_INTERNAL(name, type, field, cmp, __attribute__((__unused__)) static)
#define RB_GENERATE_INTERNAL(name, type, field, cmp, attr)		\
attr void								\
name##_RB_INSERT_COLOR(struct name *head, struct type *elm)		\
{									\
	struct type *parent, *gparent, *tmp;				\
	while ((parent = RB_PARENT(elm, field)) &&			\
	    RB_COLOR(parent, field) == RB_RED) {			\
		gparent = RB_PARENT(parent, field);			\
		if (parent == RB_LEFT(gparent, field)) {		\
			tmp = RB_RIGHT(gparent, field);			\
			if (tmp && RB_COLOR(tmp, field) == RB_RED) {	\
				RB_COLOR(tmp, field) = RB_BLACK;	\
				RB_SET_BLACKRED(parent, gparent, field);\
				elm = gparent;				\
				continue;				\
			}						\
			if (RB_RIGHT(parent, field) == elm) {		\
				RB_ROTATE_LEFT(head, parent, tmp, field);\
				tmp = parent;				\
				parent = elm;				\
				elm = tmp;				\
			}						\
			RB_SET_BLACKRED(parent, gparent, field);	\
			RB_ROTATE_RIGHT(head, gparent, tmp, field);	\
		} else {						\
			tmp = RB_LEFT(gparent, field);			\
			if (tmp && RB_COLOR(tmp, field) == RB_RED) {	\
				RB_COLOR(tmp, field) = RB_BLACK;	\
				RB_SET_BLACKRED(parent, gparent, field);\
				elm = gparent;				\
				continue;				\
			}						\
			if (RB_LEFT(parent, field) == elm) {		\
				RB_ROTATE_RIGHT(head, parent, tmp, field);\
				tmp = parent;				\
				parent = elm;				\
				elm = tmp;				\
			}						\
			RB_SET_BLACKRED(parent, gparent, field);	\
			RB_ROTATE_LEFT(head, gparent, tmp, field);	\
		}							\
	}								\
	RB_COLOR(head->rbh_root, field) = RB_BLACK;			\
}									\
									\
attr void								\
name##_RB_REMOVE_COLOR(struct name *head, struct type *parent, struct type *elm) \
{									\
	struct type *tmp;						\
	while ((elm == NULL || RB_COLOR(elm, field) == RB_BLACK) &&	\
	    elm != RB_ROOT(head)) {					\
		if (RB_LEFT(parent, field) == elm) {			\
			tmp = RB_RIGHT(parent, field);			\
			if (RB_COLOR(tmp, field) == RB_RED) {		\
				RB_SET_BLACKRED(tmp, parent, field);	\
				RB_ROTATE_LEFT(head, parent, tmp, field);\
				tmp = RB_RIGHT(parent, field);		\
			}						\
			if ((RB_LEFT(tmp, field) == NULL ||		\
			    RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) &&\
			    (RB_RIGHT(tmp, field) == NULL ||		\
			    RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK)) {\
				RB_COLOR(tmp, field) = RB_RED;		\
				elm = parent;				\
				parent = RB_PARENT(elm, field);		\
			} else {					\
				if (RB_RIGHT(tmp, field) == NULL ||	\
				    RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK) {\
					struct type *oleft;		\
					if ((oleft = RB_LEFT(tmp, field)))\
						RB_COLOR(oleft, field) = RB_BLACK;\
					RB_COLOR(tmp, field) = RB_RED;	\
					RB_ROTATE_RIGHT(head, tmp, oleft, field);\
					tmp = RB_RIGHT(parent, field);	\
				}					\
				RB_COLOR(tmp, field) = RB_COLOR(parent, field);\
				RB_COLOR(parent, field) = RB_BLACK;	\
				if (RB_RIGHT(tmp, field))		\
					RB_COLOR(RB_RIGHT(tmp, field), field) = RB_BLACK;\
				RB_ROTATE_LEFT(head, parent, tmp, field);\
				elm = RB_ROOT(head);			\
				break;					\
			}						\
		} else {						\
			tmp = RB_LEFT(parent, field);			\
			if (RB_COLOR(tmp, field) == RB_RED) {		\
				RB_SET_BLACKRED(tmp, parent, field);	\
				RB_ROTATE_RIGHT(head, parent, tmp, field);\
				tmp = RB_LEFT(parent, field);		\
			}						\
			if ((RB_LEFT(tmp, field) == NULL ||		\
			    RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) &&\
			    (RB_RIGHT(tmp, field) == NULL ||		\
			    RB_COLOR(RB_RIGHT(tmp, field), field) == RB_BLACK)) {\
				RB_COLOR(tmp, field) = RB_RED;		\
				elm = parent;				\
				parent = RB_PARENT(elm, field);		\
			} else {					\
				if (RB_LEFT(tmp, field) == NULL ||	\
				    RB_COLOR(RB_LEFT(tmp, field), field) == RB_BLACK) {\
					struct type *oright;		\
					if ((oright = RB_RIGHT(tmp, field)))\
						RB_COLOR(oright, field) = RB_BLACK;\
					RB_COLOR(tmp, field) = RB_RED;	\
					RB_ROTATE_LEFT(head, tmp, oright, field);\
					tmp = RB_LEFT(parent, field);	\
				}					\
				RB_COLOR(tmp, field) = RB_COLOR(parent, field);\
				RB_COLOR(parent, field) = RB_BLACK;	\
				if (RB_LEFT(tmp, field))		\
					RB_COLOR(RB_LEFT(tmp, field), field) = RB_BLACK;\
				RB_ROTATE_RIGHT(head, parent, tmp, field);\
				elm = RB_ROOT(head);			\
				break;					\
			}						\
		}							\
	}								\
	if (elm)							\
		RB_COLOR(elm, field) = RB_BLACK;			\
}									\
									\
attr struct type *							\
name##_RB_REMOVE(struct name *head, struct type *elm)			\
{									\
	struct type *child, *parent, *old = elm;			\
	int color;							\
	if (RB_LEFT(elm, field) == NULL)				\
		child = RB_RIGHT(elm, field);				\
	else if (RB_RIGHT(elm, field) == NULL)				\
		child = RB_LEFT(elm, field);				\
	else {								\
		struct type *left;					\
		elm = RB_RIGHT(elm, field);				\
		while ((left = RB_LEFT(elm, field)))			\
			elm = left;					\
		child = RB_RIGHT(elm, field);				\
		parent = RB_PARENT(elm, field);				\
		color = RB_COLOR(elm, field);				\
		if (child)						\
			RB_PARENT(child, field) = parent;		\
		if (parent) {						\
			if (RB_LEFT(parent, field) == elm)		\
				RB_LEFT(parent, field) = child;		\
			else						\
				RB_RIGHT(parent, field) = child;	\
			RB_AUGMENT(parent);				\
		} else							\
			RB_ROOT(head) = child;				\
		if (RB_PARENT(elm, field) == old)			\
			parent = elm;					\
		(elm)->field = (old)->field;				\
		if (RB_PARENT(old, field)) {				\
			if (RB_LEFT(RB_PARENT(old, field), field) == old)\
				RB_LEFT(RB_PARENT(old, field), field) = elm;\
			else						\
				RB_RIGHT(RB_PARENT(old, field), field) = elm;\
			RB_AUGMENT(RB_PARENT(old, field));		\
		} else							\
			RB_ROOT(head) = elm;				\
		RB_PARENT(RB_LEFT(old, field), field) = elm;		\
		if (RB_RIGHT(old, field))				\
			RB_PARENT(RB_RIGHT(old, field), field) = elm;	\
		if (parent) {						\
			left = parent;					\
			do {						\
				RB_AUGMENT(left);			\
			} while ((left = RB_PARENT(left, field)));	\
		}							\
		goto color;						\
	}								\
	parent = RB_PARENT(elm, field);					\
	color = RB_COLOR(elm, field);					\
	if (child)							\
		RB_PARENT(child, field) = parent;			\
	if (parent) {							\
		if (RB_LEFT(parent, field) == elm)			\
			RB_LEFT(parent, field) = child;			\
		else							\
			RB_RIGHT(parent, field) = child;		\
		RB_AUGMENT(parent);					\
	} else								\
		RB_ROOT(head) = child;					\
color:									\
	if (color == RB_BLACK)						\
		name##_RB_REMOVE_COLOR(head, parent, child);		\
	return (old);							\
}									\
									\
/* Inserts a node into the RB tree */					\
attr struct type *							\
name##_RB_INSERT(struct name *head, struct type *elm)			\
{									\
	struct type *tmp;						\
	struct type *parent = NULL;					\
	int comp = 0;							\
	tmp = RB_ROOT(head);						\
	while (tmp) {							\
		parent = tmp;						\
		comp = (cmp)(elm, parent);				\
		if (comp < 0)						\
			tmp = RB_LEFT(tmp, field);			\
		else if (comp > 0)					\
			tmp = RB_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	RB_SET(elm, parent, field);					\
	if (parent != NULL) {						\
		if (comp < 0)						\
			RB_LEFT(parent, field) = elm;			\
		else							\
			RB_RIGHT(parent, field) = elm;			\
		RB_AUGMENT(parent);					\
	} else								\
		RB_ROOT(head) = elm;					\
	name##_RB_INSERT_COLOR(head, elm);				\
	return (NULL);							\
}									\
									\
/* Finds the node with the same key as elm */				\
attr struct type *							\
name##_RB_FIND(struct name *head, struct type *elm)			\
{									\
	struct type *tmp = RB_ROOT(head);				\
	int comp;							\
	while (tmp) {							\
		comp = cmp(elm, tmp);					\
		if (comp < 0)						\
			tmp = RB_LEFT(tmp, field);			\
		else if (comp > 0)					\
			tmp = RB_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	return (NULL);							\
}									\
									\
/* Finds the first node greater than or equal to the search key */	\
attr struct type *							\
name##_RB_NFIND(struct name *head, struct type *elm)			\
{									\
	struct type *tmp = RB_ROOT(head);				\
	struct type *res = NULL;					\
	int comp;							\
	while (tmp) {							\
		comp = cmp(elm, tmp);					\
		if (comp < 0) {						\
			res = tmp;					\
			tmp = RB_LEFT(tmp, field);			\
		}							\
		else if (comp > 0)					\
			tmp = RB_RIGHT(tmp, field);			\
		else							\
			return (tmp);					\
	}								\
	return (res);							\
}									\
									\
attr struct type *							\
name##_RB_NEXT(struct type *elm)					\
{									\
	if (RB_RIGHT(elm, field)) {					\
		elm = RB_RIGHT(elm, field);				\
		while (RB_LEFT(elm, field))				\
			elm = RB_LEFT(elm, field);			\
	} else {							\
		if (RB_PARENT(elm, field) &&				\
		    (elm == RB_LEFT(RB_PARENT(elm, field), field)))	\
			elm = RB_PARENT(elm, field);			\
		else {							\
			while (RB_PARENT(elm, field) &&			\
			    (elm == RB_RIGHT(RB_PARENT(elm, field), field)))\
				elm = RB_PARENT(elm, field);		\
			elm = RB_PARENT(elm, field);			\
		}							\
	}								\
	return (elm);							\
}									\
									\
attr struct type *							\
name##_RB_PREV(struct type *elm)					\
{									\
	if (RB_LEFT(elm, field)) {					\
		elm = RB_LEFT(elm, field);				\
		while (RB_RIGHT(elm, field))				\
			elm = RB_RIGHT(elm, field);			\
	} else {							\
		if (RB_PARENT(elm, field) &&				\
		    (elm == RB_RIGHT(RB_PARENT(elm, field), field)))	\
			elm = RB_PARENT(elm, field);			\
		else {							\
			while (RB_PARENT(elm, field) &&			\
			    (elm == RB_LEFT(RB_PARENT(elm, field), field)))\
				elm = RB_PARENT(elm, field);		\
			elm = RB_PARENT(elm, field);			\
		}							\
	}								\
	return (elm);							\
}									\
									\
attr struct type *							\
name##_RB_MINMAX(struct name *head, int val)				\
{									\
	struct type *tmp = RB_ROOT(head);				\
	struct type *parent = NULL;					\
	while (tmp) {							\
		parent = tmp;						\
		if (val < 0)						\
			tmp = RB_LEFT(tmp, field);			\
		else							\
			tmp = RB_RIGHT(tmp, field);			\
	}								\
	return (parent);						\
}

#define RB_NEGINF	-1
#define RB_INF	1

#define RB_INSERT(name, x, y)	name##_RB_INSERT(x, y)
#define RB_REMOVE(name, x, y)	name##_RB_REMOVE(x, y)
#define RB_FIND(name, x, y)	name##_RB_FIND(x, y)
#define RB_NFIND(name, x, y)	name##_RB_NFIND(x, y)
#define RB_NEXT(name, x, y)	name##_RB_NEXT(y)
#define RB_PREV(name, x, y)	name##_RB_PREV(y)
#define RB_MIN(name, x)		name##_RB_MINMAX(x, RB_NEGINF)
#define RB_MAX(name, x)		name##_RB_MINMAX(x, RB_INF)

#define RB_FOREACH(x, name, head)					\
	for ((x) = RB_MIN(name, head);					\
	     (x) != NULL;						\
	     (x) = name##_RB_NEXT(x))

#define RB_FOREACH_SAFE(x, name, head, y)				\
	for ((x) = RB_MIN(name, head);					\
	    ((x) != NULL) && ((y) = name##_RB_NEXT(x), 1);		\
	     (x) = (y))

#define RB_FOREACH_REVERSE(x, name, head)				\
	for ((x) = RB_MAX(name, head);					\
	     (x) != NULL;						\
	     (x) = name##_RB_PREV(x))

#define RB_FOREACH_REVERSE_SAFE(x, name, head, y)			\
	for ((x) = RB_MAX(name, head);					\
	    ((x) != NULL) && ((y) = name##_RB_PREV(x), 1);		\
	     (x) = (y))


/*
 * Copyright (c) 2016 David Gwynne <dlg@openbsd.org>
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

struct rb_type {
	int		(*t_compare)(const void *, const void *);
	void		(*t_augment)(void *);
	unsigned int	  t_offset;	/* offset of rb_entry in type */
};

struct rb_tree {
	struct rb_entry	*rbt_root;
};

struct rb_entry {
	struct rb_entry	 *rbt_parent;
	struct rb_entry	 *rbt_left;
	struct rb_entry	 *rbt_right;
	unsigned int	  rbt_color;
};

#define RBT_HEAD(_name, _type)						\
struct _name {								\
	struct rb_tree rbh_root;					\
}

#define RBT_ENTRY(_type)	struct rb_entry

static inline void
_rb_init(struct rb_tree *rbt)
{
	rbt->rbt_root = NULL;
}

static inline int
_rb_empty(struct rb_tree *rbt)
{
	return (rbt->rbt_root == NULL);
}

void	*_rb_insert(const struct rb_type *, struct rb_tree *, void *);
void	*_rb_remove(const struct rb_type *, struct rb_tree *, void *);
void	*_rb_find(const struct rb_type *, struct rb_tree *, const void *);
void	*_rb_nfind(const struct rb_type *, struct rb_tree *, const void *);
void	*_rb_root(const struct rb_type *, struct rb_tree *);
void	*_rb_min(const struct rb_type *, struct rb_tree *);
void	*_rb_max(const struct rb_type *, struct rb_tree *);
void	*_rb_next(const struct rb_type *, void *);
void	*_rb_prev(const struct rb_type *, void *);
void	*_rb_left(const struct rb_type *, void *);
void	*_rb_right(const struct rb_type *, void *);
void	*_rb_parent(const struct rb_type *, void *);
void	 _rb_set_left(const struct rb_type *, void *, void *);
void	 _rb_set_right(const struct rb_type *, void *, void *);
void	 _rb_set_parent(const struct rb_type *, void *, void *);
void	 _rb_poison(const struct rb_type *, void *, unsigned long);
int	 _rb_check(const struct rb_type *, void *, unsigned long);

#define RBT_INITIALIZER(_head)	{ { NULL } }

#define RBT_PROTOTYPE(_name, _type, _field, _cmp)			\
extern const struct rb_type *const _name##_RBT_TYPE;			\
									\
__unused static inline void						\
_name##_RBT_INIT(struct _name *head)					\
{									\
	_rb_init(&head->rbh_root);					\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_INSERT(struct _name *head, struct _type *elm)		\
{									\
	return _rb_insert(_name##_RBT_TYPE, &head->rbh_root, elm);	\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_REMOVE(struct _name *head, struct _type *elm)		\
{									\
	return _rb_remove(_name##_RBT_TYPE, &head->rbh_root, elm);	\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_FIND(struct _name *head, const struct _type *key)		\
{									\
	return _rb_find(_name##_RBT_TYPE, &head->rbh_root, key);	\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_NFIND(struct _name *head, const struct _type *key)		\
{									\
	return _rb_nfind(_name##_RBT_TYPE, &head->rbh_root, key);	\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_ROOT(struct _name *head)					\
{									\
	return _rb_root(_name##_RBT_TYPE, &head->rbh_root);		\
}									\
									\
__unused static inline int						\
_name##_RBT_EMPTY(struct _name *head)					\
{									\
	return _rb_empty(&head->rbh_root);				\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_MIN(struct _name *head)					\
{									\
	return _rb_min(_name##_RBT_TYPE, &head->rbh_root);		\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_MAX(struct _name *head)					\
{									\
	return _rb_max(_name##_RBT_TYPE, &head->rbh_root);		\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_NEXT(struct _type *elm)					\
{									\
	return _rb_next(_name##_RBT_TYPE, elm);				\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_PREV(struct _type *elm)					\
{									\
	return _rb_prev(_name##_RBT_TYPE, elm);				\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_LEFT(struct _type *elm)					\
{									\
	return _rb_left(_name##_RBT_TYPE, elm);				\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_RIGHT(struct _type *elm)					\
{									\
	return _rb_right(_name##_RBT_TYPE, elm);			\
}									\
									\
__unused static inline struct _type *					\
_name##_RBT_PARENT(struct _type *elm)					\
{									\
	return _rb_parent(_name##_RBT_TYPE, elm);			\
}									\
									\
__unused static inline void						\
_name##_RBT_SET_LEFT(struct _type *elm, struct _type *left)		\
{									\
	_rb_set_left(_name##_RBT_TYPE, elm, left);			\
}									\
									\
__unused static inline void						\
_name##_RBT_SET_RIGHT(struct _type *elm, struct _type *right)		\
{									\
	_rb_set_right(_name##_RBT_TYPE, elm, right);			\
}									\
									\
__unused static inline void						\
_name##_RBT_SET_PARENT(struct _type *elm, struct _type *parent)		\
{									\
	_rb_set_parent(_name##_RBT_TYPE, elm, parent);			\
}									\
									\
__unused static inline void						\
_name##_RBT_POISON(struct _type *elm, unsigned long poison)		\
{									\
	_rb_poison(_name##_RBT_TYPE, elm, poison);			\
}									\
									\
__unused static inline int						\
_name##_RBT_CHECK(struct _type *elm, unsigned long poison)		\
{									\
	return _rb_check(_name##_RBT_TYPE, elm, poison);		\
}

#define RBT_GENERATE_INTERNAL(_name, _type, _field, _cmp, _aug)		\
static int								\
_name##_RBT_COMPARE(const void *lptr, const void *rptr)			\
{									\
	const struct _type *l = lptr, *r = rptr;			\
	return _cmp(l, r);						\
}									\
static const struct rb_type _name##_RBT_INFO = {			\
	_name##_RBT_COMPARE,						\
	_aug,								\
	offsetof(struct _type, _field),					\
};									\
const struct rb_type *const _name##_RBT_TYPE = &_name##_RBT_INFO

#define RBT_GENERATE_AUGMENT(_name, _type, _field, _cmp, _aug)		\
static void								\
_name##_RBT_AUGMENT(void *ptr)						\
{									\
	struct _type *p = ptr;						\
	return _aug(p);							\
}									\
RBT_GENERATE_INTERNAL(_name, _type, _field, _cmp, _name##_RBT_AUGMENT)

#define RBT_GENERATE(_name, _type, _field, _cmp)			\
    RBT_GENERATE_INTERNAL(_name, _type, _field, _cmp, NULL)

#define RBT_INIT(_name, _head)		_name##_RBT_INIT(_head)
#define RBT_INSERT(_name, _head, _elm)	_name##_RBT_INSERT(_head, _elm)
#define RBT_REMOVE(_name, _head, _elm)	_name##_RBT_REMOVE(_head, _elm)
#define RBT_FIND(_name, _head, _key)	_name##_RBT_FIND(_head, _key)
#define RBT_NFIND(_name, _head, _key)	_name##_RBT_NFIND(_head, _key)
#define RBT_ROOT(_name, _head)		_name##_RBT_ROOT(_head)
#define RBT_EMPTY(_name, _head)		_name##_RBT_EMPTY(_head)
#define RBT_MIN(_name, _head)		_name##_RBT_MIN(_head)
#define RBT_MAX(_name, _head)		_name##_RBT_MAX(_head)
#define RBT_NEXT(_name, _elm)		_name##_RBT_NEXT(_elm)
#define RBT_PREV(_name, _elm)		_name##_RBT_PREV(_elm)
#define RBT_LEFT(_name, _elm)		_name##_RBT_LEFT(_elm)
#define RBT_RIGHT(_name, _elm)		_name##_RBT_RIGHT(_elm)
#define RBT_PARENT(_name, _elm)		_name##_RBT_PARENT(_elm)
#define RBT_SET_LEFT(_name, _elm, _l)	_name##_RBT_SET_LEFT(_elm, _l)
#define RBT_SET_RIGHT(_name, _elm, _r)	_name##_RBT_SET_RIGHT(_elm, _r)
#define RBT_SET_PARENT(_name, _elm, _p)	_name##_RBT_SET_PARENT(_elm, _p)
#define RBT_POISON(_name, _elm, _p)	_name##_RBT_POISON(_elm, _p)
#define RBT_CHECK(_name, _elm, _p)	_name##_RBT_CHECK(_elm, _p)

#define RBT_FOREACH(_e, _name, _head)					\
	for ((_e) = RBT_MIN(_name, (_head));				\
	     (_e) != NULL;						\
	     (_e) = RBT_NEXT(_name, (_e)))

#define RBT_FOREACH_SAFE(_e, _name, _head, _n)				\
	for ((_e) = RBT_MIN(_name, (_head));				\
	     (_e) != NULL && ((_n) = RBT_NEXT(_name, (_e)), 1);	\
	     (_e) = (_n))

#define RBT_FOREACH_REVERSE(_e, _name, _head)				\
	for ((_e) = RBT_MAX(_name, (_head));				\
	     (_e) != NULL;						\
	     (_e) = RBT_PREV(_name, (_e)))

#define RBT_FOREACH_REVERSE_SAFE(_e, _name, _head, _n)			\
	for ((_e) = RBT_MAX(_name, (_head));				\
	     (_e) != NULL && ((_n) = RBT_PREV(_name, (_e)), 1);	\
	     (_e) = (_n))

#endif	/* _SYS_TREE_H_ */
