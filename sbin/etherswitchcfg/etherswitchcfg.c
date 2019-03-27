/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Stefan Bethke.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_media.h>
#include <dev/etherswitch/etherswitch.h>

int	get_media_subtype(int, const char *);
int	get_media_mode(int, const char *);
int	get_media_options(int, const char *);
int	lookup_media_word(struct ifmedia_description *, const char *);
void    print_media_word(int, int);
void    print_media_word_ifconfig(int);

/* some constants */
#define IEEE802DOT1Q_VID_MAX	4094
#define IFMEDIAREQ_NULISTENTRIES	256

enum cmdmode {
	MODE_NONE = 0,
	MODE_PORT,
	MODE_CONFIG,
	MODE_VLANGROUP,
	MODE_REGISTER,
	MODE_PHYREG,
	MODE_ATU
};

struct cfg {
	int					fd;
	int					verbose;
	int					mediatypes;
	const char			*controlfile;
	etherswitch_conf_t	conf;
	etherswitch_info_t	info;
	enum cmdmode		mode;
	int					unit;
};

struct cmds {
	enum cmdmode	mode;
	const char	*name;
	int		args;
	int		(*f)(struct cfg *, int argc, char *argv[]);
};
static struct cmds cmds[];

/* Must match the ETHERSWITCH_PORT_LED_* enum order */
static const char *ledstyles[] = { "default", "on", "off", "blink", NULL };

/*
 * Print a value a la the %b format of the kernel's printf.
 * Stolen from ifconfig.c.
 */
static void
printb(const char *s, unsigned v, const char *bits)
{
	int i, any = 0;
	char c;

	if (bits && *bits == 8)
		printf("%s=%o", s, v);
	else
		printf("%s=%x", s, v);
	bits++;
	if (bits) {
		putchar('<');
		while ((i = *bits++) != '\0') {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(c);
			} else
				for (; *bits > 32; bits++)
					;
		}
		putchar('>');
	}
}

static int
read_register(struct cfg *cfg, int r)
{
	struct etherswitch_reg er;
	
	er.reg = r;
	if (ioctl(cfg->fd, IOETHERSWITCHGETREG, &er) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETREG)");
	return (er.val);
}

static void
write_register(struct cfg *cfg, int r, int v)
{
	struct etherswitch_reg er;
	
	er.reg = r;
	er.val = v;
	if (ioctl(cfg->fd, IOETHERSWITCHSETREG, &er) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETREG)");
}

static int
read_phyregister(struct cfg *cfg, int phy, int reg)
{
	struct etherswitch_phyreg er;
	
	er.phy = phy;
	er.reg = reg;
	if (ioctl(cfg->fd, IOETHERSWITCHGETPHYREG, &er) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETPHYREG)");
	return (er.val);
}

static void
write_phyregister(struct cfg *cfg, int phy, int reg, int val)
{
	struct etherswitch_phyreg er;
	
	er.phy = phy;
	er.reg = reg;
	er.val = val;
	if (ioctl(cfg->fd, IOETHERSWITCHSETPHYREG, &er) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETPHYREG)");
}

