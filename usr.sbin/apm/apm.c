/*
 * APM BIOS utility for FreeBSD
 *
 * Copyright (C) 1994-1996 by Tatsumi Hosokawa <hosokawa@jp.FreeBSD.org>
 *
 * This software may be used, modified, copied, distributed, and sold,
 * in both source and binary form provided that the above copyright and
 * these terms are retained. Under no circumstances is the author
 * responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its
 * use.
 *
 * Sep., 1994	Implemented on FreeBSD 1.1.5.1R (Toshiba AVS001WD)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include <machine/apm_bios.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define APMDEV	"/dev/apm"

#define APM_UNKNOWN	255

#define xh(a)	(((a) & 0xff00) >> 8)
#define xl(a)	((a) & 0xff)
#define APMERR(a) xh(a)

static int cmos_wall = 0; /* True when wall time is in cmos clock, else UTC */

static void
usage(void)
{
	fprintf(stderr,
		"usage: apm [-ablstzZ] [-d enable ] [ -e enable ] "
		"[ -h enable ] [-r delta]\n");
	exit(1);
}

/*
 * Return 1 for boolean true, and 0 for false, according to the
 * interpretation of the string argument given.
 */
static int
is_true(const char *boolean)
{
	char *endp;
	long val;

	val = strtoul(boolean, &endp, 0);
	if (*endp == '\0')
		return (val != 0 ? 1 : 0);
	if (strcasecmp(boolean, "true") == 0 ||
	    strcasecmp(boolean, "yes") == 0 ||
	    strcasecmp(boolean, "enable") == 0)
		return (1);
	if (strcasecmp(boolean, "false") == 0 ||
	    strcasecmp(boolean, "no") == 0 ||
	    strcasecmp(boolean, "disable") == 0)
		return (0);
	/* Well, I have no idea what the user wants, so... */
	warnx("invalid boolean argument \"%s\"", boolean);
	usage();
	/* NOTREACHED */

	return (0);
}

static int
int2bcd(int i)
{
	int retval = 0;
	int base = 0;

	if (i >= 10000)
		return -1;
    
	while (i) {
		retval |= (i % 10) << base;
		i /= 10;
		base += 4;
	}
	return retval;
}

static int
bcd2int(int bcd)
{
	int retval = 0;
	int place = 1;

	if (bcd > 0x9999)
		return -1;

	while (bcd) {
		retval += (bcd & 0xf) * place;
		bcd >>= 4;
		place *= 10;
	}
	return retval;
}

static void 
apm_suspend(int fd)
{
	if (ioctl(fd, APMIO_SUSPEND, NULL) == -1)
		err(1, "ioctl(APMIO_SUSPEND)");
}

static void 
apm_standby(int fd)
{
	if (ioctl(fd, APMIO_STANDBY, NULL) == -1)
		err(1, "ioctl(APMIO_STANDBY)");
}

static void 
apm_getinfo(int fd, apm_info_t aip)
{
	if (ioctl(fd, APMIO_GETINFO, aip) == -1)
		err(1, "ioctl(APMIO_GETINFO)");
}

static void 
apm_enable(int fd, int enable) 
{
	if (enable) {
		if (ioctl(fd, APMIO_ENABLE) == -1)
			err(1, "ioctl(APMIO_ENABLE)");
	} else {
		if (ioctl(fd, APMIO_DISABLE) == -1)
			err(1, "ioctl(APMIO_DISABLE)");
	}
}

static void
print_batt_time(int batt_time)
{
	printf("Remaining battery time: ");
	if (batt_time == -1)
		printf("unknown\n");
	else {
		int h, m, s;

		h = batt_time;
		s = h % 60;
		h /= 60;
		m = h % 60;
		h /= 60;
		printf("%2d:%02d:%02d\n", h, m, s);
	}
}

static void
print_batt_life(u_int batt_life)
{
	printf("Remaining battery life: ");
	if (batt_life == APM_UNKNOWN)
		printf("unknown\n");
	else if (batt_life <= 100)
		printf("%d%%\n", batt_life);
	else
		printf("invalid value (0x%x)\n", batt_life);
}

static void
print_batt_stat(u_int batt_stat)
{
	const char *batt_msg[] = { "high", "low", "critical", "charging" };

	printf("Battery Status: ");
	if (batt_stat == APM_UNKNOWN)
		printf("unknown\n");
	else if (batt_stat > 3)
		printf("invalid value (0x%x)\n", batt_stat);
	else
		printf("%s\n", batt_msg[batt_stat]);
}

