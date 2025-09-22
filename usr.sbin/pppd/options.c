/*	$OpenBSD: options.c,v 1.33 2024/08/21 14:57:05 florian Exp $	*/

/*
 * options.c - handles option processing for PPP.
 *
 * Copyright (c) 1984-2000 Carnegie Mellon University. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any legal
 *    details, please contact
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <termios.h>
#include <syslog.h>
#include <string.h>
#include <netdb.h>
#include <pwd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef PPP_FILTER
#include <pcap.h>
#include <pcap-int.h>	/* XXX: To get struct pcap */
#endif

#include "pppd.h"
#include "pathnames.h"
#include "patchlevel.h"
#include "fsm.h"
#include "lcp.h"
#include "ipcp.h"
#include "upap.h"
#include "chap.h"
#include "ccp.h"
#ifdef CBCP_SUPPORT
#include "cbcp.h"
#endif

#include <net/ppp-comp.h>

#define FALSE	0
#define TRUE	1

#if defined(ultrix) || defined(NeXT)
char *strdup(char *);
#endif

#ifndef GIDSET_TYPE
#define GIDSET_TYPE	gid_t
#endif

/*
 * Option variables and default values.
 */
#ifdef PPP_FILTER
int	dflag = 0;		/* Tell libpcap we want debugging */
#endif
int	debug = 0;		/* Debug flag */
int	kdebugflag = 0;		/* Tell kernel to print debug messages */
int	default_device = 1;	/* Using /dev/tty or equivalent */
char	devnam[PATH_MAX] = "/dev/tty";	/* Device name */
int	crtscts = 0;		/* Use hardware flow control */
int	modem = 1;		/* Use modem control lines */
int	modem_chat = 0;		/* Use modem control lines during chat */
int	inspeed = 0;		/* Input/Output speed requested */
u_int32_t netmask = 0;		/* IP netmask to set on interface */
int	lockflag = 0;		/* Create lock file to lock the serial dev */
int	nodetach = 0;		/* Don't detach from controlling tty */
char	*connector = NULL;	/* Script to establish physical link */
char	*disconnector = NULL;	/* Script to disestablish physical link */
char	*welcomer = NULL;	/* Script to run after phys link estab. */
int	maxconnect = 0;		/* Maximum connect time */
char	user[MAXNAMELEN];	/* Username for PAP */
char	passwd[MAXSECRETLEN];	/* Password for PAP */
int	auth_required = 0;	/* Peer is required to authenticate */
volatile sig_atomic_t persist = 0; /* Reopen link after it goes down */
int	uselogin = 0;		/* Use /etc/passwd for checking PAP */
int	lcp_echo_interval = 0; 	/* Interval between LCP echo-requests */
int	lcp_echo_fails = 0;	/* Tolerance to unanswered echo-requests */
char	our_name[MAXNAMELEN];	/* Our name for authentication purposes */
char	remote_name[MAXNAMELEN]; /* Peer's name for authentication */
int	explicit_remote = 0;	/* User specified explicit remote name */
int	usehostname = 0;	/* Use hostname for our_name */
int	disable_defaultip = 0;	/* Don't use hostname for default IP adrs */
int	demand = 0;		/* do dial-on-demand */
char	*ipparam = NULL;	/* Extra parameter for ip up/down scripts */
int	cryptpap;		/* Passwords in pap-secrets are encrypted */
int	idle_time_limit = 0;	/* Disconnect if idle for this many seconds */
int	holdoff = 30;		/* # seconds to pause before reconnecting */
int	refuse_pap = 0;		/* Set to say we won't do PAP */
int	refuse_chap = 0;	/* Set to say we won't do CHAP */

#ifdef MSLANMAN
int	ms_lanman = 0;    	/* Nonzero if use LanMan password instead of NT */
			  	/* Has meaning only with MS-CHAP challenges */
#endif

struct option_info auth_req_info;
struct option_info connector_info;
struct option_info disconnector_info;
struct option_info welcomer_info;
struct option_info devnam_info;
#ifdef PPP_FILTER
struct	bpf_program pass_filter;/* Filter program for packets to pass */
struct	bpf_program active_filter; /* Filter program for link-active pkts */
pcap_t  pc;			/* Fake struct pcap so we can compile expr */
#endif

/*
 * Prototypes
 */
static int setdevname(char *, int);
static int setipaddr(char *);
static int setspeed(char *);
static int setdebug(char **);
static int setkdebug(char **);
static int setpassive(char **);
static int setsilent(char **);
static int noopt(char **);
static int setnovj(char **);
static int setnovjccomp(char **);
static int setvjslots(char **);
static int reqpap(char **);
static int nopap(char **);
static int nochap(char **);
static int reqchap(char **);
static int noaccomp(char **);
static int noasyncmap(char **);
static int noip(char **);
static int nomagicnumber(char **);
static int setasyncmap(char **);
static int setescape(char **);
static int setmru(char **);
static int setmtu(char **);
#ifdef CBCP_SUPPORT
static int setcbcp(char **);
#endif
static int nomru(char **);
static int nopcomp(char **);
static int setconnector(char **);
static int setdisconnector(char **);
static int setwelcomer(char **);
static int setmaxconnect(char **);
static int setdomain(char **);
static int setnetmask(char **);
static int setcrtscts(char **);
static int setnocrtscts(char **);
static int setxonxoff(char **);
static int setnodetach(char **);
static int setupdetach(char **);
static int setmodem(char **);
static int setmodem_chat(char **);
static int setlocal(char **);
static int setlock(char **);
static int setname(char **);
static int setuser(char **);
static int setremote(char **);
static int setauth(char **);
static int setnoauth(char **);
static int readfile(char **);
static int callfile(char **);
static int setdefaultroute(char **);
static int setnodefaultroute(char **);
static int setproxyarp(char **);
static int setnoproxyarp(char **);
static int setpersist(char **);
static int setnopersist(char **);
static int setdologin(char **);
static int setusehostname(char **);
static int setnoipdflt(char **);
static int setlcptimeout(char **);
static int setlcpterm(char **);
static int setlcpconf(char **);
static int setlcpfails(char **);
static int setipcptimeout(char **);
static int setipcpterm(char **);
static int setipcpconf(char **);
static int setipcpfails(char **);
static int setpaptimeout(char **);
static int setpapreqs(char **);
static int setpapreqtime(char **);
static int setchaptimeout(char **);
static int setchapchal(char **);
static int setchapintv(char **);
static int setipcpaccl(char **);
static int setipcpaccr(char **);
static int setlcpechointv(char **);
static int setlcpechofails(char **);
static int noccp(char **);
static int setbsdcomp(char **);
static int setnobsdcomp(char **);
static int setdeflate(char **);
static int setnodeflate(char **);
static int setnodeflatedraft(char **);
static int setdemand(char **);
static int setpred1comp(char **);
static int setnopred1comp(char **);
static int setipparam(char **);
static int setpapcrypt(char **);
static int setidle(char **);
static int setholdoff(char **);
static int setdnsaddr(char **);
static int setwinsaddr(char **);
static int showversion(char **);
static int showhelp(char **);

