/*-
 * Copyright (c) 2014, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <netdb.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_vxlan.h>
#include <net/route.h>
#include <netinet/in.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

static struct ifvxlanparam params = {
	.vxlp_vni	= VXLAN_VNI_MAX,
};

static int
get_val(const char *cp, u_long *valp)
{
	char *endptr;
	u_long val;

	errno = 0;
	val = strtoul(cp, &endptr, 0);
	if (cp[0] == '\0' || endptr[0] != '\0' || errno == ERANGE)
		return (-1);

	*valp = val;
	return (0);
}

static int
do_cmd(int sock, u_long op, void *arg, size_t argsize, int set)
{
	struct ifdrv ifd;

	bzero(&ifd, sizeof(ifd));

	strlcpy(ifd.ifd_name, ifr.ifr_name, sizeof(ifd.ifd_name));
	ifd.ifd_cmd = op;
	ifd.ifd_len = argsize;
	ifd.ifd_data = arg;

	return (ioctl(sock, set ? SIOCSDRVSPEC : SIOCGDRVSPEC, &ifd));
}

static int
vxlan_exists(int sock)
{
	struct ifvxlancfg cfg;

	bzero(&cfg, sizeof(cfg));

	return (do_cmd(sock, VXLAN_CMD_GET_CONFIG, &cfg, sizeof(cfg), 0) != -1);
}

static void
vxlan_status(int s)
{
	struct ifvxlancfg cfg;
	char src[NI_MAXHOST], dst[NI_MAXHOST];
	char srcport[NI_MAXSERV], dstport[NI_MAXSERV];
	struct sockaddr *lsa, *rsa;
	int vni, mc, ipv6;

	bzero(&cfg, sizeof(cfg));

	if (do_cmd(s, VXLAN_CMD_GET_CONFIG, &cfg, sizeof(cfg), 0) < 0)
		return;

	vni = cfg.vxlc_vni;
	lsa = &cfg.vxlc_local_sa.sa;
	rsa = &cfg.vxlc_remote_sa.sa;
	ipv6 = rsa->sa_family == AF_INET6;

	/* Just report nothing if the network identity isn't set yet. */
	if (vni >= VXLAN_VNI_MAX)
		return;

	if (getnameinfo(lsa, lsa->sa_len, src, sizeof(src),
	    srcport, sizeof(srcport), NI_NUMERICHOST | NI_NUMERICSERV) != 0)
		src[0] = srcport[0] = '\0';
	if (getnameinfo(rsa, rsa->sa_len, dst, sizeof(dst),
	    dstport, sizeof(dstport), NI_NUMERICHOST | NI_NUMERICSERV) != 0)
		dst[0] = dstport[0] = '\0';

	if (!ipv6) {
		struct sockaddr_in *sin = (struct sockaddr_in *)rsa;
		mc = IN_MULTICAST(ntohl(sin->sin_addr.s_addr));
	} else {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)rsa;
		mc = IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr);
	}

	printf("\tvxlan vni %d", vni);
	printf(" local %s%s%s:%s", ipv6 ? "[" : "", src, ipv6 ? "]" : "",
	    srcport);
	printf(" %s %s%s%s:%s", mc ? "group" : "remote", ipv6 ? "[" : "",
	    dst, ipv6 ? "]" : "", dstport);

	if (verbose) {
		printf("\n\t\tconfig: ");
		printf("%slearning portrange %d-%d ttl %d",
		    cfg.vxlc_learn ? "" : "no", cfg.vxlc_port_min,
		    cfg.vxlc_port_max, cfg.vxlc_ttl);
		printf("\n\t\tftable: ");
		printf("cnt %d max %d timeout %d",
		    cfg.vxlc_ftable_cnt, cfg.vxlc_ftable_max,
		    cfg.vxlc_ftable_timeout);
	}

	putchar('\n');
}

#define _LOCAL_ADDR46 \
    (VXLAN_PARAM_WITH_LOCAL_ADDR4 | VXLAN_PARAM_WITH_LOCAL_ADDR6)
