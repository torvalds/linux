/*	$OpenBSD: agentx_control.c,v 1.7 2024/01/17 10:01:24 claudio Exp $	*/

/*
 * Copyright (c) 2020 Martijn van Duren <martijn@openbsd.org>
 * Copyright (c) 2008 - 2014 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/un.h>

#include <netinet/in.h>

#include <agentx.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <event.h>
#include <imsg.h>

#include "relayd.h"

#define RELAYD_MIB	"1.3.6.1.4.1.30155.3"
#define SNMP_ELEMENT(x...)	do {				\
	if (snmp_element(RELAYD_MIB x) == -1)			\
		goto done;					\
} while (0)

/*
static struct snmp_oid	hosttrapoid = {
	{ 1, 3, 6, 1, 4, 1, 30155, 3, 1, 0 },
	10
};
*/

#define RELAYDINFO		AGENTX_ENTERPRISES, 30155, 3, 2
#define RELAYDREDIRECTS		RELAYDINFO, 1
#define RELAYDREDIRECTENTRY	RELAYDREDIRECTS, 1
#define RELAYDREDIRECTINDEX	RELAYDREDIRECTENTRY, 1
#define RELAYDREDIRECTSTATUS	RELAYDREDIRECTENTRY, 2
#define RELAYDREDIRECTNAME	RELAYDREDIRECTENTRY, 3
#define RELAYDREDIRECTCNT	RELAYDREDIRECTENTRY, 4
#define RELAYDREDIRECTAVG	RELAYDREDIRECTENTRY, 5
#define RELAYDREDIRECTLAST	RELAYDREDIRECTENTRY, 6
#define RELAYDREDIRECTAVGHOUR	RELAYDREDIRECTENTRY, 7
#define RELAYDREDIRECTLASTHOUR	RELAYDREDIRECTENTRY, 8
#define RELAYDREDIRECTAVGDAY	RELAYDREDIRECTENTRY, 9
#define RELAYDREDIRECTLASTDAY	RELAYDREDIRECTENTRY, 10
#define RELAYDRELAYS		RELAYDINFO, 2
#define RELAYDRELAYENTRY	RELAYDRELAYS, 1
#define RELAYDRELAYINDEX	RELAYDRELAYENTRY, 1
#define RELAYDRELAYSTATUS	RELAYDRELAYENTRY, 2
#define RELAYDRELAYNAME		RELAYDRELAYENTRY, 3
#define RELAYDRELAYCNT		RELAYDRELAYENTRY, 4
#define RELAYDRELAYAVG		RELAYDRELAYENTRY, 5
#define RELAYDRELAYLAST		RELAYDRELAYENTRY, 6
#define RELAYDRELAYAVGHOUR	RELAYDRELAYENTRY, 7
#define RELAYDRELAYLASTHOUR	RELAYDRELAYENTRY, 8
#define RELAYDRELAYAVGDAY	RELAYDRELAYENTRY, 9
#define RELAYDRELAYLASTDAY	RELAYDRELAYENTRY, 10
#define RELAYDROUTERS		RELAYDINFO, 3
#define RELAYDROUTERENTRY	RELAYDROUTERS, 1
#define RELAYDROUTERINDEX	RELAYDROUTERENTRY, 1
#define RELAYDROUTERTABLEINDEX	RELAYDROUTERENTRY, 2
#define RELAYDROUTERSTATUS	RELAYDROUTERENTRY, 3
#define RELAYDROUTERNAME	RELAYDROUTERENTRY, 4
#define RELAYDROUTERLABEL	RELAYDROUTERENTRY, 5
#define RELAYDROUTERRTABLE	RELAYDROUTERENTRY, 6
#define RELAYDNETROUTES		RELAYDINFO, 4
#define RELAYDNETROUTEENTRY	RELAYDNETROUTES, 1
#define RELAYDNETROUTEINDEX	RELAYDNETROUTEENTRY, 1
#define RELAYDNETROUTEADDR	RELAYDNETROUTEENTRY, 2
#define RELAYDNETROUTEADDRTYPE	RELAYDNETROUTEENTRY, 3
#define RELAYDNETROUTEPREFIXLEN	RELAYDNETROUTEENTRY, 4
#define RELAYDNETROUTEROUTERINDEX RELAYDNETROUTEENTRY, 5
#define RELAYDHOSTS		RELAYDINFO, 5
#define RELAYDHOSTENTRY		RELAYDHOSTS, 1
#define RELAYDHOSTINDEX		RELAYDHOSTENTRY, 1
#define RELAYDHOSTPARENTINDEX	RELAYDHOSTENTRY, 2
#define RELAYDHOSTTABLEINDEX	RELAYDHOSTENTRY, 3
#define RELAYDHOSTNAME		RELAYDHOSTENTRY, 4
#define RELAYDHOSTADDRESS	RELAYDHOSTENTRY, 5
#define RELAYDHOSTADDRESSTYPE	RELAYDHOSTENTRY, 6
#define RELAYDHOSTSTATUS	RELAYDHOSTENTRY, 7
#define RELAYDHOSTCHECKCNT	RELAYDHOSTENTRY, 8
#define RELAYDHOSTUPCNT		RELAYDHOSTENTRY, 9
#define RELAYDHOSTERRNO		RELAYDHOSTENTRY, 10
#define RELAYDSESSIONS		RELAYDINFO, 6
#define RELAYDSESSIONENTRY	RELAYDSESSIONS, 1
#define RELAYDSESSIONINDEX	RELAYDSESSIONENTRY, 1
#define RELAYDSESSIONRELAYINDEX	RELAYDSESSIONENTRY, 2
#define RELAYDSESSIONINADDR	RELAYDSESSIONENTRY, 3
#define RELAYDSESSIONINADDRTYPE	RELAYDSESSIONENTRY, 4
#define RELAYDSESSIONOUTADDR	RELAYDSESSIONENTRY, 5
#define RELAYDSESSIONOUTADDRTYPE RELAYDSESSIONENTRY, 6
#define RELAYDSESSIONPORTIN	RELAYDSESSIONENTRY, 7
#define RELAYDSESSIONPORTOUT	RELAYDSESSIONENTRY, 8
#define RELAYDSESSIONAGE	RELAYDSESSIONENTRY, 9
#define RELAYDSESSIONIDLE	RELAYDSESSIONENTRY, 10
#define RELAYDSESSIONSTATUS	RELAYDSESSIONENTRY, 11
#define RELAYDSESSIONPID	RELAYDSESSIONENTRY, 12
#define RELAYDTABLES		RELAYDINFO, 7
#define RELAYDTABLEENTRY	RELAYDTABLES, 1
#define RELAYDTABLEINDEX	RELAYDTABLEENTRY, 1
#define RELAYDTABLENAME		RELAYDTABLEENTRY, 2
#define RELAYDTABLESTATUS	RELAYDTABLEENTRY, 3

