/*	$OpenBSD: group.c,v 1.4 2014/11/18 20:54:28 krw Exp $ */

/*
 * Copyright (c) 2006 Esben Norby <norby@openbsd.org>
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <event.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "dvmrpe.h"
#include "log.h"
#include "control.h"

void	dead_timer(int, short, void *);
int	start_dead_timer(struct group *);
int	start_dead_timer_all(struct group *);
int	stop_dead_timer(struct group *);

void	v1_host_timer(int, short, void *);
int	start_v1_host_timer(struct group *);
int	stop_v1_host_timer(struct group *);

void	retrans_timer(int, short, void *);
int	start_retrans_timer(struct group *);
int	stop_retrans_timer(struct group *);

extern struct dvmrpd_conf	*deconf;

#define MAX_ACTIONS	4

struct {
	int			state;
	enum group_event	event;
	enum group_action	action[MAX_ACTIONS];
	int			new_state;
} grp_fsm[] = {
    /* current state		event that happened	action(s) to take		resulting state */
    /* querier fsm */
    {GRP_STA_NO_MEMB_PRSNT,	GRP_EVT_V2_REPORT_RCVD,	{GRP_ACT_ADD_GROUP,
							 GRP_ACT_START_TMR,
							 GRP_ACT_END},			GRP_STA_MEMB_PRSNT},
    {GRP_STA_NO_MEMB_PRSNT,	GRP_EVT_V1_REPORT_RCVD,	{GRP_ACT_ADD_GROUP,
							 GRP_ACT_START_TMR,
							 GRP_ACT_START_V1_HOST_TMR,
							 GRP_ACT_END},			GRP_STA_MEMB_PRSNT},

    {GRP_STA_MEMB_PRSNT,	GRP_EVT_TMR_EXPIRED,	{GRP_ACT_DEL_GROUP,
							 GRP_ACT_END},			GRP_STA_NO_MEMB_PRSNT},
    {GRP_STA_MEMB_PRSNT,	GRP_EVT_V2_REPORT_RCVD,	{GRP_ACT_START_TMR,
							 GRP_ACT_END},			0},
    {GRP_STA_MEMB_PRSNT,	GRP_EVT_V1_REPORT_RCVD,	{GRP_ACT_START_TMR,
							 GRP_ACT_START_V1_HOST_TMR,
							 GRP_ACT_END},			GRP_STA_V1_MEMB_PRSNT},
    {GRP_STA_MEMB_PRSNT,	GRP_EVT_LEAVE_RCVD,	{GRP_ACT_START_TMR_ALL,
							 GRP_ACT_START_RETRANS_TMR,
							 GRP_ACT_SEND_GRP_QUERY,
							 GRP_ACT_END},			GRP_STA_CHECK_MEMB},

    {GRP_STA_CHECK_MEMB,	GRP_EVT_V2_REPORT_RCVD,	{GRP_ACT_START_TMR,
							 GRP_ACT_END},			GRP_STA_MEMB_PRSNT},
    {GRP_STA_CHECK_MEMB,	GRP_EVT_TMR_EXPIRED,	{GRP_ACT_DEL_GROUP,
							 GRP_ACT_CLR_RETRANS_TMR,
							 GRP_ACT_END},			GRP_STA_NO_MEMB_PRSNT},
    {GRP_STA_CHECK_MEMB,	GRP_EVT_RETRANS_TMR_EXP,{GRP_ACT_SEND_GRP_QUERY,
							 GRP_ACT_START_RETRANS_TMR,
							 GRP_ACT_END},			0},
    {GRP_STA_CHECK_MEMB,	GRP_EVT_V1_REPORT_RCVD,	{GRP_ACT_START_TMR,
							 GRP_ACT_START_V1_HOST_TMR,
							 GRP_ACT_END},			GRP_STA_V1_MEMB_PRSNT},

    {GRP_STA_V1_MEMB_PRSNT,	GRP_EVT_V1_HOST_TMR_EXP,{GRP_ACT_NOTHING,
							 GRP_ACT_END},			GRP_STA_MEMB_PRSNT},
    {GRP_STA_V1_MEMB_PRSNT,	GRP_EVT_V1_REPORT_RCVD,	{GRP_ACT_START_TMR,
							 GRP_ACT_START_V1_HOST_TMR,
							 GRP_ACT_END},			0},
    {GRP_STA_V1_MEMB_PRSNT,	GRP_EVT_V2_REPORT_RCVD,	{GRP_ACT_START_TMR,
							 GRP_ACT_END},			0},
    {GRP_STA_V1_MEMB_PRSNT,	GRP_EVT_TMR_EXPIRED,	{GRP_ACT_DEL_GROUP,
							 GRP_ACT_END},			GRP_STA_NO_MEMB_PRSNT},

    /* non querier fsm */
    {GRP_STA_NO_MEMB_PRSNT,	GRP_EVT_REPORT_RCVD,	{GRP_ACT_ADD_GROUP,
							 GRP_ACT_START_TMR,
							 GRP_ACT_END},			GRP_STA_MEMB_PRSNT},
    {GRP_STA_MEMB_PRSNT,	GRP_EVT_REPORT_RCVD,	{GRP_ACT_START_TMR,
							 GRP_ACT_END},			0},
    {GRP_STA_MEMB_PRSNT,	GRP_EVT_QUERY_RCVD,	{GRP_ACT_START_TMR_ALL,
							 GRP_ACT_END},			GRP_STA_CHECK_MEMB},

    {GRP_STA_CHECK_MEMB,	GRP_EVT_REPORT_RCVD,	{GRP_ACT_START_TMR,
							 GRP_ACT_END},			GRP_STA_MEMB_PRSNT},
    {-1,			GRP_EVT_NOTHING,	{GRP_ACT_NOTHING,
							 GRP_ACT_END},			0},
};

