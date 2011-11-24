/*
 *	N U T T C P . C						v5.5.5
 *
 * Copyright(c) 2000 - 2006 Bill Fink.  All rights reserved.
 * Copyright(c) 2003 - 2006 Rob Scott.  All rights reserved.
 *
 * nuttcp is free, opensource software.  You can redistribute it and/or
 * modify it under the terms of Version 2 of the GNU General Public
 * License (GPL), as published by the GNU Project (http://www.gnu.org).
 * A copy of the license can also be found in the LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * Based on nttcp
 * Developed by Bill Fink, billfink@mindspring.com
 *          and Rob Scott, rob@hpcmo.hpc.mil
 * Latest version available at:
 *	ftp://ftp.lcp.nrl.navy.mil/pub/nuttcp/
 *
 * Test TCP connection.  Makes a connection on port 5001
 * and transfers fabricated buffers or data copied from stdin.
 *
 * Run nuttcp with no arguments to get a usage statement
 *
 * Modified for operation under 4.2BSD, 18 Dec 84
 *      T.C. Slattery, USNA
 * Minor improvements, Mike Muuss and Terry Slattery, 16-Oct-85.
 *
 * 5.5.5, Bill Fink, 1-Feb-07
 *	Change default MC addr to be based on client addr instead of xmit addr
 * 5.5.4, Bill Fink, 3-Nov-06
 *	Fix bug with negative loss causing huge drop counts on interval reports
 *	Updated Copyright notice and added GPL license notice
 * 5.5.3, Bill Fink, 23-Oct-06
 *	Fix bug with "-Ri" instantaneous rate limiting not working properly
 * 5.5.2, Bill Fink, 25-Jul-06
 *	Make manually started server multi-threaded
 *	Add "--single-threaded" server option to restore old behavior
 *	Add "-a" client option to retry a failed server connection "again"
 *	(for certain possibly transient errors)
 * 5.5.1, Bill Fink, 22-Jul-06
 *	Fix bugs with nbuf_bytes and rate_pps used with 3rd party
 *	Pass "-D" option to server (and also make work for third party)
 *	Allow setting precedence with "-c##p"
 * 5.4.3, Rob Scott & Bill Fink, 17-Jul-06
 *	Fix bug with buflen passed to server when no buflen option speicified
 *	(revert 5.3.2: Fix bug with default UDP buflen for 3rd party)
 *	Better way to fix bug with default UDP buflen for 3rd party
 *	Trim trailing '\n' character from err() calls
 *	Use fcntl() to set O_NONBLOCK instead of MSG_DONTWAIT to send() ABORT
 *	(and remove MSG_DONTWAIT from recv() because it's not needed)
 *	Don't re-initialize buflen at completion of server processing
 *	(non inetd: is needed to check for buffer memory allocation change,
 *	caused bug if smaller "-l" followed by larger default "-l")
 * 5.4.2, Bill Fink, 1-Jul-06
 *	Fix bug with interrupted UDP receive reporting negative packet loss
 *	Make sure errors (or debug) from server are propagated to the client
 *	Make setsockopt SO_SNDBUF/SO_RCVBUF error not be fatal to server
 *	Don't send stderr to client if nofork is set (manually started server)
 * 5.4.1, Bill Fink, 30-Jun-06
 *	Fix bug with UDP reporting > linerate because of bad correction
 *	Send 2nd UDP BOD packet in case 1st is lost, e.g. waiting for ARP reply
 *	(makes UDP BOD more robust for new separate control and data paths)
 *	Fix bug with interval reports after EOD for UDP with small interval
 *	Don't just exit inetd server on no data so can get partial results
 *	(keep an eye out that servers don't start hanging again)
 *	Make default idle data timeout 1/2 of timeout if interval not set
 *	(with a minimum of 5 seconds and a maximum of 60 seconds)
 *	Make server send abort via urgent TCP data if no UDP data received
 *	(non-interval only: so client won't keep transmitting for full period)
 *	Workaround for Windows not handling TCP_MAXSEG getsockopt()
 * 5.3.4, Bill Fink, 21-Jun-06
 *	Add "--idle-data-timeout" server option
 *	(server ends transfer if no data received for the specified
 *	timeout interval, previously it was a fixed 60 second timeout)
 *	Shutdown client control connection for writing at end of UDP transfer
 *	(so server can cope better with loss of all EOD packets, which is
 *	mostly of benefit when using separate control and data paths)
 * 5.3.3, Bill Fink & Mark S. Mathews, 18-Jun-06
 *	Add new capability for separate control and data paths
 *	(using syntax:  nuttcp ctl_name_or_IP/data_name_or_IP)
 *	Extend new capability for multiple independent data paths
 *	(using syntax:  nuttcp ctl/data1/data2/.../datan)
 *	Above only supported for transmit or flipped/reversed receive
 *	Fix -Wall compiler warnings on 64-bit systems
 *	Make manually started server also pass stderr to client
 *	(so client will get warning messages from server)
 * 5.3.2, Bill Fink, 09-Jun-06
 *	Fix bug with default UDP buflen for 3rd party
 *	Fix compiler warnings with -Wall on FreeBSD
 *	Give warning that windows doesn't support TCP_MAXSEG
 * 5.3.1, Rob Scott, 06-Jun-06
 *	Add "-c" COS option for setting DSCP/TOS setting
 *	Fix builds on latest MacOS X
 *	Fix bug with 3rd party unlimited rate UDP not working
 *	Change "-M" option to require a value
 *	Fix compiler warnings with -Wall (thanks to Daniel J Blueman)
 *	Remove 'v' from nuttcp version (simplify RPM packaging)
 * V5.2.2, Bill Fink, 13-May-06
 *	Have client report server warnings even if not verbose
 * V5.2.1, Bill Fink, 12-May-06
 *	Pass "-M" option to server so it also works for receives
 *	Make "-uu" be a shortcut for "-u -Ru"
 * V5.1.14, Bill Fink, 11-May-06
 *	Fix cancellation of UDP receives to work properly
 *	Allow easy building without IPv6 support
 *	Set default UDP buflen to largest 2^n less than MSS of ctlconn
 *	Add /usr/local/sbin and /usr/etc to path
 *	Allow specifying rate in pps by using 'p' suffix
 *	Give warning if actual send/receive window size is less than requested
 *	Make UDP transfers have a default rate limit of 1 Mbps
 *	Allow setting MSS for client transmitter TCP transfers with "-M" option
 *	Give more precision on reporting small UDP percentage data loss
 *	Disallow UDP transfers in "classic" mode
 *	Notify when using "classic" mode
 * V5.1.13, Bill Fink, 8-Apr-06
 *	Make "-Ri" instantaneous rate limit for very high rates more accurate
 *	(including compensating for microsecond gettimeofday() granularity)
 *	Fix bug giving bogus time/stats on UDP transmit side with "-Ri"
 *	Allow fractional rate limits (for 'm' and 'g' only)
 * V5.1.12, Bill Fink & Rob Scott, 4-Oct-05
 *	Terminate server receiver if client control connection goes away
 *	or if no data received from client within CHECK_CLIENT_INTERVAL
 * V5.1.11, Rob Scott, 25-Jun-04
 *	Add support for scoped ipv6 addresses
 * V5.1.10, Bill Fink, 16-Jun-04
 *	Allow 'b' suffix on "-w" option to specify window size in bytes
 * V5.1.9, Bill Fink, 23-May-04
 *	Fix bug with client error on "-d" option putting server into bad state
 *	Set server accept timeout (currently 5 seconds) to prevent stuck server
 *	Add nuttcp version info to error message from err() exit
 * V5.1.8, Bill Fink, 22-May-04
 *	Allow 'd|D' suffix to "-T" option to specify days
 *	Fix compiler warning about unused variable cp in getoptvalp routine
 *	Interval value check against timeout value should be >=
 * V5.1.7, Bill Fink, 29-Apr-04
 *	Drop "-nb" option in favor of "-n###[k|m|g|t|p]"
 * V5.1.6, Bill Fink, 25-Apr-04
 *	Fix bug with using interval option without timeout
 * V5.1.5, Bill Fink, 23-Apr-04
 *	Modification to allow space between option parameter and its value
 *	Permit 'k' or 'm' suffix on "-l" option
 *	Add "-nb" option to specify number of bytes to transfer
 *	Permit 'k', 'm', 'g', 't', or 'p' suffix on "-n" and "-nb" options
 * V5.1.4, Bill Fink, 21-Apr-04
 *	Change usage statement to use standard out instead of standard error
 *	Fix bug with interval > timeout, give warning and ignore interval
 *	Fix bug with counting error value in nbytes on interrupted transfers
 *	Fix bug with TCP transmitted & received nbytes not matching
 *	Merge "-t" and "-r" options in Usage: statement
 * V5.1.3, Bill Fink, 9-Apr-04
 *	Add "-Sf" force server mode (useful for starting server via rsh/ssh)
 *	Allow non-root user to find nuttcp binary in "."
 *	Fix bug with receives terminating early with manual server mode
 *	Fix bug with UDP receives not terminating with "-Ri" option
 *	Clean up output formatting of nbuf (from "%d" to "%llu")
 *	Add "-SP" to have 3rd party use same outgoing control port as incoming
 * V5.1.2, Bill Fink & Rob Scott, 18-Mar-04
 *	Fix bug with nbuf wrapping on really long transfers (int -> uint64_t)
 *	Fix multicast address to be unsigned long to allow shift
 *	Add MacOS uint8_t definition for new use of uint8_t
 * V5.1.1, Bill Fink, 8-Nov-03
 *	Add IPv4 multicast support
 *	Delay receiver EOD until EOD1 (for out of order last data packet)
 *	Above also drains UDP receive buffer (wait for fragmentation reassembly)
 * V5.0.4, Bill Fink, 6-Nov-03
 *	Fix bug reporting 0 drops when negative loss percentage
 * V5.0.3, Bill Fink, 6-Nov-03
 *	Kill server transmission if control connection goes away
 *	Kill 3rd party nuttcp if control connection goes away
 * V5.0.2, Bill Fink, 4-Nov-03
 *	Fix bug: some dummy wasn't big enough :-)
 * V5.0.1, Bill Fink, 3-Nov-03
 *	Add third party support
 *	Correct usage statement for "-xt" traceroute option
 *	Improved error messages on failed options requiring client/server mode
 * V4.1.1, David Lapsley and Bill Fink, 24-Oct-03
 *	Added "-fparse" format option to generate key=value parsable output
 *	Fix bug: need to open data connection on abortconn to clear listen
 * V4.0.3, Rob Scott, 13-Oct-03
 *	Minor tweaks to output format for alignment
 *	Interval option "-i" with no explicit value sets interval to 1.0
 * V4.0.2, Bill Fink, 10-Oct-03
 *	Changed "-xt" option to do both forward and reverse traceroute
 *	Changed to use brief output by default ("-v" for old default behavior)
 * V4.0.1, Rob Scott, 10-Oct-03
 *	Added IPv6 code 
 *	Changed inet get functions to protocol independent versions
 *	Added fakepoll for hosts without poll() (macosx)
 *	Added ifdefs to only include setprio if os supports it (non-win)
 *	Added bits to handle systems without new inet functions (solaris < 2.8)
 *	Removed SYSV obsolete code
 *	Added ifdefs and code to handle cygwin and beginning of windows support
 *	Window size can now be in meg (m|M) and gig (g|G)
 *	Added additional directories to search for traceroute
 *	Changed default to transmit, time limit of 10 seconds, no buffer limit
 *	Added (h|H) as option to specify time in hours
 *	Added getservbyname calls for port, if all fails use old defaults
 *	Changed sockaddr to sockaddr_storage to handle v6 addresses
 * v3.7.1, Bill Fink, 10-Aug-03
 *	Add "-fdebugpoll" option to help debug polling for interval reporting
 *	Fix Solaris compiler warning
 *	Use poll instead of separate process for interval reports
 * v3.6.2, Rob Scott, 18-Mar-03
 *	Allow setting server window to use default value
 *	Cleaned out BSD42 old code
 *	Marked SYSV code for future removal as it no longer appears necessary
 *	Also set RCVBUF/SNDBUF for udp transfers
 *	Changed transmit SO_DEBUG code to be like receive
 *	Some code rearrangement for setting options before accept/connect
 * v3.6.1, Bill Fink, 1-Mar-03
 *	Add -xP nuttcp process priority option
 *	Add instantaneous rate limit capability ("-Ri")
 *	Don't open data connection if server error or doing traceroute
 *	Better cleanup on server connection error (close open data connections)
 *	Don't give normal nuttcp output if server error requiring abort
 *	Implement -xt traceroute option
 * v3.5.1, Bill Fink, 27-Feb-03
 *	Don't allow flip option to be used with UDP
 *	Fix bug with UDP and transmit interval option (set stdin unbuffered)
 *	Fix start of UDP timing to be when get BOD
 *	Fix UDP timing when don't get first EOD
 *	Fix ident option used with interval option
 *	Add "-f-percentloss" option to not give %loss info on brief output
 *	Add "-f-drops" option to not give packet drop info on brief output
 *	Add packet drop info to UDP brief output (interval report and final)
 *	Add "-frunningtotal" option to give cumulative stats for "-i"
 *	Add "-fdebuginterval" option to help debug interval reporting
 *	Add "-fxmitstats" option to give transmitter stats
 *	Change flip option from "-f" to "-F"
 *	Fix divide by zero bug with "-i" option and very low rate limit
 *	Fix to allow compiling with Irix native compiler
 *	Fix by Rob Scott to allow compiling on MacOS X
 * v3.4.5, Bill Fink, 29-Jan-03
 *	Fix client/server endian issues with UDP loss info for interval option
 * v3.4.4, Bill Fink, 29-Jan-03
 *	Remove some debug printout for interval option
 *	Fix bug when using interval option reporting 0.000 MB on final
 * v3.4.3, Bill Fink, 24-Jan-03
 *	Added UDP approximate loss info for interval reporting
 *	Changed nbytes and pbytes from double to uint64_t
 *	Changed SIGUSR1 to SIGTERM to kill sleeping child when done
 * v3.4.2, Bill Fink, 15-Jan-03
 *	Make <control-C> work right with receive too
 * v3.4.1, Bill Fink, 13-Jan-03
 *	Fix bug interacting with old servers
 *	Add "-f" flip option to reverse direction of data connection open
 *	Fix bug by disabling interval timer when server done
 * v3.3.2, Bill Fink, 11-Jan-03
 *	Make "-i" option work for client transmit too
 *	Fix bug which forced "-i" option to be at least 0.1 seconds
 * v3.3.1, Bill Fink, 7-Jan-03
 *	Added -i option to set interval timer (client receive only)
 *	Fixed server bug not setting socket address family
 * v3.2.1, Bill Fink, 25-Feb-02
 *	Fixed bug so second <control-C> will definitely kill nuttcp
 *	Changed default control port to 5000 (instead of data port - 1)
 *	Modified -T option to accept fractional seconds
 * v3.1.10, Bill Fink, 6-Feb-02
 *	Added -I option to identify nuttcp output
 *	Made server always verbose (filtering is done by client)
 *	Update to usage statement
 *	Minor fix to "-b" output when "-D" option is used
 *	Fix bug with "-s" that appends nuttcp output to receiver data file
 *	Fix bug with "-b" that gave bogus CPU utilization on > 1 hour transfers
 * v3.1.9, Bill Fink, 21-Dec-01
 *	Fix bug with "-b" option on SGI systems reporting 0% CPU utilization
 * v3.1.8, Bill Fink, 21-Dec-01
 *	Minor change to brief output format to make it simpler to awk
 * v3.1.7, Bill Fink, 20-Dec-01
 *	Implement "-b" option for brief output (old "-b" -> "-wb")
 *	Report udp loss percentage when using client/server mode
 *	Fix bug with nbytes on transmitter using timed transfer
 *	Combined send/receive window size printout onto a single line
 * v3.1.6, Bill Fink, 11-Jun-01
 *	Fixed minor bug reporting error connecting to inetd server
 * Previously, Bill Fink, 7-Jun-01
 *	Added -h (usage) and -V (version) options
 *	Fixed SGI compilation warnings
 *	Added reporting server version to client
 *	Added version info and changed ttcp prints to nuttcp
 *	Fixed bug with inetd server and client using -r option
 *	Added ability to run server from inetd
 *	Added udp capability to server option
 *	Added -T option to set timeout interval
 *	Added -ws option to set server window
 *	Added -S option to support running receiver as daemon
 *	Allow setting UDP buflen up to MAXUDPBUFLEN
 *	Provide -b option for braindead Solaris 2.8
 *	Added printing of transmit rate limit
 *	Added -w option to usage statement
 *	Added -N option to support multiple streams
 *	Added -R transmit rate limit option
 *	Fix setting of destination IP address on 64-bit Irix systems
 *	Only set window size in appropriate direction to save memory
 *	Fix throughput calculation for large transfers (>= 2 GB)
 *	Fix reporting of Mb/s to give actual millions of bits per second
 *	Fix setting of INET address family in local socket
 *	Fix setting of receiver window size
 *
 * TODO/Wish-List:
 *	Transmit interval marking option
 *	Allow at least some traceroute options
 *	IPv6 multicast support
 *	Add "-ut" option to do both UDP and TCP simultaneously
 *	Default rate limit UDP if too much loss
 *	QOS support
 *	Ping option
 *	Other brief output formats
 *	Linux window size bug/feature note
 *	Retransmission/timeout info
 *	Network interface interrupts (for Linux only)
 *	netstat -i info
 *	Man page
 *	Forking for multiple streams
 *	Bidirectional option
 *	Graphical interface
 *	OWD info
 *	RTT info
 *	Jitter info
 *	MTU info
 *	Warning for window size limiting throughput
 *	Auto window size optimization
 *	Transmitter profile and playback options
 *	Server side limitations (per client host/network)
 *	Server side logging
 *	Client/server security (password)
 *	nuttcp server registration
 *	nuttcp proxy support (firewalls)
 *	nuttcp network idle time
 *
 * Distribution Status -
 *	OpenSource(tm)
 *	Licensed under version 2 of the GNU GPL
 *	Please send source modifications back to the authors
 *	Derivative works should be redistributed with a modified
 *	source and executable name
 */

/*
#ifndef lint
static char RCSid[] = "@(#)$Revision: 1.2 $ (BRL)";
#endif
*/

#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>		/* struct timeval */
#include <stdlib.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/resource.h>
#else
#include "win32nuttcp.h"			/* from win32 */
#endif /* _WIN32 */

#include <limits.h>
#include <string.h>
#include <fcntl.h>

/* Let's try changing the previous unwieldy check */
/* #if defined(linux) || defined(__FreeBSD__) || defined (sgi) || (defined(__MACH__) && defined(_SOCKLEN_T)) || defined(sparc) || defined(__CYGWIN__) */
/* to the following (hopefully equivalent) simpler form like we use
 * for HAVE_POLL */
#if !defined(_WIN32) && (!defined(__MACH__) || defined(_SOCKLEN_T))
#include <unistd.h>
#include <sys/wait.h>
#include <strings.h>
#endif

#ifndef ULLONG_MAX
#define ULLONG_MAX	18446744073709551615ULL
#endif

#define MAXRATE 0xffffffffUL

#if !defined(__CYGWIN__) && !defined(_WIN32)
#define HAVE_SETPRIO
#endif

#if !defined(_WIN32) && (!defined(__MACH__) || defined(_SOCKLEN_T))
#define HAVE_POLL
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define uint64_t u_int64_t
#define uint32_t u_int32_t
#define uint16_t u_int16_t
#define uint8_t u_int8_t
#endif

#ifdef HAVE_POLL
#include <sys/poll.h>
#else
#include "fakepoll.h"			/* from missing */
#endif

/*
 * _SOCKLEN_T is now defined by apple when they typedef socklen_t
 *
 * EAI_NONAME has nothing to do with socklen, but on sparc without it tells
 * us it's an old enough solaris to need the typedef
 */
#if (defined(__APPLE__) && defined(__MACH__)) && !defined(_SOCKLEN_T) || (defined(sparc) && !defined(EAI_NONAME))
typedef int socklen_t;
#endif

#if defined(sparc) && !defined(EAI_NONAME) /* old sparc */
#define sockaddr_storage sockaddr
#define ss_family sa_family
#endif /* old sparc */

#if defined(_AIX)
#define ss_family __ss_family
#endif

#if !defined(EAI_NONAME)
#include "addrinfo.h"			/* from missing */
#endif

static struct	timeval time0;	/* Time at which timing started */
static struct	timeval timepk;	/* Time at which last packet sent */
static struct	timeval timep;	/* Previous time - for interval reporting */
static struct	rusage ru0;	/* Resource utilization at the start */

static struct	sigaction sigact;	/* signal handler for alarm */
static struct	sigaction savesigact;

#define PERF_FMT_OUT	  "%.4f MB in %.2f real seconds = %.2f KB/sec" \
			  " = %.4f Mbps\n"
#define PERF_FMT_BRIEF	  "%10.4f MB / %6.2f sec = %9.4f Mbps %d %%TX %d %%RX"
#define PERF_FMT_BRIEF2	  "%10.4f MB / %6.2f sec = %9.4f Mbps %d %%%s"
#define PERF_FMT_BRIEF3	  " Trans: %.4f MB"
#define PERF_FMT_INTERVAL  "%10.4f MB / %6.2f sec = %9.4f Mbps"
#define PERF_FMT_INTERVAL2 " Tot: %10.4f MB / %6.2f sec = %9.4f Mbps"
#define PERF_FMT_INTERVAL3 " Trans: %10.4f MB"
#define PERF_FMT_INTERVAL4 " Tot: %10.4f MB"
#define PERF_FMT_IN	  "%lf MB in %lf real seconds = %lf KB/sec = %lf Mbps\n"
#define CPU_STATS_FMT_IN  "%*fuser %*fsys %*d:%*dreal %d%%"
#define CPU_STATS_FMT_IN2 "%*fuser %*fsys %*d:%*d:%*dreal %d%%"

#define LOSS_FMT	" %.2f%% data loss"
#define LOSS_FMT_BRIEF	" %.2f %%loss"
#define LOSS_FMT_INTERVAL " %5.2f ~%%loss"
#define LOSS_FMT5	" %.5f%% data loss"
#define LOSS_FMT_BRIEF5	" %.5f %%loss"
#define LOSS_FMT_INTERVAL5 " %7.5f ~%%loss"
#define DROP_FMT	" %lld / %lld drop/pkt"
#define DROP_FMT_BRIEF	" %lld / %lld drop/pkt"
#define DROP_FMT_INTERVAL " %5lld / %5lld ~drop/pkt"

/* Parsable output formats */

#define P_PERF_FMT_OUT	  "megabytes=%.4f real_seconds=%.2f " \
                          "rate_KBps=%.2f rate_Mbps=%.4f\n"
#define P_PERF_FMT_BRIEF  "megabytes=%.4f real_seconds=%.2f rate_Mbps=%.4f " \
			  "tx_cpu=%d rx_cpu=%d"
