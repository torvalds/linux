/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011 Hiroki Sato <hrs@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "pathnames.h"
#include "rtadvd.h"
#include "if.h"
#include "config.h"
#include "control.h"
#include "control_server.h"
#include "timer.h"

static char *do_reload_ifname;
static int do_reload;
static int do_shutdown;

void set_do_reload(int sig __unused)	{ do_reload = 1; }
void set_do_reload_ifname(char *ifname){ do_reload_ifname = ifname; }
void set_do_shutdown(int sig __unused)	{ do_shutdown = 1; }
void reset_do_reload(void)	{ do_reload = 0; do_reload_ifname = NULL; }
void reset_do_shutdown(void)	{ do_shutdown = 0; }
int is_do_reload(void)		{ return (do_reload); }
int is_do_shutdown(void)	{ return (do_shutdown); }
char *reload_ifname(void)	{ return (do_reload_ifname); }

#define	DEF_PL_HANDLER(key)	{ #key, cm_getprop_##key }

static int cm_getprop_echo(struct ctrl_msg_pl *);
static int cm_getprop_version(struct ctrl_msg_pl *);
static int cm_getprop_ifilist(struct ctrl_msg_pl *);
static int cm_getprop_ifi(struct ctrl_msg_pl *);
static int cm_getprop_ifi_ra_timer(struct ctrl_msg_pl *);
static int cm_getprop_rai(struct ctrl_msg_pl *);
static int cm_getprop_pfx(struct ctrl_msg_pl *);
static int cm_getprop_rdnss(struct ctrl_msg_pl *);
static int cm_getprop_dnssl(struct ctrl_msg_pl *);
static int cm_getprop_rti(struct ctrl_msg_pl *);

static int cm_setprop_reload(struct ctrl_msg_pl *);
static int cm_setprop_enable(struct ctrl_msg_pl *);
static int cm_setprop_disable(struct ctrl_msg_pl *);

static struct dispatch_table {
	const char	*dt_comm;
	int		(*dt_act)(struct ctrl_msg_pl *cp);
} getprop_dtable[] = {
	{ "",	cm_getprop_echo },
	DEF_PL_HANDLER(echo),
	DEF_PL_HANDLER(version),
	DEF_PL_HANDLER(ifilist),
	DEF_PL_HANDLER(ifi),
	DEF_PL_HANDLER(ifi_ra_timer),
	DEF_PL_HANDLER(rai),
	DEF_PL_HANDLER(rti),
	DEF_PL_HANDLER(pfx),
	DEF_PL_HANDLER(rdnss),
	DEF_PL_HANDLER(dnssl),
};

static int
cm_getprop_echo(struct ctrl_msg_pl *cp)
{

	syslog(LOG_DEBUG, "<%s> enter", __func__);
	cp->cp_val = strdup("");
	cp->cp_val_len = strlen(cp->cp_val) + 1;

	return (0);
}

static int
cm_getprop_version(struct ctrl_msg_pl *cp)
{

	syslog(LOG_DEBUG, "<%s> enter", __func__);
	cp->cp_val = strdup(CM_VERSION_STR);
	cp->cp_val_len = strlen(cp->cp_val) + 1;

	return (0);
}

static int
cm_getprop_ifilist(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;
	char *p;
	size_t len;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	len = 0;
	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		len += strlen(ifi->ifi_ifname) + 1;
	}

	syslog(LOG_DEBUG, "<%s> len = %zu", __func__, len);

	p = malloc(len);
	if (p == NULL)
		exit(1);
	memset(p, 0, len);
	cp->cp_val = p;

	if (len > 0)
		TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
			syslog(LOG_DEBUG, "<%s> add ifname=%s(%d)",
			    __func__, ifi->ifi_ifname, ifi->ifi_ifindex);
			strcpy(p, ifi->ifi_ifname);
			p += strlen(ifi->ifi_ifname) + 1;
		}
	cp->cp_val_len = p - cp->cp_val;

	return (0);
}

static int
cm_getprop_ifi(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;
	char *p;
	size_t len;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}

	p = malloc(sizeof(*ifi));
	if (p == NULL)
		exit(1);
	len = cm_str2bin(p, ifi, sizeof(*ifi));

	syslog(LOG_DEBUG, "<%s> len = %zu", __func__, len);

	if (len == 0)
		return (1);

	cp->cp_val = p;
	cp->cp_val_len = len;

	return (0);
}

