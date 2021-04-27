/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_EVLIST_HYBRID_H
#define __PERF_EVLIST_HYBRID_H

#include <linux/compiler.h>
#include <linux/kernel.h>
#include "evlist.h"
#include <unistd.h>

int evlist__add_default_hybrid(struct evlist *evlist, bool precise);

#endif /* __PERF_EVLIST_HYBRID_H */
