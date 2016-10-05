/*
 * Copyright 2016, Chris Smart, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Declarations for common code for copy, copy_first, paste and
 * paste_last unaligned tests.
 *
 */

#ifndef _SELFTESTS_POWERPC_COPY_PASTE_H
#define _SELFTESTS_POWERPC_COPY_PASTE_H

#include <signal.h>

int main(int argc, char *argv[]);
void signal_action_handler(int signal_num, siginfo_t *info, void *ptr);
void setup_signal_handler(void);
extern char cacheline_buf[128] __cacheline_aligned;
extern unsigned int expected_instruction;
extern unsigned int instruction_mask;

#endif /* _SELFTESTS_POWERPC_COPY_PASTE_H */
