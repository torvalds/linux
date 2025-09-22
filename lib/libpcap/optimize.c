/*	$OpenBSD: optimize.c,v 1.23 2024/04/08 02:51:14 jsg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *  Optimization module for tcpdump intermediate representation.
 */

#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "pcap-int.h"

#include "gencode.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#ifdef BDEBUG
extern int dflag;
#endif

#define A_ATOM BPF_MEMWORDS
#define X_ATOM (BPF_MEMWORDS+1)

#define NOP -1

/*
 * This define is used to represent *both* the accumulator and
 * x register in use-def computations.
 * Currently, the use-def code assumes only one definition per instruction.
 */
#define AX_ATOM N_ATOMS

/*
 * A flag to indicate that further optimization is needed.
 * Iterative passes are continued until a given pass yields no
 * branch movement.
 */
static int done;

/*
 * A block is marked if only if its mark equals the current mark.
 * Rather than traverse the code array, marking each item, 'cur_mark' is
 * incremented.  This automatically makes each element unmarked.
 */
static int cur_mark;
#define isMarked(p) ((p)->mark == cur_mark)
#define unMarkAll() cur_mark += 1
#define Mark(p) ((p)->mark = cur_mark)

static void opt_init(struct block *);
static void opt_cleanup(void);

static void make_marks(struct block *);
static void mark_code(struct block *);

static void intern_blocks(struct block *);

static int eq_slist(struct slist *, struct slist *);

static void find_levels_r(struct block *);

static void find_levels(struct block *);
static void find_dom(struct block *);
static void propedom(struct edge *);
static void find_edom(struct block *);
static void find_closure(struct block *);
static int atomuse(struct stmt *);
static int atomdef(struct stmt *);
static void compute_local_ud(struct block *);
static void find_ud(struct block *);
static void init_val(void);
static int F(int, int, int);
static __inline void vstore(struct stmt *, int *, int, int);
static void opt_blk(struct block *, int);
static int use_conflict(struct block *, struct block *);
static void opt_j(struct edge *);
static void or_pullup(struct block *);
static void and_pullup(struct block *);
static void opt_blks(struct block *, int);
static __inline void link_inedge(struct edge *, struct block *);
static void find_inedges(struct block *);
static void opt_root(struct block **);
static void opt_loop(struct block *, int);
static void fold_op(struct stmt *, int, int);
static __inline struct slist *this_op(struct slist *);
static void opt_not(struct block *);
static void opt_peep(struct block *);
static void opt_stmt(struct stmt *, int[], int);
static void deadstmt(struct stmt *, struct stmt *[]);
static void opt_deadstores(struct block *);
static void opt_blk(struct block *, int);
static int use_conflict(struct block *, struct block *);
static void opt_j(struct edge *);
static struct block *fold_edge(struct block *, struct edge *);
static __inline int eq_blk(struct block *, struct block *);
static int slength(struct slist *);
static int count_blocks(struct block *);
static void number_blks_r(struct block *);
static int count_stmts(struct block *);
static int convert_code_r(struct block *);
#ifdef BDEBUG
static void opt_dump(struct block *);
#endif

static int n_blocks;
struct block **blocks;
static int n_edges;
struct edge **edges;

/*
 * A bit vector set representation of the dominators.
 * We round up the set size to the next power of two.
 */
static int nodewords;
static int edgewords;
struct block **levels;
bpf_u_int32 *space1;
bpf_u_int32 *space2;
#define BITS_PER_WORD (8*sizeof(bpf_u_int32))
/*
 * True if a is in uset {p}
 */
#define SET_MEMBER(p, a) \
((p)[(unsigned)(a) / BITS_PER_WORD] & (1 << ((unsigned)(a) % BITS_PER_WORD)))

/*
 * Add 'a' to uset p.
 */
#define SET_INSERT(p, a) \
(p)[(unsigned)(a) / BITS_PER_WORD] |= (1 << ((unsigned)(a) % BITS_PER_WORD))

/*
 * Delete 'a' from uset p.
 */
#define SET_DELETE(p, a) \
(p)[(unsigned)(a) / BITS_PER_WORD] &= ~(1 << ((unsigned)(a) % BITS_PER_WORD))

/*
 * a := a intersect b
 */
#define SET_INTERSECT(a, b, n)\
{\
	bpf_u_int32 *_x = a, *_y = b;\
	int _n = n;\
	while (--_n >= 0) *_x++ &= *_y++;\
}

/*
 * a := a - b
 */
#define SET_SUBTRACT(a, b, n)\
{\
	bpf_u_int32 *_x = a, *_y = b;\
	int _n = n;\
	while (--_n >= 0) *_x++ &=~ *_y++;\
}

/*
 * a := a union b
 */
#define SET_UNION(a, b, n)\
{\
	bpf_u_int32 *_x = a, *_y = b;\
	int _n = n;\
	while (--_n >= 0) *_x++ |= *_y++;\
}

static uset all_dom_sets;
static uset all_closure_sets;
static uset all_edge_sets;

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

static void
find_levels_r(struct block *b)
{
	int level;

	if (isMarked(b))
		return;

	Mark(b);
	b->link = 0;

	if (JT(b)) {
		find_levels_r(JT(b));
		find_levels_r(JF(b));
		level = MAX(JT(b)->level, JF(b)->level) + 1;
	} else
		level = 0;
	b->level = level;
	b->link = levels[level];
	levels[level] = b;
}

/*
 * Level graph.  The levels go from 0 at the leaves to
 * N_LEVELS at the root.  The levels[] array points to the
 * first node of the level list, whose elements are linked
 * with the 'link' field of the struct block.
 */
static void
find_levels(struct block *root)
{
	memset((char *)levels, 0, n_blocks * sizeof(*levels));
	unMarkAll();
	find_levels_r(root);
}

/*
 * Find dominator relationships.
 * Assumes graph has been leveled.
 */
static void
find_dom(struct block *root)
{
	int i;
	struct block *b;
	bpf_u_int32 *x;

	/*
	 * Initialize sets to contain all nodes.
	 */
	x = all_dom_sets;
	i = n_blocks * nodewords;
	while (--i >= 0)
		*x++ = ~0;
	/* Root starts off empty. */
	for (i = nodewords; --i >= 0;)
		root->dom[i] = 0;

	/* root->level is the highest level no found. */
	for (i = root->level; i >= 0; --i) {
		for (b = levels[i]; b; b = b->link) {
			SET_INSERT(b->dom, b->id);
			if (JT(b) == 0)
				continue;
			SET_INTERSECT(JT(b)->dom, b->dom, nodewords);
			SET_INTERSECT(JF(b)->dom, b->dom, nodewords);
		}
	}
}

