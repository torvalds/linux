/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define	RIPCMDS
#include "defs.h"
#include "pathnames.h"
#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>

#ifdef __NetBSD__
__RCSID("$NetBSD$");
#elif defined(__FreeBSD__)
__RCSID("$FreeBSD$");
#else
__RCSID("$Revision: 2.27 $");
#ident "$Revision: 2.27 $"
#endif


#ifdef sgi
/* use *stat64 for files on large file systems */
#define stat	stat64
#endif

int	tracelevel, new_tracelevel;
FILE	*ftrace;			/* output trace file */
static const char *sigtrace_pat = "%s";
static char savetracename[PATH_MAX];
char	inittracename[PATH_MAX];
static int file_trace;			/* 1=tracing to file, not stdout */

static void trace_dump(void);
static void tmsg(const char *, ...) PATTRIB(1,2);


/* convert string to printable characters
 */
static char *
qstring(u_char *s, int len)
{
	static char buf[8*20+1];
	char *p;
	u_char *s2, c;


	for (p = buf; len != 0 && p < &buf[sizeof(buf)-1]; len--) {
		c = *s++;
		if (c == '\0') {
			for (s2 = s+1; s2 < &s[len]; s2++) {
				if (*s2 != '\0')
					break;
			}
			if (s2 >= &s[len])
			    goto exit;
		}

		if (c >= ' ' && c < 0x7f && c != '\\') {
			*p++ = c;
			continue;
		}
		*p++ = '\\';
		switch (c) {
		case '\\':
			*p++ = '\\';
			break;
		case '\n':
			*p++= 'n';
			break;
		case '\r':
			*p++= 'r';
			break;
		case '\t':
			*p++ = 't';
			break;
		case '\b':
			*p++ = 'b';
			break;
		default:
			p += sprintf(p,"%o",c);
			break;
		}
	}
exit:
	*p = '\0';
	return buf;
}


/* convert IP address to a string, but not into a single buffer
 */
char *
naddr_ntoa(naddr a)
{
#define NUM_BUFS 4
	static int bufno;
	static struct {
	    char    str[16];		/* xxx.xxx.xxx.xxx\0 */
	} bufs[NUM_BUFS];
	char *s;
	struct in_addr addr;

	addr.s_addr = a;
	s = strcpy(bufs[bufno].str, inet_ntoa(addr));
	bufno = (bufno+1) % NUM_BUFS;
	return s;
#undef NUM_BUFS
}


const char *
saddr_ntoa(struct sockaddr *sa)
{
	return (sa == NULL) ? "?" : naddr_ntoa(S_ADDR(sa));
}


static char *
ts(time_t secs) {
	static char s[20];

	secs += epoch.tv_sec;
#ifdef sgi
	(void)cftime(s, "%T", &secs);
#else
	memcpy(s, ctime(&secs)+11, 8);
	s[8] = '\0';
#endif
	return s;
}


/* On each event, display a time stamp.
 * This assumes that 'now' is update once for each event, and
 * that at least now.tv_usec changes.
 */
static struct timeval lastlog_time;

void
lastlog(void)
{
	if (lastlog_time.tv_sec != now.tv_sec
	    || lastlog_time.tv_usec != now.tv_usec) {
		(void)fprintf(ftrace, "-- %s --\n", ts(now.tv_sec));
		lastlog_time = now;
	}
}


static void
tmsg(const char *p, ...)
{
	va_list args;

	if (ftrace != NULL) {
		lastlog();
		va_start(args, p);
		vfprintf(ftrace, p, args);
		va_end(args);
		(void)fputc('\n',ftrace);
		fflush(ftrace);
	}
}


