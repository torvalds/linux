/*	$OpenBSD: dtvar.h,v 1.24 2025/09/22 07:49:43 sashan Exp $ */

/*
 * Copyright (c) 2019 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DT_H_
#define _DT_H_

#include <sys/ioccom.h>
#include <sys/stacktrace.h>
#include <sys/time.h>
#include <sys/syslimits.h>

/*
 * Length of provider/probe/function names, including NUL.
 */
#define DTNAMESIZE	16

/*
 * Length of process name, including NUL.
 */
#define DTMAXCOMLEN	_MAXCOMLEN

/*
 * Maximum number of arguments passed to a function.
 */
#define DTMAXFUNCARGS	10
#define DTMAXARGTYPES	5

/*
 * Event state: where to store information when a probe fires.
 */
struct dt_evt {
	unsigned int		dtev_pbn;	/* Probe number */
	unsigned int		dtev_cpu;	/* CPU id */
	pid_t			dtev_pid;	/* ID of current process */
	pid_t			dtev_tid;	/* ID of current thread */
	struct timespec		dtev_tsp;	/* timestamp (nsecs) */

	/*
	 * Recorded if the corresponding flag is set.
	 */
	struct stacktrace	dtev_kstack;	/* kernel stack frame */
	struct stacktrace	dtev_ustack;	/* userland stack frame */
	char			dtev_comm[DTMAXCOMLEN]; /* current pr. name */
	union {
		register_t		E_entry[DTMAXFUNCARGS];
		struct {
			register_t		__retval[2];
			int			__error;
		} E_return;
	} _args;
#define dtev_args	_args.E_entry		/* function args. */
#define dtev_retval	_args.E_return.__retval	/* function retval */
#define dtev_error	_args.E_return.__error	/* function error */
};

/*
 * States to record when a probe fires.
 */
#define DTEVT_EXECNAME	(1 << 0)		/* current process name */
#define DTEVT_USTACK	(1 << 1)		/* userland stack */
#define DTEVT_KSTACK	(1 << 2)		/* kernel stack */
#define DTEVT_FUNCARGS	(1 << 3)		/* function arguments */

#define	DTEVT_FLAG_BITS		\
	"\020"			\
	"\001EXECNAME"		\
	"\002USTACK"		\
	"\003KSTACK"		\
	"\004FUNCARGS"		\

struct dtioc_probe_info {
	uint32_t	dtpi_pbn;		/* probe number */
	uint8_t		dtpi_nargs;		/* # of arguments */
	char		dtpi_prov[DTNAMESIZE];
	char		dtpi_func[DTNAMESIZE];
	char		dtpi_name[DTNAMESIZE];
};

struct dtioc_probe {
	size_t			 dtpr_size;	/* size of the buffer */
	struct dtioc_probe_info	*dtpr_probes;	/* array of probe info */
};

struct dtioc_arg_info {
	uint32_t	dtai_pbn;		/* probe number */
	uint8_t		dtai_argn;		/* arguments number */
	char		dtai_argtype[DTNAMESIZE];
};

struct dtioc_arg {
	uint32_t		 dtar_pbn;	/* probe number */
	size_t			 dtar_size;	/* size of the buffer */
	struct dtioc_arg_info	*dtar_args;	/* array of arg info */
};

struct dtioc_req {
	uint32_t		 dtrq_pbn;	/* probe number */
	uint32_t		 __unused1;
	uint64_t		 dtrq_evtflags;	/* states to record */
	uint64_t		 dtrq_nsecs;	/* execution period */
};

struct dtioc_stat {
	uint64_t		 dtst_readevt;	/* events read */
	uint64_t		 dtst_dropevt;	/* events dropped */
	uint64_t		 dtst_skiptick;	/* clock ticks skipped */
	uint64_t		 dtst_recurevt;	/* recursive events */
};

struct dtioc_rdvn {
	pid_t			 dtrv_pid;	/* process to inspect */
	int			 dtrv_fd;	/* where to dump data */
	caddr_t			 dtrv_va;
				    /* programm counter in inspected process */
	caddr_t			 dtrv_offset;
				    /* comes from vm_map_entry::offset */
	caddr_t			 dtrv_start;	/* end address for section */
	size_t			 dtrv_len;	/* the length of ELF file */
};