#ifdef PPP_FILTER
static int setpdebug(char **);
static int setpassfilter(char **);
static int setactivefilter(char **);
#endif

#ifdef MSLANMAN
static int setmslanman(char **);
#endif

static int number_option(char *, u_int32_t *, int);
static int int_option(char *, int *);
static int readable(int fd);

/*
 * Valid arguments.
 */
static struct cmd {
    char *cmd_name;
    int num_args;
    int (*cmd_func)(char **);
} cmds[] = {
    {"-all", 0, noopt},		/* Don't request/allow any options (useless) */
    {"noaccomp", 0, noaccomp},	/* Disable Address/Control compression */
    {"-ac", 0, noaccomp},	/* Disable Address/Control compress */
    {"default-asyncmap", 0, noasyncmap}, /* Disable asyncmap negotiation */
    {"-am", 0, noasyncmap},	/* Disable asyncmap negotiation */
    {"-as", 1, setasyncmap},	/* set the desired async map */
    {"-d", 0, setdebug},	/* Increase debugging level */
    {"nodetach", 0, setnodetach}, /* Don't detach from controlling tty */
    {"-detach", 0, setnodetach}, /* don't fork */
    {"updetach", 0, setupdetach}, /* Detach once an NP has come up */
    {"noip", 0, noip},		/* Disable IP and IPCP */
    {"-ip", 0, noip},		/* Disable IP and IPCP */
    {"nomagic", 0, nomagicnumber}, /* Disable magic number negotiation */
    {"-mn", 0, nomagicnumber},	/* Disable magic number negotiation */
    {"default-mru", 0, nomru},	/* Disable MRU negotiation */
    {"-mru", 0, nomru},		/* Disable mru negotiation */
    {"-p", 0, setpassive},	/* Set passive mode */
    {"nopcomp", 0, nopcomp},	/* Disable protocol field compression */
    {"-pc", 0, nopcomp},	/* Disable protocol field compress */
    {"require-pap", 0, reqpap},	/* Require PAP authentication from peer */
    {"+pap", 0, reqpap},	/* Require PAP auth from peer */
    {"refuse-pap", 0, nopap},	/* Don't agree to auth to peer with PAP */
    {"-pap", 0, nopap},		/* Don't allow UPAP authentication with peer */
    {"require-chap", 0, reqchap}, /* Require CHAP authentication from peer */
    {"+chap", 0, reqchap},	/* Require CHAP authentication from peer */
    {"refuse-chap", 0, nochap},	/* Don't agree to auth to peer with CHAP */
    {"-chap", 0, nochap},	/* Don't allow CHAP authentication with peer */
    {"novj", 0, setnovj},	/* Disable VJ compression */
    {"-vj", 0, setnovj},	/* disable VJ compression */
    {"novjccomp", 0, setnovjccomp}, /* disable VJ connection-ID compression */
    {"-vjccomp", 0, setnovjccomp}, /* disable VJ connection-ID compression */
    {"vj-max-slots", 1, setvjslots}, /* Set maximum VJ header slots */
    {"asyncmap", 1, setasyncmap}, /* set the desired async map */
    {"escape", 1, setescape},	/* set chars to escape on transmission */
    {"connect", 1, setconnector}, /* A program to set up a connection */
    {"disconnect", 1, setdisconnector},	/* program to disconnect serial dev. */
    {"welcome", 1, setwelcomer},/* Script to welcome client */
    {"maxconnect", 1, setmaxconnect},  /* specify a maximum connect time */
    {"crtscts", 0, setcrtscts},	/* set h/w flow control */
    {"nocrtscts", 0, setnocrtscts}, /* clear h/w flow control */
    {"-crtscts", 0, setnocrtscts}, /* clear h/w flow control */
    {"xonxoff", 0, setxonxoff},	/* set s/w flow control */
    {"debug", 0, setdebug},	/* Increase debugging level */
    {"kdebug", 1, setkdebug},	/* Enable kernel-level debugging */
    {"domain", 1, setdomain},	/* Add given domain name to hostname*/
    {"mru", 1, setmru},		/* Set MRU value for negotiation */
    {"mtu", 1, setmtu},		/* Set our MTU */
#ifdef CBCP_SUPPORT
    {"callback", 1, setcbcp},	/* Ask for callback */
#endif
    {"netmask", 1, setnetmask},	/* set netmask */
    {"passive", 0, setpassive},	/* Set passive mode */
    {"silent", 0, setsilent},	/* Set silent mode */
    {"modem", 0, setmodem},	/* Use modem control lines */
    {"modem_chat", 0, setmodem_chat}, /* Use modem control lines during chat */
    {"local", 0, setlocal},	/* Don't use modem control lines */
    {"lock", 0, setlock},	/* Lock serial device (with lock file) */
    {"name", 1, setname},	/* Set local name for authentication */
    {"user", 1, setuser},	/* Set name for auth with peer */
    {"usehostname", 0, setusehostname},	/* Must use hostname for auth. */
    {"remotename", 1, setremote}, /* Set remote name for authentication */
    {"auth", 0, setauth},	/* Require authentication from peer */
    {"noauth", 0, setnoauth},	/* Don't require peer to authenticate */
    {"file", 1, readfile},	/* Take options from a file */
    {"call", 1, callfile},	/* Take options from a privileged file */
    {"defaultroute", 0, setdefaultroute}, /* Add default route */
    {"nodefaultroute", 0, setnodefaultroute}, /* disable defaultroute option */
    {"-defaultroute", 0, setnodefaultroute}, /* disable defaultroute option */
    {"proxyarp", 0, setproxyarp}, /* Add proxy ARP entry */
    {"noproxyarp", 0, setnoproxyarp}, /* disable proxyarp option */
    {"-proxyarp", 0, setnoproxyarp}, /* disable proxyarp option */
    {"persist", 0, setpersist},	/* Keep on reopening connection after close */
    {"nopersist", 0, setnopersist},  /* Turn off persist option */
    {"demand", 0, setdemand},	/* Dial on demand */
    {"login", 0, setdologin},	/* Use system password database for UPAP */
    {"noipdefault", 0, setnoipdflt}, /* Don't use name for default IP adrs */
    {"lcp-echo-failure", 1, setlcpechofails}, /* consecutive echo failures */
    {"lcp-echo-interval", 1, setlcpechointv}, /* time for lcp echo events */
    {"lcp-restart", 1, setlcptimeout}, /* Set timeout for LCP */
    {"lcp-max-terminate", 1, setlcpterm}, /* Set max #xmits for term-reqs */
    {"lcp-max-configure", 1, setlcpconf}, /* Set max #xmits for conf-reqs */
    {"lcp-max-failure", 1, setlcpfails}, /* Set max #conf-naks for LCP */
    {"ipcp-restart", 1, setipcptimeout}, /* Set timeout for IPCP */
    {"ipcp-max-terminate", 1, setipcpterm}, /* Set max #xmits for term-reqs */
    {"ipcp-max-configure", 1, setipcpconf}, /* Set max #xmits for conf-reqs */
    {"ipcp-max-failure", 1, setipcpfails}, /* Set max #conf-naks for IPCP */
    {"pap-restart", 1, setpaptimeout},	/* Set retransmit timeout for PAP */
    {"pap-max-authreq", 1, setpapreqs}, /* Set max #xmits for auth-reqs */
    {"pap-timeout", 1, setpapreqtime},	/* Set time limit for peer PAP auth. */
    {"chap-restart", 1, setchaptimeout}, /* Set timeout for CHAP */
    {"chap-max-challenge", 1, setchapchal}, /* Set max #xmits for challenge */
    {"chap-interval", 1, setchapintv}, /* Set interval for rechallenge */
    {"ipcp-accept-local", 0, setipcpaccl}, /* Accept peer's address for us */
    {"ipcp-accept-remote", 0, setipcpaccr}, /* Accept peer's address for it */
    {"noccp", 0, noccp},		/* Disable CCP negotiation */
    {"-ccp", 0, noccp},			/* Disable CCP negotiation */
    {"bsdcomp", 1, setbsdcomp},		/* request BSD-Compress */
    {"nobsdcomp", 0, setnobsdcomp},	/* don't allow BSD-Compress */
    {"-bsdcomp", 0, setnobsdcomp},	/* don't allow BSD-Compress */
    {"deflate", 1, setdeflate},		/* request Deflate compression */
    {"nodeflate", 0, setnodeflate},	/* don't allow Deflate compression */
    {"-deflate", 0, setnodeflate},	/* don't allow Deflate compression */
    {"nodeflatedraft", 0, setnodeflatedraft}, /* don't use draft deflate # */
    {"predictor1", 0, setpred1comp},	/* request Predictor-1 */
    {"nopredictor1", 0, setnopred1comp},/* don't allow Predictor-1 */
    {"-predictor1", 0, setnopred1comp},	/* don't allow Predictor-1 */
    {"ipparam", 1, setipparam},		/* set ip script parameter */
    {"papcrypt", 0, setpapcrypt},	/* PAP passwords encrypted */
    {"idle", 1, setidle},		/* idle time limit (seconds) */
    {"holdoff", 1, setholdoff},		/* set holdoff time (seconds) */
    {"ms-dns", 1, setdnsaddr},		/* DNS address for the peer's use */
    {"ms-wins", 1, setwinsaddr},	/* Nameserver for SMB over TCP/IP for peer */
    {"--version", 0, showversion},	/* Show version number */
    {"--help", 0, showhelp},		/* Show brief listing of options */
    {"-h", 0, showhelp},		/* ditto */

#ifdef PPP_FILTER
    {"pdebug", 1, setpdebug},		/* libpcap debugging */
    {"pass-filter", 1, setpassfilter},	/* set filter for packets to pass */
    {"active-filter", 1, setactivefilter}, /* set filter for active pkts */
#endif

#ifdef MSLANMAN
    {"ms-lanman", 0, setmslanman},	/* Use LanMan psswd when using MS-CHAP */
#endif

    {NULL, 0, NULL}
};