void
trace_close(int zap_stdio)
{
	int fd;


	fflush(stdout);
	fflush(stderr);

	if (ftrace != NULL && zap_stdio) {
		if (ftrace != stdout)
			fclose(ftrace);
		ftrace = NULL;
		fd = open(_PATH_DEVNULL, O_RDWR);
		if (fd < 0)
			return;
		if (isatty(STDIN_FILENO))
			(void)dup2(fd, STDIN_FILENO);
		if (isatty(STDOUT_FILENO))
			(void)dup2(fd, STDOUT_FILENO);
		if (isatty(STDERR_FILENO))
			(void)dup2(fd, STDERR_FILENO);
		(void)close(fd);
	}
	lastlog_time.tv_sec = 0;
}


void
trace_flush(void)
{
	if (ftrace != NULL) {
		fflush(ftrace);
		if (ferror(ftrace))
			trace_off("tracing off: %s", strerror(ferror(ftrace)));
	}
}


void
trace_off(const char *p, ...)
{
	va_list args;


	if (ftrace != NULL) {
		lastlog();
		va_start(args, p);
		vfprintf(ftrace, p, args);
		va_end(args);
		(void)fputc('\n',ftrace);
	}
	trace_close(file_trace);

	new_tracelevel = tracelevel = 0;
}


/* log a change in tracing
 */
void
tracelevel_msg(const char *pat,
	       int dump)		/* -1=no dump, 0=default, 1=force */
{
	static const char * const off_msgs[MAX_TRACELEVEL] = {
		"Tracing actions stopped",
		"Tracing packets stopped",
		"Tracing packet contents stopped",
		"Tracing kernel changes stopped",
	};
	static const char * const on_msgs[MAX_TRACELEVEL] = {
		"Tracing actions started",
		"Tracing packets started",
		"Tracing packet contents started",
		"Tracing kernel changes started",
	};
	u_int old_tracelevel = tracelevel;


	if (new_tracelevel < 0)
		new_tracelevel = 0;
	else if (new_tracelevel > MAX_TRACELEVEL)
		new_tracelevel = MAX_TRACELEVEL;

	if (new_tracelevel < tracelevel) {
		if (new_tracelevel <= 0) {
			trace_off(pat, off_msgs[0]);
		} else do {
			tmsg(pat, off_msgs[tracelevel]);
		}
		while (--tracelevel != new_tracelevel);

	} else if (new_tracelevel > tracelevel) {
		do {
			tmsg(pat, on_msgs[tracelevel++]);
		} while (tracelevel != new_tracelevel);
	}

	if (dump > 0
	    || (dump == 0 && old_tracelevel == 0 && tracelevel != 0))
		trace_dump();
}


void
set_tracefile(const char *filename,
	      const char *pat,
	      int dump)			/* -1=no dump, 0=default, 1=force */
{
	struct stat stbuf;
	FILE *n_ftrace;
	const char *fn;


	/* Allow a null filename to increase the level if the trace file
	 * is already open or if coming from a trusted source, such as
	 * a signal or the command line.
	 */
	if (filename == NULL || filename[0] == '\0') {
		filename = NULL;
		if (ftrace == NULL) {
			if (inittracename[0] == '\0') {
				msglog("missing trace file name");
				return;
			}
			fn = inittracename;
		} else {
			fn = NULL;
		}

	} else if (!strcmp(filename,"dump/../table")) {
		trace_dump();
		return;

	} else {
		/* Allow the file specified with "-T file" to be reopened,
		 * but require all other names specified over the net to
		 * match the official path.  The path can specify a directory
		 * in which the file is to be created.
		 */
		if (strcmp(filename, inittracename)
#ifdef _PATH_TRACE
		    && (strncmp(filename, _PATH_TRACE, sizeof(_PATH_TRACE)-1)
			|| strstr(filename,"../")
			|| 0 > stat(_PATH_TRACE, &stbuf))
#endif
		    ) {
			msglog("wrong trace file \"%s\"", filename);
			return;
		}

		/* If the new tracefile exists, it must be a regular file.
		 */
		if (stat(filename, &stbuf) >= 0 && !S_ISREG(stbuf.st_mode)) {
			msglog("wrong type (%#x) of trace file \"%s\"",
			       stbuf.st_mode, filename);
			return;
		}

		fn = filename;
	}

	if (fn != NULL) {
		n_ftrace = fopen(fn, "a");
		if (n_ftrace == NULL) {
			msglog("failed to open trace file \"%s\" %s",
			       fn, strerror(errno));
			if (fn == inittracename)
				inittracename[0] = '\0';
			return;
		}

		tmsg("switch to trace file %s", fn);

		trace_close(file_trace = 1);

		if (fn != savetracename)
			strncpy(savetracename, fn, sizeof(savetracename)-1);
		ftrace = n_ftrace;

		fflush(stdout);
		fflush(stderr);
		dup2(fileno(ftrace), STDOUT_FILENO);
		dup2(fileno(ftrace), STDERR_FILENO);
	}

	if (new_tracelevel == 0 || filename == NULL)
		new_tracelevel++;
	tracelevel_msg(pat, dump != 0 ? dump : (filename != NULL));
}


