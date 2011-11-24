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
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/version.h>
extern char *if_indextoname (unsigned int __ifindex, char *__ifname);
#include <ctype.h>
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

#undef DEBUG
#undef DBGLOG_DEBUG

#define ID_LEN                         2
#define DBGLOG_FILE                    "dbglog.h"
#define DBGLOGID_FILE                  "dbglog_id.h"

#define GET_CURRENT_TIME(s) do { \
    time_t t; \
    t = time(NULL); \
    s = strtok(ctime(&t), "\n"); \
} while (0);

#ifdef ANDROID 
#define DEBUG 1
#include <cutils/log.h>
#endif


#define AR6K_DBG_BUFFER_SIZE 256 

struct dbg_binary_header {
    A_UINT8     sig;
    A_UINT8     ver;
    A_UINT16    len;
    A_UINT32    reserved;
};

struct dbg_binary_record {
    A_UINT32 ts;                /* Timestamp of the log */
    A_UINT32 length;            /* Length of the log */
    A_UINT8  log[AR6K_DBG_BUFFER_SIZE]; /* log message */
};

static int ATH_WE_VERSION = 0;

#define SRCDIR_FLAG            0x01
#define LOGFILE_FLAG           0x02
#define DBGREC_LIMIT_FLAG      0x04
#define RESTORE_FLAG           0x08
#define BINARY_FLAG            0x10

const char *progname;
char restorefile[PATH_MAX];
char dbglogfile[PATH_MAX];
char dbglogidfile[PATH_MAX];
char dbglogoutfile[PATH_MAX];
FILE *fpout;
int dbgRecLimit = 1000000; /* Million records is a good default */
int optionflag;
char dbglog_id_tag[DBGLOG_MODULEID_NUM_MAX][DBGLOG_DBGID_NUM_MAX][DBGLOG_DBGID_DEFINITION_LEN_MAX];
const char options[] = 
"Options:\n\
-f, --logfile=<Output log file> [Mandatory]\n\
-g, --debug (-gg to print out dbglog info together) [Optional]\n\
-b, --binary\n\
-d, --srcdir=<Directory containing the dbglog header files> [Mandatory if not binary]\n\
-l, --reclimit=<Maximum number of records before the log rolls over> [Optional]\n\
-r, --restore=<Script to recover from errors on the target> [Optional]\n\
The options can also be given in the abbreviated form --option=x or -o x. The options can be given in any order";

