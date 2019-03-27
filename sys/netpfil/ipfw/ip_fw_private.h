/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Luigi Rizzo, Universita` di Pisa
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IPFW2_PRIVATE_H
#define _IPFW2_PRIVATE_H

/*
 * Internal constants and data structures used by ipfw components
 * and not meant to be exported outside the kernel.
 */

#ifdef _KERNEL

/*
 * For platforms that do not have SYSCTL support, we wrap the
 * SYSCTL_* into a function (one per file) to collect the values
 * into an array at module initialization. The wrapping macros,
 * SYSBEGIN() and SYSEND, are empty in the default case.
 */
#ifndef SYSBEGIN
#define SYSBEGIN(x)
#endif
#ifndef SYSEND
#define SYSEND
#endif

/* Return values from ipfw_chk() */
enum {
	IP_FW_PASS = 0,
	IP_FW_DENY,
	IP_FW_DIVERT,
	IP_FW_TEE,
	IP_FW_DUMMYNET,
	IP_FW_NETGRAPH,
	IP_FW_NGTEE,
	IP_FW_NAT,
	IP_FW_REASS,
	IP_FW_NAT64,
};

/*
 * Structure for collecting parameters to dummynet for ip6_output forwarding
 */
struct _ip6dn_args {
       struct ip6_pktopts *opt_or;
       int flags_or;
       struct ip6_moptions *im6o_or;
       struct ifnet *origifp_or;
       struct ifnet *ifp_or;
       struct sockaddr_in6 dst_or;
       u_long mtu_or;
};


/*
 * Arguments for calling ipfw_chk() and dummynet_io(). We put them
 * all into a structure because this way it is easier and more
 * efficient to pass variables around and extend the interface.
 */
struct ip_fw_args {
	uint32_t		flags;
#define	IPFW_ARGS_ETHER		0x00010000	/* valid ethernet header */
#define	IPFW_ARGS_NH4		0x00020000	/* IPv4 next hop in hopstore */
#define	IPFW_ARGS_NH6		0x00040000	/* IPv6 next hop in hopstore */
#define	IPFW_ARGS_NH4PTR	0x00080000	/* IPv4 next hop in next_hop */
#define	IPFW_ARGS_NH6PTR	0x00100000	/* IPv6 next hop in next_hop6 */
#define	IPFW_ARGS_REF		0x00200000	/* valid ipfw_rule_ref	*/
#define	IPFW_ARGS_IN		0x00400000	/* called on input */
#define	IPFW_ARGS_OUT		0x00800000	/* called on output */
#define	IPFW_ARGS_IP4		0x01000000	/* belongs to v4 ISR */
#define	IPFW_ARGS_IP6		0x02000000	/* belongs to v6 ISR */
#define	IPFW_ARGS_DROP		0x04000000	/* drop it (dummynet) */
#define	IPFW_ARGS_LENMASK	0x0000ffff	/* length of data in *mem */
#define	IPFW_ARGS_LENGTH(f)	((f) & IPFW_ARGS_LENMASK)
	/*
	 * On return, it points to the matching rule.
	 * On entry, rule.slot > 0 means the info is valid and
	 * contains the starting rule for an ipfw search.
	 * If chain_id == chain->id && slot >0 then jump to that slot.
	 * Otherwise, we locate the first rule >= rulenum:rule_id
	 */
	struct ipfw_rule_ref	rule;	/* match/restart info		*/

	struct ifnet		*ifp;	/* input/output interface	*/
	struct inpcb		*inp;
	union {
		/*
		 * next_hop[6] pointers can be used to point to next hop
		 * stored in rule's opcode to avoid copying into hopstore.
		 * Also, it is expected that all 0x1-0x10 flags are mutually
		 * exclusive.
		 */
		struct sockaddr_in	*next_hop;
		struct sockaddr_in6	*next_hop6;
		/* ipfw next hop storage */
		struct sockaddr_in	hopstore;
		struct ip_fw_nh6 {
			struct in6_addr sin6_addr;
			uint32_t	sin6_scope_id;
			uint16_t	sin6_port;
		} hopstore6;
	};
	union {
		struct mbuf	*m;	/* the mbuf chain		*/
		void		*mem;	/* or memory pointer		*/
	};
	struct ipfw_flow_id	f_id;	/* grabbed from IP header	*/
};

MALLOC_DECLARE(M_IPFW);

/* wrapper for freeing a packet, in case we need to do more work */
#ifndef FREE_PKT
#if defined(__linux__) || defined(_WIN32)
#define FREE_PKT(m)	netisr_dispatch(-1, m)
#else
#define FREE_PKT(m)	m_freem(m)
#endif
#endif /* !FREE_PKT */

/*
 * Function definitions.
 */
int ipfw_chk(struct ip_fw_args *args);
struct mbuf *ipfw_send_pkt(struct mbuf *, struct ipfw_flow_id *,
    u_int32_t, u_int32_t, int);

int ipfw_attach_hooks(void);
void ipfw_detach_hooks(void);
#ifdef NOTYET
void ipfw_nat_destroy(void);
#endif

/* In ip_fw_log.c */
struct ip;
struct ip_fw_chain;

void ipfw_bpf_init(int);
void ipfw_bpf_uninit(int);
void ipfw_bpf_tap(u_char *, u_int);
void ipfw_bpf_mtap(struct mbuf *);
void ipfw_bpf_mtap2(void *, u_int, struct mbuf *);
void ipfw_log(struct ip_fw_chain *chain, struct ip_fw *f, u_int hlen,
    struct ip_fw_args *args, u_short offset, uint32_t tablearg, struct ip *ip);
VNET_DECLARE(u_int64_t, norule_counter);
#define	V_norule_counter	VNET(norule_counter)
VNET_DECLARE(int, verbose_limit);
#define	V_verbose_limit		VNET(verbose_limit)

/* In ip_fw_dynamic.c */
struct sockopt_data;

enum { /* result for matching dynamic rules */
	MATCH_REVERSE = 0,
	MATCH_FORWARD,
	MATCH_NONE,
	MATCH_UNKNOWN,
};

/*
 * Macro to determine that we need to do or redo dynamic state lookup.
 * direction == MATCH_UNKNOWN means that this is first lookup, then we need
 * to do lookup.
 * Otherwise check the state name, if previous lookup was for "any" name,
 * this means there is no state with specific name. Thus no need to do
 * lookup. If previous name was not "any", redo lookup for specific name.
 */
#define	DYN_LOOKUP_NEEDED(p, cmd)	\
    ((p)->direction == MATCH_UNKNOWN ||	\
	((p)->kidx != 0 && (p)->kidx != (cmd)->arg1))
#define	DYN_INFO_INIT(p)	do {	\
	(p)->direction = MATCH_UNKNOWN;	\
	(p)->kidx = 0;			\
} while (0)
struct ipfw_dyn_info {
	uint16_t	direction;	/* match direction */
	uint16_t	kidx;		/* state name kidx */
	uint32_t	hashval;	/* hash value */
	uint32_t	version;	/* bucket version */
	uint32_t	f_pos;
};
int ipfw_dyn_install_state(struct ip_fw_chain *chain, struct ip_fw *rule,
    const ipfw_insn_limit *cmd, const struct ip_fw_args *args,
    const void *ulp, int pktlen, struct ipfw_dyn_info *info,
    uint32_t tablearg);
struct ip_fw *ipfw_dyn_lookup_state(const struct ip_fw_args *args,
    const void *ulp, int pktlen, const ipfw_insn *cmd,
    struct ipfw_dyn_info *info);

int ipfw_is_dyn_rule(struct ip_fw *rule);
void ipfw_expire_dyn_states(struct ip_fw_chain *, ipfw_range_tlv *);
void ipfw_get_dynamic(struct ip_fw_chain *chain, char **bp, const char *ep);
int ipfw_dump_states(struct ip_fw_chain *chain, struct sockopt_data *sd);

void ipfw_dyn_init(struct ip_fw_chain *);	/* per-vnet initialization */
void ipfw_dyn_uninit(int);	/* per-vnet deinitialization */
int ipfw_dyn_len(void);
uint32_t ipfw_dyn_get_count(uint32_t *, int *);
void ipfw_dyn_reset_eaction(struct ip_fw_chain *ch, uint16_t eaction_id,
    uint16_t default_id, uint16_t instance_id);

/* common variables */
VNET_DECLARE(int, fw_one_pass);
#define	V_fw_one_pass		VNET(fw_one_pass)

VNET_DECLARE(int, fw_verbose);
#define	V_fw_verbose		VNET(fw_verbose)

VNET_DECLARE(struct ip_fw_chain, layer3_chain);
#define	V_layer3_chain		VNET(layer3_chain)

VNET_DECLARE(int, ipfw_vnet_ready);
#define	V_ipfw_vnet_ready	VNET(ipfw_vnet_ready)

VNET_DECLARE(u_int32_t, set_disable);
#define	V_set_disable		VNET(set_disable)

VNET_DECLARE(int, autoinc_step);
#define V_autoinc_step		VNET(autoinc_step)

VNET_DECLARE(unsigned int, fw_tables_max);
#define V_fw_tables_max		VNET(fw_tables_max)

VNET_DECLARE(unsigned int, fw_tables_sets);
#define V_fw_tables_sets	VNET(fw_tables_sets)

struct tables_config;

#ifdef _KERNEL
/*
 * Here we have the structure representing an ipfw rule.
 *
 * It starts with a general area 
 * followed by an array of one or more instructions, which the code
 * accesses as an array of 32-bit values.
 *
 * Given a rule pointer  r:
 *
 *  r->cmd		is the start of the first instruction.
 *  ACTION_PTR(r)	is the start of the first action (things to do
 *			once a rule matched).
 */

struct ip_fw {
	uint16_t	act_ofs;	/* offset of action in 32-bit units */
	uint16_t	cmd_len;	/* # of 32-bit words in cmd	*/
	uint16_t	rulenum;	/* rule number			*/
	uint8_t		set;		/* rule set (0..31)		*/
	uint8_t		flags;		/* currently unused		*/
	counter_u64_t	cntr;		/* Pointer to rule counters	*/
	uint32_t	timestamp;	/* tv_sec of last match		*/
	uint32_t	id;		/* rule id			*/
	uint32_t	cached_id;	/* used by jump_fast		*/
	uint32_t	cached_pos;	/* used by jump_fast		*/
	uint32_t	refcnt;		/* number of references		*/

	struct ip_fw	*next;		/* linked list of deleted rules */
	ipfw_insn	cmd[1];		/* storage for commands		*/
};

#define	IPFW_RULE_CNTR_SIZE	(2 * sizeof(uint64_t))

#endif

struct ip_fw_chain {
	struct ip_fw	**map;		/* array of rule ptrs to ease lookup */
	uint32_t	id;		/* ruleset id */
	int		n_rules;	/* number of static rules */
	void		*tablestate;	/* runtime table info */
	void		*valuestate;	/* runtime table value info */
	int		*idxmap;	/* skipto array of rules */
	void		**srvstate;	/* runtime service mappings */
#if defined( __linux__ ) || defined( _WIN32 )
	spinlock_t rwmtx;
#else
	struct rmlock	rwmtx;
#endif
	int		static_len;	/* total len of static rules (v0) */
	uint32_t	gencnt;		/* NAT generation count */
	LIST_HEAD(nat_list, cfg_nat) nat;       /* list of nat entries */
	struct ip_fw	*default_rule;
	struct tables_config *tblcfg;	/* tables module data */
	void		*ifcfg;		/* interface module data */
	int		*idxmap_back;	/* standby skipto array of rules */
	struct namedobj_instance	*srvmap; /* cfg name->number mappings */
#if defined( __linux__ ) || defined( _WIN32 )
	spinlock_t uh_lock;
#else
	struct rwlock	uh_lock;	/* lock for upper half */
#endif
};

/* 64-byte structure representing multi-field table value */
struct table_value {
	uint32_t	tag;		/* O_TAG/O_TAGGED */
	uint32_t	pipe;		/* O_PIPE/O_QUEUE */
	uint16_t	divert;		/* O_DIVERT/O_TEE */
	uint16_t	skipto;		/* skipto, CALLRET */
	uint32_t	netgraph;	/* O_NETGRAPH/O_NGTEE */
	uint32_t	fib;		/* O_SETFIB */
	uint32_t	nat;		/* O_NAT */
	uint32_t	nh4;
	uint8_t		dscp;
	uint8_t		spare0;
	uint16_t	spare1;
	/* -- 32 bytes -- */
	struct in6_addr	nh6;
	uint32_t	limit;		/* O_LIMIT */
	uint32_t	zoneid;		/* scope zone id for nh6 */
	uint64_t	refcnt;		/* Number of references */
};


struct named_object {
	TAILQ_ENTRY(named_object)	nn_next;	/* namehash */
	TAILQ_ENTRY(named_object)	nv_next;	/* valuehash */
	char			*name;	/* object name */
	uint16_t		etlv;	/* Export TLV id */
	uint8_t			subtype;/* object subtype within class */
	uint8_t			set;	/* set object belongs to */
	uint16_t		kidx;	/* object kernel index */
	uint16_t		spare;
	uint32_t		ocnt;	/* object counter for internal use */
	uint32_t		refcnt;	/* number of references */
};
TAILQ_HEAD(namedobjects_head, named_object);

struct sockopt;	/* used by tcp_var.h */
struct sockopt_data {
	caddr_t		kbuf;		/* allocated buffer */
	size_t		ksize;		/* given buffer size */
	size_t		koff;		/* data already used */
	size_t		kavail;		/* number of bytes available */
	size_t		ktotal;		/* total bytes pushed */
	struct sockopt	*sopt;		/* socket data */
	caddr_t		sopt_val;	/* sopt user buffer */
	size_t		valsize;	/* original data size */
};

struct ipfw_ifc;

typedef void (ipfw_ifc_cb)(struct ip_fw_chain *ch, void *cbdata,
    uint16_t ifindex);

struct ipfw_iface {
	struct named_object	no;
	char ifname[64];
	int resolved;
	uint16_t ifindex;
	uint16_t spare;
	uint64_t gencnt;
	TAILQ_HEAD(, ipfw_ifc)	consumers;
};

struct ipfw_ifc {
	TAILQ_ENTRY(ipfw_ifc)	next;
	struct ipfw_iface	*iface;
	ipfw_ifc_cb		*cb;
	void			*cbdata;
};

/* Macro for working with various counters */
#define	IPFW_INC_RULE_COUNTER(_cntr, _bytes)	do {	\
	counter_u64_add((_cntr)->cntr, 1);		\
	counter_u64_add((_cntr)->cntr + 1, _bytes);	\
	if ((_cntr)->timestamp != time_uptime)		\
		(_cntr)->timestamp = time_uptime;	\
	} while (0)

#define	IPFW_INC_DYN_COUNTER(_cntr, _bytes)	do {		\
	(_cntr)->pcnt++;				\
	(_cntr)->bcnt += _bytes;			\
	} while (0)

#define	IPFW_ZERO_RULE_COUNTER(_cntr) do {		\
	counter_u64_zero((_cntr)->cntr);		\
	counter_u64_zero((_cntr)->cntr + 1);		\
	(_cntr)->timestamp = 0;				\
	} while (0)

#define	IPFW_ZERO_DYN_COUNTER(_cntr) do {		\
	(_cntr)->pcnt = 0;				\
	(_cntr)->bcnt = 0;				\
	} while (0)

#define	TARG_VAL(ch, k, f)	((struct table_value *)((ch)->valuestate))[k].f
#define	IP_FW_ARG_TABLEARG(ch, a, f)	\
	(((a) == IP_FW_TARG) ? TARG_VAL(ch, tablearg, f) : (a))
/*
 * The lock is heavily used by ip_fw2.c (the main file) and ip_fw_nat.c
 * so the variable and the macros must be here.
 */

#if defined( __linux__ ) || defined( _WIN32 )
#define	IPFW_LOCK_INIT(_chain) do {			\
	rw_init(&(_chain)->rwmtx, "IPFW static rules");	\
	rw_init(&(_chain)->uh_lock, "IPFW UH lock");	\
	} while (0)

#define	IPFW_LOCK_DESTROY(_chain) do {			\
	rw_destroy(&(_chain)->rwmtx);			\
	rw_destroy(&(_chain)->uh_lock);			\
	} while (0)

#define	IPFW_RLOCK_ASSERT(_chain)	rw_assert(&(_chain)->rwmtx, RA_RLOCKED)
#define	IPFW_WLOCK_ASSERT(_chain)	rw_assert(&(_chain)->rwmtx, RA_WLOCKED)

#define	IPFW_RLOCK_TRACKER
#define	IPFW_RLOCK(p)			rw_rlock(&(p)->rwmtx)
#define	IPFW_RUNLOCK(p)			rw_runlock(&(p)->rwmtx)
#define	IPFW_WLOCK(p)			rw_wlock(&(p)->rwmtx)
#define	IPFW_WUNLOCK(p)			rw_wunlock(&(p)->rwmtx)
#define	IPFW_PF_RLOCK(p)		IPFW_RLOCK(p)
#define	IPFW_PF_RUNLOCK(p)		IPFW_RUNLOCK(p)
#else /* FreeBSD */
#define	IPFW_LOCK_INIT(_chain) do {			\
	rm_init_flags(&(_chain)->rwmtx, "IPFW static rules", RM_RECURSE); \
	rw_init(&(_chain)->uh_lock, "IPFW UH lock");	\
	} while (0)

#define	IPFW_LOCK_DESTROY(_chain) do {			\
	rm_destroy(&(_chain)->rwmtx);			\
	rw_destroy(&(_chain)->uh_lock);			\
	} while (0)

#define	IPFW_RLOCK_ASSERT(_chain)	rm_assert(&(_chain)->rwmtx, RA_RLOCKED)
#define	IPFW_WLOCK_ASSERT(_chain)	rm_assert(&(_chain)->rwmtx, RA_WLOCKED)

#define	IPFW_RLOCK_TRACKER		struct rm_priotracker _tracker
#define	IPFW_RLOCK(p)			rm_rlock(&(p)->rwmtx, &_tracker)
#define	IPFW_RUNLOCK(p)			rm_runlock(&(p)->rwmtx, &_tracker)
#define	IPFW_WLOCK(p)			rm_wlock(&(p)->rwmtx)
#define	IPFW_WUNLOCK(p)			rm_wunlock(&(p)->rwmtx)
#define	IPFW_PF_RLOCK(p)		IPFW_RLOCK(p)
#define	IPFW_PF_RUNLOCK(p)		IPFW_RUNLOCK(p)
#endif

#define	IPFW_UH_RLOCK_ASSERT(_chain)	rw_assert(&(_chain)->uh_lock, RA_RLOCKED)
#define	IPFW_UH_WLOCK_ASSERT(_chain)	rw_assert(&(_chain)->uh_lock, RA_WLOCKED)
#define	IPFW_UH_UNLOCK_ASSERT(_chain)	rw_assert(&(_chain)->uh_lock, RA_UNLOCKED)

#define IPFW_UH_RLOCK(p) rw_rlock(&(p)->uh_lock)
#define IPFW_UH_RUNLOCK(p) rw_runlock(&(p)->uh_lock)
#define IPFW_UH_WLOCK(p) rw_wlock(&(p)->uh_lock)
#define IPFW_UH_WUNLOCK(p) rw_wunlock(&(p)->uh_lock)

struct obj_idx {
	uint16_t	uidx;	/* internal index supplied by userland */
	uint16_t	kidx;	/* kernel object index */
	uint16_t	off;	/* tlv offset from rule end in 4-byte words */
	uint8_t		spare;
	uint8_t		type;	/* object type within its category */
};

struct rule_check_info {
	uint16_t	flags;		/* rule-specific check flags */
	uint16_t	object_opcodes;	/* num of opcodes referencing objects */
	uint16_t	urule_numoff;	/* offset of rulenum in bytes */
	uint8_t		version;	/* rule version */
	uint8_t		spare;
	ipfw_obj_ctlv	*ctlv;		/* name TLV containter */
	struct ip_fw	*krule;		/* resulting rule pointer */
	caddr_t		urule;		/* original rule pointer */
	struct obj_idx	obuf[8];	/* table references storage */
};

/* Legacy interface support */
/*
 * FreeBSD 8 export rule format
 */
struct ip_fw_rule0 {
	struct ip_fw	*x_next;	/* linked list of rules		*/
	struct ip_fw	*next_rule;	/* ptr to next [skipto] rule	*/
	/* 'next_rule' is used to pass up 'set_disable' status		*/

	uint16_t	act_ofs;	/* offset of action in 32-bit units */
	uint16_t	cmd_len;	/* # of 32-bit words in cmd	*/
	uint16_t	rulenum;	/* rule number			*/
	uint8_t		set;		/* rule set (0..31)		*/
	uint8_t		_pad;		/* padding			*/
	uint32_t	id;		/* rule id */

	/* These fields are present in all rules.			*/
	uint64_t	pcnt;		/* Packet counter		*/
	uint64_t	bcnt;		/* Byte counter			*/
	uint32_t	timestamp;	/* tv_sec of last match		*/

	ipfw_insn	cmd[1];		/* storage for commands		*/
};

struct ip_fw_bcounter0 {
	uint64_t	pcnt;		/* Packet counter		*/
	uint64_t	bcnt;		/* Byte counter			*/
	uint32_t	timestamp;	/* tv_sec of last match		*/
};

/* Kernel rule length */
/*
 * RULE _K_ SIZE _V_ ->
 * get kernel size from userland rool version _V_.
 * RULE _U_ SIZE _V_ ->
 * get user size version _V_ from kernel rule
 * RULESIZE _V_ ->
 * get user size rule length 
 */
/* FreeBSD8 <> current kernel format */
#define	RULEUSIZE0(r)	(sizeof(struct ip_fw_rule0) + (r)->cmd_len * 4 - 4)
#define	RULEKSIZE0(r)	roundup2((sizeof(struct ip_fw) + (r)->cmd_len*4 - 4), 8)
/* FreeBSD11 <> current kernel format */
#define	RULEUSIZE1(r)	(roundup2(sizeof(struct ip_fw_rule) + \
    (r)->cmd_len * 4 - 4, 8))
#define	RULEKSIZE1(r)	roundup2((sizeof(struct ip_fw) + (r)->cmd_len*4 - 4), 8)

/*
 * Tables/Objects index rewriting code
 */

/* Default and maximum number of ipfw tables/objects. */
#define	IPFW_TABLES_MAX		65536
#define	IPFW_TABLES_DEFAULT	128
#define	IPFW_OBJECTS_MAX	65536
#define	IPFW_OBJECTS_DEFAULT	1024

#define	CHAIN_TO_SRV(ch)	((ch)->srvmap)
#define	SRV_OBJECT(ch, idx)	((ch)->srvstate[(idx)])

struct tid_info {
	uint32_t	set;	/* table set */
	uint16_t	uidx;	/* table index */
	uint8_t		type;	/* table type */
	uint8_t		atype;
	uint8_t		spare;
	int		tlen;	/* Total TLV size block */
	void		*tlvs;	/* Pointer to first TLV */
};

/*
 * Classifier callback. Checks if @cmd opcode contains kernel object reference.
 * If true, returns its index and type.
 * Returns 0 if match is found, 1 overwise.
 */
typedef int (ipfw_obj_rw_cl)(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype);
/*
 * Updater callback. Sets kernel object reference index to @puidx
 */
typedef void (ipfw_obj_rw_upd)(ipfw_insn *cmd, uint16_t puidx);
/*
 * Finder callback. Tries to find named object by name (specified via @ti).
 * Stores found named object pointer in @pno.
 * If object was not found, NULL is stored.
 *
 * Return 0 if input data was valid.
 */
typedef int (ipfw_obj_fname_cb)(struct ip_fw_chain *ch,
    struct tid_info *ti, struct named_object **pno);
/*
 * Another finder callback. Tries to findex named object by kernel index.
 *
 * Returns pointer to named object or NULL.
 */
typedef struct named_object *(ipfw_obj_fidx_cb)(struct ip_fw_chain *ch,
    uint16_t kidx);
/*
 * Object creator callback. Tries to create object specified by @ti.
 * Stores newly-allocated object index in @pkidx.
 *
 * Returns 0 on success.
 */
typedef int (ipfw_obj_create_cb)(struct ip_fw_chain *ch, struct tid_info *ti,
    uint16_t *pkidx);
/*
 * Object destroy callback. Intended to free resources allocated by
 * create_object callback.
 */
typedef void (ipfw_obj_destroy_cb)(struct ip_fw_chain *ch,
    struct named_object *no);
/*
 * Sets handler callback. Handles moving and swaping set of named object.
 *  SWAP_ALL moves all named objects from set `set' to `new_set' and vise versa;
 *  TEST_ALL checks that there aren't any named object with conflicting names;
 *  MOVE_ALL moves all named objects from set `set' to `new_set';
 *  COUNT_ONE used to count number of references used by object with kidx `set';
 *  TEST_ONE checks that named object with kidx `set' can be moved to `new_set`;
 *  MOVE_ONE moves named object with kidx `set' to set `new_set'.
 */