/* ARGSUSED */
void
sigtrace_on(int s UNUSED)
{
	new_tracelevel++;
	sigtrace_pat = "SIGUSR1: %s";
}


/* ARGSUSED */
void
sigtrace_off(int s UNUSED)
{
	new_tracelevel--;
	sigtrace_pat = "SIGUSR2: %s";
}


/* Set tracing after a signal.
 */
void
set_tracelevel(void)
{
	if (new_tracelevel == tracelevel)
		return;

	/* If tracing entirely off, and there was no tracefile specified
	 * on the command line, then leave it off.
	 */
	if (new_tracelevel > tracelevel && ftrace == NULL) {
		if (savetracename[0] != '\0') {
			set_tracefile(savetracename,sigtrace_pat,0);
		} else if (inittracename[0] != '\0') {
				set_tracefile(inittracename,sigtrace_pat,0);
		} else {
			new_tracelevel = 0;
			return;
		}
	} else {
		tracelevel_msg(sigtrace_pat, 0);
	}
}


/* display an address
 */
char *
addrname(naddr	addr,			/* in network byte order */
	 naddr	mask,
	 int	force)			/* 0=show mask if nonstandard, */
{					/*	1=always show mask, 2=never */
#define NUM_BUFS 4
	static int bufno;
	static struct {
	    char    str[15+20];
	} bufs[NUM_BUFS];
	char *s, *sp;
	naddr dmask;
	size_t l;
	int i;

	strlcpy(bufs[bufno].str, naddr_ntoa(addr), sizeof(bufs[bufno].str));
	s = bufs[bufno].str;
	l = sizeof(bufs[bufno].str);
	bufno = (bufno+1) % NUM_BUFS;

	if (force == 1 || (force == 0 && mask != std_mask(addr))) {
		sp = &s[strlen(s)];

		dmask = mask & -mask;
		if (mask + dmask == 0) {
			for (i = 0; i != 32 && ((1<<i) & mask) == 0; i++)
				continue;
			(void)snprintf(sp, s + l - sp, "/%d", 32-i);

		} else {
			(void)snprintf(sp, s + l - sp, " (mask %#x)",
			    (u_int)mask);
		}
	}

	return s;
#undef NUM_BUFS
}


/* display a bit-field
 */
struct bits {
	u_int	bits_mask;
	u_int	bits_clear;
	const char *bits_name;
};

static const struct bits if_bits[] = {
	{ IFF_LOOPBACK,		0,		"LOOPBACK" },
	{ IFF_POINTOPOINT,	0,		"PT-TO-PT" },
	{ 0,			0,		0}
};

