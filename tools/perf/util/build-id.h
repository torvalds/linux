#ifndef PERF_BUILD_ID_H_
#define PERF_BUILD_ID_H_ 1

#define BUILD_ID_SIZE 20

#include "tool.h"
#include <linux/types.h>

extern struct perf_tool build_id__mark_dso_hit_ops;
struct dso;

int build_id__sprintf(const u8 *build_id, int len, char *bf);
char *dso__build_id_filename(const struct dso *dso, char *bf, size_t size);

int build_id__mark_dso_hit(struct perf_tool *tool, union perf_event *event,
			   struct perf_sample *sample, struct perf_evsel *evsel,
			   struct machine *machine);

int dsos__hit_all(struct perf_session *session);

bool perf_session__read_build_ids(struct perf_session *session, bool with_hits);
int perf_session__write_buildid_table(struct perf_session *session, int fd);
int perf_session__cache_build_ids(struct perf_session *session);

int build_id_cache__add_s(const char *sbuild_id, const char *debugdir,
			  const char *name, bool is_kallsyms, bool is_vdso);
int build_id_cache__remove_s(const char *sbuild_id, const char *debugdir);
void disable_buildid_cache(void);

#endif
