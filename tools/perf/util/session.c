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
