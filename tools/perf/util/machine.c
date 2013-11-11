#include "callchain.h"
#include "debug.h"
#include "event.h"
#include "evsel.h"
#include "hist.h"
#include "machine.h"
#include "map.h"
#include "sort.h"
#include "strlist.h"
#include "thread.h"
#include <stdbool.h>
#include "unwind.h"

int machine__init(struct machine *machine, const char *root_dir, pid_t pid)
{
	map_groups__init(&machine->kmaps);
	RB_CLEAR_NODE(&machine->rb_node);
	INIT_LIST_HEAD(&machine->user_dsos);
	INIT_LIST_HEAD(&machine->kernel_dsos);

	machine->threads = RB_ROOT;
	INIT_LIST_HEAD(&machine->dead_threads);
	machine->last_match = NULL;

	machine->kmaps.machine = machine;
	machine->pid = pid;

	machine->root_dir = strdup(root_dir);
	if (machine->root_dir == NULL)
		return -ENOMEM;

	if (pid != HOST_KERNEL_ID) {
		struct thread *thread = machine__findnew_thread(machine, pid);
		char comm[64];

		if (thread == NULL)
			return -ENOMEM;

		snprintf(comm, sizeof(comm), "[guest/%d]", pid);
		thread__set_comm(thread, comm);
	}

	return 0;
}

static void dsos__delete(struct list_head *dsos)
{
	struct dso *pos, *n;

	list_for_each_entry_safe(pos, n, dsos, node) {
		list_del(&pos->node);
		dso__delete(pos);
	}
}

void machine__delete_dead_threads(struct machine *machine)
{
	struct thread *n, *t;

	list_for_each_entry_safe(t, n, &machine->dead_threads, node) {
		list_del(&t->node);
		thread__delete(t);
	}
}

void machine__delete_threads(struct machine *machine)
{
	struct rb_node *nd = rb_first(&machine->threads);

	while (nd) {
		struct thread *t = rb_entry(nd, struct thread, rb_node);

		rb_erase(&t->rb_node, &machine->threads);
		nd = rb_next(nd);
		thread__delete(t);
	}
}

void machine__exit(struct machine *machine)
{
	map_groups__exit(&machine->kmaps);
	dsos__delete(&machine->user_dsos);
	dsos__delete(&machine->kernel_dsos);
	free(machine->root_dir);
	machine->root_dir = NULL;
}

void machine__delete(struct machine *machine)
{
	machine__exit(machine);
	free(machine);
}

void machines__init(struct machines *machines)
{
	machine__init(&machines->host, "", HOST_KERNEL_ID);
	machines->guests = RB_ROOT;
}

void machines__exit(struct machines *machines)
{
	machine__exit(&machines->host);
	/* XXX exit guest */
}

struct machine *machines__add(struct machines *machines, pid_t pid,
			      const char *root_dir)
{
	struct rb_node **p = &machines->guests.rb_node;
	struct rb_node *parent = NULL;
	struct machine *pos, *machine = malloc(sizeof(*machine));

	if (machine == NULL)
		return NULL;

	if (machine__init(machine, root_dir, pid) != 0) {
		free(machine);
		return NULL;
	}

	while (*p != NULL) {
		parent = *p;
		pos = rb_entry(parent, struct machine, rb_node);
		if (pid < pos->pid)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&machine->rb_node, parent, p);
	rb_insert_color(&machine->rb_node, &machines->guests);

	return machine;
}

struct machine *machines__find(struct machines *machines, pid_t pid)
{
	struct rb_node **p = &machines->guests.rb_node;
	struct rb_node *parent = NULL;
	struct machine *machine;
	struct machine *default_machine = NULL;

	if (pid == HOST_KERNEL_ID)
		return &machines->host;

	while (*p != NULL) {
		parent = *p;
		machine = rb_entry(parent, struct machine, rb_node);
		if (pid < machine->pid)
			p = &(*p)->rb_left;
		else if (pid > machine->pid)
			p = &(*p)->rb_right;
		else
			return machine;
		if (!machine->pid)
			default_machine = machine;
	}

	return default_machine;
}

struct machine *machines__findnew(struct machines *machines, pid_t pid)
{
	char path[PATH_MAX];
	const char *root_dir = "";
	struct machine *machine = machines__find(machines, pid);

	if (machine && (machine->pid == pid))
		goto out;

