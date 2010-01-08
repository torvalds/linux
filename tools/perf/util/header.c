#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/list.h>

#include "util.h"
#include "header.h"
#include "../perf.h"
#include "trace-event.h"
#include "session.h"
#include "symbol.h"
#include "debug.h"

/*
 * Create new perf.data header attribute:
 */
struct perf_header_attr *perf_header_attr__new(struct perf_event_attr *attr)
{
	struct perf_header_attr *self = malloc(sizeof(*self));

	if (self != NULL) {
		self->attr = *attr;
		self->ids  = 0;
		self->size = 1;
		self->id   = malloc(sizeof(u64));
		if (self->id == NULL) {
			free(self);
			self = NULL;
		}
	}

	return self;
}

void perf_header_attr__delete(struct perf_header_attr *self)
{
	free(self->id);
	free(self);
}

int perf_header_attr__add_id(struct perf_header_attr *self, u64 id)
{
	int pos = self->ids;

	self->ids++;
	if (self->ids > self->size) {
		int nsize = self->size * 2;
		u64 *nid = realloc(self->id, nsize * sizeof(u64));

		if (nid == NULL)
			return -1;

		self->size = nsize;
		self->id = nid;
	}
	self->id[pos] = id;
	return 0;
}

int perf_header__init(struct perf_header *self)
{
	self->size = 1;
	self->attr = malloc(sizeof(void *));
	return self->attr == NULL ? -ENOMEM : 0;
}

void perf_header__exit(struct perf_header *self)
{
	int i;
	for (i = 0; i < self->attrs; ++i)
                perf_header_attr__delete(self->attr[i]);
	free(self->attr);
}

int perf_header__add_attr(struct perf_header *self,
			  struct perf_header_attr *attr)
{
	if (self->frozen)
		return -1;

	if (self->attrs == self->size) {
		int nsize = self->size * 2;
		struct perf_header_attr **nattr;

		nattr = realloc(self->attr, nsize * sizeof(void *));
		if (nattr == NULL)
			return -1;

		self->size = nsize;
		self->attr = nattr;
	}

	self->attr[self->attrs++] = attr;
	return 0;
}

#define MAX_EVENT_NAME 64

struct perf_trace_event_type {
	u64	event_id;
	char	name[MAX_EVENT_NAME];
};

static int event_count;
static struct perf_trace_event_type *events;

void perf_header__push_event(u64 id, const char *name)
{
	if (strlen(name) > MAX_EVENT_NAME)
		pr_warning("Event %s will be truncated\n", name);

	if (!events) {
		events = malloc(sizeof(struct perf_trace_event_type));
		if (!events)
			die("nomem");
	} else {
		events = realloc(events, (event_count + 1) * sizeof(struct perf_trace_event_type));
		if (!events)
			die("nomem");
	}
	memset(&events[event_count], 0, sizeof(struct perf_trace_event_type));
	events[event_count].event_id = id;
	strncpy(events[event_count].name, name, MAX_EVENT_NAME - 1);
	event_count++;
}

char *perf_header__find_event(u64 id)
{
	int i;
	for (i = 0 ; i < event_count; i++) {
		if (events[i].event_id == id)
			return events[i].name;
	}
	return NULL;
}

static const char *__perf_magic = "PERFFILE";

#define PERF_MAGIC	(*(u64 *)__perf_magic)

struct perf_file_attr {
	struct perf_event_attr	attr;
	struct perf_file_section	ids;
};

void perf_header__set_feat(struct perf_header *self, int feat)
{
	set_bit(feat, self->adds_features);
}

bool perf_header__has_feat(const struct perf_header *self, int feat)
{
	return test_bit(feat, self->adds_features);
}

static int do_write(int fd, const void *buf, size_t size)
{
	while (size) {
		int ret = write(fd, buf, size);

		if (ret < 0)
			return -errno;

		size -= ret;
		buf += ret;
	}

	return 0;
}

static int __dsos__write_buildid_table(struct list_head *head, int fd)
{
#define NAME_ALIGN	64
	struct dso *pos;
	static const char zero_buf[NAME_ALIGN];

	list_for_each_entry(pos, head, node) {
		int err;
		struct build_id_event b;
		size_t len;

		if (!pos->has_build_id)
			continue;
		len = pos->long_name_len + 1;
		len = ALIGN(len, NAME_ALIGN);
		memset(&b, 0, sizeof(b));
		memcpy(&b.build_id, pos->build_id, sizeof(pos->build_id));
		b.header.size = sizeof(b) + len;
		err = do_write(fd, &b, sizeof(b));
		if (err < 0)
			return err;
		err = do_write(fd, pos->long_name, pos->long_name_len + 1);
		if (err < 0)
			return err;
		err = do_write(fd, zero_buf, len - pos->long_name_len - 1);
		if (err < 0)
			return err;
	}

	return 0;
}

static int dsos__write_buildid_table(int fd)
{
	int err = __dsos__write_buildid_table(&dsos__kernel, fd);
	if (err == 0)
		err = __dsos__write_buildid_table(&dsos__user, fd);
	return err;
}