const char * const group_action_names[] = {
	"END MARKER",
	"START TIMER",
	"START ALL TIMER",
	"START RETRANSMISSION TIMER",
	"START V1 HOST TIMER",
	"SEND GROUP QUERY",
	"ADD GROUP",
	"DEL GROUP",
	"CLEAR RETRANSMISSION TIMER",
	"NOTHING"
};

static const char * const group_event_names[] = {
	"V2 REPORT RCVD",
	"V1 REPORT RCVD",
	"LEAVE RCVD",
	"TIMER EXPIRED",
	"RETRANS TIMER EXPIRED",
	"V1 HOST TIMER EXPIRED",
	"REPORT RCVD",
	"QUERY RCVD",
	"NOTHING"
};

int
group_fsm(struct group *group, enum group_event event)
{
	struct mfc	mfc;
	int		old_state;
	int		new_state = 0;
	int		i, j, ret = 0;

	old_state = group->state;

	for (i = 0; grp_fsm[i].state != -1; i++)
		if ((grp_fsm[i].state & old_state) &&
		    (grp_fsm[i].event == event)) {
			new_state = grp_fsm[i].new_state;
			break;
		}

	if (grp_fsm[i].state == -1) {
		/* XXX event outside of the defined fsm, ignore it. */
		log_debug("group_fsm: group %s, event '%s' not expected in "
		    "state '%s'", inet_ntoa(group->addr),
		    group_event_name(event), group_state_name(old_state));
		return (0);
	}

	for (j = 0; grp_fsm[i].action[j] != GRP_ACT_END; j++) {
		switch (grp_fsm[i].action[j]) {
		case GRP_ACT_START_TMR:
			ret = start_dead_timer(group);
			break;
		case GRP_ACT_START_TMR_ALL:
			ret = start_dead_timer_all(group);
			break;
		case GRP_ACT_START_RETRANS_TMR:
			ret = start_retrans_timer(group);
			break;
		case GRP_ACT_START_V1_HOST_TMR:
			ret = start_v1_host_timer(group);
			break;
		case GRP_ACT_SEND_GRP_QUERY:
			ret = send_igmp_query(group->iface, group);
			break;
		case GRP_ACT_ADD_GROUP:
			mfc.origin.s_addr = 0;
			mfc.group = group->addr;
			mfc.ifindex = group->iface->ifindex;
			dvmrpe_imsg_compose_rde(IMSG_GROUP_ADD, 0, 0, &mfc,
			    sizeof(mfc));
			break;
		case GRP_ACT_DEL_GROUP:
			mfc.origin.s_addr = 0;
			mfc.group = group->addr;
			mfc.ifindex = group->iface->ifindex;
			dvmrpe_imsg_compose_rde(IMSG_GROUP_DEL, 0, 0, &mfc,
			    sizeof(mfc));
			break;
		case GRP_ACT_CLR_RETRANS_TMR:
			ret = stop_retrans_timer(group);
			break;
		case GRP_ACT_NOTHING:
		case GRP_ACT_END:
			/* do nothing */
			break;
		}

		if (ret) {
			log_debug("group_fsm: error changing state for "
			    "group %s, event '%s', state '%s'",
			    inet_ntoa(group->addr), group_event_name(event),
			    group_state_name(old_state));
			return (-1);
		}
	}

	if (new_state != 0)
		group->state = new_state;

	for (j = 0; grp_fsm[i].action[j] != GRP_ACT_END; j++)
		log_debug("group_fsm: event '%s' resulted in action '%s' and "
		    "changing state for group %s from '%s' to '%s'",
		    group_event_name(event),
		    group_action_name(grp_fsm[i].action[j]),
		    inet_ntoa(group->addr), group_state_name(old_state),
		    group_state_name(group->state));

	return (ret);
}