void agentx_nofd(struct agentx *, void *, int);

struct relayd *env;

struct agentx *sa = NULL;
struct agentx_index *relaydRedirectIdx, *relaydRelayIdx;
struct agentx_index *relaydRouterIdx, *relaydNetRouteIdx;
struct agentx_index *relaydHostIdx, *relaydSessionRelayIdx;
struct agentx_index *relaydSessionIdx, *relaydTableIdx;

struct agentx_object *relaydRedirectIndex, *relaydRedirectStatus;
struct agentx_object *relaydRedirectName, *relaydRedirectCnt;
struct agentx_object *relaydRedirectAvg, *relaydRedirectLast;
struct agentx_object *relaydRedirectAvgHour, *relaydRedirectLastHour;
struct agentx_object *relaydRedirectAvgDay, *relaydRedirectLastDay;

struct agentx_object *relaydRelayIndex, *relaydRelayStatus;
struct agentx_object *relaydRelayName, *relaydRelayCnt;
struct agentx_object *relaydRelayAvg, *relaydRelayLast;
struct agentx_object *relaydRelayAvgHour, *relaydRelayLastHour;
struct agentx_object *relaydRelayAvgDay, *relaydRelayLastDay;

struct agentx_object *relaydRouterIndex, *relaydRouterTableIndex;
struct agentx_object *relaydRouterStatus, *relaydRouterName;
struct agentx_object *relaydRouterLabel, *relaydRouterRtable;

struct agentx_object *relaydNetRouteIndex, *relaydNetRouteAddr;
struct agentx_object *relaydNetRouteAddrType, *relaydNetRoutePrefixLen;
struct agentx_object *relaydNetRouteRouterIndex;

struct agentx_object *relaydHostIndex, *relaydHostParentIndex;
struct agentx_object *relaydHostTableIndex, *relaydHostName;
struct agentx_object *relaydHostAddress, *relaydHostAddressType;
struct agentx_object *relaydHostStatus, *relaydHostCheckCnt;
struct agentx_object *relaydHostUpCnt, *relaydHostErrno;

struct agentx_object *relaydSessionIndex, *relaydSessionRelayIndex;
struct agentx_object *relaydSessionInAddr, *relaydSessionInAddrType;
struct agentx_object *relaydSessionOutAddr, *relaydSessionOutAddrType;
struct agentx_object *relaydSessionPortIn, *relaydSessionPortOut;
struct agentx_object *relaydSessionAge, *relaydSessionIdle;
struct agentx_object *relaydSessionStatus, *relaydSessionPid;

struct agentx_object *relaydTableIndex, *relaydTableName, *relaydTableStatus;

void	*sstodata(struct sockaddr_storage *);
size_t	 sstolen(struct sockaddr_storage *);

struct rdr *agentxctl_rdr_byidx(uint32_t, enum agentx_request_type);
void agentxctl_redirect(struct agentx_varbind *);
struct relay *agentxctl_relay_byidx(uint32_t, enum agentx_request_type);
void agentxctl_relay(struct agentx_varbind *);
struct router *agentxctl_router_byidx(uint32_t, enum agentx_request_type);
void agentxctl_router(struct agentx_varbind *);
struct netroute *agentxctl_netroute_byidx(uint32_t, enum agentx_request_type);
void agentxctl_netroute(struct agentx_varbind *);
struct host *agentxctl_host_byidx(uint32_t, enum agentx_request_type);
void agentxctl_host(struct agentx_varbind *);
struct rsession *agentxctl_session_byidx(uint32_t, uint32_t,
    enum agentx_request_type);
void agentxctl_session(struct agentx_varbind *);
struct table *agentxctl_table_byidx(uint32_t, enum agentx_request_type);
void agentxctl_table(struct agentx_varbind *);

void	 agentx_sock(int, short, void *);
#if 0
int	 snmp_element(const char *, enum snmp_type, void *, int64_t,
	    struct agentx_pdu *);
int	 snmp_string2oid(const char *, struct snmp_oid *);
#endif