#define _REMOTE_ADDR46 \
    (VXLAN_PARAM_WITH_REMOTE_ADDR4 | VXLAN_PARAM_WITH_REMOTE_ADDR6)

static void
vxlan_check_params(void)
{

	if ((params.vxlp_with & _LOCAL_ADDR46) == _LOCAL_ADDR46)
		errx(1, "cannot specify both local IPv4 and IPv6 addresses");
	if ((params.vxlp_with & _REMOTE_ADDR46) == _REMOTE_ADDR46)
		errx(1, "cannot specify both remote IPv4 and IPv6 addresses");
	if ((params.vxlp_with & VXLAN_PARAM_WITH_LOCAL_ADDR4 &&
	     params.vxlp_with & VXLAN_PARAM_WITH_REMOTE_ADDR6) ||
	    (params.vxlp_with & VXLAN_PARAM_WITH_LOCAL_ADDR6 &&
	     params.vxlp_with & VXLAN_PARAM_WITH_REMOTE_ADDR4))
		errx(1, "cannot mix IPv4 and IPv6 addresses");
}

#undef _LOCAL_ADDR46
#undef _REMOTE_ADDR46

static void
vxlan_cb(int s, void *arg)
{

}

static void
vxlan_create(int s, struct ifreq *ifr)
{

	vxlan_check_params();

	ifr->ifr_data = (caddr_t) &params;
	if (ioctl(s, SIOCIFCREATE2, ifr) < 0)
		err(1, "SIOCIFCREATE2");
}

