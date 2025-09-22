/* $OpenBSD: ui.c,v 1.58 2021/10/24 21:24:21 deraadt Exp $	 */
/* $EOM: ui.c,v 1.43 2000/10/05 09:25:12 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2000 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999, 2000, 2001, 2002 Håkan Olsson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "conf.h"
#include "connection.h"
#include "doi.h"
#include "exchange.h"
#include "init.h"
#include "isakmp.h"
#include "log.h"
#include "monitor.h"
#include "sa.h"
#include "timer.h"
#include "transport.h"
#include "ui.h"
#include "util.h"

#define BUF_SZ 256

/* from isakmpd.c */
void		 daemon_shutdown_now(int);

/* Report all SA configuration information. */
void		 ui_report_sa(char *);

static FILE	*ui_open_result(void);

char		*ui_fifo = FIFO;
int		 ui_socket;
struct event	*ui_cr_event = NULL;
int		 ui_daemon_passive = 0;

/* Create and open the FIFO used for user control.  */
void
ui_init(void)
{
	struct stat     st;

	/* -f- means control messages comes in via stdin.  */
	if (strcmp(ui_fifo, "-") == 0) {
		ui_socket = 0;
		return;
	}

	/* Don't overwrite a file, i.e '-f /etc/isakmpd/isakmpd.conf'.  */
	if (lstat(ui_fifo, &st) == 0) {
		if (S_ISREG(st.st_mode)) {
			errno = EEXIST;
			log_fatal("ui_init: could not create FIFO \"%s\"",
			    ui_fifo);
		}
	}

	/* No need to know about errors.  */
	unlink(ui_fifo);
	if (mkfifo(ui_fifo, 0600) == -1)
		log_fatal("ui_init: mkfifo (\"%s\", 0600) failed", ui_fifo);

	ui_socket = open(ui_fifo, O_RDWR | O_NONBLOCK);
	if (ui_socket == -1)
		log_fatal("ui_init: open (\"%s\", O_RDWR | O_NONBLOCK, 0) "
		    "failed", ui_fifo);
}

/*
 * Setup a phase 2 connection.
 * XXX Maybe phase 1 works too, but teardown won't work then, fix?
 */
static void
ui_connect(char *cmd)
{
	char	name[201];

	if (sscanf(cmd, "c %200s", name) != 1) {
		log_print("ui_connect: command \"%s\" malformed", cmd);
		return;
	}
	LOG_DBG((LOG_UI, 10, "ui_connect: setup connection \"%s\"", name));
	connection_setup(name);
}

/* Tear down a connection, can be phase 1 or 2.  */
static void
ui_teardown(char *cmd)
{
	struct sockaddr_in	 addr;
	struct sockaddr_in6	 addr6;
	struct sa		*sa;
	int			 phase;
	char			 name[201];

	/* If no phase is given, we default to phase 2. */
	phase = 2;
	if (sscanf(cmd, "t main %200s", name) == 1)
		phase = 1;
	else if (sscanf(cmd, "t quick %200s", name) == 1)
		phase = 2;
	else if (sscanf(cmd, "t %200s", name) != 1) {
		log_print("ui_teardown: command \"%s\" malformed", cmd);
		return;
	}
	LOG_DBG((LOG_UI, 10, "ui_teardown: teardown connection \"%s\", "
	    "phase %d", name, phase));

	bzero(&addr, sizeof(addr));
	bzero(&addr6, sizeof(addr6));

	if (inet_pton(AF_INET, name, &addr.sin_addr) == 1) {
		addr.sin_len = sizeof(addr);
		addr.sin_family = AF_INET;

		while ((sa = sa_lookup_by_peer((struct sockaddr *)&addr,
		    SA_LEN((struct sockaddr *)&addr), phase)) != 0) {
			if (sa->name)
				connection_teardown(sa->name);
			sa_delete(sa, 1);
		}
	} else if (inet_pton(AF_INET6, name, &addr6.sin6_addr) == 1) {
		addr6.sin6_len = sizeof(addr6);
		addr6.sin6_family = AF_INET6;

		while ((sa = sa_lookup_by_peer((struct sockaddr *)&addr6,
		    SA_LEN((struct sockaddr *)&addr6), phase)) != 0) {
			if (sa->name)
				connection_teardown(sa->name);
			sa_delete(sa, 1);
		}
	} else {
		if (phase == 2)
			connection_teardown(name);
		while ((sa = sa_lookup_by_name(name, phase)) != 0)
			sa_delete(sa, 1);
	}
}

