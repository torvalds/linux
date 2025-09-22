/*	$OpenBSD: snmpd.h,v 1.120 2024/05/21 05:00:48 jsg Exp $	*/

/*
 * Copyright (c) 2007, 2008, 2012 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#ifndef SNMPD_H
#define SNMPD_H

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/tree.h>
#include <sys/types.h>
#include <sys/un.h>

#include <netinet/in.h>

#include <ber.h>
#include <event.h>
#include <limits.h>
#include <imsg.h>
#include <stddef.h>
#include <stdint.h>

#include "mib.h"
#include "snmp.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

/*
 * common definitions for snmpd
 */

#define CONF_FILE		"/etc/snmpd.conf"
#define SNMPD_BACKEND		"/usr/libexec/snmpd"                                                                                                                                                                                                                                                                        
#define SNMPD_USER		"_snmpd"
#define SNMP_PORT		"161"
#define SNMPTRAP_PORT		"162"

#define AGENTX_MASTER_PATH	"/var/agentx/master"
#define AGENTX_GROUP		"_agentx"

#define SNMPD_MAXSTRLEN		484
#define SNMPD_MAXCOMMUNITYLEN	SNMPD_MAXSTRLEN
#define SNMPD_MAXVARBIND	0x7fffffff
#define SNMPD_MAXVARBINDLEN	1210
#define SNMPD_MAXENGINEIDLEN	32
#define SNMPD_MAXUSERNAMELEN	32
#define SNMPD_MAXCONTEXNAMELEN	32

#define SNMP_USM_MAXDIGESTLEN	48
#define SNMP_USM_SALTLEN	8
#define SNMP_USM_KEYLEN		64
#define SNMP_CIPHER_KEYLEN	16

#define SMALL_READ_BUF_SIZE	1024
#define READ_BUF_SIZE		65535
#define	RT_BUF_SIZE		16384
#define	MAX_RTSOCK_BUF		(2 * 1024 * 1024)

#define SNMP_ENGINEID_OLD	0x00
#define SNMP_ENGINEID_NEW	0x80	/* RFC3411 */

#define SNMP_ENGINEID_FMT_IPv4	1
#define SNMP_ENGINEID_FMT_IPv6	2
#define SNMP_ENGINEID_FMT_MAC	3
#define SNMP_ENGINEID_FMT_TEXT	4
#define SNMP_ENGINEID_FMT_OCT	5
#define SNMP_ENGINEID_FMT_HH	129

#define PEN_OPENBSD		30155

enum imsg_type {
	IMSG_NONE,
	IMSG_CTL_VERBOSE,
	IMSG_CTL_PROCFD,
	IMSG_TRAP_EXEC,
	IMSG_AX_FD
};

struct imsgev {
	struct imsgbuf		 ibuf;
	void			(*handler)(int, short, void *);
	struct event		 ev;
	struct privsep_proc	*proc;
	void			*data;
	short			 events;
	const char		*name;
};

#define IMSG_SIZE_CHECK(imsg, p) do {				\
	if (IMSG_DATA_SIZE(imsg) < sizeof(*p))			\
		fatalx("bad length imsg received");		\
} while (0)
#define IMSG_DATA_SIZE(imsg)	((imsg)->hdr.len - IMSG_HEADER_SIZE)

enum privsep_procid {
	PROC_PARENT,	/* Parent process and application interface */
	PROC_SNMPE,	/* SNMP engine */
	PROC_MAX
};

extern enum privsep_procid privsep_process;

struct privsep_pipes {
	int			*pp_pipes[PROC_MAX];
};

struct privsep {
	struct privsep_pipes	*ps_pipes[PROC_MAX];
	struct privsep_pipes	*ps_pp;

	struct imsgev		*ps_ievs[PROC_MAX];
	const char		*ps_title[PROC_MAX];
	pid_t			 ps_pid[PROC_MAX];
	struct passwd		*ps_pw;

