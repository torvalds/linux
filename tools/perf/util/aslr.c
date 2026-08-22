// SPDX-License-Identifier: GPL-2.0
#include "aslr.h"

#include "addr_location.h"
#include "debug.h"
#include "event.h"
#include "evsel.h"
#include "evlist.h"
#include "machine.h"
#include "map.h"
#include "thread.h"
#include "tool.h"
#include "session.h"
#include "data.h"
#include "dso.h"
#include "pmu.h"
#include "pmus.h"

#include <internal/lib.h>  /* page_size */
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <byteswap.h>

/**
 * struct remap_addresses_key - Key for mapping original addresses to remapped ones.
 * @dso: Pointer to the DSO (Dynamic Shared Object) associated with the mapping.
 * @invariant: Unique offset invariant within the VMA (Virtual Memory Area).
 *             Calculated as `start - pgoff`. This value remains constant when
 *             perf's internal `maps__fixup_overlap_and_insert` splits a map into
 *             fragmented VMA pieces due to overlapping events, allowing us to
 *             resolve split maps consistently back to the original VMA.
 * @pid: Process ID associated with the mapping.
 */
struct remap_addresses_key {
	struct machine *machine;
	struct dso *dso;
	u64 invariant;
	pid_t pid;
};

struct aslr_mapping {
	struct list_head node;
	u64 orig_start;
	u64 len;
	u64 remap_start;
};

struct aslr_evsel_priv {
	u64 orig_sample_type;
	u64 orig_sample_regs_user;
	u64 orig_sample_regs_intr;
	int orig_sample_size;
};

static size_t evsel_hash(long key, void *ctx __maybe_unused)
{
	return (size_t)key;
}

static bool evsel_equal(long key1, long key2, void *ctx __maybe_unused)
{
	return key1 == key2;
}

struct process_top_address {
	u64 remapped_max;
};
struct aslr_tool {
	/** @tool: The tool implemented here and a pointer to a delegate to process the data. */
	struct delegate_tool tool;
	/** @machines: The machines with the input, not remapped, virtual address layout. */
	struct machines machines;
	/** @event_copy: Buffer used to create an event to pass to the delegate. */
	char event_copy[PERF_SAMPLE_MAX_SIZE] __aligned(8);
	/** @remap_addresses: mapping from remap_addresses_key to remapped address. */
	struct hashmap remap_addresses;
	/** @top_addresses: mapping from process to max remapped address. */
	struct hashmap top_addresses;
	/**
	 * @evsel_orig_attrs: mapping from evsel pointer to its original
	 *                    unstripped sample_type and registers bitmasks.
	 */
	struct hashmap evsel_orig_attrs;
};

static const pid_t kernel_pid = -1;

/* Start remapping user processes from a small non-zero offset. */
static const u64 user_space_start = 0x200000;
static const u64 kernel_space_start_64 = 0xffff800010000000ULL;
static const u64 kernel_space_start_32 = 0x80000000ULL;

static size_t remap_addresses__hash(long _key, void *ctx __maybe_unused)
{
	struct remap_addresses_key *key = (struct remap_addresses_key *)_key;
	void *dso_ptr = key->dso ? RC_CHK_ACCESS(key->dso) : NULL;

	return (size_t)key->machine ^ (size_t)dso_ptr ^ key->invariant ^ key->pid;
}

static bool remap_addresses__equal(long _key1, long _key2, void *ctx __maybe_unused)
{
	struct remap_addresses_key *key1 = (struct remap_addresses_key *)_key1;
	struct remap_addresses_key *key2 = (struct remap_addresses_key *)_key2;

	return key1->machine == key2->machine &&
	       RC_CHK_EQUAL(key1->dso, key2->dso) &&
	       key1->invariant == key2->invariant &&
	       key1->pid == key2->pid;
}

struct top_addresses_key {
	struct machine *machine;
	pid_t pid;
};

static size_t top_addresses__hash(long _key, void *ctx __maybe_unused)
{
	struct top_addresses_key *key = (struct top_addresses_key *)_key;

	return (size_t)key->machine ^ key->pid;
}

static bool top_addresses__equal(long _key1, long _key2, void *ctx __maybe_unused)
{
	struct top_addresses_key *key1 = (struct top_addresses_key *)_key1;
	struct top_addresses_key *key2 = (struct top_addresses_key *)_key2;

	return key1->machine == key2->machine && key1->pid == key2->pid;
}

static u64 round_up_to_page_size(u64 addr)
{
	return (addr + page_size - 1) & ~((u64)page_size - 1);
}

