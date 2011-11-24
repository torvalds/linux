/*
 * Copyright (c) 2006 Atheros Communications Inc.
 * All rights reserved.
 *
 *
 * 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <limits.h>
#include <asm/types.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/version.h>
#ifdef ANDROID
#include "wireless_copy.h"
#else
#include <linux/wireless.h>
#endif 
#include <a_config.h>
#include <a_osapi.h>
#include <a_types.h>
#include <athdefs.h>
#include <ieee80211.h>
#include <wmi.h>
#include <athdrv_linux.h>
#include <dbglog_api.h>
#include <dirent.h>
#include "wac_defs.h"
//#include "wpa_ctrl.h"
//#include "os.h"

#undef DEBUG
#undef DBGLOG_DEBUG

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,21)
/* ---------- CONSTANTS --------------- */
#define ATH_WE_HEADER_TYPE_NULL     0         /* Not available */
#define ATH_WE_HEADER_TYPE_CHAR     2         /* char [IFNAMSIZ] */
#define ATH_WE_HEADER_TYPE_UINT     4         /* __u32 */
#define ATH_WE_HEADER_TYPE_FREQ     5         /* struct iw_freq */
#define ATH_WE_HEADER_TYPE_ADDR     6         /* struct sockaddr */
#define ATH_WE_HEADER_TYPE_POINT    8         /* struct iw_point */
#define ATH_WE_HEADER_TYPE_PARAM    9         /* struct iw_param */
#define ATH_WE_HEADER_TYPE_QUAL     10        /* struct iw_quality */

#define ATH_WE_DESCR_FLAG_DUMP      0x0001    /* Not part of the dump command */
#define ATH_WE_DESCR_FLAG_EVENT     0x0002    /* Generate an event on SET */
#define ATH_WE_DESCR_FLAG_RESTRICT  0x0004    /* GET : request is ROOT only */
#define ATH_WE_DESCR_FLAG_NOMAX     0x0008    /* GET : no limit on request size */

#define ATH_SIOCSIWMODUL            0x8b2f
#define ATH_SIOCGIWMODUL            0x8b2f
#define ATH_WE_VERSION          (A_INT16)22
/* ---------------------------- TYPES ---------------------------- */

/*
 * standard IOCTL looks like.
 */
struct ath_ioctl_description
{
    A_UINT8         header_type;        /* NULL, iw_point or other */
    A_UINT8         token_type;     /* Future */
    A_UINT16    token_size;     /* Granularity of payload */
    A_UINT16    min_tokens;     /* Min acceptable token number */
    A_UINT16    max_tokens;     /* Max acceptable token number */
    A_UINT32    flags;          /* Special handling of the request */
};


/* -------------------------- VARIABLES -------------------------- */

/*
 * Meta-data about all the standard Wireless Extension request we
 * know about.
 */