static int perf_header__adds_write(struct perf_header *self, int fd)
{
	int nr_sections;
	struct perf_file_section *feat_sec;
	int sec_size;
	u64 sec_start;
	int idx = 0, err;

	if (dsos__read_build_ids())
		perf_header__set_feat(self, HEADER_BUILD_ID);

	nr_sections = bitmap_weight(self->adds_features, HEADER_FEAT_BITS);
	if (!nr_sections)
		return 0;

	feat_sec = calloc(sizeof(*feat_sec), nr_sections);
	if (feat_sec == NULL)
		return -ENOMEM;

	sec_size = sizeof(*feat_sec) * nr_sections;

	sec_start = self->data_offset + self->data_size;
	lseek(fd, sec_start + sec_size, SEEK_SET);

	if (perf_header__has_feat(self, HEADER_TRACE_INFO)) {
		struct perf_file_section *trace_sec;

		trace_sec = &feat_sec[idx++];

		/* Write trace info */
		trace_sec->offset = lseek(fd, 0, SEEK_CUR);
		read_tracing_data(fd, attrs, nr_counters);
		trace_sec->size = lseek(fd, 0, SEEK_CUR) - trace_sec->offset;
	}


	if (perf_header__has_feat(self, HEADER_BUILD_ID)) {
		struct perf_file_section *buildid_sec;

		buildid_sec = &feat_sec[idx++];

		/* Write build-ids */
		buildid_sec->offset = lseek(fd, 0, SEEK_CUR);
		err = dsos__write_buildid_table(fd);
		if (err < 0) {
			pr_debug("failed to write buildid table\n");
			goto out_free;
		}
		buildid_sec->size = lseek(fd, 0, SEEK_CUR) - buildid_sec->offset;
	}

	lseek(fd, sec_start, SEEK_SET);
	err = do_write(fd, feat_sec, sec_size);
	if (err < 0)
		pr_debug("failed to write feature section\n");
out_free:
	free(feat_sec);
	return err;
}

int perf_header__write(struct perf_header *self, int fd, bool at_exit)
{
	struct perf_file_header f_header;
	struct perf_file_attr   f_attr;
	struct perf_header_attr	*attr;
	int i, err;

	lseek(fd, sizeof(f_header), SEEK_SET);


	for (i = 0; i < self->attrs; i++) {
		attr = self->attr[i];

		attr->id_offset = lseek(fd, 0, SEEK_CUR);
		err = do_write(fd, attr->id, attr->ids * sizeof(u64));
		if (err < 0) {
			pr_debug("failed to write perf header\n");
			return err;
		}
	}


	self->attr_offset = lseek(fd, 0, SEEK_CUR);

	for (i = 0; i < self->attrs; i++) {
		attr = self->attr[i];

		f_attr = (struct perf_file_attr){
			.attr = attr->attr,
			.ids  = {
				.offset = attr->id_offset,
				.size   = attr->ids * sizeof(u64),
			}
		};
		err = do_write(fd, &f_attr, sizeof(f_attr));
		if (err < 0) {
			pr_debug("failed to write perf header attribute\n");
			return err;
		}
	}

	self->event_offset = lseek(fd, 0, SEEK_CUR);
	self->event_size = event_count * sizeof(struct perf_trace_event_type);
	if (events) {
		err = do_write(fd, events, self->event_size);
		if (err < 0) {
			pr_debug("failed to write perf header events\n");
			return err;
		}
	}

	self->data_offset = lseek(fd, 0, SEEK_CUR);

	if (at_exit) {
		err = perf_header__adds_write(self, fd);
		if (err < 0)
			return err;
	}

	f_header = (struct perf_file_header){
		.magic	   = PERF_MAGIC,
		.size	   = sizeof(f_header),
		.attr_size = sizeof(f_attr),
		.attrs = {
			.offset = self->attr_offset,
			.size   = self->attrs * sizeof(f_attr),
		},
		.data = {
			.offset = self->data_offset,
			.size	= self->data_size,
		},
		.event_types = {
			.offset = self->event_offset,
			.size	= self->event_size,
		},
	};

	memcpy(&f_header.adds_features, &self->adds_features, sizeof(self->adds_features));

	lseek(fd, 0, SEEK_SET);
	err = do_write(fd, &f_header, sizeof(f_header));
	if (err < 0) {
		pr_debug("failed to write perf header\n");
		return err;
	}
	lseek(fd, self->data_offset + self->data_size, SEEK_SET);

	self->frozen = 1;
	return 0;
}

static void do_read(int fd, void *buf, size_t size)
{
	while (size) {
		int ret = read(fd, buf, size);

		if (ret < 0)
			die("failed to read");
		if (ret == 0)
			die("failed to read: missing data");

		size -= ret;
		buf += ret;
	}
}

