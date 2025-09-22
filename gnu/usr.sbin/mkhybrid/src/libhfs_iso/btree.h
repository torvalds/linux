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

int bt_getnode(node *);
int bt_putnode(node *);

int bt_readhdr(btree *);
int bt_writehdr(btree *);

int bt_space(btree *, unsigned int);

int bt_insertx(node *, unsigned char *, int *);
int bt_insert(btree *, unsigned char *, int);

int bt_deletex(node *, unsigned char *, unsigned char *, int *);
int bt_delete(btree *, unsigned char *);

int bt_search(btree *, unsigned char *, node *);
