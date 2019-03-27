/*	$OpenBSD: dhcpd.h,v 1.33 2004/05/06 22:29:15 deraadt Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 1995, 1996, 1997, 1998, 1999
 * The Internet Software Consortium.    All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 *
 * $FreeBSD$
 */

#include <sys/param.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <netdb.h>
#include <paths.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <libcasper.h>
#include <casper/cap_syslog.h>

#include "dhcp.h"
#include "tree.h"

#define	LOCAL_PORT	68
#define	REMOTE_PORT	67

struct option_data {
	size_t		 len;
	u_int8_t	*data;
};

struct string_list {
	struct string_list	*next;
	char			*string;
};

struct iaddr {
	size_t len;
	unsigned char iabuf[16];
};

struct iaddrlist {
	struct iaddrlist *next;
	struct iaddr addr;
};

struct packet {
	struct dhcp_packet	*raw;
	int			 packet_length;
	int			 packet_type;
	int			 options_valid;
	int			 client_port;
	struct iaddr		 client_addr;
	struct interface_info	*interface;
	struct hardware		*haddr;
	struct option_data	 options[256];
};

struct hardware {
	u_int8_t htype;
	u_int8_t hlen;
	u_int8_t haddr[16];
};

struct client_lease {
	struct client_lease	*next;
	time_t			 expiry, renewal, rebind;
	struct iaddr		 address;
	struct iaddr		 nextserver;
	char			*server_name;
	char			*filename;
	struct string_list	*medium;
	unsigned int		 is_static : 1;
	unsigned int		 is_bootp : 1;
	struct option_data	 options[256];
};

/* Possible states in which the client can be. */
enum dhcp_state {
	S_REBOOTING,
	S_INIT,
	S_SELECTING,
	S_REQUESTING,
	S_BOUND,
	S_RENEWING,
	S_REBINDING
};

struct client_config {
	struct option_data	defaults[256];
	enum {
		ACTION_DEFAULT,
		ACTION_SUPERSEDE,
		ACTION_PREPEND,
		ACTION_APPEND
	} default_actions[256];

	struct option_data	 send_options[256];
	u_int8_t		 required_options[256];
	u_int8_t		 requested_options[256];
	int			 requested_option_count;
	time_t			 timeout;
	time_t			 initial_interval;
	time_t			 retry_interval;
	time_t			 select_interval;
	time_t			 reboot_timeout;
	time_t			 backoff_cutoff;
	struct string_list	*media;
	char			*script_name;
	enum { IGNORE, ACCEPT, PREFER }
				 bootp_policy;
	struct string_list	*medium;
	struct iaddrlist	*reject_list;
};

struct client_state {
	struct client_lease	 *active;
	struct client_lease	 *new;
	struct client_lease	 *offered_leases;
	struct client_lease	 *leases;
	struct client_lease	 *alias;
	enum dhcp_state		  state;
	struct iaddr		  destination;
	u_int32_t		  xid;
	u_int16_t		  secs;
	time_t			  first_sending;
	time_t			  interval;
	struct string_list	 *medium;
	struct dhcp_packet	  packet;
	int			  packet_length;
	struct iaddr		  requested_address;
	struct client_config	 *config;
	char			**scriptEnv;
	int			  scriptEnvsize;
	struct string_list	 *env;
	int			  envc;
};

struct interface_info {
	struct interface_info	*next;
	struct hardware		 hw_address;
	struct in_addr		 primary_address;
	char			 name[IFNAMSIZ];
	int			 rfdesc;
	int			 wfdesc;
	int			 ufdesc;
	unsigned char		*rbuf;
	size_t			 rbuf_max;
	size_t			 rbuf_offset;
	size_t			 rbuf_len;
	struct ifreq		*ifp;
	struct client_state	*client;
	int			 noifmedia;
	int			 errors;
	int			 dead;
	u_int16_t		 index;
	int			 linkstat;
};

struct timeout {
	struct timeout	*next;
	time_t		 when;
	void		 (*func)(void *);
	void		*what;
};

