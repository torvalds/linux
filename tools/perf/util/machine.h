#ifndef __PERF_MACHINE_H
#define __PERF_MACHINE_H

#include <sys/types.h>
#include <linux/rbtree.h>
#include "map.h"
#include "event.h"

struct addr_location;
struct branch_stack;
struct perf_evsel;
struct perf_sample;
struct symbol;
struct thread;
union perf_event;

/* Native host kernel uses -1 as pid index in machine */
#define	HOST_KERNEL_ID			(-1)
#define	DEFAULT_GUEST_KERNEL_ID		(0)

extern const char *ref_reloc_sym_names[];

struct machine {
	struct rb_node	  rb_node;
	pid_t		  pid;
	u16		  id_hdr_size;
	char		  *root_dir;
	struct rb_root	  threads;
	struct list_head  dead_threads;
	struct thread	  *last_match;
	struct list_head  user_dsos;
	struct list_head  kernel_dsos;
	struct map_groups kmaps;
	struct map	  *vmlinux_maps[MAP__NR_TYPES];
	symbol_filter_t	  symbol_filter;
};

static inline
struct map *machine__kernel_map(struct machine *machine, enum map_type type)
{
	return machine->vmlinux_maps[type];
}

struct thread *machine__find_thread(struct machine *machine, pid_t pid,
				    pid_t tid);

int machine__process_comm_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_exit_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_fork_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_lost_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_mmap_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);
int machine__process_mmap2_event(struct machine *machine, union perf_event *event,
				 struct perf_sample *sample);
int machine__process_event(struct machine *machine, union perf_event *event,
				struct perf_sample *sample);

typedef void (*machine__process_t)(struct machine *machine, void *data);

struct machines {
	struct machine host;
	struct rb_root guests;
	symbol_filter_t symbol_filter;
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

void machines__set_id_hdr_size(struct machines *machines, u16 id_hdr_size);
char *machine__mmap_name(struct machine *machine, char *bf, size_t size);

void machines__set_symbol_filter(struct machines *machines,
				 symbol_filter_t symbol_filter);

struct machine *machine__new_host(void);
int machine__init(struct machine *machine, const char *root_dir, pid_t pid);
void machine__exit(struct machine *machine);
void machine__delete_dead_threads(struct machine *machine);
void machine__delete_threads(struct machine *machine);
void machine__delete(struct machine *machine);

struct branch_info *sample__resolve_bstack(struct perf_sample *sample,
					   struct addr_location *al);
struct mem_info *sample__resolve_mem(struct perf_sample *sample,
				     struct addr_location *al);
int machine__resolve_callchain(struct machine *machine,
			       struct perf_evsel *evsel,
			       struct thread *thread,
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

struct thread *machine__findnew_thread(struct machine *machine, pid_t pid,
				       pid_t tid);

size_t machine__fprintf(struct machine *machine, FILE *fp);

static inline
struct symbol *machine__find_kernel_symbol(struct machine *machine,
					   enum map_type type, u64 addr,
					   struct map **mapp,
					   symbol_filter_t filter)
{
	return map_groups__find_symbol(&machine->kmaps, type, addr,
				       mapp, filter);
}

static inline
struct symbol *machine__find_kernel_function(struct machine *machine, u64 addr,
					     struct map **mapp,
					     symbol_filter_t filter)
{
	return machine__find_kernel_symbol(machine, MAP__FUNCTION, addr,
					   mapp, filter);
}

static inline
struct symbol *machine__find_kernel_function_by_name(struct machine *machine,
						     const char *name,
						     struct map **mapp,
						     symbol_filter_t filter)
{
	return map_groups__find_function_by_name(&machine->kmaps, name, mapp,
						 filter);
}

struct map *machine__new_module(struct machine *machine, u64 start,
				const char *filename);

int machine__load_kallsyms(struct machine *machine, const char *filename,
			   enum map_type type, symbol_filter_t filter);
int machine__load_vmlinux_path(struct machine *machine, enum map_type type,
			       symbol_filter_t filter);

size_t machine__fprintf_dsos_buildid(struct machine *machine, FILE *fp,
				     bool (skip)(struct dso *dso, int parm), int parm);
size_t machines__fprintf_dsos(struct machines *machines, FILE *fp);
size_t machines__fprintf_dsos_buildid(struct machines *machines, FILE *fp,
				     bool (skip)(struct dso *dso, int parm), int parm);

void machine__destroy_kernel_maps(struct machine *machine);
int __machine__create_kernel_maps(struct machine *machine, struct dso *kernel);
int machine__create_kernel_maps(struct machine *machine);

int machines__create_kernel_maps(struct machines *machines, pid_t pid);
int machines__create_guest_kernel_maps(struct machines *machines);
void machines__destroy_kernel_maps(struct machines *machines);

size_t machine__fprintf_vmlinux_path(struct machine *machine, FILE *fp);

int machine__for_each_thread(struct machine *machine,
			     int (*fn)(struct thread *thread, void *p),
			     void *priv);

int __machine__synthesize_threads(struct machine *machine, struct perf_tool *tool,
				  struct target *target, struct thread_map *threads,
				  perf_event__handler_t process, bool data_mmap);
static inline
int machine__synthesize_threads(struct machine *machine, struct target *target,
				struct thread_map *threads, bool data_mmap)
{
	return __machine__synthesize_threads(machine, NULL, target, threads,
					     perf_event__process, data_mmap);
}

#endif /* __PERF_MACHINE_H */
