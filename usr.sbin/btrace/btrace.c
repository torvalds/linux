/*	$OpenBSD: btrace.c,v 1.98 2025/09/22 07:49:43 sashan Exp $ */

/*
 * Copyright (c) 2019 - 2023 Martin Pieuchot <mpi@openbsd.org>
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

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <paths.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <dev/dt/dtvar.h>

#include <gelf.h>

#include "btrace.h"
#include "bt_parser.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

/*
 * Maximum number of operands an arithmetic operation can have.  This
 * is necessary to stop infinite recursion when evaluating expressions.
 */
#define __MAXOPERANDS	5

#define __PATH_DEVDT "/dev/dt"

__dead void		 usage(void);
char			*read_btfile(const char *, size_t *);

/*
 * Retrieve & parse probe information.
 */
void			 dtpi_cache(int);
void			 dtpi_print_list(int);
const char		*dtpi_func(struct dtioc_probe_info *);
struct dtioc_probe_info	*dtpi_get_by_value(const char *, const char *,
			     const char *);

/*
 * Main loop and rule evaluation.
 */
void			 probe_bail(struct bt_probe *);
const char		*probe_name(struct bt_probe *);
void			 rules_do(int);
int			 rules_setup(int);
int			 rules_apply(int, struct dt_evt *);
void			 rules_teardown(int);
int			 rule_eval(struct bt_rule *, struct dt_evt *);
void			 rule_printmaps(struct bt_rule *);

/*
 * Language builtins & functions.
 */
uint64_t		 builtin_nsecs(struct dt_evt *);
const char		*builtin_arg(struct dt_evt *, enum bt_argtype);
struct bt_arg		*fn_str(struct bt_arg *, struct dt_evt *, char *);
int			 stmt_eval(struct bt_stmt *, struct dt_evt *);
void			 stmt_bucketize(struct bt_stmt *, struct dt_evt *);
void			 stmt_clear(struct bt_stmt *);
void			 stmt_delete(struct bt_stmt *, struct dt_evt *);
void			 stmt_insert(struct bt_stmt *, struct dt_evt *);
void			 stmt_print(struct bt_stmt *, struct dt_evt *);
void			 stmt_store(struct bt_stmt *, struct dt_evt *);
bool			 stmt_test(struct bt_stmt *, struct dt_evt *);
void			 stmt_time(struct bt_stmt *, struct dt_evt *);
void			 stmt_zero(struct bt_stmt *);
struct bt_arg		*ba_read(struct bt_arg *);
struct bt_arg		*baeval(struct bt_arg *, struct dt_evt *);
const char		*ba2hash(struct bt_arg *, struct dt_evt *);
long			 baexpr2long(struct bt_arg *, struct dt_evt *);
const char		*ba2bucket(struct bt_arg *, struct bt_arg *,
			     struct dt_evt *, long *);
int			 ba2dtflags(struct bt_arg *);

/*
 * Debug routines.
 */
__dead void		 xabort(const char *, ...);
void			 debug(const char *, ...);
void			 debugx(const char *, ...);
void			 debug_dump_term(struct bt_arg *);
void			 debug_dump_expr(struct bt_arg *);
void			 debug_dump_filter(struct bt_rule *);

struct dtioc_probe_info	*dt_dtpis;	/* array of available probes */
size_t			 dt_ndtpi;	/* # of elements in the array */
struct dtioc_arg_info  **dt_args;	/* array of probe arguments */

struct dt_evt		 bt_devt;	/* fake event for BEGIN/END */
#define EVENT_BEGIN	 0
#define EVENT_END	 (unsigned int)(-1)
uint64_t		 bt_filtered;	/* # of events filtered out */

struct syms		*kelf;

char			**vargs;
int			 nargs = 0;
int			 verbose = 0;
int			 dtfd;
volatile sig_atomic_t	 quit_pending;

static void
signal_handler(int sig)
{
	quit_pending = sig;
}


int
main(int argc, char *argv[])
{
	int fd = -1, ch, error = 0;
	const char *filename = NULL, *btscript = NULL;
	int showprobes = 0, noaction = 0;
	size_t btslen = 0;

	setlocale(LC_ALL, "");

	while ((ch = getopt(argc, argv, "e:lnp:v")) != -1) {
		switch (ch) {
		case 'e':
			btscript = optarg;
			btslen = strlen(btscript);
			break;
		case 'l':
			showprobes = 1;
			break;
		case 'n':
			noaction = 1;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0 && btscript == NULL)
		filename = argv[0];

	 /* Cannot pledge due to special ioctl()s */
	if (unveil(__PATH_DEVDT, "r") == -1)
		err(1, "unveil %s", __PATH_DEVDT);
	if (unveil(_PATH_KSYMS, "r") == -1)
		err(1, "unveil %s", _PATH_KSYMS);
	if (filename != NULL) {
		if (unveil(filename, "r") == -1)
			err(1, "unveil %s", filename);
	}
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	if (filename != NULL) {
		btscript = read_btfile(filename, &btslen);
		argc--;
		argv++;
	}

	nargs = argc;
	vargs = argv;

	if (btscript == NULL && !showprobes)
		usage();

	if (btscript != NULL) {
		error = btparse(btscript, btslen, filename, 1);
		if (error)
			return error;
	}

	if (noaction)
		return error;

	if (showprobes || g_nprobes > 0) {
		fd = open(__PATH_DEVDT, O_RDONLY);
		if (fd == -1)
			err(1, "could not open %s", __PATH_DEVDT);
		dtfd = fd;
	}

	if (showprobes) {
		dtpi_cache(fd);
		dtpi_print_list(fd);
	}

	if (!TAILQ_EMPTY(&g_rules))
		rules_do(fd);

	if (fd != -1)
		close(fd);

	return error;
}

__dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-lnv] [-p elffile] "
	    "programfile | -e program [argument ...]\n", getprogname());
	exit(1);
}

char *
read_btfile(const char *filename, size_t *len)
{
	FILE *fp;
	char *fcontent;
	struct stat st;
	size_t fsize;

	if (stat(filename, &st))
		err(1, "can't stat '%s'", filename);

	fsize = st.st_size;
	fcontent = malloc(fsize + 1);
	if (fcontent == NULL)
		err(1, "malloc");

	fp = fopen(filename, "r");
	if (fp == NULL)
		err(1, "can't open '%s'", filename);

	if (fread(fcontent, 1, fsize, fp) != fsize)
		err(1, "can't read '%s'", filename);
	fcontent[fsize] = '\0';

	fclose(fp);
	*len = fsize;
	return fcontent;
}

void
dtpi_cache(int fd)
{
	struct dtioc_probe dtpr;

	if (dt_dtpis != NULL)
		return;

	memset(&dtpr, 0, sizeof(dtpr));
	if (ioctl(fd, DTIOCGPLIST, &dtpr))
		err(1, "DTIOCGPLIST");

	dt_ndtpi = dtpr.dtpr_size / sizeof(*dt_dtpis);
	dt_dtpis = reallocarray(NULL, dt_ndtpi, sizeof(*dt_dtpis));
	if (dt_dtpis == NULL)
		err(1, NULL);

	dtpr.dtpr_probes = dt_dtpis;
	if (ioctl(fd, DTIOCGPLIST, &dtpr))
		err(1, "DTIOCGPLIST");
}

