// SPDX-License-Identifier: GPL-2.0
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include "dso.h"
#include "session.h"
#include "thread.h"
#include "thread-stack.h"
#include "debug.h"
#include "namespaces.h"
#include "comm.h"
#include "map.h"
#include "symbol.h"
#include "unwind.h"
#include "callchain.h"
#include "dwarf-regs.h"

#include <api/fs/fs.h>

int thread__init_maps(struct thread *thread, struct machine *machine)
{
	pid_t pid = thread__pid(thread);

	if (pid == thread__tid(thread) || pid == -1) {
		thread__set_maps(thread, maps__new(machine));
	} else {
		struct thread *leader = machine__findnew_thread(machine, pid, pid);

		if (leader) {
			thread__set_maps(thread, maps__get(thread__maps(leader)));
			thread__put(leader);
		}
	}

	return thread__maps(thread) ? 0 : -1;
}

struct thread *thread__new(pid_t pid, pid_t tid)
{
	RC_STRUCT(thread) *_thread = zalloc(sizeof(*_thread));
	struct thread *thread;

	if (ADD_RC_CHK(thread, _thread) != NULL) {
		struct comm *comm;
		char comm_str[32];

		thread__set_pid(thread, pid);
		thread__set_tid(thread, tid);
		thread__set_ppid(thread, -1);
		thread__set_cpu(thread, -1);
		thread__set_guest_cpu(thread, -1);
		thread__set_e_machine(thread, EM_NONE);
		thread__set_lbr_stitch_enable(thread, false);
		INIT_LIST_HEAD(thread__namespaces_list(thread));
		INIT_LIST_HEAD(thread__comm_list(thread));
		init_rwsem(thread__namespaces_lock(thread));
		init_rwsem(thread__comm_lock(thread));

		snprintf(comm_str, sizeof(comm_str), ":%d", tid);
		comm = comm__new(comm_str, 0, false);
		if (!comm)
			goto err_thread;

		list_add(&comm->list, thread__comm_list(thread));
		refcount_set(thread__refcnt(thread), 1);
		/* Thread holds first ref to nsdata. */
		RC_CHK_ACCESS(thread)->nsinfo = nsinfo__new(pid);
		srccode_state_init(thread__srccode_state(thread));
	}

	return thread;

err_thread:
	thread__delete(thread);
	return NULL;
}

static void (*thread__priv_destructor)(void *priv);

void thread__set_priv_destructor(void (*destructor)(void *priv))
{
	assert(thread__priv_destructor == NULL);

	thread__priv_destructor = destructor;
}

void thread__delete(struct thread *thread)
{
	struct namespaces *namespaces, *tmp_namespaces;
	struct comm *comm, *tmp_comm;

	thread_stack__free(thread);

	if (thread__maps(thread)) {
		maps__put(thread__maps(thread));
		thread__set_maps(thread, NULL);
	}
	down_write(thread__namespaces_lock(thread));
	list_for_each_entry_safe(namespaces, tmp_namespaces,
				 thread__namespaces_list(thread), list) {
		list_del_init(&namespaces->list);
		namespaces__free(namespaces);
	}
	up_write(thread__namespaces_lock(thread));

	down_write(thread__comm_lock(thread));
	list_for_each_entry_safe(comm, tmp_comm, thread__comm_list(thread), list) {
		list_del_init(&comm->list);
		comm__free(comm);
	}
	up_write(thread__comm_lock(thread));

	nsinfo__zput(RC_CHK_ACCESS(thread)->nsinfo);
	srccode_state_free(thread__srccode_state(thread));

	exit_rwsem(thread__namespaces_lock(thread));
	exit_rwsem(thread__comm_lock(thread));
	thread__free_stitch_list(thread);

	if (thread__priv_destructor)
		thread__priv_destructor(thread__priv(thread));

	RC_CHK_FREE(thread);
}

struct thread *thread__get(struct thread *thread)
{
	struct thread *result;

	if (RC_CHK_GET(result, thread))
		refcount_inc(thread__refcnt(thread));

	return result;
}

void thread__put(struct thread *thread)
{
	if (thread && refcount_dec_and_test(thread__refcnt(thread)))
		thread__delete(thread);
	else
		RC_CHK_PUT(thread);
}

