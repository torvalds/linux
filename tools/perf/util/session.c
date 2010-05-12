#define _FILE_OFFSET_BITS 64

#include <linux/kernel.h>

#include <byteswap.h>
#include <unistd.h>
#include <sys/types.h>

#include "session.h"
#include "sort.h"
#include "util.h"

static int perf_session__open(struct perf_session *self, bool force)
{
	struct stat input_stat;

	self->fd = open(self->filename, O_RDONLY);
	if (self->fd < 0) {
		pr_err("failed to open file: %s", self->filename);
		if (!strcmp(self->filename, "perf.data"))
			pr_err("  (try 'perf record' first)");
		pr_err("\n");
		return -errno;
	}

	if (fstat(self->fd, &input_stat) < 0)
		goto out_close;

	if (!force && input_stat.st_uid && (input_stat.st_uid != geteuid())) {
		pr_err("file %s not owned by current user or root\n",
		       self->filename);
		goto out_close;
	}

	if (!input_stat.st_size) {
		pr_info("zero-sized file (%s), nothing to do!\n",
			self->filename);
		goto out_close;
	}

	if (perf_header__read(&self->header, self->fd) < 0) {
		pr_err("incompatible file format");
		goto out_close;
	}

	self->size = input_stat.st_size;
	return 0;

out_close:
	close(self->fd);
	self->fd = -1;
	return -1;
}

static inline int perf_session__create_kernel_maps(struct perf_session *self)
{
	return map_groups__create_kernel_maps(&self->kmaps, self->vmlinux_maps);
}

struct perf_session *perf_session__new(const char *filename, int mode, bool force)
{
	size_t len = filename ? strlen(filename) + 1 : 0;
	struct perf_session *self = zalloc(sizeof(*self) + len);

	if (self == NULL)
		goto out;

	if (perf_header__init(&self->header) < 0)
		goto out_free;

	memcpy(self->filename, filename, len);
	self->threads = RB_ROOT;
	self->stats_by_id = RB_ROOT;
	self->last_match = NULL;
	self->mmap_window = 32;
	self->cwd = NULL;
	self->cwdlen = 0;
	self->unknown_events = 0;
	map_groups__init(&self->kmaps);

	if (mode == O_RDONLY) {
		if (perf_session__open(self, force) < 0)
			goto out_delete;
	} else if (mode == O_WRONLY) {
		/*
		 * In O_RDONLY mode this will be performed when reading the
		 * kernel MMAP event, in event__process_mmap().
		 */
		if (perf_session__create_kernel_maps(self) < 0)
			goto out_delete;
	}

	self->sample_type = perf_header__sample_type(&self->header);
out:
	return self;
out_free:
	free(self);
	return NULL;
out_delete:
	perf_session__delete(self);
	return NULL;
}

void perf_session__delete(struct perf_session *self)
{
	perf_header__exit(&self->header);
	close(self->fd);
	free(self->cwd);
	free(self);
}

static bool symbol__match_parent_regex(struct symbol *sym)
{
	if (sym->name && !regexec(&parent_regex, sym->name, 0, NULL, 0))
		return 1;

	return 0;
}

struct symbol **perf_session__resolve_callchain(struct perf_session *self,
						struct thread *thread,
						struct ip_callchain *chain,
						struct symbol **parent)
{
	u8 cpumode = PERF_RECORD_MISC_USER;
	struct symbol **syms = NULL;
	unsigned int i;

	if (symbol_conf.use_callchain) {
		syms = calloc(chain->nr, sizeof(*syms));
		if (!syms) {
			fprintf(stderr, "Can't allocate memory for symbols\n");
			exit(-1);
		}
	}

	for (i = 0; i < chain->nr; i++) {
		u64 ip = chain->ips[i];
		struct addr_location al;

		if (ip >= PERF_CONTEXT_MAX) {
			switch (ip) {
			case PERF_CONTEXT_HV:
				cpumode = PERF_RECORD_MISC_HYPERVISOR;	break;
			case PERF_CONTEXT_KERNEL:
				cpumode = PERF_RECORD_MISC_KERNEL;	break;
			case PERF_CONTEXT_USER:
				cpumode = PERF_RECORD_MISC_USER;	break;
			default:
				break;
			}
			continue;
		}

		thread__find_addr_location(thread, self, cpumode,
					   MAP__FUNCTION, ip, &al, NULL);
		if (al.sym != NULL) {
			if (sort__has_parent && !*parent &&
			    symbol__match_parent_regex(al.sym))
				*parent = al.sym;
			if (!symbol_conf.use_callchain)
				break;
			syms[i] = al.sym;
		}
	}