static u64 aslr_tool__remap_address(struct aslr_tool *aslr,
				    struct thread *aslr_thread,
				    u8 cpumode,
				    u64 addr)
{
	struct addr_location al;
	struct remap_addresses_key key;
	u64 *remapped_invariant_ptr = NULL;
	u64 remap_addr = 0;
	u8 effective_cpumode = cpumode;
	struct dso *dso;
	const char *dso_name;

	if (!aslr_thread)
		return 0; /* No thread. */

	addr_location__init(&al);
	if (!thread__find_map(aslr_thread, cpumode, addr, &al)) {
		/*
		 * If lookup fails with specified cpumode, try fallback to the other space
		 * to be robust against bad cpumode in samples.
		 */
		if (cpumode == PERF_RECORD_MISC_KERNEL)
			effective_cpumode = PERF_RECORD_MISC_USER;
		else if (cpumode == PERF_RECORD_MISC_USER)
			effective_cpumode = PERF_RECORD_MISC_KERNEL;
		else if (cpumode == PERF_RECORD_MISC_GUEST_KERNEL)
			effective_cpumode = PERF_RECORD_MISC_GUEST_USER;
		else if (cpumode == PERF_RECORD_MISC_GUEST_USER)
			effective_cpumode = PERF_RECORD_MISC_GUEST_KERNEL;

		if (!thread__find_map(aslr_thread, effective_cpumode, addr, &al)) {
			addr_location__exit(&al);
			return 0; /* No mmap. */
		}
	}

	dso = map__dso(al.map);
	dso_name = dso ? dso__long_name(dso) : NULL;

	key.machine = maps__machine(thread__maps(aslr_thread));
	key.dso = dso;
	if (dso && !is_anon_memory(dso_name) && !is_no_dso_memory(dso_name))
		key.invariant = map__start(al.map) - map__pgoff(al.map);
	else
		key.invariant = map__start(al.map);
	key.pid = (effective_cpumode == PERF_RECORD_MISC_KERNEL ||
		   effective_cpumode == PERF_RECORD_MISC_GUEST_KERNEL) ?
		  kernel_pid : thread__pid(aslr_thread);

	if (hashmap__find(&aslr->remap_addresses, &key, &remapped_invariant_ptr)) {
		remap_addr = *remapped_invariant_ptr + map__pgoff(al.map) +
			     (addr - map__start(al.map));
	} else {
		pr_debug("Cannot find a remapped entry for address %" PRIx64 " in mapping %" PRIx64 "(%zu) for pid=%d\n",
			 addr, map__start(al.map), map__size(al.map), key.pid);
	}

	addr_location__exit(&al);
	return remap_addr;
}

struct aslr_machine_priv {
	bool kernel_maps_loaded;
};

static int aslr_tool__preload_kernel_maps(struct machine *machine)
{
	struct aslr_machine_priv *mpriv = machine->priv;

	if (!mpriv) {
		mpriv = zalloc(sizeof(*mpriv));
		if (!mpriv)
			return -ENOMEM;
		machine->priv = mpriv;
	}

	if (!mpriv->kernel_maps_loaded) {
		struct maps *kmaps = machine__kernel_maps(machine);

		if (kmaps) {
			int err = maps__load_maps(kmaps);

			if (err < 0) {
				pr_err("ASLR: Failed to preload kernel maps for machine pid %d\n",
				       machine->pid);
				return err;
			}
		}
		mpriv->kernel_maps_loaded = true;
	}
	return 0;
}

static void aslr_tool__free_machine_priv(struct machine *machine)
{
	free(machine->priv);
	machine->priv = NULL;
}

static void aslr_tool__destroy_machines_priv(struct machines *machines)
{
	struct rb_node *nd;

	aslr_tool__free_machine_priv(&machines->host);
	for (nd = rb_first_cached(&machines->guests); nd; nd = rb_next(nd)) {
		struct machine *machine = rb_entry(nd, struct machine, rb_node);

		aslr_tool__free_machine_priv(machine);
	}
}