#ifndef IMPLEMENTATION
#define IMPLEMENTATION ""
#endif

static const char usage_string[] = "\
pppd version %s patch level %d%s\n\
Usage: %s [ options ], where options are:\n\
	<device>	Communicate over the named device\n\
	<speed>		Set the baud rate to <speed>\n\
	<loc>:<rem>	Set the local and/or remote interface IP\n\
			addresses.  Either one may be omitted.\n\
	asyncmap <n>	Set the desired async map to hex <n>\n\
	auth		Require authentication from peer\n\
        connect <p>     Invoke shell command <p> to set up the serial line\n\
	crtscts		Use hardware RTS/CTS flow control\n\
	defaultroute	Add default route through interface\n\
	file <f>	Take options from file <f>\n\
	modem		Use modem control lines\n\
	modem_chat	Use modem control lines during chat\n\
	mru <n>		Set MRU value to <n> for negotiation\n\
	netmask <n>	Set interface netmask to <n>\n\
See pppd(8) for more options.\n\
";

static char *current_option;	/* the name of the option being parsed */
static int privileged_option;	/* set iff the current option came from root */
static char *option_source;	/* string saying where the option came from */

/*
 * parse_args - parse a string of arguments from the command line.
 */
int
parse_args(int argc, char **argv)
{
    char *arg;
    struct cmd *cmdp;
    int ret;

    privileged_option = privileged;
    option_source = "command line";
    while (argc > 0) {
	arg = *argv++;
	--argc;

	/*
	 * First see if it's a command.
	 */
	for (cmdp = cmds; cmdp->cmd_name; cmdp++)
	    if (!strcmp(arg, cmdp->cmd_name))
		break;

	if (cmdp->cmd_name != NULL) {
	    if (argc < cmdp->num_args) {
		option_error("too few parameters for option %s", arg);
		return 0;
	    }
	    current_option = arg;
	    if (!(*cmdp->cmd_func)(argv))
		return 0;
	    argc -= cmdp->num_args;
	    argv += cmdp->num_args;

	} else {
	    /*
	     * Maybe a tty name, speed or IP address?
	     */
	    if ((ret = setdevname(arg, 0)) == 0
		&& (ret = setspeed(arg)) == 0
		&& (ret = setipaddr(arg)) == 0) {
		option_error("unrecognized option '%s'", arg);
		usage();
		return 0;
	    }
	    if (ret < 0)	/* error */
		return 0;
	}
    }
    return 1;
}

/*
 * scan_args - scan the command line arguments to get the tty name,
 * if specified.
 */
void
scan_args(int argc, char **argv)
{
    char *arg;
    struct cmd *cmdp;

    while (argc > 0) {
	arg = *argv++;
	--argc;

	/* Skip options and their arguments */
	for (cmdp = cmds; cmdp->cmd_name; cmdp++)
	    if (!strcmp(arg, cmdp->cmd_name))
		break;

	if (cmdp->cmd_name != NULL) {
	    argc -= cmdp->num_args;
	    argv += cmdp->num_args;
	    continue;
	}

	/* Check if it's a tty name and copy it if so */
	(void) setdevname(arg, 1);
    }
}

/*
 * usage - print out a message telling how to use the program.
 */
void
usage(void)
{
    if (phase == PHASE_INITIALIZE)
	fprintf(stderr, usage_string, VERSION, PATCHLEVEL, IMPLEMENTATION,
		__progname);
}

/*
 * showhelp - print out usage message and exit.
 */
static int
showhelp(char **argv)
{
    if (phase == PHASE_INITIALIZE) {
	usage();
	exit(0);
    }
    return 0;
}

/*
 * showversion - print out the version number and exit.
 */
static int
showversion(char **argv)
{
    if (phase == PHASE_INITIALIZE) {
	fprintf(stderr, "pppd version %s patch level %d%s\n",
		VERSION, PATCHLEVEL, IMPLEMENTATION);
	exit(0);
    }
    return 0;
}

/*
 * options_from_file - Read a string of options from a file,
 * and interpret them.
 */