static int
set_port_vid(struct cfg *cfg, int argc, char *argv[])
{
	int v;
	etherswitch_port_t p;

	if (argc < 2)
		return (-1);

	v = strtol(argv[1], NULL, 0);
	if (v < 0 || v > IEEE802DOT1Q_VID_MAX)
		errx(EX_USAGE, "pvid must be between 0 and %d",
		    IEEE802DOT1Q_VID_MAX);
	bzero(&p, sizeof(p));
	p.es_port = cfg->unit;
	if (ioctl(cfg->fd, IOETHERSWITCHGETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETPORT)");
	p.es_pvid = v;
	if (ioctl(cfg->fd, IOETHERSWITCHSETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETPORT)");
	return (0);
}

static int
set_port_flag(struct cfg *cfg, int argc, char *argv[])
{
	char *flag;
	int n;
	uint32_t f;
	etherswitch_port_t p;

	if (argc < 1)
		return (-1);

	n = 0;
	f = 0;
	flag = argv[0];
	if (strcmp(flag, "none") != 0) {
		if (*flag == '-') {
			n++;
			flag++;
		}
		if (strcasecmp(flag, "striptag") == 0)
			f = ETHERSWITCH_PORT_STRIPTAG;
		else if (strcasecmp(flag, "addtag") == 0)
			f = ETHERSWITCH_PORT_ADDTAG;
		else if (strcasecmp(flag, "firstlock") == 0)
			f = ETHERSWITCH_PORT_FIRSTLOCK;
		else if (strcasecmp(flag, "dropuntagged") == 0)
			f = ETHERSWITCH_PORT_DROPUNTAGGED;
		else if (strcasecmp(flag, "doubletag") == 0)
			f = ETHERSWITCH_PORT_DOUBLE_TAG;
		else if (strcasecmp(flag, "ingress") == 0)
			f = ETHERSWITCH_PORT_INGRESS;
	}
	bzero(&p, sizeof(p));
	p.es_port = cfg->unit;
	if (ioctl(cfg->fd, IOETHERSWITCHGETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETPORT)");
	if (n)
		p.es_flags &= ~f;
	else
		p.es_flags |= f;
	if (ioctl(cfg->fd, IOETHERSWITCHSETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETPORT)");
	return (0);
}

static int
set_port_media(struct cfg *cfg, int argc, char *argv[])
{
	etherswitch_port_t p;
	int ifm_ulist[IFMEDIAREQ_NULISTENTRIES];
	int subtype;

	if (argc < 2)
		return (-1);

	bzero(&p, sizeof(p));
	p.es_port = cfg->unit;
	p.es_ifmr.ifm_ulist = ifm_ulist;
	p.es_ifmr.ifm_count = IFMEDIAREQ_NULISTENTRIES;
	if (ioctl(cfg->fd, IOETHERSWITCHGETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETPORT)");
	if (p.es_ifmr.ifm_count == 0)
		return (0);
	subtype = get_media_subtype(IFM_TYPE(ifm_ulist[0]), argv[1]);
	p.es_ifr.ifr_media = (p.es_ifmr.ifm_current & IFM_IMASK) |
	        IFM_TYPE(ifm_ulist[0]) | subtype;
	if (ioctl(cfg->fd, IOETHERSWITCHSETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETPORT)");
	return (0);
}

static int
set_port_mediaopt(struct cfg *cfg, int argc, char *argv[])
{
	etherswitch_port_t p;
	int ifm_ulist[IFMEDIAREQ_NULISTENTRIES];
	int options;

	if (argc < 2)
		return (-1);

	bzero(&p, sizeof(p));
	p.es_port = cfg->unit;
	p.es_ifmr.ifm_ulist = ifm_ulist;
	p.es_ifmr.ifm_count = IFMEDIAREQ_NULISTENTRIES;
	if (ioctl(cfg->fd, IOETHERSWITCHGETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETPORT)");
	options = get_media_options(IFM_TYPE(ifm_ulist[0]), argv[1]);
	if (options == -1)
		errx(EX_USAGE, "invalid media options \"%s\"", argv[1]);
	if (options & IFM_HDX) {
		p.es_ifr.ifr_media &= ~IFM_FDX;
		options &= ~IFM_HDX;
	}
	p.es_ifr.ifr_media |= options;
	if (ioctl(cfg->fd, IOETHERSWITCHSETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETPORT)");
	return (0);
}

static int
set_port_led(struct cfg *cfg, int argc, char *argv[])
{
	etherswitch_port_t p;
	int led;
	int i;

	if (argc < 3)
		return (-1);

	bzero(&p, sizeof(p));
	p.es_port = cfg->unit;
	if (ioctl(cfg->fd, IOETHERSWITCHGETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETPORT)");

	led = strtol(argv[1], NULL, 0);
	if (led < 1 || led > p.es_nleds)
		errx(EX_USAGE, "invalid led number %s; must be between 1 and %d",
			argv[1], p.es_nleds);

	led--;

	for (i=0; ledstyles[i] != NULL; i++) {
		if (strcmp(argv[2], ledstyles[i]) == 0) {
			p.es_led[led] = i;
			break;
		}
	} 
	if (ledstyles[i] == NULL)
		errx(EX_USAGE, "invalid led style \"%s\"", argv[2]);

	if (ioctl(cfg->fd, IOETHERSWITCHSETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETPORT)");

	return (0);
}

static int
set_vlangroup_vid(struct cfg *cfg, int argc, char *argv[])
{
	int v;
	etherswitch_vlangroup_t vg;

	if (argc < 2)
		return (-1);

	memset(&vg, 0, sizeof(vg));
	v = strtol(argv[1], NULL, 0);
	if (v < 0 || v > IEEE802DOT1Q_VID_MAX)
		errx(EX_USAGE, "vlan must be between 0 and %d", IEEE802DOT1Q_VID_MAX);
	vg.es_vlangroup = cfg->unit;
	if (ioctl(cfg->fd, IOETHERSWITCHGETVLANGROUP, &vg) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETVLANGROUP)");
	vg.es_vid = v;
	if (ioctl(cfg->fd, IOETHERSWITCHSETVLANGROUP, &vg) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETVLANGROUP)");
	return (0);
}

static int
set_vlangroup_members(struct cfg *cfg, int argc, char *argv[])
{
	etherswitch_vlangroup_t vg;
	int member, untagged;
	char *c, *d;
	int v;

	if (argc < 2)
		return (-1);

	member = untagged = 0;
	memset(&vg, 0, sizeof(vg));
	if (strcmp(argv[1], "none") != 0) {
		for (c=argv[1]; *c; c=d) {
			v = strtol(c, &d, 0);
			if (d == c)
				break;
			if (v < 0 || v >= cfg->info.es_nports)
				errx(EX_USAGE, "Member port must be between 0 and %d", cfg->info.es_nports-1);
			if (d[0] == ',' || d[0] == '\0' ||
				((d[0] == 't' || d[0] == 'T') && (d[1] == ',' || d[1] == '\0'))) {
				if (d[0] == 't' || d[0] == 'T') {
					untagged &= ~ETHERSWITCH_PORTMASK(v);
					d++;
				} else
					untagged |= ETHERSWITCH_PORTMASK(v);
				member |= ETHERSWITCH_PORTMASK(v);
				d++;
			} else
				errx(EX_USAGE, "Invalid members specification \"%s\"", d);
		}
	}
	vg.es_vlangroup = cfg->unit;
	if (ioctl(cfg->fd, IOETHERSWITCHGETVLANGROUP, &vg) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETVLANGROUP)");
	vg.es_member_ports = member;
	vg.es_untagged_ports = untagged;
	if (ioctl(cfg->fd, IOETHERSWITCHSETVLANGROUP, &vg) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETVLANGROUP)");
	return (0);
}

static int
set_register(struct cfg *cfg, char *arg)
{
	int a, v;
	char *c;
	
	a = strtol(arg, &c, 0);
	if (c==arg)
		return (1);
	if (*c == '=') {
		v = strtoul(c+1, NULL, 0);
		write_register(cfg, a, v);
	}
	printf("\treg 0x%04x=0x%08x\n", a, read_register(cfg, a));
	return (0);
}

static int
set_phyregister(struct cfg *cfg, char *arg)
{
	int phy, reg, val;
	char *c, *d;
	
	phy = strtol(arg, &c, 0);
	if (c==arg)
		return (1);
	if (*c != '.')
		return (1);
	d = c+1;
	reg = strtol(d, &c, 0);
	if (d == c)
		return (1);
	if (*c == '=') {
		val = strtoul(c+1, NULL, 0);
		write_phyregister(cfg, phy, reg, val);
	}
	printf("\treg %d.0x%02x=0x%04x\n", phy, reg, read_phyregister(cfg, phy, reg));
	return (0);
}

static int
set_vlan_mode(struct cfg *cfg, int argc, char *argv[])
{
	etherswitch_conf_t conf;

	if (argc < 2)
		return (-1);

	bzero(&conf, sizeof(conf));
	conf.cmd = ETHERSWITCH_CONF_VLAN_MODE;
	if (strcasecmp(argv[1], "isl") == 0)
		conf.vlan_mode = ETHERSWITCH_VLAN_ISL;
	else if (strcasecmp(argv[1], "port") == 0)
		conf.vlan_mode = ETHERSWITCH_VLAN_PORT;
	else if (strcasecmp(argv[1], "dot1q") == 0)
		conf.vlan_mode = ETHERSWITCH_VLAN_DOT1Q;
	else if (strcasecmp(argv[1], "dot1q4k") == 0)
		conf.vlan_mode = ETHERSWITCH_VLAN_DOT1Q_4K;
	else if (strcasecmp(argv[1], "qinq") == 0)
		conf.vlan_mode = ETHERSWITCH_VLAN_DOUBLE_TAG;
	else
		conf.vlan_mode = 0;
	if (ioctl(cfg->fd, IOETHERSWITCHSETCONF, &conf) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHSETCONF)");

	return (0);
}

static int
atu_flush(struct cfg *cfg, int argc, char *argv[])
{
	etherswitch_portid_t p;
	int i, r;

	bzero(&p, sizeof(p));

	/* note: argv[0] is "flush" */
	if (argc > 2 && strcasecmp(argv[1], "port") == 0) {
		p.es_port = atoi(argv[2]);
		i = IOETHERSWITCHFLUSHPORT;
		r = 3;
	} else if (argc > 1 && strcasecmp(argv[1], "all") == 0) {
		p.es_port = 0;
		r = 2;
		i = IOETHERSWITCHFLUSHALL;
	} else {
		fprintf(stderr,
		    "%s: invalid verb (port <x> or all) (got %s)\n",
		    __func__, argv[1]);
		return (-1);
	}

	if (ioctl(cfg->fd, i, &p) != 0)
		err(EX_OSERR, "ioctl(ATU flush (ioctl %d, port %d))",
		    i, p.es_port);
	return (r);
}

static int
atu_dump(struct cfg *cfg, int argc, char *argv[])
{
	etherswitch_atu_table_t p;
	etherswitch_atu_entry_t e;
	uint32_t i;

	(void) argc;
	(void) argv;

	/* Note: argv[0] is "dump" */
	bzero(&p, sizeof(p));

	if (ioctl(cfg->fd, IOETHERSWITCHGETTABLE, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETTABLE)");

	/* And now, iterate to get entries */
	for (i = 0; i < p.es_nitems; i++) {
		bzero(&e, sizeof(e));
		e.id = i;
		if (ioctl(cfg->fd, IOETHERSWITCHGETTABLEENTRY, &e) != 0)
			break;

		printf(" [%d] %s: portmask 0x%08x\n", i,
		    ether_ntoa((void *) &e.es_macaddr),
		    e.es_portmask);
	}

	return (1);
}

static void
print_config(struct cfg *cfg)
{
	const char *c;

	/* Get the device name. */
	c = strrchr(cfg->controlfile, '/');
	if (c != NULL)
		c = c + 1;
	else
		c = cfg->controlfile;

	/* Print VLAN mode. */
	if (cfg->conf.cmd & ETHERSWITCH_CONF_VLAN_MODE) {
		printf("%s: VLAN mode: ", c);
		switch (cfg->conf.vlan_mode) {
		case ETHERSWITCH_VLAN_ISL:
			printf("ISL\n");
			break;
		case ETHERSWITCH_VLAN_PORT:
			printf("PORT\n");
			break;
		case ETHERSWITCH_VLAN_DOT1Q:
			printf("DOT1Q\n");
			break;
		case ETHERSWITCH_VLAN_DOT1Q_4K:
			printf("DOT1Q4K\n");
			break;
		case ETHERSWITCH_VLAN_DOUBLE_TAG:
			printf("QinQ\n");
			break;
		default:
			printf("none\n");
		}
	}

	/* Print switch MAC address. */
	if (cfg->conf.cmd & ETHERSWITCH_CONF_SWITCH_MACADDR) {
		printf("%s: Switch MAC address: %s\n",
		    c,
		    ether_ntoa(&cfg->conf.switch_macaddr));
	}
}

static void
print_port(struct cfg *cfg, int port)
{
	etherswitch_port_t p;
	int ifm_ulist[IFMEDIAREQ_NULISTENTRIES];
	int i;

	bzero(&p, sizeof(p));
	p.es_port = port;
	p.es_ifmr.ifm_ulist = ifm_ulist;
	p.es_ifmr.ifm_count = IFMEDIAREQ_NULISTENTRIES;
	if (ioctl(cfg->fd, IOETHERSWITCHGETPORT, &p) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETPORT)");
	printf("port%d:\n", port);
	if (cfg->conf.vlan_mode == ETHERSWITCH_VLAN_DOT1Q)
		printf("\tpvid: %d\n", p.es_pvid);
	printb("\tflags", p.es_flags, ETHERSWITCH_PORT_FLAGS_BITS);
	printf("\n");
	if (p.es_nleds) {
		printf("\tled: ");
		for (i = 0; i < p.es_nleds; i++) {
			printf("%d:%s%s", i+1, ledstyles[p.es_led[i]], (i==p.es_nleds-1)?"":" ");
		}
		printf("\n");
	}
	printf("\tmedia: ");
	print_media_word(p.es_ifmr.ifm_current, 1);
	if (p.es_ifmr.ifm_active != p.es_ifmr.ifm_current) {
		putchar(' ');
		putchar('(');
		print_media_word(p.es_ifmr.ifm_active, 0);
		putchar(')');
	}
	putchar('\n');
	printf("\tstatus: %s\n", (p.es_ifmr.ifm_status & IFM_ACTIVE) != 0 ? "active" : "no carrier");
	if (cfg->mediatypes) {
		printf("\tsupported media:\n");
		if (p.es_ifmr.ifm_count > IFMEDIAREQ_NULISTENTRIES)
			p.es_ifmr.ifm_count = IFMEDIAREQ_NULISTENTRIES;
		for (i=0; i<p.es_ifmr.ifm_count; i++) {
			printf("\t\tmedia ");
			print_media_word(ifm_ulist[i], 0);
			putchar('\n');
		}
	}
}

static void
print_vlangroup(struct cfg *cfg, int vlangroup)
{
	etherswitch_vlangroup_t vg;
	int i, comma;
	
	vg.es_vlangroup = vlangroup;
	if (ioctl(cfg->fd, IOETHERSWITCHGETVLANGROUP, &vg) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETVLANGROUP)");
	if ((vg.es_vid & ETHERSWITCH_VID_VALID) == 0)
		return;
	vg.es_vid &= ETHERSWITCH_VID_MASK;
	printf("vlangroup%d:\n", vlangroup);
	if (cfg->conf.vlan_mode == ETHERSWITCH_VLAN_PORT)
		printf("\tport: %d\n", vg.es_vid);
	else
		printf("\tvlan: %d\n", vg.es_vid);
	printf("\tmembers ");
	comma = 0;
	if (vg.es_member_ports != 0)
		for (i=0; i<cfg->info.es_nports; i++) {
			if ((vg.es_member_ports & ETHERSWITCH_PORTMASK(i)) != 0) {
				if (comma)
					printf(",");
				printf("%d", i);
				if ((vg.es_untagged_ports & ETHERSWITCH_PORTMASK(i)) == 0)
					printf("t");
				comma = 1;
			}
		}
	else
		printf("none");
	printf("\n");
}

static void
print_info(struct cfg *cfg)
{
	const char *c;
	int i;
	
	c = strrchr(cfg->controlfile, '/');
	if (c != NULL)
		c = c + 1;
	else
		c = cfg->controlfile;
	if (cfg->verbose) {
		printf("%s: %s with %d ports and %d VLAN groups\n", c,
		    cfg->info.es_name, cfg->info.es_nports,
		    cfg->info.es_nvlangroups);
		printf("%s: ", c);
		printb("VLAN capabilities",  cfg->info.es_vlan_caps,
		    ETHERSWITCH_VLAN_CAPS_BITS);
		printf("\n");
	}
	print_config(cfg);
	for (i=0; i<cfg->info.es_nports; i++) {
		print_port(cfg, i);
	}
	for (i=0; i<cfg->info.es_nvlangroups; i++) {
		print_vlangroup(cfg, i);
	}
}

static void
usage(struct cfg *cfg __unused, char *argv[] __unused)
{
	fprintf(stderr, "usage: etherswitchctl\n");
	fprintf(stderr, "\tetherswitchcfg [-f control file] info\n");
	fprintf(stderr, "\tetherswitchcfg [-f control file] config "
	    "command parameter\n");
	fprintf(stderr, "\t\tconfig commands: vlan_mode\n");
	fprintf(stderr, "\tetherswitchcfg [-f control file] phy "
	    "phy.register[=value]\n");
	fprintf(stderr, "\tetherswitchcfg [-f control file] portX "
	    "[flags] command parameter\n");
	fprintf(stderr, "\t\tport commands: pvid, media, mediaopt, led\n");
	fprintf(stderr, "\tetherswitchcfg [-f control file] reg "
	    "register[=value]\n");
	fprintf(stderr, "\tetherswitchcfg [-f control file] vlangroupX "
	    "command parameter\n");
	fprintf(stderr, "\t\tvlangroup commands: vlan, members\n");
	exit(EX_USAGE);
}

static void
newmode(struct cfg *cfg, enum cmdmode mode)
{
	if (mode == cfg->mode)
		return;
	switch (cfg->mode) {
	case MODE_NONE:
		break;
	case MODE_CONFIG:
		/*
		 * Read the updated the configuration (it can be different
		 * from the last time we read it).
		 */
		if (ioctl(cfg->fd, IOETHERSWITCHGETCONF, &cfg->conf) != 0)
			err(EX_OSERR, "ioctl(IOETHERSWITCHGETCONF)");
		print_config(cfg);
		break;
	case MODE_PORT:
		print_port(cfg, cfg->unit);
		break;
	case MODE_VLANGROUP:
		print_vlangroup(cfg, cfg->unit);
		break;
	case MODE_REGISTER:
	case MODE_PHYREG:
	case MODE_ATU:
		break;
	}
	cfg->mode = mode;
}

int
main(int argc, char *argv[])
{
	int ch;
	struct cfg cfg;
	int i;
	
	bzero(&cfg, sizeof(cfg));
	cfg.controlfile = "/dev/etherswitch0";
	while ((ch = getopt(argc, argv, "f:mv?")) != -1)
		switch(ch) {
		case 'f':
			cfg.controlfile = optarg;
			break;
		case 'm':
			cfg.mediatypes++;
			break;
		case 'v':
			cfg.verbose++;
			break;
		case '?':
			/* FALLTHROUGH */
		default:
			usage(&cfg, argv);
		}
	argc -= optind;
	argv += optind;
	cfg.fd = open(cfg.controlfile, O_RDONLY);
	if (cfg.fd < 0)
		err(EX_UNAVAILABLE, "Can't open control file: %s", cfg.controlfile);
	if (ioctl(cfg.fd, IOETHERSWITCHGETINFO, &cfg.info) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETINFO)");
	if (ioctl(cfg.fd, IOETHERSWITCHGETCONF, &cfg.conf) != 0)
		err(EX_OSERR, "ioctl(IOETHERSWITCHGETCONF)");
	if (argc == 0) {
		print_info(&cfg);
		return (0);
	}
	cfg.mode = MODE_NONE;
	while (argc > 0) {
		switch(cfg.mode) {
		case MODE_NONE:
			if (strcmp(argv[0], "info") == 0) {
				print_info(&cfg);
			} else if (sscanf(argv[0], "port%d", &cfg.unit) == 1) {
				if (cfg.unit < 0 || cfg.unit >= cfg.info.es_nports)
					errx(EX_USAGE, "port unit must be between 0 and %d", cfg.info.es_nports - 1);
				newmode(&cfg, MODE_PORT);
			} else if (sscanf(argv[0], "vlangroup%d", &cfg.unit) == 1) {
				if (cfg.unit < 0 || cfg.unit >= cfg.info.es_nvlangroups)
					errx(EX_USAGE,
					    "vlangroup unit must be between 0 and %d",
					    cfg.info.es_nvlangroups - 1);
				newmode(&cfg, MODE_VLANGROUP);
			} else if (strcmp(argv[0], "config") == 0) {
				newmode(&cfg, MODE_CONFIG);
			} else if (strcmp(argv[0], "phy") == 0) {
				newmode(&cfg, MODE_PHYREG);
			} else if (strcmp(argv[0], "reg") == 0) {
				newmode(&cfg, MODE_REGISTER);
			} else if (strcmp(argv[0], "help") == 0) {
				usage(&cfg, argv);
			} else if (strcmp(argv[0], "atu") == 0) {
				newmode(&cfg, MODE_ATU);
			} else {
				errx(EX_USAGE, "Unknown command \"%s\"", argv[0]);
			}
			break;
		case MODE_PORT:
		case MODE_CONFIG:
		case MODE_VLANGROUP:
		case MODE_ATU:
			for(i=0; cmds[i].name != NULL; i++) {
				int r;
				if (cfg.mode == cmds[i].mode &&
				    strcmp(argv[0], cmds[i].name) == 0) {
					if ((cmds[i].args != -1) &&
					    (argc < (cmds[i].args + 1))) {
						printf("%s needs %d argument%s\n",
						    cmds[i].name, cmds[i].args,
						    (cmds[i].args==1)?"":",");
						break;
					}

					r = (cmds[i].f)(&cfg, argc, argv);

					/* -1 here means "error" */
					if (r == -1) {
						argc = 0;
						break;
					}

					/* Legacy return value */
					if (r == 0)
						r = cmds[i].args;

					argc -= r;
					argv += r;
					break;
				}
			}
			if (cmds[i].name == NULL) {
				newmode(&cfg, MODE_NONE);
				continue;
			}
			break;
		case MODE_REGISTER:
			if (set_register(&cfg, argv[0]) != 0) {
				newmode(&cfg, MODE_NONE);
				continue;
			}
			break;
		case MODE_PHYREG:
			if (set_phyregister(&cfg, argv[0]) != 0) {
				newmode(&cfg, MODE_NONE);
				continue;
			}
			break;
		}
		argc--;
		argv++;
	}
	/* switch back to command mode to print configuration for last command */
	newmode(&cfg, MODE_NONE);
	close(cfg.fd);
	return (0);
}

static struct cmds cmds[] = {
	{ MODE_PORT, "pvid", 1, set_port_vid },
	{ MODE_PORT, "media", 1, set_port_media },
	{ MODE_PORT, "mediaopt", 1, set_port_mediaopt },
	{ MODE_PORT, "led", 2, set_port_led },
	{ MODE_PORT, "addtag", 0, set_port_flag },
	{ MODE_PORT, "-addtag", 0, set_port_flag },
	{ MODE_PORT, "ingress", 0, set_port_flag },
	{ MODE_PORT, "-ingress", 0, set_port_flag },
	{ MODE_PORT, "striptag", 0, set_port_flag },
	{ MODE_PORT, "-striptag", 0, set_port_flag },
	{ MODE_PORT, "doubletag", 0, set_port_flag },
	{ MODE_PORT, "-doubletag", 0, set_port_flag },
	{ MODE_PORT, "firstlock", 0, set_port_flag },
	{ MODE_PORT, "-firstlock", 0, set_port_flag },
	{ MODE_PORT, "dropuntagged", 0, set_port_flag },
	{ MODE_PORT, "-dropuntagged", 0, set_port_flag },
	{ MODE_CONFIG, "vlan_mode", 1, set_vlan_mode },
	{ MODE_VLANGROUP, "vlan", 1, set_vlangroup_vid },
	{ MODE_VLANGROUP, "members", 1, set_vlangroup_members },
	{ MODE_ATU, "flush", -1, atu_flush },
	{ MODE_ATU, "dump", -1, atu_dump },
	{ 0, NULL, 0, NULL }
};