static u64 aslr_tool__findnew_mapping(struct aslr_tool *aslr,
				      struct machine *session_machine,
				      struct thread *aslr_thread,
				      u8 cpumode, u64 start,
				      u64 len, u64 pgoff)
{
	/* Address location for dso lookup. */
	struct addr_location al;
	/* Original ASLR address based key for the remap table. */
	struct remap_addresses_key remap_key;
	/* The address in the ASLR sanitized address space less pg_off. */
	u64 *remapped_invariant_ptr;
	/* Key for the maximum address in a process. */
	struct top_addresses_key top_addr_key;
	/* Value in top address table. */
	struct process_top_address *top = NULL;
	/* Address in ASLR sanitized address space. */
	u64 remap_addr;
	/* Potentially allocated remap table key. */
	struct remap_addresses_key *new_remap_key = NULL;
	/*
	 * Potentially allocated remap table key.
	 * TODO: Avoid allocation necessary for perf 32-bit binary support.
	 */
	u64 *new_remap_val = NULL;
	int err;

	if (!aslr_thread)
		return 0;

	/* The key to look up an incoming address to the outgoing value. */
	addr_location__init(&al);
	remap_key.machine = maps__machine(thread__maps(aslr_thread));
	remap_key.pid = (cpumode == PERF_RECORD_MISC_KERNEL ||
			 cpumode == PERF_RECORD_MISC_GUEST_KERNEL) ?
			kernel_pid : thread__pid(aslr_thread);
	if (thread__find_map(aslr_thread, cpumode, start, &al)) {
		struct dso *dso = map__dso(al.map);
		const char *dso_name = dso ? dso__long_name(dso) : NULL;

		remap_key.dso = dso;
		if (dso && !is_anon_memory(dso_name) && !is_no_dso_memory(dso_name))
			remap_key.invariant = map__start(al.map) - map__pgoff(al.map);
		else
			remap_key.invariant = map__start(al.map);
	} else {
		remap_key.dso = NULL;
		remap_key.invariant = start;
	}

	/* The key to look up top allocated address. */
	top_addr_key.machine = remap_key.machine;
	top_addr_key.pid = remap_key.pid;

	if (hashmap__find(&aslr->remap_addresses, &remap_key, &remapped_invariant_ptr)) {
		/* Mmap already exists. */
		u64 calculated_max;

		if (al.map) {
			/*
			 * The cached value is the base of the invariant. We add the
			 * offset into the VMA (start - map__start), plus the map's
			 * pgoff, to get the precise virtual address within this chunk.
			 */
			remap_addr = *remapped_invariant_ptr + map__pgoff(al.map) +
				     (start - map__start(al.map));
		} else {
			/*
			 * For unmapped memory (e.g. kernel anonymous), the cached value
			 * was stored offset by pgoff. Adding pgoff yields the true remap_addr.
			 */
			remap_addr = *remapped_invariant_ptr + pgoff;
		}

		calculated_max = remap_addr + len;

		/* See if top mapping was expanded. */
		if (hashmap__find(&aslr->top_addresses, &top_addr_key, &top)) {
			if (calculated_max > top->remapped_max)
				top->remapped_max = calculated_max;
		}
		addr_location__exit(&al);
		return remap_addr;
	}
	/* No mmap, create an entry from the top address. */
	if (hashmap__find(&aslr->top_addresses, &top_addr_key, &top)) {
		struct addr_location prev_al;
		bool is_contiguous = false;

		/* Current max allocated mmap address within the process. */
		remap_addr = top->remapped_max;

		addr_location__init(&prev_al);
		if (thread__find_map(aslr_thread, cpumode, start - 1, &prev_al)) {
			if (map__end(prev_al.map) == start)
				is_contiguous = true;
		}
		addr_location__exit(&prev_al);

		if (is_contiguous) {
			/* Contiguous mapping, do not add 1 page gap! */
			remap_addr = round_up_to_page_size(remap_addr);
		} else {
			/* Give 1 page gap from current max page. */
			remap_addr = round_up_to_page_size(remap_addr);
			remap_addr += page_size;
		}
		if (remap_addr + len > top->remapped_max)
			top->remapped_max = remap_addr + len;
	} else {
		/* First address of the process, allocate key and first top address. */
		struct top_addresses_key *tk;
		struct process_top_address *top_val;
		struct perf_env *env = session_machine ? session_machine->env : NULL;
		bool is_64 = env ? perf_env__kernel_is_64_bit(env) : (sizeof(void *) == 8);
		u64 kernel_start_addr = is_64 ? kernel_space_start_64 : kernel_space_start_32;

		remap_addr = (cpumode == PERF_RECORD_MISC_KERNEL ||
			      cpumode == PERF_RECORD_MISC_GUEST_KERNEL) ?
			     kernel_start_addr : user_space_start;
		remap_addr = round_up_to_page_size(remap_addr);

		tk = malloc(sizeof(*tk));
		top_val = malloc(sizeof(*top_val));
		if (!tk || !top_val) {
			err = -ENOMEM;
		} else {
			*tk = top_addr_key;
			top_val->remapped_max = remap_addr + len;
			err = hashmap__insert(&aslr->top_addresses, tk, top_val,
					      HASHMAP_ADD, NULL, NULL);
		}
		if (err) {
			errno = -err;
			pr_err("Failure to add ASLR process top address %m\n");
			free(tk);
			free(top_val);
			addr_location__exit(&al);
			return 0;
		}
	}
	/* Create rmeapping entry. */
	new_remap_key = malloc(sizeof(*new_remap_key));
	new_remap_val = malloc(sizeof(u64));
	if (!new_remap_key || !new_remap_val) {
		err = -ENOMEM;
	} else {
		*new_remap_key = remap_key;
		new_remap_key->dso = dso__get(remap_key.dso);
		if (cpumode == PERF_RECORD_MISC_KERNEL ||
		    cpumode == PERF_RECORD_MISC_GUEST_KERNEL) {
			if (al.map) {
				*new_remap_val = remap_addr -
						 (start - map__start(al.map)) -
						 map__pgoff(al.map);
			} else {
				/*
				 * Subtract pgoff from the base virtual address so that
				 * when the lookup path adds pgoff back, it perfectly
				 * cancels out and returns remap_addr.
				 */
				*new_remap_val = remap_addr - pgoff;
			}
		} else {
			*new_remap_val = remap_addr - (al.map ? (start - map__start(al.map)) +
							    map__pgoff(al.map) : pgoff);
		}
		err = hashmap__add(&aslr->remap_addresses, new_remap_key, new_remap_val);
		if (err)
			dso__put(new_remap_key->dso);
	}
	if (err) {
		errno = -err;
		pr_err("Failure to add ASLR remapping %m\n");
		free(new_remap_key);
		free(new_remap_val);
		addr_location__exit(&al);
		return 0;
	}
	addr_location__exit(&al);
	return remap_addr;
}

static int aslr_tool__process_mmap(const struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	union perf_event *new_event;
	u8 cpumode;
	struct thread *thread;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;
	new_event = (union perf_event *)aslr->event_copy;
	cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_mmap(tool, event, sample, aslr_machine);
	if (err)
		return err;

	thread = machine__findnew_thread(aslr_machine, event->mmap.pid, event->mmap.tid);
	if (!thread)
		return -ENOMEM;
	memcpy(&new_event->mmap, &event->mmap, event->mmap.header.size);
	/* Remaps the mmap.start. */
	new_event->mmap.start = aslr_tool__findnew_mapping(aslr, machine, thread, cpumode,
							   event->mmap.start,
							   event->mmap.len,
							   event->mmap.pgoff);
	/*
	 * For anonymous memory (and kernel maps), the kernel populates the
	 * event's pgoff field with the original un-obfuscated virtual address
	 * in bytes (i.e. (addr >> PAGE_SHIFT) << PAGE_SHIFT).
	 * We must overwrite pgoff with the new remapped byte address to prevent
	 * leaking the original ASLR layout.
	 */
	if (is_anon_memory(event->mmap.filename) || is_no_dso_memory(event->mmap.filename) ||
	    ((cpumode == PERF_RECORD_MISC_KERNEL || cpumode == PERF_RECORD_MISC_GUEST_KERNEL) &&
	     !is_kernel_module(event->mmap.filename, cpumode)))
		new_event->mmap.pgoff = new_event->mmap.start;
	err = delegate->mmap(delegate, new_event, sample, machine);
	thread__put(thread);
	return err;
}

