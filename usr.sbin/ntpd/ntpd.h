/*	$OpenBSD: ntpd.h,v 1.155 2025/08/20 10:40:21 henning Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2012 Mike Miller <mmiller@mgm51.com>
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

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <poll.h>
#include <imsg.h>

#include "ntp.h"
#include "log.h"

#define MAXIMUM(a, b)	((a) > (b) ? (a) : (b))

#define	NTPD_USER	"_ntp"
#define	CONFFILE	"/etc/ntpd.conf"
#define DRIFTFILE	"/var/db/ntpd.drift"
#define	CTLSOCKET	"/var/run/ntpd.sock"

#define	INTERVAL_QUERY_NORMAL		30	/* sync to peers every n secs */
#define	INTERVAL_QUERY_PATHETIC		60
#define	INTERVAL_QUERY_AGGRESSIVE	5
#define	INTERVAL_QUERY_ULTRA_VIOLENCE	1	/* used at startup for auto */

#define	TRUSTLEVEL_BADPEER		6
#define	TRUSTLEVEL_PATHETIC		2
#define	TRUSTLEVEL_AGGRESSIVE		8
#define	TRUSTLEVEL_MAX			10

#define	MAX_SERVERS_DNS			8

#define	QSCALE_OFF_MIN			0.001
#define	QSCALE_OFF_MAX			0.050

#define	QUERYTIME_MAX		15	/* single query might take n secs max */
#define	OFFSET_ARRAY_SIZE	8
#define	SENSOR_OFFSETS		6
#define	SETTIME_TIMEOUT		15	/* max seconds to wait with -s */
#define	LOG_NEGLIGIBLE_ADJTIME	32	/* negligible drift to not log (ms) */
#define	LOG_NEGLIGIBLE_ADJFREQ	0.05	/* negligible rate to not log (ppm) */
#define	FREQUENCY_SAMPLES	8	/* samples for est. of permanent drift */
#define	MAX_FREQUENCY_ADJUST	128e-5	/* max correction per iteration */
#define MAX_SEND_ERRORS		3	/* max send errors before reconnect */
#define	MAX_DISPLAY_WIDTH	80	/* max chars in ctl_show report line */

#define FILTER_ADJFREQ		0x01	/* set after doing adjfreq */
#define AUTO_REPLIES    	4	/* # of ntp replies we want for auto */
#define AUTO_THRESHOLD		60	/* dont bother auto setting < this */
#define INTERVAL_AUIO_DNSFAIL	1	/* DNS tmpfail interval for auto */
#define TRIES_AUTO_DNSFAIL	4	/* DNS tmpfail quick retries */


#define	SENSOR_DATA_MAXAGE		(15*60)
#define	SENSOR_QUERY_INTERVAL		15
#define	SENSOR_QUERY_INTERVAL_SETTIME	(SETTIME_TIMEOUT/3)
#define	SENSOR_SCAN_INTERVAL		(1*60)
#define	SENSOR_DEFAULT_REFID		"HARD"

#define CONSTRAINT_ERROR_MARGIN		(4)
#define CONSTRAINT_RETRY_INTERVAL	(15)
#define CONSTRAINT_SCAN_INTERVAL	(15*60)
#define CONSTRAINT_SCAN_TIMEOUT		(10)
#define CONSTRAINT_MARGIN		(2.0*60)
#define CONSTRAINT_PORT			"443"	/* HTTPS port */
#define	CONSTRAINT_MAXHEADERLENGTH	8192
#define CONSTRAINT_PASSFD		(STDERR_FILENO + 1)

#define PARENT_SOCK_FILENO		CONSTRAINT_PASSFD

#define NTP_PROC_NAME			"ntp_main"
#define NTPDNS_PROC_NAME		"ntp_dns"
#define CONSTRAINT_PROC_NAME		"constraint"

enum client_state {
	STATE_NONE,
	STATE_DNS_INPROGRESS,
	STATE_DNS_TEMPFAIL,
	STATE_DNS_DONE,
	STATE_QUERY_SENT,
	STATE_REPLY_RECEIVED,
	STATE_TIMEOUT,
	STATE_INVALID
};

struct listen_addr {
	TAILQ_ENTRY(listen_addr)	 entry;
	struct sockaddr_storage		 sa;
	int				 fd;
	int				 rtable;
};

struct ntp_addr {
	struct ntp_addr		*next;
	struct sockaddr_storage	 ss;
	int			 notauth;
};

struct ntp_addr_wrap {
	char			*name;
	char			*path;
	struct ntp_addr		*a;
	u_int8_t		 pool;
};

struct ntp_addr_msg {
	struct ntp_addr		 a;
	size_t			 namelen;
	size_t			 pathlen;
	u_int8_t		 synced;
};

struct ntp_status {
	double		rootdelay;
	double		rootdispersion;
	double		reftime;
	u_int32_t	refid;
	u_int32_t	send_refid;
	u_int8_t	synced;
	u_int8_t	leap;
	int8_t		precision;
	u_int8_t	poll;
	u_int8_t	stratum;
};

