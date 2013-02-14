#ifndef __PERF_MACHINE_H
#define __PERF_MACHINE_H

#include <sys/types.h>
#include <linux/rbtree.h>
#include "map.h"

struct branch_stack;
struct perf_evsel;
struct perf_sample;
struct symbol;
struct thread;
union perf_event;

/* Native host kernel uses -1 as pid index in machine */
#define	HOST_KERNEL_ID			(-1)
#define	DEFAULT_GUEST_KERNEL_ID		(0)

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
};

static inline
struct map *machine__kernel_map(struct machine *machine, enum map_type type)
{
	return machine->vmlinux_maps[type];
}

struct thread *machine__find_thread(struct machine *machine, pid_t pid);

int machine__process_comm_event(struct machine *machine, union perf_event *event);
int machine__process_exit_event(struct machine *machine, union perf_event *event);
int machine__process_fork_event(struct machine *machine, union perf_event *event);
int machine__process_lost_event(struct machine *machine, union perf_event *event);
int machine__process_mmap_event(struct machine *machine, union perf_event *event);
int machine__process_event(struct machine *machine, union perf_event *event);

typedef void (*machine__process_t)(struct machine *machine, void *data);

void machines__process(struct rb_root *machines,
		       machine__process_t process, void *data);

struct machine *machines__add(struct rb_root *machines, pid_t pid,
			      const char *root_dir);
struct machine *machines__find_host(struct rb_root *machines);
struct machine *machines__find(struct rb_root *machines, pid_t pid);
struct machine *machines__findnew(struct rb_root *machines, pid_t pid);

void machines__set_id_hdr_size(struct rb_root *machines, u16 id_hdr_size);
char *machine__mmap_name(struct machine *machine, char *bf, size_t size);

int machine__init(struct machine *machine, const char *root_dir, pid_t pid);
void machine__exit(struct machine *machine);
void machine__delete(struct machine *machine);


struct branch_info *machine__resolve_bstack(struct machine *machine,
					    struct thread *thread,
					    struct branch_stack *bs);
int machine__resolve_callchain(struct machine *machine,
			       struct perf_evsel *evsel,
			       struct thread *thread,
			       struct perf_sample *sample,
			       struct symbol **parent);

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

struct thread *machine__findnew_thread(struct machine *machine, pid_t pid);
void machine__remove_thread(struct machine *machine, struct thread *th);

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

size_t machine__fprintf_dsos_buildid(struct machine *machine,
				     FILE *fp, bool with_hits);
size_t machines__fprintf_dsos(struct rb_root *machines, FILE *fp);
size_t machines__fprintf_dsos_buildid(struct rb_root *machines,
				      FILE *fp, bool with_hits);

void machine__destroy_kernel_maps(struct machine *machine);
int __machine__create_kernel_maps(struct machine *machine, struct dso *kernel);
int machine__create_kernel_maps(struct machine *machine);

int machines__create_kernel_maps(struct rb_root *machines, pid_t pid);
int machines__create_guest_kernel_maps(struct rb_root *machines);
void machines__destroy_guest_kernel_maps(struct rb_root *machines);

size_t machine__fprintf_vmlinux_path(struct machine *machine, FILE *fp);

#endif /* __PERF_MACHINE_H */
