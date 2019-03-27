/*-
 * Copyright (c) 2015 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2015 Allan Jude <allanjude@FreeBSD.org>
 * Copyright (c) 2000 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <glob.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libxo/xo.h>

#include <cam/scsi/scsi_enc.h>

#include "eltsub.h"

#define SESUTIL_XO_VERSION	"1"

static int encstatus(int argc, char **argv);
static int fault(int argc, char **argv);
static int locate(int argc, char **argv);
static int objmap(int argc, char **argv);
static int sesled(int argc, char **argv, bool fault);
static void sesutil_print(bool *title, const char *fmt, ...) __printflike(2,3);

static struct command {
	const char *name;
	const char *param;
	const char *desc;
	int (*exec)(int argc, char **argv);
} cmds[] = {
	{ "fault",
	    "(<disk>|<sesid>|all) (on|off)",
	    "Change the state of the fault LED associated with a disk",
	    fault },
	{ "locate",
	    "(<disk>|<sesid>|all) (on|off)",
	    "Change the state of the locate LED associated with a disk",
	    locate },
	{ "map", "",
	    "Print a map of the devices managed by the enclosure", objmap } ,
	{ "status", "", "Print the status of the enclosure",
	    encstatus },
};

static const int nbcmds = nitems(cmds);
static const char *uflag;

static void
usage(FILE *out, const char *subcmd)
{
	int i;

	if (subcmd == NULL) {
		fprintf(out, "Usage: %s [-u /dev/ses<N>] <command> [options]\n",
		    getprogname());
		fprintf(out, "Commands supported:\n");
	}
	for (i = 0; i < nbcmds; i++) {
		if (subcmd != NULL) {
			if (strcmp(subcmd, cmds[i].name) == 0) {
				fprintf(out, "Usage: %s %s [-u /dev/ses<N>] "
				    "%s\n\t%s\n", getprogname(), subcmd,
				    cmds[i].param, cmds[i].desc);
				break;
			}
			continue;
		}
		fprintf(out, "    %-12s%s\n\t\t%s\n\n", cmds[i].name,
		    cmds[i].param, cmds[i].desc);
	}

	exit(EXIT_FAILURE);
}

static void
do_led(int fd, unsigned int idx, elm_type_t type, bool onoff, bool setfault)
{
	int state = onoff ? 1 : 0;
	encioc_elm_status_t o;
	struct ses_ctrl_dev_slot *slot;

	o.elm_idx = idx;
	if (ioctl(fd, ENCIOC_GETELMSTAT, (caddr_t) &o) < 0) {
		close(fd);
		xo_err(EXIT_FAILURE, "ENCIOC_GETELMSTAT");
	}
	slot = (struct ses_ctrl_dev_slot *) &o.cstat[0];
	switch (type) {
	case ELMTYP_DEVICE:
	case ELMTYP_ARRAY_DEV:
		ses_ctrl_common_set_select(&slot->common, 1);
		if (setfault)
			ses_ctrl_dev_slot_set_rqst_fault(slot, state);
		else
			ses_ctrl_dev_slot_set_rqst_ident(slot, state);
		break;
	default:
		return;
	}
	if (ioctl(fd, ENCIOC_SETELMSTAT, (caddr_t) &o) < 0) {
		close(fd);
		xo_err(EXIT_FAILURE, "ENCIOC_SETELMSTAT");
	}
}

static bool
disk_match(const char *devnames, const char *disk, size_t len)
{
	const char *dname;

	dname = devnames;
	while ((dname = strstr(dname, disk)) != NULL) {
		if (dname[len] == '\0' || dname[len] == ',') {
			return (true);
		}
		dname++;
	}

	return (false);
}

static int
sesled(int argc, char **argv, bool setfault)
{
	encioc_elm_devnames_t objdn;
	encioc_element_t *objp;
	glob_t g;
	char *disk, *endptr;
	size_t len, i, ndisks;
	int fd;
	unsigned int nobj, j, sesid;
	bool all, isses, onoff;

	isses = false;
	all = false;
	onoff = false;

	if (argc != 3) {
		usage(stderr, (setfault ? "fault" : "locate"));
	}

	disk = argv[1];

	sesid = strtoul(disk, &endptr, 10);
	if (*endptr == '\0') {
		endptr = strrchr(uflag, '*');
		if (endptr != NULL && *endptr == '*') {
			xo_warnx("Must specifying a SES device (-u) to use a SES "
			    "id# to identify a disk");
			usage(stderr, (setfault ? "fault" : "locate"));
		}
		isses = true;
	}

	if (strcmp(argv[2], "on") == 0) {
		onoff = true;
	} else if (strcmp(argv[2], "off") == 0) {
		onoff = false;
	} else {
		usage(stderr, (setfault ? "fault" : "locate"));
	}

	if (strcmp(disk, "all") == 0) {
		all = true;
	}
	len = strlen(disk);

	/* Get the list of ses devices */
	if (glob((uflag != NULL ? uflag : "/dev/ses[0-9]*"), 0, NULL, &g) ==
	    GLOB_NOMATCH) {
		globfree(&g);
		xo_errx(EXIT_FAILURE, "No SES devices found");
	}

	ndisks = 0;
	for (i = 0; i < g.gl_pathc; i++) {
		/* ensure we only got numbers after ses */
		if (strspn(g.gl_pathv[i] + 8, "0123456789") !=
		    strlen(g.gl_pathv[i] + 8)) {
			continue;
		}
		if ((fd = open(g.gl_pathv[i], O_RDWR)) < 0) {
			/*
			 * Don't treat non-access errors as critical if we are
			 * accessing all devices
			 */
			if (errno == EACCES && g.gl_pathc > 1) {
				xo_err(EXIT_FAILURE, "unable to access SES device");
			}
			xo_warn("unable to access SES device: %s", g.gl_pathv[i]);
			continue;
		}

		if (ioctl(fd, ENCIOC_GETNELM, (caddr_t) &nobj) < 0) {
			close(fd);
			xo_err(EXIT_FAILURE, "ENCIOC_GETNELM");
		}

		objp = calloc(nobj, sizeof(encioc_element_t));
		if (objp == NULL) {
			close(fd);
			xo_err(EXIT_FAILURE, "calloc()");
		}

		if (ioctl(fd, ENCIOC_GETELMMAP, (caddr_t) objp) < 0) {
			close(fd);
			xo_err(EXIT_FAILURE, "ENCIOC_GETELMMAP");
		}

		if (isses) {
			if (sesid > nobj) {
				close(fd);
				xo_errx(EXIT_FAILURE,
				     "Requested SES ID does not exist");
			}
			do_led(fd, sesid, objp[sesid].elm_type, onoff, setfault);
			ndisks++;
			close(fd);
			break;
		}
		for (j = 0; j < nobj; j++) {
			if (all) {
				do_led(fd, objp[j].elm_idx, objp[j].elm_type,
				    onoff, setfault);
				continue;
			}
			memset(&objdn, 0, sizeof(objdn));
			objdn.elm_idx = objp[j].elm_idx;
			objdn.elm_names_size = 128;
			objdn.elm_devnames = calloc(128, sizeof(char));
			if (objdn.elm_devnames == NULL) {
				close(fd);
				xo_err(EXIT_FAILURE, "calloc()");
			}
			if (ioctl(fd, ENCIOC_GETELMDEVNAMES,
			    (caddr_t) &objdn) <0) {
				continue;
			}
			if (objdn.elm_names_len > 0) {
				if (disk_match(objdn.elm_devnames, disk, len)) {
					do_led(fd, objdn.elm_idx, objp[j].elm_type,
					    onoff, setfault);
					ndisks++;
					break;
				}
			}
		}
		free(objp);
		close(fd);
	}
	globfree(&g);
	if (ndisks == 0 && all == false) {
		xo_errx(EXIT_FAILURE, "Count not find the SES id of device '%s'",
		    disk);
	}

	return (EXIT_SUCCESS);
}

