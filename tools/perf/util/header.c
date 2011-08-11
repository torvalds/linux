#define _FILE_OFFSET_BITS 64

#include <sys/types.h>
#include <byteswap.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/list.h>
#include <linux/kernel.h>

#include "evlist.h"
#include "evsel.h"
#include "util.h"
#include "header.h"
#include "../perf.h"
#include "trace-event.h"
#include "session.h"
#include "symbol.h"
#include "debug.h"

static bool no_buildid_cache = false;

static int event_count;
static struct perf_trace_event_type *events;

int perf_header__push_event(u64 id, const char *name)
{
	if (strlen(name) > MAX_EVENT_NAME)
		pr_warning("Event %s will be truncated\n", name);

	if (!events) {
		events = malloc(sizeof(struct perf_trace_event_type));
		if (events == NULL)
			return -ENOMEM;
	} else {
		struct perf_trace_event_type *nevents;

		nevents = realloc(events, (event_count + 1) * sizeof(*events));
		if (nevents == NULL)
			return -ENOMEM;
		events = nevents;
	}
	memset(&events[event_count], 0, sizeof(struct perf_trace_event_type));
	events[event_count].event_id = id;
	strncpy(events[event_count].name, name, MAX_EVENT_NAME - 1);
	event_count++;
	return 0;
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

void perf_header__set_feat(struct perf_header *header, int feat)
{
	set_bit(feat, header->adds_features);
}

void perf_header__clear_feat(struct perf_header *header, int feat)
{
	clear_bit(feat, header->adds_features);
}

bool perf_header__has_feat(const struct perf_header *header, int feat)
{
	return test_bit(feat, header->adds_features);
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

#define NAME_ALIGN 64

static int write_padded(int fd, const void *bf, size_t count,
			size_t count_aligned)
{
	static const char zero_buf[NAME_ALIGN];
	int err = do_write(fd, bf, count);

	if (!err)
		err = do_write(fd, zero_buf, count_aligned - count);

	return err;
}

#define dsos__for_each_with_build_id(pos, head)	\
	list_for_each_entry(pos, head, node)	\
		if (!pos->has_build_id)		\
			continue;		\
		else

static int __dsos__write_buildid_table(struct list_head *head, pid_t pid,
				u16 misc, int fd)
{
	struct dso *pos;

	dsos__for_each_with_build_id(pos, head) {
		int err;
		struct build_id_event b;
		size_t len;

		if (!pos->hit)
			continue;
		len = pos->long_name_len + 1;
		len = ALIGN(len, NAME_ALIGN);
		memset(&b, 0, sizeof(b));
		memcpy(&b.build_id, pos->build_id, sizeof(pos->build_id));
		b.pid = pid;
		b.header.misc = misc;
		b.header.size = sizeof(b) + len;
		err = do_write(fd, &b, sizeof(b));
		if (err < 0)
			return err;
		err = write_padded(fd, pos->long_name,
				   pos->long_name_len + 1, len);
		if (err < 0)
			return err;
	}

	return 0;
}

static int machine__write_buildid_table(struct machine *machine, int fd)
{
	int err;
	u16 kmisc = PERF_RECORD_MISC_KERNEL,
	    umisc = PERF_RECORD_MISC_USER;

	if (!machine__is_host(machine)) {
		kmisc = PERF_RECORD_MISC_GUEST_KERNEL;
		umisc = PERF_RECORD_MISC_GUEST_USER;
	}

	err = __dsos__write_buildid_table(&machine->kernel_dsos, machine->pid,
					  kmisc, fd);
	if (err == 0)
		err = __dsos__write_buildid_table(&machine->user_dsos,
						  machine->pid, umisc, fd);
	return err;
}

static int dsos__write_buildid_table(struct perf_header *header, int fd)
{
	struct perf_session *session = container_of(header,
			struct perf_session, header);
	struct rb_node *nd;
	int err = machine__write_buildid_table(&session->host_machine, fd);

	if (err)
		return err;

	for (nd = rb_first(&session->machines); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		err = machine__write_buildid_table(pos, fd);
		if (err)
			break;
	}
	return err;
}

int build_id_cache__add_s(const char *sbuild_id, const char *debugdir,
			  const char *name, bool is_kallsyms)
{
	const size_t size = PATH_MAX;
	char *realname, *filename = zalloc(size),
	     *linkname = zalloc(size), *targetname;
	int len, err = -1;

	if (is_kallsyms) {
		if (symbol_conf.kptr_restrict) {
			pr_debug("Not caching a kptr_restrict'ed /proc/kallsyms\n");
			return 0;
		}
		realname = (char *)name;
	} else
		realname = realpath(name, NULL);

	if (realname == NULL || filename == NULL || linkname == NULL)
		goto out_free;

	len = snprintf(filename, size, "%s%s%s",
		       debugdir, is_kallsyms ? "/" : "", realname);
	if (mkdir_p(filename, 0755))
		goto out_free;

	snprintf(filename + len, sizeof(filename) - len, "/%s", sbuild_id);

	if (access(filename, F_OK)) {
		if (is_kallsyms) {
			 if (copyfile("/proc/kallsyms", filename))
				goto out_free;
		} else if (link(realname, filename) && copyfile(name, filename))
			goto out_free;
	}

	len = snprintf(linkname, size, "%s/.build-id/%.2s",
		       debugdir, sbuild_id);

	if (access(linkname, X_OK) && mkdir_p(linkname, 0755))
		goto out_free;

	snprintf(linkname + len, size - len, "/%s", sbuild_id + 2);
	targetname = filename + strlen(debugdir) - 5;
	memcpy(targetname, "../..", 5);

	if (symlink(targetname, linkname) == 0)
		err = 0;
out_free:
	if (!is_kallsyms)
		free(realname);
	free(filename);
	free(linkname);
	return err;
}

static int build_id_cache__add_b(const u8 *build_id, size_t build_id_size,
				 const char *name, const char *debugdir,
				 bool is_kallsyms)
{
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];

	build_id__sprintf(build_id, build_id_size, sbuild_id);

	return build_id_cache__add_s(sbuild_id, debugdir, name, is_kallsyms);
}

int build_id_cache__remove_s(const char *sbuild_id, const char *debugdir)
{
	const size_t size = PATH_MAX;
	char *filename = zalloc(size),
	     *linkname = zalloc(size);
	int err = -1;

	if (filename == NULL || linkname == NULL)
		goto out_free;

	snprintf(linkname, size, "%s/.build-id/%.2s/%s",
		 debugdir, sbuild_id, sbuild_id + 2);

	if (access(linkname, F_OK))
		goto out_free;

	if (readlink(linkname, filename, size) < 0)
		goto out_free;

	if (unlink(linkname))
		goto out_free;

	/*
	 * Since the link is relative, we must make it absolute:
	 */
	snprintf(linkname, size, "%s/.build-id/%.2s/%s",
		 debugdir, sbuild_id, filename);

	if (unlink(linkname))
		goto out_free;

	err = 0;
out_free:
	free(filename);
	free(linkname);
	return err;
}

static int dso__cache_build_id(struct dso *dso, const char *debugdir)
{
	bool is_kallsyms = dso->kernel && dso->long_name[0] != '/';

	return build_id_cache__add_b(dso->build_id, sizeof(dso->build_id),
				     dso->long_name, debugdir, is_kallsyms);
}

static int __dsos__cache_build_ids(struct list_head *head, const char *debugdir)
{
	struct dso *pos;
	int err = 0;

	dsos__for_each_with_build_id(pos, head)
		if (dso__cache_build_id(pos, debugdir))
			err = -1;

	return err;
}

static int machine__cache_build_ids(struct machine *machine, const char *debugdir)
{
	int ret = __dsos__cache_build_ids(&machine->kernel_dsos, debugdir);
	ret |= __dsos__cache_build_ids(&machine->user_dsos, debugdir);
	return ret;
}

static int perf_session__cache_build_ids(struct perf_session *session)
{
	struct rb_node *nd;
	int ret;
	char debugdir[PATH_MAX];

	snprintf(debugdir, sizeof(debugdir), "%s", buildid_dir);

	if (mkdir(debugdir, 0755) != 0 && errno != EEXIST)
		return -1;

	ret = machine__cache_build_ids(&session->host_machine, debugdir);

	for (nd = rb_first(&session->machines); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		ret |= machine__cache_build_ids(pos, debugdir);
	}
	return ret ? -1 : 0;
}

static bool machine__read_build_ids(struct machine *machine, bool with_hits)
{
	bool ret = __dsos__read_build_ids(&machine->kernel_dsos, with_hits);
	ret |= __dsos__read_build_ids(&machine->user_dsos, with_hits);
	return ret;
}

static bool perf_session__read_build_ids(struct perf_session *session, bool with_hits)
{
	struct rb_node *nd;
	bool ret = machine__read_build_ids(&session->host_machine, with_hits);

	for (nd = rb_first(&session->machines); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);
		ret |= machine__read_build_ids(pos, with_hits);
	}

	return ret;
}