void
agentx_init(struct relayd *nenv)
{
	struct agentx_session *sas;
	struct agentx_context *sac;
	struct agentx_region *sar;
	struct agentx_index *session_idxs[2];
	static int freed;

	agentx_log_fatal = fatalx;
	agentx_log_warn = log_warnx;
	agentx_log_info = log_info;
	agentx_log_debug = log_debug;

	env = nenv;

	if ((env->sc_conf.flags & F_AGENTX) == 0) {
		if (sa != NULL && !freed) {
			agentx_free(sa);
			freed = 1;
		}
		return;
	}
	if (sa != NULL)
		return;

	freed = 0;
	if ((sa = agentx(agentx_nofd, NULL)) == NULL)
		fatal("%s: agentx alloc", __func__);
	if ((sas = agentx_session(sa, NULL, 0, "relayd", 0)) == NULL)
		fatal("%s: agentx session alloc", __func__);
	if ((sac = agentx_context(sas,
		env->sc_conf.agentx_context[0] == '\0' ? NULL :
		env->sc_conf.agentx_context)) == NULL)
		fatal("%s: agentx context alloc", __func__);
	sar = agentx_region(sac, AGENTX_OID(RELAYDINFO), 0);
	if (sar == NULL)
		fatal("%s: agentx region alloc", __func__);
	if ((relaydRedirectIdx = agentx_index_integer_dynamic(sar,
	    AGENTX_OID(RELAYDREDIRECTINDEX))) == NULL ||
	    (relaydRelayIdx = agentx_index_integer_dynamic(sar,
	    AGENTX_OID(RELAYDRELAYINDEX))) == NULL ||
	    (relaydRouterIdx = agentx_index_integer_dynamic(sar,
	    AGENTX_OID(RELAYDROUTERINDEX))) == NULL ||
	    (relaydNetRouteIdx = agentx_index_integer_dynamic(sar,
	    AGENTX_OID(RELAYDNETROUTEINDEX))) == NULL ||
	    (relaydHostIdx = agentx_index_integer_dynamic(sar,
	    AGENTX_OID(RELAYDHOSTINDEX))) == NULL ||
	    (relaydSessionIdx = agentx_index_integer_dynamic(sar,
	    AGENTX_OID(RELAYDSESSIONINDEX))) == NULL ||
	    (relaydSessionRelayIdx = agentx_index_integer_dynamic(sar,
	    AGENTX_OID(RELAYDSESSIONRELAYINDEX))) == NULL ||
	    (relaydTableIdx = agentx_index_integer_dynamic(sar,
	    AGENTX_OID(RELAYDTABLEINDEX))) == NULL)
		fatal("%s: agentx index alloc", __func__);
	session_idxs[0] = relaydSessionRelayIdx;
	session_idxs[1] = relaydSessionIdx;
	if ((relaydRedirectIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTINDEX), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectStatus = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTSTATUS), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectName = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTNAME), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectCnt = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTCNT), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectAvg = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTAVG), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectLast = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTLAST), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectAvgHour = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTAVGHOUR), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectLastHour = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTLASTHOUR), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectAvgDay = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTAVGDAY), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRedirectLastDay = agentx_object(sar,
	    AGENTX_OID(RELAYDREDIRECTLASTDAY), &relaydRedirectIdx, 1, 0,
	    agentxctl_redirect)) == NULL ||
	    (relaydRelayIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYINDEX), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayStatus = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYSTATUS), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayName = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYNAME), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayCnt = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYCNT), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayAvg = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYAVG), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayLast = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYLAST), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayAvgHour = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYAVGHOUR), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayLastHour = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYLASTHOUR), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayAvgDay = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYAVGDAY), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRelayLastDay = agentx_object(sar,
	    AGENTX_OID(RELAYDRELAYLASTDAY), &relaydRelayIdx, 1, 0,
	    agentxctl_relay)) == NULL ||
	    (relaydRouterIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDROUTERINDEX), &relaydRouterIdx, 1, 0,
	    agentxctl_router)) == NULL ||
	    (relaydRouterTableIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDROUTERTABLEINDEX), &relaydRouterIdx, 1, 0,
	    agentxctl_router)) == NULL ||
	    (relaydRouterStatus = agentx_object(sar,
	    AGENTX_OID(RELAYDROUTERSTATUS), &relaydRouterIdx, 1, 0,
	    agentxctl_router)) == NULL ||
	    (relaydRouterName = agentx_object(sar,
	    AGENTX_OID(RELAYDROUTERNAME), &relaydRouterIdx, 1, 0,
	    agentxctl_router)) == NULL ||
	    (relaydRouterLabel = agentx_object(sar,
	    AGENTX_OID(RELAYDROUTERLABEL), &relaydRouterIdx, 1, 0,
	    agentxctl_router)) == NULL ||
	    (relaydRouterRtable = agentx_object(sar,
	    AGENTX_OID(RELAYDROUTERRTABLE), &relaydRouterIdx, 1, 0,
	    agentxctl_router)) == NULL ||
	    (relaydNetRouteIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDNETROUTEINDEX), &relaydNetRouteIdx, 1, 0,
	    agentxctl_netroute)) == NULL ||
	    (relaydNetRouteAddr = agentx_object(sar,
	    AGENTX_OID(RELAYDNETROUTEADDR), &relaydNetRouteIdx, 1, 0,
	    agentxctl_netroute)) == NULL ||
	    (relaydNetRouteAddrType = agentx_object(sar,
	    AGENTX_OID(RELAYDNETROUTEADDRTYPE), &relaydNetRouteIdx, 1, 0,
	    agentxctl_netroute)) == NULL ||
	    (relaydNetRoutePrefixLen = agentx_object(sar,
	    AGENTX_OID(RELAYDNETROUTEPREFIXLEN), &relaydNetRouteIdx, 1, 0,
	    agentxctl_netroute)) == NULL ||
	    (relaydNetRouteRouterIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDNETROUTEROUTERINDEX), &relaydNetRouteIdx, 1, 0,
	    agentxctl_netroute)) == NULL ||
	    (relaydHostIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTINDEX), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostParentIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTPARENTINDEX), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostTableIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTTABLEINDEX), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostName = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTNAME), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostAddress = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTADDRESS), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostAddressType = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTADDRESSTYPE), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostStatus = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTSTATUS), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostCheckCnt = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTCHECKCNT), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostUpCnt = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTUPCNT), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydHostErrno = agentx_object(sar,
	    AGENTX_OID(RELAYDHOSTERRNO), &relaydHostIdx, 1, 0,
	    agentxctl_host)) == NULL ||
	    (relaydSessionIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONINDEX), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionRelayIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONRELAYINDEX), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionInAddr = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONINADDR), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionInAddrType = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONINADDRTYPE), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionOutAddr = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONOUTADDR), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionOutAddrType = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONOUTADDRTYPE), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionPortIn = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONPORTIN), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionPortOut = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONPORTOUT), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionAge = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONAGE), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionIdle = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONIDLE), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionStatus = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONSTATUS), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydSessionPid = agentx_object(sar,
	    AGENTX_OID(RELAYDSESSIONPID), session_idxs, 2, 0,
	    agentxctl_session)) == NULL ||
	    (relaydTableIndex = agentx_object(sar,
	    AGENTX_OID(RELAYDTABLEINDEX), &relaydTableIdx, 1, 0,
	    agentxctl_table)) == NULL ||
	    (relaydTableName = agentx_object(sar,
	    AGENTX_OID(RELAYDTABLENAME), &relaydTableIdx, 1, 0,
	    agentxctl_table)) == NULL ||
	    (relaydTableStatus = agentx_object(sar,
	    AGENTX_OID(RELAYDTABLESTATUS), &relaydTableIdx, 1, 0,
	    agentxctl_table)) == NULL)
		fatal("%s: agentx object alloc", __func__);
}