static
DECL_CMD_FUNC(setvxlan_vni, arg, d)
{
	struct ifvxlancmd cmd;
	u_long val;

	if (get_val(arg, &val) < 0 || val >= VXLAN_VNI_MAX)
		errx(1, "invalid network identifier: %s", arg);

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_VNI;
		params.vxlp_vni = val;
		return;
	}

	bzero(&cmd, sizeof(cmd));
	cmd.vxlcmd_vni = val;

	if (do_cmd(s, VXLAN_CMD_SET_VNI, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_VNI");
}

static
DECL_CMD_FUNC(setvxlan_local, addr, d)
{
	struct ifvxlancmd cmd;
	struct addrinfo *ai;
	struct sockaddr *sa;
	int error;

	bzero(&cmd, sizeof(cmd));

	if ((error = getaddrinfo(addr, NULL, NULL, &ai)) != 0)
		errx(1, "error in parsing local address string: %s",
		    gai_strerror(error));

	sa = ai->ai_addr;

	switch (ai->ai_family) {
#ifdef INET
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			errx(1, "local address cannot be multicast");

		cmd.vxlcmd_sa.in4 = *sin;
		break;
	}
#endif
#ifdef INET6
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

		if (IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			errx(1, "local address cannot be multicast");

		cmd.vxlcmd_sa.in6 = *sin6;
		break;
	}
#endif
	default:
		errx(1, "local address %s not supported", addr);
	}

	freeaddrinfo(ai);

	if (!vxlan_exists(s)) {
		if (cmd.vxlcmd_sa.sa.sa_family == AF_INET) {
			params.vxlp_with |= VXLAN_PARAM_WITH_LOCAL_ADDR4;
			params.vxlp_local_sa.in4 = cmd.vxlcmd_sa.in4;
		} else {
			params.vxlp_with |= VXLAN_PARAM_WITH_LOCAL_ADDR6;
			params.vxlp_local_sa.in6 = cmd.vxlcmd_sa.in6;
		}
		return;
	}

	if (do_cmd(s, VXLAN_CMD_SET_LOCAL_ADDR, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_LOCAL_ADDR");
}

static
DECL_CMD_FUNC(setvxlan_remote, addr, d)
{
	struct ifvxlancmd cmd;
	struct addrinfo *ai;
	struct sockaddr *sa;
	int error;

	bzero(&cmd, sizeof(cmd));

	if ((error = getaddrinfo(addr, NULL, NULL, &ai)) != 0)
		errx(1, "error in parsing remote address string: %s",
		    gai_strerror(error));

	sa = ai->ai_addr;

	switch (ai->ai_family) {
#ifdef INET
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			errx(1, "remote address cannot be multicast");

		cmd.vxlcmd_sa.in4 = *sin;
		break;
	}
#endif
#ifdef INET6
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

		if (IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			errx(1, "remote address cannot be multicast");

		cmd.vxlcmd_sa.in6 = *sin6;
		break;
	}
#endif
	default:
		errx(1, "remote address %s not supported", addr);
	}

	freeaddrinfo(ai);

	if (!vxlan_exists(s)) {
		if (cmd.vxlcmd_sa.sa.sa_family == AF_INET) {
			params.vxlp_with |= VXLAN_PARAM_WITH_REMOTE_ADDR4;
			params.vxlp_remote_sa.in4 = cmd.vxlcmd_sa.in4;
		} else {
			params.vxlp_with |= VXLAN_PARAM_WITH_REMOTE_ADDR6;
			params.vxlp_remote_sa.in6 = cmd.vxlcmd_sa.in6;
		}
		return;
	}

	if (do_cmd(s, VXLAN_CMD_SET_REMOTE_ADDR, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_REMOTE_ADDR");
}

static
DECL_CMD_FUNC(setvxlan_group, addr, d)
{
	struct ifvxlancmd cmd;
	struct addrinfo *ai;
	struct sockaddr *sa;
	int error;

	bzero(&cmd, sizeof(cmd));

	if ((error = getaddrinfo(addr, NULL, NULL, &ai)) != 0)
		errx(1, "error in parsing group address string: %s",
		    gai_strerror(error));

	sa = ai->ai_addr;

	switch (ai->ai_family) {
#ifdef INET
	case AF_INET: {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			errx(1, "group address must be multicast");

		cmd.vxlcmd_sa.in4 = *sin;
		break;
	}
#endif
#ifdef INET6
	case AF_INET6: {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

		if (!IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			errx(1, "group address must be multicast");

		cmd.vxlcmd_sa.in6 = *sin6;
		break;
	}
#endif
	default:
		errx(1, "group address %s not supported", addr);
	}

	freeaddrinfo(ai);

	if (!vxlan_exists(s)) {
		if (cmd.vxlcmd_sa.sa.sa_family == AF_INET) {
			params.vxlp_with |= VXLAN_PARAM_WITH_REMOTE_ADDR4;
			params.vxlp_remote_sa.in4 = cmd.vxlcmd_sa.in4;
		} else {
			params.vxlp_with |= VXLAN_PARAM_WITH_REMOTE_ADDR6;
			params.vxlp_remote_sa.in6 = cmd.vxlcmd_sa.in6;
		}
		return;
	}

	if (do_cmd(s, VXLAN_CMD_SET_REMOTE_ADDR, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_REMOTE_ADDR");
}

static
DECL_CMD_FUNC(setvxlan_local_port, arg, d)
{
	struct ifvxlancmd cmd;
	u_long val;

	if (get_val(arg, &val) < 0 || val >= UINT16_MAX)
		errx(1, "invalid local port: %s", arg);

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_LOCAL_PORT;
		params.vxlp_local_port = val;
		return;
	}

	bzero(&cmd, sizeof(cmd));
	cmd.vxlcmd_port = val;

	if (do_cmd(s, VXLAN_CMD_SET_LOCAL_PORT, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_LOCAL_PORT");
}

static
DECL_CMD_FUNC(setvxlan_remote_port, arg, d)
{
	struct ifvxlancmd cmd;
	u_long val;

	if (get_val(arg, &val) < 0 || val >= UINT16_MAX)
		errx(1, "invalid remote port: %s", arg);

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_REMOTE_PORT;
		params.vxlp_remote_port = val;
		return;
	}

	bzero(&cmd, sizeof(cmd));
	cmd.vxlcmd_port = val;

	if (do_cmd(s, VXLAN_CMD_SET_REMOTE_PORT, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_REMOTE_PORT");
}

static
DECL_CMD_FUNC2(setvxlan_port_range, arg1, arg2)
{
	struct ifvxlancmd cmd;
	u_long min, max;

	if (get_val(arg1, &min) < 0 || min >= UINT16_MAX)
		errx(1, "invalid port range minimum: %s", arg1);
	if (get_val(arg2, &max) < 0 || max >= UINT16_MAX)
		errx(1, "invalid port range maximum: %s", arg2);
	if (max < min)
		errx(1, "invalid port range");

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_PORT_RANGE;
		params.vxlp_min_port = min;
		params.vxlp_max_port = max;
		return;
	}

	bzero(&cmd, sizeof(cmd));
	cmd.vxlcmd_port_min = min;
	cmd.vxlcmd_port_max = max;

	if (do_cmd(s, VXLAN_CMD_SET_PORT_RANGE, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_PORT_RANGE");
}

static
DECL_CMD_FUNC(setvxlan_timeout, arg, d)
{
	struct ifvxlancmd cmd;
	u_long val;

	if (get_val(arg, &val) < 0 || (val & ~0xFFFFFFFF) != 0)
		errx(1, "invalid timeout value: %s", arg);

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_FTABLE_TIMEOUT;
		params.vxlp_ftable_timeout = val & 0xFFFFFFFF;
		return;
	}

	bzero(&cmd, sizeof(cmd));
	cmd.vxlcmd_ftable_timeout = val & 0xFFFFFFFF;

	if (do_cmd(s, VXLAN_CMD_SET_FTABLE_TIMEOUT, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_FTABLE_TIMEOUT");
}

static
DECL_CMD_FUNC(setvxlan_maxaddr, arg, d)
{
	struct ifvxlancmd cmd;
	u_long val;

	if (get_val(arg, &val) < 0 || (val & ~0xFFFFFFFF) != 0)
		errx(1, "invalid maxaddr value: %s",  arg);

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_FTABLE_MAX;
		params.vxlp_ftable_max = val & 0xFFFFFFFF;
		return;
	}

	bzero(&cmd, sizeof(cmd));
	cmd.vxlcmd_ftable_max = val & 0xFFFFFFFF;

	if (do_cmd(s, VXLAN_CMD_SET_FTABLE_MAX, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_FTABLE_MAX");
}

static
DECL_CMD_FUNC(setvxlan_dev, arg, d)
{
	struct ifvxlancmd cmd;

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_MULTICAST_IF;
		strlcpy(params.vxlp_mc_ifname, arg,
		    sizeof(params.vxlp_mc_ifname));
		return;
	}

	bzero(&cmd, sizeof(cmd));
	strlcpy(cmd.vxlcmd_ifname, arg, sizeof(cmd.vxlcmd_ifname));

	if (do_cmd(s, VXLAN_CMD_SET_MULTICAST_IF, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_MULTICAST_IF");
}

static
DECL_CMD_FUNC(setvxlan_ttl, arg, d)
{
	struct ifvxlancmd cmd;
	u_long val;

	if (get_val(arg, &val) < 0 || val > 256)
		errx(1, "invalid TTL value: %s", arg);

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_TTL;
		params.vxlp_ttl = val;
		return;
	}

	bzero(&cmd, sizeof(cmd));
	cmd.vxlcmd_ttl = val;

	if (do_cmd(s, VXLAN_CMD_SET_TTL, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_TTL");
}

static
DECL_CMD_FUNC(setvxlan_learn, arg, d)
{
	struct ifvxlancmd cmd;

	if (!vxlan_exists(s)) {
		params.vxlp_with |= VXLAN_PARAM_WITH_LEARN;
		params.vxlp_learn = d;
		return;
	}

	bzero(&cmd, sizeof(cmd));
	if (d != 0)
		cmd.vxlcmd_flags |= VXLAN_CMD_FLAG_LEARN;

	if (do_cmd(s, VXLAN_CMD_SET_LEARN, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_SET_LEARN");
}

static void
setvxlan_flush(const char *val, int d, int s, const struct afswtch *afp)
{
	struct ifvxlancmd cmd;

	bzero(&cmd, sizeof(cmd));
	if (d != 0)
		cmd.vxlcmd_flags |= VXLAN_CMD_FLAG_FLUSH_ALL;

	if (do_cmd(s, VXLAN_CMD_FLUSH, &cmd, sizeof(cmd), 1) < 0)
		err(1, "VXLAN_CMD_FLUSH");
}

static struct cmd vxlan_cmds[] = {

	DEF_CLONE_CMD_ARG("vni",                setvxlan_vni),
	DEF_CLONE_CMD_ARG("vxlanid",		setvxlan_vni),
	DEF_CLONE_CMD_ARG("vxlanlocal",		setvxlan_local),
	DEF_CLONE_CMD_ARG("vxlanremote",	setvxlan_remote),
	DEF_CLONE_CMD_ARG("vxlangroup",		setvxlan_group),
	DEF_CLONE_CMD_ARG("vxlanlocalport",	setvxlan_local_port),
	DEF_CLONE_CMD_ARG("vxlanremoteport",	setvxlan_remote_port),
	DEF_CLONE_CMD_ARG2("vxlanportrange",	setvxlan_port_range),
	DEF_CLONE_CMD_ARG("vxlantimeout",	setvxlan_timeout),
	DEF_CLONE_CMD_ARG("vxlanmaxaddr",	setvxlan_maxaddr),
	DEF_CLONE_CMD_ARG("vxlandev",		setvxlan_dev),
	DEF_CLONE_CMD_ARG("vxlanttl",		setvxlan_ttl),
	DEF_CLONE_CMD("vxlanlearn", 1,		setvxlan_learn),
	DEF_CLONE_CMD("-vxlanlearn", 0,		setvxlan_learn),

	DEF_CMD_ARG("vni",			setvxlan_vni),
	DEF_CMD_ARG("vxlanid",			setvxlan_vni),
	DEF_CMD_ARG("vxlanlocal",		setvxlan_local),
	DEF_CMD_ARG("vxlanremote",		setvxlan_remote),
	DEF_CMD_ARG("vxlangroup",		setvxlan_group),
	DEF_CMD_ARG("vxlanlocalport",		setvxlan_local_port),
	DEF_CMD_ARG("vxlanremoteport",		setvxlan_remote_port),
	DEF_CMD_ARG2("vxlanportrange",		setvxlan_port_range),
	DEF_CMD_ARG("vxlantimeout",		setvxlan_timeout),
	DEF_CMD_ARG("vxlanmaxaddr",		setvxlan_maxaddr),
	DEF_CMD_ARG("vxlandev",			setvxlan_dev),
	DEF_CMD_ARG("vxlanttl",			setvxlan_ttl),
	DEF_CMD("vxlanlearn", 1,		setvxlan_learn),
	DEF_CMD("-vxlanlearn", 0,		setvxlan_learn),

	DEF_CMD("vxlanflush", 0,		setvxlan_flush),
	DEF_CMD("vxlanflushall", 1,		setvxlan_flush),
};

static struct afswtch af_vxlan = {
	.af_name		= "af_vxlan",
	.af_af			= AF_UNSPEC,
	.af_other_status	= vxlan_status,
};

static __constructor void
vxlan_ctor(void)
{
	size_t i;

	for (i = 0; i < nitems(vxlan_cmds); i++)
		cmd_register(&vxlan_cmds[i]);
	af_register(&af_vxlan);
	callback_register(vxlan_cb, NULL);
	clone_setdefcallback("vxlan", vxlan_create);
}
