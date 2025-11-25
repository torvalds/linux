/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef _LINUX_LUO_INTERNAL_H
#define _LINUX_LUO_INTERNAL_H

#include <linux/liveupdate.h>
#include <linux/uaccess.h>

struct luo_ucmd {
	void __user *ubuffer;
	u32 user_size;
	void *cmd;
};

static inline int luo_ucmd_respond(struct luo_ucmd *ucmd,
				   size_t kernel_cmd_size)
{
	/*
	 * Copy the minimum of what the user provided and what we actually
	 * have.
	 */
	if (copy_to_user(ucmd->ubuffer, ucmd->cmd,
			 min_t(size_t, ucmd->user_size, kernel_cmd_size))) {
		return -EFAULT;
	}
	return 0;
}

/*
 * Handles a deserialization failure: devices and memory is in unpredictable
 * state.
 *
 * Continuing the boot process after a failure is dangerous because it could
 * lead to leaks of private data.
 */
#define luo_restore_fail(__fmt, ...) panic(__fmt, ##__VA_ARGS__)

/**
 * struct luo_session - Represents an active or incoming Live Update session.
 * @name:       A unique name for this session, used for identification and
 *              retrieval.
 * @ser:        Pointer to the serialized data for this session.
 * @list:       A list_head member used to link this session into a global list
 *              of either outgoing (to be preserved) or incoming (restored from
 *              previous kernel) sessions.
 * @retrieved:  A boolean flag indicating whether this session has been
 *              retrieved by a consumer in the new kernel.
 * @mutex:      protects fields in the luo_session.
 */
struct luo_session {
	char name[LIVEUPDATE_SESSION_NAME_LENGTH];
	struct luo_session_ser *ser;
	struct list_head list;
	bool retrieved;
	struct mutex mutex;
};

int luo_session_create(const char *name, struct file **filep);
int luo_session_retrieve(const char *name, struct file **filep);
int __init luo_session_setup_outgoing(void *fdt);
int __init luo_session_setup_incoming(void *fdt);
int luo_session_serialize(void);
int luo_session_deserialize(void);
bool luo_session_quiesce(void);
void luo_session_resume(void);

#endif /* _LINUX_LUO_INTERNAL_H */
