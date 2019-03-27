/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Isilon Systems, Inc.
 * Copyright (c) 2008 Ilya Maykov <ivmaykov@gmail.com>
 * Copyright (c) 1998 Berkeley Software Design, Inc.
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
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from BSDI $Id: mutex_witness.c,v 1.1.2.20 2000/04/27 03:10:27 cp Exp $
 *	and BSDI $Id: synch_machdep.c,v 2.3.2.39 2000/04/27 03:10:25 cp Exp $
 */

/*
 * Implementation of the `witness' lock verifier.  Originally implemented for
 * mutexes in BSD/OS.  Extended to handle generic lock objects and lock
 * classes in FreeBSD.
 */

/*
 *	Main Entry: witness
 *	Pronunciation: 'wit-n&s
 *	Function: noun
 *	Etymology: Middle English witnesse, from Old English witnes knowledge,
 *	    testimony, witness, from 2wit
 *	Date: before 12th century
 *	1 : attestation of a fact or event : TESTIMONY
 *	2 : one that gives evidence; specifically : one who testifies in
 *	    a cause or before a judicial tribunal
 *	3 : one asked to be present at a transaction so as to be able to
 *	    testify to its having taken place
 *	4 : one who has personal knowledge of something
 *	5 a : something serving as evidence or proof : SIGN
 *	  b : public affirmation by word or example of usually
 *	      religious faith or conviction <the heroic witness to divine
 *	      life -- Pilot>
 *	6 capitalized : a member of the Jehovah's Witnesses 
 */

/*
 * Special rules concerning Giant and lock orders:
 *
 * 1) Giant must be acquired before any other mutexes.  Stated another way,
 *    no other mutex may be held when Giant is acquired.
 *
 * 2) Giant must be released when blocking on a sleepable lock.
 *
 * This rule is less obvious, but is a result of Giant providing the same
 * semantics as spl().  Basically, when a thread sleeps, it must release
 * Giant.  When a thread blocks on a sleepable lock, it sleeps.  Hence rule
 * 2).
 *
 * 3) Giant may be acquired before or after sleepable locks.
 *
 * This rule is also not quite as obvious.  Giant may be acquired after
 * a sleepable lock because it is a non-sleepable lock and non-sleepable
 * locks may always be acquired while holding a sleepable lock.  The second
 * case, Giant before a sleepable lock, follows from rule 2) above.  Suppose
 * you have two threads T1 and T2 and a sleepable lock X.  Suppose that T1
 * acquires X and blocks on Giant.  Then suppose that T2 acquires Giant and
 * blocks on X.  When T2 blocks on X, T2 will release Giant allowing T1 to
 * execute.  Thus, acquiring Giant both before and after a sleepable lock
 * will not result in a lock order reversal.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_hwpmc_hooks.h"
#include "opt_stack.h"
#include "opt_witness.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sched.h>
#include <sys/stack.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <machine/stdarg.h>

#if !defined(DDB) && !defined(STACK)
#error "DDB or STACK options are required for WITNESS"
#endif

/* Note that these traces do not work with KTR_ALQ. */
#if 0
#define	KTR_WITNESS	KTR_SUBSYS
#else
#define	KTR_WITNESS	0
#endif

#define	LI_RECURSEMASK	0x0000ffff	/* Recursion depth of lock instance. */
#define	LI_EXCLUSIVE	0x00010000	/* Exclusive lock instance. */
#define	LI_NORELEASE	0x00020000	/* Lock not allowed to be released. */

/* Define this to check for blessed mutexes */
#undef BLESSING

#ifndef WITNESS_COUNT
#define	WITNESS_COUNT 		1536
#endif
#define	WITNESS_HASH_SIZE	251	/* Prime, gives load factor < 2 */
#define	WITNESS_PENDLIST	(512 + (MAXCPU * 4))

/* Allocate 256 KB of stack data space */
#define	WITNESS_LO_DATA_COUNT	2048

/* Prime, gives load factor of ~2 at full load */
#define	WITNESS_LO_HASH_SIZE	1021

/*
 * XXX: This is somewhat bogus, as we assume here that at most 2048 threads
 * will hold LOCK_NCHILDREN locks.  We handle failure ok, and we should
 * probably be safe for the most part, but it's still a SWAG.
 */
#define	LOCK_NCHILDREN	5
#define	LOCK_CHILDCOUNT	2048

#define	MAX_W_NAME	64

#define	FULLGRAPH_SBUF_SIZE	512

/*
 * These flags go in the witness relationship matrix and describe the
 * relationship between any two struct witness objects.
 */
#define	WITNESS_UNRELATED        0x00    /* No lock order relation. */
#define	WITNESS_PARENT           0x01    /* Parent, aka direct ancestor. */
#define	WITNESS_ANCESTOR         0x02    /* Direct or indirect ancestor. */
#define	WITNESS_CHILD            0x04    /* Child, aka direct descendant. */
#define	WITNESS_DESCENDANT       0x08    /* Direct or indirect descendant. */
#define	WITNESS_ANCESTOR_MASK    (WITNESS_PARENT | WITNESS_ANCESTOR)
#define	WITNESS_DESCENDANT_MASK  (WITNESS_CHILD | WITNESS_DESCENDANT)
#define	WITNESS_RELATED_MASK						\
	(WITNESS_ANCESTOR_MASK | WITNESS_DESCENDANT_MASK)
#define	WITNESS_REVERSAL         0x10    /* A lock order reversal has been
					  * observed. */
#define	WITNESS_RESERVED1        0x20    /* Unused flag, reserved. */
#define	WITNESS_RESERVED2        0x40    /* Unused flag, reserved. */
#define	WITNESS_LOCK_ORDER_KNOWN 0x80    /* This lock order is known. */

/* Descendant to ancestor flags */
#define	WITNESS_DTOA(x)	(((x) & WITNESS_RELATED_MASK) >> 2)

/* Ancestor to descendant flags */
#define	WITNESS_ATOD(x)	(((x) & WITNESS_RELATED_MASK) << 2)

#define	WITNESS_INDEX_ASSERT(i)						\
	MPASS((i) > 0 && (i) <= w_max_used_index && (i) < witness_count)

static MALLOC_DEFINE(M_WITNESS, "Witness", "Witness");

/*
 * Lock instances.  A lock instance is the data associated with a lock while
 * it is held by witness.  For example, a lock instance will hold the
 * recursion count of a lock.  Lock instances are held in lists.  Spin locks
 * are held in a per-cpu list while sleep locks are held in per-thread list.
 */
struct lock_instance {
	struct lock_object	*li_lock;
	const char		*li_file;
	int			li_line;
	u_int			li_flags;
};

/*
 * A simple list type used to build the list of locks held by a thread
 * or CPU.  We can't simply embed the list in struct lock_object since a
 * lock may be held by more than one thread if it is a shared lock.  Locks
 * are added to the head of the list, so we fill up each list entry from
 * "the back" logically.  To ease some of the arithmetic, we actually fill
 * in each list entry the normal way (children[0] then children[1], etc.) but
 * when we traverse the list we read children[count-1] as the first entry
 * down to children[0] as the final entry.
 */
struct lock_list_entry {
	struct lock_list_entry	*ll_next;
	struct lock_instance	ll_children[LOCK_NCHILDREN];
	u_int			ll_count;
};

/*
 * The main witness structure. One of these per named lock type in the system
 * (for example, "vnode interlock").
 */
struct witness {
	char  			w_name[MAX_W_NAME];
	uint32_t 		w_index;  /* Index in the relationship matrix */
	struct lock_class	*w_class;
	STAILQ_ENTRY(witness) 	w_list;		/* List of all witnesses. */
	STAILQ_ENTRY(witness) 	w_typelist;	/* Witnesses of a type. */
	struct witness		*w_hash_next; /* Linked list in hash buckets. */
	const char		*w_file; /* File where last acquired */
	uint32_t 		w_line; /* Line where last acquired */
	uint32_t 		w_refcount;
	uint16_t 		w_num_ancestors; /* direct/indirect
						  * ancestor count */
	uint16_t 		w_num_descendants; /* direct/indirect
						    * descendant count */
	int16_t 		w_ddb_level;
	unsigned		w_displayed:1;
	unsigned		w_reversed:1;
};

STAILQ_HEAD(witness_list, witness);

/*
 * The witness hash table. Keys are witness names (const char *), elements are
 * witness objects (struct witness *).
 */
struct witness_hash {
	struct witness	*wh_array[WITNESS_HASH_SIZE];
	uint32_t	wh_size;
	uint32_t	wh_count;
};

/*
 * Key type for the lock order data hash table.
 */
struct witness_lock_order_key {
	uint16_t	from;
	uint16_t	to;
};

struct witness_lock_order_data {
	struct stack			wlod_stack;
	struct witness_lock_order_key	wlod_key;
	struct witness_lock_order_data	*wlod_next;
};

/*
 * The witness lock order data hash table. Keys are witness index tuples
 * (struct witness_lock_order_key), elements are lock order data objects
 * (struct witness_lock_order_data). 
 */
struct witness_lock_order_hash {
	struct witness_lock_order_data	*wloh_array[WITNESS_LO_HASH_SIZE];
	u_int	wloh_size;
	u_int	wloh_count;
};

#ifdef BLESSING
struct witness_blessed {
	const char	*b_lock1;
	const char	*b_lock2;
};
#endif

struct witness_pendhelp {
	const char		*wh_type;
	struct lock_object	*wh_lock;
};

struct witness_order_list_entry {
	const char		*w_name;
	struct lock_class	*w_class;
};

/*
 * Returns 0 if one of the locks is a spin lock and the other is not.
 * Returns 1 otherwise.
 */
static __inline int
witness_lock_type_equal(struct witness *w1, struct witness *w2)
{

	return ((w1->w_class->lc_flags & (LC_SLEEPLOCK | LC_SPINLOCK)) ==
		(w2->w_class->lc_flags & (LC_SLEEPLOCK | LC_SPINLOCK)));
}

static __inline int
witness_lock_order_key_equal(const struct witness_lock_order_key *a,
    const struct witness_lock_order_key *b)
{

	return (a->from == b->from && a->to == b->to);
}

static int	_isitmyx(struct witness *w1, struct witness *w2, int rmask,
		    const char *fname);
static void	adopt(struct witness *parent, struct witness *child);
#ifdef BLESSING
static int	blessed(struct witness *, struct witness *);
#endif
static void	depart(struct witness *w);
static struct witness	*enroll(const char *description,
			    struct lock_class *lock_class);
static struct lock_instance	*find_instance(struct lock_list_entry *list,
				    const struct lock_object *lock);
static int	isitmychild(struct witness *parent, struct witness *child);
static int	isitmydescendant(struct witness *parent, struct witness *child);
static void	itismychild(struct witness *parent, struct witness *child);
static int	sysctl_debug_witness_badstacks(SYSCTL_HANDLER_ARGS);
static int	sysctl_debug_witness_watch(SYSCTL_HANDLER_ARGS);
static int	sysctl_debug_witness_fullgraph(SYSCTL_HANDLER_ARGS);
static int	sysctl_debug_witness_channel(SYSCTL_HANDLER_ARGS);
static void	witness_add_fullgraph(struct sbuf *sb, struct witness *parent);
#ifdef DDB
static void	witness_ddb_compute_levels(void);
static void	witness_ddb_display(int(*)(const char *fmt, ...));
static void	witness_ddb_display_descendants(int(*)(const char *fmt, ...),
		    struct witness *, int indent);
static void	witness_ddb_display_list(int(*prnt)(const char *fmt, ...),
		    struct witness_list *list);
static void	witness_ddb_level_descendants(struct witness *parent, int l);
static void	witness_ddb_list(struct thread *td);
#endif
static void	witness_debugger(int cond, const char *msg);
static void	witness_free(struct witness *m);
static struct witness	*witness_get(void);
static uint32_t	witness_hash_djb2(const uint8_t *key, uint32_t size);
static struct witness	*witness_hash_get(const char *key);
static void	witness_hash_put(struct witness *w);
static void	witness_init_hash_tables(void);
static void	witness_increment_graph_generation(void);
static void	witness_lock_list_free(struct lock_list_entry *lle);
static struct lock_list_entry	*witness_lock_list_get(void);
static int	witness_lock_order_add(struct witness *parent,
		    struct witness *child);
static int	witness_lock_order_check(struct witness *parent,
		    struct witness *child);
static struct witness_lock_order_data	*witness_lock_order_get(
					    struct witness *parent,
					    struct witness *child);
static void	witness_list_lock(struct lock_instance *instance,
		    int (*prnt)(const char *fmt, ...));
