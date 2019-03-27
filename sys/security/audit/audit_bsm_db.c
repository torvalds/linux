/*
 * Copyright (c) 1999-2009 Apple Inc.
 * Copyright (c) 2005, 2016-2018 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/fcntl.h>
#include <sys/filedesc.h>
#include <sys/libkern.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sem.h>
#include <sys/sbuf.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vnode.h>

#include <bsm/audit.h>
#include <bsm/audit_kevents.h>
#include <security/audit/audit.h>
#include <security/audit/audit_private.h>

/*
 * Hash table functions for the audit event number to event class mask
 * mapping.
 */
#define	EVCLASSMAP_HASH_TABLE_SIZE	251
struct evclass_elem {
	au_event_t event;
	au_class_t class;
	LIST_ENTRY(evclass_elem) entry;
};
struct evclass_list {
	LIST_HEAD(, evclass_elem) head;
};

static MALLOC_DEFINE(M_AUDITEVCLASS, "audit_evclass", "Audit event class");
static struct rwlock		evclass_lock;
static struct evclass_list	evclass_hash[EVCLASSMAP_HASH_TABLE_SIZE];

#define	EVCLASS_LOCK_INIT()	rw_init(&evclass_lock, "evclass_lock")
#define	EVCLASS_RLOCK()		rw_rlock(&evclass_lock)
#define	EVCLASS_RUNLOCK()	rw_runlock(&evclass_lock)
#define	EVCLASS_WLOCK()		rw_wlock(&evclass_lock)
#define	EVCLASS_WUNLOCK()	rw_wunlock(&evclass_lock)

/*
 * Hash table maintaining a mapping from audit event numbers to audit event
 * names.  For now, used only by DTrace, but present always so that userspace
 * tools can register and inspect fields consistently even if DTrace is not
 * present.
 *
 * struct evname_elem is defined in audit_private.h so that audit_dtrace.c can
 * use the definition.
 */
#define	EVNAMEMAP_HASH_TABLE_MODULE	"etc_security_audit_event"
#define	EVNAMEMAP_HASH_TABLE_SIZE	251
struct evname_list {
	LIST_HEAD(, evname_elem)	enl_head;
};

static MALLOC_DEFINE(M_AUDITEVNAME, "audit_evname", "Audit event name");
static struct sx		evnamemap_lock;
static struct evname_list	evnamemap_hash[EVNAMEMAP_HASH_TABLE_SIZE];

#define	EVNAMEMAP_LOCK_INIT()	sx_init(&evnamemap_lock, "evnamemap_lock");
#define	EVNAMEMAP_RLOCK()	sx_slock(&evnamemap_lock)
#define	EVNAMEMAP_RUNLOCK()	sx_sunlock(&evnamemap_lock)
#define	EVNAMEMAP_WLOCK()	sx_xlock(&evnamemap_lock)
#define	EVNAMEMAP_WUNLOCK()	sx_xunlock(&evnamemap_lock)

/*
 * Look up the class for an audit event in the class mapping table.
 */
au_class_t
au_event_class(au_event_t event)
{
	struct evclass_list *evcl;
	struct evclass_elem *evc;
	au_class_t class;

	EVCLASS_RLOCK();
	evcl = &evclass_hash[event % EVCLASSMAP_HASH_TABLE_SIZE];
	class = 0;
	LIST_FOREACH(evc, &evcl->head, entry) {
		if (evc->event == event) {
			class = evc->class;
			goto out;
		}
	}
out:
	EVCLASS_RUNLOCK();
	return (class);
}

/*
 * Insert a event to class mapping. If the event already exists in the
 * mapping, then replace the mapping with the new one.
 *
 * XXX There is currently no constraints placed on the number of mappings.
 * May want to either limit to a number, or in terms of memory usage.
 */