void
dtai_cache(int fd, struct dtioc_probe_info *dtpi)
{
	struct dtioc_arg dtar;

	if (dt_args == NULL) {
		dt_args = calloc(dt_ndtpi, sizeof(*dt_args));
		if (dt_args == NULL)
			err(1, NULL);
	}

	if (dt_args[dtpi->dtpi_pbn - 1] != NULL)
		return;

	dt_args[dtpi->dtpi_pbn - 1] = reallocarray(NULL, dtpi->dtpi_nargs,
	    sizeof(**dt_args));
	if (dt_args[dtpi->dtpi_pbn - 1] == NULL)
		err(1, NULL);

	dtar.dtar_pbn = dtpi->dtpi_pbn;
	dtar.dtar_size = dtpi->dtpi_nargs * sizeof(**dt_args);
	dtar.dtar_args = dt_args[dtpi->dtpi_pbn - 1];
	if (ioctl(fd, DTIOCGARGS, &dtar))
		err(1, "DTIOCGARGS");
}

void
dtpi_print_list(int fd)
{
	struct dtioc_probe_info *dtpi;
	struct dtioc_arg_info *dtai;
	size_t i, j;

	dtpi = dt_dtpis;
	for (i = 0; i < dt_ndtpi; i++, dtpi++) {
		printf("%s:%s:%s", dtpi->dtpi_prov, dtpi_func(dtpi),
		    dtpi->dtpi_name);
		if (strncmp(dtpi->dtpi_prov, "tracepoint", DTNAMESIZE) == 0) {
			dtai_cache(fd, dtpi);
			dtai = dt_args[dtpi->dtpi_pbn - 1];
			printf("(");
			for (j = 0; j < dtpi->dtpi_nargs; j++, dtai++) {
				if (j > 0)
					printf(", ");
				printf("%s", dtai->dtai_argtype);
			}
			printf(")");
		}
		printf("\n");
	}
}

const char *
dtpi_func(struct dtioc_probe_info *dtpi)
{
	char *sysnb, func[DTNAMESIZE];
	const char *errstr;
	int idx;

	if (strncmp(dtpi->dtpi_prov, "syscall", DTNAMESIZE))
		return dtpi->dtpi_func;

	/* Translate syscall names */
	strlcpy(func, dtpi->dtpi_func, sizeof(func));
	sysnb = func;
	if (strsep(&sysnb, "%") == NULL)
		return dtpi->dtpi_func;

	idx = strtonum(sysnb, 1, SYS_MAXSYSCALL, &errstr);
	if (errstr != NULL)
		return dtpi->dtpi_func;

	return syscallnames[idx];
}

struct dtioc_probe_info *
dtpi_get_by_value(const char *prov, const char *func, const char *name)
{
	struct dtioc_probe_info *dtpi;
	size_t i;

	dtpi = dt_dtpis;
	for (i = 0; i < dt_ndtpi; i++, dtpi++) {
		if (prov != NULL) {
			if (strncmp(prov, dtpi->dtpi_prov, DTNAMESIZE))
				continue;
		}
		if (func != NULL && name != NULL) {
			if (strncmp(func, dtpi_func(dtpi), DTNAMESIZE))
				continue;
			if (strncmp(name, dtpi->dtpi_name, DTNAMESIZE))
				continue;
		}

		debug("matched probe %s:%s:%s\n", dtpi->dtpi_prov,
		    dtpi_func(dtpi), dtpi->dtpi_name);
		return dtpi;
	}

	return NULL;
}

static uint64_t
bp_nsecs_to_unit(struct bt_probe *bp)
{
	static const struct {
		const char *name;
		enum { UNIT_HZ, UNIT_US, UNIT_MS, UNIT_S } id;
	} units[] = {
		{ .name = "hz", .id = UNIT_HZ },
		{ .name = "us", .id = UNIT_US },
		{ .name = "ms", .id = UNIT_MS },
		{ .name = "s", .id = UNIT_S },
	};
	size_t i;

	for (i = 0; i < nitems(units); i++) {
		if (strcmp(units[i].name, bp->bp_unit) == 0) {
			switch (units[i].id) {
			case UNIT_HZ:
				return (1000000000LLU / bp->bp_nsecs);
			case UNIT_US:
				return (bp->bp_nsecs / 1000LLU);
			case UNIT_MS:
				return (bp->bp_nsecs / 1000000LLU);
			case UNIT_S:
				return (bp->bp_nsecs / 1000000000LLU);
			}
		}
	}
	return 0;
}

void
probe_bail(struct bt_probe *bp)
{
	errx(1, "Cannot register multiple probes of the same type: '%s'",
	    probe_name(bp));
}

const char *
probe_name(struct bt_probe *bp)
{
	static char buf[64];

	if (bp->bp_type == B_PT_BEGIN)
		return "BEGIN";

	if (bp->bp_type == B_PT_END)
		return "END";

	assert(bp->bp_type == B_PT_PROBE);

	if (bp->bp_nsecs) {
		snprintf(buf, sizeof(buf), "%s:%s:%llu", bp->bp_prov,
		    bp->bp_unit, bp_nsecs_to_unit(bp));
	} else {
		snprintf(buf, sizeof(buf), "%s:%s:%s", bp->bp_prov,
		    bp->bp_unit, bp->bp_name);
	}

	return buf;
}

void
rules_do(int fd)
{
	struct sigaction sa;
	int halt = 0;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = signal_handler;
	if (sigaction(SIGINT, &sa, NULL))
		err(1, "sigaction");
	if (sigaction(SIGTERM, &sa, NULL))
		err(1, "sigaction");

	halt = rules_setup(fd);

	while (!quit_pending && !halt && g_nprobes > 0) {
		static struct dt_evt devtbuf[64];
		ssize_t rlen;
		size_t i;

		rlen = read(fd, devtbuf, sizeof(devtbuf));
		if (rlen == -1) {
			if (errno == EINTR && quit_pending) {
				printf("\n");
				break;
			}
			err(1, "read");
		}

		if ((rlen % sizeof(struct dt_evt)) != 0)
			err(1, "incorrect read");

		for (i = 0; i < rlen / sizeof(struct dt_evt); i++) {
			halt = rules_apply(fd, &devtbuf[i]);
			if (halt)
				break;
		}
	}

	rules_teardown(fd);

	if (verbose && fd != -1) {
		struct dtioc_stat dtst;

		memset(&dtst, 0, sizeof(dtst));
		if (ioctl(fd, DTIOCGSTATS, &dtst))
			warn("DTIOCGSTATS");

		fprintf(stderr, "%llu events read\n", dtst.dtst_readevt);
		fprintf(stderr, "%llu events dropped\n", dtst.dtst_dropevt);
		fprintf(stderr, "%llu events filtered\n", bt_filtered);
		fprintf(stderr, "%llu clock ticks skipped\n",
			dtst.dtst_skiptick);
		fprintf(stderr, "%llu recursive events dropped\n",
			dtst.dtst_recurevt);
	}
}

static uint64_t
rules_action_scan(struct bt_stmt *bs)
{
	struct bt_arg *ba;
	struct bt_cond *bc;
	uint64_t evtflags = 0;

	while (bs != NULL) {
		SLIST_FOREACH(ba, &bs->bs_args, ba_next)
			evtflags |= ba2dtflags(ba);

		/* Also check the value for map/hist insertion */
		switch (bs->bs_act) {
		case B_AC_BUCKETIZE:
		case B_AC_INSERT:
			ba = (struct bt_arg *)bs->bs_var;
			evtflags |= ba2dtflags(ba);
			break;
		case B_AC_TEST:
			bc = (struct bt_cond *)bs->bs_var;
			evtflags |= rules_action_scan(bc->bc_condbs);
			evtflags |= rules_action_scan(bc->bc_elsebs);
			break;
		default:
			break;
		}

		bs = SLIST_NEXT(bs, bs_next);
	}

	return evtflags;
}

