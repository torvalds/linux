/*	$OpenBSD: uvmexp.h,v 1.19 2025/03/10 19:52:57 miod Exp $	*/

#ifndef	_UVM_UVMEXP_
#define	_UVM_UVMEXP_

/*
 * CTL_VM identifiers
 */
#define	VM_METER	1		/* struct vmmeter */
#define	VM_LOADAVG	2		/* struct loadavg */
#define	VM_PSSTRINGS	3		/* PSSTRINGS */
#define VM_UVMEXP	4		/* struct uvmexp */
#define VM_SWAPENCRYPT	5		/* int */
#define VM_NKMEMPAGES	6		/* int - # kmem_map pages */
#define	VM_ANONMIN	7
#define	VM_VTEXTMIN	8
#define	VM_VNODEMIN	9
#define	VM_MAXSLP	10
#define	VM_USPACE	11
#define	VM_MALLOC_CONF	12		/* config for userland malloc */
#define	VM_MAXID	13		/* number of valid vm ids */

#define	CTL_VM_NAMES { \
	{ 0, 0 }, \
	{ "vmmeter", CTLTYPE_STRUCT }, \
	{ "loadavg", CTLTYPE_STRUCT }, \
	{ "psstrings", CTLTYPE_STRUCT }, \
	{ "uvmexp", CTLTYPE_STRUCT }, \
	{ "swapencrypt", CTLTYPE_NODE }, \
	{ "nkmempages", CTLTYPE_INT }, \
	{ "anonmin", CTLTYPE_INT }, \
	{ "vtextmin", CTLTYPE_INT }, \
	{ "vnodemin", CTLTYPE_INT }, \
	{ "maxslp", CTLTYPE_INT }, \
	{ "uspace", CTLTYPE_INT }, \
	{ "malloc_conf", CTLTYPE_STRING }, \
}

/*
 * uvmexp: global data structures that are exported to parts of the kernel
 * other than the vm system.
 *
 *  Locks used to protect struct members in this file:
 *	a	atomic operations
 *	I	immutable after creation
 *	K	kernel lock
 *	F	uvm_lock_fpageq
 *	L	uvm_lock_pageq
 *	S	uvm_swap_data_lock
 *	p	copy of per-CPU counters, used only by userland.
 */
struct uvmexp {
	/* vm_page constants */
	int pagesize;   /* size of a page (PAGE_SIZE): must be power of 2 */
	int pagemask;   /* page mask */
	int pageshift;  /* page shift */

	/* vm_page counters */
	int npages;     /* [I] number of pages we manage */
	int free;       /* [F] number of free pages */
	int active;     /* [L] # of active pages */
	int inactive;   /* [L] # of pages that we free'd but may want back */
	int paging;	/* [a] # of pages in the process of being paged out */
	int wired;      /* [a] number of wired pages */

	int zeropages;		/* [F] number of zero'd pages */
	int reserve_pagedaemon; /* [I] # of pages reserved for pagedaemon */
	int reserve_kernel;	/* [I] # of pages reserved for kernel */
	int percpucaches;	/* [a] # of pages in per-CPU caches */
	int vnodepages;		/* XXX # of pages used by vnode page cache */
	int vtextpages;		/* XXX # of pages used by vtext vnodes */

	/* pageout params */
	int freemin;    /* [I] min number of free pages */
	int freetarg;   /* [I] target number of free pages */
	int inactarg;   /* target number of inactive pages */
	int wiredmax;   /* [I] max number of wired pages */
	int anonmin;	/* min threshold for anon pages */
	int vtextmin;	/* min threshold for vtext pages */
	int vnodemin;	/* min threshold for vnode pages */
	int anonminpct;	/* min percent anon pages */
	int vtextminpct;/* min percent vtext pages */
	int vnodeminpct;/* min percent vnode pages */

	/* swap */
	int nswapdev;	/* [S] number of configured swap devices in system */
	int swpages;	/* [S] number of PAGE_SIZE'ed swap pages */
	int swpginuse;	/* [S] number of swap pages in use */
	int swpgonly;	/* [a] number of swap pages in use, not also in RAM */
	int nswget;	/* [a] number of swap pages moved from disk to RAM */
	int nanon;	/* XXX number total of anon's in system */
	int unused05;	/* formerly nanonneeded */
	int unused06;	/* formerly nfreeanon */

