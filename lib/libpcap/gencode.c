/*	$OpenBSD: gencode.c,v 1.68 2025/06/06 00:04:33 dlg Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998
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
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_pflog.h>
#include <net/pfvar.h>

#include <netmpls/mpls.h>

#include <net80211/ieee80211.h>
#include <net80211/ieee80211_radiotap.h>

#include <stdlib.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

#include "pcap-int.h"

#include "ethertype.h"
#include "llc.h"
#include "gencode.h"
#include "ppp.h"
#include <pcap-namedb.h>
#ifdef INET6
#include <netdb.h>
#endif /*INET6*/

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#define JMP(c) ((c)|BPF_JMP|BPF_K)

/* Locals */
static jmp_buf top_ctx;
static pcap_t *bpf_pcap;

/* Hack for updating VLAN offsets. */
static u_int	orig_linktype = -1, orig_nl = -1, orig_nl_nosnap = -1;
static u_int	mpls_stack = 0;

/* XXX */
#ifdef PCAP_FDDIPAD
int	pcap_fddipad = PCAP_FDDIPAD;
#else
int	pcap_fddipad;
#endif

__dead void
bpf_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (bpf_pcap != NULL)
		(void)vsnprintf(pcap_geterr(bpf_pcap), PCAP_ERRBUF_SIZE,
		    fmt, ap);
	va_end(ap);
	longjmp(top_ctx, 1);
	/* NOTREACHED */
}

static void init_linktype(int);

static int alloc_reg(void);
static void free_reg(int);

static struct block *root;

/* initialization code used for variable link header */
static struct slist *init_code = NULL;

/* Flags and registers for variable link type handling */
static int variable_nl;
static int nl_reg, iphl_reg;

/*
 * Track memory allocations, for bulk freeing at the end
 */
#define NMEMBAG 16
#define MEMBAG0SIZE (4096 / sizeof (void *))
struct membag {
	u_int total;
	u_int slot;
	void **ptrs;	/* allocated array[total] to each malloc */
};

static struct membag membag[NMEMBAG];
static int cur_membag;

static void *newchunk(size_t);
static void freechunks(void);
static __inline struct block *new_block(int);
static __inline struct slist *new_stmt(int);
static struct block *gen_retblk(int);
static __inline void syntax(void);

static void backpatch(struct block *, struct block *);
static void merge(struct block *, struct block *);
static struct block *gen_cmp(u_int, u_int, bpf_int32);
static struct block *gen_cmp_gt(u_int, u_int, bpf_int32);
static struct block *gen_cmp_nl(u_int, u_int, bpf_int32);
static struct block *gen_mcmp(u_int, u_int, bpf_int32, bpf_u_int32);
static struct block *gen_mcmp_nl(u_int, u_int, bpf_int32, bpf_u_int32);
static struct block *gen_bcmp(u_int, u_int, const u_char *);
static struct block *gen_uncond(int);
static __inline struct block *gen_true(void);
static __inline struct block *gen_false(void);
static struct block *gen_linktype(int);
static struct block *gen_hostop(bpf_u_int32, bpf_u_int32, int, int, u_int, u_int);
#ifdef INET6
static struct block *gen_hostop6(struct in6_addr *, struct in6_addr *, int, int, u_int, u_int);
#endif
static struct block *gen_ehostop(const u_char *, int);
static struct block *gen_fhostop(const u_char *, int);
static struct block *gen_dnhostop(bpf_u_int32, int, u_int);
static struct block *gen_p80211_hostop(const u_char *, int);
static struct block *gen_p80211_addr(int, u_int, const u_char *);
static struct block *gen_host(bpf_u_int32, bpf_u_int32, int, int);
#ifdef INET6
static struct block *gen_host6(struct in6_addr *, struct in6_addr *, int, int);
#endif
#ifndef INET6
static struct block *gen_gateway(const u_char *, bpf_u_int32 **, int, int);
#endif
static struct block *gen_ipfrag(void);
static struct block *gen_portatom(int, bpf_int32);
#ifdef INET6
static struct block *gen_portatom6(int, bpf_int32);
#endif
struct block *gen_portop(int, int, int);
static struct block *gen_port(int, int, int);
#ifdef INET6
struct block *gen_portop6(int, int, int);
static struct block *gen_port6(int, int, int);
#endif
static int lookup_proto(const char *, int);
static struct block *gen_protochain(int, int, int);
static struct block *gen_proto(int, int, int);
static struct slist *xfer_to_x(struct arth *);
static struct slist *xfer_to_a(struct arth *);
static struct block *gen_len(int, int);

static void *
newchunk(size_t n)
{
	struct membag *m;
	void *p;

	m = &membag[cur_membag];
	if (m->total != 0 && m->total - m->slot == 0) {
		if (++cur_membag == NMEMBAG)
			bpf_error("out of memory");
		m = &membag[cur_membag];
	}
	if (m->total - m->slot == 0) {
		m->ptrs = calloc(sizeof (char *), MEMBAG0SIZE << cur_membag);
		if (m->ptrs == NULL)
			bpf_error("out of memory");
		m->total = MEMBAG0SIZE << cur_membag;
		m->slot = 0;
	}

	p = calloc(1, n);
	if (p == NULL)
		bpf_error("out of memory");
	m->ptrs[m->slot++] = p;
	return (p);
}

static void
freechunks(void)
{
	int i, j;

	for (i = 0; i <= cur_membag; i++) {
		if (membag[i].ptrs == NULL)
			continue;
		for (j = 0; j < membag[i].slot; j++)
			free(membag[i].ptrs[j]);
		free(membag[i].ptrs);
		membag[i].ptrs = NULL;
		membag[i].slot = membag[i].total = 0;
	}
	cur_membag = 0;
}

/*
 * A strdup whose allocations are freed after code generation is over.
 */
char *
sdup(const char *s)
{
	int n = strlen(s) + 1;
	char *cp = newchunk(n);

	strlcpy(cp, s, n);
	return (cp);
}

static __inline struct block *
new_block(int code)
{
	struct block *p;

	p = (struct block *)newchunk(sizeof(*p));
	p->s.code = code;
	p->head = p;

	return p;
}

static __inline struct slist *
new_stmt(int code)
{
	struct slist *p;

	p = (struct slist *)newchunk(sizeof(*p));
	p->s.code = code;

	return p;
}

static struct block *
gen_retblk(int v)
{
	struct block *b = new_block(BPF_RET|BPF_K);

	b->s.k = v;
	return b;
}

static __inline void
syntax(void)
{
	bpf_error("syntax error in filter expression");
}

static bpf_u_int32 netmask;
static int snaplen;
int no_optimize;

int
pcap_compile(pcap_t *p, struct bpf_program *program,
	     const char *buf, int optimize, bpf_u_int32 mask)
{
	extern int n_errors;
	int len;

	no_optimize = 0;
	n_errors = 0;
	root = NULL;
	bpf_pcap = p;
	if (setjmp(top_ctx)) {
		freechunks();
		return (-1);
	}

	netmask = mask;
	snaplen = pcap_snapshot(p);

	lex_init(buf ? buf : "");
	init_linktype(pcap_datalink(p));
	(void)pcap_parse();

	if (n_errors)
		syntax();

	if (root == NULL)
		root = gen_retblk(snaplen);

	if (optimize && !no_optimize) {
		bpf_optimize(&root);
		if (root == NULL ||
		    (root->s.code == (BPF_RET|BPF_K) && root->s.k == 0))
			bpf_error("expression rejects all packets");
	}
	program->bf_insns = icode_to_fcode(root, &len);
	program->bf_len = len;

	freechunks();
	return (0);
}

/*
 * entry point for using the compiler with no pcap open
 * pass in all the stuff that is needed explicitly instead.
 */
int
pcap_compile_nopcap(int snaplen_arg, int linktype_arg,
		    struct bpf_program *program,
	     const char *buf, int optimize, bpf_u_int32 mask)
{
	extern int n_errors;
	int len;

	n_errors = 0;
	root = NULL;
	bpf_pcap = NULL;
	if (setjmp(top_ctx)) {
		freechunks();
		return (-1);
	}

	netmask = mask;

	/* XXX needed? I don't grok the use of globals here. */
	snaplen = snaplen_arg;

	lex_init(buf ? buf : "");
	init_linktype(linktype_arg);
	(void)pcap_parse();

	if (n_errors)
		syntax();

	if (root == NULL)
		root = gen_retblk(snaplen_arg);

	if (optimize) {
		bpf_optimize(&root);
		if (root == NULL ||
		    (root->s.code == (BPF_RET|BPF_K) && root->s.k == 0))
			bpf_error("expression rejects all packets");
	}
	program->bf_insns = icode_to_fcode(root, &len);
	program->bf_len = len;

	freechunks();
	return (0);
}

/*
 * Clean up a "struct bpf_program" by freeing all the memory allocated
 * in it.
 */
void
pcap_freecode(struct bpf_program *program)
{
	program->bf_len = 0;
	if (program->bf_insns != NULL) {
		free((char *)program->bf_insns);
		program->bf_insns = NULL;
	}
}

/*
 * Backpatch the blocks in 'list' to 'target'.  The 'sense' field indicates
 * which of the jt and jf fields has been resolved and which is a pointer
 * back to another unresolved block (or nil).  At least one of the fields
 * in each block is already resolved.
 */
static void
backpatch(struct block *list, struct block *target)
{
	struct block *next;

	while (list) {
		if (!list->sense) {
			next = JT(list);
			JT(list) = target;
		} else {
			next = JF(list);
			JF(list) = target;
		}
		list = next;
	}
}

/*
 * Merge the lists in b0 and b1, using the 'sense' field to indicate
 * which of jt and jf is the link.
 */
static void
merge(struct block *b0, struct block *b1)
{
	struct block **p = &b0;

	/* Find end of list. */
	while (*p)
		p = !((*p)->sense) ? &JT(*p) : &JF(*p);

	/* Concatenate the lists. */
	*p = b1;
}

void
finish_parse(struct block *p)
{
	backpatch(p, gen_retblk(snaplen));
	p->sense = !p->sense;
	backpatch(p, gen_retblk(0));
	root = p->head;

	/* prepend initialization code to root */
	if (init_code != NULL && root != NULL) {
		sappend(init_code, root->stmts);
		root->stmts = init_code;
		init_code = NULL;
	}

	if (iphl_reg != -1) {
		free_reg(iphl_reg);
		iphl_reg = -1;
	}
	if (nl_reg != -1) {
		free_reg(nl_reg);
		nl_reg = -1;
	}
}

void
gen_and(struct block *b0, struct block *b1)
{
	backpatch(b0, b1->head);
	b0->sense = !b0->sense;
	b1->sense = !b1->sense;
	merge(b1, b0);
	b1->sense = !b1->sense;
	b1->head = b0->head;
}

void
gen_or(struct block *b0, struct block *b1)
{
	b0->sense = !b0->sense;
	backpatch(b0, b1->head);
	b0->sense = !b0->sense;
	merge(b1, b0);
	b1->head = b0->head;
}

void
gen_not(struct block *b)
{
	b->sense = !b->sense;
}

static struct block *
gen_cmp(u_int offset, u_int size, bpf_int32 v)
{
	struct slist *s;
	struct block *b;

	s = new_stmt(BPF_LD|BPF_ABS|size);
	s->s.k = offset;

	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;
	b->s.k = v;

	return b;
}

static struct block *
gen_cmp_gt(u_int offset, u_int size, bpf_int32 v)
{
	struct slist *s;
	struct block *b;

	s = new_stmt(BPF_LD|BPF_ABS|size);
	s->s.k = offset;

	b = new_block(JMP(BPF_JGT));
	b->stmts = s;
	b->s.k = v;

	return b;
}

static struct block *
gen_mcmp(u_int offset, u_int size, bpf_int32 v, bpf_u_int32 mask)
{
	struct block *b = gen_cmp(offset, size, v);
	struct slist *s;

	if (mask != 0xffffffff) {
		s = new_stmt(BPF_ALU|BPF_AND|BPF_K);
		s->s.k = mask;
		sappend(b->stmts, s);
	}
	return b;
}