static int aslr_tool__process_mmap2(const struct perf_tool *tool,
				    union perf_event *event,
				    struct perf_sample *sample,
				    struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	union perf_event *new_event;
	u8 cpumode;
	struct thread *thread;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;
	new_event = (union perf_event *)aslr->event_copy;
	cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_mmap2(tool, event, sample, aslr_machine);
	if (err)
		return err;

	thread = machine__findnew_thread(aslr_machine, event->mmap2.pid, event->mmap2.tid);
	if (!thread)
		return -ENOMEM;
	memcpy(&new_event->mmap2, &event->mmap2, event->mmap2.header.size);
	/* Remaps the mmap.start. */
	new_event->mmap2.start = aslr_tool__findnew_mapping(aslr, machine, thread, cpumode,
							    event->mmap2.start,
							    event->mmap2.len,
							    event->mmap2.pgoff);
	/*
	 * For anonymous memory (and kernel maps), the kernel populates the
	 * event's pgoff field with the original un-obfuscated virtual address
	 * in bytes (i.e. (addr >> PAGE_SHIFT) << PAGE_SHIFT).
	 * We must overwrite pgoff with the new remapped byte address to prevent
	 * leaking the original ASLR layout.
	 */
	if (is_anon_memory(event->mmap2.filename) || is_no_dso_memory(event->mmap2.filename) ||
	    ((cpumode == PERF_RECORD_MISC_KERNEL || cpumode == PERF_RECORD_MISC_GUEST_KERNEL) &&
	     !is_kernel_module(event->mmap2.filename, cpumode)))
		new_event->mmap2.pgoff = new_event->mmap2.start;
	err = delegate->mmap2(delegate, new_event, sample, machine);
	thread__put(thread);
	return err;
}

static int aslr_tool__process_comm(const struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_comm(tool, event, sample, aslr_machine);
	if (err)
		return err;

	return delegate->comm(delegate, event, sample, machine);
}

static int aslr_tool__process_fork(const struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_fork(tool, event, sample, aslr_machine);
	if (err)
		return err;

	return delegate->fork(delegate, event, sample, machine);
}

static int aslr_tool__process_exit(const struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_exit(tool, event, sample, aslr_machine);
	if (err)
		return err;

	return delegate->exit(delegate, event, sample, machine);
}

static int aslr_tool__process_text_poke(const struct perf_tool *tool __maybe_unused,
					union perf_event *event __maybe_unused,
					struct perf_sample *sample __maybe_unused,
					struct machine *machine __maybe_unused)
{
	/* Drop in case the instruction encodes an ASLR revealing address. */
	return 0;
}

static int aslr_tool__process_ksymbol(const struct perf_tool *tool,
				      union perf_event *event,
				      struct perf_sample *sample,
				      struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	union perf_event *new_event;
	struct thread *thread;
	struct machine *aslr_machine;
	bool is_unregister;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;
	new_event = (union perf_event *)aslr->event_copy;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	thread = machine__findnew_thread(aslr_machine, kernel_pid, 0);
	if (!thread)
		return -ENOMEM;

	is_unregister = (event->ksymbol.flags & PERF_RECORD_KSYMBOL_FLAGS_UNREGISTER);

	memcpy(&new_event->ksymbol, &event->ksymbol, event->ksymbol.header.size);

	if (is_unregister) {
		new_event->ksymbol.addr = aslr_tool__findnew_mapping(aslr, machine, thread,
								     PERF_RECORD_MISC_KERNEL,
								     event->ksymbol.addr,
								     event->ksymbol.len,
								     /*pgoff=*/0);
		err = perf_event__process_ksymbol(tool, event, sample, aslr_machine);
	} else {
		err = perf_event__process_ksymbol(tool, event, sample, aslr_machine);
		new_event->ksymbol.addr = aslr_tool__findnew_mapping(aslr, machine, thread,
								     PERF_RECORD_MISC_KERNEL,
								     event->ksymbol.addr,
								     event->ksymbol.len,
								     /*pgoff=*/0);
	}
	if (err) {
		thread__put(thread);
		return err;
	}

	err = delegate->ksymbol(delegate, new_event, sample, machine);
	thread__put(thread);
	return err;
}

