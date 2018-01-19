/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BENCH_H
#define BENCH_H

/*
 * The madvise transparent hugepage constants were added in glibc
 * 2.13. For compatibility with older versions of glibc, define these
 * tokens if they are not already defined.
 *
 * PA-RISC uses different madvise values from other architectures and
 * needs to be special-cased.
 */
#ifdef __hppa__
# ifndef MADV_HUGEPAGE
#  define MADV_HUGEPAGE		67
# endif
# ifndef MADV_NOHUGEPAGE
#  define MADV_NOHUGEPAGE	68
# endif
#else
# ifndef MADV_HUGEPAGE
#  define MADV_HUGEPAGE		14
# endif
# ifndef MADV_NOHUGEPAGE
#  define MADV_NOHUGEPAGE	15
# endif
#endif

int bench_numa(int argc, const char **argv);
int bench_sched_messaging(int argc, const char **argv);
int bench_sched_pipe(int argc, const char **argv);
int bench_mem_memcpy(int argc, const char **argv);
int bench_mem_memset(int argc, const char **argv);
int bench_futex_hash(int argc, const char **argv);
int bench_futex_wake(int argc, const char **argv);
int bench_futex_wake_parallel(int argc, const char **argv);
int bench_futex_requeue(int argc, const char **argv);
/* pi futexes */
int bench_futex_lock_pi(int argc, const char **argv);

#define BENCH_FORMAT_DEFAULT_STR	"default"
#define BENCH_FORMAT_DEFAULT		0
#define BENCH_FORMAT_SIMPLE_STR		"simple"
#define BENCH_FORMAT_SIMPLE		1

#define BENCH_FORMAT_UNKNOWN		-1

extern int bench_format;
extern unsigned int bench_repeat;

#endif
