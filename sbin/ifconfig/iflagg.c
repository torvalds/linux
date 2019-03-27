/*-
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <stdlib.h>
#include <unistd.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_lagg.h>
#include <net/ieee8023ad_lacp.h>
#include <net/route.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

char lacpbuf[120];	/* LACP peer '[(a,a,a),(p,p,p)]' */

static void
setlaggport(const char *val, int d, int s, const struct afswtch *afp)
{
	struct lagg_reqport rp;

	bzero(&rp, sizeof(rp));
	strlcpy(rp.rp_ifname, name, sizeof(rp.rp_ifname));
	strlcpy(rp.rp_portname, val, sizeof(rp.rp_portname));

	/*
	 * Do not exit with an error here.  Doing so permits a
	 * failed NIC to take down an entire lagg.
	 *
	 * Don't error at all if the port is already in the lagg.
	 */
	if (ioctl(s, SIOCSLAGGPORT, &rp) && errno != EEXIST) {
		warnx("%s %s: SIOCSLAGGPORT: %s",
		    name, val, strerror(errno));
		exit_code = 1;
	}
}

static void
unsetlaggport(const char *val, int d, int s, const struct afswtch *afp)
{
	struct lagg_reqport rp;

	bzero(&rp, sizeof(rp));
	strlcpy(rp.rp_ifname, name, sizeof(rp.rp_ifname));
	strlcpy(rp.rp_portname, val, sizeof(rp.rp_portname));

	if (ioctl(s, SIOCSLAGGDELPORT, &rp))
		err(1, "SIOCSLAGGDELPORT");
}

static void
setlaggproto(const char *val, int d, int s, const struct afswtch *afp)
{
	struct lagg_protos lpr[] = LAGG_PROTOS;
	struct lagg_reqall ra;
	int i;

	bzero(&ra, sizeof(ra));
	ra.ra_proto = LAGG_PROTO_MAX;

	for (i = 0; i < nitems(lpr); i++) {
		if (strcmp(val, lpr[i].lpr_name) == 0) {
			ra.ra_proto = lpr[i].lpr_proto;
			break;
		}
	}
	if (ra.ra_proto == LAGG_PROTO_MAX)
		errx(1, "Invalid aggregation protocol: %s", val);

	strlcpy(ra.ra_ifname, name, sizeof(ra.ra_ifname));
	if (ioctl(s, SIOCSLAGG, &ra) != 0)
		err(1, "SIOCSLAGG");
}

static void
setlaggflowidshift(const char *val, int d, int s, const struct afswtch *afp)
{
	struct lagg_reqopts ro;

	bzero(&ro, sizeof(ro));
	ro.ro_opts = LAGG_OPT_FLOWIDSHIFT;
	strlcpy(ro.ro_ifname, name, sizeof(ro.ro_ifname));
	ro.ro_flowid_shift = (int)strtol(val, NULL, 10);
	if (ro.ro_flowid_shift & ~LAGG_OPT_FLOWIDSHIFT_MASK)
		errx(1, "Invalid flowid_shift option: %s", val);
	
	if (ioctl(s, SIOCSLAGGOPTS, &ro) != 0)
		err(1, "SIOCSLAGGOPTS");
}

static void
setlaggrr_limit(const char *val, int d, int s, const struct afswtch *afp)
{
	struct lagg_reqopts ro;
	
	bzero(&ro, sizeof(ro));
	strlcpy(ro.ro_ifname, name, sizeof(ro.ro_ifname));
	ro.ro_bkt = (int)strtol(val, NULL, 10);

	if (ioctl(s, SIOCSLAGGOPTS, &ro) != 0)
		err(1, "SIOCSLAGG");
}