static const struct bits is_bits[] = {
	{ IS_ALIAS,		0,		"ALIAS" },
	{ IS_SUBNET,		0,		"" },
	{ IS_REMOTE,		(IS_NO_RDISC
				 | IS_BCAST_RDISC), "REMOTE" },
	{ IS_PASSIVE,		(IS_NO_RDISC
				 | IS_NO_RIP
				 | IS_NO_SUPER_AG
				 | IS_PM_RDISC
				 | IS_NO_AG),	"PASSIVE" },
	{ IS_EXTERNAL,		0,		"EXTERNAL" },
	{ IS_CHECKED,		0,		"" },
	{ IS_ALL_HOSTS,		0,		"" },
	{ IS_ALL_ROUTERS,	0,		"" },
	{ IS_DISTRUST,		0,		"DISTRUST" },
	{ IS_BROKE,		IS_SICK,	"BROKEN" },
	{ IS_SICK,		0,		"SICK" },
	{ IS_DUP,		0,		"DUPLICATE" },
	{ IS_REDIRECT_OK,	0,		"REDIRECT_OK" },
	{ IS_NEED_NET_SYN,	0,		"" },
	{ IS_NO_AG,		IS_NO_SUPER_AG,	"NO_AG" },
	{ IS_NO_SUPER_AG,	0,		"NO_SUPER_AG" },
	{ (IS_NO_RIPV1_IN
	   | IS_NO_RIPV2_IN
	   | IS_NO_RIPV1_OUT
	   | IS_NO_RIPV2_OUT),	0,		"NO_RIP" },
	{ (IS_NO_RIPV1_IN
	   | IS_NO_RIPV1_OUT),	0,		"RIPV2" },
	{ IS_NO_RIPV1_IN,	0,		"NO_RIPV1_IN" },
	{ IS_NO_RIPV2_IN,	0,		"NO_RIPV2_IN" },
	{ IS_NO_RIPV1_OUT,	0,		"NO_RIPV1_OUT" },
	{ IS_NO_RIPV2_OUT,	0,		"NO_RIPV2_OUT" },
	{ (IS_NO_ADV_IN
	   | IS_NO_SOL_OUT
	   | IS_NO_ADV_OUT),	IS_BCAST_RDISC,	"NO_RDISC" },
	{ IS_NO_SOL_OUT,	0,		"NO_SOLICIT" },
	{ IS_SOL_OUT,		0,		"SEND_SOLICIT" },
	{ IS_NO_ADV_OUT,	IS_BCAST_RDISC,	"NO_RDISC_ADV" },
	{ IS_ADV_OUT,		0,		"RDISC_ADV" },
	{ IS_BCAST_RDISC,	0,		"BCAST_RDISC" },
	{ IS_PM_RDISC,		0,		"" },
	{ 0,			0,		"%#x"}
};

static const struct bits rs_bits[] = {
	{ RS_IF,		0,		"IF" },
	{ RS_NET_INT,		RS_NET_SYN,	"NET_INT" },
	{ RS_NET_SYN,		0,		"NET_SYN" },
	{ RS_SUBNET,		0,		"" },
	{ RS_LOCAL,		0,		"LOCAL" },
	{ RS_MHOME,		0,		"MHOME" },
	{ RS_STATIC,		0,		"STATIC" },
	{ RS_RDISC,		0,		"RDISC" },
	{ 0,			0,		"%#x"}
};


static void
trace_bits(const struct bits *tbl,
	   u_int field,
	   int force)
{
	u_int b;
	char c;

	if (force) {
		(void)putc('<', ftrace);
		c = 0;
	} else {
		c = '<';
	}

	while (field != 0
	       && (b = tbl->bits_mask) != 0) {
		if ((b & field) == b) {
			if (tbl->bits_name[0] != '\0') {
				if (c)
					(void)putc(c, ftrace);
				(void)fprintf(ftrace, "%s", tbl->bits_name);
				c = '|';
			}
			if (0 == (field &= ~(b | tbl->bits_clear)))
				break;
		}
		tbl++;
	}
	if (field != 0 && tbl->bits_name != NULL) {
		if (c)
			(void)putc(c, ftrace);
		(void)fprintf(ftrace, tbl->bits_name, field);
		c = '|';
	}

	if (c != '<' || force)
		(void)fputs("> ", ftrace);
}