int
rules_setup(int fd)
{
	struct dtioc_probe_info *dtpi;
	struct dtioc_req *dtrq;
	struct bt_rule *r, *rbegin = NULL, *rend = NULL;
	struct bt_probe *bp;
	struct bt_stmt *bs;
	struct bt_arg *ba;
	int dokstack = 0, halt = 0, on = 1;
	uint64_t evtflags;

	TAILQ_FOREACH(r, &g_rules, br_next) {
		evtflags = 0;

		if (r->br_filter != NULL &&
		    r->br_filter->bf_condition != NULL)  {

			bs = r->br_filter->bf_condition;
			ba = SLIST_FIRST(&bs->bs_args);

			evtflags |= ba2dtflags(ba);
		}

		evtflags |= rules_action_scan(SLIST_FIRST(&r->br_action));

		SLIST_FOREACH(bp, &r->br_probes, bp_next) {
			debug("parsed probe '%s'", probe_name(bp));
			debug_dump_filter(r);

			if (bp->bp_type != B_PT_PROBE) {
				if (bp->bp_type == B_PT_BEGIN) {
					if (rbegin != NULL)
						probe_bail(bp);
					rbegin = r;
				}
				if (bp->bp_type == B_PT_END) {
					if (rend != NULL)
						probe_bail(bp);
					rend = r;
				}
				continue;
			}

			dtpi_cache(fd);
			dtpi = dtpi_get_by_value(bp->bp_prov, bp->bp_func,
			    bp->bp_name);
			if (dtpi == NULL) {
				errx(1, "probe '%s:%s:%s' not found",
				    bp->bp_prov, bp->bp_func, bp->bp_name);
			}

			dtrq = calloc(1, sizeof(*dtrq));
			if (dtrq == NULL)
				err(1, "dtrq: 1alloc");

			bp->bp_pbn = dtpi->dtpi_pbn;
			dtrq->dtrq_pbn = dtpi->dtpi_pbn;
			dtrq->dtrq_nsecs = bp->bp_nsecs;
			dtrq->dtrq_evtflags = evtflags;
			if (dtrq->dtrq_evtflags & DTEVT_KSTACK)
				dokstack = 1;
			bp->bp_cookie = dtrq;
		}
	}

	if (dokstack)
		kelf = kelf_open_kernel(_PATH_KSYMS);

	/* Initialize "fake" event for BEGIN/END */
	bt_devt.dtev_pbn = EVENT_BEGIN;
	strlcpy(bt_devt.dtev_comm, getprogname(), sizeof(bt_devt.dtev_comm));
	bt_devt.dtev_pid = getpid();
	bt_devt.dtev_tid = getthrid();
	clock_gettime(CLOCK_REALTIME, &bt_devt.dtev_tsp);

	if (rbegin)
		halt = rule_eval(rbegin, &bt_devt);

	/* Enable all probes */
	TAILQ_FOREACH(r, &g_rules, br_next) {
		SLIST_FOREACH(bp, &r->br_probes, bp_next) {
			if (bp->bp_type != B_PT_PROBE)
				continue;

			dtrq = bp->bp_cookie;
			if (ioctl(fd, DTIOCPRBENABLE, dtrq)) {
				if (errno == EEXIST)
					probe_bail(bp);
				err(1, "DTIOCPRBENABLE");
			}
		}
	}

	if (g_nprobes > 0) {
		if (ioctl(fd, DTIOCRECORD, &on))
			err(1, "DTIOCRECORD");
	}

	return halt;
}

/*
 * Returns non-zero if the program should halt.
 */
int
rules_apply(int fd, struct dt_evt *dtev)
{
	struct bt_rule *r;
	struct bt_probe *bp;

	TAILQ_FOREACH(r, &g_rules, br_next) {
		SLIST_FOREACH(bp, &r->br_probes, bp_next) {
			if (bp->bp_type != B_PT_PROBE ||
			    bp->bp_pbn != dtev->dtev_pbn)
				continue;

			dtai_cache(fd, &dt_dtpis[dtev->dtev_pbn - 1]);
			if (rule_eval(r, dtev))
				return 1;
		}
	}
	return 0;
}

void
rules_teardown(int fd)
{
	struct dtioc_req *dtrq;
	struct bt_probe *bp;
	struct bt_rule *r, *rend = NULL;
	int dokstack = 0, off = 0;

	if (g_nprobes > 0) {
		if (ioctl(fd, DTIOCRECORD, &off))
			err(1, "DTIOCRECORD");
	}

	TAILQ_FOREACH(r, &g_rules, br_next) {
		SLIST_FOREACH(bp, &r->br_probes, bp_next) {
			if (bp->bp_type != B_PT_PROBE) {
				if (bp->bp_type == B_PT_END)
					rend = r;
				continue;
			}

			dtrq = bp->bp_cookie;
			if (ioctl(fd, DTIOCPRBDISABLE, dtrq))
				err(1, "DTIOCPRBDISABLE");
			if (dtrq->dtrq_evtflags & DTEVT_KSTACK)
				dokstack = 1;
		}
	}

	kelf_close(kelf);

	/* Update "fake" event for BEGIN/END */
	bt_devt.dtev_pbn = EVENT_END;
	clock_gettime(CLOCK_REALTIME, &bt_devt.dtev_tsp);

	if (rend)
		rule_eval(rend, &bt_devt);

	/* Print non-empty map & hist */
	TAILQ_FOREACH(r, &g_rules, br_next)
		rule_printmaps(r);
}

/*
 * Returns non-zero if the program should halt.
 */
int
rule_eval(struct bt_rule *r, struct dt_evt *dtev)
{
	struct bt_stmt *bs;
	struct bt_probe *bp;

	SLIST_FOREACH(bp, &r->br_probes, bp_next) {
		debug("eval rule '%s'", probe_name(bp));
		debug_dump_filter(r);
	}

	if (r->br_filter != NULL && r->br_filter->bf_condition != NULL) {
		if (stmt_test(r->br_filter->bf_condition, dtev) == false) {
			bt_filtered++;
			return 0;
		}
	}

	SLIST_FOREACH(bs, &r->br_action, bs_next) {
		if (stmt_eval(bs, dtev))
			return 1;
	}

	return 0;
}

void
rule_printmaps(struct bt_rule *r)
{
	struct bt_stmt *bs;

	SLIST_FOREACH(bs, &r->br_action, bs_next) {
		struct bt_arg *ba;

		SLIST_FOREACH(ba, &bs->bs_args, ba_next) {
			struct bt_var *bv = ba->ba_value;
			struct map *map;

			if (ba->ba_type != B_AT_MAP && ba->ba_type != B_AT_HIST)
				continue;

			map = (struct map *)bv->bv_value;
			if (map == NULL)
				continue;

			if (ba->ba_type == B_AT_MAP)
				map_print(map, SIZE_T_MAX, bv_name(bv));
			else
				hist_print((struct hist *)map, bv_name(bv));
			map_clear(map);
			bv->bv_value = NULL;
		}
	}
}

time_t
builtin_gettime(struct dt_evt *dtev)
{
	struct timespec ts;

	if (dtev == NULL) {
		clock_gettime(CLOCK_REALTIME, &ts);
		return ts.tv_sec;
	}

	return dtev->dtev_tsp.tv_sec;
}

static inline uint64_t
TIMESPEC_TO_NSEC(struct timespec *ts)
{
	return (ts->tv_sec * 1000000000L + ts->tv_nsec);
}

