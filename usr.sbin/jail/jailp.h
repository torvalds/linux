/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 James Gritton.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/jail.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <jail.h>

#define CONF_FILE	"/etc/jail.conf"

#define DEP_FROM	0
#define DEP_TO		1

#define DF_SEEN		0x01	/* Dependency has been followed */
#define DF_LIGHT	0x02	/* Implied dependency on jail existence only */
#define DF_NOFAIL	0x04	/* Don't propagate failed jails */

#define PF_VAR		0x01	/* This is a variable, not a true parameter */
#define PF_APPEND	0x02	/* Append to existing parameter list */
#define PF_BAD		0x04	/* Unable to resolve parameter value */
#define PF_INTERNAL	0x08	/* Internal parameter, not passed to kernel */
#define PF_BOOL		0x10	/* Boolean parameter */
#define PF_INT		0x20	/* Integer parameter */
#define PF_CONV		0x40	/* Parameter duplicated in converted form */
#define PF_REV		0x80	/* Run commands in reverse order on stopping */
#define	PF_IMMUTABLE	0x100	/* Immutable parameter */

#define JF_START	0x0001	/* -c */
#define JF_SET		0x0002	/* -m */
#define JF_STOP		0x0004	/* -r */
#define JF_DEPEND	0x0008	/* Operation required by dependency */
#define JF_WILD		0x0010	/* Not specified on the command line */
#define JF_FAILED	0x0020	/* Operation failed */
#define JF_PARAMS	0x0040	/* Parameters checked and imported */
#define JF_RDTUN	0x0080	/* Create-only parameter check has been done */
#define JF_PERSIST	0x0100	/* Jail is temporarily persistent */
#define JF_TIMEOUT	0x0200	/* A command (or process kill) timed out */
#define JF_SLEEPQ	0x0400	/* Waiting on a command and/or timeout */
#define JF_FROM_RUNQ	0x0800	/* Has already been on the run queue */
#define JF_SHOW		0x1000	/* -e Exhibit list of configured jails */

#define JF_OP_MASK		(JF_START | JF_SET | JF_STOP)
#define JF_RESTART		(JF_START | JF_STOP)
#define JF_START_SET		(JF_START | JF_SET)
#define JF_SET_RESTART		(JF_SET | JF_STOP)
#define JF_START_SET_RESTART	(JF_START | JF_SET | JF_STOP)
#define JF_DO_STOP(js)		(((js) & (JF_SET | JF_STOP)) == JF_STOP)

enum intparam {
	IP__NULL = 0,		/* Null command */
	IP_ALLOW_DYING,		/* Allow making changes to a dying jail */
	IP_COMMAND,		/* Command run inside jail at creation */
	IP_DEPEND,		/* Jail starts after (stops before) another */
	IP_EXEC_CLEAN,		/* Run commands in a clean environment */
	IP_EXEC_CONSOLELOG,	/* Redirect optput for commands run in jail */
	IP_EXEC_FIB,		/* Run jailed commands with this FIB */
	IP_EXEC_JAIL_USER,	/* Run jailed commands as this user */
	IP_EXEC_POSTSTART,	/* Commands run outside jail after creating */
	IP_EXEC_POSTSTOP,	/* Commands run outside jail after removing */
	IP_EXEC_PRESTART,	/* Commands run outside jail before creating */
	IP_EXEC_PRESTOP,	/* Commands run outside jail before removing */
	IP_EXEC_CREATED,	/* Commands run outside jail right after it was started */
	IP_EXEC_START,		/* Commands run inside jail on creation */
	IP_EXEC_STOP,		/* Commands run inside jail on removal */
	IP_EXEC_SYSTEM_JAIL_USER,/* Get jail_user from system passwd file */
	IP_EXEC_SYSTEM_USER,	/* Run non-jailed commands as this user */
	IP_EXEC_TIMEOUT,	/* Time to wait for a command to complete */
#if defined(INET) || defined(INET6)
	IP_INTERFACE,		/* Add IP addresses to this interface */
	IP_IP_HOSTNAME,		/* Get jail IP address(es) from hostname */
#endif
	IP_MOUNT,		/* Mount points in fstab(5) form */
	IP_MOUNT_DEVFS,		/* Mount /dev under prison root */
	IP_MOUNT_FDESCFS,	/* Mount /dev/fd under prison root */
	IP_MOUNT_PROCFS,	/* Mount /proc under prison root */
	IP_MOUNT_FSTAB,		/* A standard fstab(5) file */
	IP_STOP_TIMEOUT,	/* Time to wait after sending SIGTERM */
	IP_VNET_INTERFACE,	/* Assign interface(s) to vnet jail */
#ifdef INET
	IP__IP4_IFADDR,		/* Copy of ip4.addr with interface/netmask */
#endif
#ifdef INET6
	IP__IP6_IFADDR,		/* Copy of ip6.addr with interface/prefixlen */
#endif
	IP__MOUNT_FROM_FSTAB,	/* Line from mount.fstab file */
	IP__OP,			/* Placeholder for requested operation */
	KP_ALLOW_CHFLAGS,
	KP_ALLOW_MOUNT,
	KP_ALLOW_RAW_SOCKETS,
	KP_ALLOW_SET_HOSTNAME,
	KP_ALLOW_SOCKET_AF,
	KP_ALLOW_SYSVIPC,
	KP_DEVFS_RULESET,
	KP_HOST_HOSTNAME,
#ifdef INET
	KP_IP4_ADDR,
#endif
#ifdef INET6
	KP_IP6_ADDR,
#endif
	KP_JID,
	KP_NAME,
	KP_PATH,
	KP_PERSIST,
	KP_SECURELEVEL,
	KP_VNET,
	IP_NPARAM
};

