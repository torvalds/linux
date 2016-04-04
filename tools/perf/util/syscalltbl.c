/*
 * System call table mapper
 *
 * (C) 2016 Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "syscalltbl.h"
#include <string.h>
#include <libaudit.h>


struct syscalltbl *syscalltbl__new(void)
{
	struct syscalltbl *tbl = malloc(sizeof(*tbl));
	if (tbl) {
		tbl->audit_machine = audit_detect_machine();
	}
	return tbl;
}

void syscalltbl__delete(struct syscalltbl *tbl)
{
	free(tbl);
}

const char *syscalltbl__name(const struct syscalltbl *tbl, int id)
{
	return audit_syscall_to_name(id, tbl->audit_machine);
}

int syscalltbl__id(struct syscalltbl *tbl, const char *name)
{
	return audit_name_to_syscall(name, tbl->audit_machine);
}