/* timers */
void
dead_timer(int fd, short event, void *arg)
{
	struct group *group = arg;

	log_debug("dead_timer: %s", inet_ntoa(group->addr));

	group_fsm(group, GRP_EVT_TMR_EXPIRED);
}

int
start_dead_timer(struct group *group)
{
	struct timeval	tv;

	log_debug("start_dead_timer: %s", inet_ntoa(group->addr));

	/* Group Membership Interval */
	timerclear(&tv);
	tv.tv_sec = group->iface->robustness * group->iface->query_interval +
	    (group->iface->query_resp_interval / 2);

	return (evtimer_add(&group->dead_timer, &tv));
}

int
start_dead_timer_all(struct group *group)
{
	struct timeval	tv;

	log_debug("start_dead_timer_all: %s", inet_ntoa(group->addr));

	timerclear(&tv);
	if (group->iface->state == IF_STA_QUERIER) {
		/* querier */
		tv.tv_sec = group->iface->last_member_query_interval *
		    group->iface->last_member_query_cnt;
	} else {
		/* non querier */
		/* XXX max response time received in packet */
		tv.tv_sec = group->iface->recv_query_resp_interval *
		    group->iface->last_member_query_cnt;
	}

	return (evtimer_add(&group->dead_timer, &tv));
}

int
stop_dead_timer(struct group *group)
{
	log_debug("stop_dead_timer: %s", inet_ntoa(group->addr));

	return (evtimer_del(&group->dead_timer));
}

void
v1_host_timer(int fd, short event, void *arg)
{
	struct group *group = arg;

	log_debug("v1_host_timer: %s", inet_ntoa(group->addr));

	group_fsm(group, GRP_EVT_V1_HOST_TMR_EXP);
}

int
start_v1_host_timer(struct group *group)
{
	struct timeval	tv;

	log_debug("start_v1_host_timer: %s", inet_ntoa(group->addr));

	/* Group Membership Interval */
	timerclear(&tv);
	tv.tv_sec = group->iface->robustness * group->iface->query_interval +
	    (group->iface->query_resp_interval / 2);

	return (evtimer_add(&group->v1_host_timer, &tv));
}

int
stop_v1_host_timer(struct group *group)
{
	log_debug("stop_v1_host_timer: %s", inet_ntoa(group->addr));

	return (evtimer_del(&group->v1_host_timer));
}

void
retrans_timer(int fd, short event, void *arg)
{
	struct group *group = arg;
	struct timeval tv;

	log_debug("retrans_timer: %s", inet_ntoa(group->addr));

	send_igmp_query(group->iface, group);

	/* reschedule retrans_timer */
	if (group->state == GRP_STA_CHECK_MEMB) {
		timerclear(&tv);
		tv.tv_sec = group->iface->last_member_query_interval;
		evtimer_add(&group->retrans_timer, &tv);
	}
}