char *
rtname(naddr dst,
       naddr mask,
       naddr gate)
{
	static char buf[3*4+3+1+2+3	/* "xxx.xxx.xxx.xxx/xx-->" */
			+3*4+3+1];	/* "xxx.xxx.xxx.xxx" */
	int i;

	i = sprintf(buf, "%-16s-->", addrname(dst, mask, 0));
	(void)sprintf(&buf[i], "%-*s", 15+20-MAX(20,i), naddr_ntoa(gate));
	return buf;
}


static void
print_rts(struct rt_spare *rts,
	  int force_metric,		/* -1=suppress, 0=default */
	  int force_ifp,		/* -1=suppress, 0=default */
	  int force_router,		/* -1=suppress, 0=default, 1=display */
	  int force_tag,		/* -1=suppress, 0=default, 1=display */
	  int force_time)		/* 0=suppress, 1=display */
{
	int i;


	if (force_metric >= 0)
		(void)fprintf(ftrace, "metric=%-2d ", rts->rts_metric);
	if (force_ifp >= 0)
		(void)fprintf(ftrace, "%s ", (rts->rts_ifp == NULL ?
					      "if?" : rts->rts_ifp->int_name));
	if (force_router > 0
	    || (force_router == 0 && rts->rts_router != rts->rts_gate))
		(void)fprintf(ftrace, "router=%s ",
			      naddr_ntoa(rts->rts_router));
	if (force_time > 0)
		(void)fprintf(ftrace, "%s ", ts(rts->rts_time));
	if (force_tag > 0
	    || (force_tag == 0 && rts->rts_tag != 0))
		(void)fprintf(ftrace, "tag=%#x ", ntohs(rts->rts_tag));
	if (rts->rts_de_ag != 0) {
		for (i = 1; (u_int)(1 << i) <= rts->rts_de_ag; i++)
			continue;
		(void)fprintf(ftrace, "de_ag=%d ", i);
	}

}


void
trace_if(const char *act,
	 struct interface *ifp)
{
	if (!TRACEACTIONS || ftrace == NULL)
		return;

	lastlog();
	(void)fprintf(ftrace, "%-3s interface %-4s ", act, ifp->int_name);
	(void)fprintf(ftrace, "%-15s-->%-15s ",
		      naddr_ntoa(ifp->int_addr),
		      addrname(((ifp->int_if_flags & IFF_POINTOPOINT)
				? ifp->int_dstaddr
				: htonl(ifp->int_net)),
			       ifp->int_mask, 1));
	if (ifp->int_metric != 0)
		(void)fprintf(ftrace, "metric=%d ", ifp->int_metric);
	if (ifp->int_adj_inmetric != 0)
		(void)fprintf(ftrace, "adj_inmetric=%u ",
			      ifp->int_adj_inmetric);
	if (ifp->int_adj_outmetric != 0)
		(void)fprintf(ftrace, "adj_outmetric=%u ",
			      ifp->int_adj_outmetric);
	if (!IS_RIP_OUT_OFF(ifp->int_state)
	    && ifp->int_d_metric != 0)
		(void)fprintf(ftrace, "fake_default=%u ", ifp->int_d_metric);
	trace_bits(if_bits, ifp->int_if_flags, 0);
	trace_bits(is_bits, ifp->int_state, 0);
	(void)fputc('\n',ftrace);
}


void
trace_upslot(struct rt_entry *rt,
	     struct rt_spare *rts,
	     struct rt_spare *new)
{
	if (!TRACEACTIONS || ftrace == NULL)
		return;

	if (rts->rts_gate == new->rts_gate
	    && rts->rts_router == new->rts_router
	    && rts->rts_metric == new->rts_metric
	    && rts->rts_tag == new->rts_tag
	    && rts->rts_de_ag == new->rts_de_ag)
		return;