static void
propedom(struct edge *ep)
{
	SET_INSERT(ep->edom, ep->id);
	if (ep->succ) {
		SET_INTERSECT(ep->succ->et.edom, ep->edom, edgewords);
		SET_INTERSECT(ep->succ->ef.edom, ep->edom, edgewords);
	}
}

/*
 * Compute edge dominators.
 * Assumes graph has been leveled and predecessors established.
 */
static void
find_edom(struct block *root)
{
	int i;
	uset x;
	struct block *b;

	x = all_edge_sets;
	for (i = n_edges * edgewords; --i >= 0; )
		x[i] = ~0;

	/* root->level is the highest level no found. */
	memset(root->et.edom, 0, edgewords * sizeof(*(uset)0));
	memset(root->ef.edom, 0, edgewords * sizeof(*(uset)0));
	for (i = root->level; i >= 0; --i) {
		for (b = levels[i]; b != 0; b = b->link) {
			propedom(&b->et);
			propedom(&b->ef);
		}
	}
}

/*
 * Find the backwards transitive closure of the flow graph.  These sets
 * are backwards in the sense that we find the set of nodes that reach
 * a given node, not the set of nodes that can be reached by a node.
 *
 * Assumes graph has been leveled.
 */
static void
find_closure(struct block *root)
{
	int i;
	struct block *b;

	/*
	 * Initialize sets to contain no nodes.
	 */
	memset((char *)all_closure_sets, 0,
	      n_blocks * nodewords * sizeof(*all_closure_sets));

	/* root->level is the highest level no found. */
	for (i = root->level; i >= 0; --i) {
		for (b = levels[i]; b; b = b->link) {
			SET_INSERT(b->closure, b->id);
			if (JT(b) == 0)
				continue;
			SET_UNION(JT(b)->closure, b->closure, nodewords);
			SET_UNION(JF(b)->closure, b->closure, nodewords);
		}
	}
}

/*
 * Return the register number that is used by s.  If A and X are both
 * used, return AX_ATOM.  If no register is used, return -1.
 *
 * The implementation should probably change to an array access.
 */
static int
atomuse(struct stmt *s)
{
	int c = s->code;

	if (c == NOP)
		return -1;

	switch (BPF_CLASS(c)) {

	case BPF_RET:
		return (BPF_RVAL(c) == BPF_A) ? A_ATOM :
			(BPF_RVAL(c) == BPF_X) ? X_ATOM : -1;

	case BPF_LD:
	case BPF_LDX:
		return (BPF_MODE(c) == BPF_IND) ? X_ATOM :
			(BPF_MODE(c) == BPF_MEM) ? s->k : -1;

	case BPF_ST:
		return A_ATOM;

	case BPF_STX:
		return X_ATOM;

	case BPF_JMP:
	case BPF_ALU:
		if (BPF_SRC(c) == BPF_X)
			return AX_ATOM;
		return A_ATOM;

	case BPF_MISC:
		return BPF_MISCOP(c) == BPF_TXA ? X_ATOM : A_ATOM;
	}
	abort();
	/* NOTREACHED */
}

/*
 * Return the register number that is defined by 's'.  We assume that
 * a single stmt cannot define more than one register.  If no register
 * is defined, return -1.
 *
 * The implementation should probably change to an array access.
 */
static int
atomdef(struct stmt *s)
{
	if (s->code == NOP)
		return -1;

	switch (BPF_CLASS(s->code)) {

	case BPF_LD:
	case BPF_ALU:
		return A_ATOM;

	case BPF_LDX:
		return X_ATOM;

	case BPF_ST:
	case BPF_STX:
		return s->k;

	case BPF_MISC:
		return BPF_MISCOP(s->code) == BPF_TAX ? X_ATOM : A_ATOM;
	}
	return -1;
}

static void
compute_local_ud(struct block *b)
{
	struct slist *s;
	atomset def = 0, use = 0, kill = 0;
	int atom;

	for (s = b->stmts; s; s = s->next) {
		if (s->s.code == NOP)
			continue;
		atom = atomuse(&s->s);
		if (atom >= 0) {
			if (atom == AX_ATOM) {
				if (!ATOMELEM(def, X_ATOM))
					use |= ATOMMASK(X_ATOM);
				if (!ATOMELEM(def, A_ATOM))
					use |= ATOMMASK(A_ATOM);
			}
			else if (atom < N_ATOMS) {
				if (!ATOMELEM(def, atom))
					use |= ATOMMASK(atom);
			}
			else
				abort();
		}
		atom = atomdef(&s->s);
		if (atom >= 0) {
			if (!ATOMELEM(use, atom))
				kill |= ATOMMASK(atom);
			def |= ATOMMASK(atom);
		}
	}
	if (!ATOMELEM(def, A_ATOM) && BPF_CLASS(b->s.code) == BPF_JMP)
		use |= ATOMMASK(A_ATOM);

	b->def = def;
	b->kill = kill;
	b->in_use = use;
}

/*
 * Assume graph is already leveled.
 */
static void
find_ud(struct block *root)
{
	int i, maxlevel;
	struct block *p;

	/*
	 * root->level is the highest level no found;
	 * count down from there.
	 */
	maxlevel = root->level;
	for (i = maxlevel; i >= 0; --i)
		for (p = levels[i]; p; p = p->link) {
			compute_local_ud(p);
			p->out_use = 0;
		}

	for (i = 1; i <= maxlevel; ++i) {
		for (p = levels[i]; p; p = p->link) {
			p->out_use |= JT(p)->in_use | JF(p)->in_use;
			p->in_use |= p->out_use &~ p->kill;
		}
	}
}

/*
 * These data structures are used in a Cocke and Shwarz style
 * value numbering scheme.  Since the flowgraph is acyclic,
 * exit values can be propagated from a node's predecessors
 * provided it is uniquely defined.
 */
struct valnode {
	int code;
	int v0, v1;
	int val;
	struct valnode *next;
};

#define MODULUS 213
static struct valnode *hashtbl[MODULUS];
static int curval;
static int maxval;

/* Integer constants mapped with the load immediate opcode. */
#define K(i) F(BPF_LD|BPF_IMM|BPF_W, i, 0L)

struct vmapinfo {
	int is_const;
	bpf_int32 const_val;
};

struct vmapinfo *vmap;
struct valnode *vnode_base;
struct valnode *next_vnode;

static void
init_val(void)
{
	curval = 0;
	next_vnode = vnode_base;
	memset((char *)vmap, 0, maxval * sizeof(*vmap));
	memset((char *)hashtbl, 0, sizeof hashtbl);
}

