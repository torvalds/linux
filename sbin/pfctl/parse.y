/*	$OpenBSD: parse.y,v 1.554 2008/10/17 12:59:53 henning Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
 * Copyright (c) 2002,2003 Henning Brauer. All rights reserved.
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
%{
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define PFIOC_USE_LATEST

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <net/pfvar.h>
#include <arpa/inet.h>
#include <net/altq/altq.h>
#include <net/altq/altq_cbq.h>
#include <net/altq/altq_codel.h>
#include <net/altq/altq_priq.h>
#include <net/altq/altq_hfsc.h>
#include <net/altq/altq_fairq.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <err.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <md5.h>

#include "pfctl_parser.h"
#include "pfctl.h"

static struct pfctl	*pf = NULL;
static int		 debug = 0;
static int		 rulestate = 0;
static u_int16_t	 returnicmpdefault =
			    (ICMP_UNREACH << 8) | ICMP_UNREACH_PORT;
static u_int16_t	 returnicmp6default =
			    (ICMP6_DST_UNREACH << 8) | ICMP6_DST_UNREACH_NOPORT;
static int		 blockpolicy = PFRULE_DROP;
static int		 failpolicy = PFRULE_DROP;
static int		 require_order = 1;
static int		 default_statelock;

static TAILQ_HEAD(files, file)	 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	int			 lineno;
	int			 errors;
} *file;
struct file	*pushfile(const char *, int);
int		 popfile(void);
int		 check_file_secrecy(int, const char *);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...);
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 lgetc(int);
int		 lungetc(int);
int		 findeol(void);

static TAILQ_HEAD(symhead, sym)	 symhead = TAILQ_HEAD_INITIALIZER(symhead);
struct sym {
	TAILQ_ENTRY(sym)	 entry;
	int			 used;
	int			 persist;
	char			*nam;
	char			*val;
};
int		 symset(const char *, const char *, int);
char		*symget(const char *);

int		 atoul(char *, u_long *);

enum {
	PFCTL_STATE_NONE,
	PFCTL_STATE_OPTION,
	PFCTL_STATE_SCRUB,
	PFCTL_STATE_QUEUE,
	PFCTL_STATE_NAT,
	PFCTL_STATE_FILTER
};

struct node_proto {
	u_int8_t		 proto;
	struct node_proto	*next;
	struct node_proto	*tail;
};

struct node_port {
	u_int16_t		 port[2];
	u_int8_t		 op;
	struct node_port	*next;
	struct node_port	*tail;
};

struct node_uid {
	uid_t			 uid[2];
	u_int8_t		 op;
	struct node_uid		*next;
	struct node_uid		*tail;
};

struct node_gid {
	gid_t			 gid[2];
	u_int8_t		 op;
	struct node_gid		*next;
	struct node_gid		*tail;
};

struct node_icmp {
	u_int8_t		 code;
	u_int8_t		 type;
	u_int8_t		 proto;
	struct node_icmp	*next;
	struct node_icmp	*tail;
};

enum	{ PF_STATE_OPT_MAX, PF_STATE_OPT_NOSYNC, PF_STATE_OPT_SRCTRACK,
	    PF_STATE_OPT_MAX_SRC_STATES, PF_STATE_OPT_MAX_SRC_CONN,
	    PF_STATE_OPT_MAX_SRC_CONN_RATE, PF_STATE_OPT_MAX_SRC_NODES,
	    PF_STATE_OPT_OVERLOAD, PF_STATE_OPT_STATELOCK,
	    PF_STATE_OPT_TIMEOUT, PF_STATE_OPT_SLOPPY, };

enum	{ PF_SRCTRACK_NONE, PF_SRCTRACK, PF_SRCTRACK_GLOBAL, PF_SRCTRACK_RULE };

struct node_state_opt {
	int			 type;
	union {
		u_int32_t	 max_states;
		u_int32_t	 max_src_states;
		u_int32_t	 max_src_conn;
		struct {
			u_int32_t	limit;
			u_int32_t	seconds;
		}		 max_src_conn_rate;
		struct {
			u_int8_t	flush;
			char		tblname[PF_TABLE_NAME_SIZE];
		}		 overload;
		u_int32_t	 max_src_nodes;
		u_int8_t	 src_track;
		u_int32_t	 statelock;
		struct {
			int		number;
			u_int32_t	seconds;
		}		 timeout;
	}			 data;
	struct node_state_opt	*next;
	struct node_state_opt	*tail;
};

struct peer {
	struct node_host	*host;
	struct node_port	*port;
};

static struct node_queue {
	char			 queue[PF_QNAME_SIZE];
	char			 parent[PF_QNAME_SIZE];
	char			 ifname[IFNAMSIZ];
	int			 scheduler;
	struct node_queue	*next;
	struct node_queue	*tail;
}	*queues = NULL;

struct node_qassign {
	char		*qname;
	char		*pqname;
};

static struct filter_opts {
	int			 marker;
#define FOM_FLAGS	0x01
#define FOM_ICMP	0x02
#define FOM_TOS		0x04
#define FOM_KEEP	0x08
#define FOM_SRCTRACK	0x10
#define FOM_SETPRIO	0x0400
#define FOM_PRIO	0x2000
	struct node_uid		*uid;
	struct node_gid		*gid;
	struct {
		u_int8_t	 b1;
		u_int8_t	 b2;
		u_int16_t	 w;
		u_int16_t	 w2;
	} flags;
	struct node_icmp	*icmpspec;
	u_int32_t		 tos;
	u_int32_t		 prob;
	struct {
		int			 action;
		struct node_state_opt	*options;
	} keep;
	int			 fragment;
	int			 allowopts;
	char			*label;
	struct node_qassign	 queues;
	char			*tag;
	char			*match_tag;
	u_int8_t		 match_tag_not;
	u_int			 rtableid;
	u_int8_t		 prio;
	u_int8_t		 set_prio[2];
	struct {
		struct node_host	*addr;
		u_int16_t		port;
	}			 divert;
} filter_opts;

static struct antispoof_opts {
	char			*label;
	u_int			 rtableid;
} antispoof_opts;

static struct scrub_opts {
	int			 marker;
#define SOM_MINTTL	0x01
#define SOM_MAXMSS	0x02
#define SOM_FRAGCACHE	0x04
#define SOM_SETTOS	0x08
	int			 nodf;
	int			 minttl;
	int			 maxmss;
	int			 settos;
	int			 fragcache;
	int			 randomid;
	int			 reassemble_tcp;
	char			*match_tag;
	u_int8_t		 match_tag_not;
	u_int			 rtableid;
} scrub_opts;

static struct queue_opts {
	int			marker;
#define QOM_BWSPEC	0x01
#define QOM_SCHEDULER	0x02
#define QOM_PRIORITY	0x04
#define QOM_TBRSIZE	0x08
#define QOM_QLIMIT	0x10
	struct node_queue_bw	queue_bwspec;
	struct node_queue_opt	scheduler;
	int			priority;
	unsigned int		tbrsize;
	int			qlimit;
} queue_opts;

static struct table_opts {
	int			flags;
	int			init_addr;
	struct node_tinithead	init_nodes;
} table_opts;

static struct pool_opts {
	int			 marker;
#define POM_TYPE		0x01
#define POM_STICKYADDRESS	0x02
	u_int8_t		 opts;
	int			 type;
	int			 staticport;
	struct pf_poolhashkey	*key;

} pool_opts;

static struct codel_opts	 codel_opts;
static struct node_hfsc_opts	 hfsc_opts;
static struct node_fairq_opts	 fairq_opts;
static struct node_state_opt	*keep_state_defaults = NULL;

int		 disallow_table(struct node_host *, const char *);
int		 disallow_urpf_failed(struct node_host *, const char *);
int		 disallow_alias(struct node_host *, const char *);
int		 rule_consistent(struct pf_rule *, int);
int		 filter_consistent(struct pf_rule *, int);
int		 nat_consistent(struct pf_rule *);
int		 rdr_consistent(struct pf_rule *);
int		 process_tabledef(char *, struct table_opts *);
void		 expand_label_str(char *, size_t, const char *, const char *);
void		 expand_label_if(const char *, char *, size_t, const char *);
void		 expand_label_addr(const char *, char *, size_t, u_int8_t,
		    struct node_host *);
void		 expand_label_port(const char *, char *, size_t,
		    struct node_port *);
void		 expand_label_proto(const char *, char *, size_t, u_int8_t);
void		 expand_label_nr(const char *, char *, size_t);
void		 expand_label(char *, size_t, const char *, u_int8_t,
		    struct node_host *, struct node_port *, struct node_host *,
		    struct node_port *, u_int8_t);
void		 expand_rule(struct pf_rule *, struct node_if *,
		    struct node_host *, struct node_proto *, struct node_os *,
		    struct node_host *, struct node_port *, struct node_host *,
		    struct node_port *, struct node_uid *, struct node_gid *,
		    struct node_icmp *, const char *);
int		 expand_altq(struct pf_altq *, struct node_if *,
		    struct node_queue *, struct node_queue_bw bwspec,
		    struct node_queue_opt *);
int		 expand_queue(struct pf_altq *, struct node_if *,
		    struct node_queue *, struct node_queue_bw,
		    struct node_queue_opt *);
int		 expand_skip_interface(struct node_if *);

int	 check_rulestate(int);
int	 getservice(char *);
int	 rule_label(struct pf_rule *, char *);
int	 rt_tableid_max(void);

void	 mv_rules(struct pf_ruleset *, struct pf_ruleset *);
void	 decide_address_family(struct node_host *, sa_family_t *);
void	 remove_invalid_hosts(struct node_host **, sa_family_t *);
int	 invalid_redirect(struct node_host *, sa_family_t);
u_int16_t parseicmpspec(char *, sa_family_t);
int	 kw_casecmp(const void *, const void *);
int	 map_tos(char *string, int *);

static TAILQ_HEAD(loadanchorshead, loadanchors)
    loadanchorshead = TAILQ_HEAD_INITIALIZER(loadanchorshead);

struct loadanchors {
	TAILQ_ENTRY(loadanchors)	 entries;
	char				*anchorname;
	char				*filename;
};

typedef struct {
	union {
		int64_t			 number;
		double			 probability;
		int			 i;
		char			*string;
		u_int			 rtableid;
		struct {
			u_int8_t	 b1;
			u_int8_t	 b2;
			u_int16_t	 w;
			u_int16_t	 w2;
		}			 b;
		struct range {
			int		 a;
			int		 b;
			int		 t;
		}			 range;
		struct node_if		*interface;
		struct node_proto	*proto;
		struct node_icmp	*icmp;
		struct node_host	*host;
		struct node_os		*os;
		struct node_port	*port;
		struct node_uid		*uid;
		struct node_gid		*gid;
		struct node_state_opt	*state_opt;
		struct peer		 peer;
		struct {
			struct peer	 src, dst;
			struct node_os	*src_os;
		}			 fromto;
		struct {
			struct node_host	*host;
			u_int8_t		 rt;
			u_int8_t		 pool_opts;
			sa_family_t		 af;
			struct pf_poolhashkey	*key;
		}			 route;
		struct redirection {
			struct node_host	*host;
			struct range		 rport;
		}			*redirection;
		struct {
			int			 action;
			struct node_state_opt	*options;
		}			 keep_state;
		struct {
			u_int8_t	 log;
			u_int8_t	 logif;
			u_int8_t	 quick;
		}			 logquick;
		struct {
			int		 neg;
			char		*name;
		}			 tagged;
		struct pf_poolhashkey	*hashkey;
		struct node_queue	*queue;
		struct node_queue_opt	 queue_options;
		struct node_queue_bw	 queue_bwspec;
		struct node_qassign	 qassign;
		struct filter_opts	 filter_opts;
		struct antispoof_opts	 antispoof_opts;
		struct queue_opts	 queue_opts;
		struct scrub_opts	 scrub_opts;
		struct table_opts	 table_opts;
		struct pool_opts	 pool_opts;
		struct node_hfsc_opts	 hfsc_opts;
		struct node_fairq_opts	 fairq_opts;
		struct codel_opts	 codel_opts;
	} v;
	int lineno;
} YYSTYPE;

#define PPORT_RANGE	1
#define PPORT_STAR	2
int	parseport(char *, struct range *r, int);

#define DYNIF_MULTIADDR(addr) ((addr).type == PF_ADDR_DYNIFTL && \
	(!((addr).iflags & PFI_AFLAG_NOALIAS) ||		 \
	!isdigit((addr).v.ifname[strlen((addr).v.ifname)-1])))

%}

%token	PASS BLOCK SCRUB RETURN IN OS OUT LOG QUICK ON FROM TO FLAGS
%token	RETURNRST RETURNICMP RETURNICMP6 PROTO INET INET6 ALL ANY ICMPTYPE
%token	ICMP6TYPE CODE KEEP MODULATE STATE PORT RDR NAT BINAT ARROW NODF
%token	MINTTL ERROR ALLOWOPTS FASTROUTE FILENAME ROUTETO DUPTO REPLYTO NO LABEL
%token	NOROUTE URPFFAILED FRAGMENT USER GROUP MAXMSS MAXIMUM TTL TOS DROP TABLE
%token	REASSEMBLE FRAGDROP FRAGCROP ANCHOR NATANCHOR RDRANCHOR BINATANCHOR
%token	SET OPTIMIZATION TIMEOUT LIMIT LOGINTERFACE BLOCKPOLICY FAILPOLICY
%token	RANDOMID REQUIREORDER SYNPROXY FINGERPRINTS NOSYNC DEBUG SKIP HOSTID
%token	ANTISPOOF FOR INCLUDE
%token	BITMASK RANDOM SOURCEHASH ROUNDROBIN STATICPORT PROBABILITY
%token	ALTQ CBQ CODEL PRIQ HFSC FAIRQ BANDWIDTH TBRSIZE LINKSHARE REALTIME
%token	UPPERLIMIT QUEUE PRIORITY QLIMIT HOGS BUCKETS RTABLE TARGET INTERVAL
%token	LOAD RULESET_OPTIMIZATION PRIO
%token	STICKYADDRESS MAXSRCSTATES MAXSRCNODES SOURCETRACK GLOBAL RULE
%token	MAXSRCCONN MAXSRCCONNRATE OVERLOAD FLUSH SLOPPY
%token	TAGGED TAG IFBOUND FLOATING STATEPOLICY STATEDEFAULTS ROUTE SETTOS
%token	DIVERTTO DIVERTREPLY
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%token	<v.i>			PORTBINARY
%type	<v.interface>		interface if_list if_item_not if_item
%type	<v.number>		number icmptype icmp6type uid gid
%type	<v.number>		tos not yesno
%type	<v.probability>		probability
%type	<v.i>			no dir af fragcache optimizer
%type	<v.i>			sourcetrack flush unaryop statelock
%type	<v.b>			action nataction natpasslog scrubaction
%type	<v.b>			flags flag blockspec prio
%type	<v.range>		portplain portstar portrange
%type	<v.hashkey>		hashkey
%type	<v.proto>		proto proto_list proto_item
%type	<v.number>		protoval
%type	<v.icmp>		icmpspec
%type	<v.icmp>		icmp_list icmp_item
%type	<v.icmp>		icmp6_list icmp6_item
%type	<v.number>		reticmpspec reticmp6spec
%type	<v.fromto>		fromto
%type	<v.peer>		ipportspec from to
%type	<v.host>		ipspec toipspec xhost host dynaddr host_list
%type	<v.host>		redir_host_list redirspec
%type	<v.host>		route_host route_host_list routespec
%type	<v.os>			os xos os_list
%type	<v.port>		portspec port_list port_item
%type	<v.uid>			uids uid_list uid_item
%type	<v.gid>			gids gid_list gid_item
%type	<v.route>		route
%type	<v.redirection>		redirection redirpool
%type	<v.string>		label stringall tag anchorname
%type	<v.string>		string varstring numberstring
%type	<v.keep_state>		keep
%type	<v.state_opt>		state_opt_spec state_opt_list state_opt_item
%type	<v.logquick>		logquick quick log logopts logopt
%type	<v.interface>		antispoof_ifspc antispoof_iflst antispoof_if
%type	<v.qassign>		qname
%type	<v.queue>		qassign qassign_list qassign_item
%type	<v.queue_options>	scheduler
%type	<v.number>		cbqflags_list cbqflags_item
%type	<v.number>		priqflags_list priqflags_item
%type	<v.hfsc_opts>		hfscopts_list hfscopts_item hfsc_opts
%type	<v.fairq_opts>		fairqopts_list fairqopts_item fairq_opts
%type	<v.codel_opts>		codelopts_list codelopts_item codel_opts
%type	<v.queue_bwspec>	bandwidth
%type	<v.filter_opts>		filter_opts filter_opt filter_opts_l
%type	<v.filter_opts>		filter_sets filter_set filter_sets_l
%type	<v.antispoof_opts>	antispoof_opts antispoof_opt antispoof_opts_l
%type	<v.queue_opts>		queue_opts queue_opt queue_opts_l
%type	<v.scrub_opts>		scrub_opts scrub_opt scrub_opts_l
%type	<v.table_opts>		table_opts table_opt table_opts_l
%type	<v.pool_opts>		pool_opts pool_opt pool_opts_l
%type	<v.tagged>		tagged
%type	<v.rtableid>		rtable
%%

ruleset		: /* empty */
		| ruleset include '\n'
		| ruleset '\n'
		| ruleset option '\n'
		| ruleset scrubrule '\n'
		| ruleset natrule '\n'
		| ruleset binatrule '\n'
		| ruleset pfrule '\n'
		| ruleset anchorrule '\n'
		| ruleset loadrule '\n'
		| ruleset altqif '\n'
		| ruleset queuespec '\n'
		| ruleset varset '\n'
		| ruleset antispoof '\n'
		| ruleset tabledef '\n'
		| '{' fakeanchor '}' '\n';
		| ruleset error '\n'		{ file->errors++; }
		;

include		: INCLUDE STRING		{
			struct file	*nfile;

			if ((nfile = pushfile($2, 0)) == NULL) {
				yyerror("failed to include file %s", $2);
				free($2);
				YYERROR;
			}
			free($2);

			file = nfile;
			lungetc('\n');
		}
		;

/*
 * apply to previouslys specified rule: must be careful to note
 * what that is: pf or nat or binat or rdr
 */
fakeanchor	: fakeanchor '\n'
		| fakeanchor anchorrule '\n'
		| fakeanchor binatrule '\n'
		| fakeanchor natrule '\n'
		| fakeanchor pfrule '\n'
		| fakeanchor error '\n'
		;

optimizer	: string	{
			if (!strcmp($1, "none"))
				$$ = 0;
			else if (!strcmp($1, "basic"))
				$$ = PF_OPTIMIZE_BASIC;
			else if (!strcmp($1, "profile"))
				$$ = PF_OPTIMIZE_BASIC | PF_OPTIMIZE_PROFILE;
			else {
				yyerror("unknown ruleset-optimization %s", $1);
				YYERROR;
			}
		}
		;

option		: SET OPTIMIZATION STRING		{
			if (check_rulestate(PFCTL_STATE_OPTION)) {
				free($3);
				YYERROR;
			}
			if (pfctl_set_optimization(pf, $3) != 0) {
				yyerror("unknown optimization %s", $3);
				free($3);
				YYERROR;
			}
			free($3);
		}
		| SET RULESET_OPTIMIZATION optimizer {
			if (!(pf->opts & PF_OPT_OPTIMIZE)) {
				pf->opts |= PF_OPT_OPTIMIZE;
				pf->optimize = $3;
			}
		}
		| SET TIMEOUT timeout_spec
		| SET TIMEOUT '{' optnl timeout_list '}'
		| SET LIMIT limit_spec
		| SET LIMIT '{' optnl limit_list '}'
		| SET LOGINTERFACE stringall		{
			if (check_rulestate(PFCTL_STATE_OPTION)) {
				free($3);
				YYERROR;
			}
			if (pfctl_set_logif(pf, $3) != 0) {
				yyerror("error setting loginterface %s", $3);
				free($3);
				YYERROR;
			}
			free($3);
		}
		| SET HOSTID number {
			if ($3 == 0 || $3 > UINT_MAX) {
				yyerror("hostid must be non-zero");
				YYERROR;
			}
			if (pfctl_set_hostid(pf, $3) != 0) {
				yyerror("error setting hostid %08x", $3);
				YYERROR;
			}
		}
		| SET BLOCKPOLICY DROP	{
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set block-policy drop\n");
			if (check_rulestate(PFCTL_STATE_OPTION))
				YYERROR;
			blockpolicy = PFRULE_DROP;
		}
		| SET BLOCKPOLICY RETURN {
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set block-policy return\n");
			if (check_rulestate(PFCTL_STATE_OPTION))
				YYERROR;
			blockpolicy = PFRULE_RETURN;
		}
		| SET FAILPOLICY DROP	{
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set fail-policy drop\n");
			if (check_rulestate(PFCTL_STATE_OPTION))
				YYERROR;
			failpolicy = PFRULE_DROP;
		}
		| SET FAILPOLICY RETURN {
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set fail-policy return\n");
			if (check_rulestate(PFCTL_STATE_OPTION))
				YYERROR;
			failpolicy = PFRULE_RETURN;
		}
		| SET REQUIREORDER yesno {
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set require-order %s\n",
				    $3 == 1 ? "yes" : "no");
			require_order = $3;
		}
		| SET FINGERPRINTS STRING {
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set fingerprints \"%s\"\n", $3);
			if (check_rulestate(PFCTL_STATE_OPTION)) {
				free($3);
				YYERROR;
			}
			if (!pf->anchor->name[0]) {
				if (pfctl_file_fingerprints(pf->dev,
				    pf->opts, $3)) {
					yyerror("error loading "
					    "fingerprints %s", $3);
					free($3);
					YYERROR;
				}
			}
			free($3);
		}
		| SET STATEPOLICY statelock {
			if (pf->opts & PF_OPT_VERBOSE)
				switch ($3) {
				case 0:
					printf("set state-policy floating\n");
					break;
				case PFRULE_IFBOUND:
					printf("set state-policy if-bound\n");
					break;
				}
			default_statelock = $3;
		}
		| SET DEBUG STRING {
			if (check_rulestate(PFCTL_STATE_OPTION)) {
				free($3);
				YYERROR;
			}
			if (pfctl_set_debug(pf, $3) != 0) {
				yyerror("error setting debuglevel %s", $3);
				free($3);
				YYERROR;
			}
			free($3);
		}
		| SET SKIP interface {
			if (expand_skip_interface($3) != 0) {
				yyerror("error setting skip interface(s)");
				YYERROR;
			}
		}
		| SET STATEDEFAULTS state_opt_list {
			if (keep_state_defaults != NULL) {
				yyerror("cannot redefine state-defaults");
				YYERROR;
			}
			keep_state_defaults = $3;
		}
		;

stringall	: STRING	{ $$ = $1; }
		| ALL		{
			if (($$ = strdup("all")) == NULL) {
				err(1, "stringall: strdup");
			}
		}
		;

string		: STRING string				{
			if (asprintf(&$$, "%s %s", $1, $2) == -1)
				err(1, "string: asprintf");
			free($1);
			free($2);
		}
		| STRING
		;

