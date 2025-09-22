/*	$OpenBSD: parse.y,v 1.719 2025/06/27 16:53:51 sashan Exp $	*/

/*
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
 * Copyright (c) 2002 - 2013 Henning Brauer <henning@openbsd.org>
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <net/pfvar.h>
#include <arpa/inet.h>

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
static u_int16_t	 returnicmpdefault =
			    (ICMP_UNREACH << 8) | ICMP_UNREACH_PORT;
static u_int16_t	 returnicmp6default =
			    (ICMP6_DST_UNREACH << 8) | ICMP6_DST_UNREACH_NOPORT;
static int		 blockpolicy = PFRULE_DROP;
static int		 default_statelock;

TAILQ_HEAD(files, file)		 files = TAILQ_HEAD_INITIALIZER(files);
static struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	size_t			 ungetpos;
	size_t			 ungetsize;
	u_char			*ungetbuf;
	int			 eof_reached;
	int			 lineno;
	int			 errors;
} *file, *topfile;
struct file	*pushfile(const char *, int);
int		 popfile(void);
int		 check_file_secrecy(int, const char *);
int		 yyparse(void);
int		 yylex(void);
int		 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)))
    __attribute__((__nonnull__ (1)));
int		 kw_cmp(const void *, const void *);
int		 lookup(char *);
int		 igetc(void);
int		 lgetc(int);
void		 lungetc(int);
int		 findeol(void);

TAILQ_HEAD(symhead, sym)	 symhead = TAILQ_HEAD_INITIALIZER(symhead);
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
	u_int16_t		 code;	/* aux. value 256 is legit */
	u_int16_t		 type;	/* aux. value 256 is legit */
	u_int8_t		 proto;
	struct node_icmp	*next;
	struct node_icmp	*tail;
};

enum	{ PF_STATE_OPT_MAX, PF_STATE_OPT_NOSYNC, PF_STATE_OPT_SRCTRACK,
	    PF_STATE_OPT_MAX_SRC_STATES, PF_STATE_OPT_MAX_SRC_CONN,
	    PF_STATE_OPT_MAX_SRC_CONN_RATE, PF_STATE_OPT_MAX_SRC_NODES,
	    PF_STATE_OPT_OVERLOAD, PF_STATE_OPT_STATELOCK,
	    PF_STATE_OPT_TIMEOUT, PF_STATE_OPT_SLOPPY,
	    PF_STATE_OPT_PFLOW };

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

struct node_queue {
	char			 queue[PF_QNAME_SIZE];
	char			 parent[PF_QNAME_SIZE];
	char			 ifname[IFNAMSIZ];
	int			 scheduler;
	struct node_queue	*next;
	struct node_queue	*tail;
};

struct node_qassign {
	char		*qname;
	char		*pqname;
};

struct range {
	int		 a;
	int		 b;
	int		 t;
};
struct redirection {
	struct node_host	*host;
	struct range		 rport;
};

struct pool_opts {
	int			 marker;
#define POM_TYPE		0x01
#define POM_STICKYADDRESS	0x02
	u_int8_t		 opts;
	int			 type;
	int			 staticport;
	struct pf_poolhashkey	*key;

} pool_opts;

struct divertspec {
	struct node_host	*addr;
	u_int16_t		 port;
	enum pf_divert_types	 type;
};

struct redirspec {
	struct redirection	*rdr;
	struct pool_opts	 pool_opts;
	int			 binat;
	int			 af;
};

struct filter_opts {
	int			 marker;
#define FOM_FLAGS	0x0001
#define FOM_ICMP	0x0002
#define FOM_TOS		0x0004
#define FOM_KEEP	0x0008
#define FOM_SRCTRACK	0x0010
#define FOM_MINTTL	0x0020
#define FOM_MAXMSS	0x0040
#define FOM_AFTO	0x0080
#define FOM_SETTOS	0x0100
#define FOM_SCRUB_TCP	0x0200
#define FOM_SETPRIO	0x0400
#define FOM_ONCE	0x1000
#define FOM_PRIO	0x2000
#define FOM_SETDELAY	0x4000
	struct node_uid		*uid;
	struct node_gid		*gid;
	struct node_if		*rcv;
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
	u_int16_t		 delay;
	struct divertspec	 divert;
	struct redirspec	 nat;
	struct redirspec	 rdr;
	struct redirspec	 rroute;
	u_int8_t		 rt;

	/* scrub opts */
	int			 nodf;
	int			 minttl;
	int			 settos;
	int			 randomid;
	int			 max_mss;

	struct {
		u_int32_t	limit;
		u_int32_t	seconds;
	}			 pktrate;
} filter_opts;

struct antispoof_opts {
	char			*label;
	u_int			 rtableid;
} antispoof_opts;

struct scrub_opts {
	int			marker;
	int			nodf;
	int			minttl;
	int			maxmss;
	int			randomid;
	int			reassemble_tcp;
} scrub_opts;

struct node_sc {
	struct node_queue_bw	m1;
	u_int			d;
	struct node_queue_bw	m2;
};

struct node_fq {
	u_int			flows;
	u_int			quantum;
	u_int			target;
	u_int			interval;
};

struct queue_opts {
	int		 marker;
#define	QOM_BWSPEC	0x01
#define	QOM_PARENT	0x02
#define	QOM_DEFAULT	0x04
#define	QOM_QLIMIT	0x08
#define	QOM_FLOWS	0x10
#define	QOM_QUANTUM	0x20
	struct node_sc	 realtime;
	struct node_sc	 linkshare;
	struct node_sc	 upperlimit;
	struct node_fq	 flowqueue;
	char		*parent;
	int		 flags;
	u_int		 qlimit;
} queue_opts;

struct table_opts {
	int			flags;
	int			init_addr;
	struct node_tinithead	init_nodes;
} table_opts;

struct node_hfsc_opts	 hfsc_opts;
struct node_state_opt	*keep_state_defaults = NULL;
struct pfctl_watermarks	 syncookie_opts;

int		 validate_range(u_int8_t, u_int16_t, u_int16_t);
int		 disallow_table(struct node_host *, const char *);
int		 disallow_urpf_failed(struct node_host *, const char *);
int		 disallow_alias(struct node_host *, const char *);
int		 rule_consistent(struct pf_rule *);
int		 process_tabledef(char *, struct table_opts *, int);
void		 expand_label_str(char *, size_t, const char *, const char *);
void		 expand_label_if(const char *, char *, size_t, const char *);
void		 expand_label_addr(const char *, char *, size_t, u_int8_t,
		    struct node_host *);
void		 expand_label_port(const char *, char *, size_t,
		    struct node_port *);
void		 expand_label_proto(const char *, char *, size_t, u_int8_t);
void		 expand_label(char *, size_t, const char *, u_int8_t,
		    struct node_host *, struct node_port *, struct node_host *,
		    struct node_port *, u_int8_t);
int		 expand_divertspec(struct pf_rule *, struct divertspec *);
int		 collapse_redirspec(struct pf_pool *, struct pf_rule *,
		    struct redirspec *rs, int);
int		 apply_redirspec(struct pf_pool *, struct pf_rule *,
		    struct redirspec *, int, struct node_port *);
void		 expand_rule(struct pf_rule *, int, struct node_if *,
		    struct redirspec *, struct redirspec *, struct redirspec *,
		    struct node_proto *,
		    struct node_os *, struct node_host *, struct node_port *,
		    struct node_host *, struct node_port *, struct node_uid *,
		    struct node_gid *, struct node_if *, struct node_icmp *);
int		 expand_queue(char *, struct node_if *, struct queue_opts *);
int		 expand_skip_interface(struct node_if *);

int	 getservice(char *);
int	 rule_label(struct pf_rule *, char *);

void	 mv_rules(struct pf_ruleset *, struct pf_ruleset *);
void	 mv_tables(struct pfctl *, struct pfr_ktablehead *,
		    struct pf_anchor *, struct pf_anchor *);
void	 decide_address_family(struct node_host *, sa_family_t *);
int	 invalid_redirect(struct node_host *, sa_family_t);
u_int16_t parseicmpspec(char *, sa_family_t);
int	 kw_casecmp(const void *, const void *);
int	 map_tos(char *string, int *);
int	 lookup_rtable(u_int);
int	 filteropts_to_rule(struct pf_rule *, struct filter_opts *);

TAILQ_HEAD(loadanchorshead, loadanchors)
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
		u_int16_t		 weight;
		struct {
			u_int8_t	 b1;
			u_int8_t	 b2;
			u_int16_t	 w;
			u_int16_t	 w2;
		}			 b;
		struct range		 range;
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
		struct redirection	*redirection;
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
		struct node_sc		 sc;
		struct filter_opts	 filter_opts;
		struct antispoof_opts	 antispoof_opts;
		struct queue_opts	 queue_opts;
		struct scrub_opts	 scrub_opts;
		struct table_opts	 table_opts;
		struct pool_opts	 pool_opts;
		struct node_hfsc_opts	 hfsc_opts;
		struct pfctl_watermarks	*watermarks;
	} v;
	int lineno;
} YYSTYPE;

#define PPORT_RANGE	1
#define PPORT_STAR	2
int	parseport(char *, struct range *r, int);

#define DYNIF_MULTIADDR(addr) ((addr).type == PF_ADDR_DYNIFTL && \
	(!((addr).iflags & PFI_AFLAG_NOALIAS) ||		 \
	!isdigit((unsigned char)(addr).v.ifname[strlen((addr).v.ifname)-1])))

%}

%token	PASS BLOCK MATCH SCRUB RETURN IN OS OUT LOG QUICK ON FROM TO FLAGS
%token	RETURNRST RETURNICMP RETURNICMP6 PROTO INET INET6 ALL ANY ICMPTYPE
%token	ICMP6TYPE CODE KEEP MODULATE STATE PORT BINATTO NODF
%token	MINTTL ERROR ALLOWOPTS FILENAME ROUTETO DUPTO REPLYTO NO LABEL
%token	NOROUTE URPFFAILED FRAGMENT USER GROUP MAXMSS MAXIMUM TTL TOS DROP TABLE
%token	REASSEMBLE ANCHOR SYNCOOKIES
%token	SET OPTIMIZATION TIMEOUT LIMIT LOGINTERFACE BLOCKPOLICY RANDOMID
%token	SYNPROXY FINGERPRINTS NOSYNC DEBUG SKIP HOSTID
%token	ANTISPOOF FOR INCLUDE MATCHES
%token	BITMASK RANDOM SOURCEHASH ROUNDROBIN LEASTSTATES STATICPORT PROBABILITY
%token	WEIGHT BANDWIDTH FLOWS QUANTUM
%token	QUEUE PRIORITY QLIMIT RTABLE RDOMAIN MINIMUM BURST PARENT
%token	LOAD RULESET_OPTIMIZATION RTABLE RDOMAIN PRIO ONCE DEFAULT DELAY
%token	STICKYADDRESS MAXSRCSTATES MAXSRCNODES SOURCETRACK GLOBAL RULE
%token	MAXSRCCONN MAXSRCCONNRATE OVERLOAD FLUSH SLOPPY PFLOW MAXPKTRATE
%token	TAGGED TAG IFBOUND FLOATING STATEPOLICY STATEDEFAULTS ROUTE
%token	DIVERTTO DIVERTREPLY DIVERTPACKET NATTO AFTO RDRTO RECEIVEDON NE LE GE
%token	<v.string>		STRING
%token	<v.number>		NUMBER
%token	<v.i>			PORTBINARY
%type	<v.interface>		interface if_list if_item_not if_item
%type	<v.number>		number icmptype icmp6type uid gid
%type	<v.number>		tos not yesno optnodf
%type	<v.probability>		probability
%type	<v.weight>		optweight
%type	<v.i>			dir af optimizer syncookie_val
%type	<v.i>			sourcetrack flush unaryop statelock
%type	<v.b>			action
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
%type	<v.host>		ipspec xhost host dynaddr host_list
%type	<v.host>		table_host_list tablespec
%type	<v.host>		redir_host_list redirspec
%type	<v.os>			os xos os_list
%type	<v.port>		portspec port_list port_item
%type	<v.uid>			uids uid_list uid_item
%type	<v.gid>			gids gid_list gid_item
%type	<v.redirection>		redirpool
%type	<v.string>		label stringall anchorname
%type	<v.string>		string varstring numberstring
%type	<v.keep_state>		keep
%type	<v.state_opt>		state_opt_spec state_opt_list state_opt_item
%type	<v.logquick>		logquick quick log logopts logopt
%type	<v.interface>		antispoof_ifspc antispoof_iflst antispoof_if
%type	<v.qassign>		qname
%type	<v.queue_bwspec>	bandwidth
%type	<v.filter_opts>		filter_opts filter_opt filter_opts_l
%type	<v.filter_opts>		filter_sets filter_set filter_sets_l
%type	<v.antispoof_opts>	antispoof_opts antispoof_opt antispoof_opts_l
%type	<v.queue_opts>		queue_opts queue_opt queue_opts_l optscs
%type	<v.sc>			scspec
%type	<v.scrub_opts>		scrub_opts scrub_opt scrub_opts_l
%type	<v.table_opts>		table_opts table_opt table_opts_l
%type	<v.pool_opts>		pool_opts pool_opt pool_opts_l
%type	<v.watermarks>		syncookie_opts
%%

