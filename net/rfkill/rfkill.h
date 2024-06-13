/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007 Ivo van Doorn
 * Copyright 2009 Johannes Berg <johannes@sipsolutions.net>
 */


#ifndef __RFKILL_INPUT_H
#define __RFKILL_INPUT_H

/* core code */
void rfkill_switch_all(const enum rfkill_type type, bool blocked);
void rfkill_epo(void);
void rfkill_restore_states(void);
void rfkill_remove_epo_lock(void);
bool rfkill_is_epo_lock_active(void);
bool rfkill_get_global_sw_state(const enum rfkill_type type);

/* input handler */
int rfkill_handler_init(void);
void rfkill_handler_exit(void);

#endif /* __RFKILL_INPUT_H */
