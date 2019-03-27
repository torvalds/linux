/*
 * debug.c
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@whistle.com>
 *
 * $Whistle: debug.c,v 1.24 1999/01/24 01:15:33 archie Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <stdarg.h>

#include <netinet/in.h>
#include <net/ethernet.h>
#include <net/bpf.h>

#include <netgraph/ng_message.h>
#include <netgraph/ng_socket.h>

#include "netgraph.h"
#include "internal.h"

#include <netgraph/ng_UI.h>
#include <netgraph/ng_async.h>
#include <netgraph/ng_atmllc.h>
#include <netgraph/ng_bpf.h>
#include <netgraph/ng_bridge.h>
#include <netgraph/ng_car.h>
#include <netgraph/ng_cisco.h>
#include <netgraph/ng_deflate.h>
#include <netgraph/ng_device.h>
#include <netgraph/ng_echo.h>
#include <netgraph/ng_eiface.h>
#include <netgraph/ng_etf.h>
#include <netgraph/ng_ether.h>
#include <netgraph/ng_ether_echo.h>
#include <netgraph/ng_frame_relay.h>
#include <netgraph/ng_gif.h>
#include <netgraph/ng_gif_demux.h>
#include <netgraph/ng_hole.h>
#include <netgraph/ng_hub.h>
#include <netgraph/ng_iface.h>
#include <netgraph/ng_ip_input.h>
#include <netgraph/ng_ipfw.h>
#include <netgraph/ng_ksocket.h>
#include <netgraph/ng_l2tp.h>
#include <netgraph/ng_lmi.h>
#include <netgraph/ng_mppc.h>
#include <netgraph/ng_nat.h>
#include <netgraph/netflow/ng_netflow.h>
#include <netgraph/ng_one2many.h>
#include <netgraph/ng_patch.h>
#include <netgraph/ng_pipe.h>
#include <netgraph/ng_ppp.h>
#include <netgraph/ng_pppoe.h>
#include <netgraph/ng_pptpgre.h>
#include <netgraph/ng_pred1.h>
#include <netgraph/ng_rfc1490.h>
#include <netgraph/ng_socket.h>
#include <netgraph/ng_source.h>
#include <netgraph/ng_split.h>
#include <netgraph/ng_sppp.h>
#include <netgraph/ng_tag.h>
#include <netgraph/ng_tcpmss.h>
#include <netgraph/ng_tee.h>
#include <netgraph/ng_tty.h>
#include <netgraph/ng_vjc.h>
#include <netgraph/ng_vlan.h>
#ifdef	WHISTLE
#include <machine/../isa/df_def.h>
#include <machine/../isa/if_wfra.h>
#include <machine/../isa/ipac.h>
#include <netgraph/ng_df.h>
#include <netgraph/ng_ipac.h>
#include <netgraph/ng_tn.h>
#endif

/* Global debug level */
int     _gNgDebugLevel = 0;

/* Debug printing functions */
void    (*_NgLog) (const char *fmt,...) = warn;
void    (*_NgLogx) (const char *fmt,...) = warnx;

/* Internal functions */
static const	char *NgCookie(int cookie);

/* Known typecookie list */
struct ng_cookie {
	int		cookie;
	const char	*type;
};

#define COOKIE(c)	{ NGM_ ## c ## _COOKIE, #c }

/* List of known cookies */
static const struct ng_cookie cookies[] = {
	COOKIE(UI),
	COOKIE(ASYNC),
	COOKIE(ATMLLC),
	COOKIE(BPF),
	COOKIE(BRIDGE),
	COOKIE(CAR),
	COOKIE(CISCO),
	COOKIE(DEFLATE),
	COOKIE(DEVICE),
	COOKIE(ECHO),
	COOKIE(EIFACE),
	COOKIE(ETF),
	COOKIE(ETHER),
	COOKIE(ETHER_ECHO),
	COOKIE(FRAMERELAY),
	COOKIE(GIF),
	COOKIE(GIF_DEMUX),
	COOKIE(GENERIC),
	COOKIE(HOLE),
	COOKIE(HUB),
	COOKIE(IFACE),
	COOKIE(IP_INPUT),
	COOKIE(IPFW),
	COOKIE(KSOCKET),
	COOKIE(L2TP),
	COOKIE(LMI),
	COOKIE(MPPC),
	COOKIE(NAT),
	COOKIE(NETFLOW),
	COOKIE(ONE2MANY),
	COOKIE(PATCH),
	COOKIE(PIPE),
	COOKIE(PPP),
	COOKIE(PPPOE),
	COOKIE(PPTPGRE),
	COOKIE(PRED1),
	COOKIE(RFC1490),
	COOKIE(SOCKET),
	COOKIE(SOURCE),
	COOKIE(SPLIT),
	COOKIE(SPPP),
	COOKIE(TAG),
	COOKIE(TCPMSS),
	COOKIE(TEE),
	COOKIE(TTY),
	COOKIE(VJC),
	COOKIE(VLAN),
#ifdef WHISTLE
	COOKIE(DF),
	COOKIE(IPAC),
	COOKIE(TN),
	COOKIE(WFRA),
#endif
	{ 0, NULL }
};

/*
 * Set debug level, ie, verbosity, if "level" is non-negative.
 * Returns old debug level.
 */