/* Tear down all phase 2 connections.  */
static void
ui_teardown_all(char *cmd)
{
	/* Skip 'cmd' as arg. */
	sa_teardown_all();
}

static void
ui_conn_reinit_event(void *v)
{
	/*
	 * This event is required for isakmpd to reinitialize the connection
	 * and passive-connection lists. Otherwise a change to the
	 * "[Phase 2]:Connections" tag will not have any effect.
	 */
	connection_reinit();

	ui_cr_event = NULL;
}

static void
ui_conn_reinit(void)
{
	struct timespec ts;

	if (ui_cr_event)
		timer_remove_event(ui_cr_event);

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_sec += 5;

	ui_cr_event = timer_add_event("ui_conn_reinit", ui_conn_reinit_event,
	    0, &ts);
	if (!ui_cr_event)
		log_print("ui_conn_reinit: timer_add_event() failed. "
		    "Connections will not be updated.");
}

/*
 * Call the configuration API.
 * XXX Error handling!  How to do multi-line transactions?
 */
static void
ui_config(char *cmd)
{
	struct conf_list *vlist;
	struct conf_list_node *vnode;
	char	 subcmd[201], section[201], tag[201], value[201], tmp[201];
	char	*v, *nv;
	int	 trans = 0, items, skip = 0, ret;
	FILE	*fp;

	if (sscanf(cmd, "C %200s", subcmd) != 1)
		goto fail;

	if (strcasecmp(subcmd, "get") == 0) {
		if (sscanf(cmd, "C %*s [%200[^]]]:%200s", section, tag) != 2)
			goto fail;
		v = conf_get_str(section, tag);
		fp = ui_open_result();
		if (fp) {
			if (v)
				fprintf(fp, "%s\n", v);
			fclose(fp);
		}
		LOG_DBG((LOG_UI, 30, "ui_config: \"%s\"", cmd));
		return;
	}

	trans = conf_begin();
	if (strcasecmp(subcmd, "set") == 0) {
		items = sscanf(cmd, "C %*s [%200[^]]]:%200[^=]=%200s %200s",
		    section, tag, value, tmp);
		if (!(items == 3 || items == 4))
			goto fail;
		conf_set(trans, section, tag, value, items == 4 ? 1 : 0, 0);
		if (strcasecmp(section, "Phase 2") == 0 &&
		    (strcasecmp(tag, "Connections") == 0 ||
			strcasecmp(tag, "Passive-connections") == 0))
			ui_conn_reinit();
	} else if (strcasecmp(subcmd, "add") == 0) {
		items = sscanf(cmd, "C %*s [%200[^]]]:%200[^=]=%200s %200s",
		    section, tag, value, tmp);
		if (!(items == 3 || items == 4))
			goto fail;
		v = conf_get_str(section, tag);
		if (!v)
			conf_set(trans, section, tag, value, 1, 0);
		else {
			vlist = conf_get_list(section, tag);
			if (vlist) {
				for (vnode = TAILQ_FIRST(&vlist->fields);
				    vnode;
				    vnode = TAILQ_NEXT(vnode, link)) {
					if (strcmp(vnode->field, value) == 0) {
						skip = 1;
						break;
					}
				}
				conf_free_list(vlist);
			}
			/* Add the new value to the end of the 'v' list.  */
			if (skip == 0) {
				if (asprintf(&nv,
				    v[strlen(v) - 1] == ',' ? "%s%s" : "%s,%s",
				    v, value) == -1) {
					log_error("ui_config: malloc() failed");
					if (trans)
						conf_end(trans, 0);
					return;
				}
				conf_set(trans, section, tag, nv, 1, 0);
				free(nv);
			}
		}
		if (strcasecmp(section, "Phase 2") == 0 &&
		    (strcasecmp(tag, "Connections") == 0 ||
			strcasecmp(tag, "Passive-connections") == 0))
			ui_conn_reinit();
	} else if (strcasecmp(subcmd, "rmv") == 0) {
		items = sscanf(cmd, "C %*s [%200[^]]]:%200[^=]=%200s %200s",
		    section, tag, value, tmp);
		if (!(items == 3 || items == 4))
			goto fail;
		vlist = conf_get_list(section, tag);
		if (vlist) {
			nv = v = NULL;
			for (vnode = TAILQ_FIRST(&vlist->fields);
			    vnode;
			    vnode = TAILQ_NEXT(vnode, link)) {
				if (strcmp(vnode->field, value) == 0)
					continue;
				ret = v ?
				    asprintf(&nv, "%s,%s", v, vnode->field) :
				    asprintf(&nv, "%s", vnode->field);
				free(v);
				if (ret == -1) {
					log_error("ui_config: malloc() failed");
					if (trans)
						conf_end(trans, 0);
					return;
				}
				v = nv;
			}
			conf_free_list(vlist);
			if (nv) {
				conf_set(trans, section, tag, nv, 1, 0);
				free(nv);
			} else {
				conf_remove(trans, section, tag);
			}
		}
		if (strcasecmp(section, "Phase 2") == 0 &&
		    (strcasecmp(tag, "Connections") == 0 ||
			strcasecmp(tag, "Passive-connections") == 0))
			ui_conn_reinit();
	} else if (strcasecmp(subcmd, "rm") == 0) {
		if (sscanf(cmd, "C %*s [%200[^]]]:%200s", section, tag) != 2)
			goto fail;
		conf_remove(trans, section, tag);
	} else if (strcasecmp(subcmd, "rms") == 0) {
		if (sscanf(cmd, "C %*s [%200[^]]]", section) != 1)
			goto fail;
		conf_remove_section(trans, section);
	} else
		goto fail;

	LOG_DBG((LOG_UI, 30, "ui_config: \"%s\"", cmd));
	conf_end(trans, 1);
	return;

fail:
	if (trans)
		conf_end(trans, 0);
	log_print("ui_config: command \"%s\" malformed", cmd);
}