uint64_t
builtin_nsecs(struct dt_evt *dtev)
{
	struct timespec ts;

	if (dtev == NULL) {
		clock_gettime(CLOCK_REALTIME, &ts);
		return TIMESPEC_TO_NSEC(&ts);
	}

	return TIMESPEC_TO_NSEC(&dtev->dtev_tsp);
}

const char *
builtin_stack(struct dt_evt *dtev, int kernel)
{
	struct stacktrace *st = &dtev->dtev_kstack;
	static char buf[4096];
	const char *last = "\nkernel\n";
	char *bp;
	size_t i;
	int sz;

	if (!kernel) {
		st = &dtev->dtev_ustack;
		last = "\nuserland\n";
	} else if (st->st_count == 0) {
		return "\nuserland\n";
	}

	buf[0] = '\0';
	bp = buf;
	sz = sizeof(buf);
	for (i = 0; i < st->st_count; i++) {
		int l;

		if (!kernel)
			l = kelf_snprintsym_proc(dtfd, dtev->dtev_pid, bp, sz - 1,
			    st->st_pc[i]);
		else
			l = kelf_snprintsym_kernel(kelf, bp, sz - 1,
			    st->st_pc[i]);
		if (l < 0)
			break;
		if (l >= sz - 1) {
			bp += sz - 1;
			sz = 1;
			break;
		}
		bp += l;
		sz -= l;
	}
	snprintf(bp, sz, "%s", last);

	return buf;
}

const char *
builtin_arg(struct dt_evt *dtev, enum bt_argtype dat)
{
	static char buf[sizeof("18446744073709551615")]; /* UINT64_MAX */
	struct dtioc_probe_info *dtpi;
	struct dtioc_arg_info *dtai;
	const char *argtype, *fmt;
	unsigned int argn;
	long value;

	argn = dat - B_AT_BI_ARG0;
	dtpi = &dt_dtpis[dtev->dtev_pbn - 1];
	if (dtpi == NULL || argn >= dtpi->dtpi_nargs)
		return "0";

	dtai = dt_args[dtev->dtev_pbn - 1];
	argtype = dtai[argn].dtai_argtype;

	if (strncmp(argtype, "int", DTNAMESIZE) == 0) {
		fmt = "%d";
		value = (int)dtev->dtev_args[argn];
	} else {
		fmt = "%lu";
		value = dtev->dtev_args[argn];
	}

	snprintf(buf, sizeof(buf), fmt, dtev->dtev_args[argn]);

	return buf;
}

/*
 * Returns non-zero if the program should halt.
 */
int
stmt_eval(struct bt_stmt *bs, struct dt_evt *dtev)
{
	struct bt_stmt *bbs;
	struct bt_cond *bc;
	int halt = 0;

	switch (bs->bs_act) {
	case B_AC_BUCKETIZE:
		stmt_bucketize(bs, dtev);
		break;
	case B_AC_CLEAR:
		stmt_clear(bs);
		break;
	case B_AC_DELETE:
		stmt_delete(bs, dtev);
		break;
	case B_AC_EXIT:
		halt = 1;
		break;
	case B_AC_INSERT:
		stmt_insert(bs, dtev);
		break;
	case B_AC_PRINT:
		stmt_print(bs, dtev);
		break;
	case B_AC_PRINTF:
		stmt_printf(bs, dtev);
		break;
	case B_AC_STORE:
		stmt_store(bs, dtev);
		break;
	case B_AC_TEST:
		bc = (struct bt_cond *)bs->bs_var;
		if (stmt_test(bs, dtev) == true)
			bbs = bc->bc_condbs;
		else
			bbs = bc->bc_elsebs;

		while (bbs != NULL) {
			if (stmt_eval(bbs, dtev))
				return 1;
			bbs = SLIST_NEXT(bbs, bs_next);
		}
		break;
	case B_AC_TIME:
		stmt_time(bs, dtev);
		break;
	case B_AC_ZERO:
		stmt_zero(bs);
		break;
	default:
		xabort("no handler for action type %d", bs->bs_act);
	}
	return halt;
}

/*
 * Increment a bucket:	{ @h = hist(v); } or { @h = lhist(v, min, max, step); }
 *
 * In this case 'h' is represented by `bv' and '(min, max, step)' by `brange'.
 */
void
stmt_bucketize(struct bt_stmt *bs, struct dt_evt *dtev)
{
	struct bt_arg *brange, *bhist = SLIST_FIRST(&bs->bs_args);
	struct bt_arg *bval = (struct bt_arg *)bs->bs_var;
	struct bt_var *bv = bhist->ba_value;
	struct hist *hist;
	const char *bucket;
	long step = 0;

	assert(bhist->ba_type == B_AT_HIST);
	assert(SLIST_NEXT(bval, ba_next) == NULL);

	brange = bhist->ba_key;
	bucket = ba2bucket(bval, brange, dtev, &step);
	if (bucket == NULL) {
		debug("hist=%p '%s' value=%lu out of range\n", bv->bv_value,
		    bv_name(bv), ba2long(bval, dtev));
		return;
	}
	debug("hist=%p '%s' increment bucket '%s'\n", bv->bv_value,
	    bv_name(bv), bucket);

	/* hist is NULL before first insert or after clear() */
	hist = (struct hist *)bv->bv_value;
	if (hist == NULL)
		hist = hist_new(step);

	hist_increment(hist, bucket);

	debug("hist=%p '%s' increment bucket=%p '%s' bval=%p\n", hist,
	    bv_name(bv), brange, bucket, bval);

	bv->bv_value = (struct bt_arg *)hist;
	bv->bv_type = B_VT_HIST;
}


/*
 * Empty a map:		{ clear(@map); }
 */
void
stmt_clear(struct bt_stmt *bs)
{
	struct bt_arg *ba = SLIST_FIRST(&bs->bs_args);
	struct bt_var *bv = ba->ba_value;
	struct map *map;

	assert(bs->bs_var == NULL);
	assert(ba->ba_type == B_AT_VAR);

	map = (struct map *)bv->bv_value;
	if (map == NULL)
		return;

	if (bv->bv_type != B_VT_MAP && bv->bv_type != B_VT_HIST)
		errx(1, "invalid variable type for clear(%s)", ba_name(ba));

	map_clear(map);
	bv->bv_value = NULL;

	debug("map=%p '%s' clear\n", map, bv_name(bv));
}

/*
 * Map delete:	 	{ delete(@map[key]); }
 *
 * In this case 'map' is represented by `bv' and 'key' by `bkey'.
 */
void
stmt_delete(struct bt_stmt *bs, struct dt_evt *dtev)
{
	struct bt_arg *bkey, *bmap = SLIST_FIRST(&bs->bs_args);
	struct bt_var *bv = bmap->ba_value;
	struct map *map;
	const char *hash;

	assert(bmap->ba_type == B_AT_MAP);
	assert(bs->bs_var == NULL);

	map = (struct map *)bv->bv_value;
	if (map == NULL)
		return;

	bkey = bmap->ba_key;
	hash = ba2hash(bkey, dtev);
	debug("map=%p '%s' delete key=%p '%s'\n", map, bv_name(bv), bkey, hash);

	map_delete(map, hash);
}

/*
 * Map insert:	 	{ @map[key] = 42; }
 *
 * In this case 'map' is represented by `bv', 'key' by `bkey' and
 * '42' by `bval'.
 */
