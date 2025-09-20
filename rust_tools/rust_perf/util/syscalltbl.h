/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SYSCALLTBL_H
#define __PERF_SYSCALLTBL_H

const char *syscalltbl__name(int e_machine, int id);
int syscalltbl__id(int e_machine, const char *name);
int syscalltbl__num_idx(int e_machine);
int syscalltbl__id_at_idx(int e_machine, int idx);

int syscalltbl__strglobmatch_first(int e_machine, const char *syscall_glob, int *idx);
int syscalltbl__strglobmatch_next(int e_machine, const char *syscall_glob, int *idx);

#endif /* __PERF_SYSCALLTBL_H */