int
options_from_file(char *filename, int must_exist, int check_prot, int priv)
{
    FILE *f;
    int i, newline, ret;
    struct cmd *cmdp;
    int oldpriv;
    char *argv[MAXARGS];
    char args[MAXARGS][MAXWORDLEN];
    char cmd[MAXWORDLEN];

    if ((f = fopen(filename, "r")) == NULL) {
	if (!must_exist && errno == ENOENT)
	    return 1;
	option_error("Can't open options file %s: %m", filename);
	return 0;
    }
    if (check_prot && !readable(fileno(f))) {
	option_error("Can't open options file %s: access denied", filename);
	fclose(f);
	return 0;
    }

    oldpriv = privileged_option;
    privileged_option = priv;
    ret = 0;
    while (getword(f, cmd, &newline, filename)) {
	/*
	 * First see if it's a command.
	 */
	for (cmdp = cmds; cmdp->cmd_name; cmdp++)
	    if (!strcmp(cmd, cmdp->cmd_name))
		break;

	if (cmdp->cmd_name != NULL) {
	    for (i = 0; i < cmdp->num_args; ++i) {
		if (!getword(f, args[i], &newline, filename)) {
		    option_error(
			"In file %s: too few parameters for option '%s'",
			filename, cmd);
		    goto err;
		}
		argv[i] = args[i];
	    }
	    current_option = cmd;
	    if (!(*cmdp->cmd_func)(argv))
		goto err;

	} else {
	    /*
	     * Maybe a tty name, speed or IP address?
	     */
	    if ((i = setdevname(cmd, 0)) == 0
		&& (i = setspeed(cmd)) == 0
		&& (i = setipaddr(cmd)) == 0) {
		option_error("In file %s: unrecognized option '%s'",
			     filename, cmd);
		goto err;
	    }
	    if (i < 0)		/* error */
		goto err;
	}
    }
    ret = 1;

err:
    fclose(f);
    privileged_option = oldpriv;
    return ret;
}

/*
 * options_from_user - See if the use has a ~/.ppprc file,
 * and if so, interpret options from it.
 */
int
options_from_user(void)
{
    char *user, *path, *file;
    int ret;
    struct passwd *pw;

    pw = getpwuid(getuid());
    if (pw == NULL || (user = pw->pw_dir) == NULL || user[0] == 0)
	return 1;
    file = _PATH_USEROPT;
    if (asprintf(&path, "%s/%s", user, file) == -1)
	novm("init file name");
    ret = options_from_file(path, 0, 1, privileged);
    free(path);
    return ret;
}

/*
 * options_for_tty - See if an options file exists for the serial
 * device, and if so, interpret options from it.
 */
int
options_for_tty(void)
{
    char *dev, *path;
    int ret;

    dev = devnam;
    if (strncmp(dev, "/dev/", 5) == 0)
	dev += 5;
    if (strcmp(dev, "tty") == 0)
	return 1;		/* don't look for /etc/ppp/options.tty */
    if (asprintf(&path, "%s%s", _PATH_TTYOPT, dev) == -1)
	novm("tty init file name");
    ret = options_from_file(path, 0, 0, 1);
    free(path);
    return ret;
}

/*
 * option_error - print a message about an error in an option.
 * The message is logged, and also sent to
 * stderr if phase == PHASE_INITIALIZE.
 */
void
option_error(char *fmt, ...)
{
    va_list args;
    char buf[256];

    va_start(args, fmt);
    vfmtmsg(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (phase == PHASE_INITIALIZE)
	fprintf(stderr, "%s: %s\n", __progname, buf);
    syslog(LOG_ERR, "%s", buf);
}

/*
 * readable - check if a file is readable by the real user.
 */
static int
readable(int fd)
{
    uid_t uid;
    int ngroups, i;
    struct stat sbuf;
    GIDSET_TYPE groups[NGROUPS_MAX];

    uid = getuid();
    if (uid == 0)
	return 1;
    if (fstat(fd, &sbuf) != 0)
	return 0;
    if (sbuf.st_uid == uid)
	return sbuf.st_mode & S_IRUSR;
    if (sbuf.st_gid == getgid())
	return sbuf.st_mode & S_IRGRP;
    ngroups = getgroups(NGROUPS_MAX, groups);
    for (i = 0; i < ngroups; ++i)
	if (sbuf.st_gid == groups[i])
	    return sbuf.st_mode & S_IRGRP;
    return sbuf.st_mode & S_IROTH;
}

/*
 * Read a word from a file.
 * Words are delimited by white-space or by quotes (" or ').
 * Quotes, white-space and \ may be escaped with \.
 * \<newline> is ignored.
 */
int
getword(FILE *f, char *word, int *newlinep, char *filename)
{
    int c, len, escape;
    int quoted, comment;
    int value, digit, got, n;

#define isoctal(c) ((c) >= '0' && (c) < '8')

    *newlinep = 0;
    len = 0;
    escape = 0;
    comment = 0;

    /*
     * First skip white-space and comments.
     */
    for (;;) {
	c = getc(f);
	if (c == EOF)
	    break;

	/*
	 * A newline means the end of a comment; backslash-newline
	 * is ignored.  Note that we cannot have escape && comment.
	 */
	if (c == '\n') {
	    if (!escape) {
		*newlinep = 1;
		comment = 0;
	    } else
		escape = 0;
	    continue;
	}

	/*
	 * Ignore characters other than newline in a comment.
	 */
	if (comment)
	    continue;

	/*
	 * If this character is escaped, we have a word start.
	 */
	if (escape)
	    break;

	/*
	 * If this is the escape character, look at the next character.
	 */
	if (c == '\\') {
	    escape = 1;
	    continue;
	}

	/*
	 * If this is the start of a comment, ignore the rest of the line.
	 */
	if (c == '#') {
	    comment = 1;
	    continue;
	}

	/*
	 * A non-whitespace character is the start of a word.
	 */
	if (!isspace(c))
	    break;
    }

    /*
     * Save the delimiter for quoted strings.
     */
    if (!escape && (c == '"' || c == '\'')) {
        quoted = c;
	c = getc(f);
    } else
        quoted = 0;

    /*
     * Process characters until the end of the word.
     */
    while (c != EOF) {
	if (escape) {
	    /*
	     * This character is escaped: backslash-newline is ignored,
	     * various other characters indicate particular values
	     * as for C backslash-escapes.
	     */
	    escape = 0;
	    if (c == '\n') {
	        c = getc(f);
		continue;
	    }

	    got = 0;
	    switch (c) {
	    case 'a':
		value = '\a';
		break;
	    case 'b':
		value = '\b';
		break;
	    case 'f':
		value = '\f';
		break;
	    case 'n':
		value = '\n';
		break;
	    case 'r':
		value = '\r';
		break;
	    case 's':
		value = ' ';
		break;
	    case 't':
		value = '\t';
		break;

	    default:
		if (isoctal(c)) {
		    /*
		     * \ddd octal sequence
		     */
		    value = 0;
		    for (n = 0; n < 3 && isoctal(c); ++n) {
			value = (value << 3) + (c & 07);
			c = getc(f);
		    }
		    got = 1;
		    break;
		}

		if (c == 'x') {
		    /*
		     * \x<hex_string> sequence
		     */
		    value = 0;
		    c = getc(f);
		    for (n = 0; n < 2 && isxdigit(c); ++n) {
			digit = toupper(c) - '0';
			if (digit > 10)
			    digit += '0' + 10 - 'A';
			value = (value << 4) + digit;
			c = getc (f);
		    }
		    got = 1;
		    break;
		}

		/*
		 * Otherwise the character stands for itself.
		 */
		value = c;
		break;
	    }

	    /*
	     * Store the resulting character for the escape sequence.
	     */
	    if (len < MAXWORDLEN) {
		word[len] = value;
		++len;
	    }

	    if (!got)
		c = getc(f);
	    continue;

	}

	/*
	 * Not escaped: see if we've reached the end of the word.
	 */
	if (quoted) {
	    if (c == quoted)
		break;
	} else {
	    if (isspace(c) || c == '#') {
		ungetc (c, f);
		break;
	    }
	}

	/*
	 * Backslash starts an escape sequence.
	 */
	if (c == '\\') {
	    escape = 1;
	    c = getc(f);
	    continue;
	}

	/*
	 * An ordinary character: store it in the word and get another.
	 */
	if (len < MAXWORDLEN) {
	    word[len] = c;
	    ++len;
	}

	c = getc(f);
    }

    /*
     * End of the word: check for errors.
     */
    if (c == EOF) {
	if (ferror(f)) {
	    if (errno == 0)
		errno = EIO;
	    option_error("Error reading %s: %m", filename);
	    die(1);
	}
	/*
	 * If len is zero, then we didn't find a word before the
	 * end of the file.
	 */
	if (len == 0)
	    return 0;
    }

    /*
     * Warn if the word was too long, and append a terminating null.
     */
    if (len >= MAXWORDLEN) {
	option_error("warning: word in file %s too long (%.20s...)",
		     filename, word);
	len = MAXWORDLEN - 1;
    }
    word[len] = 0;

    return 1;

#undef isoctal

}

/*
 * number_option - parse an unsigned numeric parameter for an option.
 */
static int
number_option(char *str, u_int32_t *valp, int base)
{
    char *ptr;

    *valp = strtoul(str, &ptr, base);
    if (ptr == str) {
	option_error("invalid numeric parameter '%s' for %s option",
		     str, current_option);
	return 0;
    }
    return 1;
}


/*
 * int_option - like number_option, but valp is int *,
 * the base is assumed to be 0, and *valp is not changed
 * if there is an error.
 */
static int
int_option(char *str, int *valp)
{
    u_int32_t v;

    if (!number_option(str, &v, 0))
	return 0;
    *valp = (int) v;
    return 1;
}


/*
 * The following procedures parse options.
 */

/*
 * readfile - take commands from a file.
 */
static int
readfile(char **argv)
{
    return options_from_file(*argv, 1, 1, privileged_option);
}

/*
 * callfile - take commands from /etc/ppp/peers/<name>.
 * Name may not contain /../, start with / or ../, or end in /..
 */
static int
callfile(char **argv)
{
    char *fname, *arg, *p;
    int l, ok;

    arg = *argv;
    ok = 1;
    if (arg[0] == '/' || arg[0] == 0)
	ok = 0;
    else {
	for (p = arg; *p != 0; ) {
	    if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == 0)) {
		ok = 0;
		break;
	    }
	    while (*p != '/' && *p != 0)
		++p;
	    if (*p == '/')
		++p;
	}
    }
    if (!ok) {
	option_error("call option value may not contain .. or start with /");
	return 0;
    }

    l = strlen(arg) + strlen(_PATH_PEERFILES) + 1;
    if ((fname = (char *) malloc(l)) == NULL)
	novm("call file name");
    strlcpy(fname, _PATH_PEERFILES, l);
    strlcat(fname, arg, l);

    ok = options_from_file(fname, 1, 1, 1);

    free(fname);
    return ok;
}