	u_int			 ps_instances[PROC_MAX];
	u_int			 ps_instance;
	int			 ps_noaction;

	/* Event and signal handlers */
	struct event		 ps_evsigint;
	struct event		 ps_evsigterm;
	struct event		 ps_evsigchld;
	struct event		 ps_evsighup;
	struct event		 ps_evsigpipe;
	struct event		 ps_evsigusr1;

	void			*ps_env;
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
	struct privsep		*p_ps;
	struct passwd		*p_pw;
};

struct privsep_fd {
	enum privsep_procid		 pf_procid;
	unsigned int			 pf_instance;
};

#define PROC_PARENT_SOCK_FILENO	3
#define PROC_MAX_INSTANCES	32

#if DEBUG
#define DPRINTF		log_debug
#else
#define DPRINTF(x...)	do {} while(0)
#endif

#define OID(...)		(struct ber_oid){ { __VA_ARGS__ },	\
    (sizeof((uint32_t []) { __VA_ARGS__ }) / sizeof(uint32_t)) }

/*
 * daemon structures
 */

#define MSG_HAS_AUTH(m)		(((m)->sm_flags & SNMP_MSGFLAG_AUTH) != 0)
#define MSG_HAS_PRIV(m)		(((m)->sm_flags & SNMP_MSGFLAG_PRIV) != 0)
#define MSG_SECLEVEL(m)		((m)->sm_flags & SNMP_MSGFLAG_SECMASK)
#define MSG_REPORT(m)		(((m)->sm_flags & SNMP_MSGFLAG_REPORT) != 0)

struct snmp_message {
	int			 sm_sock;
	struct sockaddr_storage	 sm_ss;
	socklen_t		 sm_slen;
	int			 sm_sock_tcp;
	int			 sm_aflags;
	enum snmp_pdutype	 sm_pdutype;
	struct event		 sm_sockev;
	char			 sm_host[HOST_NAME_MAX+1];
	in_port_t		 sm_port;

	struct sockaddr_storage	 sm_local_ss;
	socklen_t		 sm_local_slen;

	struct ber		 sm_ber;
	struct ber_element	*sm_req;
	struct ber_element	*sm_resp;

	u_int8_t		 sm_data[READ_BUF_SIZE];
	size_t			 sm_datalen;

	uint32_t		 sm_transactionid;

	u_int			 sm_version;

	/* V1, V2c */
	char			 sm_community[SNMPD_MAXCOMMUNITYLEN];

	/* V3 */
	long long		 sm_msgid;
	long long		 sm_max_msg_size;
	u_int8_t		 sm_flags;
	long long		 sm_secmodel;
	u_int32_t		 sm_engine_boots;
	u_int32_t		 sm_engine_time;
	uint8_t			 sm_ctxengineid[SNMPD_MAXENGINEIDLEN];
	size_t			 sm_ctxengineid_len;
	char			 sm_ctxname[SNMPD_MAXCONTEXNAMELEN+1];

	/* USM */
	char			 sm_username[SNMPD_MAXUSERNAMELEN+1];
	struct usmuser		*sm_user;
	size_t			 sm_digest_offs;
	char			 sm_salt[SNMP_USM_SALTLEN];
	int			 sm_usmerr;

	long long		 sm_request;

	const char		*sm_errstr;
	long long		 sm_error;
#define sm_nonrepeaters		 sm_error
	long long		 sm_errorindex;
#define sm_maxrepetitions	 sm_errorindex

	struct ber_element	*sm_pdu;
	struct ber_element	*sm_pduend;

	struct ber_element	*sm_varbind;
	struct ber_element	*sm_varbindresp;

	RB_ENTRY(snmp_message)	 sm_entry;
};
RB_HEAD(snmp_messages, snmp_message);
extern struct snmp_messages snmp_messages;

