/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/eventhandler.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/sched.h>
#include <sys/taskqueue.h>
#include <sys/vnode.h>
#include <sys/sbuf.h>
#ifdef GJ_MEMDEBUG
#include <sys/stack.h>
#include <sys/kdb.h>
#endif
#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <geom/geom.h>

#include <geom/journal/g_journal.h>

FEATURE(geom_journal, "GEOM journaling support");

/*
 * On-disk journal format:
 *
 * JH - Journal header
 * RH - Record header
 *
 * %%%%%% ****** +------+ +------+     ****** +------+     %%%%%%
 * % JH % * RH * | Data | | Data | ... * RH * | Data | ... % JH % ...
 * %%%%%% ****** +------+ +------+     ****** +------+     %%%%%%
 *
 */

CTASSERT(sizeof(struct g_journal_header) <= 512);
CTASSERT(sizeof(struct g_journal_record_header) <= 512);

static MALLOC_DEFINE(M_JOURNAL, "journal_data", "GEOM_JOURNAL Data");
static struct mtx g_journal_cache_mtx;
MTX_SYSINIT(g_journal_cache, &g_journal_cache_mtx, "cache usage", MTX_DEF);

const struct g_journal_desc *g_journal_filesystems[] = {
	&g_journal_ufs,
	NULL
};

SYSCTL_DECL(_kern_geom);

int g_journal_debug = 0;
static u_int g_journal_switch_time = 10;
static u_int g_journal_force_switch = 70;
static u_int g_journal_parallel_flushes = 16;
static u_int g_journal_parallel_copies = 16;
static u_int g_journal_accept_immediately = 64;
static u_int g_journal_record_entries = GJ_RECORD_HEADER_NENTRIES;
static u_int g_journal_do_optimize = 1;

static SYSCTL_NODE(_kern_geom, OID_AUTO, journal, CTLFLAG_RW, 0,
    "GEOM_JOURNAL stuff");
SYSCTL_INT(_kern_geom_journal, OID_AUTO, debug, CTLFLAG_RWTUN, &g_journal_debug, 0,
    "Debug level");
SYSCTL_UINT(_kern_geom_journal, OID_AUTO, switch_time, CTLFLAG_RW,
    &g_journal_switch_time, 0, "Switch journals every N seconds");
SYSCTL_UINT(_kern_geom_journal, OID_AUTO, force_switch, CTLFLAG_RW,
    &g_journal_force_switch, 0, "Force switch when journal is N% full");
SYSCTL_UINT(_kern_geom_journal, OID_AUTO, parallel_flushes, CTLFLAG_RW,
    &g_journal_parallel_flushes, 0,
    "Number of flush I/O requests to send in parallel");
SYSCTL_UINT(_kern_geom_journal, OID_AUTO, accept_immediately, CTLFLAG_RW,
    &g_journal_accept_immediately, 0,
    "Number of I/O requests accepted immediately");
SYSCTL_UINT(_kern_geom_journal, OID_AUTO, parallel_copies, CTLFLAG_RW,
    &g_journal_parallel_copies, 0,
    "Number of copy I/O requests to send in parallel");
