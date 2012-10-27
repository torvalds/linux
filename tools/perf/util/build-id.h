#ifndef PERF_BUILD_ID_H_
#define PERF_BUILD_ID_H_ 1

#include "session.h"

extern struct perf_tool build_id__mark_dso_hit_ops;

int build_id__sprintf(const u8 *build_id, int len, char *bf);
char *dso__build_id_filename(struct dso *self, char *bf, size_t size);

int build_id__mark_dso_hit(struct perf_tool *tool, union perf_event *event,
			   struct perf_sample *sample, struct perf_evsel *evsel,
			   struct machine *machine);

#endif