/* Defined in SNMPv2-MIB.txt (RFC 3418) */
struct snmp_stats {
	u_int32_t		snmp_inpkts;
	u_int32_t		snmp_outpkts;
	u_int32_t		snmp_inbadversions;
	u_int32_t		snmp_inbadcommunitynames;
	u_int32_t		snmp_inbadcommunityuses;
	u_int32_t		snmp_inasnparseerrs;
	u_int32_t		snmp_intoobigs;
	u_int32_t		snmp_innosuchnames;
	u_int32_t		snmp_inbadvalues;
	u_int32_t		snmp_inreadonlys;
	u_int32_t		snmp_ingenerrs;
	u_int32_t		snmp_intotalreqvars;
	u_int32_t		snmp_intotalsetvars;
	u_int32_t		snmp_ingetrequests;
	u_int32_t		snmp_ingetnexts;
	u_int32_t		snmp_insetrequests;
	u_int32_t		snmp_ingetresponses;
	u_int32_t		snmp_intraps;
	u_int32_t		snmp_outtoobigs;
	u_int32_t		snmp_outnosuchnames;
	u_int32_t		snmp_outbadvalues;
	u_int32_t		snmp_outgenerrs;
	u_int32_t		snmp_outgetrequests;
	u_int32_t		snmp_outgetnexts;
	u_int32_t		snmp_outsetrequests;
	u_int32_t		snmp_outgetresponses;
	u_int32_t		snmp_outtraps;
	int			snmp_enableauthentraps;
	u_int32_t		snmp_silentdrops;
	u_int32_t		snmp_proxydrops;

	/* USM stats (RFC 3414) */
	u_int32_t		snmp_usmbadseclevel;
	u_int32_t		snmp_usmtimewindow;
	u_int32_t		snmp_usmnosuchuser;
	u_int32_t		snmp_usmnosuchengine;
	u_int32_t		snmp_usmwrongdigest;
	u_int32_t		snmp_usmdecrypterr;
};

struct address {
	struct sockaddr_storage	 ss;
	in_port_t		 port;
	int			 type;
	int			 flags;
	int			 fd;
	struct event		 ev;
	struct event		 evt;

	TAILQ_ENTRY(address)	 entry;
};
TAILQ_HEAD(addresslist, address);

struct agentx_master {
	int			axm_fd;
	struct sockaddr_un	axm_sun;
	uid_t			axm_owner;
	gid_t			axm_group;
	mode_t			axm_mode;

	struct event		axm_ev;

	TAILQ_ENTRY(agentx_master) axm_entry;
};
TAILQ_HEAD(axmasterlist, agentx_master);

#define ADDRESS_FLAG_READ	0x01
#define ADDRESS_FLAG_WRITE	0x02
#define ADDRESS_FLAG_NOTIFY	0x04
#define ADDRESS_FLAG_PERM	\
    (ADDRESS_FLAG_READ | ADDRESS_FLAG_WRITE | ADDRESS_FLAG_NOTIFY)
#define ADDRESS_FLAG_SNMPV1	0x10
#define ADDRESS_FLAG_SNMPV2	0x20
#define ADDRESS_FLAG_SNMPV3	0x40
#define ADDRESS_FLAG_MPS	\
    (ADDRESS_FLAG_SNMPV1 | ADDRESS_FLAG_SNMPV2 | ADDRESS_FLAG_SNMPV3)

struct trap_address {
	struct sockaddr_storage	 ta_ss;
	struct sockaddr_storage	 ta_sslocal;
	int			 ta_version;
	union {
		char		 ta_community[SNMPD_MAXCOMMUNITYLEN];
		struct {
			char		*ta_usmusername;
			struct usmuser	*ta_usmuser;
			int		 ta_seclevel;
		};
	};
	struct ber_oid		 ta_oid;

	TAILQ_ENTRY(trap_address) entry;
};
TAILQ_HEAD(trap_addresslist, trap_address);