enum ipfw_sets_cmd {
	SWAP_ALL = 0, TEST_ALL, MOVE_ALL, COUNT_ONE, TEST_ONE, MOVE_ONE
};
typedef int (ipfw_obj_sets_cb)(struct ip_fw_chain *ch,
    uint16_t set, uint8_t new_set, enum ipfw_sets_cmd cmd);


struct opcode_obj_rewrite {
	uint32_t		opcode;		/* Opcode to act upon */
	uint32_t		etlv;		/* Relevant export TLV id  */
	ipfw_obj_rw_cl		*classifier;	/* Check if rewrite is needed */
	ipfw_obj_rw_upd		*update;	/* update cmd with new value */
	ipfw_obj_fname_cb	*find_byname;	/* Find named object by name */
	ipfw_obj_fidx_cb	*find_bykidx;	/* Find named object by kidx */
	ipfw_obj_create_cb	*create_object;	/* Create named object */
	ipfw_obj_destroy_cb	*destroy_object;/* Destroy named object */
	ipfw_obj_sets_cb	*manage_sets;	/* Swap or move sets */
};

#define	IPFW_ADD_OBJ_REWRITER(f, c)	do {	\
	if ((f) != 0) 				\
		ipfw_add_obj_rewriter(c,	\
		    sizeof(c) / sizeof(c[0]));	\
	} while(0)
