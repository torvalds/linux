/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DATA_CONVERT_H
#define __DATA_CONVERT_H

#include <stdbool.h>

struct perf_data_convert_opts {
	bool force;
	bool all;
	bool tod;
};

#ifdef HAVE_LIBBABELTRACE_SUPPORT
int bt_convert__perf2ctf(const char *input_name, const char *to_ctf,
			 struct perf_data_convert_opts *opts);
#endif /* HAVE_LIBBABELTRACE_SUPPORT */

int bt_convert__perf2json(const char *input_name, const char *to_ctf,
			 struct perf_data_convert_opts *opts);

#endif /* __DATA_CONVERT_H */