enum usmauth {
	AUTH_NONE = 0,
	AUTH_MD5,	/* HMAC-MD5-96, RFC3414 */
	AUTH_SHA1,	/* HMAC-SHA-96, RFC3414 */
	AUTH_SHA224,	/* usmHMAC128SHA224AuthProtocol. RFC7860 */
	AUTH_SHA256,	/* usmHMAC192SHA256AuthProtocol. RFC7860 */
	AUTH_SHA384,	/* usmHMAC256SHA384AuthProtocol. RFC7860 */
	AUTH_SHA512	/* usmHMAC384SHA512AuthProtocol. RFC7860 */
};

#define AUTH_DEFAULT	AUTH_SHA1	/* Default digest */

enum usmpriv {
	PRIV_NONE = 0,
	PRIV_DES,	/* CBC-DES, RFC3414 */
	PRIV_AES	/* CFB128-AES-128, RFC3826 */
};

#define PRIV_DEFAULT	PRIV_AES	/* Default cipher */

struct usmuser {
	char			*uu_name;
	int			 uu_seclevel;

	enum usmauth		 uu_auth;
	char			*uu_authkey;
	unsigned		 uu_authkeylen;


	enum usmpriv		 uu_priv;
	char			*uu_privkey;
	unsigned long long	 uu_salt;

	SLIST_ENTRY(usmuser)	 uu_next;
};

struct snmp_system {
	char			 sys_descr[256];
	struct ber_oid		 sys_oid;
	char			 sys_contact[256];
	char			 sys_name[256];
	char			 sys_location[256];
	int8_t			 sys_services;
};

struct snmpd {
	u_int8_t		 sc_flags;
#define SNMPD_F_VERBOSE		 0x01
#define SNMPD_F_DEBUG		 0x02
#define SNMPD_F_NONAMES		 0x04
	enum mib_oidfmt		 sc_oidfmt;

	const char		*sc_confpath;
	struct addresslist	 sc_addresses;
	struct axmasterlist	 sc_agentx_masters;
	struct timeval		 sc_starttime;
	u_int32_t		 sc_engine_boots;

	char			 sc_rdcommunity[SNMPD_MAXCOMMUNITYLEN];
	char			 sc_rwcommunity[SNMPD_MAXCOMMUNITYLEN];
	char			 sc_trcommunity[SNMPD_MAXCOMMUNITYLEN];

	uint8_t			 sc_engineid[SNMPD_MAXENGINEIDLEN];
	size_t			 sc_engineid_len;

	struct snmp_stats	 sc_stats;
	struct snmp_system	 sc_system;

	struct trap_addresslist	 sc_trapreceivers;

	struct ber_oid		*sc_blocklist;
	size_t			 sc_nblocklist;
	int			 sc_rtfilter;

	int			 sc_min_seclevel;
	int			 sc_traphandler;

	struct privsep		 sc_ps;
};

struct trapcmd {
	struct ber_oid		 cmd_oid;
		/* sideways return for intermediate lookups */
	struct trapcmd		*cmd_maybe;

	int			 cmd_argc;
	char			**cmd_argv;

	RB_ENTRY(trapcmd)	 cmd_entry;
};
RB_HEAD(trapcmd_tree, trapcmd);
extern	struct trapcmd_tree trapcmd_tree;

extern struct snmpd *snmpd_env;

/* parse.y */
struct snmpd	*parse_config(const char *, u_int);
int		 cmdline_symset(char *);

/* snmpe.c */
void		 snmpe(struct privsep *, struct privsep_proc *);
void		 snmpe_shutdown(void);
void		 snmpe_dispatchmsg(struct snmp_message *);
void		 snmpe_response(struct snmp_message *);
int		 snmp_messagecmp(struct snmp_message *, struct snmp_message *);
RB_PROTOTYPE(snmp_messages, snmp_message, sm_entry, snmp_messagecmp)