static int
g_journal_record_entries_sysctl(SYSCTL_HANDLER_ARGS)
{
	u_int entries;
	int error;

	entries = g_journal_record_entries;
	error = sysctl_handle_int(oidp, &entries, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (entries < 1 || entries > GJ_RECORD_HEADER_NENTRIES)
		return (EINVAL);
	g_journal_record_entries = entries;
	return (0);
}
SYSCTL_PROC(_kern_geom_journal, OID_AUTO, record_entries,
    CTLTYPE_UINT | CTLFLAG_RW, NULL, 0, g_journal_record_entries_sysctl, "I",
    "Maximum number of entires in one journal record");
SYSCTL_UINT(_kern_geom_journal, OID_AUTO, optimize, CTLFLAG_RW,
    &g_journal_do_optimize, 0, "Try to combine bios on flush and copy");

static u_long g_journal_cache_used = 0;
static u_long g_journal_cache_limit = 64 * 1024 * 1024;
static u_int g_journal_cache_divisor = 2;
static u_int g_journal_cache_switch = 90;
static u_int g_journal_cache_misses = 0;
static u_int g_journal_cache_alloc_failures = 0;
static u_long g_journal_cache_low = 0;

static SYSCTL_NODE(_kern_geom_journal, OID_AUTO, cache, CTLFLAG_RW, 0,
    "GEOM_JOURNAL cache");
SYSCTL_ULONG(_kern_geom_journal_cache, OID_AUTO, used, CTLFLAG_RD,
    &g_journal_cache_used, 0, "Number of allocated bytes");
static int
g_journal_cache_limit_sysctl(SYSCTL_HANDLER_ARGS)
{
	u_long limit;
	int error;

	limit = g_journal_cache_limit;
	error = sysctl_handle_long(oidp, &limit, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	g_journal_cache_limit = limit;
	g_journal_cache_low = (limit / 100) * g_journal_cache_switch;
	return (0);
}
SYSCTL_PROC(_kern_geom_journal_cache, OID_AUTO, limit,
    CTLTYPE_ULONG | CTLFLAG_RWTUN, NULL, 0, g_journal_cache_limit_sysctl, "I",
    "Maximum number of allocated bytes");
SYSCTL_UINT(_kern_geom_journal_cache, OID_AUTO, divisor, CTLFLAG_RDTUN,
    &g_journal_cache_divisor, 0,
    "(kmem_size / kern.geom.journal.cache.divisor) == cache size");
static int
g_journal_cache_switch_sysctl(SYSCTL_HANDLER_ARGS)
{
	u_int cswitch;
	int error;

	cswitch = g_journal_cache_switch;
	error = sysctl_handle_int(oidp, &cswitch, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (cswitch > 100)
		return (EINVAL);
	g_journal_cache_switch = cswitch;
	g_journal_cache_low = (g_journal_cache_limit / 100) * cswitch;
	return (0);
}
SYSCTL_PROC(_kern_geom_journal_cache, OID_AUTO, switch,
    CTLTYPE_UINT | CTLFLAG_RW, NULL, 0, g_journal_cache_switch_sysctl, "I",
    "Force switch when we hit this percent of cache use");
SYSCTL_UINT(_kern_geom_journal_cache, OID_AUTO, misses, CTLFLAG_RW,
    &g_journal_cache_misses, 0, "Number of cache misses");
SYSCTL_UINT(_kern_geom_journal_cache, OID_AUTO, alloc_failures, CTLFLAG_RW,
    &g_journal_cache_alloc_failures, 0, "Memory allocation failures");

static u_long g_journal_stats_bytes_skipped = 0;
static u_long g_journal_stats_combined_ios = 0;
static u_long g_journal_stats_switches = 0;
static u_long g_journal_stats_wait_for_copy = 0;
static u_long g_journal_stats_journal_full = 0;
static u_long g_journal_stats_low_mem = 0;

static SYSCTL_NODE(_kern_geom_journal, OID_AUTO, stats, CTLFLAG_RW, 0,
    "GEOM_JOURNAL statistics");
SYSCTL_ULONG(_kern_geom_journal_stats, OID_AUTO, skipped_bytes, CTLFLAG_RW,
    &g_journal_stats_bytes_skipped, 0, "Number of skipped bytes");
SYSCTL_ULONG(_kern_geom_journal_stats, OID_AUTO, combined_ios, CTLFLAG_RW,
    &g_journal_stats_combined_ios, 0, "Number of combined I/O requests");
SYSCTL_ULONG(_kern_geom_journal_stats, OID_AUTO, switches, CTLFLAG_RW,
    &g_journal_stats_switches, 0, "Number of journal switches");
SYSCTL_ULONG(_kern_geom_journal_stats, OID_AUTO, wait_for_copy, CTLFLAG_RW,
    &g_journal_stats_wait_for_copy, 0, "Wait for journal copy on switch");
SYSCTL_ULONG(_kern_geom_journal_stats, OID_AUTO, journal_full, CTLFLAG_RW,
    &g_journal_stats_journal_full, 0,
    "Number of times journal was almost full.");
SYSCTL_ULONG(_kern_geom_journal_stats, OID_AUTO, low_mem, CTLFLAG_RW,
    &g_journal_stats_low_mem, 0, "Number of times low_mem hook was called.");

static g_taste_t g_journal_taste;
static g_ctl_req_t g_journal_config;
static g_dumpconf_t g_journal_dumpconf;
static g_init_t g_journal_init;
static g_fini_t g_journal_fini;

struct g_class g_journal_class = {
	.name = G_JOURNAL_CLASS_NAME,
	.version = G_VERSION,
	.taste = g_journal_taste,
	.ctlreq = g_journal_config,
	.dumpconf = g_journal_dumpconf,
	.init = g_journal_init,
	.fini = g_journal_fini
};

static int g_journal_destroy(struct g_journal_softc *sc);
static void g_journal_metadata_update(struct g_journal_softc *sc);
static void g_journal_start_switcher(struct g_class *mp);
static void g_journal_stop_switcher(void);
static void g_journal_switch_wait(struct g_journal_softc *sc);

#define	GJ_SWITCHER_WORKING	0
#define	GJ_SWITCHER_DIE		1
#define	GJ_SWITCHER_DIED	2
static struct proc *g_journal_switcher_proc = NULL;
static int g_journal_switcher_state = GJ_SWITCHER_WORKING;
static int g_journal_switcher_wokenup = 0;
static int g_journal_sync_requested = 0;

#ifdef GJ_MEMDEBUG
struct meminfo {
	size_t		mi_size;
	struct stack	mi_stack;
};
#endif

/*
 * We use our own malloc/realloc/free funtions, so we can collect statistics
 * and force journal switch when we're running out of cache.
 */
static void *
gj_malloc(size_t size, int flags)
{
	void *p;
#ifdef GJ_MEMDEBUG
	struct meminfo *mi;
#endif

	mtx_lock(&g_journal_cache_mtx);
	if (g_journal_cache_limit > 0 && !g_journal_switcher_wokenup &&
	    g_journal_cache_used + size > g_journal_cache_low) {
		GJ_DEBUG(1, "No cache, waking up the switcher.");
		g_journal_switcher_wokenup = 1;
		wakeup(&g_journal_switcher_state);
	}
	if ((flags & M_NOWAIT) && g_journal_cache_limit > 0 &&
	    g_journal_cache_used + size > g_journal_cache_limit) {
		mtx_unlock(&g_journal_cache_mtx);
		g_journal_cache_alloc_failures++;
		return (NULL);
	}
	g_journal_cache_used += size;
	mtx_unlock(&g_journal_cache_mtx);
	flags &= ~M_NOWAIT;
#ifndef GJ_MEMDEBUG
	p = malloc(size, M_JOURNAL, flags | M_WAITOK);
#else
	mi = malloc(sizeof(*mi) + size, M_JOURNAL, flags | M_WAITOK);
	p = (u_char *)mi + sizeof(*mi);
	mi->mi_size = size;
	stack_save(&mi->mi_stack);
#endif
	return (p);
}

static void
gj_free(void *p, size_t size)
{
#ifdef GJ_MEMDEBUG
	struct meminfo *mi;
#endif

	KASSERT(p != NULL, ("p=NULL"));
	KASSERT(size > 0, ("size=0"));
	mtx_lock(&g_journal_cache_mtx);
	KASSERT(g_journal_cache_used >= size, ("Freeing too much?"));
	g_journal_cache_used -= size;
	mtx_unlock(&g_journal_cache_mtx);
#ifdef GJ_MEMDEBUG
	mi = p = (void *)((u_char *)p - sizeof(*mi));
	if (mi->mi_size != size) {
		printf("GJOURNAL: Size mismatch! %zu != %zu\n", size,
		    mi->mi_size);
		printf("GJOURNAL: Alloc backtrace:\n");
		stack_print(&mi->mi_stack);
		printf("GJOURNAL: Free backtrace:\n");
		kdb_backtrace();
	}
#endif
	free(p, M_JOURNAL);
}

static void *
gj_realloc(void *p, size_t size, size_t oldsize)
{
	void *np;

#ifndef GJ_MEMDEBUG
	mtx_lock(&g_journal_cache_mtx);
	g_journal_cache_used -= oldsize;
	g_journal_cache_used += size;
	mtx_unlock(&g_journal_cache_mtx);
	np = realloc(p, size, M_JOURNAL, M_WAITOK);
#else
	np = gj_malloc(size, M_WAITOK);
	bcopy(p, np, MIN(oldsize, size));
	gj_free(p, oldsize);
#endif
	return (np);
}

static void
g_journal_check_overflow(struct g_journal_softc *sc)
{
	off_t length, used;

	if ((sc->sc_active.jj_offset < sc->sc_inactive.jj_offset &&
	     sc->sc_journal_offset >= sc->sc_inactive.jj_offset) ||
	    (sc->sc_active.jj_offset > sc->sc_inactive.jj_offset &&
	     sc->sc_journal_offset >= sc->sc_inactive.jj_offset &&
	     sc->sc_journal_offset < sc->sc_active.jj_offset)) {
		panic("Journal overflow "
		    "(id = %u joffset=%jd active=%jd inactive=%jd)",
		    (unsigned)sc->sc_id,
		    (intmax_t)sc->sc_journal_offset,
		    (intmax_t)sc->sc_active.jj_offset,
		    (intmax_t)sc->sc_inactive.jj_offset);
	}
	if (sc->sc_active.jj_offset < sc->sc_inactive.jj_offset) {
		length = sc->sc_inactive.jj_offset - sc->sc_active.jj_offset;
		used = sc->sc_journal_offset - sc->sc_active.jj_offset;
	} else {
		length = sc->sc_jend - sc->sc_active.jj_offset;
		length += sc->sc_inactive.jj_offset - sc->sc_jstart;
		if (sc->sc_journal_offset >= sc->sc_active.jj_offset)
			used = sc->sc_journal_offset - sc->sc_active.jj_offset;
		else {
			used = sc->sc_jend - sc->sc_active.jj_offset;
			used += sc->sc_journal_offset - sc->sc_jstart;
		}
	}
	/* Already woken up? */
	if (g_journal_switcher_wokenup)
		return;
	/*
	 * If the active journal takes more than g_journal_force_switch precent
	 * of free journal space, we force journal switch.
	 */
	KASSERT(length > 0,
	    ("length=%jd used=%jd active=%jd inactive=%jd joffset=%jd",
	    (intmax_t)length, (intmax_t)used,
	    (intmax_t)sc->sc_active.jj_offset,
	    (intmax_t)sc->sc_inactive.jj_offset,
	    (intmax_t)sc->sc_journal_offset));
	if ((used * 100) / length > g_journal_force_switch) {
		g_journal_stats_journal_full++;
		GJ_DEBUG(1, "Journal %s %jd%% full, forcing journal switch.",
		    sc->sc_name, (used * 100) / length);
		mtx_lock(&g_journal_cache_mtx);
		g_journal_switcher_wokenup = 1;
		wakeup(&g_journal_switcher_state);
		mtx_unlock(&g_journal_cache_mtx);
	}
}

static void
g_journal_orphan(struct g_consumer *cp)
{
	struct g_journal_softc *sc;
	char name[256];
	int error;

	g_topology_assert();
	sc = cp->geom->softc;
	strlcpy(name, cp->provider->name, sizeof(name));
	GJ_DEBUG(0, "Lost provider %s.", name);
	if (sc == NULL)
		return;
	error = g_journal_destroy(sc);
	if (error == 0)
		GJ_DEBUG(0, "Journal %s destroyed.", name);
	else {
		GJ_DEBUG(0, "Cannot destroy journal %s (error=%d). "
		    "Destroy it manually after last close.", sc->sc_name,
		    error);
	}
}

static int
g_journal_access(struct g_provider *pp, int acr, int acw, int ace)
{
	struct g_journal_softc *sc;
	int dcr, dcw, dce;

	g_topology_assert();
	GJ_DEBUG(2, "Access request for %s: r%dw%de%d.", pp->name,
	    acr, acw, ace);

	dcr = pp->acr + acr;
	dcw = pp->acw + acw;
	dce = pp->ace + ace;

	sc = pp->geom->softc;
	if (sc == NULL || (sc->sc_flags & GJF_DEVICE_DESTROY)) {
		if (acr <= 0 && acw <= 0 && ace <= 0)
			return (0);
		else
			return (ENXIO);
	}
	if (pp->acw == 0 && dcw > 0) {
		GJ_DEBUG(1, "Marking %s as dirty.", sc->sc_name);
		sc->sc_flags &= ~GJF_DEVICE_CLEAN;
		g_topology_unlock();
		g_journal_metadata_update(sc);
		g_topology_lock();
	} /* else if (pp->acw == 0 && dcw > 0 && JEMPTY(sc)) {
		GJ_DEBUG(1, "Marking %s as clean.", sc->sc_name);
		sc->sc_flags |= GJF_DEVICE_CLEAN;
		g_topology_unlock();
		g_journal_metadata_update(sc);
		g_topology_lock();
	} */
	return (0);
}

static void
g_journal_header_encode(struct g_journal_header *hdr, u_char *data)
{

	bcopy(GJ_HEADER_MAGIC, data, sizeof(GJ_HEADER_MAGIC));
	data += sizeof(GJ_HEADER_MAGIC);
	le32enc(data, hdr->jh_journal_id);
	data += 4;
	le32enc(data, hdr->jh_journal_next_id);
}

static int
g_journal_header_decode(const u_char *data, struct g_journal_header *hdr)
{

	bcopy(data, hdr->jh_magic, sizeof(hdr->jh_magic));
	data += sizeof(hdr->jh_magic);
	if (bcmp(hdr->jh_magic, GJ_HEADER_MAGIC, sizeof(GJ_HEADER_MAGIC)) != 0)
		return (EINVAL);
	hdr->jh_journal_id = le32dec(data);
	data += 4;
	hdr->jh_journal_next_id = le32dec(data);
	return (0);
}

static void
g_journal_flush_cache(struct g_journal_softc *sc)
{
	struct bintime bt;
	int error;

	if (sc->sc_bio_flush == 0)
		return;
	GJ_TIMER_START(1, &bt);
	if (sc->sc_bio_flush & GJ_FLUSH_JOURNAL) {
		error = g_io_flush(sc->sc_jconsumer);
		GJ_DEBUG(error == 0 ? 2 : 0, "Flush cache of %s: error=%d.",
		    sc->sc_jconsumer->provider->name, error);
	}
	if (sc->sc_bio_flush & GJ_FLUSH_DATA) {
		/*
		 * TODO: This could be called in parallel with the
		 *       previous call.
		 */
		error = g_io_flush(sc->sc_dconsumer);
		GJ_DEBUG(error == 0 ? 2 : 0, "Flush cache of %s: error=%d.",
		    sc->sc_dconsumer->provider->name, error);
	}
	GJ_TIMER_STOP(1, &bt, "Cache flush time");
}

static int
g_journal_write_header(struct g_journal_softc *sc)
{
	struct g_journal_header hdr;
	struct g_consumer *cp;
	u_char *buf;
	int error;

	cp = sc->sc_jconsumer;
	buf = gj_malloc(cp->provider->sectorsize, M_WAITOK);

	strlcpy(hdr.jh_magic, GJ_HEADER_MAGIC, sizeof(hdr.jh_magic));
	hdr.jh_journal_id = sc->sc_journal_id;
	hdr.jh_journal_next_id = sc->sc_journal_next_id;
	g_journal_header_encode(&hdr, buf);
	error = g_write_data(cp, sc->sc_journal_offset, buf,
	    cp->provider->sectorsize);
	/* if (error == 0) */
	sc->sc_journal_offset += cp->provider->sectorsize;

	gj_free(buf, cp->provider->sectorsize);
	return (error);
}

/*
 * Every journal record has a header and data following it.
 * Functions below are used to decode the header before storing it to
 * little endian and to encode it after reading to system endianness.
 */
static void
g_journal_record_header_encode(struct g_journal_record_header *hdr,
    u_char *data)
{
	struct g_journal_entry *ent;
	u_int i;

	bcopy(GJ_RECORD_HEADER_MAGIC, data, sizeof(GJ_RECORD_HEADER_MAGIC));
	data += sizeof(GJ_RECORD_HEADER_MAGIC);
	le32enc(data, hdr->jrh_journal_id);
	data += 8;
	le16enc(data, hdr->jrh_nentries);
	data += 2;
	bcopy(hdr->jrh_sum, data, sizeof(hdr->jrh_sum));
	data += 8;
	for (i = 0; i < hdr->jrh_nentries; i++) {
		ent = &hdr->jrh_entries[i];
		le64enc(data, ent->je_joffset);
		data += 8;
		le64enc(data, ent->je_offset);
		data += 8;
		le64enc(data, ent->je_length);
		data += 8;
	}
}

static int
g_journal_record_header_decode(const u_char *data,
    struct g_journal_record_header *hdr)
{
	struct g_journal_entry *ent;
	u_int i;

	bcopy(data, hdr->jrh_magic, sizeof(hdr->jrh_magic));
	data += sizeof(hdr->jrh_magic);
	if (strcmp(hdr->jrh_magic, GJ_RECORD_HEADER_MAGIC) != 0)
		return (EINVAL);
	hdr->jrh_journal_id = le32dec(data);
	data += 8;
	hdr->jrh_nentries = le16dec(data);
	data += 2;
	if (hdr->jrh_nentries > GJ_RECORD_HEADER_NENTRIES)
		return (EINVAL);
	bcopy(data, hdr->jrh_sum, sizeof(hdr->jrh_sum));
	data += 8;
	for (i = 0; i < hdr->jrh_nentries; i++) {
		ent = &hdr->jrh_entries[i];
		ent->je_joffset = le64dec(data);
		data += 8;
		ent->je_offset = le64dec(data);
		data += 8;
		ent->je_length = le64dec(data);
		data += 8;
	}
	return (0);
}

/*
 * Function reads metadata from a provider (via the given consumer), decodes
 * it to system endianness and verifies its correctness.
 */
static int
g_journal_metadata_read(struct g_consumer *cp, struct g_journal_metadata *md)
{
	struct g_provider *pp;
	u_char *buf;
	int error;

	g_topology_assert();

	error = g_access(cp, 1, 0, 0);
	if (error != 0)
		return (error);
	pp = cp->provider;
	g_topology_unlock();
	/* Metadata is stored in last sector. */
	buf = g_read_data(cp, pp->mediasize - pp->sectorsize, pp->sectorsize,
	    &error);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (buf == NULL) {
		GJ_DEBUG(1, "Cannot read metadata from %s (error=%d).",
		    cp->provider->name, error);
		return (error);
	}

	/* Decode metadata. */
	error = journal_metadata_decode(buf, md);
	g_free(buf);
	/* Is this is gjournal provider at all? */
	if (strcmp(md->md_magic, G_JOURNAL_MAGIC) != 0)
		return (EINVAL);
	/*
	 * Are we able to handle this version of metadata?
	 * We only maintain backward compatibility.
	 */
	if (md->md_version > G_JOURNAL_VERSION) {
		GJ_DEBUG(0,
		    "Kernel module is too old to handle metadata from %s.",
		    cp->provider->name);
		return (EINVAL);
	}
	/* Is checksum correct? */
	if (error != 0) {
		GJ_DEBUG(0, "MD5 metadata hash mismatch for provider %s.",
		    cp->provider->name);
		return (error);
	}
	return (0);
}

/*
 * Two functions below are responsible for updating metadata.
 * Only metadata on the data provider is updated (we need to update
 * information about active journal in there).
 */
static void
g_journal_metadata_done(struct bio *bp)
{

	/*
	 * There is not much we can do on error except informing about it.
	 */
	if (bp->bio_error != 0) {
		GJ_LOGREQ(0, bp, "Cannot update metadata (error=%d).",
		    bp->bio_error);
	} else {
		GJ_LOGREQ(2, bp, "Metadata updated.");
	}
	gj_free(bp->bio_data, bp->bio_length);
	g_destroy_bio(bp);
}

static void
g_journal_metadata_update(struct g_journal_softc *sc)
{
	struct g_journal_metadata md;
	struct g_consumer *cp;
	struct bio *bp;
	u_char *sector;

	cp = sc->sc_dconsumer;
	sector = gj_malloc(cp->provider->sectorsize, M_WAITOK);
	strlcpy(md.md_magic, G_JOURNAL_MAGIC, sizeof(md.md_magic));
	md.md_version = G_JOURNAL_VERSION;
	md.md_id = sc->sc_id;
	md.md_type = sc->sc_orig_type;
	md.md_jstart = sc->sc_jstart;
	md.md_jend = sc->sc_jend;
	md.md_joffset = sc->sc_inactive.jj_offset;
	md.md_jid = sc->sc_journal_previous_id;
	md.md_flags = 0;
	if (sc->sc_flags & GJF_DEVICE_CLEAN)
		md.md_flags |= GJ_FLAG_CLEAN;

	if (sc->sc_flags & GJF_DEVICE_HARDCODED)
		strlcpy(md.md_provider, sc->sc_name, sizeof(md.md_provider));
	else
		bzero(md.md_provider, sizeof(md.md_provider));
	md.md_provsize = cp->provider->mediasize;
	journal_metadata_encode(&md, sector);

	/*
	 * Flush the cache, so we know all data are on disk.
	 * We write here informations like "journal is consistent", so we need
	 * to be sure it is. Without BIO_FLUSH here, we can end up in situation
	 * where metadata is stored on disk, but not all data.
	 */
	g_journal_flush_cache(sc);

	bp = g_alloc_bio();
	bp->bio_offset = cp->provider->mediasize - cp->provider->sectorsize;
	bp->bio_length = cp->provider->sectorsize;
	bp->bio_data = sector;
	bp->bio_cmd = BIO_WRITE;
	if (!(sc->sc_flags & GJF_DEVICE_DESTROY)) {
		bp->bio_done = g_journal_metadata_done;
		g_io_request(bp, cp);
	} else {
		bp->bio_done = NULL;
		g_io_request(bp, cp);
		biowait(bp, "gjmdu");
		g_journal_metadata_done(bp);
	}

	/*
	 * Be sure metadata reached the disk.
	 */
	g_journal_flush_cache(sc);
}

/*
 * This is where the I/O request comes from the GEOM.
 */
static void
g_journal_start(struct bio *bp)
{
	struct g_journal_softc *sc;

	sc = bp->bio_to->geom->softc;
	GJ_LOGREQ(3, bp, "Request received.");

	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
		mtx_lock(&sc->sc_mtx);
		bioq_insert_tail(&sc->sc_regular_queue, bp);
		wakeup(sc);
		mtx_unlock(&sc->sc_mtx);
		return;
	case BIO_GETATTR:
		if (strcmp(bp->bio_attribute, "GJOURNAL::provider") == 0) {
			strlcpy(bp->bio_data, bp->bio_to->name, bp->bio_length);
			bp->bio_completed = strlen(bp->bio_to->name) + 1;
			g_io_deliver(bp, 0);
			return;
		}
		/* FALLTHROUGH */
	case BIO_DELETE:
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}
}

static void
g_journal_std_done(struct bio *bp)
{
	struct g_journal_softc *sc;

	sc = bp->bio_from->geom->softc;
	mtx_lock(&sc->sc_mtx);
	bioq_insert_tail(&sc->sc_back_queue, bp);
	wakeup(sc);
	mtx_unlock(&sc->sc_mtx);
}

static struct bio *
g_journal_new_bio(off_t start, off_t end, off_t joffset, u_char *data,
    int flags)
{
	struct bio *bp;

	bp = g_alloc_bio();
	bp->bio_offset = start;
	bp->bio_joffset = joffset;
	bp->bio_length = end - start;
	bp->bio_cmd = BIO_WRITE;
	bp->bio_done = g_journal_std_done;
	if (data == NULL)
		bp->bio_data = NULL;
	else {
		bp->bio_data = gj_malloc(bp->bio_length, flags);
		if (bp->bio_data != NULL)
			bcopy(data, bp->bio_data, bp->bio_length);
	}
	return (bp);
}

#define	g_journal_insert_bio(head, bp, flags)				\
	g_journal_insert((head), (bp)->bio_offset,			\
		(bp)->bio_offset + (bp)->bio_length, (bp)->bio_joffset,	\
		(bp)->bio_data, flags)
/*
 * The function below does a lot more than just inserting bio to the queue.
 * It keeps the queue sorted by offset and ensures that there are no doubled
 * data (it combines bios where ranges overlap).
 *
 * The function returns the number of bios inserted (as bio can be splitted).
 */
static int
g_journal_insert(struct bio **head, off_t nstart, off_t nend, off_t joffset,
    u_char *data, int flags)
{
	struct bio *nbp, *cbp, *pbp;
	off_t cstart, cend;
	u_char *tmpdata;
	int n;

	GJ_DEBUG(3, "INSERT(%p): (%jd, %jd, %jd)", *head, nstart, nend,
	    joffset);
	n = 0;
	pbp = NULL;
	GJQ_FOREACH(*head, cbp) {
		cstart = cbp->bio_offset;
		cend = cbp->bio_offset + cbp->bio_length;

		if (nstart >= cend) {
			/*
			 *  +-------------+
			 *  |             |
			 *  |   current   |  +-------------+
			 *  |     bio     |  |             |
			 *  |             |  |     new     |
			 *  +-------------+  |     bio     |
			 *                   |             |
			 *                   +-------------+
			 */
			GJ_DEBUG(3, "INSERT(%p): 1", *head);
		} else if (nend <= cstart) {
			/*
			 *                   +-------------+
			 *                   |             |
			 *  +-------------+  |   current   |
			 *  |             |  |     bio     |
			 *  |     new     |  |             |
			 *  |     bio     |  +-------------+
			 *  |             |
			 *  +-------------+
			 */
			nbp = g_journal_new_bio(nstart, nend, joffset, data,
			    flags);
			if (pbp == NULL)
				*head = nbp;
			else
				pbp->bio_next = nbp;
			nbp->bio_next = cbp;
			n++;
			GJ_DEBUG(3, "INSERT(%p): 2 (nbp=%p pbp=%p)", *head, nbp,
			    pbp);
			goto end;
		} else if (nstart <= cstart && nend >= cend) {
			/*
			 *      +-------------+      +-------------+
			 *      | current bio |      | current bio |
			 *  +---+-------------+---+  +-------------+---+
			 *  |   |             |   |  |             |   |
			 *  |   |             |   |  |             |   |
			 *  |   +-------------+   |  +-------------+   |
			 *  |       new bio       |  |     new bio     |
			 *  +---------------------+  +-----------------+
			 *
			 *      +-------------+  +-------------+
			 *      | current bio |  | current bio |
			 *  +---+-------------+  +-------------+
			 *  |   |             |  |             |
			 *  |   |             |  |             |
			 *  |   +-------------+  +-------------+
			 *  |     new bio     |  |   new bio   |
			 *  +-----------------+  +-------------+
			 */
			g_journal_stats_bytes_skipped += cbp->bio_length;
			cbp->bio_offset = nstart;
			cbp->bio_joffset = joffset;
			cbp->bio_length = cend - nstart;
			if (cbp->bio_data != NULL) {
				gj_free(cbp->bio_data, cend - cstart);
				cbp->bio_data = NULL;
			}
			if (data != NULL) {
				cbp->bio_data = gj_malloc(cbp->bio_length,
				    flags);
				if (cbp->bio_data != NULL) {
					bcopy(data, cbp->bio_data,
					    cbp->bio_length);
				}
				data += cend - nstart;
			}
			joffset += cend - nstart;
			nstart = cend;
			GJ_DEBUG(3, "INSERT(%p): 3 (cbp=%p)", *head, cbp);
		} else if (nstart > cstart && nend >= cend) {
			/*
			 *  +-----------------+  +-------------+
			 *  |   current bio   |  | current bio |
			 *  |   +-------------+  |   +---------+---+
			 *  |   |             |  |   |         |   |
			 *  |   |             |  |   |         |   |
			 *  +---+-------------+  +---+---------+   |
			 *      |   new bio   |      |   new bio   |
			 *      +-------------+      +-------------+
			 */
			g_journal_stats_bytes_skipped += cend - nstart;
			nbp = g_journal_new_bio(nstart, cend, joffset, data,
			    flags);
			nbp->bio_next = cbp->bio_next;
			cbp->bio_next = nbp;
			cbp->bio_length = nstart - cstart;
			if (cbp->bio_data != NULL) {
				cbp->bio_data = gj_realloc(cbp->bio_data,
				    cbp->bio_length, cend - cstart);
			}
			if (data != NULL)
				data += cend - nstart;
			joffset += cend - nstart;
			nstart = cend;
			n++;
			GJ_DEBUG(3, "INSERT(%p): 4 (cbp=%p)", *head, cbp);
		} else if (nstart > cstart && nend < cend) {
			/*
			 *  +---------------------+
			 *  |     current bio     |
			 *  |   +-------------+   |
			 *  |   |             |   |
			 *  |   |             |   |
			 *  +---+-------------+---+
			 *      |   new bio   |
			 *      +-------------+
			 */
			g_journal_stats_bytes_skipped += nend - nstart;
			nbp = g_journal_new_bio(nstart, nend, joffset, data,
			    flags);
			nbp->bio_next = cbp->bio_next;
			cbp->bio_next = nbp;
			if (cbp->bio_data == NULL)
				tmpdata = NULL;
			else
				tmpdata = cbp->bio_data + nend - cstart;
			nbp = g_journal_new_bio(nend, cend,
			    cbp->bio_joffset + nend - cstart, tmpdata, flags);
			nbp->bio_next = ((struct bio *)cbp->bio_next)->bio_next;
			((struct bio *)cbp->bio_next)->bio_next = nbp;
			cbp->bio_length = nstart - cstart;
			if (cbp->bio_data != NULL) {
				cbp->bio_data = gj_realloc(cbp->bio_data,
				    cbp->bio_length, cend - cstart);
			}
			n += 2;
			GJ_DEBUG(3, "INSERT(%p): 5 (cbp=%p)", *head, cbp);
			goto end;
		} else if (nstart <= cstart && nend < cend) {
			/*
			 *  +-----------------+      +-------------+
			 *  |   current bio   |      | current bio |
			 *  +-------------+   |  +---+---------+   |
			 *  |             |   |  |   |         |   |
			 *  |             |   |  |   |         |   |
			 *  +-------------+---+  |   +---------+---+
			 *  |   new bio   |      |   new bio   |
			 *  +-------------+      +-------------+
			 */
			g_journal_stats_bytes_skipped += nend - nstart;
			nbp = g_journal_new_bio(nstart, nend, joffset, data,
			    flags);
			if (pbp == NULL)
				*head = nbp;
			else
				pbp->bio_next = nbp;
			nbp->bio_next = cbp;
			cbp->bio_offset = nend;
			cbp->bio_length = cend - nend;
			cbp->bio_joffset += nend - cstart;
			tmpdata = cbp->bio_data;
			if (tmpdata != NULL) {
				cbp->bio_data = gj_malloc(cbp->bio_length,
				    flags);
				if (cbp->bio_data != NULL) {
					bcopy(tmpdata + nend - cstart,
					    cbp->bio_data, cbp->bio_length);
				}
				gj_free(tmpdata, cend - cstart);
			}
			n++;
			GJ_DEBUG(3, "INSERT(%p): 6 (cbp=%p)", *head, cbp);
			goto end;
		}
		if (nstart == nend)
			goto end;
		pbp = cbp;
	}
	nbp = g_journal_new_bio(nstart, nend, joffset, data, flags);
	if (pbp == NULL)
		*head = nbp;
	else
		pbp->bio_next = nbp;
	nbp->bio_next = NULL;
	n++;
	GJ_DEBUG(3, "INSERT(%p): 8 (nbp=%p pbp=%p)", *head, nbp, pbp);
end:
	if (g_journal_debug >= 3) {
		GJQ_FOREACH(*head, cbp) {
			GJ_DEBUG(3, "ELEMENT: %p (%jd, %jd, %jd, %p)", cbp,
			    (intmax_t)cbp->bio_offset,
			    (intmax_t)cbp->bio_length,
			    (intmax_t)cbp->bio_joffset, cbp->bio_data);
		}
		GJ_DEBUG(3, "INSERT(%p): DONE %d", *head, n);
	}
	return (n);
}

/*
 * The function combines neighbour bios trying to squeeze as much data as
 * possible into one bio.
 *
 * The function returns the number of bios combined (negative value).
 */
static int
g_journal_optimize(struct bio *head)
{
	struct bio *cbp, *pbp;
	int n;

	n = 0;
	pbp = NULL;
	GJQ_FOREACH(head, cbp) {
		/* Skip bios which has to be read first. */
		if (cbp->bio_data == NULL) {
			pbp = NULL;
			continue;
		}
		/* There is no previous bio yet. */
		if (pbp == NULL) {
			pbp = cbp;
			continue;
		}
		/* Is this a neighbour bio? */
		if (pbp->bio_offset + pbp->bio_length != cbp->bio_offset) {
			/* Be sure that bios queue is sorted. */
			KASSERT(pbp->bio_offset + pbp->bio_length < cbp->bio_offset,
			    ("poffset=%jd plength=%jd coffset=%jd",
			    (intmax_t)pbp->bio_offset,
			    (intmax_t)pbp->bio_length,
			    (intmax_t)cbp->bio_offset));
			pbp = cbp;
			continue;
		}
		/* Be sure we don't end up with too big bio. */
		if (pbp->bio_length + cbp->bio_length > MAXPHYS) {
			pbp = cbp;
			continue;
		}
		/* Ok, we can join bios. */
		GJ_LOGREQ(4, pbp, "Join: ");
		GJ_LOGREQ(4, cbp, "and: ");
		pbp->bio_data = gj_realloc(pbp->bio_data,
		    pbp->bio_length + cbp->bio_length, pbp->bio_length);
		bcopy(cbp->bio_data, pbp->bio_data + pbp->bio_length,
		    cbp->bio_length);
		gj_free(cbp->bio_data, cbp->bio_length);
		pbp->bio_length += cbp->bio_length;
		pbp->bio_next = cbp->bio_next;
		g_destroy_bio(cbp);
		cbp = pbp;
		g_journal_stats_combined_ios++;
		n--;
		GJ_LOGREQ(4, pbp, "Got: ");
	}
	return (n);
}

/*
 * TODO: Update comment.
 * These are functions responsible for copying one portion of data from journal
 * to the destination provider.
 * The order goes like this:
 * 1. Read the header, which contains informations about data blocks
 *    following it.
 * 2. Read the data blocks from the journal.
 * 3. Write the data blocks on the data provider.
 *
 * g_journal_copy_start()
 * g_journal_copy_done() - got finished write request, logs potential errors.
 */

/*
 * When there is no data in cache, this function is used to read it.
 */
static void
g_journal_read_first(struct g_journal_softc *sc, struct bio *bp)
{
	struct bio *cbp;

	/*
	 * We were short in memory, so data was freed.
	 * In that case we need to read it back from journal.
	 */
	cbp = g_alloc_bio();
	cbp->bio_cflags = bp->bio_cflags;
	cbp->bio_parent = bp;
	cbp->bio_offset = bp->bio_joffset;
	cbp->bio_length = bp->bio_length;
	cbp->bio_data = gj_malloc(bp->bio_length, M_WAITOK);
	cbp->bio_cmd = BIO_READ;
	cbp->bio_done = g_journal_std_done;
	GJ_LOGREQ(4, cbp, "READ FIRST");
	g_io_request(cbp, sc->sc_jconsumer);
	g_journal_cache_misses++;
}

static void
g_journal_copy_send(struct g_journal_softc *sc)
{
	struct bio *bioq, *bp, *lbp;

	bioq = lbp = NULL;
	mtx_lock(&sc->sc_mtx);
	for (; sc->sc_copy_in_progress < g_journal_parallel_copies;) {
		bp = GJQ_FIRST(sc->sc_inactive.jj_queue);
		if (bp == NULL)
			break;
		GJQ_REMOVE(sc->sc_inactive.jj_queue, bp);
		sc->sc_copy_in_progress++;
		GJQ_INSERT_AFTER(bioq, bp, lbp);
		lbp = bp;
	}
	mtx_unlock(&sc->sc_mtx);
	if (g_journal_do_optimize)
		sc->sc_copy_in_progress += g_journal_optimize(bioq);
	while ((bp = GJQ_FIRST(bioq)) != NULL) {
		GJQ_REMOVE(bioq, bp);
		GJQ_INSERT_HEAD(sc->sc_copy_queue, bp);
		bp->bio_cflags = GJ_BIO_COPY;
		if (bp->bio_data == NULL)
			g_journal_read_first(sc, bp);
		else {
			bp->bio_joffset = 0;
			GJ_LOGREQ(4, bp, "SEND");
			g_io_request(bp, sc->sc_dconsumer);
		}
	}
}

static void
g_journal_copy_start(struct g_journal_softc *sc)
{

	/*
	 * Remember in metadata that we're starting to copy journaled data
	 * to the data provider.
	 * In case of power failure, we will copy these data once again on boot.
	 */
	if (!sc->sc_journal_copying) {
		sc->sc_journal_copying = 1;
		GJ_DEBUG(1, "Starting copy of journal.");
		g_journal_metadata_update(sc);
	}
	g_journal_copy_send(sc);
}

/*
 * Data block has been read from the journal provider.
 */
static int
g_journal_copy_read_done(struct bio *bp)
{
	struct g_journal_softc *sc;
	struct g_consumer *cp;
	struct bio *pbp;

	KASSERT(bp->bio_cflags == GJ_BIO_COPY,
	    ("Invalid bio (%d != %d).", bp->bio_cflags, GJ_BIO_COPY));

	sc = bp->bio_from->geom->softc;
	pbp = bp->bio_parent;

	if (bp->bio_error != 0) {
		GJ_DEBUG(0, "Error while reading data from %s (error=%d).",
		    bp->bio_to->name, bp->bio_error);
		/*
		 * We will not be able to deliver WRITE request as well.
		 */
		gj_free(bp->bio_data, bp->bio_length);
		g_destroy_bio(pbp);
		g_destroy_bio(bp);
		sc->sc_copy_in_progress--;
		return (1);
	}
	pbp->bio_data = bp->bio_data;
	cp = sc->sc_dconsumer;
	g_io_request(pbp, cp);
	GJ_LOGREQ(4, bp, "READ DONE");
	g_destroy_bio(bp);
	return (0);
}

/*
 * Data block has been written to the data provider.
 */
static void
g_journal_copy_write_done(struct bio *bp)
{
	struct g_journal_softc *sc;

	KASSERT(bp->bio_cflags == GJ_BIO_COPY,
	    ("Invalid bio (%d != %d).", bp->bio_cflags, GJ_BIO_COPY));

	sc = bp->bio_from->geom->softc;
	sc->sc_copy_in_progress--;

	if (bp->bio_error != 0) {
		GJ_LOGREQ(0, bp, "[copy] Error while writing data (error=%d)",
		    bp->bio_error);
	}
	GJQ_REMOVE(sc->sc_copy_queue, bp);
	gj_free(bp->bio_data, bp->bio_length);
	GJ_LOGREQ(4, bp, "DONE");
	g_destroy_bio(bp);

	if (sc->sc_copy_in_progress == 0) {
		/*
		 * This was the last write request for this journal.
		 */
		GJ_DEBUG(1, "Data has been copied.");
		sc->sc_journal_copying = 0;
	}
}

static void g_journal_flush_done(struct bio *bp);

/*
 * Flush one record onto active journal provider.
 */
static void
g_journal_flush(struct g_journal_softc *sc)
{
	struct g_journal_record_header hdr;
	struct g_journal_entry *ent;
	struct g_provider *pp;
	struct bio **bioq;
	struct bio *bp, *fbp, *pbp;
	off_t joffset;
	u_char *data, hash[16];
	MD5_CTX ctx;
	u_int i;

	if (sc->sc_current_count == 0)
		return;

	pp = sc->sc_jprovider;
	GJ_VALIDATE_OFFSET(sc->sc_journal_offset, sc);
	joffset = sc->sc_journal_offset;

	GJ_DEBUG(2, "Storing %d journal entries on %s at %jd.",
	    sc->sc_current_count, pp->name, (intmax_t)joffset);

	/*
	 * Store 'journal id', so we know to which journal this record belongs.
	 */
	hdr.jrh_journal_id = sc->sc_journal_id;
	/* Could be less than g_journal_record_entries if called due timeout. */
	hdr.jrh_nentries = MIN(sc->sc_current_count, g_journal_record_entries);
	strlcpy(hdr.jrh_magic, GJ_RECORD_HEADER_MAGIC, sizeof(hdr.jrh_magic));

	bioq = &sc->sc_active.jj_queue;
	GJQ_LAST(sc->sc_flush_queue, pbp);

	fbp = g_alloc_bio();
	fbp->bio_parent = NULL;
	fbp->bio_cflags = GJ_BIO_JOURNAL;
	fbp->bio_offset = -1;
	fbp->bio_joffset = joffset;
	fbp->bio_length = pp->sectorsize;
	fbp->bio_cmd = BIO_WRITE;
	fbp->bio_done = g_journal_std_done;
	GJQ_INSERT_AFTER(sc->sc_flush_queue, fbp, pbp);
	pbp = fbp;
	fbp->bio_to = pp;
	GJ_LOGREQ(4, fbp, "FLUSH_OUT");
	joffset += pp->sectorsize;
	sc->sc_flush_count++;
	if (sc->sc_flags & GJF_DEVICE_CHECKSUM)
		MD5Init(&ctx);

	for (i = 0; i < hdr.jrh_nentries; i++) {
		bp = sc->sc_current_queue;
		KASSERT(bp != NULL, ("NULL bp"));
		bp->bio_to = pp;
		GJ_LOGREQ(4, bp, "FLUSHED");
		sc->sc_current_queue = bp->bio_next;
		bp->bio_next = NULL;
		sc->sc_current_count--;

		/* Add to the header. */
		ent = &hdr.jrh_entries[i];
		ent->je_offset = bp->bio_offset;
		ent->je_joffset = joffset;
		ent->je_length = bp->bio_length;

		data = bp->bio_data;
		if (sc->sc_flags & GJF_DEVICE_CHECKSUM)
			MD5Update(&ctx, data, ent->je_length);
		g_reset_bio(bp);
		bp->bio_cflags = GJ_BIO_JOURNAL;
		bp->bio_offset = ent->je_offset;
		bp->bio_joffset = ent->je_joffset;
		bp->bio_length = ent->je_length;
		bp->bio_data = data;
		bp->bio_cmd = BIO_WRITE;
		bp->bio_done = g_journal_std_done;
		GJQ_INSERT_AFTER(sc->sc_flush_queue, bp, pbp);
		pbp = bp;
		bp->bio_to = pp;
		GJ_LOGREQ(4, bp, "FLUSH_OUT");
		joffset += bp->bio_length;
		sc->sc_flush_count++;

		/*
		 * Add request to the active sc_journal_queue queue.
		 * This is our cache. After journal switch we don't have to
		 * read the data from the inactive journal, because we keep
		 * it in memory.
		 */
		g_journal_insert(bioq, ent->je_offset,
		    ent->je_offset + ent->je_length, ent->je_joffset, data,
		    M_NOWAIT);
	}

	/*
	 * After all requests, store valid header.
	 */
	data = gj_malloc(pp->sectorsize, M_WAITOK);
	if (sc->sc_flags & GJF_DEVICE_CHECKSUM) {
		MD5Final(hash, &ctx);
		bcopy(hash, hdr.jrh_sum, sizeof(hdr.jrh_sum));
	}
	g_journal_record_header_encode(&hdr, data);
	fbp->bio_data = data;

	sc->sc_journal_offset = joffset;

	g_journal_check_overflow(sc);
}

/*
 * Flush request finished.
 */
static void
g_journal_flush_done(struct bio *bp)
{
	struct g_journal_softc *sc;
	struct g_consumer *cp;

	KASSERT((bp->bio_cflags & GJ_BIO_MASK) == GJ_BIO_JOURNAL,
	    ("Invalid bio (%d != %d).", bp->bio_cflags, GJ_BIO_JOURNAL));

	cp = bp->bio_from;
	sc = cp->geom->softc;
	sc->sc_flush_in_progress--;

	if (bp->bio_error != 0) {
		GJ_LOGREQ(0, bp, "[flush] Error while writing data (error=%d)",
		    bp->bio_error);
	}
	gj_free(bp->bio_data, bp->bio_length);
	GJ_LOGREQ(4, bp, "DONE");
	g_destroy_bio(bp);
}

static void g_journal_release_delayed(struct g_journal_softc *sc);

static void
g_journal_flush_send(struct g_journal_softc *sc)
{
	struct g_consumer *cp;
	struct bio *bioq, *bp, *lbp;

	cp = sc->sc_jconsumer;
	bioq = lbp = NULL;
	while (sc->sc_flush_in_progress < g_journal_parallel_flushes) {
		/* Send one flush requests to the active journal. */
		bp = GJQ_FIRST(sc->sc_flush_queue);
		if (bp != NULL) {
			GJQ_REMOVE(sc->sc_flush_queue, bp);
			sc->sc_flush_count--;
			bp->bio_offset = bp->bio_joffset;
			bp->bio_joffset = 0;
			sc->sc_flush_in_progress++;
			GJQ_INSERT_AFTER(bioq, bp, lbp);
			lbp = bp;
		}
		/* Try to release delayed requests. */
		g_journal_release_delayed(sc);
		/* If there are no requests to flush, leave. */
		if (GJQ_FIRST(sc->sc_flush_queue) == NULL)
			break;
	}
	if (g_journal_do_optimize)
		sc->sc_flush_in_progress += g_journal_optimize(bioq);
	while ((bp = GJQ_FIRST(bioq)) != NULL) {
		GJQ_REMOVE(bioq, bp);
		GJ_LOGREQ(3, bp, "Flush request send");
		g_io_request(bp, cp);
	}
}

static void
g_journal_add_current(struct g_journal_softc *sc, struct bio *bp)
{
	int n;

	GJ_LOGREQ(4, bp, "CURRENT %d", sc->sc_current_count);
	n = g_journal_insert_bio(&sc->sc_current_queue, bp, M_WAITOK);
	sc->sc_current_count += n;
	n = g_journal_optimize(sc->sc_current_queue);
	sc->sc_current_count += n;
	/*
	 * For requests which are added to the current queue we deliver
	 * response immediately.
	 */
	bp->bio_completed = bp->bio_length;
	g_io_deliver(bp, 0);
	if (sc->sc_current_count >= g_journal_record_entries) {
		/*
		 * Let's flush one record onto active journal provider.
		 */
		g_journal_flush(sc);
	}
}

static void
g_journal_release_delayed(struct g_journal_softc *sc)
{
	struct bio *bp;

	for (;;) {
		/* The flush queue is full, exit. */
		if (sc->sc_flush_count >= g_journal_accept_immediately)
			return;
		bp = bioq_takefirst(&sc->sc_delayed_queue);
		if (bp == NULL)
			return;
		sc->sc_delayed_count--;
		g_journal_add_current(sc, bp);
	}
}

/*
 * Add I/O request to the current queue. If we have enough requests for one
 * journal record we flush them onto active journal provider.
 */
static void
g_journal_add_request(struct g_journal_softc *sc, struct bio *bp)
{

	/*
	 * The flush queue is full, we need to delay the request.
	 */
	if (sc->sc_delayed_count > 0 ||
	    sc->sc_flush_count >= g_journal_accept_immediately) {
		GJ_LOGREQ(4, bp, "DELAYED");
		bioq_insert_tail(&sc->sc_delayed_queue, bp);
		sc->sc_delayed_count++;
		return;
	}

	KASSERT(TAILQ_EMPTY(&sc->sc_delayed_queue.queue),
	    ("DELAYED queue not empty."));
	g_journal_add_current(sc, bp);
}

static void g_journal_read_done(struct bio *bp);

/*
 * Try to find requested data in cache.
 */
static struct bio *
g_journal_read_find(struct bio *head, int sorted, struct bio *pbp, off_t ostart,
    off_t oend)
{
	off_t cstart, cend;
	struct bio *bp;

	GJQ_FOREACH(head, bp) {
		if (bp->bio_offset == -1)
			continue;
		cstart = MAX(ostart, bp->bio_offset);
		cend = MIN(oend, bp->bio_offset + bp->bio_length);
		if (cend <= ostart)
			continue;
		else if (cstart >= oend) {
			if (!sorted)
				continue;
			else {
				bp = NULL;
				break;
			}
		}
		if (bp->bio_data == NULL)
			break;
		GJ_DEBUG(3, "READ(%p): (%jd, %jd) (bp=%p)", head, cstart, cend,
		    bp);
		bcopy(bp->bio_data + cstart - bp->bio_offset,
		    pbp->bio_data + cstart - pbp->bio_offset, cend - cstart);
		pbp->bio_completed += cend - cstart;
		if (pbp->bio_completed == pbp->bio_length) {
			/*
			 * Cool, the whole request was in cache, deliver happy
			 * message.
			 */
			g_io_deliver(pbp, 0);
			return (pbp);
		}
		break;
	}
	return (bp);
}

/*
 * This function is used for collecting data on read.
 * The complexity is because parts of the data can be stored in four different
 * places:
 * - in memory - the data not yet send to the active journal provider
 * - in the active journal
 * - in the inactive journal
 * - in the data provider
 */
static void
g_journal_read(struct g_journal_softc *sc, struct bio *pbp, off_t ostart,
    off_t oend)
{
	struct bio *bp, *nbp, *head;
	off_t cstart, cend;
	u_int i, sorted = 0;

	GJ_DEBUG(3, "READ: (%jd, %jd)", ostart, oend);

	cstart = cend = -1;
	bp = NULL;
	head = NULL;
	for (i = 1; i <= 5; i++) {
		switch (i) {
		case 1:	/* Not-yet-send data. */
			head = sc->sc_current_queue;
			sorted = 1;
			break;
		case 2: /* Skip flush queue as they are also in active queue */
			continue;
		case 3:	/* Active journal. */
			head = sc->sc_active.jj_queue;
			sorted = 1;
			break;
		case 4:	/* Inactive journal. */
			/*
			 * XXX: Here could be a race with g_journal_lowmem().
			 */
			head = sc->sc_inactive.jj_queue;
			sorted = 1;
			break;
		case 5:	/* In-flight to the data provider. */
			head = sc->sc_copy_queue;
			sorted = 0;
			break;
		default:
			panic("gjournal %s: i=%d", __func__, i);
		}
		bp = g_journal_read_find(head, sorted, pbp, ostart, oend);
		if (bp == pbp) { /* Got the whole request. */
			GJ_DEBUG(2, "Got the whole request from %u.", i);
			return;
		} else if (bp != NULL) {
			cstart = MAX(ostart, bp->bio_offset);
			cend = MIN(oend, bp->bio_offset + bp->bio_length);
			GJ_DEBUG(2, "Got part of the request from %u (%jd-%jd).",
			    i, (intmax_t)cstart, (intmax_t)cend);
			break;
		}
	}
	if (bp != NULL) {
		if (bp->bio_data == NULL) {
			nbp = g_duplicate_bio(pbp);
			nbp->bio_cflags = GJ_BIO_READ;
			nbp->bio_data =
			    pbp->bio_data + cstart - pbp->bio_offset;
			nbp->bio_offset =
			    bp->bio_joffset + cstart - bp->bio_offset;
			nbp->bio_length = cend - cstart;
			nbp->bio_done = g_journal_read_done;
			g_io_request(nbp, sc->sc_jconsumer);
		}
		/*
		 * If we don't have the whole request yet, call g_journal_read()
		 * recursively.
		 */
		if (ostart < cstart)
			g_journal_read(sc, pbp, ostart, cstart);
		if (oend > cend)
			g_journal_read(sc, pbp, cend, oend);
	} else {
		/*
		 * No data in memory, no data in journal.
		 * Its time for asking data provider.
		 */
		GJ_DEBUG(3, "READ(data): (%jd, %jd)", ostart, oend);
		nbp = g_duplicate_bio(pbp);
		nbp->bio_cflags = GJ_BIO_READ;
		nbp->bio_data = pbp->bio_data + ostart - pbp->bio_offset;
		nbp->bio_offset = ostart;
		nbp->bio_length = oend - ostart;
		nbp->bio_done = g_journal_read_done;
		g_io_request(nbp, sc->sc_dconsumer);
		/* We have the whole request, return here. */
		return;
	}
}

/*
 * Function responsible for handling finished READ requests.
 * Actually, g_std_done() could be used here, the only difference is that we
 * log error.
 */
static void
g_journal_read_done(struct bio *bp)
{
	struct bio *pbp;

	KASSERT(bp->bio_cflags == GJ_BIO_READ,
	    ("Invalid bio (%d != %d).", bp->bio_cflags, GJ_BIO_READ));

	pbp = bp->bio_parent;
	pbp->bio_inbed++;
	pbp->bio_completed += bp->bio_length;

	if (bp->bio_error != 0) {
		if (pbp->bio_error == 0)
			pbp->bio_error = bp->bio_error;
		GJ_DEBUG(0, "Error while reading data from %s (error=%d).",
		    bp->bio_to->name, bp->bio_error);
	}
	g_destroy_bio(bp);
	if (pbp->bio_children == pbp->bio_inbed &&
	    pbp->bio_completed == pbp->bio_length) {
		/* We're done. */
		g_io_deliver(pbp, 0);
	}
}

/*
 * Deactive current journal and active next one.
 */
static void
g_journal_switch(struct g_journal_softc *sc)
{
	struct g_provider *pp;

	if (JEMPTY(sc)) {
		GJ_DEBUG(3, "No need for %s switch.", sc->sc_name);
		pp = LIST_FIRST(&sc->sc_geom->provider);
		if (!(sc->sc_flags & GJF_DEVICE_CLEAN) && pp->acw == 0) {
			sc->sc_flags |= GJF_DEVICE_CLEAN;
			GJ_DEBUG(1, "Marking %s as clean.", sc->sc_name);
			g_journal_metadata_update(sc);
		}
	} else {
		GJ_DEBUG(3, "Switching journal %s.", sc->sc_geom->name);

		pp = sc->sc_jprovider;

		sc->sc_journal_previous_id = sc->sc_journal_id;

		sc->sc_journal_id = sc->sc_journal_next_id;
		sc->sc_journal_next_id = arc4random();

		GJ_VALIDATE_OFFSET(sc->sc_journal_offset, sc);

		g_journal_write_header(sc);

		sc->sc_inactive.jj_offset = sc->sc_active.jj_offset;
		sc->sc_inactive.jj_queue = sc->sc_active.jj_queue;

		sc->sc_active.jj_offset =
		    sc->sc_journal_offset - pp->sectorsize;
		sc->sc_active.jj_queue = NULL;

		/*
		 * Switch is done, start copying data from the (now) inactive
		 * journal to the data provider.
		 */
		g_journal_copy_start(sc);
	}
	mtx_lock(&sc->sc_mtx);
	sc->sc_flags &= ~GJF_DEVICE_SWITCH;
	mtx_unlock(&sc->sc_mtx);
}

static void
g_journal_initialize(struct g_journal_softc *sc)
{

	sc->sc_journal_id = arc4random();
	sc->sc_journal_next_id = arc4random();
	sc->sc_journal_previous_id = sc->sc_journal_id;
	sc->sc_journal_offset = sc->sc_jstart;
	sc->sc_inactive.jj_offset = sc->sc_jstart;
	g_journal_write_header(sc);
	sc->sc_active.jj_offset = sc->sc_jstart;
}

static void
g_journal_mark_as_dirty(struct g_journal_softc *sc)
{
	const struct g_journal_desc *desc;
	int i;

	GJ_DEBUG(1, "Marking file system %s as dirty.", sc->sc_name);
	for (i = 0; (desc = g_journal_filesystems[i]) != NULL; i++)
		desc->jd_dirty(sc->sc_dconsumer);
}

/*
 * Function read record header from the given journal.
 * It is very simlar to g_read_data(9), but it doesn't allocate memory for bio
 * and data on every call.
 */
static int
g_journal_sync_read(struct g_consumer *cp, struct bio *bp, off_t offset,
    void *data)
{
	int error;

	g_reset_bio(bp);
	bp->bio_cmd = BIO_READ;
	bp->bio_done = NULL;
	bp->bio_offset = offset;
	bp->bio_length = cp->provider->sectorsize;
	bp->bio_data = data;
	g_io_request(bp, cp);
	error = biowait(bp, "gjs_read");
	return (error);
}

#if 0
/*
 * Function is called when we start the journal device and we detect that
 * one of the journals was not fully copied.
 * The purpose of this function is to read all records headers from journal
 * and placed them in the inactive queue, so we can start journal
 * synchronization process and the journal provider itself.
 * Design decision was taken to not synchronize the whole journal here as it
 * can take too much time. Reading headers only and delaying synchronization
 * process until after journal provider is started should be the best choice.
 */
#endif

static void
g_journal_sync(struct g_journal_softc *sc)
{
	struct g_journal_record_header rhdr;
	struct g_journal_entry *ent;
	struct g_journal_header jhdr;
	struct g_consumer *cp;
	struct bio *bp, *fbp, *tbp;
	off_t joffset, offset;
	u_char *buf, sum[16];
	uint64_t id;
	MD5_CTX ctx;
	int error, found, i;

	found = 0;
	fbp = NULL;
	cp = sc->sc_jconsumer;
	bp = g_alloc_bio();
	buf = gj_malloc(cp->provider->sectorsize, M_WAITOK);
	offset = joffset = sc->sc_inactive.jj_offset = sc->sc_journal_offset;

	GJ_DEBUG(2, "Looking for termination at %jd.", (intmax_t)joffset);

	/*
	 * Read and decode first journal header.
	 */
	error = g_journal_sync_read(cp, bp, offset, buf);
	if (error != 0) {
		GJ_DEBUG(0, "Error while reading journal header from %s.",
		    cp->provider->name);
		goto end;
	}
	error = g_journal_header_decode(buf, &jhdr);
	if (error != 0) {
		GJ_DEBUG(0, "Cannot decode journal header from %s.",
		    cp->provider->name);
		goto end;
	}
	id = sc->sc_journal_id;
	if (jhdr.jh_journal_id != sc->sc_journal_id) {
		GJ_DEBUG(1, "Journal ID mismatch at %jd (0x%08x != 0x%08x).",
		    (intmax_t)offset, (u_int)jhdr.jh_journal_id, (u_int)id);
		goto end;
	}
	offset += cp->provider->sectorsize;
	id = sc->sc_journal_next_id = jhdr.jh_journal_next_id;

	for (;;) {
		/*
		 * If the biggest record won't fit, look for a record header or
		 * journal header from the beginning.
		 */
		GJ_VALIDATE_OFFSET(offset, sc);
		error = g_journal_sync_read(cp, bp, offset, buf);
		if (error != 0) {
			/*
			 * Not good. Having an error while reading header
			 * means, that we cannot read next headers and in
			 * consequence we cannot find termination.
			 */
			GJ_DEBUG(0,
			    "Error while reading record header from %s.",
			    cp->provider->name);
			break;
		}

		error = g_journal_record_header_decode(buf, &rhdr);
		if (error != 0) {
			GJ_DEBUG(2, "Not a record header at %jd (error=%d).",
			    (intmax_t)offset, error);
			/*
			 * This is not a record header.
			 * If we are lucky, this is next journal header.
			 */
			error = g_journal_header_decode(buf, &jhdr);
			if (error != 0) {
				GJ_DEBUG(1, "Not a journal header at %jd (error=%d).",
				    (intmax_t)offset, error);
				/*
				 * Nope, this is not journal header, which
				 * bascially means that journal is not
				 * terminated properly.
				 */
				error = ENOENT;
				break;
			}
			/*
			 * Ok. This is header of _some_ journal. Now we need to
			 * verify if this is header of the _next_ journal.
			 */
			if (jhdr.jh_journal_id != id) {
				GJ_DEBUG(1, "Journal ID mismatch at %jd "
				    "(0x%08x != 0x%08x).", (intmax_t)offset,
				    (u_int)jhdr.jh_journal_id, (u_int)id);
				error = ENOENT;
				break;
			}

			/* Found termination. */
			found++;
			GJ_DEBUG(1, "Found termination at %jd (id=0x%08x).",
			    (intmax_t)offset, (u_int)id);
			sc->sc_active.jj_offset = offset;
			sc->sc_journal_offset =
			    offset + cp->provider->sectorsize;
			sc->sc_journal_id = id;
			id = sc->sc_journal_next_id = jhdr.jh_journal_next_id;

			while ((tbp = fbp) != NULL) {
				fbp = tbp->bio_next;
				GJ_LOGREQ(3, tbp, "Adding request.");
				g_journal_insert_bio(&sc->sc_inactive.jj_queue,
				    tbp, M_WAITOK);
			}

			/* Skip journal's header. */
			offset += cp->provider->sectorsize;
			continue;
		}

		/* Skip record's header. */
		offset += cp->provider->sectorsize;

		/*
		 * Add information about every record entry to the inactive
		 * queue.
		 */
		if (sc->sc_flags & GJF_DEVICE_CHECKSUM)
			MD5Init(&ctx);
		for (i = 0; i < rhdr.jrh_nentries; i++) {
			ent = &rhdr.jrh_entries[i];
			GJ_DEBUG(3, "Insert entry: %jd %jd.",
			    (intmax_t)ent->je_offset, (intmax_t)ent->je_length);
			g_journal_insert(&fbp, ent->je_offset,
			    ent->je_offset + ent->je_length, ent->je_joffset,
			    NULL, M_WAITOK);
			if (sc->sc_flags & GJF_DEVICE_CHECKSUM) {
				u_char *buf2;

				/*
				 * TODO: Should use faster function (like
				 *       g_journal_sync_read()).
				 */
				buf2 = g_read_data(cp, offset, ent->je_length,
				    NULL);
				if (buf2 == NULL)
					GJ_DEBUG(0, "Cannot read data at %jd.",
					    (intmax_t)offset);
				else {
					MD5Update(&ctx, buf2, ent->je_length);
					g_free(buf2);
				}
			}
			/* Skip entry's data. */
			offset += ent->je_length;
		}
		if (sc->sc_flags & GJF_DEVICE_CHECKSUM) {
			MD5Final(sum, &ctx);
			if (bcmp(sum, rhdr.jrh_sum, sizeof(rhdr.jrh_sum)) != 0) {
				GJ_DEBUG(0, "MD5 hash mismatch at %jd!",
				    (intmax_t)offset);
			}
		}
	}
end:
	gj_free(bp->bio_data, cp->provider->sectorsize);
	g_destroy_bio(bp);

	/* Remove bios from unterminated journal. */
	while ((tbp = fbp) != NULL) {
		fbp = tbp->bio_next;
		g_destroy_bio(tbp);
	}

	if (found < 1 && joffset > 0) {
		GJ_DEBUG(0, "Journal on %s is broken/corrupted. Initializing.",
		    sc->sc_name);
		while ((tbp = sc->sc_inactive.jj_queue) != NULL) {
			sc->sc_inactive.jj_queue = tbp->bio_next;
			g_destroy_bio(tbp);
		}
		g_journal_initialize(sc);
		g_journal_mark_as_dirty(sc);
	} else {
		GJ_DEBUG(0, "Journal %s consistent.", sc->sc_name);
		g_journal_copy_start(sc);
	}
}

/*
 * Wait for requests.
 * If we have requests in the current queue, flush them after 3 seconds from the
 * last flush. In this way we don't wait forever (or for journal switch) with
 * storing not full records on journal.
 */
static void
g_journal_wait(struct g_journal_softc *sc, time_t last_write)
{
	int error, timeout;

	GJ_DEBUG(3, "%s: enter", __func__);
	if (sc->sc_current_count == 0) {
		if (g_journal_debug < 2)
			msleep(sc, &sc->sc_mtx, PRIBIO | PDROP, "gj:work", 0);
		else {
			/*
			 * If we have debug turned on, show number of elements
			 * in various queues.
			 */
			for (;;) {
				error = msleep(sc, &sc->sc_mtx, PRIBIO,
				    "gj:work", hz * 3);
				if (error == 0) {
					mtx_unlock(&sc->sc_mtx);
					break;
				}
				GJ_DEBUG(3, "Report: current count=%d",
				    sc->sc_current_count);
				GJ_DEBUG(3, "Report: flush count=%d",
				    sc->sc_flush_count);
				GJ_DEBUG(3, "Report: flush in progress=%d",
				    sc->sc_flush_in_progress);
				GJ_DEBUG(3, "Report: copy in progress=%d",
				    sc->sc_copy_in_progress);
				GJ_DEBUG(3, "Report: delayed=%d",
				    sc->sc_delayed_count);
			}
		}
		GJ_DEBUG(3, "%s: exit 1", __func__);
		return;
	}

	/*
	 * Flush even not full records every 3 seconds.
	 */
	timeout = (last_write + 3 - time_second) * hz;
	if (timeout <= 0) {
		mtx_unlock(&sc->sc_mtx);
		g_journal_flush(sc);
		g_journal_flush_send(sc);
		GJ_DEBUG(3, "%s: exit 2", __func__);
		return;
	}
	error = msleep(sc, &sc->sc_mtx, PRIBIO | PDROP, "gj:work", timeout);
	if (error == EWOULDBLOCK)
		g_journal_flush_send(sc);
	GJ_DEBUG(3, "%s: exit 3", __func__);
}

/*
 * Worker thread.
 */
static void
g_journal_worker(void *arg)
{
	struct g_journal_softc *sc;
	struct g_geom *gp;
	struct g_provider *pp;
	struct bio *bp;
	time_t last_write;
	int type;

	thread_lock(curthread);
	sched_prio(curthread, PRIBIO);
	thread_unlock(curthread);

	sc = arg;
	type = 0;	/* gcc */

	if (sc->sc_flags & GJF_DEVICE_CLEAN) {
		GJ_DEBUG(0, "Journal %s clean.", sc->sc_name);
		g_journal_initialize(sc);
	} else {
		g_journal_sync(sc);
	}
	/*
	 * Check if we can use BIO_FLUSH.
	 */
	sc->sc_bio_flush = 0;
	if (g_io_flush(sc->sc_jconsumer) == 0) {
		sc->sc_bio_flush |= GJ_FLUSH_JOURNAL;
		GJ_DEBUG(1, "BIO_FLUSH supported by %s.",
		    sc->sc_jconsumer->provider->name);
	} else {
		GJ_DEBUG(0, "BIO_FLUSH not supported by %s.",
		    sc->sc_jconsumer->provider->name);
	}
	if (sc->sc_jconsumer != sc->sc_dconsumer) {
		if (g_io_flush(sc->sc_dconsumer) == 0) {
			sc->sc_bio_flush |= GJ_FLUSH_DATA;
			GJ_DEBUG(1, "BIO_FLUSH supported by %s.",
			    sc->sc_dconsumer->provider->name);
		} else {
			GJ_DEBUG(0, "BIO_FLUSH not supported by %s.",
			    sc->sc_dconsumer->provider->name);
		}
	}

	gp = sc->sc_geom;
	g_topology_lock();
	pp = g_new_providerf(gp, "%s.journal", sc->sc_name);
	pp->mediasize = sc->sc_mediasize;
	/*
	 * There could be a problem when data provider and journal providers
	 * have different sectorsize, but such scenario is prevented on journal
	 * creation.
	 */
	pp->sectorsize = sc->sc_sectorsize;
	g_error_provider(pp, 0);
	g_topology_unlock();
	last_write = time_second;

	if (sc->sc_rootmount != NULL) {
		GJ_DEBUG(1, "root_mount_rel %p", sc->sc_rootmount);
		root_mount_rel(sc->sc_rootmount);
		sc->sc_rootmount = NULL;
	}

	for (;;) {
		/* Get first request from the queue. */
		mtx_lock(&sc->sc_mtx);
		bp = bioq_first(&sc->sc_back_queue);
		if (bp != NULL)
			type = (bp->bio_cflags & GJ_BIO_MASK);
		if (bp == NULL) {
			bp = bioq_first(&sc->sc_regular_queue);
			if (bp != NULL)
				type = GJ_BIO_REGULAR;
		}
		if (bp == NULL) {
try_switch:
			if ((sc->sc_flags & GJF_DEVICE_SWITCH) ||
			    (sc->sc_flags & GJF_DEVICE_DESTROY)) {
				if (sc->sc_current_count > 0) {
					mtx_unlock(&sc->sc_mtx);
					g_journal_flush(sc);
					g_journal_flush_send(sc);
					continue;
				}
				if (sc->sc_flush_in_progress > 0)
					goto sleep;
				if (sc->sc_copy_in_progress > 0)
					goto sleep;
			}
			if (sc->sc_flags & GJF_DEVICE_SWITCH) {
				mtx_unlock(&sc->sc_mtx);
				g_journal_switch(sc);
				wakeup(&sc->sc_journal_copying);
				continue;
			}
			if (sc->sc_flags & GJF_DEVICE_DESTROY) {
				GJ_DEBUG(1, "Shutting down worker "
				    "thread for %s.", gp->name);
				sc->sc_worker = NULL;
				wakeup(&sc->sc_worker);
				mtx_unlock(&sc->sc_mtx);
				kproc_exit(0);
			}
sleep:
			g_journal_wait(sc, last_write);
			continue;
		}
		/*
		 * If we're in switch process, we need to delay all new
		 * write requests until its done.
		 */
		if ((sc->sc_flags & GJF_DEVICE_SWITCH) &&
		    type == GJ_BIO_REGULAR && bp->bio_cmd == BIO_WRITE) {
			GJ_LOGREQ(2, bp, "WRITE on SWITCH");
			goto try_switch;
		}
		if (type == GJ_BIO_REGULAR)
			bioq_remove(&sc->sc_regular_queue, bp);
		else
			bioq_remove(&sc->sc_back_queue, bp);
		mtx_unlock(&sc->sc_mtx);
		switch (type) {
		case GJ_BIO_REGULAR:
			/* Regular request. */
			switch (bp->bio_cmd) {
			case BIO_READ:
				g_journal_read(sc, bp, bp->bio_offset,
				    bp->bio_offset + bp->bio_length);
				break;
			case BIO_WRITE:
				last_write = time_second;
				g_journal_add_request(sc, bp);
				g_journal_flush_send(sc);
				break;
			default:
				panic("Invalid bio_cmd (%d).", bp->bio_cmd);
			}
			break;
		case GJ_BIO_COPY:
			switch (bp->bio_cmd) {
			case BIO_READ:
				if (g_journal_copy_read_done(bp))
					g_journal_copy_send(sc);
				break;
			case BIO_WRITE:
				g_journal_copy_write_done(bp);
				g_journal_copy_send(sc);
				break;
			default:
				panic("Invalid bio_cmd (%d).", bp->bio_cmd);
			}
			break;
		case GJ_BIO_JOURNAL:
			g_journal_flush_done(bp);
			g_journal_flush_send(sc);
			break;
		case GJ_BIO_READ:
		default:
			panic("Invalid bio (%d).", type);
		}
	}
}

static void
g_journal_destroy_event(void *arg, int flags __unused)
{
	struct g_journal_softc *sc;

	g_topology_assert();
	sc = arg;
	g_journal_destroy(sc);
}

static void
g_journal_timeout(void *arg)
{
	struct g_journal_softc *sc;

	sc = arg;
	GJ_DEBUG(0, "Timeout. Journal %s cannot be completed.",
	    sc->sc_geom->name);
	g_post_event(g_journal_destroy_event, sc, M_NOWAIT, NULL);
}

static struct g_geom *
g_journal_create(struct g_class *mp, struct g_provider *pp,
    const struct g_journal_metadata *md)
{
	struct g_journal_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp;
	int error;

	sc = NULL;	/* gcc */

	g_topology_assert();
	/*
	 * There are two possibilities:
	 * 1. Data and both journals are on the same provider.
	 * 2. Data and journals are all on separated providers.
	 */
	/* Look for journal device with the same ID. */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_id == md->md_id)
			break;
	}
	if (gp == NULL)
		sc = NULL;
	else if (sc != NULL && (sc->sc_type & md->md_type) != 0) {
		GJ_DEBUG(1, "Journal device %u already configured.", sc->sc_id);
		return (NULL);
	}
	if (md->md_type == 0 || (md->md_type & ~GJ_TYPE_COMPLETE) != 0) {
		GJ_DEBUG(0, "Invalid type on %s.", pp->name);
		return (NULL);
	}
	if (md->md_type & GJ_TYPE_DATA) {
		GJ_DEBUG(0, "Journal %u: %s contains data.", md->md_id,
		    pp->name);
	}
	if (md->md_type & GJ_TYPE_JOURNAL) {
		GJ_DEBUG(0, "Journal %u: %s contains journal.", md->md_id,
		    pp->name);
	}

	if (sc == NULL) {
		/* Action geom. */
		sc = malloc(sizeof(*sc), M_JOURNAL, M_WAITOK | M_ZERO);
		sc->sc_id = md->md_id;
		sc->sc_type = 0;
		sc->sc_flags = 0;
		sc->sc_worker = NULL;

		gp = g_new_geomf(mp, "gjournal %u", sc->sc_id);
		gp->start = g_journal_start;
		gp->orphan = g_journal_orphan;
		gp->access = g_journal_access;
		gp->softc = sc;
		gp->flags |= G_GEOM_VOLATILE_BIO;
		sc->sc_geom = gp;

		mtx_init(&sc->sc_mtx, "gjournal", NULL, MTX_DEF);

		bioq_init(&sc->sc_back_queue);
		bioq_init(&sc->sc_regular_queue);
		bioq_init(&sc->sc_delayed_queue);
		sc->sc_delayed_count = 0;
		sc->sc_current_queue = NULL;
		sc->sc_current_count = 0;
		sc->sc_flush_queue = NULL;
		sc->sc_flush_count = 0;
		sc->sc_flush_in_progress = 0;
		sc->sc_copy_queue = NULL;
		sc->sc_copy_in_progress = 0;
		sc->sc_inactive.jj_queue = NULL;
		sc->sc_active.jj_queue = NULL;

		sc->sc_rootmount = root_mount_hold("GJOURNAL");
		GJ_DEBUG(1, "root_mount_hold %p", sc->sc_rootmount);

		callout_init(&sc->sc_callout, 1);
		if (md->md_type != GJ_TYPE_COMPLETE) {
			/*
			 * Journal and data are on separate providers.
			 * At this point we have only one of them.
			 * We setup a timeout in case the other part will not
			 * appear, so we won't wait forever.
			 */
			callout_reset(&sc->sc_callout, 5 * hz,
			    g_journal_timeout, sc);
		}
	}

	/* Remember type of the data provider. */
	if (md->md_type & GJ_TYPE_DATA)
		sc->sc_orig_type = md->md_type;
	sc->sc_type |= md->md_type;
	cp = NULL;

	if (md->md_type & GJ_TYPE_DATA) {
		if (md->md_flags & GJ_FLAG_CLEAN)
			sc->sc_flags |= GJF_DEVICE_CLEAN;
		if (md->md_flags & GJ_FLAG_CHECKSUM)
			sc->sc_flags |= GJF_DEVICE_CHECKSUM;
		cp = g_new_consumer(gp);
		error = g_attach(cp, pp);
		KASSERT(error == 0, ("Cannot attach to %s (error=%d).",
		    pp->name, error));
		error = g_access(cp, 1, 1, 1);
		if (error != 0) {
			GJ_DEBUG(0, "Cannot access %s (error=%d).", pp->name,
			    error);
			g_journal_destroy(sc);
			return (NULL);
		}
		sc->sc_dconsumer = cp;
		sc->sc_mediasize = pp->mediasize - pp->sectorsize;
		sc->sc_sectorsize = pp->sectorsize;
		sc->sc_jstart = md->md_jstart;
		sc->sc_jend = md->md_jend;
		if (md->md_provider[0] != '\0')
			sc->sc_flags |= GJF_DEVICE_HARDCODED;
		sc->sc_journal_offset = md->md_joffset;
		sc->sc_journal_id = md->md_jid;
		sc->sc_journal_previous_id = md->md_jid;
	}
	if (md->md_type & GJ_TYPE_JOURNAL) {
		if (cp == NULL) {
			cp = g_new_consumer(gp);
			error = g_attach(cp, pp);
			KASSERT(error == 0, ("Cannot attach to %s (error=%d).",
			    pp->name, error));
			error = g_access(cp, 1, 1, 1);
			if (error != 0) {
				GJ_DEBUG(0, "Cannot access %s (error=%d).",
				    pp->name, error);
				g_journal_destroy(sc);
				return (NULL);
			}
		} else {
			/*
			 * Journal is on the same provider as data, which means
			 * that data provider ends where journal starts.
			 */
			sc->sc_mediasize = md->md_jstart;
		}
		sc->sc_jconsumer = cp;
	}

	/* Start switcher kproc if needed. */
	if (g_journal_switcher_proc == NULL)
		g_journal_start_switcher(mp);

	if ((sc->sc_type & GJ_TYPE_COMPLETE) != GJ_TYPE_COMPLETE) {
		/* Journal is not complete yet. */
		return (gp);
	} else {
		/* Journal complete, cancel timeout. */
		callout_drain(&sc->sc_callout);
	}

	error = kproc_create(g_journal_worker, sc, &sc->sc_worker, 0, 0,
	    "g_journal %s", sc->sc_name);
	if (error != 0) {
		GJ_DEBUG(0, "Cannot create worker thread for %s.journal.",
		    sc->sc_name);
		g_journal_destroy(sc);
		return (NULL);
	}

	return (gp);
}

