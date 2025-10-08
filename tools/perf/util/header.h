/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_HEADER_H
#define __PERF_HEADER_H

#include <linux/stddef.h>
#include <linux/perf_event.h>
#include <sys/types.h>
#include <stdio.h> // FILE
#include <stdbool.h>
#include <linux/bitmap.h>
#include <linux/types.h>
#include "env.h"
#include <perf/cpumap.h>

struct evlist;
union perf_event;
struct perf_header;
struct perf_session;
struct perf_tool;

enum {
	HEADER_RESERVED		= 0,	/* always cleared */
	HEADER_FIRST_FEATURE	= 1,
	HEADER_TRACING_DATA	= 1,
	HEADER_BUILD_ID,

	HEADER_HOSTNAME,
	HEADER_OSRELEASE,
	HEADER_VERSION,
	HEADER_ARCH,
	HEADER_NRCPUS,
	HEADER_CPUDESC,
	HEADER_CPUID,
	HEADER_TOTAL_MEM,
	HEADER_CMDLINE,
	HEADER_EVENT_DESC,
	HEADER_CPU_TOPOLOGY,
	HEADER_NUMA_TOPOLOGY,
	HEADER_BRANCH_STACK,
	HEADER_PMU_MAPPINGS,
	HEADER_GROUP_DESC,
	HEADER_AUXTRACE,
	HEADER_STAT,
	HEADER_CACHE,
	HEADER_SAMPLE_TIME,
	HEADER_MEM_TOPOLOGY,
	HEADER_CLOCKID,
	HEADER_DIR_FORMAT,
	HEADER_BPF_PROG_INFO,
	HEADER_BPF_BTF,
	HEADER_COMPRESSED,
	HEADER_CPU_PMU_CAPS,
	HEADER_CLOCK_DATA,
	HEADER_HYBRID_TOPOLOGY,
	HEADER_PMU_CAPS,
	HEADER_LAST_FEATURE,
	HEADER_FEAT_BITS	= 256,
};

enum perf_header_version {
	PERF_HEADER_VERSION_1,
	PERF_HEADER_VERSION_2,
};

struct perf_file_section {
	u64 offset;
	u64 size;
};

/**
 * struct perf_file_header: Header representation on disk.
 */
struct perf_file_header {
	/** @magic: Holds "PERFILE2". */
	u64				magic;
	/** @size: Size of this header - sizeof(struct perf_file_header). */
	u64				size;
	/**
	 * @attr_size: Size of attrs entries - sizeof(struct perf_event_attr) +
	 * sizeof(struct perf_file_section).
	 */
	u64				attr_size;
	/** @attrs: Offset and size of file section holding attributes. */
	struct perf_file_section	attrs;
	/** @data: Offset and size of file section holding regular event data. */
	struct perf_file_section	data;
	/** @event_types: Ignored. */
	struct perf_file_section	event_types;
	/**
	 * @adds_features: Bitmap of features. The features are immediately after the data section.
	 */
	DECLARE_BITMAP(adds_features, HEADER_FEAT_BITS);
};

struct perf_pipe_file_header {
	u64				magic;
	u64				size;
};

int perf_file_header__read(struct perf_file_header *header,
			   struct perf_header *ph, int fd);

struct perf_header {
	enum perf_header_version	version;
	bool				needs_swap;
	u64				data_offset;
	u64				data_size;
	u64				feat_offset;
	DECLARE_BITMAP(adds_features, HEADER_FEAT_BITS);
	struct perf_env 	env;
};

struct feat_fd {
	struct perf_header *ph;
	int		   fd;
	void		   *buf;	/* Either buf != NULL or fd >= 0 */
	ssize_t		   offset;
	size_t		   size;
	struct evsel	   *events;
};

struct perf_header_feature_ops {
	int	   (*write)(struct feat_fd *ff, struct evlist *evlist);
	void	   (*print)(struct feat_fd *ff, FILE *fp);
	int	   (*process)(struct feat_fd *ff, void *data);
	const char *name;
	bool	   full_only;
	bool	   synthesize;
};

extern const char perf_version_string[];

int perf_session__read_header(struct perf_session *session);
int perf_session__write_header(struct perf_session *session,
			       struct evlist *evlist,
			       int fd, bool at_exit);
int perf_header__write_pipe(int fd);

/* feat_writer writes a feature section to output */
struct feat_writer {
	int (*write)(struct feat_writer *fw, void *buf, size_t sz);
};

/* feat_copier copies a feature section using feat_writer to output */
struct feat_copier {
	int (*copy)(struct feat_copier *fc, int feat, struct feat_writer *fw);
};

int perf_session__inject_header(struct perf_session *session,
				struct evlist *evlist,
				int fd,
				struct feat_copier *fc,
				bool write_attrs_after_data);

size_t perf_session__data_offset(const struct evlist *evlist);

void perf_header__set_feat(struct perf_header *header, int feat);
void perf_header__clear_feat(struct perf_header *header, int feat);
bool perf_header__has_feat(const struct perf_header *header, int feat);

int perf_header__set_cmdline(int argc, const char **argv);

int perf_header__process_sections(struct perf_header *header, int fd,
				  void *data,
				  int (*process)(struct perf_file_section *section,
				  struct perf_header *ph,
				  int feat, int fd, void *data));

int perf_header__fprintf_info(struct perf_session *s, FILE *fp, bool full);

int perf_event__process_feature(struct perf_session *session,
				union perf_event *event);
int perf_event__process_attr(const struct perf_tool *tool, union perf_event *event,
			     struct evlist **pevlist);
int perf_event__process_event_update(const struct perf_tool *tool,
				     union perf_event *event,
				     struct evlist **pevlist);
size_t perf_event__fprintf_attr(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_event_update(union perf_event *event, FILE *fp);
#ifdef HAVE_LIBTRACEEVENT
int perf_event__process_tracing_data(struct perf_session *session,
				     union perf_event *event);
#endif
int perf_event__process_build_id(struct perf_session *session,
				 union perf_event *event);
bool is_perf_magic(u64 magic);

#define NAME_ALIGN 64

struct feat_fd;

int do_write(struct feat_fd *fd, const void *buf, size_t size);

int write_padded(struct feat_fd *fd, const void *bf,
		 size_t count, size_t count_aligned);

#define MAX_CACHE_LVL 4

int build_caches_for_cpu(u32 cpu, struct cpu_cache_level caches[], u32 *cntp);

/*
 * arch specific callback
 */
int get_cpuid(char *buffer, size_t sz, struct perf_cpu cpu);

char *get_cpuid_str(struct perf_cpu cpu);

char *get_cpuid_allow_env_override(struct perf_cpu cpu);

int strcmp_cpuid_str(const char *s1, const char *s2);
#endif /* __PERF_HEADER_H */
