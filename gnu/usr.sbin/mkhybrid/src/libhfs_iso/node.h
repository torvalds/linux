/*
 * hfsutils - tools for reading and writing Macintosh HFS volumes
 * Copyright (C) 1996, 1997 Robert Leslie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

void n_init(node *, btree *, int, int);

int n_new(node *);
void n_free(node *);

void n_compact(node *);
int n_search(node *, unsigned char *);

void n_index(btree *, unsigned char *, unsigned long, unsigned char *, int *);
int n_split(node *, unsigned char *, int *);

void n_insertx(node *, unsigned char *, int);
int n_insert(node *, unsigned char *, int *);

int n_merge(node *, node *, unsigned char *, int *);
int n_delete(node *, unsigned char *, int *);