void
agentx_nofd(struct agentx *usa, void *cookie, int close)
{
	if (!close)
		proc_compose(env->sc_ps, PROC_PARENT, IMSG_AGENTXSOCK, NULL, 0);
	else {
		sa = NULL;
		agentx_init(env);
		event_del(&(env->sc_agentxev));
	}
}

void
agentx_setsock(struct relayd *lenv, enum privsep_procid id)
{
	struct sockaddr_un	 sun;
	int			 s = -1;

	if ((s = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1)
		goto done;

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	if (strlcpy(sun.sun_path, lenv->sc_conf.agentx_path,
	    sizeof(sun.sun_path)) >= sizeof(sun.sun_path))
		fatalx("invalid socket path");

	if (connect(s, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		close(s);
		s = -1;
	}
 done:
	proc_compose_imsg(lenv->sc_ps, id, -1, IMSG_AGENTXSOCK, -1, s, NULL, 0);
}

void
agentx_getsock(struct imsg *imsg)
{
	struct timeval		 tv = AGENTX_RECONNECT_TIMEOUT;
	int 			 fd;

	fd = imsg_get_fd(imsg);
	if (fd == -1)
		goto retry;

	event_del(&(env->sc_agentxev));
	event_set(&(env->sc_agentxev), fd, EV_READ | EV_PERSIST,
	    agentx_sock, env);
	event_add(&(env->sc_agentxev), NULL);

	agentx_connect(sa, fd);

 retry:
	evtimer_set(&env->sc_agentxev, agentx_sock, env);
	evtimer_add(&env->sc_agentxev, &tv);
}

void
agentx_sock(int fd, short event, void *arg)
{
	if (event & EV_TIMEOUT) {
		proc_compose(env->sc_ps, PROC_PARENT, IMSG_AGENTXSOCK, NULL, 0);
		return;
	}
	if (event & EV_WRITE) {
		event_del(&(env->sc_agentxev));
		event_set(&(env->sc_agentxev), fd, EV_READ | EV_PERSIST,
		    agentx_sock, NULL);
		event_add(&(env->sc_agentxev), NULL);
		agentx_write(sa);
	}
	if (event & EV_READ)
		agentx_read(sa);
	return;
}

void *
sstodata(struct sockaddr_storage *ss)
{
	if (ss->ss_family == AF_INET)
		return &((struct sockaddr_in *)ss)->sin_addr;
	if (ss->ss_family == AF_INET6)
		return &((struct sockaddr_in6 *)ss)->sin6_addr;
	return NULL;
}

size_t
sstolen(struct sockaddr_storage *ss)
{
	if (ss->ss_family == AF_INET)
		return sizeof(((struct sockaddr_in *)ss)->sin_addr);
	if (ss->ss_family == AF_INET6)
		return sizeof(((struct sockaddr_in6 *)ss)->sin6_addr);
	return 0;
}

struct rdr *
agentxctl_rdr_byidx(uint32_t instanceidx, enum agentx_request_type type)
{
	struct rdr	*rdr;

	TAILQ_FOREACH(rdr, env->sc_rdrs, entry) {
		if (rdr->conf.id == instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET ||
			    type == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE)
				return rdr;
			else
				return TAILQ_NEXT(rdr, entry);
		} else if (rdr->conf.id > instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET)
				return NULL;
			return rdr;
		}
	}

	return NULL;
}


