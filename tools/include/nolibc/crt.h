/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * C Run Time support for NOLIBC
 * Copyright (C) 2023 Zhangjin Wu <falcon@tinylab.org>
 */

#ifndef _NOLIBC_CRT_H
#define _NOLIBC_CRT_H

char **environ __attribute__((weak));
const unsigned long *_auxv __attribute__((weak));

static void __stack_chk_init(void);
static void exit(int);

extern void (*const __preinit_array_start[])(void) __attribute__((weak));
extern void (*const __preinit_array_end[])(void) __attribute__((weak));

extern void (*const __init_array_start[])(void) __attribute__((weak));
extern void (*const __init_array_end[])(void) __attribute__((weak));

extern void (*const __fini_array_start[])(void) __attribute__((weak));
extern void (*const __fini_array_end[])(void) __attribute__((weak));

__attribute__((weak))
void _start_c(long *sp)
{
	long argc;
	char **argv;
	char **envp;
	int exitcode;
	void (* const *func)(void);
	const unsigned long *auxv;
	/* silence potential warning: conflicting types for 'main' */
	int _nolibc_main(int, char **, char **) __asm__ ("main");

	/* initialize stack protector */
	__stack_chk_init();

	/*
	 * sp  :    argc          <-- argument count, required by main()
	 * argv:    argv[0]       <-- argument vector, required by main()
	 *          argv[1]
	 *          ...
	 *          argv[argc-1]
	 *          null
	 * environ: environ[0]    <-- environment variables, required by main() and getenv()
	 *          environ[1]
	 *          ...
	 *          null
	 * _auxv:   _auxv[0]      <-- auxiliary vector, required by getauxval()
	 *          _auxv[1]
	 *          ...
	 *          null
	 */

	/* assign argc and argv */
	argc = *sp;
	argv = (void *)(sp + 1);

	/* find environ */
	environ = envp = argv + argc + 1;

	/* find _auxv */
	for (auxv = (void *)envp; *auxv++;)
		;
	_auxv = auxv;

	for (func = __preinit_array_start; func < __preinit_array_end; func++)
		(*func)();
	for (func = __init_array_start; func < __init_array_end; func++)
		(*func)();

	/* go to application */
	exitcode = _nolibc_main(argc, argv, envp);

	for (func = __fini_array_end; func > __fini_array_start;)
		(*--func)();

	exit(exitcode);
}

#endif /* _NOLIBC_CRT_H */
