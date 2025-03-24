// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/fs.h>

struct file *rust_helper_get_file(struct file *f)
{
	return get_file(f);
}