#define P_PERF_FMT_BRIEF3 " tx_megabytes=%.4f"
#define P_PERF_FMT_INTERVAL  "megabytes=%.4f real_sec=%.2f rate_Mbps=%.4f"
#define P_PERF_FMT_INTERVAL2 " total_megabytes=%.4f total_real_sec=%.2f" \
			     " total_rate_Mbps=%.4f"
#define P_PERF_FMT_INTERVAL3 " tx_megabytes=%.4f"
#define P_PERF_FMT_INTERVAL4 " tx_total_megabytes=%.4f"
#define P_PERF_FMT_IN	  "megabytes=%lf real_seconds=%lf rate_KBps=%lf " \
			  "rate_Mbps=%lf\n"
#define P_CPU_STATS_FMT_IN  "user=%*f system=%*f elapsed=%*d:%*d cpu=%d%%"
#define P_CPU_STATS_FMT_IN2 "user=%*f system=%*f elapsed=%*d:%*d:%*d cpu=%d%%"

#define P_LOSS_FMT		" data_loss=%.5f"
#define P_LOSS_FMT_BRIEF	" data_loss=%.5f"
#define P_LOSS_FMT_INTERVAL	" data_loss=%.5f" 
#define P_DROP_FMT		" drop=%lld pkt=%lld"
#define P_DROP_FMT_BRIEF	" drop=%lld pkt=%lld"
#define P_DROP_FMT_INTERVAL	" drop=%lld pkt=%lld"

#define HELO_FMT	"HELO nuttcp v%d.%d.%d\n"

#ifndef MAXSTREAM
#define MAXSTREAM		128
#endif
#define DEFAULT_NBUF		2048
#define DEFAULT_NBYTES		134217728	/* 128 MB */
#define DEFAULT_TIMEOUT		10.0
#define DEFAULT_UDP_RATE	1000
#define DEFAULTUDPBUFLEN	8192
#define DEFAULT_MC_UDPBUFLEN	1024
#define MAXUDPBUFLEN		65507
#define MINMALLOC		1024
#define HI_MC			231ul
#define ACCEPT_TIMEOUT		5
#ifndef MAX_CONNECT_TRIES
#define MAX_CONNECT_TRIES	5	/* maximum server connect attempts */
#endif
#define IDLE_DATA_MIN		5.0	/* minimum value for chk_idle_data */
#define DEFAULT_IDLE_DATA	30.0	/* default value for chk_idle_data */
#define IDLE_DATA_MAX		60.0	/* maximum value for chk_idle_data */
#define NON_JUMBO_ETHER_MSS	1448	/* 1500 - 20:IP - 20:TCP -12:TCPOPTS */

#define XMITSTATS		0x1	/* also give transmitter stats (MB) */
#define DEBUGINTERVAL		0x2	/* add info to assist with
					 * debugging interval reports */
#define	RUNNINGTOTAL		0x4	/* give cumulative stats for "-i" */
#define	NODROPS			0x8	/* give packet drop stats for "-i" */
#define	NOPERCENTLOSS		0x10	/* don't give percent loss for "-i" */
#define DEBUGPOLL		0x20	/* add info to assist with debugging
					 * polling for interval reports */
#define PARSE			0x40	/* generate key=value parsable output */
#define DEBUGMTU		0x80	/* debug info for MTU/MSS code */

#ifdef NO_IPV6				/* Build without IPv6 support */
#undef AF_INET6
#undef IPV6_V6ONLY
#endif

void sigpipe( int signum );
void sigint( int signum );
void ignore_alarm( int signum );
void sigalarm( int signum );
static void err( char *s );
static void mes( char *s );
static void errmes( char *s );
void pattern( char *cp, int cnt );
void prep_timer();
double read_timer( char *str, int len );
static void prusage( struct rusage *r0,  struct rusage *r1, struct timeval *e, struct timeval *b, char *outp );
static void tvadd( struct timeval *tsum, struct timeval *t0, struct timeval *t1 );
static void tvsub( struct timeval *tdiff, struct timeval *t1, struct timeval *t0 );
static void psecs( long l, char *cp );
int Nread( int fd, char *buf, int count );
int Nwrite( int fd, char *buf, int count );
int delay( int us );
int mread( int fd, char *bufp, unsigned n);
char *getoptvalp( char **argv, int index, int reqval, int *skiparg );

int vers_major = 5;
int vers_minor = 5;
int vers_delta = 5;
int ivers;
int rvers_major = 0;
int rvers_minor = 0;
int rvers_delta = 0;
int irvers;

struct sockaddr_in sinme[MAXSTREAM + 1];
struct sockaddr_in sinhim[MAXSTREAM + 1];
struct sockaddr_in save_sinhim, save_mc;

#ifdef AF_INET6
struct sockaddr_in6 sinme6[MAXSTREAM + 1];
struct sockaddr_in6 sinhim6[MAXSTREAM + 1];
#endif

struct sockaddr_storage frominet;

int domain = PF_UNSPEC;
int af = AF_UNSPEC;
int explicitaf = 0;		/* address family explicit specified (-4|-6) */
int fd[MAXSTREAM + 1];		/* fd array of network sockets */
int nfd;			/* fd for accept call */
struct pollfd pollfds[MAXSTREAM + 4];	/* used for reading interval reports */
socklen_t fromlen;

int buflen = 64 * 1024;		/* length of buffer */
int nbuflen;
int mallocsize;
char *buf;			/* ptr to dynamic buffer */
unsigned long long nbuf = 0;	/* number of buffers to send in sinkmode */
int nbuf_bytes = 0;		/* set to 1 if nbuf is actually bytes */

/*  nick code  */
int sendwin=0, sendwinval=0, origsendwin=0;
socklen_t optlen;
int rcvwin=0, rcvwinval=0, origrcvwin=0;
int srvrwin=0;
/*  end nick code  */

int udp = 0;			/* 0 = tcp, !0 = udp */
int udplossinfo = 0;		/* set to 1 to give UDP loss info for
				 * interval reporting */
int need_swap;			/* client and server are different endian */
int options = 0;		/* socket options */
int one = 1;                    /* for 4.3 BSD style setsockopt() */
/* default port numbers if command arg or getserv doesn't get a port # */
#define DEFAULT_PORT	5001
#define DEFAULT_CTLPORT	5000
unsigned short port = 0;	/* TCP port number */
unsigned short ctlport = 0;	/* control port for server connection */
int tmpport;
char *host;			/* ptr to name of host */
char *host3 = NULL;		/* ptr to 3rd party host */
int thirdparty = 0;		/* set to 1 indicates doing 3rd party nuttcp */
int no3rd = 0;			/* set to 1 by server to disallow 3rd party */
int force_server = 0;		/* set to 1 to force server mode (for rsh) */
int pass_ctlport = 0;		/* set to 1 to use same outgoing control port
				   as incoming with 3rd party usage */
char *cmdargs[50];		/* command arguments array */
char tmpargs[50][40];

#ifndef AF_INET6
#define ADDRSTRLEN 16
#else
#define ADDRSTRLEN INET6_ADDRSTRLEN
int v4mapped = 0;		/* set to 1 to enable v4 mapping in v6 server */
#endif

#define HOSTNAMELEN	80

char hostbuf[ADDRSTRLEN];	/* buffer to hold text of address */
char host3buf[HOSTNAMELEN + 1];	/* buffer to hold 3rd party name or address */
int trans = 1;			/* 0=receive, !0=transmit mode */
int sinkmode = 1;		/* 0=normal I/O, !0=sink/source mode */
int nofork = 0;	 		/* set to 1 to not fork server */
int verbose = 0;		/* 0=print basic info, 1=print cpu rate, proc
				 * resource usage. */
int nodelay = 0;		/* set TCP_NODELAY socket option */
unsigned long rate = MAXRATE;	/* transmit rate limit in Kbps */
int irate = 0;			/* instantaneous rate limit if set */
double pkt_time;		/* packet transmission time in seconds */
uint64_t irate_pk_usec;		/* packet transmission time in microseconds */
double irate_pk_nsec;		/* nanosecond portion of pkt xmit time */
double irate_cum_nsec = 0.0;	/* cumulative nanaseconds over several pkts */
int rate_pps = 0;		/* set to 1 if rate is given as pps */
double timeout = 0.0;		/* timeout interval in seconds */
double interval = 0.0;		/* interval timer in seconds */
double chk_idle_data = 0.0;	/* server receiver checks this often */
				/* for client having gone away */
double chk_interval = 0.0;	/* timer (in seconds) for checking client */
int ctlconnmss;			/* control connection maximum segment size */
int datamss = 0;		/* data connection maximum segment size */
unsigned int tos = 0;		/* 8-bit TOS field for setting DSCP/TOS */
char intervalbuf[256+2];	/* buf for interval reporting */
char linebuf[256+2];		/* line buffer */
int do_poll = 0;		/* set to read interval reports (client xmit) */
int got_done = 0;		/* set when read last of interval reports */
int reverse = 0;		/* reverse direction of data connection open */
int format = 0;			/* controls formatting of output */
char fmt[257];
int traceroute = 0;		/* do traceroute back to client if set */
int skip_data = 0;		/* skip opening of data channel */
#if defined(linux)
int multicast = 0;		/* set to 1 for multicast UDP transfer */
#else
uint8_t multicast = 0;		/* set to 1 for multicast UDP transfer */
#endif
int mc_param;
struct ip_mreq mc_group;	/* holds multicast group address */

#ifdef HAVE_SETPRIO
int priority = 0;		/* nuttcp process priority */
#endif

long timeout_sec = 0;
struct itimerval itimer;	/* for setitimer */
int srvr_helo = 1;		/* set to 0 if server doesn't send HELO */
char ident[40 + 1 + 1] = "";	/* identifier for nuttcp output */
int intr = 0;
int abortconn = 0;
int braindead = 0;		/* for braindead Solaris 2.8 systems */
int brief = 1;			/* set for brief output */
int brief3 = 1;			/* for third party nuttcp */
int done = 0;			/* don't output interval report if done */
int got_begin = 0;		/* don't output interval report if not begun */
int two_bod = 0;		/* newer versions send 2 BOD packets for UDP */
int handle_urg = 0;		/* newer versions send/recv urgent TCP data */
int got_eod0 = 0;		/* got EOD0 packet - marks end of UDP xfer */
int buflenopt = 0;		/* whether or not user specified buflen */
int haverateopt = 0;		/* whether or not user specified rate */
int clientserver = 0;		/* client server mode (use control channel) */
int client = 0;			/* 0=server side, 1=client (initiator) side */
int oneshot = 0;		/* 1=run server only once */
int inetd = 0;			/* set to 1 if server run from inetd */
pid_t pid;			/* process id when forking server process */
pid_t wait_pid;			/* return of wait system call */
int pidstat;			/* status of forked process */
FILE *ctlconn;			/* uses fd[0] for control channel */
int savestdout;			/* used to save real standard out */
int realstdout;			/* used for "-s" output to real standard out */
int firsttime = 1;		/* flag for first pass through server */
struct in_addr clientaddr;	/* IP address of client connecting to server */

#ifdef AF_INET6
struct in6_addr clientaddr6;	/* IP address of client connecting to server */
uint32_t clientscope6;		/* scope part of IP address of client */
#endif

struct hostent *addr;
extern int errno;

char Usage[] = "\
Usage: nuttcp or nuttcp -h	prints this usage info\n\
Usage: nuttcp -V		prints version info\n\
Usage: nuttcp -xt [-m] host	forward and reverse traceroute to/from server\n\
Usage (transmitter): nuttcp [-t] [-options] [ctl_addr/]host [3rd-party] [<in]\n\
      |(receiver):   nuttcp -r [-options] [host] [3rd-party] [>out]\n\
	-4	Use IPv4\n"
#ifdef AF_INET6
"	-6	Use IPv6\n"
#endif
"	-c##	cos dscp value on data streams (t|T suffix for full TOS field)\n\
	-l##	length of network write|read buf (default 1K|8K/udp, 64K/tcp)\n\
	-s	use stdin|stdout for data input|output instead of pattern data\n\
	-n##	number of source bufs written to network (default unlimited)\n\
	-w##	transmitter|receiver window size in KB (or (m|M)B or (g|G)B)\n\
	-ws##	server receive|transmit window size in KB (or (m|M)B or (g|G)B)\n\
	-wb	braindead Solaris 2.8 (sets both xmit and rcv windows)\n\
	-p##	port number to send to|listen at (default 5001)\n\
	-P##	port number for control connection (default 5000)\n\
	-u	use UDP instead of TCP\n\
	-m##	use multicast with specified TTL instead of unicast (UDP)\n\
	-M##	MSS for data connection (TCP)\n\
	-N##	number of streams (starting at port number), implies -B\n\
	-R##	transmit rate limit in Kbps (or (m|M)bps or (g|G)bps or (p)ps)\n\
	-T##	transmit timeout in seconds (or (m|M)inutes or (h|H)ours)\n\
	-i##	receiver interval reporting in seconds (or (m|M)inutes)\n\
	-Ixxx	identifier for nuttcp output (max of 40 characters)\n\
	-F	flip option to reverse direction of data connection open\n\
	-a	retry failed server connection \"again\" for transient errors\n"
#ifdef HAVE_SETPRIO
"	-xP##	set nuttcp process priority (must be root)\n"
#endif
"	-d	set TCP SO_DEBUG option on data socket\n\
	-v[v]	verbose [or very verbose] output\n\
	-b	brief output (default)\n\
	-D	xmit only: don't buffer TCP writes (sets TCP_NODELAY sockopt)\n\
	-B	recv only: only output full blocks of size from -l## (for TAR)\n"
#ifdef IPV6_V6ONLY
"	--disable-v4-mapped disable v4 mapping in v6 server (default)\n"
"	--enable-v4-mapped enable v4 mapping in v6 server\n"
#endif
"Usage (server): nuttcp -S[f][P] [-options]\n\
		note server mode excludes use of -s\n\
		'f' suboption forces server mode (useful with rsh/ssh)\n\
		'P' suboption makes 3rd party {in,out}bound control ports same\n\
	-4	Use IPv4 (default)\n"
#ifdef AF_INET6
"	-6	Use IPv6\n"
#endif
"	-1	oneshot server mode (implied with inetd/xinetd), implies -S\n\
	-P##	port number for server connection (default 5000)\n\
		note don't use with inetd/xinetd (use services file instead)\n"
#ifdef HAVE_SETPRIO
"	-xP##	set nuttcp process priority (must be root)\n"
#endif
"	--idle-data-timeout <value|minimum/default/maximum>  (default: 5/30/60)\n"
"		     server timeout in seconds for idle data connection\n"
"	--no3rdparty don't allow 3rd party capability\n"
"	--nofork     don't fork server\n"
"	--single-threaded  make manually started server be single threaded\n"
#ifdef IPV6_V6ONLY
"	--disable-v4-mapped disable v4 mapping in v6 server (default)\n"
"	--enable-v4-mapped enable v4 mapping in v6 server\n"
#endif
"Format options:\n\
	-fxmitstats	also give transmitter stats (MB) with -i (UDP only)\n\
	-frunningtotal	also give cumulative stats on interval reports\n\
	-f-drops	don't give packet drop info on brief output (UDP)\n\
	-f-percentloss	don't give %%loss info on brief output (UDP)\n\
	-fparse		generate key=value parsable output\n\
";	

char stats[128];
char srvrbuf[4096];
char tmpbuf[257];
uint64_t nbytes = 0;		/* bytes on net */
int64_t pbytes = 0;		/* previous bytes - for interval reporting */
int64_t ntbytes = 0;		/* bytes sent by transmitter */
int64_t ptbytes = 0;		/* previous bytes sent by transmitter */
uint64_t ntbytesc = 0;		/* bytes sent by transmitter that have
				 * been counted */
uint64_t chk_nbytes = 0;	/* byte counter used to test if no more data
				 * being received by server (presumably because
				 * client transmitter went away */
int numCalls = 0;		/* # of NRead/NWrite calls. */
int nstream = 1;		/* number of streams */
int stream_idx = 0;		/* current stream */
int start_idx = 1;		/* set to use or bypass control channel */
int b_flag = 0;			/* use mread() */
int got_srvr_output = 0;	/* set when server output has been read */
int retry_server = 0;		/* set to retry control connect() to server */
int num_connect_tries = 0;	/* tracks attempted connects to server */
int single_threaded = 0;	/* set to make server single threaded */
double srvr_MB;
double srvr_realt;
double srvr_KBps;
double srvr_Mbps;
int srvr_cpu_util;

double cput = 0.000001, realt = 0.000001;	/* user, real time (seconds) */
double realtd = 0.000001;	/* real time delta - for interval reporting */

#ifdef SIGPIPE
void
sigpipe( int signum )
{
	signal(SIGPIPE, sigpipe);
}
#endif

void
sigint( int signum )
{
	signal(SIGINT, SIG_DFL);
	fputs("\n*** transfer interrupted ***\n", stdout);
	if (clientserver && client && !host3 && udp && !trans)
		shutdown(0, SHUT_WR);
	else
		intr = 1;
	done++;
	return;
}

void
ignore_alarm( int signum )
{
	return;
}

void
sigalarm( int signum )
{
	struct	timeval timec;	/* Current time */
	struct	timeval timed;	/* Delta time */
/*	beginnings of timestamps - not ready for prime time */
/*	struct	timeval timet; */	/* Transmitter time */
	int64_t nrbytes;
	uint64_t deltarbytes, deltatbytes;
	double fractloss;
	int nodata;
/*	int normal_eod;							*/
	int i;
	char *cp1, *cp2;
	short save_events;
	long flags, saveflags;

	if (host3 && clientserver && !client)
		return;

	if (interval && !trans) {
		/* Get real time */
		gettimeofday(&timec, (struct timezone *)0);
		tvsub( &timed, &timec, &timep );
		realtd = timed.tv_sec + ((double)timed.tv_usec) / 1000000;
		if( realtd <= 0.0 )  realtd = 0.000001;
		tvsub( &timed, &timec, &time0 );
		realt = timed.tv_sec + ((double)timed.tv_usec)
						    / 1000000;
		if( realt <= 0.0 )  realt = 0.000001;
	}

	if (clientserver && !client && !trans) {
		struct sockaddr_in peer;
		socklen_t peerlen = sizeof(peer);

		nodata = 0;
/*		normal_eod = 0;						*/

		if (getpeername(fd[0], (struct sockaddr *)&peer, &peerlen) < 0)
			nodata = 1;

		if (udp && got_begin) {
			/* checks if client did a shutdown() for writing
			 * on the control connection */
			pollfds[0].fd = fileno(ctlconn);
			save_events = pollfds[0].events;
			pollfds[0].events = POLLIN | POLLPRI;
			pollfds[0].revents = 0;
			if ((poll(pollfds, 1, 0) > 0) &&
			    (pollfds[0].revents & (POLLIN | POLLPRI))) {
				nodata = 1;
/*				normal_eod = 1;				*/
			}
			pollfds[0].events = save_events;
		}

		if (interval) {
			chk_interval += realtd;
			if (chk_interval >= chk_idle_data) {
				chk_interval = 0;
				if ((nbytes - chk_nbytes) == 0)
					nodata = 1;
				chk_nbytes = nbytes;
			}
		}
		else {
			if ((nbytes - chk_nbytes) == 0)
				nodata = 1;
			chk_nbytes = nbytes;
		}

		if (nodata) {
			/* Don't just exit anymore so can get partial results
			 * (shouldn't be a problem but keep an eye out that
			 * servers don't start hanging again) */
/*			following code untested after recent changes	*/
/*			if ((inetd  || (!nofork && !single_threaded))	*/
/*					&& !normal_eod)			*/
/*				exit(1);				*/
			if (udp && !interval && handle_urg) {
				/* send 'A' for ABORT as urgent TCP data
				 * on control connection (don't block) */
				saveflags = fcntl(fd[0], F_GETFL, 0);
				if (saveflags != -1) {
					flags = saveflags | O_NONBLOCK;
					fcntl(fd[0], F_SETFL, flags);
				}
				send(fd[0], "A", 1, MSG_OOB);
				if (saveflags != -1) {
					flags = saveflags;
					fcntl(fd[0], F_SETFL, flags);
				}
			}
			for ( i = 1; i <= nstream; i++ )
				close(fd[i]);
			intr = 1;
			return;
		}

		if (!interval)
			return;
	}

	if (interval && !trans) {
		if ((udp && !got_begin) || done) {
			timep.tv_sec = timec.tv_sec;
			timep.tv_usec = timec.tv_usec;
			return;
		}
		if (clientserver) {
			nrbytes = nbytes;
			if (udplossinfo) {
				ntbytes = *(int64_t *)(buf + 24);
				if (need_swap) {
					cp1 = (char *)&ntbytes;
					cp2 = buf + 31;
					for ( i = 0; i < 8; i++ )
						*cp1++ = *cp2--;
				}
				if (ntbytes > ntbytesc)
					/* received bytes not counted yet */
					nrbytes += buflen;
				if ((nrbytes > ntbytes) ||
				    ((nrbytes - pbytes) > (ntbytes - ptbytes)))
					/* yes they were counted */
					nrbytes -= buflen;
			}
			if (*ident)
				fprintf(stdout, "%s: ", ident + 1);
			if (format & PARSE)
				strcpy(fmt, P_PERF_FMT_INTERVAL);
			else
				strcpy(fmt, PERF_FMT_INTERVAL);
			fprintf(stdout, fmt,
				(double)(nrbytes - pbytes)/(1024*1024), realtd,
				(double)(nrbytes - pbytes)/realtd/125000);
			if (udplossinfo) {
				if (!(format & NODROPS)) {
					if (format & PARSE)
						strcpy(fmt,
						       P_DROP_FMT_INTERVAL);
					else
						strcpy(fmt, DROP_FMT_INTERVAL);
					fprintf(stdout, fmt,
						((ntbytes - ptbytes)
							- (nrbytes - pbytes))
								/buflen,
						(ntbytes - ptbytes)/buflen);
				}
				if (!(format & NOPERCENTLOSS)) {
					deltarbytes = nrbytes - pbytes;
					deltatbytes = ntbytes - ptbytes;
					fractloss = (deltatbytes ?
						1.0 -
						    (double)deltarbytes
							/(double)deltatbytes :
						0.0);
					if (format & PARSE)
						strcpy(fmt,
						       P_LOSS_FMT_INTERVAL);
					else if ((fractloss != 0.0) &&
						 (fractloss < 0.001))
						strcpy(fmt,
							LOSS_FMT_INTERVAL5);
					else
						strcpy(fmt, LOSS_FMT_INTERVAL);
					fprintf(stdout, fmt, fractloss * 100);
				}
			}
			if (format & RUNNINGTOTAL) {
				if (format & PARSE)
					strcpy(fmt, P_PERF_FMT_INTERVAL2);
				else
					strcpy(fmt, PERF_FMT_INTERVAL2);
				fprintf(stdout, fmt,
					(double)nrbytes/(1024*1024), realt,
					(double)nrbytes/realt/125000 );
				if (udplossinfo) {
					if (!(format & NODROPS)) {
						if (format & PARSE)
							strcpy(fmt,
							  P_DROP_FMT_INTERVAL);
						else
							strcpy(fmt,
							  DROP_FMT_INTERVAL);
						fprintf(stdout, fmt,
							(ntbytes - nrbytes)
								/buflen,
							ntbytes/buflen);
					}
					if (!(format & NOPERCENTLOSS)) {
						fractloss = (ntbytes ?
							1.0 -
							    (double)nrbytes
							      /(double)ntbytes :
							0.0);
						if (format & PARSE)
							strcpy(fmt,
							  P_LOSS_FMT_INTERVAL);
						else if ((fractloss != 0.0) &&
							 (fractloss < 0.001))
							strcpy(fmt,
							  LOSS_FMT_INTERVAL5);
						else
							strcpy(fmt,
							  LOSS_FMT_INTERVAL);
						fprintf(stdout, fmt,
							fractloss * 100);
					}
				}
			}
			if (udplossinfo && (format & XMITSTATS)) {
				if (format & PARSE)
					strcpy(fmt, P_PERF_FMT_INTERVAL3);
				else
					strcpy(fmt, PERF_FMT_INTERVAL3);
				fprintf(stdout, fmt,
					(double)(ntbytes - ptbytes)/1024/1024);
				if (format & RUNNINGTOTAL) {
					if (format & PARSE)
						strcpy(fmt,
						       P_PERF_FMT_INTERVAL4);
					else
						strcpy(fmt, PERF_FMT_INTERVAL4);
					fprintf(stdout, fmt,
						(double)ntbytes/1024/1024);
					if (format & DEBUGINTERVAL)
						fprintf(stdout, " Pre: %.4f MB",
							(double)ntbytesc
								  /1024/1024);
				}
			}
			fprintf(stdout, "\n");
			fflush(stdout);
/*			beginnings of timestamps - not ready for prime time */
/*			bcopy(buf + 8, &timet.tv_sec, 4);		*/
/*			bcopy(buf + 12, &timet.tv_usec, 4);		*/
/*			tvsub( &timed, &timec, &timet );		*/
/*			realt = timed.tv_sec + ((double)timed.tv_usec)	*/
/*							    / 1000000;	*/
/*			if( realt <= 0.0 )  realt = 0.000001;		*/
/*			fprintf(stdout, "%.3f ms-OWD timet = %08X/%08X timec = %08X/%08X\n", */
/*				realt*1000, timet.tv_sec, timet.tv_usec, */
/*				timec.tv_sec, timec.tv_usec);		*/
/*			fprintf(stdout, "%.3f ms-OWD\n", realt*1000);	*/
/*			fflush(stdout);					*/
			timep.tv_sec = timec.tv_sec;
			timep.tv_usec = timec.tv_usec;
			pbytes = nrbytes;
			ptbytes = ntbytes;
		}
	}
	else
		intr = 1;
	return;
}

