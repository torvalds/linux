/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/* Copyright (c) 2021 Facebook */
#ifndef __LIBBPF_STRSET_H
#define __LIBBPF_STRSET_H

#include <stdbool.h>
#include <stddef.h>

struct strset;

struct strset *strset__new(size_t max_data_sz, const char *init_data, size_t init_data_sz);
void strset__free(struct strset *set);

const char *strset__data(const struct strset *set);
size_t strset__data_size(const struct strset *set);

int strset__find_str(struct strset *set, const char *s);
int strset__add_str(struct strset *set, const char *s);

#endif /* __LIBBPF_STRSET_H */