	return syms;
}

static int process_event_stub(event_t *event __used,
			      struct perf_session *session __used)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static void perf_event_ops__fill_defaults(struct perf_event_ops *handler)
{
	if (handler->sample == NULL)
		handler->sample = process_event_stub;
	if (handler->mmap == NULL)
		handler->mmap = process_event_stub;
	if (handler->comm == NULL)
		handler->comm = process_event_stub;
	if (handler->fork == NULL)
		handler->fork = process_event_stub;
	if (handler->exit == NULL)
		handler->exit = process_event_stub;
	if (handler->lost == NULL)
		handler->lost = process_event_stub;
	if (handler->read == NULL)
		handler->read = process_event_stub;
	if (handler->throttle == NULL)
		handler->throttle = process_event_stub;
	if (handler->unthrottle == NULL)
		handler->unthrottle = process_event_stub;
}

static const char *event__name[] = {
	[0]			 = "TOTAL",
	[PERF_RECORD_MMAP]	 = "MMAP",
	[PERF_RECORD_LOST]	 = "LOST",
	[PERF_RECORD_COMM]	 = "COMM",
	[PERF_RECORD_EXIT]	 = "EXIT",
	[PERF_RECORD_THROTTLE]	 = "THROTTLE",
	[PERF_RECORD_UNTHROTTLE] = "UNTHROTTLE",
	[PERF_RECORD_FORK]	 = "FORK",
	[PERF_RECORD_READ]	 = "READ",
	[PERF_RECORD_SAMPLE]	 = "SAMPLE",
};

unsigned long event__total[PERF_RECORD_MAX];

void event__print_totals(void)
{
	int i;
	for (i = 0; i < PERF_RECORD_MAX; ++i)
		pr_info("%10s events: %10ld\n",
			event__name[i], event__total[i]);
}

void mem_bswap_64(void *src, int byte_size)
{
	u64 *m = src;

	while (byte_size > 0) {
		*m = bswap_64(*m);
		byte_size -= sizeof(u64);
		++m;
	}
}

static void event__all64_swap(event_t *self)
{
	struct perf_event_header *hdr = &self->header;
	mem_bswap_64(hdr + 1, self->header.size - sizeof(*hdr));
}

static void event__comm_swap(event_t *self)
{
	self->comm.pid = bswap_32(self->comm.pid);
	self->comm.tid = bswap_32(self->comm.tid);
}

static void event__mmap_swap(event_t *self)
{
	self->mmap.pid	 = bswap_32(self->mmap.pid);
	self->mmap.tid	 = bswap_32(self->mmap.tid);
	self->mmap.start = bswap_64(self->mmap.start);
	self->mmap.len	 = bswap_64(self->mmap.len);
	self->mmap.pgoff = bswap_64(self->mmap.pgoff);
}

static void event__task_swap(event_t *self)
{
	self->fork.pid	= bswap_32(self->fork.pid);
	self->fork.tid	= bswap_32(self->fork.tid);
	self->fork.ppid	= bswap_32(self->fork.ppid);
	self->fork.ptid	= bswap_32(self->fork.ptid);
	self->fork.time	= bswap_64(self->fork.time);
}

static void event__read_swap(event_t *self)
{
	self->read.pid		= bswap_32(self->read.pid);
	self->read.tid		= bswap_32(self->read.tid);
	self->read.value	= bswap_64(self->read.value);
	self->read.time_enabled	= bswap_64(self->read.time_enabled);
	self->read.time_running	= bswap_64(self->read.time_running);
	self->read.id		= bswap_64(self->read.id);
}

typedef void (*event__swap_op)(event_t *self);

