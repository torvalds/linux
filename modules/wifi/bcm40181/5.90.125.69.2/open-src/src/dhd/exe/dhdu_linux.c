/*
 * Linux port of dhd command line utility, hacked from wl utility.
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhdu_linux.c,v 1.16.2.2 2010-12-16 08:12:11 $
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <proto/ethernet.h>
#include <proto/bcmip.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef TARGETENV_android
#include <error.h>
typedef u_int64_t u64;
typedef u_int32_t u32;
typedef u_int16_t u16;
typedef u_int8_t u8;
#endif /* TARGETENV_android */
#include <linux/sockios.h>
#include <linux/types.h>
#include <linux/ethtool.h>

#include <typedefs.h>
#include <signal.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <bcmcdc.h>
#include <bcmutils.h>
#include "dhdu.h"
#include "wlu_remote.h"
#include "wlu_client_shared.h"
#include "wlu_pipe.h"
#include <netdb.h>
#include <dhdioctl.h>
#include "dhdu_common.h"

char *av0;
static int rwl_os_type = LINUX_OS;
/* Search the dhd_cmds table for a matching command name.
 * Return the matching command or NULL if no match found.
 */
static cmd_t *
dhd_find_cmd(char* name)
{
	cmd_t *cmd = NULL;
	/* search the dhd_cmds for a matching name */
	for (cmd = dhd_cmds; cmd->name && strcmp(cmd->name, name); cmd++);
	if (cmd->name == NULL)
		cmd = NULL;
	return cmd;
}

static void
syserr(char *s)
{
	fprintf(stderr, "%s: ", dhdu_av0);
	perror(s);
	exit(errno);
}

/* This function is called by ioctl_setinformation_fe or ioctl_queryinformation_fe 
 * for executing  remote commands or local commands
 */
static int
dhd_ioctl(void *dhd, int cmd, void *buf, int len, bool set)
{
	struct ifreq *ifr = (struct ifreq *)dhd;
	dhd_ioctl_t ioc;
	int ret = 0;
	int s;
	/* By default try to execute wl commands */
	int driver_magic = WLC_IOCTL_MAGIC;
	int get_magic = WLC_GET_MAGIC;

	/* For local dhd commands execute dhd. For wifi transport we still
	 * execute wl commands.
	 */
	if (remote_type == NO_REMOTE && strncmp (buf, RWL_WIFI_ACTION_CMD,
		strlen(RWL_WIFI_ACTION_CMD)) && strncmp(buf, RWL_WIFI_GET_ACTION_CMD,
		strlen(RWL_WIFI_GET_ACTION_CMD))) {
		driver_magic = DHD_IOCTL_MAGIC;
		get_magic = DHD_GET_MAGIC;
	}

	/* open socket to kernel */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		syserr("socket");

	/* do it */
	ioc.cmd = cmd;
	ioc.buf = buf;
	ioc.len = len;
	ioc.set = set;
	ioc.driver = driver_magic;
	ifr->ifr_data = (caddr_t) &ioc;

	if ((ret = ioctl(s, SIOCDEVPRIVATE, ifr)) < 0) {
		if (cmd != get_magic) {
			ret = IOCTL_ERROR;
		}
	}

	/* cleanup */
	close(s);
	return ret;
}

/* This function is called in wlu_pipe.c remote_wifi_ser_init() to execute 
 * the initial set of wl commands for wifi transport (e.g slow_timer, fast_timer etc)
 */
int wl_ioctl(void *wl, int cmd, void *buf, int len, bool set)
{
	return dhd_ioctl(wl, cmd, buf, len, set); /* Call actual wl_ioctl here: Shubhro */
}

/* Search if dhd adapter or wl adapter is present 
 * This is called by dhd_find to check if it supports wl or dhd
 * The reason for checking wl adapter is that we can still send remote dhd commands over
 * wifi transport.
 */
static int
dhd_get_dev_type(char *name, void *buf, char *type)
{
	int s;
	int ret;
	struct ifreq ifr;
	struct ethtool_drvinfo info;

	/* open socket to kernel */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		syserr("socket");

	/* get device type */
	memset(&info, 0, sizeof(info));
	info.cmd = ETHTOOL_GDRVINFO;
	strcpy(info.driver, "?");
	strcat(info.driver, type);
	ifr.ifr_data = (caddr_t)&info;
	strncpy(ifr.ifr_name, name, IFNAMSIZ);
	if ((ret = ioctl(s, SIOCETHTOOL, &ifr)) < 0) {

		/* print a good diagnostic if not superuser */
		if (errno == EPERM)
			syserr("dhd_get_dev_type");

		*(char *)buf = '\0';
	}
	else
		strcpy(buf, info.driver);

	close(s);
	return ret;
}