static int
locate(int argc, char **argv)
{

	return (sesled(argc, argv, false));
}

static int
fault(int argc, char **argv)
{

	return (sesled(argc, argv, true));
}

#define TEMPERATURE_OFFSET 20
static void
sesutil_print(bool *title, const char *fmt, ...)
{
	va_list args;

	if (!*title) {
		xo_open_container("extra_status");
		xo_emit("\t\tExtra status:\n");
		*title = true;
	}
	va_start(args, fmt);
	xo_emit_hv(NULL, fmt, args);
	va_end(args);
}

static void
print_extra_status(int eletype, u_char *cstat)
{
	bool title = false;

	if (cstat[0] & 0x40) {
		sesutil_print(&title, "\t\t-{e:predicted_failure/true} Predicted Failure\n");
	}
	if (cstat[0] & 0x20) {
		sesutil_print(&title, "\t\t-{e:disabled/true} Disabled\n");
	}
	if (cstat[0] & 0x10) {
		sesutil_print(&title, "\t\t-{e:swapped/true} Swapped\n");
	}
	switch (eletype) {
	case ELMTYP_DEVICE:
	case ELMTYP_ARRAY_DEV:
		if (cstat[2] & 0x02) {
			sesutil_print(&title, "\t\t- LED={q:led/locate}\n");
		}
		if (cstat[2] & 0x20) {
			sesutil_print(&title, "\t\t- LED={q:led/fault}\n");
		}
		break;
	case ELMTYP_FAN:
		sesutil_print(&title, "\t\t- Speed: {:speed/%d}{Uw:rpm}\n",
		    (((0x7 & cstat[1]) << 8) + cstat[2]) * 10);
		break;
	case ELMTYP_THERM:
		if (cstat[2]) {
			sesutil_print(&title, "\t\t- Temperature: {:temperature/%d}{Uw:C}\n",
			    cstat[2] - TEMPERATURE_OFFSET);
		} else {
			sesutil_print(&title, "\t\t- Temperature: -{q:temperature/reserved}-\n");
		}
		break;
	case ELMTYP_VOM:
		sesutil_print(&title, "\t\t- Voltage: {:voltage/%.2f}{Uw:V}\n",
		    be16dec(cstat + 2) / 100.0);
		break;
	}
	if (title) {
		xo_close_container("extra_status");
	}
}