static int
cm_getprop_rai(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;
	struct rainfo *rai;
	char *p;
	size_t len;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}
	if ((rai = ifi->ifi_rainfo) == NULL) {
		syslog(LOG_ERR, "<%s> %s has no rainfo", __func__,
		    cp->cp_ifname);
		return (1);
	}

	p = malloc(sizeof(*rai));
	if (p == NULL)
		exit(1);
	len = cm_str2bin(p, rai, sizeof(*rai));

	syslog(LOG_DEBUG, "<%s> len = %zu", __func__, len);

	if (len == 0)
		return (1);

	cp->cp_val = p;
	cp->cp_val_len = len;

	return (0);
}

static int
cm_getprop_ifi_ra_timer(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;
	struct rainfo *rai;
	struct rtadvd_timer	*rtimer;
	char *p;
	size_t len;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}
	if ((rai = ifi->ifi_rainfo) == NULL) {
		syslog(LOG_ERR, "<%s> %s has no rainfo", __func__,
		    cp->cp_ifname);
		return (1);
	}
	if ((rtimer = ifi->ifi_ra_timer) == NULL) {
		syslog(LOG_ERR, "<%s> %s has no ifi_ra_timer", __func__,
		    cp->cp_ifname);
		return (1);
	}
	p = malloc(sizeof(*rtimer));
	if (p == NULL)
		exit(1);
	len = cm_str2bin(p, rtimer, sizeof(*rtimer));

	syslog(LOG_DEBUG, "<%s> len = %zu", __func__, len);

	if (len == 0)
		return (1);

	cp->cp_val = p;
	cp->cp_val_len = len;

	return (0);
}

static int
cm_getprop_rti(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;
	struct rainfo *rai;
	struct rtinfo *rti;
	char *p;
	size_t len;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	len = 0;
	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}
	if (ifi->ifi_rainfo == NULL) {
		syslog(LOG_ERR, "<%s> %s has no rainfo", __func__,
		    cp->cp_ifname);
		return (1);
	}
	rai = ifi->ifi_rainfo;
	TAILQ_FOREACH(rti, &rai->rai_route, rti_next) {
		len += sizeof(*rti);
	}

	syslog(LOG_DEBUG, "<%s> len = %zu", __func__, len);

	p = malloc(len);
	if (p == NULL)
		exit(1);
	memset(p, 0, len);
	cp->cp_val = p;

	if (len > 0)
		TAILQ_FOREACH(rti, &rai->rai_route, rti_next) {
			memcpy(p, rti, sizeof(*rti));
			p += sizeof(*rti);
		}
	cp->cp_val_len = p - cp->cp_val;

	return (0);
}

static int
cm_getprop_pfx(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;
	struct rainfo *rai;
	struct prefix *pfx;
	char *p;
	size_t len;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	len = 0;
	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}
	if (ifi->ifi_rainfo == NULL) {
		syslog(LOG_ERR, "<%s> %s has no rainfo", __func__,
		    cp->cp_ifname);
		return (1);
	}
	rai = ifi->ifi_rainfo;
	TAILQ_FOREACH(pfx, &rai->rai_prefix, pfx_next) {
		len += sizeof(*pfx);
	}

	syslog(LOG_DEBUG, "<%s> len = %zu", __func__, len);

	p = malloc(len);
	if (p == NULL)
		exit(1);
	memset(p, 0, len);
	cp->cp_val = p;

	if (len > 0)
		TAILQ_FOREACH(pfx, &rai->rai_prefix, pfx_next) {
			memcpy(p, pfx, sizeof(*pfx));
			p += sizeof(*pfx);
		}
	cp->cp_val_len = p - cp->cp_val;

	return (0);
}