varstring	: numberstring varstring 		{
			if (asprintf(&$$, "%s %s", $1, $2) == -1)
				err(1, "string: asprintf");
			free($1);
			free($2);
		}
		| numberstring
		;

numberstring	: NUMBER				{
			char	*s;
			if (asprintf(&s, "%lld", (long long)$1) == -1) {
				yyerror("string: asprintf");
				YYERROR;
			}
			$$ = s;
		}
		| STRING
		;

varset		: STRING '=' varstring	{
			char *s = $1;
			if (pf->opts & PF_OPT_VERBOSE)
				printf("%s = \"%s\"\n", $1, $3);
			while (*s++) {
				if (isspace((unsigned char)*s)) {
					yyerror("macro name cannot contain "
					   "whitespace");
					YYERROR;
				}
			}
			if (symset($1, $3, 0) == -1)
				err(1, "cannot store variable %s", $1);
			free($1);
			free($3);
		}
		;

anchorname	: STRING			{ $$ = $1; }
		| /* empty */			{ $$ = NULL; }
		;

pfa_anchorlist	: /* empty */
		| pfa_anchorlist '\n'
		| pfa_anchorlist pfrule '\n'
		| pfa_anchorlist anchorrule '\n'
		;

pfa_anchor	: '{'
		{
			char ta[PF_ANCHOR_NAME_SIZE];
			struct pf_ruleset *rs;

			/* steping into a brace anchor */
			pf->asd++;
			pf->bn++;
			pf->brace = 1;

			/* create a holding ruleset in the root */
			snprintf(ta, PF_ANCHOR_NAME_SIZE, "_%d", pf->bn);
			rs = pf_find_or_create_ruleset(ta);
			if (rs == NULL)
				err(1, "pfa_anchor: pf_find_or_create_ruleset");
			pf->astack[pf->asd] = rs->anchor;
			pf->anchor = rs->anchor;
		} '\n' pfa_anchorlist '}'
		{
			pf->alast = pf->anchor;
			pf->asd--;
			pf->anchor = pf->astack[pf->asd];
		}
		| /* empty */
		;

