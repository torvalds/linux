/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SYSCALLTBL_H
#define __PERF_SYSCALLTBL_H

struct syscalltbl {
	int audit_machine;
	struct {
		int max_id;
		int nr_entries;
		void *entries;
	} syscalls;
};

struct syscalltbl *syscalltbl__new(void);
void syscalltbl__delete(struct syscalltbl *tbl);

const char *syscalltbl__name(const struct syscalltbl *tbl, int id);
int syscalltbl__id(struct syscalltbl *tbl, const char *name);

int syscalltbl__strglobmatch_first(struct syscalltbl *tbl, const char *syscall_glob, int *idx);
int syscalltbl__strglobmatch_next(struct syscalltbl *tbl, const char *syscall_glob, int *idx);

#endif /* __PERF_SYSCALLTBL_H */
