/*	$OpenBSD: pfctl_parser.h,v 1.121 2024/11/12 04:14:51 dlg Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2013 Henning Brauer <henning@openbsd.org>
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
 */

#ifndef _PFCTL_PARSER_H_
#define _PFCTL_PARSER_H_

#define PF_OSFP_FILE		"/etc/pf.os"

#define PF_OPT_DISABLE		0x00001
#define PF_OPT_ENABLE		0x00002
#define PF_OPT_VERBOSE		0x00004
#define PF_OPT_NOACTION		0x00008
#define PF_OPT_QUIET		0x00010
#define PF_OPT_CLRRULECTRS	0x00020
#define PF_OPT_USEDNS		0x00040
#define PF_OPT_VERBOSE2		0x00080
#define PF_OPT_DUMMYACTION	0x00100
#define PF_OPT_DEBUG		0x00200
#define PF_OPT_SHOWALL		0x00400
#define PF_OPT_OPTIMIZE		0x00800
#define PF_OPT_NODNS		0x01000
#define PF_OPT_RECURSE		0x04000
#define PF_OPT_PORTNAMES	0x08000
#define PF_OPT_IGNFAIL		0x10000
#define PF_OPT_CALLSHOW		0x20000

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
	int asd;			/* anchor stack depth */
	int bn;				/* brace number */
	int brace;
	int tdirty;			/* kernel dirty */
#define PFCTL_ANCHOR_STACK_DEPTH 64
	struct pf_anchor *astack[PFCTL_ANCHOR_STACK_DEPTH];
	struct pfioc_queue *pqueue;
	struct pfr_buffer *trans;
	struct pf_anchor *anchor, *alast;
	struct pfr_ktablehead pfr_ktlast;
	const char *ruleset;

	/* 'set foo' options */
	u_int32_t	 timeout[PFTM_MAX];
	u_int32_t	 limit[PF_LIMIT_MAX];
	u_int32_t	 debug;
	u_int32_t	 hostid;
	u_int32_t	 reassemble;
	u_int8_t	 syncookies;
	u_int8_t	 syncookieswat[2];	/* lowat, hiwat */
	char		*ifname;

	u_int8_t	 timeout_set[PFTM_MAX];
	u_int8_t	 limit_set[PF_LIMIT_MAX];
	u_int8_t	 debug_set;
	u_int8_t	 hostid_set;
	u_int8_t	 ifname_set;
	u_int8_t	 reass_set;
	u_int8_t	 syncookies_set;
	u_int8_t	 syncookieswat_set;
};

struct node_if {
	char			 ifname[IFNAMSIZ];
	u_int8_t		 not;
	u_int8_t		 dynamic; /* antispoof */
	u_int8_t		 use_rdomain;
	u_int			 ifa_flags;
	int			 rdomain;
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
	u_int16_t		 weight;	/* load balancing weight */
	char			*ifname;
	u_int			 ifa_flags;
	struct node_host	*next;
	struct node_host	*tail;
};
void	freehostlist(struct node_host *);

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

struct node_queue_opt {
	int			 qtype;
	union {
		struct priq_opts	priq_opts;
		struct node_hfsc_opts	hfsc_opts;
	}			 data;
};

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
	u_int32_t		 pt_flags;
	u_int32_t		 pt_refcnt;
	struct node_tinithead	 pt_nodes;
	struct pfr_buffer	*pt_buf;
};

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

extern TAILQ_HEAD(pf_qihead, pfctl_qsitem) qspecs, rootqs;
struct pfctl_qsitem {
	TAILQ_ENTRY(pfctl_qsitem)	 entries;
	struct pf_queuespec		 qs;
	struct pf_qihead		 children;
	int				 matches;
};

struct pfctl_watermarks {
	u_int32_t	hi;
	u_int32_t	lo;
};

struct pfr_uktable;

void		 copy_satopfaddr(struct pf_addr *, struct sockaddr *);

int	pfctl_rules(int, char *, int, int, char *, struct pfr_buffer *);
int	pfctl_optimize_ruleset(struct pfctl *, struct pf_ruleset *);
int     pf_opt_create_table(struct pfctl *, struct pf_opt_tbl *);
int     add_opt_table(struct pfctl *, struct pf_opt_tbl **, sa_family_t,
            struct pf_rule_addr *, char *);

void	pfctl_add_rule(struct pfctl *, struct pf_rule *);

int	pfctl_set_timeout(struct pfctl *, const char *, int, int);
int	pfctl_set_reassembly(struct pfctl *, int, int);
int	pfctl_set_syncookies(struct pfctl *, u_int8_t,
	    struct pfctl_watermarks *);
int	pfctl_set_optimization(struct pfctl *, const char *);
int	pfctl_set_limit(struct pfctl *, const char *, unsigned int);
int	pfctl_set_logif(struct pfctl *, char *);
void	pfctl_set_hostid(struct pfctl *, u_int32_t);
int	pfctl_set_debug(struct pfctl *, char *);
int	pfctl_set_interface_flags(struct pfctl *, char *, int, int);

int	parse_config(char *, struct pfctl *);
int	parse_flags(char *);
int	pfctl_load_anchors(int, struct pfctl *);

int	pfctl_load_queues(struct pfctl *);
int	pfctl_add_queue(struct pfctl *, struct pf_queuespec *);
struct pfctl_qsitem *	pfctl_find_queue(char *, struct pf_qihead *);

void	print_pool(struct pf_pool *, u_int16_t, u_int16_t, sa_family_t, int, int);
void	print_src_node(struct pf_src_node *, int);
void	print_rule(struct pf_rule *, const char *, int);
void	print_tabledef(const char *, int, int, struct node_tinithead *);
void	print_status(struct pf_status *, struct pfctl_watermarks *, int);
void	print_queuespec(struct pf_queuespec *);

int	pfctl_define_table(char *, int, int, const char *, struct pfr_buffer *,
	    u_int32_t, struct pfr_uktable *);
void	pfctl_expand_label_nr(struct pf_rule *, unsigned int);

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

int			  string_to_loglevel(const char *);
const char		 *loglevel_to_string(int);

struct pf_timeout {
	const char	*name;
	int		 timeout;
};

extern const struct pf_timeout pf_timeouts[];

void			 set_ipmask(struct node_host *, int);
int			 check_netmask(struct node_host *, sa_family_t);
int			 unmask(struct pf_addr *);
struct node_host	*gen_dynnode(struct node_host *, sa_family_t);
void			 ifa_load(void);
unsigned int		 ifa_nametoindex(const char *);
char			*ifa_indextoname(unsigned int, char *);
struct node_host	*ifa_exists(const char *);
struct node_host	*ifa_lookup(const char *, int);
struct node_host	*host(const char *, int);

int			 append_addr(struct pfr_buffer *, char *, int, int);
int			 append_addr_host(struct pfr_buffer *,
			    struct node_host *, int, int);
int			 pfr_ktable_compare(struct pfr_ktable *,
			    struct pfr_ktable *);
RB_PROTOTYPE(pfr_ktablehead, pfr_ktable, pfrkt_tree, pfr_ktable_compare);

#endif /* _PFCTL_PARSER_H_ */