/* Like gen_mcmp with 'dynamic off_nl' added to the offset */
static struct block *
gen_mcmp_nl(u_int offset, u_int size, bpf_int32 v, bpf_u_int32 mask)
{
	struct block *b = gen_cmp_nl(offset, size, v);
	struct slist *s;

	if (mask != 0xffffffff) {
		s = new_stmt(BPF_ALU|BPF_AND|BPF_K);
		s->s.k = mask;
		sappend(b->stmts, s);
	}
	return b;
}

static struct block *
gen_bcmp(u_int offset, u_int size, const u_char *v)
{
	struct block *b, *tmp;

	b = NULL;
	while (size >= 4) {
		const u_char *p = &v[size - 4];
		bpf_int32 w = ((bpf_int32)p[0] << 24) |
		    ((bpf_int32)p[1] << 16) | ((bpf_int32)p[2] << 8) | p[3];

		tmp = gen_cmp(offset + size - 4, BPF_W, w);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
		size -= 4;
	}
	while (size >= 2) {
		const u_char *p = &v[size - 2];
		bpf_int32 w = ((bpf_int32)p[0] << 8) | p[1];

		tmp = gen_cmp(offset + size - 2, BPF_H, w);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
		size -= 2;
	}
	if (size > 0) {
		tmp = gen_cmp(offset, BPF_B, (bpf_int32)v[0]);
		if (b != NULL)
			gen_and(b, tmp);
		b = tmp;
	}
	return b;
}

/*
 * Various code constructs need to know the layout of the data link
 * layer.  These variables give the necessary offsets.  off_linktype
 * is set to -1 for no encapsulation, in which case, IP is assumed.
 */
static u_int off_linktype;
static u_int off_nl;
static u_int off_nl_nosnap;

static int linktype;

/* Generate code to load the dynamic 'off_nl' to the X register */
static struct slist *
nl2X_stmt(void)
{
	struct slist *s, *tmp;

	if (nl_reg == -1) {
		switch (linktype) {
		case DLT_PFLOG:
			/* The pflog header contains PFLOG_REAL_HDRLEN
			   which does NOT include the padding. Round
			   up to the nearest dword boundary */
			s = new_stmt(BPF_LD|BPF_B|BPF_ABS);
			s->s.k = 0;

			tmp = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
			tmp->s.k = 3;
			sappend(s, tmp);

			tmp = new_stmt(BPF_ALU|BPF_AND|BPF_K);
			tmp->s.k = 0xfc;
			sappend(s, tmp);

			nl_reg = alloc_reg();
			tmp = new_stmt(BPF_ST);
			tmp->s.k = nl_reg;
			sappend(s, tmp);

			break;
		default:
			bpf_error("Unknown header size for link type 0x%x",
				  linktype);
		}

		if (init_code == NULL)
			init_code = s;
		else
			sappend(init_code, s);
	}

	s = new_stmt(BPF_LDX|BPF_MEM);
	s->s.k = nl_reg;

	return s;
}

/* Like gen_cmp but adds the dynamic 'off_nl' to the offset */
static struct block *
gen_cmp_nl(u_int offset, u_int size, bpf_int32 v)
{
	struct slist *s, *tmp;
	struct block *b;

	if (variable_nl) {
		s = nl2X_stmt();
		tmp = new_stmt(BPF_LD|BPF_IND|size);
		tmp->s.k = offset;
		sappend(s, tmp);
	} else {
		s = new_stmt(BPF_LD|BPF_ABS|size);
		s->s.k = offset + off_nl;
	}
	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;
	b->s.k = v;

	return b;
}

static void
init_linktype(int type)
{
	linktype = type;
	init_code = NULL;
	nl_reg = iphl_reg = -1;

	switch (type) {

	case DLT_EN10MB:
		off_linktype = 12;
		off_nl = 14;
		return;

	case DLT_SLIP:
		/*
		 * SLIP doesn't have a link level type.  The 16 byte
		 * header is hacked into our SLIP driver.
		 */
		off_linktype = -1;
		off_nl = 16;
		return;

	case DLT_SLIP_BSDOS:
		/* XXX this may be the same as the DLT_PPP_BSDOS case */
		off_linktype = -1;
		/* XXX end */
		off_nl = 24;
		return;

	case DLT_NULL:
		off_linktype = 0;
		off_nl = 4;
		return;

	case DLT_PPP:
		off_linktype = 2;
		off_nl = 4;
		return;

	case DLT_PPP_SERIAL:
		off_linktype = -1;
		off_nl = 2;
		return;

	case DLT_PPP_ETHER:
		/*
		 * This does not include the Ethernet header, and
		 * only covers session state.
 		 */
		off_linktype = 6;
		off_nl = 8;
		return;

	case DLT_PPP_BSDOS:
		off_linktype = 5;
		off_nl = 24;
		return;

	case DLT_FDDI:
		/*
		 * FDDI doesn't really have a link-level type field.
		 * We assume that SSAP = SNAP is being used and pick
		 * out the encapsulated Ethernet type.
		 */
		off_linktype = 19;
#ifdef PCAP_FDDIPAD
		off_linktype += pcap_fddipad;
#endif
		off_nl = 21;
#ifdef PCAP_FDDIPAD
		off_nl += pcap_fddipad;
#endif
		return;

	case DLT_IEEE802:
		off_linktype = 20;
		off_nl = 22;
		return;

	case DLT_IEEE802_11:
		off_linktype = 30; /* XXX variable */
		off_nl = 32;
		return;

	case DLT_IEEE802_11_RADIO: /* XXX variable */
		off_linktype = 30 + IEEE80211_RADIOTAP_HDRLEN;
		off_nl = 32 + IEEE80211_RADIOTAP_HDRLEN;
		return;

	case DLT_ATM_RFC1483:
		/*
		 * assume routed, non-ISO PDUs
		 * (i.e., LLC = 0xAA-AA-03, OUT = 0x00-00-00)
		 */
		off_linktype = 6;
		off_nl = 8;
		return;

	case DLT_LOOP:
		off_linktype = 0;
		off_nl = 4;
		return;

	case DLT_ENC:
		off_linktype = -1;
		off_nl = 12;
		return;

	case DLT_PFLOG:
		off_linktype = 0;
		variable_nl = 1;
		off_nl = 0;
		return;

	case DLT_PFSYNC:
		off_linktype = -1;
		off_nl = 4;
		return;

	case DLT_OPENFLOW:
		off_linktype = -1;
		off_nl = 12;
		return;

	case DLT_RAW:
		off_linktype = 0;
		off_nl = 0;
		return;
	case DLT_USBPCAP:
		off_linktype = -1;
		off_nl = 0;
		return;
	}
	bpf_error("unknown data link type 0x%x", linktype);
	/* NOTREACHED */
}

static struct block *
gen_uncond(int rsense)
{
	struct block *b;
	struct slist *s;

	s = new_stmt(BPF_LD|BPF_IMM);
	s->s.k = !rsense;
	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;

	return b;
}

static __inline struct block *
gen_true(void)
{
	return gen_uncond(1);
}

static __inline struct block *
gen_false(void)
{
	return gen_uncond(0);
}

static struct block *
gen_linktype(int proto)
{
	struct block *b0, *b1;

	/* If we're not using encapsulation and checking for IP, we're done */
	if ((off_linktype == -1 || mpls_stack > 0) && proto == ETHERTYPE_IP)
		return gen_true();
#ifdef INET6
	/* this isn't the right thing to do, but sometimes necessary */
	if ((off_linktype == -1 || mpls_stack > 0) && proto == ETHERTYPE_IPV6)
		return gen_true();
#endif

	switch (linktype) {

	case DLT_EN10MB:
		if (proto <= ETHERMTU) {
			/* This is an LLC SAP value */
			b0 = gen_cmp_gt(off_linktype, BPF_H, ETHERMTU);
			gen_not(b0);
			b1 = gen_cmp(off_linktype + 2, BPF_B, (bpf_int32)proto);
			gen_and(b0, b1);
			return b1;
		} else {
			/* This is an Ethernet type */
			return gen_cmp(off_linktype, BPF_H, (bpf_int32)proto);
		}
		break;

	case DLT_SLIP:
		return gen_false();

	case DLT_PPP:
	case DLT_PPP_ETHER:
		if (proto == ETHERTYPE_IP)
			proto = PPP_IP;			/* XXX was 0x21 */
#ifdef INET6
		else if (proto == ETHERTYPE_IPV6)
			proto = PPP_IPV6;
#endif
		break;

	case DLT_PPP_BSDOS:
		switch (proto) {

		case ETHERTYPE_IP:
			b0 = gen_cmp(off_linktype, BPF_H, PPP_IP);
			b1 = gen_cmp(off_linktype, BPF_H, PPP_VJC);
			gen_or(b0, b1);
			b0 = gen_cmp(off_linktype, BPF_H, PPP_VJNC);
			gen_or(b1, b0);
			return b0;

#ifdef INET6
		case ETHERTYPE_IPV6:
			proto = PPP_IPV6;
			/* more to go? */
			break;
#endif /* INET6 */

		case ETHERTYPE_DN:
			proto = PPP_DECNET;
			break;

		case ETHERTYPE_ATALK:
			proto = PPP_APPLE;
			break;

		case ETHERTYPE_NS:
			proto = PPP_NS;
			break;
		}
		break;

	case DLT_LOOP:
	case DLT_ENC:
	case DLT_NULL:
	{
		int v;

		if (proto == ETHERTYPE_IP)
			v = AF_INET;
#ifdef INET6
		else if (proto == ETHERTYPE_IPV6)
			v = AF_INET6;
#endif /* INET6 */
		else
			return gen_false();

		/*
		 * For DLT_NULL, the link-layer header is a 32-bit word
		 * containing an AF_ value in *host* byte order, and for
		 * DLT_ENC, the link-layer header begins with a 32-bit
		 * word containing an AF_ value in host byte order.
		 *
		 * For DLT_LOOP, the link-layer header is a 32-bit
		 * word containing an AF_ value in *network* byte order.
		 */
		if (linktype != DLT_LOOP)
			v = htonl(v);

		return (gen_cmp(0, BPF_W, (bpf_int32)v));
		break;
	}
	case DLT_RAW: {
		struct slist *s0, *s1;
		int ipv;

		switch (proto) {
		case ETHERTYPE_IP:
			ipv = 4;
			break;
		case ETHERTYPE_IPV6:
			ipv = 6;
			break;
		default:
			return gen_false();
		}

		/* A = p[X+off_linktype] */
		s0 = new_stmt(BPF_LD|BPF_ABS|BPF_B);
		s0->s.k = off_linktype;

		/* A = A >> 4 */
		s1 = new_stmt(BPF_ALU|BPF_RSH|BPF_K);
		s1->s.k = 4;
		sappend(s0, s1);

		/* if (A == ipv) ... */
		b0 = new_block(JMP(BPF_JEQ));
		b0->stmts = s0;
		b0->s.k = ipv;

		return (b0);
	}

	case DLT_PFLOG:
		if (proto == ETHERTYPE_IP)
			return (gen_cmp(offsetof(struct pfloghdr, af), BPF_B,
			    (bpf_int32)AF_INET));
#ifdef INET6
		else if (proto == ETHERTYPE_IPV6)
			return (gen_cmp(offsetof(struct pfloghdr, af), BPF_B,
			    (bpf_int32)AF_INET6));
#endif /* INET6 */
		else
			return gen_false();
		break;

	}
	return gen_cmp(off_linktype, BPF_H, (bpf_int32)proto);
}