static event__swap_op event__swap_ops[] = {
	[PERF_RECORD_MMAP]   = event__mmap_swap,
	[PERF_RECORD_COMM]   = event__comm_swap,
	[PERF_RECORD_FORK]   = event__task_swap,
	[PERF_RECORD_EXIT]   = event__task_swap,
	[PERF_RECORD_LOST]   = event__all64_swap,
	[PERF_RECORD_READ]   = event__read_swap,
	[PERF_RECORD_SAMPLE] = event__all64_swap,
	[PERF_RECORD_MAX]    = NULL,
};

static int perf_session__process_event(struct perf_session *self,
				       event_t *event,
				       struct perf_event_ops *ops,
				       u64 offset, u64 head)
{
	trace_event(event);

	if (event->header.type < PERF_RECORD_MAX) {
		dump_printf("%#Lx [%#x]: PERF_RECORD_%s",
			    offset + head, event->header.size,
			    event__name[event->header.type]);
		++event__total[0];
		++event__total[event->header.type];
	}

	if (self->header.needs_swap && event__swap_ops[event->header.type])
		event__swap_ops[event->header.type](event);

	switch (event->header.type) {
	case PERF_RECORD_SAMPLE:
		return ops->sample(event, self);
	case PERF_RECORD_MMAP:
		return ops->mmap(event, self);
	case PERF_RECORD_COMM:
		return ops->comm(event, self);
	case PERF_RECORD_FORK:
		return ops->fork(event, self);
	case PERF_RECORD_EXIT:
		return ops->exit(event, self);
	case PERF_RECORD_LOST:
		return ops->lost(event, self);
	case PERF_RECORD_READ:
		return ops->read(event, self);
	case PERF_RECORD_THROTTLE:
		return ops->throttle(event, self);
	case PERF_RECORD_UNTHROTTLE:
		return ops->unthrottle(event, self);
	default:
		self->unknown_events++;
		return -1;
	}
}

void perf_event_header__bswap(struct perf_event_header *self)
{
	self->type = bswap_32(self->type);
	self->misc = bswap_16(self->misc);
	self->size = bswap_16(self->size);
}

int perf_header__read_build_ids(struct perf_header *self,
				int input, u64 offset, u64 size)
{
	struct build_id_event bev;
	char filename[PATH_MAX];
	u64 limit = offset + size;
	int err = -1;

	while (offset < limit) {
		struct dso *dso;
		ssize_t len;
		struct list_head *head = &dsos__user;

		if (read(input, &bev, sizeof(bev)) != sizeof(bev))
			goto out;

		if (self->needs_swap)
			perf_event_header__bswap(&bev.header);

		len = bev.header.size - sizeof(bev);
		if (read(input, filename, len) != len)
			goto out;

		if (bev.header.misc & PERF_RECORD_MISC_KERNEL)
			head = &dsos__kernel;

		dso = __dsos__findnew(head, filename);
		if (dso != NULL) {
			dso__set_build_id(dso, &bev.build_id);
			if (head == &dsos__kernel && filename[0] == '[')
				dso->kernel = 1;
		}

		offset += bev.header.size;
	}
	err = 0;
out:
	return err;
}

static struct thread *perf_session__register_idle_thread(struct perf_session *self)
{
	struct thread *thread = perf_session__findnew(self, 0);

	if (thread == NULL || thread__set_comm(thread, "swapper")) {
		pr_err("problem inserting idle task.\n");
		thread = NULL;
	}

	return thread;
}