/*
 * setdebug - Set debug (command line argument).
 */
static int
setdebug(char **argv)
{
    debug++;
    return (1);
}

/*
 * setkdebug - Set kernel debugging level.
 */
static int
setkdebug(char **argv)
{
    return int_option(*argv, &kdebugflag);
}

#ifdef PPP_FILTER
/*
 * setpdebug - Set libpcap debugging level.
 */
static int
setpdebug(char **argv)
{
    return int_option(*argv, &dflag);
}

/*
 * setpassfilter - Set the pass filter for packets
 */
static int
setpassfilter(char **argv)
{
    pc.linktype = DLT_PPP;
    pc.snapshot = PPP_HDRLEN;
 
    if (pcap_compile(&pc, &pass_filter, *argv, 1, netmask) == 0)
	return 1;
    option_error("error in pass-filter expression: %s\n", pcap_geterr(&pc));
    return 0;
}

/*
 * setactivefilter - Set the active filter for packets
 */
static int
setactivefilter(char **argv)
{
    pc.linktype = DLT_PPP;
    pc.snapshot = PPP_HDRLEN;
 
    if (pcap_compile(&pc, &active_filter, *argv, 1, netmask) == 0)
	return 1;
    option_error("error in active-filter expression: %s\n", pcap_geterr(&pc));
    return 0;
}
#endif

/*
 * noopt - Disable all options.
 */
static int
noopt(char **argv)
{
    BZERO((char *) &lcp_wantoptions[0], sizeof (struct lcp_options));
    BZERO((char *) &lcp_allowoptions[0], sizeof (struct lcp_options));
    BZERO((char *) &ipcp_wantoptions[0], sizeof (struct ipcp_options));
    BZERO((char *) &ipcp_allowoptions[0], sizeof (struct ipcp_options));

    return (1);
}

/*
 * noaccomp - Disable Address/Control field compression negotiation.
 */
static int
noaccomp(char **argv)
{
    lcp_wantoptions[0].neg_accompression = 0;
    lcp_allowoptions[0].neg_accompression = 0;
    return (1);
}


/*
 * noasyncmap - Disable async map negotiation.
 */
static int
noasyncmap(char **argv)
{
    lcp_wantoptions[0].neg_asyncmap = 0;
    lcp_allowoptions[0].neg_asyncmap = 0;
    return (1);
}


/*
 * noip - Disable IP and IPCP.
 */
static int
noip(char **argv)
{
    ipcp_protent.enabled_flag = 0;
    return (1);
}


/*
 * nomagicnumber - Disable magic number negotiation.
 */
static int
nomagicnumber(char **argv)
{
    lcp_wantoptions[0].neg_magicnumber = 0;
    lcp_allowoptions[0].neg_magicnumber = 0;
    return (1);
}


/*
 * nomru - Disable mru negotiation.
 */
static int
nomru(char **argv)
{
    lcp_wantoptions[0].neg_mru = 0;
    lcp_allowoptions[0].neg_mru = 0;
    return (1);
}


/*
 * setmru - Set MRU for negotiation.
 */
static int
setmru(char **argv)
{
    u_int32_t mru;

    if (!number_option(*argv, &mru, 0))
	return 0;
    lcp_wantoptions[0].mru = mru;
    lcp_wantoptions[0].neg_mru = 1;
    return (1);
}


/*
 * setmru - Set the largest MTU we'll use.
 */