static struct block *
gen_hostop(bpf_u_int32 addr, bpf_u_int32 mask, int dir, int proto,
    u_int src_off, u_int dst_off)
{
	struct block *b0, *b1;
	u_int offset;

	switch (dir) {

	case Q_SRC:
		offset = src_off;
		break;

	case Q_DST:
		offset = dst_off;
		break;

	case Q_AND:
		b0 = gen_hostop(addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop(addr, mask, Q_DST, proto, src_off, dst_off);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		b0 = gen_hostop(addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop(addr, mask, Q_DST, proto, src_off, dst_off);
		gen_or(b0, b1);
		return b1;

	default:
		bpf_error("direction not supported on linktype 0x%x",
		    linktype);
	}
	b0 = gen_linktype(proto);
	b1 = gen_mcmp_nl(offset, BPF_W, (bpf_int32)addr, mask);
	gen_and(b0, b1);
	return b1;
}

#ifdef INET6
static struct block *
gen_hostop6(struct in6_addr *addr, struct in6_addr *mask, int dir, int proto,
    u_int src_off, u_int dst_off)
{
	struct block *b0, *b1;
	u_int offset;
	u_int32_t *a, *m;

	switch (dir) {

	case Q_SRC:
		offset = src_off;
		break;

	case Q_DST:
		offset = dst_off;
		break;

	case Q_AND:
		b0 = gen_hostop6(addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop6(addr, mask, Q_DST, proto, src_off, dst_off);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		b0 = gen_hostop6(addr, mask, Q_SRC, proto, src_off, dst_off);
		b1 = gen_hostop6(addr, mask, Q_DST, proto, src_off, dst_off);
		gen_or(b0, b1);
		return b1;

	default:
		bpf_error("direction not supported on linktype 0x%x",
		    linktype);
	}
	/* this order is important */
	a = (u_int32_t *)addr;
	m = (u_int32_t *)mask;
	b1 = gen_mcmp_nl(offset + 12, BPF_W, ntohl(a[3]), ntohl(m[3]));
	b0 = gen_mcmp_nl(offset + 8, BPF_W, ntohl(a[2]), ntohl(m[2]));
	gen_and(b0, b1);
	b0 = gen_mcmp_nl(offset + 4, BPF_W, ntohl(a[1]), ntohl(m[1]));
	gen_and(b0, b1);
	b0 = gen_mcmp_nl(offset + 0, BPF_W, ntohl(a[0]), ntohl(m[0]));
	gen_and(b0, b1);
	b0 = gen_linktype(proto);
	gen_and(b0, b1);
	return b1;
}
#endif /*INET6*/

static struct block *
gen_ehostop(const u_char *eaddr, int dir)
{
	struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
		return gen_bcmp(6, 6, eaddr);

	case Q_DST:
		return gen_bcmp(0, 6, eaddr);

	case Q_AND:
		b0 = gen_ehostop(eaddr, Q_SRC);
		b1 = gen_ehostop(eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_ehostop(eaddr, Q_SRC);
		b1 = gen_ehostop(eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;
	default:
		bpf_error("direction not supported on linktype 0x%x",
		    linktype);
	}
	/* NOTREACHED */
}

/*
 * Like gen_ehostop, but for DLT_FDDI
 */
static struct block *
gen_fhostop(const u_char *eaddr, int dir)
{
	struct block *b0, *b1;

	switch (dir) {
	case Q_SRC:
#ifdef PCAP_FDDIPAD
		return gen_bcmp(6 + 1 + pcap_fddipad, 6, eaddr);
#else
		return gen_bcmp(6 + 1, 6, eaddr);
#endif

	case Q_DST:
#ifdef PCAP_FDDIPAD
		return gen_bcmp(0 + 1 + pcap_fddipad, 6, eaddr);
#else
		return gen_bcmp(0 + 1, 6, eaddr);
#endif

	case Q_AND:
		b0 = gen_fhostop(eaddr, Q_SRC);
		b1 = gen_fhostop(eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_fhostop(eaddr, Q_SRC);
		b1 = gen_fhostop(eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;
	default:
		bpf_error("direction not supported on linktype 0x%x",
		    linktype);
	}
	/* NOTREACHED */
}

/*
 * This is quite tricky because there may be pad bytes in front of the
 * DECNET header, and then there are two possible data packet formats that
 * carry both src and dst addresses, plus 5 packet types in a format that
 * carries only the src node, plus 2 types that use a different format and
 * also carry just the src node.
 *
 * Yuck.
 *
 * Instead of doing those all right, we just look for data packets with
 * 0 or 1 bytes of padding.  If you want to look at other packets, that
 * will require a lot more hacking.
 *
 * To add support for filtering on DECNET "areas" (network numbers)
 * one would want to add a "mask" argument to this routine.  That would
 * make the filter even more inefficient, although one could be clever
 * and not generate masking instructions if the mask is 0xFFFF.
 */
static struct block *
gen_dnhostop(bpf_u_int32 addr, int dir, u_int base_off)
{
	struct block *b0, *b1, *b2, *tmp;
	u_int offset_lh;	/* offset if long header is received */
	u_int offset_sh;	/* offset if short header is received */

	switch (dir) {

	case Q_DST:
		offset_sh = 1;	/* follows flags */
		offset_lh = 7;	/* flgs,darea,dsubarea,HIORD */
		break;

	case Q_SRC:
		offset_sh = 3;	/* follows flags, dstnode */
		offset_lh = 15;	/* flgs,darea,dsubarea,did,sarea,ssub,HIORD */
		break;

	case Q_AND:
		/* Inefficient because we do our Calvinball dance twice */
		b0 = gen_dnhostop(addr, Q_SRC, base_off);
		b1 = gen_dnhostop(addr, Q_DST, base_off);
		gen_and(b0, b1);
		return b1;

	case Q_OR:
	case Q_DEFAULT:
		/* Inefficient because we do our Calvinball dance twice */
		b0 = gen_dnhostop(addr, Q_SRC, base_off);
		b1 = gen_dnhostop(addr, Q_DST, base_off);
		gen_or(b0, b1);
		return b1;

	default:
		bpf_error("direction not supported on linktype 0x%x",
		    linktype);
	}
	b0 = gen_linktype(ETHERTYPE_DN);
	/* Check for pad = 1, long header case */
	tmp = gen_mcmp_nl(base_off + 2, BPF_H,
		       (bpf_int32)ntohs(0x0681), (bpf_int32)ntohs(0x07FF));
	b1 = gen_cmp_nl(base_off + 2 + 1 + offset_lh,
	    BPF_H, (bpf_int32)ntohs(addr));
	gen_and(tmp, b1);
	/* Check for pad = 0, long header case */
	tmp = gen_mcmp_nl(base_off + 2, BPF_B, (bpf_int32)0x06, (bpf_int32)0x7);
	b2 = gen_cmp_nl(base_off + 2 + offset_lh, BPF_H, (bpf_int32)ntohs(addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);
	/* Check for pad = 1, short header case */
	tmp = gen_mcmp_nl(base_off + 2, BPF_H,
		       (bpf_int32)ntohs(0x0281), (bpf_int32)ntohs(0x07FF));
	b2 = gen_cmp_nl(base_off + 2 + 1 + offset_sh,
	    BPF_H, (bpf_int32)ntohs(addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);
	/* Check for pad = 0, short header case */
	tmp = gen_mcmp_nl(base_off + 2, BPF_B, (bpf_int32)0x02, (bpf_int32)0x7);
	b2 = gen_cmp_nl(base_off + 2 + offset_sh, BPF_H, (bpf_int32)ntohs(addr));
	gen_and(tmp, b2);
	gen_or(b2, b1);

	/* Combine with test for linktype */
	gen_and(b0, b1);
	return b1;
}

static struct block *
gen_host(bpf_u_int32 addr, bpf_u_int32 mask, int proto, int dir)
{
	struct block *b0, *b1;

	switch (proto) {

	case Q_DEFAULT:
		b0 = gen_host(addr, mask, Q_IP, dir);
		b1 = gen_host(addr, mask, Q_ARP, dir);
		gen_or(b0, b1);
		b0 = gen_host(addr, mask, Q_RARP, dir);
		gen_or(b1, b0);
		return b0;

	case Q_IP:
		return gen_hostop(addr, mask, dir, ETHERTYPE_IP,
				  12, 16);

	case Q_RARP:
		return gen_hostop(addr, mask, dir, ETHERTYPE_REVARP,
				  14, 24);

	case Q_ARP:
		return gen_hostop(addr, mask, dir, ETHERTYPE_ARP,
				  14, 24);

	case Q_TCP:
		bpf_error("'tcp' modifier applied to host");

	case Q_UDP:
		bpf_error("'udp' modifier applied to host");

	case Q_ICMP:
		bpf_error("'icmp' modifier applied to host");

	case Q_IGMP:
		bpf_error("'igmp' modifier applied to host");

	case Q_IGRP:
		bpf_error("'igrp' modifier applied to host");

	case Q_PIM:
		bpf_error("'pim' modifier applied to host");

	case Q_STP:
		bpf_error("'stp' modifier applied to host");

	case Q_ATALK:
		bpf_error("ATALK host filtering not implemented");

	case Q_DECNET:
		return gen_dnhostop(addr, dir, 0);

	case Q_SCA:
		bpf_error("SCA host filtering not implemented");

	case Q_LAT:
		bpf_error("LAT host filtering not implemented");

	case Q_MOPDL:
		bpf_error("MOPDL host filtering not implemented");

	case Q_MOPRC:
		bpf_error("MOPRC host filtering not implemented");

#ifdef INET6
	case Q_IPV6:
		bpf_error("'ip6' modifier applied to ip host");

	case Q_ICMPV6:
		bpf_error("'icmp6' modifier applied to host");
#endif /* INET6 */

	case Q_AH:
		bpf_error("'ah' modifier applied to host");

	case Q_ESP:
		bpf_error("'esp' modifier applied to host");

	default:
		bpf_error("direction not supported on linktype 0x%x",
		    linktype);
	}
	/* NOTREACHED */
}

#ifdef INET6
static struct block *
gen_host6(struct in6_addr *addr, struct in6_addr *mask, int proto, int dir)
{
	switch (proto) {

	case Q_DEFAULT:
		return gen_host6(addr, mask, Q_IPV6, dir);

	case Q_IP:
		bpf_error("'ip' modifier applied to ip6 host");

	case Q_RARP:
		bpf_error("'rarp' modifier applied to ip6 host");

	case Q_ARP:
		bpf_error("'arp' modifier applied to ip6 host");

	case Q_TCP:
		bpf_error("'tcp' modifier applied to host");

	case Q_UDP:
		bpf_error("'udp' modifier applied to host");

	case Q_ICMP:
		bpf_error("'icmp' modifier applied to host");

	case Q_IGMP:
		bpf_error("'igmp' modifier applied to host");

	case Q_IGRP:
		bpf_error("'igrp' modifier applied to host");

	case Q_PIM:
		bpf_error("'pim' modifier applied to host");

	case Q_STP:
		bpf_error("'stp' modifier applied to host");

	case Q_ATALK:
		bpf_error("ATALK host filtering not implemented");

	case Q_DECNET:
		bpf_error("'decnet' modifier applied to ip6 host");

	case Q_SCA:
		bpf_error("SCA host filtering not implemented");

	case Q_LAT:
		bpf_error("LAT host filtering not implemented");

	case Q_MOPDL:
		bpf_error("MOPDL host filtering not implemented");

	case Q_MOPRC:
		bpf_error("MOPRC host filtering not implemented");

	case Q_IPV6:
		return gen_hostop6(addr, mask, dir, ETHERTYPE_IPV6,
				   8, 24);

	case Q_ICMPV6:
		bpf_error("'icmp6' modifier applied to host");

	case Q_AH:
		bpf_error("'ah' modifier applied to host");

	case Q_ESP:
		bpf_error("'esp' modifier applied to host");

	default:
		abort();
	}
	/* NOTREACHED */
}
#endif /*INET6*/

#ifndef INET6
static struct block *
gen_gateway(const u_char *eaddr, bpf_u_int32 **alist, int proto, int dir)
{
	struct block *b0, *b1, *tmp;

	if (dir != 0)
		bpf_error("direction applied to 'gateway'");

	switch (proto) {
	case Q_DEFAULT:
	case Q_IP:
	case Q_ARP:
	case Q_RARP:
		if (linktype == DLT_EN10MB)
			b0 = gen_ehostop(eaddr, Q_OR);
		else if (linktype == DLT_FDDI)
			b0 = gen_fhostop(eaddr, Q_OR);
		else
			bpf_error(
			    "'gateway' supported only on ethernet or FDDI");

		b1 = gen_host(**alist++, 0xffffffff, proto, Q_OR);
		while (*alist) {
			tmp = gen_host(**alist++, 0xffffffff, proto, Q_OR);
			gen_or(b1, tmp);
			b1 = tmp;
		}
		gen_not(b1);
		gen_and(b0, b1);
		return b1;
	}
	bpf_error("illegal modifier of 'gateway'");
	/* NOTREACHED */
}
#endif	/*INET6*/

struct block *
gen_proto_abbrev(int proto)
{
	struct block *b0 = NULL, *b1;

	switch (proto) {

	case Q_TCP:
		b1 = gen_proto(IPPROTO_TCP, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_TCP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

	case Q_UDP:
		b1 = gen_proto(IPPROTO_UDP, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_UDP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

	case Q_ICMP:
		b1 = gen_proto(IPPROTO_ICMP, Q_IP, Q_DEFAULT);
		break;

#ifndef	IPPROTO_IGMP
#define	IPPROTO_IGMP	2
#endif

	case Q_IGMP:
		b1 = gen_proto(IPPROTO_IGMP, Q_IP, Q_DEFAULT);
		break;

#ifndef	IPPROTO_IGRP
#define	IPPROTO_IGRP	9
#endif
	case Q_IGRP:
		b1 = gen_proto(IPPROTO_IGRP, Q_IP, Q_DEFAULT);
		break;

#ifndef IPPROTO_PIM
#define IPPROTO_PIM	103
#endif

	case Q_PIM:
		b1 = gen_proto(IPPROTO_PIM, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_PIM, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

	case Q_IP:
		b1 =  gen_linktype(ETHERTYPE_IP);
		break;

	case Q_ARP:
		b1 =  gen_linktype(ETHERTYPE_ARP);
		break;

	case Q_RARP:
		b1 =  gen_linktype(ETHERTYPE_REVARP);
		break;

	case Q_LINK:
		bpf_error("link layer applied in wrong context");

	case Q_ATALK:
		b1 =  gen_linktype(ETHERTYPE_ATALK);
		break;

	case Q_DECNET:
		b1 =  gen_linktype(ETHERTYPE_DN);
		break;

	case Q_SCA:
		b1 =  gen_linktype(ETHERTYPE_SCA);
		break;

	case Q_LAT:
		b1 =  gen_linktype(ETHERTYPE_LAT);
		break;

	case Q_MOPDL:
		b1 =  gen_linktype(ETHERTYPE_MOPDL);
		break;

	case Q_MOPRC:
		b1 =  gen_linktype(ETHERTYPE_MOPRC);
		break;

	case Q_STP:
		b1 = gen_linktype(LLCSAP_8021D);
		break;

#ifdef INET6
	case Q_IPV6:
		b1 = gen_linktype(ETHERTYPE_IPV6);
		break;

#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6	58
#endif
	case Q_ICMPV6:
		b1 = gen_proto(IPPROTO_ICMPV6, Q_IPV6, Q_DEFAULT);
		break;
#endif /* INET6 */

#ifndef IPPROTO_AH
#define IPPROTO_AH	51
#endif
	case Q_AH:
		b1 = gen_proto(IPPROTO_AH, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_AH, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

#ifndef IPPROTO_ESP
#define IPPROTO_ESP	50
#endif
	case Q_ESP:
		b1 = gen_proto(IPPROTO_ESP, Q_IP, Q_DEFAULT);
#ifdef INET6
		b0 = gen_proto(IPPROTO_ESP, Q_IPV6, Q_DEFAULT);
		gen_or(b0, b1);
#endif
		break;

	default:
		abort();
	}
	return b1;
}

static struct block *
gen_ipfrag(void)
{
	struct slist *s, *tmp;
	struct block *b;

	/* not ip frag */
	if (variable_nl) {
		s = nl2X_stmt();
		tmp = new_stmt(BPF_LD|BPF_H|BPF_IND);
		tmp->s.k = 6;
		sappend(s, tmp);
	} else {
		s = new_stmt(BPF_LD|BPF_H|BPF_ABS);
		s->s.k = off_nl + 6;
	}
	b = new_block(JMP(BPF_JSET));
	b->s.k = 0x1fff;
	b->stmts = s;
	gen_not(b);

	return b;
}

/* For dynamic off_nl, the BPF_LDX|BPF_MSH instruction does not work
   This function generates code to set X to the start of the IP payload
   X = off_nl + IP header_len.
*/
static struct slist *
iphl_to_x(void)
{
	struct slist *s, *tmp;

	/* XXX clobbers A if variable_nl*/
	if (variable_nl) {
		if (iphl_reg == -1) {
			/* X <- off_nl */
			s = nl2X_stmt();

			/* A = p[X+0] */
			tmp = new_stmt(BPF_LD|BPF_B|BPF_IND);
			tmp->s.k = 0;
			sappend(s, tmp);

			/* A = A & 0x0f */
			tmp = new_stmt(BPF_ALU|BPF_AND|BPF_K);
			tmp->s.k = 0x0f;
			sappend(s, tmp);

			/* A = A << 2 */
			tmp = new_stmt(BPF_ALU|BPF_LSH|BPF_K);
			tmp->s.k = 2;
			sappend(s, tmp);

			/* A = A + X (add off_nl again to compensate) */
			sappend(s, new_stmt(BPF_ALU|BPF_ADD|BPF_X));
			
			/* MEM[iphl_reg] = A */
			iphl_reg = alloc_reg();
			tmp = new_stmt(BPF_ST);
			tmp->s.k = iphl_reg;
			sappend(s, tmp);

			sappend(init_code, s);
		}
		s = new_stmt(BPF_LDX|BPF_MEM);
		s->s.k = iphl_reg;

	} else {
		s = new_stmt(BPF_LDX|BPF_MSH|BPF_B);
		s->s.k = off_nl;
	}

	return s;
}

static struct block *
gen_portatom(int off, bpf_int32 v)
{
	struct slist *s, *tmp;
	struct block *b;

	s = iphl_to_x();

	tmp = new_stmt(BPF_LD|BPF_IND|BPF_H);
	tmp->s.k = off_nl + off;	/* off_nl == 0 if variable_nl */
	sappend(s, tmp);

	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;
	b->s.k = v;

	return b;
}

#ifdef INET6
static struct block *
gen_portatom6(int off, bpf_int32 v)
{
	return gen_cmp_nl(40 + off, BPF_H, v);
}
#endif/*INET6*/

struct block *
gen_portop(int port, int proto, int dir)
{
	struct block *b0, *b1, *tmp;

	/* ip proto 'proto' */
	tmp = gen_cmp_nl(9, BPF_B, (bpf_int32)proto);
	b0 = gen_ipfrag();
	gen_and(tmp, b0);

	switch (dir) {
	case Q_SRC:
		b1 = gen_portatom(0, (bpf_int32)port);
		break;

	case Q_DST:
		b1 = gen_portatom(2, (bpf_int32)port);
		break;

	case Q_OR:
	case Q_DEFAULT:
		tmp = gen_portatom(0, (bpf_int32)port);
		b1 = gen_portatom(2, (bpf_int32)port);
		gen_or(tmp, b1);
		break;

	case Q_AND:
		tmp = gen_portatom(0, (bpf_int32)port);
		b1 = gen_portatom(2, (bpf_int32)port);
		gen_and(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_port(int port, int ip_proto, int dir)
{
	struct block *b0, *b1, *tmp;

	/* ether proto ip */
	b0 =  gen_linktype(ETHERTYPE_IP);

	switch (ip_proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
		b1 = gen_portop(port, ip_proto, dir);
		break;

	case PROTO_UNDEF:
		tmp = gen_portop(port, IPPROTO_TCP, dir);
		b1 = gen_portop(port, IPPROTO_UDP, dir);
		gen_or(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);
	return b1;
}

#ifdef INET6
struct block *
gen_portop6(int port, int proto, int dir)
{
	struct block *b0, *b1, *tmp;

	/* ip proto 'proto' */
	b0 = gen_cmp_nl(6, BPF_B, (bpf_int32)proto);

	switch (dir) {
	case Q_SRC:
		b1 = gen_portatom6(0, (bpf_int32)port);
		break;

	case Q_DST:
		b1 = gen_portatom6(2, (bpf_int32)port);
		break;

	case Q_OR:
	case Q_DEFAULT:
		tmp = gen_portatom6(0, (bpf_int32)port);
		b1 = gen_portatom6(2, (bpf_int32)port);
		gen_or(tmp, b1);
		break;

	case Q_AND:
		tmp = gen_portatom6(0, (bpf_int32)port);
		b1 = gen_portatom6(2, (bpf_int32)port);
		gen_and(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);

	return b1;
}

static struct block *
gen_port6(int port, int ip_proto, int dir)
{
	struct block *b0, *b1, *tmp;

	/* ether proto ip */
	b0 =  gen_linktype(ETHERTYPE_IPV6);

	switch (ip_proto) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
		b1 = gen_portop6(port, ip_proto, dir);
		break;

	case PROTO_UNDEF:
		tmp = gen_portop6(port, IPPROTO_TCP, dir);
		b1 = gen_portop6(port, IPPROTO_UDP, dir);
		gen_or(tmp, b1);
		break;

	default:
		abort();
	}
	gen_and(b0, b1);
	return b1;
}
#endif /* INET6 */

static int
lookup_proto(const char *name, int proto)
{
	int v;

	switch (proto) {

	case Q_DEFAULT:
	case Q_IP:
		v = pcap_nametoproto(name);
		if (v == PROTO_UNDEF)
			bpf_error("unknown ip proto '%s'", name);
		break;

	case Q_LINK:
		/* XXX should look up h/w protocol type based on linktype */
		v = pcap_nametoeproto(name);
		if (v == PROTO_UNDEF) {
			v = pcap_nametollc(name);
			if (v == PROTO_UNDEF)
				bpf_error("unknown ether proto '%s'", name);
		}
		break;

	default:
		v = PROTO_UNDEF;
		break;
	}
	return v;
}

static struct block *
gen_protochain(int v, int proto, int dir)
{
	struct block *b0, *b;
	struct slist *s[100];
	int fix2, fix3, fix4, fix5;
	int ahcheck, again, end;
	int i, max;
	int reg1 = alloc_reg();
	int reg2 = alloc_reg();

	memset(s, 0, sizeof(s));
	fix2 = fix3 = fix4 = fix5 = 0;

	if (variable_nl) {
		bpf_error("'gen_protochain' not supported for variable DLTs");
		/*NOTREACHED*/
	}

	switch (proto) {
	case Q_IP:
	case Q_IPV6:
		break;
	case Q_DEFAULT:
		b0 = gen_protochain(v, Q_IP, dir);
		b = gen_protochain(v, Q_IPV6, dir);
		gen_or(b0, b);
		return b;
	default:
		bpf_error("bad protocol applied for 'protochain'");
		/*NOTREACHED*/
	}

	no_optimize = 1; /*this code is not compatible with optimzer yet */

	/*
	 * s[0] is a dummy entry to protect other BPF insn from damaged
	 * by s[fix] = foo with uninitialized variable "fix".  It is somewhat
	 * hard to find interdependency made by jump table fixup.
	 */
	i = 0;
	s[i] = new_stmt(0);	/*dummy*/
	i++;

	switch (proto) {
	case Q_IP:
		b0 = gen_linktype(ETHERTYPE_IP);

		/* A = ip->ip_p */
		s[i] = new_stmt(BPF_LD|BPF_ABS|BPF_B);
		s[i]->s.k = off_nl + 9;
		i++;
		/* X = ip->ip_hl << 2 */
		s[i] = new_stmt(BPF_LDX|BPF_MSH|BPF_B);
		s[i]->s.k = off_nl;
		i++;
		break;
	case Q_IPV6:
		b0 = gen_linktype(ETHERTYPE_IPV6);

		/* A = ip6->ip_nxt */
		s[i] = new_stmt(BPF_LD|BPF_ABS|BPF_B);
		s[i]->s.k = off_nl + 6;
		i++;
		/* X = sizeof(struct ip6_hdr) */
		s[i] = new_stmt(BPF_LDX|BPF_IMM);
		s[i]->s.k = 40;
		i++;
		break;
	default:
		bpf_error("unsupported proto to gen_protochain");
		/*NOTREACHED*/
	}

	/* again: if (A == v) goto end; else fall through; */
	again = i;
	s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.k = v;
	s[i]->s.jt = NULL;		/*later*/
	s[i]->s.jf = NULL;		/*update in next stmt*/
	fix5 = i;
	i++;

	/* if (A == IPPROTO_NONE) goto end */
	s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.jt = NULL;	/*later*/
	s[i]->s.jf = NULL;	/*update in next stmt*/
	s[i]->s.k = IPPROTO_NONE;
	s[fix5]->s.jf = s[i];
	fix2 = i;
	i++;

	if (proto == Q_IPV6) {
		int v6start, v6end, v6advance, j;

		v6start = i;
		/* if (A == IPPROTO_HOPOPTS) goto v6advance */
		s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_HOPOPTS;
		s[fix2]->s.jf = s[i];
		i++;
		/* if (A == IPPROTO_DSTOPTS) goto v6advance */
		s[i - 1]->s.jf = s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_DSTOPTS;
		i++;
		/* if (A == IPPROTO_ROUTING) goto v6advance */
		s[i - 1]->s.jf = s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*update in next stmt*/
		s[i]->s.k = IPPROTO_ROUTING;
		i++;
		/* if (A == IPPROTO_FRAGMENT) goto v6advance; else goto ahcheck; */
		s[i - 1]->s.jf = s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
		s[i]->s.jt = NULL;	/*later*/
		s[i]->s.jf = NULL;	/*later*/
		s[i]->s.k = IPPROTO_FRAGMENT;
		fix3 = i;
		v6end = i;
		i++;

		/* v6advance: */
		v6advance = i;

		/*
		 * in short,
		 * A = P[X + 1];
		 * X = X + (P[X] + 1) * 8;
		 */
		/* A = X */
		s[i] = new_stmt(BPF_MISC|BPF_TXA);
		i++;
		/* MEM[reg1] = A */
		s[i] = new_stmt(BPF_ST);
		s[i]->s.k = reg1;
		i++;
		/* A += 1 */
		s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
		s[i]->s.k = 1;
		i++;
		/* X = A */
		s[i] = new_stmt(BPF_MISC|BPF_TAX);
		i++;
		/* A = P[X + packet head]; */
		s[i] = new_stmt(BPF_LD|BPF_IND|BPF_B);
		s[i]->s.k = off_nl;
		i++;
		/* MEM[reg2] = A */
		s[i] = new_stmt(BPF_ST);
		s[i]->s.k = reg2;
		i++;
		/* X = MEM[reg1] */
		s[i] = new_stmt(BPF_LDX|BPF_MEM);
		s[i]->s.k = reg1;
		i++;
		/* A = P[X + packet head] */
		s[i] = new_stmt(BPF_LD|BPF_IND|BPF_B);
		s[i]->s.k = off_nl;
		i++;
		/* A += 1 */
		s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
		s[i]->s.k = 1;
		i++;
		/* A *= 8 */
		s[i] = new_stmt(BPF_ALU|BPF_MUL|BPF_K);
		s[i]->s.k = 8;
		i++;
		/* X = A; */
		s[i] = new_stmt(BPF_MISC|BPF_TAX);
		i++;
		/* A = MEM[reg2] */
		s[i] = new_stmt(BPF_LD|BPF_MEM);
		s[i]->s.k = reg2;
		i++;

		/* goto again; (must use BPF_JA for backward jump) */
		s[i] = new_stmt(BPF_JMP|BPF_JA);
		s[i]->s.k = again - i - 1;
		s[i - 1]->s.jf = s[i];
		i++;

		/* fixup */
		for (j = v6start; j <= v6end; j++)
			s[j]->s.jt = s[v6advance];
	} else {
		/* nop */
		s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
		s[i]->s.k = 0;
		s[fix2]->s.jf = s[i];
		i++;
	}

	/* ahcheck: */
	ahcheck = i;
	/* if (A == IPPROTO_AH) then fall through; else goto end; */
	s[i] = new_stmt(BPF_JMP|BPF_JEQ|BPF_K);
	s[i]->s.jt = NULL;	/*later*/
	s[i]->s.jf = NULL;	/*later*/
	s[i]->s.k = IPPROTO_AH;
	if (fix3)
		s[fix3]->s.jf = s[ahcheck];
	fix4 = i;
	i++;

	/*
	 * in short,
	 * A = P[X + 1];
	 * X = X + (P[X] + 2) * 4;
	 */
	/* A = X */
	s[i - 1]->s.jt = s[i] = new_stmt(BPF_MISC|BPF_TXA);
	i++;
	/* MEM[reg1] = A */
	s[i] = new_stmt(BPF_ST);
	s[i]->s.k = reg1;
	i++;
	/* A += 1 */
	s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 1;
	i++;
	/* X = A */
	s[i] = new_stmt(BPF_MISC|BPF_TAX);
	i++;
	/* A = P[X + packet head]; */
	s[i] = new_stmt(BPF_LD|BPF_IND|BPF_B);
	s[i]->s.k = off_nl;
	i++;
	/* MEM[reg2] = A */
	s[i] = new_stmt(BPF_ST);
	s[i]->s.k = reg2;
	i++;
	/* X = MEM[reg1] */
	s[i] = new_stmt(BPF_LDX|BPF_MEM);
	s[i]->s.k = reg1;
	i++;
	/* A = P[X + packet head] */
	s[i] = new_stmt(BPF_LD|BPF_IND|BPF_B);
	s[i]->s.k = off_nl;
	i++;
	/* A += 2 */
	s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 2;
	i++;
	/* A *= 4 */
	s[i] = new_stmt(BPF_ALU|BPF_MUL|BPF_K);
	s[i]->s.k = 4;
	i++;
	/* X = A; */
	s[i] = new_stmt(BPF_MISC|BPF_TAX);
	i++;
	/* A = MEM[reg2] */
	s[i] = new_stmt(BPF_LD|BPF_MEM);
	s[i]->s.k = reg2;
	i++;

	/* goto again; (must use BPF_JA for backward jump) */
	s[i] = new_stmt(BPF_JMP|BPF_JA);
	s[i]->s.k = again - i - 1;
	i++;

	/* end: nop */
	end = i;
	s[i] = new_stmt(BPF_ALU|BPF_ADD|BPF_K);
	s[i]->s.k = 0;
	s[fix2]->s.jt = s[end];
	s[fix4]->s.jf = s[end];
	s[fix5]->s.jt = s[end];
	i++;

	/*
	 * make slist chain
	 */
	max = i;
	for (i = 0; i < max - 1; i++)
		s[i]->next = s[i + 1];
	s[max - 1]->next = NULL;

	/*
	 * emit final check
	 */
	b = new_block(JMP(BPF_JEQ));
	b->stmts = s[1];	/*remember, s[0] is dummy*/
	b->s.k = v;

	free_reg(reg1);
	free_reg(reg2);

	gen_and(b0, b);
	return b;
}

static struct block *
gen_proto(int v, int proto, int dir)
{
	struct block *b0, *b1;

	if (dir != Q_DEFAULT)
		bpf_error("direction applied to 'proto'");

	switch (proto) {
	case Q_DEFAULT:
#ifdef INET6
		b0 = gen_proto(v, Q_IP, dir);
		b1 = gen_proto(v, Q_IPV6, dir);
		gen_or(b0, b1);
		return b1;
#else
		/*FALLTHROUGH*/
#endif
	case Q_IP:
		b0 = gen_linktype(ETHERTYPE_IP);
#ifndef CHASE_CHAIN
		b1 = gen_cmp_nl(9, BPF_B, (bpf_int32)v);
#else
		b1 = gen_protochain(v, Q_IP);
#endif
		gen_and(b0, b1);
		return b1;

	case Q_ARP:
		bpf_error("arp does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_RARP:
		bpf_error("rarp does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_ATALK:
		bpf_error("atalk encapsulation is not specifiable");
		/* NOTREACHED */

	case Q_DECNET:
		bpf_error("decnet encapsulation is not specifiable");
		/* NOTREACHED */

	case Q_SCA:
		bpf_error("sca does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_LAT:
		bpf_error("lat does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_MOPRC:
		bpf_error("moprc does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_MOPDL:
		bpf_error("mopdl does not encapsulate another protocol");
		/* NOTREACHED */

	case Q_LINK:
		return gen_linktype(v);

	case Q_UDP:
		bpf_error("'udp proto' is bogus");
		/* NOTREACHED */

	case Q_TCP:
		bpf_error("'tcp proto' is bogus");
		/* NOTREACHED */

	case Q_ICMP:
		bpf_error("'icmp proto' is bogus");
		/* NOTREACHED */

	case Q_IGMP:
		bpf_error("'igmp proto' is bogus");
		/* NOTREACHED */

	case Q_IGRP:
		bpf_error("'igrp proto' is bogus");
		/* NOTREACHED */

	case Q_PIM:
		bpf_error("'pim proto' is bogus");
		/* NOTREACHED */

	case Q_STP:
		bpf_error("'stp proto' is bogus");
		/* NOTREACHED */

#ifdef INET6
	case Q_IPV6:
		b0 = gen_linktype(ETHERTYPE_IPV6);
#ifndef CHASE_CHAIN
		b1 = gen_cmp_nl(6, BPF_B, (bpf_int32)v);
#else
		b1 = gen_protochain(v, Q_IPV6);
#endif
		gen_and(b0, b1);
		return b1;

	case Q_ICMPV6:
		bpf_error("'icmp6 proto' is bogus");
#endif /* INET6 */

	case Q_AH:
		bpf_error("'ah proto' is bogus");

	case Q_ESP:
		bpf_error("'esp proto' is bogus");

	default:
		abort();
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

struct block *
gen_scode(const char *name, struct qual q)
{
	int proto = q.proto;
	int dir = q.dir;
	int tproto;
	u_char *eaddr;
	bpf_u_int32 mask, addr;
#ifndef INET6
	bpf_u_int32 **alist;
#else
	int tproto6;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	struct addrinfo *res, *res0;
	struct in6_addr mask128;
#endif /*INET6*/
	struct block *b, *tmp;
	int port, real_proto;

	switch (q.addr) {

	case Q_NET:
		addr = pcap_nametonetaddr(name);
		if (addr == 0)
			bpf_error("unknown network '%s'", name);
		/* Left justify network addr and calculate its network mask */
		mask = 0xffffffff;
		while (addr && (addr & 0xff000000) == 0) {
			addr <<= 8;
			mask <<= 8;
		}
		return gen_host(addr, mask, proto, dir);

	case Q_DEFAULT:
	case Q_HOST:
		if (proto == Q_LINK) {
			switch (linktype) {

			case DLT_EN10MB:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(
					    "unknown ether host '%s'", name);
				return gen_ehostop(eaddr, dir);

			case DLT_FDDI:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(
					    "unknown FDDI host '%s'", name);
				return gen_fhostop(eaddr, dir);

			case DLT_IEEE802_11:
			case DLT_IEEE802_11_RADIO:
				eaddr = pcap_ether_hostton(name);
				if (eaddr == NULL)
					bpf_error(
					    "unknown 802.11 host '%s'", name);

				return gen_p80211_hostop(eaddr, dir);

			default:
				bpf_error(
			"only ethernet/FDDI supports link-level host name");
				break;
			}
		} else if (proto == Q_DECNET) {
			unsigned short dn_addr = __pcap_nametodnaddr(name);
			/*
			 * I don't think DECNET hosts can be multihomed, so
			 * there is no need to build up a list of addresses
			 */
			return (gen_host(dn_addr, 0, proto, dir));
		} else {
#ifndef INET6
			alist = pcap_nametoaddr(name);
			if (alist == NULL || *alist == NULL)
				bpf_error("unknown host '%s'", name);
			tproto = proto;
			if (off_linktype == -1 && tproto == Q_DEFAULT)
				tproto = Q_IP;
			b = gen_host(**alist++, 0xffffffff, tproto, dir);
			while (*alist) {
				tmp = gen_host(**alist++, 0xffffffff,
					       tproto, dir);
				gen_or(b, tmp);
				b = tmp;
			}
			return b;
#else
			memset(&mask128, 0xff, sizeof(mask128));
			res0 = res = pcap_nametoaddrinfo(name);
			if (res == NULL)
				bpf_error("unknown host '%s'", name);
			b = tmp = NULL;
			tproto = tproto6 = proto;
			if (off_linktype == -1 && tproto == Q_DEFAULT) {
				tproto = Q_IP;
				tproto6 = Q_IPV6;
			}
			for (res = res0; res; res = res->ai_next) {
				switch (res->ai_family) {
				case AF_INET:
					if (tproto == Q_IPV6)
						continue;

					sin = (struct sockaddr_in *)
						res->ai_addr;
					tmp = gen_host(ntohl(sin->sin_addr.s_addr),
						0xffffffff, tproto, dir);
					break;
				case AF_INET6:
					if (tproto6 == Q_IP)
						continue;

					sin6 = (struct sockaddr_in6 *)
						res->ai_addr;
					tmp = gen_host6(&sin6->sin6_addr,
						&mask128, tproto6, dir);
					break;
				}
				if (b)
					gen_or(b, tmp);
				b = tmp;
			}
			freeaddrinfo(res0);
			if (b == NULL) {
				bpf_error("unknown host '%s'%s", name,
				    (proto == Q_DEFAULT)
					? ""
					: " for specified address family");
			}
			return b;
#endif /*INET6*/
		}

	case Q_PORT:
		if (proto != Q_DEFAULT && proto != Q_UDP && proto != Q_TCP)
			bpf_error("illegal qualifier of 'port'");
		if (pcap_nametoport(name, &port, &real_proto) == 0)
			bpf_error("unknown port '%s'", name);
		if (proto == Q_UDP) {
			if (real_proto == IPPROTO_TCP)
				bpf_error("port '%s' is tcp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_UDP;
		}
		if (proto == Q_TCP) {
			if (real_proto == IPPROTO_UDP)
				bpf_error("port '%s' is udp", name);
			else
				/* override PROTO_UNDEF */
				real_proto = IPPROTO_TCP;
		}
#ifndef INET6
		return gen_port(port, real_proto, dir);
#else
	    {
		struct block *b;
		b = gen_port(port, real_proto, dir);
		gen_or(gen_port6(port, real_proto, dir), b);
		return b;
	    }
#endif /* INET6 */

	case Q_GATEWAY:
#ifndef INET6
		eaddr = pcap_ether_hostton(name);
		if (eaddr == NULL)
			bpf_error("unknown ether host: %s", name);

		alist = pcap_nametoaddr(name);
		if (alist == NULL || *alist == NULL)
			bpf_error("unknown host '%s'", name);
		return gen_gateway(eaddr, alist, proto, dir);
#else
		bpf_error("'gateway' not supported in this configuration");
#endif /*INET6*/

	case Q_PROTO:
		real_proto = lookup_proto(name, proto);
		if (real_proto >= 0)
			return gen_proto(real_proto, proto, dir);
		else
			bpf_error("unknown protocol: %s", name);

	case Q_PROTOCHAIN:
		real_proto = lookup_proto(name, proto);
		if (real_proto >= 0)
			return gen_protochain(real_proto, proto, dir);
		else
			bpf_error("unknown protocol: %s", name);


	case Q_UNDEF:
		syntax();
		/* NOTREACHED */
	}
	abort();
	/* NOTREACHED */
}

struct block *
gen_mcode(const char *s1, const char *s2, int masklen, struct qual q)
{
	int nlen, mlen;
	bpf_u_int32 n, m;

	nlen = __pcap_atoin(s1, &n);
	/* Promote short ipaddr */
	n <<= 32 - nlen;

	if (s2 != NULL) {
		mlen = __pcap_atoin(s2, &m);
		/* Promote short ipaddr */
		m <<= 32 - mlen;
		if ((n & ~m) != 0)
			bpf_error("non-network bits set in \"%s mask %s\"",
			    s1, s2);
	} else {
		/* Convert mask len to mask */
		if (masklen > 32)
			bpf_error("mask length must be <= 32");
		m = 0xffffffff << (32 - masklen);
		if ((n & ~m) != 0)
			bpf_error("non-network bits set in \"%s/%d\"",
			    s1, masklen);
	}

	switch (q.addr) {

	case Q_NET:
		return gen_host(n, m, q.proto, q.dir);

	default:
		bpf_error("Mask syntax for networks only");
		/* NOTREACHED */
	}
}

struct block *
gen_ncode(const char *s, bpf_u_int32 v, struct qual q)
{
	bpf_u_int32 mask;
	int proto = q.proto;
	int dir = q.dir;
	int vlen;

	if (s == NULL)
		vlen = 32;
	else if (q.proto == Q_DECNET)
		vlen = __pcap_atodn(s, &v);
	else
		vlen = __pcap_atoin(s, &v);

	switch (q.addr) {

	case Q_DEFAULT:
	case Q_HOST:
	case Q_NET:
		if (proto == Q_DECNET)
			return gen_host(v, 0, proto, dir);
		else if (proto == Q_LINK) {
			bpf_error("illegal link layer address");
		} else {
			mask = 0xffffffff;
			if (s == NULL && q.addr == Q_NET) {
				/* Promote short net number */
				while (v && (v & 0xff000000) == 0) {
					v <<= 8;
					mask <<= 8;
				}
			} else {
				/* Promote short ipaddr */
				v <<= 32 - vlen;
				mask <<= 32 - vlen;
			}
			return gen_host(v, mask, proto, dir);
		}

	case Q_PORT:
		if (proto == Q_UDP)
			proto = IPPROTO_UDP;
		else if (proto == Q_TCP)
			proto = IPPROTO_TCP;
		else if (proto == Q_DEFAULT)
			proto = PROTO_UNDEF;
		else
			bpf_error("illegal qualifier of 'port'");

#ifndef INET6
		return gen_port((int)v, proto, dir);
#else
	    {
		struct block *b;
		b = gen_port((int)v, proto, dir);
		gen_or(gen_port6((int)v, proto, dir), b);
		return b;
	    }
#endif /* INET6 */

	case Q_GATEWAY:
		bpf_error("'gateway' requires a name");
		/* NOTREACHED */

	case Q_PROTO:
		return gen_proto((int)v, proto, dir);

	case Q_PROTOCHAIN:
		return gen_protochain((int)v, proto, dir);

	case Q_UNDEF:
		syntax();
		/* NOTREACHED */

	default:
		abort();
		/* NOTREACHED */
	}
	/* NOTREACHED */
}

#ifdef INET6
struct block *
gen_mcode6(const char *s1, const char *s2, int masklen, struct qual q)
{
	struct addrinfo *res;
	struct in6_addr *addr;
	struct in6_addr mask;
	struct block *b;
	u_int32_t *a, *m;

	if (s2)
		bpf_error("no mask %s supported", s2);

	res = pcap_nametoaddrinfo(s1);
	if (!res)
		bpf_error("invalid ip6 address %s", s1);
	if (res->ai_next)
		bpf_error("%s resolved to multiple address", s1);
	addr = &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;

	if (sizeof(mask) * 8 < masklen)
		bpf_error("mask length must be <= %u", (unsigned int)(sizeof(mask) * 8));
	memset(&mask, 0, sizeof(mask));
	memset(&mask, 0xff, masklen / 8);
	if (masklen % 8) {
		mask.s6_addr[masklen / 8] =
			(0xff << (8 - masklen % 8)) & 0xff;
	}

	a = (u_int32_t *)addr;
	m = (u_int32_t *)&mask;
	if ((a[0] & ~m[0]) || (a[1] & ~m[1])
	 || (a[2] & ~m[2]) || (a[3] & ~m[3])) {
		bpf_error("non-network bits set in \"%s/%d\"", s1, masklen);
	}

	switch (q.addr) {

	case Q_DEFAULT:
	case Q_HOST:
		if (masklen != 128)
			bpf_error("Mask syntax for networks only");
		/* FALLTHROUGH */

	case Q_NET:
		b = gen_host6(addr, &mask, q.proto, q.dir);
		freeaddrinfo(res);
		return b;

	default:
		bpf_error("invalid qualifier against IPv6 address");
		/* NOTREACHED */
	}
}
#endif /*INET6*/

struct block *
gen_ecode(const u_char *eaddr, struct qual q)
{
	if ((q.addr == Q_HOST || q.addr == Q_DEFAULT) && q.proto == Q_LINK) {
		if (linktype == DLT_EN10MB)
			return gen_ehostop(eaddr, (int)q.dir);
		if (linktype == DLT_FDDI)
			return gen_fhostop(eaddr, (int)q.dir);
		if (linktype == DLT_IEEE802_11 ||
		    linktype == DLT_IEEE802_11_RADIO)
			return gen_p80211_hostop(eaddr, (int)q.dir);
	}
	bpf_error("ethernet address used in non-ether expression");
	/* NOTREACHED */
}

void
sappend(struct slist *s0, struct slist *s1)
{
	/*
	 * This is definitely not the best way to do this, but the
	 * lists will rarely get long.
	 */
	while (s0->next)
		s0 = s0->next;
	s0->next = s1;
}

static struct slist *
xfer_to_x(struct arth *a)
{
	struct slist *s;

	s = new_stmt(BPF_LDX|BPF_MEM);
	s->s.k = a->regno;
	return s;
}

static struct slist *
xfer_to_a(struct arth *a)
{
	struct slist *s;

	s = new_stmt(BPF_LD|BPF_MEM);
	s->s.k = a->regno;
	return s;
}

struct arth *
gen_load(int proto, struct arth *index, int size)
{
	struct slist *s, *tmp;
	struct block *b;
	int regno = alloc_reg();

	free_reg(index->regno);
	switch (size) {

	default:
		bpf_error("data size must be 1, 2, or 4");

	case 1:
		size = BPF_B;
		break;

	case 2:
		size = BPF_H;
		break;

	case 4:
		size = BPF_W;
		break;
	}
	switch (proto) {
	default:
		bpf_error("unsupported index operation");

	case Q_LINK:
		s = xfer_to_x(index);
		tmp = new_stmt(BPF_LD|BPF_IND|size);
		sappend(s, tmp);
		sappend(index->s, s);
		break;

	case Q_IP:
	case Q_ARP:
	case Q_RARP:
	case Q_ATALK:
	case Q_DECNET:
	case Q_SCA:
	case Q_LAT:
	case Q_MOPRC:
	case Q_MOPDL:
#ifdef INET6
	case Q_IPV6:
#endif
		/* XXX Note that we assume a fixed link header here. */
		if (variable_nl) {
			s = nl2X_stmt();
			sappend(s, xfer_to_a(index));
			sappend(s, new_stmt(BPF_ALU|BPF_ADD|BPF_X));
			sappend(s, new_stmt(BPF_MISC|BPF_TAX));
		} else {
			s = xfer_to_x(index);
		}
		tmp = new_stmt(BPF_LD|BPF_IND|size);
		tmp->s.k = off_nl;	/* off_nl == 0 for variable_nl */
		sappend(s, tmp);
		sappend(index->s, s);

		b = gen_proto_abbrev(proto);
		if (index->b)
			gen_and(index->b, b);
		index->b = b;
		break;

	case Q_TCP:
	case Q_UDP:
	case Q_ICMP:
	case Q_IGMP:
	case Q_IGRP:
	case Q_PIM:
		s = iphl_to_x();
		sappend(s, xfer_to_a(index));
		sappend(s, new_stmt(BPF_ALU|BPF_ADD|BPF_X));
		sappend(s, new_stmt(BPF_MISC|BPF_TAX));
		sappend(s, tmp = new_stmt(BPF_LD|BPF_IND|size));
		tmp->s.k = off_nl;	/* off_nl is 0 if variable_nl */
		sappend(index->s, s);

		gen_and(gen_proto_abbrev(proto), b = gen_ipfrag());
		if (index->b)
			gen_and(index->b, b);
#ifdef INET6
		gen_and(gen_proto_abbrev(Q_IP), b);
#endif
		index->b = b;
		break;
#ifdef INET6
	case Q_ICMPV6:
		bpf_error("IPv6 upper-layer protocol is not supported by proto[x]");
		/*NOTREACHED*/
#endif
	}
	index->regno = regno;
	s = new_stmt(BPF_ST);
	s->s.k = regno;
	sappend(index->s, s);

	return index;
}

struct block *
gen_relation(int code, struct arth *a0, struct arth *a1, int reversed)
{
	struct slist *s0, *s1, *s2;
	struct block *b, *tmp;

	s0 = xfer_to_x(a1);
	s1 = xfer_to_a(a0);
	s2 = new_stmt(BPF_ALU|BPF_SUB|BPF_X);
	b = new_block(JMP(code));
	if (code == BPF_JGT || code == BPF_JGE) {
		reversed = !reversed;
		b->s.k = 0x80000000;
	}
	if (reversed)
		gen_not(b);

	sappend(s1, s2);
	sappend(s0, s1);
	sappend(a1->s, s0);
	sappend(a0->s, a1->s);

	b->stmts = a0->s;

	free_reg(a0->regno);
	free_reg(a1->regno);

	/* 'and' together protocol checks */
	if (a0->b) {
		if (a1->b) {
			gen_and(a0->b, tmp = a1->b);
		}
		else
			tmp = a0->b;
	} else
		tmp = a1->b;

	if (tmp)
		gen_and(tmp, b);

	return b;
}

struct arth *
gen_loadlen(void)
{
	int regno = alloc_reg();
	struct arth *a = (struct arth *)newchunk(sizeof(*a));
	struct slist *s;

	s = new_stmt(BPF_LD|BPF_LEN);
	s->next = new_stmt(BPF_ST);
	s->next->s.k = regno;
	a->s = s;
	a->regno = regno;

	return a;
}

struct arth *
gen_loadrnd(void)
{
	int regno = alloc_reg();
	struct arth *a = (struct arth *)newchunk(sizeof(*a));
	struct slist *s;

	s = new_stmt(BPF_LD|BPF_RND);
	s->next = new_stmt(BPF_ST);
	s->next->s.k = regno;
	a->s = s;
	a->regno = regno;

	return a;
}

struct arth *
gen_loadi(int val)
{
	struct arth *a;
	struct slist *s;
	int reg;

	a = (struct arth *)newchunk(sizeof(*a));

	reg = alloc_reg();

	s = new_stmt(BPF_LD|BPF_IMM);
	s->s.k = val;
	s->next = new_stmt(BPF_ST);
	s->next->s.k = reg;
	a->s = s;
	a->regno = reg;

	return a;
}

struct arth *
gen_neg(struct arth *a)
{
	struct slist *s;

	s = xfer_to_a(a);
	sappend(a->s, s);
	s = new_stmt(BPF_ALU|BPF_NEG);
	s->s.k = 0;
	sappend(a->s, s);
	s = new_stmt(BPF_ST);
	s->s.k = a->regno;
	sappend(a->s, s);

	return a;
}

struct arth *
gen_arth(int code, struct arth *a0, struct arth *a1)
{
	struct slist *s0, *s1, *s2;

	s0 = xfer_to_x(a1);
	s1 = xfer_to_a(a0);
	s2 = new_stmt(BPF_ALU|BPF_X|code);

	sappend(s1, s2);
	sappend(s0, s1);
	sappend(a1->s, s0);
	sappend(a0->s, a1->s);

	free_reg(a1->regno);

	s0 = new_stmt(BPF_ST);
	a0->regno = s0->s.k = alloc_reg();
	sappend(a0->s, s0);

	return a0;
}

/*
 * Here we handle simple allocation of the scratch registers.
 * If too many registers are alloc'd, the allocator punts.
 */
static int regused[BPF_MEMWORDS];
static int curreg;

/*
 * Return the next free register.
 */
static int
alloc_reg(void)
{
	int n = BPF_MEMWORDS;

	while (--n >= 0) {
		if (regused[curreg])
			curreg = (curreg + 1) % BPF_MEMWORDS;
		else {
			regused[curreg] = 1;
			return curreg;
		}
	}
	bpf_error("too many registers needed to evaluate expression");
	/* NOTREACHED */
}

/*
 * Return a register to the table so it can
 * be used later.
 */
static void
free_reg(int n)
{
	regused[n] = 0;
}

static struct block *
gen_len(int jmp, int n)
{
	struct slist *s;
	struct block *b;

	s = new_stmt(BPF_LD|BPF_LEN);
	b = new_block(JMP(jmp));
	b->stmts = s;
	b->s.k = n;

	return b;
}

struct block *
gen_greater(int n)
{
	return gen_len(BPF_JGE, n);
}

struct block *
gen_less(int n)
{
	struct block *b;

	b = gen_len(BPF_JGT, n);
	gen_not(b);

	return b;
}

struct block *
gen_byteop(int op, int idx, int val)
{
	struct block *b;
	struct slist *s;

	switch (op) {
	default:
		abort();

	case '=':
		return gen_cmp((u_int)idx, BPF_B, (bpf_int32)val);

	case '<':
		b = gen_cmp((u_int)idx, BPF_B, (bpf_int32)val);
		b->s.code = JMP(BPF_JGE);
		gen_not(b);
		return b;

	case '>':
		b = gen_cmp((u_int)idx, BPF_B, (bpf_int32)val);
		b->s.code = JMP(BPF_JGT);
		return b;

	case '|':
		s = new_stmt(BPF_ALU|BPF_OR|BPF_K);
		break;

	case '&':
		s = new_stmt(BPF_ALU|BPF_AND|BPF_K);
		break;
	}
	s->s.k = val;
	b = new_block(JMP(BPF_JEQ));
	b->stmts = s;
	gen_not(b);

	return b;
}

struct block *
gen_broadcast(int proto)
{
	bpf_u_int32 hostmask;
	struct block *b0, *b1, *b2;
	static u_char ebroadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	switch (proto) {

	case Q_DEFAULT:
	case Q_LINK:
		if (linktype == DLT_EN10MB)
			return gen_ehostop(ebroadcast, Q_DST);
		if (linktype == DLT_FDDI)
			return gen_fhostop(ebroadcast, Q_DST);
		if (linktype == DLT_IEEE802_11 ||
		    linktype == DLT_IEEE802_11_RADIO)
			return gen_p80211_hostop(ebroadcast, Q_DST);
		bpf_error("not a broadcast link");
		break;

	case Q_IP:
		/*
		 * We treat a netmask of PCAP_NETMASK_UNKNOWN (0xffffffff)
		 * as an indication that we don't know the netmask, and fail
		 * in that case.
		 */
		if (netmask == PCAP_NETMASK_UNKNOWN)
			bpf_error("netmask not known, so 'ip broadcast' not supported");
		b0 = gen_linktype(ETHERTYPE_IP);
		hostmask = ~netmask;
		b1 = gen_mcmp_nl(16, BPF_W, (bpf_int32)0, hostmask);
		b2 = gen_mcmp_nl(16, BPF_W,
			      (bpf_int32)(~0 & hostmask), hostmask);
		gen_or(b1, b2);
		gen_and(b0, b2);
		return b2;
	}
	bpf_error("only ether/ip broadcast filters supported");
}

struct block *
gen_multicast(int proto)
{
	struct block *b0, *b1;
	struct slist *s;

	switch (proto) {

	case Q_DEFAULT:
	case Q_LINK:
		if (linktype == DLT_EN10MB) {
			/* ether[0] & 1 != 0 */
			s = new_stmt(BPF_LD|BPF_B|BPF_ABS);
			s->s.k = 0;
			b0 = new_block(JMP(BPF_JSET));
			b0->s.k = 1;
			b0->stmts = s;
			return b0;
		}

		if (linktype == DLT_FDDI) {
			/* XXX TEST THIS: MIGHT NOT PORT PROPERLY XXX */
			/* fddi[1] & 1 != 0 */
			s = new_stmt(BPF_LD|BPF_B|BPF_ABS);
			s->s.k = 1;
			b0 = new_block(JMP(BPF_JSET));
			b0->s.k = 1;
			b0->stmts = s;
			return b0;
		}
		/* Link not known to support multicasts */
		break;

	case Q_IP:
		b0 = gen_linktype(ETHERTYPE_IP);
		b1 = gen_cmp_nl(16, BPF_B, (bpf_int32)224);
		b1->s.code = JMP(BPF_JGE);
		gen_and(b0, b1);
		return b1;

#ifdef INET6
	case Q_IPV6:
		b0 = gen_linktype(ETHERTYPE_IPV6);
		b1 = gen_cmp_nl(24, BPF_B, (bpf_int32)255);
		gen_and(b0, b1);
		return b1;
#endif /* INET6 */
	}
	bpf_error("only IP multicast filters supported on ethernet/FDDI");
}

/*
 * generate command for inbound/outbound.  It's here so we can
 * make it link-type specific.  'dir' = 0 implies "inbound",
 * = 1 implies "outbound".
 */
struct block *
gen_inbound(int dir)
{
	struct block *b0;

	/*
	 * Only SLIP and old-style PPP data link types support
	 * inbound/outbound qualifiers.
	 */
	switch (linktype) {
	case DLT_SLIP:
	case DLT_PPP:
		b0 = gen_relation(BPF_JEQ,
				  gen_load(Q_LINK, gen_loadi(0), 1),
				  gen_loadi(0),
				  dir);
		break;

	case DLT_PFLOG:
		b0 = gen_cmp(offsetof(struct pfloghdr, dir), BPF_B,
		    (bpf_int32)((dir == 0) ? PF_IN : PF_OUT));
		break;

	default:
		bpf_error("inbound/outbound not supported on linktype 0x%x",
		    linktype);
		/* NOTREACHED */
	}

	return (b0);
}


/* PF firewall log matched interface */
struct block *
gen_pf_ifname(char *ifname)
{
	struct block *b0;
	u_int len, off;

	if (linktype == DLT_PFLOG) {
		len = sizeof(((struct pfloghdr *)0)->ifname);
		off = offsetof(struct pfloghdr, ifname);
	} else {
		bpf_error("ifname not supported on linktype 0x%x", linktype);
		/* NOTREACHED */
	}
	if (strlen(ifname) >= len) {
		bpf_error("ifname interface names can only be %d characters",
		    len - 1);
		/* NOTREACHED */
	}
	b0 = gen_bcmp(off, strlen(ifname) + 1, ifname);
	return (b0);
}


/* PF firewall log ruleset name */
struct block *
gen_pf_ruleset(char *ruleset)
{
	struct block *b0;

	if (linktype != DLT_PFLOG) {
		bpf_error("ruleset not supported on linktype 0x%x", linktype);
		/* NOTREACHED */
	}
	if (strlen(ruleset) >= sizeof(((struct pfloghdr *)0)->ruleset)) {
		bpf_error("ruleset names can only be %zu characters",
		    sizeof(((struct pfloghdr *)0)->ruleset) - 1);
		/* NOTREACHED */
	}
	b0 = gen_bcmp(offsetof(struct pfloghdr, ruleset),
	    strlen(ruleset), ruleset);
	return (b0);
}


/* PF firewall log rule number */
struct block *
gen_pf_rnr(int rnr)
{
	struct block *b0;

	if (linktype == DLT_PFLOG) {
		b0 = gen_cmp(offsetof(struct pfloghdr, rulenr), BPF_W,
			 (bpf_int32)rnr);
	} else {
		bpf_error("rnr not supported on linktype 0x%x", linktype);
		/* NOTREACHED */
	}

	return (b0);
}


/* PF firewall log sub-rule number */
struct block *
gen_pf_srnr(int srnr)
{
	struct block *b0;

	if (linktype != DLT_PFLOG) {
		bpf_error("srnr not supported on linktype 0x%x", linktype);
		/* NOTREACHED */
	}

	b0 = gen_cmp(offsetof(struct pfloghdr, subrulenr), BPF_W,
	    (bpf_int32)srnr);
	return (b0);
}

/* PF firewall log reason code */
struct block *
gen_pf_reason(int reason)
{
	struct block *b0;

	if (linktype == DLT_PFLOG) {
		b0 = gen_cmp(offsetof(struct pfloghdr, reason), BPF_B,
		    (bpf_int32)reason);
	} else {
		bpf_error("reason not supported on linktype 0x%x", linktype);
		/* NOTREACHED */
	}

	return (b0);
}

/* PF firewall log action */
struct block *
gen_pf_action(int action)
{
	struct block *b0;

	if (linktype == DLT_PFLOG) {
		b0 = gen_cmp(offsetof(struct pfloghdr, action), BPF_B,
		    (bpf_int32)action);
	} else {
		bpf_error("action not supported on linktype 0x%x", linktype);
		/* NOTREACHED */
	}

	return (b0);
}

/* IEEE 802.11 wireless header */
struct block *
gen_p80211_type(int type, int mask)
{
	struct block *b0;
	u_int offset;

	if (!(linktype == DLT_IEEE802_11 ||
	    linktype == DLT_IEEE802_11_RADIO)) {
		bpf_error("type not supported on linktype 0x%x",
		    linktype);
		/* NOTREACHED */
	}
	offset = (u_int)offsetof(struct ieee80211_frame, i_fc[0]);
	if (linktype == DLT_IEEE802_11_RADIO)
		offset += IEEE80211_RADIOTAP_HDRLEN;

	b0 = gen_mcmp(offset, BPF_B, (bpf_int32)type, (bpf_u_int32)mask);

	return (b0);
}

static struct block *
gen_ahostop(const u_char *eaddr, int dir)
{
	struct block *b0, *b1;

	switch (dir) {
	/* src comes first, different from Ethernet */
	case Q_SRC:
		return gen_bcmp(0, 1, eaddr);

	case Q_DST:
		return gen_bcmp(1, 1, eaddr);

	case Q_AND:
		b0 = gen_ahostop(eaddr, Q_SRC);
		b1 = gen_ahostop(eaddr, Q_DST);
		gen_and(b0, b1);
		return b1;

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_ahostop(eaddr, Q_SRC);
		b1 = gen_ahostop(eaddr, Q_DST);
		gen_or(b0, b1);
		return b1;
	}
	abort();
	/* NOTREACHED */
}

struct block *
gen_acode(const u_char *eaddr, struct qual q)
{
	if ((q.addr == Q_HOST || q.addr == Q_DEFAULT) && q.proto == Q_LINK) {
		if (linktype == DLT_ARCNET)
			return gen_ahostop(eaddr, (int)q.dir);
	}
	bpf_error("ARCnet address used in non-arc expression");
	/* NOTREACHED */
}

struct block *
gen_mpls(int label)
{
	struct block	*b0;

	if (label > MPLS_LABEL_MAX)
		bpf_error("invalid MPLS label : %d", label);

	if (mpls_stack > 0) /* Bottom-Of-Label-Stack bit ? */
		b0 = gen_mcmp(off_nl-2, BPF_B, (bpf_int32)0, 0x1);
	else 
		b0 = gen_linktype(ETHERTYPE_MPLS);

	if (label >= 0) {
		struct block *b1;

		b1 = gen_mcmp(off_nl, BPF_W, (bpf_int32)(label << 12),
		    MPLS_LABEL_MASK);
		gen_and(b0, b1);
		b0 = b1;
	}
	off_nl += 4;
	off_linktype += 4;
	mpls_stack++;
	return (b0);
}

/*
 * support IEEE 802.1Q VLAN trunk over ethernet
 */
struct block *
gen_vlan(int vlan_num)
{
	struct	block	*b0;

	if (variable_nl) {
		bpf_error("'vlan' not supported for variable DLTs");
		/*NOTREACHED*/
	}

	if (vlan_num > 4095) {
		bpf_error("invalid VLAN number : %d", vlan_num);
		/*NOTREACHED*/
	}

	/*
	 * Change the offsets to point to the type and data fields within
	 * the VLAN packet.  This is somewhat of a kludge.
	 */
	if (orig_nl == (u_int)-1) {
		orig_linktype = off_linktype;	/* save original values */
		orig_nl = off_nl;
		orig_nl_nosnap = off_nl_nosnap;

		switch (linktype) {

		case DLT_EN10MB:
			off_linktype = 16;
			off_nl_nosnap = 18;
			off_nl = 18;
			break;

		default:
			bpf_error("no VLAN support for data link type %d",
				  linktype);
			/*NOTREACHED*/
		}
	}

	/* check for VLAN */
	b0 = gen_cmp(orig_linktype, BPF_H, (bpf_int32)ETHERTYPE_8021Q);

	/* If a specific VLAN is requested, check VLAN id */
	if (vlan_num >= 0) {
		struct block *b1;

		b1 = gen_mcmp(orig_nl, BPF_H, (bpf_int32)vlan_num, 0x0FFF);
		gen_and(b0, b1);
		b0 = b1;
	}

	return (b0);
}

struct block *
gen_sample(int rate)
{
	struct block *b0;
	long long threshold = 0x100000000LL; /* 0xffffffff + 1 */

	if (rate < 2) {
		bpf_error("sample %d is too low", rate);
		/*NOTREACHED*/
	}
	if (rate > (1 << 20)) {
		bpf_error("sample %d is too high", rate);
		/*NOTREACHED*/
	}

	threshold /= rate;
	b0 = gen_relation(BPF_JGT, gen_loadrnd(), gen_loadi(threshold), 1);

	return (b0);
}

struct block *
gen_p80211_fcdir(int fcdir)
{
	struct block *b0;
	u_int offset;

	if (!(linktype == DLT_IEEE802_11 ||
	    linktype == DLT_IEEE802_11_RADIO)) {
		bpf_error("frame direction not supported on linktype 0x%x",
		    linktype);
		/* NOTREACHED */
	}
	offset = (u_int)offsetof(struct ieee80211_frame, i_fc[1]);
	if (linktype == DLT_IEEE802_11_RADIO)
		offset += IEEE80211_RADIOTAP_HDRLEN;

	b0 = gen_mcmp(offset, BPF_B, (bpf_int32)fcdir,
	    (bpf_u_int32)IEEE80211_FC1_DIR_MASK);

	return (b0);
}

static struct block *
gen_p80211_hostop(const u_char *lladdr, int dir)
{
	struct block *b0, *b1, *b2, *b3, *b4;
	u_int offset = 0;

	if (linktype == DLT_IEEE802_11_RADIO)
		offset = IEEE80211_RADIOTAP_HDRLEN;

	switch (dir) {
	case Q_SRC:
		b0 = gen_p80211_addr(IEEE80211_FC1_DIR_NODS, offset +
		    (u_int)offsetof(struct ieee80211_frame, i_addr2),
		    lladdr);
		b1 = gen_p80211_addr(IEEE80211_FC1_DIR_TODS, offset +
		    (u_int)offsetof(struct ieee80211_frame, i_addr2),
		    lladdr);
		b2 = gen_p80211_addr(IEEE80211_FC1_DIR_FROMDS, offset +
		    (u_int)offsetof(struct ieee80211_frame, i_addr3),
		    lladdr);
		b3 = gen_p80211_addr(IEEE80211_FC1_DIR_DSTODS, offset +
		    (u_int)offsetof(struct ieee80211_frame_addr4, i_addr4),
		    lladdr);
		b4 = gen_p80211_addr(IEEE80211_FC1_DIR_DSTODS, offset +
		    (u_int)offsetof(struct ieee80211_frame_addr4, i_addr2),
		    lladdr);

		gen_or(b0, b1);
		gen_or(b1, b2);
		gen_or(b2, b3);
		gen_or(b3, b4);
		return (b4);

	case Q_DST:
		b0 = gen_p80211_addr(IEEE80211_FC1_DIR_NODS, offset +
		    (u_int)offsetof(struct ieee80211_frame, i_addr1),
		    lladdr);
		b1 = gen_p80211_addr(IEEE80211_FC1_DIR_TODS, offset +
		    (u_int)offsetof(struct ieee80211_frame, i_addr3),
		    lladdr);
		b2 = gen_p80211_addr(IEEE80211_FC1_DIR_FROMDS, offset +
		    (u_int)offsetof(struct ieee80211_frame, i_addr1),
		    lladdr);
		b3 = gen_p80211_addr(IEEE80211_FC1_DIR_DSTODS, offset +
		    (u_int)offsetof(struct ieee80211_frame_addr4, i_addr3),
		    lladdr);
		b4 = gen_p80211_addr(IEEE80211_FC1_DIR_DSTODS, offset +
		    (u_int)offsetof(struct ieee80211_frame_addr4, i_addr1),
		    lladdr);

		gen_or(b0, b1);
		gen_or(b1, b2);
		gen_or(b2, b3);
		gen_or(b3, b4);
		return (b4);

	case Q_ADDR1:
		return (gen_bcmp(offset +
		    (u_int)offsetof(struct ieee80211_frame,
		    i_addr1), IEEE80211_ADDR_LEN, lladdr));

	case Q_ADDR2:
		return (gen_bcmp(offset +
		    (u_int)offsetof(struct ieee80211_frame,
		    i_addr2), IEEE80211_ADDR_LEN, lladdr));

	case Q_ADDR3:
		return (gen_bcmp(offset +
		    (u_int)offsetof(struct ieee80211_frame,
		    i_addr3), IEEE80211_ADDR_LEN, lladdr));

	case Q_ADDR4:
		return (gen_p80211_addr(IEEE80211_FC1_DIR_DSTODS, offset +
		    (u_int)offsetof(struct ieee80211_frame_addr4, i_addr4),
		    lladdr));

	case Q_AND:
		b0 = gen_p80211_hostop(lladdr, Q_SRC);
		b1 = gen_p80211_hostop(lladdr, Q_DST);
		gen_and(b0, b1);
		return (b1);

	case Q_DEFAULT:
	case Q_OR:
		b0 = gen_p80211_hostop(lladdr, Q_ADDR1);
		b1 = gen_p80211_hostop(lladdr, Q_ADDR2);
		b2 = gen_p80211_hostop(lladdr, Q_ADDR3);
		b3 = gen_p80211_hostop(lladdr, Q_ADDR4);
		gen_or(b0, b1);
		gen_or(b1, b2);
		gen_or(b2, b3);
		return (b3);

	default:
		bpf_error("direction not supported on linktype 0x%x",
		    linktype);
	}
	/* NOTREACHED */
}

static struct block *
gen_p80211_addr(int fcdir, u_int offset, const u_char *lladdr)
{
	struct block *b0, *b1;

	b0 = gen_mcmp(offset, BPF_B, (bpf_int32)fcdir, IEEE80211_FC1_DIR_MASK);
	b1 = gen_bcmp(offset, IEEE80211_ADDR_LEN, lladdr);
	gen_and(b0, b1);

	return (b1);
}