static void
g_journal_destroy_consumer(void *arg, int flags __unused)
{
	struct g_consumer *cp;

	g_topology_assert();
	cp = arg;
	g_detach(cp);
	g_destroy_consumer(cp);
}

static int
g_journal_destroy(struct g_journal_softc *sc)
{
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_consumer *cp;

	g_topology_assert();

	if (sc == NULL)
		return (ENXIO);

	gp = sc->sc_geom;
	pp = LIST_FIRST(&gp->provider);
	if (pp != NULL) {
		if (pp->acr != 0 || pp->acw != 0 || pp->ace != 0) {
			GJ_DEBUG(1, "Device %s is still open (r%dw%de%d).",
			    pp->name, pp->acr, pp->acw, pp->ace);
			return (EBUSY);
		}
		g_error_provider(pp, ENXIO);

		g_journal_flush(sc);
		g_journal_flush_send(sc);
		g_journal_switch(sc);
	}

	sc->sc_flags |= (GJF_DEVICE_DESTROY | GJF_DEVICE_CLEAN);

	g_topology_unlock();

	if (sc->sc_rootmount != NULL) {
		GJ_DEBUG(1, "root_mount_rel %p", sc->sc_rootmount);
		root_mount_rel(sc->sc_rootmount);
		sc->sc_rootmount = NULL;
	}

	callout_drain(&sc->sc_callout);
	mtx_lock(&sc->sc_mtx);
	wakeup(sc);
	while (sc->sc_worker != NULL)
		msleep(&sc->sc_worker, &sc->sc_mtx, PRIBIO, "gj:destroy", 0);
	mtx_unlock(&sc->sc_mtx);

	if (pp != NULL) {
		GJ_DEBUG(1, "Marking %s as clean.", sc->sc_name);
		g_journal_metadata_update(sc);
		g_topology_lock();
		g_wither_provider(pp, ENXIO);
	} else {
		g_topology_lock();
	}
	mtx_destroy(&sc->sc_mtx);

	if (sc->sc_current_count != 0) {
		GJ_DEBUG(0, "Warning! Number of current requests %d.",
		    sc->sc_current_count);
	}

	gp->softc = NULL;
	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if (cp->acr + cp->acw + cp->ace > 0)
			g_access(cp, -1, -1, -1);
		/*
		 * We keep all consumers open for writting, so if I'll detach
		 * and destroy consumer here, I'll get providers for taste, so
		 * journal will be started again.
		 * Sending an event here, prevents this from happening.
		 */
		g_post_event(g_journal_destroy_consumer, cp, M_WAITOK, NULL);
	}
	g_wither_geom(gp, ENXIO);
	free(sc, M_JOURNAL);
	return (0);
}

