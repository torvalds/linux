/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_EVLIST_HYBRID_H
#define __PERF_EVLIST_HYBRID_H

#include <linux/compiler.h>
#include <linux/kernel.h>
#include "evlist.h"
#include <unistd.h>

bool evlist__has_hybrid(struct evlist *evlist);

#endif /* __PERF_EVLIST_HYBRID_H */