ruleset		: /* empty */
		| ruleset include '\n'
		| ruleset '\n'
		| ruleset option '\n'
		| ruleset pfrule '\n'
		| ruleset anchorrule '\n'
		| ruleset loadrule '\n'
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
 * apply to previously specified rule: must be careful to note
 * what that is: pf or nat or binat or rdr
 */
fakeanchor	: fakeanchor '\n'
		| fakeanchor anchorrule '\n'
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

optnodf		: /* empty */	{ $$ = 0; }
		| NODF		{ $$ = 1; }
		;

option		: SET REASSEMBLE yesno optnodf		{
			pfctl_set_reassembly(pf, $3, $4);
		}
		| SET OPTIMIZATION STRING		{
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
			pfctl_set_hostid(pf, $3);
		}
		| SET BLOCKPOLICY DROP	{
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set block-policy drop\n");
			blockpolicy = PFRULE_DROP;
		}
		| SET BLOCKPOLICY RETURN {
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set block-policy return\n");
			blockpolicy = PFRULE_RETURN;
		}
		| SET FINGERPRINTS STRING {
			if (pf->opts & PF_OPT_VERBOSE)
				printf("set fingerprints \"%s\"\n", $3);
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
			if (pfctl_set_debug(pf, $3) != 0) {
				yyerror("error setting debuglevel %s", $3);
				free($3);
				YYERROR;
			}
			free($3);
		}
		| SET DEBUG DEBUG {
			if (pfctl_set_debug(pf, "debug") != 0) {
				yyerror("error setting debuglevel %s", "debug");
				YYERROR;
			}
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
		| SET SYNCOOKIES syncookie_val syncookie_opts {
			if (pfctl_set_syncookies(pf, $3, $4)) {
				yyerror("error setting syncookies");
				YYERROR;
			}
		}
		;

syncookie_val	: STRING	{
			if (!strcmp($1, "never"))
				$$ = PF_SYNCOOKIES_NEVER;
			else if (!strcmp($1, "adaptive"))
				$$ = PF_SYNCOOKIES_ADAPTIVE;
			else if (!strcmp($1, "always"))
				$$ = PF_SYNCOOKIES_ALWAYS;
			else {
				yyerror("illegal value for syncookies");
				YYERROR;
			}
		}
		;

syncookie_opts	: /* empty */			{ $$ = NULL; }
		| {
			memset(&syncookie_opts, 0, sizeof(syncookie_opts));
		  } '(' syncookie_opt_l ')'	{ $$ = &syncookie_opts; }
		;

syncookie_opt_l	: syncookie_opt_l comma syncookie_opt
		| syncookie_opt
		;

syncookie_opt	: STRING STRING {
			double	 val;
			char	*cp;

			val = strtod($2, &cp);
			if (cp == NULL || strcmp(cp, "%"))
				YYERROR;
			if (val <= 0 || val > 100) {
				yyerror("illegal percentage value");
				YYERROR;
			}
			if (!strcmp($1, "start")) {
				syncookie_opts.hi = val;
			} else if (!strcmp($1, "end")) {
				syncookie_opts.lo = val;
			} else {
				yyerror("illegal syncookie option");
				YYERROR;
			}
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

varstring	: numberstring varstring		{
			if (asprintf(&$$, "%s %s", $1, $2) == -1)
				err(1, "string: asprintf");
			free($1);
			free($2);
		}
		| numberstring
		;

numberstring	: NUMBER				{
			char	*s;
			if (asprintf(&s, "%lld", $1) == -1) {
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
					free($1);
					free($3);
					YYERROR;
				}
			}
			if (symset($1, $3, 0) == -1)
				err(1, "cannot store variable %s", $1);
			free($1);
			free($3);
		}
		;

anchorname	: STRING			{
			if ($1[0] == '\0') {
				free($1);
				yyerror("anchor name must not be empty");
				YYERROR;
			}
			if (strlen(pf->anchor->path) + 1 +
			    strlen($1) >= PATH_MAX) {
				free($1);
				yyerror("anchor name is longer than %u",
				    PATH_MAX - 1);
				YYERROR;
			}
			if ($1[0] == '_' || strstr($1, "/_") != NULL) {
				free($1);
				yyerror("anchor names beginning with '_' "
				    "are reserved for internal use");
				YYERROR;
			}
			$$ = $1;
		}
		| /* empty */			{ $$ = NULL; }
		;

pfa_anchorlist	: /* empty */
		| pfa_anchorlist '\n'
		| pfa_anchorlist tabledef '\n'
		| pfa_anchorlist pfrule '\n'
		| pfa_anchorlist anchorrule '\n'
		| pfa_anchorlist include '\n'
		;

pfa_anchor	: '{'
		{
			char ta[PF_ANCHOR_NAME_SIZE];
			struct pf_ruleset *rs;

			/* steping into a brace anchor */
			pf->asd++;
			if (pf->asd >= PFCTL_ANCHOR_STACK_DEPTH)
				errx(1, "pfa_anchor: anchors too deep");
			pf->bn++;
			pf->brace = 1;

			/*
			 * Anchor contents are parsed before the anchor rule
			 * production completes, so we don't know the real
			 * location yet. Create a holding ruleset in the root;
			 * contents will be moved afterwards.
			 */
			snprintf(ta, PF_ANCHOR_NAME_SIZE, "_%d", pf->bn);
			rs = pf_find_or_create_ruleset(ta);
			if (rs == NULL)
				err(1, "pfa_anchor: pf_find_or_create_ruleset (%s)", ta);
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
			char	*p;

			memset(&r, 0, sizeof(r));
			if (pf->astack[pf->asd + 1]) {
				if ($2 && strchr($2, '/') != NULL) {
					free($2);
					yyerror("anchor paths containing '/' "
					    "cannot be used for inline anchors.");
					YYERROR;
				}

				/* Move inline rules into relative location. */
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
					mv_tables(pf, &pfr_ktables, r.anchor, pf->alast);
				}
				pf_remove_if_empty_ruleset(&pf->alast->ruleset);
				pf->alast = r.anchor;
			} else {
				if (!$2) {
					yyerror("anchors without explicit "
					    "rules must specify a name");
					YYERROR;
				}

				/*
				 * Don't make non-brace anchors part of the main anchor pool.
				 */
				if ((r.anchor = calloc(1, sizeof(*r.anchor))) == NULL) {
					err(1, "anchorrule: calloc");
				}
				pf_init_ruleset(&r.anchor->ruleset);
				r.anchor->ruleset.anchor = r.anchor;
				if (strlcpy(r.anchor->path, $2,
				    sizeof(r.anchor->path)) >= sizeof(r.anchor->path)) {
					errx(1, "anchorrule: strlcpy");
				}
				if ((p = strrchr($2, '/')) != NULL) {
					if (strlen(p) == 1) {
						yyerror("anchorrule: bad anchor name %s",
						    $2);
						YYERROR;
					}
				} else
					p = $2;
				if (strlcpy(r.anchor->name, p,
				    sizeof(r.anchor->name)) >= sizeof(r.anchor->name)) {
					errx(1, "anchorrule: strlcpy");
				}
			}

			r.direction = $3;
			r.quick = $4.quick;
			r.af = $6;

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

			if (filteropts_to_rule(&r, &$9))
				YYERROR;

			if ($9.keep.action) {
				yyerror("cannot specify state handling "
				    "on anchors");
				YYERROR;
			}

			if ($9.rt) {
				yyerror("cannot specify route handling "
				    "on anchors");
				YYERROR;
			}

			decide_address_family($8.src.host, &r.af);
			decide_address_family($8.dst.host, &r.af);

			expand_rule(&r, 0, $5, NULL, NULL, NULL, $7, $8.src_os,
			    $8.src.host, $8.src.port, $8.dst.host, $8.dst.port,
			    $9.uid, $9.gid, $9.rcv, $9.icmpspec);
			free($2);
			pf->astack[pf->asd + 1] = NULL;
		}
		;

loadrule	: LOAD ANCHOR anchorname FROM string	{
			struct loadanchors	*loadanchor;

			if ($3 == NULL) {
				yyerror("anchor name is missing");
				YYERROR;
			}
			loadanchor = calloc(1, sizeof(struct loadanchors));
			if (loadanchor == NULL)
				err(1, "loadrule: calloc");
			if ((loadanchor->anchorname = malloc(PATH_MAX)) ==
			    NULL)
				err(1, "loadrule: malloc");
			if (pf->anchor->name[0])
				snprintf(loadanchor->anchorname, PATH_MAX,
				    "%s/%s", pf->anchor->path, $3);
			else
				strlcpy(loadanchor->anchorname, $3, PATH_MAX);
			if ((loadanchor->filename = strdup($5)) == NULL)
				err(1, "loadrule: strdup");

			TAILQ_INSERT_TAIL(&loadanchorshead, loadanchor,
			    entries);

			free($3);
			free($5);
		};

scrub_opts	:	{
				bzero(&scrub_opts, sizeof scrub_opts);
			}
		    scrub_opts_l
			{ $$ = scrub_opts; }
		;