void
stmt_insert(struct bt_stmt *bs, struct dt_evt *dtev)
{
	struct bt_arg *bkey, *bmap = SLIST_FIRST(&bs->bs_args);
	struct bt_arg *bval = (struct bt_arg *)bs->bs_var;
	struct bt_var *bv = bmap->ba_value;
	struct map *map;
	const char *hash;
	long val;

	assert(bmap->ba_type == B_AT_MAP);
	assert(SLIST_NEXT(bval, ba_next) == NULL);

	bkey = bmap->ba_key;
	hash = ba2hash(bkey, dtev);

	/* map is NULL before first insert or after clear() */
	map = (struct map *)bv->bv_value;
	if (map == NULL)
		map = map_new();

	/* Operate on existring value for count(), max(), min() and sum(). */
	switch (bval->ba_type) {
	case B_AT_MF_COUNT:
		val = ba2long(map_get(map, hash), NULL);
		val++;
		bval = ba_new(val, B_AT_LONG);
		break;
	case B_AT_MF_MAX:
		val = ba2long(map_get(map, hash), NULL);
		val = MAXIMUM(val, ba2long(bval->ba_value, dtev));
		bval = ba_new(val, B_AT_LONG);
		break;
	case B_AT_MF_MIN:
		val = ba2long(map_get(map, hash), NULL);
		val = MINIMUM(val, ba2long(bval->ba_value, dtev));
		bval = ba_new(val, B_AT_LONG);
		break;
	case B_AT_MF_SUM:
		val = ba2long(map_get(map, hash), NULL);
		val += ba2long(bval->ba_value, dtev);
		bval = ba_new(val, B_AT_LONG);
		break;
	default:
		bval = baeval(bval, dtev);
		break;
	}

	map_insert(map, hash, bval);

	debug("map=%p '%s' insert key=%p '%s' bval=%p\n", map,
	    bv_name(bv), bkey, hash, bval);

	bv->bv_value = (struct bt_arg *)map;
	bv->bv_type = B_VT_MAP;
}

/*
 * Print variables:	{ print(890); print(@map[, 8]); print(comm); }
 *
 * In this case the global variable 'map' is pointed at by `ba'
 * and '8' is represented by `btop'.
 */
void
stmt_print(struct bt_stmt *bs, struct dt_evt *dtev)
{
	struct bt_arg *btop, *ba = SLIST_FIRST(&bs->bs_args);
	struct bt_var *bv = ba->ba_value;
	struct map *map;
	size_t top = SIZE_T_MAX;

	assert(bs->bs_var == NULL);

	/* Parse optional `top' argument. */
	btop = SLIST_NEXT(ba, ba_next);
	if (btop != NULL) {
		assert(SLIST_NEXT(btop, ba_next) == NULL);
		top = ba2long(btop, dtev);
	}

	/* Static argument. */
	if (ba->ba_type != B_AT_VAR) {
		assert(btop == NULL);
		printf("%s\n", ba2str(ba, dtev));
		return;
	}

	map = (struct map *)bv->bv_value;
	if (map == NULL)
		return;

	debug("map=%p '%s' print (top=%d)\n", bv->bv_value, bv_name(bv), top);

	if (bv->bv_type == B_VT_MAP)
		map_print(map, top, bv_name(bv));
	else if (bv->bv_type == B_VT_HIST)
		hist_print((struct hist *)map, bv_name(bv));
	else
		printf("%s\n", ba2str(ba, dtev));
}

/*
 * Variable store: 	{ @var = 3; }
 *
 * In this case '3' is represented by `ba', the argument of a STORE
 * action.
 *
 * If the argument depends of the value of an event (builtin) or is
 * the result of an operation, its evaluation is stored in a new `ba'.
 */
void
stmt_store(struct bt_stmt *bs, struct dt_evt *dtev)
{
	struct bt_arg *ba = SLIST_FIRST(&bs->bs_args);
	struct bt_var *bvar, *bv = bs->bs_var;
	struct map *map;

	assert(SLIST_NEXT(ba, ba_next) == NULL);

	switch (ba->ba_type) {
	case B_AT_STR:
		bv->bv_value = ba;
		bv->bv_type = B_VT_STR;
		break;
	case B_AT_LONG:
		bv->bv_value = ba;
		bv->bv_type = B_VT_LONG;
		break;
	case B_AT_VAR:
		bvar = ba->ba_value;
		bv->bv_type = bvar->bv_type;
		bv->bv_value = bvar->bv_value;
		break;
	case B_AT_MAP:
		bvar = ba->ba_value;
		map = (struct map *)bvar->bv_value;
		/* Uninitialized map */
		if (map == NULL)
			bv->bv_value = 0;
		else
			bv->bv_value = map_get(map, ba2hash(ba->ba_key, dtev));
		bv->bv_type = B_VT_LONG; /* XXX should we type map? */
		break;
	case B_AT_TUPLE:
		bv->bv_value = baeval(ba, dtev);
		bv->bv_type = B_VT_TUPLE;
		break;
	case B_AT_BI_PID:
	case B_AT_BI_TID:
	case B_AT_BI_CPU:
	case B_AT_BI_NSECS:
	case B_AT_BI_RETVAL:
	case B_AT_BI_ARG0 ... B_AT_BI_ARG9:
	case B_AT_OP_PLUS ... B_AT_OP_LOR:
		bv->bv_value = baeval(ba, dtev);
		bv->bv_type = B_VT_LONG;
		break;
	case B_AT_BI_COMM:
	case B_AT_BI_KSTACK:
	case B_AT_BI_USTACK:
	case B_AT_BI_PROBE:
	case B_AT_FN_STR:
		bv->bv_value = baeval(ba, dtev);
		bv->bv_type = B_VT_STR;
		break;
	default:
		xabort("store not implemented for type %d", ba->ba_type);
	}

	debug("bv=%p var '%s' store (%p)='%s'\n", bv, bv_name(bv), bv->bv_value,
	    ba2str(bv->bv_value, dtev));
}

/*
 * String conversion	{ str($1); string($1, 3); }
 *
 * Since fn_str is currently only called in ba2str, *buf should be a pointer
 * to the static buffer provided by ba2str.
 */
struct bt_arg *
fn_str(struct bt_arg *ba, struct dt_evt *dtev, char *buf)
{
	struct bt_arg *arg, *index;
	ssize_t len = STRLEN;

	assert(ba->ba_type == B_AT_FN_STR);

	arg = (struct bt_arg*)ba->ba_value;
	assert(arg != NULL);

	index = SLIST_NEXT(arg, ba_next);
	if (index != NULL) {
		/* Should have only 1 optional argument. */
		assert(SLIST_NEXT(index, ba_next) == NULL);
		len = MINIMUM(ba2long(index, dtev) + 1, STRLEN);
	}

	/* All negative lengths behave the same as a zero length. */
	if (len < 1)
		return ba_new("", B_AT_STR);

	strlcpy(buf, ba2str(arg, dtev), len);
	return ba_new(buf, B_AT_STR);
}

/*
 * Expression test:	{ if (expr) stmt; }
 */
bool
stmt_test(struct bt_stmt *bs, struct dt_evt *dtev)
{
	struct bt_arg *ba;

	if (bs == NULL)
		return true;

	ba = SLIST_FIRST(&bs->bs_args);

	return baexpr2long(ba, dtev) != 0;
}

/*
 * Print time: 		{ time("%H:%M:%S"); }
 */
void
stmt_time(struct bt_stmt *bs, struct dt_evt *dtev)
{
	struct bt_arg *ba = SLIST_FIRST(&bs->bs_args);
	time_t time;
	struct tm *tm;
	char buf[64];

	assert(bs->bs_var == NULL);
	assert(ba->ba_type == B_AT_STR);
	assert(strlen(ba2str(ba, dtev)) < (sizeof(buf) - 1));

	time = builtin_gettime(dtev);
	tm = localtime(&time);
	strftime(buf, sizeof(buf), ba2str(ba, dtev), tm);
	printf("%s", buf);
}

