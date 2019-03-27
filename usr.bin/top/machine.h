/*
 * $FreeBSD$
 */

/*
 *  This file defines the interface between top and the machine-dependent
 *  module.  It is NOT machine dependent and should not need to be changed
 *  for any specific machine.
 */
#ifndef MACHINE_H
#define MACHINE_H

#include <sys/time.h>
#include <sys/types.h>

#define NUM_AVERAGES    3

/* Log base 2 of 1024 is 10 (2^10 == 1024) */
#define LOG1024		10

/*
 * the statics struct is filled in by machine_init
 */
struct statics
{
    const char * const *procstate_names;
    const char * const *cpustate_names;
    const char * const *memory_names;
    const char * const *arc_names;
    const char * const *carc_names;
    const char * const *swap_names;
    const char * const *order_names;
    int ncpus;
};

/*
 * the system_info struct is filled in by a machine dependent routine.
 */

struct system_info
{
    int    last_pid;
    double load_avg[NUM_AVERAGES];
    int    p_total;
    int    p_pactive;     /* number of procs considered "active" */
    int    *procstates;
    int    *cpustates;
    int    *memory;
    int    *arc;
    int    *carc;
    int    *swap;
    struct timeval boottime;
    int    ncpus;
};

/*
 * the process_select struct tells get_process_info what processes
 * and information we are interested in seeing
 */

struct process_select
{
    bool idle;		/* show idle processes */
    bool self;		/* show self */
    bool system;		/* show system processes */
    bool thread;		/* show threads */
    bool thread_id;		/* show thread ids */
#define TOP_MAX_UIDS 8
    int uid[TOP_MAX_UIDS];	/* only these uids (unless uid[0] == -1) */
    bool wcpu;		/* show weighted cpu */
    int jid;		/* only this jid (unless jid == -1) */
    bool jail;		/* show jail ID */
    bool swap;		/* show swap usage */
    bool kidle;		/* show per-CPU idle threads */
    int pid;		/* only this pid (unless pid == -1) */
    const char *command;	/* only this command (unless == NULL) */
};

/* routines defined by the machine dependent module */

struct handle;

char	*format_header(const char *uname_field);
char	*format_next_process(struct handle* handle, char *(*get_userid)(int),
	    int flags);
void	 toggle_pcpustats(void);
void	 get_system_info(struct system_info *si);
int	 machine_init(struct statics *statics);

/* non-int routines typically used by the machine dependent module */
extern struct process_select ps;

void *
get_process_info(struct system_info *si, struct process_select *sel,
    int (*compare)(const void *, const void *));

#endif /* MACHINE_H */