static struct namespaces *__thread__namespaces(struct thread *thread)
{
	if (list_empty(thread__namespaces_list(thread)))
		return NULL;

	return list_first_entry(thread__namespaces_list(thread), struct namespaces, list);
}

struct namespaces *thread__namespaces(struct thread *thread)
{
	struct namespaces *ns;

	down_read(thread__namespaces_lock(thread));
	ns = __thread__namespaces(thread);
	up_read(thread__namespaces_lock(thread));

	return ns;
}

static int __thread__set_namespaces(struct thread *thread, u64 timestamp,
				    struct perf_record_namespaces *event)
{
	struct namespaces *new, *curr = __thread__namespaces(thread);

	new = namespaces__new(event);
	if (!new)
		return -ENOMEM;

	list_add(&new->list, thread__namespaces_list(thread));

	if (timestamp && curr) {
		/*
		 * setns syscall must have changed few or all the namespaces
		 * of this thread. Update end time for the namespaces
		 * previously used.
		 */
		curr = list_next_entry(new, list);
		curr->end_time = timestamp;
	}

	return 0;
}

int thread__set_namespaces(struct thread *thread, u64 timestamp,
			   struct perf_record_namespaces *event)
{
	int ret;

	down_write(thread__namespaces_lock(thread));
	ret = __thread__set_namespaces(thread, timestamp, event);
	up_write(thread__namespaces_lock(thread));
	return ret;
}

struct comm *thread__comm(struct thread *thread)
{
	if (list_empty(thread__comm_list(thread)))
		return NULL;

	return list_first_entry(thread__comm_list(thread), struct comm, list);
}

struct comm *thread__exec_comm(struct thread *thread)
{
	struct comm *comm, *last = NULL, *second_last = NULL;

	list_for_each_entry(comm, thread__comm_list(thread), list) {
		if (comm->exec)
			return comm;
		second_last = last;
		last = comm;
	}

	/*
	 * 'last' with no start time might be the parent's comm of a synthesized
	 * thread (created by processing a synthesized fork event). For a main
	 * thread, that is very probably wrong. Prefer a later comm to avoid
	 * that case.
	 */
	if (second_last && !last->start && thread__pid(thread) == thread__tid(thread))
		return second_last;

	return last;
}

static int ____thread__set_comm(struct thread *thread, const char *str,
				u64 timestamp, bool exec)
{
	struct comm *new, *curr = thread__comm(thread);

	/* Override the default :tid entry */
	if (!thread__comm_set(thread)) {
		int err = comm__override(curr, str, timestamp, exec);
		if (err)
			return err;
	} else {
		new = comm__new(str, timestamp, exec);
		if (!new)
			return -ENOMEM;
		list_add(&new->list, thread__comm_list(thread));

		if (exec)
			unwind__flush_access(thread__maps(thread));
	}

	thread__set_comm_set(thread, true);

	return 0;
}

int __thread__set_comm(struct thread *thread, const char *str, u64 timestamp,
		       bool exec)
{
	int ret;

	down_write(thread__comm_lock(thread));
	ret = ____thread__set_comm(thread, str, timestamp, exec);
	up_write(thread__comm_lock(thread));
	return ret;
}

int thread__set_comm_from_proc(struct thread *thread)
{
	char path[64];
	char *comm = NULL;
	size_t sz;
	int err = -1;

	if (!(snprintf(path, sizeof(path), "%d/task/%d/comm",
		       thread__pid(thread), thread__tid(thread)) >= (int)sizeof(path)) &&
	    procfs__read_str(path, &comm, &sz) == 0) {
		comm[sz - 1] = '\0';
		err = thread__set_comm(thread, comm, 0);
	}

	return err;
}

static const char *__thread__comm_str(struct thread *thread)
{
	const struct comm *comm = thread__comm(thread);

	if (!comm)
		return NULL;

	return comm__str(comm);
}

const char *thread__comm_str(struct thread *thread)
{
	const char *str;

	down_read(thread__comm_lock(thread));
	str = __thread__comm_str(thread);
	up_read(thread__comm_lock(thread));

	return str;
}

