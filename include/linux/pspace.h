#ifndef _LINUX_PSPACE_H
#define _LINUX_PSPACE_H

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <linux/pid.h>

struct pidmap {
       atomic_t nr_free;
       void *page;
};

#define PIDMAP_ENTRIES         ((PID_MAX_LIMIT + 8*PAGE_SIZE - 1)/PAGE_SIZE/8)

struct pspace {
       struct pidmap pidmap[PIDMAP_ENTRIES];
       int last_pid;
};

extern struct pspace init_pspace;

#endif /* _LINUX_PSPACE_H */