#define	IPFW_DEL_OBJ_REWRITER(l, c)	do {	\
	if ((l) != 0) 				\
		ipfw_del_obj_rewriter(c,	\
		    sizeof(c) / sizeof(c[0]));	\
	} while(0)

/* In ip_fw_iface.c */
int ipfw_iface_init(void);
void ipfw_iface_destroy(void);
void vnet_ipfw_iface_destroy(struct ip_fw_chain *ch);
int ipfw_iface_ref(struct ip_fw_chain *ch, char *name,
    struct ipfw_ifc *ic);
void ipfw_iface_unref(struct ip_fw_chain *ch, struct ipfw_ifc *ic);
void ipfw_iface_add_notify(struct ip_fw_chain *ch, struct ipfw_ifc *ic);
void ipfw_iface_del_notify(struct ip_fw_chain *ch, struct ipfw_ifc *ic);

/* In ip_fw_sockopt.c */
void ipfw_init_skipto_cache(struct ip_fw_chain *chain);
void ipfw_destroy_skipto_cache(struct ip_fw_chain *chain);
int ipfw_find_rule(struct ip_fw_chain *chain, uint32_t key, uint32_t id);
int ipfw_ctl3(struct sockopt *sopt);
int ipfw_add_protected_rule(struct ip_fw_chain *chain, struct ip_fw *rule,
    int locked);