/* Because we really don't have an IR, this stuff is a little messy. */
static int
F(int code, int v0, int v1)
{
	u_int hash;
	int val;
	struct valnode *p;

	hash = (u_int)code ^ (v0 << 4) ^ (v1 << 8);
	hash %= MODULUS;

	for (p = hashtbl[hash]; p; p = p->next)
		if (p->code == code && p->v0 == v0 && p->v1 == v1)
			return p->val;

	val = ++curval;
	if (BPF_MODE(code) == BPF_IMM &&
	    (BPF_CLASS(code) == BPF_LD || BPF_CLASS(code) == BPF_LDX)) {
		vmap[val].const_val = v0;
		vmap[val].is_const = 1;
	}
	p = next_vnode++;
	p->val = val;
	p->code = code;
	p->v0 = v0;
	p->v1 = v1;
	p->next = hashtbl[hash];
	hashtbl[hash] = p;

	return val;
}

static __inline void
vstore(struct stmt *s, int *valp, int newval, int alter)
{
	if (alter && *valp == newval)
		s->code = NOP;
	else
		*valp = newval;
}

static void
fold_op(struct stmt *s, int v0, int v1)
{
	bpf_int32 a, b;

	a = vmap[v0].const_val;
	b = vmap[v1].const_val;

	switch (BPF_OP(s->code)) {
	case BPF_ADD:
		a += b;
		break;

	case BPF_SUB:
		a -= b;
		break;

	case BPF_MUL:
		a *= b;
		break;

	case BPF_DIV:
		if (b == 0)
			bpf_error("division by zero");
		a /= b;
		break;

	case BPF_AND:
		a &= b;
		break;

	case BPF_OR:
		a |= b;
		break;

	case BPF_LSH:
		a <<= b;
		break;

	case BPF_RSH:
		a >>= b;
		break;

	case BPF_NEG:
		a = -a;
		break;

	default:
		abort();
	}
	s->k = a;
	s->code = BPF_LD|BPF_IMM;
	done = 0;
}

static __inline struct slist *
this_op(struct slist *s)
{
	while (s != 0 && s->s.code == NOP)
		s = s->next;
	return s;
}

static void
opt_not(struct block *b)
{
	struct block *tmp = JT(b);

	JT(b) = JF(b);
	JF(b) = tmp;
}

static void
opt_peep(struct block *b)
{
	struct slist *s;
	struct slist *next, *last;
	int val;

	s = b->stmts;
	if (s == 0)
		return;

	last = s;
	while (1) {
		s = this_op(s);
		if (s == 0)
			break;
		next = this_op(s->next);
		if (next == 0)
			break;
		last = next;

		/*
		 * st  M[k]	-->	st  M[k]
		 * ldx M[k]		tax
		 */
		if (s->s.code == BPF_ST &&
		    next->s.code == (BPF_LDX|BPF_MEM) &&
		    s->s.k == next->s.k) {
			done = 0;
			next->s.code = BPF_MISC|BPF_TAX;
		}
		/*
		 * ld  #k	-->	ldx  #k
		 * tax			txa
		 */
		if (s->s.code == (BPF_LD|BPF_IMM) &&
		    next->s.code == (BPF_MISC|BPF_TAX)) {
			s->s.code = BPF_LDX|BPF_IMM;
			next->s.code = BPF_MISC|BPF_TXA;
			done = 0;
		}
		/*
		 * This is an ugly special case, but it happens
		 * when you say tcp[k] or udp[k] where k is a constant.
		 */
		if (s->s.code == (BPF_LD|BPF_IMM)) {
			struct slist *add, *tax, *ild;

			/*
			 * Check that X isn't used on exit from this
			 * block (which the optimizer might cause).
			 * We know the code generator won't generate
			 * any local dependencies.
			 */
			if (ATOMELEM(b->out_use, X_ATOM))
				break;

			if (next->s.code != (BPF_LDX|BPF_MSH|BPF_B))
				add = next;
			else
				add = this_op(next->next);
			if (add == 0 || add->s.code != (BPF_ALU|BPF_ADD|BPF_X))
				break;

			tax = this_op(add->next);
			if (tax == 0 || tax->s.code != (BPF_MISC|BPF_TAX))
				break;

			ild = this_op(tax->next);
			if (ild == 0 || BPF_CLASS(ild->s.code) != BPF_LD ||
			    BPF_MODE(ild->s.code) != BPF_IND)
				break;
			/*
			 * XXX We need to check that X is not
			 * subsequently used.  We know we can eliminate the
			 * accumulator modifications since it is defined
			 * by the last stmt of this sequence.
			 *
			 * We want to turn this sequence:
			 *
			 * (004) ldi     #0x2		{s}
			 * (005) ldxms   [14]		{next}  -- optional
			 * (006) addx			{add}
			 * (007) tax			{tax}
			 * (008) ild     [x+0]		{ild}
			 *
			 * into this sequence:
			 *
			 * (004) nop
			 * (005) ldxms   [14]
			 * (006) nop
			 * (007) nop
			 * (008) ild     [x+2]
			 *
			 */
			ild->s.k += s->s.k;
			s->s.code = NOP;
			add->s.code = NOP;
			tax->s.code = NOP;
			done = 0;
		}
		s = next;
	}
	/*
	 * If we have a subtract to do a comparison, and the X register
	 * is a known constant, we can merge this value into the
	 * comparison.
	 */
	if (last->s.code == (BPF_ALU|BPF_SUB|BPF_X) &&
	    !ATOMELEM(b->out_use, A_ATOM)) {
		val = b->val[X_ATOM];
		if (vmap[val].is_const) {
			int op;

			b->s.k += vmap[val].const_val;
			op = BPF_OP(b->s.code);
			if (op == BPF_JGT || op == BPF_JGE) {
				struct block *t = JT(b);
				JT(b) = JF(b);
				JF(b) = t;
				b->s.k += 0x80000000;
			}
			last->s.code = NOP;
			done = 0;
		} else if (b->s.k == 0) {
			/*
			 * sub x  ->	nop
			 * j  #0	j  x
			 */
			last->s.code = NOP;
			b->s.code = BPF_CLASS(b->s.code) | BPF_OP(b->s.code) |
				BPF_X;
			done = 0;
		}
	}
	/*
	 * Likewise, a constant subtract can be simplified.
	 */
	else if (last->s.code == (BPF_ALU|BPF_SUB|BPF_K) &&
		 !ATOMELEM(b->out_use, A_ATOM)) {
		int op;

		b->s.k += last->s.k;
		last->s.code = NOP;
		op = BPF_OP(b->s.code);
		if (op == BPF_JGT || op == BPF_JGE) {
			struct block *t = JT(b);
			JT(b) = JF(b);
			JF(b) = t;
			b->s.k += 0x80000000;
		}
		done = 0;
	}
	/*
	 * and #k	nop
	 * jeq #0  ->	jset #k
	 */
	if (last->s.code == (BPF_ALU|BPF_AND|BPF_K) &&
	    !ATOMELEM(b->out_use, A_ATOM) && b->s.k == 0) {
		b->s.k = last->s.k;
		b->s.code = BPF_JMP|BPF_K|BPF_JSET;
		last->s.code = NOP;
		done = 0;
		opt_not(b);
	}
	/*
	 * If the accumulator is a known constant, we can compute the
	 * comparison result.
	 */
	val = b->val[A_ATOM];
	if (vmap[val].is_const && BPF_SRC(b->s.code) == BPF_K) {
		bpf_int32 v = vmap[val].const_val;
		switch (BPF_OP(b->s.code)) {

		case BPF_JEQ:
			v = v == b->s.k;
			break;

		case BPF_JGT:
			v = (unsigned)v > b->s.k;
			break;

		case BPF_JGE:
			v = (unsigned)v >= b->s.k;
			break;

		case BPF_JSET:
			v &= b->s.k;
			break;

		default:
			abort();
		}
		if (JF(b) != JT(b))
			done = 0;
		if (v)
			JF(b) = JT(b);
		else
			JT(b) = JF(b);
	}
}

