/* Internal credentials stuff
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

/*
 * user.c
 */
static inline void sched_switch_user(struct task_struct *p)
{
#ifdef CONFIG_USER_SCHED
	sched_move_task(p);
#endif	/* CONFIG_USER_SCHED */
}

