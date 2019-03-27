/*	$OpenBSD: pfctl_parser.h,v 1.86 2006/10/31 23:46:25 mcbride Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _PFCTL_PARSER_H_
#define _PFCTL_PARSER_H_

#define PF_OSFP_FILE		"/etc/pf.os"

#define PF_OPT_DISABLE		0x0001
#define PF_OPT_ENABLE		0x0002
#define PF_OPT_VERBOSE		0x0004
#define PF_OPT_NOACTION		0x0008
#define PF_OPT_QUIET		0x0010
#define PF_OPT_CLRRULECTRS	0x0020
#define PF_OPT_USEDNS		0x0040
#define PF_OPT_VERBOSE2		0x0080
#define PF_OPT_DUMMYACTION	0x0100
#define PF_OPT_DEBUG		0x0200
#define PF_OPT_SHOWALL		0x0400
#define PF_OPT_OPTIMIZE		0x0800
#define PF_OPT_NUMERIC		0x1000
#define PF_OPT_MERGE		0x2000
#define PF_OPT_RECURSE		0x4000

#define PF_TH_ALL		0xFF

#define PF_NAT_PROXY_PORT_LOW	50001
#define PF_NAT_PROXY_PORT_HIGH	65535

#define PF_OPTIMIZE_BASIC	0x0001
#define PF_OPTIMIZE_PROFILE	0x0002

#define FCNT_NAMES { \
	"searches", \
	"inserts", \
	"removals", \
	NULL \
}

struct pfr_buffer;	/* forward definition */


struct pfctl {
	int dev;
	int opts;
	int optimize;
	int loadopt;
	int asd;			/* anchor stack depth */
	int bn;				/* brace number */
	int brace;
	int tdirty;			/* kernel dirty */
#define PFCTL_ANCHOR_STACK_DEPTH 64
	struct pf_anchor *astack[PFCTL_ANCHOR_STACK_DEPTH];
	struct pfioc_pooladdr paddr;
	struct pfioc_altq *paltq;
	struct pfioc_queue *pqueue;
	struct pfr_buffer *trans;
	struct pf_anchor *anchor, *alast;
	const char *ruleset;

	/* 'set foo' options */
	u_int32_t	 timeout[PFTM_MAX];
	u_int32_t	 limit[PF_LIMIT_MAX];
	u_int32_t	 debug;
	u_int32_t	 hostid;
	char		*ifname;

	u_int8_t	 timeout_set[PFTM_MAX];
	u_int8_t	 limit_set[PF_LIMIT_MAX];
	u_int8_t	 debug_set;
	u_int8_t	 hostid_set;
	u_int8_t	 ifname_set;
};

struct node_if {
	char			 ifname[IFNAMSIZ];
	u_int8_t		 not;
	u_int8_t		 dynamic; /* antispoof */
	u_int			 ifa_flags;
	struct node_if		*next;
	struct node_if		*tail;
};

struct node_host {
	struct pf_addr_wrap	 addr;
	struct pf_addr		 bcast;
	struct pf_addr		 peer;
	sa_family_t		 af;
	u_int8_t		 not;
	u_int32_t		 ifindex;	/* link-local IPv6 addrs */
	char			*ifname;
	u_int			 ifa_flags;
	struct node_host	*next;
	struct node_host	*tail;
};

struct node_os {
	char			*os;
	pf_osfp_t		 fingerprint;
	struct node_os		*next;
	struct node_os		*tail;
};

struct node_queue_bw {
	u_int64_t	bw_absolute;
	u_int16_t	bw_percent;
};

struct node_hfsc_sc {
	struct node_queue_bw	m1;	/* slope of 1st segment; bps */
	u_int			d;	/* x-projection of m1; msec */
	struct node_queue_bw	m2;	/* slope of 2nd segment; bps */
	u_int8_t		used;
};

struct node_hfsc_opts {
	struct node_hfsc_sc	realtime;
	struct node_hfsc_sc	linkshare;
	struct node_hfsc_sc	upperlimit;
	int			flags;
};

