/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 */

#ifndef _SYS_SMP_H_
#define _SYS_SMP_H_

#ifdef _KERNEL

#ifndef LOCORE

#include <sys/cpuset.h>
#include <sys/queue.h>

/*
 * Types of nodes in the topological tree.
 */
typedef enum {
	/* No node has this type; can be used in topo API calls. */
	TOPO_TYPE_DUMMY,
	/* Processing unit aka computing unit aka logical CPU. */
	TOPO_TYPE_PU,
	/* Physical subdivision of a package. */
	TOPO_TYPE_CORE,
	/* CPU L1/L2/L3 cache. */
	TOPO_TYPE_CACHE,
	/* Package aka chip, equivalent to socket. */
	TOPO_TYPE_PKG,
	/* NUMA node. */
	TOPO_TYPE_NODE,
	/* Other logical or physical grouping of PUs. */
	/* E.g. PUs on the same dye, or PUs sharing an FPU. */
	TOPO_TYPE_GROUP,
	/* The whole system. */
	TOPO_TYPE_SYSTEM
} topo_node_type;

/* Hardware indenitifier of a topology component. */
typedef	unsigned int hwid_t;
/* Logical CPU idenitifier. */
typedef	int cpuid_t;

/* A node in the topology. */
struct topo_node {
	struct topo_node			*parent;
	TAILQ_HEAD(topo_children, topo_node)	children;
	TAILQ_ENTRY(topo_node)			siblings;
	cpuset_t				cpuset;
	topo_node_type				type;
	uintptr_t				subtype;
	hwid_t					hwid;
	cpuid_t					id;
	int					nchildren;
	int					cpu_count;
};

/*
 * Scheduling topology of a NUMA or SMP system.
 *
 * The top level topology is an array of pointers to groups.  Each group
 * contains a bitmask of cpus in its group or subgroups.  It may also
 * contain a pointer to an array of child groups.
 *
 * The bitmasks at non leaf groups may be used by consumers who support
 * a smaller depth than the hardware provides.
 *
 * The topology may be omitted by systems where all CPUs are equal.
 */

struct cpu_group {
	struct cpu_group *cg_parent;	/* Our parent group. */
	struct cpu_group *cg_child;	/* Optional children groups. */
	cpuset_t	cg_mask;	/* Mask of cpus in this group. */
	int32_t		cg_count;	/* Count of cpus in this group. */
	int16_t		cg_children;	/* Number of children groups. */
	int8_t		cg_level;	/* Shared cache level. */
	int8_t		cg_flags;	/* Traversal modifiers. */
};

typedef struct cpu_group *cpu_group_t;

/*
 * Defines common resources for CPUs in the group.  The highest level
 * resource should be used when multiple are shared.
 */
#define	CG_SHARE_NONE	0
#define	CG_SHARE_L1	1
#define	CG_SHARE_L2	2
#define	CG_SHARE_L3	3

#define MAX_CACHE_LEVELS	CG_SHARE_L3

/*
 * Behavior modifiers for load balancing and affinity.
 */
#define	CG_FLAG_HTT	0x01		/* Schedule the alternate core last. */
#define	CG_FLAG_SMT	0x02		/* New age htt, less crippled. */
#define	CG_FLAG_THREAD	(CG_FLAG_HTT | CG_FLAG_SMT)	/* Any threading. */

/*
 * Convenience routines for building and traversing topologies.
 */
#ifdef SMP
void topo_init_node(struct topo_node *node);
void topo_init_root(struct topo_node *root);
struct topo_node * topo_add_node_by_hwid(struct topo_node *parent, int hwid,
    topo_node_type type, uintptr_t subtype);
struct topo_node * topo_find_node_by_hwid(struct topo_node *parent, int hwid,
    topo_node_type type, uintptr_t subtype);
void topo_promote_child(struct topo_node *child);
struct topo_node * topo_next_node(struct topo_node *top,
    struct topo_node *node);
struct topo_node * topo_next_nonchild_node(struct topo_node *top,
    struct topo_node *node);
void topo_set_pu_id(struct topo_node *node, cpuid_t id);

enum topo_level {
	TOPO_LEVEL_PKG = 0,
	/*
	 * Some systems have useful sub-package core organizations.  On these,
	 * a package has one or more subgroups.  Each subgroup contains one or
	 * more cache groups (cores that share a last level cache).
	 */
	TOPO_LEVEL_GROUP,
	TOPO_LEVEL_CACHEGROUP,
	TOPO_LEVEL_CORE,
	TOPO_LEVEL_THREAD,
	TOPO_LEVEL_COUNT	/* Must be last */
};
struct topo_analysis {
	int entities[TOPO_LEVEL_COUNT];
};
int topo_analyze(struct topo_node *topo_root, int all,
    struct topo_analysis *results);

#define	TOPO_FOREACH(i, root)	\
	for (i = root; i != NULL; i = topo_next_node(root, i))

