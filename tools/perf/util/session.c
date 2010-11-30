#define _FILE_OFFSET_BITS 64

#include <linux/kernel.h>

#include <byteswap.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "session.h"
#include "sort.h"
#include "util.h"

static int perf_session__open(struct perf_session *self, bool force)
{
	struct stat input_stat;

	if (!strcmp(self->filename, "-")) {
		self->fd_pipe = true;
		self->fd = STDIN_FILENO;

		if (perf_header__read(self, self->fd) < 0)
			pr_err("incompatible file format");

		return 0;
	}

	self->fd = open(self->filename, O_RDONLY);
	if (self->fd < 0) {
		int err = errno;

		pr_err("failed to open %s: %s", self->filename, strerror(err));
		if (err == ENOENT && !strcmp(self->filename, "perf.data"))
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

	if (perf_header__read(self, self->fd) < 0) {
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

void perf_session__update_sample_type(struct perf_session *self)
{
	self->sample_type = perf_header__sample_type(&self->header);
}

int perf_session__create_kernel_maps(struct perf_session *self)
{
	int ret = machine__create_kernel_maps(&self->host_machine);

	if (ret >= 0)
		ret = machines__create_guest_kernel_maps(&self->machines);
	return ret;
}

static void perf_session__destroy_kernel_maps(struct perf_session *self)
{
	machine__destroy_kernel_maps(&self->host_machine);
	machines__destroy_guest_kernel_maps(&self->machines);
}

struct perf_session *perf_session__new(const char *filename, int mode, bool force, bool repipe)
{
	size_t len = filename ? strlen(filename) + 1 : 0;
	struct perf_session *self = zalloc(sizeof(*self) + len);

	if (self == NULL)
		goto out;

	if (perf_header__init(&self->header) < 0)
		goto out_free;

	memcpy(self->filename, filename, len);
	self->threads = RB_ROOT;
	INIT_LIST_HEAD(&self->dead_threads);
	self->hists_tree = RB_ROOT;
	self->last_match = NULL;
	/*
	 * On 64bit we can mmap the data file in one go. No need for tiny mmap
	 * slices. On 32bit we use 32MB.
	 */
#if BITS_PER_LONG == 64
	self->mmap_window = ULLONG_MAX;
#else
	self->mmap_window = 32 * 1024 * 1024ULL;
#endif
	self->machines = RB_ROOT;
	self->repipe = repipe;
	INIT_LIST_HEAD(&self->ordered_samples.samples);
	machine__init(&self->host_machine, "", HOST_KERNEL_ID);

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

	perf_session__update_sample_type(self);
out:
	return self;
out_free:
	free(self);
	return NULL;
out_delete:
	perf_session__delete(self);
	return NULL;
}

static void perf_session__delete_dead_threads(struct perf_session *self)
{
	struct thread *n, *t;

	list_for_each_entry_safe(t, n, &self->dead_threads, node) {
		list_del(&t->node);
		thread__delete(t);
	}
}

static void perf_session__delete_threads(struct perf_session *self)
{
	struct rb_node *nd = rb_first(&self->threads);

	while (nd) {
		struct thread *t = rb_entry(nd, struct thread, rb_node);

		rb_erase(&t->rb_node, &self->threads);
		nd = rb_next(nd);
		thread__delete(t);
	}
}

void perf_session__delete(struct perf_session *self)
{
	perf_header__exit(&self->header);
	perf_session__destroy_kernel_maps(self);
	perf_session__delete_dead_threads(self);
	perf_session__delete_threads(self);
	machine__exit(&self->host_machine);
	close(self->fd);
	free(self);
}

void perf_session__remove_thread(struct perf_session *self, struct thread *th)
{
	self->last_match = NULL;
	rb_erase(&th->rb_node, &self->threads);
	/*
	 * We may have references to this thread, for instance in some hist_entry
	 * instances, so just move them to a separate list.
	 */
	list_add_tail(&th->node, &self->dead_threads);
}

static bool symbol__match_parent_regex(struct symbol *sym)
{
	if (sym->name && !regexec(&parent_regex, sym->name, 0, NULL, 0))
		return 1;

	return 0;
}

struct map_symbol *perf_session__resolve_callchain(struct perf_session *self,
						   struct thread *thread,
						   struct ip_callchain *chain,
						   struct symbol **parent)
{
	u8 cpumode = PERF_RECORD_MISC_USER;
	unsigned int i;
	struct map_symbol *syms = calloc(chain->nr, sizeof(*syms));

	if (!syms)
		return NULL;

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

		al.filtered = false;
		thread__find_addr_location(thread, self, cpumode,
				MAP__FUNCTION, thread->pid, ip, &al, NULL);
		if (al.sym != NULL) {
			if (sort__has_parent && !*parent &&
			    symbol__match_parent_regex(al.sym))
				*parent = al.sym;
			if (!symbol_conf.use_callchain)
				break;
			syms[i].map = al.map;
			syms[i].sym = al.sym;
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

static int process_finished_round_stub(event_t *event __used,
				       struct perf_session *session __used,
				       struct perf_event_ops *ops __used)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_finished_round(event_t *event,
				  struct perf_session *session,
				  struct perf_event_ops *ops);

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
		handler->lost = event__process_lost;
	if (handler->read == NULL)
		handler->read = process_event_stub;
	if (handler->throttle == NULL)
		handler->throttle = process_event_stub;
	if (handler->unthrottle == NULL)
		handler->unthrottle = process_event_stub;
	if (handler->attr == NULL)
		handler->attr = process_event_stub;
	if (handler->event_type == NULL)
		handler->event_type = process_event_stub;
	if (handler->tracing_data == NULL)
		handler->tracing_data = process_event_stub;
	if (handler->build_id == NULL)
		handler->build_id = process_event_stub;
	if (handler->finished_round == NULL) {
		if (handler->ordered_samples)
			handler->finished_round = process_finished_round;
		else
			handler->finished_round = process_finished_round_stub;
	}
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

static void event__attr_swap(event_t *self)
{
	size_t size;

	self->attr.attr.type		= bswap_32(self->attr.attr.type);
	self->attr.attr.size		= bswap_32(self->attr.attr.size);
	self->attr.attr.config		= bswap_64(self->attr.attr.config);
	self->attr.attr.sample_period	= bswap_64(self->attr.attr.sample_period);
	self->attr.attr.sample_type	= bswap_64(self->attr.attr.sample_type);
	self->attr.attr.read_format	= bswap_64(self->attr.attr.read_format);
	self->attr.attr.wakeup_events	= bswap_32(self->attr.attr.wakeup_events);
	self->attr.attr.bp_type		= bswap_32(self->attr.attr.bp_type);
	self->attr.attr.bp_addr		= bswap_64(self->attr.attr.bp_addr);
	self->attr.attr.bp_len		= bswap_64(self->attr.attr.bp_len);

	size = self->header.size;
	size -= (void *)&self->attr.id - (void *)self;
	mem_bswap_64(self->attr.id, size);
}

static void event__event_type_swap(event_t *self)
{
	self->event_type.event_type.event_id =
		bswap_64(self->event_type.event_type.event_id);
}

static void event__tracing_data_swap(event_t *self)
{
	self->tracing_data.size = bswap_32(self->tracing_data.size);
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
	[PERF_RECORD_HEADER_ATTR]   = event__attr_swap,
	[PERF_RECORD_HEADER_EVENT_TYPE]   = event__event_type_swap,
	[PERF_RECORD_HEADER_TRACING_DATA]   = event__tracing_data_swap,
	[PERF_RECORD_HEADER_BUILD_ID]   = NULL,
	[PERF_RECORD_HEADER_MAX]    = NULL,
};

struct sample_queue {
	u64			timestamp;
	event_t			*event;
	struct list_head	list;
};

static void flush_sample_queue(struct perf_session *s,
			       struct perf_event_ops *ops)
{
	struct ordered_samples *os = &s->ordered_samples;
	struct list_head *head = &os->samples;
	struct sample_queue *tmp, *iter;
	u64 limit = os->next_flush;
	u64 last_ts = os->last_sample ? os->last_sample->timestamp : 0ULL;

	if (!ops->ordered_samples || !limit)
		return;

	list_for_each_entry_safe(iter, tmp, head, list) {
		if (iter->timestamp > limit)
			break;

		ops->sample(iter->event, s);

		os->last_flush = iter->timestamp;
		list_del(&iter->list);
		free(iter->event);
		free(iter);
	}

	if (list_empty(head)) {
		os->last_sample = NULL;
	} else if (last_ts <= limit) {
		os->last_sample =
			list_entry(head->prev, struct sample_queue, list);
	}
}

/*
 * When perf record finishes a pass on every buffers, it records this pseudo
 * event.
 * We record the max timestamp t found in the pass n.
 * Assuming these timestamps are monotonic across cpus, we know that if
 * a buffer still has events with timestamps below t, they will be all
 * available and then read in the pass n + 1.
 * Hence when we start to read the pass n + 2, we can safely flush every
 * events with timestamps below t.
 *
 *    ============ PASS n =================
 *       CPU 0         |   CPU 1
 *                     |
 *    cnt1 timestamps  |   cnt2 timestamps
 *          1          |         2
 *          2          |         3
 *          -          |         4  <--- max recorded
 *
 *    ============ PASS n + 1 ==============
 *       CPU 0         |   CPU 1
 *                     |
 *    cnt1 timestamps  |   cnt2 timestamps
 *          3          |         5
 *          4          |         6
 *          5          |         7 <---- max recorded
 *
 *      Flush every events below timestamp 4
 *
 *    ============ PASS n + 2 ==============
 *       CPU 0         |   CPU 1
 *                     |
 *    cnt1 timestamps  |   cnt2 timestamps
 *          6          |         8
 *          7          |         9
 *          -          |         10
 *
 *      Flush every events below timestamp 7
 *      etc...
 */
static int process_finished_round(event_t *event __used,
				  struct perf_session *session,
				  struct perf_event_ops *ops)
{
	flush_sample_queue(session, ops);
	session->ordered_samples.next_flush = session->ordered_samples.max_timestamp;

	return 0;
}

/* The queue is ordered by time */
static void __queue_sample_event(struct sample_queue *new,
				 struct perf_session *s)
{
	struct ordered_samples *os = &s->ordered_samples;
	struct sample_queue *sample = os->last_sample;
	u64 timestamp = new->timestamp;
	struct list_head *p;

	os->last_sample = new;

	if (!sample) {
		list_add(&new->list, &os->samples);
		os->max_timestamp = timestamp;
		return;
	}

	/*
	 * last_sample might point to some random place in the list as it's
	 * the last queued event. We expect that the new event is close to
	 * this.
	 */
	if (sample->timestamp <= timestamp) {
		while (sample->timestamp <= timestamp) {
			p = sample->list.next;
			if (p == &os->samples) {
				list_add_tail(&new->list, &os->samples);
				os->max_timestamp = timestamp;
				return;
			}
			sample = list_entry(p, struct sample_queue, list);
		}
		list_add_tail(&new->list, &sample->list);
	} else {
		while (sample->timestamp > timestamp) {
			p = sample->list.prev;
			if (p == &os->samples) {
				list_add(&new->list, &os->samples);
				return;
			}
			sample = list_entry(p, struct sample_queue, list);
		}
		list_add(&new->list, &sample->list);
	}
}

static int queue_sample_event(event_t *event, struct sample_data *data,
			      struct perf_session *s)
{
	u64 timestamp = data->time;
	struct sample_queue *new;


	if (timestamp < s->ordered_samples.last_flush) {
		printf("Warning: Timestamp below last timeslice flush\n");
		return -EINVAL;
	}

	new = malloc(sizeof(*new));
	if (!new)
		return -ENOMEM;

	new->timestamp = timestamp;

	new->event = malloc(event->header.size);
	if (!new->event) {
		free(new);
		return -ENOMEM;
	}

	memcpy(new->event, event, event->header.size);

	__queue_sample_event(new, s);

	return 0;
}

static int perf_session__process_sample(event_t *event, struct perf_session *s,
					struct perf_event_ops *ops)
{
	struct sample_data data;

	if (!ops->ordered_samples)
		return ops->sample(event, s);

	bzero(&data, sizeof(struct sample_data));
	event__parse_sample(event, s->sample_type, &data);

	queue_sample_event(event, &data, s);

	return 0;
}

static int perf_session__process_event(struct perf_session *self,
				       event_t *event,
				       struct perf_event_ops *ops,
				       u64 file_offset)
{
	trace_event(event);

	if (event->header.type < PERF_RECORD_HEADER_MAX) {
		dump_printf("%#Lx [%#x]: PERF_RECORD_%s",
			    file_offset, event->header.size,
			    event__name[event->header.type]);
		hists__inc_nr_events(&self->hists, event->header.type);
	}

	if (self->header.needs_swap && event__swap_ops[event->header.type])
		event__swap_ops[event->header.type](event);

	switch (event->header.type) {
	case PERF_RECORD_SAMPLE:
		return perf_session__process_sample(event, self, ops);
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
	case PERF_RECORD_HEADER_ATTR:
		return ops->attr(event, self);
	case PERF_RECORD_HEADER_EVENT_TYPE:
		return ops->event_type(event, self);
	case PERF_RECORD_HEADER_TRACING_DATA:
		/* setup for reading amidst mmap */
		lseek(self->fd, file_offset, SEEK_SET);
		return ops->tracing_data(event, self);
	case PERF_RECORD_HEADER_BUILD_ID:
		return ops->build_id(event, self);
	case PERF_RECORD_FINISHED_ROUND:
		return ops->finished_round(event, self, ops);
	default:
		++self->hists.stats.nr_unknown_events;
		return -1;
	}
}

void perf_event_header__bswap(struct perf_event_header *self)
{
	self->type = bswap_32(self->type);
	self->misc = bswap_16(self->misc);
	self->size = bswap_16(self->size);
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

int do_read(int fd, void *buf, size_t size)
{
	void *buf_start = buf;

	while (size) {
		int ret = read(fd, buf, size);

		if (ret <= 0)
			return ret;

		size -= ret;
		buf += ret;
	}

	return buf - buf_start;
}

#define session_done()	(*(volatile int *)(&session_done))
volatile int session_done;

static int __perf_session__process_pipe_events(struct perf_session *self,
					       struct perf_event_ops *ops)
{
	event_t event;
	uint32_t size;
	int skip = 0;
	u64 head;
	int err;
	void *p;

	perf_event_ops__fill_defaults(ops);

	head = 0;
more:
	err = do_read(self->fd, &event, sizeof(struct perf_event_header));
	if (err <= 0) {
		if (err == 0)
			goto done;

		pr_err("failed to read event header\n");
		goto out_err;
	}

	if (self->header.needs_swap)
		perf_event_header__bswap(&event.header);

	size = event.header.size;
	if (size == 0)
		size = 8;

	p = &event;
	p += sizeof(struct perf_event_header);

	if (size - sizeof(struct perf_event_header)) {
		err = do_read(self->fd, p,
			      size - sizeof(struct perf_event_header));
		if (err <= 0) {
			if (err == 0) {
				pr_err("unexpected end of event stream\n");
				goto done;
			}

			pr_err("failed to read event data\n");
			goto out_err;
		}
	}

	if (size == 0 ||
	    (skip = perf_session__process_event(self, &event, ops, head)) < 0) {
		dump_printf("%#Lx [%#x]: skipping unknown header type: %d\n",
			    head, event.header.size, event.header.type);
		/*
		 * assume we lost track of the stream, check alignment, and
		 * increment a single u64 in the hope to catch on again 'soon'.
		 */
		if (unlikely(head & 7))
			head &= ~7ULL;

		size = 8;
	}

	head += size;

	dump_printf("\n%#Lx [%#x]: event: %d\n",
		    head, event.header.size, event.header.type);

	if (skip > 0)
		head += skip;

	if (!session_done())
		goto more;
done:
	err = 0;
out_err:
	return err;
}

int __perf_session__process_events(struct perf_session *session,
				   u64 data_offset, u64 data_size,
				   u64 file_size, struct perf_event_ops *ops)
{
	u64 head, page_offset, file_offset, file_pos, progress_next;
	int err, mmap_prot, mmap_flags;
	struct ui_progress *progress;
	size_t	page_size, mmap_size;
	event_t *event;
	uint32_t size;
	char *buf;

	perf_event_ops__fill_defaults(ops);

	page_size = sysconf(_SC_PAGESIZE);

	page_offset = page_size * (data_offset / page_size);
	file_offset = page_offset;
	head = data_offset - page_offset;

	if (data_offset + data_size < file_size)
		file_size = data_offset + data_size;

	progress_next = file_size / 16;
	progress = ui_progress__new("Processing events...", file_size);
	if (progress == NULL)
		return -1;

	mmap_size = session->mmap_window;
	if (mmap_size > file_size)
		mmap_size = file_size;

	mmap_prot  = PROT_READ;
	mmap_flags = MAP_SHARED;

	if (session->header.needs_swap) {
		mmap_prot  |= PROT_WRITE;
		mmap_flags = MAP_PRIVATE;
	}
remap:
	buf = mmap(NULL, mmap_size, mmap_prot, mmap_flags, session->fd,
		   file_offset);
	if (buf == MAP_FAILED) {
		pr_err("failed to mmap file\n");
		err = -errno;
		goto out_err;
	}
	file_pos = file_offset + head;

more:
	event = (event_t *)(buf + head);

	if (session->header.needs_swap)
		perf_event_header__bswap(&event->header);
	size = event->header.size;
	if (size == 0)
		size = 8;

	if (head + event->header.size >= mmap_size) {
		int munmap_ret;

		munmap_ret = munmap(buf, mmap_size);
		assert(munmap_ret == 0);

		page_offset = page_size * (head / page_size);
		file_offset += page_offset;
		head -= page_offset;
		goto remap;
	}

	size = event->header.size;

	dump_printf("\n%#Lx [%#x]: event: %d\n",
		    file_pos, event->header.size, event->header.type);

	if (size == 0 ||
	    perf_session__process_event(session, event, ops, file_pos) < 0) {
		dump_printf("%#Lx [%#x]: skipping unknown header type: %d\n",
			    file_offset + head, event->header.size,
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
	file_pos += size;

	if (file_pos >= progress_next) {
		progress_next += file_size / 16;
		ui_progress__update(progress, file_pos);
	}

	if (file_pos < file_size)
		goto more;

	err = 0;
	/* do the final flush for ordered samples */
	session->ordered_samples.next_flush = ULLONG_MAX;
	flush_sample_queue(session, ops);
out_err:
	ui_progress__delete(progress);

	if (ops->lost == event__process_lost &&
	    session->hists.stats.total_lost != 0) {
		ui__warning("Processed %Lu events and LOST %Lu!\n\n"
			    "Check IO/CPU overload!\n\n",
			    session->hists.stats.total_period,
			    session->hists.stats.total_lost);
	}

	if (session->hists.stats.nr_unknown_events != 0) {
		ui__warning("Found %u unknown events!\n\n"
			    "Is this an older tool processing a perf.data "
			    "file generated by a more recent tool?\n\n"
			    "If that is not the case, consider "
			    "reporting to linux-kernel@vger.kernel.org.\n\n",
			    session->hists.stats.nr_unknown_events);
	}

	return err;
}

int perf_session__process_events(struct perf_session *self,
				 struct perf_event_ops *ops)
{
	int err;

	if (perf_session__register_idle_thread(self) == NULL)
		return -ENOMEM;

	if (!self->fd_pipe)
		err = __perf_session__process_events(self,
						     self->header.data_offset,
						     self->header.data_size,
						     self->size, ops);
	else
		err = __perf_session__process_pipe_events(self, ops);

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

int perf_session__set_kallsyms_ref_reloc_sym(struct map **maps,
					     const char *symbol_name,
					     u64 addr)
{
	char *bracket;
	enum map_type i;
	struct ref_reloc_sym *ref;

	ref = zalloc(sizeof(struct ref_reloc_sym));
	if (ref == NULL)
		return -ENOMEM;

	ref->name = strdup(symbol_name);
	if (ref->name == NULL) {
		free(ref);
		return -ENOMEM;
	}

	bracket = strchr(ref->name, ']');
	if (bracket)
		*bracket = '\0';

	ref->addr = addr;

	for (i = 0; i < MAP__NR_TYPES; ++i) {
		struct kmap *kmap = map__kmap(maps[i]);
		kmap->ref_reloc_sym = ref;
	}

	return 0;
}

size_t perf_session__fprintf_dsos(struct perf_session *self, FILE *fp)
{
	return __dsos__fprintf(&self->host_machine.kernel_dsos, fp) +
	       __dsos__fprintf(&self->host_machine.user_dsos, fp) +
	       machines__fprintf_dsos(&self->machines, fp);
}

size_t perf_session__fprintf_dsos_buildid(struct perf_session *self, FILE *fp,
					  bool with_hits)
{
	size_t ret = machine__fprintf_dsos_buildid(&self->host_machine, fp, with_hits);
	return ret + machines__fprintf_dsos_buildid(&self->machines, fp, with_hits);
}