/*
 * Compute the symbolic value of expression of 's', and update
 * anything it defines in the value table 'val'.  If 'alter' is true,
 * do various optimizations.  This code would be cleaner if symbolic
 * evaluation and code transformations weren't folded together.
 */
static void
opt_stmt(struct stmt *s, int val[], int alter)
{
	int op;
	int v;

	switch (s->code) {

	case BPF_LD|BPF_ABS|BPF_W:
	case BPF_LD|BPF_ABS|BPF_H:
	case BPF_LD|BPF_ABS|BPF_B:
		v = F(s->code, s->k, 0L);
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_LD|BPF_IND|BPF_W:
	case BPF_LD|BPF_IND|BPF_H:
	case BPF_LD|BPF_IND|BPF_B:
		v = val[X_ATOM];
		if (alter && vmap[v].is_const) {
			s->code = BPF_LD|BPF_ABS|BPF_SIZE(s->code);
			s->k += vmap[v].const_val;
			v = F(s->code, s->k, 0L);
			done = 0;
		}
		else
			v = F(s->code, s->k, v);
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_LD|BPF_LEN:
	case BPF_LD|BPF_RND:
		v = F(s->code, 0L, 0L);
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_LD|BPF_IMM:
		v = K(s->k);
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_LDX|BPF_IMM:
		v = K(s->k);
		vstore(s, &val[X_ATOM], v, alter);
		break;

	case BPF_LDX|BPF_MSH|BPF_B:
		v = F(s->code, s->k, 0L);
		vstore(s, &val[X_ATOM], v, alter);
		break;

	case BPF_ALU|BPF_NEG:
		if (alter && vmap[val[A_ATOM]].is_const) {
			s->code = BPF_LD|BPF_IMM;
			s->k = -vmap[val[A_ATOM]].const_val;
			val[A_ATOM] = K(s->k);
		}
		else
			val[A_ATOM] = F(s->code, val[A_ATOM], 0L);
		break;

	case BPF_ALU|BPF_ADD|BPF_K:
	case BPF_ALU|BPF_SUB|BPF_K:
	case BPF_ALU|BPF_MUL|BPF_K:
	case BPF_ALU|BPF_DIV|BPF_K:
	case BPF_ALU|BPF_AND|BPF_K:
	case BPF_ALU|BPF_OR|BPF_K:
	case BPF_ALU|BPF_LSH|BPF_K:
	case BPF_ALU|BPF_RSH|BPF_K:
		op = BPF_OP(s->code);
		if (alter) {
			if (s->k == 0) {
				if (op == BPF_ADD || op == BPF_SUB ||
				    op == BPF_LSH || op == BPF_RSH ||
				    op == BPF_OR) {
					s->code = NOP;
					break;
				}
				if (op == BPF_MUL || op == BPF_AND) {
					s->code = BPF_LD|BPF_IMM;
					val[A_ATOM] = K(s->k);
					break;
				}
			}
			if (vmap[val[A_ATOM]].is_const) {
				fold_op(s, val[A_ATOM], K(s->k));
				val[A_ATOM] = K(s->k);
				break;
			}
		}
		val[A_ATOM] = F(s->code, val[A_ATOM], K(s->k));
		break;

	case BPF_ALU|BPF_ADD|BPF_X:
	case BPF_ALU|BPF_SUB|BPF_X:
	case BPF_ALU|BPF_MUL|BPF_X:
	case BPF_ALU|BPF_DIV|BPF_X:
	case BPF_ALU|BPF_AND|BPF_X:
	case BPF_ALU|BPF_OR|BPF_X:
	case BPF_ALU|BPF_LSH|BPF_X:
	case BPF_ALU|BPF_RSH|BPF_X:
		op = BPF_OP(s->code);
		if (alter && vmap[val[X_ATOM]].is_const) {
			if (vmap[val[A_ATOM]].is_const) {
				fold_op(s, val[A_ATOM], val[X_ATOM]);
				val[A_ATOM] = K(s->k);
			}
			else {
				s->code = BPF_ALU|BPF_K|op;
				s->k = vmap[val[X_ATOM]].const_val;
				done = 0;
				val[A_ATOM] =
					F(s->code, val[A_ATOM], K(s->k));
			}
			break;
		}
		/*
		 * Check if we're doing something to an accumulator
		 * that is 0, and simplify.  This may not seem like
		 * much of a simplification but it could open up further
		 * optimizations.
		 * XXX We could also check for mul by 1, and -1, etc.
		 */
		if (alter && vmap[val[A_ATOM]].is_const
		    && vmap[val[A_ATOM]].const_val == 0) {
			if (op == BPF_ADD || op == BPF_OR ||
			    op == BPF_LSH || op == BPF_RSH || op == BPF_SUB) {
				s->code = BPF_MISC|BPF_TXA;
				vstore(s, &val[A_ATOM], val[X_ATOM], alter);
				break;
			}
			else if (op == BPF_MUL || op == BPF_DIV ||
				 op == BPF_AND) {
				s->code = BPF_LD|BPF_IMM;
				s->k = 0;
				vstore(s, &val[A_ATOM], K(s->k), alter);
				break;
			}
			else if (op == BPF_NEG) {
				s->code = NOP;
				break;
			}
		}
		val[A_ATOM] = F(s->code, val[A_ATOM], val[X_ATOM]);
		break;

	case BPF_MISC|BPF_TXA:
		vstore(s, &val[A_ATOM], val[X_ATOM], alter);
		break;

	case BPF_LD|BPF_MEM:
		v = val[s->k];
		if (alter && vmap[v].is_const) {
			s->code = BPF_LD|BPF_IMM;
			s->k = vmap[v].const_val;
			done = 0;
		}
		vstore(s, &val[A_ATOM], v, alter);
		break;

	case BPF_MISC|BPF_TAX:
		vstore(s, &val[X_ATOM], val[A_ATOM], alter);
		break;

	case BPF_LDX|BPF_MEM:
		v = val[s->k];
		if (alter && vmap[v].is_const) {
			s->code = BPF_LDX|BPF_IMM;
			s->k = vmap[v].const_val;
			done = 0;
		}
		vstore(s, &val[X_ATOM], v, alter);
		break;

	case BPF_ST:
		vstore(s, &val[s->k], val[A_ATOM], alter);
		break;

	case BPF_STX:
		vstore(s, &val[s->k], val[X_ATOM], alter);
		break;
	}
}