static int aslr_tool__process_sample(const struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine)
{
	struct evsel *evsel = sample->evsel;
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	int ret;
	int orig_sample_size;
	u64 sample_type;
	struct thread *thread;
	struct machine *aslr_machine;
	__u64 max_i;
	__u64 max_j;
	union perf_event *new_event;
	struct perf_sample new_sample;
	__u64 *in_array, *out_array;
	u8 cpumode;
	u64 addr;
	size_t i;
	size_t j;
	struct aslr_evsel_priv *priv = NULL;
	u64 orig_sample_type;
	u64 orig_regs_user;
	u64 orig_regs_intr;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;

	if (evsel__is_dummy_event(evsel))
		return delegate->sample(delegate, event, sample, machine);

	ret = -EFAULT;

	if (hashmap__find(&aslr->evsel_orig_attrs, evsel, &priv)) {
		orig_sample_type = priv->orig_sample_type;
		orig_regs_user = priv->orig_sample_regs_user;
		orig_regs_intr = priv->orig_sample_regs_intr;
	} else {
		orig_sample_type = evsel->core.attr.sample_type;
		orig_regs_user = evsel->core.attr.sample_regs_user;
		orig_regs_intr = evsel->core.attr.sample_regs_intr;
	}

	orig_sample_size = evsel->sample_size;

	sample_type = orig_sample_type;
	sample_type &= ~PERF_SAMPLE_REGS_USER;
	sample_type &= ~PERF_SAMPLE_REGS_INTR;
	sample_type &= ASLR_SUPPORTED_SAMPLE_TYPE;

	max_i = (event->header.size - sizeof(struct perf_event_header)) / sizeof(__u64);
	max_j = (PERF_SAMPLE_MAX_SIZE - sizeof(struct perf_event_header)) / sizeof(__u64);
	new_event = (union perf_event *)aslr->event_copy;
	cpumode = sample->cpumode;
	i = 0;
	j = 0;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	thread = machine__findnew_thread(aslr_machine, sample->pid, sample->tid);

	if (!thread)
		return -ENOMEM;

	if (max_i > PERF_SAMPLE_MAX_SIZE / sizeof(u64))
		goto out_put;

	new_event->sample.header = event->sample.header;

	in_array = &event->sample.array[0];
	out_array = &new_event->sample.array[0];

#define CHECK_BOUNDS(required_i, required_j) \
	(i + (required_i) > max_i || j + (required_j) > max_j)

#define COPY_U64() \
	do { \
		if (CHECK_BOUNDS(1, 1)) { \
			ret = -EFAULT; \
			goto out_put; \
		} \
		out_array[j++] = in_array[i++]; \
	} while (0)

#define REMAP_U64(addr_field) \
	do { \
		u64 remapped; \
		if (CHECK_BOUNDS(1, 1)) { \
			ret = -EFAULT; \
			goto out_put; \
		} \
		remapped = aslr_tool__remap_address(aslr, thread, cpumode, addr_field); \
		out_array[j++] = remapped; \
		i++; \
	} while (0)

	if (orig_sample_type & PERF_SAMPLE_IDENTIFIER)
		COPY_U64(); /* id */
	if (orig_sample_type & PERF_SAMPLE_IP)
		REMAP_U64(sample->ip);
	if (orig_sample_type & PERF_SAMPLE_TID) {
		union {
			u64 val64;
			u32 val32[2];
		} u;

		if (CHECK_BOUNDS(1, 1)) {
			ret = -EFAULT;
			goto out_put;
		}
		u.val32[0] = sample->pid;
		u.val32[1] = sample->tid;
		out_array[j++] = u.val64;
		i++;
	}
	if (orig_sample_type & PERF_SAMPLE_TIME)
		COPY_U64(); /* time */
	if (orig_sample_type & PERF_SAMPLE_ADDR)
		REMAP_U64(sample->addr);
	if (orig_sample_type & PERF_SAMPLE_ID)
		COPY_U64(); /* id */
	if (orig_sample_type & PERF_SAMPLE_STREAM_ID)
		COPY_U64(); /* stream_id */
	if (orig_sample_type & PERF_SAMPLE_CPU)
		COPY_U64(); /* cpu, res */
	if (orig_sample_type & PERF_SAMPLE_PERIOD)
		COPY_U64(); /* period */
	if (orig_sample_type & PERF_SAMPLE_READ) {
		if ((evsel->core.attr.read_format & PERF_FORMAT_GROUP) == 0) {
			COPY_U64(); /* value */
			if (evsel->core.attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
				COPY_U64(); /* time_enabled */
			if (evsel->core.attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
				COPY_U64(); /* time_running */
			if (evsel->core.attr.read_format & PERF_FORMAT_ID)
				COPY_U64(); /* id */
			if (evsel->core.attr.read_format & PERF_FORMAT_LOST)
				COPY_U64(); /* lost */
		} else {
			u64 nr;

			if (CHECK_BOUNDS(1, 1)) {
				ret = -EFAULT;
				goto out_put;
			}
			nr = in_array[i];
			COPY_U64();
			if (evsel->core.attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
				COPY_U64(); /* time_enabled */
			if (evsel->core.attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
				COPY_U64(); /* time_running */
			for (u64 cntr = 0; cntr < nr; cntr++) {
				COPY_U64(); /* value */
				if (evsel->core.attr.read_format & PERF_FORMAT_ID)
					COPY_U64(); /* id */
				if (evsel->core.attr.read_format & PERF_FORMAT_LOST)
					COPY_U64(); /* lost */
			}
		}
	}
	if (orig_sample_type & PERF_SAMPLE_CALLCHAIN) {
		u64 nr;

		if (CHECK_BOUNDS(1, 1)) {
			ret = -EFAULT;
			goto out_put;
		}
		nr = in_array[i];
		COPY_U64();

		for (u64 cntr = 0; cntr < nr; cntr++) {
			if (CHECK_BOUNDS(1, 1)) {
				ret = -EFAULT;
				goto out_put;
			}
			addr = in_array[i++];
			if (addr >= PERF_CONTEXT_MAX) {
				out_array[j++] = addr;
				switch (addr) {
				case PERF_CONTEXT_HV:
					cpumode = PERF_RECORD_MISC_HYPERVISOR;
					break;
				case PERF_CONTEXT_KERNEL:
					cpumode = PERF_RECORD_MISC_KERNEL;
					break;
				case PERF_CONTEXT_USER:
					cpumode = PERF_RECORD_MISC_USER;
					break;
				case PERF_CONTEXT_GUEST:
					cpumode = PERF_RECORD_MISC_GUEST_KERNEL;
					break;
				case PERF_CONTEXT_GUEST_KERNEL:
					cpumode = PERF_RECORD_MISC_GUEST_KERNEL;
					break;
				case PERF_CONTEXT_GUEST_USER:
					cpumode = PERF_RECORD_MISC_GUEST_USER;
					break;
				case PERF_CONTEXT_USER_DEFERRED:
					if (cntr + 1 >= nr) {
						pr_debug("Truncated callchain deferred cookie context\n");
						ret = 0;
						goto out_put;
					}
					/*
					 * Immediately followed by a 64-bit
					 * stitching cookie. Skip/Copy it!
					 */
					if (CHECK_BOUNDS(1, 1)) {
						ret = -EFAULT;
						goto out_put;
					}
					out_array[j++] = in_array[i++];
					cntr++;
					cpumode = PERF_RECORD_MISC_USER;
					break;
				default:
					pr_debug("invalid callchain context: %"PRIx64"\n", addr);
					ret = 0;
					goto out_put;
				}
				continue;
			}
			addr = aslr_tool__remap_address(aslr, thread, cpumode, addr);
			out_array[j++] = addr;
		}
	}
	if (orig_sample_type & PERF_SAMPLE_RAW) {
		size_t bytes = sizeof(u32) + sample->raw_size;
		size_t u64_words = (bytes + 7) / 8;

		if (i + u64_words > max_i || j + u64_words > max_j) {
			ret = -EFAULT;
			goto out_put;
		}
		memcpy(&out_array[j], &in_array[i], bytes);
		i += u64_words;
		j += u64_words;
		/*
		 * TODO: certain raw samples can be remapped, such as
		 * tracepoints by examining their fields.
		 */
		pr_debug("Dropping raw samples as possible ASLR leak\n");
		ret = 0;
		goto out_put;
	}
	if (orig_sample_type & PERF_SAMPLE_BRANCH_STACK) {
		u64 nr;

		if (CHECK_BOUNDS(1, 1)) {
			ret = -EFAULT;
			goto out_put;
		}
		nr = in_array[i];
		COPY_U64();

		if (evsel->core.attr.branch_sample_type & PERF_SAMPLE_BRANCH_HW_INDEX)
			COPY_U64(); /* hw_idx */

		if (nr > (ULLONG_MAX / 3)) {
			ret = -EFAULT;
			goto out_put;
		}
		if (nr * 3 > max_i - i || nr * 3 > max_j - j) {
			ret = -EFAULT;
			goto out_put;
		}
		for (u64 cntr = 0; cntr < nr; cntr++) {
			u64 from = in_array[i++];
			u64 to = in_array[i++];

			from = aslr_tool__remap_address(aslr, thread, sample->cpumode, from);
			to = aslr_tool__remap_address(aslr, thread, sample->cpumode, to);

			out_array[j++] = from;
			out_array[j++] = to;
			out_array[j++] = in_array[i++]; /* flags */
		}
		if (evsel->core.attr.branch_sample_type & PERF_SAMPLE_BRANCH_COUNTERS) {
			if (nr > max_i - i || nr > max_j - j) {
				ret = -EFAULT;
				goto out_put;
			}
			for (u64 cntr = 0; cntr < nr; cntr++)
				COPY_U64();
		}
	}
	if (orig_sample_type & PERF_SAMPLE_REGS_USER) {
		u64 abi;

		if (CHECK_BOUNDS(1, 0)) {
			ret = -EFAULT;
			goto out_put;
		}
		abi = in_array[i++];
		if (abi != PERF_SAMPLE_REGS_ABI_NONE) {
			u64 nr = hweight64(orig_regs_user);

			if (nr > max_i - i) {
				ret = -EFAULT;
				goto out_put;
			}
			i += nr;
		}
	}
	if (orig_sample_type & PERF_SAMPLE_STACK_USER) {
		u64 size;

		if (CHECK_BOUNDS(1, 1)) {
			ret = -EFAULT;
			goto out_put;
		}
		size = in_array[i];
		COPY_U64();
		if (size > 0) {
			size_t u64_words = size / 8 + (size % 8 ? 1 : 0);

			if (u64_words > max_i - i || u64_words > max_j - j) {
				ret = -EFAULT;
				goto out_put;
			}
			memcpy(&out_array[j], &in_array[i], size);
			if (size % 8) {
				size_t pad = 8 - (size % 8);

				memset(((char *)&out_array[j]) + size, 0, pad);
			}
			i += u64_words;
			j += u64_words;
		}
		/* TODO: can this be less conservative? */
		pr_debug("Dropping stack user sample as possible ASLR leak\n");
		ret = 0;
		goto out_put;
	}
	if (orig_sample_type & PERF_SAMPLE_WEIGHT_TYPE)
		COPY_U64(); /* perf_sample_weight */
	if (orig_sample_type & PERF_SAMPLE_DATA_SRC)
		COPY_U64(); /* data_src */
	if (orig_sample_type & PERF_SAMPLE_TRANSACTION)
		COPY_U64(); /* transaction */
	if (orig_sample_type & PERF_SAMPLE_REGS_INTR) {
		u64 abi;

		if (CHECK_BOUNDS(1, 0)) {
			ret = -EFAULT;
			goto out_put;
		}
		abi = in_array[i++];
		if (abi != PERF_SAMPLE_REGS_ABI_NONE) {
			u64 nr = hweight64(orig_regs_intr);

			if (nr > max_i - i) {
				ret = -EFAULT;
				goto out_put;
			}
			i += nr;
		}
	}
	if (orig_sample_type & PERF_SAMPLE_PHYS_ADDR) {
		COPY_U64(); /* phys_addr */
		/* TODO: can this be less conservative? */
		pr_debug("Dropping physical address sample as possible ASLR leak\n");
		ret = 0;
		goto out_put;
	}
	if (orig_sample_type & PERF_SAMPLE_CGROUP)
		COPY_U64(); /* cgroup */
	if (orig_sample_type & PERF_SAMPLE_DATA_PAGE_SIZE)
		COPY_U64(); /* data_page_size */
	if (orig_sample_type & PERF_SAMPLE_CODE_PAGE_SIZE)
		COPY_U64(); /* code_page_size */

	if (orig_sample_type & PERF_SAMPLE_AUX) {
		u64 size;

		if (CHECK_BOUNDS(1, 1)) {
			ret = -EFAULT;
			goto out_put;
		}
		out_array[j] = in_array[i];
		size = out_array[j++];
		i++;
		if (size > 0) {
			size_t u64_words = size / 8 + (size % 8 ? 1 : 0);

			if (u64_words > max_i - i || u64_words > max_j - j) {
				ret = -EFAULT;
				goto out_put;
			}
			memcpy(&out_array[j], &in_array[i], size);
			if (size % 8) {
				size_t pad = 8 - (size % 8);

				memset(((char *)&out_array[j]) + size, 0, pad);
			}
			i += u64_words;
			j += u64_words;
		}
		/* TODO: can this be less conservative? */
		pr_debug("Dropping aux sample as possible ASLR leak\n");
		ret = 0;
		goto out_put;
	}

	if (evsel__is_offcpu_event(evsel)) {
		/* TODO: can this be less conservative? */
		pr_debug("Dropping off-CPU sample as possible ASLR leak\n");
		ret = 0;
		goto out_put;
	}

	new_event->sample.header.size = sizeof(struct perf_event_header) + j * sizeof(u64);
	/* Temporarily override evsel attributes to match the stripped new_event format! */
	evsel->sample_size = __evsel__sample_size(sample_type);
	evsel->core.attr.sample_type = sample_type;
	evsel->core.attr.sample_regs_user = 0;
	evsel->core.attr.sample_regs_intr = 0;
	perf_sample__init(&new_sample, /*all=*/ true);
	ret = __evsel__parse_sample(evsel, new_event, &new_sample, /*needs_swap=*/false);

	if (ret) {
		/* Restore original attributes immediately if parsing fails */
		evsel->sample_size = orig_sample_size;
		evsel->core.attr.sample_type = orig_sample_type;
		evsel->core.attr.sample_regs_user = orig_regs_user;
		evsel->core.attr.sample_regs_intr = orig_regs_intr;
		perf_sample__exit(&new_sample);
		goto out_put;
	}

	new_sample.evsel = evsel;
	ret = delegate->sample(delegate, new_event, &new_sample, machine);
	perf_sample__exit(&new_sample);

	/* Restore original attributes so trace ingestion never desynchronizes! */
	evsel->sample_size = orig_sample_size;
	evsel->core.attr.sample_type = orig_sample_type;
	evsel->core.attr.sample_regs_user = orig_regs_user;
	evsel->core.attr.sample_regs_intr = orig_regs_intr;

out_put:
	thread__put(thread);
	return ret;
}

#undef CHECK_BOUNDS
#undef COPY_U64
#undef REMAP_U64

static int skipn(int fd, off_t n)
{
	char buf[4096];
	ssize_t ret;

	while (n > 0) {
		ret = read(fd, buf, min_t(off_t, n, (off_t)sizeof(buf)));
		if (ret <= 0)
			return ret;
		n -= ret;
	}

	return 0;
}

static s64 aslr_tool__process_auxtrace(const struct perf_tool *tool __maybe_unused,
				       struct perf_session *session,
				       union perf_event *event)
{
	pr_warning_once("ASLR: Dropping auxtrace data as it cannot be obfuscated.\n");
	if (perf_data__is_pipe(session->data)) {
		/* Copy behavior of the stub by reading all pipe data. */
		int err = skipn(perf_data__fd(session->data), event->auxtrace.size);

		if (err < 0)
			return err;
	}
	return event->auxtrace.size;
}

static int aslr_tool__process_auxtrace_info(const struct perf_tool *tool __maybe_unused,
					    struct perf_session *session __maybe_unused,
					    union perf_event *event __maybe_unused)
{
	return 0;
}

static int aslr_tool__process_auxtrace_error(const struct perf_tool *tool __maybe_unused,
					     struct perf_session *session __maybe_unused,
					     union perf_event *event __maybe_unused)
{
	return 0;
}

void aslr_tool__strip_attr_event(union perf_event *event, struct evlist *evlist)
{
	u32 attr_size;

	if (!evlist)
		return;

	attr_size = event->attr.attr.size ?: PERF_ATTR_SIZE_VER0;

	if (attr_size >= (offsetof(struct perf_event_attr, sample_type) + sizeof(u64))) {
		event->attr.attr.sample_type &= ASLR_SUPPORTED_SAMPLE_TYPE;

		if (attr_size >= (offsetof(struct perf_event_attr, sample_regs_user) + sizeof(u64)))
			event->attr.attr.sample_regs_user = 0;
		if (attr_size >= (offsetof(struct perf_event_attr, sample_regs_intr) + sizeof(u64)))
			event->attr.attr.sample_regs_intr = 0;
	}

	if (attr_size >= (offsetof(struct perf_event_attr, type) + sizeof(u32))) {
		u32 type = event->attr.attr.type;

		if (type == PERF_TYPE_BREAKPOINT &&
		    attr_size >= (offsetof(struct perf_event_attr, bp_addr) + sizeof(u64))) {
			event->attr.attr.bp_addr = 0;
		} else if (type >= PERF_TYPE_MAX) {
			struct perf_pmu *pmu;

			pmu = perf_pmus__find_by_type(type);
			if (pmu && (!strcmp(pmu->name, "kprobe") ||
				    !strcmp(pmu->name, "uprobe"))) {
				if (attr_size >= (offsetof(struct perf_event_attr, config1) + sizeof(u64)))
					event->attr.attr.config1 = 0;
				if (attr_size >= (offsetof(struct perf_event_attr, config2) + sizeof(u64)))
					event->attr.attr.config2 = 0;
			}
		}
	}
}

static int aslr_tool__init(struct aslr_tool *aslr, struct perf_tool *delegate)
{
	delegate_tool__init(&aslr->tool, delegate);
	aslr->tool.tool.ordered_events = true;

	if (machines__init(&aslr->machines))
		return -ENOMEM;

	hashmap__init(&aslr->remap_addresses,
		      remap_addresses__hash, remap_addresses__equal,
		      /*ctx=*/NULL);
	hashmap__init(&aslr->top_addresses,
		      top_addresses__hash, top_addresses__equal,
		      /*ctx=*/NULL);
	hashmap__init(&aslr->evsel_orig_attrs,
		      evsel_hash, evsel_equal,
		      /*ctx=*/NULL);

	aslr->tool.tool.sample	= aslr_tool__process_sample;
	/* read - reads a counter, okay to delegate. */
	aslr->tool.tool.mmap	= aslr_tool__process_mmap;
	aslr->tool.tool.mmap2	= aslr_tool__process_mmap2;
	aslr->tool.tool.comm	= aslr_tool__process_comm;
	aslr->tool.tool.fork	= aslr_tool__process_fork;
	aslr->tool.tool.exit	= aslr_tool__process_exit;
	/* namesspaces, cgroup, lost, lost_sample, aux, */
	/* itrace_start, aux_output_hw_id, context_switch, throttle, unthrottle */
	/* - no virtual addresses. */
	aslr->tool.tool.ksymbol	= aslr_tool__process_ksymbol;
	/* bpf - no virtual address. */
	aslr->tool.tool.text_poke = aslr_tool__process_text_poke;
	/*
	 * event_update, tracing_data, finished_round, build_id, id_index,
	 * auxtrace_info, auxtrace_error, time_conv, thread_map, cpu_map,
	 * stat_config, stat, feature, finished_init, bpf_metadata, compressed,
	 * auxtrace - no virtual addresses.
	 */
	aslr->tool.tool.auxtrace = aslr_tool__process_auxtrace;
	aslr->tool.tool.auxtrace_info = aslr_tool__process_auxtrace_info;
	aslr->tool.tool.auxtrace_error = aslr_tool__process_auxtrace_error;

	return 0;
}

struct perf_tool *aslr_tool__new(struct perf_tool *delegate)
{
	struct aslr_tool *aslr = zalloc(sizeof(*aslr));

	if (!aslr)
		return NULL;

	if (aslr_tool__init(aslr, delegate)) {
		free(aslr);
		return NULL;
	}
	return &aslr->tool.tool;
}

void aslr_tool__delete(struct perf_tool *tool)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct hashmap_entry *cur;
	size_t bkt;
	struct rb_node *nd;

	if (!tool)
		return;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);

	hashmap__for_each_entry(&aslr->remap_addresses, cur, bkt) {
		struct remap_addresses_key *key = (struct remap_addresses_key *)cur->pkey;

		if (key)
			dso__put(key->dso);
		zfree(&cur->pkey);
		zfree(&cur->pvalue);
	}
	hashmap__for_each_entry(&aslr->top_addresses, cur, bkt) {
		zfree(&cur->pkey);
		zfree(&cur->pvalue);
	}
	hashmap__for_each_entry(&aslr->evsel_orig_attrs, cur, bkt) {
		zfree(&cur->pvalue);
	}

	hashmap__clear(&aslr->remap_addresses);
	hashmap__clear(&aslr->top_addresses);
	hashmap__clear(&aslr->evsel_orig_attrs);
	aslr_tool__destroy_machines_priv(&aslr->machines);
	machines__destroy_kernel_maps(&aslr->machines);

	while ((nd = rb_first_cached(&aslr->machines.guests)) != NULL) {
		struct machine *machine = rb_entry(nd, struct machine, rb_node);

		rb_erase_cached(nd, &aslr->machines.guests);
		machine__delete(machine);
	}

	machines__exit(&aslr->machines);
	free(aslr);
}

int aslr_tool__cache_orig_attrs(struct perf_tool *tool, struct evsel *evsel)
{
	struct delegate_tool *del_tool = container_of(tool, struct delegate_tool, tool);
	struct aslr_tool *aslr = container_of(del_tool, struct aslr_tool, tool);
	struct aslr_evsel_priv *priv = zalloc(sizeof(*priv));
	int err;

	if (!priv)
		return -ENOMEM;

	priv->orig_sample_type = evsel->core.attr.sample_type;
	priv->orig_sample_regs_user = evsel->core.attr.sample_regs_user;
	priv->orig_sample_regs_intr = evsel->core.attr.sample_regs_intr;
	priv->orig_sample_size = evsel->sample_size;

	err = hashmap__add(&aslr->evsel_orig_attrs, evsel, priv);
	if (err) {
		free(priv);
		return err;
	}
	return 0;
}

void aslr_tool__strip_evlist(const struct perf_tool *tool __maybe_unused, struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		evsel->core.attr.sample_type &= ASLR_SUPPORTED_SAMPLE_TYPE;
		evsel->core.attr.sample_regs_user = 0;
		evsel->core.attr.sample_regs_intr = 0;
		evsel->sample_size = __evsel__sample_size(evsel->core.attr.sample_type);
		evsel__calc_id_pos(evsel);

		if (evsel->core.attr.type == PERF_TYPE_BREAKPOINT) {
			evsel->core.attr.bp_addr = 0;
		} else if (evsel->core.attr.type >= PERF_TYPE_MAX) {
			struct perf_pmu *pmu = perf_pmus__find_by_type(evsel->core.attr.type);

			if (pmu && (!strcmp(pmu->name, "kprobe") ||
				    !strcmp(pmu->name, "uprobe"))) {
				evsel->core.attr.config1 = 0;
				evsel->core.attr.config2 = 0;
			}
		}
	}
}

void aslr_tool__restore_evlist(const struct perf_tool *tool, struct evlist *evlist)
{
	const struct delegate_tool *del_tool = container_of(tool, const struct delegate_tool, tool);
	const struct aslr_tool *aslr = container_of(del_tool, const struct aslr_tool, tool);
	struct evsel *evsel;
	struct aslr_evsel_priv *priv;

	evlist__for_each_entry(evlist, evsel) {
		if (hashmap__find(&aslr->evsel_orig_attrs, evsel, &priv)) {
			evsel->core.attr.sample_type = priv->orig_sample_type;
			evsel->core.attr.sample_regs_user = priv->orig_sample_regs_user;
			evsel->core.attr.sample_regs_intr = priv->orig_sample_regs_intr;
			evsel->sample_size = priv->orig_sample_size;
			evsel__calc_id_pos(evsel);
		}
	}
}
