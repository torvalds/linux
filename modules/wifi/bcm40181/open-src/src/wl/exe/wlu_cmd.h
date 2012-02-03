/*
 * Command structure for wl command line utility
 *
 * Copyright 2002, Broadcom Corporation
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Broadcom Corporation.
 *
 * $Id: wlu_cmd.h,v 1.6 2007/02/02 19:30:07 Exp $
 */

#ifndef _wlu_cmd_h_
#define _wlu_cmd_h_

typedef struct cmd cmd_t;
typedef int (cmd_func_t)(void *wl, cmd_t *cmd, char **argv);

/* generic command line argument handler */
struct cmd {
	const char *name;
	cmd_func_t *func;
	int get;
	int set;
	const char *help;
};

/* list of command line arguments */
extern cmd_t wl_cmds[];
extern cmd_t wl_varcmd;

/* per-port ioctl handlers */
extern int wl_get(void *wl, int cmd, void *buf, int len);
extern int wl_set(void *wl, int cmd, void *buf, int len);

/*
 * Flags for command categories.  A command may belong to
 * multiple categories.  These are used in bitmaps, so be careful
 * to keep the macro value (2 ^ n) and the array indexes (n)
 * consistent.
 */

#define CMD_PHY               0x1
#define CMD_CHAN              0x2
#define CMD_RATE              0x4
#define CMD_POWER             0x8
#define CMD_MAC               0x10
#define CMD_MGMT              0x20
#define CMD_SEC               0x40
#define CMD_WME               0x80
#define CMD_MON               0x100
#define CMD_AP                0x200
#define CMD_STA               0x400
#define CMD_BOARD             0x800
#define CMD_ADMIN             0x1000
#define CMD_DEV               0x2000
#define CMD_DEP               0x4000
#define CMD_UNCAT             0x8000

#define CMD_ALL               0xffff

/* Initializer for category string array */

#define CMD_CATEGORY_STRINGS_INIT { \
	"phy",   \
	"chan",  \
	"rate",  \
	"power", \
	"mac",   \
	"mgmt",  \
	"sec",   \
	"wme",   \
	"mon",   \
	"ap",    \
	"sta",   \
	"board", \
	"admin", \
	"dev",   \
	"dep",   \
	"uncat", \
	"" }

extern const char *wl_cmd_category_strings[];
extern const int wl_cmd_category_count;

/* Initializer for category description strings array */
#define CMD_CATEGORY_DESC_INIT { \
	"PHY and radio; speed, band, etc", \
	"Channel; subclass of phy",  \
	"Rate; subclass of phy, a/b/g",  \
	"Power; subclass of phy", \
	"MAC; Media access",   \
	"Management, association, IE, etc",  \
	"Security; subclass of mgmt",   \
	"WME; media extensions",   \
	"Monitoring device (counters, etc)",   \
	"AP subclass of mgmt", \
	"STA subclass of mgmt", \
	"Board, hardware", \
	"Administration; software, UI, diags", \
	"Device; low level control", \
	"Deprecated", \
	"Uncategorized so far", \
	"" }

extern const char *wl_cmd_category_desc[];

/*
 *
 * IO variable information
 *
 */

/* Supplemental IO variable info structure */
typedef const struct wlu_iov_info_s {
	const char *name;
	uint32 cat;  /* Category flags; same as command categories */
	uint32 flags; /* See below */
	int dflt;  /* Only for integers; see flags */
	const char *desc;  /* Description */
} wlu_iov_info_t;

/* Flags for wlu_iov_info_t */
#define WLU_IOVI_READ_ONLY         0x1   /* Known to be read only */
#define WLU_IOVI_WRITE_ONLY        0x2   /* Known to be write only */
#define WLU_IOVI_BCM_INTERNAL      0x4   /* Known to be BCM internal */
#define WLU_IOVI_DEFAULT_VALID     0x8   /* Default value in structure is valid */

extern wlu_iov_info_t wlu_iov_info[];
extern int wlu_iov_info_count;

#define WLU_IOV_BLOCK_LEN 10

#define WLU_MOD_NAME_MAX 16
#define WLU_MOD_NAME_BYTES 16

#endif /* _wlu_cmd_h_ */