static int	witness_output(const char *fmt, ...) __printflike(1, 2);
static int	witness_voutput(const char *fmt, va_list ap) __printflike(1, 0);
static void	witness_setflag(struct lock_object *lock, int flag, int set);

static SYSCTL_NODE(_debug, OID_AUTO, witness, CTLFLAG_RW, NULL,
    "Witness Locking");

/*
 * If set to 0, lock order checking is disabled.  If set to -1,
 * witness is completely disabled.  Otherwise witness performs full
 * lock order checking for all locks.  At runtime, lock order checking
 * may be toggled.  However, witness cannot be reenabled once it is
 * completely disabled.
 */
static int witness_watch = 1;
SYSCTL_PROC(_debug_witness, OID_AUTO, watch, CTLFLAG_RWTUN | CTLTYPE_INT, NULL, 0,
    sysctl_debug_witness_watch, "I", "witness is watching lock operations");

#ifdef KDB
/*
 * When KDB is enabled and witness_kdb is 1, it will cause the system
 * to drop into kdebug() when:
 *	- a lock hierarchy violation occurs
 *	- locks are held when going to sleep.
 */
#ifdef WITNESS_KDB
int	witness_kdb = 1;
#else
int	witness_kdb = 0;
#endif
SYSCTL_INT(_debug_witness, OID_AUTO, kdb, CTLFLAG_RWTUN, &witness_kdb, 0, "");
#endif /* KDB */

#if defined(DDB) || defined(KDB)
/*
 * When DDB or KDB is enabled and witness_trace is 1, it will cause the system
 * to print a stack trace:
 *	- a lock hierarchy violation occurs
 *	- locks are held when going to sleep.
 */
int	witness_trace = 1;
SYSCTL_INT(_debug_witness, OID_AUTO, trace, CTLFLAG_RWTUN, &witness_trace, 0, "");
#endif /* DDB || KDB */

#ifdef WITNESS_SKIPSPIN
int	witness_skipspin = 1;
#else
int	witness_skipspin = 0;
#endif
SYSCTL_INT(_debug_witness, OID_AUTO, skipspin, CTLFLAG_RDTUN, &witness_skipspin, 0, "");

int badstack_sbuf_size;

int witness_count = WITNESS_COUNT;
SYSCTL_INT(_debug_witness, OID_AUTO, witness_count, CTLFLAG_RDTUN, 
    &witness_count, 0, "");

/*
 * Output channel for witness messages.  By default we print to the console.
 */
enum witness_channel {
	WITNESS_CONSOLE,
	WITNESS_LOG,
	WITNESS_NONE,
};

static enum witness_channel witness_channel = WITNESS_CONSOLE;
SYSCTL_PROC(_debug_witness, OID_AUTO, output_channel, CTLTYPE_STRING |
    CTLFLAG_RWTUN, NULL, 0, sysctl_debug_witness_channel, "A",
    "Output channel for warnings");

/*
 * Call this to print out the relations between locks.
 */
SYSCTL_PROC(_debug_witness, OID_AUTO, fullgraph, CTLTYPE_STRING | CTLFLAG_RD,
    NULL, 0, sysctl_debug_witness_fullgraph, "A", "Show locks relation graphs");

/*
 * Call this to print out the witness faulty stacks.
 */
SYSCTL_PROC(_debug_witness, OID_AUTO, badstacks, CTLTYPE_STRING | CTLFLAG_RD,
    NULL, 0, sysctl_debug_witness_badstacks, "A", "Show bad witness stacks");

static struct mtx w_mtx;

/* w_list */
static struct witness_list w_free = STAILQ_HEAD_INITIALIZER(w_free);
static struct witness_list w_all = STAILQ_HEAD_INITIALIZER(w_all);

/* w_typelist */
static struct witness_list w_spin = STAILQ_HEAD_INITIALIZER(w_spin);
static struct witness_list w_sleep = STAILQ_HEAD_INITIALIZER(w_sleep);

/* lock list */
static struct lock_list_entry *w_lock_list_free = NULL;
static struct witness_pendhelp pending_locks[WITNESS_PENDLIST];
static u_int pending_cnt;

static int w_free_cnt, w_spin_cnt, w_sleep_cnt;
SYSCTL_INT(_debug_witness, OID_AUTO, free_cnt, CTLFLAG_RD, &w_free_cnt, 0, "");
SYSCTL_INT(_debug_witness, OID_AUTO, spin_cnt, CTLFLAG_RD, &w_spin_cnt, 0, "");
SYSCTL_INT(_debug_witness, OID_AUTO, sleep_cnt, CTLFLAG_RD, &w_sleep_cnt, 0,
    "");

static struct witness *w_data;
static uint8_t **w_rmatrix;
static struct lock_list_entry w_locklistdata[LOCK_CHILDCOUNT];
static struct witness_hash w_hash;	/* The witness hash table. */

/* The lock order data hash */
static struct witness_lock_order_data w_lodata[WITNESS_LO_DATA_COUNT];
static struct witness_lock_order_data *w_lofree = NULL;
static struct witness_lock_order_hash w_lohash;
static int w_max_used_index = 0;
static unsigned int w_generation = 0;
static const char w_notrunning[] = "Witness not running\n";
static const char w_stillcold[] = "Witness is still cold\n";
#ifdef __i386__
static const char w_notallowed[] = "The sysctl is disabled on the arch\n";
#endif