static int
objmap(int argc, char **argv __unused)
{
	encioc_string_t stri;
	encioc_elm_devnames_t e_devname;
	encioc_elm_status_t e_status;
	encioc_elm_desc_t e_desc;
	encioc_element_t *e_ptr;
	glob_t g;
	int fd;
	unsigned int j, nobj;
	size_t i;
	char str[32];

	if (argc != 1) {
		usage(stderr, "map");
	}

	/* Get the list of ses devices */
	if (glob(uflag, 0, NULL, &g) == GLOB_NOMATCH) {
		globfree(&g);
		xo_errx(EXIT_FAILURE, "No SES devices found");
	}
	xo_set_version(SESUTIL_XO_VERSION);
	xo_open_container("sesutil");
	xo_open_list("enclosures");
	for (i = 0; i < g.gl_pathc; i++) {
		/* ensure we only got numbers after ses */
		if (strspn(g.gl_pathv[i] + 8, "0123456789") !=
		    strlen(g.gl_pathv[i] + 8)) {
			continue;
		}
		if ((fd = open(g.gl_pathv[i], O_RDWR)) < 0) {
			/*
			 * Don't treat non-access errors as critical if we are
			 * accessing all devices
			 */
			if (errno == EACCES && g.gl_pathc > 1) {
				xo_err(EXIT_FAILURE, "unable to access SES device");
			}
			xo_warn("unable to access SES device: %s", g.gl_pathv[i]);
			continue;
		}

		if (ioctl(fd, ENCIOC_GETNELM, (caddr_t) &nobj) < 0) {
			close(fd);
			xo_err(EXIT_FAILURE, "ENCIOC_GETNELM");
		}

		e_ptr = calloc(nobj, sizeof(encioc_element_t));
		if (e_ptr == NULL) {
			close(fd);
			xo_err(EXIT_FAILURE, "calloc()");
		}

		if (ioctl(fd, ENCIOC_GETELMMAP, (caddr_t) e_ptr) < 0) {
			close(fd);
			xo_err(EXIT_FAILURE, "ENCIOC_GETELMMAP");
		}

		xo_open_instance("enclosures");
		xo_emit("{t:enc/%s}:\n", g.gl_pathv[i] + 5);
		stri.bufsiz = sizeof(str);
		stri.buf = &str[0];
		if (ioctl(fd, ENCIOC_GETENCNAME, (caddr_t) &stri) == 0)
			xo_emit("\tEnclosure Name: {t:name/%s}\n", stri.buf);
		stri.bufsiz = sizeof(str);
		stri.buf = &str[0];
		if (ioctl(fd, ENCIOC_GETENCID, (caddr_t) &stri) == 0)
			xo_emit("\tEnclosure ID: {t:id/%s}\n", stri.buf);

		xo_open_list("elements");
		for (j = 0; j < nobj; j++) {
			/* Get the status of the element */
			memset(&e_status, 0, sizeof(e_status));
			e_status.elm_idx = e_ptr[j].elm_idx;
			if (ioctl(fd, ENCIOC_GETELMSTAT,
			    (caddr_t) &e_status) < 0) {
				close(fd);
				xo_err(EXIT_FAILURE, "ENCIOC_GETELMSTAT");
			}
			/* Get the description of the element */
			memset(&e_desc, 0, sizeof(e_desc));
			e_desc.elm_idx = e_ptr[j].elm_idx;
			e_desc.elm_desc_len = UINT16_MAX;
			e_desc.elm_desc_str = calloc(UINT16_MAX, sizeof(char));
			if (e_desc.elm_desc_str == NULL) {
				close(fd);
				xo_err(EXIT_FAILURE, "calloc()");
			}
			if (ioctl(fd, ENCIOC_GETELMDESC,
			    (caddr_t) &e_desc) < 0) {
				close(fd);
				xo_err(EXIT_FAILURE, "ENCIOC_GETELMDESC");
			}
			/* Get the device name(s) of the element */
			memset(&e_devname, 0, sizeof(e_devname));
			e_devname.elm_idx = e_ptr[j].elm_idx;
			e_devname.elm_names_size = 128;
			e_devname.elm_devnames = calloc(128, sizeof(char));
			if (e_devname.elm_devnames == NULL) {
				close(fd);
				xo_err(EXIT_FAILURE, "calloc()");
			}
			if (ioctl(fd, ENCIOC_GETELMDEVNAMES,
			    (caddr_t) &e_devname) <0) {
				/* We don't care if this fails */
				e_devname.elm_devnames[0] = '\0';
			}
			xo_open_instance("elements");
			xo_emit("\tElement {:id/%u}, Type: {:type/%s}\n", e_ptr[j].elm_idx,
			    geteltnm(e_ptr[j].elm_type));
			xo_emit("\t\tStatus: {:status/%s} ({q:status_code/0x%02x 0x%02x 0x%02x 0x%02x})\n",
			    scode2ascii(e_status.cstat[0]), e_status.cstat[0],
			    e_status.cstat[1], e_status.cstat[2],
			    e_status.cstat[3]);
			if (e_desc.elm_desc_len > 0) {
				xo_emit("\t\tDescription: {:description/%s}\n",
				    e_desc.elm_desc_str);
			}
			if (e_devname.elm_names_len > 0) {
				xo_emit("\t\tDevice Names: {:device_names/%s}\n",
				    e_devname.elm_devnames);
			}
			print_extra_status(e_ptr[j].elm_type, e_status.cstat);
			xo_close_instance("elements");
			free(e_devname.elm_devnames);
		}
		xo_close_list("elements");
		free(e_ptr);
		close(fd);
	}
	globfree(&g);
	xo_close_list("enclosures");
	xo_close_container("sesutil");
	xo_finish();

	return (EXIT_SUCCESS);
}