	if ((pid != HOST_KERNEL_ID) &&
	    (pid != DEFAULT_GUEST_KERNEL_ID) &&
	    (symbol_conf.guestmount)) {
		sprintf(path, "%s/%d", symbol_conf.guestmount, pid);
		if (access(path, R_OK)) {
			static struct strlist *seen;

			if (!seen)
				seen = strlist__new(true, NULL);

			if (!strlist__has_entry(seen, path)) {
				pr_err("Can't access file %s\n", path);
				strlist__add(seen, path);
			}
			machine = NULL;
			goto out;
		}
		root_dir = path;
	}

	machine = machines__add(machines, pid, root_dir);
out:
	return machine;
}

void machines__process_guests(struct machines *machines,
			      machine__process_t process, void *data)
{
	struct rb_node *nd;

	for (nd = rb_first(&machines->guests); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		process(pos, data);
	}
}

char *machine__mmap_name(struct machine *machine, char *bf, size_t size)
{
	if (machine__is_host(machine))
		snprintf(bf, size, "[%s]", "kernel.kallsyms");
	else if (machine__is_default_guest(machine))
		snprintf(bf, size, "[%s]", "guest.kernel.kallsyms");
	else {
		snprintf(bf, size, "[%s.%d]", "guest.kernel.kallsyms",
			 machine->pid);
	}

	return bf;
}

void machines__set_id_hdr_size(struct machines *machines, u16 id_hdr_size)
{
	struct rb_node *node;
	struct machine *machine;

	machines->host.id_hdr_size = id_hdr_size;

	for (node = rb_first(&machines->guests); node; node = rb_next(node)) {
		machine = rb_entry(node, struct machine, rb_node);
		machine->id_hdr_size = id_hdr_size;
	}

	return;
}