static void
ui_delete(char *cmd)
{
	char            cookies_str[ISAKMP_HDR_COOKIES_LEN * 2 + 1];
	char            message_id_str[ISAKMP_HDR_MESSAGE_ID_LEN * 2 + 1];
	u_int8_t        cookies[ISAKMP_HDR_COOKIES_LEN];
	u_int8_t        message_id_buf[ISAKMP_HDR_MESSAGE_ID_LEN];
	u_int8_t       *message_id = message_id_buf;
	struct sa      *sa;

	if (sscanf(cmd, "d %32s %8s", cookies_str, message_id_str) != 2) {
		log_print("ui_delete: command \"%s\" malformed", cmd);
		return;
	}
	if (strcmp(message_id_str, "-") == 0)
		message_id = 0;

	if (hex2raw(cookies_str, cookies, ISAKMP_HDR_COOKIES_LEN) == -1 ||
	    (message_id && hex2raw(message_id_str, message_id_buf,
	    ISAKMP_HDR_MESSAGE_ID_LEN) == -1)) {
		log_print("ui_delete: command \"%s\" has bad arguments", cmd);
		return;
	}
	sa = sa_lookup(cookies, message_id);
	if (!sa) {
		log_print("ui_delete: command \"%s\" found no SA", cmd);
		return;
	}
	LOG_DBG((LOG_UI, 20,
	    "ui_delete: deleting SA for cookie \"%s\" msgid \"%s\"",
	    cookies_str, message_id_str));
	sa_delete(sa, 1);
}

/* Parse the debug command found in CMD.  */
static void
ui_debug(char *cmd)
{
	int             cls, level;
	char            subcmd[3];

	if (sscanf(cmd, "D %d %d", &cls, &level) == 2) {
		log_debug_cmd(cls, level);
		return;
	} else if (sscanf(cmd, "D %2s %d", subcmd, &level) == 2) {
		switch (subcmd[0]) {
		case 'A':
			for (cls = 0; cls < LOG_ENDCLASS; cls++)
				log_debug_cmd(cls, level);
			return;
		}
	} else if (sscanf(cmd, "D %2s", subcmd) == 1) {
		switch (subcmd[0]) {
		case 'T':
			log_debug_toggle();
			return;
		}
	}
	log_print("ui_debug: command \"%s\" malformed", cmd);
}

static void
ui_packetlog(char *cmd)
{
	char	subcmd[201];

	if (sscanf(cmd, "p %200s", subcmd) != 1)
		goto fail;

	if (strncasecmp(subcmd, "on=", 3) == 0) {
		/* Start capture to a new file.  */
		if (subcmd[strlen(subcmd) - 1] == '\n')
			subcmd[strlen(subcmd) - 1] = 0;
		log_packet_restart(subcmd + 3);
	} else if (strcasecmp(subcmd, "on") == 0)
		log_packet_restart(NULL);
	else if (strcasecmp(subcmd, "off") == 0)
		log_packet_stop();
	return;

fail:
	log_print("ui_packetlog: command \"%s\" malformed", cmd);
}