static int __thread__comm_len(struct thread *thread, const char *comm)
{
	if (!comm)
		return 0;
	thread__set_comm_len(thread, strlen(comm));

	return thread__var_comm_len(thread);
}

/* CHECKME: it should probably better return the max comm len from its comm list */
int thread__comm_len(struct thread *thread)
{
	int comm_len = thread__var_comm_len(thread);

	if (!comm_len) {
		const char *comm;

		down_read(thread__comm_lock(thread));
		comm = __thread__comm_str(thread);
		comm_len = __thread__comm_len(thread, comm);
		up_read(thread__comm_lock(thread));
	}

	return comm_len;
}

size_t thread__fprintf(struct thread *thread, FILE *fp)
{
	return fprintf(fp, "Thread %d %s\n", thread__tid(thread), thread__comm_str(thread)) +
	       maps__fprintf(thread__maps(thread), fp);
}

int thread__insert_map(struct thread *thread, struct map *map)
{
	int ret;

	ret = unwind__prepare_access(thread__maps(thread), map, NULL);
	if (ret)
		return ret;

	return maps__fixup_overlap_and_insert(thread__maps(thread), map);
}

struct thread__prepare_access_maps_cb_args {
	int err;
	struct maps *maps;
};

static int thread__prepare_access_maps_cb(struct map *map, void *data)
{
	bool initialized = false;
	struct thread__prepare_access_maps_cb_args *args = data;

	args->err = unwind__prepare_access(args->maps, map, &initialized);

	return (args->err || initialized) ? 1 : 0;
}

static int thread__prepare_access(struct thread *thread)
{
	struct thread__prepare_access_maps_cb_args args = {
		.err = 0,
	};

	if (dwarf_callchain_users) {
		args.maps = thread__maps(thread);
		maps__for_each_map(thread__maps(thread), thread__prepare_access_maps_cb, &args);
	}

	return args.err;
}

static int thread__clone_maps(struct thread *thread, struct thread *parent, bool do_maps_clone)
{
	/* This is new thread, we share map groups for process. */
	if (thread__pid(thread) == thread__pid(parent))
		return thread__prepare_access(thread);

	if (maps__equal(thread__maps(thread), thread__maps(parent))) {
		pr_debug("broken map groups on thread %d/%d parent %d/%d\n",
			 thread__pid(thread), thread__tid(thread),
			 thread__pid(parent), thread__tid(parent));
		return 0;
	}
	/* But this one is new process, copy maps. */
	return do_maps_clone ? maps__copy_from(thread__maps(thread), thread__maps(parent)) : 0;
}

int thread__fork(struct thread *thread, struct thread *parent, u64 timestamp, bool do_maps_clone)
{
	if (thread__comm_set(parent)) {
		const char *comm = thread__comm_str(parent);
		int err;
		if (!comm)
			return -ENOMEM;
		err = thread__set_comm(thread, comm, timestamp);
		if (err)
			return err;
	}

	thread__set_ppid(thread, thread__tid(parent));
	return thread__clone_maps(thread, parent, do_maps_clone);
}

void thread__find_cpumode_addr_location(struct thread *thread, u64 addr,
					struct addr_location *al)
{
	size_t i;
	const u8 cpumodes[] = {
		PERF_RECORD_MISC_USER,
		PERF_RECORD_MISC_KERNEL,
		PERF_RECORD_MISC_GUEST_USER,
		PERF_RECORD_MISC_GUEST_KERNEL
	};

	for (i = 0; i < ARRAY_SIZE(cpumodes); i++) {
		thread__find_symbol(thread, cpumodes[i], addr, al);
		if (al->map)
			break;
	}
}

static uint16_t read_proc_e_machine_for_pid(pid_t pid)
{
	char path[6 /* "/proc/" */ + 11 /* max length of pid */ + 5 /* "/exe\0" */];
	int fd;
	uint16_t e_machine = EM_NONE;

	snprintf(path, sizeof(path), "/proc/%d/exe", pid);
	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		_Static_assert(offsetof(Elf32_Ehdr, e_machine) == 18, "Unexpected offset");
		_Static_assert(offsetof(Elf64_Ehdr, e_machine) == 18, "Unexpected offset");
		if (pread(fd, &e_machine, sizeof(e_machine), 18) != sizeof(e_machine))
			e_machine = EM_NONE;
		close(fd);
	}
	return e_machine;
}