void
au_evclassmap_insert(au_event_t event, au_class_t class)
{
	struct evclass_list *evcl;
	struct evclass_elem *evc, *evc_new;

	/*
	 * Pessimistically, always allocate storage before acquiring mutex.
	 * Free if there is already a mapping for this event.
	 */
	evc_new = malloc(sizeof(*evc), M_AUDITEVCLASS, M_WAITOK);

	EVCLASS_WLOCK();
	evcl = &evclass_hash[event % EVCLASSMAP_HASH_TABLE_SIZE];
	LIST_FOREACH(evc, &evcl->head, entry) {
		if (evc->event == event) {
			evc->class = class;
			EVCLASS_WUNLOCK();
			free(evc_new, M_AUDITEVCLASS);
			return;
		}
	}
	evc = evc_new;
	evc->event = event;
	evc->class = class;
	LIST_INSERT_HEAD(&evcl->head, evc, entry);
	EVCLASS_WUNLOCK();
}

void
au_evclassmap_init(void)
{
	int i;

	EVCLASS_LOCK_INIT();
	for (i = 0; i < EVCLASSMAP_HASH_TABLE_SIZE; i++)
		LIST_INIT(&evclass_hash[i].head);

	/*
	 * Set up the initial event to class mapping for system calls.
	 *
	 * XXXRW: Really, this should walk all possible audit events, not all
	 * native ABI system calls, as there may be audit events reachable
	 * only through non-native system calls.  It also seems a shame to
	 * frob the mutex this early.
	 */
	for (i = 0; i < SYS_MAXSYSCALL; i++) {
		if (sysent[i].sy_auevent != AUE_NULL)
			au_evclassmap_insert(sysent[i].sy_auevent, 0);
	}
}

/*
 * Look up the name for an audit event in the event-to-name mapping table.
 */
int
au_event_name(au_event_t event, char *name)
{
	struct evname_list *enl;
	struct evname_elem *ene;
	int error;

	error = ENOENT;
	EVNAMEMAP_RLOCK();
	enl = &evnamemap_hash[event % EVNAMEMAP_HASH_TABLE_SIZE];
	LIST_FOREACH(ene, &enl->enl_head, ene_entry) {
		if (ene->ene_event == event) {
			strlcpy(name, ene->ene_name, EVNAMEMAP_NAME_SIZE);
			error = 0;
			goto out;
		}
	}
out:
	EVNAMEMAP_RUNLOCK();
	return (error);
}

/*
 * Insert a event-to-name mapping.  If the event already exists in the
 * mapping, then replace the mapping with the new one.
 *
 * XXX There is currently no constraints placed on the number of mappings.
 * May want to either limit to a number, or in terms of memory usage.
 *
 * XXXRW: Accepts truncated name -- but perhaps should return failure instead?
 *
 * XXXRW: It could be we need a way to remove existing names...?
 *
 * XXXRW: We handle collisions between numbers, but I wonder if we also need a
 * way to handle name collisions, for DTrace, where probe names must be
 * unique?
 */
void
au_evnamemap_insert(au_event_t event, const char *name)
{
	struct evname_list *enl;
	struct evname_elem *ene, *ene_new;

	/*
	 * Pessimistically, always allocate storage before acquiring lock.
	 * Free if there is already a mapping for this event.
	 */
	ene_new = malloc(sizeof(*ene_new), M_AUDITEVNAME, M_WAITOK | M_ZERO);
	EVNAMEMAP_WLOCK();
	enl = &evnamemap_hash[event % EVNAMEMAP_HASH_TABLE_SIZE];
	LIST_FOREACH(ene, &enl->enl_head, ene_entry) {
		if (ene->ene_event == event) {
			EVNAME_LOCK(ene);
			(void)strlcpy(ene->ene_name, name,
			    sizeof(ene->ene_name));
			EVNAME_UNLOCK(ene);
			EVNAMEMAP_WUNLOCK();
			free(ene_new, M_AUDITEVNAME);
			return;
		}
	}
	ene = ene_new;
	mtx_init(&ene->ene_lock, "au_evnamemap", NULL, MTX_DEF);
	ene->ene_event = event;
	(void)strlcpy(ene->ene_name, name, sizeof(ene->ene_name));
	LIST_INSERT_HEAD(&enl->enl_head, ene, ene_entry);
	EVNAMEMAP_WUNLOCK();
}

/*
 * If /etc/security/audit_event has been preloaded by the boot loader, parse
 * it to build an initial set of event number<->name mappings.
 */
