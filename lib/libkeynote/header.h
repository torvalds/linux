/* $OpenBSD: header.h,v 1.11 2021/01/18 00:53:20 mortimer Exp $ */
/*
 * The author of this code is Angelos D. Keromytis (angelos@dsl.cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Philadelphia, PA, USA,
 * in April-May 1998
 *
 * Copyright (C) 1998, 1999 by Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, THE AUTHORS MAKES NO
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#ifndef _HEADER_H_
#define _HEADER_H_

/* Functions */
extern void keynote_sign(int, char **), keynote_sigver(int, char **);
extern void keynote_verify(int, char **), keynote_keygen(int, char **);
extern void print_key(FILE *, char *, char *, int, int);
extern void print_space(FILE *, int);
extern int read_environment(char *);
extern void parse_key(char *);
extern int kvparse(void), kvlex(void);
extern void kverror(char *);

/* Variables */
extern int sessid;

/* Defines */
#define SEED_LEN        40
#define DEFAULT_PUBLIC  0x10001

#define KEY_PRINT_OFFSET      12
#define KEY_PRINT_LENGTH      50

#define SIG_PRINT_OFFSET      12
#define SIG_PRINT_LENGTH      50
#endif /* _HEADER_H_ */
