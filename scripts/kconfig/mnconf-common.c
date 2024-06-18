// SPDX-License-Identifier: GPL-2.0-only
#include "expr.h"
#include "list.h"
#include "mnconf-common.h"

int jump_key_char;

int next_jump_key(int key)
{
	if (key < '1' || key > '9')
		return '1';

	key++;

	if (key > '9')
		key = '1';

	return key;
}

int handle_search_keys(int key, size_t start, size_t end, void *_data)
{
	struct search_data *data = _data;
	struct jump_key *pos;
	int index = 0;

	if (key < '1' || key > '9')
		return 0;

	list_for_each_entry(pos, data->head, entries) {
		index = next_jump_key(index);

		if (pos->offset < start)
			continue;

		if (pos->offset >= end)
			break;

		if (key == index) {
			data->target = pos->target;
			return 1;
		}
	}

	return 0;
}

int get_jump_key_char(void)
{
	jump_key_char = next_jump_key(jump_key_char);

	return jump_key_char;
}
