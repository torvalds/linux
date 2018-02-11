/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __JIT_H__
#define __JIT_H__

#include <data.h>

int jit_process(struct perf_session *session, struct perf_data *output,
		struct machine *machine, char *filename, pid_t pid, u64 *nbytes);

int jit_inject_record(const char *filename);

#endif /* __JIT_H__ */