/*
 * Set entries to 0:	{ zero(@map); }
 */
void
stmt_zero(struct bt_stmt *bs)
{
	struct bt_arg *ba = SLIST_FIRST(&bs->bs_args);
	struct bt_var *bv = ba->ba_value;
	struct map *map;

	assert(bs->bs_var == NULL);
	assert(ba->ba_type == B_AT_VAR);

	map = (struct map *)bv->bv_value;
	if (map == NULL)
		return;

	if (bv->bv_type != B_VT_MAP && bv->bv_type != B_VT_HIST)
		errx(1, "invalid variable type for zero(%s)", ba_name(ba));

	map_zero(map);

	debug("map=%p '%s' zero\n", map, bv_name(bv));
}

struct bt_arg *
ba_read(struct bt_arg *ba)
{
	struct bt_var *bv = ba->ba_value;

	assert(ba->ba_type == B_AT_VAR);
	debug("bv=%p read '%s' (%p)\n", bv, bv_name(bv), bv->bv_value);

	/* Handle map/hist access after clear(). */
	if (bv->bv_value == NULL)
		return &g_nullba;

	return bv->bv_value;
}

// XXX
extern struct bt_arg	*ba_append(struct bt_arg *, struct bt_arg *);

/*
 * Return a new argument that doesn't depend on `dtev'.  This is used
 * when storing values in variables, maps, etc.
 */
struct bt_arg *
baeval(struct bt_arg *bval, struct dt_evt *dtev)
{
	struct bt_arg *ba, *bh = NULL;

	switch (bval->ba_type) {
	case B_AT_VAR:
		ba = baeval(ba_read(bval), NULL);
		break;
	case B_AT_LONG:
	case B_AT_BI_PID:
	case B_AT_BI_TID:
	case B_AT_BI_CPU:
	case B_AT_BI_NSECS:
	case B_AT_BI_ARG0 ... B_AT_BI_ARG9:
	case B_AT_BI_RETVAL:
	case B_AT_OP_PLUS ... B_AT_OP_LOR:
		ba = ba_new(ba2long(bval, dtev), B_AT_LONG);
		break;
	case B_AT_STR:
	case B_AT_BI_COMM:
	case B_AT_BI_KSTACK:
	case B_AT_BI_USTACK:
	case B_AT_BI_PROBE:
	case B_AT_FN_STR:
		ba = ba_new(ba2str(bval, dtev), B_AT_STR);
		break;
	case B_AT_TUPLE:
		ba = bval->ba_value;
		do {
			bh = ba_append(bh, baeval(ba, dtev));
		} while ((ba = SLIST_NEXT(ba, ba_next)) != NULL);
		ba = ba_new(bh, B_AT_TUPLE);
		break;
	default:
		xabort("no eval support for type %d", bval->ba_type);
	}

	return ba;
}

/*
 * Return a string of coma-separated values
 */
const char *
ba2hash(struct bt_arg *ba, struct dt_evt *dtev)
{
	static char buf[KLEN];
	char *hash;
	int l, len;

	buf[0] = '\0';
	l = snprintf(buf, sizeof(buf), "%s", ba2str(ba, dtev));
	if (l < 0 || (size_t)l > sizeof(buf)) {
		warn("string too long %d > %lu", l, sizeof(buf));
		return buf;
	}

	len = 0;
	while ((ba = SLIST_NEXT(ba, ba_next)) != NULL) {
		len += l;
		hash = buf + len;

		l = snprintf(hash, sizeof(buf) - len, ", %s", ba2str(ba, dtev));
		if (l < 0 || (size_t)l > (sizeof(buf) - len)) {
			warn("hash too long %d > %lu", l + len, sizeof(buf));
			break;
		}
	}

	return buf;
}

static unsigned long
next_pow2(unsigned long x)
{
	size_t i;

	x--;
	for (i = 0; i < (sizeof(x)  * 8) - 1; i++)
		x |= (x >> 1);

	return x + 1;
}

/*
 * Return the ceiling value the interval holding `ba' or NULL if it is
 * out of the (min, max) values.
 */
const char *
ba2bucket(struct bt_arg *ba, struct bt_arg *brange, struct dt_evt *dtev,
    long *pstep)
{
	static char buf[KLEN];
	long val, bucket;
	int l;

	val = ba2long(ba, dtev);
	if (brange == NULL)
		bucket = next_pow2(val);
	else {
		long min, max, step;

		assert(brange->ba_type == B_AT_LONG);
		min = ba2long(brange, NULL);

		brange = SLIST_NEXT(brange, ba_next);
		assert(brange->ba_type == B_AT_LONG);
		max = ba2long(brange, NULL);

		if ((val < min) || (val > max))
			return NULL;

		brange = SLIST_NEXT(brange, ba_next);
		assert(brange->ba_type == B_AT_LONG);
		step = ba2long(brange, NULL);

		bucket = ((val / step) + 1) * step;
		*pstep = step;
	}

	buf[0] = '\0';
	l = snprintf(buf, sizeof(buf), "%lu", bucket);
	if (l < 0 || (size_t)l > sizeof(buf)) {
		warn("string too long %d > %lu", l, sizeof(buf));
		return buf;
	}

	return buf;
}

/*
 * Evaluate the operation encoded in `ba' and return its result.
 */
long
baexpr2long(struct bt_arg *ba, struct dt_evt *dtev)
{
	static long recursions;
	struct bt_arg *lhs, *rhs;
	long lval, rval, result;

	if (++recursions >= __MAXOPERANDS)
		errx(1, "too many operands (>%d) in expression", __MAXOPERANDS);

	lhs = ba->ba_value;
	rhs = SLIST_NEXT(lhs, ba_next);

	/*
	 * String comparison also use '==' and '!='.
	 */
	if (lhs->ba_type == B_AT_STR ||
	    (rhs != NULL && rhs->ba_type == B_AT_STR)) {
	    	char lstr[STRLEN], rstr[STRLEN];

		strlcpy(lstr, ba2str(lhs, dtev), sizeof(lstr));
		strlcpy(rstr, ba2str(rhs, dtev), sizeof(rstr));

	    	result = strncmp(lstr, rstr, STRLEN) == 0;

		switch (ba->ba_type) {
		case B_AT_OP_EQ:
			break;
		case B_AT_OP_NE:
	    		result = !result;
			break;
		default:
			warnx("operation '%d' unsupported on strings",
			    ba->ba_type);
			result = 1;
		}

		debug("ba=%p eval '(%s %s %s) = %d'\n", ba, lstr, ba_name(ba),
		   rstr, result);

		goto out;
	}

	lval = ba2long(lhs, dtev);
	if (rhs == NULL) {
		rval = 0;
	} else {
		assert(SLIST_NEXT(rhs, ba_next) == NULL);
		rval = ba2long(rhs, dtev);
	}

	switch (ba->ba_type) {
	case B_AT_OP_PLUS:
		result = lval + rval;
		break;
	case B_AT_OP_MINUS:
		result = lval - rval;
		break;
	case B_AT_OP_MULT:
		result = lval * rval;
		break;
	case B_AT_OP_DIVIDE:
		result = lval / rval;
		break;
	case B_AT_OP_MODULO:
		result = lval % rval;
		break;
	case B_AT_OP_BAND:
		result = lval & rval;
		break;
	case B_AT_OP_XOR:
		result = lval ^ rval;
		break;
	case B_AT_OP_BOR:
		result = lval | rval;
		break;
	case B_AT_OP_EQ:
		result = (lval == rval);
		break;
	case B_AT_OP_NE:
		result = (lval != rval);
		break;
	case B_AT_OP_LE:
		result = (lval <= rval);
		break;
	case B_AT_OP_LT:
		result = (lval < rval);
		break;
	case B_AT_OP_GE:
		result = (lval >= rval);
		break;
	case B_AT_OP_GT:
		result = (lval > rval);
		break;
	case B_AT_OP_LAND:
		result = (lval && rval);
		break;
	case B_AT_OP_LOR:
		result = (lval || rval);
		break;
	default:
		xabort("unsupported operation %d", ba->ba_type);
	}

	debug("ba=%p eval '(%ld %s %ld) = %d'\n", ba, lval, ba_name(ba),
	   rval, result);

out:
	--recursions;

	return result;
}