static int
encstatus(int argc, char **argv __unused)
{
	glob_t g;
	int fd, status;
	size_t i, e;
	u_char estat;

	status = 0;
	if (argc != 1) {
		usage(stderr, "status");
	}

	/* Get the list of ses devices */
	if (glob(uflag, 0, NULL, &g) == GLOB_NOMATCH) {
		globfree(&g);
		xo_errx(EXIT_FAILURE, "No SES devices found");
	}

	xo_set_version(SESUTIL_XO_VERSION);
	xo_open_container("sesutil");
	xo_open_list("enclosures");
	for (i = 0; i < g.gl_pathc; i++) {
		/* ensure we only got numbers after ses */
		if (strspn(g.gl_pathv[i] + 8, "0123456789") !=
		    strlen(g.gl_pathv[i] + 8)) {
			continue;
		}
		if ((fd = open(g.gl_pathv[i], O_RDWR)) < 0) {
			/*
			 * Don't treat non-access errors as critical if we are
			 * accessing all devices
			 */
			if (errno == EACCES && g.gl_pathc > 1) {
				xo_err(EXIT_FAILURE, "unable to access SES device");
			}
			xo_warn("unable to access SES device: %s", g.gl_pathv[i]);
			continue;
		}

		if (ioctl(fd, ENCIOC_GETENCSTAT, (caddr_t) &estat) < 0) {
			xo_err(EXIT_FAILURE, "ENCIOC_GETENCSTAT");
			close(fd);
		}

		xo_open_instance("enclosures");
		xo_emit("{:enc/%s}: ", g.gl_pathv[i] + 5);
		e = 0;
		if (estat == 0) {
			if (status == 0) {
				status = 1;
			}
			xo_emit("{q:status/OK}");
		} else {
			if (estat & SES_ENCSTAT_INFO) {
				xo_emit("{lq:status/INFO}");
				e++;
			}
			if (estat & SES_ENCSTAT_NONCRITICAL) {
				if (e)
					xo_emit(",");
				xo_emit("{lq:status/NONCRITICAL}");
				e++;
			}
			if (estat & SES_ENCSTAT_CRITICAL) {
				if (e)
					xo_emit(",");
				xo_emit("{lq:status/CRITICAL}");
				e++;
				status = -1;
			}
			if (estat & SES_ENCSTAT_UNRECOV) {
				if (e)
					xo_emit(",");
				xo_emit("{lq:status/UNRECOV}");
				e++;
				status = -1;
			}
		}
		xo_close_instance("enclosures");
		xo_emit("\n");
		close(fd);
	}
	globfree(&g);

	xo_close_list("enclosures");
	xo_close_container("sesutil");
	xo_finish();

	if (status == 1) {
		return (EXIT_SUCCESS);
	} else {
		return (EXIT_FAILURE);
	}
}

int
main(int argc, char **argv)
{
	int i, ch;
	struct command *cmd = NULL;

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		exit(1);

	uflag = "/dev/ses[0-9]*";
	while ((ch = getopt_long(argc, argv, "u:", NULL, NULL)) != -1) {
		switch (ch) {
		case 'u':
			uflag = optarg;
			break;
		case '?':
		default:
			usage(stderr, NULL);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		warnx("Missing command");
		usage(stderr, NULL);
	}

	for (i = 0; i < nbcmds; i++) {
		if (strcmp(argv[0], cmds[i].name) == 0) {
			cmd = &cmds[i];
			break;
		}
	}

	if (cmd == NULL) {
		warnx("unknown command %s", argv[0]);
		usage(stderr, NULL);
	}

	return (cmd->exec(argc, argv));
}