void ipfw_reap_add(struct ip_fw_chain *chain, struct ip_fw **head,
    struct ip_fw *rule);
void ipfw_reap_rules(struct ip_fw *head);
void ipfw_init_counters(void);
void ipfw_destroy_counters(void);
struct ip_fw *ipfw_alloc_rule(struct ip_fw_chain *chain, size_t rulesize);
void ipfw_free_rule(struct ip_fw *rule);
int ipfw_match_range(struct ip_fw *rule, ipfw_range_tlv *rt);
int ipfw_mark_object_kidx(uint32_t *bmask, uint16_t etlv, uint16_t kidx);

typedef int (sopt_handler_f)(struct ip_fw_chain *ch,
    ip_fw3_opheader *op3, struct sockopt_data *sd);
struct ipfw_sopt_handler {
	uint16_t	opcode;
	uint8_t		version;
	uint8_t		dir;
	sopt_handler_f	*handler;
	uint64_t	refcnt;
};
#define	HDIR_SET	0x01	/* Handler is used to set some data */
#define	HDIR_GET	0x02	/* Handler is used to retrieve data */
#define	HDIR_BOTH	HDIR_GET|HDIR_SET

void ipfw_init_sopt_handler(void);
void ipfw_destroy_sopt_handler(void);
void ipfw_add_sopt_handler(struct ipfw_sopt_handler *sh, size_t count);
int ipfw_del_sopt_handler(struct ipfw_sopt_handler *sh, size_t count);
caddr_t ipfw_get_sopt_space(struct sockopt_data *sd, size_t needed);
caddr_t ipfw_get_sopt_header(struct sockopt_data *sd, size_t needed);
#define	IPFW_ADD_SOPT_HANDLER(f, c)	do {	\
	if ((f) != 0) 				\
		ipfw_add_sopt_handler(c,	\
		    sizeof(c) / sizeof(c[0]));	\
	} while(0)
