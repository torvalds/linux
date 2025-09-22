/*	$OpenBSD: carp.c,v 1.19 2024/04/23 13:34:51 jsg Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
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
 * This code was written under funding by Multicom Security AB.
 */


#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "monitor.h"
#include "sasyncd.h"

int carp_demoted = 0;

/* Map CARP interface link state into RUNSTATE enum */
static enum RUNSTATE
carp_map_state(u_char link_state)
{
	enum RUNSTATE state = FAIL;

	switch(link_state) {
	case LINK_STATE_UP:
	case LINK_STATE_HALF_DUPLEX:
	case LINK_STATE_FULL_DUPLEX:
		state = MASTER;
		break;
	case LINK_STATE_DOWN:
		state = SLAVE;
		break;
	case LINK_STATE_UNKNOWN:
	case LINK_STATE_INVALID:
		state = INIT;
		break;
	}

	return state;
}

static enum RUNSTATE
carp_get_state(char *ifname)
{
	struct ifreq	ifr;
	struct if_data	ifrdat;
	int		s, saved_errno;

	if (!ifname || !*ifname) {
		errno = ENOENT;
		return FAIL;
	}

	memset(&ifr, 0, sizeof ifr);
	strlcpy(ifr.ifr_name, ifname, sizeof ifr.ifr_name);

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return FAIL;

	ifr.ifr_data = (caddr_t)&ifrdat;
	if (ioctl(s, SIOCGIFDATA, (caddr_t)&ifr) == -1) {
		saved_errno = errno;
		close(s);
		errno = saved_errno;
		return FAIL;
	}
	close(s);
	return carp_map_state(ifrdat.ifi_link_state);
}

void
carp_demote(int demote, int force)
{
	struct ifgroupreq        ifgr;
	int s;

	if (carp_demoted + demote < 0) {
		log_msg(1, "carp_demote: mismatched promotion");
		return;
	}

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		log_msg(1, "carp_demote: couldn't open socket");
		return;
	}

	bzero(&ifgr, sizeof(ifgr));
	strlcpy(ifgr.ifgr_name, cfgstate.carp_ifgroup, sizeof(ifgr.ifgr_name));

	/* Unless we force it, don't demote if we're not demoting already. */
	if (!force) {
		if (ioctl(s, SIOCGIFGATTR, (caddr_t)&ifgr) == -1) {
			log_msg(1, "carp_demote: unable to get "
			    "the demote state of group '%s'",
			    cfgstate.carp_ifgroup);
			goto done;
		}

		if (ifgr.ifgr_attrib.ifg_carp_demoted == 0)
			goto done;
	}

	ifgr.ifgr_attrib.ifg_carp_demoted = demote;
	if (ioctl(s, SIOCSIFGATTR, (caddr_t)&ifgr) == -1)
		log_msg(1, "carp_demote: unable to %s the demote state "
		    "of group '%s'", (demote > 0) ?
		    "increment" : "decrement", cfgstate.carp_ifgroup);
	else {
		carp_demoted += demote;
		log_msg(1, "carp_demote: %sed the demote state "
		    "of group '%s'", (demote > 0) ?
		    "increment" : "decrement", cfgstate.carp_ifgroup);
	}
done:
	close(s);
}

const char*
carp_state_name(enum RUNSTATE state)
{
	static const char	*carpstate[] = CARPSTATES;

	if ((unsigned)state > FAIL)
		state = FAIL;
	return carpstate[state];
}

void
carp_update_state(enum RUNSTATE current_state)
{

	if ((unsigned)current_state > FAIL) {
		log_err("carp_update_state: invalid carp state, abort");
		cfgstate.runstate = FAIL;
		return;
	}

	if (current_state != cfgstate.runstate) {
		log_msg(1, "carp_update_state: switching state to %s",
		    carp_state_name(current_state));
		cfgstate.runstate = current_state;
		if (current_state == MASTER)
			pfkey_set_promisc();
		control_setrun();
		net_ctl_update_state();
	}
}

void
carp_check_state(void)
{
	carp_update_state(carp_get_state(cfgstate.carp_ifname));
}

void
carp_set_rfd(fd_set *fds)
{
	if (cfgstate.route_socket != -1)
		FD_SET(cfgstate.route_socket, fds);
}

static void
carp_read(void)
{
	char msg[2048];
	struct rt_msghdr *rtm = (struct rt_msghdr *)&msg;
	struct if_msghdr ifm;
	ssize_t len;

	len = read(cfgstate.route_socket, msg, sizeof(msg));

	if (len < (ssize_t)sizeof(struct rt_msghdr) ||
	    rtm->rtm_version != RTM_VERSION ||
	    rtm->rtm_type != RTM_IFINFO)
		return;

	memcpy(&ifm, rtm, sizeof(ifm));

	if (ifm.ifm_index == cfgstate.carp_ifindex)
		carp_update_state(carp_map_state(ifm.ifm_data.ifi_link_state));
}

void
carp_read_message(fd_set *fds)
{
	if (cfgstate.route_socket != -1)
		if (FD_ISSET(cfgstate.route_socket, fds))
			(void)carp_read();
}

/* Initialize the CARP state. */
int
carp_init(void)
{
	unsigned int rtfilter;

	cfgstate.route_socket = -1;
	if (cfgstate.lockedstate != INIT) {
		cfgstate.runstate = cfgstate.lockedstate;
		log_msg(1, "carp_init: locking runstate to %s",
		    carp_state_name(cfgstate.runstate));
		return 0;
	}

	if (!cfgstate.carp_ifname || !*cfgstate.carp_ifname) {
		fprintf(stderr, "No carp interface\n");
		return -1;
	}

	cfgstate.carp_ifindex = if_nametoindex(cfgstate.carp_ifname);
	if (!cfgstate.carp_ifindex) {
		fprintf(stderr, "No carp interface index\n");
		return -1;
	}

	cfgstate.route_socket = socket(AF_ROUTE, SOCK_RAW, 0);
	if (cfgstate.route_socket < 0) {
		fprintf(stderr, "No routing socket\n");
		return -1;
	}

	rtfilter = ROUTE_FILTER(RTM_IFINFO);
	if (setsockopt(cfgstate.route_socket, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)         /* not fatal */
		log_msg(2, "carp_init: setsockopt");

	cfgstate.runstate = carp_get_state(cfgstate.carp_ifname);
	if (cfgstate.runstate == FAIL) {
		fprintf(stderr, "Failed to check interface \"%s\".\n",
		    cfgstate.carp_ifname);
		fprintf(stderr, "Correct or manually select runstate.\n");
		return -1;
	}
	log_msg(1, "carp_init: initializing runstate to %s",
	    carp_state_name(cfgstate.runstate));

	return 0;
}

/* Enable or disable isakmpd/iked connection checker. */
void
control_setrun(void)
{
	if (cfgstate.runstate == MASTER) {
		if (monitor_control_active(1))
			log_msg(0, "failed to activate controlled daemon");
	} else {
		if (monitor_control_active(0))
			log_msg(0, "failed to passivate controlled daemon");
	}
}