STAILQ_HEAD(cfvars, cfvar);

struct cfvar {
	STAILQ_ENTRY(cfvar)	tq;
	char			*name;
	size_t			pos;
};

TAILQ_HEAD(cfstrings, cfstring);

struct cfstring {
	TAILQ_ENTRY(cfstring)	tq;
	char			*s;
	size_t			len;
	struct cfvars		vars;
};

TAILQ_HEAD(cfparams, cfparam);

struct cfparam {
	TAILQ_ENTRY(cfparam)	tq;
	char			*name;
	struct cfstrings	val;
	unsigned		flags;
	int			gen;
};

TAILQ_HEAD(cfjails, cfjail);
STAILQ_HEAD(cfdepends, cfdepend);

struct cfjail {
	TAILQ_ENTRY(cfjail)	tq;
	char			*name;
	char			*comline;
	struct cfparams		params;
	struct cfdepends	dep[2];
	struct cfjails		*queue;
	struct cfparam		*intparams[IP_NPARAM];
	struct cfstring		*comstring;
	struct jailparam	*jp;
	struct timespec		timeout;
	const enum intparam	*comparam;
	unsigned		flags;
	int			jid;
	int			seq;
	int			pstatus;
	int			ndeps;
	int			njp;
	int			nprocs;
};

struct cfdepend {
	STAILQ_ENTRY(cfdepend)	tq[2];
	struct cfjail		*j[2];
	unsigned		flags;
};

extern void *emalloc(size_t);
extern void *erealloc(void *, size_t);
extern char *estrdup(const char *);
extern int create_jail(struct cfjail *j);
extern void failed(struct cfjail *j);
extern void jail_note(const struct cfjail *j, const char *fmt, ...);
extern void jail_warnx(const struct cfjail *j, const char *fmt, ...);

extern int next_command(struct cfjail *j);
extern int finish_command(struct cfjail *j);
extern struct cfjail *next_proc(int nonblock);

extern void load_config(void);
extern struct cfjail *add_jail(void);
extern void add_param(struct cfjail *j, const struct cfparam *p,
    enum intparam ipnum, const char *value);
extern int bool_param(const struct cfparam *p);
extern int int_param(const struct cfparam *p, int *ip);
extern const char *string_param(const struct cfparam *p);
extern int check_intparams(struct cfjail *j);
extern int import_params(struct cfjail *j);
extern int equalopts(const char *opt1, const char *opt2);
extern int wild_jail_name(const char *wname);
extern int wild_jail_match(const char *jname, const char *wname);

extern void dep_setup(int docf);
extern int dep_check(struct cfjail *j);
extern void dep_done(struct cfjail *j, unsigned flags);
extern void dep_reset(struct cfjail *j);
extern struct cfjail *next_jail(void);
extern int start_state(const char *target, int docf, unsigned state,
    int running);
extern void requeue(struct cfjail *j, struct cfjails *queue);
extern void requeue_head(struct cfjail *j, struct cfjails *queue);

extern void yyerror(const char *);
extern int yylex(void);

extern struct cfjails cfjails;
extern struct cfjails ready;
extern struct cfjails depend;
extern const char *cfname;
extern int iflag;
extern int note_remove;
extern int paralimit;
extern int verbose;