static int
setmtu(char **argv)
{
    u_int32_t mtu;

    if (!number_option(*argv, &mtu, 0))
	return 0;
    if (mtu < MINMRU || mtu > MAXMRU) {
	option_error("mtu option value of %u is too %s", mtu,
		     (mtu < MINMRU? "small": "large"));
	return 0;
    }
    lcp_allowoptions[0].mru = mtu;
    return (1);
}

#ifdef CBCP_SUPPORT
static int
setcbcp(argv)
    char **argv;
{
    lcp_wantoptions[0].neg_cbcp = 1;
    cbcp_protent.enabled_flag = 1;
    cbcp[0].us_number = strdup(*argv);
    if (cbcp[0].us_number == 0)
	novm("callback number");
    cbcp[0].us_type |= (1 << CB_CONF_USER);
    cbcp[0].us_type |= (1 << CB_CONF_ADMIN);
    return (1);
}
#endif

/*
 * nopcomp - Disable Protocol field compression negotiation.
 */
static int
nopcomp(char **argv)
{
    lcp_wantoptions[0].neg_pcompression = 0;
    lcp_allowoptions[0].neg_pcompression = 0;
    return (1);
}


/*
 * setpassive - Set passive mode (don't give up if we time out sending
 * LCP configure-requests).
 */
static int
setpassive(char **argv)
{
    lcp_wantoptions[0].passive = 1;
    return (1);
}


/*
 * setsilent - Set silent mode (don't start sending LCP configure-requests
 * until we get one from the peer).
 */
static int
setsilent(char **argv)
{
    lcp_wantoptions[0].silent = 1;
    return 1;
}


/*
 * nopap - Disable PAP authentication with peer.
 */
static int
nopap(char **argv)
{
    refuse_pap = 1;
    return (1);
}


/*
 * reqpap - Require PAP authentication from peer.
 */
static int
reqpap(char **argv)
{
    lcp_wantoptions[0].neg_upap = 1;
    setauth(NULL);
    return 1;
}

/*
 * nochap - Disable CHAP authentication with peer.
 */
static int
nochap(char **argv)
{
    refuse_chap = 1;
    return (1);
}


/*
 * reqchap - Require CHAP authentication from peer.
 */
static int
reqchap(char **argv)
{
    lcp_wantoptions[0].neg_chap = 1;
    setauth(NULL);
    return (1);
}


/*
 * setnovj - disable vj compression
 */
static int
setnovj(char **argv)
{
    ipcp_wantoptions[0].neg_vj = 0;
    ipcp_allowoptions[0].neg_vj = 0;
    return (1);
}


/*
 * setnovjccomp - disable VJ connection-ID compression
 */
static int
setnovjccomp(char **argv)
{
    ipcp_wantoptions[0].cflag = 0;
    ipcp_allowoptions[0].cflag = 0;
    return 1;
}


/*
 * setvjslots - set maximum number of connection slots for VJ compression
 */
static int
setvjslots(char **argv)
{
    int value;

    if (!int_option(*argv, &value))
	return 0;
    if (value < 2 || value > 16) {
	option_error("vj-max-slots value must be between 2 and 16");
	return 0;
    }
    ipcp_wantoptions [0].maxslotindex =
        ipcp_allowoptions[0].maxslotindex = value - 1;
    return 1;
}


/*
 * setconnector - Set a program to connect to a serial line
 */
static int
setconnector(char **argv)
{
    connector = strdup(*argv);
    if (connector == NULL)
	novm("connect script");
    connector_info.priv = privileged_option;
    connector_info.source = option_source;

    return (1);
}

/*
 * setdisconnector - Set a program to disconnect from the serial line
 */
static int
setdisconnector(char **argv)
{
    disconnector = strdup(*argv);
    if (disconnector == NULL)
	novm("disconnect script");
    disconnector_info.priv = privileged_option;
    disconnector_info.source = option_source;
  
    return (1);
}

/*
 * setwelcomer - Set a program to welcome a client after connection
 */
static int
setwelcomer(char **argv)
{
    welcomer = strdup(*argv);
    if (welcomer == NULL)
	novm("welcome script");
    welcomer_info.priv = privileged_option;
    welcomer_info.source = option_source;

    return (1);
}

/*
 * setmaxconnect - Set the maximum connect time
 */
static int
setmaxconnect(char **argv)
{
    int value;

    if (!int_option(*argv, &value))
	return 0;
    if (value < 0) {
	option_error("maxconnect time must be positive");
	return 0;
    }
    if (maxconnect > 0 && (value == 0 || value > maxconnect)) {
	option_error("maxconnect time cannot be increased");
	return 0;
    }
    maxconnect = value;
    return 1;
}

/*
 * setdomain - Set domain name to append to hostname 
 */
static int
setdomain(char **argv)
{
    if (!privileged_option) {
	option_error("using the domain option requires root privilege");
	return 0;
    }
    gethostname(hostname, MAXNAMELEN);
    if (**argv != 0) {
	if (**argv != '.')
	    strlcat(hostname, ".", MAXNAMELEN);
	strlcat(hostname, *argv, MAXNAMELEN);
    }
    hostname[MAXNAMELEN-1] = 0;
    return (1);
}


/*
 * setasyncmap - add bits to asyncmap (what we request peer to escape).
 */
static int
setasyncmap(char **argv)
{
    u_int32_t asyncmap;

    if (!number_option(*argv, &asyncmap, 16))
	return 0;
    lcp_wantoptions[0].asyncmap |= asyncmap;
    lcp_wantoptions[0].neg_asyncmap = 1;
    return(1);
}


/*
 * setescape - add chars to the set we escape on transmission.
 */
static int
setescape(char **argv)
{
    int n, ret;
    char *p, *endp;

    p = *argv;
    ret = 1;
    while (*p) {
	n = strtol(p, &endp, 16);
	if (p == endp) {
	    option_error("escape parameter contains invalid hex number '%s'",
			 p);
	    return 0;
	}
	p = endp;
	if (n < 0 || (0x20 <= n && n <= 0x3F) || n == 0x5E || n > 0xFF) {
	    option_error("can't escape character 0x%x", n);
	    ret = 0;
	} else
	    xmit_accm[0][n >> 5] |= 1 << (n & 0x1F);
	while (*p == ',' || *p == ' ')
	    ++p;
    }
    return ret;
}


/*
 * setspeed - Set the speed.
 */
static int
setspeed(char *arg)
{
    char *ptr;
    int spd;

    spd = strtol(arg, &ptr, 0);
    if (ptr == arg || *ptr != 0 || spd == 0)
	return 0;
    inspeed = spd;
    return 1;
}


/*
 * setdevname - Set the device name.
 */
static int
setdevname(char *cp, int quiet)
{
    struct stat statbuf;
    char dev[PATH_MAX];

    if (*cp == 0)
	return 0;

    if (strncmp("/dev/", cp, 5) != 0) {
	strlcpy(dev, "/dev/", sizeof dev);
	strlcat(dev, cp, sizeof dev);
	cp = dev;
    }

    /*
     * Check if there is a device by this name.
     */
    if (stat(cp, &statbuf) < 0) {
	if (errno == ENOENT || quiet)
	    return 0;
	option_error("Couldn't stat %s: %m", cp);
	return -1;
    }

    (void) strlcpy(devnam, cp, PATH_MAX);
    default_device = FALSE;
    devnam_info.priv = privileged_option;
    devnam_info.source = option_source;
  
    return 1;
}


