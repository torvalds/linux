#include <linux/kernel.h>

#include <unistd.h>
#include <sys/types.h>

#include "session.h"
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

struct perf_session *perf_session__new(const char *filename, int mode,
				       bool force)
{
	size_t len = strlen(filename) + 1;
	struct perf_session *self = zalloc(sizeof(*self) + len);

	if (self == NULL)
		goto out;

	if (perf_header__init(&self->header) < 0)
		goto out_delete;

	memcpy(self->filename, filename, len);

	if (mode == O_RDONLY && perf_session__open(self, force) < 0) {
		perf_session__delete(self);
		self = NULL;
	}
out:
	return self;
out_delete:
	free(self);
	return NULL;
}

void perf_session__delete(struct perf_session *self)
{
	perf_header__exit(&self->header);
	close(self->fd);
	free(self);
}
