/*-
 * Copyright (c) 2005-2007, Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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

#ifndef	_LIBPMCSTAT_H_
#define	_LIBPMCSTAT_H_

#include <sys/_cpuset.h>
#include <sys/queue.h>

#include <stdio.h>
#include <gelf.h>

#define	PMCSTAT_ALLOCATE		1

#define	NSOCKPAIRFD			2
#define	PARENTSOCKET			0
#define	CHILDSOCKET			1

#define	PMCSTAT_OPEN_FOR_READ		0
#define	PMCSTAT_OPEN_FOR_WRITE		1
#define	READPIPEFD			0
#define	WRITEPIPEFD			1
#define	NPIPEFD				2

#define	PMCSTAT_NHASH			256
#define	PMCSTAT_HASH_MASK		0xFF
#define	DEFAULT_SAMPLE_COUNT		65536

typedef const void *pmcstat_interned_string;
struct pmc_plugins;

enum pmcstat_state {
	PMCSTAT_FINISHED = 0,
	PMCSTAT_EXITING  = 1,
	PMCSTAT_RUNNING  = 2
};

struct pmcstat_ev {
	STAILQ_ENTRY(pmcstat_ev) ev_next;
	int		ev_count; /* associated count if in sampling mode */
	uint32_t	ev_cpu;	  /* cpus for this event */
	int		ev_cumulative;  /* show cumulative counts */
	int		ev_flags; /* PMC_F_* */
	int		ev_fieldskip;   /* #leading spaces */
	int		ev_fieldwidth;  /* print width */
	enum pmc_mode	ev_mode;  /* desired mode */
	char	       *ev_name;  /* (derived) event name */
	pmc_id_t	ev_pmcid; /* allocated ID */
	pmc_value_t	ev_saved; /* for incremental counts */
	char	       *ev_spec;  /* event specification */
};

struct pmcstat_target {
	SLIST_ENTRY(pmcstat_target) pt_next;
	pid_t		pt_pid;
};

struct pmcstat_args {
	int	pa_flags;		/* argument flags */
#define	FLAG_HAS_TARGET			0x00000001	/* process target */
#define	FLAG_HAS_WAIT_INTERVAL		0x00000002	/* -w secs */
#define	FLAG_HAS_OUTPUT_LOGFILE		0x00000004	/* -O file or pipe */
#define	FLAG_HAS_COMMANDLINE		0x00000008	/* command */
#define	FLAG_HAS_SAMPLING_PMCS		0x00000010	/* -S or -P */
#define	FLAG_HAS_COUNTING_PMCS		0x00000020	/* -s or -p */
#define	FLAG_HAS_PROCESS_PMCS		0x00000040	/* -P or -p */
#define	FLAG_HAS_SYSTEM_PMCS		0x00000080	/* -S or -s */
#define	FLAG_HAS_PIPE			0x00000100	/* implicit log */
#define	FLAG_READ_LOGFILE		0x00000200	/* -R file */
#define	FLAG_DO_GPROF			0x00000400	/* -g */
#define	FLAG_HAS_SAMPLESDIR		0x00000800	/* -D dir */
#define	FLAG_HAS_KERNELPATH		0x00001000	/* -k kernel */
#define	FLAG_DO_PRINT			0x00002000	/* -o */
#define	FLAG_DO_CALLGRAPHS		0x00004000	/* -G or -F */
#define	FLAG_DO_ANNOTATE		0x00008000	/* -m */
#define	FLAG_DO_TOP			0x00010000	/* -T */
#define	FLAG_DO_ANALYSIS		0x00020000	/* -g or -G or -m or -T */
#define	FLAGS_HAS_CPUMASK		0x00040000	/* -c */
#define	FLAG_HAS_DURATION		0x00080000	/* -l secs */
#define	FLAG_DO_WIDE_GPROF_HC		0x00100000	/* -e */
#define	FLAG_SKIP_TOP_FN_RES		0x00200000	/* -I */
#define	FLAG_FILTER_THREAD_ID		0x00400000	/* -L */