static void 
print_all_info(int fd, apm_info_t aip, int bioscall_available)
{
	struct apm_bios_arg args;
	int apmerr;
	const char *line_msg[] = { "off-line", "on-line" , "backup power"};

	printf("APM version: %d.%d\n", aip->ai_major, aip->ai_minor);
	printf("APM Management: %s\n", aip->ai_status ? "Enabled" : "Disabled");
	printf("AC Line status: ");
	if (aip->ai_acline == APM_UNKNOWN)
		printf("unknown\n");
	else if (aip->ai_acline > 2)
		printf("invalid value (0x%x)\n", aip->ai_acline);
	else
		printf("%s\n", line_msg[aip->ai_acline]);

	print_batt_stat(aip->ai_batt_stat);
	print_batt_life(aip->ai_batt_life);
	print_batt_time(aip->ai_batt_time);

	if (aip->ai_infoversion >= 1) {
		printf("Number of batteries: ");
		if (aip->ai_batteries == ~0U)
			printf("unknown\n");
		else {
			u_int i;
			struct apm_pwstatus aps;

			printf("%d\n", aip->ai_batteries);
			for (i = 0; i < aip->ai_batteries; ++i) {
				bzero(&aps, sizeof(aps));
				aps.ap_device = PMDV_BATT0 + i;
				if (ioctl(fd, APMIO_GETPWSTATUS, &aps) == -1)
					continue;
				printf("Battery %d:\n", i);
				if (aps.ap_batt_flag & APM_BATT_NOT_PRESENT) {
					printf("not present\n");
					continue;
				}
				printf("\t");
				print_batt_stat(aps.ap_batt_stat);
				printf("\t");
				print_batt_life(aps.ap_batt_life);
				printf("\t");
				print_batt_time(aps.ap_batt_time);
			}
		}
	}

	if (bioscall_available) {
		/*
		 * try to get the suspend timer
		 */
		bzero(&args, sizeof(args));
		args.eax = (APM_BIOS) << 8 | APM_RESUMETIMER;
		args.ebx = PMDV_APMBIOS;
		args.ecx = 0x0001;
		if (ioctl(fd, APMIO_BIOS, &args)) {
			printf("Resume timer: unknown\n");
		} else {
			apmerr = APMERR(args.eax);
			if (apmerr == 0x0d || apmerr == 0x86)
				printf("Resume timer: disabled\n");
			else if (apmerr)
				warnx(
		"failed to get the resume timer: APM error0x%x", apmerr);
			else {
				/*
				 * OK.  We have the time (all bcd).
				 * CH - seconds
				 * DH - hours
				 * DL - minutes
				 * xh(SI) - month (1-12)
				 * xl(SI) - day of month (1-31)
				 * DI - year
				 */
				struct tm tm;
				char buf[1024];
				time_t t;

				tm.tm_sec = bcd2int(xh(args.ecx));
				tm.tm_min = bcd2int(xl(args.edx));
				tm.tm_hour = bcd2int(xh(args.edx));
				tm.tm_mday = bcd2int(xl(args.esi));
				tm.tm_mon = bcd2int(xh(args.esi)) - 1;
				tm.tm_year = bcd2int(args.edi) - 1900;
				if (cmos_wall)
					t = mktime(&tm);
				else
					t = timegm(&tm);
				if (t != -1) {
					tm = *localtime(&t);
					strftime(buf, sizeof(buf), "%c", &tm);
					printf("Resume timer: %s\n", buf);
				} else
					printf("Resume timer: unknown\n");
			}
		}

		/*
		 * Get the ring indicator resume state
		 */
		bzero(&args, sizeof(args));
		args.eax  = (APM_BIOS) << 8 | APM_RESUMEONRING;
		args.ebx = PMDV_APMBIOS;
		args.ecx = 0x0002;
		if (ioctl(fd, APMIO_BIOS, &args) == 0) {
			printf("Resume on ring indicator: %sabled\n",
			    args.ecx ? "en" : "dis");
		}
	}

	if (aip->ai_infoversion >= 1) {
		if (aip->ai_capabilities == 0xff00)
		    return;
		printf("APM Capabilities:\n");
		if (aip->ai_capabilities & 0x01)
			printf("\tglobal standby state\n");
		if (aip->ai_capabilities & 0x02)
			printf("\tglobal suspend state\n");
		if (aip->ai_capabilities & 0x04)
			printf("\tresume timer from standby\n");
		if (aip->ai_capabilities & 0x08)
			printf("\tresume timer from suspend\n");
		if (aip->ai_capabilities & 0x10)
			printf("\tRI resume from standby\n");
		if (aip->ai_capabilities & 0x20)
			printf("\tRI resume from suspend\n");
		if (aip->ai_capabilities & 0x40)
			printf("\tPCMCIA RI resume from standby\n");
		if (aip->ai_capabilities & 0x80)
			printf("\tPCMCIA RI resume from suspend\n");
	}

}

/*
 * currently, it can turn off the display, but the display never comes
 * back until the machine suspend/resumes :-).
 */