static void
deadstmt(struct stmt *s, struct stmt *last[])
{
	int atom;

	atom = atomuse(s);
	if (atom >= 0) {
		if (atom == AX_ATOM) {
			last[X_ATOM] = 0;
			last[A_ATOM] = 0;
		}
		else
			last[atom] = 0;
	}
	atom = atomdef(s);
	if (atom >= 0) {
		if (last[atom]) {
			done = 0;
			last[atom]->code = NOP;
		}
		last[atom] = s;
	}
}

static void
opt_deadstores(struct block *b)
{
	struct slist *s;
	int atom;
	struct stmt *last[N_ATOMS];

	memset((char *)last, 0, sizeof last);

	for (s = b->stmts; s != 0; s = s->next)
		deadstmt(&s->s, last);
	deadstmt(&b->s, last);

	for (atom = 0; atom < N_ATOMS; ++atom)
		if (last[atom] && !ATOMELEM(b->out_use, atom)) {
			last[atom]->code = NOP;
			done = 0;
		}
}

static void
opt_blk(struct block *b, int do_stmts)
{
	struct slist *s;
	struct edge *p;
	int i;
	bpf_int32 aval;

#if 0
	for (s = b->stmts; s && s->next; s = s->next)
		if (BPF_CLASS(s->s.code) == BPF_JMP) {
			do_stmts = 0;
			break;
		}
#endif

	/*
	 * Initialize the atom values.
	 * If we have no predecessors, everything is undefined.
	 * Otherwise, we inherent our values from our predecessors.
	 * If any register has an ambiguous value (i.e. control paths are
	 * merging) give it the undefined value of 0.
	 */
	p = b->in_edges;
	if (p == 0)
		memset((char *)b->val, 0, sizeof(b->val));
	else {
		memcpy((char *)b->val, (char *)p->pred->val, sizeof(b->val));
		while ((p = p->next) != NULL) {
			for (i = 0; i < N_ATOMS; ++i)
				if (b->val[i] != p->pred->val[i])
					b->val[i] = 0;
		}
	}
	aval = b->val[A_ATOM];
	for (s = b->stmts; s; s = s->next)
		opt_stmt(&s->s, b->val, do_stmts);

	/*
	 * This is a special case: if we don't use anything from this
	 * block, and we load the accumulator with value that is
	 * already there, or if this block is a return,
	 * eliminate all the statements.
	 */
	if (do_stmts && 
	    ((b->out_use == 0 && aval != 0 &&b->val[A_ATOM] == aval) ||
	     BPF_CLASS(b->s.code) == BPF_RET)) {
		if (b->stmts != 0) {
			b->stmts = 0;
			done = 0;
		}
	} else {
		opt_peep(b);
		opt_deadstores(b);
	}
	/*
	 * Set up values for branch optimizer.
	 */
	if (BPF_SRC(b->s.code) == BPF_K)
		b->oval = K(b->s.k);
	else
		b->oval = b->val[X_ATOM];
	b->et.code = b->s.code;
	b->ef.code = -b->s.code;
}

/*
 * Return true if any register that is used on exit from 'succ', has
 * an exit value that is different from the corresponding exit value
 * from 'b'.
 */
static int
use_conflict(struct block *b, struct block *succ)
{
	int atom;
	atomset use = succ->out_use;

	if (use == 0)
		return 0;

	for (atom = 0; atom < N_ATOMS; ++atom)
		if (ATOMELEM(use, atom))
			if (b->val[atom] != succ->val[atom])
				return 1;
	return 0;
}

static struct block *
fold_edge(struct block *child, struct edge *ep)
{
	int sense;
	int aval0, aval1, oval0, oval1;
	int code = ep->code;

	if (code < 0) {
		code = -code;
		sense = 0;
	} else
		sense = 1;

	if (child->s.code != code)
		return 0;

	aval0 = child->val[A_ATOM];
	oval0 = child->oval;
	aval1 = ep->pred->val[A_ATOM];
	oval1 = ep->pred->oval;

	if (aval0 != aval1)
		return 0;

	if (oval0 == oval1)
		/*
		 * The operands are identical, so the
		 * result is true if a true branch was
		 * taken to get here, otherwise false.
		 */
		return sense ? JT(child) : JF(child);

	if (sense && code == (BPF_JMP|BPF_JEQ|BPF_K))
		/*
		 * At this point, we only know the comparison if we
		 * came down the true branch, and it was an equality
		 * comparison with a constant.  We rely on the fact that
		 * distinct constants have distinct value numbers.
		 */
		return JF(child);

	return 0;
}

static void
opt_j(struct edge *ep)
{
	int i, k;
	struct block *target;

	if (JT(ep->succ) == 0)
		return;

	if (JT(ep->succ) == JF(ep->succ)) {
		/*
		 * Common branch targets can be eliminated, provided
		 * there is no data dependency.
		 */
		if (!use_conflict(ep->pred, ep->succ->et.succ)) {
			done = 0;
			ep->succ = JT(ep->succ);
		}
	}
	/*
	 * For each edge dominator that matches the successor of this
	 * edge, promote the edge successor to the its grandchild.
	 *
	 * XXX We violate the set abstraction here in favor a reasonably
	 * efficient loop.
	 */
 top:
	for (i = 0; i < edgewords; ++i) {
		bpf_u_int32 x = ep->edom[i];

		while (x != 0) {
			k = ffs(x) - 1;
			x &=~ (1 << k);
			k += i * BITS_PER_WORD;

			target = fold_edge(ep->succ, edges[k]);
			/*
			 * Check that there is no data dependency between
			 * nodes that will be violated if we move the edge.
			 */
			if (target != 0 && !use_conflict(ep->pred, target)) {
				done = 0;
				ep->succ = target;
				if (JT(target) != 0)
					/*
					 * Start over unless we hit a leaf.
					 */
					goto top;
				return;
			}
		}
	}
}