static void
setlaggsetopt(const char *val, int d, int s, const struct afswtch *afp)
{
	struct lagg_reqopts ro;

	bzero(&ro, sizeof(ro));
	ro.ro_opts = d;
	switch (ro.ro_opts) {
	case LAGG_OPT_USE_FLOWID:
	case -LAGG_OPT_USE_FLOWID:
	case LAGG_OPT_LACP_STRICT:
	case -LAGG_OPT_LACP_STRICT:
	case LAGG_OPT_LACP_TXTEST:
	case -LAGG_OPT_LACP_TXTEST:
	case LAGG_OPT_LACP_RXTEST:
	case -LAGG_OPT_LACP_RXTEST:
	case LAGG_OPT_LACP_TIMEOUT:
	case -LAGG_OPT_LACP_TIMEOUT:
		break;
	default:
		err(1, "Invalid lagg option");
	}
	strlcpy(ro.ro_ifname, name, sizeof(ro.ro_ifname));
	
	if (ioctl(s, SIOCSLAGGOPTS, &ro) != 0)
		err(1, "SIOCSLAGGOPTS");
}

static void
setlagghash(const char *val, int d, int s, const struct afswtch *afp)
{
	struct lagg_reqflags rf;
	char *str, *tmp, *tok;


	rf.rf_flags = 0;
	str = tmp = strdup(val);
	while ((tok = strsep(&tmp, ",")) != NULL) {
		if (strcmp(tok, "l2") == 0)
			rf.rf_flags |= LAGG_F_HASHL2;
		else if (strcmp(tok, "l3") == 0)
			rf.rf_flags |= LAGG_F_HASHL3;
		else if (strcmp(tok, "l4") == 0)
			rf.rf_flags |= LAGG_F_HASHL4;
		else
			errx(1, "Invalid lagghash option: %s", tok);
	}
	free(str);
	if (rf.rf_flags == 0)
		errx(1, "No lagghash options supplied");

	strlcpy(rf.rf_ifname, name, sizeof(rf.rf_ifname));
	if (ioctl(s, SIOCSLAGGHASH, &rf))
		err(1, "SIOCSLAGGHASH");
}

static char *
lacp_format_mac(const uint8_t *mac, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%02X-%02X-%02X-%02X-%02X-%02X",
	    (int)mac[0], (int)mac[1], (int)mac[2], (int)mac[3],
	    (int)mac[4], (int)mac[5]);

	return (buf);
}

static char *
lacp_format_peer(struct lacp_opreq *req, const char *sep)
{
	char macbuf1[20];
	char macbuf2[20];

	snprintf(lacpbuf, sizeof(lacpbuf),
	    "[(%04X,%s,%04X,%04X,%04X),%s(%04X,%s,%04X,%04X,%04X)]",
	    req->actor_prio,
	    lacp_format_mac(req->actor_mac, macbuf1, sizeof(macbuf1)),
	    req->actor_key, req->actor_portprio, req->actor_portno, sep,
	    req->partner_prio,
	    lacp_format_mac(req->partner_mac, macbuf2, sizeof(macbuf2)),
	    req->partner_key, req->partner_portprio, req->partner_portno);

	return(lacpbuf);
}