static void
ui_shutdown_daemon(char *cmd)
{
	if (strlen(cmd) == 1) {
		log_print("ui_shutdown_daemon: received shutdown command");
		daemon_shutdown_now(0);
	} else
		log_print("ui_shutdown_daemon: command \"%s\" malformed", cmd);
}

/* Report SAs and ongoing exchanges.  */
void
ui_report(char *cmd)
{
	/* XXX Skip 'cmd' as arg? */
	sa_report();
	exchange_report();
	transport_report();
	connection_report();
	timer_report();
	conf_report();
}

/* Report all SA configuration information.  */
void
ui_report_sa(char *cmd)
{
	FILE *fp = ui_open_result();

	/* Skip 'cmd' as arg? */
	if (!fp)
		return;

	sa_report_all(fp);

	fclose(fp);
}

static void
ui_setmode(char *cmd)
{
	char	arg[11];

	if (sscanf(cmd, "M %10s", arg) != 1)
		goto fail;
	if (strncmp(arg, "active", 6) == 0) {
		if (ui_daemon_passive) 
			LOG_DBG((LOG_UI, 20,
			    "ui_setmode: switching to active mode"));
		ui_daemon_passive = 0;
	} else if (strncmp(arg, "passive", 7) == 0) {
		if (!ui_daemon_passive) 
			LOG_DBG((LOG_UI, 20,
			    "ui_setmode: switching to passive mode"));
		ui_daemon_passive = 1;
	} else
		goto fail;
	return;
	
  fail:
	log_print("ui_setmode: command \"%s\" malformed", cmd);
}


/*
 * Call the relevant command handler based on the first character of the
 * line (the command).
 */
static void
ui_handle_command(char *line)
{
	/* Find out what one-letter command was sent.  */
	switch (line[0]) {
	case 'c':
		ui_connect(line);
		break;

	case 'C':
		ui_config(line);
		break;

	case 'd':
		ui_delete(line);
		break;

	case 'D':
		ui_debug(line);
		break;

	case 'M':
		ui_setmode(line);
		break;

	case 'p':
		ui_packetlog(line);
		break;

	case 'Q':
		ui_shutdown_daemon(line);
		break;

	case 'R':
		reinit();
		break;

	case 'S':
		ui_report_sa(line);
		break;

	case 'r':
		ui_report(line);
		break;

	case 't':
		ui_teardown(line);
		break;

	case 'T':
		ui_teardown_all(line);
		break;

	default:
		log_print("ui_handle_messages: unrecognized command: '%c'",
		    line[0]);
	}
}

/*
 * A half-complex implementation of reading from a file descriptor
 * line by line without resorting to stdio which apparently have
 * troubles with non-blocking fifos.
 */
void
ui_handler(void)
{
	static char    *buf = 0;
	static char    *p;
	static size_t   sz;
	static size_t   resid;
	ssize_t         n;
	char           *new_buf;

	/* If no buffer, set it up.  */
	if (!buf) {
		sz = BUF_SZ;
		buf = malloc(sz);
		if (!buf) {
			log_print("ui_handler: malloc (%lu) failed",
			    (unsigned long)sz);
			return;
		}
		p = buf;
		resid = sz;
	}
	/* If no place left in the buffer reallocate twice as large.  */
	if (!resid) {
		new_buf = reallocarray(buf, sz, 2);
		if (!new_buf) {
			log_print("ui_handler: realloc (%p, %lu) failed", buf,
			    (unsigned long)sz * 2);
			free(buf);
			buf = 0;
			return;
		}
		buf = new_buf;
		p = buf + sz;
		resid = sz;
		sz *= 2;
	}
	n = read(ui_socket, p, resid);
	if (n == -1) {
		log_error("ui_handler: read (%d, %p, %lu)", ui_socket, p,
		    (unsigned long)resid);
		return;
	}
	if (!n)
		return;
	resid -= n;
	while (n--) {
		/*
		 * When we find a newline, cut off the line and feed it to the
		 * command processor.  Then move the rest up-front.
		 */
		if (*p == '\n') {
			*p = '\0';
			ui_handle_command(buf);
			memmove(buf, p + 1, n);
			p = buf;
			resid = sz - n;
			continue;
		}
		p++;
	}
}

static FILE *
ui_open_result(void)
{
	FILE *fp = monitor_fopen(RESULT_FILE, "w");

	if (!fp)
		log_error("ui_open_result: fopen() failed");
	return fp;
}