struct node_fairq_sc {
	struct node_queue_bw	m1;	/* slope of 1st segment; bps */
	u_int			d;	/* x-projection of m1; msec */
	struct node_queue_bw	m2;	/* slope of 2nd segment; bps */
	u_int8_t		used;
};

struct node_fairq_opts {
	struct node_fairq_sc	linkshare;
	struct node_queue_bw	hogs_bw;
	u_int			nbuckets;
	int			flags;
};

struct node_queue_opt {
	int			 qtype;
	union {
		struct cbq_opts		cbq_opts;
		struct codel_opts	codel_opts;
		struct priq_opts	priq_opts;
		struct node_hfsc_opts	hfsc_opts;
		struct node_fairq_opts	fairq_opts;
	}			 data;
};

#define QPRI_BITSET_SIZE	256
BITSET_DEFINE(qpri_bitset, QPRI_BITSET_SIZE);
LIST_HEAD(gen_sc, segment);

struct pfctl_altq {
	struct pf_altq	pa;
	struct {
		STAILQ_ENTRY(pfctl_altq)	link;
		u_int64_t			bwsum;
		struct qpri_bitset		qpris;
		int				children;
		int				root_classes;
		int				default_classes;
		struct gen_sc			lssc;
		struct gen_sc			rtsc;
	} meta;
};

#ifdef __FreeBSD__
/*
 * XXX
 * Absolutely this is not correct location to define this.
 * Should we use an another sperate header file?
 */
#define	SIMPLEQ_HEAD			STAILQ_HEAD
#define	SIMPLEQ_HEAD_INITIALIZER	STAILQ_HEAD_INITIALIZER
#define	SIMPLEQ_ENTRY			STAILQ_ENTRY
#define	SIMPLEQ_FIRST			STAILQ_FIRST
#define	SIMPLEQ_END(head)		NULL
#define	SIMPLEQ_EMPTY			STAILQ_EMPTY
#define	SIMPLEQ_NEXT			STAILQ_NEXT
/*#define	SIMPLEQ_FOREACH			STAILQ_FOREACH*/
#define	SIMPLEQ_FOREACH(var, head, field)		\
    for((var) = SIMPLEQ_FIRST(head);			\
	(var) != SIMPLEQ_END(head);			\
	(var) = SIMPLEQ_NEXT(var, field))
#define	SIMPLEQ_INIT			STAILQ_INIT
#define	SIMPLEQ_INSERT_HEAD		STAILQ_INSERT_HEAD
#define	SIMPLEQ_INSERT_TAIL		STAILQ_INSERT_TAIL
#define	SIMPLEQ_INSERT_AFTER		STAILQ_INSERT_AFTER
#define	SIMPLEQ_REMOVE_HEAD		STAILQ_REMOVE_HEAD
#endif
SIMPLEQ_HEAD(node_tinithead, node_tinit);
struct node_tinit {	/* table initializer */
	SIMPLEQ_ENTRY(node_tinit)	 entries;
	struct node_host		*host;
	char				*file;
};


/* optimizer created tables */
struct pf_opt_tbl {
	char			 pt_name[PF_TABLE_NAME_SIZE];
	int			 pt_rulecount;
	int			 pt_generated;
	struct node_tinithead	 pt_nodes;
	struct pfr_buffer	*pt_buf;
};
#define PF_OPT_TABLE_PREFIX	"__automatic_"

/* optimizer pf_rule container */
struct pf_opt_rule {
	struct pf_rule		 por_rule;
	struct pf_opt_tbl	*por_src_tbl;
	struct pf_opt_tbl	*por_dst_tbl;
	u_int64_t		 por_profile_count;
	TAILQ_ENTRY(pf_opt_rule) por_entry;
	TAILQ_ENTRY(pf_opt_rule) por_skip_entry[PF_SKIP_COUNT];
};

TAILQ_HEAD(pf_opt_queue, pf_opt_rule);

int	pfctl_rules(int, char *, int, int, char *, struct pfr_buffer *);
int	pfctl_optimize_ruleset(struct pfctl *, struct pf_ruleset *);

