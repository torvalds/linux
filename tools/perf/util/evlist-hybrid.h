/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_EVLIST_HYBRID_H
#define __PERF_EVLIST_HYBRID_H

#include <linux/compiler.h>
#include <linux/kernel.h>
#include "evlist.h"
#include <unistd.h>

int evlist__add_default_hybrid(struct evlist *evlist, bool precise);
void evlist__warn_hybrid_group(struct evlist *evlist);
bool evlist__has_hybrid(struct evlist *evlist);

#endif /* __PERF_EVLIST_HYBRID_H */
