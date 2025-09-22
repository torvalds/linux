/*	$OpenBSD: sysctl.c,v 1.268 2025/08/06 16:50:53 florian Exp $	*/
/*	$NetBSD: sysctl.c,v 1.9 1995/09/30 07:12:50 thorpej Exp $	*/

/*
 * Copyright (c) 1993
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
 */

#include <sys/types.h>
#include <sys/gmon.h>
#include <sys/mount.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/tty.h>
#include <sys/namei.h>
#include <sys/sched.h>
#include <sys/sensors.h>
#include <sys/vmmeter.h>
#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_ipip.h>
#include <netinet/ip_ether.h>
#include <netinet/ip_ah.h>
#include <netinet/ip_esp.h>
#include <netinet/icmp_var.h>
#include <netinet/igmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/ip_gre.h>
#include <netinet/ip_ipcomp.h>
#include <netinet/ip_carp.h>
#include <netinet/ip_divert.h>

#include <net/pfvar.h>
#include <net/if_pfsync.h>
#include <net/pipex.h>

#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include <netmpls/mpls.h>

#include <uvm/uvm_swap_encrypt.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ffs/ffs_extern.h>

#include <miscfs/fuse/fusefs.h>

#include <nfs/nfsproto.h>
#include <nfs/nfs.h>

#include <ddb/db_var.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/cpu.h>

#ifdef CPU_BIOS
#include <machine/biosvar.h>
#endif

struct ctlname topname[] = CTL_NAMES;
struct ctlname kernname[] = CTL_KERN_NAMES;
struct ctlname vmname[] = CTL_VM_NAMES;
struct ctlname netname[] = CTL_NET_NAMES;
struct ctlname hwname[] = CTL_HW_NAMES;
struct ctlname debugname[CTL_DEBUG_MAXID];
struct ctlname kernmallocname[] = CTL_KERN_MALLOC_NAMES;
struct ctlname forkstatname[] = CTL_KERN_FORKSTAT_NAMES;
struct ctlname nchstatsname[] = CTL_KERN_NCHSTATS_NAMES;
struct ctlname ttysname[] = CTL_KERN_TTY_NAMES;
struct ctlname semname[] = CTL_KERN_SEMINFO_NAMES;
struct ctlname shmname[] = CTL_KERN_SHMINFO_NAMES;
struct ctlname watchdogname[] = CTL_KERN_WATCHDOG_NAMES;
struct ctlname tcname[] = CTL_KERN_TIMECOUNTER_NAMES;
struct ctlname *vfsname;
#ifdef CTL_MACHDEP_NAMES
struct ctlname machdepname[] = CTL_MACHDEP_NAMES;
#endif
struct ctlname ddbname[] = CTL_DDB_NAMES;
struct ctlname audioname[] = CTL_KERN_AUDIO_NAMES;
struct ctlname videoname[] = CTL_KERN_VIDEO_NAMES;
struct ctlname witnessname[] = CTL_KERN_WITNESS_NAMES;
struct ctlname batteryname[] = CTL_HW_BATTERY_NAMES;
char names[BUFSIZ];
int lastused;

/* Maximum size object to expect from sysctl(2) */
#define SYSCTL_BUFSIZ	8192

struct list {
	struct	ctlname *list;
	int	size;
};
struct list toplist = { topname, CTL_MAXID };
struct list secondlevel[] = {
	{ 0, 0 },			/* CTL_UNSPEC */
	{ kernname, KERN_MAXID },	/* CTL_KERN */
	{ vmname, VM_MAXID },		/* CTL_VM */
	{ 0, 0 },			/* was CTL_FS */
	{ netname, NET_MAXID },		/* CTL_NET */
	{ 0, CTL_DEBUG_MAXID },		/* CTL_DEBUG */
	{ hwname, HW_MAXID },		/* CTL_HW */
#ifdef CTL_MACHDEP_NAMES
	{ machdepname, CPU_MAXID },	/* CTL_MACHDEP */
#else
	{ 0, 0 },			/* CTL_MACHDEP */
#endif
	{ 0, 0 },			/* was CTL_USER */
	{ ddbname, DBCTL_MAXID },	/* CTL_DDB_NAMES */
	{ 0, 0 },			/* CTL_VFS */
};

int	Aflag, aflag, nflag, qflag;

time_t boottime;

/*
 * Variables requiring special processing.
 */
#define	CLOCK		0x00000001
#define	BOOTTIME	0x00000002
#define	CHRDEV		0x00000004
#define	BLKDEV		0x00000008
#define	BADDYNAMIC	0x00000020
#define	BIOSGEO		0x00000040
#define	BIOSDEV		0x00000080
#define	MAJ2DEV		0x00000100
#define	UNSIGNED	0x00000200
#define	KMEMBUCKETS	0x00000400
#define	LONGARRAY	0x00000800
#define	KMEMSTATS	0x00001000
#define	SENSORS		0x00002000
#define	SMALLBUF	0x00004000
#define	HEX		0x00008000
#define	TIMEOUT		0x00010000

/* prototypes */
void debuginit(void);
void listall(char *, struct list *);
int parse_hex_char(char);
ssize_t parse_hex_string(unsigned char *, size_t, const char *);
void parse(char *, int);
void parse_baddynamic(int *, size_t, char *, void **, size_t *, int, int);
void usage(void);
int findname(char *, char *, char **, struct list *);
int sysctl_inet(char *, char **, int *, int, int *);
int sysctl_inet6(char *, char **, int *, int, int *);
int sysctl_unix(char *, char **, int *, int, int *);
int sysctl_link(char *, char **, int *, int, int *);
int sysctl_bpf(char *, char **, int *, int, int *);
int sysctl_mpls(char *, char **, int *, int, int *);
int sysctl_pipex(char *, char **, int *, int, int *);
static int sysctl_vfs(char *, char **, int[], int, int *);
static int sysctl_vfsgen(char *, char **, int[], int, int *);
int sysctl_bios(char *, char **, int *, int, int *);
int sysctl_swpenc(char *, char **, int *, int, int *);
int sysctl_forkstat(char *, char **, int *, int, int *);
int sysctl_tty(char *, char **, int *, int, int *);
int sysctl_nchstats(char *, char **, int *, int, int *);
int sysctl_malloc(char *, char **, int *, int, int *);
int sysctl_seminfo(char *, char **, int *, int, int *);
int sysctl_shminfo(char *, char **, int *, int, int *);
int sysctl_watchdog(char *, char **, int *, int, int *);
int sysctl_tc(char *, char **, int *, int, int *);
int sysctl_sensors(char *, char **, int *, int, int *);
void print_sensordev(char *, int *, u_int, struct sensordev *);
void print_sensor(struct sensor *);
#ifdef CPU_CHIPSET
int sysctl_chipset(char *, char **, int *, int, int *);
#endif
int sysctl_audio(char *, char **, int *, int, int *);
int sysctl_video(char *, char **, int *, int, int *);
int sysctl_witness(char *, char **, int *, int, int *);
int sysctl_battery(char *, char **, int *, int, int *);
void vfsinit(void);

char *equ = "=";