static void
or_pullup(struct block *b)
{
	int val, at_top;
	struct block *pull;
	struct block **diffp, **samep;
	struct edge *ep;

	ep = b->in_edges;
	if (ep == 0)
		return;

	/*
	 * Make sure each predecessor loads the same value.
	 * XXX why?
	 */
	val = ep->pred->val[A_ATOM];
	for (ep = ep->next; ep != 0; ep = ep->next)
		if (val != ep->pred->val[A_ATOM])
			return;

	if (JT(b->in_edges->pred) == b)
		diffp = &JT(b->in_edges->pred);
	else
		diffp = &JF(b->in_edges->pred);

	at_top = 1;
	while (1) {
		if (*diffp == 0)
			return;

		if (JT(*diffp) != JT(b))
			return;

		if (!SET_MEMBER((*diffp)->dom, b->id))
			return;

		if ((*diffp)->val[A_ATOM] != val)
			break;

		diffp = &JF(*diffp);
		at_top = 0;
	}
	samep = &JF(*diffp);
	while (1) {
		if (*samep == 0)
			return;

		if (JT(*samep) != JT(b))
			return;

		if (!SET_MEMBER((*samep)->dom, b->id))
			return;

		if ((*samep)->val[A_ATOM] == val)
			break;

		/* XXX Need to check that there are no data dependencies
		   between dp0 and dp1.  Currently, the code generator
		   will not produce such dependencies. */
		samep = &JF(*samep);
	}
#ifdef notdef
	/* XXX This doesn't cover everything. */
	for (i = 0; i < N_ATOMS; ++i)
		if ((*samep)->val[i] != pred->val[i])
			return;
#endif
	/* Pull up the node. */
	pull = *samep;
	*samep = JF(pull);
	JF(pull) = *diffp;

	/*
	 * At the top of the chain, each predecessor needs to point at the
	 * pulled up node.  Inside the chain, there is only one predecessor
	 * to worry about.
	 */
	if (at_top) {
		for (ep = b->in_edges; ep != 0; ep = ep->next) {
			if (JT(ep->pred) == b)
				JT(ep->pred) = pull;
			else
				JF(ep->pred) = pull;
		}
	}
	else
		*diffp = pull;

	done = 0;
}

static void
and_pullup(struct block *b)
{
	int val, at_top;
	struct block *pull;
	struct block **diffp, **samep;
	struct edge *ep;

	ep = b->in_edges;
	if (ep == 0)
		return;

	/*
	 * Make sure each predecessor loads the same value.
	 */
	val = ep->pred->val[A_ATOM];
	for (ep = ep->next; ep != 0; ep = ep->next)
		if (val != ep->pred->val[A_ATOM])
			return;

	if (JT(b->in_edges->pred) == b)
		diffp = &JT(b->in_edges->pred);
	else
		diffp = &JF(b->in_edges->pred);

	at_top = 1;
	while (1) {
		if (*diffp == 0)
			return;

		if (JF(*diffp) != JF(b))
			return;

		if (!SET_MEMBER((*diffp)->dom, b->id))
			return;

		if ((*diffp)->val[A_ATOM] != val)
			break;

		diffp = &JT(*diffp);
		at_top = 0;
	}
	samep = &JT(*diffp);
	while (1) {
		if (*samep == 0)
			return;

		if (JF(*samep) != JF(b))
			return;

		if (!SET_MEMBER((*samep)->dom, b->id))
			return;

		if ((*samep)->val[A_ATOM] == val)
			break;

		/* XXX Need to check that there are no data dependencies
		   between diffp and samep.  Currently, the code generator
		   will not produce such dependencies. */
		samep = &JT(*samep);
	}
#ifdef notdef
	/* XXX This doesn't cover everything. */
	for (i = 0; i < N_ATOMS; ++i)
		if ((*samep)->val[i] != pred->val[i])
			return;
#endif
	/* Pull up the node. */
	pull = *samep;
	*samep = JT(pull);
	JT(pull) = *diffp;

	/*
	 * At the top of the chain, each predecessor needs to point at the
	 * pulled up node.  Inside the chain, there is only one predecessor
	 * to worry about.
	 */
	if (at_top) {
		for (ep = b->in_edges; ep != 0; ep = ep->next) {
			if (JT(ep->pred) == b)
				JT(ep->pred) = pull;
			else
				JF(ep->pred) = pull;
		}
	}
	else
		*diffp = pull;

	done = 0;
}

static void
opt_blks(struct block *root, int do_stmts)
{
	int i, maxlevel;
	struct block *p;

	init_val();
	maxlevel = root->level;
	for (i = maxlevel; i >= 0; --i)
		for (p = levels[i]; p; p = p->link)
			opt_blk(p, do_stmts);

	if (do_stmts)
		/*
		 * No point trying to move branches; it can't possibly
		 * make a difference at this point.
		 */
		return;

	for (i = 1; i <= maxlevel; ++i) {
		for (p = levels[i]; p; p = p->link) {
			opt_j(&p->et);
			opt_j(&p->ef);
		}
	}
	for (i = 1; i <= maxlevel; ++i) {
		for (p = levels[i]; p; p = p->link) {
			or_pullup(p);
			and_pullup(p);
		}
	}
}

static __inline void
link_inedge(struct edge *parent, struct block *child)
{
	parent->next = child->in_edges;
	child->in_edges = parent;
}

static void
find_inedges(struct block *root)
{
	int i;
	struct block *b;

	for (i = 0; i < n_blocks; ++i)
		blocks[i]->in_edges = 0;

	/*
	 * Traverse the graph, adding each edge to the predecessor
	 * list of its successors.  Skip the leaves (i.e. level 0).
	 */
	for (i = root->level; i > 0; --i) {
		for (b = levels[i]; b != 0; b = b->link) {
			link_inedge(&b->et, JT(b));
			link_inedge(&b->ef, JF(b));
		}
	}
}

static void
opt_root(struct block **b)
{
	struct slist *tmp, *s;

	s = (*b)->stmts;
	(*b)->stmts = 0;
	while (BPF_CLASS((*b)->s.code) == BPF_JMP && JT(*b) == JF(*b))
		*b = JT(*b);

	tmp = (*b)->stmts;
	if (tmp != 0)
		sappend(s, tmp);
	(*b)->stmts = s;

	/*
	 * If the root node is a return, then there is no
	 * point executing any statements (since the bpf machine
	 * has no side effects).
	 */
	if (BPF_CLASS((*b)->s.code) == BPF_RET)
		(*b)->stmts = 0;
}

static void
opt_loop(struct block *root, int do_stmts)
{

#ifdef BDEBUG
	if (dflag > 1)
		opt_dump(root);
#endif
	do {
		done = 1;
		find_levels(root);
		find_dom(root);
		find_closure(root);
		find_inedges(root);
		find_ud(root);
		find_edom(root);
		opt_blks(root, do_stmts);
#ifdef BDEBUG
		if (dflag > 1)
			opt_dump(root);
#endif
	} while (!done);
}

