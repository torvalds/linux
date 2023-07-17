/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TOPDOWN_H
#define TOPDOWN_H 1

#include <stdbool.h>

struct evsel;

bool arch_topdown_sample_read(struct evsel *leader);

#endif
