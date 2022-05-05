/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MACHINE_H
#define __PERF_MACHINE_H

#include <sys/types.h>
#include <linux/rbtree.h>
#include "maps.h"
#include "dsos.h"
#include "rwsem.h"

struct addr_location;
struct branch_stack;
struct dso;
struct dso_id;
struct evsel;
struct perf_sample;
struct symbol;
struct target;
struct thread;
union perf_event;

/* Native host kernel uses -1 as pid index in machine */
#define	HOST_KERNEL_ID			(-1)
#define	DEFAULT_GUEST_KERNEL_ID		(0)

extern const char *ref_reloc_sym_names[];

struct vdso_info;

#define THREADS__TABLE_BITS	8
#define THREADS__TABLE_SIZE	(1 << THREADS__TABLE_BITS)

struct threads {
	struct rb_root_cached  entries;
	struct rw_semaphore    lock;
	unsigned int	       nr;
	struct list_head       dead;
	struct thread	       *last_match;
};

struct machine {
	struct rb_node	  rb_node;
	pid_t		  pid;
	u16		  id_hdr_size;
	bool		  comm_exec;
	bool		  kptr_restrict_warned;
	bool		  single_address_space;
	char		  *root_dir;
	char		  *mmap_name;
	struct threads    threads[THREADS__TABLE_SIZE];
	struct vdso_info  *vdso_info;
	struct perf_env   *env;
	struct dsos	  dsos;
	struct maps	  *kmaps;
	struct map	  *vmlinux_map;
	u64		  kernel_start;
	pid_t		  *current_tid;
	union { /* Tool specific area */
		void	  *priv;
		u64	  db_id;
	};
	bool		  trampolines_mapped;
};

static inline struct threads *machine__threads(struct machine *machine, pid_t tid)
{
	/* Cast it to handle tid == -1 */
	return &machine->threads[(unsigned int)tid % THREADS__TABLE_SIZE];
}

/*
 * The main kernel (vmlinux) map
 */
static inline
struct map *machine__kernel_map(struct machine *machine)
{
	return machine->vmlinux_map;
}

/*
 * kernel (the one returned by machine__kernel_map()) plus kernel modules maps
 */
static inline
struct maps *machine__kernel_maps(struct machine *machine)
{
	return machine->kmaps;
}

int machine__get_kernel_start(struct machine *machine);

static inline u64 machine__kernel_start(struct machine *machine)
{
	if (!machine->kernel_start)
		machine__get_kernel_start(machine);
	return machine->kernel_start;
}

static inline bool machine__kernel_ip(struct machine *machine, u64 ip)
{
	u64 kernel_start = machine__kernel_start(machine);

	return ip >= kernel_start;
}

u8 machine__addr_cpumode(struct machine *machine, u8 cpumode, u64 addr);

struct thread *machine__find_thread(struct machine *machine, pid_t pid,
				    pid_t tid);
struct thread *machine__idle_thread(struct machine *machine);
struct comm *machine__thread_exec_comm(struct machine *machine,
				       struct thread *thread);

int machine__process_comm_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_exit_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_fork_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_lost_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_lost_samples_event(struct machine *machine, union perf_event *event,
					struct perf_sample *sample);
int machine__process_aux_event(struct machine *machine,
			       union perf_event *event);
int machine__process_itrace_start_event(struct machine *machine,
					union perf_event *event);
int machine__process_aux_output_hw_id_event(struct machine *machine,
					    union perf_event *event);
int machine__process_switch_event(struct machine *machine,
				  union perf_event *event);
int machine__process_namespaces_event(struct machine *machine,
				      union perf_event *event,
				      struct perf_sample *sample);
int machine__process_cgroup_event(struct machine *machine,
				  union perf_event *event,
				  struct perf_sample *sample);
int machine__process_mmap_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_mmap2_event(struct machine *machine, union perf_event *event,
				 struct perf_sample *sample);
int machine__process_ksymbol(struct machine *machine,
			     union perf_event *event,
			     struct perf_sample *sample);
int machine__process_text_poke(struct machine *machine,
			       union perf_event *event,
			       struct perf_sample *sample);
int machine__process_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);

typedef void (*machine__process_t)(struct machine *machine, void *data);

struct machines {
	struct machine host;
	struct rb_root_cached guests;
};

void machines__init(struct machines *machines);
void machines__exit(struct machines *machines);

void machines__process_guests(struct machines *machines,
			      machine__process_t process, void *data);

struct machine *machines__add(struct machines *machines, pid_t pid,
			      const char *root_dir);
struct machine *machines__find_host(struct machines *machines);
struct machine *machines__find(struct machines *machines, pid_t pid);
struct machine *machines__findnew(struct machines *machines, pid_t pid);
struct machine *machines__find_guest(struct machines *machines, pid_t pid);

