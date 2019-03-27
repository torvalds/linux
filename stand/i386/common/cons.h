/*-
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 *
 * $FreeBSD$
 */

#ifndef _CONS_H_
#define	_CONS_H_

#define IO_KEYBOARD	1
#define IO_SERIAL	2

extern uint8_t ioctrl;

void putc(int c);
void xputc(int c);
void putchar(int c);
int getc(int fn);
int xgetc(int fn);
int getchar(void);
int keyhit(unsigned int secs);
void getstr(char *cmdstr, size_t cmdstrsize);

#endif	/* !_CONS_H_ */
