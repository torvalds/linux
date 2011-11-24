/*
 * Copyright (c) 2006 Atheros Communications Inc.
 * All rights reserved.
 * 
 * 
// The software source and binaries included in this development package are
// licensed, not sold. You, or your company, received the package under one
// or more license agreements. The rights granted to you are specifically
// listed in these license agreement(s). All other rights remain with Atheros
// Communications, Inc., its subsidiaries, or the respective owner including
// those listed on the included copyright notices.  Distribution of any
// portion of this package must be in strict compliance with the license
// agreement(s) terms.
// </copyright>
// 
// <summary>
// 	Wifi driver for AR6002
// </summary>
//
 * 
 */
#include <sys/types.h>
#include <net/if.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <ctype.h>
#include <stdio.h>
#include <getopt.h>
#include "athtestcmdlib.h"
#ifdef ANDROID
#include <cutils/properties.h>
#endif 

//#define UNIT_TEST 1

typedef uint32_t A_UINT32;
typedef int32_t A_INT32;
typedef uint16_t A_UINT16;

static struct {
    int channel;
    int isTxStart;
    int isRxStart;
    AthDataRate rate;
    int txPwr;
    int txPktSize;
    int aifs;
    AthHtMode mode;
	A_UINT32 rxPkt;
    A_INT32  rxRssi;
    A_UINT32 rxCrcError;
    A_UINT32 rxSecError;
    int shortguard;
    int errCode;
    char errString[256];
    jmp_buf ctx;
} gCmd;

#ifdef UNIT_TEST
static int parseCmd(const char *cmdline, char *buf, char **argv, size_t argvlen)
{
    int argc = 0;
    char *token = buf;
    strcpy(buf, cmdline);

    while ( *token && isspace(*token) )
            ++token;
    while (*token)
    {
        if (argc>=argvlen)
        {
            break;           
        }
        argv[argc++] = token;
        while ( *token && !isspace(*token) )
            ++token;
        if (*token)
        {
            *token++ = '\0';
            while ( *token && isspace(*token) )
                ++token;
        }
    }
    if (argc==0 || (argc>0 && strcmp(argv[0], "athtestcmd")!=0))
    {
        argv[0] = "athtestcmd";
    }
    return argc;
}

int tcmd_exec(const char *cmdline, void (*reportCB)(void *), jmp_buf *jbuf)
{
#define MAX_ARGS 30
    char cmdbuf[2048];
    char *argv[MAX_ARGS]; /* max 30 arguments */
    int argc = parseCmd(cmdline, cmdbuf, argv, MAX_ARGS);
    int c;
    printf("%s\n", cmdline);
    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"version", 0, NULL, 'v'},
            {"interface", 1, NULL, 'i'},
            {"tx", 1, NULL, 't'},
            {"txfreq", 1, NULL, 'f'},
            {"txrate", 1, NULL, 'g'},
            {"txpwr", 1, NULL, 'h'},
            {"txantenna", 1, NULL, 'j'},
            {"txpktsz", 1, NULL, 'z'},
            {"txpattern", 1, NULL, 'e'},
            {"rx", 1, NULL, 'r'},
            {"rxfreq", 1, NULL, 'p'},
            {"rxantenna", 1, NULL, 'q'},
            {"pm", 1, NULL, 'x'},
            {"setmac", 1, NULL, 's'},
            {"ani", 0, NULL, 'a'},
            {"scrambleroff", 0, NULL, 'o'},
            {"aifsn", 1, NULL, 'u'},
            {"SetAntSwitchTable", 1, NULL, 'S'},
            {"shortguard", 0, NULL, 'G'},
            {"numpackets", 1, NULL, 'n'},
            {"setlongpreamble", 1, NULL, 'l'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "vi:t:f:g:r:p:q:x:u:ao",
                         long_options, &option_index);
        if (c == -1)
            break;
        printf("%c %s ", c, optarg);
    }
    printf("\n");
    return 0;
}
#else
extern int tcmd_exec(const char *cmdline, void (*rxcb)(void*), jmp_buf *jbuf);
#endif 
static void reportRx(void *data);

