#ifndef __PERF_SIGCHAIN_H
#define __PERF_SIGCHAIN_H

typedef void (*sigchain_fun)(int);

int sigchain_push(int sig, sigchain_fun f);
int sigchain_pop(int sig);

void sigchain_push_common(sigchain_fun f);

#endif /* __PERF_SIGCHAIN_H */
