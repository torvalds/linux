/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef __TEE_DEBUGFS_H__
#define __TEE_DEBUGFS_H__

struct tee;

void tee_create_debug_dir(struct tee *tee);
void tee_delete_debug_dir(struct tee *tee);

void __init tee_init_debugfs(void);
void __exit tee_exit_debugfs(void);

#endif /* __TEE_DEBUGFS_H__ */