static void
g_journal_taste_orphan(struct g_consumer *cp)
{

	KASSERT(1 == 0, ("%s called while tasting %s.", __func__,
	    cp->provider->name));
}

static struct g_geom *
g_journal_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_journal_metadata md;
	struct g_consumer *cp;
	struct g_geom *gp;
	int error;

	g_topology_assert();
	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	GJ_DEBUG(2, "Tasting %s.", pp->name);
	if (pp->geom->class == mp)
		return (NULL);

	gp = g_new_geomf(mp, "journal:taste");
	/* This orphan function should be never called. */
	gp->orphan = g_journal_taste_orphan;
	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = g_journal_metadata_read(cp, &md);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	if (error != 0)
		return (NULL);
	gp = NULL;

	if (md.md_provider[0] != '\0' &&
	    !g_compare_names(md.md_provider, pp->name))
		return (NULL);
	if (md.md_provsize != 0 && md.md_provsize != pp->mediasize)
		return (NULL);
	if (g_journal_debug >= 2)
		journal_metadata_dump(&md);

	gp = g_journal_create(mp, pp, &md);
	return (gp);
}

static struct g_journal_softc *
g_journal_find_device(struct g_class *mp, const char *name)
{
	struct g_journal_softc *sc;
	struct g_geom *gp;
	struct g_provider *pp;

	if (strncmp(name, "/dev/", 5) == 0)
		name += 5;
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_flags & GJF_DEVICE_DESTROY)
			continue;
		if ((sc->sc_type & GJ_TYPE_COMPLETE) != GJ_TYPE_COMPLETE)
			continue;
		pp = LIST_FIRST(&gp->provider);
		if (strcmp(sc->sc_name, name) == 0)
			return (sc);
		if (pp != NULL && strcmp(pp->name, name) == 0)
			return (sc);
	}
	return (NULL);
}