int perf_header__process_sections(struct perf_header *self, int fd,
				  int (*process)(struct perf_file_section *self,
						 int feat, int fd))
{
	struct perf_file_section *feat_sec;
	int nr_sections;
	int sec_size;
	int idx = 0;
	int err = 0, feat = 1;

	nr_sections = bitmap_weight(self->adds_features, HEADER_FEAT_BITS);
	if (!nr_sections)
		return 0;

	feat_sec = calloc(sizeof(*feat_sec), nr_sections);
	if (!feat_sec)
		return -1;

	sec_size = sizeof(*feat_sec) * nr_sections;

	lseek(fd, self->data_offset + self->data_size, SEEK_SET);

	do_read(fd, feat_sec, sec_size);

	while (idx < nr_sections && feat < HEADER_LAST_FEATURE) {
		if (perf_header__has_feat(self, feat)) {
			struct perf_file_section *sec = &feat_sec[idx++];

			err = process(sec, feat, fd);
			if (err < 0)
				break;
		}
		++feat;
	}

	free(feat_sec);
	return err;
};

int perf_file_header__read(struct perf_file_header *self,
			   struct perf_header *ph, int fd)
{
	lseek(fd, 0, SEEK_SET);
	do_read(fd, self, sizeof(*self));

	if (self->magic     != PERF_MAGIC ||
	    self->attr_size != sizeof(struct perf_file_attr))
		return -1;

	if (self->size != sizeof(*self)) {
		/* Support the previous format */
		if (self->size == offsetof(typeof(*self), adds_features))
			bitmap_zero(self->adds_features, HEADER_FEAT_BITS);
		else
			return -1;
	}

	memcpy(&ph->adds_features, &self->adds_features,
	       sizeof(self->adds_features));

	ph->event_offset = self->event_types.offset;
	ph->event_size	 = self->event_types.size;
	ph->data_offset	 = self->data.offset;
	ph->data_size	 = self->data.size;
	return 0;
}

static int perf_file_section__process(struct perf_file_section *self,
				      int feat, int fd)
{
	if (lseek(fd, self->offset, SEEK_SET) < 0) {
		pr_debug("Failed to lseek to %Ld offset for feature %d, "
			 "continuing...\n", self->offset, feat);
		return 0;
	}

	switch (feat) {
	case HEADER_TRACE_INFO:
		trace_report(fd);
		break;

	case HEADER_BUILD_ID:
		if (perf_header__read_build_ids(fd, self->offset, self->size))
			pr_debug("Failed to read buildids, continuing...\n");
		break;
	default:
		pr_debug("unknown feature %d, continuing...\n", feat);
	}

	return 0;
}

int perf_header__read(struct perf_header *self, int fd)
{
	struct perf_file_header f_header;
	struct perf_file_attr	f_attr;
	u64			f_id;
	int nr_attrs, nr_ids, i, j;

	if (perf_file_header__read(&f_header, self, fd) < 0) {
		pr_debug("incompatible file format\n");
		return -EINVAL;
	}

	nr_attrs = f_header.attrs.size / sizeof(f_attr);
	lseek(fd, f_header.attrs.offset, SEEK_SET);

	for (i = 0; i < nr_attrs; i++) {
		struct perf_header_attr *attr;
		off_t tmp;

		do_read(fd, &f_attr, sizeof(f_attr));
		tmp = lseek(fd, 0, SEEK_CUR);

		attr = perf_header_attr__new(&f_attr.attr);
		if (attr == NULL)
			 return -ENOMEM;

		nr_ids = f_attr.ids.size / sizeof(u64);
		lseek(fd, f_attr.ids.offset, SEEK_SET);

		for (j = 0; j < nr_ids; j++) {
			do_read(fd, &f_id, sizeof(f_id));

			if (perf_header_attr__add_id(attr, f_id) < 0) {
				perf_header_attr__delete(attr);
				return -ENOMEM;
			}
		}
		if (perf_header__add_attr(self, attr) < 0) {
			perf_header_attr__delete(attr);
			return -ENOMEM;
		}

		lseek(fd, tmp, SEEK_SET);
	}

	if (f_header.event_types.size) {
		lseek(fd, f_header.event_types.offset, SEEK_SET);
		events = malloc(f_header.event_types.size);
		if (events == NULL)
			return -ENOMEM;
		do_read(fd, events, f_header.event_types.size);
		event_count =  f_header.event_types.size / sizeof(struct perf_trace_event_type);
	}

	perf_header__process_sections(self, fd, perf_file_section__process);

	lseek(fd, self->data_offset, SEEK_SET);

	self->frozen = 1;
	return 0;
}

u64 perf_header__sample_type(struct perf_header *header)
{
	u64 type = 0;
	int i;

	for (i = 0; i < header->attrs; i++) {
		struct perf_header_attr *attr = header->attr[i];

		if (!type)
			type = attr->attr.sample_type;
		else if (type != attr->attr.sample_type)
			die("non matching sample_type");
	}

	return type;
}

struct perf_event_attr *
perf_header__find_attr(u64 id, struct perf_header *header)
{
	int i;

	for (i = 0; i < header->attrs; i++) {
		struct perf_header_attr *attr = header->attr[i];
		int j;

		for (j = 0; j < attr->ids; j++) {
			if (attr->id[j] == id)
				return &attr->attr;
		}
	}

	return NULL;
}