void
agentxctl_redirect(struct agentx_varbind *sav)
{
	struct rdr	*rdr;

	rdr = agentxctl_rdr_byidx(agentx_varbind_get_index_integer(sav,
	    relaydRedirectIdx), agentx_varbind_request(sav));
	if (rdr == NULL || rdr->conf.id > INT32_MAX) {
		agentx_varbind_notfound(sav);
		return;
	}
	agentx_varbind_set_index_integer(sav, relaydRedirectIdx,
	    rdr->conf.id);
	if (agentx_varbind_get_object(sav) == relaydRedirectIndex)
		agentx_varbind_integer(sav, rdr->conf.id);
	else if (agentx_varbind_get_object(sav) == relaydRedirectStatus) {
		if (rdr->conf.flags & F_DISABLE)
			agentx_varbind_integer(sav, 1);
		else if (rdr->conf.flags & F_DOWN)
			agentx_varbind_integer(sav, 2);
		else if (rdr->conf.flags & F_BACKUP)
			agentx_varbind_integer(sav, 3);
		else
			agentx_varbind_integer(sav, 0);
	} else if (agentx_varbind_get_object(sav) == relaydRedirectName)
		agentx_varbind_string(sav, rdr->conf.name);
	else if (agentx_varbind_get_object(sav) == relaydRedirectCnt)
		agentx_varbind_counter64(sav, rdr->stats.cnt);
	else if (agentx_varbind_get_object(sav) == relaydRedirectAvg)
		agentx_varbind_gauge32(sav, rdr->stats.avg);
	else if (agentx_varbind_get_object(sav) == relaydRedirectLast)
		agentx_varbind_gauge32(sav, rdr->stats.last);
	else if (agentx_varbind_get_object(sav) == relaydRedirectAvgHour)
		agentx_varbind_gauge32(sav, rdr->stats.avg_hour);
	else if (agentx_varbind_get_object(sav) == relaydRedirectLastHour)
		agentx_varbind_gauge32(sav, rdr->stats.last_hour);
	else if (agentx_varbind_get_object(sav) == relaydRedirectAvgDay)
		agentx_varbind_gauge32(sav, rdr->stats.avg_day);
	else if (agentx_varbind_get_object(sav) == relaydRedirectLastDay)
		agentx_varbind_gauge32(sav, rdr->stats.last_day);
}

struct relay *
agentxctl_relay_byidx(uint32_t instanceidx, enum agentx_request_type type)
{
	struct relay	*rly;

	TAILQ_FOREACH(rly, env->sc_relays, rl_entry) {
		if (rly->rl_conf.id == instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET ||
			    type == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE)
				return rly;
			else
				return TAILQ_NEXT(rly, rl_entry);
		} else if (rly->rl_conf.id > instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET)
				return NULL;
			return rly;
		}
	}

	return NULL;
}

void
agentxctl_relay(struct agentx_varbind *sav)
{
	struct relay	*rly;
	uint64_t	 value = 0;
	int		 i, nrelay = env->sc_conf.prefork_relay;

	rly = agentxctl_relay_byidx(agentx_varbind_get_index_integer(sav,
	    relaydRelayIdx), agentx_varbind_request(sav));
	if (rly == NULL || rly->rl_conf.id > INT32_MAX) {
		agentx_varbind_notfound(sav);
		return;
	}
	agentx_varbind_set_index_integer(sav, relaydRelayIdx,
	    rly->rl_conf.id);
	if (agentx_varbind_get_object(sav) == relaydRelayIndex)
		agentx_varbind_integer(sav, rly->rl_conf.id);
	else if (agentx_varbind_get_object(sav) == relaydRelayStatus) {
		if (rly->rl_up == HOST_UP)
			agentx_varbind_integer(sav, 1);
		else
			agentx_varbind_integer(sav, 0);
	} else if (agentx_varbind_get_object(sav) == relaydRelayName)
		agentx_varbind_string(sav, rly->rl_conf.name);
	else if (agentx_varbind_get_object(sav) == relaydRelayCnt) {
		for (i = 0; i < nrelay; i++)
			value += rly->rl_stats[i].cnt;
		agentx_varbind_counter64(sav, value);
	} else if (agentx_varbind_get_object(sav) == relaydRelayAvg) {
		for (i = 0; i < nrelay; i++)
			value += rly->rl_stats[i].avg;
		agentx_varbind_gauge32(sav, (uint32_t)value);
	} else if (agentx_varbind_get_object(sav) == relaydRelayLast) {
		for (i = 0; i < nrelay; i++)
			value += rly->rl_stats[i].last;
		agentx_varbind_gauge32(sav, (uint32_t)value);
	} else if (agentx_varbind_get_object(sav) == relaydRelayAvgHour) {
		for (i = 0; i < nrelay; i++)
			value += rly->rl_stats[i].avg_hour;
		agentx_varbind_gauge32(sav, (uint32_t)value);
	} else if (agentx_varbind_get_object(sav) == relaydRelayLastHour) {
		for (i = 0; i < nrelay; i++)
			value += rly->rl_stats[i].last_hour;
		agentx_varbind_gauge32(sav, (uint32_t)value);
	} else if (agentx_varbind_get_object(sav) == relaydRelayAvgDay) {
		for (i = 0; i < nrelay; i++)
			value += rly->rl_stats[i].avg_day;
		agentx_varbind_gauge32(sav, (uint32_t)value);
	} else if (agentx_varbind_get_object(sav) == relaydRelayLastDay) {
		for (i = 0; i < nrelay; i++)
			value += rly->rl_stats[i].last_day;
		agentx_varbind_gauge32(sav, (uint32_t)value);
	}
}

struct router *
agentxctl_router_byidx(uint32_t instanceidx, enum agentx_request_type type)
{
	struct router	*router;

	TAILQ_FOREACH(router, env->sc_rts, rt_entry) {
		if (router->rt_conf.id == instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET ||
			    type == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE)
				return router;
			else
				return TAILQ_NEXT(router, rt_entry);
		} else if (router->rt_conf.id > instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET)
				return NULL;
			return router;
		}
	}

	return NULL;
}

