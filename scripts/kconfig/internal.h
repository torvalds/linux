/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef INTERNAL_H
#define INTERNAL_H

struct menu;

extern struct menu *current_menu, *current_entry;

extern const char *cur_filename;
extern int cur_lineno;

#endif /* INTERNAL_H */
