/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DATA_CONVERT_BT_H
#define __DATA_CONVERT_BT_H
#include "data-convert.h"
#ifdef HAVE_LIBBABELTRACE_SUPPORT

int bt_convert__perf2ctf(const char *input_name, const char *to_ctf,
			 struct perf_data_convert_opts *opts);

#endif /* HAVE_LIBBABELTRACE_SUPPORT */
#endif /* __DATA_CONVERT_BT_H */