#define	IPFW_DEL_SOPT_HANDLER(l, c)	do {	\
	if ((l) != 0) 				\
		ipfw_del_sopt_handler(c,	\
		    sizeof(c) / sizeof(c[0]));	\
	} while(0)

struct namedobj_instance;
typedef int (objhash_cb_t)(struct namedobj_instance *ni, struct named_object *,
    void *arg);
typedef uint32_t (objhash_hash_f)(struct namedobj_instance *ni, const void *key,
    uint32_t kopt);
typedef int (objhash_cmp_f)(struct named_object *no, const void *key,
    uint32_t kopt);
struct namedobj_instance *ipfw_objhash_create(uint32_t items);
void ipfw_objhash_destroy(struct namedobj_instance *);
void ipfw_objhash_bitmap_alloc(uint32_t items, void **idx, int *pblocks);
void ipfw_objhash_bitmap_merge(struct namedobj_instance *ni,
    void **idx, int *blocks);
void ipfw_objhash_bitmap_swap(struct namedobj_instance *ni,
    void **idx, int *blocks);
void ipfw_objhash_bitmap_free(void *idx, int blocks);
void ipfw_objhash_set_hashf(struct namedobj_instance *ni, objhash_hash_f *f);
struct named_object *ipfw_objhash_lookup_name(struct namedobj_instance *ni,
    uint32_t set, char *name);
