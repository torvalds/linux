#ifndef __PERF_HEADER_H
#define __PERF_HEADER_H

#include "../../../include/linux/perf_event.h"
#include <sys/types.h>
#include <stdbool.h>
#include "types.h"
#include "event.h"

#include <linux/bitmap.h>

struct perf_header_attr {
	struct perf_event_attr attr;
	int ids, size;
	u64 *id;
	off_t id_offset;
};

enum {
	HEADER_TRACE_INFO = 1,
	HEADER_BUILD_ID,
	HEADER_LAST_FEATURE,
};

#define HEADER_FEAT_BITS			256

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
	struct perf_file_section	event_types;
	DECLARE_BITMAP(adds_features, HEADER_FEAT_BITS);
};

struct perf_pipe_file_header {
	u64				magic;
	u64				size;
};

struct perf_header;

int perf_file_header__read(struct perf_file_header *self,
			   struct perf_header *ph, int fd);

struct perf_header {
	int			frozen;
	int			attrs, size;
	bool			needs_swap;
	struct perf_header_attr **attr;
	s64			attr_offset;
	u64			data_offset;
	u64			data_size;
	u64			event_offset;
	u64			event_size;
	DECLARE_BITMAP(adds_features, HEADER_FEAT_BITS);
};

int perf_header__init(struct perf_header *self);
void perf_header__exit(struct perf_header *self);

int perf_header__read(struct perf_session *session, int fd);
int perf_header__write(struct perf_header *self, int fd, bool at_exit);
int perf_header__write_pipe(int fd);

int perf_header__add_attr(struct perf_header *self,
			  struct perf_header_attr *attr);

int perf_header__push_event(u64 id, const char *name);
char *perf_header__find_event(u64 id);

struct perf_header_attr *perf_header_attr__new(struct perf_event_attr *attr);
void perf_header_attr__delete(struct perf_header_attr *self);

int perf_header_attr__add_id(struct perf_header_attr *self, u64 id);

u64 perf_header__sample_type(struct perf_header *header);
bool perf_header__sample_id_all(const struct perf_header *header);
struct perf_event_attr *
perf_header__find_attr(u64 id, struct perf_header *header);
void perf_header__set_feat(struct perf_header *self, int feat);
void perf_header__clear_feat(struct perf_header *self, int feat);
bool perf_header__has_feat(const struct perf_header *self, int feat);

int perf_header__process_sections(struct perf_header *self, int fd,
				  int (*process)(struct perf_file_section *self,
						 struct perf_header *ph,
						 int feat, int fd));

int build_id_cache__add_s(const char *sbuild_id, const char *debugdir,
			  const char *name, bool is_kallsyms);
int build_id_cache__remove_s(const char *sbuild_id, const char *debugdir);

int event__synthesize_attr(struct perf_event_attr *attr, u16 ids, u64 *id,
			   event__handler_t process,
			   struct perf_session *session);
int event__synthesize_attrs(struct perf_header *self,
			    event__handler_t process,
			    struct perf_session *session);
int event__process_attr(event_t *self, struct perf_session *session);

int event__synthesize_event_type(u64 event_id, char *name,
				 event__handler_t process,
				 struct perf_session *session);
int event__synthesize_event_types(event__handler_t process,
				  struct perf_session *session);
int event__process_event_type(event_t *self,
			      struct perf_session *session);

int event__synthesize_tracing_data(int fd, struct perf_event_attr *pattrs,
				   int nb_events,
				   event__handler_t process,
				   struct perf_session *session);
int event__process_tracing_data(event_t *self,
				struct perf_session *session);

int event__synthesize_build_id(struct dso *pos, u16 misc,
			       event__handler_t process,
			       struct machine *machine,
			       struct perf_session *session);
int event__process_build_id(event_t *self, struct perf_session *session);

#endif /* __PERF_HEADER_H */