static int thread__e_machine_callback(struct map *map, void *machine)
{
	struct dso *dso = map__dso(map);

	_Static_assert(0 == EM_NONE, "Unexpected EM_NONE");
	if (!dso)
		return EM_NONE;

	return dso__e_machine(dso, machine);
}

uint16_t thread__e_machine(struct thread *thread, struct machine *machine)
{
	pid_t tid, pid;
	uint16_t e_machine = RC_CHK_ACCESS(thread)->e_machine;

	if (e_machine != EM_NONE)
		return e_machine;

	tid = thread__tid(thread);
	pid = thread__pid(thread);
	if (pid != tid) {
		struct thread *parent = machine__findnew_thread(machine, pid, pid);

		if (parent) {
			e_machine = thread__e_machine(parent, machine);
			thread__set_e_machine(thread, e_machine);
			return e_machine;
		}
		/* Something went wrong, fallback. */
	}
	/* Reading on the PID thread. First try to find from the maps. */
	e_machine = maps__for_each_map(thread__maps(thread),
				       thread__e_machine_callback,
				       machine);
	if (e_machine == EM_NONE) {
		/* Maps failed, perhaps we're live with map events disabled. */
		bool is_live = machine->machines == NULL;

		if (!is_live) {
			/* Check if the session has a data file. */
			struct perf_session *session = container_of(machine->machines,
								    struct perf_session,
								    machines);

			is_live = !!session->data;
		}
		/* Read from /proc/pid/exe if live. */
		if (is_live)
			e_machine = read_proc_e_machine_for_pid(pid);
	}
	if (e_machine != EM_NONE)
		thread__set_e_machine(thread, e_machine);
	else
		e_machine = EM_HOST;
	return e_machine;
}

struct thread *thread__main_thread(struct machine *machine, struct thread *thread)
{
	if (thread__pid(thread) == thread__tid(thread))
		return thread__get(thread);

	if (thread__pid(thread) == -1)
		return NULL;

	return machine__find_thread(machine, thread__pid(thread), thread__pid(thread));
}

int thread__memcpy(struct thread *thread, struct machine *machine,
		   void *buf, u64 ip, int len, bool *is64bit)
{
	u8 cpumode = PERF_RECORD_MISC_USER;
	struct addr_location al;
	struct dso *dso;
	long offset;

	if (machine__kernel_ip(machine, ip))
		cpumode = PERF_RECORD_MISC_KERNEL;

	addr_location__init(&al);
	if (!thread__find_map(thread, cpumode, ip, &al)) {
		addr_location__exit(&al);
		return -1;
	}

	dso = map__dso(al.map);

	if (!dso || dso__data(dso)->status == DSO_DATA_STATUS_ERROR || map__load(al.map) < 0) {
		addr_location__exit(&al);
		return -1;
	}

	offset = map__map_ip(al.map, ip);
	if (is64bit)
		*is64bit = dso__is_64_bit(dso);

	addr_location__exit(&al);

	return dso__data_read_offset(dso, machine, offset, buf, len);
}

void thread__free_stitch_list(struct thread *thread)
{
	struct lbr_stitch *lbr_stitch = thread__lbr_stitch(thread);
	struct stitch_list *pos, *tmp;

	if (!lbr_stitch)
		return;

	list_for_each_entry_safe(pos, tmp, &lbr_stitch->lists, node) {
		map_symbol__exit(&pos->cursor.ms);
		list_del_init(&pos->node);
		free(pos);
	}

	list_for_each_entry_safe(pos, tmp, &lbr_stitch->free_lists, node) {
		list_del_init(&pos->node);
		free(pos);
	}

	for (unsigned int i = 0 ; i < lbr_stitch->prev_lbr_cursor_size; i++)
		map_symbol__exit(&lbr_stitch->prev_lbr_cursor[i].ms);

	zfree(&lbr_stitch->prev_lbr_cursor);
	free(thread__lbr_stitch(thread));
	thread__set_lbr_stitch(thread, NULL);
}
