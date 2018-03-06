// SPDX-License-Identifier: GPL-2.0+

/*
 * Part of fork context switch microbenchmark.
 *
 * Copyright 2018, Anton Blanchard, IBM Corp.
 */

void _exit(int);
void _start(void)
{
	_exit(0);
}