	/* stat counters */
	int faults;		/* [p] page fault count */
	int traps;		/* trap count */
	int intrs;		/* interrupt count */
	int swtch;		/* context switch count */
	int softs;		/* software interrupt count */
	int syscalls;		/* system calls */
	int pageins;		/* [p] pagein operation count */
				/* pageouts are in pdpageouts below */
	int pcphit;		/* [a] # of pagealloc from per-CPU cache */
	int pcpmiss;		/* [a] # of times a per-CPU cache was empty */
	int pgswapin;		/* pages swapped in */
	int pgswapout;		/* pages swapped out */
	int forks;  		/* forks */
	int forks_ppwait;	/* forks where parent waits */
	int forks_sharevm;	/* forks where vmspace is shared */
	int pga_zerohit;	/* pagealloc where zero wanted and zero
				   was available */
	int pga_zeromiss;	/* pagealloc where zero wanted and zero
				   not available */
	int unused09;		/* formerly zeroaborts */

	/* fault subcounters */
	int fltnoram;	/* [p] # of times fault was out of ram */
	int fltnoanon;	/* [p] # of times fault was out of anons */
	int fltnoamap;	/* [p] # of times fault was out of amap chunks */
	int fltpgwait;	/* [p] # of times fault had to wait on a page */
	int fltpgrele;	/* [p] # of times fault found a released page */
	int fltrelck;	/* [p] # of times fault relock is a success */
	int fltnorelck;	/* [p] # of times fault relock failed */
	int fltanget;	/* [p] # of times fault gets anon page */
	int fltanretry;	/* [p] # of times fault retrys an anon get */
	int fltamcopy;	/* [p] # of times fault clears "needs copy" */
	int fltnamap;	/* [p] # of times fault maps a neighbor anon page */
	int fltnomap;	/* [p] # of times fault maps a neighbor obj page */
	int fltlget;	/* [p] # of times fault does a locked pgo_get */
	int fltget;	/* [p] # of times fault does an unlocked get */
	int flt_anon;	/* [p] # of times fault anon (case 1a) */
	int flt_acow;	/* [p] # of times fault anon cow (case 1b) */
	int flt_obj;	/* [p] # of times fault is on object page (2a) */
	int flt_prcopy;	/* [p] # of times fault promotes with copy (2b) */
	int flt_przero;	/* [p] # of times fault promotes with zerofill (2b) */
	int fltup;	/* [p] # of times fault upgrade is a success */
	int fltnoup;	/* [p] # of times fault upgrade failed */

	/* daemon counters */
	int pdwoke;	/* [F] # of times daemon woke up */
	int pdrevs;	/* number of times daemon scanned for free pages */
	int pdswout;	/* number of times daemon called for swapout */
	int pdfreed;	/* number of pages daemon freed since boot */
	int pdscans;	/* number of pages daemon scanned since boot */
	int pdanscan;	/* number of anonymous pages scanned by daemon */
	int pdobscan;	/* number of object pages scanned by daemon */
	int pdreact;	/* number of pages daemon reactivated since boot */
	int pdbusy;	/* number of times daemon found a busy page */
	int pdpageouts;	/* number of times daemon started a pageout */
	int pdpending;	/* number of times daemon got a pending pagout */
	int pddeact;	/* number of pages daemon deactivates */

	int unused13;	/* formerly pdrevtext */

	int fpswtch;	/* FPU context switches */
	int kmapent;	/* number of kernel map entries */
};

struct _ps_strings {
	void	*val;
};

#ifdef _KERNEL

/*
 * Per-cpu UVM counters.
 */
extern struct cpumem *uvmexp_counters;

enum uvm_exp_counters {
	/* stat counters */
	faults,		/* page fault count */
	pageins,	/* pagein operation count */

	/* fault subcounters */
	flt_noram,	/* number of times fault was out of ram */
	flt_noanon,	/* number of times fault was out of anons */
	flt_noamap,	/* number of times fault was out of amap chunks */
	flt_pgwait,	/* number of times fault had to wait on a page */
	flt_pgrele,	/* number of times fault found a released page */
	flt_relck,	/* number of times fault relock is a success */
	flt_norelck,	/* number of times fault relock failed */
	flt_anget,	/* number of times fault gets anon page */
	flt_anretry,	/* number of times fault retrys an anon get */
	flt_amcopy,	/* number of times fault clears "needs copy" */
	flt_namap,	/* number of times fault maps a neighbor anon page */
	flt_nomap,	/* number of times fault maps a neighbor obj page */
	flt_lget,	/* number of times fault does a locked pgo_get */
	flt_get,	/* number of times fault does an unlocked get */
	flt_anon,	/* number of times fault anon (case 1a) */
	flt_acow,	/* number of times fault anon cow (case 1b) */
	flt_obj,	/* number of times fault is on object page (2a) */
	flt_prcopy,	/* number of times fault promotes with copy (2b) */
	flt_przero,	/* number of times fault promotes with zerofill (2b) */
	flt_up,		/* number of times fault upgrade is a success */
	flt_noup,	/* number of times fault upgrade failed */

	exp_ncounters
};

#endif /* _KERNEL */
#endif /*_UVM_UVMEXP_ */