int
main(int argc, char *argv[])
{
	int ch, lvl1;
	const char *conffile = NULL;

	while ((ch = getopt(argc, argv, "Aaf:nqw")) != -1) {
		switch (ch) {

		case 'A':
			Aflag = 1;
			break;

		case 'a':
			aflag = 1;
			break;

		case 'f':
			conffile = optarg;
			break;

		case 'n':
			nflag = 1;
			break;

		case 'q':
			qflag = 1;
			break;

		case 'w':
			/* flag no longer needed; var=value implies write */
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	ctime(&boottime); /* satisfy potential $TZ expansion before unveil() */

	if (unveil(_PATH_DEVDB, "r") == -1 && errno != ENOENT)
		err(1,"unveil %s", _PATH_DEVDB);
	if (unveil("/dev", "r") == -1 && errno != ENOENT)
		err(1, "unveil /dev");
	if (conffile != NULL)
		if (unveil(conffile, "r") == -1 && errno != ENOENT)
			err(1, "unveil %s", conffile);
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	if ((argc == 0 && conffile == NULL) || (Aflag || aflag)) {
		debuginit();
		vfsinit();
		for (lvl1 = 1; lvl1 < CTL_MAXID; lvl1++)
			listall(topname[lvl1].ctl_name, &secondlevel[lvl1]);
		return (0);
	}

	if (conffile != NULL) {
		FILE *fp;
		char *line = NULL, *lp;
		size_t sz = 0;

		if ((fp = fopen(conffile, "r")) == NULL)
			err(1, "fopen");

		while (getline(&line, &sz, fp) != -1) {
			lp = line + strspn(line, " \t");
			line[strcspn(line, " \t\n#")] = '\0';

			if (lp[0] != '\0')
				parse(line, 1);
		}

		free(line);
		fclose(fp);
	}

	for (; *argv != NULL; ++argv)
		parse(*argv, 1);
	return (0);
}

/*
 * List all variables known to the system.
 */
void
listall(char *prefix, struct list *lp)
{
	char *cp, name[BUFSIZ];
	int lvl2, len;

	if (lp->list == NULL)
		return;
	if ((len = strlcpy(name, prefix, sizeof(name))) >= sizeof(name))
		errx(1, "%s: name too long", prefix);
	cp = name + len++;
	*cp++ = '.';
	for (lvl2 = 0; lvl2 < lp->size; lvl2++) {
		if (lp->list[lvl2].ctl_name == NULL)
			continue;
		if (strlcpy(cp, lp->list[lvl2].ctl_name,
		    sizeof(name) - len) >= sizeof(name) - len)
			warn("%s: name too long", lp->list[lvl2].ctl_name);
		parse(name, Aflag);
	}
}

int
parse_hex_char(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');

	ch = tolower((unsigned char)ch);
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 10);

	return (-1);
}

ssize_t
parse_hex_string(unsigned char *dst, size_t dstlen, const char *src)
{
	ssize_t len = 0;
	int digit;

	while (len < dstlen) {
		if (*src == '\0')
			return (len);

		digit = parse_hex_char(*src++);
		if (digit == -1)
			return (-1);
		dst[len] = digit << 4;

		digit = parse_hex_char(*src++);
		if (digit == -1)
			return (-1);
		
		dst[len] |= digit;
		len++;
	}

	while (*src != '\0') {
		if (parse_hex_char(*src++) == -1 ||
		    parse_hex_char(*src++) == -1)
			return (-1);

		len++;
	}

	return (len);
}

/*
 * Parse a name into a MIB entry.
 * Lookup and print out the MIB entry if it exists.
 * Set a new value if requested.
 */
void
parse(char *string, int flags)
{
	int indx, type, state, intval, len;
	size_t size, newsize = 0;
	int lal = 0, special = 0;
	void *newval = NULL;
	int64_t quadval;
	struct list *lp;
	int mib[CTL_MAXNAME];
	char *cp, *bufp, buf[SYSCTL_BUFSIZ];
	unsigned char hex[SYSCTL_BUFSIZ];

	(void)strlcpy(buf, string, sizeof(buf));
	bufp = buf;
	if ((cp = strchr(string, '=')) != NULL) {
		*strchr(buf, '=') = '\0';
		*cp++ = '\0';
		while (isspace((unsigned char)*cp))
			cp++;
		newval = cp;
		newsize = strlen(cp);
	}
	if ((indx = findname(string, "top", &bufp, &toplist)) == -1)
		return;
	mib[0] = indx;
	if (indx == CTL_VFS)
		vfsinit();
	if (indx == CTL_DEBUG)
		debuginit();
	lp = &secondlevel[indx];
	if (lp->list == 0) {
		warnx("%s: class is not implemented", topname[indx].ctl_name);
		return;
	}
	if (bufp == NULL) {
		listall(topname[indx].ctl_name, lp);
		return;
	}
	if ((indx = findname(string, "second", &bufp, lp)) == -1)
		return;
	mib[1] = indx;
	type = lp->list[indx].ctl_type;
	len = 2;
	switch (mib[0]) {

	case CTL_KERN:
		switch (mib[1]) {
		case KERN_PROF:
			mib[2] = GPROF_STATE;
			mib[3] = 0; /* Assume CPU ID 0 is always valid. */
			size = sizeof(state);
			if (sysctl(mib, 4, &state, &size, NULL, 0) == -1) {
				if (flags == 0)
					return;
				if (!nflag)
					(void)printf("%s: ", string);
				(void)puts("kernel is not compiled for profiling");
				return;
			}
			if (!nflag)
				(void)printf("%s = %s\n", string,
				    state == GMON_PROF_OFF ? "off" : "running");
			return;
		case KERN_FORKSTAT:
			sysctl_forkstat(string, &bufp, mib, flags, &type);
			return;
		case KERN_TTY:
			len = sysctl_tty(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		case KERN_NCHSTATS:
			sysctl_nchstats(string, &bufp, mib, flags, &type);
			return;
		case KERN_MALLOCSTATS:
			len = sysctl_malloc(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			if (mib[2] == KERN_MALLOC_BUCKET)
				special |= KMEMBUCKETS;
			if (mib[2] == KERN_MALLOC_KMEMSTATS)
				special |= KMEMSTATS;
			newsize = 0;
			break;
		case KERN_MBSTAT:
			if (flags == 0)
				return;
			warnx("use netstat to view %s", string);
			return;
		case KERN_MSGBUF:
			if (flags == 0)
				return;
			warnx("use dmesg to view %s", string);
			return;
		case KERN_PROC:
			if (flags == 0)
				return;
			warnx("use ps to view %s information", string);
			return;
		case KERN_CLOCKRATE:
			special |= CLOCK;
			break;
		case KERN_BOOTTIME:
			special |= BOOTTIME;
			break;
		case KERN_HOSTID:
			special |= UNSIGNED;
			special |= SMALLBUF;
			break;
		case KERN_CPTIME:
			special |= LONGARRAY;
			lal = CPUSTATES;
			break;
		case KERN_SEMINFO:
			len = sysctl_seminfo(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		case KERN_SHMINFO:
			len = sysctl_shminfo(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		case KERN_INTRCNT:
			if (flags == 0)
				return;
			warnx("use vmstat or systat to view %s information",
			    string);
			return;
		case KERN_WATCHDOG:
			len = sysctl_watchdog(string, &bufp, mib, flags,
			    &type);
			if (len < 0)
				return;
			break;
		case KERN_TIMECOUNTER:
			len = sysctl_tc(string, &bufp, mib, flags,
			    &type);
			if (len < 0)
				return;
			break;
		case KERN_FILE:
			if (flags == 0)
				return;
			warnx("use fstat to view %s information", string);
			return;
		case KERN_CONSDEV:
			special |= CHRDEV;
			break;
		case KERN_NETLIVELOCKS:
		case KERN_SOMAXCONN:
		case KERN_SOMINCONN:
			special |= UNSIGNED;
			break;
		case KERN_AUDIO:
			len = sysctl_audio(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		case KERN_VIDEO:
			len = sysctl_video(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		case KERN_WITNESS:
			len = sysctl_witness(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		case KERN_PFSTATUS:
			if (flags == 0)
				return;
			warnx("use pfctl to view %s information", string);
			return;
		case KERN_TIMEOUT_STATS:
			special |= TIMEOUT;
			break;
		}
		break;

	case CTL_HW:
		switch (mib[1]) {
		case HW_DISKSTATS:
			/*
			 * Only complain if someone asks explicitly for this,
			 * otherwise "fail" silently.
			 */
			if (flags)
				warnx("use vmstat to view %s information",
				    string);
			return;
		case HW_SENSORS:
			special |= SENSORS;
			len = sysctl_sensors(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		case HW_BATTERY:
			len = sysctl_battery(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		case HW_PHYSMEM:
		case HW_USERMEM:
			/*
			 * Don't print these; we'll print the 64-bit
			 * variants instead.
			 */
			return;
		}
		break;

	case CTL_VM:
		if (mib[1] == VM_LOADAVG) {
			double loads[3];

			getloadavg(loads, 3);
			if (!nflag)
				(void)printf("%s%s", string, equ);
			(void)printf("%.2f %.2f %.2f\n", loads[0],
			    loads[1], loads[2]);
			return;
		} else if (mib[1] == VM_PSSTRINGS) {
			struct _ps_strings _ps;

			size = sizeof(_ps);
			if (sysctl(mib, 2, &_ps, &size, NULL, 0) == -1) {
				if (flags == 0)
					return;
				if (!nflag)
					(void)printf("%s: ", string);
				(void)puts("can't find ps strings");
				return;
			}
			if (!nflag)
				(void)printf("%s%s", string, equ);
			(void)printf("%p\n", _ps.val);
			return;
		} else if (mib[1] == VM_SWAPENCRYPT) {
			len = sysctl_swpenc(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;

			break;
		} else if (mib[1] == VM_NKMEMPAGES ||
		    mib[1] == VM_ANONMIN ||
		    mib[1] == VM_VTEXTMIN ||
		    mib[1] == VM_VNODEMIN ||
		    mib[1] == VM_MALLOC_CONF) {
			break;
		}
		if (flags == 0)
			return;
		warnx("use vmstat or systat to view %s information", string);
		return;

		break;

	case CTL_NET:
		if (mib[1] == PF_INET) {
			len = sysctl_inet(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;

			if ((mib[2] == IPPROTO_IP && mib[3] == IPCTL_MRTSTATS) ||
			    (mib[2] == IPPROTO_IP && mib[3] == IPCTL_STATS) ||
			    (mib[2] == IPPROTO_IP && mib[3] == IPCTL_MRTMFC) ||
			    (mib[2] == IPPROTO_IP && mib[3] == IPCTL_MRTVIF) ||
			    (mib[2] == IPPROTO_TCP && mib[3] == TCPCTL_STATS) ||
			    (mib[2] == IPPROTO_UDP && mib[3] == UDPCTL_STATS) ||
			    (mib[2] == IPPROTO_ESP && mib[3] == ESPCTL_STATS) ||
			    (mib[2] == IPPROTO_AH && mib[3] == AHCTL_STATS) ||
			    (mib[2] == IPPROTO_IGMP && mib[3] == IGMPCTL_STATS) ||
			    (mib[2] == IPPROTO_ETHERIP && mib[3] == ETHERIPCTL_STATS) ||
			    (mib[2] == IPPROTO_IPIP && mib[3] == IPIPCTL_STATS) ||
			    (mib[2] == IPPROTO_IPCOMP && mib[3] == IPCOMPCTL_STATS) ||
			    (mib[2] == IPPROTO_ICMP && mib[3] == ICMPCTL_STATS) ||
			    (mib[2] == IPPROTO_CARP && mib[3] == CARPCTL_STATS) ||
			    (mib[2] == IPPROTO_PFSYNC && mib[3] == PFSYNCCTL_STATS) ||
			    (mib[2] == IPPROTO_DIVERT && mib[3] == DIVERTCTL_STATS)) {
				if (flags == 0)
					return;
				warnx("use netstat to view %s information",
				    string);
				return;
			} else if ((mib[2] == IPPROTO_TCP &&
			    (mib[3] == TCPCTL_BADDYNAMIC ||
			    mib[3] == TCPCTL_ROOTONLY)) ||
			    (mib[2] == IPPROTO_UDP &&
			    (mib[3] == UDPCTL_BADDYNAMIC ||
			    mib[3] == UDPCTL_ROOTONLY))) {

				special |= BADDYNAMIC;

				if (newval != NULL)
					parse_baddynamic(mib, len, string,
					    &newval, &newsize, flags, nflag);
			}
			break;
		}
		if (mib[1] == PF_INET6) {
			len = sysctl_inet6(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;

			if ((mib[2] == IPPROTO_IPV6 && mib[3] == IPV6CTL_MRTMFC) ||
			    (mib[2] == IPPROTO_IPV6 && mib[3] == IPV6CTL_MRTMIF)) {
				if (flags == 0)
					return;
				warnx("use netstat to view %s information",
				    string);
				return;
			}
			break;
		}
		if (mib[1] == PF_UNIX) {
			len = sysctl_unix(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		}
		if (mib[1] == PF_LINK) {
			len = sysctl_link(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		}
		if (mib[1] == PF_BPF) {
			len = sysctl_bpf(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		}
		if (mib[1] == PF_MPLS) {
			len = sysctl_mpls(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		}
		if (mib[1] == PF_PIPEX) {
			len = sysctl_pipex(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		}
		if (flags == 0)
			return;
		warnx("use netstat to view %s information", string);
		return;

	case CTL_DEBUG:
		mib[2] = CTL_DEBUG_VALUE;
		len = 3;
		break;

	case CTL_MACHDEP:
#ifdef CPU_CONSDEV
		if (mib[1] == CPU_CONSDEV)
			special |= CHRDEV;
#endif
#ifdef CPU_CPUID
		if (mib[1] == CPU_CPUID)
			special |= HEX;
#endif
#ifdef CPU_CPUFEATURE
		if (mib[1] == CPU_CPUFEATURE)
			special |= HEX;
#endif
#ifdef CPU_BLK2CHR
		if (mib[1] == CPU_BLK2CHR) {
			if (bufp == NULL)
				return;
			mib[2] = makedev(atoi(bufp),0);
			bufp = NULL;
			len = 3;
			special |= CHRDEV;
			break;
		}
#endif
#ifdef CPU_CHR2BLK
		if (mib[1] == CPU_CHR2BLK) {
			if (bufp == NULL)
				return;
			mib[2] = makedev(atoi(bufp),0);
			bufp = NULL;
			len = 3;
			special |= BLKDEV;
			break;
		}
#endif
#ifdef CPU_BIOS
		if (mib[1] == CPU_BIOS) {
			len = sysctl_bios(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			if (mib[2] == BIOS_DEV)
				special |= BIOSDEV;
			if (mib[2] == BIOS_DISKINFO)
				special |= BIOSGEO;
			break;
		}
#endif
#ifdef CPU_CHIPSET
		if (mib[1] == CPU_CHIPSET) {
			len = sysctl_chipset(string, &bufp, mib, flags, &type);
			if (len < 0)
				return;
			break;
		}
#endif
		break;

	case CTL_VFS:
		if (mib[1])
			len = sysctl_vfs(string, &bufp, mib, flags, &type);
		else
			len = sysctl_vfsgen(string, &bufp, mib, flags, &type);
		if (len >= 0) {
			if (type == CTLTYPE_STRUCT) {
				if (flags)
					warnx("use nfsstat to view %s information",
					    MOUNT_NFS);
				return;
			} else
				break;
		}
		return;

	case CTL_DDB:
		break;

	default:
		warnx("illegal top level value: %d", mib[0]);
		return;

	}
	if (bufp) {
		warnx("name %s in %s is unknown", bufp, string);
		return;
	}
	if (newsize > 0) {
		const char *errstr;

		switch (type) {
		case CTLTYPE_INT:
			if (special & UNSIGNED)
				intval = strtonum(newval, 0, UINT_MAX, &errstr);
			else
				intval = strtonum(newval, INT_MIN, INT_MAX,
				    &errstr);
			if (errstr != NULL) {
				warnx("%s: value is %s: %s", string, errstr,
				    (char *)newval);
				return;
			}
			newval = &intval;
			newsize = sizeof(intval);
			break;

		case CTLTYPE_QUAD:
			(void)sscanf(newval, "%lld", &quadval);
			newval = &quadval;
			newsize = sizeof(quadval);
			break;
		case CTLTYPE_STRING:
			if (special & HEX) {
				ssize_t len;

				len = parse_hex_string(hex, sizeof(hex),
				    newval);
				if (len == -1) {
					warnx("%s: hex string %s: invalid",
					    string, (char *)newval);
					return;
				}
				if (len > sizeof(hex)) {
					warnx("%s: hex string %s: too long",
					    string, (char *)newval);
					return;
				}

				newval = hex;
				newsize = len;
			}
			break;
		}
	}
	size = (special & SMALLBUF) ? 512 : SYSCTL_BUFSIZ;
	if (sysctl(mib, len, buf, &size, newval, newsize) == -1) {
		if (flags == 0)
			return;
		switch (errno) {
		case EOPNOTSUPP:
			warnx("%s: value is not available", string);
			return;
		case ENOTDIR:
			warnx("%s: specification is incomplete", string);
			return;
		case ENOMEM:
			warnx("%s: type is unknown to this program", string);
			return;
		case ENXIO:
			if (special & BIOSGEO)
				return;
		default:
			warn("%s", string);
			return;
		}
	}
	if (special & KMEMBUCKETS) {
		struct kmembuckets *kb = (struct kmembuckets *)buf;
		if (!nflag)
			(void)printf("%s%s", string, equ);
		printf("(");
		printf("calls = %llu ", (long long)kb->kb_calls);
		printf("total_allocated = %llu ", (long long)kb->kb_total);
		printf("total_free = %lld ", (long long)kb->kb_totalfree);
		printf("elements = %lld ", (long long)kb->kb_elmpercl);
		printf("high watermark = %lld ", (long long)kb->kb_highwat);
		printf("could_free = %lld", (long long)kb->kb_couldfree);
		printf(")\n");
		return;
	}
	if (special & KMEMSTATS) {
		struct kmemstats *km = (struct kmemstats *)buf;
		int j, first = 1;

		if (!nflag)
			(void)printf("%s%s", string, equ);
		(void)printf("(inuse = %ld, calls = %ld, memuse = %ldK, "
		    "limblocks = %d, maxused = %ldK, "
		    "limit = %ldK, spare = %ld, sizes = (",
		    km->ks_inuse, km->ks_calls,
		    (km->ks_memuse + 1023) / 1024, km->ks_limblocks,
		    (km->ks_maxused + 1023) / 1024,
		    (km->ks_limit + 1023) / 1024, km->ks_spare);
		for (j = 1 << MINBUCKET; j < 1 << (MINBUCKET + 16); j <<= 1) {
			if ((km->ks_size & j ) == 0)
				continue;
			if (first)
				(void)printf("%d", j);
			else
				(void)printf(",%d", j);
			first = 0;
		}
		if (first)
			(void)printf("none");
		(void)printf("))\n");
		return;
	}
	if (special & CLOCK) {
		struct clockinfo *clkp = (struct clockinfo *)buf;

		if (!nflag)
			(void)printf("%s%s", string, equ);
		(void)printf(
		    "tick = %d, hz = %d, profhz = %d, stathz = %d\n",
		    clkp->tick, clkp->hz, clkp->profhz, clkp->stathz);
		return;
	}
	if (special & BOOTTIME) {
		struct timeval *btp = (struct timeval *)buf;

		if (!nflag) {
			char *ct;
			boottime = btp->tv_sec;
			ct = ctime(&boottime);
			if (ct)
				(void)printf("%s%s%s", string, equ, ct);
			else
				(void)printf("%s%s%lld\n", string, equ,
				    boottime);
		} else
			(void)printf("%lld\n", (long long)btp->tv_sec);
		return;
	}
	if (special & BLKDEV) {
		dev_t dev = *(dev_t *)buf;

		if (!nflag)
			(void)printf("%s%s%s\n", string, equ,
			    devname(dev, S_IFBLK));
		else
			(void)printf("0x%x\n", dev);
		return;
	}
	if (special & CHRDEV) {
		dev_t dev = *(dev_t *)buf;

		if (!nflag)
			(void)printf("%s%s%s\n", string, equ,
			    devname(dev, S_IFCHR));
		else
			(void)printf("0x%x\n", dev);
		return;
	}
#ifdef CPU_BIOS
	if (special & BIOSGEO) {
		bios_diskinfo_t *pdi = (bios_diskinfo_t *)buf;

		if (!nflag)
			(void)printf("%s%s", string, equ);
		(void)printf("bootdev = 0x%x, "
		    "cylinders = %u, heads = %u, sectors = %u\n",
		    pdi->bsd_dev, pdi->bios_cylinders,
		    pdi->bios_heads, pdi->bios_sectors);
		return;
	}
	if (special & BIOSDEV) {
		int dev = *(int*)buf;

		if (!nflag)
			(void)printf("%s%s", string, equ);
		(void) printf("0x%02x\n", dev);
		return;
	}
#endif
	if (special & UNSIGNED) {
		if (newsize == 0) {
			if (!nflag)
				(void)printf("%s%s", string, equ);
			(void)printf("%u\n", *(u_int *)buf);
		} else {
			if (!qflag) {
				if (!nflag)
					(void)printf("%s: %u -> ", string,
					    *(u_int *)buf);
				(void)printf("%u\n", *(u_int *)newval);
			}
		}
		return;
	}
	if (special & BADDYNAMIC) {
		u_int port, lastport;
		u_int32_t *baddynamic = (u_int32_t *)buf;

		if (!qflag) {
			if (!nflag)
				(void)printf("%s%s", string,
				    newsize ? ": " : equ);
			lastport = 0;
			for (port = 0; port < 65536; port++)
				if (DP_ISSET(baddynamic, port)) {
					(void)printf("%s%u",
					    lastport ? "," : "", port);
					lastport = port;
				}
			if (newsize != 0) {
				if (!nflag)
					fputs(" -> ", stdout);
				baddynamic = (u_int32_t *)newval;
				lastport = 0;
				for (port = 0; port < 65536; port++)
					if (DP_ISSET(baddynamic, port)) {
						(void)printf("%s%u",
						    lastport ? "," : "", port);
						lastport = port;
					}
			}
			(void)putchar('\n');
		}
		return;
	}
	if (special & LONGARRAY) {
		long *la = (long *)buf;
		if (!nflag)
			printf("%s%s", string, equ);
		while (lal--)
			printf("%ld%s", *la++, lal? ",":"");
		putchar('\n');
		return;
	}
	if (special & SENSORS) {
		struct sensor *s = (struct sensor *)buf;

		if (size > 0 && (s->flags & SENSOR_FINVALID) == 0) {
			if (!nflag)
				printf("%s%s", string, equ);
			print_sensor(s);
			printf("\n");
		}
		return;
	}
	if (special & TIMEOUT) {
		struct timeoutstat *tstat = (struct timeoutstat *)buf;

		if (!nflag)
			printf("%s%s", string, equ);
		printf("added = %llu, cancelled = %llu, deleted = %llu, "
		    "late = %llu, pending = %llu, readded = %llu, "
		    "scheduled = %llu, rescheduled = %llu, "
		    "run_softclock = %llu, run_thread = %llu, "
		    "softclocks = %llu, thread_wakeups = %llu\n",
		    tstat->tos_added, tstat->tos_cancelled, tstat->tos_deleted,
		    tstat->tos_late, tstat->tos_pending, tstat->tos_readded,
		    tstat->tos_scheduled, tstat->tos_rescheduled,
		    tstat->tos_run_softclock, tstat->tos_run_thread,
		    tstat->tos_softclocks, tstat->tos_thread_wakeups);
		return;
	}
	switch (type) {
	case CTLTYPE_INT:
		if (newsize == 0) {
			if (!nflag)
				(void)printf("%s%s", string, equ);
			if (special & HEX)
				(void)printf("0x%x\n", *(int *)buf);
			else
				(void)printf("%d\n", *(int *)buf);
		} else {
			if (!qflag) {
				if (!nflag)
					(void)printf("%s: %d -> ", string,
					    *(int *)buf);
				if (special & HEX)
					(void)printf("0x%x\n", *(int *)newval);
				else
					(void)printf("%d\n", *(int *)newval);
			}
		}
		return;

	case CTLTYPE_STRING:
		if (newval == NULL) {
			if (!nflag)
				(void)printf("%s%s", string, equ);
			if (special & HEX) {
				size_t i;
				for (i = 0; i < size; i++) {
					(void)printf("%02x",
					    (unsigned char)buf[i]);
				}
				(void)printf("\n");
			} else
				(void)puts(buf);
		} else if (!qflag) {
			if (!nflag) {
				(void)printf("%s: ", string);
				if (special & HEX) {
					size_t i;
					for (i = 0; i < size; i++) {
						(void)printf("%02x",
						    (unsigned char)buf[i]);
					}
				} else
					(void)printf("%s", buf);

				(void)printf(" -> ");
			}
			(void)puts(cp);
		}
		return;

	case CTLTYPE_QUAD:
		if (newsize == 0) {
			int64_t tmp;

			memcpy(&tmp, buf, sizeof tmp);
			if (!nflag)
				(void)printf("%s%s", string, equ);
			(void)printf("%lld\n", tmp);
		} else {
			int64_t tmp;

			memcpy(&tmp, buf, sizeof tmp);
			if (!qflag) {
				if (!nflag)
					(void)printf("%s: %lld -> ",
					    string, tmp);
				memcpy(&tmp, newval, sizeof tmp);
				(void)printf("%lld\n", tmp);
			}
		}
		return;

	case CTLTYPE_STRUCT:
		warnx("%s: unknown structure returned", string);
		return;

	default:
	case CTLTYPE_NODE:
		warnx("%s: unknown type returned", string);
		return;
	}
}

static void
parse_ports(char *portspec, int *port, int *high_port)
{
	char *dash;
	const char *errstr;

	if ((dash = strchr(portspec, '-')) != NULL)
		*dash++ = '\0';
	*port = strtonum(portspec, 0, 65535, &errstr);
	if (errstr != NULL)
		errx(1, "port is %s: %s", errstr, portspec);
	if (dash != NULL) {
		*high_port = strtonum(dash, 0, 65535, &errstr);
		if (errstr != NULL)
			errx(1, "high port is %s: %s", errstr, dash);
		if (*high_port < *port)
			errx(1, "high port %d is lower than %d",
			    *high_port, *port);
	} else
		*high_port = *port;
}

void
parse_baddynamic(int mib[], size_t len, char *string, void **newvalp,
    size_t *newsizep, int flags, int nflag)
{
	static u_int32_t newbaddynamic[DP_MAPSIZE];
	int port, high_port, baddynamic_loaded = 0, full_list_set = 0;
	size_t size;
	char action, *cp;

	while (*newvalp && (cp = strsep((char **)newvalp, ", \t")) && *cp) {
		if (*cp == '+' || *cp == '-') {
			if (full_list_set)
				errx(1, "cannot mix +/- with full list");
			action = *cp++;
			if (!baddynamic_loaded) {
				size = sizeof(newbaddynamic);
				if (sysctl(mib, len, newbaddynamic,
				    &size, 0, 0) == -1) {
					if (flags == 0)
						return;
					if (!nflag)
						printf("%s: ", string);
					puts("kernel does not contain bad "
					    "dynamic port tables");
					return;
				}
				baddynamic_loaded = 1;
			}
			parse_ports(cp, &port, &high_port);
			for (; port <= high_port; port++) {
				if (action == '+')
					DP_SET(newbaddynamic, port);
				else
					DP_CLR(newbaddynamic, port);
			}
		} else {
			if (baddynamic_loaded)
				errx(1, "cannot mix +/- with full list");
			if (!full_list_set) {
				bzero(newbaddynamic, sizeof(newbaddynamic));
				full_list_set = 1;
			}
			parse_ports(cp, &port, &high_port);
			for (; port <= high_port; port++)
				DP_SET(newbaddynamic, port);
		}
	}
	*newvalp = (void *)newbaddynamic;
	*newsizep = sizeof(newbaddynamic);
}

/*
 * Initialize the set of debugging names
 */
void
debuginit(void)
{
	int mib[3], loc, i;
	size_t size;

	if (secondlevel[CTL_DEBUG].list != 0)
		return;
	secondlevel[CTL_DEBUG].list = debugname;
	mib[0] = CTL_DEBUG;
	mib[2] = CTL_DEBUG_NAME;
	for (loc = lastused, i = 0; i < CTL_DEBUG_MAXID; i++) {
		mib[1] = i;
		size = BUFSIZ - loc;
		if (sysctl(mib, 3, &names[loc], &size, NULL, 0) == -1)
			continue;
		debugname[i].ctl_name = &names[loc];
		debugname[i].ctl_type = CTLTYPE_INT;
		loc += size;
	}
	lastused = loc;
}

struct ctlname vfsgennames[] = CTL_VFSGENCTL_NAMES;
struct ctlname ffsname[] = FFS_NAMES;
struct ctlname nfsname[] = FS_NFS_NAMES;
struct ctlname fusefsname[] = FUSEFS_NAMES;
struct list *vfsvars;
int *vfs_typenums;

/*
 * Initialize the set of filesystem names
 */
void
vfsinit(void)
{
	int mib[4], maxtypenum, cnt, loc, size;
	struct vfsconf vfc;
	size_t buflen;

	if (secondlevel[CTL_VFS].list != 0)
		return;
	mib[0] = CTL_VFS;
	mib[1] = VFS_GENERIC;
	mib[2] = VFS_MAXTYPENUM;
	buflen = 4;
	if (sysctl(mib, 3, &maxtypenum, &buflen, NULL, 0) == -1)
		return;
	/*
         * We need to do 0..maxtypenum so add one, and then we offset them
	 * all by (another) one by inserting VFS_GENERIC entries at zero
	 */
	maxtypenum += 2;
	if ((vfs_typenums = calloc(maxtypenum, sizeof(int))) == NULL)
		return;
	if ((vfsvars = calloc(maxtypenum, sizeof(*vfsvars))) == NULL) {
		free(vfs_typenums);
		return;
	}
	if ((vfsname = calloc(maxtypenum, sizeof(*vfsname))) == NULL) {
		free(vfs_typenums);
		free(vfsvars);
		return;
	}
	mib[2] = VFS_CONF;
	buflen = sizeof vfc;
	for (loc = lastused, cnt = 1; cnt < maxtypenum; cnt++) {
		mib[3] = cnt - 1;
		if (sysctl(mib, 4, &vfc, &buflen, NULL, 0) == -1) {
			if (errno == EOPNOTSUPP)
				continue;
			warn("vfsinit");
			free(vfsname);
			free(vfsvars);
			free(vfs_typenums);
			return;
		}
		if (!strcmp(vfc.vfc_name, MOUNT_FFS)) {
			vfsvars[cnt].list = ffsname;
			vfsvars[cnt].size = FFS_MAXID;
		}
		if (!strcmp(vfc.vfc_name, MOUNT_NFS)) {
			vfsvars[cnt].list = nfsname;
			vfsvars[cnt].size = NFS_MAXID;
		}
		if (!strcmp(vfc.vfc_name, MOUNT_FUSEFS)) {
			vfsvars[cnt].list = fusefsname;
			vfsvars[cnt].size = FUSEFS_MAXID;
		}
		vfs_typenums[cnt] = vfc.vfc_typenum;
		strlcat(&names[loc], vfc.vfc_name, sizeof names - loc);
		vfsname[cnt].ctl_name = &names[loc];
		vfsname[cnt].ctl_type = CTLTYPE_NODE;
		size = strlen(vfc.vfc_name) + 1;
		loc += size;
	}
	lastused = loc;

	vfsname[0].ctl_name = "mounts";
	vfsname[0].ctl_type = CTLTYPE_NODE;
	vfsvars[0].list = vfsname + 1;
	vfsvars[0].size = maxtypenum - 1;

	secondlevel[CTL_VFS].list = vfsname;
	secondlevel[CTL_VFS].size = maxtypenum;
	return;
}

int
sysctl_vfsgen(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;
	size_t size;
	struct vfsconf vfc;

	if (*bufpp == NULL) {
		listall(string, vfsvars);
		return (-1);
	}

	if ((indx = findname(string, "third", bufpp, vfsvars)) == -1)
		return (-1);

	mib[1] = VFS_GENERIC;
	mib[2] = VFS_CONF;
	mib[3] = indx;
	size = sizeof vfc;
	if (sysctl(mib, 4, &vfc, &size, NULL, 0) == -1) {
		if (errno != EOPNOTSUPP)
			warn("vfs print");
		return -1;
	}
	if (flags == 0 && vfc.vfc_refcount == 0)
		return -1;
	if (!nflag)
		fprintf(stdout, "%s has %u mounted instance%s\n",
		    string, vfc.vfc_refcount,
		    vfc.vfc_refcount != 1 ? "s" : "");
	else
		fprintf(stdout, "%u\n", vfc.vfc_refcount);

	return -1;
}

int
sysctl_vfs(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	struct list *lp = &vfsvars[mib[1]];
	int indx;

	if (lp->list == NULL) {
		if (flags)
			warnx("No variables defined for file system %s", string);
		return (-1);
	}
	if (*bufpp == NULL) {
		listall(string, lp);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, lp)) == -1)
		return (-1);

	mib[1] = vfs_typenums[mib[1]];
	mib[2] = indx;
	*typep = lp->list[indx].ctl_type;
	return (3);
}

#ifdef CPU_BIOS
struct ctlname biosname[] = CTL_BIOS_NAMES;
struct list bioslist = { biosname, BIOS_MAXID };

/*
 * handle BIOS requests
 */
int
sysctl_bios(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	char *name;
	int indx;

	if (*bufpp == NULL) {
		listall(string, &bioslist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &bioslist)) == -1)
		return (-1);
	mib[2] = indx;
	if (indx == BIOS_DISKINFO) {
		const char *errstr;

		if (*bufpp == NULL) {
			char name[BUFSIZ];

			/* scan all the bios devices */
			for (indx = 0; indx < 256; indx++) {
				snprintf(name, sizeof(name), "%s.%u",
				    string, indx);
				parse(name, 1);
			}
			return (-1);
		}
		if ((name = strsep(bufpp, ".")) == NULL) {
			warnx("%s: incomplete specification", string);
			return (-1);
		}
		mib[3] = strtonum(name, 0, INT_MAX, &errstr);
		if (errstr) {
			warnx("%s: %s", string, errstr);
			return (-1);
		}
		*typep = CTLTYPE_STRUCT;
		return (4);
	} else {
		*typep = bioslist.list[indx].ctl_type;
		return (3);
	}
}
#endif

struct ctlname swpencname[] = CTL_SWPENC_NAMES;
struct list swpenclist = { swpencname, SWPENC_MAXID };

/*
 * handle swap encrypt requests
 */
int
sysctl_swpenc(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &swpenclist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &swpenclist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = swpenclist.list[indx].ctl_type;
	return (3);
}

struct ctlname inetname[] = CTL_IPPROTO_NAMES;
struct ctlname ipname[] = IPCTL_NAMES;
struct ctlname icmpname[] = ICMPCTL_NAMES;
struct ctlname igmpname[] = IGMPCTL_NAMES;
struct ctlname ipipname[] = IPIPCTL_NAMES;
struct ctlname tcpname[] = TCPCTL_NAMES;
struct ctlname udpname[] = UDPCTL_NAMES;
struct ctlname espname[] = ESPCTL_NAMES;
struct ctlname ahname[] = AHCTL_NAMES;
struct ctlname etheripname[] = ETHERIPCTL_NAMES;
struct ctlname grename[] = GRECTL_NAMES;
struct ctlname ipcompname[] = IPCOMPCTL_NAMES;
struct ctlname carpname[] = CARPCTL_NAMES;
struct ctlname pfsyncname[] = PFSYNCCTL_NAMES;
struct ctlname divertname[] = DIVERTCTL_NAMES;
struct ctlname bpfname[] = CTL_NET_BPF_NAMES;
struct ctlname ifqname[] = CTL_IFQ_NAMES;
struct ctlname pipexname[] = PIPEXCTL_NAMES;
struct list inetlist = { inetname, IPPROTO_MAXID };
struct list inetvars[] = {
	{ ipname, IPCTL_MAXID },	/* ip */
	{ icmpname, ICMPCTL_MAXID },	/* icmp */
	{ igmpname, IGMPCTL_MAXID },	/* igmp */
	{ 0, 0 },			/* ggmp */
	{ ipipname, IPIPCTL_MAXID },	/* ipencap */
	{ 0, 0 },
	{ tcpname, TCPCTL_MAXID },	/* tcp */
	{ 0, 0 },
	{ 0, 0 },			/* egp */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },			/* pup */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ udpname, UDPCTL_MAXID },	/* udp */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ grename, GRECTL_MAXID },	/* gre */
	{ 0, 0 },
	{ 0, 0 },
	{ espname, ESPCTL_MAXID },	/* esp */
	{ ahname, AHCTL_MAXID },	/* ah */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ etheripname, ETHERIPCTL_MAXID },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ ipcompname, IPCOMPCTL_MAXID },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ carpname, CARPCTL_MAXID },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ pfsyncname, PFSYNCCTL_MAXID },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ divertname, DIVERTCTL_MAXID },
};
struct list bpflist = { bpfname, NET_BPF_MAXID };
struct list ifqlist = { ifqname, IFQCTL_MAXID };
struct list pipexlist = { pipexname, PIPEXCTL_MAXID };

struct list kernmalloclist = { kernmallocname, KERN_MALLOC_MAXID };
struct list forkstatlist = { forkstatname, KERN_FORKSTAT_MAXID };
struct list nchstatslist = { nchstatsname, KERN_NCHSTATS_MAXID };
struct list ttylist = { ttysname, KERN_TTY_MAXID };
struct list semlist = { semname, KERN_SEMINFO_MAXID };
struct list shmlist = { shmname, KERN_SHMINFO_MAXID };
struct list watchdoglist = { watchdogname, KERN_WATCHDOG_MAXID };
struct list tclist = { tcname, KERN_TIMECOUNTER_MAXID };
struct list audiolist = { audioname, KERN_AUDIO_MAXID };
struct list videolist = { videoname, KERN_VIDEO_MAXID };
struct list witnesslist = { witnessname, KERN_WITNESS_MAXID };
struct list batterylist = { batteryname, HW_BATTERY_MAXID };

/*
 * handle vfs namei cache statistics
 */
int
sysctl_nchstats(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	static struct nchstats nch;
	int indx;
	size_t size;
	static int keepvalue = 0;

	if (*bufpp == NULL) {
		bzero(&nch, sizeof(struct nchstats));
		listall(string, &nchstatslist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &nchstatslist)) == -1)
		return (-1);
	mib[2] = indx;
	if (*bufpp != NULL) {
		warnx("fourth level name in %s is invalid", string);
		return (-1);
	}
	if (keepvalue == 0) {
		size = sizeof(struct nchstats);
		if (sysctl(mib, 2, &nch, &size, NULL, 0) == -1)
			return (-1);
		keepvalue = 1;
	}
	if (!nflag)
		(void)printf("%s%s", string, equ);
	switch (indx) {
	case KERN_NCHSTATS_GOODHITS:
		(void)printf("%llu\n", nch.ncs_goodhits);
		break;
	case KERN_NCHSTATS_NEGHITS:
		(void)printf("%llu\n", nch.ncs_neghits);
		break;
	case KERN_NCHSTATS_BADHITS:
		(void)printf("%llu\n", nch.ncs_badhits);
		break;
	case KERN_NCHSTATS_FALSEHITS:
		(void)printf("%llu\n", nch.ncs_falsehits);
		break;
	case KERN_NCHSTATS_MISS:
		(void)printf("%llu\n", nch.ncs_miss);
		break;
	case KERN_NCHSTATS_LONG:
		(void)printf("%llu\n", nch.ncs_long);
		break;
	case KERN_NCHSTATS_PASS2:
		(void)printf("%llu\n", nch.ncs_pass2);
		break;
	case KERN_NCHSTATS_2PASSES:
		(void)printf("%llu\n", nch.ncs_2passes);
		break;
	case KERN_NCHSTATS_REVHITS:
		(void)printf("%llu\n", nch.ncs_revhits);
		break;
	case KERN_NCHSTATS_REVMISS:
		(void)printf("%llu\n", nch.ncs_revmiss);
		break;
	case KERN_NCHSTATS_DOTHITS:
		(void)printf("%llu\n", nch.ncs_dothits);
		break;
	case KERN_NCHSTATS_DOTDOTHITS:
		(void)printf("%llu\n", nch.ncs_dotdothits);
		break;
	}
	return (-1);
}

/*
 * handle tty statistics
 */
int
sysctl_tty(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &ttylist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &ttylist)) == -1)
		return (-1);
	mib[2] = indx;

	if ((*typep = ttylist.list[indx].ctl_type) == CTLTYPE_STRUCT) {
		if (flags)
			warnx("use pstat -t to view %s information",
			    string);
		return (-1);
	}
	return (3);
}

/*
 * handle fork statistics
 */
int
sysctl_forkstat(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	static struct forkstat fks;
	static int keepvalue = 0;
	int indx;
	size_t size;

	if (*bufpp == NULL) {
		bzero(&fks, sizeof(struct forkstat));
		listall(string, &forkstatlist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &forkstatlist)) == -1)
		return (-1);
	if (*bufpp != NULL) {
		warnx("fourth level name in %s is invalid", string);
		return (-1);
	}
	if (keepvalue == 0) {
		size = sizeof(struct forkstat);
		if (sysctl(mib, 2, &fks, &size, NULL, 0) == -1)
			return (-1);
		keepvalue = 1;
	}
	if (!nflag)
		(void)printf("%s%s", string, equ);
	switch (indx)	{
	case KERN_FORKSTAT_FORK:
		(void)printf("%u\n", fks.cntfork);
		break;
	case KERN_FORKSTAT_VFORK:
		(void)printf("%u\n", fks.cntvfork);
		break;
	case KERN_FORKSTAT_TFORK:
		(void)printf("%u\n", fks.cnttfork);
		break;
	case KERN_FORKSTAT_KTHREAD:
		(void)printf("%u\n", fks.cntkthread);
		break;
	case KERN_FORKSTAT_SIZFORK:
		(void)printf("%llu\n", fks.sizfork);
		break;
	case KERN_FORKSTAT_SIZVFORK:
		(void)printf("%llu\n", fks.sizvfork);
		break;
	case KERN_FORKSTAT_SIZTFORK:
		(void)printf("%llu\n", fks.siztfork);
		break;
	case KERN_FORKSTAT_SIZKTHREAD:
		(void)printf("%llu\n", fks.sizkthread);
		break;
	}
	return (-1);
}

/*
 * handle malloc statistics
 */
int
sysctl_malloc(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx, stor, i;
	char *name, bufp[SYSCTL_BUFSIZ], *buf, *ptr;
	const char *errstr;
	struct list lp;
	size_t size;

	if (*bufpp == NULL) {
		listall(string, &kernmalloclist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &kernmalloclist)) == -1)
		return (-1);
	mib[2] = indx;
	if (mib[2] == KERN_MALLOC_BUCKET) {
		if ((name = strsep(bufpp, ".")) == NULL) {
			size = SYSCTL_BUFSIZ;
			stor = mib[2];
			mib[2] = KERN_MALLOC_BUCKETS;
			buf = bufp;
			if (sysctl(mib, 3, buf, &size, NULL, 0) == -1)
				return (-1);
			mib[2] = stor;
			for (stor = 0, i = 0; i < size; i++)
				if (buf[i] == ',')
					stor++;
			lp.list = calloc(stor + 2, sizeof(struct ctlname));
			if (lp.list == NULL)
				return (-1);
			lp.size = stor + 2;
			for (i = 1; (ptr = strsep(&buf, ",")) != NULL; i++) {
			        lp.list[i].ctl_name = ptr;
				lp.list[i].ctl_type = CTLTYPE_STRUCT;
			}
			listall(string, &lp);
			free(lp.list);
			return (-1);
		}
		mib[3] = strtonum(name, 0, INT_MAX, &errstr);
		if (errstr)
			return -1;
		return (4);
	} else if (mib[2] == KERN_MALLOC_BUCKETS) {
		*typep = CTLTYPE_STRING;
		return (3);
	} else if (mib[2] == KERN_MALLOC_KMEMSTATS) {
		size = SYSCTL_BUFSIZ;
		stor = mib[2];
		mib[2] = KERN_MALLOC_KMEMNAMES;
		buf = bufp;
		if (sysctl(mib, 3, buf, &size, NULL, 0) == -1)
			return (-1);
		mib[2] = stor;
		if ((name = strsep(bufpp, ".")) == NULL) {
			for (stor = 0, i = 0; i < size; i++)
				if (buf[i] == ',')
					stor++;
			lp.list = calloc(stor + 2, sizeof(struct ctlname));
			if (lp.list == NULL)
				return (-1);
			lp.size = stor + 2;
			for (i = 1; (ptr = strsep(&buf, ",")) != NULL; i++) {
				if (ptr[0] == '\0') {
					i--;
					continue;
				}
			    	lp.list[i].ctl_name = ptr;
				lp.list[i].ctl_type = CTLTYPE_STRUCT;
			}
			listall(string, &lp);
			free(lp.list);
			return (-1);
		}
		ptr = strstr(buf, name);
 tryagain:
		if (ptr == NULL) {
			warnx("fourth level name %s in %s is invalid", name,
			    string);
			return (-1);
		}
		if ((*(ptr + strlen(name)) != ',') &&
		    (*(ptr + strlen(name)) != '\0')) {
			ptr = strstr(ptr + 1, name); /* retry */
			goto tryagain;
		}
		if ((ptr != buf) && (*(ptr - 1) != ',')) {
			ptr = strstr(ptr + 1, name); /* retry */
			goto tryagain;
		}
		for (i = 0, stor = 0; buf + i < ptr; i++)
			if (buf[i] == ',')
				stor++;
		mib[3] = stor;
		return (4);
	} else if (mib[2] == KERN_MALLOC_KMEMNAMES) {
		*typep = CTLTYPE_STRING;
		return (3);
	}
	return (-1);
}

#ifdef CPU_CHIPSET
/*
 * handle machdep.chipset requests
 */
struct ctlname chipsetname[] = CTL_CHIPSET_NAMES;
struct list chipsetlist = { chipsetname, CPU_CHIPSET_MAXID };

int
sysctl_chipset(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx, bwx;
	static void *q;
	size_t len;
	char *p;

	if (*bufpp == NULL) {
		listall(string, &chipsetlist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &chipsetlist)) == -1)
		return (-1);
	mib[2] = indx;
	if (!nflag)
		printf("%s%s", string, equ);
	switch(mib[2]) {
	case CPU_CHIPSET_MEM:
	case CPU_CHIPSET_DENSE:
	case CPU_CHIPSET_PORTS:
	case CPU_CHIPSET_HAE_MASK:
		len = sizeof(void *);
		if (sysctl(mib, 3, &q, &len, NULL, 0) == -1)
			goto done;
		printf("%p", q);
		break;
	case CPU_CHIPSET_BWX:
		len = sizeof(int);
		if (sysctl(mib, 3, &bwx, &len, NULL, 0) == -1)
			goto done;
		printf("%d", bwx);
		break;
	case CPU_CHIPSET_TYPE:
		if (sysctl(mib, 3, NULL, &len, NULL, 0) == -1)
			goto done;
		p = malloc(len + 1);
		if (p == NULL)
			goto done;
		if (sysctl(mib, 3, p, &len, NULL, 0) == -1) {
			free(p);
			goto done;
		}
		p[len] = '\0';
		printf("%s", p);
		free(p);
		break;
	}
done:
	printf("\n");
	return (-1);
}
#endif
/*
 * handle internet requests
 */
int
sysctl_inet(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	struct list *lp;
	int indx;

	if (*bufpp == NULL) {
		listall(string, &inetlist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &inetlist)) == -1)
		return (-1);
	mib[2] = indx;
	if (indx < IPPROTO_MAXID && inetvars[indx].list != NULL)
		lp = &inetvars[indx];
	else if (!flags)
		return (-1);
	else {
		warnx("%s: no variables defined for this protocol", string);
		return (-1);
	}
	if (*bufpp == NULL) {
		listall(string, lp);
		return (-1);
	}
	if ((indx = findname(string, "fourth", bufpp, lp)) == -1)
		return (-1);
	mib[3] = indx;
	*typep = lp->list[indx].ctl_type;
	if (*typep == CTLTYPE_NODE) {
		int tindx;

		if (*bufpp == NULL) {
			listall(string, &ifqlist);
			return(-1);
		}
		lp = &ifqlist;
		if ((tindx = findname(string, "fifth", bufpp, lp)) == -1)
			return (-1);
		mib[4] = tindx;
		*typep = lp->list[tindx].ctl_type;
		return(5);
	}
	return (4);
}