struct cpu_group *smp_topo(void);
struct cpu_group *smp_topo_alloc(u_int count);
struct cpu_group *smp_topo_none(void);
struct cpu_group *smp_topo_1level(int l1share, int l1count, int l1flags);
struct cpu_group *smp_topo_2level(int l2share, int l2count, int l1share,
    int l1count, int l1flags);
struct cpu_group *smp_topo_find(struct cpu_group *top, int cpu);

extern void (*cpustop_restartfunc)(void);
extern int smp_cpus;
/* The suspend/resume cpusets are x86 only, but minimize ifdefs. */
extern volatile cpuset_t resuming_cpus;	/* woken up cpus in suspend pen */
extern volatile cpuset_t started_cpus;	/* cpus to let out of stop pen */
extern volatile cpuset_t stopped_cpus;	/* cpus in stop pen */
extern volatile cpuset_t suspended_cpus; /* cpus [near] sleeping in susp pen */
extern volatile cpuset_t toresume_cpus;	/* cpus to let out of suspend pen */
extern cpuset_t hlt_cpus_mask;		/* XXX 'mask' is detail in old impl */
extern cpuset_t logical_cpus_mask;
#endif /* SMP */

extern u_int mp_maxid;
extern int mp_maxcpus;
extern int mp_ncores;
extern int mp_ncpus;
extern volatile int smp_started;
extern int smp_threads_per_core;

extern cpuset_t all_cpus;
extern cpuset_t cpuset_domain[MAXMEMDOM]; 	/* CPUs in each NUMA domain. */

/*
 * Macro allowing us to determine whether a CPU is absent at any given
 * time, thus permitting us to configure sparse maps of cpuid-dependent
 * (per-CPU) structures.
 */
#define	CPU_ABSENT(x_cpu)	(!CPU_ISSET(x_cpu, &all_cpus))

/*
 * Macros to iterate over non-absent CPUs.  CPU_FOREACH() takes an
 * integer iterator and iterates over the available set of CPUs.
 * CPU_FIRST() returns the id of the first non-absent CPU.  CPU_NEXT()
 * returns the id of the next non-absent CPU.  It will wrap back to
 * CPU_FIRST() once the end of the list is reached.  The iterators are
 * currently implemented via inline functions.
 */
#define	CPU_FOREACH(i)							\
	for ((i) = 0; (i) <= mp_maxid; (i)++)				\
		if (!CPU_ABSENT((i)))

static __inline int
cpu_first(void)
{
	int i;

	for (i = 0;; i++)
		if (!CPU_ABSENT(i))
			return (i);
}

static __inline int
cpu_next(int i)
{

	for (;;) {
		i++;
		if (i > mp_maxid)
			i = 0;
		if (!CPU_ABSENT(i))
			return (i);
	}
}

#define	CPU_FIRST()	cpu_first()
#define	CPU_NEXT(i)	cpu_next((i))

#ifdef SMP
/*
 * Machine dependent functions used to initialize MP support.
 *
 * The cpu_mp_probe() should check to see if MP support is present and return
 * zero if it is not or non-zero if it is.  If MP support is present, then
 * cpu_mp_start() will be called so that MP can be enabled.  This function
 * should do things such as startup secondary processors.  It should also
 * setup mp_ncpus, all_cpus, and smp_cpus.  It should also ensure that
 * smp_started is initialized at the appropriate time.
 * Once cpu_mp_start() returns, machine independent MP startup code will be
 * executed and a simple message will be output to the console.  Finally,
 * cpu_mp_announce() will be called so that machine dependent messages about
 * the MP support may be output to the console if desired.
 *
 * The cpu_setmaxid() function is called very early during the boot process
 * so that the MD code may set mp_maxid to provide an upper bound on CPU IDs
 * that other subsystems may use.  If a platform is not able to determine
 * the exact maximum ID that early, then it may set mp_maxid to MAXCPU - 1.
 */
struct thread;

struct cpu_group *cpu_topo(void);
void	cpu_mp_announce(void);
int	cpu_mp_probe(void);
void	cpu_mp_setmaxid(void);
void	cpu_mp_start(void);

void	forward_signal(struct thread *);
int	restart_cpus(cpuset_t);
int	stop_cpus(cpuset_t);
int	stop_cpus_hard(cpuset_t);
#if defined(__amd64__) || defined(__i386__)
int	suspend_cpus(cpuset_t);
int	resume_cpus(cpuset_t);
#endif

void	smp_rendezvous_action(void);
extern	struct mtx smp_ipi_mtx;

#endif /* SMP */

int	quiesce_all_cpus(const char *, int);
int	quiesce_cpus(cpuset_t, const char *, int);
void	smp_no_rendezvous_barrier(void *);
void	smp_rendezvous(void (*)(void *), 
		       void (*)(void *),
		       void (*)(void *),
		       void *arg);
void	smp_rendezvous_cpus(cpuset_t,
		       void (*)(void *), 
		       void (*)(void *),
		       void (*)(void *),
		       void *arg);
#endif /* !LOCORE */
#endif /* _KERNEL */
#endif /* _SYS_SMP_H_ */