static void
g_journal_ctl_destroy(struct gctl_req *req, struct g_class *mp)
{
	struct g_journal_softc *sc;
	const char *name;
	char param[16];
	int *nargs;
	int error, i;

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs <= 0) {
		gctl_error(req, "Missing device(s).");
		return;
	}

	for (i = 0; i < *nargs; i++) {
		snprintf(param, sizeof(param), "arg%d", i);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%d' argument.", i);
			return;
		}
		sc = g_journal_find_device(mp, name);
		if (sc == NULL) {
			gctl_error(req, "No such device: %s.", name);
			return;
		}
		error = g_journal_destroy(sc);
		if (error != 0) {
			gctl_error(req, "Cannot destroy device %s (error=%d).",
			    LIST_FIRST(&sc->sc_geom->provider)->name, error);
			return;
		}
	}
}

static void
g_journal_ctl_sync(struct gctl_req *req __unused, struct g_class *mp __unused)
{

	g_topology_assert();
	g_topology_unlock();
	g_journal_sync_requested++;
	wakeup(&g_journal_switcher_state);
	while (g_journal_sync_requested > 0)
		tsleep(&g_journal_sync_requested, PRIBIO, "j:sreq", hz / 2);
	g_topology_lock();
}

static void
g_journal_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_JOURNAL_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	if (strcmp(verb, "destroy") == 0 || strcmp(verb, "stop") == 0) {
		g_journal_ctl_destroy(req, mp);
		return;
	} else if (strcmp(verb, "sync") == 0) {
		g_journal_ctl_sync(req, mp);
		return;
	}

	gctl_error(req, "Unknown verb.");
}