static int perf_header__adds_write(struct perf_header *header,
				   struct perf_evlist *evlist, int fd)
{
	int nr_sections;
	struct perf_session *session;
	struct perf_file_section *feat_sec;
	int sec_size;
	u64 sec_start;
	int idx = 0, err;

	session = container_of(header, struct perf_session, header);

	if (perf_header__has_feat(header, HEADER_BUILD_ID &&
	    !perf_session__read_build_ids(session, true)))
		perf_header__clear_feat(header, HEADER_BUILD_ID);

	nr_sections = bitmap_weight(header->adds_features, HEADER_FEAT_BITS);
	if (!nr_sections)
		return 0;

	feat_sec = calloc(sizeof(*feat_sec), nr_sections);
	if (feat_sec == NULL)
		return -ENOMEM;

	sec_size = sizeof(*feat_sec) * nr_sections;

	sec_start = header->data_offset + header->data_size;
	lseek(fd, sec_start + sec_size, SEEK_SET);

	if (perf_header__has_feat(header, HEADER_TRACE_INFO)) {
		struct perf_file_section *trace_sec;

		trace_sec = &feat_sec[idx++];

		/* Write trace info */
		trace_sec->offset = lseek(fd, 0, SEEK_CUR);
		read_tracing_data(fd, &evlist->entries);
		trace_sec->size = lseek(fd, 0, SEEK_CUR) - trace_sec->offset;
	}