struct ntp_offset {
	struct ntp_status	status;
	double			offset;
	double			delay;
	double			error;
	time_t			rcvd;
	u_int8_t		good;
};

struct ntp_peer {
	TAILQ_ENTRY(ntp_peer)		 entry;
	struct ntp_addr_wrap		 addr_head;
	struct ntp_query		 query;
	struct ntp_addr			*addr;
	struct ntp_offset		 reply[OFFSET_ARRAY_SIZE];
	struct ntp_offset		 update;
	struct sockaddr_in		 query_addr4;
	struct sockaddr_in6		 query_addr6;
	enum client_state		 state;
	time_t				 next;
	time_t				 deadline;
	time_t				 poll;
	u_int32_t			 id;
	u_int8_t			 shift;
	u_int8_t			 trustlevel;
	u_int8_t			 weight;
	u_int8_t			 trusted;
	int				 lasterror;
	int				 senderrors;
};

struct ntp_sensor {
	TAILQ_ENTRY(ntp_sensor)		 entry;
	struct ntp_offset		 offsets[SENSOR_OFFSETS];
	struct ntp_offset		 update;
	time_t				 next;
	time_t				 last;
	char				*device;
	u_int32_t			 refid;
	int				 sensordevid;
	int				 correction;
	u_int8_t			 stratum;
	u_int8_t			 weight;
	u_int8_t			 shift;
	u_int8_t			 trusted;
};

struct constraint {
	TAILQ_ENTRY(constraint)		 entry;
	struct ntp_addr_wrap		 addr_head;
	struct ntp_addr			*addr;
	int				 senderrors;
	enum client_state		 state;
	u_int32_t			 id;
	int				 fd;
	pid_t				 pid;
	struct imsgbuf			 ibuf;
	time_t				 last;
	time_t				 constraint;
	int				 dnstries;
};

struct ntp_conf_sensor {
	TAILQ_ENTRY(ntp_conf_sensor)		 entry;
	char					*device;
	char					*refstr;
	int					 correction;
	u_int8_t				 stratum;
	u_int8_t				 weight;
	u_int8_t				 trusted;
};

struct ntp_freq {
	double				overall_offset;
	double				x, y;
	double				xx, xy;
	int				samples;
	u_int				num;
};

struct ntpd_conf {
	TAILQ_HEAD(listen_addrs, listen_addr)		listen_addrs;
	TAILQ_HEAD(ntp_peers, ntp_peer)			ntp_peers;
	TAILQ_HEAD(ntp_sensors, ntp_sensor)		ntp_sensors;
	TAILQ_HEAD(ntp_conf_sensors, ntp_conf_sensor)	ntp_conf_sensors;
	TAILQ_HEAD(constraints, constraint)		constraints;
	struct ntp_status				status;
	struct ntp_freq					freq;
	struct sockaddr_in				query_addr4;
	struct sockaddr_in6				query_addr6;
	u_int32_t					scale;
	int				        	debug;
	int				        	verbose;
	u_int8_t					listen_all;
	u_int8_t					settime;
	u_int8_t					automatic;
	u_int8_t					noaction;
	u_int8_t					filters;
	u_int8_t					trusted_peers;
	u_int8_t					trusted_sensors;
	time_t						constraint_last;
	time_t						constraint_median;
	u_int						constraint_errors;
	u_int8_t					*ca;
	size_t						ca_len;
	int						tmpfail;
};

struct ctl_show_status {
	time_t		 constraint_median;
	time_t		 constraint_last;
	double		 clock_offset;
	u_int		 peercnt;
	u_int		 sensorcnt;
	u_int		 valid_peers;
	u_int		 valid_sensors;
	u_int		 constraint_errors;
	u_int8_t	 synced;
	u_int8_t	 stratum;
	u_int8_t	 constraints;
};

struct ctl_show_peer {
	char		 peer_desc[MAX_DISPLAY_WIDTH];
	u_int8_t	 syncedto;
	u_int8_t	 weight;
	u_int8_t	 trustlevel;
	u_int8_t	 stratum;
	time_t		 next;
	time_t		 poll;
	double		 offset;
	double		 delay;
	double		 jitter;
};

struct ctl_show_sensor {
	char		 sensor_desc[MAX_DISPLAY_WIDTH];
	u_int8_t	 syncedto;
	u_int8_t	 weight;
	u_int8_t	 good;
	u_int8_t	 stratum;
	time_t		 next;
	time_t		 poll;
	double		 offset;
	double		 correction;
};

struct ctl_conn {
	TAILQ_ENTRY(ctl_conn)	entry;
	struct imsgbuf		ibuf;
};

TAILQ_HEAD(ctl_conns, ctl_conn)	;