static const struct ath_ioctl_description standard_ioctl_descr[] = {
    [SIOCSIWCOMMIT  - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_NULL,
    },
    [SIOCGIWNAME    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_CHAR,
        .flags      = ATH_WE_DESCR_FLAG_DUMP,
    },
    [SIOCSIWNWID    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
        .flags      = ATH_WE_DESCR_FLAG_EVENT,
    },
    [SIOCGIWNWID    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
        .flags      = ATH_WE_DESCR_FLAG_DUMP,
    },
    [SIOCSIWFREQ    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_FREQ,
        .flags      = ATH_WE_DESCR_FLAG_EVENT,
    },
    [SIOCGIWFREQ    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_FREQ,
        .flags      = ATH_WE_DESCR_FLAG_DUMP,
    },
    [SIOCSIWMODE    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_UINT,
        .flags      = ATH_WE_DESCR_FLAG_EVENT,
    },
    [SIOCGIWMODE    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_UINT,
        .flags      = ATH_WE_DESCR_FLAG_DUMP,
    },
    [SIOCSIWSENS    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCGIWSENS    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCSIWRANGE   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_NULL,
    },
    [SIOCGIWRANGE   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = sizeof(struct iw_range),
        .flags      = ATH_WE_DESCR_FLAG_DUMP,
    },
    [SIOCSIWPRIV    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_NULL,
    },
    [SIOCGIWPRIV    - SIOCIWFIRST] = { /* (handled directly by us) */
        .header_type    = ATH_WE_HEADER_TYPE_NULL,
    },
    [SIOCSIWSTATS   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_NULL,
    },
    [SIOCGIWSTATS   - SIOCIWFIRST] = { /* (handled directly by us) */
        .header_type    = ATH_WE_HEADER_TYPE_NULL,
        .flags      = ATH_WE_DESCR_FLAG_DUMP,
    },
    [SIOCSIWSPY - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = sizeof(struct sockaddr),
        .max_tokens = IW_MAX_SPY,
    },
    [SIOCGIWSPY - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = sizeof(struct sockaddr) +
                  sizeof(struct iw_quality),
        .max_tokens = IW_MAX_SPY,
    },
    [SIOCSIWTHRSPY  - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = sizeof(struct iw_thrspy),
        .min_tokens = 1,
        .max_tokens = 1,
    },
    [SIOCGIWTHRSPY  - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = sizeof(struct iw_thrspy),
        .min_tokens = 1,
        .max_tokens = 1,
    },
    [SIOCSIWAP  - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_ADDR,
    },
    [SIOCGIWAP  - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_ADDR,
        .flags      = ATH_WE_DESCR_FLAG_DUMP,
    },
    [SIOCSIWMLME    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = sizeof(struct iw_mlme),
        .max_tokens = sizeof(struct iw_mlme),
    },
    [SIOCGIWAPLIST  - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = sizeof(struct sockaddr) +
                  sizeof(struct iw_quality),
        .max_tokens = IW_MAX_AP,
        .flags      = ATH_WE_DESCR_FLAG_NOMAX,
    },
    [SIOCSIWSCAN    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = 0,
        .max_tokens = sizeof(struct iw_scan_req),
    },
    [SIOCGIWSCAN    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_SCAN_MAX_DATA,
        .flags      = ATH_WE_DESCR_FLAG_NOMAX,
    },
    [SIOCSIWESSID   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ESSID_MAX_SIZE + 1,
        .flags      = ATH_WE_DESCR_FLAG_EVENT,
    },
    [SIOCGIWESSID   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ESSID_MAX_SIZE + 1,
        .flags      = ATH_WE_DESCR_FLAG_DUMP,
    },
    [SIOCSIWNICKN   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ESSID_MAX_SIZE + 1,
    },
    [SIOCGIWNICKN   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ESSID_MAX_SIZE + 1,
    },
    [SIOCSIWRATE    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCGIWRATE    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCSIWRTS - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCGIWRTS - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCSIWFRAG    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCGIWFRAG    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCSIWTXPOW   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCGIWTXPOW   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCSIWRETRY   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCGIWRETRY   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCSIWENCODE  - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ENCODING_TOKEN_MAX,
        .flags      = ATH_WE_DESCR_FLAG_EVENT | ATH_WE_DESCR_FLAG_RESTRICT,
    },
    [SIOCGIWENCODE  - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ENCODING_TOKEN_MAX,
        .flags      = ATH_WE_DESCR_FLAG_DUMP | ATH_WE_DESCR_FLAG_RESTRICT,
    },
    [SIOCSIWPOWER   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCGIWPOWER   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [ATH_SIOCSIWMODUL   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [ATH_SIOCGIWMODUL   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCSIWGENIE   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [SIOCGIWGENIE   - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [SIOCSIWAUTH    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCGIWAUTH    - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_PARAM,
    },
    [SIOCSIWENCODEEXT - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = sizeof(struct iw_encode_ext),
        .max_tokens = sizeof(struct iw_encode_ext) +
                  IW_ENCODING_TOKEN_MAX,
    },
    [SIOCGIWENCODEEXT - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = sizeof(struct iw_encode_ext),
        .max_tokens = sizeof(struct iw_encode_ext) +
                  IW_ENCODING_TOKEN_MAX,
    },
    [SIOCSIWPMKSA - SIOCIWFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = sizeof(struct iw_pmksa),
        .max_tokens = sizeof(struct iw_pmksa),
    },
};
static const unsigned int standard_ioctl_num = (sizeof(standard_ioctl_descr) /
                        sizeof(struct ath_ioctl_description));

/*
 * Meta-data about all the additional standard Wireless Extension events
 */
static const struct ath_ioctl_description standard_event_descr[] = {
    [IWEVTXDROP - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_ADDR,
    },
    [IWEVQUAL   - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_QUAL,
    },
    [IWEVCUSTOM - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_CUSTOM_MAX,
    },
    [IWEVREGISTERED - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_ADDR,
    },
    [IWEVEXPIRED    - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_ADDR,
    },
    [IWEVGENIE  - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [IWEVMICHAELMICFAILURE  - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = sizeof(struct iw_michaelmicfailure),
    },
    [IWEVASSOCREQIE - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [IWEVASSOCRESPIE    - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [IWEVPMKIDCAND  - IWEVFIRST] = {
        .header_type    = ATH_WE_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = sizeof(struct iw_pmkid_cand),
    },
};
static const unsigned int standard_event_num = (sizeof(standard_event_descr) /
                        sizeof(struct ath_ioctl_description));

/* Size (in bytes) of various events */
static const int event_type_size[] = {
    IW_EV_LCP_PK_LEN,   /* ATH_WE_HEADER_TYPE_NULL */
    0,
    IW_EV_CHAR_PK_LEN,  /* ATH_WE_HEADER_TYPE_CHAR */
    0,
    IW_EV_UINT_PK_LEN,  /* ATH_WE_HEADER_TYPE_UINT */
    IW_EV_FREQ_PK_LEN,  /* ATH_WE_HEADER_TYPE_FREQ */
    IW_EV_ADDR_PK_LEN,  /* ATH_WE_HEADER_TYPE_ADDR */
    0,
    IW_EV_POINT_PK_LEN, /* Without variable payload */
    IW_EV_PARAM_PK_LEN, /* ATH_WE_HEADER_TYPE_PARAM */
    IW_EV_QUAL_PK_LEN,  /* ATH_WE_HEADER_TYPE_QUAL */
};

//static const char *ctrl_iface_dir = "/var/run/wpa_supplicant";
//static struct wpa_ctrl *ctrl_conn;
//static char *ctrl_ifname = NULL;

/* Structure used for parsing event list, such as Wireless Events
 * and scan results */
typedef struct event_list
{
  A_INT8 *	end;		/* End of the list */
  A_INT8 *	current;	/* Current event in list of events */
  A_INT8 *	value;		/* Current value in event */
} event_list;

#endif /*#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,21) */




#define MAX_WAC_BSS	10
WMI_BSS_INFO_HDR2   myBssInfo[MAX_WAC_BSS];
int     myBssCnt = 0;
int	    wps_pending = 0;
char    exec_path[128];
char    wps_pin[9];

//DBG
int fakeId = 0;

// utilities functions
typedef long os_time_t;

struct os_time {
	os_time_t sec;
	os_time_t usec;
};

static int os_get_random(unsigned char *buf, size_t len)
{
	FILE *f;
	size_t rc;

	f = fopen("/dev/urandom", "rb");
	if (f == NULL) {
		printf("Could not open /dev/urandom.\n");
		return -1;
	}

	rc = fread(buf, 1, len, f);
	fclose(f);

	return rc != len ? -1 : 0;
}

static int os_get_time(struct os_time *t)
{
	int res;
	struct timeval tv;
	res = gettimeofday(&tv, NULL);
	t->sec = tv.tv_sec;
	t->usec = tv.tv_usec;
	return res;
}

static unsigned long os_random(void)
{
	return random();
}

/**
 * wps_pin_checksum - Compute PIN checksum
 * @pin: Seven digit PIN (i.e., eight digit PIN without the checksum digit)
 * Returns: Checksum digit
 */
unsigned int wps_pin_checksum(unsigned int pin)
{
	unsigned int accum = 0;
	while (pin) {
		accum += 3 * (pin % 10);
		pin /= 10;
		accum += pin % 10;
		pin /= 10;
	}

	return (10 - accum % 10) % 10;
}

/**
 * wps_generate_pin - Generate a random PIN
 * Returns: Eight digit PIN (i.e., including the checksum digit)
 */
unsigned int wps_generate_pin(void)
{
	unsigned int val;

	/* Generate seven random digits for the PIN */
	if (os_get_random((unsigned char *) &val, sizeof(val)) < 0) {
		struct os_time now;
		os_get_time(&now);
		val = os_random() ^ now.sec ^ now.usec;
	}
	val %= 10000000;

	/* Append checksum digit */
	return val * 10 + wps_pin_checksum(val);
}

static void str2hex(A_UINT8 *hex, char *str);
//
////////////////////////////////////////////////////////////////////////////////
// Library APIs
int wac_enable(int s, int enable, unsigned int period, unsigned int scan_thres, int rssi_thres)
{
    char ifname[IFNAMSIZ];
    struct ifreq ifr;
    char *ethIf;
    char *cmd_buf = malloc(256);
    WMI_WAC_ENABLE_CMD *cmd = (WMI_WAC_ENABLE_CMD *)(cmd_buf+4);
    unsigned int pin = wps_generate_pin();

    memset(cmd_buf, 0, sizeof(cmd_buf));
    memset(ifname, '\0', IFNAMSIZ);
    cmd->enable = enable;
    cmd->period = period;
    cmd->threshold = scan_thres;
    cmd->rssi = rssi_thres;
    snprintf(wps_pin, 9, "%d", pin);
    memcpy(cmd->wps_pin, wps_pin, 8);

    if ((ethIf = getenv("NETIF")) == NULL) {
        ethIf = "eth1";
    }
    strcpy(ifname, ethIf);
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    ((int *)cmd_buf)[0] = AR6000_XIOCTL_WMI_ENABLE_WAC_PARAM;
    ifr.ifr_data = cmd_buf;
    if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
    {
        err(1, "%s", ifr.ifr_name);
        free(cmd_buf);
        return -1;
    }
    printf("cmd = Wac Enable: %x, %x %x, %x\n", enable, period, scan_thres, rssi_thres);
    printf("random PIN = %s\n", wps_pin);
    free(cmd_buf);

    return 0;
}

void wac_control_request( int s,
                          WAC_REQUEST_TYPE req, 
                          WAC_COMMAND cmd, 
                          WAC_FRAME_TYPE frm, 
                          char *ie, 
                          int *ret_val, 
                          WAC_STATUS *status )
{
    char ifname[IFNAMSIZ];
    struct ifreq ifr;
    char *ethIf;
    char *cmd_buf = malloc(256);
    WMI_WAC_CTRL_REQ_CMD *ctrl_cmd = (WMI_WAC_CTRL_REQ_CMD *)(cmd_buf+4);

    memset(cmd_buf, 0, 256);
    memset(ifname, '\0', IFNAMSIZ);

    if ((ethIf = getenv("NETIF")) == NULL) {
        ethIf = "eth1";
    }
    strcpy(ifname, ethIf);
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    *ret_val = 0;

    if ( WAC_SET == req ) {
        if ( PRBREQ == frm ) {
            switch (cmd) {
            case WAC_ADD:
            {
                if (0 != strncmp(ie, "0x", 2)) {
                    printf("expect \'ie\' in hex format with 0x\n");
                    *ret_val = -1;
                    goto done_ctrl_req;
                }

                if (0 != (strlen(ie) % 2)) {
                    printf("expect \'ie\' to be even length\n");
                    *ret_val = -1;
                    goto done_ctrl_req;
                }

                ctrl_cmd->req = WAC_SET;
                ctrl_cmd->cmd = WAC_ADD;
                ctrl_cmd->frame = PRBREQ;
                str2hex(ctrl_cmd->ie, ie);

                ((int *)cmd_buf)[0] = AR6000_XIOCTL_WMI_WAC_CTRL_REQ;
                ifr.ifr_data = cmd_buf;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                {
                    err(1, "%s", ifr.ifr_name);
                    *ret_val = -1;
                }
                break;
            }

            case WAC_DEL:
            {
                ctrl_cmd->req = WAC_SET;
                ctrl_cmd->cmd = WAC_DEL;
                ctrl_cmd->frame = PRBREQ;

                ((int *)cmd_buf)[0] = AR6000_XIOCTL_WMI_WAC_CTRL_REQ;
                ifr.ifr_data = cmd_buf;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                {
                    err(1, "%s", ifr.ifr_name);
                    *ret_val = -1;
                }
                break;
            }

            default:
                printf("unknown command %d\n", cmd);
                *ret_val = -1;
                break;
            }
        }
        else {
            printf("unknown set frame type %d\n", frm);
            *ret_val = -1;
        }
    }
    else if ( WAC_GET == req ) {
        if ( WAC_GET_STATUS == cmd ) {  // GET STATUS
            ctrl_cmd->req = WAC_GET;
            ctrl_cmd->cmd = WAC_GET_STATUS;

            ((int *)cmd_buf)[0] = AR6000_XIOCTL_WMI_WAC_CTRL_REQ;
            ifr.ifr_data = cmd_buf;
            if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
            {
                err(1, "%s", ifr.ifr_name);
                *ret_val = -1;
                goto done_ctrl_req;
            }
            A_MEMCPY(status, ifr.ifr_data, sizeof(WAC_STATUS));
        } 
        else if ( PRBREQ == frm ) {   // GET IE
            ctrl_cmd->req = WAC_GET;
            ctrl_cmd->cmd = WAC_GET_IE;

            ((int *)cmd_buf)[0] = AR6000_XIOCTL_WMI_WAC_CTRL_REQ;
            ifr.ifr_data = cmd_buf;
            if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
            {
                err(1, "%s", ifr.ifr_name);
                *ret_val = -1;
                goto done_ctrl_req;
            }
            A_MEMCPY(ie, ifr.ifr_data, sizeof(WMI_GET_WAC_INFO));
        }
        else {
            printf("unknown get request\n");
            *ret_val = -1;
        }
    }
    else
    {
        printf("unkown request type %d\n", req);
        *ret_val = -1;
    }

done_ctrl_req:
    free(cmd_buf);
}

////////////////////////////////////////////////////////////////////////////////

static int htoi(char c)
{
	if (c >= 'a' && c <= 'f') {
		return (c - 'a' + 10);
	}

	if (c >= 'A' && c <= 'F') {
		return (c - 'A' + 10);
	}

	if (c >= '0' && c <= '9') {
		return (c - '0');
	}

	return 0;
}

static void str2hex(A_UINT8 *hex, char *str)
{
    int i;
    int len = strlen(str);

    for (i = 2; i < len; i+=2) { 
        hex[(i-2)/2] = htoi(str[i]) * 16 + htoi(str[i+1]);
    }
}

static void getBssid(char *bssid)
{
    WMI_BSS_INFO_HDR2 *p = &myBssInfo[0];
	char tmp[3];

    if (fakeId == (MAX_WAC_BSS+1)) {
        strcpy(bssid, "1c:af:f7:1b:81:82");
        return;
    }

	sprintf(tmp, "%02x", p->bssid[0]);
	strcpy(bssid, tmp);
	strcat(bssid, ":");
	sprintf(tmp, "%02x", p->bssid[1]);
	strcat(bssid, tmp);
	strcat(bssid, ":");
	sprintf(tmp, "%02x", p->bssid[2]);
	strcat(bssid, tmp);
	strcat(bssid, ":");
	sprintf(tmp, "%02x", p->bssid[3]);
	strcat(bssid, tmp);
	strcat(bssid, ":");
	sprintf(tmp, "%02x", p->bssid[4]);
	strcat(bssid, tmp);
	strcat(bssid, ":");
	sprintf(tmp, "%02x", p->bssid[5]);
	strcat(bssid, tmp);

	printf("bssid = %s\n", bssid);
}

static int check_stored_profile(A_UINT8 *bssid)
{
	FILE *fp;
	char buffer[128];
	char full_cmd[256];
//        char *cmd = "../../.output/LOCAL_i686-SDIO/image/wpa_cli list_networks | grep -o \'\\w\\w:\\w\\w:\\w\\w:\\w\\w:\\w\\w:\\w\\w\'";

	memset(full_cmd, '\0', 256);
        strcpy(full_cmd, exec_path);
	strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli list_networks | grep -o \'\\w\\w:\\w\\w:\\w\\w:\\w\\w:\\w\\w:\\w\\w\'");

	memset(buffer, '\0', 128);
	fp = popen(full_cmd, "r");
	fgets(buffer, 128, fp);
	pclose(fp);

	if (buffer[0] == '\0') {
		printf("no stored profile\n");
		return 1;
	}

	printf("profile bssid = %s\n", buffer);

    bssid[0] = htoi(buffer[0]) * 16 + htoi(buffer[1]);
    bssid[1] = htoi(buffer[3]) * 16 + htoi(buffer[4]);
    bssid[2] = htoi(buffer[6]) * 16 + htoi(buffer[7]);
    bssid[3] = htoi(buffer[9]) * 16 + htoi(buffer[10]);
    bssid[4] = htoi(buffer[12]) * 16 + htoi(buffer[13]);
    bssid[5] = htoi(buffer[15]) * 16 + htoi(buffer[16]);

	printf("bssid in decimal: %d:%d:%d:%d:%d:%d\n", 
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

	return 0;
}

static int match_stored_profile(A_UINT8 *myBssid)
{
    int i;

    for (i = 0; i < MAX_WAC_BSS; i++) {
        if (0 == A_MEMCMP(myBssInfo[i].bssid, myBssid, 6)) {
            break;
        }
    }

    if (i < MAX_WAC_BSS) {
        printf("matched profile: %d\n", i);
        return i;
    }
    else {
        printf("no matched profile: %d\n", i);
        return -1;
    }
}

static int select_best_bss()
{
    return 0;
}

static void wpa_profile_reconnect(void)
{
	char full_cmd[128];

	memset(full_cmd, '\0', 128);
    strcpy(full_cmd, exec_path);
	system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli ap_scan 1"));

	memset(full_cmd, '\0', 128);
    strcpy(full_cmd, exec_path);
	system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli save_config"));

	memset(full_cmd, '\0', 128);
    strcpy(full_cmd, exec_path);
	system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli reconfigure"));

    // wpa_cli ap_scan 1
    // wpa_cli save_config
    // wpa_cli reconfigure
    // wpa_cli ap_scan 0
    // wpa_cli save_config
}

static int wps_start(void)
{
    char full_cmd[128];
    char which_bssid[32];
    int ret = 0;

    memset(full_cmd, '\0', 128);
    strcpy(full_cmd, exec_path);
    system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli ap_scan 1"));

    memset(full_cmd, '\0', 128);
    strcpy(full_cmd, exec_path);
    system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli save_config"));

    memset(full_cmd, '\0', 128);
    strcpy(full_cmd, exec_path);
    system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli reconfigure"));

    memset(which_bssid, '\0', 32);
    getBssid(which_bssid);

    memset(full_cmd, '\0', 128);
    strcpy(full_cmd, exec_path);
    strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli wps_pin ");
    strcat(full_cmd, which_bssid);
    strcat(full_cmd, " ");
    strcat(full_cmd, wps_pin);

    wps_pending = 1;
    printf("wps_start: %s\n", full_cmd);

    ret = system(full_cmd);

    return ret;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,21)
int app_extract_events(struct event_list*   list,
                struct iw_event *   iwe);
#endif
static void
event_rtm_newlink(struct nlmsghdr *h, int len);

static void
event_wireless(A_INT8 *data, int len);

int
string_search(FILE *fp, char *string)
{
    char str[DBGLOG_DBGID_DEFINITION_LEN_MAX];

    rewind(fp);
    memset(str, 0, DBGLOG_DBGID_DEFINITION_LEN_MAX);
    while (!feof(fp)) {
        fscanf(fp, "%s", str);
        if (strstr(str, string)) return 1;
    }

    return 0;
}

#define RECEVENT_DEBUG_PRINTF(args...)

int s;  //socket
int main(int argc, char** argv)
{
//    int s, c, ret;
//    int s;
    struct sockaddr_nl local;
    struct sockaddr_nl from;
    socklen_t fromlen;
    struct nlmsghdr *h;
    char buf[16384];
    int left;
    FILE *fp;
    char *cmd = "pwd";
    int len;

    //DBG
    if (argc == 2) {
        fakeId = atoi(argv[1]);
    }

    // determine execution path
    memset(exec_path, '\0', 128);
    fp = popen(cmd, "r");
    fgets(exec_path, 128, fp);
    pclose(fp);

    len = strlen(exec_path);
    if ( '\n' == exec_path[len-1] ) {
        exec_path[len-1] = '/';
    }
    else {
        exec_path[len] = '/';
    }

    printf("exec_path = %s\n", exec_path);

    s = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (s < 0) {
        perror("socket(PF_NETLINK,SOCK_RAW,NETLINK_ROUTE)");
        return -1;
    }

    memset(&local, 0, sizeof(local));
    local.nl_family = AF_NETLINK;
    local.nl_groups = RTMGRP_LINK;
    if (bind(s, (struct sockaddr *) &local, sizeof(local)) < 0) {
        perror("bind(netlink)");
        close(s);
        return -1;
    }

    while (1) {
        fromlen = sizeof(from);
        left = recvfrom(s, buf, sizeof(buf), 0,
                        (struct sockaddr *) &from, &fromlen);
        if (left < 0) {
            if (errno != EINTR && errno != EAGAIN)
                perror("recvfrom(netlink)");
            break;
        }

        h = (struct nlmsghdr *) buf;

        while (left >= sizeof(*h)) {
            int len, plen;

            len = h->nlmsg_len;
            plen = len - sizeof(*h);
            if (len > left || plen < 0) {
                perror("Malformed netlink message: ");
                break;
            }

            switch (h->nlmsg_type) {
            case RTM_NEWLINK:
                event_rtm_newlink(h, plen);
                break;
            case RTM_DELLINK:
                RECEVENT_DEBUG_PRINTF("DELLINK\n");
                break;
            default:
                RECEVENT_DEBUG_PRINTF("OTHERS\n");
            }

            len = NLMSG_ALIGN(len);
            left -= len;
            h = (struct nlmsghdr *) ((char *) h + len);
        }
    }

    close(s);
    return 0;
}

static void
event_rtm_newlink(struct nlmsghdr *h, int len)
{
    struct ifinfomsg *ifi;
    int attrlen, nlmsg_len, rta_len;
    struct rtattr * attr;

    if (len < sizeof(*ifi)) {
        perror("too short\n");
        return;
    }

    ifi = NLMSG_DATA(h);

    nlmsg_len = NLMSG_ALIGN(sizeof(struct ifinfomsg));

    attrlen = h->nlmsg_len - nlmsg_len;
    if (attrlen < 0) {
        perror("bad attren\n");
        return;
    }

    attr = (struct rtattr *) (((char *) ifi) + nlmsg_len);

    rta_len = RTA_ALIGN(sizeof(struct rtattr));
    while (RTA_OK(attr, attrlen)) {
        if (attr->rta_type == IFLA_WIRELESS) {
            event_wireless( ((A_INT8*)attr) + rta_len, attr->rta_len - rta_len);
        } else if (attr->rta_type == IFLA_IFNAME) {

        }
        attr = RTA_NEXT(attr, attrlen);
    }
}

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,21)
/*------------------------------------------------------------------*/
/*
 * Extract the next event from the event list.
 */
int
app_extract_events(struct event_list*   list,   /* list of events */
            struct iw_event *   iwe /* Extracted event */
            )
{
  const struct ath_ioctl_description *  descr = NULL;
  int       event_type = 0;
  unsigned int  event_len = 1;      /* Invalid */
  A_INT8 *  pointer;
  A_INT16       we_version = ATH_WE_VERSION;
  unsigned  cmd_index;

  /* Check for end of list */
  if((list->current + IW_EV_LCP_PK_LEN) > list->end)
    return(0);

  /* Extract the event header (to get the event id).
   * Note : the event may be unaligned, therefore copy... */
  memcpy((char *) iwe, list->current, IW_EV_LCP_PK_LEN);

  /* Check invalid events */
  if(iwe->len <= IW_EV_LCP_PK_LEN)
    return(-1);

  /* Get the type and length of that event */
  if(iwe->cmd <= SIOCIWLAST)
    {
      cmd_index = iwe->cmd - SIOCIWFIRST;
      if(cmd_index < standard_ioctl_num)
    descr = &(standard_ioctl_descr[cmd_index]);
    }
  else
    {
      cmd_index = iwe->cmd - IWEVFIRST;
      if(cmd_index < standard_event_num)
    descr = &(standard_event_descr[cmd_index]);
    }
  if(descr != NULL)
    event_type = descr->header_type;
  /* Unknown events -> event_type=0 => IW_EV_LCP_PK_LEN */
  event_len = event_type_size[event_type];
  /* Fixup for earlier version of WE */
  if((we_version <= 18) && (event_type == ATH_WE_HEADER_TYPE_POINT))
    event_len += IW_EV_POINT_OFF;

  /* Check if we know about this event */
  if(event_len <= IW_EV_LCP_PK_LEN)
    {
      /* Skip to next event */
      list->current += iwe->len;
      return(2);
    }
  event_len -= IW_EV_LCP_PK_LEN;

  /* Set pointer on data */
  if(list->value != NULL)
    pointer = list->value;          /* Next value in event */
  else
    pointer = list->current + IW_EV_LCP_PK_LEN; /* First value in event */

  /* Copy the rest of the event (at least, fixed part) */
  if((pointer + event_len) > list->end)
    {
      /* Go to next event */
      list->current += iwe->len;
      return(-2);
    }
  /* Fixup for WE-19 and later : pointer no longer in the list */
  /* Beware of alignement. Dest has local alignement, not packed */
  if((we_version > 18) && (event_type == ATH_WE_HEADER_TYPE_POINT))
    memcpy((char *) iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF,
       pointer, event_len);
  else
    memcpy((char *) iwe + IW_EV_LCP_LEN, pointer, event_len);

  /* Skip event in the stream */
  pointer += event_len;

  /* Special processing for iw_point events */
  if(event_type == ATH_WE_HEADER_TYPE_POINT)
    {
      /* Check the length of the payload */
      unsigned int  extra_len = iwe->len - (event_len + IW_EV_LCP_PK_LEN);
      if(extra_len > 0)
    {
      /* Set pointer on variable part (warning : non aligned) */
      iwe->u.data.pointer = pointer;

      /* Check that we have a descriptor for the command */
      if(descr == NULL)
        /* Can't check payload -> unsafe... */
        iwe->u.data.pointer = NULL; /* Discard paylod */
      else
        {
          /* Those checks are actually pretty hard to trigger,
           * because of the checks done in the kernel... */

          unsigned int  token_len = iwe->u.data.length * descr->token_size;

          /* Ugly fixup for alignement issues.
           * If the kernel is 64 bits and userspace 32 bits,
           * we have an extra 4+4 bytes.
           * Fixing that in the kernel would break 64 bits userspace. */
          if((token_len != extra_len) && (extra_len >= 4))
        {
          A_UINT16      alt_dlen = *((A_UINT16 *) pointer);
          unsigned int  alt_token_len = alt_dlen * descr->token_size;
          if((alt_token_len + 8) == extra_len)
            {
              /* Ok, let's redo everything */
              pointer -= event_len;
              pointer += 4;
              /* Dest has local alignement, not packed */
              memcpy((char*) iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF,
                 pointer, event_len);
              pointer += event_len + 4;
              iwe->u.data.pointer = pointer;
              token_len = alt_token_len;
            }
        }

          /* Discard bogus events which advertise more tokens than
           * what they carry... */
          if(token_len > extra_len)
        iwe->u.data.pointer = NULL; /* Discard paylod */
          /* Check that the advertised token size is not going to
           * produce buffer overflow to our caller... */
          if((iwe->u.data.length > descr->max_tokens)
         && !(descr->flags & ATH_WE_DESCR_FLAG_NOMAX))
        iwe->u.data.pointer = NULL; /* Discard paylod */
          /* Same for underflows... */
          if(iwe->u.data.length < descr->min_tokens)
        iwe->u.data.pointer = NULL; /* Discard paylod */
        }
    }
      else
    /* No data */
    iwe->u.data.pointer = NULL;

      /* Go to next event */
      list->current += iwe->len;
    }
  else
    {
      /* Ugly fixup for alignement issues.
       * If the kernel is 64 bits and userspace 32 bits,
       * we have an extra 4 bytes.
       * Fixing that in the kernel would break 64 bits userspace. */
      if((list->value == NULL)
     && ((((iwe->len - IW_EV_LCP_PK_LEN) % event_len) == 4)
         || ((iwe->len == 12) && ((event_type == ATH_WE_HEADER_TYPE_UINT) ||
                      (event_type == ATH_WE_HEADER_TYPE_QUAL))) ))
    {
      pointer -= event_len;
      pointer += 4;
      /* Beware of alignement. Dest has local alignement, not packed */
      memcpy((char *) iwe + IW_EV_LCP_LEN, pointer, event_len);
      pointer += event_len;
    }

      /* Is there more value in the event ? */
      if((pointer + event_len) <= (list->current + iwe->len))
    /* Go to next value */
    list->value = pointer;
      else
    {
      /* Go to next event */
      list->value = NULL;
      list->current += iwe->len;
    }
    }
  return(1);
}
#endif /*#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,21) */

static void
event_wireless(A_INT8 *data, int len)
{
  A_INT8 *pos, *end, *custom, *buf;
  A_UINT16 eventid;
//  int status;
//  static int wps = 0;
//  A_STATUS status;

  pos = data;
  end = data + len;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,21)
  struct iw_event   iwe;
  struct event_list list;
  int           ret;

  /* Cleanup */
  memset((char *) &list, '\0', sizeof(struct event_list));

  /* Set things up */
  list.current = data;
  list.end = data + len;

  do
  {
      /* Extract an event and print it */
      ret = app_extract_events(&list, &iwe);
      if(ret != 0)
      {
          RECEVENT_DEBUG_PRINTF("\n cmd = %x, length = %d, ",iwe.cmd,iwe.u.data.length);
            
          switch (iwe.cmd) {
             case IWEVCUSTOM:
                custom = pos + IW_EV_POINT_LEN;
                if (custom + iwe.u.data.length > end)
                    return;
                buf = malloc(iwe.u.data.length + 1);
                if (buf == NULL) return;
                memcpy(buf, custom, iwe.u.data.length);
                eventid = *((A_UINT16*)buf);
                RECEVENT_DEBUG_PRINTF("\n eventid = %x",eventid);

                switch (eventid) {
                case (WMI_READY_EVENTID):
                    printf("event = Wmi Ready, len = %d\n", iwe.u.data.length);
//                    status = app_wmiready_event_rx((A_UINT8 *)(buf + ID_LEN), iwe.u.data.length - ID_LEN);
                    break;
                case (WMI_DISCONNECT_EVENTID):
                    printf("event = Wmi Disconnect, len = %d\n", iwe.u.data.length);
                    if (wps_pending) {
                        char full_cmd[128];

                        memset(full_cmd, '\0', 128);
                        strcpy(full_cmd, exec_path);
                        system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli ap_scan 1"));

                        memset(full_cmd, '\0', 128);
                        strcpy(full_cmd, exec_path);
                        system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli save_config"));

                        wps_pending = 0;
                    }
//                    status = app_disconnect_event_rx((A_UINT8 *)(buf + ID_LEN), iwe.u.data.length - ID_LEN);
                    break;
                case (WMI_SCAN_COMPLETE_EVENTID):
                    printf("event = Wmi Scan Complete, len = %d\n", iwe.u.data.length);
//                    status = app_scan_complete_event_rx((A_UINT8 *)(buf + ID_LEN), iwe.u.data.length - ID_LEN);
                    break;
                case (WMI_WAC_SCAN_DONE_EVENTID):
                {
                    char ifname[IFNAMSIZ];
                    struct ifreq ifr;
                    char *ethIf;
                    char *buf = malloc(256);
                    WMI_WAC_SCAN_REPLY_CMD *cmd = (WMI_WAC_SCAN_REPLY_CMD *)(buf+4);
                    int matched = 0, has_profile = 0;

                    memset(buf, 0, sizeof(buf));
                    memset(ifname, '\0', IFNAMSIZ);

                    if ((ethIf = getenv("NETIF")) == NULL) {
                        ethIf = "eth1";
                    }
                    strcpy(ifname, ethIf);
                    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

                    printf("event = WAC scan done, len = %d\n", iwe.u.data.length);

                    if (myBssCnt == 0) {
                        cmd->cmdid = -1;    //no BSS, continue WAC scan
                    }
                    else {
                        A_UINT8 myBssid[6];
                        // TODO: choose best BSS to connect
                        // OR match stored profile
                        if (check_stored_profile(myBssid)) {
                            // no stored profile, select best BSS to connect
                            has_profile = 0;
                        }
                        else {
                            has_profile = 1;
                            // has stored profile, match BSS with stored profile
                            if ( match_stored_profile(myBssid) >= 0 ) {
                                matched = 1;
                            }
                        }

                        if (!matched || !has_profile) {
                            cmd->cmdid = select_best_bss();
                        }
                        else {
                            // either has profile or found matched profile
                            // stop WAC scan
                            cmd->cmdid = -2;
                        }
                    }

                    //DBG
                    if (fakeId != 0) {
                        cmd->cmdid = fakeId; //fake BSS info
                    }
                    printf("Sending WAC scan reply %d\n", cmd->cmdid);

                    ((int *)buf)[0] = AR6000_XIOCTL_WAC_SCAN_REPLY;
                    ifr.ifr_data = buf;
                    if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                    {
                        err(1, "%s", ifr.ifr_name);
                    }

                    if ( -2 == cmd->cmdid ) {
                        // stop WAC scan -> reconnect
                        wpa_profile_reconnect();
                    }

                    break;
                }
                case WMI_WAC_REPORT_BSS_EVENTID:
                {
                    A_INT8 *rxB = buf+2;
                    WMI_BSS_INFO_HDR2 *p;
                    if (myBssCnt >= MAX_WAC_BSS) {
                        printf("Too many bss reported, quitting\n");
                        break;
                    }
                        
//                    printf("event = WAC report bss, len = %d\n", iwe.u.data.length);

                    p = &myBssInfo[myBssCnt++];
                    memcpy((A_UINT8 *)p, rxB, sizeof(WMI_BSS_INFO_HDR2));

                    p = (WMI_BSS_INFO_HDR2 *) rxB;
                    printf("event = WAC report bss: %x:%x:%x:%x:%x:%x\n", 
                            p->bssid[0],
                            p->bssid[1],
                            p->bssid[2],
                            p->bssid[3],
                            p->bssid[4],
                            p->bssid[5]);
                    break;
                }
                case WMI_WAC_START_WPS_EVENTID:
//                    printf("wps start\n");
                    if (wps_start()) {
                        printf("wps start failed\n");
                    }
                    break;

                default:
                    RECEVENT_DEBUG_PRINTF("Host received other event with id 0x%x\n",
                                     eventid);
                    break;
                }
                free(buf);
            break;
        case IWEVGENIE:
            custom = pos + IW_EV_POINT_LEN;
            if (custom + iwe.u.data.length > end)
                return;
            buf = malloc(iwe.u.data.length + 1);
            if (buf == NULL) return;
            memcpy(buf, custom, iwe.u.data.length);
            eventid = *((A_UINT16*)buf);

            switch (eventid) {
            case (WMI_CONNECT_EVENTID):
            {
                char ifname[IFNAMSIZ];
                struct ifreq ifr;
                char *ethIf;
                char *buf = malloc(256);
                WMI_WAC_ENABLE_CMD *cmd = (WMI_WAC_ENABLE_CMD *)(buf+4);

                memset(buf, 0, sizeof(buf));
                memset(ifname, '\0', IFNAMSIZ);

                if ((ethIf = getenv("NETIF")) == NULL) {
                    ethIf = "eth1";
                }
                strcpy(ifname, ethIf);
                strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

                ((int *)buf)[0] = AR6000_XIOCTL_WMI_ENABLE_WAC_PARAM;
                cmd->enable = 0;
                ifr.ifr_data = buf;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
                {
                    err(1, "%s", ifr.ifr_name);
                }
                printf("event = Wmi Connect, len = %d\n", iwe.u.data.length);
                printf("disabling WAC\n");
                if (!wps_pending) {
                    char full_cmd[128];

                    memset(full_cmd, '\0', 128);
                    strcpy(full_cmd, exec_path);
                    system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli ap_scan 0"));

                    memset(full_cmd, '\0', 128);
                    strcpy(full_cmd, exec_path);
                    system(strcat(full_cmd, "../../.output/LOCAL_i686-SDIO/image/wpa_cli save_config"));
                }

//                status = app_connect_event_rx((A_UINT8 *)(buf + ID_LEN), iwe.u.data.length - ID_LEN);
                break;
            }
            case WMI_ENABLE_WAC_CMDID:
            {
                WMI_WAC_ENABLE_CMD *cmd = (WMI_WAC_ENABLE_CMD *)(buf+2);
                
                wac_enable(s, cmd->enable, cmd->period, cmd->threshold, cmd->rssi);
                break;
            }
            default:
                break;
            }
            free(buf);
        break;

        default:
            RECEVENT_DEBUG_PRINTF("event = Others\n");
            break;
      }
    }
    }
    while(ret > 0);

#endif
}

