#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "header.h"

/*
 * Create new perf.data header attribute:
 */
struct perf_header_attr *perf_header_attr__new(struct perf_event_attr *attr)
{
	struct perf_header_attr *self = malloc(sizeof(*self));

	if (!self)
		die("nomem");

	self->attr = *attr;
	self->ids = 0;
	self->size = 1;
	self->id = malloc(sizeof(u64));

	if (!self->id)
		die("nomem");

	return self;
}

void perf_header_attr__add_id(struct perf_header_attr *self, u64 id)
{
	int pos = self->ids;

	self->ids++;
	if (self->ids > self->size) {
		self->size *= 2;
		self->id = realloc(self->id, self->size * sizeof(u64));
		if (!self->id)
			die("nomem");
	}
	self->id[pos] = id;
}

/*
 * Create new perf.data header:
 */
struct perf_header *perf_header__new(void)
{
	struct perf_header *self = malloc(sizeof(*self));

	if (!self)
		die("nomem");

	self->frozen = 0;

	self->attrs = 0;
	self->size = 1;
	self->attr = malloc(sizeof(void *));

	if (!self->attr)
		die("nomem");

	self->data_offset = 0;
	self->data_size = 0;

	return self;
}

void perf_header__add_attr(struct perf_header *self,
			   struct perf_header_attr *attr)
{
	int pos = self->attrs;

	if (self->frozen)
		die("frozen");

	self->attrs++;
	if (self->attrs > self->size) {
		self->size *= 2;
		self->attr = realloc(self->attr, self->size * sizeof(void *));
		if (!self->attr)
			die("nomem");
	}
	self->attr[pos] = attr;
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
		printf("Event %s will be truncated\n", name);

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

struct perf_file_section {
	u64 offset;
	u64 size;
};

struct perf_file_attr {
	struct perf_event_attr	attr;
	struct perf_file_section	ids;
};

struct perf_file_header {
	u64				magic;
	u64				size;
	u64				attr_size;
	struct perf_file_section	attrs;
	struct perf_file_section	data;
	struct perf_file_section	event_types;
};

static void do_write(int fd, void *buf, size_t size)
{
	while (size) {
		int ret = write(fd, buf, size);

		if (ret < 0)
			die("failed to write");

		size -= ret;
		buf += ret;
	}
}

void perf_header__write(struct perf_header *self, int fd)
{
	struct perf_file_header f_header;
	struct perf_file_attr   f_attr;
	struct perf_header_attr	*attr;
	int i;

	lseek(fd, sizeof(f_header), SEEK_SET);


	for (i = 0; i < self->attrs; i++) {
		attr = self->attr[i];

		attr->id_offset = lseek(fd, 0, SEEK_CUR);
		do_write(fd, attr->id, attr->ids * sizeof(u64));
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
		do_write(fd, &f_attr, sizeof(f_attr));
	}

	self->event_offset = lseek(fd, 0, SEEK_CUR);
	self->event_size = event_count * sizeof(struct perf_trace_event_type);
	if (events)
		do_write(fd, events, self->event_size);


	self->data_offset = lseek(fd, 0, SEEK_CUR);

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

	lseek(fd, 0, SEEK_SET);
	do_write(fd, &f_header, sizeof(f_header));
	lseek(fd, self->data_offset + self->data_size, SEEK_SET);

	self->frozen = 1;
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

struct perf_header *perf_header__read(int fd)
{
	struct perf_header	*self = perf_header__new();
	struct perf_file_header f_header;
	struct perf_file_attr	f_attr;
	u64			f_id;

	int nr_attrs, nr_ids, i, j;

	lseek(fd, 0, SEEK_SET);
	do_read(fd, &f_header, sizeof(f_header));

	if (f_header.magic	!= PERF_MAGIC		||
	    f_header.size	!= sizeof(f_header)	||
	    f_header.attr_size	!= sizeof(f_attr))
		die("incompatible file format");

	nr_attrs = f_header.attrs.size / sizeof(f_attr);
	lseek(fd, f_header.attrs.offset, SEEK_SET);

	for (i = 0; i < nr_attrs; i++) {
		struct perf_header_attr *attr;
		off_t tmp;

		do_read(fd, &f_attr, sizeof(f_attr));
		tmp = lseek(fd, 0, SEEK_CUR);

		attr = perf_header_attr__new(&f_attr.attr);

		nr_ids = f_attr.ids.size / sizeof(u64);
		lseek(fd, f_attr.ids.offset, SEEK_SET);

		for (j = 0; j < nr_ids; j++) {
			do_read(fd, &f_id, sizeof(f_id));

			perf_header_attr__add_id(attr, f_id);
		}
		perf_header__add_attr(self, attr);
		lseek(fd, tmp, SEEK_SET);
	}

	if (f_header.event_types.size) {
		lseek(fd, f_header.event_types.offset, SEEK_SET);
		events = malloc(f_header.event_types.size);
		if (!events)
			die("nomem");
		do_read(fd, events, f_header.event_types.size);
		event_count =  f_header.event_types.size / sizeof(struct perf_trace_event_type);
	}
	self->event_offset = f_header.event_types.offset;
	self->event_size   = f_header.event_types.size;

	self->data_offset = f_header.data.offset;
	self->data_size   = f_header.data.size;

	lseek(fd, self->data_offset, SEEK_SET);

	self->frozen = 1;

	return self;
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