/* trap.c */
void		 trap_init(void);
int		 trap_send(struct ber_oid *, struct ber_element *);

/* smi.c */
int		 smi_init(void);
int		 smi_string2oid(const char *, struct ber_oid *);
const char	*smi_insert(struct ber_oid *, const char *);
unsigned int	 smi_application(struct ber_element *);
void		 smi_debug_elements(struct ber_element *);

/* snmpd.c */
int		 snmpd_socket_af(struct sockaddr_storage *, int);
u_long		 snmpd_engine_time(void);

/* usm.c */
void		 usm_generate_keys(void);
struct usmuser	*usm_newuser(char *name, const char **);
struct usmuser	*usm_finduser(char *name);
int		 usm_checkuser(struct usmuser *, const char **);
struct ber_element *usm_decode(struct snmp_message *, struct ber_element *,
		    const char **);
struct ber_element *usm_encode(struct snmp_message *, struct ber_element *);
struct ber_element *usm_encrypt(struct snmp_message *, struct ber_element *);
void		 usm_finalize_digest(struct snmp_message *, char *, ssize_t);
void		 usm_make_report(struct snmp_message *);
const struct usmuser *usm_check_mincred(int, const char **);

/* proc.c */
enum privsep_procid
	    proc_getid(struct privsep_proc *, unsigned int, const char *);
void	 proc_init(struct privsep *, struct privsep_proc *, unsigned int, int,
	    int, char **, enum privsep_procid);
void	 proc_kill(struct privsep *);
void	 proc_connect(struct privsep *);
void	 proc_dispatch(int, short event, void *);
void	 proc_run(struct privsep *, struct privsep_proc *,
	    struct privsep_proc *, u_int,
	    void (*)(struct privsep *, struct privsep_proc *, void *), void *);
void	 imsg_event_add(struct imsgev *);
int	 imsg_compose_event(struct imsgev *, u_int16_t, u_int32_t,
	    pid_t, int, void *, u_int16_t);
int	 imsg_composev_event(struct imsgev *, u_int16_t, u_int32_t,
	    pid_t, int, const struct iovec *, int);
void	 proc_range(struct privsep *, enum privsep_procid, int *, int *);
int	 proc_compose_imsg(struct privsep *, enum privsep_procid, int,
	    u_int16_t, u_int32_t, int, void *, u_int16_t);
int	 proc_compose(struct privsep *, enum privsep_procid,
	    uint16_t, void *, uint16_t);
int	 proc_composev_imsg(struct privsep *, enum privsep_procid, int,
	    u_int16_t, u_int32_t, int, const struct iovec *, int);
int	 proc_composev(struct privsep *, enum privsep_procid,
	    uint16_t, const struct iovec *, int);
struct imsgbuf *
	 proc_ibuf(struct privsep *, enum privsep_procid, int);
struct imsgev *
	 proc_iev(struct privsep *, enum privsep_procid, int);
int	 proc_flush_imsg(struct privsep *, enum privsep_procid, int);

/* traphandler.c */
int	 traphandler_parse(struct snmp_message *);
int	 traphandler_priv_recvmsg(struct privsep_proc *, struct imsg *);
void	 trapcmd_free(struct trapcmd *);
int	 trapcmd_add(struct trapcmd *);
struct trapcmd *
	 trapcmd_lookup(struct ber_oid *);

/* util.c */
ssize_t	 sendtofrom(int, void *, size_t, int, struct sockaddr *,
	    socklen_t, struct sockaddr *, socklen_t);
ssize_t	 recvfromto(int, void *, size_t, int, struct sockaddr *,
	    socklen_t *, struct sockaddr *, socklen_t *);
const char *print_host(struct sockaddr_storage *, char *, size_t);
char	*tohexstr(u_int8_t *, int);
uint8_t *fromhexstr(uint8_t *, const char *, size_t);

#endif /* SNMPD_H */