	if (perf_header__has_feat(header, HEADER_BUILD_ID)) {
		struct perf_file_section *buildid_sec;

		buildid_sec = &feat_sec[idx++];

		/* Write build-ids */
		buildid_sec->offset = lseek(fd, 0, SEEK_CUR);
		err = dsos__write_buildid_table(header, fd);
		if (err < 0) {
			pr_debug("failed to write buildid table\n");
			goto out_free;
		}
		buildid_sec->size = lseek(fd, 0, SEEK_CUR) -
					  buildid_sec->offset;
		if (!no_buildid_cache)
			perf_session__cache_build_ids(session);
	}

	lseek(fd, sec_start, SEEK_SET);
	err = do_write(fd, feat_sec, sec_size);
	if (err < 0)
		pr_debug("failed to write feature section\n");
out_free:
	free(feat_sec);
	return err;
}

int perf_header__write_pipe(int fd)
{
	struct perf_pipe_file_header f_header;
	int err;

	f_header = (struct perf_pipe_file_header){
		.magic	   = PERF_MAGIC,
		.size	   = sizeof(f_header),
	};

	err = do_write(fd, &f_header, sizeof(f_header));
	if (err < 0) {
		pr_debug("failed to write perf pipe header\n");
		return err;
	}

	return 0;
}