scrub_opts_l	: scrub_opts_l comma scrub_opt
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
			if (scrub_opts.marker & FOM_MINTTL) {
				yyerror("min-ttl cannot be respecified");
				YYERROR;
			}
			if ($2 < 0 || $2 > 255) {
				yyerror("illegal min-ttl value %lld", $2);
				YYERROR;
			}
			scrub_opts.marker |= FOM_MINTTL;
			scrub_opts.minttl = $2;
		}
		| MAXMSS NUMBER {
			if (scrub_opts.marker & FOM_MAXMSS) {
				yyerror("max-mss cannot be respecified");
				YYERROR;
			}
			if ($2 < 0 || $2 > 65535) {
				yyerror("illegal max-mss value %lld", $2);
				YYERROR;
			}
			scrub_opts.marker |= FOM_MAXMSS;
			scrub_opts.maxmss = $2;
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
		;

antispoof	: ANTISPOOF logquick antispoof_ifspc af antispoof_opts {
			struct pf_rule		 r;
			struct node_host	*h = NULL, *hh;
			struct node_if		*i, *j;

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
					expand_rule(&r, 0, j, NULL, NULL, NULL,
					    NULL, NULL, h, NULL, NULL, NULL,
					    NULL, NULL, NULL, NULL);

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
						expand_rule(&r, 0, NULL, NULL,
						    NULL, NULL, NULL, NULL, h,
						    NULL, NULL, NULL, NULL,
						    NULL, NULL, NULL);
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

antispoof_opt	: LABEL label	{
			if (antispoof_opts.label) {
				yyerror("label cannot be redefined");
				YYERROR;
			}
			antispoof_opts.label = $2;
		}
		| RTABLE NUMBER				{
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("invalid rtable id");
				YYERROR;
			} else if (!lookup_rtable($2)) {
				yyerror("rtable %lld does not exist", $2);
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
			if (process_tabledef($3, &$5, pf->opts)) {
				free($3);
				YYERROR;
			}
			free($3);
			for (ti = SIMPLEQ_FIRST(&$5.init_nodes); ti != NULL;
			    ti = nti) {
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
		| '{' optnl table_host_list '}'	{
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

tablespec	: xhost	optweight		{
			if ($2 > 0) {
				struct node_host	*n;
				for (n = $1; n != NULL; n = n->next)
					n->weight = $2;
			}
			$$ = $1;
		}
		| '{' optnl table_host_list '}'	{ $$ = $3; }
		;

table_host_list	: tablespec optnl			{ $$ = $1; }
		| table_host_list comma tablespec optnl {
			$1->tail->next = $3;
			$1->tail = $3->tail;
			$$ = $1;
		}
		;

queuespec	: QUEUE STRING interface queue_opts		{
			struct node_host	*n;

			if ($3 == NULL && $4.parent == NULL) {
				yyerror("root queue without interface");
				YYERROR;
			}
			if ($3 != NULL &&
			    ((n = ifa_exists($3->ifname)) == NULL ||
			     n->af != AF_LINK)) {
				yyerror("not an interface");
				YYERROR;
			}

			expand_queue($2, $3, &$4);
		}
		;

queue_opts	:	{
			bzero(&queue_opts, sizeof queue_opts);
		}
		    queue_opts_l
			{ $$ = queue_opts; }
		;

queue_opts_l	: queue_opts_l queue_opt
		| queue_opt
		;

queue_opt	: BANDWIDTH scspec optscs			{
			if (queue_opts.marker & QOM_BWSPEC) {
				yyerror("bandwidth cannot be respecified");
				YYERROR;
			}
			queue_opts.marker |= QOM_BWSPEC;
			queue_opts.linkshare = $2;
			queue_opts.realtime= $3.realtime;
			queue_opts.upperlimit = $3.upperlimit;
		}
		| PARENT STRING					{
			if (queue_opts.marker & QOM_PARENT) {
				yyerror("parent cannot be respecified");
				YYERROR;
			}
			queue_opts.marker |= QOM_PARENT;
			queue_opts.parent = $2;
		}
		| DEFAULT					{
			if (queue_opts.marker & QOM_DEFAULT) {
				yyerror("default cannot be respecified");
				YYERROR;
			}
			queue_opts.marker |= QOM_DEFAULT;
			queue_opts.flags |= PFQS_DEFAULT;
		}
		| QLIMIT NUMBER					{
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
		| FLOWS NUMBER					{
			if (queue_opts.marker & QOM_FLOWS) {
				yyerror("number of flows cannot be respecified");
				YYERROR;
			}
			if ($2 < 1 || $2 > 32767) {
				yyerror("number of flows out of range: "
				    "max 32767");
				YYERROR;
			}
			queue_opts.marker |= QOM_FLOWS;
			queue_opts.flags |= PFQS_FLOWQUEUE;
			queue_opts.flowqueue.flows = $2;
		}
		| QUANTUM NUMBER				{
			if (queue_opts.marker & QOM_QUANTUM) {
				yyerror("quantum cannot be respecified");
				YYERROR;
			}
			if ($2 < 1 || $2 > 65535) {
				yyerror("quantum out of range: max 65535");
				YYERROR;
			}
			queue_opts.marker |= QOM_QUANTUM;
			queue_opts.flowqueue.quantum = $2;
		}
		;

optscs		: /* nada */					{

		}
		| comma MINIMUM scspec				{
			$$.realtime = $3;
		}
		| comma MAXIMUM scspec				{
			$$.upperlimit = $3;
		}
		| comma MINIMUM scspec comma MAXIMUM scspec	{
			$$.realtime = $3;
			$$.upperlimit = $6;
		}
		| comma MAXIMUM scspec comma MINIMUM scspec	{
			$$.realtime = $6;
			$$.upperlimit = $3;
		}
		;

scspec		: bandwidth					{
			$$.m2 = $1;
			$$.d = 0;
			if ($$.m2.bw_percent) {
				yyerror("no bandwidth in %% yet");
				YYERROR;
			}
		}
		| bandwidth BURST bandwidth FOR STRING	{
			u_long	 ul;
			char	*cp;

			ul = strtoul($5, &cp, 10);
			if (cp == NULL || strcmp(cp, "ms")) {
				yyerror("time in scspec must be in ms");
				YYERROR;
			}

			$$.m1 = $3;
			$$.d = ul;
			$$.m2 = $1;

			if ($$.m1.bw_percent || $$.m2.bw_percent) {
				yyerror("no bandwidth in %% yet");
				YYERROR;
			}
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
					yyerror("unknown unit \"%s\"", cp);
					free($1);
					YYERROR;
				}
			}
			free($1);
			if (bps < 0 || bps > (double)LLONG_MAX) {
				yyerror("bandwidth number too big");
				YYERROR;
			}
			$$.bw_absolute = (u_int64_t)bps;
		}
		| NUMBER {
			if ($1 < 0 || $1 > LLONG_MAX) {
				yyerror("bandwidth number too big");
				YYERROR;
			}
			$$.bw_percent = 0;
			$$.bw_absolute = $1;
		}
		;

pfrule		: action dir logquick interface af proto fromto
		    filter_opts
		{
			struct pf_rule		 r;
			struct node_state_opt	*o;
			struct node_proto	*proto;
			int			 srctrack = 0;
			int			 statelock = 0;
			int			 adaptive = 0;
			int			 defaults = 0;

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
			r.af = $5;

			if (filteropts_to_rule(&r, &$8))
				YYERROR;

			if ($8.flags.b1 || $8.flags.b2 || $7.src_os) {
				for (proto = $6; proto != NULL &&
				    proto->proto != IPPROTO_TCP;
				    proto = proto->next)
					;	/* nothing */
				if (proto == NULL && $6 != NULL) {
					if ($8.flags.b1 || $8.flags.b2)
						yyerror(
						    "flags only apply to tcp");
					if ($7.src_os)
						yyerror(
						    "OS fingerprinting only "
						    "apply to tcp");
					YYERROR;
				}
			}

			r.keep_state = $8.keep.action;
			o = $8.keep.options;

			/* 'keep state' by default on pass rules. */
			if (!r.keep_state && !r.action &&
			    !($8.marker & FOM_KEEP)) {
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
				case PF_STATE_OPT_PFLOW:
					if (r.rule_flag & PFRULE_PFLOW) {
						yyerror("state pflow "
						    "option: multiple "
						    "definitions");
						YYERROR;
					}
					r.rule_flag |= PFRULE_PFLOW;
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
			    !$8.fragment && !($8.marker & FOM_FLAGS) &&
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

			decide_address_family($7.src.host, &r.af);
			decide_address_family($7.dst.host, &r.af);

			if ($8.rt) {
				if ($8.rt != PF_DUPTO && !r.direction) {
					yyerror("direction must be explicit "
					    "with rules that specify routing");
					YYERROR;
				}
				r.rt = $8.rt;
			}

			if (expand_divertspec(&r, &$8.divert))
				YYERROR;

			expand_rule(&r, 0, $4, &$8.nat, &$8.rdr, &$8.rroute, $6,
			    $7.src_os,
			    $7.src.host, $7.src.port, $7.dst.host, $7.dst.port,
			    $8.uid, $8.gid, $8.rcv, $8.icmpspec);
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
			if ($2 < 0 || $2 > IFQ_MAXPRIO) {
				yyerror("prio must be 0 - %u", IFQ_MAXPRIO);
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
		| LABEL label	{
			if (filter_opts.label) {
				yyerror("label cannot be redefined");
				YYERROR;
			}
			filter_opts.label = $2;
		}
		| QUEUE qname	{
			if (filter_opts.queues.qname) {
				yyerror("queue cannot be redefined");
				YYERROR;
			}
			filter_opts.queues = $2;
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
				yyerror("invalid probability: %g%%", $2 * 100);
				YYERROR;
			}
			filter_opts.prob = (u_int32_t)p;
			if (filter_opts.prob == 0)
				filter_opts.prob = 1;
		}
		| RTABLE NUMBER				{
			if ($2 < 0 || $2 > RT_TABLEID_MAX) {
				yyerror("invalid rtable id");
				YYERROR;
			} else if (!lookup_rtable($2)) {
				yyerror("rtable %lld does not exist", $2);
				YYERROR;
			}
			filter_opts.rtableid = $2;
		}
		| DIVERTTO STRING PORT portplain {
			if (filter_opts.divert.type != PF_DIVERT_NONE) {
				yyerror("more than one divert option");
				YYERROR;
			}
			filter_opts.divert.type = PF_DIVERT_TO;
			if ((filter_opts.divert.addr = host($2, pf->opts)) == NULL) {
				yyerror("could not parse divert address: %s",
				    $2);
				free($2);
				YYERROR;
			}
			free($2);
			filter_opts.divert.port = $4.a;
			if (!filter_opts.divert.port) {
				yyerror("invalid divert port: %u", ntohs($4.a));
				YYERROR;
			}
		}
		| DIVERTREPLY {
			if (filter_opts.divert.type != PF_DIVERT_NONE) {
				yyerror("more than one divert option");
				YYERROR;
			}
			filter_opts.divert.type = PF_DIVERT_REPLY;
		}
		| DIVERTPACKET PORT portplain {
			if (filter_opts.divert.type != PF_DIVERT_NONE) {
				yyerror("more than one divert option");
				YYERROR;
			}
			filter_opts.divert.type = PF_DIVERT_PACKET;
			/*
			 * If IP reassembly was not turned off, also
			 * forcibly enable TCP reassembly by default.
			 */
			if (pf->reassemble & PF_REASS_ENABLED)
				filter_opts.marker |= FOM_SCRUB_TCP;

			filter_opts.divert.port = $3.a;
			if (!filter_opts.divert.port) {
				yyerror("invalid divert port: %u", ntohs($3.a));
				YYERROR;
			}
		}
		| SCRUB '(' scrub_opts ')' {
			filter_opts.nodf = $3.nodf;
			filter_opts.minttl = $3.minttl;
			filter_opts.randomid = $3.randomid;
			filter_opts.max_mss = $3.maxmss;
			if ($3.reassemble_tcp)
				filter_opts.marker |= FOM_SCRUB_TCP;
			filter_opts.marker |= $3.marker;
		}
		| NATTO redirpool pool_opts {
			if (filter_opts.nat.rdr) {
				yyerror("cannot respecify nat-to/binat-to");
				YYERROR;
			}
			filter_opts.nat.rdr = $2;
			memcpy(&filter_opts.nat.pool_opts, &$3,
			    sizeof(filter_opts.nat.pool_opts));
		}
		| AFTO af FROM redirpool pool_opts {
			if (filter_opts.nat.rdr) {
				yyerror("cannot respecify af-to");
				YYERROR;
			}
			if ($2 == 0) {
				yyerror("no target address family specified");
				YYERROR;
			}
			filter_opts.nat.af = $2;
			filter_opts.nat.rdr = $4;
			memcpy(&filter_opts.nat.pool_opts, &$5,
			    sizeof(filter_opts.nat.pool_opts));
			filter_opts.rdr.rdr =
			    calloc(1, sizeof(struct redirection));
			bzero(&filter_opts.rdr.pool_opts,
			    sizeof(filter_opts.rdr.pool_opts));
			filter_opts.marker |= FOM_AFTO;
		}
		| AFTO af FROM redirpool pool_opts TO redirpool pool_opts {
			if (filter_opts.nat.rdr) {
				yyerror("cannot respecify af-to");
				YYERROR;
			}
			if ($2 == 0) {
				yyerror("no address family specified");
				YYERROR;
			}
			if (($4->host->af && $4->host->af != $2) ||
			    ($7->host->af && $7->host->af != $2)) {
				yyerror("af-to addresses must be in the "
				    "target address family");
				YYERROR;
			}
			filter_opts.nat.af = $2;
			filter_opts.nat.rdr = $4;
			memcpy(&filter_opts.nat.pool_opts, &$5,
			    sizeof(filter_opts.nat.pool_opts));
			filter_opts.rdr.af = $2;
			filter_opts.rdr.rdr = $7;
			memcpy(&filter_opts.nat.pool_opts, &$8,
			    sizeof(filter_opts.nat.pool_opts));
			filter_opts.marker |= FOM_AFTO;
		}
		| RDRTO redirpool pool_opts {
			if (filter_opts.rdr.rdr) {
				yyerror("cannot respecify rdr-to");
				YYERROR;
			}
			filter_opts.rdr.rdr = $2;
			memcpy(&filter_opts.rdr.pool_opts, &$3,
			    sizeof(filter_opts.rdr.pool_opts));
		}
		| BINATTO redirpool pool_opts {
			if (filter_opts.nat.rdr) {
				yyerror("cannot respecify nat-to/binat-to");
				YYERROR;
			}
			filter_opts.nat.rdr = $2;
			filter_opts.nat.binat = 1;
			memcpy(&filter_opts.nat.pool_opts, &$3,
			    sizeof(filter_opts.nat.pool_opts));
			filter_opts.nat.pool_opts.staticport = 1;
		}
		| ROUTETO routespec {
			filter_opts.rt = PF_ROUTETO;
		}
		| REPLYTO routespec {
			filter_opts.rt = PF_REPLYTO;
		}
		| DUPTO routespec {
			filter_opts.rt = PF_DUPTO;
		}
		| not RECEIVEDON if_item {
			if (filter_opts.rcv) {
				yyerror("cannot respecify received-on");
				YYERROR;
			}
			filter_opts.rcv = $3;
			filter_opts.rcv->not = $1;
		}
		| ONCE {
			filter_opts.marker |= FOM_ONCE;
		}
		| MAXPKTRATE NUMBER '/' NUMBER {
			if ($2 < 0 || $2 > UINT_MAX ||
			    $4 < 0 || $4 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			if (filter_opts.pktrate.limit) {
				yyerror("cannot respecify max-pkt-rate");
				YYERROR;
			}
			filter_opts.pktrate.limit = $2;
			filter_opts.pktrate.seconds = $4;
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
		| QUEUE qname	{
			if (filter_opts.queues.qname) {
				yyerror("queue cannot be redefined");
				YYERROR;
			}
			filter_opts.queues = $2;
		}
		| TOS tos {
			if (filter_opts.marker & FOM_SETTOS) {
				yyerror("tos cannot be respecified");
				YYERROR;
			}
			filter_opts.marker |= FOM_SETTOS;
			filter_opts.settos = $2;
		}
		| DELAY NUMBER {
			if (filter_opts.delay) {
				yyerror("delay cannot be respecified");
				YYERROR;
			}
			if ($2 < 0 || $2 > 0xffff) {
				yyerror("illegal delay value %lld (0-%u)", $2,
				    0xffff);
				YYERROR;
			}
			filter_opts.marker |= FOM_SETDELAY;
			filter_opts.delay = $2;
		}
		;

prio		: PRIO NUMBER {
			if ($2 < 0 || $2 > IFQ_MAXPRIO) {
				yyerror("prio must be 0 - %u", IFQ_MAXPRIO);
				YYERROR;
			}
			$$.b1 = $$.b2 = $2;
		}
		| PRIO '(' NUMBER comma NUMBER ')' {
			if ($3 < 0 || $3 > IFQ_MAXPRIO ||
			    $5 < 0 || $5 > IFQ_MAXPRIO) {
				yyerror("prio must be 0 - %u", IFQ_MAXPRIO);
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


action		: PASS			{ $$.b1 = PF_PASS; $$.b2 = $$.w = 0; }
		| MATCH			{ $$.b1 = PF_MATCH; $$.b2 = $$.w = 0; }
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
				yyerror("illegal ttl value %lld", $4);
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
				yyerror("invalid icmp code %lld", $1);
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
				yyerror("invalid icmp code %lld", $1);
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
		| MATCHES	{ $$.log = PF_LOG_MATCHES; $$.logif = 0; }
		| USER		{ $$.log = PF_LOG_USER; $$.logif = 0; }
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
		| ANY				{
			$$ = calloc(1, sizeof(struct node_if));
			if ($$ == NULL)
				err(1, "if_item: calloc");
			strlcpy($$->ifname, "any", sizeof($$->ifname));
			$$->not = 0;
			$$->next = NULL;
			$$->tail = $$;
		}
		| RDOMAIN NUMBER		{
			if ($2 < 0 || $2 > RT_TABLEID_MAX)
				yyerror("rdomain %lld outside range", $2);

			$$ = calloc(1, sizeof(struct node_if));
			if ($$ == NULL)
				err(1, "if_item: calloc");
			$$->not = 0;
			$$->use_rdomain = 1;
			$$->rdomain = $2;
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

os_list		: xos optnl			{ $$ = $1; }
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
		| /* empty */
		;

ipspec		: ANY				{ $$ = NULL; }
		| xhost				{ $$ = $1; }
		| '{' optnl host_list '}'	{ $$ = $3; }
		;


host_list	: ipspec optnl			{ $$ = $1; }
		| host_list comma ipspec optnl	{
			if ($1 == NULL) {
				freehostlist($3);
				$$ = $1;
			} else if ($3 == NULL) {
				freehostlist($1);
				$$ = $3;
			} else {
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

optweight	: WEIGHT NUMBER			{
			if ($2 < 1 || $2 > USHRT_MAX) {
				yyerror("weight out of range");
				YYERROR;
			}
			$$ = $2;
		}
		| /* empty */ { $$ = 0; }
		;

host		: STRING			{
			if (($$ = host($1, pf->opts)) == NULL)	{
				/* error. "any" is handled elsewhere */
				free($1);
				yyerror("could not parse host specification");
				YYERROR;
			}
			free($1);

		}
		| STRING '-' STRING		{
			struct node_host *b, *e;

			if ((b = host($1, pf->opts)) == NULL ||
			    (e = host($3, pf->opts)) == NULL) {
				free($1);
				free($3);
				yyerror("could not parse host specification");
				YYERROR;
			}
			if (b->af != e->af ||
			    b->addr.type != PF_ADDR_ADDRMASK ||
			    e->addr.type != PF_ADDR_ADDRMASK ||
			    unmask(&b->addr.v.a.mask) !=
			    (b->af == AF_INET ? 32 : 128) ||
			    unmask(&e->addr.v.a.mask) !=
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

			if (asprintf(&buf, "%s/%lld", $1, $3) == -1)
				err(1, "host: asprintf");
			free($1);
			if (($$ = host(buf, pf->opts)) == NULL)	{
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
			if (asprintf(&buf, "%lld/%lld", $1, $3) == -1)
				err(1, "host: asprintf");
			if (($$ = host(buf, pf->opts)) == NULL)	{
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
		| ROUTE	STRING		{
			$$ = calloc(1, sizeof(struct node_host));
			if ($$ == NULL) {
				free($2);
				err(1, "host: calloc");
			}
			$$->addr.type = PF_ADDR_RTLABEL;
			if (strlcpy($$->addr.v.rtlabelname, $2,
			    sizeof($$->addr.v.rtlabelname)) >=
			    sizeof($$->addr.v.rtlabelname)) {
				yyerror("route label too long, max %zu chars",
				    sizeof($$->addr.v.rtlabelname) - 1);
				free($2);
				free($$);
				YYERROR;
			}
			$$->next = NULL;
			$$->tail = $$;
			free($2);
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
			if (!isalpha((unsigned char)op[0])) {
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
			if ($1.t) {
				$$->op = PF_OP_RRG;

				if (validate_range($$->op, $$->port[0],
				    $$->port[1])) {
					yyerror("invalid port range");
					YYERROR;
				}
			} else
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
			if (validate_range($$->op, $$->port[0], $$->port[1])) {
				yyerror("invalid port range");
				YYERROR;
			}
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
			if ($2 == -1 && $1 != PF_OP_EQ && $1 != PF_OP_NE) {
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
			if ($1 == -1 || $3 == -1) {
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
				$$ = -1;
			else {
				uid_t uid;

				if (uid_from_user($1, &uid) == -1) {
					yyerror("unknown user %s", $1);
					free($1);
					YYERROR;
				}
				$$ = uid;
			}
			free($1);
		}
		| NUMBER			{
			if ($1 < 0 || $1 >= UID_MAX) {
				yyerror("illegal uid value %lld", $1);
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
			if ($2 == -1 && $1 != PF_OP_EQ && $1 != PF_OP_NE) {
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
			if ($1 == -1 || $3 == -1) {
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
				$$ = -1;
			else {
				gid_t gid;

				if (gid_from_group($1, &gid) == -1) {
					yyerror("unknown group %s", $1);
					free($1);
					YYERROR;
				}
				$$ = gid;
			}
			free($1);
		}
		| NUMBER			{
			if ($1 < 0 || $1 >= GID_MAX) {
				yyerror("illegal gid value %lld", $1);
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
				yyerror("illegal icmp-code %lld", $3);
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
				yyerror("illegal icmp-code %lld", $3);
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
				yyerror("illegal icmp-type %lld", $1);
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
				yyerror("illegal icmp6-type %lld", $1);
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
				yyerror("illegal tos value %lld", $1);
				YYERROR;
			}
		}
		;

sourcetrack	: /* empty */		{ $$ = PF_SRCTRACK; }
		| GLOBAL		{ $$ = PF_SRCTRACK_GLOBAL; }
		| RULE			{ $$ = PF_SRCTRACK_RULE; }
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
		| SOURCETRACK sourcetrack {
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_SRCTRACK;
			$$->data.src_track = $2;
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
		| PFLOW {
			$$ = calloc(1, sizeof(struct node_state_opt));
			if ($$ == NULL)
				err(1, "state_opt_item: calloc");
			$$->type = PF_STATE_OPT_PFLOW;
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

label		: STRING			{
			$$ = $1;
		}
		;

qname		: STRING				{
			struct pfctl_qsitem *qsi;

			if ((qsi = pfctl_find_queue($1, &qspecs)) == NULL) {
				yyerror("queue %s is not defined", $1);
				YYERROR;
			}
			$$.qname = $1;
			$$.pqname = NULL;
		}
		| '(' STRING ')'			{
			struct pfctl_qsitem *qsi;

			if ((qsi = pfctl_find_queue($2, &qspecs)) == NULL) {
				yyerror("queue %s is not defined", $2);
				YYERROR;
			}
			$$.qname = $2;
			$$.pqname = NULL;
		}
		| '(' STRING comma STRING ')'	{
			struct pfctl_qsitem *qsi, *pqsi;

			if ((qsi = pfctl_find_queue($2, &qspecs)) == NULL) {
				yyerror("queue %s is not defined", $2);
				YYERROR;
			}
			if ((pqsi = pfctl_find_queue($4, &qspecs)) == NULL) {
				yyerror("queue %s is not defined", $4);
				YYERROR;
			}
			$$.qname = $2;
			$$.pqname = $4;
		}
		;

portstar	: numberstring			{
			if (parseport($1, &$$, PPORT_RANGE|PPORT_STAR) == -1) {
				free($1);
				YYERROR;
			}
			free($1);
		}
		;

redirspec	: host optweight		{
			if ($2 > 0) {
				struct node_host	*n;
				for (n = $1; n != NULL; n = n->next)
					n->weight = $2;
			}
			$$ = $1;
		}
		| '{' optnl redir_host_list '}'	{ $$ = $3; }
		;

redir_host_list	: host optweight optnl			{
			if ($1->addr.type != PF_ADDR_ADDRMASK) {
				free($1);
				yyerror("only addresses can be listed for "
				    "redirection pools ");
				YYERROR;
			}
			if ($2 > 0) {
				struct node_host	*n;
				for (n = $1; n != NULL; n = n->next)
					n->weight = $2;
			}
			$$ = $1;
		}
		| redir_host_list comma host optweight optnl {
			$1->tail->next = $3;
			$1->tail = $3->tail;
			if ($4 > 0) {
				struct node_host	*n;
				for (n = $3; n != NULL; n = n->next)
					n->weight = $4;
			}
			$$ = $1;
		}
		;

redirpool	: redirspec		{
			$$ = calloc(1, sizeof(struct redirection));
			if ($$ == NULL)
				err(1, "redirection: calloc");
			$$->host = $1;
			$$->rport.a = $$->rport.b = $$->rport.t = 0;
		}
		| redirspec PORT portstar	{
			$$ = calloc(1, sizeof(struct redirection));
			if ($$ == NULL)
				err(1, "redirection: calloc");
			$$->host = $1;
			$$->rport = $3;
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
		| LEASTSTATES	{
			if (pool_opts.type) {
				yyerror("pool type cannot be redefined");
				YYERROR;
			}
			pool_opts.type = PF_POOL_LEASTSTATES;
		}
		| STATICPORT	{
			if (pool_opts.staticport) {
				yyerror("static-port cannot be redefined");
				YYERROR;
			}
			pool_opts.staticport = 1;
		}
		| STICKYADDRESS	{
			if (pool_opts.marker & POM_STICKYADDRESS) {
				yyerror("sticky-address cannot be redefined");
				YYERROR;
			}
			pool_opts.marker |= POM_STICKYADDRESS;
			pool_opts.opts |= PF_POOL_STICKYADDR;
		}
		;

routespec	: redirspec pool_opts {
			struct redirection *redir;
			if (filter_opts.rt != PF_NOPFROUTE) {
				yyerror("cannot respecify "
				    "route-to/reply-to/dup-to");
				YYERROR;
			}
			redir = calloc(1, sizeof(*redir));
			if (redir == NULL)
				err(1, "routespec calloc");
			redir->host = $1;
			filter_opts.rroute.rdr = redir;
			memcpy(&filter_opts.rroute.pool_opts, &$2,
			    sizeof(filter_opts.rroute.pool_opts));
		}
		;

timeout_spec	: STRING NUMBER
		{
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
		;

timeout_list	: timeout_list comma timeout_spec optnl
		| timeout_spec optnl
		;

limit_spec	: STRING NUMBER
		{
			if ($2 < 0 || $2 > UINT_MAX) {
				yyerror("only positive values permitted");
				YYERROR;
			}
			if (pfctl_set_limit(pf, $1, $2) != 0) {
				yyerror("unable to set limit %s %lld", $1, $2);
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
		| NE		{ $$ = PF_OP_NE; }
		| LE		{ $$ = PF_OP_LE; }
		| '<'		{ $$ = PF_OP_LT; }
		| GE		{ $$ = PF_OP_GE; }
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
validate_range(u_int8_t op, u_int16_t p1, u_int16_t p2)
{
	u_int16_t a = ntohs(p1);
	u_int16_t b = ntohs(p2);

	if ((op == PF_OP_RRG && a > b) ||  /* 34:12,  i.e. none */
	    (op == PF_OP_IRG && a >= b) || /* 34><12, i.e. none */
	    (op == PF_OP_XRG && a > b))    /* 34<>22, i.e. all */
		return 1;
	return 0;
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
			yyerror("%s", fmt);
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
rule_consistent(struct pf_rule *r)
{
	int	problems = 0;

	if (r->proto != IPPROTO_TCP && r->os_fingerprint != PF_OSFP_ANY) {
		yyerror("os only applies to tcp");
		problems++;
	}
	if (r->proto != IPPROTO_TCP && r->proto != IPPROTO_UDP &&
	    (r->src.port_op || r->dst.port_op)) {
		yyerror("port only applies to tcp/udp");
		problems++;
	}
	if (r->proto != IPPROTO_TCP && r->proto != IPPROTO_UDP &&
	    r->uid.op) {
		yyerror("user only applies to tcp/udp");
		problems++;
	}
	if (r->proto != IPPROTO_TCP && r->proto != IPPROTO_UDP &&
	    r->gid.op) {
		yyerror("group only applies to tcp/udp");
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
	if (r->rule_flag & PFRULE_AFTO && r->af == r->naf) {
		yyerror("must indicate different address family with af-to");
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
	if (r->action != PF_PASS && r->keep_state) {
		yyerror("keep state is great, but only for pass rules");
		problems++;
	}
	if (r->rt && !r->keep_state) {
		yyerror("route-to, reply-to and dup-to require keep state");
		problems++;
	}
	if (r->rule_flag & PFRULE_STATESLOPPY &&
	    (r->keep_state == PF_STATE_MODULATE ||
	    r->keep_state == PF_STATE_SYNPROXY)) {
		yyerror("sloppy state matching cannot be used with "
		    "synproxy state or modulate state");
		problems++;
	}

	if ((r->keep_state == PF_STATE_SYNPROXY) && (r->direction != PF_IN))
		fprintf(stderr, "%s:%d: warning: "
		    "synproxy used for inbound rules only, "
		    "ignored for outbound\n", file->name, yylval.lineno);

	if ((r->nat.addr.type != PF_ADDR_NONE ||
	    r->rdr.addr.type != PF_ADDR_NONE) &&
	    r->action != PF_MATCH && !r->keep_state) {
		yyerror("nat-to and rdr-to require keep state");
		problems++;
	}
	if (r->direction == PF_INOUT && (r->nat.addr.type != PF_ADDR_NONE ||
	    r->rdr.addr.type != PF_ADDR_NONE)) {
		yyerror("nat-to and rdr-to require a direction");
		problems++;
	}
	if (r->af == AF_INET6 && (r->scrub_flags &
	    (PFSTATE_NODF|PFSTATE_RANDOMID))) {
		yyerror("address family inet6 does not support scrub options "
		    "no-df, random-id");
		problems++;
	}

	/* Basic rule sanity check. */
	switch (r->action) {
	case PF_MATCH:
		if (r->divert.type != PF_DIVERT_NONE) {
			yyerror("divert is not supported on match rules");
			problems++;
		}
		if (r->rt) {
			yyerror("route-to, reply-to and dup-to "
			   "are not supported on match rules");
			problems++;
		}
		if (r->rule_flag & PFRULE_AFTO) {
			yyerror("af-to is not supported on match rules");
			problems++;
		}
		break;
	case PF_DROP:
		if (r->rt) {
			yyerror("route-to, reply-to and dup-to "
			   "are not supported on block rules");
			problems++;
		}
		break;
	default:;
	}
	return (-problems);
}

int
process_tabledef(char *name, struct table_opts *opts, int popts)
{
	struct pfr_buffer	 ab;
	struct node_tinit	*ti;
	struct pfr_uktable	*ukt;

	bzero(&ab, sizeof(ab));
	ab.pfrb_type = PFRB_ADDRS;
	SIMPLEQ_FOREACH(ti, &opts->init_nodes, entries) {
		if (ti->file)
			if (pfr_buf_load(&ab, ti->file, 0, popts)) {
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
	if (!(pf->opts & PF_OPT_NOACTION) ||
	    (pf->opts & PF_OPT_DUMMYACTION))
		warn_duplicate_tables(name, pf->anchor->path);
	else if (pf->opts & PF_OPT_VERBOSE)
		fprintf(stderr, "%s:%d: skipping duplicate table checks"
		    " for <%s>\n", file->name, yylval.lineno, name);

	/*
	 * postpone definition of non-root tables to moment
	 * when path is fully resolved.
	 */
	if (pf->asd > 0) {
		ukt = calloc(1, sizeof(struct pfr_uktable));
		if (ukt == NULL) {
			DBGPRINT(
			    "%s:%d: not enough memory for <%s>\n", file->name,
			    yylval.lineno, name);
			goto _error;
		}
	} else
		ukt = NULL;

	if (!(pf->opts & PF_OPT_NOACTION) &&
	    pfctl_define_table(name, opts->flags, opts->init_addr,
	    pf->anchor->path, &ab, pf->anchor->ruleset.tticket, ukt)) {
		yyerror("cannot define table %s: %s", name,
		    pf_strerror(errno));
		goto _error;
	}

	if (ukt != NULL) {
		ukt->pfrukt_init_addr = opts->init_addr;
		if (RB_INSERT(pfr_ktablehead, &pfr_ktables,
		    &ukt->pfrukt_kt) != NULL) {
			/*
			 * I think this should not happen, because
			 * pfctl_define_table() above  does the same check
			 * effectively.
			 */
			DBGPRINT(
			    "%s:%d table %s already exists in %s\n",
			    file->name, yylval.lineno,
			    ukt->pfrukt_name, pf->anchor->path);
			free(ukt);
			goto _error;
		}
		DBGPRINT("%s %s@%s inserted to tree\n",
		    __func__, ukt->pfrukt_name, pf->anchor->path);

	} else
		DBGPRINT("%s ukt is null\n", __func__);

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
		err(1, "%s", __func__);
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
					bits = unmask(&h->addr.v.a.mask);
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
pfctl_expand_label_nr(struct pf_rule *r, unsigned int rno)
{
	char n[11];

	snprintf(n, sizeof(n), "%u", rno);

	if (strstr(r->label, "$nr") != NULL)
		expand_label_str(r->label, PF_RULE_LABEL_SIZE, "$nr", n);

	if (strstr(r->tagname, "$nr") != NULL)
		expand_label_str(r->tagname, PF_TAG_NAME_SIZE, "$nr", n);

	if (strstr(r->match_tagname, "$nr") != NULL)
		expand_label_str(r->match_tagname, PF_TAG_NAME_SIZE, "$nr", n);
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
	/* rule number, '$nr', gets expanded after optimizer */
}

int
expand_queue(char *qname, struct node_if *interfaces, struct queue_opts *opts)
{
	struct pf_queuespec	qspec;

	LOOP_THROUGH(struct node_if, interface, interfaces,
		bzero(&qspec, sizeof(qspec));
		if (!opts->parent && (opts->marker & QOM_BWSPEC))
			opts->flags |= PFQS_ROOTCLASS;
		if (!(opts->marker & QOM_BWSPEC) &&
		    !(opts->marker & QOM_FLOWS)) {
			yyerror("no bandwidth or flow specification");
			return (1);
		}
		if (strlcpy(qspec.qname, qname, sizeof(qspec.qname)) >=
		    sizeof(qspec.qname)) {
			yyerror("queuename too long");
			return (1);
		}
		if (opts->parent && strlcpy(qspec.parent, opts->parent,
		    sizeof(qspec.parent)) >= sizeof(qspec.parent)) {
			yyerror("parent too long");
			return (1);
		}
		if (strlcpy(qspec.ifname, interface->ifname,
		    sizeof(qspec.ifname)) >= sizeof(qspec.ifname)) {
			yyerror("interface too long");
			return (1);
		}
		qspec.realtime.m1.absolute = opts->realtime.m1.bw_absolute;
		qspec.realtime.m1.percent = opts->realtime.m1.bw_percent;
		qspec.realtime.m2.absolute = opts->realtime.m2.bw_absolute;
		qspec.realtime.m2.percent = opts->realtime.m2.bw_percent;
		qspec.realtime.d = opts->realtime.d;

		qspec.linkshare.m1.absolute = opts->linkshare.m1.bw_absolute;
		qspec.linkshare.m1.percent = opts->linkshare.m1.bw_percent;
		qspec.linkshare.m2.absolute = opts->linkshare.m2.bw_absolute;
		qspec.linkshare.m2.percent = opts->linkshare.m2.bw_percent;
		qspec.linkshare.d = opts->linkshare.d;

		qspec.upperlimit.m1.absolute = opts->upperlimit.m1.bw_absolute;
		qspec.upperlimit.m1.percent = opts->upperlimit.m1.bw_percent;
		qspec.upperlimit.m2.absolute = opts->upperlimit.m2.bw_absolute;
		qspec.upperlimit.m2.percent = opts->upperlimit.m2.bw_percent;
		qspec.upperlimit.d = opts->upperlimit.d;

		qspec.flowqueue.flows = opts->flowqueue.flows;
		qspec.flowqueue.quantum = opts->flowqueue.quantum;
		qspec.flowqueue.interval = opts->flowqueue.interval;
		qspec.flowqueue.target = opts->flowqueue.target;

		qspec.flags = opts->flags;
		qspec.qlimit = opts->qlimit;

		if (pfctl_add_queue(pf, &qspec)) {
			yyerror("cannot add queue");
			return (1);
		}
	);

	FREE_LIST(struct node_if, interfaces);
	return (0);
}

int
expand_divertspec(struct pf_rule *r, struct divertspec *ds)
{
	struct node_host *n;

	switch (ds->type) {
	case PF_DIVERT_NONE:
		return (0);
	case PF_DIVERT_TO:
		if (r->direction == PF_OUT) {
			yyerror("divert-to used with outgoing rule");
			return (1);
		}
		if (r->af) {
			for (n = ds->addr; n != NULL; n = n->next)
				if (n->af == r->af)
					break;
			if (n == NULL) {
				yyerror("divert-to address family mismatch");
				return (1);
			}
			r->divert.addr = n->addr.v.a.addr;
		} else {
			r->af = ds->addr->af;
			r->divert.addr = ds->addr->addr.v.a.addr;
		}
		r->divert.port = ds->port;
		r->divert.type = ds->type;
		return (0);
	case PF_DIVERT_REPLY:
		if (r->direction == PF_IN) {
			yyerror("divert-reply used with incoming rule");
			return (1);
		}
		r->divert.type = ds->type;
		return (0);
	case PF_DIVERT_PACKET:
		r->divert.port = ds->port;
		r->divert.type = ds->type;
		return (0);
	}
	return (1);
}

int
collapse_redirspec(struct pf_pool *rpool, struct pf_rule *r,
    struct redirspec *rs, int routing)
{
	struct pf_opt_tbl *tbl = NULL;
	struct node_host *h, *hprev = NULL;
	struct pf_rule_addr ra;
	int af = 0, naddr = 0;

	if (!rs || !rs->rdr || rs->rdr->host == NULL) {
		rpool->addr.type = PF_ADDR_NONE;
		return (0);
	}

	if (r->rule_flag & PFRULE_AFTO)
		r->naf = rs->af;

	for (h = rs->rdr->host; h != NULL; h = h->next) {
		if (routing) {
			if (h->addr.type == PF_ADDR_DYNIFTL &&
			    h->addr.iflags != PFI_AFLAG_PEER) {
				yyerror("route spec requires :peer with "
				    "dynamic interface addresses");
				return (1);
			}
		}

		/* set rule address family if redirect spec has one */
		if (rs->af && !r->af && !af) {
			/* swap address families for af-to */
			if (r->naf == AF_INET6)
				af = AF_INET;
			else if (r->naf == AF_INET)
				af = AF_INET6;
			else
				af = rs->af;
		}
		if (h->af && !r->naf) {	/* nat-to/rdr-to case */
			/* skip if the rule af doesn't match redirect af */
			if (r->af && r->af != h->af)
				continue;
			/*
			 * fail if the chosen af is not universal for
			 * all addresses in the redirect address pool
			 */
			if (!r->af && af && af != h->af) {
				yyerror("%s spec contains addresses with "
				    "different address families",
				    routing ? "routing" : "translation");
				return (1);
			}
		} else if (h->af) {	/* af-to case */
			/*
			 * fail if the redirect spec af is not universal
			 * for all addresses in the redirect address pool
			 */
			if (rs->af && rs->af != h->af) {
				yyerror("%s spec contains addresses that "
				    "don't match target address family",
				    routing ? "routing" : "translation");
				return (1);
			}
		}
		/* else if (!h->af):
		 * we silently allow any not af-specific host specs,
		 * e.g. (em0) and let the kernel deal with them
		 */

		/* if we haven't selected the rule af yet, now it's time */
		if (!r->af && !af)
			af = h->af;

		if (naddr == 0) {	/* the first host */
			rpool->addr = h->addr;
			if (h->ifname) {
				yyerror("@if not permitted for %s",
				    routing ? "routing" : "translation");
				return (1);
			}
			if (h->ifname && strlcpy(rpool->ifname, h->ifname,
			    sizeof(rpool->ifname)) >= sizeof(rpool->ifname))
				errx(1, "collapse_redirspec: strlcpy");
			hprev = h; /* in case we need to conver to a table */
		} else {		/* multiple hosts */
			if (rs->pool_opts.type &&
			    !PF_POOL_DYNTYPE(rs->pool_opts.type)) {
				yyerror("pool type is not valid for multiple "
				    "translation or routing addresses");
				return (1);
			}
			if ((hprev && hprev->addr.type != PF_ADDR_ADDRMASK) &&
			    (hprev && hprev->addr.type != PF_ADDR_NONE) &&
			    h->addr.type != PF_ADDR_ADDRMASK &&
			    h->addr.type != PF_ADDR_NONE) {
				yyerror("multiple tables or dynamic interfaces "
				    "not supported for translation or routing");
				return (1);
			}
			if (h->ifname) {
				yyerror("@if not permitted for %s",
				    routing ? "routing" : "translation");
				return (1);
			}
			if (hprev) {
				/*
				 * undo some damage and convert the single
				 * host pool to the table
				 */
				memset(&ra, 0, sizeof(ra));
				memset(rpool->ifname, 0, sizeof(rpool->ifname));
				ra.addr = hprev->addr;
				ra.weight = hprev->weight;
				if (add_opt_table(pf, &tbl,
				    hprev->af, &ra, hprev->ifname))
					return (1);
				hprev = NULL;
			}
			memset(&ra, 0, sizeof(ra));
			ra.addr = h->addr;
			ra.weight = h->weight;
			if (add_opt_table(pf, &tbl,
			    h->af, &ra, h->ifname))
				return (1);
		}
		naddr++;
	}
	/* set rule af to the one chosen above */
	if (!r->af && af)
		r->af = af;
	if (!naddr) {
		yyerror("af mismatch in %s spec",
		    routing ? "routing" : "translation");
		return (1);
	}
	if (tbl) {
		if ((pf->opts & PF_OPT_NOACTION) == 0 &&
		    pf_opt_create_table(pf, tbl))
				return (1);

		pf->tdirty = 1;

		if (pf->opts & PF_OPT_VERBOSE)
			print_tabledef(tbl->pt_name,
			    PFR_TFLAG_CONST | tbl->pt_flags,
			    1, &tbl->pt_nodes);

		memset(&rpool->addr, 0, sizeof(rpool->addr));
		rpool->addr.type = PF_ADDR_TABLE;
		strlcpy(rpool->addr.v.tblname, tbl->pt_name,
		    sizeof(rpool->addr.v.tblname));

		pfr_buf_clear(tbl->pt_buf);
		free(tbl->pt_buf);
		tbl->pt_buf = NULL;
		free(tbl);
	}
	return (0);
}


int
apply_redirspec(struct pf_pool *rpool, struct pf_rule *r, struct redirspec *rs,
    int isrdr, struct node_port *np)
{
	if (!rs || !rs->rdr)
		return (0);

	rpool->proxy_port[0] = ntohs(rs->rdr->rport.a);

	if (isrdr) {
		if (!rs->rdr->rport.b && rs->rdr->rport.t) {
			rpool->proxy_port[1] = ntohs(rs->rdr->rport.a) +
			    (ntohs(np->port[1]) - ntohs(np->port[0]));
		} else {
			if (validate_range(rs->rdr->rport.t, rs->rdr->rport.a,
			    rs->rdr->rport.b)) {
				yyerror("invalid rdr-to port range");
				return (1);
			}

			rpool->port_op = rs->rdr->rport.t;
			rpool->proxy_port[1] = ntohs(rs->rdr->rport.b);
		}
	} else {
		rpool->proxy_port[1] = ntohs(rs->rdr->rport.b);
		if (!rpool->proxy_port[0] && !rpool->proxy_port[1]) {
			rpool->proxy_port[0] = PF_NAT_PROXY_PORT_LOW;
			rpool->proxy_port[1] = PF_NAT_PROXY_PORT_HIGH;
		} else if (!rpool->proxy_port[1])
			rpool->proxy_port[1] = rpool->proxy_port[0];
	}

	rpool->opts = rs->pool_opts.type;
	if ((rpool->opts & PF_POOL_TYPEMASK) == PF_POOL_NONE &&
	    (rpool->addr.type == PF_ADDR_TABLE ||
	    DYNIF_MULTIADDR(rpool->addr)))
		rpool->opts |= PF_POOL_ROUNDROBIN;

	if (!PF_POOL_DYNTYPE(rpool->opts) &&
	    (disallow_table(rs->rdr->host,
	    "tables are not supported by pool type") ||
	    disallow_alias(rs->rdr->host,
	    "interface (%s) is not supported by pool type")))
		return (1);

	if (rs->pool_opts.key != NULL)
		memcpy(&rpool->key, rs->pool_opts.key,
		    sizeof(struct pf_poolhashkey));

	if (rs->pool_opts.opts)
		rpool->opts |= rs->pool_opts.opts;

	if (rs->pool_opts.staticport) {
		if (isrdr) {
			yyerror("the 'static-port' option is only valid with "
			    "nat rules");
			return (1);
		}
		if (rpool->proxy_port[0] != PF_NAT_PROXY_PORT_LOW &&
		    rpool->proxy_port[1] != PF_NAT_PROXY_PORT_HIGH) {
			yyerror("the 'static-port' option can't be used when "
			    "specifying a port range");
			return (1);
		}
		rpool->proxy_port[0] = 0;
		rpool->proxy_port[1] = 0;
	}

	return (0);
}


void
expand_rule(struct pf_rule *r, int keeprule, struct node_if *interfaces,
    struct redirspec *nat, struct redirspec *rdr, struct redirspec *rroute,
    struct node_proto *protos, struct node_os *src_oses,
    struct node_host *src_hosts, struct node_port *src_ports,
    struct node_host *dst_hosts, struct node_port *dst_ports,
    struct node_uid *uids, struct node_gid *gids, struct node_if *rcv,
    struct node_icmp *icmp_types)
{
	sa_family_t		 af = r->af;
	int			 added = 0, error = 0;
	char			 ifname[IF_NAMESIZE];
	char			 label[PF_RULE_LABEL_SIZE];
	char			 tagname[PF_TAG_NAME_SIZE];
	char			 match_tagname[PF_TAG_NAME_SIZE];
	u_int8_t		 flags, flagset, keep_state;
	struct node_host	*srch, *dsth, *osrch, *odsth;
	struct redirspec	 binat;
	struct pf_rule		 rb;
	int			 dir = r->direction;

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

	r->src.addr.type = r->dst.addr.type = PF_ADDR_ADDRMASK;

	LOOP_THROUGH(struct node_if, interface, interfaces,
	LOOP_THROUGH(struct node_proto, proto, protos,
	LOOP_THROUGH(struct node_icmp, icmp_type, icmp_types,
	LOOP_THROUGH(struct node_host, src_host, src_hosts,
	LOOP_THROUGH(struct node_host, dst_host, dst_hosts,
	LOOP_THROUGH(struct node_port, src_port, src_ports,
	LOOP_THROUGH(struct node_port, dst_port, dst_ports,
	LOOP_THROUGH(struct node_os, src_os, src_oses,
	LOOP_THROUGH(struct node_uid, uid, uids,
	LOOP_THROUGH(struct node_gid, gid, gids,

		r->af = af;

		error += collapse_redirspec(&r->rdr, r, rdr, 0);
		error += collapse_redirspec(&r->nat, r, nat, 0);
		error += collapse_redirspec(&r->route, r, rroute, 1);

		/* disallow @if in from or to for the time being */
		if ((src_host->addr.type == PF_ADDR_ADDRMASK &&
		    src_host->ifname) ||
		    (dst_host->addr.type == PF_ADDR_ADDRMASK &&
		    dst_host->ifname)) {
			yyerror("@if syntax not permitted in from or to");
			error++;
		}
		/* for link-local IPv6 address, interface must match up */
		if ((r->af && src_host->af && r->af != src_host->af) ||
		    (r->af && dst_host->af && r->af != dst_host->af) ||
		    (src_host->af && dst_host->af &&
		    src_host->af != dst_host->af) ||
		    (src_host->ifindex && dst_host->ifindex &&
		    src_host->ifindex != dst_host->ifindex) ||
		    (src_host->ifindex && *interface->ifname &&
		    src_host->ifindex != ifa_nametoindex(interface->ifname)) ||
		    (dst_host->ifindex && *interface->ifname &&
		    dst_host->ifindex != ifa_nametoindex(interface->ifname)))
			continue;
		if (!r->af && src_host->af)
			r->af = src_host->af;
		else if (!r->af && dst_host->af)
			r->af = dst_host->af;

		if (*interface->ifname)
			strlcpy(r->ifname, interface->ifname,
			    sizeof(r->ifname));
		else if (ifa_indextoname(src_host->ifindex, ifname))
			strlcpy(r->ifname, ifname, sizeof(r->ifname));
		else if (ifa_indextoname(dst_host->ifindex, ifname))
			strlcpy(r->ifname, ifname, sizeof(r->ifname));
		else
			memset(r->ifname, '\0', sizeof(r->ifname));

		if (interface->use_rdomain)
			r->onrdomain = interface->rdomain;
		else
			r->onrdomain = -1;
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

		osrch = odsth = NULL;
		if (src_host->addr.type == PF_ADDR_DYNIFTL) {
			osrch = src_host;
			if ((src_host = gen_dynnode(src_host, r->af)) == NULL)
				err(1, "%s", __func__);
		}
		if (dst_host->addr.type == PF_ADDR_DYNIFTL) {
			odsth = dst_host;
			if ((dst_host = gen_dynnode(dst_host, r->af)) == NULL)
				err(1, "%s", __func__);
		}

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
		if (rcv) {
			strlcpy(r->rcv_ifname, rcv->ifname,
			    sizeof(r->rcv_ifname));
			r->rcvifnot = rcv->not;
		}
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

		if (nat && nat->rdr && nat->binat) {
			if (disallow_table(src_host, "invalid use of table "
			    "<%s> as the source address of a binat-to rule") ||
			    disallow_alias(src_host, "invalid use of interface "
			    "(%s) as the source address of a binat-to rule")) {
				error++;
			} else if ((r->src.addr.type != PF_ADDR_ADDRMASK &&
			    r->src.addr.type != PF_ADDR_DYNIFTL) ||
			    (r->nat.addr.type != PF_ADDR_ADDRMASK &&
			    r->nat.addr.type != PF_ADDR_DYNIFTL)) {
				yyerror("binat-to requires a specified "
				    "source and redirect address");
				error++;
			}
			if (DYNIF_MULTIADDR(r->src.addr) ||
			    DYNIF_MULTIADDR(r->nat.addr)) {
				yyerror ("dynamic interfaces must be used with "
				    ":0 in a binat-to rule");
				error++;
			}
			if (PF_AZERO(&r->src.addr.v.a.mask, af) ||
			    PF_AZERO(&r->nat.addr.v.a.mask, af)) {
				yyerror ("source and redir addresess must have "
				    "a matching network mask in binat-rule");
				error++;
			}
			if (r->nat.addr.type == PF_ADDR_TABLE) {
				yyerror ("tables cannot be used as the redirect "
				    "address of a binat-to rule");
				error++;
			}
			if (r->direction != PF_INOUT) {
				yyerror("binat-to cannot be specified "
				    "with a direction");
				error++;
			}

			/* first specify outbound NAT rule */
			r->direction = PF_OUT;
		}

		error += apply_redirspec(&r->nat, r, nat, 0, dst_port);
		error += apply_redirspec(&r->rdr, r, rdr, 1, dst_port);
		error += apply_redirspec(&r->route, r, rroute, 2, dst_port);

		if (rule_consistent(r) < 0 || error)
			yyerror("skipping rule due to errors");
		else {
			r->nr = pf->astack[pf->asd]->match++;
			pfctl_add_rule(pf, r);
			added++;
		}
		r->direction = dir;

		/* Generate binat's matching inbound rule */
		if (!error && nat && nat->rdr && nat->binat) {
			bcopy(r, &rb, sizeof(rb));

			/* now specify inbound rdr rule */
			rb.direction = PF_IN;

			if ((srch = calloc(1, sizeof(*srch))) == NULL)
				err(1, "%s", __func__);
			bcopy(src_host, srch, sizeof(*srch));
			srch->ifname = NULL;
			srch->next = NULL;
			srch->tail = NULL;

			if ((dsth = calloc(1, sizeof(*dsth))) == NULL)
				err(1, "%s", __func__);
			bcopy(&rb.nat.addr, &dsth->addr, sizeof(dsth->addr));
			dsth->ifname = NULL;
			dsth->next = NULL;
			dsth->tail = NULL;

			bzero(&binat, sizeof(binat));
			if ((binat.rdr =
			    calloc(1, sizeof(*binat.rdr))) == NULL)
				err(1, "%s", __func__);
			bcopy(nat->rdr, binat.rdr, sizeof(*binat.rdr));
			bcopy(&nat->pool_opts, &binat.pool_opts,
			    sizeof(binat.pool_opts));
			binat.pool_opts.staticport = 0;
			binat.rdr->host = srch;

			expand_rule(&rb, 1, interface, NULL, &binat, NULL,
			    proto,
			    src_os, dst_host, dst_port, dsth, src_port,
			    uid, gid, rcv, icmp_type);
		}

		if (osrch && src_host->addr.type == PF_ADDR_DYNIFTL) {
			free(src_host);
			src_host = osrch;
		}
		if (odsth && dst_host->addr.type == PF_ADDR_DYNIFTL) {
			free(dst_host);
			dst_host = odsth;
		}
	))))))))));

	if (!keeprule) {
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
		if (nat && nat->rdr)
			FREE_LIST(struct node_host, nat->rdr->host);
		if (rdr && rdr->rdr)
			FREE_LIST(struct node_host, rdr->rdr->host);

	}

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
		} else if (interface->use_rdomain) {
			yyerror("skip on rdomain <num> is not supported");
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

void
freehostlist(struct node_host *h)
{
	struct node_host *n;

	for (n = h; n != NULL; n = n->next)
		if (n->ifname)
			free(n->ifname);
	FREE_LIST(struct node_host, h);
}

#undef FREE_LIST
#undef LOOP_THROUGH

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
		{ "af-to",		AFTO},
		{ "all",		ALL},
		{ "allow-opts",		ALLOWOPTS},
		{ "anchor",		ANCHOR},
		{ "antispoof",		ANTISPOOF},
		{ "any",		ANY},
		{ "bandwidth",		BANDWIDTH},
		{ "binat-to",		BINATTO},
		{ "bitmask",		BITMASK},
		{ "block",		BLOCK},
		{ "block-policy",	BLOCKPOLICY},
		{ "burst",		BURST},
		{ "code",		CODE},
		{ "debug",		DEBUG},
		{ "default",		DEFAULT},
		{ "delay",		DELAY},
		{ "divert-packet",	DIVERTPACKET},
		{ "divert-reply",	DIVERTREPLY},
		{ "divert-to",		DIVERTTO},
		{ "drop",		DROP},
		{ "dup-to",		DUPTO},
		{ "file",		FILENAME},
		{ "fingerprints",	FINGERPRINTS},
		{ "flags",		FLAGS},
		{ "floating",		FLOATING},
		{ "flows",		FLOWS},
		{ "flush",		FLUSH},
		{ "for",		FOR},
		{ "fragment",		FRAGMENT},
		{ "from",		FROM},
		{ "global",		GLOBAL},
		{ "group",		GROUP},
		{ "hostid",		HOSTID},
		{ "icmp-type",		ICMPTYPE},
		{ "icmp6-type",		ICMP6TYPE},
		{ "if-bound",		IFBOUND},
		{ "in",			IN},
		{ "include",		INCLUDE},
		{ "inet",		INET},
		{ "inet6",		INET6},
		{ "keep",		KEEP},
		{ "label",		LABEL},
		{ "least-states",	LEASTSTATES},
		{ "limit",		LIMIT},
		{ "load",		LOAD},
		{ "log",		LOG},
		{ "loginterface",	LOGINTERFACE},
		{ "match",		MATCH},
		{ "matches",		MATCHES},
		{ "max",		MAXIMUM},
		{ "max-mss",		MAXMSS},
		{ "max-pkt-rate",	MAXPKTRATE},
		{ "max-src-conn",	MAXSRCCONN},
		{ "max-src-conn-rate",	MAXSRCCONNRATE},
		{ "max-src-nodes",	MAXSRCNODES},
		{ "max-src-states",	MAXSRCSTATES},
		{ "min",		MINIMUM},
		{ "min-ttl",		MINTTL},
		{ "modulate",		MODULATE},
		{ "nat-to",		NATTO},
		{ "no",			NO},
		{ "no-df",		NODF},
		{ "no-route",		NOROUTE},
		{ "no-sync",		NOSYNC},
		{ "on",			ON},
		{ "once",		ONCE},
		{ "optimization",	OPTIMIZATION},
		{ "os",			OS},
		{ "out",		OUT},
		{ "overload",		OVERLOAD},
		{ "parent",		PARENT},
		{ "pass",		PASS},
		{ "pflow",		PFLOW},
		{ "port",		PORT},
		{ "prio",		PRIO},
		{ "probability",	PROBABILITY},
		{ "proto",		PROTO},
		{ "qlimit",		QLIMIT},
		{ "quantum",		QUANTUM},
		{ "queue",		QUEUE},
		{ "quick",		QUICK},
		{ "random",		RANDOM},
		{ "random-id",		RANDOMID},
		{ "rdomain",		RDOMAIN},
		{ "rdr-to",		RDRTO},
		{ "reassemble",		REASSEMBLE},
		{ "received-on",	RECEIVEDON},
		{ "reply-to",		REPLYTO},
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
		{ "skip",		SKIP},
		{ "sloppy",		SLOPPY},
		{ "source-hash",	SOURCEHASH},
		{ "source-track",	SOURCETRACK},
		{ "state",		STATE},
		{ "state-defaults",	STATEDEFAULTS},
		{ "state-policy",	STATEPOLICY},
		{ "static-port",	STATICPORT},
		{ "sticky-address",	STICKYADDRESS},
		{ "syncookies",		SYNCOOKIES},
		{ "synproxy",		SYNPROXY},
		{ "table",		TABLE},
		{ "tag",		TAG},
		{ "tagged",		TAGGED},
		{ "timeout",		TIMEOUT},
		{ "to",			TO},
		{ "tos",		TOS},
		{ "ttl",		TTL},
		{ "urpf-failed",	URPFFAILED},
		{ "user",		USER},
		{ "weight",		WEIGHT},
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

#define START_EXPAND	1
#define DONE_EXPAND	2

static int expanding;

int
igetc(void)
{
	int c;
	while (1) {
		if (file->ungetpos > 0)
			c = file->ungetbuf[--file->ungetpos];
		else
			c = getc(file->stream);
		if (c == START_EXPAND)
			expanding = 1;
		else if (c == DONE_EXPAND)
			expanding = 0;
		else
			break;
	}
	return (c);
}

int
lgetc(int quotec)
{
	int		c, next;

	if (quotec) {
		if ((c = igetc()) == EOF) {
			yyerror("reached end of file while parsing quoted string");
			if (file == topfile || popfile() == EOF)
				return (EOF);
			return (quotec);
		}
		return (c);
	}

	while ((c = igetc()) == '\\') {
		next = igetc();
		if (next != '\n') {
			c = next;
			break;
		}
		yylval.lineno = file->lineno;
		file->lineno++;
	}

	if (c == EOF) {
		/*
		 * Fake EOL when hit EOF for the first time. This gets line
		 * count right if last line in included file is syntactically
		 * invalid and has no newline.
		 */
		if (file->eof_reached == 0) {
			file->eof_reached = 1;
			return ('\n');
		}
		while (c == EOF) {
			if (file == topfile || popfile() == EOF)
				return (EOF);
			c = igetc();
		}
	}

	return (c);
}

void
lungetc(int c)
{
	if (c == EOF)
		return;
	if (file->ungetpos >= file->ungetsize) {
		void *p = reallocarray(file->ungetbuf, file->ungetsize, 2);
		if (p == NULL)
			err(1, "%s", __func__);
		file->ungetbuf = p;
		file->ungetsize *= 2;
	}
	file->ungetbuf[file->ungetpos++] = c;
}

int
findeol(void)
{
	int	c;

	/* skip to either EOF or the first real EOL */
	while (1) {
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
	if (c == '$' && !expanding) {
		while (1) {
			if ((c = lgetc(0)) == EOF)
				return (0);

			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			if (isalnum(c) || c == '_') {
				*p++ = c;
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
		p = val + strlen(val) - 1;
		lungetc(DONE_EXPAND);
		while (p >= val) {
			lungetc((unsigned char)*p);
			p--;
		}
		lungetc(START_EXPAND);
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
				if (next == quotec || next == ' ' ||
				    next == '\t')
					c = next;
				else if (next == '\n') {
					file->lineno++;
					continue;
				} else
					lungetc(next);
			} else if (c == quotec) {
				*p = '\0';
				break;
			} else if (c == '\0') {
				yyerror("syntax error");
				return (findeol());
			}
			if (p + 1 >= buf + sizeof(buf) - 1) {
				yyerror("string too long");
				return (findeol());
			}
			*p++ = c;
		}
		yylval.v.string = strdup(buf);
		if (yylval.v.string == NULL)
			err(1, "%s", __func__);
		return (STRING);
	case '!':
		next = lgetc(0);
		if (next == '=')
			return (NE);
		lungetc(next);
		break;
	case '<':
		next = lgetc(0);
		if (next == '>') {
			yylval.v.i = PF_OP_XRG;
			return (PORTBINARY);
		} else if (next == '=')
			return (LE);
		lungetc(next);
		break;
	case '>':
		next = lgetc(0);
		if (next == '<') {
			yylval.v.i = PF_OP_IRG;
			return (PORTBINARY);
		} else if (next == '=')
			return (GE);
		lungetc(next);
		break;
	}

#define allowed_to_end_number(x) \
	(isspace(x) || x == ')' || x ==',' || x == '/' || x == '}' || x == '=')

	if (c == '-' || isdigit(c)) {
		do {
			*p++ = c;
			if ((size_t)(p-buf) >= sizeof(buf)) {
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
				lungetc((unsigned char)*--p);
			c = (unsigned char)*--p;
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
			if ((size_t)(p-buf) >= sizeof(buf)) {
				yyerror("string too long");
				return (findeol());
			}
		} while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
		lungetc(c);
		*p = '\0';
		if ((token = lookup(buf)) == STRING)
			if ((yylval.v.string = strdup(buf)) == NULL)
				err(1, "%s", __func__);
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
	if (st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO)) {
		warnx("%s: group writable or world read/writable", fname);
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
		warn("%s", __func__);
		if (nfile)
			free(nfile);
		return (NULL);
	}
	if (TAILQ_FIRST(&files) == NULL && strcmp(nfile->name, "-") == 0) {
		nfile->stream = stdin;
		free(nfile->name);
		if ((nfile->name = strdup("stdin")) == NULL) {
			warn("%s", __func__);
			free(nfile);
			return (NULL);
		}
	} else if ((nfile->stream = pfctl_fopen(nfile->name, "r")) == NULL) {
		warn("%s: %s", __func__, nfile->name);
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
	nfile->lineno = TAILQ_EMPTY(&files) ? 1 : 0;
	nfile->ungetsize = 16;
	nfile->ungetbuf = malloc(nfile->ungetsize);
	if (nfile->ungetbuf == NULL) {
		warn("%s", __func__);
		fclose(nfile->stream);
		free(nfile->name);
		free(nfile);
		return (NULL);
	}
	TAILQ_INSERT_TAIL(&files, nfile, entry);
	return (nfile);
}

int
popfile(void)
{
	struct file	*prev;

	if ((prev = TAILQ_PREV(file, files, entry)) != NULL)
		prev->errors += file->errors;

	TAILQ_REMOVE(&files, file, entry);
	fclose(file->stream);
	free(file->name);
	free(file->ungetbuf);
	free(file);
	file = prev;

	return (file ? 0 : EOF);
}

int
parse_config(char *filename, struct pfctl *xpf)
{
	int		 errors = 0;
	struct sym	*sym;

	pf = xpf;
	returnicmpdefault = (ICMP_UNREACH << 8) | ICMP_UNREACH_PORT;
	returnicmp6default =
	    (ICMP6_DST_UNREACH << 8) | ICMP6_DST_UNREACH_NOPORT;
	blockpolicy = PFRULE_DROP;

	if ((file = pushfile(filename, 0)) == NULL) {
		warn("cannot open the main config file!");
		return (-1);
	}
	topfile = file;

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

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0)
			break;
	}

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

	sym = strndup(s, val - s);
	if (sym == NULL)
		err(1, "%s", __func__);
	ret = symset(sym, val + 1, 1);
	free(sym);

	return (ret);
}

char *
symget(const char *nam)
{
	struct sym	*sym;

	TAILQ_FOREACH(sym, &symhead, entry) {
		if (strcmp(nam, sym->nam) == 0) {
			sym->used = 1;
			return (sym->val);
		}
	}
	return (NULL);
}

void
mv_rules(struct pf_ruleset *src, struct pf_ruleset *dst)
{
	struct pf_rule *r;

	TAILQ_FOREACH(r, src->rules.active.ptr, entries)
		dst->anchor->match++;
	TAILQ_CONCAT(dst->rules.active.ptr, src->rules.active.ptr, entries);
	src->anchor->match = 0;
	TAILQ_CONCAT(dst->rules.inactive.ptr, src->rules.inactive.ptr, entries);
}

void
mv_tables(struct pfctl *pf, struct pfr_ktablehead *ktables,
    struct pf_anchor *a, struct pf_anchor *alast)
{

	struct pfr_ktable *kt, *kt_safe;
	char new_path[PF_ANCHOR_MAXPATH];
	char *path_cut;
	int sz;
	struct pfr_uktable *ukt;
	SLIST_HEAD(, pfr_uktable) ukt_list;

	/*
	 * Here we need to rename anchor path from temporal names such as
	 * _1/_2/foo to _1/bar/foo etc.
	 *
	 * This also means we need to remove and insert table to ktables
	 * tree as anchor path is being updated.
	 */
	SLIST_INIT(&ukt_list);
	DBGPRINT("%s [ %s ] (%s)\n", __func__, a->path, alast->path);
	RB_FOREACH_SAFE(kt, pfr_ktablehead, ktables, kt_safe) {
		path_cut = strstr(kt->pfrkt_anchor, alast->path);
		if (path_cut != NULL) {
			path_cut += strlen(alast->path);
			if (*path_cut)
				sz = snprintf(new_path, sizeof (new_path),
				    "%s%s", a->path, path_cut);
			else
				sz = snprintf(new_path, sizeof (new_path),
				    "%s", a->path);
			if (sz >= sizeof (new_path))
				errx(1, "new path is too long for %s@%s\n",
				    kt->pfrkt_name, kt->pfrkt_anchor);

			DBGPRINT("%s %s@%s -> %s@%s\n", __func__,
			    kt->pfrkt_name, kt->pfrkt_anchor,
			    kt->pfrkt_name, new_path);
			RB_REMOVE(pfr_ktablehead, ktables, kt);
			strlcpy(kt->pfrkt_anchor, new_path,
			    sizeof(kt->pfrkt_anchor));
			SLIST_INSERT_HEAD(&ukt_list, (struct pfr_uktable *)kt,
			    pfrukt_entry);
		}
	}

	while ((ukt = SLIST_FIRST(&ukt_list)) != NULL) {
		SLIST_REMOVE_HEAD(&ukt_list, pfrukt_entry);
		if (RB_INSERT(pfr_ktablehead, ktables,
		    (struct pfr_ktable *)ukt) != NULL)
			errx(1, "%s@%s exists already\n",
			    ukt->pfrukt_name,
			    ukt->pfrukt_anchor);
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
			yyerror("rule label too long (max %zu chars)",
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
	yyerror("port is invalid: %s", port);
	return (-1);
}

int
pfctl_load_anchors(int dev, struct pfctl *pf)
{
	struct loadanchors	*la;

	TAILQ_FOREACH(la, &loadanchorshead, entries) {
		if (pf->opts & PF_OPT_VERBOSE)
			fprintf(stderr, "\nLoading anchor %s from %s\n",
			    la->anchorname, la->filename);
		if (pfctl_rules(dev, la->filename, pf->opts, pf->optimize,
		    la->anchorname, pf->trans) == -1)
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
lookup_rtable(u_int rtableid)
{
	size_t			 len;
	struct rt_tableinfo	 info;
	int			 mib[6];
	static u_int		 found[RT_TABLEID_MAX+1];

	if (found[rtableid])
		return found[rtableid];

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;
	mib[4] = NET_RT_TABLE;
	mib[5] = rtableid;

	len = sizeof(info);
	if (sysctl(mib, 6, &info, &len, NULL, 0) == -1) {
		if (errno == ENOENT) {
			/* table nonexistent */
			found[rtableid] = 0;
			return 0;
		}
		err(1, "%s", __func__);
	}
	found[rtableid] = 1;
	return 1;
}

int
filteropts_to_rule(struct pf_rule *r, struct filter_opts *opts)
{
	if (opts->marker & FOM_ONCE) {
		if ((r->action != PF_PASS && r->action != PF_DROP) || r->anchor) {
			yyerror("'once' only applies to pass/block rules");
			return (1);
		}
		r->rule_flag |= PFRULE_ONCE;
	}

	r->keep_state = opts->keep.action;
	r->pktrate.limit = opts->pktrate.limit;
	r->pktrate.seconds = opts->pktrate.seconds;
	r->prob = opts->prob;
	r->rtableid = opts->rtableid;
	r->tos = opts->tos;

	if (opts->nodf)
		r->scrub_flags |= PFSTATE_NODF;
	if (opts->randomid)
		r->scrub_flags |= PFSTATE_RANDOMID;
	if (opts->minttl)
		r->min_ttl = opts->minttl;
	if (opts->max_mss)
		r->max_mss = opts->max_mss;

	if (opts->tag)
		if (strlcpy(r->tagname, opts->tag,
		    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
			yyerror("tag too long, max %u chars",
			    PF_TAG_NAME_SIZE - 1);
			return (1);
		}
	if (opts->match_tag)
		if (strlcpy(r->match_tagname, opts->match_tag,
		    PF_TAG_NAME_SIZE) >= PF_TAG_NAME_SIZE) {
			yyerror("tag too long, max %u chars",
			    PF_TAG_NAME_SIZE - 1);
			return (1);
		}
	r->match_tag_not = opts->match_tag_not;

	if (rule_label(r, opts->label))
		return (1);
	free(opts->label);

	if (opts->marker & FOM_AFTO)
		r->rule_flag |= PFRULE_AFTO;
	if ((opts->marker & FOM_AFTO) && r->direction != PF_IN) {
		yyerror("af-to can only be used with direction in");
		return (1);
	}
	if ((opts->marker & FOM_AFTO) && opts->rt) {
		yyerror("af-to cannot be used together with "
		    "route-to, reply-to, dup-to");
		return (1);
	}
	if (opts->marker & FOM_SCRUB_TCP)
		r->scrub_flags |= PFSTATE_SCRUB_TCP;
	if (opts->marker & FOM_SETDELAY) {
		r->delay = opts->delay;
		r->rule_flag |= PFRULE_SETDELAY;
	}
	if (opts->marker & FOM_SETTOS) {
		r->scrub_flags |= PFSTATE_SETTOS;
		r->set_tos = opts->settos;
	}
	if (opts->marker & FOM_PRIO)
		r->prio = opts->prio ? opts->prio : PF_PRIO_ZERO;
	if (opts->marker & FOM_SETPRIO) {
		r->set_prio[0] = opts->set_prio[0];
		r->set_prio[1] = opts->set_prio[1];
		r->scrub_flags |= PFSTATE_SETPRIO;
	}

	r->flags = opts->flags.b1;
	r->flagset = opts->flags.b2;
	if ((opts->flags.b1 & opts->flags.b2) != opts->flags.b1) {
		yyerror("flags always false");
		return (1);
	}

	if (opts->queues.qname != NULL) {
		if (strlcpy(r->qname, opts->queues.qname,
		    sizeof(r->qname)) >= sizeof(r->qname)) {
			yyerror("rule qname too long (max "
			    "%zu chars)", sizeof(r->qname)-1);
			return (1);
		}
		free(opts->queues.qname);
	}
	if (opts->queues.pqname != NULL) {
		if (strlcpy(r->pqname, opts->queues.pqname,
		    sizeof(r->pqname)) >= sizeof(r->pqname)) {
			yyerror("rule pqname too long (max "
			    "%zu chars)", sizeof(r->pqname)-1);
			return (1);
		}
		free(opts->queues.pqname);
	}

	if (opts->fragment)
		r->rule_flag |= PFRULE_FRAGMENT;
	r->allow_opts = opts->allowopts;

	return (0);
}