static void
lagg_status(int s)
{
	struct lagg_protos lpr[] = LAGG_PROTOS;
	struct lagg_reqport rpbuf[LAGG_MAX_PORTS];
	struct lagg_reqall ra;
	struct lagg_reqopts ro;
	struct lagg_reqflags rf;
	struct lacp_opreq *lp;
	const char *proto = "<unknown>";
	int i;

	bzero(&ra, sizeof(ra));
	bzero(&ro, sizeof(ro));

	strlcpy(ra.ra_ifname, name, sizeof(ra.ra_ifname));
	ra.ra_size = sizeof(rpbuf);
	ra.ra_port = rpbuf;

	strlcpy(ro.ro_ifname, name, sizeof(ro.ro_ifname));
	ioctl(s, SIOCGLAGGOPTS, &ro);

	strlcpy(rf.rf_ifname, name, sizeof(rf.rf_ifname));
	if (ioctl(s, SIOCGLAGGFLAGS, &rf) != 0)
		rf.rf_flags = 0;

	if (ioctl(s, SIOCGLAGG, &ra) == 0) {
		lp = (struct lacp_opreq *)&ra.ra_lacpreq;

		for (i = 0; i < nitems(lpr); i++) {
			if (ra.ra_proto == lpr[i].lpr_proto) {
				proto = lpr[i].lpr_name;
				break;
			}
		}

		printf("\tlaggproto %s", proto);
		if (rf.rf_flags & LAGG_F_HASHMASK) {
			const char *sep = "";

			printf(" lagghash ");
			if (rf.rf_flags & LAGG_F_HASHL2) {
				printf("%sl2", sep);
				sep = ",";
			}
			if (rf.rf_flags & LAGG_F_HASHL3) {
				printf("%sl3", sep);
				sep = ",";
			}
			if (rf.rf_flags & LAGG_F_HASHL4) {
				printf("%sl4", sep);
				sep = ",";
			}
		}
		putchar('\n');
		if (verbose) {
			printf("\tlagg options:\n");
			printb("\t\tflags", ro.ro_opts, LAGG_OPT_BITS);
			putchar('\n');
			printf("\t\tflowid_shift: %d\n", ro.ro_flowid_shift);
			if (ra.ra_proto == LAGG_PROTO_ROUNDROBIN)
				printf("\t\trr_limit: %d\n", ro.ro_bkt);
			printf("\tlagg statistics:\n");
			printf("\t\tactive ports: %d\n", ro.ro_active);
			printf("\t\tflapping: %u\n", ro.ro_flapping);
			if (ra.ra_proto == LAGG_PROTO_LACP) {
				printf("\tlag id: %s\n",
				    lacp_format_peer(lp, "\n\t\t "));
			}
		}

		for (i = 0; i < ra.ra_ports; i++) {
			lp = (struct lacp_opreq *)&rpbuf[i].rp_lacpreq;
			printf("\tlaggport: %s ", rpbuf[i].rp_portname);
			printb("flags", rpbuf[i].rp_flags, LAGG_PORT_BITS);
			if (verbose && ra.ra_proto == LAGG_PROTO_LACP)
				printb(" state", lp->actor_state,
				    LACP_STATE_BITS);
			putchar('\n');
			if (verbose && ra.ra_proto == LAGG_PROTO_LACP)
				printf("\t\t%s\n",
				    lacp_format_peer(lp, "\n\t\t "));
		}

		if (0 /* XXX */) {
			printf("\tsupported aggregation protocols:\n");
			for (i = 0; i < nitems(lpr); i++)
				printf("\t\tlaggproto %s\n", lpr[i].lpr_name);
		}
	}
}

static struct cmd lagg_cmds[] = {
	DEF_CMD_ARG("laggport",		setlaggport),
	DEF_CMD_ARG("-laggport",	unsetlaggport),
	DEF_CMD_ARG("laggproto",	setlaggproto),
	DEF_CMD_ARG("lagghash",		setlagghash),
	DEF_CMD("use_flowid",	LAGG_OPT_USE_FLOWID,	setlaggsetopt),
	DEF_CMD("-use_flowid",	-LAGG_OPT_USE_FLOWID,	setlaggsetopt),
	DEF_CMD("lacp_strict",	LAGG_OPT_LACP_STRICT,	setlaggsetopt),
	DEF_CMD("-lacp_strict",	-LAGG_OPT_LACP_STRICT,	setlaggsetopt),
	DEF_CMD("lacp_txtest",	LAGG_OPT_LACP_TXTEST,	setlaggsetopt),
	DEF_CMD("-lacp_txtest",	-LAGG_OPT_LACP_TXTEST,	setlaggsetopt),
	DEF_CMD("lacp_rxtest",	LAGG_OPT_LACP_RXTEST,	setlaggsetopt),
	DEF_CMD("-lacp_rxtest",	-LAGG_OPT_LACP_RXTEST,	setlaggsetopt),
	DEF_CMD("lacp_fast_timeout",	LAGG_OPT_LACP_TIMEOUT,	setlaggsetopt),
	DEF_CMD("-lacp_fast_timeout",	-LAGG_OPT_LACP_TIMEOUT,	setlaggsetopt),
	DEF_CMD_ARG("flowid_shift",	setlaggflowidshift),
	DEF_CMD_ARG("rr_limit",		setlaggrr_limit),
};
static struct afswtch af_lagg = {
	.af_name	= "af_lagg",
	.af_af		= AF_UNSPEC,
	.af_other_status = lagg_status,
};

static __constructor void
lagg_ctor(void)
{
	int i;

	for (i = 0; i < nitems(lagg_cmds);  i++)
		cmd_register(&lagg_cmds[i]);
	af_register(&af_lagg);
}