struct protocol {
	struct protocol	*next;
	int fd;
	void (*handler)(struct protocol *);
	void *local;
};

#define DEFAULT_HASH_SIZE 97

struct hash_bucket {
	struct hash_bucket *next;
	const unsigned char *name;
	int len;
	unsigned char *value;
};

struct hash_table {
	int hash_count;
	struct hash_bucket *buckets[DEFAULT_HASH_SIZE];
};

/* Default path to dhcpd config file. */
#define	_PATH_DHCLIENT_CONF	"/etc/dhclient.conf"
#define	_PATH_DHCLIENT_DB	"/var/db/dhclient.leases"
#define	DHCPD_LOG_FACILITY	LOG_DAEMON

#define	MAX_TIME 0x7fffffff
#define	MIN_TIME 0

/* External definitions... */

/* options.c */
int cons_options(struct packet *, struct dhcp_packet *, int,
    struct tree_cache **, int, int, int, u_int8_t *, int);
const char *pretty_print_option(unsigned int,
    unsigned char *, int, int, int);
void do_packet(struct interface_info *, struct dhcp_packet *,
    int, unsigned int, struct iaddr, struct hardware *);

/* errwarn.c */
extern int warnings_occurred;
void error(const char *, ...) __attribute__ ((__format__ (__printf__, 1, 2))) __dead2;
int warning(const char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
int note(const char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
int debug(const char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
int parse_warn(const char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

/* conflex.c */
extern int lexline, lexchar;
extern char *token_line;
extern const char *tlname;
extern char comments[4096];
extern int comment_index;
extern int eol_token;
void new_parse(const char *);
int next_token(char **, FILE *);
int peek_token(char **, FILE *);

/* parse.c */
void skip_to_semi(FILE *);
int parse_semi(FILE *);
char *parse_string(FILE *);
int parse_ip_addr(FILE *, struct iaddr *);
void parse_hardware_param(FILE *, struct hardware *);
void parse_lease_time(FILE *, time_t *);
unsigned char *parse_numeric_aggregate(FILE *, unsigned char *, size_t *,
    int, unsigned, int);
void convert_num(unsigned char *, char *, unsigned, int);
time_t parse_date(FILE *);

/* tree.c */
pair cons(caddr_t, pair);

/* alloc.c */
struct string_list	*new_string_list(size_t size);
struct hash_table	*new_hash_table(int);
struct hash_bucket	*new_hash_bucket(void);

/* bpf.c */
int if_register_bpf(struct interface_info *, int);
void if_register_send(struct interface_info *);
void if_register_receive(struct interface_info *);
void send_packet_unpriv(int, struct dhcp_packet *, size_t, struct in_addr,
    struct in_addr);
struct imsg_hdr;
void send_packet_priv(struct interface_info *, struct imsg_hdr *, int);
ssize_t receive_packet(struct interface_info *, unsigned char *, size_t,
    struct sockaddr_in *, struct hardware *);

/* dispatch.c */
extern void (*bootp_packet_handler)(struct interface_info *,
    struct dhcp_packet *, int, unsigned int, struct iaddr, struct hardware *);
void discover_interfaces(struct interface_info *);
void reinitialize_interfaces(void);
void dispatch(void);
void got_one(struct protocol *);
void add_timeout(time_t, void (*)(void *), void *);
void cancel_timeout(void (*)(void *), void *);
void add_protocol(const char *, int, void (*)(struct protocol *), void *);
void remove_protocol(struct protocol *);
int interface_link_status(char *);
void interface_set_mtu_unpriv(int, u_int16_t);
void interface_set_mtu_priv(char *, u_int16_t); 

/* hash.c */
struct hash_table *new_hash(void);
void add_hash(struct hash_table *, const unsigned char *, int, unsigned char *);
void *hash_lookup(struct hash_table *, unsigned char *, int);

/* tables.c */
extern struct option dhcp_options[256];
extern unsigned char dhcp_option_default_priority_list[];
extern int sizeof_dhcp_option_default_priority_list;
extern struct hash_table universe_hash;
extern struct universe dhcp_universe;
void initialize_universes(void);

/* convert.c */
u_int32_t getULong(unsigned char *);
int32_t getLong(unsigned char *);
u_int16_t getUShort(unsigned char *);
int16_t getShort(unsigned char *);
void putULong(unsigned char *, u_int32_t);
void putLong(unsigned char *, int32_t);
void putUShort(unsigned char *, unsigned int);
void putShort(unsigned char *, int);

/* inet.c */
struct iaddr subnet_number(struct iaddr, struct iaddr);
struct iaddr broadcast_addr(struct iaddr, struct iaddr);
int addr_eq(struct iaddr, struct iaddr);
char *piaddr(struct iaddr);

/* dhclient.c */
extern cap_channel_t *capsyslog;
extern const char *path_dhclient_conf;
extern char *path_dhclient_db;
extern time_t cur_time;
extern int log_priority;
extern int log_perror;

extern struct client_config top_level_config;

extern struct pidfh *pidfile;

extern struct interface_info *ifi;

void dhcpoffer(struct packet *);
void dhcpack(struct packet *);
void dhcpnak(struct packet *);

void send_discover(void *);
void send_request(void *);
void send_decline(void *);

void state_reboot(void *);
void state_init(void *);
void state_selecting(void *);
void state_requesting(void *);
void state_bound(void *);
void state_panic(void *);

void bind_lease(struct interface_info *);

void make_discover(struct interface_info *, struct client_lease *);
void make_request(struct interface_info *, struct client_lease *);
void make_decline(struct interface_info *, struct client_lease *);

void free_client_lease(struct client_lease *);
void rewrite_client_leases(void);
void write_client_lease(struct interface_info *, struct client_lease *, int);

void	 priv_script_init(const char *, char *);
void	 priv_script_write_params(const char *, struct client_lease *);
int	 priv_script_go(void);

void script_init(const char *, struct string_list *);
void script_write_params(const char *, struct client_lease *);
int script_go(void);
void client_envadd(struct client_state *,
    const char *, const char *, const char *, ...);
void script_set_env(struct client_state *, const char *, const char *,
    const char *);
void script_flush_env(struct client_state *);
int dhcp_option_ev_name(char *, size_t, struct option *);

struct client_lease *packet_to_lease(struct packet *);
void go_daemon(void);
void client_location_changed(void);

void bootp(struct packet *);
void dhcp(struct packet *);

/* packet.c */
void assemble_hw_header(struct interface_info *, unsigned char *, int *);
void assemble_udp_ip_header(unsigned char *, int *, u_int32_t, u_int32_t,
    unsigned int, unsigned char *, int);
ssize_t decode_hw_header(unsigned char *, int, struct hardware *);
ssize_t decode_udp_ip_header(unsigned char *, int, struct sockaddr_in *,
    unsigned char *, int);

/* clparse.c */
int read_client_conf(void);
void read_client_leases(void);
void parse_client_statement(FILE *, struct interface_info *,
    struct client_config *);
unsigned parse_X(FILE *, u_int8_t *, unsigned);
int parse_option_list(FILE *, u_int8_t *);
void parse_interface_declaration(FILE *, struct client_config *);
struct interface_info *interface_or_dummy(char *);
void make_client_state(struct interface_info *);
void make_client_config(struct interface_info *, struct client_config *);
void parse_client_lease_statement(FILE *, int);
void parse_client_lease_declaration(FILE *, struct client_lease *,
    struct interface_info **);
struct option *parse_option_decl(FILE *, struct option_data *);
void parse_string_list(FILE *, struct string_list **, int);
void parse_reject_statement(FILE *, struct client_config *);

/* privsep.c */
struct buf	*buf_open(size_t);
int		 buf_add(struct buf *, const void *, size_t);
int		 buf_close(int, struct buf *);
ssize_t		 buf_read(int, void *, size_t);
void		 dispatch_imsg(struct interface_info *, int);
