/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUBCMD_SIGCHAIN_H
#define __SUBCMD_SIGCHAIN_H

typedef void (*sigchain_fun)(int);

int sigchain_pop(int sig);

void sigchain_push_common(sigchain_fun f);

#endif /* __SUBCMD_SIGCHAIN_H */