#define DTIOCGPLIST	_IOWR('D', 1, struct dtioc_probe)
#define DTIOCGSTATS	_IOR('D', 2, struct dtioc_stat)
#define DTIOCRECORD	_IOW('D', 3, int)
#define DTIOCPRBENABLE	_IOW('D', 4, struct dtioc_req)
#define DTIOCPRBDISABLE	_IOW('D', 5, struct dtioc_req)
#define DTIOCGARGS	_IOWR('D', 6, struct dtioc_arg)
/* _IOWR('D', 7, struct dtioc_getaux)  was DTIOCGETAUXBASE */
#define DTIOCRDVNODE	_IOWR('D', 8, struct dtioc_rdvn)

#ifdef _KERNEL

#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/smr.h>

/* Flags that make sense for all providers. */
#define DTEVT_COMMON	(DTEVT_EXECNAME|DTEVT_KSTACK|DTEVT_USTACK)

#define M_DT M_DEVBUF /* XXX FIXME */

struct dt_softc;

/*
 * Probe control block, possibly per-CPU.
 *
 * At least a PCB is allocated for each probe enabled via the DTIOCPRBENABLE
 * ioctl(2).  It will hold the events written when the probe fires until
 * userland read(2)s them.
 *
 *  Locks used to protect struct members in this file:
 *	D	dt_lock
 *	I	immutable after creation
 *	K	kernel lock
 *	K,S	kernel lock for writing and SMR for reading
 *	m	per-pcb mutex
 *	c	owned (read & modified) by a single CPU
 */
struct dt_pcb {
	SMR_SLIST_ENTRY(dt_pcb)	 dp_pnext;	/* [K,S] next PCB per probe */
	TAILQ_ENTRY(dt_pcb)	 dp_snext;	/* [K] next PCB per softc */

	struct dt_softc		*dp_sc;		/* [I] related softc */
	struct dt_probe		*dp_dtp;	/* [I] related probe */
	uint64_t		 dp_evtflags;	/* [I] event states to record */

	/* Provider specific fields. */
	struct clockintr	 dp_clockintr;	/* [D] profiling handle */
	uint64_t		 dp_nsecs;	/* [I] profiling period */
	struct cpu_info		*dp_cpu;	/* [I] on which CPU */
};

TAILQ_HEAD(dt_pcb_list, dt_pcb);

struct dt_pcb	*dt_pcb_alloc(struct dt_probe *, struct dt_softc *);
void		 dt_pcb_free(struct dt_pcb *);
void		 dt_pcb_purge(struct dt_pcb_list *);

void		 dt_pcb_ring_skiptick(struct dt_pcb *, unsigned int);
struct dt_evt	*dt_pcb_ring_get(struct dt_pcb *, int);
void		 dt_pcb_ring_consume(struct dt_pcb *, struct dt_evt *);

/*
 * Probes are entry points in the system where events can be recorded.
 *
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	K	kernel lock
 *	D	dt_lock
 *	D,S	dt_lock for writing and SMR for reading
 *	M	dtp mutex
 */
struct dt_probe {
	SIMPLEQ_ENTRY(dt_probe)	 dtp_next;	/* [K] global list of probes */
	SMR_SLIST_HEAD(, dt_pcb) dtp_pcbs;	/* [D,S] list of enabled PCBs */
	struct dt_provider	*dtp_prov;	/* [I] its to provider */
	const char		*dtp_func;	/* [I] probe function */
	const char		*dtp_name;	/* [I] probe name */
	uint32_t		 dtp_pbn;	/* [I] unique ID */
	volatile uint32_t	 dtp_recording;	/* [d] is it recording? */
	unsigned		 dtp_ref;	/* [m] # of PCBs referencing the probe */

	/* Provider specific fields. */
	int			 dtp_sysnum;	/* [I] related # of syscall */
	const char		*dtp_argtype[DTMAXARGTYPES];
						/* [I] type of arguments */
	int			 dtp_nargs;	/* [I] # of arguments */
#ifdef DDBPROF
	int			 dtp_type;	/* [I] 'entry' or 'return' */
	vaddr_t			 dtp_addr;	/* [I] address of breakpoint */
	SLIST_ENTRY(dt_probe)	 dtp_knext;	/* [K] list of ELF kprobe */
#endif
};


/*
 * Providers expose a set of probes and a method to record events.
 */
struct dt_provider {
	const char		*dtpv_name;	/* [I] provider name */
	volatile uint32_t	 dtpv_recording;/* [D] # of recording PCBs */

	int		(*dtpv_alloc)(struct dt_probe *, struct dt_softc *,
			    struct dt_pcb_list *, struct dtioc_req *);
	int		(*dtpv_enter)(struct dt_provider *, ...);
	void		(*dtpv_leave)(struct dt_provider *, ...);
	int		(*dtpv_dealloc)(struct dt_probe *, struct dt_softc *,
			    struct dtioc_req *);
};