/*
 * Optimize the filter code in its dag representation.
 */
void
bpf_optimize(struct block **rootp)
{
	struct block *root;

	root = *rootp;

	opt_init(root);
	opt_loop(root, 0);
	opt_loop(root, 1);
	intern_blocks(root);
	opt_root(rootp);
	opt_cleanup();
}

static void
make_marks(struct block *p)
{
	if (!isMarked(p)) {
		Mark(p);
		if (BPF_CLASS(p->s.code) != BPF_RET) {
			make_marks(JT(p));
			make_marks(JF(p));
		}
	}
}

/*
 * Mark code array such that isMarked(i) is true
 * only for nodes that are alive.
 */
static void
mark_code(struct block *p)
{
	cur_mark += 1;
	make_marks(p);
}

/*
 * True iff the two stmt lists load the same value from the packet into
 * the accumulator.
 */
static int
eq_slist(struct slist *x, struct slist *y)
{
	while (1) {
		while (x && x->s.code == NOP)
			x = x->next;
		while (y && y->s.code == NOP)
			y = y->next;
		if (x == 0)
			return y == 0;
		if (y == 0)
			return x == 0;
		if (x->s.code != y->s.code || x->s.k != y->s.k)
			return 0;
		x = x->next;
		y = y->next;
	}
}

static __inline int
eq_blk(struct block *b0, struct block *b1)
{
	if (b0->s.code == b1->s.code &&
	    b0->s.k == b1->s.k &&
	    b0->et.succ == b1->et.succ &&
	    b0->ef.succ == b1->ef.succ)
		return eq_slist(b0->stmts, b1->stmts);
	return 0;
}

static void
intern_blocks(struct block *root)
{
	struct block *p;
	int i, j;
	int done;
 top:
	done = 1;
	for (i = 0; i < n_blocks; ++i)
		blocks[i]->link = 0;

	mark_code(root);

	for (i = n_blocks - 1; --i >= 0; ) {
		if (!isMarked(blocks[i]))
			continue;
		for (j = i + 1; j < n_blocks; ++j) {
			if (!isMarked(blocks[j]))
				continue;
			if (eq_blk(blocks[i], blocks[j])) {
				blocks[i]->link = blocks[j]->link ?
					blocks[j]->link : blocks[j];
				break;
			}
		}
	}
	for (i = 0; i < n_blocks; ++i) {
		p = blocks[i];
		if (JT(p) == 0)
			continue;
		if (JT(p)->link) {
			done = 0;
			JT(p) = JT(p)->link;
		}
		if (JF(p)->link) {
			done = 0;
			JF(p) = JF(p)->link;
		}
	}
	if (!done)
		goto top;
}

static void
opt_cleanup(void)
{
	free((void *)vnode_base);
	free((void *)vmap);
	free((void *)edges);
	free((void *)space1);
	free((void *)space2);
	free((void *)levels);
	free((void *)blocks);
}

/*
 * Return the number of stmts in 's'.
 */
static int
slength(struct slist *s)
{
	int n = 0;

	for (; s; s = s->next)
		if (s->s.code != NOP)
			++n;
	return n;
}

/*
 * Return the number of nodes reachable by 'p'.
 * All nodes should be initially unmarked.
 */
static int
count_blocks(struct block *p)
{
	if (p == 0 || isMarked(p))
		return 0;
	Mark(p);
	return count_blocks(JT(p)) + count_blocks(JF(p)) + 1;
}

/*
 * Do a depth first search on the flow graph, numbering the
 * the basic blocks, and entering them into the 'blocks' array.`
 */
static void
number_blks_r(struct block *p)
{
	int n;

	if (p == 0 || isMarked(p))
		return;

	Mark(p);
	n = n_blocks++;
	p->id = n;
	blocks[n] = p;

	number_blks_r(JT(p));
	number_blks_r(JF(p));
}

/*
 * Return the number of stmts in the flowgraph reachable by 'p'.
 * The nodes should be unmarked before calling.
 */
static int
count_stmts(struct block *p)
{
	int n;

	if (p == 0 || isMarked(p))
		return 0;
	Mark(p);
	n = count_stmts(JT(p)) + count_stmts(JF(p));
	return slength(p->stmts) + n + 1 + p->longjt + p->longjf;
}

/*
 * Allocate memory.  All allocation is done before optimization
 * is begun.  A linear bound on the size of all data structures is computed
 * from the total number of blocks and/or statements.
 */
static void
opt_init(struct block *root)
{
	bpf_u_int32 *p;
	int i, n, max_stmts;
	size_t size1, size2;

	/*
	 * First, count the blocks, so we can malloc an array to map
	 * block number to block.  Then, put the blocks into the array.
	 */
	unMarkAll();
	n = count_blocks(root);
	blocks = reallocarray(NULL, n, sizeof(*blocks));
	if (blocks == NULL)
		bpf_error("malloc");

	unMarkAll();
	n_blocks = 0;
	number_blks_r(root);

	n_edges = 2 * n_blocks;
	edges = reallocarray(NULL, n_edges, sizeof(*edges));
	if (edges == NULL)
		bpf_error("malloc");

	/*
	 * The number of levels is bounded by the number of nodes.
	 */
	levels = reallocarray(NULL, n_blocks, sizeof(*levels));
	if (levels == NULL)
		bpf_error("malloc");

	edgewords = n_edges / (8 * sizeof(bpf_u_int32)) + 1;
	nodewords = n_blocks / (8 * sizeof(bpf_u_int32)) + 1;

	size1 = 2;
	if (n_blocks > SIZE_MAX / size1)
		goto fail1;
	size1 *= n_blocks;
	if (nodewords > SIZE_MAX / size1)
		goto fail1;
	size1 *= nodewords;
	if (sizeof(*space1) > SIZE_MAX / size1)
		goto fail1;
	size1 *= sizeof(*space1);

	space1 = (bpf_u_int32 *)malloc(size1);
	if (space1 == NULL) {
fail1:
		bpf_error("malloc");
	}

	size2 = n_edges;
	if (edgewords > SIZE_MAX / size2)
		goto fail2;
	size2 *= edgewords;
	if (sizeof(*space2) > SIZE_MAX / size2)
		goto fail2;
	size2 *= sizeof(*space2);

	space2 = (bpf_u_int32 *)malloc(size2);
	if (space2 == NULL) {
fail2:
		free(space1);
		bpf_error("malloc");
	}
	
	p = space1;
	all_dom_sets = p;
	for (i = 0; i < n; ++i) {
		blocks[i]->dom = p;
		p += nodewords;
	}
	all_closure_sets = p;
	for (i = 0; i < n; ++i) {
		blocks[i]->closure = p;
		p += nodewords;
	}
	p = space2;
	all_edge_sets = p;
	for (i = 0; i < n; ++i) {
		struct block *b = blocks[i];

		b->et.edom = p;
		p += edgewords;
		b->ef.edom = p;
		p += edgewords;
		b->et.id = i;
		edges[i] = &b->et;
		b->ef.id = n_blocks + i;
		edges[n_blocks + i] = &b->ef;
		b->et.pred = b;
		b->ef.pred = b;
	}
	max_stmts = 0;
	for (i = 0; i < n; ++i)
		max_stmts += slength(blocks[i]->stmts) + 1;
	/*
	 * We allocate at most 3 value numbers per statement,
	 * so this is an upper bound on the number of valnodes
	 * we'll need.
	 */
	maxval = 3 * max_stmts;
	vmap = reallocarray(NULL, maxval, sizeof(*vmap));
	vnode_base = reallocarray(NULL, maxval, sizeof(*vnode_base));
	if (vmap == NULL || vnode_base == NULL)
		bpf_error("malloc");
}