void
agentxctl_router(struct agentx_varbind *sav)
{
	struct router	*router;

	router = agentxctl_router_byidx(agentx_varbind_get_index_integer(sav,
	    relaydRouterIdx), agentx_varbind_request(sav));
	if (router == NULL || router->rt_conf.id > INT32_MAX) {
		agentx_varbind_notfound(sav);
		return;
	}
	agentx_varbind_set_index_integer(sav, relaydRouterIdx,
	    router->rt_conf.id);
	if (agentx_varbind_get_object(sav) == relaydRouterIndex)
		agentx_varbind_integer(sav, router->rt_conf.id);
	else if (agentx_varbind_get_object(sav) == relaydRouterTableIndex) {
		if (router->rt_conf.gwtable > INT32_MAX)
			agentx_varbind_integer(sav, -1);
		else
			agentx_varbind_integer(sav, router->rt_conf.gwtable);
	} else if (agentx_varbind_get_object(sav) == relaydRouterStatus) {
		if (router->rt_conf.flags & F_DISABLE)
			agentx_varbind_integer(sav, 1);
		else
			agentx_varbind_integer(sav, 0);
	} else if (agentx_varbind_get_object(sav) == relaydRouterName)
		agentx_varbind_string(sav, router->rt_conf.name);
	else if (agentx_varbind_get_object(sav) == relaydRouterLabel)
		agentx_varbind_string(sav, router->rt_conf.label);
	else if (agentx_varbind_get_object(sav) == relaydRouterRtable)
		agentx_varbind_integer(sav, router->rt_conf.rtable);
}

struct netroute *
agentxctl_netroute_byidx(uint32_t instanceidx, enum agentx_request_type type)
{
	struct netroute		*nr;

	TAILQ_FOREACH(nr, env->sc_routes, nr_route) {
		if (nr->nr_conf.id == instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET ||
			    type == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE)
				return nr;
			else
				return TAILQ_NEXT(nr, nr_entry);
		} else if (nr->nr_conf.id > instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET)
				return NULL;
			return nr;
		}
	}

	return NULL;
}

void
agentxctl_netroute(struct agentx_varbind *sav)
{
	struct netroute	*nr;

	nr = agentxctl_netroute_byidx(agentx_varbind_get_index_integer(sav,
	    relaydNetRouteIdx), agentx_varbind_request(sav));
	if (nr == NULL || nr->nr_conf.id > INT32_MAX) {
		agentx_varbind_notfound(sav);
		return;
	}
	agentx_varbind_set_index_integer(sav, relaydNetRouteIdx,
	    nr->nr_conf.id);
	if (agentx_varbind_get_object(sav) == relaydNetRouteIndex)
		agentx_varbind_integer(sav, nr->nr_conf.id);
	else if (agentx_varbind_get_object(sav) == relaydNetRouteAddr)
		agentx_varbind_nstring(sav, sstodata(&nr->nr_conf.ss),
		    sstolen(&nr->nr_conf.ss));
	else if (agentx_varbind_get_object(sav) == relaydNetRouteAddrType) {
		if (nr->nr_conf.ss.ss_family == AF_INET)
			agentx_varbind_integer(sav, 1);
		else if (nr->nr_conf.ss.ss_family == AF_INET6)
			agentx_varbind_integer(sav, 2);
	} else if (agentx_varbind_get_object(sav) == relaydNetRoutePrefixLen)
		agentx_varbind_integer(sav, nr->nr_conf.prefixlen);
	else if (agentx_varbind_get_object(sav) == relaydNetRouteRouterIndex) {
		if (nr->nr_conf.routerid > INT32_MAX)
			agentx_varbind_integer(sav, -1);
		else
			agentx_varbind_integer(sav, nr->nr_conf.routerid);
	}
}

struct host *
agentxctl_host_byidx(uint32_t instanceidx, enum agentx_request_type type)
{
	struct host		*host;

	TAILQ_FOREACH(host, &(env->sc_hosts), globalentry) {
		if (host->conf.id == instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET ||
			    type == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE)
				return host;
			else
				return TAILQ_NEXT(host, globalentry);
		} else if (host->conf.id > instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET)
				return NULL;
			return host;
		}
	}

	return NULL;
}

void
agentxctl_host(struct agentx_varbind *sav)
{
	struct host	*host;

	host = agentxctl_host_byidx(agentx_varbind_get_index_integer(sav,
	    relaydHostIdx), agentx_varbind_request(sav));
	if (host == NULL || host->conf.id > INT32_MAX) {
		agentx_varbind_notfound(sav);
		return;
	}
	agentx_varbind_set_index_integer(sav, relaydHostIdx,
	    host->conf.id);
	if (agentx_varbind_get_object(sav) == relaydHostIndex)
		agentx_varbind_integer(sav, host->conf.id);
	else if (agentx_varbind_get_object(sav) == relaydHostParentIndex) {
		if (host->conf.parentid > INT32_MAX)
			agentx_varbind_integer(sav, -1);
		else
			agentx_varbind_integer(sav, host->conf.parentid);
	} else if (agentx_varbind_get_object(sav) == relaydHostTableIndex) {
		if (host->conf.tableid > INT32_MAX)
			agentx_varbind_integer(sav, -1);
		else
			agentx_varbind_integer(sav, host->conf.tableid);
	} else if (agentx_varbind_get_object(sav) == relaydHostName)
		agentx_varbind_string(sav, host->conf.name);
	else if (agentx_varbind_get_object(sav) == relaydHostAddress)
		agentx_varbind_nstring(sav, sstodata(&host->conf.ss),
		    sstolen(&host->conf.ss));
	else if (agentx_varbind_get_object(sav) == relaydHostAddressType) {
		if (host->conf.ss.ss_family == AF_INET)
			agentx_varbind_integer(sav, 1);
		else if (host->conf.ss.ss_family == AF_INET6)
			agentx_varbind_integer(sav, 2);
	} else if (agentx_varbind_get_object(sav) == relaydHostStatus) {
		if (host->flags & F_DISABLE)
			agentx_varbind_integer(sav, 1);
		else if (host->up == HOST_UP)
			agentx_varbind_integer(sav, 0);
		else if (host->up == HOST_DOWN)
			agentx_varbind_integer(sav, 2);
		else
			agentx_varbind_integer(sav, 3);
	} else if (agentx_varbind_get_object(sav) == relaydHostCheckCnt)
		agentx_varbind_counter64(sav, host->check_cnt);
	else if (agentx_varbind_get_object(sav) == relaydHostUpCnt)
		agentx_varbind_counter64(sav, host->up_cnt);
	else if (agentx_varbind_get_object(sav) == relaydHostErrno)
		agentx_varbind_integer(sav, host->he);
}

