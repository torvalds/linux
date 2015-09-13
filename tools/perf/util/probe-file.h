#ifndef __PROBE_FILE_H
#define __PROBE_FILE_H

#include "strlist.h"
#include "strfilter.h"
#include "probe-event.h"

#define PF_FL_UPROBE	1
#define PF_FL_RW	2

int probe_file__open(int flag);
int probe_file__open_both(int *kfd, int *ufd, int flag);
struct strlist *probe_file__get_namelist(int fd);
struct strlist *probe_file__get_rawlist(int fd);
int probe_file__add_event(int fd, struct probe_trace_event *tev);
int probe_file__del_events(int fd, struct strfilter *filter);

#endif