	lastlog();
	if (new->rts_gate == 0) {
		(void)fprintf(ftrace, "Del #%d %-35s ",
			      (int)(rts - rt->rt_spares),
			      rtname(rt->rt_dst, rt->rt_mask, rts->rts_gate));
		print_rts(rts, 0,0,0,0,
			  (rts != rt->rt_spares
			   || AGE_RT(rt->rt_state,new->rts_ifp)));

	} else if (rts->rts_gate != RIP_DEFAULT) {
		(void)fprintf(ftrace, "Chg #%d %-35s ",
			      (int)(rts - rt->rt_spares),
			      rtname(rt->rt_dst, rt->rt_mask, rts->rts_gate));
		print_rts(rts, 0,0,
			  rts->rts_gate != new->rts_gate,
			  rts->rts_tag != new->rts_tag,
			  rts != rt->rt_spares || AGE_RT(rt->rt_state,
							rt->rt_ifp));

		(void)fprintf(ftrace, "\n       %19s%-16s ", "",
			      (new->rts_gate != rts->rts_gate
			       ? naddr_ntoa(new->rts_gate) : ""));
		print_rts(new,
			  -(new->rts_metric == rts->rts_metric),
			  -(new->rts_ifp == rts->rts_ifp),
			  0,
			  rts->rts_tag != new->rts_tag,
			  (new->rts_time != rts->rts_time
			   && (rts != rt->rt_spares
			       || AGE_RT(rt->rt_state, new->rts_ifp))));

	} else {
		(void)fprintf(ftrace, "Add #%d %-35s ",
			      (int)(rts - rt->rt_spares),
			      rtname(rt->rt_dst, rt->rt_mask, new->rts_gate));
		print_rts(new, 0,0,0,0,
			  (rts != rt->rt_spares
			   || AGE_RT(rt->rt_state,new->rts_ifp)));
	}
	(void)fputc('\n',ftrace);
}


/* miscellaneous message checked by the caller
 */
void
trace_misc(const char *p, ...)
{
	va_list args;

	if (ftrace == NULL)
		return;

	lastlog();
	va_start(args, p);
	vfprintf(ftrace, p, args);
	va_end(args);
	(void)fputc('\n',ftrace);
}


/* display a message if tracing actions
 */
void
trace_act(const char *p, ...)
{
	va_list args;

	if (!TRACEACTIONS || ftrace == NULL)
		return;

	lastlog();
	va_start(args, p);
	vfprintf(ftrace, p, args);
	va_end(args);
	(void)fputc('\n',ftrace);
}


/* display a message if tracing packets
 */
void
trace_pkt(const char *p, ...)
{
	va_list args;

	if (!TRACEPACKETS || ftrace == NULL)
		return;

	lastlog();
	va_start(args, p);
	vfprintf(ftrace, p, args);
	va_end(args);
	(void)fputc('\n',ftrace);
}


void
trace_change(struct rt_entry *rt,
	     u_int	state,
	     struct	rt_spare *new,
	     const char	*label)
{
	if (ftrace == NULL)
		return;

	if (rt->rt_metric == new->rts_metric
	    && rt->rt_gate == new->rts_gate
	    && rt->rt_router == new->rts_router
	    && rt->rt_state == state
	    && rt->rt_tag == new->rts_tag
	    && rt->rt_de_ag == new->rts_de_ag)
		return;

	lastlog();
	(void)fprintf(ftrace, "%s %-35s ",
		      label,
		      rtname(rt->rt_dst, rt->rt_mask, rt->rt_gate));
	print_rts(rt->rt_spares,
		  0,0,0,0, AGE_RT(rt->rt_state, rt->rt_ifp));
	trace_bits(rs_bits, rt->rt_state, rt->rt_state != state);

	(void)fprintf(ftrace, "\n%*s %19s%-16s ",
		      (int)strlen(label), "", "",
		      (rt->rt_gate != new->rts_gate
		       ? naddr_ntoa(new->rts_gate) : ""));
	print_rts(new,
		  -(new->rts_metric == rt->rt_metric),
		  -(new->rts_ifp == rt->rt_ifp),
		  0,
		  rt->rt_tag != new->rts_tag,
		  (rt->rt_time != new->rts_time
		   && AGE_RT(rt->rt_state,new->rts_ifp)));
	if (rt->rt_state != state)
		trace_bits(rs_bits, state, 1);
	(void)fputc('\n',ftrace);
}


