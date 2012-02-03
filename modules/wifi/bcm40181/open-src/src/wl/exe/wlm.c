/*
 * WLM (Wireless LAN Manufacturing) test library.
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlm.c,v 1.21.6.3 2010/12/15 19:35:08 Exp $
 */

#if defined(WIN32)
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>

#include <bcmcdc.h>      // cdc_ioctl_t used in wlu_remote.h
#if defined(WIN32)
#include <bcmstdlib.h>
#endif
#include <bcmendian.h>
#include <bcmutils.h>    // ARRAYSIZE, bcmerrorstr()
#include <bcmsrom_fmt.h> // SROM4_WORDS
#include <bcmsrom_tbl.h> // pavars_t
#include <wlioctl.h>
#if defined(WIN32)
#include <epictrl.h>     // ADAPTER
#endif
#include <proto/ethernet.h>	 // ETHER_ADDR_LEN

#include <sys/socket.h>
#include <proto/bcmip.h> // ipv4_addr
#include <arpa/inet.h>	// struct sockaddr_in
#include <string.h>
#include <signal.h>

#include "wlu_remote.h"  // wl remote type defines (ex: NO_REMOTE)
#include "wlu_pipe.h"    // rwl_open_pipe()
#include "wlu.h"         // wl_ether_atoe()
#include "wlm.h"

/* IOCTL swapping mode for Big Endian host with Little Endian dongle.  Default to off */
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
#define htodenum(i) i
#define dtohenum(i) i

#if defined(WIN32)
static HANDLE irh;
int adapter;
#else
static void * irh;
#define HANDLE void *
#endif

#define MAX_INTERFACE_NAME_LENGTH     128
static char interfaceName[MAX_INTERFACE_NAME_LENGTH + 1] = {0};
static WLM_BAND curBand = WLM_BAND_AUTO;

extern int wl_os_type_get_rwl(void);
extern void wl_os_type_set_rwl(int os_type);
extern int wl_ir_init_rwl(HANDLE *irh);
extern int wl_ir_init_adapter_rwl(HANDLE *irh, int adapter);
extern void wl_close_rwl(int remote_type, HANDLE irh);
extern int rwl_init_socket(void);

extern int wlu_get(void *wl, int cmd, void *buf, int len);
extern int wlu_set(void *wl, int cmd, void *buf, int len);

extern int wlu_iovar_get(void *wl, const char *iovar, void *outbuf, int len);
extern int wlu_iovar_set(void *wl, const char *iovar, void *param, int paramlen);
extern int wlu_iovar_getint(void *wl, const char *iovar, int *pval);
extern int wlu_iovar_setint(void *wl, const char *iovar, int val);

extern int wlu_var_getbuf(void *wl, const char *iovar, void *param, int param_len, void **bufptr);
extern int wlu_var_setbuf(void *wl, const char *iovar, void *param, int param_len);
extern int wlu_var_getbuf_med(void *wl, const char *iovar, void *param, int parmlen, void **bufptr);
extern int wlu_var_setbuf_med(void *wl, const char *iovar, void *param, int param_len);
extern int wlu_var_getbuf_sm(void *wl, const char *iovar, void *param, int parmlen, void **bufptr);
extern int wlu_var_setbuf_sm(void *wl, const char *iovar, void *param, int param_len);

extern int wl_seq_batch_in_client(bool enable);
extern int wl_seq_start(void *wl, cmd_t *cmd, char **argv);
extern int wl_seq_stop(void *wl, cmd_t *cmd, char **argv);

static int wlmPhyTypeGet(void);

static const char *
wlmLastError(void)
{
	static const char *bcmerrorstrtable[] = BCMERRSTRINGTABLE;
	static char errorString[256];
	int bcmerror;

	if (wlu_iovar_getint(irh, "bcmerror", &bcmerror))
		return "Failed to retrieve error";

	if (bcmerror > 0 || bcmerror < BCME_LAST)
		return "Undefined error";

	sprintf(errorString, "%s (%d)", bcmerrorstrtable[-bcmerror], bcmerror);

	return errorString;
}

int wlmWLMVersionGet(const char **buffer)
{
	*buffer = WLM_VERSION_STR;
	return TRUE;
}

int wlmApiInit(void)
{
	curBand = WLM_BAND_AUTO;
	return TRUE;
}

int wlmApiCleanup(void)
{
	wl_close_rwl(rwl_get_remote_type(), irh);
	irh = 0;
	return TRUE;
}

int wlmSelectInterface(WLM_DUT_INTERFACE ifType, char *ifName,
	WLM_DUT_SERVER_PORT dutServerPort, WLM_DUT_OS dutOs)
{
	/* close previous handle */
	if (irh != NULL) {
		wlmApiCleanup();
	}

	switch (ifType) {
		case WLM_DUT_LOCAL:
			rwl_set_remote_type(NO_REMOTE);
			break;
		case WLM_DUT_SERIAL:
			rwl_set_remote_type(REMOTE_SERIAL);
			break;
		case WLM_DUT_SOCKET:
			rwl_set_remote_type(REMOTE_SOCKET);
			break;
		case WLM_DUT_WIFI:
			rwl_set_remote_type(REMOTE_WIFI);
			break;
		case WLM_DUT_DONGLE:
			rwl_set_remote_type(REMOTE_DONGLE);
			break;
		default:
			/* ERROR! Unknown interface! */
			return FALSE;
	}

	if (ifName) {
		strncpy(interfaceName, ifName, MAX_INTERFACE_NAME_LENGTH);
		interfaceName[MAX_INTERFACE_NAME_LENGTH] = 0;
	}

	switch (dutOs) {
		case WLM_DUT_OS_LINUX:
			wl_os_type_set_rwl(LINUX_OS);
			break;
		case WLM_DUT_OS_WIN32:
			wl_os_type_set_rwl(WIN32_OS);
			break;
		default:
			/* ERROR! Unknown OS! */
			return FALSE;
	}

	switch (rwl_get_remote_type()) {
		struct ipv4_addr temp;
		case REMOTE_SOCKET:
			if (!wl_atoip(interfaceName, &temp)) {
				printf("wlmSelectInterface: IP address invalid\n");
				return FALSE;
			}
			rwl_set_server_ip(interfaceName);
			rwl_set_server_port(dutServerPort);
			rwl_init_socket();
			break;
		case REMOTE_SERIAL:
			rwl_set_serial_port_name(interfaceName); /* x (port number) or /dev/ttySx */
			if ((irh = rwl_open_pipe(rwl_get_remote_type(),
				rwl_get_serial_port_name(), 0, 0)) == NULL) {
				printf("wlmSelectInterface: rwl_open_pipe failed\n");
				return FALSE;
			}
			break;
		case REMOTE_DONGLE:
			rwl_set_serial_port_name(interfaceName); /* COMx or /dev/ttySx */
			if ((irh = rwl_open_pipe(rwl_get_remote_type(), "\0", 0, 0)) == NULL) {
				printf("wlmSelectInterface: rwl_open_pipe failed\n");
				return FALSE;
			}
			break;
		case REMOTE_WIFI:
			if (!wl_ether_atoe(interfaceName,
				(struct ether_addr *)rwl_get_wifi_mac())) {
				printf("wlmSelectInterface: ethernet MAC address invalid\n");
				return FALSE;
			}
			/* intentionally no break here to pass through to NO_REMOTE case */
		case NO_REMOTE:
#if defined(WIN32)
			adapter = atoi(interfaceName);
			if (adapter == 0)
				adapter = -1;

			if (wl_ir_init_adapter_rwl(&irh, adapter) != 0) {
				printf("wlmSelectInterface: Adapter %d init failed\n", adapter);
				return FALSE;
			}
#else
			if (wl_ir_init_rwl(&irh) != 0) {
				printf("wlmSelectInterface: initialize failed\n");
				return FALSE;
			}
#endif
			break;
		default:
			/* ERROR! Invalid interface!
			 * NOTE: API should not allow code to come here.
			 */
			return FALSE;
	}

	return TRUE;
}