extern struct dt_provider dt_prov_kprobe;

int		 dt_prov_profile_init(void);
int		 dt_prov_syscall_init(void);
int		 dt_prov_static_init(void);
int		 dt_prov_kprobe_init(void);

struct dt_probe *dt_dev_alloc_probe(const char *, const char *,
		    struct dt_provider *);
void		 dt_dev_register_probe(struct dt_probe *);

void		 dt_clock(struct clockrequest *, void *, void *);

extern volatile uint32_t	dt_tracing;	/* currently tracing? */

#define DT_ENTER(provname, args...) do {				\
	extern struct dt_provider dt_prov_ ## provname ;		\
	struct dt_provider *dtpv = &dt_prov_ ## provname ;		\
									\
	if (__predict_false(dt_tracing) &&				\
	    __predict_false(dtpv->dtpv_recording)) {			\
		dtpv->dtpv_enter(dtpv, args);				\
	}								\
} while (0)

#define DT_LEAVE(provname, args...) do {				\
	extern struct dt_provider dt_prov_ ## provname ;		\
	struct dt_provider *dtpv = &dt_prov_ ## provname ;		\
									\
	if (__predict_false(dt_tracing) &&				\
	    __predict_false(dtpv->dtpv_recording)) {			\
		dtpv->dtpv_leave(dtpv, args);				\
	}								\
} while (0)

#define _DT_STATIC_P(func, name)	(dt_static_##func##_##name)

/*
 * Probe definition for the static provider.
 */
#define _DT_STATIC_PROBEN(func, name, arg0, arg1, arg2, arg3, arg4, n)	\
	struct dt_probe _DT_STATIC_P(func, name) = {			\
		.dtp_next = { NULL },					\
		.dtp_pcbs = { NULL },					\
		.dtp_prov = &dt_prov_static,				\
		.dtp_func = #func,					\
		.dtp_name = #name,					\
		.dtp_pbn = 0,						\
		.dtp_sysnum = 0,					\
		.dtp_argtype = { arg0, arg1, arg2, arg3, arg4 },	\
		.dtp_nargs = n,					\
	}								\

#define	DT_STATIC_PROBE0(func, name)					\
	_DT_STATIC_PROBEN(func, name, NULL, NULL, NULL, NULL, NULL, 0)

#define	DT_STATIC_PROBE1(func, name, arg0)				\
	_DT_STATIC_PROBEN(func, name, arg0, NULL, NULL, NULL, NULL, 1)

#define	DT_STATIC_PROBE2(func, name, arg0, arg1)			\
	_DT_STATIC_PROBEN(func, name, arg0, arg1, NULL, NULL, NULL, 2)

#define	DT_STATIC_PROBE3(func, name, arg0, arg1, arg2)		\
	_DT_STATIC_PROBEN(func, name, arg0, arg1, arg2, NULL, NULL, 3)

#define	DT_STATIC_PROBE4(func, name, arg0, arg1, arg2, arg3)		\
	_DT_STATIC_PROBEN(func, name, arg0, arg1, arg2, arg3, NULL, 4)

#define	DT_STATIC_PROBE5(func, name, arg0, arg1, arg2, arg3, arg4)	\
	_DT_STATIC_PROBEN(func, name, arg0, arg1, arg2, arg3, arg4, 5)

#define DT_STATIC_ENTER(func, name, args...) do {			\
	extern struct dt_probe _DT_STATIC_P(func, name);		\
	struct dt_probe *dtp = &_DT_STATIC_P(func, name);		\
									\
	if (__predict_false(dt_tracing) &&				\
	    __predict_false(dtp->dtp_recording)) {			\
		struct dt_provider *dtpv = dtp->dtp_prov;		\
									\
		dtpv->dtpv_enter(dtpv, dtp, args);			\
	}								\
} while (0)

#define _DT_INDEX_P(func)		(dtps_index_##func)

#define DT_INDEX_ENTER(func, index, args...) do {			\
	extern struct dt_probe **_DT_INDEX_P(func);			\
									\
	if (__predict_false(dt_tracing) &&				\
	    __predict_false(index > 0) &&				\
	    __predict_true(_DT_INDEX_P(func) != NULL)) {		\
		struct dt_probe *dtp = _DT_INDEX_P(func)[index];	\
									\
		if(__predict_false(dtp->dtp_recording)) {		\
			struct dt_provider *dtpv = dtp->dtp_prov;	\
									\
			dtpv->dtpv_enter(dtpv, dtp, args);		\
		}							\
	}								\
} while (0)

#endif /* !_KERNEL */
#endif /* !_DT_H_ */