struct named_object *ipfw_objhash_lookup_name_type(struct namedobj_instance *ni,
    uint32_t set, uint32_t type, const char *name);
struct named_object *ipfw_objhash_lookup_kidx(struct namedobj_instance *ni,
    uint16_t idx);
int ipfw_objhash_same_name(struct namedobj_instance *ni, struct named_object *a,
    struct named_object *b);
void ipfw_objhash_add(struct namedobj_instance *ni, struct named_object *no);
void ipfw_objhash_del(struct namedobj_instance *ni, struct named_object *no);
uint32_t ipfw_objhash_count(struct namedobj_instance *ni);
uint32_t ipfw_objhash_count_type(struct namedobj_instance *ni, uint16_t type);
int ipfw_objhash_foreach(struct namedobj_instance *ni, objhash_cb_t *f,
    void *arg);
int ipfw_objhash_foreach_type(struct namedobj_instance *ni, objhash_cb_t *f,
    void *arg, uint16_t type);
int ipfw_objhash_free_idx(struct namedobj_instance *ni, uint16_t idx);
int ipfw_objhash_alloc_idx(void *n, uint16_t *pidx);
void ipfw_objhash_set_funcs(struct namedobj_instance *ni,
    objhash_hash_f *hash_f, objhash_cmp_f *cmp_f);
