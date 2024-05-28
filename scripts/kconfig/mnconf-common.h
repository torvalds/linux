/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MNCONF_COMMON_H
#define MNCONF_COMMON_H

#include <stddef.h>

struct search_data {
	struct list_head *head;
	struct menu *target;
};

extern int jump_key_char;

int next_jump_key(int key);
int handle_search_keys(int key, size_t start, size_t end, void *_data);
int get_jump_key_char(void);

#endif /* MNCONF_COMMON_H */
