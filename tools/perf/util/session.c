#include <linux/kernel.h>

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
	self->last_match = NULL;
	self->mmap_window = 32;
	self->cwd = NULL;
	self->cwdlen = 0;
	map_groups__init(&self->kmaps);

	if (perf_session__create_kernel_maps(self) < 0)
		goto out_delete;

	if (mode == O_RDONLY && perf_session__open(self, force) < 0)
		goto out_delete;
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
	if (handler->process_sample_event == NULL)
		handler->process_sample_event = process_event_stub;
	if (handler->process_mmap_event == NULL)
		handler->process_mmap_event = process_event_stub;
	if (handler->process_comm_event == NULL)
		handler->process_comm_event = process_event_stub;
	if (handler->process_fork_event == NULL)
		handler->process_fork_event = process_event_stub;
	if (handler->process_exit_event == NULL)
		handler->process_exit_event = process_event_stub;
	if (handler->process_lost_event == NULL)
		handler->process_lost_event = process_event_stub;
	if (handler->process_read_event == NULL)
		handler->process_read_event = process_event_stub;
	if (handler->process_throttle_event == NULL)
		handler->process_throttle_event = process_event_stub;
	if (handler->process_unthrottle_event == NULL)
		handler->process_unthrottle_event = process_event_stub;
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

static int perf_session__process_event(struct perf_session *self,
				       event_t *event,
				       struct perf_event_ops *ops,
				       unsigned long offset, unsigned long head)
{
	trace_event(event);

	if (event->header.type < PERF_RECORD_MAX) {
		dump_printf("%p [%p]: PERF_RECORD_%s",
			    (void *)(offset + head),
			    (void *)(long)(event->header.size),
			    event__name[event->header.type]);
		++event__total[0];
		++event__total[event->header.type];
	}

	switch (event->header.type) {
	case PERF_RECORD_SAMPLE:
		return ops->process_sample_event(event, self);
	case PERF_RECORD_MMAP:
		return ops->process_mmap_event(event, self);
	case PERF_RECORD_COMM:
		return ops->process_comm_event(event, self);
	case PERF_RECORD_FORK:
		return ops->process_fork_event(event, self);
	case PERF_RECORD_EXIT:
		return ops->process_exit_event(event, self);
	case PERF_RECORD_LOST:
		return ops->process_lost_event(event, self);
	case PERF_RECORD_READ:
		return ops->process_read_event(event, self);
	case PERF_RECORD_THROTTLE:
		return ops->process_throttle_event(event, self);
	case PERF_RECORD_UNTHROTTLE:
		return ops->process_unthrottle_event(event, self);
	default:
		ops->total_unknown++;
		return -1;
	}
}

int perf_header__read_build_ids(int input, u64 offset, u64 size)
{
	struct build_id_event bev;
	char filename[PATH_MAX];
	u64 limit = offset + size;
	int err = -1;

	while (offset < limit) {
		struct dso *dso;
		ssize_t len;

		if (read(input, &bev, sizeof(bev)) != sizeof(bev))
			goto out;

		len = bev.header.size - sizeof(bev);
		if (read(input, filename, len) != len)
			goto out;

		dso = dsos__findnew(filename);
		if (dso != NULL)
			dso__set_build_id(dso, &bev.build_id);

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

int perf_session__process_events(struct perf_session *self,
				 struct perf_event_ops *ops)
{
	int err;
	unsigned long head, shift;
	unsigned long offset = 0;
	size_t	page_size;
	event_t *event;
	uint32_t size;
	char *buf;

	if (perf_session__register_idle_thread(self) == NULL)
		return -ENOMEM;

	perf_event_ops__fill_defaults(ops);

	page_size = getpagesize();

	head = self->header.data_offset;
	self->sample_type = perf_header__sample_type(&self->header);

	err = -EINVAL;
	if (ops->sample_type_check && ops->sample_type_check(self) < 0)
		goto out_err;

	if (!ops->full_paths) {
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

	shift = page_size * (head / page_size);
	offset += shift;
	head -= shift;

remap:
	buf = mmap(NULL, page_size * self->mmap_window, PROT_READ,
		   MAP_SHARED, self->fd, offset);
	if (buf == MAP_FAILED) {
		pr_err("failed to mmap file\n");
		err = -errno;
		goto out_err;
	}

more:
	event = (event_t *)(buf + head);

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

	dump_printf("\n%p [%p]: event: %d\n",
			(void *)(offset + head),
			(void *)(long)event->header.size,
			event->header.type);

	if (size == 0 ||
	    perf_session__process_event(self, event, ops, offset, head) < 0) {
		dump_printf("%p [%p]: skipping unknown header type: %d\n",
			    (void *)(offset + head),
			    (void *)(long)(event->header.size),
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

	if (offset + head >= self->header.data_offset + self->header.data_size)
		goto done;

	if (offset + head < self->size)
		goto more;
done:
	err = 0;
out_err:
	return err;
}