static void
g_journal_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_journal_softc *sc;

	g_topology_assert();

	sc = gp->softc;
	if (sc == NULL)
		return;
	if (pp != NULL) {
		/* Nothing here. */
	} else if (cp != NULL) {
		int first = 1;

		sbuf_printf(sb, "%s<Role>", indent);
		if (cp == sc->sc_dconsumer) {
			sbuf_printf(sb, "Data");
			first = 0;
		}
		if (cp == sc->sc_jconsumer) {
			if (!first)
				sbuf_printf(sb, ",");
			sbuf_printf(sb, "Journal");
		}
		sbuf_printf(sb, "</Role>\n");
		if (cp == sc->sc_jconsumer) {
			sbuf_printf(sb, "<Jstart>%jd</Jstart>\n",
			    (intmax_t)sc->sc_jstart);
			sbuf_printf(sb, "<Jend>%jd</Jend>\n",
			    (intmax_t)sc->sc_jend);
		}
	} else {
		sbuf_printf(sb, "%s<ID>%u</ID>\n", indent, (u_int)sc->sc_id);
	}
}

static eventhandler_tag g_journal_event_shutdown = NULL;
static eventhandler_tag g_journal_event_lowmem = NULL;

static void
g_journal_shutdown(void *arg, int howto __unused)
{
	struct g_class *mp;
	struct g_geom *gp, *gp2;

	if (panicstr != NULL)
		return;
	mp = arg;
	g_topology_lock();
	LIST_FOREACH_SAFE(gp, &mp->geom, geom, gp2) {
		if (gp->softc == NULL)
			continue;
		GJ_DEBUG(0, "Shutting down geom %s.", gp->name);
		g_journal_destroy(gp->softc);
	}
	g_topology_unlock();
}