/*
 * Some pointers used to convert the basic block form of the code,
 * into the array form that BPF requires.  'fstart' will point to
 * the malloc'd array while 'ftail' is used during the recursive traversal.
 */
static struct bpf_insn *fstart;
static struct bpf_insn *ftail;

#ifdef BDEBUG
int bids[1000];
#endif

/*
 * Returns true if successful.  Returns false if a branch has
 * an offset that is too large.  If so, we have marked that
 * branch so that on a subsequent iteration, it will be treated
 * properly.
 */
static int
convert_code_r(struct block *p)
{
	struct bpf_insn *dst;
	struct slist *src;
	int slen;
	u_int off;
	int extrajmps;		/* number of extra jumps inserted */
	struct slist **offset = NULL;

	if (p == 0 || isMarked(p))
		return (1);
	Mark(p);

	if (convert_code_r(JF(p)) == 0)
		return (0);
	if (convert_code_r(JT(p)) == 0)
		return (0);

	slen = slength(p->stmts);
	dst = ftail -= (slen + 1 + p->longjt + p->longjf);
		/* inflate length by any extra jumps */

	p->offset = dst - fstart;

	/* generate offset[] for convenience  */
	if (slen) {
		offset = calloc(slen, sizeof(struct slist *));
		if (!offset) {
			bpf_error("not enough core");
			/*NOTREACHED*/
		}
	}
	src = p->stmts;
	for (off = 0; off < slen && src; off++) {
#if 0
		printf("off=%d src=%x\n", off, src);
#endif
		offset[off] = src;
		src = src->next;
	}

	off = 0;
	for (src = p->stmts; src; src = src->next) {
		if (src->s.code == NOP)
			continue;
		dst->code = (u_short)src->s.code;
		dst->k = src->s.k;

		/* fill block-local relative jump */
		if (BPF_CLASS(src->s.code) != BPF_JMP || src->s.code == (BPF_JMP|BPF_JA)) {
#if 0
			if (src->s.jt || src->s.jf) {
				bpf_error("illegal jmp destination");
				/*NOTREACHED*/
			}
#endif
			goto filled;
		}
		if (off == slen - 2)	/*???*/
			goto filled;

	    {
		int i;
		int jt, jf;
		static const char ljerr[] =
		    "%s for block-local relative jump: off=%d";

#if 0
		printf("code=%x off=%d %x %x\n", src->s.code,
			off, src->s.jt, src->s.jf);
#endif

		if (!src->s.jt || !src->s.jf) {
			bpf_error(ljerr, "no jmp destination", off);
			/*NOTREACHED*/
		}

		jt = jf = 0;
		for (i = 0; i < slen; i++) {
			if (offset[i] == src->s.jt) {
				if (jt) {
					bpf_error(ljerr, "multiple matches", off);
					/*NOTREACHED*/
				}

				dst->jt = i - off - 1;
				jt++;
			}
			if (offset[i] == src->s.jf) {
				if (jf) {
					bpf_error(ljerr, "multiple matches", off);
					/*NOTREACHED*/
				}
				dst->jf = i - off - 1;
				jf++;
			}
		}
		if (!jt || !jf) {
			bpf_error(ljerr, "no destination found", off);
			/*NOTREACHED*/
		}
	    }
filled:
		++dst;
		++off;
	}
	free(offset);

#ifdef BDEBUG
	bids[dst - fstart] = p->id + 1;
#endif
	dst->code = (u_short)p->s.code;
	dst->k = p->s.k;
	if (JT(p)) {
		extrajmps = 0;
		off = JT(p)->offset - (p->offset + slen) - 1;
		if (off >= 256) {
		    /* offset too large for branch, must add a jump */
		    if (p->longjt == 0) {
		    	/* mark this instruction and retry */
			p->longjt++;
			return(0);
		    }
		    /* branch if T to following jump */
		    dst->jt = extrajmps;
		    extrajmps++;
		    dst[extrajmps].code = BPF_JMP|BPF_JA;
		    dst[extrajmps].k = off - extrajmps;
		}
		else
		    dst->jt = off;
		off = JF(p)->offset - (p->offset + slen) - 1;
		if (off >= 256) {
		    /* offset too large for branch, must add a jump */
		    if (p->longjf == 0) {
		    	/* mark this instruction and retry */
			p->longjf++;
			return(0);
		    }
		    /* branch if F to following jump */
		    /* if two jumps are inserted, F goes to second one */
		    dst->jf = extrajmps;
		    extrajmps++;
		    dst[extrajmps].code = BPF_JMP|BPF_JA;
		    dst[extrajmps].k = off - extrajmps;
		}
		else
		    dst->jf = off;
	}
	return (1);
}


/*
 * Convert flowgraph intermediate representation to the
 * BPF array representation.  Set *lenp to the number of instructions.
 */
struct bpf_insn *
icode_to_fcode(struct block *root, int *lenp)
{
	int n;
	struct bpf_insn *fp;

	/*
	 * Loop doing convert_codr_r() until no branches remain
	 * with too-large offsets.
	 */
	while (1) {
	    unMarkAll();
	    n = *lenp = count_stmts(root);
    
	    fp = calloc(n, sizeof(*fp));
	    if (fp == NULL)
		    bpf_error("calloc");

	    fstart = fp;
	    ftail = fp + n;
    
	    unMarkAll();
	    if (convert_code_r(root))
		break;
	    free(fp);
	}

	return fp;
}

#ifdef BDEBUG
static void
opt_dump(struct block *root)
{
	struct bpf_program f;

	memset(bids, 0, sizeof bids);
	f.bf_insns = icode_to_fcode(root, &f.bf_len);
	bpf_dump(&f, 1);
	putchar('\n');
	free((char *)f.bf_insns);
}
#endif
