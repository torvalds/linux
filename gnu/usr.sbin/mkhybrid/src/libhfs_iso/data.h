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

char d_getb(unsigned char *);
short d_getw(unsigned char *);
long d_getl(unsigned char *);

void d_putb(unsigned char *, char);
void d_putw(unsigned char *, short);
void d_putl(unsigned char *, long);

void d_fetchb(unsigned char **, char *);
void d_fetchw(unsigned char **, short *);
void d_fetchl(unsigned char **, long *);
void d_fetchs(unsigned char **, char *, int);

void d_storeb(unsigned char **, char);
void d_storew(unsigned char **, short);
void d_storel(unsigned char **, long);
void d_stores(unsigned char **, char *, int);

unsigned long d_tomtime(unsigned long);
unsigned long d_toutime(unsigned long);

int d_relstring(char *, char *);