	int	pa_required;		/* required features */
	int	pa_pplugin;		/* pre-processing plugin */
	int	pa_plugin;		/* analysis plugin */
	int	pa_verbosity;		/* verbosity level */
	FILE	*pa_printfile;		/* where to send printed output */
	int	pa_logfd;		/* output log file */
	char	*pa_inputpath;		/* path to input log */
	char	*pa_outputpath;		/* path to output log */
	void	*pa_logparser;		/* log file parser */
	const char	*pa_fsroot;	/* FS root where executables reside */
	char	*pa_kernel;		/* pathname of the kernel */
	const char	*pa_samplesdir;	/* directory for profile files */
	const char	*pa_mapfilename;/* mapfile name */
	FILE	*pa_graphfile;		/* where to send the callgraph */
	int	pa_graphdepth;		/* print depth for callgraphs */
	double	pa_interval;		/* printing interval in seconds */
	cpuset_t	pa_cpumask;	/* filter for CPUs analysed */
	int	pa_ctdumpinstr;		/* dump instructions with calltree */
	int	pa_topmode;		/* delta or accumulative */
	int	pa_toptty;		/* output to tty or file */
	int	pa_topcolor;		/* terminal support color */
	int	pa_mergepmc;		/* merge PMC with same name */
	double	pa_duration;		/* time duration */
	uint32_t pa_tid;
	int	pa_argc;
	char	**pa_argv;
	STAILQ_HEAD(, pmcstat_ev) pa_events;
	SLIST_HEAD(, pmcstat_target) pa_targets;
};

/*
 * Each function symbol tracked by pmcstat(8).
 */

struct pmcstat_symbol {
	pmcstat_interned_string ps_name;
	uint64_t	ps_start;
	uint64_t	ps_end;
};

/*
 * A 'pmcstat_image' structure describes an executable program on
 * disk.  'pi_execpath' is a cookie representing the pathname of
 * the executable.  'pi_start' and 'pi_end' are the least and greatest
 * virtual addresses for the text segments in the executable.
 * 'pi_gmonlist' contains a linked list of gmon.out files associated
 * with this image.
 */

enum pmcstat_image_type {
	PMCSTAT_IMAGE_UNKNOWN = 0,	/* never looked at the image */
	PMCSTAT_IMAGE_INDETERMINABLE,	/* can't tell what the image is */
	PMCSTAT_IMAGE_ELF32,		/* ELF 32 bit object */
	PMCSTAT_IMAGE_ELF64,		/* ELF 64 bit object */
	PMCSTAT_IMAGE_AOUT		/* AOUT object */
};

struct pmcstat_image {
	LIST_ENTRY(pmcstat_image) pi_next;	/* hash link */
	pmcstat_interned_string	pi_execpath;    /* cookie */
	pmcstat_interned_string pi_samplename;  /* sample path name */
	pmcstat_interned_string pi_fullpath;    /* path to FS object */
	pmcstat_interned_string pi_name;	/* display name */

	enum pmcstat_image_type pi_type;	/* executable type */

	/*
	 * Executables have pi_start and pi_end; these are zero
	 * for shared libraries.
	 */
	uintfptr_t	pi_start;	/* start address (inclusive) */
	uintfptr_t	pi_end;		/* end address (exclusive) */
	uintfptr_t	pi_entry;	/* entry address */
	uintfptr_t	pi_vaddr;	/* virtual address where loaded */
	int		pi_isdynamic;	/* whether a dynamic object */
	int		pi_iskernelmodule;
	pmcstat_interned_string pi_dynlinkerpath; /* path in .interp */

	/* All symbols associated with this object. */
	struct pmcstat_symbol *pi_symbols;
	size_t		pi_symcount;

	/* Handle to addr2line for this image. */
	FILE *pi_addr2line;

	/*
	 * Plugins private data
	 */

	/* gprof:
	 * An image can be associated with one or more gmon.out files;
	 * one per PMC.
	 */
	LIST_HEAD(,pmcstat_gmonfile) pi_gmlist;
};

