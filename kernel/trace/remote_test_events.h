/* SPDX-License-Identifier: GPL-2.0 */

#define REMOTE_TEST_EVENT_ID 1

REMOTE_EVENT(selftest, REMOTE_TEST_EVENT_ID,
	RE_STRUCT(
		re_field(u64, id)
	),
	RE_PRINTK("id=%llu", __entry->id)
);
