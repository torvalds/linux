// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 *
 * Example for symbol pointers. When compiled with Clang, gendwarfkyms
 * uses a symbol pointer for `f`.
 *
 * $ clang -g -c examples/symbolptr.c -o examples/symbolptr.o
 * $ echo -e "f\ng\np" | ./gendwarfksyms -d examples/symbolptr.o
 */

/* Kernel macros for userspace testing. */
#ifndef __used
#define __used __attribute__((__used__))
#endif
#ifndef __section
#define __section(section) __attribute__((__section__(section)))
#endif

#define __GENDWARFKSYMS_EXPORT(sym)				\
	static typeof(sym) *__gendwarfksyms_ptr_##sym __used	\
		__section(".discard.gendwarfksyms") = &sym;

extern void f(unsigned int arg);
void g(int *arg);
void g(int *arg) {}

struct s;
extern struct s *p;

__GENDWARFKSYMS_EXPORT(f);
__GENDWARFKSYMS_EXPORT(g);
__GENDWARFKSYMS_EXPORT(p);