int __perf_session__process_events(struct perf_session *self,
				   u64 data_offset, u64 data_size,
				   u64 file_size, struct perf_event_ops *ops)
{
	int err, mmap_prot, mmap_flags;
	u64 head, shift;
	u64 offset = 0;
	size_t	page_size;
	event_t *event;
	uint32_t size;
	char *buf;

	perf_event_ops__fill_defaults(ops);

	page_size = sysconf(_SC_PAGESIZE);

	head = data_offset;
	shift = page_size * (head / page_size);
	offset += shift;
	head -= shift;

	mmap_prot  = PROT_READ;
	mmap_flags = MAP_SHARED;

	if (self->header.needs_swap) {
		mmap_prot  |= PROT_WRITE;
		mmap_flags = MAP_PRIVATE;
	}
remap:
	buf = mmap(NULL, page_size * self->mmap_window, mmap_prot,
		   mmap_flags, self->fd, offset);
	if (buf == MAP_FAILED) {
		pr_err("failed to mmap file\n");
		err = -errno;
		goto out_err;
	}

more:
	event = (event_t *)(buf + head);

	if (self->header.needs_swap)
		perf_event_header__bswap(&event->header);
	size = event->header.size;
	if (size == 0)
		size = 8;

	if (head + event->header.size >= page_size * self->mmap_window) {
		int munmap_ret;

		shift = page_size * (head / page_size);

		munmap_ret = munmap(buf, page_size * self->mmap_window);
		assert(munmap_ret == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}

	size = event->header.size;

	dump_printf("\n%#Lx [%#x]: event: %d\n",
		    offset + head, event->header.size, event->header.type);

	if (size == 0 ||
	    perf_session__process_event(self, event, ops, offset, head) < 0) {
		dump_printf("%#Lx [%#x]: skipping unknown header type: %d\n",
			    offset + head, event->header.size,
			    event->header.type);
		/*
		 * assume we lost track of the stream, check alignment, and
		 * increment a single u64 in the hope to catch on again 'soon'.
		 */
		if (unlikely(head & 7))
			head &= ~7ULL;

		size = 8;
	}

	head += size;

	if (offset + head >= data_offset + data_size)
		goto done;

	if (offset + head < file_size)
		goto more;
done:
	err = 0;
out_err:
	return err;
}

int perf_session__process_events(struct perf_session *self,
				 struct perf_event_ops *ops)
{
	int err;

	if (perf_session__register_idle_thread(self) == NULL)
		return -ENOMEM;

	if (!symbol_conf.full_paths) {
		char bf[PATH_MAX];

		if (getcwd(bf, sizeof(bf)) == NULL) {
			err = -errno;
out_getcwd_err:
			pr_err("failed to get the current directory\n");
			goto out_err;
		}
		self->cwd = strdup(bf);
		if (self->cwd == NULL) {
			err = -ENOMEM;
			goto out_getcwd_err;
		}
		self->cwdlen = strlen(self->cwd);
	}

	err = __perf_session__process_events(self, self->header.data_offset,
					     self->header.data_size,
					     self->size, ops);
out_err:
	return err;
}

bool perf_session__has_traces(struct perf_session *self, const char *msg)
{
	if (!(self->sample_type & PERF_SAMPLE_RAW)) {
		pr_err("No trace sample to read. Did you call 'perf %s'?\n", msg);
		return false;
	}

	return true;
}

int perf_session__set_kallsyms_ref_reloc_sym(struct perf_session *self,
					     const char *symbol_name,
					     u64 addr)
{
	char *bracket;
	enum map_type i;

	self->ref_reloc_sym.name = strdup(symbol_name);
	if (self->ref_reloc_sym.name == NULL)
		return -ENOMEM;

	bracket = strchr(self->ref_reloc_sym.name, ']');
	if (bracket)
		*bracket = '\0';

	self->ref_reloc_sym.addr = addr;

	for (i = 0; i < MAP__NR_TYPES; ++i) {
		struct kmap *kmap = map__kmap(self->vmlinux_maps[i]);
		kmap->ref_reloc_sym = &self->ref_reloc_sym;
	}

	return 0;
}

static u64 map__reloc_map_ip(struct map *map, u64 ip)
{
	return ip + (s64)map->pgoff;
}

static u64 map__reloc_unmap_ip(struct map *map, u64 ip)
{
	return ip - (s64)map->pgoff;
}

void map__reloc_vmlinux(struct map *self)
{
	struct kmap *kmap = map__kmap(self);
	s64 reloc;

	if (!kmap->ref_reloc_sym || !kmap->ref_reloc_sym->unrelocated_addr)
		return;

	reloc = (kmap->ref_reloc_sym->unrelocated_addr -
		 kmap->ref_reloc_sym->addr);

	if (!reloc)
		return;

	self->map_ip   = map__reloc_map_ip;
	self->unmap_ip = map__reloc_unmap_ip;
	self->pgoff    = reloc;
}