int
main( int argc, char **argv )
{
	double MB;
	double rate_opt;
	double fractloss;
	int cpu_util;
	int first_read;
	int ocorrection = 0;
	double  correction = 0.0;
	int pollst = 0;
	int i, j;
	char *cp1, *cp2;
	char ch;
	int error_num = 0;
	int sockopterr = 0;
	int save_errno;
	struct servent *sp = 0;
	struct addrinfo hints, *res[MAXSTREAM + 1] = { NULL };
	struct timeval time_eod;	/* time EOD packet was received */
	struct timeval timepkrcv;	/* time last data packet received */
	struct timeval timed;		/* time delta */
	short save_events;
	int skiparg;
	int reqval;
	double idle_data_min = IDLE_DATA_MIN;
	double idle_data_max = IDLE_DATA_MAX;
	double default_idle_data = DEFAULT_IDLE_DATA;

	sendwin = 0;
	rcvwin = 0;
	srvrwin = -1;

	if (argc < 2) goto usage;

	argv++; argc--;
	while( argc>0 && argv[0][0] == '-' )  {
		skiparg = 0;
		switch (argv[0][1]) {

		case '4':
			domain = PF_INET;
			af = AF_INET;
			explicitaf = 1;
			break;
#ifdef AF_INET6
		case '6':
			domain = PF_INET6;
			af = AF_INET6;
			explicitaf = 1;
			break;
#endif
		case 'B':
			b_flag = 1;
			break;
		case 't':
			trans = 1;
			break;
		case 'r':
			trans = 0;
			break;
		case 'd':
			options |= SO_DEBUG;
			break;
		case 'D':
			nodelay = 1;
			break;
		case 'n':
			reqval = 0;
			if (argv[0][2] == 'b') {
				fprintf(stderr, "option \"-nb\" no longer supported, use \"-n###[k|m|g|t|p]\" instead\n");
				fflush(stderr);
				exit(1);
			}
			cp1 = getoptvalp(argv, 2, reqval, &skiparg);
			nbuf = strtoull(cp1, NULL, 0);
			if (nbuf == 0) {
				if (errno == EINVAL) {
					fprintf(stderr, "invalid nbuf = %s\n",
						&argv[0][2]);
					fflush(stderr);
					exit(1);
				}
				else {
					nbuf = DEFAULT_NBUF;
					break;
				}
			}
			if (*cp1)
				ch = *(cp1 + strlen(cp1) - 1);
			else
				ch = '\0';
			if ((ch == 'b') || (ch == 'B'))
				nbuf_bytes = 1;
			else if ((ch == 'k') || (ch == 'K')) {
				nbuf *= 1024;
				nbuf_bytes = 1;
			}
			else if ((ch == 'm') || (ch == 'M')) {
				nbuf *= 1048576;
				nbuf_bytes = 1;
			}
			else if ((ch == 'g') || (ch == 'G')) {
				nbuf *= 1073741824;
				nbuf_bytes = 1;
			}
			else if ((ch == 't') || (ch == 'T')) {
				nbuf *= 1099511627776ull;
				nbuf_bytes = 1;
			}
			else if ((ch == 'p') || (ch == 'P')) {
				nbuf *= 1125899906842624ull;
				nbuf_bytes = 1;
			}
			break;
		case 'l':
			reqval = 0;
			cp1 = getoptvalp(argv, 2, reqval, &skiparg);
			buflen = atoi(cp1);
			buflenopt = 1;
			if (buflen < 1) {
				fprintf(stderr, "invalid buflen = %d\n", buflen);
				fflush(stderr);
				exit(1);
			}
			if (*cp1)
				ch = *(cp1 + strlen(cp1) - 1);
			else
				ch = '\0';
			if ((ch == 'k') || (ch == 'K'))
				buflen *= 1024;
			else if ((ch == 'm') || (ch == 'M'))
				buflen *= 1048576;
			break;
		case 'w':
			reqval = 0;
			if (argv[0][2] == 's') {
				cp1 = getoptvalp(argv, 3, reqval, &skiparg);
				srvrwin = atoi(cp1);
				if (*cp1)
					ch = *(cp1 + strlen(cp1) - 1);
				else
					ch = '\0';
				if ((ch == 'k') || (ch == 'K'))
					srvrwin *= 1024;
				else if ((ch == 'm') || (ch == 'M'))
					srvrwin *= 1048576;
				else if ((ch == 'g') || (ch == 'G'))
					srvrwin *= 1073741824;
				else if ((ch != 'b') && (ch != 'B'))
					srvrwin *= 1024;
				if (srvrwin < 0) {
					fprintf(stderr, "invalid srvrwin = %d\n", srvrwin);
					fflush(stderr);
					exit(1);
				}
			}
			else {
				if (argv[0][2] == 'b') {
					braindead = 1;
					cp1 = getoptvalp(argv, 3, reqval,
							 &skiparg);
					if (*cp1 == '\0')
						break;
					sendwin = atoi(cp1);
				}
				else {
					cp1 = getoptvalp(argv, 2, reqval,
							 &skiparg);
					sendwin = atoi(cp1);
				}

				if (*cp1)
					ch = *(cp1 + strlen(cp1) - 1);
				else
					ch = '\0';
				if ((ch == 'k') || (ch == 'K'))
					sendwin *= 1024;
				else if ((ch == 'm') || (ch == 'M'))
					sendwin *= 1048576;
				else if ((ch == 'g') || (ch == 'G'))
					sendwin *= 1073741824;
				else if ((ch != 'b') && (ch != 'B'))
					sendwin *= 1024;
				rcvwin = sendwin;
				if (sendwin < 0) {
					fprintf(stderr, "invalid sendwin = %d\n", sendwin);
					fflush(stderr);
					exit(1);
				}
			}
			if (srvrwin == -1) {
				srvrwin = sendwin;
			}
			break;
		case 's':
			sinkmode = 0;	/* sink/source data */
			break;
		case 'p':
			reqval = 0;
			tmpport = atoi(getoptvalp(argv, 2, reqval, &skiparg));
			if ((tmpport < 5001) || (tmpport > 65535)) {
				fprintf(stderr, "invalid port = %d\n", tmpport);
				fflush(stderr);
				exit(1);
			}
			port = tmpport;
			break;
		case 'P':
			reqval = 0;
			tmpport = atoi(getoptvalp(argv, 2, reqval, &skiparg));
			if ((tmpport < 5000) || (tmpport > 65535)) {
				fprintf(stderr, "invalid ctlport = %d\n", tmpport);
				fflush(stderr);
				exit(1);
			}
			ctlport = tmpport;
			break;
		case 'u':
			udp = 1;
			if (!buflenopt) buflen = DEFAULTUDPBUFLEN;
			if (argv[0][2] == 'u') {
				haverateopt = 1;
				rate = MAXRATE;
			}
			break;
		case 'v':
			brief = 0;
			if (argv[0][2] == 'v')
				verbose = 1;
			break;
		case 'N':
			reqval = 0;
			nstream = atoi(getoptvalp(argv, 2, reqval, &skiparg));
			if (nstream < 1) {
				fprintf(stderr, "invalid nstream = %d\n", nstream);
				fflush(stderr);
				exit(1);
			}
			if (nstream > MAXSTREAM) {
				fprintf(stderr, "nstream = %d > MAXSTREAM, set to %d\n",
				    nstream, MAXSTREAM);
				nstream = MAXSTREAM;
			}
			if (nstream > 1) b_flag = 1;
			break;
		case 'R':
			reqval = 0;
			haverateopt = 1;
			if (argv[0][2] == 'i') {
				cp1 = getoptvalp(argv, 3, reqval, &skiparg);
				sscanf(cp1, "%lf", &rate_opt);
				irate = 1;
			}
			else if (argv[0][2] == 'u') {
				rate_opt = 0.0;
				cp1 = &argv[0][3];
			}
			else {
				cp1 = getoptvalp(argv, 2, reqval, &skiparg);
				sscanf(cp1, "%lf", &rate_opt);
			}
			if (*cp1)
				ch = *(cp1 + strlen(cp1) - 1);
			else
				ch = '\0';
			if ((ch == 'm') || (ch == 'M'))
				rate_opt *= 1000;
			else if ((ch == 'g') || (ch == 'G'))
				rate_opt *= 1000000;
			else if (ch == 'p') {
				rate_pps = 1;
				if (strlen(cp1) >= 2) {
					ch = *(cp1 + strlen(cp1) - 2);
					if ((ch == 'k') || (ch == 'K'))
						rate_opt *= 1000;
					if ((ch == 'm') || (ch == 'M'))
						rate_opt *= 1000000;
				}
			}
			rate = rate_opt;
			if (rate == 0)
				rate = MAXRATE;
			break;
		case 'T':
			reqval = 0;
			cp1 = getoptvalp(argv, 2, reqval, &skiparg);
			sscanf(cp1, "%lf", &timeout);
			if (timeout < 0) {
				fprintf(stderr, "invalid timeout = %f\n", timeout);
				fflush(stderr);
				exit(1);
			}
			else if (timeout == 0.0)
				timeout = DEFAULT_TIMEOUT;
			if (*cp1)
				ch = *(cp1 + strlen(cp1) - 1);
			else
				ch = '\0';
			if ((ch == 'm') || (ch == 'M'))
				timeout *= 60.0;
			else if ((ch == 'h') || (ch == 'H'))
				timeout *= 3600.0;
			else if ((ch == 'd') || (ch == 'D'))
				timeout *= 86400.0;
			itimer.it_value.tv_sec = timeout;
			itimer.it_value.tv_usec =
				(timeout - itimer.it_value.tv_sec)*1000000;
			if (timeout && !nbuf)
				nbuf = INT_MAX;
			break;
		case 'i':
			reqval = 0;
			cp1 = getoptvalp(argv, 2, reqval, &skiparg);
			sscanf(cp1, "%lf", &interval);
			if (interval < 0.0) {
				fprintf(stderr, "invalid interval = %f\n", interval);
				fflush(stderr);
				exit(1);
			}
			else if (interval == 0.0) 
				interval = 1.0;
			if (*cp1)
				ch = *(cp1 + strlen(cp1) - 1);
			else
				ch = '\0';
			if ((ch == 'm') || (ch == 'M'))
				interval *= 60.0;
			else if ((ch == 'h') || (ch == 'H'))
				interval *= 3600.0;
			break;
		case 'I':
			reqval = 1;
			ident[0] = '-';
			strncpy(&ident[1],
				getoptvalp(argv, 2, reqval, &skiparg), 40);
			ident[41] = '\0';
			break;
		case 'F':
			reverse = 1;
			break;
		case 'b':
			reqval = 0;
			cp1 = getoptvalp(argv, 2, reqval, &skiparg);
			if (*cp1)
				brief = atoi(cp1);
			else
				brief = 1;
			break;
		case 'S':
			if (strchr(&argv[0][2], 'f'))
				force_server = 1;
			if (strchr(&argv[0][2], 'P'))
				pass_ctlport = 1;
			trans = 0;
			clientserver = 1;
			brief = 0;
			verbose = 1;
			break;
		case '1':
			oneshot = 1;
			trans = 0;
			clientserver = 1;
			brief = 0;
			verbose = 1;
			break;
		case 'V':
			fprintf(stdout, "nuttcp-%d.%d.%d\n", vers_major,
					vers_minor, vers_delta);
			exit(0);
		case 'f':
			if (strcmp(&argv[0][2], "xmitstats") == 0)
				format |= XMITSTATS;
			else if (strcmp(&argv[0][2], "debuginterval") == 0)
				format |= DEBUGINTERVAL;
			else if (strcmp(&argv[0][2], "runningtotal") == 0)
				format |= RUNNINGTOTAL;
			else if (strcmp(&argv[0][2], "-percentloss") == 0)
				format |= NOPERCENTLOSS;
			else if (strcmp(&argv[0][2], "-drops") == 0)
				format |= NODROPS;
			else if (strcmp(&argv[0][2], "debugpoll") == 0)
				format |= DEBUGPOLL;
			else if (strcmp(&argv[0][2], "debugmtu") == 0)
				format |= DEBUGMTU;
			else if (strcmp(&argv[0][2], "parse") == 0)
				format |= PARSE;
			else {
				if (argv[0][2]) {
					fprintf(stderr, "invalid format option \"%s\"\n", &argv[0][2]);
					fflush(stderr);
					exit(1);
				}
				else {
					fprintf(stderr, "invalid null format option\n");
					fprintf(stderr, "perhaps the \"-F\" flip option was intended\n");
					fflush(stderr);
					exit(1);
				}
			}
			break;
		case 'x':
			reqval = 0;
			if (argv[0][2] == 't') {
				traceroute = 1;
				brief = 1;
			}
#ifdef HAVE_SETPRIO
			else if (argv[0][2] == 'P')
				priority = atoi(getoptvalp(argv, 3, reqval,
						&skiparg));
#endif
			else {
				if (argv[0][2]) {
					fprintf(stderr, "invalid x option \"%s\"\n", &argv[0][2]);
					fflush(stderr);
					exit(1);
				}
				else {
					fprintf(stderr, "invalid null x option\n");
					fflush(stderr);
					exit(1);
				}
			}
			break;
		case '3':
			thirdparty = 1;
			break;
		case 'm':
			reqval = 0;
			cp1 = getoptvalp(argv, 2, reqval, &skiparg);
			if (*cp1)
				mc_param = atoi(cp1);
			else
				mc_param = 1;
			if ((mc_param < 1) || (mc_param > 255)) {
				fprintf(stderr, "invalid multicast ttl = %d\n", mc_param);
				fflush(stderr);
				exit(1);
			}
			multicast = mc_param;
			break;
		case 'M':
			reqval = 1;
			datamss = atoi(getoptvalp(argv, 2, reqval, &skiparg));
			if (datamss < 0) {
				fprintf(stderr, "invalid datamss = %d\n", datamss);
				fflush(stderr);
				exit(1);
			}
			break;
		case 'c':
			reqval = 1;
			cp1 = getoptvalp(argv, 2, reqval, &skiparg);
			tos = strtol(cp1, NULL, 0);
			if (*cp1)
				ch = *(cp1 + strlen(cp1) - 1);
			else
				ch = '\0';
			if ((ch == 'p') || (ch == 'P')) {
				/* Precedence */
				if (tos > 7) {
					fprintf(stderr, "invalid precedence = %d\n", tos);
					fflush(stderr);
					exit(1);
				}
				tos <<= 5;
			}
			else if ((ch != 't') && (ch != 'T')) {
				/* DSCP */
				if (tos > 63) {
					fprintf(stderr, "invalid dscp = %d\n", tos);
					fflush(stderr);
					exit(1);
				}
				tos <<= 2;
			}
			if (tos > 255) {
				fprintf(stderr, "invalid tos = %d\n", tos);
				fflush(stderr);
				exit(1);
			}
			break;
		case 'a':
			retry_server = 1;
			break;
		case '-':
			if (strcmp(&argv[0][2], "nofork") == 0) {
				nofork=1;
			}
			else if (strcmp(&argv[0][2], "no3rdparty") == 0) {
				no3rd=1;
			}
			else if (strcmp(&argv[0][2],
				 "idle-data-timeout") == 0) {
				if ((cp1 = strchr(argv[1], '/'))) {
					if (strchr(cp1 + 1, '/')) {
						if (sscanf(argv[1],
							"%lf/%lf/%lf",
							&idle_data_min,
							&default_idle_data,
							&idle_data_max) != 3) {
							fprintf(stderr, "error scanning idle-data-timeout parameter = %s\n", argv[1]);
							fflush(stderr);
							exit(1);
						}
						if (idle_data_min <= 0.0) {
							fprintf(stderr, "invalid value for idle-data-timeout minimum = %f\n", idle_data_min);
							fflush(stderr);
							exit(1);
						}
						if (default_idle_data <= 0.0) {
							fprintf(stderr, "invalid value for idle-data-timeout default = %f\n", default_idle_data);
							fflush(stderr);
							exit(1);
						}
						if (idle_data_max <= 0.0) {
							fprintf(stderr, "invalid value for idle-data-timeout maximum = %f\n", idle_data_max);
							fflush(stderr);
							exit(1);
						}
						if (idle_data_max <
							idle_data_min) {
							fprintf(stderr, "error: idle-data-timeout maximum of %f < minimum of %f\n", idle_data_max, idle_data_min);
							fflush(stderr);
							exit(1);
						}
					}
					else {
						fprintf(stderr, "invalid idle-data-timeout parameter = %s\n", argv[1]);
						fflush(stderr);
						exit(1);
					}
				}
				else {
					sscanf(argv[1], "%lf", &idle_data_min);
					if (idle_data_min <= 0.0) {
						fprintf(stderr, "invalid value for idle-data-timeout = %f\n", idle_data_min);
						fflush(stderr);
						exit(1);
					}
					idle_data_max = idle_data_min;
					default_idle_data = idle_data_min;
				}
				argv++;
				argc--;
			}
			else if (strcmp(&argv[0][2], "single-threaded") == 0) {
				single_threaded=1;
			}
#ifdef IPV6_V6ONLY
			else if (strcmp(&argv[0][2], "disable-v4-mapped") == 0) {
				v4mapped=0;
			}
			else if (strcmp(&argv[0][2], "enable-v4-mapped") == 0) {
				v4mapped=1;
			}
#endif
			else {
				goto usage;
			}
			break;
		case 'h':
		default:
			goto usage;
		}
		argv++;
		argc--;
		if (skiparg) {
			argv++;
			argc--;
		}
	}

	if (argc > 2) goto usage;
	if (trans && (argc < 1)) goto usage;
	if (clientserver && (argc != 0)) goto usage;

	host3 = NULL;
	if (argc == 2) {
		host3 = argv[1];
		if (strlen(host3) > HOSTNAMELEN) {
			fprintf(stderr, "3rd party host '%s' too long\n", host3);
			fflush(stderr);
			exit(1);
		}
		cp1 = host3;
		while (*cp1) {
			if (!isalnum((int)(*cp1)) && (*cp1 != '-') && (*cp1 != '.')
					   && (*cp1 != ':') && (*cp1 != '/')) {
				fprintf(stderr, "invalid 3rd party host '%s'\n", host3);
				fflush(stderr);
				exit(1);
			}
			cp1++;
		}
	}

	if (multicast) {
		udp = 1;
		if (!buflenopt) buflen = DEFAULT_MC_UDPBUFLEN;
		nstream = 1;
	}

	if (udp && !haverateopt)
		rate = DEFAULT_UDP_RATE;

	bzero((char *)&frominet, sizeof(frominet));
	bzero((char *)&clientaddr, sizeof(clientaddr));

#ifdef AF_INET6
	bzero((char *)&clientaddr6, sizeof(clientaddr6));
	clientscope6 = 0;
#endif

	if (!nbuf) {
		if (timeout == 0.0) {
			timeout = DEFAULT_TIMEOUT;
			itimer.it_value.tv_sec = timeout;
			itimer.it_value.tv_usec =
				(timeout - itimer.it_value.tv_sec)*1000000;
			nbuf = INT_MAX;
		}
	}

	if (srvrwin == -1) {
		srvrwin = sendwin;
	}

	if ((argc == 0) && !explicitaf) {
		domain = PF_INET;
		af = AF_INET;
	}

	if (argc >= 1) {
		host = argv[0];
		stream_idx = 0;
		res[0] = NULL;
		cp1 = host;
		if (host[strlen(host) - 1] == '/') {
			fprintf(stderr, "bad hostname or address: trailing '/' not allowed: %s\n", host);
			fflush(stderr);
			exit(1);
		}
		if (strchr(host, '/') && !trans && !reverse) {
			fprintf(stderr, "multiple control/data paths not supported for receive\n");
			fflush(stderr);
			exit(1);
		}
		if (strchr(host, '/') && trans && reverse) {
			fprintf(stderr, "multiple control/data paths not supported for flipped transmit\n");
			fflush(stderr);
			exit(1);
		}
		if (host[0] == '/') {
			host++;
			cp1++;
			stream_idx = 1;
		}
		else if ((cp2 = strchr(host, '/'))) {
			host = cp2 + 1;
		}

		while (stream_idx <= nstream) {
			bzero(&hints, sizeof(hints));
			res[stream_idx] = NULL;
			if (explicitaf) hints.ai_family = af;
			if ((cp2 = strchr(cp1, '/'))) {
				if (stream_idx == nstream) {
					fprintf(stderr, "bad hostname or address: too many data paths for nstream=%d: %s\n", nstream, argv[0]);
					fflush(stderr);
					exit(1);
				}
				*cp2 = '\0';
			}
			if ((error_num = getaddrinfo(cp1, NULL, &hints, &res[stream_idx]))) {
				if (cp2)
					*cp2++ = '/';
				fprintf(stderr, "bad hostname or address: %s: %s\n", gai_strerror(error_num), argv[0]);
				fflush(stderr);
				exit(1);
			}
			af = res[stream_idx]->ai_family;
/*
 * At the moment PF_ matches AF_ but are maintained seperate and the socket
 * call is supposed to be PF_
 *
 * For now we set domain from the address family we looked up, but if these
 * ever get changed to not match some code will have to go here to find the
 * domain appropriate for the family
 */
			domain = af;
			stream_idx++;
			if (cp2) {
				*cp2++ = '/';
				cp1 = cp2;
			}
			else
				cp1 = host;
		}
		if (!res[0]) {
			if ((cp1 = strchr(host, '/')))
				*cp1 = '\0';
			if ((error_num = getaddrinfo(host, NULL, &hints, &res[0]))) {
				if (cp1)
					*cp1++ = '/';
				fprintf(stderr, "bad hostname or address: %s: %s\n", gai_strerror(error_num), argv[0]);
				fflush(stderr);
				exit(1);
			}
			af = res[0]->ai_family;
			/* see previous comment about domain */
			domain = af;
			if (cp1)
				*cp1 = '/';
		}
	}

#ifdef AF_INET6
	if (multicast && (af == AF_INET6)) {
		fprintf(stderr, "multicast not yet supported for IPv6\n");
		fflush(stderr);
		exit(1);
	}
#endif

	if (!port) {
		if (af == AF_INET) {
			if ((sp = getservbyname( "nuttcp-data", "tcp" )))
				port = ntohs(sp->s_port);
			else
				port = DEFAULT_PORT;
		}
#ifdef AF_INET6
		else if (af == AF_INET6) {
			if ((sp = getservbyname( "nuttcp6-data", "tcp" )))
				port = ntohs(sp->s_port);
			else {
				if ((sp = getservbyname( "nuttcp-data", "tcp" )))
					port = ntohs(sp->s_port);
				else
					port = DEFAULT_PORT;
			}
		}
#endif
		else {
			err("unsupported AF");
		}
	}

	if (!ctlport) {
		if (af == AF_INET) {
			if ((sp = getservbyname( "nuttcp", "tcp" )))
				ctlport = ntohs(sp->s_port);
			else
				ctlport = DEFAULT_CTLPORT;
		}
#ifdef AF_INET6
		else if (af == AF_INET6) {
			if ((sp = getservbyname( "nuttcp6", "tcp" )))
				ctlport = ntohs(sp->s_port);
			else {
				if ((sp = getservbyname( "nuttcp", "tcp" )))
					ctlport = ntohs(sp->s_port);
				else
					ctlport = DEFAULT_CTLPORT;
			}
		}
#endif
		else {
			err("unsupported AF");
		}
	}

	if ((port < 5000) || ((port + nstream - 1) > 65535)) {
		fprintf(stderr, "invalid port/nstream = %d/%d\n", port, nstream);
		fflush(stderr);
		exit(1);
	}

	if ((ctlport >= port) && (ctlport <= (port + nstream - 1))) {
		fprintf(stderr, "ctlport = %d overlaps port/nstream = %d/%d\n", ctlport, port, nstream);
		fflush(stderr);
		exit(1);
	}

	if (timeout && (interval >= timeout)) {
		fprintf(stderr, "ignoring interval=%f which is greater than or equal timeout=%f\n", interval, timeout);
		fflush(stderr);
		interval = 0;
	}

	if (clientserver) {
		if (trans) {
			fprintf(stderr, "server mode only allowed for receiver\n");
			goto usage;
		}
		udp = 0;
		sinkmode = 1;
		start_idx = 0;
		ident[0] = '\0';
		if (force_server) {
			close(0);
			close(1);
			close(2);
			open("/dev/null", O_RDWR);
			dup(0);
			dup(0);
		}
		if (af == AF_INET) {
		  struct sockaddr_in peer;
		  socklen_t peerlen = sizeof(peer);
		  if (!force_server && getpeername(0, (struct sockaddr *) &peer,
				&peerlen) == 0) {
			clientaddr = peer.sin_addr;
			inetd = 1;
			oneshot = 1;
			start_idx = 1;
		  }
		}
#ifdef AF_INET6
		else if (af == AF_INET6) {
		  struct sockaddr_in6 peer;
		  socklen_t peerlen = sizeof(peer);
		  if (!force_server && getpeername(0, (struct sockaddr *) &peer,
				&peerlen) == 0) {
			clientaddr6 = peer.sin6_addr;
			clientscope6 = peer.sin6_scope_id;
			inetd = 1;
			oneshot = 1;
			start_idx = 1;
		  }
		}
#endif
		else {
			err("unsupported AF");
		}
	}

	if (clientserver && !inetd && !nofork) {
		if ((pid = fork()) == (pid_t)-1)
			err("can't fork");
		if (pid != 0)
			exit(0);
	}

#ifdef HAVE_SETPRIO
	if (priority) {
		if (setpriority(PRIO_PROCESS, 0, priority) != 0)
			err("couldn't change priority");
	}
#endif

	if (argc >= 1) {
		start_idx = 0;
		client = 1;
		clientserver = 1;
	}

	if (interval && !clientserver) {
		fprintf(stderr, "interval option only supported for client/server mode\n");
		fflush(stderr);
		exit(1);
	}
	if (reverse && !clientserver) {
		fprintf(stderr, "flip option only supported for client/server mode\n");
		fflush(stderr);
		exit(1);
	}
	if (reverse && udp) {
		fprintf(stderr, "flip option not supported for UDP\n");
		fflush(stderr);
		exit(1);
	}
	if (traceroute) {
		nstream = 1;
		if (!clientserver) {
			fprintf(stderr, "traceroute option only supported for client/server mode\n");
			fflush(stderr);
			exit(1);
		}
	}
	if (host3) {
		if (!clientserver) {
			fprintf(stderr, "3rd party nuttcp only supported for client/server mode\n");
			fflush(stderr);
			exit(1);
		}
	}

	if (udp && (buflen < 5)) {
	    fprintf(stderr, "UDP buflen = %d < 5, set to 5\n", buflen);
	    buflen = 5;		/* send more than the sentinel size */
	}

	if (udp && (buflen > MAXUDPBUFLEN)) {
	    fprintf(stderr, "UDP buflen = %d > MAXUDPBUFLEN, set to %d\n",
		buflen, MAXUDPBUFLEN);
	    buflen = MAXUDPBUFLEN;
	}

	if (nbuf_bytes && !host3 && !traceroute) {
		nbuf /= buflen;
	}

	if ((rate != MAXRATE) && rate_pps && !host3 && !traceroute) {
		uint64_t llrate = rate;

		llrate *= ((double)buflen * 8 / 1000);
		rate = llrate;
	}

	if (udp && interval) {
		if (buflen >= 32)
			udplossinfo = 1;
		else
			fprintf(stderr, "Unable to print interval loss information if UDP buflen < 32\n");
	}

	ivers = vers_major*10000 + vers_minor*100 + vers_delta;

	mallocsize = buflen;
	if (mallocsize < MINMALLOC) mallocsize = MINMALLOC;
	if( (buf = (char *)malloc(mallocsize)) == (char *)NULL)
		err("malloc");

	pattern( buf, buflen );

#ifdef SIGPIPE
	signal(SIGPIPE, sigpipe);
#endif

	signal(SIGINT, sigint);

doit:
	for ( stream_idx = start_idx; stream_idx <= nstream; stream_idx++ ) {
		if (clientserver && (stream_idx == 1)) {
			if (client) {
				if (udp && !host3 && !traceroute) {
					ctlconnmss = 0;
					optlen = sizeof(ctlconnmss);
					if (getsockopt(fd[0], IPPROTO_TCP, TCP_MAXSEG,  (void *)&ctlconnmss, &optlen) < 0)
						err("get ctlconn maximum segment size didn't work");
					if (!ctlconnmss) {
						ctlconnmss = NON_JUMBO_ETHER_MSS;
						if (format & DEBUGMTU) {
							fprintf(stderr, "nuttcp%s%s: Warning: Control connection MSS reported as 0, using %d\n", trans?"-t":"-r", ident, ctlconnmss);
							fflush(stderr);
						}
					}
					else if (format & DEBUGMTU)
						fprintf(stderr, "ctlconnmss = %d\n", ctlconnmss);
					if (buflenopt) {
						if (buflen > ctlconnmss) {
							if (format & PARSE)
								fprintf(stderr, "nuttcp%s%s: Warning=\"IP_frags_or_no_data_reception_since_buflen=%d_>_ctlconnmss=%d\"\n", trans?"-t":"-r", ident, buflen, ctlconnmss);
							else
								fprintf(stderr, "nuttcp%s%s: Warning: IP frags or no data reception since buflen=%d > ctlconnmss=%d\n", trans?"-t":"-r", ident, buflen, ctlconnmss);
							fflush(stderr);
						}
					}
					else {
						while (buflen > ctlconnmss) {
							buflen >>= 1;
							if (nbuf_bytes)
								nbuf <<= 1;
							if ((rate != MAXRATE) &&
							    rate_pps)
								rate >>= 1;
						}
					}
					if (format & DEBUGMTU)
						fprintf(stderr, "buflen = %d\n", buflen);
				}
				if (!(ctlconn = fdopen(fd[0], "w")))
					err("fdopen: ctlconn for writing");
				close(0);
				dup(fd[0]);
				if (srvr_helo) {
					fprintf(ctlconn,
						HELO_FMT, vers_major,
						vers_minor, vers_delta);
					fflush(ctlconn);
					if (!fgets(buf, mallocsize, stdin)) {
						if ((errno == ECONNRESET) &&
						    (num_connect_tries <
							  MAX_CONNECT_TRIES) &&
						    retry_server) {
							/* retry control
							 * connection to server
							 * for certain possibly
							 * transient errors */
							fclose(ctlconn);
							goto doit;
						}
						mes("error from server");
						fprintf(stderr, "server aborted connection\n");
						fflush(stderr);
						exit(1);
					}
					if (sscanf(buf, HELO_FMT,
						   &rvers_major,
						   &rvers_minor,
						   &rvers_delta) < 3) {
						rvers_major = 0;
						rvers_minor = 0;
						rvers_delta = 0;
						srvr_helo = 0;
						while (fgets(buf, mallocsize,
							     stdin)) {
							if (strncmp(buf, "KO", 2) == 0)
								break;
						}
						fclose(ctlconn);
						goto doit;
					}
					irvers = rvers_major*10000
							+ rvers_minor*100
							+ rvers_delta;
				}
				if (host3 && nbuf_bytes && (irvers < 50501))
					nbuf /= buflen;
				if (host3 && (rate != MAXRATE) && rate_pps &&
					     (irvers < 50501)) {
					uint64_t llrate = rate;

					llrate *= ((double)buflen * 8 / 1000);
					rate = llrate;
				}
				if (host3 && !buflenopt && (irvers >= 50302))
					buflen = 0;
				fprintf(ctlconn, "buflen = %d, nbuf = %llu, win = %d, nstream = %d, rate = %lu, port = %hu, trans = %d, braindead = %d", buflen, nbuf, srvrwin, nstream, rate, port, trans, braindead);
				if (irvers >= 30200)
					fprintf(ctlconn, ", timeout = %f", timeout);
				else {
					timeout_sec = timeout;
					if (itimer.it_value.tv_usec)
						timeout_sec++;
					fprintf(ctlconn, ", timeout = %ld", timeout_sec);
					if (!trans && itimer.it_value.tv_usec &&
					    (brief <= 0)) {
						fprintf(stdout, "nuttcp-r%s: transmit timeout value rounded up to %ld second%s for old server\n",
							ident, timeout_sec,
							(timeout_sec == 1)?"":"s");
					}
				}
				fprintf(ctlconn, ", udp = %d, vers = %d.%d.%d", udp, vers_major, vers_minor, vers_delta);
				if (irvers >= 30302)
					fprintf(ctlconn, ", interval = %f", interval);
				else {
					if (interval) {
						fprintf(stdout, "nuttcp%s%s: interval option not supported by server version %d.%d.%d, need >= 3.3.2\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						interval = 0.0;
						abortconn = 1;
					}
				}
				if (irvers >= 30401)
					fprintf(ctlconn, ", reverse = %d", reverse);
				else {
					if (reverse) {
						fprintf(stdout, "nuttcp%s%s: flip option not supported by server version %d.%d.%d, need >= 3.4.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						reverse = 0;
						abortconn = 1;
					}
				}
				if (irvers >= 30501)
					fprintf(ctlconn, ", format = %d", format);
				else {
					if (format) {
						fprintf(stdout, "nuttcp%s%s: format option not supported by server version %d.%d.%d, need >= 3.5.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						format = 0;
					}
				}
				if (irvers >= 30601) {
					fprintf(ctlconn, ", traceroute = %d", traceroute);
					if (traceroute)
						skip_data = 1;
					fprintf(ctlconn, ", irate = %d", irate);
				}
				else {
					if (traceroute) {
						fprintf(stdout, "nuttcp%s%s: traceroute option not supported by server version %d.%d.%d, need >= 3.6.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						traceroute = 0;
						abortconn = 1;
					}
					if (irate && !trans) {
						fprintf(stdout, "nuttcp%s%s: instantaneous rate option not supported by server version %d.%d.%d, need >= 3.6.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						irate = 0;
					}
				}
				if (srvrwin && udp && (irvers < 30602)) {
					fprintf(stdout, "nuttcp%s%s: server version %d.%d.%d ignores UDP window parameter, need >= 3.6.2\n",
						trans?"-t":"-r",
						ident, rvers_major,
						rvers_minor,
						rvers_delta);
					fflush(stdout);
				}
				if ((irvers < 40101) && (format & PARSE)) {
					fprintf(stdout, "nuttcp%s%s: \"-fparse\" option not supported by server version %d.%d.%d, need >= 4.1.1\n",
						trans?"-t":"-r",
						ident, rvers_major,
						rvers_minor,
						rvers_delta);
					fflush(stdout);
					format &= ~PARSE;
					abortconn = 1;
				}
				if (irvers >= 50001) {
					fprintf(ctlconn, ", thirdparty = %.*s", HOSTNAMELEN, host3 ? host3 : "_NULL_");
					if (host3) {
						skip_data = 1;
						fprintf(ctlconn, " , brief3 = %d", brief);
					}
				}
				else {
					if (host3) {
						fprintf(stdout, "nuttcp%s%s: 3rd party nuttcp not supported by server version %d.%d.%d, need >= 5.0.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						host3 = NULL;
						abortconn = 1;
					}
				}
				if (irvers >= 50101) {
					fprintf(ctlconn, " , multicast = %d", multicast);
				}
				else {
					if (multicast) {
						fprintf(stdout, "nuttcp%s%s: multicast not supported by server version %d.%d.%d, need >= 5.1.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						multicast = 0;
						abortconn = 1;
					}
				}
				if (irvers >= 50201) {
					fprintf(ctlconn, " , datamss = %d", datamss);
				}
				else {
					if (datamss && !trans) {
						fprintf(stdout, "nuttcp%s%s: mss option not supported by server version %d.%d.%d, need >= 5.2.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						datamss = 0;
						abortconn = 1;
					}
				}
				if (irvers >= 50301) {
					fprintf(ctlconn, " , tos = %X", tos);
				}
				else {
					if (tos && !trans) {
						fprintf(stdout, "nuttcp%s%s: tos option not supported by server version %d.%d.%d, need >= 5.3.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						tos = 0;
						abortconn = 1;
					}
				}
				if (irvers >= 50501) {
					fprintf(ctlconn, " , nbuf_bytes = %d", nbuf_bytes);
					fprintf(ctlconn, " , rate_pps = %d", rate_pps);
					fprintf(ctlconn, " , nodelay = %d", nodelay);
				}
				else {
					if (host3 && udp && nbuf_bytes) {
						fprintf(stdout, "nuttcp%s%s: Warning: \"-n\" option in bytes for third party not supported\n",
							trans?"-t":"-r", ident);
						fprintf(stdout, "          Warning: by server version %d.%d.%d, need >= 5.5.1\n",
							rvers_major,
							rvers_minor,
							rvers_delta);
						fprintf(stdout, "          Warning: third party request may not transfer\n");
						fprintf(stdout, "          Warning: desired number of bytes in some UDP cases\n");
						fflush(stdout);
						nbuf_bytes = 0;
					}
					if (host3 && udp && rate_pps) {
						fprintf(stdout, "nuttcp%s%s: Warning: \"-R\" option in pps for third party not supported\n",
							trans?"-t":"-r", ident);
						fprintf(stdout, "          Warning: by server version %d.%d.%d, need >= 5.5.1\n",
							rvers_major,
							rvers_minor,
							rvers_delta);
						fprintf(stdout, "          Warning: third party request may not produce\n");
						fprintf(stdout, "          Warning: desired pps rate in some UDP cases\n");
						fflush(stdout);
						rate_pps = 0;
					}
					if (nodelay && !trans) {
						fprintf(stdout, "nuttcp%s%s: TCP_NODELAY opt not supported by server version %d.%d.%d, need >= 5.5.1\n",
							trans?"-t":"-r",
							ident, rvers_major,
							rvers_minor,
							rvers_delta);
						fflush(stdout);
						nodelay = 0;
						abortconn = 1;
					}
				}
				fprintf(ctlconn, "\n");
				fflush(ctlconn);
				if (abortconn) {
					brief = 1;
					if ((!trans && !reverse) ||
					    (trans && reverse))
						skip_data = 1;
				}
				if (!fgets(buf, mallocsize, stdin)) {
					mes("error from server");
					fprintf(stderr, "server aborted connection\n");
					fflush(stderr);
					exit(1);
				}
				if (irvers < 30403)
					udplossinfo = 0;
				if (irvers >= 50401) {
					two_bod = 1;
					handle_urg = 1;
				}
				if (strncmp(buf, "OK", 2) != 0) {
					mes("error from server");
					fprintf(stderr, "server ");
					while (fgets(buf, mallocsize, stdin)) {
						if (strncmp(buf, "KO", 2) == 0)
							break;
						fputs(buf, stderr);
					}
					fflush(stderr);
					exit(1);
				}
				if (sscanf(buf, "OK v%d.%d.%d\n", &rvers_major, &rvers_minor, &rvers_delta) < 3) {
					rvers_major = 0;
					rvers_minor = 0;
					rvers_delta = 0;
				}
				irvers = rvers_major*10000
						+ rvers_minor*100
						+ rvers_delta;
				usleep(10000);
			}
			else {
				if (inetd) {
					ctlconn = stdin;
				}
				else {
					if (!(ctlconn = fdopen(fd[0], "r")))
						err("fdopen: ctlconn for reading");
				}
				fflush(stdout);
				if (!inetd) {
					/* manually started server */
					/* send stdout to client   */
					savestdout=dup(1);
					close(1);
					dup(fd[0]);
					if (!nofork) {
					    /* send stderr to client */
					    close(2);
					    dup(1);
					    if (!single_threaded) {
						/* multi-threaded server */
						if ((pid = fork()) == (pid_t)-1)
						    err("can't fork");
						if (pid != 0) {
						    /* parent just waits for
						     * quick child exit */
						    while ((wait_pid =
								wait(&pidstat))
								    != pid) {
							if (wait_pid ==
								(pid_t)-1) {
							    if (errno == ECHILD)
								break;
							    err("wait failed");
							}
						    }
						    /* and then accepts another
						     * client connection */
						    goto cleanup;
						}
						/* child just makes a grandchild
						 * and then immediately exits
						 * (avoids zombie processes) */
						if ((pid = fork()) == (pid_t)-1)
						    err("can't fork");
						if (pid != 0)
						    exit(0);
						/* grandkid does all the work */
						oneshot = 1;
					    }
					}
				}
				fgets(buf, mallocsize, ctlconn);
				if (sscanf(buf, HELO_FMT, &rvers_major,
					   &rvers_minor, &rvers_delta) == 3) {
					fprintf(stdout, HELO_FMT, vers_major,
						vers_minor, vers_delta);
					fflush(stdout);
					fgets(buf, mallocsize, ctlconn);
				}
				irvers = rvers_major*10000
						+ rvers_minor*100
						+ rvers_delta;
				if (sscanf(buf, "buflen = %d, nbuf = %llu, win = %d, nstream = %d, rate = %lu, port = %hu, trans = %d, braindead = %d, timeout = %lf, udp = %d, vers = %d.%d.%d", &nbuflen, &nbuf, &sendwin, &nstream, &rate, &port, &trans, &braindead, &timeout, &udp, &rvers_major, &rvers_minor, &rvers_delta) < 13) {
					trans = !trans;
					fputs("KO\n", stdout);
					mes("error scanning parameters");
					fprintf(stdout, "may be using older client version than server\n");
					fputs(buf, stdout);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				irvers = rvers_major*10000
						+ rvers_minor*100
						+ rvers_delta;
				if (irvers >= 30302)
					sscanf(strstr(buf, ", interval =") + 13,
						"%lf", &interval);
				else
					interval = 0.0;
				if (irvers >= 30401)
					sscanf(strstr(buf, ", reverse =") + 12,
						"%d", &reverse);
				else
					reverse = 0;
				if (irvers >= 30501)
					sscanf(strstr(buf, ", format =") + 11,
						"%d", &format);
				else
					format = 0;
				if (irvers >= 30601) {
					sscanf(strstr(buf, ", traceroute =") + 15,
						"%d", &traceroute);
					if (traceroute) {
						skip_data = 1;
						brief = 1;
					}
					sscanf(strstr(buf, ", irate =") + 10,
						"%d", &irate);
				}
				else {
					traceroute = 0;
					irate = 0;
				}
				if (irvers >= 50001) {
					sprintf(fmt, "%%%ds", HOSTNAMELEN);
					sscanf(strstr(buf, ", thirdparty =") + 15,
						fmt, host3buf);
					host3buf[HOSTNAMELEN] = '\0';
					if (strcmp(host3buf, "_NULL_") == 0)
						host3 = NULL;
					else
						host3 = host3buf;
					if (host3) {
						if (no3rd) {
							fputs("KO\n", stdout);
							fprintf(stdout, "doesn't allow 3rd party nuttcp\n");
							fputs("KO\n", stdout);
							goto cleanup;
						}
						cp1 = host3;
						while (*cp1) {
							if (!isalnum((int)(*cp1))
							     && (*cp1 != '-')
							     && (*cp1 != '.')
							     && (*cp1 != ':')
							     && (*cp1 != '/')) {
								fputs("KO\n", stdout);
								mes("invalid 3rd party host");
								fprintf(stdout, "3rd party host = '%s'\n", host3);
								fputs("KO\n", stdout);
								goto cleanup;
							}
							cp1++;
						}
						skip_data = 1;
						brief = 1;
						sscanf(strstr(buf, ", brief3 =") + 11,
							"%d", &brief3);
					}
				}
				else {
					host3 = NULL;
				}
				if (irvers >= 50101) {
					sscanf(strstr(buf, ", multicast =") + 14,
						"%d", &mc_param);
				}
				else {
					mc_param = 0;
				}
				if (irvers >= 50201) {
					sscanf(strstr(buf, ", datamss =") + 12,
						"%d", &datamss);
				}
				else {
					datamss = 0;
				}
				if (irvers >= 50301) {
					sscanf(strstr(buf, ", tos =") + 8,
						"%X", &tos);
				}
				else {
					tos = 0;
				}
				if (irvers >= 50501) {
					sscanf(strstr(buf, ", nbuf_bytes =")
							+ 15,
						"%d", &nbuf_bytes);
					sscanf(strstr(buf, ", rate_pps =") + 13,
						"%d", &rate_pps);
					sscanf(strstr(buf, ", nodelay =") + 12,
						"%d", &nodelay);
				}
				else {
					nbuf_bytes = 0;
					rate_pps = 0;
					nodelay = 0;
				}
				trans = !trans;
				if (!traceroute && !host3 &&
				    (nbuflen != buflen)) {
					if (nbuflen < 1) {
						fputs("KO\n", stdout);
						mes("invalid buflen");
						fprintf(stdout, "buflen = %d\n", nbuflen);
						fputs("KO\n", stdout);
						goto cleanup;
					}
					free(buf);
					mallocsize = nbuflen;
					if (mallocsize < MINMALLOC) mallocsize = MINMALLOC;
					if( (buf = (char *)malloc(mallocsize)) == (char *)NULL)
						err("malloc");
					pattern( buf, nbuflen );
				}
				buflen = nbuflen;
				if (nbuf < 1) {
					fputs("KO\n", stdout);
					mes("invalid nbuf");
					fprintf(stdout, "nbuf = %llu\n", nbuf);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				rcvwin = sendwin;
				if (sendwin < 0) {
					fputs("KO\n", stdout);
					mes("invalid win");
					fprintf(stdout, "win = %d\n", sendwin);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if ((nstream < 1) || (nstream > MAXSTREAM)) {
					fputs("KO\n", stdout);
					mes("invalid nstream");
					fprintf(stdout, "nstream = %d\n", nstream);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if (nstream > 1) b_flag = 1;
				if (rate == 0)
					rate = MAXRATE;
				if (timeout < 0) {
					fputs("KO\n", stdout);
					mes("invalid timeout");
					fprintf(stdout, "timeout = %f\n", timeout);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				itimer.it_value.tv_sec = timeout;
				itimer.it_value.tv_usec =
					(timeout - itimer.it_value.tv_sec)
						*1000000;
				if ((port < 5000) || ((port + nstream - 1) > 65535)) {
					fputs("KO\n", stdout);
					mes("invalid port/nstream");
					fprintf(stdout, "port/nstream = %hu/%d\n", port, nstream);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if ((ctlport >= port) && (ctlport <= (port + nstream - 1))) {
					fputs("KO\n", stdout);
					mes("ctlport overlaps port/nstream");
					fprintf(stdout, "ctlport = %hu, port/nstream = %hu/%d\n", ctlport, port, nstream);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if (interval < 0) {
					fputs("KO\n", stdout);
					mes("invalid interval");
					fprintf(stdout, "interval = %f\n", interval);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if (mc_param) {
					if ((mc_param < 1) ||
					    (mc_param > 255)) {
						fputs("KO\n", stdout);
						mes("invalid multicast ttl");
						fprintf(stdout, "multicast ttl = %d\n", mc_param);
						fputs("KO\n", stdout);
						goto cleanup;
					}
					udp = 1;
					nstream = 1;
					if (rate == MAXRATE)
						rate = DEFAULT_UDP_RATE;
				}
				multicast = mc_param;
				if (datamss < 0) {
					fputs("KO\n", stdout);
					mes("invalid datamss");
					fprintf(stdout, "datamss = %d\n", datamss);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if (tos > 255) {
					fputs("KO\n", stdout);
					mes("invalid tos");
					fprintf(stdout, "tos = %d\n", tos);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if (nbuf_bytes < 0) {
					fputs("KO\n", stdout);
					mes("invalid nbuf_bytes");
					fprintf(stdout, "nbuf_bytes = %d\n",
						nbuf_bytes);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if (rate_pps < 0) {
					fputs("KO\n", stdout);
					mes("invalid rate_pps");
					fprintf(stdout, "rate_pps = %d\n",
						rate_pps);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				if (nodelay < 0) {
					fputs("KO\n", stdout);
					mes("invalid nodelay");
					fprintf(stdout, "nodelay = %d\n",
						nodelay);
					fputs("KO\n", stdout);
					goto cleanup;
				}
				fprintf(stdout, "OK v%d.%d.%d\n", vers_major,
						vers_minor, vers_delta);
				fflush(stdout);
				if (udp && interval && (buflen >= 32) &&
					(irvers >= 30403))
					udplossinfo = 1;
				if (irvers >= 50401) {
					two_bod = 1;
					handle_urg = 1;
				}
				if ((trans && !reverse) || (!trans && reverse))
					usleep(50000);
			}
		}

		if (!client) {
		    if (af == AF_INET) {
			inet_ntop(af, &clientaddr.s_addr, hostbuf, sizeof(hostbuf));
		    }
#ifdef AF_INET6
		    else if (af == AF_INET6) {
			inet_ntop(af, clientaddr6.s6_addr, hostbuf, sizeof(hostbuf));
		    }
#endif
		    host = hostbuf;
		}

		if ((stream_idx > 0) && skip_data)
			break;

		bzero((char *)&sinme[stream_idx], sizeof(sinme[stream_idx]));
		bzero((char *)&sinhim[stream_idx], sizeof(sinhim[stream_idx]));

#ifdef AF_INET6
		bzero((char *)&sinme6[stream_idx], sizeof(sinme6[stream_idx]));
		bzero((char *)&sinhim6[stream_idx], sizeof(sinhim6[stream_idx]));
#endif

		if (((trans && !reverse) && (stream_idx > 0)) ||
		    ((!trans && reverse) && (stream_idx > 0)) ||
		    (client && (stream_idx == 0))) {
			/* xmitr initiates connections (unless reversed) */
			if (client) {
				if (af == AF_INET) {
				    sinhim[stream_idx].sin_family = af;
				    bcopy((char *)&(((struct sockaddr_in *)res[stream_idx]->ai_addr)->sin_addr),
					  (char *)&sinhim[stream_idx].sin_addr.s_addr,
					  sizeof(sinhim[stream_idx].sin_addr.s_addr));
				}
#ifdef AF_INET6
				else if (af == AF_INET6) {
				    sinhim6[stream_idx].sin6_family = af;
				    bcopy((char *)&(((struct sockaddr_in6 *)res[stream_idx]->ai_addr)->sin6_addr),
					  (char *)&sinhim6[stream_idx].sin6_addr.s6_addr,
					  sizeof(sinhim6[stream_idx].sin6_addr.s6_addr));
				    sinhim6[stream_idx].sin6_scope_id = ((struct sockaddr_in6 *)res[stream_idx]->ai_addr)->sin6_scope_id;
				}
#endif
				else {
					err("unsupported AF");
				}
			} else {
				sinhim[stream_idx].sin_family = af;
				sinhim[stream_idx].sin_addr = clientaddr;
#ifdef AF_INET6
				sinhim6[stream_idx].sin6_family = af;
				sinhim6[stream_idx].sin6_addr = clientaddr6;
				sinhim6[stream_idx].sin6_scope_id = clientscope6;
#endif
			}
			if (stream_idx == 0) {
				sinhim[stream_idx].sin_port = htons(ctlport);
#ifdef AF_INET6
				sinhim6[stream_idx].sin6_port = htons(ctlport);
#endif
			} else {
				sinhim[stream_idx].sin_port = htons(port + stream_idx - 1);
#ifdef AF_INET6
				sinhim6[stream_idx].sin6_port = htons(port + stream_idx - 1);
#endif
			}
			sinme[stream_idx].sin_port = 0;		/* free choice */
#ifdef AF_INET6
			sinme6[stream_idx].sin6_port = 0;	/* free choice */
#endif
		} else {
			/* rcvr listens for connections (unless reversed) */
			if (stream_idx == 0) {
				sinme[stream_idx].sin_port =   htons(ctlport);
#ifdef AF_INET6
				sinme6[stream_idx].sin6_port = htons(ctlport);
#endif
			} else {
				sinme[stream_idx].sin_port =   htons(port + stream_idx - 1);
#ifdef AF_INET6
				sinme6[stream_idx].sin6_port = htons(port + stream_idx - 1);
#endif
			}
		}
		sinme[stream_idx].sin_family = af;
#ifdef AF_INET6
		sinme6[stream_idx].sin6_family = af;
#endif

		if ((fd[stream_idx] = socket(domain, (udp && (stream_idx != 0))?SOCK_DGRAM:SOCK_STREAM, 0)) < 0)
			err("socket");

		if (stream_idx == nstream) {
			if (!sinkmode && !trans) {
				realstdout = dup(1);
				close(1);
				dup(2);
			}
			if (brief <= 0)
				mes("socket");
#ifdef HAVE_SETPRIO
			if (priority && (brief <= 0)) {
				errno = 0;
				priority = getpriority(PRIO_PROCESS, 0);
				if (errno)
					mes("couldn't get priority");
				else
					fprintf(stdout,
						"nuttcp%s%s: priority = %d\n",
						trans ? "-t" : "-r", ident,
						priority);
			}
#endif
			if (trans) {
			    char tmphost[ADDRSTRLEN] = "\0";
			    if (multicast) {
				/* The multicast transmitter just sends
				 * to the multicast group
				 */
				if (af == AF_INET) {
				    bcopy((char *)&sinhim[1].sin_addr.s_addr,
					(char *)&save_sinhim.sin_addr.s_addr,
					sizeof(struct in_addr));
				    if (!client && (irvers >= 50505)) {
					struct sockaddr_in peer;
					socklen_t peerlen = sizeof(peer);
					if (getpeername(fd[0],
						      (struct sockaddr *)&peer, 
						      &peerlen) < 0) {
						err("getpeername");
					}
					bcopy((char *)&peer.sin_addr.s_addr,
					    (char *)&sinhim[1].sin_addr.s_addr,
					    sizeof(struct in_addr));
				    }
				    else {
					struct sockaddr_in me;
					socklen_t melen = sizeof(me);
					if (getsockname(fd[0],
				    			(struct sockaddr *)&me, 
							&melen) < 0) {
						err("getsockname");
					}
					bcopy((char *)&me.sin_addr.s_addr,
					    (char *)&sinhim[1].sin_addr.s_addr,
					    sizeof(struct in_addr));
				    }
				    sinhim[1].sin_addr.s_addr &=
					htonl(0xFFFFFF);
				    sinhim[1].sin_addr.s_addr |=
					htonl(HI_MC << 24);
				    inet_ntop(af, &sinhim[1].sin_addr,
					      tmphost, sizeof(tmphost));
				    if (setsockopt(fd[1], IPPROTO_IP,
						   IP_MULTICAST_TTL,
						   (void *)&multicast,
						   sizeof(multicast)) < 0)
					err("setsockopt");
				}
				else {
				    err("unsupported AF");
				}
			    }
			    if ((brief <= 0) && (format & PARSE)) {
				fprintf(stdout,"nuttcp-t%s: buflen=%d ",
					ident, buflen);
				if (nbuf != INT_MAX)
				    fprintf(stdout,"nbuf=%llu ", nbuf);
				fprintf(stdout,"nstream=%d port=%d mode=%s host=%s",
				    nstream, port,
				    udp?"udp":"tcp",
				    multicast ? tmphost : host);
				if (multicast)
				    fprintf(stdout, " multicast_ttl=%d",
					    multicast);
				fprintf(stdout, "\n");
				if (timeout)
				    fprintf(stdout,"nuttcp-t%s: time_limit=%.2f\n", 
				    ident, timeout);
				if ((rate != MAXRATE) || tos)
				    fprintf(stdout,"nuttcp-t%s:", ident);
				if (rate != MAXRATE) {
				    fprintf(stdout," rate_limit=%.3f rate_unit=Mbps rate_mode=%s",
					(double)rate/1000,
					irate ? "instantaneous" : "aggregate");
				    if (udp) {
					unsigned long long ppsrate =
					    ((uint64_t)rate * 1000)/8/buflen;

					fprintf(stdout," pps_rate=%llu",
					    ppsrate);
				    }
				}
				if (tos)
				    fprintf(stdout," tos=0x%X", tos);
				if ((rate != MAXRATE) || tos)
				    fprintf(stdout,"\n");
			    }
			    else if (brief <= 0) {
				fprintf(stdout,"nuttcp-t%s: buflen=%d, ",
					ident, buflen);
				if (nbuf != INT_MAX)
				    fprintf(stdout,"nbuf=%llu, ", nbuf);
				fprintf(stdout,"nstream=%d, port=%d %s -> %s",
				    nstream, port,
				    udp?"udp":"tcp",
				    multicast ? tmphost : host);
				if (multicast)
				    fprintf(stdout, " ttl=%d", multicast);
				fprintf(stdout, "\n");
				if (timeout)
				    fprintf(stdout,"nuttcp-t%s: time limit = %.2f second%s\n",
					ident, timeout,
					(timeout == 1.0)?"":"s");
				if ((rate != MAXRATE) || tos)
				    fprintf(stdout,"nuttcp-t%s:", ident);
				if (rate != MAXRATE) {
				    fprintf(stdout," rate limit = %.3f Mbps (%s)",
					(double)rate/1000,
					irate ? "instantaneous" : "aggregate");
				    if (udp) {
					unsigned long long ppsrate =
					    ((uint64_t)rate * 1000)/8/buflen;

					fprintf(stdout,", %llu pps", ppsrate);
				    }
				    if (tos)
					fprintf(stdout,",");
				}
				if (tos)
				    fprintf(stdout," tos = 0x%X", tos);
				if ((rate != MAXRATE) || tos)
				    fprintf(stdout,"\n");
			    }
			} else {
			    if ((brief <= 0) && (format & PARSE)) {
				fprintf(stdout,"nuttcp-r%s: buflen=%d ",
					ident, buflen);
				if (nbuf != INT_MAX)
				    fprintf(stdout,"nbuf=%llu ", nbuf);
				fprintf(stdout,"nstream=%d port=%d mode=%s\n",
				    nstream, port,
				    udp?"udp":"tcp");
				if (tos)
				    fprintf(stdout,"nuttcp-r%s: tos=0x%X\n",
					ident, tos);
				if (interval)
				    fprintf(stdout,"nuttcp-r%s: reporting_interval=%.2f\n",
					ident, interval);
			    }
			    else if (brief <= 0) {
				fprintf(stdout,"nuttcp-r%s: buflen=%d, ",
					ident, buflen);
				if (nbuf != INT_MAX)
				    fprintf(stdout,"nbuf=%llu, ", nbuf);
				fprintf(stdout,"nstream=%d, port=%d %s\n",
				    nstream, port,
				    udp?"udp":"tcp");
				if (tos)
				    fprintf(stdout,"nuttcp-r%s: tos = 0x%X\n",
					ident, tos);
				if (interval)
				    fprintf(stdout,"nuttcp-r%s: interval reporting every %.2f second%s\n",
					ident, interval,
					(interval == 1.0)?"":"s");
			    }
			}
		}

		if (setsockopt(fd[stream_idx], SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(one)) < 0)
				err("setsockopt: so_reuseaddr");

#ifdef IPV6_V6ONLY
		if ((af == AF_INET6) && !v4mapped) {
			if (setsockopt(fd[stream_idx], IPPROTO_IPV6, IPV6_V6ONLY, (void *)&one, sizeof(int)) < 0) {
				err("setsockopt: ipv6_only");
			}
		}
#endif

		if (af == AF_INET) {
		    if (bind(fd[stream_idx], (struct sockaddr *)&sinme[stream_idx], sizeof(sinme[stream_idx])) < 0)
			err("bind");
		}
#ifdef AF_INET6
		else if (af == AF_INET6) {
		    if (bind(fd[stream_idx], (struct sockaddr *)&sinme6[stream_idx], sizeof(sinme6[stream_idx])) < 0)
			err("bind");
		}
#endif
		else {
		    err("unsupported AF");
		}

		if (stream_idx > 0)  {
		    if (trans) {
			/* Set the transmitter options */
			if (sendwin) {
				if( setsockopt(fd[stream_idx], SOL_SOCKET, SO_SNDBUF,
					(void *)&sendwin, sizeof(sendwin)) < 0)
					errmes("unable to setsockopt SO_SNDBUF");
				if (braindead && (setsockopt(fd[stream_idx], SOL_SOCKET, SO_RCVBUF,
					(void *)&rcvwin, sizeof(rcvwin)) < 0))
					errmes("unable to setsockopt SO_RCVBUF");
			}
			if (tos) {
				if( setsockopt(fd[stream_idx], IPPROTO_IP, IP_TOS,
					(void *)&tos, sizeof(tos)) < 0)
					err("setsockopt");
			}
			if (nodelay && !udp) {
				struct protoent *p;
				p = getprotobyname("tcp");
				if( p && setsockopt(fd[stream_idx], p->p_proto, TCP_NODELAY, 
				    (void *)&one, sizeof(one)) < 0)
					err("setsockopt: nodelay");
				if ((stream_idx == nstream) && (brief <= 0))
					mes("nodelay");
			}
		    } else {
			/* Set the receiver options */
			if (rcvwin) {
				if( setsockopt(fd[stream_idx], SOL_SOCKET, SO_RCVBUF,
					(void *)&rcvwin, sizeof(rcvwin)) < 0)
					errmes("unable to setsockopt SO_RCVBUF");
				if (braindead && (setsockopt(fd[stream_idx], SOL_SOCKET, SO_SNDBUF,
					(void *)&sendwin, sizeof(sendwin)) < 0))
					errmes("unable to setsockopt SO_SNDBUF");
			}
			if (tos) {
				if( setsockopt(fd[stream_idx], IPPROTO_IP, IP_TOS,
					(void *)&tos, sizeof(tos)) < 0)
					err("setsockopt");
			}
		    }
		}
		if (!udp || (stream_idx == 0))  {
		    if (((trans && !reverse) && (stream_idx > 0)) ||
		        ((!trans && reverse) && (stream_idx > 0)) ||
		        (client && (stream_idx == 0))) {
			/* The transmitter initiates the connection
			 * (unless reversed by the flip option)
			 */
			if (options && (stream_idx > 0))  {
				if( setsockopt(fd[stream_idx], SOL_SOCKET, options, (void *)&one, sizeof(one)) < 0)
					errmes("unable to setsockopt options");
			}
			usleep(20000);
			if (trans && (stream_idx > 0) && datamss) {
#if defined(__CYGWIN__) || defined(_WIN32)
				if (format & PARSE)
					fprintf(stderr, "nuttcp%s%s: Warning=\"setting_maximum_segment_size_not_supported_on_windows\"\n",
						trans?"-t":"-r", ident);
				else
					fprintf(stderr, "nuttcp%s%s: Warning: setting maximum segment size not supported on windows\n",
						trans?"-t":"-r", ident);
				fflush(stderr);
#endif
				optlen = sizeof(datamss);
				if ((sockopterr = setsockopt(fd[stream_idx], IPPROTO_TCP, TCP_MAXSEG,  (void *)&datamss, optlen)) < 0)
					if (errno != EINVAL)
						err("unable to set maximum segment size");
			}
			num_connect_tries++;
			if (af == AF_INET) {
				error_num = connect(fd[stream_idx], (struct sockaddr *)&sinhim[stream_idx], sizeof(sinhim[stream_idx]));
			}
#ifdef AF_INET6
			else if (af == AF_INET6) {
				error_num = connect(fd[stream_idx], (struct sockaddr *)&sinhim6[stream_idx], sizeof(sinhim6[stream_idx]));
			}
#endif
			else {
			    err("unsupported AF");
			}
			if(error_num < 0) {
				if (clientserver && client && (stream_idx == 0)
						 && ((errno == ECONNREFUSED) ||
						     (errno == ECONNRESET))
						 && (num_connect_tries <
							MAX_CONNECT_TRIES)
						 && retry_server) {
					/* retry control connection to
					 * server for certain possibly
					 * transient errors */
					goto doit;
				}
				if (!trans && (stream_idx == 0))
					err("connect");
				if (stream_idx > 0) {
					if (clientserver && !client) {
						for ( i = 1; i <= stream_idx;
							     i++ )
							close(fd[i]);
						goto cleanup;
					}
					err("connect");
				}
				if (stream_idx == 0) {
					clientserver = 0;
					if (thirdparty) {
						perror("3rd party connect failed");
						fprintf(stderr, "3rd party nuttcp only supported for client/server mode\n");
						fflush(stderr);
						exit(1);
					}
					if (interval) {
						perror("connect failed");
						fprintf(stderr, "interval option only supported for client/server mode\n");
						fflush(stderr);
						exit(1);
					}
					if (reverse) {
						perror("connect failed");
						fprintf(stderr, "flip option only supported for client/server mode\n");
						fflush(stderr);
						exit(1);
					}
					if (traceroute) {
						perror("connect failed");
						fprintf(stderr, "traceroute option only supported for client/server mode\n");
						fflush(stderr);
						exit(1);
					}
					if (host3) {
						perror("connect failed");
						fprintf(stderr, "3rd party nuttcp only supported for client/server mode\n");
						fflush(stderr);
						exit(1);
					}
					if (multicast) {
						perror("connect failed");
						fprintf(stderr, "multicast only supported for client/server mode\n");
						fflush(stderr);
						exit(1);
					}
					if (udp) {
						perror("connect failed");
						fprintf(stderr, "UDP transfers only supported for client/server mode\n");
						fflush(stderr);
						exit(1);
					}
					if (format & PARSE) {
						fprintf(stderr, "nuttcp%s%s: Info=\"attempting_to_switch_to_deprecated_classic_mode\"\n",
							trans?"-t":"-r", ident);
						fprintf(stderr, "nuttcp%s%s: Info=\"will_use_less_reliable_transmitter_side_statistics\"\n",
							trans?"-t":"-r", ident);
					}
					else {
						fprintf(stderr, "nuttcp%s%s: Info: attempting to switch to deprecated \"classic\" mode\n",
							trans?"-t":"-r", ident);
						fprintf(stderr, "nuttcp%s%s: Info: will use less reliable transmitter side statistics\n",
							trans?"-t":"-r", ident);
					}
					fflush(stderr);
				}
			}
			if (sockopterr && trans &&
			    (stream_idx > 0) && datamss) {
				optlen = sizeof(datamss);
				if ((sockopterr = setsockopt(fd[stream_idx], IPPROTO_TCP, TCP_MAXSEG,  (void *)&datamss, optlen)) < 0) {
					if (errno != EINVAL)
						err("unable to set maximum segment size");
					else
						err("setting maximum segment size not supported on this OS");
				}
			}
			if (stream_idx == nstream) {
				optlen = sizeof(datamss);
				if (getsockopt(fd[stream_idx], IPPROTO_TCP, TCP_MAXSEG,  (void *)&datamss, &optlen) < 0)
					err("get dataconn maximum segment size didn't work");
				if (format & DEBUGMTU)
					fprintf(stderr, "datamss = %d\n", datamss);
			}
			if ((stream_idx == nstream) && (brief <= 0)) {
				char tmphost[ADDRSTRLEN] = "\0";
				if (af == AF_INET) {
				    inet_ntop(af, &sinhim[stream_idx].sin_addr.s_addr,
					      tmphost, sizeof(tmphost));
				}
#ifdef AF_INET6
				else if (af == AF_INET6) {
				    inet_ntop(af, sinhim6[stream_idx].sin6_addr.s6_addr,
					      tmphost, sizeof(tmphost));
				}
#endif
				else {
				    err("unsupported AF");
				}

				if (format & PARSE) {
					fprintf(stdout,
						"nuttcp%s%s: connect=%s", 
						trans?"-t":"-r", ident,
						tmphost);
					if (trans && datamss) {
						fprintf(stdout, " mss=%d",
							datamss);
					}
				}
				else {
					fprintf(stdout,
						"nuttcp%s%s: connect to %s", 
						trans?"-t":"-r", ident,
						tmphost);
					if (trans && datamss) {
						fprintf(stdout, " with mss=%d",
							datamss);
					}
				}
				fprintf(stdout, "\n");
			}
		    } else {
			/* The receiver listens for the connection
			 * (unless reversed by the flip option)
			 */
			if (trans && (stream_idx > 0) && datamss) {
#if defined(__CYGWIN__) || defined(_WIN32)
				if (format & PARSE)
					fprintf(stderr, "nuttcp%s%s: Warning=\"setting_maximum_segment_size_not_supported_on_windows\"\n",
						trans?"-t":"-r", ident);
				else
					fprintf(stderr, "nuttcp%s%s: Warning: setting maximum segment size not supported on windows\n",
						trans?"-t":"-r", ident);
				fflush(stderr);
#endif
				optlen = sizeof(datamss);
				if ((sockopterr = setsockopt(fd[stream_idx], IPPROTO_TCP, TCP_MAXSEG,  (void *)&datamss, optlen)) < 0)
					if (errno != EINVAL)
						err("unable to set maximum segment size");
			}
			listen(fd[stream_idx],1);   /* allow a queue of 1 */
			if (options && (stream_idx > 0))  {
				if( setsockopt(fd[stream_idx], SOL_SOCKET, options, (void *)&one, sizeof(one)) < 0)
					errmes("unable to setsockopt options");
			}
			if (sockopterr && trans &&
			    (stream_idx > 0) && datamss) {
				optlen = sizeof(datamss);
				if ((sockopterr = setsockopt(fd[stream_idx], IPPROTO_TCP, TCP_MAXSEG,  (void *)&datamss, optlen)) < 0)
					if (errno != EINVAL)
						err("unable to set maximum segment size");
			}
			if (clientserver && !client && (stream_idx > 0)) {
				sigact.sa_handler = ignore_alarm;
				sigemptyset(&sigact.sa_mask);
				sigact.sa_flags = 0;
				sigaction(SIGALRM, &sigact, &savesigact);
				alarm(ACCEPT_TIMEOUT);
			}
			fromlen = sizeof(frominet);
			nfd=accept(fd[stream_idx], (struct sockaddr *)&frominet, &fromlen);
			save_errno = errno;
			if (clientserver && !client && (stream_idx > 0)) {
				alarm(0);
				sigact.sa_handler = savesigact.sa_handler;
				sigact.sa_mask = savesigact.sa_mask;
				sigact.sa_flags = savesigact.sa_flags;
				sigaction(SIGALRM, &sigact, 0);
			}
			if (nfd < 0) {
				/* check for interrupted system call,
				 * close data streams, cleanup and try
				 * again - all other errors just die
				 */
				if ((save_errno == EINTR) && clientserver
							  && !client
							  && (stream_idx > 0)) {
					for ( i = 1; i <= stream_idx; i++ )
						close(fd[i]);
					goto cleanup;
				}
				err("accept");
			}
			af = frominet.ss_family;
			close(fd[stream_idx]);
			fd[stream_idx]=nfd;
			if (sockopterr && trans &&
			    (stream_idx > 0) && datamss) {
				optlen = sizeof(datamss);
				if ((sockopterr = setsockopt(fd[stream_idx], IPPROTO_TCP, TCP_MAXSEG,  (void *)&datamss, optlen)) < 0) {
					if (errno != EINVAL)
						err("unable to set maximum segment size");
					else
						err("setting maximum segment size not supported on this OS");
				}
			}
			if (stream_idx == nstream) {
				optlen = sizeof(datamss);
				if (getsockopt(fd[stream_idx], IPPROTO_TCP, TCP_MAXSEG,  (void *)&datamss, &optlen) < 0)
					err("get dataconn maximum segment size didn't work");
				if (format & DEBUGMTU)
					fprintf(stderr, "datamss = %d\n", datamss);
			}
			if (af == AF_INET) {
			    struct sockaddr_in peer;
			    socklen_t peerlen = sizeof(peer);
			    if (getpeername(fd[stream_idx], (struct sockaddr *) &peer, 
					&peerlen) < 0) {
				err("getpeername");
			    }
			    if ((stream_idx == nstream) && (brief <= 0)) {
				char tmphost[ADDRSTRLEN] = "\0";
				inet_ntop(af, &peer.sin_addr.s_addr,
					  tmphost, sizeof(tmphost));

				if (format & PARSE) {
					fprintf(stdout,
						"nuttcp%s%s: accept=%s", 
						trans?"-t":"-r", ident,
						tmphost);
					if (trans && datamss) {
						fprintf(stdout, " mss=%d",
							datamss);
					}
				}
				else {
					fprintf(stdout,
						"nuttcp%s%s: accept from %s", 
						trans?"-t":"-r", ident,
						tmphost);
					if (trans && datamss) {
						fprintf(stdout, " with mss=%d",
							datamss);
					}
				}
				fprintf(stdout, "\n");
			    }
			    if (stream_idx == 0) clientaddr = peer.sin_addr;
			}
#ifdef AF_INET6
			else if (af == AF_INET6) {
			    struct sockaddr_in6 peer;
			    socklen_t peerlen = sizeof(peer);
			    if (getpeername(fd[stream_idx], (struct sockaddr *) &peer, 
					&peerlen) < 0) {
				err("getpeername");
			    }
			    if ((stream_idx == nstream) && (brief <= 0)) {
				char tmphost[ADDRSTRLEN] = "\0";
				inet_ntop(af, peer.sin6_addr.s6_addr,
					  tmphost, sizeof(tmphost));
				if (format & PARSE) {
				    fprintf(stdout,
					    "nuttcp%s%s: accept=%s", 
					    trans?"-t":"-r", ident,
					    tmphost);
				    if (trans && datamss) {
					fprintf(stdout, " mss=%d", datamss);
				    }
				}
				else {
				    fprintf(stdout,
					    "nuttcp%s%s: accept from %s", 
					    trans?"-t":"-r", ident,
					    tmphost);
				    if (trans && datamss) {
					fprintf(stdout, " with mss=%d",
						datamss);
				    }
				}
				fprintf(stdout, "\n");
			    }
			    if (stream_idx == 0) {
			    	clientaddr6 = peer.sin6_addr;
			    	clientscope6 = peer.sin6_scope_id;
			    }
			}
#endif
			else {
			    err("unsupported AF");
			}
		    }
		}
		optlen = sizeof(sendwinval);
		if (getsockopt(fd[stream_idx], SOL_SOCKET, SO_SNDBUF,  (void *)&sendwinval, &optlen) < 0)
			err("get send window size didn't work");
#if defined(linux)
		sendwinval /= 2;
#endif
		if ((stream_idx > 0) && sendwin && (trans || braindead) &&
		    (sendwinval < (0.98 * sendwin))) {
			if (format & PARSE)
				fprintf(stderr, "nuttcp%s%s: Warning=\"send_window_size_%d_<_requested_window_size_%d\"\n",
					trans?"-t":"-r", ident,
					sendwinval, sendwin);
			else
				fprintf(stderr, "nuttcp%s%s: Warning: send window size %d < requested window size %d\n",
					trans?"-t":"-r", ident,
					sendwinval, sendwin);
			fflush(stderr);
		}
		optlen = sizeof(rcvwinval);
		if (getsockopt(fd[stream_idx], SOL_SOCKET, SO_RCVBUF,  (void *)&rcvwinval, &optlen) < 0)
			err("Get recv window size didn't work");
#if defined(linux)
		rcvwinval /= 2;
#endif
		if ((stream_idx > 0) && rcvwin && (!trans || braindead) &&
		    (rcvwinval < (0.98 * rcvwin))) {
			if (format & PARSE)
				fprintf(stderr, "nuttcp%s%s: Warning=\"receive_window_size_%d_<_requested_window_size_%d\"\n",
					trans?"-t":"-r", ident,
					rcvwinval, rcvwin);
			else
				fprintf(stderr, "nuttcp%s%s: Warning: receive window size %d < requested window size %d\n",
					trans?"-t":"-r", ident,
					rcvwinval, rcvwin);
			fflush(stderr);
		}

		if ((stream_idx == nstream) && (brief <= 0)) {
			if (format & PARSE)
				fprintf(stdout,"nuttcp%s%s: send_window_size=%d receive_window_size=%d\n", trans?"-t":"-r", ident, sendwinval, rcvwinval);
			else
				fprintf(stdout,"nuttcp%s%s: send window size = %d, receive window size = %d\n", trans?"-t":"-r", ident, sendwinval, rcvwinval);
		}

		if (firsttime) {
			firsttime = 0;
			origsendwin = sendwinval;
			origrcvwin = rcvwinval;
		}
	}

	if (abortconn)
		exit(1);

	if (host3 && clientserver) {
		char path[64];
		char *cmd;

		fflush(stdout);
		fflush(stderr);
		cmd = "nuttcp";

		if (client) {
			if ((pid = fork()) == (pid_t)-1)
				err("can't fork");
			if (pid == 0) {
				while (fgets(linebuf, sizeof(linebuf),
					     stdin) && !intr) {
					if (strncmp(linebuf, "DONE", 4)
							== 0)
						exit(0);
					if (*ident && (*linebuf != '\n'))
						fprintf(stdout, "%s: ",
							ident + 1);
					fputs(linebuf, stdout);
					fflush(stdout);
				}
				exit(0);
			}
			signal(SIGINT, SIG_IGN);
			while ((wait_pid = wait(&pidstat)) != pid) {
				if (wait_pid == (pid_t)-1) {
					if (errno == ECHILD)
						break;
					err("wait failed");
				}
			}
			exit(0);
		}
		else {
			if ((pid = fork()) == (pid_t)-1)
				err("can't fork");
			if (pid != 0) {
				sigact.sa_handler = &sigalarm;
				sigemptyset(&sigact.sa_mask);
				sigact.sa_flags = 0;
				sigaction(SIGALRM, &sigact, 0);
				alarm(10);
				while ((wait_pid = wait(&pidstat))
						!= pid) {
					if (wait_pid == (pid_t)-1) {
						if (errno == ECHILD)
							break;
						if (errno == EINTR) {
							pollfds[0].fd =
							    fileno(ctlconn);
							pollfds[0].events =
							    POLLIN | POLLPRI;
							pollfds[0].revents = 0;
							if ((poll(pollfds, 1, 0)
									> 0)
								&& (pollfds[0].revents &
									(POLLIN | POLLPRI))) {
								kill(pid,
								     SIGINT);
								sleep(1);
								kill(pid,
								     SIGINT);
								continue;
							}
							sigact.sa_handler =
								&sigalarm;
							sigemptyset(&sigact.sa_mask);
							sigact.sa_flags = 0;
							sigaction(SIGALRM,
								  &sigact, 0);
							alarm(10);
							continue;
						}
						err("wait failed");
					}
				}
				fprintf(stdout, "DONE\n");
				fflush(stdout);
				goto cleanup;
			}
			close(2);
			dup(1);
			i = 0;
			j = 0;
			cmdargs[i++] = cmd;
			cmdargs[i++] = "-3";
			if (pass_ctlport) {
				sprintf(tmpargs[j], "-P%hu", ctlport);
				cmdargs[i++] = tmpargs[j++];
			}
			if (irvers < 50302) {
				if ((udp && !multicast
					 && (buflen != DEFAULTUDPBUFLEN)) ||
				    (udp && multicast
					 && (buflen != DEFAULT_MC_UDPBUFLEN)) ||
				    (!udp && (buflen != 65536))) {
					sprintf(tmpargs[j], "-l%d", buflen);
					cmdargs[i++] = tmpargs[j++];
				}
			}
			else if (buflen) {
				sprintf(tmpargs[j], "-l%d", buflen);
				cmdargs[i++] = tmpargs[j++];
			}
			if (nbuf != INT_MAX) {
				if (nbuf_bytes)
					sprintf(tmpargs[j], "-n%llub", nbuf);
				else
					sprintf(tmpargs[j], "-n%llu", nbuf);
				cmdargs[i++] = tmpargs[j++];
			}
			if (brief3 != 1) {
				sprintf(tmpargs[j], "-b%d", brief3);
				cmdargs[i++] = tmpargs[j++];
			}
			if (sendwin) {
				sprintf(tmpargs[j], "-w%d", sendwin/1024);
				cmdargs[i++] = tmpargs[j++];
			}
			if (nstream != 1) {
				sprintf(tmpargs[j], "-N%d", nstream);
				cmdargs[i++] = tmpargs[j++];
			}
			if (rate != MAXRATE) {
				if (rate_pps)
					sprintf(tmpargs[j], "-R%s%lup",
						irate ? "i" : "", rate);
				else
					sprintf(tmpargs[j], "-R%s%lu",
						irate ? "i" : "", rate);
				cmdargs[i++] = tmpargs[j++];
			} else {
				if (udp && !multicast)
					cmdargs[i++] = "-R0";
			}
			if (port != DEFAULT_PORT) {
				sprintf(tmpargs[j], "-p%hu", port);
				cmdargs[i++] = tmpargs[j++];
			}
			if (trans)
				cmdargs[i++] = "-r";
			if (braindead)
				cmdargs[i++] = "-wb";
			if (timeout && (timeout != DEFAULT_TIMEOUT)) {
				sprintf(tmpargs[j], "-T%lf", timeout);
				cmdargs[i++] = tmpargs[j++];
			}
			if (udp) {
				if (multicast) {
					sprintf(tmpargs[j], "-m%d", multicast);
					cmdargs[i++] = tmpargs[j++];
				}
				else
					cmdargs[i++] = "-u";
			}
			if (interval) {
				sprintf(tmpargs[j], "-i%f", interval);
				cmdargs[i++] = tmpargs[j++];
			}
			if (reverse)
				cmdargs[i++] = "-F";
			if (format) {
				if (format & XMITSTATS)
					cmdargs[i++] = "-fxmitstats";
				if (format & RUNNINGTOTAL)
					cmdargs[i++] = "-frunningtotal";
				if (format & NOPERCENTLOSS)
					cmdargs[i++] = "-f-percentloss";
				if (format & NODROPS)
					cmdargs[i++] = "-f-drops";
				if (format & PARSE)
					cmdargs[i++] = "-fparse";
			}
			if (traceroute)
				cmdargs[i++] = "-xt";
			if (datamss) {
				sprintf(tmpargs[j], "-M%d", datamss);
				cmdargs[i++] = tmpargs[j++];
			}
			if (tos) {
				sprintf(tmpargs[j], "-c0x%Xt", tos);
				cmdargs[i++] = tmpargs[j++];
			}
			if (nodelay)
				cmdargs[i++] = "-D";
			cmdargs[i++] = host3;
			cmdargs[i] = NULL;
			execvp(cmd, cmdargs);
			if (errno == ENOENT) {
				strcpy(path, "/usr/local/sbin/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if (errno == ENOENT) {
				strcpy(path, "/usr/local/bin/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if (errno == ENOENT) {
				strcpy(path, "/usr/sbin/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if (errno == ENOENT) {
				strcpy(path, "/sbin/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if (errno == ENOENT) {
				strcpy(path, "/usr/etc/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if ((errno == ENOENT) && (getuid() != 0)
					      && (geteuid() != 0)) {
				strcpy(path, "./");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			perror("execvp failed");
			fprintf(stderr, "failed to execute %s\n", cmd);
			fflush(stdout);
			fflush(stderr);
			if (!inetd)
				exit(0);
			goto cleanup;
		}
	}

	if (traceroute && clientserver) {
		char path[64];
		char *cmd;

		fflush(stdout);
		fflush(stderr);
		if (multicast)
			cmd = "mtrace";
		else {
			cmd = "traceroute";
#ifdef AF_INET6
			if (af == AF_INET6)
				cmd = "traceroute6";
#endif
		}
		if (client) {
			if ((pid = fork()) == (pid_t)-1)
				err("can't fork");
			if (pid != 0) {
				while ((wait_pid = wait(&pidstat)) != pid) {
					if (wait_pid == (pid_t)-1) {
						if (errno == ECHILD)
							break;
						err("wait failed");
					}
				}
				fflush(stdout);
			}
			else {
				signal(SIGINT, SIG_DFL);
				close(2);
				dup(1);
				i = 0;
				cmdargs[i++] = cmd;
				cmdargs[i++] = host;
				cmdargs[i] = NULL;
				execvp(cmd, cmdargs);
				if (errno == ENOENT) {
					strcpy(path, "/usr/local/sbin/");
					strcat(path, cmd);
					execv(path, cmdargs);
				}
				if (errno == ENOENT) {
					strcpy(path, "/usr/local/bin/");
					strcat(path, cmd);
					execv(path, cmdargs);
				}
				if (errno == ENOENT) {
					strcpy(path, "/usr/sbin/");
					strcat(path, cmd);
					execv(path, cmdargs);
				}
				if (errno == ENOENT) {
					strcpy(path, "/sbin/");
					strcat(path, cmd);
					execv(path, cmdargs);
				}
				if (errno == ENOENT) {
					strcpy(path, "/usr/etc/");
					strcat(path, cmd);
					execv(path, cmdargs);
				}
				perror("execvp failed");
				fprintf(stderr, "failed to execute %s\n", cmd);
				fflush(stdout);
				fflush(stderr);
				exit(0);
			}
		}
		fprintf(stdout, "\n");
		if (intr) {
			intr = 0;
			fprintf(stdout, "\n");
			signal(SIGINT, sigint);
		}
		if (!skip_data) {
			for ( stream_idx = 1; stream_idx <= nstream;
					      stream_idx++ )
				close(fd[stream_idx]);
		}
		if (client) {
			if ((pid = fork()) == (pid_t)-1)
				err("can't fork");
			if (pid == 0) {
				while (fgets(linebuf, sizeof(linebuf),
					     stdin) && !intr) {
					if (strncmp(linebuf, "DONE", 4)
							== 0)
						exit(0);
					fputs(linebuf, stdout);
					fflush(stdout);
				}
				exit(0);
			}
			signal(SIGINT, SIG_IGN);
			while ((wait_pid = wait(&pidstat)) != pid) {
				if (wait_pid == (pid_t)-1) {
					if (errno == ECHILD)
						break;
					err("wait failed");
				}
			}
			exit(0);
		}
		else {
			if (!inetd) {
				if ((pid = fork()) == (pid_t)-1)
					err("can't fork");
				if (pid != 0) {
					while ((wait_pid = wait(&pidstat))
							!= pid) {
						if (wait_pid == (pid_t)-1) {
							if (errno == ECHILD)
								break;
							err("wait failed");
						}
					}
					fprintf(stdout, "DONE\n");
					fflush(stdout);
					goto cleanup;
				}
			}
			close(2);
			dup(1);
			i = 0;
			cmdargs[i++] = cmd;
			cmdargs[i++] = host;
			cmdargs[i] = NULL;
			execvp(cmd, cmdargs);
			if (errno == ENOENT) {
				strcpy(path, "/usr/local/sbin/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if (errno == ENOENT) {
				strcpy(path, "/usr/local/bin/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if (errno == ENOENT) {
				strcpy(path, "/usr/sbin/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if (errno == ENOENT) {
				strcpy(path, "/sbin/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			if (errno == ENOENT) {
				strcpy(path, "/usr/etc/");
				strcat(path, cmd);
				execv(path, cmdargs);
			}
			perror("execvp failed");
			fprintf(stderr, "failed to execute %s\n", cmd);
			fflush(stdout);
			fflush(stderr);
			if (!inetd)
				exit(0);
			goto cleanup;
		}
	}

	if (multicast && !trans) {
		/* The multicast receiver must join the multicast group */
		if (af == AF_INET) {
			struct sockaddr_in peer;
			char tmphost[ADDRSTRLEN] = "\0";
			char tmphost2[ADDRSTRLEN] = "\0";
			socklen_t peerlen = sizeof(peer);
			if (getpeername(fd[0], (struct sockaddr *)&peer, 
					&peerlen) < 0) {
				err("getpeername");
			}
			if (client && (irvers >= 50505)) {
				struct sockaddr_in me;
				socklen_t melen = sizeof(me);
				if (getsockname(fd[0], (struct sockaddr *)&me, 
						&melen) < 0) {
					err("getsockname");
				}
				bcopy((char *)&me.sin_addr.s_addr,
				      (char *)&mc_group.imr_multiaddr.s_addr,
				      sizeof(struct in_addr));
			}
			else {
				bcopy((char *)&peer.sin_addr.s_addr,
				      (char *)&mc_group.imr_multiaddr.s_addr,
				      sizeof(struct in_addr));
			}
			mc_group.imr_multiaddr.s_addr &= htonl(0xFFFFFF);
			mc_group.imr_multiaddr.s_addr |= htonl(HI_MC << 24);
			if (setsockopt(fd[1], IPPROTO_IP, IP_ADD_MEMBERSHIP,
				       (void *)&mc_group, sizeof(mc_group)) < 0)
				err("setsockopt");
			if (brief <= 0) {
				inet_ntop(af, &peer.sin_addr.s_addr,
					  tmphost, sizeof(tmphost));
				inet_ntop(af, &mc_group.imr_multiaddr,
					  tmphost2, sizeof(tmphost2));

				if (format & PARSE)
					fprintf(stdout,
						"nuttcp%s%s: multicast_source=%s multicast_group=%s\n", 
						trans?"-t":"-r", ident,
						tmphost, tmphost2);
				else
					fprintf(stdout,
						"nuttcp%s%s: receiving from multicast source %s on group %s\n", 
						trans?"-t":"-r", ident,
						tmphost, tmphost2);
			}
		}
		else {
			err("unsupported AF");
		}
	}

	if (trans && timeout) {
		itimer.it_value.tv_sec = timeout;
		itimer.it_value.tv_usec =
			(timeout - itimer.it_value.tv_sec)*1000000;
		signal(SIGALRM, sigalarm);
		if (!udp)
			setitimer(ITIMER_REAL, &itimer, 0);
	}
	else if (!trans && interval) {
		sigact.sa_handler = &sigalarm;
		sigemptyset(&sigact.sa_mask);
		sigact.sa_flags = SA_RESTART;
		sigaction(SIGALRM, &sigact, 0);
		itimer.it_value.tv_sec = interval;
		itimer.it_value.tv_usec =
			(interval - itimer.it_value.tv_sec)*1000000;
		itimer.it_interval.tv_sec = interval;
		itimer.it_interval.tv_usec =
			(interval - itimer.it_interval.tv_sec)*1000000;
		setitimer(ITIMER_REAL, &itimer, 0);
		if (clientserver && !client) {
			chk_idle_data = (interval < idle_data_min) ?
						idle_data_min : interval;
			chk_idle_data = (chk_idle_data > idle_data_max) ?
						idle_data_max : chk_idle_data;
		}
	}
	else if (clientserver && !client && !trans) {
		sigact.sa_handler = &sigalarm;
		sigemptyset(&sigact.sa_mask);
		sigact.sa_flags = SA_RESTART;
		sigaction(SIGALRM, &sigact, 0);
		if (timeout) {
			chk_idle_data = timeout/2;
		}
		else {
			if (rate != MAXRATE)
				chk_idle_data = (double)(nbuf*buflen)
							    /rate/125/2;
			else
				chk_idle_data = default_idle_data;
		}
		chk_idle_data = (chk_idle_data < idle_data_min) ?
					idle_data_min : chk_idle_data;
		chk_idle_data = (chk_idle_data > idle_data_max) ?
					idle_data_max : chk_idle_data;
		itimer.it_value.tv_sec = chk_idle_data;
		itimer.it_value.tv_usec =
			(chk_idle_data - itimer.it_value.tv_sec)
				*1000000;
		itimer.it_interval.tv_sec = chk_idle_data;
		itimer.it_interval.tv_usec =
			(chk_idle_data - itimer.it_interval.tv_sec)
				*1000000;
		setitimer(ITIMER_REAL, &itimer, 0);
	}

	if (interval && clientserver && client && trans)
		do_poll = 1;

	if (irate) {
		pkt_time = (double)buflen/rate/125;
		irate_pk_usec = pkt_time*1000000;
		irate_pk_nsec = (pkt_time*1000000 - irate_pk_usec)*1000;
	}
	prep_timer();
	errno = 0;
	stream_idx = 0;
	ocorrection = 0;
	correction = 0.0;
	if (do_poll) {
		long flags;

		pollfds[0].fd = fileno(ctlconn);
		pollfds[0].events = POLLIN | POLLPRI;
		pollfds[0].revents = 0;
		for ( i = 1; i <= nstream; i++ ) {
			pollfds[i].fd = fd[i];
			pollfds[i].events = POLLOUT;
			pollfds[i].revents = 0;
		}
		flags = fcntl(0, F_GETFL, 0);
		if (flags < 0)
			err("fcntl 1");
		flags |= O_NONBLOCK;
		if (fcntl(0, F_SETFL, flags) < 0)
			err("fcntl 2");
	}
	if (sinkmode) {      
		register int cnt = 0;
		if (trans)  {
			if(udp) {
				strcpy(buf, "BOD0");
				if (multicast) {
				    bcopy((char *)&sinhim[1].sin_addr.s_addr,
					  (char *)&save_mc.sin_addr.s_addr,
					  sizeof(struct in_addr));
				    bcopy((char *)&save_sinhim.sin_addr.s_addr,
					  (char *)&sinhim[1].sin_addr.s_addr,
					  sizeof(struct in_addr));
				}
				(void)Nwrite( fd[stream_idx + 1], buf, 4 ); /* rcvr start */
				if (two_bod) {
					usleep(250000);
					strcpy(buf, "BOD1");
					(void)Nwrite( fd[stream_idx + 1], buf, 4 ); /* rcvr start */
				}
				if (multicast) {
				    bcopy((char *)&save_mc.sin_addr.s_addr,
					  (char *)&sinhim[1].sin_addr.s_addr,
					  sizeof(struct in_addr));
				}
				if (timeout)
					setitimer(ITIMER_REAL, &itimer, 0);
				prep_timer();
			}
/*			beginnings of timestamps - not ready for prime time */
/*			bzero(buf + 8, 4);				*/
/*			bzero(buf + 12, 4);				*/
			nbytes += buflen;
			if (do_poll && (format & DEBUGPOLL)) {
				fprintf(stdout, "do_poll is set\n");
				fflush(stdout);
			}
			if (udplossinfo)
				bcopy(&nbytes, buf + 24, 8);
			if (nbuf == INT_MAX)
				nbuf = ULLONG_MAX;
			while (nbuf-- && ((cnt = Nwrite(fd[stream_idx + 1],buf,buflen)) == buflen) && !intr) {
				if (clientserver && ((nbuf & 0x3FF) == 0)) {
				    if (!client) {
					/* check if client went away */
					pollfds[0].fd = fileno(ctlconn);
					save_events = pollfds[0].events;
					pollfds[0].events = POLLIN | POLLPRI;
					pollfds[0].revents = 0;
					if ((poll(pollfds, 1, 0) > 0)
						&& (pollfds[0].revents &
							(POLLIN | POLLPRI)))
						intr = 1;
					pollfds[0].events = save_events;
				    }
				    else if (handle_urg) {
					/* check for urgent TCP data
					 * on control connection */
					pollfds[0].fd = fileno(ctlconn);
					save_events = pollfds[0].events;
					pollfds[0].events = POLLPRI;
					pollfds[0].revents = 0;
					if ((poll(pollfds, 1, 0) > 0)
						&& (pollfds[0].revents &
							POLLPRI)) {
						tmpbuf[0] = '\0';
						if ((recv(fd[0], tmpbuf, 1,
							  MSG_OOB) == -1) &&
						    (errno == EINVAL)) 
							recv(fd[0], tmpbuf,
							     1, 0);
						if (tmpbuf[0] == 'A')
							intr = 1;
						else
							err("recv urgent data");
					}
					pollfds[0].events = save_events;
				    }
				}
				nbytes += buflen;
				cnt = 0;
				if (udplossinfo)
					bcopy(&nbytes, buf + 24, 8);
				stream_idx++;
				stream_idx = stream_idx % nstream;
				if (do_poll &&
				       ((pollst = poll(pollfds, nstream + 1, 5000))
						> 0) &&
				       (pollfds[0].revents & (POLLIN | POLLPRI)) && !intr) {
					/* check for server output */
#ifdef DEBUG
					if (format & DEBUGPOLL) {
						fprintf(stdout, "got something %d: ", i);
				    for ( i = 0; i < nstream + 1; i++ ) {
					if (pollfds[i].revents & POLLIN) {
						fprintf(stdout, " rfd %d",
							pollfds[i].fd);
					}
					if (pollfds[i].revents & POLLPRI) {
						fprintf(stdout, " pfd %d",
							pollfds[i].fd);
					}
					if (pollfds[i].revents & POLLOUT) {
						fprintf(stdout, " wfd %d",
							pollfds[i].fd);
					}
					if (pollfds[i].revents & POLLERR) {
						fprintf(stdout, " xfd %d",
							pollfds[i].fd);
					}
					if (pollfds[i].revents & POLLHUP) {
						fprintf(stdout, " hfd %d",
							pollfds[i].fd);
					}
					if (pollfds[i].revents & POLLNVAL) {
						fprintf(stdout, " nfd %d",
							pollfds[i].fd);
					}
				    }
						fprintf(stdout, "\n");
						fflush(stdout);
				    }
					if (format & DEBUGPOLL) {
						fprintf(stdout, "got server output: %s", intervalbuf);
						fflush(stdout);
					}
#endif
					while (fgets(intervalbuf, sizeof(intervalbuf), stdin))
					{
					if (strncmp(intervalbuf, "DONE", 4) == 0) {
						if (format & DEBUGPOLL) {
							fprintf(stdout, "got DONE\n");
							fflush(stdout);
						}
						got_done = 1;
						intr = 1;
						do_poll = 0;
						break;
					}
					else if (strncmp(intervalbuf, "nuttcp-r", 8) == 0) {
						if ((brief <= 0) ||
						    strstr(intervalbuf,
							    "Warning") ||
						    strstr(intervalbuf,
							    "Error") ||
						    strstr(intervalbuf,
							    "Debug")) {
							if (*ident) {
								fputs("nuttcp-r", stdout);
								fputs(ident, stdout);
								fputs(intervalbuf + 8, stdout);
							}
							else
								fputs(intervalbuf, stdout);
							fflush(stdout);
						}
					}
					else {
						if (*ident)
							fprintf(stdout, "%s: ", ident + 1);
						fputs(intervalbuf, stdout);
						fflush(stdout);
					}
					}
				}
				if (do_poll && (pollst < 0)) {
					if (errno == EINTR)
						break;
					err("poll");
				}
			}
			nbytes -= buflen;
			if (intr && (cnt > 0))
				nbytes += cnt;
			if(udp) {
				if (multicast)
				    bcopy((char *)&save_sinhim.sin_addr.s_addr,
					  (char *)&sinhim[1].sin_addr.s_addr,
					  sizeof(struct in_addr));
				strcpy(buf, "EOD0");
				(void)Nwrite( fd[stream_idx + 1], buf, 4 ); /* rcvr end */
			}
		} else {
			if (udp) {
			    bzero(buf + 24, 8);
			    ntbytesc = 0;
			    first_read = 1;
			    need_swap = 0;
			    got_eod0 = 0;
			    while (((cnt=Nread(fd[stream_idx + 1],buf,buflen)) > 0) && !intr)  {
				    if( cnt <= 4 ) {
					    if (strncmp(buf, "EOD0", 4) == 0) {
						    gettimeofday(&timepkrcv,
							(struct timezone *)0);
						    got_eod0 = 1;
						    done = 1;
						    continue;
					    }
					    if (strncmp(buf, "EOD", 3) == 0) {
						    ocorrection = buf[3] - '0';
						    gettimeofday(&time_eod,
							(struct timezone *)0);
						    done = 1;
						    break;	/* "EOF" */
					    }
					    if (strncmp(buf, "BOD", 3) == 0) {
						    if (two_bod &&
							(buf[3] == '0'))
							    continue;
						    if (interval)
							setitimer(ITIMER_REAL,
								  &itimer, 0);
						    prep_timer();
						    got_begin = 1;
						    continue;
					    }
					    break;
				    }
				    else if (!got_begin) {
					    if (interval)
						    setitimer(ITIMER_REAL,
							      &itimer, 0);
					    prep_timer();
					    got_begin = 1;
				    }
				    else if (got_eod0) {
					    gettimeofday(&timepkrcv,
							 (struct timezone *)0);
				    }
				    if (!got_begin)
					    continue;
				    nbytes += cnt;
				    cnt = 0;
				    /* problematic if the interval timer
				     * goes off right here */
				    if (udplossinfo) {
					    if (first_read) {
						    bcopy(buf + 24, &ntbytesc,
								8);
						    first_read = 0;
						    if (ntbytesc > 0x100000000ull)
							    need_swap = 1;
						    if (!need_swap)
							    continue;
					    }
					    if (!need_swap)
						    bcopy(buf + 24, &ntbytesc,
								8);
					    else {
						    cp1 = (char *)&ntbytesc;
						    cp2 = buf + 31;
						    for ( i = 0; i < 8; i++ )
							    *cp1++ = *cp2--;
					    }
				    }
				    stream_idx++;
				    stream_idx = stream_idx % nstream;
			    }
			    if (intr && (cnt > 0))
				    nbytes += cnt;
			    if (got_eod0) {
				    tvsub( &timed, &time_eod, &timepkrcv );
				    correction = timed.tv_sec +
						    ((double)timed.tv_usec)
								/ 1000000;
			    }
			} else {
			    while (((cnt=Nread(fd[stream_idx + 1],buf,buflen)) > 0) && !intr)  {
				    nbytes += cnt;
				    cnt = 0;
				    stream_idx++;
				    stream_idx = stream_idx % nstream;
			    }
			    if (intr && (cnt > 0))
				    nbytes += cnt;
			}
		}
	} else {
		register int cnt;
		if (trans)  {
			while((cnt=read(0,buf,buflen)) > 0 &&
			    Nwrite(fd[stream_idx + 1],buf,cnt) == cnt) {
				nbytes += cnt;
				cnt = 0;
				stream_idx++;
				stream_idx = stream_idx % nstream;
			}
		}  else  {
			while((cnt=Nread(fd[stream_idx + 1],buf,buflen)) > 0 &&
			    write(realstdout,buf,cnt) == cnt) {
				nbytes += cnt;
				cnt = 0;
				stream_idx++;
				stream_idx = stream_idx % nstream;
			}
		}
	}
	if (errno && (errno != EAGAIN)) {
		if ((errno != EINTR) && (!clientserver || client)) err("IO");
	}
	itimer.it_value.tv_sec = 0;
	itimer.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, &itimer, 0);
	done = 1;
	(void)read_timer(stats,sizeof(stats));
	if(udp&&trans)  {
		usleep(500000);
		strcpy(buf, "EOD1");
		(void)Nwrite( fd[stream_idx + 1], buf, 4 ); /* rcvr end */
		stream_idx++;
		stream_idx = stream_idx % nstream;
		usleep(500000);
		strcpy(buf, "EOD2");
		(void)Nwrite( fd[stream_idx + 1], buf, 4 ); /* rcvr end */
		stream_idx++;
		stream_idx = stream_idx % nstream;
		usleep(500000);
		strcpy(buf, "EOD3");
		(void)Nwrite( fd[stream_idx + 1], buf, 4 ); /* rcvr end */
		stream_idx++;
		stream_idx = stream_idx % nstream;
		usleep(500000);
		strcpy(buf, "EOD4");
		(void)Nwrite( fd[stream_idx + 1], buf, 4 ); /* rcvr end */
		stream_idx++;
		stream_idx = stream_idx % nstream;
	}

	if (clientserver && client && !host3 && udp && trans)
		/* If all the EOD packets get lost at the end of a UDP
		 * transfer, having the client do a shutdown() for writing
		 * on the control connection allows the server to more
		 * quickly realize that the UDP transfer has completed
		 * (mostly of benefit for separate control and data paths) */
		shutdown(0, SHUT_WR);

	if (multicast && !trans) {
		/* Leave the multicast group */
		if (af == AF_INET) {
			if (setsockopt(fd[1], IPPROTO_IP, IP_DROP_MEMBERSHIP,
				       (void *)&mc_group, sizeof(mc_group)) < 0)
				err("setsockopt");
		}
		else {
			err("unsupported AF");
		}
	}

	for ( stream_idx = 1; stream_idx <= nstream; stream_idx++ )
		close(fd[stream_idx]);

	if (interval && clientserver && !client && !trans) {
		fprintf(stdout, "DONE\n");
		fflush(stdout);
	}

	if( cput <= 0.0 )  cput = 0.000001;
	if( realt <= 0.0 )  realt = 0.000001;

	if (udp && !trans) {
		if (got_eod0)
			realt -= correction;
		else
			realt -= ocorrection * 0.5;
	}

	sprintf(srvrbuf, "%.4f", (double)nbytes/1024/1024);
	sscanf(srvrbuf, "%lf", &MB);

	if (interval && clientserver && client && trans && !got_done) {
		long flags;

		if (format & DEBUGPOLL) {
			fprintf(stdout, "getting rest of server output\n");
			fflush(stdout);
		}
		flags = fcntl(0, F_GETFL, 0);
		if (flags < 0)
			err("fcntl 3");
		flags &= ~O_NONBLOCK;
		if (fcntl(0, F_SETFL, flags) < 0)
			err("fcntl 4");
		while (fgets(intervalbuf, sizeof(intervalbuf), stdin)) {
			if (strncmp(intervalbuf, "DONE", 4) == 0) {
				if (format & DEBUGPOLL) {
					fprintf(stdout, "got DONE 2\n");
					fflush(stdout);
				}
				break;
			}
			if ((!strstr(intervalbuf, " MB / ") ||
			     !strstr(intervalbuf, " sec = ")) && (brief > 0))
				continue;
			if (*ident)
				fprintf(stdout, "%s: ", ident + 1);
			fputs(intervalbuf, stdout);
			fflush(stdout);
		}
	}

	if (clientserver && client) {
		cp1 = srvrbuf;
		while (fgets(cp1, sizeof(srvrbuf) - (cp1 - srvrbuf), stdin)) {
			if (*(cp1 + strlen(cp1) - 1) != '\n') {
				*cp1 = '\0';
				break;
			}
			if (strstr(cp1, "real") && strstr(cp1, "seconds")) {
				strcpy(fmt, "nuttcp-%*c: ");
				if (format & PARSE)
					strcat(fmt, P_PERF_FMT_IN);
				else
					strcat(fmt, PERF_FMT_IN);
				sscanf(cp1, fmt,
				       &srvr_MB, &srvr_realt, &srvr_KBps,
				       &srvr_Mbps);
				if (trans && udp) {
					strncpy(tmpbuf, cp1, 256);
					*(tmpbuf + 256) = '\0';
					if (strncmp(tmpbuf,
						    "nuttcp-r", 8) == 0)
						sprintf(cp1, "nuttcp-r%s%s",
							ident, tmpbuf + 8);
					cp1 += strlen(cp1);
					cp2 = cp1;
					sprintf(cp2, "nuttcp-r:");
					cp2 += 9;
					if (format & PARSE)
						strcpy(fmt, P_DROP_FMT);
					else
						strcpy(fmt, DROP_FMT);
					sprintf(cp2, fmt,
						(int64_t)(((MB - srvr_MB)
							*1024*1024)
								/buflen + 0.5),
						(uint64_t)((MB*1024*1024)
							/buflen + 0.5));
					cp2 += strlen(cp2);
					fractloss = ((MB != 0.0) ?
						1 - srvr_MB/MB : 0.0);
					if (format & PARSE)
						strcpy(fmt, P_LOSS_FMT);
					else if ((fractloss != 0.0) &&
						 (fractloss < 0.001))
						strcpy(fmt, LOSS_FMT5);
					else
						strcpy(fmt, LOSS_FMT);
					sprintf(cp2, fmt, fractloss * 100);
					cp2 += strlen(cp2);
					sprintf(cp2, "\n");
				}
			}
			else if (strstr(cp1, "sys")) {
				strcpy(fmt, "nuttcp-%*c: ");
				if (format & PARSE) {
					strcat(fmt, "stats=cpu ");
					strcat(fmt, P_CPU_STATS_FMT_IN2);
				}
				else
					strcat(fmt, CPU_STATS_FMT_IN2);
				if (sscanf(cp1, fmt,
					   &srvr_cpu_util) != 7) {
					strcpy(fmt, "nuttcp-%*c: ");
					if (format & PARSE) {
						strcat(fmt, "stats=cpu ");
						strcat(fmt,
						       P_CPU_STATS_FMT_IN);
					}
					else
						strcat(fmt, CPU_STATS_FMT_IN);
					sscanf(cp1, fmt,
					       &srvr_cpu_util);
				}
			}
			else if ((strstr(cp1, "KB/cpu")) && !verbose)
				continue;
			strncpy(tmpbuf, cp1, 256);
			*(tmpbuf + 256) = '\0';
			if (strncmp(tmpbuf, "nuttcp-", 7) == 0)
				sprintf(cp1, "nuttcp-%c%s%s",
					tmpbuf[7], ident, tmpbuf + 8);
			if ((strstr(cp1, "Warning") ||
			     strstr(cp1, "Error") ||
			     strstr(cp1, "Debug"))
					&& (brief > 0)) {
				fputs(cp1, stdout);
				fflush(stdout);
			}
			cp1 += strlen(cp1);
		}
		got_srvr_output = 1;
	}

	if (brief <= 0) {
		strcpy(fmt, "nuttcp%s%s: ");
		if (format & PARSE)
			strcat(fmt, P_PERF_FMT_OUT);
		else
			strcat(fmt, PERF_FMT_OUT);
		fprintf(stdout, fmt, trans?"-t":"-r", ident,
			(double)nbytes/(1024*1024), realt,
			(double)nbytes/realt/1024,
			(double)nbytes/realt/125000 );
		if (clientserver && client && !trans && udp) {
			fprintf(stdout, "nuttcp-r%s:", ident);
			if (format & PARSE)
				strcpy(fmt, P_DROP_FMT);
			else
				strcpy(fmt, DROP_FMT);
			fprintf(stdout, fmt,
				(int64_t)(((srvr_MB - MB)*1024*1024)
					/buflen + 0.5),
				(uint64_t)((srvr_MB*1024*1024)/buflen + 0.5));
			fractloss = ((srvr_MB != 0.0) ? 1 - MB/srvr_MB : 0.0);
			if (format & PARSE)
				strcpy(fmt, P_LOSS_FMT);
			else if ((fractloss != 0.0) && (fractloss < 0.001))
				strcpy(fmt, LOSS_FMT5);
			else
				strcpy(fmt, LOSS_FMT);
			fprintf(stdout, fmt, fractloss * 100);
			fprintf(stdout, "\n");
		}
		if (verbose) {
			strcpy(fmt, "nuttcp%s%s: ");
			if (format & PARSE)
				strcat(fmt, "megabytes=%.4f cpu_seconds=%.2f KB_per_cpu_second=%.2f\n");
			else
				strcat(fmt, "%.4f MB in %.2f CPU seconds = %.2f KB/cpu sec\n");
			fprintf(stdout, fmt,
				trans?"-t":"-r", ident,
				(double)nbytes/(1024*1024), cput,
				(double)nbytes/cput/1024 );
		}

		strcpy(fmt, "nuttcp%s%s: ");
		if (format & PARSE)
			strcat(fmt, "io_calls=%d msec_per_call=%.2f calls_per_sec=%.2f\n");
		else
			strcat(fmt, "%d I/O calls, msec/call = %.2f, calls/sec = %.2f\n");
		fprintf(stdout, fmt,
			trans?"-t":"-r", ident,
			numCalls,
			1024.0 * realt/((double)numCalls),
			((double)numCalls)/realt);

		strcpy(fmt, "nuttcp%s%s: ");
		if (format & PARSE)
			strcat(fmt, "stats=cpu %s\n");
		else
			strcat(fmt, "%s\n");
		fprintf(stdout, fmt, trans?"-t":"-r", ident, stats);
	}

	if (format & PARSE)
		strcpy(fmt, P_CPU_STATS_FMT_IN2);
	else
		strcpy(fmt, CPU_STATS_FMT_IN2);
	if (sscanf(stats, fmt, &cpu_util) != 6) {
		if (format & PARSE)
			strcpy(fmt, P_CPU_STATS_FMT_IN);
		else
			strcpy(fmt, CPU_STATS_FMT_IN);
		sscanf(stats, fmt, &cpu_util);
	}

	if (brief && clientserver && client) {
		if ((brief < 0) || interval)
			fprintf(stdout, "\n");
		if (udp) {
			if (trans) {
				if (*ident)
					fprintf(stdout, "%s: ", ident + 1);
				if (format & PARSE)
					strcpy(fmt, P_PERF_FMT_BRIEF);
				else
					strcpy(fmt, PERF_FMT_BRIEF);
				fprintf(stdout, fmt,
					srvr_MB, srvr_realt, srvr_Mbps,
					cpu_util, srvr_cpu_util);
				if (!(format & NODROPS)) {
					if (format & PARSE)
						strcpy(fmt, P_DROP_FMT_BRIEF);
					else
						strcpy(fmt, DROP_FMT_BRIEF);
					fprintf(stdout, fmt,
						(int64_t)(((MB - srvr_MB)
							*1024*1024)
								/buflen + 0.5),
						(uint64_t)((MB*1024*1024)
							/buflen + 0.5));
				}
				if (!(format & NOPERCENTLOSS)) {
					fractloss = ((MB != 0.0) ?
						1 - srvr_MB/MB : 0.0);
					if (format & PARSE)
						strcpy(fmt, P_LOSS_FMT_BRIEF);
					else if ((fractloss != 0.0) &&
						 (fractloss < 0.001))
						strcpy(fmt, LOSS_FMT_BRIEF5);
					else
						strcpy(fmt, LOSS_FMT_BRIEF);
					fprintf(stdout, fmt, fractloss * 100);
				}
				if (format & XMITSTATS) {
					if (format & PARSE)
						strcpy(fmt, P_PERF_FMT_BRIEF3);
					else
						strcpy(fmt, PERF_FMT_BRIEF3);
					fprintf(stdout, fmt, MB);
				}
			}
			else {
				if (*ident)
					fprintf(stdout, "%s: ", ident + 1);
				if (format & PARSE)
					strcpy(fmt, P_PERF_FMT_BRIEF);
				else
					strcpy(fmt, PERF_FMT_BRIEF);
				fprintf(stdout, fmt,
					MB, realt, (double)nbytes/realt/125000,
					srvr_cpu_util, cpu_util);
				if (!(format & NODROPS)) {
					if (format & PARSE)
						strcpy(fmt, P_DROP_FMT_BRIEF);
					else
						strcpy(fmt, DROP_FMT_BRIEF);
					fprintf(stdout, fmt,
						(int64_t)(((srvr_MB - MB)
							*1024*1024)
								/buflen + 0.5),
						(uint64_t)((srvr_MB*1024*1024)
							/buflen + 0.5));
				}
				if (!(format & NOPERCENTLOSS)) {
					fractloss = ((srvr_MB != 0.0) ?
						1 - MB/srvr_MB : 0.0);
					if (format & PARSE)
						strcpy(fmt, P_LOSS_FMT_BRIEF);
					else if ((fractloss != 0.0) &&
						 (fractloss < 0.001))
						strcpy(fmt, LOSS_FMT_BRIEF5);
					else
						strcpy(fmt, LOSS_FMT_BRIEF);
					fprintf(stdout, fmt, fractloss * 100);
				}
				if (format & XMITSTATS) {
					if (format & PARSE)
						strcpy(fmt, P_PERF_FMT_BRIEF3);
					else
						strcpy(fmt, PERF_FMT_BRIEF3);
					fprintf(stdout, fmt, srvr_MB);
				}
			}
			fprintf(stdout, "\n");
		}
		else
			if (trans) {
				if (*ident)
					fprintf(stdout, "%s: ", ident + 1);
				if (format & PARSE)
					strcpy(fmt, P_PERF_FMT_BRIEF);
				else
					strcpy(fmt, PERF_FMT_BRIEF);
				strcat(fmt, "\n");
				fprintf(stdout, fmt,
					srvr_MB, srvr_realt, srvr_Mbps,
					cpu_util, srvr_cpu_util );
			}
			else {
				if (*ident)
					fprintf(stdout, "%s: ", ident + 1);
				if (format & PARSE)
					strcpy(fmt, P_PERF_FMT_BRIEF);
				else
					strcpy(fmt, PERF_FMT_BRIEF);
				strcat(fmt, "\n");
				fprintf(stdout, fmt,
					MB, realt, (double)nbytes/realt/125000,
					srvr_cpu_util, cpu_util );
			}
	}
	else {
		if (brief && !clientserver) {
			if (brief < 0)
				fprintf(stdout, "\n");
			if (*ident)
				fprintf(stdout, "%s: ", ident + 1);
			fprintf(stdout, PERF_FMT_BRIEF2 "\n", MB,
				realt, (double)nbytes/realt/125000, cpu_util,
				trans?"TX":"RX" );
		}
	}

cleanup:
	if (clientserver) {
		if (client) {
			if (brief <= 0)
				fputs("\n", stdout);
			if (brief <= 0) {
				if (got_srvr_output) {
					fputs(srvrbuf, stdout);
				}
			}
			else {
				while (fgets(buf, mallocsize, stdin))
					fputs(buf, stdout);
			}
			fflush(stdout);
			close(0);
		}
		else {
			fflush(stdout);
			close(1);
			if (!inetd) {
				dup(savestdout);
				close(savestdout);
				fflush(stderr);
				if (!nofork) {
					close(2);
					dup(1);
				}
			}
		}
		fclose(ctlconn);
		if (!inetd)
			close(fd[0]);
	}
	if (clientserver && !client) {
		itimer.it_value.tv_sec = 0;
		itimer.it_value.tv_usec = 0;
		setitimer(ITIMER_REAL, &itimer, 0);
		signal(SIGALRM, SIG_DFL);
		bzero((char *)&frominet, sizeof(frominet));
		bzero((char *)&clientaddr, sizeof(clientaddr));
#ifdef AF_INET6
		bzero((char *)&clientaddr6, sizeof(clientaddr));
		clientscope6 = 0;
#endif
		cput = 0.000001;
		realt = 0.000001;
		nbytes = 0;
		ntbytes = 0;
		ntbytesc = 0;
		chk_nbytes = 0;
		numCalls = 0;
/*		Don't re-initialize buflen since it's used to		*/
/*		determine if we need to change the buffer memory	*/
/*		allocation for the next client data stream request	*/
/*		buflen = 64 * 1024;					*/
/*		if (udp) buflen = DEFAULTUDPBUFLEN;			*/
		nbuf = 0;
		sendwin = origsendwin;
		rcvwin = origrcvwin;
		nstream = 1;
		b_flag = 0;
		rate = MAXRATE;
		irate = 0;
		irate_cum_nsec = 0.0;
		timeout = 0.0;
		interval = 0.0;
		chk_interval = 0.0;
		chk_idle_data = 0.0;
		datamss = 0;
		tos = 0;
		nodelay = 0;
		do_poll = 0;
		pbytes = 0;
		ptbytes = 0;
		ident[0] = '\0';
		intr = 0;
		abortconn = 0;
		port = 5001;
		trans = 0;
		braindead = 0;
		udp = 0;
		udplossinfo = 0;
		got_srvr_output = 0;
		reverse = 0;
		format = 0;
		traceroute = 0;
		multicast = 0;
		skip_data = 0;
		host3 = NULL;
		thirdparty = 0;
		nbuf_bytes = 0;
		rate_pps = 0;

#ifdef HAVE_SETPRIO
		priority = 0;
#endif

		brief = 0;
		done = 0;
		got_begin = 0;
		two_bod = 0;
		handle_urg = 0;
		for ( stream_idx = 0; stream_idx <= nstream; stream_idx++ )
			res[stream_idx] = NULL;
		if (!oneshot)
			goto doit;
	}
	if (!sinkmode && !trans)
		close(realstdout);
	for ( stream_idx = 0; stream_idx <= nstream; stream_idx++ )
		if (res[stream_idx])
			freeaddrinfo(res[stream_idx]);
	exit(0);

usage:
	fprintf(stdout,Usage);
	exit(1);
}

static void
err( char *s )
{
	long flags, saveflags;

	fprintf(stderr,"nuttcp%s%s: v%d.%d.%d: Error: ", trans?"-t":"-r", ident,
			vers_major, vers_minor, vers_delta);
	perror(s);
	fprintf(stderr,"errno=%d\n",errno);
	fflush(stderr);
	if ((stream_idx > 0) && !done &&
	    clientserver && !client && !trans && handle_urg) {
		/* send 'A' for ABORT as urgent TCP data
		 * on control connection (don't block) */
		saveflags = fcntl(fd[0], F_GETFL, 0);
		if (saveflags != -1) {
			flags = saveflags | O_NONBLOCK;
			fcntl(fd[0], F_SETFL, flags);
		}
		send(fd[0], "A", 1, MSG_OOB);
		if (saveflags != -1) {
			flags = saveflags;
			fcntl(fd[0], F_SETFL, flags);
		}
	}
	exit(1);
}

static void
mes( char *s )
{
	fprintf(stdout,"nuttcp%s%s: v%d.%d.%d: %s\n", trans?"-t":"-r", ident,
			vers_major, vers_minor, vers_delta, s);
}

static void
errmes( char *s )
{
	fprintf(stderr,"nuttcp%s%s: v%d.%d.%d: Error: ", trans?"-t":"-r", ident,
			vers_major, vers_minor, vers_delta);
	perror(s);
	fprintf(stderr,"errno=%d\n",errno);
	fflush(stderr);
}

void
pattern( register char *cp, register int cnt )
{
	register char c;
	c = 0;
	while( cnt-- > 0 )  {
		while( !isprint((c&0x7F)) )  c++;
		*cp++ = (c++&0x7F);
	}
}

/*
 *			P R E P _ T I M E R
 */
void
prep_timer()
{
	gettimeofday(&time0, (struct timezone *)0);
	timep.tv_sec = time0.tv_sec;
	timep.tv_usec = time0.tv_usec;
	timepk.tv_sec = time0.tv_sec;
	timepk.tv_usec = time0.tv_usec;
	getrusage(RUSAGE_SELF, &ru0);
}

/*
 *			R E A D _ T I M E R
 * 
 */
double
read_timer( char *str, int len )
{
	struct timeval timedol;
	struct rusage ru1;
	struct timeval td;
	struct timeval tend, tstart;
	char line[132];

	getrusage(RUSAGE_SELF, &ru1);
	gettimeofday(&timedol, (struct timezone *)0);
	prusage(&ru0, &ru1, &timedol, &time0, line);
	(void)strncpy( str, line, len );

	/* Get real time */
	tvsub( &td, &timedol, &time0 );
	realt = td.tv_sec + ((double)td.tv_usec) / 1000000;

	/* Get CPU time (user+sys) */
	tvadd( &tend, &ru1.ru_utime, &ru1.ru_stime );
	tvadd( &tstart, &ru0.ru_utime, &ru0.ru_stime );
	tvsub( &td, &tend, &tstart );
	cput = td.tv_sec + ((double)td.tv_usec) / 1000000;
	if( cput < 0.00001 )  cput = 0.00001;
	return( cput );
}

static void
prusage( register struct rusage *r0, register struct rusage *r1, struct timeval *e, struct timeval *b, char *outp )
{
	struct timeval tdiff;
	register time_t t;
	register char *cp;
	register int i;
	int ms;

	t = (r1->ru_utime.tv_sec-r0->ru_utime.tv_sec)*100+
	    (r1->ru_utime.tv_usec-r0->ru_utime.tv_usec)/10000+
	    (r1->ru_stime.tv_sec-r0->ru_stime.tv_sec)*100+
	    (r1->ru_stime.tv_usec-r0->ru_stime.tv_usec)/10000;
	ms =  (e->tv_sec-b->tv_sec)*100 + (e->tv_usec-b->tv_usec)/10000;

#define END(x)	{while(*x) x++;}

	if (format & PARSE)
		cp = "user=%U system=%S elapsed=%E cpu=%P memory=%Xi+%Dd-%Mmaxrss io=%F+%Rpf swaps=%Ccsw";
	else
		cp = "%Uuser %Ssys %Ereal %P %Xi+%Dd %Mmaxrss %F+%Rpf %Ccsw";

	for (; *cp; cp++)  {
		if (*cp != '%')
			*outp++ = *cp;
		else if (cp[1]) switch(*++cp) {

		case 'U':
			tvsub(&tdiff, &r1->ru_utime, &r0->ru_utime);
			sprintf(outp,"%ld.%01ld", (long)tdiff.tv_sec, (long)tdiff.tv_usec/100000);
			END(outp);
			break;

		case 'S':
			tvsub(&tdiff, &r1->ru_stime, &r0->ru_stime);
			sprintf(outp,"%ld.%01ld", (long)tdiff.tv_sec, (long)tdiff.tv_usec/100000);
			END(outp);
			break;

		case 'E':
			psecs(ms / 100, outp);
			END(outp);
			break;

		case 'P':
			sprintf(outp,"%d%%", (int) (t*100 / ((ms ? ms : 1))));
			END(outp);
			break;

		case 'W':
			i = r1->ru_nswap - r0->ru_nswap;
			sprintf(outp,"%d", i);
			END(outp);
			break;

		case 'X':
			sprintf(outp,"%ld", t == 0 ? 0 : (r1->ru_ixrss-r0->ru_ixrss)/t);
			END(outp);
			break;

		case 'D':
			sprintf(outp,"%ld", t == 0 ? 0 :
			    (r1->ru_idrss+r1->ru_isrss-(r0->ru_idrss+r0->ru_isrss))/t);
			END(outp);
			break;

		case 'K':
			sprintf(outp,"%ld", t == 0 ? 0 :
			    ((r1->ru_ixrss+r1->ru_isrss+r1->ru_idrss) -
			    (r0->ru_ixrss+r0->ru_idrss+r0->ru_isrss))/t);
			END(outp);
			break;

		case 'M':
			sprintf(outp,"%ld", r1->ru_maxrss/2);
			END(outp);
			break;

		case 'F':
			sprintf(outp,"%ld", r1->ru_majflt-r0->ru_majflt);
			END(outp);
			break;

		case 'R':
			sprintf(outp,"%ld", r1->ru_minflt-r0->ru_minflt);
			END(outp);
			break;

		case 'I':
			sprintf(outp,"%ld", r1->ru_inblock-r0->ru_inblock);
			END(outp);
			break;

		case 'O':
			sprintf(outp,"%ld", r1->ru_oublock-r0->ru_oublock);
			END(outp);
			break;
		case 'C':
			sprintf(outp,"%ld+%ld", r1->ru_nvcsw-r0->ru_nvcsw,
				r1->ru_nivcsw-r0->ru_nivcsw );
			END(outp);
			break;
		}
	}
	*outp = '\0';
}

static void
tvadd( struct timeval *tsum, struct timeval *t0, struct timeval *t1 )
{

	tsum->tv_sec = t0->tv_sec + t1->tv_sec;
	tsum->tv_usec = t0->tv_usec + t1->tv_usec;
	if (tsum->tv_usec > 1000000)
		tsum->tv_sec++, tsum->tv_usec -= 1000000;
}

static void
tvsub( struct timeval *tdiff, struct timeval *t1, struct timeval *t0 )
{

	tdiff->tv_sec = t1->tv_sec - t0->tv_sec;
	tdiff->tv_usec = t1->tv_usec - t0->tv_usec;
	if (tdiff->tv_usec < 0)
		tdiff->tv_sec--, tdiff->tv_usec += 1000000;
}

static void
psecs( long l, register char *cp )
{
	register int i;

	i = l / 3600;
	if (i) {
		sprintf(cp,"%d:", i);
		END(cp);
		i = l % 3600;
		sprintf(cp,"%d%d", (i/60) / 10, (i/60) % 10);
		END(cp);
	} else {
		i = l;
		sprintf(cp,"%d", i / 60);
		END(cp);
	}
	i %= 60;
	*cp++ = ':';
	sprintf(cp,"%d%d", i / 10, i % 10);
}

/*
 *			N R E A D
 */
int
Nread( int fd, char *buf, int count )
{
	struct sockaddr_storage from;
	socklen_t len = sizeof(from);
	register int cnt;
	if( udp )  {
		cnt = recvfrom( fd, buf, count, 0, (struct sockaddr *)&from, &len );
		numCalls++;
	} else {
		if( b_flag )
			cnt = mread( fd, buf, count );	/* fill buf */
		else {
			cnt = read( fd, buf, count );
			numCalls++;
		}
	}
	return(cnt);
}

/*
 *			N W R I T E
 */
int
Nwrite( int fd, char *buf, int count )
{
	struct timeval timedol;
	struct timeval td;
	register int cnt = 0;
	double deltat;

	if (irate) {
		/* Get real time */
		gettimeofday(&timedol, (struct timezone *)0);
		tvsub( &td, &timedol, &timepk );
		deltat = td.tv_sec + ((double)td.tv_usec) / 1000000;

		if (deltat >= 2*pkt_time) {
			timepk.tv_sec = timedol.tv_sec;
			timepk.tv_usec = timedol.tv_usec;
			irate_cum_nsec = 0;
		}

		while (((double)count/rate/125 > deltat) && !intr) {
			/* Get real time */
			gettimeofday(&timedol, (struct timezone *)0);
			tvsub( &td, &timedol, &timepk );
			deltat = td.tv_sec + ((double)td.tv_usec) / 1000000;
		}

		irate_cum_nsec += irate_pk_nsec;
		if (irate_cum_nsec >= 1000.0) {
			irate_cum_nsec -= 1000.0;
			timepk.tv_usec++;
		}
		timepk.tv_usec += irate_pk_usec;
		while (timepk.tv_usec >= 1000000) {
			timepk.tv_usec -= 1000000;
			timepk.tv_sec++;
		}
		if (intr && (!udp || (count != 4))) return(0);
	}
	else {
		while ((double)nbytes/realt/125 > rate) {
			/* Get real time */
			gettimeofday(&timedol, (struct timezone *)0);
			tvsub( &td, &timedol, &time0 );
			realt = td.tv_sec + ((double)td.tv_usec) / 1000000;
			if( realt <= 0.0 )  realt = 0.000001;
		}
	}
/*	beginnings of timestamps - not ready for prime time		*/
/*	gettimeofday(&timedol, (struct timezone *)0);			*/
/*	bcopy(&timedol.tv_sec, buf + 8, 4);				*/
/*	bcopy(&timedol.tv_usec, buf + 12, 4);				*/
	if( udp )  {
again:
		if (af == AF_INET) {
			cnt = sendto( fd, buf, count, 0, (struct sockaddr *)&sinhim[stream_idx + 1], sizeof(sinhim[stream_idx + 1]) );
		}
#ifdef AF_INET6
		else if (af == AF_INET6) {
			cnt = sendto( fd, buf, count, 0, (struct sockaddr *)&sinhim6[stream_idx + 1], sizeof(sinhim6[stream_idx + 1]) );
		}
#endif
		else {
			err("unsupported AF");
		}
		numCalls++;
		if( cnt<0 && errno == ENOBUFS )  {
			delay(18000);
			errno = 0;
			goto again;
		}
	} else {
		cnt = write( fd, buf, count );
		numCalls++;
	}
	return(cnt);
}

int
delay( int us )
{
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = us;
	(void)select( 1, (fd_set *)0, (fd_set *)0, (fd_set *)0, &tv );
	return(1);
}

/*
 *			M R E A D
 *
 * This function performs the function of a read(II) but will
 * call read(II) multiple times in order to get the requested
 * number of characters.  This can be necessary because
 * network connections don't deliver data with the same
 * grouping as it is written with.  Written by Robert S. Miles, BRL.
 */
int
mread( int fd, register char *bufp, unsigned n )
{
	register unsigned	count = 0;
	register int		nread;

	do {
		nread = read(fd, bufp, n-count);
		numCalls++;
		if(nread < 0)  {
			if (errno != EINTR)
				perror("nuttcp_mread");
			return(-1);
		}
		if(nread == 0)
			return((int)count);
		count += (unsigned)nread;
		bufp += nread;
	 } while(count < n);

	return((int)count);
}

/*
 *			G E T O P T V A L P
 *
 * This function returns a character pointer to the option value
 * pointed at by argv and sets skiparg to 1 if the option and its
 * value were passed as separate arguments (otherwise it sets
 * skiparg to 0).  index is the position within argv where the
 * option value resides if the option was specified as a single
 * argument.  reqval indicates whether or not the option requires
 * a value
 */
char *
getoptvalp( char **argv, int index, int reqval, int *skiparg )
{
	struct sockaddr_storage dummy;
	char **nextarg;

	*skiparg = 0;
	nextarg = argv + 1;

	/* if there is a value in the current arg return it */
	if (argv[0][index])
		return(&argv[0][index]);

	/* if there isn't a next arg return a pointer to the
	   current arg value (which will be an empty string) */
	if (*nextarg == NULL)
		return(&argv[0][index]);

	/* if the next arg is another option, return a pointer to the
	   current arg value (which will be an empty string) */
	if (**nextarg == '-')
		return(&argv[0][index]);

	/* if there is an arg after the next arg and it is another
	   option, return the next arg as the option value */
	if (*(nextarg + 1) && (**(nextarg + 1) == '-')) {
		*skiparg = 1;
		return(*nextarg);
	}

	/* if the option requires a value, return the next arg
	   as the option value */
	if (reqval) {
		*skiparg = 1;
		return(*nextarg);
	}

	/* if the next arg is an Ipv4 address, return a pointer to the
	   current arg value (which will be an empty string) */
	if (inet_pton(AF_INET, *nextarg, &dummy) > 0)
		return(&argv[0][index]);

#ifdef AF_INET6
	/* if the next arg is an Ipv6 address, return a pointer to the
	   current arg value (which will be an empty string) */
	if (inet_pton(AF_INET6, *nextarg, &dummy) > 0)
		return(&argv[0][index]);
#endif

	/* if the next arg begins with an alphabetic character,
	   assume it is a hostname and thus return a pointer to the
	   current arg value (which will be an empty string).
	   note all current options which don't require a value
	   have numeric values (start with a digit) */
	if (isalpha((int)(**nextarg)))
		return(&argv[0][index]);

	/* assume the next arg is the option value */
	*skiparg = 1;

	return(*nextarg);
}