/* dhd_get/dhd_set is called by several functions in dhdu.c. This used to call dhd_ioctl
 * directly. However now we need to execute the dhd commands remotely.
 * So we make use of wl pipes to execute this.
 * wl_get or wl_set functions also check if it is a local command hence they in turn
 * call dhd_ioctl if required. Name wl_get/wl_set is retained because these functions are
 * also called by wlu_pipe.c wlu_client_shared.c
 */
int
dhd_get(void *dhd, int cmd, void *buf, int len)
{
	return wl_get(dhd, cmd, buf, len);
}

/*
 * To use /dev/node interface:
 *   1.  mknod /dev/hnd0 c 248 0
 *   2.  chmod 777 /dev/hnd0
 */
#define NODE "/dev/hnd0"

int
dhd_set(void *dhd, int cmd, void *buf, int len)
{
	static int dnode = -1;

	switch (cmd) {
	case DHD_DLDN_ST:
		if (dnode == -1)
			dnode = open(NODE, O_RDWR);
		else
			fprintf(stderr, "devnode already opened!\n");

		return dnode;
		break;
	case DHD_DLDN_WRITE:
		if (dnode > 0)
			return write(dnode, buf, len);
		break;
	case DHD_DLDN_END:
		if (dnode > 0)
			return close(dnode);
		break;
	default:
		return wl_set(dhd, cmd, buf, len);

	}

	return -1;
}

/* Verify the wl adapter found.
 * This is called by dhd_find to check if it supports wl
 * The reason for checking wl adapter is that we can still send remote dhd commands over
 * wifi transport. The function is copied from wlu.c.
 */
int
wl_check(void *wl)
{
	int ret;
	int val = 0;

	if (!dhd_check (wl))
		return 0;

	/* 
	 *  If dhd_check() fails then go for a regular wl driver verification
	 */
	if ((ret = wl_get(wl, WLC_GET_MAGIC, &val, sizeof(int))) < 0)
		return ret;
	if (val != WLC_IOCTL_MAGIC)
		return BCME_ERROR;
	if ((ret = wl_get(wl, WLC_GET_VERSION, &val, sizeof(int))) < 0)
		return ret;
	if (val > WLC_IOCTL_VERSION) {
		fprintf(stderr, "Version mismatch, please upgrade\n");
		return BCME_ERROR;
	}
	return 0;
}
/* Search and verify the request type of adapter (wl or dhd)
 * This is called by main before executing local dhd commands
 * or sending remote dhd commands over wifi transport
 */
void
dhd_find(struct ifreq *ifr, char *type)
{
	char proc_net_dev[] = "/proc/net/dev";
	FILE *fp;
	static char buf[400];
	char *c, *name;
	char dev_type[32];

	ifr->ifr_name[0] = '\0';
	/* eat first two lines */
	if (!(fp = fopen(proc_net_dev, "r")) ||
	    !fgets(buf, sizeof(buf), fp) ||
	    !fgets(buf, sizeof(buf), fp))
		return;

	while (fgets(buf, sizeof(buf), fp)) {
		c = buf;
		while (isspace(*c))
			c++;
		if (!(name = strsep(&c, ":")))
			continue;
		strncpy(ifr->ifr_name, name, IFNAMSIZ);
		if (dhd_get_dev_type(name, dev_type, type) >= 0 &&
			!strncmp(dev_type, type, strlen(dev_type) - 1))
		{
			if (!wl_check((void*)ifr))
				break;
		}
		ifr->ifr_name[0] = '\0';
	}

	fclose(fp);
}
/* This function is called by wl_get to execute either local dhd command
 * or send a dhd command over wl transport
 */
static int
ioctl_queryinformation_fe(void *wl, int cmd, void* input_buf, int *input_len)
{
	if (remote_type == NO_REMOTE) {
		return dhd_ioctl(wl, cmd, input_buf, *input_len, FALSE);
	} else {
		return rwl_queryinformation_fe(wl, cmd, input_buf,
			(unsigned long*)input_len, 0, RDHD_GET_IOCTL);
	}
}

/* This function is called by wl_set to execute either local dhd command
 * or send a dhd command over wl transport
 */
static int
ioctl_setinformation_fe(void *wl, int cmd, void* buf, int *len)
{
	if (remote_type == NO_REMOTE) {
		return dhd_ioctl(wl,  cmd, buf, *len, TRUE);
	} else {
		return rwl_setinformation_fe(wl, cmd, buf, (unsigned long*)len, 0, RDHD_SET_IOCTL);

	}
}