static int
cm_getprop_rdnss(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;
	struct rainfo *rai;
	struct rdnss *rdn;
	struct rdnss_addr *rda;
	char *p;
	size_t len;
	uint16_t *rdn_cnt;
	uint16_t *rda_cnt;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	len = 0;
	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}
	if (ifi->ifi_rainfo == NULL) {
		syslog(LOG_ERR, "<%s> %s has no rainfo", __func__,
		    cp->cp_ifname);
		return (1);
	}
	rai = ifi->ifi_rainfo;

	len = sizeof(*rdn_cnt);
	TAILQ_FOREACH(rdn, &rai->rai_rdnss, rd_next) {
		len += sizeof(*rdn);
		len += sizeof(*rda_cnt);
		TAILQ_FOREACH(rda, &rdn->rd_list, ra_next) {
			len += sizeof(*rda);
		}
	}

	syslog(LOG_DEBUG, "<%s> len = %zu", __func__, len);

	p = malloc(len);
	if (p == NULL)
		exit(1);
	memset(p, 0, len);
	cp->cp_val = p;

	rdn_cnt = (uint16_t *)p;
	p += sizeof(*rdn_cnt);
	TAILQ_FOREACH(rdn, &rai->rai_rdnss, rd_next) {
		*rdn_cnt += 1;
		memcpy(p, rdn, sizeof(*rdn));
		p += sizeof(*rdn);

		rda_cnt = (uint16_t *)p;
		p += sizeof(*rda_cnt);
		TAILQ_FOREACH(rda, &rdn->rd_list, ra_next) {
			*rda_cnt += 1;
			memcpy(p, rda, sizeof(*rda));
			p += sizeof(*rda);
		}
	}
	syslog(LOG_DEBUG, "<%s> rdn_cnt = %d", __func__, *rdn_cnt);
	cp->cp_val_len = p - cp->cp_val;

	return (0);
}

static int
cm_getprop_dnssl(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;
	struct rainfo *rai;
	struct dnssl *dns;
	struct dnssl_addr *dna;
	char *p;
	size_t len;
	uint16_t *dns_cnt;
	uint16_t *dna_cnt;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	len = 0;
	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}
	if (ifi->ifi_rainfo == NULL) {
		syslog(LOG_ERR, "<%s> %s has no rainfo", __func__,
		    cp->cp_ifname);
		return (1);
	}
	rai = ifi->ifi_rainfo;

	len = sizeof(*dns_cnt);
	TAILQ_FOREACH(dns, &rai->rai_dnssl, dn_next) {
		len += sizeof(*dns);
		len += sizeof(*dna_cnt);
		TAILQ_FOREACH(dna, &dns->dn_list, da_next) {
			len += sizeof(*dna);
		}
	}

	syslog(LOG_DEBUG, "<%s> len = %zu", __func__, len);

	p = malloc(len);
	if (p == NULL)
		exit(1);
	memset(p, 0, len);
	cp->cp_val = p;

	dns_cnt = (uint16_t *)cp->cp_val;
	p += sizeof(*dns_cnt);
	TAILQ_FOREACH(dns, &rai->rai_dnssl, dn_next) {
		(*dns_cnt)++;
		memcpy(p, dns, sizeof(*dns));
		p += sizeof(*dns);

		dna_cnt = (uint16_t *)p;
		p += sizeof(*dna_cnt);
		TAILQ_FOREACH(dna, &dns->dn_list, da_next) {
			(*dna_cnt)++;
			memcpy(p, dna, sizeof(*dna));
			p += sizeof(*dna);
		}
	}
	cp->cp_val_len = p - cp->cp_val;

	return (0);
}

int
cm_getprop(struct ctrl_msg_pl *cp)
{
	size_t i;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (cp == NULL)
		return (1);

	for (i = 0;
	     i < sizeof(getprop_dtable) / sizeof(getprop_dtable[0]);
	     i++) {
		if (strcmp(cp->cp_key, getprop_dtable[i].dt_comm) == 0)
			return (getprop_dtable[i].dt_act(cp));
	}
	return (1);
}

int
cm_setprop(struct ctrl_msg_pl *cp)
{
	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (cp == NULL || cp->cp_key == NULL)
		return (1);

	if (strncmp(cp->cp_key, "reload", sizeof("reload")) == 0)
		cm_setprop_reload(cp);
	else if (strncmp(cp->cp_key, "shutdown", sizeof("shutdown")) == 0)
		set_do_shutdown(0);
	else if (strncmp(cp->cp_key, "enable", sizeof("enable")) == 0)
		cm_setprop_enable(cp);
	else if (strncmp(cp->cp_key, "disable", sizeof("disable")) == 0)
		cm_setprop_disable(cp);
	else if (strncmp(cp->cp_key, "echo", 8) == 0)
		; 		/* do nothing */
	else
		return (1);

	return (0);
}

static int
cm_setprop_reload(struct ctrl_msg_pl *cp)
{

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	set_do_reload_ifname(cp->cp_ifname);
	set_do_reload(1);

	return (0);
}

static int
cm_setprop_enable(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}

	ifi->ifi_persist = 1;
	set_do_reload_ifname(ifi->ifi_ifname);
	set_do_reload(0);

	return (0);
}