const char *
ba_name(struct bt_arg *ba)
{
	switch (ba->ba_type) {
	case B_AT_STR:
		return (const char *)ba->ba_value;
	case B_AT_LONG:
		return ba2str(ba, NULL);
	case B_AT_NIL:
		return "0";
	case B_AT_VAR:
	case B_AT_MAP:
	case B_AT_HIST:
		break;
	case B_AT_BI_PID:
		return "pid";
	case B_AT_BI_TID:
		return "tid";
	case B_AT_BI_COMM:
		return "comm";
	case B_AT_BI_CPU:
		return "cpu";
	case B_AT_BI_NSECS:
		return "nsecs";
	case B_AT_BI_KSTACK:
		return "kstack";
	case B_AT_BI_USTACK:
		return "ustack";
	case B_AT_BI_ARG0:
		return "arg0";
	case B_AT_BI_ARG1:
		return "arg1";
	case B_AT_BI_ARG2:
		return "arg2";
	case B_AT_BI_ARG3:
		return "arg3";
	case B_AT_BI_ARG4:
		return "arg4";
	case B_AT_BI_ARG5:
		return "arg5";
	case B_AT_BI_ARG6:
		return "arg6";
	case B_AT_BI_ARG7:
		return "arg7";
	case B_AT_BI_ARG8:
		return "arg8";
	case B_AT_BI_ARG9:
		return "arg9";
	case B_AT_BI_ARGS:
		return "args";
	case B_AT_BI_RETVAL:
		return "retval";
	case B_AT_BI_PROBE:
		return "probe";
	case B_AT_FN_STR:
		return "str";
	case B_AT_OP_PLUS:
		return "+";
	case B_AT_OP_MINUS:
		return "-";
	case B_AT_OP_MULT:
		return "*";
	case B_AT_OP_DIVIDE:
		return "/";
	case B_AT_OP_MODULO:
		return "%";
	case B_AT_OP_BAND:
		return "&";
	case B_AT_OP_XOR:
		return "^";
	case B_AT_OP_BOR:
		return "|";
	case B_AT_OP_EQ:
		return "==";
	case B_AT_OP_NE:
		return "!=";
	case B_AT_OP_LE:
		return "<=";
	case B_AT_OP_LT:
		return "<";
	case B_AT_OP_GE:
		return ">=";
	case B_AT_OP_GT:
		return ">";
	case B_AT_OP_LAND:
		return "&&";
	case B_AT_OP_LOR:
		return "||";
	default:
		xabort("unsupported type %d", ba->ba_type);
	}

	assert(ba->ba_type == B_AT_VAR || ba->ba_type == B_AT_MAP ||
	    ba->ba_type == B_AT_HIST);

	static char buf[64];
	size_t sz;
	int l;

	buf[0] = '@';
	buf[1] = '\0';
	sz = sizeof(buf) - 1;
	l = snprintf(buf+1, sz, "%s", bv_name(ba->ba_value));
	if (l < 0 || (size_t)l > sz) {
		warn("string too long %d > %zu", l, sz);
		return buf;
	}

	if (ba->ba_type == B_AT_MAP) {
		sz -= l;
		l = snprintf(buf+1+l, sz, "[%s]", ba_name(ba->ba_key));
		if (l < 0 || (size_t)l > sz) {
			warn("string too long %d > %zu", l, sz);
			return buf;
		}
	}

	return buf;
}

/*
 * Return the representation of `ba' as long.
 */
long
ba2long(struct bt_arg *ba, struct dt_evt *dtev)
{
	struct bt_var *bv;
	long val;

	switch (ba->ba_type) {
	case B_AT_STR:
		val = (*ba2str(ba, dtev) == '\0') ? 0 : 1;
		break;
	case B_AT_LONG:
		val = (long)ba->ba_value;
		break;
	case B_AT_VAR:
		ba = ba_read(ba);
		val = (long)ba->ba_value;
		break;
	case B_AT_MAP:
		bv = ba->ba_value;
		/* Uninitialized map */
		if (bv->bv_value == NULL)
			return 0;
		val = ba2long(map_get((struct map *)bv->bv_value,
		    ba2hash(ba->ba_key, dtev)), dtev);
		break;
	case B_AT_NIL:
		val = 0L;
		break;
	case B_AT_BI_PID:
		val = dtev->dtev_pid;
		break;
	case B_AT_BI_TID:
		val = dtev->dtev_tid;
		break;
	case B_AT_BI_CPU:
		val = dtev->dtev_cpu;
		break;
	case B_AT_BI_NSECS:
		val = builtin_nsecs(dtev);
		break;
	case B_AT_BI_ARG0 ... B_AT_BI_ARG9:
		val = dtev->dtev_args[ba->ba_type - B_AT_BI_ARG0];
		break;
	case B_AT_BI_RETVAL:
		val = dtev->dtev_retval[0];
		break;
	case B_AT_BI_PROBE:
		val = dtev->dtev_pbn;
		break;
	case B_AT_OP_PLUS ... B_AT_OP_LOR:
		val = baexpr2long(ba, dtev);
		break;
	default:
		xabort("no long conversion for type %d", ba->ba_type);
	}

	return  val;
}

/*
 * Return the representation of `ba' as string.
 */