static void 
apm_display(int fd, int newstate)
{
	if (ioctl(fd, APMIO_DISPLAY, &newstate) == -1)
		err(1, "ioctl(APMIO_DISPLAY)");
}

static void
apm_haltcpu(int fd, int enable)
{
	if (enable) {
		if (ioctl(fd, APMIO_HALTCPU, NULL) == -1)
			err(1, "ioctl(APMIO_HALTCPU)");
	} else {
		if (ioctl(fd, APMIO_NOTHALTCPU, NULL) == -1)
			err(1, "ioctl(APMIO_NOTHALTCPU)");
	}
}

static void
apm_set_timer(int fd, int delta)
{
	time_t tmr;
	struct tm *tm;
	struct apm_bios_arg args;

	tmr = time(NULL) + delta;
	if (cmos_wall)
		tm = localtime(&tmr);
	else
		tm = gmtime(&tmr);
	bzero(&args, sizeof(args));
	args.eax = (APM_BIOS) << 8 | APM_RESUMETIMER;
	args.ebx = PMDV_APMBIOS;
	if (delta > 0) {
		args.ecx = (int2bcd(tm->tm_sec) << 8) | 0x02;
		args.edx = (int2bcd(tm->tm_hour) << 8) | int2bcd(tm->tm_min);
		args.esi = (int2bcd(tm->tm_mon + 1) << 8) | int2bcd(tm->tm_mday);
		args.edi = int2bcd(tm->tm_year + 1900);
	} else {
		args.ecx = 0x0000;
	}
	if (ioctl(fd, APMIO_BIOS, &args)) {
		err(1,"set resume timer");
	}
}

int 
main(int argc, char *argv[])
{
	int	c, fd;
	int     dosleep = 0, all_info = 1, apm_status = 0, batt_status = 0;
	int     display = -1, batt_life = 0, ac_status = 0, standby = 0;
	int	batt_time = 0, delta = 0, enable = -1, haltcpu = -1;
	int	bioscall_available = 0;
	size_t	cmos_wall_len = sizeof(cmos_wall);

	if (sysctlbyname("machdep.wall_cmos_clock", &cmos_wall, &cmos_wall_len,
	    NULL, 0) == -1)
		err(1, "sysctlbyname(machdep.wall_cmos_clock)");

	while ((c = getopt(argc, argv, "abe:h:lRr:stzd:Z")) != -1) {
		switch (c) {
		case 'a':
			ac_status = 1;
			all_info = 0;
			break;
		case 'b':
			batt_status = 1;
			all_info = 0;
			break;
		case 'd':
			display = is_true(optarg);
			all_info = 0;
			break;
		case 'l':
			batt_life = 1;
			all_info = 0;
			break;
		case 'R':
			delta = -1;
			break;
		case 'r':
			delta = atoi(optarg);
			break;
		case 's':
			apm_status = 1;
			all_info = 0;
			break;
		case 'e':
			enable = is_true(optarg);
			all_info = 0;
			break;
		case 'h':
			haltcpu = is_true(optarg);
			all_info = 0;
			break;
		case 't':
			batt_time = 1;
			all_info = 0;
			break;
		case 'z':
			dosleep = 1;
			all_info = 0;
			break;
		case 'Z':
			standby = 1;
			all_info = 0;
			break;
		case '?':
		default:
			usage();
		}
		argc -= optind;
		argv += optind;
	}
	if (haltcpu != -1 || enable != -1 || display != -1 || delta || dosleep
	    || standby) {
		fd = open(APMDEV, O_RDWR);
		bioscall_available = 1;
	} else if ((fd = open(APMDEV, O_RDWR)) >= 0)
		bioscall_available = 1;
	else
		fd = open(APMDEV, O_RDONLY);
	if (fd == -1)
		err(1, "can't open %s", APMDEV);
	if (enable != -1)
		apm_enable(fd, enable);
	if (haltcpu != -1)
		apm_haltcpu(fd, haltcpu);
	if (delta)
		apm_set_timer(fd, delta);
	if (dosleep)
		apm_suspend(fd);
	else if (standby)
		apm_standby(fd);
	else if (delta == 0) {
		struct apm_info info;

		apm_getinfo(fd, &info);
		if (all_info)
			print_all_info(fd, &info, bioscall_available);
		if (ac_status)
			printf("%d\n", info.ai_acline);
		if (batt_status)
			printf("%d\n", info.ai_batt_stat);
		if (batt_life)
			printf("%d\n", info.ai_batt_life);
		if (apm_status)
			printf("%d\n", info.ai_status);
		if (batt_time)
			printf("%d\n", info.ai_batt_time);
		if (display != -1)
			apm_display(fd, display);
	}
	close(fd);
	exit(0);
}