int	pfctl_add_rule(struct pfctl *, struct pf_rule *, const char *);
int	pfctl_add_altq(struct pfctl *, struct pf_altq *);
int	pfctl_add_pool(struct pfctl *, struct pf_pool *, sa_family_t);
void	pfctl_move_pool(struct pf_pool *, struct pf_pool *);
void	pfctl_clear_pool(struct pf_pool *);

int	pfctl_set_timeout(struct pfctl *, const char *, int, int);
int	pfctl_set_optimization(struct pfctl *, const char *);
int	pfctl_set_limit(struct pfctl *, const char *, unsigned int);
int	pfctl_set_logif(struct pfctl *, char *);
int	pfctl_set_hostid(struct pfctl *, u_int32_t);
int	pfctl_set_debug(struct pfctl *, char *);
int	pfctl_set_interface_flags(struct pfctl *, char *, int, int);

int	parse_config(char *, struct pfctl *);
int	parse_flags(char *);
int	pfctl_load_anchors(int, struct pfctl *, struct pfr_buffer *);

void	print_pool(struct pf_pool *, u_int16_t, u_int16_t, sa_family_t, int);
void	print_src_node(struct pf_src_node *, int);
void	print_rule(struct pf_rule *, const char *, int, int);
void	print_tabledef(const char *, int, int, struct node_tinithead *);
void	print_status(struct pf_status *, int);
void	print_running(struct pf_status *);

int	eval_pfaltq(struct pfctl *, struct pf_altq *, struct node_queue_bw *,
	    struct node_queue_opt *);
int	eval_pfqueue(struct pfctl *, struct pf_altq *, struct node_queue_bw *,
	    struct node_queue_opt *);

void	 print_altq(const struct pf_altq *, unsigned, struct node_queue_bw *,
	    struct node_queue_opt *);
void	 print_queue(const struct pf_altq *, unsigned, struct node_queue_bw *,
	    int, struct node_queue_opt *);

int	pfctl_define_table(char *, int, int, const char *, struct pfr_buffer *,
	    u_int32_t);

void		 pfctl_clear_fingerprints(int, int);
int		 pfctl_file_fingerprints(int, int, const char *);
pf_osfp_t	 pfctl_get_fingerprint(const char *);
int		 pfctl_load_fingerprints(int, int);
char		*pfctl_lookup_fingerprint(pf_osfp_t, char *, size_t);
void		 pfctl_show_fingerprints(int);


struct icmptypeent {
	const char *name;
	u_int8_t type;
};

struct icmpcodeent {
	const char *name;
	u_int8_t type;
	u_int8_t code;
};

const struct icmptypeent *geticmptypebynumber(u_int8_t, u_int8_t);
const struct icmptypeent *geticmptypebyname(char *, u_int8_t);
const struct icmpcodeent *geticmpcodebynumber(u_int8_t, u_int8_t, u_int8_t);
const struct icmpcodeent *geticmpcodebyname(u_long, char *, u_int8_t);

struct pf_timeout {
	const char	*name;
	int		 timeout;
};

#define PFCTL_FLAG_FILTER	0x02
#define PFCTL_FLAG_NAT		0x04
#define PFCTL_FLAG_OPTION	0x08
#define PFCTL_FLAG_ALTQ		0x10
#define PFCTL_FLAG_TABLE	0x20

extern const struct pf_timeout pf_timeouts[];

void			 set_ipmask(struct node_host *, u_int8_t);
int			 check_netmask(struct node_host *, sa_family_t);
int			 unmask(struct pf_addr *, sa_family_t);
void			 ifa_load(void);
int			 get_query_socket(void);
struct node_host	*ifa_exists(char *);
struct node_host	*ifa_grouplookup(char *ifa_name, int flags);
struct node_host	*ifa_lookup(char *, int);
struct node_host	*host(const char *);

int			 append_addr(struct pfr_buffer *, char *, int);
int			 append_addr_host(struct pfr_buffer *,
			    struct node_host *, int, int);

#endif /* _PFCTL_PARSER_H_ */