int perf_session__write_header(struct perf_session *session,
			       struct perf_evlist *evlist,
			       int fd, bool at_exit)
{
	struct perf_file_header f_header;
	struct perf_file_attr   f_attr;
	struct perf_header *header = &session->header;
	struct perf_evsel *attr, *pair = NULL;
	int err;

	lseek(fd, sizeof(f_header), SEEK_SET);

	if (session->evlist != evlist)
		pair = list_entry(session->evlist->entries.next, struct perf_evsel, node);

	list_for_each_entry(attr, &evlist->entries, node) {
		attr->id_offset = lseek(fd, 0, SEEK_CUR);
		err = do_write(fd, attr->id, attr->ids * sizeof(u64));
		if (err < 0) {
out_err_write:
			pr_debug("failed to write perf header\n");
			return err;
		}
		if (session->evlist != evlist) {
			err = do_write(fd, pair->id, pair->ids * sizeof(u64));
			if (err < 0)
				goto out_err_write;
			attr->ids += pair->ids;
			pair = list_entry(pair->node.next, struct perf_evsel, node);
		}
	}

	header->attr_offset = lseek(fd, 0, SEEK_CUR);

	list_for_each_entry(attr, &evlist->entries, node) {
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

	header->event_offset = lseek(fd, 0, SEEK_CUR);
	header->event_size = event_count * sizeof(struct perf_trace_event_type);
	if (events) {
		err = do_write(fd, events, header->event_size);
		if (err < 0) {
			pr_debug("failed to write perf header events\n");
			return err;
		}
	}

	header->data_offset = lseek(fd, 0, SEEK_CUR);

	if (at_exit) {
		err = perf_header__adds_write(header, evlist, fd);
		if (err < 0)
			return err;
	}

	f_header = (struct perf_file_header){
		.magic	   = PERF_MAGIC,
		.size	   = sizeof(f_header),
		.attr_size = sizeof(f_attr),
		.attrs = {
			.offset = header->attr_offset,
			.size   = evlist->nr_entries * sizeof(f_attr),
		},
		.data = {
			.offset = header->data_offset,
			.size	= header->data_size,
		},
		.event_types = {
			.offset = header->event_offset,
			.size	= header->event_size,
		},
	};

	memcpy(&f_header.adds_features, &header->adds_features, sizeof(header->adds_features));

	lseek(fd, 0, SEEK_SET);
	err = do_write(fd, &f_header, sizeof(f_header));
	if (err < 0) {
		pr_debug("failed to write perf header\n");
		return err;
	}
	lseek(fd, header->data_offset + header->data_size, SEEK_SET);

	header->frozen = 1;
	return 0;
}

static int perf_header__getbuffer64(struct perf_header *header,
				    int fd, void *buf, size_t size)
{
	if (readn(fd, buf, size) <= 0)
		return -1;

	if (header->needs_swap)
		mem_bswap_64(buf, size);

	return 0;
}

int perf_header__process_sections(struct perf_header *header, int fd,
				  int (*process)(struct perf_file_section *section,
						 struct perf_header *ph,
						 int feat, int fd))
{
	struct perf_file_section *feat_sec;
	int nr_sections;
	int sec_size;
	int idx = 0;
	int err = -1, feat = 1;

	nr_sections = bitmap_weight(header->adds_features, HEADER_FEAT_BITS);
	if (!nr_sections)
		return 0;

	feat_sec = calloc(sizeof(*feat_sec), nr_sections);
	if (!feat_sec)
		return -1;

	sec_size = sizeof(*feat_sec) * nr_sections;

	lseek(fd, header->data_offset + header->data_size, SEEK_SET);

	if (perf_header__getbuffer64(header, fd, feat_sec, sec_size))
		goto out_free;

	err = 0;
	while (idx < nr_sections && feat < HEADER_LAST_FEATURE) {
		if (perf_header__has_feat(header, feat)) {
			struct perf_file_section *sec = &feat_sec[idx++];

			err = process(sec, header, feat, fd);
			if (err < 0)
				break;
		}
		++feat;
	}
out_free:
	free(feat_sec);
	return err;
}

int perf_file_header__read(struct perf_file_header *header,
			   struct perf_header *ph, int fd)
{
	lseek(fd, 0, SEEK_SET);

	if (readn(fd, header, sizeof(*header)) <= 0 ||
	    memcmp(&header->magic, __perf_magic, sizeof(header->magic)))
		return -1;

	if (header->attr_size != sizeof(struct perf_file_attr)) {
		u64 attr_size = bswap_64(header->attr_size);

		if (attr_size != sizeof(struct perf_file_attr))
			return -1;

		mem_bswap_64(header, offsetof(struct perf_file_header,
					    adds_features));
		ph->needs_swap = true;
	}

	if (header->size != sizeof(*header)) {
		/* Support the previous format */
		if (header->size == offsetof(typeof(*header), adds_features))
			bitmap_zero(header->adds_features, HEADER_FEAT_BITS);
		else
			return -1;
	}

	memcpy(&ph->adds_features, &header->adds_features,
	       sizeof(ph->adds_features));
	/*
	 * FIXME: hack that assumes that if we need swap the perf.data file
	 * may be coming from an arch with a different word-size, ergo different
	 * DEFINE_BITMAP format, investigate more later, but for now its mostly
	 * safe to assume that we have a build-id section. Trace files probably
	 * have several other issues in this realm anyway...
	 */
	if (ph->needs_swap) {
		memset(&ph->adds_features, 0, sizeof(ph->adds_features));
		perf_header__set_feat(ph, HEADER_BUILD_ID);
	}

	ph->event_offset = header->event_types.offset;
	ph->event_size   = header->event_types.size;
	ph->data_offset  = header->data.offset;
	ph->data_size	 = header->data.size;
	return 0;
}

static int __event_process_build_id(struct build_id_event *bev,
				    char *filename,
				    struct perf_session *session)
{
	int err = -1;
	struct list_head *head;
	struct machine *machine;
	u16 misc;
	struct dso *dso;
	enum dso_kernel_type dso_type;

	machine = perf_session__findnew_machine(session, bev->pid);
	if (!machine)
		goto out;

	misc = bev->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	switch (misc) {
	case PERF_RECORD_MISC_KERNEL:
		dso_type = DSO_TYPE_KERNEL;
		head = &machine->kernel_dsos;
		break;
	case PERF_RECORD_MISC_GUEST_KERNEL:
		dso_type = DSO_TYPE_GUEST_KERNEL;
		head = &machine->kernel_dsos;
		break;
	case PERF_RECORD_MISC_USER:
	case PERF_RECORD_MISC_GUEST_USER:
		dso_type = DSO_TYPE_USER;
		head = &machine->user_dsos;
		break;
	default:
		goto out;
	}

	dso = __dsos__findnew(head, filename);
	if (dso != NULL) {
		char sbuild_id[BUILD_ID_SIZE * 2 + 1];

		dso__set_build_id(dso, &bev->build_id);

		if (filename[0] == '[')
			dso->kernel = dso_type;

		build_id__sprintf(dso->build_id, sizeof(dso->build_id),
				  sbuild_id);
		pr_debug("build id event received for %s: %s\n",
			 dso->long_name, sbuild_id);
	}

	err = 0;
out:
	return err;
}

static int perf_header__read_build_ids_abi_quirk(struct perf_header *header,
						 int input, u64 offset, u64 size)
{
	struct perf_session *session = container_of(header, struct perf_session, header);
	struct {
		struct perf_event_header   header;
		u8			   build_id[ALIGN(BUILD_ID_SIZE, sizeof(u64))];
		char			   filename[0];
	} old_bev;
	struct build_id_event bev;
	char filename[PATH_MAX];
	u64 limit = offset + size;

	while (offset < limit) {
		ssize_t len;

		if (read(input, &old_bev, sizeof(old_bev)) != sizeof(old_bev))
			return -1;

		if (header->needs_swap)
			perf_event_header__bswap(&old_bev.header);

		len = old_bev.header.size - sizeof(old_bev);
		if (read(input, filename, len) != len)
			return -1;

		bev.header = old_bev.header;
		bev.pid	   = 0;
		memcpy(bev.build_id, old_bev.build_id, sizeof(bev.build_id));
		__event_process_build_id(&bev, filename, session);

		offset += bev.header.size;
	}

	return 0;
}

static int perf_header__read_build_ids(struct perf_header *header,
				       int input, u64 offset, u64 size)
{
	struct perf_session *session = container_of(header, struct perf_session, header);
	struct build_id_event bev;
	char filename[PATH_MAX];
	u64 limit = offset + size, orig_offset = offset;
	int err = -1;

	while (offset < limit) {
		ssize_t len;

		if (read(input, &bev, sizeof(bev)) != sizeof(bev))
			goto out;

		if (header->needs_swap)
			perf_event_header__bswap(&bev.header);

		len = bev.header.size - sizeof(bev);
		if (read(input, filename, len) != len)
			goto out;
		/*
		 * The a1645ce1 changeset:
		 *
		 * "perf: 'perf kvm' tool for monitoring guest performance from host"
		 *
		 * Added a field to struct build_id_event that broke the file
		 * format.
		 *
		 * Since the kernel build-id is the first entry, process the
		 * table using the old format if the well known
		 * '[kernel.kallsyms]' string for the kernel build-id has the
		 * first 4 characters chopped off (where the pid_t sits).
		 */
		if (memcmp(filename, "nel.kallsyms]", 13) == 0) {
			if (lseek(input, orig_offset, SEEK_SET) == (off_t)-1)
				return -1;
			return perf_header__read_build_ids_abi_quirk(header, input, offset, size);
		}

		__event_process_build_id(&bev, filename, session);

		offset += bev.header.size;
	}
	err = 0;
out:
	return err;
}

static int perf_file_section__process(struct perf_file_section *section,
				      struct perf_header *ph,
				      int feat, int fd)
{
	if (lseek(fd, section->offset, SEEK_SET) == (off_t)-1) {
		pr_debug("Failed to lseek to %" PRIu64 " offset for feature "
			  "%d, continuing...\n", section->offset, feat);
		return 0;
	}

	switch (feat) {
	case HEADER_TRACE_INFO:
		trace_report(fd, false);
		break;

	case HEADER_BUILD_ID:
		if (perf_header__read_build_ids(ph, fd, section->offset, section->size))
			pr_debug("Failed to read buildids, continuing...\n");
		break;
	default:
		pr_debug("unknown feature %d, continuing...\n", feat);
	}

	return 0;
}

static int perf_file_header__read_pipe(struct perf_pipe_file_header *header,
				       struct perf_header *ph, int fd,
				       bool repipe)
{
	if (readn(fd, header, sizeof(*header)) <= 0 ||
	    memcmp(&header->magic, __perf_magic, sizeof(header->magic)))
		return -1;

	if (repipe && do_write(STDOUT_FILENO, header, sizeof(*header)) < 0)
		return -1;

	if (header->size != sizeof(*header)) {
		u64 size = bswap_64(header->size);

		if (size != sizeof(*header))
			return -1;

		ph->needs_swap = true;
	}

	return 0;
}

static int perf_header__read_pipe(struct perf_session *session, int fd)
{
	struct perf_header *header = &session->header;
	struct perf_pipe_file_header f_header;

	if (perf_file_header__read_pipe(&f_header, header, fd,
					session->repipe) < 0) {
		pr_debug("incompatible file format\n");
		return -EINVAL;
	}

	session->fd = fd;

	return 0;
}

int perf_session__read_header(struct perf_session *session, int fd)
{
	struct perf_header *header = &session->header;
	struct perf_file_header	f_header;
	struct perf_file_attr	f_attr;
	u64			f_id;
	int nr_attrs, nr_ids, i, j;

	session->evlist = perf_evlist__new(NULL, NULL);
	if (session->evlist == NULL)
		return -ENOMEM;

	if (session->fd_pipe)
		return perf_header__read_pipe(session, fd);

	if (perf_file_header__read(&f_header, header, fd) < 0) {
		pr_debug("incompatible file format\n");
		return -EINVAL;
	}

	nr_attrs = f_header.attrs.size / sizeof(f_attr);
	lseek(fd, f_header.attrs.offset, SEEK_SET);

	for (i = 0; i < nr_attrs; i++) {
		struct perf_evsel *evsel;
		off_t tmp;

		if (readn(fd, &f_attr, sizeof(f_attr)) <= 0)
			goto out_errno;

		if (header->needs_swap)
			perf_event__attr_swap(&f_attr.attr);

		tmp = lseek(fd, 0, SEEK_CUR);
		evsel = perf_evsel__new(&f_attr.attr, i);

		if (evsel == NULL)
			goto out_delete_evlist;
		/*
		 * Do it before so that if perf_evsel__alloc_id fails, this
		 * entry gets purged too at perf_evlist__delete().
		 */
		perf_evlist__add(session->evlist, evsel);

		nr_ids = f_attr.ids.size / sizeof(u64);
		/*
		 * We don't have the cpu and thread maps on the header, so
		 * for allocating the perf_sample_id table we fake 1 cpu and
		 * hattr->ids threads.
		 */
		if (perf_evsel__alloc_id(evsel, 1, nr_ids))
			goto out_delete_evlist;

		lseek(fd, f_attr.ids.offset, SEEK_SET);

		for (j = 0; j < nr_ids; j++) {
			if (perf_header__getbuffer64(header, fd, &f_id, sizeof(f_id)))
				goto out_errno;

			perf_evlist__id_add(session->evlist, evsel, 0, j, f_id);
		}

		lseek(fd, tmp, SEEK_SET);
	}

	if (f_header.event_types.size) {
		lseek(fd, f_header.event_types.offset, SEEK_SET);
		events = malloc(f_header.event_types.size);
		if (events == NULL)
			return -ENOMEM;
		if (perf_header__getbuffer64(header, fd, events,
					     f_header.event_types.size))
			goto out_errno;
		event_count =  f_header.event_types.size / sizeof(struct perf_trace_event_type);
	}

	perf_header__process_sections(header, fd, perf_file_section__process);

	lseek(fd, header->data_offset, SEEK_SET);

	header->frozen = 1;
	return 0;
out_errno:
	return -errno;

out_delete_evlist:
	perf_evlist__delete(session->evlist);
	session->evlist = NULL;
	return -ENOMEM;
}

int perf_event__synthesize_attr(struct perf_event_attr *attr, u16 ids, u64 *id,
				perf_event__handler_t process,
				struct perf_session *session)
{
	union perf_event *ev;
	size_t size;
	int err;

	size = sizeof(struct perf_event_attr);
	size = ALIGN(size, sizeof(u64));
	size += sizeof(struct perf_event_header);
	size += ids * sizeof(u64);

	ev = malloc(size);

	if (ev == NULL)
		return -ENOMEM;

	ev->attr.attr = *attr;
	memcpy(ev->attr.id, id, ids * sizeof(u64));

	ev->attr.header.type = PERF_RECORD_HEADER_ATTR;
	ev->attr.header.size = size;

	err = process(ev, NULL, session);

	free(ev);

	return err;
}

int perf_session__synthesize_attrs(struct perf_session *session,
				   perf_event__handler_t process)
{
	struct perf_evsel *attr;
	int err = 0;

	list_for_each_entry(attr, &session->evlist->entries, node) {
		err = perf_event__synthesize_attr(&attr->attr, attr->ids,
						  attr->id, process, session);
		if (err) {
			pr_debug("failed to create perf header attribute\n");
			return err;
		}
	}

	return err;
}

int perf_event__process_attr(union perf_event *event,
			     struct perf_session *session)
{
	unsigned int i, ids, n_ids;
	struct perf_evsel *evsel;

	if (session->evlist == NULL) {
		session->evlist = perf_evlist__new(NULL, NULL);
		if (session->evlist == NULL)
			return -ENOMEM;
	}

	evsel = perf_evsel__new(&event->attr.attr,
				session->evlist->nr_entries);
	if (evsel == NULL)
		return -ENOMEM;

	perf_evlist__add(session->evlist, evsel);

	ids = event->header.size;
	ids -= (void *)&event->attr.id - (void *)event;
	n_ids = ids / sizeof(u64);
	/*
	 * We don't have the cpu and thread maps on the header, so
	 * for allocating the perf_sample_id table we fake 1 cpu and
	 * hattr->ids threads.
	 */
	if (perf_evsel__alloc_id(evsel, 1, n_ids))
		return -ENOMEM;

	for (i = 0; i < n_ids; i++) {
		perf_evlist__id_add(session->evlist, evsel, 0, i,
				    event->attr.id[i]);
	}

	perf_session__update_sample_type(session);

	return 0;
}

int perf_event__synthesize_event_type(u64 event_id, char *name,
				      perf_event__handler_t process,
				      struct perf_session *session)
{
	union perf_event ev;
	size_t size = 0;
	int err = 0;

	memset(&ev, 0, sizeof(ev));

	ev.event_type.event_type.event_id = event_id;
	memset(ev.event_type.event_type.name, 0, MAX_EVENT_NAME);
	strncpy(ev.event_type.event_type.name, name, MAX_EVENT_NAME - 1);

	ev.event_type.header.type = PERF_RECORD_HEADER_EVENT_TYPE;
	size = strlen(name);
	size = ALIGN(size, sizeof(u64));
	ev.event_type.header.size = sizeof(ev.event_type) -
		(sizeof(ev.event_type.event_type.name) - size);

	err = process(&ev, NULL, session);

	return err;
}

int perf_event__synthesize_event_types(perf_event__handler_t process,
				       struct perf_session *session)
{
	struct perf_trace_event_type *type;
	int i, err = 0;

	for (i = 0; i < event_count; i++) {
		type = &events[i];

		err = perf_event__synthesize_event_type(type->event_id,
							type->name, process,
							session);
		if (err) {
			pr_debug("failed to create perf header event type\n");
			return err;
		}
	}

	return err;
}

int perf_event__process_event_type(union perf_event *event,
				   struct perf_session *session __unused)
{
	if (perf_header__push_event(event->event_type.event_type.event_id,
				    event->event_type.event_type.name) < 0)
		return -ENOMEM;

	return 0;
}

int perf_event__synthesize_tracing_data(int fd, struct perf_evlist *evlist,
					 perf_event__handler_t process,
				   struct perf_session *session __unused)
{
	union perf_event ev;
	ssize_t size = 0, aligned_size = 0, padding;
	int err __used = 0;

	memset(&ev, 0, sizeof(ev));

	ev.tracing_data.header.type = PERF_RECORD_HEADER_TRACING_DATA;
	size = read_tracing_data_size(fd, &evlist->entries);
	if (size <= 0)
		return size;
	aligned_size = ALIGN(size, sizeof(u64));
	padding = aligned_size - size;
	ev.tracing_data.header.size = sizeof(ev.tracing_data);
	ev.tracing_data.size = aligned_size;

	process(&ev, NULL, session);

	err = read_tracing_data(fd, &evlist->entries);
	write_padded(fd, NULL, 0, padding);

	return aligned_size;
}

int perf_event__process_tracing_data(union perf_event *event,
				     struct perf_session *session)
{
	ssize_t size_read, padding, size = event->tracing_data.size;
	off_t offset = lseek(session->fd, 0, SEEK_CUR);
	char buf[BUFSIZ];

	/* setup for reading amidst mmap */
	lseek(session->fd, offset + sizeof(struct tracing_data_event),
	      SEEK_SET);

	size_read = trace_report(session->fd, session->repipe);

	padding = ALIGN(size_read, sizeof(u64)) - size_read;

	if (read(session->fd, buf, padding) < 0)
		die("reading input file");
	if (session->repipe) {
		int retw = write(STDOUT_FILENO, buf, padding);
		if (retw <= 0 || retw != padding)
			die("repiping tracing data padding");
	}

	if (size_read + padding != size)
		die("tracing data size mismatch");

	return size_read + padding;
}

int perf_event__synthesize_build_id(struct dso *pos, u16 misc,
				    perf_event__handler_t process,
				    struct machine *machine,
				    struct perf_session *session)
{
	union perf_event ev;
	size_t len;
	int err = 0;

	if (!pos->hit)
		return err;

	memset(&ev, 0, sizeof(ev));

	len = pos->long_name_len + 1;
	len = ALIGN(len, NAME_ALIGN);
	memcpy(&ev.build_id.build_id, pos->build_id, sizeof(pos->build_id));
	ev.build_id.header.type = PERF_RECORD_HEADER_BUILD_ID;
	ev.build_id.header.misc = misc;
	ev.build_id.pid = machine->pid;
	ev.build_id.header.size = sizeof(ev.build_id) + len;
	memcpy(&ev.build_id.filename, pos->long_name, pos->long_name_len);

	err = process(&ev, NULL, session);

	return err;
}

int perf_event__process_build_id(union perf_event *event,
				 struct perf_session *session)
{
	__event_process_build_id(&event->build_id,
				 event->build_id.filename,
				 session);
	return 0;
}

void disable_buildid_cache(void)
{
	no_buildid_cache = true;
}