const char *
ba2str(struct bt_arg *ba, struct dt_evt *dtev)
{
	static char buf[STRLEN];
	struct bt_var *bv;
	struct dtioc_probe_info *dtpi;
	unsigned long idx;
	const char *str;

	buf[0] = '\0';
	switch (ba->ba_type) {
	case B_AT_STR:
		str = (const char *)ba->ba_value;
		break;
	case B_AT_LONG:
		snprintf(buf, sizeof(buf), "%ld",(long)ba->ba_value);
		str = buf;
		break;
	case B_AT_TUPLE:
		snprintf(buf, sizeof(buf), "(%s)", ba2hash(ba->ba_value, dtev));
		str = buf;
		break;
	case B_AT_TMEMBER:
		idx = (unsigned long)ba->ba_key;
		bv = ba->ba_value;
		/* Uninitialized tuple */
		if (bv->bv_value == NULL) {
			str = buf;
			break;
		}
		ba = bv->bv_value;
		assert(ba->ba_type == B_AT_TUPLE);
		ba = ba->ba_value;
		while (ba != NULL && idx-- > 0) {
			ba = SLIST_NEXT(ba, ba_next);
		}
		str = ba2str(ba, dtev);
		break;
	case B_AT_NIL:
		str = "";
		break;
	case B_AT_BI_KSTACK:
		str = builtin_stack(dtev, 1);
		break;
	case B_AT_BI_USTACK:
		str = builtin_stack(dtev, 0);
		break;
	case B_AT_BI_COMM:
		str = dtev->dtev_comm;
		break;
	case B_AT_BI_CPU:
		snprintf(buf, sizeof(buf), "%u", dtev->dtev_cpu);
		str = buf;
		break;
	case B_AT_BI_PID:
		snprintf(buf, sizeof(buf), "%d", dtev->dtev_pid);
		str = buf;
		break;
	case B_AT_BI_TID:
		snprintf(buf, sizeof(buf), "%d", dtev->dtev_tid);
		str = buf;
		break;
	case B_AT_BI_NSECS:
		snprintf(buf, sizeof(buf), "%llu", builtin_nsecs(dtev));
		str = buf;
		break;
	case B_AT_BI_ARG0 ... B_AT_BI_ARG9:
		str = builtin_arg(dtev, ba->ba_type);
		break;
	case B_AT_BI_RETVAL:
		snprintf(buf, sizeof(buf), "%ld", (long)dtev->dtev_retval[0]);
		str = buf;
		break;
	case B_AT_BI_PROBE:
		if (dtev->dtev_pbn == EVENT_BEGIN) {
			str = "BEGIN";
			break;
		} else if (dtev->dtev_pbn == EVENT_END) {
			str = "END";
			break;
		}
		dtpi = &dt_dtpis[dtev->dtev_pbn - 1];
		if (dtpi != NULL)
			snprintf(buf, sizeof(buf), "%s:%s:%s",
			    dtpi->dtpi_prov, dtpi_func(dtpi), dtpi->dtpi_name);
		else
			snprintf(buf, sizeof(buf), "%u", dtev->dtev_pbn);
		str = buf;
		break;
	case B_AT_MAP:
		bv = ba->ba_value;
		/* Uninitialized map */
		if (bv->bv_value == NULL) {
			str = buf;
			break;
		}
		str = ba2str(map_get((struct map *)bv->bv_value,
		    ba2hash(ba->ba_key, dtev)), dtev);
		break;
	case B_AT_VAR:
		str = ba2str(ba_read(ba), dtev);
		break;
	case B_AT_FN_STR:
		str = (const char*)(fn_str(ba, dtev, buf))->ba_value;
		break;
	case B_AT_OP_PLUS ... B_AT_OP_LOR:
		snprintf(buf, sizeof(buf), "%ld", ba2long(ba, dtev));
		str = buf;
		break;
	case B_AT_MF_COUNT:
	case B_AT_MF_MAX:
	case B_AT_MF_MIN:
	case B_AT_MF_SUM:
		assert(0);
		break;
	default:
		xabort("no string conversion for type %d", ba->ba_type);
	}

	return str;
}

int
ba2flags(struct bt_arg *ba)
{
	int flags = 0;

	assert(ba->ba_type != B_AT_MAP);
	assert(ba->ba_type != B_AT_TUPLE);

	switch (ba->ba_type) {
	case B_AT_STR:
	case B_AT_LONG:
	case B_AT_TMEMBER:
	case B_AT_VAR:
	case B_AT_HIST:
	case B_AT_NIL:
		break;
	case B_AT_BI_KSTACK:
		flags |= DTEVT_KSTACK;
		break;
	case B_AT_BI_USTACK:
		flags |= DTEVT_USTACK;
		break;
	case B_AT_BI_COMM:
		flags |= DTEVT_EXECNAME;
		break;
	case B_AT_BI_CPU:
	case B_AT_BI_PID:
	case B_AT_BI_TID:
	case B_AT_BI_NSECS:
		break;
	case B_AT_BI_ARG0 ... B_AT_BI_ARG9:
		flags |= DTEVT_FUNCARGS;
		break;
	case B_AT_BI_RETVAL:
	case B_AT_BI_PROBE:
		break;
	case B_AT_MF_COUNT:
	case B_AT_MF_MAX:
	case B_AT_MF_MIN:
	case B_AT_MF_SUM:
	case B_AT_FN_STR:
		break;
	case B_AT_OP_PLUS ... B_AT_OP_LOR:
		flags |= ba2dtflags(ba->ba_value);
		break;
	default:
		xabort("invalid argument type %d", ba->ba_type);
	}

	return flags;
}

/*
 * Return dt(4) flags indicating which data should be recorded by the
 * kernel, if any, for a given `ba'.
 */
int
ba2dtflags(struct bt_arg *ba)
{
	static long recursions;
	struct bt_arg *bval;
	int flags = 0;

	if (++recursions >= __MAXOPERANDS)
		errx(1, "too many operands (>%d) in expression", __MAXOPERANDS);

	do {
		if (ba->ba_type == B_AT_MAP)
			flags |= ba2flags(ba->ba_key);
		else if (ba->ba_type == B_AT_TUPLE) {
			bval = ba->ba_value;
			do {
				flags |= ba2flags(bval);
			} while ((bval = SLIST_NEXT(bval, ba_next)) != NULL);
		} else
			flags |= ba2flags(ba);

	} while ((ba = SLIST_NEXT(ba, ba_next)) != NULL);

	--recursions;

	return flags;
}

long
bacmp(struct bt_arg *a, struct bt_arg *b)
{
	char astr[STRLEN];
	long val;

	if (a->ba_type != b->ba_type)
		return a->ba_type - b->ba_type;

	switch (a->ba_type) {
	case B_AT_LONG:
		return ba2long(a, NULL) - ba2long(b, NULL);
	case B_AT_STR:
		strlcpy(astr, ba2str(a, NULL), sizeof(astr));
		return strcmp(astr, ba2str(b, NULL));
	case B_AT_TUPLE:
		/* Compare two lists of arguments one by one. */
		a = a->ba_value;
		b = b->ba_value;
		do {
			val = bacmp(a, b);
			if (val != 0)
				break;

			a = SLIST_NEXT(a, ba_next);
			b = SLIST_NEXT(b, ba_next);
			if (a == NULL && b != NULL)
				val = -1;
			else if (a != NULL && b == NULL)
				val = 1;
		} while (a != NULL && b != NULL);

		return val;
	default:
		xabort("no compare support for type %d", a->ba_type);
	}
}

__dead void
xabort(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fprintf(stderr, "\n");
	abort();
}

void
debug(const char *fmt, ...)
{
	va_list ap;

	if (verbose < 2)
		return;

	fprintf(stderr, "debug: ");

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void
debugx(const char *fmt, ...)
{
	va_list ap;

	if (verbose < 2)
		return;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void
debug_dump_term(struct bt_arg *ba)
{
	switch (ba->ba_type) {
	case B_AT_LONG:
		debugx("%s", ba2str(ba, NULL));
		break;
	case B_AT_OP_PLUS ... B_AT_OP_LOR:
		debug_dump_expr(ba);
		break;
	default:
		debugx("%s", ba_name(ba));
	}
}

void
debug_dump_expr(struct bt_arg *ba)
{
	struct bt_arg *lhs, *rhs;

	lhs = ba->ba_value;
	rhs = SLIST_NEXT(lhs, ba_next);

	/* Left */
	debug_dump_term(lhs);

	/* Right */
	if (rhs != NULL) {
		debugx(" %s ", ba_name(ba));
		debug_dump_term(rhs);
	} else {
		if (ba->ba_type != B_AT_OP_NE)
			debugx(" %s NULL", ba_name(ba));
	}
}

void
debug_dump_filter(struct bt_rule *r)
{
	struct bt_stmt *bs;

	if (verbose < 2)
		return;

	if (r->br_filter == NULL) {
		debugx("\n");
		return;
	}

	bs = r->br_filter->bf_condition;

	debugx(" /");
	debug_dump_expr(SLIST_FIRST(&bs->bs_args));
	debugx("/\n");
}
