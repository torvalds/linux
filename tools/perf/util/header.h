/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_HEADER_H
#define __PERF_HEADER_H

#include <linux/perf_event.h>
#include <sys/types.h>
#include <stdbool.h>
#include <linux/bitmap.h>
#include <linux/types.h>
#include "event.h"
#include "env.h"
#include "pmu.h"

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

struct perf_file_header {
	u64				magic;
	u64				size;
	u64				attr_size;
	struct perf_file_section	attrs;
	struct perf_file_section	data;
	/* event_types is ignored */
	struct perf_file_section	event_types;
	DECLARE_BITMAP(adds_features, HEADER_FEAT_BITS);
};

struct perf_pipe_file_header {
	u64				magic;
	u64				size;
};

struct perf_header;

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

struct perf_evlist;
struct perf_session;

int perf_session__read_header(struct perf_session *session);
int perf_session__write_header(struct perf_session *session,
			       struct perf_evlist *evlist,
			       int fd, bool at_exit);
int perf_header__write_pipe(int fd);

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

int perf_event__synthesize_features(struct perf_tool *tool,
				    struct perf_session *session,
				    struct perf_evlist *evlist,
				    perf_event__handler_t process);

int perf_event__synthesize_extra_attr(struct perf_tool *tool,
				      struct perf_evlist *evsel_list,
				      perf_event__handler_t process,
				      bool is_pipe);

int perf_event__process_feature(struct perf_tool *tool,
				union perf_event *event,
				struct perf_session *session);

int perf_event__synthesize_attr(struct perf_tool *tool,
				struct perf_event_attr *attr, u32 ids, u64 *id,
				perf_event__handler_t process);
int perf_event__synthesize_attrs(struct perf_tool *tool,
				 struct perf_session *session,
				 perf_event__handler_t process);
int perf_event__synthesize_event_update_unit(struct perf_tool *tool,
					     struct perf_evsel *evsel,
					     perf_event__handler_t process);
int perf_event__synthesize_event_update_scale(struct perf_tool *tool,
					      struct perf_evsel *evsel,
					      perf_event__handler_t process);
int perf_event__synthesize_event_update_name(struct perf_tool *tool,
					     struct perf_evsel *evsel,
					     perf_event__handler_t process);
int perf_event__synthesize_event_update_cpus(struct perf_tool *tool,
					     struct perf_evsel *evsel,
					     perf_event__handler_t process);
int perf_event__process_attr(struct perf_tool *tool, union perf_event *event,
			     struct perf_evlist **pevlist);
int perf_event__process_event_update(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_evlist **pevlist);
size_t perf_event__fprintf_event_update(union perf_event *event, FILE *fp);

int perf_event__synthesize_tracing_data(struct perf_tool *tool,
					int fd, struct perf_evlist *evlist,
					perf_event__handler_t process);
int perf_event__process_tracing_data(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_session *session);

int perf_event__synthesize_build_id(struct perf_tool *tool,
				    struct dso *pos, u16 misc,
				    perf_event__handler_t process,
				    struct machine *machine);
int perf_event__process_build_id(struct perf_tool *tool,
				 union perf_event *event,
				 struct perf_session *session);
bool is_perf_magic(u64 magic);

#define NAME_ALIGN 64

struct feat_fd;

int do_write(struct feat_fd *fd, const void *buf, size_t size);

int write_padded(struct feat_fd *fd, const void *bf,
		 size_t count, size_t count_aligned);

/*
 * arch specific callback
 */
int get_cpuid(char *buffer, size_t sz);

char *get_cpuid_str(struct perf_pmu *pmu __maybe_unused);
#endif /* __PERF_HEADER_H */