extern LIST_HEAD(pmcstat_image_hash_list, pmcstat_image) pmcstat_image_hash[PMCSTAT_NHASH];

/*
 * A simple implementation of interned strings.  Each interned string
 * is assigned a unique address, so that subsequent string compares
 * can be done by a simple pointer comparison instead of using
 * strcmp().  This speeds up hash table lookups and saves memory if
 * duplicate strings are the norm.
 */
struct pmcstat_string {
	LIST_ENTRY(pmcstat_string)	ps_next;	/* hash link */
	int		ps_len;
	int		ps_hash;
	char		*ps_string;
};

/*
 * A 'pmcstat_pcmap' structure maps a virtual address range to an
 * underlying 'pmcstat_image' descriptor.
 */
struct pmcstat_pcmap {
	TAILQ_ENTRY(pmcstat_pcmap) ppm_next;
	uintfptr_t	ppm_lowpc;
	uintfptr_t	ppm_highpc;
	struct pmcstat_image *ppm_image;
};

/*
 * A 'pmcstat_process' structure models processes.  Each process is
 * associated with a set of pmcstat_pcmap structures that map
 * addresses inside it to executable objects.  This set is implemented
 * as a list, kept sorted in ascending order of mapped addresses.
 *
 * 'pp_pid' holds the pid of the process.  When a process exits, the
 * 'pp_isactive' field is set to zero, but the process structure is
 * not immediately reclaimed because there may still be samples in the
 * log for this process.
 */

struct pmcstat_process {
	LIST_ENTRY(pmcstat_process) pp_next;	/* hash-next */
	pid_t			pp_pid;		/* associated pid */
	int			pp_isactive;	/* whether active */
	uintfptr_t		pp_entryaddr;	/* entry address */
	TAILQ_HEAD(,pmcstat_pcmap) pp_map;	/* address range map */
};
extern LIST_HEAD(pmcstat_process_hash_list, pmcstat_process) pmcstat_process_hash[PMCSTAT_NHASH];

/*
 * 'pmcstat_pmcrecord' is a mapping from PMC ids to human-readable
 * names.
 */

struct pmcstat_pmcrecord {
	LIST_ENTRY(pmcstat_pmcrecord)	pr_next;
	pmc_id_t			pr_pmcid;
	int				pr_pmcin;
	pmcstat_interned_string		pr_pmcname;
	int				pr_samples;
	int				pr_dubious_frames;
	struct pmcstat_pmcrecord	*pr_merge;
};
extern LIST_HEAD(pmcstat_pmcs, pmcstat_pmcrecord) pmcstat_pmcs; /* PMC list */

struct pmc_plugins {
	const char *pl_name;

	/* configure */
	int (*pl_configure)(char *opt);

	/* init and shutdown */
	int (*pl_init)(void);
	void (*pl_shutdown)(FILE *mf);

	/* sample processing */
	void (*pl_process)(struct pmcstat_process *pp,
	    struct pmcstat_pmcrecord *pmcr, uint32_t nsamples,
	    uintfptr_t *cc, int usermode, uint32_t cpu);

	/* image */
	void (*pl_initimage)(struct pmcstat_image *pi);
	void (*pl_shutdownimage)(struct pmcstat_image *pi);

	/* pmc */
	void (*pl_newpmc)(pmcstat_interned_string ps,
		struct pmcstat_pmcrecord *pr);
	
	/* top display */
	void (*pl_topdisplay)(void);

	/* top keypress */
	int (*pl_topkeypress)(int c, void *w);
};

/*
 * Misc. statistics
 */
struct pmcstat_stats {
	int ps_exec_aout;	/* # a.out executables seen */
	int ps_exec_elf;	/* # elf executables seen */
	int ps_exec_errors;	/* # errors processing executables */
	int ps_exec_indeterminable; /* # unknown executables seen */
	int ps_samples_total;	/* total number of samples processed */
	int ps_samples_skipped; /* #samples filtered out for any reason */
	int ps_samples_unknown_offset;	/* #samples of rank 0 not in a map */
	int ps_samples_indeterminable;	/* #samples in indeterminable images */
	int ps_samples_unknown_function;/* #samples with unknown function at offset */
	int ps_callchain_dubious_frames;/* #dubious frame pointers seen */
};

