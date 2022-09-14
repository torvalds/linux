/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOPDOWN_H
#define _TOPDOWN_H 1

bool topdown_sys_has_perf_metrics(void);
int topdown_parse_events(struct evlist *evlist);

#endif