static struct witness_order_list_entry order_lists[] = {
	/*
	 * sx locks
	 */
	{ "proctree", &lock_class_sx },
	{ "allproc", &lock_class_sx },
	{ "allprison", &lock_class_sx },
	{ NULL, NULL },
	/*
	 * Various mutexes
	 */
	{ "Giant", &lock_class_mtx_sleep },
	{ "pipe mutex", &lock_class_mtx_sleep },
	{ "sigio lock", &lock_class_mtx_sleep },
	{ "process group", &lock_class_mtx_sleep },
#ifdef	HWPMC_HOOKS
	{ "pmc-sleep", &lock_class_mtx_sleep },
#endif
	{ "process lock", &lock_class_mtx_sleep },
	{ "session", &lock_class_mtx_sleep },
	{ "uidinfo hash", &lock_class_rw },
	{ "time lock", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * umtx
	 */
	{ "umtx lock", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * Sockets
	 */
	{ "accept", &lock_class_mtx_sleep },
	{ "so_snd", &lock_class_mtx_sleep },
	{ "so_rcv", &lock_class_mtx_sleep },
	{ "sellck", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * Routing
	 */
	{ "so_rcv", &lock_class_mtx_sleep },
	{ "radix node head", &lock_class_rm },
	{ "rtentry", &lock_class_mtx_sleep },
	{ "ifaddr", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * IPv4 multicast:
	 * protocol locks before interface locks, after UDP locks.
	 */
	{ "in_multi_sx", &lock_class_sx },
	{ "udpinp", &lock_class_rw },
	{ "in_multi_list_mtx", &lock_class_mtx_sleep },
	{ "igmp_mtx", &lock_class_mtx_sleep },
	{ "ifnet_rw", &lock_class_rw },
	{ "if_addr_lock", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * IPv6 multicast:
	 * protocol locks before interface locks, after UDP locks.
	 */
	{ "in6_multi_sx", &lock_class_sx },
	{ "udpinp", &lock_class_rw },
	{ "in6_multi_list_mtx", &lock_class_mtx_sleep },
	{ "mld_mtx", &lock_class_mtx_sleep },
	{ "ifnet_rw", &lock_class_rw },
	{ "if_addr_lock", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * UNIX Domain Sockets
	 */
	{ "unp_link_rwlock", &lock_class_rw },
	{ "unp_list_lock", &lock_class_mtx_sleep },
	{ "unp", &lock_class_mtx_sleep },
	{ "so_snd", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * UDP/IP
	 */
	{ "udp", &lock_class_mtx_sleep },
	{ "udpinp", &lock_class_rw },
	{ "so_snd", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * TCP/IP
	 */
	{ "tcp", &lock_class_mtx_sleep },
	{ "tcpinp", &lock_class_rw },
	{ "so_snd", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * BPF
	 */
	{ "bpf global lock", &lock_class_sx },
	{ "bpf interface lock", &lock_class_rw },
	{ "bpf cdev lock", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * NFS server
	 */
	{ "nfsd_mtx", &lock_class_mtx_sleep },
	{ "so_snd", &lock_class_mtx_sleep },
	{ NULL, NULL },

	/*
	 * IEEE 802.11
	 */
	{ "802.11 com lock", &lock_class_mtx_sleep},
	{ NULL, NULL },
	/*
	 * Network drivers
	 */
	{ "network driver", &lock_class_mtx_sleep},
	{ NULL, NULL },

	/*
	 * Netgraph
	 */
	{ "ng_node", &lock_class_mtx_sleep },
	{ "ng_worklist", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * CDEV
	 */
	{ "vm map (system)", &lock_class_mtx_sleep },
	{ "vnode interlock", &lock_class_mtx_sleep },
	{ "cdev", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * VM
	 */
	{ "vm map (user)", &lock_class_sx },
	{ "vm object", &lock_class_rw },
	{ "vm page", &lock_class_mtx_sleep },
	{ "pmap pv global", &lock_class_rw },
	{ "pmap", &lock_class_mtx_sleep },
	{ "pmap pv list", &lock_class_rw },
	{ "vm page free queue", &lock_class_mtx_sleep },
	{ "vm pagequeue", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * kqueue/VFS interaction
	 */
	{ "kqueue", &lock_class_mtx_sleep },
	{ "struct mount mtx", &lock_class_mtx_sleep },
	{ "vnode interlock", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * VFS namecache
	 */
	{ "ncvn", &lock_class_mtx_sleep },
	{ "ncbuc", &lock_class_rw },
	{ "vnode interlock", &lock_class_mtx_sleep },
	{ "ncneg", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * ZFS locking
	 */
	{ "dn->dn_mtx", &lock_class_sx },
	{ "dr->dt.di.dr_mtx", &lock_class_sx },
	{ "db->db_mtx", &lock_class_sx },
	{ NULL, NULL },
	/*
	 * TCP log locks
	 */
	{ "TCP ID tree", &lock_class_rw },
	{ "tcp log id bucket", &lock_class_mtx_sleep },
	{ "tcpinp", &lock_class_rw },
	{ "TCP log expireq", &lock_class_mtx_sleep },
	{ NULL, NULL },
	/*
	 * spin locks
	 */
#ifdef SMP
	{ "ap boot", &lock_class_mtx_spin },
#endif
	{ "rm.mutex_mtx", &lock_class_mtx_spin },
	{ "sio", &lock_class_mtx_spin },
#ifdef __i386__
	{ "cy", &lock_class_mtx_spin },
#endif
#ifdef __sparc64__
	{ "pcib_mtx", &lock_class_mtx_spin },
	{ "rtc_mtx", &lock_class_mtx_spin },
#endif
	{ "scc_hwmtx", &lock_class_mtx_spin },
	{ "uart_hwmtx", &lock_class_mtx_spin },
	{ "fast_taskqueue", &lock_class_mtx_spin },
	{ "intr table", &lock_class_mtx_spin },
	{ "process slock", &lock_class_mtx_spin },
	{ "syscons video lock", &lock_class_mtx_spin },
	{ "sleepq chain", &lock_class_mtx_spin },
	{ "rm_spinlock", &lock_class_mtx_spin },
	{ "turnstile chain", &lock_class_mtx_spin },
	{ "turnstile lock", &lock_class_mtx_spin },
	{ "sched lock", &lock_class_mtx_spin },
	{ "td_contested", &lock_class_mtx_spin },
	{ "callout", &lock_class_mtx_spin },
	{ "entropy harvest mutex", &lock_class_mtx_spin },
#ifdef SMP
	{ "smp rendezvous", &lock_class_mtx_spin },
#endif
#ifdef __powerpc__
	{ "tlb0", &lock_class_mtx_spin },
#endif
	{ NULL, NULL },
	{ "sched lock", &lock_class_mtx_spin },
#ifdef	HWPMC_HOOKS
	{ "pmc-per-proc", &lock_class_mtx_spin },
#endif
	{ NULL, NULL },
	/*
	 * leaf locks
	 */
	{ "intrcnt", &lock_class_mtx_spin },
	{ "icu", &lock_class_mtx_spin },
#if defined(SMP) && defined(__sparc64__)
	{ "ipi", &lock_class_mtx_spin },
#endif
#ifdef __i386__
	{ "allpmaps", &lock_class_mtx_spin },
	{ "descriptor tables", &lock_class_mtx_spin },
#endif
	{ "clk", &lock_class_mtx_spin },
	{ "cpuset", &lock_class_mtx_spin },
	{ "mprof lock", &lock_class_mtx_spin },
	{ "zombie lock", &lock_class_mtx_spin },
	{ "ALD Queue", &lock_class_mtx_spin },
#if defined(__i386__) || defined(__amd64__)
	{ "pcicfg", &lock_class_mtx_spin },
	{ "NDIS thread lock", &lock_class_mtx_spin },
#endif
	{ "tw_osl_io_lock", &lock_class_mtx_spin },
	{ "tw_osl_q_lock", &lock_class_mtx_spin },
	{ "tw_cl_io_lock", &lock_class_mtx_spin },
	{ "tw_cl_intr_lock", &lock_class_mtx_spin },
	{ "tw_cl_gen_lock", &lock_class_mtx_spin },
#ifdef	HWPMC_HOOKS
	{ "pmc-leaf", &lock_class_mtx_spin },
#endif
	{ "blocked lock", &lock_class_mtx_spin },
	{ NULL, NULL },
	{ NULL, NULL }
};

#ifdef BLESSING
/*
 * Pairs of locks which have been blessed
 * Don't complain about order problems with blessed locks
 */
static struct witness_blessed blessed_list[] = {
};
#endif

/*
 * This global is set to 0 once it becomes safe to use the witness code.
 */
static int witness_cold = 1;

/*
 * This global is set to 1 once the static lock orders have been enrolled
 * so that a warning can be issued for any spin locks enrolled later.
 */
static int witness_spin_warn = 0;

/* Trim useless garbage from filenames. */
static const char *
fixup_filename(const char *file)
{

	if (file == NULL)
		return (NULL);
	while (strncmp(file, "../", 3) == 0)
		file += 3;
	return (file);
}

/*
 * Calculate the size of early witness structures.
 */
int
witness_startup_count(void)
{
	int sz;

	sz = sizeof(struct witness) * witness_count;
	sz += sizeof(*w_rmatrix) * (witness_count + 1);
	sz += sizeof(*w_rmatrix[0]) * (witness_count + 1) *
	    (witness_count + 1);

	return (sz);
}

/*
 * The WITNESS-enabled diagnostic code.  Note that the witness code does
 * assume that the early boot is single-threaded at least until after this
 * routine is completed.
 */
void
witness_startup(void *mem)
{
	struct lock_object *lock;
	struct witness_order_list_entry *order;
	struct witness *w, *w1;
	uintptr_t p;
	int i;

	p = (uintptr_t)mem;
	w_data = (void *)p;
	p += sizeof(struct witness) * witness_count;

	w_rmatrix = (void *)p;
	p += sizeof(*w_rmatrix) * (witness_count + 1);

	for (i = 0; i < witness_count + 1; i++) {
		w_rmatrix[i] = (void *)p;
		p += sizeof(*w_rmatrix[i]) * (witness_count + 1);
	}
	badstack_sbuf_size = witness_count * 256;

	/*
	 * We have to release Giant before initializing its witness
	 * structure so that WITNESS doesn't get confused.
	 */
	mtx_unlock(&Giant);
	mtx_assert(&Giant, MA_NOTOWNED);

	CTR1(KTR_WITNESS, "%s: initializing witness", __func__);
	mtx_init(&w_mtx, "witness lock", NULL, MTX_SPIN | MTX_QUIET |
	    MTX_NOWITNESS | MTX_NOPROFILE);
	for (i = witness_count - 1; i >= 0; i--) {
		w = &w_data[i];
		memset(w, 0, sizeof(*w));
		w_data[i].w_index = i;	/* Witness index never changes. */
		witness_free(w);
	}
	KASSERT(STAILQ_FIRST(&w_free)->w_index == 0,
	    ("%s: Invalid list of free witness objects", __func__));

	/* Witness with index 0 is not used to aid in debugging. */
	STAILQ_REMOVE_HEAD(&w_free, w_list);
	w_free_cnt--;

	for (i = 0; i < witness_count; i++) {
		memset(w_rmatrix[i], 0, sizeof(*w_rmatrix[i]) * 
		    (witness_count + 1));
	}

	for (i = 0; i < LOCK_CHILDCOUNT; i++)
		witness_lock_list_free(&w_locklistdata[i]);
	witness_init_hash_tables();

	/* First add in all the specified order lists. */
	for (order = order_lists; order->w_name != NULL; order++) {
		w = enroll(order->w_name, order->w_class);
		if (w == NULL)
			continue;
		w->w_file = "order list";
		for (order++; order->w_name != NULL; order++) {
			w1 = enroll(order->w_name, order->w_class);
			if (w1 == NULL)
				continue;
			w1->w_file = "order list";
			itismychild(w, w1);
			w = w1;
		}
	}
	witness_spin_warn = 1;

	/* Iterate through all locks and add them to witness. */
	for (i = 0; pending_locks[i].wh_lock != NULL; i++) {
		lock = pending_locks[i].wh_lock;
		KASSERT(lock->lo_flags & LO_WITNESS,
		    ("%s: lock %s is on pending list but not LO_WITNESS",
		    __func__, lock->lo_name));
		lock->lo_witness = enroll(pending_locks[i].wh_type,
		    LOCK_CLASS(lock));
	}

	/* Mark the witness code as being ready for use. */
	witness_cold = 0;

	mtx_lock(&Giant);
}

void
witness_init(struct lock_object *lock, const char *type)
{
	struct lock_class *class;

	/* Various sanity checks. */
	class = LOCK_CLASS(lock);
	if ((lock->lo_flags & LO_RECURSABLE) != 0 &&
	    (class->lc_flags & LC_RECURSABLE) == 0)
		kassert_panic("%s: lock (%s) %s can not be recursable",
		    __func__, class->lc_name, lock->lo_name);
	if ((lock->lo_flags & LO_SLEEPABLE) != 0 &&
	    (class->lc_flags & LC_SLEEPABLE) == 0)
		kassert_panic("%s: lock (%s) %s can not be sleepable",
		    __func__, class->lc_name, lock->lo_name);
	if ((lock->lo_flags & LO_UPGRADABLE) != 0 &&
	    (class->lc_flags & LC_UPGRADABLE) == 0)
		kassert_panic("%s: lock (%s) %s can not be upgradable",
		    __func__, class->lc_name, lock->lo_name);

	/*
	 * If we shouldn't watch this lock, then just clear lo_witness.
	 * Otherwise, if witness_cold is set, then it is too early to
	 * enroll this lock, so defer it to witness_initialize() by adding
	 * it to the pending_locks list.  If it is not too early, then enroll
	 * the lock now.
	 */
	if (witness_watch < 1 || panicstr != NULL ||
	    (lock->lo_flags & LO_WITNESS) == 0)
		lock->lo_witness = NULL;
	else if (witness_cold) {
		pending_locks[pending_cnt].wh_lock = lock;
		pending_locks[pending_cnt++].wh_type = type;
		if (pending_cnt > WITNESS_PENDLIST)
			panic("%s: pending locks list is too small, "
			    "increase WITNESS_PENDLIST\n",
			    __func__);
	} else
		lock->lo_witness = enroll(type, class);
}

void
witness_destroy(struct lock_object *lock)
{
	struct lock_class *class;
	struct witness *w;

	class = LOCK_CLASS(lock);

	if (witness_cold)
		panic("lock (%s) %s destroyed while witness_cold",
		    class->lc_name, lock->lo_name);

	/* XXX: need to verify that no one holds the lock */
	if ((lock->lo_flags & LO_WITNESS) == 0 || lock->lo_witness == NULL)
		return;
	w = lock->lo_witness;

	mtx_lock_spin(&w_mtx);
	MPASS(w->w_refcount > 0);
	w->w_refcount--;

	if (w->w_refcount == 0)
		depart(w);
	mtx_unlock_spin(&w_mtx);
}

#ifdef DDB
static void
witness_ddb_compute_levels(void)
{
	struct witness *w;

	/*
	 * First clear all levels.
	 */
	STAILQ_FOREACH(w, &w_all, w_list)
		w->w_ddb_level = -1;

	/*
	 * Look for locks with no parents and level all their descendants.
	 */
	STAILQ_FOREACH(w, &w_all, w_list) {

		/* If the witness has ancestors (is not a root), skip it. */
		if (w->w_num_ancestors > 0)
			continue;
		witness_ddb_level_descendants(w, 0);
	}
}

static void
witness_ddb_level_descendants(struct witness *w, int l)
{
	int i;

	if (w->w_ddb_level >= l)
		return;

	w->w_ddb_level = l;
	l++;

	for (i = 1; i <= w_max_used_index; i++) {
		if (w_rmatrix[w->w_index][i] & WITNESS_PARENT)
			witness_ddb_level_descendants(&w_data[i], l);
	}
}

static void
witness_ddb_display_descendants(int(*prnt)(const char *fmt, ...),
    struct witness *w, int indent)
{
	int i;

 	for (i = 0; i < indent; i++)
 		prnt(" ");
	prnt("%s (type: %s, depth: %d, active refs: %d)",
	     w->w_name, w->w_class->lc_name,
	     w->w_ddb_level, w->w_refcount);
 	if (w->w_displayed) {
 		prnt(" -- (already displayed)\n");
 		return;
 	}
 	w->w_displayed = 1;
	if (w->w_file != NULL && w->w_line != 0)
		prnt(" -- last acquired @ %s:%d\n", fixup_filename(w->w_file),
		    w->w_line);
	else
		prnt(" -- never acquired\n");
	indent++;
	WITNESS_INDEX_ASSERT(w->w_index);
	for (i = 1; i <= w_max_used_index; i++) {
		if (db_pager_quit)
			return;
		if (w_rmatrix[w->w_index][i] & WITNESS_PARENT)
			witness_ddb_display_descendants(prnt, &w_data[i],
			    indent);
	}
}

static void
witness_ddb_display_list(int(*prnt)(const char *fmt, ...),
    struct witness_list *list)
{
	struct witness *w;

	STAILQ_FOREACH(w, list, w_typelist) {
		if (w->w_file == NULL || w->w_ddb_level > 0)
			continue;

		/* This lock has no anscestors - display its descendants. */
		witness_ddb_display_descendants(prnt, w, 0);
		if (db_pager_quit)
			return;
	}
}
	
static void
witness_ddb_display(int(*prnt)(const char *fmt, ...))
{
	struct witness *w;

	KASSERT(witness_cold == 0, ("%s: witness_cold", __func__));
	witness_ddb_compute_levels();

	/* Clear all the displayed flags. */
	STAILQ_FOREACH(w, &w_all, w_list)
		w->w_displayed = 0;

	/*
	 * First, handle sleep locks which have been acquired at least
	 * once.
	 */
	prnt("Sleep locks:\n");
	witness_ddb_display_list(prnt, &w_sleep);
	if (db_pager_quit)
		return;
	
	/*
	 * Now do spin locks which have been acquired at least once.
	 */
	prnt("\nSpin locks:\n");
	witness_ddb_display_list(prnt, &w_spin);
	if (db_pager_quit)
		return;
	
	/*
	 * Finally, any locks which have not been acquired yet.
	 */
	prnt("\nLocks which were never acquired:\n");
	STAILQ_FOREACH(w, &w_all, w_list) {
		if (w->w_file != NULL || w->w_refcount == 0)
			continue;
		prnt("%s (type: %s, depth: %d)\n", w->w_name,
		    w->w_class->lc_name, w->w_ddb_level);
		if (db_pager_quit)
			return;
	}
}
#endif /* DDB */

int
witness_defineorder(struct lock_object *lock1, struct lock_object *lock2)
{

	if (witness_watch == -1 || panicstr != NULL)
		return (0);

	/* Require locks that witness knows about. */
	if (lock1 == NULL || lock1->lo_witness == NULL || lock2 == NULL ||
	    lock2->lo_witness == NULL)
		return (EINVAL);

	mtx_assert(&w_mtx, MA_NOTOWNED);
	mtx_lock_spin(&w_mtx);

	/*
	 * If we already have either an explicit or implied lock order that
	 * is the other way around, then return an error.
	 */
	if (witness_watch &&
	    isitmydescendant(lock2->lo_witness, lock1->lo_witness)) {
		mtx_unlock_spin(&w_mtx);
		return (EDOOFUS);
	}
	
	/* Try to add the new order. */
	CTR3(KTR_WITNESS, "%s: adding %s as a child of %s", __func__,
	    lock2->lo_witness->w_name, lock1->lo_witness->w_name);
	itismychild(lock1->lo_witness, lock2->lo_witness);
	mtx_unlock_spin(&w_mtx);
	return (0);
}

void
witness_checkorder(struct lock_object *lock, int flags, const char *file,
    int line, struct lock_object *interlock)
{
	struct lock_list_entry *lock_list, *lle;
	struct lock_instance *lock1, *lock2, *plock;
	struct lock_class *class, *iclass;
	struct witness *w, *w1;
	struct thread *td;
	int i, j;

	if (witness_cold || witness_watch < 1 || lock->lo_witness == NULL ||
	    panicstr != NULL)
		return;

	w = lock->lo_witness;
	class = LOCK_CLASS(lock);
	td = curthread;

	if (class->lc_flags & LC_SLEEPLOCK) {

		/*
		 * Since spin locks include a critical section, this check
		 * implicitly enforces a lock order of all sleep locks before
		 * all spin locks.
		 */
		if (td->td_critnest != 0 && !kdb_active)
			kassert_panic("acquiring blockable sleep lock with "
			    "spinlock or critical section held (%s) %s @ %s:%d",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);

		/*
		 * If this is the first lock acquired then just return as
		 * no order checking is needed.
		 */
		lock_list = td->td_sleeplocks;
		if (lock_list == NULL || lock_list->ll_count == 0)
			return;
	} else {

		/*
		 * If this is the first lock, just return as no order
		 * checking is needed.  Avoid problems with thread
		 * migration pinning the thread while checking if
		 * spinlocks are held.  If at least one spinlock is held
		 * the thread is in a safe path and it is allowed to
		 * unpin it.
		 */
		sched_pin();
		lock_list = PCPU_GET(spinlocks);
		if (lock_list == NULL || lock_list->ll_count == 0) {
			sched_unpin();
			return;
		}
		sched_unpin();
	}

	/*
	 * Check to see if we are recursing on a lock we already own.  If
	 * so, make sure that we don't mismatch exclusive and shared lock
	 * acquires.
	 */
	lock1 = find_instance(lock_list, lock);
	if (lock1 != NULL) {
		if ((lock1->li_flags & LI_EXCLUSIVE) != 0 &&
		    (flags & LOP_EXCLUSIVE) == 0) {
			witness_output("shared lock of (%s) %s @ %s:%d\n",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
			witness_output("while exclusively locked from %s:%d\n",
			    fixup_filename(lock1->li_file), lock1->li_line);
			kassert_panic("excl->share");
		}
		if ((lock1->li_flags & LI_EXCLUSIVE) == 0 &&
		    (flags & LOP_EXCLUSIVE) != 0) {
			witness_output("exclusive lock of (%s) %s @ %s:%d\n",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
			witness_output("while share locked from %s:%d\n",
			    fixup_filename(lock1->li_file), lock1->li_line);
			kassert_panic("share->excl");
		}
		return;
	}

	/* Warn if the interlock is not locked exactly once. */
	if (interlock != NULL) {
		iclass = LOCK_CLASS(interlock);
		lock1 = find_instance(lock_list, interlock);
		if (lock1 == NULL)
			kassert_panic("interlock (%s) %s not locked @ %s:%d",
			    iclass->lc_name, interlock->lo_name,
			    fixup_filename(file), line);
		else if ((lock1->li_flags & LI_RECURSEMASK) != 0)
			kassert_panic("interlock (%s) %s recursed @ %s:%d",
			    iclass->lc_name, interlock->lo_name,
			    fixup_filename(file), line);
	}

	/*
	 * Find the previously acquired lock, but ignore interlocks.
	 */
	plock = &lock_list->ll_children[lock_list->ll_count - 1];
	if (interlock != NULL && plock->li_lock == interlock) {
		if (lock_list->ll_count > 1)
			plock =
			    &lock_list->ll_children[lock_list->ll_count - 2];
		else {
			lle = lock_list->ll_next;

			/*
			 * The interlock is the only lock we hold, so
			 * simply return.
			 */
			if (lle == NULL)
				return;
			plock = &lle->ll_children[lle->ll_count - 1];
		}
	}
	
	/*
	 * Try to perform most checks without a lock.  If this succeeds we
	 * can skip acquiring the lock and return success.  Otherwise we redo
	 * the check with the lock held to handle races with concurrent updates.
	 */
	w1 = plock->li_lock->lo_witness;
	if (witness_lock_order_check(w1, w))
		return;

	mtx_lock_spin(&w_mtx);
	if (witness_lock_order_check(w1, w)) {
		mtx_unlock_spin(&w_mtx);
		return;
	}
	witness_lock_order_add(w1, w);

	/*
	 * Check for duplicate locks of the same type.  Note that we only
	 * have to check for this on the last lock we just acquired.  Any
	 * other cases will be caught as lock order violations.
	 */
	if (w1 == w) {
		i = w->w_index;
		if (!(lock->lo_flags & LO_DUPOK) && !(flags & LOP_DUPOK) &&
		    !(w_rmatrix[i][i] & WITNESS_REVERSAL)) {
		    w_rmatrix[i][i] |= WITNESS_REVERSAL;
			w->w_reversed = 1;
			mtx_unlock_spin(&w_mtx);
			witness_output(
			    "acquiring duplicate lock of same type: \"%s\"\n", 
			    w->w_name);
			witness_output(" 1st %s @ %s:%d\n", plock->li_lock->lo_name,
			    fixup_filename(plock->li_file), plock->li_line);
			witness_output(" 2nd %s @ %s:%d\n", lock->lo_name,
			    fixup_filename(file), line);
			witness_debugger(1, __func__);
		} else
			mtx_unlock_spin(&w_mtx);
		return;
	}
	mtx_assert(&w_mtx, MA_OWNED);

	/*
	 * If we know that the lock we are acquiring comes after
	 * the lock we most recently acquired in the lock order tree,
	 * then there is no need for any further checks.
	 */
	if (isitmychild(w1, w))
		goto out;

	for (j = 0, lle = lock_list; lle != NULL; lle = lle->ll_next) {
		for (i = lle->ll_count - 1; i >= 0; i--, j++) {

			MPASS(j < LOCK_CHILDCOUNT * LOCK_NCHILDREN);
			lock1 = &lle->ll_children[i];

			/*
			 * Ignore the interlock.
			 */
			if (interlock == lock1->li_lock)
				continue;

			/*
			 * If this lock doesn't undergo witness checking,
			 * then skip it.
			 */
			w1 = lock1->li_lock->lo_witness;
			if (w1 == NULL) {
				KASSERT((lock1->li_lock->lo_flags & LO_WITNESS) == 0,
				    ("lock missing witness structure"));
				continue;
			}

			/*
			 * If we are locking Giant and this is a sleepable
			 * lock, then skip it.
			 */
			if ((lock1->li_lock->lo_flags & LO_SLEEPABLE) != 0 &&
			    lock == &Giant.lock_object)
				continue;

			/*
			 * If we are locking a sleepable lock and this lock
			 * is Giant, then skip it.
			 */
			if ((lock->lo_flags & LO_SLEEPABLE) != 0 &&
			    lock1->li_lock == &Giant.lock_object)
				continue;

			/*
			 * If we are locking a sleepable lock and this lock
			 * isn't sleepable, we want to treat it as a lock
			 * order violation to enfore a general lock order of
			 * sleepable locks before non-sleepable locks.
			 */
			if (((lock->lo_flags & LO_SLEEPABLE) != 0 &&
			    (lock1->li_lock->lo_flags & LO_SLEEPABLE) == 0))
				goto reversal;

			/*
			 * If we are locking Giant and this is a non-sleepable
			 * lock, then treat it as a reversal.
			 */
			if ((lock1->li_lock->lo_flags & LO_SLEEPABLE) == 0 &&
			    lock == &Giant.lock_object)
				goto reversal;

			/*
			 * Check the lock order hierarchy for a reveresal.
			 */
			if (!isitmydescendant(w, w1))
				continue;
		reversal:

			/*
			 * We have a lock order violation, check to see if it
			 * is allowed or has already been yelled about.
			 */
#ifdef BLESSING

			/*
			 * If the lock order is blessed, just bail.  We don't
			 * look for other lock order violations though, which
			 * may be a bug.
			 */
			if (blessed(w, w1))
				goto out;
#endif

			/* Bail if this violation is known */
			if (w_rmatrix[w1->w_index][w->w_index] & WITNESS_REVERSAL)
				goto out;

			/* Record this as a violation */
			w_rmatrix[w1->w_index][w->w_index] |= WITNESS_REVERSAL;
			w_rmatrix[w->w_index][w1->w_index] |= WITNESS_REVERSAL;
			w->w_reversed = w1->w_reversed = 1;
			witness_increment_graph_generation();
			mtx_unlock_spin(&w_mtx);

#ifdef WITNESS_NO_VNODE
			/*
			 * There are known LORs between VNODE locks. They are
			 * not an indication of a bug. VNODE locks are flagged
			 * as such (LO_IS_VNODE) and we don't yell if the LOR
			 * is between 2 VNODE locks.
			 */
			if ((lock->lo_flags & LO_IS_VNODE) != 0 &&
			    (lock1->li_lock->lo_flags & LO_IS_VNODE) != 0)
				return;
#endif

			/*
			 * Ok, yell about it.
			 */
			if (((lock->lo_flags & LO_SLEEPABLE) != 0 &&
			    (lock1->li_lock->lo_flags & LO_SLEEPABLE) == 0))
				witness_output(
		"lock order reversal: (sleepable after non-sleepable)\n");
			else if ((lock1->li_lock->lo_flags & LO_SLEEPABLE) == 0
			    && lock == &Giant.lock_object)
				witness_output(
		"lock order reversal: (Giant after non-sleepable)\n");
			else
				witness_output("lock order reversal:\n");

			/*
			 * Try to locate an earlier lock with
			 * witness w in our list.
			 */
			do {
				lock2 = &lle->ll_children[i];
				MPASS(lock2->li_lock != NULL);
				if (lock2->li_lock->lo_witness == w)
					break;
				if (i == 0 && lle->ll_next != NULL) {
					lle = lle->ll_next;
					i = lle->ll_count - 1;
					MPASS(i >= 0 && i < LOCK_NCHILDREN);
				} else
					i--;
			} while (i >= 0);
			if (i < 0) {
				witness_output(" 1st %p %s (%s) @ %s:%d\n",
				    lock1->li_lock, lock1->li_lock->lo_name,
				    w1->w_name, fixup_filename(lock1->li_file),
				    lock1->li_line);
				witness_output(" 2nd %p %s (%s) @ %s:%d\n", lock,
				    lock->lo_name, w->w_name,
				    fixup_filename(file), line);
			} else {
				witness_output(" 1st %p %s (%s) @ %s:%d\n",
				    lock2->li_lock, lock2->li_lock->lo_name,
				    lock2->li_lock->lo_witness->w_name,
				    fixup_filename(lock2->li_file),
				    lock2->li_line);
				witness_output(" 2nd %p %s (%s) @ %s:%d\n",
				    lock1->li_lock, lock1->li_lock->lo_name,
				    w1->w_name, fixup_filename(lock1->li_file),
				    lock1->li_line);
				witness_output(" 3rd %p %s (%s) @ %s:%d\n", lock,
				    lock->lo_name, w->w_name,
				    fixup_filename(file), line);
			}
			witness_debugger(1, __func__);
			return;
		}
	}

	/*
	 * If requested, build a new lock order.  However, don't build a new
	 * relationship between a sleepable lock and Giant if it is in the
	 * wrong direction.  The correct lock order is that sleepable locks
	 * always come before Giant.
	 */
	if (flags & LOP_NEWORDER &&
	    !(plock->li_lock == &Giant.lock_object &&
	    (lock->lo_flags & LO_SLEEPABLE) != 0)) {
		CTR3(KTR_WITNESS, "%s: adding %s as a child of %s", __func__,
		    w->w_name, plock->li_lock->lo_witness->w_name);
		itismychild(plock->li_lock->lo_witness, w);
	}
out:
	mtx_unlock_spin(&w_mtx);
}

void
witness_lock(struct lock_object *lock, int flags, const char *file, int line)
{
	struct lock_list_entry **lock_list, *lle;
	struct lock_instance *instance;
	struct witness *w;
	struct thread *td;

	if (witness_cold || witness_watch == -1 || lock->lo_witness == NULL ||
	    panicstr != NULL)
		return;
	w = lock->lo_witness;
	td = curthread;

	/* Determine lock list for this lock. */
	if (LOCK_CLASS(lock)->lc_flags & LC_SLEEPLOCK)
		lock_list = &td->td_sleeplocks;
	else
		lock_list = PCPU_PTR(spinlocks);

	/* Check to see if we are recursing on a lock we already own. */
	instance = find_instance(*lock_list, lock);
	if (instance != NULL) {
		instance->li_flags++;
		CTR4(KTR_WITNESS, "%s: pid %d recursed on %s r=%d", __func__,
		    td->td_proc->p_pid, lock->lo_name,
		    instance->li_flags & LI_RECURSEMASK);
		instance->li_file = file;
		instance->li_line = line;
		return;
	}

	/* Update per-witness last file and line acquire. */
	w->w_file = file;
	w->w_line = line;

	/* Find the next open lock instance in the list and fill it. */
	lle = *lock_list;
	if (lle == NULL || lle->ll_count == LOCK_NCHILDREN) {
		lle = witness_lock_list_get();
		if (lle == NULL)
			return;
		lle->ll_next = *lock_list;
		CTR3(KTR_WITNESS, "%s: pid %d added lle %p", __func__,
		    td->td_proc->p_pid, lle);
		*lock_list = lle;
	}
	instance = &lle->ll_children[lle->ll_count++];
	instance->li_lock = lock;
	instance->li_line = line;
	instance->li_file = file;
	if ((flags & LOP_EXCLUSIVE) != 0)
		instance->li_flags = LI_EXCLUSIVE;
	else
		instance->li_flags = 0;
	CTR4(KTR_WITNESS, "%s: pid %d added %s as lle[%d]", __func__,
	    td->td_proc->p_pid, lock->lo_name, lle->ll_count - 1);
}

void
witness_upgrade(struct lock_object *lock, int flags, const char *file, int line)
{
	struct lock_instance *instance;
	struct lock_class *class;

	KASSERT(witness_cold == 0, ("%s: witness_cold", __func__));
	if (lock->lo_witness == NULL || witness_watch == -1 || panicstr != NULL)
		return;
	class = LOCK_CLASS(lock);
	if (witness_watch) {
		if ((lock->lo_flags & LO_UPGRADABLE) == 0)
			kassert_panic(
			    "upgrade of non-upgradable lock (%s) %s @ %s:%d",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		if ((class->lc_flags & LC_SLEEPLOCK) == 0)
			kassert_panic(
			    "upgrade of non-sleep lock (%s) %s @ %s:%d",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
	}
	instance = find_instance(curthread->td_sleeplocks, lock);
	if (instance == NULL) {
		kassert_panic("upgrade of unlocked lock (%s) %s @ %s:%d",
		    class->lc_name, lock->lo_name,
		    fixup_filename(file), line);
		return;
	}
	if (witness_watch) {
		if ((instance->li_flags & LI_EXCLUSIVE) != 0)
			kassert_panic(
			    "upgrade of exclusive lock (%s) %s @ %s:%d",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		if ((instance->li_flags & LI_RECURSEMASK) != 0)
			kassert_panic(
			    "upgrade of recursed lock (%s) %s r=%d @ %s:%d",
			    class->lc_name, lock->lo_name,
			    instance->li_flags & LI_RECURSEMASK,
			    fixup_filename(file), line);
	}
	instance->li_flags |= LI_EXCLUSIVE;
}

void
witness_downgrade(struct lock_object *lock, int flags, const char *file,
    int line)
{
	struct lock_instance *instance;
	struct lock_class *class;

	KASSERT(witness_cold == 0, ("%s: witness_cold", __func__));
	if (lock->lo_witness == NULL || witness_watch == -1 || panicstr != NULL)
		return;
	class = LOCK_CLASS(lock);
	if (witness_watch) {
		if ((lock->lo_flags & LO_UPGRADABLE) == 0)
			kassert_panic(
			    "downgrade of non-upgradable lock (%s) %s @ %s:%d",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		if ((class->lc_flags & LC_SLEEPLOCK) == 0)
			kassert_panic(
			    "downgrade of non-sleep lock (%s) %s @ %s:%d",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
	}
	instance = find_instance(curthread->td_sleeplocks, lock);
	if (instance == NULL) {
		kassert_panic("downgrade of unlocked lock (%s) %s @ %s:%d",
		    class->lc_name, lock->lo_name,
		    fixup_filename(file), line);
		return;
	}
	if (witness_watch) {
		if ((instance->li_flags & LI_EXCLUSIVE) == 0)
			kassert_panic(
			    "downgrade of shared lock (%s) %s @ %s:%d",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		if ((instance->li_flags & LI_RECURSEMASK) != 0)
			kassert_panic(
			    "downgrade of recursed lock (%s) %s r=%d @ %s:%d",
			    class->lc_name, lock->lo_name,
			    instance->li_flags & LI_RECURSEMASK,
			    fixup_filename(file), line);
	}
	instance->li_flags &= ~LI_EXCLUSIVE;
}

void
witness_unlock(struct lock_object *lock, int flags, const char *file, int line)
{
	struct lock_list_entry **lock_list, *lle;
	struct lock_instance *instance;
	struct lock_class *class;
	struct thread *td;
	register_t s;
	int i, j;

	if (witness_cold || lock->lo_witness == NULL || panicstr != NULL)
		return;
	td = curthread;
	class = LOCK_CLASS(lock);

	/* Find lock instance associated with this lock. */
	if (class->lc_flags & LC_SLEEPLOCK)
		lock_list = &td->td_sleeplocks;
	else
		lock_list = PCPU_PTR(spinlocks);
	lle = *lock_list;
	for (; *lock_list != NULL; lock_list = &(*lock_list)->ll_next)
		for (i = 0; i < (*lock_list)->ll_count; i++) {
			instance = &(*lock_list)->ll_children[i];
			if (instance->li_lock == lock)
				goto found;
		}

	/*
	 * When disabling WITNESS through witness_watch we could end up in
	 * having registered locks in the td_sleeplocks queue.
	 * We have to make sure we flush these queues, so just search for
	 * eventual register locks and remove them.
	 */
	if (witness_watch > 0) {
		kassert_panic("lock (%s) %s not locked @ %s:%d", class->lc_name,
		    lock->lo_name, fixup_filename(file), line);
		return;
	} else {
		return;
	}
found:

	/* First, check for shared/exclusive mismatches. */
	if ((instance->li_flags & LI_EXCLUSIVE) != 0 && witness_watch > 0 &&
	    (flags & LOP_EXCLUSIVE) == 0) {
		witness_output("shared unlock of (%s) %s @ %s:%d\n",
		    class->lc_name, lock->lo_name, fixup_filename(file), line);
		witness_output("while exclusively locked from %s:%d\n",
		    fixup_filename(instance->li_file), instance->li_line);
		kassert_panic("excl->ushare");
	}
	if ((instance->li_flags & LI_EXCLUSIVE) == 0 && witness_watch > 0 &&
	    (flags & LOP_EXCLUSIVE) != 0) {
		witness_output("exclusive unlock of (%s) %s @ %s:%d\n",
		    class->lc_name, lock->lo_name, fixup_filename(file), line);
		witness_output("while share locked from %s:%d\n",
		    fixup_filename(instance->li_file),
		    instance->li_line);
		kassert_panic("share->uexcl");
	}
	/* If we are recursed, unrecurse. */
	if ((instance->li_flags & LI_RECURSEMASK) > 0) {
		CTR4(KTR_WITNESS, "%s: pid %d unrecursed on %s r=%d", __func__,
		    td->td_proc->p_pid, instance->li_lock->lo_name,
		    instance->li_flags);
		instance->li_flags--;
		return;
	}
	/* The lock is now being dropped, check for NORELEASE flag */
	if ((instance->li_flags & LI_NORELEASE) != 0 && witness_watch > 0) {
		witness_output("forbidden unlock of (%s) %s @ %s:%d\n",
		    class->lc_name, lock->lo_name, fixup_filename(file), line);
		kassert_panic("lock marked norelease");
	}

	/* Otherwise, remove this item from the list. */
	s = intr_disable();
	CTR4(KTR_WITNESS, "%s: pid %d removed %s from lle[%d]", __func__,
	    td->td_proc->p_pid, instance->li_lock->lo_name,
	    (*lock_list)->ll_count - 1);
	for (j = i; j < (*lock_list)->ll_count - 1; j++)
		(*lock_list)->ll_children[j] =
		    (*lock_list)->ll_children[j + 1];
	(*lock_list)->ll_count--;
	intr_restore(s);

	/*
	 * In order to reduce contention on w_mtx, we want to keep always an
	 * head object into lists so that frequent allocation from the 
	 * free witness pool (and subsequent locking) is avoided.
	 * In order to maintain the current code simple, when the head
	 * object is totally unloaded it means also that we do not have
	 * further objects in the list, so the list ownership needs to be
	 * hand over to another object if the current head needs to be freed.
	 */
	if ((*lock_list)->ll_count == 0) {
		if (*lock_list == lle) {
			if (lle->ll_next == NULL)
				return;
		} else
			lle = *lock_list;
		*lock_list = lle->ll_next;
		CTR3(KTR_WITNESS, "%s: pid %d removed lle %p", __func__,
		    td->td_proc->p_pid, lle);
		witness_lock_list_free(lle);
	}
}

void
witness_thread_exit(struct thread *td)
{
	struct lock_list_entry *lle;
	int i, n;

	lle = td->td_sleeplocks;
	if (lle == NULL || panicstr != NULL)
		return;
	if (lle->ll_count != 0) {
		for (n = 0; lle != NULL; lle = lle->ll_next)
			for (i = lle->ll_count - 1; i >= 0; i--) {
				if (n == 0)
					witness_output(
		    "Thread %p exiting with the following locks held:\n", td);
				n++;
				witness_list_lock(&lle->ll_children[i],
				    witness_output);
				
			}
		kassert_panic(
		    "Thread %p cannot exit while holding sleeplocks\n", td);
	}
	witness_lock_list_free(lle);
}

/*
 * Warn if any locks other than 'lock' are held.  Flags can be passed in to
 * exempt Giant and sleepable locks from the checks as well.  If any
 * non-exempt locks are held, then a supplied message is printed to the
 * output channel along with a list of the offending locks.  If indicated in the
 * flags then a failure results in a panic as well.
 */
int
witness_warn(int flags, struct lock_object *lock, const char *fmt, ...)
{
	struct lock_list_entry *lock_list, *lle;
	struct lock_instance *lock1;
	struct thread *td;
	va_list ap;
	int i, n;

	if (witness_cold || witness_watch < 1 || panicstr != NULL)
		return (0);
	n = 0;
	td = curthread;
	for (lle = td->td_sleeplocks; lle != NULL; lle = lle->ll_next)
		for (i = lle->ll_count - 1; i >= 0; i--) {
			lock1 = &lle->ll_children[i];
			if (lock1->li_lock == lock)
				continue;
			if (flags & WARN_GIANTOK &&
			    lock1->li_lock == &Giant.lock_object)
				continue;
			if (flags & WARN_SLEEPOK &&
			    (lock1->li_lock->lo_flags & LO_SLEEPABLE) != 0)
				continue;
			if (n == 0) {
				va_start(ap, fmt);
				vprintf(fmt, ap);
				va_end(ap);
				printf(" with the following %slocks held:\n",
				    (flags & WARN_SLEEPOK) != 0 ?
				    "non-sleepable " : "");
			}
			n++;
			witness_list_lock(lock1, printf);
		}

	/*
	 * Pin the thread in order to avoid problems with thread migration.
	 * Once that all verifies are passed about spinlocks ownership,
	 * the thread is in a safe path and it can be unpinned.
	 */
	sched_pin();
	lock_list = PCPU_GET(spinlocks);
	if (lock_list != NULL && lock_list->ll_count != 0) {
		sched_unpin();

		/*
		 * We should only have one spinlock and as long as
		 * the flags cannot match for this locks class,
		 * check if the first spinlock is the one curthread
		 * should hold.
		 */
		lock1 = &lock_list->ll_children[lock_list->ll_count - 1];
		if (lock_list->ll_count == 1 && lock_list->ll_next == NULL &&
		    lock1->li_lock == lock && n == 0)
			return (0);

		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		printf(" with the following %slocks held:\n",
		    (flags & WARN_SLEEPOK) != 0 ?  "non-sleepable " : "");
		n += witness_list_locks(&lock_list, printf);
	} else
		sched_unpin();
	if (flags & WARN_PANIC && n)
		kassert_panic("%s", __func__);
	else
		witness_debugger(n, __func__);
	return (n);
}

const char *
witness_file(struct lock_object *lock)
{
	struct witness *w;

	if (witness_cold || witness_watch < 1 || lock->lo_witness == NULL)
		return ("?");
	w = lock->lo_witness;
	return (w->w_file);
}

int
witness_line(struct lock_object *lock)
{
	struct witness *w;

	if (witness_cold || witness_watch < 1 || lock->lo_witness == NULL)
		return (0);
	w = lock->lo_witness;
	return (w->w_line);
}

static struct witness *
enroll(const char *description, struct lock_class *lock_class)
{
	struct witness *w;

	MPASS(description != NULL);

	if (witness_watch == -1 || panicstr != NULL)
		return (NULL);
	if ((lock_class->lc_flags & LC_SPINLOCK)) {
		if (witness_skipspin)
			return (NULL);
	} else if ((lock_class->lc_flags & LC_SLEEPLOCK) == 0) {
		kassert_panic("lock class %s is not sleep or spin",
		    lock_class->lc_name);
		return (NULL);
	}

	mtx_lock_spin(&w_mtx);
	w = witness_hash_get(description);
	if (w)
		goto found;
	if ((w = witness_get()) == NULL)
		return (NULL);
	MPASS(strlen(description) < MAX_W_NAME);
	strcpy(w->w_name, description);
	w->w_class = lock_class;
	w->w_refcount = 1;
	STAILQ_INSERT_HEAD(&w_all, w, w_list);
	if (lock_class->lc_flags & LC_SPINLOCK) {
		STAILQ_INSERT_HEAD(&w_spin, w, w_typelist);
		w_spin_cnt++;
	} else if (lock_class->lc_flags & LC_SLEEPLOCK) {
		STAILQ_INSERT_HEAD(&w_sleep, w, w_typelist);
		w_sleep_cnt++;
	}

	/* Insert new witness into the hash */
	witness_hash_put(w);
	witness_increment_graph_generation();
	mtx_unlock_spin(&w_mtx);
	return (w);
found:
	w->w_refcount++;
	if (w->w_refcount == 1)
		w->w_class = lock_class;
	mtx_unlock_spin(&w_mtx);
	if (lock_class != w->w_class)
		kassert_panic(
		    "lock (%s) %s does not match earlier (%s) lock",
		    description, lock_class->lc_name,
		    w->w_class->lc_name);
	return (w);
}

static void
depart(struct witness *w)
{

	MPASS(w->w_refcount == 0);
	if (w->w_class->lc_flags & LC_SLEEPLOCK) {
		w_sleep_cnt--;
	} else {
		w_spin_cnt--;
	}
	/*
	 * Set file to NULL as it may point into a loadable module.
	 */
	w->w_file = NULL;
	w->w_line = 0;
	witness_increment_graph_generation();
}


static void
adopt(struct witness *parent, struct witness *child)
{
	int pi, ci, i, j;

	if (witness_cold == 0)
		mtx_assert(&w_mtx, MA_OWNED);

	/* If the relationship is already known, there's no work to be done. */
	if (isitmychild(parent, child))
		return;

	/* When the structure of the graph changes, bump up the generation. */
	witness_increment_graph_generation();

	/*
	 * The hard part ... create the direct relationship, then propagate all
	 * indirect relationships.
	 */
	pi = parent->w_index;
	ci = child->w_index;
	WITNESS_INDEX_ASSERT(pi);
	WITNESS_INDEX_ASSERT(ci);
	MPASS(pi != ci);
	w_rmatrix[pi][ci] |= WITNESS_PARENT;
	w_rmatrix[ci][pi] |= WITNESS_CHILD;

	/*
	 * If parent was not already an ancestor of child,
	 * then we increment the descendant and ancestor counters.
	 */
	if ((w_rmatrix[pi][ci] & WITNESS_ANCESTOR) == 0) {
		parent->w_num_descendants++;
		child->w_num_ancestors++;
	}

	/* 
	 * Find each ancestor of 'pi'. Note that 'pi' itself is counted as 
	 * an ancestor of 'pi' during this loop.
	 */
	for (i = 1; i <= w_max_used_index; i++) {
		if ((w_rmatrix[i][pi] & WITNESS_ANCESTOR_MASK) == 0 && 
		    (i != pi))
			continue;

		/* Find each descendant of 'i' and mark it as a descendant. */
		for (j = 1; j <= w_max_used_index; j++) {

			/* 
			 * Skip children that are already marked as
			 * descendants of 'i'.
			 */
			if (w_rmatrix[i][j] & WITNESS_ANCESTOR_MASK)
				continue;

			/*
			 * We are only interested in descendants of 'ci'. Note
			 * that 'ci' itself is counted as a descendant of 'ci'.
			 */
			if ((w_rmatrix[ci][j] & WITNESS_ANCESTOR_MASK) == 0 && 
			    (j != ci))
				continue;
			w_rmatrix[i][j] |= WITNESS_ANCESTOR;
			w_rmatrix[j][i] |= WITNESS_DESCENDANT;
			w_data[i].w_num_descendants++;
			w_data[j].w_num_ancestors++;

			/* 
			 * Make sure we aren't marking a node as both an
			 * ancestor and descendant. We should have caught 
			 * this as a lock order reversal earlier.
			 */
			if ((w_rmatrix[i][j] & WITNESS_ANCESTOR_MASK) &&
			    (w_rmatrix[i][j] & WITNESS_DESCENDANT_MASK)) {
				printf("witness rmatrix paradox! [%d][%d]=%d "
				    "both ancestor and descendant\n",
				    i, j, w_rmatrix[i][j]); 
				kdb_backtrace();
				printf("Witness disabled.\n");
				witness_watch = -1;
			}
			if ((w_rmatrix[j][i] & WITNESS_ANCESTOR_MASK) &&
			    (w_rmatrix[j][i] & WITNESS_DESCENDANT_MASK)) {
				printf("witness rmatrix paradox! [%d][%d]=%d "
				    "both ancestor and descendant\n",
				    j, i, w_rmatrix[j][i]); 
				kdb_backtrace();
				printf("Witness disabled.\n");
				witness_watch = -1;
			}
		}
	}
}

static void
itismychild(struct witness *parent, struct witness *child)
{
	int unlocked;

	MPASS(child != NULL && parent != NULL);
	if (witness_cold == 0)
		mtx_assert(&w_mtx, MA_OWNED);

	if (!witness_lock_type_equal(parent, child)) {
		if (witness_cold == 0) {
			unlocked = 1;
			mtx_unlock_spin(&w_mtx);
		} else {
			unlocked = 0;
		}
		kassert_panic(
		    "%s: parent \"%s\" (%s) and child \"%s\" (%s) are not "
		    "the same lock type", __func__, parent->w_name,
		    parent->w_class->lc_name, child->w_name,
		    child->w_class->lc_name);
		if (unlocked)
			mtx_lock_spin(&w_mtx);
	}
	adopt(parent, child);
}

/*
 * Generic code for the isitmy*() functions. The rmask parameter is the
 * expected relationship of w1 to w2.
 */
static int
_isitmyx(struct witness *w1, struct witness *w2, int rmask, const char *fname)
{
	unsigned char r1, r2;
	int i1, i2;

	i1 = w1->w_index;
	i2 = w2->w_index;
	WITNESS_INDEX_ASSERT(i1);
	WITNESS_INDEX_ASSERT(i2);
	r1 = w_rmatrix[i1][i2] & WITNESS_RELATED_MASK;
	r2 = w_rmatrix[i2][i1] & WITNESS_RELATED_MASK;

	/* The flags on one better be the inverse of the flags on the other */
	if (!((WITNESS_ATOD(r1) == r2 && WITNESS_DTOA(r2) == r1) ||
	    (WITNESS_DTOA(r1) == r2 && WITNESS_ATOD(r2) == r1))) {
		/* Don't squawk if we're potentially racing with an update. */
		if (!mtx_owned(&w_mtx))
			return (0);
		printf("%s: rmatrix mismatch between %s (index %d) and %s "
		    "(index %d): w_rmatrix[%d][%d] == %hhx but "
		    "w_rmatrix[%d][%d] == %hhx\n",
		    fname, w1->w_name, i1, w2->w_name, i2, i1, i2, r1,
		    i2, i1, r2);
		kdb_backtrace();
		printf("Witness disabled.\n");
		witness_watch = -1;
	}
	return (r1 & rmask);
}

/*
 * Checks if @child is a direct child of @parent.
 */
static int
isitmychild(struct witness *parent, struct witness *child)
{

	return (_isitmyx(parent, child, WITNESS_PARENT, __func__));
}

/*
 * Checks if @descendant is a direct or inderect descendant of @ancestor.
 */
static int
isitmydescendant(struct witness *ancestor, struct witness *descendant)
{

	return (_isitmyx(ancestor, descendant, WITNESS_ANCESTOR_MASK,
	    __func__));
}

#ifdef BLESSING
static int
blessed(struct witness *w1, struct witness *w2)
{
	int i;
	struct witness_blessed *b;

	for (i = 0; i < nitems(blessed_list); i++) {
		b = &blessed_list[i];
		if (strcmp(w1->w_name, b->b_lock1) == 0) {
			if (strcmp(w2->w_name, b->b_lock2) == 0)
				return (1);
			continue;
		}
		if (strcmp(w1->w_name, b->b_lock2) == 0)
			if (strcmp(w2->w_name, b->b_lock1) == 0)
				return (1);
	}
	return (0);
}
#endif

static struct witness *
witness_get(void)
{
	struct witness *w;
	int index;

	if (witness_cold == 0)
		mtx_assert(&w_mtx, MA_OWNED);

	if (witness_watch == -1) {
		mtx_unlock_spin(&w_mtx);
		return (NULL);
	}
	if (STAILQ_EMPTY(&w_free)) {
		witness_watch = -1;
		mtx_unlock_spin(&w_mtx);
		printf("WITNESS: unable to allocate a new witness object\n");
		return (NULL);
	}
	w = STAILQ_FIRST(&w_free);
	STAILQ_REMOVE_HEAD(&w_free, w_list);
	w_free_cnt--;
	index = w->w_index;
	MPASS(index > 0 && index == w_max_used_index+1 &&
	    index < witness_count);
	bzero(w, sizeof(*w));
	w->w_index = index;
	if (index > w_max_used_index)
		w_max_used_index = index;
	return (w);
}

static void
witness_free(struct witness *w)
{

	STAILQ_INSERT_HEAD(&w_free, w, w_list);
	w_free_cnt++;
}

static struct lock_list_entry *
witness_lock_list_get(void)
{
	struct lock_list_entry *lle;

	if (witness_watch == -1)
		return (NULL);
	mtx_lock_spin(&w_mtx);
	lle = w_lock_list_free;
	if (lle == NULL) {
		witness_watch = -1;
		mtx_unlock_spin(&w_mtx);
		printf("%s: witness exhausted\n", __func__);
		return (NULL);
	}
	w_lock_list_free = lle->ll_next;
	mtx_unlock_spin(&w_mtx);
	bzero(lle, sizeof(*lle));
	return (lle);
}
		
static void
witness_lock_list_free(struct lock_list_entry *lle)
{

	mtx_lock_spin(&w_mtx);
	lle->ll_next = w_lock_list_free;
	w_lock_list_free = lle;
	mtx_unlock_spin(&w_mtx);
}

static struct lock_instance *
find_instance(struct lock_list_entry *list, const struct lock_object *lock)
{
	struct lock_list_entry *lle;
	struct lock_instance *instance;
	int i;

	for (lle = list; lle != NULL; lle = lle->ll_next)
		for (i = lle->ll_count - 1; i >= 0; i--) {
			instance = &lle->ll_children[i];
			if (instance->li_lock == lock)
				return (instance);
		}
	return (NULL);
}

static void
witness_list_lock(struct lock_instance *instance,
    int (*prnt)(const char *fmt, ...))
{
	struct lock_object *lock;

	lock = instance->li_lock;
	prnt("%s %s %s", (instance->li_flags & LI_EXCLUSIVE) != 0 ?
	    "exclusive" : "shared", LOCK_CLASS(lock)->lc_name, lock->lo_name);
	if (lock->lo_witness->w_name != lock->lo_name)
		prnt(" (%s)", lock->lo_witness->w_name);
	prnt(" r = %d (%p) locked @ %s:%d\n",
	    instance->li_flags & LI_RECURSEMASK, lock,
	    fixup_filename(instance->li_file), instance->li_line);
}

static int
witness_output(const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = witness_voutput(fmt, ap);
	va_end(ap);
	return (ret);
}

static int
witness_voutput(const char *fmt, va_list ap)
{
	int ret;

	ret = 0;
	switch (witness_channel) {
	case WITNESS_CONSOLE:
		ret = vprintf(fmt, ap);
		break;
	case WITNESS_LOG:
		vlog(LOG_NOTICE, fmt, ap);
		break;
	case WITNESS_NONE:
		break;
	}
	return (ret);
}

#ifdef DDB
static int
witness_thread_has_locks(struct thread *td)
{

	if (td->td_sleeplocks == NULL)
		return (0);
	return (td->td_sleeplocks->ll_count != 0);
}

static int
witness_proc_has_locks(struct proc *p)
{
	struct thread *td;

	FOREACH_THREAD_IN_PROC(p, td) {
		if (witness_thread_has_locks(td))
			return (1);
	}
	return (0);
}
#endif

int
witness_list_locks(struct lock_list_entry **lock_list,
    int (*prnt)(const char *fmt, ...))
{
	struct lock_list_entry *lle;
	int i, nheld;

	nheld = 0;
	for (lle = *lock_list; lle != NULL; lle = lle->ll_next)
		for (i = lle->ll_count - 1; i >= 0; i--) {
			witness_list_lock(&lle->ll_children[i], prnt);
			nheld++;
		}
	return (nheld);
}

/*
 * This is a bit risky at best.  We call this function when we have timed
 * out acquiring a spin lock, and we assume that the other CPU is stuck
 * with this lock held.  So, we go groveling around in the other CPU's
 * per-cpu data to try to find the lock instance for this spin lock to
 * see when it was last acquired.
 */
void
witness_display_spinlock(struct lock_object *lock, struct thread *owner,
    int (*prnt)(const char *fmt, ...))
{
	struct lock_instance *instance;
	struct pcpu *pc;

	if (owner->td_critnest == 0 || owner->td_oncpu == NOCPU)
		return;
	pc = pcpu_find(owner->td_oncpu);
	instance = find_instance(pc->pc_spinlocks, lock);
	if (instance != NULL)
		witness_list_lock(instance, prnt);
}

void
witness_save(struct lock_object *lock, const char **filep, int *linep)
{
	struct lock_list_entry *lock_list;
	struct lock_instance *instance;
	struct lock_class *class;

	/*
	 * This function is used independently in locking code to deal with
	 * Giant, SCHEDULER_STOPPED() check can be removed here after Giant
	 * is gone.
	 */
	if (SCHEDULER_STOPPED())
		return;
	KASSERT(witness_cold == 0, ("%s: witness_cold", __func__));
	if (lock->lo_witness == NULL || witness_watch == -1 || panicstr != NULL)
		return;
	class = LOCK_CLASS(lock);
	if (class->lc_flags & LC_SLEEPLOCK)
		lock_list = curthread->td_sleeplocks;
	else {
		if (witness_skipspin)
			return;
		lock_list = PCPU_GET(spinlocks);
	}
	instance = find_instance(lock_list, lock);
	if (instance == NULL) {
		kassert_panic("%s: lock (%s) %s not locked", __func__,
		    class->lc_name, lock->lo_name);
		return;
	}
	*filep = instance->li_file;
	*linep = instance->li_line;
}

void
witness_restore(struct lock_object *lock, const char *file, int line)
{
	struct lock_list_entry *lock_list;
	struct lock_instance *instance;
	struct lock_class *class;

	/*
	 * This function is used independently in locking code to deal with
	 * Giant, SCHEDULER_STOPPED() check can be removed here after Giant
	 * is gone.
	 */
	if (SCHEDULER_STOPPED())
		return;
	KASSERT(witness_cold == 0, ("%s: witness_cold", __func__));
	if (lock->lo_witness == NULL || witness_watch == -1 || panicstr != NULL)
		return;
	class = LOCK_CLASS(lock);
	if (class->lc_flags & LC_SLEEPLOCK)
		lock_list = curthread->td_sleeplocks;
	else {
		if (witness_skipspin)
			return;
		lock_list = PCPU_GET(spinlocks);
	}
	instance = find_instance(lock_list, lock);
	if (instance == NULL)
		kassert_panic("%s: lock (%s) %s not locked", __func__,
		    class->lc_name, lock->lo_name);
	lock->lo_witness->w_file = file;
	lock->lo_witness->w_line = line;
	if (instance == NULL)
		return;
	instance->li_file = file;
	instance->li_line = line;
}

void
witness_assert(const struct lock_object *lock, int flags, const char *file,
    int line)
{
#ifdef INVARIANT_SUPPORT
	struct lock_instance *instance;
	struct lock_class *class;

	if (lock->lo_witness == NULL || witness_watch < 1 || panicstr != NULL)
		return;
	class = LOCK_CLASS(lock);
	if ((class->lc_flags & LC_SLEEPLOCK) != 0)
		instance = find_instance(curthread->td_sleeplocks, lock);
	else if ((class->lc_flags & LC_SPINLOCK) != 0)
		instance = find_instance(PCPU_GET(spinlocks), lock);
	else {
		kassert_panic("Lock (%s) %s is not sleep or spin!",
		    class->lc_name, lock->lo_name);
		return;
	}
	switch (flags) {
	case LA_UNLOCKED:
		if (instance != NULL)
			kassert_panic("Lock (%s) %s locked @ %s:%d.",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		break;
	case LA_LOCKED:
	case LA_LOCKED | LA_RECURSED:
	case LA_LOCKED | LA_NOTRECURSED:
	case LA_SLOCKED:
	case LA_SLOCKED | LA_RECURSED:
	case LA_SLOCKED | LA_NOTRECURSED:
	case LA_XLOCKED:
	case LA_XLOCKED | LA_RECURSED:
	case LA_XLOCKED | LA_NOTRECURSED:
		if (instance == NULL) {
			kassert_panic("Lock (%s) %s not locked @ %s:%d.",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
			break;
		}
		if ((flags & LA_XLOCKED) != 0 &&
		    (instance->li_flags & LI_EXCLUSIVE) == 0)
			kassert_panic(
			    "Lock (%s) %s not exclusively locked @ %s:%d.",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		if ((flags & LA_SLOCKED) != 0 &&
		    (instance->li_flags & LI_EXCLUSIVE) != 0)
			kassert_panic(
			    "Lock (%s) %s exclusively locked @ %s:%d.",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		if ((flags & LA_RECURSED) != 0 &&
		    (instance->li_flags & LI_RECURSEMASK) == 0)
			kassert_panic("Lock (%s) %s not recursed @ %s:%d.",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		if ((flags & LA_NOTRECURSED) != 0 &&
		    (instance->li_flags & LI_RECURSEMASK) != 0)
			kassert_panic("Lock (%s) %s recursed @ %s:%d.",
			    class->lc_name, lock->lo_name,
			    fixup_filename(file), line);
		break;
	default:
		kassert_panic("Invalid lock assertion at %s:%d.",
		    fixup_filename(file), line);

	}
#endif	/* INVARIANT_SUPPORT */
}

static void
witness_setflag(struct lock_object *lock, int flag, int set)
{
	struct lock_list_entry *lock_list;
	struct lock_instance *instance;
	struct lock_class *class;

	if (lock->lo_witness == NULL || witness_watch == -1 || panicstr != NULL)
		return;
	class = LOCK_CLASS(lock);
	if (class->lc_flags & LC_SLEEPLOCK)
		lock_list = curthread->td_sleeplocks;
	else {
		if (witness_skipspin)
			return;
		lock_list = PCPU_GET(spinlocks);
	}
	instance = find_instance(lock_list, lock);
	if (instance == NULL) {
		kassert_panic("%s: lock (%s) %s not locked", __func__,
		    class->lc_name, lock->lo_name);
		return;
	}

	if (set)
		instance->li_flags |= flag;
	else
		instance->li_flags &= ~flag;
}

void
witness_norelease(struct lock_object *lock)
{

	witness_setflag(lock, LI_NORELEASE, 1);
}

void
witness_releaseok(struct lock_object *lock)
{

	witness_setflag(lock, LI_NORELEASE, 0);
}

#ifdef DDB
static void
witness_ddb_list(struct thread *td)
{

	KASSERT(witness_cold == 0, ("%s: witness_cold", __func__));
	KASSERT(kdb_active, ("%s: not in the debugger", __func__));

	if (witness_watch < 1)
		return;

	witness_list_locks(&td->td_sleeplocks, db_printf);

	/*
	 * We only handle spinlocks if td == curthread.  This is somewhat broken
	 * if td is currently executing on some other CPU and holds spin locks
	 * as we won't display those locks.  If we had a MI way of getting
	 * the per-cpu data for a given cpu then we could use
	 * td->td_oncpu to get the list of spinlocks for this thread
	 * and "fix" this.
	 *
	 * That still wouldn't really fix this unless we locked the scheduler
	 * lock or stopped the other CPU to make sure it wasn't changing the
	 * list out from under us.  It is probably best to just not try to
	 * handle threads on other CPU's for now.
	 */
	if (td == curthread && PCPU_GET(spinlocks) != NULL)
		witness_list_locks(PCPU_PTR(spinlocks), db_printf);
}

DB_SHOW_COMMAND(locks, db_witness_list)
{
	struct thread *td;

	if (have_addr)
		td = db_lookup_thread(addr, true);
	else
		td = kdb_thread;
	witness_ddb_list(td);
}

DB_SHOW_ALL_COMMAND(locks, db_witness_list_all)
{
	struct thread *td;
	struct proc *p;

	/*
	 * It would be nice to list only threads and processes that actually
	 * held sleep locks, but that information is currently not exported
	 * by WITNESS.
	 */
	FOREACH_PROC_IN_SYSTEM(p) {
		if (!witness_proc_has_locks(p))
			continue;
		FOREACH_THREAD_IN_PROC(p, td) {
			if (!witness_thread_has_locks(td))
				continue;
			db_printf("Process %d (%s) thread %p (%d)\n", p->p_pid,
			    p->p_comm, td, td->td_tid);
			witness_ddb_list(td);
			if (db_pager_quit)
				return;
		}
	}
}
DB_SHOW_ALIAS(alllocks, db_witness_list_all)

DB_SHOW_COMMAND(witness, db_witness_display)
{

	witness_ddb_display(db_printf);
}
#endif

static void
sbuf_print_witness_badstacks(struct sbuf *sb, size_t *oldidx)
{
	struct witness_lock_order_data *data1, *data2, *tmp_data1, *tmp_data2;
	struct witness *tmp_w1, *tmp_w2, *w1, *w2;
	int generation, i, j;

	tmp_data1 = NULL;
	tmp_data2 = NULL;
	tmp_w1 = NULL;
	tmp_w2 = NULL;

	/* Allocate and init temporary storage space. */
	tmp_w1 = malloc(sizeof(struct witness), M_TEMP, M_WAITOK | M_ZERO);
	tmp_w2 = malloc(sizeof(struct witness), M_TEMP, M_WAITOK | M_ZERO);
	tmp_data1 = malloc(sizeof(struct witness_lock_order_data), M_TEMP, 
	    M_WAITOK | M_ZERO);
	tmp_data2 = malloc(sizeof(struct witness_lock_order_data), M_TEMP, 
	    M_WAITOK | M_ZERO);
	stack_zero(&tmp_data1->wlod_stack);
	stack_zero(&tmp_data2->wlod_stack);

restart:
	mtx_lock_spin(&w_mtx);
	generation = w_generation;
	mtx_unlock_spin(&w_mtx);
	sbuf_printf(sb, "Number of known direct relationships is %d\n",
	    w_lohash.wloh_count);
	for (i = 1; i < w_max_used_index; i++) {
		mtx_lock_spin(&w_mtx);
		if (generation != w_generation) {
			mtx_unlock_spin(&w_mtx);

			/* The graph has changed, try again. */
			*oldidx = 0;
			sbuf_clear(sb);
			goto restart;
		}

		w1 = &w_data[i];
		if (w1->w_reversed == 0) {
			mtx_unlock_spin(&w_mtx);
			continue;
		}

		/* Copy w1 locally so we can release the spin lock. */
		*tmp_w1 = *w1;
		mtx_unlock_spin(&w_mtx);

		if (tmp_w1->w_reversed == 0)
			continue;
		for (j = 1; j < w_max_used_index; j++) {
			if ((w_rmatrix[i][j] & WITNESS_REVERSAL) == 0 || i > j)
				continue;

			mtx_lock_spin(&w_mtx);
			if (generation != w_generation) {
				mtx_unlock_spin(&w_mtx);

				/* The graph has changed, try again. */
				*oldidx = 0;
				sbuf_clear(sb);
				goto restart;
			}

			w2 = &w_data[j];
			data1 = witness_lock_order_get(w1, w2);
			data2 = witness_lock_order_get(w2, w1);

			/*
			 * Copy information locally so we can release the
			 * spin lock.
			 */
			*tmp_w2 = *w2;

			if (data1) {
				stack_zero(&tmp_data1->wlod_stack);
				stack_copy(&data1->wlod_stack,
				    &tmp_data1->wlod_stack);
			}
			if (data2 && data2 != data1) {
				stack_zero(&tmp_data2->wlod_stack);
				stack_copy(&data2->wlod_stack,
				    &tmp_data2->wlod_stack);
			}
			mtx_unlock_spin(&w_mtx);

			sbuf_printf(sb,
	    "\nLock order reversal between \"%s\"(%s) and \"%s\"(%s)!\n",
			    tmp_w1->w_name, tmp_w1->w_class->lc_name, 
			    tmp_w2->w_name, tmp_w2->w_class->lc_name);
			if (data1) {
				sbuf_printf(sb,
			"Lock order \"%s\"(%s) -> \"%s\"(%s) first seen at:\n",
				    tmp_w1->w_name, tmp_w1->w_class->lc_name, 
				    tmp_w2->w_name, tmp_w2->w_class->lc_name);
				stack_sbuf_print(sb, &tmp_data1->wlod_stack);
				sbuf_printf(sb, "\n");
			}
			if (data2 && data2 != data1) {
				sbuf_printf(sb,
			"Lock order \"%s\"(%s) -> \"%s\"(%s) first seen at:\n",
				    tmp_w2->w_name, tmp_w2->w_class->lc_name, 
				    tmp_w1->w_name, tmp_w1->w_class->lc_name);
				stack_sbuf_print(sb, &tmp_data2->wlod_stack);
				sbuf_printf(sb, "\n");
			}
		}
	}
	mtx_lock_spin(&w_mtx);
	if (generation != w_generation) {
		mtx_unlock_spin(&w_mtx);

		/*
		 * The graph changed while we were printing stack data,
		 * try again.
		 */
		*oldidx = 0;
		sbuf_clear(sb);
		goto restart;
	}
	mtx_unlock_spin(&w_mtx);

	/* Free temporary storage space. */
	free(tmp_data1, M_TEMP);
	free(tmp_data2, M_TEMP);
	free(tmp_w1, M_TEMP);
	free(tmp_w2, M_TEMP);
}

static int
sysctl_debug_witness_badstacks(SYSCTL_HANDLER_ARGS)
{
	struct sbuf *sb;
	int error;

	if (witness_watch < 1) {
		error = SYSCTL_OUT(req, w_notrunning, sizeof(w_notrunning));
		return (error);
	}
	if (witness_cold) {
		error = SYSCTL_OUT(req, w_stillcold, sizeof(w_stillcold));
		return (error);
	}
	error = 0;
	sb = sbuf_new(NULL, NULL, badstack_sbuf_size, SBUF_AUTOEXTEND);
	if (sb == NULL)
		return (ENOMEM);

	sbuf_print_witness_badstacks(sb, &req->oldidx);

	sbuf_finish(sb);
	error = SYSCTL_OUT(req, sbuf_data(sb), sbuf_len(sb) + 1);
	sbuf_delete(sb);

	return (error);
}

#ifdef DDB
static int
sbuf_db_printf_drain(void *arg __unused, const char *data, int len)
{

	return (db_printf("%.*s", len, data));
}

DB_SHOW_COMMAND(badstacks, db_witness_badstacks)
{
	struct sbuf sb;
	char buffer[128];
	size_t dummy;

	sbuf_new(&sb, buffer, sizeof(buffer), SBUF_FIXEDLEN);
	sbuf_set_drain(&sb, sbuf_db_printf_drain, NULL);
	sbuf_print_witness_badstacks(&sb, &dummy);
	sbuf_finish(&sb);
}
#endif

static int
sysctl_debug_witness_channel(SYSCTL_HANDLER_ARGS)
{
	static const struct {
		enum witness_channel channel;
		const char *name;
	} channels[] = {
		{ WITNESS_CONSOLE, "console" },
		{ WITNESS_LOG, "log" },
		{ WITNESS_NONE, "none" },
	};
	char buf[16];
	u_int i;
	int error;

	buf[0] = '\0';
	for (i = 0; i < nitems(channels); i++)
		if (witness_channel == channels[i].channel) {
			snprintf(buf, sizeof(buf), "%s", channels[i].name);
			break;
		}

	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	error = EINVAL;
	for (i = 0; i < nitems(channels); i++)
		if (strcmp(channels[i].name, buf) == 0) {
			witness_channel = channels[i].channel;
			error = 0;
			break;
		}
	return (error);
}

static int
sysctl_debug_witness_fullgraph(SYSCTL_HANDLER_ARGS)
{
	struct witness *w;
	struct sbuf *sb;
	int error;

#ifdef __i386__
	error = SYSCTL_OUT(req, w_notallowed, sizeof(w_notallowed));
	return (error);
#endif

	if (witness_watch < 1) {
		error = SYSCTL_OUT(req, w_notrunning, sizeof(w_notrunning));
		return (error);
	}
	if (witness_cold) {
		error = SYSCTL_OUT(req, w_stillcold, sizeof(w_stillcold));
		return (error);
	}
	error = 0;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sb = sbuf_new_for_sysctl(NULL, NULL, FULLGRAPH_SBUF_SIZE, req);
	if (sb == NULL)
		return (ENOMEM);
	sbuf_printf(sb, "\n");

	mtx_lock_spin(&w_mtx);
	STAILQ_FOREACH(w, &w_all, w_list)
		w->w_displayed = 0;
	STAILQ_FOREACH(w, &w_all, w_list)
		witness_add_fullgraph(sb, w);
	mtx_unlock_spin(&w_mtx);

	/*
	 * Close the sbuf and return to userland.
	 */
	error = sbuf_finish(sb);
	sbuf_delete(sb);

	return (error);
}

static int
sysctl_debug_witness_watch(SYSCTL_HANDLER_ARGS)
{
	int error, value;

	value = witness_watch;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value > 1 || value < -1 ||
	    (witness_watch == -1 && value != witness_watch))
		return (EINVAL);
	witness_watch = value;
	return (0);
}

static void
witness_add_fullgraph(struct sbuf *sb, struct witness *w)
{
	int i;

	if (w->w_displayed != 0 || (w->w_file == NULL && w->w_line == 0))
		return;
	w->w_displayed = 1;

	WITNESS_INDEX_ASSERT(w->w_index);
	for (i = 1; i <= w_max_used_index; i++) {
		if (w_rmatrix[w->w_index][i] & WITNESS_PARENT) {
			sbuf_printf(sb, "\"%s\",\"%s\"\n", w->w_name,
			    w_data[i].w_name);
			witness_add_fullgraph(sb, &w_data[i]);
		}
	}
}

/*
 * A simple hash function. Takes a key pointer and a key size. If size == 0,
 * interprets the key as a string and reads until the null
 * terminator. Otherwise, reads the first size bytes. Returns an unsigned 32-bit
 * hash value computed from the key.
 */
static uint32_t
witness_hash_djb2(const uint8_t *key, uint32_t size)
{
	unsigned int hash = 5381;
	int i;

	/* hash = hash * 33 + key[i] */
	if (size)
		for (i = 0; i < size; i++)
			hash = ((hash << 5) + hash) + (unsigned int)key[i];
	else
		for (i = 0; key[i] != 0; i++)
			hash = ((hash << 5) + hash) + (unsigned int)key[i];

	return (hash);
}


/*
 * Initializes the two witness hash tables. Called exactly once from
 * witness_initialize().
 */
static void
witness_init_hash_tables(void)
{
	int i;

	MPASS(witness_cold);

	/* Initialize the hash tables. */
	for (i = 0; i < WITNESS_HASH_SIZE; i++)
		w_hash.wh_array[i] = NULL;

	w_hash.wh_size = WITNESS_HASH_SIZE;
	w_hash.wh_count = 0;

	/* Initialize the lock order data hash. */
	w_lofree = NULL;
	for (i = 0; i < WITNESS_LO_DATA_COUNT; i++) {
		memset(&w_lodata[i], 0, sizeof(w_lodata[i]));
		w_lodata[i].wlod_next = w_lofree;
		w_lofree = &w_lodata[i];
	}
	w_lohash.wloh_size = WITNESS_LO_HASH_SIZE;
	w_lohash.wloh_count = 0;
	for (i = 0; i < WITNESS_LO_HASH_SIZE; i++)
		w_lohash.wloh_array[i] = NULL;
}

static struct witness *
witness_hash_get(const char *key)
{
	struct witness *w;
	uint32_t hash;
	
	MPASS(key != NULL);
	if (witness_cold == 0)
		mtx_assert(&w_mtx, MA_OWNED);
	hash = witness_hash_djb2(key, 0) % w_hash.wh_size;
	w = w_hash.wh_array[hash];
	while (w != NULL) {
		if (strcmp(w->w_name, key) == 0)
			goto out;
		w = w->w_hash_next;
	}

out:
	return (w);
}

static void
witness_hash_put(struct witness *w)
{
	uint32_t hash;

	MPASS(w != NULL);
	MPASS(w->w_name != NULL);
	if (witness_cold == 0)
		mtx_assert(&w_mtx, MA_OWNED);
	KASSERT(witness_hash_get(w->w_name) == NULL,
	    ("%s: trying to add a hash entry that already exists!", __func__));
	KASSERT(w->w_hash_next == NULL,
	    ("%s: w->w_hash_next != NULL", __func__));

	hash = witness_hash_djb2(w->w_name, 0) % w_hash.wh_size;
	w->w_hash_next = w_hash.wh_array[hash];
	w_hash.wh_array[hash] = w;
	w_hash.wh_count++;
}


static struct witness_lock_order_data *
witness_lock_order_get(struct witness *parent, struct witness *child)
{
	struct witness_lock_order_data *data = NULL;
	struct witness_lock_order_key key;
	unsigned int hash;

	MPASS(parent != NULL && child != NULL);
	key.from = parent->w_index;
	key.to = child->w_index;
	WITNESS_INDEX_ASSERT(key.from);
	WITNESS_INDEX_ASSERT(key.to);
	if ((w_rmatrix[parent->w_index][child->w_index]
	    & WITNESS_LOCK_ORDER_KNOWN) == 0)
		goto out;

	hash = witness_hash_djb2((const char*)&key,
	    sizeof(key)) % w_lohash.wloh_size;
	data = w_lohash.wloh_array[hash];
	while (data != NULL) {
		if (witness_lock_order_key_equal(&data->wlod_key, &key))
			break;
		data = data->wlod_next;
	}

out:
	return (data);
}

/*
 * Verify that parent and child have a known relationship, are not the same,
 * and child is actually a child of parent.  This is done without w_mtx
 * to avoid contention in the common case.
 */
static int
witness_lock_order_check(struct witness *parent, struct witness *child)
{

	if (parent != child &&
	    w_rmatrix[parent->w_index][child->w_index]
	    & WITNESS_LOCK_ORDER_KNOWN &&
	    isitmychild(parent, child))
		return (1);

	return (0);
}

static int
witness_lock_order_add(struct witness *parent, struct witness *child)
{
	struct witness_lock_order_data *data = NULL;
	struct witness_lock_order_key key;
	unsigned int hash;
	
	MPASS(parent != NULL && child != NULL);
	key.from = parent->w_index;
	key.to = child->w_index;
	WITNESS_INDEX_ASSERT(key.from);
	WITNESS_INDEX_ASSERT(key.to);
	if (w_rmatrix[parent->w_index][child->w_index]
	    & WITNESS_LOCK_ORDER_KNOWN)
		return (1);

	hash = witness_hash_djb2((const char*)&key,
	    sizeof(key)) % w_lohash.wloh_size;
	w_rmatrix[parent->w_index][child->w_index] |= WITNESS_LOCK_ORDER_KNOWN;
	data = w_lofree;
	if (data == NULL)
		return (0);
	w_lofree = data->wlod_next;
	data->wlod_next = w_lohash.wloh_array[hash];
	data->wlod_key = key;
	w_lohash.wloh_array[hash] = data;
	w_lohash.wloh_count++;
	stack_zero(&data->wlod_stack);
	stack_save(&data->wlod_stack);
	return (1);
}

/* Call this whenever the structure of the witness graph changes. */
static void
witness_increment_graph_generation(void)
{

	if (witness_cold == 0)
		mtx_assert(&w_mtx, MA_OWNED);
	w_generation++;
}

static int
witness_output_drain(void *arg __unused, const char *data, int len)
{

	witness_output("%.*s", len, data);
	return (len);
}

static void
witness_debugger(int cond, const char *msg)
{
	char buf[32];
	struct sbuf sb;
	struct stack st;

	if (!cond)
		return;

	if (witness_trace) {
		sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN);
		sbuf_set_drain(&sb, witness_output_drain, NULL);

		stack_zero(&st);
		stack_save(&st);
		witness_output("stack backtrace:\n");
		stack_sbuf_print_ddb(&sb, &st);

		sbuf_finish(&sb);
	}

#ifdef KDB
	if (witness_kdb)
		kdb_enter(KDB_WHY_WITNESS, msg);
#endif
}
