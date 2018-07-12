/*
 * Interface to booleans in the security server. This is exported
 * for the selinuxfs.
 *
 * Author: Karl MacMillan <kmacmillan@tresys.com>
 *
 * Copyright (C) 2003 - 2004 Tresys Technology, LLC
 *	This program is free software; you can redistribute it and/or modify
 *  	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 */

#ifndef _SELINUX_CONDITIONAL_H_
#define _SELINUX_CONDITIONAL_H_

#include "security.h"

int security_get_bools(struct selinux_state *state,
		       int *len, char ***names, int **values);

int security_set_bools(struct selinux_state *state,
		       int len, int *values);

int security_get_bool_value(struct selinux_state *state,
			    int index);

#endif