anchorrule	: ANCHOR anchorname dir quick interface af proto fromto
		    filter_opts pfa_anchor
		{
			struct pf_rule	r;
			struct node_proto	*proto;

			if (check_rulestate(PFCTL_STATE_FILTER)) {
				if ($2)
					free($2);
				YYERROR;
			}

			if ($2 && ($2[0] == '_' || strstr($2, "/_") != NULL)) {
				free($2);
				yyerror("anchor names beginning with '_' "
				    "are reserved for internal use");
				YYERROR;
			}

			memset(&r, 0, sizeof(r));
			if (pf->astack[pf->asd + 1]) {
				/* move inline rules into relative location */
				pf_anchor_setup(&r,
				    &pf->astack[pf->asd]->ruleset,
				    $2 ? $2 : pf->alast->name);
		
				if (r.anchor == NULL)
					err(1, "anchorrule: unable to "
					    "create ruleset");

				if (pf->alast != r.anchor) {
					if (r.anchor->match) {
						yyerror("inline anchor '%s' "
						    "already exists",
						    r.anchor->name);
						YYERROR;
					}
					mv_rules(&pf->alast->ruleset,
					    &r.anchor->ruleset);
				}
				pf_remove_if_empty_ruleset(&pf->alast->ruleset);
				pf->alast = r.anchor;
			} else {
				if (!$2) {
					yyerror("anchors without explicit "
					    "rules must specify a name");
					YYERROR;
				}
			}
			r.direction = $3;
			r.quick = $4.quick;
			r.af = $6;
			r.prob = $9.prob;
			r.rtableid = $9.rtableid;

			if ($9.tag)
				if (strlcpy(r.tagname, $9.tag,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			if ($9.match_tag)
				if (strlcpy(r.match_tagname, $9.match_tag,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			r.match_tag_not = $9.match_tag_not;
			if (rule_label(&r, $9.label))
				YYERROR;
			free($9.label);
			r.flags = $9.flags.b1;
			r.flagset = $9.flags.b2;
			if (($9.flags.b1 & $9.flags.b2) != $9.flags.b1) {
				yyerror("flags always false");
				YYERROR;
			}
			if ($9.flags.b1 || $9.flags.b2 || $8.src_os) {
				for (proto = $7; proto != NULL &&
				    proto->proto != IPPROTO_TCP;
				    proto = proto->next)
					;	/* nothing */
				if (proto == NULL && $7 != NULL) {
					if ($9.flags.b1 || $9.flags.b2)
						yyerror(
						    "flags only apply to tcp");
					if ($8.src_os)
						yyerror(
						    "OS fingerprinting only "
						    "applies to tcp");
					YYERROR;
				}
			}

			r.tos = $9.tos;

			if ($9.keep.action) {
				yyerror("cannot specify state handling "
				    "on anchors");
				YYERROR;
			}

			if ($9.match_tag)
				if (strlcpy(r.match_tagname, $9.match_tag,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			r.match_tag_not = $9.match_tag_not;
			if ($9.marker & FOM_PRIO) {
				if ($9.prio == 0)
					r.prio = PF_PRIO_ZERO;
				else
					r.prio = $9.prio;
			}
			if ($9.marker & FOM_SETPRIO) {
				r.set_prio[0] = $9.set_prio[0];
				r.set_prio[1] = $9.set_prio[1];
				r.scrub_flags |= PFSTATE_SETPRIO;
			}

			decide_address_family($8.src.host, &r.af);
			decide_address_family($8.dst.host, &r.af);

			expand_rule(&r, $5, NULL, $7, $8.src_os,
			    $8.src.host, $8.src.port, $8.dst.host, $8.dst.port,
			    $9.uid, $9.gid, $9.icmpspec,
			    pf->astack[pf->asd + 1] ? pf->alast->name : $2);
			free($2);
			pf->astack[pf->asd + 1] = NULL;
		}
		| NATANCHOR string interface af proto fromto rtable {
			struct pf_rule	r;

			if (check_rulestate(PFCTL_STATE_NAT)) {
				free($2);
				YYERROR;
			}

			memset(&r, 0, sizeof(r));
			r.action = PF_NAT;
			r.af = $4;
			r.rtableid = $7;

			decide_address_family($6.src.host, &r.af);
			decide_address_family($6.dst.host, &r.af);

			expand_rule(&r, $3, NULL, $5, $6.src_os,
			    $6.src.host, $6.src.port, $6.dst.host, $6.dst.port,
			    0, 0, 0, $2);
			free($2);
		}
		| RDRANCHOR string interface af proto fromto rtable {
			struct pf_rule	r;

			if (check_rulestate(PFCTL_STATE_NAT)) {
				free($2);
				YYERROR;
			}

			memset(&r, 0, sizeof(r));
			r.action = PF_RDR;
			r.af = $4;
			r.rtableid = $7;

			decide_address_family($6.src.host, &r.af);
			decide_address_family($6.dst.host, &r.af);

			if ($6.src.port != NULL) {
				yyerror("source port parameter not supported"
				    " in rdr-anchor");
				YYERROR;
			}
			if ($6.dst.port != NULL) {
				if ($6.dst.port->next != NULL) {
					yyerror("destination port list "
					    "expansion not supported in "
					    "rdr-anchor");
					YYERROR;
				} else if ($6.dst.port->op != PF_OP_EQ) {
					yyerror("destination port operators"
					    " not supported in rdr-anchor");
					YYERROR;
				}
				r.dst.port[0] = $6.dst.port->port[0];
				r.dst.port[1] = $6.dst.port->port[1];
				r.dst.port_op = $6.dst.port->op;
			}

			expand_rule(&r, $3, NULL, $5, $6.src_os,
			    $6.src.host, $6.src.port, $6.dst.host, $6.dst.port,
			    0, 0, 0, $2);
			free($2);
		}
		| BINATANCHOR string interface af proto fromto rtable {
			struct pf_rule	r;

			if (check_rulestate(PFCTL_STATE_NAT)) {
				free($2);
				YYERROR;
			}

			memset(&r, 0, sizeof(r));
			r.action = PF_BINAT;
			r.af = $4;
			r.rtableid = $7;
			if ($5 != NULL) {
				if ($5->next != NULL) {
					yyerror("proto list expansion"
					    " not supported in binat-anchor");
					YYERROR;
				}
				r.proto = $5->proto;
				free($5);
			}

			if ($6.src.host != NULL || $6.src.port != NULL ||
			    $6.dst.host != NULL || $6.dst.port != NULL) {
				yyerror("fromto parameter not supported"
				    " in binat-anchor");
				YYERROR;
			}

			decide_address_family($6.src.host, &r.af);
			decide_address_family($6.dst.host, &r.af);

			pfctl_add_rule(pf, &r, $2);
			free($2);
		}
		;

loadrule	: LOAD ANCHOR string FROM string	{
			struct loadanchors	*loadanchor;

			if (strlen(pf->anchor->name) + 1 +
			    strlen($3) >= MAXPATHLEN) {
				yyerror("anchorname %s too long, max %u\n",
				    $3, MAXPATHLEN - 1);
				free($3);
				YYERROR;
			}
			loadanchor = calloc(1, sizeof(struct loadanchors));
			if (loadanchor == NULL)
				err(1, "loadrule: calloc");
			if ((loadanchor->anchorname = malloc(MAXPATHLEN)) ==
			    NULL)
				err(1, "loadrule: malloc");
			if (pf->anchor->name[0])
				snprintf(loadanchor->anchorname, MAXPATHLEN,
				    "%s/%s", pf->anchor->name, $3);
			else
				strlcpy(loadanchor->anchorname, $3, MAXPATHLEN);
			if ((loadanchor->filename = strdup($5)) == NULL)
				err(1, "loadrule: strdup");

			TAILQ_INSERT_TAIL(&loadanchorshead, loadanchor,
			    entries);

			free($3);
			free($5);
		};

scrubaction	: no SCRUB {
			$$.b2 = $$.w = 0;
			if ($1)
				$$.b1 = PF_NOSCRUB;
			else
				$$.b1 = PF_SCRUB;
		}
		;

scrubrule	: scrubaction dir logquick interface af proto fromto scrub_opts
		{
			struct pf_rule	r;

			if (check_rulestate(PFCTL_STATE_SCRUB))
				YYERROR;

			memset(&r, 0, sizeof(r));

			r.action = $1.b1;
			r.direction = $2;

			r.log = $3.log;
			r.logif = $3.logif;
			if ($3.quick) {
				yyerror("scrub rules do not support 'quick'");
				YYERROR;
			}

			r.af = $5;
			if ($8.nodf)
				r.rule_flag |= PFRULE_NODF;
			if ($8.randomid)
				r.rule_flag |= PFRULE_RANDOMID;
			if ($8.reassemble_tcp) {
				if (r.direction != PF_INOUT) {
					yyerror("reassemble tcp rules can not "
					    "specify direction");
					YYERROR;
				}
				r.rule_flag |= PFRULE_REASSEMBLE_TCP;
			}
			if ($8.minttl)
				r.min_ttl = $8.minttl;
			if ($8.maxmss)
				r.max_mss = $8.maxmss;
			if ($8.marker & SOM_SETTOS) {
				r.rule_flag |= PFRULE_SET_TOS;
				r.set_tos = $8.settos;
			}
			if ($8.fragcache)
				r.rule_flag |= $8.fragcache;
			if ($8.match_tag)
				if (strlcpy(r.match_tagname, $8.match_tag,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			r.match_tag_not = $8.match_tag_not;
			r.rtableid = $8.rtableid;

			expand_rule(&r, $4, NULL, $6, $7.src_os,
			    $7.src.host, $7.src.port, $7.dst.host, $7.dst.port,
			    NULL, NULL, NULL, "");
		}
		;

scrub_opts	:	{
				bzero(&scrub_opts, sizeof scrub_opts);
				scrub_opts.rtableid = -1;
			}
		    scrub_opts_l
			{ $$ = scrub_opts; }
		| /* empty */ {
			bzero(&scrub_opts, sizeof scrub_opts);
			scrub_opts.rtableid = -1;
			$$ = scrub_opts;
		}
		;

scrub_opts_l	: scrub_opts_l scrub_opt
		| scrub_opt
		;

scrub_opt	: NODF	{
			if (scrub_opts.nodf) {
				yyerror("no-df cannot be respecified");
				YYERROR;
			}
			scrub_opts.nodf = 1;
		}
		| MINTTL NUMBER {
			if (scrub_opts.marker & SOM_MINTTL) {
				yyerror("min-ttl cannot be respecified");
				YYERROR;
			}
			if ($2 < 0 || $2 > 255) {
				yyerror("illegal min-ttl value %d", $2);
				YYERROR;
			}
			scrub_opts.marker |= SOM_MINTTL;
			scrub_opts.minttl = $2;
		}
		| MAXMSS NUMBER {
			if (scrub_opts.marker & SOM_MAXMSS) {
				yyerror("max-mss cannot be respecified");
				YYERROR;
			}
			if ($2 < 0 || $2 > 65535) {
				yyerror("illegal max-mss value %d", $2);
				YYERROR;
			}
			scrub_opts.marker |= SOM_MAXMSS;
			scrub_opts.maxmss = $2;
		}
		| SETTOS tos {
			if (scrub_opts.marker & SOM_SETTOS) {
				yyerror("set-tos cannot be respecified");
				YYERROR;
			}
			scrub_opts.marker |= SOM_SETTOS;
			scrub_opts.settos = $2;
		}
		| fragcache {
			if (scrub_opts.marker & SOM_FRAGCACHE) {
				yyerror("fragcache cannot be respecified");
				YYERROR;
			}
			scrub_opts.marker |= SOM_FRAGCACHE;
			scrub_opts.fragcache = $1;
		}
		| REASSEMBLE STRING {
			if (strcasecmp($2, "tcp") != 0) {
				yyerror("scrub reassemble supports only tcp, "
				    "not '%s'", $2);
				free($2);
				YYERROR;
			}
			free($2);
			if (scrub_opts.reassemble_tcp) {
				yyerror("reassemble tcp cannot be respecified");
				YYERROR;
			}
			scrub_opts.reassemble_tcp = 1;
		}
		| RANDOMID {
			if (scrub_opts.randomid) {
				yyerror("random-id cannot be respecified");
				YYERROR;
			}
			scrub_opts.randomid = 1;
		}
		| RTABLE NUMBER				{
			if ($2 < 0 || $2 > rt_tableid_max()) {
				yyerror("invalid rtable id");
				YYERROR;
			}
			scrub_opts.rtableid = $2;
		}
		| not TAGGED string			{
			scrub_opts.match_tag = $3;
			scrub_opts.match_tag_not = $1;
		}
		;

fragcache	: FRAGMENT REASSEMBLE	{ $$ = 0; /* default */ }
		| FRAGMENT FRAGCROP	{ $$ = 0; }
		| FRAGMENT FRAGDROP	{ $$ = 0; }
		;

antispoof	: ANTISPOOF logquick antispoof_ifspc af antispoof_opts {
			struct pf_rule		 r;
			struct node_host	*h = NULL, *hh;
			struct node_if		*i, *j;

			if (check_rulestate(PFCTL_STATE_FILTER))
				YYERROR;

			for (i = $3; i; i = i->next) {
				bzero(&r, sizeof(r));

				r.action = PF_DROP;
				r.direction = PF_IN;
				r.log = $2.log;
				r.logif = $2.logif;
				r.quick = $2.quick;
				r.af = $4;
				if (rule_label(&r, $5.label))
					YYERROR;
				r.rtableid = $5.rtableid;
				j = calloc(1, sizeof(struct node_if));
				if (j == NULL)
					err(1, "antispoof: calloc");
				if (strlcpy(j->ifname, i->ifname,
				    sizeof(j->ifname)) >= sizeof(j->ifname)) {
					free(j);
					yyerror("interface name too long");
					YYERROR;
				}
				j->not = 1;
				if (i->dynamic) {
					h = calloc(1, sizeof(*h));
					if (h == NULL)
						err(1, "address: calloc");
					h->addr.type = PF_ADDR_DYNIFTL;
					set_ipmask(h, 128);
					if (strlcpy(h->addr.v.ifname, i->ifname,
					    sizeof(h->addr.v.ifname)) >=
					    sizeof(h->addr.v.ifname)) {
						free(h);
						yyerror(
						    "interface name too long");
						YYERROR;
					}
					hh = malloc(sizeof(*hh));
					if (hh == NULL)
						 err(1, "address: malloc");
					bcopy(h, hh, sizeof(*hh));
					h->addr.iflags = PFI_AFLAG_NETWORK;
				} else {
					h = ifa_lookup(j->ifname,
					    PFI_AFLAG_NETWORK);
					hh = NULL;
				}

				if (h != NULL)
					expand_rule(&r, j, NULL, NULL, NULL, h,
					    NULL, NULL, NULL, NULL, NULL,
					    NULL, "");

				if ((i->ifa_flags & IFF_LOOPBACK) == 0) {
					bzero(&r, sizeof(r));

					r.action = PF_DROP;
					r.direction = PF_IN;
					r.log = $2.log;
					r.logif = $2.logif;
					r.quick = $2.quick;
					r.af = $4;
					if (rule_label(&r, $5.label))
						YYERROR;
					r.rtableid = $5.rtableid;
					if (hh != NULL)
						h = hh;
					else
						h = ifa_lookup(i->ifname, 0);
					if (h != NULL)
						expand_rule(&r, NULL, NULL,
						    NULL, NULL, h, NULL, NULL,
						    NULL, NULL, NULL, NULL, "");
				} else
					free(hh);
			}
			free($5.label);
		}
		;

antispoof_ifspc	: FOR antispoof_if			{ $$ = $2; }
		| FOR '{' optnl antispoof_iflst '}'	{ $$ = $4; }
		;

antispoof_iflst	: antispoof_if optnl			{ $$ = $1; }
		| antispoof_iflst comma antispoof_if optnl {
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

antispoof_if	: if_item				{ $$ = $1; }
		| '(' if_item ')'			{
			$2->dynamic = 1;
			$$ = $2;
		}
		;

antispoof_opts	:	{
				bzero(&antispoof_opts, sizeof antispoof_opts);
				antispoof_opts.rtableid = -1;
			}
		    antispoof_opts_l
			{ $$ = antispoof_opts; }
		| /* empty */	{
			bzero(&antispoof_opts, sizeof antispoof_opts);
			antispoof_opts.rtableid = -1;
			$$ = antispoof_opts;
		}
		;

antispoof_opts_l	: antispoof_opts_l antispoof_opt
			| antispoof_opt
			;

antispoof_opt	: label	{
			if (antispoof_opts.label) {
				yyerror("label cannot be redefined");
				YYERROR;
			}
			antispoof_opts.label = $1;
		}
		| RTABLE NUMBER				{
			if ($2 < 0 || $2 > rt_tableid_max()) {
				yyerror("invalid rtable id");
				YYERROR;
			}
			antispoof_opts.rtableid = $2;
		}
		;

not		: '!'		{ $$ = 1; }
		| /* empty */	{ $$ = 0; }
		;

tabledef	: TABLE '<' STRING '>' table_opts {
			struct node_host	 *h, *nh;
			struct node_tinit	 *ti, *nti;

			if (strlen($3) >= PF_TABLE_NAME_SIZE) {
				yyerror("table name too long, max %d chars",
				    PF_TABLE_NAME_SIZE - 1);
				free($3);
				YYERROR;
			}
			if (pf->loadopt & PFCTL_FLAG_TABLE)
				if (process_tabledef($3, &$5)) {
					free($3);
					YYERROR;
				}
			free($3);
			for (ti = SIMPLEQ_FIRST(&$5.init_nodes);
			    ti != SIMPLEQ_END(&$5.init_nodes); ti = nti) {
				if (ti->file)
					free(ti->file);
				for (h = ti->host; h != NULL; h = nh) {
					nh = h->next;
					free(h);
				}
				nti = SIMPLEQ_NEXT(ti, entries);
				free(ti);
			}
		}
		;

table_opts	:	{
			bzero(&table_opts, sizeof table_opts);
			SIMPLEQ_INIT(&table_opts.init_nodes);
		}
		    table_opts_l
			{ $$ = table_opts; }
		| /* empty */
			{
			bzero(&table_opts, sizeof table_opts);
			SIMPLEQ_INIT(&table_opts.init_nodes);
			$$ = table_opts;
		}
		;

table_opts_l	: table_opts_l table_opt
		| table_opt
		;

table_opt	: STRING		{
			if (!strcmp($1, "const"))
				table_opts.flags |= PFR_TFLAG_CONST;
			else if (!strcmp($1, "persist"))
				table_opts.flags |= PFR_TFLAG_PERSIST;
			else if (!strcmp($1, "counters"))
				table_opts.flags |= PFR_TFLAG_COUNTERS;
			else {
				yyerror("invalid table option '%s'", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		| '{' optnl '}'		{ table_opts.init_addr = 1; }
		| '{' optnl host_list '}'	{
			struct node_host	*n;
			struct node_tinit	*ti;

			for (n = $3; n != NULL; n = n->next) {
				switch (n->addr.type) {
				case PF_ADDR_ADDRMASK:
					continue; /* ok */
				case PF_ADDR_RANGE:
					yyerror("address ranges are not "
					    "permitted inside tables");
					break;
				case PF_ADDR_DYNIFTL:
					yyerror("dynamic addresses are not "
					    "permitted inside tables");
					break;
				case PF_ADDR_TABLE:
					yyerror("tables cannot contain tables");
					break;
				case PF_ADDR_NOROUTE:
					yyerror("\"no-route\" is not permitted "
					    "inside tables");
					break;
				case PF_ADDR_URPFFAILED:
					yyerror("\"urpf-failed\" is not "
					    "permitted inside tables");
					break;
				default:
					yyerror("unknown address type %d",
					    n->addr.type);
				}
				YYERROR;
			}
			if (!(ti = calloc(1, sizeof(*ti))))
				err(1, "table_opt: calloc");
			ti->host = $3;
			SIMPLEQ_INSERT_TAIL(&table_opts.init_nodes, ti,
			    entries);
			table_opts.init_addr = 1;
		}
		| FILENAME STRING	{
			struct node_tinit	*ti;

			if (!(ti = calloc(1, sizeof(*ti))))
				err(1, "table_opt: calloc");
			ti->file = $2;
			SIMPLEQ_INSERT_TAIL(&table_opts.init_nodes, ti,
			    entries);
			table_opts.init_addr = 1;
		}
		;

altqif		: ALTQ interface queue_opts QUEUE qassign {
			struct pf_altq	a;

			if (check_rulestate(PFCTL_STATE_QUEUE))
				YYERROR;

			memset(&a, 0, sizeof(a));
			if ($3.scheduler.qtype == ALTQT_NONE) {
				yyerror("no scheduler specified!");
				YYERROR;
			}
			a.scheduler = $3.scheduler.qtype;
			a.qlimit = $3.qlimit;
			a.tbrsize = $3.tbrsize;
			if ($5 == NULL && $3.scheduler.qtype != ALTQT_CODEL) {
				yyerror("no child queues specified");
				YYERROR;
			}
			if (expand_altq(&a, $2, $5, $3.queue_bwspec,
			    &$3.scheduler))
				YYERROR;
		}
		;

queuespec	: QUEUE STRING interface queue_opts qassign {
			struct pf_altq	a;

			if (check_rulestate(PFCTL_STATE_QUEUE)) {
				free($2);
				YYERROR;
			}

			memset(&a, 0, sizeof(a));

			if (strlcpy(a.qname, $2, sizeof(a.qname)) >=
			    sizeof(a.qname)) {
				yyerror("queue name too long (max "
				    "%d chars)", PF_QNAME_SIZE-1);
				free($2);
				YYERROR;
			}
			free($2);
			if ($4.tbrsize) {
				yyerror("cannot specify tbrsize for queue");
				YYERROR;
			}
			if ($4.priority > 255) {
				yyerror("priority out of range: max 255");
				YYERROR;
			}
			a.priority = $4.priority;
			a.qlimit = $4.qlimit;
			a.scheduler = $4.scheduler.qtype;
			if (expand_queue(&a, $3, $5, $4.queue_bwspec,
			    &$4.scheduler)) {
				yyerror("errors in queue definition");
				YYERROR;
			}
		}
		;

queue_opts	:	{
			bzero(&queue_opts, sizeof queue_opts);
			queue_opts.priority = DEFAULT_PRIORITY;
			queue_opts.qlimit = DEFAULT_QLIMIT;
			queue_opts.scheduler.qtype = ALTQT_NONE;
			queue_opts.queue_bwspec.bw_percent = 100;
		}
		    queue_opts_l
			{ $$ = queue_opts; }
		| /* empty */ {
			bzero(&queue_opts, sizeof queue_opts);
			queue_opts.priority = DEFAULT_PRIORITY;
			queue_opts.qlimit = DEFAULT_QLIMIT;
			queue_opts.scheduler.qtype = ALTQT_NONE;
			queue_opts.queue_bwspec.bw_percent = 100;
			$$ = queue_opts;
		}
		;

queue_opts_l	: queue_opts_l queue_opt
		| queue_opt
		;

queue_opt	: BANDWIDTH bandwidth	{
			if (queue_opts.marker & QOM_BWSPEC) {
				yyerror("bandwidth cannot be respecified");
				YYERROR;
			}
			queue_opts.marker |= QOM_BWSPEC;
			queue_opts.queue_bwspec = $2;
		}
		| PRIORITY NUMBER	{
			if (queue_opts.marker & QOM_PRIORITY) {
				yyerror("priority cannot be respecified");
				YYERROR;
			}
			if ($2 < 0 || $2 > 255) {
				yyerror("priority out of range: max 255");
				YYERROR;
			}
			queue_opts.marker |= QOM_PRIORITY;
			queue_opts.priority = $2;
		}
		| QLIMIT NUMBER	{
			if (queue_opts.marker & QOM_QLIMIT) {
				yyerror("qlimit cannot be respecified");
				YYERROR;
			}
			if ($2 < 0 || $2 > 65535) {
				yyerror("qlimit out of range: max 65535");
				YYERROR;
			}
			queue_opts.marker |= QOM_QLIMIT;
			queue_opts.qlimit = $2;
		}
		| scheduler	{
			if (queue_opts.marker & QOM_SCHEDULER) {
				yyerror("scheduler cannot be respecified");
				YYERROR;
			}
			queue_opts.marker |= QOM_SCHEDULER;
			queue_opts.scheduler = $1;
		}
		| TBRSIZE NUMBER	{
			if (queue_opts.marker & QOM_TBRSIZE) {
				yyerror("tbrsize cannot be respecified");
				YYERROR;
			}
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("tbrsize too big: max %u", UINT_MAX);
				YYERROR;
			}
			queue_opts.marker |= QOM_TBRSIZE;
			queue_opts.tbrsize = $2;
		}
		;

bandwidth	: STRING {
			double	 bps;
			char	*cp;

			$$.bw_percent = 0;

			bps = strtod($1, &cp);
			if (cp != NULL) {
				if (strlen(cp) > 1) {
					char *cu = cp + 1;
					if (!strcmp(cu, "Bit") ||
					    !strcmp(cu, "B") ||
					    !strcmp(cu, "bit") ||
					    !strcmp(cu, "b")) {
						*cu = 0;
					}
				}
				if (!strcmp(cp, "b"))
					; /* nothing */
				else if (!strcmp(cp, "K"))
					bps *= 1000;
				else if (!strcmp(cp, "M"))
					bps *= 1000 * 1000;
				else if (!strcmp(cp, "G"))
					bps *= 1000 * 1000 * 1000;
				else if (!strcmp(cp, "%")) {
					if (bps < 0 || bps > 100) {
						yyerror("bandwidth spec "
						    "out of range");
						free($1);
						YYERROR;
					}
					$$.bw_percent = bps;
					bps = 0;
				} else {
					yyerror("unknown unit %s", cp);
					free($1);
					YYERROR;
				}
			}
			free($1);
			$$.bw_absolute = (u_int64_t)bps;
		}
		| NUMBER {
			if ($1 < 0 || $1 >= LLONG_MAX) {
				yyerror("bandwidth number too big");
				YYERROR;
			}
			$$.bw_percent = 0;
			$$.bw_absolute = $1;
		}
		;

scheduler	: CBQ				{
			$$.qtype = ALTQT_CBQ;
			$$.data.cbq_opts.flags = 0;
		}
		| CBQ '(' cbqflags_list ')'	{
			$$.qtype = ALTQT_CBQ;
			$$.data.cbq_opts.flags = $3;
		}
		| PRIQ				{
			$$.qtype = ALTQT_PRIQ;
			$$.data.priq_opts.flags = 0;
		}
		| PRIQ '(' priqflags_list ')'	{
			$$.qtype = ALTQT_PRIQ;
			$$.data.priq_opts.flags = $3;
		}
		| HFSC				{
			$$.qtype = ALTQT_HFSC;
			bzero(&$$.data.hfsc_opts,
			    sizeof(struct node_hfsc_opts));
		}
		| HFSC '(' hfsc_opts ')'	{
			$$.qtype = ALTQT_HFSC;
			$$.data.hfsc_opts = $3;
		}
		| FAIRQ				{
			$$.qtype = ALTQT_FAIRQ;
			bzero(&$$.data.fairq_opts,
				sizeof(struct node_fairq_opts));
		}
		| FAIRQ '(' fairq_opts ')'      {
			$$.qtype = ALTQT_FAIRQ;
			$$.data.fairq_opts = $3;
		}
		| CODEL				{
			$$.qtype = ALTQT_CODEL;
			bzero(&$$.data.codel_opts,
				sizeof(struct codel_opts));
		}
		| CODEL '(' codel_opts ')'	{
			$$.qtype = ALTQT_CODEL;
			$$.data.codel_opts = $3;
		}
		;

cbqflags_list	: cbqflags_item				{ $$ |= $1; }
		| cbqflags_list comma cbqflags_item	{ $$ |= $3; }
		;

cbqflags_item	: STRING	{
			if (!strcmp($1, "default"))
				$$ = CBQCLF_DEFCLASS;
			else if (!strcmp($1, "borrow"))
				$$ = CBQCLF_BORROW;
			else if (!strcmp($1, "red"))
				$$ = CBQCLF_RED;
			else if (!strcmp($1, "ecn"))
				$$ = CBQCLF_RED|CBQCLF_ECN;
			else if (!strcmp($1, "rio"))
				$$ = CBQCLF_RIO;
			else if (!strcmp($1, "codel"))
				$$ = CBQCLF_CODEL;
			else {
				yyerror("unknown cbq flag \"%s\"", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

priqflags_list	: priqflags_item			{ $$ |= $1; }
		| priqflags_list comma priqflags_item	{ $$ |= $3; }
		;

priqflags_item	: STRING	{
			if (!strcmp($1, "default"))
				$$ = PRCF_DEFAULTCLASS;
			else if (!strcmp($1, "red"))
				$$ = PRCF_RED;
			else if (!strcmp($1, "ecn"))
				$$ = PRCF_RED|PRCF_ECN;
			else if (!strcmp($1, "rio"))
				$$ = PRCF_RIO;
			else if (!strcmp($1, "codel"))
				$$ = PRCF_CODEL;
			else {
				yyerror("unknown priq flag \"%s\"", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

hfsc_opts	:	{
				bzero(&hfsc_opts,
				    sizeof(struct node_hfsc_opts));
			}
		    hfscopts_list				{
			$$ = hfsc_opts;
		}
		;

hfscopts_list	: hfscopts_item
		| hfscopts_list comma hfscopts_item
		;

hfscopts_item	: LINKSHARE bandwidth				{
			if (hfsc_opts.linkshare.used) {
				yyerror("linkshare already specified");
				YYERROR;
			}
			hfsc_opts.linkshare.m2 = $2;
			hfsc_opts.linkshare.used = 1;
		}
		| LINKSHARE '(' bandwidth comma NUMBER comma bandwidth ')'
		    {
			if ($5 < 0 || $5 > INT_MAX) {
				yyerror("timing in curve out of range");
				YYERROR;
			}
			if (hfsc_opts.linkshare.used) {
				yyerror("linkshare already specified");
				YYERROR;
			}
			hfsc_opts.linkshare.m1 = $3;
			hfsc_opts.linkshare.d = $5;
			hfsc_opts.linkshare.m2 = $7;
			hfsc_opts.linkshare.used = 1;
		}
		| REALTIME bandwidth				{
			if (hfsc_opts.realtime.used) {
				yyerror("realtime already specified");
				YYERROR;
			}
			hfsc_opts.realtime.m2 = $2;
			hfsc_opts.realtime.used = 1;
		}
		| REALTIME '(' bandwidth comma NUMBER comma bandwidth ')'
		    {
			if ($5 < 0 || $5 > INT_MAX) {
				yyerror("timing in curve out of range");
				YYERROR;
			}
			if (hfsc_opts.realtime.used) {
				yyerror("realtime already specified");
				YYERROR;
			}
			hfsc_opts.realtime.m1 = $3;
			hfsc_opts.realtime.d = $5;
			hfsc_opts.realtime.m2 = $7;
			hfsc_opts.realtime.used = 1;
		}
		| UPPERLIMIT bandwidth				{
			if (hfsc_opts.upperlimit.used) {
				yyerror("upperlimit already specified");
				YYERROR;
			}
			hfsc_opts.upperlimit.m2 = $2;
			hfsc_opts.upperlimit.used = 1;
		}
		| UPPERLIMIT '(' bandwidth comma NUMBER comma bandwidth ')'
		    {
			if ($5 < 0 || $5 > INT_MAX) {
				yyerror("timing in curve out of range");
				YYERROR;
			}
			if (hfsc_opts.upperlimit.used) {
				yyerror("upperlimit already specified");
				YYERROR;
			}
			hfsc_opts.upperlimit.m1 = $3;
			hfsc_opts.upperlimit.d = $5;
			hfsc_opts.upperlimit.m2 = $7;
			hfsc_opts.upperlimit.used = 1;
		}
		| STRING	{
			if (!strcmp($1, "default"))
				hfsc_opts.flags |= HFCF_DEFAULTCLASS;
			else if (!strcmp($1, "red"))
				hfsc_opts.flags |= HFCF_RED;
			else if (!strcmp($1, "ecn"))
				hfsc_opts.flags |= HFCF_RED|HFCF_ECN;
			else if (!strcmp($1, "rio"))
				hfsc_opts.flags |= HFCF_RIO;
			else if (!strcmp($1, "codel"))
				hfsc_opts.flags |= HFCF_CODEL;
			else {
				yyerror("unknown hfsc flag \"%s\"", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

fairq_opts	:	{
				bzero(&fairq_opts,
				    sizeof(struct node_fairq_opts));
			}
		    fairqopts_list				{
			$$ = fairq_opts;
		}
		;

fairqopts_list	: fairqopts_item
		| fairqopts_list comma fairqopts_item
		;

fairqopts_item	: LINKSHARE bandwidth				{
			if (fairq_opts.linkshare.used) {
				yyerror("linkshare already specified");
				YYERROR;
			}
			fairq_opts.linkshare.m2 = $2;
			fairq_opts.linkshare.used = 1;
		}
		| LINKSHARE '(' bandwidth number bandwidth ')'	{
			if (fairq_opts.linkshare.used) {
				yyerror("linkshare already specified");
				YYERROR;
			}
			fairq_opts.linkshare.m1 = $3;
			fairq_opts.linkshare.d = $4;
			fairq_opts.linkshare.m2 = $5;
			fairq_opts.linkshare.used = 1;
		}
		| HOGS bandwidth {
			fairq_opts.hogs_bw = $2;
		}
		| BUCKETS number {
			fairq_opts.nbuckets = $2;
		}
		| STRING	{
			if (!strcmp($1, "default"))
				fairq_opts.flags |= FARF_DEFAULTCLASS;
			else if (!strcmp($1, "red"))
				fairq_opts.flags |= FARF_RED;
			else if (!strcmp($1, "ecn"))
				fairq_opts.flags |= FARF_RED|FARF_ECN;
			else if (!strcmp($1, "rio"))
				fairq_opts.flags |= FARF_RIO;
			else if (!strcmp($1, "codel"))
				fairq_opts.flags |= FARF_CODEL;
			else {
				yyerror("unknown fairq flag \"%s\"", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

codel_opts	:	{
				bzero(&codel_opts,
				    sizeof(struct codel_opts));
			}
		    codelopts_list				{
			$$ = codel_opts;
		}
		;

codelopts_list	: codelopts_item
		| codelopts_list comma codelopts_item
		;

codelopts_item	: INTERVAL number				{
			if (codel_opts.interval) {
				yyerror("interval already specified");
				YYERROR;
			}
			codel_opts.interval = $2;
		}
		| TARGET number					{
			if (codel_opts.target) {
				yyerror("target already specified");
				YYERROR;
			}
			codel_opts.target = $2;
		}
		| STRING					{
			if (!strcmp($1, "ecn"))
				codel_opts.ecn = 1;
			else {
				yyerror("unknown codel option \"%s\"", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

qassign		: /* empty */		{ $$ = NULL; }
		| qassign_item		{ $$ = $1; }
		| '{' optnl qassign_list '}'	{ $$ = $3; }
		;

qassign_list	: qassign_item optnl		{ $$ = $1; }
		| qassign_list comma qassign_item optnl	{
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

qassign_item	: STRING			{
			$$ = calloc(1, sizeof(struct node_queue));
			if ($$ == NULL)
				err(1, "qassign_item: calloc");
			if (strlcpy($$->queue, $1, sizeof($$->queue)) >=
			    sizeof($$->queue)) {
				yyerror("queue name '%s' too long (max "
				    "%d chars)", $1, sizeof($$->queue)-1);
				free($1);
				free($$);
				YYERROR;
			}
			free($1);
			$$->next = NULL;
			$$->tail = $$;
		}
		;

pfrule		: action dir logquick interface route af proto fromto
		    filter_opts
		{
			struct pf_rule		 r;
			struct node_state_opt	*o;
			struct node_proto	*proto;
			int			 srctrack = 0;
			int			 statelock = 0;
			int			 adaptive = 0;
			int			 defaults = 0;

			if (check_rulestate(PFCTL_STATE_FILTER))
				YYERROR;

			memset(&r, 0, sizeof(r));

			r.action = $1.b1;
			switch ($1.b2) {
			case PFRULE_RETURNRST:
				r.rule_flag |= PFRULE_RETURNRST;
				r.return_ttl = $1.w;
				break;
			case PFRULE_RETURNICMP:
				r.rule_flag |= PFRULE_RETURNICMP;
				r.return_icmp = $1.w;
				r.return_icmp6 = $1.w2;
				break;
			case PFRULE_RETURN:
				r.rule_flag |= PFRULE_RETURN;
				r.return_icmp = $1.w;
				r.return_icmp6 = $1.w2;
				break;
			}
			r.direction = $2;
			r.log = $3.log;
			r.logif = $3.logif;
			r.quick = $3.quick;
			r.prob = $9.prob;
			r.rtableid = $9.rtableid;

			if ($9.marker & FOM_PRIO) {
				if ($9.prio == 0)
					r.prio = PF_PRIO_ZERO;
				else
					r.prio = $9.prio;
			}
			if ($9.marker & FOM_SETPRIO) {
				r.set_prio[0] = $9.set_prio[0];
				r.set_prio[1] = $9.set_prio[1];
				r.scrub_flags |= PFSTATE_SETPRIO;
			}

			r.af = $6;
			if ($9.tag)
				if (strlcpy(r.tagname, $9.tag,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			if ($9.match_tag)
				if (strlcpy(r.match_tagname, $9.match_tag,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			r.match_tag_not = $9.match_tag_not;
			if (rule_label(&r, $9.label))
				YYERROR;
			free($9.label);
			r.flags = $9.flags.b1;
			r.flagset = $9.flags.b2;
			if (($9.flags.b1 & $9.flags.b2) != $9.flags.b1) {
				yyerror("flags always false");
				YYERROR;
			}
			if ($9.flags.b1 || $9.flags.b2 || $8.src_os) {
				for (proto = $7; proto != NULL &&
				    proto->proto != IPPROTO_TCP;
				    proto = proto->next)
					;	/* nothing */
				if (proto == NULL && $7 != NULL) {
					if ($9.flags.b1 || $9.flags.b2)
						yyerror(
						    "flags only apply to tcp");
					if ($8.src_os)
						yyerror(
						    "OS fingerprinting only "
						    "apply to tcp");
					YYERROR;
				}
#if 0
				if (($9.flags.b1 & parse_flags("S")) == 0 &&
				    $8.src_os) {
					yyerror("OS fingerprinting requires "
					    "the SYN TCP flag (flags S/SA)");
					YYERROR;
				}
#endif
			}

			r.tos = $9.tos;
			r.keep_state = $9.keep.action;
			o = $9.keep.options;

			/* 'keep state' by default on pass rules. */
			if (!r.keep_state && !r.action &&
			    !($9.marker & FOM_KEEP)) {
				r.keep_state = PF_STATE_NORMAL;
				o = keep_state_defaults;
				defaults = 1;
			}

			while (o) {
				struct node_state_opt	*p = o;

				switch (o->type) {
				case PF_STATE_OPT_MAX:
					if (r.max_states) {
						yyerror("state option 'max' "
						    "multiple definitions");
						YYERROR;
					}
					r.max_states = o->data.max_states;
					break;
				case PF_STATE_OPT_NOSYNC:
					if (r.rule_flag & PFRULE_NOSYNC) {
						yyerror("state option 'sync' "
						    "multiple definitions");
						YYERROR;
					}
					r.rule_flag |= PFRULE_NOSYNC;
					break;
				case PF_STATE_OPT_SRCTRACK:
					if (srctrack) {
						yyerror("state option "
						    "'source-track' "
						    "multiple definitions");
						YYERROR;
					}
					srctrack =  o->data.src_track;
					r.rule_flag |= PFRULE_SRCTRACK;
					break;
				case PF_STATE_OPT_MAX_SRC_STATES:
					if (r.max_src_states) {
						yyerror("state option "
						    "'max-src-states' "
						    "multiple definitions");
						YYERROR;
					}
					if (o->data.max_src_states == 0) {
						yyerror("'max-src-states' must "
						    "be > 0");
						YYERROR;
					}
					r.max_src_states =
					    o->data.max_src_states;
					r.rule_flag |= PFRULE_SRCTRACK;
					break;
				case PF_STATE_OPT_OVERLOAD:
					if (r.overload_tblname[0]) {
						yyerror("multiple 'overload' "
						    "table definitions");
						YYERROR;
					}
					if (strlcpy(r.overload_tblname,
					    o->data.overload.tblname,
					    PF_TABLE_NAME_SIZE) >=
					    PF_TABLE_NAME_SIZE) {
						yyerror("state option: "
						    "strlcpy");
						YYERROR;
					}
					r.flush = o->data.overload.flush;
					break;
				case PF_STATE_OPT_MAX_SRC_CONN:
					if (r.max_src_conn) {
						yyerror("state option "
						    "'max-src-conn' "
						    "multiple definitions");
						YYERROR;
					}
					if (o->data.max_src_conn == 0) {
						yyerror("'max-src-conn' "
						    "must be > 0");
						YYERROR;
					}
					r.max_src_conn =
					    o->data.max_src_conn;
					r.rule_flag |= PFRULE_SRCTRACK |
					    PFRULE_RULESRCTRACK;
					break;
				case PF_STATE_OPT_MAX_SRC_CONN_RATE:
					if (r.max_src_conn_rate.limit) {
						yyerror("state option "
						    "'max-src-conn-rate' "
						    "multiple definitions");
						YYERROR;
					}
					if (!o->data.max_src_conn_rate.limit ||
					    !o->data.max_src_conn_rate.seconds) {
						yyerror("'max-src-conn-rate' "
						    "values must be > 0");
						YYERROR;
					}
					if (o->data.max_src_conn_rate.limit >
					    PF_THRESHOLD_MAX) {
						yyerror("'max-src-conn-rate' "
						    "maximum rate must be < %u",
						    PF_THRESHOLD_MAX);
						YYERROR;
					}
					r.max_src_conn_rate.limit =
					    o->data.max_src_conn_rate.limit;
					r.max_src_conn_rate.seconds =
					    o->data.max_src_conn_rate.seconds;
					r.rule_flag |= PFRULE_SRCTRACK |
					    PFRULE_RULESRCTRACK;
					break;
				case PF_STATE_OPT_MAX_SRC_NODES:
					if (r.max_src_nodes) {
						yyerror("state option "
						    "'max-src-nodes' "
						    "multiple definitions");
						YYERROR;
					}
					if (o->data.max_src_nodes == 0) {
						yyerror("'max-src-nodes' must "
						    "be > 0");
						YYERROR;
					}
					r.max_src_nodes =
					    o->data.max_src_nodes;
					r.rule_flag |= PFRULE_SRCTRACK |
					    PFRULE_RULESRCTRACK;
					break;
				case PF_STATE_OPT_STATELOCK:
					if (statelock) {
						yyerror("state locking option: "
						    "multiple definitions");
						YYERROR;
					}
					statelock = 1;
					r.rule_flag |= o->data.statelock;
					break;
				case PF_STATE_OPT_SLOPPY:
					if (r.rule_flag & PFRULE_STATESLOPPY) {
						yyerror("state sloppy option: "
						    "multiple definitions");
						YYERROR;
					}
					r.rule_flag |= PFRULE_STATESLOPPY;
					break;
				case PF_STATE_OPT_TIMEOUT:
					if (o->data.timeout.number ==
					    PFTM_ADAPTIVE_START ||
					    o->data.timeout.number ==
					    PFTM_ADAPTIVE_END)
						adaptive = 1;
					if (r.timeout[o->data.timeout.number]) {
						yyerror("state timeout %s "
						    "multiple definitions",
						    pf_timeouts[o->data.
						    timeout.number].name);
						YYERROR;
					}
					r.timeout[o->data.timeout.number] =
					    o->data.timeout.seconds;
				}
				o = o->next;
				if (!defaults)
					free(p);
			}

			/* 'flags S/SA' by default on stateful rules */
			if (!r.action && !r.flags && !r.flagset &&
			    !$9.fragment && !($9.marker & FOM_FLAGS) &&
			    r.keep_state) {
				r.flags = parse_flags("S");
				r.flagset =  parse_flags("SA");
			}
			if (!adaptive && r.max_states) {
				r.timeout[PFTM_ADAPTIVE_START] =
				    (r.max_states / 10) * 6;
				r.timeout[PFTM_ADAPTIVE_END] =
				    (r.max_states / 10) * 12;
			}
			if (r.rule_flag & PFRULE_SRCTRACK) {
				if (srctrack == PF_SRCTRACK_GLOBAL &&
				    r.max_src_nodes) {
					yyerror("'max-src-nodes' is "
					    "incompatible with "
					    "'source-track global'");
					YYERROR;
				}
				if (srctrack == PF_SRCTRACK_GLOBAL &&
				    r.max_src_conn) {
					yyerror("'max-src-conn' is "
					    "incompatible with "
					    "'source-track global'");
					YYERROR;
				}
				if (srctrack == PF_SRCTRACK_GLOBAL &&
				    r.max_src_conn_rate.seconds) {
					yyerror("'max-src-conn-rate' is "
					    "incompatible with "
					    "'source-track global'");
					YYERROR;
				}
				if (r.timeout[PFTM_SRC_NODE] <
				    r.max_src_conn_rate.seconds)
					r.timeout[PFTM_SRC_NODE] =
					    r.max_src_conn_rate.seconds;
				r.rule_flag |= PFRULE_SRCTRACK;
				if (srctrack == PF_SRCTRACK_RULE)
					r.rule_flag |= PFRULE_RULESRCTRACK;
			}
			if (r.keep_state && !statelock)
				r.rule_flag |= default_statelock;

			if ($9.fragment)
				r.rule_flag |= PFRULE_FRAGMENT;
			r.allow_opts = $9.allowopts;

			decide_address_family($8.src.host, &r.af);
			decide_address_family($8.dst.host, &r.af);

			if ($5.rt) {
				if (!r.direction) {
					yyerror("direction must be explicit "
					    "with rules that specify routing");
					YYERROR;
				}
				r.rt = $5.rt;
				r.rpool.opts = $5.pool_opts;
				if ($5.key != NULL)
					memcpy(&r.rpool.key, $5.key,
					    sizeof(struct pf_poolhashkey));
			}
			if (r.rt) {
				decide_address_family($5.host, &r.af);
				remove_invalid_hosts(&$5.host, &r.af);
				if ($5.host == NULL) {
					yyerror("no routing address with "
					    "matching address family found.");
					YYERROR;
				}
				if ((r.rpool.opts & PF_POOL_TYPEMASK) ==
				    PF_POOL_NONE && ($5.host->next != NULL ||
				    $5.host->addr.type == PF_ADDR_TABLE ||
				    DYNIF_MULTIADDR($5.host->addr)))
					r.rpool.opts |= PF_POOL_ROUNDROBIN;
				if ((r.rpool.opts & PF_POOL_TYPEMASK) !=
				    PF_POOL_ROUNDROBIN &&
				    disallow_table($5.host, "tables are only "
				    "supported in round-robin routing pools"))
					YYERROR;
				if ((r.rpool.opts & PF_POOL_TYPEMASK) !=
				    PF_POOL_ROUNDROBIN &&
				    disallow_alias($5.host, "interface (%s) "
				    "is only supported in round-robin "
				    "routing pools"))
					YYERROR;
				if ($5.host->next != NULL) {
					if ((r.rpool.opts & PF_POOL_TYPEMASK) !=
					    PF_POOL_ROUNDROBIN) {
						yyerror("r.rpool.opts must "
						    "be PF_POOL_ROUNDROBIN");
						YYERROR;
					}
				}
			}
			if ($9.queues.qname != NULL) {
				if (strlcpy(r.qname, $9.queues.qname,
				    sizeof(r.qname)) >= sizeof(r.qname)) {
					yyerror("rule qname too long (max "
					    "%d chars)", sizeof(r.qname)-1);
					YYERROR;
				}
				free($9.queues.qname);
			}
			if ($9.queues.pqname != NULL) {
				if (strlcpy(r.pqname, $9.queues.pqname,
				    sizeof(r.pqname)) >= sizeof(r.pqname)) {
					yyerror("rule pqname too long (max "
					    "%d chars)", sizeof(r.pqname)-1);
					YYERROR;
				}
				free($9.queues.pqname);
			}
#ifdef __FreeBSD__
			r.divert.port = $9.divert.port;
#else
			if ((r.divert.port = $9.divert.port)) {
				if (r.direction == PF_OUT) {
					if ($9.divert.addr) {
						yyerror("address specified "
						    "for outgoing divert");
						YYERROR;
					}
					bzero(&r.divert.addr,
					    sizeof(r.divert.addr));
				} else {
					if (!$9.divert.addr) {
						yyerror("no address specified "
						    "for incoming divert");
						YYERROR;
					}
					if ($9.divert.addr->af != r.af) {
						yyerror("address family "
						    "mismatch for divert");
						YYERROR;
					}
					r.divert.addr =
					    $9.divert.addr->addr.v.a.addr;
				}
			}
#endif

			expand_rule(&r, $4, $5.host, $7, $8.src_os,
			    $8.src.host, $8.src.port, $8.dst.host, $8.dst.port,
			    $9.uid, $9.gid, $9.icmpspec, "");
		}
		;

filter_opts	:	{
				bzero(&filter_opts, sizeof filter_opts);
				filter_opts.rtableid = -1;
			}
		    filter_opts_l
			{ $$ = filter_opts; }
		| /* empty */	{
			bzero(&filter_opts, sizeof filter_opts);
			filter_opts.rtableid = -1;
			$$ = filter_opts;
		}
		;

filter_opts_l	: filter_opts_l filter_opt
		| filter_opt
		;

filter_opt	: USER uids {
			if (filter_opts.uid)
				$2->tail->next = filter_opts.uid;
			filter_opts.uid = $2;
		}
		| GROUP gids {
			if (filter_opts.gid)
				$2->tail->next = filter_opts.gid;
			filter_opts.gid = $2;
		}
		| flags {
			if (filter_opts.marker & FOM_FLAGS) {
				yyerror("flags cannot be redefined");
				YYERROR;
			}
			filter_opts.marker |= FOM_FLAGS;
			filter_opts.flags.b1 |= $1.b1;
			filter_opts.flags.b2 |= $1.b2;
			filter_opts.flags.w |= $1.w;
			filter_opts.flags.w2 |= $1.w2;
		}
		| icmpspec {
			if (filter_opts.marker & FOM_ICMP) {
				yyerror("icmp-type cannot be redefined");
				YYERROR;
			}
			filter_opts.marker |= FOM_ICMP;
			filter_opts.icmpspec = $1;
		}
		| PRIO NUMBER {
			if (filter_opts.marker & FOM_PRIO) {
				yyerror("prio cannot be redefined");
				YYERROR;
			}
			if ($2 < 0 || $2 > PF_PRIO_MAX) {
				yyerror("prio must be 0 - %u", PF_PRIO_MAX);
				YYERROR;
			}
			filter_opts.marker |= FOM_PRIO;
			filter_opts.prio = $2;
		}
		| TOS tos {
			if (filter_opts.marker & FOM_TOS) {
				yyerror("tos cannot be redefined");
				YYERROR;
			}
			filter_opts.marker |= FOM_TOS;
			filter_opts.tos = $2;
		}
		| keep {
			if (filter_opts.marker & FOM_KEEP) {
				yyerror("modulate or keep cannot be redefined");
				YYERROR;
			}
			filter_opts.marker |= FOM_KEEP;
			filter_opts.keep.action = $1.action;
			filter_opts.keep.options = $1.options;
		}
		| FRAGMENT {
			filter_opts.fragment = 1;
		}
		| ALLOWOPTS {
			filter_opts.allowopts = 1;
		}
		| label	{
			if (filter_opts.label) {
				yyerror("label cannot be redefined");
				YYERROR;
			}
			filter_opts.label = $1;
		}
		| qname	{
			if (filter_opts.queues.qname) {
				yyerror("queue cannot be redefined");
				YYERROR;
			}
			filter_opts.queues = $1;
		}
		| TAG string				{
			filter_opts.tag = $2;
		}
		| not TAGGED string			{
			filter_opts.match_tag = $3;
			filter_opts.match_tag_not = $1;
		}
		| PROBABILITY probability		{
			double	p;

			p = floor($2 * UINT_MAX + 0.5);
			if (p < 0.0 || p > UINT_MAX) {
				yyerror("invalid probability: %lf", p);
				YYERROR;
			}
			filter_opts.prob = (u_int32_t)p;
			if (filter_opts.prob == 0)
				filter_opts.prob = 1;
		}
		| RTABLE NUMBER				{
			if ($2 < 0 || $2 > rt_tableid_max()) {
				yyerror("invalid rtable id");
				YYERROR;
			}
			filter_opts.rtableid = $2;
		}
		| DIVERTTO portplain {
#ifdef __FreeBSD__
			filter_opts.divert.port = $2.a;
			if (!filter_opts.divert.port) {
				yyerror("invalid divert port: %u", ntohs($2.a));
				YYERROR;
			}
#endif
		}
		| DIVERTTO STRING PORT portplain {
#ifndef __FreeBSD__
			if ((filter_opts.divert.addr = host($2)) == NULL) {
				yyerror("could not parse divert address: %s",
				    $2);
				free($2);
				YYERROR;
			}
#else
			if ($2)
#endif
			free($2);
			filter_opts.divert.port = $4.a;
			if (!filter_opts.divert.port) {
				yyerror("invalid divert port: %u", ntohs($4.a));
				YYERROR;
			}
		}
		| DIVERTREPLY {
#ifdef __FreeBSD__
			yyerror("divert-reply has no meaning in FreeBSD pf(4)");
			YYERROR;
#else
			filter_opts.divert.port = 1;	/* some random value */
#endif
		}
		| filter_sets
		;

filter_sets	: SET '(' filter_sets_l ')'	{ $$ = filter_opts; }
		| SET filter_set		{ $$ = filter_opts; }
		;

filter_sets_l	: filter_sets_l comma filter_set
		| filter_set
		;

filter_set	: prio {
			if (filter_opts.marker & FOM_SETPRIO) {
				yyerror("prio cannot be redefined");
				YYERROR;
			}
			filter_opts.marker |= FOM_SETPRIO;
			filter_opts.set_prio[0] = $1.b1;
			filter_opts.set_prio[1] = $1.b2;
		}
prio		: PRIO NUMBER {
			if ($2 < 0 || $2 > PF_PRIO_MAX) {
				yyerror("prio must be 0 - %u", PF_PRIO_MAX);
				YYERROR;
			}
			$$.b1 = $$.b2 = $2;
		}
		| PRIO '(' NUMBER comma NUMBER ')' {
			if ($3 < 0 || $3 > PF_PRIO_MAX ||
			    $5 < 0 || $5 > PF_PRIO_MAX) {
				yyerror("prio must be 0 - %u", PF_PRIO_MAX);
				YYERROR;
			}
			$$.b1 = $3;
			$$.b2 = $5;
		}
		;

probability	: STRING				{
			char	*e;
			double	 p = strtod($1, &e);

			if (*e == '%') {
				p *= 0.01;
				e++;
			}
			if (*e) {
				yyerror("invalid probability: %s", $1);
				free($1);
				YYERROR;
			}
			free($1);
			$$ = p;
		}
		| NUMBER				{
			$$ = (double)$1;
		}
		;


action		: PASS 			{
			$$.b1 = PF_PASS;
			$$.b2 = failpolicy;
			$$.w = returnicmpdefault;
			$$.w2 = returnicmp6default;
		}
		| BLOCK blockspec	{ $$ = $2; $$.b1 = PF_DROP; }
		;

blockspec	: /* empty */		{
			$$.b2 = blockpolicy;
			$$.w = returnicmpdefault;
			$$.w2 = returnicmp6default;
		}
		| DROP			{
			$$.b2 = PFRULE_DROP;
			$$.w = 0;
			$$.w2 = 0;
		}
		| RETURNRST		{
			$$.b2 = PFRULE_RETURNRST;
			$$.w = 0;
			$$.w2 = 0;
		}
		| RETURNRST '(' TTL NUMBER ')'	{
			if ($4 < 0 || $4 > 255) {
				yyerror("illegal ttl value %d", $4);
				YYERROR;
			}
			$$.b2 = PFRULE_RETURNRST;
			$$.w = $4;
			$$.w2 = 0;
		}
		| RETURNICMP		{
			$$.b2 = PFRULE_RETURNICMP;
			$$.w = returnicmpdefault;
			$$.w2 = returnicmp6default;
		}
		| RETURNICMP6		{
			$$.b2 = PFRULE_RETURNICMP;
			$$.w = returnicmpdefault;
			$$.w2 = returnicmp6default;
		}
		| RETURNICMP '(' reticmpspec ')'	{
			$$.b2 = PFRULE_RETURNICMP;
			$$.w = $3;
			$$.w2 = returnicmpdefault;
		}
		| RETURNICMP6 '(' reticmp6spec ')'	{
			$$.b2 = PFRULE_RETURNICMP;
			$$.w = returnicmpdefault;
			$$.w2 = $3;
		}
		| RETURNICMP '(' reticmpspec comma reticmp6spec ')' {
			$$.b2 = PFRULE_RETURNICMP;
			$$.w = $3;
			$$.w2 = $5;
		}
		| RETURN {
			$$.b2 = PFRULE_RETURN;
			$$.w = returnicmpdefault;
			$$.w2 = returnicmp6default;
		}
		;

reticmpspec	: STRING			{
			if (!($$ = parseicmpspec($1, AF_INET))) {
				free($1);
				YYERROR;
			}
			free($1);
		}
		| NUMBER			{
			u_int8_t		icmptype;

			if ($1 < 0 || $1 > 255) {
				yyerror("invalid icmp code %lu", $1);
				YYERROR;
			}
			icmptype = returnicmpdefault >> 8;
			$$ = (icmptype << 8 | $1);
		}
		;

reticmp6spec	: STRING			{
			if (!($$ = parseicmpspec($1, AF_INET6))) {
				free($1);
				YYERROR;
			}
			free($1);
		}
		| NUMBER			{
			u_int8_t		icmptype;

			if ($1 < 0 || $1 > 255) {
				yyerror("invalid icmp code %lu", $1);
				YYERROR;
			}
			icmptype = returnicmp6default >> 8;
			$$ = (icmptype << 8 | $1);
		}
		;

dir		: /* empty */			{ $$ = PF_INOUT; }
		| IN				{ $$ = PF_IN; }
		| OUT				{ $$ = PF_OUT; }
		;

quick		: /* empty */			{ $$.quick = 0; }
		| QUICK				{ $$.quick = 1; }
		;

logquick	: /* empty */	{ $$.log = 0; $$.quick = 0; $$.logif = 0; }
		| log		{ $$ = $1; $$.quick = 0; }
		| QUICK		{ $$.quick = 1; $$.log = 0; $$.logif = 0; }
		| log QUICK	{ $$ = $1; $$.quick = 1; }
		| QUICK log	{ $$ = $2; $$.quick = 1; }
		;

log		: LOG			{ $$.log = PF_LOG; $$.logif = 0; }
		| LOG '(' logopts ')'	{
			$$.log = PF_LOG | $3.log;
			$$.logif = $3.logif;
		}
		;

logopts		: logopt			{ $$ = $1; }
		| logopts comma logopt		{
			$$.log = $1.log | $3.log;
			$$.logif = $3.logif;
			if ($$.logif == 0)
				$$.logif = $1.logif;
		}
		;

logopt		: ALL		{ $$.log = PF_LOG_ALL; $$.logif = 0; }
		| USER		{ $$.log = PF_LOG_SOCKET_LOOKUP; $$.logif = 0; }
		| GROUP		{ $$.log = PF_LOG_SOCKET_LOOKUP; $$.logif = 0; }
		| TO string	{
			const char	*errstr;
			u_int		 i;

			$$.log = 0;
			if (strncmp($2, "pflog", 5)) {
				yyerror("%s: should be a pflog interface", $2);
				free($2);
				YYERROR;
			}
			i = strtonum($2 + 5, 0, 255, &errstr);
			if (errstr) {
				yyerror("%s: %s", $2, errstr);
				free($2);
				YYERROR;
			}
			free($2);
			$$.logif = i;
		}
		;

interface	: /* empty */			{ $$ = NULL; }
		| ON if_item_not		{ $$ = $2; }
		| ON '{' optnl if_list '}'	{ $$ = $4; }
		;

if_list		: if_item_not optnl		{ $$ = $1; }
		| if_list comma if_item_not optnl	{
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

if_item_not	: not if_item			{ $$ = $2; $$->not = $1; }
		;

if_item		: STRING			{
			struct node_host	*n;

			$$ = calloc(1, sizeof(struct node_if));
			if ($$ == NULL)
				err(1, "if_item: calloc");
			if (strlcpy($$->ifname, $1, sizeof($$->ifname)) >=
			    sizeof($$->ifname)) {
				free($1);
				free($$);
				yyerror("interface name too long");
				YYERROR;
			}

			if ((n = ifa_exists($1)) != NULL)
				$$->ifa_flags = n->ifa_flags;

			free($1);
			$$->not = 0;
			$$->next = NULL;
			$$->tail = $$;
		}
		;

af		: /* empty */			{ $$ = 0; }
		| INET				{ $$ = AF_INET; }
		| INET6				{ $$ = AF_INET6; }
		;

proto		: /* empty */				{ $$ = NULL; }
		| PROTO proto_item			{ $$ = $2; }
		| PROTO '{' optnl proto_list '}'	{ $$ = $4; }
		;

proto_list	: proto_item optnl		{ $$ = $1; }
		| proto_list comma proto_item optnl	{
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

proto_item	: protoval			{
			u_int8_t	pr;

			pr = (u_int8_t)$1;
			if (pr == 0) {
				yyerror("proto 0 cannot be used");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_proto));
			if ($$ == NULL)
				err(1, "proto_item: calloc");
			$$->proto = pr;
			$$->next = NULL;
			$$->tail = $$;
		}
		;

protoval	: STRING			{
			struct protoent	*p;

			p = getprotobyname($1);
			if (p == NULL) {
				yyerror("unknown protocol %s", $1);
				free($1);
				YYERROR;
			}
			$$ = p->p_proto;
			free($1);
		}
		| NUMBER			{
			if ($1 < 0 || $1 > 255) {
				yyerror("protocol outside range");
				YYERROR;
			}
		}
		;

fromto		: ALL				{
			$$.src.host = NULL;
			$$.src.port = NULL;
			$$.dst.host = NULL;
			$$.dst.port = NULL;
			$$.src_os = NULL;
		}
		| from os to			{
			$$.src = $1;
			$$.src_os = $2;
			$$.dst = $3;
		}
		;

os		: /* empty */			{ $$ = NULL; }
		| OS xos			{ $$ = $2; }
		| OS '{' optnl os_list '}'	{ $$ = $4; }
		;

xos		: STRING {
			$$ = calloc(1, sizeof(struct node_os));
			if ($$ == NULL)
				err(1, "os: calloc");
			$$->os = $1;
			$$->tail = $$;
		}
		;

os_list		: xos optnl 			{ $$ = $1; }
		| os_list comma xos optnl	{
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

from		: /* empty */			{
			$$.host = NULL;
			$$.port = NULL;
		}
		| FROM ipportspec		{
			$$ = $2;
		}
		;

to		: /* empty */			{
			$$.host = NULL;
			$$.port = NULL;
		}
		| TO ipportspec		{
			if (disallow_urpf_failed($2.host, "\"urpf-failed\" is "
			    "not permitted in a destination address"))
				YYERROR;
			$$ = $2;
		}
		;

ipportspec	: ipspec			{
			$$.host = $1;
			$$.port = NULL;
		}
		| ipspec PORT portspec		{
			$$.host = $1;
			$$.port = $3;
		}
		| PORT portspec			{
			$$.host = NULL;
			$$.port = $2;
		}
		;

optnl		: '\n' optnl
		|
		;

ipspec		: ANY				{ $$ = NULL; }
		| xhost				{ $$ = $1; }
		| '{' optnl host_list '}'	{ $$ = $3; }
		;

toipspec	: TO ipspec			{ $$ = $2; }
		| /* empty */			{ $$ = NULL; }
		;

host_list	: ipspec optnl			{ $$ = $1; }
		| host_list comma ipspec optnl	{
			if ($3 == NULL)
				$$ = $1;
			else if ($1 == NULL)
				$$ = $3;
			else {
				$1->tail->next = $3;
				$1->tail = $3->tail;
				$$ = $1;
			}
		}
		;

xhost		: not host			{
			struct node_host	*n;

			for (n = $2; n != NULL; n = n->next)
				n->not = $1;
			$$ = $2;
		}
		| not NOROUTE			{
			$$ = calloc(1, sizeof(struct node_host));
			if ($$ == NULL)
				err(1, "xhost: calloc");
			$$->addr.type = PF_ADDR_NOROUTE;
			$$->next = NULL;
			$$->not = $1;
			$$->tail = $$;
		}
		| not URPFFAILED		{
			$$ = calloc(1, sizeof(struct node_host));
			if ($$ == NULL)
				err(1, "xhost: calloc");
			$$->addr.type = PF_ADDR_URPFFAILED;
			$$->next = NULL;
			$$->not = $1;
			$$->tail = $$;
		}
		;

host		: STRING			{
			if (($$ = host($1)) == NULL)	{
				/* error. "any" is handled elsewhere */
				free($1);
				yyerror("could not parse host specification");
				YYERROR;
			}
			free($1);

		}
		| STRING '-' STRING		{
			struct node_host *b, *e;

			if ((b = host($1)) == NULL || (e = host($3)) == NULL) {
				free($1);
				free($3);
				yyerror("could not parse host specification");
				YYERROR;
			}
			if (b->af != e->af ||
			    b->addr.type != PF_ADDR_ADDRMASK ||
			    e->addr.type != PF_ADDR_ADDRMASK ||
			    unmask(&b->addr.v.a.mask, b->af) !=
			    (b->af == AF_INET ? 32 : 128) ||
			    unmask(&e->addr.v.a.mask, e->af) !=
			    (e->af == AF_INET ? 32 : 128) ||
			    b->next != NULL || b->not ||
			    e->next != NULL || e->not) {
				free(b);
				free(e);
				free($1);
				free($3);
				yyerror("invalid address range");
				YYERROR;
			}
			memcpy(&b->addr.v.a.mask, &e->addr.v.a.addr,
			    sizeof(b->addr.v.a.mask));
			b->addr.type = PF_ADDR_RANGE;
			$$ = b;
			free(e);
			free($1);
			free($3);
		}
		| STRING '/' NUMBER		{
			char	*buf;

			if (asprintf(&buf, "%s/%lld", $1, (long long)$3) == -1)
				err(1, "host: asprintf");
			free($1);
			if (($$ = host(buf)) == NULL)	{
				/* error. "any" is handled elsewhere */
				free(buf);
				yyerror("could not parse host specification");
				YYERROR;
			}
			free(buf);
		}
		| NUMBER '/' NUMBER		{
			char	*buf;

			/* ie. for 10/8 parsing */
#ifdef __FreeBSD__
			if (asprintf(&buf, "%lld/%lld", (long long)$1, (long long)$3) == -1)
#else
			if (asprintf(&buf, "%lld/%lld", $1, $3) == -1)
#endif
				err(1, "host: asprintf");
			if (($$ = host(buf)) == NULL)	{
				/* error. "any" is handled elsewhere */
				free(buf);
				yyerror("could not parse host specification");
				YYERROR;
			}
			free(buf);
		}
		| dynaddr
		| dynaddr '/' NUMBER		{
			struct node_host	*n;

			if ($3 < 0 || $3 > 128) {
				yyerror("bit number too big");
				YYERROR;
			}
			$$ = $1;
			for (n = $1; n != NULL; n = n->next)
				set_ipmask(n, $3);
		}
		| '<' STRING '>'	{
			if (strlen($2) >= PF_TABLE_NAME_SIZE) {
				yyerror("table name '%s' too long", $2);
				free($2);
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_host));
			if ($$ == NULL)
				err(1, "host: calloc");
			$$->addr.type = PF_ADDR_TABLE;
			if (strlcpy($$->addr.v.tblname, $2,
			    sizeof($$->addr.v.tblname)) >=
			    sizeof($$->addr.v.tblname))
				errx(1, "host: strlcpy");
			free($2);
			$$->next = NULL;
			$$->tail = $$;
		}
		;

number		: NUMBER
		| STRING		{
			u_long	ulval;

			if (atoul($1, &ulval) == -1) {
				yyerror("%s is not a number", $1);
				free($1);
				YYERROR;
			} else
				$$ = ulval;
			free($1);
		}
		;

dynaddr		: '(' STRING ')'		{
			int	 flags = 0;
			char	*p, *op;

			op = $2;
			if (!isalpha(op[0])) {
				yyerror("invalid interface name '%s'", op);
				free(op);
				YYERROR;
			}
			while ((p = strrchr($2, ':')) != NULL) {
				if (!strcmp(p+1, "network"))
					flags |= PFI_AFLAG_NETWORK;
				else if (!strcmp(p+1, "broadcast"))
					flags |= PFI_AFLAG_BROADCAST;
				else if (!strcmp(p+1, "peer"))
					flags |= PFI_AFLAG_PEER;
				else if (!strcmp(p+1, "0"))
					flags |= PFI_AFLAG_NOALIAS;
				else {
					yyerror("interface %s has bad modifier",
					    $2);
					free(op);
					YYERROR;
				}
				*p = '\0';
			}
			if (flags & (flags - 1) & PFI_AFLAG_MODEMASK) {
				free(op);
				yyerror("illegal combination of "
				    "interface modifiers");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_host));
			if ($$ == NULL)
				err(1, "address: calloc");
			$$->af = 0;
			set_ipmask($$, 128);
			$$->addr.type = PF_ADDR_DYNIFTL;
			$$->addr.iflags = flags;
			if (strlcpy($$->addr.v.ifname, $2,
			    sizeof($$->addr.v.ifname)) >=
			    sizeof($$->addr.v.ifname)) {
				free(op);
				free($$);
				yyerror("interface name too long");
				YYERROR;
			}
			free(op);
			$$->next = NULL;
			$$->tail = $$;
		}
		;

portspec	: port_item			{ $$ = $1; }
		| '{' optnl port_list '}'	{ $$ = $3; }
		;

port_list	: port_item optnl		{ $$ = $1; }
		| port_list comma port_item optnl	{
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

port_item	: portrange			{
			$$ = calloc(1, sizeof(struct node_port));
			if ($$ == NULL)
				err(1, "port_item: calloc");
			$$->port[0] = $1.a;
			$$->port[1] = $1.b;
			if ($1.t)
				$$->op = PF_OP_RRG;
			else
				$$->op = PF_OP_EQ;
			$$->next = NULL;
			$$->tail = $$;
		}
		| unaryop portrange	{
			if ($2.t) {
				yyerror("':' cannot be used with an other "
				    "port operator");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_port));
			if ($$ == NULL)
				err(1, "port_item: calloc");
			$$->port[0] = $2.a;
			$$->port[1] = $2.b;
			$$->op = $1;
			$$->next = NULL;
			$$->tail = $$;
		}
		| portrange PORTBINARY portrange	{
			if ($1.t || $3.t) {
				yyerror("':' cannot be used with an other "
				    "port operator");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_port));
			if ($$ == NULL)
				err(1, "port_item: calloc");
			$$->port[0] = $1.a;
			$$->port[1] = $3.a;
			$$->op = $2;
			$$->next = NULL;
			$$->tail = $$;
		}
		;

portplain	: numberstring			{
			if (parseport($1, &$$, 0) == -1) {
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

portrange	: numberstring			{
			if (parseport($1, &$$, PPORT_RANGE) == -1) {
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

uids		: uid_item			{ $$ = $1; }
		| '{' optnl uid_list '}'	{ $$ = $3; }
		;

uid_list	: uid_item optnl		{ $$ = $1; }
		| uid_list comma uid_item optnl	{
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

uid_item	: uid				{
			$$ = calloc(1, sizeof(struct node_uid));
			if ($$ == NULL)
				err(1, "uid_item: calloc");
			$$->uid[0] = $1;
			$$->uid[1] = $1;
			$$->op = PF_OP_EQ;
			$$->next = NULL;
			$$->tail = $$;
		}
		| unaryop uid			{
			if ($2 == UID_MAX && $1 != PF_OP_EQ && $1 != PF_OP_NE) {
				yyerror("user unknown requires operator = or "
				    "!=");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_uid));
			if ($$ == NULL)
				err(1, "uid_item: calloc");
			$$->uid[0] = $2;
			$$->uid[1] = $2;
			$$->op = $1;
			$$->next = NULL;
			$$->tail = $$;
		}
		| uid PORTBINARY uid		{
			if ($1 == UID_MAX || $3 == UID_MAX) {
				yyerror("user unknown requires operator = or "
				    "!=");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_uid));
			if ($$ == NULL)
				err(1, "uid_item: calloc");
			$$->uid[0] = $1;
			$$->uid[1] = $3;
			$$->op = $2;
			$$->next = NULL;
			$$->tail = $$;
		}
		;

uid		: STRING			{
			if (!strcmp($1, "unknown"))
				$$ = UID_MAX;
			else {
				struct passwd	*pw;

				if ((pw = getpwnam($1)) == NULL) {
					yyerror("unknown user %s", $1);
					free($1);
					YYERROR;
				}
				$$ = pw->pw_uid;
			}
			free($1);
		}
		| NUMBER			{
			if ($1 < 0 || $1 >= UID_MAX) {
				yyerror("illegal uid value %lu", $1);
				YYERROR;
			}
			$$ = $1;
		}
		;

gids		: gid_item			{ $$ = $1; }
		| '{' optnl gid_list '}'	{ $$ = $3; }
		;

gid_list	: gid_item optnl		{ $$ = $1; }
		| gid_list comma gid_item optnl	{
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

gid_item	: gid				{
			$$ = calloc(1, sizeof(struct node_gid));
			if ($$ == NULL)
				err(1, "gid_item: calloc");
			$$->gid[0] = $1;
			$$->gid[1] = $1;
			$$->op = PF_OP_EQ;
			$$->next = NULL;
			$$->tail = $$;
		}
		| unaryop gid			{
			if ($2 == GID_MAX && $1 != PF_OP_EQ && $1 != PF_OP_NE) {
				yyerror("group unknown requires operator = or "
				    "!=");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_gid));
			if ($$ == NULL)
				err(1, "gid_item: calloc");
			$$->gid[0] = $2;
			$$->gid[1] = $2;
			$$->op = $1;
			$$->next = NULL;
			$$->tail = $$;
		}
		| gid PORTBINARY gid		{
			if ($1 == GID_MAX || $3 == GID_MAX) {
				yyerror("group unknown requires operator = or "
				    "!=");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_gid));
			if ($$ == NULL)
				err(1, "gid_item: calloc");
			$$->gid[0] = $1;
			$$->gid[1] = $3;
			$$->op = $2;
			$$->next = NULL;
			$$->tail = $$;
		}
		;

gid		: STRING			{
			if (!strcmp($1, "unknown"))
				$$ = GID_MAX;
			else {
				struct group	*grp;

				if ((grp = getgrnam($1)) == NULL) {
					yyerror("unknown group %s", $1);
					free($1);
					YYERROR;
				}
				$$ = grp->gr_gid;
			}
			free($1);
		}
		| NUMBER			{
			if ($1 < 0 || $1 >= GID_MAX) {
				yyerror("illegal gid value %lu", $1);
				YYERROR;
			}
			$$ = $1;
		}
		;

flag		: STRING			{
			int	f;

			if ((f = parse_flags($1)) < 0) {
				yyerror("bad flags %s", $1);
				free($1);
				YYERROR;
			}
			free($1);
			$$.b1 = f;
		}
		;

flags		: FLAGS flag '/' flag	{ $$.b1 = $2.b1; $$.b2 = $4.b1; }
		| FLAGS '/' flag	{ $$.b1 = 0; $$.b2 = $3.b1; }
		| FLAGS ANY		{ $$.b1 = 0; $$.b2 = 0; }
		;

icmpspec	: ICMPTYPE icmp_item			{ $$ = $2; }
		| ICMPTYPE '{' optnl icmp_list '}'	{ $$ = $4; }
		| ICMP6TYPE icmp6_item			{ $$ = $2; }
		| ICMP6TYPE '{' optnl icmp6_list '}'	{ $$ = $4; }
		;

icmp_list	: icmp_item optnl		{ $$ = $1; }
		| icmp_list comma icmp_item optnl {
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

icmp6_list	: icmp6_item optnl		{ $$ = $1; }
		| icmp6_list comma icmp6_item optnl {
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

icmp_item	: icmptype		{
			$$ = calloc(1, sizeof(struct node_icmp));
			if ($$ == NULL)
				err(1, "icmp_item: calloc");
			$$->type = $1;
			$$->code = 0;
			$$->proto = IPPROTO_ICMP;
			$$->next = NULL;
			$$->tail = $$;
		}
		| icmptype CODE STRING	{
			const struct icmpcodeent	*p;

			if ((p = geticmpcodebyname($1-1, $3, AF_INET)) == NULL) {
				yyerror("unknown icmp-code %s", $3);
				free($3);
				YYERROR;
			}

			free($3);
			$$ = calloc(1, sizeof(struct node_icmp));
			if ($$ == NULL)
				err(1, "icmp_item: calloc");
			$$->type = $1;
			$$->code = p->code + 1;
			$$->proto = IPPROTO_ICMP;
			$$->next = NULL;
			$$->tail = $$;
		}
		| icmptype CODE NUMBER	{
			if ($3 < 0 || $3 > 255) {
				yyerror("illegal icmp-code %lu", $3);
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_icmp));
			if ($$ == NULL)
				err(1, "icmp_item: calloc");
			$$->type = $1;
			$$->code = $3 + 1;
			$$->proto = IPPROTO_ICMP;
			$$->next = NULL;
			$$->tail = $$;
		}
		;

icmp6_item	: icmp6type		{
			$$ = calloc(1, sizeof(struct node_icmp));
			if ($$ == NULL)
				err(1, "icmp_item: calloc");
			$$->type = $1;
			$$->code = 0;
			$$->proto = IPPROTO_ICMPV6;
			$$->next = NULL;
			$$->tail = $$;
		}
		| icmp6type CODE STRING	{
			const struct icmpcodeent	*p;

			if ((p = geticmpcodebyname($1-1, $3, AF_INET6)) == NULL) {
				yyerror("unknown icmp6-code %s", $3);
				free($3);
				YYERROR;
			}
			free($3);

			$$ = calloc(1, sizeof(struct node_icmp));
			if ($$ == NULL)
				err(1, "icmp_item: calloc");
			$$->type = $1;
			$$->code = p->code + 1;
			$$->proto = IPPROTO_ICMPV6;
			$$->next = NULL;
			$$->tail = $$;
		}
		| icmp6type CODE NUMBER	{
			if ($3 < 0 || $3 > 255) {
				yyerror("illegal icmp-code %lu", $3);
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_icmp));
			if ($$ == NULL)
				err(1, "icmp_item: calloc");
			$$->type = $1;
			$$->code = $3 + 1;
			$$->proto = IPPROTO_ICMPV6;
			$$->next = NULL;
			$$->tail = $$;
		}
		;

icmptype	: STRING			{
			const struct icmptypeent	*p;

			if ((p = geticmptypebyname($1, AF_INET)) == NULL) {
				yyerror("unknown icmp-type %s", $1);
				free($1);
				YYERROR;
			}
			$$ = p->type + 1;
			free($1);
		}
		| NUMBER			{
			if ($1 < 0 || $1 > 255) {
				yyerror("illegal icmp-type %lu", $1);
				YYERROR;
			}
			$$ = $1 + 1;
		}
		;

icmp6type	: STRING			{
			const struct icmptypeent	*p;

			if ((p = geticmptypebyname($1, AF_INET6)) ==
			    NULL) {
				yyerror("unknown icmp6-type %s", $1);
				free($1);
				YYERROR;
			}
			$$ = p->type + 1;
			free($1);
		}
		| NUMBER			{
			if ($1 < 0 || $1 > 255) {
				yyerror("illegal icmp6-type %lu", $1);
				YYERROR;
			}
			$$ = $1 + 1;
		}
		;

tos	: STRING			{
			int val;
			char *end;

			if (map_tos($1, &val))
				$$ = val;
			else if ($1[0] == '0' && $1[1] == 'x') {
				errno = 0;
				$$ = strtoul($1, &end, 16);
				if (errno || *end != '\0')
					$$ = 256;
			} else
				$$ = 256;		/* flag bad argument */
			if ($$ < 0 || $$ > 255) {
				yyerror("illegal tos value %s", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		| NUMBER			{
			$$ = $1;
			if ($$ < 0 || $$ > 255) {
				yyerror("illegal tos value %s", $1);
				YYERROR;
			}
		}
		;

sourcetrack	: SOURCETRACK		{ $$ = PF_SRCTRACK; }
		| SOURCETRACK GLOBAL	{ $$ = PF_SRCTRACK_GLOBAL; }
		| SOURCETRACK RULE	{ $$ = PF_SRCTRACK_RULE; }
		;

statelock	: IFBOUND {
			$$ = PFRULE_IFBOUND;
		}
		| FLOATING {
			$$ = 0;
		}
		;

keep		: NO STATE			{
			$$.action = 0;
			$$.options = NULL;
		}
		| KEEP STATE state_opt_spec	{
			$$.action = PF_STATE_NORMAL;
			$$.options = $3;
		}
		| MODULATE STATE state_opt_spec {
			$$.action = PF_STATE_MODULATE;
			$$.options = $3;
		}
		| SYNPROXY STATE state_opt_spec {
			$$.action = PF_STATE_SYNPROXY;
			$$.options = $3;
		}
		;

flush		: /* empty */			{ $$ = 0; }
		| FLUSH				{ $$ = PF_FLUSH; }
		| FLUSH GLOBAL			{
			$$ = PF_FLUSH | PF_FLUSH_GLOBAL;
		}
		;

state_opt_spec	: '(' state_opt_list ')'	{ $$ = $2; }
		| /* empty */			{ $$ = NULL; }
		;

state_opt_list	: state_opt_item		{ $$ = $1; }
		| state_opt_list comma state_opt_item {
			$1->tail->next = $3;
			$1->tail = $3;
			$$ = $1;
		}
		;

state_opt_item	: MAXIMUM NUMBER		{
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_MAX;
			$$->data.max_states = $2;
			$$->next = NULL;
			$$->tail = $$;
		}
		| NOSYNC				{
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_NOSYNC;
			$$->next = NULL;
			$$->tail = $$;
		}
		| MAXSRCSTATES NUMBER			{
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_MAX_SRC_STATES;
			$$->data.max_src_states = $2;
			$$->next = NULL;
			$$->tail = $$;
		}
		| MAXSRCCONN NUMBER			{
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_MAX_SRC_CONN;
			$$->data.max_src_conn = $2;
			$$->next = NULL;
			$$->tail = $$;
		}
		| MAXSRCCONNRATE NUMBER '/' NUMBER	{
			if ($2 < 0 || $2 > UINT_MAX ||
			    $4 < 0 || $4 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_MAX_SRC_CONN_RATE;
			$$->data.max_src_conn_rate.limit = $2;
			$$->data.max_src_conn_rate.seconds = $4;
			$$->next = NULL;
			$$->tail = $$;
		}
		| OVERLOAD '<' STRING '>' flush		{
			if (strlen($3) >= PF_TABLE_NAME_SIZE) {
				yyerror("table name '%s' too long", $3);
				free($3);
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			if (strlcpy($$->data.overload.tblname, $3,
			    PF_TABLE_NAME_SIZE) >= PF_TABLE_NAME_SIZE)
				errx(1, "state_opt_item: strlcpy");
			free($3);
			$$->type = PF_STATE_OPT_OVERLOAD;
			$$->data.overload.flush = $5;
			$$->next = NULL;
			$$->tail = $$;
		}
		| MAXSRCNODES NUMBER			{
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_MAX_SRC_NODES;
			$$->data.max_src_nodes = $2;
			$$->next = NULL;
			$$->tail = $$;
		}
		| sourcetrack {
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_SRCTRACK;
			$$->data.src_track = $1;
			$$->next = NULL;
			$$->tail = $$;
		}
		| statelock {
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_STATELOCK;
			$$->data.statelock = $1;
			$$->next = NULL;
			$$->tail = $$;
		}
		| SLOPPY {
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_SLOPPY;
			$$->next = NULL;
			$$->tail = $$;
		}
		| STRING NUMBER			{
			int	i;

			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			for (i = 0; pf_timeouts[i].name &&
			    strcmp(pf_timeouts[i].name, $1); ++i)
				;	/* nothing */
			if (!pf_timeouts[i].name) {
				yyerror("illegal timeout name %s", $1);
				free($1);
				YYERROR;
			}
			if (strchr(pf_timeouts[i].name, '.') == NULL) {
				yyerror("illegal state timeout %s", $1);
				free($1);
				YYERROR;
			}
			free($1);
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_TIMEOUT;
			$$->data.timeout.number = pf_timeouts[i].timeout;
			$$->data.timeout.seconds = $2;
			$$->next = NULL;
			$$->tail = $$;
		}
		;

label		: LABEL STRING			{
			$$ = $2;
		}
		;

qname		: QUEUE STRING				{
			$$.qname = $2;
			$$.pqname = NULL;
		}
		| QUEUE '(' STRING ')'			{
			$$.qname = $3;
			$$.pqname = NULL;
		}
		| QUEUE '(' STRING comma STRING ')'	{
			$$.qname = $3;
			$$.pqname = $5;
		}
		;

no		: /* empty */			{ $$ = 0; }
		| NO				{ $$ = 1; }
		;

portstar	: numberstring			{
			if (parseport($1, &$$, PPORT_RANGE|PPORT_STAR) == -1) {
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

redirspec	: host				{ $$ = $1; }
		| '{' optnl redir_host_list '}'	{ $$ = $3; }
		;

redir_host_list	: host optnl			{ $$ = $1; }
		| redir_host_list comma host optnl {
			$1->tail->next = $3;
			$1->tail = $3->tail;
			$$ = $1;
		}
		;

redirpool	: /* empty */			{ $$ = NULL; }
		| ARROW redirspec		{
			$$ = calloc(1, sizeof(struct redirection));
			if ($$ == NULL)
				err(1, "redirection: calloc");
			$$->host = $2;
			$$->rport.a = $$->rport.b = $$->rport.t = 0;
		}
		| ARROW redirspec PORT portstar	{
			$$ = calloc(1, sizeof(struct redirection));
			if ($$ == NULL)
				err(1, "redirection: calloc");
			$$->host = $2;
			$$->rport = $4;
		}
		;

hashkey		: /* empty */
		{
			$$ = calloc(1, sizeof(struct pf_poolhashkey));
			if ($$ == NULL)
				err(1, "hashkey: calloc");
			$$->key32[0] = arc4random();
			$$->key32[1] = arc4random();
			$$->key32[2] = arc4random();
			$$->key32[3] = arc4random();
		}
		| string
		{
			if (!strncmp($1, "0x", 2)) {
				if (strlen($1) != 34) {
					free($1);
					yyerror("hex key must be 128 bits "
						"(32 hex digits) long");
					YYERROR;
				}
				$$ = calloc(1, sizeof(struct pf_poolhashkey));
				if ($$ == NULL)
					err(1, "hashkey: calloc");

				if (sscanf($1, "0x%8x%8x%8x%8x",
				    &$$->key32[0], &$$->key32[1],
				    &$$->key32[2], &$$->key32[3]) != 4) {
					free($$);
					free($1);
					yyerror("invalid hex key");
					YYERROR;
				}
			} else {
				MD5_CTX	context;

				$$ = calloc(1, sizeof(struct pf_poolhashkey));
				if ($$ == NULL)
					err(1, "hashkey: calloc");
				MD5Init(&context);
				MD5Update(&context, (unsigned char *)$1,
				    strlen($1));
				MD5Final((unsigned char *)$$, &context);
				HTONL($$->key32[0]);
				HTONL($$->key32[1]);
				HTONL($$->key32[2]);
				HTONL($$->key32[3]);
			}
			free($1);
		}
		;

pool_opts	:	{ bzero(&pool_opts, sizeof pool_opts); }
		    pool_opts_l
			{ $$ = pool_opts; }
		| /* empty */	{
			bzero(&pool_opts, sizeof pool_opts);
			$$ = pool_opts;
		}
		;

pool_opts_l	: pool_opts_l pool_opt
		| pool_opt
		;

pool_opt	: BITMASK	{
			if (pool_opts.type) {
				yyerror("pool type cannot be redefined");
				YYERROR;
			}
			pool_opts.type =  PF_POOL_BITMASK;
		}
		| RANDOM	{
			if (pool_opts.type) {
				yyerror("pool type cannot be redefined");
				YYERROR;
			}
			pool_opts.type = PF_POOL_RANDOM;
		}
		| SOURCEHASH hashkey {
			if (pool_opts.type) {
				yyerror("pool type cannot be redefined");
				YYERROR;
			}
			pool_opts.type = PF_POOL_SRCHASH;
			pool_opts.key = $2;
		}
		| ROUNDROBIN	{
			if (pool_opts.type) {
				yyerror("pool type cannot be redefined");
				YYERROR;
			}
			pool_opts.type = PF_POOL_ROUNDROBIN;
		}
		| STATICPORT	{
			if (pool_opts.staticport) {
				yyerror("static-port cannot be redefined");
				YYERROR;
			}
			pool_opts.staticport = 1;
		}
		| STICKYADDRESS	{
			if (filter_opts.marker & POM_STICKYADDRESS) {
				yyerror("sticky-address cannot be redefined");
				YYERROR;
			}
			pool_opts.marker |= POM_STICKYADDRESS;
			pool_opts.opts |= PF_POOL_STICKYADDR;
		}
		;

redirection	: /* empty */			{ $$ = NULL; }
		| ARROW host			{
			$$ = calloc(1, sizeof(struct redirection));
			if ($$ == NULL)
				err(1, "redirection: calloc");
			$$->host = $2;
			$$->rport.a = $$->rport.b = $$->rport.t = 0;
		}
		| ARROW host PORT portstar	{
			$$ = calloc(1, sizeof(struct redirection));
			if ($$ == NULL)
				err(1, "redirection: calloc");
			$$->host = $2;
			$$->rport = $4;
		}
		;

natpasslog	: /* empty */	{ $$.b1 = $$.b2 = 0; $$.w2 = 0; }
		| PASS		{ $$.b1 = 1; $$.b2 = 0; $$.w2 = 0; }
		| PASS log	{ $$.b1 = 1; $$.b2 = $2.log; $$.w2 = $2.logif; }
		| log		{ $$.b1 = 0; $$.b2 = $1.log; $$.w2 = $1.logif; }
		;

nataction	: no NAT natpasslog {
			if ($1 && $3.b1) {
				yyerror("\"pass\" not valid with \"no\"");
				YYERROR;
			}
			if ($1)
				$$.b1 = PF_NONAT;
			else
				$$.b1 = PF_NAT;
			$$.b2 = $3.b1;
			$$.w = $3.b2;
			$$.w2 = $3.w2;
		}
		| no RDR natpasslog {
			if ($1 && $3.b1) {
				yyerror("\"pass\" not valid with \"no\"");
				YYERROR;
			}
			if ($1)
				$$.b1 = PF_NORDR;
			else
				$$.b1 = PF_RDR;
			$$.b2 = $3.b1;
			$$.w = $3.b2;
			$$.w2 = $3.w2;
		}
		;

natrule		: nataction interface af proto fromto tag tagged rtable
		    redirpool pool_opts
		{
			struct pf_rule	r;

			if (check_rulestate(PFCTL_STATE_NAT))
				YYERROR;

			memset(&r, 0, sizeof(r));

			r.action = $1.b1;
			r.natpass = $1.b2;
			r.log = $1.w;
			r.logif = $1.w2;
			r.af = $3;

			if (!r.af) {
				if ($5.src.host && $5.src.host->af &&
				    !$5.src.host->ifindex)
					r.af = $5.src.host->af;
				else if ($5.dst.host && $5.dst.host->af &&
				    !$5.dst.host->ifindex)
					r.af = $5.dst.host->af;
			}

			if ($6 != NULL)
				if (strlcpy(r.tagname, $6, PF_TAG_NAME_SIZE) >=
				    PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}

			if ($7.name)
				if (strlcpy(r.match_tagname, $7.name,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			r.match_tag_not = $7.neg;
			r.rtableid = $8;

			if (r.action == PF_NONAT || r.action == PF_NORDR) {
				if ($9 != NULL) {
					yyerror("translation rule with 'no' "
					    "does not need '->'");
					YYERROR;
				}
			} else {
				if ($9 == NULL || $9->host == NULL) {
					yyerror("translation rule requires '-> "
					    "address'");
					YYERROR;
				}
				if (!r.af && ! $9->host->ifindex)
					r.af = $9->host->af;

				remove_invalid_hosts(&$9->host, &r.af);
				if (invalid_redirect($9->host, r.af))
					YYERROR;
				if (check_netmask($9->host, r.af))
					YYERROR;

				r.rpool.proxy_port[0] = ntohs($9->rport.a);

				switch (r.action) {
				case PF_RDR:
					if (!$9->rport.b && $9->rport.t &&
					    $5.dst.port != NULL) {
						r.rpool.proxy_port[1] =
						    ntohs($9->rport.a) +
						    (ntohs(
						    $5.dst.port->port[1]) -
						    ntohs(
						    $5.dst.port->port[0]));
					} else
						r.rpool.proxy_port[1] =
						    ntohs($9->rport.b);
					break;
				case PF_NAT:
					r.rpool.proxy_port[1] =
					    ntohs($9->rport.b);
					if (!r.rpool.proxy_port[0] &&
					    !r.rpool.proxy_port[1]) {
						r.rpool.proxy_port[0] =
						    PF_NAT_PROXY_PORT_LOW;
						r.rpool.proxy_port[1] =
						    PF_NAT_PROXY_PORT_HIGH;
					} else if (!r.rpool.proxy_port[1])
						r.rpool.proxy_port[1] =
						    r.rpool.proxy_port[0];
					break;
				default:
					break;
				}

				r.rpool.opts = $10.type;
				if ((r.rpool.opts & PF_POOL_TYPEMASK) ==
				    PF_POOL_NONE && ($9->host->next != NULL ||
				    $9->host->addr.type == PF_ADDR_TABLE ||
				    DYNIF_MULTIADDR($9->host->addr)))
					r.rpool.opts = PF_POOL_ROUNDROBIN;
				if ((r.rpool.opts & PF_POOL_TYPEMASK) !=
				    PF_POOL_ROUNDROBIN &&
				    disallow_table($9->host, "tables are only "
				    "supported in round-robin redirection "
				    "pools"))
					YYERROR;
				if ((r.rpool.opts & PF_POOL_TYPEMASK) !=
				    PF_POOL_ROUNDROBIN &&
				    disallow_alias($9->host, "interface (%s) "
				    "is only supported in round-robin "
				    "redirection pools"))
					YYERROR;
				if ($9->host->next != NULL) {
					if ((r.rpool.opts & PF_POOL_TYPEMASK) !=
					    PF_POOL_ROUNDROBIN) {
						yyerror("only round-robin "
						    "valid for multiple "
						    "redirection addresses");
						YYERROR;
					}
				}
			}

			if ($10.key != NULL)
				memcpy(&r.rpool.key, $10.key,
				    sizeof(struct pf_poolhashkey));

			 if ($10.opts)
				r.rpool.opts |= $10.opts;

			if ($10.staticport) {
				if (r.action != PF_NAT) {
					yyerror("the 'static-port' option is "
					    "only valid with nat rules");
					YYERROR;
				}
				if (r.rpool.proxy_port[0] !=
				    PF_NAT_PROXY_PORT_LOW &&
				    r.rpool.proxy_port[1] !=
				    PF_NAT_PROXY_PORT_HIGH) {
					yyerror("the 'static-port' option can't"
					    " be used when specifying a port"
					    " range");
					YYERROR;
				}
				r.rpool.proxy_port[0] = 0;
				r.rpool.proxy_port[1] = 0;
			}

			expand_rule(&r, $2, $9 == NULL ? NULL : $9->host, $4,
			    $5.src_os, $5.src.host, $5.src.port, $5.dst.host,
			    $5.dst.port, 0, 0, 0, "");
			free($9);
		}
		;

binatrule	: no BINAT natpasslog interface af proto FROM ipspec toipspec tag
		    tagged rtable redirection
		{
			struct pf_rule		binat;
			struct pf_pooladdr	*pa;

			if (check_rulestate(PFCTL_STATE_NAT))
				YYERROR;
			if (disallow_urpf_failed($9, "\"urpf-failed\" is not "
			    "permitted as a binat destination"))
				YYERROR;

			memset(&binat, 0, sizeof(binat));

			if ($1 && $3.b1) {
				yyerror("\"pass\" not valid with \"no\"");
				YYERROR;
			}
			if ($1)
				binat.action = PF_NOBINAT;
			else
				binat.action = PF_BINAT;
			binat.natpass = $3.b1;
			binat.log = $3.b2;
			binat.logif = $3.w2;
			binat.af = $5;
			if (!binat.af && $8 != NULL && $8->af)
				binat.af = $8->af;
			if (!binat.af && $9 != NULL && $9->af)
				binat.af = $9->af;

			if (!binat.af && $13 != NULL && $13->host)
				binat.af = $13->host->af;
			if (!binat.af) {
				yyerror("address family (inet/inet6) "
				    "undefined");
				YYERROR;
			}

			if ($4 != NULL) {
				memcpy(binat.ifname, $4->ifname,
				    sizeof(binat.ifname));
				binat.ifnot = $4->not;
				free($4);
			}

			if ($10 != NULL)
				if (strlcpy(binat.tagname, $10,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			if ($11.name)
				if (strlcpy(binat.match_tagname, $11.name,
				    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
					yyerror("tag too long, max %u chars",
					    PF_TAG_NAME_SIZE - 1);
					YYERROR;
				}
			binat.match_tag_not = $11.neg;
			binat.rtableid = $12;

			if ($6 != NULL) {
				binat.proto = $6->proto;
				free($6);
			}

			if ($8 != NULL && disallow_table($8, "invalid use of "
			    "table <%s> as the source address of a binat rule"))
				YYERROR;
			if ($8 != NULL && disallow_alias($8, "invalid use of "
			    "interface (%s) as the source address of a binat "
			    "rule"))
				YYERROR;
			if ($13 != NULL && $13->host != NULL && disallow_table(
			    $13->host, "invalid use of table <%s> as the "
			    "redirect address of a binat rule"))
				YYERROR;
			if ($13 != NULL && $13->host != NULL && disallow_alias(
			    $13->host, "invalid use of interface (%s) as the "
			    "redirect address of a binat rule"))
				YYERROR;

			if ($8 != NULL) {
				if ($8->next) {
					yyerror("multiple binat ip addresses");
					YYERROR;
				}
				if ($8->addr.type == PF_ADDR_DYNIFTL)
					$8->af = binat.af;
				if ($8->af != binat.af) {
					yyerror("binat ip versions must match");
					YYERROR;
				}
				if (check_netmask($8, binat.af))
					YYERROR;
				memcpy(&binat.src.addr, &$8->addr,
				    sizeof(binat.src.addr));
				free($8);
			}
			if ($9 != NULL) {
				if ($9->next) {
					yyerror("multiple binat ip addresses");
					YYERROR;
				}
				if ($9->af != binat.af && $9->af) {
					yyerror("binat ip versions must match");
					YYERROR;
				}
				if (check_netmask($9, binat.af))
					YYERROR;
				memcpy(&binat.dst.addr, &$9->addr,
				    sizeof(binat.dst.addr));
				binat.dst.neg = $9->not;
				free($9);
			}

			if (binat.action == PF_NOBINAT) {
				if ($13 != NULL) {
					yyerror("'no binat' rule does not need"
					    " '->'");
					YYERROR;
				}
			} else {
				if ($13 == NULL || $13->host == NULL) {
					yyerror("'binat' rule requires"
					    " '-> address'");
					YYERROR;
				}

				remove_invalid_hosts(&$13->host, &binat.af);
				if (invalid_redirect($13->host, binat.af))
					YYERROR;
				if ($13->host->next != NULL) {
					yyerror("binat rule must redirect to "
					    "a single address");
					YYERROR;
				}
				if (check_netmask($13->host, binat.af))
					YYERROR;

				if (!PF_AZERO(&binat.src.addr.v.a.mask,
				    binat.af) &&
				    !PF_AEQ(&binat.src.addr.v.a.mask,
				    &$13->host->addr.v.a.mask, binat.af)) {
					yyerror("'binat' source mask and "
					    "redirect mask must be the same");
					YYERROR;
				}

				TAILQ_INIT(&binat.rpool.list);
				pa = calloc(1, sizeof(struct pf_pooladdr));
				if (pa == NULL)
					err(1, "binat: calloc");
				pa->addr = $13->host->addr;
				pa->ifname[0] = 0;
				TAILQ_INSERT_TAIL(&binat.rpool.list,
				    pa, entries);

				free($13);
			}

			pfctl_add_rule(pf, &binat, "");
		}
		;

tag		: /* empty */		{ $$ = NULL; }
		| TAG STRING		{ $$ = $2; }
		;

tagged		: /* empty */		{ $$.neg = 0; $$.name = NULL; }
		| not TAGGED string	{ $$.neg = $1; $$.name = $3; }
		;

rtable		: /* empty */		{ $$ = -1; }
		| RTABLE NUMBER		{
			if ($2 < 0 || $2 > rt_tableid_max()) {
				yyerror("invalid rtable id");
				YYERROR;
			}
			$$ = $2;
		}
		;

route_host	: STRING			{
			$$ = calloc(1, sizeof(struct node_host));
			if ($$ == NULL)
				err(1, "route_host: calloc");
			$$->ifname = strdup($1);
			set_ipmask($$, 128);
			$$->next = NULL;
			$$->tail = $$;
		}
		| '(' STRING host ')'		{
			struct node_host *n;

			$$ = $3;
			for (n = $3; n != NULL; n = n->next)
				n->ifname = strdup($2);
		}
		;

route_host_list	: route_host optnl			{ $$ = $1; }
		| route_host_list comma route_host optnl {
			if ($1->af == 0)
				$1->af = $3->af;
			if ($1->af != $3->af) {
				yyerror("all pool addresses must be in the "
				    "same address family");
				YYERROR;
			}
			$1->tail->next = $3;
			$1->tail = $3->tail;
			$$ = $1;
		}
		;

routespec	: route_host			{ $$ = $1; }
		| '{' optnl route_host_list '}'	{ $$ = $3; }
		;

route		: /* empty */			{
			$$.host = NULL;
			$$.rt = 0;
			$$.pool_opts = 0;
		}
		| FASTROUTE {
			/* backwards-compat */
			$$.host = NULL;
			$$.rt = 0;
			$$.pool_opts = 0;
		}
		| ROUTETO routespec pool_opts {
			$$.host = $2;
			$$.rt = PF_ROUTETO;
			$$.pool_opts = $3.type | $3.opts;
			if ($3.key != NULL)
				$$.key = $3.key;
		}
		| REPLYTO routespec pool_opts {
			$$.host = $2;
			$$.rt = PF_REPLYTO;
			$$.pool_opts = $3.type | $3.opts;
			if ($3.key != NULL)
				$$.key = $3.key;
		}
		| DUPTO routespec pool_opts {
			$$.host = $2;
			$$.rt = PF_DUPTO;
			$$.pool_opts = $3.type | $3.opts;
			if ($3.key != NULL)
				$$.key = $3.key;
		}
		;

timeout_spec	: STRING NUMBER
		{
			if (check_rulestate(PFCTL_STATE_OPTION)) {
				free($1);
				YYERROR;
			}
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			if (pfctl_set_timeout(pf, $1, $2, 0) != 0) {
				yyerror("unknown timeout %s", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		| INTERVAL NUMBER		{
			if (check_rulestate(PFCTL_STATE_OPTION))
				YYERROR;
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			if (pfctl_set_timeout(pf, "interval", $2, 0) != 0)
				YYERROR;
		}
		;

timeout_list	: timeout_list comma timeout_spec optnl
		| timeout_spec optnl
		;

limit_spec	: STRING NUMBER
		{
			if (check_rulestate(PFCTL_STATE_OPTION)) {
				free($1);
				YYERROR;
			}
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			if (pfctl_set_limit(pf, $1, $2) != 0) {
				yyerror("unable to set limit %s %u", $1, $2);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

limit_list	: limit_list comma limit_spec optnl
		| limit_spec optnl
		;

comma		: ','
		| /* empty */
		;

yesno		: NO			{ $$ = 0; }
		| STRING		{
			if (!strcmp($1, "yes"))
				$$ = 1;
			else {
				yyerror("invalid value '%s', expected 'yes' "
				    "or 'no'", $1);
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

unaryop		: '='		{ $$ = PF_OP_EQ; }
		| '!' '='	{ $$ = PF_OP_NE; }
		| '<' '='	{ $$ = PF_OP_LE; }
		| '<'		{ $$ = PF_OP_LT; }
		| '>' '='	{ $$ = PF_OP_GE; }
		| '>'		{ $$ = PF_OP_GT; }
		;

%%

int
yyerror(const char *fmt, ...)
{
	va_list		 ap;

	file->errors++;
	va_start(ap, fmt);
	fprintf(stderr, "%s:%d: ", file->name, yylval.lineno);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	return (0);
}

int
disallow_table(struct node_host *h, const char *fmt)
{
	for (; h != NULL; h = h->next)
		if (h->addr.type == PF_ADDR_TABLE) {
			yyerror(fmt, h->addr.v.tblname);
			return (1);
		}
	return (0);
}

int
disallow_urpf_failed(struct node_host *h, const char *fmt)
{
	for (; h != NULL; h = h->next)
		if (h->addr.type == PF_ADDR_URPFFAILED) {
			yyerror(fmt);
			return (1);
		}
	return (0);
}

int
disallow_alias(struct node_host *h, const char *fmt)
{
	for (; h != NULL; h = h->next)
		if (DYNIF_MULTIADDR(h->addr)) {
			yyerror(fmt, h->addr.v.tblname);
			return (1);
		}
	return (0);
}

int
rule_consistent(struct pf_rule *r, int anchor_call)
{
	int	problems = 0;

	switch (r->action) {
	case PF_PASS:
	case PF_DROP:
	case PF_SCRUB:
	case PF_NOSCRUB:
		problems = filter_consistent(r, anchor_call);
		break;
	case PF_NAT:
	case PF_NONAT:
		problems = nat_consistent(r);
		break;
	case PF_RDR:
	case PF_NORDR:
		problems = rdr_consistent(r);
		break;
	case PF_BINAT:
	case PF_NOBINAT:
	default:
		break;
	}
	return (problems);
}

int
filter_consistent(struct pf_rule *r, int anchor_call)
{
	int	problems = 0;

	if (r->proto != IPPROTO_TCP && r->proto != IPPROTO_UDP &&
	    (r->src.port_op || r->dst.port_op)) {
		yyerror("port only applies to tcp/udp");
		problems++;
	}
	if (r->proto != IPPROTO_ICMP && r->proto != IPPROTO_ICMPV6 &&
	    (r->type || r->code)) {
		yyerror("icmp-type/code only applies to icmp");
		problems++;
	}
	if (!r->af && (r->type || r->code)) {
		yyerror("must indicate address family with icmp-type/code");
		problems++;
	}
	if (r->overload_tblname[0] &&
	    r->max_src_conn == 0 && r->max_src_conn_rate.seconds == 0) {
		yyerror("'overload' requires 'max-src-conn' "
		    "or 'max-src-conn-rate'");
		problems++;
	}
	if ((r->proto == IPPROTO_ICMP && r->af == AF_INET6) ||
	    (r->proto == IPPROTO_ICMPV6 && r->af == AF_INET)) {
		yyerror("proto %s doesn't match address family %s",
		    r->proto == IPPROTO_ICMP ? "icmp" : "icmp6",
		    r->af == AF_INET ? "inet" : "inet6");
		problems++;
	}
	if (r->allow_opts && r->action != PF_PASS) {
		yyerror("allow-opts can only be specified for pass rules");
		problems++;
	}
	if (r->rule_flag & PFRULE_FRAGMENT && (r->src.port_op ||
	    r->dst.port_op || r->flagset || r->type || r->code)) {
		yyerror("fragments can be filtered only on IP header fields");
		problems++;
	}
	if (r->rule_flag & PFRULE_RETURNRST && r->proto != IPPROTO_TCP) {
		yyerror("return-rst can only be applied to TCP rules");
		problems++;
	}
	if (r->max_src_nodes && !(r->rule_flag & PFRULE_RULESRCTRACK)) {
		yyerror("max-src-nodes requires 'source-track rule'");
		problems++;
	}
	if (r->action == PF_DROP && r->keep_state) {
		yyerror("keep state on block rules doesn't make sense");
		problems++;
	}
	if (r->rule_flag & PFRULE_STATESLOPPY &&
	    (r->keep_state == PF_STATE_MODULATE ||
	    r->keep_state == PF_STATE_SYNPROXY)) {
		yyerror("sloppy state matching cannot be used with "
		    "synproxy state or modulate state");
		problems++;
	}
	return (-problems);
}

int
nat_consistent(struct pf_rule *r)
{
	return (0);	/* yeah! */
}

int
rdr_consistent(struct pf_rule *r)
{
	int			 problems = 0;

	if (r->proto != IPPROTO_TCP && r->proto != IPPROTO_UDP) {
		if (r->src.port_op) {
			yyerror("src port only applies to tcp/udp");
			problems++;
		}
		if (r->dst.port_op) {
			yyerror("dst port only applies to tcp/udp");
			problems++;
		}
		if (r->rpool.proxy_port[0]) {
			yyerror("rpool port only applies to tcp/udp");
			problems++;
		}
	}
	if (r->dst.port_op &&
	    r->dst.port_op != PF_OP_EQ && r->dst.port_op != PF_OP_RRG) {
		yyerror("invalid port operator for rdr destination port");
		problems++;
	}
	return (-problems);
}

int
process_tabledef(char *name, struct table_opts *opts)
{
	struct pfr_buffer	 ab;
	struct node_tinit	*ti;
	unsigned long		 maxcount;
	size_t			 s = sizeof(maxcount);

	bzero(&ab, sizeof(ab));
	ab.pfrb_type = PFRB_ADDRS;
	SIMPLEQ_FOREACH(ti, &opts->init_nodes, entries) {
		if (ti->file)
			if (pfr_buf_load(&ab, ti->file, 0, append_addr)) {
				if (errno)
					yyerror("cannot load \"%s\": %s",
					    ti->file, strerror(errno));
				else
					yyerror("file \"%s\" contains bad data",
					    ti->file);
				goto _error;
			}
		if (ti->host)
			if (append_addr_host(&ab, ti->host, 0, 0)) {
				yyerror("cannot create address buffer: %s",
				    strerror(errno));
				goto _error;
			}
	}
	if (pf->opts & PF_OPT_VERBOSE)
		print_tabledef(name, opts->flags, opts->init_addr,
		    &opts->init_nodes);
	if (!(pf->opts & PF_OPT_NOACTION) &&
	    pfctl_define_table(name, opts->flags, opts->init_addr,
	    pf->anchor->name, &ab, pf->anchor->ruleset.tticket)) {

		if (sysctlbyname("net.pf.request_maxcount", &maxcount, &s,
		    NULL, 0) == -1)
			maxcount = 65535;

		if (ab.pfrb_size > maxcount)
			yyerror("cannot define table %s: too many elements.\n"
			    "Consider increasing net.pf.request_maxcount.",
			    name);
		else
			yyerror("cannot define table %s: %s", name,
			    pfr_strerror(errno));

		goto _error;
	}
	pf->tdirty = 1;
	pfr_buf_clear(&ab);
	return (0);
_error:
	pfr_buf_clear(&ab);
	return (-1);
}

struct keywords {
	const char	*k_name;
	int		 k_val;
};

/* macro gore, but you should've seen the prior indentation nightmare... */

#define FREE_LIST(T,r) \
	do { \
		T *p, *node = r; \
		while (node != NULL) { \
			p = node; \
			node = node->next; \
			free(p); \
		} \
	} while (0)

#define LOOP_THROUGH(T,n,r,C) \
	do { \
		T *n; \
		if (r == NULL) { \
			r = calloc(1, sizeof(T)); \
			if (r == NULL) \
				err(1, "LOOP: calloc"); \
			r->next = NULL; \
		} \
		n = r; \
		while (n != NULL) { \
			do { \
				C; \
			} while (0); \
			n = n->next; \
		} \
	} while (0)

void
expand_label_str(char *label, size_t len, const char *srch, const char *repl)
{
	char *tmp;
	char *p, *q;

	if ((tmp = calloc(1, len)) == NULL)
		err(1, "expand_label_str: calloc");
	p = q = label;
	while ((q = strstr(p, srch)) != NULL) {
		*q = '\0';
		if ((strlcat(tmp, p, len) >= len) ||
		    (strlcat(tmp, repl, len) >= len))
			errx(1, "expand_label: label too long");
		q += strlen(srch);
		p = q;
	}
	if (strlcat(tmp, p, len) >= len)
		errx(1, "expand_label: label too long");
	strlcpy(label, tmp, len);	/* always fits */
	free(tmp);
}

void
expand_label_if(const char *name, char *label, size_t len, const char *ifname)
{
	if (strstr(label, name) != NULL) {
		if (!*ifname)
			expand_label_str(label, len, name, "any");
		else
			expand_label_str(label, len, name, ifname);
	}
}

void
expand_label_addr(const char *name, char *label, size_t len, sa_family_t af,
    struct node_host *h)
{
	char tmp[64], tmp_not[66];

	if (strstr(label, name) != NULL) {
		switch (h->addr.type) {
		case PF_ADDR_DYNIFTL:
			snprintf(tmp, sizeof(tmp), "(%s)", h->addr.v.ifname);
			break;
		case PF_ADDR_TABLE:
			snprintf(tmp, sizeof(tmp), "<%s>", h->addr.v.tblname);
			break;
		case PF_ADDR_NOROUTE:
			snprintf(tmp, sizeof(tmp), "no-route");
			break;
		case PF_ADDR_URPFFAILED:
			snprintf(tmp, sizeof(tmp), "urpf-failed");
			break;
		case PF_ADDR_ADDRMASK:
			if (!af || (PF_AZERO(&h->addr.v.a.addr, af) &&
			    PF_AZERO(&h->addr.v.a.mask, af)))
				snprintf(tmp, sizeof(tmp), "any");
			else {
				char	a[48];
				int	bits;

				if (inet_ntop(af, &h->addr.v.a.addr, a,
				    sizeof(a)) == NULL)
					snprintf(tmp, sizeof(tmp), "?");
				else {
					bits = unmask(&h->addr.v.a.mask, af);
					if ((af == AF_INET && bits < 32) ||
					    (af == AF_INET6 && bits < 128))
						snprintf(tmp, sizeof(tmp),
						    "%s/%d", a, bits);
					else
						snprintf(tmp, sizeof(tmp),
						    "%s", a);
				}
			}
			break;
		default:
			snprintf(tmp, sizeof(tmp), "?");
			break;
		}

		if (h->not) {
			snprintf(tmp_not, sizeof(tmp_not), "! %s", tmp);
			expand_label_str(label, len, name, tmp_not);
		} else
			expand_label_str(label, len, name, tmp);
	}
}

void
expand_label_port(const char *name, char *label, size_t len,
    struct node_port *port)
{
	char	 a1[6], a2[6], op[13] = "";

	if (strstr(label, name) != NULL) {
		snprintf(a1, sizeof(a1), "%u", ntohs(port->port[0]));
		snprintf(a2, sizeof(a2), "%u", ntohs(port->port[1]));
		if (!port->op)
			;
		else if (port->op == PF_OP_IRG)
			snprintf(op, sizeof(op), "%s><%s", a1, a2);
		else if (port->op == PF_OP_XRG)
			snprintf(op, sizeof(op), "%s<>%s", a1, a2);
		else if (port->op == PF_OP_EQ)
			snprintf(op, sizeof(op), "%s", a1);
		else if (port->op == PF_OP_NE)
			snprintf(op, sizeof(op), "!=%s", a1);
		else if (port->op == PF_OP_LT)
			snprintf(op, sizeof(op), "<%s", a1);
		else if (port->op == PF_OP_LE)
			snprintf(op, sizeof(op), "<=%s", a1);
		else if (port->op == PF_OP_GT)
			snprintf(op, sizeof(op), ">%s", a1);
		else if (port->op == PF_OP_GE)
			snprintf(op, sizeof(op), ">=%s", a1);
		expand_label_str(label, len, name, op);
	}
}

void
expand_label_proto(const char *name, char *label, size_t len, u_int8_t proto)
{
	struct protoent *pe;
	char n[4];

	if (strstr(label, name) != NULL) {
		pe = getprotobynumber(proto);
		if (pe != NULL)
			expand_label_str(label, len, name, pe->p_name);
		else {
			snprintf(n, sizeof(n), "%u", proto);
			expand_label_str(label, len, name, n);
		}
	}
}

void
expand_label_nr(const char *name, char *label, size_t len)
{
	char n[11];

	if (strstr(label, name) != NULL) {
		snprintf(n, sizeof(n), "%u", pf->anchor->match);
		expand_label_str(label, len, name, n);
	}
}

void
expand_label(char *label, size_t len, const char *ifname, sa_family_t af,
    struct node_host *src_host, struct node_port *src_port,
    struct node_host *dst_host, struct node_port *dst_port,
    u_int8_t proto)
{
	expand_label_if("$if", label, len, ifname);
	expand_label_addr("$srcaddr", label, len, af, src_host);
	expand_label_addr("$dstaddr", label, len, af, dst_host);
	expand_label_port("$srcport", label, len, src_port);
	expand_label_port("$dstport", label, len, dst_port);
	expand_label_proto("$proto", label, len, proto);
	expand_label_nr("$nr", label, len);
}

int
expand_altq(struct pf_altq *a, struct node_if *interfaces,
    struct node_queue *nqueues, struct node_queue_bw bwspec,
    struct node_queue_opt *opts)
{
	struct pf_altq		 pa, pb;
	char			 qname[PF_QNAME_SIZE];
	struct node_queue	*n;
	struct node_queue_bw	 bw;
	int			 errs = 0;

	if ((pf->loadopt & PFCTL_FLAG_ALTQ) == 0) {
		FREE_LIST(struct node_if, interfaces);
		if (nqueues)
			FREE_LIST(struct node_queue, nqueues);
		return (0);
	}

	LOOP_THROUGH(struct node_if, interface, interfaces,
		memcpy(&pa, a, sizeof(struct pf_altq));
		if (strlcpy(pa.ifname, interface->ifname,
		    sizeof(pa.ifname)) >= sizeof(pa.ifname))
			errx(1, "expand_altq: strlcpy");

		if (interface->not) {
			yyerror("altq on ! <interface> is not supported");
			errs++;
		} else {
			if (eval_pfaltq(pf, &pa, &bwspec, opts))
				errs++;
			else
				if (pfctl_add_altq(pf, &pa))
					errs++;

			if (pf->opts & PF_OPT_VERBOSE) {
				print_altq(&pf->paltq->altq, 0,
				    &bwspec, opts);
				if (nqueues && nqueues->tail) {
					printf("queue { ");
					LOOP_THROUGH(struct node_queue, queue,
					    nqueues,
						printf("%s ",
						    queue->queue);
					);
					printf("}");
				}
				printf("\n");
			}

			if (pa.scheduler == ALTQT_CBQ ||
			    pa.scheduler == ALTQT_HFSC) {
				/* now create a root queue */
				memset(&pb, 0, sizeof(struct pf_altq));
				if (strlcpy(qname, "root_", sizeof(qname)) >=
				    sizeof(qname))
					errx(1, "expand_altq: strlcpy");
				if (strlcat(qname, interface->ifname,
				    sizeof(qname)) >= sizeof(qname))
					errx(1, "expand_altq: strlcat");
				if (strlcpy(pb.qname, qname,
				    sizeof(pb.qname)) >= sizeof(pb.qname))
					errx(1, "expand_altq: strlcpy");
				if (strlcpy(pb.ifname, interface->ifname,
				    sizeof(pb.ifname)) >= sizeof(pb.ifname))
					errx(1, "expand_altq: strlcpy");
				pb.qlimit = pa.qlimit;
				pb.scheduler = pa.scheduler;
				bw.bw_absolute = pa.ifbandwidth;
				bw.bw_percent = 0;
				if (eval_pfqueue(pf, &pb, &bw, opts))
					errs++;
				else
					if (pfctl_add_altq(pf, &pb))
						errs++;
			}

			LOOP_THROUGH(struct node_queue, queue, nqueues,
				n = calloc(1, sizeof(struct node_queue));
				if (n == NULL)
					err(1, "expand_altq: calloc");
				if (pa.scheduler == ALTQT_CBQ ||
				    pa.scheduler == ALTQT_HFSC)
					if (strlcpy(n->parent, qname,
					    sizeof(n->parent)) >=
					    sizeof(n->parent))
						errx(1, "expand_altq: strlcpy");
				if (strlcpy(n->queue, queue->queue,
				    sizeof(n->queue)) >= sizeof(n->queue))
					errx(1, "expand_altq: strlcpy");
				if (strlcpy(n->ifname, interface->ifname,
				    sizeof(n->ifname)) >= sizeof(n->ifname))
					errx(1, "expand_altq: strlcpy");
				n->scheduler = pa.scheduler;
				n->next = NULL;
				n->tail = n;
				if (queues == NULL)
					queues = n;
				else {
					queues->tail->next = n;
					queues->tail = n;
				}
			);
		}
	);
	FREE_LIST(struct node_if, interfaces);
	if (nqueues)
		FREE_LIST(struct node_queue, nqueues);

	return (errs);
}

int
expand_queue(struct pf_altq *a, struct node_if *interfaces,
    struct node_queue *nqueues, struct node_queue_bw bwspec,
    struct node_queue_opt *opts)
{
	struct node_queue	*n, *nq;
	struct pf_altq		 pa;
	u_int8_t		 found = 0;
	u_int8_t		 errs = 0;

	if ((pf->loadopt & PFCTL_FLAG_ALTQ) == 0) {
		FREE_LIST(struct node_queue, nqueues);
		return (0);
	}

	if (queues == NULL) {
		yyerror("queue %s has no parent", a->qname);
		FREE_LIST(struct node_queue, nqueues);
		return (1);
	}

	LOOP_THROUGH(struct node_if, interface, interfaces,
		LOOP_THROUGH(struct node_queue, tqueue, queues,
			if (!strncmp(a->qname, tqueue->queue, PF_QNAME_SIZE) &&
			    (interface->ifname[0] == 0 ||
			    (!interface->not && !strncmp(interface->ifname,
			    tqueue->ifname, IFNAMSIZ)) ||
			    (interface->not && strncmp(interface->ifname,
			    tqueue->ifname, IFNAMSIZ)))) {
				/* found ourself in queues */
				found++;

				memcpy(&pa, a, sizeof(struct pf_altq));

				if (pa.scheduler != ALTQT_NONE &&
				    pa.scheduler != tqueue->scheduler) {
					yyerror("exactly one scheduler type "
					    "per interface allowed");
					return (1);
				}
				pa.scheduler = tqueue->scheduler;

				/* scheduler dependent error checking */
				switch (pa.scheduler) {
				case ALTQT_PRIQ:
					if (nqueues != NULL) {
						yyerror("priq queues cannot "
						    "have child queues");
						return (1);
					}
					if (bwspec.bw_absolute > 0 ||
					    bwspec.bw_percent < 100) {
						yyerror("priq doesn't take "
						    "bandwidth");
						return (1);
					}
					break;
				default:
					break;
				}

				if (strlcpy(pa.ifname, tqueue->ifname,
				    sizeof(pa.ifname)) >= sizeof(pa.ifname))
					errx(1, "expand_queue: strlcpy");
				if (strlcpy(pa.parent, tqueue->parent,
				    sizeof(pa.parent)) >= sizeof(pa.parent))
					errx(1, "expand_queue: strlcpy");

				if (eval_pfqueue(pf, &pa, &bwspec, opts))
					errs++;
				else
					if (pfctl_add_altq(pf, &pa))
						errs++;

				for (nq = nqueues; nq != NULL; nq = nq->next) {
					if (!strcmp(a->qname, nq->queue)) {
						yyerror("queue cannot have "
						    "itself as child");
						errs++;
						continue;
					}
					n = calloc(1,
					    sizeof(struct node_queue));
					if (n == NULL)
						err(1, "expand_queue: calloc");
					if (strlcpy(n->parent, a->qname,
					    sizeof(n->parent)) >=
					    sizeof(n->parent))
						errx(1, "expand_queue strlcpy");
					if (strlcpy(n->queue, nq->queue,
					    sizeof(n->queue)) >=
					    sizeof(n->queue))
						errx(1, "expand_queue strlcpy");
					if (strlcpy(n->ifname, tqueue->ifname,
					    sizeof(n->ifname)) >=
					    sizeof(n->ifname))
						errx(1, "expand_queue strlcpy");
					n->scheduler = tqueue->scheduler;
					n->next = NULL;
					n->tail = n;
					if (queues == NULL)
						queues = n;
					else {
						queues->tail->next = n;
						queues->tail = n;
					}
				}
				if ((pf->opts & PF_OPT_VERBOSE) && (
				    (found == 1 && interface->ifname[0] == 0) ||
				    (found > 0 && interface->ifname[0] != 0))) {
					print_queue(&pf->paltq->altq, 0,
					    &bwspec, interface->ifname[0] != 0,
					    opts);
					if (nqueues && nqueues->tail) {
						printf("{ ");
						LOOP_THROUGH(struct node_queue,
						    queue, nqueues,
							printf("%s ",
							    queue->queue);
						);
						printf("}");
					}
					printf("\n");
				}
			}
		);
	);

	FREE_LIST(struct node_queue, nqueues);
	FREE_LIST(struct node_if, interfaces);

	if (!found) {
		yyerror("queue %s has no parent", a->qname);
		errs++;
	}

	if (errs)
		return (1);
	else
		return (0);
}

void
expand_rule(struct pf_rule *r,
    struct node_if *interfaces, struct node_host *rpool_hosts,
    struct node_proto *protos, struct node_os *src_oses,
    struct node_host *src_hosts, struct node_port *src_ports,
    struct node_host *dst_hosts, struct node_port *dst_ports,
    struct node_uid *uids, struct node_gid *gids, struct node_icmp *icmp_types,
    const char *anchor_call)
{
	sa_family_t		 af = r->af;
	int			 added = 0, error = 0;
	char			 ifname[IF_NAMESIZE];
	char			 label[PF_RULE_LABEL_SIZE];
	char			 tagname[PF_TAG_NAME_SIZE];
	char			 match_tagname[PF_TAG_NAME_SIZE];
	struct pf_pooladdr	*pa;
	struct node_host	*h;
	u_int8_t		 flags, flagset, keep_state;

	if (strlcpy(label, r->label, sizeof(label)) >= sizeof(label))
		errx(1, "expand_rule: strlcpy");
	if (strlcpy(tagname, r->tagname, sizeof(tagname)) >= sizeof(tagname))
		errx(1, "expand_rule: strlcpy");
	if (strlcpy(match_tagname, r->match_tagname, sizeof(match_tagname)) >=
	    sizeof(match_tagname))
		errx(1, "expand_rule: strlcpy");
	flags = r->flags;
	flagset = r->flagset;
	keep_state = r->keep_state;

	LOOP_THROUGH(struct node_if, interface, interfaces,
	LOOP_THROUGH(struct node_proto, proto, protos,
	LOOP_THROUGH(struct node_icmp, icmp_type, icmp_types,
	LOOP_THROUGH(struct node_host, src_host, src_hosts,
	LOOP_THROUGH(struct node_port, src_port, src_ports,
	LOOP_THROUGH(struct node_os, src_os, src_oses,
	LOOP_THROUGH(struct node_host, dst_host, dst_hosts,
	LOOP_THROUGH(struct node_port, dst_port, dst_ports,
	LOOP_THROUGH(struct node_uid, uid, uids,
	LOOP_THROUGH(struct node_gid, gid, gids,

		r->af = af;
		/* for link-local IPv6 address, interface must match up */
		if ((r->af && src_host->af && r->af != src_host->af) ||
		    (r->af && dst_host->af && r->af != dst_host->af) ||
		    (src_host->af && dst_host->af &&
		    src_host->af != dst_host->af) ||
		    (src_host->ifindex && dst_host->ifindex &&
		    src_host->ifindex != dst_host->ifindex) ||
		    (src_host->ifindex && *interface->ifname &&
		    src_host->ifindex != if_nametoindex(interface->ifname)) ||
		    (dst_host->ifindex && *interface->ifname &&
		    dst_host->ifindex != if_nametoindex(interface->ifname)))
			continue;
		if (!r->af && src_host->af)
			r->af = src_host->af;
		else if (!r->af && dst_host->af)
			r->af = dst_host->af;

		if (*interface->ifname)
			strlcpy(r->ifname, interface->ifname,
			    sizeof(r->ifname));
		else if (if_indextoname(src_host->ifindex, ifname))
			strlcpy(r->ifname, ifname, sizeof(r->ifname));
		else if (if_indextoname(dst_host->ifindex, ifname))
			strlcpy(r->ifname, ifname, sizeof(r->ifname));
		else
			memset(r->ifname, '\0', sizeof(r->ifname));

		if (strlcpy(r->label, label, sizeof(r->label)) >=
		    sizeof(r->label))
			errx(1, "expand_rule: strlcpy");
		if (strlcpy(r->tagname, tagname, sizeof(r->tagname)) >=
		    sizeof(r->tagname))
			errx(1, "expand_rule: strlcpy");
		if (strlcpy(r->match_tagname, match_tagname,
		    sizeof(r->match_tagname)) >= sizeof(r->match_tagname))
			errx(1, "expand_rule: strlcpy");
		expand_label(r->label, PF_RULE_LABEL_SIZE, r->ifname, r->af,
		    src_host, src_port, dst_host, dst_port, proto->proto);
		expand_label(r->tagname, PF_TAG_NAME_SIZE, r->ifname, r->af,
		    src_host, src_port, dst_host, dst_port, proto->proto);
		expand_label(r->match_tagname, PF_TAG_NAME_SIZE, r->ifname,
		    r->af, src_host, src_port, dst_host, dst_port,
		    proto->proto);

		error += check_netmask(src_host, r->af);
		error += check_netmask(dst_host, r->af);

		r->ifnot = interface->not;
		r->proto = proto->proto;
		r->src.addr = src_host->addr;
		r->src.neg = src_host->not;
		r->src.port[0] = src_port->port[0];
		r->src.port[1] = src_port->port[1];
		r->src.port_op = src_port->op;
		r->dst.addr = dst_host->addr;
		r->dst.neg = dst_host->not;
		r->dst.port[0] = dst_port->port[0];
		r->dst.port[1] = dst_port->port[1];
		r->dst.port_op = dst_port->op;
		r->uid.op = uid->op;
		r->uid.uid[0] = uid->uid[0];
		r->uid.uid[1] = uid->uid[1];
		r->gid.op = gid->op;
		r->gid.gid[0] = gid->gid[0];
		r->gid.gid[1] = gid->gid[1];
		r->type = icmp_type->type;
		r->code = icmp_type->code;

		if ((keep_state == PF_STATE_MODULATE ||
		    keep_state == PF_STATE_SYNPROXY) &&
		    r->proto && r->proto != IPPROTO_TCP)
			r->keep_state = PF_STATE_NORMAL;
		else
			r->keep_state = keep_state;

		if (r->proto && r->proto != IPPROTO_TCP) {
			r->flags = 0;
			r->flagset = 0;
		} else {
			r->flags = flags;
			r->flagset = flagset;
		}
		if (icmp_type->proto && r->proto != icmp_type->proto) {
			yyerror("icmp-type mismatch");
			error++;
		}

		if (src_os && src_os->os) {
			r->os_fingerprint = pfctl_get_fingerprint(src_os->os);
			if ((pf->opts & PF_OPT_VERBOSE2) &&
			    r->os_fingerprint == PF_OSFP_NOMATCH)
				fprintf(stderr,
				    "warning: unknown '%s' OS fingerprint\n",
				    src_os->os);
		} else {
			r->os_fingerprint = PF_OSFP_ANY;
		}

		TAILQ_INIT(&r->rpool.list);
		for (h = rpool_hosts; h != NULL; h = h->next) {
			pa = calloc(1, sizeof(struct pf_pooladdr));
			if (pa == NULL)
				err(1, "expand_rule: calloc");
			pa->addr = h->addr;
			if (h->ifname != NULL) {
				if (strlcpy(pa->ifname, h->ifname,
				    sizeof(pa->ifname)) >=
				    sizeof(pa->ifname))
					errx(1, "expand_rule: strlcpy");
			} else
				pa->ifname[0] = 0;
			TAILQ_INSERT_TAIL(&r->rpool.list, pa, entries);
		}

		if (rule_consistent(r, anchor_call[0]) < 0 || error)
			yyerror("skipping rule due to errors");
		else {
			r->nr = pf->astack[pf->asd]->match++;
			pfctl_add_rule(pf, r, anchor_call);
			added++;
		}

	))))))))));

	FREE_LIST(struct node_if, interfaces);
	FREE_LIST(struct node_proto, protos);
	FREE_LIST(struct node_host, src_hosts);
	FREE_LIST(struct node_port, src_ports);
	FREE_LIST(struct node_os, src_oses);
	FREE_LIST(struct node_host, dst_hosts);
	FREE_LIST(struct node_port, dst_ports);
	FREE_LIST(struct node_uid, uids);
	FREE_LIST(struct node_gid, gids);
	FREE_LIST(struct node_icmp, icmp_types);
	FREE_LIST(struct node_host, rpool_hosts);

	if (!added)
		yyerror("rule expands to no valid combination");
}

int
expand_skip_interface(struct node_if *interfaces)
{
	int	errs = 0;

	if (!interfaces || (!interfaces->next && !interfaces->not &&
	    !strcmp(interfaces->ifname, "none"))) {
		if (pf->opts & PF_OPT_VERBOSE)
			printf("set skip on none\n");
		errs = pfctl_set_interface_flags(pf, "", PFI_IFLAG_SKIP, 0);
		return (errs);
	}

	if (pf->opts & PF_OPT_VERBOSE)
		printf("set skip on {");
	LOOP_THROUGH(struct node_if, interface, interfaces,
		if (pf->opts & PF_OPT_VERBOSE)
			printf(" %s", interface->ifname);
		if (interface->not) {
			yyerror("skip on ! <interface> is not supported");
			errs++;
		} else
			errs += pfctl_set_interface_flags(pf,
			    interface->ifname, PFI_IFLAG_SKIP, 1);
	);
	if (pf->opts & PF_OPT_VERBOSE)
		printf(" }\n");

	FREE_LIST(struct node_if, interfaces);

	if (errs)
		return (1);
	else
		return (0);
}

#undef FREE_LIST
#undef LOOP_THROUGH

int
check_rulestate(int desired_state)
{
	if (require_order && (rulestate > desired_state)) {
		yyerror("Rules must be in order: options, normalization, "
		    "queueing, translation, filtering");
		return (1);
	}
	rulestate = desired_state;
	return (0);
}

int
kw_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int
lookup(char *s)
{
	/* this has to be sorted always */
	static const struct keywords keywords[] = {
		{ "all",		ALL},
		{ "allow-opts",		ALLOWOPTS},
		{ "altq",		ALTQ},
		{ "anchor",		ANCHOR},
		{ "antispoof",		ANTISPOOF},
		{ "any",		ANY},
		{ "bandwidth",		BANDWIDTH},
		{ "binat",		BINAT},
		{ "binat-anchor",	BINATANCHOR},
		{ "bitmask",		BITMASK},
		{ "block",		BLOCK},
		{ "block-policy",	BLOCKPOLICY},
		{ "buckets",		BUCKETS},
		{ "cbq",		CBQ},
		{ "code",		CODE},
		{ "codelq",		CODEL},
		{ "crop",		FRAGCROP},
		{ "debug",		DEBUG},
		{ "divert-reply",	DIVERTREPLY},
		{ "divert-to",		DIVERTTO},
		{ "drop",		DROP},
		{ "drop-ovl",		FRAGDROP},
		{ "dup-to",		DUPTO},
		{ "fail-policy",	FAILPOLICY},
		{ "fairq",		FAIRQ},
		{ "fastroute",		FASTROUTE},
		{ "file",		FILENAME},
		{ "fingerprints",	FINGERPRINTS},
		{ "flags",		FLAGS},
		{ "floating",		FLOATING},
		{ "flush",		FLUSH},
		{ "for",		FOR},
		{ "fragment",		FRAGMENT},
		{ "from",		FROM},
		{ "global",		GLOBAL},
		{ "group",		GROUP},
		{ "hfsc",		HFSC},
		{ "hogs",		HOGS},
		{ "hostid",		HOSTID},
		{ "icmp-type",		ICMPTYPE},
		{ "icmp6-type",		ICMP6TYPE},
		{ "if-bound",		IFBOUND},
		{ "in",			IN},
		{ "include",		INCLUDE},
		{ "inet",		INET},
		{ "inet6",		INET6},
		{ "interval",		INTERVAL},
		{ "keep",		KEEP},
		{ "label",		LABEL},
		{ "limit",		LIMIT},
		{ "linkshare",		LINKSHARE},
		{ "load",		LOAD},
		{ "log",		LOG},
		{ "loginterface",	LOGINTERFACE},
		{ "max",		MAXIMUM},
		{ "max-mss",		MAXMSS},
		{ "max-src-conn",	MAXSRCCONN},
		{ "max-src-conn-rate",	MAXSRCCONNRATE},
		{ "max-src-nodes",	MAXSRCNODES},
		{ "max-src-states",	MAXSRCSTATES},
		{ "min-ttl",		MINTTL},
		{ "modulate",		MODULATE},
		{ "nat",		NAT},
		{ "nat-anchor",		NATANCHOR},
		{ "no",			NO},
		{ "no-df",		NODF},
		{ "no-route",		NOROUTE},
		{ "no-sync",		NOSYNC},
		{ "on",			ON},
		{ "optimization",	OPTIMIZATION},
		{ "os",			OS},
		{ "out",		OUT},
		{ "overload",		OVERLOAD},
		{ "pass",		PASS},
		{ "port",		PORT},
		{ "prio",		PRIO},
		{ "priority",		PRIORITY},
		{ "priq",		PRIQ},
		{ "probability",	PROBABILITY},
		{ "proto",		PROTO},
		{ "qlimit",		QLIMIT},
		{ "queue",		QUEUE},
		{ "quick",		QUICK},
		{ "random",		RANDOM},
		{ "random-id",		RANDOMID},
		{ "rdr",		RDR},
		{ "rdr-anchor",		RDRANCHOR},
		{ "realtime",		REALTIME},
		{ "reassemble",		REASSEMBLE},
		{ "reply-to",		REPLYTO},
		{ "require-order",	REQUIREORDER},
		{ "return",		RETURN},
		{ "return-icmp",	RETURNICMP},
		{ "return-icmp6",	RETURNICMP6},
		{ "return-rst",		RETURNRST},
		{ "round-robin",	ROUNDROBIN},
		{ "route",		ROUTE},
		{ "route-to",		ROUTETO},
		{ "rtable",		RTABLE},
		{ "rule",		RULE},
		{ "ruleset-optimization",	RULESET_OPTIMIZATION},
		{ "scrub",		SCRUB},
		{ "set",		SET},
		{ "set-tos",		SETTOS},
		{ "skip",		SKIP},
		{ "sloppy",		SLOPPY},
		{ "source-hash",	SOURCEHASH},
		{ "source-track",	SOURCETRACK},
		{ "state",		STATE},
		{ "state-defaults",	STATEDEFAULTS},
		{ "state-policy",	STATEPOLICY},
		{ "static-port",	STATICPORT},
		{ "sticky-address",	STICKYADDRESS},
		{ "synproxy",		SYNPROXY},
		{ "table",		TABLE},
		{ "tag",		TAG},
		{ "tagged",		TAGGED},
		{ "target",		TARGET},
		{ "tbrsize",		TBRSIZE},
		{ "timeout",		TIMEOUT},
		{ "to",			TO},
		{ "tos",		TOS},
		{ "ttl",		TTL},
		{ "upperlimit",		UPPERLIMIT},
		{ "urpf-failed",	URPFFAILED},
		{ "user",		USER},
	};
	const struct keywords	*p;

	p = bsearch(s, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kw_cmp);

	if (p) {
		if (debug > 1)
			fprintf(stderr, "%s: %d\n", s, p->k_val);
		return (p->k_val);
	} else {
		if (debug > 1)
			fprintf(stderr, "string: %s\n", s);
		return (STRING);
	}
}

#define MAXPUSHBACK	128

static char	*parsebuf;
static int	 parseindex;
static char	 pushback_buffer[MAXPUSHBACK];
static int	 pushback_index = 0;

int
lgetc(int quotec)
{
	int		c, next;

	if (parsebuf) {
		/* Read character from the parsebuffer instead of input. */
		if (parseindex >= 0) {
			c = parsebuf[parseindex++];
			if (c != '\0')
				return (c);
			parsebuf = NULL;
		} else
			parseindex++;
	}

	if (pushback_index)
		return (pushback_buffer[--pushback_index]);

	if (quotec) {
		if ((c = getc(file->stream)) == EOF) {
			yyerror("reached end of file while parsing quoted string");
			if (popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = getc(file->stream)) == '\\') {
		next = getc(file->stream);
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	while (c == EOF) {
		if (popfile() == EOF)
			return (EOF);
		c = getc(file->stream);
	}
	return (c);
}

int
lungetc(int c)
{
	if (c == EOF)
		return (EOF);
	if (parsebuf) {
		parseindex--;
		if (parseindex >= 0)
			return (c);
	}
	if (pushback_index < MAXPUSHBACK-1)
		return (pushback_buffer[pushback_index++] = c);
	else
		return (EOF);
}

int
findeol(void)
{
	int	c;

	parsebuf = NULL;

	/* skip to either EOF or the first real EOL */
	while (1) {
		if (pushback_index)
			c = pushback_buffer[--pushback_index];
		else
			c = lgetc(0);
		if (c == '\n') {
			file->lineno++;
			break;
		}
		if (c == EOF)
			break;
	}
	return (ERROR);
}

int
yylex(void)
{
	char	 buf[8096];
	char	*p, *val;
	int	 quotec, next, c;
	int	 token;

top:
	p = buf;
	while ((c = lgetc(0)) == ' ' || c == '\t')
		; /* nothing */

	yylval.lineno = file->lineno;
	if (c == '#')
		while ((c = lgetc(0)) != '\n' && c != EOF)
			; /* nothing */
	if (c == '$' && parsebuf == NULL) {
		while (1) {
			if ((c = lgetc(0)) == EOF)
				return (0);

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			if (isalnum(c) || c == '_') {
				*p++ = (char)c;
				continue;
			}
			*p = '\0';
			lungetc(c);
			break;
		}
		val = symget(buf);
		if (val == NULL) {
			yyerror("macro '%s' not defined", buf);
			return (findeol());
		}
		parsebuf = val;
		parseindex = 0;
		goto top;
	}

	switch (c) {
	case '\'':
	case '"':
		quotec = c;
		while (1) {
			if ((c = lgetc(quotec)) == EOF)
				return (0);
			if (c == '\n') {
				file->lineno++;
				continue;
			} else if (c == '\\') {
				if ((next = lgetc(quotec)) == EOF)
					return (0);
				if (next == quotec || c == ' ' || c == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				}
				else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = (char)c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			err(1, "yylex: strdup");
		return (STRING);
	case '<':
		next = lgetc(0);
		if (next == '>') {
			yylval.v.i = PF_OP_XRG;
			return (PORTBINARY);
		}
		lungetc(next);
		break;
	case '>':
		next = lgetc(0);
		if (next == '<') {
			yylval.v.i = PF_OP_IRG;
			return (PORTBINARY);
		}
		lungetc(next);
		break;
	case '-':
		next = lgetc(0);
		if (next == '>')
			return (ARROW);
		lungetc(next);
		break;
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((unsigned)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && isdigit(c));
		lungetc(c);
		if (p == buf + 1 && buf[0] == '-')
			goto nodigits;
		if (c == EOF || allowed_to_end_number(c)) {
			const char *errstr = NULL;

			*p = '\0';
			yylval.v.number = strtonum(buf, LLONG_MIN,
			    LLONG_MAX, &errstr);
			if (errstr) {
				yyerror("\"%s\" invalid number: %s",
				    buf, errstr);
				return (findeol());
			}
			return (NUMBER);
		} else {
nodigits:
			while (p > buf + 1)
				lungetc(*--p);
			c = *--p;
			if (c == '-')
				return (c);
		}
	}

#define allowed_in_string(x) \
	(isalnum(x) || (ispunct(x) && x != '(' && x != ')' && \
	x != '{' && x != '}' && x != '<' && x != '>' && \
	x != '!' && x != '=' && x != '/' && x != '#' && \
	x != ','))

	if (isalnum(c) || c == ':' || c == '_') {
		do {
			*p++ = c;
			if ((unsigned)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == STRING)
			if ((yylval.v.string = strdup(buf)) == NULL)
				err(1, "yylex: strdup");
		return (token);
	}
	if (c == '\n') {
		yylval.lineno = file->lineno;
		file->lineno++;
	}
	if (c == EOF)
		return (0);
	return (c);
}

int
check_file_secrecy(int fd, const char *fname)
{
	struct stat	st;

	if (fstat(fd, &st)) {
		warn("cannot stat %s", fname);
		return (-1);
	}
	if (st.st_uid != 0 && st.st_uid != getuid()) {
		warnx("%s: owner not root or current user", fname);
		return (-1);
	}
	if (st.st_mode & (S_IRWXG | S_IRWXO)) {
		warnx("%s: group/world readable/writeable", fname);
		return (-1);
	}
	return (0);
}

struct file *
pushfile(const char *name, int secret)
{
	struct file	*nfile;

	if ((nfile = calloc(1, sizeof(struct file))) == NULL ||
	    (nfile->name = strdup(name)) == NULL) {
		warn("malloc");
		return (NULL);
	}
	if (TAILQ_FIRST(&files) == NULL && strcmp(nfile->name, "-") == 0) {
		nfile->stream = stdin;
		free(nfile->name);
		if ((nfile->name = strdup("stdin")) == NULL) {
			warn("strdup");
			free(nfile);
			return (NULL);
		}
	} else if ((nfile->stream = fopen(nfile->name, "r")) == NULL) {
		warn("%s", nfile->name);
		free(nfile->name);
		free(nfile);
		return (NULL);
	} else if (secret &&
	    check_file_secrecy(fileno(nfile->stream), nfile->name)) {
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	nfile->lineno = 1;
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL) {
		prev->errors += file->errors;
		TAILQ_REMOVE(&files, file, entry);
		fclose(file->stream);
		free(file->name);
		free(file);
		file = prev;
		return (0);
	}
	return (EOF);
}

int
parse_config(char *filename, struct pfctl *xpf)
{
	int		 errors = 0;
	struct sym	*sym;

	pf = xpf;
	errors = 0;
	rulestate = PFCTL_STATE_NONE;
	returnicmpdefault = (ICMP_UNREACH << 8) | ICMP_UNREACH_PORT;
	returnicmp6default =
	    (ICMP6_DST_UNREACH << 8) | ICMP6_DST_UNREACH_NOPORT;
	blockpolicy = PFRULE_DROP;
	failpolicy = PFRULE_DROP;
	require_order = 1;

	if ((file = pushfile(filename, 0)) == NULL) {
		warn("cannot open the main config file!");
		return (-1);
	}

	yyparse();
	errors = file->errors;
	popfile();

	/* Free macros and check which have not been used. */
	while ((sym = TAILQ_FIRST(&symhead))) {
		if ((pf->opts & PF_OPT_VERBOSE2) && !sym->used)
			fprintf(stderr, "warning: macro '%s' not "
			    "used\n", sym->nam);
		free(sym->nam);
		free(sym->val);
		TAILQ_REMOVE(&symhead, sym, entry);
		free(sym);
	}

	return (errors ? -1 : 0);
}

int
symset(const char *nam, const char *val, int persist)
{
	struct sym	*sym;

	for (sym = TAILQ_FIRST(&symhead); sym && strcmp(nam, sym->nam);
	    sym = TAILQ_NEXT(sym, entry))
		;	/* nothing */

	if (sym != NULL) {
		if (sym->persist == 1)
			return (0);
		else {
			free(sym->nam);
			free(sym->val);
			TAILQ_REMOVE(&symhead, sym, entry);
			free(sym);
		}
	}
	if ((sym = calloc(1, sizeof(*sym))) == NULL)
		return (-1);

	sym->nam = strdup(nam);
	if (sym->nam == NULL) {
		free(sym);
		return (-1);
	}
	sym->val = strdup(val);
	if (sym->val == NULL) {
		free(sym->nam);
		free(sym);
		return (-1);
	}
	sym->used = 0;
	sym->persist = persist;
	TAILQ_INSERT_TAIL(&symhead, sym, entry);
	return (0);
}

int
pfctl_cmdline_symset(char *s)
{
	char	*sym, *val;
	int	 ret;

	if ((val = strrchr(s, '=')) == NULL)
		return (-1);

	if ((sym = malloc(strlen(s) - strlen(val) + 1)) == NULL)
		err(1, "pfctl_cmdline_symset: malloc");

	strlcpy(sym, s, strlen(s) - strlen(val) + 1);

	ret = symset(sym, val + 1, 1);
	free(sym);

	return (ret);
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry)
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return (sym->val);
		}
	return (NULL);
}

void
mv_rules(struct pf_ruleset *src, struct pf_ruleset *dst)
{
	int i;
	struct pf_rule *r;

	for (i = 0; i < PF_RULESET_MAX; ++i) {
		while ((r = TAILQ_FIRST(src->rules[i].active.ptr))
		    != NULL) {
			TAILQ_REMOVE(src->rules[i].active.ptr, r, entries);
			TAILQ_INSERT_TAIL(dst->rules[i].active.ptr, r, entries);
			dst->anchor->match++;
		}
		src->anchor->match = 0;
		while ((r = TAILQ_FIRST(src->rules[i].inactive.ptr))
		    != NULL) {
			TAILQ_REMOVE(src->rules[i].inactive.ptr, r, entries);
			TAILQ_INSERT_TAIL(dst->rules[i].inactive.ptr,
				r, entries);
		}
	}
}

void
decide_address_family(struct node_host *n, sa_family_t *af)
{
	if (*af != 0 || n == NULL)
		return;
	*af = n->af;
	while ((n = n->next) != NULL) {
		if (n->af != *af) {
			*af = 0;
			return;
		}
	}
}

void
remove_invalid_hosts(struct node_host **nh, sa_family_t *af)
{
	struct node_host	*n = *nh, *prev = NULL;

	while (n != NULL) {
		if (*af && n->af && n->af != *af) {
			/* unlink and free n */
			struct node_host *next = n->next;

			/* adjust tail pointer */
			if (n == (*nh)->tail)
				(*nh)->tail = prev;
			/* adjust previous node's next pointer */
			if (prev == NULL)
				*nh = next;
			else
				prev->next = next;
			/* free node */
			if (n->ifname != NULL)
				free(n->ifname);
			free(n);
			n = next;
		} else {
			if (n->af && !*af)
				*af = n->af;
			prev = n;
			n = n->next;
		}
	}
}

int
invalid_redirect(struct node_host *nh, sa_family_t af)
{
	if (!af) {
		struct node_host *n;

		/* tables and dyniftl are ok without an address family */
		for (n = nh; n != NULL; n = n->next) {
			if (n->addr.type != PF_ADDR_TABLE &&
			    n->addr.type != PF_ADDR_DYNIFTL) {
				yyerror("address family not given and "
				    "translation address expands to multiple "
				    "address families");
				return (1);
			}
		}
	}
	if (nh == NULL) {
		yyerror("no translation address with matching address family "
		    "found.");
		return (1);
	}
	return (0);
}

int
atoul(char *s, u_long *ulvalp)
{
	u_long	 ulval;
	char	*ep;

	errno = 0;
	ulval = strtoul(s, &ep, 0);
	if (s[0] == '\0' || *ep != '\0')
		return (-1);
	if (errno == ERANGE && ulval == ULONG_MAX)
		return (-1);
	*ulvalp = ulval;
	return (0);
}

int
getservice(char *n)
{
	struct servent	*s;
	u_long		 ulval;

	if (atoul(n, &ulval) == 0) {
		if (ulval > 65535) {
			yyerror("illegal port value %lu", ulval);
			return (-1);
		}
		return (htons(ulval));
	} else {
		s = getservbyname(n, "tcp");
		if (s == NULL)
			s = getservbyname(n, "udp");
		if (s == NULL) {
			yyerror("unknown port %s", n);
			return (-1);
		}
		return (s->s_port);
	}
}

int
rule_label(struct pf_rule *r, char *s)
{
	if (s) {
		if (strlcpy(r->label, s, sizeof(r->label)) >=
		    sizeof(r->label)) {
			yyerror("rule label too long (max %d chars)",
			    sizeof(r->label)-1);
			return (-1);
		}
	}
	return (0);
}

u_int16_t
parseicmpspec(char *w, sa_family_t af)
{
	const struct icmpcodeent	*p;
	u_long				 ulval;
	u_int8_t			 icmptype;

	if (af == AF_INET)
		icmptype = returnicmpdefault >> 8;
	else
		icmptype = returnicmp6default >> 8;

	if (atoul(w, &ulval) == -1) {
		if ((p = geticmpcodebyname(icmptype, w, af)) == NULL) {
			yyerror("unknown icmp code %s", w);
			return (0);
		}
		ulval = p->code;
	}
	if (ulval > 255) {
		yyerror("invalid icmp code %lu", ulval);
		return (0);
	}
	return (icmptype << 8 | ulval);
}

int
parseport(char *port, struct range *r, int extensions)
{
	char	*p = strchr(port, ':');

	if (p == NULL) {
		if ((r->a = getservice(port)) == -1)
			return (-1);
		r->b = 0;
		r->t = PF_OP_NONE;
		return (0);
	}
	if ((extensions & PPORT_STAR) && !strcmp(p+1, "*")) {
		*p = 0;
		if ((r->a = getservice(port)) == -1)
			return (-1);
		r->b = 0;
		r->t = PF_OP_IRG;
		return (0);
	}
	if ((extensions & PPORT_RANGE)) {
		*p++ = 0;
		if ((r->a = getservice(port)) == -1 ||
		    (r->b = getservice(p)) == -1)
			return (-1);
		if (r->a == r->b) {
			r->b = 0;
			r->t = PF_OP_NONE;
		} else
			r->t = PF_OP_RRG;
		return (0);
	}
	return (-1);
}

int
pfctl_load_anchors(int dev, struct pfctl *pf, struct pfr_buffer *trans)
{
	struct loadanchors	*la;

	TAILQ_FOREACH(la, &loadanchorshead, entries) {
		if (pf->opts & PF_OPT_VERBOSE)
			fprintf(stderr, "\nLoading anchor %s from %s\n",
			    la->anchorname, la->filename);
		if (pfctl_rules(dev, la->filename, pf->opts, pf->optimize,
		    la->anchorname, trans) == -1)
			return (-1);
	}

	return (0);
}

int
kw_casecmp(const void *k, const void *e)
{
	return (strcasecmp(k, ((const struct keywords *)e)->k_name));
}

int
map_tos(char *s, int *val)
{
	/* DiffServ Codepoints and other TOS mappings */
	const struct keywords	 toswords[] = {
		{ "af11",		IPTOS_DSCP_AF11 },
		{ "af12",		IPTOS_DSCP_AF12 },
		{ "af13",		IPTOS_DSCP_AF13 },
		{ "af21",		IPTOS_DSCP_AF21 },
		{ "af22",		IPTOS_DSCP_AF22 },
		{ "af23",		IPTOS_DSCP_AF23 },
		{ "af31",		IPTOS_DSCP_AF31 },
		{ "af32",		IPTOS_DSCP_AF32 },
		{ "af33",		IPTOS_DSCP_AF33 },
		{ "af41",		IPTOS_DSCP_AF41 },
		{ "af42",		IPTOS_DSCP_AF42 },
		{ "af43",		IPTOS_DSCP_AF43 },
		{ "critical",		IPTOS_PREC_CRITIC_ECP },
		{ "cs0",		IPTOS_DSCP_CS0 },
		{ "cs1",		IPTOS_DSCP_CS1 },
		{ "cs2",		IPTOS_DSCP_CS2 },
		{ "cs3",		IPTOS_DSCP_CS3 },
		{ "cs4",		IPTOS_DSCP_CS4 },
		{ "cs5",		IPTOS_DSCP_CS5 },
		{ "cs6",		IPTOS_DSCP_CS6 },
		{ "cs7",		IPTOS_DSCP_CS7 },
		{ "ef",			IPTOS_DSCP_EF },
		{ "inetcontrol",	IPTOS_PREC_INTERNETCONTROL },
		{ "lowdelay",		IPTOS_LOWDELAY },
		{ "netcontrol",		IPTOS_PREC_NETCONTROL },
		{ "reliability",	IPTOS_RELIABILITY },
		{ "throughput",		IPTOS_THROUGHPUT }
	};
	const struct keywords	*p;

	p = bsearch(s, toswords, sizeof(toswords)/sizeof(toswords[0]),
	    sizeof(toswords[0]), kw_casecmp);

	if (p) {
		*val = p->k_val;
		return (1);
	}
	return (0);
}

int
rt_tableid_max(void)
{
#ifdef __FreeBSD__
	int fibs;
	size_t l = sizeof(fibs);

        if (sysctlbyname("net.fibs", &fibs, &l, NULL, 0) == -1)
		fibs = 16;	/* XXX RT_MAXFIBS, at least limit it some. */
	/*
	 * As the OpenBSD code only compares > and not >= we need to adjust
	 * here given we only accept values of 0..n and want to avoid #ifdefs
	 * in the grammar.
	 */
	return (fibs - 1);
#else
	return (RT_TABLEID_MAX);
#endif
}