/*
 * Free cached requests from inactive queue in case of low memory.
 * We free GJ_FREE_AT_ONCE elements at once.
 */
#define	GJ_FREE_AT_ONCE	4
static void
g_journal_lowmem(void *arg, int howto __unused)
{
	struct g_journal_softc *sc;
	struct g_class *mp;
	struct g_geom *gp;
	struct bio *bp;
	u_int nfree = GJ_FREE_AT_ONCE;

	g_journal_stats_low_mem++;
	mp = arg;
	g_topology_lock();
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL || (sc->sc_flags & GJF_DEVICE_DESTROY))
			continue;
		mtx_lock(&sc->sc_mtx);
		for (bp = sc->sc_inactive.jj_queue; nfree > 0 && bp != NULL;
		    nfree--, bp = bp->bio_next) {
			/*
			 * This is safe to free the bio_data, because:
			 * 1. If bio_data is NULL it will be read from the
			 *    inactive journal.
			 * 2. If bp is sent down, it is first removed from the
			 *    inactive queue, so it's impossible to free the
			 *    data from under in-flight bio.
			 * On the other hand, freeing elements from the active
			 * queue, is not safe.
			 */
			if (bp->bio_data != NULL) {
				GJ_DEBUG(2, "Freeing data from %s.",
				    sc->sc_name);
				gj_free(bp->bio_data, bp->bio_length);
				bp->bio_data = NULL;
			}
		}
		mtx_unlock(&sc->sc_mtx);
		if (nfree == 0)
			break;
	}
	g_topology_unlock();
}