struct ctlname inet6name[] = CTL_IPV6PROTO_NAMES;
struct ctlname ip6name[] = IPV6CTL_NAMES;
struct ctlname icmp6name[] = ICMPV6CTL_NAMES;
struct list inet6list = { inet6name, IPV6PROTO_MAXID };
struct list inet6vars[] = {
/*0*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
/*10*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*20*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*30*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*40*/	{ 0, 0 },
	{ ip6name, IPV6CTL_MAXID },	/* ipv6 */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*50*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ icmp6name, ICMPV6CTL_MAXID },	/* icmp6 */
	{ 0, 0 },
/*60*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*70*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*80*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*90*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*100*/	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },	/* pim6 */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
/*110*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*120*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*130*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*140*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*150*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*160*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*170*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*180*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*190*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*200*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*210*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*220*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*230*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*240*/	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
/*250*/	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
};

/*
 * handle internet6 requests
 */
int
sysctl_inet6(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	struct list *lp;
	int indx;

	if (*bufpp == NULL) {
		listall(string, &inet6list);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &inet6list)) == -1)
		return (-1);
	mib[2] = indx;
	if (indx < IPV6PROTO_MAXID && inet6vars[indx].list != NULL)
		lp = &inet6vars[indx];
	else if (!flags)
		return (-1);
	else {
		warnx("%s: no variables defined for this protocol", string);
		return (-1);
	}
	if (*bufpp == NULL) {
		listall(string, lp);
		return (-1);
	}
	if ((indx = findname(string, "fourth", bufpp, lp)) == -1)
		return (-1);
	mib[3] = indx;
	*typep = lp->list[indx].ctl_type;
	if (*typep == CTLTYPE_NODE) {
		int tindx;

		if (*bufpp == NULL) {
			listall(string, &ifqlist);
			return(-1);
		}
		lp = &ifqlist;
		if ((tindx = findname(string, "fifth", bufpp, lp)) == -1)
			return (-1);
		mib[4] = tindx;
		*typep = lp->list[tindx].ctl_type;
		return(5);
	}
	return (4);
}

