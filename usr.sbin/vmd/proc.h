/*	$OpenBSD: proc.h,v 1.29 2025/08/14 13:58:46 jsg Exp $	*/

/*
 * Copyright (c) 2010-2015 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <sys/socket.h>

#include <event.h>
#include <imsg.h>

#ifndef _PROC_H
#define _PROC_H

enum {
	IMSG_CTL_OK,
	IMSG_CTL_FAIL,
	IMSG_CTL_VERBOSE,
	IMSG_CTL_RESET,
	IMSG_PROC_MAX
};

/* imsg */
struct imsgev {
	struct imsgbuf		 ibuf;
	void			(*handler)(int, short, void *);
	struct event		 ev;
	struct privsep_proc	*proc;
	void			*data;
	short			 events;
};

/* control socket */
struct control_sock {
	const char	*cs_name;
	struct event	 cs_ev;
	struct event	 cs_evt;
	int		 cs_fd;
	int		 cs_restricted;
	void		*cs_env;
	uid_t		 cs_uid;
	gid_t		 cs_gid;

	TAILQ_ENTRY(control_sock) cs_entry;
};
TAILQ_HEAD(control_socks, control_sock);

struct ctl_conn {
	TAILQ_ENTRY(ctl_conn)	 entry;
	struct imsgev		 iev;
	struct sockpeercred	 peercred;
};
TAILQ_HEAD(ctl_connlist, ctl_conn);

/* privsep */
enum privsep_procid {
	PROC_PARENT	= 0,
	PROC_CONTROL,
	PROC_AGENTX,
	PROC_VMM,
	PROC_PRIV,
	PROC_MAX,
};
extern enum privsep_procid privsep_process;

#define CONFIG_RELOAD		0x00
#define CONFIG_VMS		0x01
#define CONFIG_SWITCHES		0x02
#define CONFIG_ALL		0xff

struct privsep {
	int				 ps_pipes[PROC_MAX];

	struct imsgev			 ps_ievs[PROC_MAX];
	const char			*ps_title[PROC_MAX];
	uint8_t				 ps_what[PROC_MAX];

	struct passwd			*ps_pw;
	int				 ps_noaction;

	struct control_sock		 ps_csock;
	struct control_socks		 ps_rcsocks;

	/* Event and signal handlers */
	struct event			 ps_evsigint;
	struct event			 ps_evsigterm;
	struct event			 ps_evsigchld;
	struct event			 ps_evsighup;
	struct event			 ps_evsigpipe;
	struct event			 ps_evsigusr1;

	void				*ps_env;
};

struct privsep_proc {
	const char		*p_title;
	enum privsep_procid	 p_id;
	int			(*p_cb)(int, struct privsep_proc *,
				    struct imsg *);
	void			(*p_init)(struct privsep *,
				    struct privsep_proc *);
	void			(*p_shutdown)(void);
	const char		*p_chroot;
	struct passwd		*p_pw;
	struct privsep		*p_ps;
};

#if DEBUG
#define DPRINTF		log_debug
#else
#define DPRINTF(x...)	do {} while(0)
#endif

#define PROC_PARENT_SOCK_FILENO	3

/* proc.c */
void	 proc_init(struct privsep *, struct privsep_proc *, unsigned int, int,
	    int, char **, enum privsep_procid);
void	 proc_kill(struct privsep *);
void	 proc_connect(struct privsep *ps);
void	 proc_dispatch(int, short event, void *);
void	 proc_run(struct privsep *, struct privsep_proc *,
	    struct privsep_proc *, unsigned int,
	    void (*)(struct privsep *, struct privsep_proc *, void *), void *);
void	 imsg_event_add(struct imsgev *);
void	 imsg_event_add2(struct imsgev *, struct event_base *);
int	 imsg_compose_event(struct imsgev *, uint32_t, uint32_t,
	    pid_t, int, void *, size_t);
int	 imsg_compose_event2(struct imsgev *, uint32_t, uint32_t,
    	    pid_t, int, void *, uint16_t, struct event_base *);
int	 imsg_composev_event(struct imsgev *, uint32_t, uint32_t,
	    pid_t, int, const struct iovec *, int);
void	 imsg_forward_event(struct imsgev *, struct imsg *);
int	 proc_compose_imsg(struct privsep *, enum privsep_procid,
	    uint32_t, uint32_t, int, void *, size_t);
int	 proc_compose(struct privsep *, enum privsep_procid,
	    uint32_t, void *data, size_t);
int	 proc_composev_imsg(struct privsep *, enum privsep_procid,
	    uint32_t, uint32_t, int, const struct iovec *, int);
int	 proc_composev(struct privsep *, enum privsep_procid,
	    uint32_t, const struct iovec *, int);
int	 proc_forward_imsg(struct privsep *, struct imsg *,
	    enum privsep_procid, uint32_t);
enum privsep_procid
	 proc_getid(struct privsep_proc *, unsigned int, const char *);
int	 proc_flush_imsg(struct privsep *, enum privsep_procid);

/* control.c */
void	 control(struct privsep *, struct privsep_proc *);
int	 control_init(struct privsep *, struct control_sock *);
int	 control_reset(struct control_sock *);
int	 control_listen(struct control_sock *);

/* log.c */
void	log_init(int, int);
void	log_procinit(const char *, ...);
void	log_setverbose(int);
int	log_getverbose(void);
void	log_warn(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_warnx(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_info(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_debug(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	logit(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
void	vlog(int, const char *, va_list)
	    __attribute__((__format__ (printf, 2, 0)));
__dead void fatal(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
__dead void fatalx(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));

/* imsg unmarshalling used in proc.c and elsewhere. */
int 		 imsg_int_read(struct imsg *);
unsigned int	 imsg_uint_read(struct imsg *);
char		*imsg_string_read(struct imsg *, size_t max);

#endif /* _PROC_H */
