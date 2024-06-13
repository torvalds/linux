// SPDX-License-Identifier: GPL-2.0
#ifndef PERF_COPYFILE_H_
#define PERF_COPYFILE_H_

#include <linux/types.h>
#include <sys/types.h>
#include <fcntl.h>

struct nsinfo;

int copyfile(const char *from, const char *to);
int copyfile_mode(const char *from, const char *to, mode_t mode);
int copyfile_ns(const char *from, const char *to, struct nsinfo *nsi);
int copyfile_offset(int ifd, loff_t off_in, int ofd, loff_t off_out, u64 size);

#endif // PERF_COPYFILE_H_