int ipfw_objhash_find_type(struct namedobj_instance *ni, struct tid_info *ti,
    uint32_t etlv, struct named_object **pno);
void ipfw_export_obj_ntlv(struct named_object *no, ipfw_obj_ntlv *ntlv);
ipfw_obj_ntlv *ipfw_find_name_tlv_type(void *tlvs, int len, uint16_t uidx,
    uint32_t etlv);
void ipfw_init_obj_rewriter(void);
void ipfw_destroy_obj_rewriter(void);
void ipfw_add_obj_rewriter(struct opcode_obj_rewrite *rw, size_t count);
int ipfw_del_obj_rewriter(struct opcode_obj_rewrite *rw, size_t count);

int create_objects_compat(struct ip_fw_chain *ch, ipfw_insn *cmd,
    struct obj_idx *oib, struct obj_idx *pidx, struct tid_info *ti);
void update_opcode_kidx(ipfw_insn *cmd, uint16_t idx);
int classify_opcode_kidx(ipfw_insn *cmd, uint16_t *puidx);
void ipfw_init_srv(struct ip_fw_chain *ch);
void ipfw_destroy_srv(struct ip_fw_chain *ch);
int ipfw_check_object_name_generic(const char *name);
int ipfw_obj_manage_sets(struct namedobj_instance *ni, uint16_t type,
    uint16_t set, uint8_t new_set, enum ipfw_sets_cmd cmd);