void
trace_add_del(const char * action, struct rt_entry *rt)
{
	if (ftrace == NULL)
		return;

	lastlog();
	(void)fprintf(ftrace, "%s    %-35s ",
		      action,
		      rtname(rt->rt_dst, rt->rt_mask, rt->rt_gate));
	print_rts(rt->rt_spares, 0,0,0,0,AGE_RT(rt->rt_state,rt->rt_ifp));
	trace_bits(rs_bits, rt->rt_state, 0);
	(void)fputc('\n',ftrace);
}


/* ARGSUSED */
static int
walk_trace(struct radix_node *rn,
	   struct walkarg *w UNUSED)
{
#define RT ((struct rt_entry *)rn)
	struct rt_spare *rts;
	int i;

	(void)fprintf(ftrace, "  %-35s ",
		      rtname(RT->rt_dst, RT->rt_mask, RT->rt_gate));
	print_rts(&RT->rt_spares[0], 0,0,0,0, AGE_RT(RT->rt_state, RT->rt_ifp));
	trace_bits(rs_bits, RT->rt_state, 0);
	if (RT->rt_poison_time >= now_garbage
	    && RT->rt_poison_metric < RT->rt_metric)
		(void)fprintf(ftrace, "pm=%d@%s",
			      RT->rt_poison_metric, ts(RT->rt_poison_time));

	rts = &RT->rt_spares[1];
	for (i = 1; i < NUM_SPARES; i++, rts++) {
		if (rts->rts_gate != RIP_DEFAULT) {
			(void)fprintf(ftrace,"\n    #%d%15s%-16s ",
				      i, "", naddr_ntoa(rts->rts_gate));
			print_rts(rts, 0,0,0,0,1);
		}
	}
	(void)fputc('\n',ftrace);

	return 0;
}


static void
trace_dump(void)
{
	struct interface *ifp;

	if (ftrace == NULL)
		return;
	lastlog();

	(void)fputs("current daemon state:\n", ftrace);
	LIST_FOREACH(ifp, &ifnet, int_list) 
		trace_if("", ifp);
	(void)rn_walktree(rhead, walk_trace, 0);
}