static int
cm_setprop_disable(struct ctrl_msg_pl *cp)
{
	struct ifinfo *ifi;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (strcmp(cp->cp_ifname, ifi->ifi_ifname) == 0)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_ERR, "<%s> %s not found", __func__,
		    cp->cp_ifname);
		return (1);
	}

	if (ifi->ifi_persist == 1) {
		ifi->ifi_persist = 0;
		rm_ifinfo(ifi);

		/* MC leaving needed here */
		sock_mc_leave(&sock, ifi->ifi_ifindex);

		set_do_reload_ifname(ifi->ifi_ifname);
		set_do_reload(0);
	}

	return (0);
}

int
cm_handler_server(int fd)
{
	int state;
	char *msg;
	struct ctrl_msg_hdr *cm;
	struct ctrl_msg_pl cp;
	char buf[CM_MSG_MAXLEN];
	char pbuf[CM_MSG_MAXLEN];
	int error;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	memset(buf, 0, sizeof(buf));
	memset(pbuf, 0, sizeof(pbuf));
	cm = (struct ctrl_msg_hdr *)buf;
	msg = (char *)buf + sizeof(*cm);

	state = CM_STATE_INIT;
	while (state != CM_STATE_EOM) {
		syslog(LOG_DEBUG, "<%s> state = %d", __func__, state);

		switch (state) {
		case CM_STATE_INIT:
			state = CM_STATE_MSG_RECV;
			break;
		case CM_STATE_MSG_DISPATCH:
			cm->cm_version = CM_VERSION;
			error = cm_send(fd, buf);
			if (error)
				syslog(LOG_WARNING,
				    "<%s> cm_send()", __func__);
			state = CM_STATE_EOM;
			break;
		case CM_STATE_ACK_WAIT:
			error = cm_recv(fd, buf);
			if (error) {
				syslog(LOG_ERR,
				    "<%s> cm_recv()", __func__);
				close(fd);
				return (-1);
			}

			switch (cm->cm_type) {
			case CM_TYPE_ACK:
				break;
			case CM_TYPE_ERR:
				syslog(LOG_DEBUG,
				    "<%s> CM_TYPE_ERR", __func__);
				close(fd);
				return (-1);
			default:
				syslog(LOG_DEBUG,
				    "<%s> unknown status", __func__);
				close(fd);
				return (-1);
			}
			state = CM_STATE_EOM;
			break;
		case CM_STATE_MSG_RECV:
			error = cm_recv(fd, buf);

			if (error) {
				syslog(LOG_ERR,
				    "<%s> cm_recv()", __func__);
				close(fd);
				return (-1);
			}
			memset(&cp, 0, sizeof(cp));

			syslog(LOG_DEBUG,
			    "<%s> cm->cm_type = %d", __func__, cm->cm_type);
			syslog(LOG_DEBUG,
			    "<%s> cm->cm_len = %zu", __func__, cm->cm_len);

			switch (cm->cm_type) {
			case CM_TYPE_EOM:
				state = CM_STATE_EOM;
			case CM_TYPE_NUL:
				cm->cm_type = CM_TYPE_ACK;
				cm->cm_len = sizeof(*cm);
				break;
			case CM_TYPE_REQ_GET_PROP:
				cm_bin2pl(msg, &cp);
				error = cm_getprop(&cp);
				if (error) {
					cm->cm_type = CM_TYPE_ERR;
					cm->cm_len = sizeof(*cm);
				} else {
					cm->cm_type = CM_TYPE_ACK;
					cm->cm_len = sizeof(*cm);
					cm->cm_len += cm_pl2bin(msg, &cp);
				}
				if (cp.cp_val != NULL)
					free(cp.cp_val);
				break;
			case CM_TYPE_REQ_SET_PROP:
				cm_bin2pl(msg, &cp);
				error = cm_setprop(&cp);
				if (error) {
					cm->cm_type = CM_TYPE_ERR;
					cm->cm_len = sizeof(*cm);
				} else {
					cm->cm_type = CM_TYPE_ACK;
					cm->cm_len = sizeof(*cm);
				}
				break;
			default:
				cm->cm_type = CM_TYPE_ERR;
				cm->cm_len = sizeof(*cm);
			}

			switch (cm->cm_type) {
			case CM_TYPE_ERR:
			case CM_TYPE_ACK:
				state = CM_STATE_MSG_DISPATCH;
				break;
			}
		}
	}
	syslog(LOG_DEBUG, "<%s> leave", __func__);

	return (0);
}