/* handle net.unix requests */
struct ctlname netunixname[] = CTL_NET_UNIX_NAMES;
struct ctlname netunixprotoname[] = CTL_NET_UNIX_PROTO_NAMES;
struct list netunixlist = { netunixname, NET_UNIX_MAXID };
struct list netunixvars[] = {
	[SOCK_STREAM] = { netunixprotoname, NET_UNIX_PROTO_MAXID },
	[SOCK_DGRAM] = { netunixprotoname, NET_UNIX_PROTO_MAXID },
	[SOCK_SEQPACKET] = { netunixprotoname, NET_UNIX_PROTO_MAXID },
	[NET_UNIX_MAXID] = { 0, 0 },
};

int
sysctl_unix(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	struct list *lp;
	int indx;

	if (*bufpp == NULL) {
		listall(string, &netunixlist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &netunixlist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = netunixname[indx].ctl_type;

	if (indx < NET_UNIX_MAXID && netunixvars[indx].list != NULL)
		lp = &netunixvars[indx];
	else
		return (3);

	if (*bufpp == NULL) {
		listall(string, lp);
		return (-1);
	}
	if ((indx = findname(string, "fourth", bufpp, lp)) == -1)
		return (-1);
	mib[3] = indx;
	*typep = lp->list[indx].ctl_type;
	return (4);
}

/* handle net.link requests */
struct ctlname netlinkname[] = CTL_NET_LINK_NAMES;
struct ctlname ifrxqname[] = CTL_NET_LINK_IFRXQ_NAMES;
struct list netlinklist = { netlinkname, NET_LINK_MAXID };
struct list netlinkvars[] = {
	[NET_LINK_IFRXQ] = { ifrxqname, NET_LINK_IFRXQ_MAXID },
};

int
sysctl_link(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	struct list *lp;
	int indx;

	if (*bufpp == NULL) {
		listall(string, &netlinklist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &netlinklist)) == -1)
		return (-1);
	mib[2] = indx;
	if (indx < NET_LINK_MAXID && netlinkvars[indx].list != NULL)
		lp = &netlinkvars[indx];
	else if (!flags)
		return (-1);
	else {
		warnx("%s: no variables defined for this protocol", string);
		return (-1);
	}
	if (*bufpp == NULL) {
		listall(string, lp);
		return (-1);
	}
	if ((indx = findname(string, "fourth", bufpp, lp)) == -1)
		return (-1);
	mib[3] = indx;
	*typep = lp->list[indx].ctl_type;
	return (4);
}

/* handle bpf requests */
int
sysctl_bpf(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &bpflist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &bpflist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = CTLTYPE_INT;
	return (3);
}

struct ctlname mplsname[] = MPLSCTL_NAMES;
struct list mplslist = { mplsname, MPLSCTL_MAXID };

/* handle MPLS requests */
int
sysctl_mpls(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	struct list *lp;
	int indx;

	if (*bufpp == NULL) {
		listall(string, &mplslist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &mplslist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = mplslist.list[indx].ctl_type;
	if (*typep == CTLTYPE_NODE) {
		int tindx;

		if (*bufpp == NULL) {
			listall(string, &ifqlist);
			return(-1);
		}
		lp = &ifqlist;
		if ((tindx = findname(string, "fourth", bufpp, lp)) == -1)
			return (-1);
		mib[3] = tindx;
		*typep = lp->list[tindx].ctl_type;
		return(4);
	}
	return (3);
}

/* handle PIPEX requests */
int
sysctl_pipex(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	struct list *lp;
	int indx;

	if (*bufpp == NULL) {
		listall(string, &pipexlist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &pipexlist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = pipexlist.list[indx].ctl_type;
	if (*typep == CTLTYPE_NODE) {
		int tindx;

		if (*bufpp == NULL) {
			listall(string, &ifqlist);
			return(-1);
		}
		lp = &ifqlist;
		if ((tindx = findname(string, "fourth", bufpp, lp)) == -1)
			return (-1);
		mib[3] = tindx;
		*typep = lp->list[tindx].ctl_type;
		return(4);
	}
	return (3);
}

/*
 * Handle SysV semaphore info requests
 */
int
sysctl_seminfo(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &semlist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &semlist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = CTLTYPE_INT;
	return (3);
}

/*
 * Handle SysV shared memory info requests
 */
int
sysctl_shminfo(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &shmlist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &shmlist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = CTLTYPE_INT;
	return (3);
}

/*
 * Handle watchdog support
 */
int
sysctl_watchdog(char *string, char **bufpp, int mib[], int flags,
    int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &watchdoglist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &watchdoglist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = watchdoglist.list[indx].ctl_type;
	return (3);
}

/*
 * Handle timecounter support
 */
int
sysctl_tc(char *string, char **bufpp, int mib[], int flags,
    int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &tclist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &tclist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = tclist.list[indx].ctl_type;
	return (3);
}

/*
 * Handle hardware monitoring sensors support
 */
int
sysctl_sensors(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	char *devname, *typename;
	int dev, numt, i;
	enum sensor_type type;
	struct sensordev snsrdev;
	size_t sdlen = sizeof(snsrdev);

	if (*bufpp == NULL) {
		char buf[SYSCTL_BUFSIZ];

		/* scan all sensor devices */
		for (dev = 0; ; dev++) {
			mib[2] = dev;
			if (sysctl(mib, 3, &snsrdev, &sdlen, NULL, 0) == -1) {
				if (errno == ENXIO)
					continue;
				if (errno == ENOENT)
					break;
				warn("sensors dev %d", dev);
				return (-1);
			}
			snprintf(buf, sizeof(buf), "%s.%s",
			    string, snsrdev.xname);
			print_sensordev(buf, mib, 3, &snsrdev);
		}
		return (-1);
	}

	/*
	 * If we get this far, it means that some arguments were
	 * provided below hw.sensors tree.
	 * The first branch of hw.sensors tree is the device name.
	 */
	if ((devname = strsep(bufpp, ".")) == NULL) {
		warnx("%s: incomplete specification", string);
		return (-1);
	}
	/* convert sensor device string to an integer */
	for (dev = 0; ; dev++) {
		mib[2] = dev;
		if (sysctl(mib, 3, &snsrdev, &sdlen, NULL, 0) == -1) {
			if (errno == ENXIO)
				continue;
			if (errno == ENOENT)
				break;
			warn("sensors dev %d", dev);
			return (-1);
		}
		if (strcmp(devname, snsrdev.xname) == 0)
			break;
	}
	if (strcmp(devname, snsrdev.xname) != 0) {
		warnx("%s: sensor device not found: %s", string, devname);
		return (-1);
	}
	if (*bufpp == NULL) {
		/* only device name was provided -- let's print all sensors
		 * that are attached to the specified device
		 */
		print_sensordev(string, mib, 3, &snsrdev);
		return (-1);
	}

	/*
	 * At this point we have identified the sensor device,
	 * now let's go further and identify sensor type.
	 */
	if ((typename = strsep(bufpp, ".")) == NULL) {
		warnx("%s: incomplete specification", string);
		return (-1);
	}
	numt = -1;
	for (i = 0; typename[i] != '\0'; i++)
		if (isdigit((unsigned char)typename[i])) {
			const char *errstr;

			numt = strtonum(&typename[i], 0, INT_MAX, &errstr);
			if (errstr) {
				warnx("%s: %s", string, errstr);
				return (-1);
			}
			typename[i] = '\0';
			break;
		}
	for (type = 0; type < SENSOR_MAX_TYPES; type++)
		if (strcmp(typename, sensor_type_s[type]) == 0)
			break;
	if (type == SENSOR_MAX_TYPES) {
		warnx("%s: sensor type not recognised: %s", string, typename);
		return (-1);
	}
	mib[3] = type;

	/*
	 * If no integer was provided after sensor_type, let's
	 * print all sensors of the specified type.
	 */
	if (numt == -1) {
		print_sensordev(string, mib, 4, &snsrdev);
		return (-1);
	}

	/*
	 * At this point we know that we have received a direct request
	 * via command-line for a specific sensor. Let's have the parse()
	 * function deal with it further, and report any errors if such
	 * sensor node does not exist.
	 */
	mib[4] = numt;
	*typep = CTLTYPE_STRUCT;
	return (5);
}

/*
 * Print sensors from the specified device.
 */

void
print_sensordev(char *string, int mib[], u_int mlen, struct sensordev *snsrdev)
{
	char buf[SYSCTL_BUFSIZ];
	enum sensor_type type;

	if (mlen == 3) {
		for (type = 0; type < SENSOR_MAX_TYPES; type++) {
			mib[3] = type;
			snprintf(buf, sizeof(buf), "%s.%s",
			    string, sensor_type_s[type]);
			print_sensordev(buf, mib, mlen+1, snsrdev);
		}
		return;
	}

	if (mlen == 4) {
		int numt;

		type = mib[3];
		for (numt = 0; numt < snsrdev->maxnumt[type]; numt++) {
			mib[4] = numt;
			snprintf(buf, sizeof(buf), "%s%u", string, numt);
			print_sensordev(buf, mib, mlen+1, snsrdev);
		}
		return;
	}

	if (mlen == 5) {
		struct sensor snsr;
		size_t slen = sizeof(snsr);

		/* this function is only printing sensors in bulk, so we
		 * do not return any error messages if the requested sensor
		 * is not found by sysctl(3)
		 */
		if (sysctl(mib, 5, &snsr, &slen, NULL, 0) == -1)
			return;

		if (slen > 0 && (snsr.flags & SENSOR_FINVALID) == 0) {
			if (!nflag)
				printf("%s%s", string, equ);
			print_sensor(&snsr);
			printf("\n");
		}
		return;
	}
}

void
print_sensor(struct sensor *s)
{
	const char *name;

	if (s->flags & SENSOR_FUNKNOWN)
		printf("unknown");
	else {
		switch (s->type) {
		case SENSOR_TEMP:
			printf("%.2f degC",
			    (s->value - 273150000) / 1000000.0);
			break;
		case SENSOR_FANRPM:
			printf("%lld RPM", s->value);
			break;
		case SENSOR_VOLTS_DC:
			printf("%.2f VDC", s->value / 1000000.0);
			break;
		case SENSOR_VOLTS_AC:
			printf("%.2f VAC", s->value / 1000000.0);
			break;
		case SENSOR_OHMS:
			printf("%lld ohm", s->value);
			break;
		case SENSOR_WATTS:
			printf("%.2f W", s->value / 1000000.0);
			break;
		case SENSOR_AMPS:
			printf("%.2f A", s->value / 1000000.0);
			break;
		case SENSOR_WATTHOUR:
			printf("%.2f Wh", s->value / 1000000.0);
			break;
		case SENSOR_AMPHOUR:
			printf("%.2f Ah", s->value / 1000000.0);
			break;
		case SENSOR_INDICATOR:
			printf("%s", s->value ? "On" : "Off");
			break;
		case SENSOR_INTEGER:
			printf("%lld", s->value);
			break;
		case SENSOR_PERCENT:
			printf("%.2f%%", s->value / 1000.0);
			break;
		case SENSOR_LUX:
			printf("%.2f lx", s->value / 1000000.0);
			break;
		case SENSOR_DRIVE:
			switch (s->value) {
			case SENSOR_DRIVE_EMPTY:
				name = "empty";
				break;
			case SENSOR_DRIVE_READY:
				name = "ready";
				break;
			case SENSOR_DRIVE_POWERUP:
				name = "powering up";
				break;
			case SENSOR_DRIVE_ONLINE:
				name = "online";
				break;
			case SENSOR_DRIVE_IDLE:
				name = "idle";
				break;
			case SENSOR_DRIVE_ACTIVE:
				name = "active";
				break;
			case SENSOR_DRIVE_REBUILD:
				name = "rebuilding";
				break;
			case SENSOR_DRIVE_POWERDOWN:
				name = "powering down";
				break;
			case SENSOR_DRIVE_FAIL:
				name = "failed";
				break;
			case SENSOR_DRIVE_PFAIL:
				name = "degraded";
				break;
			default:
				name = "unknown";
				break;
			}
			printf("%s", name);
			break;
		case SENSOR_TIMEDELTA:
			printf("%.6f secs", s->value / 1000000000.0);
			break;
		case SENSOR_HUMIDITY:
			printf("%.2f%%", s->value / 1000.0);
			break;
		case SENSOR_FREQ:
			printf("%.2f Hz", s->value / 1000000.0);
			break;
		case SENSOR_ANGLE:
			printf("%3.4f degrees", s->value / 1000000.0);
			break;
		case SENSOR_DISTANCE:
			printf("%.3f m", s->value / 1000000.0);
			break;
		case SENSOR_PRESSURE:
			printf("%.2f Pa", s->value / 1000.0);
			break;
		case SENSOR_ACCEL:
			printf("%2.4f m/s^2", s->value / 1000000.0);
			break;
		case SENSOR_VELOCITY:
			printf("%4.3f m/s", s->value / 1000000.0);
			break;
		case SENSOR_ENERGY:
			printf("%.2f J", s->value / 1000000.0);
			break;
		default:
			printf("unknown");
		}
	}

	if (s->desc[0] != '\0')
		printf(" (%s)", s->desc);

	switch (s->status) {
	case SENSOR_S_UNSPEC:
		break;
	case SENSOR_S_OK:
		printf(", OK");
		break;
	case SENSOR_S_WARN:
		printf(", WARNING");
		break;
	case SENSOR_S_CRIT:
		printf(", CRITICAL");
		break;
	case SENSOR_S_UNKNOWN:
		printf(", UNKNOWN");
		break;
	}

	if (s->tv.tv_sec) {
		time_t t = s->tv.tv_sec;
		char ct[26];

		if (ctime_r(&t, ct)) {
			ct[19] = '\0';
			printf(", %s.%03ld", ct, s->tv.tv_usec / 1000);
		} else
			printf(", %lld.%03ld", t, s->tv.tv_usec / 1000);
	}
}

/*
 * Handle audio support
 */
int
sysctl_audio(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &audiolist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &audiolist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = audiolist.list[indx].ctl_type;
	return (3);
}

/*
 * Handle video support
 */
int
sysctl_video(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &videolist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &videolist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = videolist.list[indx].ctl_type;
	return (3);
}

/*
 * Handle witness support
 */
int
sysctl_witness(char *string, char **bufpp, int mib[], int flags, int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &witnesslist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &witnesslist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = witnesslist.list[indx].ctl_type;
	return (3);
}

/*
 * Handle battery support
 */
int
sysctl_battery(char *string, char **bufpp, int mib[], int flags,
    int *typep)
{
	int indx;

	if (*bufpp == NULL) {
		listall(string, &batterylist);
		return (-1);
	}
	if ((indx = findname(string, "third", bufpp, &batterylist)) == -1)
		return (-1);
	mib[2] = indx;
	*typep = batterylist.list[indx].ctl_type;
	return (3);
}

/*
 * Scan a list of names searching for a particular name.
 */
int
findname(char *string, char *level, char **bufp, struct list *namelist)
{
	char *name;
	int i;

	if (namelist->list == 0 || (name = strsep(bufp, ".")) == NULL) {
		warnx("%s: incomplete specification", string);
		return (-1);
	}
	for (i = 0; i < namelist->size; i++)
		if (namelist->list[i].ctl_name != NULL &&
		    strcmp(name, namelist->list[i].ctl_name) == 0)
			break;
	if (i == namelist->size) {
		warnx("%s level name %s in %s is invalid", level, name, string);
		return (-1);
	}
	return (i);
}

void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: sysctl [-Aanq] [-f file] [name[=value] ...]\n");
	exit(1);
}