int
NgSetDebug(int level)
{
	int old = _gNgDebugLevel;

	if (level >= 0)
		_gNgDebugLevel = level;
	return (old);
}

/*
 * Set debug logging functions.
 */
void
NgSetErrLog(void (*log) (const char *fmt,...),
		void (*logx) (const char *fmt,...))
{
	_NgLog = log;
	_NgLogx = logx;
}

/*
 * Display a netgraph sockaddr
 */
void
_NgDebugSockaddr(const struct sockaddr_ng *sg)
{
	NGLOGX("SOCKADDR: { fam=%d len=%d addr=\"%s\" }",
	       sg->sg_family, sg->sg_len, sg->sg_data);
}

#define ARGS_BUFSIZE		2048
#define RECURSIVE_DEBUG_ADJUST	4

/*
 * Display a negraph message
 */
void
_NgDebugMsg(const struct ng_mesg *msg, const char *path)
{
	u_char buf[2 * sizeof(struct ng_mesg) + ARGS_BUFSIZE];
	struct ng_mesg *const req = (struct ng_mesg *)buf;
	struct ng_mesg *const bin = (struct ng_mesg *)req->data;
	int arglen, csock = -1;

	/* Display header stuff */
	NGLOGX("NG_MESG :");
	NGLOGX("  vers   %d", msg->header.version);
	NGLOGX("  arglen %u", msg->header.arglen);
	NGLOGX("  flags  %x", msg->header.flags);
	NGLOGX("  token  %u", msg->header.token);
	NGLOGX("  cookie %s (%u)",
	    NgCookie(msg->header.typecookie), msg->header.typecookie);

	/* At lower debugging levels, skip ASCII translation */
	if (_gNgDebugLevel <= 2)
		goto fail2;

	/* If path is not absolute, don't bother trying to use relative
	   address on a different socket for the ASCII translation */
	if (strchr(path, ':') == NULL)
		goto fail2;

	/* Get a temporary socket */
	if (NgMkSockNode(NULL, &csock, NULL) < 0)
		goto fail;

	/* Copy binary message into request message payload */
	arglen = msg->header.arglen;
	if (arglen > ARGS_BUFSIZE)
		arglen = ARGS_BUFSIZE;
	memcpy(bin, msg, sizeof(*msg) + arglen);
	bin->header.arglen = arglen;

	/* Lower debugging to avoid infinite recursion */
	_gNgDebugLevel -= RECURSIVE_DEBUG_ADJUST;

	/* Ask the node to translate the binary message to ASCII for us */
	if (NgSendMsg(csock, path, NGM_GENERIC_COOKIE,
	    NGM_BINARY2ASCII, bin, sizeof(*bin) + bin->header.arglen) < 0) {
		_gNgDebugLevel += RECURSIVE_DEBUG_ADJUST;
		goto fail;
	}
	if (NgRecvMsg(csock, req, sizeof(buf), NULL) < 0) {
		_gNgDebugLevel += RECURSIVE_DEBUG_ADJUST;
		goto fail;
	}

	/* Restore debugging level */
	_gNgDebugLevel += RECURSIVE_DEBUG_ADJUST;

	/* Display command string and arguments */
	NGLOGX("  cmd    %s (%d)", bin->header.cmdstr, bin->header.cmd);
	NGLOGX("  args   %s", bin->data);
	goto done;

fail:
	/* Just display binary version */
	NGLOGX("  [error decoding message: %s]", strerror(errno));
fail2:
	NGLOGX("  cmd    %d", msg->header.cmd);
	NGLOGX("  args (%d bytes)", msg->header.arglen);
	_NgDebugBytes((u_char *)msg->data, msg->header.arglen);

done:
	if (csock != -1)
		(void)close(csock);
}

/*
 * Return the name of the node type corresponding to the cookie
 */
static const char *
NgCookie(int cookie)
{
	int k;

	for (k = 0; cookies[k].cookie != 0; k++) {
		if (cookies[k].cookie == cookie)
			return cookies[k].type;
	}
	return "??";
}

/*
 * Dump bytes in hex
 */
void
_NgDebugBytes(const u_char *ptr, int len)
{
	char    buf[100];
	int     k, count;

#define BYPERLINE	16

	for (count = 0; count < len; ptr += BYPERLINE, count += BYPERLINE) {

		/* Do hex */
		snprintf(buf, sizeof(buf), "%04x:  ", count);
		for (k = 0; k < BYPERLINE; k++, count++)
			if (count < len)
				snprintf(buf + strlen(buf),
				    sizeof(buf) - strlen(buf), "%02x ", ptr[k]);
			else
				snprintf(buf + strlen(buf),
				    sizeof(buf) - strlen(buf), "   ");
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "  ");
		count -= BYPERLINE;

		/* Do ASCII */
		for (k = 0; k < BYPERLINE; k++, count++)
			if (count < len)
				snprintf(buf + strlen(buf),
				    sizeof(buf) - strlen(buf),
				    "%c", isprint(ptr[k]) ? ptr[k] : '.');
			else
				snprintf(buf + strlen(buf),
				    sizeof(buf) - strlen(buf), "  ");
		count -= BYPERLINE;

		/* Print it */
		NGLOGX("%s", buf);
	}
}