/*
 * Every session is spawned in one of multiple processes.
 * However, there is no central session id registration, so not every session
 * is shown here
 */
struct rsession *
agentxctl_session_byidx(uint32_t sessidx, uint32_t relayidx,
    enum agentx_request_type type)
{
	struct rsession		*session;

	TAILQ_FOREACH(session, &(env->sc_sessions), se_entry) {
		if (session->se_id == sessidx) {
			if (type == AGENTX_REQUEST_TYPE_GET) {
				if (relayidx != session->se_relayid)
					return NULL;
				return session;
			}
			if (type == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE) {
				if (relayidx <= session->se_relayid)
					return session;
				return TAILQ_NEXT(session, se_entry);
			}
			if (relayidx < session->se_relayid)
				return session;
			return TAILQ_NEXT(session, se_entry);
		} else if (session->se_id > sessidx) {
			if (type == AGENTX_REQUEST_TYPE_GET)
				return NULL;
			return session;
		}
	}

	return NULL;
}

void
agentxctl_session(struct agentx_varbind *sav)
{
	struct timeval	 tv, now;
	struct rsession	*session;

	session = agentxctl_session_byidx(agentx_varbind_get_index_integer(sav,
	    relaydSessionIdx), agentx_varbind_get_index_integer(sav,
	    relaydSessionRelayIdx), agentx_varbind_request(sav));
	if (session == NULL || session->se_id > INT32_MAX ||
	    session->se_relayid > INT32_MAX) {
		agentx_varbind_notfound(sav);
		return;
	}

	agentx_varbind_set_index_integer(sav, relaydSessionIdx,
	    session->se_id);
	agentx_varbind_set_index_integer(sav, relaydSessionRelayIdx,
	    session->se_relayid);
	if (agentx_varbind_get_object(sav) == relaydSessionIndex)
		agentx_varbind_integer(sav, session->se_id);
	else if (agentx_varbind_get_object(sav) == relaydSessionRelayIndex)
		agentx_varbind_integer(sav, session->se_relayid);
	else if (agentx_varbind_get_object(sav) == relaydSessionInAddr)
		agentx_varbind_nstring(sav, sstodata(&(session->se_in.ss)),
		    sstolen(&(session->se_in.ss)));
	else if (agentx_varbind_get_object(sav) == relaydSessionInAddrType) {
		if (session->se_in.ss.ss_family == AF_INET)
			agentx_varbind_integer(sav, 1);
		else if (session->se_in.ss.ss_family == AF_INET6)
			agentx_varbind_integer(sav, 2);
	} else if (agentx_varbind_get_object(sav) == relaydSessionOutAddr)
		agentx_varbind_nstring(sav, sstodata(&(session->se_out.ss)),
		    sstolen(&(session->se_out.ss)));
	else if (agentx_varbind_get_object(sav) == relaydSessionOutAddrType) {
		if (session->se_out.ss.ss_family == AF_INET)
			agentx_varbind_integer(sav, 1);
		else if (session->se_out.ss.ss_family == AF_INET6)
			agentx_varbind_integer(sav, 2);
		else
			agentx_varbind_integer(sav, 0);
	} else if (agentx_varbind_get_object(sav) == relaydSessionPortIn)
		agentx_varbind_integer(sav, session->se_in.port);
	else if (agentx_varbind_get_object(sav) == relaydSessionPortOut)
		agentx_varbind_integer(sav, session->se_out.port);
	else if (agentx_varbind_get_object(sav) == relaydSessionAge) {
		getmonotime(&now);
		timersub(&now, &session->se_tv_start, &tv);
		agentx_varbind_timeticks(sav,
		    tv.tv_sec * 100 + tv.tv_usec / 10000);
	} else if (agentx_varbind_get_object(sav) == relaydSessionIdle) {
		getmonotime(&now);
		timersub(&now, &session->se_tv_last, &tv);
		agentx_varbind_timeticks(sav,
		    tv.tv_sec * 100 + tv.tv_usec / 10000);
	} else if (agentx_varbind_get_object(sav) == relaydSessionStatus) {
		if (session->se_done)
			agentx_varbind_integer(sav, 1);
		else
			agentx_varbind_integer(sav, 0);
	} else if (agentx_varbind_get_object(sav) == relaydSessionPid)
		agentx_varbind_integer(sav, session->se_pid);
}

struct table *
agentxctl_table_byidx(uint32_t instanceidx, enum agentx_request_type type)
{
	struct table		*table;

	TAILQ_FOREACH(table, env->sc_tables, entry) {
		if (table->conf.id == instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET ||
			    type == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE)
				return table;
			else
				return TAILQ_NEXT(table, entry);
		} else if (table->conf.id > instanceidx) {
			if (type == AGENTX_REQUEST_TYPE_GET)
				return NULL;
			return table;
		}
	}

	return NULL;
}

