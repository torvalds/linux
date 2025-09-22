/*
 * popen3.h -- execute a command and connect stdin, stdout and stderr
 *
 * Copyright (c) 2019, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#ifndef POPEN3_H
#define POPEN3_H

#include <stdio.h>
#include <sys/types.h>

/*
 * Execute a command and connect stdin, stdout and stderr of the process to
 * respectively finptr, foutptr and ferrptr if non-NULL. The process
 * identifier of the new process is returned on success and the pointers to
 * the FILE handles will have been set. On failure, -1 is returned and none
 * of the pointers will have been set.
 */
pid_t popen3(char *const *command,
             int *fdinptr,
             int *fdoutptr,
             int *fderrptr);

#endif /* POPEN3_H */