/* The function is replica of wl_get in wlu_linux.c. Optimize when we have some 
 * common code between wlu_linux.c and dhdu_linux.c
 */
int
wl_get(void *wl, int cmd, void *buf, int len)
{
	int error = BCME_OK;
	/* For RWL: When interfacing to a Windows client, need t add in OID_BASE */
	if ((rwl_os_type == WIN32_OS) && (remote_type != NO_REMOTE)) {
		error = (int)ioctl_queryinformation_fe(wl, WL_OID_BASE + cmd, buf, &len);
	} else {
		error = (int)ioctl_queryinformation_fe(wl, cmd, buf, &len);
	}
	if (error == SERIAL_PORT_ERR)
		return SERIAL_PORT_ERR;

	if (error != 0)
		return IOCTL_ERROR;

	return error;
}

/* The function is replica of wl_set in wlu_linux.c. Optimize when we have some 
 * common code between wlu_linux.c and dhdu_linux.c
 */
int
wl_set(void *wl, int cmd, void *buf, int len)
{
	int error = BCME_OK;

	/* For RWL: When interfacing to a Windows client, need t add in OID_BASE */
	if ((rwl_os_type == WIN32_OS) && (remote_type != NO_REMOTE)) {
		error = (int)ioctl_setinformation_fe(wl, WL_OID_BASE + cmd, buf, &len);
	} else {
		error = (int)ioctl_setinformation_fe(wl, cmd, buf, &len);
	}

	if (error == SERIAL_PORT_ERR)
		return SERIAL_PORT_ERR;

	if (error != 0) {
		return IOCTL_ERROR;
	}
	return error;
}
/* Main client function
 * The code is mostly from wlu_linux.c. This function takes care of executing remote dhd commands
 * along with the local dhd commands now.
 */
int
main(int argc, char **argv)
{
	struct ifreq ifr;
	char *ifname = NULL;
	int err = 0;
	int help = 0;
	int status = CMD_DHD;
	void* serialHandle = NULL;
	struct ipv4_addr temp;

	UNUSED_PARAMETER(argc);

	av0 = argv[0];
	memset(&ifr, 0, sizeof(ifr));
	argv++;

	if ((status = dhd_option(&argv, &ifname, &help)) == CMD_OPT) {
		if (ifname)
			strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	}
	/* Linux client looking for a Win32 server */
	if (*argv && strncmp (*argv, "--wince", strlen(*argv)) == 0) {
		rwl_os_type = WIN32_OS;
		argv++;
	}

	/* RWL socket transport Usage: --socket ipaddr [port num] */
	if (*argv && strncmp (*argv, "--socket", strlen(*argv)) == 0) {
		argv++;

		remote_type = REMOTE_SOCKET;

		if (!(*argv)) {
			rwl_usage(remote_type);
			return err;
		}

		if (!dhd_atoip(*argv, &temp)) {
			rwl_usage(remote_type);
			return err;
		}
		g_rwl_servIP = *argv;
		argv++;

		g_rwl_servport = DEFAULT_SERVER_PORT;
		if ((*argv) && isdigit(**argv)) {
			g_rwl_servport = atoi(*argv);
			argv++;
		}
	}

	/* RWL from system serial port on client to uart dongle port on server */
	/* Usage: --dongle /dev/ttyS0 */
	if (*argv && strncmp (*argv, "--dongle", strlen(*argv)) == 0) {
		argv++;
		remote_type = REMOTE_DONGLE;

		if (!(*argv)) {
			rwl_usage(remote_type);
			return err;
		}
		g_rwl_device_name_serial = *argv;
		argv++;
		if ((serialHandle = rwl_open_pipe(remote_type, "\0", 0, 0)) == NULL) {
			DPRINT_ERR(ERR, "serial device open error\r\n");
			return BCME_ERROR;
		}
		ifr = (*(struct ifreq *)serialHandle);
	}

	/* RWL over wifi.  Usage: --wifi mac_address */
	if (*argv && strncmp (*argv, "--wifi", strlen(*argv)) == 0) {
		argv++;
		remote_type = NO_REMOTE;
		if (!ifr.ifr_name[0])
		{
			dhd_find(&ifr, "wl");
		}
		/* validate the interface */
		if (!ifr.ifr_name[0] || wl_check((void*)&ifr)) {
			fprintf(stderr, "%s: wl driver adapter not found\n", av0);
			exit(1);
		}
		remote_type = REMOTE_WIFI;

		if (argc < 4) {
			rwl_usage(remote_type);
			return err;
		}
		/* copy server mac address to local buffer for later use by findserver cmd */
		if (!dhd_ether_atoe(*argv, (struct ether_addr *)g_rwl_buf_mac)) {
			fprintf(stderr,
			        "could not parse as an ethernet MAC address\n");
			return FAIL;
		}
		argv++;
	}

	/* Process for local dhd */
	if (remote_type == NO_REMOTE) {
		err = process_args(&ifr, argv);
		return err;
	}

	if (*argv) {
		err = process_args(&ifr, argv);
		if ((err == SERIAL_PORT_ERR) && (remote_type == REMOTE_DONGLE)) {
			DPRINT_ERR(ERR, "\n Retry again\n");
			err = process_args((struct ifreq*)&ifr, argv);
		}
		return err;
	}
	rwl_usage(remote_type);

	if (remote_type == REMOTE_DONGLE)
		rwl_close_pipe(remote_type, (void*)&ifr);

	return err;
}
/* 
 * Function called for  'local' execution and for 'remote' non-interactive session
 * (shell cmd, wl cmd) .The code is mostly from wlu_linux.c. This code can be
 * common to wlu_linux.c and dhdu_linux.c
 */