int
start_retrans_timer(struct group *group)
{
	struct timeval	tv;

	log_debug("start_retrans_timer: %s", inet_ntoa(group->addr));

	timerclear(&tv);
	tv.tv_sec = group->iface->last_member_query_interval;

	return (evtimer_add(&group->retrans_timer, &tv));
}

int
stop_retrans_timer(struct group *group)
{
	log_debug("stop_retrans_timer: %s", inet_ntoa(group->addr));

	return (evtimer_del(&group->retrans_timer));
}

/* group list */
struct group *
group_list_add(struct iface *iface, u_int32_t group)
{
	struct group	*ge;
	struct timeval	 now;

	/* validate group id */
	if (!IN_MULTICAST(htonl(group)))
		fatalx("group_list_add: invalid group");

	if ((ge = group_list_find(iface, group)) != NULL) {
		return (ge);
	}

	if ((ge = calloc(1, sizeof(*ge))) == NULL)
		fatal("group_list_add");

	ge->addr.s_addr = group;
	ge->state = GRP_STA_NO_MEMB_PRSNT;
	evtimer_set(&ge->dead_timer, dead_timer, ge);
	evtimer_set(&ge->v1_host_timer, v1_host_timer, ge);
	evtimer_set(&ge->retrans_timer, retrans_timer, ge);

	gettimeofday(&now, NULL);
	ge->uptime = now.tv_sec;

	TAILQ_INSERT_TAIL(&iface->group_list, ge, entry);
	iface->group_cnt++;

	ge->iface = iface;

	log_debug("group_list_add: interface %s, group %s", iface->name,
	    inet_ntoa(ge->addr));

	return (ge);
}

void
group_list_remove(struct iface *iface, struct group *group)
{
	log_debug("group_list_remove: interface %s, group %s", iface->name,
	    inet_ntoa(group->addr));

	/* stop timers */
	stop_dead_timer(group);
	start_v1_host_timer(group);
	stop_retrans_timer(group);

	TAILQ_REMOVE(&iface->group_list, group, entry);
	free(group);
	iface->group_cnt--;
}

struct group *
group_list_find(struct iface *iface, u_int32_t group)
{
	struct group	*ge = NULL;

	/* validate group id */
	if (!IN_MULTICAST(htonl(group)))
		fatalx("group_list_find: invalid group");

	TAILQ_FOREACH(ge, &iface->group_list, entry) {
		if (ge->addr.s_addr == group)
			return (ge);
	}

	return (ge);
}

void
group_list_clr(struct iface *iface)
{
	struct group	*ge;

	while ((ge = TAILQ_FIRST(&iface->group_list)) != NULL) {
		TAILQ_REMOVE(&iface->group_list, ge, entry);
		free(ge);
	}
	iface->group_cnt = 0;
}

int
group_list_empty(struct iface *iface)
{
	return (TAILQ_EMPTY(&iface->group_list));
}

void
group_list_dump(struct iface *iface, struct ctl_conn *c)
{
	struct group		*ge;
	struct ctl_group	*gctl;

	TAILQ_FOREACH(ge, &iface->group_list, entry) {
		gctl = group_to_ctl(ge);
		imsg_compose_event(&c->iev, IMSG_CTL_SHOW_IGMP, 0, 0,
		    -1, gctl, sizeof(struct ctl_group));
	}
}

/* names */
const char *
group_event_name(int event)
{
	return (group_event_names[event]);
}

const char *
group_action_name(int action)
{
	return (group_action_names[action]);
}

struct ctl_group *
group_to_ctl(struct group *group)
{
	static struct ctl_group	 gctl;
	struct timeval		 tv, now, res;

	memcpy(&gctl.addr, &group->addr, sizeof(gctl.addr));

	gctl.state = group->state;

	gettimeofday(&now, NULL);
	if (evtimer_pending(&group->dead_timer, &tv)) {
		timersub(&tv, &now, &res);
		gctl.dead_timer = res.tv_sec;
	} else
		gctl.dead_timer = 0;

	if (group->state != GRP_STA_NO_MEMB_PRSNT) {
		gctl.uptime = now.tv_sec - group->uptime;
	} else
		gctl.uptime = 0;

	return (&gctl);
}