void
agentxctl_table(struct agentx_varbind *sav)
{
	struct table	*table;

	table = agentxctl_table_byidx(agentx_varbind_get_index_integer(sav,
	    relaydTableIdx), agentx_varbind_request(sav));
	if (table == NULL || table->conf.id > INT32_MAX) {
		agentx_varbind_notfound(sav);
		return;
	}
	agentx_varbind_set_index_integer(sav, relaydTableIdx,
	    table->conf.id);
	if (agentx_varbind_get_object(sav) == relaydTableIndex)
		agentx_varbind_integer(sav, table->conf.id);
	else if (agentx_varbind_get_object(sav) == relaydTableName)
		agentx_varbind_string(sav, table->conf.name);
	else if (agentx_varbind_get_object(sav) == relaydTableStatus) {
		if (TAILQ_EMPTY(&table->hosts))
			agentx_varbind_integer(sav, 1);
		else if (table->conf.flags & F_DISABLE)
			agentx_varbind_integer(sav, 2);
		else
			agentx_varbind_integer(sav, 0);
	}

}
#if 0

int
snmp_element(const char *oidstr, enum snmp_type type, void *buf, int64_t val,
    struct agentx_pdu *pdu)
{
	u_int32_t		 d;
	u_int64_t		 l;
	struct snmp_oid		 oid;

	DPRINTF("%s: oid %s type %d buf %p val %lld", __func__,
	    oidstr, type, buf, val);

	if (snmp_string2oid(oidstr, &oid) == -1)
		return -1;

	switch (type) {
	case SNMP_GAUGE32:
	case SNMP_NSAPADDR:
	case SNMP_INTEGER32:
	case SNMP_UINTEGER32:
		d = (u_int32_t)val;
		if (snmp_agentx_varbind(pdu, &oid, AGENTX_INTEGER,
		    &d, sizeof(d)) == -1)
			return -1;
		break;

	case SNMP_COUNTER32:
		d = (u_int32_t)val;
		if (snmp_agentx_varbind(pdu, &oid, AGENTX_COUNTER32,
		    &d, sizeof(d)) == -1)
			return -1;
		break;

	case SNMP_TIMETICKS:
		d = (u_int32_t)val;
		if (snmp_agentx_varbind(pdu, &oid, AGENTX_TIME_TICKS,
		    &d, sizeof(d)) == -1)
			return -1;
		break;

	case SNMP_COUNTER64:
		l = (u_int64_t)val;
		if (snmp_agentx_varbind(pdu, &oid, AGENTX_COUNTER64,
		    &l, sizeof(l)) == -1)
			return -1;
		break;

	case SNMP_IPADDR:
	case SNMP_OPAQUE:
		d = (u_int32_t)val;
		if (snmp_agentx_varbind(pdu, &oid, AGENTX_OPAQUE,
		    buf, strlen(buf)) == -1)
			return -1;
		break;

	case SNMP_OBJECT: {
		struct snmp_oid		oid1;

		if (snmp_string2oid(buf, &oid1) == -1)
			return -1;
		if (snmp_agentx_varbind(pdu, &oid, AGENTX_OBJECT_IDENTIFIER,
		    &oid1, sizeof(oid1)) == -1)
			return -1;
	}

	case SNMP_BITSTRING:
	case SNMP_OCTETSTRING:
		if (snmp_agentx_varbind(pdu, &oid, AGENTX_OCTET_STRING,
		    buf, strlen(buf)) == -1)
			return -1;
		break;

	case SNMP_NULL:
		/* no data beyond the OID itself */
		if (snmp_agentx_varbind(pdu, &oid, AGENTX_NULL,
		    NULL, 0) == -1)
			return -1;
	}

	return 0;
}

/*
 * SNMP traps for relayd
 */

void
snmp_hosttrap(struct relayd *env, struct table *table, struct host *host)
{
	struct agentx_pdu *pdu;

	if (snmp_agentx == NULL || env->sc_snmp == -1)
		return;

	/*
	 * OPENBSD-RELAYD-MIB host status trap
	 * XXX The trap format needs some tweaks and other OIDs
	 */

	if ((pdu = snmp_agentx_notify_pdu(&hosttrapoid)) == NULL)
		return;

	SNMP_ELEMENT(".1.0", SNMP_NULL, NULL, 0, pdu);
	SNMP_ELEMENT(".1.1.0", SNMP_OCTETSTRING, host->conf.name, 0, pdu);
	SNMP_ELEMENT(".1.2.0", SNMP_INTEGER32, NULL, host->up, pdu);
	SNMP_ELEMENT(".1.3.0", SNMP_INTEGER32, NULL, host->last_up, pdu);
	SNMP_ELEMENT(".1.4.0", SNMP_INTEGER32, NULL, host->up_cnt, pdu);
	SNMP_ELEMENT(".1.5.0", SNMP_INTEGER32, NULL, host->check_cnt, pdu);
	SNMP_ELEMENT(".1.6.0", SNMP_OCTETSTRING, table->conf.name, 0, pdu);
	SNMP_ELEMENT(".1.7.0", SNMP_INTEGER32, NULL, table->up, pdu);
	if (!host->conf.retry)
		goto done;
	SNMP_ELEMENT(".1.8.0", SNMP_INTEGER32, NULL, host->conf.retry, pdu);
	SNMP_ELEMENT(".1.9.0", SNMP_INTEGER32, NULL, host->retry_cnt, pdu);

 done:
	snmp_agentx_send(snmp_agentx, pdu);
	snmp_event_add(env, EV_WRITE);
}

int
snmp_string2oid(const char *oidstr, struct snmp_oid *o)
{
	char			*sp, *p, str[BUFSIZ];
	const char		*errstr;

	if (strlcpy(str, oidstr, sizeof(str)) >= sizeof(str))
		return -1;
	bzero(o, sizeof(*o));

	for (p = sp = str; p != NULL; sp = p) {
		if ((p = strpbrk(p, ".-")) != NULL)
			*p++ = '\0';
		o->o_id[o->o_n++] = strtonum(sp, 0, UINT_MAX, &errstr);
		if (errstr || o->o_n > SNMP_MAX_OID_LEN)
			return -1;
	}

	return 0;
}
#endif