void machines__set_id_hdr_size(struct machines *machines, u16 id_hdr_size);
void machines__set_comm_exec(struct machines *machines, bool comm_exec);

struct machine *machine__new_host(void);
struct machine *machine__new_kallsyms(void);
int machine__init(struct machine *machine, const char *root_dir, pid_t pid);
void machine__exit(struct machine *machine);
void machine__delete_threads(struct machine *machine);
void machine__delete(struct machine *machine);
void machine__remove_thread(struct machine *machine, struct thread *th);

struct branch_info *sample__resolve_bstack(struct perf_sample *sample,
					   struct addr_location *al);
struct mem_info *sample__resolve_mem(struct perf_sample *sample,
				     struct addr_location *al);

struct callchain_cursor;

int thread__resolve_callchain(struct thread *thread,
			      struct callchain_cursor *cursor,
			      struct evsel *evsel,
			      struct perf_sample *sample,
			      struct symbol **parent,
			      struct addr_location *root_al,
			      int max_stack);

/*
 * Default guest kernel is defined by parameter --guestkallsyms
 * and --guestmodules
 */
static inline bool machine__is_default_guest(struct machine *machine)
{
	return machine ? machine->pid == DEFAULT_GUEST_KERNEL_ID : false;
}

static inline bool machine__is_host(struct machine *machine)
{
	return machine ? machine->pid == HOST_KERNEL_ID : false;
}

bool machine__is(struct machine *machine, const char *arch);
bool machine__normalized_is(struct machine *machine, const char *arch);
int machine__nr_cpus_avail(struct machine *machine);

struct thread *__machine__findnew_thread(struct machine *machine, pid_t pid, pid_t tid);
struct thread *machine__findnew_thread(struct machine *machine, pid_t pid, pid_t tid);

struct dso *machine__findnew_dso_id(struct machine *machine, const char *filename, struct dso_id *id);
struct dso *machine__findnew_dso(struct machine *machine, const char *filename);

size_t machine__fprintf(struct machine *machine, FILE *fp);

static inline
struct symbol *machine__find_kernel_symbol(struct machine *machine, u64 addr,
					   struct map **mapp)
{
	return maps__find_symbol(machine->kmaps, addr, mapp);
}

static inline
struct symbol *machine__find_kernel_symbol_by_name(struct machine *machine,
						   const char *name,
						   struct map **mapp)
{
	return maps__find_symbol_by_name(machine->kmaps, name, mapp);
}

int arch__fix_module_text_start(u64 *start, u64 *size, const char *name);

int machine__load_kallsyms(struct machine *machine, const char *filename);

int machine__load_vmlinux_path(struct machine *machine);

size_t machine__fprintf_dsos_buildid(struct machine *machine, FILE *fp,
				     bool (skip)(struct dso *dso, int parm), int parm);
size_t machines__fprintf_dsos(struct machines *machines, FILE *fp);
size_t machines__fprintf_dsos_buildid(struct machines *machines, FILE *fp,
				     bool (skip)(struct dso *dso, int parm), int parm);

void machine__destroy_kernel_maps(struct machine *machine);
int machine__create_kernel_maps(struct machine *machine);

int machines__create_kernel_maps(struct machines *machines, pid_t pid);
int machines__create_guest_kernel_maps(struct machines *machines);
void machines__destroy_kernel_maps(struct machines *machines);

size_t machine__fprintf_vmlinux_path(struct machine *machine, FILE *fp);

typedef int (*machine__dso_t)(struct dso *dso, struct machine *machine, void *priv);

int machine__for_each_dso(struct machine *machine, machine__dso_t fn,
			  void *priv);
int machine__for_each_thread(struct machine *machine,
			     int (*fn)(struct thread *thread, void *p),
			     void *priv);
int machines__for_each_thread(struct machines *machines,
			      int (*fn)(struct thread *thread, void *p),
			      void *priv);

pid_t machine__get_current_tid(struct machine *machine, int cpu);
int machine__set_current_tid(struct machine *machine, int cpu, pid_t pid,
			     pid_t tid);
/*
 * For use with libtraceevent's tep_set_function_resolver()
 */
char *machine__resolve_kernel_addr(void *vmachine, unsigned long long *addrp, char **modp);

void machine__get_kallsyms_filename(struct machine *machine, char *buf,
				    size_t bufsz);

int machine__create_extra_kernel_maps(struct machine *machine,
				      struct dso *kernel);

/* Kernel-space maps for symbols that are outside the main kernel map and module maps */
struct extra_kernel_map {
	u64 start;
	u64 end;
	u64 pgoff;
	char name[KMAP_NAME_LEN];
};

int machine__create_extra_kernel_map(struct machine *machine,
				     struct dso *kernel,
				     struct extra_kernel_map *xm);

int machine__map_x86_64_entry_trampolines(struct machine *machine,
					  struct dso *kernel);

#endif /* __PERF_MACHINE_H */