enum imsg_type {
	IMSG_NONE,
	IMSG_ADJTIME,
	IMSG_ADJFREQ,
	IMSG_SETTIME,
	IMSG_HOST_DNS,
	IMSG_CONSTRAINT_DNS,
	IMSG_CONSTRAINT_QUERY,
	IMSG_CONSTRAINT_RESULT,
	IMSG_CONSTRAINT_CLOSE,
	IMSG_CONSTRAINT_KILL,
	IMSG_CTL_SHOW_STATUS,
	IMSG_CTL_SHOW_PEERS,
	IMSG_CTL_SHOW_PEERS_END,
	IMSG_CTL_SHOW_SENSORS,
	IMSG_CTL_SHOW_SENSORS_END,
	IMSG_CTL_SHOW_ALL,
	IMSG_CTL_SHOW_ALL_END,
	IMSG_SYNCED,
	IMSG_UNSYNCED,
	IMSG_PROBE_ROOT
};

enum ctl_actions {
	CTL_SHOW_STATUS,
	CTL_SHOW_PEERS,
	CTL_SHOW_SENSORS,
	CTL_SHOW_ALL
};

/* prototypes */

/* ntp.c */
void	 ntp_main(struct ntpd_conf *, struct passwd *, int, char **);
void	 peer_addr_head_clear(struct ntp_peer *);
int	 priv_adjtime(void);
void	 priv_settime(double, char *);
void	 priv_dns(int, char *, u_int32_t);
int	 offset_compare(const void *, const void *);
void	 update_scale(double);
time_t	 scale_interval(time_t);
time_t	 error_interval(void);
extern struct ntpd_conf *conf;
extern struct ctl_conns  ctl_conns;

#define  SCALE_INTERVAL(x)	 MAXIMUM(5, (x) / 10)

/* parse.y */
int	 parse_config(const char *, struct ntpd_conf *);

/* config.c */
void			 host(const char *, struct ntp_addr **);
int			 host_dns(const char *, int, struct ntp_addr **);
void			 host_dns_free(struct ntp_addr *);
struct ntp_peer		*new_peer(void);
struct ntp_conf_sensor	*new_sensor(char *);
struct constraint	*new_constraint(void);

/* ntp_msg.c */
int	ntp_getmsg(struct sockaddr *, char *, ssize_t, struct ntp_msg *);
int	ntp_sendmsg(int, struct sockaddr *, struct ntp_msg *);

/* server.c */
int	setup_listeners(struct servent *, struct ntpd_conf *, u_int *);
int	server_dispatch(int, struct ntpd_conf *);

/* client.c */
int	client_peer_init(struct ntp_peer *);
int	client_addr_init(struct ntp_peer *);
int	client_nextaddr(struct ntp_peer *);
int	client_query(struct ntp_peer *);
int	client_dispatch(struct ntp_peer *, u_int8_t, u_int8_t);
void	client_log_error(struct ntp_peer *, const char *, int);
void	set_next(struct ntp_peer *, time_t);

/* constraint.c */
void	 constraint_add(struct constraint *);
void	 constraint_remove(struct constraint *);
void	 constraint_purge(void);
void	 constraint_reset(void);
int	 constraint_init(struct constraint *);
int	 constraint_query(struct constraint *, int);
int	 constraint_check(double);
void	 constraint_msg_dns(u_int32_t, u_int8_t *, size_t);
void	 constraint_msg_result(u_int32_t, u_int8_t *, size_t);
void	 constraint_msg_close(u_int32_t, u_int8_t *, size_t);
void	 priv_constraint_msg(u_int32_t, u_int8_t *, size_t, int, char **);
void	 priv_constraint_child(const char *, uid_t, gid_t);
void	 priv_constraint_kill(u_int32_t);
int	 priv_constraint_dispatch(struct pollfd *);
void	 priv_constraint_check_child(pid_t, int);
char	*get_string(u_int8_t *, size_t);

/* util.c */
double			 gettime_corrected(void);
double			 gettime_from_timeval(struct timeval *);
double			 getoffset(void);
double			 gettime(void);
time_t			 getmonotime(void);
int			 d_to_tv(double, struct timeval *);
double			 lfp_to_d(struct l_fixedpt);
struct l_fixedpt	 d_to_lfp(double);
double			 sfp_to_d(struct s_fixedpt);
struct s_fixedpt	 d_to_sfp(double);
char			*print_rtable(int);
const char		*log_sockaddr(struct sockaddr *);
const char		*log_ntp_addr(struct ntp_addr *);
pid_t			 start_child(char *, int, int, char **);
int			 sanitize_argv(int *, char ***);

/* sensors.c */
void			sensor_init(void);
int			sensor_scan(void);
void			sensor_query(struct ntp_sensor *);

/* ntp_dns.c */
void			ntp_dns(struct ntpd_conf *, struct passwd *);

/* control.c */
int			 control_check(char *);
int			 control_init(char *);
int			 control_listen(int);
void			 control_shutdown(int);
int			 control_accept(int);
struct ctl_conn		*control_connbyfd(int);
int			 control_close(int);
int			 control_dispatch_msg(struct pollfd *, u_int *);
void			 session_socket_nonblockmode(int);
void			 build_show_status(struct ctl_show_status *);
void			 build_show_peer(struct ctl_show_peer *,
			     struct ntp_peer *);
void			 build_show_sensor(struct ctl_show_sensor *,
			     struct ntp_sensor *);