static struct thread *__machine__findnew_thread(struct machine *machine, pid_t pid,
						bool create)
{
	struct rb_node **p = &machine->threads.rb_node;
	struct rb_node *parent = NULL;
	struct thread *th;

	/*
	 * Font-end cache - PID lookups come in blocks,
	 * so most of the time we dont have to look up
	 * the full rbtree:
	 */
	if (machine->last_match && machine->last_match->pid == pid)
		return machine->last_match;

	while (*p != NULL) {
		parent = *p;
		th = rb_entry(parent, struct thread, rb_node);

		if (th->pid == pid) {
			machine->last_match = th;
			return th;
		}

		if (pid < th->pid)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	if (!create)
		return NULL;

	th = thread__new(pid);
	if (th != NULL) {
		rb_link_node(&th->rb_node, parent, p);
		rb_insert_color(&th->rb_node, &machine->threads);
		machine->last_match = th;
	}

	return th;
}

struct thread *machine__findnew_thread(struct machine *machine, pid_t pid)
{
	return __machine__findnew_thread(machine, pid, true);
}

struct thread *machine__find_thread(struct machine *machine, pid_t pid)
{
	return __machine__findnew_thread(machine, pid, false);
}

int machine__process_comm_event(struct machine *machine, union perf_event *event)
{
	struct thread *thread = machine__findnew_thread(machine, event->comm.tid);

	if (dump_trace)
		perf_event__fprintf_comm(event, stdout);

	if (thread == NULL || thread__set_comm(thread, event->comm.comm)) {
		dump_printf("problem processing PERF_RECORD_COMM, skipping event.\n");
		return -1;
	}

	return 0;
}

int machine__process_lost_event(struct machine *machine __maybe_unused,
				union perf_event *event)
{
	dump_printf(": id:%" PRIu64 ": lost:%" PRIu64 "\n",
		    event->lost.id, event->lost.lost);
	return 0;
}

struct map *machine__new_module(struct machine *machine, u64 start,
				const char *filename)
{
	struct map *map;
	struct dso *dso = __dsos__findnew(&machine->kernel_dsos, filename);

	if (dso == NULL)
		return NULL;

	map = map__new2(start, dso, MAP__FUNCTION);
	if (map == NULL)
		return NULL;

	if (machine__is_host(machine))
		dso->symtab_type = DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE;
	else
		dso->symtab_type = DSO_BINARY_TYPE__GUEST_KMODULE;
	map_groups__insert(&machine->kmaps, map);
	return map;
}

size_t machines__fprintf_dsos(struct machines *machines, FILE *fp)
{
	struct rb_node *nd;
	size_t ret = __dsos__fprintf(&machines->host.kernel_dsos, fp) +
		     __dsos__fprintf(&machines->host.user_dsos, fp);

	for (nd = rb_first(&machines->guests); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		ret += __dsos__fprintf(&pos->kernel_dsos, fp);
		ret += __dsos__fprintf(&pos->user_dsos, fp);
	}

	return ret;
}

size_t machine__fprintf_dsos_buildid(struct machine *machine, FILE *fp,
				     bool (skip)(struct dso *dso, int parm), int parm)
{
	return __dsos__fprintf_buildid(&machine->kernel_dsos, fp, skip, parm) +
	       __dsos__fprintf_buildid(&machine->user_dsos, fp, skip, parm);
}

size_t machines__fprintf_dsos_buildid(struct machines *machines, FILE *fp,
				     bool (skip)(struct dso *dso, int parm), int parm)
{
	struct rb_node *nd;
	size_t ret = machine__fprintf_dsos_buildid(&machines->host, fp, skip, parm);

	for (nd = rb_first(&machines->guests); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		ret += machine__fprintf_dsos_buildid(pos, fp, skip, parm);
	}
	return ret;
}

size_t machine__fprintf_vmlinux_path(struct machine *machine, FILE *fp)
{
	int i;
	size_t printed = 0;
	struct dso *kdso = machine->vmlinux_maps[MAP__FUNCTION]->dso;

	if (kdso->has_build_id) {
		char filename[PATH_MAX];
		if (dso__build_id_filename(kdso, filename, sizeof(filename)))
			printed += fprintf(fp, "[0] %s\n", filename);
	}

	for (i = 0; i < vmlinux_path__nr_entries; ++i)
		printed += fprintf(fp, "[%d] %s\n",
				   i + kdso->has_build_id, vmlinux_path[i]);

	return printed;
}

size_t machine__fprintf(struct machine *machine, FILE *fp)
{
	size_t ret = 0;
	struct rb_node *nd;

	for (nd = rb_first(&machine->threads); nd; nd = rb_next(nd)) {
		struct thread *pos = rb_entry(nd, struct thread, rb_node);

		ret += thread__fprintf(pos, fp);
	}

	return ret;
}

static struct dso *machine__get_kernel(struct machine *machine)
{
	const char *vmlinux_name = NULL;
	struct dso *kernel;

	if (machine__is_host(machine)) {
		vmlinux_name = symbol_conf.vmlinux_name;
		if (!vmlinux_name)
			vmlinux_name = "[kernel.kallsyms]";

		kernel = dso__kernel_findnew(machine, vmlinux_name,
					     "[kernel]",
					     DSO_TYPE_KERNEL);
	} else {
		char bf[PATH_MAX];

		if (machine__is_default_guest(machine))
			vmlinux_name = symbol_conf.default_guest_vmlinux_name;
		if (!vmlinux_name)
			vmlinux_name = machine__mmap_name(machine, bf,
							  sizeof(bf));

		kernel = dso__kernel_findnew(machine, vmlinux_name,
					     "[guest.kernel]",
					     DSO_TYPE_GUEST_KERNEL);
	}

	if (kernel != NULL && (!kernel->has_build_id))
		dso__read_running_kernel_build_id(kernel, machine);

	return kernel;
}

struct process_args {
	u64 start;
};

static int symbol__in_kernel(void *arg, const char *name,
			     char type __maybe_unused, u64 start)
{
	struct process_args *args = arg;

	if (strchr(name, '['))
		return 0;

	args->start = start;
	return 1;
}

/* Figure out the start address of kernel map from /proc/kallsyms */
static u64 machine__get_kernel_start_addr(struct machine *machine)
{
	const char *filename;
	char path[PATH_MAX];
	struct process_args args;

	if (machine__is_host(machine)) {
		filename = "/proc/kallsyms";
	} else {
		if (machine__is_default_guest(machine))
			filename = (char *)symbol_conf.default_guest_kallsyms;
		else {
			sprintf(path, "%s/proc/kallsyms", machine->root_dir);
			filename = path;
		}
	}

	if (symbol__restricted_filename(filename, "/proc/kallsyms"))
		return 0;

	if (kallsyms__parse(filename, &args, symbol__in_kernel) <= 0)
		return 0;

	return args.start;
}

int __machine__create_kernel_maps(struct machine *machine, struct dso *kernel)
{
	enum map_type type;
	u64 start = machine__get_kernel_start_addr(machine);

	for (type = 0; type < MAP__NR_TYPES; ++type) {
		struct kmap *kmap;

		machine->vmlinux_maps[type] = map__new2(start, kernel, type);
		if (machine->vmlinux_maps[type] == NULL)
			return -1;

		machine->vmlinux_maps[type]->map_ip =
			machine->vmlinux_maps[type]->unmap_ip =
				identity__map_ip;
		kmap = map__kmap(machine->vmlinux_maps[type]);
		kmap->kmaps = &machine->kmaps;
		map_groups__insert(&machine->kmaps,
				   machine->vmlinux_maps[type]);
	}

	return 0;
}

void machine__destroy_kernel_maps(struct machine *machine)
{
	enum map_type type;

	for (type = 0; type < MAP__NR_TYPES; ++type) {
		struct kmap *kmap;

		if (machine->vmlinux_maps[type] == NULL)
			continue;

		kmap = map__kmap(machine->vmlinux_maps[type]);
		map_groups__remove(&machine->kmaps,
				   machine->vmlinux_maps[type]);
		if (kmap->ref_reloc_sym) {
			/*
			 * ref_reloc_sym is shared among all maps, so free just
			 * on one of them.
			 */
			if (type == MAP__FUNCTION) {
				free((char *)kmap->ref_reloc_sym->name);
				kmap->ref_reloc_sym->name = NULL;
				free(kmap->ref_reloc_sym);
			}
			kmap->ref_reloc_sym = NULL;
		}

		map__delete(machine->vmlinux_maps[type]);
		machine->vmlinux_maps[type] = NULL;
	}
}

int machines__create_guest_kernel_maps(struct machines *machines)
{
	int ret = 0;
	struct dirent **namelist = NULL;
	int i, items = 0;
	char path[PATH_MAX];
	pid_t pid;
	char *endp;

	if (symbol_conf.default_guest_vmlinux_name ||
	    symbol_conf.default_guest_modules ||
	    symbol_conf.default_guest_kallsyms) {
		machines__create_kernel_maps(machines, DEFAULT_GUEST_KERNEL_ID);
	}

	if (symbol_conf.guestmount) {
		items = scandir(symbol_conf.guestmount, &namelist, NULL, NULL);
		if (items <= 0)
			return -ENOENT;
		for (i = 0; i < items; i++) {
			if (!isdigit(namelist[i]->d_name[0])) {
				/* Filter out . and .. */
				continue;
			}
			pid = (pid_t)strtol(namelist[i]->d_name, &endp, 10);
			if ((*endp != '\0') ||
			    (endp == namelist[i]->d_name) ||
			    (errno == ERANGE)) {
				pr_debug("invalid directory (%s). Skipping.\n",
					 namelist[i]->d_name);
				continue;
			}
			sprintf(path, "%s/%s/proc/kallsyms",
				symbol_conf.guestmount,
				namelist[i]->d_name);
			ret = access(path, R_OK);
			if (ret) {
				pr_debug("Can't access file %s\n", path);
				goto failure;
			}
			machines__create_kernel_maps(machines, pid);
		}
failure:
		free(namelist);
	}

	return ret;
}

void machines__destroy_kernel_maps(struct machines *machines)
{
	struct rb_node *next = rb_first(&machines->guests);

	machine__destroy_kernel_maps(&machines->host);

	while (next) {
		struct machine *pos = rb_entry(next, struct machine, rb_node);

		next = rb_next(&pos->rb_node);
		rb_erase(&pos->rb_node, &machines->guests);
		machine__delete(pos);
	}
}

int machines__create_kernel_maps(struct machines *machines, pid_t pid)
{
	struct machine *machine = machines__findnew(machines, pid);

	if (machine == NULL)
		return -1;

	return machine__create_kernel_maps(machine);
}

int machine__load_kallsyms(struct machine *machine, const char *filename,
			   enum map_type type, symbol_filter_t filter)
{
	struct map *map = machine->vmlinux_maps[type];
	int ret = dso__load_kallsyms(map->dso, filename, map, filter);

	if (ret > 0) {
		dso__set_loaded(map->dso, type);
		/*
		 * Since /proc/kallsyms will have multiple sessions for the
		 * kernel, with modules between them, fixup the end of all
		 * sections.
		 */
		__map_groups__fixup_end(&machine->kmaps, type);
	}

	return ret;
}

int machine__load_vmlinux_path(struct machine *machine, enum map_type type,
			       symbol_filter_t filter)
{
	struct map *map = machine->vmlinux_maps[type];
	int ret = dso__load_vmlinux_path(map->dso, map, filter);

	if (ret > 0) {
		dso__set_loaded(map->dso, type);
		map__reloc_vmlinux(map);
	}

	return ret;
}

static void map_groups__fixup_end(struct map_groups *mg)
{
	int i;
	for (i = 0; i < MAP__NR_TYPES; ++i)
		__map_groups__fixup_end(mg, i);
}

static char *get_kernel_version(const char *root_dir)
{
	char version[PATH_MAX];
	FILE *file;
	char *name, *tmp;
	const char *prefix = "Linux version ";

	sprintf(version, "%s/proc/version", root_dir);
	file = fopen(version, "r");
	if (!file)
		return NULL;

	version[0] = '\0';
	tmp = fgets(version, sizeof(version), file);
	fclose(file);

	name = strstr(version, prefix);
	if (!name)
		return NULL;
	name += strlen(prefix);
	tmp = strchr(name, ' ');
	if (tmp)
		*tmp = '\0';

	return strdup(name);
}

static int map_groups__set_modules_path_dir(struct map_groups *mg,
				const char *dir_name)
{
	struct dirent *dent;
	DIR *dir = opendir(dir_name);
	int ret = 0;

	if (!dir) {
		pr_debug("%s: cannot open %s dir\n", __func__, dir_name);
		return -1;
	}

	while ((dent = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		struct stat st;

		/*sshfs might return bad dent->d_type, so we have to stat*/
		snprintf(path, sizeof(path), "%s/%s", dir_name, dent->d_name);
		if (stat(path, &st))
			continue;

		if (S_ISDIR(st.st_mode)) {
			if (!strcmp(dent->d_name, ".") ||
			    !strcmp(dent->d_name, ".."))
				continue;

			ret = map_groups__set_modules_path_dir(mg, path);
			if (ret < 0)
				goto out;
		} else {
			char *dot = strrchr(dent->d_name, '.'),
			     dso_name[PATH_MAX];
			struct map *map;
			char *long_name;

			if (dot == NULL || strcmp(dot, ".ko"))
				continue;
			snprintf(dso_name, sizeof(dso_name), "[%.*s]",
				 (int)(dot - dent->d_name), dent->d_name);

			strxfrchar(dso_name, '-', '_');
			map = map_groups__find_by_name(mg, MAP__FUNCTION,
						       dso_name);
			if (map == NULL)
				continue;

			long_name = strdup(path);
			if (long_name == NULL) {
				ret = -1;
				goto out;
			}
			dso__set_long_name(map->dso, long_name);
			map->dso->lname_alloc = 1;
			dso__kernel_module_get_build_id(map->dso, "");
		}
	}

out:
	closedir(dir);
	return ret;
}

static int machine__set_modules_path(struct machine *machine)
{
	char *version;
	char modules_path[PATH_MAX];

	version = get_kernel_version(machine->root_dir);
	if (!version)
		return -1;

	snprintf(modules_path, sizeof(modules_path), "%s/lib/modules/%s/kernel",
		 machine->root_dir, version);
	free(version);

	return map_groups__set_modules_path_dir(&machine->kmaps, modules_path);
}

static int machine__create_modules(struct machine *machine)
{
	char *line = NULL;
	size_t n;
	FILE *file;
	struct map *map;
	const char *modules;
	char path[PATH_MAX];

	if (machine__is_default_guest(machine))
		modules = symbol_conf.default_guest_modules;
	else {
		sprintf(path, "%s/proc/modules", machine->root_dir);
		modules = path;
	}

	if (symbol__restricted_filename(path, "/proc/modules"))
		return -1;

	file = fopen(modules, "r");
	if (file == NULL)
		return -1;

	while (!feof(file)) {
		char name[PATH_MAX];
		u64 start;
		char *sep;
		int line_len;

		line_len = getline(&line, &n, file);
		if (line_len < 0)
			break;

		if (!line)
			goto out_failure;

		line[--line_len] = '\0'; /* \n */

		sep = strrchr(line, 'x');
		if (sep == NULL)
			continue;

		hex2u64(sep + 1, &start);

		sep = strchr(line, ' ');
		if (sep == NULL)
			continue;

		*sep = '\0';

		snprintf(name, sizeof(name), "[%s]", line);
		map = machine__new_module(machine, start, name);
		if (map == NULL)
			goto out_delete_line;
		dso__kernel_module_get_build_id(map->dso, machine->root_dir);
	}

	free(line);
	fclose(file);

	return machine__set_modules_path(machine);

out_delete_line:
	free(line);
out_failure:
	return -1;
}

int machine__create_kernel_maps(struct machine *machine)
{
	struct dso *kernel = machine__get_kernel(machine);

	if (kernel == NULL ||
	    __machine__create_kernel_maps(machine, kernel) < 0)
		return -1;

	if (symbol_conf.use_modules && machine__create_modules(machine) < 0) {
		if (machine__is_host(machine))
			pr_debug("Problems creating module maps, "
				 "continuing anyway...\n");
		else
			pr_debug("Problems creating module maps for guest %d, "
				 "continuing anyway...\n", machine->pid);
	}

	/*
	 * Now that we have all the maps created, just set the ->end of them:
	 */
	map_groups__fixup_end(&machine->kmaps);
	return 0;
}

static void machine__set_kernel_mmap_len(struct machine *machine,
					 union perf_event *event)
{
	int i;

	for (i = 0; i < MAP__NR_TYPES; i++) {
		machine->vmlinux_maps[i]->start = event->mmap.start;
		machine->vmlinux_maps[i]->end   = (event->mmap.start +
						   event->mmap.len);
		/*
		 * Be a bit paranoid here, some perf.data file came with
		 * a zero sized synthesized MMAP event for the kernel.
		 */
		if (machine->vmlinux_maps[i]->end == 0)
			machine->vmlinux_maps[i]->end = ~0ULL;
	}
}

static int machine__process_kernel_mmap_event(struct machine *machine,
					      union perf_event *event)
{
	struct map *map;
	char kmmap_prefix[PATH_MAX];
	enum dso_kernel_type kernel_type;
	bool is_kernel_mmap;

	machine__mmap_name(machine, kmmap_prefix, sizeof(kmmap_prefix));
	if (machine__is_host(machine))
		kernel_type = DSO_TYPE_KERNEL;
	else
		kernel_type = DSO_TYPE_GUEST_KERNEL;

	is_kernel_mmap = memcmp(event->mmap.filename,
				kmmap_prefix,
				strlen(kmmap_prefix) - 1) == 0;
	if (event->mmap.filename[0] == '/' ||
	    (!is_kernel_mmap && event->mmap.filename[0] == '[')) {

		char short_module_name[1024];
		char *name, *dot;

		if (event->mmap.filename[0] == '/') {
			name = strrchr(event->mmap.filename, '/');
			if (name == NULL)
				goto out_problem;

			++name; /* skip / */
			dot = strrchr(name, '.');
			if (dot == NULL)
				goto out_problem;
			snprintf(short_module_name, sizeof(short_module_name),
					"[%.*s]", (int)(dot - name), name);
			strxfrchar(short_module_name, '-', '_');
		} else
			strcpy(short_module_name, event->mmap.filename);

		map = machine__new_module(machine, event->mmap.start,
					  event->mmap.filename);
		if (map == NULL)
			goto out_problem;

		name = strdup(short_module_name);
		if (name == NULL)
			goto out_problem;

		map->dso->short_name = name;
		map->dso->sname_alloc = 1;
		map->end = map->start + event->mmap.len;
	} else if (is_kernel_mmap) {
		const char *symbol_name = (event->mmap.filename +
				strlen(kmmap_prefix));
		/*
		 * Should be there already, from the build-id table in
		 * the header.
		 */
		struct dso *kernel = __dsos__findnew(&machine->kernel_dsos,
						     kmmap_prefix);
		if (kernel == NULL)
			goto out_problem;

		kernel->kernel = kernel_type;
		if (__machine__create_kernel_maps(machine, kernel) < 0)
			goto out_problem;

		machine__set_kernel_mmap_len(machine, event);

		/*
		 * Avoid using a zero address (kptr_restrict) for the ref reloc
		 * symbol. Effectively having zero here means that at record
		 * time /proc/sys/kernel/kptr_restrict was non zero.
		 */
		if (event->mmap.pgoff != 0) {
			maps__set_kallsyms_ref_reloc_sym(machine->vmlinux_maps,
							 symbol_name,
							 event->mmap.pgoff);
		}

		if (machine__is_default_guest(machine)) {
			/*
			 * preload dso of guest kernel and modules
			 */
			dso__load(kernel, machine->vmlinux_maps[MAP__FUNCTION],
				  NULL);
		}
	}
	return 0;
out_problem:
	return -1;
}

int machine__process_mmap_event(struct machine *machine, union perf_event *event)
{
	u8 cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	struct thread *thread;
	struct map *map;
	enum map_type type;
	int ret = 0;

	if (dump_trace)
		perf_event__fprintf_mmap(event, stdout);

	if (cpumode == PERF_RECORD_MISC_GUEST_KERNEL ||
	    cpumode == PERF_RECORD_MISC_KERNEL) {
		ret = machine__process_kernel_mmap_event(machine, event);
		if (ret < 0)
			goto out_problem;
		return 0;
	}

	thread = machine__findnew_thread(machine, event->mmap.pid);
	if (thread == NULL)
		goto out_problem;

	if (event->header.misc & PERF_RECORD_MISC_MMAP_DATA)
		type = MAP__VARIABLE;
	else
		type = MAP__FUNCTION;

	map = map__new(&machine->user_dsos, event->mmap.start,
			event->mmap.len, event->mmap.pgoff,
			event->mmap.pid, event->mmap.filename,
			type);

	if (map == NULL)
		goto out_problem;

	thread__insert_map(thread, map);
	return 0;

out_problem:
	dump_printf("problem processing PERF_RECORD_MMAP, skipping event.\n");
	return 0;
}

int machine__process_fork_event(struct machine *machine, union perf_event *event)
{
	struct thread *thread = machine__findnew_thread(machine, event->fork.tid);
	struct thread *parent = machine__findnew_thread(machine, event->fork.ptid);

	if (dump_trace)
		perf_event__fprintf_task(event, stdout);

	if (thread == NULL || parent == NULL ||
	    thread__fork(thread, parent) < 0) {
		dump_printf("problem processing PERF_RECORD_FORK, skipping event.\n");
		return -1;
	}

	return 0;
}

static void machine__remove_thread(struct machine *machine, struct thread *th)
{
	machine->last_match = NULL;
	rb_erase(&th->rb_node, &machine->threads);
	/*
	 * We may have references to this thread, for instance in some hist_entry
	 * instances, so just move them to a separate list.
	 */
	list_add_tail(&th->node, &machine->dead_threads);
}

int machine__process_exit_event(struct machine *machine, union perf_event *event)
{
	struct thread *thread = machine__find_thread(machine, event->fork.tid);

	if (dump_trace)
		perf_event__fprintf_task(event, stdout);

	if (thread != NULL)
		machine__remove_thread(machine, thread);

	return 0;
}

int machine__process_event(struct machine *machine, union perf_event *event)
{
	int ret;

	switch (event->header.type) {
	case PERF_RECORD_COMM:
		ret = machine__process_comm_event(machine, event); break;
	case PERF_RECORD_MMAP:
		ret = machine__process_mmap_event(machine, event); break;
	case PERF_RECORD_FORK:
		ret = machine__process_fork_event(machine, event); break;
	case PERF_RECORD_EXIT:
		ret = machine__process_exit_event(machine, event); break;
	case PERF_RECORD_LOST:
		ret = machine__process_lost_event(machine, event); break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

static bool symbol__match_parent_regex(struct symbol *sym)
{
	if (sym->name && !regexec(&parent_regex, sym->name, 0, NULL, 0))
		return 1;

	return 0;
}

static const u8 cpumodes[] = {
	PERF_RECORD_MISC_USER,
	PERF_RECORD_MISC_KERNEL,
	PERF_RECORD_MISC_GUEST_USER,
	PERF_RECORD_MISC_GUEST_KERNEL
};
#define NCPUMODES (sizeof(cpumodes)/sizeof(u8))

static void ip__resolve_ams(struct machine *machine, struct thread *thread,
			    struct addr_map_symbol *ams,
			    u64 ip)
{
	struct addr_location al;
	size_t i;
	u8 m;

	memset(&al, 0, sizeof(al));

	for (i = 0; i < NCPUMODES; i++) {
		m = cpumodes[i];
		/*
		 * We cannot use the header.misc hint to determine whether a
		 * branch stack address is user, kernel, guest, hypervisor.
		 * Branches may straddle the kernel/user/hypervisor boundaries.
		 * Thus, we have to try consecutively until we find a match
		 * or else, the symbol is unknown
		 */
		thread__find_addr_location(thread, machine, m, MAP__FUNCTION,
				ip, &al, NULL);
		if (al.sym)
			goto found;
	}
found:
	ams->addr = ip;
	ams->al_addr = al.addr;
	ams->sym = al.sym;
	ams->map = al.map;
}

static void ip__resolve_data(struct machine *machine, struct thread *thread,
			     u8 m, struct addr_map_symbol *ams, u64 addr)
{
	struct addr_location al;

	memset(&al, 0, sizeof(al));

	thread__find_addr_location(thread, machine, m, MAP__VARIABLE, addr, &al,
				   NULL);
	ams->addr = addr;
	ams->al_addr = al.addr;
	ams->sym = al.sym;
	ams->map = al.map;
}

struct mem_info *machine__resolve_mem(struct machine *machine,
				      struct thread *thr,
				      struct perf_sample *sample,
				      u8 cpumode)
{
	struct mem_info *mi = zalloc(sizeof(*mi));

	if (!mi)
		return NULL;

	ip__resolve_ams(machine, thr, &mi->iaddr, sample->ip);
	ip__resolve_data(machine, thr, cpumode, &mi->daddr, sample->addr);
	mi->data_src.val = sample->data_src;

	return mi;
}

struct branch_info *machine__resolve_bstack(struct machine *machine,
					    struct thread *thr,
					    struct branch_stack *bs)
{
	struct branch_info *bi;
	unsigned int i;

	bi = calloc(bs->nr, sizeof(struct branch_info));
	if (!bi)
		return NULL;

	for (i = 0; i < bs->nr; i++) {
		ip__resolve_ams(machine, thr, &bi[i].to, bs->entries[i].to);
		ip__resolve_ams(machine, thr, &bi[i].from, bs->entries[i].from);
		bi[i].flags = bs->entries[i].flags;
	}
	return bi;
}

static int machine__resolve_callchain_sample(struct machine *machine,
					     struct thread *thread,
					     struct ip_callchain *chain,
					     struct symbol **parent)

{
	u8 cpumode = PERF_RECORD_MISC_USER;
	unsigned int i;
	int err;

	callchain_cursor_reset(&callchain_cursor);

	if (chain->nr > PERF_MAX_STACK_DEPTH) {
		pr_warning("corrupted callchain. skipping...\n");
		return 0;
	}

	for (i = 0; i < chain->nr; i++) {
		u64 ip;
		struct addr_location al;

		if (callchain_param.order == ORDER_CALLEE)
			ip = chain->ips[i];
		else
			ip = chain->ips[chain->nr - i - 1];

		if (ip >= PERF_CONTEXT_MAX) {
			switch (ip) {
			case PERF_CONTEXT_HV:
				cpumode = PERF_RECORD_MISC_HYPERVISOR;
				break;
			case PERF_CONTEXT_KERNEL:
				cpumode = PERF_RECORD_MISC_KERNEL;
				break;
			case PERF_CONTEXT_USER:
				cpumode = PERF_RECORD_MISC_USER;
				break;
			default:
				pr_debug("invalid callchain context: "
					 "%"PRId64"\n", (s64) ip);
				/*
				 * It seems the callchain is corrupted.
				 * Discard all.
				 */
				callchain_cursor_reset(&callchain_cursor);
				return 0;
			}
			continue;
		}

		al.filtered = false;
		thread__find_addr_location(thread, machine, cpumode,
					   MAP__FUNCTION, ip, &al, NULL);
		if (al.sym != NULL) {
			if (sort__has_parent && !*parent &&
			    symbol__match_parent_regex(al.sym))
				*parent = al.sym;
			if (!symbol_conf.use_callchain)
				break;
		}

		err = callchain_cursor_append(&callchain_cursor,
					      ip, al.map, al.sym);
		if (err)
			return err;
	}

	return 0;
}

static int unwind_entry(struct unwind_entry *entry, void *arg)
{
	struct callchain_cursor *cursor = arg;
	return callchain_cursor_append(cursor, entry->ip,
				       entry->map, entry->sym);
}

int machine__resolve_callchain(struct machine *machine,
			       struct perf_evsel *evsel,
			       struct thread *thread,
			       struct perf_sample *sample,
			       struct symbol **parent)

{
	int ret;

	callchain_cursor_reset(&callchain_cursor);

	ret = machine__resolve_callchain_sample(machine, thread,
						sample->callchain, parent);
	if (ret)
		return ret;

	/* Can we do dwarf post unwind? */
	if (!((evsel->attr.sample_type & PERF_SAMPLE_REGS_USER) &&
	      (evsel->attr.sample_type & PERF_SAMPLE_STACK_USER)))
		return 0;

	/* Bail out if nothing was captured. */
	if ((!sample->user_regs.regs) ||
	    (!sample->user_stack.size))
		return 0;

	return unwind__get_entries(unwind_entry, &callchain_cursor, machine,
				   thread, evsel->attr.sample_regs_user,
				   sample);

}