void
trace_rip(const char *dir1, const char *dir2,
	  struct sockaddr_in *who,
	  struct interface *ifp,
	  struct rip *msg,
	  int size)			/* total size of message */
{
	struct netinfo *n, *lim;
#	define NA ((struct netauth*)n)
	int i, seen_route;

	if (!TRACEPACKETS || ftrace == NULL)
		return;

	lastlog();
	if (msg->rip_cmd >= RIPCMD_MAX
	    || msg->rip_vers == 0) {
		(void)fprintf(ftrace, "%s bad RIPv%d cmd=%d %s"
			      " %s.%d size=%d\n",
			      dir1, msg->rip_vers, msg->rip_cmd, dir2,
			      naddr_ntoa(who->sin_addr.s_addr),
			      ntohs(who->sin_port),
			      size);
		return;
	}

	(void)fprintf(ftrace, "%s RIPv%d %s %s %s.%d%s%s\n",
		      dir1, msg->rip_vers, ripcmds[msg->rip_cmd], dir2,
		      naddr_ntoa(who->sin_addr.s_addr), ntohs(who->sin_port),
		      ifp ? " via " : "", ifp ? ifp->int_name : "");
	if (!TRACECONTENTS)
		return;

	seen_route = 0;
	switch (msg->rip_cmd) {
	case RIPCMD_REQUEST:
	case RIPCMD_RESPONSE:
		n = msg->rip_nets;
		lim = (struct netinfo *)((char*)msg + size);
		for (; n < lim; n++) {
			if (!seen_route
			    && n->n_family == RIP_AF_UNSPEC
			    && ntohl(n->n_metric) == HOPCNT_INFINITY
			    && msg->rip_cmd == RIPCMD_REQUEST
			    && (n+1 == lim
				|| (n+2 == lim
				    && (n+1)->n_family == RIP_AF_AUTH))) {
				(void)fputs("\tQUERY ", ftrace);
				if (n->n_dst != 0)
					(void)fprintf(ftrace, "%s ",
						      naddr_ntoa(n->n_dst));
				if (n->n_mask != 0)
					(void)fprintf(ftrace, "mask=%#x ",
						      (u_int)ntohl(n->n_mask));
				if (n->n_nhop != 0)
					(void)fprintf(ftrace, "nhop=%s ",
						      naddr_ntoa(n->n_nhop));
				if (n->n_tag != 0)
					(void)fprintf(ftrace, "tag=%#x ",
						      ntohs(n->n_tag));
				(void)fputc('\n',ftrace);
				continue;
			}

			if (n->n_family == RIP_AF_AUTH) {
				if (NA->a_type == RIP_AUTH_PW
				    && n == msg->rip_nets) {
					(void)fprintf(ftrace, "\tPassword"
						      " Authentication:"
						      " \"%s\"\n",
						      qstring(NA->au.au_pw,
							  RIP_AUTH_PW_LEN));
					continue;
				}

				if (NA->a_type == RIP_AUTH_MD5
				    && n == msg->rip_nets) {
					(void)fprintf(ftrace,
						      "\tMD5 Auth"
						      " pkt_len=%d KeyID=%u"
						      " auth_len=%d"
						      " seqno=%#x"
						      " rsvd=%#x,%#x\n",
					    ntohs(NA->au.a_md5.md5_pkt_len),
					    NA->au.a_md5.md5_keyid,
					    NA->au.a_md5.md5_auth_len,
					    (int)ntohl(NA->au.a_md5.md5_seqno),
					    (int)ntohs(NA->au.a_md5.rsvd[0]),
					    (int)ntohs(NA->au.a_md5.rsvd[1]));
					continue;
				}
				(void)fprintf(ftrace,
					      "\tAuthentication type %d: ",
					      ntohs(NA->a_type));
				for (i = 0;
				     i < (int)sizeof(NA->au.au_pw);
				     i++)
					(void)fprintf(ftrace, "%02x ",
						      NA->au.au_pw[i]);
				(void)fputc('\n',ftrace);
				continue;
			}

			seen_route = 1;
			if (n->n_family != RIP_AF_INET) {
				(void)fprintf(ftrace,
					      "\t(af %d) %-18s mask=%#x ",
					      ntohs(n->n_family),
					      naddr_ntoa(n->n_dst),
					      (u_int)ntohl(n->n_mask));
			} else if (msg->rip_vers == RIPv1) {
				(void)fprintf(ftrace, "\t%-18s ",
					      addrname(n->n_dst,
						       ntohl(n->n_mask),
						       n->n_mask==0 ? 2 : 1));
			} else {
				(void)fprintf(ftrace, "\t%-18s ",
					      addrname(n->n_dst,
						       ntohl(n->n_mask),
						       n->n_mask==0 ? 2 : 0));
			}
			(void)fprintf(ftrace, "metric=%-2d ",
				      (u_int)ntohl(n->n_metric));
			if (n->n_nhop != 0)
				(void)fprintf(ftrace, " nhop=%s ",
					      naddr_ntoa(n->n_nhop));
			if (n->n_tag != 0)
				(void)fprintf(ftrace, "tag=%#x",
					      ntohs(n->n_tag));
			(void)fputc('\n',ftrace);
		}
		if (size != (char *)n - (char *)msg)
			(void)fprintf(ftrace, "truncated record, len %d\n",
				size);
		break;

	case RIPCMD_TRACEON:
		fprintf(ftrace, "\tfile=\"%.*s\"\n", size-4,
			msg->rip_tracefile);
		break;

	case RIPCMD_TRACEOFF:
		break;
	}
}