__BEGIN_DECLS
int pmcstat_symbol_compare(const void *a, const void *b);
struct pmcstat_symbol *pmcstat_symbol_search(struct pmcstat_image *image,
    uintfptr_t addr);
void pmcstat_image_add_symbols(struct pmcstat_image *image, Elf *e,
    Elf_Scn *scn, GElf_Shdr *sh);

const char *pmcstat_string_unintern(pmcstat_interned_string _is);
pmcstat_interned_string pmcstat_string_intern(const char *_s);
int pmcstat_string_compute_hash(const char *s);
pmcstat_interned_string pmcstat_string_lookup(const char *_s);
void pmcstat_image_get_elf_params(struct pmcstat_image *image, struct pmcstat_args *args);

struct pmcstat_image *
    pmcstat_image_from_path(pmcstat_interned_string internedpath,
    int iskernelmodule, struct pmcstat_args *args,
    struct pmc_plugins *plugins);
int pmcstat_string_lookup_hash(pmcstat_interned_string _is);

void pmcstat_process_elf_exec(struct pmcstat_process *_pp,
    struct pmcstat_image *_image, uintfptr_t _entryaddr,
    struct pmcstat_args *args, struct pmc_plugins *plugins,
    struct pmcstat_stats *pmcstat_stats);

void pmcstat_image_link(struct pmcstat_process *_pp,
    struct pmcstat_image *_i, uintfptr_t _lpc);

void pmcstat_process_aout_exec(struct pmcstat_process *_pp,
    struct pmcstat_image *_image, uintfptr_t _entryaddr);
void pmcstat_process_exec(struct pmcstat_process *_pp,
    pmcstat_interned_string _path, uintfptr_t _entryaddr,
    struct pmcstat_args *args, struct pmc_plugins *plugins,
    struct pmcstat_stats *pmcstat_stats);
void pmcstat_image_determine_type(struct pmcstat_image *_image, struct pmcstat_args *args);
void pmcstat_image_get_aout_params(struct pmcstat_image *_image, struct pmcstat_args *args);
struct pmcstat_pcmap *pmcstat_process_find_map(struct pmcstat_process *_p,
    uintfptr_t _pc);
void pmcstat_initialize_logging(struct pmcstat_process **pmcstat_kernproc,
    struct pmcstat_args *args, struct pmc_plugins *plugins,
    int *pmcstat_npmcs, int *pmcstat_mergepmc);
void pmcstat_shutdown_logging(struct pmcstat_args *args,
    struct pmc_plugins *plugins,
    struct pmcstat_stats *pmcstat_stats);
struct pmcstat_process *pmcstat_process_lookup(pid_t _pid, int _allocate);
void pmcstat_clone_event_descriptor(struct pmcstat_ev *ev, const cpuset_t *cpumask, struct pmcstat_args *args);

void pmcstat_create_process(int *pmcstat_sockpair, struct pmcstat_args *args, int pmcstat_kq);
void pmcstat_start_process(int *pmcstat_sockpair);

void pmcstat_attach_pmcs(struct pmcstat_args *args);
struct pmcstat_symbol *pmcstat_symbol_search_by_name(struct pmcstat_process *pp, const char *pi_name, const char *name, uintptr_t *, uintptr_t *);

void pmcstat_string_initialize(void);
void pmcstat_string_shutdown(void);

int pmcstat_analyze_log(struct pmcstat_args *args,
    struct pmc_plugins *plugins,
    struct pmcstat_stats *pmcstat_stats,
    struct pmcstat_process *pmcstat_kernproc,
    int pmcstat_mergepmc,
    int *pmcstat_npmcs,
    int *ps_samples_period);

int pmcstat_open_log(const char *_p, int _mode);
int pmcstat_close_log(struct pmcstat_args *args);

__END_DECLS

#endif /* !_LIBPMCSTAT_H_ */