static void getIfName(char *ifname)
{   
#ifdef ANDROID 
    char ifprop[PROPERTY_VALUE_MAX];
#endif 
    char *src;
    char defIfname[IFNAMSIZ];  
    char linebuf[1024];
    FILE *f = fopen("/proc/net/wireless", "r");
    if (f) {
        while (fgets(linebuf, sizeof(linebuf)-1, f)) {
            if (strchr(linebuf, ':')) {
                char *dest = defIfname;
                char *p = linebuf;
                while (*p && isspace(*p)) ++p;
                while (*p && *p != ':')
                    *dest++ = *p++;
                *dest = '\0';
                break;
            }
        }
        fclose(f);
    }

    src = defIfname;    
#ifdef ANDROID
    if (property_get("wifi.interface", ifprop, defIfname)) {
        src = ifprop;
    }
#endif
    memcpy(ifname, src, IFNAMSIZ);
}

static int doCommand(const char *fmt, ...)
{
    int ret = 0;
    int len;
    char ifname[IFNAMSIZ];
    char cmd[1024];
    va_list ap;
    getIfName(ifname);
    len = snprintf(cmd, sizeof(cmd)-1, "athtestcmd -i %s ", ifname); 
    va_start(ap, fmt);
    len = vsnprintf(cmd + len, sizeof(cmd)-1-len, fmt, ap);
    va_end(ap);

    if (len > 0) {
        gCmd.errCode = 0;
        gCmd.errString[0] = '\0';      
        ret = tcmd_exec(cmd, &reportRx, &gCmd.ctx);
    }
    optarg = NULL;
    optind = opterr = 1;
    optopt = '?';
    return 0;
}
//
// PUBLIC FUNCTIONS
//


static void athCheck()
{
	if (gCmd.channel == 0 ) { 
		gCmd.channel = 1;
	}
	athTxStop();
    athRxPacketStop();
}

static void reportRx(void *buf)
{
    //A_UINT16 rateCnt[TCMD_MAX_RATES];
    //A_UINT16 rateCntShortGuard[TCMD_MAX_RATES];

    gCmd.rxPkt = *(A_UINT32 *)buf;
    gCmd.rxRssi = *((A_INT32 *)buf + 1);
    gCmd.rxCrcError = *((A_UINT32 *)buf + 2);
    gCmd.rxSecError = *((A_UINT32 *)buf + 3);
}

void testcmd_error(int code, const char *fmt, ...)
{
    gCmd.errCode = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(gCmd.errString, sizeof(gCmd.errString)-1, fmt, ap);
    va_end(ap);

    longjmp(gCmd.ctx, 1);
}

int athApiInit(void) 
{
    memset(&gCmd, 0, sizeof(gCmd));
    optarg = NULL;
    optind = opterr = 1;
    optopt = '?';
    return gCmd.errCode;
}

void athApiCleanup(void)
{
    athCheck();
    memset(&gCmd, 0, sizeof(gCmd));
}

void athChannelSet(int channel)
{
    gCmd.channel = channel;   
}

void athShortGuardSet(int enable)
{
    gCmd.shortguard = enable ? 1 : 0;
}

void athRateSet(AthDataRate r)
{
    gCmd.rate = r;
}

void athTxPowerSet(int txpwr)
{
    gCmd.txPwr = txpwr;
}

void athTxPacketSizeSet(int size)
{
    gCmd.txPktSize = size;
}