/*
 * setipaddr - Set the IP address
 */
static int
setipaddr(char *arg)
{
    struct hostent *hp;
    char *colon;
    struct in_addr ina;
    u_int32_t local, remote;
    ipcp_options *wo = &ipcp_wantoptions[0];
  
    /*
     * IP address pair separated by ":".
     */
    if ((colon = strchr(arg, ':')) == NULL)
	return 0;
  
    /*
     * If colon first character, then no local addr.
     */
    if (colon != arg) {
	*colon = '\0';
	if (inet_pton(AF_INET, arg, &ina) != 1) {
	    if ((hp = gethostbyname(arg)) == NULL) {
		option_error("unknown host: %s", arg);
		return -1;
	    } else {
		local = *(u_int32_t *)hp->h_addr;
		if (our_name[0] == 0)
		    strlcpy(our_name, arg, MAXNAMELEN);
	    }
	} else
	    local = ina.s_addr;
	if (bad_ip_adrs(local)) {
	    option_error("bad local IP address %s", ip_ntoa(local));
	    return -1;
	}
	if (local != 0)
	    wo->ouraddr = local;
	*colon = ':';
    }
  
    /*
     * If colon last character, then no remote addr.
     */
    if (*++colon != '\0') {
	if (inet_pton(AF_INET, colon, &ina) != 1) {
	    if ((hp = gethostbyname(colon)) == NULL) {
		option_error("unknown host: %s", colon);
		return -1;
	    } else {
		remote = *(u_int32_t *)hp->h_addr;
		if (remote_name[0] == 0)
		    strlcpy(remote_name, colon, MAXNAMELEN);
	    }
	} else
	    remote = ina.s_addr;
	if (bad_ip_adrs(remote)) {
	    option_error("bad remote IP address %s", ip_ntoa(remote));
	    return -1;
	}
	if (remote != 0)
	    wo->hisaddr = remote;
    }

    return 1;
}


/*
 * setnoipdflt - disable setipdefault()
 */
static int
setnoipdflt(char **argv)
{
    disable_defaultip = 1;
    return 1;
}


/*
 * setipcpaccl - accept peer's idea of our address
 */
static int
setipcpaccl(char **argv)
{
    ipcp_wantoptions[0].accept_local = 1;
    return 1;
}


/*
 * setipcpaccr - accept peer's idea of its address
 */
static int
setipcpaccr(char **argv)
{
    ipcp_wantoptions[0].accept_remote = 1;
    return 1;
}


/*
 * setnetmask - set the netmask to be used on the interface.
 */
static int
setnetmask(char **argv)
{
    struct in_addr ina;

    if (inet_pton(AF_INET, *argv, &ina) != 1 || (netmask & ~ina.s_addr) != 0) {
	option_error("invalid netmask value '%s'", *argv);
	return (0);
    }

    netmask = ina.s_addr;
    return (1);
}

static int
setcrtscts(char **argv)
{
    crtscts = 1;
    return (1);
}

static int
setnocrtscts(char **argv)
{
    crtscts = -1;
    return (1);
}

static int
setxonxoff(char **argv)
{
    lcp_wantoptions[0].asyncmap |= 0x000A0000;	/* escape ^S and ^Q */
    lcp_wantoptions[0].neg_asyncmap = 1;

    crtscts = -2;
    return (1);
}

static int
setnodetach(char **argv)
{
    nodetach = 1;
    return (1);
}

static int
setupdetach(char **argv)
{
    nodetach = -1;
    return (1);
}

static int
setdemand(char **argv)
{
    demand = 1;
    persist = 1;
    return 1;
}

static int
setmodem(char **argv)
{
    modem = 1;
    return 1;
}

static int
setmodem_chat(char **argv)
{
    modem_chat = 1;
    return 1;
}

static int
setlocal(char **argv)
{
    modem = 0;
    return 1;
}

static int
setlock(char **argv)
{
    lockflag = 1;
    return 1;
}

static int
setusehostname(char **argv)
{
    usehostname = 1;
    return 1;
}

static int
setname(char **argv)
{
    if (!privileged_option) {
	option_error("using the name option requires root privilege");
	return 0;
    }
    strlcpy(our_name, argv[0], MAXNAMELEN);
    return 1;
}

static int
setuser(char **argv)
{
    strlcpy(user, argv[0], MAXNAMELEN);
    return 1;
}

static int
setremote(char **argv)
{
    strlcpy(remote_name, argv[0], MAXNAMELEN);
    return 1;
}

static int
setauth(char **argv)
{
    auth_required = 1;
    if (privileged_option > auth_req_info.priv) {
	auth_req_info.priv = privileged_option;
	auth_req_info.source = option_source;
    }
    return 1;
}

static int
setnoauth(char **argv)
{
    if (auth_required && privileged_option < auth_req_info.priv) {
	if (auth_req_info.source == NULL)
	    option_error("cannot override default auth option");
	else
	    option_error("cannot override auth option set by %s",
	        auth_req_info.source);
	return 0;
    }
    auth_required = 0;
    return 1;
}

static int
setdefaultroute(char **argv)
{
    if (!ipcp_allowoptions[0].default_route) {
	option_error("defaultroute option is disabled");
	return 0;
    }
    ipcp_wantoptions[0].default_route = 1;
    return 1;
}

static int
setnodefaultroute(char **argv)
{
    ipcp_allowoptions[0].default_route = 0;
    ipcp_wantoptions[0].default_route = 0;
    return 1;
}

static int
setproxyarp(char **argv)
{
    if (!ipcp_allowoptions[0].proxy_arp) {
	option_error("proxyarp option is disabled");
	return 0;
    }
    ipcp_wantoptions[0].proxy_arp = 1;
    return 1;
}

static int
setnoproxyarp(char **argv)
{
    ipcp_wantoptions[0].proxy_arp = 0;
    ipcp_allowoptions[0].proxy_arp = 0;
    return 1;
}

static int
setpersist(char **argv)
{
    persist = 1;
    return 1;
}

static int
setnopersist(char **argv)
{
    persist = 0;
    return 1;
}

static int
setdologin(char **argv)
{
    uselogin = 1;
    return 1;
}

/*
 * Functions to set the echo interval for modem-less monitors
 */

static int
setlcpechointv(char **argv)
{
    return int_option(*argv, &lcp_echo_interval);
}

static int
setlcpechofails(char **argv)
{
    return int_option(*argv, &lcp_echo_fails);
}

/*
 * Functions to set timeouts, max transmits, etc.
 */
static int
setlcptimeout(char **argv)
{
    return int_option(*argv, &lcp_fsm[0].timeouttime);
}

static int
setlcpterm(char **argv)
{
    return int_option(*argv, &lcp_fsm[0].maxtermtransmits);
}

static int
setlcpconf(char **argv)
{
    return int_option(*argv, &lcp_fsm[0].maxconfreqtransmits);
}

static int
setlcpfails(char **argv)
{
    return int_option(*argv, &lcp_fsm[0].maxnakloops);
}

