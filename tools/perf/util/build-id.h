/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_BUILD_ID_H_
#define PERF_BUILD_ID_H_ 1

#define BUILD_ID_SIZE	20 /* SHA-1 length in bytes */
#define BUILD_ID_MIN_SIZE	16 /* MD5/UUID/GUID length in bytes */
#define SBUILD_ID_SIZE	(BUILD_ID_SIZE * 2 + 1)
#define SBUILD_ID_MIN_SIZE	(BUILD_ID_MIN_SIZE * 2 + 1)

#include "machine.h"
#include "tool.h"
#include <linux/types.h>

struct build_id {
	u8	data[BUILD_ID_SIZE];
	size_t	size;
};

struct nsinfo;

extern struct perf_tool build_id__mark_dso_hit_ops;
struct dso;
struct feat_fd;

void build_id__init(struct build_id *bid, const u8 *data, size_t size);
int build_id__sprintf(const struct build_id *build_id, char *bf);
bool build_id__is_defined(const struct build_id *bid);
int sysfs__sprintf_build_id(const char *root_dir, char *sbuild_id);
int filename__sprintf_build_id(const char *pathname, char *sbuild_id);
char *build_id_cache__kallsyms_path(const char *sbuild_id, char *bf,
				    size_t size);

char *dso__build_id_filename(const struct dso *dso, char *bf, size_t size,
			     bool is_debug);
char *__dso__build_id_filename(const struct dso *dso, char *bf, size_t size,
			       bool is_debug, bool is_kallsyms);

int build_id__mark_dso_hit(struct perf_tool *tool, union perf_event *event,
			   struct perf_sample *sample, struct evsel *evsel,
			   struct machine *machine);

int dsos__hit_all(struct perf_session *session);

int perf_event__inject_buildid(struct perf_tool *tool, union perf_event *event,
			       struct perf_sample *sample, struct evsel *evsel,
			       struct machine *machine);

bool perf_session__read_build_ids(struct perf_session *session, bool with_hits);
int perf_session__write_buildid_table(struct perf_session *session,
				      struct feat_fd *fd);
int perf_session__cache_build_ids(struct perf_session *session);
int __perf_session__cache_build_ids(struct perf_session *session,
				    machine__dso_t fn, void *priv);

char *build_id_cache__origname(const char *sbuild_id);
char *build_id_cache__linkname(const char *sbuild_id, char *bf, size_t size);
char *build_id_cache__cachedir(const char *sbuild_id, const char *name,
			       struct nsinfo *nsi, bool is_kallsyms,
			       bool is_vdso);

struct strlist;

struct strlist *build_id_cache__list_all(bool validonly);
char *build_id_cache__complement(const char *incomplete_sbuild_id);
int build_id_cache__list_build_ids(const char *pathname, struct nsinfo *nsi,
				   struct strlist **result);
bool build_id_cache__cached(const char *sbuild_id);
int build_id_cache__add(const char *sbuild_id, const char *name, const char *realname,
			struct nsinfo *nsi, bool is_kallsyms, bool is_vdso);
int build_id_cache__add_s(const char *sbuild_id,
			  const char *name, struct nsinfo *nsi,
			  bool is_kallsyms, bool is_vdso);
int build_id_cache__remove_s(const char *sbuild_id);

extern char buildid_dir[];

void set_buildid_dir(const char *dir);
void disable_buildid_cache(void);

#endif