/* In ip_fw_eaction.c */
typedef int (ipfw_eaction_t)(struct ip_fw_chain *ch, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done);
int ipfw_eaction_init(struct ip_fw_chain *ch, int first);
void ipfw_eaction_uninit(struct ip_fw_chain *ch, int last);

uint16_t ipfw_add_eaction(struct ip_fw_chain *ch, ipfw_eaction_t handler,
    const char *name);
int ipfw_del_eaction(struct ip_fw_chain *ch, uint16_t eaction_id);
int ipfw_run_eaction(struct ip_fw_chain *ch, struct ip_fw_args *args,
    ipfw_insn *cmd, int *done);
int ipfw_reset_eaction(struct ip_fw_chain *ch, struct ip_fw *rule,
    uint16_t eaction_id, uint16_t default_id, uint16_t instance_id);
int ipfw_reset_eaction_instance(struct ip_fw_chain *ch, uint16_t eaction_id,
    uint16_t instance_id);

/* In ip_fw_table.c */
struct table_info;

typedef int (table_lookup_t)(struct table_info *ti, void *key, uint32_t keylen,
    uint32_t *val);

int ipfw_lookup_table(struct ip_fw_chain *ch, uint16_t tbl, uint16_t plen,
    void *paddr, uint32_t *val);
struct named_object *ipfw_objhash_lookup_table_kidx(struct ip_fw_chain *ch,
    uint16_t kidx);
int ipfw_ref_table(struct ip_fw_chain *ch, ipfw_obj_ntlv *ntlv, uint16_t *kidx);
void ipfw_unref_table(struct ip_fw_chain *ch, uint16_t kidx);
int ipfw_init_tables(struct ip_fw_chain *ch, int first);
int ipfw_resize_tables(struct ip_fw_chain *ch, unsigned int ntables);
int ipfw_switch_tables_namespace(struct ip_fw_chain *ch, unsigned int nsets);
void ipfw_destroy_tables(struct ip_fw_chain *ch, int last);

/* In ip_fw_nat.c -- XXX to be moved to ip_var.h */

extern struct cfg_nat *(*lookup_nat_ptr)(struct nat_list *, int);

typedef int ipfw_nat_t(struct ip_fw_args *, struct cfg_nat *, struct mbuf *);
typedef int ipfw_nat_cfg_t(struct sockopt *);

VNET_DECLARE(int, ipfw_nat_ready);
#define	V_ipfw_nat_ready	VNET(ipfw_nat_ready)
#define	IPFW_NAT_LOADED	(V_ipfw_nat_ready)

extern ipfw_nat_t *ipfw_nat_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_cfg_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_del_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_get_cfg_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_get_log_ptr;

/* Helper functions for IP checksum adjustment */
static __inline uint16_t
cksum_add(uint16_t sum, uint16_t a)
{
	uint16_t res;

	res = sum + a;
	return (res + (res < a));
}

static __inline uint16_t
cksum_adjust(uint16_t oldsum, uint16_t old, uint16_t new)
{

	return (~cksum_add(cksum_add(~oldsum, ~old), new));
}

#endif /* _KERNEL */
#endif /* _IPFW2_PRIVATE_H */