static int
setipcptimeout(char **argv)
{
    return int_option(*argv, &ipcp_fsm[0].timeouttime);
}

static int
setipcpterm(char **argv)
{
    return int_option(*argv, &ipcp_fsm[0].maxtermtransmits);
}

static int
setipcpconf(char **argv)
{
    return int_option(*argv, &ipcp_fsm[0].maxconfreqtransmits);
}

static int
setipcpfails(char **argv)
{
    return int_option(*argv, &lcp_fsm[0].maxnakloops);
}

static int
setpaptimeout(char **argv)
{
    return int_option(*argv, &upap[0].us_timeouttime);
}

static int
setpapreqtime(char **argv)
{
    return int_option(*argv, &upap[0].us_reqtimeout);
}

static int
setpapreqs(char **argv)
{
    return int_option(*argv, &upap[0].us_maxtransmits);
}

static int
setchaptimeout(char **argv)
{
    return int_option(*argv, &chap[0].timeouttime);
}

static int
setchapchal(char **argv)
{
    return int_option(*argv, &chap[0].max_transmits);
}

static int
setchapintv(char **argv)
{
    return int_option(*argv, &chap[0].chal_interval);
}

static int
noccp(char **argv)
{
    ccp_protent.enabled_flag = 0;
    return 1;
}

static int
setbsdcomp(char **argv)
{
    int rbits, abits;
    char *str, *endp;

    str = *argv;
    abits = rbits = strtol(str, &endp, 0);
    if (endp != str && *endp == ',') {
	str = endp + 1;
	abits = strtol(str, &endp, 0);
    }
    if (*endp != 0 || endp == str) {
	option_error("invalid parameter '%s' for bsdcomp option", *argv);
	return 0;
    }
    if ((rbits != 0 && (rbits < BSD_MIN_BITS || rbits > BSD_MAX_BITS))
	|| (abits != 0 && (abits < BSD_MIN_BITS || abits > BSD_MAX_BITS))) {
	option_error("bsdcomp option values must be 0 or %d .. %d",
		     BSD_MIN_BITS, BSD_MAX_BITS);
	return 0;
    }
    if (rbits > 0) {
	ccp_wantoptions[0].bsd_compress = 1;
	ccp_wantoptions[0].bsd_bits = rbits;
    } else
	ccp_wantoptions[0].bsd_compress = 0;
    if (abits > 0) {
	ccp_allowoptions[0].bsd_compress = 1;
	ccp_allowoptions[0].bsd_bits = abits;
    } else
	ccp_allowoptions[0].bsd_compress = 0;
    return 1;
}

static int
setnobsdcomp(char **argv)
{
    ccp_wantoptions[0].bsd_compress = 0;
    ccp_allowoptions[0].bsd_compress = 0;
    return 1;
}

static int
setdeflate(char **argv)
{
    int rbits, abits;
    char *str, *endp;

    str = *argv;
    abits = rbits = strtol(str, &endp, 0);
    if (endp != str && *endp == ',') {
	str = endp + 1;
	abits = strtol(str, &endp, 0);
    }
    if (*endp != 0 || endp == str) {
	option_error("invalid parameter '%s' for deflate option", *argv);
	return 0;
    }
    if ((rbits != 0 && (rbits < DEFLATE_MIN_SIZE || rbits > DEFLATE_MAX_SIZE))
	|| (abits != 0 && (abits < DEFLATE_MIN_SIZE
			  || abits > DEFLATE_MAX_SIZE))) {
	option_error("deflate option values must be 0 or %d .. %d",
		     DEFLATE_MIN_SIZE, DEFLATE_MAX_SIZE);
	return 0;
    }
    if (rbits > 0) {
	ccp_wantoptions[0].deflate = 1;
	ccp_wantoptions[0].deflate_size = rbits;
    } else
	ccp_wantoptions[0].deflate = 0;
    if (abits > 0) {
	ccp_allowoptions[0].deflate = 1;
	ccp_allowoptions[0].deflate_size = abits;
    } else
	ccp_allowoptions[0].deflate = 0;
    return 1;
}

static int
setnodeflate(char **argv)
{
    ccp_wantoptions[0].deflate = 0;
    ccp_allowoptions[0].deflate = 0;
    return 1;
}

static int
setnodeflatedraft(char **argv)
{
    ccp_wantoptions[0].deflate_draft = 0;
    ccp_allowoptions[0].deflate_draft = 0;
    return 1;
}

static int
setpred1comp(char **argv)
{
    ccp_wantoptions[0].predictor_1 = 1;
    ccp_allowoptions[0].predictor_1 = 1;
    return 1;
}

static int
setnopred1comp(char **argv)
{
    ccp_wantoptions[0].predictor_1 = 0;
    ccp_allowoptions[0].predictor_1 = 0;
    return 1;
}

static int
setipparam(char **argv)
{
    ipparam = strdup(*argv);
    if (ipparam == NULL)
	novm("ipparam string");

    return 1;
}

static int
setpapcrypt(char **argv)
{
    cryptpap = 1;
    return 1;
}

static int
setidle(char **argv)
{
    return int_option(*argv, &idle_time_limit);
}

static int
setholdoff(char **argv)
{
    return int_option(*argv, &holdoff);
}

/*
 * setdnsaddr - set the dns address(es)
 */
static int
setdnsaddr(char **argv)
{
    struct in_addr ina;
    struct hostent *hp;

    if (inet_pton(AF_INET, *argv, &ina) != 1) {
	if ((hp = gethostbyname(*argv)) == NULL) {
	    option_error("invalid address parameter '%s' for ms-dns option",
			 *argv);
	    return (0);
	}
	ina.s_addr = *(u_int32_t *)hp->h_addr;
    }

    /* if there is no primary then update it. */
    if (ipcp_allowoptions[0].dnsaddr[0] == 0)
	ipcp_allowoptions[0].dnsaddr[0] = ina.s_addr;

    /* always set the secondary address value to the same value. */
    ipcp_allowoptions[0].dnsaddr[1] = ina.s_addr;

    return (1);
}

/*
 * setwinsaddr - set the wins address(es)
 * This is primrarly used with the Samba package under UNIX or for pointing
 * the caller to the existing WINS server on a Windows NT platform.
 */
static int
setwinsaddr(char **argv)
{
    struct in_addr ina;
    struct hostent *hp;

    if (inet_pton(AF_INET, *argv, &ina) != 1) {
	if ((hp = gethostbyname(*argv)) == NULL) {
	    option_error("invalid address parameter '%s' for ms-wins option",
			 *argv);
	    return (0);
	}
	ina.s_addr = *(u_int32_t *)hp->h_addr;
    }

    /* if there is no primary then update it. */
    if (ipcp_allowoptions[0].winsaddr[0] == 0)
	ipcp_allowoptions[0].winsaddr[0] = ina.s_addr;

    /* always set the secondary address value to the same value. */
    ipcp_allowoptions[0].winsaddr[1] = ina.s_addr;

    return (1);
}

#ifdef MSLANMAN
static int
setmslanman(char **argv)
{
    ms_lanman = 1;
    return (1);
}
#endif