int wlmVersionGet(char *buf, int len)
{
	if (buf == 0) {
		printf("wlmVersionGet: buffer invalid\n");
		return FALSE;
	}

	memset(buf, 0, sizeof(buf));

	/* query for 'ver' to get version info */
	if (wlu_iovar_get(irh, "ver", buf, (len < WLC_IOCTL_SMLEN) ? len : WLC_IOCTL_SMLEN)) {
		printf("wlmVersionGet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmEnableAdapterUp(int enable)
{
	/*  Enable/disable adapter  */
	if (enable)
	{
		if (wlu_set(irh, WLC_UP, NULL, 0)) {
			printf("wlmEnableAdapterUp: %s\n", wlmLastError());
			return FALSE;
		}
	}
	else {
		if (wlu_set(irh, WLC_DOWN, NULL, 0)) {
			printf("wlmEnableAdapterUp: %s\n", wlmLastError());
			return FALSE;
		}
	}

	return TRUE;
}

int wlmIsAdapterUp(int *up)
{
	/*  Get 'isup' - check if adapter is up */
	up = dtoh32(up);
	if (wlu_get(irh, WLC_GET_UP, up, sizeof(int))) {
		printf("wlmIsAdapterUp: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmMinPowerConsumption(int enable)
{
	if (wlu_iovar_setint(irh, "mpc", enable)) {
		printf("wlmMinPowerConsumption: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmMimoPreambleGet(int* type)
{
	if (wlu_iovar_getint(irh, "mimo_preamble", type)) {
		printf("wlmMimoPreambleGet(): %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmMimoPreambleSet(int type)
{
	if (wlu_iovar_setint(irh, "mimo_preamble", type)) {
		printf("wlmMimoPreambleSet(): %s\n", wlmLastError());
		return FALSE;
	}
	return  TRUE;
}

int wlmChannelSet(int channel)
{

	/* Check band lock first before set  channel */
	if ((channel <= 14) && (curBand != WLM_BAND_2G)) {
		curBand = WLM_BAND_2G;
	} else if ((channel > 14) && (curBand != WLM_BAND_5G)) {
		curBand = WLM_BAND_5G;
	}

	/* Set 'channel' */
	channel = htod32(channel);
	if (wlu_set(irh, WLC_SET_CHANNEL, &channel, sizeof(channel))) {
		printf("wlmChannelSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRateSet(WLM_RATE rate)
{
	char aname[] = "a_rate";
	char bgname[] = "bg_rate";
	char *name;

	switch (curBand) {
	        case WLM_BAND_AUTO :
			printf("wlmRateSet: must set channel or band lock first \n");
			return FALSE;
	        case WLM_BAND_DUAL :
			printf("wlmRateSet: must set channel or band lock first\n");
			return FALSE;
		case WLM_BAND_5G :
			name = (char *)aname;
			break;
		case WLM_BAND_2G :
			name = (char *)bgname;
			break;
		default :
			return FALSE;
	}

	rate = htod32(rate);
	if (wlu_iovar_setint(irh, name, rate)) {
		printf("wlmRateSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmLegacyRateSet(WLM_RATE rate)
{
	uint32 nrate = 0;
	uint stf = NRATE_STF_SISO;

	nrate |= rate & NRATE_RATE_MASK;
	nrate |= (stf << NRATE_STF_SHIFT) & NRATE_STF_MASK;

	if (wlu_iovar_setint(irh, "nrate", (int)nrate)) {
		printf("wlmMcsRateSet: %s\n", wlmLastError());
		return FALSE;
	}

	return  TRUE;
}

int wlmMcsRateSet(WLM_MCS_RATE mcs_rate, WLM_STF_MODE stf_mode)
{
	uint32 nrate = 0;
	uint stf;

	nrate |= mcs_rate & NRATE_RATE_MASK;
	nrate |= NRATE_MCS_INUSE;

	if (!stf_mode) {
		stf = 0;
		if ((nrate & NRATE_RATE_MASK) <= HIGHEST_SINGLE_STREAM_MCS ||
		    (nrate & NRATE_RATE_MASK) == 32)
			stf = NRATE_STF_SISO;	/* SISO */
		else
			stf = NRATE_STF_SDM;	/* SDM */
	} else
			stf = stf_mode;

	nrate |= (stf << NRATE_STF_SHIFT) & NRATE_STF_MASK;


	if (wlu_iovar_setint(irh, "nrate", (int)nrate)) {
		printf("wlmMcsRateSet: %s\n", wlmLastError());
		return FALSE;
	}
	return  TRUE;
}

int wlmPreambleSet(WLM_PREAMBLE preamble)
{
	preamble = htod32(preamble);

	if (wlu_set(irh, WLC_SET_PLCPHDR, &preamble, sizeof(preamble))) {
		printf("wlmPreambleSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmBandSet(WLM_BAND band)
{
	band = htod32(band);

	if (wlu_set(irh, WLC_SET_BAND, (void *)&band, sizeof(band))) {
		printf("wlmBandSet: %s\n", wlmLastError());
		return FALSE;
	}

	curBand = band;

	return TRUE;
}

int wlmGetBandList(WLM_BAND * bands)
{
	unsigned int list[3];
	unsigned int i;

	if (wlu_get(irh, WLC_GET_BANDLIST, list, sizeof(list))) {
		printf("wlmGetBandList: %s\n", wlmLastError());
		return FALSE;
	}

	list[0] = dtoh32(list[0]);
	list[1] = dtoh32(list[1]);
	list[2] = dtoh32(list[2]);

	/* list[0] is count, followed by 'count' bands */

	if (list[0] > 2)
		list[0] = 2;

	for (i = 1, *bands = (WLM_BAND)0; i <= list[0]; i++)
		*bands |= list[i];

	return TRUE;
}

int wlmGmodeSet(WLM_GMODE gmode)
{
	/*  Set 'gmode' - select mode in 2.4G band */
	gmode = htod32(gmode);

	if (wlu_set(irh, WLC_SET_GMODE, (void *)&gmode, sizeof(gmode))) {
		printf("wlmGmodeSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRxAntSet(int antenna)
{
	/*  Set 'antdiv' - select receive antenna */
	antenna = htod32(antenna);

	if (wlu_set(irh, WLC_SET_ANTDIV, &antenna, sizeof(antenna))) {
		printf("wlmRxAntSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmTxAntSet(int antenna)
{
	/*  Set 'txant' - select transmit antenna */
	antenna = htod32(antenna);

	if (wlu_set(irh, WLC_SET_TXANT, &antenna, sizeof(antenna))) {
		printf("wlmTxAntSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmEstimatedPowerGet(int *estPower, int chain)
{
	tx_power_t power;
	int mimo;

	memset(&power, 0, sizeof(power));

	if (wlu_get(irh, WLC_CURRENT_PWR, &power, sizeof(power)) < 0) {
		printf("wlmEstimatedPowerGet: %s\n", wlmLastError());
		return FALSE;
	}
	power.flags = dtoh32(power.flags);
	power.chanspec = dtohchanspec(power.chanspec);
	mimo = (power.flags & WL_TX_POWER_F_MIMO);

	/* value returned is in units of quarter dBm, need to multiply by 250 to get milli-dBm */
	if (mimo) {
		*estPower = power.est_Pout[chain] * 250;
	} else {
		*estPower = power.est_Pout[0] * 250;
	}

	if (!mimo && CHSPEC_IS2G(power.chanspec)) {
		*estPower = power.est_Pout_cck * 250;
	}

	return TRUE;
}

int wlmTxPowerGet(int *power)
{
	int val;

	if ((wlu_iovar_getint(irh, "qtxpower", &val)) < 0) {
		printf("wlmTxPowerGet: %s\n", wlmLastError());
		return FALSE;
	}

	val &= ~WL_TXPWR_OVERRIDE;

	/* value returned is in units of quarter dBm, need to multiply by 250 to get milli-dBm */
	*power = val * 250;
	return TRUE;
}

int wlmTxPowerSet(int powerValue)
{
	int newValue = 0;

	if (powerValue == -1) {
		newValue = 127;		/* Max val of 127 qdbm */
	} else {
		/* expected to be in units of quarter dBm */
		newValue = powerValue / 250;
		newValue |= WL_TXPWR_OVERRIDE;
	}

	if (wlu_iovar_setint(irh, "qtxpower", newValue)) {
		printf("wlmTxPowerSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

static int wlmPhyTypeGet(void)
{

	int phytype = PHY_TYPE_NULL;

	if (wlu_get(irh, WLC_GET_PHYTYPE, &phytype, sizeof(int)) < 0) {
	        printf("wlmPhyTypeGet: %s\n", wlmLastError());
		return FALSE;
	}

	return phytype;
}

int wlmPaParametersGet(WLM_BANDRANGE bandrange,
	unsigned int *a1, unsigned int *b0, unsigned int *b1)
{
	uint16 inpa[WL_PHY_PAVARS_LEN];
	void *ptr = NULL;
	uint16 *outpa;
	int i = 0;
	int phytype = PHY_TYPE_NULL;

	*a1 = 0;
	*b0 = 0;
	*b1 = 0;

	/* Do not rely on user to have knowledge of phytype */
	phytype = wlmPhyTypeGet();
	if (phytype != PHY_TYPE_NULL) {
		inpa[i++] = phytype;
		inpa[i++] = bandrange;
		inpa[i++] = 0;  /* Fix me: default with chain 0 for all SISO system */
	} else {
		printf("wlmPaParametersGet: unknow Phy type\n");
		return FALSE;
	}

	if (wlu_var_getbuf(irh, "pavars", inpa, WL_PHY_PAVARS_LEN * sizeof(uint16), &ptr)) {
		printf("wlmPaParametersGet: %s\n", wlmLastError());
		return FALSE;
	}

	outpa = (uint16 *)ptr;
	*b0 = outpa[i++];
	*b1 = outpa[i++];
	*a1 = outpa[i++];

	return TRUE;
}

int wlmPaParametersSet(WLM_BANDRANGE bandrange,
	unsigned int a1, unsigned int b0, unsigned int b1)
{
	uint16 inpa[WL_PHY_PAVARS_LEN];
	int i = 0;
	int phytype = PHY_TYPE_NULL;

	/* Do not rely on user to have knowledge of phy type */
	phytype = wlmPhyTypeGet();
	if (phytype != PHY_TYPE_NULL) {
		inpa[i++] = phytype;
		inpa[i++] = bandrange;
		inpa[i++] = 0;  /* Fix me: default with chain 0 for all SISO system */
	} else {
	        printf("wlmPaParametersSet: unknow Phy type\n");
		return FALSE;
	}

	inpa[i++] = b0;
	inpa[i++] = b1;
	inpa[i++] = a1;

	if (wlu_var_setbuf(irh, "pavars", inpa, WL_PHY_PAVARS_LEN * sizeof(uint16))) {
		printf("wlmPaParametersSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}


int wlmMIMOPaParametersGet(WLM_BANDRANGE bandrange, int chain,
	unsigned int *a1, unsigned int *b0, unsigned int *b1)
{
	uint16 inpa[WL_PHY_PAVARS_LEN];
	void *ptr = NULL;
	uint16 *outpa;
	int i = 0;
	int phytype = PHY_TYPE_NULL;

	/* Do not rely on user to have knowledge of phytype */
	phytype = wlmPhyTypeGet();
	if (phytype != PHY_TYPE_NULL) {
		inpa[i++] = phytype;
		inpa[i++] = bandrange;
		inpa[i++] = chain;
	} else {
		printf("wlmMIMOPaParametersGet: unknow Phy type\n");
		return FALSE;
	}

	if (wlu_var_getbuf(irh, "pavars", inpa, WL_PHY_PAVARS_LEN * sizeof(uint16), &ptr)) {
		printf("wlmMIMOPaParametersGet: %s\n", wlmLastError());
		return FALSE;
	}

	outpa = (uint16 *)ptr;
	*b0 = outpa[i++];
	*b1 = outpa[i++];
	*a1 = outpa[i++];

	return TRUE;
}

int wlmMIMOPaParametersSet(WLM_BANDRANGE bandrange, int chain,
	unsigned int a1, unsigned int b0, unsigned int b1)
{
	uint16 inpa[WL_PHY_PAVARS_LEN];
	int i = 0;
	int phytype = PHY_TYPE_NULL;

	/* Do not rely on user to have knowledge of phy type */
	phytype = wlmPhyTypeGet();
	if (phytype != PHY_TYPE_NULL) {
	        inpa[i++] = phytype;
		inpa[i++] = bandrange;
		inpa[i++] = chain;
	} else {
	        printf("wlmMIMOPaParametersSet: unknow Phy type\n");
		return FALSE;
	}

	inpa[i++] = b0;
	inpa[i++] = b1;
	inpa[i++] = a1;

	if (wlu_var_setbuf(irh, "pavars", inpa, WL_PHY_PAVARS_LEN * sizeof(uint16))) {
		printf("wlmMIMOPaParametersSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmMacAddrGet(char *macAddr, int length)
{
	struct ether_addr ea = {{0, 0, 0, 0, 0, 0}};

	/* query for 'cur_etheraddr' to get MAC address */
	if (wlu_iovar_get(irh, "cur_etheraddr", &ea, ETHER_ADDR_LEN) < 0) {
		printf("wlmMacAddrGet: %s\n", wlmLastError());
		return FALSE;
	}

	strncpy(macAddr, wl_ether_etoa(&ea), length);

	return TRUE;
}

int wlmMacAddrSet(const char* macAddr)
{
	struct ether_addr ea;

	if (!wl_ether_atoe(macAddr, &ea)) {
		printf("wlmMacAddrSet: MAC address invalid: %s\n", macAddr);
		return FALSE;
	}

	/*  Set 'cur_etheraddr' to set MAC address */
	if (wlu_iovar_set(irh, "cur_etheraddr", (void *)&ea, ETHER_ADDR_LEN)) {
		printf("wlmMacAddrSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmEnableCarrierTone(int enable, int channel)
{
	int val = channel;

	if (!enable) {
		val = 0;
	}
	else {
		wlmEnableAdapterUp(1);
		if (wlu_set(irh, WLC_OUT, NULL, 0) < 0) {
		printf("wlmEnableCarrierTone: %s\n", wlmLastError());
		return FALSE;
		}
	}
	val = htod32(val);
	if (wlu_set(irh, WLC_FREQ_ACCURACY, &val, sizeof(int)) < 0) {
		printf("wlmEnableCarrierTone: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmEnableEVMTest(int enable, WLM_RATE rate, int channel)
{
	int val[3] = {0};
	val[1] = WLM_RATE_1M; /* default value */
	if (enable) {
		val[0] = htod32(channel);
		val[1] = htod32(rate);
		wlmEnableAdapterUp(1);
		if (wlu_set(irh, WLC_OUT, NULL, 0) < 0) {
			printf("wlmEnableEVMTest: %s\n", wlmLastError());
			return FALSE;
		}
	}
	if (wlu_set(irh, WLC_EVM, val, sizeof(val)) < 0) {
		printf("wlmEnableEVMTest: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmTxPacketStart(unsigned int interPacketDelay,
	unsigned int numPackets, unsigned int packetLength,
	const char* destMac, int withAck, int syncMode)
{
	wl_pkteng_t pkteng;

	if (!wl_ether_atoe(destMac, (struct ether_addr *)&pkteng.dest)) {
		printf("wlmTxPacketStart: destMac invalid\n");
		return FALSE;
	}

	pkteng.flags = withAck ? WL_PKTENG_PER_TX_WITH_ACK_START : WL_PKTENG_PER_TX_START;

	if (syncMode)
		pkteng.flags |= WL_PKTENG_SYNCHRONOUS;
	else
		pkteng.flags &= ~WL_PKTENG_SYNCHRONOUS;

	pkteng.delay = interPacketDelay;
	pkteng.length = packetLength;
	pkteng.nframes = numPackets;

	pkteng.seqno = 0;			/* not used */
	pkteng.src = ether_null;	/* implies current ether addr */

	if (wlu_var_setbuf(irh, "pkteng", &pkteng, sizeof(pkteng))) {
		printf("wlmTxPacketStart: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmTxPacketStop(void)
{
	wl_pkteng_t pkteng;

	memset(&pkteng, 0, sizeof(pkteng));
	pkteng.flags = WL_PKTENG_PER_TX_STOP;

	if (wlu_var_setbuf(irh, "pkteng", &pkteng, sizeof(pkteng)) < 0) {
		printf("wlmTxPacketStop: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRxPacketStart(const char* srcMac, int withAck,
	int syncMode, unsigned int numPackets, unsigned int timeout)
{
	wl_pkteng_t pkteng;

	if (!wl_ether_atoe(srcMac, (struct ether_addr *)&pkteng.dest)) {
		printf("wlmRxPacketStart: srcMac invalid\n");
		return FALSE;
	}

	pkteng.flags = withAck ? WL_PKTENG_PER_RX_WITH_ACK_START : WL_PKTENG_PER_RX_START;

	if (syncMode) {
		pkteng.flags |= WL_PKTENG_SYNCHRONOUS;
		pkteng.nframes = numPackets;
		pkteng.delay = timeout;
	}
	else
		pkteng.flags &= ~WL_PKTENG_SYNCHRONOUS;

	if (wlu_var_setbuf(irh, "pkteng", &pkteng, sizeof(pkteng))) {
		printf("wlmRxPacketStart: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRxPacketStop(void)
{
	wl_pkteng_t pkteng;

	memset(&pkteng, 0, sizeof(pkteng));
	pkteng.flags = WL_PKTENG_PER_RX_STOP;

	if (wlu_var_setbuf(irh, "pkteng", &pkteng, sizeof(pkteng)) < 0) {
		printf("wlmRxPacketStop: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmTxGetAckedPackets(unsigned int *count)
{
	wl_cnt_t *cnt;

	if (wlu_var_getbuf(irh, "counters", NULL, 0, (void **)&cnt)) {
		printf("wlmTxGetAckedPackets: %s\n", wlmLastError());
		return FALSE;
	}

	*count = dtoh32(cnt->rxackucast);

	return TRUE;
}

int wlmRxGetReceivedPackets(unsigned int *count)
{
	wl_cnt_t *cnt;

	if (wlu_var_getbuf_med(irh, "counters", NULL, 0, (void **)&cnt)) {
		printf("wlmRxGetReceivedPackets: %s\n", wlmLastError());
		return FALSE;
	}

	cnt->version = dtoh16(cnt->version);
	cnt->length = dtoh16(cnt->version);

	/* current wl_cnt_t version is 7 */
	if (cnt->version == WL_CNT_T_VERSION) {
		*count = dtoh32(cnt->pktengrxducast);
	} else {
		*count = dtoh32(cnt->rxdfrmucastmbss);
	}

	return TRUE;
}

int wlmRssiGet(int *rssi)
{
	wl_pkteng_stats_t *cnt;

	if (wlu_var_getbuf(irh, "pkteng_stats", NULL, 0, (void **)&cnt)) {
		printf("wlmRssiGet: %s\n", wlmLastError());
		return FALSE;
	}

	*rssi = dtoh32(cnt->rssi);

	return TRUE;
}



int wlmSequenceStart(int clientBatching)
{
	if (wl_seq_batch_in_client((bool)clientBatching)) {
		printf("wlmSequenceStart: %s\n", wlmLastError());
		return FALSE;
	}

	if (wl_seq_start(irh, 0, 0)) {
		printf("wlmSequenceStart: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmSequenceStop(void)
{
	if (wl_seq_stop(irh, 0, 0)) {
		printf("wlmSequenceStop: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmSequenceDelay(int msec)
{
	if (wlu_iovar_setint(irh, "seq_delay", msec)) {
		printf("wlmSequenceDelay: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmSequenceErrorIndex(int *index)
{
	if (wlu_iovar_getint(irh, "seq_error_index", index)) {
		printf("wlmSequenceErrorIndex: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmDeviceImageWrite(const char* byteStream, int length, WLM_IMAGE_TYPE imageType)
{
	srom_rw_t *srt;
	char buffer[WLC_IOCTL_MAXLEN] = {0};

	char *bufp;
	char *cisp, *cisdata;
	cis_rw_t cish;

	if (byteStream == NULL) {
		printf("wlmDeviceImageWrite: Buffer is invalid!\n");
		return FALSE;
	}
	if (length > SROM_MAX+1) {
	    printf("wlmDeviceImageWrite: Data length should be less than %d bytes\n", SROM_MAX);
	    return FALSE;
	}

	switch (imageType) {
	case WLM_TYPE_SROM:
		srt = (srom_rw_t *)buffer;
		memcpy(srt->buf, byteStream, length);

		if (length == SROM4_WORDS * 2) {
			if ((srt->buf[SROM4_SIGN] != SROM4_SIGNATURE) &&
			    (srt->buf[SROM8_SIGN] != SROM4_SIGNATURE)) {
				printf("wlmDeviceImageWrite: Data lacks a REV4 signature!\n");
			    return FALSE;
			}
		} else if ((length != SROM_WORDS * 2) && (length != SROM_MAX)) {
		    printf("wlmDeviceImageWrite: Data length is invalid!\n");
		    return FALSE;
		}

		srt->nbytes = length;
		if (wlu_set(irh, WLC_SET_SROM, buffer, length + 8)) {
		    printf("wlmDeviceImageWrite: %s\n", wlmLastError());
		    return FALSE;
		}


		break;
	case WLM_TYPE_OTP:
		bufp = buffer;
		strcpy(bufp, "ciswrite");
		bufp += strlen("ciswrite") + 1;
		cisp = bufp;
		cisdata = cisp + sizeof(cish);
		cish.source = htod32(0);
		memcpy(cisdata, byteStream, length);

		cish.byteoff = htod32(0);
		cish.nbytes = htod32(length);
		memcpy(cisp, (char*)&cish, sizeof(cish));

		if (wl_set(irh, WLC_SET_VAR, buffer, (cisp - buffer) + sizeof(cish) + length) < 0) {
		    printf("wlmDeviceImageWrite: %s\n", wlmLastError());
		    return FALSE;
		 }
		break;
	case WLM_TYPE_AUTO:
		if (!wlmDeviceImageWrite(byteStream, length, WLM_TYPE_SROM) &&
			!wlmDeviceImageWrite(byteStream, length, WLM_TYPE_OTP)) {
		    printf("wlmDeviceImageWrite: %s\n", wlmLastError());
		    return FALSE;
	    }
	    break;
	default:
		printf("wlmDeviceImageWrite: Invalid image type!\n");
		return FALSE;
	}
	return TRUE;
}

int wlmDeviceImageRead(char* byteStream, unsigned int len, WLM_IMAGE_TYPE imageType)
{
	srom_rw_t *srt;
	cis_rw_t *cish;
	char buf[WLC_IOCTL_MAXLEN] = {0};
	unsigned int numRead = 0;

	if (byteStream == NULL) {
		printf("wlmDeviceImageRead: Buffer is invalid!\n");
		return FALSE;
	}

	if (len > SROM_MAX) {
	    printf("wlmDeviceImageRead: byteStream should be less than %d bytes!\n", SROM_MAX);
	    return FALSE;
	}

	if (len & 1) {
			printf("wlmDeviceImageRead: Invalid byte count %d, must be even\n", len);
			return FALSE;
	}

	switch (imageType) {
	case WLM_TYPE_SROM:
		if (len < 2*SROM4_WORDS) {
			printf("wlmDeviceImageRead: Buffer not large enough!\n");
			return FALSE;
		}

		srt = (srom_rw_t *)buf;
		srt->byteoff = 0;
		srt->nbytes = htod32(2 * SROM4_WORDS);
		/* strlen("cisdump ") = 9 */
		if (wlu_get(irh, WLC_GET_SROM, buf, 9 + (len < SROM_MAX ? len : SROM_MAX)) < 0) {
			printf("wlmDeviceImageRead: %s\n", wlmLastError());
			return FALSE;
		}
		memcpy(byteStream, buf + 8, srt->nbytes);
		numRead = srt->nbytes;
		break;
	case WLM_TYPE_OTP:
		strcpy(buf, "cisdump");
		/* strlen("cisdump ") = 9 */
		if (wl_get(irh, WLC_GET_VAR, buf, 9  + (len < SROM_MAX ? len : SROM_MAX)) < 0) {
		    printf("wlmDeviceImageRead: %s\n", wlmLastError());
		    return FALSE;
		}

		cish = (cis_rw_t *)buf;
		cish->source = dtoh32(cish->source);
		cish->byteoff = dtoh32(cish->byteoff);
		cish->nbytes = dtoh32(cish->nbytes);

		if (len < cish->nbytes) {
			printf("wlmDeviceImageRead: Buffer not large enough!\n");
			return FALSE;
		}
		memcpy(byteStream, buf + sizeof(cis_rw_t), cish->nbytes);
		numRead = cish->nbytes;
		break;
	case WLM_TYPE_AUTO:
	  numRead = wlmDeviceImageRead(byteStream, len, WLM_TYPE_SROM);
		if (!numRead) {
			numRead = wlmDeviceImageRead(byteStream, len, WLM_TYPE_OTP);
		    printf("wlmDeviceImageRead: %s\n", wlmLastError());
		    return FALSE;
	    }
	    break;
	default:
		printf("wlmDeviceImageRead: Invalid image type!\n");
		return FALSE;
	}
	return numRead;
}

int wlmSecuritySet(WLM_AUTH_TYPE authType, WLM_AUTH_MODE authMode,
	WLM_ENCRYPTION encryption, const char *key)
{
	int length = 0;
	int wpa_auth;
	int sup_wpa;
	int primary_key = 0;
	wl_wsec_key_t wepKey[4];
	wsec_pmk_t psk;
	int wsec;

	if (encryption != WLM_ENCRYPT_NONE && key == 0) {
		printf("wlmSecuritySet: invalid key\n");
		return FALSE;
	}

	if (key) {
		length = strlen(key);
	}

	switch (encryption) {

	case WLM_ENCRYPT_NONE:
		wpa_auth = WPA_AUTH_DISABLED;
		sup_wpa = 0;
		break;

	case WLM_ENCRYPT_WEP: {
		int i;
		int len = length / 4;

		wpa_auth = WPA_AUTH_DISABLED;
		sup_wpa = 0;

		if (!(length == 40 || length == 104 || length == 128 || length == 256)) {
			printf("wlmSecuritySet: invalid WEP key length %d"
			"       - expect 40, 104, 128, or 256"
			" (i.e. 10, 26, 32, or 64 for each of 4 keys)\n", length);
			return FALSE;
		}

		/* convert hex key string to 4 binary keys */
		for (i = 0; i < 4; i++) {
			wl_wsec_key_t *k = &wepKey[i];
			const char *data = &key[i * len];
			unsigned int j;

			memset(k, 0, sizeof(*k));
			k->index = i;
			k->len = len / 2;

			for (j = 0; j < k->len; j++) {
				char hex[] = "XX";
				char *end = NULL;
				strncpy(hex, &data[j * 2], 2);
				k->data[j] = (char)strtoul(hex, &end, 16);
				if (*end != 0) {
					printf("wlmSecuritySet: invalid WEP key"
					"       - expect hex values\n");
					return FALSE;
				}
			}

			switch (k->len) {
			case 5:
				k->algo = CRYPTO_ALGO_WEP1;
				break;
			case 13:
				k->algo = CRYPTO_ALGO_WEP128;
				break;
			case 16:
				k->algo = CRYPTO_ALGO_AES_CCM;
				break;
			case 32:
				k->algo = CRYPTO_ALGO_TKIP;
				break;
			default:
				/* invalid */
				return FALSE;
			}

			k->flags |= WL_PRIMARY_KEY;
		}

		break;
	}

	case WLM_ENCRYPT_TKIP:
	case WLM_ENCRYPT_AES: {

		if (authMode != WLM_WPA_AUTH_PSK && authMode != WLM_WPA2_AUTH_PSK) {
			printf("wlmSecuritySet: authentication mode must be WPA PSK or WPA2 PSK\n");
			return FALSE;
		}

		wpa_auth = authMode;
		sup_wpa = 1;

		if (length < WSEC_MIN_PSK_LEN || length > WSEC_MAX_PSK_LEN) {
			printf("wlmSecuritySet: passphrase must be between %d and %d characters\n",
			WSEC_MIN_PSK_LEN, WSEC_MAX_PSK_LEN);
			return FALSE;
		}

		psk.key_len = length;
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, length);

		break;
	}

	case WLM_ENCRYPT_WSEC:
	case WLM_ENCRYPT_FIPS:
	default:
		printf("wlmSecuritySet: encryption not supported\n");
		return FALSE;
	}

	if (wlu_iovar_setint(irh, "auth", authType)) {
		printf("wlmSecuritySet: %s\n", wlmLastError());
		return FALSE;
	}

	if (wlu_iovar_setint(irh, "wpa_auth", wpa_auth)) {
		printf("wlmSecuritySet: %s\n", wlmLastError());
		return FALSE;
	}

	if (wlu_iovar_setint(irh, "sup_wpa", sup_wpa)) {
		printf("wlmSecuritySet: %s\n", wlmLastError());
		return FALSE;
	}

	if (encryption == WLM_ENCRYPT_WEP) {
		int i;
		for (i = 0; i < 4; i++) {
			wl_wsec_key_t *k = &wepKey[i];
			k->index = htod32(k->index);
			k->len = htod32(k->len);
			k->algo = htod32(k->algo);
			k->flags = htod32(k->flags);

			if (wlu_set(irh, WLC_SET_KEY, k, sizeof(*k))) {
				printf("wlmSecuritySet: %s\n", wlmLastError());
				return FALSE;
			}
		}

		primary_key = htod32(primary_key);
		if (wlu_set(irh, WLC_SET_KEY_PRIMARY, &primary_key, sizeof(primary_key)) < 0) {
			printf("wlmSecuritySet: %s\n", wlmLastError());
			return FALSE;
		}
	}
	else if (encryption == WLM_ENCRYPT_TKIP || encryption == WLM_ENCRYPT_AES) {
		psk.key_len = htod16(psk.key_len);
		psk.flags = htod16(psk.flags);

		if (wlu_set(irh, WLC_SET_WSEC_PMK, &psk, sizeof(psk))) {
			printf("wlmSecuritySet: %s\n", wlmLastError());
			return FALSE;
		}
	}

	wsec = htod32(encryption);
	if (wlu_set(irh, WLC_SET_WSEC, &wsec, sizeof(wsec)) < 0) {
		printf("wlmSecuritySet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmJoinNetwork(const char* ssid, WLM_JOIN_MODE mode)
{
	wlc_ssid_t wlcSsid;
	int infra = htod32(mode);
	if (wlu_set(irh, WLC_SET_INFRA, &infra, sizeof(int)) < 0) {
	    printf("wlmJoinNetwork: %s\n", wlmLastError());
	    return FALSE;
	}

	wlcSsid.SSID_len = htod32(strlen(ssid));
	memcpy(wlcSsid.SSID, ssid, wlcSsid.SSID_len);

	if (wlu_set(irh, WLC_SET_SSID, &wlcSsid, sizeof(wlc_ssid_t)) < 0) {
		printf("wlmJoinNetwork: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmDisassociateNetwork(void)
{
	if (wlu_set(irh, WLC_DISASSOC, NULL, 0) < 0) {
		printf("wlmDisassociateNetwork: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmSsidGet(char *ssid, int length)
{
	wlc_ssid_t wlc_ssid;

	if (length < SSID_FMT_BUF_LEN) {
		printf("wlmSsidGet: Ssid buffer too short - %d bytes at least\n",
		SSID_FMT_BUF_LEN);
		return FALSE;
	}

	/* query for 'ssid' */
	if (wlu_get(irh, WLC_GET_SSID, &wlc_ssid, sizeof(wlc_ssid_t))) {
		printf("wlmSsidGet: %s\n", wlmLastError());
		return FALSE;
	}

	wl_format_ssid(ssid, wlc_ssid.SSID, dtoh32(wlc_ssid.SSID_len));

	return TRUE;
}

int wlmBssidGet(char *bssid, int length)
{
	struct ether_addr ea;

	if (length != ETHER_ADDR_LEN) {
		printf("wlmBssiGet: bssid requires %d bytes", ETHER_ADDR_LEN);
		return FALSE;
	}

	if (wlu_get(irh, WLC_GET_BSSID, &ea, ETHER_ADDR_LEN) == 0) {
		/* associated - format and return bssid */
		strncpy(bssid, wl_ether_etoa(&ea), length);
	}
	else {
		/* not associated - return empty string */
		memset(bssid, 0, length);
	}

	return TRUE;
}


int wlmGlacialTimerSet(int val)
{
	if (wlu_iovar_setint(irh, "glacial_timer", val)) {
		printf("wlmGlacialTimerSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmFastTimerSet(int val)
{
	if (wlu_iovar_setint(irh, "fast_timer", val)) {
		printf("wlmFastTimerSet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmSlowTimerSet(int val)
{
	if (wlu_iovar_setint(irh, "slow_timer", val)) {
		printf("wlmGlacialTimerSet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}


int wlmScanSuppress(int on)
{
	int val;
	if (on)
		val = 1;
	else
		val = 0;

	if (wlu_set(irh, WLC_SET_SCANSUPPRESS, &val, sizeof(int))) {
		printf("wlmSetScansuppress: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmCountryCodeSet(const char * country_name)
{
	wl_country_t cspec;
	int err;

	memset(&cspec, 0, sizeof(cspec));
	cspec.rev = -1;

	/* arg matched a country name */
	memcpy(cspec.country_abbrev, country_name, WLC_CNTRY_BUF_SZ);
	err = 0;

	/* first try the country iovar */
	if (cspec.rev == -1 && cspec.ccode[0] == '\0')
		err = wlu_iovar_set(irh, "country", &cspec, WLC_CNTRY_BUF_SZ);
	else
		err = wlu_iovar_set(irh, "country", &cspec, sizeof(cspec));

	if (err == 0)
		return TRUE;
	return FALSE;
}

int wlmFullCal(void)
{
	if (wlu_var_setbuf(irh, "lpphy_fullcal", NULL, 0)) {
		printf("wlmLPPY_FULLCAL: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmIoctlGet(int cmd, void *buf, int len)
{
	if (wlu_get(irh, cmd, buf, len)) {
		printf("wlmIoctlGet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmIoctlSet(int cmd, void *buf, int len)
{
	if (wlu_set(irh, cmd, buf, len)) {
		printf("wlmIoctlSet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmIovarGet(const char *iovar, void *buf, int len)
{
	if (wlu_iovar_get(irh, iovar, buf, len)) {
		printf("wlmIovarGet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmIovarSet(const char *iovar, void *buf, int len)
{
	if (wlu_iovar_set(irh, iovar, buf, len)) {
		printf("wlmIovarSet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmIovarIntegerGet(const char *iovar, int *val)
{
	if (wlu_iovar_getint(irh, iovar, val)) {
		printf("wlmIovarIntegerGet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmIovarIntegerSet(const char *iovar, int val)
{
	if (wlu_iovar_setint(irh, iovar, val)) {
		printf("wlmIovarIntegerSet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmIovarBufferGet(const char *iovar, void *param, int param_len, void **bufptr)
{
	if (wlu_var_getbuf(irh, iovar, param, param_len, bufptr)) {
		printf("wlmIovarBufferGet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmIovarBufferSet(const char *iovar, void *param, int param_len)
{
	if (wlu_var_setbuf(irh, iovar, param, param_len)) {
		printf("wlmIovarBufferSet: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmCga5gOffsetsSet(char* values, int len)
{
	if (len != CGA_5G_OFFSETS_LEN) {
		printf("wlmCga5gOffsetsSet() requires a %d-value array as a parameter\n",
		      CGA_5G_OFFSETS_LEN);
		return FALSE;
	}

	if ((wlu_var_setbuf(irh, "sslpnphy_cga_5g", values,
	                    CGA_5G_OFFSETS_LEN * sizeof(int8))) < 0) {
		printf("wlmCga5gOffsetsSet(): Error setting offset values (%s)\n",  wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmCga5gOffsetsGet(char* buf, int len)
{
	if (len != CGA_5G_OFFSETS_LEN) {
		printf("wlmCga5gOffsetsGet() requires a %d-value array as a parameter\n",
		       CGA_5G_OFFSETS_LEN);
		return FALSE;
	}

	if ((wlu_iovar_get(irh, "sslpnphy_cga_5g", buf, CGA_5G_OFFSETS_LEN * sizeof(int8))) < 0) {
		printf("wlmCga5gOffsetsGet(): Error setting offset values (%s)\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmCga2gOffsetsSet(char* values, int len)
{
	if (len != CGA_2G_OFFSETS_LEN) {
		printf("wlmCga2gOffsetsSet(): requires a %d-value array as a parameter\n",
		       CGA_2G_OFFSETS_LEN);
		return FALSE;
	}

	if ((wlu_var_setbuf(irh, "sslpnphy_cga_2g", values,
	                    CGA_2G_OFFSETS_LEN * sizeof(int8))) < 0) {
		printf("wlmCga2gOffsetsSet(): Error setting offset values (%s)\n", wlmLastError());
		return FALSE;
	}

	return TRUE;

}

int wlmCga2gOffsetsGet(char* buf, int len)
{
	if (len != CGA_2G_OFFSETS_LEN) {
		printf("wlmCga2gOffsetsGet(): requires a %d-value array as a parameter\n",
		       CGA_2G_OFFSETS_LEN);
		return FALSE;
	}

	if ((wlu_iovar_get(irh, "sslpnphy_cga_2g", buf, CGA_2G_OFFSETS_LEN * sizeof(int8))) < 0) {
		printf("wlmCga2gOffsetsGet(): Error setting offset values (%s)\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

#ifdef SERDOWNLOAD
/* parsed values are the filename of the firmware and the args file */
int wlmDhdDownload(const char* firmware, const char* vars)
{
	char *args[3] = {0};
	bool ret;

	if (!firmware) {
		printf("Missing firmware path/filename\n");
		return FALSE;
	}

	if (!vars) {
		printf("Missing vars file\n");
		return FALSE;
	}

	args[0] = malloc(sizeof(char*) * (strlen(" ")+1));
	args[1] = malloc(sizeof(char*) * (strlen(firmware)+1));
	args[2] = malloc(sizeof(char*) * (strlen(vars)+1));

	if (args[0] == NULL || args[1] == NULL || args[2] == NULL) {
		ret = FALSE;
		printf("Malloc failures, aborting download\n");
		goto cleanup;
	}

	memset(args[0], 0, sizeof(char*) * (strlen(" ")+1));
	memset(args[1], 0, sizeof(char*) * (strlen(firmware)+1));
	memset(args[2], 0, sizeof(char*) * (strlen(vars)+1));

	strncpy(args[0], " ", strlen(" "));
	strncpy(args[1], firmware, strlen(firmware));
	strncpy(args[2], vars, strlen(vars));

	printf("downloading firmware...\n");

	if (dhd_download(irh, 0, (char **) args)) {
		printf("wlmDhdDownload for firmware %s and vars file %s failed\n", firmware, vars);
		ret = FALSE;
	}

	printf("firmware download complete\n");
	    ret = TRUE;

cleanup:
	    if (args[0] != NULL)
		    free(args[0]);
	    if (args[1] != NULL)
		    free(args[1]);
	    if (args[2] != NULL)
		    free(args[2]);

	    return ret;

}

/* only parsed argv value is the chip string */

int wlmDhdInit(const char *chip)
{
	bool ret;
	char *args[2] = {0};

	args[0] = malloc(sizeof(char*) * (strlen(" ")+1));
	args[1] = malloc(sizeof(char*) * (strlen(chip)+1));

	if (args[0] == NULL || args[1] == NULL) {
		ret = FALSE;
		printf("wlmDhdInit: Malloc failures, aborting download\n");
		goto cleanup;
	}

	memset(args[0], 0, sizeof(char*) * (strlen(" ")+1));
	memset(args[1], 0, sizeof(char*) * (strlen(" ")+1));

	strncpy(args[0], " ", strlen(" "));
	strncpy(args[1], chip, strlen(chip));

	printf("wlmDhdInit: initializing firmware download...\n");

	if (dhd_init(irh, 0, (char **)args)) {
		printf("wlmDhdInit for chip %s failed\n", chip);
		ret = FALSE;
	}
	    ret = TRUE;

cleanup:
	    if (args[0] != NULL)
		    free(args[0]);
	    if (args[1] != NULL)
		    free(args[1]);

	    return ret;
}
#endif /* SERDOWNLOAD */

int wlmRadioOn(void)
{
	int val;

	/* val = WL_RADIO_SW_DISABLE << 16; */
	val = (1<<0) << 16;

	if (wlu_set(irh, WLC_SET_RADIO, &val, sizeof(int)) < 0) {
		printf("wlmRadioOn: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRadioOff(void)
{
	int val;

	/* val = WL_RADIO_SW_DISABLE << 16 | WL_RADIO_SW_DISABLE; */
	val = (1<<0) << 16 | (1<<0);

	if (wlu_set(irh, WLC_SET_RADIO, &val, sizeof(int)) < 0) {
		 printf("wlmRadioOff: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmPMmode(int val)
{
	if (val < 0 || val > 2) {
	        printf("wlmPMmode: setting for PM mode out of range [0,2].\n");
		/* 0: CAM constant awake mode */
		/* 1: PS (Power save) mode */
		/* 2: Fast PS mode */
	}

	if (wlu_set(irh, WLC_SET_PM, &val, sizeof(int)) < 0) {
		printf("wlmPMmode: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRoamingOn(void)
{
	if (wlu_iovar_setint(irh, "roam_off", 0) < 0) {
		printf("wlmRoamingOn: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRoamingOff(void)
{
	if (wlu_iovar_setint(irh, "roam_off", 1) < 0) {
		printf("wlmRoamingOff: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRoamTriggerLevelGet(int *val, WLM_BAND band)
{
	struct {
		int val;
		int band;
	} x;

	x.band = htod32(band);
	x.val = -1;

	if (wlu_get(irh, WLC_GET_ROAM_TRIGGER, &x, sizeof(x)) < 0) {
		printf("wlmRoamTriggerLevelGet: %s\n", wlmLastError());
		return FALSE;
	}

	*val = htod32(x.val);

	return TRUE;
}

int wlmRoamTriggerLevelSet(int val, WLM_BAND band)
{
	struct {
		int val;
		int band;
	} x;

	x.band = htod32(band);
	x.val = htod32(val);

	if (wlu_set(irh, WLC_SET_ROAM_TRIGGER, &x, sizeof(x)) < 0) {
		printf("wlmRoamTriggerLevelSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmFrameBurstOn(void)
{
	int val = 1;

	if (wlu_set(irh, WLC_SET_FAKEFRAG, &val, sizeof(int)) < 0) {
		printf("wlmFrameBurstOn: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmFrameBurstOff(void)
{
	int val = 0;

	if (wlu_set(irh, WLC_SET_FAKEFRAG, &val, sizeof(int)) < 0) {
		printf("wlmFrameBurstOff: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}


int wlmBeaconIntervalSet(int val)
{
	val = htod32(val);
	if (wlu_set(irh, WLC_SET_BCNPRD, &val, sizeof(int)) < 0) {
		printf("wlmBeaconIntervalSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmAMPDUModeSet(int val)
{
	val = htod32(val);
	if (wlu_iovar_setint(irh, "ampdu", val) < 0) {
		printf("wlmAMPDUModeSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmMIMOBandwidthCapabilitySet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "mimo_bw_cap", val) < 0) {
		printf("wlmMIMOBandwidthCapabilitySet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmInterferenceSet(int val)
{
	val = htod32(val);

	if (val < 0 || val > 4) {
		printf("wlmInterferenceSet: interference setting out of range [0, 4]\n");
		return FALSE;
	}

	if (wlu_set(irh, WLC_SET_INTERFERENCE_MODE, &val, sizeof(int)) < 0) {
		printf("wlmInterferenceSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmInterferenceOverrideSet(int val)
{
	val = htod32(val);

	if (val < 0 || val > 4) {
		printf("wlmInterferenceOverrideSet: interference setting out of range [0, 4]\n");
		return FALSE;
	}

	if (wlu_set(irh, WLC_SET_INTERFERENCE_OVERRIDE_MODE, &val, sizeof(int)) < 0) {
		printf("wlmInterferenceOverrideSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmTransmitBandwidthSet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "mimo_txbw", val) < 0) {
		printf("wlmTransmitBadnwidthSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmShortGuardIntervalSet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "sgi_tx", val) < 0) {
		printf("wlmShortGuardIntervalSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmObssCoexSet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "obss_coex", val) < 0) {
		printf("wlmObssCoexSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmPHYPeriodicalCalSet(void)
{
	if (wlu_iovar_setint(irh, "phy_percal", 0) < 0) {
		printf("wlmPHYPeriodicalCalSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmPHYForceCalSet(void)
{
	if (wlu_iovar_setint(irh, "phy_forcecal", 0) < 0) {
		printf("wlmPHYForceCalSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmPHYScramblerUpdateDisable(void)
{
	if (wlu_iovar_setint(irh, "phy_scraminit", 0x7f) < 0) {
		printf("wlmPHYScramblerUpdateDisable: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmPHYScramblerUpdateEnable(void)
{
	if (wlu_iovar_setint(irh, "phy_scraminit", -1) < 0) {
		printf("wlmPHYScramblerUpdateEnable: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmPHYWatchdogSet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "phy_watchdog", val) < 0) {
	        printf("wlmPHYWatchdogSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmTemperatureSensorDisable(void)
{
	int val = 1; /* 0 = temp sensor enabled; 1 = temp sensor disabled */
	if (wlu_iovar_setint(irh, "tempsense_disable", val) < 0) {
		printf("wlmTempSensorDisable %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmTemperatureSensorEnable(void)
{
	int val = 0; /* 0 = temp sensor enabled; 1 = temp sensor disabled */
	if (wlu_iovar_setint(irh, "tempsense_disable", val) < 0) {
		printf("wlmTempSensorEnable %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmTransmitCoreSet(int val)
{	val = htod32(val);

	if (wlu_iovar_setint(irh, "txcore", val) < 0) {
		printf("wlmTransmitCoreSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmPhyTempSenseGet(int *val)
{
	if (wlu_iovar_getint(irh, "phy_tempsense", val) < 0) {
		printf("wlmPhyTempSense: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmOtpFabidGet(int *val)
{
	if (wlu_iovar_getint(irh, "otp_fabid", val)) {
		printf("wlmOtpFabid: %s\n", wlmLastError());
		return FALSE;
	}
	return TRUE;
}

int wlmChannelSpecSet(int channel, int bandwidth, int sideband)
{
	chanspec_t chanspec = 0;

	if (channel > 224) {
		printf("wlmChannelSpecSet: %d is invalid channel\n", channel);
		return FALSE;
	} else
		chanspec |= channel;

	if (channel > 14)
		chanspec |= WL_CHANSPEC_BAND_5G;
	else
		chanspec |= WL_CHANSPEC_BAND_2G;

	if ((bandwidth != 20) && (bandwidth != 40)) {
		printf("wlChannelSpecSet: %d is invalid channel bandwidth.\n", bandwidth);
		return FALSE;
	}

	if (bandwidth == 20)
		chanspec |= WL_CHANSPEC_BW_20;
	else
		chanspec |= WL_CHANSPEC_BW_40;

	if ((sideband != -1) && (sideband != 1) && (sideband != 0)) {
		printf("wlmChannelSpecSet: %d is invalid channel sideband.\n", sideband);
		return FALSE;
	}

	if (sideband == -1)
		chanspec |= WL_CHANSPEC_CTL_SB_LOWER;
	else if (sideband == 1)
		chanspec |= WL_CHANSPEC_CTL_SB_UPPER;
	else
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;

	if (wlu_iovar_setint(irh, "chanspec", (int) chanspec) < 0) {
		printf("wlmChannelSpecSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRtsThresholdOverride(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "rtsthresh", val) < 0) {
	        printf("wlmRtsThresholdOverride: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}


int wlmSTBCTxSet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "stbc_tx", val) < 0) {
	        printf("wlmSTBCTxSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}


int wlmSTBCRxSet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "stbc_rx", val) < 0) {
	        printf("wlmSTBCRxSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}


int wlmTxChainSet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "txchain", val) < 0) {
	        printf("wlmTxChainSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRxChainSet(int val)
{
	val = htod32(val);

	if (wlu_iovar_setint(irh, "rxchain", val) < 0) {
	        printf("wlmRxChainSet: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}

int wlmRxIQEstGet(float *val, int sampleCount, int ant)
{
	uint32 rxiq;
	int sample_count = sampleCount;  /* [0, 16], default: maximum 15 sample counts */
	int antenna = ant ;       /* [0, 3], default: antenna 0 */
	int err;
	uint8 resolution = 1;     /* resolution default to 0.25dB */ 
	float x, y;

	/* default: resolution 1 (coarse), samples = 1024 (2^10) and antenna 3 */

	rxiq = (10 << 8) | 3;
	if ((sample_count < 0) || (sample_count > 16)) {
		printf("wlmRxIQGet: SampleCount out of range of [0, 15].\n");
		return FALSE;
	} else {
		rxiq = (((sample_count & 0xff) << 8) | (rxiq & 0xff));
	}

	if ((antenna < 0) || (antenna > 3)) {
		printf("wlmRxIQGet: Antenna out of range of [0, 3].\n");
		return FALSE;
	} else {
		rxiq = ((rxiq & 0xff00) | (antenna & 0xff));
	}

	if ((err = wlu_iovar_setint(irh, "phy_rxiqest", (int) rxiq)) < 0) {
		printf("wlmRxIQGet: %s\n", wlmLastError());
		return FALSE;
	}

	if ((err = wlu_iovar_getint(irh, "phy_rxiqest", (int*)&rxiq)) < 0) {
		printf("wlmRxIQGet: %s\n", wlmLastError());
		return FALSE;
	}

	if (resolution == 1) {
		/* fine resolutin power reporting (0.25dB resolution) */
		if (rxiq >> 20) {
		} else if (rxiq >> 10) {
		} else {
			/* 1-chain specific */
			int16 tmp;
			tmp = (rxiq & 0x3ff);
			tmp = ((int16)(tmp << 6)) >> 6; /* sing extension */
			if (tmp < 0) {
				tmp = -1 * tmp;
			}

			x = (float)(tmp >> 2);
			y = (float)(tmp & 0x3);

			*val = (x + y * 25 /100) * (-1);
		}
	}
	return TRUE;
}

int wlmPHYTxPowerIndexGet(unsigned int *val, const char *chipid)
{
	uint32 power_index = -1;
	uint32 txpwridx[4] = {0};
	int chip = atoi(chipid);

	switch (chip) {
	        case 4329:
		case 43291:
			if (wlu_iovar_getint(irh, "sslpnphy_txpwrindex", &power_index) < 0) {
				printf("wlmPHYTxPowerIndexGet: %s\n", wlmLastError());
				return FALSE;
			}
			*val = power_index;
			break;
	        case 4325:
			if (wlu_iovar_getint(irh, "lppphy_txpwrindex", &power_index) < 0) {
				printf("wlmPHYTxPowerIndexGet: %s\n", wlmLastError());
				return FALSE;
			}
			*val = power_index;
			break;
	        default:
		  if (wlu_iovar_getint(irh, "phy_txpwrindex", (int*)&txpwridx[0]) < 0) {
			  printf("wlmPHYTxPowerIndexGet: %s\n", wlmLastError());
			  return FALSE;
		  }
		  txpwridx[0] = dtoh32(txpwridx[0]);
		  *val = txpwridx[0];
		  break;
	}

	return TRUE;
}

int wlmPHYTxPowerIndexSet(unsigned int val, const char *chipid)
{
	uint32 power_index = -1;
	uint32 txpwridx[4] = {0};
	int chip = atoi(chipid);

	power_index = dtoh32(val);
	switch (chip) {
	        case 4329:
	        case 43291:
			if (wlu_iovar_setint(irh, "sslpnphy_txpwrindex", power_index) < 0) {
				printf("wlmPHYTxPowerIndexSet: %s\n", wlmLastError());
				return FALSE;
			}
			break;
	        case 4325:
			if (wlu_iovar_setint(irh, "lppphy_txpwrindex", power_index) < 0) {
				printf("wlmPHYTxPowerIndexSet: %s\n", wlmLastError());
				return FALSE;
			}
			break;
	        default:
			txpwridx[0] = (int8) (power_index & 0xff);
			txpwridx[1] = (int8) ((power_index >> 8) & 0xff);
			txpwridx[2] = (int8) ((power_index >> 16) & 0xff);
			txpwridx[3] = (int8) ((power_index >> 24) & 0xff);

			if (wlu_var_setbuf(irh, "phy_txpwrindex", txpwridx, 4*sizeof(uint32)) < 0) {
				printf("wlmPHYTxPowerIndexSet: %s\n", wlmLastError());
				return FALSE;
			}
			break;
	}

	return TRUE;
}

int wlmRIFSEnable(int enable)
{
	int val, rifs;

	val = rifs = htod32(enable);
	if (rifs != 0 && rifs != 1) {
		printf("wlmRIFSEnable: Usage: input must be 0 or 1\n");
		return FALSE;
	}

	if (wlu_set(irh, WLC_SET_FAKEFRAG, &val, sizeof(int)) < 0) {
		printf("wlmRIFSEnable: %s\n", wlmLastError());
		return FALSE;
	}

	if (wlu_iovar_setint(irh, "rifs", (int)rifs) < 0) {
		printf("wlmRIFSEnable: %s\n", wlmLastError());
		return FALSE;
	}

	return TRUE;
}