static void
au_evnamemap_init_preload(void)
{
	caddr_t kmdp;
	char *endptr, *line, *nextline, *ptr;
	const char *evnum_str, *evname;
	size_t size;
	long evnum;
	u_int lineno;

	kmdp = preload_search_by_type(EVNAMEMAP_HASH_TABLE_MODULE);
	if (kmdp == NULL)
		return;
	ptr = preload_fetch_addr(kmdp);
	size = preload_fetch_size(kmdp);

	/*
	 * Parse preloaded configuration file "in place".  Assume that the
	 * last character is a new line, meaning that we can replace it with a
	 * nul byte safely.  We can then use strsep(3) to process the full
	 * buffer.
	 */
	ptr[size - 1] = '\0';

	/*
	 * Process line by line.
	 */
	nextline = ptr;
	lineno = 0;
	while ((line = strsep(&nextline, "\n")) != NULL) {
		/*
		 * Skip any leading white space.
		 */
		while (line[0] == ' ' || line[0] == '\t')
			line++;

		/*
		 * Skip blank lines and comment lines.
		 */
		if (line[0] == '\0' || line[0] == '#') {
			lineno++;
			continue;
		}

		/*
		 * Parse each line -- ":"-separated tuple of event number,
		 * event name, and other material we are less interested in.
		 */
		evnum_str = strsep(&line, ":");
		if (evnum_str == NULL || *evnum_str == '\0') {
			printf("%s: Invalid line %u - evnum strsep\n",
			    __func__, lineno);
			lineno++;
			continue;
		}
		evnum = strtol(evnum_str, &endptr, 10);
		if (*evnum_str == '\0' || *endptr != '\0' ||
		    evnum <= 0 || evnum > UINT16_MAX) {
			printf("%s: Invalid line %u - evnum strtol\n",
			    __func__, lineno);
			lineno++;
			continue;
		}
		evname = strsep(&line, ":");
		if (evname == NULL || *evname == '\0') {
			printf("%s: Invalid line %u - evname strsp\n",
			    __func__, lineno);
			lineno++;
			continue;
		}
		au_evnamemap_insert(evnum, evname);
		lineno++;
	}
}

void
au_evnamemap_init(void)
{
	int i;

	EVNAMEMAP_LOCK_INIT();
	for (i = 0; i < EVNAMEMAP_HASH_TABLE_SIZE; i++)
		LIST_INIT(&evnamemap_hash[i].enl_head);
	au_evnamemap_init_preload();
}

/*
 * The DTrace audit provider occasionally needs to walk the entries in the
 * event-to-name mapping table, and uses this public interface to do so.  A
 * write lock is acquired so that the provider can safely update its fields in
 * table entries.
 */
void
au_evnamemap_foreach(au_evnamemap_callback_t callback)
{
	struct evname_list *enl;
	struct evname_elem *ene;
	int i;

	EVNAMEMAP_WLOCK();
	for (i = 0; i < EVNAMEMAP_HASH_TABLE_SIZE; i++) {
		enl = &evnamemap_hash[i];
		LIST_FOREACH(ene, &enl->enl_head, ene_entry)
			callback(ene);
	}
	EVNAMEMAP_WUNLOCK();
}

#ifdef KDTRACE_HOOKS
/*
 * Look up an event-to-name mapping table entry by event number.  As evname
 * elements are stable in memory, we can return the pointer without the table
 * lock held -- but the caller will need to lock the element mutex before
 * accessing element fields.
 *
 * NB: the event identifier in elements is stable and can be read without
 * holding the evname_elem lock.
 */
struct evname_elem *
au_evnamemap_lookup(au_event_t event)
{
	struct evname_list *enl;
	struct evname_elem *ene;

	EVNAMEMAP_RLOCK();
	enl = &evnamemap_hash[event % EVNAMEMAP_HASH_TABLE_SIZE];
	LIST_FOREACH(ene, &enl->enl_head, ene_entry) {
		if (ene->ene_event == event)
			goto out;
	}
	ene = NULL;
out:
	EVNAMEMAP_RUNLOCK();
	return (ene);
}
#endif /* !KDTRACE_HOOKS */