static void g_journal_switcher(void *arg);

static void
g_journal_init(struct g_class *mp)
{

	/* Pick a conservative value if provided value sucks. */
	if (g_journal_cache_divisor <= 0 ||
	    (vm_kmem_size / g_journal_cache_divisor == 0)) {
		g_journal_cache_divisor = 5;
	}
	if (g_journal_cache_limit > 0) {
		g_journal_cache_limit = vm_kmem_size / g_journal_cache_divisor;
		g_journal_cache_low =
		    (g_journal_cache_limit / 100) * g_journal_cache_switch;
	}
	g_journal_event_shutdown = EVENTHANDLER_REGISTER(shutdown_post_sync,
	    g_journal_shutdown, mp, EVENTHANDLER_PRI_FIRST);
	if (g_journal_event_shutdown == NULL)
		GJ_DEBUG(0, "Warning! Cannot register shutdown event.");
	g_journal_event_lowmem = EVENTHANDLER_REGISTER(vm_lowmem,
	    g_journal_lowmem, mp, EVENTHANDLER_PRI_FIRST);
	if (g_journal_event_lowmem == NULL)
		GJ_DEBUG(0, "Warning! Cannot register lowmem event.");
}

static void
g_journal_fini(struct g_class *mp)
{

	if (g_journal_event_shutdown != NULL) {
		EVENTHANDLER_DEREGISTER(shutdown_post_sync,
		    g_journal_event_shutdown);
	}
	if (g_journal_event_lowmem != NULL)
		EVENTHANDLER_DEREGISTER(vm_lowmem, g_journal_event_lowmem);
	g_journal_stop_switcher();
}

DECLARE_GEOM_CLASS(g_journal_class, g_journal);

static const struct g_journal_desc *
g_journal_find_desc(const char *fstype)
{
	const struct g_journal_desc *desc;
	int i;

	for (desc = g_journal_filesystems[i = 0]; desc != NULL;
	     desc = g_journal_filesystems[++i]) {
		if (strcmp(desc->jd_fstype, fstype) == 0)
			break;
	}
	return (desc);
}

static void
g_journal_switch_wait(struct g_journal_softc *sc)
{
	struct bintime bt;

	mtx_assert(&sc->sc_mtx, MA_OWNED);
	if (g_journal_debug >= 2) {
		if (sc->sc_flush_in_progress > 0) {
			GJ_DEBUG(2, "%d requests flushing.",
			    sc->sc_flush_in_progress);
		}
		if (sc->sc_copy_in_progress > 0) {
			GJ_DEBUG(2, "%d requests copying.",
			    sc->sc_copy_in_progress);
		}
		if (sc->sc_flush_count > 0) {
			GJ_DEBUG(2, "%d requests to flush.",
			    sc->sc_flush_count);
		}
		if (sc->sc_delayed_count > 0) {
			GJ_DEBUG(2, "%d requests delayed.",
			    sc->sc_delayed_count);
		}
	}
	g_journal_stats_switches++;
	if (sc->sc_copy_in_progress > 0)
		g_journal_stats_wait_for_copy++;
	GJ_TIMER_START(1, &bt);
	sc->sc_flags &= ~GJF_DEVICE_BEFORE_SWITCH;
	sc->sc_flags |= GJF_DEVICE_SWITCH;
	wakeup(sc);
	while (sc->sc_flags & GJF_DEVICE_SWITCH) {
		msleep(&sc->sc_journal_copying, &sc->sc_mtx, PRIBIO,
		    "gj:switch", 0);
	}
	GJ_TIMER_STOP(1, &bt, "Switch time of %s", sc->sc_name);
}

static void
g_journal_do_switch(struct g_class *classp)
{
	struct g_journal_softc *sc;
	const struct g_journal_desc *desc;
	struct g_geom *gp;
	struct mount *mp;
	struct bintime bt;
	char *mountpoint;
	int error, save;

	g_topology_lock();
	LIST_FOREACH(gp, &classp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_flags & GJF_DEVICE_DESTROY)
			continue;
		if ((sc->sc_type & GJ_TYPE_COMPLETE) != GJ_TYPE_COMPLETE)
			continue;
		mtx_lock(&sc->sc_mtx);
		sc->sc_flags |= GJF_DEVICE_BEFORE_SWITCH;
		mtx_unlock(&sc->sc_mtx);
	}
	g_topology_unlock();

	mtx_lock(&mountlist_mtx);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		if (mp->mnt_gjprovider == NULL)
			continue;
		if (mp->mnt_flag & MNT_RDONLY)
			continue;
		desc = g_journal_find_desc(mp->mnt_stat.f_fstypename);
		if (desc == NULL)
			continue;
		if (vfs_busy(mp, MBF_NOWAIT | MBF_MNTLSTLOCK))
			continue;
		/* mtx_unlock(&mountlist_mtx) was done inside vfs_busy() */

		g_topology_lock();
		sc = g_journal_find_device(classp, mp->mnt_gjprovider);
		g_topology_unlock();

		if (sc == NULL) {
			GJ_DEBUG(0, "Cannot find journal geom for %s.",
			    mp->mnt_gjprovider);
			goto next;
		} else if (JEMPTY(sc)) {
			mtx_lock(&sc->sc_mtx);
			sc->sc_flags &= ~GJF_DEVICE_BEFORE_SWITCH;
			mtx_unlock(&sc->sc_mtx);
			GJ_DEBUG(3, "No need for %s switch.", sc->sc_name);
			goto next;
		}

		mountpoint = mp->mnt_stat.f_mntonname;

		error = vn_start_write(NULL, &mp, V_WAIT);
		if (error != 0) {
			GJ_DEBUG(0, "vn_start_write(%s) failed (error=%d).",
			    mountpoint, error);
			goto next;
		}

		save = curthread_pflags_set(TDP_SYNCIO);

		GJ_TIMER_START(1, &bt);
		vfs_msync(mp, MNT_NOWAIT);
		GJ_TIMER_STOP(1, &bt, "Msync time of %s", mountpoint);

		GJ_TIMER_START(1, &bt);
		error = VFS_SYNC(mp, MNT_NOWAIT);
		if (error == 0)
			GJ_TIMER_STOP(1, &bt, "Sync time of %s", mountpoint);
		else {
			GJ_DEBUG(0, "Cannot sync file system %s (error=%d).",
			    mountpoint, error);
		}

		curthread_pflags_restore(save);

		vn_finished_write(mp);

		if (error != 0)
			goto next;

		/*
		 * Send BIO_FLUSH before freezing the file system, so it can be
		 * faster after the freeze.
		 */
		GJ_TIMER_START(1, &bt);
		g_journal_flush_cache(sc);
		GJ_TIMER_STOP(1, &bt, "BIO_FLUSH time of %s", sc->sc_name);

		GJ_TIMER_START(1, &bt);
		error = vfs_write_suspend(mp, VS_SKIP_UNMOUNT);
		GJ_TIMER_STOP(1, &bt, "Suspend time of %s", mountpoint);
		if (error != 0) {
			GJ_DEBUG(0, "Cannot suspend file system %s (error=%d).",
			    mountpoint, error);
			goto next;
		}

		error = desc->jd_clean(mp);
		if (error != 0)
			goto next;

		mtx_lock(&sc->sc_mtx);
		g_journal_switch_wait(sc);
		mtx_unlock(&sc->sc_mtx);

		vfs_write_resume(mp, 0);
next:
		mtx_lock(&mountlist_mtx);
		vfs_unbusy(mp);
	}
	mtx_unlock(&mountlist_mtx);

	sc = NULL;
	for (;;) {
		g_topology_lock();
		LIST_FOREACH(gp, &g_journal_class.geom, geom) {
			sc = gp->softc;
			if (sc == NULL)
				continue;
			mtx_lock(&sc->sc_mtx);
			if ((sc->sc_type & GJ_TYPE_COMPLETE) == GJ_TYPE_COMPLETE &&
			    !(sc->sc_flags & GJF_DEVICE_DESTROY) &&
			    (sc->sc_flags & GJF_DEVICE_BEFORE_SWITCH)) {
				break;
			}
			mtx_unlock(&sc->sc_mtx);
			sc = NULL;
		}
		g_topology_unlock();
		if (sc == NULL)
			break;
		mtx_assert(&sc->sc_mtx, MA_OWNED);
		g_journal_switch_wait(sc);
		mtx_unlock(&sc->sc_mtx);
	}
}

static void
g_journal_start_switcher(struct g_class *mp)
{
	int error;

	g_topology_assert();
	MPASS(g_journal_switcher_proc == NULL);
	g_journal_switcher_state = GJ_SWITCHER_WORKING;
	error = kproc_create(g_journal_switcher, mp, &g_journal_switcher_proc,
	    0, 0, "g_journal switcher");
	KASSERT(error == 0, ("Cannot create switcher thread."));
}

static void
g_journal_stop_switcher(void)
{
	g_topology_assert();
	MPASS(g_journal_switcher_proc != NULL);
	g_journal_switcher_state = GJ_SWITCHER_DIE;
	wakeup(&g_journal_switcher_state);
	while (g_journal_switcher_state != GJ_SWITCHER_DIED)
		tsleep(&g_journal_switcher_state, PRIBIO, "jfini:wait", hz / 5);
	GJ_DEBUG(1, "Switcher died.");
	g_journal_switcher_proc = NULL;
}

/*
 * TODO: Kill switcher thread on last geom destruction?
 */
static void
g_journal_switcher(void *arg)
{
	struct g_class *mp;
	struct bintime bt;
	int error;

	mp = arg;
	curthread->td_pflags |= TDP_NORUNNINGBUF;
	for (;;) {
		g_journal_switcher_wokenup = 0;
		error = tsleep(&g_journal_switcher_state, PRIBIO, "jsw:wait",
		    g_journal_switch_time * hz);
		if (g_journal_switcher_state == GJ_SWITCHER_DIE) {
			g_journal_switcher_state = GJ_SWITCHER_DIED;
			GJ_DEBUG(1, "Switcher exiting.");
			wakeup(&g_journal_switcher_state);
			kproc_exit(0);
		}
		if (error == 0 && g_journal_sync_requested == 0) {
			GJ_DEBUG(1, "Out of cache, force switch (used=%jd "
			    "limit=%jd).", (intmax_t)g_journal_cache_used,
			    (intmax_t)g_journal_cache_limit);
		}
		GJ_TIMER_START(1, &bt);
		g_journal_do_switch(mp);
		GJ_TIMER_STOP(1, &bt, "Entire switch time");
		if (g_journal_sync_requested > 0) {
			g_journal_sync_requested = 0;
			wakeup(&g_journal_sync_requested);
		}
	}
}