#ifdef DEBUG
static int debugRecEventLevel = 0;
#ifdef ANDROID 
static const char TAGS[] = "recEvent";
#define RECEVENT_DEBUG_PRINTF(args...)        \
    if (debugRecEventLevel)  __android_log_print(ANDROID_LOG_DEBUG, TAGS, ##args);
#define RECEVENT_DBGLOG_PRINTF(args...)      \
    if (debugRecEventLevel>1) __android_log_print(ANDROID_LOG_DEBUG, TAGS, ##args);
#else
#define RECEVENT_DEBUG_PRINTF(args...)    if (debugRecEventLevel) printf(args);
#define RECEVENT_DBGLOG_PRINTF(args...) if (debugRecEventLevel>1) printf(args);
#endif
#else
#define RECEVENT_DEBUG_PRINTF(args...)
#define RECEVENT_DBGLOG_PRINTF(args...)
#endif

static A_STATUS app_sleep_report_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_wmiready_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_connect_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_disconnect_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_bssInfo_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_pstream_timeout_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_reportError_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_rssi_threshold_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_scan_complete_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_challenge_resp_event_rx(A_UINT8 *datap, size_t len);
static A_STATUS app_target_debug_event_rx(A_INT8 *datap, size_t len);

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

void
get_module_name(char *string, char *dest)
{
    char *str1, *str2;
    char str[DBGLOG_DBGID_DEFINITION_LEN_MAX];

    memset(str, 0, DBGLOG_DBGID_DEFINITION_LEN_MAX);
    strcpy(str, string);
    str1 = strtok(str, "_");
    while ((str2 = strtok(NULL, "_"))) {
        str1 = str2;
    }

    strcpy(dest, str1);
}

#ifdef DBGLOG_DEBUG
void
dbglog_print_id_tags(void)
{
    int i, j;

    for (i = 0; i < DBGLOG_MODULEID_NUM_MAX; i++) {
        for (j = 0; j < DBGLOG_DBGID_NUM_MAX; j++) {
            printf("[%d][%d]: %s\n", i, j, dbglog_id_tag[i][j]);
        }
    }
}
#endif /* DBGLOG_DEBUG */

int
dbglog_generate_id_tags(void)
{
    int id1, id2;
    FILE *fp1, *fp2;
    char str1[DBGLOG_DBGID_DEFINITION_LEN_MAX];
    char str2[DBGLOG_DBGID_DEFINITION_LEN_MAX];
    char str3[DBGLOG_DBGID_DEFINITION_LEN_MAX];

    if (!(fp1 = fopen(dbglogfile, "r"))) {
        perror(dbglogfile);
        return -1;
    }

    if (!(fp2 = fopen(dbglogidfile, "r"))) {
        perror(dbglogidfile);
        fclose(fp1);
        return -1;
    }

    memset(dbglog_id_tag, 0, sizeof(dbglog_id_tag));
    if (string_search(fp1, "DBGLOG_MODULEID_START")) {
        fscanf(fp1, "%s %s %d", str1, str2, &id1);
        do {
            memset(str3, 0, DBGLOG_DBGID_DEFINITION_LEN_MAX);
            get_module_name(str2, str3);
            strcat(str3, "_DBGID_DEFINITION_START");
            if (string_search(fp2, str3)) {
                memset(str3, 0, DBGLOG_DBGID_DEFINITION_LEN_MAX);
                get_module_name(str2, str3);
                strcat(str3, "_DBGID_DEFINITION_END");
                fscanf(fp2, "%s %s %d", str1, str2, &id2);
                while (!(strstr(str2, str3))) {
                    strcpy((char *)&dbglog_id_tag[id1][id2], str2);
                    fscanf(fp2, "%s %s %d", str1, str2, &id2);
                }
            }
            fscanf(fp1, "%s %s %d", str1, str2, &id1);
        } while (!(strstr(str2, "DBGLOG_MODULEID_END")));
    }

    fclose(fp2);
    fclose(fp1);

    return 0;
}

static void
usage(void)
{
    fprintf(stderr, "Usage:\n%s options\n", progname);
    fprintf(stderr, "%s\n", options);
    exit(-1);
}

int main(int argc, char** argv)
{
    int s, c, ret;
    struct sockaddr_nl local;
    struct sockaddr_nl from;
    socklen_t fromlen;
    struct nlmsghdr *h;
    char buf[16384];
    int left;

    progname = argv[0];

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"logfile", 1, NULL, 'f'},
            {"debug", 0, NULL, 'g' },
            {"binary", 0, NULL, 'b'},
            {"srcdir", 1, NULL, 'd'},
            {"reclimit", 1, NULL, 'l'},
            {"restore", 1, NULL, 'r'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "f:gbd:l:r:", long_options, &option_index);
        if (c == -1) break;

        switch (c) {
            case 'g':
#ifdef DEBUG
                ++debugRecEventLevel;
#endif
                break;
            case 'f':
                memset(dbglogoutfile, 0, PATH_MAX);
                strncpy(dbglogoutfile, optarg, sizeof(dbglogoutfile)-1);
                optionflag |= LOGFILE_FLAG;
                break;

            case 'b':
                optionflag |= BINARY_FLAG;
                break;

            case 'd':
                memset(dbglogfile, 0, PATH_MAX);
                strncpy(dbglogfile, optarg, sizeof(dbglogfile) - 1);
                strcat(dbglogfile, DBGLOG_FILE);
                memset(dbglogidfile, 0, PATH_MAX);
                strncpy(dbglogidfile, optarg, sizeof(dbglogidfile) - 1);
                strcat(dbglogidfile, DBGLOGID_FILE);
                optionflag |= SRCDIR_FLAG;
                break;

            case 'l':
                dbgRecLimit = strtoul(optarg, NULL, 0);
                break;

            case 'r':
                strncpy(restorefile, optarg, sizeof(restorefile)-1);
                optionflag |= RESTORE_FLAG;
                break;

            default:
                usage();
        }
    }

    if (!((optionflag & (SRCDIR_FLAG | BINARY_FLAG)) && (optionflag & LOGFILE_FLAG))) {
        usage();
    }

    /* Get the file name for dbglog output file */
    if (!(fpout = fopen(dbglogoutfile, "w+"))) {
        perror(dbglogoutfile);
        return -1;
    }

    if (optionflag & BINARY_FLAG) {
        struct dbg_binary_header header;

        header.sig = 0xDB;
        header.ver = 0x1;
        header.len = AR6K_DBG_BUFFER_SIZE;
        header.reserved = 0;
    
        fseek(fpout, 0, SEEK_SET);
        fwrite(&header, sizeof(header), 1, fpout); 
        /* first 8 bytes contains log header */
        fseek(fpout, 8, SEEK_SET);
    } else {
        /* first 8 bytes are to indicate the last record */
        fseek(fpout, 8, SEEK_SET);
        fprintf(fpout, "\n");
    }

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

    /* Generate id tags if dbglog header files are present */
    if (optionflag & SRCDIR_FLAG) {
        if ((ret = dbglog_generate_id_tags()) < 0) {
            return -1;
        }

#ifdef DBGLOG_DEBUG
        dbglog_print_id_tags();
#endif /* DBGLOG_DEBUG */
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

        while (left >= (int)sizeof(*h)) {
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

    fclose(fpout);
    close(s);
    return 0;
}

static void
event_rtm_newlink(struct nlmsghdr *h, int len)
{
    struct ifinfomsg *ifi;
    int attrlen, nlmsg_len, rta_len;
    struct rtattr * attr;

    if (len < (int)sizeof(*ifi)) {
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
            if (ATH_WE_VERSION==0) {
                int s = socket(PF_INET, SOCK_DGRAM, 0);
                struct iw_range range;
                struct iwreq iwr;
                if (s>=0) {                        
                    memset(&iwr, 0, sizeof(iwr));
                    if_indextoname(ifi->ifi_index, iwr.ifr_name);
                    iwr.u.data.pointer = (caddr_t) &range;
                    iwr.u.data.length = sizeof(range);
                    if (ioctl(s, SIOCGIWRANGE, &iwr) >= 0) {
                        ATH_WE_VERSION = range.we_version_compiled;
                    }
                    close(s);
                }
                if (ATH_WE_VERSION==0) {
                    RECEVENT_DEBUG_PRINTF("Fail to check we_version\n");
                    return ;
                } else {
                    RECEVENT_DEBUG_PRINTF("Get we version is %d", ATH_WE_VERSION);
                }
            }
            event_wireless( ((A_INT8*)attr) + rta_len, attr->rta_len - rta_len);
        } else if (attr->rta_type == IFLA_IFNAME) {

        }
        attr = RTA_NEXT(attr, attrlen);
    }
}

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

static void
event_wireless(A_INT8 *data, int len)
{
    struct iw_event iwe_buf, *iwe = &iwe_buf;
    A_INT8 *pos, *end, *custom, *buf;
    A_UINT16 eventid;
    A_STATUS status;

    pos = data;
    end = data + len;

    while (pos + IW_EV_LCP_LEN <= end) 
    {
        /* Event data may be unaligned, so make a local, aligned copy
         * before processing. */
        memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
        if (iwe->len <= IW_EV_LCP_LEN)
            break;

        custom = pos + IW_EV_POINT_LEN;
        if (ATH_WE_VERSION > 18 &&
            (iwe->cmd == IWEVMICHAELMICFAILURE ||
             iwe->cmd == IWEVCUSTOM ||
             iwe->cmd == IWEVASSOCREQIE ||
             iwe->cmd == IWEVASSOCRESPIE ||
             iwe->cmd == IWEVPMKIDCAND ||
             iwe->cmd == IWEVGENIE)) {
            /* WE-19 removed the pointer from struct iw_point */
            char *dpos = (char *) &iwe_buf.u.data.length;
            int dlen = dpos - (char *) &iwe_buf;
            memcpy(dpos, pos + IW_EV_LCP_LEN,
                   sizeof(struct iw_event) - dlen);
        } else {
            memcpy(&iwe_buf, pos, sizeof(struct iw_event));
            custom += IW_EV_POINT_OFF;
        }
            
        switch (iwe->cmd) {
        case SIOCGIWAP:                
            RECEVENT_DEBUG_PRINTF("event = new AP: "
               "%02x:%02x:%02x:%02x:%02x:%02x %s" ,
                MAC2STR((__u8 *) iwe->u.ap_addr.sa_data),
                (memcmp(iwe->u.ap_addr.sa_data,
                   "\x00\x00\x00\x00\x00\x00", 6) == 0 ||
                memcmp(iwe->u.ap_addr.sa_data,
                   "\x44\x44\x44\x44\x44\x44", 6) == 0) ?
                   "Disassociated\n" : " Associated\n");
            break;
        case IWEVMICHAELMICFAILURE:
            RECEVENT_DEBUG_PRINTF("event = Michael failure len = %d\n", iwe->u.data.length);
            break;
        case IWEVCUSTOM:
            if (custom + iwe->u.data.length > end || (iwe->u.data.length < ID_LEN)) {
                RECEVENT_DEBUG_PRINTF("event = IWEVCUSTOM with wrong length %d remain %d\n", 
                    iwe->u.data.length, (end-custom));
                return;
            }
            buf = malloc(iwe->u.data.length + 1);
            if (buf == NULL) return;
            memcpy(buf, custom, iwe->u.data.length);
            eventid = *((A_UINT16*)buf);

            switch (eventid) {
            case (WMI_READY_EVENTID):
                status = app_wmiready_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            case (WMI_DISCONNECT_EVENTID):
                status = app_disconnect_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            case (WMI_PSTREAM_TIMEOUT_EVENTID):
                status = app_pstream_timeout_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            case (WMI_ERROR_REPORT_EVENTID):
                status = app_reportError_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            case (WMI_RSSI_THRESHOLD_EVENTID):
                status = app_rssi_threshold_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            case (WMI_SCAN_COMPLETE_EVENTID):
                status = app_scan_complete_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            case (WMI_TX_RETRY_ERR_EVENTID):
                RECEVENT_DEBUG_PRINTF("event = Wmi Tx Retry Err, len = %d\n", iwe->u.data.length);
                break;
            case WMI_REPORT_SLEEP_STATE_EVENTID:
                status = app_sleep_report_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            case (WMIX_HB_CHALLENGE_RESP_EVENTID):
                status = app_challenge_resp_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            case (WMIX_DBGLOG_EVENTID):
                status = app_target_debug_event_rx((A_INT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            default:
#ifdef DEBUG
               if (isalnum(*buf) && isalnum(*(buf+1))) {
                    buf[iwe->u.data.length] = '\0';
                    RECEVENT_DEBUG_PRINTF("Host received custom event: %s\n", buf);
                } else {
                    RECEVENT_DEBUG_PRINTF("Host received other event with id 0x%x length %d\n",
                                          eventid, iwe->u.data.length);
                }
#endif
                break;
            }
            free(buf);
            break;
        case SIOCGIWSCAN:
            RECEVENT_DEBUG_PRINTF("event = scanComplete \n");
            break;
        case SIOCSIWESSID:
            RECEVENT_DEBUG_PRINTF("event = ESSID: ");
            break;
        case IWEVASSOCREQIE:
            RECEVENT_DEBUG_PRINTF("event = associte ReqIE \n");
            break;
        case IWEVASSOCRESPIE:
            RECEVENT_DEBUG_PRINTF("event = associte RespIE \n");
            break;
        case IWEVPMKIDCAND:
            RECEVENT_DEBUG_PRINTF("event = PMKID candidate \n");
            break;
        case IWEVGENIE:
            if (custom + iwe->u.data.length > end || (iwe->u.data.length < ID_LEN)) {
                RECEVENT_DEBUG_PRINTF("event = IWEVGENIE with wrong length %d remain %d\n", 
                                      iwe->u.data.length, (end-custom));
                return;
            }
            buf = malloc(iwe->u.data.length + 1);
            if (buf == NULL) return;
            memcpy(buf, custom, iwe->u.data.length);
            eventid = *((A_UINT16*)buf);

            switch (eventid) {
            case (WMI_BSSINFO_EVENTID):                    
                status = app_bssInfo_event_rx((A_UINT8 *)buf + ID_LEN, iwe->u.data.length - ID_LEN);
                break;
            case (WMI_CONNECT_EVENTID):                    
                status = app_connect_event_rx((A_UINT8 *)(buf + ID_LEN), iwe->u.data.length - ID_LEN);
                break;
            default:
                RECEVENT_DEBUG_PRINTF("Host received other generic event with id 0x%x\n", eventid);
                break;
            }
            free(buf);
            break;
        default:
            RECEVENT_DEBUG_PRINTF("event = Others %x length = %d\n", iwe->cmd, iwe->u.data.length);
            break;
        }
        pos += iwe->len;
    }
}

static void app_data_hexdump(const char *title, const unsigned char *buf, size_t len)
{
#ifdef DEBUG
    if (!debugRecEventLevel) {
        return;
    }
    if (len==0) {
        RECEVENT_DEBUG_PRINTF("%s - is EMPTY (len=%lu)\n", title, (unsigned long)len);
        return;
    }
    RECEVENT_DEBUG_PRINTF("%s - HEX DUMP(len=%lu):\n", title, (unsigned long)len);
    if (buf) {
        char line[82];
        size_t i = 0;
        while (i<len) {
            int j, next=0;
            for (j=0; j<16 && i<len; ++j, ++i) {
                next += sprintf(line+next, "%02X ", buf[i]);
                if (j==7) {
                    next += sprintf(line+next, "   ");
                }
            }
            if (j>0) {
                next += sprintf(line+next, "\n");
            }
            RECEVENT_DEBUG_PRINTF("%s", line);
        }        
    }
#endif
}

static A_STATUS app_sleep_report_event_rx(A_UINT8 *datap, size_t len)
{
    const char *status;
    WMI_REPORT_SLEEP_STATE_EVENT *ev;
    if(len < sizeof(WMI_REPORT_SLEEP_STATE_EVENT)) {
        return A_EINVAL;
    }
    ev = (WMI_REPORT_SLEEP_STATE_EVENT *)datap;
    switch(ev->sleepState) {
        case  WMI_REPORT_SLEEP_STATUS_IS_DEEP_SLEEP:
            status = "SLEEP";
            break;
        case WMI_REPORT_SLEEP_STATUS_IS_AWAKE:
            status = "AWAKE";
            break;
        default:
            status = "unknown";
            break;
    }
    RECEVENT_DEBUG_PRINTF("Application receive sleep report %s\n", status);
    return A_OK;
}

static A_STATUS app_wmiready_event_rx(A_UINT8 *datap, size_t len)
{
    WMI_READY_EVENT *ev;

    if (len < sizeof(WMI_READY_EVENT)) {
        return A_EINVAL;
    }
    ev = (WMI_READY_EVENT *)datap;
    RECEVENT_DEBUG_PRINTF("Application receive wmi ready event:\n");
    RECEVENT_DEBUG_PRINTF("mac address =  %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ",
              ev->macaddr[0], ev->macaddr[1], ev->macaddr[2], ev->macaddr[3],
              ev->macaddr[4], ev->macaddr[5]);
    RECEVENT_DEBUG_PRINTF("Physical capability = %d\n",ev->phyCapability);
    return A_OK;
}

static A_STATUS app_connect_event_rx(A_UINT8 *datap, size_t len)
{
    WMI_CONNECT_EVENT *ev;

    if (len < sizeof(WMI_CONNECT_EVENT)) {
        return A_EINVAL;
    }
    ev = (WMI_CONNECT_EVENT *)datap;

    RECEVENT_DEBUG_PRINTF("Application receive connected event (len=%d) on freq %d \n", len, ev->u.infra_ibss_bss.channel);
    RECEVENT_DEBUG_PRINTF("with bssid %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x "
            " listenInterval=%d, assocReqLen=%d assocRespLen =%d\n",
             ev->u.infra_ibss_bss.bssid[0], ev->u.infra_ibss_bss.bssid[1],
             ev->u.infra_ibss_bss.bssid[2], ev->u.infra_ibss_bss.bssid[3],
             ev->u.infra_ibss_bss.bssid[4], ev->u.infra_ibss_bss.bssid[5],
             ev->u.infra_ibss_bss.listenInterval, ev->assocReqLen, ev->assocRespLen);

    return A_OK;
}

static A_STATUS app_disconnect_event_rx(A_UINT8 *datap, size_t len)
{
    WMI_DISCONNECT_EVENT *ev;

    if (len < sizeof(WMI_DISCONNECT_EVENT)) {
        return A_EINVAL;
    }

    ev = (WMI_DISCONNECT_EVENT *)datap;
    RECEVENT_DEBUG_PRINTF("Application receive disconnected event: reason is %d protocol reason/status code is %d\n",
            ev->disconnectReason, ev->protocolReasonStatus);
    RECEVENT_DEBUG_PRINTF("Disconnect from %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x \n",
              ev->bssid[0], ev->bssid[1], ev->bssid[2], ev->bssid[3],
              ev->bssid[4], ev->bssid[5]);

    app_data_hexdump("AssocResp Frame", datap, ev->assocRespLen);
    return A_OK;
}

static A_STATUS app_bssInfo_event_rx(A_UINT8 *datap, size_t len)
{
    WMI_BSS_INFO_HDR *bih;

    if (len <= sizeof(WMI_BSS_INFO_HDR)) {
        return A_EINVAL;
    }
    bih = (WMI_BSS_INFO_HDR *)datap;
    RECEVENT_DEBUG_PRINTF("Application receive BSS info event, len = %d\n", len);
    RECEVENT_DEBUG_PRINTF("channel = %d, frame type = %d, snr = %d rssi = %d.\n",
            bih->channel, bih->frameType, bih->snr, bih->rssi);
    RECEVENT_DEBUG_PRINTF("BSSID is: %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x \n",
              bih->bssid[0], bih->bssid[1], bih->bssid[2], bih->bssid[3],
              bih->bssid[4], bih->bssid[5]);
    return A_OK;
}

static A_STATUS app_pstream_timeout_event_rx(A_UINT8 *datap, size_t len)
{
    WMI_PSTREAM_TIMEOUT_EVENT *ev;

    if (len < sizeof(WMI_PSTREAM_TIMEOUT_EVENT)) {
        return A_EINVAL;
    }
    ev = (WMI_PSTREAM_TIMEOUT_EVENT *)datap;
    RECEVENT_DEBUG_PRINTF("Application receive pstream timeout event:\n");
    RECEVENT_DEBUG_PRINTF("streamID= %d\n", ev->trafficClass);
    return A_OK;
}

static A_STATUS app_reportError_event_rx(A_UINT8 *datap, size_t len)
{
    WMI_TARGET_ERROR_REPORT_EVENT *reply;

    if (len < sizeof(WMI_TARGET_ERROR_REPORT_EVENT)) {
        return A_EINVAL;
    }
    reply = (WMI_TARGET_ERROR_REPORT_EVENT *)datap;
    RECEVENT_DEBUG_PRINTF("Application receive report error event\n");
    RECEVENT_DEBUG_PRINTF("error value is %d\n",reply->errorVal);

    /* Initiate recovery if its a fatal error */
    if (reply->errorVal & WMI_TARGET_FATAL_ERR) {
        /* Reset the ar6000 module in the driver */
        if (optionflag & RESTORE_FLAG) {
            printf("Executing script: %s\n", restorefile);
            system(restorefile);
        }
    }

    return A_OK;
}

static A_STATUS
app_rssi_threshold_event_rx(A_UINT8 *datap, size_t len)
{
    USER_RSSI_THOLD *evt;

    if (len < sizeof(USER_RSSI_THOLD)) {
        return A_EINVAL;
    }
    evt = (USER_RSSI_THOLD*)datap;
    RECEVENT_DEBUG_PRINTF("Application receive rssi threshold event\n");
    RECEVENT_DEBUG_PRINTF("tag is %d, rssi is %d\n", evt->tag, evt->rssi);

    return A_OK;
}

static A_STATUS
app_scan_complete_event_rx(A_UINT8 *datap, size_t len)
{
    char buf[10];
    const char *status;
    WMI_SCAN_COMPLETE_EVENT *ev;
    if (len < sizeof(WMI_SCAN_COMPLETE_EVENT)) {
        return A_EINVAL;
    }
    ev = (WMI_SCAN_COMPLETE_EVENT *)datap;
    switch (ev->status) {
    case A_OK:
        status = "OK";
        break;
    case A_EBUSY:
        status = "BUSY";
        break;
    case A_ECANCELED:
        status = "CANCEL";
        break;
    default:
        sprintf(buf, "%d", ev->status);
        status = buf;
        break;
    }
    RECEVENT_DEBUG_PRINTF("Application receive scan complete event %s\n", status);
    return A_OK;
}

static A_STATUS
app_challenge_resp_event_rx(A_UINT8 *datap, size_t len)
{
    A_UINT32 cookie;

    memcpy(&cookie, datap, len);
    RECEVENT_DEBUG_PRINTF("Application receive challenge response event: 0x%x\n", cookie);

    return A_OK;
}

static A_STATUS
app_target_debug_event_rx(A_INT8 *datap, size_t len)
{
#define BUF_SIZE    120
    A_UINT32 count;
    A_UINT32 numargs;
    A_INT32 *buffer;
    A_UINT32 length;
    A_CHAR buf[BUF_SIZE];
    long curpos;
    static int numOfRec = 0;
#ifdef ANDROID
    A_INT8 *tmp_buf = (A_INT8*)malloc(len);
#endif 
#ifdef DBGLOG_DEBUG
    RECEVENT_DEBUG_PRINTF("Application received target debug event: %d\n", len);
#endif /* DBGLOG_DEBUG */
    count = 0;

#ifdef ANDROID /*Android cannot take casting; crash at run-time*/   
    if (!tmp_buf) {
        return A_NO_MEMORY;
    }
    memcpy(tmp_buf, datap, len);
    buffer=(A_INT32*)tmp_buf;
#else
    buffer = (A_INT32 *)datap;
#endif
    
    if (optionflag & BINARY_FLAG) {
        /*
         * If saved in binary format, create a binary 
         * record and write it without decoding
         */
    
        struct dbg_binary_record rec;
    
        rec.ts = time(NULL);
        rec.length = len;
        memcpy(rec.log, buffer, len);
        fwrite((void *)&rec, sizeof(rec),1, fpout); 
        fflush(fpout);
        numOfRec += len/12; /* assume the average record size is 12 bytes */
    
        /* If exceeded the record limit, go back to beginning */
        if(dbgRecLimit && (numOfRec >= dbgRecLimit)) {
            numOfRec = 0;
            fseek(fpout, 8, SEEK_SET);
        }
    } else {
        char outputBuf[2048]; 
        length = (len >> 2);
        while (count < length) {
            numargs = DBGLOG_GET_NUMARGS(buffer[count]);
            if (dbg_formater(0, outputBuf, sizeof(outputBuf), (A_UINT32)time(NULL), &buffer[count]) > 0) {
                fprintf(fpout, "%s", outputBuf);
            }
#ifdef DEBUG
            if (debugRecEventLevel>1) {
                if (dbg_formater(1, outputBuf, sizeof(outputBuf), 0, &buffer[count]) > 0) {
                    RECEVENT_DBGLOG_PRINTF("%s", outputBuf);
                }
            }
#endif
            count += (numargs + 1);

            numOfRec++;
            if(dbgRecLimit && (numOfRec % dbgRecLimit == 0)) {
                /* Once record limit is hit, rewind to start
                * after 8 bytes from start
                */
                numOfRec = 0;
                curpos = ftell(fpout);
                truncate(dbglogoutfile, curpos);
                rewind(fpout);
                fseek(fpout, 8, SEEK_SET);
                fprintf(fpout, "\n");
            }
        }

        /* Update the last rec at the top of file */
        curpos = ftell(fpout);
        if( fgets(buf, BUF_SIZE, fpout) ) {
            buf[BUF_SIZE - 1] = 0;  /* In case string is longer from logs */
            length = strlen(buf);
            memset(buf, ' ', length-1);
            buf[length] = 0;
            fseek(fpout, curpos, SEEK_SET);
            fprintf(fpout, "%s", buf);
        }

        rewind(fpout);
        /* Update last record */
        fprintf(fpout, "%08d\n", numOfRec);
        fseek(fpout, curpos, SEEK_SET);
        fflush(fpout);
    }

#undef BUF_SIZE
#ifdef ANDROID
    free(tmp_buf);
#endif 
    return A_OK;
}