int athTxStart(const char *txtype)
{
    const char *shortuard = gCmd.shortguard ? "--shortguard" : "";
    char frameParams[512];
    char ht40Params[512];
    athCheck();
    if (strcmp(txtype, "frame")==0) {
        sprintf(frameParams, "--txpktsz %d --aifsn %d", gCmd.txPktSize, gCmd.aifs);
    } else {
        frameParams[0] = '\0';
    }

    if (gCmd.rate >= ATH_RATE_HT40_13_5M && gCmd.rate <= ATH_RATE_HT40_135M) {
        const char *htmode;
        if (gCmd.mode==ATH_HT20) {
            gCmd.mode = ATH_HT40Plus;
        }
        if (gCmd.channel==  1 && gCmd.mode == ATH_HT40Minus ) {
            gCmd.mode = ATH_HT40Plus;
        } else if (gCmd.channel==14 && gCmd.mode == ATH_HT40Plus ) {
            gCmd.mode = ATH_HT40Minus;
        }
        ht40Params[0] = '\0';
        switch (gCmd.mode) {
        case ATH_HT40Minus:
            sprintf(ht40Params, "--mode ht40minus");
            break;
        case ATH_HT40Plus:
            sprintf(ht40Params, "--mode ht40plus");
            break;
        case ATH_HT20:
            sprintf(ht40Params, "--mode ht20");
            break;
        default:
        case ATH_NOHT:
            break;
        }
    }

    doCommand("--tx %s --txfreq %d --txrate %d --txpwr %d --txantenna 0 %s %s %s",
              txtype, gCmd.channel, gCmd.rate, gCmd.txPwr, shortuard, frameParams, ht40Params);
    if (gCmd.errCode==0) {
        gCmd.isTxStart = 1;
    }
    return(gCmd.errCode==0) ? 0 : -1;
}

int athTxSineStart()
{
    return athTxStart("sine");
}

int athTx99Start()
{
    return athTxStart("tx99");
}

int athTxFrameStart()
{
    return athTxStart("frame");
}

int athTxStop()
{
    if (gCmd.isTxStart) {
        doCommand("--tx off");
        if (gCmd.errCode==0) {
            gCmd.isTxStart = 0;
        }
        return(gCmd.errCode==0) ? 0 : -1;
    }
    return 0;
}

int athRxPacketStart(void)
{
    athCheck();
    gCmd.rxPkt = gCmd.rxRssi = gCmd.rxCrcError = gCmd.rxSecError = 0;
    doCommand("--rx promis --rxfreq %d --rxantenna 0", gCmd.channel);
    if (gCmd.errCode==0) {
        gCmd.isRxStart = 1;
    }    
    return (gCmd.errCode==0) ? 0 : -1;
}

int athRxPacketStop(void)
{
    if (gCmd.isRxStart) {
        doCommand("--rx report --rxfreq %d --rxantenna 0", gCmd.channel);
        if (gCmd.errCode==0) {
            gCmd.isRxStart = 0;
        }
        return (gCmd.errCode==0) ? 0 : -1;
    }
    return -1;
}

int athSetLongPreamble(int enable)
{
    int ret = doCommand("--setlongpreamble %d\n", enable);
    return ret;
}

void athSetAifsNum(int slot)
{
    gCmd.aifs = slot;
}

uint32_t athRxGetErrorFrameNum(void)
{
    return (1000 - gCmd.rxPkt);
}

uint32_t athRxGetGoodFrameNum(void)
{
    return gCmd.rxPkt;
}

const char *athGetErrorString(void)
{
    return gCmd.errString;
}

void athHtModeSet(AthHtMode mode)
{
    gCmd.mode = mode;
}

#ifdef UNIT_TEST
int main()
{
    athApiInit();
    athRateSet(ATH_RATE_12M);
    athChannelSet(1);
    athTxPowerSet(20);
    athTxPacketSizeSet(1200);
    athTxFrameStart();
    athChannelSet(6);
    athRateSet(ATH_RATE_11M);
    athTxSineStart();

    athRxPacketStart();
   // athRxPacketStop();
    athApiCleanup();
}
#endif 