static int
process_args(struct ifreq* ifr, char **argv)
{
	char *ifname = NULL;
	int help = 0;
	int status = 0;
	int err = BCME_OK;
	cmd_t *cmd = NULL;
	while (*argv) {
		if ((strcmp (*argv, "sh") == 0) && (remote_type != NO_REMOTE)) {
			argv++; /* Get the shell command */
			if (*argv) {
				/* Register handler in case of shell command only */
				signal(SIGINT, ctrlc_handler);
				err = rwl_shell_cmd_proc((void*)ifr, argv, SHELL_CMD);
			} else {
				DPRINT_ERR(ERR,
				"Enter the shell command (e.g ls(Linux) or dir(Win CE) \n");
				err = BCME_ERROR;
			}
			return err;
		}

		if ((status = dhd_option(&argv, &ifname, &help)) == CMD_OPT) {
			if (help)
				break;
			if (ifname)
				strncpy(ifr->ifr_name, ifname, IFNAMSIZ);
			continue;
		}
		/* parse error */
		else if (status == CMD_ERR)
		    break;

		if (remote_type == NO_REMOTE) {
		/* use default interface */
			if (!ifr->ifr_name[0])
				dhd_find(ifr, "dhd");
			/* validate the interface */
			if (!ifr->ifr_name[0] || dhd_check((void *)ifr)) {
			if (strcmp("dldn", *argv) != 0) {
				fprintf(stderr, "%s: dhd driver adapter not found\n", av0);
				exit(BCME_ERROR);
				}
			}

		}
		/* search for command */
		cmd = dhd_find_cmd(*argv);
		/* if not found, use default set_var and get_var commands */
		if (!cmd) {
			cmd = &dhd_varcmd;
		}

		/* do command */
		err = (*cmd->func)((void *) ifr, cmd, argv);
		break;
	} /* while loop end */

	/* provide for help on a particular command */
	if (help && *argv) {
		cmd = dhd_find_cmd(*argv);
		if (cmd) {
			dhd_cmd_usage(cmd);
		} else {
			DPRINT_ERR(ERR, "%s: Unrecognized command \"%s\", type -h for help\n",
			           av0, *argv);
		}
	} else if (!cmd)
		dhd_usage(NULL);
	else if (err == USAGE_ERROR)
		dhd_cmd_usage(cmd);
	else if (err == IOCTL_ERROR)
		dhd_printlasterror((void *) ifr);

	return err;
}

int
rwl_shell_createproc(void *wl)
{
	UNUSED_PARAMETER(wl);
	return fork();
}

void
rwl_shell_killproc(int pid)
{
	kill(pid, SIGKILL);
}

#ifdef RWL_SOCKET
/* validate hostname/ip given by the client */
int
validate_server_address()
{
	struct hostent *he;
	struct ipv4_addr temp;

	if (!dhd_atoip(g_rwl_servIP, &temp)) {
		/* Wrong IP address format check for hostname */
		if ((he = gethostbyname(g_rwl_servIP)) != NULL) {
			if (!dhd_atoip(*he->h_addr_list, &temp)) {
				g_rwl_servIP = inet_ntoa(*(struct in_addr *)*he->h_addr_list);
				if (g_rwl_servIP == NULL) {
					DPRINT_ERR(ERR, "Error at inet_ntoa \n");
					return FAIL;
				}
			} else {
				DPRINT_ERR(ERR, "Error in IP address \n");
				return FAIL;
			}
		} else {
			DPRINT_ERR(ERR, "Enter correct IP address/hostname format\n");
			return FAIL;
		}
	}
	return SUCCESS;
}
#endif /* RWL_SOCKET */
